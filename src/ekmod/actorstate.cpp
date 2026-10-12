// Finds actor[] without an offset: each live sprite's position is mirrored in actor[i].bpos every tic
// (actors.cpp:9286-9331), so look for an array agreeing with sprite[] index by index. Needs non-zero coordinates and an
// exact three-int32 match. `htowner == i` is NOT an invariant: docs/re/actorstate.md.

#include <windows.h>
#include "log.h"
#include "gamestate.h"
#include "badguy.h"
#include "esp_names.h"
#include "actorstate.h"

// Scan thread writes, render reads: an aligned pointer store is atomic on x64, so a reader sees NULL or a validated
// array.
static const unsigned char* volatile s_actors = NULL;
static volatile LONG s_rva = 0;

struct Anchor { int index, x, y, z; };
static Anchor s_anchor[EK_ACTOR_SAMPLES];
static int s_anchors = 0;

struct Span { const unsigned char* base; const unsigned char* end; };
static Span s_writable[16];
static int s_writableCount = 0;

static void collect_writable_sections(void) {
  s_writableCount = 0;
  const unsigned char* const base = gamestate_module_base();
  if (!base) return;
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
  const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return;
  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  const int nsec = nt->FileHeader.NumberOfSections;
  const int cap = (int)(sizeof(s_writable) / sizeof(s_writable[0]));
  for (int i = 0; i < nsec && s_writableCount < cap; i++) {
    if (!(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
    s_writable[s_writableCount].base = base + sec[i].VirtualAddress;
    s_writable[s_writableCount].end  = base + sec[i].VirtualAddress + sec[i].Misc.VirtualSize;
    s_writableCount++;
  }
}

static bool in_writable_section(const unsigned char* p) {
  for (int i = 0; i < s_writableCount; i++)
    if (p >= s_writable[i].base && p < s_writable[i].end) return true;
  return false;
}

// Sampled off live sprites: only a sprite on a status list has bpos written this tic, and one away from both axes
// carries three int32s of evidence.
static void collect_anchors(void) {
  s_anchors = 0;
  for (int i = 0; i < EK_MAXSPRITES && s_anchors < EK_ACTOR_SAMPLES; i++) {
    const unsigned char* r = gamestate_sprite(i);
    if (!r) break;
    const short statnum = *(const short*)(r + EK_SPR_STATNUM);
    const int x = *(const int*)(r + EK_SPR_X);
    const int y = *(const int*)(r + EK_SPR_Y);
    if (!ekm_actor_sample_usable(statnum, EK_MAXSTATUS, x, y)) continue;
    s_anchor[s_anchors].index = i;
    s_anchor[s_anchors].x = x;
    s_anchor[s_anchors].y = y;
    s_anchor[s_anchors].z = *(const int*)(r + EK_SPR_Z);
    s_anchors++;
  }
}

static bool anchor_matches(const unsigned char* cand, const Anchor& a) {
  const unsigned char* const rec = cand + (size_t)a.index * EK_ACTOR_STRIDE;
  return *(const int*)(rec + EK_ACT_BPOS_X) == a.x &&
         *(const int*)(rec + EK_ACT_BPOS_Y) == a.y &&
         *(const int*)(rec + EK_ACT_BPOS_Z) == a.z;
}

// Two anchors at opposite ends of the sample: a candidate shifted by k records could satisfy one from a neighbour's
// stale bpos, not two hundreds apart. Six exact int32s keep the expensive pass off almost every candidate.
static bool cheap_probe(const unsigned char* cand) {
  return anchor_matches(cand, s_anchor[0]) &&
         anchor_matches(cand, s_anchor[s_anchors - 1]);
}

static int score(const unsigned char* cand) {
  int matched = 0;
  for (int i = 0; i < s_anchors; i++)
    if (anchor_matches(cand, s_anchor[i])) matched++;
  return matched;
}

static bool scan_now(void) {
  const size_t arrayBytes = (size_t)EK_MAXSPRITES * EK_ACTOR_STRIDE;

  collect_anchors();
  if (s_anchors < EK_ACTOR_MIN_SAMPLES) {
    ek_log("actorstate: %d usable anchor(s) of %d needed -- no level, or every "
           "live sprite sits on an axis; retrying", s_anchors, EK_ACTOR_MIN_SAMPLES);
    return false;
  }

  collect_writable_sections();
  gamestate_regions_refresh();

  // Volatile because written inside __try and read after __except: MSVC leaves a non-volatile local there undefined, so
  // a faulting region could hand a garbage `best` to the accept test.
  const unsigned char* volatile best = NULL;
  volatile int bestMatch = 0, probed = 0, examined = 0;

  // The walk is bounded by the region so every byte is committed and the 1.5MB span lies in one run. Stride is four,
  // not eight: actor_t is packed with an int32 as widest member, so 8-alignment is not guaranteed.
  const int regions = gamestate_region_count();
  for (int ri = 0; ri < regions; ri++) {
    const unsigned char* rbase = NULL;
    size_t rsize = 0;
    if (!gamestate_region_at(ri, &rbase, &rsize)) continue;
    if (rsize < arrayBytes) continue;
    const unsigned char* const last = rbase + rsize - arrayBytes;

    // One guard per region, not per candidate: __except is free until it fires, and a faulting region must cost that
    // region only.
    __try {
      for (const unsigned char* cand = rbase; cand <= last; cand += 4) {
        if (!in_writable_section(cand)) continue;
        examined++;
        if (!cheap_probe(cand)) continue;
        probed++;
        const int matched = score(cand);
        if (matched > bestMatch) { bestMatch = matched; best = cand; }
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      ek_log("actorstate: region %d faulted mid-walk; skipping the rest of it", ri);
    }
  }

  if (!best || !ekm_actor_scan_accept(s_anchors, bestMatch)) {
    ek_log("actorstate: not found. %d slot(s) examined across %d region(s), "
           "%d passed the two-anchor probe, best %d of %d anchor(s) matched "
           "(%d needed)",
           examined, regions, probed, bestMatch, s_anchors,
           (s_anchors * EK_ACTOR_MATCH_NUM + EK_ACTOR_MATCH_DEN - 1) / EK_ACTOR_MATCH_DEN);
    return false;
  }

  const unsigned rva = (unsigned)(best - gamestate_module_base());
  InterlockedExchange(&s_rva, (LONG)rva);
  s_actors = best;
  ek_log("actorstate: actor[] at rva 0x%07X -- %d of %d anchor(s) matched, "
         "%d slot(s) examined, %d passed the probe",
         rva, bestMatch, s_anchors, examined, probed);
  return true;
}

bool actorstate_scan(void) {
  if (s_actors) return true;
  if (!gamestate_ready()) return false;
  return scan_now();
}

bool actorstate_ready(void) { return s_actors != NULL; }
unsigned actorstate_rva(void) { return (unsigned)InterlockedCompareExchange(&s_rva, 0, 0); }

const unsigned char* actorstate_record(int i) {
  const unsigned char* const arr = s_actors;
  if (!arr || i < 0 || i >= EK_MAXSPRITES) return NULL;
  return arr + (size_t)i * EK_ACTOR_STRIDE;
}

// Guarded like every engine read. False means "nothing known", not "nothing pending": a failed read must leave health
// alone. apScript is solved for, not scanned: the base is the one address in execPtr's allocation where two known
// action records decode exactly (GAME.CON:4780/4008, :4740/4010); stride checked first (docs/re/actorstate.md).
struct ScriptAnchor {
  int picnum, strength;
  int sf, nf, vt, iv, dl;
};
static const ScriptAnchor kScriptAnchors[2] = {
  { 1680, 30,  0, 1, 5, 1, 1 },
  { 1682, 30, -2, 1, 5, 1, 1 },
};
// ATROOPSTAND :4008, ATROOPGROW :4009, ATROOPSTAYSTAND :4010.
static const int kAnchorDeclsApart = 2;

static const unsigned char* volatile s_script = NULL;
static const unsigned char* volatile s_scriptEnd = NULL;

// The committed run containing `p`, walked out to the whole allocation; a few VirtualQuery calls once.
static bool script_block(const unsigned char* p, const unsigned char** outBase,
                         const unsigned char** outEnd) {
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
  if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
  const void* const alloc = mbi.AllocationBase;
  const unsigned char* base = (const unsigned char*)mbi.BaseAddress;
  const unsigned char* end = base + mbi.RegionSize;

  for (int guard = 0; guard < 4096; guard++) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery(end, &m, sizeof(m)) || m.RegionSize == 0) break;
    if (m.AllocationBase != alloc || m.State != MEM_COMMIT) break;
    if (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)) break;
    end = (const unsigned char*)m.BaseAddress + m.RegionSize;
  }
  for (int guard = 0; guard < 4096 && base > (const unsigned char*)alloc; guard++) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery(base - 1, &m, sizeof(m)) || m.RegionSize == 0) break;
    if (m.AllocationBase != alloc || m.State != MEM_COMMIT) break;
    if (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)) break;
    base = (const unsigned char*)m.BaseAddress;
  }
  *outBase = base;
  *outEnd = end;
  return true;
}

