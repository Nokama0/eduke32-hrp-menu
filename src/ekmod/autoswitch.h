#pragma once

// Weapon switching is ONE engine cvar, cl_weaponswitch (0..7: 1 if new, 2 if empty, 4 by wchoice). Bits 1 and 4 are one
// three-state option (the engine menu never sets 4 without 1; P_AddWeaponMaybeSwitch), so only bit 2 has its own row.

const int EK_WSWITCH_NEW     = 1;
const int EK_WSWITCH_EMPTY   = 2;
const int EK_WSWITCH_WCHOICE = 4;
const int EK_WSWITCH_MASK    = EK_WSWITCH_NEW | EK_WSWITCH_EMPTY | EK_WSWITCH_WCHOICE;

// Clamped, not masked, to agree with the engine OSD's vmin/vmax clamp (8 becomes 7, not 0). Pinned by tb_0075.
inline int wswitch_mask_clamped(int v) {
  if (v < 0) return 0;
  if (v > EK_WSWITCH_MASK) return EK_WSWITCH_MASK;
  return v;
}

inline int wswitch_bit_on(int mask, int bit) {
  return (wswitch_mask_clamped(mask) & bit) ? 1 : 0;
}

// Read-modify-write: the three rows share one engine int.
inline int wswitch_with_bit(int mask, int bit, int on) {
  const int m = wswitch_mask_clamped(mask);
  const int b = bit & EK_WSWITCH_MASK;
  return on ? (m | b) : (m & ~b);
}

// The panel writes exactly 0, 1 and 1|4, never a value the game's own menu could not produce.
const int EK_WSWITCH_PICKUP_MASK = EK_WSWITCH_NEW | EK_WSWITCH_WCHOICE;
const int EK_PICKUP_NEVER = 0, EK_PICKUP_ALWAYS = 1, EK_PICKUP_IF_BETTER = 2;
const int EK_PICKUP_STATES = 3;

inline int wswitch_pickup_state(int mask) {
  const int m = wswitch_mask_clamped(mask);
  if (!(m & EK_WSWITCH_NEW)) return EK_PICKUP_NEVER;
  return (m & EK_WSWITCH_WCHOICE) ? EK_PICKUP_IF_BETTER : EK_PICKUP_ALWAYS;
}

// Read-modify-write so bit 2 and any future bit survive.
inline int wswitch_with_pickup(int mask, int state) {
  int m = wswitch_mask_clamped(mask) & ~EK_WSWITCH_PICKUP_MASK;
  if (state == EK_PICKUP_ALWAYS)    m |= EK_WSWITCH_NEW;
  if (state == EK_PICKUP_IF_BETTER) m |= EK_WSWITCH_PICKUP_MASK;
  return m;
}

// "Only if better" is not "your weapon order": wchoice is a per-player list this port does not expose.
inline const char* wswitch_pickup_name(int state) {
  switch (state) {
    case EK_PICKUP_ALWAYS:    return "Always";
    case EK_PICKUP_IF_BETTER: return "Only if better";
    default:                  return "Never";
  }
}
