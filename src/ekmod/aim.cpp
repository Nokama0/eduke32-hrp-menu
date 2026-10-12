// The aimbot. No hook: drawframe_do (game.cpp:6478-6511) runs P_GetInput (player.cpp:3080, :3084), G_DrawRooms, then
// our SwapBuffers hook, on the game thread after the view angle's only writer. The write lands next frame.

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include "aim.h"
#include "config.h"
#include "targets.h"
#include "gamestate.h"
#include "aimsolve.h"
#include "cansee.h"
#include "hitscan.h"
#include "shotray.h"
#include "playerstate.h"
#include "gamehooks.h"
#include "log.h"
#include "ui/bind.h"
#include "input.h"
#include "mousebind.h"
#include "camera.h"

// One list, one thread (aim_frame runs only from the SwapBuffers hook): no lock, and 11 KB stays off the stack.
static EkTarget s_targets[EK_TARGET_MAX];

static int s_candIdx[EK_TARGET_MAX];
static int s_candDist[EK_TARGET_MAX];

// No probe budget: a 64-probe cap was a hard ceiling on RANGE (nearest enemy at 16849 never looked at). Once per tic
// (EKM_ACQUIRE_GAP_MS) pays for it.

// Rate limit by content: a full span is understood (one line a second); a partial span is an enemy behind cover, worth
// ten times the room.
static const unsigned EK_AIM_SPAN_FULL_MS = 1000;
static const unsigned EK_AIM_SPAN_PARTIAL_MS = 100;

static const int EK_AIM_RUNGS = 4;

// QueryPerformanceCounter, not GetTickCount: at 144 fps a frame (7 ms) is below the 15.6 ms system timer. Negative
// means the clock failed; callers assume one tic per frame.
static double aim_now_ms(void) {
  static double freqMs = 0.0;
  static bool broken = false;
  if (broken) return -1.0;
  if (freqMs == 0.0) {
    LARGE_INTEGER f;
    if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0) {
      broken = true;
      ek_log("aim: QueryPerformanceFrequency failed, smoothing assumes 30 fps");
      return -1.0;
    }
    freqMs = 1000.0 / (double)f.QuadPart;
  }
  LARGE_INTEGER now;
  if (!QueryPerformanceCounter(&now)) {
    broken = true;
    ek_log("aim: QueryPerformanceCounter failed, smoothing assumes 30 fps");
    return -1.0;
  }
  return (double)now.QuadPart * freqMs;
}

// Whole-millisecond stamp, unsigned so differences survive a wrap of either clock.
static unsigned aim_tick_of(double ms) {
  return ms >= 0.0 ? (unsigned)(unsigned long long)ms : GetTickCount();
}

// The stamp every freshness test here uses, published so other instruments date events on the same counter (aim.h).
unsigned aim_clock_ms(void) { return aim_tick_of(aim_now_ms()); }

// Previous position and timestamp per candidate, indexed by sprite slot: a hashed bucket would hand one enemy another's
// motion and aim at empty floor.
struct EkAimSample {
  int x, y, z;
  // Position one tic earlier: the engine's actor[].bpos, from which sprites are drawn (game.cpp:3882).
  int bx, by, bz;
  int vx, vy, vz;
  unsigned lastChangeMs;
  short picnum;
  unsigned char used;
  unsigned char stopped;
};
static EkAimSample s_prev[EK_MAXSPRITES];

// Where the renderer draws this target now, and its velocity in Build units PER TIC. Updated only when the position
// changes and divided by tics, not frames. A first sighting or recycled slot gets zero.
static void aim_track(const EkTarget& t, unsigned nowMs,
                      int* ix, int* iy, int* iz,
                      int* vx, int* vy, int* vz) {
  *ix = t.x; *iy = t.y; *iz = t.z;
  *vx = 0; *vy = 0; *vz = 0;
  if (t.sprite < 0 || t.sprite >= EK_MAXSPRITES) return;
  EkAimSample& s = s_prev[t.sprite];

  if (!s.used || s.picnum != t.picnum) {
    s.used = 1; s.picnum = t.picnum;
    s.x = t.x; s.y = t.y; s.z = t.z;
    s.bx = t.x; s.by = t.y; s.bz = t.z;
    s.vx = 0; s.vy = 0; s.vz = 0;
    s.stopped = 0;
    s.lastChangeMs = nowMs;
    return;
  }

  if (t.x != s.x || t.y != s.y || t.z != s.z) {
    const int tics = ekm_lead_divisor(nowMs - s.lastChangeMs);
    if (tics <= 0) {
      // Too long a gap to call motion: the slot is sampled only while the target survives the cone and ladder, so
      // dividing an unseen walk by the cap threw the aim point into the floor. Treated as a first sighting.
      s.x = t.x; s.y = t.y; s.z = t.z;
      s.bx = t.x; s.by = t.y; s.bz = t.z;
      s.vx = 0; s.vy = 0; s.vz = 0;
      s.stopped = 0;
      s.lastChangeMs = nowMs;
      return;
    }
    s.stopped = 0;
    s.vx = (t.x - s.x) / tics;
    s.vy = (t.y - s.y) / tics;
    // Grounded actors report fake vertical speed (WT target.h:763: 272..320 units per tic led rockets into the floor):
    // vz only when horizontal speed is non-zero.
    s.vz = (s.vx != 0 || s.vy != 0) ? (t.z - s.z) / tics : 0;
    // One tic back, not one observation back: bpos is always one tic back though a candidate can go unobserved for
    // several. z skips the gated s.vz: that rule stops fake z being LED, but the renderer lerps z always.
    s.bx = t.x - s.vx; s.by = t.y - s.vy; s.bz = t.z - (t.z - s.z) / tics;
    s.x = t.x; s.y = t.y; s.z = t.z;
    s.lastChangeMs = nowMs;
  } else if (s.stopped || ekm_velocity_stale(nowMs - s.lastChangeMs)) {
    // A stopped enemy stops being led; the stamp moves every frame (sticky `stopped`), or the first step after a pause
    // divides by the cap.
    s.stopped = 1;
    s.vx = 0; s.vy = 0; s.vz = 0;
    // bpos collapses onto the position: a stopped target interpolates to itself, or the aim point glides back and
    // forth.
    s.bx = s.x; s.by = s.y; s.bz = s.z;
    s.lastChangeMs = nowMs;
  }

  // The engine draws at bpos + smoothratio * (pos - bpos); lastChangeMs is the boundary we observed, so this is the
  // same line.
  const double r = ekm_smoothratio((double)(nowMs - s.lastChangeMs));
  *ix = ekm_lerp_pos(s.bx, s.x, r);
  *iy = ekm_lerp_pos(s.by, s.y, r);
  *iz = ekm_lerp_pos(s.bz, s.z, r);
  *vx = s.vx; *vy = s.vy; *vz = s.vz;
}

// The eye interpolated as the camera is (DukePlayer_t carries opos after pos); a failed opos read leaves the raw
// position.
static void aim_eye_interp(const PlayerSnap& me, unsigned nowMs,
                           int* ex, int* ey, int* ez) {
  *ex = me.x; *ey = me.y; *ez = me.z;

  int ox = 0, oy = 0, oz = 0;
  if (!player_read_opos(&ox, &oy, &oz)) return;

  // One frame of history, render thread only.
  static bool s_seen = false;
  static int s_px = 0, s_py = 0, s_pz = 0;
  static int s_ox = 0, s_oy = 0, s_oz = 0;
  static unsigned s_ticMs = 0;
  if (!s_seen || me.x != s_px || me.y != s_py || me.z != s_pz ||
      ox != s_ox || oy != s_oy || oz != s_oz) {
    s_seen = true;
    s_px = me.x; s_py = me.y; s_pz = me.z;
    s_ox = ox;   s_oy = oy;   s_oz = oz;
    s_ticMs = nowMs;
  }

  const double r = ekm_smoothratio((double)(nowMs - s_ticMs));
  *ex = ekm_lerp_pos(ox, me.x, r);
  *ey = ekm_lerp_pos(oy, me.y, r);
  *ez = ekm_lerp_pos(oz, me.z, r);
}

