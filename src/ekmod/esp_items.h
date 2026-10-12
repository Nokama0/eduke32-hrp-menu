#pragma once

// Which picnum is a pickup and its name; every number is this game's own (verification: docs/re/picnums.md). World Tour's table
// was not copied. An unlisted picnum returns NULL and is not collected: tile names are runtime variables (esp_names.h).
#include <string.h>
#include "ui/canvas.h"

const int EKM_PIC_ACCESSCARD = 60;
const int EKM_PIC_HEAVYHBOMB = 26;

// gameexec.cpp:1035-1037 sets got_access from exactly these three pal values.
const int EKM_PAL_KEY_BLUE   = 0;
const int EKM_PAL_KEY_RED    = 21;
const int EKM_PAL_KEY_YELLOW = 23;

inline const char* ekm_item_name(int picnum) {
  switch (picnum) {
    case 21:   return "Pistol";
    case 22:   return "Chaingun cannon";
    case 23:   return "RPG";
    case 24:   return "Freezethrower";
    case 25:   return "Shrinker";
    // One thrown bomb; 47 is the box. A pickup here unlike World Tour's table: a bomb is takeable
    // (ekm_pipebomb_takeable).
    case 26:   return "Pipebomb";
    case 27:   return "Laser tripbomb";
    case 28:   return "Shotgun";
    case 29:   return "Devastator";
    case 37:   return "Freezethrower ammo";
    case 40:   return "Pistol ammo";
    case 41:   return "Chaingun ammo";
    case 42:   return "Devastator ammo";
    case 44:   return "RPG ammo";
    case 45:   return "Expander ammo";
    case 46:   return "Shrinker ammo";
    case 47:   return "Pipebombs";
    case 48:   return "Pistol ammo";
    case 49:   return "Shotgun ammo";
    case 51:   return "Small medkit";
    case 52:   return "Large medkit";
    case 53:   return "Portable medkit";
    case 54:   return "Armour";
    case 55:   return "Steroids";
    case 56:   return "Scuba gear";
    case 57:   return "Jetpack";
    case 59:   return "Night vision goggles";
    case 60:   return "Keycard";
    case 61:   return "Boots";
    case 100:  return "Atomic health";
    case 1348: return "Holoduke";
    default:   return 0;
  }
}

// Only the keycard is named by palette.
inline const char* ekm_item_name_pal(int picnum, int pal) {
  const char* nm = ekm_item_name(picnum);
  if (nm == 0 || picnum != EKM_PIC_ACCESSCARD) return nm;
  if (pal == EKM_PAL_KEY_BLUE)   return "Blue keycard";
  if (pal == EKM_PAL_KEY_RED)    return "Red keycard";
  if (pal == EKM_PAL_KEY_YELLOW) return "Yellow keycard";
  return nm;
}

// A respawning item is hidden by cstat bit 15, not deleted; mask it, as cstat arrives sign-extended from a short. Zero
// xrepeat marks a skill-filtered pickup.
inline bool ekm_item_present(int cstat, int xrepeat) {
  return (cstat & 0x8000) == 0 && xrepeat > 0;
}

// Takeable pipebomb, the engine's condition at actors.cpp:5435: HEAVYHBOMB && pData[0] > 7 && xvel == 0 (the distance
// term is the player's). An armed bomb stays marked: t_data[6] (1 timer, 2 remote, 3 detonate, actors.cpp:5373, :5401)
// never gates pickup.
inline bool ekm_pipebomb_takeable(int tdata0, int xvel) {
  return tdata0 > 7 && xvel == 0;
}

// Keycard colour by pal, or 0. Diverges from World Tour, which draws every pickup in C_BLUE so a red card reads blue
// (its esp_layout.h:338 claims a shared card mark that only the lock uses). Mapping is WT's esp_switch.h:154, so a card
// and its lock match; yellow is C_HAZARD.
inline unsigned ekm_keycard_colour(int pal) {
  if (pal == EKM_PAL_KEY_BLUE)   return C_BLUE;
  if (pal == EKM_PAL_KEY_RED)    return C_RED;
  if (pal == EKM_PAL_KEY_YELLOW) return C_HAZARD;
  return 0;
}
