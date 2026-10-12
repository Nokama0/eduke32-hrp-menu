// Screenshot mode, the DLL half; decisions are pure rules in shot.h. Crosshair, weapon and level stats are OSD cvars;
// the status bar and scuba mask have no view-safe cvar, so their call sites are redirected. HAZARD: the engine writes
// these to settings.cfg on exit and every save (config.cpp:1044), so a missed restore is silent.
#include <windows.h>

#include "shot.h"
#include "cvars.h"
#include "config.h"
#include "hook.h"
#include "imagescan.h"
#include "hitscan.h"
#include "log.h"

// Order: SHOT_CVAR_CROSSHAIR, _WEAPON, _STATS.
static const char* const kShotCvars[SHOT_CVARS] = {
  "crosshair", "r_drawweapon", "hud_stats"
};

// One thread owns this: shot_frame and shot_active_now run on the game thread from overlay.cpp's per-frame path.
static ShotHold s_hold = { 0, 0, { 0, 0, 0 }, 0 };
static bool s_active = false;

bool shot_active_now(void) { return s_active; }

bool shot_available(void) {
  // The level stats are a bonus; the weapon and crosshair are the feature.
  return cvar_row_available(cvars_find(kShotCvars[SHOT_CVAR_CROSSHAIR])) &&
         cvar_row_available(cvars_find(kShotCvars[SHOT_CVAR_WEAPON]));
}

// A refusal costs that one cvar (cvar_set_int logs it); half a restore beats none.
static void shot_write(int mask, const int* value) {
  for (int i = 0; i < SHOT_CVARS; i++)
    if (mask & (1 << i)) cvar_set_int(kShotCvars[i], value[i]);
}

// Clear the on-disk stash after an OFF-edge restore and after any install-time replay, matched or not; a
// carried-forward stash is the never-cleared bug.
static void shot_stash_clear(void) {
  g_cfg.shotstashheld = 0;
  g_cfg.shotstashmask = 0;
  g_cfg.shotstashcrosshair = 0;
  g_cfg.shotstashweapon = 0;
  g_cfg.shotstashstats = 0;
  g_cfgDirty = 1;
  config_write_changed();
}

static void shot_step(int edge) {
  int cur[SHOT_CVARS] = { 0, 0, 0 };
  int readMask = 0;
  for (int i = 0; i < SHOT_CVARS; i++)
    if (cvar_get_int(kShotCvars[i], &cur[i])) readMask |= (1 << i);

  int outMask = 0;
  int outVal[SHOT_CVARS] = { 0, 0, 0 };
  const int act = shot_hold_step(&s_hold, edge, readMask, cur, &outMask, outVal);
  if (act == SHOT_ACT_NONE) return;

  // The stash goes to disk before the cvars are touched on hide and after them on restore, so there is never an instant
  // of hidden cvars without the recovery copy.
  if (act == SHOT_ACT_HIDE) {
    g_cfg.shotstashheld = 1;
    g_cfg.shotstashmask = outMask;
    g_cfg.shotstashcrosshair = s_hold.saved[SHOT_CVAR_CROSSHAIR];
    g_cfg.shotstashweapon    = s_hold.saved[SHOT_CVAR_WEAPON];
    g_cfg.shotstashstats     = s_hold.saved[SHOT_CVAR_STATS];
    g_cfgDirty = 1;
    config_write_changed();
  }

  shot_write(outMask, outVal);

  if (act == SHOT_ACT_RESTORE) shot_stash_clear();

  // Not behind g_cfg.debuglog: this writes the player's settings and the log must prove the pair happened. The two
  // redirects are named on the same line since neither leaves a trace in settings.cfg.
  const char* const gone = act == SHOT_ACT_HIDE ? "not drawn from the next frame"
                                                : "drawn again from the next frame";
  const char* const barWord =
      shot_statusbar_available() ? gone : "left alone, the redirect is not in";
  const char* const maskWord =
      shot_scuba_available() ? gone : "left alone, the redirect is not in";
  ek_log(act == SHOT_ACT_HIDE
           ? "shot: on -- crosshair %d, r_drawweapon %d, hud_stats %d saved and hidden (mask 0x%X); status bar %s; scuba mask %s"
           : "shot: off -- crosshair %d, r_drawweapon %d, hud_stats %d restored (mask 0x%X); status bar %s; scuba mask %s",
         act == SHOT_ACT_HIDE ? cur[SHOT_CVAR_CROSSHAIR] : outVal[SHOT_CVAR_CROSSHAIR],
         act == SHOT_ACT_HIDE ? cur[SHOT_CVAR_WEAPON] : outVal[SHOT_CVAR_WEAPON],
         act == SHOT_ACT_HIDE ? cur[SHOT_CVAR_STATS] : outVal[SHOT_CVAR_STATS],
         outMask, barWord, maskWord);
}

