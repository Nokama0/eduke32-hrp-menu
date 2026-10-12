// Finding DukePlayer_t, which is HEAP-allocated (game.cpp:6433), so no image search can find it. Anchor: the player's
// sprite (player.cpp:5807 copies pos into it every tic); scan private read-write memory for its exact x,y, confirm `i`.
// z is NOT matched (spritezoffset, :5806). Then one image pass finds the slot holding the address, `g_player[0].ps`.

#include <windows.h>
#include "log.h"
#include "gamestate.h"
#include "playerstate.h"
#include "aimsolve.h"
#include "config.h"
#include "freecam.h"
#include "mapextent.h"

// The struct and the image slot pointing at it: reads prefer s_slot; s_ps is the scan's fallback.
static const unsigned char* volatile s_ps = NULL;
static const unsigned char* const volatile* volatile s_slot = NULL;
static volatile LONG s_slotRva = 0;

// Eight scans then stop: one that failed with a live sprite in hand keeps failing.
static const int EK_PS_MAX_SCANS = 8;
static int s_heavyScans = 0;

struct Region { const unsigned char* base; size_t size; };
static Region s_regions[8192];
static int s_regionCount = 0;
static int s_regionOverflow = 0;

// Every committed, private, PAGE_READWRITE region in the process: the heap is private while art data, GRP and DLLs are
// MEM_MAPPED or MEM_IMAGE and dwarf it; a malloc'd block is never executable, copy-on-write or guarded.
static void enumerate_regions(void) {
  s_regionCount = 0;
  s_regionOverflow = 0;

  SYSTEM_INFO si;
  GetSystemInfo(&si);
  const unsigned char* p = (const unsigned char*)si.lpMinimumApplicationAddress;
  const unsigned char* const end = (const unsigned char*)si.lpMaximumApplicationAddress;
  const int cap = (int)(sizeof(s_regions) / sizeof(s_regions[0]));

  while (p < end) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
    if (mbi.RegionSize == 0) break;
    const unsigned char* rbase = (const unsigned char*)mbi.BaseAddress;

    if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
        mbi.Protect == PAGE_READWRITE) {
      Region* last = s_regionCount ? &s_regions[s_regionCount - 1] : NULL;
      if (last && last->base + last->size == rbase) last->size += mbi.RegionSize;
      else if (s_regionCount < cap) {
        s_regions[s_regionCount].base = rbase;
        s_regions[s_regionCount].size = mbi.RegionSize;
        s_regionCount++;
      } else s_regionOverflow++;
    }

    const unsigned char* next = rbase + mbi.RegionSize;
    if (next <= p) break;
    p = next;
  }
}

static int find_player_sprite(int* xOut, int* yOut) {
  for (int i = 0; i < EK_MAXSPRITES; i++) {
    const unsigned char* r = gamestate_sprite(i);
    if (!r) break;
    if (!ekm_is_player_sprite(*(const short*)(r + EK_SPR_PICNUM),
                              *(const short*)(r + EK_SPR_STATNUM))) continue;
    if (xOut) *xOut = *(const int*)(r + EK_SPR_X);
    if (yOut) *yOut = *(const int*)(r + EK_SPR_Y);
    return i;
  }
  return -1;
}

