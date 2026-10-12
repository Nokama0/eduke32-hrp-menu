// The Nuke Panel, ported from World Tour's menu.cpp: same chrome, five-tab rail and row widgets, minus rows whose
// feature this port lacks. Remaining cut: ANIM_GLASS (config.h has a 0/1 `animations`, canvas.cpp's backdrop capture is
// a stub), so every chrome_box is its canvas_bevel branch.
#include <windows.h>
#include "menu.h"
#include "panel_layout.h"
#include "canvas.h"
#include "font.h"
// Numeric entry rules (entry.h) are pure; this file keeps the one instance of state. Its "project.h" resolves to the
// sibling shim (ui/project.h), not the Polymer projection header.
#include "entry.h"
#include "anim.h"
#include "wallclock.h"
#include "tooltip.h"
#include "bind.h"
#include "input.h"
#include "../config.h"
#include "../log.h"
#include "../aim.h"
#include "../gamehooks.h"
#include "../aimsolve.h"
#include "../esp.h"
#include "../input.h"
#include "../mousebind.h"
#include "../weaponspeed.h"
#include "../cvars.h"
#include "../autoswitch.h"
#include "../autosave.h"
#include "../camera.h"
#include "../shot.h"
#include "../freecam.h"
#include "../fov.h"
#include "../gamecfg.h"
#include <stdio.h>
#include <string.h>

static int s_tab = TAB_VISUALS;

// Which row is waiting for a key, if any: ONE capture for the whole panel, or the next press would be ambiguous.
// Transitions are keycap_step's (bind.h); this is the store.
static void menu_note_new_session();

static KeyCapState s_keycap = { KEYCAP_IDLE, -1 };

bool menu_keycap_armed() { return s_keycap.phase == KEYCAP_ARMED; }

void menu_keycap_bind(int vk) {
  KeyCapEvent e;
  e.kind = bind_is_mouse(vk) ? KEYCAP_EV_MOUSE : KEYCAP_EV_KEY;
  e.row = -1;
  e.vk = vk;
  const KeyCapResult r = keycap_step(s_keycap, e);
  s_keycap = r.state;
  if (r.boundRow >= 0) config_bind_set(r.boundRow, r.boundVk);
}

void menu_keycap_cancel() {
  // Escape cancels; the panel closing disarms through menu_keycap_close() instead.
  KeyCapEvent e;
  e.kind = KEYCAP_EV_ESCAPE;
  e.row = -1;
  e.vk = 0;
  s_keycap = keycap_step(s_keycap, e).state;
}

void menu_keycap_close() {
  KeyCapEvent e;
  e.kind = KEYCAP_EV_CLOSE;
  e.row = -1;
  e.vk = 0;
  s_keycap = keycap_step(s_keycap, e).state;
  menu_note_new_session();
}

// Stub: menu.h declares it but the real parser is gamecfg.h/gamecfg.cpp (row_bind calls it directly). Kept as a no-op
// rather than removing a declared symbol.
void menu_gamecfg_load(const char*) {}

// Scancode names: entry.h's header assumes input.h names SDL_SCANCODE_A/C (true in World Tour, not here), and the
// do-not-edit header cannot change. Values from SDL's table, matching ui/common.h's case 4/6.
const int SDL_SCANCODE_A = 4;
const int SDL_SCANCODE_C = 6;

static EntryState s_entry = entry_begin();

// Left went down inside the field's well and is still down: a press puts the caret, held frames drag the selection.
// Cleared on release and at begin_edit so a drag never carries into the next field.
static bool s_entryCaretDrag = false;

// The field's drawn box last frame, for the click-away pass to tell a press inside from outside; an empty rect hits
// nothing.
static UiRect s_entryFieldRect = { 0, 0, 0, 0 };

static char s_entryClip[ENTRY_TEXT_CAP + 1] = { 0 };

bool menu_entry_open() { return s_entry.open != 0; }

const char* menu_entry_text() { return s_entry.text; }

void menu_entry_key(int sc) { menu_entry_key_mod(sc, false, false); }

void menu_entry_paste(const char* s) {
  if (!s_entry.open || !s) return;
  s_entry = entry_step(s_entry, ENTRY_EV_PASTE, 0, 0, EK_NONE, 0, 0, s).state;
}

const int GROUP_H = 22, GROUP_GAP = 6;
const int SUB_RULE_X = 3, SUB_INDENT = 21;

// Drag state for panel_layout.h: `s_drag` the machine, `s_dragBox` the last live box (no mouse to re-derive from once
// the button is up), `s_dragLive` so no row acts under a hand moving the panel.
static PanelDrag s_drag = { PDRAG_IDLE, 0, 0, { 0, 0, 0, 0 } };
static PanelBox  s_dragBox = { 0, 0, 0, 0 };
static bool      s_dragLive = false;

// A press already spent: the reset glyph overlaps the track and an open dropdown covers rows, so one press could do two
// things. A latch (the button stays down), cleared in menu_draw's preamble once up.
static bool      s_pressSpent = false;

// The press a control has claimed; an unclaimed left press in the panel is a move. Not s_pressSpent: that spans the
// whole hold, so a slider would disarm itself.
static bool      s_pressClaimed = false;

static PanelBox panel_stored_box() {
  PanelBox b = { g_cfg.panelx, g_cfg.panely, g_cfg.panelw, g_cfg.panelh };
  return b;
}

PanelBox menu_panel_box(int logicalW, int logicalH) {
  const PanelBox laid = panel_layout(panel_stored_box(), logicalW, logicalH);
  return s_dragLive ? s_dragBox : laid;
}

UiRect menu_panel_rect(int screenW, int screenH) {
  const PanelBox b = menu_panel_box(screenW, screenH);
  return UiRect{ b.x, b.y, b.w, b.h };
}

// The scale the panel settles at on this screen height: the stored setting, capped so the logical height fits, floored
// at PANEL_MIN_H (panel_layout raises anything below it).
static int menu_ceiling_height() {
  const int h = g_cfg.panelh > 0 ? g_cfg.panelh : PANEL_H;
  return h < PANEL_MIN_H ? PANEL_MIN_H : h;
}

int menu_applied_scale(int screenH) {
  const int stored = ui_scale_clamped(g_cfg.uiscale);
  const int ceiling = ui_scale_max_for(screenH, menu_ceiling_height(), PANEL_VINSET);
  return stored < ceiling ? stored : ceiling;
}

static int s_scaleHeld = UI_SCALE_DEFAULT;

int  menu_active_tab() { return s_tab; }
void menu_set_tab(int tab) { if (tab >= 0 && tab < TAB_COUNT) s_tab = tab; }

static const char* TAB_NAMES[TAB_COUNT] = { "Visuals", "Combat", "Weapons", "View", "Settings" };
static const unsigned TAB_COLOURS[TAB_COUNT] = { C_BLUE, C_RED, C_HAZARD, C_GREEN, C_SETTINGS };

static int anim_style() { return g_cfg.animations ? ANIM_SIMPLE : ANIM_OFF; }

// A cosmetic layer beside the logical layout. Hit testing never reads it (ui_hit compares the logical rect), so a click
// mid-transition lands where the control is going. `knob`/`fill`/`hover` are indexed by row ordinal, which is stable
// within one tab only, hence every row's spring snaps on a tab switch.
struct MenuPresentation {
  unsigned lastMs;
  AnimSpring open;
  int openTarget;
  int wasOpen;
  unsigned tabSwitchMs;
  int tabFrom;
  AnimSpring knob[64];
  AnimSpring fill[64];
  double hover[64];
  double resetHover[64];
  int rowsThisFrame;
};
static MenuPresentation s_pres;

static const int ANIM_ROW_SLOTS = 64;
static bool s_tabSwitchedThisFrame = false;
static double s_frameDt = 0.0;

// Hints as tooltips (rule: tooltip.h). `s_lastRow`/`s_lastRowKey` are the row just drawn, set by each row helper, so
// row_hint attaches by adjacency. The key is `s_tab * 1000 + ordinal` since ordinals repeat across tabs; -1 means no
// row.
static UiRect s_lastRow = { 0, 0, 0, 0 };
static int s_lastRowKey = -1;
static int s_hoverRowKey = -1;
// A row that ran out of cursor height draws nothing, invisibly (no scrollbar), so it is counted and reported once a
// session at the foot of menu_draw.
static int s_rowsDropped = 0;
static bool s_rowsDroppedLogged = false;
static char s_hintBuf[96];
static const char* s_hoverHint = nullptr;
static const char* s_tooltipText = nullptr;
static TooltipState s_tooltip = tooltip_begin();
static const int TOOLTIP_R = 4;
static const char* s_glyphHint = nullptr;

// What a session must not inherit: an armed drag, a spent press, a tooltip on the row the cursor closed over. input.cpp
// calls menu_keycap_close() on close; menu_draw also takes the edge from the open state, which keeps this right under
// ANIM_OFF.
static void menu_note_new_session() {
  s_drag.phase = PDRAG_IDLE;
  s_dragLive = false;
  s_pressSpent = false;
  s_tooltip = tooltip_begin();
  s_tooltipText = nullptr;
  s_hoverHint = nullptr;
  s_glyphHint = nullptr;
  s_hoverRowKey = -1;
  s_lastRowKey = -1;
}

const char* menu_tooltip_text() { return s_tooltipText; }

static void row_hint(const char* text) {
  if (s_lastRowKey < 0 || !text) return;
  if (!ui_hit(s_lastRow, g_ui.mx, g_ui.my)) return;
  s_hoverRowKey = s_lastRowKey;
  strncpy_s(s_hintBuf, sizeof(s_hintBuf), text, _TRUNCATE);
  s_hoverHint = s_hintBuf;
}

static unsigned s_nowOverride = 0;
void menu_set_now_ms(unsigned ms) { s_nowOverride = ms; }
static unsigned menu_now() { return s_nowOverride ? s_nowOverride : wallclock_ms(); }

bool menu_closing_animation_active() {
  if (anim_style() == ANIM_OFF || input_panel_open()) return false;
  // A spring nothing has stepped for a second is at rest; this reader runs on frames menu_draw may not, so it settles
  // the spring rather than just reporting. Unsigned, wrap-safe.
  if (menu_now() - s_pres.lastMs > ANIM_STALE_MS) { s_pres.open.x = 0.0; s_pres.open.v = 0.0; }
  return !anim_spring_settled(s_pres.open, 0.0, 0.01);
}

