// The ESP: a box and a label over every live actor. Size comes from polymer.cpp:3983-3999: a facing sprite's quad has
// xratio = xrepeat*0.20, yratio = yrepeat*0.25, and polymer-y is -z/16, so height is 4*yrepeat*tilesiz.y in z and
// half-width 0.1*xrepeat*tilesiz.x. The draw refuses rather than size a box from zero.

#include <windows.h>
#include "imgui/imgui.h"
#include "gamestate.h"
#include "project.h"
#include "esp.h"
#include "projmath.h"
#include "targets.h"
#include <math.h>
#include "config.h"
#include "ui/common.h"
#include "ui/canvas.h"
#include "ui/font.h"
#include "ui/wallclock.h"
#include "esp_layout.h"
#include "esp_names.h"
#include "esp_items.h"
#include "esp_breakables.h"
#include "esp_switches.h"
#include "esp_tint.h"
#include "worldquad.h"
#include "tiles.h"
#include "actorstate.h"
#include "playerstate.h"
#include "aimsolve.h"
#include "aim.h"
#include "camera.h"
#include "mapextent.h"
#include "sectorstate.h"
#include "wallstate.h"
#include "secretvol.h"
#include "secrettrig.h"
#include "secretpath.h"
#include "tracer.h"

// The maximum is OBSERVED, not tabulated: EDuke32 strengths come from CON and mods change them. A wounded first
// sighting gets no bar (WT). Keyed on the occupant (ekm_health_latch, esp_names.h) because indices are recycled (E1L1,
// E1L4, E1L1 read two troopers 30/175); an in-session load gives no gm 0 frame to clear on.
static EkmHealthLatch s_health[EK_MAXSPRITES];

void esp_forget_health(void) {
  for (int i = 0; i < EK_MAXSPRITES; i++) {
    s_health[i].max = 0;
    s_health[i].picnum = 0;
    s_health[i].pal = 0;
  }
}

static int health_max_for(int index, short hp, int picnum, int pal) {
  if (index < 0 || index >= EK_MAXSPRITES) return 0;
  return ekm_health_latch(&s_health[index], picnum, pal, (int)hp);
}

// The ceiling already learned for a slot, observing nothing: the collector feeds the latch, so a marker the cap drops
// still teaches it.
static int health_max_seen(int index, int picnum, int pal) {
  if (index < 0 || index >= EK_MAXSPRITES) return 0;
  const EkmHealthLatch& slot = s_health[index];
  if (!ekm_same_enemy(picnum, pal, (int)slot.picnum, (int)slot.pal)) return 0;
  return (int)slot.max;
}

static int esp_dist2d(int cx, int cy, int x, int y) {
  const double dx = (double)x - (double)cx, dy = (double)y - (double)cy;
  return clamp_double_to_int(sqrt(dx * dx + dy * dy) + 0.5);
}

int esp_dist2d_public(int cx, int cy, int x, int y) { return esp_dist2d(cx, cy, x, y); }

static int esp_distance_metres(int cx, int cy, int x, int y) {
  const double dx = (double)x - (double)cx, dy = (double)y - (double)cy;
  const double d = sqrt(dx * dx + dy * dy) / (double)BUILD_UNITS_PER_METRE;
  return clamp_double_to_int(d + 0.5);
}

// Brackets, not an outline, so markers do not merge in a crowd; arithmetic is esp_layout.h's, from WT.
static void esp_draw_brackets(int x, int y, int w, int h, unsigned colour) {
  const int cx = esp_bracket_arm(w);
  const int cy = esp_bracket_arm(h);
  const int t  = esp_bracket_thickness(cx, cy);
  if (t <= 0) return;
  const int x1 = x + w, y1 = y + h;
  canvas_rect(x,       y,       cx, t,  colour);
  canvas_rect(x,       y,       t,  cy, colour);
  canvas_rect(x1 - cx, y,       cx, t,  colour);
  canvas_rect(x1 - t,  y,       t,  cy, colour);
  canvas_rect(x,       y1 - t,  cx, t,  colour);
  canvas_rect(x,       y1 - cy, t,  cy, colour);
  canvas_rect(x1 - cx, y1 - t,  cx, t,  colour);
  canvas_rect(x1 - t,  y1 - cy, t,  cy, colour);
}

static void esp_collect(EkMarker* markers, int* count, int* seq, int* overflow,
                        int kind, int index,
                        const EkScreenPt& p, const EkScreenBox& box) {
  if (*count >= EK_MARKER_COLLECT_MAX) { (*overflow)++; return; }
  EkMarker& m = markers[(*count)++];
  m.kind = kind; m.index = index; m.dist2d = p.dist2d; m.seq = (*seq)++;
  m.p = p; m.box = box;
}

// The frame's camera for the metres label (render thread only). Not derived from m.dist2d: re-rounding moves the
// printed number by one at each half-metre.
static int s_frameCamX = 0, s_frameCamY = 0;

// `m.index` is the slot in THIS frame's targets[] (esp_layout.h). Returns 1 when something reached the canvas.
static int esp_draw_enemy_marker(const EkMarker& m, const EkTarget* targets) {
  const EkTarget& tgt = targets[m.index];
  const short picnum = tgt.picnum;
  const int bx = m.box.x, by = m.box.y;
  const int bw = m.box.w, bh = m.box.h;
  int drew = 0;

  if (g_cfg.espboxes) {
    esp_draw_brackets(bx, by, bw, bh, canvas_esp_colour(true));
    drew = 1;
  }

  const short hp = tgt.hp;
  const short shownHp = tgt.hpShown;

  // GAME.CON's first-tic override, recomputed here (pure); the latch must NOT be recomputed (health_max_seen).
  const int ruledMax = ekm_enemy_ruled_max((int)picnum, (int)tgt.pal);
  const int shown = ekm_display_hp((int)hp,
                                   ekm_enemy_max_strength((int)picnum),
                                   ruledMax,
                                   actorstate_has_run(tgt.sprite));

  const int seen = health_max_seen(tgt.sprite, (int)picnum, (int)tgt.pal);

  // One maximum for bar and label: they disagreed by 4499 on a pal'd Cycloid (`1/4500`). A CON-stated maximum wins,
  // floored at the shown value (esp_names.h ekm_enemy_bar_max).
  const int ruledBar = ekm_enemy_bar_max(ruledMax, shown);
  const int seenMax = ruledBar > 0
      ? ruledBar
      : ekm_enemy_shown_max(ekm_enemy_max_strength((int)picnum), seen);

  // Queued damage comes off the substituted number: a Captain woken by an in-flight shot ends the tic on 60 less
  // damage. hpShown carries the discount (targets.cpp); clamped so it only subtracts.
  const int pendingSeen = (int)hp - (int)shownHp;
  const int displayHp = ekm_actor_shown_extra(shown, pendingSeen);

  if (g_cfg.esphealthbar && hp > 0 && seenMax > 0) {
    const int barX = esp_bar_x(bx);
    const int fill = canvas_bar_fill(bh, displayHp, seenMax);
    canvas_rect(barX, by, ESP_BAR_W, bh, C_WELL);
    if (fill > 0)
      canvas_rect(barX, by + (bh - fill), ESP_BAR_W, fill,
                  canvas_health_colour(displayHp, seenMax));
    drew = 1;
  }

  if (g_cfg.espname) {
    char nmBuf[16];
    // The number is the fallback for an unlisted picnum (a mod's enemy), never a wrong name (esp_names.h).
    const char* enemy = ekm_enemy_name_pal((int)picnum, (int)tgt.pal);
    const char* nm = enemy;
    if (!nm) { wsprintfA(nmBuf, "%d", (int)picnum); nm = nmBuf; }
    const int nameW = canvas_text_width(nm, FONT_SMALL);

    char numBuf[ESP_HEALTH_NUM_BUFSIZE];
    const int maxhp = seenMax;
    // A state label replaces the number: "Playing dead", "Choking" (a live-at-zero trooper would print 0/30).
    const bool haveNum = esp_health_number(numBuf, displayHp, maxhp,
                                           ekm_actor_state_label(tgt.state));
    const int numW = haveNum ? canvas_text_width(numBuf, FONT_MONO) : 0;

    const EspLabelRuns runs = esp_label_runs(bx, bw, nameW, numW);
    const int nameY = esp_name_label_y(by);
    canvas_text(runs.nameX, nameY, nm, C_BONE, FONT_SMALL);
    if (haveNum)
      canvas_text(runs.numX,
                  esp_label_mono_y(nameY, font_height[FONT_SMALL], font_height[FONT_MONO]),
                  numBuf, C_BONE, FONT_MONO);
    drew = 1;
  }

  if (g_cfg.espdistance) {
    char dm[24];
    wsprintfA(dm, "%d m", esp_distance_metres(s_frameCamX, s_frameCamY, tgt.x, tgt.y));
    canvas_text(esp_label_x(bx, bw, canvas_text_width(dm, FONT_SMALL)),
                esp_dist_label_y(by, bh), dm, C_BONEDIM, FONT_SMALL);
    drew = 1;
  }

  return drew;
}

