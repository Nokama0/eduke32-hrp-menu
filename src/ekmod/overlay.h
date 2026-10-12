#pragma once
#include <windows.h>

// Render thread; disables itself if init fails.
void overlay_on_swap(HDC hdc);
