// Finds the sprite array without an offset (no cvar names it). sprite is a POINTER (build.h:619-621), loaded with `mov
// reg,[rip+disp]`. The validator demands the free-slot marker (zeroed .bss passed range-only tests); regions are
// enumerated once. camera.cpp decodes the engine's sprite load and decides; this witnesses it (docs/re/gamestate.md).

#include <windows.h>
#include "log.h"
#include "gamestate.h"
#include "tiles.h"
#include "badguy.h"
#include "playerstate.h"
#include "actorstate.h"
#include "wallstate.h"
#include "sectorstate.h"
#include "mapextent.h"
#include "maskscan.h"
#include "modelbounds.h"
#include "secretvol.h"
#include "secrettrig.h"
#include "secretpath.h"

static const unsigned char* s_base = NULL;
static size_t s_size = 0;

// Worker writes, render reads: an aligned pointer store is atomic on x64, so render sees NULL or a validated array.
static const unsigned char* volatile s_sprites = NULL;
static volatile LONG s_spritePtrRva = 0;
static volatile LONG s_workerState = 0;

// Published once by camera.cpp (gamestate.h: why a decoded instruction outranks a content match).
static volatile LONG s_authRva = 0;
static volatile LONG s_authState = 0;

void gamestate_sprite_authority(unsigned spritePtrRva) {
  InterlockedExchange(&s_authRva, (LONG)spritePtrRva);
  InterlockedExchange(&s_authState,
                      spritePtrRva ? EK_SPR_AUTH_IMAGE : EK_SPR_AUTH_REFUSED);
}

// Committed-region cache: the scan makes no system calls.
struct Region { const unsigned char* base; size_t size; };
static Region s_regions[1024];
static int s_regionCount = 0;
static size_t s_regionBytes = 0;

// Adjacent regions are merged: an unmerged table filled its 128 slots and hid the rest, and in_region needs the whole
// 720KB array in ONE entry.
static void enumerate_regions(const unsigned char* start, size_t len) {
  s_regionCount = 0;
  s_regionBytes = 0;
  const unsigned char* p = start;
  const unsigned char* const end = start + len;
  const int cap = (int)(sizeof(s_regions) / sizeof(s_regions[0]));
  while (p < end) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
    if (mbi.RegionSize == 0) break;
    const unsigned char* rbase = (const unsigned char*)mbi.BaseAddress;
    if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
      Region* last = s_regionCount ? &s_regions[s_regionCount - 1] : NULL;
      if (last && last->base + last->size == rbase) {
        last->size += mbi.RegionSize;
      } else if (s_regionCount < cap) {
        s_regions[s_regionCount].base = rbase;
        s_regions[s_regionCount].size = mbi.RegionSize;
        s_regionCount++;
      }
      s_regionBytes += mbi.RegionSize;
    }
    p = rbase + mbi.RegionSize;
  }
}

void gamestate_regions_refresh(void) {
  if (s_base && s_size) enumerate_regions(s_base, s_size);
}
int gamestate_region_count(void) { return s_regionCount; }
bool gamestate_region_at(int i, const unsigned char** base, size_t* bytes) {
  if (i < 0 || i >= s_regionCount || !base || !bytes) return false;
  *base = s_regions[i].base;
  *bytes = s_regions[i].size;
  return true;
}
const unsigned char* gamestate_module_base(void) { return s_base; }
size_t gamestate_module_size(void) { return s_size; }

// Shared with wallstate.cpp, same thread and sequence.
bool gamestate_region_contains(const void* p, size_t bytes) {
  const unsigned char* b = (const unsigned char*)p;
  for (int i = 0; i < s_regionCount; i++)
    if (b >= s_regions[i].base && b + bytes <= s_regions[i].base + s_regions[i].size)
      return true;
  return false;
}

static bool in_region(const void* p, size_t bytes) {
  const unsigned char* b = (const unsigned char*)p;
  for (int i = 0; i < s_regionCount; i++)
    if (b >= s_regions[i].base && b + bytes <= s_regions[i].base + s_regions[i].size)
      return true;
  return false;
}

// MAXSECTORS is a legal stored sectnum: free slots park there (engine.cpp:9380, :8838), so an exclusive bound rejected
// the real array while zeros passed. The bound is inclusive.
static bool record_plausible(const unsigned char* r) {
  const short sectnum = *(const short*)(r + EK_SPR_SECTNUM);
  const short statnum = *(const short*)(r + EK_SPR_STATNUM);
  const short picnum  = *(const short*)(r + EK_SPR_PICNUM);
  if (sectnum < -1 || sectnum > EK_MAXSECTORS) return false;
  if (statnum < 0 || statnum > EK_MAXSTATUS) return false;
  if (picnum < 0 || picnum >= EK_MAXTILES) return false;
  return true;
}

