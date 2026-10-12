# Engine facts behind small modules: aim, cvars, damage numbers, mouse binds, tiles, hooks

Short findings that back single modules. Build: eduke32.exe r10669.

## Aim from the muzzle (aim.h)
- Aim is solved from the muzzle, not the eye. World Tour records (silentwt.cpp:369) that an eye-solved, muzzle-fired shot
  is a parallel line about pyoff + 1024 low, measured as 20 consecutive wall hits with the target still at full health.
- A refusal leaves the player's own shot untouched.
- `aim_solution_at` is for a caller that already has a muzzle and must agree with it exactly. The triggerbot casts its
  verification ray from the muzzle it computed, and passing that same origin in keeps the ray and the shot on one line
  instead of two. q16horizoff is passed rather than read because the engine subtracts it separately (player.cpp:498)
  and silent aim writes only q16horiz.
- Replicating World Tour's "which modifier applies" rule would need PLUTOPAK located and would miss any mod modifier;
  an observation misses none.

## Saving cvars (cvars.cpp)
The engine's "should this be saved to the config" test already answers yes for any value that differs from the cvar's
registered default (osd.cpp:105-116). The flag only covers explicitly restoring a default, so the panel's changes
persist into eduke32.cfg without the menu touching a second field of a record it found by shape.

## Damage numbers (dmgnum.h)
A_IncurDamage has four paths that return -1 having applied nothing, including a CON script vetoing
EVENT_PREACTORDAMAGE (actors.cpp:1319). World Tour pushes before the call, so it reports damage for all four. The
hook that locates A_IncurDamage is in [gamehooks.md](gamehooks.md).

## Mouse-bound game functions (mousebind.h)
- The SK_ bit values are plain #defines in duke3d/src/macros.h and were confirmed against the shipped binary: an `shl
  eax, 0x1a` at rva 0x4b3ce is SK_GAMEQUIT = 26, which fixes the numbering of the whole block.
- P_HandleSharedKeys edge-masks steroids, nightvision, medkit, holoduke and jetpack against `interface_toggle`
  (sector.cpp:2694-2699), so a held bit fires once and does not refire until the button is released.
- QUICK KICK is the exception: it is tested unmasked at sector.cpp:2683, guarded only by a 14-tic counter, so holding the
  button auto-repeats a kick roughly every 14 tics.

## Projection (project.cpp)
See [polymer-projection.md](polymer-projection.md).

## Tiles (tiles.cpp, tiles.h)
- `vec2_16_t tilesiz[MAXTILES]` (build/include/build.h:889) is a DIRECT array in .bss, not a pointer, so it is found
  differently from sprite[]: the locator looks for the array itself rather than for something pointing at it.
- The engine cannot draw a sprite whose tile is 0x0, but it happily HOLDS one and skips it. artReadManifest drops
  zero-size tiles (tiles.cpp:415-419) and six separate draw paths guard with `if (tilesiz[pic].x <= 0) return;`.

## Call-site redirects (hook.cpp)
A redirect rewrites the four operand bytes of a five-byte `call rel32`. The displacement is from the END of the five
bytes: 0x1000 + 5 + 0x0FFB = 0x2000 (synthetic addresses; real ones are re-derived at run time).

## Guarded reads
A write beginning at 0xFFFFFFFF and ending at 0 moved the counter, and moving makes a read bad on either side.

## Method
Engine source reading; Ghidra on the stock build for the SK_ block; behaviour checked in game.
