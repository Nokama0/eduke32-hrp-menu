#pragma once

// The engine's sector array, needed by the free camera: the sky draws only on polymer's external-view arm
// (polymer.cpp:1214), taken when z is outside the sector's band. Found by content like wall[]: sectors TILE wall[] from
// index 0 and loops close, so the scan waits for wallstate. Details: docs/re/polymer-sweep.md.

#include "wallstate.h"

// sectortypev7, buildtypes.h:41-56 (40 bytes). Only the fields the camera reads are named.
const int EK_SECTOR_STRIDE   = 40;
const int EK_SECT_WALLPTR    = 0x00;
const int EK_SECT_WALLNUM    = 0x02;
const int EK_SECT_CEILINGZ   = 0x04;
const int EK_SECT_FLOORZ     = 0x08;
const int EK_SECT_CEILINGSTAT = 0x0C;
const int EK_SECT_FLOORSTAT  = 0x0E;
// lotag: teleporter 0, water surface 1, underside 2 (game.h:535, read by G_MoveTransports). 0x22 follows the z, stat,
// tile and visibility fields (buildtypes.h:53).
const int EK_SECT_LOTAG      = 0x22;

// A secret's lotag: sector.cpp:3293 zeroes it when the player stands in the sector; premap.cpp:1357 counts it at load.
// In a header so the suite can pin "0 is not a secret".
const int EKM_SECRET_LOTAG = 32767;

inline bool ekm_sector_is_secret(int lotag) { return lotag == EKM_SECRET_LOTAG; }

// Bit 1: SLOPED, by the engine's test (getzsofslopeptr, engine.cpp:14292, an OR of both stats), not buildtypes.h:22's
// key.
const int EK_SECT_STAT_SLOPED = 2;

// Bit 0: PARALLAXED. The wall-sprite clamp tests each side's own bit (engine.cpp:6186), so NOT ORed.
const int EK_SECT_STAT_PARALLAX = 1;

// build.h:630: MAXSECTORS plus two editor spares. The memory bound, not the map's extent.
const int EK_SECTOR_SLOTS = EK_MAXSECTORS + 2;
const int EK_SECTOR_BYTES = EK_SECTOR_SLOTS * EK_SECTOR_STRIDE;

// Sixteen consecutive tiling records with closing loops are not an accident of a 21MB .bss.
const int EK_SECTOR_SCAN_DEPTH = 256;
const int EK_SECTOR_MIN_CHAIN  = 16;

inline bool ekm_sector_record_plausible(int wallptr, int wallnum) {
  if (wallnum < 3 || wallnum > EK_WALL_SLOTS) return false;
  if (wallptr < 0 || wallptr > EK_WALL_SLOTS - wallnum) return false;
  return true;
}

// The weaker question, for WALKING a found map: a two-wall sector is degenerate, not implausible. E4L1 has one at index
// 34 and the discovery rule cost the sweep 94% of the level. wallnum 0 is bounded; only a span leaving wall[] is
// refused.
inline bool ekm_sector_span_usable(int wallptr, int wallnum) {
  if (wallnum < 0 || wallnum > EK_WALL_SLOTS) return false;
  if (wallptr < 0 || wallptr > EK_WALL_SLOTS - wallnum) return false;
  return true;
}

// Sector 0 is checked against a notional (0, 0) predecessor.
inline bool ekm_sector_follows(int prevWallptr, int prevWallnum, int wallptr) {
  return wallptr == prevWallptr + prevWallnum;
}

// Sweep bound: past the loaded map polymer_freeboard NULLs prsectors[]/prwalls[] and polymer_drawsector dereferences
// them unchecked (:3043), so the sweep reads prsectors[] itself: non-NULL IS the extent. It avoids the tiling heuristic
// for that (a false accept is a NULL dereference); the tiling test stays to bound the WALL span. Every doubt stops.
struct EkSectorSweep { int stop; int wallFirst; int wallCount; };

