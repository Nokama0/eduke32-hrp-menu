// Secret-trigger scan: which hidden trigger operates which sector, and whether it is or borders a secret. The join is
// by lotag only: a plate calls G_OperateActivators(lotag) (actors.cpp:2672), which operates each matching activator's
// OWN sector (sector.cpp:1117-1136). Many-to-many, resolved here.

#include <windows.h>
#include "secrettrig.h"
#include "gamestate.h"
#include "sectorstate.h"
#include "wallstate.h"
#include "esp_switches.h"

// Scratch capacity, well above any real map; overflow is counted separately (`dropped`) from a chain refused at the
// cap. 12 KB .bss.
const int EK_TRIG_SCAN_TRIGGERS   = 256;
const int EK_TRIG_SCAN_RESPONDERS = 512;

struct EkTrigTrigger { int index, kind, lotag, x, y, z; };

// Resolved once per responder, not per pair, bounding engine reads at one sector walk per matched responder. `ok` is 0
// until the loop reads whole, so a half-read loop never yields a centre.
struct EkTrigResponder {
  int lotag, sectnum, locked;
  int resolved, ok, isSecret, adjoins, cx, cy;
};

// Two generations (as secretvol.cpp): secrettrig_chain returns a POINTER, so the writer fills the idle buffer and
// flips. Chains are REPACKED each scan, so a reader keeps one generation's index throughout.
static EkTrigChain s_chains[2][EK_TRIG_MAX_CHAINS];
static int s_chainCount[2];
static int s_refusedCount[2];
static int s_droppedCount[2];
static int s_seenCount[2];

static EkTrigTrigger s_trigs[EK_TRIG_SCAN_TRIGGERS];
static EkTrigResponder s_resps[EK_TRIG_SCAN_RESPONDERS];

// -1 until the first walk. Runs at 1 Hz: placement is fixed per map and a found secret's lotag is zeroed
// (sector.cpp:3293).
static volatile LONG s_live = -1;

static int trig_gen_ok(int gen) { return gen == 0 || gen == 1; }

// Is the responder's sector a secret or next to one, and where is its centre. Walked as esp_collect_secrets does
// (esp.cpp:343-354) so the line meets the secret cross; the neighbour test is ekm_sector_is_secret, shared so the rules
// cannot drift.
static void trig_resolve_responder(EkTrigResponder* a) {
  if (a->resolved) return;
  a->resolved = 1;

  EkSectorInfo si = { 0, 0, 0, 0, 0, 0, 0, 0 };
  if (!sectorstate_info(a->sectnum, &si)) return;
  a->isSecret = ekm_sector_is_secret(si.lotag) ? 1 : 0;

  EkSectorCentre c = { 0, 0, 0, 0, 0, 0 };
  int accepted = 0;
  int adjoins = 0;

  for (int k = 0; k < si.wallnum; k++) {
    const int wi = si.wallptr + k;

    // point2 tested before the corner: an index outside its own sector's span is a corrupt record.
    int p2 = 0;
    if (!wallstate_point2(wi, &p2)) return;
    if (!ekm_point2_ok(p2, wi, si.wallptr, si.wallnum)) return;

    EkWallCorner wc = { 0, 0, 0 };
    if (!wallstate_corner(wi, &wc)) return;

    if (!adjoins && wc.nextsector >= 0) {
      EkSectorInfo nsi = { 0, 0, 0, 0, 0, 0, 0, 0 };
      if (sectorstate_info(wc.nextsector, &nsi) && ekm_sector_is_secret(nsi.lotag))
        adjoins = 1;
    }

    ekm_sector_centre_accumulate(&c, wc.x, wc.y);
    accepted++;
  }

  if (!ekm_sector_centre_finish(&c, accepted)) return;

  a->adjoins = adjoins;
  a->cx = c.x;
  a->cy = c.y;
  a->ok = 1;
}

// One trigger may match two responders in the SAME sector: one chain drawn twice, two of 32 slots spent. The merge
// keeps lock precedence, since ekm_trig_effect makes UNLOCKS win over a secret sector. Returns 1 when already staged.
static int trig_merge_duplicate(EkTrigChain* chains, int staged,
                                int triggerIndex, int sectnum, int effect) {
  for (int i = 0; i < staged; i++) {
    if (chains[i].triggerIndex != triggerIndex) continue;
    if (chains[i].sectnum != sectnum) continue;
    if (effect == EK_TRIG_UNLOCKS) chains[i].effect = EK_TRIG_UNLOCKS;
    return 1;
  }
  return 0;
}

