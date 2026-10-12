#pragma once

// EDuke32's `user_defs ud` (offsets computed from game.h:161-279). It is internal and may change in any build, so
// ud_base() re-checks the layout every time.
const int EK_UD_CAMERAPOS      = 0;
const int EK_UD_SCREEN_SIZE    = 228;
const int EK_UD_FOV            = 428;
const int EK_UD_CAMERAQ16ANG   = 432;
const int EK_UD_CAMERAQ16HORIZ = 436;
const int EK_UD_CAMERASECT     = 440;
// int16_t, game.h:205: the pause the game's own code tests (player.cpp:2358); gm stays MODE_GAME.
const int EK_UD_PAUSE_ON       = 442;
const int EK_UD_CONFIG_AUTOAIM = 468;

// The two fields deciding whether G_DrawRooms draws the world were read off the shipped image, not transcribed;
// details: docs/re/ud-offsets.md.
const int EK_UD_OVERHEAD_ON    = 1348;
const int EK_UD_SHOW_HELP      = 1356;

const int EK_BUILD_ANGLE_UNITS = 2048;

struct EkCamera {
  int x, y, z;
  int angFix, horizFix;
  int ang;
  int sect;
  int fov;
};

// NULL when the cross-check fails: the struct layout moved and every offset above is suspect.
const unsigned char* ud_base(void);

bool ud_camera(EkCamera* out);

// Writing ud.camera* is inside the "write only what the engine writes" rule: G_DrawRooms rebuilds all six from the
// player every frame (game.cpp:1001-1110) and the draw at :1142 reads them back.

// Not put back, on purpose: overlay, ESP and tracers project from these fields (project.cpp:70). The rebuild is not
// universal (game.cpp:784/800 return early; the camerasprite branch never rebuilds pos or horiz). The 3D audio listener
// reads them too (sounds.cpp:1112), so sound follows the flying camera.

bool ud_camera_write(int x, int y, int z, int angFix, int horizFix, int sect);

bool ud_camera_guarded(EkCamera* out);

// False means "assume paused".
bool ud_paused(bool* out);

// Full automap or help screen up (G_DrawRooms returns at game.cpp:800). Self-checking: an implausible value
// (overhead_on > 2, show_help > 1) means the struct moved and is refused.
bool ud_map_or_help(bool* out);
