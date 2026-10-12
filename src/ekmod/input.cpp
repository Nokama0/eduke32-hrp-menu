// Panel input: the window proc is subclassed (SDL2 is static with no exports but receives ordinary window messages), so
// a swallowed message never reaches the game. The mouse is ours: SDL's relative mode warps the real cursor, so we
// accumulate WM_INPUT deltas into our own position. Window thread only; interlocked scalars cross to the render thread.

#include <windows.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "log.h"
#include "input.h"
#include "config.h"
#include "camera.h"
#include "campos.h"
#include "clipboard.h"
#include "ui/menu.h"
#include "ui/bind.h"
#include "ui/entry.h"
#include "gamecfg.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static WNDPROC s_origProc = NULL;
static HWND s_wnd = NULL;
static volatile LONG s_open = 0;

// Virtual cursor in client pixels; window thread writes, render reads scalars, so a torn read costs a frame of stale
// coordinate.
static volatile LONG s_mouseX = 0;
static volatile LONG s_mouseY = 0;

// Thumb buttons' held state for mousebind.cpp. Fed from BOTH message sources: RIDEV_NOLEGACY suppresses WM_XBUTTON* and
// this game's Windows layer registers the mouse that way (rawinput.cpp:210), while the shipped SDL build's registration
// was not read. The raw path costs a GetRawInputData per move, so it is gated on a bound button.
static volatile LONG s_thumb[2] = { 0, 0 };

// Which mouse source is live now, not ever seen: raw wins while arriving (the cursor is being warped). It was a
// never-reset latch: the pause menu leaves relative mode (menus.cpp:5520, sdlayer.cpp:1284) but the latch kept the
// WM_MOUSEMOVE fallback off and the cursor froze. Aged by a stamp, and not reset on open.
static volatile LONG s_sawRawInput = 0;
static volatile LONG s_lastRawMs = 0;

static const DWORD EK_RAW_LIVE_MS = 1000;

// Debug counts, rolled at a ceiling so they cannot overflow when nothing drains them.
static volatile LONG s_rawCount = 0;
static volatile LONG s_moveCount = 0;
static const LONG EK_DEBUG_COUNT_MAX = 1000000;

static void bump_debug_count(volatile LONG* c) {
  if (InterlockedIncrement(c) > EK_DEBUG_COUNT_MAX) InterlockedExchange(c, 0);
}

static bool raw_input_is_live(void) {
  if (!InterlockedCompareExchange(&s_sawRawInput, 0, 0)) return false;
  const DWORD last = (DWORD)InterlockedCompareExchange(&s_lastRawMs, 0, 0);
  return (DWORD)(GetTickCount() - last) < EK_RAW_LIVE_MS;
}

// The panel key is the player's; Insert when unmappable or unset, so the panel stays reachable.
static int panel_key(void) {
  const int vk = config_bind_get(0);
  return vk ? vk : VK_INSERT;
}

static int cam_key(void) { return config_bind_get(BIND_ROW_CAMERA); }

static int cam_copy_key(void)  { return config_bind_get(BIND_ROW_CAMCOPY); }
static int cam_paste_key(void) { return config_bind_get(BIND_ROW_CAMPASTE); }

static int cam_teleport_key(void) { return config_bind_get(BIND_ROW_TELEPORT); }

// Who owns the keyboard, so who owes the game a release. A second claim would forge releases for keys the game never
// saw pressed (opening the panel mid-flight with W held did); freecam.h's ekm_input_owned_edge owns the rule.
static LONG s_modOwnsInput = 0;

static bool camera_owns_input(void);

static bool claim_input(bool nowOwned) {
  const LONG now = nowOwned ? 1 : 0;
  const bool need = ekm_input_owned_edge(s_modOwnsInput, now);
  s_modOwnsInput = now;
  return need;
}

// Is this a key the mod swallows? release_held_keys must never forge a release for one. The eight flight keys are ours
// per window (camera_owns_input), so the claim keeps the invariant.
static bool key_is_ours(int vk) {
  if (vk == panel_key()) return true;
  if (g_cfg.freecamon == 0 || !camera_available()) return false;
  const int keys[3] = { cam_key(), cam_copy_key(), cam_paste_key() };
  for (int i = 0; i < 3; i++) if (keys[i] != 0 && vk == keys[i]) return true;
  return false;
}

