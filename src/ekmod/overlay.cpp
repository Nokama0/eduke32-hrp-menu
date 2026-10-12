// The panel: our own ImGui drawn on the hooked present (EDuke32's built-in one is statically linked and unexported).
// Runs in GDI32!SwapBuffers on the render thread with the game's GL context current; input is input.cpp's, on the
// window thread.

#include <windows.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_impl_win32.h"
#include "log.h"
#include "overlay.h"
#include "cvars.h"
#include "input.h"
#include "gamestate.h"
#include "esp.h"
#include "ui/menu.h"
#include "config.h"
#include "ui/input.h"
#include "ui/canvas.h"
#include "project.h"
#include "cansee.h"
#include "hitscan.h"
#include "aim.h"
#include "tbot.h"
#include "tracer.h"
#include "dmgnum.h"
#include "gamehooks.h"
#include "mousebind.h"
#include "weaponspeed.h"
#include "autosave.h"
#include "camera.h"
#include "shot.h"
#include "fov.h"
#include "maskscan.h"
#include "perf.h"

static bool s_ready = false;
static bool s_failed = false;
static HWND s_wnd = NULL;

typedef const unsigned char*(WINAPI* GetStringFn)(unsigned int);
static void log_gl_identity(void) {
  const HMODULE gl = GetModuleHandleA("opengl32.dll");
  if (!gl) { ek_log("overlay: opengl32.dll not in the process"); return; }
  const GetStringFn get = (GetStringFn)GetProcAddress(gl, "glGetString");
  if (!get) { ek_log("overlay: no glGetString"); return; }
  const unsigned char* ver = get(0x1F02);
  const unsigned char* ren = get(0x1F01);
  ek_log("overlay: GL_VERSION=%s", ver ? (const char*)ver : "(null)");
  ek_log("overlay: GL_RENDERER=%s", ren ? (const char*)ren : "(null)");
}

static bool overlay_init(HDC hdc) {
  s_wnd = WindowFromDC(hdc);
  if (!s_wnd) { ek_log("overlay: WindowFromDC gave no window; cannot init"); return false; }

  log_gl_identity();

  IMGUI_CHECKVERSION();
  if (!ImGui::CreateContext()) { ek_log("overlay: CreateContext failed"); return false; }

  ImGuiIO& io = ImGui::GetIO();
  // No ini file: this is someone else's game directory.
  io.IniFilename = NULL;
  io.LogFilename = NULL;

  // The backend, not SDL, was showing the cursor: ImGui_ImplWin32_UpdateMouseCursor calls ::SetCursor whenever
  // io.MouseDrawCursor is false (imgui_impl_win32.cpp:274), until the next WM_SETCURSOR. NoMouseCursorChange stops
  // that.
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

  if (!ImGui_ImplWin32_Init(s_wnd)) { ek_log("overlay: ImplWin32_Init failed"); return false; }
  if (!ImGui_ImplOpenGL3_Init(NULL)) { ek_log("overlay: ImplOpenGL3_Init failed"); return false; }

  ek_log("overlay: ImGui %s initialised on hwnd=%p", IMGUI_VERSION, (void*)s_wnd);

  config_load();

  input_install(s_wnd);

  // Scanned here, not at load: the engine fills its cvar arrays from eduke32.cfg after DllMain.
  cvars_scan();

  // The crash stash is replayed here only: the earliest point cvar_get_int answers, strictly before camera_frame can
  // reach another ON edge that zeroes those cvars (shot.h's shot_stash_replay).
  shot_replay_stash();

  // The FOV stash beside it, same argument and independent of shot's (fov.h).
  fov_replay_stash();

  // The cross-check: 204 records read proves the scan found cvar-shaped data, not right pointers. These names carry
  // known values from settings.cfg, so a match proves ptr is the live variable; a mismatch is loud.
  static const char* const kCheck[] = {
    "r_borderless", "r_hightile", "r_texfilter", "r_anisotropy", "r_downsize",
    "r_models", "r_texcompr", "r_useindexedcolortextures", "r_pr_lighting",
    "r_pr_shadowcount", "r_pr_shadowdetail", "r_maxfps", "fov", "cl_autoaim",
    "r_maxspritesonscreen", "cl_weaponswitch",
  };
  for (int i = 0; i < (int)(sizeof(kCheck) / sizeof(kCheck[0])); i++) {
    int v = 0;
    const EkCvar* c = cvars_find(kCheck[i]);
    if (!c) { ek_log("cvars:   %-26s NOT FOUND", kCheck[i]); continue; }
    if (!cvar_get_int(kCheck[i], &v)) {
      ek_log("cvars:   %-26s found (flags=0x%X) but not int-readable", kCheck[i], c->flags);
      continue;
    }
    ek_log("cvars:   %-26s = %-6d [%d,%d] at %p", kCheck[i], v, c->vmin, c->vmax, c->ptr);
  }
  return true;
}

