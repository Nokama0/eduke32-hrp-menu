// World-quad storage and its one GL draw; rules are in worldquad.h.

#include <windows.h>

#include "worldquad.h"
#include "frustum.h"
#include "log.h"
#include "perf.h"

// One frame's worth, rebuilt from live geometry each frame.
static EkWorldQuad s_quads[EK_WORLD_QUAD_MAX];
static int s_count = 0;
static int s_refused = 0;
// Most any one collect pass was refused; outlives the reset, because the overlay row reads it in the SwapBuffers hook
// long after the flush emptied the list. A pass, not a frame: the producer resets at the head of its collect.
static int s_refusedPeak = 0;
// Interlocked: set on the game thread and at DLL detach, asked from another. A session total, not cleared by the reset:
// with no draw point no flush runs.
static volatile LONG s_haveDrawPoint = 0;
static int s_noDrawPoint = 0;

void worldquad_reset(void) {
  // Folded here so no path empties the list without banking the number.
  if (s_refused > s_refusedPeak) s_refusedPeak = s_refused;
  s_count = 0;
  s_refused = 0;
}

void worldquad_set_drawpoint(bool ok) {
  const LONG was = InterlockedExchange(&s_haveDrawPoint, ok ? 1 : 0);
  if (ok && was == 0) s_noDrawPoint = 0;
}

int worldquad_no_drawpoint(void) { return s_noDrawPoint; }

void worldquad_push(const EkWorldQuad& q) {
  if (InterlockedCompareExchange(&s_haveDrawPoint, 0, 0) == 0) {
    s_noDrawPoint++;
    return;
  }
  const int after = ekm_world_quad_append(s_quads, s_count, EK_WORLD_QUAD_MAX, q);
  if (after == s_count) s_refused++;
  s_count = after;
}

int worldquad_count(void) { return s_count; }

int worldquad_refused(void) { return s_refusedPeak; }

// GL 1.1 from opengl32.dll: the context is compatibility and immediate mode works (spike: live world transform in
// GL_MODELVIEW_MATRIX with the 1/1000 Build scale). Same lookup as camera.cpp:356. polymer has unbound its shader
// program by now.
#define EK_GL_QUADS 0x0007u
#define EK_GL_LEQUAL 0x0203u
#define EK_GL_CURRENT_COLOR 0x0B00u
#define EK_GL_SRC_ALPHA 0x0302u
#define EK_GL_ONE_MINUS_SRC_ALPHA 0x0303u
#define EK_GL_CULL_FACE 0x0B44u
#define EK_GL_DEPTH_TEST 0x0B71u
#define EK_GL_DEPTH_WRITEMASK 0x0B72u
#define EK_GL_DEPTH_FUNC 0x0B74u
#define EK_GL_BLEND_DST 0x0BE0u
#define EK_GL_BLEND_SRC 0x0BE1u
#define EK_GL_BLEND 0x0BE2u
#define EK_GL_ALPHA_TEST 0x0BC0u
#define EK_GL_TEXTURE_2D 0x0DE1u
#define EK_GL_NO_ERROR 0x0000u
#define EK_GL_MODELVIEW_MATRIX 0x0BA6u
#define EK_GL_PROJECTION_MATRIX 0x0BA7u
#define EK_GL_VIEWPORT 0x0BA2u
#define EK_GL_CURRENT_PROGRAM 0x8B8Du
#define EK_GL_DEPTH_RANGE 0x0B70u
// No fog disable: the volume would be the one surface that does not dim with distance. Polygon offset is on: secret
// panels are built from a wall's own vertices and the sector's z, so exactly coplanar with real walls; values in
// worldquad.h.
#define EK_GL_POLYGON_OFFSET_FILL 0x8037u
#define EK_GL_POLYGON_OFFSET_FACTOR 0x8038u
#define EK_GL_POLYGON_OFFSET_UNITS 0x2A00u

typedef void(WINAPI* WqToggleFn)(unsigned int);
typedef unsigned char(WINAPI* WqIsEnabledFn)(unsigned int);
typedef void(WINAPI* WqBeginFn)(unsigned int);
typedef void(WINAPI* WqEndFn)(void);
typedef void(WINAPI* WqColor4ubFn)(unsigned char, unsigned char, unsigned char,
                                   unsigned char);
