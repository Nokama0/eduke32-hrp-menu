// The aim arithmetic only; engine-free so the test host checks it with no game.

#pragma once
#include <math.h>
#include "projmath.h"

const int EKM_YAW_UNITS   = 2048;
const int EKM_HORIZ_MIN   = -99;
const int EKM_HORIZ_MAX   = 299;
const int EKM_HORIZ_LEVEL = 100;

// The run under horiz's rise: horiz = F16(100) + F16(128)*tan(pitch) (player.cpp:3084). The pitch quantum must agree
// with it.
const int EKM_HORIZ_PER_SLOPE = 128;
const int EKM_UNITS_PER_METRE = 512;

// Engine tic in ms (30/s). Every rate here is per tic, not per frame.
constexpr double EKM_MS_PER_TIC = 1000.0 / 30.0;

constexpr int EKM_LEAD_TICS_MAX = 4;

// Acquisition runs once per TIC, not per frame: per-frame ran the candidate loop 2-4 times a tic on unmoved actors, and
// the 64-probe budget that bounded it hid the nearest enemy at 32.9 m. Gate truncated (33 vs 33.33): early is harmless,
// never firing cannot happen.
constexpr unsigned EKM_ACQUIRE_GAP_MS = (unsigned)EKM_MS_PER_TIC;

// `everAcquired` false is the first frame and must acquire. Unsigned: a wrapped stamp acquires.
inline bool ekm_acquire_due(bool everAcquired, unsigned msSinceAcquire) {
  if (!everAcquired) return true;
  return msSinceAcquire >= EKM_ACQUIRE_GAP_MS;
}

const unsigned EKM_VEL_STALE_MS = 83;

// Longest gap a position delta may span and still be motion; past it the delta is discarded (ekm_lead_divisor). Derived
// from the tic, not a rounded 133.
constexpr unsigned EKM_LEAD_GAP_MAX_MS =
    (unsigned)(EKM_MS_PER_TIC * EKM_LEAD_TICS_MAX + 0.5);

inline int ekm_f16(int whole) { return whole * 65536; }

// Build's angle as getangle (engine.cpp:12464); within one of 2048 units.
inline int ekm_aim_yaw(int dx, int dy) {
  if (dx == 0 && dy == 0) return 0;
  const double a = atan2((double)dy, (double)dx) * (EKM_YAW_UNITS / 2) / 3.14159265358979323846;
  int v = (int)(a + (a >= 0 ? 0.5 : -0.5));
  v &= (EKM_YAW_UNITS - 1);
  return v;
}

inline int ekm_yaw_delta(int from, int to) {
  int d = (to - from) & (EKM_YAW_UNITS - 1);
  if (d > EKM_YAW_UNITS / 2) d -= EKM_YAW_UNITS;
  return d;
}

// Short way round in 16.16 over the full circle: dropping the mouse's fraction left a permanent jitter floor. Masked in
// unsigned so any inputs are legal (the engine masks q16ang, player.cpp:3080).
const int EKM_YAW_UNITS_Q16 = EKM_YAW_UNITS * 65536;

inline int ekm_yaw_delta_q16(int fromQ16, int toQ16) {
  const unsigned d = ((unsigned)toQ16 - (unsigned)fromQ16) &
                     ((unsigned)EKM_YAW_UNITS_Q16 - 1u);
  return (int)d > EKM_YAW_UNITS_Q16 / 2 ? (int)d - EKM_YAW_UNITS_Q16 : (int)d;
}

inline int ekm_angular_offset(int a, int b) {
  const int d = ekm_yaw_delta(a, b);
  return d < 0 ? -d : d;
}

// q16horiz for a target dz below the eye: horiz is a slope (player.cpp:3084); z is 16x the horizontal unit so the rise
// is dz/16; negated since z grows downward.
inline int ekm_q16horiz_for(int dz, int dist2d) {
  if (dist2d <= 0) return ekm_f16(EKM_HORIZ_LEVEL);
  const double slope = (-(double)dz / (double)EKM_Z_PER_XY) / (double)dist2d;
  const double v = (double)ekm_f16(EKM_HORIZ_LEVEL)
                 + (double)EKM_HORIZ_PER_SLOPE * 65536.0 * slope;
  if (v > 2147483000.0) return 2147483000;
  if (v < -2147483000.0) return -2147483000;
  return (int)v;
}

