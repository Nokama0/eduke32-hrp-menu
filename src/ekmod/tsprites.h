#pragma once

#include <stddef.h>
#include "gamestate.h"

// The whole-map sweep must make swept sectors' tsprite list like the engine's external arm (polymer_scansprites,
// polymer.cpp:1234). Reimplemented, not called: it is `static inline` with both call sites inlined. The copy is
// renderMakeTSpriteFromSprite (build.h:700); details: docs/re/tsprite-sweep.md.

const int EK_MAXSPRITESONSCREEN = 4096;

const int EK_TSPR_STRIDE   = EK_SPR_STRIDE;
const int EK_TSPR_CLIPDIST = 0x12;
const int EK_TSPR_OWNER    = EK_SPR_OWNER;

// The bound is the safety argument: an append past tsprite[] corrupts the host's .bss, so a negative or full counter
// stops the pass rather than being clamped.
inline bool ekm_tsprite_admit(int count, int max) {
  return max > 0 && count >= 0 && count < max;
}

// polymer.cpp:3896's own filter; showInvisible is a parameter so that the game's 0 reads as a decision.
inline bool ekm_tsprite_eligible(unsigned cstat, int xrepeat, int yrepeat,
                                 int showInvisible) {
  if ((cstat & 0x8000u) != 0 && !showInvisible) return false;
  return xrepeat > 0 && yrepeat > 0;
}

// sprite[].sectnum answers membership too: the engine keeps the sector chain and sectnum in lockstep (engine.cpp:8721,
// :8838). The chain is walked for cost (all 16384 records per frame was choppy); the invariant stays as a WITNESS,
// since chain and sprite[] were found by unrelated searches. sweptEnd is where the sweep stopped.
inline bool ekm_sprite_swept(int statnum, int sectnum, int sweptEnd,
                             int maxSectors) {
  if (sweptEnd < 0 || sweptEnd > maxSectors) return false;
  if (statnum < 0 || statnum >= EK_MAXSTATUS) return false;
  if (sectnum < 0 || sectnum >= sweptEnd) return false;
  return true;
}

const int EK_HEADSPRITESECT_SLOTS = EK_MAXSECTORS + 1;
const int EK_NEXTSPRITESECT_SLOTS = EK_MAXSPRITES;

// The engine ends a sector chain at -1 (polymer.cpp:3893).
inline bool ekm_sprite_chain_live(int i, int maxSprites) {
  return i >= 0 && i < maxSprites;
}

// Witness that the chain is the chain: a link whose record names another sector means an array is not what we believe.
inline bool ekm_sprite_chain_agrees(int sectnum, int expectSect) {
  return sectnum == expectSect;
}

// Acyclicity is the engine's property, not ours: MAXSPRITES links is every chain, one more means no chain.
inline bool ekm_chain_budget_ok(int walked, int maxSprites) {
  return walked >= 0 && walked <= maxSprites;
}

inline bool ekm_tsprite_owner_ok(int owner, int maxSprites) {
  return owner >= 0 && owner < maxSprites;
}

// Both pointers come through mingw `.refptr` slots in the inlined polymer_scansprites, between two call sites
// camera.cpp proved unique: one `imul r64,r64,44`, one `add` (tells the loads apart), two `mov r64,[reg]`, one `movsxd`
// + compare 4095. Witness: the slot called sprite[] must hold what gamestate's runtime content scan found.
inline bool ekm_scansprites_shape_ok(int imuls, int adds, int derefs,
                                     int spriteDeref, int counters) {
  return imuls == 1 && adds == 1 && derefs == 2 && spriteDeref == 1 &&
         counters == 1;
}

inline bool ekm_scansprites_slots_ok(int spriteLoads, int tspriteLoads,
                                     int countLoads, size_t spriteSlot,
                                     size_t tspriteSlot, size_t countSlot) {
  if (spriteLoads != 1 || tspriteLoads != 1 || countLoads != 1) return false;
  if (spriteSlot == 0 || tspriteSlot == 0 || countSlot == 0) return false;
  if (spriteSlot == tspriteSlot || spriteSlot == countSlot) return false;
  return tspriteSlot != countSlot;
}

// Chain arrays: each is one `movsx r32, word [base+idx*2]`; the one indexed by the imul'd (sprite) register is
// nextspritesect[], the other headspritesect[]. Two candidates for either is ambiguous.
inline bool ekm_chain_shape_ok(int heads, int nexts) {
  return heads == 1 && nexts == 1;
}

// Five distinct slots: two arrays through one slot would be read as both.
inline bool ekm_chain_slots_ok(int headLoads, int nextLoads, size_t headSlot,
                               size_t nextSlot, size_t spriteSlot,
                               size_t tspriteSlot, size_t countSlot) {
  if (headLoads != 1 || nextLoads != 1) return false;
  if (headSlot == 0 || nextSlot == 0) return false;
  if (headSlot == nextSlot) return false;
  if (headSlot == spriteSlot || headSlot == tspriteSlot ||
      headSlot == countSlot)
    return false;
  return nextSlot != spriteSlot && nextSlot != tspriteSlot &&
         nextSlot != countSlot;
}

inline bool ekm_tsprite_slot_ok(size_t slotRva, size_t targetRva,
                                size_t imageSize, int slotInData,
                                size_t bytesNeeded) {
  if (!slotInData || imageSize == 0) return false;
  if (slotRva + sizeof(void*) > imageSize) return false;
  if (targetRva == 0 || targetRva >= imageSize) return false;
  return imageSize - targetRva >= bytesNeeded;
}

// Read from the image: one record past the array is heap corruption.
inline bool ekm_tsprite_array_ok(size_t arrayRva, size_t imageSize) {
  if (imageSize == 0 || arrayRva == 0 || arrayRva >= imageSize) return false;
  return imageSize - arrayRva >=
         (size_t)EK_MAXSPRITESONSCREEN * (size_t)EK_TSPR_STRIDE;
}

// Compare on the ARRAY, not the slot: the image walk names `sprite`, the content scan whichever global held the value
// first, so slots differ legitimately. Zero is "not known", never agreement.
inline bool ekm_sprite_array_agrees(unsigned predicted, unsigned found) {
  return predicted != 0 && found != 0 && predicted == found;
}
