#include "canvas.h"
#include "font.h"
#include "ui.h"

// World Tour's canvas.cpp with its backend replaced by Dear ImGui's ImDrawList, nothing else. The entry point lives
// HERE, not in canvas.h (verbatim across ports): `void canvas_set_drawlist(ImDrawList* dl);`, set once a frame;
// primitives are no-ops until then and canvas_end clears it. The NO_GL build has no ImGui.

// Outside NO_GL: both builds scale, and a recorder logging logical coordinates would pin a panel nobody sees.
static int s_scale = UI_SCALE_DEFAULT;

void canvas_set_scale(int pct) { s_scale = ui_scale_clamped(pct); }
int  canvas_scale()            { return s_scale; }

static int    s_txDx = 0, s_txDy = 0;
static int    s_txCx = 0, s_txCy = 0;
static double s_txScale = 1.0;
static bool   s_txIdentity = true;

void canvas_set_transform(int dx, int dy, double scale, int cx, int cy) {
  s_txDx = dx; s_txDy = dy; s_txCx = cx; s_txCy = cy;
  s_txScale = scale;
  s_txIdentity = (dx == 0 && dy == 0 && scale == 1.0);
}

static int tx_round(double v) {
  return clamp_double_to_int(v < 0.0 ? v - 0.5 : v + 0.5);
}

static int tx_dev_x(int d) {
  if (s_txIdentity) return d;
  const double c = (double)ui_scale_px(s_txCx, s_scale);
  return tx_round(((double)d - c) * s_txScale + c + (double)ui_scale_px(s_txDx, s_scale));
}
static int tx_dev_y(int d) {
  if (s_txIdentity) return d;
  const double c = (double)ui_scale_px(s_txCy, s_scale);
  return tx_round(((double)d - c) * s_txScale + c + (double)ui_scale_px(s_txDy, s_scale));
}
static int tx_dev_len(int d) {
  if (s_txIdentity) return d;
  return tx_round((double)d * s_txScale);
}

static int dev_x(int x)   { return tx_dev_x(ui_scale_px(x, s_scale)); }
static int dev_y(int y)   { return tx_dev_y(ui_scale_px(y, s_scale)); }
static int dev_len(int v) { return tx_dev_len(ui_scale_px(v, s_scale)); }

#ifndef NO_GL
#include <math.h>
#include <string.h>
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include "../log.h"
#include "fontgen.h"
#include "fontpack.h"

static int s_devW = 0, s_devH = 0;

static ImDrawList* s_dl = 0;

void canvas_set_drawlist(ImDrawList* dl) { s_dl = dl; }

// font_data.cpp's coverage is packed into ImGui's OWN atlas as a custom rect, so glyphs and flat fills (ImGui's white
// texel) share one texture and draw call. Two rects, not one, chosen per string: overlay.cpp draws the ESP at 100% and
// the panel at the applied scale inside ONE canvas frame.

// Reserved once at a rung that cannot overflow: custom rects allocate when the atlas is built, and growing one means a
// repack. 1024x1024 is FONTPACK_LADDER rung 3; 200% needs at most rung 2. Costs 4 MB RGBA32.
static const int RUN_RECT_RUNG = 3;

static ImFontAtlas*      s_atlas  = 0;
static ImFontAtlasRectId s_rectId = ImFontAtlasRectId_Invalid;
static ImFontAtlasRectId s_runRectId = ImFontAtlasRectId_Invalid;
static int s_atlasFailed = 0;
static int s_runRectPct  = 0;  // the scale whose pixels are IN the scaled rect; 0 = none yet

// Coverage into one of our rects in the texture's format (imgui_impl_opengl3 asserts RGBA32; Alpha8 is a row copy). The
// WHOLE rect is written so a previous scale's leftovers are not uploaded; a too-large source is refused, not clipped.
static bool atlas_write_pixels(ImFontAtlas* atlas, const ImFontAtlasRect& r,
                               const unsigned char* src, int srcW, int srcH) {
  ImTextureData* tex = atlas->TexData;
  if (!tex || !tex->Pixels) {
    ek_log("canvas: ImGui's atlas has no pixel buffer; the panel will not draw");
    return false;
  }
  if (!src || srcW <= 0 || srcH <= 0 || srcW > (int)r.w || srcH > (int)r.h) {
    ek_log("canvas: a %dx%d atlas does not fit the %dx%d rect reserved for it",
           srcW, srcH, (int)r.w, (int)r.h);
    return false;
  }
  for (int y = 0; y < (int)r.h; y++) {
    const unsigned char* line = (y < srcH) ? src + (size_t)y * (size_t)srcW : 0;
    const int copied = line ? srcW : 0;
    unsigned char* dst = (unsigned char*)tex->GetPixelsAt(r.x, r.y + y);
    if (tex->Format == ImTextureFormat_Alpha8) {
      if (copied) memcpy(dst, line, (size_t)copied);
      memset(dst + copied, 0, (size_t)((int)r.w - copied));
    } else {
      ImU32* out = (ImU32*)dst;
      for (int x = 0; x < copied; x++) out[x] = IM_COL32(255, 255, 255, line[x]);
      for (int x = copied; x < (int)r.w; x++) out[x] = 0;
    }
  }
  ImFontAtlasTextureBlockQueueUpload(atlas, tex, r.x, r.y, r.w, r.h);
  return true;
}