// One world point, projected, culled, collected; a degenerate box is stored (a point has no sprite box). Returns 1 when
// collected: the switch walk needs it so the combination tint follows its marker.
static int esp_collect_point(const EkProjection& p, int kind, int index,
                             int wx, int wy, int wz,
                             EkMarker* markers, int* count, int* seq, int* overflow) {
  const int dist2d = esp_dist2d(p.cam.x, p.cam.y, wx, wy);
  if (!ekm_within_draw_distance(dist2d, g_cfg.espdrawdist, g_cfg.espnolimit)) return 0;
  float fx = 0.0f, fy = 0.0f;
  if (!project_point(p, wx, wy, wz, &fx, &fy)) return 0;
  const EkScreenPt pt = { (int)fx, (int)fy, dist2d };
  const EkScreenBox box = { (int)fx, (int)fy, 0, 0 };
  // "It collected" is "the count moved": restating the cap would be a second copy of the rule.
  const int before = *count;
  esp_collect(markers, count, seq, overflow, kind, index, pt, box);
  return *count != before;
}

// Secret areas: a SECTOR pass, one marker per sector still carrying the secret lotag. No found-set: sector.cpp:3293
// zeroes the lotag when the player stands in it. Sectors alone bound the walk (mapextent_numsectors is one coherent
// read); walls are bounds-checked.
static void esp_collect_secrets(const EkProjection& p, EkMarker* markers,
                                int* count, int* seq, int* overflow) {
  const int ns = mapextent_numsectors();
  for (int sec = 0; sec < ns; sec++) {
    EkSectorInfo si = { 0, 0, 0, 0, 0, 0 };
    if (!sectorstate_info(sec, &si)) continue;
    if (!ekm_sector_is_secret(si.lotag)) continue;

    EkSectorCentre c = { 0, 0, 0, 0, 0, 0 };
    int accepted = 0;
    bool corrupt = false;
    for (int k = 0; k < si.wallnum; k++) {
      const int wallIndex = si.wallptr + k;
      int p2 = 0;
      if (!wallstate_point2(wallIndex, &p2)) { corrupt = true; break; }
      if (!ekm_point2_ok(p2, wallIndex, si.wallptr, si.wallnum)) { corrupt = true; break; }
      EkWallCorner wc = { 0, 0, 0 };
      if (!wallstate_corner(wallIndex, &wc)) { corrupt = true; break; }
      ekm_sector_centre_accumulate(&c, wc.x, wc.y);
      accepted++;
    }
    if (corrupt) continue;
    if (!ekm_sector_centre_finish(&c, accepted)) continue;

    esp_collect_point(p, EK_MARKER_SECRET, sec, c.x, c.y, si.florz,
                      markers, count, seq, overflow);
  }
}

// The secret volume's five numbers per pass, because a blank screen has five look-alike causes: nothing staged, staged
// but none drawn, rooms skipped (secretvol.h caps), walls inexact (drawn flat on sloped geometry), quads pushed.
// Staged, skipped and inexact come from the generation drawn.
static int s_secvolStaged = 0, s_secvolDrawn = 0, s_secvolSkipped = 0,
           s_secvolUnreadable = 0, s_secvolInexact = 0, s_secvolQuads = 0;
int esp_secretvol_staged(void)     { return s_secvolStaged; }
int esp_secretvol_drawn(void)      { return s_secvolDrawn; }
int esp_secretvol_skipped(void)    { return s_secvolSkipped; }
int esp_secretvol_unreadable(void) { return s_secvolUnreadable; }
int esp_secretvol_inexact(void)    { return s_secvolInexact; }
int esp_secretvol_quads(void)      { return s_secvolQuads; }

// Every undiscovered secret room in range, one translucent panel per wall. Drawn by the NEXT frame's flush: this runs
// inside SwapBuffers, after the frame's flush (worldquad.h).
static void esp_push_secret_volumes(const EkProjection& p) {
  if (!secretvol_ready()) return;

  // Acquired once for the pass (secretvol.h): the cache is double-buffered and a found secret REPACKS it, so a second
  // acquisition would return another room's wall.
  const int gen = secretvol_generation();

  s_secvolStaged     = secretvol_rooms(gen);
  s_secvolSkipped    = secretvol_skipped(gen);
  s_secvolUnreadable = secretvol_unreadable(gen);
  s_secvolInexact    = secretvol_inexact(gen);

  for (int i = 0; i < s_secvolStaged; i++) {
    int sectnum = 0, cx = 0, cy = 0, first = 0, n = 0;
    if (!secretvol_room(gen, i, &sectnum, &cx, &cy, &first, &n)) continue;

    // The lotag is re-read live, the one check the scan cannot make: it is zeroed the instant the player stands in the
    // sector, so panels would outlive the cross. Same predicate as the cross (sectorstate.h).
    EkSectorInfo si = { 0, 0, 0, 0, 0, 0, 0, 0 };
    if (!sectorstate_info(sectnum, &si)) continue;
    if (!ekm_sector_is_secret(si.lotag)) continue;

    // Wall count compared because it is in hand: mitigates the map-change window, when an index secret in both maps
    // would draw the OLD walls. Misses the same index with the same count and other vertices (known limit,
    // secretvol.h).
    if (si.wallnum != n) continue;

    const int dist2d = esp_dist2d(p.cam.x, p.cam.y, cx, cy);
    if (!ekm_within_draw_distance(dist2d, g_cfg.espdrawdist, g_cfg.espnolimit))
      continue;

    s_secvolDrawn++;
    for (int k = 0; k < n; k++) {
      const EkSecretWall* const w = secretvol_wall(gen, first + k);
      if (!w) continue;

      double x4[4] = { 0, 0, 0, 0 }, y4[4] = { 0, 0, 0, 0 };
      int z4[4] = { 0, 0, 0, 0 };
      if (ekm_secvol_quad_corners(*w, x4, y4, z4) != 4) continue;

      EkWorldQuad q;
      for (int c = 0; c < 4; c++) { q.x[c] = x4[c]; q.y[c] = y4[c]; q.z[c] = z4[c]; }
      q.argb = C_SECRET_VOLUME;
      worldquad_push(q);
      s_secvolQuads++;
    }
  }
}

