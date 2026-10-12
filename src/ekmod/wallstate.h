#pragma once

// The engine's wall array. Breakable glass is a WALL, not a sprite (sector.cpp:1570): a shot reports hit.wall == N,
// hit.sprite == -1. Found like sprite[]: enginePreInit assigns `wall = wall_s` (engine.cpp:9155). Validator: the map's
// own geometry (WALLS_ARE_CONSISTENT engine.cpp:1296, reciprocal nextwalls :1373); zeroed memory dies at index 0.

#include "gamestate.h"

// walltypev7, 32 bytes (buildtypes.h:73-88), read straight from the map (engine.cpp:11200).
const int EK_WALL_STRIDE     = 32;
const int EK_WALL_X          = 0x00;
const int EK_WALL_Y          = 0x04;
const int EK_WALL_POINT2     = 0x08;
const int EK_WALL_NEXTWALL   = 0x0A;
const int EK_WALL_NEXTSECTOR = 0x0C;
const int EK_WALL_CSTAT      = 0x0E;
const int EK_WALL_PICNUM     = 0x10;
const int EK_WALL_OVERPICNUM = 0x12;

// Derived and bracketed by the 32-byte end (buildtypes.h:84-87): shade 0x14, pal 0x15, xrepeat 0x16, yrepeat 0x17,
// xpanning 0x18, ypanning 0x19, lotag 0x1A, hitag 0x1C, extra 0x1E.
const int EK_WALL_PAL        = 0x15;
const int EK_WALL_LOTAG      = 0x1A;
const int EK_WALL_HITAG      = 0x1C;

// build.h:49,60 plus M32_FIXME_WALLS: wall_s has slack the index may reach.
const int EK_MAXWALLS   = 16384;
const int EK_WALL_SLACK = 512;
const int EK_WALL_SLOTS = EK_MAXWALLS + EK_WALL_SLACK;
const int EK_WALL_BYTES = EK_WALL_SLOTS * EK_WALL_STRIDE;

// cstat bit 4 (buildtypes.h:64): masking wall, i.e. glass and vent fans.
const int EK_WALL_CSTAT_MASKED = 16;

// Minimum clauses are load-bearing: zero of zero would satisfy every threshold.
const int EK_WALL_SCAN_DEPTH   = 256;
const int EK_WALL_MIN_CHECKED  = 64;
const int EK_WALL_MIN_PAIRS    = 8;

// point2 kills zeros: it is never the wall itself.
inline bool ekm_wall_record_plausible(int index, int point2, int nextwall,
                                      int nextsector, int picnum, int overpicnum) {
  if (point2 < 0 || point2 >= EK_WALL_SLOTS) return false;
  if (point2 == index) return false;
  if (nextwall < -1 || nextwall >= EK_WALL_SLOTS) return false;
  if (nextsector < -1 || nextsector >= EK_MAXSECTORS) return false;
  if (picnum < 0 || picnum >= EK_MAXTILES) return false;
  if (overpicnum < 0 || overpicnum >= EK_MAXTILES) return false;
  return true;
}

// ints only: MSVC /O2 loses a bool member returned by value.
struct EkWallPair {
  int index, nextwall, nextOfNext;
  int x1, y1, x2, y2;
  int kx, ky, kp2x, kp2y;
};

inline bool ekm_wall_pair_consistent(const EkWallPair& p) {
  if (p.nextOfNext != p.index) return false;
  if (p.kx != p.x2 || p.ky != p.y2) return false;
  if (p.kp2x != p.x1 || p.kp2y != p.y1) return false;
  return true;
}

// Every record plausible, not most.
inline bool ekm_wall_scan_accept(int checked, int plausible, int pairs) {
  if (checked < EK_WALL_MIN_CHECKED) return false;
  if (plausible != checked) return false;
  if (pairs < EK_WALL_MIN_PAIRS) return false;
  return true;
}

struct EkWallFace { int picnum, overpicnum, cstat, nextsector; };

bool wallstate_scan(void);
bool wallstate_ready(void);

unsigned wallstate_rva(void);

// False means nothing known, never "nothing there".
bool wallstate_face(int wallnum, EkWallFace* out);

// Separate so the per-shot breakable path reads four fields, not six.
struct EkWallCorner { int x, y, nextsector; };
bool wallstate_corner(int wallnum, EkWallCorner* out);

bool wallstate_point2(int wallnum, int* out);

// Tags P_ActivateSwitch's wall branch reads (sector.cpp:1294); separate from the face for the same reason.
struct EkWallTags { int pal, lotag, hitag; };
bool wallstate_tags(int wallnum, EkWallTags* out);
