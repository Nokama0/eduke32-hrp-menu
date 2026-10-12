# Picnum tables: switches, doors, pickups, breakable walls, one-shot cover

The ESP and triggerbot name tiles by picnum. Every number is this game's own (names.h); World Tour's differ.

## Door tiles (esp_switches.h, ekm_isadoorwall)
picnum -> DOORTILE name: 150..159 = DOORTILE5, 6, 1, 2, 3, 4, 7, 8, 9, 10; 395 = 22; 447..449 = 18, 19, 20; 717 = 14;
781 = 16; 1102 = 15; 1144 = 21; 1169 = 17; 1178, 1179 = 11, 12; 4391 = 23. names.h has no DOORTILE13. These are the 22
DOORTILE cases of CheckDoorTile (sector.cpp:153).

## Switch base tiles (esp_switches.h, ekm_switch_base_for kBases)
Base picnum -> name (family), following G_GetBaseSwitch (sector.cpp:1250):
130 ACCESSSWITCH (access), 132 SLOTDOOR, 134 LIGHTSWITCH, 136 SPACEDOORSWITCH, 138 SPACELIGHTSWITCH, 140
FRANKENSTINESWITCH (rest), 162 DIPSWITCH (dipswitch-like), 164 DIPSWITCH2 (rest), 166 TECHSWITCH (dipswitch-like), 168
DIPSWITCH3 (rest), 170 ACCESSSWITCH2 (access), 712 LIGHTSWITCH2, 860 POWERSWITCH1, 862 LOCKSWITCH1, 864 POWERSWITCH2,
1111 HANDSWITCH, 1122 PULLSWITCH (rest), 1142 ALIENSWITCH (dipswitch-like).

## Pickups (esp_items.h, ekm_item_name; picnum -> names.h symbol)
21 FIRSTGUNSPRITE, 22 CHAINGUNSPRITE, 23 RPGSPRITE, 24 FREEZESPRITE, 25 SHRINKERSPRITE, 26 HEAVYHBOMB (one thrown
bomb), 27 TRIPBOMBSPRITE, 28 SHOTGUNSPRITE, 29 DEVISTATORSPRITE, 37 FREEZEAMMO, 40 AMMO, 41 BATTERYAMMO, 42
DEVISTATORAMMO, 44 RPGAMMO, 45 GROWAMMO, 46 CRYSTALAMMO, 47 HBOMBAMMO (box of pipebombs), 48 AMMOLOTS (big pistol box),
49 SHOTGUNAMMO, 51 COLA (small medkit), 52 SIXPAK (large), 53 FIRSTAID (portable), 54 SHIELD, 55 STEROIDS, 56 AIRTANK,
57 JETPACK, 59 HEATSENSOR (night vision), 60 ACCESSCARD (coloured by pal), 61 BOOTS, 100 ATOMICHEALTH, 1348 HOLODUKE.
- Verification: every number resolves in names.h, matches GAME.CON actors calling addweapon / addammo / addphealth /
  addinventory, and appears live as statnum-2 target refusals in the mod's log.
- World Tour's FLAMETHROWERSPRITE 5134 and FLAMETHROWERAMMO 5135 do not exist in this game; Nuclear Winter, dukedc.grp
  and vacation.grp add no pickup actors.

## Breakable walls (esp_breakables.h, ekm_breakable_name)
293 W_TECHWALL1, 297 W_TECHWALL2, 299 W_TECHWALL15, 301 W_TECHWALL3, 305 W_TECHWALL4, 306 W_TECHWALL10, 307
W_TECHWALL16, 1212 VENDMACHINE, 1215 COLAMACHINE, 4130-4133 W_TECHWALL11..14, 4134 / 4136 / 4138 / 4140 W_TECHWALL5..8,
4142 W_TECHWALL9, 4181 W_MILKSHELF.

## Cover a single bullet opens (tbot.h, ekm_picnum_breaks_open / ekm_wall_breaks_open)
- Sprite picnums: GRATE1 595 (vent grate), FANSPRITE 407, TOILET 569, STALL 571, CIRCLEPANNEL 1113, PANNEL1 342, PANNEL2
  343, PANNEL3 4099.
- Wall picnums: GLASS 503 (cstat = 0 and the neighbour's, sector.cpp:1676-1685), STAINGLASS1 510 (:1698-1703), FANSPRITE
  407 as a masked wall (`cstat &= 65535-65`).

## Method
names.h and GAME.CON in DUKE3D.GRP; the tables checked against live ESP output.
