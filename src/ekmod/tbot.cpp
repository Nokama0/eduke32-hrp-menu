// The triggerbot (tbot.h). Runs in the SwapBuffers hook right after aim_frame: P_GetInput (game.cpp:6485) and
// videoNextPage (:6508) have nothing touching localInput between, and the record clears at the tic's end (:6542).
// Writes one bit, clears nothing. The delay is in wallclock_ms, not frames.

#include <windows.h>
#include <stdio.h>
#include <math.h>
#include "tbot.h"
#include "shotray.h"
#include "hitscan.h"
#include "targets.h"
#include "aim.h"
#include "gamehooks.h"
#include "config.h"
#include "gamestate.h"
#include "wallstate.h"
#include "impact.h"
#include "tracer.h"
#include "projmath.h"
#include "tiles.h"
#include "playerstate.h"
#include "input.h"
#include "udstate.h"
#include "mousebind.h"
#include "camera.h"
#include "log.h"
#include "wallclock.h"

static TbotState s_state = { 0, 0, 0 };
static bool s_haveLast = false;
static unsigned s_lastMs = 0;

// The player's own sprite carries the answers the DukePlayer_t table cannot (no `frozen` field); see player_read_body.
// A cached sprite index once went stale when ps->i was reassigned on respawn, and a failed read counts as dead,
// silently disabling the bot after a level transition. The index no longer exists.
static const int EK_PLAYER_PAL_FROZEN = 1;

// INSTRUMENTATION ONLY (ceiling-turret miss investigation): records what the cast saw for the fire line; feeds no
// decision. `stage` says how far tbot_shot_ray_hit got; TBOT_RAY_UNKNOWN passes tbot_ray_cond.
const int TBOT_TRACE_NOCAST = 1;
const int TBOT_TRACE_RAY    = 2;
const int TBOT_TRACE_HIT    = 3;
struct TbotRayTrace {
  int stage;
  int usedSolution;
  int q16ang, q16horiz, horizoff;
  int vx, vy, vz;
  int ox, oy, oz, osect;
  int hitSprite, hitWall, hitSect, hx, hy, hz;
};
static const TbotRayTrace TBOT_TRACE_BLANK =
    { TBOT_TRACE_NOCAST, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, 0, 0, 0 };
static TbotRayTrace s_trace = TBOT_TRACE_BLANK;

// The cast on record for the fire-time diff (TbotCastRecord, tbot.h). s_castThisFrame ensures `fired` is stamped only
// on a record this frame's cast published.
static TbotCastRecord s_cast;
static bool s_castHave = false;
static bool s_castThisFrame = false;

// Quarter-second spacing between fire lines: the bit goes down every presented frame the condition holds.
const unsigned TBOT_TRACE_MIN_MS = 250;

// INSTRUMENTATION ONLY: counters of events, not log lines, since rate-limited fire lines were misread as consecutive
// frames.
struct TbotTally {
  unsigned fires;
  unsigned casts;
  unsigned enemy;
  unsigned enemyLocked;
  unsigned breaking;
  unsigned blocked;
  // Hit was the player's own sprite (ray starts inside it); ek_hitscan now clears the shooter's bits, so zero confirms
  // that fix.
  unsigned blockedOwn;
  unsigned unknown;
  unsigned unknownNoRay;
  // Frames where the angular condition held and the pitch quantum refused it. Counted before the ray cast, which such a
  // frame never pays for.
  unsigned noroom;
};
static TbotTally s_tally = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// Tallies at each return of tbot_shot_ray_hit so counting cannot drift from its branch order. hitSprite -1 = none;
// rayBuilt 0 = refused before a ray.
static int tbot_tally_ray(int ray, int hitSprite, int lockedSprite, int rayBuilt) {
  if (!g_cfg.debuglog) return ray;
  if (ray == TBOT_RAY_ENEMY) {
    s_tally.enemy++;
    if (hitSprite >= 0 && hitSprite == lockedSprite) s_tally.enemyLocked++;
  } else if (ray == TBOT_RAY_BREAK) {
    s_tally.breaking++;
  } else if (ray == TBOT_RAY_BLOCKED) {
    s_tally.blocked++;
  } else {
    s_tally.unknown++;
    if (!rayBuilt) s_tally.unknownNoRay++;
  }
  return ray;
}

// INSTRUMENTATION ONLY: what a blocked ray hit (sprite or wall, picnum, own sprite?). Rate limited in time; counters at
// the event.
static const unsigned TBOT_BLOCKED_MIN_MS = 1000;
static unsigned s_blockedMs = 0;
static bool s_blockedHave = false;

