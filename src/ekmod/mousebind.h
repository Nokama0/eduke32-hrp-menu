#pragma once

// Binding the mouse's thumb buttons to a game action, because the GAME'S OWN MENU HIDES THEM BEHIND ITS NUMBERING.
const int MB_ACTION_COUNT = 7;

// Index 0 is off and must stay index 0: it is the default for both buttons, so
// a feature that presses a key nobody asked for cannot be the out-of-box state.
struct MouseBindAction { const char* name; int bit; };
const MouseBindAction MB_ACTIONS[MB_ACTION_COUNT] = {
  { "Off",          -1 },
  { "Quick kick",   22 },   // SK_QUICK_KICK
  { "Jetpack",      25 },   // SK_JETPACK
  { "Night vision", 15 },   // SK_NIGHTVISION
  { "Medkit",       16 },   // SK_MEDKIT
  { "HoloDuke",     24 },   // SK_HOLODUKE
  { "Steroids",     12 },   // SK_STEROIDS
};

// Out of range answers Off: the value comes from an ini a player can hand-edit.
inline int mousebind_action_clamped(int v) {
  return v < 0 ? 0 : (v >= MB_ACTION_COUNT ? 0 : v);
}
inline const char* mousebind_action_name(int v) {
  return MB_ACTIONS[mousebind_action_clamped(v)].name;
}

// Quick kick is edge-unmasked in the game: holding it repeats a kick about every 14 tics.
inline unsigned mousebind_action_mask(int v) {
  const int bit = MB_ACTIONS[mousebind_action_clamped(v)].bit;
  if (bit < 0 || bit > 31) return 0u;
  return 1u << bit;
}

inline unsigned mousebind_bits(int action4, bool down4, int action5, bool down5) {
  unsigned m = 0;
  if (down4) m |= mousebind_action_mask(action4);
  if (down5) m |= mousebind_action_mask(action5);
  return m;
}

// --- DLL-only wiring, declared here so the rows and the hook agree --------- Read, not run, by the suite.

// Once per frame, from the SwapBuffers hook only.
void mousebind_frame(void);

// False until localInput has been found, and permanently false if the scan refused.
bool mousebind_ready(void);

unsigned mousebind_rva(void);

// SK_FIRE, the bit the player's own fire key sets (macros.h); only the triggerbot may set it.
const int EK_SK_FIRE = 2;

// OR a bit mask into localInput.bits, through this file's own located address and SEH guard.
// Never clear: the record is cleared every tic.
bool mousebind_or_bits(unsigned mask);
