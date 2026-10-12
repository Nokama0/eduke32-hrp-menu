#pragma once

// Shot tracers: our predicted ray (exact, always drawn) and the engine's actual impact (the SHOTSPARK1 spawned by
// Proj_InsertShotspark, player.cpp:428); divergence is the bug. The actual line is a heuristic pairing (oldest unpaired
// shot within EKM_TRACER_PAIR_MS) drawn only when paired.

// 64 shots, oldest overwritten: draw cost stays independent of fire rate.
const int EKM_TRACER_MAX = 64;

const unsigned EKM_TRACER_FADE_MS = 10000;

// The spark spawns in the firing tic and is scanned at the next swap; 250 ms is narrow enough that an unsparked shot
// cannot steal the next pull's.
const unsigned EKM_TRACER_PAIR_MS = 250;

// Ints, not bools: MSVC 19.50 /O2 loses a bool member of a struct an inline function returns.
struct EkTracer {
  int ox, oy, oz;
  int px, py, pz;
  int ax, ay, az;
  int haveActual;  // 0 until a pairing was made
  unsigned ms;
};

struct EkTracerRing {
  EkTracer slot[EKM_TRACER_MAX];
  int count;
  int next;
};

inline void ekm_tracer_reset(EkTracerRing* r) {
  if (!r) return;
  r->count = 0;
  r->next = 0;
}

// Overwrite, never refuse: a dropped new shot would show the burst from a minute ago.
inline void ekm_tracer_add(EkTracerRing* r, const EkTracer& t) {
  if (!r) return;
  r->slot[r->next] = t;
  r->next = (r->next + 1) % EKM_TRACER_MAX;
  if (r->count < EKM_TRACER_MAX) r->count++;
}

// An index, not a pointer: pairing writes through it, drawing reads, and one rule cannot drift.
inline int ekm_tracer_index(const EkTracerRing* r, int i) {
  if (!r || i < 0 || i >= r->count) return -1;
  int k = r->next - 1 - i;
  while (k < 0) k += EKM_TRACER_MAX;
  return k;
}

inline int ekm_tracer_alpha(unsigned ageMs) {
  if (ageMs >= EKM_TRACER_FADE_MS) return 0;
  return 255 - (int)((ageMs * 255u) / EKM_TRACER_FADE_MS);
}

// Unsigned: exact across the wallclock wrap, and an impact stamped BEFORE the shot is refused.
inline bool ekm_tracer_pair_ok(unsigned shotMs, unsigned impactMs, unsigned windowMs) {
  return (unsigned)(impactMs - shotMs) <= windowMs;
}

bool tracer_impacts_wanted(void);

// From the P_PreFireHitscan detour (tic side), after the original filled shootAng and zvel. Nothing is cast here:
// ek_hitscan writes clipsectorlist, so only the SwapBuffers hook may run it (tracer_frame).
void tracer_note_shot(int x, int y, int z, int shootAng, int zvel);

// `ours` is 0 for a round somebody else fired (turret); those never pair.
void tracer_note_impact(int x, int y, int z, int ours);

// SwapBuffers hook only, BEFORE the impact scan, so this frame's shots are in the ring first.
void tracer_frame(void);

int tracer_draw(void);

// One world-space segment, near-clipped and drawn (defined in tracer.cpp); wholly behind returns false.
// `aFront`/`bFront` say which ends are real: a clipped end lies on the near plane, so no mark may claim to be there.
struct EkProjection;
struct EkWorldLine { float ax, ay, bx, by; int aFront, bFront; };

bool worldline_draw(const EkProjection& p, int x0, int y0, int z0,
                    int x1, int y1, int z1, unsigned colour, EkWorldLine* out);
