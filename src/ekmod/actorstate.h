#pragma once

// Death-record rule is scoped by picnum; that scope is its safety argument.
#include "esp_names.h"
#include "gamestate.h"

// actor_t actor[MAXSPRITES]: state the sprite lacks, e.g. queued damage (htextra) and the running CON action
// (t_data[4]). A direct .bss array (global.h:67). Packed, sizeof 96 (actors.h:128-146); fixed-width, so x64 does not
// move it.
const int EK_ACTOR_STRIDE = 96;

const int EK_ACT_T_DATA    = 0;
const int EK_ACT_ACTION_ID = 16;
// t_data[5] is AC_AI_ID (actors.h:104); zero until the actor's CON reached an `ai`, which `ifai 0` branches test.
const int EK_ACT_AI_ID     = 20;
const int EK_ACT_FLAGS     = 40;
const int EK_ACT_BPOS_X    = 44;
const int EK_ACT_BPOS_Y    = 48;
const int EK_ACT_BPOS_Z    = 52;
const int EK_ACT_FLOORZ    = 56;
const int EK_ACT_CEILINGZ  = 60;
const int EK_ACT_LASTV_X   = 64;
const int EK_ACT_LASTV_Y   = 68;
const int EK_ACT_HTPICNUM  = 72;
const int EK_ACT_HTANG     = 74;
const int EK_ACT_HTEXTRA   = 76;
const int EK_ACT_HTOWNER   = 78;
const int EK_ACT_MOVFLAG   = 80;
const int EK_ACT_TEMPANG   = 82;
const int EK_ACT_TIMETOSLEEP = 84;
const int EK_ACT_STAYPUT   = 86;

// bpos is set for every sprite at the head of G_MoveWorld (actors.cpp:9286-9331); the scan relies on sprites that did
// not move (bpos == xyz). A mover just misses. `htowner == i` is NOT an invariant (actors.cpp:1348, :3326).

inline bool ekm_actor_sample_usable(int statnum, int maxStatus, int x, int y) {
  return statnum >= 0 && statnum < maxStatus && x != 0 && y != 0;
}

const int EK_ACTOR_SAMPLES     = 128;
const int EK_ACTOR_MIN_SAMPLES = 32;

const int EK_ACTOR_MATCH_NUM = 3;
const int EK_ACTOR_MATCH_DEN = 4;

inline bool ekm_actor_scan_accept(int sampled, int matched) {
  if (sampled < EK_ACTOR_MIN_SAMPLES) return false;
  if (matched < 0 || matched > sampled) return false;
  return matched * EK_ACTOR_MATCH_DEN >= sampled * EK_ACTOR_MATCH_NUM;
}


// A sitting trooper takes damage late: LIZTROOPONTOILET and LIZTROOPJUSTSIT have no `ifhitweapon` (GAME.CON:4750-4777),
// so hits pile in htextra for 24 (30) tics until `cactor LIZTROOP`. Only these two gate the handover on `ifcount`; a
// general discount would let a tic of lag retire a live enemy.
const int EK_PICNUM_TROOP_ON_TOILET = 1741;
const int EK_PICNUM_TROOP_JUST_SIT  = 1742;
inline bool ekm_actor_defers_damage(int picnum) {
  return picnum == EK_PICNUM_TROOP_ON_TOILET || picnum == EK_PICNUM_TROOP_JUST_SIT;
}

// Instrumentation only: a trooper shot while sitting regained its ESP box for a few frames after leaving the toilet.
// Hypothesis: the discount is scoped to 1741/1742 and stops once it becomes 1680. Samples frames either side.
const int EK_PICNUM_TROOP_STANDING = 1680;

inline bool ekm_toilet_transition(int oldPicnum, int newPicnum) {
  return ekm_actor_defers_damage(oldPicnum) && newPicnum == EK_PICNUM_TROOP_STANDING;
}

const int EK_TOILET_TRACE_SLOTS  = 4;
const int EK_TOILET_TRACE_BEFORE = 4;
const int EK_TOILET_TRACE_AFTER  = 8;
const int EK_TOILET_TRACE_LINES  = 260;

// Oldest-first walk of the before-ring; -1 where it cannot answer, never slot 0.
inline int ekm_ring_oldest(int head, int count, int n, int k) {
  if (n <= 0 || count <= 0 || count > n) return -1;
  if (k < 0 || k >= count) return -1;
  int idx = (head - count + k) % n;
  if (idx < 0) idx += n;
  return idx;
}

