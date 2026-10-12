#pragma once

// The mod's ini beside eduke32.exe. Every field is a plain int: the row helpers and ini parser know only ints.

const int CONFIG_LINE_BUFSIZE = 512;

enum { CONFIG_LINE_END = -1, CONFIG_LINE_OVERLONG = -2 };

// One line from next() (a getc: a byte, or -1 at the end) into line[cap], '\n' dropped. Returns its length,
// CONFIG_LINE_END, or CONFIG_LINE_OVERLONG for a line over cap - 1 bytes, read to its end and dropped: cut, its tail
// would parse as a line of its own.
template <class Next> inline int config_read_line(Next next, char* line, int cap) {
  if (!line || cap < 1) return CONFIG_LINE_END;
  int len = 0;
  bool any = false, over = false;
  for (;;) {
    const int c = next();
    if (c < 0) break;
    any = true;
    if (c == '\n') break;
    if (len < cap - 1) line[len++] = (char)c;
    else over = true;
  }
  line[len] = 0;
  if (!any) return CONFIG_LINE_END;
  return over ? CONFIG_LINE_OVERLONG : len;
}

struct Config {
  int menukey;    // virtual-key code that opens the panel; default VK_INSERT (0x2D)
  int uiscale;    // percent, UI_SCALE_MIN..UI_SCALE_MAX (ui.h), default 100
  // Stored panel box in logical pixels at UI scale 100; -1/-1/0/0 is unset (0,0 is a real corner, no panel is
  // zero-sized).
  int panelx, panely, panelw, panelh;
  int paneltab;   // which tab was open last, 0..TAB_COUNT-1
  int animations; // 0/1, the panel's springs and sweeps

  int esp;
  int espboxes;
  int esphealthbar;
  int espname;
  int espdistance;
  int espitems;
  int espsecrets;
  int secretvolume;
  int secrettriggers;
  int secretpath;
  int dmgnumbers;
  int espexplodables;
  int espbreakables;
  int espswitches;
  int esptint;
  int espdrawdist;   // Build units, ESP_DRAWDIST_MIN..ESP_DRAWDIST_MAX (esp.h)
  int espnolimit;    // 0/1, ignores espdrawdist entirely when set

  int aimon;
  int aimhold;       // 0/1, only while the bound key is held
  int aimsmooth;
  int aimtarget;
  int aimpoint;
  int aimcone;
  int aimrange;
  int aimlead;
  int aimleadscale;
  int aimdebug;

  int accpistol;
  int accchaingun;
  int accshotgun;
  int accexpander;

  int firerate;     // the firing animation's dead frames
  int switchspeed;  // the lowering and raising runs

  int silenton;
  int silentmaxangle;  // 1..180 degrees; a shot further off than this is left alone

  int tboton;
  int tbotwhen;   // TBOT_WHEN_CROSSHAIR (0) or TBOT_WHEN_AIMBOT (1), tbot.h
  int tbotdelay;  // 0..500 ms, converted to whole tics by tbot_delay_tics

  int mouse4action;
  int mouse5action;

  // Minutes between timed autosaves; 0 is off. The engine's own autosave settings are cvars the panel edits in place,
  // so a copy here would give two answers.
  int autosavemins;

  int fovon;
  int fov;
  // The crash stash, replayed next launch: an unclean exit while on never reaches the OFF edge (see shot.h).
  int fovstashheld, fovstashsaved, fovstashforced;

  int freecamon;
  int freecamspeed;  // FREECAM_SPEED_MIN..MAX percent (freecam.h), default 100
  // Draws every sector after the engine's draw so out of bounds has the whole map AND the sky; about twice the
  // geometry, hence its own row.
  int freecamwholemap;
  int freecamfog;
  // Hides weapon, crosshair and level stats while flying. It writes the PLAYER'S OWN cvars, hence off by default
  // (shot.h).
  int freecamshot;
  int freecamshowplayer;
  int freecamdebug;
  // Carries the camera across a paired SE 7 effector (teleporter or water surface) by the engine's rule (freecam.h).
  int freecamtransport;
  int freecamteleportmarker;
  // Screenshot-mode crash stash, written before the cvars are zeroed: an Alt+F4 or crash bypasses the DllMain restore,
  // so it is replayed next launch if shot_stash_valid agrees.
  int shotstashheld;
  int shotstashmask;        // which of SHOT_CVARS were held; 0 = nothing to replay
  int shotstashcrosshair, shotstashweapon, shotstashstats;

  int debuglog;   // 0/1, log beyond the install-time lines
  int tracers;
  // The frame-timing table on screen and in the log every five seconds; the measurement itself is always on (perf.h).
  int perfzones;
  int perfdetail2;
  int perftable;
  // Bitmask, INI ONLY: 1 skips the sprite pass, 2 the wall draws, 4 the updates, 8 makes the frustum test reject
  // nothing; draws wrong on purpose to price a phase.
  int perfskip;

  int bindaim, bindcamera, bindteleport;
  int bindcamcopy, bindcampaste;
};

extern Config g_cfg;

extern int g_cfgDirty;

// The bind store bind.h reads and writes. Aimbot and teleport rows store a value but nothing acts on them yet.
int  config_bind_get(int row);
void config_bind_set(int row, int vk);

void config_defaults(void);
bool config_load(void);
void config_write_changed(void);
