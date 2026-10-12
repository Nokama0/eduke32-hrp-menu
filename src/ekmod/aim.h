#pragma once

// Once per frame from the overlay hook only: ek_cansee may run only inside SwapBuffers.
void aim_frame(void);

bool aim_available(void);

// Solution for the best target, solved from the muzzle at the shot: a pre-solved angle goes stale. False (shot
// untouched) when the point is missing or stale or the pitch exceeds aimsolve.h's SHOT bound.
// _at takes the caller's muzzle; q16horizoff is passed since the engine subtracts it separately.
bool aim_solution_at(int muzzleX, int muzzleY, int muzzleZ, int q16horizoff,
                     int* q16ang, int* q16horiz);

bool aim_solution_for_shot(int* q16ang, int* q16horiz);

struct AimLock {
  int sprite;       // engine sprite index of the target
  int yaw;          // Build units, the direction to it from the eye
  int q16horiz;     // the q16horiz that would face it, less q16horizoff
  int myYaw;        // the player's own yaw that frame, Build units
  int myQ16Horiz;   // the player's own q16horiz that frame
  // Triggerbot hold-fire rule: marginZ is z to the nearer edge of the visible run; both zero = no measurement.
  int marginZ;
  int range2d;
  // The engine's pitch offset that frame: q16horiz has it subtracted and the engine adds it back; rounding needs both.
  int q16horizoff;
  int spanLo;
  int spanHi;
  int spanMid;
  int spanHeight;
};

// False when nothing was acquired on the last drawn frame or that frame is too old (ekm_shot_fresh).
bool aim_lock(AimLock* out);

// The calibration marker's world points, published beside the lock, not in AimLock (which must stay un-led for the
// triggerbot).
struct AimMarkerPoints {
  int sprite;
  int rawX, rawY, rawZ;
  int ledX, ledY, ledZ;
};
bool aim_marker_points(AimMarkerPoints* out);

// Clock behind ekm_shot_fresh (wallclock_ms is too coarse for one frame).
unsigned aim_clock_ms(void);