// Ints, no bool member (wallstate.h's EkWallPair). Bounded by EK_MAXWALLS, not EK_WALL_SLOTS: prwalls[] is [MAXWALLS]
// while wall[] carries 512 editor spares.
inline EkSectorSweep ekm_sector_sweep_step(int index, int readOk, int prsectorLive,
                                           int wallptr, int wallnum,
                                           int prevWallptr, int prevWallnum,
                                           int maxSectors) {
  EkSectorSweep s = { 1, 0, 0 };
  if (index < 0 || index >= maxSectors) return s;
  if (!readOk) return s;
  if (!prsectorLive) return s;
  // The span rule, not the discovery rule: a degenerate sector is walked past.
  if (!ekm_sector_span_usable(wallptr, wallnum)) return s;
  if (wallptr + wallnum > EK_MAXWALLS) return s;
  if (!ekm_sector_follows(prevWallptr, prevWallnum, wallptr)) return s;
  s.stop = 0;
  s.wallFirst = wallptr;
  s.wallCount = wallnum;
  return s;
}

// Average of the wall vertices (z comes from florz). The loop runs `wallnum` times, never the point2 chain; point2 is
// still checked, as it proves the range is this sector's closed outline, and a corrupt one is refused.
inline bool ekm_point2_ok(int point2, int self, int wallptr, int wallnum) {
  if (point2 == self) return false;
  return point2 >= wallptr && point2 < wallptr + wallnum;
}

// 64-bit sums: four full-range coordinates overflow an int. `ok` is int (the /O2 reason above).
struct EkSectorCentre { long long sx, sy; int n; int x, y, ok; };

inline void ekm_sector_centre_accumulate(EkSectorCentre* c, int wx, int wy) {
  c->sx += wx; c->sy += wy; c->n++;
}

// No accepted vertex means no centre, not the origin.
inline bool ekm_sector_centre_finish(EkSectorCentre* c, int accepted) {
  if (accepted <= 0 || c->n != accepted) { c->ok = 0; return false; }
  c->x = (int)(c->sx / accepted);
  c->y = (int)(c->sy / accepted);
  c->ok = 1;
  return true;
}

// Backface culling is OFF when our detour gets the frame back: engine.cpp:9563 `buildgl_setDisabled(GL_CULL_FACE)` is
// the last thing renderDrawRoomsQ16 does. Put back exactly as found: glbuild.cpp caches enables (buildgl_setEnabled,
// :114). No entry points, no sweep. Int members, not bool.
struct EkSweepCull {
  int enable;
  int restore;
  int refuse;
};

inline EkSweepCull ekm_sweep_cull(int entriesOk, int enabledNow) {
  EkSweepCull c = { 0, 0, 0 };
  if (!entriesOk) { c.refuse = 1; return c; }
  if (!enabledNow) { c.enable = 1; c.restore = 1; }
  return c;
}

// domasks stays TRUE: FALSE appends to cursectormasks (numsectors entries, polymer.cpp:1838) unbounded, safe only if
// each sector queues once, and mirror recursion over one shared bitmap (:2129, :711) breaks that. TRUE only costs
// masked floors in skipped sectors. Details: docs/re/polymer-sweep.md.

// prsectors[]'s `.refptr` slot, found in polymer_updatesector's head (`mov rdi,[rip+d32]`, `mov r11,[rdi+rax*8]`).
// Shape: one scale-8 load + test/je (polymer.cpp:2624). Witnesses: rip-referenced from drawsector and updatewall, NOT
// drawwall. Span: room for MAXSECTORS pointers in image data.
inline bool ekm_prs_shape_ok(int loads, int refptrs) {
  return loads == 1 && refptrs == 1;
}

inline bool ekm_prs_witness_ok(int usRefs, int dsRefs, int uwRefs, int dwRefs) {
  return usRefs >= 1 && dsRefs >= 1 && uwRefs >= 1 && dwRefs == 0;
}

inline bool ekm_prs_span_ok(size_t slotRva, size_t arrayRva, size_t imageSize,
                            int slotInData) {
  if (!slotInData || imageSize == 0) return false;
  if (slotRva + sizeof(void*) > imageSize) return false;
  if (arrayRva == 0 || arrayRva >= imageSize) return false;
  return imageSize - arrayRva >= (size_t)EK_MAXSECTORS * sizeof(void*);
}