// Bounds-checked on every read: the record's six units must lie inside the block.
static EkAction decode_at(const unsigned char* script, const unsigned char* end, int id) {
  EkAction a;
  a.startframe = a.numframes = a.viewtype = a.incval = a.delay = 0;
  a.readable = a.plausible = 0;
  if (!script || id < 0) return a;
  const unsigned char* const rec = script + (size_t)id * EK_ACTION_UNIT;
  if (rec < script) return a;
  if (rec + (size_t)EK_ACTION_PARAMS * EK_ACTION_UNIT > end) return a;
  __try {
    const __int64* const v = (const __int64*)rec;
    a.startframe = (int)v[0];
    a.numframes  = (int)v[1];
    a.viewtype   = (int)v[2];
    a.incval     = (int)v[3];
    a.delay      = (int)v[4];
    a.readable   = 1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    a.readable = 0;
    return a;
  }
  a.plausible = ekm_action_plausible(a.startframe, a.numframes, a.viewtype) ? 1 : 0;
  return a;
}

static bool anchor_decodes(const unsigned char* script, const unsigned char* end,
                           const ScriptAnchor& an, int id) {
  const EkAction a = decode_at(script, end, id);
  return ekm_action_is(a, an.sf, an.nf, an.vt, an.iv, an.dl);
}