// Install-time replay, called once from overlay.cpp right after cvars_scan(): the earliest the registry answers, before
// another ON edge can zero the cvars.
void shot_replay_stash(void) {
  if (!g_cfg.shotstashheld) return;

  int cur[SHOT_CVARS] = { 0, 0, 0 };
  int readMask = 0;
  for (int i = 0; i < SHOT_CVARS; i++)
    if (cvar_get_int(kShotCvars[i], &cur[i])) readMask |= (1 << i);

  const int saved[SHOT_CVARS] = {
    g_cfg.shotstashcrosshair, g_cfg.shotstashweapon, g_cfg.shotstashstats
  };
  int outMask = 0;
  int outVal[SHOT_CVARS] = { 0, 0, 0 };
  const int act = shot_stash_replay(g_cfg.shotstashheld, g_cfg.shotstashmask, saved,
                                     readMask, cur, &outMask, outVal);
  if (act == SHOT_ACT_RESTORE) {
    shot_write(outMask, outVal);
    ek_log("shot: replayed a crash stash from a previous session -- "
           "crosshair %d, r_drawweapon %d, hud_stats %d put back (mask 0x%X)",
           outVal[SHOT_CVAR_CROSSHAIR], outVal[SHOT_CVAR_WEAPON], outVal[SHOT_CVAR_STATS], outMask);
  } else if (shot_stash_healed(g_cfg.shotstashmask & readMask, saved, cur)) {
    // Not a failure: a stash outlives a session that healed itself (detach restores the cvars without touching the
    // file).
    ek_log("shot: a stash from a previous session is already back in the live "
           "cvars -- crosshair %d, r_drawweapon %d, hud_stats %d, so nothing "
           "was replayed and the stash is dropped. This is what a session that "
           "restored itself on the way out leaves behind (stash mask 0x%X, "
           "readable 0x%X)",
           cur[SHOT_CVAR_CROSSHAIR], cur[SHOT_CVAR_WEAPON], cur[SHOT_CVAR_STATS],
           g_cfg.shotstashmask, readMask);
  } else {
    ek_log("shot: a stash from a previous session matches neither the hidden "
           "state nor its own saved values -- crosshair %d, r_drawweapon %d, "
           "hud_stats %d against saved %d/%d/%d. Something changed these since, "
           "so they are left exactly as they are and the stash is dropped "
           "without replay (stash mask 0x%X, readable 0x%X)",
           cur[SHOT_CVAR_CROSSHAIR], cur[SHOT_CVAR_WEAPON], cur[SHOT_CVAR_STATS],
           saved[SHOT_CVAR_CROSSHAIR], saved[SHOT_CVAR_WEAPON], saved[SHOT_CVAR_STATS],
           g_cfg.shotstashmask, readMask);
  }
  shot_stash_clear();
}

void shot_frame(bool cameraActive, bool menuOpen) {
  const bool now = shot_active(cameraActive, g_cfg.freecamshot != 0, menuOpen);
  const int edge = shot_edge(s_active, now);
  s_active = now;
  // Steady frames cost two int reads and a return.
  if (edge == SHOT_EDGE_NONE) return;
  shot_step(edge);
}