// Colour saved and restored in floats: GL_CURRENT_COLOR is a float vector and glColor4ub would quantise it.
typedef void(WINAPI* WqColor4fFn)(float, float, float, float);
typedef void(WINAPI* WqGetFloatvFn)(unsigned int, float*);
typedef void(WINAPI* WqVertex3dFn)(double, double, double);
typedef void(WINAPI* WqBlendFuncFn)(unsigned int, unsigned int);
typedef void(WINAPI* WqDepthMaskFn)(unsigned char);
typedef void(WINAPI* WqDepthFuncFn)(unsigned int);
typedef void(WINAPI* WqPolygonOffsetFn)(float, float);
typedef void(WINAPI* WqGetIntegervFn)(unsigned int, int*);
typedef void(WINAPI* WqGetBooleanvFn)(unsigned int, unsigned char*);
typedef unsigned int(WINAPI* WqGetErrorFn)(void);

static WqToggleFn s_glEnable = NULL;
static WqToggleFn s_glDisable = NULL;
static WqIsEnabledFn s_glIsEnabled = NULL;
static WqBeginFn s_glBegin = NULL;
static WqEndFn s_glEnd = NULL;
static WqColor4ubFn s_glColor4ub = NULL;
static WqColor4fFn s_glColor4f = NULL;
static WqGetFloatvFn s_glGetFloatv = NULL;
static WqVertex3dFn s_glVertex3d = NULL;
static WqBlendFuncFn s_glBlendFunc = NULL;
static WqDepthMaskFn s_glDepthMask = NULL;
static WqDepthFuncFn s_glDepthFunc = NULL;
static WqPolygonOffsetFn s_glPolygonOffset = NULL;
static WqGetIntegervFn s_glGetIntegerv = NULL;
static WqGetBooleanvFn s_glGetBooleanv = NULL;
static WqGetErrorFn s_glGetError = NULL;

static bool s_glTried = false;
static bool s_saidNoGl = false;
static bool s_saidError = false;
static bool s_saidState = false;

// True only with all entry points: without glIsEnabled or glGetIntegerv it would draw by assuming state. Tried once.
static bool worldquad_gl_entries(void) {
  if (!s_glTried) {
    s_glTried = true;
    const HMODULE gl = GetModuleHandleA("opengl32.dll");
    if (gl) {
      s_glEnable = (WqToggleFn)GetProcAddress(gl, "glEnable");
      s_glDisable = (WqToggleFn)GetProcAddress(gl, "glDisable");
      s_glIsEnabled = (WqIsEnabledFn)GetProcAddress(gl, "glIsEnabled");
      s_glBegin = (WqBeginFn)GetProcAddress(gl, "glBegin");
      s_glEnd = (WqEndFn)GetProcAddress(gl, "glEnd");
      s_glColor4ub = (WqColor4ubFn)GetProcAddress(gl, "glColor4ub");
      s_glColor4f = (WqColor4fFn)GetProcAddress(gl, "glColor4f");
      s_glGetFloatv = (WqGetFloatvFn)GetProcAddress(gl, "glGetFloatv");
      s_glVertex3d = (WqVertex3dFn)GetProcAddress(gl, "glVertex3d");
      s_glBlendFunc = (WqBlendFuncFn)GetProcAddress(gl, "glBlendFunc");
      s_glDepthMask = (WqDepthMaskFn)GetProcAddress(gl, "glDepthMask");
      s_glDepthFunc = (WqDepthFuncFn)GetProcAddress(gl, "glDepthFunc");
      s_glPolygonOffset =
          (WqPolygonOffsetFn)GetProcAddress(gl, "glPolygonOffset");
      s_glGetIntegerv = (WqGetIntegervFn)GetProcAddress(gl, "glGetIntegerv");
      s_glGetBooleanv = (WqGetBooleanvFn)GetProcAddress(gl, "glGetBooleanv");
      s_glGetError = (WqGetErrorFn)GetProcAddress(gl, "glGetError");
    }
  }
  return s_glEnable && s_glDisable && s_glIsEnabled && s_glBegin && s_glEnd &&
         s_glColor4ub && s_glColor4f && s_glGetFloatv && s_glVertex3d &&
         s_glBlendFunc && s_glDepthMask && s_glDepthFunc && s_glPolygonOffset &&
         s_glGetIntegerv && s_glGetBooleanv && s_glGetError;
}