// Evidence for the MODE_MENU gate: the aimbot still aimed over the game menu (Menu_Open sets the bit, menus.cpp:5509).
// Expected 0x05; 0x04 would mean a wrong offset.
static void aim_log_gate(int gm, bool readOk, bool bailed) {
  if (!g_cfg.debuglog) return;
  // At most one line a second.
  static unsigned lastMs = 0;
  static bool logged = false;
  const unsigned now = GetTickCount();
  if (logged && now - lastMs < 1000) return;
  logged = true; lastMs = now;
  // `bailed`: whether the GATE stopped the aimbot.
  ek_log("aim gate: gm=0x%02X read=%d bailed=%d slotrva=0x%08X",
         readOk ? (unsigned)(gm & 0xFF) : 0u, readOk ? 1 : 0, bailed ? 1 : 0,
         player_rva());
}

bool aim_available(void) { return player_ready() && cansee_ready(); }

// Acquisition's result for the winner: one pass, three answers (view solve, triggerbot's un-led line, silent aim's
// point). The VIEW gets an angle (interpolated, what the player SEES); the SHOT gets a raw POINT (fired a tic later
// from the pos then, player.cpp:1709-1711).
struct EkAimPick {
  int yaw;
  int horiz;
  // No shot angle here: silent aim publishes the POINT and solves at the muzzle that fires. `sprite` and the un-led
  // line are for the triggerbot.
  int sprite;
  int viewYaw;
  int viewHoriz;
  // The marker's raw and led world points, carried out from the shot solve.
  int rawX, rawY, rawZ;
  int ledX, ledY, ledZ;
  // Pass 2's measurement for the hold-fire rule: z margin to the nearer edge of the run and the range. Zero when stage
  // 2 did not run.
  int marginZ;
  int range2d;
  // -1 when pass 2 never set them (aim.h AimLock).
  int spanLo, spanHi, spanMid, spanHeight;
  // The view's fallback: the best candidate whose pitch the VIEW can hold; 0 if none. Read by aim_frame alone.
  int fallbackHave;
  int fallbackYaw, fallbackHoriz, fallbackSprite;
  // The winner's record, kept for the per-frame re-solve (aim_refresh). Copied: s_targets is rebuilt by the next
  // collect.
  EkTarget target;
};

// Silent aim's answer is a world POINT, not an angle: selection needs ek_cansee (SwapBuffers hook only) while
// A_ShootWithZvel runs mid-tic, and an angle went stale on a jetpack (16 of 105 samples landed 2048 high);
// aim_solution_at re-solves at the firing muzzle.
static int s_shotRawX = 0, s_shotRawY = 0, s_shotRawZ = 0;
static int s_shotLedX = 0, s_shotLedY = 0, s_shotLedZ = 0;
static bool s_shotHave = false;
// When it was published: aim_frame runs on the draw, so a stretch with no draw leaves the last point standing
// (ekm_shot_fresh).
static unsigned s_shotMs = 0;

// The triggerbot's lock, separate from the shot solution, which exists only with silent aim on.
static bool s_lockHave = false;
static unsigned s_lockMs = 0;
static int s_lockSprite = -1;
static int s_lockYaw = 0, s_lockHoriz = 0;
static int s_lockMyYaw = 0, s_lockMyHoriz = 0;
// The marker's points, published with the lock (same s_lockHave, s_lockMs): one freshness rule.
static int s_lockRawX = 0, s_lockRawY = 0, s_lockRawZ = 0;
static int s_lockLedX = 0, s_lockLedY = 0, s_lockLedZ = 0;
// Pass 2's placement measurement, published with the lock.
static int s_lockMarginZ = 0, s_lockRange2d = 0;
static int s_lockSpanLo = -1, s_lockSpanHi = -1, s_lockSpanMid = -1,
           s_lockSpanH = 0;
static int s_lockHorizoff = 0;

// Why a candidate never became the answer. A Recon, a Pig Cop and a jetpacking trooper were ignored at distance; the
// probe budget was the cause (46 candidates, 16 probed, farthest accepted 13432, nearest unprobed 16849). The cap is
// gone; the lines stay. Every term reports; an inactive Recon has no blocking bits (actors.cpp:1247).
struct AimFar {
  int dist, sprite, pic;
  // The term's own detail: cstat and xrep for hittable, offset for the cone, rungs for cansee, needed q16horiz and dz
  // for pitch; none for range.
  int d1, d2;
};

// Records a refusal in the term's farthest slot and farthest-ever; `first` mirrors the nearest slots.
static void aim_far_note(AimFar* f, bool first, int* ever, int dist,
                         int sprite, int pic, int d1, int d2) {
  if (dist > *ever) *ever = dist;
  if (!first && dist <= f->dist) return;
  f->dist = dist; f->sprite = sprite; f->pic = pic; f->d1 = d1; f->d2 = d2;
}

struct AimFrameStats {
  int cand;
  int probed;
  int probes;
  int spanProbes;
  int spanHitscan;
  // Each refusal keeps its NEAREST example: the enemy a player complains about is the one in front of them.
  int rangeSkips, rangeDist, rangeSprite, rangePic;
  int coneSkips, coneDist, coneSprite, conePic, coneOff, coneLimit;
  int crosshair;
  // The three terms that used to refuse silently. hittable is the Recon case; the cansee ladder is the standing guess
  // for troopers seen through a broken window.
  int hitSkips, hitDist, hitSprite, hitPic, hitCstat, hitXrep;
  int seeSkips, seeDist, seeSprite, seePic, seeRungs;
  int pitchSkips, pitchDist, pitchSprite, pitchPic, pitchNeed, pitchDz;
  AimFar hitFar, rangeFar, coneFar, seeFar, pitchFar;
  // How far acquisition actually REACHES, the quantity the 32 m report disputes. acceptHave is 0 on a frame that picked
  // nobody.
  int acceptHave, acceptDist, acceptSprite, acceptPic;
};

// Cumulative, never reset, appended to `aim acquire:`, Debug log only: a rate-limited sample says nothing about one
// candidate versus four hundred.
struct AimRefuseTotals {
  unsigned hittable, range, cone, cansee, pitch;
};
static AimRefuseTotals s_refuse = {};

// Cumulative farthest-ever per term plus farthest ACQUIRED: how far out refusals still happened.
struct AimFarTotals {
  int hittable, range, cone, cansee, pitch, accepted;
};
static AimFarTotals s_far = {};

// Appends one term's farthest refusal and farthest-ever; a full buffer stops appending.
static int aim_far_append(char* buf, int cap, int at, const char* name,
                          int skips, const AimFar& f, int ever,
                          const char* detail) {
  if (at < 0 || at >= cap) return at;
  const char* sep = at > 0 ? ", " : "";
  const int n = skips <= 0
      ? snprintf(buf + at, (size_t)(cap - at), "%s%s none (ever %d)",
                 sep, name, ever)
      : snprintf(buf + at, (size_t)(cap - at),
                 "%s%s sprite %d picnum %d at %d%s (ever %d)",
                 sep, name, f.sprite, f.pic, f.dist, detail, ever);
  if (n < 0) return at;
  return at + n >= cap ? cap - 1 : at + n;
}

