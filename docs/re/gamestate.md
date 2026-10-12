# Locating sprite[] and the other engine arrays (gamestate.cpp)

How the menu finds the sprite array and what the locating scans cost. Build: eduke32.exe r10669.

## sprite
- `sprite` is a POINTER (build.h:619-621), loaded with `mov reg,[rip+disp]`. In r10669 it is at rva 0x0AEE360 (181
  rip-relative loads, 83 within twelve instructions of a multiply by 44 = sizeof(spritetype); the runner-up has 62
  loads, 21%).
- The debug build's adjacent sprite / wall / sector pointers do NOT transfer: both neighbouring slots have zero loads
  in release, and the enginePreInit pointer-block locator was removed for the same reason.
- The array is at rva 0x127C640 in 66 logged launches (enginePreInit's lea at rva 0x0155147 stores it into the sprite
  pointer). The slot the scan read it from varied: 64 times the engine's `sprite` at 0x0AEE360, 2 times the CON VM's
  vm.pSprite at 0x0DDC150 (same value), because "most free slots" is re-measured while sprites churn.
- A global holding &sprite[n] would pass every test and shift every index, so camera.cpp's decode of the engine's
  sprite load decides and this scan only witnesses it.

## Validation
- MAXSECTORS (4096) is a legal stored sectnum: initspritelists (engine.cpp:9380) and deletesprite (:8838) park free
  slots there. statnum = MAXSTATUS on free sprites (engine.cpp:8851), so a real array carries thousands of records with
  statnum 1024.
- A range-only validator accepts zeroed .bss; samples need non-zero coordinates and an exact match.
- No system call per candidate: VirtualQuery per 8-byte slot is about 2.7M kernel transitions and hung the game when it
  ran on the render thread every 2 s. Regions are enumerated once and merged; a fixed 128-slot table filled up and hid
  the rest, and the 720 KB array must lie in ONE entry.

## Scan budget and the 1 Hz sampler
- 900 attempts at 2 s is 30 minutes; then the loop is gone for the process. sectorstate needs >= 16 sectors, so small
  maps can lose sector[] permanently. Only the extent and the secret scans re-run each second on the 1 Hz sampler.
- What runs on the sampler and why: maskscan_probe waits for camera_scan's bracket (a cheap load and branch);
  modelbounds_scan can precede def parsing; secretvol / secrettrig / secretpath answers change on map load and when a
  found secret's lotag is zeroed; secretpath is the largest piece (map wall walk, sprite walk, BFS over 8192 edges) and
  runs last, its sub-tick running seven times between full scans.
- mapextent_ready is deliberately not in the loop's break test: it would keep the loop running and silence
  player_log_tick for 30 minutes, in exactly the run that debugs a missing extent.

## What each lost array costs
Without apScript a play-dead trooper reads as an ordinary corpse; without g_tile the ESP boxes every actor; without
wall[] the triggerbot reads every wall hit as blocked; without sector[] the free camera out of bounds draws the whole
map and no sky.

Related: [actorstate.md](actorstate.md), [tsprite-sweep.md](tsprite-sweep.md), [playerstate.md](playerstate.md).

## Method
Ghidra rip-relative load counts on the stock build; logged scan results over many launches.