static bool ensure_atlas() {
  if (!ImGui::GetCurrentContext()) return false;
  ImFontAtlas* atlas = ImGui::GetIO().Fonts;
  if (!atlas) return false;
  if (atlas == s_atlas && s_rectId != ImFontAtlasRectId_Invalid) return true;
  if (s_atlasFailed) return false;
  // A rebuilt context owns a different atlas, so old rect ids are dropped.
  s_atlas  = 0;
  s_rectId = ImFontAtlasRectId_Invalid;
  s_runRectId = ImFontAtlasRectId_Invalid;
  s_runRectPct = 0;
  if (atlas->Locked) {
    static int said = 0;
    if (!said) {
      said = 1;
      ek_log("canvas: ImGui's atlas is locked; the panel will not draw");
    }
    return false;
  }
  ImFontAtlasRect r;
  const ImFontAtlasRectId id = atlas->AddCustomRect(FONT_ATLAS_W, FONT_ATLAS_H, &r);
  if (id == ImFontAtlasRectId_Invalid) {
    s_atlasFailed = 1;
    ek_log("canvas: no room in ImGui's atlas for a %dx%d block; the panel will not draw",
           FONT_ATLAS_W, FONT_ATLAS_H);
    return false;
  }
  if (!atlas_write_pixels(atlas, r, font_atlas, FONT_ATLAS_W, FONT_ATLAS_H)) {
    atlas->RemoveCustomRect(id);
    s_atlasFailed = 1;
    return false;
  }
  s_atlas  = atlas;
  s_rectId = id;
  ek_log("canvas: atlas %dx%d packed at (%d,%d) in ImGui's texture, %d fonts",
         FONT_ATLAS_W, FONT_ATLAS_H, (int)r.x, (int)r.y, FONT_COUNT);

  // The scaled rect is reserved together with the first. Not fatal: losing it costs crisp text at non-100 scales only.
  const int rw = FONTPACK_LADDER[RUN_RECT_RUNG].w;
  const int rh = FONTPACK_LADDER[RUN_RECT_RUNG].h;
  ImFontAtlasRect rr;
  const ImFontAtlasRectId rid = atlas->AddCustomRect(rw, rh, &rr);
  if (rid == ImFontAtlasRectId_Invalid) {
    ek_log("canvas: no room in ImGui's atlas for a %dx%d scaled-text block; "
           "text will be magnified instead of rasterised", rw, rh);
  } else {
    s_runRectId = rid;
    ek_log("canvas: scaled-text block %dx%d (ladder rung %d) reserved at (%d,%d)",
           rw, rh, RUN_RECT_RUNG, (int)rr.x, (int)rr.y);
  }
  return true;
}

// Re-read each frame: ImGui repacks its texture as it bakes fonts, copying our pixels but moving the origin and uv
// scale.
static int    s_activeResolved = 0;
static int    s_activeOk       = 0;
static int    s_rectX = 0, s_rectY = 0;
static int    s_runRectX = 0, s_runRectY = 0;
static int    s_runRectReady = 0;
static ImVec2 s_uvScale(0.0f, 0.0f);
static ImVec2 s_white(0.0f, 0.0f);

