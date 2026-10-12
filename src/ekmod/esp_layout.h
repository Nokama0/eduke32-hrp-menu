#pragma once
#include <stdio.h>
#include "ui/common.h"
#include "ui/canvas.h"
#include "ui/font.h"

// ESP marker layout. The placement arithmetic is World Tour's verbatim so a player switching games reads the same
// picture. Everything widens to long long and clamps back: a projected coordinate can saturate int.

const int ESP_BRACKET_T   = 2;
const int ESP_BRACKET_DIV = 3;
const int ESP_BRACKET_MIN = 3;

const int ESP_BAR_W   = 3;
const int ESP_BAR_GAP = 3;

const int ESP_LABEL_GAP = 17;
const int ESP_DIST_GAP  = 2;

// Cap is (dim-1)/2, not dim/2: two opposing arms of dim/2 on an even edge would meet and merge markers into a grid. The
// cap overrides the minimum on tiny boxes.
inline int esp_bracket_arm(int dim) {
  if (dim <= 0) return 0;
  int a = dim / ESP_BRACKET_DIV;
  if (a < ESP_BRACKET_MIN) a = ESP_BRACKET_MIN;
  const int cap = (dim - 1) / 2;
  if (a > cap) a = cap;
  return a < 0 ? 0 : a;
}

inline int esp_bracket_thickness(int armX, int armY) {
  int t = ESP_BRACKET_T;
  if (t > armX) t = armX;
  if (t > armY) t = armY;
  return t < 0 ? 0 : t;
}

inline int esp_label_x(int boxX, int boxW, int textW) {
  return clamp_ll_to_int((long long)boxX + (long long)boxW / 2 - (long long)textW / 2);
}

inline int esp_name_label_y(int boxY) {
  return clamp_ll_to_int((long long)boxY - ESP_LABEL_GAP);
}

inline int esp_dist_label_y(int boxY, int boxH) {
  return clamp_ll_to_int((long long)boxY + (long long)boxH + ESP_DIST_GAP);
}

inline int esp_bar_x(int boxX) {
  return clamp_ll_to_int((long long)boxX - ESP_BAR_GAP - ESP_BAR_W);
}

// Name in FONT_SMALL and number in FONT_MONO (digits stay aligned), centred as a pair; WT's shape
// (esp_layout.h:142-156).
const int ESP_LABEL_RUN_GAP = 4;
struct EspLabelRuns { int nameX, numX, totalW; };
inline EspLabelRuns esp_label_runs(int boxX, int boxW, int nameW, int numW) {
  EspLabelRuns r = { 0, 0, 0 };
  const int gap = (nameW > 0 && numW > 0) ? ESP_LABEL_RUN_GAP : 0;
  r.totalW = clamp_ll_to_int((long long)nameW + gap + numW);
  r.nameX = esp_label_x(boxX, boxW, r.totalW);
  r.numX = clamp_ll_to_int((long long)r.nameX + nameW + gap);
  return r;
}

inline int esp_label_mono_y(int nameY, int smallH, int monoH) {
  return clamp_ll_to_int((long long)nameY + smallH - monoH);
}

// One frame's marks are collected before drawing: without a list there is no painter order and no way to choose what to
// keep over the cap.
struct EkScreenPt  { int x, y, dist2d; };
struct EkScreenBox { int x, y, w, h; };

// `index` is kind-specific: slot in the frame's targets[] for ENEMY; SPRITE index for ITEM, CRACK, SWITCH_SPRITE;
// SECTOR for SECRET; WALL for BREAKABLE, SWITCH_WALL. A draw must never index another kind's array. Switches are split
// by kind because WT's offset decode would sit in esp.cpp, which test_host cannot link.
struct EkMarker { int kind, index, dist2d, seq; EkScreenPt p; EkScreenBox box; };

// Gather must exceed draw, or ekm_keep_nearest's `keep >= count` guard is always true and the cap becomes first-come
// (lowest sprite index).
const int EK_MARKER_COLLECT_MAX = 1024;
const int EK_MARKER_MAX         = 256;
enum { EK_MARKER_ENEMY = 0, EK_MARKER_ITEM = 1, EK_MARKER_SECRET = 2,
       EK_MARKER_CRACK = 3, EK_MARKER_BREAKABLE = 4,
       EK_MARKER_SWITCH_SPRITE = 5, EK_MARKER_SWITCH_WALL = 6 };

