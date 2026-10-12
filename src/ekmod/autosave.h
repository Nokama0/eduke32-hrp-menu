#pragma once
#include <string.h>
#include <time.h>
#include "playerstate.h"

// Rows 1-3 plumb EDuke32's own cvars (osdcmds.cpp:1601-1603); the timed save (from World Tour) is this file.

const int AUTOSAVE_MINS_MIN = 0, AUTOSAVE_MINS_MAX = 30;
const int AUTOSAVE_MINS_DEFAULT = AUTOSAVE_MINS_MIN;
const unsigned AUTOSAVE_MS_PER_MIN = 60000u;

inline int autosave_mins_clamped(int v) {
  if (v < AUTOSAVE_MINS_MIN) return AUTOSAVE_MINS_MIN;
  if (v > AUTOSAVE_MINS_MAX) return AUTOSAVE_MINS_MAX;
  return v;
}

// int flags, never bool: MSVC 19.50 /O2 loses a bool member of a struct returned by inline.
struct AutosaveState { unsigned lastMs; int armed; };

// Is a timed save due? A level build (`levelLoading`, ekm_level_loading) restarts the interval.
inline int autosave_step(AutosaveState* s, int intervalMins, unsigned nowMs,
                         int levelLoading, int inPlay) {
  if (!s) return 0;
  if (!s->armed || levelLoading) {
    s->armed = 1;
    s->lastMs = nowMs;
    return 0;
  }
  const int mins = autosave_mins_clamped(intervalMins);
  if (mins <= 0) { s->lastMs = nowMs; return 0; }
  if (!inPlay) return 0;
  if ((unsigned)(nowMs - s->lastMs) < (unsigned)mins * AUTOSAVE_MS_PER_MIN) return 0;
  s->lastMs = nowMs;
  return 1;
}

// May a save be asked for? The engine saves only while gm & MODE_GAME and refuses a dead player (game.cpp:5040-5055); a
// failed read is not a yes.
inline int autosave_in_play(int gmOk, int gm, int healthOk, int health, int panelOpen) {
  if (!gmOk || !healthOk) return 0;
  if (!esp_level_loaded(gm, true)) return 0;
  if (health <= 0) return 0;
  if (panelOpen) return 0;
  return 1;
}

// g_lastautosave is a savebrief_t { char name[32]; char path[256]; uint8_t isExt; } (savegame.h:37, 88-90), confirmed
// in the image.
const int EK_SAVEBRIEF_NAME_CAP = 32;
const int EK_SAVEBRIEF_PATH_OFF = EK_SAVEBRIEF_NAME_CAP;

// The name is our job: only CON_SAVE/CON_SAVENN fill it (gameexec.cpp:5339-5344), absent from this GRP. An empty name
// makes the save isUnreadable (savegame.cpp:189-195): hidden and deleted by G_DeleteOldSaves (:817-826).
inline int autosave_name_ok(const char* name) {
  if (!name || name[0] == 0) return 0;
  for (int i = 0; i < EK_SAVEBRIEF_NAME_CAP; i++)
    if (name[i] == 0) return 1;
  return 0;
}

// The engine's autosave name, gameexec.cpp:5344 byte for byte; refuses a short buffer rather than truncating.
inline int autosave_name_from_tm(char* out, int cap, const struct tm* t) {
  if (!out || cap < EK_SAVEBRIEF_NAME_CAP) return 0;
  size_t n = 0;
  if (t) n = strftime(out, (size_t)EK_SAVEBRIEF_NAME_CAP, "%d %b %Y %I:%M%p", t);
  if (n == 0) memcpy(out, "Autosave", 9);
  out[EK_SAVEBRIEF_NAME_CAP - 1] = 0;
  return autosave_name_ok(out);
}

void autosave_frame(void);

// False until the save request is located (permanently if the scan refused).
bool autosave_ready(void);
