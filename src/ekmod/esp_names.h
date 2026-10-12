#pragma once

#include <string.h>   // strcmp, for ekm_same_enemy's family comparison

// Table from EDuke32's own names.h, not World Tour's. Tile names are runtime variables a CON `define` can move
// (dnames.cpp:1595), so unlisted picnums print the NUMBER: a wrong name is worse.

// pal 21 marks the Assault Captain and the trooper picnums are shared with the Trooper (esp_pics.cpp:179-182), so
// picnum alone mislabels every Captain.
const int EKM_PAL_CAPTAIN = 21;

inline bool ekm_picnum_is_trooper(int picnum) {
  switch (picnum) {
    case 1680: case 1681: case 1682: case 1715:   // LIZTROOP, RUNNING, STAYPUT, SHOOT
    case 1725: case 1741: case 1742: case 1744:   // JETPACK, ONTOILET, JUSTSIT, DUCKING
      return true;
    default:
      return false;
  }
}

// The name for a picnum, or null when this table does not know it -- the caller
// must print the number rather than guess.
inline const char* ekm_enemy_name(int picnum) {
  if (picnum >= 2370 && picnum <= 2377) return "Protozoid Slimer";
  if (ekm_picnum_is_trooper(picnum)) return "Assault Trooper";
  switch (picnum) {
    case 675:  return "Egg";
    case 1267: return "Rat";
    case 1550: return "Shark";
    case 1810: return "Cannon";
    case 1820: case 1821: return "Octabrain";
    case 1880: return "Sentry Drone";
    case 1920: case 1921: return "Assault Commander";
    case 1960: return "Recon Patrol";
    case 1975: return "Pig Cop Tank";
    case 2000: case 2001: case 2045: return "Pig Cop";
    case 2120: case 2121: case 2150:
    case 2160: case 2165: return "Enforcer";
    case 2360: return "Turret";
    case 2420: return "Organic turret";
    case 2630: case 2631: return "Battlelord";
    case 2710: case 2711: return "Overlord";
    case 2760: case 2761: return "Cycloid Emperor";
    case 4610: case 4611: case 4670:
    case 4671: case 4690: return "Protector Drone";
    case 4740: case 4741: return "Alien Queen";
    case 5180: return "Firefly Trooper";
    case 5310: case 5311: return "Alien Overlord";
    default:   return 0;
  }
}

// A paletted BOSS1 is a different enemy: GAME.CON:6466-6470 gives pal != 0 `strength BOSS1PALSTRENGTH` (1000 vs 4500)
// and game.cpp:2073-2081 halves its size (the Sentry). Any non-zero pal, unlike the Captain's `ifspritepal 21`
// (GAME.CON:4715).
const int EKM_PIC_BOSS1 = 2630, EKM_PIC_BOSS1STAYPUT = 2631;

// STAYPUT variants are real enemies whose TILE the engine never flags as a badguy (A_Spawn routes them via
// actor->stayput, game.cpp:1996-2009), so picnum_is_enemy missed them until activation. World Tour lists them outright
// (wtmod/esp_pics.cpp:18-51).
inline bool ekm_picnum_is_stayput(int picnum) {
  switch (picnum) {
    case 1682:
    case 1821:
    case 1921:
    case 2001:
    case 2045:
    case 2121:
    case 2631:
    case 2711:
    case 2761:
    case 4611:
    case 4741:
    case 5311:
      return true;
    default:
      return false;
  }
}
inline bool ekm_picnum_is_boss1(int picnum) {
  return picnum == EKM_PIC_BOSS1 || picnum == EKM_PIC_BOSS1STAYPUT;
}

inline const char* ekm_enemy_name_pal(int picnum, int pal) {
  if (pal == EKM_PAL_CAPTAIN && ekm_picnum_is_trooper(picnum)) return "Assault Captain";
  if (pal != 0 && ekm_picnum_is_boss1(picnum)) return "Battlelord Sentry";
  return ekm_enemy_name(picnum);
}

