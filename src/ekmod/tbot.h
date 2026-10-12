#pragma once

// The triggerbot: hold the fire bit while a condition holds. Decisions live here for the suite; tbot.cpp ORs one bit
// (the player's own fire bit, so the shot takes the real weapon path) into this tic's input record, never clearing it.

#include "aimsolve.h"
#include "playerstate.h"
#include "wallstate.h"

const int TBOT_WHEN_CROSSHAIR = 0;
const int TBOT_WHEN_AIMBOT    = 1;

const int TBOT_DELAY_MS_MIN = 0, TBOT_DELAY_MS_MAX = 500;
const int TBOT_DELAY_MS_DEFAULT = TBOT_DELAY_MS_MIN;
const int TBOT_TICS_PER_SECOND = 30;

// Saturates, not wraps.
const int TBOT_HELD_MS_CAP = 1000000;

// Most one frame adds to the held clock: past four tics a gap is a pause or load.
const int TBOT_FRAME_MS_MAX = (int)EKM_LEAD_GAP_MAX_MS;

// The crosshair cone approximates a hitscan: it misses thin cover between the ladder's rungs, glass or grates (cansee
// looks through) and an actor in the way. 24 units = 4.2 degrees, half the engine's AUTO_AIM_ANGLE (48, player.h:42),
// since autoaim may be off.
const int TBOT_CROSSHAIR_YAW_UNITS = 24;

// Vertical cone in horiz units: horiz is a slope, F16(100) + F16(128)*tan(pitch) (player.cpp:3084), so 4.2 degrees =
// 9.4, rounded to 10.
const int TBOT_CROSSHAIR_HORIZ_UNITS = 10;

// ints, not bools: MSVC /O2 loses a bool member returned by value.
struct TbotState { int heldMs; int heldTics; int firing; };

struct TbotCond { int holds; const char* reason; };

inline int tbot_when_clamped(int when) {
  return when == TBOT_WHEN_AIMBOT ? TBOT_WHEN_AIMBOT : TBOT_WHEN_CROSSHAIR;
}

inline int tbot_delay_ms_clamped(int ms) {
  return ms < TBOT_DELAY_MS_MIN ? TBOT_DELAY_MS_MIN
                                : (ms > TBOT_DELAY_MS_MAX ? TBOT_DELAY_MS_MAX : ms);
}

// Milliseconds to whole tics, nearest. Integer: 30 tics/s is exact in thousandths and +500 rounds.
inline int tbot_delay_tics(int ms) {
  const int c = tbot_delay_ms_clamped(ms);
  return (c * TBOT_TICS_PER_SECOND + 500) / 1000;
}

// Tics held, counting the current. Against the tic clock, not frames (no tic hook: frames make 100 ms mean 12 ms at 240
// fps). Integer: 1000/30 is inexact (500/it = 14.999...), so both sides of `held > delay` use thousandths.
inline int tbot_held_tics(int heldMs) {
  if (heldMs <= 0) return 1;
  return 1 + (heldMs * TBOT_TICS_PER_SECOND) / 1000;
}

// Hitscan or projectile weapons. Excluded: knee, handremote (fire nothing), pipe bomb (throws on RELEASE,
// player.cpp:4496) and trip bomb (places on a wall).
inline bool tbot_weapon_usable(int weapon) {
  return ekm_weapon_is_hitscan(weapon) || ekm_weapon_proj_speed(weapon) > 0;
}

// `haveTarget` is chosen once a TIC; both offsets are re-solved every frame from one snapshot (ekm_aim_view_pair).
// Which enemy is a tic old, the angles are not.
inline TbotCond tbot_when_crosshair(bool haveTarget, int myYaw, int targetYaw,
                                    int myQ16Horiz, int targetQ16Horiz) {
  TbotCond c = { 0, "notarget" };
  if (!haveTarget) return c;
  if (ekm_angular_offset(myYaw, targetYaw) > TBOT_CROSSHAIR_YAW_UNITS) {
    c.reason = "offyaw";
    return c;
  }
  const int dh = targetQ16Horiz - myQ16Horiz;
  if ((dh < 0 ? -dh : dh) > ekm_f16(TBOT_CROSSHAIR_HORIZ_UNITS)) {
    c.reason = "offpitch";
    return c;
  }
  c.holds = 1;
  c.reason = "on";
  return c;
}

// A target is locked wherever the view points. Staleness is the caller's: tbot.cpp asks ekm_shot_fresh.
inline TbotCond tbot_when_aimbot(bool haveLock) {
  TbotCond c = { 0, "nolock" };
  if (!haveLock) return c;
  c.holds = 1;
  c.reason = "lock";
  return c;
}

// What the shot's own ray landed on: the cone and the lock both rely on a cansee ladder that looks through glass,
// grates and a dying enemy that still stops bullets.
const int TBOT_RAY_UNKNOWN = 0;
const int TBOT_RAY_ENEMY   = 1;
const int TBOT_RAY_BREAK   = 2;
const int TBOT_RAY_BLOCKED = 3;

// May only widen a refusal into a hold, passing refusals through with their reason. UNKNOWN is the old behaviour (a
// failed hitscan scan costs precision, not the feature). BREAKING holds: the first bullet opens the cover.
inline TbotCond tbot_ray_cond(TbotCond base, int ray) {
  if (!base.holds) return base;
  if (ray == TBOT_RAY_BLOCKED) { TbotCond c = { 0, "blocked" }; return c; }
  if (ray == TBOT_RAY_BREAK)   { TbotCond c = { 1, "breaking" }; return c; }
  return base;
}

