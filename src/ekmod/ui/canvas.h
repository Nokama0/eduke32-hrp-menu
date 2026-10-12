// The palette above the ESP entries matches Megaton's canvas.h; everything below is this port's own.
#pragma once
const unsigned C_PANEL   = 0xF014171A;
const unsigned C_RECESS  = 0xF01C2127;
const unsigned C_RAISED  = 0xFF333C46;
const unsigned C_BEVELHI = 0xFF48535F;
const unsigned C_BEVELLO = 0xFF0A0C0E;
const unsigned C_HAZARD  = 0xFFF2B01E;
const unsigned C_RED     = 0xFFD8402F;
const unsigned C_BLUE    = 0xFF3C9BD6;
const unsigned C_GREEN   = 0xFF6FBF5B;
const unsigned C_BONE    = 0xFFE8E4DA;
const unsigned C_BONEDIM = 0xFF9AA1A8;
const unsigned C_BONEFNT = 0xFF666E77;
const unsigned C_WELL      = 0xFF0E1114;
const unsigned C_HAZARDDIM = 0xFF8A6512;
const unsigned C_AIM       = 0xFFC061E8;  // aim marker: violet, unused elsewhere
const unsigned C_PLAYER    = 0xFF2FE0D0;  // own-body marker: cyan
const unsigned C_SETTINGS  = 0xFFB8A9E0;

// Combination tint, a VOLUME around the lock's model, pushed toward saturation to survive a dark wall. Only one layer
// blends over the switch body: the far face is depth-rejected (GL_LEQUAL).
const unsigned C_TINT_BOX_FLIP  = 0x7660FF60;
const unsigned C_TINT_BOX_LEAVE = 0x76FF5050;

// Lower alpha than the tint: 2-3 room panels overlap along one sightline.
const unsigned C_SECRET_VOLUME = 0x40F2B01E;

const unsigned C_TELEPORT_MARK = 0x402FE0D0;

// Tracers must differ from each other and from the markers' six hues (red 15, yellow 43, green 108, cyan 175, blue 203,
// violet 280): the exact ray is near white, the actual impact rose at about 340.
const unsigned C_TRACE_PRED = 0xFFF2F4FF;
const unsigned C_TRACE_REAL = 0xFFFF3E8C;

// Glass style colours (menuanim == 2 only): what a surface is seen through.
const unsigned C_GLASS_TINT = 0x99141C26;
const unsigned C_GLASS_RIM  = 0xB0A8C8E0;
const unsigned C_GLASS_TOP  = 0x40D8E8F8;
const unsigned C_GLASS_GLOW = 0x66A8D8FF;

// True when canvas_hazard's stripe geometry is well formed: a non-positive period never advances the loop (glBegin left
// open).
inline bool canvas_hazard_params_ok(int w, int h, int period) {
  return w > 0 && h > 0 && period > 0;
}

// Keycards keep their colour in the NAME so a red box still reads "enemy".
inline unsigned canvas_esp_colour(bool enemy) { return enemy ? C_RED : C_BLUE; }

inline unsigned canvas_esp_secret_colour() { return C_HAZARD; }

inline unsigned canvas_notice_colour() { return C_HAZARD; }

inline unsigned canvas_aim_colour() { return C_AIM; }

inline unsigned canvas_player_colour() { return C_PLAYER; }

// Two tracer functions, not one with a flag: a guess must not be drawn in a certainty's colour.
inline unsigned canvas_tracer_predicted_colour() { return C_TRACE_PRED; }
inline unsigned canvas_tracer_actual_colour() { return C_TRACE_REAL; }

inline unsigned canvas_esp_explodable_colour() { return C_HAZARD; }

inline unsigned canvas_esp_switch_colour() { return C_BLUE; }

inline unsigned canvas_health_colour(int hp, int maxhp) {
  if (maxhp <= 0 || hp <= 0) return C_RED;
  // Widened before the multiply: hp is a short that may be unbounded.
  const long long pct = (long long)hp * 100 / maxhp;
  if (pct > 50) return C_GREEN;
  if (pct > 25) return C_HAZARD;
  return C_RED;
}

inline int canvas_bar_fill(int total, int hp, int maxhp) {
  if (total <= 0 || maxhp <= 0 || hp <= 0) return 0;
  if (hp >= maxhp) return total;
  return (int)((long long)total * hp / maxhp);
}

