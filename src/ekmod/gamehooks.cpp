// gamehooks.cpp -- locates P_PreFireHitscan, A_ShootWithZvel and A_IncurDamage by signature (no COFF symbol or
// registry entry, so every number is a search result) and hooks them. Patterns are wildcard-free and the entry comes
// from the exception directory; any ambiguity means no hook. Evidence: docs/re/gamehooks.md.

#include <windows.h>
#include <stdio.h>
#include "log.h"
#include "hook.h"
#include "gamehooks.h"
#include "config.h"
#include "tracer.h"
#include "playerstate.h"
#include "aim.h"
#include "aimsolve.h"
#include "shotray.h"
#include "tbot.h"
#include "wallclock.h"
#include "imagescan.h"
#include "dmgnum.h"

// EVERY ARGUMENT MUST BE RE-PASSED: both targets read arguments five through eight off the stack relative to their own
// entry rsp, so dropping one corrupts the original's frame. vec3_t stays a void*.
typedef void (*PreFireFn)(int spriteNum, int playerNum, int projecTile, void* srcVect,
                          int* zvel, int* shootAng, int accurateAim, int doSpread);
typedef int  (*ShootFn)(int spriteNum, int projecTile, int forceZvel);

// player.cpp:500-501, the hitscan() call.
static const unsigned char EK_PREFIRE_SIG[] = {
  0x41, 0x89, 0xD0,
  0x81, 0xC2, 0x00, 0x02, 0x00, 0x00,
  0x48, 0x03, 0x1E,
  0x41, 0x81, 0xE0, 0xFF, 0x07, 0x00, 0x00,
  0x81, 0xE2, 0xFF, 0x07, 0x00, 0x00,
  0x46, 0x0F, 0xBF, 0x0C, 0x41,
  0x44, 0x0F, 0xBF, 0x04, 0x51,
  0x48, 0x8D, 0x4C, 0x24, 0x50,
  0x0F, 0xBF, 0x53, 0x18,
  0x48, 0x89, 0x4C, 0x24, 0x28,
  0x48, 0x89, 0xF9,
  0xC7, 0x44, 0x24, 0x30, 0x40, 0x00, 0x00, 0x01,
  0x89, 0x44, 0x24, 0x20
};

// player.cpp:1710-1714, the player arm.
static const unsigned char EK_SHOOT_SIG[] = {
  0x41, 0x0F, 0xBF, 0x96, 0xE8, 0x01, 0x00, 0x00,
  0x41, 0xB8, 0x09, 0x03, 0x00, 0x00,
  0x41, 0x8B, 0x46, 0x08,
  0x66, 0x45, 0x89, 0x86, 0x34, 0x02, 0x00, 0x00,
  0x45, 0x8B, 0x26,
  0x45, 0x8B, 0x6E, 0x04,
  0xF3, 0x41, 0x0F, 0x7E, 0x36,
  0x8D, 0xAC, 0x02, 0x00, 0x04, 0x00, 0x00
};

// actors.cpp:1327-1328, the actor arm's exit. rsi is spriteNum, rdi &actor[0]; MSVC would spell it differently, so the
// bytes come from the shipped image.
static const unsigned char EK_INCURDAMAGE_SIG[] = {
  0xBA, 0xFF, 0xFF, 0xFF, 0xFF,
  0x48, 0x8D, 0x04, 0x76,
  0x48, 0xC1, 0xE0, 0x05,
  0x66, 0x89, 0x54, 0x07, 0x4C,
  0x0F, 0xBF, 0x44, 0x07, 0x48
};

// actors.cpp:1319, the VM_OnEvent call: the SECOND fact, from a different statement. It stops before the `call` because
// a rel32 moves with layout.
static const unsigned char EK_INCURDAMAGE_SIG2[] = {
  0xB9, 0xA0, 0x00, 0x00, 0x00,
  0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00
};

static EkHook s_prefireHook;
static EkHook s_shootHook;

// Published like every cross-thread value; the install writes once on the game thread.
static PreFireFn volatile s_prefireOrig = NULL;
static ShootFn   volatile s_shootOrig   = NULL;
static volatile LONG s_prefireRva = 0;
static volatile LONG s_shootRva   = 0;

// Read through the accessor on the RENDER thread while the install writes once on the game thread.
static volatile LONG s_incurRva = 0;

// The gate lives here: the overlay calls this every frame and the work happens once. A refusal is permanent (the image
// does not change).
static bool s_attempted = false;