struct ArrayScore { int sampled, plausible, freeSlots, nonZero; };

// Sampled across the whole array: allocation takes the head of the free list, so the front has few free slots.
static void score_array(const unsigned char* arr, ArrayScore* out) {
  out->sampled = out->plausible = out->freeSlots = out->nonZero = 0;
  const int step = EK_MAXSPRITES / EK_SPRITE_SCAN_DEPTH;
  for (int i = 0; i < EK_MAXSPRITES; i += step) {
    const unsigned char* r = arr + (size_t)i * EK_SPR_STRIDE;
    out->sampled++;
    if (!record_plausible(r)) continue;
    out->plausible++;
    if (*(const short*)(r + EK_SPR_STATNUM) == EK_MAXSTATUS) out->freeSlots++;
    for (int b = 0; b < EK_SPR_STRIDE; b++)
      if (r[b]) { out->nonZero++; break; }
  }
}

// Demands evidence: freed sprites carry statnum MAXSTATUS (engine.cpp:8851), so a real array carries thousands at 1024;
// zeros carry none.
static bool array_looks_real(const ArrayScore& s) {
  if (s.plausible < (s.sampled * 15) / 16) return false;
  if (s.freeSlots < s.sampled / 8) return false;
  if (s.nonZero  < s.sampled / 8) return false;
  return true;
}

// The slot camera.cpp named holds the array; every content test still applies (failing means no level is loaded).
static const unsigned char* image_walk_array(unsigned slotRva, ArrayScore* out) {
  out->sampled = out->plausible = out->freeSlots = out->nonZero = 0;
  if (!s_base || !slotRva) return NULL;
  if ((size_t)slotRva + sizeof(void*) > s_size) return NULL;
  const unsigned char* const slot = s_base + slotRva;
  if (!in_region(slot, sizeof(void*))) return NULL;
  const unsigned char* const cand = *(const unsigned char* const*)slot;
  if (cand < s_base || cand >= s_base + s_size) return NULL;
  if (((UINT_PTR)cand & 3u) != 0) return NULL;
  if (!in_region(cand, (size_t)EK_MAXSPRITES * EK_SPR_STRIDE)) return NULL;
  score_array(cand, out);
  return array_looks_real(*out) ? cand : NULL;
}

struct ScanResult {
  const unsigned char* best;
  unsigned bestRva;
  int candidates;
  ArrayScore score;
  ArrayScore bestSeen;
  unsigned bestSeenRva;
};

