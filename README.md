# SW2PS2

A PlayStation 2 emulator for the Nintendo Switch 2 (hbmenu / kaiHEN), built on the
[OunceSX2](https://github.com/nyathea/OunceSX2) core (ARMSX2 / PCSX2 lineage) with a native
front end: game list with favourites and recents, per-game settings, save-state slots,
cheats, CRT shaders, live resolution switching, and Switch 2 CPU clocking.

Pre-release: each build works for a week from when it was built.

No games, BIOS files or other copyrighted material are included. You need your own PS2
BIOS (in `sdmc:/switch/sw2ps2/system/pcsx2/bios`) and your own game dumps.

## Layout

| Path | What |
|---|---|
| `source/` | the front end (game list, menus, settings, video/audio output, test harness) |
| `native/` | glue between the front end and the core (libretro bridge, Horizon helpers) |
| `patches/` | SW2PS2's changes to the emulator cores, against the upstream commits in `patches/upstreams.txt` |
| `romfs/` | fonts (with their licences) and picture shaders |
| `tools/` | icon generator, speed-list generator, profiling helpers, third-party setup |
| `tests/` | on-console test runner (`remote_test.sh`) and host benchmarks |
| `probe/` | a small hardware probe app |

## Building

Needs devkitPro (devkitA64 + libnx) on the build machine.

1. `tools/setup_third_party.sh` clones the cores at the pinned commits and applies `patches/`.
2. Build the core (CMake/Ninja tree `third_party/ouncesx2-eval/build-sw2-vk`):
   `ninja -C third_party/ouncesx2-eval/build-sw2-vk PCSX2 sw2-armsx2-libretro`
3. Build the app:
   `make CORE_KIND=armsx2 ARMSX2_VK=1 OUNCEVK= BUILD=build-armsx2-vk-old TARGET=sw2ps2-armsx2-vk-old APP_TITLE="SW2PS2 ARMSX2"`

The core's Switch 2 build tree, its prebuilt dependencies (`third_party/ouncesx2-eval/deps-sw2`)
and the prebuilt Mesa/NVK Vulkan driver (`third_party/ouncevk-20261118`) are not in this
repository.

## Credits

- [OunceSX2](https://github.com/nyathea/OunceSX2), [ARMSX2](https://github.com/ARMSX2/ARMSX2) and
  [PCSX2](https://github.com/PCSX2/pcsx2): the emulator core (GPL-3.0+). OunceSX2 also showed how to
  set Switch 2 clock rates from hbmenu.
- [libretro ps2 (LRPS2)](https://github.com/libretro/ps2): the earlier core.
- Per-game CPU settings from the community-tested AetherSX2/NetherSX2 playable lists by
  Darkelf2024 (CC BY 4.0).
- Fonts: Roboto (Apache 2.0), Source Code Pro (OFL 1.1) — licences in `romfs/licenses`.