// An empty list costs nothing but the perf zone pair above the return. Read what it depends on, set what it needs,
// restore all: inherited state bit this port three times (cull face off drew wall backsides, camera.cpp:317-330; depth
// test off; an assumed depth func). GL_DEPTH_FUNC is read too.
void worldquad_flush(void) {
  // The zone spans the whole function and closes on every return, or empty frames would read as a win.
  perf_zone_begin(EK_PZ_OVERLAY_QUADS);
  if (s_count <= 0) { perf_zone_end(EK_PZ_OVERLAY_QUADS, 0); return; }

  if (!worldquad_gl_entries()) {
    if (!s_saidNoGl) {
      s_saidNoGl = true;
      ek_log("worldquad: opengl32.dll did not yield every GL 1.1 entry point "
             "this draw needs, so the quad layer stands down for the session");
    }
    worldquad_reset();
    perf_zone_end(EK_PZ_OVERLAY_QUADS, 0);
    return;
  }

  // Drain first so the final reading is ours: glGetError clears ONE flag per call and the engine may leave several,
  // which would spend the said-once latch. Bounded (16): a lying driver would hang the frame.
  for (int drain = 0; drain < 16; drain++)
    if (s_glGetError() == EK_GL_NO_ERROR) break;

  // Once, on the first flush with quads. A bound shader program swallows immediate-mode vertices silently and a
  // modelview that is not the world transform puts the quad off screen; both look like this bug.
  if (!s_saidState) {
    s_saidState = true;
    float mv[16] = {0.0f}, pr[16] = {0.0f};
    int vp[4] = {0, 0, 0, 0}, prog = -1;
    s_glGetFloatv(EK_GL_MODELVIEW_MATRIX, mv);
    s_glGetFloatv(EK_GL_PROJECTION_MATRIX, pr);
    s_glGetIntegerv(EK_GL_VIEWPORT, vp);
    s_glGetIntegerv(EK_GL_CURRENT_PROGRAM, &prog);
    int dfunc = 0;
    unsigned char dmask = 1;
    s_glGetIntegerv(EK_GL_DEPTH_FUNC, &dfunc);
    s_glGetBooleanv(EK_GL_DEPTH_WRITEMASK, &dmask);
    ek_log("worldquad: state at the draw point -- program %d, viewport "
           "%d,%d %dx%d, depth test %d func 0x%04X mask %d, blend %d, "
           "texture2d %d, alphatest %d, cull %d",
           prog, vp[0], vp[1], vp[2], vp[3],
           s_glIsEnabled(EK_GL_DEPTH_TEST) ? 1 : 0,
           (unsigned)dfunc, (int)dmask,
           s_glIsEnabled(EK_GL_BLEND) ? 1 : 0,
           s_glIsEnabled(EK_GL_TEXTURE_2D) ? 1 : 0,
           s_glIsEnabled(EK_GL_ALPHA_TEST) ? 1 : 0,
           s_glIsEnabled(EK_GL_CULL_FACE) ? 1 : 0);
    ek_log("worldquad: modelview diag %.6f %.6f %.6f %.6f, row3 %.2f %.2f %.2f",
           mv[0], mv[5], mv[10], mv[15], mv[12], mv[13], mv[14]);
    float dr[2] = {0.0f, 1.0f};
    s_glGetFloatv(EK_GL_DEPTH_RANGE, dr);
    ek_log("worldquad: projection diag %.6f %.6f %.6f %.6f, depth range %.6f..%.6f",
           pr[0], pr[5], pr[10], pr[15], dr[0], dr[1]);
    const EkWorldQuad& q0 = s_quads[0];
    ek_log("worldquad: %d quad(s); first argb 0x%08X corners "
           "(%.1f,%.1f,%d) (%.1f,%.1f,%d) (%.1f,%.1f,%d) (%.1f,%.1f,%d)",
           s_count, q0.argb,
           q0.x[0], q0.y[0], q0.z[0], q0.x[1], q0.y[1], q0.z[1],
           q0.x[2], q0.y[2], q0.z[2], q0.x[3], q0.y[3], q0.z[3]);
  }

  const int wasDepth = s_glIsEnabled(EK_GL_DEPTH_TEST) ? 1 : 0;
  const int wasBlend = s_glIsEnabled(EK_GL_BLEND) ? 1 : 0;
  const int wasTex = s_glIsEnabled(EK_GL_TEXTURE_2D) ? 1 : 0;
  const int wasAlpha = s_glIsEnabled(EK_GL_ALPHA_TEST) ? 1 : 0;
  const int wasCull = s_glIsEnabled(EK_GL_CULL_FACE) ? 1 : 0;
  // Both halves saved, enable and values: glPolygonOffset is global and the engine biases per sprite
  // (polymer.cpp:1583).
  const int wasPolyOff = s_glIsEnabled(EK_GL_POLYGON_OFFSET_FILL) ? 1 : 0;
  float wasOffset[2] = {0.0f, 0.0f};
  s_glGetFloatv(EK_GL_POLYGON_OFFSET_FACTOR, &wasOffset[0]);
  s_glGetFloatv(EK_GL_POLYGON_OFFSET_UNITS, &wasOffset[1]);
  unsigned char wasDepthMask = 1;
  s_glGetBooleanv(EK_GL_DEPTH_WRITEMASK, &wasDepthMask);
  int wasDepthFunc = (int)EK_GL_LEQUAL;
  int wasSrc = (int)EK_GL_SRC_ALPHA, wasDst = (int)EK_GL_ONE_MINUS_SRC_ALPHA;
  s_glGetIntegerv(EK_GL_DEPTH_FUNC, &wasDepthFunc);
  s_glGetIntegerv(EK_GL_BLEND_SRC, &wasSrc);
  s_glGetIntegerv(EK_GL_BLEND_DST, &wasDst);
  float wasColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  s_glGetFloatv(EK_GL_CURRENT_COLOR, wasColor);

  if (!wasDepth) s_glEnable(EK_GL_DEPTH_TEST);
  s_glDepthFunc(EK_GL_LEQUAL);
  // Translucent: tests depth, does not write it.
  if (wasDepthMask) s_glDepthMask(0);
  if (!wasBlend) s_glEnable(EK_GL_BLEND);
  s_glBlendFunc(EK_GL_SRC_ALPHA, EK_GL_ONE_MINUS_SRC_ALPHA);
  if (wasTex) s_glDisable(EK_GL_TEXTURE_2D);
  if (wasAlpha) s_glDisable(EK_GL_ALPHA_TEST);
  // Load-bearing: a box is seen outside on one face and inside on the opposite, so culling either winding draws half a
  // volume.
  if (wasCull) s_glDisable(EK_GL_CULL_FACE);
  // Toward the viewer once per flush: the panels tie with the room's real walls and would z-fight. One value serves
  // both producers; sized for the panels. Derivation in worldquad.h.
  if (!wasPolyOff) s_glEnable(EK_GL_POLYGON_OFFSET_FILL);
  s_glPolygonOffset(EK_WORLD_OFFSET_FACTOR, EK_WORLD_OFFSET_UNITS);

  for (int i = 0; i < s_count; i++) {
    const EkWorldQuad& q = s_quads[i];
    const unsigned c = q.argb;
    s_glColor4ub((unsigned char)((c >> 16) & 0xFFu),
                 (unsigned char)((c >> 8) & 0xFFu),
                 (unsigned char)(c & 0xFFu),
                 (unsigned char)((c >> 24) & 0xFFu));
    s_glBegin(EK_GL_QUADS);
    // Build units in, polymer's axes out: polymer_drawrooms fills pos[] as (daposy, -daposz/16, -daposx)
    // (polymer.cpp:1118), so all three axes permute and z carries the 16-to-1 unit. Without it the tint drew off screen
    // with no error.
    for (int v = 0; v < 4; v++) {
      const EkGlPointD g = ekm_build_to_gld(q.x[v], q.y[v], (double)q.z[v]);
      s_glVertex3d(g.x, g.y, g.z);
    }
    s_glEnd();
  }

  s_glColor4f(wasColor[0], wasColor[1], wasColor[2], wasColor[3]);
  if (wasCull) s_glEnable(EK_GL_CULL_FACE);
  if (!wasPolyOff) s_glDisable(EK_GL_POLYGON_OFFSET_FILL);
  s_glPolygonOffset(wasOffset[0], wasOffset[1]);
  if (wasAlpha) s_glEnable(EK_GL_ALPHA_TEST);
  if (wasTex) s_glEnable(EK_GL_TEXTURE_2D);
  s_glBlendFunc((unsigned int)wasSrc, (unsigned int)wasDst);
  if (!wasBlend) s_glDisable(EK_GL_BLEND);
  if (wasDepthMask) s_glDepthMask(1);
  s_glDepthFunc((unsigned int)wasDepthFunc);
  if (!wasDepth) s_glDisable(EK_GL_DEPTH_TEST);

  // Asked after the restore, so it covers it too; said once.
  const unsigned int err = s_glGetError();
  if (err != EK_GL_NO_ERROR && !s_saidError) {
    s_saidError = true;
    ek_log("worldquad: glGetError reads 0x%04X after a flush of %d quad(s) "
           "(%d refused by the cap of %d)",
           err, s_count, s_refused, EK_WORLD_QUAD_MAX);
  }

  // Taken before the reset clears it.
  const int quadsDrawn = s_count;
  worldquad_reset();
  perf_zone_end(EK_PZ_OVERLAY_QUADS, quadsDrawn);
}
