// Shot tracers (tracer.h): queue each engine round, cast our prediction at the next swap, pair it with the impact
// sprite. tracer_note_shot runs in the P_PreFireHitscan detour: six ints and a timestamp, no calls. The rest runs in
// the swap on one thread, so no interlock.

#include <windows.h>
#include <math.h>
#include "tracer.h"
#include "config.h"
#include "gamestate.h"
#include "playerstate.h"
#include "hitscan.h"
#include "shotray.h"
#include "project.h"
#include "projmath.h"
#include "ui/canvas.h"
#include "log.h"
#include "wallclock.h"

// A shotgun blast is seven rounds; past 32 pending, rounds are counted and dropped rather than overwriting uncast ones.
const int EKM_TRACER_PENDING_MAX = 32;

struct EkPendingShot {
  int x, y, z;
  int shootAng;
  int zvel;
  unsigned ms;
};

static EkPendingShot s_pending[EKM_TRACER_PENDING_MAX];
static int s_pendingCount = 0;
static unsigned s_droppedPending = 0;
static unsigned s_droppedNoAnswer = 0;
static bool s_saidDropped = false;

static EkTracerRing s_ring;
static bool s_ringReady = false;

bool tracer_impacts_wanted(void) { return g_cfg.tracers != 0; }

void tracer_note_shot(int x, int y, int z, int shootAng, int zvel) {
  if (g_cfg.tracers == 0) return;
  if (s_pendingCount >= EKM_TRACER_PENDING_MAX) { s_droppedPending++; return; }
  EkPendingShot& p = s_pending[s_pendingCount++];
  p.x = x; p.y = y; p.z = z;
  p.shootAng = shootAng;
  p.zvel = zvel;
  p.ms = wallclock_ms();
}

void tracer_frame(void) {
  if (g_cfg.tracers == 0) {
    // Dropped with the row, or old rounds would be cast against a newer world.
    s_pendingCount = 0;
    return;
  }

  if (!s_ringReady) { ekm_tracer_reset(&s_ring); s_ringReady = true; }

  // A level change invalidates every ring coordinate; the rule is ekm_level_loading (playerstate.h). Two statements:
  // argument evaluation order is unspecified, so `gm` must be filled before it is read.
  int gm = 0;
  const bool gmOk = player_read_gm(&gm);
  if (ekm_level_loading(gm, gmOk)) {
    ekm_tracer_reset(&s_ring);
    s_pendingCount = 0;
    return;
  }

  const int pending = s_pendingCount;
  s_pendingCount = 0;
  if (pending <= 0) return;
  if (!hitscan_ready() || !gamestate_ready()) { s_droppedNoAnswer += pending; return; }

  // Starting sector hint only; ek_hitscan corrects it (hitscan.h). Read once per drain.
  PlayerSnap me;
  if (!player_snap(&me)) { s_droppedNoAnswer += pending; return; }

  for (int i = 0; i < pending; i++) {
    const EkPendingShot& p = s_pending[i];

    // player.cpp:556: sintable[(shootAng + 512) & 2047] across, sintable[shootAng & 2047] along, zvel << 6 down.
    const int vx = ekm_sintable(p.shootAng + 512);
    const int vy = ekm_sintable(p.shootAng);
    const int vz = p.zvel << 6;

    EkHitData hit;
    if (!ek_hitscan(p.x, p.y, p.z, me.sect, vx, vy, vz, &hit)) {
      // No answer is not a prediction: nothing is drawn, and the impact may pair with a neighbouring shot.
      s_droppedNoAnswer++;
      continue;
    }

    EkTracer t;
    t.ox = p.x; t.oy = p.y; t.oz = p.z;
    t.px = hit.x; t.py = hit.y; t.pz = hit.z;
    t.ax = 0; t.ay = 0; t.az = 0;
    t.haveActual = 0;
    t.ms = p.ms;
    ekm_tracer_add(&s_ring, t);
  }

  // Logged once, only when something was lost.
  if (!s_saidDropped && (s_droppedPending || s_droppedNoAnswer)) {
    s_saidDropped = true;
    ek_log("tracer: %u round(s) dropped at the queue and %u for want of a "
           "hitscan answer; those shots have no predicted line and are not drawn",
           s_droppedPending, s_droppedNoAnswer);
  }
}