// One line per reason, each on its own clock, to see whether a reason fires repeatedly while the player looks at
// something.
static void aim_log_frame(const AimFrameStats& st, unsigned nowMs) {
  if (!g_cfg.debuglog) return;
  // Written whenever any term refused or a target was acquired, so the reach shows on clean frames.
  const bool anySkip = st.hitSkips > 0 ||
                       st.rangeSkips > 0 || st.coneSkips > 0 ||
                       st.seeSkips > 0 || st.pitchSkips > 0 ||
                       st.acceptHave > 0;
  if (anySkip) {
    static unsigned lastMs = 0;
    if (lastMs == 0 || (unsigned)(nowMs - lastMs) >= 1000u) {
      lastMs = nowMs;
      char collect[128];
      targets_refusal_summary(collect, sizeof(collect));
      // The mode is on the line: Closest never asks the cone, so a missing `aim skip cone:` line is otherwise
      // ambiguous. The reach is early because ek_log truncates at 1024.
      char accepted[96];
      if (st.acceptHave)
        snprintf(accepted, sizeof(accepted),
                 "sprite %d picnum %d at %d units, farthest ever %d",
                 st.acceptSprite, st.acceptPic, st.acceptDist, s_far.accepted);
      else
        snprintf(accepted, sizeof(accepted), "nothing, farthest ever %d",
                 s_far.accepted);

      // Farthest refusal per term, beside the nearest the `aim skip` lines carry.
      char farthest[512];
      char detail[72];
      int at = 0;
      snprintf(detail, sizeof(detail), ", cstat 0x%04X xrep %d",
               (unsigned)st.hitFar.d1, st.hitFar.d2);
      at = aim_far_append(farthest, (int)sizeof(farthest), at, "hittable",
                          st.hitSkips, st.hitFar, s_far.hittable, detail);
      at = aim_far_append(farthest, (int)sizeof(farthest), at, "range",
                          st.rangeSkips, st.rangeFar, s_far.range, "");
      snprintf(detail, sizeof(detail), ", off %d units", st.coneFar.d1);
      at = aim_far_append(farthest, (int)sizeof(farthest), at, "cone",
                          st.coneSkips, st.coneFar, s_far.cone, detail);
      snprintf(detail, sizeof(detail), ", %d of %d rungs tried",
               st.seeFar.d1, EK_AIM_RUNGS);
      at = aim_far_append(farthest, (int)sizeof(farthest), at, "cansee",
                          st.seeSkips, st.seeFar, s_far.cansee, detail);
      snprintf(detail, sizeof(detail), ", need q16horiz %d whole, dz %d",
               st.pitchFar.d1, st.pitchFar.d2);
      at = aim_far_append(farthest, (int)sizeof(farthest), at, "pitch",
                          st.pitchSkips, st.pitchFar, s_far.pitch, detail);
      // Was `aim budget:`. `never probed` now reads zero every frame, which confirms the ceiling is gone, with
      // `farthest ever` rising past 13432.
      ek_log("aim acquire: %s, %d candidates, %d probed, %d never probed; "
             "accepted %s; %d probes (uncapped, once per tic); pass 2 took "
             "%d of %d (%s); refused so far: hittable %u, "
             "range %u, cone %u, cansee %u, pitch %u; farthest "
             "refused (Build units): %s; "
             "collect: %s",
             st.crosshair ? "crosshair" : "closest",
             st.cand, st.probed, st.cand - st.probed, accepted, st.probes,
             st.spanProbes, EKM_SPAN_BUDGET,
             st.spanHitscan ? "hitscan" : "cansee",
             s_refuse.hittable, s_refuse.range, s_refuse.cone,
             s_refuse.cansee, s_refuse.pitch, farthest, collect);
    }
  }
  if (st.hitSkips > 0) {
    static unsigned lastMs = 0;
    if (lastMs == 0 || (unsigned)(nowMs - lastMs) >= 1000u) {
      lastMs = nowMs;
      // The cstat is the whole answer: a Recon that has not activated reads 0x0000 and gets 0x0101 when
      // G_MoveZombieActors sees the player; a corpse has the blocking bits cleared and 0x8000 set.
      ek_log("aim skip hittable: %d not hittable; nearest sprite %d picnum %d "
             "at %d units, cstat 0x%04X xrep %d (wants (cstat & 0x8100) == "
             "0x0100 and xrep != 0)",
             st.hitSkips, st.hitSprite, st.hitPic, st.hitDist,
             (unsigned)st.hitCstat, st.hitXrep);
    }
  }
  if (st.rangeSkips > 0) {
    static unsigned lastMs = 0;
    if (lastMs == 0 || (unsigned)(nowMs - lastMs) >= 1000u) {
      lastMs = nowMs;
      ek_log("aim skip range: %d past range; nearest sprite %d picnum %d "
             "at %d units, limit %d",
             st.rangeSkips, st.rangeSprite, st.rangePic, st.rangeDist,
             g_cfg.aimrange);
    }
  }
  if (st.coneSkips > 0) {
    static unsigned lastMs = 0;
    if (lastMs == 0 || (unsigned)(nowMs - lastMs) >= 1000u) {
      lastMs = nowMs;
      ek_log("aim skip cone: %d outside the cone; nearest sprite %d picnum %d "
             "at %d units, off %d units (%d deg), limit %d units (%d deg)",
             st.coneSkips, st.coneSprite, st.conePic, st.coneDist,
             st.coneOff, st.coneOff * 360 / EKM_YAW_UNITS,
             st.coneLimit, g_cfg.aimcone);
    }
  }
  if (st.seeSkips > 0) {
    static unsigned lastMs = 0;
    if (lastMs == 0 || (unsigned)(nowMs - lastMs) >= 1000u) {
      lastMs = nowMs;
      // Rungs tried: always EK_AIM_RUNGS now, so a short count means the ladder stopped early.
      ek_log("aim skip cansee: %d with no visible rung; nearest sprite %d "
             "picnum %d at %d units, %d of %d rungs tried and all refused "
             "(eye to body, aim point %d)",
             st.seeSkips, st.seeSprite, st.seePic, st.seeDist,
             st.seeRungs, EK_AIM_RUNGS, g_cfg.aimpoint);
    }
  }
  if (st.pitchSkips > 0) {
    static unsigned lastMs = 0;
    if (lastMs == 0 || (unsigned)(nowMs - lastMs) >= 1000u) {
      lastMs = nowMs;
      // Was `aim drop:`; rate limited like the others, keeping the NEAREST example. The bound is the SHOT's, so a line
      // means geometry no ray can express, not an enemy on a ledge.
      ek_log("aim drop: %d past the shot's pitch; nearest sprite %d picnum %d "
             "at %d units -- need q16horiz %d whole, the shot reaches %d..%d; "
             "dz=%d",
             st.pitchSkips, st.pitchSprite, st.pitchPic, st.pitchDist,
             st.pitchNeed, EKM_SHOT_HORIZ_MIN, EKM_SHOT_HORIZ_MAX, st.pitchDz);
    }
  }
}