static const unsigned kRawButtonDown = 0x0001u | 0x0004u | 0x0010u | 0x0040u | 0x0100u;
static const unsigned kRawButtonUp   = 0x0002u | 0x0008u | 0x0020u | 0x0080u | 0x0200u;

bool input_panel_open(void) { return InterlockedCompareExchange(&s_open, 0, 0) != 0; }

bool input_thumb_down(int which) {
  if (which < 0 || which > 1) return false;
  return InterlockedCompareExchange(&s_thumb[which], 0, 0) != 0;
}

void input_mouse_pos(float* x, float* y) {
  if (x) *x = (float)InterlockedCompareExchange(&s_mouseX, 0, 0);
  if (y) *y = (float)InterlockedCompareExchange(&s_mouseY, 0, 0);
}

// Instrument for the missing cursor: separates a frozen cursor from an ImGui input-queue race. Takes and zeroes the
// counts.
void input_debug_take(int* rawSeen, int* rawLive, int* rawCount, int* moveCount) {
  if (rawSeen)   *rawSeen   = InterlockedCompareExchange(&s_sawRawInput, 0, 0) ? 1 : 0;
  if (rawLive)   *rawLive   = raw_input_is_live() ? 1 : 0;
  if (rawCount)  *rawCount  = (int)InterlockedExchange(&s_rawCount, 0);
  if (moveCount) *moveCount = (int)InterlockedExchange(&s_moveCount, 0);
}

static void clamp_cursor_to_client(void) {
  RECT rc = {0, 0, 0, 0};
  if (!s_wnd || !GetClientRect(s_wnd, &rc)) return;
  LONG x = InterlockedCompareExchange(&s_mouseX, 0, 0);
  LONG y = InterlockedCompareExchange(&s_mouseY, 0, 0);
  if (x < 0) x = 0; else if (x > rc.right) x = rc.right;
  if (y < 0) y = 0; else if (y > rc.bottom) y = rc.bottom;
  InterlockedExchange(&s_mouseX, x);
  InterlockedExchange(&s_mouseY, y);
}

// Tell the game every held key went up, once, at open: WM_KEYUP is swallowed, so SDL never learns and Duke keeps
// walking. Once at the transition, not by letting WM_KEYUP through (unpaired releases). Window thread: GetKeyboardState
// reports the calling thread's queue.
static void release_held_keys(void) {
  if (!s_origProc || !s_wnd) return;
  BYTE state[256];
  if (!GetKeyboardState(state)) return;

  int keys = 0;

  // Mouse buttons go through SendInput: a forged WM_LBUTTONUP never reaches SDL, which reads buttons from raw input in
  // relative mode.
  INPUT inj[5];
  int buttons = 0;
  static const struct { int vk; DWORD flag; DWORD data; } kBtn[] = {
    { VK_LBUTTON,  MOUSEEVENTF_LEFTUP,   0 },
    { VK_RBUTTON,  MOUSEEVENTF_RIGHTUP,  0 },
    { VK_MBUTTON,  MOUSEEVENTF_MIDDLEUP, 0 },
    { VK_XBUTTON1, MOUSEEVENTF_XUP,      XBUTTON1 },
    { VK_XBUTTON2, MOUSEEVENTF_XUP,      XBUTTON2 },
  };
  for (int i = 0; i < (int)(sizeof(kBtn) / sizeof(kBtn[0])); i++) {
    if (!(state[kBtn[i].vk] & 0x80)) continue;
    ZeroMemory(&inj[buttons], sizeof(INPUT));
    inj[buttons].type = INPUT_MOUSE;
    inj[buttons].mi.dwFlags = kBtn[i].flag;
    inj[buttons].mi.mouseData = kBtn[i].data;
    buttons++;
  }
  if (buttons) {
    const UINT sent = SendInput((UINT)buttons, inj, sizeof(INPUT));
    if (sent != (UINT)buttons)
      ek_log("input: SendInput released %u of %d button(s) (%lu)", sent, buttons,
             GetLastError());
  }

  for (int vk = 0; vk < 256; vk++) {
    if (!(state[vk] & 0x80)) continue;
    if (key_is_ours(vk)) continue;
    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
        vk == VK_XBUTTON1 || vk == VK_XBUTTON2) continue;

    // A real release: scancode in bits 16-23, bits 30 and 31 set, or SDL may not read it as genuine.
    const UINT sc = MapVirtualKeyA((UINT)vk, 0);
    const LPARAM lp = (LPARAM)((sc << 16) | 0x1u) | (LPARAM)0xC0000000;
    CallWindowProcW(s_origProc, s_wnd, WM_KEYUP, (WPARAM)vk, lp);
    keys++;
  }

  if (keys || buttons)
    ek_log("input: released %d key(s) and %d mouse button(s) on open", keys, buttons);
}

