// The one list of things that can be shot, shared by the ESP and the aimbot so a filter fix cannot reach only one copy.

#include "targets.h"
#include "gamestate.h"
#include "tiles.h"
#include "badguy.h"
#include "actorstate.h"
#include "playerstate.h"
#include "projmath.h"
#include "config.h"
#include "log.h"
#include "iat.h"

#include <windows.h>
#include <stdio.h>

// A refusal is silent (no box, no lock), so this names the term that rejected a sprite; instrumentation only. Rate
// limited PER PICNUM (a global limit is spent on whichever corpse came first), first-come with the last slot a shared
// overflow.
static const int EK_REFUSE_SLOTS = 32;
static const unsigned EK_REFUSE_MS = 1000;

static void targets_log_refusal(const unsigned char* r, int sprite, int picnum,
                                int statnum, int extra, const char* term) {
  static short s_pic[EK_REFUSE_SLOTS];
  static unsigned s_ms[EK_REFUSE_SLOTS];
  static int s_used = 0;

  int slot = -1;
  for (int i = 0; i < s_used; i++)
    if (s_pic[i] == (short)picnum) { slot = i; break; }
  if (slot < 0) {
    if (s_used < EK_REFUSE_SLOTS - 1) {
      slot = s_used++;
      s_pic[slot] = (short)picnum;
      s_ms[slot] = 0;
    } else {
      slot = EK_REFUSE_SLOTS - 1;
    }
  }

  const unsigned now = GetTickCount();
  if (s_ms[slot] != 0 && (unsigned)(now - s_ms[slot]) < EK_REFUSE_MS) return;
  s_ms[slot] = now;

  ek_log("target refused: sprite %d picnum %d statnum %d -- %s "
         "(cstat 0x%04X extra %d pal %d xrep %d yrep %d)",
         sprite, picnum, statnum, term,
         (unsigned)*(const unsigned short*)(r + EK_SPR_CSTAT), extra,
         (int)r[EK_SPR_PAL], (int)r[EK_SPR_XREPEAT], (int)r[EK_SPR_YREPEAT]);
}

// Cumulative counts beside the samples: a sample shows one refusal, only a count shows a rate. Printed on the aimbot's
// `aim acquire:` line; incremented only with the Debug log row on.
static unsigned s_refuseNotEnemy = 0, s_refuseCritter = 0;
static unsigned s_refuseZeroRep  = 0, s_refuseDown    = 0;

void targets_refusal_summary(char* buf, int n) {
  if (!buf || n <= 0) return;
  buf[0] = 0;
  _snprintf_s(buf, (size_t)n, _TRUNCATE,
              "notenemy %u, critter %u, zerorep %u, down %u",
              s_refuseNotEnemy, s_refuseCritter, s_refuseZeroRep, s_refuseDown);
}

// Toilet-transition trace (hypothesis in actorstate.h): log lines only, no verdict. Sampled once per FRAME
// (targets_collect runs twice a frame, aim_frame then esp_draw) using iat_swap_frames; one render thread, so a plain
// static.
struct ToiletSample {
  long frame;
  int  sprite_index, picnum, rawExtra, judgedExtra, pending, t4, state, dyingRecord;
  int  cstat;
  EkAction rec;
};
struct ToiletTrack {
  int  used, sprite, lastPicnum, after, head, count;
  long lastFrame;
  ToiletSample ring[EK_TOILET_TRACE_BEFORE];
};
static ToiletTrack s_toilet[EK_TOILET_TRACE_SLOTS];
static int s_toiletLines = 0;

static void toilet_log_sample(const ToiletSample& s, const char* when) {
  if (s_toiletLines >= EK_TOILET_TRACE_LINES) return;
  s_toiletLines++;
  ek_log("toilet trace %-6s frame %ld sprite %d picnum %d cstat 0x%04X "
         "extra %d (raw %d) htextra %d | t4 %d rec (%d,%d,%d,%d,%d) readable %d "
         "| dyingRecord %d state %d shown %d",
         when, s.frame, s.sprite_index, s.picnum, (unsigned)s.cstat,
         s.judgedExtra, s.rawExtra, s.pending, s.t4,
         s.rec.startframe, s.rec.numframes, s.rec.viewtype, s.rec.incval,
         s.rec.delay, s.rec.readable, s.dyingRecord, s.state,
         ekm_actor_state_shown(s.state) ? 1 : 0);
}

