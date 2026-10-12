#pragma once

// The shot's own ray as player.cpp:498-501 builds it: zvel from fix16_to_int(F16(100)-q16horiz-q16horizoff)<<5, hitscan
// dir sintable[(shootAng+512)&2047], sintable[shootAng&2047], zvel<<6. sintable is reproduced, not located
// (engine.cpp:8661-8666): no third scan to fault, and the suite can pin it.

#include <math.h>
#include "aimsolve.h"

const float EKM_BANG2RAD = 3.14159265358979323846f / 1024.0f;

// fix16.h:42. Rounds away from zero at the half; a `>> 16` would be up to half a unit off.
inline int ekm_fix16_to_int(int a) {
  return (a + (a >= 0 ? 32768 : -32768)) / 65536;
}

// First quarter (i = 0..512), truncating with the engine's +0.0001f nudge.
inline int ekm_sintable_quarter(int i) {
  return (int)(short)(16384.0f * sinf((float)i * EKM_BANG2RAD) + 0.0001f);
}

inline int ekm_sintable(int ang) {
  const int a = ang & (EKM_YAW_UNITS - 1);
  const int h = a >= 1024 ? a - 1024 : a;
  const int q = h > 512 ? 1024 - h : h;
  const int v = ekm_sintable_quarter(q);
  return a >= 1024 ? -v : v;
}

// Ints, not bool: MSVC /O2 loses a bool member of a struct returned by inline.
struct EkShotRay { int vx, vy, vz; };

// The ray a shot at this view leaves on; q16horizoff is separate because silent aim writes only q16horiz.
inline EkShotRay ekm_shot_ray(int q16ang, int q16horiz, int q16horizoff) {
  const int shootAng = ekm_fix16_to_int(q16ang);
  long long d = (long long)ekm_f16(EKM_HORIZ_LEVEL)
              - (long long)q16horiz - (long long)q16horizoff;
  if (d >  2147483000LL) d =  2147483000LL;
  if (d < -2147483000LL) d = -2147483000LL;
  int whole = ekm_fix16_to_int((int)d);
  if (whole >  EKM_SHOT_ZVEL_WHOLE_MAX) whole =  EKM_SHOT_ZVEL_WHOLE_MAX;
  if (whole < -EKM_SHOT_ZVEL_WHOLE_MAX) whole = -EKM_SHOT_ZVEL_WHOLE_MAX;

  EkShotRay r;
  r.vx = ekm_sintable(shootAng + 512);
  r.vy = ekm_sintable(shootAng);
  // Two stages as the engine (player.cpp:521 <<5, :556 <<6), as multiplies: shifting a negative is undefined before
  // C++20.
  r.vz = (whole * 32) * 64;
  return r;
}

// The ray at a delta (origin z is the muzzle's), quantised as the shot is.
inline EkShotRay ekm_shot_ray_at(int dx, int dy, int dz, int dist2d) {
  return ekm_shot_ray(ekm_f16(ekm_aim_yaw(dx, dy)),
                      ekm_q16horiz_for(dz, dist2d), 0);
}