// Pickups: a SEPARATE walk from targets_collect, which feeds the aimbot, so a pickup never enters an EkTarget. Pickups
// sit on statnum 2. Gated on g_cfg.espitems.
static void esp_collect_items(const EkProjection& p, EkMarker* markers,
                              int* count, int* seq, int* overflow) {
  for (int i = 0; i < EK_MAXSPRITES; i++) {
    const unsigned char* r = gamestate_sprite(i);
    if (!r) break;

    const short statnum = *(const short*)(r + EK_SPR_STATNUM);
    if (statnum != EK_STAT_ACTOR && statnum != EK_STAT_ZOMBIE) continue;

    const short picnum = *(const short*)(r + EK_SPR_PICNUM);
    const unsigned char pal = r[EK_SPR_PAL];
    const char* nm = ekm_item_name_pal((int)picnum, (int)pal);
    if (!nm) continue;

    const int cstat = (int)*(const short*)(r + EK_SPR_CSTAT);
    const unsigned char xrep = r[EK_SPR_XREPEAT];
    const unsigned char yrep = r[EK_SPR_YREPEAT];
    if (!ekm_item_present(cstat, (int)xrep)) continue;

    // A thrown pipebomb is a pickup only once the engine would hand it back (actors.cpp:5435: t_data[0] > 7, xvel ==
    // 0).
    if (picnum == EKM_PIC_HEAVYHBOMB) {
      const unsigned char* const act = actorstate_record(i);
      if (!act) continue;
      const int tdata0 = *(const int*)(act + EK_ACT_T_DATA);
      const int xvel   = (int)*(const short*)(r + EK_SPR_XVEL);
      if (!ekm_pipebomb_takeable(tdata0, xvel)) continue;
    }

    const int x = *(const int*)(r + EK_SPR_X);
    const int y = *(const int*)(r + EK_SPR_Y);
    const int z = *(const int*)(r + EK_SPR_Z);

    const int dist2d = esp_dist2d(p.cam.x, p.cam.y, x, y);
    if (!ekm_within_draw_distance(dist2d, g_cfg.espdrawdist, g_cfg.espnolimit))
      continue;

    int tw = 0, th = 0;
    tile_size((int)picnum, &tw, &th);
    if (tw <= 0 || th <= 0) { tw = 64; th = 64; }

    const float heightZ = ekm_sprite_height_z((int)yrep, th);
    const int   feetZ   = ekm_feet_z(z, heightZ, (unsigned)cstat);
    const int   height  = (int)heightZ;

    float fx = 0.0f, fy = 0.0f, hx = 0.0f, hy = 0.0f;
    if (!project_point(p, x, y, feetZ, &fx, &fy)) continue;
    if (!project_point(p, x, y, feetZ - height, &hx, &hy)) continue;

    const float boxH = fy - hy;
    if (boxH < 2.0f || boxH > 4000.0f) continue;
    const float halfW = ekm_projected_halfwidth(boxH, xrep, tw, yrep, th);

    const EkScreenPt pt = { (int)fx, (int)fy, dist2d };
    const EkScreenBox box = { (int)(fx - halfW), (int)hy,
                              (int)(halfW * 2.0f), (int)boxH };
    esp_collect(markers, count, seq, overflow, EK_MARKER_ITEM, i, pt, box);
  }
}

// Cracks: a walk of the STANDABLE list. The hitag is the whole liveness test (esp_breakables.h).
static void esp_collect_cracks(const EkProjection& p, EkMarker* markers,
                               int* count, int* seq, int* overflow) {
  for (int i = 0; i < EK_MAXSPRITES; i++) {
    const unsigned char* r = gamestate_sprite(i);
    if (!r) break;
    if (*(const short*)(r + EK_SPR_STATNUM) != EK_STAT_STANDABLE) continue;
    const short picnum = *(const short*)(r + EK_SPR_PICNUM);
    if (!ekm_crack_name((int)picnum)) continue;
    if (!ekm_crack_live((int)*(const short*)(r + EK_SPR_HITAG))) continue;
    esp_collect_point(p, EK_MARKER_CRACK, i,
                      *(const int*)(r + EK_SPR_X),
                      *(const int*)(r + EK_SPR_Y),
                      *(const int*)(r + EK_SPR_Z),
                      markers, count, seq, overflow);
  }
}

// Wall marks refreshed on a period (the file's most expensive walk). Only WHICH wall and WHERE is cached; the picnum is
// re-read live, so a stale cache is only late to notice a new mark.
struct EkWallMark { int kind, wallnum, x, y, z; };
static EkWallMark s_wallMarks[EK_WALL_MARK_MAX];
static int s_wallCount = 0, s_wallOverflow = 0, s_wallScanned = 0, s_wallSig = 0;
static unsigned s_wallScanMs = 0;

// Render thread; NEVER calls mapextent_scan() (scan thread), only reads the published pair.
static void esp_scan_walls(void) {
  // Off means no scan and no latched marks. Called unconditionally for this. Gate is OR, clear is AND: one walk feeds
  // both kinds and neither row gates the other.
  if (!g_cfg.espbreakables && !g_cfg.espswitches) {
    s_wallCount = 0; s_wallOverflow = 0; s_wallScanned = 0; return;
  }

  // mapextent_ready() is part of the gate: unbounded, the walk reads the previous map's records.
  if (!mapextent_ready()) return;

  int ns = 0, nw = 0;
  if (!mapextent_extent(&ns, &nw)) return;

  const int sig = (ns << 16) ^ nw;
  const unsigned now = wallclock_ms();
  if (!ekm_wall_scan_due(s_wallScanned, sig, s_wallSig, now, s_wallScanMs)) return;
  s_wallScanMs = now;
  s_wallSig = sig;
  s_wallScanned = 1;

  int count = 0, overflow = 0;
  for (int sec = 0; sec < ns; sec++) {
    EkSectorInfo si = { 0, 0, 0, 0, 0, 0 };
    if (!sectorstate_info(sec, &si)) continue;

    const int midz = ekm_wall_mid_z(si.florz, si.ceilz);

    for (int k = 0; k < si.wallnum; k++) {
      const int wallIndex = si.wallptr + k;
      EkWallFace f = { 0, 0, 0, 0 };
      if (!wallstate_face(wallIndex, &f)) continue;

      // Which kind, tag read last (a second guarded read). The lotag term is load-bearing: P_ActivateSwitch's wall
      // branch returns 0 on lotag 0 (sector.cpp:1294) and a door tile is its own base, so every DOORTILE face would
      // take a slot.
      int kind = -1;
      if (ekm_breakable_name(f.picnum)) {
        kind = EK_WKIND_BREAKABLE;
      } else if (ekm_switch_base_for(f.picnum) >= 0) {
        EkWallTags t = { 0, 0, 0 };
        if (wallstate_tags(wallIndex, &t) && t.lotag != 0) kind = EK_WKIND_SWITCH;
      }
      if (kind < 0) continue;

      int p2 = 0;
      if (!wallstate_point2(wallIndex, &p2)) continue;
      if (!ekm_point2_ok(p2, wallIndex, si.wallptr, si.wallnum)) continue;

      EkWallCorner a = { 0, 0, 0 }, b = { 0, 0, 0 };
      if (!wallstate_corner(wallIndex, &a)) continue;
      if (!wallstate_corner(p2, &b)) continue;

      if (count >= EK_WALL_MARK_MAX) { overflow++; continue; }
      EkWallMark& wm = s_wallMarks[count++];
      wm.kind    = kind;
      wm.wallnum = wallIndex;
      wm.x = ekm_wall_mid(a.x, b.x);
      wm.y = ekm_wall_mid(a.y, b.y);
      wm.z = midz;
    }
  }

  s_wallCount = count;
  s_wallOverflow = overflow;
}

// Breakable panels out of the cache, picnum re-read live: membership is the liveness test (esp_breakables.h).
static void esp_collect_breakables(const EkProjection& p, EkMarker* markers,
                                   int* count, int* seq, int* overflow) {
  const int n = ekm_wall_mark_count_clamped(s_wallCount);
  for (int i = 0; i < n; i++) {
    const EkWallMark& wm = s_wallMarks[i];
    if (wm.kind != EK_WKIND_BREAKABLE) continue;
    EkWallFace f = { 0, 0, 0, 0 };
    if (!wallstate_face(wm.wallnum, &f)) continue;
    if (!ekm_breakable_name(f.picnum)) continue;
    esp_collect_point(p, EK_MARKER_BREAKABLE, wm.wallnum, wm.x, wm.y, wm.z,
                      markers, count, seq, overflow);
  }
}

// The combination lock's tint: a translucent BOX around the model, green Flip, red Leave (rules in esp_tint.h). A
// volume because with r_models a quad on the sprite plane lost four hardware rounds (offset, larger offset, pull, lift,
// shrink). Drawn by the NEXT frame's flush (worldquad.h). No draw-point check: worldquad_push counts it.
static int s_comboSeen = 0, s_comboPushed = 0, s_comboBoxed = 0, s_comboQuads = 0;
int esp_combination_seen(void) { return s_comboSeen; }
int esp_combination_pushed(void) { return s_comboPushed; }
int esp_combination_boxed(void) { return s_comboBoxed; }
int esp_combination_quads(void) { return s_comboQuads; }

