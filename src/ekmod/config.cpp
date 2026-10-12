// ekmod.ini, beside eduke32.exe. Written key by key, never rewritten whole: a rewrite would eat keys a NEWER build
// wrote, so a downgrade would lose settings.

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "config.h"
#include "esp.h"   // ESP_DRAWDIST_DEFAULT
#include "tbot.h"  // TBOT_WHEN_CROSSHAIR, TBOT_DELAY_MS_DEFAULT
#include "autosave.h"  // AUTOSAVE_MINS_DEFAULT
#include "weaponspeed.h"  // FIRERATE_DEFAULT
#include "freecam.h"      // FREECAM_SPEED_DEFAULT
#include "fov.h"          // EK_FOV_DEFAULT

Config g_cfg;
int g_cfgDirty = 0;

// A copy of what was loaded, so config_write_changed can write only what moved.
static Config s_onDisk;
static char s_path[MAX_PATH];

static bool ini_path(void) {
  if (s_path[0]) return true;
  const DWORD n = GetModuleFileNameA(NULL, s_path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) { s_path[0] = 0; return false; }
  char* slash = strrchr(s_path, '\\');
  if (!slash) { s_path[0] = 0; return false; }
  slash[1] = 0;
  if (strlen(s_path) + 11 >= MAX_PATH) { s_path[0] = 0; return false; }
  strcat_s(s_path, MAX_PATH, "ekmod.ini");
  return true;
}

struct Field { const char* key; int Config::* member; };
static const Field kFields[] = {
  { "menukey",     &Config::menukey     },
  { "uiscale",     &Config::uiscale     },
  { "panelx",      &Config::panelx      },
  { "panely",      &Config::panely      },
  { "panelw",      &Config::panelw      },
  { "panelh",      &Config::panelh      },
  { "paneltab",    &Config::paneltab    },
  { "animations",  &Config::animations  },
  { "esp",         &Config::esp         },
  { "espboxes",    &Config::espboxes    },
  { "esphealthbar",&Config::esphealthbar},
  { "espname",     &Config::espname     },
  { "espdistance", &Config::espdistance },
  { "espitems",    &Config::espitems    },
  { "espsecrets",  &Config::espsecrets  },
  { "secretvolume",   &Config::secretvolume   },
  { "secrettriggers", &Config::secrettriggers },
  { "secretpath",     &Config::secretpath     },
  { "dmgnumbers",     &Config::dmgnumbers     },
  { "espexplodables", &Config::espexplodables },
  { "espbreakables",  &Config::espbreakables  },
  { "espswitches",    &Config::espswitches    },
  { "esptint",        &Config::esptint        },
  { "espdrawdist", &Config::espdrawdist },
  { "espnolimit",  &Config::espnolimit  },
  { "aimon",       &Config::aimon       },
  { "aimhold",     &Config::aimhold     },
  { "aimsmooth",   &Config::aimsmooth   },
  { "aimtarget",   &Config::aimtarget   },
  { "aimpoint",    &Config::aimpoint    },
  { "aimcone",     &Config::aimcone     },
  { "aimrange",    &Config::aimrange    },
  { "aimlead",     &Config::aimlead     },
  { "aimleadscale",&Config::aimleadscale},
  { "aimdebug",    &Config::aimdebug    },
  { "accpistol",   &Config::accpistol   },
  { "accchaingun", &Config::accchaingun },
  { "accshotgun",  &Config::accshotgun  },
  { "accexpander", &Config::accexpander },
  { "firerate",    &Config::firerate    },
  { "switchspeed", &Config::switchspeed },
  { "silenton",    &Config::silenton    },
  { "silentmaxangle", &Config::silentmaxangle },
  { "tboton",      &Config::tboton      },
  { "tbotwhen",    &Config::tbotwhen    },
  { "tbotdelay",   &Config::tbotdelay   },
  { "mouse4action",&Config::mouse4action},
  { "mouse5action",&Config::mouse5action},
  { "autosavemins",&Config::autosavemins},
  { "fovon",       &Config::fovon       },
  { "fov",         &Config::fov         },
  { "fovstashheld",   &Config::fovstashheld   },
  { "fovstashsaved",  &Config::fovstashsaved  },
  { "fovstashforced", &Config::fovstashforced },
  { "freecamon",   &Config::freecamon   },
  { "freecamspeed",&Config::freecamspeed},
  { "freecamwholemap", &Config::freecamwholemap },
  { "freecamfog",  &Config::freecamfog   },
  { "freecamshot", &Config::freecamshot  },
  { "freecamtransport", &Config::freecamtransport },
  { "freecamshowplayer", &Config::freecamshowplayer },
  { "freecamdebug", &Config::freecamdebug },
  { "freecamteleportmarker", &Config::freecamteleportmarker },
  { "shotstashheld",      &Config::shotstashheld      },
  { "shotstashmask",      &Config::shotstashmask      },
  { "shotstashcrosshair", &Config::shotstashcrosshair },
  { "shotstashweapon",    &Config::shotstashweapon    },
  { "shotstashstats",     &Config::shotstashstats     },
  { "debuglog",    &Config::debuglog    },
  { "tracers",     &Config::tracers     },
  { "perfzones",   &Config::perfzones   },
  { "perfdetail2", &Config::perfdetail2 },
  { "perftable",   &Config::perftable   },
  { "perfskip",    &Config::perfskip    },
  { "bindaim",     &Config::bindaim     },
  { "bindcamera",  &Config::bindcamera  },
  { "bindteleport",&Config::bindteleport},
  { "bindcamcopy", &Config::bindcamcopy  },
  { "bindcampaste",&Config::bindcampaste },
};
static const int kFieldCount = (int)(sizeof(kFields) / sizeof(kFields[0]));