static unsigned pres_colour(unsigned c) {
  if (anim_style() == ANIM_OFF) return c;
  const int srcA = (int)((c >> 24) & 0xFFu);
  const int a = (int)(srcA * s_pres.open.x + 0.5);
  return canvas_with_alpha(c, a);
}

static unsigned alpha_scale(unsigned c, double a) {
  if (a < 0.0) a = 0.0;
  if (a > 1.0) a = 1.0;
  const int srcA = (int)((c >> 24) & 0xFFu);
  return canvas_with_alpha(c, (int)(srcA * a + 0.5));
}

static unsigned pres_content_colour(unsigned c) {
  const unsigned withOpen = pres_colour(c);
  if (anim_style() == ANIM_OFF) return withOpen;
  const double tabP = anim_ease_out(anim_progress(s_pres.tabSwitchMs, s_pres.lastMs, ANIM_TAB_MS));
  const int srcA = (int)((withOpen >> 24) & 0xFFu);
  const int a = (int)(srcA * tabP + 0.5);
  return canvas_with_alpha(withOpen, a);
}

static unsigned pres_hover_lighten(unsigned c, double hoverFrac) {
  if (anim_style() == ANIM_OFF) return c;
  return anim_blend_colour(c, C_BONE, hoverFrac);
}

static unsigned pres_hover_toward(unsigned c, unsigned target, double hoverFrac) {
  if (anim_style() == ANIM_OFF) return c;
  return anim_blend_colour(c, target, hoverFrac);
}

static const int ROW_R = 4;
static const int SIMPLE_SLIDE_PX = 8;

// No row acts while the panel is dragged: panel_layout clamps at the edge while the hand keeps going, so a slider would
// read the held button as a drag. A filter on the button fields, as rows must still draw and hover.
static bool row_pressed() { return !s_dragLive && !s_pressSpent && g_ui.pressed; }
static bool row_down()    { return !s_dragLive && !s_pressSpent && g_ui.down; }

// Where a value lands from outside its own row: one slot, not a queue, for the reset glyph, dropdown and typed field.
// Written from BOTH threads (render: glyph and dropdown; window: Enter via input.cpp's entry_route_key), so it is ONE
// LONG64: rowKey high, value low, -1 empty (row_commit_pack, common.h). A claim CASes against the word it read.
static LONG64 s_rowCommit = row_commit_pack(-1, 0);

static void row_commit_publish(int rowKey, int value) {
  InterlockedExchange64(&s_rowCommit, row_commit_pack(rowKey, value));
}

// Claims for `key` ONLY: several rows ask every frame. Reads the word ONCE (`seen`) so both the row check and the
// payload come from one snapshot.
static bool row_commit_take(int key, int* value) {
  const LONG64 seen = InterlockedCompareExchange64(&s_rowCommit, 0, 0);
  if (row_commit_key(seen) != key) return false;
  // Claims exactly `seen`; if the word moved the CAS fails and nothing is taken, rather than apply a commit never read.
  if (InterlockedCompareExchange64(&s_rowCommit, row_commit_pack(-1, 0), seen) != seen) return false;
  if (value) *value = row_commit_value(seen);
  return true;
}

static void row_commit_drop() {
  InterlockedExchange64(&s_rowCommit, row_commit_pack(-1, 0));
}

bool menu_entry_pending(int* rowKey, int* value) {
  const LONG64 seen = InterlockedCompareExchange64(&s_rowCommit, 0, 0);
  const int key = row_commit_key(seen);
  if (key == -1) return false;
  if (rowKey) *rowKey = key;
  if (value)  *value  = row_commit_value(seen);
  return true;
}

void menu_entry_close() {
  // The field goes with the panel: left open it would eat keys with nothing on screen. Now reachable from the window
  // thread too. A LOST UPDATE is possible (the render thread's caret-drag write can resurrect a field a window-thread
  // Enter just closed); narrow and accepted, since fixing it needs an identity-checked publish like s_rowCommit's.
  s_entry = entry_begin();
  row_commit_drop();
}

// The one place a click away becomes a commit: a left press outside the field, or a right press opening a field on
// another row. Escape is the only cancel (entry.h).
static void entry_clickaway_commit() {
  if (!s_entry.open) return;
  const int row = s_entry.rowKey;
  const EntryResult r = entry_step(s_entry, ENTRY_EV_CLICKAWAY, 0, 0, EK_NONE, 0, 0, 0);
  s_entry = r.state;
  if (r.commit) row_commit_publish(row, r.value);
}

void menu_entry_begin_edit(int rowKey, int kind, int lo, int hi, const char* text) {
  // An armed capture goes down first: input.cpp asks the field before the capture, so one armed behind an open field
  // could never bind a digit, Backspace, Enter or Escape. Cancel, not close: the row keeps its binding.
  menu_keycap_cancel();
  s_entryCaretDrag = false;
  s_entry = entry_step(s_entry, ENTRY_EV_OPEN, 0, rowKey, kind, lo, hi, text).state;
}

const char* menu_entry_key_mod(int sc, bool shift, bool ctrl) {
  if (!s_entry.open) return nullptr;
  const int row = s_entry.rowKey;

  int ev = ENTRY_EV_CHAR;
  int ch = 0;
  if (ctrl && sc == SDL_SCANCODE_A)      ev = ENTRY_EV_SELECT_ALL;
  else if (ctrl && sc == SDL_SCANCODE_C) ev = ENTRY_EV_COPY;
  else if (ctrl && sc == SDL_SCANCODE_X) ev = ENTRY_EV_CUT;
  else if (ctrl && sc == SDL_SCANCODE_V) return nullptr;
  else if (sc == SDL_SCANCODE_ESCAPE)    ev = ENTRY_EV_ESCAPE;
  else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
                                         ev = ENTRY_EV_ENTER;
  else if (sc == SDL_SCANCODE_BACKSPACE) ev = ENTRY_EV_BACKSPACE;
  else if (sc == SDL_SCANCODE_DELETE)    ev = ENTRY_EV_DELETE;
  else if (sc == SDL_SCANCODE_LEFT)  { ev = ENTRY_EV_LEFT;  ch = shift ? 1 : 0; }
  else if (sc == SDL_SCANCODE_RIGHT) { ev = ENTRY_EV_RIGHT; ch = shift ? 1 : 0; }
  else if (sc == SDL_SCANCODE_HOME)  { ev = ENTRY_EV_HOME;  ch = shift ? 1 : 0; }
  else if (sc == SDL_SCANCODE_END)   { ev = ENTRY_EV_END;   ch = shift ? 1 : 0; }
  else {
    ch = entry_char_of_scancode(sc);
    if (!ch) return nullptr;
  }

  const EntryResult r = entry_step(s_entry, ev, ch, 0, EK_NONE, 0, 0, 0);
  s_entry = r.state;
  if (r.commit) row_commit_publish(row, r.value);
  if (!r.hasClip) return nullptr;
  int n = 0;
  while (n < ENTRY_TEXT_CAP && r.clip[n]) { s_entryClip[n] = r.clip[n]; n++; }
  s_entryClip[n] = 0;
  return s_entryClip;
}

// The row widgets are the panel's whole vocabulary, carried complete. Several have no caller yet (dropdown, named and
// sub sliders); a widget added back approximately later is a different panel.

// The shared body of both switch rows; returns the new value. With no cursor room it draws nothing (ui_row has no
// floor). A disabled row is inert; a disabled row that is ON keeps its knob right but fills neutral C_RAISED: "on,
// doing nothing" must read differently from "on and live".
static int row_switch_at(UiRect& cursor, const char* label, int value, unsigned accent,
                         bool enabled, int indent, unsigned labelOn) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return value; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const UiRect sw = { r.x + r.w - 42, r.y + 5, 42, 20 };
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;

  const bool hovered = enabled && ui_hit(sw, g_ui.mx, g_ui.my);
  if (anim_style() == ANIM_OFF) {
    s_pres.hover[idx] = hovered ? 1.0 : 0.0;
  } else {
    const double rate = hovered ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double hv = s_pres.hover[idx] + rate * s_frameDt;
    if (hv < 0.0) hv = 0.0; else if (hv > 1.0) hv = 1.0;
    s_pres.hover[idx] = hv;
  }

  int knobX;
  if (anim_style() == ANIM_OFF) {
    knobX = value ? sw.x + 23 : sw.x + 2;
  } else {
    const double targetX = value ? 1.0 : 0.0;
    if (s_tabSwitchedThisFrame) { s_pres.knob[idx].x = targetX; s_pres.knob[idx].v = 0.0; }
    else anim_spring_step(&s_pres.knob[idx], targetX, ANIM_SPRING_CRITICAL.stiffness,
                          ANIM_SPRING_CRITICAL.damping, s_frameDt);
    knobX = sw.x + 2 + (int)(21.0 * s_pres.knob[idx].x + 0.5);
  }

  if (indent > 0) canvas_rect(r.x + SUB_RULE_X, r.y, 1, r.h, pres_content_colour(C_RAISED));
  canvas_text(r.x + indent, r.y + 6, label,
              pres_content_colour(pres_hover_lighten(enabled ? labelOn : C_BONEFNT, s_pres.hover[idx])), FONT_UI);
  canvas_bevel(sw.x, sw.y, sw.w, sw.h,
               pres_content_colour(value ? (enabled ? accent : C_RAISED) : C_WELL),
               pres_content_colour(C_BEVELLO), pres_content_colour(C_RAISED));
  canvas_rect(knobX, sw.y + 2, 17, 16,
              pres_content_colour((anim_style() == ANIM_OFF ? value != 0 : s_pres.knob[idx].x >= 0.5)
                                   ? C_BONE : C_RAISED));
  if (ui_row_accepts_click(enabled, row_pressed(), ui_hit(sw, g_ui.mx, g_ui.my))) {
    s_pressClaimed = true;
    g_cfgDirty = 1;
    return !value;
  }
  return value;
}

static int row_switch(UiRect& cursor, const char* label, int value, unsigned accent, bool enabled) {
  return row_switch_at(cursor, label, value, accent, enabled, 0, C_BONE);
}

static int row_switch_sub(UiRect& cursor, const char* label, int value, unsigned accent,
                          bool enabled) {
  return row_switch_at(cursor, label, value, accent, enabled, SUB_INDENT, C_BONEDIM);
}

const int FIELD_PAD        = 8;
const int TRACK_GAP        = 8;
const int TRACK_X_OFFSET   = 172;
const int TRACK_MAX_W      = 120;
const int TRACK_MIN_W      = 40;

