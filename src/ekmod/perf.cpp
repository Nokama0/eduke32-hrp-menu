#include <windows.h>
#include <cstdio>
#include "perf.h"
#include "config.h"
#include "log.h"
#include "ui/canvas.h"
#include "ui/font.h"

// Plain ints on purpose: zone groups A and B run on the game thread; the scan worker must never touch a zone.
static EkPerfZone s_zone[EK_PERF_ZONE_COUNT];
static EkPerfRing s_ring[EK_PERF_ZONE_COUNT];
static long long  s_freq = 0;
static int        s_ready = 0;
static int        s_tried = 0;
static long long  s_pairCostTenths = 0;
static int        s_calibrated = 0;
static int        s_frameCalls = 0;
static unsigned   s_lastDumpMs = 0;

static long long perf_now(void) {
  LARGE_INTEGER n;
  if (!QueryPerformanceCounter(&n)) return 0;
  return (long long)n.QuadPart;
}

static void perf_init_once(void) {
  if (s_tried) return;
  s_tried = 1;
  LARGE_INTEGER f;
  if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0) {
    ek_log("perf: QueryPerformanceFrequency failed, frame timings are off");
    return;
  }
  s_freq = (long long)f.QuadPart;
  for (int i = 0; i < EK_PERF_ZONE_COUNT; i++) {
    s_zone[i] = ekm_perf_zone_zero();
    s_ring[i] = ekm_perf_ring_init(EK_PERF_RING);
  }
  s_ready = 1;
}

int  perf_ready(void) { return s_ready; }
long long perf_freq(void) { return s_freq; }
// Gated on perfzones too: the sub-row only dims when the parent is off, so ~4600 QPC pairs a frame kept running and
// could not be turned off.
int  perf_detail2(void) { return (g_cfg.perfzones && g_cfg.perfdetail2) ? 1 : 0; }
int  perf_skip_mask(void) { return ekm_perf_skip_sane(g_cfg.perfskip); }

void perf_zone_begin(int zone) {
  if (!s_ready || !ekm_perf_zone_admits(zone, perf_detail2())) return;
  ekm_perf_zone_begin(&s_zone[zone], perf_now());
}

void perf_zone_end(int zone, int items) {
  if (!s_ready || !ekm_perf_zone_admits(zone, perf_detail2())) return;
  ekm_perf_zone_end(&s_zone[zone], perf_now(), items);
  s_frameCalls++;
}

long long perf_zone_ticks(int zone) {
  return (s_ready && zone >= 0 && zone < EK_PERF_ZONE_COUNT) ? s_zone[zone].ticks : 0;
}

int perf_zone_calls(int zone) {
  return (s_ready && zone >= 0 && zone < EK_PERF_ZONE_COUNT) ? s_zone[zone].calls : 0;
}

int perf_zone_items(int zone) {
  return (s_ready && zone >= 0 && zone < EK_PERF_ZONE_COUNT) ? s_zone[zone].items : 0;
}

long long perf_zone_stat(int zone, int permille) {
  if (!s_ready || zone < 0 || zone >= EK_PERF_ZONE_COUNT) return 0;
  long long scratch[EK_PERF_RING];
  return ekm_perf_ring_select(&s_ring[zone], permille, scratch, EK_PERF_RING);
}

long long perf_zone_mean(int zone) {
  return (s_ready && zone >= 0 && zone < EK_PERF_ZONE_COUNT)
             ? ekm_perf_ring_mean(&s_ring[zone]) : 0;
}

// Ring depth, not this frame's calls: detail-2 zones can show a p99 over a handful of samples.
int perf_zone_samples(int zone) {
  return (s_ready && zone >= 0 && zone < EK_PERF_ZONE_COUNT)
             ? s_ring[zone].count : 0;
}

long long perf_overhead_ticks_now(void) {
  return ekm_perf_overhead_ticks(s_pairCostTenths, s_frameCalls);
}

// 4096 empty pairs once, on the first frame the readout is on (never at load: 0.1 ms is a visible hitch). Timed through
// the live entry points; the inlined helpers understated cost by 10-20% (no /GL).
static void perf_calibrate_once(void) {
  if (s_calibrated || !s_ready) return;
  s_calibrated = 1;
  const int savedCalls = s_frameCalls;
  // Saved, not zeroed: perf_frame_end runs after the drawrooms bracket has recorded its real sample; zeroing would feed
  // the ring a false zero.
  const EkPerfZone savedZone = s_zone[EK_PZ_ENGINE];
  const long long t0 = perf_now();
  for (int i = 0; i < 4096; i++) {
    perf_zone_begin(EK_PZ_ENGINE);
    perf_zone_end(EK_PZ_ENGINE, 0);
  }
  const long long t1 = perf_now();
  const long long span = t1 - t0;
  s_pairCostTenths = ekm_perf_pair_cost_tenths(span, 4096);
  s_zone[EK_PZ_ENGINE] = savedZone;
  s_frameCalls = savedCalls;
  ek_log("perf: one zone pair costs %lld/10 ticks (%.4f ms per 1000 pairs)",
         s_pairCostTenths, ekm_perf_ms(s_pairCostTenths * 100, s_freq));
}

