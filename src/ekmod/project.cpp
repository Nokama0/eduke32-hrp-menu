// World-to-screen for Polymer, taken from polymer.cpp, not from memory of Build. `horiz` is a slope (level = 100):
// pitch = atan2(horiz - 100, 128). viewingrange is derived, never read: G_DrawRooms restores it before we draw,
// so it reads a constant 65536.

#include <windows.h>
#include <math.h>
#include "log.h"
#include "gamestate.h"
#include "projmath.h"
#include "project.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// GL_VIEWPORT, asked of OpenGL itself rather than reconstructed from windowxy1/xdimen/ydimen.
typedef void(WINAPI* GetIntegervFn)(unsigned int, int*);
static GetIntegervFn s_glGetIntegerv = NULL;

static void load_gl(void) {
  if (s_glGetIntegerv) return;
  const HMODULE gl = GetModuleHandleA("opengl32.dll");
  if (gl) s_glGetIntegerv = (GetIntegervFn)GetProcAddress(gl, "glGetIntegerv");
}

// The player's sprite carries the shrink/grow state that scales the field of view.
static const int EK_APLAYER  = 1405;   // names.h:407
static const int EK_STAT_PLAYER = 10;  // actors.h:59

static int player_yrepeat(void) {
  if (!gamestate_ready()) return 36;                 // premap.cpp:718, spawn value
  for (int i = 0; i < EK_MAXSPRITES; i++) {
    const unsigned char* r = gamestate_sprite(i);
    if (!r) break;
    if (*(const short*)(r + EK_SPR_PICNUM) != EK_APLAYER) continue;
    if (*(const short*)(r + EK_SPR_STATNUM) != EK_STAT_PLAYER) continue;
    const unsigned char yr = r[EK_SPR_YREPEAT];
    return yr ? yr : 36;
  }
  return 36;
}

bool project_begin(EkProjection* out) {
  if (!out) return false;
  out->valid = false;
  if (!ud_camera(&out->cam)) return false;

  load_gl();

  // Viewport: ask GL, and say so in the log the first time either way.
  EkViewport vp = {0, 0, 0, 0, false};
  if (s_glGetIntegerv) {
    int v[4] = {0, 0, 0, 0};
    s_glGetIntegerv(0x0BA2 /* GL_VIEWPORT */, v);
    if (v[2] > 0 && v[3] > 0) {
      // GL's origin is bottom-left; ours is top-left like ImGui's.
      vp.x = v[0];
      vp.w = v[2];
      vp.h = v[3];
      vp.y = 0;                 // filled below once the client height is known
      vp.fromGL = true;
      vp.y = v[1];
    }
  }

  RECT rc = {0, 0, 0, 0};
  const HWND wnd = GetActiveWindow();
  if (wnd) GetClientRect(wnd, &rc);
  const int clientW = rc.right - rc.left;
  const int clientH = rc.bottom - rc.top;

  if (!vp.fromGL) {
    vp.x = 0; vp.y = 0; vp.w = clientW; vp.h = clientH;
  } else if (clientH > 0) {
    vp.y = clientH - (vp.y + vp.h);       // GL bottom-left -> top-left
  }
  if (vp.w <= 0 || vp.h <= 0) return false;
  out->vp = vp;

  // viewingrange, derived. game.cpp:886-895.
  const int yrep = player_yrepeat();
  out->viewingRange = ekm_viewingrange(out->cam.fov, yrep);

  // gluPerspective as polymer builds it.
  const float xdim = (float)(clientW > 0 ? clientW : vp.w);
  const float ydim = (float)(clientH > 0 ? clientH : vp.h);
  out->cot = ekm_cot(out->viewingRange, vp.w, vp.h, (int)xdim, (int)ydim);
  if (out->cot <= 0.0f) return false;
  out->aspect = (float)vp.w / (float)vp.h;

  static bool logged = false;
  if (!logged) {
    logged = true;
    ek_log("project: viewport %d,%d %dx%d (%s), client %dx%d",
           vp.x, vp.y, vp.w, vp.h, vp.fromGL ? "from GL" : "ASSUMED client rect",
           clientW, clientH);
    ek_log("project: fov %d, player yrepeat %d -> viewingrange %.0f, cot %.4f, aspect %.4f",
           out->cam.fov, yrep, out->viewingRange, out->cot, out->aspect);
  }

  out->valid = true;
  return true;
}

// THE ARITHMETIC LIVES IN projmath.h AND IS PINNED BY THE SUITE.
EkmVec3 project_to_eye(const EkProjection& p, int wx, int wy, int wz) {
  const EkmVec3 rel = ekm_to_eye_space(wx, wy, wz, p.cam.x, p.cam.y, p.cam.z);
  const EkmVec3 yawed = ekm_rotate_yaw(rel, ekm_buildang_to_degrees(p.cam.angFix));
  return ekm_rotate_pitch(yawed,
                          ekm_horiz_to_pitch((float)p.cam.horizFix / 65536.0f));
}

bool project_eye_point(const EkProjection& p, EkmVec3 eye, float* sx, float* sy) {
  if (!p.valid || !ekm_projectable(eye)) return false;
  if (sx) *sx = ekm_screen_x(ekm_ndc_x(eye, p.cot, p.aspect), p.vp.x, p.vp.w);
  if (sy) *sy = ekm_screen_y(ekm_ndc_y(eye, p.cot), p.vp.y, p.vp.h);
  return true;
}

// The two above combined; the strict ekm_in_front stays the point path's test.
bool project_point(const EkProjection& p, int wx, int wy, int wz,
                   float* sx, float* sy) {
  if (!p.valid) return false;
  const EkmVec3 eye = project_to_eye(p, wx, wy, wz);
  if (!ekm_in_front(eye)) return false;
  return project_eye_point(p, eye, sx, sy);
}