static void esp_push_combination_tint(const unsigned char* r,
                                      const EkSwitchInfo& si) {
  const int verdict = ekm_tint_verdict(si.family, si.line);
  if (verdict == EKM_TINT_NONE) return;

  // Only a wall-aligned sprite (cstat bits 4-5, buildtypes.h:134-138).
  const int cstat = (int)*(const unsigned short*)(r + EK_SPR_CSTAT);
  if ((cstat & 0x30) != 0x10) return;

  const int picnum = (int)*(const short*)(r + EK_SPR_PICNUM);
  int tw = 0, th = 0;
  tile_size(picnum, &tw, &th);
  if (tw <= 0 || th <= 0) return;

  // No bounds, no volume, and no flat-quad fallback (two appearances cannot be told apart). Marker and label stay;
  // EK_MDL_NONE and EK_MDL_UNREADY are silent here.
  EkModelBox box;
  if (modelbounds_for(picnum, (int)r[EK_SPR_PAL], &box) != EK_MDL_OK) return;

  // The model's origin, not tspr->z: polymer.cpp:4510 translates a y-centred sprite's model down by 2*tilesiz.y*yrepeat
  // (esp_tint.h).
  const int originZ = ekm_tint_model_origin_z(*(const int*)(r + EK_SPR_Z),
                                              (int)r[EK_SPR_YREPEAT], th,
                                              (cstat & 128) ? 1 : 0);

  EkWorldQuad faces[6];
  const int n = ekm_tint_box_faces(box, *(const int*)(r + EK_SPR_X),
                                   *(const int*)(r + EK_SPR_Y), originZ,
                                   (int)*(const short*)(r + EK_SPR_ANG),
                                   (int)r[EK_SPR_XREPEAT],
                                   (int)r[EK_SPR_YREPEAT], faces,
                                   ekm_tint_colour(verdict, C_TINT_BOX_FLIP,
                                                   C_TINT_BOX_LEAVE));
  // Counted here, not at the bounds, so `boxed` means a box the face rule accepted.
  if (n <= 0) return;
  s_comboBoxed++;

  // All six or none: the secret volume pushes arbitrary counts ahead, so a cap mid-box would leave floating faces. The
  // refusal is silent: worldquad_push owns that count.
  if (ekm_world_quad_room(worldquad_count(), EK_WORLD_QUAD_MAX) < n) return;

  for (int i = 0; i < n; i++) { worldquad_push(faces[i]); s_comboQuads++; }
}

// Switch sprites: everything the classifier names, off the DEFAULT list (gamestate.h). Whether the level has a secret
// exit is set as walks CLASSIFY (an unfound one is off screen); cleared per frame.
static bool s_levelHasSecretExit = false;
bool esp_level_has_secret_exit(void) { return s_levelHasSecretExit; }

static void esp_collect_switch_sprites(const EkProjection& p, int gotAccess,
                                       EkMarker* markers, int* count, int* seq,
                                       int* overflow) {
  for (int i = 0; i < EK_MAXSPRITES; i++) {
    const unsigned char* r = gamestate_sprite(i);
    if (!r) break;
    if (*(const short*)(r + EK_SPR_STATNUM) != EK_STAT_DEFAULT) continue;

    const EkSwitchInfo si = ekm_switch_classify(
        (int)*(const short*)(r + EK_SPR_PICNUM), (int)r[EK_SPR_PAL],
        (int)*(const short*)(r + EK_SPR_HITAG), (int)*(const short*)(r + EK_SPR_LOTAG),
        gotAccess);
    if (si.family == EK_SW_NONE) continue;
    if (si.family == EK_SW_EXIT && si.exitKind == EKM_EXIT_SECRET)
      s_levelHasSecretExit = true;

    const int marked = esp_collect_point(p, EK_MARKER_SWITCH_SPRITE, i,
                                         *(const int*)(r + EK_SPR_X),
                                         *(const int*)(r + EK_SPR_Y),
                                         *(const int*)(r + EK_SPR_Z),
                                         markers, count, seq, overflow);

    // The tint follows its own marker: the culls live in esp_collect_point. It follows the collect, not the final
    // selection; the layer's cap of 64 covers the difference.
    if (si.family == EK_SW_COMBINATION) s_comboSeen++;
    if (marked && g_cfg.esptint && si.family == EK_SW_COMBINATION) {
      s_comboPushed++;
      esp_push_combination_tint(r, si);
    }
  }
}

// The exit button needs its own walk: NUKEBUTTON is the one switch moved onto the MISC list (gamestate.h). Sequential,
// never nested.
static void esp_collect_exit_buttons(const EkProjection& p, EkMarker* markers,
                                     int* count, int* seq, int* overflow) {
  for (int i = 0; i < EK_MAXSPRITES; i++) {
    const unsigned char* r = gamestate_sprite(i);
    if (!r) break;
    if (*(const short*)(r + EK_SPR_STATNUM) != EK_STAT_MISC) continue;
    if (*(const short*)(r + EK_SPR_PICNUM) != EKM_PIC_NUKEBUTTON) continue;

    if (ekm_switch_exit_kind((int)*(const short*)(r + EK_SPR_PICNUM),
                             (int)r[EK_SPR_PAL]) == EKM_EXIT_SECRET)
      s_levelHasSecretExit = true;

    esp_collect_point(p, EK_MARKER_SWITCH_SPRITE, i,
                      *(const int*)(r + EK_SPR_X),
                      *(const int*)(r + EK_SPR_Y),
                      *(const int*)(r + EK_SPR_Z),
                      markers, count, seq, overflow);
  }
}

// Switch walls out of the breakable cache, re-classified LIVE. No combination tint here (scoped limit): esp_tint.h's
// span is the wall-SPRITE branch; WT is scoped alike (wtmod/esp.cpp:462).
static void esp_collect_switch_walls(const EkProjection& p, int gotAccess,
                                     EkMarker* markers, int* count, int* seq,
                                     int* overflow) {
  const int n = ekm_wall_mark_count_clamped(s_wallCount);
  for (int i = 0; i < n; i++) {
    const EkWallMark& wm = s_wallMarks[i];
    if (wm.kind != EK_WKIND_SWITCH) continue;
    EkWallFace f = { 0, 0, 0, 0 };
    EkWallTags t = { 0, 0, 0 };
    if (!wallstate_face(wm.wallnum, &f)) continue;
    if (!wallstate_tags(wm.wallnum, &t)) continue;
    const EkSwitchInfo sw = ekm_switch_classify(f.picnum, t.pal, t.hitag, t.lotag, gotAccess);
    if (sw.family == EK_SW_NONE) continue;
    esp_collect_point(p, EK_MARKER_SWITCH_WALL, wm.wallnum, wm.x, wm.y, wm.z,
                      markers, count, seq, overflow);
  }
}

// Items: blue corner brackets, name above; no health or distance. `m.index` is the SPRITE index. Returns 1 when it
// drew: a slot recycled since collection no longer holds a pickup.
static int esp_draw_item_marker(const EkMarker& m) {
  const unsigned char* r = gamestate_sprite(m.index);
  if (!r) return 0;
  const short picnum = *(const short*)(r + EK_SPR_PICNUM);
  const unsigned char pal = r[EK_SPR_PAL];
  const char* nm = ekm_item_name_pal((int)picnum, (int)pal);
  if (!nm) return 0;

  // A keycard wears its own colour; ekm_keycard_colour returns 0 for a pal with no lock colour, so an odd-pal card
  // falls back to the pickup mark.
  const unsigned card = (picnum == EKM_PIC_ACCESSCARD)
                      ? ekm_keycard_colour((int)pal) : 0u;
  const unsigned markColour = card ? card : canvas_esp_colour(false);
  const unsigned textColour = card ? card : C_BONE;

  esp_draw_brackets(m.box.x, m.box.y, m.box.w, m.box.h, markColour);
  canvas_text(esp_label_x(m.box.x, m.box.w, canvas_text_width(nm, FONT_SMALL)),
              esp_name_label_y(m.box.y), nm, textColour, FONT_SMALL);
  return 1;
}

