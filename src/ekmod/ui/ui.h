#pragma once
// clamp_int and clamp_ll_to_int, which the UI SCALE block below uses; canvas.cpp includes ui.h directly, so they must
// arrive with this header.
#include "common.h"

struct UiRect { int x, y, w, h; };

struct UiInput {
  int  mx, my;      // cursor position in overlay pixels
  bool down;        // left button currently held
  bool pressed;     // left button went down this frame (edge)
  // Right button, carried by the same rules as the left: input.cpp's poll hook sets rdown, input_begin_frame derives
  // rpressed via ui_press_edge.
  bool rdown;       // right button currently held
  bool rpressed;
};

bool   ui_hit(const UiRect& r, int x, int y);
UiRect ui_row(UiRect& cursor, int height, int gap);
int    ui_slider_value(const UiRect& track, int mx, int lo, int hi);
int    ui_slider_x(const UiRect& track, int value, int lo, int hi);

bool   ui_press_edge(bool down, bool wasDown);

bool   ui_key_is_initial_press(bool repeat);

// Value for a save/force/restore toggle: never invents a default; the caller must pass one it observed.
int    ui_saved_toggle_value(bool panelOwnsInput, int openValue, int savedValue);

bool   ui_bytes_any_nonzero(const unsigned char* bytes, int count);

// A disabled row is inert, not dimmed: a click on it must not change a setting.
bool   ui_row_accepts_click(bool enabled, bool buttonState, bool hit);

// Logical-pixel layout, multiplied by the canvas. Floor 75: at 50 a switch is 10px.
const int UI_SCALE_MIN     = 75;
const int UI_SCALE_MAX     = 200;
const int UI_SCALE_DEFAULT = 100;

inline int ui_scale_clamped(int pct) { return clamp_int(pct, UI_SCALE_MIN, UI_SCALE_MAX); }

inline int ekm_scale_needs_atlas(int pct) {
  return ui_scale_clamped(pct) != UI_SCALE_DEFAULT ? 1 : 0;
}

// Rounds half away from zero (truncation erases 1px bevels) and widens before multiplying.
inline int ui_scale_px(int px, int pct) {
  const long long p = (long long)ui_scale_clamped(pct);
  const long long v = (long long)px * p;
  return clamp_ll_to_int(v >= 0 ? (v + 50) / 100 : (v - 50) / 100);
}

inline int ui_unscale_px(int px, int pct) {
  const long long p = (long long)ui_scale_clamped(pct);
  const long long v = (long long)px * 100;
  return clamp_ll_to_int(v >= 0 ? (v + p / 2) / p : (v - p / 2) / p);
}

// Largest scale at which the panel height fits the screen (raising it clips rows). Snapped down to a multiple of 5.
inline int ui_scale_max_for(int screenH, int panelLogicalH, int insetLogical) {
  const long long total = (long long)panelLogicalH + 2LL * (long long)insetLogical;
  if (total <= 0) return UI_SCALE_MAX;
  long long pct = ((long long)screenH * 100 + 49) / total;
  pct -= pct % 5;
  return clamp_int(clamp_ll_to_int(pct), UI_SCALE_MIN, UI_SCALE_MAX);
}

// Which scale the panel draws at this frame: the settled one, except while the left button is held, when it keeps
// whatever was already applied: row_slider_at re-tests its grab box every frame, so a layout moving with the value
// would slide the row out from under the cursor and slam the value between the ends.
inline int ui_scale_effective(int stored, int applied, bool dragging) {
  return dragging ? applied : stored;
}