// Nothing pending is -1, not 0 (actors.cpp:1305-1327; game.cpp:1389, :1485); 0 is a hit for no damage. The rule
// `pending > 0` fits both.
const int EK_HTEXTRA_NONE = -1;

// Health as the engine will have it: extra less queued damage, floored at 0. Freeze blasts use `ifwasweapon
// FREEZEBLAST` (GAME.CON:4372), so a hit sitter can read dead. The deferral follows the ACTOR, not its picnum: a trace
// showed 83 queued, the picnum becoming 1680, and the trooper reading alive for six frames.
inline bool ekm_actor_discounts_pending(int picnum, int sticky) {
  return ekm_actor_defers_damage(picnum) ||
         (sticky != 0 && picnum == EK_PICNUM_TROOP_STANDING);
}

// Flag after one observation: (1) identity: deletesprite parks a freed record at MAXSTATUS and keeps its picnum
// (engine.cpp:8825), so any other tile is a different occupant; (2) a failed read is inert; (3) queue cleared: pending
// <= 0; (4) set only by a sitter. A new level clears every flag (sprites stamped -1 on insert, game.cpp:1389, :1485).
inline int ekm_defer_sticky_next(int prev, int picnum, int statnum,
                                 int pendingKnown, int pending) {
  if (statnum < 0 || statnum >= EK_MAXSTATUS) return 0;
  if (!ekm_actor_defers_damage(picnum) && picnum != EK_PICNUM_TROOP_STANDING) return 0;
  if (!pendingKnown) return prev != 0 ? 1 : 0;
  if (pending <= 0) return 0;
  if (ekm_actor_defers_damage(picnum)) return 1;
  return prev != 0 ? 1 : 0;
}

// `sticky` is the per-actor flag; zero is the old rule.
inline int ekm_actor_effective_extra(int picnum, int extra, int pending,
                                     int sticky = 0) {
  if (!ekm_actor_discounts_pending(picnum, sticky) || pending <= 0) return extra;
  const int left = extra - pending;
  return left > 0 ? left : 0;
}


// Display only: the ESP number follows queued damage. Must not reach targeting (every actor lags a tic before
// `ifhitweapon`). A negative extra passes through.
inline int ekm_actor_shown_extra(int extra, int pending) {
  if (pending <= 0) return extra;
  const int left = extra - pending;
  return left > 0 ? left : 0;
}


// t_data[4] indexes apScript in intptr_t units (gameexec.cpp:6940, gamedef.cpp:3177; global.h:139): 8 bytes.
const int EK_ACTION_UNIT = 8;

// Six stored params, not the five an `action` line declares (actors.h:106-114: startframe, numframes, viewtype, incval,
// delay, flags), zero-padded (gamedef.cpp:3183).
const int EK_ACTION_PARAMS = 6;

// Stride 8: CON_ACTION, six params, CON_END (gamedef.cpp:3150-3190); labelcode points at the first param, so t4 -/+
// stride name the neighbours.
const int EK_ACTION_STRIDE = 8;

// The five declared values plus read status; `readable` and `plausible` differ. int flags (MSVC /O2 bool loss).
struct EkAction {
  int startframe, numframes, viewtype, incval, delay;
  int readable;
  int plausible;
};

// Does the decoder look like action declarations; all-zero is legal and common. Bounds from this GAME.CON's 402
// actions: viewtype in {1,4,5,7} (236 leave it 0), numframes 1..20, startframe -10..395. The WT fork admits 3 and 8 and
// rejects 4.
inline bool ekm_action_plausible(int startframe, int numframes, int viewtype) {
  const bool viewtypeOk = viewtype == 1 || viewtype == 4 || viewtype == 5 || viewtype == 7;
  const bool numframesOk = numframes >= 1 && numframes <= 64;
  const bool startframeOk = startframe >= -64 && startframe <= 1023;
  return viewtypeOk && numframesOk && startframeOk;
}

// Never AND `a.plausible` into record equality: the WT fork did and silently retired its play-dead indicator
// (ATROOPPLAYDEAD is deliberately (54, 0, 0, 0, 0)). tb_0044 pins it.
inline bool ekm_action_is(const EkAction& a, int sf, int nf, int vt, int iv, int dl) {
  return a.readable != 0 && a.startframe == sf && a.numframes == nf &&
         a.viewtype == vt && a.incval == iv && a.delay == dl;
}

// Index 0 means no opinion: actionless actors have t_data[4] zero (engine bails on actionofs == 0, gameexec.cpp:6944).
// Below one stride there is no predecessor.
inline bool ekm_action_id_usable(int t4) {
  return t4 >= EK_ACTION_STRIDE;
}

