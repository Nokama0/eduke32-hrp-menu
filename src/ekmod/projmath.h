#pragma once
#include <math.h>

// Projection arithmetic with no Win32 or OpenGL, split out so the suite can test it.

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const int EKM_ANGLE_UNITS = 2048;

// Build has two scales: z is 16x the horizontal unit, so polymer maps a point as (y, -z/16, -x).
const float EKM_Z_PER_XY = 16.0f;

struct EkmVec3 { float x, y, z; };

// Masked, not shifted: the raw value runs outside the range and wraps on use.
inline int ekm_fix16_to_buildang(int angFix) {
  return (angFix >> 16) & (EKM_ANGLE_UNITS - 1);
}

// horiz is a slope: pitch = atan2(horiz - 100, 128) (polymer.cpp:1121); level is 100.
inline float ekm_horiz_to_pitch(float horiz) {
  return -atan2f(horiz - 100.0f, 128.0f);
}

inline float ekm_buildang_to_degrees(int angFix) {
  return ((float)angFix / 65536.0f) * 360.0f / (float)EKM_ANGLE_UNITS;
}

inline EkmVec3 ekm_to_eye_space(int wx, int wy, int wz, int cx, int cy, int cz) {
  EkmVec3 v;
  v.x = ((float)wy - (float)cy) / 1000.0f;
  v.y = -(((float)wz - (float)cz) / EKM_Z_PER_XY) / 1000.0f;
  v.z = -((float)wx - (float)cx) / 1000.0f;
  return v;
}

// glRotatef(ang, 0,1,0): x' = cos*x + sin*z, z' = -sin*x + cos*z.
inline EkmVec3 ekm_rotate_yaw(EkmVec3 v, float angDeg) {
  const float a = angDeg * (float)M_PI / 180.0f;
  const float c = cosf(a), s = sinf(a);
  EkmVec3 o;
  o.x =  c * v.x + s * v.z;
  o.y =  v.y;
  o.z = -s * v.x + c * v.z;
  return o;
}

// glRotatef(horizang, 1,0,0): y' = cos*y - sin*z, z' = sin*y + cos*z.
inline EkmVec3 ekm_rotate_pitch(EkmVec3 v, float pitchRad) {
  const float c = cosf(pitchRad), s = sinf(pitchRad);
  EkmVec3 o;
  o.x = v.x;
  o.y = c * v.y - s * v.z;
  o.z = s * v.y + c * v.z;
  return o;
}

// Derived, never read post-frame: G_DrawRooms restores viewingrange at frame end (game.cpp:1250), so it reads 65536.
inline float ekm_viewingrange(int fovDegrees, int playerYrepeat) {
  const float shrink = 64.0f / (float)(playerYrepeat + 28);
  return 65536.0f * tanf((float)fovDegrees * (float)M_PI / 360.0f) * shrink;
}

// gluPerspective's cotangent (polymer.cpp:919-936).
inline float ekm_cot(float viewingRange, int vpW, int vpH, int xdim, int ydim) {
  if (vpW <= 0 || vpH <= 0 || xdim <= 0 || ydim <= 0) return 0.0f;
  const float k = (viewingRange / 65536.0f) * ((float)vpH / (float)vpW) *
                  ((float)xdim / (float)ydim) * 0.75f;
  return k > 0.0001f ? 1.0f / k : 0.0f;
}

// Near plane in eye space (polymer looks down -z); named once so the clip lands exactly on it.
const float EKM_NEAR_EYE_Z = -0.01f;

inline bool ekm_in_front(EkmVec3 eye) { return eye.z < EKM_NEAR_EYE_Z; }

// At or in front: what a clipped endpoint is; the strict test would refuse the clip's own output.
inline bool ekm_projectable(EkmVec3 eye) { return eye.z <= EKM_NEAR_EYE_Z; }

// Clip BEFORE projecting either end: a point behind the eye projects mirrored and the line streaks. The behind end
// moves onto the plane; both behind returns 0 (a/b untouched).
inline int ekm_clip_near(EkmVec3* a, EkmVec3* b) {
  if (!a || !b) return 0;
  const bool aFront = ekm_in_front(*a);
  const bool bFront = ekm_in_front(*b);
  if (aFront && bFront) return 1;
  if (!aFront && !bFront) return 0;

  const EkmVec3 in = aFront ? *a : *b;
  const EkmVec3 out = aFront ? *b : *a;
  const float t = (EKM_NEAR_EYE_Z - in.z) / (out.z - in.z);
  EkmVec3 cut;
  cut.x = in.x + (out.x - in.x) * t;
  cut.y = in.y + (out.y - in.y) * t;
  cut.z = EKM_NEAR_EYE_Z;
  if (aFront) *b = cut; else *a = cut;
  return 1;
}

inline float ekm_ndc_x(EkmVec3 eye, float cot, float aspect) {
  return (cot / aspect) * eye.x / -eye.z;
}
inline float ekm_ndc_y(EkmVec3 eye, float cot) {
  return cot * eye.y / -eye.z;
}

inline float ekm_screen_x(float ndcX, int vpX, int vpW) {
  return (float)vpX + (ndcX + 1.0f) * 0.5f * (float)vpW;
}
inline float ekm_screen_y(float ndcY, int vpY, int vpH) {
  return (float)vpY + (1.0f - ndcY) * 0.5f * (float)vpH;
}

// polymer.cpp:3983: xratio = xrepeat*0.20, yratio = yrepeat*0.25 against tilesiz.

inline float ekm_sprite_height_z(int yrepeat, int tileH) {
  return 4.0f * (float)yrepeat * (float)tileH;
}

inline float ekm_sprite_halfwidth(int xrepeat, int tileW) {
  return 0.1f * (float)xrepeat * (float)tileW;
}

// Horizontal units: the only form comparable to the half-width.
inline float ekm_sprite_height_flat(int yrepeat, int tileH) {
  return ekm_sprite_height_z(yrepeat, tileH) / EKM_Z_PER_XY;
}

// A pure ratio: no second projection, tracks distance for free.
inline float ekm_projected_halfwidth(float projectedHeight, int xrepeat, int tileW,
                                     int yrepeat, int tileH) {
  const float flat = ekm_sprite_height_flat(yrepeat, tileH);
  if (flat <= 0.0f) return projectedHeight * 0.25f;
  return projectedHeight * (ekm_sprite_halfwidth(xrepeat, tileW) / flat);
}

inline int ekm_feet_z(int spriteZ, float heightZ, unsigned cstat) {
  return (cstat & 128u) ? spriteZ + (int)(heightZ / 2.0f) : spriteZ;
}