static void panel_set(bool open) {
  InterlockedExchange(&s_open, open ? 1 : 0);
  if (open) {
    gamecfg_load();

    // Before anything else: from here every key message is swallowed. Only on the FIRST claim, as opening mid-flight is
    // the camera handing over.
    if (claim_input(true)) {
      release_held_keys();

      InterlockedExchange(&s_thumb[0], 0);
      InterlockedExchange(&s_thumb[1], 0);
    }

    // Seeded at the centre: in relative mode the real cursor is the warp point.
    RECT rc = {0, 0, 0, 0};
    if (s_wnd && GetClientRect(s_wnd, &rc)) {
      InterlockedExchange(&s_mouseX, rc.right / 2);
      InterlockedExchange(&s_mouseY, rc.bottom / 2);
    }
  }
  // Written on close, not per change (a drag writes many times a second).
  if (!open) {
    claim_input(camera_owns_input());
    menu_keycap_close();
    // The typed field closes with the panel, or it would eat keys with nothing on screen.
    menu_entry_close();
    config_write_changed();
  }

  ek_log("input: panel %s", open ? "opened" : "closed");
}

static bool is_input_message(UINT m) {
  switch (m) {
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
    case WM_CHAR: case WM_SYSCHAR: case WM_DEADCHAR: case WM_UNICHAR:
    case WM_MOUSEMOVE: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
    case WM_INPUT:
      return true;
    default:
      return false;
  }
}

static bool raw_mouse(LPARAM lParam, RAWMOUSE* out) {
  UINT size = 0;
  if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &size,
                      sizeof(RAWINPUTHEADER)) != 0) return false;
  if (size == 0 || size > sizeof(RAWINPUT) * 4) return false;

  BYTE buf[sizeof(RAWINPUT) * 4];
  if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf, &size,
                      sizeof(RAWINPUTHEADER)) != size) return false;

  const RAWINPUT* ri = (const RAWINPUT*)buf;
  if (ri->header.dwType != RIM_TYPEMOUSE) return false;
  *out = ri->data.mouse;
  return true;
}

static unsigned accumulate_raw(LPARAM lParam) {
  RAWMOUSE m;
  if (!raw_mouse(lParam, &m)) return 0;
  InterlockedExchange(&s_sawRawInput, 1);
  InterlockedExchange(&s_lastRawMs, (LONG)GetTickCount());

  // Transitions returned so the caller decides what the game sees; read before the absolute-mode bail.
  const unsigned btn = m.usButtonFlags;

  // MOUSE_MOVE_ABSOLUTE is not a delta; left alone.
  if (m.usFlags & MOUSE_MOVE_ABSOLUTE) return btn;

  InterlockedExchangeAdd(&s_mouseX, m.lLastX);
  InterlockedExchangeAdd(&s_mouseY, m.lLastY);
  clamp_cursor_to_client();
  return btn;
}

// DBLCLK counts as down: it arrives instead of the second XBUTTONDOWN.
static void note_thumb(UINT msg, WPARAM w) {
  const WORD which = GET_XBUTTON_WPARAM(w);
  const int idx = (which == XBUTTON1) ? 0 : (which == XBUTTON2 ? 1 : -1);
  if (idx < 0) return;
  InterlockedExchange(&s_thumb[idx], msg == WM_XBUTTONUP ? 0 : 1);
}

static bool thumbs_are_bound(void) {
  return g_cfg.mouse4action != 0 || g_cfg.mouse5action != 0;
}

