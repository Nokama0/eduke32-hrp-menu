// The file half: settings.cfg read once, parsed through gamecfg.h's rules.
#include <windows.h>
#include <stdio.h>
#include "gamecfg.h"
#include "log.h"

// Larger files are truncated, not refused: that costs a missed warning, never a wrong one.
static const int GAMECFG_MAX = 16384;
static const int GAMECFG_ROWS = 128;

struct GamecfgBind { char key[32]; char fn[64]; };
static GamecfgBind s_binds[GAMECFG_ROWS];
static int s_count = 0;
static int s_loaded = 0;

void gamecfg_load(void) {
  if (s_loaded) return;
  s_loaded = 1;
  s_count = 0;
  char path[MAX_PATH];
  const DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
  if (!n || n >= MAX_PATH) { ek_log("gamecfg: no module path; no clash warnings"); return; }
  char* slash = strrchr(path, '\\');
  if (!slash) { ek_log("gamecfg: no directory in module path"); return; }
  slash[1] = 0;
  strncat_s(path, MAX_PATH, "settings.cfg", _TRUNCATE);

  FILE* f = 0;
  if (fopen_s(&f, path, "rb") != 0 || !f) {
    ek_log("gamecfg: %s not readable; no clash warnings", path);
    return;
  }
  static char buf[GAMECFG_MAX];
  const size_t got = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[got] = 0;

  char* ctx = 0;
  for (char* line = strtok_s(buf, "\r\n", &ctx); line; line = strtok_s(0, "\r\n", &ctx)) {
    if (s_count >= GAMECFG_ROWS) break;
    char k[32], fn[64];
    const int ok = ekm_gamecfg_parse_line(line, k, (int)sizeof(k), fn, (int)sizeof(fn));
    if (!ok) continue;
    strncpy_s(s_binds[s_count].key, sizeof(s_binds[s_count].key), k, _TRUNCATE);
    strncpy_s(s_binds[s_count].fn, sizeof(s_binds[s_count].fn), fn, _TRUNCATE);
    s_count++;
  }
  // EDuke32 rewrites settings.cfg at exit, so rebinds in the game's menu show stale warnings until restart.
  ek_log("gamecfg: %d bindings read from %s at panel open", s_count, path);
}

const char* gamecfg_function_for(int vk) {
  const char* name = ekm_gamecfg_key_name(vk);
  if (!name) return 0;
  for (int i = 0; i < s_count; i++)
    if (ekm_gamecfg_same_key(s_binds[i].key, name)) return s_binds[i].fn;
  return 0;
}