// Detach is the belt, not the guarantee: on a clean quit settings.cfg is written first (G_Shutdown, game.cpp:6040), and
// a menu quit never reaches an OFF edge (the menu cannot open while flying), which the on-disk stash closes. A plain
// four-byte store. It does NOT clear the stash: a file write under the loader lock is what this path must not do.
void shot_restore(void) {
  if (!s_hold.held) return;
  int cur[SHOT_CVARS] = { 0, 0, 0 };
  int outMask = 0;
  int outVal[SHOT_CVARS] = { 0, 0, 0 };
  if (shot_hold_step(&s_hold, SHOT_EDGE_OFF, 0, cur, &outMask, outVal) == SHOT_ACT_RESTORE) {
    shot_write(outMask, outVal);
    ek_log("shot: the mod is unloading with the hud still hidden -- "
           "crosshair %d, r_drawweapon %d, hud_stats %d put back (mask 0x%X)",
           outVal[SHOT_CVAR_CROSSHAIR], outVal[SHOT_CVAR_WEAPON],
           outVal[SHOT_CVAR_STATS], outMask);
  }
  s_active = false;
}

// THE STATUS BAR: ONE CALL SITE. No cvar hides it without moving the view (hud_statusbarmode needs G_UpdateScreenArea).
// Call site, not entry hook (hook.cpp). Found by "Game Paused" (screens.cpp:1301) -> its lea -> G_DisplayRest -> the
// one call target loading "On", "Off", "Auto" -> its one site (docs/re/shot.md).

typedef void (*DrawStatusBarFn)(int snum);

// Searched with their NULs (sizeof), so a longer string with the same prefix cannot match.
static const char EK_SBAR_ON[]     = "On";
static const char EK_SBAR_OFF[]    = "Off";
static const char EK_SBAR_AUTO[]   = "Auto";
static const char EK_SBAR_ANCHOR[] = "Game Paused";

// Cap so a wrong scan cannot walk off the array (shipped: 40).
static const int EK_SBAR_MAX_TARGETS = 64;

// Where the redirected site used to go; the thunk forwards through this only.
static DrawStatusBarFn s_sbarOrig = NULL;
static EkCallPatch s_sbarPatch;
static bool s_saidNoSbarOrig = false;

bool shot_statusbar_available(void) { return s_sbarPatch.site != NULL; }

// THE THUNK. One int argument (ecx). It forwards unless the mode is on, since this is the only G_DrawStatusBar call in
// the image. Skipping is safe: its non-drawing effects are a dirty flag it sets itself (sbar.cpp:587) and lazy icon
// tables. Same thread as s_active's writer, read one frame after written (draw precedes swap).
static void EK_DrawStatusBarThunk(int snum) {
  DrawStatusBarFn const orig = s_sbarOrig;
  if (!orig) {
    // Unreachable (captured before patching, cleared after); said once, and no bar corrupts nothing.
    if (!s_saidNoSbarOrig) {
      s_saidNoSbarOrig = true;
      ek_log("shot: the status bar thunk ran with no original to forward to, "
             "which nothing in this build can produce -- please report it. The "
             "bar is not drawn while this lasts");
    }
    return;
  }
  if (!shot_draws_statusbar(s_active)) return;
  orig(snum);
}

// Bounds first: a literal test must not walk off a section.
static bool sbar_literal_at(const unsigned char* base, unsigned rva,
                            const char* lit, unsigned n) {
  unsigned b = 0, e = 0;
  if (!ek_image_data_extent(base, rva, &b, &e)) return false;
  if (rva < b || rva >= e || e - rva < n) return false;
  for (unsigned i = 0; i < n; i++)
    if (base[rva + i] != (unsigned char)lit[i]) return false;
  return true;
}