// One call site, in ek_SwapBuffers, which has no early return; overlay_on_swap has four and would publish some frames
// only.
void perf_frame_end(void) {
  perf_init_once();
  if (!s_ready) return;
  if (g_cfg.perfzones) perf_calibrate_once();
  const int detail2 = perf_detail2();
  // Every five seconds while on, pasteable. Above the zeroing loop: below it every value reads zero.
  if (g_cfg.perfzones) {
    const unsigned now = GetTickCount();
    if ((unsigned)(now - s_lastDumpMs) >= 5000u) {
      s_lastDumpMs = now;
      ek_log("perf: --- frame timings, mean/p50/p99/max ms ---");
      for (int i = 0; i < EK_PERF_ZONE_COUNT; i++) {
        if (!ekm_perf_zone_admits(i, detail2)) continue;
        // calls and items are this frame's; samples is the ring depth behind the stats.
        ek_log("perf: %-14s %6.2f %6.2f %6.2f %6.2f  calls %d items %d samples %d",
               ekm_perf_zone_name(i),
               ekm_perf_ms(perf_zone_mean(i), s_freq),
               ekm_perf_ms(perf_zone_stat(i, 500), s_freq),
               ekm_perf_ms(perf_zone_stat(i, 990), s_freq),
               ekm_perf_ms(perf_zone_stat(i, 1000), s_freq),
               perf_zone_calls(i), perf_zone_items(i), perf_zone_samples(i));
      }
      // Named bits so a pasted log reads without the game.
      const int skip = perf_skip_mask();
      if (skip) {
        char skipbuf[64];
        snprintf(skipbuf, sizeof(skipbuf), "%s%s%s%s",
                 ekm_perf_skips(skip, EK_PERF_SKIP_SPRITES) ? "sprites " : "",
                 ekm_perf_skips(skip, EK_PERF_SKIP_WALLS) ? "walls " : "",
                 ekm_perf_skips(skip, EK_PERF_SKIP_UPDATES) ? "updates " : "",
                 ekm_perf_skips(skip, EK_PERF_SKIP_NOCULL) ? "nocull " : "");
        ek_log("perf: instrument %.3f ms over %d pairs, perfskip %d: %s",
               ekm_perf_ms(perf_overhead_ticks_now(), s_freq), s_frameCalls,
               skip, skipbuf);
      } else {
        ek_log("perf: instrument %.3f ms over %d pairs, perfskip none",
               ekm_perf_ms(perf_overhead_ticks_now(), s_freq), s_frameCalls);
      }
    }
  }
  for (int i = 0; i < EK_PERF_ZONE_COUNT; i++) {
    // Pushed only for a zone admitted AND run this frame (calls > 0): skipped brackets banked zeros and diluted the
    // ring for 256 frames.
    if (ekm_perf_zone_admits(i, detail2) && s_zone[i].calls > 0)
      ekm_perf_ring_push(&s_ring[i], s_zone[i].ticks);
    // Zeroed unconditionally so a zone left open by an SEH unwind is discarded.
    s_zone[i] = ekm_perf_zone_zero();
  }
  s_frameCalls = 0;
}

// Excluded from its own measurement.
void perf_draw(void) {
  if (!g_cfg.perfzones || !g_cfg.perftable || !s_ready) return;
  const int detail2 = perf_detail2();
  int y = 8;
  char line[128];
  snprintf(line, sizeof(line),
           "zone            mean    p50    p99    max   calls   items  samples");
  canvas_text(8, y, line, C_BONEFNT, FONT_MONO);
  y += 12;
  for (int i = 0; i < EK_PERF_ZONE_COUNT; i++) {
    if (!ekm_perf_zone_admits(i, detail2)) continue;
    snprintf(line, sizeof(line), "%-14s %6.2f %6.2f %6.2f %6.2f %7d %7d %8d",
             ekm_perf_zone_name(i),
             ekm_perf_ms(perf_zone_mean(i), s_freq),
             ekm_perf_ms(perf_zone_stat(i, 500), s_freq),
             ekm_perf_ms(perf_zone_stat(i, 990), s_freq),
             ekm_perf_ms(perf_zone_stat(i, 1000), s_freq),
             perf_zone_calls(i), perf_zone_items(i), perf_zone_samples(i));
    canvas_text(8, y, line, C_BONEFNT, FONT_MONO);
    y += 12;
  }
  snprintf(line, sizeof(line), "instrument %.3f ms over %d pairs",
           ekm_perf_ms(perf_overhead_ticks_now(), s_freq), s_frameCalls);
  canvas_text(8, y, line, C_BONEDIM, FONT_MONO);
  y += 12;
  // A mask left set must never read as a defect.
  const int skip = perf_skip_mask();
  if (skip) {
    snprintf(line, sizeof(line),
             "perfskip %d: the map is drawn wrong on purpose", skip);
    canvas_text(8, y, line, C_HAZARD, FONT_MONO);
  }
}
