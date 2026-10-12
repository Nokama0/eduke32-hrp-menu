#pragma once
// The free camera: pure state and step integration, no engine memory or hooks. Shape from World Tour's freecam.h; rates
// re-derived for this game's 30 Hz tic.

#include "esp.h"
#include "ui/common.h"
#include "shotray.h"
#include "aimsolve.h"
#include "projmath.h"

const int FREECAM_SPEED_MIN     = 1;
const int FREECAM_SPEED_MAX     = 500;
const int FREECAM_SPEED_DEFAULT = 100;

// 100% = 128 units per tic = 7.5 m/s at 30 Hz.
const int FREECAM_BASE_SPEED = BUILD_UNITS_PER_METRE / 4;

// Build z is 16x x/y, aliased from projmath.h. PHEIGHT (9728) is the sprite's z offset, NOT the eye height (see
// EK_TP_PHEIGHT).
const int FREECAM_Z_PER_XY = (int)EKM_Z_PER_XY;

const int FREECAM_ANG_UNITS = EKM_YAW_UNITS;

const int FREECAM_SIN_SCALE = 16384;

// Pitch as a free angle, bounded short of vertical so the horiz derivation never divides by a zero cosine; 500 units ~
// 87.9 degrees.
const int FREECAM_PITCH_MAX = 500;

const int FREECAM_HORIZ_LEVEL = EKM_HORIZ_LEVEL;

// Milli angle units per mouse count, so a fractional rate survives integers; yawRem/pitchRem carry the remainder.
const int FREECAM_SENS_MILLI_UNIT = 1000;
const int FREECAM_SENS_MILLI_MIN  = 1;
const int FREECAM_SENS_MILLI_MAX  = 10000;

struct FreecamSens { int yawMilli, pitchMilli; };

inline int freecam_sens_milli_clamped(int m) {
  return clamp_int(m, FREECAM_SENS_MILLI_MIN, FREECAM_SENS_MILLI_MAX);
}

inline FreecamSens freecam_sens_default() {
  FreecamSens s = { FREECAM_SENS_MILLI_UNIT, FREECAM_SENS_MILLI_UNIT };
  return s;
}

// Pose plus the previous tic's. The p-prefixed fields are the camera's opos/oq16ang: it steps at 30 Hz, so at 240 fps
// it would update every eighth frame; G_DrawRooms interpolates the player by smoothRatio (game.cpp:1001-1029) and this
// pair does the same.
struct FreecamState {
  int x, y, z;
  int ang;
  int pitch;
  int px, py, pz;
  int pang, ppitch;
  int yawRem, pitchRem;
  bool active;
};

// Collapse only the previous POSITION: a transport is a jump the camera does not turn through.
inline void freecam_pose_carry_pos(FreecamState* s) {
  if (!s) return;
  s->px = s->x;
  s->py = s->y;
  s->pz = s->z;
}

// Collapse the previous pose onto the current at each tic start and at every discontinuity, so no frame interpolates a
// streak.
inline void freecam_pose_carry(FreecamState* s) {
  if (!s) return;
  freecam_pose_carry_pos(s);
  s->pang = s->ang;
  s->ppitch = s->pitch;
}

struct FreecamInput { int dx, dy; bool fwd, back, left, right, up, down, fast, slow; };

inline int freecam_speed_clamped(int speedPct) {
  return clamp_int(speedPct, FREECAM_SPEED_MIN, FREECAM_SPEED_MAX);
}

// Fast doubles, slow halves, never below 1; both together CANCEL.
inline int freecam_speed(int speedPct, bool fast, bool slow) {
  int v = FREECAM_BASE_SPEED * speedPct / 100;
  if (fast && !slow) v *= 2;
  else if (slow && !fast) v /= 2;
  return v < 1 ? 1 : v;
}

// horiz = 100 + 128*sin/cos (player.cpp:3084 forwards). The camera's horiz never passes P_UpdateAngles' clamp
// (player.cpp:3179), so it may look past the player's limit; aimsolve.h's shot bound rests on the same fact.
inline int freecam_pitch_to_horiz(int pitch) {
  const int sinP = ekm_sintable(pitch);
  const int cosP = ekm_sintable(pitch + FREECAM_ANG_UNITS / 4);
  if (cosP == 0) return sinP >= 0 ? 32767 : -32768;
  return FREECAM_HORIZ_LEVEL + (128 * sinP) / cosP;
}

