#pragma once

// The engine's own ray cast, CALLED not hooked. It MUTATES shared engine globals (clipsectorlist and friends, used by
// clipmove/cansee/neartag), so ek_hitscan may be called ONLY inside the SwapBuffers hook on the tic's thread, never
// from the scan worker (docs/re/hitscan.md).

// hitdata_t (build.h:1205-1209): vec3 union, then int16 sprite, wall, sect in that order; pinned, as a wrong order
// gives a plausible answer.
const int EK_HIT_X      = 0;
const int EK_HIT_Y      = 4;
const int EK_HIT_Z      = 8;
const int EK_HIT_SPRITE = 12;
const int EK_HIT_WALL   = 14;
const int EK_HIT_SECT   = 16;

struct EkHitData {
  int   x, y, z;
  short sprite, wall, sect;
};

const int EK_CLIPMASK1 = (256 << 16) + 64;

// THE SHOOTER'S OWN BLOCKING BITS: the ray starts inside the player's sprite and Build can return it as the hit. The
// engine clears the bits around its cast (Proj_DoHitscan, player.cpp:548-560, mask 256+1); bit 0 is blocking, bit 8
// hitscan-sensitive. Our write is the engine's own and is restored in the same hook call.
const int EK_CSTAT_BLOCKING      = 1;
const int EK_CSTAT_BLOCK_HITSCAN = 256;
const int EK_SHOOTER_CSTATMASK   = EK_CSTAT_BLOCKING + EK_CSTAT_BLOCK_HITSCAN;

inline int ekm_shooter_cstat_cleared(int cstat) {
  return cstat & ~EK_SHOOTER_CSTATMASK;
}

// A sprite with neither bit must not be written to.
inline bool ekm_shooter_cstat_changes(int cstat) {
  return (cstat & EK_SHOOTER_CSTATMASK) != 0;
}

// THE ORIGIN SECTOR: the engine runs updatesector at the origin (player.cpp:548-560); the player's own sector makes the
// walk hit through geometry the bullet stops at. Corrected in ek_hitscan for every caller.

const int EKM_US_INITIALDIST = 256;
const int EKM_US_MAXDIST     = 1536;

const int EKM_US_SHAPE_WINDOW = 0x40;

// `<opcode> <imm32>` in the window, immediate compared as a number.
inline bool ekm_us_imm_at(const unsigned char* b, int n,
                          const unsigned char* op, int opLen, int want) {
  if (!b || !op || opLen <= 0) return false;
  for (int i = 0; i + opLen + 4 <= n; i++) {
    int k = 0;
    for (; k < opLen; k++) if (b[i + k] != op[k]) break;
    if (k != opLen) continue;
    const unsigned char* v = b + i + opLen;
    const unsigned imm = (unsigned)v[0] | ((unsigned)v[1] << 8) |
                         ((unsigned)v[2] << 16) | ((unsigned)v[3] << 24);
    if (imm == (unsigned)want) return true;
  }
  return false;
}

// updatesector and updatesectorz differ by one parameter. Win64: arg 4 in r9d, arg 5 at [rsp+0x20]; updatesectorz has z
// in the way (0x20, 0x28). Calling it with three args writes through a wild r9, so this runs BEFORE any call.
inline bool ekm_us_shape_match(const unsigned char* b, int n) {
  const unsigned char movR9d[2]  = { 0x41, 0xB9 };
  const unsigned char movArg5[4] = { 0xC7, 0x44, 0x24, 0x20 };
  return ekm_us_imm_at(b, n, movR9d, 2, EKM_US_INITIALDIST) &&
         ekm_us_imm_at(b, n, movArg5, 4, EKM_US_MAXDIST);
}

// `call rel32`: displacement from the END of the five bytes.
inline unsigned ekm_us_rel32_target(unsigned siteRva, int disp) {
  return (unsigned)((int)siteRva + 5 + disp);
}

inline int ekm_us_origin_sector(bool haveCorrection, int corrected,
                                int passedIn, int maxSectors) {
  if (!haveCorrection) return passedIn;
  if (corrected < 0 || corrected >= maxSectors) return -1;
  return corrected;
}

// Locate and self-test; idempotent, safe every frame. Driven from overlay.cpp, not the scan worker.
bool hitscan_scan(void);
bool hitscan_ready(void);

unsigned hitscan_rva(void);

// Casts a ray and reports the hit. The direction is in world units on all axes (vz not scaled by 16;
// clip.cpp:2664-2670). False means NO ANSWER, not "nothing hit": callers degrade to pre-hitscan behaviour. SwapBuffers
// hook only.
bool ek_hitscan(int x, int y, int z, int sect, int vx, int vy, int vz,
                EkHitData* out);

// Locate and self-test updatesector. Not pure: it writes updatesectorneighborlist[]/map[] (engine.cpp:13310,
// :13507-13543), the clipsectorlist hazard. Callable from three places only: SwapBuffers, the polymer_drawrooms
// bracket, the G_DrawRooms hook (docs/re/hitscan.md). Needs hitscan armed.
bool updatesector_scan(void);
bool updatesector_ready(void);
unsigned updatesector_rva(void);

// Which sector a point is in; `from` starts the neighbour search (-1 scans all, engine.cpp:13301). False means NO
// ANSWER; true puts a valid sector in *out.
bool ek_updatesector(int x, int y, int from, int* out);
