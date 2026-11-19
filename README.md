# SW2PS2

> **Early development:** SW2PS2 is new and there will be bugs. Please report them in
> [Issues](https://github.com/Rodpad/SW2PS2/issues) with the game, what happened and, if you can,
> `/switch/sw2ps2/log.txt` from the SD card.

A PlayStation 2 emulator for the Nintendo Switch 2 (hbmenu / kaiHEN), built on the
[OunceSX2](https://github.com/nyathea/OunceSX2) core (ARMSX2 / PCSX2 lineage) with a native
front end: game list with favourites and recents, settings and an in-game menu in tabs
(L / R), per-game settings, save-state slots, cheats, CRT shaders, live resolution switching,
and Switch 2 CPU clocking.

No games, BIOS files or other copyrighted material are included. You need your own PS2
BIOS (default folder `/bios/ps2`) and your own game dumps (default folder `/roms/ps2`); both
folders can be changed in Settings -> App. The app keeps its own files in `/switch/sw2ps2`
(save states in `states/`, per-game settings in `settings/`).

## What SW2PS2 adds to OunceSX2

SW2PS2 uses the [OunceSX2](https://github.com/nyathea/OunceSX2) core with its own changes
(`patches/ouncesx2.patch`).

### Performance

- **Smarter detection of self-modifying code.** Under kaiHEN there is no hardware memory
  protection, so the core otherwise re-checks every compiled block of game code each time it
  runs. SW2PS2 tracks writes to the memory pages that hold compiled code instead, including DMA
  transfers, and recompiles only code that was actually overwritten.
- **ARMSX2's newer IOP thread, made to work on the Switch 2.** The PS2's I/O processor runs on
  its own core using ARMSX2's latest IOP thread, including its lag control. SW2PS2 adds the
  Switch 2 fixes it needs: detecting the console's CPU cores correctly (the standard check
  reports none) and waiting without WFE instructions, which never wake on the Switch 2.
- **Per-game speed settings built in.** Demanding games start with emulated-CPU settings
  measured on the Switch 2, falling back to the community-tested AetherSX2/NetherSX2 lists. Any
  game can be adjusted from the in-game menu, and the app tells you once if a game falls below
  full speed.

### Stability

- **Several games in one session.** Starting a second game and loading a state no longer
  crashes. The cause was the core's background VU compiler, which SW2PS2 turns off; it gave no
  measurable speed.
- **Relaunching from hbmenu.** The emulated PS2's memory is reserved once and released when the
  app exits; before, repeated use fragmented memory until the app could no longer start.
- **Loading a state right after boot.** A state load is held until the game itself has
  started; loading while the BIOS was still booting crashed God of War II.

## Layout

| Path | What |
|---|---|
| `source/` | the front end (game list, menus, settings, video/audio output, test harness) |
| `native/` | glue between the front end and the core (libretro bridge, Horizon helpers; two files of OunceSX2's own app compiled in) |
| `patches/` | SW2PS2's changes to the emulator cores, against the upstream commits in `patches/upstreams.txt` |
| `romfs/` | fonts (with their licences) and picture shaders |
| `icon.png`, `sw2ps2.jpg` | the app icon: the lossless master and the JPEG the build uses (`sw2ps2-test.jpg`: test builds) |
| `tools/` | speed-list generator, profiling helpers, third-party setup |
| `tests/` | on-console test runner (`remote_test.sh`) and its log summariser |
| `probe/` | a small hardware probe app |

## Building

Needs devkitPro (devkitA64) with the ounce-devkit Switch 2 additions (libounce, kaiHEN, OunceVK).

1. `tools/setup_third_party.sh` clones the cores at the pinned commits and applies `patches/`.
2. `tools/build_core.sh` builds libjpeg-turbo (from the core's own copy) and the core, with the
   exact settings the app is built with (CMake + Ninja).
3. `make` builds the app (the Vulkan build on the devkit's OunceVK driver).

After changing what the app links, `tools/make_notices.py` regenerates
`romfs/licenses/THIRD_PARTY_NOTICES.txt`.

## Licence

SW2PS2 is licensed under the GNU General Public License v3.0 or later (see `LICENSE`), as the
OunceSX2 / ARMSX2 / PCSX2 core it builds on is. The libraries linked into the app keep their
own licences: their notices are in `romfs/licenses/THIRD_PARTY_NOTICES.txt`, which ships inside
the app with the GPL text.

## Credits

- [OunceSX2](https://github.com/nyathea/OunceSX2), [ARMSX2](https://github.com/ARMSX2/ARMSX2) and
  [PCSX2](https://github.com/PCSX2/pcsx2): the emulator core (GPL-3.0+). From OunceSX2's Switch 2 app,
  `HorizonThreadWrap` and `ServiceLookupGuard` are compiled in, and its approach to setting Switch 2
  clock rates and to cleaning up threads at exit is followed. The IOP thread is ARMSX2's.
- Per-game CPU settings adapted (converted to SW2PS2's own table) from the community-tested
  [AetherSX2/NetherSX2 playable lists](https://github.com/Darkelf2024/PS2-Playable-Lists-for-AetherSX2-NetherSX2)
  by Darkelf2024, licensed [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
- Fonts: Roboto (Apache 2.0), Source Code Pro (OFL 1.1) — licences in `romfs/licenses`.