// Inverse by linear search over the forward function. Ties keep the candidate nearest 0: truncation makes a plateau,
// and horiz_to_pitch(LEVEL) must be 0.
inline int freecam_horiz_to_pitch(int horiz) {
  int best = 0;
  int bestDiff = -1;
  for (int p = -FREECAM_PITCH_MAX; p <= FREECAM_PITCH_MAX; p++) {
    const int h = freecam_pitch_to_horiz(p);
    int diff = h - horiz;
    if (diff < 0) diff = -diff;
    const int absP = p < 0 ? -p : p;
    const int absBest = best < 0 ? -best : best;
    if (bestDiff < 0 || diff < bestDiff ||
        (diff == bestDiff && absP < absBest)) {
      bestDiff = diff; best = p;
    }
  }
  return best;
}

// Integrates one tic; untouched when inactive. Travel follows the full look direction (xy = v*cos, z =
// -Z_PER_XY*v*sin); strafe stays horizontal. `sens` keeps the modulo remainder so a slow mouse still turns.
inline void freecam_step(FreecamState* s, const FreecamInput& in,
                          int speedPct, const FreecamSens& sens) {
  if (!s || !s->active) return;

  const int yawNum = in.dx * sens.yawMilli + s->yawRem;
  s->ang = (s->ang + yawNum / FREECAM_SENS_MILLI_UNIT) & (FREECAM_ANG_UNITS - 1);
  s->yawRem = yawNum % FREECAM_SENS_MILLI_UNIT;

  const int pitchNum = -in.dy * sens.pitchMilli + s->pitchRem;
  int pitch = s->pitch + pitchNum / FREECAM_SENS_MILLI_UNIT;
  s->pitchRem = pitchNum % FREECAM_SENS_MILLI_UNIT;
  s->pitch = clamp_int(pitch, -FREECAM_PITCH_MAX, FREECAM_PITCH_MAX);

  const int speed = freecam_speed(speedPct, in.fast, in.slow);

  const int cosA = ekm_sintable(s->ang + FREECAM_ANG_UNITS / 4);
  const int sinA = ekm_sintable(s->ang);
  const int cosP = ekm_sintable(s->pitch + FREECAM_ANG_UNITS / 4);
  const int sinP = ekm_sintable(s->pitch);

  int moveFwd = 0, moveStrafe = 0, moveUp = 0;
  if (in.fwd)  moveFwd += speed;
  if (in.back) moveFwd -= speed;
  if (in.right) moveStrafe += speed;
  if (in.left)  moveStrafe -= speed;
  if (in.up)   moveUp += speed;
  if (in.down) moveUp -= speed;

  const int fwdXY = moveFwd * cosP / FREECAM_SIN_SCALE;
  s->x += (fwdXY * cosA) / FREECAM_SIN_SCALE - (moveStrafe * sinA) / FREECAM_SIN_SCALE;
  s->y += (fwdXY * sinA) / FREECAM_SIN_SCALE + (moveStrafe * cosA) / FREECAM_SIN_SCALE;
  s->z += -(moveFwd * sinP / FREECAM_SIN_SCALE) * FREECAM_Z_PER_XY
          - moveUp * FREECAM_Z_PER_XY;
}

// FOLLOWING A TELEPORTER OR WATER SURFACE: G_MoveTransports (actors.cpp:3908) carries the player across a paired SE 7.
// None of World Tour's four z constants survived; derivations in docs/re/freecam.md.
const int FREECAM_TRANSPORT_STAT = 9;
// Airborne carry window and landing distance (actors.cpp:3993, :4001-4004).
const int FREECAM_TRANSPORT_ZBAND = 6144;

// P_Emerge's band below the ceiling (actors.cpp:3843).
const int FREECAM_TRANSPORT_EMERGE = 1080;
// Hold in tics; its SIGN is which arm armed it, the engine's encoding: a paired surface sets -2 (actors.cpp:3878) and a
// negative hold counts up out of reach of the positive top-up (player.cpp:5233-5237). The sign scopes the top-up;
// applying it to a water pair made it one-way.
const int FREECAM_TRANSPORT_WATER_HOLD_TICS = -2;

