#pragma once
#include <windows.h>
#include "freecam.h"   // FreecamInput, which this file fills from its own scalars

bool input_install(HWND wnd);

void input_remove(void);

// Window thread writes, render thread reads: interlocked scalars only.
bool input_panel_open(void);
void input_mouse_pos(float* x, float* y);

void input_debug_take(int* rawSeen, int* rawLive, int* rawCount, int* moveCount);

// ---- Free camera's share of this window proc: not a second input path ----

// This tic's camera input from interlocked scalars; mouse deltas are drained on every call.
bool input_freecam_state(FreecamInput* out);

int input_freecam_toggle_take(void);

int input_teleport_take(void);

// Held state of a thumb button: 0 is mouse 4 (XBUTTON1), 1 is mouse 5 (XBUTTON2).
bool input_thumb_down(int which);
