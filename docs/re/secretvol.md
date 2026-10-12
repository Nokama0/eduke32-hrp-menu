# Secret sector volumes: cache and map-change window (secretvol.h)

The menu draws the volume of each secret sector from a double-buffered cache that a 1 Hz sampler refreshes
(`secretvol_generation`).

## Map-change window
- Nothing invalidates the cache on level load; it is replaced on the next 1 Hz sampler tick. For up to one scan
  interval the old map's geometry can draw for a sector that is a secret in both maps.
- The lotag re-check reads the NEW map's lotag, so it does not cover this. The producer also compares the live wallnum
  against the staged `n`, which turns away changed loop lengths.
- What remains: same index, same wall count, different vertices, for under a second; it corrects itself.
- A `(numsectors << 16) ^ numwalls` signature lags equally: mapextent republishes on the same sampler, and
  esp_breakables.h's ekm_wall_scan_due states the same lag.
- `ekm_level_loading` is not a render-thread level signal. An in-session load drives gm to 0 and back inside one
  synchronous call without presenting a frame (see [playerstate.md](playerstate.md)).

Related: [secret-triggers.md](secret-triggers.md), [secret-path.md](secret-path.md).

## Method
Reading the sampler and producer code against the engine's level-load path.