// 24 logical pixels between every numeric slider's track and readout are reserved on every frame: shortening only on
// hover would move the knob from under the hand, and drawing over the track's end would put a click target on a drag
// target.
const int RESET_W          = 20;
const int RESET_RESERVE    = 24;

// MEASURED, NEVER MULTIPLIED: canvas_text accumulates the DEVICE advance and canvas_text_width unscales a sum once, so
// multiplying a round-tripped advance drifts from the glyphs (about 40% of a glyph over 11 characters at uiscale 132).
// Measuring the prefix pays the unscale once.
static int entry_prefix_w(const EntryState& s, int i) {
  if (i < 0) i = 0;
  if (i > s.len) i = s.len;
  char buf[ENTRY_TEXT_CAP + 1];
  int n = 0;
  while (n < i && n < ENTRY_TEXT_CAP && s.text[n]) { buf[n] = s.text[n]; n++; }
  buf[n] = '\0';
  return canvas_text_width(buf, FONT_MONO);
}
static int entry_index_measured(const EntryState& s, int textX, int px) {
  int i = 0, wPrev = 0;
  while (i < s.len) {
    const int wNext = entry_prefix_w(s, i + 1);
    if (px - textX < (wPrev + wNext + 1) / 2) break;
    wPrev = wNext;
    i++;
  }
  return i;
}

// The shared slider body. `reserveText` is the widest readout the row can show and sizes the track, never the live text
// (a track whose width depended on the dragged value would move its own hit geometry). `kind` is entry.h's EK_*;
// EK_NONE (a name readout) takes no right-click and no reset glyph. `def` is the reset glyph's only reader.
static int row_slider_at(UiRect& cursor, const char* label, int value, int lo, int hi,
                         const char* valueText, const char* reserveText,
                         bool enabled, int indent, unsigned labelOn,
                         int kind, int def) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return value; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  const int key = s_lastRowKey;
  const int valueW = canvas_text_width(valueText, FONT_MONO);
  const int valueX = r.x + r.w - VALUE_MARGIN - valueW;
  const int reserveX = r.x + r.w - VALUE_MARGIN - canvas_text_width(reserveText, FONT_MONO);
  // The track's left edge is the fixed TRACK_X_OFFSET so tracks align; an over-wide reserve slides it left to keep
  // TRACK_MIN_W a floor, and a row with a number gives up RESET_RESERVE more for the glyph.
  const int resetRoom   = (kind != EK_NONE) ? RESET_RESERVE : 0;
  const int trackRight  = reserveX - TRACK_GAP - resetRoom;
  const int trackXFixed = r.x + r.w - TRACK_X_OFFSET;
  const int trackXFit   = trackRight - TRACK_MIN_W;
  const int trackX      = trackXFixed < trackXFit ? trackXFixed : trackXFit;
  const UiRect track = { trackX, r.y + 11,
                         clamp_int(trackRight - trackX, TRACK_MIN_W, TRACK_MAX_W),
                         8 };
  const UiRect grab = { track.x - 6, track.y - 8, track.w + 12, track.h + 16 };
  const UiRect glyph = { trackRight + (RESET_RESERVE - RESET_W) / 2, r.y + 5, RESET_W, RESET_W };
  const bool fieldHere = s_entry.open != 0 && s_entry.rowKey == key;
  const bool glyphLive = enabled && kind != EK_NONE && !fieldHere
                         && !s_dragLive && ui_hit(r, g_ui.mx, g_ui.my);
  const bool glyphHot  = glyphLive && ui_hit(glyph, g_ui.mx, g_ui.my);

  if (glyphHot && row_pressed()) {
    row_commit_publish(key, def);
    s_pressSpent = true;
    s_pressClaimed = true;
  }

  bool committed = false;
  int landedRaw = 0;
  if (row_commit_take(key, &landedRaw)) {
    const int landed = clamp_int(landedRaw, lo, hi);
    committed = landed != value;
    value = landed;
  }

  if (kind != EK_NONE && enabled && !s_dragLive && g_ui.rpressed
      && ui_hit(r, g_ui.mx, g_ui.my)) {
    char prefill[ENTRY_TEXT_CAP + 1];
    entry_format(prefill, sizeof prefill, value, kind);
    entry_clickaway_commit();
    menu_entry_begin_edit(key, kind, lo, hi, prefill);
  }

  const bool fieldOpen = s_entry.open != 0 && s_entry.rowKey == key;

  if (glyphHot) { s_hoverRowKey = key; s_glyphHint = MENU_RESET_HINT; }

  const int kx = ui_slider_x(track, value, lo, hi);

  const bool hovered = enabled && ui_hit(grab, g_ui.mx, g_ui.my);
  if (anim_style() == ANIM_OFF) {
    s_pres.hover[idx] = hovered ? 1.0 : 0.0;
  } else {
    const double rate = hovered ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double hv = s_pres.hover[idx] + rate * s_frameDt;
    if (hv < 0.0) hv = 0.0; else if (hv > 1.0) hv = 1.0;
    s_pres.hover[idx] = hv;
  }
  if (anim_style() == ANIM_OFF) {
    s_pres.resetHover[idx] = glyphHot ? 1.0 : 0.0;
  } else {
    const double rate = glyphHot ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double rv = s_pres.resetHover[idx] + rate * s_frameDt;
    if (rv < 0.0) rv = 0.0; else if (rv > 1.0) rv = 1.0;
    s_pres.resetHover[idx] = rv;
  }

  int drawnKx;
  if (anim_style() == ANIM_OFF) {
    drawnKx = kx;
  } else {
    const double targetFrac = track.w > 0 ? (double)(kx - track.x) / (double)track.w : 0.0;
    if (s_tabSwitchedThisFrame) { s_pres.fill[idx].x = targetFrac; s_pres.fill[idx].v = 0.0; }
    else anim_spring_step(&s_pres.fill[idx], targetFrac, ANIM_SPRING_CRITICAL.stiffness,
                          ANIM_SPRING_CRITICAL.damping, s_frameDt);
    drawnKx = track.x + (int)(s_pres.fill[idx].x * track.w + 0.5);
    if (drawnKx < track.x) drawnKx = track.x;
    if (drawnKx > track.x + track.w) drawnKx = track.x + track.w;
  }

  if (indent > 0) canvas_rect(r.x + SUB_RULE_X, r.y, 1, r.h, pres_content_colour(C_RAISED));
  canvas_text(r.x + indent, r.y + 6, label,
              pres_content_colour(pres_hover_lighten(enabled ? labelOn : C_BONEFNT, s_pres.hover[idx])), FONT_UI);
  canvas_bevel(track.x, track.y, track.w, track.h, pres_content_colour(C_WELL),
               pres_content_colour(C_BEVELLO), pres_content_colour(C_RAISED));
  canvas_rect(track.x, track.y, drawnKx - track.x, track.h, pres_content_colour(C_HAZARDDIM));
  canvas_rect(drawnKx - 4, track.y - 4, 9, 16, pres_content_colour(enabled ? C_HAZARD : C_BONEFNT));
  if (fieldOpen) {
    // The typed field: rounded well, C_HAZARD rim ("acting right now"), sized for the widest text it can hold
    // ("-2147483648"), as a well that grew while typed into would move under the hand.
    const int fieldW = canvas_text_width("-2147483648", FONT_MONO) + 2 * FIELD_PAD;
    const UiRect field = { r.x + r.w - fieldW, r.y + 5, fieldW, 20 };
    s_entryFieldRect = field;
    canvas_round_rect(field.x, field.y, field.w, field.h, ROW_R,
                      pres_content_colour(C_WELL));
    canvas_round_rect_outline(field.x, field.y, field.w, field.h, ROW_R, 1,
                              pres_content_colour(C_HAZARD));
    const Glyph& capA  = font_glyphs[FONT_MONO]['A' - 0x20];
    const Glyph& descY = font_glyphs[FONT_MONO]['y' - 0x20];
    const int inkTop = capA.by;
    const int inkH   = (descY.by + descY.h) - inkTop;
    const int textY  = field.y + (field.h - inkH) / 2 - inkTop;
    const int textX  = field.x + FIELD_PAD;
    // Every x inside the well comes from entry_prefix_w. A left press in the well puts the caret and holding drags the
    // selection (the press index is the anchor). It uses the raw button and its own latch, not row_pressed():
    // s_pressSpent, set by this very press, would filter it.
    if (g_ui.pressed && ui_hit(field, g_ui.mx, g_ui.my)) {
      s_entry = entry_step(s_entry, ENTRY_EV_SET_CARET,
                           entry_index_measured(s_entry, textX, g_ui.mx),
                           0, EK_NONE, 0, 0, 0).state;
      s_entryCaretDrag = true;
    } else if (s_entryCaretDrag && g_ui.down) {
      s_entry = entry_step(s_entry, ENTRY_EV_SET_CARET,
                           entry_index_measured(s_entry, textX, g_ui.mx),
                           1, EK_NONE, 0, 0, 0).state;
    }
    const int selA = entry_sel_lo(s_entry), selB = entry_sel_hi(s_entry);
    if (selB > selA) {
      const int selAx = entry_prefix_w(s_entry, selA);
      const int selBx = entry_prefix_w(s_entry, selB);
      canvas_rect(textX + selAx, field.y + 4, selBx - selAx,
                  field.h - 8, pres_content_colour(C_HAZARDDIM));
    }
    canvas_text(textX, textY, s_entry.text, pres_content_colour(C_BONE), FONT_MONO);
    const bool caretOn = (anim_style() == ANIM_OFF) || ((menu_now() / 500u) % 2u == 0u);
    if (caretOn)
      canvas_rect(textX + entry_prefix_w(s_entry, s_entry.caret) + 1, field.y + 4,
                  1, field.h - 8, pres_content_colour(C_BONE));
  } else {
  canvas_text(valueX, r.y + 7, valueText, pres_content_colour(C_BONE), FONT_MONO);

  // The reset button: a well with a circular arrow, hovered by its own fraction. The icon is two open rings of eight
  // points (radii 6 and 5, so the stroke reads 2px) plus a 3x3 head; no segment may close a ring, canvas_line being the
  // only curve.
  if (glyphLive) {
    const double rf = s_pres.resetHover[idx];
    canvas_round_rect(glyph.x, glyph.y, glyph.w, glyph.h, ROW_R,
                      pres_content_colour(pres_hover_toward(C_WELL, C_RAISED, rf)));
    canvas_round_rect_outline(glyph.x, glyph.y, glyph.w, glyph.h, ROW_R, 1,
                              pres_content_colour(pres_hover_toward(C_RAISED, C_BONEDIM, rf)));
    static const int RING6[8][2] = {
      { 16, 10 }, { 15, 6 }, { 11, 4 }, { 7, 5 }, { 4, 8 },
      { 4, 12 }, { 7, 15 }, { 11, 16 }
    };
    static const int RING5[8][2] = {
      { 15, 10 }, { 14, 7 }, { 11, 5 }, { 7, 6 }, { 5, 8 },
      { 5, 12 }, { 7, 14 }, { 11, 15 }
    };
    static const int BARB[2] = { 13, 13 };
    const int ix = glyph.x - 1, iy = glyph.y;
    const unsigned gc = pres_content_colour(glyphHot ? C_BONE : C_BONEDIM);
    for (int i = 0; i < 7; i++) {
      canvas_line(ix + RING6[i][0], iy + RING6[i][1],
                  ix + RING6[i + 1][0], iy + RING6[i + 1][1], gc);
      canvas_line(ix + RING5[i][0], iy + RING5[i][1],
                  ix + RING5[i + 1][0], iy + RING5[i + 1][1], gc);
    }
    canvas_rect(ix + RING6[0][0] - 1, iy + RING6[0][1] - 1, 3, 3, gc);
    canvas_line(ix + RING6[0][0], iy + RING6[0][1], ix + BARB[0], iy + BARB[1], gc);
  }
  }

  // The held button, not the press edge: a slider is dragged. It reads the logical track so a drag lands on the value
  // the control is going to.
  if (ui_row_accepts_click(enabled, row_down(), ui_hit(grab, g_ui.mx, g_ui.my))) {
    // The drag's press is claimed every frame since a slider reads the held button; not s_pressSpent, which spans the
    // hold and would stop its own drag.
    s_pressClaimed = true;
    const int v = ui_slider_value(track, g_ui.mx, lo, hi);
    if (v != value) { g_cfgDirty = 1; return v; }
  }
  if (committed) g_cfgDirty = 1;
  return value;
}