// One snapshot gives both yaws: when acquisition went per-tic, the player's yaw and the target's could be a tic apart
// (a 300 units/tic crosser at 1000 swings 95 units vs a cone of 24); one call makes it structural (tb_0071). Pure
// arithmetic, every frame. lastYaw/lastDist are kept when the target has no direction (yaw 0 would snap east).
struct EkAimViewPair {
  int myYaw;
  int yaw;
  int q16horiz;
  int dist2d;
};

inline EkAimViewPair ekm_aim_view_pair(int meAngQ16, int meHorizoff,
                                       int eyeX, int eyeY, int eyeZ,
                                       int tx, int ty, int tz,
                                       int lastYaw, int lastDist) {
  EkAimViewPair p;
  p.myYaw = (meAngQ16 >> 16) & (EKM_YAW_UNITS - 1);
  p.yaw = lastYaw;
  p.dist2d = lastDist;
  const int dx = tx - eyeX, dy = ty - eyeY;
  if (dx != 0 || dy != 0) {
    p.dist2d = (int)sqrt((double)dx * dx + (double)dy * dy);
    p.yaw = ekm_aim_yaw(dx, dy);
  }
  p.q16horiz = ekm_q16horiz_for(tz - eyeZ, p.dist2d) - meHorizoff;
  return p;
}

// How far one whole horiz unit moves the impact: P_PreFireHitscan rounds F16(100) - q16horiz - q16horizoff to a whole
// unit (player.cpp:498, :521), displacing it by d * EKM_Z_PER_XY / EKM_HORIZ_PER_SLOPE z; 734 at range 5873 vs a body
// 3040 tall. Not the cause of 39 chaingun rounds landing 3 (that was spread and silent aim's auto_aim 2, fixed).
inline double ekm_pitch_quantum_z(int range2d) {
  if (range2d <= 0) return 0.0;
  return (double)range2d * (double)EKM_Z_PER_XY / (double)EKM_HORIZ_PER_SLOPE;
}

// Which candidate acquisition prefers; a rule so the winner and the best view-pointable accumulators rank alike. Tier
// first (live outranks frozen or choking), then angular offset (crosshair mode) or distance; ties keep the first found.
inline bool ekm_cand_better(bool have, bool crosshair, int tier, int off,
                            int dist, int bestTier, int bestOff, int bestDist) {
  if (!have || tier < bestTier) return true;
  if (tier != bestTier) return false;
  return crosshair ? (off < bestOff || (off == bestOff && dist < bestDist))
                   : (dist < bestDist);
}

// P_GetInput clamps q16horiz to MIN..MAX once per tic (player.cpp:3179); steering past it parks the view at the limit.
inline bool ekm_horiz_reachable(int q16horiz) {
  return q16horiz >= ekm_f16(EKM_HORIZ_MIN) && q16horiz <= ekm_f16(EKM_HORIZ_MAX);
}

// Shot pitch bound in whole horiz units: zvel is (100 - horiz - horizoff) << 5, then << 6 in hitscan (player.cpp:498),
// so << 11; 4096 leaves 8 bits of int32 headroom and a steepest shot of 88.2 degrees. A clamped ray is not the pitch
// asked for.
const int EKM_SHOT_ZVEL_WHOLE_MAX = 4096;

// The shot's bound is not the view's: silent aim sets q16horiz after P_GetInput's clamp and the engine derives zvel
// unclamped (player.cpp:521).
const int EKM_SHOT_HORIZ_MIN = EKM_HORIZ_LEVEL - EKM_SHOT_ZVEL_WHOLE_MAX;
const int EKM_SHOT_HORIZ_MAX = EKM_HORIZ_LEVEL + EKM_SHOT_ZVEL_WHOLE_MAX;

// Acquisition uses this unconditionally: silent aim on must not change which enemy the aimbot picks.
inline bool ekm_shot_horiz_reachable(int q16horiz) {
  return q16horiz >= ekm_f16(EKM_SHOT_HORIZ_MIN) &&
         q16horiz <= ekm_f16(EKM_SHOT_HORIZ_MAX);
}

// The shot's angle, solved from the muzzle it leaves. A point, not an angle: an angle goes stale when the player moves
// (jetpack ascent of 2048 z per tic put rounds 2048 high). Led first, raw fallback, refuse last, with the SHOT's bound.
// Pure: the caller is the A_ShootWithZvel detour mid-tic.
struct EkShotSolve {
  int yaw;
  int q16horiz;
  int ok;  // INT, not bool (MSVC /O2).
  int usedLed;
};