static void note_thumb_raw(LPARAM lParam) {
  RAWMOUSE m;
  if (!raw_mouse(lParam, &m)) return;
  const unsigned btn = m.usButtonFlags;

  if (btn & RI_MOUSE_BUTTON_4_DOWN) InterlockedExchange(&s_thumb[0], 1);
  if (btn & RI_MOUSE_BUTTON_4_UP)   InterlockedExchange(&s_thumb[0], 0);
  if (btn & RI_MOUSE_BUTTON_5_DOWN) InterlockedExchange(&s_thumb[1], 1);
  if (btn & RI_MOUSE_BUTTON_5_UP)   InterlockedExchange(&s_thumb[1], 0);
}


// Key ownership is one chain with no overlap: panel, then camera, then game, each swallowing the same is_input_message
// set. One bitmask for the eight keys (read at one instant), two motion counters, a toggle-edge count.
enum {
  FCK_FWD  = 1 << 0, FCK_BACK = 1 << 1, FCK_LEFT = 1 << 2, FCK_RIGHT = 1 << 3,
  FCK_UP   = 1 << 4, FCK_DOWN = 1 << 5, FCK_FAST = 1 << 6, FCK_SLOW  = 1 << 7,
};

// Fixed keys, not eight more bind rows. VK_SHIFT/CONTROL/MENU are what WM_KEYDOWN carries; Alt arrives as
// WM_SYSKEYDOWN.
static const struct { int vk; LONG bit; } kCamKeys[] = {
  { 'W', FCK_FWD },       { 'S', FCK_BACK },        { 'A', FCK_LEFT },
  { 'D', FCK_RIGHT },     { VK_SPACE, FCK_UP },     { VK_CONTROL, FCK_DOWN },
  { VK_SHIFT, FCK_FAST }, { VK_MENU, FCK_SLOW },
};

static volatile LONG s_camKeys = 0;
static volatile LONG s_camDx = 0;
static volatile LONG s_camDy = 0;
static volatile LONG s_camToggle = 0;

static volatile LONG s_camTeleport = 0;

static LONG s_camSeenActive = 0;

// The panel wins: the camera gets zeroed input while it is up.
static bool camera_owns_input(void) {
  return camera_active() && !input_panel_open();
}

// The handover runs here because GetKeyboardState reports the calling thread's queue. Noticed on the first message
// after the flag moves. Nothing is owed going down: keys pressed in flight were swallowed.
static void cam_sync_active(void) {
  const LONG now = camera_owns_input() ? 1 : 0;
  if (now == s_camSeenActive) return;
  s_camSeenActive = now;

  InterlockedExchange(&s_camKeys, 0);
  InterlockedExchange(&s_camDx, 0);
  InterlockedExchange(&s_camDy, 0);
  // An ownership-scoped latch: a press banked as the camera lands must not fire on a later flight.
  InterlockedExchange(&s_camTeleport, 0);
  if (claim_input(now ? true : input_panel_open())) {
    release_held_keys();
    InterlockedExchange(&s_thumb[0], 0);
    InterlockedExchange(&s_thumb[1], 0);
  }
  ek_log("input: the free camera %s the keyboard and the mouse",
         now ? "has taken" : "has given back");
}

static void cam_note_key(WPARAM w, bool down) {
  for (int i = 0; i < (int)(sizeof(kCamKeys) / sizeof(kCamKeys[0])); i++) {
    if ((int)w != kCamKeys[i].vk) continue;
    if (down) InterlockedOr(&s_camKeys, kCamKeys[i].bit);
    else      InterlockedAnd(&s_camKeys, ~kCamKeys[i].bit);
    return;
  }
}

// WM_MOUSEMOVE is no fallback here: in play SDL warps the cursor, so its delta is the warp.
static unsigned cam_note_raw(LPARAM lParam) {
  RAWMOUSE m;
  if (!raw_mouse(lParam, &m)) return 0;
  InterlockedExchange(&s_sawRawInput, 1);
  InterlockedExchange(&s_lastRawMs, (LONG)GetTickCount());
  const unsigned btn = m.usButtonFlags;
  if (m.usFlags & MOUSE_MOVE_ABSOLUTE) return btn;
  InterlockedExchangeAdd(&s_camDx, m.lLastX);
  InterlockedExchangeAdd(&s_camDy, m.lLastY);
  return btn;
}

