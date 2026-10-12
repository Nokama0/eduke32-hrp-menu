// Derives &ud from the cvar registry: osdcmds.cpp:1650 registers "fov" as &ud.fov, so subtracting its offset yields the
// struct. user_defs may gain fields, so two cvars must imply the same base: "fov" at 428 and "cl_autoaim"
// (&ud.config.AutoAim, osdcmds.cpp:1598) at 468. Measured on r10669: both give 0x0AD5B60.

#include <windows.h>
#include "log.h"
#include "cvars.h"
#include "udstate.h"

static const unsigned char* s_ud = NULL;
static bool s_resolved = false;

const unsigned char* ud_base(void) {
  if (s_resolved) return s_ud;
  s_resolved = true;

  const EkCvar* fov = cvars_find("fov");
  const EkCvar* aim = cvars_find("cl_autoaim");
  if (!fov || !aim) {
    ek_log("ud: cvar(s) missing (fov=%p cl_autoaim=%p); camera unavailable",
           (void*)fov, (void*)aim);
    return NULL;
  }

  const unsigned char* fromFov = (const unsigned char*)fov->ptr - EK_UD_FOV;
  const unsigned char* fromAim = (const unsigned char*)aim->ptr - EK_UD_CONFIG_AUTOAIM;

  if (fromFov != fromAim) {
    // Disagreement is a refusal: every udstate.h offset would be wrong.
    ek_log("ud: LAYOUT CHANGED -- fov implies %p, cl_autoaim implies %p "
           "(delta %lld). user_defs has moved; refusing to guess.",
           (const void*)fromFov, (const void*)fromAim,
           (long long)(fromAim - fromFov));
    return NULL;
  }

  s_ud = fromFov;
  ek_log("ud: &ud = %p (rva 0x%07X), agreed by fov and cl_autoaim",
         (const void*)s_ud,
         (unsigned)(s_ud - (const unsigned char*)GetModuleHandleA(NULL)));
  return s_ud;
}

bool ud_camera(EkCamera* out) {
  if (!out) return false;
  const unsigned char* ud = ud_base();
  if (!ud) return false;

  out->x = *(const int*)(ud + EK_UD_CAMERAPOS + 0);
  out->y = *(const int*)(ud + EK_UD_CAMERAPOS + 4);
  out->z = *(const int*)(ud + EK_UD_CAMERAPOS + 8);
  out->angFix   = *(const int*)(ud + EK_UD_CAMERAQ16ANG);
  out->horizFix = *(const int*)(ud + EK_UD_CAMERAQ16HORIZ);
  out->sect     = *(const short*)(ud + EK_UD_CAMERASECT);
  out->fov      = *(const int*)(ud + EK_UD_FOV);

  // fix16_t is 16.16; masked because the engine lets the raw value leave 0..2047.
  out->ang = (out->angFix >> 16) & (EK_BUILD_ANGLE_UNITS - 1);
  return true;
}

// Guarded because the base is derived. All six fields or none, so a position never pairs with a stale angle.
bool ud_camera_write(int x, int y, int z, int angFix, int horizFix, int sect) {
  unsigned char* const ud = (unsigned char*)ud_base();
  if (!ud) return false;
  __try {
    *(int*)(ud + EK_UD_CAMERAPOS + 0)   = x;
    *(int*)(ud + EK_UD_CAMERAPOS + 4)   = y;
    *(int*)(ud + EK_UD_CAMERAPOS + 8)   = z;
    *(int*)(ud + EK_UD_CAMERAQ16ANG)    = angFix;
    *(int*)(ud + EK_UD_CAMERAQ16HORIZ)  = horizFix;
    *(short*)(ud + EK_UD_CAMERASECT)    = (short)sect;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

// Guarded: this runs inside a five-byte code patch in the renderer's call path where a fault has nowhere to go; failure
// means "no answer" and the player's view is drawn.
bool ud_camera_guarded(EkCamera* out) {
  if (!out) return false;
  __try {
    return ud_camera(out);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

// Guarded like every engine read added after ud_camera.
bool ud_paused(bool* out) {
  if (!out) return false;
  const unsigned char* const ud = ud_base();
  if (!ud) return false;
  __try {
    *out = *(const short*)(ud + EK_UD_PAUSE_ON) != 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

// Only plausibility can cross-check a field 1348 bytes in: overhead_on is 0, 1 or 2 and show_help 0 or 1 (game.cpp);
// anything else means the layout moved.
bool ud_map_or_help(bool* out) {
  if (!out) return false;
  const unsigned char* const ud = ud_base();
  if (!ud) return false;
  int overhead = 0, help = 0;
  __try {
    overhead = *(const char*)(ud + EK_UD_OVERHEAD_ON);
    help     = *(const char*)(ud + EK_UD_SHOW_HELP);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  if (overhead < 0 || overhead > 2 || help < 0 || help > 1) return false;
  // Only overhead_on == 2 suppresses the draw; 1 is the corner map.
  *out = (overhead == 2) || (help != 0);
  return true;
}