inline EkShotSolve ekm_shot_solve(int muzzleX, int muzzleY, int muzzleZ,
                                  int ledX, int ledY, int ledZ,
                                  int rawX, int rawY, int rawZ,
                                  int q16horizoff) {
  // Raw line first: it is the fallback and lends yaw and range when the lead has no 2D offset.
  const int rdx = rawX - muzzleX, rdy = rawY - muzzleY;
  int rawYaw = 0, rawDist = 0;
  if (rdx != 0 || rdy != 0) {
    rawDist = (int)sqrt((double)rdx * rdx + (double)rdy * rdy);
    rawYaw = ekm_aim_yaw(rdx, rdy);
  }

  const int sdx = ledX - muzzleX, sdy = ledY - muzzleY;
  int yaw = rawYaw, dist = rawDist;
  if (sdx != 0 || sdy != 0) {
    dist = (int)sqrt((double)sdx * sdx + (double)sdy * sdy);
    yaw = ekm_aim_yaw(sdx, sdy);
  }

  EkShotSolve out;
  out.usedLed = 1;
  out.q16horiz = ekm_q16horiz_for(ledZ - muzzleZ, dist) - q16horizoff;
  if (!ekm_shot_horiz_reachable(out.q16horiz)) {
    out.q16horiz = ekm_q16horiz_for(rawZ - muzzleZ, rawDist) - q16horizoff;
    yaw = rawYaw;
    out.usedLed = 0;
  }
  out.yaw = yaw;
  out.ok = ekm_shot_horiz_reachable(out.q16horiz) ? 1 : 0;
  return out;
}

// Fraction of the remaining delta per frame; the forced unit step stops it parking one unit short.
inline int ekm_smooth_step(int delta, int percent) {
  if (delta == 0) return 0;
  if (percent <= 0) return delta;
  if (percent >= 100) return 0;
  int step = (int)((long long)delta * (100 - percent) / 100);
  if (step == 0) step = delta > 0 ? 1 : -1;
  return step;
}

// A point on the body: rung 0 is the row's choice, the rest a ladder tried when it is not visible (aimwt.cpp:325).
inline int ekm_aim_z(int feetZ, float heightZ, int rung, int aimPoint) {
  const float h = heightZ < 0.0f ? 0.0f : heightZ;
  float up;
  // Four rungs, four heights: a fixed h*0.5 equalled rung 0 on Centre and wasted a quarter of the probes.
  switch (rung) {
    case 0:  up = aimPoint ? h * 0.75f : h * 0.5f;  break;
    case 1:  up = h * 0.875f; break;
    case 2:  up = aimPoint ? h * 0.5f : h * 0.75f;  break;
    default: up = h * 0.25f;  break;
  }
  return feetZ - (int)up;
}

// Pass 2: midpoint of the longest VISIBLE span. The ladder returns the first rung that answers, the worst point
// (silhouette top, where motion, lead and spread fall off).

// Nine samples over 0.125..0.875: feet in the floor plane and the top pixel are points a probe answers for and a bullet
// misses. Centred so a fully visible body still gives 0.5; odd so the centre is a sample.
const int EKM_SPAN_SAMPLES = 9;

// Pass 2's own budget: sharing the selection's skipped refinement in busy rooms. All nine samples or none; at most
// nine casts a frame.
const int EKM_SPAN_BUDGET = EKM_SPAN_SAMPLES;

// Is a sample a point the bullet reaches: the ray for it is cast and must land on the target sprite itself (answers
// glass, grates and blockers). Pitfalls: no answer is not visibility, `hit.sprite >= 0` accepts anything, a wall hit
// leaves -1 so `== -1` accepts walls.
inline bool ekm_span_sample_hit(bool castOk, int hitSprite, int targetSprite) {
  return castOk && targetSprite >= 0 && hitSprite == targetSprite;
}
const float EKM_SPAN_LO = 0.125f;
const float EKM_SPAN_HI = 0.875f;
const float EKM_SPAN_STEP =
    (EKM_SPAN_HI - EKM_SPAN_LO) / (float)(EKM_SPAN_SAMPLES - 1);

inline int ekm_span_prefer(int aimPoint) {
  const float want = aimPoint ? 0.75f : 0.5f;
  int idx = (int)((want - EKM_SPAN_LO) / EKM_SPAN_STEP + 0.5f);
  if (idx < 0) idx = 0;
  if (idx >= EKM_SPAN_SAMPLES) idx = EKM_SPAN_SAMPLES - 1;
  return idx;
}

