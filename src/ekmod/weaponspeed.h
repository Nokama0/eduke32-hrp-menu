#pragma once
#include <math.h>
#include "aimsolve.h"

// Weapon speed as pure rules over two per-tic counters: kickback_pic (firing frame, player.cpp:4588) and weapon_pos
// (raise/lower, :4212). Advancing a counter past dead steps, never past the next event, shortens the cadence without
// skipping a shot, sound or reset; both are engine-written fields. Rules from WT/Megaton; the table is not.

const int FIRERATE_MIN = 100, FIRERATE_MAX = 400, FIRERATE_STEP = 50;
// 100 is the identity: nothing advanced, no byte written.
const int FIRERATE_DEFAULT = FIRERATE_MIN;

const int FIRERATE_EVENTS_MAX = 5;
struct FireRateWeapon { int weapon; int nEvents; int events[FIRERATE_EVENTS_MAX]; };

// Table read from EDuke32's data-driven P_ProcessWeapon (player.cpp:4587-4741, weapondata_t gamevars.cpp:1487):
// kickback_pic values whose NEXT tic does more than count. No entry for pipebomb (:4477), remote (:4560) or tripbomb
// (counter gates movement, :3035). Function-local: one copy.
inline const FireRateWeapon* firerate_weapon(int weapon) {
  static const FireRateWeapon FIRERATE_TABLE[10] = {
    // KNEE 7/14 (gamevars.cpp:1496).
    { 0,  2, { 6, 13 } },
    // PISTOL 2/5, Reload 27 (:1503): reload ladder EJECT_CLIP 6, INSERT_CLIP 9.
    { 1,  5, { 1, 4, 5, 8, 26 } },
    // SHOTGUN 4/30, Reload 13 (:1510): CHECKATRELOAD from 13, cock 15, shell 24.
    { 2,  5, { 3, 12, 14, 23, 29 } },
    // CHAINGUN 3/12 (:1517): fires 3, 6, 9; WEAPON_RESET from 10 (:4717).
    { 3,  5, { 2, 5, 8, 9, 11 } },
    { 4,  2, { 3, 19 } },
    { 6,  2, { 9, 11 } },
    // DEVASTATOR 3/6 (:1545); second barrel is the t_data[7] burst (:4292).
    { 7,  2, { 2, 5 } },
    { 9,  2, { 2, 4 } },
    { 11, 2, { 2, 4 } },
    // FLAMETHROWER 2/16 (:1580); matches WT's derived values.
    { 12, 2, { 1, 15 } },
  };
  for (int i = 0; i < 10; i++)
    if (FIRERATE_TABLE[i].weapon == weapon) return &FIRERATE_TABLE[i];
  return 0;
}

inline int firerate_rate_clamped(int v) {
  if (v < FIRERATE_MIN) v = FIRERATE_MIN;
  if (v > FIRERATE_MAX) v = FIRERATE_MAX;
  return ((v - FIRERATE_MIN + FIRERATE_STEP / 2) / FIRERATE_STEP) * FIRERATE_STEP + FIRERATE_MIN;
}

inline int firerate_next_event(const FireRateWeapon& t, int k) {
  for (int i = 0; i < t.nEvents; i++) if (t.events[i] >= k) return t.events[i];
  return -1;
}

// `k <= 0` is rest: the player's own press sets the counter to 1 (:4588); advancing from rest would fire.
inline int firerate_advance(const FireRateWeapon& t, int k, int steps) {
  if (k <= 0 || steps <= 0) return k;
  const int nxt = firerate_next_event(t, k);
  if (nxt < 0) return k;
  const int target = k + steps;
  return target > nxt ? nxt : target;
}

// weapon_pos: change sets -1 down to -8; at -9 the engine alone flips to 10, then 10..1 to rest 0. Never move rest, the
// flip, or past -8 or 1 (sector.cpp:2875 tests == 1).
inline int wswitch_advance(int pos, int steps) {
  if (steps <= 0 || pos == 0 || pos == -9) return pos;
  if (pos < 0) {
    const int target = pos - steps;
    return target < -8 ? -8 : target;
  }
  const int target = pos - steps;
  return target < 1 ? 1 : target;
}

// The carry persists across tics (resetting it would speed nothing below 200); the caller resets it on every refusal.
inline int firerate_steps(int ratePct, double* carry) {
  const double extra = (firerate_rate_clamped(ratePct) - 100) / 100.0;
  if (!carry) return (int)floor(extra);
  *carry += extra;
  const int n = (int)floor(*carry);
  *carry -= n;
  if (*carry < 0.0) *carry = 0.0;
  return n;
}

// One carry for all weapons: reset on a change.
inline double firerate_carry_for_weapon(double carry, int lastWeapon, int weapon) {
  return weapon != lastWeapon ? 0.0 : carry;
}

// No tic hook: the tic comes from the wall clock; `acc` is ms owed across frames (per-frame advance is frame-rate
// dependent). Capped: a stall is not a burst.
const int FIRERATE_TICS_MAX = 4;
inline int firerate_tics_due(double* acc, int elapsedMs) {
  if (!acc) return 0;
  if (elapsedMs > 0) *acc += (double)elapsedMs;
  int n = 0;
  while (*acc >= EKM_MS_PER_TIC && n < FIRERATE_TICS_MAX) { *acc -= EKM_MS_PER_TIC; n++; }
  if (n >= FIRERATE_TICS_MAX) *acc = 0.0;
  if (*acc < 0.0) *acc = 0.0;
  return n;
}

inline bool firerate_gate(bool ready, bool menuOpen, bool paused, bool panelOpen,
                          int ratePct, int kickback, bool hasTable) {
  return ready && !menuOpen && !paused && !panelOpen
      && firerate_rate_clamped(ratePct) > 100 && kickback > 0 && hasTable;
}

inline const char* firerate_gate_reason(bool ready, bool menuOpen, bool paused, bool panelOpen,
                                        int ratePct, int kickback, bool hasTable) {
  if (!ready)      return "unready";
  if (menuOpen)    return "menu";
  if (paused)      return "paused";
  if (panelOpen)   return "panel";
  if (firerate_rate_clamped(ratePct) <= 100) return "off";
  if (kickback <= 0) return "rest";
  if (!hasTable)   return "notable";
  return "on";
}

inline bool wswitch_gate(bool ready, bool menuOpen, bool paused, bool panelOpen,
                         int ratePct, int pos) {
  return ready && !menuOpen && !paused && !panelOpen
      && firerate_rate_clamped(ratePct) > 100 && pos != 0 && pos != -9;
}

// Engine half: called from the SwapBuffers hook on the game thread after the tic ran.
void weaponspeed_frame(void);
