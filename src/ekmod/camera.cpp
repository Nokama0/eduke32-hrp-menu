// The bracket, the pose, the sweep and the frame driver: substitutes the camera's pose into the located draw call and
// reads the state camerascan.cpp locates (its file comment has the locate story).

#include <windows.h>
#include <intrin.h>
#include <string.h>
#include "log.h"
#include "camera.h"
#include "camerascan.h"
#include "hitscan.h"
#include "sectorstate.h"
#include "actorstate.h"
#include "tsprites.h"
#include "frustum.h"
#include "gamestate.h"
#include "hook.h"
#include "playerstate.h"
#include "aimsolve.h"
#include "config.h"
#include "input.h"
#include "udstate.h"
#include "cvars.h"
#include "shot.h"
#include "perf.h"
#include "ui/wallclock.h"

#pragma intrinsic(_ReturnAddress)

// THE BRACKET: the substitution is ONE engine call wide, inside the "write only what the engine writes" rule
// (G_DrawRooms rebuilds all six ud.camera* fields, game.cpp:1001-1110). It is not put back, so overlay, ESP and tracers
// stay registered with the drawn view (project.cpp:70). Every refusal draws the player's own view.

// The sector whose band contains the camera (freecam.h has the rule): the last sector plus one ring of wall neighbours,
// since this runs inside polymer_drawrooms and the camera leaves a band through a floor, ceiling or wall. Extra walls
// are truncated.
static const int EK_CAM_BAND_SLOTS = 24;
static const int EK_CAM_BAND_WALLS = 64;

// engine.cpp:14288's signature; the sector arrives as a POINTER to its record (sectorstate publishes one).
typedef void (*GetZsOfSlopePtrFn)(const void* sec, int dax, int day,
                                  int* ceilz, int* florz);

// Said once: a fault means the located function is not what we think. Interlocked because secretvol.cpp also calls
// cam_band_fill, from the scan thread.
static volatile LONG s_saidNoGetz = 0;

bool cam_band_fill(int sectnum, int x, int y, FreecamBand* b,
                   EkSectorInfo* si) {
  if (!sectorstate_info(sectnum, si)) return false;
  b->sect = sectnum;
  b->ceilz = si->ceilz;
  b->florz = si->florz;
  b->exact = si->sloped ? 0 : 1;
  // Unsloped sectors need no call (getzsofslopeptr returns the flat pair first, engine.cpp:14292).
  if (b->exact) return true;

  const LONG rva = InterlockedCompareExchange(&s_getzRva, 0, 0);
  const unsigned char* const base = s_base;
  if (!rva || !base) return true;

  // Bounds-checked before the engine is handed the record: getzsofslopeptr indexes wall[] unchecked. It bounds the
  // first hop only (the engine then follows point2 unchecked: a wrong z, not a fault, on the game thread). On the scan
  // thread a map load can land in between, so the __except is the bound; a fault costs the slope call for the session.
  if (!ekm_sector_record_plausible(si->wallptr, si->wallnum)) return true;
  const void* const rec = sectorstate_record(sectnum);
  if (!rec) return true;

  int cz = b->ceilz, fz = b->florz;
  __try {
    ((GetZsOfSlopePtrFn)(const void*)(base + rva))(rec, x, y, &cz, &fz);
    b->ceilz = cz;
    b->florz = fz;
    b->exact = 1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    InterlockedExchange(&s_getzRva, 0);
    if (InterlockedExchange(&s_saidNoGetz, 1) == 0) {
      ek_log("camera: the located getzsofslopeptr faulted; it will not be "
             "called again. Sloped sectors fall back to their flat band and "
             "the sector pick declines to substitute on one");
    }
  }
  return true;
}

static int cam_band_sect(int sect, int x, int y, int z) {
  if (!sectorstate_ready()) return sect;

  // Candidate zero is the sector the engine resolved, so a camera already inside its band picks what it always did.
  FreecamBand bands[EK_CAM_BAND_SLOTS];
  EkSectorInfo here = { 0, 0, 0, 0, 0 };
  if (!cam_band_fill(sect, x, y, &bands[0], &here)) return sect;
  int n = 1;

  const int walls = here.wallnum < EK_CAM_BAND_WALLS ? here.wallnum : EK_CAM_BAND_WALLS;
  for (int k = 0; k < walls && n < EK_CAM_BAND_SLOTS; k++) {
    EkWallFace face;
    if (!wallstate_face(here.wallptr + k, &face)) continue;
    const int ns = face.nextsector;
    if (ns < 0 || ns >= EK_MAXSECTORS || ns == sect) continue;
    bool seen = false;
    for (int j = 0; j < n; j++) { if (bands[j].sect == ns) { seen = true; break; } }
    if (seen) continue;
    EkSectorInfo nb = { 0, 0, 0, 0, 0 };
    if (!cam_band_fill(ns, x, y, &bands[n], &nb)) continue;
    n++;
  }

  return freecam_band_pick(bands, n, z, sect, EK_MAXSECTORS);
}

// The whole map on top of the sky: polymer.cpp:1232's loops after the normal arm, only for sectors the portal walk
// skipped, with domasks TRUE (FALSE would overflow cursectormasks on mirror frames: sectorstate.h). Extent:
// prsectors[i] non-NULL, the tiling chain and the EK_MAXWALLS clamp; __try backstops (a fault ends the sweep).
typedef void (*PolyUpdateSectorFn)(short sectnum);
typedef void (*PolyDrawSectorFn)(short sectnum, int domasks);
typedef void (*PolyUpdateWallFn)(short wallnum);
typedef void (*PolyDrawWallFn)(short sectnum, short wallnum);

static bool s_saidSweepFault = false;

// GL_CULL_FACE: engine.cpp:9563 is the last GL state renderDrawRoomsQ16 sets (culling OFF), so the sweep drew both
// faces of every wall; sectorstate.h's ekm_sweep_cull has the rule. Resolved from opengl32.dll's GL 1.1 exports, once,
// on the render thread; a failure refuses the sweep rather than drawing the defect.
#define EK_GL_CULL_FACE 0x0B44u
#define EK_GL_MODELVIEW_MATRIX 0x0BA6u
#define EK_GL_PROJECTION_MATRIX 0x0BA7u
typedef void (WINAPI* GlToggleFn)(unsigned int);
typedef unsigned char (WINAPI* GlIsEnabledFn)(unsigned int);
typedef void (WINAPI* GlGetFloatvFn)(unsigned int, float*);
static GlToggleFn s_glEnable = NULL;
static GlToggleFn s_glDisable = NULL;
static GlIsEnabledFn s_glIsEnabled = NULL;
// Not in cam_gl_entries' verdict: without it the sweep merely draws more than it needs.
static GlGetFloatvFn s_glGetFloatv = NULL;
static bool s_glTried = false;
static bool s_saidNoGl = false;

// Said once per answer; finding culling already ON would mean a misread image.
static int s_saidCull = -1;

// Tried once: a module that did not export them on the first frame will not start.
static bool cam_gl_entries(void) {
  if (!s_glTried) {
    s_glTried = true;
    const HMODULE gl = GetModuleHandleA("opengl32.dll");
    if (gl) {
      s_glEnable = (GlToggleFn)GetProcAddress(gl, "glEnable");
      s_glDisable = (GlToggleFn)GetProcAddress(gl, "glDisable");
      s_glIsEnabled = (GlIsEnabledFn)GetProcAddress(gl, "glIsEnabled");
      s_glGetFloatv = (GlGetFloatvFn)GetProcAddress(gl, "glGetFloatv");
    }
  }
  return s_glEnable != NULL && s_glDisable != NULL && s_glIsEnabled != NULL;
}

// The sweep's visibility test: frustum.h has the evidence; this is the engine-facing plumbing. Nothing is kept between
// frames (effectors move geometry).

// The pose handed to polymer_drawrooms, for the apex gate: render thread, inside one call, so plain ints.
static int s_sweptEyeX = 0, s_sweptEyeY = 0, s_sweptEyeZ = 0;

// Rebuilt each swept frame; sloped sectors and their neighbours are never culled.
static unsigned char s_cullSloped[EK_MAXSECTORS];

static bool s_saidNoGetFloatv = false;
static bool s_saidApex = false;

struct SweepFrustum {
  EkSidePlane left, right;
  float offL, offR;
  int ready;
};

// Every refusal draws the whole map, as before the test existed.
static void cam_build_frustum(EkCullSlab slab, SweepFrustum* out) {
  out->ready = 0;
  out->offL = 0.0f;
  out->offR = 0.0f;
  if (!s_glGetFloatv) {
    if (!s_saidNoGetFloatv) {
      s_saidNoGetFloatv = true;
      ek_log("camera: opengl32.dll did not export glGetFloatv, so the whole-map "
             "sweep cannot read the matrices it would test visibility against. "
             "It draws every sector the portal walk skipped, exactly as it did "
             "before; the row and the camera are unaffected");
    }
    return;
  }
  if (!ekm_slab_usable(slab)) return;

  float mv[16], pr[16], pm[16];
  s_glGetFloatv(EK_GL_MODELVIEW_MATRIX, mv);
  s_glGetFloatv(EK_GL_PROJECTION_MATRIX, pr);
  // polymer.cpp:3843: projection loaded, modelview multiplied onto it.
  ekm_mat4_mul(pr, mv, pm);

  const EkSidePlane L = ekm_plane_normalise(ekm_frustum_left(pm));
  const EkSidePlane R = ekm_plane_normalise(ekm_frustum_right(pm));
  if (!ekm_plane_usable(L) || !ekm_plane_usable(R)) return;

  // Both side planes contain the eye, so the pose just drawn from must read zero on both: this is the check that these
  // are this frame's matrices.
  const EkGlPoint eye = ekm_build_to_gl(s_sweptEyeX, s_sweptEyeY, s_sweptEyeZ);
  const float fL = ekm_plane_eval(L, eye);
  const float fR = ekm_plane_eval(R, eye);
  if (!ekm_frustum_apex_ok(fL, fR, EK_CULL_APEX_TOL)) {
    if (!s_saidApex) {
      s_saidApex = true;
      ek_log("camera: the frustum GL handed back does not have the camera at "
             "its apex (left %.1f, right %.1f build units, tolerance %.1f), so "
             "it is not this frame's view. The whole-map sweep draws every "
             "sector the portal walk skipped and culls nothing",
             fL, fR, EK_CULL_APEX_TOL);
    }
    return;
  }

  const float yLo = ekm_slab_ylo(slab);
  const float yHi = ekm_slab_yhi(slab);
  out->left = L;
  out->right = R;
  out->offL = ekm_cull_offset(L, yLo, yHi, EK_CULL_MARGIN);
  out->offR = ekm_cull_offset(R, yLo, yHi, EK_CULL_MARGIN);
  out->ready = 1;
}

