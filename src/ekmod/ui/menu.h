#pragma once
#include "ui.h"
#include "common.h"   // clamp_int, and the shared engine helpers below it
#include "input.h"    // MOUSE_BUTTON_MIN/MAX, still the poll hook's own range
#include "tbot.h"     // TBOT_WHEN_CROSSHAIR/AIMBOT, tbot_when_clamped
#include "panel_layout.h"  // PanelBox, for menu_panel_box below

enum { TAB_VISUALS = 0, TAB_COMBAT = 1, TAB_WEAPONS = 2, TAB_VIEW = 3, TAB_SETTINGS = 4, TAB_COUNT = 5 };

UiRect menu_panel_rect(int screenW, int screenH);
PanelBox menu_panel_box(int logicalW, int logicalH);
int    menu_active_tab();
void   menu_set_tab(int tab);
void   menu_draw(int screenW, int screenH);
int    menu_applied_scale(int screenH);

// Cheat categories live, for the hazard strip and "N cheats live": PARENT switches only (never a sub-setting inert
// until its parent is on, or a fresh install reports cheats live); accuracy switches count individually; mouseaimon and
// the firerate/switchspeed ranges are preferences.
inline int menu_live_count(int fovon, int espenemies, int espitems, int espsecrets,
                           int espexplodables, int espswitches,
                           int aimon, int silenton, int freecamon,
                           int accpistol, int accchaingun, int accshotgun, int accexpander,
                           int dmgnumbers, int secrettriggers, int tboton,
                           int espbreakables = 0) {
  return (fovon       ? 1 : 0) + (espenemies  ? 1 : 0)
       + (espitems    ? 1 : 0) + (espsecrets  ? 1 : 0)
       + (espexplodables ? 1 : 0) + (espswitches ? 1 : 0)
       + (aimon       ? 1 : 0) + (silenton    ? 1 : 0)
       + (freecamon   ? 1 : 0)
       + (accpistol   ? 1 : 0) + (accchaingun ? 1 : 0)
       + (accshotgun  ? 1 : 0) + (accexpander ? 1 : 0)
       + (dmgnumbers  ? 1 : 0) + (secrettriggers ? 1 : 0)
       + (tboton      ? 1 : 0) + (espbreakables  ? 1 : 0);
}

// Menu log lines, named here because input.cpp cannot link into test_host.
const char* const MENU_LOG_OPEN  = "menu: open";
const char* const MENU_LOG_CLOSE = "menu: close";

const char* const MENU_KEYCAP_PROMPT = "Press a key";
const char* const MENU_GAMECFG_NOTE  = "Also bound in the game";
const char* const MENU_KEYCAP_HOVER  = "Click to set";

const char* const MENU_RESET_HINT = "Reset to default";

// STALE STUB from before gamecfg.h, called from nowhere; gamecfg_load/gamecfg_function_for do the work. Kept because
// menu.cpp defines it.
void menu_gamecfg_load(const char* path);

bool menu_keycap_armed();

void menu_keycap_bind(int vk);

void menu_keycap_cancel();

// Disarms because the panel is closing: a capture left armed would eat the next in-game key.
void menu_keycap_close();

// One numeric entry field for the whole panel; rules are in entry.h.

bool menu_entry_open();

// Scancodes: 41 Escape cancels, 40/88 commit, 42 deletes, others typed if entry_char_of_scancode names them.
void menu_entry_key(int sc);

// What input.cpp's poll hook calls; adds caret/selection keys and Ctrl+A/C/X. Returns the text for the clipboard on
// Ctrl+C/X (input.cpp owns the syscall and the Ctrl+V read).
const char* menu_entry_key_mod(int sc, bool shift, bool ctrl);

void menu_entry_paste(const char* s);

void menu_entry_begin_edit(int rowKey, int kind, int lo, int hi, const char* text);

// Closes the field because the panel is closing, or it would keep eating keys with nothing on screen.
void menu_entry_close();

const char* menu_entry_text();

bool menu_entry_pending(int* rowKey, int* value);

// Test hook: overrides the wall clock menu_draw reads; 0 is none.
void menu_set_now_ms(unsigned ms);

// True while the panel closes under animation, so overlay.cpp fades it out; false under ANIM_OFF.
bool menu_closing_animation_active();

const char* menu_tooltip_text();

const int RAIL_W = 116;
const int RAIL_LABEL_X = 12;

const int STRIP_H = 15, STATUS_H = 30, ROW_H = 30, ROW_GAP = 4, PAD = 14;

const int VALUE_MARGIN = 10;

// Whether a Combat sub-row is enabled: its parent switch is nonzero.
inline bool combat_sub_enabled(int parentValue) { return parentValue != 0; }

// TARGETING serves both aim modes, so either parent suffices.
inline bool combat_shared_enabled(int aimon, int silenton) {
  return aimon != 0 || silenton != 0;
}

// "Pick target" shows a NAME: any nonzero reads as "closest".
inline const char* aim_target_label(int aimtarget) {
  return aimtarget ? "Closest" : "Crosshair";
}

inline const char* aim_point_label(int aimpoint) {
  return aimpoint ? "Upper" : "Centre";
}

inline const char* tbot_when_label(int when) {
  return tbot_when_clamped(when) == TBOT_WHEN_AIMBOT ? "Aimbot locked" : "Crosshair on enemy";
}
