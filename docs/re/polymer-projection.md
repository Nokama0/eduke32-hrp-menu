# Polymer projection and viewingrange

How the menu projects world points to the screen under Polymer. Build: eduke32.exe r10669.

## Matrices
- polymer_drawrooms sets the modelview at polymer.cpp:1113-1171.
- The projection is a gluPerspective at polymer.cpp:919-936 via glbuild.cpp:418-440.
- renderDrawRoomsQ16 branches to polymer before the classic path (engine.cpp:9561-9565).

## Pitch
Polymer forms the pitch as `atan2(horiz - 100, 128)` (polymer.cpp:1121), so dead level is horiz == 100. The classic
"globalhoriz is a screen line" formula is real (engine.cpp:9447) but belongs to a renderer the menu does not use.

## viewingrange must be derived, not read
- G_DrawRooms restores `viewingrange` to its entry value when the frame ends (game.cpp:1250-1254), and the menu draws
  after that, so the global reads a constant 65536 at SwapBuffers time whatever the player's field of view.
- Reading it would give a plausible number that is wrong at every fov but the default. game.cpp:886-895 gives the real
  expression. Under Polymer videoSetCorrectedAspect takes the branch that leaves the second factor at 65536
  (engine.cpp:13719), so what survives is the tangent term.

## Locating the player
The player is found by picnum, not by locating g_player, which GCC may have constant-folded out of existence (it is
declared `playerdata_t *const`, global.h:152).

## Method
Engine source reading.