static void toilet_trace(int sprite, int picnum, int cstat, int rawExtra,
                         int judgedExtra, int pending, int state,
                         int dyingRecord) {
  if (!g_cfg.debuglog) return;
  if (s_toiletLines >= EK_TOILET_TRACE_LINES) return;
  const bool sitting = ekm_actor_defers_damage(picnum);
  if (!sitting && picnum != EK_PICNUM_TROOP_STANDING) return;

  ToiletTrack* t = NULL;
  for (int k = 0; k < EK_TOILET_TRACE_SLOTS; k++)
    if (s_toilet[k].used && s_toilet[k].sprite == sprite) { t = &s_toilet[k]; break; }
  if (!t) {
    if (!sitting) return;
    for (int k = 0; k < EK_TOILET_TRACE_SLOTS; k++) {
      if (s_toilet[k].used) continue;
      ToiletTrack z = {};
      s_toilet[k] = z;
      s_toilet[k].used = 1;
      s_toilet[k].sprite = sprite;
      s_toilet[k].lastPicnum = picnum;
      t = &s_toilet[k];
      break;
    }
    if (!t) return;
  }

  const long frame = iat_swap_frames();
  // Second call this frame dropped. Not gated on the ring: the burst clears it and the following frames would log
  // twice. A fresh lastFrame is 0 and iat_swap_frames starts at 1.
  if (t->lastFrame == frame) return;

  ToiletSample s = {};
  s.frame = frame;
  s.sprite_index = sprite;
  s.picnum = picnum;
  s.cstat = cstat;
  s.rawExtra = rawExtra;
  s.judgedExtra = judgedExtra;
  s.pending = pending;
  s.state = state;
  s.dyingRecord = dyingRecord;
  actorstate_action_at(sprite, &s.t4, &s.rec);

  if (t->after > 0) {
    toilet_log_sample(s, "after");
    if (--t->after == 0) { ToiletTrack z = {}; *t = z; return; }
  } else if (ekm_toilet_transition(t->lastPicnum, picnum)) {
    for (int k = 0; k < t->count; k++) {
      const int idx = ekm_ring_oldest(t->head, t->count, EK_TOILET_TRACE_BEFORE, k);
      if (idx >= 0) toilet_log_sample(t->ring[idx], "before");
    }
    toilet_log_sample(s, "change");
    t->count = 0;
    t->head = 0;
    t->after = EK_TOILET_TRACE_AFTER;
  } else if (sitting) {
    t->ring[t->head] = s;
    t->head = (t->head + 1) % EK_TOILET_TRACE_BEFORE;
    if (t->count < EK_TOILET_TRACE_BEFORE) t->count++;
  } else {
    ToiletTrack z = {};
    *t = z;
    return;
  }

  t->lastPicnum = picnum;
  t->lastFrame = frame;
}

// One byte per sprite slot; actorstate.h has the rule and the rejected alternatives. Only the collector writes it.
static unsigned char s_deferSticky[EK_MAXSPRITES];

// Cleared on level change so the recycling argument need not be right to be safe: the sweep stops at the first
// unreadable slot and a map load recycles every index at once.
static void targets_forget_deferrals(void) {
  for (int i = 0; i < EK_MAXSPRITES; i++) s_deferSticky[i] = 0;
}