struct SigHit { unsigned rva; int count; int faulted; };

// Guarded: a section's VirtualSize can run past what is committed.
static bool find_pattern(const unsigned char* base, const unsigned char* sig,
                         size_t len, SigHit* out) {
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  const int nsec = (int)nt->FileHeader.NumberOfSections;

  out->rva = 0;
  out->count = 0;
  out->faulted = 0;

  for (int i = 0; i < nsec; i++) {
    if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
    DWORD span = sec[i].Misc.VirtualSize;
    if (span > sec[i].SizeOfRawData && sec[i].SizeOfRawData != 0) span = sec[i].SizeOfRawData;
    if (span < (DWORD)len) continue;
    const unsigned char* const start = base + sec[i].VirtualAddress;

    __try {
      const DWORD last = span - (DWORD)len;
      for (DWORD off = 0; off <= last; off++) {
        if (start[off] != sig[0]) continue;
        size_t k = 1;
        for (; k < len; k++) if (start[off + k] != sig[k]) break;
        if (k != len) continue;
        if (out->count == 0) out->rva = (unsigned)(sec[i].VirtualAddress + off);
        out->count++;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      // A section that faulted part-way was NOT searched; the partial count must not be reported as complete, so it
      // forces a refusal.
      out->faulted = 1;
    }
  }
  return true;
}

// Occurrences of `sig` in [begin, end), to corroborate a locate inside the function it resolved to; a fault is
// reported, not folded.
static void find_within(const unsigned char* base, unsigned begin, unsigned end,
                        const unsigned char* sig, size_t len, SigHit* out) {
  out->rva = 0;
  out->count = 0;
  out->faulted = 0;
  if (end <= begin || (size_t)(end - begin) < len) return;

  __try {
    const unsigned char* const p = base + begin;
    const size_t last = (size_t)(end - begin) - len;
    for (size_t off = 0; off <= last; off++) {
      if (p[off] != sig[0]) continue;
      size_t k = 1;
      for (; k < len; k++) if (p[off + k] != sig[k]) break;
      if (k != len) continue;
      if (out->count == 0) out->rva = (unsigned)(begin + off);
      out->count++;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    out->faulted = 1;
  }
}

// 0 when no RUNTIME_FUNCTION covers it.
static unsigned entry_containing(const unsigned char* base, unsigned rva) {
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
  const IMAGE_DATA_DIRECTORY* dir =
      &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
  if (dir->VirtualAddress == 0 || dir->Size < sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY))
    return 0;

  unsigned found = 0;
  __try {
    const IMAGE_RUNTIME_FUNCTION_ENTRY* fn =
        (const IMAGE_RUNTIME_FUNCTION_ENTRY*)(base + dir->VirtualAddress);
    const DWORD n = dir->Size / (DWORD)sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY);
    for (DWORD i = 0; i < n; i++) {
      if (fn[i].EndAddress <= fn[i].BeginAddress) continue;
      if (rva < fn[i].BeginAddress || rva >= fn[i].EndAddress) continue;
      found = fn[i].BeginAddress;
      break;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    found = 0;
  }
  return found;
}

// Entry rva of the function containing the one match, or 0 after logging the refusal. `sig2` is a second fact from a
// different statement inside that function's extent: unique is not correct (an inlined fold would match once). NULL
// skips it.
static unsigned locate(const unsigned char* base, const char* what,
                       const unsigned char* sig, size_t len,
                       const unsigned char* sig2, size_t len2) {
  SigHit hit;
  if (!find_pattern(base, sig, len, &hit)) {
    ek_log("gamehooks: %s -- the host module has no PE headers; refusing", what);
    return 0;
  }
  if (hit.count == 0 && !hit.faulted) {
    ek_log("gamehooks: %s -- signature not found in this build; not hooking", what);
    return 0;
  }
  if (hit.count != 1 || hit.faulted) {
    if (hit.faulted)
      ek_log("gamehooks: %s -- a code section faulted mid-scan, so uniqueness "
             "cannot be claimed (%d match(es) before the fault); refusing",
             what, hit.count);
    else
      ek_log("gamehooks: %s -- signature is ambiguous, %d matches, first at rva "
             "0x%07X; refusing to guess", what, hit.count, hit.rva);
    return 0;
  }
  const unsigned entry = entry_containing(base, hit.rva);
  if (entry == 0 || entry > hit.rva) {
    ek_log("gamehooks: %s -- match at rva 0x%07X is not inside any function in "
           "the exception directory; refusing", what, hit.rva);
    return 0;
  }
  if (sig2 == NULL) {
    ek_log("gamehooks: %s -- signature matched once at rva 0x%07X, inside the "
           "function at rva 0x%07X (%+d)", what, hit.rva, entry,
           (int)(hit.rva - entry));
    return entry;
  }

  unsigned begin = 0, end = 0;
  if (!ek_image_extent_containing(base, entry, &begin, &end) || end <= begin) {
    ek_log("gamehooks: %s -- fact 1 matched once at rva 0x%07X but the function "
           "at rva 0x%07X has no extent to corroborate inside; refusing",
           what, hit.rva, entry);
    return 0;
  }
  SigHit second;
  find_within(base, begin, end, sig2, len2, &second);
  if (second.count == 0 || second.faulted) {
    ek_log("gamehooks: %s -- fact 1 matched once at rva 0x%07X inside the "
           "function at rva 0x%07X, but FACT 2 IS ABSENT from its %u bytes%s; "
           "refusing, because one window occurring once does not make it this "
           "function", what, hit.rva, entry, end - begin,
           second.faulted ? " (the range also faulted mid-scan)" : "");
    return 0;
  }
  ek_log("gamehooks: %s -- signature matched once at rva 0x%07X, inside the "
         "function at rva 0x%07X (%+d); corroborated by fact 2, %d time(s) in "
         "that function, first at rva 0x%07X (%+d)", what, hit.rva, entry,
         (int)(hit.rva - entry), second.count, second.rva,
         (int)(second.rva - entry));
  return entry;
}

// Detours delegate, re-passing every argument so the compiler rebuilds the frame the original reads. Perfect Accuracy
// zeroes doSpread by curr_weapon (playerstate.h); a failed read delegates untouched. The logging is instrumentation:
// GetAutoAimAng (player.cpp:480) can move both out-parameters, so they are read going in and out, rate limited in time.
static const unsigned EK_PREFIRE_LOG_MIN_MS = 250;
static unsigned s_prefireLogMs = 0;
static int s_prefireLogHave = 0;

// Staleness diff, instrumentation: the triggerbot validates its ray in the SwapBuffers hook but the weapon fires in the
// tic BEFORE it, so a ray gates the NEXT tic's shot; one line prints both instants. It calls no hook-restricted engine
// helper (hitscan.h) and has its own rate limit.
static const unsigned EK_STALE_LOG_MIN_MS = 300;
static unsigned s_staleLogMs = 0;
static int s_staleLogHave = 0;

static void ek_log_shot_staleness(void) {
  TbotCastRecord rec;
  if (!tbot_cast_record(&rec)) {
    ek_log("stale: NO CAST ON RECORD -- nothing gated this shot. Either the "
           "trigger was the player's own finger or the triggerbot has not cast "
           "a ray yet this session");
    return;
  }

  // ekm_shot_fresh's clock on both ends; unsigned, so it survives the wrap.
  const unsigned age = aim_clock_ms() - rec.ms;
  if (!ekm_shot_fresh(age)) {
    ek_log("stale: the cast on record is STALE -- age %u ms, past the port's "
           "%u ms ceiling. No delta printed: it would be a diff against a cast "
           "from before some gap, not against the shot's own frame",
           age, EKM_SHOT_STALE_MS);
    return;
  }

  PlayerSnap now;
  if (!player_snap(&now)) {
    ek_log("stale: a cast is on record (age %u ms) but the player struct did "
           "not read at fire time, so there is nothing to diff it against", age);
    return;
  }

  // Whole tics by tbot_held_tics's integer arithmetic; the freshness gate bounds age.
  const int ageTics = (int)((age * (unsigned)TBOT_TICS_PER_SECOND) / 1000u);

  int pyoff = 0;
  const bool pyoffOk = player_read_pyoff(&pyoff);
  int muzzleWeapon = 0;
  (void)player_read_weapon(&muzzleWeapon);
  const int nowMuzzleZ =
      pyoffOk ? ekm_muzzle_z(now.z, pyoff, muzzleWeapon) : now.z;

  // What the pitch drift is worth at the target's range: hitscan advances x, y, z together and shotray.h's horizontal
  // pair has magnitude 16384, so the impact z moves range2d * dvz / 16384. Both rays use the RECORDED yaw so only pitch
  // differs.
  const EkShotRay castRay = ekm_shot_ray(rec.q16ang, rec.q16horiz, rec.horizoff);
  const EkShotRay nowRay  = ekm_shot_ray(rec.q16ang, now.horiz, now.horizoff);
  const int dvz = nowRay.vz - castRay.vz;

  char worth[192];
  if (rec.range2d > 0 && rec.targetSprite >= 0) {
    const long long dz = (long long)rec.range2d * (long long)dvz / 16384;
    _snprintf_s(worth, sizeof(worth), _TRUNCATE,
                "worth at range: sprite %d is %d units out, so that pitch moves "
                "the impact %d z units (%d x/y units)",
                rec.targetSprite, rec.range2d, (int)dz,
                (int)(dz / (long long)(int)EKM_Z_PER_XY));
  } else {
    _snprintf_s(worth, sizeof(worth), _TRUNCATE,
                "worth at range: UNKNOWN -- the cast named no target sprite, so "
                "there is no range to price the pitch delta over");
  }

  ek_log("stale: the cast %s | age %u ms (%d tic(s)) | fire minus cast: "
         "dx %+d dy %+d dz %+d dmuzzlez %+d%s dsector %+d (%d -> %d) "
         "dq16ang %+d dq16horiz %+d dhorizoff %+d | %s",
         rec.fired ? (rec.usedSolution
                        ? "was the bot's own fire, cast on SILENT AIM'S SOLUTION"
                        : "was the bot's own fire, cast on the player's view")
                   : "happened but the bot did NOT fire on it (so this shot is "
                     "probably the player's own trigger)",
         age, ageTics,
         now.x - rec.ox, now.y - rec.oy, now.z - rec.posZ,
         nowMuzzleZ - rec.oz,
         (pyoffOk && rec.pyoffOk) ? "" : " (pyoff unreadable on one side, so "
                                         "dmuzzlez is not a muzzle delta)",
         now.sect - rec.osect, rec.osect, now.sect,
         now.ang - rec.q16ang, now.horiz - rec.q16horiz,
         now.horizoff - rec.horizoff, worth);
}

static volatile LONG s_bulletsFired = 0;

static void EK_PreFireDetour(int spriteNum, int playerNum, int projecTile, void* srcVect,
                             int* zvel, int* shootAng, int accurateAim, int doSpread) {
  PreFireFn const orig = s_prefireOrig;
  if (!orig) return;

  int weapon = 0;
  if (player_read_weapon(&weapon) &&
      ekm_weapon_wants_accuracy(weapon, g_cfg.accpistol, g_cfg.accshotgun,
                                g_cfg.accchaingun, g_cfg.accexpander))
    doSpread = 0;

  // EVERY round, not every logged round: the engine calls this once per bullet, the only honest shot count (the
  // triggerbot's tally counts presented frames; the prefire line is rate limited). Unconditional, so a burst fired with
  // the log off is comparable.
  s_bulletsFired++;

  // Decided BEFORE the call, so the values going in are the values that went in.
  const unsigned now = g_cfg.debuglog ? wallclock_ms() : 0;
  const bool say = g_cfg.debuglog && zvel != NULL && shootAng != NULL &&
                   (!s_prefireLogHave ||
                    (unsigned)(now - s_prefireLogMs) >= EK_PREFIRE_LOG_MIN_MS);
  const int inZvel = say ? *zvel : 0;
  const int inAng  = say ? *shootAng : 0;

  // Called BEFORE the original so "current" is the shot's departing state.
  if (g_cfg.debuglog &&
      (!s_staleLogHave || (unsigned)(now - s_staleLogMs) >= EK_STALE_LOG_MIN_MS)) {
    s_staleLogHave = 1;
    s_staleLogMs = now;
    ek_log_shot_staleness();
  }

  orig(spriteNum, playerNum, projecTile, srcVect, zvel, shootAng, accurateAim, doSpread);

  // Tracers go after the original: its last act is `srcVect->z -= ZOFFSET6` (player.cpp:531) and auto-aim,
  // EVENT_GETSHOTRANGE and spread have written shootAng and zvel by here, so these are what Proj_DoHitscan casts with.
  // Nothing is cast: this is the tic and ek_hitscan is SwapBuffers-hook only (hitscan.h).
  if (g_cfg.tracers != 0 && srcVect != NULL && zvel != NULL && shootAng != NULL) {
    const int* const sv = (const int*)srcVect;
    tracer_note_shot(sv[0], sv[1], sv[2], *shootAng, *zvel);
  }

  if (say) {
    s_prefireLogHave = 1;
    s_prefireLogMs = now;
    ek_log("prefire: sprite %d player %d tile %d weapon %d accurateAim %d "
           "doSpread %d | zvel %d -> %d (%+d), shootAng %d -> %d (%+d)",
           spriteNum, playerNum, projecTile, weapon, accurateAim, doSpread,
           inZvel, *zvel, *zvel - inZvel, inAng, *shootAng, *shootAng - inAng);
  }
}

// Silent aim redirects the shot, not the view: the whole substitution lives inside ONE engine call, between two frames.
// Re-entrancy is a depth counter, not a tile list (World Tour's tile-keyed check matched nothing after its fork
// renumbered tiles, silentwt.cpp:73).
static int s_silentDepth = 0;

// Logged once each: this is the firing path and the second occurrence says nothing new.
static bool s_silentWriteLogged = false;
static bool s_silentRestoreLogged = false;

static int EK_ShootDetour(int spriteNum, int projecTile, int forceZvel) {
  ShootFn const orig = s_shootOrig;
  if (!orig) return -1;

  // Every term must hold, in cost order: the row's switch, the depth counter, "is this the player firing", then the
  // solution and bound. player_sprite_matches, not the latched index: ps->i changes on respawn, and a stale compare
  // could write our yaw into an enemy's sprite ang, which the non-player arm reads (player.cpp:1719).
  int q16ang = 0, q16horiz = 0, curAng = 0;
  EkShotSave save;
  const bool wants =
      g_cfg.silenton != 0 &&
      s_silentDepth == 0 &&
      player_sprite_matches(spriteNum) &&
      player_q16ang(&curAng) &&
      aim_solution_for_shot(&q16ang, &q16horiz) &&
      ekm_redirect_within((curAng >> 16) & (EKM_YAW_UNITS - 1),
                          (q16ang >> 16) & (EKM_YAW_UNITS - 1),
                          g_cfg.silentmaxangle);

  // Its own step, so its failure can be told from an ordinary reason not to redirect.
  bool redirect = false;
  if (wants) {
    redirect = player_write_shot_view(q16ang, q16horiz, EK_SILENT_AUTO_AIM, &save);
    if (!redirect && !s_silentWriteLogged) {
      s_silentWriteLogged = true;
      ek_log("gamehooks: silent aim could not write the shot view; the shot is "
             "the player's own and stays that way");
    }
  }

  if (!redirect) return orig(spriteNum, projecTile, forceZvel);

  // Restored on every path the process survives, hence __finally: normal return, early return and a fault some handler
  // elects to handle. An unhandled fault skips termination handlers, but that is process death. No destructors, so it
  // compiles under /EHsc.
  int spawned = -1;
  s_silentDepth++;
  __try {
    spawned = orig(spriteNum, projecTile, forceZvel);
  } __finally {
    s_silentDepth--;
    if (!player_restore_shot_view(&save) && !s_silentRestoreLogged) {
      s_silentRestoreLogged = true;
      // A failed restore is the one state this feature must never leave behind, so it is logged.
      ek_log("gamehooks: silent aim could not restore the shot view after a "
             "shot; the player's aim may be left where the shot went");
    }
  }
  return spawned;
}

typedef int (*IncurDamageFn)(int spriteNum);
static IncurDamageFn volatile s_incurOrig = 0;
static EkHook s_incurHook;

// Read before, decide after: A_IncurDamage subtracts damage and sets htextra to -1 (actors.cpp:1322, :1327), but four
// paths return -1 having applied nothing, including a CON veto of EVENT_PREACTORDAMAGE (:1319). World Tour pushes
// before the call and reports all four; this waits for the answer.
static int ek_incur_damage(int spriteNum) {
  IncurDamageFn orig = s_incurOrig;
  while (!orig) { YieldProcessor(); orig = s_incurOrig; }
  EkDmgPending p = { 0, 0, 0, 0, 0, 0, 0 };
  const int have = dmgnum_pre(spriteNum, &p);
  const int r = orig(spriteNum);
  if (have && r >= 0) dmgnum_post(&p);
  return r;
}

static bool install_one(const unsigned char* base, const char* what,
                        const unsigned char* sig, size_t len,
                        void* detour, EkHook* hook, volatile LONG* rvaOut) {
  const unsigned entry = locate(base, what, sig, len, NULL, 0);
  if (entry == 0) return false;

  if (!hook_install((void*)(base + entry), detour, hook)) {
    // rvaOut is published only on success, or _rva() would report nonzero while _ready() says false.
    ek_log("gamehooks: %s at rva 0x%07X was NOT hooked; the game keeps its own",
           what, entry);
    return false;
  }
  InterlockedExchange(rvaOut, (LONG)entry);
  ek_log("gamehooks: %s hooked at rva 0x%07X", what, entry);
  return true;
}

bool gamehooks_install(void) {
  if (s_attempted) return s_prefireOrig != NULL && s_shootOrig != NULL;
  s_attempted = true;

  const unsigned char* const base = (const unsigned char*)GetModuleHandleA(NULL);
  if (!base) { ek_log("gamehooks: no host module; not hooking"); return false; }

  if (install_one(base, "P_PreFireHitscan", EK_PREFIRE_SIG, sizeof(EK_PREFIRE_SIG),
                  (void*)&EK_PreFireDetour, &s_prefireHook, &s_prefireRva))
    s_prefireOrig = (PreFireFn)hook_trampoline(&s_prefireHook);

  if (install_one(base, "A_ShootWithZvel", EK_SHOOT_SIG, sizeof(EK_SHOOT_SIG),
                  (void*)&EK_ShootDetour, &s_shootHook, &s_shootRva))
    s_shootOrig = (ShootFn)hook_trampoline(&s_shootHook);

  // A_IncurDamage (actors.cpp:1299): the damage applier the floating numbers read before it consumes htextra. A refusal
  // leaves the damage numbers off and the other two hooks alone; two facts are required (locate's sig2).
  const unsigned incurEntry = locate(base, "A_IncurDamage",
                                     EK_INCURDAMAGE_SIG,  sizeof(EK_INCURDAMAGE_SIG),
                                     EK_INCURDAMAGE_SIG2, sizeof(EK_INCURDAMAGE_SIG2));
  if (incurEntry != 0) InterlockedExchange(&s_incurRva, (LONG)incurEntry);

  // A refusal here leaves the other two hooks alone.
  if (s_incurRva != 0) {
    if (hook_install((void*)(base + incurEntry), (void*)ek_incur_damage, &s_incurHook)) {
      s_incurOrig = (IncurDamageFn)hook_trampoline(&s_incurHook);
      ek_log("gamehooks: A_IncurDamage hooked at rva 0x%07X; the damage numbers "
             "read htextra before the original consumes it and keep the event "
             "only when the original reports the damage landed", incurEntry);
    } else {
      InterlockedExchange(&s_incurRva, 0);
      ek_log("gamehooks: the A_IncurDamage hook did not take; damage numbers stay off");
    }
  }

  ek_log("gamehooks: install finished -- P_PreFireHitscan=%s A_ShootWithZvel=%s",
         s_prefireOrig ? "hooked" : "stock", s_shootOrig ? "hooked" : "stock");
  return s_prefireOrig != NULL && s_shootOrig != NULL;
}

void gamehooks_remove(void) {
  // Pointers first: this NARROWS the unload window; a thread already inside the original still returns into the
  // trampoline block hook_remove frees. Clearing stops only NEW calls.
  s_prefireOrig = NULL;
  s_shootOrig = NULL;
  if (s_prefireHook.target && !hook_remove(&s_prefireHook))
    ek_log("gamehooks: P_PreFireHitscan could not be unpatched");
  if (s_shootHook.target && !hook_remove(&s_shootHook))
    ek_log("gamehooks: A_ShootWithZvel could not be unpatched");
  // Opposite order, on purpose: ek_incur_damage spins on a null original and hook.cpp:186 leaves the target patched
  // when a restore fails, so clearing first would hang the next damaged actor.
  if (s_incurHook.target && !hook_remove(&s_incurHook))
    ek_log("gamehooks: A_IncurDamage could not be unpatched");
  else
    s_incurOrig = 0;
}

bool gamehooks_shoot_ready(void)  { return s_shootOrig != NULL; }
bool gamehooks_spread_ready(void) { return s_prefireOrig != NULL; }

unsigned gamehooks_bullets_fired(void) {
  return (unsigned)InterlockedCompareExchange(&s_bulletsFired, 0, 0);
}

unsigned gamehooks_shoot_rva(void)  { return (unsigned)s_shootRva; }
unsigned gamehooks_spread_rva(void) { return (unsigned)s_prefireRva; }
unsigned gamehooks_incurdamage_rva(void) { return (unsigned)s_incurRva; }