// The whole acquisition. Selection uses the eye solve alone: `muzzleZ` reaches pass 2 only, so silent aim cannot
// change which enemy the AIMBOT picks, and the pitch gate is the SHOT's wider bound. WHICH enemy is asked from the eye,
// WHERE to shoot from the muzzle. Filters and cansee keep the raw `me`.
static bool aim_acquire(const PlayerSnap& me, int eyeX, int eyeY, int eyeZ,
                        int muzzleZ, unsigned nowTickMs, int projSpeed,
                        EkAimPick* out) {
  const int count = targets_collect(s_targets, EK_TARGET_MAX);
  if (count <= 0) return false;

  const int myYaw    = (me.ang >> 16) & (EKM_YAW_UNITS - 1);
  const int cone     = g_cfg.aimcone * EKM_YAW_UNITS / 360;
  const bool crosshair = (g_cfg.aimtarget == 0);

  const bool leadThis = projSpeed > 0;

  int probes = 0;
  bool have = false;
  int bestTier = 0, bestOff = 0, bestDist = 0;
  int bestYaw = 0, bestHoriz = 0;
  int bestRawX = 0, bestRawY = 0, bestRawZ = 0, bestRawDist = 0;
  int bestVx = 0, bestVy = 0, bestVz = 0;
  int bestSprite = -1, bestViewYaw = 0, bestViewHoriz = 0;
  // The winner's z-dependent inputs for the pass 2 refinement; only the height moves there (yaw and 2D distances are
  // z-independent).
  int bestCand = -1, bestZShift = 0, bestBaseX = 0, bestBaseY = 0;
  int bestBaseDist = 0, bestAimDist = 0, bestAimYaw = 0;

  // The best candidate the VIEW can hold, beside the winner: selection judges by the SHOT's bound, so the winner's
  // pitch may be clamped away and the aimbot did not turn at all. Read only then.
  bool fbHave = false;
  int fbTier = 0, fbOff = 0, fbDist = 0;
  int fbYaw = 0, fbHoriz = 0, fbSprite = -1;

  // Cheap filters first, so probes are spent on survivors. Bookkeeping counts only, with the Debug row on.
  AimFrameStats stats = {};
  int nCand = 0;
  for (int i = 0; i < count; i++) {
    const EkTarget& t = s_targets[i];

    // The hittable rule is the aimbot's own (targets.h): the ESP must draw enemies the player cannot yet shoot.
    if (!ekm_target_hittable(t.xrep, t.cstat)) {
      // Range measured inside the refusal: a sqrt per sprite on the cheapest term would cost the Debug-off case too.
      if (g_cfg.debuglog) {
        const int hdx = t.x - me.x, hdy = t.y - me.y;
        const int hd = (int)sqrt((double)hdx * hdx + (double)hdy * hdy);
        if (stats.hitSkips == 0 || hd < stats.hitDist) {
          stats.hitDist = hd; stats.hitSprite = t.sprite;
          stats.hitPic = (int)t.picnum; stats.hitCstat = (int)t.cstat;
          stats.hitXrep = (int)t.xrep;
        }
        aim_far_note(&stats.hitFar, stats.hitSkips == 0, &s_far.hittable, hd,
                     t.sprite, (int)t.picnum, (int)t.cstat, (int)t.xrep);
        stats.hitSkips++;
        s_refuse.hittable++;
      }
      continue;
    }

    const int dx = t.x - me.x, dy = t.y - me.y;
    // double: dx*dx overflows 32 bits on a full-size map.
    const int dist2d = (int)sqrt((double)dx * dx + (double)dy * dy);
    if (dist2d > g_cfg.aimrange) {
      if (g_cfg.debuglog) {
        if (stats.rangeSkips == 0 || dist2d < stats.rangeDist) {
          stats.rangeDist = dist2d; stats.rangeSprite = t.sprite;
          stats.rangePic = (int)t.picnum;
        }
        aim_far_note(&stats.rangeFar, stats.rangeSkips == 0, &s_far.range,
                     dist2d, t.sprite, (int)t.picnum, 0, 0);
        stats.rangeSkips++;
        s_refuse.range++;
      }
      continue;
    }

    // Only in crosshair mode, so Closest pays no atan2.
    if (crosshair) {
      const int off = ekm_angular_offset(myYaw, ekm_aim_yaw(dx, dy));
      if (off > cone) {
        if (g_cfg.debuglog) {
          if (stats.coneSkips == 0 || dist2d < stats.coneDist) {
            stats.coneDist = dist2d; stats.coneSprite = t.sprite;
            stats.conePic = (int)t.picnum; stats.coneOff = off;
          }
          aim_far_note(&stats.coneFar, stats.coneSkips == 0, &s_far.cone,
                       dist2d, t.sprite, (int)t.picnum, off, 0);
          stats.coneSkips++;
          s_refuse.cone++;
        }
        continue;
      }
    }

    s_candIdx[nCand] = i;
    s_candDist[nCand] = dist2d;
    nCand++;
  }

  // Nearest first: it decides ties cheaply and the ladder's early exit settles the nearest enemy first.
  for (int a = 0; a < nCand; a++) {
    int pick = a;
    for (int b = a + 1; b < nCand; b++)
      if (s_candDist[b] < s_candDist[pick]) pick = b;
    if (pick != a) {
      const int ti = s_candIdx[a];  s_candIdx[a] = s_candIdx[pick];  s_candIdx[pick] = ti;
      const int td = s_candDist[a]; s_candDist[a] = s_candDist[pick]; s_candDist[pick] = td;
    }
  }

  // One pass over every candidate; a second finer-ladder pass retried 40345 and recovered 0.
  for (int k = 0; k < nCand; k++) {
    stats.probed++;
    const EkTarget& t = s_targets[s_candIdx[k]];
    const int dist2d = s_candDist[k];
    const int dx = t.x - me.x, dy = t.y - me.y;
    const int yaw = ekm_aim_yaw(dx, dy);
    const int off = ekm_angular_offset(myYaw, yaw);

    // First visible rung is the aim point; every probe counted. Pass 1 probes from the EYE and selects; pass 2 asks
    // from the MUZZLE (ZOFFSET6 = 1024 z plus pyoff).
    int aimZ = 0;
    bool visible = false;
    // No sector clamp on the rungs (WT, aimwt.cpp:326-331): clamping head probes above the ceiling aims at an obscured
    // torso. targets.cpp validates tile size and repeat.
    int rung = 0;
    for (; rung < EK_AIM_RUNGS; rung++) {
      const int z = ekm_aim_z(t.feetZ, t.heightZ, rung, g_cfg.aimpoint);
      probes++;
      if (ek_cansee(me.x, me.y, me.z, me.sect, t.x, t.y, z, t.sect)) {
        aimZ = z;
        visible = true;
        break;
      }
    }
    if (!visible) {
      // Suspect for the window report (troopers unengaged through a broken window at the same height): a window
      // restricts which heights pass, but fitting is not diagnosing.
      if (g_cfg.debuglog) {
        if (stats.seeSkips == 0 || dist2d < stats.seeDist) {
          stats.seeDist = dist2d; stats.seeSprite = t.sprite;
          stats.seePic = (int)t.picnum; stats.seeRungs = rung;
        }
        aim_far_note(&stats.seeFar, stats.seeSkips == 0, &s_far.cansee, dist2d,
                     t.sprite, (int)t.picnum, rung, 0);
        stats.seeSkips++;
        s_refuse.cansee++;

        // Why all four rungs fail on a visible target (E1L3 wall turrets, ROTATEGUN 2360): a face sprite flush on a
        // wall can sit at or behind the wall plane, so rays to it cross geometry depending on angle. `pull` retries the
        // chosen rung dragged toward the eye.
        static unsigned s_lastRungMs = 0;
        if (s_lastRungMs == 0 ||
            (unsigned)(nowTickMs - s_lastRungMs) >= 1000u) {
          s_lastRungMs = nowTickMs;
          const int z0 = ekm_aim_z(t.feetZ, t.heightZ, 0, g_cfg.aimpoint);
          const int z1 = ekm_aim_z(t.feetZ, t.heightZ, 1, g_cfg.aimpoint);
          const int z2 = ekm_aim_z(t.feetZ, t.heightZ, 2, g_cfg.aimpoint);
          const int z3 = ekm_aim_z(t.feetZ, t.heightZ, 3, g_cfg.aimpoint);
          // Toward the eye in 2D at two distances; dist2d is non-zero here.
          const int dx = me.x - t.x, dy = me.y - t.y;
          const int d  = dist2d > 0 ? dist2d : 1;
          int pull[2] = { 0, 0 };
          const int pullBy[2] = { 64, 192 };
          for (int p = 0; p < 2; p++) {
            const int px = t.x + (int)((long long)dx * pullBy[p] / d);
            const int py = t.y + (int)((long long)dy * pullBy[p] / d);
            pull[p] = ek_cansee(me.x, me.y, me.z, me.sect, px, py, z0, t.sect) ? 1 : 0;
          }
          ek_log("aim rung detail: sprite %d picnum %d at %d units -- eye "
                 "(%d,%d,%d) sect %d, target (%d,%d) sect %d, feetZ %d height "
                 "%d; rung z: %d %d %d %d; pulled toward eye: 64u=%s 192u=%s",
                 t.sprite, (int)t.picnum, dist2d, me.x, me.y, me.z, me.sect,
                 t.x, t.y, t.sect, t.feetZ, (int)t.heightZ, z0, z1, z2, z3,
                 pull[0] ? "VISIBLE" : "blocked",
                 pull[1] ? "VISIBLE" : "blocked");
        }
      }
      continue;
    }

    // Leading decides where to shoot, never whether a target is visible. Interpolate first, then lead from the
    // interpolated point, or the staircase returns.
    int tix = t.x, tiy = t.y, tiz = t.z;
    int vx = 0, vy = 0, vz = 0;
    aim_track(t, nowTickMs, &tix, &tiy, &tiz, &vx, &vy, &vz);

    // The ladder chose its height from the raw record, so interpolation is carried as a delta.
    const int baseX = tix, baseY = tiy, baseZ = aimZ + (tiz - t.z);

    // Un-led line from the interpolated eye to the interpolated target: the answer when leading is off and the fallback
    // for an unreachable lead pitch.
    const int bdx = baseX - eyeX, bdy = baseY - eyeY;
    int baseYaw = yaw, baseDist = dist2d;
    if (bdx != 0 || bdy != 0) {
      baseDist = (int)sqrt((double)bdx * bdx + (double)bdy * bdy);
      baseYaw = ekm_aim_yaw(bdx, bdy);
    }

    int aimX = baseX, aimY = baseY, leadZ = baseZ;
    if (leadThis) {
      int lx = 0, ly = 0, lz = 0;
      ekm_lead_point(baseX, baseY, baseZ, vx, vy, vz, eyeX, eyeY,
                     projSpeed, &lx, &ly, &lz);
      aimX  = ekm_lead_scaled(baseX, lx, g_cfg.aimleadscale);
      aimY  = ekm_lead_scaled(baseY, ly, g_cfg.aimleadscale);
      leadZ = ekm_lead_scaled(baseZ, lz, g_cfg.aimleadscale);
    }
    // A lead point on the player has no direction (ekm_aim_yaw gives 0, snapping east); the un-led point keeps it.
    const int adx = aimX - eyeX, ady = aimY - eyeY;
    int aimYaw = baseYaw, aimDist = baseDist;
    if (adx != 0 || ady != 0) {
      aimDist = (int)sqrt((double)adx * adx + (double)ady * ady);
      aimYaw = ekm_aim_yaw(adx, ady);
    }

    // The solved total less the engine's part: effective pitch is q16horiz PLUS q16horizoff, never written here. Asked
    // of the SHOT's bound (the view's refused ledge enemies by 3 of 299 units). Leading moves the shot, not the target:
    // the raw point is tried before giving up.
    const int ledYaw = aimYaw;
    int q16h = ekm_q16horiz_for(leadZ - eyeZ, aimDist) - me.horizoff;
    if (!ekm_shot_horiz_reachable(q16h)) {
      q16h = ekm_q16horiz_for(baseZ - eyeZ, baseDist) - me.horizoff;
      if (!ekm_shot_horiz_reachable(q16h)) {
        // Instrumented because the symptom is silence: a dropped candidate draws no ESP state and publishes no lock.
        if (g_cfg.debuglog) {
          if (stats.pitchSkips == 0 || dist2d < stats.pitchDist) {
            stats.pitchDist = dist2d; stats.pitchSprite = t.sprite;
            stats.pitchPic = (int)t.picnum; stats.pitchNeed = q16h >> 16;
            stats.pitchDz = baseZ - eyeZ;
          }
          aim_far_note(&stats.pitchFar, stats.pitchSkips == 0, &s_far.pitch,
                       dist2d, t.sprite, (int)t.picnum, q16h >> 16,
                       baseZ - eyeZ);
          stats.pitchSkips++;
          s_refuse.pitch++;
        }
        continue;
      }
      aimYaw = baseYaw;
    }

    // Two tiers: frozen and choking enemies (hp <= 0, targets.h) are alive and worth a shot but demoted behind anything
    // with health, as a bad hp read should be.
    const int tier = t.hp > 0 ? 0 : 1;

    // The fallback is recorded BEFORE the winner is decided (the candidate the aimbot needs usually loses the
    // comparison), gated on the VIEW's bound.
    if (ekm_horiz_reachable(q16h) &&
        ekm_cand_better(fbHave, crosshair, tier, off, dist2d,
                        fbTier, fbOff, fbDist)) {
      fbHave = true;
      fbTier = tier; fbOff = off; fbDist = dist2d;
      fbYaw = aimYaw; fbHoriz = q16h; fbSprite = t.sprite;
    }

    if (!ekm_cand_better(have, crosshair, tier, off, dist2d,
                         bestTier, bestOff, bestDist))
      continue;

    have = true;
    bestTier = tier; bestOff = off; bestDist = dist2d;
    bestYaw = aimYaw; bestHoriz = q16h;
    // The winner's raw inputs kept for the shot solve after the loop, not solved per candidate.
    bestRawX = t.x; bestRawY = t.y; bestRawZ = aimZ;
    bestRawDist = dist2d;
    bestVx = vx; bestVy = vy; bestVz = vz;
    // Un-led line for the triggerbot, from the point the crosshair is drawn over.
    bestSprite = t.sprite;
    bestViewYaw = baseYaw;
    bestViewHoriz = ekm_q16horiz_for(baseZ - eyeZ, baseDist) - me.horizoff;
    bestCand = s_candIdx[k];
    bestZShift = tiz - t.z;
    bestBaseX = baseX; bestBaseY = baseY;
    bestBaseDist = baseDist; bestAimDist = aimDist; bestAimYaw = ledYaw;
  }

  // `probes` is selection's alone (pass 2 charges spanProbes): a cost reported, not capped.
  stats.cand = nCand;
  stats.probes = probes;
  stats.coneLimit = cone;
  stats.crosshair = crosshair ? 1 : 0;
  // Reported even when nothing was picked: an enemy never probed is the case the player describes.
  if (!have) {
    aim_log_frame(stats, nowTickMs);
    return false;
  }

  // Reach taken from pass 1's winner, before pass 2, which moves only the height.
  if (g_cfg.debuglog) {
    stats.acceptHave = 1;
    stats.acceptDist = bestDist;
    stats.acceptSprite = bestSprite;
    stats.acceptPic = bestCand >= 0 ? (int)s_targets[bestCand].picnum : 0;
    if (bestDist > s_far.accepted) s_far.accepted = bestDist;
  }

  // Pass 2 refines the winner HEIGHT only: pass 1 returns the first visible rung (on cover, the silhouette edge); the
  // winner gets a finer ladder aimed at the middle of the longest visible run (ekm_span_mid). A row height the engine
  // still sees is kept. Never rejects a target. Nine samples or none.
  if (bestCand >= 0 && EKM_SPAN_SAMPLES > EKM_SPAN_BUDGET) {
    static bool saidOnce = false;
    if (!saidOnce) {
      saidOnce = true;
      ek_log("aim span: SKIPPED -- %d samples do not fit pass 2's budget of "
             "%d; the aim point stays pass 1's for the whole session",
             EKM_SPAN_SAMPLES, EKM_SPAN_BUDGET);
    }
  }
  // Zero until pass 2 measures: no range is no quantum, so the triggerbot's placement rule stands down.
  int bestMarginZ = 0, bestMarginRange = 0;
  int bestSpanLo = -1, bestSpanHi = -1, bestSpanMid = -1, bestSpanH = 0;
  if (bestCand >= 0 && EKM_SPAN_SAMPLES <= EKM_SPAN_BUDGET) {
    const EkTarget& bt = s_targets[bestCand];
    unsigned char vis[EKM_SPAN_SAMPLES];
    // The ray, not the sightline, when hitscan is available; cansee is the fallback so a refused scan costs precision
    // only (hitscan.h).
    const bool byRay = hitscan_ready();
    int spanCasts = 0;
    for (int si = 0; si < EKM_SPAN_SAMPLES; si++) {
      // Probed from the MUZZLE, not the eye: the eye sees over a windowsill the muzzle cannot (shotgun and chaingun hit
      // below a half-visible trooper, the pistol did not: accurateAim snaps its angle, player.cpp:1202). muzzleZ is
      // me.z when pyoff failed.
      const int z = ekm_span_z(bt.feetZ, bt.heightZ, si, g_cfg.aimpoint);
      spanCasts++;
      if (byRay) {
        // The ray that would be fired at this sample (shotray.h) counts only when it lands on the target itself: glass,
        // grates and actors for free.
        const EkShotRay ray = ekm_shot_ray_at(bt.x - me.x, bt.y - me.y,
                                              z - muzzleZ, bestRawDist);
        EkHitData hit;
        const bool ok = ek_hitscan(me.x, me.y, muzzleZ, me.sect,
                                   ray.vx, ray.vy, ray.vz, &hit);
        vis[si] = ekm_span_sample_hit(ok, ok ? (int)hit.sprite : -1, bt.sprite) ? 1 : 0;
      } else {
        vis[si] = ek_cansee(me.x, me.y, muzzleZ, me.sect, bt.x, bt.y, z, bt.sect) ? 1 : 0;
      }
    }
    stats.spanProbes = spanCasts;
    stats.spanHitscan = byRay ? 1 : 0;
    int spanFirst = -1, spanLast = -1, spanTight = 0;
    // The configured height needs a quantum of room in the run (aimsolve.h); bestRawDist is the range the rays were
    // quantised at.
    const int mid = ekm_span_mid(vis, EKM_SPAN_SAMPLES,
                                 ekm_span_prefer(g_cfg.aimpoint),
                                 &spanFirst, &spanLast,
                                 bt.heightZ, bestRawDist, g_cfg.aimpoint,
                                 &spanTight);
    if (mid >= 0) {
      bestMarginZ = (int)ekm_span_margin_z(
          mid, spanFirst, spanLast,
          bt.heightZ > 0.0f ? (double)bt.heightZ : 0.0, g_cfg.aimpoint);
      bestMarginRange = bestRawDist;
      bestSpanLo = spanFirst; bestSpanHi = spanLast; bestSpanMid = mid;
      bestSpanH = (int)bt.heightZ;
      // Not even the midpoint has a quantum of margin: the target is smaller than the engine can aim at from here (36
      // of 39 chaingun rounds lost). The midpoint stands; rate limited.
      if (spanTight && g_cfg.debuglog) {
        static unsigned s_lastTightMs = 0;
        if (s_lastTightMs == 0 ||
            (unsigned)(nowTickMs - s_lastTightMs) >= EK_AIM_SPAN_PARTIAL_MS) {
          s_lastTightMs = nowTickMs;
          ek_log("aim quantum: sprite %d height %d at range %d -- one horiz "
                 "unit is %.0f z and the run %d..%d has less margin than that "
                 "anywhere; aiming at sample %d, the most forgiving point",
                 bt.sprite, (int)bt.heightZ, bestRawDist,
                 ekm_pitch_quantum_z(bestRawDist), spanFirst, spanLast, mid);
        }
      }
      // The winner's solve redone with the better height and nothing else: same interpolation delta, lead and
      // reachability fallback, same order. aimZ is the seam.
      const int spanZ = ekm_span_z(bt.feetZ, bt.heightZ, mid, g_cfg.aimpoint);
      const int spanBaseZ = spanZ + bestZShift;
      int spanLeadZ = spanBaseZ;
      if (leadThis) {
        int lx = 0, ly = 0, lz = 0;
        ekm_lead_point(bestBaseX, bestBaseY, spanBaseZ, bestVx, bestVy, bestVz,
                       eyeX, eyeY, projSpeed, &lx, &ly, &lz);
        spanLeadZ = ekm_lead_scaled(spanBaseZ, lz, g_cfg.aimleadscale);
      }
      int spanHoriz = ekm_q16horiz_for(spanLeadZ - eyeZ, bestAimDist) - me.horizoff;
      int spanYaw = bestAimYaw;
      // The SHOT's bound again, as pass 1 accepted on, or the refinement would hand back the coarser height exactly
      // for the targets the widening was for.
      if (!ekm_shot_horiz_reachable(spanHoriz)) {
        spanHoriz = ekm_q16horiz_for(spanBaseZ - eyeZ, bestBaseDist) - me.horizoff;
        spanYaw = bestViewYaw;
      }
      if (ekm_shot_horiz_reachable(spanHoriz)) {
        // Rate limited; names the run so the log shows the aim point rising as cover rises.
        if (g_cfg.debuglog) {
          // A partial span is the case nobody has seen (47 logged lines said run 0..8 of 9), so it gets its own more
          // generous clock.
          const bool full = ekm_span_is_full(spanFirst, spanLast, EKM_SPAN_SAMPLES);
          static unsigned s_lastFullMs = 0, s_lastPartMs = 0;
          unsigned& lastMs = full ? s_lastFullMs : s_lastPartMs;
          const unsigned gap = full ? EK_AIM_SPAN_FULL_MS : EK_AIM_SPAN_PARTIAL_MS;
          if (lastMs == 0 || (unsigned)(nowTickMs - lastMs) >= gap) {
            lastMs = nowTickMs;
            const double bodyH = bt.heightZ > 0.0f ? (double)bt.heightZ : 0.0;
            const double was = bodyH > 0.0 ? (double)(bt.feetZ - bestRawZ) / bodyH : 0.0;
            ek_log("aim span: sprite %d run %d..%d of %d, aim at sample %d "
                   "(%.3f of body height, pass 1 had %.3f)",
                   bt.sprite, spanFirst, spanLast, EKM_SPAN_SAMPLES, mid,
                   (double)ekm_span_frac(mid, g_cfg.aimpoint), was);
          }
        }
        bestYaw = spanYaw;
        bestHoriz = spanHoriz;
        bestRawZ = spanZ;
        bestViewHoriz = ekm_q16horiz_for(spanBaseZ - eyeZ, bestBaseDist) - me.horizoff;
      }
    } else if (g_cfg.debuglog) {
      // The muzzle saw none of the samples though the eye saw one: pass 1's answer stands. Logged to learn how often.
      // Rate limited.
      static unsigned s_lastBlindMs = 0;
      if (s_lastBlindMs == 0 ||
          (unsigned)(nowTickMs - s_lastBlindMs) >= EK_AIM_SPAN_PARTIAL_MS) {
        s_lastBlindMs = nowTickMs;
        ek_log("aim muzzle blind: sprite %d picnum %d -- the shot reaches none "
               "of %d samples (%s), keeping pass 1's height; muzzle is %d "
               "below the eye, feetZ %d height %d",
               bt.sprite, (int)bt.picnum, EKM_SPAN_SAMPLES,
               byRay ? "hitscan" : "cansee",
               muzzleZ - me.z, bt.feetZ, (int)bt.heightZ);
      }
    }
  }

  aim_log_frame(stats, nowTickMs);

  out->yaw = bestYaw;
  out->horiz = bestHoriz;
  out->sprite = bestSprite;
  out->viewYaw = bestViewYaw;
  out->viewHoriz = bestViewHoriz;

  // Raw positions throughout (the engine hitscans raw). The angle is solved at the firing muzzle in ekm_shot_solve.
  int sx = bestRawX, sy = bestRawY, sz = bestRawZ;
  if (leadThis) {
    int lx = 0, ly = 0, lz = 0;
    ekm_lead_point(bestRawX, bestRawY, bestRawZ, bestVx, bestVy, bestVz,
                   me.x, me.y, projSpeed, &lx, &ly, &lz);
    sx = ekm_lead_scaled(bestRawX, lx, g_cfg.aimleadscale);
    sy = ekm_lead_scaled(bestRawY, ly, g_cfg.aimleadscale);
    sz = ekm_lead_scaled(bestRawZ, lz, g_cfg.aimleadscale);
  }

  out->rawX = bestRawX; out->rawY = bestRawY; out->rawZ = bestRawZ;
  out->ledX = sx; out->ledY = sy; out->ledZ = sz;
  out->marginZ = bestMarginZ;
  out->range2d = bestMarginRange;
  out->spanLo = bestSpanLo; out->spanHi = bestSpanHi;
  out->spanMid = bestSpanMid; out->spanHeight = bestSpanH;
  out->fallbackHave = fbHave ? 1 : 0;
  out->fallbackYaw = fbYaw;
  out->fallbackHoriz = fbHoriz;
  out->fallbackSprite = fbSprite;
  if (bestCand >= 0) out->target = s_targets[bestCand];
  else               out->target.sprite = -1;
  return true;
}