static bool atlas_resolve() {
  if (s_activeResolved) return s_activeOk != 0;
  s_activeResolved = 1;
  s_activeOk = 0;
  if (!ensure_atlas()) return false;
  ImFontAtlasRect r;
  if (!s_atlas->GetCustomRect(s_rectId, &r)) {
    // Rect unknown (rebuild or a new atlas in its place): pack again rather than draw from a stale origin.
    s_atlas  = 0;
    s_rectId = ImFontAtlasRectId_Invalid;
    if (!ensure_atlas()) return false;
    if (!s_atlas->GetCustomRect(s_rectId, &r)) return false;
  }
  s_rectX   = r.x;
  s_rectY   = r.y;
  s_runRectReady = 0;
  if (s_runRectId != ImFontAtlasRectId_Invalid) {
    ImFontAtlasRect rr;
    if (s_atlas->GetCustomRect(s_runRectId, &rr)) {
      s_runRectX = rr.x;
      s_runRectY = rr.y;
      s_runRectReady = 1;
    } else {
      s_runRectId  = ImFontAtlasRectId_Invalid;
      s_runRectPct = 0;
    }
  }
  s_uvScale = s_atlas->TexUvScale;
  s_white   = s_atlas->TexUvWhitePixel;
  s_activeOk = 1;
  return true;
}

// A scaled quad of a fixed bitmap only blows pixels up, so at any scale but 100 the faces are rasterised at that scale
// (fontgen.cpp) into the reserved rect and placed with device advances; 100% stays the baked atlas byte for byte.
// Rebuilt on mouse release, never mid-drag (menu.cpp holds the pre-drag scale: ui_scale_effective).
static FontAtlas s_run = { 0 };
static int s_runFailedPct = 0;

static bool runtime_ready() {
  return ekm_scale_needs_atlas(s_scale) && s_run.pct == s_scale && s_run.pixels != 0;
}

static int ladder_rung(int w, int h) {
  for (int i = 0; i < 6; i++)
    if (FONTPACK_LADDER[i].w == w && FONTPACK_LADDER[i].h == h) return i;
  return -1;
}

// CPU and GDI only, so canvas_text_width can call it during layout. The first return is before fontgen_build, so only a
// real refusal reaches s_runFailedPct (fontgen.cpp logged it); going back to 100 logs no warning.
static void runtime_prepare() {
  if (!ekm_scale_needs_atlas(s_scale)) return;
  if (s_scale == s_runFailedPct)       return;
  if (runtime_ready())                 return;
  fontgen_free(&s_run);
  if (!fontgen_build(s_scale, &s_run)) {
    s_runFailedPct = s_scale;
    return;
  }
  // The rung is logged on every rebuild: the reservation is rung 3 and 200% needs at most 2.
  ek_log("canvas: scaled atlas %d%% is %dx%d, ladder rung %d; the reserved rect is rung %d",
         s_run.pct, s_run.w, s_run.h, ladder_rung(s_run.w, s_run.h), RUN_RECT_RUNG);
}

// Whether THIS text samples the scaled atlas. Everything that measures or draws asks this one question, so a width and
// its glyphs never come from different atlases.
static bool runtime_live() {
  if (!ekm_scale_needs_atlas(s_scale)) return false;
  runtime_prepare();
  if (!runtime_ready())  return false;
  if (!atlas_resolve())  return false;
  if (!s_runRectReady)   return false;
  if (s_runRectPct == s_scale) return true;
  ImFontAtlasRect rr;
  if (!s_atlas->GetCustomRect(s_runRectId, &rr)) return false;
  if (!atlas_write_pixels(s_atlas, rr, s_run.pixels, s_run.w, s_run.h)) {
    s_runFailedPct = s_scale;
    return false;
  }
  s_runRectPct = s_scale;
  return true;
}

// canvas.h colours are 0xAARRGGBB; IM_COL32 is 0xAABBGGRR, so red and blue swap.
static ImU32 to_imgui(unsigned c) {
  return ((c & 0x00FF0000u) >> 16) |
         ( c & 0x0000FF00u)        |
         ((c & 0x000000FFu) << 16) |
         ( c & 0xFF000000u);
}

// A flat point on the white texel; ImDrawVert mirrors OvPoint, so the shapes are the other ports' code.
static ImDrawVert flat(float x, float y, unsigned rgba) {
  atlas_resolve();
  ImDrawVert p;
  p.pos = ImVec2(x, y);
  p.uv  = s_white;
  p.col = to_imgui(rgba);
  return p;
}

bool canvas_white_texel(float* u, float* v) {
  if (u) *u = 0.0f;
  if (v) *v = 0.0f;
  if (!atlas_resolve()) return false;
  if (u) *u = s_white.x;
  if (v) *v = s_white.y;
  return true;
}

