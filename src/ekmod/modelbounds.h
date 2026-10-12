#pragma once

// Model bounds, the pure rules: plausible modelid, real MD3 frame box, scaling to world units (read: modelbounds.cpp).
#include <math.h>

#define EK_MDL_IDP3_MAGIC 0x33504449u

// ok is int, not bool: MSVC 19.5x at /O2 drops a bool member of a struct an inline function returns by value.
struct EkModelBox {
  double minx, miny, minz, maxx, maxy, maxz;
  int ok;
};

// -1 means "no model replaces this tile" (legal); any other id must lie in [0, nextmodelid).
inline int ekm_mdl_modelid_plausible(int modelid, int nextmodelid) {
  if (modelid == -1) return 1;
  if (nextmodelid <= 0) return 0;
  return modelid >= 0 && modelid < nextmodelid;
}

// Each axis is checked on its own: a combined comparison passes when two axes are swapped.
inline int ekm_mdl_frame_plausible(double minx, double miny, double minz,
                                    double maxx, double maxy, double maxz) {
  if (!isfinite(minx) || !isfinite(maxx)) return 0;
  if (!isfinite(miny) || !isfinite(maxy)) return 0;
  if (!isfinite(minz) || !isfinite(maxz)) return 0;
  if (minx >= maxx) return 0;
  if (miny >= maxy) return 0;
  if (minz >= maxz) return 0;
  return 1;
}

// One MD3 frame's box in BUILD units, repeats excluded (the consumer multiplies by xrepeat/yrepeat). x,y = raw*s*0.25,
// z = -raw*s*4 - (zadd+yoffset)*s*256, s = scale*bscale (bscale is the def file's number). Build z is down-positive, so
// min and max exchange ends. Derivation: docs/re/modelbox.md.
inline EkModelBox ekm_mdl_scale_bbox(double minx, double miny, double minz,
                                      double maxx, double maxy, double maxz,
                                      double scale, double bscale,
                                      double zadd, double yoffset) {
  EkModelBox b;
  const double s = scale * bscale;
  const double kxy = s * 0.25;
  const double kz = s * 4.0;
  const double off = (zadd + yoffset) * s * 256.0;
  b.minx = minx * kxy;
  b.maxx = maxx * kxy;
  b.miny = miny * kxy;
  b.maxy = maxy * kxy;
  b.minz = -(maxz * kz) - off;
  b.maxz = -(minz * kz) - off;
  // The scales are part of ok: zero collapses the box, negative turns it inside out.
  const int scalesOk = (isfinite(scale) && scale > 0.0 &&
                        isfinite(bscale) && bscale > 0.0) ? 1 : 0;
  b.ok = (ekm_mdl_frame_plausible(minx, miny, minz, maxx, maxy, maxz) &&
          scalesOk) ? 1 : 0;
  return b;
}

// DLL only (reads the host image and the engine heap); everything above is pinned by the suite.

// NONE is the ordinary answer (most tiles have no model) and stays silent.
const int EK_MDL_UNREADY = -1;
const int EK_MDL_NONE    = 0;
const int EK_MDL_OK      = 1;

bool modelbounds_scan(void);

// False until both locates agree (models load while the defs parse) and after a refusal.
bool modelbounds_ready(void);

// True only after the scan GAVE UP; !ready is also the startup wait, so failure reports must ask this.
bool modelbounds_refused(void);

int modelbounds_for(int picnum, int pal, EkModelBox* out);