void overlay_on_swap(HDC hdc) {
  if (s_failed) return;
  if (!s_ready) {
    if (!overlay_init(hdc)) {
      s_failed = true;
      ek_log("overlay: initialisation failed; overlay disabled for this session");
      return;
    }
    s_ready = true;
  }

  // The hunt starts here, before any early-out; it used to start inside the panel block, so nothing began unless the
  // panel opened.
  gamestate_scan();

  // The engine-calling scans (cansee, hitscan, updatesector) run here only: their self-tests write clipsectorlist[] and
  // other engine scratch, so they need the game thread with the tic finished, never the scan worker (cansee.h,
  // hitscan.h).
  cansee_scan();

  hitscan_scan();

  updatesector_scan();

  // G_DrawRooms's locate reads the image and calls nothing; a refusal costs the free camera only.
  camera_scan();

  // Patches go in from here only: the writer is the thread that runs the patched functions (P_PreFireHitscan,
  // A_ShootWithZvel, G_DrawRooms, G_DisplayRest), and here it is provably not inside them. Never the window thread or
  // scan worker.
  gamehooks_install();

  // Unconditional so the Settings switch knows whether it can act; one flag read per frame afterwards.
  camera_install();

  shot_install();

  // The world-quad draw point: inert until the sampler publishes a site.
  maskscan_install();

  // The per-frame game-thread steps below run with the tic just finished and before the early-out, so they work with
  // the panel shut: camera, FOV, thumb buttons (the only instant the bit can be added, between P_GetInput and
  // dukeFillInputForTic), weapon speed, timed autosave.
  camera_frame();

  fov_frame();

  mousebind_frame();

  weaponspeed_frame();

  autosave_frame();

  // Tracer casts ahead of tbot_frame, whose impact scan needs this frame's shots in the ring first; ek_hitscan may be
  // called here, not from the detour (hitscan.h).
  tracer_frame();

  // The ESP draws with the panel shut; the early-out skips only the PANEL. Screenshot mode turns our overlays off
  // through this one gate. It does NOT stand the bots down (shot_active excludes the panel by design): each bot carries
  // its own camera term (freecam_bots_run, in aim.cpp and tbot.cpp).
  const bool wantEsp = gamestate_ready() && !shot_hides_overlays(shot_active_now());
  if (!input_panel_open() && !wantEsp) return;

  perf_zone_begin(EK_PZ_OVERLAY_BUILD);
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplWin32_NewFrame();

  // Our cursor, pushed after the backend's, which queues GetCursorPos (the constant warp point under SDL relative
  // mode).
  {
    ImGuiIO& io = ImGui::GetIO();
    // Only while the panel is open: the ESP begins frames with the panel shut.
    io.MouseDrawCursor = input_panel_open();
    float mx = 0.0f, my = 0.0f;
    input_mouse_pos(&mx, &my);
    io.AddMousePosEvent(mx, my);
  }

  ImGui::NewFrame();

  // The canvas opens before anything draws through it, or every primitive is a silent no-op.
  extern void canvas_set_drawlist(ImDrawList* dl);
  extern void uibridge_begin_frame(void);

  const ImGuiIO& cio = ImGui::GetIO();
  canvas_set_drawlist(ImGui::GetBackgroundDrawList());
  canvas_begin((int)cio.DisplaySize.x, (int)cio.DisplaySize.y);
  uibridge_begin_frame();

  // Before the ESP, so boxes show where the view ended up this frame.
  aim_frame();

  // Immediately after, and the order is the contract: the triggerbot reads the lock aim_frame publishes and clears at
  // the top of every call.
  tbot_frame();

  // Its own statement: an out-param is never read in the argument list that fills it.
  int espNotDrawn = 0, espRefused = 0, espWallOverflow = 0;
  perf_zone_begin(EK_PZ_OVERLAY_ESP);
  const int espCount = wantEsp ? esp_draw(&espNotDrawn, &espRefused, &espWallOverflow) : 0;
  // Behind wantEsp, the screenshot gate, not the ESP row: with the panel open the early-out does not fire, so these
  // would draw into screenshot mode's clean frame.
  if (wantEsp) {
    aim_draw_marker();
    tracer_draw();
  }
  perf_zone_end(EK_PZ_OVERLAY_ESP, espCount);

  // Inside this file's canvas frame; unconditional so the diagnostics counters zero when the gate closes.
  dmgnum_draw(wantEsp);

  // The "You" marker, last so nothing sits on it. Behind wantEsp (a screenshot must not show it), not g_cfg.esp (a
  // flying player with the ESP off still needs it).
  if (wantEsp) camera_draw_player_marker();
  esp_draw_teleport(wantEsp ? 1 : 0, (int)cio.DisplaySize.x, (int)cio.DisplaySize.y);
  // The panel last, over the ESP.
  if (input_panel_open())
    menu_draw((int)cio.DisplaySize.x, (int)cio.DisplaySize.y);

  // Closed before perf_draw so the readout is not inside its own measurement.
  perf_zone_end(EK_PZ_OVERLAY_BUILD, 1);

  perf_zone_begin(EK_PZ_OVERLAY_PERF);
  perf_draw();
  perf_zone_end(EK_PZ_OVERLAY_PERF, 1);

  canvas_end();
  canvas_set_drawlist(NULL);

  if (!input_panel_open()) {
    perf_zone_begin(EK_PZ_OVERLAY_PANEL);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    perf_zone_end(EK_PZ_OVERLAY_PANEL, 1);
    return;
  }

  perf_zone_begin(EK_PZ_OVERLAY_PANEL);
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  perf_zone_end(EK_PZ_OVERLAY_PANEL, 1);
}