// G_DrawStatusBar's fingerprint: [b,e) loads all three literals. The positional lea walk can only over-match, the safe
// direction for a test whose authority is that exactly one function passes.
static bool sbar_shape_of(const unsigned char* base, unsigned b, unsigned e) {
  bool on = false, off = false, autoLit = false;
  if (e <= b + 7) return false;
  for (unsigned q = b; q + 7 <= e; q++) {
    const unsigned char rex = base[q];
    if ((rex != 0x48 && rex != 0x4C) || base[q + 1] != 0x8D) continue;
    if ((base[q + 2] & 0xC7) != 0x05) continue;
    const int disp = *(const int*)(base + q + 3);
    const unsigned t = (unsigned)((int)q + 7 + disp);
    if (!on && sbar_literal_at(base, t, EK_SBAR_ON, (unsigned)sizeof(EK_SBAR_ON)))
      on = true;
    if (!off && sbar_literal_at(base, t, EK_SBAR_OFF, (unsigned)sizeof(EK_SBAR_OFF)))
      off = true;
    if (!autoLit &&
        sbar_literal_at(base, t, EK_SBAR_AUTO, (unsigned)sizeof(EK_SBAR_AUTO)))
      autoLit = true;
    if (shot_sbar_shape(on, off, autoLit)) return true;
  }
  return shot_sbar_shape(on, off, autoLit);
}

// `sites` counts only 0xE8 positions whose target is a function entry, so an operand byte is not read as an opcode.
struct SbarTargets {
  int sites; int targets; int shaped; unsigned rva; int capped; int faulted;
};