// Both run on the window thread because the clipboard belongs to it (clipboard.h); copy takes a seqlock snapshot, paste
// leaves a latch for the game thread. The format is campos.h's.

// The line always goes in the log: a key that seems to do nothing is the likeliest failure.
static void chord_copy_view(void) {
  FreecamPose p;
  if (!camera_viewpoint_read(&p)) {
    ek_log("input: no viewpoint has been published yet, so nothing was copied");
    return;
  }
  char line[96];
  if (!campos_format(p, line, (int)sizeof line)) {
    ek_log("input: the viewpoint did not fit its own line; nothing copied");
    return;
  }
  const bool ok = clipboard_write_text(line);
  ek_log("input: viewpoint %s the clipboard: %s",
         ok ? "copied to" : "NOT copied to", line);
}

static void chord_paste_view(void) {
  char text[1200];
  FreecamPose want;
  if (!clipboard_read_text(text, (int)sizeof text) ||
      !campos_accept(text, &want)) {
    // Nothing moves on a bad paste.
    ek_log("input: the clipboard holds no plausible "
           "\"cam x= y= z= ang= pitch=\" line; the camera is left where it is");
    return;
  }
  camera_request_pose(want);
  ek_log("input: viewpoint taken from the clipboard, (%d,%d,%d) ang %d horiz %d; "
         "the next frame flies there", want.x, want.y, want.z, want.ang, want.horiz);
}

// The typed numeric field's keys, on the window thread: entry.h is pure, so the clipboard read/write happens here and
// only text crosses.
static void entry_route_key(WPARAM w) {
  int sc = 0;
  if (!vk_to_entry_scancode((int)w, &sc)) return;
  const bool shift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
  const bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  if (ctrl && (int)w == 'V') {
    char text[ENTRY_PASTE_CAP + 8];
    if (clipboard_read_text(text, (int)sizeof text)) menu_entry_paste(text);
    return;
  }
  const char* clip = menu_entry_key_mod(sc, shift, ctrl);
  if (clip) clipboard_write_text(clip);
}

int input_freecam_toggle_take(void) {
  return (int)InterlockedExchange(&s_camToggle, 0);
}

int input_teleport_take(void) {
  return (int)InterlockedExchange(&s_camTeleport, 0);
}

bool input_freecam_state(FreecamInput* out) {
  if (!out) return false;

  // Drained even when the camera does not own input, so panel-time motion is never handed back.
  const int dx = (int)InterlockedExchange(&s_camDx, 0);
  const int dy = (int)InterlockedExchange(&s_camDy, 0);
  const LONG keys = InterlockedCompareExchange(&s_camKeys, 0, 0);
  const bool owns = camera_owns_input();

  out->dx    = owns ? dx : 0;
  out->dy    = owns ? dy : 0;
  out->fwd   = owns && (keys & FCK_FWD)   != 0;
  out->back  = owns && (keys & FCK_BACK)  != 0;
  out->left  = owns && (keys & FCK_LEFT)  != 0;
  out->right = owns && (keys & FCK_RIGHT) != 0;
  out->up    = owns && (keys & FCK_UP)    != 0;
  out->down  = owns && (keys & FCK_DOWN)  != 0;
  out->fast  = owns && (keys & FCK_FAST)  != 0;
  out->slow  = owns && (keys & FCK_SLOW)  != 0;
  return owns;
}