// Keep the nearest, then order: far-to-near is the painter order, so capping that order would drop the near markers.
inline void ekm_keep_nearest(EkMarker* m, int count, int keep) {
  if (!m || count <= 0 || keep <= 0 || keep >= count) return;
  for (int i = 0; i < keep; i++) {
    int best = i;
    for (int j = i + 1; j < count; j++)
      if (m[j].dist2d < m[best].dist2d) best = j;
    if (best != i) { const EkMarker t = m[i]; m[i] = m[best]; m[best] = t; }
  }
}

// Takes the caller's count as given: a second clamp here silently left the tail unordered.
inline void ekm_sort_far_to_near(EkMarker* m, int count) {
  if (!m || count < 2) return;
  for (int i = 1; i < count; i++) {
    const EkMarker key = m[i];
    int j = i - 1;
    while (j >= 0 && m[j].dist2d < key.dist2d) { m[j + 1] = m[j]; j--; }
    m[j + 1] = key;
  }
}

// Aim calibration marker (WT esp_layout.h:256-262, 372-406): a hollow square at the raw body point, a filled one at the
// led point. They coincide for hitscan and separate for a projectile; that gap is the instrument.
const int AIM_MARKER_HALF = 4;
const int AIM_MARKER_T    = 1;
const char* const AIM_MARKER_LABEL = "Aim";

inline int aim_marker_span() { return AIM_MARKER_HALF * 2 + 1; }

inline int aim_marker_x(int cx) { return clamp_ll_to_int((long long)cx - AIM_MARKER_HALF); }
inline int aim_marker_y(int cy) { return clamp_ll_to_int((long long)cy - AIM_MARKER_HALF); }

inline int aim_marker_edge_far(int c) {
  return clamp_ll_to_int((long long)c + AIM_MARKER_HALF + 1 - AIM_MARKER_T);
}

inline int aim_marker_label_y(int cy) {
  return clamp_ll_to_int((long long)cy - AIM_MARKER_HALF - ESP_LABEL_GAP);
}

// Needs its own switch AND a live assist, or aimdebug alone would mark a target nothing aims at.
inline bool aim_marker_on(int aimon, int silenton, int aimdebug) {
  return (aimon != 0 || silenton != 0) && aimdebug != 0;
}

// Free camera's own-body diamond labelled "You", the only way to find the body once the camera detaches. Megaton
// esp_layout.h:376-419, WT :407-450.
const int PLAYER_MARKER_HALF = 6;

const char* const PLAYER_MARKER_LABEL = "You";

inline int player_marker_rows() { return PLAYER_MARKER_HALF * 2 + 1; }

// Rows outside the diamond return 0, not negative.
inline int player_marker_row_halfwidth(int row) {
  const int d = row - PLAYER_MARKER_HALF;
  const int w = PLAYER_MARKER_HALF - (d < 0 ? -d : d);
  return w < 0 ? 0 : w;
}

inline int player_marker_row_x(int cx, int row) {
  return clamp_ll_to_int((long long)cx - player_marker_row_halfwidth(row));
}
inline int player_marker_row_y(int cy, int row) {
  return clamp_ll_to_int((long long)cy - PLAYER_MARKER_HALF + row);
}
inline int player_marker_row_w(int row) { return player_marker_row_halfwidth(row) * 2 + 1; }

inline int player_marker_label_y(int cy) {
  return clamp_ll_to_int((long long)cy - PLAYER_MARKER_HALF - ESP_LABEL_GAP);
}

// First term must be the live camera state, not the toggle, or the marker sits on the body the camera still looks out
// of.
inline bool freecam_marker_on(bool freecamActive, int freecamshowplayer) {
  return freecamActive && freecamshowplayer != 0;
}

inline bool freecam_debug_on(bool freecamActive, int freecamdebug) {
  return freecamActive && freecamdebug != 0;
}

// Five numbers on one mono line while the camera flies (WT camera.cpp:1211), labelled horiz= to match FreecamPose. The
// secret-exit note owns the top-left corner; the readout sits one ESP_LABEL_GAP below (WT overlaps both at 8,8).
const int ESP_SECRET_EXIT_NOTE_MARGIN = 8;
const char* const ESP_SECRET_EXIT_NOTE = "Secret exit in this level";
inline int esp_secret_exit_note_x() { return ESP_SECRET_EXIT_NOTE_MARGIN; }
inline int esp_secret_exit_note_y() { return ESP_SECRET_EXIT_NOTE_MARGIN; }