// Best effort: the oldest unpaired shot in the window wins (sparks spawn in firing order), and a shot pairs at most
// once, or one spark would claim a whole burst.
void tracer_note_impact(int x, int y, int z, int ours) {
  if (g_cfg.tracers == 0 || !s_ringReady || ours == 0) return;

  const unsigned now = wallclock_ms();
  for (int i = s_ring.count - 1; i >= 0; i--) {
    const int k = ekm_tracer_index(&s_ring, i);
    if (k < 0) continue;
    EkTracer& t = s_ring.slot[k];
    if (t.haveActual) continue;
    if (!ekm_tracer_pair_ok(t.ms, now, EKM_TRACER_PAIR_MS)) continue;
    t.ax = x; t.ay = y; t.az = z;
    t.haveActual = 1;
    return;
  }
}

const int EKM_TRACER_MARK = 2;

static void tracer_mark(float sx, float sy, unsigned colour) {
  canvas_rect((int)sx - EKM_TRACER_MARK, (int)sy - EKM_TRACER_MARK,
              EKM_TRACER_MARK * 2 + 1, EKM_TRACER_MARK * 2 + 1, colour);
}

// One clipped world-space segment, shared with the ESP's secret-trigger line (tracer.h).
bool worldline_draw(const EkProjection& p, int x0, int y0, int z0,
                    int x1, int y1, int z1, unsigned colour, EkWorldLine* out) {
  if (!out) return false;
  EkmVec3 a = project_to_eye(p, x0, y0, z0);
  EkmVec3 b = project_to_eye(p, x1, y1, z1);
  const bool aWasFront = ekm_in_front(a);
  const bool bWasFront = ekm_in_front(b);
  if (!ekm_clip_near(&a, &b)) return false;

  float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
  if (!project_eye_point(p, a, &ax, &ay)) return false;
  if (!project_eye_point(p, b, &bx, &by)) return false;

  canvas_line((int)ax, (int)ay, (int)bx, (int)by, colour);
  out->ax = ax; out->ay = ay; out->bx = bx; out->by = by;
  out->aFront = aWasFront ? 1 : 0;
  out->bFront = bWasFront ? 1 : 0;
  return true;
}

// Marks are drawn only for unclipped ends, so a mark never claims a shot ended at the view edge.
static bool tracer_segment(const EkProjection& p, int x0, int y0, int z0,
                           int x1, int y1, int z1, unsigned colour) {
  EkWorldLine ln = { 0.0f, 0.0f, 0.0f, 0.0f, 0, 0 };
  const bool drew = worldline_draw(p, x0, y0, z0, x1, y1, z1, colour, &ln);
  if (!drew) return false;
  if (ln.aFront) tracer_mark(ln.ax, ln.ay, colour);
  if (ln.bFront) tracer_mark(ln.bx, ln.by, colour);
  return true;
}

int tracer_draw(void) {
  if (g_cfg.tracers == 0 || !s_ringReady) return 0;
  if (s_ring.count <= 0) return 0;

  EkProjection p;
  if (!project_begin(&p)) return 0;

  const unsigned now = wallclock_ms();
  int drawn = 0;

  for (int i = 0; i < s_ring.count; i++) {
    const int k = ekm_tracer_index(&s_ring, i);
    if (k < 0) continue;
    const EkTracer& t = s_ring.slot[k];

    // BREAK, not continue: the ring is in insertion order, so older entries follow the first one past ten seconds. The
    // unsigned difference survives clock wrap.
    const int alpha = ekm_tracer_alpha((unsigned)(now - t.ms));
    if (alpha <= 0) break;

    bool any = tracer_segment(p, t.ox, t.oy, t.oz, t.px, t.py, t.pz,
                              canvas_with_alpha(canvas_tracer_predicted_colour(), alpha));

    if (t.haveActual)
      any = tracer_segment(p, t.ox, t.oy, t.oz, t.ax, t.ay, t.az,
                           canvas_with_alpha(canvas_tracer_actual_colour(), alpha)) || any;

    if (any) drawn++;
  }
  return drawn;
}
