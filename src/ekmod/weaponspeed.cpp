// Weapon speed, the engine half (decisions: weaponspeed.h). No tic hook exists, so tics come from the wall clock (a
// per-frame advance would vary with frame rate). Runs in the SwapBuffers hook; the write is safe because advances never
// move a counter at rest or past the next value where P_ProcessWeapon does something.

#include <windows.h>
#include "weaponspeed.h"
#include "config.h"
#include "playerstate.h"
#include "udstate.h"
#include "input.h"
#include "log.h"
#include "wallclock.h"

static bool s_haveLast = false;
static unsigned s_lastMs = 0;

// Milliseconds owed toward the next tic; reset on refusal so a pause is not banked.
static double s_ticAcc = 0.0;

// Carries must persist across tics: at 150 the extra is half a step, which a per-tic reset would floor to zero.
static double s_kickCarry = 0.0;
static double s_posCarry  = 0.0;
static int s_lastWeapon = -1;

// Reset on every refusal path: a leftover fraction is not the next sequence's to spend.
static void weaponspeed_reset(void) {
  s_ticAcc = 0.0;
  s_kickCarry = 0.0;
  s_posCarry = 0.0;
}

static void weaponspeed_log_once(const char* what, int rate, int from, int to) {
  if (!g_cfg.debuglog) return;
  ek_log("weaponspeed: first %s write -- %d%%, %d to %d", what, rate, from, to);
}

void weaponspeed_frame(void) {
  const unsigned now = wallclock_ms();
  const int elapsedMs = s_haveLast ? (int)(now - s_lastMs) : 0;
  s_haveLast = true;
  s_lastMs = now;

  const int fireRate   = firerate_rate_clamped(g_cfg.firerate);
  const int switchRate = firerate_rate_clamped(g_cfg.switchspeed);

  // Cheap refusal first: both rows at 100 reads and writes nothing.
  if (fireRate <= 100 && switchRate <= 100) { weaponspeed_reset(); return; }

  // A failed read is a refusal: a feature that cannot tell whether the menu is open assumes it is.
  int gm = 0;
  const bool gmOk = player_read_gm(&gm);
  const bool menuOpen = !gmOk || (gm & EK_MODE_MENU) != 0;

  // The Pause key leaves gm at MODE_GAME, so the clock would bank tics through a pause; unreadable &ud reads as paused.
  bool pauseOn = true;
  const bool paused = !ud_paused(&pauseOn) || pauseOn;

  // Our own panel too: a player dragging these sliders is not playing.
  const bool panelOpen = input_panel_open();

  int weapon = 0, kickback = 0, weaponPos = 0;
  const bool readOk = player_read_weapon_anim(&weapon, &kickback, &weaponPos);
  if (!readOk) { weaponspeed_reset(); return; }

  const FireRateWeapon* const table = firerate_weapon(weapon);

  const bool fireOk = firerate_gate(readOk, menuOpen, paused, panelOpen,
                                    fireRate, kickback, table != 0);
  const bool switchOk = wswitch_gate(readOk, menuOpen, paused, panelOpen,
                                     switchRate, weaponPos);
  if (!fireOk && !switchOk) {
    weaponspeed_reset();
    if (g_cfg.debuglog) {
      static unsigned lastMs = 0;
      static bool said = false;
      if (!said || now - lastMs >= 1000) {
        said = true; lastMs = now;
        ek_log("weaponspeed: idle -- fire %s, weapon %d, kickback %d, pos %d",
               firerate_gate_reason(readOk, menuOpen, paused, panelOpen,
                                    fireRate, kickback, table != 0),
               weapon, kickback, weaponPos);
      }
    }
    return;
  }

  s_kickCarry = firerate_carry_for_weapon(s_kickCarry, s_lastWeapon, weapon);
  s_posCarry  = firerate_carry_for_weapon(s_posCarry,  s_lastWeapon, weapon);
  s_lastWeapon = weapon;

  const int tics = firerate_tics_due(&s_ticAcc, elapsedMs);
  if (tics <= 0) return;

  // One write per counter: tic-by-tic and summed advances clamp at the same event, and one write is one chance to
  // fault.
  if (fireOk) {
    int steps = 0;
    for (int i = 0; i < tics; i++) steps += firerate_steps(fireRate, &s_kickCarry);
    const int next = firerate_advance(*table, kickback, steps);
    if (next != kickback && player_write_kickback(next)) {
      static bool said = false;
      if (!said) { said = true; weaponspeed_log_once("fire rate", fireRate, kickback, next); }
    }
  } else {
    s_kickCarry = 0.0;
  }

  if (switchOk) {
    int steps = 0;
    for (int i = 0; i < tics; i++) steps += firerate_steps(switchRate, &s_posCarry);
    const int next = wswitch_advance(weaponPos, steps);
    if (next != weaponPos && player_write_weapon_pos(next)) {
      static bool said = false;
      if (!said) { said = true; weaponspeed_log_once("switch speed", switchRate, weaponPos, next); }
    }
  } else {
    s_posCarry = 0.0;
  }
}