// Alpha taps must sum exactly to the tint alpha. Cumulative rounding, not aTint/taps plus remainder, which goes
// negative on the last tap (aTint=5, taps=9) and wraps to a wrong colour byte.
inline int canvas_tap_alpha(int aTint, int taps, int i) {
  if (taps <= 0 || i < 0 || i >= taps) return 0;
  if (aTint < 0) aTint = 0;
  if (aTint > 255) aTint = 255;
  const int upto = (int)((double)aTint * (double)(i + 1) / (double)taps + 0.5);
  const int prev = (int)((double)aTint * (double)i / (double)taps + 0.5);
  return upto - prev;
}

inline unsigned canvas_with_alpha(unsigned rgba, int alpha) {
  if (alpha < 0) alpha = 0;
  if (alpha > 255) alpha = 255;
  return (rgba & 0x00FFFFFFu) | ((unsigned)alpha << 24);
}

// Second canvas-wide mode: the open motion moves every drawn pixel while the LOGICAL layout hit testing uses stays put;
// d = ((p - c) * scale + c) + offset after the UI scale, identity exact. Reset by canvas_begin.
struct CanvasTransform { int dx, dy, cx, cy; double scale; };
void canvas_set_transform(int dx, int dy, double scale, int cx, int cy);

#ifdef NO_GL
// Test-build-only recording of what was drawn.
const int CANVAS_RECORD_MAX = 512;
enum { CANVAS_OP_RECT = 0, CANVAS_OP_BEVEL, CANVAS_OP_HAZARD,
       CANVAS_OP_LINE, CANVAS_OP_TEXT,
       CANVAS_OP_ROUNDRECT, CANVAS_OP_GRADIENT, CANVAS_OP_GLOW,
       CANVAS_OP_CAPTURE, CANVAS_OP_BACKDROP,
       CANVAS_OP_ROUNDOUTLINE };
struct CanvasOp {
  int kind;
  int x, y, w, h;
  unsigned colour;
  int font;
  char text[64];
};
void canvas_record_begin();
void canvas_record_end();
int  canvas_record_count();
bool canvas_record_overflowed();  // true if ops were dropped: never ignore it
const CanvasOp* canvas_record_op(int i);
const CanvasOp* canvas_record_find_text(const char* s);
const CanvasOp* canvas_record_find_kind(int kind, unsigned colour);
CanvasTransform canvas_record_transform();
#endif

// Coordinates are LOGICAL; the canvas scales on the way out. menu_draw restores UI_SCALE_DEFAULT before returning,
// since the ESP draws through the same primitives and a standing factor would scale world markers. canvas_text_width
// stays LOGICAL or the factor applies twice.
void canvas_set_scale(int pct);
int  canvas_scale();

void canvas_begin(int w, int h);
void canvas_end();
void canvas_rect(int x, int y, int w, int h, unsigned rgba);
void canvas_bevel(int x, int y, int w, int h, unsigned fill, unsigned hi, unsigned lo);
void canvas_line(int x0, int y0, int x1, int y1, unsigned rgba);
void canvas_hazard(int x, int y, int w, int h, unsigned stripe, unsigned ground, int period);
int canvas_text(int x, int y, const char* s, unsigned rgba, int font);
int canvas_text_width(const char* s, int font);

void canvas_round_rect(int x, int y, int w, int h, int r, unsigned rgba);
// A rounded outline as its own primitive: a filled round rect under a translucent slab does not read as an edge.
void canvas_round_rect_outline(int x, int y, int w, int h, int r, int thickness,
                               unsigned rgba);
void canvas_gradient_rect(int x, int y, int w, int h, unsigned top, unsigned bottom);
void canvas_glow_rect(int x, int y, int w, int h, unsigned rgba);
bool canvas_backdrop_capture(int x, int y, int w, int h);
void canvas_backdrop_draw(int x, int y, int w, int h, int r, unsigned tint,
                          double wobbleAmp, double wobblePhase, int blurTaps, int blurRadius);
void canvas_backdrop_release();

// The atlas texel a flat fill samples, for callers queueing their own triangles; false when no atlas could be built.
bool canvas_white_texel(float* u, float* v);