static void cv_quad(const ImDrawVert& a, const ImDrawVert& b,
                    const ImDrawVert& c, const ImDrawVert& d) {
  if (!s_dl) return;
  s_dl->PrimReserve(6, 4);
  const unsigned int base = s_dl->_VtxCurrentIdx;
  s_dl->PrimWriteIdx((ImDrawIdx)base);
  s_dl->PrimWriteIdx((ImDrawIdx)(base + 1));
  s_dl->PrimWriteIdx((ImDrawIdx)(base + 2));
  s_dl->PrimWriteIdx((ImDrawIdx)base);
  s_dl->PrimWriteIdx((ImDrawIdx)(base + 2));
  s_dl->PrimWriteIdx((ImDrawIdx)(base + 3));
  s_dl->PrimWriteVtx(a.pos, a.uv, a.col);
  s_dl->PrimWriteVtx(b.pos, b.uv, b.col);
  s_dl->PrimWriteVtx(c.pos, c.uv, c.col);
  s_dl->PrimWriteVtx(d.pos, d.uv, d.col);
}

static void flat_quad(float x0, float y0, float x1, float y1,
                      float x2, float y2, float x3, float y3, unsigned rgba) {
  const ImDrawVert a = flat(x0, y0, rgba), b = flat(x1, y1, rgba);
  const ImDrawVert c = flat(x2, y2, rgba), d = flat(x3, y3, rgba);
  cv_quad(a, b, c, d);
}

// A rect already in device pixels: multi-part shapes scale their rect ONCE. Accepted: a right edge can differ by one
// from ui_scale_px(x + w), so abutting fills may show a hairline seam at non-integer scales (scaling the edge would
// make w depend on x).
static void fill_device(int x, int y, int w, int h, unsigned rgba) {
  flat_quad((float)x,       (float)y,
            (float)(x + w), (float)y,
            (float)(x + w), (float)(y + h),
            (float)x,       (float)(y + h), rgba);
}

void canvas_begin(int w, int h) {
  s_devW = w;
  s_devH = h;
  // Cleared every frame: menu_draw leaves its transform standing (canvas.h), and earlier draws must not inherit it.
  canvas_set_transform(0, 0, 1.0, 0, 0);
  s_activeResolved = 0;
  s_activeOk       = 0;
}

void canvas_end() {
  s_dl = 0;
}

void canvas_rect(int x, int y, int w, int h, unsigned rgba) {
  fill_device(dev_x(x), dev_y(y), dev_len(w), dev_len(h), rgba);
}

void canvas_line(int x0, int y0, int x1, int y1, unsigned rgba) {
  const int dx0 = dev_x(x0), dy0 = dev_y(y0);
  const int dx1 = dev_x(x1), dy1 = dev_y(y1);
  // GL draws a one-pixel line between pixel centres; this quad half a pixel either side rasterises identically when
  // axis aligned and within a pixel otherwise.
  const float ax = (float)dx0 + 0.5f, ay = (float)dy0 + 0.5f;
  const float bx = (float)dx1 + 0.5f, by = (float)dy1 + 0.5f;
  float nx = 0.5f, ny = 0.0f;
  const float ex = bx - ax, ey = by - ay;
  const float len = (float)sqrt((double)(ex * ex + ey * ey));
  if (len > 0.0f) { nx = -ey / len * 0.5f; ny = ex / len * 0.5f; }
  flat_quad(ax - nx, ay - ny, bx - nx, by - ny,
            bx + nx, by + ny, ax + nx, ay + ny, rgba);
}

void canvas_bevel(int x, int y, int w, int h, unsigned fill, unsigned hi, unsigned lo) {
  // Scaled once, then built: routing the edges through canvas_rect would scale twice.
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  int e = dev_len(1);
  if (e < 1) e = 1;
  fill_device(dx, dy, dw, dh, fill);
  fill_device(dx,          dy,          dw, e,          hi);
  fill_device(dx,          dy + dh - e, dw, e,          lo);
  fill_device(dx,          dy + e,      e,  dh - 2 * e, hi);
  fill_device(dx + dw - e, dy + e,      e,  dh - 2 * e, lo);
}

void canvas_hazard(int x, int y, int w, int h, unsigned stripe, unsigned ground, int period) {
  // Period scaled too, or stripes would bunch at 200%.
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  const int dp = dev_len(period);
  if (!canvas_hazard_params_ok(dw, dh, dp)) return;
  if (!s_dl) return;

  fill_device(dx, dy, dw, dh, ground);
  // 45 degree bars clipped to the strip; ImGui's clip rect is top-left origin, like every coordinate here.
  s_dl->PushClipRect(ImVec2((float)dx, (float)dy),
                     ImVec2((float)(dx + dw), (float)(dy + dh)), false);
  for (int i = -dh; i < dw + dh; i += dp * 2) {
    flat_quad((float)(dx + i),           (float)(dy + dh),
              (float)(dx + i + dp),      (float)(dy + dh),
              (float)(dx + i + dp + dh), (float)dy,
              (float)(dx + i + dh),      (float)dy, stripe);
  }
  s_dl->PopClipRect();
}