// The cheap terms re-run every frame for the winner: per-tic selection left the triggerbot's two yaws describing
// different instants and the aim point stepping at 30 Hz. No cansee, hitscan or candidate loop: the lerp, one atan2,
// two sqrts and the lead for ONE sprite. Re-solves, never re-selects.
static EkAimViewPair aim_refresh(const PlayerSnap& me, int eyeX, int eyeY,
                                 int eyeZ, unsigned nowTickMs, int projSpeed,
                                 EkAimPick* pick) {
  const EkTarget& t = pick->target;

  // Where the renderer draws it now: the raw position is at most a tic old but the smoothratio is a function of the
  // wall clock, so this advances every frame.
  int tix = t.x, tiy = t.y, tiz = t.z;
  int vx = 0, vy = 0, vz = 0;
  aim_track(t, nowTickMs, &tix, &tiy, &tiz, &vx, &vy, &vz);

  const int baseX = tix, baseY = tiy, baseZ = pick->rawZ + (tiz - t.z);

  // Un-led line and both triggerbot angles from one call; degenerate fallback is the acquisition's yaw and range
  // EKM_YAW_UNITS (zero range would read as level).
  const EkAimViewPair pair = ekm_aim_view_pair(me.ang, me.horizoff,
                                               eyeX, eyeY, eyeZ,
                                               baseX, baseY, baseZ,
                                               pick->viewYaw, EKM_YAW_UNITS);
  pick->viewYaw = pair.yaw;
  pick->viewHoriz = pair.q16horiz;

  int aimX = baseX, aimY = baseY, leadZ = baseZ;
  if (projSpeed > 0) {
    int lx = 0, ly = 0, lz = 0;
    ekm_lead_point(baseX, baseY, baseZ, vx, vy, vz, eyeX, eyeY,
                   projSpeed, &lx, &ly, &lz);
    aimX  = ekm_lead_scaled(baseX, lx, g_cfg.aimleadscale);
    aimY  = ekm_lead_scaled(baseY, ly, g_cfg.aimleadscale);
    leadZ = ekm_lead_scaled(baseZ, lz, g_cfg.aimleadscale);
  }
  const EkAimViewPair led = ekm_aim_view_pair(me.ang, me.horizoff,
                                              eyeX, eyeY, eyeZ,
                                              aimX, aimY, leadZ,
                                              pair.yaw, pair.dist2d);
  if (ekm_shot_horiz_reachable(led.q16horiz)) {
    pick->yaw = led.yaw;
    pick->horiz = led.q16horiz;
  } else if (ekm_shot_horiz_reachable(pair.q16horiz)) {
    pick->yaw = pair.yaw;
    pick->horiz = pair.q16horiz;
  }
  return pair;
}