const int FREECAM_TRANSPORT_WARP_HOLD_TICS = 2;

// THE VISIBLE TELEPORTER IS DESIGNED, NOT PORTED: World Tour and Megaton refuse a floor pad. The engine's on_ground
// gate (actors.cpp:3947) is meaningless for a camera, so its position is ported: at most floorzoffset (40<<8,
// premap.cpp:660) above the floor.
const int FREECAM_TRANSPORT_STAND = 10240;
// The engine's hold for this branch (actors.cpp:3972). The z edge refuses only the landing point, so the top-up below
// is the rest of the answer.
const int FREECAM_TRANSPORT_FLOOR_HOLD_TICS = 13;

// `lotag` is the effector SECTOR's (0 none, 1 above water, 2 underwater; game.h:535), -1 unreadable. `tempData4` is
// t_data[4], the ONFLOORZ flag for SE 7 (game.cpp:2930).
struct FreecamTransport { int lotag, seX, seY, seZ, ownerX, ownerY, ownerZ,
                          ownerCeilZ, ownerFloorZ, secFloorZ, secCeilZ, tempData4; };
// int flags only (MSVC bool fold). `hold` is the tics every transport is then refused.
struct FreecamJump { int hit, x, y, z, hold; };

// Whether this effector carries a camera at (x,y,z), and where. `dz` stands in for vel.z (positive is down). The
// direction term is required: each water landing is the paired plane exactly, so inside the other half's trigger;
// velocity separates them (actors.cpp:3815, :3843). A camera's downward step stands for the dive.
inline FreecamJump freecam_transport_jump(const FreecamTransport& t, int x, int y,
                                          int z, int dz) {
  FreecamJump j = { 0, x, y, z, 0 };
  if (t.seX == t.ownerX && t.seY == t.ownerY && t.seZ == t.ownerZ) return j;
  // The z windows are tested against the SEGMENT travelled this tic: a fast camera steps clean over both bands. They
  // reduce to the point form at dz 0 and keep strict edges.
  const long long zPrev = (long long)z - (long long)dz;
  const long long zLo = (zPrev < (long long)z) ? zPrev : (long long)z;
  const long long zHi = (zPrev < (long long)z) ? (long long)z : zPrev;

  int hit = 0, zTo = z, hold = 0;
  if (t.tempData4 == 0) {
    hit = (zLo < (long long)t.seZ + FREECAM_TRANSPORT_ZBAND &&
           zHi > (long long)t.seZ - FREECAM_TRANSPORT_ZBAND) ? 1 : 0;
    // The landing's sign is the camera's direction (actors.cpp:4001-4004); either way it is exactly ZBAND away, which
    // the far half's strict test refuses.
    zTo = (dz < 0) ? t.ownerZ - FREECAM_TRANSPORT_ZBAND
                   : t.ownerZ + FREECAM_TRANSPORT_ZBAND;
  } else if (t.lotag == 0) {
    // On the pad and no higher than a standing viewpoint; no velocity term (actors.cpp:3947). The landing is the far
    // window's edge, 512 above the engine's set-down (actors.cpp:3975-3976).
    hit = (zLo <= (long long)t.seZ &&
           zHi > (long long)t.seZ - FREECAM_TRANSPORT_STAND) ? 1 : 0;
    zTo = t.ownerZ - FREECAM_TRANSPORT_STAND;
    hold = FREECAM_TRANSPORT_FLOOR_HOLD_TICS;
  } else if (t.lotag == 1) {
    hit = (z >= t.secFloorZ && dz > 0) ? 1 : 0;
    zTo = t.ownerCeilZ;
    hold = FREECAM_TRANSPORT_WATER_HOLD_TICS;
  } else if (t.lotag == 2) {
    hit = (z < t.secCeilZ + FREECAM_TRANSPORT_EMERGE && dz <= 0) ? 1 : 0;
    zTo = t.ownerFloorZ;
    hold = FREECAM_TRANSPORT_WATER_HOLD_TICS;
  }
  if (!hit) return j;
  j.hit = 1;
  // All four arms keep the pair's separation; on the floor arm that deliberately diverges from the engine, which
  // ASSIGNS the far position (actors.cpp:3975): the player asked for dead-centre to dead-centre.
  j.x = clamp_ll_to_int((long long)x + t.ownerX - t.seX);
  j.y = clamp_ll_to_int((long long)y + t.ownerY - t.seY);
  j.z = zTo;
  j.hold = hold;
  return j;
}