// The cross every point marker draws: two bars on one centre pixel, label above, optional line below. Shared by secret,
// crack, breakable and four switch shapes. Returns 1 when it drew.
static int esp_draw_cross_mark(const EkScreenPt& p, unsigned colour,
                               const char* label, const char* line2) {
  canvas_rect(esp_secret_hbar_x(p.x), esp_secret_hbar_y(p.y),
              esp_secret_bar_span(), ESP_SECRET_T, colour);
  canvas_rect(esp_secret_vbar_x(p.x), esp_secret_vbar_y(p.y),
              ESP_SECRET_T, esp_secret_bar_span(), colour);
  if (label)
    canvas_text(esp_label_x(p.x, 0, canvas_text_width(label, FONT_SMALL)),
                esp_secret_name_label_y(p.y), label, colour, FONT_SMALL);
  if (line2)
    canvas_text(esp_label_x(p.x, 0, canvas_text_width(line2, FONT_SMALL)),
                esp_secret_dist_label_y(p.y), line2, C_BONEDIM, FONT_SMALL);
  return 1;
}

// A keycard lock draws a CARD, not a cross, in that card's colour with the hint beneath.
static int esp_draw_card_mark(const EkScreenPt& p, unsigned colour,
                              const char* label, const char* line2) {
  canvas_round_rect_outline(esp_card_x(p.x), esp_card_y(p.y),
                            ESP_CARD_W, ESP_CARD_H, ESP_CARD_R, ESP_CARD_T, colour);
  canvas_rect(esp_card_notch_x(p.x), esp_card_notch_y(p.y),
              ESP_CARD_NOTCH, ESP_CARD_NOTCH, colour);
  if (label)
    canvas_text(esp_label_x(p.x, 0, canvas_text_width(label, FONT_SMALL)),
                esp_card_name_label_y(p.y), label, colour, FONT_SMALL);
  if (line2)
    canvas_text(esp_label_x(p.x, 0, canvas_text_width(line2, FONT_SMALL)),
                esp_card_dist_label_y(p.y), line2, colour, FONT_SMALL);
  return 1;
}

// Secret areas: crossing bars, label above, 2D distance below.
static int esp_draw_secret_marker(const EkMarker& m) {
  char dist[16];
  wsprintfA(dist, "%d m", (m.p.dist2d + BUILD_UNITS_PER_METRE / 2) / BUILD_UNITS_PER_METRE);
  return esp_draw_cross_mark(m.p, canvas_esp_secret_colour(), ESP_SECRET_LABEL, dist);
}

// Name re-derived from the live picnum, so a recycled slot stops drawing rather than mislabelling.
static int esp_draw_crack_marker(const EkMarker& m) {
  const unsigned char* r = gamestate_sprite(m.index);
  if (!r) return 0;
  const char* nm = ekm_crack_name((int)*(const short*)(r + EK_SPR_PICNUM));
  if (!nm) return 0;
  return esp_draw_cross_mark(m.p, canvas_esp_explodable_colour(), nm, 0);
}

static int esp_draw_breakable_marker(const EkMarker& m) {
  EkWallFace f = { 0, 0, 0, 0 };
  if (!wallstate_face(m.index, &f)) return 0;
  const char* nm = ekm_breakable_name(f.picnum);
  if (!nm) return 0;
  return esp_draw_cross_mark(m.p, canvas_esp_explodable_colour(), nm, 0);
}

// A switch's look is decided once from the classifier's output plus the pal: it is both sprite and wall face, and
// differing shapes would be a defect no pin catches (esp.cpp is not in test_host).
static int esp_draw_switch_shape(const EkScreenPt& p, const EkSwitchInfo& si, int pal) {
  if (si.family == EK_SW_ACCESS) {
    const unsigned card = ekm_keycard_colour(pal);
    return esp_draw_card_mark(p, card ? card : canvas_esp_switch_colour(),
                              EKM_SWITCH_LABEL, si.line);
  }

  // The exit's own line is its name ("Exit"/"Secret exit"), so it takes the label row; both use the secret colour.
  if (si.family == EK_SW_EXIT)
    return esp_draw_cross_mark(p, canvas_esp_secret_colour(), si.line, 0);

  return esp_draw_cross_mark(p, canvas_esp_switch_colour(),
                             si.family == EK_SW_DOOR ? EKM_DOOR_LABEL : EKM_SWITCH_LABEL,
                             si.line);
}

// Re-classified from the live tile, so the card hint disappears the frame the card is picked up.
static int esp_draw_switch_sprite_marker(const EkMarker& m, int gotAccess) {
  const unsigned char* r = gamestate_sprite(m.index);
  if (!r) return 0;
  const int pal = (int)r[EK_SPR_PAL];
  const EkSwitchInfo si = ekm_switch_classify(
      (int)*(const short*)(r + EK_SPR_PICNUM), pal,
      (int)*(const short*)(r + EK_SPR_HITAG), (int)*(const short*)(r + EK_SPR_LOTAG),
      gotAccess);
  if (si.family == EK_SW_NONE) return 0;
  return esp_draw_switch_shape(m.p, si, pal);
}

static int esp_draw_switch_wall_marker(const EkMarker& m, int gotAccess) {
  EkWallFace f = { 0, 0, 0, 0 };
  EkWallTags t = { 0, 0, 0 };
  if (!wallstate_face(m.index, &f)) return 0;
  if (!wallstate_tags(m.index, &t)) return 0;
  const EkSwitchInfo si = ekm_switch_classify(f.picnum, t.pal, t.hitag, t.lotag, gotAccess);
  if (si.family == EK_SW_NONE) return 0;
  return esp_draw_switch_shape(m.p, si, t.pal);
}

// The secret trigger's five numbers per pass: `seen` (0 = none or not answered), `staged`, `drawn` (after range, live
// reach, near-plane clip), and `refused`/`dropped` apart (raise EK_TRIG_MAX_CHAINS vs the scan never looked at part of
// the map).
static int s_trigSeen = 0, s_trigStaged = 0, s_trigDrawn = 0,
           s_trigRefused = 0, s_trigDropped = 0;
int esp_secrettrig_seen(void)    { return s_trigSeen; }
int esp_secrettrig_staged(void)  { return s_trigStaged; }
int esp_secrettrig_drawn(void)   { return s_trigDrawn; }
int esp_secrettrig_refused(void) { return s_trigRefused; }
int esp_secrettrig_dropped(void) { return s_trigDropped; }

// Every staged chain in range: a line from the hidden trigger to the centre of the sector it operates. Screen space
// (canvas_line is 2D). Does not feed esp_draw's `drawn`, which counts markers; s_trigDrawn reports.
static void esp_draw_secret_triggers(const EkProjection& p) {
  // Acquired once (secrettrig.h): chains are REPACKED every scan. -1 before the first walk, which accessors refuse.
  const int gen = secrettrig_generation();

  s_trigSeen    = secrettrig_seen(gen);
  s_trigStaged  = secrettrig_chains(gen);
  s_trigRefused = secrettrig_refused(gen);
  s_trigDropped = secrettrig_dropped(gen);

  for (int i = 0; i < s_trigStaged; i++) {
    const EkTrigChain* const ch = secrettrig_chain(gen, i);
    if (!ch) continue;

    // Culled at the TRIGGER end, where the player stands; first, so out-of-range chains pay for nothing below.
    const int dist2d = esp_dist2d(p.cam.x, p.cam.y, ch->tx, ch->ty);
    if (!ekm_within_draw_distance(dist2d, g_cfg.espdrawdist, g_cfg.espnolimit))
      continue;

    // Reach re-tested live: sector.cpp:3293 zeroes the lotag when the player stands there, so the line would outlive
    // the cross. It is the REACH, not the operated sector's lotag (an OPENS chain's sector is the door). Through
    // ekm_trig_reaches_secret; at most one wall loop per in-range chain.
    EkSectorInfo si = { 0, 0, 0, 0, 0, 0, 0, 0 };
    if (!sectorstate_info(ch->sectnum, &si)) continue;
    const int isSecret = ekm_sector_is_secret(si.lotag) ? 1 : 0;
    int adjoins = 0;
    for (int k = 0; !isSecret && !adjoins && k < si.wallnum; k++) {
      EkWallCorner wc = { 0, 0, 0 };
      if (!wallstate_corner(si.wallptr + k, &wc)) break;
      if (wc.nextsector < 0) continue;
      EkSectorInfo nsi = { 0, 0, 0, 0, 0, 0, 0, 0 };
      if (sectorstate_info(wc.nextsector, &nsi) && ekm_sector_is_secret(nsi.lotag))
        adjoins = 1;
    }
    if (!ekm_trig_reaches_secret(isSecret, adjoins)) continue;

    // Clipped at the near plane, not refused: demanding both endpoints made lines vanish as the player turned (WT fixed
    // it, wtmod/trigger.h:100-152). worldline_draw (tracer.h) is hardware-proven. The far end is the operated sector's
    // centre (the cross's rule), or the door when it only borders a secret.
    const unsigned colour = canvas_esp_secret_colour();
    EkWorldLine ln = { 0.0f, 0.0f, 0.0f, 0.0f, 0, 0 };
    const bool drew = worldline_draw(p, ch->tx, ch->ty, ch->tz,
                                     ch->cx, ch->cy, si.florz, colour, &ln);
    if (!drew) continue;

    // Label only when the trigger end is really there; a clipped end is a point on the near plane.
    if (ln.aFront) {
      const EkScreenPt pt = { (int)ln.ax, (int)ln.ay, dist2d };
      const char* const label = ekm_trig_label(ch->effect);
      if (ch->triggerKind == EK_TRIG_TOUCHPLATE) {
        // A touchplate gets a marker, a switch does not: the plate is an invisible sprite while a switch carries its
        // own cross.
        esp_draw_cross_mark(pt, colour, label, 0);
      } else {
        // One row above the name only when Switches draws a label there. A case the gate misses:
        // esp_collect_switch_sprites requires statnum DEFAULT while secrettrig.h admits a switch on STAT_ACTIVATOR,
        // never marked, so its label sits a row high.
        const int nameY = esp_secret_name_label_y(pt.y);
        canvas_text(esp_label_x(pt.x, 0, canvas_text_width(label, FONT_SMALL)),
                    g_cfg.espswitches ? nameY - font_height[FONT_SMALL] - 1 : nameY,
                    label, colour, FONT_SMALL);
      }
    }

    s_trigDrawn++;
  }
}