// Printed with what stopped the ray, because separately rate-limited lines cannot be matched. `short of it` / `past it`
// is measured against aim_marker_points, the point the ray was cast at.
static void tbot_log_blocked(const EkHitData& hit, bool own, int lockedSprite,
                             int originX, int originY, int originZ) {
  const unsigned now = wallclock_ms();
  if (s_blockedHave && (unsigned)(now - s_blockedMs) < TBOT_BLOCKED_MIN_MS) return;
  s_blockedHave = true;
  s_blockedMs = now;

  char aimed[256];
  aimed[0] = 0;
  int lockedPic = 0;
  const bool lockedPicOk = lockedSprite >= 0 &&
                           targets_sprite_picnum(lockedSprite, &lockedPic);
  AimMarkerPoints mp;
  if (lockedSprite >= 0 && aim_marker_points(&mp) && mp.sprite == lockedSprite) {
    const double adx = (double)mp.rawX - originX, ady = (double)mp.rawY - originY;
    const double hdx = (double)hit.x - originX, hdy = (double)hit.y - originY;
    const int aimDist = (int)sqrt(adx * adx + ady * ady);
    const int hitDist = (int)sqrt(hdx * hdx + hdy * hdy);
    // Perpendicular miss distance |(P-O) x D| / |D| (the hit lies on the ray): a few units means it threaded the sprite
    // unreported, hundreds means the solve is off.
    const double dx = (double)hit.x - originX;
    const double dy = (double)hit.y - originY;
    const double dz = (double)hit.z - originZ;
    const double px = (double)mp.rawX - originX;
    const double py = (double)mp.rawY - originY;
    const double pz = (double)mp.rawZ - originZ;
    const double cx = py * dz - pz * dy;
    const double cy = pz * dx - px * dz;
    const double cz = px * dy - py * dx;
    const double dlen = sqrt(dx * dx + dy * dy + dz * dz);
    const double perp =
        dlen > 0.0 ? sqrt(cx * cx + cy * cy + cz * cz) / dlen : -1.0;
    _snprintf_s(aimed, sizeof(aimed), _TRUNCATE,
                " | AIMED AT sprite %d picnum %s%d%s at (%d,%d,%d) from "
                "(%d,%d,%d): the stop is %d units %s it and %d z %s it, and "
                "THE RAY PASSED %.0f UNITS FROM THE AIM POINT",
                lockedSprite, lockedPicOk ? "" : "UNREADABLE(", lockedPic,
                lockedPicOk ? "" : ")", mp.rawX, mp.rawY, mp.rawZ,
                originX, originY, originZ,
                hitDist > aimDist ? hitDist - aimDist : aimDist - hitDist,
                hitDist > aimDist ? "PAST" : "SHORT OF",
                hit.z > mp.rawZ ? hit.z - mp.rawZ : mp.rawZ - hit.z,
                hit.z > mp.rawZ ? "BELOW" : "ABOVE", perp);
  } else if (lockedSprite >= 0) {
    _snprintf_s(aimed, sizeof(aimed), _TRUNCATE,
                " | AIMED AT sprite %d picnum %s%d%s (no marker point to "
                "measure against)",
                lockedSprite, lockedPicOk ? "" : "UNREADABLE(", lockedPic,
                lockedPicOk ? "" : ")");
  } else {
    _snprintf_s(aimed, sizeof(aimed), _TRUNCATE, " | no target was locked");
  }

  int picnum = 0;
  if (hit.sprite >= 0) {
    const bool havePic = targets_sprite_picnum(hit.sprite, &picnum);
    ek_log("tbot blocked: the ray stopped at SPRITE %d picnum %s%d%s "
           "(sect %d, at %d,%d,%d) -- %s%s",
           (int)hit.sprite, havePic ? "" : "UNREADABLE(", picnum,
           havePic ? "" : ")", (int)hit.sect, hit.x, hit.y, hit.z,
           own ? "IT IS THE PLAYER'S OWN SPRITE" : "not the player's own sprite",
           aimed);
    return;
  }

  EkWallFace face;
  if (hit.wall >= 0) {
    const bool haveWall = wallstate_face(hit.wall, &face);
    if (haveWall)
      ek_log("tbot blocked: the ray stopped at WALL %d picnum %d overpicnum %d "
             "cstat 0x%04X nextsector %d (sect %d, at %d,%d,%d)%s",
             (int)hit.wall, face.picnum, face.overpicnum,
             (unsigned)face.cstat, face.nextsector, (int)hit.sect,
             hit.x, hit.y, hit.z, aimed);
    else
      ek_log("tbot blocked: the ray stopped at WALL %d, whose record COULD NOT "
             "BE READ (sect %d, at %d,%d,%d)%s",
             (int)hit.wall, (int)hit.sect, hit.x, hit.y, hit.z, aimed);
    return;
  }

  // Neither: a floor or ceiling hit, which hitscan reports as a sector hit with no sprite or wall.
  ek_log("tbot blocked: the ray stopped at the FLOOR or CEILING of sect %d "
         "(at %d,%d,%d) -- no sprite and no wall were named%s",
         (int)hit.sect, hit.x, hit.y, hit.z, aimed);
}

// Three seconds between totals, printed only when a counter moved.
const unsigned TBOT_TALLY_MIN_MS = 3000;
static TbotTally s_tallyPrinted = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static unsigned s_impactsPrinted = 0;
static unsigned s_tallyMs = 0;
static bool s_tallyHave = false;
static int s_tallySprite = -1;

// INSTRUMENTATION ONLY: the locked target's health, RAW EK_SPR_EXTRA. Falling under confirmed hits proves the rounds
// land; flat over a burst means damage goes elsewhere. Damage is deferred a tic (htextra).
static int s_hpSprite = -1;
static int s_hpExtra = 0;
static bool s_hpHave = false;
static unsigned s_hpDrops = 0;
static bool s_hpUnreadSaid = false;