// Guarded: another thread may free a region between enumeration and read.
static const unsigned char* scan_regions(int px, int py, int spr, size_t* bytesOut) {
  const unsigned char* found = NULL;
  size_t bytes = 0;

  for (int r = 0; r < s_regionCount && !found; r++) {
    const unsigned char* const base = s_regions[r].base;
    const size_t span = s_regions[r].size;
    if (span < (size_t)EK_PS_SIZE) continue;
    bytes += span;

    __try {
      for (size_t off = 0; off + EK_PS_SIZE <= span; off += 4) {
        const unsigned char* const cand = base + off;

        if (*(const int*)(cand + EK_PS_POS + 0) != px) continue;
        if (*(const int*)(cand + EK_PS_POS + 4) != py) continue;

        // The independent confirmation: the position could appear in a network buffer or a struct copy; `i` could not.
        if (*(const short*)(cand + EK_PS_I) != (short)spr) continue;

        found = cand;
        break;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
  }

  if (bytesOut) *bytesOut = bytes;
  return found;
}

// The image qword holding `addr` (`g_player[0].ps`, player.h:271). Stride 1, not 8: playerdata_t is packed to 133 bytes
// so the slot is never 8-aligned (playerstate.h static_asserts it). A raw match proves nothing; a slot flanked by NULLs
// one playerdata_t either side is inside the array.
static const unsigned char* const volatile* find_image_slot(const unsigned char* addr,
                                                            unsigned* rvaOut,
                                                            int* hitsOut, int* shapedOut) {
  const unsigned char* const base = (const unsigned char*)GetModuleHandleA(NULL);
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
  const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;

  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  const int nsec = nt->FileHeader.NumberOfSections;

  const unsigned char* firstHit = NULL;  unsigned firstRva = 0;
  const unsigned char* shaped = NULL;    unsigned shapedRva = 0;
  int hits = 0, shapedHits = 0;

  for (int i = 0; i < nsec; i++) {
    if (!(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
    const unsigned char* const start = base + sec[i].VirtualAddress;
    const DWORD span = sec[i].Misc.VirtualSize;
    if (span < sizeof(void*)) continue;

    __try {
      for (DWORD off = 0; off + sizeof(void*) <= span; off++) {
        const unsigned char* const at = start + off;
        if (*(const unsigned char* const*)at != addr) continue;
        hits++;
        if (!firstHit) { firstHit = at; firstRva = (unsigned)(sec[i].VirtualAddress + off); }

        if (off < (DWORD)EK_PD_SIZE) continue;
        if ((size_t)off + (size_t)EK_PD_SIZE * (EK_MAXPLAYERS - 1) + sizeof(void*) > span) continue;

        const void* players[EK_MAXPLAYERS];
        for (int k = 0; k < EK_MAXPLAYERS; k++)
          players[k] = *(const void* const*)(at + (size_t)EK_PD_SIZE * k);
        if (!ekm_slot_run_ok(*(const void* const*)(at - EK_PD_SIZE), players,
                             EK_MAXPLAYERS)) continue;
        shapedHits++;
        if (!shaped) { shaped = at; shapedRva = (unsigned)(sec[i].VirtualAddress + off); }
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
  }

  if (hitsOut) *hitsOut = hits;
  if (shapedOut) *shapedOut = shapedHits;

  if (shapedHits == 1) { if (rvaOut) *rvaOut = shapedRva; return (const unsigned char* const volatile*)shaped; }
  if (shapedHits == 0 && hits == 1) { if (rvaOut) *rvaOut = firstRva; return (const unsigned char* const volatile*)firstHit; }
  return NULL;
}

bool player_scan(void) {
  if (s_ps) return true;
  if (!gamestate_ready()) return false;

  int px = 0, py = 0;
  const int spr = find_player_sprite(&px, &py);
  if (spr < 0) return false;

  if (px == 0 && py == 0) return false;

  if (s_heavyScans >= EK_PS_MAX_SCANS) return false;
  s_heavyScans++;

  const DWORD t0 = GetTickCount();
  enumerate_regions();
  size_t bytes = 0;
  const unsigned char* const best = scan_regions(px, py, spr, &bytes);
  const DWORD ms = GetTickCount() - t0;

  if (!best) {
    ek_log("player: scan %d/%d found nothing -- sprite %d at (%d,%d), "
           "%d region(s)%s covering %llu bytes, %lu ms",
           s_heavyScans, EK_PS_MAX_SCANS, spr, px, py, s_regionCount,
           s_regionOverflow ? " (TABLE FULL)" : "", (unsigned long long)bytes, ms);
    if (s_heavyScans >= EK_PS_MAX_SCANS)
      ek_log("player: giving up; nothing in this port will use the player struct");
    return false;
  }

  unsigned slotRva = 0;
  int hits = 0, shapedHits = 0;
  const unsigned char* const volatile* const slot =
      find_image_slot(best, &slotRva, &hits, &shapedHits);

  // Published slot first: player_ready() gates on s_ps. The sprite index is NOT published (once it was, and a stale
  // compare refused for the session after a level transition); every consumer re-reads ps->i.
  InterlockedExchange(&s_slotRva, (LONG)slotRva);
  InterlockedExchangePointer((void* volatile*)&s_slot, (void*)slot);
  InterlockedExchangePointer((void* volatile*)&s_ps, (void*)best);

  ek_log("player: DukePlayer_t at %p (heap), anchored on sprite %d at (%d,%d); "
         "%d region(s), %llu bytes, %lu ms",
         (const void*)best, spr, px, py, s_regionCount,
         (unsigned long long)bytes, ms);
  if (slot)
    ek_log("player:   g_player[0].ps at rva 0x%07X -- reads go through it "
           "(%d raw hit(s), %d of the right shape)", slotRva, hits, shapedHits);
  else
    ek_log("player:   no usable image pointer (%d raw hit(s), %d of the right "
           "shape); reading the heap address directly", hits, shapedHits);

  PlayerSnap s;
  if (player_snap(&s))
    ek_log("player:   ang=%d (%d deg) horiz=%d sect=%d",
           s.ang, ekm_q16ang_degrees(s.ang), s.horiz, s.sect);
  return true;
}

// Prefer the slot, and BELIEVE a NULL in it: G_MaybeAllocPlayer only allocates, so null means the player was freed and
// the cached address dangles. Only a scan that never found a slot uses the cached address.
static const unsigned char* player_ptr(void) {
  const unsigned char* const volatile* const slot = s_slot;
  if (!slot) return s_ps;
  const unsigned char* const p = *slot;
  return (p && ((UINT_PTR)p & 7) == 0) ? p : NULL;
}

// Every live read goes through a guard: a heap pointer cannot be bounds-checked like an rva, and game.cpp:5977 frees
// every g_player[i].ps at shutdown WITHOUT nulling it while the sampler runs on. SEH costs nothing until it faults.
bool player_snap(PlayerSnap* out) {
  const unsigned char* const p = player_ptr();
  if (!p || !out) return false;
  __try {
    out->x     = *(const int*)(p + EK_PS_POS + 0);
    out->y     = *(const int*)(p + EK_PS_POS + 4);
    out->z     = *(const int*)(p + EK_PS_POS + 8);
    out->ang   = *(const int*)(p + EK_PS_Q16ANG);
    out->horiz = *(const int*)(p + EK_PS_Q16HORIZ);
    out->horizoff = *(const int*)(p + EK_PS_Q16HORIZOFF);
    out->sect  = *(const short*)(p + EK_PS_CURSECTNUM);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

bool player_read_draw_mode(int* newowner, int* overShoulderOn) {
  const unsigned char* const p = player_ptr();
  if (!p || !newowner || !overShoulderOn) return false;
  __try {
    *newowner      = (int)*(const short*)(p + EK_PS_NEWOWNER);
    *overShoulderOn = (int)*(const unsigned char*)(p + EK_PS_OVER_SHOULDER_ON);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

bool player_read_gm(int* out) {
  const unsigned char* const p = player_ptr();
  if (!p || !out) return false;
  __try {
    *out = (int)*(const unsigned char*)(p + EK_PS_GM);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

bool player_read_weapon(int* out) {
  const unsigned char* const p = player_ptr();
  if (!p || !out) return false;
  __try {
    *out = (int)*(const signed char*)(p + EK_PS_CURR_WEAPON);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

// The one write is a field the engine owns: P_UpdateAngles writes q16ang/q16horiz every frame (player.cpp:3080), and
// the tic carries them to the o-prefixed pair. Guarded: the struct is freed without nulling.
bool player_write_view(int q16ang, int q16horiz, bool alsoOld) {
  unsigned char* const p = (unsigned char*)player_ptr();
  if (!p) return false;
  __try {
    *(int*)(p + EK_PS_Q16ANG)   = q16ang;
    *(int*)(p + EK_PS_Q16HORIZ) = q16horiz;
    if (alsoOld) {
      *(int*)(p + EK_PS_OQ16ANG)   = q16ang;
      *(int*)(p + EK_PS_OQ16HORIZ) = q16horiz;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

bool player_read_weapon_anim(int* weapon, int* kickback, int* weaponPos) {
  const unsigned char* const p = player_ptr();
  if (!p || !weapon || !kickback || !weaponPos) return false;
  __try {
    *weapon    = (int)*(const signed char*)(p + EK_PS_CURR_WEAPON);
    *kickback  = (int)*(const unsigned char*)(p + EK_PS_KICKBACK);
    *weaponPos = (int)*(const signed char*)(p + EK_PS_WEAPON_POS);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

bool player_write_kickback(int v) {
  unsigned char* const p = (unsigned char*)player_ptr();
  if (!p) return false;
  __try {
    *(unsigned char*)(p + EK_PS_KICKBACK) = (unsigned char)v;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

bool player_write_weapon_pos(int v) {
  unsigned char* const p = (unsigned char*)player_ptr();
  if (!p) return false;
  __try {
    *(signed char*)(p + EK_PS_WEAPON_POS) = (signed char)v;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

bool player_read_pyoff(int* out) {
  const unsigned char* const p = player_ptr();
  if (!p || !out) return false;
  __try {
    *out = (int)*(const short*)(p + EK_PS_PYOFF);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

// auto_aim is uint8_t (player.h:240). Read-only, for instrumentation: A_FindTargetSprite (player.cpp:221) returns -1
// only at 0, at any other value the engine may move a hitscan shot onto a sprite of its own choosing. The bail is in
// that callee, not GetAutoAimAng.
bool player_read_auto_aim(int* out) {
  const unsigned char* const p = player_ptr();
  if (!p || !out) return false;
  __try {
    *out = (int)*(const unsigned char*)(p + EK_PS_AUTO_AIM);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

// got_access is int16 (player.h:225), masked to three bits: sector.cpp:1315 tests `got_access & (1 << n)` for n in 0..2
// only. The local is filled first and *out written on success only (an unsequenced out-param read broke the teleport).
bool player_read_got_access(int* out) {
  const unsigned char* const p = player_ptr();
  if (!p || !out) return false;
  int got = 0;
  __try {
    got = ((int)*(const short*)(p + EK_PS_GOT_ACCESS)) & 0x07;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  *out = got;
  return true;
}

// opos is three int32s after pos (player.h:185); all three or none, so the caller falls back to no interpolation.
bool player_read_opos(int* x, int* y, int* z) {
  const unsigned char* const p = player_ptr();
  if (!p || !x || !y || !z) return false;
  __try {
    *x = *(const int*)(p + EK_PS_OPOS + 0);
    *y = *(const int*)(p + EK_PS_OPOS + 4);
    *z = *(const int*)(p + EK_PS_OPOS + 8);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

// ps->i read fresh every time: it changes when the player sprite respawns at a level transition, and the struct does
// not move (allocated once, game.cpp:6433), so features recover next frame instead of staying off for the session.
static bool player_read_sprite_index(int* out) {
  const unsigned char* const p = player_ptr();
  if (!p || !out) return false;
  __try {
    *out = (int)*(const short*)(p + EK_PS_I);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

static bool sprite_reads_as_player(int spriteNum) {
  const unsigned char* const r = gamestate_sprite(spriteNum);
  if (!r) return false;
  bool ok = false;
  __try {
    ok = ekm_is_player_sprite(*(const short*)(r + EK_SPR_PICNUM),
                              *(const short*)(r + EK_SPR_STATNUM));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    ok = false;
  }
  return ok;
}

bool player_sprite_matches(int spriteNum) {
  int cur = 0;
  if (!player_read_sprite_index(&cur)) return false;
  if (cur != spriteNum) return false;
  // Not belt and braces: ps->i comes from a content-scanned struct; if that were wrong, this stops a solved yaw landing
  // in an actor's sprite.
  return sprite_reads_as_player(spriteNum);
}

// The player's sprite record, writable, confirmed and never from the latch: a stale index would write into whatever
// actor now holds the slot.
static unsigned char* player_sprite_rec(void) {
  int spr = 0;
  if (!player_read_sprite_index(&spr)) return NULL;
  if (!sprite_reads_as_player(spr)) return NULL;
  return (unsigned char*)gamestate_sprite(spr);
}

// The engine keeps health and "frozen" on the player's SPRITE, not in DukePlayer_t; via player_sprite_rec, so it
// survives a level transition.
bool player_read_body(int* hp, int* pal) {
  const unsigned char* const r = player_sprite_rec();
  if (!r) return false;
  __try {
    if (hp)  *hp  = (int)*(const short*)(r + EK_SPR_EXTRA);
    if (pal) *pal = (int)r[EK_SPR_PAL];
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

// Silent aim's substitution: every field is one the engine writes each tic, put back inside the same engine call
// (playerstate.h). The save is its own guard, before any write: an unreadable struct has nothing to put back.
bool player_write_shot_view(int q16ang, int q16horiz, int autoAim, EkShotSave* save) {
  unsigned char* const p = (unsigned char*)player_ptr();
  if (!p || !save) return false;
  save->sprOk = 0;
  save->sprAng = 0;

  __try {
    save->q16ang    = *(const int*)(p + EK_PS_Q16ANG);
    save->oq16ang   = *(const int*)(p + EK_PS_OQ16ANG);
    save->q16horiz  = *(const int*)(p + EK_PS_Q16HORIZ);
    save->oq16horiz = *(const int*)(p + EK_PS_OQ16HORIZ);
    save->autoAim   = (int)*(const unsigned char*)(p + EK_PS_AUTO_AIM);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }

  unsigned char* const s = player_sprite_rec();
  if (s) {
    __try {
      save->sprAng = (int)*(const short*)(s + EK_SPR_ANG);
      save->sprOk = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      save->sprOk = 0;
    }
  }

  __try {
    *(int*)(p + EK_PS_Q16ANG)    = q16ang;
    *(int*)(p + EK_PS_OQ16ANG)   = q16ang;
    *(int*)(p + EK_PS_Q16HORIZ)  = q16horiz;
    *(int*)(p + EK_PS_OQ16HORIZ) = q16horiz;
    *(unsigned char*)(p + EK_PS_AUTO_AIM) = (unsigned char)autoAim;
    if (save->sprOk && s)
      *(short*)(s + EK_SPR_ANG) = (short)((q16ang >> 16) & (EKM_YAW_UNITS - 1));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    player_restore_shot_view(save);
    return false;
  }
  return true;
}

// Called on EVERY path out of a substituted shot, the faulting one included; reports failure, puts back what it can.
bool player_restore_shot_view(const EkShotSave* save) {
  unsigned char* const p = (unsigned char*)player_ptr();
  if (!p || !save) return false;
  int ok = 1;

  __try {
    *(int*)(p + EK_PS_Q16ANG)    = save->q16ang;
    *(int*)(p + EK_PS_OQ16ANG)   = save->oq16ang;
    *(int*)(p + EK_PS_Q16HORIZ)  = save->q16horiz;
    *(int*)(p + EK_PS_OQ16HORIZ) = save->oq16horiz;
    *(unsigned char*)(p + EK_PS_AUTO_AIM) = (unsigned char)save->autoAim;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    ok = 0;
  }

  if (save->sprOk) {
    unsigned char* const s = player_sprite_rec();
    if (!s) ok = 0;
    else {
      __try {
        *(short*)(s + EK_SPR_ANG) = (short)save->sprAng;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = 0;
      }
    }
  }
  return ok != 0;
}

// The shooter's blocking bits (playerstate.h, hitscan.h). The save is written before the store: the safe direction to
// be wrong is "restore has work it need not do".
bool player_clear_shooter_cstat(int mask, EkShooterCstat* save) {
  if (!save) return false;
  save->rec = NULL;
  save->saved = 0;
  save->held = 0;

  // Confirms ps->i afresh and that the record reads as a live player; otherwise nothing is written.
  unsigned char* const r = player_sprite_rec();
  if (!r) return false;

  __try {
    const int cur = (int)*(const unsigned short*)(r + EK_SPR_CSTAT);
    const int cleared = cur & ~mask;
    if (cleared == cur) return true;
    save->rec = r;
    save->saved = cur;
    save->held = 1;
    *(unsigned short*)(r + EK_SPR_CSTAT) = (unsigned short)cleared;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    if (!save->held) { save->rec = NULL; save->saved = 0; }
    return false;
  }
  return true;
}

bool player_restore_shooter_cstat(const EkShooterCstat* save) {
  if (!save || !save->held || !save->rec) return true;
  int ok = 1;
  // Through the saved pointer, not a second lookup: a lookup answering differently would leave the player permanently
  // non-blocking.
  __try {
    *(unsigned short*)(save->rec + EK_SPR_CSTAT) = (unsigned short)save->saved;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    ok = 0;
  }
  return ok != 0;
}

// Refuses before touching anything and writes all ten fields or none: a half-written pose is worse than a refused one.
bool player_write_pos(const EkTeleportPlan& plan) {
  if (!plan.ok) return false;
  if (plan.sect < 0 || plan.sect >= EK_MAXSECTORS) return false;
  // Against the LIVE map too: player.cpp:5038 rescues only cursectnum == -1, so an index past this map's sector count
  // is trusted by P_ClampZ, changespritesect and the renderer. mapextent's count lags up to a second, which errs
  // harmlessly.
  const int live = mapextent_numsectors();
  if (live > 0 && plan.sect >= live) return false;

  unsigned char* const p = (unsigned char*)player_ptr();
  if (!p) return false;

  __try {
    int* const pos    = (int*)(p + EK_PS_POS);
    int* const opos   = (int*)(p + EK_PS_OPOS);
    int* const bobpos = (int*)(p + EK_PS_BOBPOS);
    pos[0]  = plan.x; pos[1]  = plan.y; pos[2]  = plan.z;
    opos[0] = plan.x; opos[1] = plan.y; opos[2] = plan.z;
    // And the bob origin: the next tic measures xvel as bobpos to pos (player.cpp:5308), so leaving it behind jolts the
    // view bob and weapon sway on arrival.
    bobpos[0] = plan.x; bobpos[1] = plan.y;
    *(short*)(p + EK_PS_CURSECTNUM) = (short)plan.sect;
    *(int*)(p + EK_PS_VELZ) = 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

bool player_ready(void) { return s_ps != NULL; }
unsigned player_rva(void) { return (unsigned)InterlockedCompareExchange(&s_slotRva, 0, 0); }
bool player_q16ang(int* out) {
  PlayerSnap s;
  if (!out || !player_snap(&s)) return false;
  *out = s.ang;
  return true;
}

bool player_q16horiz(int* out) {
  PlayerSnap s;
  if (!out || !player_snap(&s)) return false;
  *out = s.horiz;
  return true;
}

bool player_pos(int* x, int* y, int* z) {
  PlayerSnap s;
  if (!player_snap(&s)) return false;
  if (x) *x = s.x;
  if (y) *y = s.y;
  if (z) *z = s.z;
  return true;
}

// Live sampling for the "Debug log" toggle, only when something moved: proving the struct is the right one needs the
// player to turn and the number to follow.
void player_log_tick(void) {
  if (!g_cfg.debuglog) return;
  PlayerSnap s;
  if (!player_snap(&s)) return;

  static int lastAng = 0, lastHoriz = 0, lastSect = -2;
  if (s.ang == lastAng && s.horiz == lastHoriz && s.sect == lastSect) return;
  lastAng = s.ang; lastHoriz = s.horiz; lastSect = s.sect;

  // Degrees beside the raw fix16: wrapping through 0 to 359 as they turn confirms the field.
  ek_log("player: ang=%d (%d deg) horiz=%d sect=%d",
         s.ang, ekm_q16ang_degrees(s.ang), s.horiz, s.sect);
}