static int row_slider(UiRect& cursor, const char* label, int value, int lo, int hi,
                      const char* suffix, bool enabled, int def) {
  char buf[32], reserve[32];
  sprintf_s(buf, "%d%s", value, suffix);
  sprintf_s(reserve, "%d%s", hi, suffix);
  return row_slider_at(cursor, label, value, lo, hi, buf, reserve, enabled, 0, C_BONE,
                       EK_INT, def);
}

static int row_slider_sub(UiRect& cursor, const char* label, int value, int lo, int hi,
                          const char* suffix, bool enabled, int def) {
  char buf[32], reserve[32];
  sprintf_s(buf, "%d%s", value, suffix);
  sprintf_s(reserve, "%d%s", hi, suffix);
  return row_slider_at(cursor, label, value, lo, hi, buf, reserve, enabled,
                       SUB_INDENT, C_BONEDIM, EK_INT, def);
}

// A slider whose readout is METRES (Aim range): row_slider's suffix cannot divide. A sibling caller, not a wider
// contract. The reserve is `hi` through the same formatting, so the track stays fixed while dragging.
static int row_slider_metres(UiRect& cursor, const char* label, int value, int lo, int hi,
                             bool enabled, int def) {
  char buf[32], reserve[32];
  sprintf_s(buf, "%d m", ekm_metres(value));
  sprintf_s(reserve, "%d m", ekm_metres(hi));
  return row_slider_at(cursor, label, value, lo, hi, buf, reserve, enabled, 0, C_BONE,
                       EK_METRES, def);
}

static int row_slider_named(UiRect& cursor, const char* label, int value, int lo, int hi,
                            const char* (*labelFn)(int), bool enabled) {
  const char* reserve = labelFn(lo);
  int reserveW = canvas_text_width(reserve, FONT_MONO);
  for (int v = lo + 1; v <= hi; v++) {
    const char* s = labelFn(v);
    const int w = canvas_text_width(s, FONT_MONO);
    if (w > reserveW) { reserve = s; reserveW = w; }
  }
  return row_slider_at(cursor, label, value, lo, hi, labelFn(value), reserve,
                       enabled, 0, C_BONE, EK_NONE, 0);
}

static int row_slider_named_sub(UiRect& cursor, const char* label, int value, int lo, int hi,
                                const char* (*labelFn)(int), bool enabled) {
  const char* reserve = labelFn(lo);
  int reserveW = canvas_text_width(reserve, FONT_MONO);
  for (int v = lo + 1; v <= hi; v++) {
    const char* s = labelFn(v);
    const int w = canvas_text_width(s, FONT_MONO);
    if (w > reserveW) { reserve = s; reserveW = w; }
  }
  return row_slider_at(cursor, label, value, lo, hi, labelFn(value), reserve,
                       enabled, SUB_INDENT, C_BONEDIM, EK_NONE, 0);
}

// A dropdown: a value with several named stops. Mouse only (the panel has no arrow-key navigation). The open list
// cannot be drawn by its row: rows draw in order into an immediate-mode canvas with no z-order, so every later row
// would paint over it. It is deferred to menu_dropdown_draw, called where the tooltip is.
struct MenuDropdown {
  int  open;  // int, not bool: MSVC 19.50 /O2.
  int  rowKey;
  UiRect box;
  int  count;
  const char* (*labelFn)(int);
};
static MenuDropdown s_dd = { 0, -1, { 0, 0, 0, 0 }, 0, 0 };

static const int DD_CHEVRON_W = 14;
static const int DD_PAD       = 6;

static int row_dropdown(UiRect& cursor, const char* label, int value, int count,
                        const char* (*labelFn)(int), bool enabled) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return value; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  const int key = s_lastRowKey;

  int nameW = 0;
  for (int v = 0; v < count; v++) {
    const int w = canvas_text_width(labelFn(v), FONT_MONO);
    if (w > nameW) nameW = w;
  }
  const int boxW = nameW + DD_PAD * 2 + DD_CHEVRON_W;
  const UiRect box = { r.x + r.w - VALUE_MARGIN - boxW, r.y + 4, boxW, ROW_H - 8 };

  const bool hot = enabled && ui_hit(box, g_ui.mx, g_ui.my);
  const bool mine = s_dd.open != 0 && s_dd.rowKey == key;
  if (hot && row_pressed()) {
    s_pressSpent = true;
    if (mine) { s_dd.open = 0; s_dd.rowKey = -1; }
    else { s_dd.open = 1; s_dd.rowKey = key; s_dd.box = box; s_dd.count = count;
           s_dd.labelFn = labelFn; }
  } else if (mine) {
    s_dd.box = box;
  }

  const unsigned fill = enabled ? (hot || mine ? C_RAISED : C_WELL) : C_WELL;
  canvas_bevel(box.x, box.y, box.w, box.h, pres_content_colour(fill),
               pres_content_colour(C_BEVELHI), pres_content_colour(C_BEVELLO));
  canvas_text(r.x, r.y + 8, label, pres_colour(enabled ? C_BONE : C_BONEFNT), FONT_UI);
  canvas_text(box.x + DD_PAD, box.y + 4, labelFn(clamp_int(value, 0, count - 1)),
              pres_colour(enabled ? C_BONE : C_BONEFNT), FONT_MONO);
  {
    const int cx = box.x + box.w - DD_CHEVRON_W / 2 - DD_PAD / 2;
    const int cy = box.y + box.h / 2;
    const unsigned cc = pres_colour(enabled ? C_BONEDIM : C_BONEFNT);
    const int dy = mine ? -3 : 3;
    canvas_line(cx - 4, cy - dy / 2, cx, cy + dy, cc);
    canvas_line(cx, cy + dy, cx + 4, cy - dy / 2, cc);
  }

  int pickedRaw = 0;
  if (row_commit_take(key, &pickedRaw)) {
    const int v = clamp_int(pickedRaw, 0, count - 1);
    if (v != value) g_cfgDirty = 1;
    return v;
  }
  return value;
}

static void menu_dropdown_draw() {
  if (!s_dd.open || !s_dd.labelFn || s_dd.count <= 0) return;
  const int itemH = ROW_H - 6;
  const UiRect list = { s_dd.box.x, s_dd.box.y + s_dd.box.h + 2,
                        s_dd.box.w, itemH * s_dd.count + 4 };
  canvas_bevel(list.x, list.y, list.w, list.h, pres_content_colour(C_PANEL),
               pres_content_colour(C_BEVELHI), pres_content_colour(C_BEVELLO));
  bool onAnItem = false;
  for (int i = 0; i < s_dd.count; i++) {
    const UiRect it = { list.x + 2, list.y + 2 + i * itemH, list.w - 4, itemH };
    const bool hot = ui_hit(it, g_ui.mx, g_ui.my);
    if (hot) {
      onAnItem = true;
      canvas_rect(it.x, it.y, it.w, it.h, pres_content_colour(C_RAISED));
    }
    canvas_text(it.x + DD_PAD - 2, it.y + 3, s_dd.labelFn(i),
                pres_colour(hot ? C_BONE : C_BONEDIM), FONT_MONO);
    if (hot && row_pressed()) {
      s_pressSpent = true;
      row_commit_publish(s_dd.rowKey, i);
      s_dd.open = 0; s_dd.rowKey = -1;
      return;
    }
  }
  // Click away closes without changing anything; the box is excluded since its own row toggles on that press (it would
  // reopen in one frame).
  if (!onAnItem && row_pressed() && !ui_hit(s_dd.box, g_ui.mx, g_ui.my)) {
    s_pressSpent = true;
    s_dd.open = 0; s_dd.rowKey = -1;
  }
}

// Uppercase and spaced, passed uppercase by the caller (no text-transform); a heading labels a group, so it is not a
// sentence-case breach.
static void draw_group(UiRect& cursor, const char* heading) {
  if (cursor.h < GROUP_H) { s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, GROUP_H, GROUP_GAP);
  canvas_text(r.x, r.y + 5, heading, pres_content_colour(C_BONEFNT), FONT_SMALL);
}