static int round_rect_segments(int r) {
  const int s = 3 + (r > 0 ? r / 2 : 0);
  return s < 3 ? 3 : s > 12 ? 12 : s;
}

// One tessellation shared by the rounded rect and the backdrop, or a hairline of untinted scene shows at the corners.
// 4*seg points, each arc inclusive; at dr == 0 it collapses to the plain rect.
static const int ROUND_FAN_MAX = 4 * 12;
static int round_fan_points(int dx, int dy, int dw, int dh, int dr, int seg,
                            float* px, float* py) {
  if (seg < 3) seg = 3;
  if (seg > 12) seg = 12;
  const double pi = 3.14159265358979323846;
  const float cx[4] = { (float)(dx + dr),      (float)(dx + dw - dr),
                        (float)(dx + dw - dr), (float)(dx + dr) };
  const float cy[4] = { (float)(dy + dr),      (float)(dy + dr),
                        (float)(dy + dh - dr), (float)(dy + dh - dr) };
  int n = 0;
  for (int c = 0; c < 4; c++) {
    const double a0 = (180.0 + 90.0 * c) * pi / 180.0;
    for (int i = 0; i < seg; i++) {
      const double a = a0 + (pi / 2.0) * (double)i / (double)(seg - 1);
      px[n] = cx[c] + (float)(dr * cos(a));
      py[n] = cy[c] + (float)(dr * sin(a));
      n++;
    }
  }
  return n;
}

static void round_rect_device(int dx, int dy, int dw, int dh, int dr, int seg,
                              unsigned rgba) {
  float px[ROUND_FAN_MAX], py[ROUND_FAN_MAX];
  const int n = round_fan_points(dx, dy, dw, dh, dr, seg, px, py);
  const ImDrawVert c = flat((float)dx + (float)dw * 0.5f,
                            (float)dy + (float)dh * 0.5f, rgba);
  if (!s_dl || n <= 0) return;
  s_dl->PrimReserve(3 * n, n + 1);
  const unsigned int base = s_dl->_VtxCurrentIdx;
  for (int i = 0; i < n; i++) {
    const int j = (i + 1 < n) ? i + 1 : 0;
    s_dl->PrimWriteIdx((ImDrawIdx)base);
    s_dl->PrimWriteIdx((ImDrawIdx)(base + 1 + (unsigned int)i));
    s_dl->PrimWriteIdx((ImDrawIdx)(base + 1 + (unsigned int)j));
  }
  s_dl->PrimWriteVtx(c.pos, c.uv, c.col);
  for (int i = 0; i < n; i++)
    s_dl->PrimWriteVtx(ImVec2(px[i], py[i]), c.uv, c.col);
}

// Never more than half the shorter side, or the arcs cross and the fan folds.
static int corner_radius_device(int dw, int dh, int r) {
  int dr = dev_len(r > 0 ? r : 0);
  const int half = (dw < dh ? dw : dh) / 2;
  if (dr > half) dr = half;
  return dr < 0 ? 0 : dr;
}

void canvas_round_rect(int x, int y, int w, int h, int r, unsigned rgba) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  // Segments from the device radius: a 200% corner in the same steps would be faceted.
  const int dr = corner_radius_device(dw, dh, r);
  round_rect_device(dx, dy, dw, dh, dr, round_rect_segments(dr), rgba);
}