// Read in the SwapBuffers hook, never the tic-side detour; unreadable is reported, not zero.
static bool tbot_read_target_hp(int sprite, int* extra, int* picnum) {
  if (sprite < 0 || sprite >= EK_MAXSPRITES) return false;
  const unsigned char* const r = gamestate_sprite(sprite);
  if (!r) return false;
  *extra  = (int)*(const short*)(r + EK_SPR_EXTRA);
  *picnum = (int)*(const short*)(r + EK_SPR_PICNUM);
  return true;
}

// INSTRUMENTATION ONLY: target geometry. Sparks landed 2-7 units from the ceiling turret horizontally and 3216 z away.
// ekm_feet_z handles only cstat bit 7; a ceiling sprite is y-flipped (bit 3) and hangs DOWNWARD from its z, so
// measuring up puts samples in the ceiling.
static const unsigned EK_CSTAT_YFLIP    = 8u;
static const unsigned EK_CSTAT_CENTRED  = 128u;
static const unsigned EK_CSTAT_WALLALIGN = 16u;
static const unsigned EK_CSTAT_FLOORALIGN = 32u;

static void tbot_log_target_geometry(int sprite) {
  if (!g_cfg.debuglog || sprite < 0 || sprite >= EK_MAXSPRITES) return;
  static int s_saidFor = -1;
  if (s_saidFor == sprite) return;
  const unsigned char* const r = gamestate_sprite(sprite);
  if (!r) {
    ek_log("tbot geom: sprite %d -- the record refused; no geometry read", sprite);
    s_saidFor = sprite;
    return;
  }
  const int picnum = (int)*(const short*)(r + EK_SPR_PICNUM);
  const unsigned cstat = *(const unsigned short*)(r + EK_SPR_CSTAT);
  const int xrep = r[EK_SPR_XREPEAT], yrep = r[EK_SPR_YREPEAT];
  const int z = *(const int*)(r + EK_SPR_Z);
  int tw = 0, th = 0;
  tile_size(picnum, &tw, &th);
  const float heightZ = ekm_sprite_height_z(yrep, th);
  const int feetZ = ekm_feet_z(z, heightZ, cstat);

  AimMarkerPoints mp;
  const bool haveAim = aim_marker_points(&mp) && mp.sprite == sprite;

  ek_log("tbot geom: sprite %d picnum %d | cstat 0x%04X ="
         "%s%s%s%s | tile %dx%d xrep %d yrep %d | sprite z %d, our heightZ %d, "
         "our feetZ %d (%s) | our aim z %s%d -- %d above the sprite z. "
         "A y-flipped sprite hangs DOWNWARD and ekm_feet_z does not know bit 3.",
         sprite, picnum, cstat,
         (cstat & EK_CSTAT_YFLIP) ? " YFLIP(bit3)" : "",
         (cstat & EK_CSTAT_CENTRED) ? " CENTRED(bit7)" : "",
         (cstat & EK_CSTAT_WALLALIGN) ? " WALLALIGN(bit4)" : "",
         (cstat & EK_CSTAT_FLOORALIGN) ? " FLOORALIGN(bit5)" : "",
         tw, th, xrep, yrep, z, (int)heightZ, feetZ,
         (cstat & EK_CSTAT_CENTRED) ? "centred, so z + heightZ/2"
                                    : "taken as the sprite z itself",
         haveAim ? "" : "(none published) ", haveAim ? mp.rawZ : 0,
         haveAim ? (z - mp.rawZ) : 0);
  s_saidFor = sprite;
}

// INSTRUMENTATION ONLY: where rounds landed (rules: impact.h). A second sprite-array walk rather than a spawn hook
// (which would patch the firing path); behind g_cfg.debuglog. State: the previous frame's class per slot (16 KB .bss,
// this thread only).
static unsigned char s_impactPrev[EK_MAXSPRITES];
// Until filled once every live impact looks new, so the first walk records nothing and priming drops whenever the scan
// is skipped.
static bool s_impactPrimed = false;

// The last target, not only the current: a burst's last rounds land after the lock is gone. Never cleared; the line
// names the sprite and picnum so recycling is visible.
static int s_impactTarget = -1;