// The path's three numbers: `points` published, `segments` reaching the screen, `crosses` teleport hops. Points 0 means
// no route; segments 0 with points > 0 means behind or beyond the player.
static int s_pathPoints = 0, s_pathSegments = 0, s_pathCrosses = 0;
int esp_secretpath_points(void)   { return s_pathPoints; }
int esp_secretpath_segments(void) { return s_pathSegments; }
int esp_secretpath_crosses(void)  { return s_pathCrosses; }

// Teleport hop cross: the secret marker's shape at HALF its arm (full size reads as a smudge). Widened and clamped:
// projection saturates.
static void esp_draw_path_cross(int cx, int cy, unsigned colour) {
  const int arm = ESP_SECRET_ARM / 2;
  const int span = arm * 2 + 1;
  canvas_rect(clamp_ll_to_int((long long)cx - arm),
              clamp_ll_to_int((long long)cy - ESP_SECRET_T / 2),
              span, ESP_SECRET_T, colour);
  canvas_rect(clamp_ll_to_int((long long)cx - ESP_SECRET_T / 2),
              clamp_ll_to_int((long long)cy - arm),
              ESP_SECRET_T, span, colour);
}

// The published route: polyline through turn points, a cross per teleport hop, a label at the first turn. Screen space.
// SEGMENTS are clipped (worldline_draw), CROSSES and LABELS are projected and absent when behind. Does not feed
// `drawn`; s_pathSegments reports.
static void esp_draw_secret_path(const EkProjection& p) {
  const int gen = secretpath_generation();

  s_pathPoints = secretpath_count(gen);
  if (s_pathPoints <= 0) return;

  const unsigned colour = canvas_esp_secret_colour();
  const int hops = secretpath_hops(gen);

  int prevX = 0, prevY = 0, prevZ = 0, prevOk = 0, prevKind = EK_SPATH_EDGE_WALK;
  for (int k = 0; k < s_pathPoints; k++) {
    const EkSpathPoint* const pt = secretpath_point(gen, k);
    if (!pt) { prevOk = 0; continue; }

    const int dist2d = esp_dist2d(p.cam.x, p.cam.y, pt->x, pt->y);
    const int inRange =
        ekm_within_draw_distance(dist2d, g_cfg.espdrawdist, g_cfg.espnolimit) ? 1 : 0;

    // No segment out of a teleport hop: p[k] is the SOURCE effector and p[k+1] a doorway in the DESTINATION, so the
    // line is the engine's move; the cross replaces it (secretpath.h). The segment INTO a hop is a real walk.
    if (prevOk && inRange && prevKind != EK_SPATH_EDGE_TELEPORT) {
      EkWorldLine ln = { 0.0f, 0.0f, 0.0f, 0.0f, 0, 0 };
      const bool drew = worldline_draw(p, prevX, prevY, prevZ,
                                       pt->x, pt->y, pt->z, colour, &ln);
      if (drew) s_pathSegments++;
    }

    if (inRange) {
      float sx = 0.0f, sy = 0.0f;
      const bool onScreen = project_point(p, pt->x, pt->y, pt->z, &sx, &sy);
      if (onScreen) {
        if (pt->kind == EK_SPATH_EDGE_TELEPORT) {
          esp_draw_path_cross((int)sx, (int)sy, colour);
          s_pathCrosses++;
        }
        // One label at the first turn with the SEARCH's hop count; with the scan's outCap it equals the published
        // points, so it can exceed segments drawn but never the points.
        if (k == 0) {
          char lab[32];
          wsprintfA(lab, "Secret: %d %s", hops, ekm_spath_room_word(hops));
          canvas_text(esp_label_x((int)sx, 0, canvas_text_width(lab, FONT_SMALL)),
                      esp_secret_name_label_y((int)sy), lab, colour, FONT_SMALL);
        }
      }
    }

    prevX = pt->x; prevY = pt->y; prevZ = pt->z;
    prevOk = inRange; prevKind = pt->kind;
  }
}

// Scratch in .bss for size (1024 markers, about 45 KB, too much for the stack in SwapBuffers). Not cross-frame state;
// refilled each call. Render thread only.
static EkMarker s_markers[EK_MARKER_COLLECT_MAX];