// The rim is a real ring: this slab is translucent, so a filled rim would wash a third of the face. The two outlines
// share a corner centre (while dr >= t).
void canvas_round_rect_outline(int x, int y, int w, int h, int r, int thickness,
                               unsigned rgba) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  int t = dev_len(thickness > 0 ? thickness : 0);
  if (t < 1) t = 1;
  const int dr  = corner_radius_device(dw, dh, r);
  const int seg = round_rect_segments(dr);
  // Thinner than two rims the outlines would cross, so it degenerates into the filled shape.
  if (dw <= 2 * t || dh <= 2 * t) {
    round_rect_device(dx, dy, dw, dh, dr, seg, rgba);
    return;
  }
  float ox[ROUND_FAN_MAX], oy[ROUND_FAN_MAX];
  float ix[ROUND_FAN_MAX], iy[ROUND_FAN_MAX];
  const int n = round_fan_points(dx, dy, dw, dh, dr, seg, ox, oy);
  const int ir = dr - t < 0 ? 0 : dr - t;
  round_fan_points(dx + t, dy + t, dw - 2 * t, dh - 2 * t, ir, seg, ix, iy);
  atlas_resolve();
  const ImU32 col = to_imgui(rgba);
  if (!s_dl || n <= 0) return;
  s_dl->PrimReserve(6 * n, 2 * n);
  const unsigned int base = s_dl->_VtxCurrentIdx;
  for (int i = 0; i < n; i++) {
    const int j = (i + 1 < n) ? i + 1 : 0;
    const unsigned int o0 = base + (unsigned int)(2 * i);
    const unsigned int i0 = o0 + 1;
    const unsigned int o1 = base + (unsigned int)(2 * j);
    const unsigned int i1 = o1 + 1;
    s_dl->PrimWriteIdx((ImDrawIdx)o0);
    s_dl->PrimWriteIdx((ImDrawIdx)i0);
    s_dl->PrimWriteIdx((ImDrawIdx)o1);
    s_dl->PrimWriteIdx((ImDrawIdx)i0);
    s_dl->PrimWriteIdx((ImDrawIdx)i1);
    s_dl->PrimWriteIdx((ImDrawIdx)o1);
  }
  for (int i = 0; i < n; i++) {
    s_dl->PrimWriteVtx(ImVec2(ox[i], oy[i]), s_white, col);
    s_dl->PrimWriteVtx(ImVec2(ix[i], iy[i]), s_white, col);
  }
}

void canvas_gradient_rect(int x, int y, int w, int h, unsigned top, unsigned bottom) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  const ImDrawVert a = flat((float)dx,        (float)dy,        top);
  const ImDrawVert b = flat((float)(dx + dw), (float)dy,        top);
  const ImDrawVert c = flat((float)(dx + dw), (float)(dy + dh), bottom);
  const ImDrawVert d = flat((float)dx,        (float)(dy + dh), bottom);
  cv_quad(a, b, c, d);
}

void canvas_glow_rect(int x, int y, int w, int h, unsigned rgba) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  // Additive approximated with alpha, deliberately: ImGui has one blend state per draw data. The error is dst*a darker,
  // (8,9,10)/255 over C_PANEL at C_GLASS_GLOW's alpha. Do not "fix" it with AddCallback.
  fill_device(dx, dy, dw, dh, rgba);
}

// STUBBED as World Tour's is. The real Glass ground is Megaton's canvas.cpp:384-556 (glReadBuffer(GL_BACK) +
// glCopyTexSubImage2D, blurred and tinted); our SwapBuffers hook is the place for it. Megaton does not branch on a
// failed capture, so a false one leaves the tint alone. Off and Simple never come here.
static int s_backSaid = 0;

bool canvas_backdrop_capture(int, int, int, int) {
  if (!s_backSaid) {
    s_backSaid = 1;
    ek_log("canvas: the Glass backdrop capture is not implemented yet; the style degrades to its tint");
  }
  return false;
}

// The tint slab only, Megaton's own failure behaviour; drawing nothing would leave Glass's rim and sheen floating over
// bare game.
void canvas_backdrop_draw(int x, int y, int w, int h, int r, unsigned tint,
                          double, double, int, int) {
  static int said = 0;
  if (!said) { said = 1; ek_log("canvas: no captured ground; drawing the Glass tint alone"); }
  canvas_round_rect(x, y, w, h, r, tint);
}

void canvas_backdrop_release() {
  static int said = 0;
  if (!said) { said = 1; ek_log("canvas: canvas_backdrop_release is a stub; nothing to release"); }
}

// The metrics twins: at any scale but 100 these are the scaled atlas's DEVICE metrics. runtime_live decides which and
// canvas_text asks it too, so advances and glyphs come from one atlas. The baked answer stays when fontgen refuses a
// scale.
static const Glyph* metrics_for(int font) {
  return runtime_live() ? s_run.glyphs[font] : font_glyphs[font];
}
static bool metrics_are_device() { return runtime_live(); }