int targets_collect(EkTarget* out, int max) {
  int count = 0;

  // ekm_level_loading, not !esp_level_loaded: a PAUSE must not clear the flag and give a sitting trooper its health
  // back.
  {
    int gm = 0;
    const bool gmOk = player_read_gm(&gm);
    if (ekm_level_loading(gm, gmOk)) targets_forget_deferrals();
  }

  for (int i = 0; i < EK_MAXSPRITES; i++) {
    // The collection cap is not the sweep's: a full list stops collecting but keeps sweeping, else a stranded flag is
    // inherited by the next sprite in the slot and HIDES A LIVE ENEMY.
    const bool sweepOnly = count >= max;
    if (sweepOnly && !s_deferSticky[i]) continue;

    const unsigned char* r = gamestate_sprite(i);
    if (!r) break;

    const short statnum = *(const short*)(r + EK_SPR_STATNUM);

    // Swept for EVERY slot: deletesprite parks a freed record at MAXSTATUS, so a dead sitter's slot never passes the
    // filter again and its flag would be inherited.
    if (s_deferSticky[i])
      s_deferSticky[i] = (unsigned char)ekm_defer_sticky_next(
          1, (int)*(const short*)(r + EK_SPR_PICNUM), (int)statnum, 0, 0);

    if (sweepOnly) continue;

    if (statnum != EK_STAT_ACTOR && statnum != EK_STAT_ZOMBIE) continue;

    // The statnum term is not instrumented: it rejects most sprites in the level and says nothing about an enemy.
    const short picnum = *(const short*)(r + EK_SPR_PICNUM);
    // The stayput term is ours: g_tile carries no badguy bit for variant tiles (esp_names.h), so the engine alone hides
    // a real enemy until Duke swaps its picnum.
    if (badguy_ready() && !picnum_is_enemy(picnum) && !ekm_picnum_is_stayput(picnum)) {
      if (g_cfg.debuglog) {
        targets_log_refusal(r, i, picnum, statnum,
                            *(const short*)(r + EK_SPR_EXTRA),
                            "picnum is not an enemy tile (g_tile)");
        s_refuseNotEnemy++;
      }
      continue;
    }
    if (picnum_is_harmless_critter(picnum)) {
      if (g_cfg.debuglog) {
        targets_log_refusal(r, i, picnum, statnum,
                            *(const short*)(r + EK_SPR_EXTRA),
                            "harmless critter");
        s_refuseCritter++;
      }
      continue;
    }

    const unsigned char xrep = r[EK_SPR_XREPEAT];
    const unsigned char yrep = r[EK_SPR_YREPEAT];
    if (xrep == 0 || yrep == 0) {
      if (g_cfg.debuglog) {
        targets_log_refusal(r, i, picnum, statnum,
                            *(const short*)(r + EK_SPR_EXTRA),
                            "xrepeat or yrepeat is zero");
        s_refuseZeroRep++;
      }
      continue;
    }
    const unsigned short cstat = *(const unsigned short*)(r + EK_SPR_CSTAT);

    // Liveness was wrong three times by reading the engine; stock GAME.CON settles it. The engine's dead predicate is
    // `extra <= 0` (CON_IFDEAD, gameexec.cpp:3325); `pal != 1` is the frozen enemy; the cstat-cleared test is scoped to
    // troopers (Enforcers, Octabrains, Drones zero strength without clearing cstat); see docs/re/target-liveness.md.
    const short rawExtra = *(const short*)(r + EK_SPR_EXTRA);
    const unsigned char pal = r[EK_SPR_PAL];

    // The sitting trooper's queued damage is discounted HERE so ESP, aimbot and triggerbot see one health number
    // (actorstate.h has the rule). Read for every enemy because the ESP number wants it; the targeting number is
    // unchanged.
    int pending = EK_HTEXTRA_NONE;
    const bool pendingKnown = actorstate_pending_damage(i, &pending);
    if (!pendingKnown) pending = EK_HTEXTRA_NONE;

    s_deferSticky[i] = (unsigned char)ekm_defer_sticky_next(
        s_deferSticky[i], picnum, (int)statnum, pendingKnown ? 1 : 0, pending);
    const int sticky = s_deferSticky[i];
    const short extra =
        (short)ekm_actor_effective_extra(picnum, rawExtra, pending, sticky);

    // Display only: where the discount is already in `extra`, subtracting again would count the queue twice. One
    // predicate decides both.
    const short hpShown = (short)ekm_actor_shown_extra(
        extra, ekm_actor_discounts_pending(picnum, sticky) ? EK_HTEXTRA_NONE
                                                           : pending);

    // A trooper playing dead stays in this list but is not a target: every sprite field says corpse, so only the CON
    // action record can see it, decoded only for a trooper the filter would otherwise drop. The aimbot refuses it by
    // its cstat.
    const bool couldBeDown = ekm_picnum_is_trooper(picnum) && extra <= 0 &&
                             !ekm_target_hittable(xrep, cstat);
    const bool playDead = couldBeDown && actorstate_is_playing_dead(i, picnum);

    const int suffering =
        (ekm_picnum_is_trooper(picnum) && extra == 0 &&
         ekm_target_hittable(xrep, cstat)) ? actorstate_suffering(i, picnum)
                                           : EKM_SUFFER_UNKNOWN;


    // The death record is asked only for health that still reads alive; scoped to the trooper family and the Commander.
    const int dyingRecord =
        (extra > 0 && (ekm_picnum_is_trooper(picnum) || picnum == EK_PICNUM_COMMANDER))
            ? actorstate_dying_record(i, picnum) : EKM_DYING_UNKNOWN;

    const int state = ekm_actor_state(picnum, extra, pal, cstat, xrep, playDead,
                                      suffering, dyingRecord);

    toilet_trace(i, picnum, (int)cstat, (int)rawExtra, (int)extra, pending,
                 state, dyingRecord);

    if (!ekm_actor_state_shown(state)) {
      // The first candidate for the Recon report: a picnum with no CON `actor` line has no strength, so `extra <= 0`
      // reads it as a corpse.
      if (g_cfg.debuglog) {
        targets_log_refusal(r, i, picnum, statnum, extra,
                            "state is down (dying, dead or playing dead)");
        s_refuseDown++;
      }
      continue;
    }

    EkTarget& t = out[count];
    t.sprite = i;
    t.x = *(const int*)(r + EK_SPR_X);
    t.y = *(const int*)(r + EK_SPR_Y);
    t.z = *(const int*)(r + EK_SPR_Z);
    t.sect = *(const short*)(r + EK_SPR_SECTNUM);
    t.hp = extra;
    t.hpShown = hpShown;
    t.picnum = picnum;
    t.state = state;
    t.xrep = xrep;
    t.yrep = yrep;
    t.cstat = cstat;
    t.pal = pal;

    int tw = 0, th = 0;
    tile_size(picnum, &tw, &th);
    if (tw <= 0 || th <= 0) { tw = 64; th = 64; }
    t.tw = tw;
    t.th = th;

    t.heightZ = ekm_sprite_height_z(yrep, th);
    t.feetZ = ekm_feet_z(t.z, t.heightZ, cstat);

    count++;
  }

  return count;
}