// Can the shot be put on the target? The quantum, not height: pitch rounds to a whole horiz unit (P_PreFireHitscan,
// ekm_pitch_quantum_z), displacing the impact by range2d * EKM_Z_PER_XY / EKM_HORIZ_PER_SLOPE; at a turret 5873 away
// that is 734 z against margin 380 (39 rounds, 3 landed).
inline bool ekm_shot_placeable(double marginZ, int range2d, int q16horiz,
                               int q16horizoff) {
  const double quantum = ekm_pitch_quantum_z(range2d);
  if (quantum <= 0.0) return true;
  // Asks where THIS shot lands: the leftover fraction of F16(100) - q16horiz - q16horizoff (horizoff passed in;
  // q16horiz alone is wrong on slopes) times the quantum, within marginZ. No range means old behaviour.
  const double ideal = (double)q16horiz / 65536.0 + (double)q16horizoff / 65536.0;
  // Round half away from zero as fix16_to_int does; no <math.h>.
  const double rounded = (ideal >= 0.0) ? (double)(long long)(ideal + 0.5)
                                        : (double)(long long)(ideal - 0.5);
  double leftover = ideal - rounded;
  if (leftover < 0.0) leftover = -leftover;
  return leftover * quantum <= marginZ;
}

// Only turns a hold into a refusal.
inline TbotCond tbot_placed_cond(TbotCond base, double marginZ, int range2d,
                                int q16horiz, int q16horizoff) {
  if (!base.holds) return base;
  if (!ekm_shot_placeable(marginZ, range2d, q16horiz, q16horizoff)) {
    TbotCond c = { 0, "noroom" };
    return c;
  }
  return base;
}

// Cover one bullet opens: cases in A_DamageObject_Duke3D (sector.cpp:1995-2492) that unconditionally clear the blocking
// and hitscan bits. TREE1/2, TIRE, CONE, BOX, CACTUS need explosives; BGRATE1 596 is already open. Glass is a wall
// (below).
inline bool ekm_picnum_breaks_open(int picnum) {
  switch (picnum) {
    case 595:
    case 407:
    case 569:
    case 571:
    case 1113:
    case 342:
    case 343:
    case 4099:
      return true;
    default:
      return false;
  }
}

// Walls: A_DamageWall_Internal (sector.cpp:1570) clears blocking bits outright for GLASS, STAINGLASS1, FANSPRITE;
// forcefields stay solid, MIRROR needs explosives, the rest go through G_BreakWall, which changes only picnum. Reads
// overpicnum. Neighbour-floor guards (:1623) skipped: sector[] not located.
inline bool ekm_wall_breaks_open(int overpicnum, int cstat, int nextsector) {
  if (nextsector < 0) return false;
  if ((cstat & EK_WALL_CSTAT_MASKED) == 0) return false;
  switch (overpicnum) {
    case 503:
    case 510:
    case 407:
      return true;
    default:
      return false;
  }
}

// Every refusal resets the held clock; first refusal wins. WT's set plus the frozen player: the engine tests `pal == 1
// || ud.pause_on || gm & MODE_MENU` (player.cpp:2358). menuOpen misses Pause (screens.cpp:1300), or a paused frame
// would charge the delay and OR the fire bit.
inline bool tbot_decide(bool on, bool menuOpen, bool paused, bool dead, bool frozen,
                        bool panelOpen, bool weaponUsable, TbotCond cond,
                        int delayTics, int elapsedMs,
                        TbotState* st, const char** reason) {
  if (!st) { if (reason) *reason = "nostate"; return false; }

  const char* why = 0;
  if (!on)                why = "off";
  else if (menuOpen)      why = "menu";
  else if (paused)        why = "paused";
  else if (dead)          why = "dead";
  else if (frozen)        why = "frozen";
  else if (panelOpen)     why = "panel";
  else if (!weaponUsable) why = "weapon";
  else if (!cond.holds)   why = cond.reason ? cond.reason : "cond";
  if (why) {
    st->heldMs = 0; st->heldTics = 0; st->firing = 0;
    if (reason) *reason = why;
    return false;
  }

  int add = elapsedMs;
  if (add < 0) add = 0;
  if (add > TBOT_FRAME_MS_MAX) add = TBOT_FRAME_MS_MAX;
  if (st->heldMs < TBOT_HELD_MS_CAP) st->heldMs += add;

  st->heldTics = tbot_held_tics(st->heldMs);
  // Strictly greater: at delay 0 the first frame's heldTics of 1 fires immediately; >= would fire a tic early
  // elsewhere.
  st->firing = st->heldTics > delayTics ? 1 : 0;
  if (reason) *reason = st->firing ? "fire" : "delay";
  return st->firing != 0;
}

// After aim_frame in the SwapBuffers hook; the lock it reads is published there.
void tbot_frame(void);

bool tbot_firing(void);

// Instrumentation only: where the ray was cast from, to diff against the FIRE instant. They differ: the weapon fires in
// the TIC, our cast is at the swap after, so each ray gates the NEXT tic's shot. Game thread both ends; published only
// with debuglog on.
struct TbotCastRecord {
  int ox, oy, oz;
  int posZ;
  int pyoff;
  int pyoffOk;
  int osect;
  int q16ang, q16horiz, horizoff;
  // 1 when q16ang/horiz are silent aim's solution, not the player's view.
  int usedSolution;
  int targetSprite;
  int range2d;
  int fired;
  unsigned ms;
};

// Never true with the Debug log off.
bool tbot_cast_record(TbotCastRecord* out);
