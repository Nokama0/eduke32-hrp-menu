#pragma once

// The map's extent derived from the sector chain: a sector past the map's end holds the PREVIOUS map's record. No
// search for the engine's globals: on E1L1 numwalls sits at rva 0x0AD5AF8 and numsectors at 0x0AD5B44, 76 bytes apart
// and in the opposite order to build.h, so no adjacent-pair scan finds them.
#include "sectorstate.h"
#include "wallstate.h"
#include "gamestate.h"

// At least a triangle's worth of walls and EK_SECTOR_MIN_CHAIN sectors (sectorstate's constant; the two move together).
// A failing pair is not published.
inline bool ekm_extent_counts_plausible(int n, int w) {
  if (n < EK_SECTOR_MIN_CHAIN || n > EK_MAXSECTORS) return false;
  if (w < 3 || w > EK_MAXWALLS)   return false;
  return true;
}

// ints, no bool member (/O2 struct returns). Only `stop`: a wallCount duplicated a quantity the walk already sums.
struct EkExtentStep { int stop; };

// One sector at a time from the previous span, (0, 0) before sector 0, so sector 0 must start at wall 0. An unreadable
// sector STOPS: a hole cannot be told from the end.
inline EkExtentStep ekm_extent_step(int index, int readOk,
                                    int wallptr, int wallnum,
                                    int prevWallptr, int prevWallnum, int n) {
  EkExtentStep s = { 1 };
  if (index < 0 || index >= n) return s;
  if (!readOk) return s;
  // Span rule as the sweep uses it: a degenerate sector must not end the extent (E4L1 stopped at sector 34 of 546).
  if (!ekm_sector_span_usable(wallptr, wallnum)) return s;
  if (wallptr + wallnum > EK_MAXWALLS) return s;
  if (!ekm_sector_follows(prevWallptr, prevWallnum, wallptr)) return s;
  s.stop = 0;
  return s;
}

// The stop index IS numsectors. At the true end sector[n] is zeroed (wallnum 0 fails) or a stale record that generally
// does not follow this chain. n == EK_MAXSECTORS ends the walk on the bound.
inline bool ekm_extent_chain_stops(int n, int readOk, int wallptr, int wallnum,
                                   int prevWallptr, int prevWallnum) {
  if (n >= EK_MAXSECTORS) return true;
  return ekm_extent_step(n, readOk, wallptr, wallnum,
                         prevWallptr, prevWallnum, n + 1).stop == 1;
}

// Cross-check: once a map is up, the scan thread locates numsectors/numwalls BY CONTENT in a window around the sector
// pointer and compares them each walk. Disagreement publishes zero and logs loudly; unlocated, the walk stands alone.

// Measured offsets from the anchor: numwalls +8, numsectors +84 (not hardcoded); window 512 forward, 256 back, small so
// "exactly one position" means something.
const int EK_EXTENT_WINDOW_BACK = 256;
const int EK_EXTENT_WINDOW_FWD  = 512;

// Steps two bytes (int16_t alignment); this rule is the loop's bound in mapextent.cpp.
inline bool ekm_extent_window_holds(int offset) {
  return offset >= -EK_EXTENT_WINDOW_BACK && offset < EK_EXTENT_WINDOW_FWD;
}

// Both counts are int16_t. Equal halves are refused: one position would answer for both.
inline bool ekm_extent_values_searchable(int n, int w) {
  if (n < 1 || n > 32767) return false;
  if (w < 1 || w > 32767) return false;
  return n != w;
}

// Uniqueness makes this a locate: two hits is an index table (323, 324, 325, ...), none is an unknown layout. Anything
// but one of each falls back to the walk alone.
inline bool ekm_extent_locate_unique(int nHits, int wHits) {
  return nHits == 1 && wHits == 1;
}

// Both halves: an over-run publishes the previous pair whole; both also catch a half-written load.
inline bool ekm_extent_agrees(int walkN, int walkW, int globN, int globW) {
  return walkN == globN && walkW == globW;
}

// Residual risk: if the walk over-ran at locate time (a stale record continues the chain and the published pair is the
// previous map's) the search finds the old numbers and agrees. The locate runs on the first plausible walk after start,
// when .bss past the map is still zero. PENDING searches next walk, LOCATED cross-checks, NONE gave up.
enum {
  EK_EXTENT_HARDEN_PENDING = 0,
  EK_EXTENT_HARDEN_LOCATED = 1,
  EK_EXTENT_HARDEN_NONE    = 2
};

// RELOCATE: addresses stopped holding counts, so drop them, look again, publish this second's walk unhardened.
enum {
  EK_EXTENT_PUBLISH_NOTHING  = 0,
  EK_EXTENT_PUBLISH_WALK     = 1,
  EK_EXTENT_PUBLISH_REFUSE   = 2,
  EK_EXTENT_PUBLISH_RELOCATE = 3
};

// Decides whether the ESP draws anything. An implausible walk outranks everything: a half-loaded level must not be
// reported as a disagreement.
inline int ekm_extent_publish_verdict(int hardenState, int walkOk,
                                      int globPlausible, int agrees) {
  if (!walkOk) return EK_EXTENT_PUBLISH_NOTHING;
  if (hardenState != EK_EXTENT_HARDEN_LOCATED) return EK_EXTENT_PUBLISH_WALK;
  if (!globPlausible) return EK_EXTENT_PUBLISH_RELOCATE;
  return agrees ? EK_EXTENT_PUBLISH_WALK : EK_EXTENT_PUBLISH_REFUSE;
}

// Re-locates allowed before giving up; a layout cannot move inside a process, so a drop means a wrong window.
const int EK_EXTENT_RELOCATE_MAX = 4;
inline bool ekm_extent_may_relocate(int drops) {
  return drops < EK_EXTENT_RELOCATE_MAX;
}

// The loud line waits for a second look: loadboard writes the counts before all records, so a 1 Hz scan can derive a
// plausible smaller n against already-new globals. The zero-publish stays immediate. The streak rises on each
// disagreement and drops to zero on any agreement.
inline int ekm_extent_disagree_next(int priorStreak, int disagreeing) {
  if (!disagreeing) return 0;
  return priorStreak + 1;
}

// True at exactly two, so one continuous disagreement logs once with no extra flag.
inline bool ekm_extent_disagree_confirmed(int streak) {
  return streak == 2;
}

// Scan thread only (gamestate.cpp); refuses until sectorstate_ready().
bool mapextent_scan(void);

// Readers are render-thread safe: one interlocked 64-bit read of the published pair.
bool mapextent_ready(void);

// One read of the packed pair, coherent alone; ready() then this can be ready with 0, which is benign. No numwalls
// getter: numsectors then numwalls could straddle a publish.
int mapextent_numsectors(void);

// Both halves from one read, the only coherent-pair accessor; both out-params are 0 on false.
bool mapextent_extent(int* numsectors, int* numwalls);