bool aim_lock(AimLock* out) {
  if (!s_lockHave || !out) return false;
  // Same freshness rule as the shot solution: a load or alt-tab would leave a stale lock.
  if (!ekm_shot_fresh(aim_tick_of(aim_now_ms()) - s_lockMs)) return false;
  out->sprite = s_lockSprite;
  out->yaw = s_lockYaw;
  out->q16horiz = s_lockHoriz;
  out->myYaw = s_lockMyYaw;
  out->myQ16Horiz = s_lockMyHoriz;
  out->marginZ = s_lockMarginZ;
  out->range2d = s_lockRange2d;
  out->q16horizoff = s_lockHorizoff;
  out->spanLo = s_lockSpanLo; out->spanHi = s_lockSpanHi;
  out->spanMid = s_lockSpanMid; out->spanHeight = s_lockSpanH;
  return true;
}

bool aim_marker_points(AimMarkerPoints* out) {
  if (!s_lockHave || !out) return false;
  if (!ekm_shot_fresh(aim_tick_of(aim_now_ms()) - s_lockMs)) return false;
  out->sprite = s_lockSprite;
  out->rawX = s_lockRawX; out->rawY = s_lockRawY; out->rawZ = s_lockRawZ;
  out->ledX = s_lockLedX; out->ledY = s_lockLedY; out->ledZ = s_lockLedZ;
  return true;
}