// Sample height fraction, clamped. The row's own sample is its exact height (the grid's 0.78125 would shift an
// unobstructed Head aim point from 0.75); snapping costs no probe.
inline float ekm_span_frac(int sample, int aimPoint = 0) {
  const int i = sample < 0 ? 0 : (sample >= EKM_SPAN_SAMPLES ? EKM_SPAN_SAMPLES - 1 : sample);
  if (i == ekm_span_prefer(aimPoint)) return aimPoint ? 0.75f : 0.5f;
  return EKM_SPAN_LO + (float)i * EKM_SPAN_STEP;
}

inline int ekm_span_z(int feetZ, float heightZ, int sample, int aimPoint = 0) {
  const float h = heightZ < 0.0f ? 0.0f : heightZ;
  return feetZ - (int)(h * ekm_span_frac(sample, aimPoint));
}

// Z between a sample and the NEARER edge of its run. Half a step credit per side, since the run edge is only the last
// place we looked: a one-sample run measured zero and the triggerbot refused forever (E1L4: run 8..8 of 9 on a 14560
// body, 682 z). tb_0060 pins it. Shared by ekm_span_mid and tbot.h so room has one definition.
inline double ekm_span_margin_z(int sample, int lo, int hi, double heightZ,
                                int aimPoint = 0) {
  const double h = heightZ > 0.0 ? heightZ : 0.0;
  const double f = (double)ekm_span_frac(sample, aimPoint);
  const double half = (double)EKM_SPAN_STEP * 0.5;
  const double dLo = f - (double)ekm_span_frac(lo, aimPoint) + half;
  const double dHi = (double)ekm_span_frac(hi, aimPoint) - f + half;
  double d = dLo < dHi ? dLo : dHi;
  if (d < 0.0) d = 0.0;
  return d * h;
}

// Sample to aim at given the visible mask; -1 keeps the current answer. Longest run, not centroid (a railing leaves
// head and legs visible). The configured point wins inside the run, but only with a quantum of margin to BOTH edges (39
// rounds at a turret landed 3); else the run midpoint. Ties go to the run nearest preferIdx.
inline int ekm_span_mid(const unsigned char* vis, int n, int preferIdx,
                        int* first = 0, int* last = 0,
                        float heightZ = 0.0f, int range2d = 0,
                        int aimPoint = 0, int* tight = 0) {
  const double quantum = ekm_pitch_quantum_z(range2d);
  const double h = heightZ > 0.0f ? (double)heightZ : 0.0;
  int bestMid = -1, bestLen = 0, bestDist = 0, bestLo = 0, bestHi = 0;
  if (vis) {
    for (int i = 0; i < n; ) {
      if (!vis[i]) { i++; continue; }
      const int lo = i;
      while (i < n && vis[i]) i++;
      const int hi = i - 1;
      const int len = hi - lo + 1;
      const bool holds = preferIdx >= lo && preferIdx <= hi;
      const bool room =
          holds && ekm_span_margin_z(preferIdx, lo, hi, h, aimPoint) >= quantum;
      const int mid = room ? preferIdx : (lo + hi + 1) / 2;
      const int d = mid > preferIdx ? mid - preferIdx : preferIdx - mid;
      if (bestMid < 0 || len > bestLen || (len == bestLen && d < bestDist)) {
        bestMid = mid; bestLen = len; bestDist = d; bestLo = lo; bestHi = hi;
      }
    }
  }
  if (first) *first = bestMid < 0 ? -1 : bestLo;
  if (last)  *last  = bestMid < 0 ? -1 : bestHi;
  if (tight)
    *tight = (bestMid >= 0 && quantum > 0.0 &&
              ekm_span_margin_z(bestMid, bestLo, bestHi, h, aimPoint) < quantum)
                 ? 1 : 0;
  return bestMid;
}

inline bool ekm_span_is_full(int first, int last, int n) {
  return n > 0 && first == 0 && last == n - 1;
}

// Tics a position delta spans. Above 30 fps the divisor is 1: two changed readings are already one tic apart, and
// dividing by frames would halve every lead.
inline int ekm_lead_tics(unsigned msElapsed) {
  const int t = (int)((double)msElapsed / EKM_MS_PER_TIC + 0.5);
  if (t < 1) return 1;
  if (t > EKM_LEAD_TICS_MAX) return EKM_LEAD_TICS_MAX;
  return t;
}

// Divisor for a delta over msElapsed, or 0 to DISCARD. Capping lies: an enemy that walked 2000 units over three seconds
// had it divided by four, an aim point deep in the floor.
inline int ekm_lead_divisor(unsigned msElapsed) {
  if (msElapsed > EKM_LEAD_GAP_MAX_MS) return 0;
  return ekm_lead_tics(msElapsed);
}