// True only when every corner is outside the SAME side plane; false (draw it) on a sloped sector or neighbour, an
// unreadable wall or a short span. The footprint is exactly these corners: walls take their x,y from the same two
// records (WALLS_ARE_CONSISTENT, engine.cpp:1296).
static int cam_sector_offscreen(int wallFirst, int wallCount, int sloped,
                                int chainEnd, const SweepFrustum* fr) {
  if (sloped) return 0;
  int corners = 0, outL = 0, outR = 0;
  const int last = wallFirst + wallCount;
  for (int w = wallFirst; w < last; w++) {
    EkWallCorner c;
    if (!wallstate_corner(w, &c)) return 0;
    const int nsSloped =
        (c.nextsector >= 0 && c.nextsector < chainEnd)
            ? (int)s_cullSloped[c.nextsector] : 1;
    if (ekm_cull_neighbour_blocks(c.nextsector, chainEnd, nsSloped)) return 0;
    const EkGlPoint p = ekm_build_to_gl(c.x, c.y, 0);
    outL += ekm_cull_column_out(fr->left, fr->offL, p.x, p.z);
    outR += ekm_cull_column_out(fr->right, fr->offR, p.x, p.z);
    corners++;
    if (outL != corners && outR != corners) return 0;
  }
  return ekm_cull_reject(corners, outL, outR, 0, 1);
}

// polymer_scansprites (polymer.cpp:3891) written out (`static inline`): walk each swept sector's chain into tsprite[].
// The chain is walked for cost; sectnum lockstep is a WITNESS (tsprites.h). Duplicates: the list's own owner fields
// plus the sector bitmap. The bound is the safety argument: tsprite[4096] was partly filled (polymer.cpp:2152).

static bool s_saidTsprRefused = false;
static bool s_saidTsprFull = false;

// Which sprites the list already holds, rebuilt from the owner fields each frame (2KB).
static unsigned char s_inList[(EK_MAXSPRITES + 7) / 8];

// Takes the sprite half down for the session and nothing else (the sky and sweep keep running).
static void cam_sprites_refuse(const char* why) {
  InterlockedExchange(&s_spritesOk, 0);
  if (s_saidTsprRefused) return;
  s_saidTsprRefused = true;
  ek_log("camera: the swept sectors' sprites cannot be collected -- %s. The "
         "sprite pass is off for the session, so the sectors only the sweep "
         "reaches draw without their actors; THE SKY REDIRECT STAYS ARMED, the "
         "geometry sweep keeps running and the free camera keeps flying; "
         "nothing was left changed", why);
}

