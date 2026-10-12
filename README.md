<div align="center">

# EDuke32 HRP Mod Menu

**Outlines, aim assist and a free camera for Duke Nukem 3D with the High Resolution Pack, one key away.**

A mod menu for EDuke32 running the High Resolution Pack.

![Game](https://img.shields.io/badge/Game-EDuke32%20HRP-FFB000?labelColor=0E0F13&style=flat)
![Platform](https://img.shields.io/badge/Platform-Windows-FFB000?labelColor=0E0F13&style=flat)
![Release](https://img.shields.io/github/v/release/Nokama0/eduke32-hrp-menu?label=Release&color=FFB000&labelColor=0E0F13&style=flat)
![Build](https://img.shields.io/github/actions/workflow/status/Nokama0/eduke32-hrp-menu/build.yml?branch=main&label=Build&labelColor=0E0F13&style=flat)
![License](https://img.shields.io/badge/License-Unlicense-FFB000?labelColor=0E0F13&style=flat)

</div>

---

A mod menu for **EDuke32** (synthesis build `r10669-ec5824db8`, 64-bit) running the **High Resolution Pack**. Press
**Insert** in game to open it.

It loads as a `version.dll` next to `eduke32.exe`, which hands every call on to the real `version.dll` from System32.
It does not change any game file. It finds the engine's variables through the game's own console registry and by
scanning for known code, and it carries no fixed addresses.

## What it does

The menu has five tabs.

**Visuals**
- Enemy outlines, health bars, names and distance
- Markers for items and pickups, explodables, breakables, switches and locks
- Secret areas, with their volume, triggers and a path to them
- Damage numbers; draw distance with an unlimited option

**Combat**
- Aimbot, with a hold key and smoothing
- Silent aim: target choice, aim point, cone, range, lead for moving targets
- Triggerbot with a delay

**Weapons**
- Perfect accuracy for the chaingun and the expander
- Fire rate and weapon switch speed sliders
- Automatic switching on pickup or when a weapon runs dry

**View**
- Custom field of view
- A free camera: flight speed, draw the whole map, teleport, show the player, copy and paste a view

**Settings**
- Menu key and scale, mouse button and hotkey bindings
- The game's own checkpoint autosave options plus a timer
- Panel animations, and diagnostics

Cheat-style features start off. Hover a row for a short explanation.

## Get it

You need EDuke32 `r10669-ec5824db8` (x64) with the High Resolution Pack. Other builds are not supported, and the menu
says in its log what it could not find. It was built and tested on the **Polymer** renderer. On Polymost the free
camera still works, but its sky and map drawing are Polymer-only and do nothing.

1. **Download** `version.dll` from the [latest release](https://github.com/Nokama0/eduke32-hrp-menu/releases/latest).
2. **Copy** it next to `eduke32.exe`. If a `version.dll` is already there, it belongs to another mod: back it up first.
3. **Start** the game and press **Insert**.

To remove it, delete `version.dll`, along with `ekmod.ini` and `ekmod.log` if you want them gone too. All three sit
next to `eduke32.exe`.

## Good to know

- **Where files go.** The menu's settings (`ekmod.ini`) and its log (`ekmod.log`) are written next to
  `eduke32.exe`. It writes keys one at a time and never rewrites your game's own config.
- **Only the game.** The menu starts only inside `eduke32.exe`. The map editor (`mapster32.exe`) in the same folder,
  or a renamed exe, loads the DLL but gets no menu and no `ekmod.log`.
- **If something does not work,** `ekmod.log` records which part of the engine the menu could not find. Rows for
  those parts read "Unavailable" in the menu.

---

## Build it

You need Windows with Visual Studio 2022 (the MSVC x64 toolset) and a Windows 10 SDK. The script finds them with
`vswhere`.

```powershell
powershell -File src/ekmod/build.ps1
```

### Tests

`build.ps1` runs a unit-test host of the pure rules first, then compiles `build\version.dll`. A red test fails the
build; a good run prints a line ending in `N passed, 0 failed`. The build also checks that the DLL exports every
function the real `version.dll` does.

Design notes and reverse-engineering findings are in [`docs/`](docs/README.md).

## Other menus

The same menu, ported to other Duke Nukem games.

| Game | Repository |
|---|---|
| Duke Nukem Forever | [dukenukem-forever-menu](https://github.com/Nokama0/dukenukem-forever-menu) |
| Duke Nukem: Manhattan Project | [dukenukem-manhattanproject-menu](https://github.com/Nokama0/dukenukem-manhattanproject-menu) |
| Alien Armageddon on EDuke32 | [eduke32-aa-menu](https://github.com/Nokama0/eduke32-aa-menu) |
| Duke Nukem 3D: 20th Anniversary World Tour | [duke3d-worldtour-menu](https://github.com/Nokama0/duke3d-worldtour-menu) |
| Duke Nukem 3D: Megaton Edition | [duke3d-megaton-menu](https://github.com/Nokama0/duke3d-megaton-menu) |

## License

[The Unlicense](LICENSE): public domain, no warranty.

Dear ImGui 1.92.6 (`src/ekmod/imgui`) is MIT licensed; its `LICENSE.txt` stays beside it.