bool actorstate_script_scan(void) {
  if (s_script) return true;
  if (!badguy_ready()) return false;

  const intptr_t* exec[2] = { NULL, NULL };
  int id[2] = { 0, 0 };

  for (int i = 0; i < 2; i++) {
    const unsigned char* const rec = badguy_tile_record(kScriptAnchors[i].picnum);
    if (!rec) {
      ek_log("actorstate: no g_tile record for picnum %d", kScriptAnchors[i].picnum);
      return false;
    }
    __try {
      exec[i] = *(const intptr_t* const*)rec;
      if (!exec[i]) {
        ek_log("actorstate: picnum %d has no CON actor block (execPtr null) -- "
               "this is not the stock GAME.CON", kScriptAnchors[i].picnum);
        return false;
      }
      const int strength = (int)exec[i][0];
      id[i] = (int)exec[i][1];
      if (strength != kScriptAnchors[i].strength) {
        ek_log("actorstate: picnum %d declares strength %d, expected %d -- "
               "refusing to decode a CON these records do not describe",
               kScriptAnchors[i].picnum, strength, kScriptAnchors[i].strength);
        return false;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      ek_log("actorstate: reading g_tile[%d].execPtr faulted", kScriptAnchors[i].picnum);
      return false;
    }
  }

  const int gap = id[1] - id[0];
  if (gap != kAnchorDeclsApart * EK_ACTION_STRIDE) {
    ek_log("actorstate: action ids %d and %d are %d unit(s) apart, expected %d -- "
           "the declaration stride is not what this build's CON uses; not decoding",
           id[0], id[1], gap, kAnchorDeclsApart * EK_ACTION_STRIDE);
    return false;
  }

  const unsigned char* base = NULL;
  const unsigned char* end = NULL;
  const unsigned char* const execBytes = (const unsigned char*)exec[0];
  if (!script_block(execBytes, &base, &end)) {
    ek_log("actorstate: VirtualQuery refused the block holding execPtr");
    return false;
  }

  // apScript <= execPtr, so search from the allocation base up to execPtr for the address where both anchors decode.
  const unsigned char* found = NULL;
  int hits = 0, tried = 0;
  for (const unsigned char* cand = base; cand <= execBytes; cand += EK_ACTION_UNIT) {
    tried++;
    if (!anchor_decodes(cand, end, kScriptAnchors[0], id[0])) continue;
    if (!anchor_decodes(cand, end, kScriptAnchors[1], id[1])) continue;
    hits++;
    if (!found) found = cand;
  }

  if (!found) {
    ek_log("actorstate: apScript not solved. %d candidate(s) across %llu byte(s) "
           "of the block holding g_tile[%d].execPtr; ids %d and %d, %d apart as "
           "expected, but no base decodes both records",
           tried, (unsigned long long)(end - base), kScriptAnchors[0].picnum,
           id[0], id[1], gap);
    return false;
  }
  // Ambiguity is a refusal, not a tie-break: "nothing known" must never become an answer. Refusing leaves playing-dead
  // false (a cosmetic label), where the lowest base would decode every trooper from the wrong place with solved
  // confidence.
  if (hits > 1) {
    ek_log("actorstate: %d bases decoded both anchors -- refusing an ambiguous "
           "apScript rather than guessing; the play-dead label stays off", hits);
    return false;
  }

  s_scriptEnd = end;
  s_script = found;
  ek_log("actorstate: apScript at %p (block %llu bytes), confirmed by "
         "ATROOPSTAND at id %d and ATROOPSTAYSTAND at id %d, %d units apart",
         (const void*)found, (unsigned long long)(end - base), id[0], id[1], gap);
  return true;
}

bool actorstate_script_ready(void) { return s_script != NULL; }

// Troopers only: the records compared are the Assault Trooper's, and this keeps three decodes off every enemy each ESP
// frame.
bool actorstate_is_playing_dead(int i, int picnum) {
  const unsigned char* const script = s_script;
  const unsigned char* const end = s_scriptEnd;
  if (!script || !end) return false;
  if (!ekm_picnum_is_trooper(picnum)) return false;

  const unsigned char* const rec = actorstate_record(i);
  if (!rec) return false;

  int t4 = 0;
  __try {
    t4 = *(const int*)(rec + EK_ACT_ACTION_ID);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  if (!ekm_action_id_usable(t4)) return false;

  const EkAction self   = decode_at(script, end, t4);
  const EkAction before = decode_at(script, end, t4 - EK_ACTION_STRIDE);
  const EkAction after  = decode_at(script, end, t4 + EK_ACTION_STRIDE);
  return ekm_action_is_playdead(self, before, after);
}

// Choking, not dying: same scope, three-valued: UNKNOWN whenever the array, script or action id is unreadable (the
// caller keeps its old behaviour). ATROOPSUFFERING is unique, so one record suffices.
int actorstate_suffering(int i, int picnum) {
  const unsigned char* const script = s_script;
  const unsigned char* const end = s_scriptEnd;
  if (!script || !end) return EKM_SUFFER_UNKNOWN;
  if (!ekm_picnum_is_trooper(picnum)) return EKM_SUFFER_UNKNOWN;

  const unsigned char* const rec = actorstate_record(i);
  if (!rec) return EKM_SUFFER_UNKNOWN;

  int t4 = 0;
  __try {
    t4 = *(const int*)(rec + EK_ACT_ACTION_ID);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return EKM_SUFFER_UNKNOWN;
  }
  if (!ekm_action_id_usable(t4)) return EKM_SUFFER_UNKNOWN;

  const EkAction self = decode_at(script, end, t4);
  // An unreadable record is not a "no": answering NO would retire a live enemy.
  if (!self.readable) return EKM_SUFFER_UNKNOWN;
  return ekm_action_is_suffering(self) ? EKM_SUFFER_YES : EKM_SUFFER_NO;
}

// Dying: scope is wider by the Assault Commander, whose dying phase health cannot settle (`ifai AICOMMDYING` runs
// `strength 0` every tic with cstat hittable, GAME.CON:7372-7392). ekm_action_is_dying answers false for other picnums.
int actorstate_dying_record(int i, int picnum) {
  const unsigned char* const script = s_script;
  const unsigned char* const end = s_scriptEnd;
  if (!script || !end) return EKM_DYING_UNKNOWN;
  if (!ekm_picnum_is_trooper(picnum) && picnum != EK_PICNUM_COMMANDER)
    return EKM_DYING_UNKNOWN;

  const unsigned char* const rec = actorstate_record(i);
  if (!rec) return EKM_DYING_UNKNOWN;

  int t4 = 0;
  __try {
    t4 = *(const int*)(rec + EK_ACT_ACTION_ID);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return EKM_DYING_UNKNOWN;
  }
  if (!ekm_action_id_usable(t4)) return EKM_DYING_UNKNOWN;

  const EkAction self = decode_at(script, end, t4);
  if (!self.readable) return EKM_DYING_UNKNOWN;
  return ekm_action_is_dying(picnum, self) ? EKM_DYING_YES : EKM_DYING_NO;
}

// Raw id and record for the trace only; false prints "no record", not a decoded-looking zero.
bool actorstate_action_at(int i, int* t4, EkAction* out) {
  if (out) {
    EkAction z;
    z.startframe = z.numframes = z.viewtype = z.incval = z.delay = 0;
    z.readable = z.plausible = 0;
    *out = z;
  }
  if (t4) *t4 = 0;
  const unsigned char* const script = s_script;
  const unsigned char* const end = s_scriptEnd;
  if (!script || !end || !out || !t4) return false;

  const unsigned char* const rec = actorstate_record(i);
  if (!rec) return false;

  int id = 0;
  __try {
    id = *(const int*)(rec + EK_ACT_ACTION_ID);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  *t4 = id;
  if (!ekm_action_id_usable(id)) return false;
  *out = decode_at(script, end, id);
  return out->readable != 0;
}

// The AI id, read and guarded like the pending-damage queue; the default is the safe `true` (actorstate.h).
bool actorstate_has_run(int i) {
  const unsigned char* const rec = actorstate_record(i);
  if (!rec) return true;
  __try {
    return *(const int*)(rec + EK_ACT_AI_ID) != 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return true;
  }
}

bool actorstate_pending_damage(int i, int* out) {
  const unsigned char* const rec = actorstate_record(i);
  if (!rec || !out) return false;
  __try {
    *out = *(const short*)(rec + EK_ACT_HTEXTRA);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
