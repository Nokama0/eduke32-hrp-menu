#pragma once

// World-space quads drawn translucent in the engine's frame; producers push, one flush draws and empties. The rules are
// pure and the caller owns the storage because worldquad.cpp does not link into test_host.

// Corners are double in X and Y: lattice rounding tilts a wall-sprite quad's plane through the sprite's. The bias breaks
// a tie with EXACTLY coplanar walls; too large makes a room read as a SOLID BOX (World Tour wtmod/worldwt.h:13-33) and
// ordinary walls get no engine offset (polymer.cpp:1632-1637), so -1/-2 (Megaton's pair). If wrong, halve or double.
const float EK_WORLD_OFFSET_FACTOR = -1.0f;
const float EK_WORLD_OFFSET_UNITS = -2.0f;

struct EkWorldQuad {
  double x[4], y[4];
  int z[4];
  // Colours are 0xAARRGGBB (canvas.h).
  unsigned argb;
};

// Room for one frame's locks (six faces each) and secret rooms (one quad per wall, capped at EK_SECVOL_WALLS_MAX = 512
// by secretvol.h): 512 plus 126 for the locks. A box stays whole because the tint asks ekm_world_quad_room for all six
// first. worldquad_refused is the instrument: nonzero means 642 is wrong.
const int EK_WORLD_QUAD_MAX = 642;

// Never negative, so `room > 0` cannot be fooled by an over-full list.
inline int ekm_world_quad_room(int count, int max) {
  if (max <= 0) return 0;
  if (count <= 0) return max;
  if (count >= max) return 0;
  return max - count;
}

// Returns the new count, or `count` unchanged when full: overwriting the last slot would flicker between locks.
inline int ekm_world_quad_append(EkWorldQuad* list, int count, int max,
                                 const EkWorldQuad& q) {
  if (!list || max <= 0 || count < 0 || count >= max) return count;
  list[count] = q;
  return count + 1;
}

// Producers collect in the SwapBuffers hook at the END of the frame G_DrawRooms drew (esp.cpp:68, camera.cpp:1055), so
// a flush inside G_DrawRooms draws the previous frame. The producer resets at its own pass head because that flush does
// not run in every mode.
void worldquad_reset(void);

// Whether there is a draw point, pushed in by the patch owner; without one nothing drains the list, so pushes are
// dropped. Default false.
void worldquad_set_drawpoint(bool ok);
// Quads dropped for want of a draw point, kept apart from worldquad_refused so the two causes are not confused.
int worldquad_no_drawpoint(void);

void worldquad_push(const EkWorldQuad& q);
int worldquad_count(void);
// The most quads one collect pass pushed past the cap; nonzero means EK_WORLD_QUAD_MAX is too small.
int worldquad_refused(void);
void worldquad_flush(void);
