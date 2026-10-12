#pragma once

// Combination-lock tint, pure half: verdict, colour, model origin in build z, the box as six faces. esp.cpp cannot link
// into test_host, so a rule there is unpinnable. The wall-sprite zrange and sector-clamp helpers have no production
// caller (see each).
#include <string.h>
#include "shotray.h"
#include "esp_switches.h"
#include "modelbounds.h"
#include "worldquad.h"
#include "ui/common.h"

// Only a combination lock gets a verdict; "Needs blue card" is a fact about keys.
enum { EKM_TINT_NONE = 0, EKM_TINT_FLIP = 1, EKM_TINT_LEAVE = 2 };

// Compares the classifier's line (esp_switches.h) so tint and label cannot disagree.
inline int ekm_tint_verdict(int family, const char* line) {
  if (family != EK_SW_COMBINATION || !line) return EKM_TINT_NONE;
  if (strcmp(line, "Flip") == 0)  return EKM_TINT_FLIP;
  if (strcmp(line, "Leave") == 0) return EKM_TINT_LEAVE;
  return EKM_TINT_NONE;
}

// 0 for NONE is not a colour: the caller refuses first. Colours live in ui/canvas.h, passed in.
inline unsigned ekm_tint_colour(int verdict, unsigned flip, unsigned leave) {
  if (verdict == EKM_TINT_FLIP)  return flip;
  if (verdict == EKM_TINT_LEAVE) return leave;
  return 0u;
}

// Wall-sprite vertical extent (engine.cpp:5383-5396); cstat bit 7 adds half the height (+1 yrepeat if tilesizy is odd).
// Kept: a test derives the centred shift of ekm_tint_model_origin_z independently from it.
inline void ekm_tint_wall_sprite_zrange(int z, int yrepeat, int tilesizy, int yoff,
                                         int centred, int* top, int* bottom) {
  if (!top || !bottom) return;
  long long z2 = (long long)z - (((long long)yoff * (long long)yrepeat) << 2);
  if (centred) {
    z2 += ((long long)tilesizy * (long long)yrepeat) << 1;
    if (tilesizy & 1) z2 += (long long)yrepeat << 1;
  }
  const long long z1 = z2 - (((long long)tilesizy * (long long)yrepeat) << 2);
  *top    = clamp_ll_to_int(z1);
  *bottom = clamp_ll_to_int(z2);
}

// Sector clamp (engine.cpp:6180-6183): flat ceilingz/floorz, each side refused on its own parallax bit. Kept: a secret
// volume will want its parallax rule.
inline void ekm_tint_clamp_to_sector(int ceilingz, int ceilingParallax,
                                      int floorz, int floorParallax,
                                      int* top, int* bottom) {
  if (!top || !bottom) return;
  if (!ceilingParallax && *top < ceilingz) *top = ceilingz;
  if (!floorParallax && *bottom > floorz) *bottom = floorz;
}

// Not tspr->z when y-centred: polymer.cpp:4510 translates by -(tilesiz.y*yrepeat)/8 in GL, i.e. build_dz = +2 *
// tilesiz.y * yrepeat (downward: the model stands on its origin at the foot). No odd-height fixup, unlike zrange:
// polymer's model path has none.
inline int ekm_tint_model_origin_z(int z, int yrepeat, int tilesizy, int centred) {
  if (!centred) return z;
  return clamp_ll_to_int((long long)z +
                         (((long long)tilesizy * (long long)yrepeat) << 1));
}

// A volume, because no depth bias beats volume: with r_models the HRP model stands proud of the sprite's plane and four
// tints on that plane lost to it. Axes: facing normal, span (engine_priv.h:375-388), up. A degenerate box yields zero
// faces: six coincident faces would draw one plane at six times the alpha.
inline int ekm_tint_box_faces(const EkModelBox& box, int x, int y, int z,
                              int ang, int xrepeat, int yrepeat,
                              EkWorldQuad* out6, unsigned argb) {
  if (!out6 || !box.ok) return 0;
  if (xrepeat <= 0 || yrepeat <= 0) return 0;
  // Finite and ordered per axis via modelbounds.h: `!(min < max)` misses an infinity.
  if (!ekm_mdl_frame_plausible(box.minx, box.miny, box.minz,
                               box.maxx, box.maxy, box.maxz)) return 0;

  const double cosA = (double)ekm_sintable(ang + 512) / 16384.0;
  const double sinA = (double)ekm_sintable(ang) / 16384.0;
  const double nlo = box.minx * (double)xrepeat;
  const double nhi = box.maxx * (double)xrepeat;
  const double slo = box.miny * (double)xrepeat;
  const double shi = box.maxy * (double)xrepeat;
  const double vlo = box.minz * (double)yrepeat;
  const double vhi = box.maxz * (double)yrepeat;

  // Axes from polymer.cpp:4455-4532 (ang less 90, rotate -ang about up, -90 about x): with a = ang*2pi/2048, dx =
  // bx*cos(a) + by*sin(a), dy = bx*sin(a) - by*cos(a). So model x runs out of the wall and y across the face. Corners
  // stay double (rounding tilts the quad, as worldquad.h); z clamped.
  double cx[8], cy[8];
  int cz[8];
  for (int i = 0; i < 8; i++) {
    const double n = (i & 1) ? nhi : nlo;
    const double s = (i & 2) ? shi : slo;
    const double v = (i & 4) ? vhi : vlo;
    cx[i] = (double)x + n * cosA + s * sinA;
    cy[i] = (double)y + n * sinA - s * cosA;
    cz[i] = clamp_double_to_int((double)z + v);
  }

  // Corner bits: 1 normal, 2 span, 4 vertical. Each face walks its rectangle one bit at a time; top/top/bottom/bottom
  // is a bowtie.
  static const int kFace[6][4] = {
      {0, 2, 6, 4},
      {1, 5, 7, 3},
      {0, 4, 5, 1},
      {2, 3, 7, 6},
      {0, 1, 3, 2},
      {4, 6, 7, 5},
  };
  for (int fi = 0; fi < 6; fi++) {
    for (int k = 0; k < 4; k++) {
      const int c = kFace[fi][k];
      out6[fi].x[k] = cx[c];
      out6[fi].y[k] = cy[c];
      out6[fi].z[k] = cz[c];
    }
    out6[fi].argb = argb;
  }
  return 6;
}
