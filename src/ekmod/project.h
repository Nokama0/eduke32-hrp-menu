#pragma once
#include "udstate.h"
#include "projmath.h"

// World-to-screen for POLYMER, which skips the classic xdimenscale/yxaspect path.

struct EkViewport {
  int x, y, w, h;
  bool fromGL;
};

struct EkProjection {
  EkCamera cam;
  EkViewport vp;
  float viewingRange;
  float cot, aspect;
  bool valid;
};

// Once per frame, render thread, GL context current.
bool project_begin(EkProjection* out);

bool project_point(const EkProjection& p, int wx, int wy, int wz,
                   float* sx, float* sy);

// First half of project_point: a segment must be near-clipped in eye space or it streaks.
EkmVec3 project_to_eye(const EkProjection& p, int wx, int wy, int wz);

// Accepts a point AT the near plane (what ekm_clip_near returns).
bool project_eye_point(const EkProjection& p, EkmVec3 eye, float* sx, float* sy);
