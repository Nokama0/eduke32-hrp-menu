#pragma once

// The four per-call zones are LAST: the detail gate is one comparison.
enum {
  EK_PZ_ENGINE = 0,      // the SUBSTITUTED orig() call: the camera's own view
  // Every orig() that hands the frame back unsubstituted, for whatever reason.
  EK_PZ_ENGINE_PLAIN,
  EK_PZ_SWEEP_SCAN,      // pass one, the extent/slab/chain loop
  EK_PZ_SWEEP_FRUSTUM,   // cam_build_frustum
  EK_PZ_SWEEP_LOOP,      // the whole pass-two loop, inner zones included
  EK_PZ_SWEEP_SPRITES,   // the sprite pass
  EK_PZ_OVERLAY_BUILD,
  EK_PZ_OVERLAY_ESP,     // esp_draw and the markers beside it
  EK_PZ_OVERLAY_QUADS,   // the world-quad flush
  EK_PZ_OVERLAY_PANEL,   // the ImGui render
  EK_PZ_OVERLAY_PERF,
  EK_PZ_SWEEP_CULL,      // cam_sector_offscreen
  EK_PZ_SWEEP_US,        // polymer_updatesector
  EK_PZ_SWEEP_DS,        // polymer_drawsector
  EK_PZ_SWEEP_UW,        // polymer_updatewall
  EK_PZ_SWEEP_DW,        // polymer_drawwall
  EK_PERF_ZONE_COUNT
};

#define EK_PERF_DETAIL2_FIRST EK_PZ_SWEEP_CULL
#define EK_PERF_RING 256

// Bisect mask: ini-only, since it draws the map wrong on purpose.
#define EK_PERF_SKIP_SPRITES 1
#define EK_PERF_SKIP_WALLS   2
#define EK_PERF_SKIP_UPDATES 4
#define EK_PERF_SKIP_NOCULL  8
#define EK_PERF_SKIP_ALL     15

inline const char* ekm_perf_zone_name(int zone) {
  static const char* const names[EK_PERF_ZONE_COUNT] = {
    "engine", "engine.plain", "sweep.scan", "sweep.frustum", "sweep.loop",
    "sweep.sprites",
    "overlay.build", "overlay.esp", "overlay.quads", "overlay.panel",
    "overlay.perf",
    "sweep.cull", "sweep.us", "sweep.ds", "sweep.uw", "sweep.dw"
  };
  return (zone >= 0 && zone < EK_PERF_ZONE_COUNT) ? names[zone] : "?";
}

inline int ekm_perf_zone_admits(int zone, int detail2) {
  if (zone < 0 || zone >= EK_PERF_ZONE_COUNT) return 0;
  return (zone < EK_PERF_DETAIL2_FIRST || detail2 != 0) ? 1 : 0;
}

typedef struct EkPerfZone {
  long long ticks;   // accumulated this frame
  long long start;   // the open sample's stamp; meaningless while open == 0
  int calls;         // begin/end pairs completed this frame
  int items;         // whatever the zone counts: sectors, walls, sprites
  int open;          // 1 between a begin and its end
} EkPerfZone;

inline EkPerfZone ekm_perf_zone_zero(void) {
  EkPerfZone z = { 0, 0, 0, 0, 0 };
  return z;
}

// A begin over an open zone keeps the FIRST stamp (outer span).
inline void ekm_perf_zone_begin(EkPerfZone* z, long long now) {
  if (!z || z->open) return;
  z->start = now;
  z->open = 1;
}

// An end with no begin adds nothing: the sweep runs under __try (camera.cpp:797) and an unwind skips the end, so a lost
// pair must cost exactly one sample and never corrupt the total.
inline void ekm_perf_zone_end(EkPerfZone* z, long long now, int items) {
  if (!z || !z->open) return;
  z->open = 0;
  const long long d = now - z->start;
  if (d > 0) z->ticks += d;
  z->calls++;
  z->items += items;
}

inline double ekm_perf_ms(long long ticks, long long freq) {
  return (freq > 0 && ticks > 0) ? (double)ticks * 1000.0 / (double)freq : 0.0;
}