int canvas_text(int x, int y, const char* s, unsigned rgba, int font) {
  if (!s || font < 0 || font >= FONT_COUNT) return x;
  if (!atlas_resolve()) return x;
  if (!s_dl) return x;
  const bool device = metrics_are_device();
  const Glyph* glyphs = metrics_for(font);
  const int rx = device ? s_runRectX : s_rectX;
  const int ry = device ? s_runRectY : s_rectY;
  const float iw = s_uvScale.x, ih = s_uvScale.y;
  const ImU32 c = to_imgui(rgba);
  // On the scaled atlas the pen is DEVICE from zero (advances are already device, nothing rounds per character); on the
  // baked one it stays logical and the scale applies to the accumulated pen (per-advance scaling would pile up
  // rounding).
  const int ox = device ? ui_scale_px(x, s_scale) : 0;
  const int oy = device ? ui_scale_px(y, s_scale) : 0;
  int pen = device ? 0 : x;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if (*p < 0x20 || *p > 0x7F) continue;
    const Glyph& g = glyphs[*p - 0x20];
    if (g.w > 0) {
      const float gx = device ? (float)tx_dev_x(ox + pen + g.bx) : (float)dev_x(pen + g.bx);
      const float gy = device ? (float)tx_dev_y(oy + g.by)       : (float)dev_y(y + g.by);
      const float gw = device ? (float)tx_dev_len(g.w)           : (float)dev_len(g.w);
      const float gh = device ? (float)tx_dev_len(g.h)           : (float)dev_len(g.h);
      const float u0 = (float)(rx + g.x) * iw;
      const float v0 = (float)(ry + g.y) * ih;
      const float u1 = (float)(rx + g.x + g.w) * iw;
      const float v1 = (float)(ry + g.y + g.h) * ih;
      s_dl->PrimReserve(6, 4);
      const unsigned int base = s_dl->_VtxCurrentIdx;
      s_dl->PrimWriteIdx((ImDrawIdx)base);
      s_dl->PrimWriteIdx((ImDrawIdx)(base + 1));
      s_dl->PrimWriteIdx((ImDrawIdx)(base + 2));
      s_dl->PrimWriteIdx((ImDrawIdx)base);
      s_dl->PrimWriteIdx((ImDrawIdx)(base + 2));
      s_dl->PrimWriteIdx((ImDrawIdx)(base + 3));
      s_dl->PrimWriteVtx(ImVec2(gx,      gy),      ImVec2(u0, v0), c);
      s_dl->PrimWriteVtx(ImVec2(gx + gw, gy),      ImVec2(u1, v0), c);
      s_dl->PrimWriteVtx(ImVec2(gx + gw, gy + gh), ImVec2(u1, v1), c);
      s_dl->PrimWriteVtx(ImVec2(gx,      gy + gh), ImVec2(u0, v1), c);
    }
    pen += g.adv;
  }
  return device ? x + ui_unscale_px(pen, s_scale) : pen;
}
#else
       // Recording bodies so menu.cpp links into a test host without a renderer. These were no-ops once, and mutations
       // to menu_draw's drawing survived the suite; recording each call makes its output testable.
#include <string.h>

static CanvasOp s_ops[CANVAS_RECORD_MAX];
static int  s_opCount = 0;
static bool s_recording = false;
static bool s_overflowed = false;

void canvas_record_begin() { s_opCount = 0; s_overflowed = false; s_recording = true; }
void canvas_record_end()   { s_recording = false; }
int  canvas_record_count() { return s_opCount; }
bool canvas_record_overflowed() { return s_overflowed; }
const CanvasOp* canvas_record_op(int i) {
  return (i >= 0 && i < s_opCount) ? &s_ops[i] : nullptr;
}
const CanvasOp* canvas_record_find_text(const char* s) {
  if (!s) return nullptr;
  for (int i = 0; i < s_opCount; i++)
    if (s_ops[i].kind == CANVAS_OP_TEXT && strcmp(s_ops[i].text, s) == 0) return &s_ops[i];
  return nullptr;
}
const CanvasOp* canvas_record_find_kind(int kind, unsigned colour) {
  for (int i = 0; i < s_opCount; i++)
    if (s_ops[i].kind == kind && s_ops[i].colour == colour) return &s_ops[i];
  return nullptr;
}

// Overflow sets a flag: a truncated recording would let a "never drawn" check pass for the wrong reason.
static CanvasOp* push_op(int kind, int x, int y, int w, int h, unsigned colour) {
  if (!s_recording) return nullptr;
  if (s_opCount >= CANVAS_RECORD_MAX) { s_overflowed = true; return nullptr; }
  CanvasOp& o = s_ops[s_opCount++];
  o.kind = kind; o.x = x; o.y = y; o.w = w; o.h = h;
  o.colour = colour; o.font = -1; o.text[0] = 0;
  return &o;
}

