#pragma once

// Screenshot mode: weapon, crosshair and level stats off while the free camera flies, so a comparison shot has only the
// world (the HUD weapon is placed against a view no longer the player's). Ported from WT's rules, not its mechanism:
// EDuke32 registers the three as plain OSD cvars (osdcmds.cpp:1596, :1665, :1713), so no hook.

// The menu term is Duke's own menu, not this panel's: opening our panel mid-flight must not restore the hud, so the ESP
// is hidden then too (shot_hides_overlays). Esc cannot reach Duke's menu mid-flight, so the ekmod.ini stash replayed at
// install is the whole protection against an unclean exit.
inline bool shot_active(bool cameraActive, bool shotSwitch, bool menuOpen) {
  return cameraActive && shotSwitch && !menuOpen;
}

// The one signal for shot_active's menu argument: Duke's menu bit (EK_MODE_MENU), never camera.cpp's broader menuOpen
// that also ORs the panel.
inline bool shot_menu_term(bool gmMenuOpen) { return gmMenuOpen; }

enum ShotEdge { SHOT_EDGE_NONE = 0, SHOT_EDGE_ON = 1, SHOT_EDGE_OFF = 2 };

inline int shot_edge(bool wasActive, bool nowActive) {
  if (wasActive == nowActive) return SHOT_EDGE_NONE;
  return nowActive ? SHOT_EDGE_ON : SHOT_EDGE_OFF;
}

// Appended, never inserted or reordered: the indices name both the config fields and the on-disk mask bits, validated
// only against THIS build. Pinned by tb_0115. A fourth cvar is appended here and in config.cpp's table.
const int SHOT_CVAR_CROSSHAIR = 0;
const int SHOT_CVAR_WEAPON    = 1;
const int SHOT_CVAR_STATS     = 2;
const int SHOT_CVARS          = 3;

// `mask`: cvars the registry could read on the ON edge; only those are hidden and restored. `held` is int, not bool
// (MSVC 19.50 /O2 bool loss).
struct ShotHold {
  int held;
  int mask;
  int saved[SHOT_CVARS];
  // Taken once a session, never retaken (shot_hold_step).
  int baselined;
};

enum ShotAction { SHOT_ACT_NONE = 0, SHOT_ACT_HIDE = 1, SHOT_ACT_RESTORE = 2 };

// Player values are read ONCE A SESSION and written back on every OFF edge. A capture per ON adopted our own zeros: a
// quicksave while flying persists them (savegame.cpp:957) and r_drawweapon, being console-only, stayed 0. HIDE writes 0
// (each cvar's vmin); RESTORE gives back outValue for *outMask.
inline int shot_hold_step(ShotHold* h, int edge, int readMask, const int* current,
                          int* outMask, int* outValue) {
  if (!h || !current || !outMask || !outValue) return SHOT_ACT_NONE;
  *outMask = 0;
  for (int i = 0; i < SHOT_CVARS; i++) outValue[i] = 0;

  if (edge == SHOT_EDGE_ON && !h->held) {
    h->held = 1;
    if (!h->baselined) {
      h->baselined = 1;
      h->mask = readMask;
      for (int i = 0; i < SHOT_CVARS; i++)
        h->saved[i] = (readMask & (1 << i)) ? current[i] : 0;
    }
    *outMask = h->mask;
    return SHOT_ACT_HIDE;
  }
  if (edge == SHOT_EDGE_OFF && h->held) {
    h->held = 0;
    *outMask = h->mask;
    for (int i = 0; i < SHOT_CVARS; i++) outValue[i] = h->saved[i];
    // Baseline survives the OFF edge: clearing the mask let the next ON re-read live values.
    return SHOT_ACT_RESTORE;
  }
  return SHOT_ACT_NONE;
}

// Crash stash replayed at install. Trustworthy = every checked bit still reads 0, what the ON edge forced. Not
// crash-only (a player who zeroed all three is overwritten) but narrow and self-limiting. Any drift refuses the WHOLE
// stash.
inline bool shot_stash_valid(int mask, const int* current) {
  if (mask == 0) return false;
  for (int i = 0; i < SHOT_CVARS; i++)
    if ((mask & (1 << i)) && current[i] != 0) return false;
  return true;
}

// A stash on disk is not proof of failure: shot_restore heals at DLL_PROCESS_DETACH without writing a file. Every bit
// at its SAVED value means nothing to do. A tie goes to the crash replay (asked first; same value).
inline bool shot_stash_healed(int mask, const int* saved, const int* current) {
  if (mask == 0 || !saved || !current) return false;
  for (int i = 0; i < SHOT_CVARS; i++)
    if ((mask & (1 << i)) && current[i] != saved[i]) return false;
  return true;
}

