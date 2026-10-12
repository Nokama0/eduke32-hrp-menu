#pragma once

#include "esp_names.h"
#include "actorstate.h"

// The one list of things that can be shot, shared by ESP and aimbot (targets.cpp explains the move).

// Generous ceiling: a bound below the room's enemy count would silently drop boxes.
const int EK_TARGET_MAX = 256;

struct EkTarget {
  int   sprite;
  int   x, y, z;
  short sect;
  short hp;
  // What the ESP prints: hp less the damage queued in actor[].htextra, so the bar moves during a pain animation. No
  // targeting rule may read it (ekm_actor_shown_extra).
  short hpShown;
  short picnum;
  int   feetZ;
  float heightZ;
  unsigned char xrep, yrep;
  unsigned short cstat;
  unsigned char pal;
  int   tw, th;
  // EKA_PLAYDEAD is listed but not a target: its cstat is clear, so the aimbot refuses it; the ESP labels the state.
  int   state;
};

// Frozen is palette index 1 (player.cpp:285-287).
const int EKM_PAL_FROZEN = 1;

// The aimbot's rule, not the ESP's: a shot needs hitscan-sensitive cstat and no invisible bit; the ESP shows
// unshootable enemies. Bit 0 is not part of it: the sprite mask is cliptype>>16 = 256, bit 8 alone (clip.cpp:2849,
// 2671; CLIPMASK1 build.h:226), and a dormant Recon Patrol Vehicle (cstat 0x0100) is hit fine.
inline bool ekm_target_hittable(unsigned char xrepeat, unsigned cstat) {
  return xrepeat != 0 && (cstat & 0x8100u) == 0x100u;
}

// A choking trooper is alive at zero health with cstat left alone, while real death states clear cstat (GAME.CON
// checktroophit :4415-4421); that holds for troopers only (Enforcer, Octabrain, Drone corpses keep cstat 257), so it is
// scoped by picnum, sitters excluded, frozen excluded, suffering action required. Details: docs/re/targets.md.
inline bool ekm_trooper_choking(int picnum, int extra, int pal,
                                unsigned cstat, unsigned char xrepeat,
                                int suffering = EKM_SUFFER_UNKNOWN) {
  if (suffering == EKM_SUFFER_NO) return false;
  return extra == 0 && pal != EKM_PAL_FROZEN
      && ekm_picnum_is_trooper(picnum)
      && !ekm_actor_defers_damage(picnum)
      && ekm_target_hittable(xrepeat, cstat);
}

// Dying or dead: not a target for ESP, aimbot or triggerbot. Do not widen the picnum-scoped rule from engine source
// (docs/re/targets.md). Damage is deferred (htextra, actors.cpp:1322), so ~33 ms of lingering is unavoidable.
inline bool ekm_target_dying(int extra, int pal, int picnum,
                             unsigned cstat, unsigned char xrepeat,
                             int suffering = EKM_SUFFER_UNKNOWN) {
  if (ekm_trooper_choking(picnum, extra, pal, cstat, xrepeat, suffering)) return false;
  return extra <= 0 && pal != EKM_PAL_FROZEN;
}

// One state per sprite; test order is the rule. playDead outranks all (cleared cstat at zero health, seen only via the
// decoded action record): labelled, not targeted. Choking is shown and targeted; what dying refuses is EKA_DOWN, not
// drawn.
inline int ekm_actor_state(int picnum, int extra, int pal, unsigned cstat,
                           unsigned char xrepeat, bool playDead,
                           int suffering = EKM_SUFFER_UNKNOWN,
                           int dyingRecord = EKM_DYING_UNKNOWN) {
  if (playDead) return EKA_PLAYDEAD;
  if (dyingRecord == EKM_DYING_YES) return EKA_DOWN;
  if (ekm_trooper_choking(picnum, extra, pal, cstat, xrepeat, suffering)) return EKA_CHOKING;
  return ekm_target_dying(extra, pal, picnum, cstat, xrepeat, suffering) ? EKA_DOWN
                                                                        : EKA_ALIVE;
}

int targets_collect(EkTarget* out, int max);

// Per-term refusal counts since load ("notenemy N, critter N, zerorep N, down N") for the aimbot's `aim acquire:` line;
// counts only while the Debug log row is on.
void targets_refusal_summary(char* buf, int n);

// Is this sprite worth a bullet now? Asked of the sprite hitscan named. Includes the aimbot's hittable rule (can it be
// shot), unlike targets_collect (should it be drawn). False for anything unreadable.
bool targets_sprite_targetable(int sprite);

bool targets_sprite_picnum(int sprite, int* out);