bool aim_solution_at(int muzzleX, int muzzleY, int muzzleZ, int q16horizoff,
                    int* q16ang, int* q16horiz) {
  if (!s_shotHave) return false;
  if (!ekm_shot_fresh(aim_tick_of(aim_now_ms()) - s_shotMs)) return false;

  // Pure arithmetic: the fire detour runs mid-tic and may not call ek_hitscan, ek_cansee or ek_updatesector.
  const EkShotSolve sol = ekm_shot_solve(muzzleX, muzzleY, muzzleZ,
                                         s_shotLedX, s_shotLedY, s_shotLedZ,
                                         s_shotRawX, s_shotRawY, s_shotRawZ,
                                         q16horizoff);
  if (!sol.ok) return false;
  if (q16ang) *q16ang = ekm_f16(sol.yaw);
  if (q16horiz) *q16horiz = sol.q16horiz;
  return true;
}

bool aim_solution_for_shot(int* q16ang, int* q16horiz) {
  // The live muzzle, as aim_frame's and startPos (player.cpp:1709-1711). A failed read refuses (WT silentwt.cpp:369).
  PlayerSnap me;
  if (!player_snap(&me)) return false;
  int pyoff = 0;
  if (!player_read_pyoff(&pyoff)) return false;
  int weapon = 0;
  (void)player_read_weapon(&weapon);
  return aim_solution_at(me.x, me.y, ekm_muzzle_z(me.z, pyoff, weapon),
                         me.horizoff, q16ang, q16horiz);
}