// Takes the resolved flag. test_host does not link canvas_text, so the pin covers geometry and text, not the draw.
inline void esp_draw_secret_exit_note(bool show) {
  if (!show) return;
  canvas_text(esp_secret_exit_note_x(), esp_secret_exit_note_y(),
              ESP_SECRET_EXIT_NOTE, canvas_esp_secret_colour(), FONT_SMALL);
}

const int FREECAM_POSE_X = ESP_SECRET_EXIT_NOTE_MARGIN;
const int FREECAM_POSE_Y = ESP_SECRET_EXIT_NOTE_MARGIN + ESP_LABEL_GAP;
const int FREECAM_POSE_BUFSIZE = 96;
inline void freecam_pose_line(char* buf, int x, int y, int z, int ang,
                              int horiz) {
  sprintf_s(buf, FREECAM_POSE_BUFSIZE, "cam x=%d y=%d z=%d ang=%d horiz=%d", x,
            y, z, ang, horiz);
}

// Health number run. A state label replaces the number (no source yet; unreachable). max 0 means unknown: "%d", never
// "%d/0". hp <= 0 draws no number.
const int ESP_HEALTH_NUM_BUFSIZE = 32;
inline bool esp_health_number(char* numBuf, int hp, int maxhp, const char* stateLabel) {
  if (!stateLabel && hp <= 0) return false;
  if (stateLabel)     sprintf_s(numBuf, ESP_HEALTH_NUM_BUFSIZE, "%s", stateLabel);
  else if (maxhp > 0) sprintf_s(numBuf, ESP_HEALTH_NUM_BUFSIZE, "%d/%d", hp, maxhp);
  else                sprintf_s(numBuf, ESP_HEALTH_NUM_BUFSIZE, "%d", hp);
  return true;
}

const int ESP_SECRET_ARM = 5;
const int ESP_SECRET_T   = 2;

const char* const ESP_SECRET_LABEL = "Secret";

inline int esp_secret_bar_span() { return ESP_SECRET_ARM * 2 + 1; }

inline int esp_secret_hbar_x(int cx) { return clamp_ll_to_int((long long)cx - ESP_SECRET_ARM); }
inline int esp_secret_hbar_y(int cy) { return clamp_ll_to_int((long long)cy - ESP_SECRET_T / 2); }
inline int esp_secret_vbar_x(int cx) { return clamp_ll_to_int((long long)cx - ESP_SECRET_T / 2); }
inline int esp_secret_vbar_y(int cy) { return clamp_ll_to_int((long long)cy - ESP_SECRET_ARM); }

inline int esp_secret_name_label_y(int cy) {
  return clamp_ll_to_int((long long)cy - ESP_SECRET_ARM - ESP_LABEL_GAP);
}
inline int esp_secret_dist_label_y(int cy) {
  return clamp_ll_to_int((long long)cy + ESP_SECRET_ARM + 1 + ESP_DIST_GAP);
}

const int ESP_CARD_W     = 14;
const int ESP_CARD_H     = 10;
const int ESP_CARD_R     = 2;
const int ESP_CARD_T     = 1;
const int ESP_CARD_NOTCH = 3;

inline int esp_card_x(int cx) { return clamp_ll_to_int((long long)cx - ESP_CARD_W / 2); }
inline int esp_card_y(int cy) { return clamp_ll_to_int((long long)cy - ESP_CARD_H / 2); }

inline int esp_card_notch_x(int cx) { return clamp_ll_to_int((long long)cx - ESP_CARD_NOTCH / 2); }
inline int esp_card_notch_y(int cy) { return clamp_ll_to_int((long long)cy - ESP_CARD_NOTCH / 2); }

inline int esp_card_name_label_y(int cy) {
  return clamp_ll_to_int((long long)cy - ESP_CARD_H / 2 - ESP_LABEL_GAP);
}
inline int esp_card_dist_label_y(int cy) {
  return clamp_ll_to_int((long long)cy + ESP_CARD_H / 2 + ESP_DIST_GAP);
}