static LRESULT CALLBACK ek_WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
  // First: the handover must precede acting on this message.
  cam_sync_active();

  // The toggle, de-repeated (bit 30 of lParam). A bind row waiting for a key gets it first, so the current panel key
  // can be rebound.
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && menu_keycap_armed()) {
    if ((l & (1 << 30)) == 0) menu_keycap_bind((int)w);
    return 0;
  }

  // An open typed field takes the keyboard whole: the panel key does not close the panel; Escape cancels, a click away
  // commits. Not de-repeated: a held digit keeps typing.
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && input_panel_open() && menu_entry_open()) {
    entry_route_key(w);
    return 0;
  }

  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && (int)w == panel_key()) {
    if ((l & (1 << 30)) == 0) panel_set(!input_panel_open());
    return 0;
  }

  // The camera toggle: de-repeated, swallowed like the panel key, recorded only while the row is on. camera_available()
  // too: a persisted freecamon=1 on a build whose locate refused would eat this key.
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && g_cfg.freecamon != 0 &&
      camera_available() && cam_key() != 0 && (int)w == cam_key()) {
    if ((l & (1 << 30)) == 0) InterlockedIncrement(&s_camToggle);
    return 0;
  }

  // The clipboard keys sit ABOVE the camera_owns_input block, which would eat them.
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && g_cfg.freecamon != 0 &&
      camera_available()) {
    const int copyKey = cam_copy_key();
    const int pasteKey = cam_paste_key();
    if (copyKey != 0 && (int)w == copyKey) {
      if ((l & (1 << 30)) == 0) chord_copy_view();
      return 0;
    }
    if (pasteKey != 0 && (int)w == pasteKey) {
      if ((l & (1 << 30)) == 0) chord_paste_view();
      return 0;
    }
  }

  // The teleport key, above the flight block for the same reason, de-repeated. Ours only while the camera owns the
  // keyboard, so absent from key_is_ours.
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && camera_owns_input()) {
    const int tpKey = cam_teleport_key();
    if (tpKey != 0 && (int)w == tpKey) {
      if ((l & (1 << 30)) == 0) InterlockedIncrement(&s_camTeleport);
      return 0;
    }
  }

  if (!input_panel_open() && !camera_owns_input()) {
    if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP || msg == WM_XBUTTONDBLCLK)
      note_thumb(msg, w);
    else if (msg == WM_INPUT && thumbs_are_bound())
      note_thumb_raw(l);
  }

  if (camera_owns_input()) {
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) { cam_note_key(w, true);  return 0; }
    if (msg == WM_KEYUP   || msg == WM_SYSKEYUP)   { cam_note_key(w, false); return 0; }
    if (msg == WM_INPUT) {
      // Motion swallowed, button RELEASES not: cam_sync_active SendInput'ed releases that return as raw packets, and
      // swallowing them leaves SDL thinking the trigger is down all flight.
      const unsigned btn = cam_note_raw(l);
      if ((btn & kRawButtonUp) && !(btn & kRawButtonDown))
        return CallWindowProcW(s_origProc, h, msg, w, l);
      return 0;
    }
    if (is_input_message(msg)) return 0;
  }

  if (input_panel_open()) {
    if (msg == WM_INPUT)          bump_debug_count(&s_rawCount);
    else if (msg == WM_MOUSEMOVE) bump_debug_count(&s_moveCount);

    if (msg == WM_INPUT) {
      const unsigned btn = accumulate_raw(l);

      // Motion swallowed, releases not: SDL takes buttons from raw input in relative mode, so swallowing all WM_INPUT
      // left the gun firing. Releases only; presses would fire on panel clicks.
      if ((btn & kRawButtonUp) && !(btn & kRawButtonDown))
        return CallWindowProcW(s_origProc, h, msg, w, l);

      return 0;
    }

    if (msg == WM_MOUSEMOVE && !raw_input_is_live()) {
      InterlockedExchange(&s_mouseX, (short)LOWORD(l));
      InterlockedExchange(&s_mouseY, (short)HIWORD(l));
    }

    ImGui_ImplWin32_WndProcHandler(h, msg, w, l);

    if (is_input_message(msg)) return 0;
  }

  return CallWindowProcW(s_origProc, h, msg, w, l);
}

bool input_install(HWND wnd) {
  if (!wnd) return false;
  if (s_origProc) return true;
  s_wnd = wnd;

  s_origProc = (WNDPROC)SetWindowLongPtrW(wnd, GWLP_WNDPROC, (LONG_PTR)ek_WndProc);
  if (!s_origProc) {
    ek_log("input: SetWindowLongPtrW failed (%lu) -- panel cannot take input", GetLastError());
    return false;
  }
  ek_log("input: WndProc subclassed on hwnd=%p, original=%p, panel key 0x%02X",
         (void*)wnd, (void*)s_origProc, panel_key());
  return true;
}

void input_remove(void) {
  if (s_origProc && s_wnd) {
    SetWindowLongPtrW(s_wnd, GWLP_WNDPROC, (LONG_PTR)s_origProc);
    s_origProc = NULL;
  }
}