struct FreecamHold { int tics, expired; };

inline FreecamHold freecam_transport_hold_next(int hold) {
  FreecamHold h = { 0, 0 };
  if (hold > 0) {
    h.tics = hold - 1;
    h.expired = (h.tics == 0) ? 1 : 0;
  } else if (hold < 0) {
    h.tics = hold + 1;
  }
  return h;
}

// The engine's top-up (player.cpp:5233-5234). "On a warping sector" is deliberately broad: a sprite sharing a sector
// with a PAIRED effector (actors.cpp:3943), since a narrower floor-arm test is false at the landing edge. Safe because
// the negative water hold never expires.
inline int freecam_transport_hold_topup(int expired, int onWarping) {
  return (expired && onWarping) ? FREECAM_TRANSPORT_WARP_HOLD_TICS : 0;
}

// Which sector's effectors may carry it: a seeded resolve that disagrees with the tracked sector means the camera left
// it (engine.cpp:13517-13520), so it refuses. Both answers are parameters because a call site once read updatesector's
// out-param in argument position and searched sector -1 every tic.
inline int freecam_transport_sector(bool inMap, int resolvedSect,
                                    int trackedSect, int maxSectors) {
  if (!inMap) return -1;
  if (resolvedSect < 0 || resolvedSect >= maxSectors) return -1;
  if (trackedSect < 0 || trackedSect >= maxSectors) return -1;
  if (resolvedSect != trackedSect) return -1;
  return trackedSect;
}


// Whether the substitution reaches the view. newowner >= 0 (game.cpp:999) draws from a viewscreen sprite;
// over_shoulder_on (game.cpp:1025-1040) moves position and sector. Both are refusals: they are the player's.
inline bool freecam_draw_reachable(int newowner, int overShoulderOn) {
  return newowner < 0 && overShoulderOn == 0;
}

// Which sector the substituted frame hands the renderer. The camera does not clip, so past a boundary updatesector
// answers none; refusing drew the PLAYER's view. Invalid (out of range) is dangerous, the draw walks the graph from it;
// stale but valid is what the engine does.
struct FreecamSectorPick { int sect; int last; };

inline FreecamSectorPick freecam_sector_pick(bool resolvedOk, int resolved,
                                             int lastSect, int maxSectors) {
  FreecamSectorPick r;
  if (resolvedOk && resolved >= 0 && resolved < maxSectors) {
    r.sect = resolved;
    r.last = resolved;
    return r;
  }
  if (lastSect >= 0 && lastSect < maxSectors) {
    r.sect = lastSect;
    r.last = lastSect;
    return r;
  }
  r.sect = -1;
  r.last = -1;
  return r;
}

// Which band the renderer accepts: polymer_drawrooms goes external when the sector is out of range or z is outside its
// band (polymer.cpp:1214-1251) and returns before polymer_drawsky (:1845): whole map, black sky. So prefer a reachable
// sector whose band contains z; nothing qualifying keeps today's answer.

// One candidate band. `exact` is 1 when the band is the whole truth (engine getzsofslopeptr, or flat with no slope bit,
// engine.cpp:14292); a sloped sector read flat is a guess. An int for the MSVC bool fold.
struct FreecamBand { int sect; int ceilz; int florz; int exact; };

// polymer.cpp:1215-1217 negated, inclusive at both ends.
inline bool freecam_band_contains(const FreecamBand& b, int z) {
  return z >= b.ceilz && z <= b.florz;
}

// List order is preference, but an EXACT band containing z beats a guess. Nothing containing z returns lastSect. A
// guessed candidate zero REFUSES the substitution (monotone): a false miss would seed a sector the camera is not in.
// The bound is load-bearing (game.cpp:1080-1082).
inline int freecam_band_pick(const FreecamBand* bands, int count, int z,
                             int lastSect, int maxSectors) {
  if (!bands || count <= 0) return lastSect;
  if (!bands[0].exact) return lastSect;
  int loose = -1;
  for (int i = 0; i < count; i++) {
    const FreecamBand& b = bands[i];
    if (b.sect < 0 || b.sect >= maxSectors) continue;
    if (!freecam_band_contains(b, z)) continue;
    if (b.exact) return b.sect;
    if (loose < 0) loose = b.sect;
  }
  return loose >= 0 ? loose : lastSect;
}