// A row of text for a state the player must see without hovering.
static void draw_placeholder(UiRect& cursor, const char* text) {
  if (cursor.h < ROW_H) { s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  canvas_text(r.x, r.y + 6, text, pres_content_colour(C_BONEFNT), FONT_SMALL);
}

// A binding row: a field-shaped well invites a click. The click only ARMS; the next key or button, handed by input.cpp
// to menu_keycap_bind, binds. It lands anywhere on the row. Always live: the feature's own switch decides whether the
// key acts, so there is no `enabled`.
static void row_bind(UiRect& cursor, const char* label, int row) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  // Sized for the widest of three strings (the prompt, the invitation, the widest key name), so the well never changes
  // width when armed or hovered.
  int fieldW = canvas_text_width(MENU_KEYCAP_PROMPT, FONT_MONO);
  {
    const int hoverW = canvas_text_width(MENU_KEYCAP_HOVER, FONT_MONO);
    const int nameW  = canvas_text_width(BIND_LABEL_WIDEST, FONT_MONO);
    if (hoverW > fieldW) fieldW = hoverW;
    if (nameW  > fieldW) fieldW = nameW;
  }
  fieldW += 2 * FIELD_PAD;
  const UiRect field = { r.x + r.w - fieldW, r.y + 5, fieldW, 20 };

  const bool hovered = ui_hit(field, g_ui.mx, g_ui.my);
  if (anim_style() == ANIM_OFF) {
    s_pres.hover[idx] = hovered ? 1.0 : 0.0;
  } else {
    const double rate = hovered ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double hv = s_pres.hover[idx] + rate * s_frameDt;
    if (hv < 0.0) hv = 0.0; else if (hv > 1.0) hv = 1.0;
    s_pres.hover[idx] = hv;
  }

  canvas_text(r.x, r.y + 6, label, pres_content_colour(C_BONE), FONT_UI);
  const bool armed = s_keycap.phase == KEYCAP_ARMED && s_keycap.row == row;
  const int vk = config_bind_get(row);
  // Hover lifts the well, not the text, or a fully hovered field would draw its name in its well's colour. The rim is
  // hazard yellow in capture.
  canvas_round_rect(field.x, field.y, field.w, field.h, ROW_R,
                    pres_content_colour(pres_hover_toward(C_WELL, C_RAISED, s_pres.hover[idx])));
  canvas_round_rect_outline(field.x, field.y, field.w, field.h, ROW_R, 1,
                            pres_content_colour(armed ? C_HAZARD
                                                      : pres_hover_toward(C_RAISED, C_BONEDIM, s_pres.hover[idx])));
  const char* const value = bind_field_text(armed, hovered, vk,
                                            MENU_KEYCAP_PROMPT, MENU_KEYCAP_HOVER);
  const int valueW = canvas_text_width(value, FONT_MONO);
  // Ink centred, not the pen: canvas_text puts a glyph's top at pen y + glyph.by, so a descender stays off the rim.
  const Glyph& capA  = font_glyphs[FONT_MONO]['A' - 0x20];
  const Glyph& descY = font_glyphs[FONT_MONO]['y' - 0x20];
  const int inkTop = capA.by;
  const int inkH   = (descY.by + descY.h) - inkTop;
  const int textY  = field.y + (field.h - inkH) / 2 - inkTop;
  canvas_text(field.x + (field.w - valueW) / 2, textY, value,
              pres_content_colour(armed ? C_HAZARD : C_BONE), FONT_MONO);
  if (ui_row_accepts_click(true, row_pressed(), ui_hit(r, g_ui.mx, g_ui.my))) {
    KeyCapEvent e; e.kind = KEYCAP_EV_CLICK; e.row = row; e.vk = 0;
    s_keycap = keycap_step(s_keycap, e).state;
    s_pressClaimed = true;
  }
  // THE GAME'S OWN CLAIM ON THIS KEY: one line that the key is also the game's, shown while choosing. Absent when
  // settings.cfg could not be read: a warning that cannot be computed is never guessed.
  const char* taken = gamecfg_function_for(vk);
  if (taken) draw_placeholder(cursor, MENU_GAMECFG_NOTE);
}