static void find_sbar_target(const unsigned char* base, unsigned begin,
                             unsigned end, SbarTargets* out) {
  out->sites = 0; out->targets = 0; out->shaped = 0;
  out->rva = 0; out->capped = 0; out->faulted = 0;
  if (end <= begin + 5) { out->faulted = 1; return; }

  unsigned seen[EK_SBAR_MAX_TARGETS];
  int nseen = 0;
  __try {
    for (unsigned q = begin; q + 5 <= end; q++) {
      if (base[q] != 0xE8) continue;
      const int disp = *(const int*)(base + q + 1);
      const unsigned t = ekm_us_rel32_target(q, disp);
      unsigned b = 0, e = 0;
      if (!ek_image_extent_containing(base, t, &b, &e) || b != t) continue;
      out->sites++;
      int dup = 0;
      for (int k = 0; k < nseen; k++) if (seen[k] == t) { dup = 1; break; }
      if (dup) continue;
      if (nseen < EK_SBAR_MAX_TARGETS) seen[nseen++] = t;
      else out->capped = 1;
      out->targets++;
      if (!sbar_shape_of(base, b, e)) continue;
      out->shaped++;
      if (out->rva == 0) out->rva = t;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    out->faulted = 1;
  }
}

// Counts `call rel32` reaching one target in a pass of its own (the target walk skips targets already seen). Shared by
// both redirects.
struct EkSiteCount { int count; unsigned rva; int faulted; };

static void find_sites_reaching(const unsigned char* base, unsigned begin,
                                unsigned end, unsigned target, EkSiteCount* out) {
  out->count = 0; out->rva = 0; out->faulted = 0;
  if (target == 0 || end <= begin + 5) { out->faulted = 1; return; }
  __try {
    for (unsigned q = begin; q + 5 <= end; q++) {
      if (base[q] != 0xE8) continue;
      const int disp = *(const int*)(base + q + 1);
      if (ekm_us_rel32_target(q, disp) != target) continue;
      if (out->count++ == 0) out->rva = q;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    out->faulted = 1;
  }
}

// G_DisplayRest's extent, shared by both redirects: anchor string, the lea reaching it, its RUNTIME_FUNCTION. `what`
// names the feature a refusal loses.
static bool locate_displayrest(const unsigned char* base, const char* what,
                               unsigned* beginOut, unsigned* endOut) {
  *beginOut = 0; *endOut = 0;

  EkStrHit s;
  ek_image_find_string(base, EK_SBAR_ANCHOR, sizeof(EK_SBAR_ANCHOR), &s);
  if (s.count != 1 || s.faulted) {
    ek_log("shot: %s -- the \"%s\" anchor occurs %d time(s) "
           "in the image's read-only data%s, so G_DisplayRest cannot be "
           "bracketed (r10669-ec5824db8 has exactly 1)", what, EK_SBAR_ANCHOR,
           s.count,
           s.faulted ? " and a section faulted mid-scan, so that count is partial"
                     : "");
    return false;
  }

  EkRefHit r;
  ek_image_find_lea_refs(base, s.rva, &r);
  if (r.count != 1 || r.faulted) {
    ek_log("shot: %s -- %d instruction(s) load the address "
           "of \"%s\" at rva 0x%07X (%d position(s) held the displacement before "
           "the lea shape test)%s; exactly one is what names G_DisplayRest",
           what, r.count, EK_SBAR_ANCHOR, s.rva, r.raw,
           r.faulted ? " and a section faulted mid-scan" : "");
    return false;
  }

  if (!ek_image_extent_containing(base, r.rva, beginOut, endOut)) {
    ek_log("shot: %s -- no RUNTIME_FUNCTION covers the lea "
           "at rva 0x%07X, so there is no bracketed caller to search", what,
           r.rva);
    return false;
  }
  return true;
}

static bool locate_sbar(const unsigned char* base, unsigned begin, unsigned end,
                        unsigned* targetOut, unsigned* siteOut) {
  *targetOut = 0; *siteOut = 0;

  SbarTargets t;
  find_sbar_target(base, begin, end, &t);
  if (!shot_sbar_target_ok(t.shaped, t.faulted)) {
    ek_log("shot: the status bar stays up -- G_DrawStatusBar not identified. %d "
           "`call rel32` site(s) inside 0x%07X..0x%07X reach %d distinct function "
           "entry(ies), %d of which load all three of \"On\", \"Off\" and "
           "\"Auto\"%s%s (r10669-ec5824db8 has 1 of 40)",
           t.sites, begin, end, t.targets, t.shaped,
           t.capped ? " (target list capped)" : "",
           t.faulted ? " (the body faulted mid-scan, so those counts are partial)"
                     : "");
    return false;
  }

  EkSiteCount site;
  find_sites_reaching(base, begin, end, t.rva, &site);
  if (!shot_sbar_locate_ok(site.count, site.faulted)) {
    ek_log("shot: the status bar stays up -- G_DrawStatusBar at rva 0x%07X is "
           "reached from %d call site(s) inside 0x%07X..0x%07X%s; refusing to "
           "patch any of them. Two would mean SPLITSCREEN_MOD_HACKS is compiled "
           "in and screens.cpp:1160 draws a second bar this would not hide "
           "(r10669-ec5824db8 has exactly 1)", t.rva, site.count, begin, end,
           site.faulted ? " (the body faulted mid-scan, so that count is partial)"
                        : "");
    return false;
  }

  ek_log("shot: G_DrawStatusBar at rva 0x%07X -- the only one of %d distinct "
         "target(s) of the %d `call rel32` site(s) inside 0x%07X..0x%07X that "
         "loads \"On\", \"Off\" and \"Auto\"; the ONE site reaching it is at rva "
         "0x%07X, and the \"%s\" anchor is what named that bracket",
         t.rva, t.targets, t.sites, begin, end, site.rva, EK_SBAR_ANCHOR);
  *targetOut = t.rva;
  *siteOut = site.rva;
  return true;
}

// THE SCUBA MASK: three call sites, all or none. P_DisplayScuba is inlined in G_DisplayRest, so its three
// G_DrawTileScaled draws (player.cpp:2178-2181) are redirected, each found by its source x as `mov ecx, imm32`; all
// must reach one callee with exactly three sites (docs/re/shot.md).

// Mov-to-call distance is 18-21 bytes shipped; 32 tolerates reordered setup.
static const unsigned EK_SCUBA_ARG_WINDOW = 32;

typedef void (*DrawTileScaledFn)(int drawX, int drawY, int tileNum, int drawShade,
                                 int drawBits, int drawPal, int uniqueID);

// One anchored draw: positions holding the immediate, entry calls after the first, its site and target.
struct ScubaDraw {
  int anchors; int calls; unsigned anchorRva; unsigned site; unsigned target;
  int faulted;
};

static void find_scuba_draw(const unsigned char* base, unsigned begin,
                            unsigned end, unsigned imm, ScubaDraw* out) {
  out->anchors = 0; out->calls = 0; out->anchorRva = 0;
  out->site = 0; out->target = 0; out->faulted = 0;
  if (end <= begin + (unsigned)EKM_CALL_LEN) { out->faulted = 1; return; }
  __try {
    for (unsigned q = begin; q + 5 <= end; q++) {
      if (base[q] != 0xB9) continue;
      if (*(const unsigned*)(base + q + 1) != imm) continue;
      out->anchors++;
      // A second position makes the anchor ambiguous and is refused on that count alone.
      if (out->anchors > 1) continue;
      out->anchorRva = q;
      const unsigned stop =
          (end - q < EK_SCUBA_ARG_WINDOW) ? end : q + EK_SCUBA_ARG_WINDOW;
      for (unsigned c = q + 5; c + 5 <= stop; c++) {
        if (base[c] != 0xE8) continue;
        const int disp = *(const int*)(base + c + 1);
        const unsigned t = ekm_us_rel32_target(c, disp);
        unsigned b = 0, e = 0;
        // A function entry, so an operand byte is not read as an opcode.
        if (!ek_image_extent_containing(base, t, &b, &e) || b != t) continue;
        out->calls++;
        if (out->calls == 1) { out->site = c; out->target = t; }
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    out->faulted = 1;
  }
}

static bool locate_scuba(const unsigned char* base, unsigned begin, unsigned end,
                         unsigned* targetOut, unsigned* sitesOut) {
  *targetOut = 0;
  for (int i = 0; i < SHOT_SCUBA_SITES; i++) sitesOut[i] = 0;

  // Corroboration only: the inlined body is in this bracket.
  ScubaDraw ev;
  find_scuba_draw(base, begin, end, (unsigned)SHOT_EVENT_DISPLAYSCUBA, &ev);
  if (!shot_scuba_anchor_ok(ev.anchors, ev.faulted)) {
    ek_log("shot: the scuba mask stays on -- %d position(s) inside "
           "0x%07X..0x%07X load EVENT_DISPLAYSCUBA (%d) into ecx%s, so "
           "P_DisplayScuba's own body is not identified inside G_DisplayRest "
           "(r10669-ec5824db8 has exactly 1)", ev.anchors, begin, end,
           SHOT_EVENT_DISPLAYSCUBA,
           ev.faulted ? " and the body faulted mid-scan, so that count is partial"
                      : "");
    return false;
  }

  const unsigned kx[SHOT_SCUBA_SITES] = {
    (unsigned)SHOT_SCUBA_X_LEFT, (unsigned)SHOT_SCUBA_X_RIGHT,
    (unsigned)SHOT_SCUBA_X_SEAM
  };
  ScubaDraw d[SHOT_SCUBA_SITES];
  int anchored = 0, targets = 0, faulted = 0;
  unsigned target = 0;
  for (int i = 0; i < SHOT_SCUBA_SITES; i++) {
    find_scuba_draw(base, begin, end, kx[i], &d[i]);
    if (d[i].faulted) faulted = 1;
    if (!shot_scuba_anchor_ok(d[i].anchors, d[i].faulted)) continue;
    if (!shot_scuba_anchor_ok(d[i].calls, d[i].faulted)) continue;
    anchored++;
    sitesOut[i] = d[i].site;
    if (target == 0) { target = d[i].target; targets = 1; }
    else if (target != d[i].target) targets++;
  }

  // Target 0 means nothing anchored, reported by faulting rather than a confident 0.
  EkSiteCount sc;
  find_sites_reaching(base, begin, end, target, &sc);
  if (sc.faulted) faulted = 1;

  if (!shot_scuba_locate_ok(anchored, targets, sc.count, faulted)) {
    ek_log("shot: the scuba mask stays on -- P_DisplayScuba's three draws not "
           "named. Inside 0x%07X..0x%07X, x=%d found %d position(s)/%d call(s), "
           "x=%d found %d/%d, x=%d found %d/%d; %d of 3 anchored, reaching %d "
           "distinct callee(s), and %d site(s) in the bracket reach 0x%07X%s "
           "(r10669-ec5824db8 has 3, 1 and 3)",
           begin, end,
           SHOT_SCUBA_X_LEFT, d[0].anchors, d[0].calls,
           SHOT_SCUBA_X_RIGHT, d[1].anchors, d[1].calls,
           SHOT_SCUBA_X_SEAM, d[2].anchors, d[2].calls,
           anchored, targets, sc.count, target,
           faulted ? " (a scan faulted mid-walk, so those counts are partial)"
                   : "");
    return false;
  }

  ek_log("shot: P_DisplayScuba is INLINED into G_DisplayRest and its three "
         "G_DrawTileScaled draws are at rva 0x%07X (x %d), 0x%07X (x %d) and "
         "0x%07X (x %d); all three reach 0x%07X, which %d site(s) inside "
         "0x%07X..0x%07X reach in total, and the EVENT_DISPLAYSCUBA guard at "
         "rva 0x%07X corroborates",
         sitesOut[0], SHOT_SCUBA_X_LEFT, sitesOut[1], SHOT_SCUBA_X_RIGHT,
         sitesOut[2], SHOT_SCUBA_X_SEAM, target, sc.count, begin, end,
         ev.anchorRva);
  *targetOut = target;
  return true;
}

// The original callee and patches. The count is the authority: the thunk forwards unless it is three, so a
// half-installed or half-removed state draws as the engine would.
static DrawTileScaledFn s_scubaOrig = NULL;
static EkCallPatch s_scubaPatch[SHOT_SCUBA_SITES];
static int s_scubaPatched = 0;
static bool s_saidNoScubaOrig = false;
static bool s_saidScubaStranger = false;

bool shot_scuba_available(void) { return shot_scuba_installed(s_scubaPatched); }

// THE THUNK. Seven int arguments: four in ecx/edx/r8d/r9d, rest at [rsp+0x20..0x30]; all three sites write the seventh
// slot and keep nothing in a volatile register across the call. Same thread and one-frame lag as the status bar's
// thunk.
static void EK_DrawTileScaledThunk(int drawX, int drawY, int tileNum,
                                   int drawShade, int drawBits, int drawPal,
                                   int uniqueID) {
  DrawTileScaledFn const orig = s_scubaOrig;
  if (!orig) {
    // Unreachable; said once, and the frame just has no mask.
    if (!s_saidNoScubaOrig) {
      s_saidNoScubaOrig = true;
      ek_log("shot: the scuba thunk ran with no original to forward to, which "
             "nothing in this build can produce -- please report it. The mask "
             "is not drawn while this lasts");
    }
    return;
  }
  if (shot_scuba_installed(s_scubaPatched)) {
    if (!shot_draws_scuba(s_active, drawX, drawShade, drawBits)) return;
    // A call through our sites that is not a mask draw means the locate named the wrong callee. It forwards; said once.
    if (s_active && !s_saidScubaStranger) {
      s_saidScubaStranger = true;
      ek_log("shot: a redirected scuba draw arrived with x %d, shade %d, bits "
             "0x%X, which is not one of P_DisplayScuba's three -- forwarding it "
             "and leaving the mask alone. Please report it", drawX, drawShade,
             drawBits);
    }
  }
  orig(drawX, drawY, tileNum, drawShade, drawBits, drawPal, uniqueID);
}

// All three or none: s_scubaPatched stays 0 during the loop, so an early patched site forwards, and a failed removal
// leaves the mask drawing.
static void install_scuba(const unsigned char* base, unsigned begin,
                          unsigned end) {
  unsigned target = 0;
  unsigned sites[SHOT_SCUBA_SITES];
  if (!locate_scuba(base, begin, end, &target, sites)) return;

  s_scubaOrig = (DrawTileScaledFn)(const void*)(base + target);
  int in = 0;
  for (int i = 0; i < SHOT_SCUBA_SITES; i++) {
    if (!callsite_install((void*)(base + sites[i]), (void*)(base + target),
                          (void*)&EK_DrawTileScaledThunk, &s_scubaPatch[i]))
      break;
    in++;
  }

  if (in == SHOT_SCUBA_SITES) {
    s_scubaPatched = SHOT_SCUBA_SITES;
    ek_log("shot: the scuba mask redirect is IN -- all %d call(s), at rva "
           "0x%07X, 0x%07X and 0x%07X, now reach our thunk, which forwards to "
           "0x%07X on every frame except while screenshot mode is active",
           SHOT_SCUBA_SITES, sites[0], sites[1], sites[2], target);
    return;
  }

  int out = 0;
  for (int i = 0; i < in; i++) if (callsite_remove(&s_scubaPatch[i])) out++;
  if (out == in) s_scubaOrig = NULL;
  ek_log("shot: the scuba mask's redirect was NOT installed -- %d of %d site(s) "
         "took the patch and %d of those were put back; the mask stays on in a "
         "shot. The weapon, the crosshair, the stats and the status bar are "
         "unaffected", in, SHOT_SCUBA_SITES, out);
}

// Once per session from overlay.cpp's per-frame path only (game thread in SwapBuffers, hook.cpp's write window; never
// input.cpp's thread). Unconditional: while off the thunk costs one flag read.
static void install_sbar(const unsigned char* base, unsigned begin,
                         unsigned end) {
  unsigned target = 0, site = 0;
  if (!locate_sbar(base, begin, end, &target, &site)) return;

  // The address goes in first: the patch is live the instant its bytes land.
  s_sbarOrig = (DrawStatusBarFn)(const void*)(base + target);
  if (!callsite_install((void*)(base + site), (void*)(base + target),
                        (void*)&EK_DrawStatusBarThunk, &s_sbarPatch)) {
    s_sbarOrig = NULL;
    ek_log("shot: the status bar's call site at rva 0x%07X was NOT redirected; "
           "the game keeps its own call and the bar stays up in a shot. The "
           "weapon, the crosshair and the stats are unaffected", site);
    return;
  }

  ek_log("shot: the status bar redirect is IN -- the call at rva 0x%07X now "
         "reaches our thunk, which forwards to 0x%07X on every frame except "
         "while screenshot mode is active", site, target);
}

void shot_install(void) {
  static bool tried = false;
  if (tried) return;
  tried = true;

  const unsigned char* const base = (const unsigned char*)GetModuleHandleA(NULL);
  if (!base) {
    ek_log("shot: neither redirect goes in -- GetModuleHandleA(NULL) returned "
           "nothing, so the host image cannot be searched (%lu)", GetLastError());
    return;
  }

  // One bracket, two independent redirects: either may refuse alone.
  unsigned begin = 0, end = 0;
  if (!locate_displayrest(base, "neither redirect goes in", &begin, &end)) return;

  install_sbar(base, begin, end);
  install_scuba(base, begin, end);
}

void shot_remove(void) {
  // The count goes to zero before the first removal, so a thunk still reachable from a site that kept its bytes
  // forwards.
  s_scubaPatched = 0;
  int in = 0, out = 0;
  for (int i = 0; i < SHOT_SCUBA_SITES; i++) {
    if (!s_scubaPatch[i].site) continue;
    in++;
    if (callsite_remove(&s_scubaPatch[i])) out++;
  }
  // Forwarding outlives a failed restore: hook.cpp keeps the patch and stub, so clearing the address would make a
  // failed removal fatal.
  if (in > 0 && out == in) s_scubaOrig = NULL;
  if (in != out)
    ek_log("shot: %d of %d scuba site(s) would not take their original bytes "
           "back, so the thunk stays reachable -- it forwards every call from "
           "here, which draws the mask exactly as the engine would", in - out,
           in);

  if (!s_sbarPatch.site) return;
  if (callsite_remove(&s_sbarPatch)) s_sbarOrig = NULL;
}