// When no band will do, widen the test's input: polymer.cpp:1211 fills both band locals from ONE getzsofslopeptr call,
// which camera.cpp redirects to a thunk obeying these rules.

struct FreecamWidened { int ceilz; int florz; };

// The engine is asked first and its answer EXTENDED, never replaced: only the plane z falls outside of moves, never
// inward, so a containing band returns byte for byte.
inline FreecamWidened freecam_band_widen(int ceilz, int florz, int z) {
  FreecamWidened r;
  r.ceilz = z < ceilz ? z : ceilz;
  r.florz = z > florz ? z : florz;
  return r;
}

// Exactly one call in the bracket may reach getzsofslopeptr (one call fills the two locals); a faulted scan cannot
// claim uniqueness.
inline bool freecam_redirect_locate_ok(int sites, int faulted) {
  return sites == 1 && faulted == 0;
}

// Sprites no longer gate the install: the sky arm hands sprite scanning to the portal walk; a missing distant actor was
// never reported, black sky three times.
inline bool freecam_redirect_install_ok(bool siteOk, bool sweepOk) {
  return siteOk && sweepOk;
}

// Armed by the two terms that decide whether the sweep runs: with it off, widening gives a sky over an almost empty
// world.
inline bool freecam_redirect_arm(bool wholeMapOn, bool sweepOk) {
  return wholeMapOn && sweepOk;
}

inline bool freecam_widen_now(bool inFlight) {
  return inFlight;
}

// Nothing aims or fires while the player is elsewhere: aim_frame and tbot_frame ran from the PLAYER's position during
// flight. A term of its own: screenshot mode stands them down only when on AND the panel is shut.
inline bool freecam_bots_run(bool cameraActive) {
  return !cameraActive;
}

// The panel and camera share one keyboard. The release of held keys is owed once, on the edge where the mod takes the
// keyboard from the game, never on a swap between owners (opening the panel mid-flight with W held forged a W release).
inline bool ekm_input_owned_edge(int wasOwned, int nowOwned) {
  return nowOwned != 0 && wasOwned == 0;
}

// Which drawrooms call is the player's: the bracketed function has TEN call sites in six functions. The return address
// excludes five owners but not G_DrawRooms' second call (game.cpp:870); the arguments exclude that one. Both are
// needed.

struct FreecamViewArgs { int x, y, z, angFix, horizFix, sect; };

// Half-open (EndAddress is past the function); an empty span matches NOTHING.
inline bool freecam_rva_in_span(unsigned rva, unsigned begin, unsigned end) {
  if (end <= begin) return false;
  return rva >= begin && rva < end;
}

inline bool freecam_same_view(const FreecamViewArgs& a, const FreecamViewArgs& b) {
  return a.x == b.x && a.y == b.y && a.z == b.z && a.angFix == b.angFix &&
         a.horizFix == b.horizFix && a.sect == b.sect;
}

inline bool freecam_is_player_view(unsigned retRva, unsigned drawBegin,
                                   unsigned drawEnd, const FreecamViewArgs& args,
                                   const FreecamViewArgs& cam) {
  return freecam_rva_in_span(retRva, drawBegin, drawEnd) &&
         freecam_same_view(args, cam);
}

// Is the bracketed draw still reached? An observation (no rendmode cvar), so revocable when the renderer changes. It
// does not prove which renderer drew: the hooked function is renderDrawRoomsQ16 (camera.cpp).

// Idle drawn frames before revoking: 300 is 2.5-5 s at 60-120 fps. Not a camera timeout.
const int FREECAM_DRAW_IDLE_FRAMES = 300;

struct FreecamDrawSeenVerdict { int idle; int revoke; };

// A zero is how "the detour ran" enters; no memory of revoking, so switching back restores the feature. A budget <= 0
// revokes nothing.
inline FreecamDrawSeenVerdict freecam_draw_seen_verdict(int idle, int threshold) {
  FreecamDrawSeenVerdict r;
  r.idle = idle;
  r.revoke = 0;
  if (threshold <= 0 || idle >= threshold) return r;
  r.idle = idle + 1;
  r.revoke = (r.idle == threshold) ? 1 : 0;
  return r;
}