// Every recorded coordinate is device space, matching the device bodies (a recorder in logical numbers would let "the
// scale is applied" pass while nothing moved). The transform is cleared: it belongs to the frame.
void canvas_begin(int, int) { canvas_set_transform(0, 0, 1.0, 0, 0); }
void canvas_end() {}
bool canvas_white_texel(float* u, float* v) {
  if (u) *u = 0.0f;
  if (v) *v = 0.0f;
  return false;
}
CanvasTransform canvas_record_transform() {
  CanvasTransform t;
  t.dx = s_txDx; t.dy = s_txDy; t.cx = s_txCx; t.cy = s_txCy; t.scale = s_txScale;
  return t;
}
void canvas_rect(int x, int y, int w, int h, unsigned c) {
  push_op(CANVAS_OP_RECT, dev_x(x), dev_y(y), dev_len(w), dev_len(h), c);
}
void canvas_bevel(int x, int y, int w, int h, unsigned fill, unsigned, unsigned) {
  push_op(CANVAS_OP_BEVEL, dev_x(x), dev_y(y), dev_len(w), dev_len(h), fill);
}
void canvas_line(int x0, int y0, int x1, int y1, unsigned c) {
  const int dx0 = dev_x(x0), dy0 = dev_y(y0);
  const int dx1 = dev_x(x1), dy1 = dev_y(y1);
  push_op(CANVAS_OP_LINE, dx0, dy0, dx1 - dx0, dy1 - dy0, c);
}
void canvas_hazard(int x, int y, int w, int h, unsigned stripe, unsigned, int) {
  push_op(CANVAS_OP_HAZARD, dev_x(x), dev_y(y), dev_len(w), dev_len(h), stripe);
}
void canvas_round_rect(int x, int y, int w, int h, int, unsigned c) {
  push_op(CANVAS_OP_ROUNDRECT, dev_x(x), dev_y(y), dev_len(w), dev_len(h), c);
}
void canvas_round_rect_outline(int x, int y, int w, int h, int, int, unsigned c) {
  push_op(CANVAS_OP_ROUNDOUTLINE, dev_x(x), dev_y(y), dev_len(w), dev_len(h), c);
}
void canvas_gradient_rect(int x, int y, int w, int h, unsigned top, unsigned) {
  push_op(CANVAS_OP_GRADIENT, dev_x(x), dev_y(y), dev_len(w), dev_len(h), top);
}
void canvas_glow_rect(int x, int y, int w, int h, unsigned c) {
  push_op(CANVAS_OP_GLOW, dev_x(x), dev_y(y), dev_len(w), dev_len(h), c);
}
// Its own op kind: capture and draw are different events. True unconditionally: no device to fail.
bool canvas_backdrop_capture(int x, int y, int w, int h) {
  push_op(CANVAS_OP_CAPTURE, dev_x(x), dev_y(y), dev_len(w), dev_len(h), 0u);
  return true;
}
void canvas_backdrop_draw(int x, int y, int w, int h, int, unsigned tint,
                          double, double, int, int) {
  push_op(CANVAS_OP_BACKDROP, dev_x(x), dev_y(y), dev_len(w), dev_len(h), tint);
}
void canvas_backdrop_release() {}

static const Glyph* metrics_for(int font) { return font_glyphs[font]; }
static bool metrics_are_device() { return false; }

int canvas_text(int x, int y, const char* s, unsigned c, int font) {
  // The recorded width scales like the device quads, since tab_text_fits compares it to a device edge. Accepted: it is
  // ui_scale_px(sum of advances) while the device edge rounds the last glyph, so they differ by about a pixel off 100%.
  CanvasOp* o = push_op(CANVAS_OP_TEXT, dev_x(x), dev_y(y),
                        dev_len(canvas_text_width(s, font)), 0, c);
  if (o) {
    o->font = font;
    if (s) strncpy_s(o->text, sizeof(o->text), s, _TRUNCATE);
  }
  return x;
}
#endif

// Logical for every caller (menu.cpp lays out in logical pixels, so a scale-dependent width would apply the factor
// twice), yet measured through the atlas that will be drawn, since GDI hinting is not linear: sum on the live atlas,
// scale divided back out. The NO_GL host has no GDI, so there the baked sum is the only answer.
int canvas_text_width(const char* s, int font) {
  if (!s || font < 0 || font >= FONT_COUNT) return 0;
  const Glyph* glyphs = metrics_for(font);
  int w = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if (*p < 0x20 || *p > 0x7F) continue;
    w += glyphs[*p - 0x20].adv;
  }
  return metrics_are_device() ? ui_unscale_px(w, s_scale) : w;
}