void config_defaults(void) {
  g_cfg.menukey    = 0x2D;   // VK_INSERT, the same key on all three games
  g_cfg.uiscale    = 100;
  g_cfg.panelx = -1; g_cfg.panely = -1;   // unset: 0,0 is a real corner
  g_cfg.panelw = 0;  g_cfg.panelh = 0;    // unset: no real panel is zero-sized
  g_cfg.paneltab   = 0;
  g_cfg.animations = 1;

  g_cfg.esp          = 0;
  g_cfg.espboxes     = 1;
  g_cfg.esphealthbar = 1;
  g_cfg.espname      = 1;
  g_cfg.espdistance  = 0;
  g_cfg.espitems     = 0;
  g_cfg.espsecrets   = 0;
  g_cfg.secretvolume = 0;
  g_cfg.secrettriggers = 0;
  g_cfg.secretpath     = 0;
  g_cfg.dmgnumbers     = 0;
  g_cfg.espexplodables = 0;
  g_cfg.espbreakables  = 0;
  g_cfg.espswitches    = 0;
  g_cfg.esptint        = 0;
  // The maximum on purpose (esp.h): out of the box the cull is inert, which is
  // this feature's version of "off".
  g_cfg.espdrawdist  = ESP_DRAWDIST_DEFAULT;
  g_cfg.espnolimit   = 0;

  // The aimbot off, its shape the one a player switching it on would want.
  g_cfg.aimon = 0; g_cfg.aimhold = 1; g_cfg.aimsmooth = 0;
  g_cfg.aimtarget = 0; g_cfg.aimpoint = 0;
  g_cfg.aimcone = 30; g_cfg.aimrange = 32768;
  g_cfg.aimlead = 1; g_cfg.aimleadscale = 70;
  g_cfg.aimdebug = 0;   // FEATURES DEFAULT OFF: no marker until asked for

  g_cfg.accpistol = 0; g_cfg.accchaingun = 0;
  g_cfg.accshotgun = 0; g_cfg.accexpander = 0;

  // FEATURES DEFAULT OFF, and for these two rows "off" is the identity value rather than a zero.
  g_cfg.firerate = FIRERATE_DEFAULT;
  g_cfg.switchspeed = FIRERATE_DEFAULT;

  // FEATURES DEFAULT OFF. 45 degrees is the redirect the row starts at -- wide
  // enough to cover anything the player could plausibly have meant to shoot,
  // narrow enough that a shot at a wall stays a shot at a wall.
  g_cfg.silenton = 0; g_cfg.silentmaxangle = 45;

  // FEATURES DEFAULT OFF, and this one most of all: it is the only feature in the mod that presses a key.
  g_cfg.tboton = 0;
  g_cfg.tbotwhen = TBOT_WHEN_CROSSHAIR;
  g_cfg.tbotdelay = TBOT_DELAY_MS_DEFAULT;

  g_cfg.mouse4action = 0;
  g_cfg.mouse5action = 0;

  // FEATURES DEFAULT OFF, and this one most literally: at 0 autosave.cpp's
  // rule returns "not due" on every frame and no save is ever asked for.
  g_cfg.autosavemins = AUTOSAVE_MINS_DEFAULT;

  g_cfg.fovon = 0;
  g_cfg.fov = EK_FOV_DEFAULT;
  // The crash stash starts empty: a held flag only arrives by being READ off disk (fov.h).
  g_cfg.fovstashheld = 0;
  g_cfg.fovstashsaved = 0;
  g_cfg.fovstashforced = 0;

  g_cfg.freecamon = 0;
  g_cfg.freecamspeed = FREECAM_SPEED_DEFAULT;
  // AND THE WHOLE-MAP SWEEP IS ITS OWN ROW, OFF EVEN WHILE THE CAMERA FLIES: it draws every sector twice.
  g_cfg.freecamwholemap = 0;
  g_cfg.freecamfog = 0;
  g_cfg.freecamshot = 0;
  g_cfg.freecamtransport = 0;
  g_cfg.freecamshowplayer = 0;
  g_cfg.freecamdebug = 0;
  g_cfg.freecamteleportmarker = 0;
  // The crash stash starts empty: a held flag only arrives by being READ off disk (shot.h).
  g_cfg.shotstashheld = 0;
  g_cfg.shotstashmask = 0;
  g_cfg.shotstashcrosshair = 0;
  g_cfg.shotstashweapon = 0;
  g_cfg.shotstashstats = 0;

  g_cfg.debuglog = 0;

  g_cfg.tracers = 0;

  g_cfg.perfzones = 0;
  g_cfg.perfdetail2 = 0;
  g_cfg.perftable = 0;
  g_cfg.perfskip = 0;

  // 0 is "unbound", which bind.h renders as an empty field rather than as a key nobody can press. bindaim has no
  // feature yet.
  g_cfg.bindaim = g_cfg.bindteleport = 0;

  // T, World Tour's default. input.cpp takes it only while freecamon is set, so a player who never uses the camera
  // keeps T for the game.
  g_cfg.bindcamera = 0x54;

  // B AND V: C and X are bound by the game; V is unbound and the familiar paste key, B sits beside it. Taken only while
  // freecamon is set.
  g_cfg.bindcamcopy  = 0x42;   // 'B'
  g_cfg.bindcampaste = 0x56;   // 'V'
}