// `outAdded` counts appended sprites for the perf zone; set beside every `*cnt = n` so refusals mid-chain report what
// they appended.
static void cam_append_sprites(int sweptEnd, int* outAdded) {
  *outAdded = 0;
  const unsigned char* const base = s_base;
  const unsigned tsprSlot = (unsigned)InterlockedCompareExchange(&s_tsprSlotRva, 0, 0);
  const unsigned cntSlot = (unsigned)InterlockedCompareExchange(&s_ssCntSlotRva, 0, 0);
  const unsigned sprSlot = (unsigned)InterlockedCompareExchange(&s_sprSlotRva, 0, 0);
  const unsigned headSlot = (unsigned)InterlockedCompareExchange(&s_headSlotRva, 0, 0);
  const unsigned nextSlot = (unsigned)InterlockedCompareExchange(&s_nextSlotRva, 0, 0);
  const unsigned dstate = (unsigned)InterlockedCompareExchange(&s_dstateRva, 0, 0);
  if (!base || !tsprSlot || !cntSlot || !sprSlot || !dstate) return;
  if (!headSlot || !nextSlot) return;
  if (!InterlockedCompareExchange(&s_spritesOk, 0, 0)) return;
  if (sweptEnd <= 0) return;
  // No sprite array until gamestate's scan has a loaded level.
  if (!gamestate_ready()) return;

  // The witness is about the ARRAY: the walk names `sprite`, the content scan whichever global held the value (the CON
  // VM's vm.pSprite), so slot rvas legitimately differ (array 0x127C640). Two derefs: the slot names
  // `sprite`, which names the array.
  const unsigned found = gamestate_sprite_array_rva();
  const size_t sprVar = slot_target_rva(base, sprSlot);
  const unsigned predicted =
      sprVar ? (unsigned)slot_target_rva(base, (unsigned)sprVar) : 0u;
  if (!ekm_sprite_array_agrees(predicted, found)) {
    ek_log("camera: the inlined scansprites names sprite[] at array rva 0x%07X "
           "(through the `sprite` pointer at rva 0x%llX) and the array in use "
           "is at 0x%07X; the sprite pass appends nothing rather than copy "
           "records it cannot vouch for",
           predicted, (unsigned long long)sprVar, found);
    cam_sprites_refuse("the array the image walk decodes is not the array "
                       "gamestate is handing out");
    return;
  }

  const size_t image = host_image_size(base);
  __try {
    const unsigned char* const drawn = (const unsigned char*)(base + dstate);
    unsigned char* const* const tspritep =
        *(unsigned char* const* const*)(base + tsprSlot);
    unsigned char* const arr = *tspritep;
    int* const cnt = *(int* const*)(base + cntSlot);
    const size_t arrRva = (arr > base) ? (size_t)(arr - base) : 0;
    if (!ekm_tsprite_array_ok(arrRva, image)) {
      cam_sprites_refuse("tsprite[] does not point inside the host image with "
                         "room for the whole list");
      return;
    }
    int n = *cnt;
    if (n < 0 || n > EK_MAXSPRITESONSCREEN) {
      cam_sprites_refuse("spritesortcnt is outside the list it indexes");
      return;
    }

    memset(s_inList, 0, sizeof(s_inList));
    for (int k = 0; k < n; k++) {
      const int owner = *(const short*)(arr + (size_t)k * EK_TSPR_STRIDE +
                                        EK_TSPR_OWNER);
      if (ekm_tsprite_owner_ok(owner, EK_MAXSPRITES))
        s_inList[ekm_bitmap_byte(owner)] |= (unsigned char)ekm_bitmap_mask(owner);
    }

    const short* const head =
        *(const short* const*)(base + headSlot);
    const short* const nxt =
        *(const short* const*)(base + nextSlot);
    int added = 0;
    int truncated = 0;
    int walked = 0;
    for (int s = 0; s < sweptEnd && !truncated; s++) {
      if (ekm_bitmap_test(drawn[ekm_bitmap_byte(s)], s)) continue;
      for (int i = head[s]; ekm_sprite_chain_live(i, EK_MAXSPRITES);
           i = nxt[i]) {
        if (!ekm_chain_budget_ok(++walked, EK_MAXSPRITES)) {
          *cnt = n;
          *outAdded = added;
          cam_sprites_refuse("a sector's sprite chain does not end -- more "
                             "links than there are sprites in the map");
          return;
        }
        const unsigned char* const r = gamestate_sprite(i);
        if (!r) {
          *cnt = n;
          *outAdded = added;
          cam_sprites_refuse("a chain link is outside the sprite array the "
                             "content scan found");
          return;
        }
        const int statnum = *(const short*)(r + EK_SPR_STATNUM);
        const int sectnum = *(const short*)(r + EK_SPR_SECTNUM);
        // Two searches agreeing, one link at a time: a link naming another sector means the chain arrays or sprite[] is
        // not what we believe. A refusal costs the actors, not the geometry.
        if (!ekm_sprite_chain_agrees(sectnum, s) ||
            !ekm_sprite_swept(statnum, sectnum, sweptEnd, EK_MAXSECTORS)) {
          *cnt = n;
          *outAdded = added;
          cam_sprites_refuse("a sprite reached through a sector's own chain "
                             "does not say it is in that sector");
          return;
        }
        // The list's own record, which the chain does not replace: on an external-arm frame the walk's bitmap is the
        // LAST walk's.
        if (ekm_bitmap_test(s_inList[ekm_bitmap_byte(i)], i)) continue;
        const unsigned cstat = *(const unsigned short*)(r + EK_SPR_CSTAT);
        if (!ekm_tsprite_eligible(cstat, r[EK_SPR_XREPEAT], r[EK_SPR_YREPEAT], 0))
          continue;
        if (!ekm_tsprite_admit(n, EK_MAXSPRITESONSCREEN)) { truncated = 1; break; }
        unsigned char* const dst = arr + (size_t)n * EK_TSPR_STRIDE;
        memcpy(dst, r, (size_t)EK_TSPR_STRIDE);
        dst[EK_TSPR_CLIPDIST] = 0;
        *(short*)(dst + EK_TSPR_OWNER) = (short)i;
        n++;
        added++;
      }
    }
    *cnt = n;
    *outAdded = added;

    if (truncated && !s_saidTsprFull) {
      s_saidTsprFull = true;
      ek_log("camera: the frame's sprite list filled at %d entries, which is "
             "the engine's own MAXSPRITESONSCREEN, after the sweep added %d of "
             "the swept sectors' sprites. The rest of the map's actors, items "
             "and effects are not drawn on a frame this happens; the picture is "
             "short, and nothing was written past the list",
             EK_MAXSPRITESONSCREEN, added);
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    cam_sprites_refuse("a read or a write of the engine's sprite list faulted");
  }
}

static void cam_draw_whole_map(void) {
  const unsigned char* const base = s_base;
  if (!base || !InterlockedCompareExchange(&s_sweepOk, 0, 0)) return;
  if (!sectorstate_ready()) return;

  const int skip = perf_skip_mask();

  const PolyUpdateSectorFn us =
      (PolyUpdateSectorFn)(const void*)(base + (unsigned)s_usRva);
  const PolyDrawSectorFn ds =
      (PolyDrawSectorFn)(const void*)(base + (unsigned)s_dsRva);
  const PolyUpdateWallFn uw =
      (PolyUpdateWallFn)(const void*)(base + (unsigned)s_uwRva);
  const PolyDrawWallFn dw =
      (PolyDrawWallFn)(const void*)(base + (unsigned)s_dwRva);

  // Backface culling on for the draw loop and put back after: the original turned it off on its way out
  // (engine.cpp:9563).
  const int glOk = cam_gl_entries() ? 1 : 0;
  const EkSweepCull cull =
      ekm_sweep_cull(glOk, glOk && s_glIsEnabled(EK_GL_CULL_FACE) ? 1 : 0);
  if (cull.refuse) {
    if (!s_saidNoGl) {
      s_saidNoGl = true;
      ek_log("camera: the whole-map sweep needs glEnable, glDisable and "
             "glIsEnabled from opengl32.dll so it can draw with backface "
             "culling on, and could not resolve all three. Refusing: the row "
             "delivers nothing, and the free camera and the game are "
             "unaffected");
    }
    return;
  }
  if (cull.enable != s_saidCull) {
    s_saidCull = cull.enable;
    ek_log("camera: the whole-map sweep found backface culling %s. %s",
           cull.enable ? "OFF, which is what renderDrawRoomsQ16 leaves behind "
                         "(engine.cpp:9563)"
                       : "already ON, which this file did not expect",
           cull.enable ? "Turning it on for the sweep's own draws and off again "
                         "after, so glbuild's cached state is left as it was"
                       : "Leaving it exactly as found; if the sweep still shows "
                         "wall backs, the cause is not this");
  }
  if (cull.enable) s_glEnable(EK_GL_CULL_FACE);


  int prevPtr = 0, prevNum = 0;
  // Volatile: written in the __try, read after the handler, zeroed there so a faulted sweep adds no sprites.
  volatile int sweptEnd = 0;
  __try {
    const void* const* const prsectors =
        *(const void* const* const*)(base + (unsigned)s_prsRva);
    const void* const* const prwalls =
        *(const void* const* const*)(base + (unsigned)s_prwRva);
    const unsigned char* const drawn =
        (const unsigned char*)(base + (unsigned)s_dstateRva);

    EkCullSlab slab = ekm_slab_init();
    int chainEnd = 0;
    perf_zone_begin(EK_PZ_SWEEP_SCAN);
    {
      int pPtr = 0, pNum = 0;
      for (int i = 0; i < EK_MAXSECTORS; i++) {
        EkSectorInfo si = { 0, 0, 0, 0, 0 };
        const int ok = sectorstate_info(i, &si) ? 1 : 0;
        const int live = prsectors[i] != NULL ? 1 : 0;
        const EkSectorSweep step = ekm_sector_sweep_step(
            i, ok, live, si.wallptr, si.wallnum, pPtr, pNum, EK_MAXSECTORS);
        if (step.stop) break;
        s_cullSloped[i] = (unsigned char)(si.sloped ? 1 : 0);
        slab = ekm_slab_add(slab, si.ceilz, si.florz);
        chainEnd = i + 1;
        pPtr = si.wallptr;
        pNum = si.wallnum;
      }
    }
    perf_zone_end(EK_PZ_SWEEP_SCAN, chainEnd);

    SweepFrustum fr;
    perf_zone_begin(EK_PZ_SWEEP_FRUSTUM);
    cam_build_frustum(slab, &fr);
    perf_zone_end(EK_PZ_SWEEP_FRUSTUM, 1);

    int drewSectors = 0;
    perf_zone_begin(EK_PZ_SWEEP_LOOP);
    for (int i = 0; i < EK_MAXSECTORS; i++) {
      EkSectorInfo si = { 0, 0, 0, 0, 0 };
      const int ok = sectorstate_info(i, &si) ? 1 : 0;
      const int live = prsectors[i] != NULL ? 1 : 0;
      const EkSectorSweep step = ekm_sector_sweep_step(
          i, ok, live, si.wallptr, si.wallnum, prevPtr, prevNum, EK_MAXSECTORS);
      if (step.stop) break;
      sweptEnd = i + 1;
      // The portal walk's sectors are skipped after the chain test, so the chain still advances.
      if (ekm_bitmap_test(drawn[ekm_bitmap_byte(i)], i)) {
        prevPtr = si.wallptr;
        prevNum = si.wallnum;
        continue;
      }
      if (fr.ready) {
        perf_zone_begin(EK_PZ_SWEEP_CULL);
        const int off = cam_sector_offscreen(step.wallFirst, step.wallCount,
                                             si.sloped, chainEnd, &fr);
        perf_zone_end(EK_PZ_SWEEP_CULL, 1);
        if (off && !ekm_perf_skips(skip, EK_PERF_SKIP_NOCULL)) {
          prevPtr = si.wallptr;
          prevNum = si.wallnum;
          continue;
        }
      }
      if (!ekm_perf_skips(skip, EK_PERF_SKIP_UPDATES)) {
        perf_zone_begin(EK_PZ_SWEEP_US);
        us((short)i);
        perf_zone_end(EK_PZ_SWEEP_US, 1);
      }
      perf_zone_begin(EK_PZ_SWEEP_DS);
      ds((short)i, 1);
      perf_zone_end(EK_PZ_SWEEP_DS, 1);
      drewSectors++;
      // Walls come from the sector's own span (wallsect[] is filled from it, engine.cpp:10877); the bisect mask
      // shortens the loop bound.
      const int last = ekm_perf_skips(skip, EK_PERF_SKIP_WALLS)
                           ? step.wallFirst
                           : step.wallFirst + step.wallCount;
      for (int w = step.wallFirst; w < last; w++) {
        // polymer_drawwall dereferences prwalls[w] unchecked, so a span past the loaded map skips that wall, not the
        // sweep.
        if (!ekm_wall_drawable(prwalls[w] != NULL ? 1 : 0)) continue;
        if (!ekm_perf_skips(skip, EK_PERF_SKIP_UPDATES)) {
          perf_zone_begin(EK_PZ_SWEEP_UW);
          uw((short)w);
          perf_zone_end(EK_PZ_SWEEP_UW, 1);
        }
        perf_zone_begin(EK_PZ_SWEEP_DW);
        dw((short)i, (short)w);
        perf_zone_end(EK_PZ_SWEEP_DW, 1);
      }
      prevPtr = si.wallptr;
      prevNum = si.wallnum;
    }
    perf_zone_end(EK_PZ_SWEEP_LOOP, drewSectors);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    InterlockedExchange(&s_sweepOk, 0);
    if (!s_saidSweepFault) {
      s_saidSweepFault = true;
      ek_log("camera: the whole-map sweep faulted and will not run again this "
             "session. The free camera keeps flying and the row now reads "
             "Unavailable; out of bounds the picture is the portal walk's "
             "again");
    }
    sweptEnd = 0;
  }

  // Put back on both paths: the handler swallows and falls through here.
  if (cull.restore) s_glDisable(EK_GL_CULL_FACE);

  // The sprites, outside the handler so a sprite-list fault is charged to the sprite list.
  if (!ekm_perf_skips(skip, EK_PERF_SKIP_SPRITES)) {
    int spritesAdded = 0;
    perf_zone_begin(EK_PZ_SWEEP_SPRITES);
    cam_append_sprites((int)sweptEnd, &spritesAdded);
    perf_zone_end(EK_PZ_SWEEP_SPRITES, spritesAdded);
  }
}

// REMOVE FOG: saved and restored, never written one way (Duke moves visibility about: flashlight sectors, underwater
// palettes). It self-heals: game.cpp:817 recomputes g_visibility at the head of every G_DrawRooms, so a missed restore
// costs one frame.
struct FogHold { int* p; int saved; };

// Read through a guard like every engine access.
static bool cam_fog_arm(FogHold* h) {
  h->p = (int*)(INT_PTR)InterlockedCompareExchange64(&s_gvisAddr, 0, 0);
  h->saved = 0;
  if (!h->p) return false;
  __try {
    h->saved = *h->p;
    *h->p = 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    // A field that would not take the write is one the locate got wrong; zeroing the address takes the row to
    // Unavailable.
    InterlockedExchange64(&s_gvisAddr, 0);
    ek_log("camera: g_visibility would not read or write at the located "
           "address; Remove fog is off for the session and nothing was left "
           "changed");
    return false;
  }
  return true;
}

static void cam_fog_release(FogHold* h) {
  if (!h->p) return;
  __try {
    *h->p = h->saved;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

typedef void (*DrawRoomsFn)(int playerNum, int smoothRatio);

typedef void (*DrawRoomsQ16Fn)(int x, int y, int z, int angFix, int horizFix,
                           short sect);

static EkHook s_hook;
static EkHook s_roomsQ16Hook;

static DrawRoomsFn volatile s_drawOrig = NULL;
static DrawRoomsQ16Fn volatile s_roomsQ16Orig = NULL;

// The renderer is observed, not configured (rendmode has no cvar). The detour sits on renderDrawRoomsQ16, whose
// REND_POLYMER test is INSIDE it (engine.cpp:9548): being reached proves the world is drawn, not that Polymer draws it,
// so on Polymost the polymer-only parts (sky, sweep, fog) have nothing behind them.
static volatile LONG s_drawSeenOk = 0;
static volatile LONG s_roomsQ16Idle = 0;

static bool s_attempted = false;

// THE POSE, and the seqlock's engine half (freecam.h owns the counter arithmetic): five ints cannot be read with one
// interlocked op, so a reader that lands inside a write retries. The interlocked ops are the barriers (full fences on
// x64); the rules test only parity and equality, which survive the wrap.
static volatile LONG s_seq = 0;
static FreecamPose s_pose;

static FreecamPose s_posePrev;

static unsigned seq_load(void) {
  return (unsigned)InterlockedCompareExchange(&s_seq, 0, 0);
}

static volatile LONG s_active = 0;

static FreecamState s_cam;

static const FreecamSens s_camSens = { FREECAM_SENS_MILLI_UNIT,
                                       FREECAM_SENS_MILLI_UNIT };

// Seeds the next resolve and is the answer kept when one fails; no interlock (the detour and cam_start run on the game
// thread).
static int s_camSect = -1;

static const int EK_CAM_POSE_TRIES = 4;

// The viewpoint a copy takes (camera.h), on its own counter: published on the FRAME clock, from the player's pose while
// the camera is down.
static volatile LONG s_vpSeq = 0;
static volatile LONG s_vpHave = 0;
static FreecamPose s_vp;

// The reverse direction: a pose parsed on the WINDOW thread for the game thread to spend. Data before flag.
static volatile LONG s_reqSeq = 0;
static volatile LONG s_reqPending = 0;
static FreecamPose s_req;

bool camera_active(void) { return s_active != 0; }

int camera_pose_read(FreecamPose* prev, FreecamPose* cur) {
  if (!prev || !cur) return 0;
  for (int i = 0; i < EK_CAM_POSE_TRIES; i++) {
    const unsigned before = seq_load();
    const FreecamPose q = s_posePrev;
    const FreecamPose p = s_pose;
    const unsigned after = seq_load();
    if (freecam_seq_read_ok(before, after)) { *prev = q; *cur = p; return 1; }
  }
  return 0;
}

int camera_viewpoint_read(FreecamPose* out) {
  if (!out) return 0;
  if (!InterlockedCompareExchange(&s_vpHave, 0, 0)) return 0;
  for (int i = 0; i < EK_CAM_POSE_TRIES; i++) {
    const unsigned before = (unsigned)InterlockedCompareExchange(&s_vpSeq, 0, 0);
    const FreecamPose p = s_vp;
    const unsigned after = (unsigned)InterlockedCompareExchange(&s_vpSeq, 0, 0);
    if (freecam_seq_read_ok(before, after)) { *out = p; return 1; }
  }
  return 0;
}

static void cam_publish_viewpoint(const FreecamPose& p) {
  const unsigned begin =
      freecam_seq_write_begin((unsigned)InterlockedCompareExchange(&s_vpSeq, 0, 0));
  InterlockedExchange(&s_vpSeq, (LONG)begin);
  s_vp = p;
  InterlockedExchange(&s_vpSeq, (LONG)freecam_seq_write_end(begin));
  InterlockedExchange(&s_vpHave, 1);
}

void camera_request_pose(const FreecamPose& pose) {
  const unsigned begin =
      freecam_seq_write_begin((unsigned)InterlockedCompareExchange(&s_reqSeq, 0, 0));
  InterlockedExchange(&s_reqSeq, (LONG)begin);
  s_req = pose;
  InterlockedExchange(&s_reqSeq, (LONG)freecam_seq_write_end(begin));
  InterlockedExchange(&s_reqPending, 1);
}

// Clears the flag FIRST, so a request landing during the read is taken next frame.
static bool cam_take_request(FreecamPose* out) {
  if (!InterlockedExchange(&s_reqPending, 0)) return false;
  for (int i = 0; i < EK_CAM_POSE_TRIES; i++) {
    const unsigned before = (unsigned)InterlockedCompareExchange(&s_reqSeq, 0, 0);
    const FreecamPose p = s_req;
    const unsigned after = (unsigned)InterlockedCompareExchange(&s_reqSeq, 0, 0);
    if (freecam_seq_read_ok(before, after)) { *out = p; return true; }
  }
  return false;
}

void camera_tic(void) {
  // Flag before data on the way down, data before flag on the way up: no reader acts on a not-yet-up zeroed pose.
  if (!s_cam.active) {
    InterlockedExchange(&s_active, 0);
    return;
  }

  FreecamPose p, q;
  p.x = s_cam.x;
  p.y = s_cam.y;
  p.z = s_cam.z;
  p.ang = s_cam.ang & (FREECAM_ANG_UNITS - 1);
  p.horiz = freecam_pitch_to_horiz(s_cam.pitch);
  q.x = s_cam.px;
  q.y = s_cam.py;
  q.z = s_cam.pz;
  q.ang = s_cam.pang & (FREECAM_ANG_UNITS - 1);
  q.horiz = freecam_pitch_to_horiz(s_cam.ppitch);

  const unsigned begin = freecam_seq_write_begin(seq_load());
  InterlockedExchange(&s_seq, (LONG)begin);
  s_posePrev = q;
  s_pose = p;
  InterlockedExchange(&s_seq, (LONG)freecam_seq_write_end(begin));

  InterlockedExchange(&s_active, 1);
}

// Following a teleporter or water surface: decisions are freecam.h's freecam_transport_jump and its hold. This walks
// the chain of the sector the camera is tracked into (the set G_MoveTransports covers, actors.cpp:3908); the gate asks
// whether the point is inside ANY sector, as s_camSect is sticky (freecam.h).
static int s_transportHold = 0;
static bool s_saidNoTransport = false;
static bool s_saidTransportFault = false;
static bool s_saidTransportChain = false;

// t_data[4] of an SE 7 is the ONFLOORZ flag (game.cpp:2930, actors.cpp:3924), not the CON action id. A read that faults
// is not a zero.
static bool cam_transport_onfloor(int i, int* out) {
  const unsigned char* const r = actorstate_record(i);
  if (!r || !out) return false;
  __try {
    *out = *(const int*)(r + EK_ACT_ACTION_ID);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static void camera_transport_tic(void) {
  if (g_cfg.freecamtransport == 0) {
    s_transportHold = 0;
    return;
  }
  // One tic off the hold BEFORE any refusal can skip it (a held camera follows nothing: actors.cpp:3945); the top-up
  // needs evidence the camera is still on a warping sector, so refusals release the hold.
  const FreecamHold held = freecam_transport_hold_next(s_transportHold);
  s_transportHold = held.tics;
  if (s_transportHold != 0) return;

  const unsigned char* const base = s_base;
  const unsigned headSlot = (unsigned)InterlockedCompareExchange(&s_headSlotRva, 0, 0);
  const unsigned nextSlot = (unsigned)InterlockedCompareExchange(&s_nextSlotRva, 0, 0);
  // Said once and names the missing part: the sector and actor scans need a loaded map.
  if (!base || !headSlot || !nextSlot || !gamestate_ready() ||
      !sectorstate_ready() || !actorstate_ready() || !updatesector_ready()) {
    if (!s_saidNoTransport) {
      s_saidNoTransport = true;
      ek_log("camera: follow teleporters is on but the parts it reads are not "
             "all up (chains %s, sprites %s, sectors %s, actors %s, "
             "updatesector %s); the camera flies through transports until they "
             "are. Nothing else is affected",
             (headSlot && nextSlot) ? "yes" : "no",
             gamestate_ready() ? "yes" : "no",
             sectorstate_ready() ? "yes" : "no",
             actorstate_ready() ? "yes" : "no",
             updatesector_ready() ? "yes" : "no");
    }
    return;
  }

  // Inside the map and still in the sector it draws from? The seeded query returns the seed untouched whenever the
  // point is inside it, so a different answer means the camera left. Silent: flying outside the map is ordinary use.
  // s_camSect is not written here.
  int resolved = -1;
  const bool inMap = ek_updatesector(s_cam.x, s_cam.y, s_camSect, &resolved);
  const int inSect =
      freecam_transport_sector(inMap, resolved, s_camSect, EK_MAXSECTORS);
  if (inSect < 0) return;

  // The camera's vel.z on the engine's axis (down is positive); tells the two halves of a water pair apart; clamped
  // (full-int terms).
  const int camDz = clamp_ll_to_int((long long)s_cam.z - (long long)s_cam.pz);

  int chainBad = 0, onWarping = 0;
  int jumpSe = -1, jumpSect = -1, jumpLotag = 0;
  FreecamJump jump = { 0, 0, 0, 0, 0 };

  __try {
    const short* const head = *(const short* const*)(base + headSlot);
    const short* const nxt = *(const short* const*)(base + nextSlot);
    int walked = 0;
    for (int i = head[inSect]; ekm_sprite_chain_live(i, EK_MAXSPRITES);
         i = nxt[i]) {
      // A chain that does not end is a mislocated array, not a long map.
      if (!ekm_chain_budget_ok(++walked, EK_MAXSPRITES)) { chainBad = 1; break; }
      const unsigned char* const r = gamestate_sprite(i);
      if (!r) { chainBad = 1; break; }
      // The same witness the sprite pass takes, one link at a time.
      if (!ekm_sprite_chain_agrees(*(const short*)(r + EK_SPR_SECTNUM), inSect)) {
        chainBad = 1;
        break;
      }
      if (*(const short*)(r + EK_SPR_STATNUM) != FREECAM_TRANSPORT_STAT) continue;
      // owner == self is the engine's unpaired effector (game.cpp:2928), skipped at actors.cpp:3916.
      const int owner = *(const short*)(r + EK_SPR_OWNER);
      if (owner < 0 || owner >= EK_MAXSPRITES || owner == i) continue;
      onWarping = 1;
      const unsigned char* const o = gamestate_sprite(owner);
      if (!o) continue;
      const int ownSect = *(const short*)(o + EK_SPR_SECTNUM);
      if (ownSect < 0 || ownSect >= EK_MAXSECTORS) continue;

      EkSectorInfo sec, own;
      if (!sectorstate_info(inSect, &sec)) continue;
      if (!sectorstate_info(ownSect, &own)) continue;
      int td4 = 0;
      if (!cam_transport_onfloor(i, &td4)) continue;

      FreecamTransport t;
      // The RAW floor and ceiling, as G_MoveTransports' water tests read (actors.cpp:3814, :3843).
      t.lotag = sec.lotag;
      t.seX = *(const int*)(r + EK_SPR_X);
      t.seY = *(const int*)(r + EK_SPR_Y);
      t.seZ = *(const int*)(r + EK_SPR_Z);
      t.ownerX = *(const int*)(o + EK_SPR_X);
      t.ownerY = *(const int*)(o + EK_SPR_Y);
      t.ownerZ = *(const int*)(o + EK_SPR_Z);
      t.ownerCeilZ = own.ceilz;
      t.ownerFloorZ = own.florz;
      t.secCeilZ = sec.ceilz;
      t.secFloorZ = sec.florz;
      t.tempData4 = td4;

      const FreecamJump j = freecam_transport_jump(t, s_cam.x, s_cam.y, s_cam.z,
                                                   camDz);
      if (!j.hit) continue;
      jump = j;
      jumpSe = i;
      jumpSect = ownSect;
      jumpLotag = t.lotag;
      break;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    if (!s_saidTransportFault) {
      s_saidTransportFault = true;
      ek_log("camera: reading the transports in sector %d faulted; the camera "
             "keeps flying and follows none for the rest of this tic. Nothing "
             "was written", inSect);
    }
    return;
  }

  if (chainBad && !s_saidTransportChain) {
    s_saidTransportChain = true;
    ek_log("camera: sector %d's sprite chain does not agree with sprite[] -- "
           "the camera follows no transports while that is true, and nothing "
           "was written", inSect);
  }

  // The top-up wins over the jump on the tic the hold expires, as in the engine (player.cpp:5233, actors.cpp:3945), or
  // the camera drifts inside the far pad's window and is carried back.
  const int topup = freecam_transport_hold_topup(held.expired, onWarping);
  if (topup != 0) {
    s_transportHold = topup;
    return;
  }
  if (jumpSe < 0) return;

  s_cam.x = jump.x;
  s_cam.y = jump.y;
  s_cam.z = jump.z;
  // The only partial discontinuity: the point pair collapses onto the landing (no streak), the turn pair stays real
  // mouse movement.
  freecam_pose_carry_pos(&s_cam);
  const int fromSect = inSect;
  s_camSect = jumpSect;
  // The hold the rule chose, sign and all: 13 down for a pad, -2 up for water, none for an airborne teleporter.
  s_transportHold = jump.hold;
  // Not behind debuglog: "it teleported me somewhere odd" is what players report.
  ek_log("camera: followed transport sprite %d (sector %d lotag %d) to "
         "(%d,%d,%d) in sector %d", jumpSe, fromSect, jumpLotag, s_cam.x,
         s_cam.y, s_cam.z, jumpSect);
}

// THE STEP, on the engine's own tic: the per-frame ratio minimum is gone with the rule it served; smoothRatio is still
// captured per entry, as it is what the frame draws with.

// One tic of the camera, read off the engine's game-tic counter (freecam_tic_advanced): a GetTickCount accumulator
// rubber-banded and smoothRatio's wrap goes blind below 30 fps. Tracked while the camera is down; the mouse is drained
// AT the step.
static int s_prevTic = 0;
static bool s_haveTic = false;
static bool s_saidNoClock = false;

static void camera_step_on_engine_tic(void) {
  const LONGLONG addr = InterlockedCompareExchange64(&s_otclkAddr, 0, 0);
  if (addr == 0) return;
  int tic = 0;
  __try {
    // clockticks.hpp:136: wholeTicks is first in member order (the address of a ClockTicks is an int32_t pointer).
    tic = *(const volatile int*)(size_t)addr;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    InterlockedExchange64(&s_otclkAddr, 0);
    if (!s_saidNoClock) {
      s_saidNoClock = true;
      ek_log("camera: reading the engine's tic clock faulted; the free camera "
             "stops here and the row now reads Unavailable. The game is "
             "unaffected");
    }
    return;
  }
  const bool began = freecam_tic_advanced(s_prevTic, tic, s_haveTic);
  s_prevTic = tic;
  s_haveTic = true;
  if (!began || !s_cam.active) return;

  freecam_pose_carry(&s_cam);

  FreecamInput in;
  input_freecam_state(&in);
  freecam_step(&s_cam, in, freecam_speed_clamped(g_cfg.freecamspeed), s_camSens);

  camera_transport_tic();

  camera_tic();
}

static bool s_saidNoPose = false;
static bool s_saidNoSector = false;
static bool s_saidNoWrite = false;
static bool s_saidNotView = false;
static bool s_saidNoRatio = false;

// This frame's smoothRatio: G_DrawRooms is handed it (game.cpp:775) and calls polymer_drawrooms inside, so capture
// precedes use on one thread. "This frame" is the dynamic extent of G_DrawRooms; outside it the frame draws the CURRENT
// pose unblended.
static int s_smoothRatio = 0;
static bool s_smoothFresh = false;

// The pose moved below G_DrawRooms' camera-z clamps, but smoothRatio reaches this port only through this argument, and
// it is the engine's tic clock, so the hook also STEPS the camera.
static void EK_DrawRoomsDetour(int playerNum, int smoothRatio) {
  DrawRoomsFn const orig = s_drawOrig;
  // Reachable only in teardown between camera_remove clearing the pointer and the patch coming out.
  if (!orig) return;
  s_smoothRatio = smoothRatio;
  s_smoothFresh = true;
  // Order inside one G_DrawRooms: detect the tic, step, then the polymer_drawrooms detour blends and draws by the same
  // ratio.
  camera_step_on_engine_tic();
  orig(playerNum, smoothRatio);
  s_smoothFresh = false;
}

// Which of the ten calls this is: only the player's view takes the pose (freecam.h's rule). On Polymer only the
// viewscreen (game.cpp:870) is reachable and the ARGUMENT half excludes it; the return-address half excludes the others
// (G_HandleMirror returns at game.cpp:610, yax_drawrooms at engine.cpp:980).
static bool detour_is_player_view(const void* ret, int x, int y, int z,
                                  int angFix, int horizFix, int sect) {
  const unsigned char* const base = s_base;
  if (!base) return false;
  // GUARDED, unlike project.cpp's read: this runs inside a code patch in the renderer's call path, where a fault has
  // nowhere good to go; no answer means "not the player's view".
  EkCamera cam;
  if (!ud_camera_guarded(&cam)) return false;
  const FreecamViewArgs args = { x, y, z, angFix, horizFix, sect };
  const FreecamViewArgs eng = { cam.x, cam.y, cam.z, cam.angFix, cam.horizFix,
                                cam.sect };
  const unsigned retRva =
      (unsigned)(size_t)((const unsigned char*)ret - base);
  return freecam_is_player_view(retRva, (unsigned)s_drawBegin,
                                (unsigned)s_drawEnd, args, eng);
}

// THE SKY REDIRECT. polymer.cpp:1214 sends a z outside its sector's band down the external arm, which returns before
// the sky (:1845). Calling the original twice fails (glClear :941) and polymer_drawsky is inlined, so the test's input
// comes from us: the band locals come from one getzsofslopeptr call that callsite_install redirects to a thunk.

// The address the site used to reach, captured at install; NOT s_getzRva, which cam_band_fill may zero after its own
// fault.
static GetZsOfSlopePtrFn s_getzOrig = NULL;
static EkCallPatch s_getzPatch;

static int s_widenActive = 0;
static int s_widenZ = 0;

static bool s_saidNoGetzOrig = false;

// The thunk: Windows x64, fifth argument on the stack; the call's own return lands in polymer.cpp:1211.
static void EK_GetZsOfSlopeThunk(const void* sec, int dax, int day,
                                 int* ceilz, int* florz) {
  GetZsOfSlopePtrFn const orig = s_getzOrig;
  if (!orig) {
    if (ceilz) *ceilz = 0;
    if (florz) *florz = 0;
    if (!s_saidNoGetzOrig) {
      s_saidNoGetzOrig = true;
      ek_log("camera: the band-test thunk ran with no original to forward to, "
             "which nothing in this build can produce -- please report it. The "
             "frame draws the external arm, as it would without the mod.");
    }
    return;
  }

  // The engine is asked first and UNGUARDED on purpose: a fault is one it would take without the mod; widening an
  // answer is the design, inventing one is not.
  orig(sec, dax, day, ceilz, florz);

  if (!freecam_widen_now(s_widenActive != 0)) return;
  if (!ceilz || !florz) return;

  const FreecamWidened w = freecam_band_widen(*ceilz, *florz, s_widenZ);
  *ceilz = w.ceilz;
  *florz = w.florz;
}

// Called once from camera_install AFTER both entry hooks: the site is reached only inside the function whose entry we
// own.
static bool cam_install_redirect(const unsigned char* base) {
  const unsigned target = (unsigned)InterlockedCompareExchange(&s_getzRva, 0, 0);
  const unsigned site = (unsigned)InterlockedCompareExchange(&s_getzSiteRva, 0, 0);
  const bool sweepOk = InterlockedCompareExchange(&s_sweepOk, 0, 0) != 0;
  const bool spritesOk = InterlockedCompareExchange(&s_spritesOk, 0, 0) != 0;

  if (!freecam_redirect_install_ok(target != 0 && site != 0, sweepOk)) {
    ek_log("camera: the sky redirect stays OUT -- call site %s, whole-map sweep "
           "%s. The camera flies and the sky comes back only where a reachable "
           "sector's band already contains it, which is what this build did "
           "before the redirect existed",
           (target && site) ? "claimed" : "REFUSED",
           sweepOk ? "available" : "REFUSED");
    return false;
  }
  if (!spritesOk)
    ek_log("camera: and it goes in WITHOUT the sprite pass -- the sky and the "
           "whole map, with no actors in the sectors only the sweep reaches. "
           "That is the deliberate trade: losing distant actors is a smaller "
           "loss than losing the sky");

  s_getzOrig = (GetZsOfSlopePtrFn)(const void*)(base + target);
  if (!callsite_install((void*)(base + site), (void*)(base + target),
                        (void*)&EK_GetZsOfSlopeThunk, &s_getzPatch)) {
    s_getzOrig = NULL;
    ek_log("camera: the band test's call site at rva 0x%07X was NOT redirected; "
           "the game keeps its own call and the sky comes back only where a "
           "reachable sector's band contains the camera", site);
    return false;
  }

  ek_log("camera: the sky redirect is IN -- the band test's call at rva 0x%07X "
         "now reaches our thunk, which asks getzsofslopeptr at 0x%07X first and "
         "widens its answer to contain the drawn z while, and only while, the "
         "substituted draw is in flight", site, target);
  return true;
}

// s_getzPatch.site is non-NULL exactly while the redirect is in; a sprite refusal costs actors, not the sky.
bool camera_sky_available(void) {
  return camera_wholemap_available() && s_getzPatch.site != NULL;
}

static void EK_RoomsQ16Detour(int x, int y, int z, int angFix,
                                      int horizFix, short sect) {
  // First, before anything touches the frame: entry patch and relay are JUMPS, so this is the game's call site and the
  // intrinsic cannot be folded away.
  const void* const ret = _ReturnAddress();

  DrawRoomsQ16Fn const orig = s_roomsQ16Orig;
  if (!orig) return;

  // Cleared on every entry so it cannot leak: the patched site is reachable only through here, and a clear flag is the
  // engine's answer.
  s_widenActive = 0;

  // Reached means the world is drawn, re-proved EVERY frame: the renderer can change from the video menu and the
  // automap/help screen stop the world, and a latched verdict would hand camera_owns_input the keyboard and mouse with
  // no change to the view. It does NOT prove Polymer.
  if (s_roomsQ16Idle) InterlockedExchange(&s_roomsQ16Idle, 0);
  if (!s_drawSeenOk) {
    InterlockedExchange(&s_drawSeenOk, 1);
    ek_log("camera: the hooked drawrooms was reached, so the world is being "
           "drawn and the free camera row is available (this does NOT prove "
           "the renderer is Polymer -- see camera.cpp)");
  }

  // Two branches are the whole cost while the camera is down; this is the PLAY BASELINE (engine.plain).
  if (!s_active) {
    perf_zone_begin(EK_PZ_ENGINE_PLAIN);
    orig(x, y, z, angFix, horizFix, sect);
    perf_zone_end(EK_PZ_ENGINE_PLAIN, 1);
    return;
  }

  if (!detour_is_player_view(ret, x, y, z, angFix, horizFix, (int)sect)) {
    if (!s_saidNotView) {
      s_saidNotView = true;
      // Distinct from "the camera is down": a viewscreen drawn from the camera would look like the camera not working.
      // Under Polymer the viewscreen is the only one of the five that reaches here (G_HandleMirror returns at
      // game.cpp:610; yax_drawrooms at engine.cpp:980): game.cpp:870, excluded by the ARGUMENT half of the gate.
      ek_log("camera: a drawrooms call that is NOT the player's own view -- on "
             "Polymer that means a viewscreen (ud.camerasprite) -- draws "
             "unsubstituted, as it must. This is not the camera declining to "
             "fly.");
    }
    perf_zone_begin(EK_PZ_ENGINE_PLAIN);
    orig(x, y, z, angFix, horizFix, sect);
    perf_zone_end(EK_PZ_ENGINE_PLAIN, 1);
    return;
  }

  FreecamPose prevPose, curPose;
  if (!camera_pose_read(&prevPose, &curPose)) {
    if (!s_saidNoPose) {
      s_saidNoPose = true;
      ek_log("camera: the pose would not read under the seqlock; this frame "
             "draws the player's own view");
    }
    perf_zone_begin(EK_PZ_ENGINE_PLAIN);
    orig(x, y, z, angFix, horizFix, sect);
    perf_zone_end(EK_PZ_ENGINE_PLAIN, 1);
    return;
  }

  // The drawn pose is between the two tics (the camera steps at 30 Hz; the player's view blends by the same ratio,
  // game.cpp:1001). A frame without a captured ratio draws the CURRENT pose unblended; no build path reaches that, so
  // the log reports an impossible state.
  const bool fresh = s_smoothFresh;
  if (!fresh && !s_saidNoRatio) {
    s_saidNoRatio = true;
    ek_log("camera: no smoothRatio was captured for this frame -- no path in "
           "this build can produce that, so please report it. The camera draws "
           "at the tic pose unblended, which is safe but will look stepped at "
           "high frame rates.");
  }
  const FreecamPose pose =
      fresh ? freecam_pose_lerp(prevPose, curPose, s_smoothRatio) : curPose;

  // The camera's sector, asked of the engine and seeded from the camera's last: the camera does not clip, so crossing a
  // wall its point is in no sector, and the pick keeps the last real one. Never -1 (polymer.cpp:1175/:1177). A z
  // outside the band draws every sector and wall (:1229-1243), stalling big maps. The DRAWN point is resolved.
  int resolved = -1;
  const bool resolvedOk = ek_updatesector(pose.x, pose.y, s_camSect, &resolved);
  const FreecamSectorPick pick =
      freecam_sector_pick(resolvedOk, resolved, s_camSect, EK_MAXSECTORS);
  s_camSect = pick.last;
  if (pick.sect < 0) {
    if (!s_saidNoSector) {
      s_saidNoSector = true;
      ek_log("camera: no sector to draw from at (%d,%d) and none resolved yet "
             "this flight -- the frame draws the player's own view", pose.x,
             pose.y);
    }
    perf_zone_begin(EK_PZ_ENGINE_PLAIN);
    orig(x, y, z, angFix, horizFix, sect);
    perf_zone_end(EK_PZ_ENGINE_PLAIN, 1);
    return;
  }

  // The sector whose band contains the camera brings the sky back: such a sector satisfies polymer.cpp:1214 and the
  // normal arm runs, with bands from the engine's own getzsofslopeptr. The seed s_camSect is NOT touched. It stays with
  // the redirect in: a sector the camera is inside is a better place for the portal walk to start from.
  const int drawSect = cam_band_sect(pick.sect, pose.x, pose.y, pose.z);

  // Converted once, before the bracket.
  const int subAng = ekm_f16(pose.ang);
  const int subHoriz = ekm_f16(pose.horiz);
  const short subSect = (short)drawSect;

  // ud.camera* is written and left written, so nothing needs unwinding and no termination handler.
  if (!ud_camera_write(pose.x, pose.y, pose.z, subAng, subHoriz, drawSect)) {
    if (!s_saidNoWrite) {
      s_saidNoWrite = true;
      ek_log("camera: the camera view could not be written into ud.camera*; "
             "the frame draws the player's own view and nothing was left "
             "changed");
    }
    perf_zone_begin(EK_PZ_ENGINE_PLAIN);
    orig(x, y, z, angFix, horizFix, sect);
    perf_zone_end(EK_PZ_ENGINE_PLAIN, 1);
    return;
  }

  // Remove fog is armed here only; it covers the sweep too, or the added sectors would be fogged and the rest not.
  FogHold fog;
  const bool fogHeld =
      freecam_fog_substitute(true, g_cfg.freecamfog != 0) && cam_fog_arm(&fog);

  // Band widening is raised here only, around this one call, so no other caller of the site is widened; z is what
  // polymer.cpp:1215 compares. Armed by the sweep (freecam_redirect_arm); s_swept* is the pose the sweep's frustum is
  // checked against (the eye is every plane's apex).
  s_sweptEyeX = pose.x;
  s_sweptEyeY = pose.y;
  s_sweptEyeZ = pose.z;

  s_widenZ = pose.z;
  s_widenActive =
      freecam_redirect_arm(g_cfg.freecamwholemap != 0,
                           InterlockedCompareExchange(&s_sweepOk, 0, 0) != 0)
          ? 1 : 0;

  perf_zone_begin(EK_PZ_ENGINE);
  orig(pose.x, pose.y, pose.z, subAng, subHoriz, subSect);
  perf_zone_end(EK_PZ_ENGINE, 1);

  s_widenActive = 0;

  // The rest of the map on top of the sky, only on the substituted path. With the redirect in, the sweep adds exactly
  // what the walk skipped; without it some geometry draws twice (depth-correct).
  if (g_cfg.freecamwholemap) cam_draw_whole_map();

  if (fogHeld) cam_fog_release(&fog);
}


// THE FRAME DRIVER. Every engine term the gates need is read here once per frame; an unreadable term lands in `valid`
// false, which refuses entry and forces an exit ("nothing known" about the player is not permission).

static void cam_start(const PlayerSnap& snap) {
  // Starts where the player is looking, not the map origin.
  s_cam.x = snap.x;
  s_cam.y = snap.y;
  s_cam.z = snap.z;
  s_cam.ang = (snap.ang >> 16) & (FREECAM_ANG_UNITS - 1);
  // Effective pitch is q16horiz PLUS q16horizoff (playerstate.h).
  s_cam.pitch = freecam_horiz_to_pitch((snap.horiz + snap.horizoff) >> 16);
  s_cam.yawRem = 0;
  s_cam.pitchRem = 0;
  s_camSect = (snap.sect >= 0 && snap.sect < EK_MAXSECTORS) ? snap.sect : -1;

  // Collapsed so the first frame of a flight interpolates nothing: entry is a jump, and a blend would streak from the
  // old site.
  freecam_pose_carry(&s_cam);
  s_transportHold = 0;
  s_cam.active = true;
  ek_log("camera: flying from (%d,%d,%d) ang %d pitch %d",
         s_cam.x, s_cam.y, s_cam.z, s_cam.ang, s_cam.pitch);
}

static void cam_stop(const char* why) {
  s_cam.active = false;
  ek_log("camera: down (%s)", why);
}


// Two records: the LIVE PLAN moves every frame the marker is on, the SAID OUTCOME only when the key fired. One record
// would pair the live plan's words with the last press's clock and could overwrite a NO_WRITE unread (Megaton
// camera.cpp:665). Each on its own counter, written by the game thread, read by the draw.
static volatile LONG s_tpPlanSeq = 0;
static volatile LONG s_tpPlanHave = 0;
static EkTeleportPlan s_tpPlan;

static volatile LONG s_tpSaidSeq = 0;
static volatile LONG s_tpHave = 0;
static EkTeleportPlan s_tpSaid;
static unsigned s_tpMs = 0;

// One cast feeds both marker and key, so they can never name different spots.
static void cam_teleport_publish_plan(const EkTeleportPlan& p) {
  const unsigned begin =
      freecam_seq_write_begin((unsigned)InterlockedCompareExchange(&s_tpPlanSeq, 0, 0));
  InterlockedExchange(&s_tpPlanSeq, (LONG)begin);
  s_tpPlan = p;
  InterlockedExchange(&s_tpPlanSeq, (LONG)freecam_seq_write_end(begin));
  InterlockedExchange(&s_tpPlanHave, 1);
}

// Only from the key's branch (the reference's say()); outcome and timestamp share one odd window.
static void cam_teleport_say(const EkTeleportPlan& p) {
  const unsigned begin =
      freecam_seq_write_begin((unsigned)InterlockedCompareExchange(&s_tpSaidSeq, 0, 0));
  InterlockedExchange(&s_tpSaidSeq, (LONG)begin);
  s_tpSaid = p;
  s_tpMs = wallclock_ms();
  InterlockedExchange(&s_tpSaidSeq, (LONG)freecam_seq_write_end(begin));
  InterlockedExchange(&s_tpHave, 1);
}

int camera_teleport_plan_read(EkTeleportPlan* plan, int* have) {
  if (!plan || !have) return 0;
  for (int i = 0; i < EK_CAM_POSE_TRIES; i++) {
    const unsigned before = (unsigned)InterlockedCompareExchange(&s_tpPlanSeq, 0, 0);
    const EkTeleportPlan p = s_tpPlan;
    const int hv = InterlockedCompareExchange(&s_tpPlanHave, 0, 0) ? 1 : 0;
    const unsigned after = (unsigned)InterlockedCompareExchange(&s_tpPlanSeq, 0, 0);
    if (freecam_seq_read_ok(before, after)) {
      *plan = p;
      *have = hv;
      return 1;
    }
  }
  return 0;
}

int camera_teleport_outcome(EkTeleportPlan* plan, unsigned* setMs, int* have) {
  if (!plan || !setMs || !have) return 0;
  for (int i = 0; i < EK_CAM_POSE_TRIES; i++) {
    const unsigned before = (unsigned)InterlockedCompareExchange(&s_tpSaidSeq, 0, 0);
    const EkTeleportPlan p = s_tpSaid;
    const unsigned ms = s_tpMs;
    const int hv = InterlockedCompareExchange(&s_tpHave, 0, 0) ? 1 : 0;
    const unsigned after = (unsigned)InterlockedCompareExchange(&s_tpSaidSeq, 0, 0);
    if (freecam_seq_read_ok(before, after)) {
      *plan = p;
      *setMs = ms;
      *have = hv;
      return 1;
    }
  }
  return 0;
}

static EkTeleportPlan cam_teleport_plan_now(FreecamBand* outBand) {
  EkTeleportPlan refused = { 0, 0, 0, 0, -1, EK_TP_NOTHING };
  if (outBand) { outBand->sect = -1; outBand->ceilz = 0; outBand->florz = 0; outBand->exact = 0; }

  FreecamPose pose;
  const int havePose = camera_viewpoint_read(&pose);
  if (!havePose) return refused;

  const EkHitscanDir dir = ekm_tp_direction(pose.ang, pose.horiz);

  // The ray starts from the sector holding its own start point: pose is the tic pose while s_camSect came from the
  // blended drawn point.
  int startSect = s_camSect;
  const bool haveStart = ek_updatesector(pose.x, pose.y, s_camSect, &startSect);
  if (!haveStart || startSect < 0) return refused;

  EkHitData hit = { 0, 0, 0, -1, -1, -1 };
  const bool cast = ek_hitscan(pose.x, pose.y, pose.z, startSect,
                               dir.vx, dir.vy, dir.vz, &hit);
  if (!cast) return refused;

  const EkTeleportAim aim = ekm_tp_aim(hit.sect, hit.wall, hit.sprite,
                                       hit.x, hit.y, hit.z, dir.vx, dir.vy);
  if (!aim.ok) { refused.reason = aim.reason; return refused; }

  int sect = -1;
  const bool inMap = ek_updatesector(aim.x, aim.y, s_camSect, &sect);
  if (!inMap) sect = -1;

  if (sect < 0) {
    EkTeleportPlan noSect = { 0, 0, 0, 0, -1, EK_TP_NO_SECTOR };
    return noSect;
  }

  // The band must be exact: a flat fallback on a slope would land the player at the wrong height. Its own wording: the
  // point IS in the map.
  FreecamBand band = { -1, 0, 0, 0 };
  EkSectorInfo si = { 0, 0, 0, 0, 0, 0, 0, 0 };
  const bool haveBand = cam_band_fill(sect, aim.x, aim.y, &band, &si);
  if (haveBand && outBand) *outBand = band;
  if (!haveBand || !band.exact) {
    EkTeleportPlan noFloor = { 0, 0, 0, 0, -1, EK_TP_NO_FLOOR };
    return noFloor;
  }

  return ekm_tp_plan(aim.x, aim.y, aim.z, sect, band.florz, band.ceilz, EK_TP_PHEIGHT);
}

// From camera_frame only: the game thread with the tic finished, outside the bracket. menuOpen is passed in so teleport
// and camera agree.
static void camera_teleport_frame(bool menuOpen) {
  const int edge = input_teleport_take();
  const int fire = ekm_tp_fire_rule(camera_active() ? 1 : 0, edge, menuOpen ? 1 : 0);

  // The marker needs a plan every frame, the SAME one the key acts on (Megaton camera.cpp:993); with the row off and no
  // press this returns before the cast (a plan is a hitscan, updatesector and cstat round trip, hitscan.cpp:781).
  const int wantMark =
      (camera_active() && g_cfg.freecamteleportmarker != 0 && !menuOpen) ? 1 : 0;
  if (!fire && !wantMark) return;

  FreecamBand band = { -1, 0, 0, 0 };
  const EkTeleportPlan plan = cam_teleport_plan_now(&band);
  cam_teleport_publish_plan(plan);
  if (!fire) return;

  EkTeleportPlan outcome = plan;
  if (plan.ok && !player_write_pos(plan)) {
    outcome.ok = 0;
    outcome.reason = EK_TP_NO_WRITE;
  }
  // One line per press, in the key's branch: a refusal on hardware is otherwise four words under a crosshair.
  if (g_cfg.debuglog)
    ek_log("teleport: %s -- point %d,%d,%d sector %d, floor %d ceiling %d%s",
           ekm_tp_has_notice(outcome) ? outcome.reason : "done",
           plan.x, plan.y, plan.z, plan.sect, band.florz, band.ceilz,
           band.sect < 0 ? " (band not read)" : "");
  cam_teleport_say(outcome);
}

void camera_frame(void) {
  const bool on = g_cfg.freecamon != 0;

  // Drained before the early-out: an edge banked in the frame the row goes to zero would start the camera unbidden
  // later.
  const bool keyEdge = input_freecam_toggle_take() > 0;

  // The paste latch, drained for the same reason; the early-out drops it (input.cpp does not swallow the key while the
  // row is off).
  FreecamPose want;
  const bool wantPose = cam_take_request(&want);

  if (!on && !s_cam.active) { shot_frame(false, false); return; }

  PlayerSnap snap;
  int hp = 0, pal = 0, gm = 0, newowner = 0, overShoulder = 0;
  const bool snapOk = player_snap(&snap);
  const bool bodyOk = player_read_body(&hp, &pal);
  const bool gmOk   = player_read_gm(&gm);
  const bool modeOk = player_read_draw_mode(&newowner, &overShoulder);

  const bool dead = bodyOk && hp <= 0;

  // Not esp_level_loaded (false over the pause menu): ekm_level_loading asks whether a level is being BUILT. The hook
  // is a term of `valid`: freecamon persists to the ini, so a stale row where the patch refused would swallow the
  // camera key (a silent lockout). The automap and help screen (game.cpp:800) are not drawn frames.
  bool mapOrHelp = false;
  const bool drawn = camera_installed() && snapOk && gmOk &&
                     !ekm_level_loading(gm, gmOk) &&
                     !(ud_map_or_help(&mapOrHelp) && mapOrHelp);
  if (drawn) {
    const FreecamDrawSeenVerdict verdict =
        freecam_draw_seen_verdict((int)s_roomsQ16Idle, FREECAM_DRAW_IDLE_FRAMES);
    if (verdict.idle != (int)s_roomsQ16Idle)
      InterlockedExchange(&s_roomsQ16Idle, (LONG)verdict.idle);
    if (verdict.revoke) {
      const bool was = s_drawSeenOk != 0;
      if (was) InterlockedExchange(&s_drawSeenOk, 0);
      // Not a renderer diagnosis: reaching the hooked drawrooms proves a frame was drawn, nothing about which renderer.
      ek_log("camera: the hooked drawrooms has %s been reached in %d drawn "
             "frames, so no frame appears to have been drawn there. The free "
             "camera has no pose to substitute: the row now reads Unavailable "
             "and the camera key is the game's again.",
             was ? "not" : "never", FREECAM_DRAW_IDLE_FRAMES);
    }
  }

  const bool valid = camera_available() && snapOk && bodyOk && gmOk && modeOk &&
                     !ekm_level_loading(gm, gmOk) &&
                     freecam_draw_reachable(newowner, overShoulder);

  // ENTRY ONLY: the game's menu owns the keyboard as surely as ours; freecam_exit_rule ignores both (being unable to
  // leave is worse).
  const bool gmMenuOpen = gmOk && (gm & EK_MODE_MENU) != 0;
  const bool menuOpen = input_panel_open() || gmMenuOpen;

  if (freecam_force_off_rule(s_cam.active, on, dead, valid)) {
    cam_stop(!on ? "the row was switched off"
                 : dead ? "the player died"
                        : "the player, the level or the draw path stopped reading");
  } else if (s_cam.active) {
    if (freecam_exit_rule(true, keyEdge, menuOpen)) cam_stop("the key");
  } else if (freecam_enter_rule(on, keyEdge, menuOpen, dead, valid)) {
    cam_start(snap);
  } else if (keyEdge) {
    // A refused entry says WHICH term refused (Debug log), or a press in third person or at a viewscreen looks like a
    // frozen camera.
    if (g_cfg.debuglog)
      ek_log("camera: entry refused -- row %s, draw hook %s, menu %s, dead %s, "
             "snap %s, body %s, "
             "gm %s, draw mode %s, level loading %s, viewscreen %s, third person %s",
             on ? "on" : "OFF",
             !camera_installed() ? "NOT INSTALLED"
                                 : (s_drawSeenOk ? "installed"
                                                : "installed, drawrooms not reached"),
             menuOpen ? "OPEN" : "shut", dead ? "YES" : "no",
             snapOk ? "ok" : "FAILED", bodyOk ? "ok" : "FAILED",
             gmOk ? "ok" : "FAILED", modeOk ? "ok" : "FAILED",
             ekm_level_loading(gm, gmOk) ? "YES" : "no",
             (modeOk && newowner >= 0) ? "YES" : "no",
             (modeOk && overShoulder != 0) ? "YES" : "no");
  }

  // A viewpoint off the clipboard, applied here only (the game thread owns s_cam), below the entry so a paste in the
  // same frame wins.
  if (wantPose) {
    if (!s_cam.active) {
      // Refused and said so, not banked (a press acting minutes later) and not starting the camera (it bypasses
      // freecam_enter_rule).
      ek_log("camera: a viewpoint was pasted while the camera is down -- press "
             "the camera key first. Nothing moved.");
    } else {
      s_cam.x = want.x;
      s_cam.y = want.y;
      s_cam.z = want.z;
      s_cam.ang = want.ang & (FREECAM_ANG_UNITS - 1);
      s_cam.pitch = freecam_horiz_to_pitch(want.horiz);
      s_cam.yawRem = 0;
      s_cam.pitchRem = 0;
      freecam_pose_carry(&s_cam);
      // s_camSect is left alone: it is the seed and the answer kept when a resolve fails; having no sector draws the
      // player's view.
      ek_log("camera: flown to a pasted viewpoint (%d,%d,%d) ang %d pitch %d",
             s_cam.x, s_cam.y, s_cam.z, s_cam.ang, s_cam.pitch);
    }
  }

  // Published every frame the row is on: the camera's pose in flight, the player's otherwise; a failed player read
  // keeps the last good one.
  if (s_cam.active) {
    FreecamPose vp;
    vp.x = s_cam.x;
    vp.y = s_cam.y;
    vp.z = s_cam.z;
    vp.ang = s_cam.ang & (FREECAM_ANG_UNITS - 1);
    vp.horiz = freecam_pitch_to_horiz(s_cam.pitch);
    cam_publish_viewpoint(vp);
  } else if (snapOk) {
    FreecamPose vp;
    vp.x = snap.x;
    vp.y = snap.y;
    vp.z = snap.z;
    vp.ang = (snap.ang >> 16) & (FREECAM_ANG_UNITS - 1);
    vp.horiz = (snap.horiz + snap.horizoff) >> 16;
    cam_publish_viewpoint(vp);
  }

  // Screenshot mode on this frame's camera state, below every entry/exit/force-off so the hud returns the SAME frame
  // the camera lands. shot_menu_term(gmMenuOpen), not menuOpen: Duke's own menu bit only, so our panel mid-flight does
  // not give the hud back.
  shot_frame(s_cam.active != 0, shot_menu_term(gmMenuOpen));

  // Always, and last: the step runs on the engine's tic in the G_DrawRooms detour; this covers the entry frame and the
  // landing frame.
  camera_tic();

  // After the step and publish: a transport that fired this tic has already moved the camera.
  camera_teleport_frame(menuOpen);
}

bool camera_installed(void) { return s_roomsQ16Orig != NULL; }

// Installed is not able: the tic clock is the one locate whose refusal costs the feature (a camera that cannot see a
// tic hangs in the air).
bool camera_available(void) {
  return s_roomsQ16Orig != NULL && s_drawSeenOk != 0 &&
         InterlockedCompareExchange64(&s_otclkAddr, 0, 0) != 0;
}

// Follow teleporters needs the two sprite-chain arrays decoded from the inlined polymer_scansprites (camerascan.cpp).
// The sprite[], sector[] and actor[] scans are not terms: they come up per level, so Unavailable in the main menu would
// speak about the map.
bool camera_transport_available(void) {
  return camera_available() && s_base != NULL &&
         InterlockedCompareExchange(&s_headSlotRva, 0, 0) != 0 &&
         InterlockedCompareExchange(&s_nextSlotRva, 0, 0) != 0;
}

// The install-time unwind is the OPPOSITE order to remove: at install the process keeps running and hook_remove can
// fail (hook.cpp:188), leaving the entry patched, so clearing s_drawOrig first would leave a NULL original and a world
// that draws nothing. The pointer is cleared only after the bytes are proved back.
static void unwind_draw_hook(void) {
  if (hook_remove(&s_hook)) {
    s_drawOrig = NULL;
    return;
  }
  ek_log("camera: G_DrawRooms could not be unpatched after a failed install -- "
         "the entry still reaches this module, so the trampoline pointer is "
         "KEPT and the detour forwards every frame to the engine's own draw. "
         "The free camera stays unavailable and the picture is the game's");
}

bool camera_install(void) {
  if (s_attempted) return s_drawOrig != NULL;

  if (!camera_scan()) return false;

  s_attempted = true;

  const unsigned char* const base = (const unsigned char*)GetModuleHandleA(NULL);
  if (!base) {
    ek_log("camera: no host module; G_DrawRooms is not hooked and the free "
           "camera stays unavailable");
    return false;
  }

  const unsigned entry = camera_rva();
  if (!hook_install((void*)(base + entry), (void*)&EK_DrawRoomsDetour, &s_hook)) {
    ek_log("camera: G_DrawRooms at rva 0x%07X was NOT hooked; the game keeps "
           "its own draw and the free camera stays unavailable", entry);
    return false;
  }

  DrawRoomsFn const tramp = (DrawRoomsFn)hook_trampoline(&s_hook);
  if (!tramp) {
    // Cannot happen after a successful install; a patched draw call with nowhere to return is worse, so it is
    // unpatched.
    if (!hook_remove(&s_hook))
      ek_log("camera: G_DrawRooms has no trampoline AND could not be unpatched "
             "-- the entry still jumps into this module with nowhere to "
             "return to. The free camera stays unavailable");
    ek_log("camera: G_DrawRooms was patched but has no trampoline; unpatched "
           "again and the free camera stays unavailable");
    return false;
  }
  s_drawOrig = tramp;

  // The one the pose goes into: BOTH OR NEITHER, since a G_DrawRooms patch alone is dead code holding a live five-byte
  // rewrite.
  const unsigned poly = camera_roomsQ16_rva();
  if (!hook_install((void*)(base + poly), (void*)&EK_RoomsQ16Detour,
                    &s_roomsQ16Hook)) {
    unwind_draw_hook();
    ek_log("camera: polymer_drawrooms at rva 0x%07X was NOT hooked; G_DrawRooms "
           "was unpatched again and the free camera stays unavailable", poly);
    return false;
  }

  DrawRoomsQ16Fn const polyTramp = (DrawRoomsQ16Fn)hook_trampoline(&s_roomsQ16Hook);
  if (!polyTramp) {
    if (!hook_remove(&s_roomsQ16Hook))
      ek_log("camera: polymer_drawrooms has no trampoline AND could not be "
             "unpatched -- its entry still jumps into this module with nowhere "
             "to return to");
    unwind_draw_hook();
    ek_log("camera: polymer_drawrooms was patched but has no trampoline; both "
           "hooks were taken out again and the free camera stays unavailable");
    return false;
  }

  s_roomsQ16Orig = polyTramp;
  ek_log("camera: G_DrawRooms hooked at rva 0x%07X (no longer substituting) and "
         "the hooked drawrooms at rva 0x%07X (the pose goes in here); the row "
         "reads Unavailable until a drawn frame reaches the second -- which "
         "proves a frame was drawn there, not which renderer drew it (see "
         "s_drawSeenOk's declaration)", entry, poly);

  // The sky is a call site, not a third detour: last and not gating.
  cam_install_redirect(base);

  // One-time diagnostic: polymer.cpp:5359's precedence bug means the art-tile fog term zeroes only at the compiled
  // default r_usenewshading == 4 (polymost.cpp:48); polymer.cpp:5461 depends alike. Logged once.
  cvars_scan();
  int usns = 4;
  if (cvar_get_int("r_usenewshading", &usns) && usns != 4) {
    ek_log("camera: r_usenewshading is %d, not the compiled default 4 -- "
           "Remove Fog may leave the art-tile path's fog term partly on "
           "(polymer.cpp:5359, :5461). Set r_usenewshading back to 4 if the "
           "fog only half goes away.", usns);
  }
  return true;
}

void camera_remove(void) {
  // The call-site redirect comes out first: the site is reached only inside the function whose entry the detour owns,
  // so unpatching the entry first would leave a window reaching a stub page about to be freed.
  if (s_getzPatch.site) {
    if (callsite_remove(&s_getzPatch)) {
      s_getzOrig = NULL;
    } else {
      ek_log("camera: the band test's call site could not be put back; it "
             "still reaches our thunk, which forwards to the engine unchanged");
    }
  }
  // Down either way, so an unremoved stub is a pass-through.
  s_widenActive = 0;

  // The pointer goes first, the opposite of unwind_draw_hook: at teardown it NARROWS the window (a thread inside the
  // original still returns into the block hook_remove frees) and a frame that draws nothing is nothing at all.
  s_roomsQ16Orig = NULL;
  s_drawOrig = NULL;
  if (s_roomsQ16Hook.target && !hook_remove(&s_roomsQ16Hook))
    ek_log("camera: polymer_drawrooms could not be unpatched");
  if (s_hook.target && !hook_remove(&s_hook))
    ek_log("camera: G_DrawRooms could not be unpatched");
}