// Unchanged this long means stopped; velocity goes with it, or a standing enemy is led forever.
inline bool ekm_velocity_stale(unsigned msSinceChange) {
  return msSinceChange >= EKM_VEL_STALE_MS;
}

// How far into the tic, 0..1. The aim point must move as the sprite does: actors are drawn at bpos + smoothratio*(pos -
// bpos) (game.cpp:3882) while targets read the raw position, so the aim point jumped at tic boundaries. NaN floors to
// 0.
inline double ekm_smoothratio(double msSinceTic) {
  if (!(msSinceTic > 0.0)) return 0.0;
  const double r = msSinceTic / EKM_MS_PER_TIC;
  return r >= 1.0 ? 1.0 : r;
}

inline int ekm_lerp_pos(int from, int to, double ratio) {
  if (from == to) return from;
  if (!(ratio > 0.0)) return from;
  if (ratio >= 1.0) return to;
  const double v = (double)from + ((double)to - (double)from) * ratio;
  return (int)(v + (v >= 0.0 ? 0.5 : -0.5));
}

// Fraction retained per TIC over `tics`, so smoothing means the same at 30 and 240 fps. Step = delta*(1 -
// (percent/100)^tics), equal to ekm_smooth_step at tics == 1 (checked over two million pairs). Keeps the forced unit
// step.
inline int ekm_smooth_step_tics(int delta, int percent, double tics) {
  if (delta == 0) return 0;
  if (percent <= 0) return delta;
  if (percent >= 100) return 0;
  if (tics <= 0.0) return 0;
  const double keptN = pow((double)percent, tics);
  const double fullN = pow(100.0, tics);
  int step = (int)((double)delta * (fullN - keptN) / fullN);
  if (step == 0) step = delta > 0 ? 1 : -1;
  return step;
}

// Where a target moving at v will be when a projectile arrives: two iterations of t = dist/vel (as WT). 2D only: z is
// on another scale.
inline void ekm_lead_point(int tx, int ty, int tz, int vx, int vy, int vz,
                           int sx, int sy, int projVel, int* ox, int* oy, int* oz) {
  int px = tx, py = ty, pz = tz;
  if (projVel > 0) {
    for (int i = 0; i < 2; i++) {
      const double dx = (double)px - (double)sx;
      const double dy = (double)py - (double)sy;
      const double d = sqrt(dx * dx + dy * dy);
      if (d <= 0.0) { px = tx; py = ty; pz = tz; break; }
      const double t = d / (double)projVel;
      px = tx + (int)(vx * t);
      py = ty + (int)(vy * t);
      pz = tz + (int)(vz * t);
    }
  }
  if (ox) *ox = px;
  if (oy) *oy = py;
  if (oz) *oz = pz;
}

inline int ekm_lead_scaled(int raw, int led, int percent) {
  return raw + (int)((long long)(led - raw) * percent / 100);
}

inline int ekm_metres(int units) { return units / EKM_UNITS_PER_METRE; }

// Compared against dist2d, never depth (under-reads off-axis, moves with FOV; WT's finding). `unlimited` is its own
// term so the slider keeps meaning metres.
inline bool ekm_within_draw_distance(int dist2d, int limit, int unlimited) {
  if (unlimited) return true;
  if (dist2d < 0) return false;
  return dist2d <= limit;
}

// Is the redirect yaw within maxDegrees of the view? Wrapped the short way (2040 vs 10 is 18 apart). 2048 units per
// turn, so 180 degrees is 1024; zero or below redirects nothing.
inline bool ekm_redirect_within(int fromYaw, int toYaw, int maxDegrees) {
  if (maxDegrees <= 0) return false;
  const int deg = maxDegrees > 180 ? 180 : maxDegrees;
  return ekm_angular_offset(fromYaw, toYaw) <= deg * EKM_YAW_UNITS / 360;
}

// Older than this was not computed for the situation fired into; same ceiling as EKM_LEAD_GAP_MAX_MS.
const unsigned EKM_SHOT_STALE_MS = EKM_LEAD_GAP_MAX_MS;

// Is a published solution still the one to shoot with? In time, not frames: tics run before the draw, so a frame
// counter does not advance where nothing swapped and a pre-level-change solution could pass the angle bound. Unsigned:
// wraps survive, a future stamp is refused.
inline bool ekm_shot_fresh(unsigned msSincePublish) {
  return msSincePublish <= EKM_SHOT_STALE_MS;
}
