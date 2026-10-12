#pragma once
#include "ui.h"

// Menu's view of input; distinct from src/ekmod/input.h, which fills g_ui.

extern UiInput g_ui;

// SDL button numbers, as in the sibling ports.
const int MOUSE_BUTTON_MIN = 1;
const int MOUSE_BUTTON_MAX = 5;
