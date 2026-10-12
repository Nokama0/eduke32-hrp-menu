# EDuke32 cheat codes

Every cheat the engine defines. Alien Armageddon 5.91 adds, removes and hooks
none, so the table is AA's table too.

Method: the source rather than a wiki: `source/duke3d/src/cheats.h` (the enum),
`cheats.cpp` (the strings, the descriptions and the switch that runs them) and
`menus.cpp` (the Cheats menu), in `eduke32 r10669-ec5824db8` (the upstream
version string); AA's scripts searched for the cheat keywords.

**How they are typed.** The prefix is two keys held in `CheatKeys`
(`global.cpp:133`), by default `D` then `N` — so `dncornholio`, `dnstuff` and so
on. It is not Duke-specific: NAM uses `NV`, and WW2GI uses `GI` (its original
`WW` prefix is disabled in source because `W` collides with WASD movement).

**The Cheats menu** under Options lists 21 of them with their descriptions. A
cheat with an empty description does not get a menu row, which is why several
below are invisible unless you type them.

---

## The full table

27 slots exist; 22 have a typeable string. `###` and `#` are digits you supply.

| cheat | what it does |
|---|---|
| `dncornholio` | God mode toggle. |
| `dnkroz` | God mode. **Same handler as `dncornholio`** — a pure alias. |
| `dncgs` | God mode. The third alias, "come get some". |
| `dnstuff` | Everything: all weapons, full ammo on each, full inventory, all three keys. |
| `dnweapons` | All weapons only. |
| `dnitems` | Full inventory and all three keys. |
| `dninventory` | Inventory only. |
| `dnkeys` | The three access keys only. |
| `dnhyper` | Steroids to 399 and night-vision/heat to 1200. |
| `dnscotty###` | Level warp. Three digits: volume then level, e.g. `dnscotty214`. |
| `dnskill#` | Change skill, then restarts the level. |
| `dnview` | Third-person camera toggle. |
| `dnshowmap` | Reveal the whole automap. |
| `dnunlock` | Toggle every lock in the map (skipped in shareware). |
| `dnclip` | No-clip toggle. |
| `dnmonsters` | Cycles **three** states, not two: On, Off, On (2). |
| `dncashman` | Toggle. Money sprays out whenever you press Use. |
| `dncoords` | Coordinate display, cycling 0-2 (0-3 under OpenGL). |
| `dnrate` | Framerate display, cycling 0-3. |
| `dndebug` | Toggles debug mode **and dumps all gamevars to the log**. |
| `dntime` | **Nothing at all.** See below. |
| `dnbeta` | Prints an easter-egg quote. No effect. |
| `dntodd` | Prints an easter-egg quote. No effect. |
| `dnallen` | Prints an easter-egg quote. No effect. |

---

## The interesting parts

### Three names for one god mode
`cornholio`, `kroz` and `cgs` fall through to the same `case` block
(`cheats.cpp:471-473`). There is no behavioural difference between them.

### `dntime` does nothing whatsoever
Its handler is two lines, and the only statement that would have done anything
is commented out:

```c
case CHEAT_TIME:
    //                    P_DoQuote(QUOTE_21,pPlayer);
    end_cheat(pPlayer);
    return;
```

It has an empty description too, so it is absent from the Cheats menu. It
exists only so the string is recognised and swallowed.

### Three cheats you cannot type
Slots 14, 15 and 25 are `"<RESERVED>"`, and a reserved string cannot be
entered. Their handlers are real:

- **Slots 14 and 25** set `ud.eog = 1` and `gm |= MODE_EOL` — end of game.
- **Slot 15** sets `player_par = 0` and `gm = MODE_EOL` — end of level.

So the engine carries a working "finish the level" and "finish the game" cheat
with no way to invoke either by typing. They are reachable only from code — a
CON mod or a fork can rename them by writing into `CheatStrings[]`, which is
exactly what the NAM and WW2GI paths do at `cheats.cpp:164-180`.

### Six cheats survive Damn I'm Good
Cheats are refused on skill 4 with a "Cheats disabled" quote — except six
(`cheats.cpp:265-276`):

```c
if (!FURY && ud.player_skill == 4)
{
    switch (cheatNum)
    {
    case CHEAT_DEBUG:
    case CHEAT_COORDS:
    case CHEAT_RATE:
    case CHEAT_RESERVED:
    case CHEAT_RESERVED2:
    case CHEAT_RESERVED3:
        break;
    default:
        P_DoQuote(QUOTE_CHEATS_DISABLED, pPlayer);
```

The three typeable survivors are `dndebug`, `dncoords` and `dnrate` — they are
treated as diagnostics rather than cheats. The other three are the untypeable
end-of-level ones above, which means **the level-skip cheats are exempt from
the difficulty lock** even though nothing can reach them.

