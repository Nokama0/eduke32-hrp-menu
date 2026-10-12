#pragma once

// DukePlayer_t: the local player's view, weapon, sector. Packed (player.h:170-292), sizeof 640 (:267). Offset
// derivations and which were bracketed, not measured: docs/re/playerstate.md.
const int EK_PS_SIZE       = 640;
const int EK_PS_POS        = 0;
const int EK_PS_OPOS       = 12;  // previous tic's pos (player.h:185)
const int EK_PS_Q16HORIZ   = 64;
const int EK_PS_Q16HORIZOFF= 68;  // fix16, ADDED to q16horiz by the engine
const int EK_PS_OQ16HORIZ  = 72;
const int EK_PS_OQ16HORIZOFF = 76;
const int EK_PS_Q16ANG     = 80;
const int EK_PS_OQ16ANG    = 84;
const int EK_PS_CURSECTNUM = 404;
const int EK_PS_I          = 516;
const int EK_PS_PYOFF      = 488;  // view bob (player.h:221)

// vel.z, bracketed. The teleport zeroes it.
const int EK_PS_VELZ = 32;

// bobpos (x 48, y 52). The teleport MUST write it: player.cpp:5308 derives the sprite's xvel from pos vs bobpos and
// wraps on a big difference.
const int EK_PS_BOBPOS = 48;

// Bracketed offsets the camera added; each is the near anchor of a live offset's walk, so they stay.
const int EK_PS_OLOOK_ANG  = 372;
const int EK_PS_LOOK_ANG   = 406;
const int EK_PS_OPYOFF     = 490;
const int EK_PS_GM         = 598;

// newowner 492 and over_shoulder_on 608, bracketed (docs/re/playerstate.md).
const int EK_PS_NEWOWNER   = 492;
const int EK_PS_OVER_SHOULDER_ON = 608;

// got_access 506, bracketed. A wrong offset suppresses the hint on a real lock, never leaves one unmarked.
const int EK_PS_GOT_ACCESS = 506;
const int EK_PS_AUTO_AIM   = 590;
const int EK_PS_CURR_WEAPON = 635;
// The weapon counters were measured by compiling DukePlayer_t standalone.
const int EK_PS_KICKBACK    = 597;
const int EK_PS_WEAPON_POS  = 633;

// actors.h:43. Hitscan weapons fire from pos.z + pyoff (player.cpp:528 subtracts it again); projectiles keep it.
const int EK_ZOFFSET6 = 4 << 8;

// auto_aim is 0 during a silent-aim shot, measured in play: the engine's snap (player.cpp:380-382) uses Build's
// approximate distance, ran ~5% long at 11432 units and missed our correct line (308 rounds, 8 damage events). 0
// returns -1 for every weapon, so the lead is kept.
const int EK_SILENT_AUTO_AIM = 0;

const int EK_APLAYER     = 1405;
const int EK_STAT_PLAYER = 10;

// Does a sprite read as a live player? statnum is the load-bearing half: multiplayer-start APLAYERs go to STAT_MISC in
// single player (game.cpp:2786-2794).
inline bool ekm_is_player_sprite(int picnum, int statnum) {
  return picnum == EK_APLAYER && statnum == EK_STAT_PLAYER;
}

const int EK_MODE_MENU = 0x01;

// player.h:120-128, bit 2. gm equals this exactly only once a level is resident (premap.cpp:2046; gm = 0 during load,
// :1465).
const int EK_MODE_GAME = 0x04;

// Is the level loaded? EXACT EQUALITY, not "MODE_GAME clear": setlevel, cheat restart and reload-after-death keep
// MODE_GAME set through a load. A failed read is false. ekm_level_loading (gm == 0) is not its negation; a failed read
// is not a level change.
inline bool ekm_level_loading(int gm, bool gmOk) { return gmOk && gm == 0; }

inline bool esp_level_loaded(int gm, bool gmOk) {
  return gmOk && gm == EK_MODE_GAME;
}