static void content_scan(ScanResult* r) {
  r->best = NULL; r->bestRva = 0; r->candidates = 0; r->bestSeenRva = 0;
  r->score.sampled = r->score.plausible = 0;
  r->score.freeSlots = r->score.nonZero = 0;
  r->bestSeen = r->score;
  const size_t arrayBytes = (size_t)EK_MAXSPRITES * EK_SPR_STRIDE;
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)s_base;
  const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(s_base + dos->e_lfanew);
  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  const int nsec = nt->FileHeader.NumberOfSections;

  // The ranking moves while sprites churn, so this search no longer decides.
  const unsigned char* best = NULL;
  unsigned bestRva = 0;
  int bestScore = 0, candidates = 0;

  // Near misses name which test refused.
  ArrayScore bestSeen = {0, 0, 0, 0};
  unsigned bestSeenRva = 0;

  for (int i = 0; i < nsec; i++) {
    if (!(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
    const unsigned char* start = s_base + sec[i].VirtualAddress;
    const DWORD secSpan = sec[i].Misc.VirtualSize;

    for (DWORD off = 0; off + 8 <= secSpan; off += 8) {
      const unsigned char* slot = start + off;
      if (!in_region(slot, 8)) continue;

      const unsigned char* cand = *(const unsigned char* const*)slot;
      if (cand < s_base || cand >= s_base + s_size) continue;
      if (((UINT_PTR)cand & 3u) != 0) continue;
      if (!in_region(cand, arrayBytes)) continue;

      candidates++;
      ArrayScore s;
      score_array(cand, &s);
      if (s.plausible > bestSeen.plausible) {
        bestSeen = s;
        bestSeenRva = (unsigned)(sec[i].VirtualAddress + off);
      }
      if (!array_looks_real(s)) continue;

      // Among real-looking candidates, the densest free-list marker wins.
      if (s.freeSlots > bestScore) {
        bestScore = s.freeSlots;
        best = cand;
        bestRva = (unsigned)(sec[i].VirtualAddress + off);
      }
    }
  }

  r->best = best;
  r->bestRva = bestRva;
  r->candidates = candidates;
  r->bestSeen = bestSeen;
  r->bestSeenRva = bestSeenRva;
  if (best) score_array(best, &r->score);
}

// Both on one pass, so the cross-check is a single view of memory.
static bool locate_now(bool waitedOut) {
  const int authState = (int)InterlockedCompareExchange(&s_authState, 0, 0);
  const unsigned authSlot = (unsigned)InterlockedCompareExchange(&s_authRva, 0, 0);

  ArrayScore imgScore;
  imgScore.sampled = imgScore.plausible = 0;
  imgScore.freeSlots = imgScore.nonZero = 0;
  const unsigned char* const fromImage =
      (authState == EK_SPR_AUTH_IMAGE) ? image_walk_array(authSlot, &imgScore)
                                       : NULL;

  ScanResult sc;
  content_scan(&sc);

  const int src = ekm_sprite_source_pick(authState, fromImage != NULL,
                                         sc.best != NULL, waitedOut);
  if (src == EK_SPR_SRC_IMAGE) {
    InterlockedExchange(&s_spritePtrRva, (LONG)authSlot);
    s_sprites = fromImage;
    ek_log("gamestate: sprite[] at array rva 0x%07X, from the engine's own "
           "sprite load -- the `sprite` pointer at rva 0x%07X, decoded inside "
           "the inlined polymer_scansprites. sampled %d, plausible %d, free "
           "slots %d, non-zero %d",
           (unsigned)(fromImage - s_base), authSlot, imgScore.sampled,
           imgScore.plausible, imgScore.freeSlots, imgScore.nonZero);
    if (!sc.best)
      ek_log("gamestate:   the content scan has no answer to witness it with on "
             "this pass; %d candidate(s) examined", sc.candidates);
    else if (sc.best == fromImage)
      ek_log("gamestate:   and the content scan agrees on the array, reading it "
             "out of the slot at rva 0x%07X (%d candidate(s) examined). The two "
             "searches may name different slots and often do; the array is what "
             "they have to agree on",
             sc.bestRva, sc.candidates);
    else
      ek_log("gamestate:   BUT THE CONTENT SCAN DISAGREES: its slot at rva "
             "0x%07X names array rva 0x%07X, not 0x%07X. The decoded "
             "instruction wins and nothing reads the scan's answer; the ESP, "
             "the triggerbot and the aim are unaffected",
             sc.bestRva, (unsigned)(sc.best - s_base),
             (unsigned)(fromImage - s_base));
    return true;
  }

  if (src == EK_SPR_SRC_SCAN) {
    InterlockedExchange(&s_spritePtrRva, (LONG)sc.bestRva);
    s_sprites = sc.best;
    ek_log("gamestate: sprite[] at array rva 0x%07X, from the content scan "
           "alone and UNWITNESSED -- the image walk %s. Read out of the slot at "
           "rva 0x%07X; %d candidate array(s) examined; sampled %d, plausible "
           "%d, free slots %d, non-zero %d",
           (unsigned)(sc.best - s_base),
           authState == EK_SPR_AUTH_REFUSED ? "refused" : "never answered",
           sc.bestRva, sc.candidates, sc.score.sampled, sc.score.plausible,
           sc.score.freeSlots, sc.score.nonZero);
    return true;
  }

  if (authState == EK_SPR_AUTH_IMAGE)
    ek_log("gamestate: the `sprite` pointer at rva 0x%07X holds nothing that "
           "reads as sprites yet, which is what an unloaded level looks like; "
           "%d content-scan candidate(s) examined meanwhile",
           authSlot, sc.candidates);
  else
    ek_log("gamestate: %d candidate(s) examined, none accepted. closest was "
           "rva 0x%07X with sampled %d, plausible %d, free slots %d, non-zero %d",
           sc.candidates, sc.bestSeenRva, sc.bestSeen.sampled,
           sc.bestSeen.plausible, sc.bestSeen.freeSlots, sc.bestSeen.nonZero);
  return false;
}

// Off the render thread: scan cost must not land on a frame. Retries because the main menu has nothing to find.
static DWORD WINAPI scan_thread(LPVOID) {
  for (int attempt = 1; attempt <= 900 && !s_sprites; attempt++) {
    enumerate_regions(s_base, s_size);
    if (locate_now(attempt > EK_SPRITE_AUTHORITY_WAIT)) break;
    if (attempt == 1)
      ek_log("gamestate: no sprite array yet -- %d merged region(s) covering "
             "%llu bytes; retrying every 2s while you play",
             s_regionCount, (unsigned long long)s_regionBytes);
    Sleep(2000);
  }
  if (!s_sprites) {
    ek_log("gamestate: gave up looking for the sprite array");
    InterlockedExchange(&s_workerState, 2);
    return 0;
  }

  // tilesiz is hunted here because its validator needs live picnums from the confirmed sprite array.
  for (int attempt = 1; attempt <= 900 && !tiles_ready(); attempt++) {
    if (tiles_scan()) break;
    Sleep(2000);
  }

  // g_tile last: without it the ESP boxes every actor, a worse view not a broken one.
  badguy_scan();

  // The last three share a tick instead of queueing: each is independent, and sequential loops made each fail its whole
  // 900 attempts (30 minutes) before the next was tried. Without apScript a play-dead trooper reads as a corpse.
  for (int attempt = 1; attempt <= 900; attempt++) {
    if (!player_ready())             player_scan();
    if (!actorstate_ready())         actorstate_scan();
    if (!actorstate_script_ready())  actorstate_script_scan();
    // wall[] joins the tick; without it the triggerbot reads every wall hit as blocked.
    if (!wallstate_ready())          wallstate_scan();
    // sector[] refuses until wall[] is up (its validator is wall geometry, sectorstate.h). The budget expires after 30
    // minutes and small maps (< 16 sectors) can lose sector[] permanently; nothing detects a level change.
    if (!sectorstate_ready())        sectorstate_scan();
    // After sectorstate: the extent's walk is the sector array.
    if (!mapextent_ready())          mapextent_scan();
    // The model locates need nothing and may succeed before a map exists. Not in the break test: a machine without HRP
    // would spend the whole budget.
    if (!modelbounds_ready())        modelbounds_scan();
    // mapextent_ready is deliberately not in this test: the 1 Hz sampler already walks it, and including it ran 900
    // attempts at 2 s with player_log_tick silent for half an hour.
    if (player_ready() && actorstate_ready() && actorstate_script_ready() &&
        wallstate_ready() && sectorstate_ready()) break;
    Sleep(2000);
  }
  InterlockedExchange(&s_workerState, 2);

  // The thread drops to a 1 Hz sampler (no file writes on the render thread). The extent rides it unconditionally so a
  // level change shows without a detector. The secret scans ride it because their answers change on map load and when a
  // found secret's lotag is zeroed; secretpath is largest and runs last, after mapextent_scan.
  for (;;) {
    player_log_tick();
    mapextent_scan();
    maskscan_probe();
    modelbounds_scan();
    secretvol_scan();
    secrettrig_scan();
    secretpath_scan();
    // The route's cheap half runs at 8 Hz (secretpath_refresh re-solves the staged graph, so the route follows the
    // player within EK_SPATH_SUBTICK_MS); the sleeps still total 1000 ms.
    for (int i = 0; i < EK_SPATH_SUBTICKS; i++) {
      Sleep(EK_SPATH_SUBTICK_MS);
      secretpath_refresh();
    }
    Sleep(EK_SPATH_SUBTICK_MS);
  }

}

bool gamestate_scan(void) {
  if (s_sprites) return true;
  // Starts the worker once; later calls are no-ops.
  if (InterlockedCompareExchange(&s_workerState, 1, 0) != 0) return false;

  s_base = (const unsigned char*)GetModuleHandleA(NULL);
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)s_base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    ek_log("gamestate: host is not a DOS image");
    InterlockedExchange(&s_workerState, 2); return false;
  }
  const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(s_base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) {
    ek_log("gamestate: host is not a PE image");
    InterlockedExchange(&s_workerState, 2); return false;
  }
  s_size = nt->OptionalHeader.SizeOfImage;

  const HANDLE h = CreateThread(NULL, 0, scan_thread, NULL, 0, NULL);
  if (!h) {
    ek_log("gamestate: CreateThread failed (%lu); the scan will not run", GetLastError());
    InterlockedExchange(&s_workerState, 2); return false;
  }
  CloseHandle(h);
  ek_log("gamestate: scanning for the sprite array on a worker thread");
  return false;
}

bool gamestate_ready(void) { return s_sprites != NULL; }

unsigned gamestate_sprite_ptr_rva(void) {
  return (unsigned)InterlockedCompareExchange(&s_spritePtrRva, 0, 0);
}

unsigned gamestate_sprite_array_rva(void) {
  const unsigned char* const arr = s_sprites;
  if (!arr || !s_base || arr <= s_base) return 0;
  return (unsigned)(arr - s_base);
}

const unsigned char* gamestate_sprite(int i) {
  const unsigned char* const arr = s_sprites;
  if (!arr || i < 0 || i >= EK_MAXSPRITES) return NULL;
  return arr + (size_t)i * EK_SPR_STRIDE;
}

int gamestate_live_sprites(void) {
  const unsigned char* const arr = s_sprites;
  if (!arr) return 0;
  int live = 0;
  for (int i = 0; i < EK_MAXSPRITES; i++) {
    const short st = *(const short*)(arr + (size_t)i * EK_SPR_STRIDE + EK_SPR_STATNUM);
    if (st >= 0 && st < EK_MAXSTATUS) live++;
  }
  return live;
}