// May the camera start? Each term refuses; nothing known is never go-ahead. Entry, exit and the dead-man's switch are
// separate rules (one symmetric rule once stuck the lockout on).
inline bool freecam_enter_rule(bool on, bool keyEdge, bool menuOpen, bool dead,
                                bool valid) {
  return on && keyEdge && !menuOpen && !dead && valid;
}

// Easier than enter: a key edge suffices and the menu does not block.
inline bool freecam_exit_rule(bool active, bool keyEdge, bool menuOpen) {
  return active && keyEdge;
}

// Must it come down regardless of any key? Row off, player died, or the read stopped resolving (level change).
inline bool freecam_force_off_rule(bool active, bool on, bool dead, bool valid) {
  return active && (!on || dead || !valid);
}

// Seqlock counter arithmetic (camera.cpp owns the counter): odd means a write is in flight.
struct FreecamPose { int x, y, z, ang, horiz; };

const int FREECAM_SMOOTH_UNIT = 65536;

// THE ENGINE'S TIC COUNTER. smoothRatio cannot be the clock: it clamps to 65536 when a frame exceeds a tic
// (baselayer.h:364-375), so at <= 30 fps the camera stops stepping. ototalclock advances per tic (game.cpp:7122). Any
// change is a tic, a decrease included; several tics step once.
inline bool freecam_tic_advanced(int prevTic, int curTic, bool havePrev) {
  if (!havePrev) return false;
  return curTic != prevTic;
}

// Accepting the clocks: totalclock is the `xor ax, ax` of ClockTicks::update() (clockticks.hpp:155), unique in the
// located drawrooms and agreeing with G_DrawRooms (game.cpp:786); ototalclock is what the game copies INTO totalclock
// at eight sites. Span: 16-byte ClockTicks in the image. Rules: docs/re/freecam.md.
inline bool ekm_tclk_shape_ok(int xors, int loads, int refptrs) {
  return xors == 1 && loads == 1 && refptrs == 1;
}

inline bool ekm_tclk_witness_ok(int drawRoomsLoads) {
  return drawRoomsLoads >= 1;
}

inline bool ekm_otclk_witness_ok(int sites, int distinctSources) {
  return sites >= 1 && distinctSources == 1;
}

inline bool ekm_tclk_span_ok(size_t slotRva, size_t addrRva, size_t imageSize,
                             int inData) {
  if (!inData || imageSize == 0) return false;
  if (slotRva == 0 || slotRva + sizeof(void*) > imageSize) return false;
  if (addrRva == 0 || addrRva >= imageSize) return false;
  return imageSize - addrRva >= 16;
}

// The pose a frame draws between the tic before and now (as game.cpp:1001-1029). Yaw takes the SHORT ARC or it spins
// the long way across zero. 64-bit: z can move tens of thousands of units a tic and times 65536 overflows.
inline FreecamPose freecam_pose_lerp(const FreecamPose& prev,
                                     const FreecamPose& cur, int ratio) {
  const int r = clamp_int(ratio, 0, FREECAM_SMOOTH_UNIT);
  const int half = FREECAM_ANG_UNITS / 2;
  const int dAng = ((cur.ang - prev.ang + half) & (FREECAM_ANG_UNITS - 1)) - half;
  FreecamPose p;
  p.x = prev.x + (int)((long long)(cur.x - prev.x) * r / FREECAM_SMOOTH_UNIT);
  p.y = prev.y + (int)((long long)(cur.y - prev.y) * r / FREECAM_SMOOTH_UNIT);
  p.z = prev.z + (int)((long long)(cur.z - prev.z) * r / FREECAM_SMOOTH_UNIT);
  p.ang = (prev.ang + (int)((long long)dAng * r / FREECAM_SMOOTH_UNIT))
          & (FREECAM_ANG_UNITS - 1);
  p.horiz = prev.horiz
            + (int)((long long)(cur.horiz - prev.horiz) * r / FREECAM_SMOOTH_UNIT);
  return p;
}

inline unsigned freecam_seq_write_begin(unsigned seq) { return seq + 1; }

inline unsigned freecam_seq_write_end(unsigned seq) { return seq + 1; }