// Only the intersection of the readable set and the stash mask is checked; if not all 0, NONE. The stash is cleared on
// disk either way.
inline int shot_stash_replay(int held, int stashMask, const int* saved,
                              int readMask, const int* current,
                              int* outMask, int* outValue) {
  *outMask = 0;
  for (int i = 0; i < SHOT_CVARS; i++) outValue[i] = 0;
  if (!held) return SHOT_ACT_NONE;
  const int checkMask = stashMask & readMask;
  if (!shot_stash_valid(checkMask, current)) return SHOT_ACT_NONE;
  *outMask = checkMask;
  for (int i = 0; i < SHOT_CVARS; i++)
    if (checkMask & (1 << i)) outValue[i] = saved[i];
  return SHOT_ACT_RESTORE;
}

// No cvar hides the status bar (none for ud.screen_size; hud_statusbarmode's handler calls G_UpdateScreenArea, which
// cvar_set_int does not). G_DrawStatusBar has exactly one `call rel32`, redirected by hook.cpp's callsite_install.

// G_DrawStatusBar has no unique string; the combination is: sbar.cpp draws the legend with minitext "On", "Off" and
// "Auto" and no other function loads all three.
inline bool shot_sbar_shape(bool on, bool off, bool autoLit) {
  return on && off && autoLit;
}

// Exactly one target may carry it; a faulted scan cannot claim uniqueness.
inline bool shot_sbar_target_ok(int shaped, int faulted) {
  return shaped == 1 && faulted == 0;
}

// Exactly one call site: screens.cpp calls it twice (:1154, :1160 under SPLITSCREEN_MOD_HACKS); hiding one of two is
// not the feature.
inline bool shot_sbar_locate_ok(int sites, int faulted) {
  return sites == 1 && faulted == 0;
}

inline bool shot_draws_statusbar(bool active) { return !active; }

// P_DisplayScuba (player.cpp:2156), the last HUD r_drawweapon does not gate, has no call in this image: it is inlined
// into G_DisplayRest, so its three G_DrawTileScaled calls (:2178-2181) are named by argument values.

// player.cpp:1863, :2178-2181. The right half's `mov ecx, 320-43` occurs once in eduke32.exe.
const int SHOT_DRAWEAP_CENTER  = 262144;
const int SHOT_SCUBA_X_SEAM    = 44;
const int SHOT_SCUBA_X_LEFT    = 43;
const int SHOT_SCUBA_X_RIGHT   = 320 - 43;
const int SHOT_SCUBA_BITS      = 2 + 16 + SHOT_DRAWEAP_CENTER;
const int SHOT_SCUBA_BITS_FLIP = 2 + 4 + 16 + SHOT_DRAWEAP_CENTER;
const int SHOT_SCUBA_SITES     = 3;

// events_defs.h:120, frozen by the engine's EDuke 2.0 compatibility rule; only corroborates the bracket.
const int SHOT_EVENT_DISPLAYSCUBA = 112;

inline bool shot_scuba_anchor_ok(int count, int faulted) {
  return count == 1 && faulted == 0;
}

// The three sites must reach one callee nothing else in the bracket calls; a fourth site would be redirected blind.
inline bool shot_scuba_locate_ok(int anchored, int targets, int sites, int faulted) {
  return anchored == SHOT_SCUBA_SITES && targets == 1 &&
         sites == SHOT_SCUBA_SITES && faulted == 0;
}

// Exact pairs: left and seam carry 2+16+DRAWEAP_CENTER, the right adds the flip bit; unshaded. A belt: a wrong callee
// forwards instead of dropping another tile.
inline bool shot_scuba_tile(int drawX, int drawShade, int drawBits) {
  if (drawShade != 0) return false;
  if (drawX == SHOT_SCUBA_X_LEFT || drawX == SHOT_SCUBA_X_SEAM)
    return drawBits == SHOT_SCUBA_BITS;
  if (drawX == SHOT_SCUBA_X_RIGHT)
    return drawBits == SHOT_SCUBA_BITS_FLIP;
  return false;
}

inline bool shot_draws_scuba(bool active, int drawX, int drawShade, int drawBits) {
  return !(active && shot_scuba_tile(drawX, drawShade, drawBits));
}

// All three or none: count rises after the third install, drops before the first remove.
inline bool shot_scuba_installed(int patched) {
  return patched == SHOT_SCUBA_SITES;
}

inline bool shot_hides_overlays(bool active) { return active; }

void shot_frame(bool cameraActive, bool menuOpen);

bool shot_active_now(void);

void shot_restore(void);

// The status bar's site and the scuba mask's three, installed once from overlay.cpp's per-frame path (hook.cpp's write
// window) and removed at detach BEFORE shot_restore; the locates are independent.
void shot_install(void);
void shot_remove(void);

bool shot_statusbar_available(void);

bool shot_scuba_available(void);

// Once at install, after cvars_scan, before anything can zero these cvars again; always clears the on-disk stash.
void shot_replay_stash(void);

bool shot_available(void);
