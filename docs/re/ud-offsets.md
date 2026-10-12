# The `ud` structure: offsets read off the image

`ud` (the game's user-data struct, game.h:161-279) is the menu's source of the screen size, field of view and camera
fields. Build: eduke32.exe r10669.

## Field map
pos 0, screen_size 228, fov 428, camera q16ang 432 / q16horiz 436 / sect 440, pause_on 442 (int16), config.AutoAim 468,
overhead_on 1348, show_help 1356.
- fov is game.h:201, config.AutoAim game.h:214-215, pause_on game.h:205 (player.cpp:2358 and screens.cpp:1300 test it).
- The shipped draw call loads the six camera arguments from rdi+0, 4, 8, 0x1B0, 0x1B4, 0x1B8 (432/436/440 as
  transcribed).
- The camera-sprite branch game.cpp:840-876 writes only CAMERA(sect) (:819) and CAMERA(q16ang) (:846).

## How the offsets were established
- A transcription of the struct prefix game.h:161-279 reproduces screen_size 228, fov 428, the camera trio 432/436/440
  and config.AutoAim 468. The first and last are proven at run time by the two-cvar agreement of `ud_base`: the `fov`
  record pointer is &ud+428 and the `r_size` record pointer is &ud+228; both answer 0x140AD5B60.
- EK_UD_OVERHEAD_ON and EK_UD_SHOW_HELP were read off the image, not transcribed: they sit past `config` and `setup`,
  whose sizes depend on MAXJOYAXES, NUMGAMEFUNCTIONS and an #ifdef.
- G_DrawRooms loads &ud into rdi once (rva 0x0031AB7, from the address-pool slot at 0x05A45F0 holding 0x140AD5B60).
  game.cpp:792-800 compiles to:
  - 0x0031AC4 `83 BF E4 00 00 00 07` cmp dword [rdi+228],7 (ud.screen_size >= 8)
  - 0x0031AE9 `80 BF 44 05 00 00 02` cmp byte [rdi+1348],2 (overhead_on == 2)
  - 0x0031AF6 `80 BF 4C 05 00 00 00` cmp byte [rdi+1356],0 (show_help)
- Three agreements back this: screen_size is the 228 proven by ud_base; the immediates 2 and 0 are the source
  predicate's; 1356-1348 = 8 is game.h:267-269's gap (last_overhead, showweapons, god, warp_on, cashman, eog,
  showallmap).

## Method
Ghidra on the stock build, then compare against the source layout.