// Good only if the counter was even and unchanged; else no pose.
inline bool freecam_seq_read_ok(unsigned before, unsigned after) {
  return (before & 1u) == 0u && before == after;
}

// REMOVE FOG. Polymer's one distance-dependent darkening term scales with globalvisibility. The field substituted is
// g_visibility, which engine.cpp:9460-9482 turns into globalvisibility at the head of renderDrawRoomsQ16 (written every
// frame, game.cpp:817), so this writes memory the engine already writes.

inline bool freecam_fog_substitute(bool cameraActive, bool fogSwitch) {
  return cameraActive && fogSwitch;
}

// RULE 1, the trace from engine.cpp:9480's `* 150` back through shl, dword read and pointer load, each unique in its
// window. The refptr load may repeat (mingw, same register): take the last, refuse on zero.
inline bool ekm_gvis_shape_ok(int imuls, int shls, int loads, int refptrs) {
  return imuls == 1 && shls == 1 && loads == 1 && refptrs >= 1;
}

// RULE 2, witnesses from source: every renderer arm reads g_visibility, and game.cpp:817 writes it once in G_DrawRooms.
inline bool ekm_gvis_witness_ok(int drawroomsLoads, int drawStores) {
  return drawroomsLoads >= 2 && drawStores == 1;
}

inline bool ekm_gvis_span_ok(size_t slotRva, size_t addrRva, size_t imageSize,
                             bool slotInData) {
  if (!slotInData) return false;
  if (slotRva == 0 || addrRva == 0) return false;
  if (slotRva >= imageSize) return false;
  if (addrRva + sizeof(int) > imageSize) return false;
  return true;
}

// THE TELEPORT: decision here; camera.cpp casts and writes. From Megaton with `bool ok` -> `int ok`. Both heights are
// the reference's own; an earlier "correction" was wrong (see EK_TP_PHEIGHT).

const int EK_TP_CLEARANCE = 256;

// Eye height, Megaton's 0x2800: floorzoffset 40<<8 (premap.cpp:660), pos.z = floorZ - floorZOffset (player.cpp:5414).
// NOT PHEIGHT (38<<8, the sprite's z offset): 9728 refuses fitting rooms and lands 512 low.
const int EK_TP_PHEIGHT = 10240;

// Megaton's PHEIGHT_MARGIN: P_ClampZ's ceilZ + PMINHEIGHT 1024 (player.cpp:5005-5006). The 4608 of P_DoJetpack is a
// different clamp.
const int EK_TP_CEIL_MARGIN = 1024;

const unsigned EK_TP_NOTICE_MS = 2000;

// Marker half-width 224 (0.44 m) and 0x3000 tall, deliberately under the 2 m footprint, which read too big.
const int EK_TP_MARK_HALF   = 224;
const int EK_TP_MARK_HEIGHT = 0x3000;

const int EK_TP_NOTICE_Y = 24;

const char* const EK_TP_NOTHING   = "Nothing to teleport to";
const char* const EK_TP_NO_SECTOR = "Camera is outside the map";
const char* const EK_TP_TIGHT     = "Not enough room to stand";
// The write's failure gets its own words: it is an engine fact, and "not enough room" would send the player elsewhere
// forever.
const char* const EK_TP_NO_WRITE  = "The player could not be moved";
// A sloped sector whose slope call did not answer: neither outside nor too tight.
const char* const EK_TP_NO_FLOOR  = "Could not read the floor there";
const char* const EK_TP_NONE      = "";

// May the teleport fire? Its own switch is deliberately absent: it is a way OUT of the free camera.
inline int ekm_tp_fire_rule(int active, int keyEdge, int menuOpen) {
  if (!active) return 0;
  if (!keyEdge) return 0;
  return menuOpen ? 0 : 1;
}

// Look direction in hitscan convention: vz from pitch alone (no horizoff), via ekm_sintable (shotray.h:57).
struct EkHitscanDir { int vx, vy, vz; };

inline EkHitscanDir ekm_tp_direction(int ang, int horiz) {
  EkHitscanDir d = { 0, 0, 0 };
  d.vx = ekm_sintable(ang + FREECAM_ANG_UNITS / 4);
  d.vy = ekm_sintable(ang);
  // Multiplies, not shifts: shifting a negative is undefined before C++20.
  d.vz = ((100 - horiz) * 32) * 64;
  return d;
}