void menu_draw(int screenW, int screenH) {
  s_pres.rowsThisFrame = 0;
  s_rowsDropped = 0;
  s_hoverRowKey = -1;
  s_hoverHint = nullptr;
  s_lastRowKey = -1;
  s_glyphHint = nullptr;
  if (!g_ui.down) s_pressSpent = false;
  s_pressClaimed = false;
  {
    const unsigned now = menu_now();
    const bool stale = (now - s_pres.lastMs) > ANIM_STALE_MS;
    s_frameDt = anim_dt_clamped(s_pres.lastMs, now);
    s_pres.lastMs = now;
    s_pres.openTarget = input_panel_open() ? 1 : 0;
    if (s_pres.openTarget && (!s_pres.wasOpen || stale)) {
      menu_note_new_session();
      if (stale) { s_pres.open.x = 0.0; s_pres.open.v = 0.0; }
    }
    s_pres.wasOpen = s_pres.openTarget;
    if (anim_style() == ANIM_OFF) {
      s_pres.open.x = (double)s_pres.openTarget;
      s_pres.open.v = 0.0;
    } else {
      anim_spring_step(&s_pres.open, (double)s_pres.openTarget,
                       ANIM_SPRING_CRITICAL.stiffness, ANIM_SPRING_CRITICAL.damping, s_frameDt);
    }
  }
  // Logical pixels below; the factor must be back to UI_SCALE_DEFAULT on return, as the ESP draws through the same
  // canvas earlier. `pct` is the settled scale held at its pre-drag value while the button is down so rows do not slide
  // from under the hand.
  const int settled = menu_applied_scale(screenH);
  const int pct = ui_scale_effective(settled, s_scaleHeld, g_ui.down);
  if (!g_ui.down) s_scaleHeld = settled;
  canvas_set_scale(pct);
  const int deviceMx = g_ui.mx, deviceMy = g_ui.my;
  g_ui.mx = ui_unscale_px(deviceMx, pct);
  g_ui.my = ui_unscale_px(deviceMy, pct);
  const int logicalW = ui_unscale_px(screenW, pct);
  const int logicalH = ui_unscale_px(screenH, pct);

  // The click away, once, before any row: a press of either button outside the open field finishes it (the press may
  // land on an earlier row or nothing). A right press opening a field elsewhere reads raw `rpressed`, so it commits
  // here and opens there. An open field claims the left press.
  if (s_entry.open && (g_ui.pressed || g_ui.rpressed) && !s_dragLive) {
    if (g_ui.pressed) { s_pressClaimed = true; s_pressSpent = true; }
    if (!ui_hit(s_entryFieldRect, g_ui.mx, g_ui.my)) entry_clickaway_commit();
  }

  // The drag, before drawing: the box is the four ini keys through panel_layout, overridden by the live box during a
  // move or resize. The grip wins over a move because panel_drag_step asks about it first; every other pixel reaches
  // PDRAG_MOVE via the rule at the foot.
  const PanelBox laid = panel_layout(panel_stored_box(), logicalW, logicalH);
  PanelBox box = laid;
  bool gripHit = false;
  {
    const bool titleHit = ui_hit(panel_title_rect(laid, STRIP_H, STATUS_H), g_ui.mx, g_ui.my);
    gripHit = ui_hit(panel_grip_rect(s_dragLive ? s_dragBox : laid), g_ui.mx, g_ui.my);
    bool ended = false;
    s_drag = panel_drag_step(s_drag, g_ui.pressed, g_ui.down, titleHit, gripHit,
                             g_ui.mx, g_ui.my, laid, logicalW, logicalH, &box, &ended);
    s_dragLive = s_drag.phase != PDRAG_IDLE;
    if (ended) {
      box = s_dragBox;
      if (box.x != laid.x || box.y != laid.y || box.w != laid.w || box.h != laid.h) {
        g_cfg.panelx = box.x; g_cfg.panely = box.y;
        g_cfg.panelw = box.w; g_cfg.panelh = box.h;
        g_cfgDirty = 1;
      }
    } else if (s_dragLive) {
      s_dragBox = box;
    }
  }
  const UiRect p = { box.x, box.y, box.w, box.h };

  // Counts parent switches only (menu_live_count, menu.h). The free camera counts though not a combat edge: seeing
  // anywhere in the map is an advantage. fovon counts (a wider view than the game's menu offers). secrettriggers passes
  // 0 as a SUB-ROW of Secret areas.
  const int liveCount = menu_live_count(g_cfg.fovon, g_cfg.esp,
                                        g_cfg.espitems, g_cfg.espsecrets, g_cfg.espexplodables, g_cfg.espswitches,
                                        g_cfg.aimon, g_cfg.silenton, g_cfg.freecamon,
                                        g_cfg.accpistol, g_cfg.accchaingun,
                                        g_cfg.accshotgun, g_cfg.accexpander,
                                        g_cfg.dmgnumbers, 0, g_cfg.tboton, g_cfg.espbreakables);
  const unsigned strip = liveCount ? C_RED : C_HAZARD;

  // The whole panel moves as one canvas-wide transform (canvas.h) rather than a second cursor through every draw call:
  // `p` and `cursor` stay put and every ui_hit tests the logical layout. Set in every style (identity under Off) so a
  // style change leaves no motion standing; canvas_begin clears it each frame before the ESP draws.
  if (anim_style() == ANIM_SIMPLE) {
    const int slideY = (int)((1.0 - s_pres.open.x) * (double)SIMPLE_SLIDE_PX + 0.5);
    canvas_set_transform(0, -slideY, 1.0, 0, 0);
  } else {
    canvas_set_transform(0, 0, 1.0, 0, 0);
  }

  canvas_bevel(p.x, p.y, p.w, p.h,
               pres_colour(C_PANEL), pres_colour(C_BEVELHI), pres_colour(C_BEVELLO));
  canvas_hazard(p.x + 1, p.y + 1, p.w - 2, STRIP_H, pres_colour(strip), pres_colour(C_PANEL), 9);

  const int sy = p.y + 1 + STRIP_H;
  canvas_rect(p.x + 1, sy, p.w - 2, STATUS_H, pres_colour(C_RECESS));
  canvas_rect(p.x + PAD, sy + 11, 9, 9, pres_colour(strip));
  char status[32];
  if (liveCount) sprintf_s(status, "%d cheat%s live", liveCount, liveCount == 1 ? "" : "s");
  else           sprintf_s(status, "Idle");
  canvas_text(p.x + PAD + 18, sy + 7, status, pres_colour(C_BONEDIM), FONT_SMALL);
  // The panel key BY NAME, not World Tour's literal "Ins closes": it is rebindable. Through bind_menu_key_effective,
  // the fallback input.cpp applies. Right-aligned and measured, as a wide name ("Page down") would run off the panel.
  char closeText[32];
  sprintf_s(closeText, "%s closes",
            bind_label(bind_menu_key_effective(config_bind_get(BIND_ROW_MENU))));
  canvas_text(p.x + p.w - PAD - canvas_text_width(closeText, FONT_SMALL), sy + 7,
              closeText, pres_colour(C_BONEFNT), FONT_SMALL);

  const int bodyY = sy + STATUS_H;
  const int bodyH = p.y + p.h - bodyY - 1;

  canvas_rect(p.x + 1, bodyY, RAIL_W, bodyH, pres_colour(C_WELL));
  // The rail click is applied at the foot of the function, after the tab's rows have had their chance to consume a
  // pending commit: switching here would orphan a commit owned by a row nothing draws.
  int railTab = -1;
  for (int i = 0; i < TAB_COUNT; i++) {
    const UiRect t = { p.x + 1, bodyY + i * 46, RAIL_W, 46 };
    if (i == s_tab) {
      canvas_rect(t.x, t.y, t.w, t.h, pres_colour(C_PANEL));
      canvas_rect(t.x, t.y, 3, t.h, pres_colour(TAB_COLOURS[i]));
    }
    const int nameY = t.y + (t.h - font_height[FONT_UI]) / 2;
    canvas_text(t.x + RAIL_LABEL_X, nameY, TAB_NAMES[i],
                pres_colour(i == s_tab ? C_BONE : C_BONEDIM), FONT_UI);
    if (g_ui.pressed && ui_hit(t, g_ui.mx, g_ui.my)) {
      railTab = i;
      s_pressClaimed = true;
    }
  }

  // A tab switch snaps every row spring on its first content frame and starts the crossfade; the switch is applied at
  // the foot, so this is the first frame the new content draws.
  s_tabSwitchedThisFrame = (s_tab != s_pres.tabFrom);
  if (s_tabSwitchedThisFrame) s_pres.tabSwitchMs = s_pres.lastMs;
  s_pres.tabFrom = s_tab;

  UiRect cursor = { p.x + 1 + RAIL_W + PAD, bodyY + PAD, p.w - RAIL_W - 2 * PAD - 2, bodyH - 2 * PAD };
  switch (s_tab) {
    case TAB_VISUALS: {
      draw_group(cursor, "ENEMIES");
      g_cfg.esp = row_switch(cursor, "Enemies", g_cfg.esp, C_RED, true);
      const bool enemiesOn = g_cfg.esp != 0;
      g_cfg.espboxes     = row_switch_sub(cursor, "Bounding boxes",  g_cfg.espboxes,     C_RED, enemiesOn);
      g_cfg.esphealthbar = row_switch_sub(cursor, "Health bars",     g_cfg.esphealthbar, C_RED, enemiesOn);
      g_cfg.espname      = row_switch_sub(cursor, "Name and health", g_cfg.espname,      C_RED, enemiesOn);
      g_cfg.espdistance  = row_switch_sub(cursor, "Distance",        g_cfg.espdistance,  C_RED, enemiesOn);

      g_cfg.espitems = row_switch(cursor, "Items and pickups", g_cfg.espitems,
                                  C_BLUE, true);

      g_cfg.espsecrets = row_switch(cursor, "Secret areas", g_cfg.espsecrets,
                                    C_HAZARD, true);
      // A sub-row gated on its parent: the panels are pushed inside the secret pass, which only runs while that row is
      // on. Takes the parent's accent (canvas.h's C_SECRET_VOLUME).
      g_cfg.secretvolume = row_switch_sub(cursor, "Secret volume", g_cfg.secretvolume,
                                          C_HAZARD, g_cfg.espsecrets != 0);
      // Beside it, gated the same way (chains draw inside the secret pass). The hint admits the limit: an activator's
      // hitag gates on whether its sector is open (sector.cpp:1133-1140) and EVENT_OPERATEACTIVATORS can veto (:1121),
      // neither knowable at scan time, so the line shows map structure.
      g_cfg.secrettriggers = row_switch_sub(cursor, "Secret triggers", g_cfg.secrettriggers,
                                            C_HAZARD, g_cfg.espsecrets != 0);
      row_hint("Map structure only. A hitag or CON script may still block.");

      // A third sub-row of Secret areas. The hint is load-bearing: the search sees geometry only, so it routes through
      // keycard doors, past locked activators and down drops that cannot be climbed, a confident wrong answer.
      g_cfg.secretpath = row_switch_sub(cursor, "Secret path", g_cfg.secretpath,
                                        C_HAZARD, g_cfg.espsecrets != 0);
      row_hint("Geometry only. May route through locks and one way drops.");

      g_cfg.espexplodables = row_switch(cursor, "Explodables", g_cfg.espexplodables,
                                        C_HAZARD, true);
      g_cfg.espbreakables  = row_switch(cursor, "Breakables", g_cfg.espbreakables,
                                        C_HAZARD, true);

      // Its own parent: drawn from the damage detour's ring, independent of any ESP category.
      g_cfg.dmgnumbers = row_switch(cursor, "Damage numbers", g_cfg.dmgnumbers,
                                    C_HAZARD, true);
      row_hint("Your own hits only. Needs the damage hook to have taken.");

      g_cfg.espswitches = row_switch(cursor, "Switches and locks", g_cfg.espswitches,
                                     C_BLUE, true);
      g_cfg.esptint = row_switch_sub(cursor, "Combination tint", g_cfg.esptint,
                                     C_BLUE, g_cfg.espswitches != 0);

      draw_group(cursor, "RANGE");
      // Always live: a range, not a feature. row_slider_metres divides for the label only (stored Build units, esp.h).
      // Default is the MAXIMUM: an active cull reads as a broken projection.
      g_cfg.espdrawdist = row_slider_metres(cursor, "Draw distance", g_cfg.espdrawdist,
                                            ESP_DRAWDIST_MIN, ESP_DRAWDIST_MAX,
                                            true, ESP_DRAWDIST_DEFAULT);
      row_hint("Markers stop at this range.");
      g_cfg.espnolimit = row_switch_sub(cursor, "Unlimited range", g_cfg.espnolimit,
                                        C_GREEN, true);
      row_hint("Ignores the distance. Slow on a large map.");

      // Fullbright is not ported: World Tour's row levers a renderer lighting term Polymer does not share.
      break;
    }
    case TAB_COMBAT: {
      draw_group(cursor, "TARGETING");
      // Targeting shapes silent aim too: aim_acquire serves both, so cone, range, pick, aim point and lead apply to
      // silent aim, and gating on the aimbot alone locked silent-aim-only players out. Mirrors aim_frame's `wantAim ||
      // wantSilent`.
      const bool tgtOn = aim_available() &&
                         (g_cfg.aimon || (g_cfg.silenton && gamehooks_shoot_ready()));
      g_cfg.aimtarget = row_slider_named(cursor, "Pick target", g_cfg.aimtarget,
                                         0, 1, aim_target_label, tgtOn);
      row_hint("Crosshair picks what you are facing.");
      g_cfg.aimpoint = row_slider_named(cursor, "Aim at", g_cfg.aimpoint,
                                        0, 1, aim_point_label, tgtOn);
      g_cfg.aimcone = row_slider(cursor, "Cone", g_cfg.aimcone, 1, 90, " deg", tgtOn, 30);
      row_hint("How far off centre a target may be.");
      g_cfg.aimrange = row_slider_metres(cursor, "Aim range", g_cfg.aimrange,
                                         512, 32768, tgtOn, 32768);
      g_cfg.aimlead = row_switch(cursor, "Lead moving targets", g_cfg.aimlead, C_RED, tgtOn);
      g_cfg.aimleadscale = row_slider_sub(cursor, "Lead amount", g_cfg.aimleadscale,
                                          0, 100, "%", tgtOn && g_cfg.aimlead, 70);
      // The marker is gated by its own switch AND (aimon || silenton) (esp_layout.h aim_marker_on); tgtOn is that OR.
      g_cfg.aimdebug = row_switch(cursor, "Aim calibration marker", g_cfg.aimdebug,
                                  C_RED, tgtOn);
      row_hint("Shows the raw aim point and the led point.");

      draw_group(cursor, "AIMBOT");
      // The suffix is not decoration: without the player struct or cansee there is no honest aimbot.
      const bool armed = aim_available();
      // The stored value is passed whether or not the row is live: a disabled row returns what it was given, so a 0
      // handed in during the scan overwrote the player's setting.
      g_cfg.aimon = row_switch(cursor, armed ? "Aimbot" : "Aimbot (Unavailable)",
                               g_cfg.aimon, C_RED, armed);
      row_hint(armed ? "Turns your view onto the nearest visible enemy."
                     : "Needs the engine scan to finish. See ekmod.log.");
      const bool aimOn = armed && g_cfg.aimon;
      g_cfg.aimhold = row_switch_sub(cursor, "Hold key", g_cfg.aimhold, C_RED, aimOn);
      row_hint("Only while the Aimbot button is held down.");
      g_cfg.aimsmooth = row_slider_sub(cursor, "Smoothing", g_cfg.aimsmooth,
                                       0, 100, "%", aimOn, 0);

      draw_group(cursor, "TRIGGERBOT");
      // Two independent requirements: player struct and cansee for acquisition, and localInput located for the write.
      // One suffix for both; the log says which half is missing.
      const bool tb = aim_available() && mousebind_ready();
      g_cfg.tboton = row_switch(cursor, tb ? "Triggerbot" : "Triggerbot (Unavailable)",
                                g_cfg.tboton, C_RED, tb);
      row_hint(tb ? "Fires by itself while the condition below holds"
                  : "Needs the engine scan to finish. See ekmod.log.");
      const bool tbOn = tb && g_cfg.tboton;
      g_cfg.tbotwhen = row_slider_named_sub(cursor, "Fire when", g_cfg.tbotwhen,
                                            TBOT_WHEN_CROSSHAIR, TBOT_WHEN_AIMBOT,
                                            tbot_when_label, tbOn);
      row_hint("Crosshair mode needs you pointing at a visible enemy.");
      g_cfg.tbotdelay = row_slider_sub(cursor, "Delay", g_cfg.tbotdelay,
                                       TBOT_DELAY_MS_MIN, TBOT_DELAY_MS_MAX, " ms",
                                       tbOn, TBOT_DELAY_MS_DEFAULT);
      row_hint("How long the condition must hold before it fires.");

      draw_group(cursor, "SILENT AIM");
      // Two independent requirements: the firing hook installed, and the player struct and cansee found. One suffix;
      // the log says which.
      const bool sa = gamehooks_shoot_ready() && aim_available();
      g_cfg.silenton = row_switch(cursor, sa ? "Silent aim" : "Silent aim (Unavailable)",
                                  g_cfg.silenton, C_RED, sa);
      row_hint("Shots go to the target without moving your view.");
      g_cfg.silentmaxangle = row_slider_sub(cursor, "Max redirect", g_cfg.silentmaxangle,
                                            1, 180, " deg", sa && g_cfg.silenton, 45);
      row_hint("Shots further off than this are left alone.");
      break;
    }
    // VIEW stays empty on purpose: a heading over no row promises what the DLL cannot do, and the rail keeps all five
    // entries because it is the menu.
    case TAB_WEAPONS: {
      draw_group(cursor, "PERFECT ACCURACY");
      const bool sp = gamehooks_spread_ready();
      // All four rows carry the suffix when the hook is absent, so any one row alone tells the player the feature
      // cannot run.
      g_cfg.accpistol   = row_switch(cursor, sp ? "Pistol" : "Pistol (Unavailable)",
                                     g_cfg.accpistol, C_HAZARD, sp);
      row_hint("Removes this weapon's own spread.");
      g_cfg.accchaingun = row_switch(cursor, sp ? "Chaingun" : "Chaingun (Unavailable)",
                                     g_cfg.accchaingun, C_HAZARD, sp);
      g_cfg.accshotgun  = row_switch(cursor, sp ? "Shotgun" : "Shotgun (Unavailable)",
                                     g_cfg.accshotgun, C_HAZARD, sp);
      row_hint("Pellets land in one hole.");
      g_cfg.accexpander = row_switch(cursor, sp ? "Expander" : "Expander (Unavailable)",
                                     g_cfg.accexpander, C_HAZARD, sp);

      // WEAPON SPEED rows are always live: the write goes through playerstate.cpp, which answers NULL (not a fault)
      // without the struct. The value goes back through firerate_rate_clamped so a drag settles on the 50-step stops
      // and a hand-edited ini cannot hold an unsittable rate.
      draw_group(cursor, "WEAPON SPEED");
      g_cfg.firerate = firerate_rate_clamped(
          row_slider(cursor, "Fire rate", g_cfg.firerate,
                     FIRERATE_MIN, FIRERATE_MAX, "%", true, FIRERATE_DEFAULT));
      row_hint("Compresses every frame that is not a shot.");
      g_cfg.switchspeed = firerate_rate_clamped(
          row_slider(cursor, "Weapon switch speed", g_cfg.switchspeed,
                     FIRERATE_MIN, FIRERATE_MAX, "%", true, FIRERATE_DEFAULT));
      row_hint("Faster lowering and raising when you switch");

      // AUTOMATIC SWITCHING is the engine's own cl_weaponswitch (autoswitch.h): no hook. Two rows over one int; the
      // first is THREE-STATE, as bits 1 and 4 are one setting (4 alone reads "never switch on pickup") and independent
      // switches could write that. Bit 2 is separate. Each row read-modify-writes.
      draw_group(cursor, "AUTOMATIC SWITCHING");
      {
        const EkCvar* const ws = cvars_find("cl_weaponswitch");
        const bool haveWs = cvar_row_available(ws);
        int mask = 0;
        if (haveWs) cvar_get_int("cl_weaponswitch", &mask);

        const int wasPickup = wswitch_pickup_state(mask);
        const int isPickup = row_dropdown(cursor, haveWs ? "Switch on pickup"
                                                         : "Switch on pickup (Unavailable)",
                                          wasPickup, EK_PICKUP_STATES,
                                          wswitch_pickup_name, haveWs);
        if (haveWs && isPickup != wasPickup)
          cvar_set_int("cl_weaponswitch", wswitch_with_pickup(mask, isPickup), &mask);
        row_hint("Better is by the game's own weapon order.");

        const int wasEmpty = wswitch_bit_on(mask, EK_WSWITCH_EMPTY);
        const int isEmpty = row_switch(cursor, haveWs ? "Switch when empty"
                                                      : "Switch when empty (Unavailable)",
                                       wasEmpty, C_HAZARD, haveWs);
        if (haveWs && isEmpty != wasEmpty)
          cvar_set_int("cl_weaponswitch", wswitch_with_bit(mask, EK_WSWITCH_EMPTY, isEmpty), &mask);
        row_hint("Falls back to another weapon when this one runs dry.");
      }
      break;
    }
    case TAB_VIEW: {
      // THE FIELD OF VIEW is the engine's own cvar (fov.h). It has its own Unavailable: `fov` must be reachable and
      // writable through the registry, which a name-first search already lost once on this port.
      draw_group(cursor, "FIELD OF VIEW");
      const bool fovOk = fov_available();
      g_cfg.fovon = row_switch(cursor, fovOk ? "Custom field of view"
                                             : "Custom field of view (Unavailable)",
                               g_cfg.fovon, C_GREEN, fovOk);
      row_hint("Writes the game's own fov. Off gives your value back.");
      g_cfg.fov = row_slider_sub(cursor, "Field of view", g_cfg.fov,
                                 EK_FOV_MIN, EK_FOV_MAX, "",
                                 fovOk && g_cfg.fovon != 0, EK_FOV_DEFAULT);
      row_hint("Wider than the game's own menu allows. 90 is stock.");

      // THE FREE CAMERA never moves the player: the pose is substituted around one drawrooms call and taken back out
      // (camera.h). Unavailable until the detour is in and a frame proved the renderer is Polymer.
      draw_group(cursor, "FREE CAMERA");
      const bool camOk = camera_available();
      g_cfg.freecamon = row_switch(cursor, camOk ? "Free camera"
                                                 : "Free camera (Unavailable)",
                                   g_cfg.freecamon, C_GREEN, camOk);
      row_hint("Fly a camera away from Duke. He stays where he is.");
      g_cfg.freecamspeed =
          row_slider_sub(cursor, "Flight speed", g_cfg.freecamspeed,
                         FREECAM_SPEED_MIN, FREECAM_SPEED_MAX, "%",
                         camOk && g_cfg.freecamon, FREECAM_SPEED_DEFAULT);
      row_hint("100% matches a running player.");
      // THE WHOLE MAP has its own Unavailable: the sweep needs four more located functions than the camera, so it can
      // refuse where the camera works.
      const bool sweepOk = camera_wholemap_available();
      g_cfg.freecamwholemap =
          row_switch_sub(cursor, sweepOk ? "Draw the whole map"
                                         : "Draw the whole map (Unavailable)",
                         g_cfg.freecamwholemap, C_GREEN,
                         sweepOk && g_cfg.freecamon);
      // The hint says what this build actually adds: geometry always, the actors when the sprite pass located its
      // arrays, and the sky from outside the map when the call-site redirect went in. The sky is its own term (it once
      // required the sprite pass).
      const bool skyOk = camera_sky_available();
      const bool actorsOk = camera_sprites_available();
      row_hint(skyOk ? (actorsOk
                            ? "Adds the sky and the skipped sectors, actors and all."
                            : "Adds the sky and the skipped sectors. No actors in them.")
                     : (actorsOk
                            ? "Adds the skipped sectors and their actors."
                            : "Adds the skipped sectors. No actors in them."));
      // REMOVE FOG has its own Unavailable (one more located global than the camera). The hint says what the player
      // SEES, not which field moved.
      const bool fogOk = camera_fog_available();
      g_cfg.freecamfog =
          row_switch_sub(cursor, fogOk ? "Remove fog"
                                       : "Remove fog (Unavailable)",
                         g_cfg.freecamfog, C_GREEN, fogOk && g_cfg.freecamon);
      row_hint("Every sector keeps its own light, however far away.");
      // FOLLOW TELEPORTERS has its own Unavailable (it needs two sprite-chain arrays, camera.cpp); without it the
      // camera comes out in the wrong half of a pair.
      const bool tportOk = camera_transport_available();
      g_cfg.freecamtransport =
          row_switch_sub(cursor, tportOk ? "Follow teleporters"
                                         : "Follow teleporters (Unavailable)",
                         g_cfg.freecamtransport, C_GREEN,
                         tportOk && g_cfg.freecamon);
      row_hint("The camera is carried across teleporters and water.");
      g_cfg.freecamshowplayer =
          row_switch_sub(cursor, "Show player", g_cfg.freecamshowplayer,
                         C_GREEN, g_cfg.freecamon);
      row_hint("Marks your own body, so you can find it again.");
      g_cfg.freecamdebug =
          row_switch_sub(cursor, "Debug readout", g_cfg.freecamdebug, C_GREEN,
                         g_cfg.freecamon);
      row_hint("Shows the camera's position and angle while flying.");
      // A sub-row of the camera: the marker draws from the camera's pose. The teleport itself has no row: it is the
      // bound key's action, and gating a way out of the camera could only strand somebody.
      g_cfg.freecamteleportmarker = row_switch_sub(cursor, "Teleport marker",
                                                   g_cfg.freecamteleportmarker,
                                                   C_GREEN, g_cfg.freecamon != 0);
      row_hint("Where the teleport key would put you. Bind it first.");
      // SCREENSHOT MODE has its own Unavailable: the weapon and crosshair cvars must be reachable and writable. The
      // level stats, status bar and scuba mask are bonuses and deliberately not terms: each rides on a redirect a build
      // could refuse, and the row should not go down for them. The log says which went.
      const bool shotOk = shot_available();
      g_cfg.freecamshot =
          row_switch_sub(cursor, shotOk ? "Screenshot mode"
                                        : "Screenshot mode (Unavailable)",
                         g_cfg.freecamshot, C_GREEN,
                         shotOk && g_cfg.freecamon);
      row_hint("Hides the gun, crosshair, stats, bar and scuba mask.");
      row_bind(cursor, "Camera key", BIND_ROW_CAMERA);
      row_hint("Press to fly. W A S D, space, ctrl, shift and alt fly it.");
      row_bind(cursor, "Teleport key", BIND_ROW_TELEPORT);
      row_hint("Press while flying to put Duke where you are looking.");
      row_bind(cursor, "Copy view key", BIND_ROW_CAMCOPY);
      row_hint("Copies where the camera is looking to the clipboard.");
      row_bind(cursor, "Paste view key", BIND_ROW_CAMPASTE);
      row_hint("Flies the camera to a viewpoint on the clipboard.");
      break;
    }
    case TAB_SETTINGS: {
      // MOUSE BUTTONS are input configuration. The game binds the thumb buttons but hides them: X1 is its index 3, X2
      // its 6, while the wheel takes rows labelled 4 and 5. The mod writes no binding table, it ORs the action's bit
      // into the input record while the button is held (mousebind.h). Unavailable until the scan has localInput.
      draw_group(cursor, "MOUSE BUTTONS");
      const bool mb = mousebind_ready();
      g_cfg.mouse4action =
          row_dropdown(cursor, mb ? "Mouse 4 does" : "Mouse 4 does (Unavailable)",
                       g_cfg.mouse4action, MB_ACTION_COUNT, mousebind_action_name, mb);
      row_hint("The game's own menu numbers these 3 and 6, not 4 and 5.");
      g_cfg.mouse5action =
          row_dropdown(cursor, mb ? "Mouse 5 does" : "Mouse 5 does (Unavailable)",
                       g_cfg.mouse5action, MB_ACTION_COUNT, mousebind_action_name, mb);
      row_hint("Held. Quick kick repeats; the other five fire once.");

      draw_group(cursor, "HOTKEYS");
      // The panel's own key: a hand-edited ini can leave an unmappable value; input.cpp falls back to Insert while the
      // row shows what is stored. BIND_ROW_CAMERA and BIND_ROW_TELEPORT have their rows on the View tab beside what
      // they act on.
      row_bind(cursor, "Panel key", BIND_ROW_MENU);
      row_hint("The key that opens and closes this panel.");
      row_bind(cursor, "Aimbot button", BIND_ROW_AIM);
      row_hint("Held to aim when Hold key is on.");

      // AUTOSAVE: the first three rows store nothing. cl_autosave, cl_autosavedeletion and cl_maxautosaves are
      // registered cvars (osdcmds.cpp:1601-1603), so rows read and write them live; a copy in ekmod.ini would give two
      // answers. The fourth is this port's timer, World Tour's rule, through the engine's own autosave request.
      draw_group(cursor, "AUTOSAVE");
      {
        int autos = 0;
        const bool haveAuto = cvar_get_int("cl_autosave", &autos);
        const int nAuto = row_switch(cursor, haveAuto ? "Checkpoint autosave"
                                                      : "Checkpoint autosave (Unavailable)",
                                     autos, C_GREEN, haveAuto);
        if (haveAuto && nAuto != autos) cvar_set_int("cl_autosave", nAuto, &autos);
        row_hint("The game's own save when you reach a checkpoint.");

        int del = 0;
        const bool haveDel = cvar_get_int("cl_autosavedeletion", &del);
        const int nDel = row_switch(cursor, haveDel ? "Delete old autosaves"
                                                    : "Delete old autosaves (Unavailable)",
                                    del, C_GREEN, haveDel);
        if (haveDel && nDel != del) cvar_set_int("cl_autosavedeletion", nDel, &del);
        row_hint("Keeps the newest few and removes the rest.");

        // Gated on the row above as the game's menu does (menus.cpp:2731). The track ends are the record's own
        // vmin/vmax so the row cannot offer a refused value; with no record a disabled 0..1 track draws. No reset glyph
        // or typed field (EK_NONE): osdcvardata_t has no registered default, and this panel will not guess one.
        const EkCvar* const mx = cvars_find("cl_maxautosaves");
        const bool haveMax = cvar_row_available(mx);
        int keep = 0;
        if (haveMax) cvar_get_int("cl_maxautosaves", &keep);
        const int keepLo = haveMax ? mx->vmin : 0;
        const int keepHi = haveMax ? mx->vmax : 1;
        char keepText[24], keepReserve[24];
        sprintf_s(keepText, "%d", keep);
        sprintf_s(keepReserve, "%d", keepHi);
        const int nKeep = row_slider_at(cursor, haveMax ? "Autosaves kept"
                                                        : "Autosaves kept (Unavailable)",
                                        keep, keepLo, keepHi, keepText, keepReserve,
                                        haveMax && del != 0, SUB_INDENT, C_BONEDIM,
                                        EK_NONE, keep);
        if (haveMax && nKeep != keep) cvar_set_int("cl_maxautosaves", nKeep, &keep);
        row_hint("How many are kept before the oldest one goes.");

        // The timer is the only row here backed by ekmod.ini. Unavailable until autosave.cpp found the request; the
        // scan runs on the first frame ahead of this panel, so the answer is never provisional.
        const bool ts = autosave_ready();
        g_cfg.autosavemins = row_slider(cursor, ts ? "Autosave timer"
                                                   : "Autosave timer (Unavailable)",
                                        g_cfg.autosavemins,
                                        AUTOSAVE_MINS_MIN, AUTOSAVE_MINS_MAX, " min",
                                        ts, AUTOSAVE_MINS_DEFAULT);
        row_hint(ts ? "0 is off. Each one is a new autosave."
                    : "The game's autosave was not found. See ekmod.log.");
      }

      draw_group(cursor, "PANEL");
      {
        // The range is what this screen can apply; knob and readout show the applied scale. The reserve is the literal
        // "200%". Drawing is not a write: a stored value above the ceiling is kept, and only a drag stores, so a taller
        // screen returns the player's number.
        const int ceiling = ui_scale_max_for(screenH, menu_ceiling_height(), PANEL_VINSET);
        char scaleText[16];
        sprintf_s(scaleText, "%d%%", settled);
        const int v = row_slider_at(cursor, "UI scale", settled, UI_SCALE_MIN, ceiling,
                                    scaleText, "200%", true, 0, C_BONE,
                                    EK_INT, UI_SCALE_DEFAULT);
        if (v != settled) g_cfg.uiscale = v;
      }
      g_cfg.animations = row_switch(cursor, "Panel animations", g_cfg.animations, C_GREEN, true);
      row_hint("Fades and slides in the panel");

      draw_group(cursor, "DIAGNOSTICS");
      g_cfg.debuglog = row_switch(cursor, "Debug log", g_cfg.debuglog, C_GREEN, true);
      row_hint("Per-feature detail in ekmod.log. Off by default.");
      g_cfg.tracers = row_switch(cursor, "Shot tracers", g_cfg.tracers, C_GREEN, true);
      row_hint("Draws each shot: our predicted ray and the real impact.");
      g_cfg.perfzones = row_switch(cursor, "Frame timings", g_cfg.perfzones,
                                   C_GREEN, true);
      row_hint("Where each millisecond goes. Also written to the log.");
      g_cfg.perftable = row_switch_sub(cursor, "Show the table",
                                       g_cfg.perftable, C_GREEN,
                                       g_cfg.perfzones != 0);
      row_hint("The table costs more than the timings it reports.");
      g_cfg.perfdetail2 = row_switch_sub(cursor, "Per sector and wall",
                                         g_cfg.perfdetail2, C_GREEN,
                                         g_cfg.perfzones != 0);
      row_hint("Times each engine call. Use while hunting, not always.");
      break;
    }
  }

  // A tab that did not fit says so once a session, before the tab can change so `s_tab` names the dropper (PANEL_MIN_H
  // 740 once cost two rows silently).
  if (s_rowsDropped && !s_rowsDroppedLogged) {
    s_rowsDroppedLogged = true;
    ek_log("menu: tab %d ran out of room at panel %dx%d -- rows not drawn",
           s_tab, box.w, box.h);
  }

  if (railTab >= 0) menu_set_tab(railTab);

  // The resize grip: three diagonals in the lower-right triangle of panel_grip_rect, drawn after content (never covered
  // by a row) and before the tooltip. C_BONEFNT at rest, C_BONEDIM only under the cursor.
  {
    const UiRect g = panel_grip_rect(box);
    const unsigned gc = pres_colour(gripHit ? C_BONEDIM : C_BONEFNT);
    for (int i = 1; i <= 3; i++) {
      const int off = i * 4;
      canvas_line(g.x + off, g.y + g.h - 1, g.x + g.w - 1, g.y + off, gc);
    }
  }

  menu_dropdown_draw();

  // The tooltip, last so it is on top, stepped once per frame: exactly one row can be rested on. s_hoverRowKey is -1
  // off any row. Either button puts it away.
  s_tooltip = tooltip_step(s_tooltip, s_hoverRowKey, g_ui.pressed || g_ui.rpressed,
                           s_pres.lastMs, anim_style(), s_frameDt);
  const char* const hint = s_glyphHint ? s_glyphHint : s_hoverHint;
  s_tooltipText = nullptr;
  if (s_tooltip.shown && s_tooltip.alpha > 0.0 && hint && s_pres.openTarget) {
    const UiRect b = tooltip_box_place(g_ui.mx, g_ui.my,
                                       canvas_text_width(hint, FONT_SMALL),
                                       font_height[FONT_SMALL], logicalW, logicalH);
    canvas_round_rect(b.x, b.y, b.w, b.h, TOOLTIP_R,
                      alpha_scale(C_WELL, s_tooltip.alpha));
    canvas_round_rect_outline(b.x, b.y, b.w, b.h, TOOLTIP_R, 1,
                              alpha_scale(C_RAISED, s_tooltip.alpha));
    canvas_text(b.x + TOOLTIP_PAD, b.y + TOOLTIP_PAD, hint,
                alpha_scale(C_BONE, s_tooltip.alpha), FONT_SMALL);
    s_tooltipText = hint;
  }

  // A left press nothing claimed is a panel grab (continues via panel_drag_step), decided last since only then is
  // "unclaimed" knowable; the press frame moves nothing. `!s_dragLive` makes the grip win; `g_ui.down` and `openTarget`
  // stop a stale or closing-frame press arming a drag.
  if (g_ui.pressed && g_ui.down && !s_pressClaimed && !s_dragLive &&
      s_pres.openTarget && ui_hit(p, g_ui.mx, g_ui.my)) {
    s_drag.phase = PDRAG_MOVE;
    s_drag.start = box;
    s_drag.grabDx = g_ui.mx - box.x;
    s_drag.grabDy = g_ui.my - box.y;
    s_dragBox = box;
    s_dragLive = true;
  }

  g_ui.mx = deviceMx;
  g_ui.my = deviceMy;
  canvas_set_scale(UI_SCALE_DEFAULT);
}