void aim_frame(void) {
  // Cleared first, before any gate: a stale solution is a shot at where something used to be.
  s_shotHave = false;
  s_lockHave = false;

  const bool wantAim = g_cfg.aimon != 0;
  // gamehooks_shoot_ready() is part of the condition so a failed shoot hook does not cost a probe pass every frame.
  const bool wantSilent = g_cfg.silenton != 0 && gamehooks_shoot_ready();
  // The triggerbot drives this acquisition on its own (its row names the lock, not the view write). mousebind_ready()
  // is part of the condition: its only output is a bit in localInput.
  const bool wantTbot = g_cfg.tboton != 0 && mousebind_ready();
  if (!wantAim && !wantSilent && !wantTbot) return;
  if (!aim_available()) return;

  // Our own panel stops us: the game menu pauses the simulation but this panel does not, so gm stays MODE_GAME.
  if (input_panel_open()) return;

  // The free camera stops us: the aimbot would solve from Duke's position and publish a lock for whatever stands near a
  // body nobody looks at.
  if (!freecam_bots_run(camera_active())) return;

  PlayerSnap me;
  if (!player_snap(&me)) return;

  int gm = 0;
  const bool gmOk = player_read_gm(&gm);
  const bool gmGate = !gmOk || (gm & EK_MODE_MENU) != 0;
  aim_log_gate(gm, gmOk, gmGate);
  if (gmGate) return;

  bool aimNow = wantAim;
  if (aimNow && g_cfg.aimhold) {
    const int vk = config_bind_get(BIND_ROW_AIM);
    aimNow = vk != 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
  }
  // Nothing to do is nothing to pay: acquisition walks the sector graph per candidate.
  if (!aimNow && !wantSilent && !wantTbot) return;

  // Simulation time this frame covers, in tics, so settings mean the same at 30 and 240 fps. Capped at
  // EKM_LEAD_TICS_MAX.
  const double nowMs = aim_now_ms();
  const unsigned nowTickMs = aim_tick_of(nowMs);
  static double s_lastFrameMs = -1.0;
  double ticsElapsed = 1.0;
  if (nowMs >= 0.0) {
    if (s_lastFrameMs >= 0.0) {
      ticsElapsed = (nowMs - s_lastFrameMs) / EKM_MS_PER_TIC;
      if (ticsElapsed < 0.0) ticsElapsed = 0.0;
      if (ticsElapsed > (double)EKM_LEAD_TICS_MAX) ticsElapsed = (double)EKM_LEAD_TICS_MAX;
    }
    s_lastFrameMs = nowMs;
  }

  // Where the camera is this frame: the view is drawn interpolated from opos.
  int eyeX = me.x, eyeY = me.y, eyeZ = me.z;
  aim_eye_interp(me, nowTickMs, &eyeX, &eyeY, &eyeZ);

  // The muzzle, not the eye (WT silentwt.cpp:369: about pyoff + 1024 low). Measured from raw me.z: A_ShootWithZvel uses
  // pPlayer->pos (player.cpp:1709-1711).
  int pyoff = 0;
  const bool muzzleOk = player_read_pyoff(&pyoff);
  int muzzleWeapon = 0;
  (void)player_read_weapon(&muzzleWeapon);
  const int muzzleZ =
      muzzleOk ? ekm_muzzle_z(me.z, pyoff, muzzleWeapon) : me.z;

  // Shot speed asked once a frame. Zero means a straight-line lead is wrong (hitscan, thrown, placed); a failed weapon
  // read does not lead. Read here so the per-frame re-solve leads equally.
  int projSpeed = 0;
  if (g_cfg.aimlead) {
    int weapon = 0;
    if (player_read_weapon(&weapon)) projSpeed = ekm_weapon_proj_speed(weapon);
  }

  // Acquisition once per TIC, not per frame (the one place this port differed from WT). The tic boundary is wall-clock
  // derived from the 30 Hz tic (aimsolve.h): at most 30.3 passes a second. The pick is republished every frame and only
  // the LOOP is skipped, so lock and points are at most a tic old (tb_0066).
  static EkAimPick s_pick;
  static bool s_pickHave = false, s_pickEver = false;
  static unsigned s_pickMs = 0;
  if (ekm_acquire_due(s_pickEver, nowTickMs - s_pickMs)) {
    s_pickEver = true;
    s_pickMs = nowTickMs;
    s_pickHave = aim_acquire(me, eyeX, eyeY, eyeZ, muzzleZ, nowTickMs,
                             projSpeed, &s_pick);
  }
  if (!s_pickHave) return;

  // A copy: the next acquisition must start from the loop's answers.
  EkAimPick pick = s_pick;
  const EkAimViewPair pair =
      aim_refresh(me, eyeX, eyeY, eyeZ, nowTickMs, projSpeed, &pick);

  // The triggerbot's lock, published whenever acquisition succeeded, without a reachability test: it describes where
  // the enemy IS. All three angles come from one ekm_aim_view_pair call (tb_0071).
  s_lockSprite = pick.sprite;
  s_lockYaw = pair.yaw;
  s_lockHoriz = pair.q16horiz;
  s_lockMyYaw = pair.myYaw;
  s_lockMyHoriz = me.horiz;
  s_lockRawX = pick.rawX; s_lockRawY = pick.rawY; s_lockRawZ = pick.rawZ;
  s_lockLedX = pick.ledX; s_lockLedY = pick.ledY; s_lockLedZ = pick.ledZ;
  s_lockMarginZ = pick.marginZ; s_lockRange2d = pick.range2d;
  s_lockSpanLo = pick.spanLo; s_lockSpanHi = pick.spanHi;
  s_lockSpanMid = pick.spanMid; s_lockSpanH = pick.spanHeight;
  s_lockHorizoff = me.horizoff;
  // The acquisition's own stamp: re-dating a reused pick would defeat the freshness rule.
  s_lockMs = s_pickMs;
  s_lockHave = true;

  // The point for the A_ShootWithZvel detour. The muzzle read and reachability test live in aim_solution_at, against
  // the pitch the firing muzzle yields; the test is the SHOT's bound, and a pitch no ray can express leaves the
  // player's shot untouched. ledX/Y/Z is post-lead, rawX/Y/Z the un-led body point.
  if (wantSilent) {
    s_shotRawX = pick.rawX; s_shotRawY = pick.rawY; s_shotRawZ = pick.rawZ;
    s_shotLedX = pick.ledX; s_shotLedY = pick.ledY; s_shotLedZ = pick.ledZ;
    s_shotMs = s_pickMs;
    s_shotHave = true;
  }

  if (!aimNow) return;

  // The view's own bound, asked here only: the winner's pitch is judged by what the SHOT reaches and may be one
  // P_GetInput clamps (player.cpp:3179). A winner the view cannot hold must not starve the bot, so the best
  // view-reachable candidate (fbHave) is spent here; silent aim and the lock keep the winner.
  int aimYawTo = pick.yaw, aimHorizTo = pick.horiz;
  if (!ekm_horiz_reachable(pick.horiz)) {
    if (!pick.fallbackHave) return;
    aimYawTo = pick.fallbackYaw;
    aimHorizTo = pick.fallbackHoriz;
    if (g_cfg.debuglog) {
      static unsigned s_lastFbMs = 0;
      if (s_lastFbMs == 0 ||
          (unsigned)(nowTickMs - s_lastFbMs) >= EK_AIM_SPAN_PARTIAL_MS) {
        s_lastFbMs = nowTickMs;
        ek_log("aim fallback: sprite %d needs q16horiz %d, which the view "
               "cannot hold; turning to sprite %d instead (the lock and silent "
               "aim keep sprite %d)",
               pick.sprite, pick.horiz >> 16, pick.fallbackSprite, pick.sprite);
      }
    }
  }

  // The whole delta in 16.16 (a whole-unit delta left the player's fractional angle in the result, a permanent
  // remainder). Per tic, not per frame.
  const int stepYaw   = ekm_smooth_step_tics(ekm_yaw_delta_q16(me.ang, ekm_f16(aimYawTo)),
                                             g_cfg.aimsmooth, ticsElapsed);
  const int stepHoriz = ekm_smooth_step_tics(aimHorizTo - me.horiz,
                                             g_cfg.aimsmooth, ticsElapsed);
  if (stepYaw == 0 && stepHoriz == 0) return;

  const int newAng   = (me.ang + stepYaw) & (ekm_f16(EKM_YAW_UNITS) - 1);
  const int newHoriz = me.horiz + stepHoriz;

  // Four fields, not two: G_DrawRooms interpolates oq16ang to q16ang, so a snap needs the old value overwritten.
  player_write_view(newAng, newHoriz, g_cfg.aimsmooth == 0);
}
