#pragma once
#include "freecam.h"   // FreecamPose: what the bracket reads and the tic writes

// The free camera brackets renderDrawRoomsQ16 (polymer_drawrooms is inlined into it, so its preamble runs with OUR
// arguments); G_DrawRooms is located too since its extent says which of ten callers the bracket is in. It substitutes
// ud.camera* and never writes the player struct.

bool camera_scan(void);

// True once BOTH functions have been located.
bool camera_ready(void);

unsigned camera_rva(void);
unsigned camera_roomsQ16_rva(void);


bool camera_install(void);

void camera_remove(void);

bool camera_active(void);

// One tic of the camera on the game thread: publishes the pose (the seqlock's only writer).
void camera_tic(void);

// One presented frame: gates, entry/exit, publish. The step runs inside the detour on ototalclock so step and
// smoothRatio share one clock.
void camera_frame(void);

// Did the detour actually go in?
bool camera_installed(void);

// Installed AND still reached, observed from the bracketed draw and re-proved because the renderer can change
// mid-session.
bool camera_available(void);

// The whole-map sweep is a separate answer: it needs four more located functions, and is false after one fault.
bool camera_wholemap_available(void);

// Sweep parts differ: geometry needs the sweep; actors need the inlined polymer_scansprites chains; the sky redirect
// needs the actors.
bool camera_sprites_available(void);
bool camera_sky_available(void);

// Remove fog needs g_visibility, traced from the located drawrooms; false after one failed read or write.
bool camera_fog_available(void);

bool camera_transport_available(void);

// The PAIR of poses (before the last tic, and now) under one seqlock: a frame draws BETWEEN them, blended by
// smoothRatio.
int camera_pose_read(FreecamPose* prev, FreecamPose* cur);

// ---- THE VIEWPOINT, OUT AND BACK -------------------------------------------

// A copy takes the camera's pose while flying and the PLAYER's otherwise, so comparisons work with the camera down. 0
// before anything was published.
int camera_viewpoint_read(FreecamPose* out);

// A pasted viewpoint is LATCHED from the window thread, not applied: camera_frame is the only writer. It is a
// discontinuity, so the previous-tic pose collapses onto it.
void camera_request_pose(const FreecamPose& pose);

// ---- WHERE THE TELEPORT WOULD GO, AND WHAT THE LAST ONE DID ----------------


int camera_teleport_plan_read(EkTeleportPlan* plan, int* have);

// The last teleport's words, clock and `have` under ONE seqlock read, so a draw never pairs one teleport's words with
// another's clock.
int camera_teleport_outcome(EkTeleportPlan* plan, unsigned* setMs, int* have);

// ---- ONE SECTOR'S BAND AT ONE POINT, SHARED RATHER THAN RE-IMPLEMENTED -----

// Ceiling and floor z of `sectnum` at (x, y); `b->exact` is 0 when the flat pair stands in for a slope, and ignoring it
// gives flat geometry for a sloped sector. Also called from secretvol.cpp's scan thread: a fault on either thread
// zeroes the shared s_getzRva and disables the slope call for both.
struct EkSectorInfo;
bool cam_band_fill(int sectnum, int x, int y, FreecamBand* b, EkSectorInfo* si);
