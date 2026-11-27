# SW2PS2

> **Early development:** SW2PS2 is new and there will be bugs. Please report them in
> [Issues](https://github.com/Rodpad/SW2PS2/issues) with the game and what happened. If you can, turn on
> Settings -> App -> Debug logging (shown with Advanced options), make it happen again and attach
> `/switch/sw2ps2/log.txt` from the SD card.

A PlayStation 2 emulator for the Nintendo Switch 2 (hbmenu / kaiHEN), built on the
[OunceSX2](https://github.com/nyathea/OunceSX2) core (ARMSX2 / PCSX2 lineage) with its own
native front end.

No games, BIOS files or other copyrighted material are included. You need your own PS2
BIOS (default folder `/bios/ps2`) and your own game dumps (default folder `/roms/ps2`); both
folders can be changed in Settings -> App. The app keeps its own files in `/switch/sw2ps2`
(save states in `states/`, per-game settings in `settings/`).

## Features

### Games
- **Box art browsing, scraped on the console:** browse your games as a grid of box covers or as a
  list, with a details panel showing the box in 3D (front, back and sides; drag to turn it), the
  title, publisher, year, genre, region and description. Box art and details are downloaded on
  the console from [ScreenScraper](https://www.screenscraper.fr), with no account needed and no PC
  required (Settings -> App -> Download missing box art and descriptions; the app offers it the first
  time games have none). Four views (List, List & details, Grid, Grid & details) on the right stick
  press. Games are matched by their disc serial (`.iso` and `.cue`/`.bin`); with details downloaded,
  the list shows each game's proper title.
- **Game list** with All / Favourites / Recent tabs (L / R). Games are found in the games
  folder and any folders inside it: `.iso`, `.chd`, `.cso`, `.zso`, `.ciso`, `.gz`, `.m3u`, and `.cue` with its
  `.bin`. The BIOS is found the same way, in its folder or folders inside it.
- **Per-game settings:** a setting changed in a game's menu is kept for that game; Settings holds
  the defaults for all games.
- **Menus wrap round:** in the game list, Settings and the in-game menu, going past the bottom
  jumps to the top and past the top to the bottom.
- **Search and sort:** the game list's header row has [L] All / Favourites / Recent [R], a search
  field (with a clear button) and the sort order (name, last played, date added). Buttons: A play,
  L / R tabs, ZL / ZR page up / down (hold to keep paging), left stick press the next sort order,
  right stick press the next view, − Favourite, + Search, Y Refresh, X Settings, B Exit (press
  twice). The list and grid wrap round, stopping on the header row and the footer on the way. On a
  single Joy-Con, SL switches tabs and SR pages down.
- **Touch on every screen:** tap anything (it lights while held and acts when released), drag to
  scroll, tap outside a window to close it. During play an on-screen Menu button appears when
  the game is touched (Settings -> App: When touched / Always / Hidden).
- **UI size:** Normal (larger rows and text, easier to tap; the default) or Compact, in
  Settings -> App.
- **Themes:** Dark, Light, Black, Blue, Purple, Red, Green, Orange, Retro and Retro Colour.
- **Built-in speed settings** for demanding games, measured on the Switch 2 or taken from the
  community AetherSX2/NetherSX2 lists, so most games start well without tuning.

### In-game menu
Opened with L + R + L3 + R3 (SL + SR + stick press on a single Joy-Con), in tabs:
- **Game:** resume, save and load state (several slots, with optional auto-load when a game
  starts), save screenshot, cheats, reset, quit.
- **Emulation**, applied straight away: PS2 CPU speed and cycle skip, GPU readbacks
  (Accurate / Fast), Switch 2 CPU clock, internal resolution (1x and up), widescreen patches,
  and a stats overlay (FPS, full statistics, or with a frame-time graph).
- **Picture:** aspect ratio (4:3, 16:9, stretch, 1:1), integer or fit scaling, overscan cropping,
  shaders (sharp bilinear, Scale2x, LCD grid, and CRT shaders: CRT-Geom, CRT-Lottes, zfast CRT,
  CRT-Easymode, CRT-Aperture, CRT-Royale), signal look (RGB, composite, composite with dot
  crawl, S-Video), brightness boost, screen curvature, rounded corners and vignette.
- **Controls:** see below.

The menu can pause the game and dim it behind (both optional). Less common options appear with
Settings -> App -> Advanced options.

The in-game Emulation tab also offers per-game **Blending accuracy** with Advanced options
shown. The default follows PCSX2's game database: each game gets the level it recommends
(and any limit it sets), and Basic where it names none. Choosing a level overrides that for
the game. Lower levels may change visual effects; higher ones can cost speed, mainly at higher
resolutions.

### Cheats

Open **Game → Cheats…** while playing. SW2PS2 lists the codes for the disc's serial and ELF CRC
and remembers the codes you enable for that revision. The starting database covers Final Fantasy X
International (Japan) and Final Fantasy X (North America).

To add your own codes, copy a PCSX2 PNACH file with `[named cheat]` sections to the exact path shown
under **Add a PNACH file…**, then restart the game. The default folder is
`/switch/sw2ps2/system/pcsx2/cheats/` and the filename is `SERIAL_CRC.pnach`.
For Final Fantasy X International, enable **All Al Bhed Primers** after loading your save, resume
the game, and check **Items → Key Items** for volumes I–XXVI. Save in-game to keep the primers.

### Display and sound
- 60 Hz, or 120 Hz with VRR on displays that support it; adjustable frame delay.
- Volume, audio buffer size and controller vibration strength.

## Controllers

- **Up to four players.** Players 1 and 2 use the PS2's two controller ports. For games made
  for the PS2 Multitap (TimeSplitters: Future Perfect, Tekken Tag Tournament's Pair Play and
  others), turn on **Multitap (players 3-4)** in the in-game menu's Controls tab (saved for that
  game) or in Settings -> Sound & controls (the default for all games). Players 1-4 are then the
  Multitap's four slots, as on a real PS2. Some games need each player to press Start to join.
- **Pairing.** "Pair controllers" opens the Switch's own controller screen, with four slots,
  from Settings or mid-game. Every connected player can move around SW2PS2's menus.
- **Button remapping.** Each player's PS2 buttons can be mapped one by one on an interactive
  screen: press the Switch button you want for each PS2 button, or wait to leave it unmapped.
  Stick directions can be mapped too (for example, the stick as the D-pad). Layouts made in
  Settings apply to every game; layouts made from the in-game Controls tab apply only to that
  game. "Reset buttons to standard" puts everyone back.
- **Single Joy-Cons (experimental).** A Joy-Con held sideways works as its own controller: the
  stick and buttons are turned to match, menus show position-based button hints, and its
  default layout is SL / SR = L1 / R1, stick press = L3, - or + = Start, the four buttons the
  PS2's by position, and the stick as both analog stick and D-pad. SL + SR + stick press opens
  the in-game menu (L + R + L3 + R3 on other controllers). Games needing L2 / R2 / Select / R3
  need a remap or a fuller controller.

## SW2PS2 core improvements

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
- **Faster hand-offs between the emulated processors.** The emulated CPU no longer waits for
  memory to settle each time it hands graphics work to the vector unit or checks on the I/O
  processor, and EE interrupt scheduling skips a needless read from the I/O processor's core.
- **Graphics driver.** SW2PS2 links the OunceVK 20261117.1 release and shuts its Vulkan device
  down fully at exit. (OunceVK 20261118 to 20261123.1 crashed on the way back to hbmenu.)
- **Per-game speed settings built in.** Demanding games start with emulated-CPU settings
  measured on the Switch 2, falling back to the community-tested AetherSX2/NetherSX2 lists. Any
  game can be adjusted from the in-game menu, and the app tells you once if a game falls below
  full speed.

### Stability

- **Several games in one session.** Starting a second game and loading a state no longer
  crashes. The cause was the core's background VU compiler, which SW2PS2 turns off; it gave no
  measurable speed. A new game now starts in the same session instead of restarting the app
  through the homebrew loader, which on kaiHEN 20261117.1 could drop to the HOME menu.
- **Memory reserved up front.** The emulated PS2's 293 MB data area must sit in a 2 GB region
  where thread stacks land at random addresses. It is now reserved as soon as the app starts and
  reused by every game; searched for at game start, it was sometimes missing, and the game stayed
  on a black screen.
- **Burnout Revenge boots.** Two game-database graphics hooks meant for the other Burnout games
  stopped the hardware renderer on the Switch 2; Burnout Revenge skips them.
- **Loading a state right after boot.** A state load is held until the game itself has
  started; loading while the BIOS was still booting crashed God of War II.
- **No crash on the way back to hbmenu.** OunceVK 20261118 to 20261123.1 crashed there after
  10-20 s of rendering, even with no game played; SW2PS2 links 20261117.1 (measured at the same
  speed).
- **Games that play videos with the software renderer** (the game database's SoftwareRendererFMV,
  194 entries, e.g. Naruto: Narutimate Hero 3) showed a black screen and crashed: switching
  renderers rebuilt the graphics on SW2PS2's own Vulkan / OpenGL context. Their videos now stay on
  the hardware renderer.
- **Games that draw sprites on the CPU** (the game database's cpuSpriteRenderBW, 318 entries, e.g.
  Dark Cloud 2) crashed: the ARM64 JIT's code for some of those draws (a bilinear paletted sprite)
  is faulty. The hardware renderer's CPU draws use the C scanline functions instead; the game
  database's fix stays on, so those games keep their correct graphics.

## Layout

| Path | What |
|---|---|
| `source/` | the front end (game list, menus, settings, video/audio output, test harness) |
| `native/` | glue between the front end and the core (libretro bridge, Horizon helpers; two files of OunceSX2's own app compiled in) |
| `patches/` | SW2PS2's changes to the emulator cores, against the upstream commits in `patches/upstreams.txt` |
| `romfs/` | fonts (with their licences), picture shaders and the bundled PNACH cheat database |
| `icon.png`, `sw2ps2.jpg` | the app icon: the lossless master and the JPEG the build uses (`sw2ps2-test.jpg`: test builds) |
| `tools/` | speed-list generator, profiling helpers, third-party setup |
| `tests/` | on-console test runner (`remote_test.sh`) and its log summariser |
| `probe/` | a small hardware probe app |

## Building

Needs devkitPro (devkitA64) with the ounce-devkit Switch 2 additions (libounce, kaiHEN, OunceVK).

1. `tools/setup_third_party.sh` clones the cores at the pinned commits and applies `patches/`.
2. `tools/build_core.sh` builds libjpeg-turbo (from the core's own copy) and the core, with the
   exact settings the app is built with (CMake + Ninja).
3. `make` builds the app, linking the OunceVK 20261117.1 driver that step 1 unpacks into
   `third_party/` (`make OUNCEVK=` uses the devkit's copy instead).

After changing what the app links, `tools/make_notices.py` regenerates
`romfs/licenses/THIRD_PARTY_NOTICES.txt`.

Box art downloads use [ScreenScraper](https://www.screenscraper.fr) and need a ScreenScraper
developer account, which is not in this repository: put `devid=...` and `devpassword=...` on two
lines in `~/.config/sw2/screenscraper.txt` and run `tools/make_ss_dev.sh`, which writes the
ignored `source/ss_dev.h`. Without it the app builds and runs but cannot download box art.

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
