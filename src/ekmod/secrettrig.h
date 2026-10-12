#pragma once

// Secret trigger rules: pure classification of a (touchplate/switch, activator, operated sector) chain; secrettrig.cpp
// scans. Ports World Tour's trigger.h; the secret-vs-opens split is this port's own.

#include "gamestate.h"

// Engine numbers (names.h:25-30, actors.h:57) live here so the responder rule is pure.
const int EK_TRIG_PIC_TOUCHPLATE      = 3;
const int EK_TRIG_PIC_ACTIVATORLOCKED = 4;
const int EK_TRIG_PIC_MASTERSWITCH    = 8;
const int EK_TRIG_STAT_ACTIVATOR      = 8;

enum { EK_TRIG_TOUCHPLATE = 0, EK_TRIG_SWITCH = 1 };

// A free sprite slot keeps its old picnum and lotag (engine.cpp:9380, :8830 reset only sectnum/statnum), so only
// statnum says "not in the world". `< EK_MAXSTATUS`, not `== 0`: a switch on STAT_ACTIVATOR is valid.
inline int ekm_trig_sprite_live(int statnum) {
  return statnum >= 0 && statnum < EK_MAXSTATUS;
}

// Two lotag-driven walks: G_OperateActivators (STAT_ACTIVATOR, sector.cpp:1117) and G_OperateMasterSwitches
// (STAT_STANDABLE, :1179, which arms an actor that later operates its own sector, actors.cpp:2556). Both end at the
// responder's sector, so one lotag join serves both. Kinds are exclusive; trigger and responder roles can coexist.
enum { EK_TRIG_RESP_NONE = -1, EK_TRIG_RESP_ACTIVATOR = 0,
       EK_TRIG_RESP_MASTERSWITCH = 1 };

inline int ekm_trig_responder_kind(int statnum, int picnum) {
  if (statnum == EK_TRIG_STAT_ACTIVATOR) return EK_TRIG_RESP_ACTIVATOR;
  if (statnum == EK_STAT_STANDABLE && picnum == EK_TRIG_PIC_MASTERSWITCH)
    return EK_TRIG_RESP_MASTERSWITCH;
  return EK_TRIG_RESP_NONE;
}

// Only an activator can be locked (ACTIVATORLOCKED toggles a bit and opens nothing, sector.cpp:1126). The guard is on
// the KIND, so it holds for callers that derive one differently.
inline int ekm_trig_responder_locked(int responderKind, int picnum) {
  if (responderKind != EK_TRIG_RESP_ACTIVATOR) return 0;
  return picnum == EK_TRIG_PIC_ACTIVATORLOCKED ? 1 : 0;
}

// SECRET may be unreachable on real maps: a secret's lotag 32767 masks to 16383 and matches no ST_* case
// (sector.cpp:596), so chains resolve to OPENS. UNLOCKS (locked activator) wins over both. Kept: correct, costs
// nothing.
enum { EK_TRIG_SECRET = 0, EK_TRIG_OPENS = 1, EK_TRIG_UNLOCKS = 2 };

// World Tour's figure (trigger.h:20).
const int EK_TRIG_MAX_CHAINS = 32;

struct EkTrigChain { int triggerIndex, triggerKind, effect, sectnum, tx, ty, tz, cx, cy; };

inline int ekm_trig_effect(int operatedIsSecret, int operatedAdjoinsSecret, int activatorLocked) {
  if (activatorLocked) return EK_TRIG_UNLOCKS;
  if (operatedIsSecret) return EK_TRIG_SECRET;
  return EK_TRIG_OPENS;
}

// "Locked" names the state the player sees, not the verb.
inline const char* ekm_trig_label(int effect) {
  if (effect == EK_TRIG_SECRET) return "Secret";
  if (effect == EK_TRIG_UNLOCKS) return "Locked";
  return "Opens secret";
}

inline int ekm_trig_reaches_secret(int operatedIsSecret, int operatedAdjoinsSecret) {
  return operatedIsSecret || operatedAdjoinsSecret;
}

inline int ekm_trig_chain_fits(int staged, int max) {
  return staged < max;
}

// Join triggers to the activators sharing their lotag and stage chains reaching a secret. 1 Hz sampler only, never the
// frame path: entering a secret zeroes its lotag (sector.cpp:3293). Read-only; false means a scan target is not up yet.
bool secrettrig_scan(void);

// Acquire once, then pass `gen`: the cache is double-buffered and chains are REPACKED each scan, so index 3 of one
// generation is another chain in the other (secretvol.h has the argument). -1 before the first walk.
int secrettrig_generation(void);

int secrettrig_chains(int gen);
const EkTrigChain* secrettrig_chain(int gen, int i);

// seen: triggers classified, counted BEFORE capacity. refused: chains turned away by EK_TRIG_MAX_CHAINS. dropped:
// candidates lost to scratch capacity before the join. Two counters because they name different limits.
int secrettrig_seen(int gen);
int secrettrig_refused(int gen);
int secrettrig_dropped(int gen);