// Trooper down-states (GAME.CON:4018-4022): ATROOPDYING 50 5 1 1 16; ATROOPDEAD 54; ATROOPPLAYDEAD 54; ATROOPSUFFERDEAD
// 58 2 1 -4 24; ATROOPSUFFERING 59 2 1 1 21.
inline bool ekm_action_is_suffering(const EkAction& a) {
  return ekm_action_is(a, 59, 2, 1, 1, 21);
}

// Three answers: cannot tell (unlocated, or t_data[4] zero) must not read as not suffering.
const int EKM_SUFFER_UNKNOWN = 0;
const int EKM_SUFFER_YES     = 1;
const int EKM_SUFFER_NO      = -1;

// Play-dead and a real corpse are identical in every sprite field and in record contents (ATROOPDEAD vs
// ATROOPPLAYDEAD); they separate by position: playing dead iff the predecessor is also down and the successor is
// SUFFERDEAD.
inline bool ekm_action_is_playdead(const EkAction& self, const EkAction& before,
                                   const EkAction& after) {
  return ekm_action_is(self, 54, 0, 0, 0, 0) &&
         ekm_action_is(before, 54, 0, 0, 0, 0) &&
         ekm_action_is(after, 58, 2, 1, -4, 24);
}

// The action record outranks `extra`, which only moves on the victim's own `ifhitweapon`. Three answers, as
// EKM_SUFFER_*.
const int EKM_DYING_UNKNOWN = 0;
const int EKM_DYING_YES     = 1;
const int EKM_DYING_NO      = -1;

// COMMANDERSTAYPUT (1921) runs `cactor COMMANDER` on its first tic (GAME.CON:7208), so one picnum covers both.
const int EK_PICNUM_COMMANDER = 1920;

// From this GAME.CON: ATROOPDYING 50 5 1 1 16 (:4018), ATROOPSUFFERDEAD 58 2 1 -4 24 (:4021), ACOMMDYING 30 8 1 1 12
// (:7126), ACOMMDEAD 38 1 1 1 1 (:7127). The (54, 0, 0, 0, 0) records are left out, or every play-dead trooper reads
// dying.
inline bool ekm_action_is_troop_dying(const EkAction& a) {
  return ekm_action_is(a, 50, 5, 1, 1, 16) || ekm_action_is(a, 58, 2, 1, -4, 24);
}
// A dying commander is hittable for about three seconds with health flickering (GAME.CON:7372-7392); a health threshold
// hid two commanders still fighting.
inline bool ekm_action_is_commander_dying(const EkAction& a) {
  return ekm_action_is(a, 30, 8, 1, 1, 12) || ekm_action_is(a, 38, 1, 1, 1, 1);
}

// Picnum scope is the safety argument: AOCTADEAD (GAME.CON:5450) declares the same values as ACOMMDEAD. No plausibility
// gate.
inline bool ekm_action_is_dying(int picnum, const EkAction& a) {
  if (ekm_picnum_is_trooper(picnum)) return ekm_action_is_troop_dying(a);
  if (picnum == EK_PICNUM_COMMANDER) return ekm_action_is_commander_dying(a);
  return false;
}

enum EkActorState { EKA_ALIVE = 0, EKA_CHOKING, EKA_PLAYDEAD, EKA_DOWN };

inline bool ekm_actor_state_shown(int s) { return s != EKA_DOWN; }
inline bool ekm_actor_state_targetable(int s) {
  return s == EKA_ALIVE || s == EKA_CHOKING;
}
inline const char* ekm_actor_state_label(int s) {
  switch (s) {
    case EKA_CHOKING:  return "Choking";
    case EKA_PLAYDEAD: return "Playing dead";
    default:           return 0;
  }
}

bool actorstate_scan(void);
bool actorstate_ready(void);

unsigned actorstate_rva(void);

const unsigned char* actorstate_record(int i);

bool actorstate_pending_damage(int i, int* out);

// Has the CON run its first tic? Display only. A refusal answers "has run", leaving the sprite's health.
bool actorstate_has_run(int i);

// apScript, solved from g_tile's execPtr and confirmed by two known action records.
bool actorstate_script_scan(void);
bool actorstate_script_ready(void);

bool actorstate_is_playing_dead(int i, int picnum);

// UNKNOWN whenever anything is unknown; troopers only.
int actorstate_suffering(int i, int picnum);

// UNKNOWN keeps the sprite-field verdict.
int actorstate_dying_record(int i, int picnum);

// Trace only.
bool actorstate_action_at(int i, int* t4, EkAction* out);