bool secrettrig_scan(void) {
  if (!sectorstate_ready() || !wallstate_ready()) return false;
  if (!gamestate_sprite(0)) return false;

  // Sole writer: gamestate.cpp's scan_thread.
  const LONG live = s_live;
  const int next = (live == 0) ? 1 : 0;
  EkTrigChain* const chains = s_chains[next];

  int ntrig = 0, nresp = 0, seen = 0, refused = 0, dropped = 0;

  // The two role tests are independent, not else-branches: a switch tile on STAT_ACTIVATOR is both.
  for (int i = 0; i < EK_MAXSPRITES; i++) {
    const unsigned char* const r = gamestate_sprite(i);
    if (!r) break;

    const int statnum = (int)*(const short*)(r + EK_SPR_STATNUM);
    const int picnum  = (int)*(const short*)(r + EK_SPR_PICNUM);
    const int lotag   = (int)*(const short*)(r + EK_SPR_LOTAG);

    // Responder: an activator or a master switch (actors.cpp:2671; it arms, then operates its sector, actors.cpp:2556).
    // A fired one (yvel 1, sector.cpp:1181) is still collected: the secret stays one until entered. Argument order is
    // unpinnable: statnum first, picnum second.
    const int respKind = ekm_trig_responder_kind(statnum, picnum);
    if (respKind != EK_TRIG_RESP_NONE) {
      if (nresp < EK_TRIG_SCAN_RESPONDERS) {
        EkTrigResponder* const a = &s_resps[nresp++];
        a->lotag    = lotag;
        a->sectnum  = (int)*(const short*)(r + EK_SPR_SECTNUM);
        a->locked   = ekm_trig_responder_locked(respKind, picnum);
        a->resolved = 0;
        a->ok       = 0;
        a->isSecret = 0;
        a->adjoins  = 0;
        a->cx       = 0;
        a->cy       = 0;
      } else dropped++;
    }

    // Trigger: a touchplate is a picnum on STAT_STANDABLE (actors.cpp:2687); a switch needs a nonzero lotag
    // (sector.cpp:1284). Free slots keep picnum and lotag from the previous map, so the switch branch needs
    // ekm_trig_sprite_live.
    int kind = -1;
    if (picnum == EK_TRIG_PIC_TOUCHPLATE && statnum == EK_STAT_STANDABLE)
      kind = EK_TRIG_TOUCHPLATE;
    else if (lotag != 0 && ekm_trig_sprite_live(statnum) &&
             ekm_switch_base_for(picnum) >= 0)
      kind = EK_TRIG_SWITCH;
    if (kind < 0) continue;

    seen++;
    if (ntrig >= EK_TRIG_SCAN_TRIGGERS) { dropped++; continue; }

    EkTrigTrigger* const t = &s_trigs[ntrig++];
    t->index = i;
    t->kind  = kind;
    t->lotag = lotag;
    t->x     = *(const int*)(r + EK_SPR_X);
    t->y     = *(const int*)(r + EK_SPR_Y);
    t->z     = *(const int*)(r + EK_SPR_Z);
  }

  int staged = 0;
  for (int ti = 0; ti < ntrig; ti++) {
    const EkTrigTrigger& t = s_trigs[ti];

    for (int ai = 0; ai < nresp; ai++) {
      EkTrigResponder* const a = &s_resps[ai];
      if (a->lotag != t.lotag) continue;

      trig_resolve_responder(a);
      if (!a->ok) continue;

      if (!ekm_trig_reaches_secret(a->isSecret, a->adjoins)) continue;

      const int effect = ekm_trig_effect(a->isSecret, a->adjoins, a->locked);
      if (trig_merge_duplicate(chains, staged, t.index, a->sectnum, effect)) continue;

      // Asked only of a chain that would otherwise be staged, so the refusal count means something.
      if (!ekm_trig_chain_fits(staged, EK_TRIG_MAX_CHAINS)) { refused++; continue; }

      EkTrigChain* const ch = &chains[staged++];
      ch->triggerIndex = t.index;
      ch->triggerKind  = t.kind;
      ch->effect       = effect;
      ch->sectnum      = a->sectnum;
      ch->tx           = t.x;
      ch->ty           = t.y;
      ch->tz           = t.z;
      ch->cx           = a->cx;
      ch->cy           = a->cy;
    }
  }

  // Counts first, then the flip.
  s_chainCount[next]   = staged;
  s_refusedCount[next] = refused;
  s_droppedCount[next] = dropped;
  s_seenCount[next]    = seen;
  InterlockedExchange(&s_live, next);
  return true;
}

int secrettrig_generation(void) { return (int)s_live; }

int secrettrig_chains(int gen) {
  return trig_gen_ok(gen) ? s_chainCount[gen] : 0;
}

int secrettrig_refused(int gen) {
  return trig_gen_ok(gen) ? s_refusedCount[gen] : 0;
}

int secrettrig_dropped(int gen) {
  return trig_gen_ok(gen) ? s_droppedCount[gen] : 0;
}

int secrettrig_seen(int gen) {
  return trig_gen_ok(gen) ? s_seenCount[gen] : 0;
}

const EkTrigChain* secrettrig_chain(int gen, int i) {
  if (!trig_gen_ok(gen)) return 0;
  if (i < 0 || i >= s_chainCount[gen]) return 0;
  return &s_chains[gen][i];
}
