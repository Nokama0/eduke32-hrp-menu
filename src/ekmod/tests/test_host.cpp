
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <limits>

#include "../projmath.h"
#include "../udstate.h"
#include "../gamestate.h"
#include "../tiles.h"
#include "../badguy.h"
#include "../esp_layout.h"
#include "../playerstate.h"
#include "../ui/ui.h"
#include "../ui/panel_layout.h"
#include "../ui/menu.h"
#include "../ui/entry.h"
#include "../ui/fontpack.h"
#include "../targets.h"
#include "../aimsolve.h"
#include "../hook.h"
#include "../mousebind.h"
#include "../tbot.h"
#include "../esp_names.h"
#include "../esp_items.h"
#include "../weaponspeed.h"
#include "../actorstate.h"
#include "../hitscan.h"
#include "../shotray.h"
#include "../wallstate.h"
#include "../impact.h"
#include "../tracer.h"
#include "../cvars.h"
#include "../autoswitch.h"
#include "../autosave.h"
#include "../freecam.h"
#include "../campos.h"
#include "../sectorstate.h"
#include "../tsprites.h"
#include "../frustum.h"
#include "../shot.h"
#include "../mapextent.h"
#include "../esp_breakables.h"
#include "../esp_switches.h"
#include "../worldquad.h"
#include "../maskscan.h"
#include "../esp_tint.h"
#include "../modelbounds.h"
#include "../secretvol.h"
#include "../secrettrig.h"
#include "../secretpath.h"
#include "../dmgnum.h"
#include "../fov.h"
#define EK_PERF_PURE_ONLY 1
#include "../perf.h"
#define EK_GAMECFG_PURE_ONLY 1
#include "../gamecfg.h"
#include "../hostcheck.h"
#include "../config.h"

static int passed = 0, failed = 0;

#define CHECK_MSG(cond, what)                                                 \
  do {                                                                        \
    if (cond) { passed++; }                                                   \
    else { failed++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, what); }  \
  } while (0)
#define CHECK(cond) CHECK_MSG(cond, #cond)

static bool approx(float a, float b, float tol) { return fabsf(a - b) <= tol; }

// sectnum == MAXSECTORS is what the engine STORES in a free sprite (engine.cpp:9380, :8838).
static bool record_plausible_ref(int sectnum, int statnum, int picnum) {
  if (sectnum < -1 || sectnum > EK_MAXSECTORS) return false;
  if (statnum < 0 || statnum > EK_MAXSTATUS) return false;
  if (picnum < 0 || picnum >= EK_MAXTILES) return false;
  return true;
}

static void tb_0001_free_sprite_is_plausible() {
  CHECK_MSG(record_plausible_ref(EK_MAXSECTORS, EK_MAXSTATUS, 0),
        "a FREE sprite -- sectnum and statnum both at the limit -- is plausible");
  CHECK_MSG(record_plausible_ref(-1, 0, 1680), "sectnum -1 is legal");
  CHECK_MSG(record_plausible_ref(306, 1, 1680), "an ordinary live sprite is plausible");
  CHECK_MSG(!record_plausible_ref(EK_MAXSECTORS + 1, 0, 0), "past MAXSECTORS is not");
  CHECK_MSG(!record_plausible_ref(0, EK_MAXSTATUS + 1, 0), "past MAXSTATUS is not");
  CHECK_MSG(!record_plausible_ref(0, 0, EK_MAXTILES), "picnum is exclusive at MAXTILES");
  CHECK_MSG(!record_plausible_ref(-2, 0, 0), "sectnum below -1 is not");

  // Zeros pass every range test, so range tests alone can never identify the array.
  CHECK_MSG(record_plausible_ref(0, 0, 0),
        "a ZEROED record passes every range test -- ranges alone cannot identify");
}

static void tb_0002_box_units() {
  const int tile = 64, xrep = 32, yrep = 32;
  CHECK_MSG(approx(ekm_sprite_halfwidth(xrep, tile), 204.8f, 0.01f),
        "half-width = 0.1 * xrepeat * tilesiz.x");
  CHECK_MSG(approx(ekm_sprite_height_z(yrep, tile), 8192.0f, 0.01f),
        "height in z units = 4 * yrepeat * tilesiz.y");
  CHECK_MSG(approx(ekm_sprite_height_flat(yrep, tile), 512.0f, 0.01f),
        "the SAME height across is z/16 -- the two must never be compared raw");

  const float r = ekm_sprite_halfwidth(xrep, tile) / ekm_sprite_height_flat(yrep, tile);
  CHECK_MSG(approx(r, 0.4f, 0.001f), "half-width is 0.4x the height for a square tile");
  CHECK_MSG(r > 0.1f, "and emphatically not the 0.025 the z-unit mix produced");

  const float projH = 100.0f;
  CHECK_MSG(approx(ekm_projected_halfwidth(projH, xrep, tile, yrep, tile), 40.0f, 0.01f),
        "a 100px box is 80px wide, not 5px");

  // A tall thin tile must stay tall and thin.
  CHECK_MSG(ekm_projected_halfwidth(projH, 32, 32, 32, 128) <
        ekm_projected_halfwidth(projH, 32, 128, 32, 32),
        "a narrow tile gives a narrower box than a wide one");
}

// glRotatef(a,0,1,0) is x' = cos*x + sin*z, z' = -sin*x + cos*z; flipping both signs made boxes slide sideways when
// turning.
static void tb_0003_yaw_direction() {
  EkmVec3 ahead = {0.0f, 0.0f, -1.0f};        // one unit in front of the camera
  EkmVec3 r90 = ekm_rotate_yaw(ahead, 90.0f);
  CHECK_MSG(approx(r90.x, -1.0f, 0.001f) && approx(r90.z, 0.0f, 0.001f),
        "yawing 90 degrees sends the forward axis to -x, not +x");

  EkmVec3 r = ekm_rotate_yaw(ahead, -90.0f);
  CHECK_MSG(approx(r.x, 1.0f, 0.001f), "and -90 sends it the other way");
  CHECK_MSG(approx(ekm_rotate_yaw(ahead, 0.0f).z, -1.0f, 0.001f), "zero rotates nothing");

  // Rotation preserves length either way, so this is NOT the check that catches the transpose.
  EkmVec3 v = {3.0f, 0.0f, 4.0f};
  EkmVec3 o = ekm_rotate_yaw(v, 37.0f);
  CHECK_MSG(approx(sqrtf(o.x * o.x + o.z * o.z), 5.0f, 0.001f),
        "length is preserved -- true of the transpose too, hence the direction checks");
}

static void tb_0004_pitch_and_horiz() {
  CHECK_MSG(approx(ekm_horiz_to_pitch(100.0f), 0.0f, 0.0001f),
        "horiz 100 is dead level -- it is a SLOPE, not a pixel row");
  CHECK_MSG(ekm_horiz_to_pitch(200.0f) < 0.0f, "looking up gives a negative pitch");
  CHECK_MSG(ekm_horiz_to_pitch(0.0f) > 0.0f, "looking down gives a positive one");
  CHECK_MSG(approx(ekm_horiz_to_pitch(228.0f), -(float)M_PI / 4.0f, 0.001f),
        "horiz 228 is 45 degrees up, from tan(pitch) = (horiz-100)/128");

  EkmVec3 ahead = {0.0f, 0.0f, -1.0f};
  EkmVec3 up = ekm_rotate_pitch(ahead, ekm_horiz_to_pitch(200.0f));
  CHECK_MSG(up.y < 0.0f, "pitching up moves a forward point down the eye-space y axis");
}

static void tb_0005_angle_decode() {
  CHECK_MSG(ekm_fix16_to_buildang(0) == 0, "zero is zero");
  CHECK_MSG(ekm_fix16_to_buildang(512 << 16) == 512, "a whole angle survives the shift");
  CHECK_MSG(ekm_fix16_to_buildang(2048 << 16) == 0, "a full turn wraps to zero");
  CHECK_MSG(ekm_fix16_to_buildang(2050 << 16) == 2,
        "and past a full turn wraps -- the engine does let it run over");
  CHECK_MSG(ekm_fix16_to_buildang(-65536) == 2047,
        "negative wraps too, which a bare >>16 would not");
  CHECK_MSG(approx(ekm_buildang_to_degrees(512 << 16), 90.0f, 0.001f),
        "512 of 2048 is 90 degrees");
}

// Build -> Polymer axes: (y, -z/16, -x); a wrong sign mirrors the world.
static void tb_0006_axis_mapping() {
  EkmVec3 v = ekm_to_eye_space(1000, 2000, 3200, 0, 0, 0);
  CHECK_MSG(approx(v.x, 2.0f, 0.001f), "polymer x is build y");
  CHECK_MSG(approx(v.z, -1.0f, 0.001f), "polymer z is MINUS build x");
  CHECK_MSG(approx(v.y, -0.2f, 0.001f), "polymer y is minus build z over sixteen");

  EkmVec3 same = ekm_to_eye_space(500, 500, 500, 500, 500, 500);
  CHECK_MSG(approx(same.x, 0.0f, 0.0001f) && approx(same.y, 0.0f, 0.0001f) &&
        approx(same.z, 0.0f, 0.0001f), "a point at the camera is the origin");
}

static void tb_0007_viewingrange_is_derived() {
  // The engine's own global reads a constant 65536 after the frame, so this must come from fov.
  CHECK_MSG(approx(ekm_viewingrange(90, 36) * (64.0f / 64.0f), 65536.0f, 1.0f),
        "fov 90 at spawn yrepeat gives exactly 65536");
  CHECK_MSG(ekm_viewingrange(120, 36) > ekm_viewingrange(90, 36),
        "a wider fov gives a larger viewingrange");
  CHECK_MSG(ekm_viewingrange(60, 36) < ekm_viewingrange(90, 36), "and a narrower one smaller");
  CHECK_MSG(ekm_viewingrange(90, 8) > ekm_viewingrange(90, 36),
        "a shrunk player sees wider -- the yrepeat term is not decorative");
}

static void tb_0008_projection_and_culling() {
  EkmVec3 behind = {0.0f, 0.0f, 1.0f};
  CHECK_MSG(!ekm_in_front(behind), "a point behind the camera is culled");
  EkmVec3 onPlane = {0.0f, 0.0f, 0.0f};
  CHECK_MSG(!ekm_in_front(onPlane), "and so is one exactly at the eye");
  EkmVec3 ahead = {0.0f, 0.0f, -5.0f};
  CHECK_MSG(ekm_in_front(ahead), "a point in front is kept");

  const float cot = ekm_cot(65536.0f, 3440, 1440, 3440, 1440);
  CHECK_MSG(cot > 0.0f, "a sane viewport gives a usable cotangent");
  CHECK_MSG(ekm_cot(65536.0f, 0, 1440, 3440, 1440) == 0.0f,
        "a zero-width viewport gives zero rather than a division by zero");

  // Dead centre must land dead centre, whatever the viewport.
  const float aspect = 3440.0f / 1440.0f;
  const float nx = ekm_ndc_x(ahead, cot, aspect);
  CHECK_MSG(approx(nx, 0.0f, 0.0001f), "a point straight ahead is at ndc x zero");
  CHECK_MSG(approx(ekm_screen_x(nx, 0, 3440), 1720.0f, 0.01f), "which is the middle of the screen");
  CHECK_MSG(approx(ekm_screen_y(0.0f, 0, 1440), 720.0f, 0.01f), "and ndc y zero is the middle");

  // The viewport offset must actually be applied.
  CHECK_MSG(approx(ekm_screen_x(0.0f, 100, 3440), 1820.0f, 0.01f),
        "a viewport that does not start at zero shifts the result");

  CHECK_MSG(approx(ekm_screen_x(1.0f, 0, 3440), 3440.0f, 0.01f), "ndc +1 is the right edge");
  CHECK_MSG(approx(ekm_screen_x(-1.0f, 0, 3440), 0.0f, 0.01f), "ndc -1 is the left");
  CHECK_MSG(approx(ekm_screen_y(1.0f, 0, 1440), 0.0f, 0.01f), "ndc y +1 is the TOP, not the bottom");
}

static void tb_0009_centred_sprites() {
  const float h = ekm_sprite_height_z(32, 64);        // 8192
  CHECK_MSG(ekm_feet_z(1000, h, 0) == 1000, "an ordinary sprite stands on its own z");
  CHECK_MSG(ekm_feet_z(1000, h, 128) == 1000 + 4096,
        "cstat bit 7 centres it, so the feet are half a height lower");
  CHECK_MSG(ekm_feet_z(1000, h, 64) == 1000, "other cstat bits do not move it");
}

// Struct layout constants: a wrong one reads plausible numbers from the wrong field.
static void tb_0010_layout_constants() {
  CHECK_MSG(EK_SPR_STRIDE == 44, "spritetype is 44 bytes across all three Duke engines");
  CHECK_MSG(EK_SPR_STATNUM == 0x1A && EK_SPR_SECTNUM == 0x18, "statnum follows sectnum");
  CHECK_MSG(EK_SPR_EXTRA == 0x2A, "extra is the last field, and is the actor's health");
  CHECK_MSG(EK_SPR_CSTAT == 0x0C && EK_SPR_PICNUM == 0x0E, "cstat then picnum");

  CHECK_MSG(EK_TILEDATA_STRIDE == 40, "tiledata_t is 40 bytes on x64, not the x86 24");
  CHECK_MSG(EK_TILEDATA_FLAGS == 32, "flags sit after four 8-byte pointers");
  CHECK_MSG(EK_SFLAG_ANY_BADGUY == (0x20u | 0x40000u),
        "A_CheckEnemyTile tests SFLAG_BADGUY and SFLAG_HARDCODED_BADGUY");

  CHECK_MSG(EK_UD_CONFIG_AUTOAIM - EK_UD_FOV == 40,
        "the gap between the two cross-check cvars is what detects a layout change");
  CHECK_MSG(EK_UD_CAMERAQ16ANG == 432 && EK_UD_CAMERAQ16HORIZ == 436,
        "the camera angles are adjacent fix16 fields");

  CHECK_MSG(EK_MAXSPRITES == 16384 && EK_MAXSECTORS == 4096, "the V8 limits, not V7's");
  CHECK_MSG(EK_MAXTILES_ARR == EK_MAXTILES, "one tile count, not two that can drift apart");
  CHECK_MSG(EKM_Z_PER_XY == 16.0f, "the scale that made every box sixteen times too narrow");
}

static bool tiles_hits_ok(int hits, int npics) { return hits * 100 >= npics * 80; }

static void tb_0011_validator_thresholds() {
  CHECK_MSG(tiles_hits_ok(113, 113), "every picnum having a size passes");
  CHECK_MSG(tiles_hits_ok(80, 81), "and 80 of 81 does -- which really happened, and the");
  CHECK_MSG(tiles_hits_ok(8, 10), "strict version would have rejected the real array");
  CHECK_MSG(!tiles_hits_ok(0, 113), "a zeroed region scores zero and is refused");
  CHECK_MSG(!tiles_hits_ok(7, 10), "below four in five is refused");
}

// Carried UI layer: a tripwire against "adapting" a rule from the sibling ports without knowing what it was for.
static void tb_0012_panel_layout() {
  // A panel bigger than the screen is clamped; one with no stored box gets a default, not a zero-sized hole.
  PanelBox unset = {-1, -1, 0, 0};
  const PanelBox d = panel_layout(unset, 3440, 1440);
  CHECK_MSG(d.w >= PANEL_MIN_W && d.h >= PANEL_MIN_H,
            "an unset panel comes back at least the minimum size");
  CHECK_MSG(d.x >= 0 && d.y >= 0, "and on screen");

  PanelBox huge = {0, 0, 99999, 99999};
  const PanelBox c = panel_layout(huge, 1280, 720);
  CHECK_MSG(c.w <= 1280 && c.h <= 720, "a panel larger than the screen is clamped to it");

  PanelBox b = {100, 200, 600, 800};
  const UiRect g = panel_grip_rect(b);
  CHECK_MSG(g.x + g.w == b.x + b.w && g.y + g.h == b.y + b.h,
            "the grip is flush with the bottom-right corner");
  CHECK_MSG(ui_hit(g, b.x + b.w - 1, b.y + b.h - 1), "the corner pixel is in the grip");
  CHECK_MSG(!ui_hit(g, b.x, b.y), "the opposite corner is not");
}

static void tb_0013_ui_scale() {
  CHECK_MSG(ui_scale_clamped(50) >= UI_SCALE_MIN, "a tiny scale is raised to the floor");
  CHECK_MSG(ui_scale_clamped(9999) <= UI_SCALE_MAX, "and a huge one lowered to the ceiling");
  CHECK_MSG(ui_scale_px(100, 100) == 100, "100 percent is identity");
  CHECK_MSG(ui_scale_px(100, 200) == 200, "200 percent doubles");

  // A row measured logically and drawn scaled must land where the hit test says.
  for (int pct = UI_SCALE_MIN; pct <= UI_SCALE_MAX; pct += 5) {
    const int back = ui_unscale_px(ui_scale_px(200, pct), pct);
    CHECK_MSG(back >= 198 && back <= 202,
              "scale then unscale returns the logical size, within rounding");
  }
}

static void tb_0014_esp_markers() {
  // World Tour caps the arm at (dim-1)/2 so four corners never close into an outline that merges markers in a crowd.
  for (int dim = 2; dim <= 400; dim += 2) {
    const int arm = esp_bracket_arm(dim);
    CHECK_MSG(arm * 2 < dim, "two opposing arms never meet on an even edge");
  }
  CHECK_MSG(esp_bracket_arm(0) == 0, "a zero edge has no arm");
  CHECK_MSG(esp_bracket_arm(-5) == 0, "and neither does a negative one");
  CHECK_MSG(esp_bracket_thickness(0, 10) == 0, "thickness never exceeds the shorter arm");

  CHECK_MSG(esp_bar_x(100) < 100, "the health bar is left of the box");
  CHECK_MSG(esp_bar_x(100) + ESP_BAR_W <= 100, "and does not overlap it");

  CHECK_MSG(esp_label_x(100, 80, 40) == 120, "a label is centred on the box");
  CHECK_MSG(esp_name_label_y(200) < 200, "the name sits above the box");
  CHECK_MSG(esp_dist_label_y(200, 80) > 280, "and the distance below it");
}


// playerdata_t is packed to 133 bytes, so g_player[0].ps sits at an odd offset and a stride-8 walk can never land on
// it.
static void tb_0015_player_struct_rules() {
  CHECK(EK_PD_SIZE % 8 != 0);
  CHECK((0 + EK_PD_SIZE) % 8 == 5);

  // game.cpp:6803 allocates ALL MAXPLAYERS even in single player: one null then sixteen distinct non-null blocks.
  char blocks[EK_MAXPLAYERS];
  const void* run[EK_MAXPLAYERS];
  for (int i = 0; i < EK_MAXPLAYERS; i++) run[i] = &blocks[i];
  CHECK(ekm_slot_run_ok(NULL, run, EK_MAXPLAYERS));

  const void* const saved = run[7];
  run[7] = NULL;
  CHECK(!ekm_slot_run_ok(NULL, run, EK_MAXPLAYERS));   // a hole in the array
  run[7] = run[2];
  CHECK(!ekm_slot_run_ok(NULL, run, EK_MAXPLAYERS));   // two players aliased
  run[7] = saved;

  CHECK(!ekm_slot_run_ok(&blocks[0], run, EK_MAXPLAYERS));  // zeroth allocated
  CHECK(!ekm_slot_run_ok(NULL, NULL, EK_MAXPLAYERS));       // no array
  CHECK(!ekm_slot_run_ok(NULL, run, EK_MAXPLAYERS - 1));    // wrong length

  CHECK(ekm_q16ang_degrees(0) == 0);
  CHECK(ekm_q16ang_degrees(512 << 16) == 90);
  CHECK(ekm_q16ang_degrees(1024 << 16) == 180);
  CHECK(ekm_q16ang_degrees(1536 << 16) == 270);
  CHECK(ekm_q16ang_degrees(2048 << 16) == 0);      // a full turn is 0, not 360
  CHECK(ekm_q16ang_degrees(2047 << 16) == 359);
  // The fractional part is view smoothing and must not move the whole degree.
  CHECK(ekm_q16ang_degrees((512 << 16) | 0xFFFF) == 90);
}

// Hittable: cstat bit 8 set and bit 15 clear; xrepeat 0 is not drawn, so not a target.
static void tb_0016_target_hittable() {
  CHECK(ekm_target_hittable(32, 0x0101));            // the ordinary enemy
  CHECK(!ekm_target_hittable(0, 0x0101));            // not drawn
  CHECK(!ekm_target_hittable(32, 0x0001));           // not hitscan-sensitive
  CHECK(ekm_target_hittable(32, 0x0100));            // the dormant Recon
  CHECK(!ekm_target_hittable(0, 0x0100));            // the Recon, not drawn
  CHECK(!ekm_target_hittable(32, 0x8101));           // invisible
  CHECK(!ekm_target_hittable(32, 0x8100));           // invisible, bit 0 clear
  // Bits outside the mask must not matter: real sprites carry many.
  CHECK(ekm_target_hittable(32, 0x0101 | 0x0002 | 0x0020 | 0x4000));
}

static void tb_0017_aim_solve() {
  CHECK(ekm_aim_yaw(100, 0) == 0);
  CHECK(ekm_aim_yaw(0, 100) == 512);
  CHECK(ekm_aim_yaw(-100, 0) == 1024);
  CHECK(ekm_aim_yaw(0, -100) == 1536);
  CHECK(ekm_aim_yaw(100, 100) == 256);
  CHECK(ekm_aim_yaw(0, 0) == 0);            // degenerate, must not be junk
  // EXACT VALUES, NOT A RANGE: ekm_aim_yaw ends in `v &= 2047`, so a range check stayed green when atan2(dy,dx) was
  // transposed.
  {
    const int ds[5] = { -1000, -500, 0, 500, 1000 };
    const int expect[5][5] = {
      { 1280, 1175, 1024,  873,  768 },   // dx = -1000
      { 1385, 1280, 1024,  768,  663 },   // dx =  -500
      { 1536, 1536,    0,  512,  512 },   // dx =     0
      { 1687, 1792,    0,  256,  361 },   // dx =   500
      { 1792, 1897,    0,  151,  256 },   // dx =  1000
    };
    for (int i = 0; i < 5; i++)
      for (int j = 0; j < 5; j++)
        CHECK(ekm_aim_yaw(ds[i], ds[j]) == expect[i][j]);
  }

  CHECK(ekm_yaw_delta(0, 100) == 100);
  CHECK(ekm_yaw_delta(100, 0) == -100);
  CHECK(ekm_yaw_delta(2040, 8) == 16);      // forward over the seam
  CHECK(ekm_yaw_delta(8, 2040) == -16);     // backward over the seam
  CHECK(ekm_yaw_delta(0, 1024) == 1024);    // exactly opposite
  CHECK(ekm_yaw_delta(512, 512) == 0);

  // Offset is the unsigned version and never exceeds half a turn.
  CHECK(ekm_angular_offset(2040, 8) == 16);
  CHECK(ekm_angular_offset(8, 2040) == 16);
  CHECK(ekm_angular_offset(0, 1024) == 1024);

  CHECK(ekm_q16horiz_for(0, 5000) == ekm_f16(EKM_HORIZ_LEVEL));
  // Build z grows downward, so a target ABOVE the eye has a negative dz and
  // must raise the view above level.
  CHECK(ekm_q16horiz_for(-8000, 5000) > ekm_f16(EKM_HORIZ_LEVEL));
  CHECK(ekm_q16horiz_for(8000, 5000) < ekm_f16(EKM_HORIZ_LEVEL));
  // Direction checks are scale-invariant; pin the exact q16 value to catch a wrong EKM_Z_PER_XY.
  CHECK(ekm_q16horiz_for(-8000, 5000) == 7392460);
  // A zero distance must not divide by zero.
  CHECK(ekm_q16horiz_for(-8000, 0) == ekm_f16(EKM_HORIZ_LEVEL));

  // The engine clamps horiz to -99..299 (player.cpp:3179); an unreachable solution is dropped, never clamped.
  CHECK(ekm_horiz_reachable(ekm_f16(EKM_HORIZ_LEVEL)));
  CHECK(ekm_horiz_reachable(ekm_f16(EKM_HORIZ_MIN)));     // the boundary is in
  CHECK(ekm_horiz_reachable(ekm_f16(EKM_HORIZ_MAX)));
  CHECK(!ekm_horiz_reachable(ekm_f16(EKM_HORIZ_MIN) - 1));
  CHECK(!ekm_horiz_reachable(ekm_f16(EKM_HORIZ_MAX) + 1));

  // Smoothing. 0 snaps the whole way, 100 never moves, and nothing in between
  // may stall short of the target.
  CHECK(ekm_smooth_step(1000, 0) == 1000);
  CHECK(ekm_smooth_step(1000, 100) == 0);
  CHECK(ekm_smooth_step(1000, 50) == 500);
  CHECK(ekm_smooth_step(0, 50) == 0);            // already there
  CHECK(ekm_smooth_step(1, 99) == 1);            // forced step, never stalls
  CHECK(ekm_smooth_step(-1, 99) == -1);
  CHECK(ekm_smooth_step(-1000, 50) == -500);     // symmetric

  // Exact drops per rung in both modes: a `z <= feet && z >= feet - h` pin held for any rung fraction in 0..1.
  const int feet = 10000;  const float h = 4096.0f;
  CHECK(ekm_aim_z(feet, h, 0, 0) == feet - 2048);
  CHECK(ekm_aim_z(feet, h, 1, 0) == feet - 3584);
  CHECK(ekm_aim_z(feet, h, 2, 0) == feet - 3072);
  CHECK(ekm_aim_z(feet, h, 3, 0) == feet - 1024);
  CHECK(ekm_aim_z(feet, h, 0, 1) == feet - 3072);
  CHECK(ekm_aim_z(feet, h, 1, 1) == feet - 3584);
  CHECK(ekm_aim_z(feet, h, 2, 1) == feet - 2048);
  CHECK(ekm_aim_z(feet, h, 3, 1) == feet - 1024);
  // Four rungs, four distinct heights: a fixed h*0.5 rung 2 duplicated rung 0 on Centre (the default).
  for (int pt = 0; pt < 2; pt++)
    for (int a = 0; a < 4; a++)
      for (int b = a + 1; b < 4; b++)
        CHECK(ekm_aim_z(feet, h, a, pt) != ekm_aim_z(feet, h, b, pt));
  // Still inside the body, which the exact pins alone would not say.
  for (int rung = 0; rung < 4; rung++)
    for (int pt = 0; pt < 2; pt++) {
      const int z = ekm_aim_z(feet, h, rung, pt);
      CHECK(z <= feet && z >= feet - (int)h);
    }

  CHECK(ekm_lead_scaled(100, 200, 0) == 100);
  CHECK(ekm_lead_scaled(100, 200, 100) == 200);
  CHECK(ekm_lead_scaled(100, 200, 50) == 150);

  CHECK(ekm_metres(512) == 1);
  CHECK(ekm_metres(0) == 0);
}

static void tb_0018_aim_lead() {
  int ox = 0, oy = 0, oz = 0;

  ekm_lead_point(5000, 0, 100, 0, 0, 0, 0, 0, 800, &ox, &oy, &oz);
  CHECK(ox == 5000 && oy == 0 && oz == 100);

  ekm_lead_point(5000, 0, 0, 100, 0, 0, 0, 0, 800, &ox, &oy, &oz);
  CHECK(ox > 5000);
  ekm_lead_point(5000, 0, 0, -100, 0, 0, 0, 0, 800, &ox, &oy, &oz);
  CHECK(ox < 5000);

  // Crossing motion is led along its own axis only: 629 is the measured two-iteration answer (100 a tic, 5038.9 flight
  // at 800 a tic).
  ekm_lead_point(5000, 0, 0, 0, 100, 0, 0, 0, 800, &ox, &oy, &oz);
  CHECK(ox == 5000);
  CHECK(oy == 629);
  CHECK(oz == 0);

  // A zero or negative projectile speed must not divide by zero.
  ekm_lead_point(5000, 0, 0, 100, 0, 0, 0, 0, 0, &ox, &oy, &oz);
  CHECK(ox == 5000 && oy == 0 && oz == 0);

  // A target on top of the shooter is degenerate and must come back unchanged.
  ekm_lead_point(0, 0, 0, 100, 100, 0, 0, 0, 800, &ox, &oy, &oz);
  CHECK(ox == 0 && oy == 0 && oz == 0);
}

// Frames are not tics; every check here exists because a first version treated them as one.
static void tb_0019_per_tic_rules() {
  CHECK(ekm_lead_tics(0) == 1);
  CHECK(ekm_lead_tics(7) == 1);     // 144 fps
  CHECK(ekm_lead_tics(17) == 1);    // 60 fps
  CHECK(ekm_lead_tics(33) == 1);    // exactly a tic
  // Below 30 fps one delta spans several tics and must be divided by them.
  CHECK(ekm_lead_tics(67) == 2);
  CHECK(ekm_lead_tics(100) == 3);
  CHECK(ekm_lead_tics(133) == 4);
  // A long gap is a pause or a load, not a speed: capped, never unbounded.
  CHECK(ekm_lead_tics(10000) == EKM_LEAD_TICS_MAX);
  for (unsigned ms = 0; ms <= 300; ms += 6) {
    const int t = ekm_lead_tics(ms);
    CHECK(t >= 1 && t <= EKM_LEAD_TICS_MAX);
  }

  // A gap too long to represent is not a velocity: dividing a seconds-long vanish by the cap reported ~500 units a tic
  // for a true twenty.
  CHECK(ekm_lead_divisor(0) == 1);
  CHECK(ekm_lead_divisor(33) == 1);
  CHECK(ekm_lead_divisor(67) == 2);
  CHECK(ekm_lead_divisor(100) == 3);
  CHECK(ekm_lead_divisor(EKM_LEAD_GAP_MAX_MS) == 4);       // the last honest gap
  CHECK(ekm_lead_divisor(EKM_LEAD_GAP_MAX_MS + 1) == 0);   // one ms past it
  CHECK(ekm_lead_divisor(3000) == 0);                      // a case that once misfired
  // Never a divisor the caller did not ask for: either a real tic count in
  // 1..EKM_LEAD_TICS_MAX, or the discard.
  for (unsigned ms = 0; ms <= 300; ms += 6) {
    const int d = ekm_lead_divisor(ms);
    CHECK(d == 0 || (d >= 1 && d <= EKM_LEAD_TICS_MAX));
    CHECK((d == 0) == (ms > EKM_LEAD_GAP_MAX_MS));
  }

  CHECK(!ekm_velocity_stale(0));
  CHECK(!ekm_velocity_stale(EKM_VEL_STALE_MS - 1));
  CHECK(ekm_velocity_stale(EKM_VEL_STALE_MS));
  CHECK(ekm_velocity_stale(10000));

  // One tic reproduces the per-frame formula exactly, so the old behaviour is
  // what a 30 fps client still gets.
  for (int p = 0; p <= 100; p += 5) {
    CHECK(ekm_smooth_step_tics(1000, p, 1.0) == ekm_smooth_step(1000, p));
    CHECK(ekm_smooth_step_tics(-7654, p, 1.0) == ekm_smooth_step(-7654, p));
  }

  // 0 snaps and 100 never moves, at EVERY elapsed time -- the two settings a
  // player reads as "off" and "frozen" must not depend on their frame rate.
  const double someTics[] = { 0.125, 0.5, 1.0, 2.0, 4.0 };
  for (int i = 0; i < 5; i++) {
    CHECK(ekm_smooth_step_tics(1000, 0, someTics[i]) == 1000);
    CHECK(ekm_smooth_step_tics(1000, 100, someTics[i]) == 0);
  }

  int prev = -1;
  for (int i = 1; i <= 16; i++) {
    const int step = ekm_smooth_step_tics(1000000, 50, i * 0.25);
    CHECK(step > prev);
    prev = step;
  }

  CHECK(ekm_smooth_step_tics(1000, 50, 2.0) == 750);
  CHECK(ekm_smooth_step_tics(1000, 50, 3.0) == 875);

  // A step never overshoots, and never stalls on a delta it has not closed.
  CHECK(ekm_smooth_step_tics(1, 99, 0.125) == 1);
  CHECK(ekm_smooth_step_tics(-1, 99, 0.125) == -1);
  CHECK(ekm_smooth_step_tics(0, 50, 1.0) == 0);
}

// The decoder accepts only bytes GCC emits in a prologue and returns 0 otherwise: a jump landing mid-instruction
// corrupts the game.
static void tb_0020_hook_rules() {
  const unsigned char pre[] = { 0x41,0x56, 0x41,0x55, 0x41,0x54, 0x55, 0x57 };
  CHECK(ekm_prologue_len(pre, sizeof(pre)) == 6);

  const unsigned char shoot[] = { 0x41,0x57, 0x41,0x56, 0x41,0x55, 0x41,0x54, 0x55 };
  CHECK(ekm_prologue_len(shoot, sizeof(shoot)) == 6);

  const unsigned char five[] = { 0x55, 0x57, 0x56, 0x53, 0x51, 0x52 };
  CHECK(ekm_prologue_len(five, sizeof(five)) == 5);

  const unsigned char pushsub[] = { 0x55, 0x48,0x83,0xEC,0x70, 0x90 };
  CHECK(ekm_prologue_len(pushsub, sizeof(pushsub)) == 5);

  const unsigned char sub32[] = { 0x48,0x81,0xEC,0x28,0x01,0x00,0x00, 0x90 };
  CHECK(ekm_prologue_len(sub32, sizeof(sub32)) == 7);

  // REFUSALS. Each of these is a byte the decoder must not guess at.
  const unsigned char relcall[] = { 0xE8,0x00,0x00,0x00,0x00, 0x90 };
  CHECK(ekm_prologue_len(relcall, sizeof(relcall)) == 0);   // a call
  const unsigned char jmpshort[] = { 0xEB,0x10, 0x55, 0x57, 0x56, 0x53 };
  CHECK(ekm_prologue_len(jmpshort, sizeof(jmpshort)) == 0); // a branch
  const unsigned char riprel[] = { 0x48,0x8B,0x35,0xDE,0x9A,0x4E,0x00 };
  CHECK(ekm_prologue_len(riprel, sizeof(riprel)) == 0);     // rip-relative
  const unsigned char junk[] = { 0x0F,0x0B, 0x55, 0x57, 0x56, 0x53 };
  CHECK(ekm_prologue_len(junk, sizeof(junk)) == 0);         // ud2, unknown
  CHECK(ekm_prologue_len(pre, 3) == 0);                     // not enough bytes

  int d = 0;
  CHECK(ekm_rel32(0x1000, 0x1005, &d) && d == 0);
  CHECK(ekm_rel32(0x1000, 0x2000, &d) && d == 0x0FFB);
  CHECK(ekm_rel32(0x2000, 0x1000, &d) && d == -0x1005);
  CHECK(ekm_rel32(0, 0x7FFFFFFFull + 5, &d) && d == 0x7FFFFFFF);
  CHECK(!ekm_rel32(0, 0x80000000ull + 5, &d));
  CHECK(ekm_rel32(0x80000000ull, 5, &d) && d == -0x80000000LL);
  CHECK(!ekm_rel32(0x80000001ull, 5, &d));
}

// A bad trampoline displacement is a valid jump to the wrong place and no run-time check catches it.
static void tb_0021_hook_bytes() {
  unsigned char buf[64];
  // 24 wide, not 16: the len = 17 refusal would read past a smaller fixture if its guard were mutated away.
  const unsigned char saved[24] = { 0x41,0x56, 0x41,0x55, 0x41,0x54, 0x55, 0x57,
                                    0x56, 0x53, 0x48,0x83,0xEC,0x70, 0x11, 0x22,
                                    0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA };

  memset(buf, 0xCC, sizeof(buf));
  CHECK(ekm_build_absjmp(buf, (int)sizeof(buf), 0x1122334455667788ull) == 14);
  CHECK(buf[0] == 0xFF && buf[1] == 0x25);
  CHECK(buf[2] == 0 && buf[3] == 0 && buf[4] == 0 && buf[5] == 0);
  CHECK(buf[6] == 0x88 && buf[7] == 0x77 && buf[8] == 0x66 && buf[9] == 0x55);
  CHECK(buf[10] == 0x44 && buf[11] == 0x33 && buf[12] == 0x22 && buf[13] == 0x11);
  CHECK(buf[14] == 0xCC);                             // nothing written past the end
  CHECK(ekm_build_absjmp(buf, 13, 0x1000ull) == 0);   // one byte short: refuse

  const int lens[3] = { 5, 6, 11 };
  for (int k = 0; k < 3; k++) {
    const int len = lens[k];
    memset(buf, 0xCC, sizeof(buf));
    CHECK(ekm_build_trampoline(buf, (int)sizeof(buf), saved, len,
                               0xDEADBEEF12345678ull) == len + 14);
    int verbatim = 1;
    for (int i = 0; i < len; i++) if (buf[i] != saved[i]) verbatim = 0;
    CHECK(verbatim == 1);                             // the clobbered bytes, unaltered
    CHECK(buf[len] == 0xFF && buf[len + 1] == 0x25);  // the jump starts AT len
    CHECK(buf[len + 2] == 0 && buf[len + 5] == 0);
    CHECK(buf[len + 6] == 0x78 && buf[len + 13] == 0xDE);  // address at len+6
    CHECK(buf[len + 14] == 0xCC);
  }
  memset(buf, 0xCC, sizeof(buf));
  CHECK(ekm_build_trampoline(buf, 18, saved, 5, 0x1000ull) == 0);   // tail one byte short
  CHECK(buf[0] == 0xCC);                                            // and it wrote nothing
  CHECK(ekm_build_trampoline(buf, 5, saved, 5, 0x1000ull) == 0);    // no room for a tail
  CHECK(ekm_build_trampoline(buf, 19, saved, 5, 0x1000ull) == 19);  // one more byte is enough
  CHECK(ekm_build_trampoline(buf, 64, saved, 4, 0x1000ull) == 0);   // shorter than a jmp
  CHECK(ekm_build_trampoline(buf, 64, saved, 17, 0x1000ull) == 0);  // past original[]

  // The NOPs are not cosmetic: half an instruction left in the tail is a landing site for a later jump.
  memset(buf, 0xCC, sizeof(buf));
  CHECK(ekm_build_patch(buf, (int)sizeof(buf), 5, -0x1005) == 5);
  CHECK(buf[0] == 0xE9);
  CHECK(buf[1] == 0xFB && buf[2] == 0xEF && buf[3] == 0xFF && buf[4] == 0xFF);
  CHECK(buf[5] == 0xCC);                              // nothing padded at len 5

  memset(buf, 0xCC, sizeof(buf));
  CHECK(ekm_build_patch(buf, (int)sizeof(buf), 6, 0x0FFB) == 6);
  CHECK(buf[0] == 0xE9);
  CHECK(buf[1] == 0xFB && buf[2] == 0x0F && buf[3] == 0x00 && buf[4] == 0x00);
  CHECK(buf[5] == 0x90 && buf[6] == 0xCC);            // exactly one NOP

  memset(buf, 0xCC, sizeof(buf));
  CHECK(ekm_build_patch(buf, (int)sizeof(buf), 11, 0) == 11);
  int nops = 0;
  for (int i = 5; i < 11; i++) if (buf[i] == 0x90) nops++;
  CHECK(nops == 6);                                   // padded to the last byte
  CHECK(buf[11] == 0xCC);
  CHECK(ekm_build_patch(buf, 4, 5, 0) == 0);          // no room
  CHECK(ekm_build_patch(buf, 64, 4, 0) == 0);         // shorter than a jmp
}

// Keyed on curr_weapon, NOT projecTile: a tile discriminator refuses every shot once a CON mod renumbers tiles
// (gamehooks.cpp).
static void tb_0022_weapon_accuracy_map() {
  // The routing checks spell the same constant the switch does, so pin the numbers against dukeweapon_t (inv.h:62-72).
  CHECK(EK_WEAPON_PISTOL   == 1);
  CHECK(EK_WEAPON_SHOTGUN  == 2);
  CHECK(EK_WEAPON_CHAINGUN == 3);
  CHECK(EK_WEAPON_GROW     == 11);

  CHECK(!ekm_weapon_wants_accuracy(EK_WEAPON_PISTOL,   0, 0, 0, 0));
  CHECK(!ekm_weapon_wants_accuracy(EK_WEAPON_SHOTGUN,  0, 0, 0, 0));
  CHECK(!ekm_weapon_wants_accuracy(EK_WEAPON_CHAINGUN, 0, 0, 0, 0));
  CHECK(!ekm_weapon_wants_accuracy(EK_WEAPON_GROW,     0, 0, 0, 0));

  // Each row gates only its own weapon, not the other three.
  CHECK(ekm_weapon_wants_accuracy(EK_WEAPON_PISTOL,   1, 0, 0, 0));
  CHECK(!ekm_weapon_wants_accuracy(EK_WEAPON_SHOTGUN,  1, 0, 0, 0));
  CHECK(!ekm_weapon_wants_accuracy(EK_WEAPON_CHAINGUN, 1, 0, 0, 0));
  CHECK(!ekm_weapon_wants_accuracy(EK_WEAPON_GROW,     1, 0, 0, 0));

  CHECK(ekm_weapon_wants_accuracy(EK_WEAPON_SHOTGUN, 0, 1, 0, 0));
  CHECK(!ekm_weapon_wants_accuracy(EK_WEAPON_PISTOL, 0, 1, 0, 0));

  CHECK(ekm_weapon_wants_accuracy(EK_WEAPON_CHAINGUN, 0, 0, 1, 0));
  CHECK(!ekm_weapon_wants_accuracy(EK_WEAPON_PISTOL,  0, 0, 1, 0));

  CHECK(ekm_weapon_wants_accuracy(EK_WEAPON_GROW,    0, 0, 0, 1));
  CHECK(!ekm_weapon_wants_accuracy(EK_WEAPON_PISTOL, 0, 0, 0, 1));

  CHECK(ekm_weapon_wants_accuracy(EK_WEAPON_PISTOL,   1, 1, 1, 1));
  CHECK(ekm_weapon_wants_accuracy(EK_WEAPON_SHOTGUN,  1, 1, 1, 1));
  CHECK(ekm_weapon_wants_accuracy(EK_WEAPON_CHAINGUN, 1, 1, 1, 1));
  CHECK(ekm_weapon_wants_accuracy(EK_WEAPON_GROW,     1, 1, 1, 1));

  // RPG is 4 (inv.h:65) and never matches, even with every row on.
  CHECK(!ekm_weapon_wants_accuracy(4, 1, 1, 1, 1));
  CHECK(!ekm_weapon_wants_accuracy(0, 1, 1, 1, 1));
  CHECK(!ekm_weapon_wants_accuracy(-1, 1, 1, 1, 1));
}

// The hitscan map must not drift from the accuracy map it mirrors (kept side by side in playerstate.h).
static void tb_0023_weapon_hitscan_map() {
  CHECK(ekm_weapon_is_hitscan(EK_WEAPON_PISTOL));
  CHECK(ekm_weapon_is_hitscan(EK_WEAPON_SHOTGUN));
  CHECK(ekm_weapon_is_hitscan(EK_WEAPON_CHAINGUN));
  CHECK(ekm_weapon_is_hitscan(EK_WEAPON_GROW));

  for (int w = -1; w <= 12; w++) {
    const bool acc = ekm_weapon_wants_accuracy(w, 1, 1, 1, 1);
    const bool hit = ekm_weapon_is_hitscan(w);
    CHECK(acc == hit);
  }

  // A real projectile keeps its lead: RPG is 4 (inv.h:65).
  CHECK(!ekm_weapon_is_hitscan(4));
  CHECK(!ekm_weapon_is_hitscan(0));
  CHECK(!ekm_weapon_is_hitscan(-1));
  CHECK(!ekm_weapon_is_hitscan(999));
}

// The redirect bound separates "shoots what you meant" from "shoots backwards"; a raw subtraction fails across zero.
static void tb_0024_silent_redirect_bound() {
  CHECK(ekm_redirect_within(0, 256, 45));     // exactly on the limit is inside
  CHECK(!ekm_redirect_within(0, 257, 45));    // one unit past is not
  CHECK(ekm_redirect_within(0, 0, 1));        // dead on, at the narrowest row value
  CHECK(!ekm_redirect_within(0, 6, 1));       // 1 deg is 5 units; 6 is out

  // Direction does not matter: the bound is a magnitude.
  CHECK(ekm_redirect_within(1000, 1000 + 256, 45));
  CHECK(ekm_redirect_within(1000, 1000 - 256, 45));
  CHECK(!ekm_redirect_within(1000, 1000 + 257, 45));
  CHECK(!ekm_redirect_within(1000, 1000 - 257, 45));

  // A player at 2040 and a target at 10 are 18 units apart, not 2030; a raw difference refuses every shot near north.
  CHECK(ekm_redirect_within(2040, 10, 45));
  CHECK(ekm_redirect_within(10, 2040, 45));
  CHECK(ekm_redirect_within(2047, 0, 1));
  CHECK(ekm_redirect_within(0, 2047, 1));
  CHECK(!ekm_redirect_within(2040, 300, 45));   // 308 units the short way, still out

  CHECK(ekm_redirect_within(0, 1024, 180));     // the half turn itself
  CHECK(!ekm_redirect_within(0, 1024, 179));    // and one degree less refuses it

  // Two-sided: the yaw ON the limit is accepted and one unit out refused, so an always-true stub fails.
  for (int deg = 1; deg <= 64; deg++) {
    const int limit = deg * EKM_YAW_UNITS / 360;
    CHECK(ekm_redirect_within(0, limit, deg));
    CHECK(!ekm_redirect_within(0, limit + 1, deg));
  }

  // Out of range is reachable only by a hand-edited ini: above 180 saturates, zero or less redirects nothing.
  CHECK(ekm_redirect_within(0, 1024, 3600));
  CHECK(!ekm_redirect_within(0, 0, 0));
  CHECK(!ekm_redirect_within(0, 0, -45));
}

// The solution is published from the SwapBuffers hook but read inside a tic, and tics run BEFORE the draw, so only
// elapsed time (not a flag or frame counter) can expire it across a level change.
static void tb_0025_silent_solution_freshness() {
  // Four tics, the gap the velocity sampler calls a pause; pinned as a number so the two cannot drift.
  CHECK(EKM_SHOT_STALE_MS == 133);
  CHECK(EKM_SHOT_STALE_MS == EKM_LEAD_GAP_MAX_MS);

  CHECK(ekm_shot_fresh(0));
  CHECK(ekm_shot_fresh(1));
  CHECK(ekm_shot_fresh(33));
  CHECK(ekm_shot_fresh(100));
  CHECK(ekm_shot_fresh(EKM_SHOT_STALE_MS));       // exactly on the ceiling is fresh
  CHECK(!ekm_shot_fresh(EKM_SHOT_STALE_MS + 1));  // one millisecond past is not

  // A level load, save restore or alt-tab: the ages the rule exists to refuse.
  CHECK(!ekm_shot_fresh(200));
  CHECK(!ekm_shot_fresh(1000));
  CHECK(!ekm_shot_fresh(60000));

  CHECK(!ekm_shot_fresh((unsigned)0 - 1u));
  CHECK(!ekm_shot_fresh(0x80000000u));
  CHECK(!ekm_shot_fresh(0xFFFFFFFFu));

  // Walked from both sides so an off-by-one cannot cancel out.
  for (unsigned ms = EKM_SHOT_STALE_MS - 4; ms <= EKM_SHOT_STALE_MS + 4; ms++)
    CHECK(ekm_shot_fresh(ms) == (ms <= EKM_SHOT_STALE_MS));
}

// A latched player index goes stale on respawn; if its slot now holds an enemy, our yaw would land in the field
// A_ShootWithZvel reads for shootAng (player.cpp:1719) and the enemy shoots at our target.
static void tb_0026_player_sprite_identity() {
  // Pinned as numbers so a wrong VALUE cannot drift along with the test.
  CHECK(EK_APLAYER == 1405);      // names.h:407
  CHECK(EK_STAT_PLAYER == 10);    // actors.h:59

  CHECK(ekm_is_player_sprite(EK_APLAYER, EK_STAT_PLAYER));

  // Both halves are required, statnum most: game.cpp:2786-2794 sends multiplayer-start APLAYER sprites to STAT_MISC
  // (5), so picnum alone matches scenery.
  CHECK(!ekm_is_player_sprite(EK_APLAYER, 5));    // STAT_MISC: a map's spawn point
  CHECK(!ekm_is_player_sprite(EK_APLAYER, 0));
  CHECK(!ekm_is_player_sprite(EK_APLAYER, 1));    // STAT_ACTOR
  CHECK(!ekm_is_player_sprite(EK_APLAYER, 2));    // STAT_ZOMBIEACTOR
  CHECK(!ekm_is_player_sprite(EK_APLAYER, 13));   // STAT_DUMMYPLAYER

  CHECK(!ekm_is_player_sprite(1680, EK_STAT_PLAYER));   // LIZTROOP
  CHECK(!ekm_is_player_sprite(1680, 1));
  CHECK(!ekm_is_player_sprite(0, EK_STAT_PLAYER));
  CHECK(!ekm_is_player_sprite(0, 0));
  CHECK(!ekm_is_player_sprite(-1, -1));

  // For every statnum the rule accepts exactly STAT_PLAYER with the player's picnum.
  for (int st = 0; st < 32; st++) {
    CHECK(ekm_is_player_sprite(EK_APLAYER, st) == (st == EK_STAT_PLAYER));
    CHECK(!ekm_is_player_sprite(EK_APLAYER + 1, st));
  }
}

// G_DoSpriteAnimations draws actors at bpos + smoothratio * (pos - bpos) (game.cpp:3882-3884).
static void tb_0027_tic_interpolation() {
  // opos is a vec3_t right after pos (player.h:185), three int32s in.
  CHECK(EK_PS_OPOS == 12);
  CHECK(EK_PS_OPOS == EK_PS_POS + 12);
  CHECK(EK_PS_OPOS + 12 <= EK_PS_Q16HORIZ);   // and it ends before the pitch

  // Equality, not a tolerance: deriving the tic length back out of the rounded gap ceiling manufactures drift.
  CHECK(EKM_MS_PER_TIC == 1000.0 / 30.0);      // 30 Hz, the engine's own rate
  CHECK(EKM_LEAD_TICS_MAX == 4);
  // The ceiling is four tics, rounded, and still 133; a changed tic rate breaks this instead of moving it silently.
  CHECK(EKM_LEAD_GAP_MAX_MS == 133);

  CHECK(ekm_smoothratio(0.0) == 0.0);
  // A late frame must not extrapolate past the position the tic reached.
  CHECK(ekm_smoothratio(EKM_MS_PER_TIC) == 1.0);
  CHECK(ekm_smoothratio(EKM_MS_PER_TIC + 0.001) == 1.0);
  CHECK(ekm_smoothratio(100.0) == 1.0);
  CHECK(ekm_smoothratio(5000.0) == 1.0);
  // ...and not one a hair before, which stops an early clamp passing.
  CHECK(ekm_smoothratio(EKM_MS_PER_TIC - 0.001) < 1.0);
  CHECK(ekm_smoothratio(-1.0) == 0.0);
  CHECK(ekm_smoothratio(-100000.0) == 0.0);

  // Halves and quarters divide back exactly, so equalities; a denominator off by a quarter percent (33.25) fails each.
  CHECK(ekm_smoothratio(EKM_MS_PER_TIC / 2.0) == 0.5);
  CHECK(ekm_smoothratio(EKM_MS_PER_TIC / 4.0) == 0.25);
  CHECK(ekm_smoothratio(EKM_MS_PER_TIC / 8.0) == 0.125);

  // Strictly increasing inside the unit interval: a constant stub fails the step, a lost clamp the bound.
  double prevRatio = -1.0;
  for (int i = 0; i < 64; i++) {
    const double r = ekm_smoothratio(EKM_MS_PER_TIC * i / 64.0);
    CHECK(r >= 0.0 && r <= 1.0);
    CHECK(r > prevRatio);
    prevRatio = r;
  }

  CHECK(ekm_lerp_pos(1000, 2000, 0.0) == 1000);
  CHECK(ekm_lerp_pos(1000, 2000, 1.0) == 2000);
  CHECK(ekm_lerp_pos(2000, 1000, 0.0) == 2000);
  CHECK(ekm_lerp_pos(2000, 1000, 1.0) == 1000);
  CHECK(ekm_lerp_pos(-5000, -1000, 0.0) == -5000);
  CHECK(ekm_lerp_pos(-5000, -1000, 1.0) == -1000);

  CHECK(ekm_lerp_pos(1000, 2000, 0.5) == 1500);
  CHECK(ekm_lerp_pos(2000, 1000, 0.5) == 1500);
  CHECK(ekm_lerp_pos(-1000, 1000, 0.5) == 0);
  CHECK(ekm_lerp_pos(0, 1024, 0.25) == 256);
  CHECK(ekm_lerp_pos(0, 1024, 0.75) == 768);
  CHECK(ekm_lerp_pos(-2048, 0, 0.25) == -1536);

  for (int i = 0; i <= 8; i++)
    CHECK(ekm_lerp_pos(12345, 12345, (double)i / 8.0) == 12345);
  CHECK(ekm_lerp_pos(-7, -7, 0.5) == -7);
  CHECK(ekm_lerp_pos(0, 0, 2.0) == 0);

  // Clamped outside the unit interval so a stale stamp cannot push the aim point past the tic.
  CHECK(ekm_lerp_pos(1000, 2000, -0.5) == 1000);
  CHECK(ekm_lerp_pos(1000, 2000, 2.0) == 2000);
  CHECK(ekm_lerp_pos(1000, 2000, 100.0) == 2000);

  // Monotonic across one tic of motion, and never outside the two endpoints.
  int lastPos = 1000;
  for (int i = 0; i < 64; i++) {
    const int v = ekm_lerp_pos(1000, 2000, (double)i / 64.0);
    CHECK(v >= 1000 && v <= 2000);
    CHECK(v >= lastPos);
    lastPos = v;
  }

  CHECK(ekm_lerp_pos(0, 1000, ekm_smoothratio(EKM_MS_PER_TIC / 2.0)) == 500);
  CHECK(ekm_lerp_pos(0, 1000, ekm_smoothratio(EKM_MS_PER_TIC / 4.0)) == 250);
  CHECK(ekm_lerp_pos(0, 1000, ekm_smoothratio(0.0)) == 0);
  CHECK(ekm_lerp_pos(0, 1000, ekm_smoothratio(1000.0)) == 1000);
}

static void tb_0028_yaw_delta_q16() {
  CHECK(EKM_YAW_UNITS_Q16 == 0x8000000);
  CHECK(EKM_YAW_UNITS_Q16 == ekm_f16(EKM_YAW_UNITS));

  CHECK(ekm_yaw_delta_q16(0, 0) == 0);
  CHECK(ekm_yaw_delta_q16(ekm_f16(512), ekm_f16(512)) == 0);
  CHECK(ekm_yaw_delta_q16(EKM_YAW_UNITS_Q16 - 1, EKM_YAW_UNITS_Q16 - 1) == 0);
  CHECK(ekm_yaw_delta_q16(12345, 12345) == 0);

  CHECK(ekm_yaw_delta_q16(ekm_f16(100) + 32768, ekm_f16(100)) == -32768);
  CHECK(ekm_yaw_delta_q16(ekm_f16(100), ekm_f16(100) + 32768) == 32768);
  CHECK(ekm_yaw_delta_q16(0, 1) == 1);
  CHECK(ekm_yaw_delta_q16(1, 0) == -1);
  CHECK(ekm_yaw_delta_q16(EKM_YAW_UNITS_Q16 - 1, 0) == 1);
  CHECK(ekm_yaw_delta_q16(0, EKM_YAW_UNITS_Q16 - 1) == -1);

  CHECK(ekm_yaw_delta_q16(ekm_f16(2040), ekm_f16(10)) == ekm_f16(18));
  CHECK(ekm_yaw_delta_q16(ekm_f16(10), ekm_f16(2040)) == -ekm_f16(18));
  CHECK(ekm_yaw_delta_q16(ekm_f16(2047), ekm_f16(0)) == ekm_f16(1));
  CHECK(ekm_yaw_delta_q16(ekm_f16(0), ekm_f16(2047)) == -ekm_f16(1));

  CHECK(ekm_yaw_delta_q16(0, ekm_f16(1024)) == ekm_f16(1024));
  CHECK(ekm_yaw_delta_q16(ekm_f16(1024), 0) == ekm_f16(1024));
  CHECK(ekm_yaw_delta_q16(ekm_f16(1500), ekm_f16(476)) == ekm_f16(1024));

  CHECK(ekm_yaw_delta_q16(0, ekm_f16(1024) + 1) == -(ekm_f16(1024) - 1));
  CHECK(ekm_yaw_delta_q16(0, ekm_f16(1024) - 1) == ekm_f16(1024) - 1);

  // Inputs are not assumed in range: the engine masks q16ang (player.cpp:3080) but a full turn and a negative angle
  // must answer as the masked pair would.
  CHECK(ekm_yaw_delta_q16(EKM_YAW_UNITS_Q16 + 12345, 12345) == 0);
  CHECK(ekm_yaw_delta_q16(12345, EKM_YAW_UNITS_Q16 + 12345) == 0);
  CHECK(ekm_yaw_delta_q16(-65536, 0) == 65536);
  CHECK(ekm_yaw_delta_q16(0, -65536) == -65536);

  // Agrees with the whole-unit version when both ends are whole; 8 x 8 pairs, second stride coprime with the first.
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 8; j++) {
      const int a = i * 256;   // 0, 256, ... 1792
      const int b = j * 257;   // 0, 257, ... 1799
      CHECK(ekm_yaw_delta_q16(ekm_f16(a), ekm_f16(b)) ==
            ekm_f16(ekm_yaw_delta(a, b)));
    }
  }

  for (int i = 0; i < 64; i++) {
    const int from = i * 2097152;                  // 32 units apart, in 16.16
    const int to   = (63 - i) * 2097152 + 33333;   // and a fraction, always
    const int d    = ekm_yaw_delta_q16(from, to);
    CHECK(d >= -(EKM_YAW_UNITS_Q16 / 2) && d <= EKM_YAW_UNITS_Q16 / 2);
  }
}

static void tb_0029_weapon_proj_speed() {
  // Every index, named, with exact values: one wrong number is invisible until a rocket lands in the floor.
  CHECK(ekm_weapon_proj_speed(0)  == 0);     // knee: nothing is fired
  CHECK(ekm_weapon_proj_speed(1)  == 0);     // pistol, hitscan
  CHECK(ekm_weapon_proj_speed(2)  == 0);     // shotgun, hitscan
  CHECK(ekm_weapon_proj_speed(3)  == 0);     // chaingun, hitscan
  CHECK(ekm_weapon_proj_speed(4)  == 644);   // RPG
  CHECK(ekm_weapon_proj_speed(5)  == 0);     // pipe bomb: thrown, arcs, bounces
  CHECK(ekm_weapon_proj_speed(6)  == 768);   // shrinker
  CHECK(ekm_weapon_proj_speed(7)  == 644);   // devastator, the RPG's own case
  CHECK(ekm_weapon_proj_speed(8)  == 0);     // trip bomb: a wall placement
  CHECK(ekm_weapon_proj_speed(9)  == 644);   // freezer, the RPG's own case
  CHECK(ekm_weapon_proj_speed(10) == 0);     // handremote: nothing is fired
  CHECK(ekm_weapon_proj_speed(11) == 0);     // expander, hitscan
  CHECK(ekm_weapon_proj_speed(12) == 840);   // flamethrower

  CHECK(ekm_weapon_proj_speed(EK_WEAPON_RPG) == 644);
  CHECK(ekm_weapon_proj_speed(EK_WEAPON_DEVISTATOR) == 644);
  CHECK(ekm_weapon_proj_speed(EK_WEAPON_FREEZE) == 644);
  CHECK(ekm_weapon_proj_speed(EK_WEAPON_SHRINKER) == 768);
  CHECK(ekm_weapon_proj_speed(EK_WEAPON_FLAMETHROWER) == 840);
  CHECK(EK_WEAPON_RPG == 4 && EK_WEAPON_SHRINKER == 6);
  CHECK(EK_WEAPON_DEVISTATOR == 7 && EK_WEAPON_FREEZE == 9);
  CHECK(EK_WEAPON_FLAMETHROWER == 12);

  for (int w = -1; w <= 13; w++) {
    if (ekm_weapon_is_hitscan(w)) CHECK(ekm_weapon_proj_speed(w) == 0);
    if (w < 0 || w > 12) CHECK(ekm_weapon_proj_speed(w) == 0);
  }
  CHECK(ekm_weapon_proj_speed(999) == 0);
  CHECK(ekm_weapon_proj_speed(-999) == 0);

  // 800 was the speed of no weapon and must not return as a default for an unmapped one.
  for (int w = -1; w <= 13; w++) CHECK(ekm_weapon_proj_speed(w) != 800);

  for (int w = 0; w <= 12; w++) {
    const int v = ekm_weapon_proj_speed(w);
    CHECK(v == 0 || (v >= 128 && v <= 4096));
  }
}

// Exact equality: `gm & EK_MODE_GAME` is true for MODE_GAME|MODE_MENU (0x05) and |MODE_TYPE (0x14), the pause menu and
// chat.
static void tb_0030_esp_level_loaded() {
  CHECK(EK_MODE_GAME == 0x04);

  CHECK(esp_level_loaded(0x04, true) == true);    // exactly MODE_GAME: loaded

  CHECK(esp_level_loaded(0x00, true) == false);   // premap.cpp:1465, mid-load
  CHECK(esp_level_loaded(0x05, true) == false);   // MODE_GAME|MODE_MENU: pause
  CHECK(esp_level_loaded(0x14, true) == false);   // MODE_GAME|MODE_TYPE: chat
  CHECK(esp_level_loaded(0x08, true) == false);   // MODE_EOL alone
  CHECK(esp_level_loaded(0x02, true) == false);   // MODE_DEMO alone

  CHECK(esp_level_loaded(0x04, false) == false);  // failed read: draw nothing
  CHECK(esp_level_loaded(0x00, false) == false);
}

static void tb_0031_mousebind_table() {
  CHECK(MB_ACTION_COUNT == 7);

  CHECK(strcmp(mousebind_action_name(0), "Off") == 0);
  CHECK(MB_ACTIONS[0].bit < 0);
  CHECK(mousebind_action_mask(0) == 0u);

  CHECK(strcmp(mousebind_action_name(1), "Quick kick") == 0);
  CHECK(mousebind_action_mask(1) == (1u << 22));   // SK_QUICK_KICK
  CHECK(strcmp(mousebind_action_name(2), "Jetpack") == 0);
  CHECK(mousebind_action_mask(2) == (1u << 25));   // SK_JETPACK
  CHECK(strcmp(mousebind_action_name(3), "Night vision") == 0);
  CHECK(mousebind_action_mask(3) == (1u << 15));   // SK_NIGHTVISION
  CHECK(strcmp(mousebind_action_name(4), "Medkit") == 0);
  CHECK(mousebind_action_mask(4) == (1u << 16));   // SK_MEDKIT
  CHECK(strcmp(mousebind_action_name(5), "HoloDuke") == 0);
  CHECK(mousebind_action_mask(5) == (1u << 24));   // SK_HOLODUKE
  CHECK(strcmp(mousebind_action_name(6), "Steroids") == 0);
  CHECK(mousebind_action_mask(6) == (1u << 12));   // SK_STEROIDS

  for (int i = 0; i < MB_ACTION_COUNT; i++)
    CHECK(mousebind_action_mask(i) != (1u << 2));

  for (int i = 1; i < MB_ACTION_COUNT; i++) {
    CHECK(MB_ACTIONS[i].bit >= 0 && MB_ACTIONS[i].bit <= 31);
    for (int j = i + 1; j < MB_ACTION_COUNT; j++)
      CHECK(MB_ACTIONS[i].bit != MB_ACTIONS[j].bit);
  }

  static const int kBad[] = { -1, 7, 8, 99, -99, 32767, -32768 };
  for (int i = 0; i < (int)(sizeof(kBad) / sizeof(kBad[0])); i++) {
    CHECK(mousebind_action_mask(kBad[i]) == 0u);
    CHECK(strcmp(mousebind_action_name(kBad[i]), "Off") == 0);
  }

  // The held flags decide, not the selection.
  CHECK(mousebind_bits(1, false, 2, false) == 0u);
  CHECK(mousebind_bits(1, true,  2, false) == (1u << 22));
  CHECK(mousebind_bits(1, false, 2, true)  == (1u << 25));
  CHECK(mousebind_bits(1, true,  2, true)  == ((1u << 22) | (1u << 25)));

  CHECK(mousebind_bits(0, true, 0, true) == 0u);
  CHECK(mousebind_bits(0, true, 3, true) == (1u << 15));

  // Both buttons on the same action is one bit, not two presses.
  CHECK(mousebind_bits(4, true, 4, true) == (1u << 16));

  CHECK(mousebind_bits(99, true, -1, true) == 0u);
}

// The ESP draw-distance cull is dist2d against the limit, never depth (aimsolve.h).
static void tb_0032_esp_draw_distance() {
  CHECK(ekm_within_draw_distance(100, 500, 0) == true);     // inside
  CHECK(ekm_within_draw_distance(900, 500, 0) == false);    // outside
  CHECK(ekm_within_draw_distance(500, 500, 0) == true);     // exactly at the limit
  CHECK(ekm_within_draw_distance(999999, 1, 1) == true);    // unlimited overrides a small limit
  CHECK(ekm_within_draw_distance(-5, 500, 0) == false);     // negative distance: never inside
  CHECK(ekm_within_draw_distance(0, 500, 0) == true);       // zero distance: at the player
}

// The triggerbot's pure rules; a >= fire test fires one tic early at every setting.
static void tb_0033_triggerbot_rules() {
  CHECK(tbot_delay_tics(0) == 0);       // no delay is the identity
  CHECK(tbot_delay_tics(16) == 0);      // 0.48 tics: rounds DOWN
  CHECK(tbot_delay_tics(17) == 1);      // 0.51 tics: rounds UP
  CHECK(tbot_delay_tics(500) == 15);    // the row's top, 500 ms at 30 tics a second
  CHECK(tbot_delay_tics(-40) == 0);
  CHECK(tbot_delay_tics(99999) == tbot_delay_tics(TBOT_DELAY_MS_MAX));

  CHECK(tbot_held_tics(0) == 1);
  CHECK(tbot_held_tics(33) == 1);
  CHECK(tbot_held_tics(34) == 2);
  CHECK(tbot_held_tics(500) == 16);

  TbotCond hold = { 1, "on" };
  TbotCond miss = { 0, "notarget" };
  const char* why = "";
  TbotState st = { 0, 0, 0 };

  CHECK(tbot_decide(true, false, false, false, false, false, true, hold, 0, 16, &st, &why) == true);
  CHECK_MSG(strcmp(why, "fire") == 0, "a firing frame reports fire");

  const bool refusal[7][7] = {
    { false, false, false, false, false, false, true  },
    { true,  true,  false, false, false, false, true  },
    { true,  false, true,  false, false, false, true  },
    { true,  false, false, true,  false, false, true  },
    { true,  false, false, false, true,  false, true  },
    { true,  false, false, false, false, true,  true  },
    { true,  false, false, false, false, false, false },
  };
  const char* const expected[7] = { "off", "menu", "paused", "dead", "frozen",
                                    "panel", "weapon" };
  for (int i = 0; i < 7; i++) {
    TbotState s2 = { 0, 0, 0 };
    for (int f = 0; f < 4; f++)
      tbot_decide(true, false, false, false, false, false, true, hold, 15, 33, &s2, &why);
    CHECK(s2.heldMs > 0);
    const bool fired = tbot_decide(refusal[i][0], refusal[i][1], refusal[i][2],
                                   refusal[i][3], refusal[i][4], refusal[i][5],
                                   refusal[i][6], hold, 15, 33, &s2, &why);
    CHECK(fired == false);
    CHECK_MSG(strcmp(why, expected[i]) == 0, "the first refusal names itself");
    CHECK_MSG(s2.heldMs == 0, "a refusal zeroes the held clock");
    CHECK(s2.heldTics == 0);
    CHECK(s2.firing == 0);
  }

  {
    TbotState s2 = { 0, 0, 0 };
    for (int f = 0; f < 4; f++)
      tbot_decide(true, false, false, false, false, false, true, hold, 15, 33, &s2, &why);
    CHECK(tbot_decide(true, false, false, false, false, false, true, miss, 15, 33, &s2, &why) == false);
    CHECK_MSG(strcmp(why, "notarget") == 0, "the condition's own reason survives");
    CHECK(s2.heldMs == 0);
  }

  // Fire needs held > delay, NOT >=: a 10-tic delay waits frames 1..10 and fires on 11.
  {
    TbotState s2 = { 0, 0, 0 };
    int firstFire = -1;
    for (int f = 1; f <= 20; f++) {
      const bool fired = tbot_decide(true, false, false, false, false, false, true,
                                     hold, 10, 33, &s2, &why);
      if (fired && firstFire < 0) firstFire = f;
    }
    CHECK_MSG(firstFire == 11, "a 10-tic delay fires on the 11th tic, not the 10th");
  }

  // The refusal restarts the whole wait: it does not resume where it stopped.
  {
    TbotState s2 = { 0, 0, 0 };
    for (int f = 0; f < 8; f++)
      tbot_decide(true, false, false, false, false, false, true, hold, 10, 33, &s2, &why);
    tbot_decide(true, false, false, false, false, false, true, miss, 10, 33, &s2, &why);
    int firstFire = -1;
    for (int f = 1; f <= 20; f++) {
      const bool fired = tbot_decide(true, false, false, false, false, false, true,
                                     hold, 10, 33, &s2, &why);
      if (fired && firstFire < 0) firstFire = f;
    }
    CHECK_MSG(firstFire == 11, "the delay starts again after a refusal");
  }

  {
    TbotState fast = { 0, 0, 0 };
    int fastMs = 0;
    for (int f = 0; f < 200; f++) {
      if (tbot_decide(true, false, false, false, false, false, true, hold, 10, 4, &fast, &why)) break;
      fastMs += 4;
    }
    TbotState slow = { 0, 0, 0 };
    int slowMs = 0;
    for (int f = 0; f < 40; f++) {
      if (tbot_decide(true, false, false, false, false, false, true, hold, 10, 33, &slow, &why)) break;
      slowMs += 33;
    }
    CHECK_MSG(fastMs >= slowMs - 33 && fastMs <= slowMs + 33,
              "the delay is a time, not a frame count");
  }

  // A stalled frame contributes at most four tics: an alt-tab is not a held condition.
  {
    TbotState s2 = { 0, 0, 0 };
    tbot_decide(true, false, false, false, false, false, true, hold, 10, 10000, &s2, &why);
    CHECK(s2.heldMs == TBOT_FRAME_MS_MAX);
    CHECK(s2.firing == 0);
  }

  CHECK(tbot_when_aimbot(true).holds == 1);
  CHECK(tbot_when_aimbot(false).holds == 0);
  CHECK_MSG(strcmp(tbot_when_aimbot(false).reason, "nolock") == 0, "no lock says so");

  const int onHoriz = ekm_f16(100);
  CHECK(tbot_when_crosshair(true, 1000, 1000, onHoriz, onHoriz).holds == 1);
  CHECK(tbot_when_crosshair(false, 1000, 1000, onHoriz, onHoriz).holds == 0);
  CHECK(tbot_when_crosshair(true, 1000, 1000 + TBOT_CROSSHAIR_YAW_UNITS,
                            onHoriz, onHoriz).holds == 1);   // exactly at the edge
  CHECK(tbot_when_crosshair(true, 1000, 1000 + TBOT_CROSSHAIR_YAW_UNITS + 1,
                            onHoriz, onHoriz).holds == 0);
  // Across the wrap: yaw 2040 and 6 are 14 units apart, not 2034, which is why the cone goes through
  // ekm_angular_offset.
  CHECK(tbot_when_crosshair(true, 2040, 6, onHoriz, onHoriz).holds == 1);
  CHECK(tbot_when_crosshair(true, 1000, 1000, onHoriz,
                            onHoriz - ekm_f16(TBOT_CROSSHAIR_HORIZ_UNITS)).holds == 1);
  CHECK(tbot_when_crosshair(true, 1000, 1000, onHoriz,
                            onHoriz + ekm_f16(TBOT_CROSSHAIR_HORIZ_UNITS) + 1).holds == 0);

  CHECK(tbot_weapon_usable(EK_WEAPON_PISTOL) == true);
  CHECK(tbot_weapon_usable(EK_WEAPON_SHOTGUN) == true);
  CHECK(tbot_weapon_usable(EK_WEAPON_CHAINGUN) == true);
  CHECK(tbot_weapon_usable(EK_WEAPON_RPG) == true);
  CHECK(tbot_weapon_usable(EK_WEAPON_DEVISTATOR) == true);
  CHECK(tbot_weapon_usable(EK_WEAPON_FREEZE) == true);
  CHECK(tbot_weapon_usable(EK_WEAPON_SHRINKER) == true);
  CHECK(tbot_weapon_usable(EK_WEAPON_GROW) == true);
  CHECK(tbot_weapon_usable(0) == false);    // knee
  CHECK(tbot_weapon_usable(5) == false);    // pipe bomb: thrown on the RELEASE edge
  CHECK(tbot_weapon_usable(8) == false);    // trip bomb: placed on a wall
  CHECK(tbot_weapon_usable(10) == false);   // handremote: fires nothing
  CHECK(tbot_weapon_usable(-1) == false);
  CHECK(tbot_weapon_usable(9999) == false);

  CHECK(tbot_when_clamped(TBOT_WHEN_AIMBOT) == TBOT_WHEN_AIMBOT);
  CHECK(tbot_when_clamped(-7) == TBOT_WHEN_CROSSHAIR);
  CHECK(tbot_when_clamped(42) == TBOT_WHEN_CROSSHAIR);

  CHECK(EK_SK_FIRE == 2);
}

static void tb_0034_target_liveness() {
  const int PIC = 2120;              // LIZMAN: an enemy the trooper scope excludes
  const unsigned CS = 257;           // and a live cstat, so only extra and pal decide
  const unsigned char XR = 32;
  CHECK(!ekm_target_dying(100, 0, PIC, CS, XR));    // an ordinary live enemy
  CHECK(!ekm_target_dying(1, 0, PIC, CS, XR));      // one hit point left
  CHECK_MSG(ekm_target_dying(0, 0, PIC, CS, XR), "the exact kill is dead, not alive");
  CHECK(ekm_target_dying(-1, 0, PIC, CS, XR));
  CHECK(ekm_target_dying(-400, 0, PIC, CS, XR));    // overkill, an RPG corpse
  // Frozen is not dead: pal == 1 is the frozen marker and a frozen enemy sits at zero but is still a legitimate target.
  CHECK_MSG(!ekm_target_dying(0, EKM_PAL_FROZEN, PIC, CS, XR), "frozen at zero is a target");
  CHECK_MSG(!ekm_target_dying(-5, EKM_PAL_FROZEN, PIC, CS, XR), "frozen below zero is a target");
  CHECK(!ekm_target_dying(50, EKM_PAL_FROZEN, PIC, CS, XR));
  CHECK(ekm_target_dying(0, 2, PIC, CS, XR));
  CHECK(ekm_target_dying(0, 21, PIC, CS, XR));
  CHECK(EKM_PAL_FROZEN == 1);
}

// A rule `extra == 0 && !hittable` asserted that a choking Assault Trooper is alive, but stock GAME.CON runs `strength
// 0` on the lying-dead corpses of the Enforcer, Octabrain and Protector Drone without clearing cstat, which made those
// corpses permanent targets.

static void tb_0036_enemy_names() {
  // The Captain is a PALETTE, not a picnum: keying on picnum alone labels every Assault Captain "Assault Trooper"
  // (World Tour, esp_pics.h:29-34).
  CHECK(strcmp(ekm_enemy_name_pal(1680, 0), "Assault Trooper") == 0);
  CHECK_MSG(strcmp(ekm_enemy_name_pal(1680, EKM_PAL_CAPTAIN), "Assault Captain") == 0,
            "a trooper picnum at pal 21 is a Captain");
  CHECK(strcmp(ekm_enemy_name_pal(1744, EKM_PAL_CAPTAIN), "Assault Captain") == 0);
  CHECK(strcmp(ekm_enemy_name_pal(1725, 0), "Assault Trooper") == 0);
  // A non-trooper at pal 21 is not a Captain: on a boss that palette is a strength boost, not an identity.
  CHECK_MSG(strcmp(ekm_enemy_name_pal(1920, EKM_PAL_CAPTAIN), "Assault Commander") == 0,
            "pal 21 on a commander is still a commander");
  CHECK(strcmp(ekm_enemy_name_pal(2000, EKM_PAL_CAPTAIN), "Pig Cop") == 0);

  // The Battlelord Sentry is also a palette: GAME.CON:6466-6470 gives a pal != 0 BOSS1 `strength BOSS1PALSTRENGTH`
  // (1000, from 4500) and game.cpp:2073 halves its size, a different enemy the game calls the Sentry.
  CHECK_MSG(strcmp(ekm_enemy_name_pal(2630, 0), "Battlelord") == 0,
            "an unpaletted BOSS1 is still the Battlelord");
  CHECK_MSG(strcmp(ekm_enemy_name_pal(2630, EKM_PAL_CAPTAIN), "Battlelord Sentry") == 0,
            "a paletted BOSS1 is the Sentry, not the Battlelord");
  CHECK(strcmp(ekm_enemy_name_pal(2630, 1), "Battlelord Sentry") == 0);
  CHECK(strcmp(ekm_enemy_name_pal(2631, 21), "Battlelord Sentry") == 0);
  CHECK(strcmp(ekm_enemy_name_pal(2631, 0), "Battlelord") == 0);
  CHECK(strcmp(ekm_enemy_name_pal(2710, 21), "Overlord") == 0);
  CHECK(strcmp(ekm_enemy_name_pal(2760, 21), "Cycloid Emperor") == 0);

  // Every listed picnum names something; fifty-three values from EDuke32's own names.h, not World Tour's table.
  static const int listed[] = {
    675, 1267, 1550, 1680, 1681, 1682, 1715, 1725, 1741, 1742, 1744, 1810,
    1820, 1821, 1880, 1920, 1921, 1960, 1975, 2000, 2001, 2045, 2120, 2121,
    2150, 2160, 2165, 2360, 2370, 2371, 2372, 2373, 2374, 2375, 2376, 2377,
    2420, 2630, 2631, 2710, 2711, 2760, 2761, 4610, 4611, 4670, 4671, 4690,
    4740, 4741, 5180, 5310, 5311 };
  for (int i = 0; i < (int)(sizeof(listed) / sizeof(listed[0])); i++)
    CHECK_MSG(ekm_enemy_name(listed[i]) != 0, "every listed picnum has a name");

  // An unlisted picnum returns NULL so the caller prints the number: tile names are runtime variables a CON `define`
  // can move (dnames.cpp:1595).
  CHECK_MSG(ekm_enemy_name(0) == 0, "an unlisted picnum has no name");
  CHECK(ekm_enemy_name(1679) == 0);
  CHECK(ekm_enemy_name(2369) == 0);        // one below the slimer range
  CHECK(ekm_enemy_name(2378) == 0);        // one above it
  CHECK(ekm_enemy_name(9999) == 0);
  CHECK(ekm_enemy_name_pal(9999, EKM_PAL_CAPTAIN) == 0);
  CHECK(ekm_enemy_name_pal(0, 0) == 0);

  CHECK(EKM_PAL_CAPTAIN == 21);
  CHECK(ekm_picnum_is_trooper(1680) && !ekm_picnum_is_trooper(1920));
}

// Weapon speed moves a counter the engine steps itself every tic, so the risks are moving a counter the engine owns,
// moving one past a frame where the engine acts, and the carry.

// One firing cycle simulated as the engine and weaponspeed.cpp interleave (engine steps, then the feature advances).
static int wspd_cycle_tics(const FireRateWeapon& t, int ratePct) {
  double carry = 0.0;
  const int last = t.events[t.nEvents - 1];
  int tics = 1;
  int k = firerate_advance(t, 1, firerate_steps(ratePct, &carry));   // the press
  while (k <= last && tics < 64) {
    k++;                                       // the engine's own per-tic step
    tics++;
    k = firerate_advance(t, k, firerate_steps(ratePct, &carry));
  }
  return tics;
}

static void tb_0037_weapon_speed() {
  const FireRateWeapon* const rpg = firerate_weapon(4);
  const FireRateWeapon* const pistol = firerate_weapon(1);
  CHECK_MSG(rpg != 0 && pistol != 0, "the table has the RPG and the pistol");
  if (!rpg || !pistol) return;

  // 100 is the identity: no step at all, since the off state is a slider value, not a switch.
  CHECK_MSG(firerate_steps(100, 0) == 0, "100% asks for no extra step");
  {
    double carry = 0.0;
    int total = 0;
    for (int i = 0; i < 30; i++) total += firerate_steps(100, &carry);
    CHECK_MSG(total == 0 && carry == 0.0, "thirty tics at 100% advance nothing");
  }
  CHECK_MSG(wspd_cycle_tics(*rpg, 100) == 20,
            "at 100% the RPG takes its stock 20 tics (TotalTime, gamevars.cpp:1524)");

  // 200 doubles; the cycle is 11, not 10, because the shot at frame 4 is a landing the feature may not step past.
  CHECK_MSG(firerate_steps(200, 0) == 1, "200% is one extra step a tic");
  CHECK_MSG(wspd_cycle_tics(*rpg, 200) == 11, "at 200% the RPG's cycle halves");
  CHECK_MSG(firerate_steps(400, 0) == 3, "400% is three extra steps a tic");

  {
    double carry = 0.0;
    const int t1 = firerate_steps(150, &carry);
    const int t2 = firerate_steps(150, &carry);
    CHECK_MSG(t1 == 0 && t2 == 1, "150% is half a step a tic, paid every other tic");
    int total = t1 + t2;
    for (int i = 0; i < 8; i++) total += firerate_steps(150, &carry);
    CHECK_MSG(total == 5, "ten tics at 150% are five extra steps, not zero");
  }
  CHECK_MSG(wspd_cycle_tics(*rpg, 150) < wspd_cycle_tics(*rpg, 100),
            "150% is faster than 100% -- the carry is not floored away");
  CHECK_MSG(wspd_cycle_tics(*rpg, 150) == 14, "at 150% the RPG's cycle is 14 tics");

  {
    double carry = 0.0;
    firerate_steps(150, &carry);                       // half a step owed
    CHECK_MSG(carry > 0.0, "150% leaves a fraction owed");
    carry = firerate_carry_for_weapon(carry, 1, 2);    // the weapon changed
    CHECK_MSG(carry == 0.0, "a weapon change drops the old weapon's carry");
    CHECK_MSG(firerate_steps(150, &carry) == 0,
              "after a reset the next tic asks for nothing again");
    double keep = 0.75;
    CHECK_MSG(firerate_carry_for_weapon(keep, 3, 3) == 0.75,
              "the same weapon keeps its carry");
  }

  // Rest never moves: kickback_pic 0 means not firing and the PRESS sets it to 1 (player.cpp:4588); advancing from 0
  // would fire for the feature.
  CHECK_MSG(firerate_advance(*rpg, 0, 3) == 0, "a resting weapon is never advanced");
  CHECK_MSG(firerate_advance(*rpg, -1, 3) == -1, "nor is a negative counter");
  CHECK_MSG(firerate_advance(*rpg, 5, 0) == 5, "nor is anything at zero steps");

  // -9 is where P_ProcessWeapon turns lowering into rising (player.cpp:4205) and 0 where a switch finishes.
  CHECK_MSG(wswitch_advance(0, 3) == 0, "a settled weapon is never advanced");
  CHECK_MSG(wswitch_advance(-9, 3) == -9, "the flip value is never moved");
  CHECK_MSG(wswitch_advance(-1, 3) == -4, "the lowering run compresses");
  CHECK_MSG(wswitch_advance(-6, 5) == -8, "and stops at -8, not past it");
  CHECK_MSG(wswitch_advance(10, 3) == 7, "the raising run compresses");
  CHECK_MSG(wswitch_advance(3, 5) == 1, "and stops at 1, not past it");
  CHECK_MSG(wswitch_advance(-4, 0) == -4 && wswitch_advance(4, -2) == 4,
            "no steps means no move, on either run");

  {
    bool pastEvent = false, wentBackwards = false, movedRest = false;
    for (int w = 0; w < 13; w++) {
      const FireRateWeapon* const t = firerate_weapon(w);
      if (!t) continue;
      const int last = t->events[t->nEvents - 1];
      for (int k = 1; k <= last; k++) {
        const int nxt = firerate_next_event(*t, k);
        for (int steps = 1; steps <= 3; steps++) {
          const int got = firerate_advance(*t, k, steps);
          if (got > nxt) pastEvent = true;
          if (got < k) wentBackwards = true;
        }
      }
      if (firerate_advance(*t, 0, 3) != 0) movedRest = true;
    }
    CHECK_MSG(!pastEvent, "no advance lands past the next event, for any weapon");
    CHECK_MSG(!wentBackwards, "and none moves a counter backwards");
    CHECK_MSG(!movedRest, "and none moves a weapon at rest");
  }
  // A counter already on an event is held there: the engine performs it next tic, so nothing is left to compress.
  CHECK_MSG(firerate_advance(*rpg, 3, 3) == 3, "a counter on an event is held");
  CHECK_MSG(firerate_next_event(*rpg, 1) == 3 && firerate_next_event(*rpg, 4) == 19,
            "the next event is the smallest at or above the counter");
  CHECK_MSG(firerate_next_event(*rpg, 20) == -1, "past the last event there is none");
  CHECK_MSG(firerate_advance(*rpg, 20, 3) == 20, "and nothing past it is advanced");

  CHECK_MSG(firerate_rate_clamped(100) == 100 && firerate_rate_clamped(400) == 400,
            "the ends are stops");
  CHECK_MSG(firerate_rate_clamped(0) == 100 && firerate_rate_clamped(9999) == 400,
            "and everything outside is clamped onto them");
  CHECK_MSG(firerate_rate_clamped(124) == 100 && firerate_rate_clamped(125) == 150,
            "the nearest stop wins, and the halfway point rounds up");
  CHECK_MSG(firerate_rate_clamped(174) == 150 && firerate_rate_clamped(176) == 200,
            "a drag either side of 175 settles on its own stop");
  CHECK_MSG(FIRERATE_DEFAULT == FIRERATE_MIN,
            "the default is the identity, which is what a reset has to give back");
  // 100..124 all snap to 100, which must be the identity.
  {
    bool cheated = false;
    for (int v = FIRERATE_MIN; v < FIRERATE_MIN + FIRERATE_STEP / 2; v++)
      if (firerate_steps(v, 0) != 0) cheated = true;
    CHECK_MSG(!cheated, "no rate that snaps to 100 asks for a step");
  }

  // Per tic, not per frame: four 16 ms frames owe one tic, and a per-frame advance would change the rate with the frame
  // rate.
  {
    double acc = 0.0;
    CHECK_MSG(firerate_tics_due(&acc, 16) == 0, "one 16 ms frame is not a tic");
    CHECK_MSG(firerate_tics_due(&acc, 16) == 0, "nor are two");
    CHECK_MSG(firerate_tics_due(&acc, 16) == 1, "three of them are");
    int tics = 0;
    acc = 0.0;
    for (int i = 0; i < 60; i++) tics += firerate_tics_due(&acc, 16);   // 960 ms
    CHECK_MSG(tics == 28, "960 ms of 16 ms frames is 28 tics, not 60");
    acc = 0.0;
    CHECK_MSG(firerate_tics_due(&acc, 34) == 1, "a 34 ms frame is one tic");
    acc = 0.0;
    CHECK_MSG(firerate_tics_due(&acc, 5000) == FIRERATE_TICS_MAX,
              "a stall is capped rather than paid as a burst");
    CHECK_MSG(acc == 0.0, "and its remainder is dropped, not banked");
    CHECK_MSG(firerate_tics_due(0, 100) == 0, "a null accumulator owes nothing");
  }

  // The table against EDuke32's weapondefaults (gamevars.cpp) as read by P_ProcessWeapon's basic path
  // (player.cpp:4587-4741).
  CHECK_MSG(rpg->nEvents == 2 && rpg->events[0] == 3 && rpg->events[1] == 19,
            "RPG: shot at 4 (FireDelay), reset at 20 (TotalTime)");
  CHECK_MSG(pistol->nEvents == 5 && pistol->events[0] == 1 && pistol->events[4] == 26,
            "pistol: shot at 2, and the reload ladder ends at 27 (Reload)");
  {
    const FireRateWeapon* const sg = firerate_weapon(2);
    CHECK_MSG(sg && sg->nEvents == 5 && sg->events[2] == 14 && sg->events[3] == 23,
              "shotgun: the cock sound at 15 and the shell at 24 are both kept");
  }
  {
    const FireRateWeapon* const cg = firerate_weapon(3);
    CHECK_MSG(cg && cg->events[0] == 2 && cg->events[1] == 5 && cg->events[2] == 8,
              "chaingun: FIREEVERYTHIRD fires at 3, 6 and 9");
  }
  // Three weapons get no entry, deliberately: the pipebomb and hand remote never run the counting path, and the
  // tripbomb's counter is read outside P_ProcessWeapon (player.cpp:3035).
  CHECK_MSG(firerate_weapon(5) == 0, "the pipebomb has no entry");
  CHECK_MSG(firerate_weapon(8) == 0, "the tripbomb has no entry");
  CHECK_MSG(firerate_weapon(10) == 0, "the hand remote has no entry");
  CHECK_MSG(firerate_weapon(-1) == 0 && firerate_weapon(99) == 0,
            "and a weapon index the game does not have has none either");

  // Every gate refuses and names the first term that would have, so "the rate is 100" is told apart from "the panel is
  // open".
  CHECK_MSG(firerate_gate(true, false, false, false, 200, 3, true), "the open gate passes");
  CHECK_MSG(!firerate_gate(false, false, false, false, 200, 3, true), "no player struct refuses");
  CHECK_MSG(!firerate_gate(true, true, false, false, 200, 3, true), "the game's menu refuses");
  CHECK_MSG(!firerate_gate(true, false, true, false, 200, 3, true), "a pause refuses");
  CHECK_MSG(!firerate_gate(true, false, false, true, 200, 3, true), "our own panel refuses");
  CHECK_MSG(!firerate_gate(true, false, false, false, 100, 3, true), "100% refuses");
  CHECK_MSG(!firerate_gate(true, false, false, false, 124, 3, true),
            "and so does a rate that snaps to 100");
  CHECK_MSG(!firerate_gate(true, false, false, false, 200, 0, true), "a resting counter refuses");
  CHECK_MSG(!firerate_gate(true, false, false, false, 200, 3, false), "a weapon with no table refuses");
  CHECK_MSG(strcmp(firerate_gate_reason(true, false, false, false, 200, 3, true), "on") == 0,
            "the reason agrees with the gate");
  CHECK_MSG(strcmp(firerate_gate_reason(true, true, false, false, 200, 3, true), "menu") == 0,
            "and names the menu");
  CHECK_MSG(strcmp(firerate_gate_reason(true, false, false, false, 100, 3, true), "off") == 0,
            "and names the identity rate");
  CHECK_MSG(strcmp(firerate_gate_reason(true, false, false, false, 200, 0, true), "rest") == 0,
            "and names a counter at rest");
  CHECK_MSG(wswitch_gate(true, false, false, false, 200, -3), "the switch gate passes mid-run");
  CHECK_MSG(!wswitch_gate(true, false, false, false, 200, 0), "a settled weapon refuses");
  CHECK_MSG(!wswitch_gate(true, false, false, false, 200, -9), "and so does the flip");
  CHECK_MSG(!wswitch_gate(true, false, false, false, 100, -3), "and so does 100%");

  CHECK_MSG(EK_PS_KICKBACK == 597 && EK_PS_WEAPON_POS == 633,
            "kickback_pic at 597 and weapon_pos at 633 (player.h:241, :257)");
  CHECK_MSG(EK_PS_KICKBACK < EK_PS_SIZE && EK_PS_WEAPON_POS < EK_PS_SIZE,
            "and both lie inside the 640-byte struct");
}

// The ESP health label's number run: "cur/max", bare "cur", or the state label outright, never reaching for hp <= 0.
static void tb_0038_esp_health_label() {
  char buf[ESP_HEALTH_NUM_BUFSIZE];

  CHECK_MSG(esp_health_number(buf, 40, 0, 0) && strcmp(buf, "40") == 0,
            "max 0 is the bare form, not 40/0");
  CHECK_MSG(esp_health_number(buf, 40, 100, 0) && strcmp(buf, "40/100") == 0,
            "a known max is cur/max");
  CHECK_MSG(esp_health_number(buf, 4600, 4500, 0) && strcmp(buf, "4600/4500") == 0,
            "hp above max prints honestly, not clamped");
  CHECK_MSG(esp_health_number(buf, 0, 100, "Dying") && strcmp(buf, "Dying") == 0,
            "a state label replaces the number outright");
  CHECK_MSG(!esp_health_number(buf, 0, 100, 0), "hp 0 draws no number run");
  CHECK_MSG(!esp_health_number(buf, -5, 100, 0), "negative hp does not reach the number run");

  const EspLabelRuns r = esp_label_runs(100, 80, 40, 30);
  CHECK_MSG(r.totalW == 40 + ESP_LABEL_RUN_GAP + 30, "total width is both runs plus the gap");
  CHECK_MSG(r.numX == r.nameX + 40 + ESP_LABEL_RUN_GAP, "the number run sits after the name run");
  CHECK_MSG(esp_label_mono_y(200, 17, 12) == 200 + 17 - 12, "mono run bottom-aligns to the name run");
}

// The label's maximum comes from THIS game's GAME.CON/DEFS.CON/USER.CON strengths, never World Tour's table.
static void tb_0039_enemy_max_strength() {
  CHECK_MSG(ekm_enemy_max_strength(1680) == 30, "LIZTROOP: TROOPSTRENGTH");
  CHECK_MSG(ekm_enemy_max_strength(1744) == 30, "LIZTROOPDUCKING: TROOPSTRENGTH too");
  CHECK_MSG(ekm_enemy_max_strength(1810) == 400, "CANNON: CANNONSTRENGTH");
  CHECK_MSG(ekm_enemy_max_strength(1820) == 175, "OCTABRAIN: OCTASTRENGTH");
  CHECK_MSG(ekm_enemy_max_strength(1880) == 150, "DRONE: DRONESTRENGTH");
  CHECK_MSG(ekm_enemy_max_strength(1920) == 350, "COMMANDER: COMMANDERSTRENGTH");
  CHECK_MSG(ekm_enemy_max_strength(1975) == 500, "TANK: TANKSTRENGTH");
  CHECK_MSG(ekm_enemy_max_strength(2000) == 100, "PIGCOP: PIGCOPSTRENGTH");
  CHECK_MSG(ekm_enemy_max_strength(2120) == 100, "LIZMAN: LIZSTRENGTH");
  CHECK_MSG(ekm_enemy_max_strength(2360) == 40, "ROTATEGUN: ROTTURRETSTRENGTH");
  CHECK_MSG(ekm_enemy_max_strength(2420) == 30, "ORGANTIC: TURRETSTRENGTH");
  CHECK_MSG(ekm_enemy_max_strength(2630) == 4500, "BOSS1: BOSS1STRENGTH");
  CHECK_MSG(ekm_enemy_max_strength(2710) == 4500, "BOSS2: BOSS2STRENGTH");
  CHECK_MSG(ekm_enemy_max_strength(2760) == 4500, "BOSS3: BOSS3STRENGTH");
  CHECK_MSG(ekm_enemy_max_strength(4610) == 300, "NEWBEAST: NEWBEASTSTRENGTH");
  CHECK_MSG(ekm_enemy_max_strength(4671) == 20, "NEWBEASTHANGDEAD: TOUGH");
  CHECK_MSG(ekm_enemy_max_strength(4740) == 6000, "BOSS4: BOSS4STRENGTH");
  CHECK_MSG(ekm_enemy_max_strength(675) == 20, "EGG: TOUGH");

  // No actor line in this build's CON means no maximum (RAT, RECON, GREENSLIME, LIZMANFEEDING, the Atomic-only bosses).
  CHECK_MSG(ekm_enemy_max_strength(1267) == 0, "RAT has no actor line");
  CHECK_MSG(ekm_enemy_max_strength(1960) == 0, "RECON has no actor line");
  CHECK_MSG(ekm_enemy_max_strength(2370) == 0, "GREENSLIME has no actor line");
  CHECK_MSG(ekm_enemy_max_strength(2160) == 0, "LIZMANFEEDING has no actor line");
  CHECK_MSG(ekm_enemy_max_strength(2711) == 0, "BOSS2STAYPUT: not in this build's CON");
  CHECK_MSG(ekm_enemy_max_strength(5310) == 0, "BOSS5: not in this build's CON");
  CHECK_MSG(ekm_enemy_max_strength(9999) == 0, "an unlisted picnum has no maximum");
}

// The aim calibration marker's layout arithmetic and the gate's truth table over (aimon || silenton) x aimdebug.
static void tb_0040_aim_marker() {
  CHECK_MSG(aim_marker_span() == AIM_MARKER_HALF * 2 + 1,
            "odd span so the projected pixel is the exact centre");
  CHECK_MSG((aim_marker_span() % 2) == 1, "and it really is odd");

  CHECK_MSG(aim_marker_x(100) == 100 - AIM_MARKER_HALF, "top-left is half short of centre");
  CHECK_MSG(aim_marker_y(100) == 100 - AIM_MARKER_HALF, "same on y");

  const int c = 100;
  const int left = aim_marker_x(c);
  const int span = aim_marker_span();
  const int farEdge = aim_marker_edge_far(c);
  CHECK_MSG(farEdge + AIM_MARKER_T - 1 == left + span - 1,
            "the far outline bar's outer pixel is the span's last pixel");
  CHECK_MSG(farEdge >= left, "and its own near pixel still lands inside the span");
  // And it is the FAR side: past the centre, not the near one.
  CHECK_MSG(farEdge > c, "the far bar is past the centre, not before it");

  CHECK_MSG(aim_marker_label_y(200) == 200 - AIM_MARKER_HALF - ESP_LABEL_GAP,
            "the label sits one ESP_LABEL_GAP above the marker");

  // Acquisition runs whenever either assist asks, so the marker needs its own switch AND at least one of the two.
  CHECK_MSG(!aim_marker_on(0, 0, 0), "neither assist, no debug: nothing");
  CHECK_MSG(!aim_marker_on(0, 0, 1), "aimdebug alone draws nothing -- no acquisition ran");
  CHECK_MSG(!aim_marker_on(1, 0, 0), "aimon alone, no debug switch: nothing");
  CHECK_MSG(!aim_marker_on(0, 1, 0), "silenton alone, no debug switch: nothing");
  CHECK_MSG(aim_marker_on(1, 0, 1), "aimon and debug: draws");
  CHECK_MSG(aim_marker_on(0, 1, 1), "silenton and debug: draws");
  CHECK_MSG(aim_marker_on(1, 1, 1), "both assists and debug: draws");
  CHECK_MSG(!aim_marker_on(1, 1, 0), "both assists, no debug switch: nothing");
}

static void tb_0041_trooper_choking() {
  const unsigned CSTAT_LIVE = 257;   // blocking | hitscan-sensitive, the stock pair
  const unsigned CSTAT_DEAD = 0;     // what `cstat 0` leaves behind
  const unsigned char XREP = 32;

  // Zero health, cstat never touched: alive.
  CHECK_MSG(!ekm_target_dying(0, 0, 1680, CSTAT_LIVE, XREP),
            "a choking trooper is alive");
  // Same for every picnum that reaches troopcode on its own first tic (`cactor LIZTROOP`, GAME.CON:4721-4748).
  static const int troopers[] = { 1680, 1681, 1682, 1715, 1725, 1744 };
  for (int i = 0; i < (int)(sizeof(troopers) / sizeof(troopers[0])); i++) {
    CHECK_MSG(!ekm_target_dying(0, 0, troopers[i], CSTAT_LIVE, XREP),
              "every trooper picnum that runs troopcode chokes alive");
    CHECK_MSG(ekm_target_dying(0, 0, troopers[i], CSTAT_DEAD, XREP),
              "and every one of them is dead once its cstat is cleared");
  }
  CHECK_MSG(ekm_target_dying(0, 0, 1741, CSTAT_DEAD, XREP),
            "a sitting trooper with its cstat cleared is dead too");
  CHECK(ekm_target_dying(0, 0, 1742, CSTAT_DEAD, XREP));

  // The scope is the safety argument: these three run `strength 0` on the corpse and never clear cstat, so they present
  // the choker's pair and must read dead.
  CHECK_MSG(ekm_target_dying(0, 0, 2120, CSTAT_LIVE, XREP),
            "an Enforcer corpse at zero is dead however its cstat reads");
  CHECK_MSG(ekm_target_dying(0, 0, 1820, CSTAT_LIVE, XREP),
            "an Octabrain corpse at zero is dead however its cstat reads");
  CHECK_MSG(ekm_target_dying(0, 0, 4610, CSTAT_LIVE, XREP),
            "a Protector Drone corpse at zero is dead however its cstat reads");
  CHECK(ekm_target_dying(0, 0, 2000, CSTAT_LIVE, XREP));    // Pig Cop
  CHECK(ekm_target_dying(0, 0, 1920, CSTAT_LIVE, XREP));    // Assault Commander

  CHECK_MSG(ekm_target_dying(-1, 0, 1680, CSTAT_LIVE, XREP),
            "a trooper below zero is dead even while its cstat still reads live");
  CHECK(ekm_target_dying(-400, 0, 1680, CSTAT_LIVE, XREP));

  // Hittable is the aimbot's own predicate so the two cannot drift: an undrawn or non-hitscannable sprite is not
  // choking.
  CHECK_MSG(ekm_target_dying(0, 0, 1680, CSTAT_LIVE, 0),
            "xrepeat 0 is not a choking trooper");
  CHECK_MSG(ekm_target_dying(0, 0, 1680, 0x8101u, XREP),
            "cstat bit 15 invisible is not hittable, so not a choking trooper");
  // Bit 8 alone is hittable: `cstat 256` is not a down state (the CON's down states run `cstat 0`), so a trooper at
  // zero carrying it is choking.
  CHECK_MSG(!ekm_target_dying(0, 0, 1680, 256u, XREP),
            "bit 8 alone is hittable, so this trooper is choking, not dying");
  CHECK_MSG(ekm_target_dying(0, 0, 1680, 1u, XREP),
            "blocking without hitscan-sensitive is not hittable");

  CHECK(!ekm_target_dying(30, 0, 1680, CSTAT_LIVE, XREP));
  CHECK_MSG(!ekm_target_dying(0, EKM_PAL_FROZEN, 1680, CSTAT_DEAD, XREP),
            "frozen at zero is still a target with its cstat cleared");
  CHECK_MSG(!ekm_target_dying(0, EKM_PAL_FROZEN, 2120, CSTAT_LIVE, XREP),
            "and frozen still outranks the picnum scope");
  CHECK_MSG(!ekm_target_dying(0, EKM_PAL_FROZEN, 1680, CSTAT_LIVE, XREP),
            "a frozen trooper with a live cstat is a target either way");
}

// actor_t's layout and the rule for when the content scan has seen enough; the scan itself (actorstate.cpp) is read,
// not run.
static void tb_0042_actor_layout() {
  // The engine static-asserts 96 (actors.h:146) and each offset is a running total of the packed widths
  // (actors.h:128-145), checked as a chain so one wrong width cannot hide behind a right one.
  CHECK(EK_ACTOR_STRIDE == 96);
  CHECK(EK_ACT_T_DATA == 0);
  CHECK_MSG(EK_ACT_ACTION_ID == EK_ACT_T_DATA + 4 * 4,
            "t_data[4] is AC_ACTION_ID, four int32s into t_data");
  CHECK_MSG(EK_ACT_FLAGS == EK_ACT_T_DATA + 10 * 4, "flags follows all ten t_data");
  CHECK(EK_ACT_BPOS_X == EK_ACT_FLAGS + 4);
  CHECK(EK_ACT_BPOS_Y == EK_ACT_BPOS_X + 4);
  CHECK(EK_ACT_BPOS_Z == EK_ACT_BPOS_Y + 4);
  CHECK(EK_ACT_FLOORZ == EK_ACT_BPOS_Z + 4);
  CHECK(EK_ACT_CEILINGZ == EK_ACT_FLOORZ + 4);
  CHECK(EK_ACT_LASTV_X == EK_ACT_CEILINGZ + 4);
  CHECK(EK_ACT_LASTV_Y == EK_ACT_LASTV_X + 4);
  CHECK(EK_ACT_HTPICNUM == EK_ACT_LASTV_Y + 4);
  CHECK(EK_ACT_HTANG == EK_ACT_HTPICNUM + 2);
  CHECK_MSG(EK_ACT_HTEXTRA == EK_ACT_HTANG + 2, "htextra, the queued damage");
  CHECK(EK_ACT_HTOWNER == EK_ACT_HTEXTRA + 2);
  CHECK(EK_ACT_MOVFLAG == EK_ACT_HTOWNER + 2);
  CHECK(EK_ACT_TEMPANG == EK_ACT_MOVFLAG + 2);
  CHECK(EK_ACT_TIMETOSLEEP == EK_ACT_TEMPANG + 2);
  CHECK(EK_ACT_STAYPUT == EK_ACT_TIMETOSLEEP + 2);
  // Every named field must fit inside one record, or the scan reads its neighbour and looks fine.
  CHECK(EK_ACT_STAYPUT + 2 <= EK_ACTOR_STRIDE);

  // A free slot's bpos is never rewritten and a sprite at the origin is a sample zeroed memory satisfies for nothing.
  CHECK(ekm_actor_sample_usable(0, EK_MAXSTATUS, 1000, -2000));
  CHECK(ekm_actor_sample_usable(EK_MAXSTATUS - 1, EK_MAXSTATUS, 5, 5));
  CHECK_MSG(!ekm_actor_sample_usable(EK_MAXSTATUS, EK_MAXSTATUS, 1000, 2000),
            "a free slot is not an anchor: nothing has written its bpos");
  CHECK(!ekm_actor_sample_usable(-1, EK_MAXSTATUS, 1000, 2000));
  CHECK_MSG(!ekm_actor_sample_usable(0, EK_MAXSTATUS, 0, 2000),
            "x at the origin: zeroed memory would match it");
  CHECK_MSG(!ekm_actor_sample_usable(0, EK_MAXSTATUS, 1000, 0), "and the same on y");
  CHECK(!ekm_actor_sample_usable(0, EK_MAXSTATUS, 0, 0));

  // The vacuous case is the point of the minimum: nothing sampled establishes nothing, yet satisfies the ratio (the
  // sprite scan's first validator accepted zeroed .bss this way).
  CHECK_MSG(!ekm_actor_scan_accept(0, 0), "no samples is not a pass");
  CHECK_MSG(!ekm_actor_scan_accept(1, 1), "one sprite is not evidence");
  CHECK(!ekm_actor_scan_accept(EK_ACTOR_MIN_SAMPLES - 1, EK_ACTOR_MIN_SAMPLES - 1));
  CHECK_MSG(ekm_actor_scan_accept(EK_ACTOR_MIN_SAMPLES, EK_ACTOR_MIN_SAMPLES),
            "the floor itself, fully matched, passes");

  CHECK(ekm_actor_scan_accept(128, 128));
  CHECK(ekm_actor_scan_accept(128, 96));       // exactly three quarters
  CHECK(!ekm_actor_scan_accept(128, 95));
  CHECK(!ekm_actor_scan_accept(128, 64));      // half agreement is not the array
  CHECK(!ekm_actor_scan_accept(128, 0));
  // A count that cannot have come from this many samples is a caller bug, not a stronger result.
  CHECK_MSG(!ekm_actor_scan_accept(128, 129), "more matches than samples is refused");
  CHECK(!ekm_actor_scan_accept(128, -1));

  CHECK(EK_ACTOR_SAMPLES >= EK_ACTOR_MIN_SAMPLES);
}

// The sitting trooper's queued damage: two CON blocks without `ifhitweapon` pile hits into actor[].htextra for up to 24
// (30) tics while sprite.extra reads full health, so the bot keeps firing into a dead enemy.
static void tb_0043_deferred_damage() {
  // The picnum list is the rule's whole scope: only these two gate `cactor LIZTROOP` on an ifcount.
  CHECK(ekm_actor_defers_damage(EK_PICNUM_TROOP_ON_TOILET));
  CHECK(ekm_actor_defers_damage(EK_PICNUM_TROOP_JUST_SIT));
  CHECK(EK_PICNUM_TROOP_ON_TOILET == 1741);
  CHECK(EK_PICNUM_TROOP_JUST_SIT == 1742);
  CHECK_MSG(!ekm_actor_defers_damage(1680), "an ordinary trooper does not defer");
  CHECK_MSG(!ekm_actor_defers_damage(1744), "nor a ducking one: it cactors at once");
  CHECK(!ekm_actor_defers_damage(1725));    // jetpack
  CHECK(!ekm_actor_defers_damage(1715));    // shoot
  CHECK(!ekm_actor_defers_damage(2120));    // Enforcer
  CHECK(!ekm_actor_defers_damage(0));

  // Nothing pending is -1: A_IncurDamage bails on htextra < 0 and resets to -1 after applying (actors.cpp:1305-1307,
  // :1322-1327).
  CHECK(EK_HTEXTRA_NONE == -1);
  CHECK_MSG(ekm_actor_effective_extra(1741, 30, EK_HTEXTRA_NONE) == 30,
            "nothing pending leaves full health");
  CHECK_MSG(ekm_actor_effective_extra(1741, 30, 0) == 30,
            "a pending hit for zero damage takes nothing off");
  CHECK(ekm_actor_effective_extra(1741, 30, -400) == 30);

  // The pile is already lethal and the sprite has not noticed.
  CHECK(ekm_actor_effective_extra(1741, 30, 10) == 20);
  CHECK_MSG(ekm_actor_effective_extra(1741, 30, 30) == 0, "the exact kill, queued");
  CHECK_MSG(ekm_actor_effective_extra(1741, 30, 45) == 0, "overkill floors at zero");
  CHECK_MSG(ekm_actor_effective_extra(1742, 30, 32767) == 0,
            "INT16_MAX, which the kill-everything cheat writes, floors too");
  CHECK(ekm_actor_effective_extra(1742, 30, 29) == 1);

  CHECK_MSG(ekm_actor_effective_extra(1680, 30, 45) == 30,
            "an ordinary trooper's queued damage is not discounted");
  CHECK(ekm_actor_effective_extra(2120, 100, 100) == 100);
  CHECK(ekm_actor_effective_extra(1744, 30, 30) == 30);

  // End to end: a sitting trooper with a lethal pile reads as dying; one with nothing queued is a live target.
  const unsigned CS = 257;
  const unsigned char XR = 32;
  CHECK_MSG(ekm_target_dying(ekm_actor_effective_extra(1741, 30, 45), 0, 1741, CS, XR),
            "a sitting trooper with lethal damage queued is not a target");
  CHECK_MSG(!ekm_target_dying(ekm_actor_effective_extra(1741, 30, EK_HTEXTRA_NONE),
                              0, 1741, CS, XR),
            "and one with nothing queued still is");
  // The choking rule must not rescue it: 1741/1742 carry the stock hittable cstat, but they cannot be choking since
  // ATROOPSUFFERING is reached only from troopcode, after `cactor LIZTROOP` has changed the picnum to 1680.
  CHECK_MSG(ekm_target_dying(0, 0, 1741, CS, XR),
            "a sitting trooper at zero health is dead, not choking");
  CHECK(ekm_target_dying(0, 0, 1742, CS, XR));
  CHECK_MSG(!ekm_target_dying(0, 0, 1680, CS, XR),
            "while an ordinary trooper at zero and hittable is still choking");
}

static EkAction mk(int sf, int nf, int vt, int iv, int dl) {
  EkAction a;
  a.startframe = sf; a.numframes = nf; a.viewtype = vt; a.incval = iv; a.delay = dl;
  a.readable = 1;
  a.plausible = ekm_action_plausible(sf, nf, vt) ? 1 : 0;
  return a;
}
static EkAction mk_unreadable() {
  EkAction a = mk(54, 0, 0, 0, 0);
  a.readable = 0;
  return a;
}

static void tb_0044_action_record() {
  // One `action` compiles to CON_ACTION + ACTION_PARAM_COUNT params + CON_END and the label id points at the first
  // param.
  CHECK(EK_ACTION_PARAMS == 6);
  CHECK_MSG(EK_ACTION_STRIDE == 1 + EK_ACTION_PARAMS + 1,
            "CON_ACTION, six params, CON_END");
  CHECK_MSG(EK_ACTION_UNIT == 8, "apScript is intptr_t* and this build is x64");

  // Index 0 is no opinion: LIZTROOPONTOILET declares no action, so its t_data[4] is zero like every actionless actor's.
  CHECK_MSG(!ekm_action_id_usable(0), "index 0 is no opinion, never record 0");
  CHECK(!ekm_action_id_usable(1));
  CHECK(!ekm_action_id_usable(EK_ACTION_STRIDE - 1));
  CHECK_MSG(!ekm_action_id_usable(-8), "and a negative id is not a read we may make");
  CHECK_MSG(ekm_action_id_usable(EK_ACTION_STRIDE),
            "the first id with a predecessor inside the script is usable");
  CHECK(ekm_action_id_usable(4000));

  const EkAction dying     = mk(50, 5, 1, 1, 16);
  const EkAction dead      = mk(54, 0, 0, 0, 0);
  const EkAction playdead  = mk(54, 0, 0, 0, 0);
  const EkAction sufferdd  = mk(58, 2, 1, -4, 24);
  const EkAction suffering = mk(59, 2, 1, 1, 21);

  CHECK(ekm_action_is_suffering(suffering));
  CHECK(!ekm_action_is_suffering(sufferdd));
  CHECK(!ekm_action_is_suffering(dead));

  // Dead and PLAYDEAD are indistinguishable by content; the whole rule exists to work around that.
  CHECK_MSG(ekm_action_is(dead, 54, 0, 0, 0, 0) && ekm_action_is(playdead, 54, 0, 0, 0, 0),
            "DEAD and PLAYDEAD have identical record contents");

  // Position decides: PLAYDEAD's predecessor is DEAD and its successor SUFFERDEAD; DEAD's predecessor is DYING.
  CHECK_MSG(ekm_action_is_playdead(playdead, dead, sufferdd),
            "DEAD before it and SUFFERDEAD after it: playing dead");
  CHECK_MSG(!ekm_action_is_playdead(dead, dying, playdead),
            "a real corpse has DYING before it, not a down record");
  CHECK(!ekm_action_is_playdead(playdead, dying, sufferdd));
  CHECK(!ekm_action_is_playdead(playdead, dead, suffering));
  CHECK(!ekm_action_is_playdead(playdead, dead, dying));
  CHECK(!ekm_action_is_playdead(suffering, dead, sufferdd));

  // An unreadable record is never a match.
  CHECK(!ekm_action_is_playdead(mk_unreadable(), dead, sufferdd));
  CHECK(!ekm_action_is_playdead(playdead, mk_unreadable(), sufferdd));
  CHECK(!ekm_action_is_playdead(playdead, dead, mk_unreadable()));
  CHECK(!ekm_action_is(mk_unreadable(), 54, 0, 0, 0, 0));

  CHECK_MSG(!playdead.plausible, "PLAYDEAD's record is deliberately implausible");
  CHECK_MSG(!dead.plausible, "and so is a real corpse's -- same contents");
  CHECK_MSG(ekm_action_is_playdead(playdead, dead, sufferdd),
            "and the rule holds anyway, because plausibility is not part of it");

  CHECK(suffering.plausible);           // (59, 2, 1)
  CHECK(sufferdd.plausible);            // (58, 2, 1)
  CHECK(dying.plausible);               // (50, 5, 1)
  CHECK_MSG(!ekm_action_plausible(0, 0, 0), "all zeros is not evidence a read landed");
  CHECK_MSG(!ekm_action_plausible(0, 1, 0), "viewtype 0 is the compiler's padding");
  CHECK_MSG(!ekm_action_plausible(0, 0, 1), "numframes 0 is padding too");
  CHECK_MSG(ekm_action_plausible(0, 4, 5), "viewtype 5 is the most common in this CON");
  CHECK_MSG(ekm_action_plausible(0, 1, 4),
            "viewtype 4 IS used here -- the World Tour gate rejects it");
  CHECK(ekm_action_plausible(0, 1, 7));
  CHECK_MSG(!ekm_action_plausible(0, 1, 3),
            "and 3 is not, though the fork's gate admits it");
  CHECK(!ekm_action_plausible(0, 1, 8));
  CHECK(!ekm_action_plausible(0, 65, 1));
  CHECK(!ekm_action_plausible(1024, 1, 1));
  CHECK(!ekm_action_plausible(-65, 1, 1));
  CHECK_MSG(ekm_action_plausible(-10, 1, 5), "this CON's most negative startframe");
  CHECK_MSG(ekm_action_plausible(395, 1, 5), "and its largest");

  const unsigned CS_LIVE = 257, CS_DEAD = 0;
  const unsigned char XR = 32;

  CHECK(ekm_actor_state(1680, 30, 0, CS_LIVE, XR, false) == EKA_ALIVE);
  CHECK_MSG(ekm_actor_state(1680, 0, 0, CS_LIVE, XR, false) == EKA_CHOKING,
            "zero health, cstat untouched, and a trooper: choking");
  CHECK_MSG(ekm_actor_state(1680, 0, 0, CS_DEAD, XR, false) == EKA_DOWN,
            "cstat cleared and not playing dead: a corpse");
  CHECK_MSG(ekm_actor_state(1680, 0, 0, CS_DEAD, XR, true) == EKA_PLAYDEAD,
            "the same sprite fields, and only the record says otherwise");
  // PLAYDEAD outranks every sprite-only test, since every field a sprite carries says corpse.
  CHECK(ekm_actor_state(1680, -50, 0, CS_DEAD, XR, true) == EKA_PLAYDEAD);
  CHECK(ekm_actor_state(2120, 0, 0, CS_LIVE, XR, false) == EKA_DOWN);   // Enforcer corpse
  CHECK(ekm_actor_state(1680, 0, EKM_PAL_FROZEN, CS_DEAD, XR, false) == EKA_ALIVE);

  // A frozen trooper keeps its cstat: GAME.CON's checktroophit freeze branch (:4373-4381) runs `spritepal 1`, `action
  // ATROOPFROZEN`, `strength 0` and never `cstat 0`, so it presents the choker's exact pair (picnum 1680, extra 0,
  // cstat 257).
  CHECK_MSG(ekm_actor_state(1680, 0, EKM_PAL_FROZEN, CS_LIVE, XR, false) == EKA_ALIVE,
            "a frozen trooper with its cstat intact is frozen, not choking");
  CHECK_MSG(ekm_actor_state(1741, 0, EKM_PAL_FROZEN, CS_LIVE, XR, false) == EKA_ALIVE,
            "and so is a frozen sitter, which the deferred-damage rule also excludes");

  CHECK(ekm_actor_state_shown(EKA_ALIVE));
  CHECK(ekm_actor_state_shown(EKA_CHOKING));
  CHECK_MSG(ekm_actor_state_shown(EKA_PLAYDEAD), "a play-dead trooper is SHOWN");
  CHECK_MSG(!ekm_actor_state_shown(EKA_DOWN), "and a corpse is not");
  CHECK(ekm_actor_state_targetable(EKA_ALIVE));
  CHECK_MSG(ekm_actor_state_targetable(EKA_CHOKING), "a choking trooper IS worth shooting");
  CHECK_MSG(!ekm_actor_state_targetable(EKA_PLAYDEAD),
            "a play-dead one is not: its cstat is clear and a bullet does nothing");
  CHECK(!ekm_actor_state_targetable(EKA_DOWN));

  CHECK(strcmp(ekm_actor_state_label(EKA_CHOKING), "Choking") == 0);
  CHECK(strcmp(ekm_actor_state_label(EKA_PLAYDEAD), "Playing dead") == 0);
  CHECK_MSG(ekm_actor_state_label(EKA_ALIVE) == 0, "a live enemy prints its health");
  CHECK(ekm_actor_state_label(EKA_DOWN) == 0);
  CHECK(strlen(ekm_actor_state_label(EKA_PLAYDEAD)) < ESP_HEALTH_NUM_BUFSIZE);

  char buf[ESP_HEALTH_NUM_BUFSIZE];
  CHECK_MSG(esp_health_number(buf, 0, 30, ekm_actor_state_label(EKA_PLAYDEAD)),
            "a state label draws where a zero-health number would not");
  CHECK(strcmp(buf, "Playing dead") == 0);
  CHECK(esp_health_number(buf, 0, 30, ekm_actor_state_label(EKA_CHOKING)));
  CHECK(strcmp(buf, "Choking") == 0);
  CHECK_MSG(!esp_health_number(buf, 0, 30, ekm_actor_state_label(EKA_DOWN)),
            "and a corpse still prints nothing");
}

// Cover rising from the floor: samples at or above `hiddenBelow` of the body height answer yes, the rest are blocked.
static void span_cover(unsigned char* v, float hiddenBelow) {
  for (int i = 0; i < EKM_SPAN_SAMPLES; i++)
    v[i] = (unsigned char)(ekm_span_frac(i) >= hiddenBelow ? 1 : 0);
}

static void tb_0045_visible_span() {
  // Aim at the middle of what is visible, not the first point that answers.
  const int feet = 10000;  const float h = 4096.0f;
  unsigned char vis[EKM_SPAN_SAMPLES];

  // The span is not 0..1 and is centred on the body centre: with nothing hidden the answer is still the centre, with
  // all but the head hidden still the head rung.
  CHECK(EKM_SPAN_SAMPLES == 9);
  CHECK(approx(ekm_span_frac(0), 0.125f, 0.0001f));
  CHECK(approx(ekm_span_frac(4), 0.5f, 0.0001f));
  CHECK(approx(ekm_span_frac(8), 0.875f, 0.0001f));
  for (int i = 1; i < EKM_SPAN_SAMPLES; i++)
    CHECK(ekm_span_frac(i) > ekm_span_frac(i - 1));
  // The span's ends are two rungs of the existing ladder, so refinement never aims outside what the first stage
  // accepted.
  CHECK_MSG(ekm_span_z(feet, h, 4) == ekm_aim_z(feet, h, 0, 0),
            "the middle sample is the Centre rung exactly");
  CHECK_MSG(ekm_span_z(feet, h, 8) == ekm_aim_z(feet, h, 1, 0),
            "the top sample is the head rung exactly");
  for (int i = 0; i < EKM_SPAN_SAMPLES; i++) {
    const int z = ekm_span_z(feet, h, i);
    CHECK(z <= feet && z >= feet - (int)h);
  }

  // The configured row's height rounded to the nearest sample: Centre 0.5, Upper 0.75, the fractions ekm_aim_z rung 0
  // uses.
  CHECK(ekm_span_prefer(0) == 4);
  CHECK(ekm_span_prefer(1) == 7);

  // Four cases, cover rising from below: 1 nothing hidden gives the body centre, as the ladder does today.
  span_cover(vis, 0.0f);
  int first = -1, last = -1;
  int mid = ekm_span_mid(vis, EKM_SPAN_SAMPLES, ekm_span_prefer(0), &first, &last);
  CHECK(mid == 4 && first == 0 && last == 8);
  CHECK_MSG(ekm_span_z(feet, h, mid) == ekm_aim_z(feet, h, 0, 0),
            "case 1 must not disagree with today's answer");
  mid = ekm_span_mid(vis, EKM_SPAN_SAMPLES, ekm_span_prefer(1), &first, &last);
  CHECK_MSG(mid == ekm_span_prefer(1) && first == 0 && last == 8,
            "a fully visible enemy is aimed at the Head row's own sample");
  // Exactly 0.75 of the body, not the grid's nearest 0.78125: "as before" is an equality.
  CHECK_MSG(ekm_span_z(feet, h, mid, 1) == ekm_aim_z(feet, h, 0, 1),
            "Head, unobstructed: stage 2 lands on stage 1's own z");
  CHECK(approx(ekm_span_frac(mid, 1), 0.75f, 0.0001f));
  CHECK(approx(ekm_span_frac(ekm_span_prefer(0), 0), 0.5f, 0.0001f));
  for (int pt = 0; pt < 2; pt++)
    for (int i = 1; i < EKM_SPAN_SAMPLES; i++)
      CHECK(ekm_span_frac(i, pt) > ekm_span_frac(i - 1, pt));
  span_cover(vis, 0.40f);
  mid = ekm_span_mid(vis, EKM_SPAN_SAMPLES, ekm_span_prefer(0), &first, &last);
  CHECK(mid == ekm_span_prefer(0) && first == 3 && last == 8);
  CHECK(approx(ekm_span_frac(mid), 0.5f, 0.001f));
  span_cover(vis, 0.55f);
  mid = ekm_span_mid(vis, EKM_SPAN_SAMPLES, ekm_span_prefer(0), &first, &last);
  CHECK(mid == 7 && first == 5 && last == 8);
  CHECK(approx(ekm_span_frac(mid), 0.75f, 0.05f));
  CHECK_MSG(ekm_span_z(feet, h, mid) != ekm_aim_z(feet, h, 1, 0),
            "case 3 is the defect: the head rung is no longer the answer");
  span_cover(vis, 0.80f);
  mid = ekm_span_mid(vis, EKM_SPAN_SAMPLES, ekm_span_prefer(0), &first, &last);
  CHECK(mid == 8 && first == 8 && last == 8);
  CHECK_MSG(ekm_span_z(feet, h, mid) == ekm_aim_z(feet, h, 1, 0),
            "case 4 must not disagree with today's answer either");

  // The longest run, not the centroid: a railing or half-open door leaves head and legs visible with the chest blocked,
  // and the centroid lands on the one part the engine cannot see.
  {
    const unsigned char rail[EKM_SPAN_SAMPLES] = { 1, 1, 1, 0, 0, 0, 0, 1, 1 };
    int sum = 0, n = 0;
    for (int i = 0; i < EKM_SPAN_SAMPLES; i++) if (rail[i]) { sum += i; n++; }
    const int centroid = (sum + n / 2) / n;
    CHECK_MSG(rail[centroid] == 0, "the centroid lands on a BLOCKED sample");
    mid = ekm_span_mid(rail, EKM_SPAN_SAMPLES, ekm_span_prefer(0), &first, &last);
    CHECK(mid == 1 && first == 0 && last == 2);
    CHECK_MSG(rail[mid] != 0, "the longest run's midpoint is one the engine sees");
  }

  {
    unsigned char one[EKM_SPAN_SAMPLES] = { 0, 0, 1, 0, 0, 0, 0, 0, 0 };
    CHECK(ekm_span_mid(one, EKM_SPAN_SAMPLES, ekm_span_prefer(0), &first, &last) == 2);
    CHECK(first == 2 && last == 2);
    unsigned char none[EKM_SPAN_SAMPLES] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    CHECK_MSG(ekm_span_mid(none, EKM_SPAN_SAMPLES, ekm_span_prefer(0)) < 0,
              "nothing visible refines nothing");
    CHECK(ekm_span_mid(none, 0, ekm_span_prefer(0)) < 0);
    CHECK(ekm_span_mid(0, EKM_SPAN_SAMPLES, ekm_span_prefer(0)) < 0);
  }

  // The midpoint is the fallback, only when the row's point is gone: both masks block the configured sample.
  {
    // A longer run elsewhere wins, by its midpoint, not the nearer short one.
    const unsigned char above[EKM_SPAN_SAMPLES] = { 1, 1, 0, 0, 0, 1, 1, 1, 1 };
    CHECK(ekm_span_mid(above, EKM_SPAN_SAMPLES, ekm_span_prefer(0), &first, &last) == 7);
    CHECK(first == 5 && last == 8);
    const unsigned char under[EKM_SPAN_SAMPLES] = { 1, 1, 1, 1, 0, 1, 1, 0, 0 };
    CHECK(ekm_span_mid(under, EKM_SPAN_SAMPLES, ekm_span_prefer(0), &first, &last) == 2);
    CHECK(first == 0 && last == 3);
  }

  {
    const unsigned char two[EKM_SPAN_SAMPLES] = { 1, 1, 0, 0, 0, 0, 0, 1, 1 };
    CHECK(ekm_span_mid(two, EKM_SPAN_SAMPLES, ekm_span_prefer(0)) == 1);
    CHECK_MSG(ekm_span_mid(two, EKM_SPAN_SAMPLES, ekm_span_prefer(1)) == ekm_span_prefer(1),
              "Head's own sample is visible here, so it is the answer");
  }

  // Whatever the mask, the answer is a sample the engine said it could see.
  for (int m = 1; m < 512; m += 8) {
    for (int i = 0; i < EKM_SPAN_SAMPLES; i++)
      vis[i] = (unsigned char)((m >> i) & 1);
    for (int pt = 0; pt < 2; pt++) {
      first = -1; last = -1;
      const int p = ekm_span_prefer(pt);
      const int s = ekm_span_mid(vis, EKM_SPAN_SAMPLES, p, &first, &last);
      CHECK(s >= 0 && s < EKM_SPAN_SAMPLES && vis[s] != 0);
      CHECK(first <= s && s <= last && vis[first] != 0 && vis[last] != 0);
      // The row's own point is never passed over when it is inside the winning run.
      if (p >= first && p <= last) {
        CHECK_MSG(s == p, "the configured point wins inside the winning run");
      }
    }
  }
}

// Whether the winning run is the WHOLE body decides the log's rate limiting, so it is a rule, not a comparison at the
// call site.
static void tb_0046_span_is_full() {
  // A partial span (an enemy behind a bookshelf) is the case never yet seen.
  CHECK(ekm_span_is_full(0, EKM_SPAN_SAMPLES - 1, EKM_SPAN_SAMPLES));
  // Nothing visible is partial, not full: ekm_span_mid reports -1/-1 there.
  CHECK(!ekm_span_is_full(-1, -1, EKM_SPAN_SAMPLES));
  CHECK(!ekm_span_is_full(0, EKM_SPAN_SAMPLES - 2, EKM_SPAN_SAMPLES));
  CHECK(!ekm_span_is_full(1, EKM_SPAN_SAMPLES - 1, EKM_SPAN_SAMPLES));
  CHECK(!ekm_span_is_full(0, 0, EKM_SPAN_SAMPLES));
  CHECK(!ekm_span_is_full(0, -1, 0));

  // And through the rule that produces those two numbers.
  unsigned char vis[EKM_SPAN_SAMPLES];
  int first = -1, last = -1;
  span_cover(vis, 0.0f);
  ekm_span_mid(vis, EKM_SPAN_SAMPLES, ekm_span_prefer(0), &first, &last);
  CHECK(ekm_span_is_full(first, last, EKM_SPAN_SAMPLES));
  span_cover(vis, 0.40f);
  ekm_span_mid(vis, EKM_SPAN_SAMPLES, ekm_span_prefer(0), &first, &last);
  CHECK_MSG(!ekm_span_is_full(first, last, EKM_SPAN_SAMPLES),
            "cover over the lower 40 % is a partial span");
}

// hitdata_t's FIELD ORDER is not the intuitive one: build.h:1205-1209 declares `int16_t sprite, wall, sect;` after the
// x/y/z union (sprite 12, wall 14, sect 16).
static void tb_0047_hitdata_layout() {
  CHECK(EK_HIT_X == 0);
  CHECK(EK_HIT_Y == 4);
  CHECK(EK_HIT_Z == 8);
  CHECK_MSG(EK_HIT_SPRITE == 12, "hitdata_t.sprite is the FIRST of the three int16s");
  CHECK_MSG(EK_HIT_WALL == 14, "hitdata_t.wall is the second");
  CHECK_MSG(EK_HIT_SECT == 16, "hitdata_t.sect is the LAST, not the first");
  CHECK_MSG((int)sizeof(EkHitData) == 20, "12 bytes of x/y/z, three int16s, padded to 4");
  CHECK((int)offsetof(EkHitData, sprite) == EK_HIT_SPRITE);
  CHECK((int)offsetof(EkHitData, wall) == EK_HIT_WALL);
  CHECK((int)offsetof(EkHitData, sect) == EK_HIT_SECT);
  // build.h:226. What every one of the engine's own shot hitscans passes.
  CHECK_MSG(EK_CLIPMASK1 == (256 << 16) + 64, "CLIPMASK1, build.h:226");
}

// The shot's own ray. sintable is REPRODUCED rather than located (shotray.h), so it is pinned against the engine's
// initialiser, engine.cpp:8661-8666.
static void tb_0048_shot_ray() {
  // The four quarter points, exact in the engine's arithmetic.
  CHECK(ekm_sintable(0) == 0);
  CHECK_MSG(ekm_sintable(512) == 16384, "the table's amplitude is 16384, not 16383");
  CHECK(ekm_sintable(1024) == 0);
  CHECK(ekm_sintable(1536) == -16384);
  CHECK(ekm_sintable(256) == 11585);
  for (int i = 0; i <= 512; i += 16) {
    CHECK(ekm_sintable(1024 - i) == ekm_sintable(i));
    CHECK(ekm_sintable(i + 1024) == -ekm_sintable(i));
  }
  CHECK(ekm_sintable(2048) == ekm_sintable(0));
  CHECK(ekm_sintable(-512) == ekm_sintable(1536));

  // fix16_to_int (fix16.h:42) rounds AWAY FROM ZERO at the half, not a shift; the shot angle and zvel both use it.
  CHECK(ekm_fix16_to_int(ekm_f16(3)) == 3);
  CHECK(ekm_fix16_to_int(ekm_f16(3) + 32768) == 4);
  CHECK(ekm_fix16_to_int(-ekm_f16(3) - 32768) == -4);

  // A level view fires a level ray: player.cpp:498 takes zvel from F16(100) - q16horiz - q16horizoff, so horiz 100 has
  // no vertical component.
  const EkShotRay level = ekm_shot_ray(ekm_f16(0), ekm_f16(EKM_HORIZ_LEVEL), 0);
  CHECK(level.vz == 0);
  CHECK_MSG(level.vx == 16384 && level.vy == 0,
            "angle 0 fires along +x: cos at (ang+512), sin at ang");
  const EkShotRay east = ekm_shot_ray(ekm_f16(512), ekm_f16(EKM_HORIZ_LEVEL), 0);
  CHECK_MSG(east.vx == 0 && east.vy == 16384, "angle 512 fires along +y");

  const EkShotRay down = ekm_shot_ray_at(1024, 0, 512, 1024);
  CHECK_MSG(down.vz > 0, "aiming at a point below fires downward");
  const EkShotRay up = ekm_shot_ray_at(1024, 0, -512, 1024);
  CHECK(up.vz < 0);

  // The ray reaches the point: hitscan advances x by vx*dz/vz (clip.cpp:2664-2670), so vz/|vxy| must be dz/dist2d.
  for (int dz = -2048; dz <= 2048; dz += 256) {
    const int dist = 2048;
    const EkShotRay r = ekm_shot_ray_at(dist, 0, dz, dist);
    const double want = (double)dz / (double)dist;
    const double got = (double)r.vz / 16384.0;
    CHECK_MSG(fabs(got - want) < 0.02, "the ray's slope is dz over dist2d");
  }
}

// The triggerbot's ray gate: a shot landing on something not a targetable enemy is not worth taking, and only the ray
// can tell.
static void tb_0049_triggerbot_ray() {
  TbotCond lock = { 1, "lock" };
  TbotCond none = { 0, "nolock" };
  TbotCond off  = { 0, "offyaw" };

  CHECK(tbot_ray_cond(lock, TBOT_RAY_UNKNOWN).holds == 1);
  CHECK(strcmp(tbot_ray_cond(lock, TBOT_RAY_UNKNOWN).reason, "lock") == 0);

  CHECK(tbot_ray_cond(lock, TBOT_RAY_ENEMY).holds == 1);
  CHECK(strcmp(tbot_ray_cond(lock, TBOT_RAY_ENEMY).reason, "lock") == 0);

  CHECK_MSG(tbot_ray_cond(lock, TBOT_RAY_BLOCKED).holds == 0,
            "a blocked ray holds fire even with a lock");
  CHECK(strcmp(tbot_ray_cond(lock, TBOT_RAY_BLOCKED).reason, "blocked") == 0);

  CHECK_MSG(tbot_ray_cond(lock, TBOT_RAY_BREAK).holds == 1,
            "breakable cover is the first bullet of the shot, not a blocked one");
  CHECK(strcmp(tbot_ray_cond(lock, TBOT_RAY_BREAK).reason, "breaking") == 0);

  // It may only widen a refusal into a hold, never narrow one into another.
  for (int ray = TBOT_RAY_UNKNOWN; ray <= TBOT_RAY_BLOCKED; ray++) {
    CHECK(tbot_ray_cond(none, ray).holds == 0);
    CHECK(strcmp(tbot_ray_cond(none, ray).reason, "nolock") == 0);
    CHECK(tbot_ray_cond(off, ray).holds == 0);
    CHECK(strcmp(tbot_ray_cond(off, ray).reason, "offyaw") == 0);
  }

  CHECK_MSG(ekm_picnum_breaks_open(595), "GRATE1, the vent grate");
  CHECK_MSG(ekm_picnum_breaks_open(407), "FANSPRITE");
  CHECK(ekm_picnum_breaks_open(342) && ekm_picnum_breaks_open(343));
  CHECK(ekm_picnum_breaks_open(4099));
  CHECK(ekm_picnum_breaks_open(1113));
  CHECK(ekm_picnum_breaks_open(569) && ekm_picnum_breaks_open(571));
  CHECK_MSG(!ekm_picnum_breaks_open(596), "BGRATE1 is the ALREADY broken grate");
  CHECK_MSG(!ekm_picnum_breaks_open(1680), "an Assault Trooper is not cover");
  CHECK_MSG(!ekm_picnum_breaks_open(0), "picnum 0 is not a special case");
  // TREE1 and CACTUS clear the same bits only for a named damage source, so a bullet does not open them.
  CHECK_MSG(!ekm_picnum_breaks_open(911), "CACTUS needs an explosive");
}

// The second stage's sample test: `hit.sprite == the target` IS the question, and every near miss yields a plausible aim point.
static void tb_0050_span_sample_hit() {
  CHECK_MSG(!ekm_span_sample_hit(false, 7, 7),
            "a cast with no answer is not a visible sample");
  CHECK(ekm_span_sample_hit(true, 7, 7));
  CHECK_MSG(!ekm_span_sample_hit(true, 8, 7),
            "another actor in the way is not the target");
  CHECK_MSG(!ekm_span_sample_hit(true, -1, 7),
            "a wall, floor or ceiling hit leaves hit.sprite at -1");
  CHECK_MSG(!ekm_span_sample_hit(true, -1, -1),
            "-1 == -1 is a wall hit against no target, not a match");
  CHECK_MSG(ekm_span_sample_hit(true, 0, 0), "sprite 0 is a real sprite index");

  CHECK_MSG(EKM_SPAN_BUDGET >= EKM_SPAN_SAMPLES,
            "stage 2's budget must cover all nine samples or none");
}

// The wall array's layout. wall[] is what the triggerbot could not read, and a pane of glass is a WALL.
static void tb_0051_wall_layout() {
  CHECK_MSG(EK_WALL_STRIDE == 32, "walltypev7 is 32 bytes (buildinf.txt:294)");
  CHECK(EK_WALL_X == 0 && EK_WALL_Y == 4);
  CHECK_MSG(EK_WALL_POINT2 == 8, "point2 is the first int16, after the x/y union");
  CHECK(EK_WALL_NEXTWALL == 10);
  CHECK(EK_WALL_NEXTSECTOR == 12);
  CHECK_MSG(EK_WALL_CSTAT == 14, "cstat sits BETWEEN nextsector and picnum");
  CHECK_MSG(EK_WALL_PICNUM == 16, "picnum, and overpicnum immediately after it");
  CHECK(EK_WALL_OVERPICNUM == 18);

  // MAXWALLS + M32_FIXME_WALLS (build.h:60, :70) is the SIZE OF wall_s, which enginePreInit points wall at
  // (engine.cpp:9155, :9175).
  CHECK(EK_MAXWALLS == 16384);
  CHECK_MSG(EK_WALL_SLOTS == 16896, "the array is MAXWALLS plus M32_FIXME_WALLS");
  CHECK_MSG(EK_WALL_BYTES == 16896 * 32, "540,672 bytes must fit in one region");

  CHECK_MSG(!ekm_wall_record_plausible(0, 0, 0, 0, 0, 0),
            "wall 0 pointing at itself is zeroed memory, not a map");
  CHECK_MSG(ekm_wall_record_plausible(5, 0, 0, 0, 0, 0),
            "away from index 0 zeros are in range -- the pair test is what kills them");
  CHECK(ekm_wall_record_plausible(0, 1, -1, -1, 350, 0));
  CHECK_MSG(!ekm_wall_record_plausible(0, EK_WALL_SLOTS, -1, -1, 350, 0),
            "point2 past the allocation is not a wall");
  CHECK_MSG(!ekm_wall_record_plausible(0, 1, -2, -1, 350, 0),
            "nextwall is -1 or an index, never -2");
  CHECK_MSG(!ekm_wall_record_plausible(0, 1, -1, EK_MAXSECTORS, 350, 0),
            "nextsector is bounded by MAXSECTORS, unlike a sprite's sectnum");
  CHECK(!ekm_wall_record_plausible(0, 1, -1, -1, EK_MAXTILES, 0));
  CHECK(!ekm_wall_record_plausible(0, 1, -1, -1, 350, EK_MAXTILES));
  CHECK(!ekm_wall_record_plausible(0, 1, -1, -1, -1, 0));

  // The engine's WALLS_ARE_CONSISTENT (engine.cpp:1296-1297) plus the reciprocity beside it (:1373-1376).
  EkWallPair p;
  p.index = 7; p.nextwall = 40; p.nextOfNext = 7;
  p.x1 = 1024; p.y1 = -2048; p.x2 = 4096; p.y2 = -2048;
  p.kx = 4096; p.ky = -2048; p.kp2x = 1024; p.kp2y = -2048;
  CHECK(ekm_wall_pair_consistent(p));
  p.nextOfNext = 8;
  CHECK_MSG(!ekm_wall_pair_consistent(p), "a neighbour that names another wall is not ours");
  p.nextOfNext = 7; p.kx = 4097;
  CHECK_MSG(!ekm_wall_pair_consistent(p), "the neighbour must START where this wall ends");
  p.kx = 4096; p.kp2y = 0;
  CHECK_MSG(!ekm_wall_pair_consistent(p), "and END where this wall starts");
  // Zeroed memory is not killed HERE: an all-zero pair at index 0 satisfies this geometry, so what rejects zeros is the
  // plausibility test (wall 0's point2 may not be 0) and the accept rule demanding EVERY record pass.
  EkWallPair z = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  CHECK_MSG(ekm_wall_pair_consistent(z),
            "zeros agree with themselves at index 0; plausibility is what refuses them");
  z.index = 9;
  CHECK_MSG(!ekm_wall_pair_consistent(z), "zeroed wall 0 cannot name wall 9 back");

  CHECK(ekm_wall_scan_accept(EK_WALL_SCAN_DEPTH, EK_WALL_SCAN_DEPTH, EK_WALL_MIN_PAIRS));
  CHECK_MSG(!ekm_wall_scan_accept(0, 0, 0), "nothing checked is not a verdict");
  CHECK_MSG(!ekm_wall_scan_accept(EK_WALL_MIN_CHECKED - 1, EK_WALL_MIN_CHECKED - 1,
                                  EK_WALL_MIN_PAIRS),
            "too few records checked to mean anything");
  CHECK_MSG(!ekm_wall_scan_accept(EK_WALL_SCAN_DEPTH, EK_WALL_SCAN_DEPTH - 1,
                                  EK_WALL_MIN_PAIRS),
            "EVERY record, not most: one implausible record is a different array");
  CHECK_MSG(!ekm_wall_scan_accept(EK_WALL_SCAN_DEPTH, EK_WALL_SCAN_DEPTH,
                                  EK_WALL_MIN_PAIRS - 1),
            "a map with no two-sided walls confirmed is not evidence");
  CHECK(EK_WALL_MIN_CHECKED <= EK_WALL_SCAN_DEPTH);
  CHECK(EK_WALL_MIN_PAIRS > 0);
}

static void tb_0052_wall_breaks_open() {
  // GLASS 503, STAINGLASS1 510, FANSPRITE 407 (names.h:167, :169, :155).
  CHECK_MSG(ekm_wall_breaks_open(503, EK_WALL_CSTAT_MASKED, 12), "GLASS");
  CHECK_MSG(ekm_wall_breaks_open(510, EK_WALL_CSTAT_MASKED, 0), "STAINGLASS1");
  CHECK_MSG(ekm_wall_breaks_open(407, EK_WALL_CSTAT_MASKED, 3), "FANSPRITE, as a wall");

  CHECK_MSG(!ekm_wall_breaks_open(503, EK_WALL_CSTAT_MASKED, -1),
            "nextsector >= 0 is the branch's own condition");
  CHECK_MSG(!ekm_wall_breaks_open(503, 0, 12),
            "an unmasked wall is not the pane, whatever tile it draws");
  CHECK(!ekm_wall_breaks_open(503, 1 + 64, 12));

  CHECK_MSG(!ekm_wall_breaks_open(504, EK_WALL_CSTAT_MASKED, 12),
            "GLASS2 is the ALREADY broken pane, which stops nothing");
  CHECK_MSG(!ekm_wall_breaks_open(663, EK_WALL_CSTAT_MASKED, 12),
            "W_FORCEFIELD ripples and stays solid -- World Tour fired into one");
  CHECK_MSG(!ekm_wall_breaks_open(230, EK_WALL_CSTAT_MASKED, 12),
            "BIGFORCE is the same case as the forcefield");
  CHECK_MSG(!ekm_wall_breaks_open(560, EK_WALL_CSTAT_MASKED, 12),
            "MIRROR breaks for an RPG or an explosive, never for a bullet");
  CHECK_MSG(!ekm_wall_breaks_open(0, EK_WALL_CSTAT_MASKED, 12),
            "overpicnum 0 is not a special case");

  // The two lists must not be crossed: 595 is the vent grate, a SPRITE; 503 is glass, a WALL.
  CHECK_MSG(!ekm_wall_breaks_open(595, EK_WALL_CSTAT_MASKED, 12),
            "GRATE1 is a sprite case, not a wall case");
  CHECK_MSG(!ekm_picnum_breaks_open(503), "glass is a wall, not a sprite");
  CHECK(ekm_picnum_breaks_open(407) && ekm_wall_breaks_open(407, EK_WALL_CSTAT_MASKED, 1));
}

static void tb_0053_origin_sector() {
  // updatesector @ rva 0x01635A0: `mov r9d,0x100` at +0x10 is INITIALUPDATESECTORDIST (4th arg), `mov [rsp+0x20],0x600`
  // at +0x1D is MAXUPDATESECTORDIST (5th).
  const unsigned char us[EKM_US_SHAPE_WINDOW] = {
    0x41,0x57, 0x41,0x56, 0x41,0x55, 0x41,0x54, 0x55, 0x57, 0x56, 0x53,
    0x48,0x83,0xEC,0x58,
    0x41,0xB9,0x00,0x01,0x00,0x00, 0x41,0x0F,0xB7,0x00, 0x4D,0x89,0xC7,
    0xC7,0x44,0x24,0x20,0x00,0x06,0x00,0x00, 0x89,0xCD, 0x41,0x89,0xD4,
    0x4C,0x8D,0x44,0x24,0x4E, 0x66,0x89,0x44,0x24,0x4E,
    0xE8,0x77,0xFD,0xFF,0xFF, 0x44,0x0F,0xB7,0x74,0x24,0x4E, 0x66,
  };
  // updatesectorz @ rva 0x0167740: the same constants one slot higher, `mov [rsp+0x28],0x600` at +0x27 and `mov
  // [rsp+0x20],0x100` at +0x2F.
  const unsigned char usz[EKM_US_SHAPE_WINDOW] = {
    0x41,0x57, 0x41,0x56, 0x41,0x55, 0x41,0x54, 0x55, 0x57, 0x56, 0x53,
    0x48,0x83,0xEC,0x58,
    0x41,0x0F,0xB7,0x01, 0x4C,0x89,0x8C,0x24,0xB8,0x00,0x00,0x00,
    0x4C,0x8D,0x4C,0x24,0x46, 0x41,0x89,0xCC, 0x44,0x89,0xC5,
    0xC7,0x44,0x24,0x28,0x00,0x06,0x00,0x00,
    0xC7,0x44,0x24,0x20,0x00,0x01,0x00,0x00,
    0x89,0x94,0x24,0xA8,0x00,0x00,0x00, 0x66,0x89,
  };
  int same = 1;
  for (int i = 0; i < 16; i++) if (us[i] != usz[i]) same = 0;
  CHECK_MSG(same == 1,
        "the two prologues really are identical for sixteen bytes -- the "
        "fixtures are the shipped bytes, not a straw man");

  CHECK_MSG(ekm_us_shape_match(us, EKM_US_SHAPE_WINDOW),
        "updatesector: 256 into the 4th argument register, 1536 into the 5th slot");
  CHECK_MSG(!ekm_us_shape_match(usz, EKM_US_SHAPE_WINDOW),
        "updatesectorZ must NOT match -- three arguments into it is a wild write");

  CHECK_MSG(!ekm_us_shape_match(us, 0x1D),
        "a window that stops before [rsp+0x20] refuses");
  CHECK(ekm_us_shape_match(us, 0x25));
  CHECK_MSG(!ekm_us_shape_match(us, 0),   "an empty window matches nothing");
  CHECK_MSG(!ekm_us_shape_match(NULL, 64), "and neither does no window at all");

  // The constants against build.h:1362-1363.
  CHECK(EKM_US_INITIALDIST == 256 && EKM_US_MAXDIST == 1536);
  // Wrong immediates in the right slots is still a refusal.
  const unsigned char wrongImm[16] = {
    0x41,0xB9,0x00,0x02,0x00,0x00, 0xC7,0x44,0x24,0x20,0x00,0x06,0x00,0x00, 0x90,0x90
  };
  CHECK_MSG(!ekm_us_shape_match(wrongImm, 16), "512 is not INITIALUPDATESECTORDIST");

  // `call rel32` from the shipped image: Proj_DoHitscan's calls to updatesector (0x00B76D5) and hitscan (0x00B772A).
  CHECK_MSG(ekm_us_rel32_target(0x00B76D5u, 0x000ABEC6) == 0x01635A0u,
        "the call at 0x00B76D5 resolves to updatesector");
  CHECK_MSG(ekm_us_rel32_target(0x00B772Au, 0x000617C1) == 0x0118EF0u,
        "and the one five instructions later to hitscan");
  CHECK_MSG(ekm_us_rel32_target(0x00B76D5u, 0x000ABEC6 + 5) != 0x01635A0u,
        "off by the instruction's own length lands in the wrong function");
  // A backward displacement: `E8 77 FD FF FF` at +0x34 of the fixture, updatesector's call to updatesectorneighbor.
  CHECK(ekm_us_rel32_target(0x01635D4u, -0x289) == 0x0163350u);

  // No correction means the caller's sector goes through untouched, so a refused scan costs precision only.
  CHECK_MSG(ekm_us_origin_sector(false, 999, 412, EK_MAXSECTORS) == 412,
        "not armed: the caller's own sector, unchanged");
  CHECK_MSG(ekm_us_origin_sector(false, -1, 412, EK_MAXSECTORS) == 412,
        "and the corrected value is not even looked at");
  CHECK_MSG(ekm_us_origin_sector(true, 88, 412, EK_MAXSECTORS) == 88,
        "armed: the muzzle's sector wins over the eye's");
  CHECK_MSG(ekm_us_origin_sector(true, 412, 412, EK_MAXSECTORS) == 412,
        "agreement is the ordinary case and changes nothing");
  CHECK_MSG(ekm_us_origin_sector(true, -1, 412, EK_MAXSECTORS) == -1,
        "the muzzle in NO sector is a refusal, not a fallback to the eye's");
  CHECK_MSG(ekm_us_origin_sector(true, EK_MAXSECTORS, 412, EK_MAXSECTORS) == -1,
        "and so is anything at or past the array");
  CHECK(ekm_us_origin_sector(true, EK_MAXSECTORS - 1, 412, EK_MAXSECTORS) == EK_MAXSECTORS - 1);
}

// Where the bullets actually land: instrumentation only, gating no shot.
static void tb_0054_impact_rules() {
  // Against the shipped names.h: BULLETHOLE 952 (:276), SMALLSMOKE 2329 (:498), SHOTSPARK1 2595 (:582).
  CHECK(EKM_PIC_BULLETHOLE == 952);
  CHECK(EKM_PIC_SMALLSMOKE == 2329);
  CHECK(EKM_PIC_SHOTSPARK1 == 2595);

  CHECK(ekm_impact_class(952)  == EKM_IMPACT_BULLETHOLE);
  CHECK(ekm_impact_class(2329) == EKM_IMPACT_SMALLSMOKE);
  CHECK(ekm_impact_class(2595) == EKM_IMPACT_SHOTSPARK1);
  CHECK_MSG(ekm_impact_class(2360) == EKM_IMPACT_NONE,
        "the turret's own tile is not an impact");
  CHECK(ekm_impact_class(0) == EKM_IMPACT_NONE);
  CHECK(ekm_impact_class(951) == EKM_IMPACT_NONE);
  CHECK(ekm_impact_class(953) == EKM_IMPACT_NONE);

  // A freed slot keeps its picnum: deletesprite (engine.cpp:8825-8839) moves it to statnum MAXSTATUS and sectnum
  // MAXSECTORS without touching picnum, so only the liveness term separates a live bullet hole from a dead one.
  CHECK(ekm_impact_live_class(952, 5) == EKM_IMPACT_BULLETHOLE);
  CHECK(ekm_impact_live_class(2595, 4) == EKM_IMPACT_SHOTSPARK1);
  CHECK_MSG(ekm_impact_live_class(952, EK_MAXSTATUS) == EKM_IMPACT_NONE,
        "a deleted bullet hole is not an impact any more");
  CHECK(ekm_impact_live_class(952, -1) == EKM_IMPACT_NONE);
  CHECK(ekm_impact_live_class(2360, 1) == EKM_IMPACT_NONE);

  // The new-impact rule cannot see a hole deleted and replaced by another in the SAME slot between two frames.
  CHECK_MSG(ekm_impact_is_new(EKM_IMPACT_NONE, EKM_IMPACT_SHOTSPARK1),
        "a slot that was not an impact and now is");
  CHECK_MSG(!ekm_impact_is_new(EKM_IMPACT_SHOTSPARK1, EKM_IMPACT_SHOTSPARK1),
        "the same spark still burning is not a second round");
  CHECK_MSG(ekm_impact_is_new(EKM_IMPACT_SHOTSPARK1, EKM_IMPACT_BULLETHOLE),
        "the slot was recycled into a different impact");
  CHECK_MSG(!ekm_impact_is_new(EKM_IMPACT_BULLETHOLE, EKM_IMPACT_NONE),
        "an impact ending is not an impact starting");
  CHECK(!ekm_impact_is_new(EKM_IMPACT_NONE, EKM_IMPACT_NONE));

  // 256 horizontal units is about a stock enemy's half-width (ekm_sprite_halfwidth is 0.1*xrepeat*tileW.
  CHECK(EKM_IMPACT_ON_XY == 256 && EKM_IMPACT_ON_Z == 4096);
  CHECK(EKM_IMPACT_NEAR_XY == 1024 && EKM_IMPACT_NEAR_Z == 16384);
  CHECK((double)EKM_IMPACT_ON_Z / (double)EKM_IMPACT_ON_XY == (double)EKM_Z_PER_XY);

  CHECK(ekm_impact_band(0, 0) == EKM_BAND_ON);
  CHECK_MSG(ekm_impact_band(256, 4096) == EKM_BAND_ON,
        "the bound is inclusive on both axes");
  CHECK_MSG(ekm_impact_band(257, 0) == EKM_BAND_NEAR,
        "one unit past the sprite's own width is a near miss");
  CHECK_MSG(ekm_impact_band(0, 4097) == EKM_BAND_NEAR,
        "and z alone is enough to leave the box");
  CHECK(ekm_impact_band(1024, 16384) == EKM_BAND_NEAR);
  CHECK_MSG(ekm_impact_band(1025, 0) == EKM_BAND_FAR,
        "past the near box horizontally is elsewhere");
  CHECK_MSG(ekm_impact_band(0, 16385) == EKM_BAND_FAR,
        "a shot through the ceiling above the target is elsewhere, not a miss");
  CHECK_MSG(ekm_impact_band(40000, 200000) == EKM_BAND_FAR,
        "a different room");

  // Names, so the log says BULLETHOLE and not 1; P_PostFireHitscan spawns a decal only on the wall branch
  // (player.cpp:701-763).
  CHECK(strcmp(ekm_impact_name(EKM_IMPACT_BULLETHOLE), "BULLETHOLE") == 0);
  CHECK(strcmp(ekm_impact_name(EKM_IMPACT_SMALLSMOKE), "SMALLSMOKE") == 0);
  CHECK(strcmp(ekm_impact_name(EKM_IMPACT_SHOTSPARK1), "SHOTSPARK1") == 0);
  CHECK_MSG(ekm_impact_name(EKM_IMPACT_NONE) != 0,
        "no class index may return a null the log would print through");
  CHECK(strcmp(ekm_impact_band_name(EKM_BAND_ON), "ON THE TARGET") == 0);
  CHECK(strcmp(ekm_impact_band_name(EKM_BAND_NEAR), "a near miss") == 0);
  CHECK(strcmp(ekm_impact_band_name(EKM_BAND_FAR), "FAR from the target") == 0);
}


static void tb_0055_pitch_quantum() {
  // The aim point must respect the pitch quantum: 39 chaingun rounds at a ceiling turret landed 3 though the ray was
  // right.

  // The quantum itself, derived and not the number 8 written out.
  CHECK(EKM_HORIZ_PER_SLOPE == 128);
  CHECK_MSG(approx((float)ekm_pitch_quantum_z(1024), 128.0f, 0.001f),
        "one horiz unit at 1024 units of range is 128 z");
  CHECK_MSG(approx((float)ekm_pitch_quantum_z(5873), 734.125f, 0.001f),
        "the measured range to the ceiling turret");
  CHECK_MSG(ekm_pitch_quantum_z(0) == 0.0 && ekm_pitch_quantum_z(-1) == 0.0,
        "no range is no quantum, and nothing divides by it");

  unsigned char vis[EKM_SPAN_SAMPLES];
  span_cover(vis, 0.0f);            // the whole body visible: one run, 0..8
  int first = -1, last = -1, tight = -1;

  CHECK(ekm_span_mid(vis, EKM_SPAN_SAMPLES, ekm_span_prefer(1), &first, &last)
        == ekm_span_prefer(1));

  for (int pt = 0; pt < 2; pt++) {
    const int p = ekm_span_prefer(pt);
    const int m = ekm_span_mid(vis, EKM_SPAN_SAMPLES, p, &first, &last,
                               4096.0f, 512, pt, &tight);
    CHECK_MSG(m == p && first == 0 && last == 8 && tight == 0,
          "close and large: the configured aim point is honoured");
  }

  // The ceiling turret: heightZ 3040 (top -435024, bottom -431984), range2d 5873, Head 0.75, whole body visible. 0.75
  // leaves 380 z of margin to the top of the run against a quantum of 734, so one upward round leaves the body (impacts
  // at -435200).
  {
    const int p = ekm_span_prefer(1);
    const int m = ekm_span_mid(vis, EKM_SPAN_SAMPLES, p, &first, &last,
                               3040.0f, 5873, 1, &tight);
    CHECK_MSG(m == 4 && m != p && first == 0 && last == 8,
          "the ceiling turret: 380 of margin against a 734 quantum, so the "
          "run's midpoint and not the configured 0.75");
    CHECK_MSG(tight == 0,
          "the midpoint's own 1140 of margin clears the quantum");
    // The margin rule is pinned, not the answer alone. 522.5 and 1282.5.
    CHECK(approx((float)ekm_span_margin_z(p, 0, 8, 3040.0, 1), 522.5f, 0.5f));
    CHECK(approx((float)ekm_span_margin_z(m, 0, 8, 3040.0, 1), 1282.5f, 0.5f));

    // The run one sample wide (the E1L4 defect): lo == mid == hi measured 0 and the triggerbot refused forever on a
    // body 14560 tall.
    CHECK_MSG(approx((float)ekm_span_margin_z(8, 8, 8, 14560.0, 1), 682.5f, 0.5f),
              "a run one sample wide has the resolution's own room, not zero");
    // Still a real test, not a floor: a thin body measures small.
    CHECK(approx((float)ekm_span_margin_z(8, 8, 8, 200.0, 1), 9.375f, 0.1f));
    CHECK(approx((float)ekm_span_margin_z(4, 4, 4, 3040.0, 0), 142.5f, 0.5f));

    // The band the half-step created: every margin rises 0.046875 of the body, so ekm_span_mid's `room` term flips
    // where quantum - 0.046875h <= margin < quantum.
    CHECK_MSG(approx((float)ekm_span_margin_z(7, 4, 7, 3040.0, 1), 142.5f, 0.5f),
              "the configured sample at the run's top edge now has the "
              "resolution's room, where it had none");
    {
      CHECK(ekm_span_margin_z(7, 4, 7, 3040.0, 1) >= ekm_pitch_quantum_z(1024));
      CHECK(ekm_span_margin_z(7, 4, 7, 3040.0, 1) < ekm_pitch_quantum_z(1536));
    }

    // A thin target at range is still held: 200 units in a one-sample run gives 9.375 margin against a quantum of 734
    // at the turret's range.
    CHECK_MSG(!ekm_shot_placeable(ekm_span_margin_z(8, 8, 8, 200.0, 1), 5873,
                                  140 * 65536 + (int)(0.25 * 65536), 0),
              "half a step is not a licence: a thin body at range is held");
    // The same body up close is fine, so the line above tests the rule, not the number 200.
    CHECK(ekm_shot_placeable(ekm_span_margin_z(8, 8, 8, 200.0, 1), 256,
                             140 * 65536 + (int)(0.25 * 65536), 0));
  }

  // Cover and distance together: lower 40 % hidden leaves run 3..8, holding Centre's sample, honoured at 512 range.
  span_cover(vis, 0.40f);
  CHECK(ekm_span_mid(vis, EKM_SPAN_SAMPLES, ekm_span_prefer(0), &first, &last,
                     4096.0f, 512, 0, &tight) == ekm_span_prefer(0));
  CHECK_MSG(ekm_span_mid(vis, EKM_SPAN_SAMPLES, ekm_span_prefer(0), &first,
                         &last, 4096.0f, 5873, 0, &tight) == 6,
        "cover and range together: the middle of what is left, 3..8");
  CHECK(first == 3 && last == 8);

  // A quantum past the whole body: still the midpoint, the best point available, and the rule says so rather than
  // failing silently.
  span_cover(vis, 0.0f);
  CHECK_MSG(ekm_span_mid(vis, EKM_SPAN_SAMPLES, ekm_span_prefer(1), &first,
                         &last, 3040.0f, 40000, 1, &tight) == 4,
        "no margin anywhere: the midpoint is still the best available");
  CHECK_MSG(tight == 1, "and it is reported, not swallowed");

  CHECK_MSG(ekm_span_mid(vis, EKM_SPAN_SAMPLES, ekm_span_prefer(1), &first,
                         &last, 3040.0f, 0, 1, &tight) == ekm_span_prefer(1),
        "no range means no quantum, so the row is honoured");
  CHECK(tight == 0);
  CHECK(ekm_span_mid(vis, EKM_SPAN_SAMPLES, ekm_span_prefer(1), &first,
                     &last, 3040.0f, -5873, 1, &tight) == ekm_span_prefer(1));
  CHECK_MSG(ekm_span_mid(vis, EKM_SPAN_SAMPLES, ekm_span_prefer(1), &first,
                         &last, 0.0f, 5873, 1, &tight) == 4,
        "a body with no height has no margin at any range");
  CHECK(tight == 1);
  CHECK(ekm_span_mid(vis, EKM_SPAN_SAMPLES, ekm_span_prefer(1), &first,
                     &last, -1.0f, 5873, 1, &tight) == 4);
  // Nothing visible still reports nothing, and the flag does not claim a run.
  {
    unsigned char none[EKM_SPAN_SAMPLES] = {0,0,0,0,0,0,0,0,0};
    CHECK(ekm_span_mid(none, EKM_SPAN_SAMPLES, ekm_span_prefer(1), &first,
                       &last, 3040.0f, 5873, 1, &tight) < 0);
    CHECK(first == -1 && last == -1 && tight == 0);
  }
}


// An angle is only valid for the muzzle it was solved from.

// The inverse of ekm_q16horiz_for: where a shot on this pitch crosses a vertical plane dist2d away.
static double impact_dz_for(int q16horiz, int dist2d) {
  const double slope = ((double)q16horiz / 65536.0 - (double)EKM_HORIZ_LEVEL)
                     / (double)EKM_HORIZ_PER_SLOPE;
  return -slope * (double)EKM_Z_PER_XY * (double)dist2d;
}

static void tb_0056_live_shot_solve() {
  const int TX = 5873, TY = 0, TZ = -433504;   // the turret's body centre
  const int MX = 0, MY = 0;
  const int MZ = TZ + 47000;                   // a muzzle 47000 z below it
  const int UP = 2048;                         // one tic of jetpack climb

  // A moved muzzle must re-solve the pitch and still hit.
  const EkShotSolve lo = ekm_shot_solve(MX, MY, MZ, TX, TY, TZ, TX, TY, TZ, 0);
  const EkShotSolve hi = ekm_shot_solve(MX, MY, MZ - UP, TX, TY, TZ, TX, TY, TZ, 0);
  CHECK(lo.ok == 1 && hi.ok == 1);
  CHECK_MSG(lo.q16horiz != hi.q16horiz,
        "2048 of climb must change the pitch; the published-angle path left it "
        "identical on all 16 ascending samples");
  CHECK_MSG(lo.yaw == hi.yaw, "climbing straight up moves no yaw");

  // Both pitches still point AT the target: either muzzle crosses the body centre at the measured range.
  CHECK_MSG(approx((float)((double)MZ + impact_dz_for(lo.q16horiz, 5873)),
                   (float)TZ, 1.0f),
        "the standing muzzle's pitch reaches the body centre");
  CHECK_MSG(approx((float)((double)(MZ - UP) + impact_dz_for(hi.q16horiz, 5873)),
                   (float)TZ, 1.0f),
        "and so does the climbed muzzle's, which is the point of re-solving");

  // The defect priced: the low muzzle's pitch fired from the high muzzle is exactly 2048 z high, two thirds of a
  // 3040-tall turret.
  CHECK_MSG(approx((float)((double)(MZ - UP) + impact_dz_for(lo.q16horiz, 5873)),
                   (float)(TZ - UP), 1.0f),
        "a stale pitch fired from the climbed muzzle lands 2048 above the body");

  for (int k = 0; k < 4; k++) {
    const int tz = TZ + k * 4096;
    const int sdx = TX - MX, sdy = TY - MY;
    int wasYaw = 0, wasDist = 0;
    if (sdx != 0 || sdy != 0) {
      wasDist = (int)sqrt((double)sdx * sdx + (double)sdy * sdy);
      wasYaw = ekm_aim_yaw(sdx, sdy);
    }
    const int wasHoriz = ekm_q16horiz_for(tz - MZ, wasDist) - 17;
    const EkShotSolve now = ekm_shot_solve(MX, MY, MZ, TX, TY, tz, TX, TY, tz, 17);
    CHECK_MSG(now.ok == 1 && now.usedLed == 1 &&
              now.yaw == wasYaw && now.q16horiz == wasHoriz,
          "a muzzle that has not moved yields exactly what shipped");
  }

  // Led first, raw as the fallback: aim.cpp solved to the led point and fell back to the raw body point when the lead
  // threw the pitch past the engine's clamp.
  {
    const EkShotSolve s = ekm_shot_solve(MX, MY, MZ, MX + 10, MY, MZ - 470000,
                                         TX, TY, TZ, 0);
    CHECK_MSG(s.ok == 1 && s.usedLed == 0,
          "an unreachable led pitch falls back to the raw point");
    CHECK(s.yaw == lo.yaw && s.q16horiz == lo.q16horiz);
  }

  {
    const EkShotSolve s = ekm_shot_solve(MX, MY, MZ, MX + 10, MY, MZ - 470000,
                                         MX + 10, MY, MZ - 470000, 0);
    CHECK_MSG(s.ok == 0,
          "a pitch the engine would clamp is not a shot, so silent aim leaves "
          "the player's own alone");
  }

  {
    const EkShotSolve same = ekm_shot_solve(MX, MY, MZ, MX, MY, MZ, MX, MY, MZ, 0);
    CHECK_MSG(same.ok == 1 && same.yaw == 0 &&
              same.q16horiz == ekm_f16(EKM_HORIZ_LEVEL),
          "target on the muzzle: level, not a division by zero");

    const EkShotSolve over = ekm_shot_solve(MX, MY, MZ, MX, MY, MZ - 470000,
                                            MX, MY, MZ - 470000, 0);
    CHECK_MSG(over.ok == 1 && over.q16horiz == ekm_f16(EKM_HORIZ_LEVEL) &&
              ekm_horiz_reachable(over.q16horiz),
          "straight overhead has no 2D range, so no slope and no wild angle");
  }
}

// The shooter's own blocking bits: our rays start at the muzzle, inside the player's sprite, so Build could return it
// as the hit and the triggerbot read the shot as blocked (940 of 10927 rays in one session).
static void tb_0057_shooter_cstat() {
  CHECK_MSG(EK_SHOOTER_CSTATMASK == 257,
        "the mask the engine passes for every hitscan weapon is 256+1");
  CHECK(EK_CSTAT_BLOCKING == 1);
  CHECK(EK_CSTAT_BLOCK_HITSCAN == 256);
  CHECK(EK_SHOOTER_CSTATMASK == EK_CSTAT_BLOCKING + EK_CSTAT_BLOCK_HITSCAN);

  // Exactly those two bits: the player's stock cstat is 257, and clearing a third would be a write the engine never
  // makes.
  CHECK_MSG(ekm_shooter_cstat_cleared(257) == 0,
        "the stock player cstat clears to nothing");
  CHECK_MSG(ekm_shooter_cstat_cleared(0x8101) == 0x8000,
        "the invisible bit is not ours to touch");
  CHECK_MSG(ekm_shooter_cstat_cleared(0x7FFF) == (0x7FFF & ~257),
        "every other bit survives the clear");
  CHECK(ekm_shooter_cstat_cleared(0) == 0);

  CHECK(ekm_shooter_cstat_changes(257));
  CHECK(ekm_shooter_cstat_changes(1));
  CHECK(ekm_shooter_cstat_changes(256));
  CHECK_MSG(!ekm_shooter_cstat_changes(0x8000),
        "no blocking bits set is no write");
  CHECK(!ekm_shooter_cstat_changes(0));

  for (int c = 0; c <= 0x1FF; c++) {
    const int saved = c;
    const int cleared = ekm_shooter_cstat_cleared(c);
    CHECK_MSG((cleared & EK_SHOOTER_CSTATMASK) == 0 && saved == c,
          "clear leaves neither bit, and the saved value is untouched by it");
  }
}

// A dying trooper is not a choking one: `state troopdying` (GAME.CON:4341) runs `cstat 0` only at `ifactioncount 5`, so
// through the fall a dead trooper reads extra 0 with a hittable cstat, bit for bit the choking pair.
static void tb_0058_choking_needs_suffering() {
  const unsigned CS_LIVE = 0x101u;     // blocking and hitscan-sensitive
  const unsigned char XR = 16;
  const int TROOP = 1680;

  // The three answers are distinct and only one refuses.
  CHECK(EKM_SUFFER_YES != EKM_SUFFER_NO);
  CHECK(EKM_SUFFER_UNKNOWN != EKM_SUFFER_YES);
  CHECK(EKM_SUFFER_UNKNOWN != EKM_SUFFER_NO);

  EkAction dying;
  dying.startframe = 50; dying.numframes = 5; dying.viewtype = 1;
  dying.incval = 1; dying.delay = 16; dying.readable = 1; dying.plausible = 1;
  CHECK_MSG(!ekm_action_is_suffering(dying),
        "ATROOPDYING is not ATROOPSUFFERING");

  CHECK_MSG(ekm_trooper_choking(TROOP, 0, 0, CS_LIVE, XR, EKM_SUFFER_YES),
        "a trooper running ATROOPSUFFERING at zero health is choking");
  CHECK_MSG(!ekm_trooper_choking(TROOP, 0, 0, CS_LIVE, XR, EKM_SUFFER_NO),
        "a trooper running some other action at zero health is dying, not choking");

  CHECK_MSG(ekm_trooper_choking(TROOP, 0, 0, CS_LIVE, XR, EKM_SUFFER_UNKNOWN),
        "an undecodable action falls back to the pre-fix verdict");
  CHECK_MSG(ekm_trooper_choking(TROOP, 0, 0, CS_LIVE, XR),
        "and so does a caller that does not ask, which is what the default is for");

  // The suffering answer rescues nothing the other terms refuse: one more requirement, never a new way in.
  CHECK(!ekm_trooper_choking(TROOP, 30, 0, CS_LIVE, XR, EKM_SUFFER_YES));
  CHECK_MSG(!ekm_trooper_choking(TROOP, 0, EKM_PAL_FROZEN, CS_LIVE, XR, EKM_SUFFER_YES),
        "frozen is still not choking");
  CHECK_MSG(!ekm_trooper_choking(2000, 0, 0, CS_LIVE, XR, EKM_SUFFER_YES),
        "a Pig Cop is still out of scope whatever its action says");
  CHECK(!ekm_trooper_choking(1741, 0, 0, CS_LIVE, XR, EKM_SUFFER_YES));
  CHECK(!ekm_trooper_choking(TROOP, 0, 0, 0u, XR, EKM_SUFFER_YES));

  CHECK_MSG(ekm_actor_state(TROOP, 0, 0, CS_LIVE, XR, false, EKM_SUFFER_NO) == EKA_DOWN,
        "the ATROOPDYING fall is down, so the box clears and the bot stops");
  CHECK(ekm_actor_state(TROOP, 0, 0, CS_LIVE, XR, false, EKM_SUFFER_YES) == EKA_CHOKING);
  CHECK(ekm_actor_state(TROOP, 0, 0, CS_LIVE, XR, false) == EKA_CHOKING);
  CHECK_MSG(ekm_target_dying(0, 0, TROOP, CS_LIVE, XR, EKM_SUFFER_NO),
        "and the dying rule agrees with the state");
  CHECK(!ekm_target_dying(0, 0, TROOP, CS_LIVE, XR, EKM_SUFFER_YES));

  CHECK(ekm_actor_state(TROOP, 0, 0, 0u, XR, true, EKM_SUFFER_NO) == EKA_PLAYDEAD);
  CHECK(ekm_actor_state(TROOP, 0, 0, 0u, XR, true, EKM_SUFFER_YES) == EKA_PLAYDEAD);
}

// The ESP's health number does not move during a pain animation (a Pig Cop shows it most).
static void tb_0059_shown_health() {
  // Nothing pending is -1, not 0, as the engine already establishes.
  CHECK(EK_HTEXTRA_NONE == -1);
  CHECK_MSG(ekm_actor_shown_extra(100, EK_HTEXTRA_NONE) == 100,
        "nothing pending shows the sprite's own health");
  CHECK_MSG(ekm_actor_shown_extra(100, 0) == 100,
        "a pending hit for no damage moves nothing");

  CHECK_MSG(ekm_actor_shown_extra(100, 30) == 70,
        "a Pig Cop mid-pain shows what the hit already took");
  CHECK(ekm_actor_shown_extra(100, 1) == 99);
  CHECK(ekm_actor_shown_extra(1, 1) == 0);

  // Floored at zero, never negative: an overkill is still a readable number.
  CHECK(ekm_actor_shown_extra(10, 400) == 0);
  CHECK(ekm_actor_shown_extra(10, 10) == 0);

  // A negative `extra` is left alone: it is already a corpse's number and the dying rule refused it.
  CHECK(ekm_actor_shown_extra(-5, EK_HTEXTRA_NONE) == -5);

  const unsigned CS = 0x101u;
  const unsigned char XR = 16;
  CHECK_MSG(ekm_actor_effective_extra(2000, 100, 100) == 100,
        "a Pig Cop's pending damage is NOT discounted for targeting");
  CHECK_MSG(!ekm_target_dying(ekm_actor_effective_extra(2000, 100, 100), 0, 2000, CS, XR),
        "so a Pig Cop mid-pain is still a live target");
  CHECK_MSG(ekm_actor_shown_extra(100, 100) == 0,
        "while the number it shows is the one the player is watching fall");

  CHECK(ekm_actor_effective_extra(1741, 30, 45) == 0);
  CHECK(ekm_actor_effective_extra(1742, 30, 45) == 0);
  CHECK_MSG(ekm_actor_effective_extra(1680, 30, 45) == 30,
        "a standing trooper is not a sitting one");

  // The whole range at one health, so the two rules cannot be transposed.
  for (int pending = -1; pending <= 60; pending++) {
    const int shown = ekm_actor_shown_extra(50, pending);
    const int target = ekm_actor_effective_extra(2000, 50, pending);
    CHECK_MSG(shown >= 0 && shown <= 50 && target == 50,
          "shown health falls with pending damage; the targeting number never does");
  }
}

// Silent aim is not bound by the view's pitch clamp. player.cpp:3179 clamps q16horiz to -99..299 once per tic in
// P_GetInput, but silent aim substitutes q16horiz inside the shot call after that, and the engine derives zvel from it
// with no clamp (player.cpp:521).
static void tb_0060_shot_pitch_bound() {
  // The VIEW bound is untouched: it is what the engine KEEPS.
  CHECK(EKM_HORIZ_MIN == -99 && EKM_HORIZ_MAX == 299);

  // The SHOT bound is derived: the span ekm_shot_ray computes the asked-for pitch over before its own clamp.
  CHECK(EKM_SHOT_HORIZ_MAX == EKM_HORIZ_LEVEL + EKM_SHOT_ZVEL_WHOLE_MAX);
  CHECK(EKM_SHOT_HORIZ_MIN == EKM_HORIZ_LEVEL - EKM_SHOT_ZVEL_WHOLE_MAX);
  CHECK_MSG(EKM_SHOT_HORIZ_MIN < EKM_HORIZ_MIN && EKM_SHOT_HORIZ_MAX > EKM_HORIZ_MAX,
        "the shot reaches strictly further than the view, on both sides");

  // A pitch inside the view bound is reachable by both; stepped by 13 to cover the span in 31 samples.
  for (int h = EKM_HORIZ_MIN; h <= EKM_HORIZ_MAX; h += 13) {
    CHECK_MSG(ekm_horiz_reachable(ekm_f16(h)) &&
              ekm_shot_horiz_reachable(ekm_f16(h)),
          "a pitch the view can hold is one the shot can reach");
  }

  // The four measured refusals, ordinary enemies on a ledge: three above the view's ceiling, one below its floor.
  const int measured[4] = { 302, 314, 356, -102 };
  for (int i = 0; i < 4; i++) {
    CHECK_MSG(!ekm_horiz_reachable(ekm_f16(measured[i])),
          "the view still cannot be pointed there -- Duke's own limit stands");
    CHECK_MSG(ekm_shot_horiz_reachable(ekm_f16(measured[i])),
          "but the shot can, and acquisition must no longer refuse it");
  }

  CHECK(ekm_shot_horiz_reachable(ekm_f16(EKM_SHOT_HORIZ_MIN)));
  CHECK(ekm_shot_horiz_reachable(ekm_f16(EKM_SHOT_HORIZ_MAX)));
  CHECK(!ekm_shot_horiz_reachable(ekm_f16(EKM_SHOT_HORIZ_MIN) - 1));
  CHECK(!ekm_shot_horiz_reachable(ekm_f16(EKM_SHOT_HORIZ_MAX) + 1));

  // Something absurd is reachable by neither: a target 470000 z above the muzzle at ten units wants about 376100 horiz
  // units.
  const int absurd = ekm_q16horiz_for(-470000, 10);
  CHECK(!ekm_horiz_reachable(absurd) && !ekm_shot_horiz_reachable(absurd));

  // At the bound the builder still computes the asked-for pitch and the ray stays a ray (horizontal components keep the
  // sintable's full amplitude, so hitscan advances in 2D).
  const EkShotRay edge = ekm_shot_ray(ekm_f16(0), ekm_f16(EKM_SHOT_HORIZ_MAX), 0);
  CHECK(edge.vz == -EKM_SHOT_ZVEL_WHOLE_MAX * 32 * 64);
  CHECK_MSG(edge.vx == 16384 && edge.vy == 0,
        "the steepest shot still has its whole horizontal component");
  const EkShotRay past = ekm_shot_ray(ekm_f16(0), ekm_f16(EKM_SHOT_HORIZ_MAX + 1), 0);
  CHECK_MSG(past.vz == edge.vz,
        "past the bound the ray builder clamps, so the bound is where the "
        "shot stops being the one that was asked for");
}

// Shot tracers draw two lines per shot: our predicted ray (exact) and the engine's impact (a PAIRING, hence a guess).
static void tb_0061_tracer_rules() {
  CHECK_MSG(ekm_tracer_alpha(0) == 255,
        "a tracer is at full strength the instant the shot is recorded");
  CHECK_MSG(ekm_tracer_alpha(EKM_TRACER_FADE_MS) == 0,
        "and is gone at exactly ten seconds");
  CHECK_MSG(ekm_tracer_alpha(EKM_TRACER_FADE_MS + 1) == 0, "and stays gone past it");
  CHECK_MSG(ekm_tracer_alpha(EKM_TRACER_FADE_MS - 1) > 0,
        "a tracer one millisecond short of the end is still drawn -- it FADES "
        "rather than vanishing at the edge");
  {
    const int half = ekm_tracer_alpha(EKM_TRACER_FADE_MS / 2);
    CHECK_MSG(half > 100 && half < 155, "half way through it is about half strength");
  }
  {
    int prev = 256;
    bool monotonic = true;
    for (int i = 0; i < 64; i++) {
      const int a = ekm_tracer_alpha((unsigned)((long long)EKM_TRACER_FADE_MS * i / 63));
      if (a > prev) monotonic = false;
      prev = a;
    }
    CHECK_MSG(monotonic, "the fade never brightens between age 0 and ten seconds");
  }

  {
    EkTracerRing r;
    ekm_tracer_reset(&r);
    CHECK_MSG(r.count == 0, "a reset ring holds nothing");
    CHECK_MSG(ekm_tracer_index(&r, 0) < 0, "and has no most-recent entry to hand back");

    EkTracer t;
    memset(&t, 0, sizeof(t));
    for (int i = 0; i < EKM_TRACER_MAX; i++) { t.ox = i; ekm_tracer_add(&r, t); }
    CHECK_MSG(r.count == EKM_TRACER_MAX, "sixty-four shots fill the ring");
    CHECK_MSG(r.slot[ekm_tracer_index(&r, 0)].ox == EKM_TRACER_MAX - 1,
          "entry 0 is the NEWEST shot, not the oldest");
    CHECK_MSG(r.slot[ekm_tracer_index(&r, EKM_TRACER_MAX - 1)].ox == 0,
          "and the last entry is the oldest still held");
    CHECK_MSG(ekm_tracer_index(&r, EKM_TRACER_MAX) < 0, "there is no sixty-fifth entry");

    // Sixteen more shots must EVICT the sixteen oldest: a ring that refused the insert would keep the 64 oldest, still
    // a full buffer but showing shots from a minute ago.
    for (int i = EKM_TRACER_MAX; i < EKM_TRACER_MAX + 16; i++) {
      t.ox = i;
      ekm_tracer_add(&r, t);
    }
    CHECK_MSG(r.count == EKM_TRACER_MAX, "the ring does not grow past sixty-four");
    CHECK_MSG(r.slot[ekm_tracer_index(&r, 0)].ox == EKM_TRACER_MAX + 15,
          "the newest entry is the last shot added, across the wrap");
    CHECK_MSG(r.slot[ekm_tracer_index(&r, EKM_TRACER_MAX - 1)].ox == 16,
          "and the oldest held is shot 16: a full ring keeps the 64 MOST "
          "RECENT, so the first sixteen were evicted and not the last sixteen");
    bool ordered = true;
    for (int i = 0; i < EKM_TRACER_MAX; i++)
      if (r.slot[ekm_tracer_index(&r, i)].ox != EKM_TRACER_MAX + 15 - i) ordered = false;
    CHECK_MSG(ordered, "every entry is the i-th most recent, newest first, across the wrap");
  }

  // The clip rule.
  {
    EkmVec3 a = { 1.0f, 2.0f, -5.0f }, b = { 3.0f, 4.0f, -1.0f };
    const EkmVec3 a0 = a, b0 = b;
    CHECK_MSG(ekm_clip_near(&a, &b) == 1,
          "a segment with both endpoints in front of the near plane is drawable");
    CHECK_MSG(a.x == a0.x && a.y == a0.y && a.z == a0.z &&
              b.x == b0.x && b.y == b0.y && b.z == b0.z,
          "...and is left exactly as it was: the ordinary case is UNCHANGED");
  }
  {
    EkmVec3 a = { 1.0f, 1.0f, 2.0f }, b = { 2.0f, 2.0f, 0.5f };
    CHECK_MSG(ekm_clip_near(&a, &b) == 0,
          "a segment wholly behind the eye draws NOTHING -- this is the case "
          "that streaks a mirrored line across the whole display");
  }
  {
    EkmVec3 a = { 0.0f, 0.0f, -1.0f };    // in front
    EkmVec3 b = { 10.0f, 20.0f, 1.0f };   // behind
    CHECK_MSG(ekm_clip_near(&a, &b) == 1, "a segment crossing the near plane is drawable");
    CHECK_MSG(a.x == 0.0f && a.y == 0.0f && a.z == -1.0f,
          "the endpoint already in front is not moved");
    CHECK_MSG(approx(b.z, EKM_NEAR_EYE_Z, 0.0001f),
          "the endpoint behind is moved ONTO the near plane");
    CHECK_MSG(approx(b.x, 4.95f, 0.01f) && approx(b.y, 9.9f, 0.02f),
          "and moved ALONG the segment, not to some other point on the plane");
    CHECK_MSG(ekm_projectable(b), "a clipped endpoint is one the projector accepts");
    CHECK_MSG(!ekm_in_front(b),
          "while the strict in-front test rejects the boundary itself, which "
          "is why the clipped path asks ekm_projectable and not ekm_in_front");
  }
  {
    // Order must not matter: the muzzle is behind the eye as often as the impact (a shot fired while turning away).
    EkmVec3 a = { 10.0f, 20.0f, 1.0f }, b = { 0.0f, 0.0f, -1.0f };
    CHECK_MSG(ekm_clip_near(&a, &b) == 1,
          "the crossing case works with the behind endpoint given first");
    CHECK_MSG(approx(a.z, EKM_NEAR_EYE_Z, 0.0001f) && approx(a.x, 4.95f, 0.01f),
          "...and clips to the same point");
    CHECK_MSG(b.x == 0.0f && b.z == -1.0f, "leaving the other end alone");
  }

  // The pairing window: the actual line is a heuristic, and an impact not close enough in time to a recorded shot pairs
  // with nothing, leaving that shot's predicted line alone.
  {
    const unsigned shot = 1000u;
    CHECK_MSG(ekm_tracer_pair_ok(shot, shot, EKM_TRACER_PAIR_MS),
          "an impact in the same millisecond as the shot pairs");
    CHECK_MSG(ekm_tracer_pair_ok(shot, shot + EKM_TRACER_PAIR_MS, EKM_TRACER_PAIR_MS),
          "and one at the very edge of the window pairs");
    CHECK_MSG(!ekm_tracer_pair_ok(shot, shot + EKM_TRACER_PAIR_MS + 1, EKM_TRACER_PAIR_MS),
          "a candidate one millisecond PAST the window does not pair");
    CHECK_MSG(!ekm_tracer_pair_ok(shot, shot - 1, EKM_TRACER_PAIR_MS),
          "and neither does one stamped BEFORE the shot it would be paired to");
    CHECK_MSG(ekm_tracer_pair_ok(0xFFFFFFF0u, 0x00000005u, EKM_TRACER_PAIR_MS),
          "the window is exact across the wallclock counter's wrap");
  }
}

// The triggerbot holds fire when the shot cannot be PLACED.
static void tb_0062_shot_placeable() {
  // f16 pitch helpers: a whole unit plus a fraction, since the subject is the fraction fix16_to_int throws away.
  const int kWhole = 140 * 65536;

  CHECK_MSG(ekm_shot_placeable(1140.0, 10825, kWhole + (int)(0.0825 * 65536), 0),
            "the E1L3 turret fires: 112 z of rounding against 1140 of body");
  CHECK_MSG(ekm_shot_placeable(1140.0, 10956, kWhole + (int)(0.3991 * 65536), 0),
            "the widest logged leftover still lands");
  CHECK(ekm_shot_placeable(760.0, 10564, kWhole + (int)(0.0333 * 65536), 0));

  // It still refuses when the rounding genuinely leaves the body: 0.49 of a quantum 1320 is 647 z against a body
  // offering 300.
  CHECK_MSG(!ekm_shot_placeable(300.0, 10564, kWhole + (int)(0.49 * 65536), 0),
            "a 647 z rounding error against 300 of body is refused");
  // Exactly on the nearer edge is placeable, at a range where the quantum is a whole number: 1024 * 16 / 128 is 128 and
  // a quarter unit is 32.
  CHECK_MSG(ekm_shot_placeable(32.0, 1024, kWhole + (int)(0.25 * 65536), 0),
            "an impact exactly on the nearer edge is placeable");
  CHECK_MSG(!ekm_shot_placeable(31.0, 1024, kWhole + (int)(0.25 * 65536), 0),
            "one z past the edge is not");

  // A pitch the engine rounds exactly loses nothing, whatever the body size.
  CHECK_MSG(ekm_shot_placeable(1.0, 5873, kWhole, 0),
            "a whole-unit pitch has no rounding to lose");

  // Rounding is to nearest, not toward zero: 0.6 of a unit leaves 0.4, so 528 rather than 792 at quantum 1320.
  CHECK(ekm_shot_placeable(600.0, 10564, kWhole + (int)(0.6 * 65536), 0));
  CHECK(!ekm_shot_placeable(400.0, 10564, kWhole + (int)(0.6 * 65536), 0));

  CHECK(ekm_shot_placeable(600.0, 10564, -kWhole - (int)(0.6 * 65536), 0));
  CHECK(!ekm_shot_placeable(400.0, 10564, -kWhole - (int)(0.6 * 65536), 0));

  // Horizoff is part of the pitch being rounded: the solver subtracts it and the engine adds it back, so half a unit of
  // horizoff is 660 z of leftover at this quantum and 400 of body cannot hold it.
  CHECK_MSG(ekm_shot_placeable(700.0, 10564, kWhole, (int)(0.5 * 65536)),
            "horizoff's own fraction is what gets rounded away");
  CHECK_MSG(!ekm_shot_placeable(400.0, 10564, kWhole, (int)(0.5 * 65536)),
            "and it is refused when the body cannot hold it");
  CHECK_MSG(ekm_shot_placeable(1.0, 10564, kWhole + (int)(0.5 * 65536),
                               (int)(0.5 * 65536)),
            "two half-unit fractions sum to a whole and lose nothing");

  // No range is no quantum, unchanged: a caller with no measurement must not start holding fire.
  CHECK_MSG(ekm_shot_placeable(0.0, 0, kWhole + 32768, 0),
            "an unmeasured candidate is placeable, as it was before this rule");
  CHECK(ekm_shot_placeable(0.0, -1, kWhole + 32768, 0));

  // It may only widen a refusal into a hold, like tbot_ray_cond.
  TbotCond holds = { 1, "lock" };
  TbotCond refused = { 0, "notarget" };
  TbotCond ok = tbot_placed_cond(holds, 1140.0, 10825,
                                 kWhole + (int)(0.0825 * 65536), 0);
  CHECK(ok.holds == 1 && strcmp(ok.reason, "lock") == 0);
  TbotCond tight = tbot_placed_cond(holds, 300.0, 10564,
                                    kWhole + (int)(0.49 * 65536), 0);
  CHECK_MSG(tight.holds == 0 && strcmp(tight.reason, "noroom") == 0,
            "a shot that rounds off the body holds fire and logs noroom");
  TbotCond kept = tbot_placed_cond(refused, 300.0, 10564,
                                   kWhole + (int)(0.49 * 65536), 0);
  CHECK_MSG(kept.holds == 0 && strcmp(kept.reason, "notarget") == 0,
            "a refusal keeps the reason it already had");
}

// The candidate comparison, run by TWO accumulators: acquisition keeps its winner and the best candidate the VIEW can
// be pointed at, so an unreachable winner cannot starve the aimbot.
static void tb_0063_cand_better() {
  CHECK(ekm_cand_better(false, false, 1, 999, 99999, 0, 0, 0));

  // Tier first, outranking distance and crosshair offset: a live enemy beats a frozen or choking one however far.
  CHECK(ekm_cand_better(true, false, 0, 0, 40000, 1, 0, 10));
  CHECK(!ekm_cand_better(true, false, 1, 0, 10, 0, 0, 40000));
  CHECK(!ekm_cand_better(true, true, 1, 0, 10, 0, 500, 40000));

  // Within a tier, crosshair mode ranks by angular offset, distance only breaking an equal offset.
  CHECK(ekm_cand_better(true, true, 0, 10, 5000, 0, 20, 100));
  CHECK(!ekm_cand_better(true, true, 0, 20, 100, 0, 10, 5000));
  CHECK(ekm_cand_better(true, true, 0, 10, 99, 0, 10, 100));
  CHECK(!ekm_cand_better(true, true, 0, 10, 100, 0, 10, 100));

  CHECK(ekm_cand_better(true, false, 0, 900, 99, 0, 0, 100));
  CHECK(!ekm_cand_better(true, false, 0, 0, 100, 0, 900, 99));

  CHECK(!ekm_cand_better(true, false, 0, 7, 100, 0, 7, 100));
  CHECK(!ekm_cand_better(true, true, 0, 7, 100, 0, 7, 100));
}

// Acquisition runs once per TIC. aim_frame runs from the SwapBuffers hook, so the loop ran once per rendered frame (2-4
// times a tic at 60-120 fps) with identical results, and that cost made a probe budget look necessary, a hard ceiling
// on acquisition range.
static void tb_0066_acquire_cadence() {
  // Nothing acquired yet is always due: a false here would never acquire, the one failure the cadence may not have.
  CHECK_MSG(ekm_acquire_due(false, 0), "the first frame must acquire");
  CHECK(ekm_acquire_due(false, 1));
  CHECK(ekm_acquire_due(false, 100000));

  // The gap is the engine's tic truncated to whole milliseconds, the unit of the clock.
  CHECK(EKM_ACQUIRE_GAP_MS == (unsigned)EKM_MS_PER_TIC);
  for (unsigned ms = 0; ms < 64; ms++)
    CHECK(ekm_acquire_due(true, ms) == (ms >= EKM_ACQUIRE_GAP_MS));

  // A stamp from the future, or a wrapped subtraction, reads as enormous and acquires: fail toward doing the work.
  CHECK(ekm_acquire_due(true, 0xFFFFFFFFu));

  // The published lock cannot go stale at this cadence: the oldest a solution can be is one gap, and ekm_shot_fresh
  // must call that fresh (33 against a 133 ms window).
  CHECK_MSG(ekm_shot_fresh(EKM_ACQUIRE_GAP_MS),
            "a solution one tic old must still be shootable");
  CHECK(EKM_ACQUIRE_GAP_MS < EKM_SHOT_STALE_MS);
  CHECK(ekm_shot_fresh(EKM_ACQUIRE_GAP_MS * 2));
}

// The health label's maximum is the GREATER of this build's CON strength and the highest health seen on that sprite.
static void tb_0065_health_max_shown() {
  // The Captain: TROOPSTRENGTH 30 in CON, 60 once the engine doubled it.
  CHECK_MSG(ekm_enemy_shown_max(ekm_enemy_max_strength(1680), 60) == 60,
            "an observed 60 beats LIZTROOP's tabled 30");
  // Observed below the table is the table: an enemy first seen wounded must not lower its own maximum.
  CHECK_MSG(ekm_enemy_shown_max(30, 18) == 30,
            "an observation below the table does not lower the maximum");
  CHECK_MSG(ekm_enemy_shown_max(30, 30) == 30, "equal is the same number");
  // Unlisted and unobserved stays 0, selecting esp_health_number's bare "%d" form, not "%d/0".
  CHECK_MSG(ekm_enemy_shown_max(ekm_enemy_max_strength(1960), 0) == 0,
            "RECON has no CON line and nothing seen: no maximum");
  char buf[ESP_HEALTH_NUM_BUFSIZE];
  CHECK_MSG(esp_health_number(buf, 40, ekm_enemy_shown_max(0, 0), 0) &&
                strcmp(buf, "40") == 0,
            "and that still prints the bare number");
  CHECK_MSG(ekm_enemy_shown_max(ekm_enemy_max_strength(1960), 250) == 250,
            "an observed RECON measures against what was seen");
  // HP above the maximum still prints honestly: the label is a report, not a clamp, and capping would hide the
  // doubling.
  CHECK_MSG(esp_health_number(buf, 61, ekm_enemy_shown_max(30, 60), 0) &&
                strcmp(buf, "61/60") == 0,
            "hp above the resulting maximum is not clamped");
  // Garbage in either half is unknown, never a negative maximum.
  CHECK(ekm_enemy_shown_max(-5, -5) == 0);
  CHECK(ekm_enemy_shown_max(0, -5) == 0);
}

// The Assault Captain's real maximum before its CON has run: a Captain shot at 30/30 showed ~50/30 because `state
// checktrooppalette` (GAME.CON:4709-4718) sits in `ifai 0` and fires on the FIRST EXECUTED TIC.
static void tb_0067_captain_first_tic_health() {
  const int spawn = ekm_enemy_max_strength(1680);          // TROOPSTRENGTH, 30
  const int ruled = ekm_enemy_ruled_max(1680, EKM_PAL_CAPTAIN);
  CHECK_MSG(spawn == 30, "LIZTROOP spawns at TROOPSTRENGTH");
  CHECK_MSG(ruled == 60, "addstrength TROOPSTRENGTH doubles it for pal 21");

  // A dormant Captain at exactly its spawn strength shows the ruled maximum.
  CHECK_MSG(ekm_display_hp(spawn, spawn, ruled, false) == 60,
            "a dormant Captain at 30 really has 60");

  // One point below shows its own health: only health exactly on the spawn strength means "the override has not fired".
  CHECK_MSG(ekm_display_hp(spawn - 1, spawn, ruled, false) == 29,
            "one point below spawn is not an unfired override");
  CHECK_MSG(ekm_display_hp(45, spawn, ruled, false) == 45,
            "nor is anything between the two strengths");

  // An actor that has run is never substituted: its sprite already carries what the CON made of it.
  CHECK_MSG(ekm_display_hp(spawn, spawn, ruled, true) == 30,
            "a woken Captain at 30 has been hurt, not doubled");

  CHECK_MSG(ekm_display_hp(spawn, spawn, 0, false) == 30,
            "no ruled maximum, no substitution");
  CHECK_MSG(ekm_display_hp(spawn, spawn, spawn, false) == 30,
            "a ruled maximum equal to spawn changes nothing");

  const int plain = ekm_enemy_ruled_max(1680, 0);
  CHECK_MSG(plain == 0, "a pal 0 trooper has no ruled maximum");
  bool plainHeld = true;
  for (int hp = 0; hp <= 60; hp++)
    if (ekm_display_hp(hp, spawn, plain, false) != hp) plainHeld = false;
  CHECK_MSG(plainHeld, "an Assault Trooper reads its own health at every value");

  CHECK_MSG(ekm_enemy_ruled_max(2000, EKM_PAL_CAPTAIN) == 0,
            "a pal 21 Pig Cop is not a Captain");
  CHECK_MSG(ekm_enemy_ruled_max(1960, EKM_PAL_CAPTAIN) == 0,
            "and a picnum with no CON strength has no ruled maximum either");

  const int shown = ekm_display_hp(spawn, spawn, ruled, false);
  CHECK_MSG(ekm_enemy_shown_max(spawn, shown) == 60,
            "the ceiling fed the substituted value reads 60");
  // Fed the discounted number, one burst teaches a maximum below the real one (a Captain hit for 18 would read 42/30).
  CHECK_MSG(ekm_enemy_shown_max(spawn, ekm_actor_shown_extra(spawn, 18)) == 30,
            "fed a mid-flight number it would learn 30, which is the bug");
  CHECK_MSG(ekm_actor_shown_extra(shown, 18) == 42,
            "the queued damage comes off the substituted number");
}

static void tb_0068_dying_record() {
  // The three answers are distinct and only one retires anything.
  CHECK(EKM_DYING_YES != EKM_DYING_NO);
  CHECK(EKM_DYING_UNKNOWN != EKM_DYING_YES);
  CHECK(EKM_DYING_UNKNOWN != EKM_DYING_NO);

  const EkAction troopDying = mk(50, 5, 1, 1, 16);    // ATROOPDYING,      :4018
  const EkAction troopDead  = mk(54, 0, 0, 0, 0);     // ATROOPDEAD,       :4019
  const EkAction playDead   = mk(54, 0, 0, 0, 0);     // ATROOPPLAYDEAD,   :4020
  const EkAction sufferDead = mk(58, 2, 1, -4, 24);   // ATROOPSUFFERDEAD, :4021
  const EkAction suffering  = mk(59, 2, 1, 1, 21);    // ATROOPSUFFERING,  :4022
  const EkAction commDying  = mk(30, 8, 1, 1, 12);    // ACOMMDYING,       :7126
  const EkAction commDead   = mk(38, 1, 1, 1, 1);     // ACOMMDEAD,        :7127
  const int TROOP = 1680, COMM = EK_PICNUM_COMMANDER, OCTA = 1820;

  // The trooper's two, and not the other three.
  CHECK(ekm_action_is_dying(TROOP, troopDying));
  CHECK(ekm_action_is_dying(TROOP, sufferDead));
  CHECK_MSG(!ekm_action_is_dying(TROOP, suffering),
            "ATROOPSUFFERING is a trooper that may still be finished off");
  // ATROOPDEAD and ATROOPPLAYDEAD have IDENTICAL records, so admitting (54,0,0,0,0) would make every play-dead trooper
  // a death record and put two rules in a fight; the corpse needs no help (extra <= 0 already answers DOWN).
  CHECK_MSG(!ekm_action_is_dying(TROOP, troopDead),
            "the DEAD record is excluded, because PLAYDEAD is bit for bit the same");
  CHECK(!ekm_action_is_dying(TROOP, playDead));

  // The Commander's two: `ifai AICOMMDYING` runs `fall`, `strength 0` and does not clear cstat until actioncount 8
  // (GAME.CON:7372-7392), so the record, not the number, says it is dying.
  CHECK(ekm_action_is_dying(COMM, commDying));
  CHECK(ekm_action_is_dying(COMM, commDead));

  // The scope is the safety argument: AOCTADEAD (GAME.CON:5450) declares the SAME five values as ACOMMDEAD, so an
  // unscoped record test would retire a live Octabrain.
  CHECK_MSG(!ekm_action_is_dying(OCTA, commDead),
            "AOCTADEAD is (38,1,1,1,1) too -- the picnum is what separates them");
  CHECK(!ekm_action_is_dying(TROOP, commDying));
  CHECK(!ekm_action_is_dying(COMM, troopDying));
  CHECK_MSG(!ekm_action_is_dying(2000, troopDying),
            "a Pig Cop is judged by neither family's declarations");

  // An unreadable record is never a death record.
  CHECK(!ekm_action_is_dying(TROOP, mk_unreadable()));
  CHECK(!ekm_action_is_dying(COMM, mk_unreadable()));

  // No plausibility gate: it would not matter here but must not be added.
  CHECK(troopDying.plausible && sufferDead.plausible);
  CHECK(commDying.plausible && commDead.plausible);

  const unsigned CS_LIVE = 0x101u, CS_DEAD = 0u;
  const unsigned char XR = 32;

  // It outranks a full `extra`: 30 of 30 health, cstat untouched, and the action says the actor is falling over.
  CHECK_MSG(ekm_actor_state(TROOP, 30, 0, CS_LIVE, XR, false,
                            EKM_SUFFER_UNKNOWN, EKM_DYING_YES) == EKA_DOWN,
            "a full health bar loses to a death record");
  CHECK(ekm_actor_state(COMM, 250, 0, CS_LIVE, XR, false,
                        EKM_SUFFER_UNKNOWN, EKM_DYING_YES) == EKA_DOWN);

  CHECK_MSG(ekm_actor_state(TROOP, 30, 0, CS_LIVE, XR, false,
                            EKM_SUFFER_UNKNOWN, EKM_DYING_UNKNOWN) == EKA_ALIVE,
            "an undecodable action keeps the pre-fix verdict");
  CHECK_MSG(ekm_actor_state(TROOP, 30, 0, CS_LIVE, XR, false) == EKA_ALIVE,
            "and so does a caller that does not ask, which is what the default is for");

  CHECK(ekm_actor_state(TROOP, 30, 0, CS_LIVE, XR, false,
                        EKM_SUFFER_UNKNOWN, EKM_DYING_NO) == EKA_ALIVE);
  CHECK(ekm_actor_state(COMM, 250, 0, CS_LIVE, XR, false,
                        EKM_SUFFER_UNKNOWN, EKM_DYING_NO) == EKA_ALIVE);
  CHECK_MSG(ekm_actor_state(TROOP, 0, 0, CS_LIVE, XR, false,
                            EKM_SUFFER_YES, EKM_DYING_NO) == EKA_CHOKING,
            "a choking trooper still chokes");

  CHECK_MSG(ekm_actor_state(TROOP, 0, 0, CS_LIVE, XR, false,
                            EKM_SUFFER_YES, EKM_DYING_YES) == EKA_DOWN,
            "a death record outranks the suffering one");

  CHECK(ekm_actor_state(TROOP, 0, 0, CS_DEAD, XR, true,
                        EKM_SUFFER_NO, EKM_DYING_YES) == EKA_PLAYDEAD);
  CHECK(ekm_actor_state(TROOP, 0, 0, CS_DEAD, XR, true,
                        EKM_SUFFER_UNKNOWN, EKM_DYING_UNKNOWN) == EKA_PLAYDEAD);

  CHECK(ekm_actor_state(TROOP, 0, 0, CS_LIVE, XR, false) == EKA_CHOKING);
  CHECK(ekm_actor_state(TROOP, 0, 0, CS_DEAD, XR, false) == EKA_DOWN);
  CHECK(ekm_actor_state(2120, 0, 0, CS_LIVE, XR, false) == EKA_DOWN);
  CHECK(ekm_actor_state(TROOP, 0, EKM_PAL_FROZEN, CS_LIVE, XR, false) == EKA_ALIVE);
}

// The trace window must span the frames: a trooper shot on a toilet regained its ESP box for a few frames after `cactor
// LIZTROOP` turned it into 1680, and one sample at the change cannot show a multi-frame window, so the instrument keeps
// a ring ahead of it and logs a run after.
static void tb_0069_toilet_trace_window() {
  // The change is one-directional: GAME.CON:4750-4777 gates `cactor LIZTROOP` on `ifcount`, so a sitter becomes a
  // walker, never the reverse.
  CHECK(ekm_toilet_transition(EK_PICNUM_TROOP_ON_TOILET, EK_PICNUM_TROOP_STANDING));
  CHECK(ekm_toilet_transition(EK_PICNUM_TROOP_JUST_SIT, EK_PICNUM_TROOP_STANDING));
  CHECK_MSG(!ekm_toilet_transition(EK_PICNUM_TROOP_STANDING, EK_PICNUM_TROOP_ON_TOILET),
            "the other direction is not this window");
  CHECK_MSG(!ekm_toilet_transition(EK_PICNUM_TROOP_STANDING, EK_PICNUM_TROOP_STANDING),
            "and no change is not a transition, so the burst fires once");
  CHECK(!ekm_toilet_transition(EK_PICNUM_TROOP_ON_TOILET, EK_PICNUM_TROOP_ON_TOILET));
  CHECK_MSG(!ekm_toilet_transition(EK_PICNUM_TROOP_ON_TOILET, 1681),
            "1681 is LIZTROOPRUNNING, which the sitter's CON does not hand to");
  CHECK(!ekm_toilet_transition(2000, EK_PICNUM_TROOP_STANDING));
  CHECK(EK_PICNUM_TROOP_STANDING == 1680);     // LIZTROOP, names.h:428

  // The ring, oldest first: 4 held with the next write at 0 walks 0,1,2,3; next write at 2 walks 2,3,0,1.
  const int N = 4;
  CHECK(ekm_ring_oldest(0, 4, N, 0) == 0);
  CHECK(ekm_ring_oldest(0, 4, N, 3) == 3);
  CHECK(ekm_ring_oldest(2, 4, N, 0) == 2);
  CHECK(ekm_ring_oldest(2, 4, N, 1) == 3);
  CHECK_MSG(ekm_ring_oldest(2, 4, N, 2) == 0, "and it wraps to the newest pair");
  CHECK(ekm_ring_oldest(2, 4, N, 3) == 1);

  CHECK_MSG(ekm_ring_oldest(2, 2, N, 0) == 0, "two held, next write at 2: oldest is 0");
  CHECK(ekm_ring_oldest(2, 2, N, 1) == 1);
  CHECK(ekm_ring_oldest(1, 1, N, 0) == 0);
  CHECK(ekm_ring_oldest(0, 1, N, 0) == 3);     // one held, written at 3, head wrapped

  // Nothing to walk is -1, never slot 0: an empty ring must not hand back a zeroed sample to be printed as a frame.
  CHECK_MSG(ekm_ring_oldest(0, 0, N, 0) == -1, "an empty ring has no oldest");
  CHECK(ekm_ring_oldest(0, 4, N, 4) == -1);
  CHECK(ekm_ring_oldest(0, 4, N, -1) == -1);
  CHECK_MSG(ekm_ring_oldest(0, 5, N, 0) == -1, "more held than the ring can hold is a bug, not a wrap");
  CHECK(ekm_ring_oldest(0, 1, 0, 0) == -1);

  CHECK(EK_TOILET_TRACE_BEFORE >= 3);
  CHECK(EK_TOILET_TRACE_AFTER >= EK_TOILET_TRACE_BEFORE);
  CHECK_MSG(EK_TOILET_TRACE_SLOTS >= 1, "at least one sitter tracked at a time");
  // Bounded: one burst cannot spend the session's whole budget.
  CHECK(EK_TOILET_TRACE_LINES > EK_TOILET_TRACE_BEFORE + EK_TOILET_TRACE_AFTER + 1);
}

// The deferral follows the ACTOR, not its picnum.

// The triggerbot's two angles must describe ONE instant.
static void tb_0071_lock_pair_one_instant() {
  const int eyeX = 0, eyeY = 0, eyeZ = 0;
  const int LEVEL = ekm_f16(EKM_HORIZ_LEVEL);

  const EkAimViewPair a = ekm_aim_view_pair(ekm_f16(0), 0, eyeX, eyeY, eyeZ,
                                            1000, 0, 0, 0, 1000);
  CHECK_MSG(a.myYaw == 0, "the player's own yaw comes out of the same call");
  CHECK(a.yaw == ekm_aim_yaw(1000 - eyeX, 0 - eyeY));
  CHECK(a.dist2d == 1000);
  CHECK_MSG(a.q16horiz == LEVEL, "a target level with the eye is level pitch");
  CHECK(ekm_angular_offset(a.myYaw, a.yaw) == 0);

  // Instant B, one tic later: the target crossed 300 units and the player tracked it exactly; both angles moved 95
  // units.
  const int crossedYaw = ekm_aim_yaw(1000, 300);
  CHECK_MSG(crossedYaw == 95, "300 across at 1000 out is 95 Build units of yaw");
  const EkAimViewPair b = ekm_aim_view_pair(ekm_f16(crossedYaw), 0,
                                            eyeX, eyeY, eyeZ,
                                            1000, 300, 0, 0, 1044);
  CHECK(b.myYaw == crossedYaw);
  CHECK(b.yaw == crossedYaw);

  CHECK_MSG(tbot_when_crosshair(true, b.myYaw, b.yaw, LEVEL, b.q16horiz).holds == 1,
            "one instant: the crosshair is on the target and the bot fires");
  const TbotCond mixed = tbot_when_crosshair(true, b.myYaw, a.yaw, LEVEL, a.q16horiz);
  CHECK_MSG(mixed.holds == 0,
            "a tic-old target yaw beside this frame's player yaw misses the cone");
  CHECK(strcmp(mixed.reason, "offyaw") == 0);
  CHECK_MSG(ekm_angular_offset(b.myYaw, a.yaw) > TBOT_CROSSHAIR_YAW_UNITS * 3,
            "and it misses it by four times the cone, as the report measured");

  const EkAimViewPair lowOff = ekm_aim_view_pair(ekm_f16(0), 4096,
                                                 eyeX, eyeY, eyeZ,
                                                 1000, 0, 0, 0, 1000);
  CHECK(lowOff.q16horiz == LEVEL - 4096);
  const EkAimViewPair up = ekm_aim_view_pair(ekm_f16(0), 0, eyeX, eyeY, eyeZ,
                                             1000, 0, -2048, 0, 1000);
  CHECK(up.q16horiz > LEVEL);
  const EkAimViewPair down = ekm_aim_view_pair(ekm_f16(0), 0, eyeX, eyeY, eyeZ,
                                               1000, 0, 2048, 0, 1000);
  CHECK(down.q16horiz < LEVEL);

  // The player's fractional angle is dropped as the engine's own read drops it, and the circle wraps.
  CHECK(ekm_aim_view_pair(ekm_f16(2047) + 65535, 0, 0, 0, 0,
                          1000, 0, 0, 0, 1000).myYaw == 2047);
  CHECK(ekm_aim_view_pair(ekm_f16(2048), 0, 0, 0, 0,
                          1000, 0, 0, 0, 1000).myYaw == 0);
  CHECK(ekm_aim_view_pair(ekm_f16(4096 + 7), 0, 0, 0, 0,
                          1000, 0, 0, 0, 1000).myYaw == 7);

  const EkAimViewPair on = ekm_aim_view_pair(ekm_f16(100), 0, 500, 500, 0,
                                             500, 500, 0, 613, 777);
  CHECK_MSG(on.yaw == 613, "no direction keeps the yaw the caller came in with");
  CHECK_MSG(on.dist2d == 777, "and its range, so the pitch is not solved at zero");
  CHECK(on.myYaw == 100);
}


// What a per-level reset may be keyed on. tracer.cpp's ring of world coordinates, targets.cpp's sticky deferral flag
// and esp.cpp's health maxima are all wrong once a level changes, and there is no level-change callback.
static void tb_0072_level_loading() {
  CHECK_MSG(ekm_level_loading(0x00, true), "gm 0 is a level being built");
  CHECK_MSG(!ekm_level_loading(0x04, true), "MODE_GAME alone is a level running");
  CHECK_MSG(!ekm_level_loading(0x05, true),
            "MODE_GAME|MODE_MENU is a PAUSE and must not reset anything");
  CHECK(!ekm_level_loading(0x14, true));   // MODE_GAME|MODE_TYPE: chat
  CHECK(!ekm_level_loading(0x08, true));   // MODE_EOL
  CHECK(!ekm_level_loading(0x24, true));   // MODE_GAME|MODE_RESTART

  CHECK(!ekm_level_loading(0x00, false));
  CHECK(!ekm_level_loading(0x04, false));

  // The two predicates are not each other's negation: they never both hold, and in some states (the pause menu) neither
  // does.
  bool sawNeither = false;
  for (int gm = 0; gm < 64; gm++) {
    CHECK(!(ekm_level_loading(gm, true) && esp_level_loaded(gm, true)));
    if (!ekm_level_loading(gm, true) && !esp_level_loaded(gm, true)) sawNeither = true;
  }
  CHECK_MSG(sawNeither,
            "a paused level is neither loading nor drawable, and resets neither");
  CHECK_MSG(!ekm_level_loading(0x05, true) && !esp_level_loaded(0x05, true),
            "and the pause menu is exactly that state");
}


static void tb_0073_boss_pal_health() {
  CHECK_MSG(ekm_enemy_ruled_max(2630, 1) == 1000,
            "a pal'd Battlelord is BOSS1PALSTRENGTH, not twice 4500");
  CHECK_MSG(ekm_enemy_ruled_max(2710, 1) == 1,
            "a pal'd Overlord is set to 1 and dies to one hit");
  CHECK(ekm_enemy_ruled_max(2760, 1) == 1);
  CHECK_MSG(ekm_enemy_max_strength(2630) == 4500, "and all three spawn at 4500");
  CHECK(ekm_enemy_max_strength(2710) == 4500);
  CHECK(ekm_enemy_max_strength(2760) == 4500);

  // Any non-zero pal, not 21.
  for (int pal = 1; pal <= 64; pal++) {
    CHECK(ekm_enemy_ruled_max(2630, pal) == 1000);
    CHECK(ekm_enemy_ruled_max(2710, pal) == 1);
    CHECK(ekm_enemy_ruled_max(2760, pal) == 1);
  }
  CHECK_MSG(ekm_enemy_ruled_max(2630, 0) == 0,
            "pal 0 takes the other branch and no rule is pending");
  CHECK(ekm_enemy_ruled_max(2710, 0) == 0);
  CHECK(ekm_enemy_ruled_max(2760, 0) == 0);

  // The STAYPUT variants differ: 2631 BOSS1STAYPUT is `actor BOSS1STAYPUT BOSS1STRENGTH cactor BOSS1 enda`
  // (GAME.CON:6513), with no ifspritepal of its own.
  for (int pal = 0; pal <= 64; pal++) {
    CHECK(ekm_enemy_ruled_max(2631, pal) == 0);
    CHECK(ekm_enemy_ruled_max(2711, pal) == 0);
    CHECK(ekm_enemy_ruled_max(2761, pal) == 0);
  }
  // BOSS4's `ifai 0` (GAME.CON:7916) is `ai AIBOSS4LAYEGGS` and touches no strength: no pal rule for the Alien Queen
  // here.
  CHECK(ekm_enemy_ruled_max(4740, 21) == 0);
  CHECK(ekm_enemy_ruled_max(4741, 21) == 0);

  // The Captain's rule is unchanged.
  CHECK_MSG(ekm_enemy_ruled_max(1680, EKM_PAL_CAPTAIN) == 60,
            "addstrength ADDS: the Captain is still twice its 30");
  CHECK_MSG(ekm_enemy_ruled_max(1680, 20) == 0,
            "and a trooper on any other pal takes no boss's rule by accident");
  CHECK(ekm_enemy_ruled_max(1680, 22) == 0);
  CHECK(ekm_enemy_ruled_max(1680, 0) == 0);

  CHECK_MSG(ekm_display_hp(4500, 4500, 1000, false) == 1000,
            "a dormant pal'd Battlelord reads 4500 and really has 1000");
  CHECK_MSG(ekm_display_hp(4500, 4500, 1, false) == 1,
            "and a dormant pal'd Cycloid really has 1");
  CHECK_MSG(ekm_display_hp(4500, 4500, 1000, true) == 4500,
            "once the CON has run the sprite is the truth, whichever way");
  CHECK_MSG(ekm_display_hp(4200, 4500, 1000, false) == 4200,
            "off the spawn value exactly, nothing is substituted");

  // The ruled maximum never sits low (World Tour esp_pics.cpp:235-240).
  CHECK_MSG(ekm_enemy_bar_max(0, 30) == 0,
            "no rule means no answer, and the caller's learned ceiling stands");
  CHECK(ekm_enemy_bar_max(1000, 1000) == 1000);
  CHECK(ekm_enemy_bar_max(60, 30) == 60);
  CHECK_MSG(ekm_enemy_bar_max(1, 4200) == 4200,
            "a rule that disagrees with the sprite never draws past full");
  for (int hp = 0; hp <= 64; hp++) {
    CHECK(ekm_enemy_bar_max(1, hp) >= hp);
    CHECK(ekm_enemy_bar_max(1000, hp) >= hp);
  }
  CHECK_MSG(ekm_enemy_bar_max(-5, 30) == 0, "a negative rule is no rule");

  // The pair as esp.cpp composes them: a dormant pal'd Cycloid is 1/1, not the 1/4500 the spawn table gives.
  {
    const int ruled = ekm_enemy_ruled_max(2760, 1);
    const int shown = ekm_display_hp(4500, ekm_enemy_max_strength(2760), ruled, false);
    const int maxhp = ekm_enemy_bar_max(ruled, shown);
    CHECK(shown == 1);
    CHECK_MSG(maxhp == 1, "and its maximum is its real one, not the spawn 4500");
    CHECK_MSG(ekm_enemy_shown_max(ekm_enemy_max_strength(2760), shown) == 4500,
              "which is exactly what the table floor would have printed");
  }
}

static void tb_0070_sticky_deferral() {
  // Any statnum inside the world, and deletesprite's parking one: a freed record moves to statnum MAXSTATUS and its
  // picnum is never touched (impact.h), so picnum alone cannot carry identity.
  const int LIVE = 1;
  const int FREED = EK_MAXSTATUS;

  CHECK_MSG(ekm_actor_effective_extra(EK_PICNUM_TROOP_STANDING, 30, 83, 0) == 30,
            "keyed on picnum, the standing trooper gets its health back: the bug");
  CHECK_MSG(ekm_actor_effective_extra(EK_PICNUM_TROOP_STANDING, 30, 83, 1) == 0,
            "sticky to the sprite, the same 83 is still queued and it stays dead");
  CHECK(ekm_actor_effective_extra(EK_PICNUM_TROOP_ON_TOILET, 30, 83, 0) == 0);

  // Set only by a sitter, then carried.
  CHECK(ekm_defer_sticky_next(0, EK_PICNUM_TROOP_ON_TOILET, LIVE, 1, 83) == 1);
  CHECK(ekm_defer_sticky_next(0, EK_PICNUM_TROOP_JUST_SIT, LIVE, 1, 83) == 1);
  CHECK_MSG(ekm_defer_sticky_next(1, EK_PICNUM_TROOP_STANDING, LIVE, 1, 83) == 1,
            "and it survives `cactor LIZTROOP` with the queue still standing");

  // An ordinary trooper that was never sitting is never discounted.
  CHECK_MSG(ekm_defer_sticky_next(0, EK_PICNUM_TROOP_STANDING, LIVE, 1, 83) == 0,
            "a walker with a queue is ordinary lag, not a deferral");
  for (int p = 1; p <= 64; p++) {
    CHECK(ekm_defer_sticky_next(0, EK_PICNUM_TROOP_STANDING, LIVE, 1, p) == 0);
    CHECK(ekm_actor_effective_extra(EK_PICNUM_TROOP_STANDING, 30, p, 0) == 30);
  }

  // Cleared when the queue clears: nothing pending is -1 (EK_HTEXTRA_NONE); zero is a hit for no damage.
  CHECK(ekm_defer_sticky_next(1, EK_PICNUM_TROOP_STANDING, LIVE, 1, EK_HTEXTRA_NONE) == 0);
  CHECK(ekm_defer_sticky_next(1, EK_PICNUM_TROOP_STANDING, LIVE, 1, 0) == 0);
  CHECK(ekm_defer_sticky_next(1, EK_PICNUM_TROOP_ON_TOILET, LIVE, 1, -400) == 0);
  CHECK_MSG(ekm_actor_effective_extra(EK_PICNUM_TROOP_STANDING, 30, EK_HTEXTRA_NONE, 0) == 30,
            "and a trooper that somehow survived the pile reads alive again");

  // A recycled slot: a stale flag would hide a live enemy, worse than the bug.
  CHECK_MSG(ekm_defer_sticky_next(1, EK_PICNUM_TROOP_STANDING, FREED, 0, 0) == 0,
            "a freed slot drops the flag with nothing read at all");
  CHECK(ekm_defer_sticky_next(1, EK_PICNUM_TROOP_ON_TOILET, FREED, 1, 83) == 0);
  CHECK(ekm_defer_sticky_next(1, EK_PICNUM_TROOP_STANDING, -1, 0, 0) == 0);
  CHECK_MSG(ekm_defer_sticky_next(1, 2120, LIVE, 1, 83) == 0,
            "an Enforcer in the slot is not the trooper that set the flag");
  CHECK_MSG(ekm_defer_sticky_next(1, 1744, LIVE, 1, 83) == 0,
            "nor a ducking trooper: a sitter can only ever become 1680");
  CHECK(ekm_defer_sticky_next(1, 0, LIVE, 0, 0) == 0);

  // The discount is scoped to the three tiles, flag or no flag, so an orphaned flag can only touch a LIZTROOP.
  CHECK(ekm_actor_discounts_pending(EK_PICNUM_TROOP_ON_TOILET, 0));
  CHECK(ekm_actor_discounts_pending(EK_PICNUM_TROOP_JUST_SIT, 0));
  CHECK(ekm_actor_discounts_pending(EK_PICNUM_TROOP_STANDING, 1));
  CHECK(!ekm_actor_discounts_pending(EK_PICNUM_TROOP_STANDING, 0));
  CHECK_MSG(!ekm_actor_discounts_pending(2120, 1),
            "the flag can never widen the discount past the trooper tiles");
  CHECK(ekm_actor_effective_extra(2120, 100, 100, 1) == 100);

  // A failed htextra read guesses neither way: the discount needs a successful `pending > 0`, so a held flag is inert,
  // while clearing would retire a real deferral.
  CHECK(ekm_defer_sticky_next(1, EK_PICNUM_TROOP_STANDING, LIVE, 0, EK_HTEXTRA_NONE) == 1);
  CHECK(ekm_defer_sticky_next(0, EK_PICNUM_TROOP_ON_TOILET, LIVE, 0, EK_HTEXTRA_NONE) == 0);
  CHECK_MSG(ekm_actor_effective_extra(EK_PICNUM_TROOP_STANDING, 30, EK_HTEXTRA_NONE, 1) == 30,
            "held, and inert: nothing is queued that a discount could take off");

  for (int p = -8; p <= 0; p++) {
    CHECK(ekm_actor_effective_extra(EK_PICNUM_TROOP_ON_TOILET, 30, p, 1) == 30);
    CHECK(ekm_actor_effective_extra(EK_PICNUM_TROOP_STANDING, 30, p, 1) == 30);
    CHECK(ekm_actor_effective_extra(EK_PICNUM_TROOP_STANDING, 30, p, 0) == 30);
  }

  // The ESP must not subtract twice: esp.cpp derives pending as `hp - hpShown` from the collector's two numbers, so the
  // display number must already carry the discount wherever the targeting number does.
  for (int sticky = 0; sticky <= 1; sticky++) {
    const int pic = EK_PICNUM_TROOP_STANDING;
    const int hp = ekm_actor_effective_extra(pic, 30, 83, sticky);
    const int hpShown = ekm_actor_shown_extra(
        hp, ekm_actor_discounts_pending(pic, sticky) ? EK_HTEXTRA_NONE : 83);
    const int pendingSeen = hp - hpShown;
    CHECK_MSG(hpShown == 0, "either way the ESP shows a trooper with 83 queued as dead");
    CHECK_MSG(ekm_actor_shown_extra(hp, pendingSeen) == 0,
              "and subtracting the seen difference again cannot go below it");
    CHECK_MSG(sticky ? pendingSeen == 0 : pendingSeen == 30,
              "the discount is taken exactly once, and the ESP sees the remainder");
  }
}

static void tb_0074_cvar_write_rules() {
  EkCvar maxsaves = { "cl_maxautosaves", "", 0, EK_CVAR_INT, 1, 100 };
  CHECK_MSG(cvar_clamped(maxsaves, 0) == 1, "below the record's vmin lands ON vmin");
  CHECK_MSG(cvar_clamped(maxsaves, 101) == 100, "above vmax lands ON vmax");
  CHECK(cvar_clamped(maxsaves, 1) == 1);
  CHECK(cvar_clamped(maxsaves, 100) == 100);
  CHECK(cvar_clamped(maxsaves, 50) == 50);
  CHECK_MSG(cvar_clamped(maxsaves, -2147483647 - 1) == 1,
            "and the extremes land on the ends rather than wrapping");
  CHECK(cvar_clamped(maxsaves, 2147483647) == 100);

  // The bounds come out of the record: the same value through two records lands in two places, and the writer knows
  // neither 7 nor 100.
  EkCvar wswitch = { "cl_weaponswitch", "", 0, EK_CVAR_INT | 0x0400, 0, 7 };
  EkCvar autosave = { "cl_autosave", "", 0, EK_CVAR_BOOL, 0, 1 };
  CHECK(cvar_clamped(wswitch, 9) == 7);
  CHECK(cvar_clamped(autosave, 9) == 1);
  CHECK_MSG(cvar_clamped(maxsaves, 9) == 9,
            "one value, three records, three answers: the record is the authority");
  for (int v = -8; v <= 120; v++) {
    CHECK(cvar_clamped(maxsaves, v) >= maxsaves.vmin && cvar_clamped(maxsaves, v) <= maxsaves.vmax);
    CHECK(cvar_clamped(wswitch, v) >= 0 && cvar_clamped(wswitch, v) <= 7);
  }

  CHECK(cvar_type_writable(EK_CVAR_INT));
  CHECK(cvar_type_writable(EK_CVAR_UINT));
  CHECK(cvar_type_writable(EK_CVAR_BOOL));
  CHECK_MSG(cvar_type_writable(EK_CVAR_INT | 0x0400),
            "cl_weaponswitch carries CVAR_MULTI beside its type and is still an int");
  CHECK_MSG(!cvar_type_writable(EK_CVAR_STRING),
            "a string cvar is not four bytes and must never be stored into as one");
  CHECK(!cvar_type_writable(EK_CVAR_FLOAT));
  CHECK(!cvar_type_writable(EK_CVAR_DOUBLE));
  CHECK(!cvar_type_writable(0));

  CHECK_MSG(!cvar_row_available(0),
            "no record means the row is unavailable -- absent AND ambiguous");
  CHECK(cvar_row_available(&maxsaves));
  CHECK(cvar_row_available(&autosave));
  EkCvar named = { "hud_scale", "", 0, EK_CVAR_STRING, 0, 0 };
  CHECK_MSG(!cvar_row_available(&named),
            "a record this writer cannot write is unavailable too, not merely unwritten");
}

// cl_weaponswitch is one engine int of three independent bits ("0: disabled, 1: if new, 2: if empty, 4.
static void tb_0075_weapon_switch_mask() {
  CHECK(!wswitch_bit_on(0, EK_WSWITCH_NEW));
  CHECK(!wswitch_bit_on(0, EK_WSWITCH_EMPTY));
  CHECK(!wswitch_bit_on(0, EK_WSWITCH_WCHOICE));
  CHECK_MSG(wswitch_bit_on(3, EK_WSWITCH_NEW) && wswitch_bit_on(3, EK_WSWITCH_EMPTY) &&
            !wswitch_bit_on(3, EK_WSWITCH_WCHOICE),
            "3 is the engine's own default: on pickup and when empty, no wchoice");
  CHECK(wswitch_bit_on(4, EK_WSWITCH_WCHOICE) && !wswitch_bit_on(4, EK_WSWITCH_NEW));
  CHECK(wswitch_bit_on(7, EK_WSWITCH_NEW) && wswitch_bit_on(7, EK_WSWITCH_EMPTY) &&
        wswitch_bit_on(7, EK_WSWITCH_WCHOICE));

  static const int BITS[3] = { EK_WSWITCH_NEW, EK_WSWITCH_EMPTY, EK_WSWITCH_WCHOICE };
  for (int m = 0; m <= EK_WSWITCH_MASK; m++) {
    for (int b = 0; b < 3; b++) {
      for (int on = 0; on <= 1; on++) {
        const int out = wswitch_with_bit(m, BITS[b], on);
        CHECK_MSG(out >= 0 && out <= EK_WSWITCH_MASK,
                  "no combination of the three rows can escape the engine's 0..7");
        CHECK_MSG(wswitch_bit_on(out, BITS[b]) == on, "the row's own bit took");
        for (int o = 0; o < 3; o++)
          if (o != b)
            CHECK_MSG(wswitch_bit_on(out, BITS[o]) == wswitch_bit_on(m, BITS[o]),
                      "and the other two rows are untouched");
      }
    }
  }

  int v = 0;
  v = wswitch_with_bit(v, EK_WSWITCH_NEW, 1);      CHECK(v == 1);
  v = wswitch_with_bit(v, EK_WSWITCH_EMPTY, 1);    CHECK(v == 3);
  v = wswitch_with_bit(v, EK_WSWITCH_WCHOICE, 1);  CHECK(v == 7);
  v = wswitch_with_bit(v, EK_WSWITCH_NEW, 0);      CHECK(v == 6);
  v = wswitch_with_bit(v, EK_WSWITCH_EMPTY, 0);    CHECK(v == 4);
  v = wswitch_with_bit(v, EK_WSWITCH_WCHOICE, 0);
  CHECK_MSG(v == 0, "all three off is the engine's own 0: disabled");

  // A hand-edited or stale value is clamped, not masked, agreeing with the registry's own vmin/vmax (cvar_clamped).
  EkCvar wswitch = { "cl_weaponswitch", "", 0, EK_CVAR_INT | 0x0400, 0, 7 };
  for (int raw = -4; raw <= 20; raw++)
    CHECK_MSG(wswitch_mask_clamped(raw) == cvar_clamped(wswitch, raw),
              "the panel's clamp and the registry's clamp are the same clamp");
  CHECK(wswitch_mask_clamped(-1) == 0);
  CHECK_MSG(wswitch_mask_clamped(8) == 7,
            "8 clamps DOWN to 7, it does not mask to 0 and silently disable switching");
  CHECK(wswitch_bit_on(-1, EK_WSWITCH_NEW) == 0);
  CHECK(wswitch_with_bit(99, EK_WSWITCH_NEW, 0) == 6);
}

// The pickup option is ONE three-state setting, not two toggles.
static void tb_0078_weapon_pickup_state() {
  // The three states the engine writes.
  CHECK(wswitch_with_pickup(0, EK_PICKUP_NEVER) == 0);
  CHECK(wswitch_with_pickup(0, EK_PICKUP_ALWAYS) == 1);
  CHECK_MSG(wswitch_with_pickup(0, EK_PICKUP_IF_BETTER) == (1 | 4),
            "the wchoice state is 1|4, never 4 on its own");

  // The engine's getter, (ws & 1) ?
  for (int m = 0; m <= EK_WSWITCH_MASK; m++) {
    const int expect = (m & 1) ? ((m & 4) ? EK_PICKUP_IF_BETTER : EK_PICKUP_ALWAYS)
                               : EK_PICKUP_NEVER;
    CHECK_MSG(wswitch_pickup_state(m) == expect,
              "the panel decodes the pair exactly as menus.cpp:4493 does");
  }
  CHECK_MSG(wswitch_pickup_state(4) == EK_PICKUP_NEVER,
            "bit 4 alone is what the engine reads as never, not as if-better");

  // Round trip: every state survives a read back, and no state this row writes is one the engine's menu cannot write.
  for (int m = 0; m <= EK_WSWITCH_MASK; m++)
    for (int st = EK_PICKUP_NEVER; st < EK_PICKUP_STATES; st++) {
      const int out = wswitch_with_pickup(m, st);
      CHECK_MSG(wswitch_pickup_state(out) == st, "the state written is the state read");
      CHECK_MSG((out & EK_WSWITCH_PICKUP_MASK) != EK_WSWITCH_WCHOICE,
                "no write of this row can leave bit 4 standing alone");
      CHECK_MSG(out >= 0 && out <= EK_WSWITCH_MASK, "and it stays inside the cvar's 0..7");
    }

  for (int empty = 0; empty <= 1; empty++)
    for (int st = EK_PICKUP_NEVER; st < EK_PICKUP_STATES; st++) {
      const int m = wswitch_with_bit(0, EK_WSWITCH_EMPTY, empty);
      const int out = wswitch_with_pickup(m, st);
      CHECK_MSG(wswitch_bit_on(out, EK_WSWITCH_EMPTY) == empty,
                "switch when empty is untouched by the pickup row");
      CHECK_MSG(wswitch_bit_on(wswitch_with_bit(out, EK_WSWITCH_EMPTY, 1 - empty),
                               EK_WSWITCH_EMPTY) == 1 - empty,
                "and the empty row still moves its own bit afterwards");
      CHECK_MSG(wswitch_pickup_state(wswitch_with_bit(out, EK_WSWITCH_EMPTY, 1 - empty)) == st,
                "with the pickup state unchanged by the empty row");
    }

  CHECK(wswitch_pickup_state(-1) == EK_PICKUP_NEVER);
  CHECK_MSG(wswitch_pickup_state(99) == EK_PICKUP_IF_BETTER,
            "99 clamps to 7, which the engine reads as if-better");
  CHECK(wswitch_with_pickup(99, EK_PICKUP_NEVER) == 2);

  for (int st = EK_PICKUP_NEVER; st < EK_PICKUP_STATES; st++) {
    const char* n = wswitch_pickup_name(st);
    CHECK_MSG(n && n[0], "every state the row can show has a name");
  }
}

// The timed autosave's clock, ported from World Tour's autosave.h (Megaton's verbatim).
static void tb_0076_autosave_clock() {
  AutosaveState s = { 0, 0 };

  // The first call ever never saves.
  CHECK_MSG(autosave_step(&s, 1, 1000, 0, 1) == 0,
            "the first frame arms the clock and saves nothing on it");
  CHECK(s.armed == 1 && s.lastMs == 1000);
  CHECK_MSG(autosave_step(&s, 1, 1000 + 60000, 0, 1) == 1,
            "and one whole minute later it is due");
  CHECK(s.lastMs == 61000);
  CHECK_MSG(autosave_step(&s, 1, 61000 + 59999, 0, 1) == 0, "a millisecond short is not due");
  CHECK(autosave_step(&s, 1, 61000 + 60000, 0, 1) == 1);

  AutosaveState L = { 0, 0 };
  CHECK(autosave_step(&L, 1, 0, 0, 1) == 0);
  CHECK(autosave_step(&L, 1, 60000, 0, 1) == 1);          // due, taken
  CHECK_MSG(autosave_step(&L, 1, 60001, 1, 1) == 0, "the load itself never saves");
  CHECK(L.lastMs == 60001);
  CHECK_MSG(autosave_step(&L, 1, 70000, 1, 1) == 0, "nor any later frame of it");
  CHECK_MSG(L.lastMs == 70000,
            "the clock is held at the LAST frame of the load, so the interval "
            "starts when the level finishes rather than when it started");
  CHECK(autosave_step(&L, 1, 70000 + 59999, 0, 1) == 0);
  CHECK(autosave_step(&L, 1, 70000 + 60000, 0, 1) == 1);

  // Off holds the clock at now, so turning the row up gives a full interval, not a save for time spent off.
  AutosaveState O = { 0, 0 };
  CHECK(autosave_step(&O, 0, 0, 0, 1) == 0);
  for (unsigned t = 0; t <= 600000u; t += 60000u) CHECK(autosave_step(&O, 0, t, 0, 1) == 0);
  CHECK_MSG(O.lastMs == 600000u, "and it kept the clock with it the whole time");
  CHECK_MSG(autosave_step(&O, 1, 600000u + 1, 0, 1) == 0,
            "so the frame the row is turned up owes nothing");
  CHECK(autosave_step(&O, 1, 600000u + 60000u, 0, 1) == 1);
  CHECK_MSG(autosave_step(&O, -5, 900000u, 0, 1) == 0, "a negative interval is off, not due");

  // Out of play the clock runs on and only the save waits, so one that fell due during a pause lands on the first frame
  // back.
  AutosaveState P = { 0, 0 };
  CHECK(autosave_step(&P, 1, 0, 0, 1) == 0);
  for (unsigned t = 60000u; t <= 300000u; t += 60000u)
    CHECK_MSG(autosave_step(&P, 1, t, 0, 0) == 0, "nothing is saved out of play");
  CHECK_MSG(P.lastMs == 0, "and the clock was never touched while it waited");
  CHECK_MSG(autosave_step(&P, 1, 300001u, 0, 1) == 1, "the first frame back takes it");

  CHECK(autosave_mins_clamped(-1) == AUTOSAVE_MINS_MIN);
  CHECK(autosave_mins_clamped(0) == 0);
  CHECK(autosave_mins_clamped(31) == AUTOSAVE_MINS_MAX);
  CHECK(autosave_mins_clamped(30) == 30);
  CHECK_MSG(AUTOSAVE_MINS_DEFAULT == 0,
            "off by default: a feature that saves unasked has to be asked first");
  for (int m = -4; m <= 40; m++) {
    const int c = autosave_mins_clamped(m);
    CHECK(c >= AUTOSAVE_MINS_MIN && c <= AUTOSAVE_MINS_MAX);
  }

  AutosaveState C = { 0, 0 };
  CHECK(autosave_step(&C, 60, 0, 0, 1) == 0);
  CHECK(autosave_step(&C, 60, 30u * 60000u, 0, 1) == 1);

  // wallclock_ms is GetTickCount and wraps; every age is an UNSIGNED difference, exact across the wrap, where a signed
  // one would stall the timer for 49 days.
  AutosaveState W = { 0xFFFFF000u, 1 };
  CHECK_MSG(autosave_step(&W, 1, 0xFFFFF000u + 59999u, 0, 1) == 0,
            "a minute straddling the wrap is not short-changed");
  CHECK_MSG(autosave_step(&W, 1, 0xFFFFF000u + 60000u, 0, 1) == 1,
            "and it comes due exactly on time on the other side of it");

  CHECK_MSG(autosave_step(0, 1, 0, 0, 1) == 0, "no state is never due");
}

// What counts as in play: the engine's quicksave refuses over a dead player (game.cpp:5051-5055, QUOTE_SAVE_DEAD) and
// acts only while gm has MODE_GAME, so do not ask for a save the game would discard.
static void tb_0077_autosave_in_play() {
  CHECK_MSG(autosave_in_play(1, EK_MODE_GAME, 1, 100, 0) == 1, "an ordinary live frame");

  CHECK_MSG(autosave_in_play(1, EK_MODE_GAME | EK_MODE_MENU, 1, 100, 0) == 0,
            "not over the game's own pause menu");
  CHECK_MSG(autosave_in_play(1, 0x14, 1, 100, 0) == 0, "not while the player is typing");
  CHECK_MSG(autosave_in_play(1, 0x00, 1, 100, 0) == 0, "not while a level is being built");
  CHECK(autosave_in_play(1, 0x08, 1, 100, 0) == 0);   // MODE_EOL
  CHECK(autosave_in_play(1, 0x24, 1, 100, 0) == 0);   // MODE_GAME|MODE_RESTART
  CHECK(autosave_in_play(1, 0x02, 1, 100, 0) == 0);   // MODE_DEMO

  // Dead is dead: the quicksave refuses, and a save on the tic the player dies would overwrite the slot with the death
  // they are about to reload away from.
  CHECK_MSG(autosave_in_play(1, EK_MODE_GAME, 1, 0, 0) == 0, "not at zero health");
  CHECK(autosave_in_play(1, EK_MODE_GAME, 1, -40, 0) == 0);
  CHECK(autosave_in_play(1, EK_MODE_GAME, 1, 1, 0) == 1);

  // A failed read is not a yes.
  CHECK_MSG(autosave_in_play(0, EK_MODE_GAME, 1, 100, 0) == 0,
            "gm unreadable: the feature stands down rather than guessing");
  CHECK_MSG(autosave_in_play(1, EK_MODE_GAME, 0, 100, 0) == 0,
            "health unreadable likewise, whatever the stale number says");

  CHECK_MSG(autosave_in_play(1, EK_MODE_GAME, 1, 100, 1) == 0, "not with the panel open");

  int pass = 0;
  for (int gm = 0; gm < 64; gm++) if (autosave_in_play(1, gm, 1, 100, 0)) pass++;
  CHECK_MSG(pass == 1, "exactly one of the 64 gm states is a frame in play");
}

// The autosave's name is not optional.
static void tb_0079_autosave_name() {
  // The engine's own layout.
  CHECK_MSG(EK_SAVEBRIEF_NAME_CAP == 32,
            "savebrief_t::name is char[MAXSAVEGAMENAMESTRUCT] (savegame.h:37,88)");
  CHECK_MSG(EK_SAVEBRIEF_PATH_OFF == EK_SAVEBRIEF_NAME_CAP,
            "path follows name with nothing between (savegame.h:88-89)");

  // What the engine will keep, and not.
  char full[EK_SAVEBRIEF_NAME_CAP];
  for (int i = 0; i < EK_SAVEBRIEF_NAME_CAP; i++) full[i] = 'x';
  CHECK_MSG(!autosave_name_ok(""), "an empty name is the one the engine throws away");
  CHECK(!autosave_name_ok(0));
  CHECK_MSG(!autosave_name_ok(full), "and so is one with no terminator inside the field");
  CHECK(autosave_name_ok("Autosave"));

  // gameexec.cpp:5344's format, byte for byte.
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_mday = 16; t.tm_mon = 8; t.tm_year = 126; t.tm_hour = 13; t.tm_min = 5;
  t.tm_wday = 3; t.tm_yday = 258;
  char name[EK_SAVEBRIEF_NAME_CAP];
  CHECK(autosave_name_from_tm(name, (int)sizeof(name), &t));
  CHECK_MSG(strcmp(name, "16 Sep 2026 01:05PM") == 0,
            "the name is the engine's own \"%d %b %Y %I:%M%p\"");
  CHECK(autosave_name_ok(name));

  // It is never allowed to come out empty.
  CHECK_MSG(autosave_name_from_tm(name, (int)sizeof(name), 0) &&
            autosave_name_ok(name),
            "with no clock it still writes a name the engine will keep");
  CHECK_MSG(!autosave_name_from_tm(name, EK_SAVEBRIEF_NAME_CAP - 1, &t),
            "a buffer shorter than the engine's field is refused, not truncated into");
  CHECK(!autosave_name_from_tm(0, EK_SAVEBRIEF_NAME_CAP, &t));

  // Every minute of one day, so no hour or minute yields a name the engine would discard.
  static const int MINS[3] = { 0, 5, 59 };
  for (int h = 0; h < 24; h++)
    for (int mi = 0; mi < 3; mi++) {
      memset(&t, 0, sizeof(t));
      t.tm_mday = 1; t.tm_mon = 0; t.tm_year = 126;
      t.tm_hour = h; t.tm_min = MINS[mi];
      CHECK(autosave_name_from_tm(name, (int)sizeof(name), &t));
      CHECK_MSG(autosave_name_ok(name), "every hour of the day fits the engine's field");
    }
}

// The free camera's own arithmetic; nothing here touches engine memory.
static void tb_0080_freecam_step() {
  // Speed is per tic and this game's tic is 30 Hz, not World Tour's 26: 100% is BUILD_UNITS_PER_METRE/4 = 128 units/tic
  // = 3840 units/s.
  CHECK(FREECAM_BASE_SPEED == 128);
  CHECK(freecam_speed(100, false, false) == 128);
  CHECK_MSG(freecam_speed(100, true, false) == 256, "fast doubles");
  CHECK_MSG(freecam_speed(100, false, true) == 64, "slow halves");
  CHECK_MSG(freecam_speed(100, true, true) == 128,
            "fast and slow together cancel rather than compounding");
  CHECK(freecam_speed_clamped(0) == FREECAM_SPEED_MIN);
  CHECK(freecam_speed_clamped(9999) == FREECAM_SPEED_MAX);

  FreecamState s = {};
  s.active = true;
  s.x = 0; s.y = 0; s.z = 0; s.ang = 0; s.pitch = 0;
  FreecamInput in = {};
  in.fwd = true;
  FreecamSens sens = freecam_sens_default();
  freecam_step(&s, in, 100, sens);
  CHECK_MSG(s.x == 128 && s.y == 0, "ang 0 moves along +x only");

  // Vertical travel is scaled by Build's z:xy ratio, or up feels wrong against forward.
  FreecamState u = {};
  u.active = true;
  FreecamInput up = {};
  up.up = true;
  freecam_step(&u, up, 100, sens);
  CHECK_MSG(u.z == -128 * FREECAM_Z_PER_XY,
            "up is 16x the horizontal step, and negative z is up in Build");

  // An inactive camera does not integrate.
  FreecamState off = {};
  off.active = false;
  FreecamInput all = {};
  all.fwd = true; all.up = true; all.dx = 500;
  freecam_step(&off, all, 100, sens);
  CHECK_MSG(off.x == 0 && off.z == 0 && off.ang == 0,
            "an inactive camera ignores every input");

  // Yaw wraps into 0..2047 and the remainder is carried, or a slow mouse never turns the camera.
  FreecamState y = {};
  y.active = true;
  y.ang = 2047;
  FreecamInput turn = {};
  turn.dx = 1;
  freecam_step(&y, turn, 100, sens);
  CHECK_MSG(y.ang >= 0 && y.ang < FREECAM_ANG_UNITS, "yaw stays in range");

  // Pitch converts to the engine's horiz, the reason the camera can look straight down. horiz is a SLOPE.
  CHECK_MSG(freecam_pitch_to_horiz(0) == FREECAM_HORIZ_LEVEL,
            "zero pitch is level");
  // Named pu/pn, not up/dn: a local FreecamInput named "up" exists above and MSVC rejects the redeclaration.
  const int pu = freecam_pitch_to_horiz(FREECAM_PITCH_MAX);
  const int pn = freecam_pitch_to_horiz(-FREECAM_PITCH_MAX);
  CHECK_MSG(pu > FREECAM_HORIZ_LEVEL && pn < FREECAM_HORIZ_LEVEL,
            "positive pitch looks up, negative looks down");
  CHECK_MSG(pu > 299, "the camera's full pitch is past what the player may hold");
  CHECK(freecam_horiz_to_pitch(FREECAM_HORIZ_LEVEL) == 0);
  const int rt = freecam_horiz_to_pitch(freecam_pitch_to_horiz(100));
  CHECK_MSG(rt >= 99 && rt <= 101, "a round trip lands within one unit");
}

static void tb_0081_freecam_gates() {
  // Enter needs the row on, a key edge, no menu, a live player and a valid read; each is a refusal alone.
  CHECK(freecam_enter_rule(true, true, false, false, true) == true);
  CHECK_MSG(freecam_enter_rule(false, true, false, false, true) == false,
            "the row being off refuses");
  CHECK_MSG(freecam_enter_rule(true, false, false, false, true) == false,
            "a held key is not an edge");
  CHECK_MSG(freecam_enter_rule(true, true, true, false, true) == false,
            "not from inside a menu");
  CHECK_MSG(freecam_enter_rule(true, true, false, true, true) == false,
            "not while dead");
  CHECK_MSG(freecam_enter_rule(true, true, false, false, false) == false,
            "not on an unreadable player -- 'nothing known' is not 'go ahead'");

  // Exit is deliberately easier: a key edge while active, and the menu does not block it.
  CHECK(freecam_exit_rule(true, true, false) == true);
  CHECK_MSG(freecam_exit_rule(true, true, true) == true,
            "the menu does not trap the camera");
  CHECK(freecam_exit_rule(false, true, false) == false);
  CHECK(freecam_exit_rule(true, false, false) == false);

  CHECK(freecam_force_off_rule(true, false, false, true) == true);
  CHECK(freecam_force_off_rule(true, true, true, true) == true);
  CHECK(freecam_force_off_rule(true, true, false, false) == true);
  CHECK_MSG(freecam_force_off_rule(true, true, false, true) == false,
            "a healthy flying camera is left alone");
  CHECK_MSG(freecam_force_off_rule(false, false, true, false) == false,
            "a camera that is not active cannot be forced off");
}

static void tb_0082_freecam_seqlock() {
  CHECK(freecam_seq_write_begin(0) == 1);
  CHECK(freecam_seq_write_end(1) == 2);
  CHECK((freecam_seq_write_begin(2) & 1u) == 1u);

  // A read is good only if the counter was even and did not move.
  CHECK_MSG(freecam_seq_read_ok(2, 2) == true, "even and unchanged is a good read");
  CHECK_MSG(freecam_seq_read_ok(3, 3) == false,
            "odd means a write was in flight the whole time");
  CHECK_MSG(freecam_seq_read_ok(2, 4) == false,
            "a write started and finished underneath the reader");
  CHECK_MSG(freecam_seq_read_ok(2, 3) == false, "a write started underneath");

  // Wrap must not manufacture a good read: the counter is unsigned and wraps; equality matters, not ordering.
  CHECK(freecam_seq_read_ok(0xFFFFFFFEu, 0xFFFFFFFEu) == true);
  CHECK(freecam_seq_read_ok(0xFFFFFFFFu, 0xFFFFFFFFu) == false);
  CHECK(freecam_seq_write_end(0xFFFFFFFFu) == 0u);

  // The pairs that STRADDLE the wrap (the pairs above are same-side and pass an `after <= before` implementation as
  // readily).
  CHECK_MSG(freecam_seq_read_ok(0xFFFFFFFEu, 0u) == false,
            "a write that started and finished across the wrap is not a good read");
  CHECK_MSG(freecam_seq_read_ok(0u, 0xFFFFFFFEu) == false,
            "and the same pair the other way round is no better");
}

static void tb_0083_freecam_player_offsets() {
  CHECK(EK_PS_OQ16HORIZOFF == 76);
  CHECK_MSG(EK_PS_OQ16HORIZOFF == EK_PS_OQ16HORIZ + 4 &&
            EK_PS_OQ16HORIZOFF + 4 == EK_PS_Q16ANG,
            "one fix16_t between oq16horiz and q16ang, and room for nothing else");
  CHECK(EK_PS_LOOK_ANG == 406);
  CHECK_MSG(EK_PS_LOOK_ANG == EK_PS_CURSECTNUM + 2,
            "look_ang is the int16 immediately after cursectnum");
  CHECK(EK_PS_OLOOK_ANG == 372);
  CHECK_MSG(EK_PS_CURSECTNUM - EK_PS_OLOOK_ANG == 16 * 2,
            "sixteen int16 fields from olook_ang forward to cursectnum");
  CHECK(EK_PS_OPYOFF == 490);
  CHECK_MSG(EK_PS_OPYOFF == EK_PS_PYOFF + 2 && EK_PS_I - EK_PS_PYOFF == 14 * 2,
            "opyoff follows pyoff, and pyoff to i is fourteen int16 fields");
}

static void tb_0084_freecam_frame_gates() {
  CHECK(freecam_draw_reachable(-1, 0) == true);
  CHECK_MSG(freecam_draw_reachable(0, 0) == false,
            "newowner 0 is a viewscreen: game.cpp:998 draws from the camera sprite");
  CHECK_MSG(freecam_draw_reachable(12, 0) == false, "any newowner at all refuses");
  CHECK_MSG(freecam_draw_reachable(-1, 1) == false,
            "third person drops the camera 3072 and runs G_DoThirdPerson on it");

  // The two offsets the gates read, pinned as a number and as the walk that derived it.
  CHECK(EK_PS_NEWOWNER == 492);
  CHECK_MSG(EK_PS_NEWOWNER == EK_PS_OPYOFF + 2 && EK_PS_I - EK_PS_NEWOWNER == 12 * 2,
            "newowner follows opyoff, and newowner to i is twelve int16 fields");
  CHECK(EK_PS_OVER_SHOULDER_ON == 608);
  CHECK_MSG(EK_PS_OVER_SHOULDER_ON - EK_PS_GM == 10 &&
            EK_PS_WEAPON_POS - EK_PS_OVER_SHOULDER_ON == 25,
            "ten uint8 fields on from gm, and twenty-five back from weapon_pos");
}

static void tb_0086_freecam_sector_pick() {
  const FreecamSectorPick a = freecam_sector_pick(true, 7, 3, 4096);
  CHECK(a.sect == 7);
  CHECK_MSG(a.last == 7,
            "the camera's own sector seeds the next frame, not the player's");

  // A failed resolve keeps the last sector and substitutes anyway.
  const FreecamSectorPick b = freecam_sector_pick(false, -1, 3, 4096);
  CHECK_MSG(b.sect == 3, "a point in no sector draws from the last good one");
  CHECK_MSG(b.last == 3, "and that sector stays the seed");

  CHECK_MSG(freecam_sector_pick(true, 4096, 3, 4096).sect == 3,
            "at the bound is past it");
  CHECK(freecam_sector_pick(true, 9999, 3, 4096).sect == 3);
  CHECK(freecam_sector_pick(true, -1, 3, 4096).sect == 3);
  CHECK(freecam_sector_pick(true, -5, 3, 4096).sect == 3);
  CHECK_MSG(freecam_sector_pick(true, 4096, 3, 4096).last == 3,
            "a value the engine never produced never becomes the seed");

  // No valid last is the only refusal left, reachable only before the flight's first successful resolve.
  const FreecamSectorPick d = freecam_sector_pick(false, -1, -1, 4096);
  CHECK_MSG(d.sect == -1, "nothing valid to draw from is the one refusal");
  CHECK(d.last == -1);
  CHECK_MSG(freecam_sector_pick(false, -1, 4096, 4096).sect == -1,
            "an out-of-range last is no better than none");
  CHECK_MSG(freecam_sector_pick(true, 9, -1, 4096).sect == 9,
            "the first resolve of a flight needs no last at all");

  // In, through the wall, further out, back in: once the camera has been in a sector the view never reverts to the
  // player's, and picks the real sector up when the point resolves.
  int last = 5;
  const int ok[4]   = { 1, 0, 0, 1 };
  const int got[4]  = { 11, -1, -1, 12 };
  const int want[4] = { 11, 11, 11, 12 };
  for (int i = 0; i < 4; i++) {
    const FreecamSectorPick r = freecam_sector_pick(ok[i] != 0, got[i], last, 4096);
    CHECK(r.sect == want[i]);
    last = r.last;
  }
  CHECK_MSG(last == 12, "the seed follows the camera, not the player");

  // The bound is the engine's own, and a degenerate one leaves nothing valid.
  CHECK(freecam_sector_pick(true, 0, 0, EK_MAXSECTORS).sect == 0);
  CHECK(freecam_sector_pick(true, 0, 0, 0).sect == -1);
}

// WHICH drawrooms call is the player's view. polymer_drawrooms is reached from ten `call rel32` sites in six functions
// (mirrors, three TROR passes, a viewscreen sprite, the player's view).
static void tb_0087_freecam_player_view() {
  // The span half is half-open: a RUNTIME_FUNCTION's EndAddress is the first byte PAST the function.
  CHECK(freecam_rva_in_span(0x0031A20, 0x0031A20, 0x003385E));
  CHECK(freecam_rva_in_span(0x003298A, 0x0031A20, 0x003385E));
  CHECK_MSG(freecam_rva_in_span(0x003385E, 0x0031A20, 0x003385E) == false,
            "the end address is the first byte past the function");
  CHECK_MSG(freecam_rva_in_span(0x0031A1F, 0x0031A20, 0x003385E) == false,
            "one byte before the entry is another function");

  // A failed locate hands 0/0 through here and an empty span must match nothing, not everything, or a refused locate
  // would substitute into every draw call.
  CHECK_MSG(freecam_rva_in_span(0x0031A20, 0, 0) == false,
            "a failed locate matches nothing");
  CHECK_MSG(freecam_rva_in_span(0, 0, 0) == false, "not even rva zero");
  CHECK_MSG(freecam_rva_in_span(7, 10, 4) == false,
            "an inverted span matches nothing");

  const FreecamViewArgs cam = { 100, 200, 300, 0x40000, 0x640000, 12 };
  FreecamViewArgs same = cam;
  CHECK(freecam_same_view(same, cam));

  FreecamViewArgs t = cam; t.x = 101;
  CHECK_MSG(freecam_same_view(t, cam) == false, "x alone decides it");
  t = cam; t.y = 201;
  CHECK_MSG(freecam_same_view(t, cam) == false, "y alone decides it");
  t = cam; t.z = 300 - 1024;
  CHECK_MSG(freecam_same_view(t, cam) == false,
            "game.cpp:870 passes the sprite's z less ZOFFSET6");
  t = cam; t.angFix = 0x40001;
  CHECK_MSG(freecam_same_view(t, cam) == false, "the angle alone decides it");
  t = cam; t.horizFix = 0x650000;
  CHECK_MSG(freecam_same_view(t, cam) == false, "the horiz alone decides it");
  t = cam; t.sect = 12 + 4096;
  CHECK_MSG(freecam_same_view(t, cam) == false,
            "a mirror pass adds MAXSECTORS to the sector");

  // And only both together.
  CHECK(freecam_is_player_view(0x003298A, 0x0031A20, 0x003385E, same, cam));
  CHECK_MSG(freecam_is_player_view(0x0032042, 0x0031A20, 0x003385E, t, cam) == false,
            "the second call G_DrawRooms makes is not the player's view");
  CHECK_MSG(freecam_is_player_view(0x0031706, 0x0031A20, 0x003385E, same, cam) == false,
            "another owner handed the same six values is still not it");
  CHECK_MSG(freecam_is_player_view(0x003298A, 0, 0, same, cam) == false,
            "a refused locate never substitutes");
}

static void tb_0088_freecam_draw_seen_verdict() {
  const int T = FREECAM_DRAW_IDLE_FRAMES;

  FreecamDrawSeenVerdict v = freecam_draw_seen_verdict(T - 2, T);
  CHECK(v.idle == T - 1);
  CHECK_MSG(v.revoke == 0, "one frame short of the budget revokes nothing");

  v = freecam_draw_seen_verdict(T - 1, T);
  CHECK(v.idle == T);
  CHECK_MSG(v.revoke != 0, "the budget is spent on the frame that reaches it");

  // The count stops at the budget so the log line prints once, not every frame on the other renderer.
  v = freecam_draw_seen_verdict(T, T);
  CHECK_MSG(v.idle == T, "the count stops at the budget");
  CHECK_MSG(v.revoke == 0, "a spent budget is not spent again");

  v = freecam_draw_seen_verdict(0, T);
  CHECK(v.idle == 1);
  CHECK_MSG(v.revoke == 0, "a zero re-arms the count");

  // A degenerate budget must not revoke on frame one.
  v = freecam_draw_seen_verdict(0, 0);
  CHECK_MSG(v.idle == 0 && v.revoke == 0, "a zero budget revokes nothing");
}

static void tb_0089_freecam_pose_lerp() {
  const FreecamPose a = { 100, 200, 300, 0, 100 };
  const FreecamPose b = { 300, 600, 700, 512, 228 };

  // Ratio 0 is the previous tic exactly: an endpoint off by one unit is a camera that never quite sits still.
  FreecamPose r = freecam_pose_lerp(a, b, 0);
  CHECK_MSG(r.x == a.x && r.y == a.y && r.z == a.z, "ratio 0 gives prev's position");
  CHECK_MSG(r.ang == a.ang && r.horiz == a.horiz, "ratio 0 gives prev's facing");

  r = freecam_pose_lerp(a, b, FREECAM_SMOOTH_UNIT);
  CHECK_MSG(r.x == b.x && r.y == b.y && r.z == b.z, "a full ratio gives cur's position");
  CHECK_MSG(r.ang == b.ang && r.horiz == b.horiz, "a full ratio gives cur's facing");

  r = freecam_pose_lerp(a, b, FREECAM_SMOOTH_UNIT / 2);
  CHECK_MSG(r.x == 200 && r.y == 400 && r.z == 500, "the midpoint position");
  CHECK_MSG(r.ang == 256, "the midpoint yaw, nowhere near a wrap");
  CHECK_MSG(r.horiz == 164, "horiz blends linearly; it does not wrap");

  // The short arc forwards past zero: prev 2040, cur 4 is twelve units clockwise, not 2036 the other way.
  const FreecamPose w0 = { 0, 0, 0, 2040, 100 };
  const FreecamPose w1 = { 0, 0, 0,    4, 100 };
  r = freecam_pose_lerp(w0, w1, FREECAM_SMOOTH_UNIT / 2);
  CHECK_MSG(r.ang == 2046, "the short arc forwards across the wrap");

  // The answer is masked back into 0..2047 when the blend lands past zero: three quarters is 2049, which is 1.
  r = freecam_pose_lerp(w0, w1, FREECAM_SMOOTH_UNIT * 3 / 4);
  CHECK_MSG(r.ang == 1, "a blend that crosses zero comes back inside the units");

  r = freecam_pose_lerp(w1, w0, FREECAM_SMOOTH_UNIT / 2);
  CHECK_MSG(r.ang == 2046, "the short arc backwards across the wrap");
  r = freecam_pose_lerp(w1, w0, FREECAM_SMOOTH_UNIT * 3 / 4);
  CHECK_MSG(r.ang == 2043, "and it keeps going the short way");

  // A linear blend would answer 1022 for both midpoints; named so the pin says what it guards against.
  CHECK_MSG(freecam_pose_lerp(w0, w1, FREECAM_SMOOTH_UNIT / 2).ang != 1022,
            "a linear yaw blend would have gone the long way round");

  // The exact half turn is a tie resolved one way on purpose: the masked difference of 1024 is -1024, so 180 degrees
  // takes the counter-clockwise arc.
  const FreecamPose h0 = { 0, 0, 0,    0, 100 };
  const FreecamPose h1 = { 0, 0, 0, 1024, 100 };
  CHECK_MSG(freecam_pose_lerp(h0, h1, FREECAM_SMOOTH_UNIT / 2).ang == 1536,
            "a half turn resolves counter-clockwise rather than undefined");

  // A ratio outside 0..65536 is clamped, not extrapolated: baselayer.h says smoothRatio is in range, but a hooked
  // argument is not assumed.
  r = freecam_pose_lerp(a, b, -5000);
  CHECK_MSG(r.x == a.x && r.ang == a.ang, "a negative ratio clamps to prev");
  r = freecam_pose_lerp(a, b, FREECAM_SMOOTH_UNIT + 70000);
  CHECK_MSG(r.x == b.x && r.ang == b.ang, "a ratio past the unit clamps to cur");

  // Prev collapsed onto cur draws no streak at any ratio (every discontinuity does this, cam_start at minimum).
  const int collapsedRatios[3] = { 0, 40961, FREECAM_SMOOTH_UNIT };
  for (int i = 0; i < 3; i++) {
    const FreecamPose s = freecam_pose_lerp(b, b, collapsedRatios[i]);
    CHECK_MSG(s.x == b.x && s.y == b.y && s.z == b.z && s.ang == b.ang &&
              s.horiz == b.horiz,
              "a collapsed pair draws the same pose at every ratio");
  }

  const FreecamPose nearPrev = freecam_pose_lerp(a, b, 1);
  CHECK_MSG(nearPrev.x >= a.x && nearPrev.x <= b.x && nearPrev.z >= a.z &&
            nearPrev.z <= b.z && nearPrev.ang >= 0 &&
            nearPrev.ang < FREECAM_ANG_UNITS,
            "one unit in from prev is still inside the pair");
  const FreecamPose nearCur = freecam_pose_lerp(a, b, FREECAM_SMOOTH_UNIT - 1);
  CHECK_MSG(nearCur.x >= a.x && nearCur.x <= b.x && nearCur.z >= a.z &&
            nearCur.z <= b.z && nearCur.ang >= 0 &&
            nearCur.ang < FREECAM_ANG_UNITS,
            "and one unit short of cur is too");

  // A z delta big enough to overflow 32 bits when scaled: one tic can move z by tens of thousands (z is 16x the x/y
  // axis) and 100000 * 65536 does not fit an int, so the arithmetic inside is wider than the fields.
  const FreecamPose z0 = { 0, 0,      0, 0, 100 };
  const FreecamPose z1 = { 0, 0, 100000, 0, 100 };
  CHECK_MSG(freecam_pose_lerp(z0, z1, FREECAM_SMOOTH_UNIT / 2).z == 50000,
            "a z delta that overflows 32 bits when scaled still blends");
  CHECK_MSG(freecam_pose_lerp(z1, z0, FREECAM_SMOOTH_UNIT / 2).z == 50000,
            "and so does the same delta downwards");
}

// The viewpoint as text, in a format not ours to choose: Megaton's debug readout writes "cam x=%d y=%d z=%d ang=%d
// pitch=%d" and all three games load the same .MAP data, so a copied viewpoint pastes into another.
static const char* campos_line_ref(char* buf, int cap, int x, int y, int z,
                                   int ang, int pitch) {
  sprintf_s(buf, (size_t)cap, "cam x=%d y=%d z=%d ang=%d pitch=%d",
            x, y, z, ang, pitch);
  return buf;
}

static void tb_0092_campos_text() {
  char line[96];

  const FreecamPose p = { 100, -200, 3000, 512, FREECAM_HORIZ_LEVEL };
  CHECK(campos_format(p, line, (int)sizeof line));
  CHECK_MSG(strcmp(line, "cam x=100 y=-200 z=3000 ang=512 pitch=0") == 0,
            "the line is Megaton's, field for field and space for space");

  const FreecamPose neg = { -1, -2, -3, 0, FREECAM_HORIZ_LEVEL };
  CHECK(campos_format(neg, line, (int)sizeof line));
  CHECK_MSG(strcmp(line, "cam x=-1 y=-2 z=-3 ang=0 pitch=0") == 0,
            "negative coordinates keep the same shape");

  char small[8];
  CHECK_MSG(!campos_format(p, small, (int)sizeof small), "a short buffer refuses");
  CHECK_MSG(small[0] == 0, "and leaves the caller a defined empty string");
  CHECK(!campos_format(p, line, 0));
  CHECK(!campos_format(p, 0, 16));

  // The trap by name: a horiz written straight into the pitch field still round-trips against itself, so the number in
  // the line is compared against the pitch it came from, and the two are checked to differ so the pin cannot be
  // vacuous.
  const FreecamPose high = { 0, 0, 0, 0, freecam_pitch_to_horiz(400) };
  CHECK(campos_format(high, line, (int)sizeof line));
  CHECK_MSG(strstr(line, " pitch=400") != 0,
            "a pose at pitch 400 writes pitch=400, not its horiz");
  CHECK_MSG(high.horiz != 400, "and horiz at that pitch is a different number");

  // Megaton's own readout, with two fields that mean nothing here.
  FreecamPose got = { 0, 0, 0, 0, 0 };
  CHECK(campos_accept("cam x=1024 y=-2048 z=16384 ang=1536 pitch=-123 cap=0 cutaway=0",
                      &got));
  CHECK(got.x == 1024 && got.y == -2048 && got.z == 16384 && got.ang == 1536);
  CHECK_MSG(got.horiz == freecam_pitch_to_horiz(-123),
            "the text's pitch arrives as this port's horiz, converted");

  CHECK(campos_accept("junk\r\ncam x=1 y=2 z=3 ang=4 pitch=5\r\nmore", &got));
  CHECK(got.x == 1 && got.y == 2 && got.z == 3 && got.ang == 4);

  // Out is untouched on every refusal, so a caller ignoring the answer cannot move the camera with a pose never
  // accepted.
  const FreecamPose sentinel = { 11, 22, 33, 44, 55 };
  FreecamPose keep = sentinel;
  CHECK_MSG(!campos_accept("nothing like a readout here", &keep), "no line, no pose");
  CHECK_MSG(!campos_accept("cam x=1 y=2 z=3 ang=4", &keep), "four fields is not a pose");
  CHECK_MSG(!campos_accept("cam x=1 y=2 z=3 ang=4 pitch=", &keep), "nor a truncated one");
  CHECK(!campos_accept("", &keep));
  CHECK(!campos_accept(0, &keep));
  CHECK(!campos_accept("cam x=1 y=2 z=3 ang=4 pitch=5", 0));
  CHECK_MSG(keep.x == sentinel.x && keep.y == sentinel.y && keep.z == sentinel.z &&
            keep.ang == sentinel.ang && keep.horiz == sentinel.horiz,
            "a refused paste writes nothing at all");

  // The edges are accepted, one unit past is not: a bound refusing a legitimate map coordinate is worse than none.
  char buf[128];
  CHECK(campos_accept(campos_line_ref(buf, (int)sizeof buf, CAMPOS_MAX_XY,
                                      -CAMPOS_MAX_XY, CAMPOS_MAX_Z, 0,
                                      FREECAM_PITCH_MAX), &got));
  CHECK(campos_accept(campos_line_ref(buf, (int)sizeof buf, 0, 0, -CAMPOS_MAX_Z,
                                      FREECAM_ANG_UNITS - 1,
                                      -FREECAM_PITCH_MAX), &got));
  CHECK(!campos_accept(campos_line_ref(buf, (int)sizeof buf, CAMPOS_MAX_XY + 1,
                                       0, 0, 0, 0), &got));
  CHECK(!campos_accept(campos_line_ref(buf, (int)sizeof buf, 0,
                                       -CAMPOS_MAX_XY - 1, 0, 0, 0), &got));
  CHECK(!campos_accept(campos_line_ref(buf, (int)sizeof buf, 0, 0,
                                       CAMPOS_MAX_Z + 1, 0, 0), &got));
  CHECK(!campos_accept(campos_line_ref(buf, (int)sizeof buf, 0, 0, 0,
                                       FREECAM_ANG_UNITS, 0), &got));
  CHECK(!campos_accept(campos_line_ref(buf, (int)sizeof buf, 0, 0, 0, -1, 0), &got));
  CHECK(!campos_accept(campos_line_ref(buf, (int)sizeof buf, 0, 0, 0, 0,
                                       FREECAM_PITCH_MAX + 1), &got));
  CHECK(!campos_accept(campos_line_ref(buf, (int)sizeof buf, 0, 0, 0, 0,
                                       -FREECAM_PITCH_MAX - 1), &got));

  int badFields = 0, badHoriz = 0, badCall = 0;
  for (int pitch = -FREECAM_PITCH_MAX; pitch <= FREECAM_PITCH_MAX; pitch += 25) {
    const FreecamPose src = { 1234, -5678, 9012, 777,
                              freecam_pitch_to_horiz(pitch) };
    char t[96];
    FreecamPose back = { 0, 0, 0, 0, 0 };
    if (!campos_format(src, t, (int)sizeof t) || !campos_accept(t, &back)) {
      badCall++;
      continue;
    }
    if (back.x != src.x || back.y != src.y || back.z != src.z ||
        back.ang != src.ang) badFields++;
    if (back.horiz != src.horiz) badHoriz++;
  }
  CHECK_MSG(badCall == 0, "every pose formats and parses back");
  CHECK_MSG(badFields == 0, "x, y, z and ang survive the round trip");
  CHECK_MSG(badHoriz == 0,
            "and so does horiz, out through the pitch the text carries and back");
}

static void tb_0093_freecam_band_pick() {
  // The engine's own test, inclusive at both ends: :1215-1217 goes external on `daposz > cursectflorz || daposz <
  // cursectceilz`, so exactly on a surface is INSIDE.
  const FreecamBand b = { 4, -1000, 1000, 1 };
  CHECK_MSG(freecam_band_contains(b, -1000), "exactly on the ceiling is inside");
  CHECK_MSG(freecam_band_contains(b, 1000), "exactly on the floor is inside");
  CHECK(freecam_band_contains(b, 0));
  CHECK(!freecam_band_contains(b, -1001));
  CHECK(!freecam_band_contains(b, 1001));

  // An inverted band contains nothing with no term of its own: a ceiling below its floor fails one side for every z.
  const FreecamBand inv = { 4, 1000, -1000, 1 };
  CHECK(!freecam_band_contains(inv, 0));

  CHECK_MSG(freecam_band_pick(NULL, 0, 0, 3, 4096) == 3, "no list keeps the last");
  const FreecamBand one[1] = { { 5, 0, 100, 1 } };
  CHECK(freecam_band_pick(one, 0, 50, 3, 4096) == 3);
  CHECK_MSG(freecam_band_pick(one, -1, 50, 3, 4096) == 3,
            "a negative count is an empty list, not a walk backwards");

  const FreecamBand miss[2] = { { 7, 0, 100, 1 }, { 8, 200, 300, 1 } };
  CHECK_MSG(freecam_band_pick(miss, 2, 150, 3, 4096) == 3,
            "a band that does not contain z is not an answer");

  // A containing band is never passed over for one that misses, wherever it sits.
  const FreecamBand mix[3] = { { 7, 0, 100, 1 }, { 8, 200, 300, 1 }, { 9, 100, 200, 1 } };
  CHECK(freecam_band_pick(mix, 3, 150, 3, 4096) == 9);
  const FreecamBand mix2[3] = { { 9, 100, 200, 1 }, { 7, 0, 100, 1 }, { 8, 200, 300, 1 } };
  CHECK(freecam_band_pick(mix2, 3, 150, 3, 4096) == 9);

  // List order decides between two that both contain z, and camera.cpp puts the engine-resolved sector first, so an
  // in-band frame picks what it picks today.
  const FreecamBand two[2] = { { 11, 0, 100, 1 }, { 12, 0, 100, 1 } };
  CHECK_MSG(freecam_band_pick(two, 2, 50, 3, 4096) == 11,
            "the first containing candidate wins");

  // An exact band beats a guessed one that came first: a sloped sector's flat ceilingz/floorz is the plane's z at its
  // first wall, not the camera, so preferring it over a flat sector that certainly contains z trades a sure answer for
  // one the renderer may reject.
  const FreecamBand slope[3] = { { 10, 200, 300, 1 }, { 11, 0, 100, 0 }, { 12, 0, 100, 1 } };
  CHECK(freecam_band_pick(slope, 3, 50, 3, 4096) == 12);
  const FreecamBand slopeOnly[2] = { { 11, 200, 300, 1 }, { 12, 0, 100, 0 } };
  CHECK_MSG(freecam_band_pick(slopeOnly, 2, 50, 3, 4096) == 12,
            "a guess is still better than no answer at all");

  // A candidate zero that cannot be evaluated refuses the whole substitution.
  const FreecamBand guessMiss[2] = { { 11, 0, 100, 0 }, { 12, 100, 200, 1 } };
  CHECK_MSG(freecam_band_pick(guessMiss, 2, 150, 3, 4096) == 3,
            "a guessed candidate zero that misses substitutes nothing");
  // The other direction is the same ignorance: a guessed candidate zero that appears to CONTAIN z is not knowable and
  // must not be passed over for an exact neighbour.
  const FreecamBand guessHit[2] = { { 11, 0, 100, 0 }, { 12, 0, 200, 1 } };
  CHECK_MSG(freecam_band_pick(guessHit, 2, 50, 3, 4096) == 3,
            "a guessed candidate zero is not evaluated at all");
  const FreecamBand guessOne[1] = { { 11, 0, 100, 0 } };
  CHECK(freecam_band_pick(guessOne, 1, 50, 3, 4096) == 3);
  // An exact candidate zero is evaluated as before (the engine's getzsofslopeptr puts every candidate on that path).
  const FreecamBand exactZero[2] = { { 11, 0, 100, 1 }, { 12, 100, 200, 1 } };
  CHECK(freecam_band_pick(exactZero, 2, 150, 3, 4096) == 12);

  // The bound is load-bearing as for freecam_sector_pick: game.cpp:1080-1082 walks CAMERA(sect) with no upper bound of
  // its own.
  const FreecamBand bad[2] = { { 4096, 0, 100, 1 }, { -1, 0, 100, 1 } };
  CHECK_MSG(freecam_band_pick(bad, 2, 50, 3, 4096) == 3,
            "an out-of-range candidate is never chosen, even containing z");
  CHECK(freecam_band_pick(bad, 2, 50, 3, EK_MAXSECTORS) == 3);
  const FreecamBand edge[1] = { { 4095, 0, 100, 1 } };
  CHECK(freecam_band_pick(edge, 1, 50, 3, 4096) == 4095);
  CHECK_MSG(freecam_band_pick(edge, 1, 50, 3, 4095) == 3, "at the bound is past it");

  // The fallback is handed back untouched, including the one refusal freecam_sector_pick can still answer.
  CHECK(freecam_band_pick(miss, 2, 150, -1, 4096) == -1);
}

// sectortypev7's layout and the invariant the sector[] scan accepts on (buildtypes.h:41-56, build.h:630).
static void tb_0094_sector_layout() {
  CHECK_MSG(EK_SECTOR_STRIDE == 40, "sectortypev7 is 40 bytes, as its own comment says");
  CHECK(EK_SECT_WALLPTR == 0x00);
  CHECK(EK_SECT_WALLNUM == 0x02);
  CHECK_MSG(EK_SECT_CEILINGZ == 0x04, "two int16s come before the first int32");
  CHECK(EK_SECT_FLOORZ == 0x08);
  CHECK(EK_SECT_CEILINGSTAT == 0x0C);
  CHECK(EK_SECT_FLOORSTAT == 0x0E);
  // The slope bit is bit 1 on the authority of the engine's own test, `((ceilingstat|floorstat)&2) != 2` at
  // engine.cpp:14292.
  CHECK(EK_SECT_STAT_SLOPED == 2);
  // The allocation the index bound is taken against is MAXSECTORS plus the editor's two spare slots (build.h:70, :630).
  CHECK(EK_SECTOR_SLOTS == EK_MAXSECTORS + 2);

  CHECK(ekm_sector_record_plausible(0, 4));
  CHECK_MSG(!ekm_sector_record_plausible(0, 0), "zeroed memory is not sector 0");
  CHECK(!ekm_sector_record_plausible(0, 2));
  CHECK(!ekm_sector_record_plausible(-1, 4));
  CHECK(ekm_sector_record_plausible(EK_WALL_SLOTS - 3, 3));
  CHECK_MSG(!ekm_sector_record_plausible(EK_WALL_SLOTS - 2, 3),
            "a sector whose walls run off the end of wall[] is not a sector");

  // Each sector's walls begin where the one before ended; an unrelated array fails within a few records.
  CHECK(ekm_sector_follows(0, 4, 4));
  CHECK(!ekm_sector_follows(0, 4, 5));
}

// Where the whole-map sweep stops. polymer_drawsector dereferences prsectors[sectnum] unchecked (polymer.cpp:3043),
// polymer_freeboard NULLs all MAXSECTORS and polymer_loadboard fills exactly numsectors, so that pointer IS the map's
// extent and the sweep reads it; the sector chain stays as the wall span's own bound.
static void tb_0095_sector_sweep_step() {
  // Sector 0 starts at wall 0: the walk seeds a notional (0, 0) predecessor, the seed sectorstate's scan is accepted
  // on.
  const EkSectorSweep first = ekm_sector_sweep_step(0, 1, 1, 0, 4, 0, 0, EK_MAXSECTORS);
  CHECK(first.stop == 0);
  CHECK_MSG(first.wallFirst == 0, "the span the wall half of the sweep walks");
  CHECK(first.wallCount == 4);

  const EkSectorSweep next = ekm_sector_sweep_step(1, 1, 1, 4, 6, 0, 4, EK_MAXSECTORS);
  CHECK(next.stop == 0);
  CHECK(next.wallFirst == 4);
  CHECK(next.wallCount == 6);

  // A NULL prsectors[i] is the end of the map, the exact answer the chain only approximates.
  const EkSectorSweep dead = ekm_sector_sweep_step(1, 1, 0, 4, 6, 0, 4, EK_MAXSECTORS);
  CHECK_MSG(dead.stop == 1, "a sector the renderer has no _prsector for");
  CHECK(dead.wallCount == 0);

  // A gap is the end of the map too, not a sector to skip.
  const EkSectorSweep gap = ekm_sector_sweep_step(1, 1, 1, 5, 6, 0, 4, EK_MAXSECTORS);
  CHECK_MSG(gap.stop == 1, "a sector that does not continue the tiling");

  // A degenerate sector is walked PAST, not stopped on: E4L1's sector 34 is wallptr 182, wallnum 2, a real sector in a
  // stock map, and stopping cost the sweep 512 of 546 sectors.
  const EkSectorSweep degen = ekm_sector_sweep_step(34, 1, 1, 182, 2, 178, 4,
                                                    EK_MAXSECTORS);
  CHECK_MSG(degen.stop == 0, "a two-wall sector does not end the sweep");
  CHECK(degen.wallFirst == 182 && degen.wallCount == 2);
  CHECK_MSG(!ekm_sector_record_plausible(182, 2),
            "and the discovery rule still refuses it, which is its job");

  // Zeroed memory no longer dies on the span alone: the sweep runs only on an array discovery accepted strictly, and
  // prsectorLive stops it past the live map (the `dead` case).
  const EkSectorSweep empty = ekm_sector_sweep_step(0, 1, 1, 0, 0, 0, 0,
                                                    EK_MAXSECTORS);
  CHECK_MSG(empty.stop == 0, "an empty span is bounded, not a stop");
  CHECK(empty.wallCount == 0);
  CHECK_MSG(ekm_sector_sweep_step(0, 1, 0, 0, 0, 0, 0, EK_MAXSECTORS).stop == 1,
            "and the no-_prsector net is what ends it past the live map");

  // A span that does not continue the tiling is still the end of the map.
  CHECK(ekm_sector_sweep_step(0, 1, 1, 1, 4, 0, 0, EK_MAXSECTORS).stop == 1);
  CHECK_MSG(ekm_sector_sweep_step(0, 1, 1, EK_WALL_SLOTS - 2, 3, 0, 0,
                                  EK_MAXSECTORS).stop == 1,
            "a span running off the end of wall[] ends the sweep");

  // The span is bounded by prwalls[]/wallsect[], which are [MAXWALLS], not wall[]'s EK_WALL_SLOTS (the editor's spare
  // slots make that 512 too generous).
  CHECK_MSG(ekm_sector_record_plausible(EK_MAXWALLS - 1, 4),
            "wall[] itself accepts a span past MAXWALLS");
  CHECK_MSG(ekm_sector_sweep_step(0, 1, 1, EK_MAXWALLS - 1, 4, EK_MAXWALLS - 5,
                                  4, EK_MAXSECTORS).stop == 1,
            "a span crossing MAXWALLS ends the sweep");
  CHECK(ekm_sector_sweep_step(0, 1, 1, EK_MAXWALLS - 4, 4, EK_MAXWALLS - 8, 4,
                              EK_MAXSECTORS).stop == 0);

  // A read that failed is not permission: "nothing known" stops the sweep as a bad record does.
  CHECK(ekm_sector_sweep_step(2, 0, 1, 10, 4, 4, 6, EK_MAXSECTORS).stop == 1);

  CHECK(ekm_sector_sweep_step(EK_MAXSECTORS, 1, 1, 0, 4, 0, 0, EK_MAXSECTORS).stop == 1);
  CHECK(ekm_sector_sweep_step(-1, 1, 1, 0, 4, 0, 0, EK_MAXSECTORS).stop == 1);
  CHECK(ekm_sector_sweep_step(4095, 1, 1, 0, 4, 0, 0, 4096).stop == 0);
}

// The three rules that accept prsectors[]'s refptr SLOT, the one address the sweep dereferences per sector.
static void tb_0096_prsectors_slot_rules() {
  // One indexed load and one refptr, or the shape is not the head of polymer_updatesector's and nothing may be
  // concluded.
  CHECK(ekm_prs_shape_ok(1, 1));
  CHECK_MSG(!ekm_prs_shape_ok(2, 1), "two candidate loads name nothing");
  CHECK(!ekm_prs_shape_ok(1, 2));
  CHECK(!ekm_prs_shape_ok(0, 0));

  // Witnesses are polymer.cpp's own uses: drawsector :3043 and updatewall :3258/:3262 read prsectors[]; drawwall never
  // does.
  CHECK(ekm_prs_witness_ok(3, 1, 1, 0));
  CHECK_MSG(!ekm_prs_witness_ok(3, 1, 1, 1), "polymer_drawwall never reads it");
  CHECK(!ekm_prs_witness_ok(3, 0, 1, 0));
  CHECK(!ekm_prs_witness_ok(3, 1, 0, 0));
  CHECK(!ekm_prs_witness_ok(0, 1, 1, 0));

  const size_t img = 0x1AA6000;
  const size_t slot = 0x5A3DF0, arr = 0x13E9D00;
  CHECK(ekm_prs_span_ok(slot, arr, img, 1));
  CHECK_MSG(!ekm_prs_span_ok(slot, arr, img, 0), "a slot in a code section");
  CHECK(!ekm_prs_span_ok(slot, arr, 0, 1));
  CHECK(!ekm_prs_span_ok(slot, 0, img, 1));
  CHECK(!ekm_prs_span_ok(slot, img, img, 1));
  CHECK_MSG(!ekm_prs_span_ok(slot, img - EK_MAXSECTORS * sizeof(void*) + 8, img, 1),
            "an array with room for all but one pointer");
  CHECK(ekm_prs_span_ok(slot, img - EK_MAXSECTORS * sizeof(void*), img, 1));
  CHECK(!ekm_prs_span_ok(img, arr, img, 1));
}

static void tb_0097_freecam_fog_rules() {
  CHECK(freecam_fog_substitute(true, true));
  CHECK_MSG(!freecam_fog_substitute(false, true),
            "the switch alone, with the camera down, substitutes nothing");
  CHECK(!freecam_fog_substitute(true, false));
  CHECK(!freecam_fog_substitute(false, false));

  // The trace back from engine.cpp:9480's `* 150`: one imul, one shl, one dword read and at least one refptr load
  // before it.
  CHECK(ekm_gvis_shape_ok(1, 1, 1, 1));
  CHECK(ekm_gvis_shape_ok(1, 1, 1, 2));
  CHECK_MSG(!ekm_gvis_shape_ok(2, 1, 1, 1), "two `* 150` sites name nothing");
  CHECK(!ekm_gvis_shape_ok(1, 2, 1, 1));
  CHECK(!ekm_gvis_shape_ok(1, 1, 0, 1));
  CHECK(!ekm_gvis_shape_ok(1, 1, 1, 0));

  // Every arm of engine.cpp:9460-9482's switch reads g_visibility, and game.cpp:817 writes it once inside G_DrawRooms.
  CHECK(ekm_gvis_witness_ok(3, 1));
  CHECK(ekm_gvis_witness_ok(2, 1));
  CHECK_MSG(!ekm_gvis_witness_ok(1, 1),
            "one arm reading it is not the three-way switch");
  CHECK(!ekm_gvis_witness_ok(3, 0));
  CHECK_MSG(!ekm_gvis_witness_ok(3, 2),
            "game.cpp:817 is the only write in G_DrawRooms");

  const size_t img = 0x1AA6000;
  const size_t slot = 0x5A2420, addr = 0xC222E0;
  CHECK(ekm_gvis_span_ok(slot, addr, img, true));
  CHECK_MSG(!ekm_gvis_span_ok(slot, addr, img, false),
            "a slot in a code section");
  CHECK(!ekm_gvis_span_ok(0, addr, img, true));
  CHECK(!ekm_gvis_span_ok(slot, 0, img, true));
  CHECK(!ekm_gvis_span_ok(slot, img - 3, img, true));
  CHECK(ekm_gvis_span_ok(slot, img - 4, img, true));
}

static void tb_0098_drawingstate_rules() {
  // The addressing is a BIT, not a byte: Polymer's drawingstate is bitmap_size(MAXSECTORS) bytes (compat.h:1173), so a
  // byte-per-sector read answers for sector n>>3 and skips seven of eight.
  CHECK(ekm_bitmap_byte(0) == 0);
  CHECK(ekm_bitmap_byte(7) == 0);
  CHECK(ekm_bitmap_byte(8) == 1);
  CHECK(ekm_bitmap_byte(EK_MAXSECTORS - 1) == EK_SECTOR_BITMAP_BYTES - 1);
  CHECK(ekm_bitmap_mask(0) == 1);
  CHECK(ekm_bitmap_mask(7) == 128);
  CHECK_MSG(ekm_bitmap_mask(8) == 1, "the mask wraps every eight sectors");
  CHECK(ekm_bitmap_mask(9) == 2);

  // Eight sectors 257 apart land in every bit lane: each reads back set and its neighbour does not.
  unsigned char bits[EK_SECTOR_BITMAP_BYTES];
  for (int i = 0; i < EK_SECTOR_BITMAP_BYTES; i++) bits[i] = 0;
  for (int k = 0; k < 8; k++) {
    const int n = k * 257;
    bits[ekm_bitmap_byte(n)] |= (unsigned char)ekm_bitmap_mask(n);
  }
  for (int k = 0; k < 8; k++) {
    const int n = k * 257;
    CHECK(ekm_bitmap_test(bits[ekm_bitmap_byte(n)], n));
    CHECK(!ekm_bitmap_test(bits[ekm_bitmap_byte(n + 1)], n + 1));
  }

  // polymer_displayrooms carries no string, so it is the ONE callee of the located drawrooms calling both
  // polymer_drawsector and polymer_drawwall, twice each (polymer.cpp:1859/:2058 and :1937/:2062).
  CHECK(ekm_dstate_owner_ok(1, 2, 2));
  CHECK(ekm_dstate_owner_ok(1, 3, 2));
  CHECK_MSG(!ekm_dstate_owner_ok(2, 2, 2),
            "two callees calling both name nothing");
  CHECK(!ekm_dstate_owner_ok(0, 2, 2));
  CHECK(!ekm_dstate_owner_ok(1, 1, 2));
  CHECK(!ekm_dstate_owner_ok(1, 2, 1));

  // The shape: polymer.cpp:1827's 512-byte zero then :1832's bitmap_set.
  CHECK(ekm_dstate_shape_ok(1, 1, 1, 1));
  CHECK_MSG(!ekm_dstate_shape_ok(0, 1, 1, 1), "no 512-byte zero, no anchor");
  CHECK(!ekm_dstate_shape_ok(2, 1, 1, 1));
  CHECK(!ekm_dstate_shape_ok(1, 2, 1, 1));
  CHECK(!ekm_dstate_shape_ok(1, 1, 2, 1));
  CHECK_MSG(!ekm_dstate_shape_ok(1, 1, 1, 0),
            "the memset alone could be any 512-byte array");

  // polymer.cpp reads or writes drawingstate nine times inside polymer_displayrooms, four inside the TROR block.
  CHECK(ekm_dstate_use_ok(9));
  CHECK(ekm_dstate_use_ok(5));
  CHECK(!ekm_dstate_use_ok(4));
  CHECK(!ekm_dstate_use_ok(10));

  const size_t secB = 0x05E5000, secE = 0x1A2C5C0;
  CHECK(ekm_dstate_span_ok(0x14B0BE0, secB, secE, true));
  CHECK_MSG(!ekm_dstate_span_ok(0x14B0BE0, secB, secE, false),
            "an address in no readable data section is not a bitmap's");
  CHECK(!ekm_dstate_span_ok(0, secB, secE, true));
  CHECK(!ekm_dstate_span_ok(secE - EK_SECTOR_BITMAP_BYTES + 1, secB, secE, true));
  CHECK(ekm_dstate_span_ok(secE - EK_SECTOR_BITMAP_BYTES, secB, secE, true));
}

static void tb_0100_dstate_section_span() {
  // Rule 4 refused on every machine: drawingstate is a static array in .bss, whose SizeOfRawData is 0, and the
  // byte-search membership test drops any section the file does not back, so the sweep was never available.
  const size_t bssBegin = 0x05E5000;   // .bss of the shipped image, by
  const size_t bssEnd   = 0x1A2C5C0;   // VirtualSize and not by raw size
  CHECK_MSG(ekm_dstate_span_ok(0x14B0BE0, bssBegin, bssEnd, 1),
            "the shipped build's drawingstate is in .bss and must be accepted");
  CHECK_MSG(!ekm_dstate_span_ok(0x14B0BE0, bssBegin, bssEnd, 0),
            "no containing section is a refusal and not a guess");
  CHECK(!ekm_dstate_span_ok(0, bssBegin, bssEnd, 1));
  CHECK(!ekm_dstate_span_ok(bssBegin - 1, bssBegin, bssEnd, 1));
  CHECK(ekm_dstate_span_ok(bssBegin, bssBegin, bssEnd, 1));
  CHECK(!ekm_dstate_span_ok(bssEnd, bssBegin, bssEnd, 1));
  CHECK_MSG(!ekm_dstate_span_ok(0x14B0BE0, bssEnd, bssBegin, 1),
            "an empty or inverted section extent decides nothing");

  // Strengthened: the bitmap's LAST byte must be inside the same section.
  CHECK(ekm_dstate_span_ok(bssEnd - EK_SECTOR_BITMAP_BYTES, bssBegin, bssEnd, 1));
  CHECK_MSG(!ekm_dstate_span_ok(bssEnd - EK_SECTOR_BITMAP_BYTES + 1, bssBegin,
                                bssEnd, 1),
            "a bitmap running off the end of its own section is refused");
}
static void tb_0099_freecam_engine_tic() {
  // The tic test asks the engine's own counter: ototalclock advances by TICSPERFRAME per processed tic (game.cpp:7122),
  // so any change is a tic at any framerate.
  CHECK(freecam_tic_advanced(0, 4, true));
  CHECK(!freecam_tic_advanced(4, 4, true));
  CHECK_MSG(!freecam_tic_advanced(0, 4, false),
            "no previous reading is not a tic");
  CHECK_MSG(freecam_tic_advanced(400, 0, true),
            "a level entry resets the clock, and that is a boundary too");
  CHECK(freecam_tic_advanced(-4, 0, true));

  int steps = 0, prev = 0;
  bool have = false;
  for (int f = 0; f < 4; f++) {
    const int tic = f * 4;
    if (freecam_tic_advanced(prev, tic, have)) steps++;
    prev = tic;
    have = true;
  }
  CHECK_MSG(steps == 3, "four frames a tic apart are three boundaries");

  // The 240 fps case is unchanged: eight frames a tic, three tics, two boundaries, one step each with blended frames
  // between.
  int fast = 0;
  prev = 0;
  have = false;
  for (int t = 0; t < 3; t++)
    for (int f = 0; f < 8; f++) {
      if (freecam_tic_advanced(prev, t * 4, have)) fast++;
      prev = t * 4;
      have = true;
    }
  CHECK_MSG(fast == 2, "three tics of eight frames each cross two boundaries");

  // ClockTicks::update()'s mask is `xor ax,ax`, and the located drawrooms has one reached through one load and one
  // refptr.
  CHECK(ekm_tclk_shape_ok(1, 1, 1));
  CHECK_MSG(!ekm_tclk_shape_ok(0, 1, 1), "no ClockTicks assignment, no anchor");
  CHECK(!ekm_tclk_shape_ok(2, 1, 1));
  CHECK(!ekm_tclk_shape_ok(1, 2, 1));
  CHECK(!ekm_tclk_shape_ok(1, 1, 2));
  CHECK(!ekm_tclk_shape_ok(1, 1, 0));

  // G_DrawRooms carries the same statement at game.cpp:786.
  CHECK(ekm_tclk_witness_ok(1));
  CHECK(ekm_tclk_witness_ok(2));
  CHECK_MSG(!ekm_tclk_witness_ok(0),
            "one function naming the slot is not two that agree");

  // ototalclock is what the game copies INTO totalclock, and every site doing so must name the same one.
  CHECK(ekm_otclk_witness_ok(6, 1));
  CHECK(ekm_otclk_witness_ok(1, 1));
  CHECK(!ekm_otclk_witness_ok(0, 0));
  CHECK_MSG(!ekm_otclk_witness_ok(6, 2),
            "two disagreeing sources name nothing");

  // The slot is image data and the clock it names has a whole ClockTicks of room inside the same image.
  const size_t img = 0x1AA6000;
  CHECK(ekm_tclk_span_ok(0x5A4580, 0x71B0F0, img, true));
  CHECK_MSG(!ekm_tclk_span_ok(0x5A4580, 0x71B0F0, img, false),
            "a slot in a code section");
  CHECK(!ekm_tclk_span_ok(0, 0x71B0F0, img, true));
  CHECK(!ekm_tclk_span_ok(0x5A4580, 0, img, true));
  CHECK(!ekm_tclk_span_ok(0x5A4580, img - 15, img, true));
  CHECK(ekm_tclk_span_ok(0x5A4580, img - 16, img, true));
}

static void tb_0102_sky_redirect() {
  const unsigned char callFwd[5] = { 0xE8, 0xFB, 0x0F, 0x00, 0x00 };
  CHECK_MSG(ekm_call_rel32_targets(callFwd, 5, 0x1000ull, 0x2000ull),
            "E8 with disp 0x0FFB at 0x1000 reaches 0x2000");
  CHECK_MSG(!ekm_call_rel32_targets(callFwd, 5, 0x1000ull, 0x2005ull),
            "the five-byte term is not optional -- 0x2005 is the off-by-five");
  CHECK_MSG(!ekm_call_rel32_targets(callFwd, 5, 0x1001ull, 0x2000ull),
            "the same bytes at a different site reach a different function");
  const unsigned char jmpFwd[5] = { 0xE9, 0xFB, 0x0F, 0x00, 0x00 };
  CHECK_MSG(!ekm_call_rel32_targets(jmpFwd, 5, 0x1000ull, 0x2000ull),
            "E9 is a jmp, not a call; redirecting it would lose the return");
  CHECK(!ekm_call_rel32_targets(NULL, 5, 0x1000ull, 0x2000ull));
  CHECK_MSG(!ekm_call_rel32_targets(callFwd, 4, 0x1000ull, 0x2000ull),
            "four bytes is half an operand and cannot be read as one");

  // Backwards too, since rel32 is signed and half the call sites reach an earlier function: 0x2000 + 5 - 0x1005 =
  // 0x1000.
  const unsigned char callBack[5] = { 0xE8, 0xFB, 0xEF, 0xFF, 0xFF };
  CHECK(ekm_call_rel32_targets(callBack, 5, 0x2000ull, 0x1000ull));
  CHECK(!ekm_call_rel32_targets(callBack, 5, 0x2000ull, 0x1001ull));

  // The four it writes: an operand this builds must read back through the verifier as reaching where it was aimed.
  unsigned char op[4] = { 0, 0, 0, 0 };
  CHECK(ekm_build_call_operand(op, 4, 0x1000ull, 0x2000ull) == 4);
  CHECK_MSG(op[0] == 0xFB && op[1] == 0x0F && op[2] == 0x00 && op[3] == 0x00,
            "little-endian because x86 is, not because this machine is");
  unsigned char rebuilt[5] = { 0xE8, 0, 0, 0, 0 };
  rebuilt[1] = op[0]; rebuilt[2] = op[1]; rebuilt[3] = op[2]; rebuilt[4] = op[3];
  CHECK_MSG(ekm_call_rel32_targets(rebuilt, 5, 0x1000ull, 0x2000ull),
            "round trip: what the builder writes is what the verifier reads");

  CHECK(ekm_build_call_operand(op, 4, 0x2000ull, 0x1000ull) == 4);
  rebuilt[1] = op[0]; rebuilt[2] = op[1]; rebuilt[3] = op[2]; rebuilt[4] = op[3];
  CHECK_MSG(ekm_call_rel32_targets(rebuilt, 5, 0x2000ull, 0x1000ull),
            "round trip backwards, where the displacement is negative");

  // A call to the instruction after itself is displacement zero, legal and what an off-by-five would silently produce.
  CHECK(ekm_build_call_operand(op, 4, 0x1000ull, 0x1005ull) == 4);
  CHECK(op[0] == 0 && op[1] == 0 && op[2] == 0 && op[3] == 0);

  CHECK_MSG(ekm_build_call_operand(NULL, 4, 0x1000ull, 0x2000ull) == 0,
            "refusing is 0, the way every other builder in this file refuses");
  CHECK(ekm_build_call_operand(op, 3, 0x1000ull, 0x2000ull) == 0);
  CHECK_MSG(ekm_build_call_operand(op, 4, 0x1000ull, 0x90000000ull) == 0,
            "past rel32's reach refuses rather than wrapping into a near call");

  // Widening the band, what the thunk does: the engine is asked first and its answer EXTENDED, never replaced, since
  // polymer.cpp:1211's locals are the renderer's own map reading.
  FreecamWidened w = freecam_band_widen(-1000, 1000, 0);
  CHECK_MSG(w.ceilz == -1000 && w.florz == 1000,
            "a band that already contains z is handed back untouched");

  w = freecam_band_widen(-1000, 1000, 5000);
  CHECK_MSG(w.florz == 5000, "below the floor stretches the FLOOR to z");
  CHECK_MSG(w.ceilz == -1000, "and leaves the ceiling exactly where it was");

  w = freecam_band_widen(-1000, 1000, -5000);
  CHECK_MSG(w.ceilz == -5000, "above the ceiling stretches the CEILING to z");
  CHECK_MSG(w.florz == 1000, "and leaves the floor exactly where it was");

  w = freecam_band_widen(-1000, 1000, -1000);
  CHECK_MSG(w.ceilz == -1000 && w.florz == 1000,
            "exactly on the ceiling is already inside: :1215-1217 goes external "
            "on strictly less and strictly greater");

  // An inverted band is not special: getzsofslopeptr can answer a ceiling below its floor where two slopes cross, and
  // the widened band must contain z all the same.
  w = freecam_band_widen(1000, -1000, 0);
  CHECK(w.ceilz <= 0 && w.florz >= 0);

  const FreecamWidened once = freecam_band_widen(-1000, 1000, 5000);
  const FreecamWidened twice = freecam_band_widen(once.ceilz, once.florz, 5000);
  CHECK(twice.ceilz == once.ceilz && twice.florz == once.florz);

  int monotone = 1, holds = 1;
  for (int k = 0; k < 9; k++) {
    const int z = (k - 4) * 4096;
    const FreecamWidened g = freecam_band_widen(-1000, 1000, z);
    if (g.ceilz > -1000 || g.florz < 1000) monotone = 0;
    FreecamBand b;
    b.sect = 0; b.ceilz = g.ceilz; b.florz = g.florz; b.exact = 1;
    if (!freecam_band_contains(b, z)) holds = 0;
  }
  CHECK_MSG(monotone, "widening never moves either plane inwards");
  CHECK_MSG(holds,
            "and the widened band satisfies the renderer's own test for the z "
            "it was widened for");

  CHECK(freecam_redirect_locate_ok(1, 0));
  CHECK_MSG(!freecam_redirect_locate_ok(0, 0), "nothing to patch is a refusal");
  CHECK_MSG(!freecam_redirect_locate_ok(2, 0), "two sites is a refusal, not a pick");
  CHECK_MSG(!freecam_redirect_locate_ok(1, 1),
            "a scan that faulted counted partially and cannot claim uniqueness");

  // The sprites no longer gate the redirect: that coupling took the sky with a sprite refusal, and the black sky
  // outside the map was reported three times while losing distant actors never was.
  CHECK(freecam_redirect_install_ok(true, true));
  CHECK_MSG(freecam_redirect_install_ok(true, true),
            "a refused sprite pass no longer keeps the sky out");
  CHECK_MSG(!freecam_redirect_install_ok(true, false),
            "no sweep, no redirect: the normal arm's walk misses the geometry");
  CHECK(!freecam_redirect_install_ok(false, true));

  // The sweep arms it frame by frame: out of bounds the normal arm's portal walk is LESS of the map than the external
  // arm drew, and the sweep puts the rest back, so a player with the whole-map switch off must keep the picture they
  // have rather than a sky over an empty world.
  CHECK(freecam_redirect_arm(true, true));
  CHECK_MSG(!freecam_redirect_arm(false, true),
            "the whole-map switch off means no widening, and today's picture");
  CHECK_MSG(!freecam_redirect_arm(true, false),
            "and a refused sweep is the same case arrived at from the locate");
  CHECK(!freecam_redirect_arm(false, false));

  // Only while our own substituted draw is in flight: every other caller, and every frame the camera is down, gets the
  // engine's answer unchanged, or the player's own draw would land on an arm the engine did not choose.
  CHECK(freecam_widen_now(true));
  CHECK_MSG(!freecam_widen_now(false),
            "not in flight is the engine's own answer, byte for byte");
}

static void tb_0101_swept_sprites() {
  // The bound is the safety argument: spritesortcnt indexes tsprite[MAXSPRITESONSCREEN] (build.h:102, :633,
  // engine.cpp:9160) and the portal walk already filled part of it, so an append past 4096 is heap corruption.
  // polymer_scansprites tests `< MAXSPRITESONSCREEN` (polymer.cpp:3898), emitted as `cmp r10d, 0xFFF`.
  CHECK(EK_MAXSPRITESONSCREEN == 4096);
  CHECK(ekm_tsprite_admit(0, EK_MAXSPRITESONSCREEN));
  CHECK(ekm_tsprite_admit(EK_MAXSPRITESONSCREEN - 1, EK_MAXSPRITESONSCREEN));
  CHECK_MSG(!ekm_tsprite_admit(EK_MAXSPRITESONSCREEN, EK_MAXSPRITESONSCREEN),
            "the last slot is 4095 and the engine's own test is strict");
  CHECK(!ekm_tsprite_admit(EK_MAXSPRITESONSCREEN + 1, EK_MAXSPRITESONSCREEN));
  CHECK_MSG(!ekm_tsprite_admit(-1, EK_MAXSPRITESONSCREEN),
            "a counter left negative is not an invitation to write at it");
  CHECK(!ekm_tsprite_admit(0, 0));
  CHECK(!ekm_tsprite_admit(0, -1));

  // polymer.cpp:3896's filter, term for term. cstat bit 15 is invisible.
  CHECK(ekm_tsprite_eligible(0, 1, 1, 0));
  CHECK_MSG(!ekm_tsprite_eligible(0x8000u, 1, 1, 0),
            "an invisible sprite is not collected unless the editor asked");
  CHECK(ekm_tsprite_eligible(0x8000u, 1, 1, 1));
  CHECK_MSG(!ekm_tsprite_eligible(0, 0, 1, 0),
            "xrepeat 0 is a sprite with no width and the engine skips it");
  CHECK(!ekm_tsprite_eligible(0, 1, 0, 0));
  CHECK(!ekm_tsprite_eligible(0, 0, 0, 1));
  CHECK(ekm_tsprite_eligible(0x7FFFu, 64, 64, 0));

  // The sweep drew sector i exactly when i < sweptEnd (where the loop stopped) and the portal walk's bitmap does not
  // claim it.
  CHECK(ekm_sprite_swept(0, 3, 8, EK_MAXSECTORS));
  CHECK(ekm_sprite_swept(0, 7, 8, EK_MAXSECTORS));
  CHECK_MSG(!ekm_sprite_swept(EK_MAXSTATUS, 3, 8, EK_MAXSECTORS),
            "a freed sprite keeps a stale sectnum and must never be collected");
  CHECK(!ekm_sprite_swept(-1, 3, 8, EK_MAXSECTORS));
  CHECK_MSG(!ekm_sprite_swept(0, EK_MAXSECTORS, 8, EK_MAXSECTORS),
            "deletesprite parks sectnum at MAXSECTORS (engine.cpp:8838)");
  CHECK_MSG(!ekm_sprite_swept(0, -1, 8, EK_MAXSECTORS),
            "engine.cpp:11047 leaves sectnum -1 on a sprite in no sector");
  CHECK_MSG(!ekm_sprite_swept(0, 8, 8, EK_MAXSECTORS),
            "the sweep stopped at 8, so sector 8 was never drawn");
  CHECK(!ekm_sprite_swept(0, 0, 0, EK_MAXSECTORS));
  CHECK_MSG(!ekm_sprite_swept(0, 3, EK_MAXSECTORS + 1, EK_MAXSECTORS),
            "a sweep end past the sector array decides nothing");

  // The list's own record of what it holds: renderMakeTSpriteFromSprite writes owner = spritenum (build.h:726), the one
  // gate right on BOTH renderer arms, where the walk's bitmap is this frame's on the normal arm and the last walk's on
  // the other.
  CHECK(ekm_tsprite_owner_ok(0, EK_MAXSPRITES));
  CHECK(ekm_tsprite_owner_ok(EK_MAXSPRITES - 1, EK_MAXSPRITES));
  CHECK(!ekm_tsprite_owner_ok(EK_MAXSPRITES, EK_MAXSPRITES));
  CHECK(!ekm_tsprite_owner_ok(-1, EK_MAXSPRITES));

  CHECK(EK_TSPR_STRIDE == EK_SPR_STRIDE);
  CHECK(EK_TSPR_CLIPDIST == 0x12);
  CHECK(EK_TSPR_OWNER == EK_SPR_OWNER);

  const size_t image = 0x1AA6000;   // the shipped image's SizeOfImage
  CHECK(ekm_tsprite_slot_ok(0x05A45C0, 0x0C23540, image, 1, 8));
  CHECK(ekm_tsprite_slot_ok(0x05A4440, 0x0C2354C, image, 1, 4));
  CHECK_MSG(!ekm_tsprite_slot_ok(0x05A45C0, 0x0C23540, image, 0, 8),
            "a slot outside the image's data is refused and not guessed at");
  CHECK(!ekm_tsprite_slot_ok(0x05A45C0, 0, image, 1, 8));
  CHECK(!ekm_tsprite_slot_ok(0x05A45C0, image, image, 1, 8));
  CHECK_MSG(!ekm_tsprite_slot_ok(0x05A45C0, image - 4, image, 1, 8),
            "a pointer whose last byte is past the image does not fit");
  CHECK(!ekm_tsprite_slot_ok(0x05A45C0, 0x0C23540, 0, 1, 8));

  CHECK(ekm_tsprite_array_ok(0x0C23540, image));
  CHECK(ekm_tsprite_array_ok(image - EK_MAXSPRITESONSCREEN * EK_TSPR_STRIDE, image));
  CHECK_MSG(!ekm_tsprite_array_ok(
                image - EK_MAXSPRITESONSCREEN * EK_TSPR_STRIDE + 1, image),
            "an array with room for 4095 records is refused, not truncated to fit");
  CHECK(!ekm_tsprite_array_ok(0, image));

  // The scanner's acceptance: camera.cpp reads the inlined polymer_scansprites between two proven-unique call sites.
  CHECK(ekm_scansprites_shape_ok(1, 1, 2, 1, 1));
  CHECK(!ekm_scansprites_shape_ok(0, 1, 2, 1, 1));
  CHECK(!ekm_scansprites_shape_ok(2, 1, 2, 1, 1));
  CHECK(!ekm_scansprites_shape_ok(1, 0, 2, 1, 1));
  CHECK(!ekm_scansprites_shape_ok(1, 1, 1, 1, 1));
  CHECK(!ekm_scansprites_shape_ok(1, 1, 3, 1, 1));
  CHECK_MSG(!ekm_scansprites_shape_ok(1, 1, 2, 0, 1),
            "with neither load naming the multiply's base, nothing tells "
            "tsprite from sprite");
  CHECK(!ekm_scansprites_shape_ok(1, 1, 2, 1, 0));

  CHECK(ekm_scansprites_slots_ok(1, 1, 1, 0x05A43F0, 0x05A45C0, 0x05A4440));
  CHECK(!ekm_scansprites_slots_ok(0, 1, 1, 0x05A43F0, 0x05A45C0, 0x05A4440));
  CHECK(!ekm_scansprites_slots_ok(2, 1, 1, 0x05A43F0, 0x05A45C0, 0x05A4440));
  CHECK(!ekm_scansprites_slots_ok(1, 2, 1, 0x05A43F0, 0x05A45C0, 0x05A4440));
  CHECK(!ekm_scansprites_slots_ok(1, 1, 2, 0x05A43F0, 0x05A45C0, 0x05A4440));
  CHECK_MSG(!ekm_scansprites_slots_ok(1, 1, 1, 0x05A43F0, 0x05A43F0, 0x05A4440),
            "one slot answering for two arrays is a body this cannot read");
  CHECK(!ekm_scansprites_slots_ok(1, 1, 1, 0x05A43F0, 0x05A45C0, 0x05A45C0));
  CHECK(!ekm_scansprites_slots_ok(1, 1, 1, 0, 0x05A45C0, 0x05A4440));

  // The ONE witness that is not a byte pattern is the ARRAY, not the slot.
  CHECK(ekm_sprite_array_agrees(0x127C640, 0x127C640));
  CHECK_MSG(!ekm_sprite_array_agrees(0x127C640, 0x127C640 + EK_SPR_STRIDE),
            "one record along is a different array: it would shift every index");
  CHECK(!ekm_sprite_array_agrees(0, 0x127C640));
  CHECK(!ekm_sprite_array_agrees(0x127C640, 0));
}

static void tb_0103_sprite_chains() {
  // The sector's own sprite chain, not a walk of sprite[]: headspritesect/nextspritesect and sprite[].sectnum are kept
  // in lockstep, but asking every record touches all 16384 each frame (~720KB at 240 fps, the reported choppiness)
  // where polymer_scansprites' loop (polymer.cpp:3893) is O(sprites in the sector).
  CHECK(ekm_sprite_chain_live(0, EK_MAXSPRITES));
  CHECK(ekm_sprite_chain_live(EK_MAXSPRITES - 1, EK_MAXSPRITES));
  CHECK_MSG(!ekm_sprite_chain_live(-1, EK_MAXSPRITES),
            "-1 is the engine's own end of chain (polymer.cpp:3893)");
  CHECK_MSG(!ekm_sprite_chain_live(EK_MAXSPRITES, EK_MAXSPRITES),
            "a link past the array is a bad locate, not a sprite");
  CHECK(!ekm_sprite_chain_live(-32768, EK_MAXSPRITES));

  // The witness that the chain IS the chain: headspritesect/nextspritesect come from an image walk and sprite[] from a
  // runtime content scan, so a link whose record does not name the sector whose chain reached it means one is not what
  // is believed.
  CHECK(ekm_sprite_chain_agrees(17, 17));
  CHECK(ekm_sprite_chain_agrees(0, 0));
  CHECK_MSG(!ekm_sprite_chain_agrees(18, 17),
            "a chain that hands back somebody else's sprite is not the chain");
  CHECK(!ekm_sprite_chain_agrees(-1, 17));

  // The budget: an engine chain is acyclic but an array located by shape is not, so the pass counts links across the
  // frame and refuses rather than spinning.
  CHECK(ekm_chain_budget_ok(0, EK_MAXSPRITES));
  CHECK(ekm_chain_budget_ok(EK_MAXSPRITES, EK_MAXSPRITES));
  CHECK_MSG(!ekm_chain_budget_ok(EK_MAXSPRITES + 1, EK_MAXSPRITES),
            "more links than there are sprites is a cycle");
  CHECK(!ekm_chain_budget_ok(-1, EK_MAXSPRITES));

  // Accepting the two arrays: inside the inlined polymer_scansprites exactly one `movsx r32, word [base + idx*2]` has
  // the index register the `imul r64, r64, 44` multiplies (the sprite index) and that is nextspritesect[].
  CHECK(ekm_chain_shape_ok(1, 1));
  CHECK_MSG(!ekm_chain_shape_ok(0, 1),
            "no sector-indexed load means this window is not the chain loop");
  CHECK(!ekm_chain_shape_ok(1, 0));
  CHECK_MSG(!ekm_chain_shape_ok(2, 1),
            "two candidates is an ambiguous answer, which is worse than none");
  CHECK(!ekm_chain_shape_ok(1, 2));

  CHECK(ekm_chain_slots_ok(1, 1, 0x05A3380, 0x05A3800, 0x05A43F0, 0x05A45C0,
                           0x05A4440));
  CHECK(!ekm_chain_slots_ok(2, 1, 0x05A3380, 0x05A3800, 0x05A43F0, 0x05A45C0,
                            0x05A4440));
  CHECK(!ekm_chain_slots_ok(1, 2, 0x05A3380, 0x05A3800, 0x05A43F0, 0x05A45C0,
                            0x05A4440));
  CHECK(!ekm_chain_slots_ok(1, 1, 0, 0x05A3800, 0x05A43F0, 0x05A45C0,
                            0x05A4440));
  CHECK(!ekm_chain_slots_ok(1, 1, 0x05A3380, 0, 0x05A43F0, 0x05A45C0,
                            0x05A4440));
  CHECK_MSG(!ekm_chain_slots_ok(1, 1, 0x05A3380, 0x05A3380, 0x05A43F0,
                                0x05A45C0, 0x05A4440),
            "one slot cannot be both chain arrays");
  CHECK(!ekm_chain_slots_ok(1, 1, 0x05A43F0, 0x05A3800, 0x05A43F0, 0x05A45C0,
                            0x05A4440));
  CHECK(!ekm_chain_slots_ok(1, 1, 0x05A3380, 0x05A45C0, 0x05A43F0, 0x05A45C0,
                            0x05A4440));
  CHECK(!ekm_chain_slots_ok(1, 1, 0x05A3380, 0x05A4440, 0x05A43F0, 0x05A45C0,
                            0x05A4440));

  // Spans through the rule the other three slots use; counts are the engine's declarations,
  // headspritesect[MAXSECTORS+1] and nextspritesect[MAXSPRITES] (build.h:885, :887).
  CHECK(EK_HEADSPRITESECT_SLOTS == EK_MAXSECTORS + 1);
  CHECK(EK_NEXTSPRITESECT_SLOTS == EK_MAXSPRITES);
  const size_t img = 0x1AA6000;
  CHECK(ekm_tsprite_slot_ok(0x05A3380, 0x0ACF6C0, img, 1,
                            (size_t)EK_HEADSPRITESECT_SLOTS * 2));
  CHECK(ekm_tsprite_slot_ok(0x05A3800, 0x0AC76C0, img, 1,
                            (size_t)EK_NEXTSPRITESECT_SLOTS * 2));
  CHECK_MSG(!ekm_tsprite_slot_ok(0x05A3380, img - 16, img, 1,
                                 (size_t)EK_HEADSPRITESECT_SLOTS * 2),
            "an array that runs off the end of the image is not the array");
}

static void tb_0105_sweep_cull() {
  // The sweep may not copy the engine arm's GL_CULL_FACE: polymer_glinit enables it (polymer.cpp:967-971) but
  // engine.cpp:9563 disables it after polymer_drawrooms, so at our detour culling is OFF and back faces write depth
  // and occlude the room behind. Enable only when off, and restore exactly.
  {
    const EkSweepCull c = ekm_sweep_cull(1, 0);
    CHECK_MSG(c.enable == 1, "culling off at the sweep is the shipped case: turn it on");
    CHECK_MSG(c.restore == 1, "and put it back, because glbuild's cache still reads FALSE");
    CHECK(c.refuse == 0);
  }
  {
    const EkSweepCull c = ekm_sweep_cull(1, 1);
    CHECK_MSG(c.enable == 0 && c.restore == 0,
              "culling already on is left exactly as found, cache and all");
    CHECK(c.refuse == 0);
  }
  // No entry points, no sweep: drawing without them is the defect, and the refusal costs the whole-map row, never the
  // game.
  {
    const EkSweepCull c = ekm_sweep_cull(0, 0);
    CHECK_MSG(c.refuse == 1, "glEnable/glDisable/glIsEnabled unresolved refuses the sweep");
    CHECK(c.enable == 0 && c.restore == 0);
  }
  {
    const EkSweepCull c = ekm_sweep_cull(0, 1);
    CHECK_MSG(c.refuse == 1, "a resolved-looking state does not excuse a missing entry point");
    CHECK(c.enable == 0 && c.restore == 0);
  }
  // Never an unpaired change, over every combination the caller can present.
  for (int e = 0; e <= 1; e++) {
    for (int on = 0; on <= 1; on++) {
      const EkSweepCull c = ekm_sweep_cull(e, on);
      CHECK_MSG(c.enable == c.restore, "an enable this sweep makes is an enable it undoes");
      CHECK_MSG(!(c.refuse && c.enable), "a refused sweep touches no GL state at all");
    }
  }
  CHECK(ekm_sweep_cull(1, 0).enable == 1 || ekm_sweep_cull(1, 0).enable == 0);
}

// The sweep had no visibility test: it drew every sector the portal walk did not reach, and all its walls, each frame
// the whole-map row was on (~60 fps against ~240 off).
static void tb_0106_sweep_frustum() {
  // The mapping cannot be guessed: polymer_drawrooms:1121-1123 sets pos[] and polymer_updatesector fills the vertex
  // buffers.
  {
    const EkGlPoint p = ekm_build_to_gl(1024, -2048, 8192);
    CHECK_MSG(approx(p.x, -2048.0f, 0.001f), "GL x is the build y");
    CHECK_MSG(approx(p.y, -512.0f, 0.001f), "GL y is -build z / 16");
    CHECK_MSG(approx(p.z, -1024.0f, 0.001f), "GL z is -build x");
    const EkGlPoint o = ekm_build_to_gl(0, 0, 0);
    CHECK_MSG(o.x == 0.0f && o.y == 0.0f && o.z == 0.0f, "and the origin maps to the origin");
  }

  // A 90-degree symmetric perspective and a pure-translation modelview, both column-major as GL stores them.
  float P[16], M[16], PM[16], Z0[16];
  memset(P, 0, sizeof(P));
  memset(Z0, 0, sizeof(Z0));
  P[0] = 1.0f;          // 1 / tan(45 deg), aspect 1
  P[5] = 1.0f;
  P[10] = -1.0002f;
  P[11] = -1.0f;
  P[14] = -2.0002f;
  const EkGlPoint cam = ekm_build_to_gl(4096, -1024, 512);
  memset(M, 0, sizeof(M));
  M[0] = M[5] = M[10] = M[15] = 1.0f;
  M[12] = -cam.x;
  M[13] = -cam.y;
  M[14] = -cam.z;
  ekm_mat4_mul(P, M, PM);
  CHECK_MSG(approx(PM[0], 1.0f, 0.0001f), "the product keeps the projection's own first column");
  CHECK_MSG(approx(PM[11], -1.0f, 0.0001f),
            "and its w row, which is what makes the side planes perspective");
  CHECK_MSG(approx(PM[12], -cam.x, 0.001f),
            "and carries the modelview's translation into column three");

  const EkSidePlane L = ekm_plane_normalise(ekm_frustum_left(PM));
  const EkSidePlane R = ekm_plane_normalise(ekm_frustum_right(PM));
  CHECK_MSG(ekm_plane_usable(L) == 1 && ekm_plane_usable(R) == 1,
            "a real matrix gives two usable side planes");
  CHECK_MSG(ekm_plane_usable(ekm_plane_normalise(ekm_frustum_left(Z0))) == 0,
            "a zeroed matrix gives no plane, and no plane means no cull at all");

  const float offL = ekm_cull_offset(L, cam.y, cam.y, 0.0f);
  const float offR = ekm_cull_offset(R, cam.y, cam.y, 0.0f);
  CHECK_MSG(ekm_cull_column_out(L, offL, cam.x, cam.z - 1000.0f) == 0 &&
            ekm_cull_column_out(R, offR, cam.x, cam.z - 1000.0f) == 0,
            "straight ahead is inside both side planes");
  CHECK_MSG(ekm_cull_column_out(L, offL, cam.x - 3000.0f, cam.z - 1000.0f) == 1,
            "well off the left edge is outside the left plane");
  CHECK_MSG(ekm_cull_column_out(R, offR, cam.x - 3000.0f, cam.z - 1000.0f) == 0,
            "and inside the right one, which is why one plane alone must carry a reject");
  CHECK_MSG(ekm_cull_column_out(R, offR, cam.x + 3000.0f, cam.z - 1000.0f) == 1,
            "well off the right edge is outside the right plane");
  CHECK_MSG(ekm_cull_column_out(L, offL, cam.x + 500.0f, cam.z - 1000.0f) == 0 &&
            ekm_cull_column_out(R, offR, cam.x + 500.0f, cam.z - 1000.0f) == 0,
            "just off centre and ahead is inside both");
  CHECK_MSG(ekm_cull_column_out(L, offL, cam.x, cam.z + 1000.0f) == 1 &&
            ekm_cull_column_out(R, offR, cam.x, cam.z + 1000.0f) == 1,
            "directly behind the camera is outside both side planes");
  // At the edge of the view, where a wrong reject would happen.
  CHECK_MSG(ekm_cull_column_out(L, offL, cam.x - 1000.0f, cam.z - 1000.0f) == 0,
            "a corner exactly on the left edge of the view is not rejected");
  const float offLm = ekm_cull_offset(L, cam.y, cam.y, EK_CULL_MARGIN);
  CHECK_MSG(ekm_cull_column_out(L, offLm, cam.x - 1000.0f - EK_CULL_MARGIN * 0.5f,
                                cam.z - 1000.0f) == 0,
            "and the margin keeps drawing a corner that has only just crossed it");

  // The apex gate: every side plane of a perspective frustum passes through the eye, so the camera must read zero on
  // both.
  {
    const float fL = ekm_plane_eval(L, cam);
    const float fR = ekm_plane_eval(R, cam);
    CHECK_MSG(approx(fL, 0.0f, 0.5f), "the camera is ON the left plane: it is the frustum's apex");
    CHECK_MSG(approx(fR, 0.0f, 0.5f), "and on the right plane too");
    CHECK_MSG(ekm_frustum_apex_ok(fL, fR, EK_CULL_APEX_TOL) == 1,
              "so the gate passes for the matrices that drew this frame");
    CHECK_MSG(ekm_frustum_apex_ok(EK_CULL_APEX_TOL + 1.0f, 0.0f, EK_CULL_APEX_TOL) == 0,
              "a frustum whose apex is not the camera is refused, and nothing is culled");
    CHECK_MSG(ekm_frustum_apex_ok(0.0f, -(EK_CULL_APEX_TOL + 1.0f), EK_CULL_APEX_TOL) == 0,
              "on either plane and in either sign");
  }

  {
    EkCullSlab s = ekm_slab_init();
    CHECK_MSG(ekm_slab_usable(s) == 0, "an empty slab is not a bound, and no bound means no cull");
    s = ekm_slab_add(s, -8192, 4096);
    s = ekm_slab_add(s, -16384, 1024);
    CHECK_MSG(ekm_slab_usable(s) == 1, "two sectors are a bound");
    CHECK_MSG(s.lo == -16384, "the slab's top is the LOWEST ceilingz any sector has");
    CHECK_MSG(s.hi == 4096, "and its bottom the highest floorz");
    CHECK_MSG(approx(ekm_slab_ylo(s), -256.0f, 0.001f), "GL y's low end is -maxfloorz/16");
    CHECK_MSG(approx(ekm_slab_yhi(s), 1024.0f, 0.001f), "and its high end -minceilingz/16");
  }

  // The offset takes the corner's best case: f is linear in y, so its maximum over the slab is at one end, and the
  // wrong end rejects a column the other would keep.
  {
    EkSidePlane p;
    p.a = 0.0f; p.b = 1.0f; p.c = 0.0f; p.d = 0.0f;
    CHECK_MSG(approx(ekm_cull_offset(p, -100.0f, 300.0f, 0.0f), 300.0f, 0.001f),
              "a positive b takes the slab's high end");
    p.b = -1.0f;
    CHECK_MSG(approx(ekm_cull_offset(p, -100.0f, 300.0f, 0.0f), 100.0f, 0.001f),
              "and a negative b the low end, for the same reason");
    p.b = 0.0f; p.d = -50.0f;
    CHECK_MSG(approx(ekm_cull_offset(p, -100.0f, 300.0f, 64.0f), 14.0f, 0.001f),
              "and the margin is added on top, so every doubt draws");
  }

  {
    CHECK_MSG(ekm_cull_reject(6, 6, 0, 0, 1) == 1, "every corner outside the left plane is a reject");
    CHECK_MSG(ekm_cull_reject(6, 0, 6, 0, 1) == 1, "and every corner outside the right one");
    CHECK_MSG(ekm_cull_reject(6, 5, 1, 0, 1) == 0,
              "five of six is NOT a reject: the sector straddles the edge and is drawn");
    CHECK_MSG(ekm_cull_reject(6, 3, 3, 0, 1) == 0,
              "and corners split across the two planes is the view looking BETWEEN them");
    CHECK_MSG(ekm_cull_reject(6, 6, 6, 1, 1) == 0,
              "a sloped sector or a sloped neighbour is never rejected, however far off screen");
    CHECK_MSG(ekm_cull_reject(6, 6, 6, 0, 0) == 0,
              "and nothing at all is rejected when the frustum could not be built");
    CHECK_MSG(ekm_cull_reject(2, 2, 2, 0, 1) == 0,
              "fewer than three corners is not a polygon and is not a verdict");
    CHECK_MSG(ekm_cull_reject(0, 0, 0, 0, 1) == 0,
              "and no corners read at all is the case that must never reject");
  }

  // The neighbour's z that this sector's wall panels borrow: polymer_updatewall builds a two-sided wall's quad from
  // ns->floor.buffer and ns->ceil.buffer (polymer.cpp:3404-3410, :3478-3481), so walls leave their own z band when the
  // neighbour's differs, and a SLOPED neighbour's band is not what its record says.
  {
    CHECK_MSG(ekm_cull_neighbour_blocks(-1, 100, 0) == 0,
              "a one-sided wall has no neighbour geometry to borrow");
    CHECK_MSG(ekm_cull_neighbour_blocks(5, 100, 0) == 0,
              "an unsloped neighbour inside the chain is inside the slab");
    CHECK_MSG(ekm_cull_neighbour_blocks(5, 100, 1) == 1,
              "a sloped neighbour blocks: its panels leave the slab the record describes");
    CHECK_MSG(ekm_cull_neighbour_blocks(100, 100, 0) == 1,
              "and so does a neighbour past where the chain stopped, never read at all");
    CHECK_MSG(ekm_cull_neighbour_blocks(4095, 16, 0) == 1, "however far past it");
  }
}

static void tb_0107_sprite_authority() {
  // The image walk is a proof and the content scan a heuristic, so the proof decides for sprite[]: the content scan once
  // named a different SLOT (rva 0x0DDC150, the CON VM's vm.pSprite) for the same array because the live sprite count
  // moves while it walks 21MB.
  CHECK(ekm_sprite_source_pick(EK_SPR_AUTH_IMAGE, true, true, false) ==
        EK_SPR_SRC_IMAGE);
  CHECK_MSG(ekm_sprite_source_pick(EK_SPR_AUTH_IMAGE, true, false, false) ==
                EK_SPR_SRC_IMAGE,
            "the proof does not wait for the heuristic to catch up");
  CHECK_MSG(ekm_sprite_source_pick(EK_SPR_AUTH_IMAGE, false, true, true) ==
                EK_SPR_SRC_NONE,
            "and the slot the engine reads holding nothing live means no level "
            "yet, never the scan's different answer instead");
  CHECK_MSG(ekm_sprite_source_pick(EK_SPR_AUTH_WAITING, false, true, false) ==
                EK_SPR_SRC_NONE,
            "nothing is adopted while the image walk has still to speak");
  CHECK_MSG(ekm_sprite_source_pick(EK_SPR_AUTH_WAITING, false, true, true) ==
                EK_SPR_SRC_SCAN,
            "but a walk that never speaks must not cost the ESP its array");
  CHECK(ekm_sprite_source_pick(EK_SPR_AUTH_REFUSED, false, true, false) ==
        EK_SPR_SRC_SCAN);
  CHECK(ekm_sprite_source_pick(EK_SPR_AUTH_REFUSED, false, false, false) ==
        EK_SPR_SRC_NONE);
}

// Screenshot mode's hazard is not a crash: all three cvars it hides are the PLAYER'S OWN and the engine writes them to
// settings.cfg on exit and on every save (config.cpp:1044 CONFIG_WriteSettings via G_SavePlayerMaybeMulti,
// savegame.cpp:957).
static void tb_0108_shot_rules() {
  CHECK(shot_active(true, true, false) == true);
  CHECK(shot_active(false, true, false) == false);
  CHECK(shot_active(true, false, false) == false);
  CHECK_MSG(shot_active(true, true, true) == false,
            "a menu owning the screen has to give the hud back. Not for "
            "settings.cfg: input.cpp swallows Esc for the whole flight, so "
            "Duke's menu cannot open under the camera and only the ekmod.ini "
            "stash covers an unclean exit");
  CHECK(shot_active(false, false, false) == false);
  CHECK(shot_active(false, true, true) == false);
  CHECK(shot_active(true, false, true) == false);
  CHECK(shot_active(false, false, true) == false);

  CHECK(shot_menu_term(true) == true);
  CHECK(shot_menu_term(false) == false);

  // Only the two transitions.
  CHECK(shot_edge(false, false) == SHOT_EDGE_NONE);
  CHECK(shot_edge(true, true) == SHOT_EDGE_NONE);
  CHECK(shot_edge(false, true) == SHOT_EDGE_ON);
  CHECK(shot_edge(true, false) == SHOT_EDGE_OFF);

  // The ON edge saves and hides, writing 0 because it is each cvar's own off state.
  ShotHold h;
  h.held = 0;
  h.mask = 0;
  h.saved[0] = -1; h.saved[1] = -1; h.saved[2] = -1;
  const int cur[SHOT_CVARS] = { 1, 2, 1 };
  int outMask = -1;
  int outVal[SHOT_CVARS] = { -1, -1, -1 };
  CHECK(shot_hold_step(&h, SHOT_EDGE_ON, 0x7, cur, &outMask, outVal) == SHOT_ACT_HIDE);
  CHECK(h.held == 1);
  CHECK(h.mask == 0x7);
  CHECK(h.saved[SHOT_CVAR_CROSSHAIR] == 1);
  CHECK(h.saved[SHOT_CVAR_WEAPON] == 2);
  CHECK(h.saved[SHOT_CVAR_STATS] == 1);
  CHECK(outMask == 0x7);
  CHECK(outVal[0] == 0 && outVal[1] == 0 && outVal[2] == 0);

  const int poisoned[SHOT_CVARS] = { 0, 0, 0 };
  outMask = -1;
  CHECK_MSG(shot_hold_step(&h, SHOT_EDGE_ON, 0x7, poisoned, &outMask, outVal) == SHOT_ACT_NONE,
            "a second ON edge must not re-save: what is on screen is ours");
  CHECK(h.saved[SHOT_CVAR_WEAPON] == 2);
  CHECK(outMask == 0);
  CHECK(shot_hold_step(&h, SHOT_EDGE_NONE, 0x7, poisoned, &outMask, outVal) == SHOT_ACT_NONE);
  CHECK(h.saved[SHOT_CVAR_WEAPON] == 2);

  outMask = -1;
  outVal[0] = -1; outVal[1] = -1; outVal[2] = -1;
  CHECK(shot_hold_step(&h, SHOT_EDGE_OFF, 0x7, poisoned, &outMask, outVal) == SHOT_ACT_RESTORE);
  CHECK(h.held == 0);
  CHECK(outMask == 0x7);
  CHECK(outVal[SHOT_CVAR_CROSSHAIR] == 1);
  CHECK(outVal[SHOT_CVAR_WEAPON] == 2);
  CHECK(outVal[SHOT_CVAR_STATS] == 1);
  {
    ShotHold b = { 0, 0, { 0, 0, 0 }, 0 };
    const int real[SHOT_CVARS] = { 1, 2, 1 };
    int m = 0, v[SHOT_CVARS] = { 0, 0, 0 };
    CHECK(shot_hold_step(&b, SHOT_EDGE_ON, 0x7, real, &m, v) == SHOT_ACT_HIDE);
    CHECK(shot_hold_step(&b, SHOT_EDGE_OFF, 0x7, real, &m, v) == SHOT_ACT_RESTORE);
    CHECK(v[SHOT_CVAR_WEAPON] == 2);

    const int zeroed[SHOT_CVARS] = { 0, 0, 0 };
    CHECK(shot_hold_step(&b, SHOT_EDGE_ON, 0x7, zeroed, &m, v) == SHOT_ACT_HIDE);
    CHECK_MSG(m == 0x7, "the second ON still hides all three");
    m = 0; v[0] = v[1] = v[2] = -1;
    CHECK(shot_hold_step(&b, SHOT_EDGE_OFF, 0x7, zeroed, &m, v) == SHOT_ACT_RESTORE);
    CHECK_MSG(v[SHOT_CVAR_WEAPON] == 2,
              "the second OFF restores the session's baseline, not the zeros");
    CHECK(v[SHOT_CVAR_CROSSHAIR] == 1 && v[SHOT_CVAR_STATS] == 1);
  }

  // A player's own zero is still his: the baseline is his value, so someone playing with the crosshair off gets it back
  // off.
  {
    ShotHold z = { 0, 0, { 0, 0, 0 }, 0 };
    const int hisZero[SHOT_CVARS] = { 0, 2, 1 };
    int m = 0, v[SHOT_CVARS] = { -1, -1, -1 };
    CHECK(shot_hold_step(&z, SHOT_EDGE_ON, 0x7, hisZero, &m, v) == SHOT_ACT_HIDE);
    CHECK(shot_hold_step(&z, SHOT_EDGE_OFF, 0x7, hisZero, &m, v) == SHOT_ACT_RESTORE);
    CHECK_MSG(v[SHOT_CVAR_CROSSHAIR] == 0, "his crosshair stays off");
    CHECK(v[SHOT_CVAR_WEAPON] == 2);
  }

  outMask = -1;
  CHECK_MSG(shot_hold_step(&h, SHOT_EDGE_OFF, 0x7, poisoned, &outMask, outVal) == SHOT_ACT_NONE,
            "a second OFF edge must write nothing at all");
  CHECK(outMask == 0);

  ShotHold p;
  p.held = 0;
  p.mask = 0;
  p.saved[0] = -1; p.saved[1] = -1; p.saved[2] = -1;
  const int two = (1 << SHOT_CVAR_CROSSHAIR) | (1 << SHOT_CVAR_WEAPON);
  CHECK(shot_hold_step(&p, SHOT_EDGE_ON, two, cur, &outMask, outVal) == SHOT_ACT_HIDE);
  CHECK(outMask == two);
  CHECK(p.saved[SHOT_CVAR_STATS] == 0);
  outMask = -1;
  CHECK(shot_hold_step(&p, SHOT_EDGE_OFF, 0x7, cur, &outMask, outVal) == SHOT_ACT_RESTORE);
  CHECK_MSG(outMask == two, "only what the ON edge took may be given back");

  CHECK(shot_hold_step(0, SHOT_EDGE_ON, 0x7, cur, &outMask, outVal) == SHOT_ACT_NONE);
  CHECK(shot_hold_step(&h, SHOT_EDGE_ON, 0x7, 0, &outMask, outVal) == SHOT_ACT_NONE);
  CHECK(shot_hold_step(&h, SHOT_EDGE_ON, 0x7, cur, 0, outVal) == SHOT_ACT_NONE);
  CHECK(shot_hold_step(&h, SHOT_EDGE_ON, 0x7, cur, &outMask, 0) == SHOT_ACT_NONE);
  CHECK(h.held == 0);

  // Our own overlay goes too: a comparison shot with ESP boxes on it is not clean.
  CHECK(shot_hides_overlays(true) == true);
  CHECK(shot_hides_overlays(false) == false);
}

static void tb_0109_shot_stash() {
  // Trustworthy only when every held, readable cvar still reads exactly 0, the fingerprint of an unhealed crash.
  const int hidden[SHOT_CVARS] = { 0, 0, 0 };
  CHECK(shot_stash_valid(0x7, hidden) == true);
  CHECK(shot_stash_valid(0, hidden) == false);
  const int drifted[SHOT_CVARS] = { 0, 1, 0 };
  CHECK_MSG(shot_stash_valid(0x7, drifted) == false,
            "one cvar off the forced 0 means somebody already made their own "
            "choice since the crash -- the whole stash is stale, not just that bit");
  CHECK_MSG(shot_stash_valid(0x2, drifted) == false,
            "checked only over the mask bit that drifted");
  CHECK_MSG(shot_stash_valid(0x1, drifted) == true,
            "a bit outside the checked mask cannot spoil validity");

  const int saved[SHOT_CVARS] = { 1, 2, 1 };
  int outMask = -1;
  int outVal[SHOT_CVARS] = { -1, -1, -1 };

  CHECK(shot_stash_replay(0, 0x7, saved, 0x7, hidden, &outMask, outVal) == SHOT_ACT_NONE);
  CHECK(outMask == 0);

  outMask = -1;
  CHECK(shot_stash_replay(1, 0x7, saved, 0x7, hidden, &outMask, outVal) == SHOT_ACT_RESTORE);
  CHECK(outMask == 0x7);
  CHECK(outVal[SHOT_CVAR_CROSSHAIR] == 1);
  CHECK(outVal[SHOT_CVAR_WEAPON] == 2);
  CHECK(outVal[SHOT_CVAR_STATS] == 1);

  // Held but a later session changed one: do not fight that choice, refuse the whole replay.
  outMask = -1;
  CHECK_MSG(shot_stash_replay(1, 0x7, saved, 0x7, drifted, &outMask, outVal) == SHOT_ACT_NONE,
            "a deliberate change since the crash beats a remembered value");
  CHECK(outMask == 0);

  // Held but this build cannot read one: replay only the verifiable bits.
  outMask = -1;
  const int partialRead = (1 << SHOT_CVAR_CROSSHAIR) | (1 << SHOT_CVAR_WEAPON);
  CHECK(shot_stash_replay(1, 0x7, saved, partialRead, hidden, &outMask, outVal) == SHOT_ACT_RESTORE);
  CHECK_MSG(outMask == partialRead, "cannot verify or write a cvar this build cannot currently read");

  outMask = -1;
  CHECK(shot_stash_replay(1, 0, saved, 0x7, hidden, &outMask, outVal) == SHOT_ACT_NONE);
}

static void tb_0110_shot_statusbar() {
  CHECK(shot_sbar_shape(true, true, true) == true);
  CHECK(shot_sbar_shape(false, true, true) == false);
  CHECK(shot_sbar_shape(true, false, true) == false);
  CHECK(shot_sbar_shape(true, true, false) == false);
  CHECK(shot_sbar_shape(false, false, false) == false);

  CHECK(shot_sbar_target_ok(1, 0) == true);
  CHECK_MSG(shot_sbar_target_ok(0, 0) == false, "nothing shaped is a refusal");
  CHECK_MSG(shot_sbar_target_ok(2, 0) == false,
            "two shaped is a refusal, not a pick");
  CHECK_MSG(shot_sbar_target_ok(1, 1) == false,
            "a scan that faulted counted partially and cannot claim uniqueness");

  // Exactly one call site inside G_DisplayRest may reach it: screens.cpp calls G_DrawStatusBar again under
  // SPLITSCREEN_MOD_HACKS, and hiding one bar while the other draws is not the feature.
  CHECK(shot_sbar_locate_ok(1, 0) == true);
  CHECK_MSG(shot_sbar_locate_ok(0, 0) == false, "no site is a refusal");
  CHECK_MSG(shot_sbar_locate_ok(2, 0) == false,
            "two sites is a refusal, not a pick");
  CHECK_MSG(shot_sbar_locate_ok(1, 1) == false,
            "a scan that faulted counted partially and cannot claim uniqueness");

  CHECK(shot_draws_statusbar(true) == false);
  CHECK(shot_draws_statusbar(false) == true);

  CHECK_MSG(shot_draws_statusbar(shot_active(true, true, false)) == false,
            "flying, the row on, no menu: the bar is hidden with the weapon");
  CHECK_MSG(shot_draws_statusbar(shot_active(false, true, false)) == true,
            "the camera down gives the bar back");
  CHECK_MSG(shot_draws_statusbar(shot_active(true, false, false)) == true,
            "the row switched off mid-flight gives the bar back");
  CHECK_MSG(shot_draws_statusbar(shot_active(true, true, true)) == true,
            "a menu open gives the bar back, exactly as it gives the weapon back");
}

// The scuba mask is three call sites, not one: P_DisplayScuba is inlined into G_DisplayRest so there is no `call` to
// redirect.
static void tb_0111_shot_scuba() {
  CHECK_MSG(SHOT_SCUBA_X_RIGHT == 277,
            "320 - 43 is the right half's x, and `mov ecx, 277` occurs exactly "
            "once in the whole of eduke32.exe");
  CHECK_MSG(SHOT_SCUBA_BITS == 0x40012,
            "2 + 16 + DRAWEAP_CENTER, which is what the image stores");
  CHECK_MSG(SHOT_SCUBA_BITS_FLIP == 0x40016,
            "2 + 4 + 16 + DRAWEAP_CENTER, the right half's extra flip bit");

  CHECK(shot_scuba_anchor_ok(1, 0) == true);
  CHECK_MSG(shot_scuba_anchor_ok(0, 0) == false, "no anchor is a refusal");
  CHECK_MSG(shot_scuba_anchor_ok(2, 0) == false,
            "two positions is a refusal, not a pick");
  CHECK_MSG(shot_scuba_anchor_ok(1, 1) == false,
            "a faulted scan cannot claim uniqueness");

  // The three anchored sites must agree on ONE target, reached from exactly those three sites inside G_DisplayRest.
  CHECK(shot_scuba_locate_ok(3, 1, 3, 0) == true);
  CHECK_MSG(shot_scuba_locate_ok(2, 1, 3, 0) == false,
            "two anchors found is not the scuba mask's three draws");
  CHECK_MSG(shot_scuba_locate_ok(3, 2, 3, 0) == false,
            "three sites reaching two different functions is not one callee");
  CHECK_MSG(shot_scuba_locate_ok(3, 1, 2, 0) == false,
            "fewer sites in the bracket than anchors means one was missed");
  CHECK_MSG(shot_scuba_locate_ok(3, 1, 4, 0) == false,
            "a fourth site reaching the same callee is a draw we did not name");
  CHECK_MSG(shot_scuba_locate_ok(3, 1, 3, 1) == false,
            "a faulted scan cannot claim any of those counts");

  CHECK(shot_scuba_tile(SHOT_SCUBA_X_LEFT, 0, SHOT_SCUBA_BITS) == true);
  CHECK(shot_scuba_tile(SHOT_SCUBA_X_SEAM, 0, SHOT_SCUBA_BITS) == true);
  CHECK(shot_scuba_tile(SHOT_SCUBA_X_RIGHT, 0, SHOT_SCUBA_BITS_FLIP) == true);
  CHECK_MSG(shot_scuba_tile(SHOT_SCUBA_X_RIGHT, 0, SHOT_SCUBA_BITS) == false,
            "the right half without its flip bit is not a call we named");
  CHECK_MSG(shot_scuba_tile(SHOT_SCUBA_X_LEFT, 0, SHOT_SCUBA_BITS_FLIP) == false,
            "the left half with a flip bit is not a call we named");
  CHECK_MSG(shot_scuba_tile(SHOT_SCUBA_X_LEFT, 1, SHOT_SCUBA_BITS) == false,
            "the mask is drawn unshaded; any shade is somebody else's tile");
  CHECK_MSG(shot_scuba_tile(0, 0, SHOT_SCUBA_BITS) == false, "x 0 is not a half");
  CHECK_MSG(shot_scuba_tile(160, 0, SHOT_SCUBA_BITS_FLIP) == false,
            "a centred tile is the weapon's, and the weapon has its own cvar");

  // The mask goes on the ON edge that hides the weapon and returns on the OFF edge that restores it, with no second
  // lifecycle.
  CHECK(shot_draws_scuba(true, SHOT_SCUBA_X_LEFT, 0, SHOT_SCUBA_BITS) == false);
  CHECK(shot_draws_scuba(false, SHOT_SCUBA_X_LEFT, 0, SHOT_SCUBA_BITS) == true);
  CHECK_MSG(shot_draws_scuba(true, 160, 0, 0) == true,
            "a call that is not one of the three forwards even while active");
  CHECK_MSG(shot_draws_scuba(shot_active(true, true, false), SHOT_SCUBA_X_RIGHT, 0,
                             SHOT_SCUBA_BITS_FLIP) == false,
            "flying, the row on, no menu: the mask goes with the weapon");
  CHECK_MSG(shot_draws_scuba(shot_active(false, true, false), SHOT_SCUBA_X_RIGHT, 0,
                             SHOT_SCUBA_BITS_FLIP) == true,
            "the camera down gives the mask back");
  CHECK_MSG(shot_draws_scuba(shot_active(true, false, false), SHOT_SCUBA_X_RIGHT, 0,
                             SHOT_SCUBA_BITS_FLIP) == true,
            "the row switched off mid-flight gives the mask back");
  CHECK_MSG(shot_draws_scuba(shot_active(true, true, true), SHOT_SCUBA_X_RIGHT, 0,
                             SHOT_SCUBA_BITS_FLIP) == true,
            "a menu open gives the mask back, as it gives the weapon back");

  // All three or none: a patch count short of three makes every surviving thunk forward, so a half-installed or
  // half-removed redirect draws the mask as the engine would.
  CHECK(shot_scuba_installed(3) == true);
  CHECK_MSG(shot_scuba_installed(0) == false, "nothing patched is not in");
  CHECK_MSG(shot_scuba_installed(1) == false, "one of three is not in");
  CHECK_MSG(shot_scuba_installed(2) == false, "two of three is not in");
  CHECK_MSG(shot_scuba_installed(4) == false,
            "more than three means the locate counted something else");
}

// The rules that accept prwalls[]'s refptr SLOT.
static void tb_0112_prwalls_slot_rules() {
  CHECK(ekm_prw_shape_ok(1, 1));
  CHECK_MSG(!ekm_prw_shape_ok(2, 1), "two candidate loads name nothing");
  CHECK(!ekm_prw_shape_ok(1, 2));
  CHECK(!ekm_prw_shape_ok(0, 0));

  // Witnesses are prsectors[]'s complement: drawwall reads prwalls[] (polymer.cpp:3652) and drawsector never does
  // (3033-3142), the pair that tells the arrays apart. polymer_updatesector is NOT a term (see sectorstate.h).
  CHECK(ekm_prw_witness_ok(0, 1, 1, 0));
  CHECK_MSG(!ekm_prw_witness_ok(1, 1, 1, 0), "polymer_drawsector never reads it");
  CHECK_MSG(!ekm_prw_witness_ok(0, 1, 0, 0), "polymer_drawwall always does");
  CHECK(!ekm_prw_witness_ok(0, 0, 1, 0));
  CHECK_MSG(!ekm_prw_witness_ok(0, 1, 1, 1),
            "prsectors[]'s own slot is not this one");

  // The span is MAXWALLS pointers, not MAXSECTORS.
  const size_t img = 0x1AA6000;
  const size_t slot = 0x5A3E10, arr = 0x10E62C0;
  CHECK(ekm_prw_span_ok(slot, arr, img, 1));
  CHECK_MSG(!ekm_prw_span_ok(slot, arr, img, 0), "a slot in a code section");
  CHECK(!ekm_prw_span_ok(slot, 0, img, 1));
  CHECK(!ekm_prw_span_ok(slot, arr, 0, 1));
  CHECK_MSG(!ekm_prw_span_ok(slot, img - EK_MAXWALLS * sizeof(void*) + 8, img, 1),
            "an array with room for all but one pointer");
  CHECK(ekm_prw_span_ok(slot, img - EK_MAXWALLS * sizeof(void*), img, 1));
  CHECK(ekm_prs_span_ok(slot, img - EK_MAXSECTORS * sizeof(void*), img, 1));
  CHECK_MSG(!ekm_prw_span_ok(slot, img - EK_MAXSECTORS * sizeof(void*), img, 1),
            "room for the sectors is not room for the walls");
}

static void tb_0113_bots_stand_down() {
  CHECK(freecam_bots_run(false));
  CHECK_MSG(!freecam_bots_run(true),
            "a body the player is not behind does not aim and does not fire");
}

// Who owes the game a release: release_held_keys forges a WM_KEYUP per physically held key and may never forge one for
// a key the game never saw pressed, which covers every key swallowed while the mod owned the keyboard (opening the
// panel mid-flight with W held).
static void tb_0114_input_ownership() {
  CHECK_MSG(ekm_input_owned_edge(0, 1),
            "the first claim is the one that empties the game's keyboard");
  CHECK_MSG(!ekm_input_owned_edge(1, 1),
            "the panel opening mid-flight forges nothing: the game saw none of it");
  CHECK_MSG(!ekm_input_owned_edge(1, 0), "nothing is owed on the way back down");
  CHECK(!ekm_input_owned_edge(0, 0));
}

// Telling a healed stash from a changed config, and the enum both index. shot_restore restores the cvars at
// DLL_PROCESS_DETACH and writes no file there, so the stash outlives a working session and the next launch logged a
// false mismatch.
static void tb_0115_stash_healed() {
  const int saved[SHOT_CVARS] = { 1, 2, 1 };
  int cur[SHOT_CVARS] = { 1, 2, 1 };
  const int all = (1 << SHOT_CVAR_CROSSHAIR) | (1 << SHOT_CVAR_WEAPON) |
                  (1 << SHOT_CVAR_STATS);

  // Every checked bit back at its saved value is the fingerprint of a session that restored them, not of a crash.
  CHECK(shot_stash_healed(all, saved, cur));
  CHECK_MSG(!shot_stash_valid(all, cur), "an unhealed crash reads zero, not saved");

  // One bit moved is not healed, as for shot_stash_valid: a mixed session cannot be disentangled.
  cur[SHOT_CVAR_WEAPON] = 0;
  CHECK(!shot_stash_healed(all, saved, cur));
  CHECK_MSG(shot_stash_healed(all & ~(1 << SHOT_CVAR_WEAPON), saved, cur),
            "only the checked bits are ever looked at");
  CHECK_MSG(!shot_stash_healed(0, saved, cur), "an empty mask heals nothing");

  // The indices are append-only, enforced here: they index the config fields AND the on-disk mask bits, and the mask is
  // validated against THIS build's readable set, so a reorder would re-point an older build's saved values at different
  // cvars.
  CHECK(SHOT_CVAR_CROSSHAIR == 0);
  CHECK(SHOT_CVAR_WEAPON == 1);
  CHECK(SHOT_CVAR_STATS == 2);
  CHECK_MSG(SHOT_CVARS == 3, "a fourth cvar is APPENDED, never inserted");
}

static void tb_0117_freecam_transport() {
  // The record field the rule's lotag comes from: sectortypev7 (buildtypes.h:41-55, lotag at :53) is two int16 wall
  // fields, two int32 z fields, two uint16 stats, the picnum/shade/pal/panning pairs per surface and visibility/fogpal,
  // 0x22 bytes; lotag is the int16 there.
  CHECK(EK_SECT_LOTAG == 0x22);
  CHECK(EK_SECT_LOTAG + 2 <= EK_SECTOR_STRIDE);

  // The engine's own numbers, not World Tour's: 6144 is G_MoveTransports' klabs window and landing offset
  // (actors.cpp:3993, :4004), 1080 is P_Emerge's ceiling band (:3843), the water landings are the paired sector's
  // ceilingz/floorz with no offset (:3829, :3859).
  CHECK(FREECAM_TRANSPORT_STAT == 9);            // STAT_TRANSPORT, actors.h:58
  CHECK(FREECAM_TRANSPORT_ZBAND == 6144);
  CHECK(FREECAM_TRANSPORT_EMERGE == 1080);
  // The hold is the water change's own, sign included: P_FinishWaterChange sets -2 (actors.cpp:3878) and
  // player.cpp:5236-5237 counts a negative hold up to zero, out of reach of the top-up at :5233-5234.
  CHECK(FREECAM_TRANSPORT_WATER_HOLD_TICS == -2);


  // One pair: the effector at (1000,2000,-5000) and its owner at (9000,7000,-12000), the effector's sector banded
  // [-20000, 0] and the owner's [-30000, 4096].
  FreecamTransport t;
  t.lotag = 0;
  t.seX = 1000; t.seY = 2000; t.seZ = -5000;
  t.ownerX = 9000; t.ownerY = 7000; t.ownerZ = -12000;
  t.ownerCeilZ = -30000; t.ownerFloorZ = 4096;
  t.secCeilZ = -20000; t.secFloorZ = 0;
  t.tempData4 = 0;

  // The air teleporter: t_data[4] is the effector's ONFLOORZ flag (game.cpp:2930) and zero is the one G_MoveTransports
  // carries a player through the air across, inside 6144 of the effector's z.
  FreecamJump j = freecam_transport_jump(t, 1100, 2050, -4000, 2048);
  CHECK(j.hit);
  // The xy is the pair's separation, not the owner's position: the camera keeps where it stood in the sector
  // (actors.cpp:3999-4000).
  CHECK_MSG(j.x == 9100 && j.y == 7050, "the pair's own offset, applied to the camera");
  CHECK_MSG(j.z == -12000 + 6144, "actors.cpp:4004's landing, below the owner");
  CHECK_MSG(j.hold == 0, "and the engine holds nothing after an airborne carry");

  // A rising camera lands ABOVE it (actors.cpp:4001-4003, a player going up under his own power).
  j = freecam_transport_jump(t, 1100, 2050, -4000, -2048);
  CHECK(j.hit);
  CHECK_MSG(j.z == -12000 - 6144, "actors.cpp:4003's landing, above the owner");
  // A camera moving level takes the engine's else branch.
  CHECK(freecam_transport_jump(t, 1100, 2050, -4000, 0).z == -12000 + 6144);

  // The band is strict, the engine's klabs(...) < 6144, taken with a still camera since the test is against the segment
  // travelled.
  CHECK(!freecam_transport_jump(t, 1100, 2050, -5000 + 6144, 0).hit);
  CHECK(freecam_transport_jump(t, 1100, 2050, -5000 + 6143, 0).hit);

  // A camera faster than the window is still carried: freecam_step moves z up to 40960 a tic (500% and fast key), clean
  // over a band 12288 wide, so the window is tested against the segment travelled.
  CHECK_MSG(freecam_transport_jump(t, 1100, 2050, 24576, 40960).hit,
            "a camera that steps over the airborne band in one tic is carried");
  CHECK_MSG(!freecam_transport_jump(t, 1100, 2050, 24576, 0).hit,
            "and a still camera that far out is not");

  // The floor teleporter is carried, but not from under the pad: the camera here is 1000 below the effector's floor
  // plane, and a player on the ground is never under it.
  t.tempData4 = 1;
  CHECK_MSG(!freecam_transport_jump(t, 1100, 2050, -4000, 0).hit,
            "under the pad's floor plane nothing is carried");

  // The air case is not gated on the sector's lotag: actors.cpp:3993 tests onFloor and the z window alone; only the
  // water cases read sectLotag.
  t.tempData4 = 0;
  t.lotag = 1;
  CHECK(freecam_transport_jump(t, 1100, 2050, -4000, 2048).hit);
  t.lotag = 2;
  CHECK(freecam_transport_jump(t, 1100, 2050, -4000, 2048).hit);

  // Down through a water surface (lotag 1): at or below the sector's floor plane AND still going down, P_Submerge's
  // test with the camera's descent in the crouch key's place (actors.cpp:3814-3815).
  t.tempData4 = 1;
  t.lotag = 1;
  j = freecam_transport_jump(t, 1100, 2050, 0, 2048);
  CHECK(j.hit);
  CHECK_MSG(j.z == -30000, "the pair's own ceilingz, with no offset of ours");
  CHECK(j.x == 9100 && j.y == 7050);
  CHECK_MSG(j.hold == -2, "and a water change holds for its own two tics, counting up");
  CHECK_MSG(!freecam_transport_jump(t, 1100, 2050, -1, 2048).hit,
            "one unit above the surface is still above it");
  CHECK_MSG(!freecam_transport_jump(t, 1100, 2050, 0, 0).hit,
            "a camera that is not descending is not pulled under");
  CHECK_MSG(!freecam_transport_jump(t, 1100, 2050, 0, -2048).hit,
            "and one on its way up is certainly not");

  // Back up (lotag 2): within 1080 of the sector's ceiling and not going down, landing on the paired sector's floor
  // (actors.cpp:3843, :3859).
  t.lotag = 2;
  j = freecam_transport_jump(t, 1100, 2050, -20000 + 1079, -2048);
  CHECK(j.hit);
  CHECK_MSG(j.z == 4096, "the pair's own floorz, with no offset of ours");
  CHECK(j.hold == -2);
  CHECK_MSG(!freecam_transport_jump(t, 1100, 2050, -20000 + 1080, -2048).hit,
            "P_Emerge's band is strict too");
  CHECK_MSG(freecam_transport_jump(t, 1100, 2050, -20000 + 1079, 0).hit,
            "P_Emerge's own vel.z <= 0 includes standing still");
  CHECK_MSG(!freecam_transport_jump(t, 1100, 2050, -20000 + 1079, 2048).hit,
            "but a camera still descending is not thrown back up");

  t.lotag = 3;
  CHECK(!freecam_transport_jump(t, 1100, 2050, 0, 2048).hit);
  t.lotag = -1;
  CHECK(!freecam_transport_jump(t, 1100, 2050, 0, 2048).hit);

  // A pair sharing one point carries nothing: the engine skips an unpaired effector by owner == self, and an owner on
  // the effector could only carry the camera where it already is.
  t.lotag = 0;
  t.tempData4 = 0;
  t.ownerX = t.seX; t.ownerY = t.seY; t.ownerZ = t.seZ;
  CHECK(!freecam_transport_jump(t, 1100, 2050, -4000, 2048).hit);

  j = freecam_transport_jump(t, 1100, 2050, -4000, 2048);
  CHECK(j.x == 1100 && j.y == 2050 && j.z == -4000 && j.hold == 0);

  CHECK_MSG(freecam_transport_hold_next(0).tics == 0, "a camera that has not jumped is never held");
  CHECK(freecam_transport_hold_next(2).tics == 1);
  CHECK(freecam_transport_hold_next(1).tics == 0);
  CHECK_MSG(freecam_transport_hold_next(-3).tics == -2, "a negative hold counts UP");
  CHECK_MSG(freecam_transport_hold_next(-1).tics == 0, "and stops at zero too");

  FreecamTransport down, up;
  down.lotag = 1; down.tempData4 = 1;
  down.seX = 1000; down.seY = 2000; down.seZ = 0;
  down.ownerX = 9000; down.ownerY = 7000; down.ownerZ = 4096;
  down.ownerCeilZ = -30000; down.ownerFloorZ = 4096;
  down.secCeilZ = -20000; down.secFloorZ = 0;
  up = down;
  up.lotag = 2;
  up.seX = 9000; up.seY = 7000; up.seZ = 4096;
  up.ownerX = 1000; up.ownerY = 2000; up.ownerZ = 0;
  up.ownerCeilZ = -20000; up.ownerFloorZ = 0;
  up.secCeilZ = -30000; up.secFloorZ = 4096;

  FreecamJump d = freecam_transport_jump(down, 1100, 2050, 0, 2048);
  CHECK(d.hit && d.z == -30000 && d.x == 9100 && d.y == 7050);
  CHECK(d.z < up.secCeilZ + FREECAM_TRANSPORT_EMERGE);
  // Still descending, so the far half does not throw it back.
  CHECK_MSG(!freecam_transport_jump(up, d.x, d.y, d.z, 2048).hit,
            "a camera still descending is not thrown straight back up");
  // The hold expires by itself in two tics however long the camera stays down; a re-armed hold made the way back
  // impossible.
  int hold = d.hold;
  hold = freecam_transport_hold_next(hold).tics;
  const FreecamHold last = freecam_transport_hold_next(hold);
  hold = last.tics;
  CHECK_MSG(hold == 0, "a hold inside a transport sector still expires, or the return jump never fires");
  CHECK_MSG(freecam_transport_hold_topup(last.expired, 1) == 0,
            "and no top-up can re-arm it, which is the whole of 7404903");

  FreecamJump u = freecam_transport_jump(up, d.x, d.y, d.z, -2048);
  CHECK_MSG(u.hit, "rising at the far half's ceiling, the pair carries it back");
  CHECK_MSG(u.x == 1100 && u.y == 2050 && u.z == 0, "to the point it went down at");
  // The landing above the water does not drop it back in (u.z is the down half's trigger plane), the same defect
  // mirrored.
  CHECK(u.z >= down.secFloorZ);
  CHECK_MSG(!freecam_transport_jump(down, u.x, u.y, u.z, -2048).hit,
            "a camera still rising is not pulled back under");

  // The same round trip through an airborne pair, where each landing is exactly ZBAND from the far effector and that
  // half's test is strict, which is why the engine holds nothing here.
  FreecamTransport airDown, airUp;
  airDown.lotag = 0; airDown.tempData4 = 0;
  airDown.seX = 1000; airDown.seY = 2000; airDown.seZ = -5000;
  airDown.ownerX = 9000; airDown.ownerY = 7000; airDown.ownerZ = -12000;
  airDown.ownerCeilZ = -30000; airDown.ownerFloorZ = 4096;
  airDown.secCeilZ = -20000; airDown.secFloorZ = 0;
  airUp = airDown;
  airUp.seX = 9000; airUp.seY = 7000; airUp.seZ = -12000;
  airUp.ownerX = 1000; airUp.ownerY = 2000; airUp.ownerZ = -5000;
  // The tic after a landing starts at the landing: camera.cpp collapses the previous-tic point onto it
  // (freecam_pose_carry_pos), so the next segment never runs back across the boundary.
  FreecamJump ad = freecam_transport_jump(airDown, 1100, 2050, -4000, 2048);
  CHECK(ad.hit && ad.z == -12000 + 6144);
  CHECK_MSG(!freecam_transport_jump(airUp, ad.x, ad.y, ad.z, 0).hit,
            "the landing sits exactly on the far half's strict band");
  CHECK_MSG(!freecam_transport_jump(airUp, ad.x, ad.y, ad.z + 2048, 2048).hit,
            "and the tic after it, still descending, leaves that edge behind");
  FreecamJump au = freecam_transport_jump(airUp, ad.x, ad.y, ad.z - 1, -2048);
  CHECK_MSG(au.hit && au.z == -5000 - 6144,
            "and rising back through it lands above the near effector, still clear");
  CHECK(!freecam_transport_jump(airDown, au.x, au.y, au.z, 0).hit);
  CHECK(!freecam_transport_jump(airDown, au.x, au.y, au.z - 2048, -2048).hit);



  // The landing is a discontinuity in position only: the previous-tic point collapses onto it so no frame interpolates
  // a streak across the jump (as cam_start and a pasted pose do), but the turn pair is left alone since a transport
  // does not turn the camera.
  FreecamState s;
  memset(&s, 0, sizeof(s));
  s.x = 11; s.y = 22; s.z = 33; s.ang = 100; s.pitch = 50;
  s.px = 1; s.py = 2; s.pz = 3; s.pang = 7; s.ppitch = 8;
  freecam_pose_carry_pos(&s);
  CHECK(s.px == 11 && s.py == 22 && s.pz == 33);
  CHECK_MSG(s.pang == 7 && s.ppitch == 8, "the tic's real turn is not collapsed with it");
  // The whole-pose carry still collapses all five fields, as at the start of every tic and the other two
  // discontinuities.
  freecam_pose_carry(&s);
  CHECK(s.pang == 100 && s.ppitch == 50);
}


// The free camera's "You" marker is a 2D overlay diamond, not a sprite.
static void tb_0118_freecam_marker() {
  // The first term is the live camera, not the switch (World Tour.
  CHECK(freecam_marker_on(true, 1));
  CHECK_MSG(!freecam_marker_on(false, 1),
            "the switch alone, with the camera down, draws nothing");
  CHECK(!freecam_marker_on(true, 0));
  CHECK(!freecam_marker_on(false, 0));

  // Odd row count, so the projected pixel is a real centre row, not a seam.
  CHECK(player_marker_rows() == PLAYER_MARKER_HALF * 2 + 1);
  CHECK_MSG(player_marker_rows() % 2 == 1, "an even diamond has no centre row");

  // The profile is zero at both points, widest in the middle, as wide as tall: a diamond, not a lozenge.
  CHECK(player_marker_row_halfwidth(0) == 0);
  CHECK(player_marker_row_halfwidth(player_marker_rows() - 1) == 0);
  CHECK(player_marker_row_halfwidth(PLAYER_MARKER_HALF) == PLAYER_MARKER_HALF);
  CHECK(player_marker_row_w(PLAYER_MARKER_HALF) == player_marker_rows());

  for (int r = -1; r <= player_marker_rows(); r++) {
    CHECK_MSG(player_marker_row_halfwidth(r) >= 0, "no negative half-width");
    CHECK_MSG(player_marker_row_w(r) >= 1, "every rect is at least one pixel");
  }
  for (int r = 0; r < player_marker_rows(); r++)
    CHECK_MSG(player_marker_row_halfwidth(r) ==
                  player_marker_row_halfwidth(player_marker_rows() - 1 - r),
              "the diamond is symmetric about its centre row");

  CHECK(player_marker_row_y(200, 0) == 200 - PLAYER_MARKER_HALF);
  CHECK(player_marker_row_y(200, player_marker_rows() - 1) == 200 + PLAYER_MARKER_HALF);
  CHECK(player_marker_row_x(300, PLAYER_MARKER_HALF) == 300 - PLAYER_MARKER_HALF);
  CHECK_MSG(player_marker_row_x(300, 0) == 300, "the top point is one pixel at cx");

  // The label sits above the top point by the gap every marker label uses, so a descender never clips the diamond.
  CHECK(player_marker_label_y(200) == 200 - PLAYER_MARKER_HALF - ESP_LABEL_GAP);
  CHECK(player_marker_label_y(200) < player_marker_row_y(200, 0));

  CHECK(strcmp(PLAYER_MARKER_LABEL, "You") == 0);

  // Saturation, not wrap: project_point can return a coordinate at the int range's end.
  CHECK(player_marker_row_x(-2147483647 - 1, PLAYER_MARKER_HALF) == -2147483647 - 1);
  CHECK(player_marker_row_y(-2147483647 - 1, 0) == -2147483647 - 1);
  CHECK(player_marker_row_y(2147483647, player_marker_rows() - 1) == 2147483647);
  CHECK(player_marker_label_y(-2147483647 - 1) == -2147483647 - 1);
}

static void tb_0119_freecam_transport_sector() {
  // The out-of-bounds refusal is the point: ek_updatesector answers false for a point in no sector, and a camera there
  // is inside nothing the engine could carry.
  CHECK_MSG(freecam_transport_sector(false, -1, 306, EK_MAXSECTORS) == -1,
            "a camera whose position does not resolve is in no sector");
  // Not even with a sector left in the out slot or a good tracked sector.
  CHECK(freecam_transport_sector(false, 12, 12, EK_MAXSECTORS) == -1);

  // The stale sector really does hold a live pair, so the refusal, not an absence of anything to fire, stops the carry
  // out of bounds (reported.
  FreecamTransport t;
  t.lotag = 1;
  t.seX = 1000; t.seY = 2000; t.seZ = -5000;
  t.ownerX = 9000; t.ownerY = 7000; t.ownerZ = -12000;
  t.ownerCeilZ = -30000; t.ownerFloorZ = 4096;
  t.secCeilZ = -20000; t.secFloorZ = 0;
  t.tempData4 = 1;
  CHECK_MSG(freecam_transport_jump(t, 1100, 2050, 4096, 2048).hit,
            "the stale sector's own pair would carry the camera");
  CHECK_MSG(freecam_transport_sector(false, 7, 7, EK_MAXSECTORS) < 0,
            "and out of bounds no sector is searched, live pair or not");

  CHECK(freecam_transport_sector(true, 7, 7, EK_MAXSECTORS) == 7);
  CHECK(freecam_transport_sector(true, 0, 0, EK_MAXSECTORS) == 0);

  CHECK(freecam_transport_sector(true, 7, EK_MAXSECTORS, EK_MAXSECTORS) == -1);
  CHECK(freecam_transport_sector(true, 7, -1, EK_MAXSECTORS) == -1);
  CHECK(freecam_transport_sector(true, -5, 7, EK_MAXSECTORS) == -1);
  CHECK(freecam_transport_sector(true, 7, 9999, 4096) == -1);

  // The sticky sector is not an input to this rule, which is how the sky keeps it.
  CHECK_MSG(freecam_sector_pick(false, -1, 7, EK_MAXSECTORS).sect == 7,
            "the renderer's sector stays sticky; only the transport asks again");
}

static void tb_0120_freecam_transport_crossing() {
  // updatesector is SEEDED from the tracked sector and returns it when the point is inside (engine.cpp:13517-13520,
  // :13281-13282), so a different answer means the camera crossed out and nothing may fire: the old sector's effectors
  // would fire at the new position at the pair's offset.
  CHECK_MSG(freecam_transport_sector(true, 16, 463, EK_MAXSECTORS) == -1,
            "a 2D answer that is not the tracked sector refuses the tic");
  CHECK_MSG(freecam_transport_sector(true, 463, 463, EK_MAXSECTORS) == 463,
            "and agreement carries the tracked sector through");

  // The pair in that sector fires: 463 is the above-water half (lotag 1), the camera at its floor plane still
  // descending, carried to the paired sector's ceiling with the water's two-tic hold.
  FreecamTransport t;
  t.lotag = 1;
  t.seX = 19828; t.seY = -1628; t.seZ = -96256;
  t.ownerX = -9356; t.ownerY = 1444; t.ownerZ = 15360;
  t.ownerCeilZ = 15360; t.ownerFloorZ = 40960;
  t.secCeilZ = -120000; t.secFloorZ = -96000;
  t.tempData4 = 1;
  const FreecamJump j = freecam_transport_jump(t, 19000, -1500, -96000, 2048);
  CHECK_MSG(j.hit, "an in-bounds crossing of the water surface still fires");
  CHECK(j.z == t.ownerCeilZ);
  CHECK(j.hold == FREECAM_TRANSPORT_WATER_HOLD_TICS);

  CHECK(freecam_transport_sector(false, -1, 463, EK_MAXSECTORS) == -1);
}

// The observed health ceiling belongs to the ENEMY in the slot, not the slot.
static void tb_0121_health_latch_identity() {
  // One occupant: the high-water mark still holds and an observation below the ceiling never lowers it.
  EkmHealthLatch one = { 0, 0, 0 };
  CHECK(ekm_health_latch(&one, 1680, EKM_PAL_CAPTAIN, 60) == 60);
  CHECK_MSG(ekm_health_latch(&one, 1680, EKM_PAL_CAPTAIN, 41) == 60,
            "an observation below the ceiling does not lower it");

  // An Octabrain dies, the engine reuses its index, and the trooper that lands there starts its own ceiling instead of
  // inheriting 175.
  EkmHealthLatch reused = { 0, 0, 0 };
  CHECK(ekm_health_latch(&reused, 1820, 0, 175) == 175);
  CHECK_MSG(ekm_health_latch(&reused, 1680, 0, 30) == 30,
            "a trooper in an octabrain's old slot does not inherit 175");
  CHECK_MSG(ekm_enemy_shown_max(ekm_enemy_max_strength(1680), 30) == 30,
            "so the label reads 30/30 and not 30/175");

  // The doubled Captain must survive that test, which is why identity is not the raw picnum.
  EkmHealthLatch captain = { 0, 0, 0 };
  const int shown = ekm_display_hp(30, ekm_enemy_max_strength(1741),
                                   ekm_enemy_ruled_max(1741, EKM_PAL_CAPTAIN), false);
  CHECK_MSG(ekm_health_latch(&captain, 1741, EKM_PAL_CAPTAIN, shown) == 60,
            "a dormant captain still reaches its doubled maximum");
  CHECK_MSG(ekm_health_latch(&captain, 1680, EKM_PAL_CAPTAIN, 45) == 60,
            "and standing up is the same enemy, not a new occupant");

  // A plain trooper in a Captain's slot is not the Captain: same tile family, and the palette alone says so.
  EkmHealthLatch afterCaptain = { 0, 0, 0 };
  CHECK(ekm_health_latch(&afterCaptain, 1680, EKM_PAL_CAPTAIN, 60) == 60);
  CHECK_MSG(ekm_health_latch(&afterCaptain, 1680, 0, 30) == 30,
            "an ordinary trooper does not inherit a captain's 60");

  // A Slimer's frames are one occupant: GREENSLIME has no CON `actor` line, so the observation is its only maximum and
  // a frame change restarting it would leave the bar with none.
  EkmHealthLatch slime = { 0, 0, 0 };
  CHECK(ekm_health_latch(&slime, 2370, 0, 8) == 8);
  CHECK_MSG(ekm_health_latch(&slime, 2372, 0, 4) == 8,
            "a protozoid slimer's frames do not restart its ceiling");

  EkmHealthLatch modded = { 0, 0, 0 };
  CHECK(ekm_health_latch(&modded, 9000, 0, 500) == 500);
  CHECK_MSG(ekm_health_latch(&modded, 9001, 0, 50) == 50,
            "an unnamed tile is keyed on itself");
  CHECK_MSG(ekm_health_latch(&modded, 9001, 0, 40) == 50,
            "and latches normally once it is the occupant");

  CHECK(ekm_health_latch(0, 1680, 0, 30) == 0);
}

// The visible teleporter, which neither reference carries: the pad with the glowing column is an SE 7 ON its sector's
// floor (ONFLOORZ, game.cpp:2930) in a lotag 0 sector, the branch actors.cpp:3947 gates on pPlayer->on_ground without
// the jetpack.
static void tb_0122_freecam_transport_floor() {
  // The window is floorzoffset (premap.cpp:660) and the hold is the engine's for this branch (actors.cpp:3972).
  CHECK(FREECAM_TRANSPORT_STAND == 10240);
  CHECK(FREECAM_TRANSPORT_FLOOR_HOLD_TICS == 13);

  // A pair of pads: the effector on its sector's floor at (1000,2000,0) (ONFLOORZ means the effector IS the floor) and
  // the paired pad at (9000,7000,-4096).
  FreecamTransport t;
  t.lotag = 0;
  t.seX = 1000; t.seY = 2000; t.seZ = 0;
  t.ownerX = 9000; t.ownerY = 7000; t.ownerZ = -4096;
  t.ownerCeilZ = -30000; t.ownerFloorZ = -4096;
  t.secCeilZ = -20000; t.secFloorZ = 0;
  t.tempData4 = 1;

  // The crossing fires; a refusal alone was pinned before and a change killing the whole feature shipped green.
  FreecamJump j = freecam_transport_jump(t, 1100, 2050, 0, 2048);
  CHECK_MSG(j.hit, "a camera on the pad is carried, as the player on it is");
  CHECK_MSG(j.x == 9100 && j.y == 7050,
            "the pair's own separation, which is OURS here and not the engine's "
            "(actors.cpp:3975 assigns) -- freecam.h says why");
  CHECK_MSG(j.z == -4096 - 10240, "set down at the far pad's own window edge");
  CHECK_MSG(j.hold == 13, "and held for the engine's own 13 tics (actors.cpp:3972)");

  // The window is the position on_ground stands for: floorzoffset, 40<<8, a standing viewpoint's height above the floor
  // and the engine's line between standing and falling (premap.cpp:660, player.cpp:5409).
  CHECK(freecam_transport_jump(t, 1100, 2050, -10239, 2048).hit);
  CHECK_MSG(!freecam_transport_jump(t, 1100, 2050, -10240, 2048).hit,
            "a camera at the landing height is not carried again");

  // Below the pad's floor plane nothing fires (a player on the ground is never under it), taken still.
  CHECK(!freecam_transport_jump(t, 1100, 2050, 1, 0).hit);

  // A camera faster than the pad's window is still carried: 40960 a tic steps clean over 10240 (see freecam.h), so the
  // window is tested against the segment travelled.
  CHECK_MSG(freecam_transport_jump(t, 1100, 2050, 20480, 40960).hit,
            "a camera that steps over the pad's window in one tic is carried");
  CHECK_MSG(!freecam_transport_jump(t, 1100, 2050, 20480, 0).hit,
            "and a still camera that far below is not");

  // No velocity term, matching the engine: actors.cpp:3947 tests a position only.
  CHECK(freecam_transport_jump(t, 1100, 2050, 0, 0).hit);
  CHECK(freecam_transport_jump(t, 1100, 2050, 0, -2048).hit);

  // Only lotag 0 (actors.cpp:3947), and -1 (field unreadable) is a refusal, never a teleporter by default.
  t.lotag = -1;
  CHECK(!freecam_transport_jump(t, 1100, 2050, 0, 2048).hit);
  t.lotag = 3;
  CHECK(!freecam_transport_jump(t, 1100, 2050, 0, 2048).hit);
  t.lotag = 0;

  FreecamTransport back = t;
  back.seX = 9000; back.seY = 7000; back.seZ = -4096;
  back.ownerX = 1000; back.ownerY = 2000; back.ownerZ = 0;
  back.ownerFloorZ = 0; back.secFloorZ = -4096;
  const FreecamJump a = freecam_transport_jump(t, 1100, 2050, 0, 2048);
  CHECK(a.hit && a.z == -4096 - 10240);
  CHECK_MSG(!freecam_transport_jump(back, a.x, a.y, a.z, 2048).hit,
            "the landing sits exactly on the far pad's strict edge");
  const FreecamJump b = freecam_transport_jump(back, a.x, a.y, -4096, 2048);
  CHECK_MSG(b.hit, "the far pad carries it back");
  CHECK_MSG(b.x == 1100 && b.y == 2050, "to the point it stood at");
  CHECK(b.z == 0 - 10240);
  CHECK_MSG(!freecam_transport_jump(t, b.x, b.y, b.z, 2048).hit,
            "and that landing is clear of the near pad in its turn");

  FreecamHold step = { b.hold, 0 };
  for (int i = 0; i < 13; ++i) { step = freecam_transport_hold_next(step.tics); }
  CHECK_MSG(step.tics == 0, "a hold on a pad still counts out, or the way back never fires");
  CHECK_MSG(freecam_transport_hold_topup(step.expired, 0) == 0,
            "and off the pad nothing tops it back up");
}

// A camera that has landed and stopped is not carried again, and the landing's XY is half of why.
static void tb_0123_freecam_transport_landed() {
  FreecamTransport t;
  t.lotag = 0;
  t.seX = 1000; t.seY = 2000; t.seZ = 0;
  t.ownerX = 9000; t.ownerY = 7000; t.ownerZ = -4096;
  t.ownerCeilZ = -30000; t.ownerFloorZ = -4096;
  t.secCeilZ = -20000; t.secFloorZ = 0;
  t.tempData4 = 1;
  FreecamTransport back = t;
  back.seX = 9000; back.seY = 7000; back.seZ = -4096;
  back.ownerX = 1000; back.ownerY = 2000; back.ownerZ = 0;
  back.ownerFloorZ = 0; back.secFloorZ = -4096;

  // A camera that crossed the near pad 300 units off its effector keeps those 300 units, so it is not set down in the
  // middle of the far pad's footprint.
  const FreecamJump a = freecam_transport_jump(t, 1300, 2300, 0, 2048);
  CHECK(a.hit);
  CHECK_MSG(a.x == 9300 && a.y == 7300,
            "the landing keeps the camera's own offset from the pad");

  CHECK_MSG(!freecam_transport_jump(back, a.x, a.y, a.z, 0).hit,
            "a camera that has just landed and stopped is not carried again");

  // The edge covers that point alone: one z-unit of drift during the 13 tics puts the camera inside the far window and
  // stopping does not take it back out (the residual, seen from the standing side).
  CHECK_MSG(freecam_transport_jump(back, a.x, a.y, a.z + 1, 0).hit,
            "one unit below the edge and a STILL camera is carried");
  CHECK_MSG(freecam_transport_jump(back, a.x, a.y, a.z + 1, 1).hit,
            "as it is while that drift is still going");
}

// The hold is what keeps it there, which the z edge never could.
static void tb_0124_freecam_transport_held_on_pad() {
  CHECK(FREECAM_TRANSPORT_WATER_HOLD_TICS == -2);
  CHECK(FREECAM_TRANSPORT_WARP_HOLD_TICS == 2);

  FreecamTransport t;
  t.lotag = 0;
  t.seX = 1000; t.seY = 2000; t.seZ = 0;
  t.ownerX = 9000; t.ownerY = 7000; t.ownerZ = -4096;
  t.ownerCeilZ = -30000; t.ownerFloorZ = -4096;
  t.secCeilZ = -20000; t.secFloorZ = 0;
  t.tempData4 = 1;
  FreecamTransport back = t;
  back.seX = 9000; back.seY = 7000; back.seZ = -4096;
  back.ownerX = 1000; back.ownerY = 2000; back.ownerZ = 0;
  back.ownerFloorZ = 0; back.secFloorZ = -4096;

  // Dead centre in, dead centre out: the landing the player asked for by name, which repros most consistently.
  const FreecamJump a = freecam_transport_jump(t, 1000, 2000, 0, 2048);
  CHECK(a.hit);
  CHECK_MSG(a.x == 9000 && a.y == 7000, "dead centre of the near pad lands dead centre on the far one");
  CHECK(a.hold == FREECAM_TRANSPORT_FLOOR_HOLD_TICS);
  CHECK_MSG(freecam_transport_jump(back, a.x, a.y, a.z + 1, 0).hit,
            "and the far pad would take it back the moment it drifts, so the z edge is not what stops this");

  // So the hold has to: thirteen tics, then re-armed while the camera is in a sector holding a paired effector
  // (camera.cpp's onWarping, actors.cpp:3943's own question).
  int hold = a.hold, carried = 0;
  for (int tic = 0; tic < 64; ++tic) {
    const FreecamHold h = freecam_transport_hold_next(hold);
    hold = h.tics;
    if (hold != 0) continue;                        // camera.cpp returns here
    hold = freecam_transport_hold_topup(h.expired, 1);
    if (hold == 0) { carried = 1; break; }          // a transport could fire
  }
  CHECK_MSG(!carried, "a camera left standing on the far pad is never carried again");
  CHECK(hold > 0);

  // Leaving releases it: a hold that never expires once the camera has left is the water-pair bug in another form.
  int released = 0;
  for (int tic = 0; tic < 64; ++tic) {
    const FreecamHold h = freecam_transport_hold_next(hold);
    hold = h.tics;
    if (hold != 0) continue;
    hold = freecam_transport_hold_topup(h.expired, 0);
    if (hold == 0) { released = 1; break; }
  }
  CHECK_MSG(released, "off the pad the hold expires, or the way back never fires");
  CHECK_MSG(freecam_transport_jump(back, a.x, a.y, a.z + 1, 0).hit,
            "and flying back onto the pad deliberately is carried as before");

  FreecamHold w = freecam_transport_hold_next(FREECAM_TRANSPORT_WATER_HOLD_TICS);
  CHECK(w.tics == -1 && w.expired == 0);
  CHECK(freecam_transport_hold_topup(w.expired, 1) == 0);
  w = freecam_transport_hold_next(w.tics);
  CHECK_MSG(w.tics == 0 && w.expired == 0,
            "a negative hold reaches zero without ever expiring");
  CHECK(freecam_transport_hold_topup(w.expired, 1) == 0);

  // Standing on a pad arms nothing by itself: the top-up is reachable only from a hold the camera was given
  // (player.cpp:5229).
  const FreecamHold idle = freecam_transport_hold_next(0);
  CHECK(idle.tics == 0 && idle.expired == 0);
  CHECK(freecam_transport_hold_topup(idle.expired, 1) == 0);
}

static void tb_0125_item_table() {
  // A sample across the families, not the whole table: a test that restates the table proves only that copy-paste works
  // twice.
  CHECK(!strcmp(ekm_item_name(21), "Pistol"));
  CHECK(!strcmp(ekm_item_name(47), "Pipebombs"));
  CHECK(!strcmp(ekm_item_name(52), "Large medkit"));
  CHECK(!strcmp(ekm_item_name(100), "Atomic health"));
  CHECK(!strcmp(ekm_item_name(1348), "Holoduke"));

  // A single thrown pipebomb is not the box: 26 is one bomb on the floor, 47 the pickup that gives several.
  CHECK(!strcmp(ekm_item_name(EKM_PIC_HEAVYHBOMB), "Pipebomb"));

  CHECK(ekm_item_name(0) == 0);
  CHECK(ekm_item_name(1680) == 0);     // Assault Trooper: an enemy, not an item
  CHECK(ekm_item_name(43) == 0);       // a gap inside the pickup range
  CHECK(ekm_item_name(5134) == 0);     // World Tour's flamethrower: not this game

  // Keycards are named by palette, from gameexec.cpp:1035-1037.
  CHECK(!strcmp(ekm_item_name_pal(EKM_PIC_ACCESSCARD, 0),  "Blue keycard"));
  CHECK(!strcmp(ekm_item_name_pal(EKM_PIC_ACCESSCARD, 21), "Red keycard"));
  CHECK(!strcmp(ekm_item_name_pal(EKM_PIC_ACCESSCARD, 23), "Yellow keycard"));
  // A pal the game has no lock colour for falls back to the plain name rather than inventing a fourth card.
  CHECK(!strcmp(ekm_item_name_pal(EKM_PIC_ACCESSCARD, 7), "Keycard"));
  // The pal argument must not leak onto anything else.
  CHECK(!strcmp(ekm_item_name_pal(52, 21), "Large medkit"));

  int bothPic = -1;
  for (int pic = 0; pic < 2800 && bothPic < 0; pic++)
    if (ekm_item_name(pic) && ekm_enemy_name(pic)) bothPic = pic;
  char bothMsg[64];
  sprintf_s(bothMsg, sizeof(bothMsg),
            "picnum %d is both an enemy and an item", bothPic);
  CHECK_MSG(bothPic < 0, bothMsg);

  // Respawn hides the pickup by cstat bit 15, not by deleting it.
  CHECK(ekm_item_present(0x0000, 32));
  CHECK(ekm_item_present(0x0101, 32));
  CHECK(!ekm_item_present(0x8000, 32));
  CHECK(!ekm_item_present((int)(short)0x8000, 32));   // the sign-extended form
  CHECK(!ekm_item_present(0x0000, 0));                // zero-sized

  // A keycard's colour must agree with its name: they are looked up by separate functions off the same pal, so a card
  // could name itself red and draw blue.
  CHECK(ekm_keycard_colour(EKM_PAL_KEY_BLUE)   == C_BLUE);
  CHECK(ekm_keycard_colour(EKM_PAL_KEY_RED)    == C_RED);
  CHECK(ekm_keycard_colour(EKM_PAL_KEY_YELLOW) == C_HAZARD);
  CHECK(ekm_keycard_colour(7) == 0);
  CHECK(C_BLUE != 0 && C_RED != 0 && C_HAZARD != 0);
  CHECK(C_BLUE != C_RED && C_RED != C_HAZARD && C_BLUE != C_HAZARD);

  // The pipebomb's own rule, actors.cpp:5435: settled for more than seven tics AND no longer moving. playerDist is not
  // here.
  CHECK(ekm_pipebomb_takeable(8, 0));
  CHECK(!ekm_pipebomb_takeable(7, 0));    // strict >, pinned at the boundary
  CHECK(!ekm_pipebomb_takeable(8, 1));    // still moving
  CHECK(!ekm_pipebomb_takeable(8, -1));   // moving backwards is still moving
  CHECK(!ekm_pipebomb_takeable(0, 0));
}

// The marker pipeline's two ordering rules: selection and ordering are separate questions, and the reference got that
// wrong once.
static void tb_0126_marker_pipeline() {
  EkMarker m[8];
  for (int i = 0; i < 8; i++) {
    m[i].kind = EK_MARKER_ENEMY; m[i].index = i; m[i].seq = i;
    m[i].p.x = 0; m[i].p.y = 0;
  }
  const int d[8] = { 500, 100, 300, 900, 300, 50, 700, 200 };
  for (int i = 0; i < 8; i++) { m[i].dist2d = d[i]; m[i].p.dist2d = d[i]; }

  EkMarker k[8];
  memcpy(k, m, sizeof(m));
  ekm_keep_nearest(k, 8, 3);
  CHECK(k[0].dist2d == 50);
  CHECK(k[1].dist2d == 100);
  CHECK(k[2].dist2d == 200);

  // A no-op when there is nothing to drop, and it must not reorder.
  EkMarker n[8];
  memcpy(n, m, sizeof(m));
  ekm_keep_nearest(n, 8, 8);
  for (int i = 0; i < 8; i++) CHECK(n[i].dist2d == d[i]);
  memcpy(n, m, sizeof(m));
  ekm_keep_nearest(n, 8, 0);
  for (int i = 0; i < 8; i++) CHECK(n[i].dist2d == d[i]);
  ekm_keep_nearest(0, 8, 3);    // null is survivable, not a crash

  // Far to near is the painter's order: the farthest is emitted first so a near marker draws over a far one.
  EkMarker s[8];
  memcpy(s, m, sizeof(m));
  ekm_sort_far_to_near(s, 8);
  for (int i = 1; i < 8; i++)
    CHECK_MSG(s[i - 1].dist2d >= s[i].dist2d, "far to near is descending");
  CHECK(s[0].dist2d == 900);
  CHECK(s[7].dist2d == 50);

  // Strict `<` in the shift, so equal elements keep collection order: the two markers at 300 are seq 2 and seq 4, and 2
  // stays first.
  int firstSeq = -1, secondSeq = -1;
  for (int i = 0; i < 8; i++)
    if (s[i].dist2d == 300) { if (firstSeq < 0) firstSeq = s[i].seq; else secondSeq = s[i].seq; }
  CHECK_MSG(firstSeq == 2 && secondSeq == 4, "equal distances keep collection order");

  ekm_sort_far_to_near(s, 1);   // fewer than two is a no-op, not a read past the end
  ekm_sort_far_to_near(0, 8);
}

// The map extent's rules serve a WALK, not a search: the chain tiles from sector 0 and the index it stops at is
// numsectors.
static void tb_0127_map_extent_rules() {
  // The floor on n is sectorstate's own EK_SECTOR_MIN_CHAIN and the two must not drift.
  CHECK(!ekm_extent_counts_plausible(1, 3));
  CHECK(ekm_extent_counts_plausible(1024, 8192));
  CHECK(ekm_extent_counts_plausible(EK_MAXSECTORS, EK_MAXWALLS));
  // The floor pinned on both sides so it cannot drift from sectorstate's constant.
  CHECK(ekm_extent_counts_plausible(EK_SECTOR_MIN_CHAIN, 3));
  CHECK(!ekm_extent_counts_plausible(EK_SECTOR_MIN_CHAIN - 1, 3));
  CHECK(!ekm_extent_counts_plausible(0, 3));                  // no sectors
  CHECK(!ekm_extent_counts_plausible(-1, 3));
  CHECK(!ekm_extent_counts_plausible(EK_MAXSECTORS + 1, 3));
  CHECK(!ekm_extent_counts_plausible(EK_SECTOR_MIN_CHAIN, 2));   // under a triangle
  CHECK(!ekm_extent_counts_plausible(EK_SECTOR_MIN_CHAIN, EK_MAXWALLS + 1));

  // Sector 0 must start at wall 0 and each sector's walls begin where the last ended.
  EkExtentStep s = ekm_extent_step(0, 1, 0, 4, 0, 0, 8);
  CHECK(s.stop == 0);
  s = ekm_extent_step(1, 1, 4, 6, 0, 4, 8);
  CHECK(s.stop == 0);

  // A gap is a stop, and so is an overlap: either means this is not the chain.
  CHECK(ekm_extent_step(1, 1, 5, 6, 0, 4, 8).stop == 1);
  CHECK(ekm_extent_step(1, 1, 3, 6, 0, 4, 8).stop == 1);
  CHECK(ekm_extent_step(0, 1, 1, 4, 0, 0, 8).stop == 1);
  // An unreadable sector stops rather than being skipped: a gap cannot be told from the map ending.
  CHECK(ekm_extent_step(1, 0, 4, 6, 0, 4, 8).stop == 1);
  CHECK(ekm_extent_step(-1, 1, 0, 4, 0, 0, 8).stop == 1);
  CHECK(ekm_extent_step(8, 1, 0, 4, 0, 0, 8).stop == 1);
  CHECK_MSG(ekm_extent_step(0, 1, 0, 2, 0, 0, 8).stop == 0,
            "a two-wall sector does not end the map");
  CHECK_MSG(ekm_extent_step(1, 1, 2, 4, 0, 2, 8).stop == 0,
            "and the chain continues from its own span, not a rounded one");
  CHECK_MSG(ekm_extent_step(1, 1, 9, 4, 0, 2, 8).stop == 1,
            "while a record that does not continue the tiling still ends it");
  CHECK(ekm_extent_step(0, 1, 0, EK_MAXWALLS + 1, 0, 0, 8).stop == 1);

  CHECK(!ekm_extent_chain_stops(20, 1, 120, 6, 114, 6));
  // The true end on a freshly started game: the region past the map is zeroed, so the record is not plausible.
  CHECK(ekm_extent_chain_stops(20, 1, 0, 0, 114, 6));
  // The true end after a LARGER map: the stale record is plausible but its wallptr is the OLD map's prefix sum, not
  // this map's.
  CHECK(ekm_extent_chain_stops(20, 1, 340, 8, 114, 6));
  // An unreadable sector n ends the map: a hole cannot be told from the chain ending.
  CHECK(ekm_extent_chain_stops(20, 0, 120, 6, 114, 6));
  CHECK(ekm_extent_chain_stops(EK_MAXSECTORS, 1, 120, 6, 114, 6));
  // No special case for the floor: n is at least EK_SECTOR_MIN_CHAIN, so sector n-1 exists and the record's content
  // decides.
  CHECK(!ekm_extent_chain_stops(EK_SECTOR_MIN_CHAIN, 1, 96, 6, 90, 6));
  CHECK(ekm_extent_chain_stops(EK_SECTOR_MIN_CHAIN, 1, 0, 0, 90, 6));
  int continued = 0;
  for (int i = EK_SECTOR_MIN_CHAIN; i < EK_SECTOR_MIN_CHAIN + 16; i++) {
    if (!ekm_extent_chain_stops(i, 1, i * 6, 6, (i - 1) * 6, 6)) continued++;
  }
  CHECK(continued == 16);
}
// The secret marker's two pure halves: where a sector's centre is and where the cross and labels sit once it is on
// screen.
static void tb_0128_secret_marker_rules() {
  // point2 must name a wall inside this sector's own span and never itself.
  CHECK(ekm_point2_ok(5, 4, 4, 4));
  CHECK(ekm_point2_ok(4, 7, 4, 4));      // wrapping back to the first is normal
  CHECK(!ekm_point2_ok(4, 4, 4, 4));     // pointing at itself
  CHECK(!ekm_point2_ok(3, 4, 4, 4));     // below the span
  CHECK(!ekm_point2_ok(8, 4, 4, 4));     // at the end, exclusive
  CHECK(!ekm_point2_ok(-1, 4, 4, 4));

  // The centre is the average of the vertices: a unit square at the origin centres on (5, 5) with these corners.
  EkSectorCentre c = { 0, 0, 0 };
  ekm_sector_centre_accumulate(&c, 0, 0);
  ekm_sector_centre_accumulate(&c, 10, 0);
  ekm_sector_centre_accumulate(&c, 10, 10);
  ekm_sector_centre_accumulate(&c, 0, 10);
  CHECK(ekm_sector_centre_finish(&c, 4));
  CHECK(c.ok == 1 && c.x == 5 && c.y == 5);

  // A sector with no accepted vertices has no centre and must say so rather than report the origin.
  EkSectorCentre e = { 0, 0, 0 };
  CHECK(!ekm_sector_centre_finish(&e, 0));
  CHECK(e.ok == 0);

  EkSectorCentre f = { 0, 0, 0 };
  for (int i = 0; i < 4; i++) ekm_sector_centre_accumulate(&f, 2000000000, -2000000000);
  CHECK(ekm_sector_centre_finish(&f, 4));
  CHECK(f.x == 2000000000 && f.y == -2000000000);

  CHECK(esp_secret_bar_span() == ESP_SECRET_ARM * 2 + 1);
  CHECK(esp_secret_hbar_x(100) == 100 - ESP_SECRET_ARM);
  CHECK(esp_secret_vbar_y(100) == 100 - ESP_SECRET_ARM);
  CHECK(esp_secret_hbar_y(100) == 100 - ESP_SECRET_T / 2);
  CHECK(esp_secret_vbar_x(100) == 100 - ESP_SECRET_T / 2);

  CHECK(esp_secret_name_label_y(100) == 100 - ESP_SECRET_ARM - ESP_LABEL_GAP);
  CHECK(esp_secret_dist_label_y(100) == 100 + ESP_SECRET_ARM + 1 + ESP_DIST_GAP);

  CHECK(EK_MARKER_SECRET != EK_MARKER_ENEMY && EK_MARKER_SECRET != EK_MARKER_ITEM);

  // The secret lotag, exactly. 0 is the load-bearing half: the engine zeroes a found secret's lotag (hence no
  // found-set), so answering yes to 0 would keep drawing taken secrets.
  CHECK(EKM_SECRET_LOTAG == 32767);
  CHECK(ekm_sector_is_secret(32767));
  CHECK(!ekm_sector_is_secret(0));
  CHECK(!ekm_sector_is_secret(-1));
}
static void tb_0129_map_extent_crosscheck() {
  // Window size: a probe on E1L1 measured numwalls at anchor+8 and numsectors at anchor+84, so 512 past the anchor
  // holds the furthest six times over and 256 before covers a build laying the pair out the other side.
  CHECK(ekm_extent_window_holds(8));
  CHECK(ekm_extent_window_holds(84));
  CHECK(ekm_extent_window_holds(84 * 2));            // room, not a fit
  CHECK(ekm_extent_window_holds(0));
  CHECK(ekm_extent_window_holds(-8));
  CHECK(ekm_extent_window_holds(-EK_EXTENT_WINDOW_BACK));
  CHECK(!ekm_extent_window_holds(-EK_EXTENT_WINDOW_BACK - 1));
  CHECK(ekm_extent_window_holds(EK_EXTENT_WINDOW_FWD - 2));
  CHECK(!ekm_extent_window_holds(EK_EXTENT_WINDOW_FWD));

  // Searchable values: the counts are int16_t, so a pair that cannot be STORED in one cannot be FOUND in one, and equal
  // halves would let one position answer for both globals and record the same address twice.
  CHECK(ekm_extent_values_searchable(325, 1975));     // the measured map
  CHECK(ekm_extent_values_searchable(EK_MAXSECTORS, EK_MAXWALLS));
  CHECK(!ekm_extent_values_searchable(325, 325));
  CHECK(!ekm_extent_values_searchable(0, 1975));
  CHECK(!ekm_extent_values_searchable(325, 0));
  CHECK(!ekm_extent_values_searchable(-1, 1975));
  CHECK(!ekm_extent_values_searchable(40000, 1975));
  CHECK(!ekm_extent_values_searchable(325, 40000));

  // Uniqueness makes it a locate: exactly one position in the window holds each value, or the window found an index
  // table counting through it, as the abandoned whole-.bss search kept finding (323, 324, 325, 326, 327).
  CHECK(ekm_extent_locate_unique(1, 1));
  CHECK(!ekm_extent_locate_unique(0, 1));
  CHECK(!ekm_extent_locate_unique(1, 0));
  CHECK(!ekm_extent_locate_unique(2, 1));
  CHECK(!ekm_extent_locate_unique(1, 2));
  CHECK(!ekm_extent_locate_unique(0, 0));

  // Both halves must agree, not either.
  CHECK(ekm_extent_agrees(325, 1975, 325, 1975));
  CHECK(!ekm_extent_agrees(325, 1975, 324, 1975));
  CHECK(!ekm_extent_agrees(325, 1975, 325, 1974));
  // The over-run, the case this unit exists for: the walk ran into the previous map and returned that map's pair WHOLE.
  CHECK(!ekm_extent_agrees(412, 2600, 325, 1975));

  // Located and agreeing publishes; located and disagreeing REFUSES with zero, not a winner picked between the two.
  CHECK(ekm_extent_publish_verdict(EK_EXTENT_HARDEN_LOCATED, 1, 1, 1) ==
        EK_EXTENT_PUBLISH_WALK);
  CHECK(ekm_extent_publish_verdict(EK_EXTENT_HARDEN_LOCATED, 1, 1, 0) ==
        EK_EXTENT_PUBLISH_REFUSE);
  // A window that named nothing does not cost the feature: the walk stands alone and the documented residual applies.
  CHECK(ekm_extent_publish_verdict(EK_EXTENT_HARDEN_NONE, 1, 0, 0) ==
        EK_EXTENT_PUBLISH_WALK);
  CHECK(ekm_extent_publish_verdict(EK_EXTENT_HARDEN_PENDING, 1, 0, 0) ==
        EK_EXTENT_PUBLISH_WALK);
  // An unhardened state never consults the cross-check, even with numbers that would disagree.
  CHECK(ekm_extent_publish_verdict(EK_EXTENT_HARDEN_NONE, 1, 1, 0) ==
        EK_EXTENT_PUBLISH_WALK);
  CHECK(ekm_extent_publish_verdict(EK_EXTENT_HARDEN_LOCATED, 0, 1, 1) ==
        EK_EXTENT_PUBLISH_NOTHING);
  CHECK(ekm_extent_publish_verdict(EK_EXTENT_HARDEN_LOCATED, 0, 1, 0) ==
        EK_EXTENT_PUBLISH_NOTHING);
  CHECK(ekm_extent_publish_verdict(EK_EXTENT_HARDEN_NONE, 0, 0, 0) ==
        EK_EXTENT_PUBLISH_NOTHING);
  CHECK(ekm_extent_publish_verdict(EK_EXTENT_HARDEN_LOCATED, 1, 0, 0) ==
        EK_EXTENT_PUBLISH_RELOCATE);
  CHECK(ekm_extent_publish_verdict(EK_EXTENT_HARDEN_LOCATED, 1, 0, 1) ==
        EK_EXTENT_PUBLISH_RELOCATE);

  // The re-locate is bounded, so a build whose window keeps naming the wrong bytes settles unhardened instead of
  // searching every second for the life of the process.
  CHECK(ekm_extent_may_relocate(0));
  CHECK(ekm_extent_may_relocate(EK_EXTENT_RELOCATE_MAX - 1));
  CHECK(!ekm_extent_may_relocate(EK_EXTENT_RELOCATE_MAX));
  CHECK(!ekm_extent_may_relocate(EK_EXTENT_RELOCATE_MAX + 1));

  // The four verdicts are four distinct values; collapsing a pair would make the refusal indistinguishable from a
  // publish.
  CHECK(EK_EXTENT_PUBLISH_NOTHING != EK_EXTENT_PUBLISH_WALK &&
        EK_EXTENT_PUBLISH_NOTHING != EK_EXTENT_PUBLISH_REFUSE &&
        EK_EXTENT_PUBLISH_NOTHING != EK_EXTENT_PUBLISH_RELOCATE &&
        EK_EXTENT_PUBLISH_WALK != EK_EXTENT_PUBLISH_REFUSE &&
        EK_EXTENT_PUBLISH_WALK != EK_EXTENT_PUBLISH_RELOCATE &&
        EK_EXTENT_PUBLISH_REFUSE != EK_EXTENT_PUBLISH_RELOCATE);
}
// The confirmation counter (mapextent.h): the loud EXTENT REFUSED line waits for a second consecutive disagreeing scan,
// so a level load's mid-write window never prints it.
static void tb_0130_map_extent_confirm() {
  // The first disagreement is silent: the streak reaches 1, not the confirming 2.
  int streak = ekm_extent_disagree_next(0, 1);
  CHECK(streak == 1);
  CHECK(!ekm_extent_disagree_confirmed(streak));

  streak = ekm_extent_disagree_next(streak, 1);
  CHECK(streak == 2);
  CHECK(ekm_extent_disagree_confirmed(streak));

  // An agreement between them resets: two disagreements either side of an agreeing scan are two transients.
  int resetStreak = ekm_extent_disagree_next(1, 0);
  CHECK(resetStreak == 0);
  resetStreak = ekm_extent_disagree_next(resetStreak, 1);
  CHECK(resetStreak == 1);
  CHECK(!ekm_extent_disagree_confirmed(resetStreak));

  // No repeat speak for one continuous disagreement: the streak climbs past 2 and confirmed is false after the one time
  // it was true, so a stable over-run logs once.
  int longStreak = 2;
  for (int i = 0; i < 5; i++) {
    longStreak = ekm_extent_disagree_next(longStreak, 1);
    CHECK(longStreak == 3 + i);
    CHECK(!ekm_extent_disagree_confirmed(longStreak));
  }

  CHECK(ekm_extent_disagree_next(ekm_extent_disagree_next(5, 0), 1) == 1);
}

// menu.cpp's call site passed 0 for aimon/silenton/tboton/freecamon under a comment saying those features did not exist
// yet, so the panel under-reported live cheats.
static void tb_0131_menu_live_count() {
  const int base = menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  CHECK_MSG(base == 0, "every parent switch off reports zero live");

  CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0) == base + 1); // aimon
  CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0) == base + 1); // silenton
  CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0) == base + 1); // freecamon
  CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1) == base + 1); // tboton

  // All four together add four, not one: no branch collapses two switches into one bit.
  CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1) == base + 4);
}

// Crack sprites and breakable wall panels, derived from THIS engine.
static void tb_0132_breakable_rules() {
  CHECK(!strcmp(ekm_crack_name(546), "Explodable"));
  CHECK(!strcmp(ekm_crack_name(549), "Explodable"));
  CHECK(ekm_crack_name(545) == 0);
  CHECK(ekm_crack_name(550) == 0);

  // A hitag of zero is inert decoration: actors.cpp:2362 takes the detonate branch only when the hitag is set, so a
  // hitag-0 crack never blows and must not be marked.
  CHECK(!ekm_crack_live(0));
  CHECK(ekm_crack_live(1));
  CHECK(ekm_crack_live(32767));
  CHECK(!ekm_crack_live(-1));

  CHECK(!strcmp(ekm_breakable_name(293), "Breakable"));    // W_TECHWALL1
  CHECK(!strcmp(ekm_breakable_name(1212), "Breakable"));   // VENDMACHINE
  CHECK(!strcmp(ekm_breakable_name(4181), "Breakable"));   // W_MILKSHELF
  CHECK(ekm_breakable_name(0) == 0);
  // The broken counterparts must be ABSENT, which is the liveness rule.
  CHECK(ekm_breakable_name(360) == 0);    // W_HITTECHWALL1
  CHECK(ekm_breakable_name(1214) == 0);   // VENDMACHINEBROKE
  CHECK(ekm_breakable_name(1217) == 0);   // COLAMACHINEBROKE
  CHECK(ekm_breakable_name(4203) == 0);   // W_MILKSHELFBROKE
  // Glass, screens and barrels are the SPRITE damage path, not wall panels.
  CHECK(ekm_breakable_name(503) == 0);    // GLASS
  CHECK(ekm_breakable_name(1238) == 0);   // EXPLODINGBARREL

  int bothPic = -1;
  for (int pic = 0; pic < 4300 && bothPic < 0; pic++) {
    int n = 0;
    if (ekm_enemy_name(pic))     n++;
    if (ekm_item_name(pic))      n++;
    if (ekm_crack_name(pic))     n++;
    if (ekm_breakable_name(pic)) n++;
    if (n > 1) bothPic = pic;
  }
  char bothMsg[80];
  sprintf_s(bothMsg, sizeof(bothMsg),
            "picnum %d is in more than one ESP table", bothPic);
  CHECK_MSG(bothPic < 0, bothMsg);

  CHECK(EK_MARKER_CRACK != EK_MARKER_BREAKABLE);
  CHECK(EK_MARKER_CRACK != EK_MARKER_ENEMY && EK_MARKER_CRACK != EK_MARKER_ITEM &&
        EK_MARKER_CRACK != EK_MARKER_SECRET);
  CHECK(EK_MARKER_BREAKABLE != EK_MARKER_ENEMY && EK_MARKER_BREAKABLE != EK_MARKER_ITEM &&
        EK_MARKER_BREAKABLE != EK_MARKER_SECRET);

  // The wall mark's midpoint: two Build coordinates sum past int, so the average widens to 64 bits first.
  CHECK(ekm_wall_mid(0, 10) == 5);
  CHECK(ekm_wall_mid(-10, 10) == 0);
  CHECK(ekm_wall_mid(2000000000, 2000000000) == 2000000000);
  CHECK(ekm_wall_mid(-2000000000, -2000000000) == -2000000000);
  CHECK(ekm_wall_mid_z(1000, 0) == 500);
  CHECK(ekm_wall_mid_z(-2000000000, 2000000000) == 0);

  // The cache's cap, and the draw loop's clamp on a count it did not compute.
  CHECK(ekm_wall_mark_count_clamped(-1) == 0);
  CHECK(ekm_wall_mark_count_clamped(7) == 7);
  CHECK(ekm_wall_mark_count_clamped(EK_WALL_MARK_MAX + 1) == EK_WALL_MARK_MAX);

  CHECK(ekm_wall_scan_due(0, 100, 100, 5000, 5000));      // never scanned
  CHECK(ekm_wall_scan_due(1, 200, 100, 5000, 5000));      // signature changed
  CHECK(!ekm_wall_scan_due(1, 100, 100, 5000, 5000));     // same tick
  CHECK(!ekm_wall_scan_due(1, 100, 100, 5999, 5000));     // inside the period
  CHECK(ekm_wall_scan_due(1, 100, 100, 6000, 5000));      // period elapsed
  CHECK(!ekm_wall_scan_due(1, 100, 100, 10u, 5u));
}

static void tb_0133_switch_classify() {
  static const int kBases[] = { 130, 132, 134, 136, 138, 140, 162, 164, 166,
                                168, 170, 712, 860, 862, 864, 1111, 1122, 1142 };
  for (int i = 0; i < 18; i++) {
    CHECK(ekm_switch_base_for(kBases[i])     == kBases[i]);
    CHECK(ekm_switch_base_for(kBases[i] + 1) == kBases[i]);
  }

  // MULTISWITCH is the one FOUR-frame family: G_GetBaseSwitch maps 146<p<=149 back to 146, which is already the base.
  for (int p = 146; p <= 149; p++) CHECK(ekm_switch_base_for(p) == 146);
  CHECK(ekm_switch_base_for(150) == 150);   // a door tile, its own base
  CHECK(ekm_switch_base_for(145) == -1);    // neither

  CHECK(ekm_isadoorwall(150) && ekm_isadoorwall(159));
  CHECK(ekm_isadoorwall(395) && ekm_isadoorwall(4391));
  CHECK(ekm_isadoorwall(1178) && ekm_isadoorwall(1179));
  CHECK(!ekm_isadoorwall(149) && !ekm_isadoorwall(160));

  // A lotag of zero is not a switch in any family: P_ActivateSwitch returns 0 for it (sector.cpp:1284, :1294), so a
  // marker would mark nothing.
  CHECK(ekm_switch_classify(134, 0, 0, 0, 0).family == EK_SW_NONE);   // plain
  CHECK(ekm_switch_classify(130, 0, 0, 0, 0).family == EK_SW_NONE);   // access
  CHECK(ekm_switch_classify(150, 0, 0, 0, 0).family == EK_SW_NONE);   // door
  CHECK(ekm_switch_classify(146, 0, 0, 0, 0).family == EK_SW_NONE);   // multi

  CHECK(ekm_switch_classify(134, 0, 0, 1, 0).family == EK_SW_TWOSTATE);
  CHECK(ekm_switch_classify(146, 0, 0, 1, 0).family == EK_SW_MULTI);
  CHECK(ekm_switch_classify(162, 0, 0, 1, 0).family == EK_SW_COMBINATION);
  CHECK(ekm_switch_classify(166, 0, 0, 1, 0).family == EK_SW_COMBINATION);
  CHECK(ekm_switch_classify(1142, 0, 0, 1, 0).family == EK_SW_COMBINATION);
  CHECK(ekm_switch_classify(130, 0, 0, 1, 0).family == EK_SW_ACCESS);
  CHECK(ekm_switch_classify(170, 0, 0, 1, 0).family == EK_SW_ACCESS);
  // DIPSWITCH2 (164) and DIPSWITCH3 (168) are REST_SWITCH_CASES despite their names.
  CHECK(ekm_switch_classify(164, 0, 0, 1, 0).family == EK_SW_TWOSTATE);
  CHECK(ekm_switch_classify(168, 0, 0, 1, 0).family == EK_SW_TWOSTATE);

  // A door tile must not fall into the two-state default: it is tested before it.
  CHECK(ekm_switch_classify(150, 0, 0, 1, 0).family == EK_SW_DOOR);
  CHECK(ekm_switch_classify(4391, 0, 0, 1, 0).family == EK_SW_DOOR);

  // Access: the hint names the card only while the player lacks its bit.
  CHECK(!strcmp(ekm_switch_classify(130, 0,  0, 1, 0).line, "Needs blue card"));
  CHECK(!strcmp(ekm_switch_classify(130, 21, 0, 1, 0).line, "Needs red card"));
  CHECK(!strcmp(ekm_switch_classify(130, 23, 0, 1, 0).line, "Needs yellow card"));
  CHECK(ekm_switch_classify(130, 0,  0, 1, 1).line == 0);   // holding blue
  CHECK(ekm_switch_classify(130, 21, 0, 1, 2).line == 0);   // holding red
  CHECK(ekm_switch_classify(130, 23, 0, 1, 4).line == 0);   // holding yellow
  // Another card is not this card.
  CHECK(!strcmp(ekm_switch_classify(130, 21, 0, 1, 1).line, "Needs red card"));
  CHECK(ekm_switch_classify(131, 21, 0, 1, 0).family == EK_SW_ACCESS);
  CHECK(ekm_switch_classify(131, 21, 0, 1, 0).line == 0);

  CHECK(!strcmp(ekm_switch_classify(162, 0, 0, 1, 0).line, "Leave"));  // want 0, is 0
  CHECK(!strcmp(ekm_switch_classify(163, 0, 0, 1, 0).line, "Flip"));   // want 0, is 1
  CHECK(!strcmp(ekm_switch_classify(162, 0, 1, 1, 0).line, "Flip"));   // want 1, is 0
  CHECK(!strcmp(ekm_switch_classify(163, 0, 1, 1, 0).line, "Leave"));  // want 1, is 1

  // The exit button: the reference is wrong for this engine. sector.cpp:3500 is the whole rule, `(buttonpalette = pal)
  // ? lotag .
  CHECK(ekm_switch_exit_kind(142, 0)  == EKM_EXIT_NORMAL);
  CHECK(ekm_switch_exit_kind(142, 21) == EKM_EXIT_SECRET);
  CHECK(ekm_switch_exit_kind(142, 1)  == EKM_EXIT_SECRET);
  CHECK(ekm_switch_exit_kind(141, 0)  == EKM_EXIT_NONE);   // not the button
  // The classify wrapper agrees, at lotags WT's rule would have refused.
  CHECK(ekm_switch_classify(142, 0, 0, 1, 0).family == EK_SW_EXIT);
  CHECK(ekm_switch_classify(142, 0, 0, 7, 0).family == EK_SW_EXIT);
  CHECK(!strcmp(ekm_switch_classify(142, 21, 0, 7, 0).line, "Secret exit"));
  CHECK(!strcmp(ekm_switch_classify(142, 0, 0, 7, 0).line, "Exit"));

  int clash = -1;
  for (int pic = 0; pic < 4400 && clash < 0; pic++) {
    if (ekm_switch_base_for(pic) < 0 && pic != EKM_PIC_NUKEBUTTON) continue;
    if (ekm_enemy_name(pic) || ekm_item_name(pic) ||
        ekm_crack_name(pic) || ekm_breakable_name(pic)) clash = pic;
  }
  char msg[80];
  sprintf_s(msg, sizeof(msg), "picnum %d is a switch and also another table's", clash);
  CHECK_MSG(clash < 0, msg);
}

// The keycard mark's geometry: a card rather than a cross so a lock reads as a lock; constants are World Tour's.
static void tb_0134_card_mark() {
  CHECK(esp_card_x(100) == 100 - ESP_CARD_W / 2);
  CHECK(esp_card_y(100) == 100 - ESP_CARD_H / 2);
  CHECK(esp_card_notch_y(100) == 100 - ESP_CARD_NOTCH / 2);
  CHECK(esp_card_name_label_y(100) == 100 - ESP_CARD_H / 2 - ESP_LABEL_GAP);
  CHECK(esp_card_dist_label_y(100) == 100 + ESP_CARD_H / 2 + ESP_DIST_GAP);
  // The card must be wider than tall and the notch must fit inside it, or it stops reading as a keycard.
  CHECK(ESP_CARD_W > ESP_CARD_H);
  CHECK(ESP_CARD_NOTCH < ESP_CARD_H && ESP_CARD_NOTCH < ESP_CARD_W);
  CHECK(ESP_CARD_T >= 1 && ESP_CARD_R >= 1);

  // A switch sprite and a switch wall are different kinds, because the index means a sprite in one and a wall in the
  // other.
  CHECK(EK_MARKER_SWITCH_SPRITE != EK_MARKER_SWITCH_WALL);
  CHECK(EK_MARKER_SWITCH_SPRITE != EK_MARKER_ENEMY);
  CHECK(EK_MARKER_SWITCH_SPRITE != EK_MARKER_ITEM);
  CHECK(EK_MARKER_SWITCH_SPRITE != EK_MARKER_SECRET);
  CHECK(EK_MARKER_SWITCH_SPRITE != EK_MARKER_CRACK);
  CHECK(EK_MARKER_SWITCH_SPRITE != EK_MARKER_BREAKABLE);
  CHECK(EK_MARKER_SWITCH_WALL   != EK_MARKER_BREAKABLE);
}

static void tb_0135_world_quad_list() {
  EkWorldQuad list[EK_WORLD_QUAD_MAX];
  memset(list, 0, sizeof(list));
  EkWorldQuad q;
  memset(&q, 0, sizeof(q));

  CHECK(ekm_world_quad_room(0, EK_WORLD_QUAD_MAX) == EK_WORLD_QUAD_MAX);
  CHECK(ekm_world_quad_room(EK_WORLD_QUAD_MAX - 1, EK_WORLD_QUAD_MAX) == 1);
  CHECK(ekm_world_quad_room(EK_WORLD_QUAD_MAX, EK_WORLD_QUAD_MAX) == 0);
  // Past the cap reports NO room, not negative room.
  CHECK(ekm_world_quad_room(EK_WORLD_QUAD_MAX + 7, EK_WORLD_QUAD_MAX) == 0);
  CHECK(ekm_world_quad_room(-3, EK_WORLD_QUAD_MAX) == EK_WORLD_QUAD_MAX);

  int count = 0, counted = 1, ordered = 1;
  for (int i = 0; i < EK_WORLD_QUAD_MAX; i++) {
    q.argb = (unsigned)(i + 1);
    q.x[0] = (double)i + 0.5;
    q.z[0] = i;
    const int after = ekm_world_quad_append(list, count, EK_WORLD_QUAD_MAX, q);
    if (after != count + 1) counted = 0;
    if (list[count].argb != (unsigned)(i + 1)) ordered = 0;
    if (list[count].x[0] != (double)i + 0.5) ordered = 0;
    if (list[count].z[0] != i) ordered = 0;
    count = after;
  }
  CHECK(counted);
  CHECK(ordered);
  CHECK(count == EK_WORLD_QUAD_MAX);

  // Full: the count does not move and the last slot keeps its content rather than taking the refused quad.
  q.argb = 0xDEADBEEFu;
  CHECK(ekm_world_quad_append(list, count, EK_WORLD_QUAD_MAX, q) == count);
  CHECK(list[EK_WORLD_QUAD_MAX - 1].argb == (unsigned)EK_WORLD_QUAD_MAX);
  CHECK(ekm_world_quad_append(list, count + 3, EK_WORLD_QUAD_MAX, q) == count + 3);
  CHECK(ekm_world_quad_append(NULL, 0, EK_WORLD_QUAD_MAX, q) == 0);
}


// The rules telling the player's own renderDrawMasks call from the viewscreen branch's, PINNED AGAINST THE REAL BYTES
// of the six call sites read from the shipped eduke32.exe (a constructed negative would only pin what I imagined the
// compiler emits). maskscan.cpp does not link into test_host, so the rules live in the header.
static void tb_0136_mask_site_rules() {
  CHECK(ekm_mask_straddles(200, 400, 100, 300));
  // Both inside the first chain, both inside the second, or the wrong way up.
  CHECK(!ekm_mask_straddles(150, 250, 100, 300));
  CHECK(!ekm_mask_straddles(310, 400, 100, 300));
  CHECK(!ekm_mask_straddles(200, 290, 100, 300));
  // A site ON a head is the head, not a member of the chain it opens.
  CHECK(!ekm_mask_straddles(100, 400, 100, 300));
  CHECK(!ekm_mask_straddles(200, 300, 100, 300));
  CHECK(!ekm_mask_straddles(200, 400, 300, 100));
  // The pair either way round: the walk hands them over in address order today and the answer must not rest on that.
  CHECK(ekm_mask_straddles(400, 200, 100, 300));
  CHECK(!ekm_mask_straddles(250, 150, 100, 300));

  // Step (d), the one choice separating the player from the monitor.
  CHECK(ekm_mask_player_site(200, 400, 300) == 400);
  CHECK(ekm_mask_player_site(400, 200, 300) == 400);
  // Both after the second head, or neither: not a pair this can choose from.
  CHECK(ekm_mask_player_site(400, 500, 300) == 0);
  CHECK(ekm_mask_player_site(100, 200, 300) == 0);
  // A site ON the head is not after it.
  CHECK(ekm_mask_player_site(300, 400, 300) == 400);
  CHECK(ekm_mask_player_site(200, 300, 300) == 0);
  // The two must agree wherever the first says yes, which no pin above reaches alone.
  static const unsigned kV[7] = { 50, 100, 150, 200, 300, 350, 400 };
  int agree = 1, straddling = 0;
  for (int i = 0; i < 7; i++)
    for (int k = 0; k < 7; k++) {
      const unsigned a = kV[i], b = kV[k];
      if (!ekm_mask_straddles(a, b, 100, 300)) continue;
      straddling++;
      const unsigned pick = ekm_mask_player_site(a, b, 300);
      const unsigned hi = a > b ? a : b;
      const unsigned lo = a > b ? b : a;
      if (pick == 0 || pick != hi || pick <= 300 || lo >= 300) agree = 0;
    }
  CHECK(agree);
  CHECK(straddling > 0);

  // The row table's margin: mask_locate needs a COMPLETE table, so the cap is a feature kill, not a log-length bound.
  CHECK(!ekm_mask_table_is_tight(88, 160));
  CHECK(!ekm_mask_table_is_tight(120, 160));
  CHECK(ekm_mask_table_is_tight(121, 160));
  CHECK(ekm_mask_table_is_tight(160, 160));
  // The old 96 would have called 88 tight: the warning missing when eight rows stood between a working feature and a
  // refusal.
  CHECK(ekm_mask_table_is_tight(88, 96));
  CHECK(!ekm_mask_table_is_tight(0, 0));

  CHECK(ekm_mask_window_len(0x800, 0) == 16);
  // ...and only what follows the previous call when one does.
  CHECK(ekm_mask_window_len(0x800, 0x7FA) == 6);
  CHECK(ekm_mask_window_len(0x800, 0x800) == 0);
  CHECK(ekm_mask_window_len(0x800, 0x700) == 16);
  CHECK(ekm_mask_window_len(0x800, 0x804) == 16);

  // The anchor selection and its permissive direction: three listed calls before a site at 0x100, each ending five
  // bytes on.
  static const unsigned kSites[3] = { 0x0E0, 0x0F0, 0x0FA };
  static const unsigned char kAll[3] = { 1, 1, 1 };
  static const unsigned char kNone[3] = { 0, 0, 0 };
  static const unsigned char kLast[3] = { 1, 1, 0 };
  // The NEAREST one that may anchor, not the first and not the furthest.
  CHECK(ekm_mask_prev_call_end(kSites, kAll, 3, 0x100) == 0x0FF);
  CHECK(ekm_mask_prev_call_end(kSites, kLast, 3, 0x100) == 0x0F5);
  CHECK(ekm_mask_prev_call_end(kSites, kNone, 3, 0x100) == 0);
  CHECK(ekm_mask_window_len(0x100, ekm_mask_prev_call_end(kSites, kNone, 3, 0x100)) == 16);
  // A call ending AFTER the site is not one that precedes it.
  static const unsigned kPast[3] = { 0x0E0, 0x100, 0x200 };
  CHECK(ekm_mask_prev_call_end(kPast, kAll, 3, 0x100) == 0x0E5);
  // Nothing to walk is no anchor, not a read past the end.
  CHECK(ekm_mask_prev_call_end(NULL, kAll, 3, 0x100) == 0);
  CHECK(ekm_mask_prev_call_end(kSites, NULL, 3, 0x100) == 0);
  CHECK(ekm_mask_prev_call_end(kSites, kAll, 0, 0x100) == 0);

  // The permissive direction is the one that matters: one extra anchor cuts the window from eleven bytes to one...
  CHECK(ekm_mask_window_len(0x100, ekm_mask_prev_call_end(kSites, kLast, 3, 0x100)) == 11);
  CHECK(ekm_mask_window_len(0x100, ekm_mask_prev_call_end(kSites, kAll, 3, 0x100)) == 1);
  static const unsigned char hidden[16] = {
    0x41, 0x89, 0xF0, 0x90, 0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
  CHECK(ekm_mask_touches_arg(hidden, 16));
  CHECK(!ekm_mask_touches_arg(hidden + 15, 1));

  static const unsigned char yaxA[16] = {
    0x8B, 0xB4, 0x24, 0x88, 0x00, 0x00, 0x00, 0x31,
    0xD2, 0x0F, 0xBF, 0x4B, 0x18, 0x41, 0x89, 0xF0 };
  static const unsigned char yaxB[16] = {
    0xBF, 0x8F, 0xB8, 0x01, 0x00, 0x00, 0x31, 0xD2,
    0x44, 0x8B, 0x84, 0x24, 0x88, 0x00, 0x00, 0x00 };
  static const unsigned char aniA[16] = {
    0x08, 0x89, 0x74, 0x24, 0x20, 0x41, 0xC1, 0xF9,
    0x10, 0x44, 0x8D, 0x80, 0x00, 0xFC, 0xFF, 0xFF };
  static const unsigned char aniB[16] = {
    0xC8, 0x8B, 0x84, 0x24, 0x88, 0x00, 0x00, 0x00,
    0x41, 0xC1, 0xF9, 0x10, 0x89, 0x44, 0x24, 0x20 };

  CHECK(ekm_mask_touches_arg(yaxA, 16));
  CHECK(ekm_mask_touches_arg(yaxB, 16));
  // G_DoSpriteAnimations takes five and both its calls show it; it survives step (b) too, the third candidate the
  // measured table did not expect, and step (c) throws it out.
  CHECK(ekm_mask_touches_arg(aniA, 16));
  CHECK(ekm_mask_touches_arg(aniB, 16));

  static const unsigned char masksB[10] = {
    0xC7, 0x05, 0xAE, 0x18, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x00 };
  CHECK(!ekm_mask_touches_arg(masksB, 10));
  CHECK(ekm_mask_arg_dest(masksB, 10, 0) == -1);

  // The flat sixteen would have thrown the right function out: chain A's sixteen bytes before renderDrawMasks include
  // the G_DoSpriteAnimations call five bytes earlier (that callee's third and fifth arguments), which is why
  // ekm_mask_window_len exists rather than a constant 16.
  static const unsigned char masksA16[16] = {
    0x41, 0xC1, 0xF9, 0x10, 0x44, 0x8D, 0x80, 0x00,
    0xFC, 0xFF, 0xFF, 0xE8, 0xAB, 0xE1, 0x00, 0x00 };
  CHECK(ekm_mask_touches_arg(masksA16, 16));
  CHECK(!ekm_mask_touches_arg(masksA16, 0));

  static const unsigned char xorEdx[2] = { 0x31, 0xD2 };           // xor edx,edx
  static const unsigned char movR8dMem[8] = {                      // mov r8d,[rsp+0x88]
    0x44, 0x8B, 0x84, 0x24, 0x88, 0x00, 0x00, 0x00 };
  static const unsigned char movR8dEsi[3] = { 0x41, 0x89, 0xF0 };  // mov r8d,esi
  static const unsigned char movEaxMem[7] = {                      // mov eax,[rsp+0x88]
    0x8B, 0x84, 0x24, 0x88, 0x00, 0x00, 0x00 };
  CHECK(ekm_mask_arg_dest(xorEdx, 2, 0) == 2);
  CHECK(ekm_mask_arg_dest(movR8dMem, 8, 0) == 8);
  CHECK(ekm_mask_arg_dest(movR8dEsi, 3, 0) == 8);
  // A write to a register this does not care about is named, not counted.
  CHECK(ekm_mask_arg_dest(movEaxMem, 7, 0) == 0);
  CHECK(!ekm_mask_is_arg_reg(ekm_mask_arg_dest(movEaxMem, 7, 0)));
  CHECK(ekm_mask_is_arg_reg(1) && ekm_mask_is_arg_reg(2));
  CHECK(ekm_mask_is_arg_reg(6) && ekm_mask_is_arg_reg(7));
  CHECK(ekm_mask_is_arg_reg(8) && ekm_mask_is_arg_reg(9));
  CHECK(!ekm_mask_is_arg_reg(0) && !ekm_mask_is_arg_reg(3));
  CHECK(!ekm_mask_is_arg_reg(-1));
  CHECK(ekm_mask_arg_dest(movR8dEsi, 1, 0) == -1);
  CHECK(ekm_mask_arg_dest(movR8dEsi, 3, 3) == -1);
  CHECK(ekm_mask_arg_dest(NULL, 3, 0) == -1);
  CHECK(!ekm_mask_touches_arg(NULL, 16));

  // Step (e), corroboration: renderDrawMasks measured 10 sites in 7 functions against source's 8 in five translation
  // units.
  CHECK(ekm_mask_corroborated(10, 7));
  CHECK(ekm_mask_corroborated(EK_MASK_MIN_SITES, EK_MASK_MIN_OWNERS));
  CHECK(!ekm_mask_corroborated(EK_MASK_MIN_SITES - 1, EK_MASK_MIN_OWNERS));
  CHECK(!ekm_mask_corroborated(EK_MASK_MIN_SITES, EK_MASK_MIN_OWNERS - 1));
  CHECK(!ekm_mask_corroborated(2, 2));
  // The extent floor sits well under renderDrawMasks' measured 6937 bytes (a rebuild may move it) and over the small
  // helpers the two chains share.
  CHECK(EK_MASK_MIN_EXTENT >= 2048u && EK_MASK_MIN_EXTENT <= 6000u);
}


// esp_tint.h's pure half: the verdict, the wall-sprite span and vertical extent, and the sector clamp.
static void tb_0140_model_bounds_rules() {
  // -1 is absent, not an error: legal with an open range and an empty one.
  CHECK(ekm_mdl_modelid_plausible(-1, 500));
  CHECK(ekm_mdl_modelid_plausible(-1, 0));
  CHECK(ekm_mdl_modelid_plausible(0, 500));
  // nextmodelid is one past the loaded range (build.h:1619, mdsprite.h:231), so out of range, not the last valid id.
  CHECK(!ekm_mdl_modelid_plausible(500, 500));
  CHECK(!ekm_mdl_modelid_plausible(-2, 500));
  CHECK(!ekm_mdl_modelid_plausible(0, 0));
  CHECK(!ekm_mdl_modelid_plausible(5, -1));

  CHECK(ekm_mdl_frame_plausible(-10.0, -20.0, -5.0, 10.0, 20.0, 5.0));
  CHECK(!ekm_mdl_frame_plausible(10.0, -20.0, -5.0, -10.0, 20.0, 5.0));  // x swapped
  CHECK(!ekm_mdl_frame_plausible(-10.0, 20.0, -5.0, 10.0, -20.0, 5.0));  // y swapped
  CHECK(!ekm_mdl_frame_plausible(-10.0, -20.0, 5.0, 10.0, 20.0, -5.0));  // z swapped
  // A zero-size box on any one axis is refused, not clamped.
  CHECK(!ekm_mdl_frame_plausible(0.0, -20.0, -5.0, 0.0, 20.0, 5.0));
  CHECK(!ekm_mdl_frame_plausible(-10.0, 0.0, -5.0, 10.0, 0.0, 5.0));
  CHECK(!ekm_mdl_frame_plausible(-10.0, -20.0, 0.0, 10.0, 20.0, 0.0));
  const double inf = std::numeric_limits<double>::infinity();
  const double nan_v = std::numeric_limits<double>::quiet_NaN();
  CHECK(!ekm_mdl_frame_plausible(-10.0, -20.0, -5.0, inf, 20.0, 5.0));
  CHECK(!ekm_mdl_frame_plausible(-10.0, nan_v, -5.0, 10.0, 20.0, 5.0));
  CHECK(!ekm_mdl_frame_plausible(-10.0, -20.0, -5.0, 10.0, 20.0, inf));
  CHECK(!ekm_mdl_frame_plausible(-10.0, -20.0, nan_v, 10.0, 20.0, 5.0));

  EkModelBox u = ekm_mdl_scale_bbox(-8.0, -16.0, -4.0, 8.0, 16.0, 4.0,
                                    1.0, 1.0, 0.0, 0.0);
  CHECK(u.ok);
  CHECK(u.minx == -2.0 && u.maxx == 2.0);      // 8 * 0.25
  CHECK(u.miny == -4.0 && u.maxy == 4.0);      // 16 * 0.25
  CHECK(u.minz == -16.0 && u.maxz == 16.0);    // 4 * 4, negated and swapped

  // BSCALE is an input: md_setmisc (mdsprite.cpp:279) writes the def file's scale into bscale and leaves scale at the
  // loader's value, so a rule reading only `scale` is wrong by 1/bscale on every def'd model and passes any pin leaving
  // bscale at 1.
  EkModelBox h = ekm_mdl_scale_bbox(-8.0, -16.0, -4.0, 8.0, 16.0, 4.0,
                                    1.0, 0.5, 0.0, 0.0);
  CHECK(h.minx == -1.0 && h.maxx == 1.0);
  CHECK(h.miny == -2.0 && h.maxy == 2.0);
  CHECK(h.minz == -8.0 && h.maxz == 8.0);

  EkModelBox m2 = ekm_mdl_scale_bbox(-8.0, -16.0, -4.0, 8.0, 16.0, 4.0,
                                     4.0, 0.5, 0.0, 0.0);
  CHECK(m2.minx == -4.0 && m2.maxx == 4.0);    // 8 * 4 * 0.5 * 0.25
  CHECK(m2.minz == -32.0 && m2.maxz == 32.0);  // 4 * 4 * 0.5 * 4

  // z does not share x and y's factor; the gap is Build's vertical unit, sixteen.
  EkModelBox iso = ekm_mdl_scale_bbox(-8.0, -8.0, -8.0, 8.0, 8.0, 8.0,
                                      1.0, 1.0, 0.0, 0.0);
  CHECK((iso.maxx - iso.minx) == 4.0);
  CHECK((iso.maxz - iso.minz) == 64.0);
  CHECK((iso.maxz - iso.minz) == 16.0 * (iso.maxx - iso.minx));

  EkModelBox up = ekm_mdl_scale_bbox(-8.0, -16.0, 1.0, 8.0, 16.0, 8.0,
                                     1.0, 1.0, 0.0, 0.0);
  CHECK(up.ok);
  CHECK(up.minz == -32.0 && up.maxz == -4.0);   // -(8*4), -(1*4)
  CHECK(up.minx == -2.0 && up.maxx == 2.0 && up.miny == -4.0 && up.maxy == 4.0);

  // The real shape: an MD3 stands on its own origin, so raw 0..+H occupies build z ABOVE the sprite and touches 0 at
  // its feet.
  EkModelBox stand = ekm_mdl_scale_bbox(-8.0, -16.0, 0.0, 8.0, 16.0, 64.0,
                                        1.0, 1.0, 0.0, 0.0);
  CHECK(stand.minz == -256.0 && stand.maxz == 0.0);

  // zadd shifts z only, at 256: SIXTY-FOUR TIMES the per-vertex z factor (the [10:6] fixed point the raw vertices are
  // stored in).
  EkModelBox za = ekm_mdl_scale_bbox(-8.0, -16.0, -4.0, 8.0, 16.0, 4.0,
                                     1.0, 1.0, 2.0, 0.0);
  CHECK(za.minx == -2.0 && za.maxx == 2.0 && za.miny == -4.0 && za.maxy == 4.0);
  CHECK(za.minz == -528.0 && za.maxz == -496.0);   // -16 - 512, 16 - 512

  // yoffset does the same, and the two add rather than one replacing the other (mdsprite.cpp:2032 then :2050, which is
  // +=).
  EkModelBox yo = ekm_mdl_scale_bbox(-8.0, -16.0, -4.0, 8.0, 16.0, 4.0,
                                     1.0, 1.0, 0.0, 2.0);
  CHECK(yo.minz == -528.0 && yo.maxz == -496.0);
  EkModelBox both = ekm_mdl_scale_bbox(-8.0, -16.0, -4.0, 8.0, 16.0, 4.0,
                                       1.0, 1.0, 2.0, 2.0);
  CHECK(both.minz == -1040.0 && both.maxz == -1008.0);  // -16 - 1024, 16 - 1024

  // The offset on the lopsided box too, so its sign is pinned where the swap cannot be mistaken for it.
  EkModelBox upoff = ekm_mdl_scale_bbox(-8.0, -16.0, 1.0, 8.0, 16.0, 8.0,
                                        1.0, 1.0, 1.0, 0.0);
  CHECK(upoff.minz == -288.0 && upoff.maxz == -260.0);  // -32 - 256, -4 - 256

  EkModelBox offs = ekm_mdl_scale_bbox(-8.0, -16.0, -4.0, 8.0, 16.0, 4.0,
                                       2.0, 1.0, 2.0, 0.0);
  CHECK(offs.minz == -1056.0 && offs.maxz == -992.0);  // -4*8 - 1024, 4*8 - 1024
  EkModelBox offb = ekm_mdl_scale_bbox(-8.0, -16.0, -4.0, 8.0, 16.0, 4.0,
                                       1.0, 2.0, 2.0, 0.0);
  CHECK(offb.minz == -1056.0 && offb.maxz == -992.0);

  CHECK(!ekm_mdl_scale_bbox(-8.0, -16.0, -4.0, 8.0, 16.0, 4.0, 0.0, 1.0, 0.0, 0.0).ok);
  CHECK(!ekm_mdl_scale_bbox(-8.0, -16.0, -4.0, 8.0, 16.0, 4.0, 1.0, 0.0, 0.0, 0.0).ok);
  CHECK(!ekm_mdl_scale_bbox(-8.0, -16.0, -4.0, 8.0, 16.0, 4.0, -1.0, 1.0, 0.0, 0.0).ok);
  CHECK(!ekm_mdl_scale_bbox(-8.0, -16.0, -4.0, 8.0, 16.0, 4.0, 1.0, -1.0, 0.0, 0.0).ok);
  CHECK(!ekm_mdl_scale_bbox(-8.0, -16.0, -4.0, 8.0, 16.0, 4.0, inf, 1.0, 0.0, 0.0).ok);
  CHECK(!ekm_mdl_scale_bbox(-8.0, -16.0, -4.0, 8.0, 16.0, 4.0, 1.0, nan_v, 0.0, 0.0).ok);
  CHECK(!ekm_mdl_scale_bbox(-8.0, -16.0, 0.0, 8.0, 16.0, 0.0, 1.0, 1.0, 0.0, 0.0).ok);
}

// Two corners of one quad compared exactly: every input is a power of two and both sines are exact at these angles, so
// a tolerance would only let a wrong constant through.
static bool wq_corner_same(const EkWorldQuad& q, int a, int b) {
  return q.x[a] == q.x[b] && q.y[a] == q.y[b] && q.z[a] == q.z[b];
}

// The bowtie test: corners given top/top/bottom/bottom render as two crossing triangles (an hourglass).
static bool wq_walks_its_rectangle(const EkWorldQuad& q) {
  const double dx = (q.x[0] + q.x[2]) - (q.x[1] + q.x[3]);
  const double dy = (q.y[0] + q.y[2]) - (q.y[1] + q.y[3]);
  const double dz = ((double)q.z[0] + (double)q.z[2]) -
                    ((double)q.z[1] + (double)q.z[3]);
  return fabs(dx) < 1e-9 && fabs(dy) < 1e-9 && fabs(dz) < 1e-9;
}

// Extents of a whole face list, read from every corner of every face.
static void wq_box_extent(const EkWorldQuad* f, int n, double* lox, double* hix,
                          double* loy, double* hiy, int* loz, int* hiz) {
  *lox = *hix = f[0].x[0];
  *loy = *hiy = f[0].y[0];
  *loz = *hiz = f[0].z[0];
  for (int i = 0; i < n; i++)
    for (int k = 0; k < 4; k++) {
      if (f[i].x[k] < *lox) *lox = f[i].x[k];
      if (f[i].x[k] > *hix) *hix = f[i].x[k];
      if (f[i].y[k] < *loy) *loy = f[i].y[k];
      if (f[i].y[k] > *hiy) *hiy = f[i].y[k];
      if (f[i].z[k] < *loz) *loz = f[i].z[k];
      if (f[i].z[k] > *hiz) *hiz = f[i].z[k];
    }
}

static int wq_coincident_corners(const EkWorldQuad* f, int n) {
  int hits = 0;
  for (int i = 0; i < n; i++)
    for (int a = 0; a < 4; a++)
      for (int c = a + 1; c < 4; c++)
        if (wq_corner_same(f[i], a, c)) hits++;
  return hits;
}

// esp_tint.h's box geometry: one model bounding box, turned by the sprite's ang and scaled by its repeats, cut into six
// faces.
static void tb_0141_tint_box() {
  // The model's own extents, repeats excluded: one unit out of the wall, eight across, thirty-two up and down.
  EkModelBox b;
  b.minx = -1.0;  b.maxx = 1.0;
  b.miny = -8.0;  b.maxy = 8.0;
  b.minz = -32.0; b.maxz = 32.0;
  b.ok = 1;

  // ang 0: Build's cosine is sintable[512], the peak, so the facing normal is +x and the span -y, and both sines are
  // exact.
  EkWorldQuad f[6];
  CHECK(ekm_tint_box_faces(b, 1000, 2000, 3000, 0, 1, 1, f, 0x11223344u) == 6);
  CHECK(wq_coincident_corners(f, 6) == 0);

  int walked = 0;
  for (int i = 0; i < 6; i++)
    if (wq_walks_its_rectangle(f[i])) walked++;
  CHECK(walked == 6);

  // The bowtie test can fail, which is the check the check needs: swapping the last two corners of a rectangle walk
  // gives the top/top/bottom/bottom order and must be rejected, or a rule returning true would pass every winding pin.
  EkWorldQuad bow = f[0];
  bow.x[2] = f[0].x[3];  bow.y[2] = f[0].y[3];  bow.z[2] = f[0].z[3];
  bow.x[3] = f[0].x[2];  bow.y[3] = f[0].y[2];  bow.z[3] = f[0].z[2];
  CHECK(!wq_walks_its_rectangle(bow));

  double ux[24], uy[24];
  int uz[24], useen[24], ucount = 0;
  for (int i = 0; i < 6; i++)
    for (int k = 0; k < 4; k++) {
      int at = -1;
      for (int u = 0; u < ucount; u++)
        if (ux[u] == f[i].x[k] && uy[u] == f[i].y[k] && uz[u] == f[i].z[k]) {
          at = u;
          break;
        }
      if (at < 0) {
        at = ucount++;
        ux[at] = f[i].x[k];
        uy[at] = f[i].y[k];
        uz[at] = f[i].z[k];
        useen[at] = 0;
      }
      useen[at]++;
    }
  CHECK(ucount == 8);
  int thrice = 0;
  for (int u = 0; u < ucount; u++)
    if (useen[u] == 3) thrice++;
  CHECK(thrice == 8);

  // Which model axis lands on which world axis: at ang 0 the model's x runs along the normal (one unit of depth, build
  // x) and its y across the face (eight units, build y), the opposite of the obvious guess.
  double lox, hix, loy, hiy;
  int loz, hiz;
  wq_box_extent(f, 6, &lox, &hix, &loy, &hiy, &loz, &hiz);
  CHECK(lox == 999.0 && hix == 1001.0);
  CHECK(loy == 1992.0 && hiy == 2008.0);
  CHECK(loz == 2968 && hiz == 3032);

  // The box's own min and max place it; it is not a half-extent hung symmetrically off the sprite.
  EkModelBox off;
  off.minx = 2.0;   off.maxx = 6.0;    // entirely out of the wall
  off.miny = -2.0;  off.maxy = 10.0;   // and off-centre across the face
  off.minz = -32.0; off.maxz = -8.0;   // entirely above the sprite
  off.ok = 1;
  EkWorldQuad a[6];
  CHECK(ekm_tint_box_faces(off, 1000, 2000, 3000, 0, 1, 1, a, 0x11223344u) == 6);
  wq_box_extent(a, 6, &lox, &hix, &loy, &hiy, &loz, &hiz);
  // A centring rule would give 998..1002, 1994..2006 and 2988..3012 instead.
  CHECK(lox == 1002.0 && hix == 1006.0);
  CHECK(loy == 1990.0 && hiy == 2002.0);
  CHECK(loz == 2968 && hiz == 2992);

  // ARGB in, ARGB out: this port's colours are 0xAARRGGBB (ui/canvas.h:3), and a geometry helpfully reversing to rgba
  // would draw every tint in the wrong hue at the wrong alpha.
  int tinted = 0;
  for (int i = 0; i < 6; i++)
    if (f[i].argb == 0x11223344u) tinted++;
  CHECK(tinted == 6);

  // It turns with ang: a quarter turn sends the normal onto +y and the span onto +x, so width and depth exchange axes.
  EkWorldQuad q[6];
  CHECK(ekm_tint_box_faces(b, 1000, 2000, 3000, 512, 1, 1, q, 0x11223344u) == 6);
  wq_box_extent(q, 6, &lox, &hix, &loy, &hiy, &loz, &hiz);
  CHECK(hix - lox == 16.0 && hiy - loy == 2.0);
  CHECK(loz == 2968 && hiz == 3032);  // the vertical axis does not turn

  // The repeats: polymer.cpp:4531 scales both horizontals by xrepeat and the vertical by yrepeat.
  EkWorldQuad w[6];
  CHECK(ekm_tint_box_faces(b, 1000, 2000, 3000, 0, 2, 1, w, 0x11223344u) == 6);
  wq_box_extent(w, 6, &lox, &hix, &loy, &hiy, &loz, &hiz);
  CHECK(hix - lox == 4.0 && hiy - loy == 32.0);
  CHECK(hiz - loz == 64);

  CHECK(ekm_tint_box_faces(b, 1000, 2000, 3000, 0, 1, 4, w, 0x11223344u) == 6);
  wq_box_extent(w, 6, &lox, &hix, &loy, &hiy, &loz, &hiz);
  CHECK(hiz - loz == 256);
  CHECK(hix - lox == 2.0 && hiy - loy == 16.0);

  // A degenerate box yields nothing, not six flat faces: six coincident faces draw as one plane at six times the alpha,
  // the flat tint this replaces, silently.
  EkWorldQuad r[6];
  EkModelBox d = b;
  d.ok = 0;
  CHECK(ekm_tint_box_faces(d, 1000, 2000, 3000, 0, 1, 1, r, 0x11223344u) == 0);
  // Each axis on its own: one combined comparison passes with a single axis flat, and ok is the caller's word.
  EkModelBox flatx = b;
  flatx.minx = flatx.maxx = 0.0;
  CHECK(ekm_tint_box_faces(flatx, 1000, 2000, 3000, 0, 1, 1, r, 0x11223344u) == 0);
  EkModelBox flaty = b;
  flaty.miny = flaty.maxy = 0.0;
  CHECK(ekm_tint_box_faces(flaty, 1000, 2000, 3000, 0, 1, 1, r, 0x11223344u) == 0);
  EkModelBox flatz = b;
  flatz.minz = flatz.maxz = 0.0;
  CHECK(ekm_tint_box_faces(flatz, 1000, 2000, 3000, 0, 1, 1, r, 0x11223344u) == 0);
  EkModelBox inv = b;
  inv.minz = 32.0;
  inv.maxz = -32.0;
  CHECK(ekm_tint_box_faces(inv, 1000, 2000, 3000, 0, 1, 1, r, 0x11223344u) == 0);
  // An infinite axis, which ordering alone does NOT catch: -inf < +inf is true, so the old `!(min < max)` guard passed
  // with ok still 1 and handed the layer infinite corners. ekm_mdl_scale_bbox refuses non-finite input, so nothing here
  // can produce one; the pin keeps that a property of this rule.
  EkModelBox inf = b;
  inf.minz = -HUGE_VAL;
  inf.maxz = HUGE_VAL;
  CHECK(ekm_tint_box_faces(inf, 1000, 2000, 3000, 0, 1, 1, r, 0x11223344u) == 0);
  EkModelBox infx = b;
  infx.maxx = HUGE_VAL;
  CHECK(ekm_tint_box_faces(infx, 1000, 2000, 3000, 0, 1, 1, r, 0x11223344u) == 0);
  // A repeat of zero collapses the box exactly as the engine's own scale does.
  CHECK(ekm_tint_box_faces(b, 1000, 2000, 3000, 0, 0, 1, r, 0x11223344u) == 0);
  CHECK(ekm_tint_box_faces(b, 1000, 2000, 3000, 0, 1, 0, r, 0x11223344u) == 0);
  // Nowhere to write is a refusal, not a crash.
  CHECK(ekm_tint_box_faces(b, 1000, 2000, 3000, 0, 1, 1, NULL, 0x11223344u) == 0);

  // All of it holds off the axes: at ang 0 and 512 one sine is zero, so a face from the wrong corners can still land on
  // a rectangle by accident. ang 300 has neither sine zero, and the position and repeats are off round numbers too.
  EkWorldQuad o[6];
  CHECK(ekm_tint_box_faces(b, -900, 450, -7000, 300, 3, 5, o, 0x11223344u) == 6);
  int walkedOff = 0;
  for (int i = 0; i < 6; i++)
    if (wq_walks_its_rectangle(o[i])) walkedOff++;
  CHECK(walkedOff == 6);
  CHECK(wq_coincident_corners(o, 6) == 0);
}

// The model-origin rule: where a y-centred sprite's MODEL sits in build z. polymer.cpp:4510-4511 translates it half a
// sprite-height down before anything scales it, and handing the box the raw tspr->z would hang every centred lock's
// volume half a switch off the lock.
static void tb_0142_tint_model_origin_z() {
  // Not centred is the identity: the sprite's own z is already the origin.
  CHECK(ekm_tint_model_origin_z(3000, 32, 64, 0) == 3000);
  CHECK(ekm_tint_model_origin_z(-70000, 20, 17, 0) == -70000);

  // Centred moves it by 2 * tilesizy * yrepeat: a full sprite height is 4 * tilesizy * yrepeat, so exactly half.
  CHECK(ekm_tint_model_origin_z(3000, 32, 64, 1) == 3000 + 4096);
  CHECK(ekm_tint_model_origin_z(0, 1, 1, 1) == 2);

  // The direction is the pin that matters: build z grows DOWNWARD and the origin belongs at the foot, so the number
  // goes UP.
  CHECK(ekm_tint_model_origin_z(1000, 16, 50, 1) > 1000);

  // Agreement with the engine's own sprite path is corroboration: at yoff 0 and an EVEN tile height the centred bottom
  // ekm_tint_wall_sprite_zrange computes IS the sprite's foot.
  int agree = 0, tried = 0;
  for (int t = 2; t <= 32; t += 2)
    for (int yr = 1; yr <= 25; yr += 8) {
      int top = 0, bottom = 0;
      ekm_tint_wall_sprite_zrange(5000, yr, t, 0, 1, &top, &bottom);
      tried++;
      if (ekm_tint_model_origin_z(5000, yr, t, 1) == bottom) agree++;
    }
  CHECK(tried == 64 && agree == 64);

  int otop = 0, obottom = 0;
  ekm_tint_wall_sprite_zrange(5000, 24, 17, 0, 1, &otop, &obottom);
  CHECK(obottom - ekm_tint_model_origin_z(5000, 24, 17, 1) == 48);

  // A garbage repeat saturates rather than wraps: yrepeat and tilesizy are unchecked engine fields, and without 64-bit
  // widening this overflows and lands the box at a large negative z across the map.
  CHECK(ekm_tint_model_origin_z(2147000000, 255, 1024, 1) == 2147483647);
}

// secretvol.h's pure rules: quad winding/z-survival and the staging caps.
static void tb_0143_secret_volume_rules() {
  double x4[4], y4[4]; int z4[4];

  // Each end's inversion refuses on its own: a combined "both ends inverted" check would pass a wall with one end
  // swapped.
  EkSecretWall badTop0 = { 0, 0, 100, 50, 200, 0, 0, 100 };   // end A inverted, end B fine
  CHECK(ekm_secvol_quad_corners(badTop0, x4, y4, z4) == 0);
  EkSecretWall badTop1 = { 0, 0, 0, 100, 200, 0, 100, 50 };   // end A fine, end B inverted
  CHECK(ekm_secvol_quad_corners(badTop1, x4, y4, z4) == 0);
  EkSecretWall zeroLen = { 500, 500, 0, 100, 500, 500, 0, 100 };
  CHECK(ekm_secvol_quad_corners(zeroLen, x4, y4, z4) == 0);

  EkSecretWall flat = { 0, 0, 0, 100, 200, 300, 0, 100 };
  CHECK(ekm_secvol_quad_corners(flat, x4, y4, z4) == 4);
  CHECK(x4[0] + x4[2] == x4[1] + x4[3]);
  CHECK(y4[0] + y4[2] == y4[1] + y4[3]);
  CHECK(z4[0] + z4[2] == z4[1] + z4[3]);

  // Prove the predicate can fail: swap the middle two corners of the same rectangle, crossing the quad through its
  // centre.
  double bx[4] = { x4[0], x4[2], x4[1], x4[3] };
  double by[4] = { y4[0], y4[2], y4[1], y4[3] };
  int    bz[4] = { z4[0], z4[2], z4[1], z4[3] };
  CHECK(!(bx[0] + bx[2] == bx[1] + bx[3] && by[0] + by[2] == by[1] + by[3] && bz[0] + bz[2] == bz[1] + bz[3]));

  // Four distinct z values, one per corner, survive independently.
  EkSecretWall sloped = { 10, 20, 1, 2, 30, 40, 3, 4 };
  CHECK(ekm_secvol_quad_corners(sloped, x4, y4, z4) == 4);
  CHECK(z4[0] == 1 && z4[1] == 2 && z4[2] == 4 && z4[3] == 3);
  CHECK(x4[0] == 10 && y4[0] == 20 && x4[1] == 10 && y4[1] == 20);
  CHECK(x4[2] == 30 && y4[2] == 40 && x4[3] == 30 && y4[3] == 40);

  // Each cap refused separately, plus the malformed (wallnum <= 0) case a refused engine read would also produce.
  CHECK(ekm_secvol_room_fits(EK_SECVOL_WALLS_PER_ROOM, 0, 0) == 1);        // exactly at the per-room cap
  CHECK(ekm_secvol_room_fits(EK_SECVOL_WALLS_PER_ROOM + 1, 0, 0) == 0);    // one over the per-room cap alone
  CHECK(ekm_secvol_room_fits(64, 480, 0) == 0);                           // fits per-room, 480+64 > total cap
  CHECK(ekm_secvol_room_fits(10, 0, EK_SECVOL_ROOMS_MAX) == 0);           // rooms already at cap
  CHECK(ekm_secvol_room_fits(1, 0, EK_SECVOL_ROOMS_MAX - 1) == 1);        // one room short of the cap
  CHECK(ekm_secvol_room_fits(0, 0, 0) == 0);                              // malformed: zero walls
  CHECK(ekm_secvol_room_fits(-1, 0, 0) == 0);                             // malformed: negative walls
  CHECK(ekm_secvol_room_fits(10, 100, 5) == 1);                           // ordinary passing case
}

static void tb_0144_secret_trigger_rules() {
  CHECK(ekm_trig_effect(1, 0, 1) == EK_TRIG_UNLOCKS);
  CHECK(ekm_trig_effect(1, 1, 1) == EK_TRIG_UNLOCKS);   // secret AND adjoins AND locked: still the lock
  CHECK(ekm_trig_effect(0, 1, 1) == EK_TRIG_UNLOCKS);   // adjoins-only AND locked: still the lock
  CHECK(ekm_trig_effect(0, 0, 1) == EK_TRIG_UNLOCKS);   // locked ALONE, neither secret nor adjoins: still
  CHECK(ekm_trig_effect(1, 0, 0) == EK_TRIG_SECRET);
  CHECK(ekm_trig_effect(0, 1, 0) == EK_TRIG_OPENS);
  CHECK(ekm_trig_effect(1, 1, 0) == EK_TRIG_SECRET);    // both forms true: the stronger one (SECRET) wins

  CHECK(strcmp(ekm_trig_label(EK_TRIG_SECRET), "Secret") == 0);
  CHECK(strcmp(ekm_trig_label(EK_TRIG_OPENS), "Opens secret") == 0);
  // "Locked" names the state the player sees, not "Unlocks" (the verb).
  CHECK(strcmp(ekm_trig_label(EK_TRIG_UNLOCKS), "Locked") == 0);
  CHECK(ekm_trig_label(99) != NULL);
  CHECK(ekm_trig_label(-1) != NULL);

  // The two positive forms on their own lines: a combined "either nonzero" check would pass a rule testing only one
  // argument.
  CHECK(ekm_trig_reaches_secret(1, 0) != 0);   // operatedIsSecret alone
  CHECK(ekm_trig_reaches_secret(0, 1) != 0);   // operatedAdjoinsSecret alone
  CHECK(ekm_trig_reaches_secret(0, 0) == 0);   // neither

  CHECK(ekm_trig_chain_fits(EK_TRIG_MAX_CHAINS - 1, EK_TRIG_MAX_CHAINS) != 0);  // one short of the cap
  CHECK(ekm_trig_chain_fits(EK_TRIG_MAX_CHAINS, EK_TRIG_MAX_CHAINS) == 0);      // exactly at the cap
  CHECK(ekm_trig_chain_fits(EK_TRIG_MAX_CHAINS + 1, EK_TRIG_MAX_CHAINS) == 0);  // one past the cap

  EkTrigChain c = { 3, EK_TRIG_TOUCHPLATE, EK_TRIG_UNLOCKS, 7, 100, 200, 300, 400, 500 };
  CHECK(c.triggerIndex == 3 && c.triggerKind == EK_TRIG_TOUCHPLATE && c.effect == EK_TRIG_UNLOCKS);
  CHECK(c.sectnum == 7 && c.tx == 100 && c.ty == 200 && c.tz == 300);
  CHECK(c.cx == 400 && c.cy == 500);
}

static void tb_0145_secret_trigger_responders() {
  CHECK(ekm_trig_responder_kind(EK_TRIG_STAT_ACTIVATOR, 2) == EK_TRIG_RESP_ACTIVATOR);           // [swap]
  CHECK(ekm_trig_responder_kind(EK_TRIG_STAT_ACTIVATOR, EK_TRIG_PIC_ACTIVATORLOCKED) == EK_TRIG_RESP_ACTIVATOR);  // [swap]
  // The status list ALONE decides an activator, because G_OperateActivators walks it: a switch tile parked there is
  // still one.
  CHECK(ekm_trig_responder_kind(EK_TRIG_STAT_ACTIVATOR, 130) == EK_TRIG_RESP_ACTIVATOR);         // [swap]
  // Picnum 8 on the ACTIVATOR list is an activator, not a master switch.
  CHECK(ekm_trig_responder_kind(EK_TRIG_STAT_ACTIVATOR, EK_TRIG_PIC_MASTERSWITCH) == EK_TRIG_RESP_ACTIVATOR);

  CHECK(ekm_trig_responder_kind(EK_STAT_STANDABLE, EK_TRIG_PIC_MASTERSWITCH) == EK_TRIG_RESP_MASTERSWITCH);  // [swap]
  CHECK(ekm_trig_responder_kind(EK_STAT_STANDABLE, EK_TRIG_PIC_TOUCHPLATE) == EK_TRIG_RESP_NONE);
  // MASTERSWITCH's picnum off its own status list is nothing: a picnum-only rule fails, and so does a swapped one.
  CHECK(ekm_trig_responder_kind(0, EK_TRIG_PIC_MASTERSWITCH) == EK_TRIG_RESP_NONE);              // [swap]
  CHECK(ekm_trig_responder_kind(0, 0) == EK_TRIG_RESP_NONE);

  CHECK(EK_TRIG_STAT_ACTIVATOR != EK_STAT_STANDABLE);

  // Only an activator can be locked.
  CHECK(ekm_trig_responder_locked(EK_TRIG_RESP_ACTIVATOR, EK_TRIG_PIC_ACTIVATORLOCKED) == 1);
  CHECK(ekm_trig_responder_locked(EK_TRIG_RESP_ACTIVATOR, 2) == 0);
  CHECK(ekm_trig_responder_locked(EK_TRIG_RESP_MASTERSWITCH, EK_TRIG_PIC_ACTIVATORLOCKED) == 0);
  CHECK(ekm_trig_responder_locked(EK_TRIG_RESP_MASTERSWITCH, EK_TRIG_PIC_MASTERSWITCH) == 0);
  CHECK(ekm_trig_responder_locked(EK_TRIG_RESP_NONE, EK_TRIG_PIC_ACTIVATORLOCKED) == 0);

  // No fourth effect: a master switch reuses the existing three because it can never be locked, so EK_TRIG_SECRET and
  // EK_TRIG_OPENS name both its outcomes.
  const int msLocked =
      ekm_trig_responder_locked(EK_TRIG_RESP_MASTERSWITCH, EK_TRIG_PIC_ACTIVATORLOCKED);
  CHECK(ekm_trig_effect(1, 0, msLocked) == EK_TRIG_SECRET);
  CHECK(ekm_trig_effect(0, 1, msLocked) == EK_TRIG_OPENS);

  // The stale-slot guard: a free slot keeps its old picnum and lotag forever (secrettrig.h), so the switch role, the
  // one branch testing neither against an exact statnum, needs this or it stages a chain from the previous map's
  // coordinates.
  CHECK(ekm_trig_sprite_live(EK_MAXSTATUS) == 0);        // exactly the free-slot value
  CHECK(ekm_trig_sprite_live(EK_MAXSTATUS + 1) == 0);    // past it
  CHECK(ekm_trig_sprite_live(-1) == 0);                  // negative is not a status list
  CHECK(ekm_trig_sprite_live(EK_MAXSTATUS - 1) != 0);    // the last live list, one short
  CHECK(ekm_trig_sprite_live(EK_STAT_DEFAULT) != 0);     // where most switch sprites sit
  // The case a `statnum == EK_STAT_DEFAULT` guard would have deleted: a switch tile on the activator list is BOTH
  // roles.
  CHECK(ekm_trig_sprite_live(EK_TRIG_STAT_ACTIVATOR) != 0);
}

// The secret path's pure half: the door set this engine's source settles, the two passability boundaries measured
// apart, and the breadth-first search including which END of an over-long route survives truncation.
static void tb_0146_secret_path_rules() {
  // The door set, one check per lotag, as a table: World Tour's RANGE rule `(l >= 20 && l <= 23) || (l >= 25 && l <=
  // 31)` fails on 9, 27, 28, 30 and 31, not only the one written out.
  static const unsigned char kDoor[32] = {
    /* 0-8   */ 0,0,0,0,0,0,0,0,0,
    /* 9     */ 1,
    /* 10-14 */ 0,0,0,0,0,
    /* 15-19 */ 0,0,0,0,0,
    /* 20-23 */ 1,1,1,1,
    /* 24    */ 0,
    /* 25,26 */ 1,1,
    /* 27,28 */ 0,0,
    /* 29    */ 1,
    /* 30,31 */ 0,0
  };
  for (int l = 0; l < 32; l++)
    CHECK_MSG(ekm_spath_lotag_is_door(l) == (int)kDoor[l], "door set");

  // The high byte is live state: a door records open/closed in bit 15 and ACTIVATORLOCKED toggles bit 14
  // (sector.cpp:830, :1126), and isanearoperator masks to `lotag & 0xff` (:201).
  CHECK(ekm_spath_lotag_is_door(0x8000 | 20) != 0);   // an OPEN ceiling door
  CHECK(ekm_spath_lotag_is_door(0x4000 | 21) != 0);   // a LOCKED floor door
  CHECK(ekm_spath_lotag_is_door(0x8000 | 27) == 0);   // an open stretch bridge is still not a door
  CHECK(ekm_spath_lotag_is_door(0x4000 | 24) == 0);   // 24 is not a door with or without state bits

  // Passability: the two boundaries, each on its own, since one combined check passes with either test alone (a rule
  // dropping the step test would ship green).
  const EkSpathSector low  = { 100000, 80000, 0 };
  const EkSpathSector upOk = { 100000 - EK_SPATH_STEP_UP, 100000 - EK_SPATH_STEP_UP - 20000, 0 };
  const EkSpathSector upNo = { 100000 - EK_SPATH_STEP_UP - 1, 100000 - EK_SPATH_STEP_UP - 1 - 20000, 0 };
  CHECK(ekm_spath_edge_passable(low, upOk, 0) != 0);   // climb EXACTLY 6144: allowed
  CHECK(ekm_spath_edge_passable(low, upNo, 0) == 0);   // climb 6145: refused

  // Level floors (climb 0), so only the gap is under test.
  const EkSpathSector flat   = { 100000, 80000, 0 };
  const EkSpathSector gapOk  = { 100000, 100000 - EK_SPATH_MIN_GAP, 0 };
  const EkSpathSector gapNo  = { 100000, 100000 - EK_SPATH_MIN_GAP + 1, 0 };
  CHECK(ekm_spath_edge_passable(flat, gapOk, 0) != 0);  // gap EXACTLY 10240: allowed
  CHECK(ekm_spath_edge_passable(flat, gapNo, 0) == 0);  // gap 10239: refused

  const EkSpathSector shut = { 100000, 100000, 0 };     // gap 0, floors level
  CHECK(ekm_spath_edge_passable(flat, shut, 1) != 0);   // door: gap waived
  CHECK(ekm_spath_edge_passable(flat, shut, 0) == 0);   // same sector, not a door
  CHECK(ekm_spath_edge_passable(low, upNo, 1) == 0);    // door: the STEP is not waived

  EkSpathSector sec[8];
  unsigned char isDoor[8], isGoal[8];
  for (int i = 0; i < 8; i++) {
    sec[i].floorz = 100000; sec[i].ceilingz = 80000; sec[i].lotag = 0;
    isDoor[i] = 0; isGoal[i] = 0;
  }
  EkSpathEdge chain[8];
  for (int i = 0; i < 7; i++) {
    chain[i].from = i; chain[i].to = i + 1; chain[i].kind = EK_SPATH_EDGE_WALK;
    chain[i].x = 1000 * (i + 1); chain[i].y = 2000 * (i + 1); chain[i].z = 100000;
  }
  EkSpathEdge out[EK_SPATH_MAX_HOPS];
  isGoal[1] = 1;
  CHECK(ekm_spath_search(chain, 7, sec, 8, isDoor, isGoal, 0, out, EK_SPATH_MAX_HOPS) == 1);
  CHECK(out[0].from == 0 && out[0].to == 1);
  isGoal[1] = 0;

  // Standing in the secret already is NOT a zero-hop path: nothing to point at, and a label over a found room is noise.
  isGoal[0] = 1;
  isGoal[3] = 1;
  CHECK(ekm_spath_search(chain, 7, sec, 8, isDoor, isGoal, 0, out, EK_SPATH_MAX_HOPS) == 0);
  isGoal[0] = 0;
  CHECK(ekm_spath_search(chain, 7, sec, 8, isDoor, isGoal, 0, out, EK_SPATH_MAX_HOPS) == 3);
  isGoal[3] = 0;

  // An unreachable goal is 0 hops, never a partial answer.
  isGoal[7] = 1;
  CHECK(ekm_spath_search(chain, 3, sec, 8, isDoor, isGoal, 0, out, EK_SPATH_MAX_HOPS) == 0);
  isGoal[7] = 0;

  // Near-end truncation pinned by WHICH edges come back: a truncated route must show where to go NEXT, not where it
  // ends.
  isGoal[5] = 1;
  CHECK(ekm_spath_search(chain, 7, sec, 8, isDoor, isGoal, 0, out, 2) == 5);
  CHECK(out[0].from == 0 && out[0].to == 1);   // the player's own first hop
  CHECK(out[1].from == 1 && out[1].to == 2);
  isGoal[5] = 0;

  // A teleport edge skips BOTH passability tests: it is the engine's own move with neither step nor gap.
  EkSpathSector tsec[2];
  tsec[0].floorz = 100000; tsec[0].ceilingz = 80000;  tsec[0].lotag = 0;
  tsec[1].floorz = 0;      tsec[1].ceilingz = 0;      tsec[1].lotag = 0;
  unsigned char tDoor[2] = { 0, 0 }, tGoal[2] = { 0, 1 };
  EkSpathEdge tel = { 0, 1, EK_SPATH_EDGE_TELEPORT, 5, 6, 7 };
  CHECK(ekm_spath_search(&tel, 1, tsec, 2, tDoor, tGoal, 0, out, EK_SPATH_MAX_HOPS) == 1);
  EkSpathEdge walk = { 0, 1, EK_SPATH_EDGE_WALK, 5, 6, 7 };
  CHECK(ekm_spath_search(&walk, 1, tsec, 2, tDoor, tGoal, 0, out, EK_SPATH_MAX_HOPS) == 0);

  static EkSpathSector longSec[66];
  static unsigned char longDoor[66], longGoal[66];
  static EkSpathEdge longEdge[65];
  for (int i = 0; i < 66; i++) {
    longSec[i].floorz = 100000; longSec[i].ceilingz = 80000; longSec[i].lotag = 0;
    longDoor[i] = 0; longGoal[i] = 0;
  }
  for (int i = 0; i < 65; i++) {
    longEdge[i].from = i; longEdge[i].to = i + 1; longEdge[i].kind = EK_SPATH_EDGE_WALK;
    longEdge[i].x = i; longEdge[i].y = i; longEdge[i].z = 100000;
  }
  longGoal[65] = 1;
  CHECK(ekm_spath_search(longEdge, 65, longSec, 66, longDoor, longGoal, 0,
                         out, EK_SPATH_MAX_HOPS) == 0);
  longGoal[64] = 1;   // exactly AT the cap: still found
  longGoal[65] = 0;
  CHECK(ekm_spath_search(longEdge, 65, longSec, 66, longDoor, longGoal, 0,
                         out, EK_SPATH_MAX_HOPS) == 64);

  isGoal[1] = 1;
  CHECK(ekm_spath_search(NULL, 7, sec, 8, isDoor, isGoal, 0, out, 8) == 0);
  CHECK(ekm_spath_search(chain, 7, NULL, 8, isDoor, isGoal, 0, out, 8) == 0);
  CHECK(ekm_spath_search(chain, 7, sec, 8, NULL, isGoal, 0, out, 8) == 0);
  CHECK(ekm_spath_search(chain, 7, sec, 8, isDoor, NULL, 0, out, 8) == 0);
  CHECK(ekm_spath_search(chain, 7, sec, 8, isDoor, isGoal, 0, NULL, 8) == 0);
  CHECK(ekm_spath_search(chain, 7, sec, 8, isDoor, isGoal, 0, out, 0) == 0);
  CHECK(ekm_spath_search(chain, 7, sec, 8, isDoor, isGoal, -1, out, 8) == 0);
  CHECK(ekm_spath_search(chain, 7, sec, 8, isDoor, isGoal, 8, out, 8) == 0);
  CHECK(ekm_spath_search(chain, 7, sec, 0, isDoor, isGoal, 0, out, 8) == 0);
  CHECK(ekm_spath_search(chain, 7, sec, EK_SPATH_MAX_SECTORS + 1, isDoor, isGoal,
                         0, out, 8) == 0);
  isGoal[1] = 0;

  CHECK(strcmp(ekm_spath_room_word(1), "room") == 0);
  CHECK(strcmp(ekm_spath_room_word(2), "rooms") == 0);
  CHECK(strcmp(ekm_spath_room_word(0), "rooms") == 0);

  // EkSpathPoint: field order survives a positional init. kind sits last and is the one a swap would silently move.
  EkSpathPoint pt = { 11, 22, 33, EK_SPATH_EDGE_TELEPORT };
  CHECK(pt.x == 11 && pt.y == 22 && pt.z == 33);
  CHECK(pt.kind == EK_SPATH_EDGE_TELEPORT);
}

// The sub-tick's two rules.
static void tb_0147_secret_path_refresh() {
  // The signature distinguishes a swapped pair: the shift is on numsectors so a sum or bare xor (sig(2,1) == sig(1,2))
  // cannot reuse the graph across a map that swapped the two counts.
  CHECK(ekm_spath_graph_sig(2, 1) != ekm_spath_graph_sig(1, 2));
  CHECK(ekm_spath_graph_sig(325, 2810) == ekm_spath_graph_sig(325, 2810));
  CHECK(ekm_spath_graph_sig(325, 2810) != ekm_spath_graph_sig(325, 2811));
  CHECK(ekm_spath_graph_sig(325, 2810) != ekm_spath_graph_sig(326, 2810));

  // Reuse: every refusal on its own line, or a rule testing only the signature would pass.
  const unsigned sig = ekm_spath_graph_sig(325, 2810);
  CHECK(ekm_spath_graph_usable(1, sig, sig, 325, 325) != 0);   // all four agree
  CHECK(ekm_spath_graph_usable(0, sig, sig, 325, 325) == 0);   // never staged
  CHECK(ekm_spath_graph_usable(1, sig, sig + 1, 325, 325) == 0);  // signature moved
  CHECK(ekm_spath_graph_usable(1, sig, sig, 326, 325) == 0);   // sector count grew
  CHECK(ekm_spath_graph_usable(1, sig, sig, 324, 325) == 0);   // sector count shrank

  // The sub-tick numbers multiply to exactly one second: gamestate.cpp sleeps EK_SPATH_SUBTICKS + 1 times per full scan
  // and five other scans on that thread depend on the total being 1000 ms.
  CHECK((EK_SPATH_SUBTICKS + 1) * EK_SPATH_SUBTICK_MS == 1000);
}

// The damage numbers' pure half: which hits become an event, how long one lives across the clock's wrap, how it rises
// and fades, how two hits on one victim stay apart.
static void tb_0148_damage_number_rules() {
  CHECK(ekm_dmg_event_wanted(7, 7, 1, 25) != 0);    // all four terms good
  CHECK(ekm_dmg_event_wanted(-1, 7, 1, 25) == 0);   // no owner: the engine's "nothing hit"
  CHECK(ekm_dmg_event_wanted(8, 7, 1, 25) == 0);    // someone else's damage
  CHECK(ekm_dmg_event_wanted(7, -1, 1, 25) == 0);   // local player sprite unknown
  CHECK(ekm_dmg_event_wanted(7, 7, 0, 25) == 0);    // victim is not a live enemy
  CHECK(ekm_dmg_event_wanted(7, 7, 1, 0) == 0);     // a spark that did nothing
  CHECK(ekm_dmg_event_wanted(7, 7, 1, -3) == 0);    // negative damage is not a hit

  EkDmgRing r;
  ekm_dmg_reset(&r);
  CHECK(r.next == 0);
  CHECK(r.e[0].live == 0);
  for (int i = 0; i < EKM_DMG_MAX; i++) {
    EkDmgEvent ev = { i, 0, 0, 100 + i, 0, i, 1000u, 0 };
    ekm_dmg_push(&r, ev);
  }
  CHECK(r.next == 0);                     // wrapped exactly once
  CHECK(r.e[0].damage == 100);            // the first push is still slot 0
  EkDmgEvent extra = { 999, 0, 0, 777, 0, 999, 1000u, 0 };
  ekm_dmg_push(&r, extra);
  CHECK(r.e[0].damage == 777);            // one past the cap overwrote the OLDEST
  CHECK(r.next == 1);
  CHECK(r.e[1].damage == 101);            // and nothing else moved

  EkDmgEvent w = { 0, 0, 0, 10, 0, 3, 0xFFFFFF00u, 1 };
  CHECK(ekm_dmg_is_live(w, 0xFFFFFF00u) != 0);            // age 0
  CHECK(ekm_dmg_is_live(w, 0x00000010u) != 0);            // age 272 ms, PAST THE WRAP
  CHECK(ekm_dmg_is_live(w, 0x00000300u) == 0);            // age 1024 ms, expired past the wrap
  EkDmgEvent dead = { 0, 0, 0, 10, 0, 3, 1000u, 0 };
  CHECK(ekm_dmg_is_live(dead, 1000u) == 0);               // live flag clear: never live

  CHECK(ekm_dmg_progress(1000u, 1000u) == 0.0);
  CHECK(ekm_dmg_progress(1000u, 1000u + EKM_DMG_LIFE_MS) == 1.0);
  CHECK(ekm_dmg_progress(1000u, 1000u + EKM_DMG_LIFE_MS * 2) == 1.0);   // clamped, not wrapped

  // Ease-out quadratic: starts at 0, ends at one text height, and is at least halfway up by the halfway point, which a
  // LINEAR rise only just reaches and an ease-IN would not.
  CHECK(ekm_dmg_rise_px(0.0, 20) == 0);
  CHECK(ekm_dmg_rise_px(1.0, 20) == 20);
  CHECK(ekm_dmg_rise_px(0.5, 20) == 15);        // 1-(1-0.5)^2 = 0.75
  CHECK(ekm_dmg_rise_px(-1.0, 20) == 0);        // clamped below
  CHECK(ekm_dmg_rise_px(2.0, 20) == 20);        // clamped above

  // Opaque, then linear over the LAST THIRD only.
  CHECK(ekm_dmg_alpha(0.0) == 255);
  CHECK(ekm_dmg_alpha(2.0 / 3.0) == 255);       // still fully opaque AT the knee
  CHECK(ekm_dmg_alpha(1.0) == 0);
  // The knee, bracketed from both sides because no single point pins it.
  CHECK(ekm_dmg_alpha(0.625) == 255);     // 5/8, below the knee: still opaque
  CHECK(ekm_dmg_alpha(0.6875) == 239);    // 11/16, above it: 255*(5/16)*3 = 239.0625
  // Pinned at an exactly representable point, hence 0.875: 7/8 is exact in binary so 255*(1-p)*3 is exactly 95.625 with
  // no wobble, and a rule dropping the `+0.5` from its rounding returns 95 instead of 96. 5.0/6.0 does not work: its
  // true result is 127.49999999999997, so correct and truncating rules both return 127.
  CHECK(ekm_dmg_alpha(0.875) == 96);

  // The colour rule: two distinct values so a rule ignoring `kill` cannot pass.
  CHECK(ekm_dmg_colour(1, 0x11223344u, 0x55667788u) == 0x55667788u);
  CHECK(ekm_dmg_colour(0, 0x11223344u, 0x55667788u) == 0x11223344u);

  // The stack offset counts only LIVE events on the SAME victim pushed BEFORE this one, walking from the oldest slot.
  EkDmgRing s;
  ekm_dmg_reset(&s);
  // a1's t0 is 100 ms earlier than a2's, still live at now=1000u (age 100), so earlier checks reading both as
  // simultaneous are unmoved.
  EkDmgEvent a1 = { 0, 0, 0, 10, 0, 4, 900u, 1 };
  EkDmgEvent a2 = { 0, 0, 0, 20, 0, 4, 1000u, 1 };
  EkDmgEvent b1 = { 0, 0, 0, 30, 0, 9, 1000u, 1 };
  ekm_dmg_push(&s, a1);
  ekm_dmg_push(&s, a2);
  ekm_dmg_push(&s, b1);
  CHECK(ekm_dmg_stack_index(s, 0, 1000u) == 0);   // first hit on victim 4
  CHECK(ekm_dmg_stack_index(s, 1, 1000u) == 1);   // second hit on victim 4 sits one row up
  CHECK(ekm_dmg_stack_index(s, 2, 1000u) == 0);   // different victim: its own first row
  CHECK(ekm_dmg_stack_index(s, 1, 1000u + EKM_DMG_LIFE_MS) == 0);  // all expired: not counted
  CHECK(ekm_dmg_stack_index(s, -1, 1000u) == 0);            // out of range below
  CHECK(ekm_dmg_stack_index(s, EKM_DMG_MAX, 1000u) == 0);   // out of range above
  // The inner liveness term, which the "all expired" check cannot pin.
  CHECK(ekm_dmg_stack_index(s, 1, 1700u) == 0);

  EkDmgEvent f = { 11, 22, 33, 44, 1, 55, 66u, 1 };
  CHECK(f.x == 11 && f.y == 22 && f.z == 33 && f.damage == 44);
  CHECK(f.kill == 1 && f.victim == 55 && f.t0 == 66u && f.live == 1);
}

// The free camera's teleport: every rule, nothing else.
static void tb_0149_freecam_teleport_rules() {
  // When the key may fire, each term on its own line: a combined check passes with any one working and this rule has
  // three.
  CHECK(ekm_tp_fire_rule(1, 1, 0) != 0);    // flying, pressed this tic, panel shut
  CHECK(ekm_tp_fire_rule(0, 1, 0) == 0);    // no camera to aim from
  CHECK(ekm_tp_fire_rule(1, 0, 0) == 0);    // key held, not a fresh press
  CHECK(ekm_tp_fire_rule(1, 1, 1) == 0);    // panel open: the camera's pose is stale

  // The ray direction: vy is the sine of the yaw and vx its cosine (the table a quarter turn on), checked against
  // ekm_sintable itself so the pin says "these agree".
  const EkHitscanDir d = ekm_tp_direction(0, 100);
  CHECK(d.vy == ekm_sintable(0));
  CHECK(d.vx == ekm_sintable(FREECAM_ANG_UNITS / 4));
  const EkHitscanDir d2 = ekm_tp_direction(300, 100);    // not a symmetric angle
  CHECK(d2.vy == ekm_sintable(300));
  CHECK(d2.vx == ekm_sintable(300 + FREECAM_ANG_UNITS / 4));
  CHECK(d2.vx != d2.vy);                                 // a swap would be visible
  CHECK(d.vz == 0);
  CHECK(ekm_tp_direction(0, 90).vz > 0);                 // looking down
  CHECK(ekm_tp_direction(0, 110).vz < 0);                // looking up

  int sx = 0, sy = 0;
  ekm_tp_step_back(10000, 20000, 0, 0, 0, &sx, &sy);
  CHECK(sx == 10000 && sy == 20000);                     // stepBack 0: untouched
  ekm_tp_step_back(10000, 20000, 0, FREECAM_SIN_SCALE, 1, &sx, &sy);
  CHECK(sx == 10000);
  CHECK(sy == 20000 - EK_TP_CLEARANCE);
  ekm_tp_step_back(1, 2, 0, 0, 1, 0, &sy);               // must not fault

  const EkTeleportAim miss = ekm_tp_aim(-1, -1, -1, 0, 0, 0, 0, 0);
  CHECK(miss.ok == 0);
  CHECK(strcmp(miss.reason, EK_TP_NOTHING) == 0);
  const EkTeleportAim floorHit = ekm_tp_aim(3, -1, -1, 500, 600, 700, 0, FREECAM_SIN_SCALE);
  CHECK(floorHit.ok != 0);
  CHECK(floorHit.x == 500 && floorHit.y == 600 && floorHit.z == 700);
  const EkTeleportAim wallHit = ekm_tp_aim(3, 7, -1, 500, 600, 700, 0, FREECAM_SIN_SCALE);
  CHECK(wallHit.ok != 0);
  CHECK(wallHit.y == 600 - EK_TP_CLEARANCE);
  CHECK(wallHit.z == 700);
  const EkTeleportAim sprHit = ekm_tp_aim(3, -1, 9, 500, 600, 700, 0, FREECAM_SIN_SCALE);
  CHECK(sprHit.y == 600 - EK_TP_CLEARANCE);

  const int roomy = EK_TP_PHEIGHT + EK_TP_CEIL_MARGIN + 4096;
  const EkTeleportPlan noSect = ekm_tp_plan(1, 2, 3, -1, 0, 0, EK_TP_PHEIGHT);
  CHECK(noSect.ok == 0);
  CHECK(strcmp(noSect.reason, EK_TP_NO_SECTOR) == 0);
  const int need = EK_TP_PHEIGHT + EK_TP_CEIL_MARGIN;
  const EkTeleportPlan exact = ekm_tp_plan(1, 2, 3, 5, 100000, 100000 - need, EK_TP_PHEIGHT);
  CHECK(exact.ok != 0);
  const EkTeleportPlan tight = ekm_tp_plan(1, 2, 3, 5, 100000, 100000 - need + 1, EK_TP_PHEIGHT);
  CHECK(tight.ok == 0);
  CHECK(strcmp(tight.reason, EK_TP_TIGHT) == 0);
  // A room that fits the eye but not the clearance is still refused, the point of the margin.
  const EkTeleportPlan headroom = ekm_tp_plan(1, 2, 3, 5, 100000, 100000 - EK_TP_PHEIGHT - 1, EK_TP_PHEIGHT);
  CHECK(headroom.ok == 0);
  const EkTeleportPlan good = ekm_tp_plan(11, 22, 33, 5, 100000, 100000 - roomy, EK_TP_PHEIGHT);
  CHECK(good.ok != 0);
  CHECK(good.x == 11 && good.y == 22 && good.sect == 5);
  CHECK(strcmp(good.reason, EK_TP_NONE) == 0);
  CHECK(good.z == 100000 - EK_TP_PHEIGHT);

  CHECK(strcmp(EK_TP_NOTHING, "Nothing to teleport to") == 0);
  CHECK(strcmp(EK_TP_NO_SECTOR, "Camera is outside the map") == 0);
  CHECK(strcmp(EK_TP_TIGHT, "Not enough room to stand") == 0);
  CHECK(strcmp(EK_TP_NO_WRITE, "The player could not be moved") == 0);
  CHECK(strcmp(EK_TP_NO_FLOOR, "Could not read the floor there") == 0);
  CHECK(EK_TP_NONE[0] == '\0');

  // Which outcomes deserve a line on screen: a refusal always, a clean teleport not (arriving is its own feedback).
  CHECK(ekm_tp_has_notice(tight) != 0);
  CHECK(ekm_tp_has_notice(noSect) != 0);
  CHECK(ekm_tp_has_notice(good) == 0);

  // `have` false is a freshly launched process that has said nothing; a zero timestamp must not display a notice nobody
  // set.
  CHECK(ekm_tp_notice_visible(0, 0u, 0u) == 0);
  CHECK(ekm_tp_notice_visible(1, 1000u, 1000u) != 0);
  CHECK(ekm_tp_notice_visible(1, 1000u, 1000u + EK_TP_NOTICE_MS - 1) != 0);
  CHECK(ekm_tp_notice_visible(1, 1000u, 1000u + EK_TP_NOTICE_MS) == 0);
  // Across the tick counter's wrap, the reason the subtraction is unsigned: a signed `now < set` strands a notice for
  // 49 days.
  CHECK(ekm_tp_notice_visible(1, 0xFFFFFF00u, 0x00000010u) != 0);   // age 272 ms
  CHECK(ekm_tp_notice_visible(1, 0xFFFFFF00u, 0x00000900u) == 0);   // age 2560 ms

  const EkMarkBox b = ekm_tp_mark_box(10000, 20000, 30000);
  CHECK(b.c[0].x == 10000 - EK_TP_MARK_HALF && b.c[0].y == 20000 - EK_TP_MARK_HALF);
  CHECK(b.c[1].x == 10000 + EK_TP_MARK_HALF && b.c[1].y == 20000 - EK_TP_MARK_HALF);
  CHECK(b.c[2].x == 10000 + EK_TP_MARK_HALF && b.c[2].y == 20000 + EK_TP_MARK_HALF);
  CHECK(b.c[3].x == 10000 - EK_TP_MARK_HALF && b.c[3].y == 20000 + EK_TP_MARK_HALF);
  CHECK(b.zBottom == 30000);
  CHECK(b.zTop == 30000 - EK_TP_MARK_HEIGHT);
  CHECK(b.zTop < b.zBottom);
}

static void tb_0138_build_to_gl_double() {
  // Each axis alone, so a permutation cannot pass by symmetry.
  const EkGlPointD x1 = ekm_build_to_gld(100.0, 0.0, 0.0);
  CHECK(x1.x == 0.0 && x1.y == 0.0 && x1.z == -100.0);
  const EkGlPointD y1 = ekm_build_to_gld(0.0, 200.0, 0.0);
  CHECK(y1.x == 200.0 && y1.y == 0.0 && y1.z == 0.0);
  const EkGlPointD z1 = ekm_build_to_gld(0.0, 0.0, 1600.0);
  CHECK(z1.x == 0.0 && z1.y == -100.0 && z1.z == 0.0);

  // Agreement with the integer version at whole inputs, including negatives and the sign-carrying vertical.
  const int probe[6] = { -4096, -257, 0, 1, 1024, 32767 };
  int agree = 0;
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++) {
      const EkGlPoint f = ekm_build_to_gl(probe[i], probe[j], probe[(i + j) % 6]);
      const EkGlPointD d = ekm_build_to_gld((double)probe[i], (double)probe[j],
                                            (double)probe[(i + j) % 6]);
      if ((double)f.x == d.x && (double)f.y == d.y && (double)f.z == d.z) agree++;
    }
  CHECK(agree == 36);

  // The reason the double version exists: a fractional span survives instead of snapping to the lattice.
  const EkGlPointD frac = ekm_build_to_gld(10.5, -20.25, 8.0);
  CHECK(frac.z == -10.5 && frac.x == -20.25 && frac.y == -0.5);
}

static void tb_0137_tint_rules() {
  CHECK(ekm_tint_verdict(EK_SW_COMBINATION, "Flip") == EKM_TINT_FLIP);
  CHECK(ekm_tint_verdict(EK_SW_COMBINATION, "Leave") == EKM_TINT_LEAVE);
  CHECK(ekm_tint_verdict(EK_SW_COMBINATION, "Needs blue card") == EKM_TINT_NONE);
  CHECK(ekm_tint_verdict(EK_SW_COMBINATION, NULL) == EKM_TINT_NONE);
  // Not a combination lock at all, even with a line that would otherwise say Flip.
  CHECK(ekm_tint_verdict(EK_SW_TWOSTATE, "Flip") == EKM_TINT_NONE);
  CHECK(ekm_tint_verdict(EK_SW_ACCESS, "Leave") == EKM_TINT_NONE);

  // The colour, and that 0 is not one of the two real colours.
  const unsigned kFlip = 0x6060FF60, kLeave = 0x60FF5050;
  CHECK(ekm_tint_colour(EKM_TINT_FLIP, kFlip, kLeave) == kFlip);
  CHECK(ekm_tint_colour(EKM_TINT_LEAVE, kFlip, kLeave) == kLeave);
  CHECK(ekm_tint_colour(EKM_TINT_NONE, kFlip, kLeave) == 0u);

  int top, bottom;
  ekm_tint_wall_sprite_zrange(1000, 4, 32, 2, 0, &top, &bottom);
  CHECK(top == 456 && bottom == 968);
  ekm_tint_wall_sprite_zrange(1000, 4, 32, 2, 1, &top, &bottom);
  CHECK(top == 712 && bottom == 1224);
  ekm_tint_wall_sprite_zrange(1000, 4, 33, 2, 0, &top, &bottom);
  CHECK(top == 440 && bottom == 968);
  ekm_tint_wall_sprite_zrange(1000, 4, 33, 2, 1, &top, &bottom);
  CHECK(top == 712 && bottom == 1240);

  top = 100; bottom = 200;
  ekm_tint_clamp_to_sector(150, 0, 180, 0, &top, &bottom);
  CHECK(top == 150 && bottom == 180);
  // Ceiling refused on its own parallax bit, only the floor moves.
  top = 100; bottom = 200;
  ekm_tint_clamp_to_sector(150, 1, 180, 0, &top, &bottom);
  CHECK(top == 100 && bottom == 180);
  // Floor refused on its own parallax bit: only the ceiling moves.
  top = 100; bottom = 200;
  ekm_tint_clamp_to_sector(150, 0, 180, 1, &top, &bottom);
  CHECK(top == 150 && bottom == 200);
  top = 100; bottom = 200;
  ekm_tint_clamp_to_sector(150, 1, 180, 1, &top, &bottom);
  CHECK(top == 100 && bottom == 200);
}

static void tb_0150_fov_rules() {
  CHECK(ekm_fov_clamped(EK_FOV_MIN) == EK_FOV_MIN);
  CHECK(ekm_fov_clamped(EK_FOV_MAX) == EK_FOV_MAX);
  CHECK(ekm_fov_clamped(EK_FOV_MIN - 1) == EK_FOV_MIN);
  CHECK(ekm_fov_clamped(EK_FOV_MAX + 1) == EK_FOV_MAX);
  CHECK(ekm_fov_clamped(EK_FOV_DEFAULT) == EK_FOV_DEFAULT);

  EkFovHold h = { 0, 0, 0 };
  int out = -1;
  CHECK(ekm_fov_step(&h, 1, 90, 110, &out) == EK_FOV_ACT_SET);
  CHECK(out == 110);
  CHECK(h.held == 1);
  CHECK(h.saved == 90);
  CHECK(h.forced == 110);
  // A repeated ON edge is the dangerous one: it would save our forced value as the player's and never give his back.
  out = -1;
  CHECK(ekm_fov_step(&h, 1, 110, 120, &out) == EK_FOV_ACT_NONE);
  CHECK(h.saved == 90);
  CHECK(h.forced == 110);
  CHECK(ekm_fov_step(&h, 0, 110, 120, &out) == EK_FOV_ACT_NONE);
  CHECK(h.saved == 90);

  out = -1;
  CHECK(ekm_fov_retarget(&h, 130, &out) == 1);
  CHECK(out == 130);
  CHECK(h.forced == 130);
  CHECK(h.saved == 90);
  CHECK(ekm_fov_retarget(&h, 130, &out) == 0);     // same value: nothing to write
  CHECK(ekm_fov_retarget(&h, EK_FOV_MAX + 50, &out) == 1);
  CHECK(out == EK_FOV_MAX);                        // and it clamps like the slider
  CHECK(h.forced == EK_FOV_MAX);

  out = -1;
  CHECK(ekm_fov_step(&h, -1, EK_FOV_MAX, 130, &out) == EK_FOV_ACT_RESTORE);
  CHECK(out == 90);
  CHECK(h.held == 0);
  CHECK(h.saved == 0);
  CHECK(h.forced == 0);
  out = -1;
  CHECK(ekm_fov_step(&h, -1, 90, 130, &out) == EK_FOV_ACT_NONE);   // nothing held
  // A slider move while the row is off never reaches the cvar.
  CHECK(ekm_fov_retarget(&h, 120, &out) == 0);
  CHECK(ekm_fov_step(0, 1, 90, 110, &out) == EK_FOV_ACT_NONE);
  CHECK(ekm_fov_step(&h, 1, 90, 110, 0) == EK_FOV_ACT_NONE);
  CHECK(ekm_fov_retarget(0, 110, &out) == 0);

  // The stash fingerprint is the FORCED value, not zero.
  CHECK(ekm_fov_stash_valid(1, 110, 110) == 1);   // untouched since: replay it
  CHECK(ekm_fov_stash_valid(1, 110, 90) == 0);    // changed since: refuse
  CHECK(ekm_fov_stash_valid(0, 110, 110) == 0);   // nothing was ever held
  CHECK(ekm_fov_stash_valid(1, EK_FOV_MIN - 1, EK_FOV_MIN - 1) == 0);
  CHECK(ekm_fov_stash_valid(1, EK_FOV_MAX + 1, EK_FOV_MAX + 1) == 0);

  // The third answer, once logged as a failure: on a real crash EDuke32 never reaches CONFIG_WriteSetup, so
  // settings.cfg still holds the player's own fov and the live value comes back as `saved`; nothing was wrong.
  CHECK(ekm_fov_stash_answer(1, 110, 90, 110) == EK_FOV_STASH_REPLAY);
  CHECK(ekm_fov_stash_answer(1, 110, 90, 90) == EK_FOV_STASH_BACK);
  CHECK(ekm_fov_stash_answer(1, 110, 90, 75) == EK_FOV_STASH_NONE);
  CHECK(ekm_fov_stash_answer(0, 110, 90, 90) == EK_FOV_STASH_NONE);
  // A tie goes to the replay (this rule's order): a stash whose saved value was already the forced one satisfies both
  // and the write is the same either way.
  CHECK(ekm_fov_stash_answer(1, 90, 90, 90) == EK_FOV_STASH_REPLAY);
  CHECK(ekm_fov_stash_answer(1, EK_FOV_MIN - 1, 90, 90) == EK_FOV_STASH_BACK);
}

static void tb_0151_perf_zone_rules() {
  // The detail gate is an ordering fact: the per-call zones are LAST in the enum so the gate is one comparison.
  CHECK(ekm_perf_zone_admits(EK_PZ_ENGINE, 0) == 1);
  CHECK(ekm_perf_zone_admits(EK_PZ_OVERLAY_PANEL, 0) == 1);
  CHECK(ekm_perf_zone_admits(EK_PZ_SWEEP_CULL, 0) == 0);
  CHECK(ekm_perf_zone_admits(EK_PZ_SWEEP_DW, 0) == 0);
  CHECK(ekm_perf_zone_admits(EK_PZ_SWEEP_CULL, 1) == 1);
  CHECK(ekm_perf_zone_admits(EK_PZ_SWEEP_DW, 1) == 1);
  CHECK(ekm_perf_zone_admits(-1, 1) == 0);
  CHECK(ekm_perf_zone_admits(EK_PERF_ZONE_COUNT, 1) == 0);

  // Every zone has a name, and the last is not off the end.
  for (int i = 0; i < EK_PERF_ZONE_COUNT; i++) {
    const char* n = ekm_perf_zone_name(i);
    CHECK(n != 0);
    CHECK(n[0] != 0);
    CHECK(n[0] != '?');
  }
  CHECK(ekm_perf_zone_name(-1)[0] == '?');
  CHECK(ekm_perf_zone_name(EK_PERF_ZONE_COUNT)[0] == '?');

  EkPerfZone z = ekm_perf_zone_zero();
  CHECK(z.ticks == 0 && z.calls == 0 && z.items == 0 && z.open == 0);
  ekm_perf_zone_begin(&z, 1000);
  ekm_perf_zone_end(&z, 1300, 7);
  CHECK(z.ticks == 300);
  CHECK(z.calls == 1);
  CHECK(z.items == 7);
  CHECK(z.open == 0);
  ekm_perf_zone_begin(&z, 2000);
  ekm_perf_zone_end(&z, 2100, 3);
  CHECK(z.ticks == 400);
  CHECK(z.calls == 2);
  CHECK(z.items == 10);

  ekm_perf_zone_end(&z, 9999, 5);
  CHECK(z.ticks == 400);
  CHECK(z.calls == 2);
  CHECK(z.items == 10);

  ekm_perf_zone_begin(&z, 3000);
  CHECK(z.open == 1);
  // A begin left open by an SEH unwind loses ONE sample: camera.cpp's sweep runs under __try (camera.cpp:797), where an
  // exception skips the end.
  ekm_perf_zone_begin(&z, 3500);
  CHECK(z.start == 3000);
  ekm_perf_zone_end(&z, 3600, 1);
  CHECK(z.ticks == 1000);          // 400 + 600, measured from the OUTER begin
  CHECK(z.calls == 3);

  EkPerfZone b = ekm_perf_zone_zero();
  ekm_perf_zone_begin(&b, 5000);
  ekm_perf_zone_end(&b, 4000, 2);
  CHECK(b.ticks == 0);
  CHECK(b.calls == 1);
  CHECK(b.items == 2);

  // A null zone is a no-op, not a fault.
  ekm_perf_zone_begin(0, 1);
  ekm_perf_zone_end(0, 2, 1);

  CHECK(ekm_perf_skip_sane(0) == 0);
  CHECK(ekm_perf_skip_sane(EK_PERF_SKIP_ALL) == EK_PERF_SKIP_ALL);
  CHECK(ekm_perf_skip_sane(-1) == EK_PERF_SKIP_ALL);
  CHECK(ekm_perf_skip_sane(999) == (999 & EK_PERF_SKIP_ALL));
  CHECK(ekm_perf_skips(0, EK_PERF_SKIP_SPRITES) == 0);
  CHECK(ekm_perf_skips(EK_PERF_SKIP_SPRITES, EK_PERF_SKIP_SPRITES) == 1);
  CHECK(ekm_perf_skips(EK_PERF_SKIP_SPRITES, EK_PERF_SKIP_WALLS) == 0);
  const int all = EK_PERF_SKIP_ALL;
  CHECK(ekm_perf_skips(all, EK_PERF_SKIP_SPRITES) == 1);
  CHECK(ekm_perf_skips(all, EK_PERF_SKIP_WALLS)   == 1);
  CHECK(ekm_perf_skips(all, EK_PERF_SKIP_UPDATES) == 1);
  CHECK(ekm_perf_skips(all, EK_PERF_SKIP_NOCULL)  == 1);
  // A query for something not one of the four bits is refused, so a typo cannot read as "skip".
  CHECK(ekm_perf_skips(all, 0) == 0);
  CHECK(ekm_perf_skips(all, 3) == 0);
  CHECK(ekm_perf_skips(all, 16) == 0);
}

static void tb_0152_perf_report_rules() {
  CHECK(ekm_perf_ms(10000000, 10000000) == 1000.0);
  CHECK(ekm_perf_ms(0, 10000000) == 0.0);
  CHECK(ekm_perf_ms(1234, 0) == 0.0);
  CHECK(ekm_perf_ms(1234, -1) == 0.0);

  // Nearest rank in permille so the index is exact integer maths: p99 of 256 frames is index 253 and of one frame is
  // that frame.
  CHECK(ekm_perf_rank_index(990, 256) == 253);
  CHECK(ekm_perf_rank_index(990, 1) == 0);
  CHECK(ekm_perf_rank_index(500, 100) == 49);
  CHECK(ekm_perf_rank_index(0, 10) == 0);
  CHECK(ekm_perf_rank_index(1000, 10) == 9);
  CHECK(ekm_perf_rank_index(-5, 10) == 0);
  CHECK(ekm_perf_rank_index(5000, 10) == 9);
  // An empty set has no index, which is not index 0.
  CHECK(ekm_perf_rank_index(500, 0) == -1);

  EkPerfRing r = ekm_perf_ring_init(8);
  CHECK(r.cap == 8 && r.count == 0 && r.next == 0);
  for (int i = 1; i <= 8; i++) ekm_perf_ring_push(&r, (long long)i);
  CHECK(r.count == 8);
  CHECK(r.next == 0);
  CHECK(ekm_perf_ring_max(&r) == 8);
  CHECK(ekm_perf_ring_mean(&r) == 4);   // 36 / 8 == 4 (integer)
  ekm_perf_ring_push(&r, 100);
  ekm_perf_ring_push(&r, 200);
  ekm_perf_ring_push(&r, 300);
  CHECK(r.count == 8);
  CHECK(r.next == 3);
  CHECK(ekm_perf_ring_max(&r) == 300);

  long long scratch[EK_PERF_RING];
  EkPerfRing s = ekm_perf_ring_init(8);
  const long long vals[8] = { 50, 10, 80, 20, 70, 30, 60, 40 };
  for (int i = 0; i < 8; i++) ekm_perf_ring_push(&s, vals[i]);
  CHECK(ekm_perf_ring_select(&s, 0,    scratch, EK_PERF_RING) == 10);
  CHECK(ekm_perf_ring_select(&s, 1000, scratch, EK_PERF_RING) == 80);
  CHECK(ekm_perf_ring_select(&s, 500,  scratch, EK_PERF_RING) == 40);  // index 3
  CHECK(ekm_perf_ring_select(&s, 990,  scratch, EK_PERF_RING) == 80);  // index 7
  // The ring is not disturbed by being read.
  CHECK(s.count == 8);
  CHECK(ekm_perf_ring_max(&s) == 80);

  // The degenerate sets, where a percentile goes wrong.
  EkPerfRing one = ekm_perf_ring_init(8);
  ekm_perf_ring_push(&one, 42);
  CHECK(ekm_perf_ring_select(&one, 990, scratch, EK_PERF_RING) == 42);
  CHECK(ekm_perf_ring_select(&one, 0,   scratch, EK_PERF_RING) == 42);
  CHECK(ekm_perf_ring_mean(&one) == 42);
  EkPerfRing same = ekm_perf_ring_init(8);
  for (int i = 0; i < 8; i++) ekm_perf_ring_push(&same, 5);
  CHECK(ekm_perf_ring_select(&same, 990, scratch, EK_PERF_RING) == 5);
  CHECK(ekm_perf_ring_mean(&same) == 5);
  CHECK(ekm_perf_ring_max(&same) == 5);
  EkPerfRing empty = ekm_perf_ring_init(8);
  CHECK(ekm_perf_ring_select(&empty, 500, scratch, EK_PERF_RING) == 0);
  CHECK(ekm_perf_ring_max(&empty) == 0);
  CHECK(ekm_perf_ring_mean(&empty) == 0);
  CHECK(ekm_perf_ring_select(&s, 500, scratch, 2) == 0);
  CHECK(ekm_perf_ring_select(&s, 500, 0, EK_PERF_RING) == 0);

  // The calibration: 4096 pairs spanning 8192 ticks is 2.0 ticks a pair, carried in tenths so a sub-tick pair does not
  // round to nothing.
  CHECK(ekm_perf_pair_cost_tenths(8192, 4096) == 20);
  CHECK(ekm_perf_pair_cost_tenths(2048, 4096) == 5);   // 0.5 ticks a pair
  CHECK(ekm_perf_pair_cost_tenths(0, 4096) == 0);
  CHECK(ekm_perf_pair_cost_tenths(8192, 0) == 0);
  CHECK(ekm_perf_pair_cost_tenths(-1, 4096) == 0);
  CHECK(ekm_perf_overhead_ticks(20, 1000) == 2000);
  CHECK(ekm_perf_overhead_ticks(5, 1000) == 500);
  CHECK(ekm_perf_overhead_ticks(0, 1000) == 0);
  CHECK(ekm_perf_overhead_ticks(20, 0) == 0);
}

static void tb_0153_gamecfg_rules() {
  char k[32], f[64];
  CHECK(ekm_gamecfg_parse_line("bind \"T\" \"gamefunc_SendMessage\"", k, 32, f, 64) == 1);
  CHECK(strcmp(k, "T") == 0);
  CHECK(strcmp(f, "gamefunc_SendMessage") == 0);
  // Its own abbreviations, which are not Windows names.
  CHECK(ekm_gamecfg_parse_line("bind \"Kpad7\" \"gamefunc_Move_Forward\"", k, 32, f, 64) == 1);
  CHECK(strcmp(k, "Kpad7") == 0);
  // Leading whitespace is accepted; a game wrote the file.
  CHECK(ekm_gamecfg_parse_line("  bind \"A\" \"gamefunc_Strafe_Left\"", k, 32, f, 64) == 1);
  CHECK(strcmp(k, "A") == 0);

  // Everything that is not that shape: a line that is not a bind at all (settings.cfg is full of them).
  CHECK(ekm_gamecfg_parse_line("r_maxfps 0", k, 32, f, 64) == 0);
  CHECK(ekm_gamecfg_parse_line("", k, 32, f, 64) == 0);
  CHECK(ekm_gamecfg_parse_line("bind", k, 32, f, 64) == 0);
  CHECK(ekm_gamecfg_parse_line("bind \"T\"", k, 32, f, 64) == 0);
  CHECK(ekm_gamecfg_parse_line("bind \"T\" \"gamefunc_Send", k, 32, f, 64) == 0);
  CHECK(ekm_gamecfg_parse_line(0, k, 32, f, 64) == 0);
  // A key longer than the buffer is refused, not truncated, which would match the wrong row.
  CHECK(ekm_gamecfg_parse_line("bind \"ABCDE\" \"gamefunc_X\"", k, 4, f, 64) == 0);

  CHECK(ekm_gamecfg_same_key(ekm_gamecfg_key_name(0x54), "T") == 1);        // VK_T
  CHECK(ekm_gamecfg_same_key(ekm_gamecfg_key_name(0x2D), "Insert") == 1);   // VK_INSERT
  CHECK(ekm_gamecfg_same_key(ekm_gamecfg_key_name(0x09), "Tab") == 1);
  CHECK(ekm_gamecfg_same_key(ekm_gamecfg_key_name(0x20), "Space") == 1);
  CHECK(ekm_gamecfg_same_key(ekm_gamecfg_key_name(0x08), "BakSpc") == 1);
  CHECK(ekm_gamecfg_same_key(ekm_gamecfg_key_name(0xA2), "LCtrl") == 1);
  // A VK the game has no name for answers null rather than a wrong name.
  CHECK(ekm_gamecfg_key_name(0x00) == 0);
  CHECK(ekm_gamecfg_key_name(0xFF) == 0);

  // The compare is case-insensitive, which is load-bearing: EDuke32's file is inconsistent about case and a
  // case-sensitive compare would silently disable the warning for whole rows.
  CHECK(ekm_gamecfg_same_key("Insert", "INSERT") == 1);
  CHECK(ekm_gamecfg_same_key("kpad7", "Kpad7") == 1);
  CHECK(ekm_gamecfg_same_key("T", "Y") == 0);
  CHECK(ekm_gamecfg_same_key("T", "") == 0);
  CHECK(ekm_gamecfg_same_key(0, "T") == 0);
  CHECK(ekm_gamecfg_same_key("T", 0) == 0);
}

// entry.h was unpinned in World Tour, so this coverage is new; loops capped at 64 values, no pixel or range sweeps.
static void tb_0154_entry_rules() {
  bool ok = false;

  int v = entry_parse("120", EK_INT, 0, 200, &ok);
  CHECK(ok == true);
  CHECK(v == 120);
  // An out-param is never read in the argument list that fills it.
  v = entry_parse("", EK_INT, 0, 200, &ok);
  CHECK(ok == false);
  v = entry_parse("-", EK_INT, -200, 200, &ok);
  CHECK(ok == false);
  v = entry_parse("12x", EK_INT, 0, 200, &ok);
  CHECK(ok == false);

  // The clamp at both ends, the rule a slider shares.
  v = entry_parse("999", EK_INT, 0, 200, &ok);
  CHECK(ok == true);
  CHECK(v == 200);
  v = entry_parse("-999", EK_INT, 0, 200, &ok);
  CHECK(ok == true);
  CHECK(v == 0);

  // EK_METRES stores WORLD UNITS for text typed in METRES: the shown and stored numbers differ, so the rule is most
  // likely to be got wrong.
  v = entry_parse("1", EK_METRES, 0, 1 << 20, &ok);
  CHECK(ok == true);
  CHECK(v == BUILD_UNITS_PER_METRE);
  v = entry_parse("2", EK_METRES, 0, 1 << 20, &ok);
  CHECK(ok == true);
  CHECK(v == 2 * BUILD_UNITS_PER_METRE);
  // Zero metres is a value, not an empty field.
  v = entry_parse("0", EK_METRES, 0, 1 << 20, &ok);
  CHECK(ok == true);
  CHECK(v == 0);

  EntryState st = entry_begin();
  CHECK(entry_type_char(&st, '4') == 1);
  CHECK(entry_type_char(&st, '2') == 1);
  CHECK(entry_type_char(&st, 'q') == 0);
  // The field never grows past its cap.
  for (int i = 0; i < 64; i++) entry_type_char(&st, '9');
  CHECK((int)strlen(st.text) <= ENTRY_TEXT_CAP);

  char buf[ENTRY_TEXT_CAP + 1];
  entry_format(buf, sizeof(buf), 120, EK_INT);
  v = entry_parse(buf, EK_INT, 0, 200, &ok);
  CHECK(ok == true);
  CHECK(v == 120);
  entry_format(buf, sizeof(buf), 2 * BUILD_UNITS_PER_METRE, EK_METRES);
  v = entry_parse(buf, EK_METRES, 0, 1 << 20, &ok);
  CHECK(ok == true);
  CHECK(v == 2 * BUILD_UNITS_PER_METRE);

  // vk_to_entry_scancode: input.cpp carries Windows VK codes and entry.h names SDL scancodes. vk_to_sdl_scancode
  // (ui/common.h) covers all entry.h uses except Escape, excluded there for a bind row's different meaning.
  int sc2 = -1;
  CHECK(vk_to_entry_scancode('1', &sc2) == true);
  CHECK(sc2 == 30);
  CHECK(vk_to_entry_scancode('0', &sc2) == true);
  CHECK(sc2 == 39);
  CHECK(vk_to_entry_scancode(0x61 /* VK_NUMPAD1 */, &sc2) == true);
  CHECK(sc2 == 89);
  CHECK(vk_to_entry_scancode(0x60 /* VK_NUMPAD0 */, &sc2) == true);
  CHECK(sc2 == 98);
  // Minus and period, main row and keypad: the only punctuation entry.h reads.
  CHECK(vk_to_entry_scancode(0xBD /* VK_OEM_MINUS */, &sc2) == true);
  CHECK(sc2 == 45);   // SDL_SCANCODE_MINUS
  CHECK(vk_to_entry_scancode(0x6D /* VK_SUBTRACT */, &sc2) == true);
  CHECK(sc2 == 86);   // SDL_SCANCODE_KP_MINUS
  CHECK(vk_to_entry_scancode(0xBE /* VK_OEM_PERIOD */, &sc2) == true);
  CHECK(sc2 == 55);
  CHECK(vk_to_entry_scancode(0x6E /* VK_DECIMAL */, &sc2) == true);
  CHECK(sc2 == 99);
  CHECK(vk_to_entry_scancode(0x25 /* VK_LEFT */, &sc2) == true);
  CHECK(sc2 == SDL_SCANCODE_LEFT);
  CHECK(vk_to_entry_scancode(0x27 /* VK_RIGHT */, &sc2) == true);
  CHECK(sc2 == SDL_SCANCODE_RIGHT);
  CHECK(vk_to_entry_scancode(0x24 /* VK_HOME */, &sc2) == true);
  CHECK(sc2 == SDL_SCANCODE_HOME);
  CHECK(vk_to_entry_scancode(0x23 /* VK_END */, &sc2) == true);
  CHECK(sc2 == SDL_SCANCODE_END);
  CHECK(vk_to_entry_scancode(0x2E /* VK_DELETE */, &sc2) == true);
  CHECK(sc2 == SDL_SCANCODE_DELETE);
  CHECK(vk_to_entry_scancode(0x08 /* VK_BACK */, &sc2) == true);
  CHECK(sc2 == SDL_SCANCODE_BACKSPACE);
  CHECK(vk_to_entry_scancode(0x0D /* VK_RETURN */, &sc2) == true);
  CHECK(sc2 == SDL_SCANCODE_RETURN);
  // Ctrl+A/C/V/X's own letter, so the caller's chord check has a scancode to compare.
  CHECK(vk_to_entry_scancode('A', &sc2) == true);
  CHECK(sc2 == 4);
  CHECK(vk_to_entry_scancode('C', &sc2) == true);
  CHECK(sc2 == 6);
  CHECK(vk_to_entry_scancode('V', &sc2) == true);
  CHECK(sc2 == SDL_SCANCODE_V);
  CHECK(vk_to_entry_scancode('X', &sc2) == true);
  CHECK(sc2 == SDL_SCANCODE_X);
  // The one case this wrapper adds: Escape, which vk_to_sdl_scancode refuses (a bind row reads it as "cancel the
  // capture"), so entry.h's cancel needs the wrapper.
  bool escInBase = vk_to_sdl_scancode(0x1B, &sc2);
  CHECK(escInBase == false);
  CHECK(vk_to_entry_scancode(0x1B /* VK_ESCAPE */, &sc2) == true);
  CHECK(sc2 == SDL_SCANCODE_ESCAPE);
  CHECK(vk_to_entry_scancode(0x14 /* VK_CAPITAL */, &sc2) == false);

  // row_commit_pack/_key/_value: menu.cpp's s_rowCommit packs a row key and value into one 64-bit word so an
  // Interlocked64 publish/claim can never separate them; pure and header-only (ui/common.h) for exactly this.
  long long w = row_commit_pack(42, 7);
  CHECK(row_commit_key(w) == 42);
  CHECK(row_commit_value(w) == 7);
  w = row_commit_pack(-1, 0);
  CHECK(row_commit_key(w) == -1);
  CHECK(row_commit_value(w) == 0);
  // A negative VALUE (s_camDx goes negative; a slider's lo can be) must not smear into the row-key half once shifted.
  w = row_commit_pack(3, -100);
  CHECK(row_commit_key(w) == 3);
  CHECK(row_commit_value(w) == -100);
  w = row_commit_pack(-5, -200);
  CHECK(row_commit_key(w) == -5);
  CHECK(row_commit_value(w) == -200);
  w = row_commit_pack(2147483647, -2147483647 - 1);
  CHECK(row_commit_key(w) == 2147483647);
  CHECK(row_commit_value(w) == -2147483647 - 1);
}

// The auto_aim written for the duration of a silent-aim shot is a one-character constant that decides who aims.
static void tb_0156_silent_auto_aim() {
  CHECK_MSG(EK_SILENT_AUTO_AIM == 0,
            "silent aim must leave the engine's auto aim off, or the engine "
            "re-aims the shot it was handed");
}

static void tb_0157_freecam_debug_readout() {
  // Both terms are required; the switch alone is the case that must not draw.
  CHECK(freecam_debug_on(true, 1));
  CHECK_MSG(!freecam_debug_on(false, 1),
            "the row on with the camera down draws nothing");
  CHECK(!freecam_debug_on(true, 0));
  CHECK(!freecam_debug_on(false, 0));
  CHECK(freecam_debug_on(true, 2));

  // Five numbers in the reference's order, horiz rather than pitch as the field is called elsewhere.
  char buf[FREECAM_POSE_BUFSIZE];
  freecam_pose_line(buf, 1024, -2048, 16384, 1536, 140);
  CHECK_MSG(strcmp(buf, "cam x=1024 y=-2048 z=16384 ang=1536 horiz=140") == 0,
            "the readout names horiz, not pitch, and keeps the field order");

  // Negatives survive on every field: Build z grows downward and the camera flies above the map, so a negative z is
  // ordinary.
  freecam_pose_line(buf, -1, -1, -1, -1, -1);
  CHECK(strcmp(buf, "cam x=-1 y=-1 z=-1 ang=-1 horiz=-1") == 0);

  // The buffer holds the widest line: five INT_MINs are 60 characters of digits and signs plus 21 of labels, 81, which
  // must fit FREECAM_POSE_BUFSIZE with the terminator; sprintf_s would trap on overflow.
  freecam_pose_line(buf, -2147483647 - 1, -2147483647 - 1, -2147483647 - 1,
                    -2147483647 - 1, -2147483647 - 1);
  CHECK_MSG((int)strlen(buf) < FREECAM_POSE_BUFSIZE,
            "the widest pose any int can make still fits the buffer");
  CHECK(strncmp(buf, "cam x=-2147483648 ", 18) == 0);
}

static void tb_0158_muzzle_z() {
  const int posZ = -33684, pyoff = 200;   // the E1L4 shot's own numbers

  CHECK(ekm_muzzle_z(posZ, pyoff, EK_WEAPON_PISTOL)   == posZ + pyoff);
  CHECK(ekm_muzzle_z(posZ, pyoff, EK_WEAPON_SHOTGUN)  == posZ + pyoff);
  CHECK(ekm_muzzle_z(posZ, pyoff, EK_WEAPON_CHAINGUN) == posZ + pyoff);
  CHECK(ekm_muzzle_z(posZ, pyoff, EK_WEAPON_GROW)     == posZ + pyoff);

  CHECK(ekm_muzzle_z(posZ, pyoff, EK_WEAPON_RPG) == posZ + pyoff + EK_ZOFFSET6);
  CHECK(ekm_muzzle_z(posZ, pyoff, EK_WEAPON_SHRINKER) ==
        posZ + pyoff + EK_ZOFFSET6);
  CHECK(ekm_muzzle_z(posZ, pyoff, EK_WEAPON_FREEZE) ==
        posZ + pyoff + EK_ZOFFSET6);
  CHECK(ekm_muzzle_z(posZ, pyoff, EK_WEAPON_DEVISTATOR) ==
        posZ + pyoff + EK_ZOFFSET6);

  CHECK_MSG(ekm_muzzle_z(posZ, pyoff, 0) == posZ + pyoff + EK_ZOFFSET6,
            "a failed weapon read keeps the behaviour already in the field");

  // The offset is 1024 and that number is the bug's size: ZOFFSET6 is 4<<8 (actors.h:43).
  CHECK(EK_ZOFFSET6 == 1024);
  CHECK(ekm_muzzle_z(posZ, pyoff, EK_WEAPON_RPG) -
            ekm_muzzle_z(posZ, pyoff, EK_WEAPON_SHOTGUN) ==
        1024);

  // The measured case: the E1L4 turret's body ran from -74832 down to -77872 and the aim point was 2346 above its
  // bottom, at -77178.
  {
    const int aimZ = -77178, bodyTop = -77872, bodyBottom = -74832;
    // Through the function, not around it: asserting on literals plus EK_ZOFFSET6 could not go red for a change to the
    // rule. wasZ is the impact the old muzzle produced, rebuilt from the difference the two arms of ekm_muzzle_z give.
    const int shotgunMuzzle = ekm_muzzle_z(posZ, pyoff, EK_WEAPON_SHOTGUN);
    const int oldMuzzle = ekm_muzzle_z(posZ, pyoff, 0);   // the unread degrade
    const int wasZ = aimZ - (oldMuzzle - shotgunMuzzle);
    CHECK_MSG(wasZ < bodyTop,
              "the old muzzle put the round above the body, every round");
    CHECK(bodyTop - wasZ == 330);
    CHECK_MSG(aimZ > bodyTop && aimZ < bodyBottom,
              "the corrected muzzle lands the same shot inside the body");
  }
}

// The secret-exit level note: World Tour's corner note, carried because the ESP marks a secret exit but never said the
// level HAS one, the useful half since an unfound exit is off screen.
static void tb_0159_secret_exit_note() {
  // The corner is the note's and the pose readout sits below it: World Tour draws both at 8,8 and they overlap.
  CHECK(esp_secret_exit_note_x() == 8 && esp_secret_exit_note_y() == 8);
  CHECK_MSG(FREECAM_POSE_Y > esp_secret_exit_note_y(),
            "the free camera readout clears the note rather than sharing 8,8");
  CHECK_MSG(FREECAM_POSE_Y - esp_secret_exit_note_y() >= ESP_LABEL_GAP,
            "and it clears it by a whole line, not by a pixel");
  CHECK(FREECAM_POSE_X == esp_secret_exit_note_x());

  // The text is sentence case, pinned here rather than in esp.cpp so reworded copy cannot drift from the reference.
  CHECK(strcmp(ESP_SECRET_EXIT_NOTE, "Secret exit in this level") == 0);

  // The draw is not exercised: World Tour's suite can call it because canvas.cpp links into its test host under NO_GL.
}

static void tb_0161_stayput_tiles_are_enemies() {
  CHECK_MSG(ekm_picnum_is_stayput(1821), "OCTABRAINSTAYPUT is an enemy tile");
  CHECK_MSG(ekm_picnum_is_stayput(2631), "BOSS1STAYPUT is an enemy tile");

  // The rest of the set the engine routes through the same arm, from dnames.h.
  const int staySet[12] = { 1682, 1821, 1921, 2001, 2045, 2121,
                            2631, 2711, 2761, 4611, 4741, 5311 };
  for (int i = 0; i < 12; i++)
    CHECK(ekm_picnum_is_stayput(staySet[i]));

  CHECK_MSG(!ekm_picnum_is_stayput(1820), "the plain Octabrain is the engine's job");
  CHECK_MSG(!ekm_picnum_is_stayput(2630), "the plain Battlelord is the engine's job");
  CHECK_MSG(!ekm_picnum_is_stayput(0), "tile 0 is not an enemy");
  CHECK_MSG(!ekm_picnum_is_stayput(60), "the access card is not an enemy");
  CHECK_MSG(!ekm_picnum_is_stayput(1233), "ordinary scenery is not an enemy");

  // The set is exactly twelve wide; a stepped sweep proves nothing else in the tile range answers yes.
  int found = 0;
  for (int pic = 0; pic < 6144; pic++)
    if (ekm_picnum_is_stayput(pic)) found++;
  CHECK_MSG(found == 12, "exactly the twelve variant tiles, no more");
}

static void tb_0155_fontscale_rules() {
  for (int i = 1; i < 6; i++) {
    const long long prev = (long long)FONTPACK_LADDER[i-1].w * FONTPACK_LADDER[i-1].h;
    const long long cur  = (long long)FONTPACK_LADDER[i].w   * FONTPACK_LADDER[i].h;
    CHECK(cur > prev);
  }

  PackBox boxes[4] = { {16,16}, {16,16}, {32,8}, {8,32} };
  PackPos pos[4];
  CHECK(fontpack_shelf(boxes, 4, 512, 256, 1, pos) == true);
  for (int i = 0; i < 4; i++) {
    CHECK(pos[i].x >= 0);
    CHECK(pos[i].y >= 0);
    CHECK(pos[i].x + boxes[i].w <= 512);
    CHECK(pos[i].y + boxes[i].h <= 256);
  }

  // Nothing overlaps anything else: four boxes is six pairs, and a packer stacking glyphs on top of each other still
  // "fits".
  for (int a = 0; a < 4; a++)
    for (int b = a + 1; b < 4; b++) {
      const int ax0 = pos[a].x, ax1 = pos[a].x + boxes[a].w;
      const int ay0 = pos[a].y, ay1 = pos[a].y + boxes[a].h;
      const int bx0 = pos[b].x, bx1 = pos[b].x + boxes[b].w;
      const int by0 = pos[b].y, by1 = pos[b].y + boxes[b].h;
      CHECK(ax1 <= bx0 || bx1 <= ax0 || ay1 <= by0 || by1 <= ay0);
    }

  // A box no rung can hold answers -1, not the last rung.
  PackBox huge[1] = { {4096, 4096} };
  PackPos hp[1];
  PackBox chosen = { 0, 0 };
  CHECK(fontpack_choose(huge, 1, 1, hp, &chosen) == -1);

  // The reservation holds what 200% can ask for: canvas.cpp reserves FONTPACK_LADDER rung 3 (1024x1024), and any packer
  // choice at any allowed scale must fit inside or the atlas write runs off the rect.
  CHECK(FONTPACK_LADDER[3].w == 1024);
  CHECK(FONTPACK_LADDER[3].h == 1024);
  CHECK(FONTPACK_LADDER[2].w <= FONTPACK_LADDER[3].w);
  CHECK(FONTPACK_LADDER[2].h <= FONTPACK_LADDER[3].h);

  // Which scales need a rebuild: 100% IS the baked atlas, so fontgen_build refuses it by design and that refusal is not
  // a failure.
  CHECK(ekm_scale_needs_atlas(UI_SCALE_DEFAULT) == 0);
  CHECK(ekm_scale_needs_atlas(UI_SCALE_MIN) == 1);
  CHECK(ekm_scale_needs_atlas(UI_SCALE_MAX) == 1);
  CHECK(ekm_scale_needs_atlas(125) == 1);
  // Out of range clamps to a scale that is still not the default.
  CHECK(ekm_scale_needs_atlas(0) == 1);
  CHECK(ekm_scale_needs_atlas(10000) == 1);
}

static void tb_0162_host_is_game() {
  CHECK(proxy_host_is_game(L"AB\\Games\\EDuke32\\eduke32.exe"));
  CHECK_MSG(proxy_host_is_game(L"AB\\x\\EDuke32.EXE"), "the name compares without case");
  CHECK(proxy_host_is_game(L"AB/Games/eduke32.exe"));
  CHECK(proxy_host_is_game(L"eduke32.exe"));
  CHECK_MSG(!proxy_host_is_game(L"AB\\Games\\EDuke32\\mapster32.exe"), "the map editor gets a pure forwarder");
  CHECK(!proxy_host_is_game(L"AB\\Games\\EDuke32\\voidsw.exe"));
  CHECK_MSG(!proxy_host_is_game(L"AB\\eduke32.exe\\other.exe"), "only the last path part is the name");
  CHECK(!proxy_host_is_game(L"AB\\x\\eduke32.exe.bak"));
  CHECK(!proxy_host_is_game(L"AB\\x\\eduke32.ex"));
  CHECK(!proxy_host_is_game(L"AB\\x\\xeduke32.exe"));
  CHECK(!proxy_host_is_game(L"AB\\x\\"));
  CHECK(!proxy_host_is_game(L""));
  CHECK(!proxy_host_is_game(NULL));
}

// Reads text the way config_load reads ekmod.ini, one line per call.
struct LineSrc {
  const char* s; int i;
  int operator()() { return s[i] ? (unsigned char)s[i++] : -1; }
};

static void tb_0163_config_line_reader() {
  char line[8];
  LineSrc a = { "a=1\n\nb=2", 0 };
  CHECK(config_read_line([&] { return a(); }, line, 8) == 3 && strcmp(line, "a=1") == 0);
  CHECK(config_read_line([&] { return a(); }, line, 8) == 0 && line[0] == 0);
  CHECK_MSG(config_read_line([&] { return a(); }, line, 8) == 3 && strcmp(line, "b=2") == 0,
            "a last line without a newline is still a line");
  CHECK(config_read_line([&] { return a(); }, line, 8) == CONFIG_LINE_END);

  LineSrc b = { "abcdefg\nabcdefgh\nk=1\nabcdefghij", 0 };
  CHECK_MSG(config_read_line([&] { return b(); }, line, 8) == 7 && strcmp(line, "abcdefg") == 0,
            "cap - 1 bytes fit");
  CHECK_MSG(config_read_line([&] { return b(); }, line, 8) == CONFIG_LINE_OVERLONG, "cap bytes do not");
  CHECK_MSG(config_read_line([&] { return b(); }, line, 8) == 3 && strcmp(line, "k=1") == 0,
            "the line after an over-long one is read whole");
  CHECK_MSG(config_read_line([&] { return b(); }, line, 8) == CONFIG_LINE_OVERLONG,
            "an over-long last line without a newline");
  CHECK(config_read_line([&] { return b(); }, line, 8) == CONFIG_LINE_END);

  LineSrc e = { "", 0 };
  CHECK(config_read_line([&] { return e(); }, line, 8) == CONFIG_LINE_END);

  // The review's case: the tail of a long comment must never be read as a key.
  static char text[700];
  memset(text, 0, sizeof text);
  text[0] = ';';
  memset(text + 1, 'x', 520);
  strcat_s(text, sizeof text, "menukey=0\nuiscale=150\n");
  LineSrc c = { text, 0 };
  char big[CONFIG_LINE_BUFSIZE];
  CHECK(config_read_line([&] { return c(); }, big, CONFIG_LINE_BUFSIZE) == CONFIG_LINE_OVERLONG);
  CHECK(config_read_line([&] { return c(); }, big, CONFIG_LINE_BUFSIZE) == 11 && strcmp(big, "uiscale=150") == 0);
  CHECK(config_read_line([&] { return c(); }, big, CONFIG_LINE_BUFSIZE) == CONFIG_LINE_END);
}

int main() {
  tb_0001_free_sprite_is_plausible();
  tb_0002_box_units();
  tb_0003_yaw_direction();
  tb_0004_pitch_and_horiz();
  tb_0005_angle_decode();
  tb_0006_axis_mapping();
  tb_0007_viewingrange_is_derived();
  tb_0008_projection_and_culling();
  tb_0009_centred_sprites();
  tb_0010_layout_constants();
  tb_0011_validator_thresholds();
  tb_0012_panel_layout();
  tb_0013_ui_scale();
  tb_0014_esp_markers();
  tb_0015_player_struct_rules();
  tb_0016_target_hittable();
  tb_0017_aim_solve();
  tb_0018_aim_lead();
  tb_0019_per_tic_rules();
  tb_0020_hook_rules();
  tb_0021_hook_bytes();
  tb_0022_weapon_accuracy_map();
  tb_0023_weapon_hitscan_map();
  tb_0024_silent_redirect_bound();
  tb_0025_silent_solution_freshness();
  tb_0026_player_sprite_identity();
  tb_0027_tic_interpolation();
  tb_0028_yaw_delta_q16();
  tb_0029_weapon_proj_speed();
  tb_0030_esp_level_loaded();
  tb_0031_mousebind_table();
  tb_0032_esp_draw_distance();
  tb_0033_triggerbot_rules();
  tb_0034_target_liveness();
  tb_0036_enemy_names();
  tb_0037_weapon_speed();
  tb_0038_esp_health_label();
  tb_0039_enemy_max_strength();
  tb_0040_aim_marker();
  tb_0041_trooper_choking();
  tb_0042_actor_layout();
  tb_0043_deferred_damage();
  tb_0044_action_record();
  tb_0045_visible_span();
  tb_0046_span_is_full();
  tb_0047_hitdata_layout();
  tb_0048_shot_ray();
  tb_0049_triggerbot_ray();
  tb_0050_span_sample_hit();
  tb_0051_wall_layout();
  tb_0052_wall_breaks_open();
  tb_0053_origin_sector();
  tb_0054_impact_rules();
  tb_0055_pitch_quantum();
  tb_0056_live_shot_solve();
  tb_0057_shooter_cstat();
  tb_0058_choking_needs_suffering();
  tb_0059_shown_health();
  tb_0060_shot_pitch_bound();
  tb_0061_tracer_rules();
  tb_0062_shot_placeable();
  tb_0063_cand_better();
  tb_0065_health_max_shown();
  tb_0066_acquire_cadence();
  tb_0067_captain_first_tic_health();
  tb_0068_dying_record();
  tb_0069_toilet_trace_window();
  tb_0070_sticky_deferral();
  tb_0071_lock_pair_one_instant();
  tb_0072_level_loading();
  tb_0073_boss_pal_health();
  tb_0074_cvar_write_rules();
  tb_0075_weapon_switch_mask();
  tb_0076_autosave_clock();
  tb_0077_autosave_in_play();
  tb_0078_weapon_pickup_state();
  tb_0079_autosave_name();
  tb_0080_freecam_step();
  tb_0081_freecam_gates();
  tb_0082_freecam_seqlock();
  tb_0083_freecam_player_offsets();
  tb_0084_freecam_frame_gates();
  tb_0086_freecam_sector_pick();
  tb_0087_freecam_player_view();
  tb_0088_freecam_draw_seen_verdict();
  tb_0089_freecam_pose_lerp();
  tb_0092_campos_text();
  tb_0093_freecam_band_pick();
  tb_0094_sector_layout();
  tb_0095_sector_sweep_step();
  tb_0096_prsectors_slot_rules();
  tb_0097_freecam_fog_rules();
  tb_0098_drawingstate_rules();
  tb_0099_freecam_engine_tic();
  tb_0100_dstate_section_span();
  tb_0101_swept_sprites();
  tb_0102_sky_redirect();

  tb_0103_sprite_chains();
  tb_0105_sweep_cull();
  tb_0106_sweep_frustum();
  tb_0107_sprite_authority();
  tb_0108_shot_rules();
  tb_0109_shot_stash();
  tb_0110_shot_statusbar();
  tb_0111_shot_scuba();
  tb_0112_prwalls_slot_rules();
  tb_0113_bots_stand_down();
  tb_0114_input_ownership();
  tb_0115_stash_healed();
  tb_0117_freecam_transport();
  tb_0118_freecam_marker();
  tb_0119_freecam_transport_sector();
  tb_0120_freecam_transport_crossing();
  tb_0121_health_latch_identity();
  tb_0122_freecam_transport_floor();
  tb_0123_freecam_transport_landed();
  tb_0124_freecam_transport_held_on_pad();
  tb_0125_item_table();
  tb_0126_marker_pipeline();
  tb_0127_map_extent_rules();
  tb_0128_secret_marker_rules();
  tb_0129_map_extent_crosscheck();
  tb_0130_map_extent_confirm();
  tb_0131_menu_live_count();
  tb_0132_breakable_rules();
  tb_0133_switch_classify();
  tb_0134_card_mark();
  tb_0135_world_quad_list();
  tb_0136_mask_site_rules();
  tb_0137_tint_rules();
  tb_0138_build_to_gl_double();
  tb_0140_model_bounds_rules();
  tb_0141_tint_box();
  tb_0142_tint_model_origin_z();
  tb_0143_secret_volume_rules();
  tb_0144_secret_trigger_rules();
  tb_0145_secret_trigger_responders();
  tb_0146_secret_path_rules();
  tb_0147_secret_path_refresh();
  tb_0148_damage_number_rules();
  tb_0149_freecam_teleport_rules();
  tb_0150_fov_rules();
  tb_0151_perf_zone_rules();
  tb_0152_perf_report_rules();
  tb_0153_gamecfg_rules();

  tb_0154_entry_rules();
  tb_0155_fontscale_rules();
  tb_0156_silent_auto_aim();
  tb_0157_freecam_debug_readout();
  tb_0158_muzzle_z();
  tb_0159_secret_exit_note();
  tb_0161_stayput_tiles_are_enemies();
  tb_0162_host_is_game();
  tb_0163_config_line_reader();
  printf("%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