// inv.h:61-73. The four weapons P_PreFireHitscan routes through and the five straight-flight projectiles.
const int EK_WEAPON_PISTOL       = 1;
const int EK_WEAPON_SHOTGUN      = 2;
const int EK_WEAPON_CHAINGUN     = 3;
const int EK_WEAPON_RPG          = 4;
const int EK_WEAPON_SHRINKER     = 6;
const int EK_WEAPON_DEVISTATOR   = 7;
const int EK_WEAPON_FREEZE       = 9;
const int EK_WEAPON_GROW         = 11;
const int EK_WEAPON_FLAMETHROWER = 12;

// Keyed on curr_weapon, not projecTile: World Tour renumbered tiles (silentwt.cpp:73); a weapon index survives.
inline bool ekm_weapon_wants_accuracy(int weapon, int accPistol, int accShotgun,
                                      int accChaingun, int accExpander) {
  switch (weapon) {
    case EK_WEAPON_PISTOL:   return accPistol   != 0;
    case EK_WEAPON_SHOTGUN:  return accShotgun  != 0;
    case EK_WEAPON_CHAINGUN: return accChaingun != 0;
    case EK_WEAPON_GROW:     return accExpander != 0;
    default:                 return false;
  }
}

// Mirrors ekm_weapon_wants_accuracy so one edit shows the other.
inline bool ekm_weapon_is_hitscan(int weapon) {
  switch (weapon) {
    case EK_WEAPON_PISTOL:
    case EK_WEAPON_SHOTGUN:
    case EK_WEAPON_CHAINGUN:
    case EK_WEAPON_GROW:
      return true;
    default:
      return false;
  }
}

// Where the shot leaves: hitscan from pos.z + pyoff, projectiles keep ZOFFSET6. Adding it everywhere put impacts 1024
// too high. Unreadable weapon keeps the offset. Approximate for RPG, Devastator, freezer, shrinker, knee (256-512
// more).
inline int ekm_muzzle_z(int posZ, int pyoff, int weapon) {
  return posZ + pyoff + (ekm_weapon_is_hitscan(weapon) ? 0 : EK_ZOFFSET6);
}

// Projectile speed per TIC, or 0 for DO NOT LEAD (hitscan, thrown pipe bomb, trip bomb, knee, remote). Flamethrower 840
// is unconfirmed in game (needs CON version 16, duke3d.h:55). Sources: docs/re/playerstate.md.
inline int ekm_weapon_proj_speed(int weapon) {
  switch (weapon) {
    case EK_WEAPON_RPG:
    case EK_WEAPON_DEVISTATOR:
    case EK_WEAPON_FREEZE:         return 644;
    case EK_WEAPON_SHRINKER:       return 768;
    case EK_WEAPON_FLAMETHROWER:   return 840;
    default:                       return 0;
  }
}

// playerdata_t holds `ps` first (player.h:271); g_player[0].ps is at g_player_s + 133. 133 is ODD: an 8-stride search
// could never reach it.
const int EK_PD_SIZE = 133;
static_assert(EK_PD_SIZE % 8 != 0,
              "playerdata_t is not a multiple of 8, so the pointer search "
              "must stride 1; a stride of 8 cannot reach g_player[0].ps");

const int EK_MAXPLAYERS = 16;

// True when a run of ps fields looks like g_player_s: null zeroth entry, then sixteen DISTINCT non-null allocations
// (game.cpp:6803-6804).
inline bool ekm_slot_run_ok(const void* zeroth, const void* const* players, int count) {
  if (zeroth != nullptr || players == nullptr || count != EK_MAXPLAYERS) return false;
  for (int i = 0; i < count; i++) {
    if (players[i] == nullptr) return false;
    for (int j = 0; j < i; j++)
      if (players[i] == players[j]) return false;
  }
  return true;
}