// See targets.h. The collector's terms in its order, minus logging, plus the aimbot's hittable rule; only the sequence
// is written twice, and drift shows in the log.
bool targets_sprite_targetable(int sprite) {
  if (sprite < 0 || sprite >= EK_MAXSPRITES) return false;
  const unsigned char* const r = gamestate_sprite(sprite);
  if (!r) return false;

  const short statnum = *(const short*)(r + EK_SPR_STATNUM);
  if (statnum != EK_STAT_ACTOR && statnum != EK_STAT_ZOMBIE) return false;

  const short picnum = *(const short*)(r + EK_SPR_PICNUM);
  // The collector's stayput fallback, so the two cannot disagree about what an enemy is.
  if (badguy_ready() && !picnum_is_enemy(picnum) && !ekm_picnum_is_stayput(picnum))
    return false;
  if (picnum_is_harmless_critter(picnum)) return false;

  const unsigned char xrep = r[EK_SPR_XREPEAT];
  const unsigned char yrep = r[EK_SPR_YREPEAT];
  if (xrep == 0 || yrep == 0) return false;

  const unsigned short cstat = *(const unsigned short*)(r + EK_SPR_CSTAT);
  const short rawExtra = *(const short*)(r + EK_SPR_EXTRA);
  const unsigned char pal = r[EK_SPR_PAL];

  // The collector's flag, read and never written here: a second writer is a second lifecycle.
  const int sticky = s_deferSticky[sprite];
  int pending = EK_HTEXTRA_NONE;
  if (ekm_actor_discounts_pending(picnum, sticky))
    if (!actorstate_pending_damage(sprite, &pending)) pending = EK_HTEXTRA_NONE;
  const short extra =
      (short)ekm_actor_effective_extra(picnum, rawExtra, pending, sticky);

  const bool couldBeDown = ekm_picnum_is_trooper(picnum) && extra <= 0 &&
                           !ekm_target_hittable(xrep, cstat);
  const bool playDead = couldBeDown && actorstate_is_playing_dead(sprite, picnum);

  const int suffering =
      (ekm_picnum_is_trooper(picnum) && extra == 0 &&
       ekm_target_hittable(xrep, cstat)) ? actorstate_suffering(sprite, picnum)
                                         : EKM_SUFFER_UNKNOWN;


  const int dyingRecord =
      (extra > 0 && (ekm_picnum_is_trooper(picnum) || picnum == EK_PICNUM_COMMANDER))
          ? actorstate_dying_record(sprite, picnum) : EKM_DYING_UNKNOWN;

  const int state = ekm_actor_state(picnum, extra, pal, cstat, xrep, playDead,
                                    suffering, dyingRecord);
  if (!ekm_actor_state_shown(state)) return false;

  // The aimbot's own rule, last: a play-dead trooper arrives with cstat cleared and this refuses it.
  return ekm_target_hittable(xrep, cstat);
}

bool targets_sprite_picnum(int sprite, int* out) {
  if (!out || sprite < 0 || sprite >= EK_MAXSPRITES) return false;
  const unsigned char* const r = gamestate_sprite(sprite);
  if (!r) return false;
  *out = (int)*(const short*)(r + EK_SPR_PICNUM);
  return true;
}