struct TbotImpactTally {
  unsigned seen[EKM_IMPACT_CLASSES];
  unsigned band[EKM_BAND_COUNT];
  unsigned sparkBand[EKM_BAND_COUNT];
  unsigned ours;
  unsigned notOurs;
  unsigned unmeasured;
};
static TbotImpactTally s_impacts = { { 0, 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, 0, 0, 0 };

// A per-frame cap on lines, not a rate limit: every impact is counted.
const int TBOT_IMPACT_LINES_MAX = 8;

static bool tbot_read_sprite_pos(int sprite, int* x, int* y, int* z, int* picnum) {
  if (sprite < 0 || sprite >= EK_MAXSPRITES) return false;
  const unsigned char* const r = gamestate_sprite(sprite);
  if (!r) return false;
  *x = *(const int*)(r + EK_SPR_X);
  *y = *(const int*)(r + EK_SPR_Y);
  *z = *(const int*)(r + EK_SPR_Z);
  *picnum = (int)*(const short*)(r + EK_SPR_PICNUM);
  return true;
}

// The z difference is against the target's RAW z (diagnostic).
static void tbot_impact_scan(int lockedSprite) {
  // Two consumers, either runs the walk: the Debug log wants lines, the tracers want positions. The log gates only the
  // logging.
  const bool say = g_cfg.debuglog != 0;
  const bool feed = tracer_impacts_wanted();
  if ((!say && !feed) || !gamestate_ready()) { s_impactPrimed = false; return; }

  if (lockedSprite >= 0) s_impactTarget = lockedSprite;
  const int target = lockedSprite >= 0 ? lockedSprite : s_impactTarget;
  const char* const which = lockedSprite >= 0
      ? "the locked target" : "the LAST locked target (the lock has dropped)";

  int tx = 0, ty = 0, tz = 0, tpic = 0;
  const bool haveTarget = target >= 0 &&
                          tbot_read_sprite_pos(target, &tx, &ty, &tz, &tpic);

  const bool prime = !s_impactPrimed;
  int lines = 0, newThisFrame = 0;

  for (int i = 0; i < EK_MAXSPRITES; i++) {
    const unsigned char* const r = gamestate_sprite(i);
    if (!r) { s_impactPrimed = false; return; }

    const int statnum = (int)*(const short*)(r + EK_SPR_STATNUM);
    const int picnum  = (int)*(const short*)(r + EK_SPR_PICNUM);
    const int cls = ekm_impact_live_class(picnum, statnum);
    const int prev = (int)s_impactPrev[i];
    s_impactPrev[i] = (unsigned char)cls;

    if (prime || !ekm_impact_is_new(prev, cls)) continue;

    newThisFrame++;
    if (say) s_impacts.seen[cls]++;

    const int ix = *(const int*)(r + EK_SPR_X);
    const int iy = *(const int*)(r + EK_SPR_Y);
    const int iz = *(const int*)(r + EK_SPR_Z);
    const int owner = (int)*(const short*)(r + EK_SPR_OWNER);
    const bool mine = player_sprite_matches(owner);
    if (say) { if (mine) s_impacts.ours++; else s_impacts.notOurs++; }

    // The tracers' half: SHOTSPARK1 only (Proj_InsertShotspark puts one at hitData for every hitscan round, impact.h).
    // Whose round it was is passed, not filtered.
    if (feed && cls == EKM_IMPACT_SHOTSPARK1)
      tracer_note_impact(ix, iy, iz, mine ? 1 : 0);

    if (!haveTarget) {
      if (!say) continue;
      s_impacts.unmeasured++;
      if (lines < TBOT_IMPACT_LINES_MAX) {
        lines++;
        ek_log("tbot impact: %s (picnum %d) sprite %d at (%d,%d,%d) statnum %d, "
               "owner %d (%s) -- NO TARGET to measure against (none has been "
               "locked yet, or its record refused)%s",
               ekm_impact_name(cls), picnum, i, ix, iy, iz, statnum, owner,
               mine ? "OUR OWN ROUND" : "not ours",
               cls == EKM_IMPACT_BULLETHOLE
                   ? " | BULLETHOLE means this round hit a WALL: the decal is"
                     " spawned on the wall branch only" : "");
      }
      continue;
    }

    if (!say) continue;

    const double dx = (double)ix - (double)tx;
    const double dy = (double)iy - (double)ty;
    const int dist2d = (int)sqrt(dx * dx + dy * dy);
    const int dz = iz - tz;
    const int dzAbs = dz < 0 ? -dz : dz;
    const int band = ekm_impact_band(dist2d, dzAbs);

    s_impacts.band[band]++;
    if (cls == EKM_IMPACT_SHOTSPARK1) s_impacts.sparkBand[band]++;

    if (lines < TBOT_IMPACT_LINES_MAX) {
      lines++;
      ek_log("tbot impact: %s (picnum %d) sprite %d at (%d,%d,%d) statnum %d, "
             "owner %d (%s) | %s: sprite %d picnum %d at (%d,%d,%d) "
             "| HORIZONTAL %d units, z %+d (%d in horizontal units) -- %s%s",
             ekm_impact_name(cls), picnum, i, ix, iy, iz, statnum, owner,
             mine ? "OUR OWN ROUND" : "not ours",
             which, target, tpic, tx, ty, tz,
             dist2d, dz, dz / (int)EKM_Z_PER_XY, ekm_impact_band_name(band),
             cls == EKM_IMPACT_BULLETHOLE
                 ? " | BULLETHOLE means this round hit a WALL: the decal is"
                   " spawned on the wall branch only" : "");
    }
  }

  if (prime) {
    s_impactPrimed = true;
    if (!say) return;
    ek_log("tbot impact: the impact scan is primed -- this frame's live "
           "BULLETHOLE, SMALLSMOKE and SHOTSPARK1 sprites were recorded and "
           "NOT reported, so nothing already on the walls is counted as a hit.");
    return;
  }
  if (say && newThisFrame > lines)
    ek_log("tbot impact: ...and %d more impact(s) this frame, counted in the "
           "totals but not listed (the per-frame line cap is %d)",
           newThisFrame - lines, TBOT_IMPACT_LINES_MAX);
}

// What the next shot would hit, along the ray it will leave on (cansee looks through glass and grates). Origin is the
// muzzle pos.z + pyoff + ZOFFSET6 (player.cpp:1710-1711), pos.z on a failed pyoff read. When silent aim will substitute
// the angle, follow ITS solution (gamehooks.cpp:344-353).
static int tbot_shot_ray_hit(int lockedSprite) {
  s_castThisFrame = false;
  if (g_cfg.debuglog) { s_trace = TBOT_TRACE_BLANK; s_tally.casts++; }
  if (!hitscan_ready())
    return tbot_tally_ray(TBOT_RAY_UNKNOWN, -1, lockedSprite, 0);

  PlayerSnap me;
  if (!player_snap(&me))
    return tbot_tally_ray(TBOT_RAY_UNKNOWN, -1, lockedSprite, 0);

  int pyoff = 0;
  const bool pyoffOk = player_read_pyoff(&pyoff);
  int muzzleWeapon = 0;
  (void)player_read_weapon(&muzzleWeapon);
  const int muzzleZ = pyoffOk ? ekm_muzzle_z(me.z, pyoff, muzzleWeapon) : me.z;

  int q16ang = me.ang, q16horiz = me.horiz;
  int usedSolution = 0;
  int solvedAng = 0, solvedHoriz = 0, curAng = 0;
  // aim_solution_at, not aim_solution_for_shot: silent aim solves from a muzzle, so ray and shot agree only from the
  // same muzzle and horizoff. pyoffOk is part of the condition: unreadable pyoff means the shot is the player's own.
  if (g_cfg.silenton != 0 && gamehooks_shoot_ready() && pyoffOk &&
      player_q16ang(&curAng) &&
      aim_solution_at(me.x, me.y, muzzleZ, me.horizoff,
                      &solvedAng, &solvedHoriz) &&
      ekm_redirect_within((curAng >> 16) & (EKM_YAW_UNITS - 1),
                          (solvedAng >> 16) & (EKM_YAW_UNITS - 1),
                          g_cfg.silentmaxangle)) {
    // horizoff is NOT replaced: silent aim writes q16horiz only and the engine subtracts both (player.cpp:498).
    q16ang = solvedAng;
    q16horiz = solvedHoriz;
    usedSolution = 1;
  }

  const EkShotRay ray = ekm_shot_ray(q16ang, q16horiz, me.horizoff);
  if (g_cfg.debuglog) {
    s_trace.stage = TBOT_TRACE_RAY;
    s_trace.usedSolution = usedSolution;
    s_trace.q16ang = q16ang; s_trace.q16horiz = q16horiz;
    s_trace.horizoff = me.horizoff;
    s_trace.vx = ray.vx; s_trace.vy = ray.vy; s_trace.vz = ray.vz;
    s_trace.ox = me.x; s_trace.oy = me.y; s_trace.oz = muzzleZ;
    s_trace.osect = me.sect;

    // Published where the ray is built, not after the answer: a cast hitscan refused still gated a shot.
    s_cast.ox = me.x; s_cast.oy = me.y; s_cast.oz = muzzleZ;
    s_cast.posZ = me.z;
    s_cast.pyoff = pyoffOk ? pyoff : 0;
    s_cast.pyoffOk = pyoffOk ? 1 : 0;
    s_cast.osect = me.sect;
    s_cast.q16ang = q16ang; s_cast.q16horiz = q16horiz;
    s_cast.horizoff = me.horizoff;
    s_cast.usedSolution = usedSolution;
    s_cast.targetSprite = -1;
    s_cast.range2d = -1;
    s_cast.fired = 0;
    s_cast.ms = aim_clock_ms();
    s_castHave = true;
    s_castThisFrame = true;
  }
  EkHitData hit;
  if (!ek_hitscan(me.x, me.y, muzzleZ, me.sect, ray.vx, ray.vy, ray.vz, &hit))
    return tbot_tally_ray(TBOT_RAY_UNKNOWN, -1, lockedSprite, 1);
  if (g_cfg.debuglog) {
    s_trace.stage = TBOT_TRACE_HIT;
    s_trace.hitSprite = hit.sprite; s_trace.hitWall = hit.wall;
    s_trace.hitSect = hit.sect;
    s_trace.hx = hit.x; s_trace.hy = hit.y; s_trace.hz = hit.z;

    // Range measured here and carried (hitscan's muzzle-to-hit distance), not re-derived at fire time; recorded only
    // when a sprite was named.
    s_cast.targetSprite = hit.sprite;
    if (hit.sprite >= 0) {
      const double dx = (double)hit.x - (double)me.x;
      const double dy = (double)hit.y - (double)me.y;
      s_cast.range2d = (int)sqrt(dx * dx + dy * dy);
    }
  }

  // Any live or choking enemy satisfies it, not only the locked one; refusing would hold fire when the player most
  // wants it down.
  if (hit.sprite >= 0 && targets_sprite_targetable(hit.sprite))
    return tbot_tally_ray(TBOT_RAY_ENEMY, hit.sprite, lockedSprite, 1);

  int picnum = 0;
  if (hit.sprite >= 0 && targets_sprite_picnum(hit.sprite, &picnum) &&
      ekm_picnum_breaks_open(picnum))
    return tbot_tally_ray(TBOT_RAY_BREAK, hit.sprite, lockedSprite, 1);

  // A wall hit asks the wall: Duke3D's glass is a WALL (hit.wall). A failed read falls through to `blocked`.
  EkWallFace face;
  if (hit.wall >= 0 && wallstate_face(hit.wall, &face) &&
      ekm_wall_breaks_open(face.overpicnum, face.cstat, face.nextsector))
    return tbot_tally_ray(TBOT_RAY_BREAK, hit.sprite, lockedSprite, 1);

  // Why it was blocked, named rather than counted; behind the Debug log row.
  if (g_cfg.debuglog) {
    const bool own = player_sprite_matches(hit.sprite);
    if (own) s_tally.blockedOwn++;
    tbot_log_blocked(hit, own, lockedSprite, me.x, me.y, muzzleZ);
  }
  return tbot_tally_ray(TBOT_RAY_BLOCKED, hit.sprite, lockedSprite, 1);
}

void tbot_frame(void) {
  const unsigned now = wallclock_ms();
  // Unsigned so the difference survives wrap.
  const int elapsedMs = s_haveLast ? (int)(now - s_lastMs) : 0;
  s_haveLast = true;
  s_lastMs = now;

  // INSTRUMENTATION ONLY, ahead of the feature's refusals: rounds landed concerns the engine's bullets, so it runs on
  // every frame the Debug log is on, hand-fired included; off costs one bool read.
  if (g_cfg.debuglog || tracer_impacts_wanted()) {
  AimLock ilock = { -1, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, 0 };
    tbot_impact_scan(aim_lock(&ilock) ? ilock.sprite : -1);
  } else {
    // Called with both consumers off rather than skipped: the gate drops priming, whereas a skipped walk leaves a stale
    // s_impactPrev and the next walk reads every change as a new impact.
    tbot_impact_scan(-1);
  }

  const char* reason = "";

  // Cheap refusal first: off costs one int read. tbot_decide still names the reason and resets the held clock.
  if (g_cfg.tboton == 0) {
    TbotCond none = { 0, "off" };
    tbot_decide(false, false, false, false, false, false, false, none, 0, 0,
                &s_state, &reason);
    return;
  }

  // The free camera, ahead of every engine read: the player is across the map, so a fire bit would empty the gun at
  // whatever stands near his body. freecam.h owns the rule.
  if (!freecam_bots_run(camera_active())) {
    TbotCond flying = { 0, "camera" };
    tbot_decide(false, false, false, false, false, false, false, flying, 0, 0,
                &s_state, &reason);
    return;
  }

  // A failed read is a refusal: assume the answer that holds fire.
  int gm = 0;
  const bool gmOk = player_read_gm(&gm);
  const bool menuOpen = !gmOk || (gm & EK_MODE_MENU) != 0;

  // Pause is its own term: it leaves gm at MODE_GAME (screens.cpp:1300 draws "Game Paused" on ud.pause_on alone) while
  // the hook still runs, so the held clock would charge through it. An unreadable &ud reads as paused.
  bool pauseOn = true;
  const bool paused = !ud_paused(&pauseOn) || pauseOn;

  int hp = 0, pal = 0;
  const bool bodyOk = player_read_body(&hp, &pal);
  const bool dead = !bodyOk || hp <= 0;
  const bool frozen = bodyOk && pal == EK_PLAYER_PAL_FROZEN;

  int weapon = 0;
  const bool weaponUsable = player_read_weapon(&weapon) && tbot_weapon_usable(weapon);

  // aim_frame's lock, published this frame; its gates already cleared it. Zeroed first: no uninitialised struct into a
  // pure rule.
  AimLock lock = { -1, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, 0 };
  const bool haveLock = aim_lock(&lock);

  const TbotCond angular = tbot_when_clamped(g_cfg.tbotwhen) == TBOT_WHEN_AIMBOT
      ? tbot_when_aimbot(haveLock)
      : tbot_when_crosshair(haveLock, lock.myYaw, lock.yaw,
                            lock.myQ16Horiz, lock.q16horiz);

  // Can the shot be placed at all? Before the ray: it is arithmetic on the lock's numbers, the cast is the costliest
  // thing here, and "noroom" belongs ahead of "blocked".
  const TbotCond placed =
      tbot_placed_cond(angular, (double)lock.marginZ, lock.range2d,
                       lock.q16horiz, lock.q16horizoff);
  if (g_cfg.debuglog && angular.holds && !placed.holds) {
    s_tally.noroom++;
    // The rounding cost this shot: the fraction fix16_to_int drops, the z it moves the impact, the margin. One line a
    // second.
    static unsigned s_lastNoRoomMs = 0;
    const unsigned nowNoRoomMs = GetTickCount();
    if (s_lastNoRoomMs == 0 ||
        (unsigned)(nowNoRoomMs - s_lastNoRoomMs) >= 1000u) {
      s_lastNoRoomMs = nowNoRoomMs;
      const double quantum = ekm_pitch_quantum_z(lock.range2d);
      const double ideal = (double)lock.q16horiz / 65536.0 +
                           (double)lock.q16horizoff / 65536.0;
      const double rounded = (ideal >= 0.0) ? (double)(long long)(ideal + 0.5)
                                            : (double)(long long)(ideal - 0.5);
      double leftover = ideal - rounded;
      if (leftover < 0.0) leftover = -leftover;
      ek_log("tbot noroom: sprite %d at range %d -- pitch %.4f rounds to %.0f, "
             "the leftover %.4f moves the impact %.0f z and the body offers "
             "only %d | that margin came from run %d..%d of %d, aim sample %d, "
             "height %d (-1 means stage 2 never measured it)",
             lock.sprite, lock.range2d, ideal, rounded, leftover,
             leftover * quantum, lock.marginZ, lock.spanLo, lock.spanHi,
             EKM_SPAN_SAMPLES, lock.spanMid, lock.spanHeight);
    }
  }

  // The shot's own ray, only when it could change the answer: one hitscan per frame at most.
  const TbotCond cond = placed.holds
      ? tbot_ray_cond(placed, tbot_shot_ray_hit(lock.sprite))
      : placed;

  const bool fire = tbot_decide(true, menuOpen, paused, dead, frozen,
                                input_panel_open(), weaponUsable, cond,
                                tbot_delay_tics(g_cfg.tbotdelay),
                                elapsedMs, &s_state, &reason);
  if (g_cfg.debuglog && fire && s_castThisFrame) s_cast.fired = 1;

  // Target health then running totals, before the `!fire` return so totals flow after a burst ends.
  if (g_cfg.debuglog) {
    if (fire) {
      s_tally.fires++;

      // Logged only when the number moved.
      int extra = 0, picnum = 0;
      const bool hpOk = tbot_read_target_hp(lock.sprite, &extra, &picnum);
      if (lock.sprite != s_hpSprite) {
        s_hpSprite = lock.sprite;
        s_hpHave = false;
        s_hpUnreadSaid = false;
      }
      if (!hpOk) {
        if (!s_hpUnreadSaid) {
          s_hpUnreadSaid = true;
          ek_log("tbot hp: locked sprite %d -- extra COULD NOT BE READ (the "
                 "sprite record refused), so no health is being tracked for it. "
                 "This is not a zero and not a stale value.", lock.sprite);
        }
        s_hpHave = false;
      } else {
        s_hpUnreadSaid = false;
        tbot_log_target_geometry(lock.sprite);
        if (!s_hpHave) {
          s_hpHave = true;
          s_hpExtra = extra;
          ek_log("tbot hp: locked sprite %d picnum %d extra %d at fire #%u -- "
                 "first reading at this sprite, no delta yet",
                 lock.sprite, picnum, extra, s_tally.fires);
        } else if (extra != s_hpExtra) {
          // Drops only: a rise is a save being loaded (+33, +39).
          if (extra < s_hpExtra) s_hpDrops++;
          ek_log("tbot hp: locked sprite %d picnum %d extra %d (delta %+d since "
                 "the previous fire at this sprite) at fire #%u",
                 lock.sprite, picnum, extra, extra - s_hpExtra, s_tally.fires);
          s_hpExtra = extra;
        }
      }
    }

    // Impacts are tested for movement too: hand-firing with the bot held moves neither fires nor casts.
    const unsigned impactsAll = s_impacts.seen[EKM_IMPACT_BULLETHOLE] +
                                s_impacts.seen[EKM_IMPACT_SMALLSMOKE] +
                                s_impacts.seen[EKM_IMPACT_SHOTSPARK1];
    const bool moved = s_tally.fires != s_tallyPrinted.fires ||
                       s_tally.casts != s_tallyPrinted.casts ||
                       impactsAll != s_impactsPrinted;
    const bool due = !s_tallyHave ||
                     (unsigned)(now - s_tallyMs) >= TBOT_TALLY_MIN_MS;
    if (moved && (due || lock.sprite != s_tallySprite)) {
      char hpText[128];
      if (s_hpHave)
        _snprintf_s(hpText, sizeof(hpText), _TRUNCATE,
                    "sprite %d extra %d at the last fire", s_hpSprite, s_hpExtra);
      else if (s_hpSprite >= 0)
        _snprintf_s(hpText, sizeof(hpText), _TRUNCATE,
                    "sprite %d extra COULD NOT BE READ", s_hpSprite);
      else
        _snprintf_s(hpText, sizeof(hpText), _TRUNCATE,
                    "no target has been fired at yet");

      ek_log("tbot totals (CUMULATIVE since the Debug log went on -- running "
             "totals of EVERY event, not per-frame values and not a sample of "
             "the rate limited lines above): fire bit written %u time(s); rays "
             "cast %u = enemy %u (of which the locked target %u) + breaking %u "
             "+ blocked %u (of which THE PLAYER'S OWN SPRITE %u) + no answer "
             "%u (%u of those never reached hitscan) | held for NO ROOM "
             "(the pitch quantum, no cast paid) %u | "
             "currently locked sprite %d | ROUNDS ACTUALLY FIRED BY THE "
             "ENGINE %u, damage events landed on the locked target %u | "
             "target health: %s | IMPACT SPRITES SEEN: BULLETHOLE %u "
             "(every one of those is a round into a WALL) + SMALLSMOKE %u + "
             "SHOTSPARK1 %u, of which ours %u and someone else's %u | measured "
             "against the locked target: on it %u, a near miss %u, FAR %u "
             "(%u could not be measured) | SHOTSPARK1 alone, one per round: "
             "on %u, near %u, FAR %u",
             s_tally.fires, s_tally.casts, s_tally.enemy, s_tally.enemyLocked,
             s_tally.breaking, s_tally.blocked, s_tally.blockedOwn, s_tally.unknown,
             s_tally.unknownNoRay, s_tally.noroom,
             lock.sprite, gamehooks_bullets_fired(),
             s_hpDrops, hpText,
             s_impacts.seen[EKM_IMPACT_BULLETHOLE],
             s_impacts.seen[EKM_IMPACT_SMALLSMOKE],
             s_impacts.seen[EKM_IMPACT_SHOTSPARK1],
             s_impacts.ours, s_impacts.notOurs,
             s_impacts.band[EKM_BAND_ON], s_impacts.band[EKM_BAND_NEAR],
             s_impacts.band[EKM_BAND_FAR], s_impacts.unmeasured,
             s_impacts.sparkBand[EKM_BAND_ON],
             s_impacts.sparkBand[EKM_BAND_NEAR],
             s_impacts.sparkBand[EKM_BAND_FAR]);

      s_impactsPrinted = impactsAll;
      s_tallyPrinted = s_tally;
      s_tallyMs = now;
      s_tallyHave = true;
      s_tallySprite = lock.sprite;
    }
  }

  if (!fire) return;

  static int s_saidOnce = 0;
  if (!s_saidOnce && g_cfg.debuglog) {
    s_saidOnce = 1;
    ek_log("tbot: first write -- mode %d, delay %d ms (%d tic(s)), target sprite %d, "
           "held %d ms", tbot_when_clamped(g_cfg.tbotwhen), g_cfg.tbotdelay,
           tbot_delay_tics(g_cfg.tbotdelay), lock.sprite, s_state.heldMs);
  }

  // INSTRUMENTATION ONLY: the shot beside fields that can move it. auto_aim has no other view (GetAutoAimAng returns -1
  // only at 0; otherwise the engine may snap the shot onto a sprite).
  if (g_cfg.debuglog) {
    static unsigned s_traceMs = 0;
    static int s_traceHave = 0;
    if (!s_traceHave || (unsigned)(now - s_traceMs) >= TBOT_TRACE_MIN_MS) {
      s_traceHave = 1;
      s_traceMs = now;

      int autoAim = 0;
      const bool autoAimOk = player_read_auto_aim(&autoAim);
      int picnum = 0;
      const bool picOk = lock.sprite >= 0 &&
                         targets_sprite_picnum(lock.sprite, &picnum);

      if (s_trace.stage == TBOT_TRACE_NOCAST) {
        ek_log("tbot fire: target sprite %d picnum %d (read %s), auto_aim %d "
               "(read %s) -- NO RAY WAS CAST this frame (hitscan or the player "
               "snapshot refused), so the shot went out unchecked",
               lock.sprite, picnum, picOk ? "ok" : "failed",
               autoAim, autoAimOk ? "ok" : "failed");
      } else {
        ek_log("tbot fire: target sprite %d picnum %d (read %s), auto_aim %d "
               "(read %s) | ray built from %s: q16ang %d q16horiz %d "
               "horizoff %d, handed to hitscan vx %d vy %d vz %d from "
               "(%d,%d,%d) sect %d | %s",
               lock.sprite, picnum, picOk ? "ok" : "failed",
               autoAim, autoAimOk ? "ok" : "failed",
               s_trace.usedSolution ? "SILENT AIM'S PUBLISHED SOLUTION"
                                    : "THE PLAYER'S OWN VIEW",
               s_trace.q16ang, s_trace.q16horiz, s_trace.horizoff,
               s_trace.vx, s_trace.vy, s_trace.vz,
               s_trace.ox, s_trace.oy, s_trace.oz, s_trace.osect,
               s_trace.stage == TBOT_TRACE_HIT ? "hit follows" : "HITSCAN GAVE NO ANSWER");
        if (s_trace.stage == TBOT_TRACE_HIT)
          ek_log("tbot fire: ...hit sprite %d wall %d sect %d at (%d,%d,%d)",
                 s_trace.hitSprite, s_trace.hitWall, s_trace.hitSect,
                 s_trace.hx, s_trace.hy, s_trace.hz);
      }
    }
  }

  mousebind_or_bits(1u << EK_SK_FIRE);
}

bool tbot_firing(void) { return s_state.firing != 0; }

// A copy, never a pointer: the caller is on the firing path in another translation unit.
bool tbot_cast_record(TbotCastRecord* out) {
  if (!s_castHave || !out) return false;
  *out = s_cast;
  return true;
}