// Masked, not clamped: the engine lets the angle run past a turn.
inline int ekm_q16ang_degrees(int q16ang) {
  return (int)(((q16ang >> 16) & 2047) * 360 / 2048);
}

// Needs a loaded level and live player sprite, so it retries. The struct is on the heap (game.cpp:6433); player_rva is
// the image slot pointing at it.
bool player_scan(void);
bool player_ready(void);
unsigned player_rva(void);

// Health and palette from the player's SPRITE (player.cpp:2262, 3033, 1796). Nothing caches the index: a latch went
// stale across levels.
bool player_read_body(int* hp, int* pal);

// Is `spriteNum` the sprite the player drives? Re-reads ps->i and requires a live player record; false on any failed
// read.
bool player_sprite_matches(int spriteNum);

// One guarded read; the only path to engine memory here. horizoff is included as the effective pitch is q16horiz +
// q16horizoff (player.cpp:498, :304).
struct PlayerSnap { int x, y, z, ang, horiz, horizoff, sect; };
bool player_snap(PlayerSnap* out);

// Both draw-mode fields at one instant; both or neither.
bool player_read_draw_mode(int* newowner, int* overShoulderOn);

// gm, guarded; false means assume the menu is open.
bool player_read_gm(int* out);

bool player_read_weapon(int* out);

// The view write, to fields the engine writes itself (P_UpdateAngles, player.cpp:3080-3084). `alsoOld` avoids
// interpolating from the old angle on a snap. Hook only, after P_GetInput.
bool player_write_view(int q16ang, int q16horiz, bool alsoOld);

bool player_read_weapon_anim(int* weapon, int* kickback, int* weaponPos);

// Weapon counter writes, to fields the engine steps itself (player.cpp:4588, :4212). Hook only, after the tic; a failed
// write costs the feature, not the game.
bool player_write_kickback(int v);
bool player_write_weapon_pos(int v);

bool player_read_pyoff(int* out);

// auto_aim, read only: it decides whether the engine may redirect a shot off our line.
bool player_read_auto_aim(int* out);

// Masked to the three card bits. False means "cannot tell", not "holds nothing".
bool player_read_got_access(int* out);

// opos: the point G_DrawRooms interpolates the camera from.
bool player_read_opos(int* x, int* y, int* z);

// Silent aim's substitution and undo: six fields (q16ang, q16horiz, their o pair, auto_aim, the sprite's ang). Valid
// for ONE engine call; the restore is mandatory.
struct EkShotSave {
  int q16ang, oq16ang, q16horiz, oq16horiz;
  int autoAim;
  int sprAng;
  int sprOk;
};

bool player_write_shot_view(int q16ang, int q16horiz, int autoAim, EkShotSave* save);
bool player_restore_shot_view(const EkShotSave* save);

// The shooter's blocking bits, cleared around one cast as Proj_DoHitscan does (player.cpp:548-560; hitscan.h). The
// restore is mandatory (__try/__finally). `rec` is the record written; `held` is 0 when nothing was.
struct EkShooterCstat { unsigned char* rec; int saved; int held; };

// Clear `mask` from the local player's sprite, confirmed as player_sprite_matches does; unconfirmed writes nothing.
bool player_clear_shooter_cstat(int mask, EkShooterCstat* save);

// False only for a faulted owed write, which the caller logs.
bool player_restore_shooter_cstat(const EkShooterCstat* save);

// Moves the player to ekm_tp_plan's destination (freecam.h): ten fields, all or refused before the first store
// (pos/opos xyz, bobpos x/y, cursectnum, vel.z = 0; the engine's transporter writes the same, actors.cpp:3859-3881). No
// rollback. Tic clock only, from camera_frame; false for a refused plan or untrusted sector.
struct EkTeleportPlan;
bool player_write_pos(const EkTeleportPlan& plan);

bool player_q16ang(int* out);
bool player_q16horiz(int* out);
bool player_pos(int* x, int* y, int* z);

// One line a second under the Debug log, only when a value moved.
void player_log_tick(void);