int esp_draw(int* notDrawn, int* refused, int* wallOverflow) {
  if (notDrawn)     *notDrawn     = 0;
  if (refused)      *refused      = 0;
  if (wallOverflow) *wallOverflow = 0;

  // The quad list's pass boundary: the flush is inside G_DrawRooms, which returns before renderDrawMasks on the
  // automap, help screen and cursectnum == -1 (game.cpp:799), while this collect keeps running; resetting only there
  // latched duplicate quads. The flush runs first within a frame, so no pending set is discarded. Above every gate.
  worldquad_reset();
  s_comboSeen = 0;
  s_comboPushed = 0;
  s_comboBoxed = 0;
  s_comboQuads = 0;
  // Zeroed with the list it counts, above every gate, so an early-returned pass reports itself.
  s_secvolStaged = 0;
  s_secvolDrawn = 0;
  s_secvolSkipped = 0;
  s_secvolUnreadable = 0;
  s_secvolInexact = 0;
  s_secvolQuads = 0;
  s_trigSeen = 0;
  s_trigStaged = 0;
  s_trigDrawn = 0;
  s_trigRefused = 0;
  s_trigDropped = 0;
  s_pathPoints = 0;
  s_pathSegments = 0;
  s_pathCrosses = 0;

  if (!gamestate_ready()) return 0;

  int gm = 0;
  const bool gmOk = player_read_gm(&gm);

  // The per-level reset: esp_forget_health was never called, so a slot that held a Battlelord printed a trooper as
  // 30/4500. Above the row's gate. ekm_level_loading, not !esp_level_loaded, which is also false over the pause menu
  // (tb_0072).
  if (ekm_level_loading(gm, gmOk)) esp_forget_health();

  // Enemies, Items, Secret areas and Explodables are independent parents: the early return fires only when all are off;
  // each walk has its own switch. The enemy sub-rows choose WHAT a marker draws, g_cfg.esp WHETHER.
  if (!g_cfg.esp &&
      !g_cfg.espitems &&
      !g_cfg.espsecrets &&
      !g_cfg.espexplodables &&
      !g_cfg.espbreakables &&
      !g_cfg.espswitches) return 0;
  if (!esp_level_loaded(gm, gmOk)) return 0;

  EkProjection p;
  if (!project_begin(&p)) return 0;
  s_frameCamX = p.cam.x;
  s_frameCamY = p.cam.y;

  int drawn = 0;

  EkTarget targets[EK_TARGET_MAX];
  // The walk belongs to the enemy row: with Enemies off only the item walk runs.
  const int count = g_cfg.esp ? targets_collect(targets, EK_TARGET_MAX) : 0;

  int markerCount = 0, markerSeq = 0, markerOverflow = 0;

  for (int t = 0; t < count; t++) {
    const EkTarget& tgt = targets[t];

    const int x = tgt.x;
    const int y = tgt.y;

    const int dist2d = esp_dist2d(p.cam.x, p.cam.y, x, y);
    if (!ekm_within_draw_distance(dist2d, g_cfg.espdrawdist, g_cfg.espnolimit)) continue;

    const short picnum = tgt.picnum;
    const unsigned char xrep = tgt.xrep;
    const unsigned char yrep = tgt.yrep;
    const int tw = tgt.tw;
    const int th = tgt.th;

    // sprite.z is the FEET; the quad extends upward (polymer.cpp:4020-4025) unless cstat bit 7 centres it.
    const int height = (int)tgt.heightZ;
    const int feetZ = tgt.feetZ;

    float fx = 0.0f, fy = 0.0f, hx = 0.0f, hy = 0.0f;
    if (!project_point(p, x, y, feetZ, &fx, &fy)) continue;
    if (!project_point(p, x, y, feetZ - height, &hx, &hy)) continue;

    const float boxH = fy - hy;
    if (boxH < 2.0f || boxH > 4000.0f) continue;

    // Half-width through the same projected scale as the height. Build has two scales (z is 16x): dividing a horizontal
    // half-width by a z height made boxes sixteen times too narrow. Both are horizontal here: 0.1*xrepeat*tilesiz.x and
    // 0.25*yrepeat*tilesiz.y.
    const float halfW = ekm_projected_halfwidth(boxH, xrep, tw, yrep, th);

    // Colours come from canvas.h so ESP and panel agree.
    const bool anyMark = g_cfg.espboxes || g_cfg.esphealthbar ||
                         g_cfg.espname || g_cfg.espdistance;
    if (!anyMark) continue;

    const short hp = tgt.hp;

    // Observed at collection, not draw, so the cap cannot cost a ceiling. GAME.CON's first-tic override is applied
    // first: a dormant Assault Captain says 30 and has 60 (`ifai 0` not yet run); a pal'd Battlelord is set to 1000, a
    // pal'd Overlord or Cycloid to 1 (esp_names.h).
    const int ruledMax = ekm_enemy_ruled_max((int)picnum, (int)tgt.pal);
    const int shown = ekm_display_hp((int)hp,
                                     ekm_enemy_max_strength((int)picnum),
                                     ruledMax,
                                     actorstate_has_run(tgt.sprite));

    // Fed `shown`: it MUST see the substitution (or a dormant Captain reads 60/30) and MUST NOT see the pending-damage
    // discount.
    health_max_for(tgt.sprite, (short)shown, (int)picnum, (int)tgt.pal);

    const int bx = (int)(fx - halfW), by = (int)hy;
    const int bw = (int)(halfW * 2.0f), bh = (int)boxH;

    const EkScreenPt  pt  = { (int)fx, (int)fy, dist2d };
    const EkScreenBox box = { bx, by, bw, bh };
    esp_collect(s_markers, &markerCount, &markerSeq, &markerOverflow,
                EK_MARKER_ENEMY, t, pt, box);
  }

  if (g_cfg.espitems)
    esp_collect_items(p, s_markers, &markerCount, &markerSeq, &markerOverflow);

  // mapextent_ready() is part of the gate: a category that cannot bound its walk must not walk. esp.cpp never calls
  // mapextent_scan() (scan thread).
  if (g_cfg.espsecrets && mapextent_ready())
    esp_collect_secrets(p, s_markers, &markerCount, &markerSeq, &markerOverflow);

  // Sub-row gate inside the parent's, no mapextent_ready() term: the panels come from secretvol's cache.
  if (g_cfg.espsecrets && g_cfg.secretvolume)
    esp_push_secret_volumes(p);

  // Same for the chains (secrettrig's cache). BEFORE the marker loop: canvas draws in call order, so lines go under the
  // crosses.
  if (g_cfg.espsecrets && g_cfg.secrettriggers)
    esp_draw_secret_triggers(p);

  // Same for the route (secretpath's cache); after the trigger lines, before the markers.
  if (g_cfg.espsecrets && g_cfg.secretpath)
    esp_draw_secret_path(p);

  if (g_cfg.espexplodables)
    esp_collect_cracks(p, s_markers, &markerCount, &markerSeq, &markerOverflow);

  // The scan is ungated: its first branch clears the cache when BOTH rows are off, and gating would leave marks latched
  // for the next switch-on. No wider staleness window (ekm_wall_scan_due measures wall-clock).
  esp_scan_walls();
  if (g_cfg.espbreakables)
    esp_collect_breakables(p, s_markers, &markerCount, &markerSeq, &markerOverflow);

  // Once per frame, not per sprite (hundreds of guarded reads). A failed read passes 0, which shows every card hint:
  // noise beats the ESP going quiet on a lock the player cannot open.
  int gotAccess = 0;
  // Cleared above the row gate so switching the row off leaves the note off.
  s_levelHasSecretExit = false;
  if (g_cfg.espswitches) {
    if (!player_read_got_access(&gotAccess)) gotAccess = 0;
    // Two walks, sequential, never nested: the exit button is not on the switch list (gamestate.h).
    esp_collect_switch_sprites(p, gotAccess, s_markers,
                               &markerCount, &markerSeq, &markerOverflow);
    esp_collect_exit_buttons(p, s_markers,
                             &markerCount, &markerSeq, &markerOverflow);
    esp_collect_switch_walls(p, gotAccess, s_markers,
                             &markerCount, &markerSeq, &markerOverflow);
  }

  // Select, clamp, order, in that order: clamping first made `keep >= count` and the cap lowest-sprite-index. The cap's
  // discard is counted too.
  ekm_keep_nearest(s_markers, markerCount, EK_MARKER_MAX);
  const int markerCapped = markerCount > EK_MARKER_MAX ? markerCount - EK_MARKER_MAX : 0;
  markerCount -= markerCapped;
  ekm_sort_far_to_near(s_markers, markerCount);

  for (int i = 0; i < markerCount; i++) {
    switch (s_markers[i].kind) {
      case EK_MARKER_ENEMY: drawn += esp_draw_enemy_marker(s_markers[i], targets); break;
      case EK_MARKER_ITEM:  drawn += esp_draw_item_marker(s_markers[i]);           break;
      case EK_MARKER_SECRET: drawn += esp_draw_secret_marker(s_markers[i]);        break;
      case EK_MARKER_CRACK: drawn += esp_draw_crack_marker(s_markers[i]);          break;
      case EK_MARKER_BREAKABLE: drawn += esp_draw_breakable_marker(s_markers[i]);  break;
      case EK_MARKER_SWITCH_SPRITE:
        drawn += esp_draw_switch_sprite_marker(s_markers[i], gotAccess);           break;
      case EK_MARKER_SWITCH_WALL:
        drawn += esp_draw_switch_wall_marker(s_markers[i], gotAccess);             break;
      default: break;
    }
  }

  // After the markers so nothing paints over it. Gated on Switches, which runs the walks that answer it (WT gates on
  // secrets).
  esp_draw_secret_exit_note(g_cfg.espswitches != 0 && s_levelHasSecretExit);


  if (notDrawn)     *notDrawn     = markerOverflow + markerCapped;
  if (refused)      *refused      = markerOverflow;
  if (wallOverflow) *wallOverflow = s_wallOverflow;
  return drawn;
}

