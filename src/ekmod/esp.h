#pragma once

// Draws the ESP from the render thread inside SwapBuffers; returns markers drawn. `notDrawn` counts gathered markers
// not drawn, `refused` the share the collection ceiling turned away (a silent drop is a hazard). All outputs zeroed on
// every path; null accepted.
int esp_draw(int* notDrawn, int* refused, int* wallOverflow);

int esp_combination_seen(void);
int esp_combination_pushed(void);
int esp_combination_boxed(void);
int esp_combination_quads(void);

int esp_secretvol_staged(void);
int esp_secretvol_drawn(void);
int esp_secretvol_skipped(void);
int esp_secretvol_unreadable(void);
int esp_secretvol_inexact(void);
int esp_secretvol_quads(void);

// Secret trigger per pass; refused (chain cap) and dropped (scratch capacity) are separate limits (secrettrig.h).
int esp_secrettrig_seen(void);
int esp_secrettrig_staged(void);
int esp_secrettrig_drawn(void);
int esp_secrettrig_refused(void);
int esp_secrettrig_dropped(void);

int esp_secretpath_points(void);
int esp_secretpath_segments(void);
int esp_secretpath_crosses(void);

// Shared so the damage numbers cull on the same distance.
int esp_dist2d_public(int cx, int cy, int x, int y);

// 512 units per metre, as in the sibling ports.
const int BUILD_UNITS_PER_METRE = 512;

// Draw-distance bounds (World Tour's); default is the maximum, since an active cull reads as a broken projection.
const int ESP_DRAWDIST_MIN     = 512;
const int ESP_DRAWDIST_MAX     = 131072;
const int ESP_DRAWDIST_DEFAULT = ESP_DRAWDIST_MAX;

// Level change: sprite indices are reused.
void esp_forget_health(void);

int aim_draw_marker(void);

// The free camera's "You" diamond; gated by the live camera and overlay.cpp's screenshot gate.
int camera_draw_player_marker(void);

bool esp_level_has_secret_exit(void);

// Teleport landing marker and refusal notice via the canvas overlay.cpp opened; the call is unconditional so the
// counters zero when `active` (the screenshot gate) is off.
void esp_draw_teleport(int active, int screenW, int screenH);

int esp_teleport_quads(void);
int esp_teleport_notice(void);