// The health label's maximum, from THIS build's CON (`actor <PICNUM> <STRENGTH>` lines via DEFS.CON/USER.CON), never
// World Tour's table. 0 means UNKNOWN (no actor line: RAT, RECON, GREENSLIME, LIZMANFEEDING), selecting the bare "%d"
// form.
inline int ekm_enemy_max_strength(int picnum) {
  switch (picnum) {
    case 675:  return 20;     // EGG: actor EGG TOUGH (TOUGH=20)
    case 1550: return 35;
    case 1680: case 1681: case 1682: case 1715:
    case 1725: case 1741: case 1742: case 1744:
      return 30;               // LIZTROOP family: TROOPSTRENGTH=30
    case 1810: return 400;
    case 1820: case 1821: return 175;   // OCTABRAIN(STAYPUT): OCTASTRENGTH=175
    case 1880: return 150;
    case 1920: case 1921: return 350;   // COMMANDER(STAYPUT): COMMANDERSTRENGTH=350
    case 1975: return 500;
    case 2000: case 2001: case 2045: return 100;  // PIGCOP family: PIGCOPSTRENGTH=100
    // LIZMANFEEDING (2160) has no actor line of its own in this CON.
    case 2120: case 2121: case 2150: case 2165: return 100;
    case 2360: return 40;     // ROTATEGUN: ROTTURRETSTRENGTH=40
    case 2420: return 30;     // ORGANTIC: TURRETSTRENGTH=30
    case 2630: case 2631: return 4500;  // BOSS1(STAYPUT): BOSS1STRENGTH=4500
    case 2710: return 4500;   // BOSS2: BOSS2STRENGTH=4500 (BOSS2STAYPUT undefined here)
    case 2760: return 4500;   // BOSS3: BOSS3STRENGTH=4500 (BOSS3STAYPUT undefined here)
    // NEWBEASTHANGDEAD (4671) is its own actor line at TOUGH=20.
    case 4610: case 4611: case 4670: case 4690: return 300;
    case 4671: return 20;     // NEWBEASTHANGDEAD: actor ... TOUGH (TOUGH=20)
    case 4740: case 4741: return 6000;  // BOSS4(STAYPUT): BOSS4STRENGTH=6000
    // Spawned by the engine with no actor line: RAT, RECON, GREENSLIME.
    // BOSS2STAYPUT/BOSS3STAYPUT/FIREFLY/BOSS5(STAYPUT) are Atomic content with no `define` here.
    default: return 0;
  }
}

// The table is a FLOOR: the shown maximum is the GREATER of it and the highest health seen on that sprite, since
// A_Spawn doubles `extra` for non-zero pals on a !PLUTOPAK build (game.cpp:2060-2062).
inline int ekm_enemy_shown_max(int strength, int observed) {
  const int a = strength > 0 ? strength : 0;
  const int b = observed > 0 ? observed : 0;
  return a > b ? a : b;
}

// First-tic maximum: `state checktrooppalette` (GAME.CON:4709-4718) is in `ifai 0`, so a pal 21 trooper gets
// `addstrength TROOPSTRENGTH` on its first executed tic. Pal'd bosses `strength` SETS (gameexec.cpp:3504-3506):
// GAME.CON:6464 BOSS1 (USER.CON:190 1000), :6796 BOSS2, :7071 BOSS3 set 1.
inline int ekm_boss_pal_strength(int picnum) {
  switch (picnum) {
    case 2630: return 1000;   // BOSS1: BOSS1PALSTRENGTH (GAME.CON:6470,
    case 2710: return 1;      // BOSS2: a literal 1 (GAME.CON:6802)
    case 2760: return 1;      // BOSS3: a literal 1 (GAME.CON:7077)
    default:   return 0;
  }
}

inline int ekm_enemy_ruled_max(int picnum, int pal) {
  if (pal == 0) return 0;   // both rules take the `ifspritepal 0` branch
  const int bossPal = ekm_boss_pal_strength(picnum);
  if (bossPal > 0) return bossPal;
  if (pal != EKM_PAL_CAPTAIN || !ekm_picnum_is_trooper(picnum)) return 0;
  const int spawn = ekm_enemy_max_strength(picnum);
  return spawn > 0 ? spawn * 2 : 0;
}

// Never below the health shown (World Tour esp_pics.cpp:235-240): a bar past full reads as broken, so the disagreement
// goes in the NUMBER.
inline int ekm_enemy_bar_max(int ruledMax, int current) {
  if (ruledMax <= 0) return 0;
  return ruledMax > current ? ruledMax : current;
}

// Health before the pending-damage discount. A dormant Captain carries spawn 30 with a real 60; substituting only when
// health equals the spawn strength limits a misread `hasRun` to one wrong value.
inline int ekm_display_hp(int hp, int spawnStrength, int ruledMax, bool hasRun) {
  if (hasRun) return hp;
  if (ruledMax <= 0 || ruledMax == spawnStrength) return hp;
  return hp == spawnStrength ? ruledMax : hp;
}

struct EkmHealthLatch {
  short max;      // the highest health seen on this slot's current occupant
  short picnum;
  short pal;
};

// Same enemy means the panel names them alike, not equal picnums: LIZTROOPONTOILET runs `cactor LIZTROOP`
// (GAME.CON:4749-4777). A runtime pal change (actors.cpp:4905) reads as a new occupant, an accepted divergence.
inline bool ekm_same_enemy(int picnumA, int palA, int picnumB, int palB) {
  const char* const a = ekm_enemy_name_pal(picnumA, palA);
  const char* const b = ekm_enemy_name_pal(picnumB, palB);
  if (!a || !b) return picnumA == picnumB && palA == palB;
  return a == b || strcmp(a, b) == 0;
}

inline int ekm_health_latch(EkmHealthLatch* slot, int picnum, int pal, int hp) {
  if (!slot) return 0;
  if (!ekm_same_enemy(picnum, pal, (int)slot->picnum, (int)slot->pal)) {
    slot->max = 0;                  // a new occupant learns its own ceiling
    slot->picnum = (short)picnum;
    slot->pal = (short)pal;
  }
  if (hp > (int)slot->max) slot->max = (short)hp;
  return (int)slot->max;
}