typedef struct EkPerfRing {
  long long v[EK_PERF_RING];
  int cap;     // <= EK_PERF_RING; the suite drives a small one
  int count;   // filled slots, capped at cap
  int next;    // where the next push goes
} EkPerfRing;

inline EkPerfRing ekm_perf_ring_init(int cap) {
  EkPerfRing r;
  r.cap = (cap > 0 && cap <= EK_PERF_RING) ? cap : EK_PERF_RING;
  r.count = 0;
  r.next = 0;
  for (int i = 0; i < EK_PERF_RING; i++) r.v[i] = 0;
  return r;
}

inline void ekm_perf_ring_push(EkPerfRing* r, long long x) {
  if (!r || r->cap <= 0) return;
  r->v[r->next] = x;
  r->next = (r->next + 1) % r->cap;
  if (r->count < r->cap) r->count++;
}

inline int ekm_perf_rank_index(int permille, int n) {
  if (n <= 0) return -1;
  if (permille <= 0) return 0;
  if (permille >= 1000) return n - 1;
  int idx = (permille * n + 999) / 1000 - 1;
  if (idx < 0) idx = 0;
  if (idx > n - 1) idx = n - 1;
  return idx;
}

// The caller owns `scratch`, which must hold the whole ring: a scratch too
// small refuses rather than overrunning.
inline long long ekm_perf_ring_select(const EkPerfRing* r, int permille,
                                      long long* scratch, int scratchLen) {
  if (!r || r->count <= 0 || !scratch || scratchLen < r->count) return 0;
  const int n = r->count;
  for (int i = 0; i < n; i++) scratch[i] = r->v[i];
  // Insertion sort: n <= EK_PERF_RING, once per report.
  for (int i = 1; i < n; i++) {
    const long long k = scratch[i];
    int j = i - 1;
    while (j >= 0 && scratch[j] > k) { scratch[j + 1] = scratch[j]; j--; }
    scratch[j + 1] = k;
  }
  const int idx = ekm_perf_rank_index(permille, n);
  return (idx >= 0) ? scratch[idx] : 0;
}

inline long long ekm_perf_ring_max(const EkPerfRing* r) {
  if (!r || r->count <= 0) return 0;
  long long m = r->v[0];
  for (int i = 1; i < r->count; i++) if (r->v[i] > m) m = r->v[i];
  return m;
}

inline long long ekm_perf_ring_mean(const EkPerfRing* r) {
  if (!r || r->count <= 0) return 0;
  long long sum = 0;
  for (int i = 0; i < r->count; i++) sum += r->v[i];
  return sum / (long long)r->count;
}

// What one begin/end pair costs, in TENTHS of a tick, so a pair cheaper than
// one tick is not rounded away.
inline long long ekm_perf_pair_cost_tenths(long long spanTicks, int pairs) {
  if (pairs <= 0 || spanTicks <= 0) return 0;
  return (spanTicks * 10) / (long long)pairs;
}

inline long long ekm_perf_overhead_ticks(long long pairCostTenths, int calls) {
  if (pairCostTenths <= 0 || calls <= 0) return 0;
  return (pairCostTenths * (long long)calls) / 10;
}

inline int ekm_perf_skip_sane(int mask) { return mask & EK_PERF_SKIP_ALL; }

inline int ekm_perf_skips(int mask, int bit) {
  if (bit != EK_PERF_SKIP_SPRITES && bit != EK_PERF_SKIP_WALLS &&
      bit != EK_PERF_SKIP_UPDATES && bit != EK_PERF_SKIP_NOCULL) return 0;
  return (ekm_perf_skip_sane(mask) & bit) ? 1 : 0;
}

// ---- the live instrument, in perf.cpp (test_host links the pure half only) ----
#ifndef EK_PERF_PURE_ONLY
void perf_zone_begin(int zone);
void perf_zone_end(int zone, int items);
void perf_frame_end(void);
int  perf_detail2(void);
int  perf_skip_mask(void);
int  perf_ready(void);
long long perf_freq(void);
long long perf_zone_ticks(int zone);
int       perf_zone_calls(int zone);
int       perf_zone_items(int zone);
long long perf_zone_stat(int zone, int permille);
long long perf_zone_mean(int zone);
int       perf_zone_samples(int zone);
long long perf_overhead_ticks_now(void);
void      perf_draw(void);
#endif