bool config_load(void) {
  config_defaults();
  if (!ini_path()) { s_onDisk = g_cfg; return false; }

  FILE* f = NULL;
  if (fopen_s(&f, s_path, "r") != 0 || !f) {
    s_onDisk = g_cfg;
    return false;
  }

  char line[CONFIG_LINE_BUFSIZE];
  int read = 0, lineNo = 0;
  for (;;) {
    const int got = config_read_line([f] { return fgetc(f); }, line, CONFIG_LINE_BUFSIZE);
    if (got == CONFIG_LINE_END) break;
    lineNo++;
    if (got == CONFIG_LINE_OVERLONG) {
      ek_log("config: line %d is over %d bytes; skipped", lineNo, CONFIG_LINE_BUFSIZE - 1);
      continue;
    }
    char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == 0 || *p == ';' || *p == '#' || *p == '[') continue;

    char* eq = strchr(p, '=');
    if (!eq) continue;
    *eq = 0;
    char* key = p;
    char* val = eq + 1;

    // Trim the key's trailing space; the value's leading space is skipped by
    // strtol, and the reader has already dropped the newline.
    char* end = key + strlen(key);
    while (end > key && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;

    for (int i = 0; i < kFieldCount; i++) {
      if (_stricmp(key, kFields[i].key) != 0) continue;
      g_cfg.*(kFields[i].member) = (int)strtol(val, NULL, 0);
      read++;
      break;
    }
    // An unknown key is left alone rather than reported: it may belong to a
    // newer build of the mod, and this one must not eat it.
  }
  fclose(f);

  s_onDisk = g_cfg;
  g_cfgDirty = 0;
  ek_log("config: read %d key(s) from %s", read, s_path);
  return true;
}

void config_write_changed(void) {
  if (!g_cfgDirty) return;
  g_cfgDirty = 0;
  if (!ini_path()) return;

  int wrote = 0;
  for (int i = 0; i < kFieldCount; i++) {
    const int now = g_cfg.*(kFields[i].member);
    if (now == s_onDisk.*(kFields[i].member)) continue;
    char buf[32];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%d", now);
    if (WritePrivateProfileStringA("ekmod", kFields[i].key, buf, s_path)) {
      s_onDisk.*(kFields[i].member) = now;
      wrote++;
    } else {
      // Logged once per attempt rather than per field, and the value stays
      // dirty in memory so the panel still behaves as the player set it.
      ek_log("config: could not write %s (%lu)", kFields[i].key, GetLastError());
      g_cfgDirty = 1;
    }
  }
  if (wrote) ek_log("config: wrote %d changed key(s)", wrote);
}

// bind.h addresses binds by row, not by field name, so one place maps between them.
int config_bind_get(int row) {
  switch (row) {
    case 0:  return g_cfg.menukey;
    case 1:  return g_cfg.bindaim;
    case 2:  return g_cfg.bindcamera;
    case 3:  return g_cfg.bindteleport;
    case 4:  return g_cfg.bindcamcopy;
    case 5:  return g_cfg.bindcampaste;
    default: return 0;
  }
}

void config_bind_set(int row, int vk) {
  switch (row) {
    case 0:  g_cfg.menukey = vk; break;
    case 1:  g_cfg.bindaim = vk; break;
    case 2:  g_cfg.bindcamera = vk; break;
    case 3:  g_cfg.bindteleport = vk; break;
    case 4:  g_cfg.bindcamcopy = vk; break;
    case 5:  g_cfg.bindcampaste = vk; break;
    default: return;
  }
  g_cfgDirty = 1;
}