// The aim calibration marker (WT's, esp_layout.h). Gate first: aimdebug alone draws nothing, matching aim_frame's
// acquisition gate.
int aim_draw_marker(void) {
  if (!aim_marker_on(g_cfg.aimon, g_cfg.silenton, g_cfg.aimdebug)) return 0;

  AimMarkerPoints m;
  if (!aim_marker_points(&m)) return 0;

  EkProjection p;
  if (!project_begin(&p)) return 0;

  float rx = 0.0f, ry = 0.0f, lx = 0.0f, ly = 0.0f;
  const bool haveRaw = project_point(p, m.rawX, m.rawY, m.rawZ, &rx, &ry);
  const bool haveLed = project_point(p, m.ledX, m.ledY, m.ledZ, &lx, &ly);
  if (!haveRaw && !haveLed) return 0;

  const unsigned colour = canvas_aim_colour();

  if (haveRaw) {
    const int bx = aim_marker_x((int)rx), by = aim_marker_y((int)ry);
    const int span = aim_marker_span();
    canvas_rect(bx, by, span, AIM_MARKER_T, colour);
    canvas_rect(bx, aim_marker_edge_far((int)ry), span, AIM_MARKER_T, colour);
    canvas_rect(bx, by, AIM_MARKER_T, span, colour);
    canvas_rect(aim_marker_edge_far((int)rx), by, AIM_MARKER_T, span, colour);
  }

  if (haveLed) {
    canvas_rect(aim_marker_x((int)lx), aim_marker_y((int)ly),
                aim_marker_span(), aim_marker_span(), colour);
    char lab[32];
    wsprintfA(lab, "%s %d", AIM_MARKER_LABEL, m.sprite);
    canvas_text(esp_label_x((int)lx, 0, canvas_text_width(lab, FONT_SMALL)),
                aim_marker_label_y((int)ly), lab, colour, FONT_SMALL);
  }

  return 1;
}

// The free camera's "You" marker: a cyan diamond on the player's body, to find it once the view detached (WT
// camera.cpp:1187, Megaton esp.cpp:1210-1248). Here, not camera.cpp (the game thread's hook file); this runs in
// SwapBuffers.
int camera_draw_player_marker(void) {
  // The first term is the LIVE camera, not g_cfg.freecamon, or the marker sits on the body the player still looks out
  // of.
  const bool live = camera_active();
  const bool wantBody = freecam_marker_on(live, g_cfg.freecamshowplayer);
  const bool wantText = freecam_debug_on(live, g_cfg.freecamdebug);
  if (!wantBody && !wantText) return 0;
  int drew = 0;

  // The readout first: it needs neither player nor projection and must survive their refusals. camera_viewpoint_read,
  // not the camera state: the camera steps on the game thread, so the seqlock read is the only safe one (camera.h).
  if (wantText) {
    FreecamPose vp;
    if (camera_viewpoint_read(&vp)) {
      char line[FREECAM_POSE_BUFSIZE];
      freecam_pose_line(line, vp.x, vp.y, vp.z, vp.ang, vp.horiz);
      canvas_text(FREECAM_POSE_X, FREECAM_POSE_Y, line, canvas_player_colour(),
                  FONT_MONO);
      drew = 1;
    }
  }

  if (!wantBody) return drew;

  // ps->pos, not the player's sprite (WT camera.cpp:388-390): our camera substitutes ud.camera* and never writes
  // ps->pos. pos.z is the view height, so the diamond sits on the body, not the feet.
  PlayerSnap ps;
  if (!player_snap(&ps)) return drew;

  EkProjection p;
  if (!project_begin(&p)) return drew;

  float sx = 0.0f, sy = 0.0f;
  if (!project_point(p, ps.x, ps.y, ps.z, &sx, &sy)) return drew;

  const unsigned colour = canvas_player_colour();
  const int cx = (int)sx, cy = (int)sy;
  for (int r = 0; r < player_marker_rows(); r++)
    canvas_rect(player_marker_row_x(cx, r), player_marker_row_y(cy, r),
                player_marker_row_w(r), 1, colour);
  canvas_text(esp_label_x(cx, 0, canvas_text_width(PLAYER_MARKER_LABEL, FONT_SMALL)),
              player_marker_label_y(cy), PLAYER_MARKER_LABEL, colour, FONT_SMALL);
  return 1;
}

// The teleport's landing column (four side quads and a cap) from the plan the key acts on (camera.cpp). Quads through
// worldquad_push: Megaton draws a filled translucent column (camera.cpp:1074-1100).
static int s_tpQuads = 0, s_tpNotice = 0;
int esp_teleport_quads(void)  { return s_tpQuads; }
int esp_teleport_notice(void) { return s_tpNotice; }

static void esp_draw_teleport_mark(const EkTeleportPlan& plan) {
  // The column stands on the FLOOR, not the plan's z (the landing EYE): ekm_tp_plan returns florz - EK_TP_PHEIGHT and z
  // grows downward (Megaton camera.cpp:1066). Widened: foreign memory.
  const int floorz = clamp_ll_to_int((long long)plan.z + (long long)EK_TP_PHEIGHT);
  const EkMarkBox b = ekm_tp_mark_box(plan.x, plan.y, floorz);
  const unsigned colour = C_TELEPORT_MARK;

  for (int i = 0; i < 4; i++) {
    const int j = (i + 1) & 3;
    // Winding of ekm_secvol_quad_corners: A-top, A-bottom, B-bottom, B-top; swapping one adjacent pair bowties.
    EkWorldQuad q;
    q.x[0] = b.c[i].x; q.y[0] = b.c[i].y; q.z[0] = b.zTop;
    q.x[1] = b.c[i].x; q.y[1] = b.c[i].y; q.z[1] = b.zBottom;
    q.x[2] = b.c[j].x; q.y[2] = b.c[j].y; q.z[2] = b.zBottom;
    q.x[3] = b.c[j].x; q.y[3] = b.c[j].y; q.z[3] = b.zTop;
    q.argb = colour;
    worldquad_push(q);
    s_tpQuads++;
  }

  // The cap closes the column seen from above; ekm_tp_mark_box walks the square once, so consecutive corners are a
  // side.
  EkWorldQuad cap;
  for (int c = 0; c < 4; c++) { cap.x[c] = b.c[c].x; cap.y[c] = b.c[c].y; cap.z[c] = b.zTop; }
  cap.argb = colour;
  worldquad_push(cap);
  s_tpQuads++;
}

// The refusal line under the crosshair for the seconds ekm_tp_notice_visible allows. C_PLAYER, not C_TELEPORT_MARK:
// that colour's alpha 0x40 (ui/canvas.h:47) makes glyphs unreadable.
static void esp_draw_teleport_notice(const EkTeleportPlan& plan, int have,
                                     unsigned setMs, int screenW, int screenH) {
  if (!ekm_tp_has_notice(plan)) return;
  if (!ekm_tp_notice_visible(have, setMs, wallclock_ms())) return;

  const int w = canvas_text_width(plan.reason, FONT_SMALL);
  canvas_text((screenW - w) / 2, screenH / 2 + EK_TP_NOTICE_Y, plan.reason,
              canvas_player_colour(), FONT_SMALL);
  s_tpNotice = 1;
}

// `active` is wantEsp. Unconditional with the gate as a parameter, so both counters zero when nothing may draw (as
// dmgnum_draw, dmgnum.h:151): counters behind a gate have latched three times.
void esp_draw_teleport(int active, int screenW, int screenH) {
  // Zeroed on every path so the row never reads a stale frame.
  s_tpQuads = 0;
  s_tpNotice = 0;
  if (!active) return;

  // The live camera, not g_cfg.freecamon: the last flight's plan is still published.
  if (!camera_active()) return;

  // The column takes the LIVE plan and the notice the LATCH (camera.h); one for both printed a refusal nobody asked
  // for. A refused plan draws nothing.
  if (g_cfg.freecamteleportmarker) {
    EkTeleportPlan live = { 0, 0, 0, 0, -1, 0 };
    int haveLive = 0;
    const int gotLive = camera_teleport_plan_read(&live, &haveLive);
    if (gotLive && haveLive && live.ok) esp_draw_teleport_mark(live);
  }

  EkTeleportPlan said = { 0, 0, 0, 0, -1, 0 };
  unsigned setMs = 0;
  int have = 0;
  const int gotSaid = camera_teleport_outcome(&said, &setMs, &have);
  if (gotSaid) esp_draw_teleport_notice(said, have, setMs, screenW, screenH);
}