### `dndebug` dumps gamevars
```c
case CHEAT_DEBUG:
    g_Debug = 1-g_Debug;
    G_DumpDebugInfo();
```
This writes the full gamevar state to the log. For the Alien Armageddon work
that is directly useful: AA's XP and upgrade system lives in CON gamevars, and
this is a built-in way to see all of them without reading any memory.

### `dntodd` is a name, not a joke
In NAM the quote is replaced with the cheat's own description string, which is
a credit line for Matt Saettler with his contact address, left in the source
(`cheats.cpp:77`). Saettler did the NAM and WW2GI conversions.

### `dnmonsters` has a third state
Most references say it toggles. It cycles `g_noEnemies` through 0, 1, 2 and
prints "Monsters: On / Off / On (2)". State 2 is a separate mode, not a
duplicate of On.

---

## Relevance to this project

- `dndebug`'s gamevar dump is the cheapest route into AA's XP/upgrade state,
  which lives in CON gamevars (see the gamevar oracle below).
- `dncoords` and `dnrate` overlap with the panel's own readouts; on skill 4
  they are the only ones available.
- `dnmonsters` and `dnclip` are useful when testing ESP and the aimbot against
  a quiet map.
- Alien Armageddon is a CON mod and could rewrite `CheatStrings[]` the same
  way the NAM path does, but it does not (next section): this list is AA's too.

---

## Alien Armageddon 5.91

### AA adds no cheats, removes none, and hooks none

CON can change the cheat table -- `gamedef.cpp` has five keywords for it:
`definecheat`, `definecheatdescription`, `undefinecheat`, `cheatkeys` and
`activatecheat`. AA uses **none of them**:
zero occurrences across all 91 scripts in `scripts\` and the bundled Duke
Caribbean set in `maps\Caribbean\script\`.

It also never takes `EVENT_ACTIVATECHEAT`, the hook that can remap or veto a
cheat by return value (`cheats.cpp:395`). The only cheat-adjacent event AA uses
anywhere is a single `EVENT_CHEATGETJETPACK`.

The `CheatStrings[]` rewrite at `cheats.cpp:164-180` is inside the NAM/WW2GI
game-mode branch, and AA ships our exact stock `eduke32.exe` (the same sha256,
matched against the install). That branch
never runs. AA's cheat prefix is `D`,`N` and its 22 typeable strings are the
stock ones.

**So the table above is AA's table.** The three untypeable `<RESERVED>` slots
stay untypeable under AA too.

### A name trap: `CHEATLEVEL` is not a cheat

`AADEFS.CON:71` defines `CHEATLEVEL 77` and `AAPRELOAD.CON:1095` has
`case CHEATLEVEL`. It is a **sprite tile number** -- a map-placed sprite whose
hitag is multiplied by 1000 and added to the player's credits. Nothing to do
with cheat codes. Only the implementation shows it is not; the name
reads exactly like a cheat.

### `dndebug` is the verification oracle for the gamevar capability

AA's allies, characters and XP all live in CON gamevars, and reading gamevars
by name is the one capability the AA port turns on. This cheat is a free,
independent cross-check for it.

`G_DumpDebugInfo` (`game.cpp:1259`) writes to `eduke32.log`:

- **Every gamevar label and its flags**, via `Gv_DumpValues`
  (`gamevars.cpp`) -- a complete inventory of the table our lookup has to walk.
- **Per-actor values per sprite**: it walks every statnum, and for each sprite
  prints each `GAMEVAR_PERACTOR` var as
  `gamevar <szLabel> <value> GAMEVAR_PERACTOR`. That is `team` printed per
  sprite -- the exact ally data the aimbot needs, straight from the engine.

It also names the structure our lookup must walk: `aGameVars[i].szLabel`,
`.pValues[]`, `.defaultValue`, `.flags`, and `g_gameVarCount`.

Two caveats, both load-bearing:

1. **The per-actor dump prints only values that DIFFER from the default.**
   `team` defaults to 0, which is "the aliens", so an ordinary hostile prints no
   `team` line at all. **Absence means 0, not unknown** -- reading it the other
   way would invert the ally test.
2. **It writes a file.** The last statement is
   `saveboard("debug.map", ...)`, so `dndebug` drops a `debug.map` into the game
   folder. Harmless, but it is a write into `<game folder>`.

A third, smaller one: for a PERPLAYER or PERACTOR var, `Gv_DumpValues` prints
`.global`, which is not where those values live. The label and the flags from
that dump are trustworthy; the printed value is not. Per-actor values come from
the sprite loop, not from there.