// prwalls[]'s slot: polymer_drawwall checks nothing (prwalls[wallnum], polymer.cpp:3652), so the sweep needs the
// engine's liveness fact (NULLed on load, :2515); a refused locate refuses the SWEEP. Shape: the one scale-8 `mov` in
// drawwall. Witnesses: updatewall and drawwall, never drawsector, and not prsectors[]'s slot. Bound is MAXWALLS.
inline bool ekm_prw_shape_ok(int loads, int refptrs) {
  return loads == 1 && refptrs == 1;
}

inline bool ekm_prw_witness_ok(int dsRefs, int uwRefs, int dwRefs,
                               int sameSlotAsPrsectors) {
  if (sameSlotAsPrsectors) return false;
  return dsRefs == 0 && uwRefs >= 1 && dwRefs >= 1;
}

inline bool ekm_prw_span_ok(size_t slotRva, size_t arrayRva, size_t imageSize,
                            int slotInData) {
  if (!slotInData || imageSize == 0) return false;
  if (slotRva + sizeof(void*) > imageSize) return false;
  if (arrayRva == 0 || arrayRva >= imageSize) return false;
  return imageSize - arrayRva >= (size_t)EK_MAXWALLS * sizeof(void*);
}

// A NULL slot inside a live sector costs THAT WALL, not the rest of the map.
inline bool ekm_wall_drawable(int prwallLive) { return prwallLive != 0; }

// drawingstate: a bit per sector set when the portal walk queues one (polymer.cpp:1832), so a set bit means this
// frame's walk drew it and the sweep skips it. A BITMAP (compat.h:1173): a byte per sector would skip seven in eight.
const int EK_SECTOR_BITMAP_BYTES = (EK_MAXSECTORS + 7) / 8;

inline int ekm_bitmap_byte(int n) { return n >> 3; }
inline int ekm_bitmap_mask(int n) { return 1 << (n & 7); }
inline bool ekm_bitmap_test(int byteValue, int n) {
  return (byteValue & ekm_bitmap_mask(n)) != 0;
}

// drawingstate's address, by call graph (polymer_displayrooms has no string): the callee calling drawsector and
// drawwall twice each, with a `rep stosq` ecx 0x40 into a rip `lea` that bitmap_set names again, 5..9 uses; span
// against the containing SECTION (.bss has no raw bytes). Details: docs/re/polymer-sweep.md.
inline bool ekm_dstate_owner_ok(int candidates, int dsCalls, int dwCalls) {
  return candidates == 1 && dsCalls >= 2 && dwCalls >= 2;
}

inline bool ekm_dstate_shape_ok(int stosSites, int countSites, int leaSites,
                                int confirmSites) {
  return stosSites == 1 && countSites == 1 && leaSites == 1 &&
         confirmSites == 1;
}

inline bool ekm_dstate_use_ok(int uses) { return uses >= 5 && uses <= 9; }

inline bool ekm_dstate_span_ok(size_t rva, size_t secBegin, size_t secEnd,
                               int inData) {
  if (!inData || rva == 0) return false;
  if (secEnd <= secBegin) return false;
  if (rva < secBegin || rva >= secEnd) return false;
  return secEnd - rva >= (size_t)EK_SECTOR_BITMAP_BYTES;
}

// `sloped`: either surface carries the slope bit, so ceilz/florz are the z at the FIRST wall. Parallax bits are kept
// apart (the clamp refuses each side on its own). Int, not bool: MSVC 19.5x /O2 loses a bool member of a returned
// struct.
struct EkSectorInfo { int ceilz, florz, wallptr, wallnum, sloped, lotag,
                      ceilparallax, florparallax; };

// Refuses until wallstate_ready(). Scan thread only.
bool sectorstate_scan(void);
bool sectorstate_ready(void);

// Where the engine's `sector` pointer lives (rva): evidence, and the anchor for a narrow search for numsectors.
unsigned sectorstate_rva(void);

bool sectorstate_info(int sectnum, EkSectorInfo* out);

// getzsofslopeptr (engine.cpp:14288) takes a pointer, not an index.
const void* sectorstate_record(int sectnum);