inline void ekm_tp_step_back(int hitx, int hity, int vx, int vy, int stepBack,
                             int* outX, int* outY) {
  if (!outX || !outY) return;
  if (!stepBack) { *outX = hitx; *outY = hity; return; }
  *outX = clamp_ll_to_int((long long)hitx - (long long)vx * EK_TP_CLEARANCE / FREECAM_SIN_SCALE);
  *outY = clamp_ll_to_int((long long)hity - (long long)vy * EK_TP_CLEARANCE / FREECAM_SIN_SCALE);
}

// STAGE ONE: raw hitscan result to the point the engine resolves a sector for. A wall or sprite hit steps back; floor
// or ceiling does not.
struct EkTeleportAim { int ok; int x, y, z; const char* reason; };

inline EkTeleportAim ekm_tp_aim(int hitsect, int hitwall, int hitsprite,
                                int hitx, int hity, int hitz, int vx, int vy) {
  EkTeleportAim a = { 0, 0, 0, 0, EK_TP_NOTHING };
  if (hitsect < 0) return a;
  const int stepBack = (hitwall >= 0 || hitsprite >= 0) ? 1 : 0;
  int x = 0, y = 0;
  ekm_tp_step_back(hitx, hity, vx, vy, stepBack, &x, &y);
  a.ok = 1;
  a.x = x;
  a.y = y;
  a.z = hitz;
  a.reason = EK_TP_NONE;
  return a;
}

// STAGE TWO: may the player stand there? The eye lands eye-height above the floor, not at the ray's z. A tight space is
// a REFUSAL, not a clamp.
struct EkTeleportPlan { int ok; int x, y, z; int sect; const char* reason; };

inline EkTeleportPlan ekm_tp_plan(int x, int y, int z, int sectFromEngine,
                                  int florz, int ceilz, int eyeHeight) {
  EkTeleportPlan p = { 0, 0, 0, 0, -1, EK_TP_NO_SECTOR };
  (void)z;
  if (sectFromEngine < 0) return p;
  // Z grows downward so the gap is floor minus ceiling; widened (unchecked map memory).
  const long long gap = (long long)florz - (long long)ceilz;
  const long long need = (long long)eyeHeight + (long long)EK_TP_CEIL_MARGIN;
  if (gap < need) { p.reason = EK_TP_TIGHT; return p; }
  p.ok = 1;
  p.x = x;
  p.y = y;
  // The player's z is their EYE: floor minus eyeHeight (player.cpp:5414).
  p.z = clamp_ll_to_int((long long)florz - (long long)eyeHeight);
  p.sect = sectFromEngine;
  p.reason = EK_TP_NONE;
  return p;
}

inline int ekm_tp_has_notice(const EkTeleportPlan& p) {
  return (p.reason != 0 && p.reason[0] != '\0') ? 1 : 0;
}

// Unsigned subtraction: exact across the 49.7-day tick wrap.
inline int ekm_tp_notice_visible(int have, unsigned setMs, unsigned nowMs) {
  if (!have) return 0;
  return ((unsigned)(nowMs - setMs) < EK_TP_NOTICE_MS) ? 1 : 0;
}

// One turn around the square so consecutive corners are always a side (zig-zag draws a bowtie); widened and saturated.
struct EkMarkCorner { int x, y; };
struct EkMarkBox { EkMarkCorner c[4]; int zTop, zBottom; };

inline EkMarkBox ekm_tp_mark_box(int x, int y, int z) {
  const long long h = EK_TP_MARK_HALF;
  const int lx = clamp_ll_to_int((long long)x - h), hx = clamp_ll_to_int((long long)x + h);
  const int ly = clamp_ll_to_int((long long)y - h), hy = clamp_ll_to_int((long long)y + h);
  EkMarkBox b;
  b.c[0].x = lx; b.c[0].y = ly;
  b.c[1].x = hx; b.c[1].y = ly;
  b.c[2].x = hx; b.c[2].y = hy;
  b.c[3].x = lx; b.c[3].y = hy;
  b.zBottom = z;
  b.zTop    = clamp_ll_to_int((long long)z - (long long)EK_TP_MARK_HEIGHT);
  return b;
}
