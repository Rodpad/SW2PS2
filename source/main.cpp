// SW2PS2 for hbmenu (Switch 2 homebrew, kaiHEN): PlayStation 2 emulator front end
// (the SW2NES/SW2SNES front end on the OunceSX2 core).
// A port of the CivHomebrew app (app/sw2nes.js): the same screens, settings,
// save formats and defaults, drawn with OpenGL ES at 1920x1080.
#include <deque>
#include <switch.h>
#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <malloc.h>
#include <functional>
#include <string>
#include <vector>

#if __has_include("iteration_build_id.h")
#include "iteration_build_id.h"
#else
#define SW2_ITERATION_ID "manual"
#endif

#include "audio.h"
#include "core.h"

// The active emulation core (chosen per game start; see Settings "Core").
static const CoreApi* C = &PS2_CORE;
#include "crt.h"
#include "uigfx.h"
#ifdef SW2_VK
#include "vkpresent.h"
extern int sw2_osd_stats;  // native/armsx2_compat.cpp
#endif
#include "json.h"
#include <memory>
#include <sstream>
#include <minizip/unzip.h>

#ifndef APP_VERSION
#define APP_VERSION "2.0.0"
#endif

using std::string;
using std::vector;
using gfx::Color;

/* ---------------- Constants (as in sw2nes.js) ---------------- */
// ROM folder: sdmc:/roms/nes by default (the shared roms/<system> layout that
// RetroArch users and frontends use); any folder can be chosen in Settings.
static const char* DEFAULT_ROM_DIR = "sdmc:/roms/ps2";
static std::string romDir = DEFAULT_ROM_DIR;
static const char* DEFAULT_BIOS_DIR = "sdmc:/bios/ps2";
static std::string biosDir = DEFAULT_BIOS_DIR;  // where the PS2 BIOS dump is (Settings -> BIOS folder)
static bool pickForBios = false;  // the folder picker is choosing the BIOS folder, not the ROM folder
static const char* DATA_DIR = "sdmc:/switch/sw2ps2";
static double NES_RATE = 59.94;  // the running game's frame rate (59.94 NTSC, 50 PAL)
// Npad button bits (the same values the JS app uses).
enum { B_A = 1, B_B = 2, B_X = 4, B_Y = 8, B_L = 64, B_R = 128, B_PLUS = 1024, B_MINUS = 2048,
       B_LEFT = 4096, B_UP = 8192, B_RIGHT = 16384, B_DOWN = 32768, B_SL = 65536, B_SU = 131072, B_SR = 262144, B_SD = 524288 };
// SNES joypad bits (snes9x.h SNES_*_MASK).
enum { S_R = 1 << 4, S_L = 1 << 5, S_X = 1 << 6, S_A = 1 << 7, S_RIGHT = 1 << 8, S_LEFT = 1 << 9, S_DOWN = 1 << 10, S_UP = 1 << 11,
       S_START = 1 << 12, S_SELECT = 1 << 13, S_Y = 1 << 14, S_B = 1 << 15 };

static const uint32_t C_BG = 0x0b0d12, C_PANEL = 0x151924, C_LINE = 0x2a3142, C_TEXT = 0xeef1f7, C_DIM = 0x9aa3b5, C_ACCENT = 0xe4463b, C_SEL = 0x2b3550;
static Color col(uint32_t c, float a = 1) { return gfx::rgb(c, a); }

static const vector<string> ASPECTS = {"1:1", "16:9", "4:3", "Stretch"}, SCALES = {"Integer", "Fit"}, CROPS = {"Show all", "Hide sides", "Hide all edges"};
static const vector<string> CRT_STYLES = {"Off", "Scanlines", "Soft scanlines", "Bold scanlines", "Aperture grille", "Scanlines + grille",
                                          "Slot mask", "Shadow mask", "Dot mask", "LCD matrix", "Screen door"};
static const vector<int> CRT_CHOICES = {0, 1, 2, 3, 4, 5, 9, 10};
// Picture effects: rounded corners and vignette (all black-only, default off).
static const vector<string> CORNER_RADII = {"Off", "Small", "Medium", "Large", "Extra large"};
static const float CORNER_RADIUS_FRAC[5] = {0, 0.04f, 0.07f, 0.11f, 0.16f};  // of picture height
static const vector<string> CORNER_SOFTNESS = {"Sharp", "Low", "Medium", "High"};
static const float CORNER_SOFT_FRAC[4] = {0, 0.006f, 0.015f, 0.03f};  // of picture height ("Sharp" = 1 px anti-aliasing)
static const vector<string> VIGNETTES = {"Off", "Light", "Medium", "Strong"};
static const float VIGNETTE_STRENGTH[4] = {0, 0.3f, 0.5f, 0.75f};
static const vector<string> VIGNETTE_SIZES = {"Small", "Medium", "Large"};
static const float VIGNETTE_START[3] = {0.75f, 0.5f, 0.2f};  // where darkening begins (0 centre .. 1 corner)
// GPU shaders (romfs:/shaders). "None" keeps exact pixels and the CRT styles.
static const vector<string> SHADERS = {"None", "Sharp bilinear", "Bilinear", "Scale2x", "CRT-Geom", "CRT-Lottes", "zfast CRT", "CRT-Easymode", "CRT-Aperture", "LCD grid", "CRT-Royale"};
static const vector<string> SHADER_FILES = {"none.glsl", "sharp_bilinear.glsl", "bilinear.glsl", "scale2x.glsl", "crt_geom.glsl", "crt_lottes.glsl", "zfast_crt.glsl", "crt_easymode.glsl", "crt_aperture.glsl", "lcd_grid.glsl", "crt_royale.glsl"};
// Brightness boost (AutoHDR-style lift in SDR): exponent k of 1-(1-c)^k.
static const vector<string> BOOSTS = {"Off", "Low", "Medium", "High"};
static const float BOOST_K[4] = {1.0f, 1.3f, 1.6f, 2.0f};

static const vector<string> SIGNALS = {"RGB", "Composite", "Composite + dot crawl", "S-Video"};
static const vector<string> CURVATURES = {"Off", "Light", "Medium", "Strong"};
static const float CURVATURE[4] = {0, 0.03f, 0.055f, 0.09f};
// Latency and playback options (all games).
static const vector<string> RUN_AHEADS = {"Off", "1 frame", "2 frames"};
static const vector<string> FRAME_DELAYS = {"Off", "4 ms", "8 ms"};  // 12 ms overran 60 Hz refreshes
static const int FRAME_DELAY_MS[3] = {0, 4, 8};
static const vector<string> AUDIO_BUFFERS = {"Small", "Normal", "Large"};
static const int AUDIO_TARGET_FRAMES[3] = {1920, 2880, 4320};  // queued audio: 40 / 60 / 90 ms at 48 kHz
static const vector<string> DISPLAY_MODES = {"60 Hz", "120 Hz / VRR"};
static const vector<string> FF_MODES = {"Off", "Hold ZR", "Toggle ZR"};
static const vector<string> FF_SPEEDS = {"1.5x", "2x", "3x", "4x"};
static const double FF_FACTOR[4] = {1.5, 2, 3, 4};
static const vector<string> STATS_LEVELS = {"Off", "FPS", "Full", "Full + graph"};
static const vector<string> OFF_ON = {"Off", "On"};
// Button mapping: Switch A, B, X, Y, L, R -> SNES buttons (same layout by
// default), turbo versions of the face buttons, or nothing.
enum { NFACE = 6 };
static const vector<string> FACE_KEYS = {"A", "B", "X", "Y", "L", "R"};
static const vector<string> FACE_CHOICES = {"SNES A", "SNES B", "SNES X", "SNES Y", "SNES L", "SNES R", "Turbo A", "Turbo B", "Turbo X", "Turbo Y", "None"};
static const int FACE_BITS[NFACE] = {B_A, B_B, B_X, B_Y, B_L, B_R}, FACE_TARGETS[6] = {S_A, S_B, S_X, S_Y, S_L, S_R}, FACE_DEFAULT[NFACE] = {0, 1, 2, 3, 4, 5};
static const vector<string> TURBO_RATES = {"10 Hz", "15 Hz", "20 Hz", "30 Hz"};
static const int TURBO_PERIOD[4] = {6, 4, 3, 2};
static const vector<string> VOLUMES = {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"};
static const vector<string> FILTERS = {"Off", "Soft", "Strong"};
static const vector<string> LIST_VIEWS = {"All", "Favourites", "Recent"};
static const int RECENT_MAX = 5, SLOT_COUNT = 4;
static const vector<string> EE_WAIT_CHOICES = {"0.125x (384)", "0.25x (768)", "0.5x (1536)", "0.75x (2304)", "1x (3072)", "2x (6144)", "4x (12288)", "8x (24576)", "16x (49152)", "32x (98304)"};
static const vector<uint32_t> EE_WAIT_VALUES = {384, 768, 1536, 2304, 3072, 6144, 12288, 24576, 49152, 98304};
extern "C" uint32_t sw2_ee_wait_cycles;

/* ---------------- State ---------------- */
// Defaults (the author's chosen setup, 2026-11-14).
static int aspectMode = 2, scaleMode = 1, cropMode = 0, crtStyle = 0, paletteMode = 0, statsLevel = 0;
static int runAhead = 0, frameDelay = 0, audioBuffer = 1, displayMode = 1, ffMode = 0, ffSpeed = 1;
static bool rewindOn = false;
static bool sw2_audio_panic_refill = true;
// PS2 options (apply when a game starts).
#ifdef SW2_VK
static const vector<string> RENDERERS = {"GPU (Vulkan)", "Software", "GPU (OpenGL)"};  // Vulkan first: the default (a saved "GPU" becomes Vulkan)
static const vector<string> RESOLUTIONS = {"1x (native)", "2x", "4x"};
#else
static const vector<string> RENDERERS = {"GPU", "Software"}, RESOLUTIONS = {"1x (native)", "2x", "4x"};
#endif
static const char* RESOLUTION_VALUES[3] = {"1x (Native)", "2x", "4x"};
static const int RESOLUTION_SCALE[3] = {1, 2, 4};
static const vector<string> EE_SPEEDS = {"100%", "75%", "60%", "50%"};
static const vector<string> FPU_MODES = {"Fast", "Exact"};
static const vector<string> BLENDING_LEVELS = {"Minimum", "Basic", "Medium", "High", "Full", "Maximum"};
static std::map<string, int> gameBlending;  // 0 = default, 1-6 = BLENDING_LEVELS
extern int sw2_blending_explicit __attribute__((weak));
// GPU readbacks, per game: Accurate (default) waits for the GPU each time; Fast is the core's
// "Unsynchronized" download mode (Armored Core 3 28 -> 38 fps) but breaks games that build effects
// from read-back frames (Ico's glow came out as dark horizontal bars).
static const vector<string> READBACKS = {"Accurate", "Fast"};
static vector<string> fastReadbackGames;  // explicit Fast (older settings)
static std::map<string, int> gameReadback;  // 0 Auto, 1 Accurate, 2 Fast
static int autoReadback = -1;  // test runs: -1 = the game's own setting  // Fast = PCSX2-style single precision (sw2_fast_fpu)
static const char* EE_SPEED_VALUES[4] = {"100% (Normal Speed)", "75% (Underclock)", "60% (Underclock)", "50% (Underclock)"};
// EE cycle skip: fewer emulated CPU cycles per frame (a speed hack; games with spare CPU time run the
// same, heavy scenes can slow down inside the game).
static const vector<string> CYCLE_SKIPS = {"Off", "Mild", "Moderate", "Maximum"};
static const char* CYCLE_SKIP_VALUES[4] = {"disabled", "Mild Underclock", "Moderate Underclock", "Maximum Underclock"};
static int ps2CycleSkip = 0;  // global default (Settings)
// Per game: index into {"Auto", <EE_SPEEDS>...} / {"Auto", <CYCLE_SKIPS>...}; 0 (Auto) = the built-in
// value for known heavy games (gameSpeedDefaults), else the global setting.
static std::map<string, int> gameRate, gameSkip, gameRes;  // gameRes: 0 = the global resolution, else RESOLUTIONS index + 1
static double tuneQuietUntil = 0; static bool slowNoticeShown = false; static std::deque<double> tuneWin;  // slowdown notice (see slowdownNotice)
// Per-game CPU timing from the community-tested AetherSX2/NetherSX2 playable lists (Darkelf2024,
// CC BY 4.0; see source/speedlist.inc and tools/make_speedlist.py). Matched on the normalised title
// (the file name). Only the EE cycle rate and skip are used -- they change no effects; the lists'
// hardware-download column is not applied (non-Accurate modes broke Ico's glow), see fastReadback.
struct SpeedDefault { const char* keyword; int rate; int skip; const char* hw; const char* title; int idxRate; bool fastReadback; };
struct SpeedRow { const char* key; int rate; int skip; const char* hw; };
static const SpeedRow SPEED_LIST[] = {
#include "speedlist.inc"
};
// Measured on the Switch 2 with the user's save states (CPU clock Maximum): what reaches ~60 fps.

// The file name without its extension and region/version tags, as letters and digits ("&" -> "and").
static string titleKey(const string& name) {
  string n = name; size_t dot = n.rfind('.'); if (dot != string::npos) n = n.substr(0, dot);
  string k; int depth = 0;
  for (size_t i = 0; i < n.size(); i++) {
    char c = n[i];
    if (c == '(' || c == '[') { depth++; continue; }
    if (c == ')' || c == ']') { if (depth) depth--; continue; }
    if (depth) continue;
    if (c == '&') { k += "and"; continue; }
    if (isalnum((unsigned char)c)) k += (char)tolower((unsigned char)c);
  }
  return k;
}
static bool skipMeasured = false;  // test runs: "measured": false starts games from the community list / defaults
static const SpeedDefault* speedDefault(const string& name) {
  static SpeedDefault found; static string lastName; static bool have = false;
  if (name == lastName && !skipMeasured) return have ? &found : nullptr;
  lastName = name; have = false;
  string k = titleKey(name), alt = k.rfind("the", 0) == 0 ? k.substr(3) : "the" + k;
#ifdef SW2_ARMSX2
  // Measured on the Switch 2 (ARMSX2 core, Vulkan, the user's saves, 2026-11-18): the settings that
  // come closest to 60 fps with the most frames shown. EE cycle skip cost more shown frames than it
  // gained in every game measured, so none uses it except Burnout (unmeasured on this core).
  // Index: rate 1 = 75%, 2 = 60%, 3 = 50%. These win over the community list below.
  struct Measured { const char* key; int rate; int skip; bool fastReadback; };
  static const Measured MEASURED[] = {
    // (God of War II: no entry since 2026-11-19 -- at 100% it now runs 60/60; 50% only cost it frames)
    {"shadowofthecolossus", 1, 0, false},   // 75%: 60 emu / 30 shown; 100% falls behind (55 emu), 60% and 50% show fewer frames (re-measured 2026-11-19)
    {"thesimpsonshitandrun", 1, 0, false},  // 75%: 50 (PAL full speed) / 45 shown; 100% 40 emu, 60% 38 shown (re-measured 2026-11-19)
    {"tekkentagtournament", 1, 0, false},   // 75%: 57 emu / 57 shown; 100% 55 (2026-11-19)
    {"armoredcore3", 0, 0, true},           // 46 -> 60, all 60 shown (readbacks only, no CPU change)
  };
  for (const Measured& m : MEASURED)
    if (!skipMeasured) if (k == m.key || alt == m.key) {
      found = SpeedDefault{m.key, m.rate, m.skip, "", m.key, m.rate, m.fastReadback};
      have = true; return &found;
    }
#endif
  for (auto& r : SPEED_LIST) {
    if (k != r.key && alt != r.key) continue;
    const int idx = r.rate <= -3 ? 3 : r.rate == -2 ? 2 : r.rate == -1 ? 1 : 0;  // AetherSX2 -1/-2/-3 = 75/60/50%
    found = SpeedDefault{r.key, idx, r.skip, r.hw, r.key, idx, false};
    have = true; break;
  }
  return have ? &found : nullptr;
}
static int ps2EeSpeedGlobal();
static int autoRate(const string& name);
static int effectiveRate(const string& name) {
  auto it = gameRate.find(name); if (it != gameRate.end() && it->second > 0) return it->second - 1;
  return autoRate(name);
}
// Games that keep 100% whatever the default: at 75% they show far fewer frames (measured on the
// Switch 2, 2026-11-19: TimeSplitters: Future Perfect 57 -> 43 game fps; God of War II the same at both).
static bool fullSpeedGame(const string& name) {
  const string k = titleKey(name);
  return k == "godofwarii" || k == "timesplittersfutureperfect";
}
static int autoRate(const string& name) {
  if (fullSpeedGame(name)) return 0;
  // 75% by default (the global setting); games the community lists or our measurements set lower keep that.
  const SpeedDefault* d = speedDefault(name); int r = d ? std::max(d->rate, ps2EeSpeedGlobal()) : ps2EeSpeedGlobal();
  return std::min(r, 3);
}
static int autoSkip(const string& name) { const SpeedDefault* d = speedDefault(name); return d ? std::max(d->skip, ps2CycleSkip) : ps2CycleSkip; }
// GPU readbacks: Fast unless this game is set to Accurate in the in-game menu (Fast measured as
// fast or faster in every game tried, 2026-11-19, with the same picture; Armored Core 3 49 -> 60).
static bool fastReadback(const string& name) {
  if (autoReadback >= 0) return autoReadback == 1;  // (test runs)
  auto rb = gameReadback.find(name);
  if (rb != gameReadback.end() && rb->second > 0) return rb->second == 2;
  return true;
}
static int effectiveSkip(const string& name) {
  auto it = gameSkip.find(name); if (it != gameSkip.end() && it->second > 0) return it->second - 1;
  if (const SpeedDefault* d = speedDefault(name)) return std::max(d->skip, ps2CycleSkip);
  return ps2CycleSkip;
}
static int ps2Renderer = 0, ps2Resolution = 0, ps2EeSpeed = 1, ps2Fpu = 0;
static int effectiveRes(const string& name) { auto it = gameRes.find(name); return it != gameRes.end() && it->second > 0 ? it->second - 1 : ps2Resolution; }
static int ps2EeSpeedGlobal() { return ps2EeSpeed; }
// CPU clock while a game runs: 0 normal, 1 the system's boost mode (CPU 1523 MHz, but the GPU drops
// to 306 MHz), 2 the CPU's maximum through the clock service (1702 MHz, GPU unchanged; the approach
// comes from OunceSX2, https://github.com/nyathea/OunceSX2). Default: maximum.
static const vector<string> CPU_CLOCKS = {"Normal", "Boost mode", "Maximum"};
static int cpuClock = 2;
static int gpuClock = 0;  // 0 normal, 1 maximum (clock service) -- "GPU clock" setting
static const vector<string> GPU_CLOCKS = {"Normal", "Maximum"};
static int renderScale = 1;  // the running game's resolution multiplier
static bool runningHw = false;  // the running game is on a GPU renderer (resolution can change)
// Advanced options: hidden by default, leaving what a console's own emulator offers.
static bool showAdvanced = false;  // default: hidden (also after Reset all settings)
static const vector<string> ADVANCED_SETTINGS = {"Debug logging", "Emulated CPU speed (next start)", "CPU boost mode (next start)", "Scale", "Overscan", "Signal", "Curvature", "Corner radius", "Corner softness", "Vignette", "Vignette size",
    "Brightness boost", "Stats", "Run-ahead", "Frame delay (60 Hz)", "Audio buffer", "Display", "Fast-forward speed", "Dim game in menu", "Audio filter", "Turbo speed"};
static const vector<string> ADVANCED_MENU = {"Run-ahead", "Stats", "Scale", "Overscan", "Signal", "Curvature", "Corner radius", "Corner softness", "Vignette", "Blending accuracy",
    "Vignette size", "Brightness boost", "Settings for", "Resolution"};
static int cornerRadius = 0, cornerSoftness = 1, vignette = 0, vignetteSize = 0, curvature = 0, shader = 6, signalMode = 0, boost = 0;
// Button mapping per player (Switch A, B, X, Y, L, R -> FACE_CHOICES).
static int faceMap[NFACE] = {0, 1, 2, 3, 4, 5}, faceMap2[NFACE] = {0, 1, 2, 3, 4, 5};
static int remapPlayer = 0;  // Settings: whose buttons the mapping rows edit (not saved)
static int* curMap() { return remapPlayer ? faceMap2 : faceMap; }
static int turboRate = 1, volumeLevel = 10, audioFilter = 0;
static bool autoLoadState = false, pauseInMenu = false, dimInMenu = false;
// At most one frame in flight: input is read only after the previous frame
// finished on the GPU (the driver default let frames queue up: 100+ ms lag).
static const int frameQueue = 1;
static vector<string> recentGames, favourites, playHistory;
static Json lastPlayedDates = Json::object();
static const vector<string> VIB_LEVELS = {"Off", "Low", "Normal", "High"};
static const float VIB_GAIN[4] = {0.0f, 0.5f, 1.0f, 1.6f};
static int vibLevel = 2;  // index into VIB_LEVELS: Normal by default
static bool vibration = true;  // (settings before vibLevel: on = Normal, off = Off)
static vector<string> widescreenGames;  // games shown at 16:9, with the core's widescreen patch from their next start

enum RomSort { DefaultOrder, NameAsc, NameDesc, PlayedAsc, PlayedDesc, AddedAsc, AddedDesc };
static const vector<string> ROM_SORT_LABELS = {
    "Default order", "Name (A-Z)", "Name (Z-A)", "Last played (oldest first)",
    "Last played (newest first)", "Date added (oldest first)", "Date added (newest first)"
};
static const vector<int> DEFAULT_ROM_SORT_BY_VIEW = {NameAsc, NameAsc, PlayedDesc};
static vector<int> romSortByView = DEFAULT_ROM_SORT_BY_VIEW;

struct Rom { string name, dir; u64 added = 0; };
static vector<Rom> allRoms, roms;
static int selected = 0, top = 0, listView = 0, btnIndex = 0;
static string focusArea = "list";
static double exitArmed = 0;

struct GameVals { int aspect, scale, crop, crt, palette, faces[NFACE], faces2p[NFACE], turbo, volume, filter, corner, cornerSoft, vignette, vignetteSize, curve, shader, signal, boost; };
struct Running {
  string name, base, stateBase, settingsBase;  // base: <data>/<game> (saves); states and per-game settings in their own folders
  bool battery = false, perGame = false;
  uint32_t sramHash = 0;
  double sramCheck = 0;
  GameVals globalVals;
  Json slots = Json::object();
};
static Running* running = nullptr;
static string mode = "browser";
static bool quitApp = false;
static int stateSlot = 1, turbo = 0, paletteApplied = -1;

// UI state
static double toastUntil = 0, hintUntil = 0;
static string toastText, messageText;
static std::function<void()> messageThen;
static int settingsIndex = 0, settingsTop = 0;

static double nowMs() { return armTicksToNs(armGetSystemTick()) / 1e6; }
static FILE* logFile = nullptr;
// Button mapping, per player: for each PS2 button (PS2_BUTTONS order) the Switch button (its
// HidNpadButton bit) that presses it, or -1 for none. Settings -> Sound & controls remaps it.
struct Ps2Button { const char* name; int id; int def; };  // id: libretro joypad id; def: default Switch bit
// libretro joypad ids: B0 Y1 SELECT2 START3 UP4 DOWN5 LEFT6 RIGHT7 A8 X9 L10 R11 L2 12 R2 13 L3 14 R3 15
static const Ps2Button PS2_BUTTONS[16] = {
    {"Cross", 0, 1}, {"Circle", 8, 0}, {"Square", 1, 3}, {"Triangle", 9, 2},
    {"L1", 10, 6}, {"R1", 11, 7}, {"L2", 12, 8}, {"R2", 13, 9}, {"L3", 14, 4}, {"R3", 15, 5},
    {"Start", 3, 10}, {"Select", 2, 11}, {"D-pad up", 4, 13}, {"D-pad down", 5, 15}, {"D-pad left", 6, 12}, {"D-pad right", 7, 14}};
static int padMap[4][16];  // players 1-4 (3-4 only with the Multitap)
// Per-game layouts, made from the in-game menu: a game listed here uses its own instead of padMap.
struct GamePadMap { int m[4][16]; };
static std::map<string, GamePadMap> gamePadMaps;
// The Multitap (players 3-4): Settings' default, and a game's own choice from the in-game menu.
static bool multitapDefault = false;
static std::map<string, bool> gameMultitap;
static bool multitapFor(const string& name) { auto it = gameMultitap.find(name); return it != gameMultitap.end() ? it->second : multitapDefault; }
static void resetPadMap(int player) { for (int i = 0; i < 16; i++) padMap[player][i] = PS2_BUTTONS[i].def; }
static bool padMapIsDefault(int player) { for (int i = 0; i < 16; i++) if (padMap[player][i] != PS2_BUTTONS[i].def) return false; return true; }
// The Switch inputs a mapping can use: buttons (bits 0-15), each stick's four directions (16-23, as the
// system reports them past its threshold), and the Joy-Cons' SL / SR (24-27).
static const u64 MAPPABLE = 0xFFFFFFull | (0xFull << 24);
static const char* switchButtonName(int bit) {
  static const char* N[28] = {"A", "B", "X", "Y", "Left stick press", "Right stick press", "L", "R", "ZL", "ZR", "+", "-",
                              "D-pad left", "D-pad up", "D-pad right", "D-pad down",
                              "Left stick left", "Left stick up", "Left stick right", "Left stick down",
                              "Right stick left", "Right stick up", "Right stick right", "Right stick down",
                              "SL (left Joy-Con)", "SR (left Joy-Con)", "SL (right Joy-Con)", "SR (right Joy-Con)"};
  return bit >= 0 && bit < 28 ? N[bit] : "";
}
// Automated test (autotest.json, see main): overrides apply to one run and are not saved.
static bool autoTest = false, autoAgent = false, autoWaiting = false;
static double autoActivityNext = 0;
static bool autoActivityLogged = false;
static bool autoFixedSettings = false;
static int autoCycleSkip = 0;
extern "C" void sw2ax_vk_test_options(bool, unsigned, bool, bool, bool, bool) __attribute__((weak));
extern "C" void sw2ax_vk_test_counters(uint64_t*) __attribute__((weak));
extern "C" void sw2ax_vk_trace_pass_ends(bool) __attribute__((weak));
extern "C" uint64_t sw2ax_vk_pass_end_summary(unsigned*, unsigned*, unsigned) __attribute__((weak));
extern "C" void sw2ax_vk_target_break_summary(unsigned*, unsigned*) __attribute__((weak));
extern "C" void sw2ax_trace_vu0_blocks(bool) __attribute__((weak));
extern "C" void sw2ax_trace_spr(bool) __attribute__((weak));
extern "C" void sw2ax_spr_counts(uint64_t*) __attribute__((weak));
extern "C" void sw2ax_trace_frame_work(bool) __attribute__((weak));
extern "C" uint64_t sw2ax_frame_work_counts(uint64_t*) __attribute__((weak));
extern "C" void sw2ax_trace_vif_copies(bool) __attribute__((weak));
extern "C" void sw2ax_vif_copy_counts(uint64_t*) __attribute__((weak));
extern "C" void sw2ax_trace_vu_notifications(bool) __attribute__((weak));
extern "C" void sw2ax_vu_notification_counts(uint64_t*) __attribute__((weak));
static bool autoMeasureVulkan = false;
static bool autoTraceVuNotifications = false;
static bool autoTraceVifCopies = false;
static bool autoTraceFrameWork = false;
static bool autoTraceSpr = false;
static bool autoTracePassEnds = false;
static bool autoMeasureHandoff = false;
static uint64_t autoVulkanStart[9];
static string autoReuseLogPath;
static double autoTestEnd = 0, autoWaitSince = 0, autoPollAt = 0;
static unsigned long long autoHeartbeat = 0;
static double autoMeasureSeconds = 0;
static bool autoAwaitingState = false;
static bool autoProfileAfterLoad = false;
static void profStart();
static unsigned autoMeasuredFrames = 0;
static vector<float> autoFrameGaps;
static vector<float> autoGameIntervals;
static double autoPrevSwap = 0;
static constexpr double AUTO_LOW_WARMUP_MS = 3000;
static bool autoLowWindowStarted = false, autoSkipFirstGameInterval = false;
static vector<std::pair<string, string>> autoOptions;  // extra core options for the current test run
static string autoLabel;
static std::deque<std::pair<double, int>> autoLiveRes;
struct AutoLiveSet { double at; string what; int v; };
static std::deque<AutoLiveSet> autoLiveSet;  // "liveSet": [[seconds, "rate"|"skip"|"readback"|"clock", menu index], ...] (in memory only)  // "liveRes": [[seconds, resolution index], ...]: change resolution mid-game, as from the menu
static bool autoPlayerStats = false;  // "playerStats": true: the stats text and log players get (to check them)
// Detailed logging (every second, thread detail, the core's output in full): Settings -> App ->
// Debug logging (off by default); always on in test runs, unless they ask for what players get.
static bool debugLog = false;
static bool verboseLog() { return autoTest ? !autoPlayerStats : debugLog; }
static double autoSaveAt = -1; static int autoSaveSlot = 4;  // "saveState": [seconds, slot]: save a state (tests the states folder)
static double autoMenuOpenAt = -1;  // "menuOpen": seconds: open the in-game menu and leave it open (for screenshots)
static double autoEarlyLoadAt = -1; static int autoEarlyLoadSlot = 1;  // "earlyLoad": [seconds, slot]: Load state straight after boot, no menu
static double autoMenuLoadAt = -1; static int autoMenuLoadSlot = 1, autoMenuLoadStage = 0;  // "menuLoad": [seconds, slot]: open the menu, then Load state from it
static vector<double> autoShots;  // seconds into the run at which to take screenshots ("shots")
static int autoGameShotTarget = 0, autoGameShotFrames = 0;
static bool autoGameShotPriming = false;
static double autoRunStart = 0;  // shown at the bottom of the screen during a test run
static int coreLogLines = 0;
static void record(const char* fmt, ...) {
  if (!logFile) return;
  va_list ap; va_start(ap, fmt); vfprintf(logFile, fmt, ap); va_end(ap); fputc('\n', logFile); fflush(logFile);
}
void sw2_log_line(const char* line) { record("%s", line); }
// The log file; the previous session's is kept as log.prev.txt (a test's results survive a relaunch).
static void openLog(bool append) {
  if (logFile) return;
  if (!append) {
    remove((string(DATA_DIR) + "/log.prev.txt").c_str());
    rename((string(DATA_DIR) + "/log.txt").c_str(), (string(DATA_DIR) + "/log.prev.txt").c_str());
  }
  logFile = fopen((string(DATA_DIR) + "/log.txt").c_str(), append ? "a" : "w");
}  // for other files (vkpresent.cpp)

/* ---------------- Files ---------------- */
static bool readFile(const string& path, vector<uint8_t>& out, long limit = 4 * 1024 * 1024) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  if (n < 0 || n > limit) { fclose(f); return false; }
  out.resize(n);
  bool ok = n == 0 || fread(out.data(), 1, n, f) == (size_t)n;
  fclose(f);
  return ok;
}
static bool readText(const string& path, string& out) {
  vector<uint8_t> d;
  if (!readFile(path, d, 1024 * 1024)) return false;
  out.assign(d.begin(), d.end());
  return true;
}
// Writes via a temporary file and rename, so a crash never leaves half a save.
static bool writeFile(const string& path, const void* data, size_t n) {
  string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(data, 1, n, f) == n;
  ok = fclose(f) == 0 && ok;
  if (!ok) { remove(tmp.c_str()); return false; }
  remove(path.c_str());
  return rename(tmp.c_str(), path.c_str()) == 0;
}
static bool writeText(const string& path, const string& text) { return writeFile(path, text.data(), text.size()); }
static bool exists(const string& path) { struct stat st; return stat(path.c_str(), &st) == 0; }

static string lower(string s) { for (auto& c : s) c = tolower((unsigned char)c); return s; }
static bool hasExt(const string& n, const char* ext) { size_t l = strlen(ext); return n.size() > l && lower(n.substr(n.size() - l)) == ext; }
// Disc images the core opens (libretro/main.c valid_extensions); a .cue names its .bin, which is not listed itself.
static bool isRomExt(const string& n) {
  for (const char* e : {".iso", ".chd", ".cso", ".zso", ".cue", ".gz", ".ciso", ".m3u"}) if (hasExt(n, e)) return true;
  return false;
}
static string stripNes(const string& n) { size_t d = n.rfind('.'); return isRomExt(n) && d != string::npos ? n.substr(0, d) : n; }
static string sanitize(const string& name) {
  string s = stripNes(name), out;
  for (char c : s) out += (isalnum((unsigned char)c) || c == '.' || c == '_' || c == ' ' || c == '-') ? c : '_';
  if (out.size() > 80) out.resize(80);
  return out.empty() ? "game" : out;
}
static bool contains(const vector<string>& v, const string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
static int indexOf(const vector<string>& v, const string& s) { auto it = std::find(v.begin(), v.end(), s); return it == v.end() ? -1 : (int)(it - v.begin()); }

/* ---------------- Settings ---------------- */
static string SETTINGS() { return string(DATA_DIR) + "/settings.json"; }
static GameVals gameValues() {
  GameVals g = {aspectMode, scaleMode, cropMode, crtStyle, paletteMode, {faceMap[0], faceMap[1], faceMap[2], faceMap[3], faceMap[4], faceMap[5]}, {faceMap2[0], faceMap2[1], faceMap2[2], faceMap2[3], faceMap2[4], faceMap2[5]}, turboRate, volumeLevel, audioFilter,
                cornerRadius, cornerSoftness, vignette, vignetteSize, curvature, shader, signalMode, boost};
  return g;
}
static int pick(const Json* x, int n, int d) { return x && x->isNum() && x->n >= 0 && x->n < n ? (int)x->n : d; }
static void setGameValues(const GameVals& g) {
  aspectMode = g.aspect; scaleMode = g.scale; cropMode = g.crop; crtStyle = g.crt; paletteMode = g.palette;
  for (int i = 0; i < NFACE; i++) { faceMap[i] = g.faces[i]; faceMap2[i] = g.faces2p[i]; }
  turboRate = g.turbo; volumeLevel = g.volume; audioFilter = g.filter;
  cornerRadius = g.corner; cornerSoftness = g.cornerSoft; vignette = g.vignette; vignetteSize = g.vignetteSize; curvature = g.curve; shader = g.shader; signalMode = g.signal; boost = g.boost;
}
static void setGameValuesJson(const Json& v) {
  aspectMode = pick(v.get("aspect"), ASPECTS.size(), aspectMode); scaleMode = pick(v.get("scale"), SCALES.size(), scaleMode); cropMode = pick(v.get("crop"), CROPS.size(), cropMode);
  const Json* c = v.get("crt");
  (void)c;  // the CRT strip styles were replaced by shaders (2.5.0): old values are ignored
  turboRate = pick(v.get("turbo"), TURBO_RATES.size(), turboRate);
  volumeLevel = pick(v.get("volume"), VOLUMES.size(), volumeLevel); audioFilter = pick(v.get("filter"), FILTERS.size(), audioFilter);
  cornerRadius = pick(v.get("corner"), CORNER_RADII.size(), cornerRadius); cornerSoftness = pick(v.get("cornerSoft"), CORNER_SOFTNESS.size(), cornerSoftness);
  vignette = pick(v.get("vignette"), VIGNETTES.size(), vignette); vignetteSize = pick(v.get("vignetteSize"), VIGNETTE_SIZES.size(), vignetteSize);
  curvature = pick(v.get("curvature"), CURVATURES.size(), curvature);
  shader = pick(v.get("shader"), SHADERS.size(), shader);
  signalMode = pick(v.get("signal"), SIGNALS.size(), signalMode);
  boost = pick(v.get("boost"), BOOSTS.size(), boost);
  const Json* f = v.get("faces");
  if (f && f->isArr() && f->a.size() == NFACE) for (int i = 0; i < NFACE; i++) faceMap[i] = pick(&f->a[i], FACE_CHOICES.size(), FACE_DEFAULT[i]);
  const Json* f2 = v.get("facesP2");
  if (f2 && f2->isArr() && f2->a.size() == NFACE) for (int i = 0; i < NFACE; i++) faceMap2[i] = pick(&f2->a[i], FACE_CHOICES.size(), FACE_DEFAULT[i]);
}
static Json gameJson(const GameVals& g) {
  Json j = Json::object();
  j.set("aspect", Json::number(g.aspect)); j.set("scale", Json::number(g.scale)); j.set("crop", Json::number(g.crop));
  j.set("crt", Json::number(g.crt)); j.set("palette", Json::number(g.palette));
  Json f = Json::array(); for (int i = 0; i < NFACE; i++) f.a.push_back(Json::number(g.faces[i]));
  j.set("faces", f);
  Json f2 = Json::array(); for (int i = 0; i < NFACE; i++) f2.a.push_back(Json::number(g.faces2p[i]));
  j.set("facesP2", f2); j.set("turbo", Json::number(g.turbo)); j.set("volume", Json::number(g.volume)); j.set("filter", Json::number(g.filter));
  j.set("corner", Json::number(g.corner)); j.set("cornerSoft", Json::number(g.cornerSoft)); j.set("vignette", Json::number(g.vignette)); j.set("vignetteSize", Json::number(g.vignetteSize)); j.set("curvature", Json::number(g.curve)); j.set("shader", Json::number(g.shader)); j.set("signal", Json::number(g.signal)); j.set("boost", Json::number(g.boost));
  return j;
}
static Json strings(const vector<string>& v) { Json a = Json::array(); for (auto& s : v) a.a.push_back(Json::string(s)); return a; }

static Json settingsJson(const GameVals& g) {
  Json j = Json::object();
  j.set("v", Json::number(8)); j.set("crt", Json::number(g.crt)); j.set("stats", Json::boolean(statsLevel > 0)); j.set("statsLevel", Json::number(statsLevel));
  j.set("aspect", Json::number(g.aspect)); j.set("scale", Json::number(g.scale)); j.set("crop", Json::number(g.crop));
  Json f = Json::array(); for (int i = 0; i < NFACE; i++) f.a.push_back(Json::number(g.faces[i]));
  j.set("faces2", f);
  Json f2 = Json::array(); for (int i = 0; i < NFACE; i++) f2.a.push_back(Json::number(g.faces2p[i]));
  j.set("facesP2", f2); j.set("turbo", Json::number(g.turbo)); j.set("palette", Json::number(g.palette));
  j.set("volume", Json::number(g.volume)); j.set("filter", Json::number(g.filter));
  j.set("corner", Json::number(g.corner)); j.set("cornerSoft", Json::number(g.cornerSoft)); j.set("vignette", Json::number(g.vignette)); j.set("vignetteSize", Json::number(g.vignetteSize)); j.set("curvature", Json::number(g.curve)); j.set("shader", Json::number(g.shader)); j.set("signal", Json::number(g.signal)); j.set("boost", Json::number(g.boost));
  Json sorts = Json::array();
  for (int sort : romSortByView) sorts.a.push_back(Json::number(sort));
  j.set("romSortByView", sorts); j.set("playHistory", strings(playHistory));
  j.set("lastPlayedDates", lastPlayedDates);
  j.set("recent", strings(recentGames)); j.set("favourites", strings(favourites)); j.set("widescreen", strings(widescreenGames));
  j.set("romDir", Json::string(romDir)); j.set("biosDir", Json::string(biosDir));
  j.set("advanced", Json::boolean(showAdvanced));
  j.set("runAhead", Json::number(runAhead)); j.set("frameDelay", Json::number(frameDelay)); j.set("audioBuffer", Json::number(audioBuffer));
  j.set("display", Json::number(displayMode)); j.set("rewind", Json::boolean(rewindOn)); j.set("ffMode", Json::number(ffMode)); j.set("ffSpeed", Json::number(ffSpeed));
  j.set("ps2Renderer", Json::number(ps2Renderer)); j.set("ps2Resolution", Json::number(ps2Resolution)); j.set("ps2EeSpeed", Json::number(ps2EeSpeed)); j.set("ps2Fpu", Json::number(ps2Fpu)); j.set("ps2CycleSkip", Json::number(ps2CycleSkip));
  { Json rb = Json::object(); for (auto& kv : gameReadback) if (kv.second) rb.set(kv.first, Json::number(kv.second)); j.set("gameReadback", rb); }
  { Json b = Json::object(); for (auto& kv : gameBlending) if (kv.second) b.set(kv.first, Json::number(kv.second)); j.set("gameBlending", b); }
  { Json r = Json::object(), k = Json::object(); for (auto& kv : gameRate) if (kv.second) r.set(kv.first, Json::number(kv.second)); for (auto& kv : gameSkip) if (kv.second) k.set(kv.first, Json::number(kv.second)); j.set("gameRate", r); j.set("gameSkip", k);
    Json rs = Json::object(); for (auto& kv : gameRes) if (kv.second) rs.set(kv.first, Json::number(kv.second)); j.set("gameRes", rs); j.set("res1x", Json::boolean(true)); j.set("debugLog", Json::boolean(debugLog));
    for (int pl = 0; pl < 4; pl++) { Json m = Json::array(); for (int i = 0; i < 16; i++) m.a.push_back(Json::number(padMap[pl][i])); j.set("padMap" + std::to_string(pl + 1), m); }
    j.set("multitap", Json::boolean(multitapDefault));
    { Json g = Json::object(); for (auto& kv : gameMultitap) g.set(kv.first, Json::boolean(kv.second)); j.set("gameMultitap", g); }
    { Json g = Json::object();
      for (auto& kv : gamePadMaps) { Json both = Json::array(); for (int pl = 0; pl < 4; pl++) { Json m = Json::array(); for (int i = 0; i < 16; i++) m.a.push_back(Json::number(kv.second.m[pl][i])); both.a.push_back(m); } g.set(kv.first, both); }
      j.set("gamePadMaps", g); } j.set("ee75", Json::boolean(true)); j.set("zfastDefault", Json::boolean(true)); } j.set("fastReadback", strings(fastReadbackGames)); j.set("cpuClock", Json::number(cpuClock)); j.set("gpuClock", Json::number(gpuClock));
  j.set("autoLoad", Json::boolean(autoLoadState)); j.set("pauseMenu", Json::boolean(pauseInMenu)); j.set("vibration", Json::boolean(vibLevel != 0)); j.set("vibLevel", Json::number(vibLevel)); j.set("dimMenu", Json::boolean(dimInMenu));
  return j;
}
static void saveSettings() {
  if (autoTest) return;  // a test run leaves the settings file alone
  GameVals g = running && running->perGame ? running->globalVals : gameValues();
  if (running && running->perGame && !writeText(running->settingsBase + ".settings.json", gameJson(gameValues()).dump())) record("Game settings save failed");
  if (!writeText(SETTINGS(), settingsJson(g).dump())) record("Settings save failed");
}
static void applySettingsJson(const Json& v) {
  Json gv = Json::object();
  if (const Json* f = v.get("faces2")) gv.set("faces", *f);
  if (const Json* f = v.get("facesP2")) gv.set("facesP2", *f);
  for (const char* k : {"turbo", "palette", "volume", "filter", "aspect", "scale", "crop", "corner", "cornerSoft", "vignette", "vignetteSize", "curvature", "shader", "signal", "boost"}) if (const Json* x = v.get(k)) gv.set(k, *x);
  setGameValuesJson(gv);
  if (const Json* r = v.get("recent")) if (r->isArr()) { recentGames.clear(); for (auto& x : r->a) if (x.isStr() && (int)recentGames.size() < RECENT_MAX) recentGames.push_back(x.s); }
  romSortByView = DEFAULT_ROM_SORT_BY_VIEW;
  const int legacySort = pick(v.get("romSort"), ROM_SORT_LABELS.size(), -1);
  if (legacySort >= 0) std::fill(romSortByView.begin(), romSortByView.end(), legacySort);
  if (const Json* sorts = v.get("romSortByView"); sorts && sorts->isArr()) {
    for (size_t i = 0; i < std::min(sorts->a.size(), romSortByView.size()); i++)
      romSortByView[i] = pick(&sorts->a[i], ROM_SORT_LABELS.size(), romSortByView[i]);
  }
  playHistory = recentGames;  // seed from the existing Recent tab on upgrade
  if (const Json* h = v.get("playHistory"); h && h->isArr()) {
    playHistory.clear();
    for (const auto& entry : h->a) if (entry.isStr()) playHistory.push_back(entry.s);
  }
  if (const Json* r = v.get("favourites")) if (r->isArr()) { favourites.clear(); for (auto& x : r->a) if (x.isStr()) favourites.push_back(x.s); }
  if (const Json* r = v.get("widescreen")) if (r->isArr()) { widescreenGames.clear(); for (auto& x : r->a) if (x.isStr()) widescreenGames.push_back(x.s); }
  if (const Json* b = v.get("autoLoad")) if (b->isBool()) autoLoadState = b->b;
  if (const Json* b = v.get("pauseMenu")) if (b->isBool()) pauseInMenu = b->b;
  if (const Json* b = v.get("vibration")) if (b->isBool()) vibration = b->b;
  vibLevel = v.get("vibLevel") ? pick(v.get("vibLevel"), VIB_LEVELS.size(), 2) : (vibration ? 2 : 0);
  if (const Json* b = v.get("dimMenu")) if (b->isBool()) dimInMenu = b->b;
  statsLevel = pick(v.get("statsLevel"), STATS_LEVELS.size(), statsLevel);
  if (const Json* b = v.get("advanced")) if (b->isBool()) showAdvanced = b->b;
  if (const Json* r = v.get("romDir")) if (r->isStr() && !r->s.empty()) romDir = r->s;
  if (const Json* r = v.get("biosDir")) if (r->isStr() && !r->s.empty()) biosDir = r->s;
  runAhead = pick(v.get("runAhead"), RUN_AHEADS.size(), runAhead); frameDelay = pick(v.get("frameDelay"), FRAME_DELAYS.size(), frameDelay);
  audioBuffer = pick(v.get("audioBuffer"), AUDIO_BUFFERS.size(), audioBuffer); displayMode = pick(v.get("display"), DISPLAY_MODES.size(), displayMode);
  ffMode = pick(v.get("ffMode"), FF_MODES.size(), ffMode); ffSpeed = pick(v.get("ffSpeed"), FF_SPEEDS.size(), ffSpeed);
  ps2Renderer = 0;  // always the GPU (Vulkan) renderer
  ps2Resolution = pick(v.get("ps2Resolution"), RESOLUTIONS.size(), ps2Resolution);
  ps2EeSpeed = pick(v.get("ps2EeSpeed"), EE_SPEEDS.size(), ps2EeSpeed);
  if (!v.get("ee75") && ps2EeSpeed == 0) ps2EeSpeed = 1;  // 75% became the default (1.0.2); once, for settings saved on the old 100% default
  ps2Fpu = pick(v.get("ps2Fpu"), FPU_MODES.size(), ps2Fpu);
  ps2CycleSkip = pick(v.get("ps2CycleSkip"), CYCLE_SKIPS.size(), ps2CycleSkip);

  if (const Json* r = v.get("gameRate")) if (r->type == Json::OBJ) { gameRate.clear(); for (auto& kv : r->o) if (kv.second.isNum()) gameRate[kv.first] = pick(&kv.second, EE_SPEEDS.size() + 1, 0); }
  if (const Json* r = v.get("gameReadback")) if (r->type == Json::OBJ) { gameReadback.clear(); for (auto& kv : r->o) if (kv.second.isNum()) gameReadback[kv.first] = pick(&kv.second, 3, 0); }
  if (const Json* r = v.get("gameBlending")) if (r->type == Json::OBJ) { gameBlending.clear(); for (auto& kv : r->o) if (kv.second.isNum()) gameBlending[kv.first] = pick(&kv.second, BLENDING_LEVELS.size() + 1, 0); }
  if (const Json* r = v.get("gameRes")) if (r->type == Json::OBJ) { gameRes.clear(); for (auto& kv : r->o) if (kv.second.isNum()) gameRes[kv.first] = pick(&kv.second, RESOLUTIONS.size() + 1, 0); }
  if (!v.get("zfastDefault") && shader == 2) shader = 6;  // zfast CRT became the default shader (2026-11-19); once, for settings saved on the old default
  if (const Json* d = v.get("debugLog")) if (d->isBool()) debugLog = d->b;
  if (const Json* b = v.get("multitap")) if (b->isBool()) multitapDefault = b->b;
  gameMultitap.clear();
  if (const Json* g = v.get("gameMultitap")) if (g->type == Json::OBJ) for (auto& kv : g->o) if (kv.second.isBool()) gameMultitap[kv.first] = kv.second.b;
  for (int pl = 0; pl < 4; pl++) {
    resetPadMap(pl);
    if (const Json* m = v.get("padMap" + std::to_string(pl + 1))) if (m->isArr() && m->a.size() == 16)
      for (int i = 0; i < 16; i++) if (m->a[i].isNum()) { const int b = (int)m->a[i].n; padMap[pl][i] = (b >= 0 && b < 28 && (MAPPABLE >> b & 1)) ? b : -1; }
  }
  gamePadMaps.clear();
  if (const Json* g = v.get("gamePadMaps")) if (g->type == Json::OBJ)
    for (auto& kv : g->o) if (kv.second.isArr() && (kv.second.a.size() == 2 || kv.second.a.size() == 4)) {
      GamePadMap gm; bool ok = true;
      for (int pl = 2; pl < 4; pl++) memcpy(gm.m[pl], padMap[pl], sizeof gm.m[pl]);  // (older saves: players 1-2 only)
      for (int pl = 0; pl < (int)kv.second.a.size() && ok; pl++) {
        const Json& m = kv.second.a[pl]; if (!m.isArr() || m.a.size() != 16) { ok = false; break; }
        for (int i = 0; i < 16; i++) { const int b = m.a[i].isNum() ? (int)m.a[i].n : -1; gm.m[pl][i] = (b >= 0 && b < 28 && (MAPPABLE >> b & 1)) ? b : -1; }
      }
      if (ok) gamePadMaps[kv.first] = gm;
    }
  if (!v.get("res1x") && ps2Resolution == 1) ps2Resolution = 0;  // 1x is the default again (1.0.2); once, for settings saved on 1.0.0-1.0.1's 2x default
  if (const Json* r = v.get("gameSkip")) if (r->type == Json::OBJ) { gameSkip.clear(); for (auto& kv : r->o) if (kv.second.isNum()) gameSkip[kv.first] = pick(&kv.second, CYCLE_SKIPS.size() + 1, 0); }
  if (const Json* r = v.get("fastReadback")) if (r->isArr()) { fastReadbackGames.clear(); for (auto& x : r->a) if (x.isStr()) fastReadbackGames.push_back(x.s); }
  {  // settings before v8 had boost on/off: those start on the new default (Maximum)
    const Json* ver = v.get("v");
    if (ver && ver->isNum() && ver->n >= 8) cpuClock = pick(v.get("cpuClock"), CPU_CLOCKS.size(), cpuClock);
    gpuClock = 0;  // the GPU stays at the system rate: Maximum (1389 MHz) gained nothing measured, and the GPU is not the limit
  }
  runAhead = 0; ffMode = 0; rewindOn = false;  // not on this core
  // ("crt": the CRT strip styles were replaced by shaders in 2.5.0; old values are ignored.)
}
static void orderRoms();
static Json defaultSettings;  // the built-in defaults, captured before the settings file is read
static bool displaySaved = false;  // the file chose a display mode (otherwise 120 Hz only where available)
static void loadSettings() {
  for (int pl = 0; pl < 4; pl++) resetPadMap(pl);  // the standard layout, before the defaults are captured
  defaultSettings = settingsJson(gameValues());
  string t; Json v;
  if (!readText(SETTINGS(), t) || !Json::parse(t, v) || v.type != Json::OBJ) return;
  applySettingsJson(v);
  if (const Json* dates = v.get("lastPlayedDates"); dates && dates->type == Json::OBJ)
    lastPlayedDates = *dates;
  displaySaved = v.get("display") != nullptr;
  if (const Json* d = v.get("frameDelay")) if (d->isNum() && d->n >= FRAME_DELAYS.size()) frameDelay = (int)FRAME_DELAYS.size() - 1;  // 12 ms -> 8 ms cap
}
// Settings -> Reset all settings: back to the built-in defaults (recent games,
// favourites and per-game settings files are kept).
static void resetAllSettings() {
  vector<string> rec = recentGames, fav = favourites, ws = widescreenGames, fr = fastReadbackGames, history = playHistory;
  applySettingsJson(defaultSettings);
  recentGames = rec; favourites = fav; widescreenGames = ws; fastReadbackGames = fr; playHistory = history;
  orderRoms();
  saveSettings();
}

/* ---------------- ROM list ---------------- */
static bool naturalLess(const string& a0, const string& b0) {
  string a = lower(a0), b = lower(b0);
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[j])) {
      size_t i2 = i, j2 = j;
      while (i2 < a.size() && isdigit((unsigned char)a[i2])) i2++;
      while (j2 < b.size() && isdigit((unsigned char)b[j2])) j2++;
      double x = atof(a.substr(i, i2 - i).c_str()), y = atof(b.substr(j, j2 - j).c_str());
      if (x != y) return x < y;
      i = i2; j = j2; continue;
    }
    if (a[i] != b[j]) return a[i] < b[j];
    i++; j++;
  }
  return a.size() - i < b.size() - j;
}
static void orderRoms() {
  const int romSort = romSortByView[listView];
  string cur = selected < (int)roms.size() ? roms[selected].name : "";
  vector<Rom> list;
  if (listView == 1) {
    for (auto& r : allRoms) if (contains(favourites, r.name)) list.push_back(r);
    std::sort(list.begin(), list.end(), [](const Rom& a, const Rom& b) { return naturalLess(a.name, b.name); });
  } else if (listView == 2) {
    for (auto& n : recentGames) for (auto& r : allRoms) if (r.name == n) { list.push_back(r); break; }
  } else {
    list = allRoms;
    auto rank = [](const Rom& r) -> std::pair<int, int> {
      if (contains(favourites, r.name)) return {0, 0};
      int k = indexOf(recentGames, r.name);
      return k >= 0 ? std::make_pair(1, k) : std::make_pair(2, 0);
    };
    std::stable_sort(list.begin(), list.end(), [&](const Rom& a, const Rom& b) {
      auto x = rank(a), y = rank(b);
      if (x != y) return x < y;
      return naturalLess(a.name, b.name);
    });
  }
  if (romSort != DefaultOrder) {
    std::map<string, size_t> playedRank;
    if (romSort == PlayedAsc || romSort == PlayedDesc) {
      for (size_t i = 0; i < playHistory.size(); i++) playedRank.emplace(playHistory[i], i);
    }
    const auto rank = [&](const Rom& r) {
      auto it = playedRank.find(r.name);
      return it == playedRank.end() ? playHistory.size() : it->second;
    };
    std::stable_sort(list.begin(), list.end(), [&](const Rom& a, const Rom& b) {
      if (romSort == PlayedAsc || romSort == PlayedDesc) {
        const size_t x = rank(a), y = rank(b);
        if ((x == playHistory.size()) != (y == playHistory.size())) return x < y;
        if (x != y) return romSort == PlayedDesc ? x < y : x > y;
      } else if (romSort == AddedAsc || romSort == AddedDesc) {
        if ((a.added == 0) != (b.added == 0)) return a.added != 0;
        if (a.added != b.added) return romSort == AddedAsc ? a.added < b.added : a.added > b.added;
      }
      const string an = stripNes(a.name), bn = stripNes(b.name);
      return romSort == NameDesc ? naturalLess(bn, an) : naturalLess(an, bn);
    });
  }
  roms = list; selected = 0; top = 0;
  if (!cur.empty()) for (size_t i = 0; i < roms.size(); i++) if (roms[i].name == cur) selected = (int)i;
}
// Time played per game (seconds, by file name) in playtime.json: counted while
// a game runs (also behind an unpaused menu), saved every 5 minutes, when a
// game is quit and when SW2PS2 exits.
static Json playtime = Json::object();
static double playAccumSec = 0, playSavedAt = 0;
static string PLAYTIME() { return string(DATA_DIR) + "/playtime.json"; }
static void loadPlaytime() {
  string t; Json j;
  if (readText(PLAYTIME(), t) && Json::parse(t, j) && j.type == Json::OBJ) playtime = j;
}
static double playSeconds(const string& name) { const Json* v = playtime.get(name); return v && v->isNum() ? v->n : 0; }
static string dateFormat = "%Y-%m-%d";
static std::map<string, string> lastPlayedLabels;
static void loadDateFormat() {
  u64 code = 0;
  if (R_FAILED(setInitialize())) return;
  const Result rc = setGetSystemLanguage(&code);
  setExit();
  if (R_FAILED(rc)) return;
  char language[sizeof code + 1]{};
  memcpy(language, &code, sizeof code);
  string patterns;
  if (!readText("romfs:/date-patterns.tsv", patterns)) return;
  patterns.insert(0, 1, '\n');
  for (string locale = language; !locale.empty();) {
    const string key = "\n" + locale + "\t";
    const size_t match = patterns.find(key);
    if (match != string::npos) {
      const size_t start = match + key.size();
      dateFormat = patterns.substr(start, patterns.find('\n', start) - start);
      return;
    }
    const size_t parent = locale.rfind('-');
    if (parent == string::npos) break;
    locale.resize(parent);
  }
}
static string formatLastPlayedDate(const Json* saved) {
  if (!saved) return "";
  struct tm date{};
  if (saved->isNum() && saved->n > 0 && saved->n < 253402300800.0) {
    TimeCalendarTime local{};
    TimeCalendarAdditionalInfo info{};
    if (R_SUCCEEDED(timeToCalendarTimeWithMyRule((u64)saved->n, &local, &info))) {
      date.tm_year = local.year - 1900;
      date.tm_mon = local.month - 1;
      date.tm_mday = local.day;
    } else {
      const time_t when = (time_t)saved->n;
      if (!gmtime_r(&when, &date)) return "";
    }
  } else if (saved->isStr() && saved->s.size() == 8) {
    int year, month, day;
    if (sscanf(saved->s.c_str(), "%2d/%2d/%2d", &year, &month, &day) != 3 ||
        year < 0 || month < 1 || month > 12 || day < 1 || day > 31) return "";
    date.tm_year = year + 100;  // the old UTC date stores only the year within this century
    date.tm_mon = month - 1;
    date.tm_mday = day;
  } else {
    return "";
  }
  char label[64];
  return strftime(label, sizeof label, dateFormat.c_str(), &date) ? label : "";
}
static const string& lastPlayedLabel(const string& name) {
  auto [it, inserted] = lastPlayedLabels.try_emplace(name);
  if (inserted) it->second = formatLastPlayedDate(lastPlayedDates.get(name));
  return it->second;
}
static string playLabel(const string& name) {
  double sec = playSeconds(name);
  if (sec <= 0) return "";
  if (sec < 3600) return "< 1 hour";
  int h = (int)(sec / 3600);
  return h == 1 ? "1 hour" : std::to_string(h) + " hours";
}
static void flushPlaytime(const string& name) {
  if (playAccumSec <= 0 || name.empty()) return;
  playtime.set(name, Json::number(round(playSeconds(name) + playAccumSec)));
  playAccumSec = 0;
  if (!writeText(PLAYTIME(), playtime.dump())) record("Play time save failed");
}
static u64 romAddedTime(const string& path) {
  FsFileSystem* fs = nullptr;
  char translated[FS_MAX_PATH];
  FsTimeStampRaw stamp{};
  if (fsdevTranslatePath(path.c_str(), &fs, translated) != 0 ||
      R_FAILED(fsFsGetFileTimeStampRaw(fs, translated, &stamp)) || !stamp.is_valid) return 0;
  return stamp.created;
}
// Games in dir and its subfolders (hidden folders skipped; at most 6 levels; from the SD card's
// root the app and system folders are left out).
static void scanGames(const string& dir, int depth, vector<Rom>& found) {
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  vector<string> subdirs;
  while (struct dirent* e = readdir(d)) {
    string n = e->d_name;
    if (n.empty() || n[0] == '.') continue;
    bool isDir = e->d_type == DT_DIR;
    if (e->d_type == DT_UNKNOWN) { struct stat st; isDir = stat((dir + "/" + n).c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
    if (isDir) subdirs.push_back(n);
    else if (isRomExt(n)) found.push_back(Rom{n, dir, romAddedTime(dir + "/" + n)});
  }
  closedir(d);
  if (depth >= 6) return;
  const bool root = dir == "sdmc:" || dir == "sdmc:/";
  std::sort(subdirs.begin(), subdirs.end(), [](const string& a, const string& b) { return naturalLess(a, b); });
  for (const string& n : subdirs) {
    if (root && (lower(n) == "switch" || lower(n) == "atmosphere")) continue;
    scanGames((root ? string("sdmc:") : dir) + "/" + n, depth + 1, found);
  }
}
static void scan() {
  allRoms.clear();
  // One entry per game name (the first found wins over a same-named file deeper in the tree).
  for (const string& dir : {romDir}) {
    vector<Rom> found;
    scanGames(dir, 0, found);
    std::sort(found.begin(), found.end(), [](const Rom& a, const Rom& b) { return isRomExt(a.name) && !isRomExt(b.name); });
    for (auto& r : found) {
      bool dup = false;
      for (auto& o : allRoms) if (lower(stripNes(o.name)) == lower(stripNes(r.name))) dup = true;
      if (!dup) allRoms.push_back(r);
    }
  }
  orderRoms();
  if (selected >= (int)roms.size()) selected = std::max(0, (int)roms.size() - 1);
}

static void applyEmuOptions() {
  if (!running) return;
  C->set_audio((int)lround(volumeLevel * 25.6), audioFilter);
}

/* ---------------- Picture geometry and CRT ---------------- */
static gfx::Picture* picture = nullptr;
// Current frame size (256 or 512 wide, 224/239 or 448/478 tall); geometry
// works in "logical" SNES pixels (256 x 224/239), the texture in frame pixels.
static int W = 640, H = 448;
static int logW() { return std::max(1, W / renderScale); }
static int logH() { return std::max(1, H / renderScale); }
static int picX = 0, picY = 0, picW = 0, picH = 0, picCX = 0, picCY = 0, picCW = 0, picCH = 0;
static double picSX = 0, picSY = 0;
static string crtKey;
// Port of fitPicture(): Integer = whole-number pixel sizes closest to the
// aspect; Fit = largest exact aspect; crop offsets the image.
static void fitPicture() {
  int vw = gfx::SCREEN_W, vh = gfx::SCREEN_H;
  int lw = logW(), lh = logH(), kx = W / lw, ky = H / lh;
  int cx = cropMode >= 1 ? 8 : 0, cy = cropMode >= 2 ? 8 : 0, cw = lw - 2 * cx, ch = lh - 2 * cy;
  const bool wide = running && !autoFixedSettings && contains(widescreenGames, running->name) && aspectMode == 2;  // 4:3 becomes 16:9 for a widescreen game
  double par = (aspectMode == 1 || wide) ? (16.0 / 9.0) * ch / cw : aspectMode == 2 ? (4.0 / 3.0) * ch / cw : 1, sx, sy;
  if (scaleMode == 0 && vh >= ch && vw >= cw) {
    int maxX = vw / cw, maxY = vh / ch;
    if (aspectMode == 3) { sx = maxX; sy = maxY; }
    else { sy = maxY; sx = std::max(1, std::min(maxX, (int)lround(sy * par))); if (sx * cw > vw) sx = maxX; }
  } else if (aspectMode == 3) { sx = (double)vw / cw; sy = (double)vh / ch; }
  else {
    double fit = std::min(vw / (cw * par), (double)vh / ch); sy = fit; sx = fit * par;
    // Just above a whole scale (a PAL 512-line frame on a 1080-line screen is 2.11x), one game line
    // in every few gets an extra screen row, and scanline shaders turn that into slow horizontal
    // bands (Simpsons on Vulkan: a ripple every 19.5 rows). Snap down to the whole scale there; the
    // aspect stays. Scales well between whole numbers (2.41) alternate too finely to show.
    if (aspectMode != 3 && sy >= 2 && sy - floor(sy) < 0.3) { double k = floor(sy) / sy; sy *= k; sx *= k; }
  }
  int w = (int)lround(cw * sx), h = (int)lround(ch * sy);
  picX = (vw - w) / 2; picY = (vh - h) / 2; picW = w; picH = h; picSX = sx; picSY = sy;
  picCX = cx * kx; picCY = cy * ky; picCW = cw * kx; picCH = ch * ky;  // source rectangle in frame pixels
}
static double emuFrames = 0;
// Shows the core's latest frame; refits the picture when the SNES changes
// resolution (e.g. between 256 and 512 wide modes).
static void applyCrt();
static void showFrame() {
  if (!picture) return;
  int w = C->width(), h = C->height();
  if (w <= 0 || h <= 0 || w > 2560 || h > 2048) return;  // nothing shown yet
  if (w != W || h != H) { record("frame size %dx%d (was %dx%d)", w, h, W, H); W = w; H = h; fitPicture(); crtKey.clear(); applyCrt(); }
#ifdef SW2_VK
  if (ps2core_vulkan()) {  // the core's Vulkan frame, copied to host memory (vkpresent)
    int vw = 0, vh = 0;
    if (const uint32_t* px = vkp::grab(&vw, &vh)) {
      const bool measure = vkp::measuringHandoff();
      const uint64_t start = measure ? armGetSystemTick() : 0;
      gfx::pictureUpload(picture, px, vw, vh, vw);
      if (measure) vkp::recordUpload(armTicksToNs(armGetSystemTick() - start));
    }
    gfx::pictureSetFrameCount(picture, (float)emuFrames);
    return;
  }
#endif
  if (ps2core_is_gpu()) gfx::pictureFromCore(picture, W, H);
  else if (C->video()) gfx::pictureUpload(picture, C->video(), W, H, C->video_pitch());
  gfx::pictureSetFrameCount(picture, (float)emuFrames);
}
static void applyCrt() {
  if (!picture) return;
  gfx::pictureSetShader(picture, shader);
  gfx::pictureSetBoost(picture, BOOST_K[boost]);
  {
    float dec[4] = {0.0f, 1.0f, 0.0f, 1.0f};  // NTSC decode: no hue shift, normal saturation
    gfx::pictureSetSignal(picture, signalMode, dec, 1.0f);
  }
  gfx::pictureSetEffects(picture, picH * CORNER_RADIUS_FRAC[cornerRadius], std::max(1.0f, picH * CORNER_SOFT_FRAC[cornerSoftness]),
                         VIGNETTE_STRENGTH[vignette], VIGNETTE_START[vignetteSize], CURVATURE[curvature]);
  char key[200];
  snprintf(key, sizeof key, "%d,%d,%d,%.6f,%.6f,%d,%d", crtStyle, picW, picH, picSX, picSY, picCX, picCY);
  if (crtKey == key) return;
  crtKey = key;
  if (crtStyle <= 0) { gfx::pictureSetCrt(picture, nullptr, 0, nullptr, 0); return; }
  vector<uint8_t> rows, cols;
  crt::strips(crtStyle, picW, picH, picSX, picSY, picCX, picCY, rows, cols);
  gfx::pictureSetCrt(picture, rows.data(), picH, cols.data(), picW);
}
static void refreshPicture() { if (running && picture) { applyEmuOptions(); showFrame(); applyCrt(); } }
static string scaleLabel(int i) {
  if (i == 0 && scaleMode == 0 && picSX == (int)picSX && picSY == (int)picSY && picSX > 0) return SCALES[0] + " " + std::to_string((int)picSX) + "x" + std::to_string((int)picSY);
  return SCALES[i];
}

/* ---------------- Toasts and messages ---------------- */
static void showToast(const string& t, double ms = 1800) { toastText = t; toastUntil = nowMs() + ms; }
static void showMessage(const string& text, std::function<void()> then) { messageText = text; messageThen = then; mode = "message"; }

/* ---------------- Timing and stats ---------------- */
static double shownTimes[601];
static int shownHead = 0, shownCount = 0;
static void noteShown(double t) { shownTimes[shownHead] = t; shownHead = (shownHead + 1) % 601; if (shownCount < 601) shownCount++; }
static void videoFps(double& avg, double& low) {
  avg = low = 0;
  if (shownCount < 3) return;
  int n = shownCount, L = 601;
  double first = shownTimes[(shownHead - n + L) % L], last = shownTimes[(shownHead - 1 + L) % L];
  int k = std::max(1, (n - 1) / 100);
  vector<double> gaps;
  for (int i = 1; i < n; i++) gaps.push_back(shownTimes[(shownHead - n + i + L) % L] - shownTimes[(shownHead - n + i - 1 + L) % L]);
  std::partial_sort(gaps.begin(), gaps.begin() + k, gaps.end(), std::greater<double>());
  double worst = 0; for (int i = 0; i < k; i++) worst += gaps[i];
  avg = (n - 1) * 1000 / (last - first); low = 1000 / (worst / k);
}
// Frame-time graph: the time between the game's own frames (a 30 fps game sits at 33.3 ms, a stutter
// is a spike), 240 frames, 2 px each, scrolling left; guide lines at 16.7 and 33.3 ms, top 50 ms.
enum { GRAPH_STEP = 2, GRAPH_FRAMES = 240, GRAPH_W = GRAPH_STEP * GRAPH_FRAMES, GRAPH_H = 40 };
static const double GRAPH_MAX_MS = 50.0;
static bool graphGuide(int r) {
  for (double g : {16.7, 33.3}) if (r == GRAPH_H - 1 - (int)lround(g / GRAPH_MAX_MS * (GRAPH_H - 2))) return true;
  return false;
}
static uint32_t graphPix[GRAPH_W * GRAPH_H];
static gfx::Image* graphImage = nullptr;
static int graphPrevY = -1;
static bool graphSkipBacklog = true;  // the next read drops game frames from before the reset
static void resetGraph() {
  graphPrevY = -1; graphSkipBacklog = true;
  for (int r = 0; r < GRAPH_H; r++) for (int x = 0; x < GRAPH_W; x++) graphPix[r * GRAPH_W + x] = graphGuide(r) ? 0x4dffffff : 0;
}
extern "C" __attribute__((weak)) int sw2_game_frame_intervals(unsigned* seq, float* out_ms, int max);
static void drawGraph(double ms) {
  int S = GRAPH_STEP;
  int y = GRAPH_H - 1 - (int)lround(std::min(ms, GRAPH_MAX_MS) / GRAPH_MAX_MS * (GRAPH_H - 2));
  int y0 = graphPrevY < 0 ? y : graphPrevY, lo = std::min(y0, y), hi = std::max(y0, y);
  for (int r = 0; r < GRAPH_H; r++) {
    uint32_t* row = graphPix + r * GRAPH_W;
    memmove(row, row + S, (GRAPH_W - S) * 4);
    // a line: the first column joins the previous point, the second holds the new value
    const uint32_t bg = graphGuide(r) ? 0x4dffffff : 0;
    row[GRAPH_W - 2] = (r >= lo && r <= hi) ? 0xff6ee26e : bg;
    row[GRAPH_W - 1] = r == y ? 0xff6ee26e : bg;
  }
  graphPrevY = y;
  if (graphImage) gfx::imageUpload(graphImage, graphPix);
}

static double phase[5] = {0, 0, 0, 0, 0};  // per-second sums: fence wait, frame start (display wait), emulation, drawing, swap
static int phaseN = 0, sessionLog = 0;
static double swapAcc = 0, waitAcc = 0, swapMax = 0;
static int swapN = 0;
static double audioInCount = 0;  // audio frames the core produced (per-second stats)
static bool adaptiveAudio = true;
static double outputTempo = 1.0, minAudioQueue = 0;
static unsigned audioRefills = 0;
extern std::atomic<float> sw2_output_tempo __attribute__((weak));
extern int sw2_audio_stretch __attribute__((weak));
static double lastSwap = 0, tickMs = 1000.0 / 60, audioRatio = 1, pendEma = 2, statsAt = 0, workMs = 0, startedAt = 0;
static int emuCount = 0, ratioTicks = 0;
static string statsText;
static bool comboLatch = false, frameDrawn = false;

// Rewind history: a state every frame, delta-compressed (cores.cpp; ~1 KB per
// frame), within REWIND_BUDGET bytes; stepped back one per refresh.
static const long REWIND_BUDGET = 48L * 1024 * 1024;
static bool snapsReady = false;
static bool ffLatched = false, ffPrevHeld = false, rewinding = false, fastForward = false;
static double emuAcc = 0, ffAcc = 0, audioInteg = 0, loopGapMs = 16.69, loopStartPrev = 0, vrrNext = 0;
// The screen's refresh period when the loop is not paced by the game (menus):
// 120 Hz / VRR timing is used only when the screen really runs faster than 60 Hz.
static double freeTickMs = 0;
// VI2's report of the display (polled about once a second): the 120 Hz / VRR
// timing is used only while the display really is at 120 Hz or has VRR active.
static unsigned dispHz = 0;
static bool dispVrr = false, dispCan120 = false, dispKnown = false;
static double dispPolled = 0;
static void pollDisplay(double t) {
  if (dispPolled && t - dispPolled < 1000) return;
  dispPolled = t;
  unsigned hz; bool vrr, can;
  bool ok = gfx::displayInfo(hz, vrr, can);
  if (ok && (!dispKnown || hz != dispHz || vrr != dispVrr || can != dispCan120)) record("display: %u Hz, VRR %s, 120 Hz modes %s", hz, vrr ? "on" : "off", can ? "yes" : "no");
  if (ok) { dispHz = hz; dispVrr = vrr; dispCan120 = can; }
  dispKnown = ok;
}
static bool highRefresh() {
  if (displayMode != 1) return false;
  if (dispKnown) return dispHz >= 120 || dispVrr;
  return freeTickMs > 0 && freeTickMs < 12;  // fallback: measured refresh in menus
}
static void resetRewind() { rewind_clear(); }
static void setAudioRatio(double r) { if (r != audioRatio) { audioRatio = r; C->set_audio_ratio(r); } }
static void setOutputTempo(double tempo) {
  outputTempo = tempo;
  if (&sw2_output_tempo) sw2_output_tempo.store((float)tempo, std::memory_order_relaxed);
}
static void resetAudioMeasurements() {
  audioRefills = 0;
  minAudioQueue = AUDIO_TARGET_FRAMES[audioBuffer];
}

static void resetClock() { shownHead = shownCount = 0; resetGraph(); lastSwap = 0; }

/* ---------------- Battery saves and save states ---------------- */
static uint32_t hashSram() {
  uint32_t h = 2166136261u; uint8_t* p = C->sram(); int n = C->sram_size();
  for (int i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static void writeSram(uint32_t h) {
  if (writeFile(running->base + ".srm", C->sram(), C->sram_size())) running->sramHash = h;
  else { record("SRAM save failed"); showToast("Could not save game data"); }
}
static void saveSram(bool force) {
  if (!running || !running->battery) return;
  uint32_t h = hashSram();
  if (!force && h == running->sramHash) return;
  writeSram(h);
}
// Save states: <game>.state (slot 1), <game>.state2..4.
static string statePath(int n) { return running->stateBase + C->stateTag + (n == 1 ? ".state" : ".state" + std::to_string(n)); }
static string slotsPath() { return running->stateBase + C->stateTag + ".slots.json"; }
static double slotTime(int n) { const Json* t = running ? running->slots.get(std::to_string(n)) : nullptr; return t && t->isNum() ? t->n : 0; }
static string slotLabel(int n) {
  double t = slotTime(n);
  if (!t) return std::to_string(n) + "  (empty)";
  time_t s = (time_t)(t / 1000), nowT = time(nullptr);
  struct tm d = *localtime(&s), today = *localtime(&nowT);
  char buf[40];
  if (d.tm_year == today.tm_year && d.tm_yday == today.tm_yday) snprintf(buf, sizeof buf, "%02d:%02d", d.tm_hour, d.tm_min);
  else snprintf(buf, sizeof buf, "%02d/%02d %02d:%02d", d.tm_mday, d.tm_mon + 1, d.tm_hour, d.tm_min);
  return std::to_string(n) + "  " + buf;
}
static void saveState() {
  long len = C->state_save();
  if (!len) { showToast("State save failed: Emulator could not create a save state."); return; }
  if (!writeFile(statePath(stateSlot), C->state_buffer(len), len)) { showToast("State save failed"); return; }
  running->slots.set(std::to_string(stateSlot), Json::number((double)time(nullptr) * 1000));
  writeText(slotsPath(), running->slots.dump());
  showToast("Saved to slot " + std::to_string(stateSlot));
}
// ARMSX2 boots its virtual machine in the background after a game starts and turns a state
// down until that is done, so a refused load is retried each frame for up to 20 s.
static int pendingLoadSlot = 0, pendingLoadTries = 0;
extern "C" __attribute__((weak)) int sw2ax_game_booted();
static int bootedFrames = 0;  // frames since the game took over from the BIOS (see stateLoadReady)
// A state can load once the game has booted and settled: loaded during the BIOS boot, it makes the
// core switch to the game mid-load, which crashed God of War II.
static bool stateLoadReady() { return !sw2ax_game_booted || bootedFrames >= 30; }
static long pendingLoadSize = 0;
static void loadState(bool quietIfNone, int n) {
  vector<uint8_t> d;
  if (!readFile(statePath(n), d, 256L * 1024 * 1024)) { if (!quietIfNone) showToast("Slot " + std::to_string(n) + " is empty"); return; }
  char* buf = C->state_buffer((long)d.size());
  if (!buf) { showToast("State load failed"); return; }
  memcpy(buf, d.data(), d.size());
  pendingLoadSlot = 0;
  if (!stateLoadReady()) {  // still booting: held until the game is running (emulateShown)
    pendingLoadSlot = n; pendingLoadSize = (long)d.size(); pendingLoadTries = 0;
    if (!quietIfNone) showToast("Loading slot " + std::to_string(n) + " once the game has started...", 3000);
    return;
  }
  if (!C->state_load((long)d.size())) {
    if (!strcmp(C->name, "ARMSX2")) { pendingLoadSlot = n; pendingLoadSize = (long)d.size(); pendingLoadTries = 0; return; }
    showToast("State load failed"); return;
  }
  C->audio_reset();
  tuneQuietUntil = nowMs() + 10000; tuneWin.clear();
  showToast("Loaded slot " + std::to_string(n));
}
static int newestSlot() {
  int best = 0; double bestT = 0;
  for (int n = 1; n <= SLOT_COUNT; n++) { double t = slotTime(n); if (t > bestT) { bestT = t; best = n; } }
  return best ? best : 1;
}

/* ---------------- ROM checks ---------------- */
// Snes9x finds the layout (LoROM/HiROM, copier headers, chips) itself; this
// only rejects files that cannot be SNES games.
static string romCheck(const vector<uint8_t>& d) {
  if (d.size() < 0x8000) return "This file is too small to be a SNES game.\n\nThe file may be damaged, or an incomplete copy: copy it to the SD card again.";
  if (d.size() > 12 * 1024 * 1024) return "This file is too large to be a SNES game.";
  return "";
}

/* ---------------- Game lifecycle ---------------- */
static void quitGame();
// Reads a .sfc/.smc file, or the first one inside a .zip.
static bool readRom(const Rom& rom, vector<uint8_t>& data, string& err) {
  string path = rom.dir + "/" + rom.name;
  if (!hasExt(rom.name, ".zip")) { if (!readFile(path, data)) { err = "The file could not be read."; return false; } return true; }
  unzFile z = unzOpen(path.c_str());
  if (!z) { err = "This zip file could not be opened. It may be damaged."; return false; }
  bool found = false;
  for (int r = unzGoToFirstFile(z); r == UNZ_OK; r = unzGoToNextFile(z)) {
    char name[512]; unz_file_info info;
    if (unzGetCurrentFileInfo(z, &info, name, sizeof name, NULL, 0, NULL, 0) != UNZ_OK) continue;
    if (!isRomExt(name)) continue;
    if (info.uncompressed_size > 8 * 1024 * 1024) { err = "The game inside this zip is too large."; unzClose(z); return false; }
    data.resize(info.uncompressed_size);
    if (unzOpenCurrentFile(z) != UNZ_OK) break;
    int got = data.empty() ? 0 : unzReadCurrentFile(z, data.data(), (unsigned)data.size());
    unzCloseCurrentFile(z);
    found = got == (int)data.size();
    if (!found) { err = "The game inside this zip could not be unpacked. The zip may be damaged."; unzClose(z); return false; }
    break;
  }
  unzClose(z);
  if (!found) { err = "This zip file has no SNES game (.sfc or .smc) inside."; return false; }
  return true;
}
extern "C" int memjit_live_regions(size_t* bytes);
// CPU clock through the clock service (as OunceSX2 does, common/Horizon/HorizonThreads.cpp):
// the CPU's highest rate without boost mode, which also drops the GPU to its minimum.
static u32 clkMaxPrev = 0;
static u32 clkNormalHz = 0;
static u32 gpuPrevHz = 0;  // CPU rate at startup, before any boost: what quitting a game restores
extern "C" __attribute__((weak)) void sw2ax_crash_watch(int);
extern int sw2_vuaot __attribute__((weak));
extern char sw2_bios_dir[512] __attribute__((weak));
static bool memClockMax = false;
static int memClockMhz = 0, gpuClockMhz = 0;  // test: exact rates  // test: the memory clock at its highest rate too
static u32 emcPrevHz = 0;
static void clockMaxApply(bool on);
// The Switch 2 CPU clock for the chosen mode, now: Normal, the system's boost mode, or the maximum
// through the clock service (restored to the startup rate when leaving Maximum or the game).
static void applySwitchClock() {
  appletSetCpuBoostMode(cpuClock == 1 ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
  if (cpuClock == 2) { if (!clkMaxPrev) clockMaxApply(true); }
  else if (clkMaxPrev) clockMaxApply(false);
}
static void clockMaxApply(bool on) {
  if (R_FAILED(clkrstInitialize())) { record("clock: clkrst unavailable"); return; }
  ClkrstSession cpu, gpu;
  if (R_FAILED(clkrstOpenSession(&cpu, PcvModuleId_CpuBus, 3))) { clkrstExit(); record("clock: no CPU session"); return; }
  bool g = R_SUCCEEDED(clkrstOpenSession(&gpu, PcvModuleId_GPU, 3));
  u32 now = 0, rates[32] = {}, gh = 0; s32 n = 0; PcvClockRatesListType type = PcvClockRatesListType_Invalid;
  clkrstGetClockRate(&cpu, &now);
  Result rr = clkrstGetPossibleClockRates(&cpu, rates, 32, &type, &n);
  if (on) {
    u32 mx = 0;
    if (R_SUCCEEDED(rr) && n > 0) { if (type == PcvClockRatesListType_Range && n >= 2) mx = rates[1]; else for (int i = 0; i < n && i < 32; i++) mx = std::max(mx, rates[i]); }
    string list; char b[24]; for (int i = 0; i < n && i < 32; i++) { snprintf(b, sizeof b, " %u", rates[i] / 1000000); list += b; }
    clkMaxPrev = now;
    Result rs = mx ? clkrstSetClockRate(&cpu, mx) : -1;
    u32 after = 0; clkrstGetClockRate(&cpu, &after); if (g) clkrstGetClockRate(&gpu, &gh);
    if (g) { u32 gr[32] = {}; s32 gn = 0; PcvClockRatesListType gt = PcvClockRatesListType_Invalid; string gl; char gb[24];
      if (R_SUCCEEDED(clkrstGetPossibleClockRates(&gpu, gr, 32, &gt, &gn))) for (int i = 0; i < gn && i < 32; i++) { snprintf(gb, sizeof gb, " %u", gr[i] / 1000000); gl += gb; }
      record("clock: GPU possible rates (type %d):%s", (int)gt, gl.c_str()); }
    if (g && gpuClockMhz > 0) {  // test: an exact GPU rate
      u32 before = 0; clkrstGetClockRate(&gpu, &before);
      Result rg = clkrstSetClockRate(&gpu, (u32)gpuClockMhz * 1000000u); gpuPrevHz = before;
      clkrstGetClockRate(&gpu, &gh); record("clock: GPU %u -> %u MHz requested %d (rc %x)", before / 1000000, gh / 1000000, gpuClockMhz, rg);
    } else if (g && gpuClock == 1) {  // the GPU's highest rate too (GoW2 was GPU-bound at 561 MHz)
      u32 gr[32] = {}; s32 gn = 0; PcvClockRatesListType gt = PcvClockRatesListType_Invalid; u32 gmx = 0;
      if (R_SUCCEEDED(clkrstGetPossibleClockRates(&gpu, gr, 32, &gt, &gn)) && gn > 0) { if (gt == PcvClockRatesListType_Range && gn >= 2) gmx = gr[1]; else for (int i = 0; i < gn && i < 32; i++) gmx = std::max(gmx, gr[i]); }
      u32 before = 0; clkrstGetClockRate(&gpu, &before);
      if (gmx > before) { Result rg = clkrstSetClockRate(&gpu, gmx); gpuPrevHz = before; record("clock: GPU %u -> max %u MHz (rc %x)", before / 1000000, gmx / 1000000, rg); }
      clkrstGetClockRate(&gpu, &gh);
    }
    record("clock: CPU %u -> %u MHz (max %u, rc %x; rates type %d:%s), GPU %u MHz", now / 1000000, after / 1000000, mx / 1000000, rs, (int)type, list.c_str(), gh / 1000000);
    {  // memory (EMC): its possible rates, and its highest when asked (memClockMax)
      ClkrstSession emc;
      if (R_SUCCEEDED(clkrstOpenSession(&emc, PcvModuleId_EMC, 3))) {
        u32 er[32] = {}; s32 en = 0; PcvClockRatesListType et = PcvClockRatesListType_Invalid; string el; char eb[24]; u32 emx = 0, ebefore = 0, eafter = 0;
        clkrstGetClockRate(&emc, &ebefore);
        Result rl = clkrstGetPossibleClockRates(&emc, er, 32, &et, &en);
        if (R_SUCCEEDED(rl)) for (int i = 0; i < en && i < 32; i++) { snprintf(eb, sizeof eb, " %u", er[i] / 1000000); el += eb; emx = std::max(emx, er[i]); }
        if (et == PcvClockRatesListType_Range && en >= 2) emx = er[1];
        Result re = -1;
        if (memClockMhz > 0) { re = clkrstSetClockRate(&emc, (u32)memClockMhz * 1000000u); emcPrevHz = ebefore; }
        else if (memClockMax && emx > ebefore) { re = clkrstSetClockRate(&emc, emx); emcPrevHz = ebefore; }
        clkrstGetClockRate(&emc, &eafter);
        record("clock: memory %u -> %u MHz (rc %x; possible rates rc %x type %d:%s)", ebefore / 1000000, eafter / 1000000, re, rl, (int)et, el.c_str());
        clkrstCloseSession(&emc);
      } else record("clock: no memory clock session");
    }
  } else if (clkMaxPrev) {
    if (g && gpuPrevHz) { clkrstSetClockRate(&gpu, gpuPrevHz); gpuPrevHz = 0; }
    if (emcPrevHz) { ClkrstSession emc; if (R_SUCCEEDED(clkrstOpenSession(&emc, PcvModuleId_EMC, 3))) { clkrstSetClockRate(&emc, emcPrevHz); clkrstCloseSession(&emc); } emcPrevHz = 0; }
    u32 to = clkNormalHz ? clkNormalHz : clkMaxPrev;
    Result rs = clkrstSetClockRate(&cpu, to); clkMaxPrev = 0;
    u32 after = 0; clkrstGetClockRate(&cpu, &after);
    record("clock: CPU restored to %u MHz (rc %x)", after / 1000000, rs);
  }
  clkrstCloseSession(&cpu); if (g) clkrstCloseSession(&gpu);
  clkrstExit();
}
// Each game gets a fresh process: the core does not come back clean from a game (Tekken 4 crashed
// loading a fight after two or three game switches, OpenGL and Vulkan alike; earlier a switched-to
// game could freeze). A second game in a session is started by relaunching the app (homebrew
// loader next-load) with launch.json naming it; startup starts it straight away.
static bool gameStartedThisProcess = false;
static string appSelfPath;
static bool relaunchFor(const Rom& rom) {
  if (autoTest || !gameStartedThisProcess || appSelfPath.empty() || !envHasNextLoad()) return false;
  Json j = Json::object(); j.set("dir", Json::string(rom.dir)); j.set("name", Json::string(rom.name));
  if (!writeText(string(DATA_DIR) + "/launch.json", j.dump())) return false;
  if (R_FAILED(envSetNextLoad(appSelfPath.c_str(), ("\"" + appSelfPath + "\"").c_str()))) { remove((string(DATA_DIR) + "/launch.json").c_str()); return false; }
  record("relaunching for %s (fresh process per game)", rom.name.c_str());
  quitApp = true;
  return true;
}
static void startGame(const Rom& rom) {
  if (relaunchFor(rom)) return;
  gameStartedThisProcess = true;
  static string romPath;
  romPath = rom.dir + "/" + rom.name;
  char bios[256];
  char biosPath[512];
  if (!ps2core_find_bios(biosDir.c_str(), biosPath, sizeof biosPath)) {
    const string shown = biosDir.rfind("sdmc:", 0) == 0 ? biosDir.substr(5) : biosDir;
    showMessage("No PS2 BIOS found.\n\nCopy the BIOS dump from your own PS2 to\n\n    " + shown + "/\n\non the SD card (or choose another folder in Settings -> App -> BIOS folder),\nthen start the game again.", [] { mode = "browser"; });
    return;
  }
  {  // the dump may be in a subfolder: the core gets that folder ("/bios/ps2/...", no "sdmc:") and the file's name
    string p = biosPath; size_t slash = p.rfind('/');
    string folder = slash == string::npos ? biosDir : p.substr(0, slash);
    snprintf(bios, sizeof bios, "%s", slash == string::npos ? p.c_str() : p.c_str() + slash + 1);
    if (&sw2_bios_dir) snprintf(sw2_bios_dir, 512, "%s", folder.rfind("sdmc:", 0) == 0 ? folder.c_str() + 5 : folder.c_str());
  }
#ifdef SW2_VK
  bool gpu = ps2Renderer == 2;  // OpenGL
  const bool vulkan = ps2Renderer == 0;  // the core renders with Vulkan; vkpresent brings its frames to the GL picture
  ps2core_use_vulkan(vulkan);
  sw2_osd_stats = 0;  // the front end's own stats show over a Vulkan game too (its frame comes to the GL picture)
#else
  bool gpu = ps2Renderer == 0;
  const bool vulkan = false;
#endif
  const int res = autoFixedSettings ? ps2Resolution : effectiveRes(rom.name);
  renderScale = gpu || vulkan ? RESOLUTION_SCALE[res] : 1;
  const char* gpuErr = nullptr;
  if (gpu && !ps2core_use_gpu(true, renderScale, &gpuErr)) { record("GPU renderer unavailable: %s", gpuErr ? gpuErr : "?"); gpu = false; renderScale = 1; }
  if (!gpu) ps2core_use_gpu(false, 1, nullptr);
  runningHw = gpu || vulkan;
  ps2core_option("pcsx2_bios", bios);
  ps2core_option("pcsx2_fastboot", "enabled");
  ps2core_option("pcsx2_renderer", gpu ? "OpenGL" : "Software (SW)");
  ps2core_option("pcsx2_upscale_multiplier", gpu || vulkan ? RESOLUTION_VALUES[res] : "1x (Native)");
  ps2core_option("pcsx2_sw_renderer_threads", "3");
  const int rate = autoFixedSettings ? ps2EeSpeed : effectiveRate(rom.name);
  const int skip = autoFixedSettings ? autoCycleSkip : effectiveSkip(rom.name);
  tuneQuietUntil = nowMs() + 12000; slowNoticeShown = false; tuneWin.clear();
  ps2core_option("pcsx2_ee_cycle_rate", EE_SPEED_VALUES[rate]);
  ps2core_option("pcsx2_ee_cycle_skip", CYCLE_SKIP_VALUES[skip]);
  ps2core_option("pcsx2_hw_download_mode", fastReadback(rom.name) ? "Unsynchronized" : "Accurate");
  { auto it = gameBlending.find(rom.name); int b = !autoFixedSettings && it != gameBlending.end() ? it->second : 0;
    // Default: the game database decides (its recommended level, minimum and caps) over a Basic base;
    // a level the player chose is used as it is
    if (&sw2_blending_explicit) sw2_blending_explicit = b > 0 ? 1 : 0;
    ps2core_option("armsx2_blending_accuracy", b > 0 ? BLENDING_LEVELS[b - 1].c_str() : "Basic"); }
  ps2core_option("armsx2_multitap", !autoFixedSettings && multitapFor(rom.name) ? "enabled" : "disabled");  // players 3-4
  ps2core_option("pcsx2_widescreen_hint", !autoFixedSettings && contains(widescreenGames, rom.name) ? "enabled (16:9)" : "disabled");
  for (auto& o : autoOptions) ps2core_option(o.first.c_str(), o.second.c_str());
  if (autoFixedSettings) ps2core_option("armsx2_cheats", "disabled");
  {  // leak check across game starts
    size_t jb = 0; int jn = memjit_live_regions(&jb);
    u64 used = 0, total = 0;
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0); svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    struct mallinfo mi = ::mallinfo();
    record("memory before start: process %llu / %llu MB, heap in use %d MB, JIT regions %d (%zu MB)", (unsigned long long)(used >> 20), (unsigned long long)(total >> 20), (int)(mi.uordblks >> 20), jn, jb >> 20);
  }
  {  // the stack region holds the core's emulated memory (svcMapMemory targets): how much room is left
    u64 sbase = 0, ssize = 0; svcGetInfo(&sbase, InfoType_StackRegionAddress, CUR_PROCESS_HANDLE, 0); svcGetInfo(&ssize, InfoType_StackRegionSize, CUR_PROCESS_HANDLE, 0);
    u64 a = sbase, freeTotal = 0, freeMax = 0; int used = 0; u64 usedBytes = 0;
    while (a < sbase + ssize) {
      MemoryInfo mi{}; u32 pi = 0; if (R_FAILED(svcQueryMemory(&mi, &pi, a)) || !mi.size) break;
      const u64 end = std::min<u64>(mi.addr + mi.size, sbase + ssize), len = end - std::max<u64>(mi.addr, a);
      if (mi.type == MemType_Unmapped) { freeTotal += len; freeMax = std::max(freeMax, len); } else { used++; usedBytes += len; }
      a = end;
    }
    record("stack region: %llu MB, free %llu MB (largest %llu MB), %d mapped blocks (%llu MB)", (unsigned long long)(ssize >> 20),
      (unsigned long long)(freeTotal >> 20), (unsigned long long)(freeMax >> 20), used, (unsigned long long)(usedBytes >> 20));
  }
  coreLogLines = 0;  // the core log limit is per game, so a later game's start-up is logged too
  record("Starting %s: BIOS %s, renderer %s%s, %dx, EE %s, FPU %s, CPU clock %s, cycle skip %s, list entry %s", rom.name.c_str(), bios, gpu || vulkan ? "GPU" : "software", vulkan ? " (Vulkan)" : "", renderScale, EE_SPEEDS[rate].c_str(), FPU_MODES[ps2Fpu].c_str(), CPU_CLOCKS[cpuClock].c_str(), CYCLE_SKIPS[skip].c_str(), speedDefault(rom.name) ? speedDefault(rom.name)->title : "none");
  applySwitchClock();
  if (R_SUCCEEDED(clkrstInitialize())) {  // the clocks this game starts with, for the log
    u32 c = 0, g = 0; ClkrstSession ses;
    if (R_SUCCEEDED(clkrstOpenSession(&ses, PcvModuleId_CpuBus, 3))) { clkrstGetClockRate(&ses, &c); clkrstCloseSession(&ses); }
    if (R_SUCCEEDED(clkrstOpenSession(&ses, PcvModuleId_GPU, 3))) { clkrstGetClockRate(&ses, &g); clkrstCloseSession(&ses); }
    clkrstExit();
    record("clocks at game start: CPU %u MHz, GPU %u MHz", c / 1000000, g / 1000000);
  }
  // Loading takes a moment: say so.
  gfx::beginFrame(col(C_BG)); gfx::text("Starting " + stripNes(rom.name) + "...", 60, 60, 30, col(C_TEXT)); gfx::endFrame(0);
  gfx::beginFrame(col(C_BG)); gfx::text("Starting " + stripNes(rom.name) + "...", 60, 60, 30, col(C_TEXT));
  core_rom_name = romPath.c_str();
#ifdef SW2_VK
  if (vulkan) { std::string err; if (!vkp::prepare(err)) record("vulkan: %s", err.c_str()); }
#endif
  if (const char* err = C->load()) {
    C->unload();

    appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
    showMessage(err, [] { mode = "browser"; });
    return;
  }
  NES_RATE = ps2core_fps() > 1 ? ps2core_fps() : 59.94;
  running = new Running();
  running->name = rom.name;
  running->base = string(DATA_DIR) + "/" + sanitize(rom.name);
  running->stateBase = string(DATA_DIR) + "/states/" + sanitize(rom.name);
  running->settingsBase = string(DATA_DIR) + "/settings/" + sanitize(rom.name);
  {  // states and per-game settings once sat beside everything else in <data>: move them to their folders
    mkdir((string(DATA_DIR) + "/states").c_str(), 0777); mkdir((string(DATA_DIR) + "/settings").c_str(), 0777);
    auto move = [](const string& from, const string& to) { if (exists(from) && !exists(to) && rename(from.c_str(), to.c_str()) == 0) record("moved %s -> %s", from.c_str(), to.c_str()); };
    for (int n = 1; n <= SLOT_COUNT; n++) {
      const string suf = C->stateTag + string(n == 1 ? ".state" : ".state" + std::to_string(n));
      move(running->base + suf, running->stateBase + suf);
    }
    move(running->base + C->stateTag + ".slots.json", running->stateBase + C->stateTag + ".slots.json");
    move(running->base + ".settings.json", running->settingsBase + ".settings.json");
  }
  running->battery = C->has_battery() != 0;
  if (running->battery) {
    vector<uint8_t> s;
    if (readFile(running->base + ".srm", s, 65536)) {
      if ((int)s.size() <= C->sram_size()) memcpy(C->sram(), s.data(), s.size());
      else record("Save file has the wrong size");
    }
    running->sramHash = hashSram();
  }
  C->power();
  snapsReady = rewind_init(REWIND_BUDGET);
  if (C->width() > 0) { W = C->width(); H = C->height(); }
  record("Core %s, battery %d (%d bytes)", C->name, running->battery ? 1 : 0, C->sram_size());
  string t; Json j;
  if (readText(slotsPath(), t) && Json::parse(t, j) && j.type == Json::OBJ) running->slots = j;
  stateSlot = autoLoadState ? newestSlot() : 1;
  running->globalVals = gameValues(); running->perGame = false;
  if (!autoFixedSettings && readText(running->settingsBase + ".settings.json", t) && Json::parse(t, j) && j.type == Json::OBJ) { setGameValuesJson(j); running->perGame = true; }
  recentGames.erase(std::remove(recentGames.begin(), recentGames.end(), rom.name), recentGames.end());
  recentGames.insert(recentGames.begin(), rom.name);
  playHistory.erase(std::remove(playHistory.begin(), playHistory.end(), rom.name), playHistory.end());
  playHistory.insert(playHistory.begin(), rom.name);
  const time_t playedAt = time(nullptr);
  if (playedAt > 0) {
    lastPlayedDates.set(rom.name, Json::number((double)playedAt));
    lastPlayedLabels.erase(rom.name);
  }
  if ((int)recentGames.size() > RECENT_MAX) recentGames.resize(RECENT_MAX);
  saveSettings();
  applyEmuOptions();
  fitPicture(); crtKey.clear(); applyCrt();
  hintUntil = nowMs() + 6000;
  bootedFrames = 0;
  if (autoLoadState && !autoTest) loadState(true, newestSlot());
  resetClock(); mode = "running"; comboLatch = true;
  resetRewind(); emuAcc = 0; ffLatched = false; fastForward = false;
  setAudioRatio(1); setOutputTempo(1); resetAudioMeasurements();
  audioInteg = 0; pendEma = AUDIO_TARGET_FRAMES[audioBuffer];
  audio::prefill(AUDIO_TARGET_FRAMES[audioBuffer]); ratioTicks = 0; emuCount = 0; workMs = 0; statsAt = nowMs(); startedAt = statsAt;
  frameDrawn = false; statsText.clear(); sessionLog = 0;
  running->sramCheck = nowMs();
  playSavedAt = nowMs(); playAccumSec = 0;
  record("Started %s", rom.name.c_str());
}
static void quitGame() {
  pendingLoadSlot = 0;
  if (sw2ax_crash_watch) sw2ax_crash_watch(0);  // a test's crash watch ends with its game
  if (!running) return;
  flushPlaytime(running->name);
  saveSram(true);
  C->unload();
  appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
  if (clkMaxPrev) clockMaxApply(false);
  if (running->perGame) setGameValues(running->globalVals);
  delete running; running = nullptr;
  orderRoms();
  mode = "browser";
}

/* ---------------- Input ---------------- */
static int snesPad(u64 b, const int* map = faceMap) {
  int p = 0;
  bool turboOn = (turbo % TURBO_PERIOD[turboRate]) * 2 < TURBO_PERIOD[turboRate];
  for (int f = 0; f < NFACE; f++) if (b & FACE_BITS[f]) {
    int t = map[f];
    if (t < 6) p |= FACE_TARGETS[t];
    else if (t < 10 && turboOn) p |= FACE_TARGETS[t - 6];
  }
  if (b & B_PLUS) p |= S_START;
  if (b & B_MINUS) p |= S_SELECT;
  if (b & (B_UP | B_SU)) p |= S_UP;
  if (b & (B_DOWN | B_SD)) p |= S_DOWN;
  if (b & (B_LEFT | B_SL)) p |= S_LEFT;
  if (b & (B_RIGHT | B_SR)) p |= S_RIGHT;
  if ((p & (S_UP | S_DOWN)) == (S_UP | S_DOWN)) p &= ~(S_UP | S_DOWN);
  if ((p & (S_LEFT | S_RIGHT)) == (S_LEFT | S_RIGHT)) p &= ~(S_LEFT | S_RIGHT);
  return p;
}
// PS2 pads: every Switch button is a DualShock 2 button in the same place
// (A = Circle, B = Cross, X = Triangle, Y = Square, L/R = L1/R1, ZL/ZR = L2/R2,
// stick clicks = L3/R3, + = Start, - = Select), sticks are the analog sticks.
static PadState* padStates[4];  // players 1-4
// An input's name as the player holds the controller: a single Joy-Con held sideways names its four
// buttons and stick directions by where they are now (its upright names would point elsewhere).
static string inputName(int bit, u32 style) {
  if (style == HidNpadStyleTag_NpadJoyLeft) {
    switch (bit) {
      case 15: return "Right button"; case 12: return "Bottom button"; case 14: return "Top button"; case 13: return "Left button";
      case 19: return "Stick right"; case 17: return "Stick left"; case 16: return "Stick down"; case 18: return "Stick up";
      case 4: return "Stick press"; case 24: return "SL"; case 25: return "SR";
    }
  } else if (style == HidNpadStyleTag_NpadJoyRight) {
    switch (bit) {
      case 2: return "Right button"; case 0: return "Bottom button"; case 3: return "Top button"; case 1: return "Left button";
      case 21: return "Stick right"; case 23: return "Stick left"; case 20: return "Stick up"; case 22: return "Stick down";
      case 5: return "Stick press"; case 26: return "SL"; case 27: return "SR";
    }
  }
  return switchButtonName(bit);
}
// The default layout for a single Joy-Con held sideways (used while a player's layout is the standard
// one): SL / SR = L1 / R1, stick press = L3, its - or + = Start, the four buttons = the PS2's by
// position (right Circle, bottom Cross, top Triangle, left Square), and the stick also the D-pad.
// In PS2_BUTTONS order: Cross Circle Square Triangle L1 R1 L2 R2 L3 R3 Start Select Up Down Left Right.
static const int JOY_LEFT_MAP[16] = {12, 15, 13, 14, 24, 25, -1, -1, 4, -1, 11, -1, 18, 16, 17, 19};
static const int JOY_RIGHT_MAP[16] = {0, 2, 1, 3, 26, 27, -1, -1, 5, -1, 10, -1, 20, 22, 23, 21};
static void setPs2Pad(int player, u64 held, bool blank) {
  Ps2Input& p = ps2Input[player];
  p.buttons = 0; p.lx = p.ly = p.rx = p.ry = 0;
  if (blank || !padStates[player]) return;
  const int* map = padMap[player];
  bool custom = !padMapIsDefault(player);
  if (running) { auto g = gamePadMaps.find(running->name); if (g != gamePadMaps.end()) { map = g->second.m[player]; custom = true; } }  // this game's own layout
  if (!custom) {  // a single Joy-Con on the standard layout gets the Joy-Con one
    const u32 st = padGetStyleSet(padStates[player]);
    if (st == HidNpadStyleTag_NpadJoyLeft) map = JOY_LEFT_MAP;
    else if (st == HidNpadStyleTag_NpadJoyRight) map = JOY_RIGHT_MAP;
  }
  for (int i = 0; i < 16; i++) { const int b = map[i]; if (b >= 0 && (held & (1ull << b))) p.buttons |= 1u << PS2_BUTTONS[i].id; }
  HidAnalogStickState l = padGetStickPos(padStates[player], 0), r = padGetStickPos(padStates[player], 1);
  {  // a single Joy-Con held sideways: its stick, turned upright, is the PS2's left stick
    const u32 st = padGetStyleSet(padStates[player]);
    if (st == HidNpadStyleTag_NpadJoyLeft) l = HidAnalogStickState{-l.y, l.x};          // a quarter anticlockwise
    else if (st == HidNpadStyleTag_NpadJoyRight) { l = HidAnalogStickState{r.y, -r.x}; r = HidAnalogStickState{}; }  // a quarter clockwise
  }
  p.lx = (int16_t)l.x; p.ly = (int16_t)std::max(-32767, -l.y); p.rx = (int16_t)r.x; p.ry = (int16_t)std::max(-32767, -r.y);
}
// In-game menu: L + R with both sticks clicked (games use L3 + R3 alone, e.g.
// Katamari Damacy); every button still belongs to the game. In the menu a lone tap of L / R
// cycles its categories (see the "menu" input).
static const u64 MENU_COMBO = HidNpadButton_L | HidNpadButton_R | HidNpadButton_StickL | HidNpadButton_StickR;
// The menu combo, or on a single Joy-Con held sideways: SL + SR + its stick press.
static bool menuCombo(u64 bits) {
  const u64 left = HidNpadButton_LeftSL | HidNpadButton_LeftSR | HidNpadButton_StickL, right = HidNpadButton_RightSL | HidNpadButton_RightSR | HidNpadButton_StickR;
  return (bits & MENU_COMBO) == MENU_COMBO || (bits & left) == left || (bits & right) == right;
}
// A controller's buttons as the menus read them. A single Joy-Con held sideways gets the layout by
// position: its four buttons are A (right), B (bottom), X (top), Y (left), its stick moves the
// cursor, SL / SR are L / R (tabs); + / - as they are.
static u64 uiBits(PadState* p, u64 raw) {
  const u32 st = padGetStyleSet(p);
  if (st == HidNpadStyleTag_NpadJoyLeft) {
    // held sideways it is turned a quarter anticlockwise: its stick's down is right, up left, left down, right up
    u64 b = raw & ~(u64)(HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right | B_SL | B_SU | B_SR | B_SD);
    if (raw & HidNpadButton_StickLDown) b |= B_SR;
    if (raw & HidNpadButton_StickLUp) b |= B_SL;
    if (raw & HidNpadButton_StickLLeft) b |= B_SD;
    if (raw & HidNpadButton_StickLRight) b |= B_SU;
    if (raw & HidNpadButton_Down) b |= B_A;
    if (raw & HidNpadButton_Left) b |= B_B;
    if (raw & HidNpadButton_Right) b |= B_X;
    if (raw & HidNpadButton_Up) b |= B_Y;
    if (raw & HidNpadButton_LeftSL) b |= B_L;
    if (raw & HidNpadButton_LeftSR) b |= B_R;
    return b;
  }
  if (st == HidNpadStyleTag_NpadJoyRight) {
    u64 b = raw & ~(u64)(HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y);
    if (raw & HidNpadButton_X) b |= B_A;
    if (raw & HidNpadButton_A) b |= B_B;
    if (raw & HidNpadButton_Y) b |= B_X;
    if (raw & HidNpadButton_B) b |= B_Y;
    if (raw & HidNpadButton_RightSL) b |= B_L;
    if (raw & HidNpadButton_RightSR) b |= B_R;
    // turned a quarter clockwise: its stick's up is right, down left, left up, right down (as a left stick)
    if (raw & HidNpadButton_StickRUp) b |= B_SR;
    if (raw & HidNpadButton_StickRDown) b |= B_SL;
    if (raw & HidNpadButton_StickRLeft) b |= B_SU;
    if (raw & HidNpadButton_StickRRight) b |= B_SD;
    return b;
  }
  return raw;
}
static u64 lastBits = 0;
static int heldDirs = 0;
static double repeatAt = 0;
struct Nav { u64 pressed; bool up, down; };
static Nav navEvents(u64 bits) {
  Nav ev; ev.pressed = bits & ~lastBits;
  double t = nowMs(); int out = 0;
  int dirs = (bits & (B_UP | B_SU) ? 1 : 0) | (bits & (B_DOWN | B_SD) ? 2 : 0);
  if (dirs != heldDirs) { heldDirs = dirs; repeatAt = t + 380; out |= dirs; }
  else if (dirs && t >= repeatAt) { repeatAt = t + 110; out |= dirs; }
  lastBits = bits;
  ev.up = out & 1; ev.down = out & 2;
  return ev;
}

/* ---------------- In-game menu ---------------- */
struct MenuItem {
  string label;
  std::function<void()> action;                  // plain action (A)
  std::function<vector<string>()> choices;       // option: choices, get, set, after
  std::function<int()> get;
  std::function<void(int)> set;
  std::function<void()> after;
  bool option() const { return (bool)choices; }
};
static vector<MenuItem> menuItems;
static string menuTitle;
static int menuIndex = 0, menuTop = 0;
static MenuItem act(const string& l, std::function<void()> f) { MenuItem m; m.label = l; m.action = f; return m; }
static MenuItem opt(const string& l, std::function<vector<string>()> c, std::function<int()> g, std::function<void(int)> s, std::function<void()> a = nullptr) {
  MenuItem m; m.label = l; m.choices = c; m.get = g; m.set = s; m.after = a; return m;
}
static bool menuHasTabs = false;  // the main in-game menu: its categories show as tabs (L / R)
static const vector<string> MENU_CATS = {"Game", "Emulation", "Picture", "Controls"};
static int menuCat = 0;  // menuIndex -1: the cursor is on the category tabs
static void showMenuItems(const string& title, const vector<MenuItem>& items) { menuItems = items; menuTitle = title; menuIndex = 0; menuTop = 0; menuHasTabs = false; }
static bool captureRequested = false;
static string captureName;  // set by test runs; otherwise the date and time
static void closeMenu() { if (running) { mode = "running"; if (pauseInMenu) resetClock(); } }
static void pictureChanged() { fitPicture(); applyCrt(); saveSettings(); }
static void setPerGame(bool on) {
  if (on == running->perGame) return;
  if (on) { running->perGame = true; saveSettings(); showToast("Settings saved for this game"); }
  else { running->perGame = false; remove((running->settingsBase + ".settings.json").c_str()); setGameValues(running->globalVals); showToast("Using settings for all games"); }
}
static int crtChoiceIndex() { for (size_t i = 0; i < CRT_CHOICES.size(); i++) if (CRT_CHOICES[i] == crtStyle) return (int)i; return 0; }
static vector<string> crtChoiceNames() { vector<string> v; for (int id : CRT_CHOICES) v.push_back(CRT_STYLES[id]); return v; }

static void openMenu();
static void rebuildSettings();
static void openCheatsMenu() {
  vector<Ps2Cheat> catalog;
  const string pnachPath = ps2core_cheats(catalog);
  if (pnachPath.empty()) {
    showToast("Cheats are available once the PS2 disc is identified");
    return;
  }

  const string selectionPath = pnachPath + ".enabled";
  string text;
  readText(selectionPath, text);
  auto enabled = std::make_shared<vector<string>>();
  std::istringstream input(text);
  for (string name; std::getline(input, name);) enabled->push_back(std::move(name));

  vector<MenuItem> items;
  for (const auto& cheat : catalog) {
    items.push_back(opt(cheat.name, [] { return OFF_ON; },
      [enabled, cheat] { return contains(*enabled, cheat.name) ? 1 : 0; },
      [enabled, cheat, selectionPath](int value) {
        auto selected = *enabled;
        selected.erase(std::remove(selected.begin(), selected.end(), cheat.name), selected.end());
        if (value) selected.push_back(cheat.name);
        string text;
        for (const auto& name : selected) text += name + "\n";
        if (!writeText(selectionPath, text)) {
          showToast("Could not save cheat selection");
          return;
        }
        *enabled = std::move(selected);
        ps2core_reload_cheats();
        if (cheat.bootOnly && value) showToast("This cheat applies when the game restarts");
      }));
  }
  items.push_back(act(catalog.empty() ? "No matching cheats. Add a PNACH file" : "Add a PNACH file...",
    [pnachPath] {
      showMessage("Copy a PNACH file with [named cheat] sections to:\n\n" +
                  pnachPath + "\n\nThen restart the game.", [] { mode = "menu"; });
    }));
  items.push_back(act("Back", [] { openMenu(); }));
  showMenuItems("Cheats", items);
}
// The core changes its upscale live (the GS purges its texture cache, a short hitch); the picture
// geometry follows the new frame size through renderScale.
static void applyResolutionLive() {
  if (!running || !runningHw) { showToast("This game runs on the software renderer: resolution stays native", 3000); return; }
  const int r = effectiveRes(running->name);
  if (RESOLUTION_SCALE[r] == renderScale) return;
  renderScale = RESOLUTION_SCALE[r];
  ps2core_option("pcsx2_upscale_multiplier", RESOLUTION_VALUES[r]);
  pictureChanged();
  record("resolution: %s now %s", running->name.c_str(), RESOLUTIONS[r].c_str());
  showToast("Resolution " + RESOLUTIONS[r], 2000);
}
// Per-game emulation settings applied to the running game at once (the core takes the option on
// its next frame and recompiles where needed: a short hitch).
static void applyRateLive() {
  const int r = effectiveRate(running->name); ps2core_option("pcsx2_ee_cycle_rate", EE_SPEED_VALUES[r]);
  record("live: %s PS2 CPU speed %s", running->name.c_str(), EE_SPEEDS[r].c_str());
}
static void applySkipLive() {
  const int k = effectiveSkip(running->name); ps2core_option("pcsx2_ee_cycle_skip", CYCLE_SKIP_VALUES[k]);
  record("live: %s PS2 CPU cycle skip %s", running->name.c_str(), CYCLE_SKIPS[k].c_str());
}
static void applyReadbackLive() {
  const bool f = fastReadback(running->name); ps2core_option("pcsx2_hw_download_mode", f ? "Unsynchronized" : "Accurate");
  record("live: %s GPU readbacks %s", running->name.c_str(), f ? "Fast" : "Accurate");
}
static void applyBlendingChoice(int b) {
  if (&sw2_blending_explicit) sw2_blending_explicit = b > 0 ? 1 : 0;  // Default (0): the game database decides, over Basic
  const string& level = BLENDING_LEVELS[b > 0 ? b - 1 : 1];
  ps2core_option("armsx2_blending_accuracy", level.c_str());
  record("live: %s blending accuracy %s", running->name.c_str(), b > 0 ? level.c_str() : "Default (game database, base Basic)");
  if (b == 1) showToast("Minimum blending may change visual effects", 3500);
}
static void applyBlendingLive() {
  const auto it = gameBlending.find(running->name);
  applyBlendingChoice(it == gameBlending.end() ? 0 : it->second);
}
static void openRemap(int player, bool forGame);
static void pairControllers();
// The main in-game menu's rows for one category (adv: with the advanced ones).
static vector<MenuItem> menuItemsFor(int cat, bool adv) {
  vector<MenuItem> items = {
      act("Resume", [] { closeMenu(); }),
      act("Save state", [] { saveState(); closeMenu(); }),
      act("Load state", [] { closeMenu(); loadState(false, stateSlot); }),
      opt("Slot", [] { vector<string> a; for (int n = 1; n <= SLOT_COUNT; n++) a.push_back(slotLabel(n)); return a; },
          [] { return stateSlot - 1; }, [](int v) { stateSlot = v + 1; }),
      act("Reset", [] { C->reset(0); closeMenu(); showToast("Reset"); }),
      opt("Run-ahead", [] { return RUN_AHEADS; }, [] { return runAhead; }, [](int v) { runAhead = v; }, saveSettings),
      opt("Stats", [] { return STATS_LEVELS; }, [] { return statsLevel; }, [](int v) { statsLevel = v; }, [] { resetGraph(); saveSettings(); }),
      opt("Widescreen", [] { return OFF_ON; }, [] { return contains(widescreenGames, running->name) ? 1 : 0; },
          [](int v) { widescreenGames.erase(std::remove(widescreenGames.begin(), widescreenGames.end(), running->name), widescreenGames.end()); if (v) widescreenGames.push_back(running->name); },
          [] { saveSettings(); pictureChanged(); showToast("16:9 now. Games without a widescreen setting of their own get the patch from their next start", 4000); }),
      // These apply at once (the core recompiles: a short hitch); saved for this game.
      opt("PS2 CPU speed", [] { vector<string> a = {"Auto (" + EE_SPEEDS[autoRate(running->name)] + ")"}; for (auto& x : EE_SPEEDS) a.push_back(x); return a; },
          [] { auto it = gameRate.find(running->name); return it == gameRate.end() ? 0 : it->second; },
          [](int v) { gameRate[running->name] = v; },
          [] { saveSettings(); applyRateLive(); }),
      opt("PS2 CPU cycle skip", [] { vector<string> a = {"Auto (" + CYCLE_SKIPS[autoSkip(running->name)] + ")"}; for (auto& x : CYCLE_SKIPS) a.push_back(x); return a; },
          [] { auto it = gameSkip.find(running->name); return it == gameSkip.end() ? 0 : it->second; },
          [](int v) { gameSkip[running->name] = v; },
          [] { saveSettings(); applySkipLive(); }),
      opt("EE event wait", [] { return EE_WAIT_CHOICES; },
          [] {
            for (size_t i = 0; i < EE_WAIT_VALUES.size(); i++)
              if (sw2_ee_wait_cycles == EE_WAIT_VALUES[i]) return (int)i;
            return 4;
          },
          [](int v) {
            if (v >= 0 && v < (int)EE_WAIT_VALUES.size()) {
              sw2_ee_wait_cycles = EE_WAIT_VALUES[v];
              record("live: %s EE event wait %s", running->name.c_str(), EE_WAIT_CHOICES[v].c_str());
            }
          }),
      opt("GPU readbacks", [] { return vector<string>{"Auto (Fast)", "Accurate", "Fast"}; },
          [] { auto it = gameReadback.find(running->name); return it != gameReadback.end() ? it->second : 0; },
          [](int v) { fastReadbackGames.erase(std::remove(fastReadbackGames.begin(), fastReadbackGames.end(), running->name), fastReadbackGames.end()); gameReadback[running->name] = v; },
          [] { saveSettings(); applyReadbackLive(); }),
      opt("Blending accuracy", [] { vector<string> a = {"Default (game database)"}; for (const auto& x : BLENDING_LEVELS) a.push_back(x); return a; },
          [] { auto it = gameBlending.find(running->name); return it == gameBlending.end() ? 0 : it->second; },
          [](int v) { gameBlending[running->name] = v; }, [] { saveSettings(); applyBlendingLive(); }),
      opt("Switch 2 CPU clock", [] { return CPU_CLOCKS; }, [] { return cpuClock; }, [](int v) { cpuClock = v; },
          [] { saveSettings(); applySwitchClock(); record("live: Switch 2 CPU clock %s", CPU_CLOCKS[cpuClock].c_str()); }),
      opt("Resolution", [] { vector<string> a = {"Auto (" + RESOLUTIONS[ps2Resolution] + ")"}; for (auto& x : RESOLUTIONS) a.push_back(x); return a; },
          [] { auto it = gameRes.find(running->name); return it == gameRes.end() ? 0 : it->second; },
          [](int v) { gameRes[running->name] = v; }, [] { saveSettings(); applyResolutionLive(); }),
      opt("Aspect", [] { return ASPECTS; }, [] { return aspectMode; }, [](int v) { aspectMode = v; }, pictureChanged),
      opt("Scale", [] { return vector<string>{scaleLabel(0), scaleLabel(1)}; }, [] { return scaleMode; }, [](int v) { scaleMode = v; }, pictureChanged),
      opt("Overscan", [] { return CROPS; }, [] { return cropMode; }, [](int v) { cropMode = v; }, pictureChanged),
      opt("Signal", [] { return SIGNALS; }, [] { return signalMode; }, [](int v) { signalMode = v; }, pictureChanged),
      opt("Shader", [] { return SHADERS; }, [] { return shader; }, [](int v) { shader = v; }, pictureChanged),
      opt("Brightness boost", [] { return BOOSTS; }, [] { return boost; }, [](int v) { boost = v; }, pictureChanged),
      opt("Curvature", [] { return CURVATURES; }, [] { return curvature; }, [](int v) { curvature = v; }, pictureChanged),
      opt("Corner radius", [] { return CORNER_RADII; }, [] { return cornerRadius; }, [](int v) { cornerRadius = v; }, pictureChanged),
      opt("Corner softness", [] { return CORNER_SOFTNESS; }, [] { return cornerSoftness; }, [](int v) { cornerSoftness = v; }, pictureChanged),
      opt("Vignette", [] { return VIGNETTES; }, [] { return vignette; }, [](int v) { vignette = v; }, pictureChanged),
      opt("Vignette size", [] { return VIGNETTE_SIZES; }, [] { return vignetteSize; }, [](int v) { vignetteSize = v; }, pictureChanged),
      opt("Favourite", [] { return OFF_ON; }, [] { return contains(favourites, running->name) ? 1 : 0; },
          [](int v) { favourites.erase(std::remove(favourites.begin(), favourites.end(), running->name), favourites.end()); if (v) favourites.push_back(running->name); }, saveSettings),
      opt("Settings for", [] { return vector<string>{"All games", "This game"}; }, [] { return running->perGame ? 1 : 0; }, [](int v) { setPerGame(v != 0); },
          [] { pictureChanged(); refreshPicture(); }),
      act("Save screenshot", [] { closeMenu(); captureRequested = true; }),
      act("Cheats...", [] { openCheatsMenu(); }),
      // Controls: a layout made here is this game's own
      act("Pair controllers", [] { pairControllers(); }),
      act("Player 1 buttons", [] { openRemap(0, true); }),
      act("Player 2 buttons", [] { openRemap(1, true); }),
      opt("Multitap (players 3-4)", [] { return OFF_ON; }, [] { return multitapFor(running->name) ? 1 : 0; }, [](int v) { gameMultitap[running->name] = v != 0; },
          [] {  // plugged in or out now, as on a PS2; the menu's rows change (players 3-4)
            saveSettings(); ps2core_option("armsx2_multitap", multitapFor(running->name) ? "enabled" : "disabled");
            record("live: %s Multitap %s", running->name.c_str(), multitapFor(running->name) ? "on" : "off");
            openMenu(); for (int i = 0; i < (int)menuItems.size(); i++) if (menuItems[i].label == "Multitap (players 3-4)") menuIndex = i;
          }),
      act("Player 3 buttons", [] { openRemap(2, true); }),
      act("Player 4 buttons", [] { openRemap(3, true); }),
      act("Use Settings buttons", [] { if (gamePadMaps.erase(running->name)) { saveSettings(); showToast("This game uses the Settings buttons again", 2500); openMenu(); } }),
      act("Reset buttons to standard", [] {  // a second A within 4 s confirms
        static double armedUntil = 0;
        if (nowMs() > armedUntil) { armedUntil = nowMs() + 4000; showToast("Press {A} again to put every player's buttons back to standard", 4000); return; }
        armedUntil = 0; gamePadMaps.erase(running->name); for (int pl = 0; pl < 4; pl++) resetPadMap(pl); saveSettings();
        showToast("Every player's buttons are back to standard (single Joy-Cons: their own default)", 3500); openMenu();
      }),
      opt("Advanced options", [] { return vector<string>{"Hidden", "Shown"}; }, [] { return showAdvanced ? 1 : 0; }, [](int v) { showAdvanced = v != 0; },
          [] {  // the menu's own rows change: rebuild it, the cursor staying on this row
            saveSettings(); rebuildSettings();
            openMenu();
            for (int i = 0; i < (int)menuItems.size(); i++) if (menuItems[i].label == "Advanced options") menuIndex = i;
          }),
      act("Quit game", [] { quitGame(); }),
  };
  {
    vector<MenuItem> kept;
    static const vector<string> EMU = {"PS2 CPU speed", "PS2 CPU cycle skip", "EE event wait", "GPU readbacks", "Blending accuracy", "Switch 2 CPU clock", "Resolution", "Widescreen", "Stats"};
    static const vector<string> PIC = {"Aspect", "Scale", "Overscan", "Signal", "Shader", "Brightness boost", "Curvature", "Corner radius", "Corner softness",
                                       "Vignette", "Vignette size", "Settings for"};
    static const vector<string> CTL = {"Pair controllers", "Player 1 buttons", "Player 2 buttons", "Multitap (players 3-4)", "Player 3 buttons", "Player 4 buttons", "Use Settings buttons", "Reset buttons to standard"};
    auto catOf = [](const string& l) { return contains(EMU, l) ? 1 : contains(PIC, l) ? 2 : contains(CTL, l) ? 3 : 0; };
    const bool ownLayout = running && gamePadMaps.count(running->name), multitap = running && multitapFor(running->name);
    for (auto& m : items) if (m.label != "Run-ahead" && (m.label != "Use Settings buttons" || ownLayout) &&
                             (multitap || (m.label != "Player 3 buttons" && m.label != "Player 4 buttons")) && (adv || !contains(ADVANCED_MENU, m.label)) && catOf(m.label) == cat) kept.push_back(m);
    items = kept;
  }
  return items;
}
// The fixed number of rows the tabbed menu is sized for: its largest category with every
// advanced row shown, so switching category or showing / hiding advanced never resizes it.
static int menuFixedRows() {
  static int rows = 0;
  if (!rows) for (int c = 0; c < (int)MENU_CATS.size(); c++) rows = std::max(rows, (int)menuItemsFor(c, true).size());
  return rows;
}
static void openMenu() {
  showMenuItems(stripNes(running->name), menuItemsFor(menuCat, showAdvanced));
  menuHasTabs = true;
  mode = "menu";
  saveSram(false);
}
static void cycleMenuOption(int dir) {
  MenuItem& o = menuItems[menuIndex];
  if (!o.option()) return;
  int n = (int)o.choices().size();
  o.set((o.get() + dir + n) % n);
  auto after = o.after;  // a copy: it may rebuild the menu (Advanced options), freeing o
  if (after) after();
}

/* ---------------- ROM folder picker ---------------- */
static string pickPath;
static vector<string> pickDirs;
static int pickSel = 0, pickTop = 0, pickGames = 0;
static string parentDir(const string& p) {
  size_t k = p.find_last_of('/');
  if (k == string::npos || k <= 5) return "sdmc:/";
  return p.substr(0, k);
}
static void pickLoad(const string& path) {
  pickPath = path; pickDirs.clear(); pickSel = 0; pickTop = 0; pickGames = 0;
  string open = path == "sdmc:" ? "sdmc:/" : path;
  if (DIR* d = opendir(open.c_str())) {
    while (struct dirent* e = readdir(d)) {
      string n = e->d_name;
      if (n.empty() || n[0] == '.') continue;
      if (e->d_type == DT_DIR) pickDirs.push_back(n);
      else if (isRomExt(n) || hasExt(n, ".zip")) pickGames++;
    }
    closedir(d);
  }
  std::sort(pickDirs.begin(), pickDirs.end(), [](const string& a, const string& b) { return naturalLess(a, b); });
}
static bool pickAtRoot() { return pickPath == "sdmc:/" || pickPath == "sdmc:"; }
// Rows: "Use this folder", then ".." (unless at the root), then subfolders.
static int pickRows() { return 1 + (pickAtRoot() ? 0 : 1) + (int)pickDirs.size(); }
static void openFolderPicker(bool bios = false) {
  struct stat st;
  pickForBios = bios;
  const string& start = bios ? biosDir : romDir;
  pickLoad(stat(start.c_str(), &st) == 0 ? start : "sdmc:/");
  mode = "folders";
}

/* ---------------- Settings screen ---------------- */
/* ---------------- Button remapping ---------------- */
// One PS2 button at a time: the player presses the Switch button they want on their controller, or
// waits REMAP_WAIT_MS to leave it unmapped; then Save / Start again / Cancel.
static const double REMAP_WAIT_MS = 4000;
static int remapWho = 0, remapStep = 0, remapPick = 0, remapNew[16];
static bool remapForGame = false;  // opened from the in-game menu: saves this game's own layout, returns to the menu
static double remapDeadline = 0;
static bool remapWaitRelease = true;
static void openRemap(int player, bool forGame) {
  remapForGame = forGame && running;
  remapWho = player; remapStep = 0; remapPick = 0; remapWaitRelease = true;
  for (int i = 0; i < 16; i++) remapNew[i] = -1;
  remapDeadline = nowMs() + REMAP_WAIT_MS;
  mode = "remap";
}
static bool padConnected(int player) { return padStates[player] && padIsConnected(padStates[player]); }
// Joy-Cons: together (two detached Joy-Cons are one controller) or separate (each one its own
// controller, held sideways: the first is player 1, the next player 2). Joy-Cons attached to the
// console stay one controller. (The system's controller screen can't be opened from hbmenu here.)
// The Switch's own controller screen (connect / change grip and order, single Joy-Cons sideways).
// From hbmenu the plain call is refused; the system variant opens it.
static void pairControllers() {
  HidLaControllerSupportArg arg; hidLaCreateControllerSupportArg(&arg);
  arg.hdr.player_count_min = 0; arg.hdr.player_count_max = 4;  // every controller shows; PS2 ports 1-2 are players 1-2
  HidLaControllerSupportResultInfo info{};
  Result rc = hidLaShowControllerSupport(&info, &arg), rc2 = 0;
  const bool triedSystem = R_FAILED(rc);
  if (triedSystem) rc2 = hidLaShowControllerSupportForSystem(&info, &arg, false);
  record("controllers: support applet rc 0x%x, system variant rc 0x%x, players %d", rc, rc2, (int)info.player_count);
  char msg[160];
  if (R_SUCCEEDED(rc) || (triedSystem && R_SUCCEEDED(rc2))) snprintf(msg, sizeof msg, "Controllers updated");
  else snprintf(msg, sizeof msg, "The controller screen could not open (0x%x / 0x%x)", rc, rc2);
  showToast(msg, 6000);
}
static bool resetMapArmed = false;
static bool settingsByA = false;  // the row's action came from A (not left/right): for rows that do something
struct SettingItem { string label; std::function<vector<string>()> choices; std::function<int()> get; std::function<void(int)> set; };
static vector<SettingItem> settingsItems() {
  return {
      {"ROM folder", [] { return vector<string>{romDir.rfind("sdmc:", 0) == 0 ? romDir.substr(5) : romDir}; }, [] { return 0; }, [](int) { openFolderPicker(); }},
      {"Debug logging", [] { return OFF_ON; }, [] { return debugLog ? 1 : 0; }, [](int v) {
        debugLog = v != 0;
        if (debugLog && !logFile) { openLog(false); record("SW2PS2 %s: debug logging turned on", APP_VERSION); }
        else if (!debugLog && logFile && !autoTest) { fclose(logFile); logFile = nullptr; }
      }},
      {"BIOS folder", [] { return vector<string>{biosDir.rfind("sdmc:", 0) == 0 ? biosDir.substr(5) : biosDir}; }, [] { return 0; }, [](int) { openFolderPicker(true); }},
      {"Resolution", [] { return RESOLUTIONS; }, [] { return ps2Resolution; }, [](int v) { ps2Resolution = v; }},
      {"Emulated CPU speed (next start)", [] { return EE_SPEEDS; }, [] { return ps2EeSpeed; }, [](int v) { ps2EeSpeed = v; }},
      {"Emulated CPU cycle skip (next start)", [] { return CYCLE_SKIPS; }, [] { return ps2CycleSkip; }, [](int v) { ps2CycleSkip = v; }},
      {"Vibration", [] { return VIB_LEVELS; }, [] { return vibLevel; }, [](int v) { vibLevel = v; }},
      {"Pair controllers", [] { return vector<string>{"Press {A}"}; }, [] { return 0; }, [](int) { if (settingsByA) pairControllers(); }},
      {"Player 1 buttons", [] { return vector<string>{padMapIsDefault(0) ? "Standard - {A} to remap" : "Custom - {A} to remap"}; }, [] { return 0; }, [](int) { if (settingsByA) openRemap(0, false); }},
      {"Player 2 buttons", [] { return vector<string>{padMapIsDefault(1) ? "Standard - {A} to remap" : "Custom - {A} to remap"}; }, [] { return 0; }, [](int) { if (settingsByA) openRemap(1, false); }},
      {"Multitap (players 3-4)", [] { return OFF_ON; }, [] { return multitapDefault ? 1 : 0; }, [](int v) { multitapDefault = v != 0; rebuildSettings(); }},
      {"Player 3 buttons", [] { return vector<string>{padMapIsDefault(2) ? "Standard - {A} to remap" : "Custom - {A} to remap"}; }, [] { return 0; }, [](int) { if (settingsByA) openRemap(2, false); }},
      {"Player 4 buttons", [] { return vector<string>{padMapIsDefault(3) ? "Standard - {A} to remap" : "Custom - {A} to remap"}; }, [] { return 0; }, [](int) { if (settingsByA) openRemap(3, false); }},
      {"Reset button mapping", [] { return vector<string>{resetMapArmed ? "Press {A} again to confirm" : "Press {A}"}; }, [] { return 0; },
       [](int) { if (!settingsByA) return; if (!resetMapArmed) { resetMapArmed = true; return; }
                 resetMapArmed = false; for (int pl = 0; pl < 4; pl++) resetPadMap(pl); saveSettings(); showToast("All players back on the standard layout", 2500); }},
      {"Switch 2 CPU clock (next start)", [] { return CPU_CLOCKS; }, [] { return cpuClock; }, [](int v) { cpuClock = v; }},
      {"Aspect", [] { return ASPECTS; }, [] { return aspectMode; }, [](int v) { aspectMode = v; }},
      {"Scale", [] { return SCALES; }, [] { return scaleMode; }, [](int v) { scaleMode = v; }},
      {"Overscan", [] { return CROPS; }, [] { return cropMode; }, [](int v) { cropMode = v; }},
      {"Signal", [] { return SIGNALS; }, [] { return signalMode; }, [](int v) { signalMode = v; }},
      {"Shader", [] { return SHADERS; }, [] { return shader; }, [](int v) { shader = v; }},
      {"Brightness boost", [] { return BOOSTS; }, [] { return boost; }, [](int v) { boost = v; }},
      {"Curvature", [] { return CURVATURES; }, [] { return curvature; }, [](int v) { curvature = v; }},
      {"Corner radius", [] { return CORNER_RADII; }, [] { return cornerRadius; }, [](int v) { cornerRadius = v; }},
      {"Corner softness", [] { return CORNER_SOFTNESS; }, [] { return cornerSoftness; }, [](int v) { cornerSoftness = v; }},
      {"Vignette", [] { return VIGNETTES; }, [] { return vignette; }, [](int v) { vignette = v; }},
      {"Vignette size", [] { return VIGNETTE_SIZES; }, [] { return vignetteSize; }, [](int v) { vignetteSize = v; }},
      {"Stats", [] { return STATS_LEVELS; }, [] { return statsLevel; }, [](int v) { statsLevel = v; }},
      {"Run-ahead", [] { return RUN_AHEADS; }, [] { return runAhead; }, [](int v) { runAhead = v; }},
      {"Frame delay (60 Hz)", [] { return FRAME_DELAYS; }, [] { return frameDelay; }, [](int v) { frameDelay = v; }},
      {"Audio buffer", [] { return AUDIO_BUFFERS; }, [] { return audioBuffer; }, [](int v) { audioBuffer = v; }},
      {"Display", [] { return DISPLAY_MODES; }, [] { return displayMode; }, [](int v) { displayMode = v; gfx::setHighRefresh(v == 1); }},
      {"Rewind (hold ZL)", [] { return OFF_ON; }, [] { return rewindOn ? 1 : 0; }, [](int v) { rewindOn = v != 0; }},
      {"Fast-forward", [] { return FF_MODES; }, [] { return ffMode; }, [](int v) { ffMode = v; }},
      {"Fast-forward speed", [] { return FF_SPEEDS; }, [] { return ffSpeed; }, [](int v) { ffSpeed = v; }},
      {"Auto-load save state", [] { return OFF_ON; }, [] { return autoLoadState ? 1 : 0; }, [](int v) { autoLoadState = v != 0; }},
      {"Pause in menu", [] { return OFF_ON; }, [] { return pauseInMenu ? 1 : 0; }, [](int v) { pauseInMenu = v != 0; }},
      {"Dim game in menu", [] { return OFF_ON; }, [] { return dimInMenu ? 1 : 0; }, [](int v) { dimInMenu = v != 0; }},
      {"Volume", [] { return VOLUMES; }, [] { return volumeLevel; }, [](int v) { volumeLevel = v; }},
      {"Audio filter", [] { return FILTERS; }, [] { return audioFilter; }, [](int v) { audioFilter = v; }},
      {"Turbo speed", [] { return TURBO_RATES; }, [] { return turboRate; }, [](int v) { turboRate = v; }},
      {"Button mapping for", [] { return vector<string>{"Player 1", "Player 2"}; }, [] { return remapPlayer; }, [](int v) { remapPlayer = v; }},
      {"Switch A button", [] { return FACE_CHOICES; }, [] { return curMap()[0]; }, [](int v) { curMap()[0] = v; }},
      {"Switch B button", [] { return FACE_CHOICES; }, [] { return curMap()[1]; }, [](int v) { curMap()[1] = v; }},
      {"Switch X button", [] { return FACE_CHOICES; }, [] { return curMap()[2]; }, [](int v) { curMap()[2] = v; }},
      {"Switch Y button", [] { return FACE_CHOICES; }, [] { return curMap()[3]; }, [](int v) { curMap()[3] = v; }},
      {"Switch L button", [] { return FACE_CHOICES; }, [] { return curMap()[4]; }, [](int v) { curMap()[4] = v; }},
      {"Switch R button", [] { return FACE_CHOICES; }, [] { return curMap()[5]; }, [](int v) { curMap()[5] = v; }},
  };
}
static vector<SettingItem> SETTINGS_ITEMS;
static bool resetArmed = false;
// Settings come in categories, cycled with L / R like the game list's tabs.
static const vector<string> SETTINGS_CATS = {"Emulation", "Picture", "Sound & controls", "App"};
static int settingsCat = 0;  // settingsIndex -1: the cursor is on the category tabs
static int settingsCatOf(const string& label) {
  static const vector<string> EMU = {"Resolution", "Emulated CPU speed (next start)", "Emulated CPU cycle skip (next start)", "Switch 2 CPU clock (next start)", "Auto-load save state"};
  static const vector<string> PIC = {"Aspect", "Scale", "Overscan", "Signal", "Shader", "Brightness boost", "Curvature", "Corner radius", "Corner softness", "Vignette", "Vignette size", "Display", "Frame delay (60 Hz)"};
  static const vector<string> SND = {"Volume", "Audio buffer", "Vibration", "Pair controllers", "Player 1 buttons", "Player 2 buttons", "Multitap (players 3-4)", "Player 3 buttons", "Player 4 buttons", "Reset button mapping"};
  return contains(EMU, label) ? 0 : contains(PIC, label) ? 1 : contains(SND, label) ? 2 : 3;  // the rest: App
}
static vector<SettingItem> settingsFor(int cat, bool adv);
static void rebuildSettings() {
  SETTINGS_ITEMS = settingsFor(settingsCat, showAdvanced);
  if (settingsIndex > (int)SETTINGS_ITEMS.size()) settingsIndex = (int)SETTINGS_ITEMS.size();
}
// The fixed number of rows Settings is sized for: its largest category with every advanced row
// shown, plus Back, so switching category or showing / hiding advanced never resizes it.
static int settingsFixedRows() {
  static int rows = 0;
  if (!rows) for (int c = 0; c < (int)SETTINGS_CATS.size(); c++) rows = std::max(rows, (int)settingsFor(c, true).size() + 1);
  return rows;
}
static vector<SettingItem> settingsFor(int cat, bool adv) {
  vector<SettingItem> items;
  if (cat == 3) items.push_back({"Advanced options", [] { return vector<string>{"Hidden", "Shown"}; }, [] { return showAdvanced ? 1 : 0; },
                                 [](int v) { showAdvanced = v != 0; rebuildSettings(); }});
  // Not on this core: run-ahead, rewind, fast-forward, turbo and button remapping.
  static const vector<string> NOT_HERE = {"Run-ahead", "Rewind (hold ZL)", "Fast-forward", "Fast-forward speed", "Turbo speed", "Button mapping for", "Audio filter",
      "Switch A button", "Switch B button", "Switch X button", "Switch Y button", "Switch L button", "Switch R button"};
  for (auto& it : settingsItems())
    if (!contains(NOT_HERE, it.label) && (adv || !contains(ADVANCED_SETTINGS, it.label)) && settingsCatOf(it.label) == cat &&
        (multitapDefault || (it.label != "Player 3 buttons" && it.label != "Player 4 buttons"))) items.push_back(it);
  if (adv && cat == 3)
    items.push_back({"Reset all settings", [] { return vector<string>{"Press {A}", "Press {A} again to confirm"}; }, [] { return resetArmed ? 1 : 0; },
                     [](int v) { if (resetArmed) { resetArmed = false; resetAllSettings(); gfx::setHighRefresh(displayMode == 1); rebuildSettings(); settingsIndex = 0; settingsTop = 0; showToast("All settings reset to defaults"); } else resetArmed = true; }});
  return items;
}

/* ---------------- Drawing helpers ---------------- */
static const float LH = 1.15f;  // CSS "normal" line height, approximately
// Text vertically centred in a box.
// Button names in text: "{A}" "{B}" "{X}" "{Y}" are drawn as the letter, or -- while a single Joy-Con
// is connected, whose buttons have no letters that match -- as the position glyph Nintendo and Steam
// use: four dots in a diamond, the one to press filled (A right, B bottom, X top, Y left).
static bool singleJoyConConnected = false;  // updated each frame (main loop)
static int glyphToken(const string& s, size_t i) {  // 0-3 for {A} {B} {X} {Y} at i, else -1
  if (i + 2 >= s.size() || s[i] != '{' || s[i + 2] != '}') return -1;
  const char* k = strchr("ABXY", s[i + 1]);
  return k && s[i + 1] ? (int)(k - "ABXY") : -1;
}
static float glyphWidth(float size) { return size * 1.15f; }
static void drawFaceGlyph(int which, float x, float y, float size, Color c) {  // in a size x size box
  const float r = size * 0.17f, d = size * 0.31f, cx = x + glyphWidth(size) / 2, cy = y + size * 0.62f;
  static const float DX[4] = {1, 0, 0, -1}, DY[4] = {0, 1, -1, 0};  // A right, B bottom, X top, Y left
  for (int k = 0; k < 4; k++) {
    const float px = cx + DX[k] * d - r, py = cy + DY[k] * d - r;
    if (k == which) gfx::rect(px, py, r * 2, r * 2, c, r);
    else gfx::rect(px, py, r * 2, r * 2, Color{0, 0, 0, 0}, r, std::max(1.0f, size * 0.07f), c);
  }
}
static float glyphTextWidth(const string& s, float size, gfx::Font f = gfx::REGULAR) {
  if (s.find('{') == string::npos) return gfx::textWidth(s, size, f);
  float w = 0; string run;
  for (size_t i = 0; i < s.size(); i++) {
    const int g = glyphToken(s, i);
    if (g < 0) { run += s[i]; continue; }
    w += gfx::textWidth(run, size, f); run.clear();
    w += singleJoyConConnected ? glyphWidth(size) : gfx::textWidth(string(1, "ABXY"[g]), size, f);
    i += 2;
  }
  return w + gfx::textWidth(run, size, f);
}
static float glyphText(const string& s, float x, float y, float size, Color c, gfx::Font f = gfx::REGULAR) {
  if (s.find('{') == string::npos) return gfx::text(s, x, y, size, c, f);
  float x0 = x; string run;
  auto flush = [&]() { if (!run.empty()) { gfx::text(run, x, y, size, c, f); x += gfx::textWidth(run, size, f); run.clear(); } };
  for (size_t i = 0; i < s.size(); i++) {
    const int g = glyphToken(s, i);
    if (g < 0) { run += s[i]; continue; }
    flush();
    if (singleJoyConConnected) { drawFaceGlyph(g, x, y, size, c); x += glyphWidth(size); }
    else { const string l(1, "ABXY"[g]); gfx::text(l, x, y, size, c, f); x += gfx::textWidth(l, size, f); }
    i += 2;
  }
  flush();
  return x - x0;
}
static float textMid(const string& s, float x, float y, float h, float size, Color c, gfx::Font f = gfx::REGULAR) {
  return glyphText(s, x, y + (h - size * 1.25f) / 2, size, c, f);
}
// "< value >" selector, right-aligned at xr; returns its width.
static float valueBox(const string& t, float xr, float y, float h, float size, bool active) {
  Color c = col(active ? C_TEXT : C_DIM);
  float wl = gfx::textWidth("<", size, gfx::BOLD), wt = glyphTextWidth(t, size), wr = gfx::textWidth(">", size, gfx::BOLD);
  float total = wl + 10 + wt + 10 + wr, x = xr - total;
  textMid("<", x, y, h, size, c, gfx::BOLD);
  textMid(t, x + wl + 10, y, h, size, c);
  textMid(">", x + wl + 10 + wt + 10, y, h, size, c, gfx::BOLD);
  return total;
}
static vector<string> wrap(const string& text, float width, float size, gfx::Font f = gfx::REGULAR) {
  vector<string> out;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t e = text.find('\n', pos); if (e == string::npos) e = text.size();
    string para = text.substr(pos, e - pos), line;
    size_t i = 0;
    // Keep leading spaces (indented lines in messages).
    while (i < para.size() && para[i] == ' ') { line += ' '; i++; }
    string lead = line;
    while (i <= para.size()) {
      size_t sp = para.find(' ', i); if (sp == string::npos) sp = para.size();
      string word = para.substr(i, sp - i);
      string next = line.size() > lead.size() ? line + " " + word : line + word;
      if (line.size() > lead.size() && gfx::textWidth(next, size, f) > width) { out.push_back(line); line = word; }
      else line = next;
      i = sp + 1;
    }
    out.push_back(line);
    pos = e + 1;
  }
  return out;
}
static void panelHeader(float x, float y, const string& title, const string& note) {
  gfx::rect(x, y + 4, 10, 26, col(C_ACCENT), 2);
  float w = textMid(title, x + 20, y, 34, 26, col(C_TEXT), gfx::BOLD);
  if (!note.empty()) textMid(note, x + 20 + w + 12, y + 3, 34, 16, col(C_DIM));
}
static void scrollbar(float x, float y, float h, int total, int vis, int topIndex) {
  if (total <= vis) return;
  gfx::rect(x, y, 8, h, col(C_LINE), 4);
  float th = std::max(24.0f, roundf(h * vis / total));
  gfx::rect(x, y + roundf((h - th) * topIndex / (total - vis)), 8, th, col(C_DIM), 4);
}

/* ---------------- Screens ---------------- */
static const vector<string> BROWSER_BUTTONS = {"Settings", "Refresh", "Exit"};
static const int BROWSER_KEYS[] = {3, 2, 1};  // gamepad shortcut for each button: Y, X, B (glyph index)
static const int ROW_H = 40;
static const float LIST_HEAD = 26;  // column headings above the game list
// A row of tabs with small L / R shoulder-button caps either side: L and R cycle them.
static void drawTabBar(float cx, float cy, const vector<string>& labels, int sel, int focusTab) {  // focusTab: the tab the cursor is on, -1 none
  cx = roundf(cx); cy = roundf(cy);  // whole pixels: a 1 px border at a fractional x can vanish
  auto cap = [&](const char* label, float bx) {
    bx = roundf(bx);
    gfx::rect(bx, cy + 7, 28, 20, Color{0, 0, 0, 0}, 6, 1, col(C_DIM));
    textMid(label, bx + 28 / 2 - gfx::textWidth(label, 13, gfx::BOLD) / 2, cy + 7, 20, 13, col(C_DIM), gfx::BOLD);
  };
  cap("L", cx);
  float tx = cx + 28 + 10;
  for (size_t i = 0; i < labels.size(); i++) {
    bool on = (int)i == sel, focus = (int)i == focusTab;
    float w = roundf(gfx::textWidth(labels[i], 17) + 32);
    gfx::rect(tx, cy, w, 34, focus ? col(C_SEL) : on ? col(C_LINE) : Color{0, 0, 0, 0}, 16, 1, col(focus ? C_ACCENT : C_LINE));
    textMid(labels[i], tx + 16, cy, 34, 17, col(on ? C_TEXT : C_DIM));
    tx += w + 8;
  }
  cap("R", tx + 2);
}
static int maxRows() { return std::max(3, (int)((gfx::UI_H * 0.9 - 36 - 38 - 44 - 12 - 30 - 14 - LIST_HEAD) / ROW_H)); }

static void drawBrowser() {
  const float winW = 960, padX = 20, padY = 18, innerW = winW - 2 * padX;
  int VIS = std::max(1, std::min((int)roms.size(), maxRows()));
  if (selected < top) top = selected;
  if (selected >= top + VIS) top = selected - VIS + 1;
  if (top > std::max(0, (int)roms.size() - VIS)) top = std::max(0, (int)roms.size() - VIS);
  string empty;
  if (roms.empty()) {
    if (listView == 1) empty = "No favourites yet.\n\nWhile playing, open the menu (L + R + both stick clicks) and set Favourite to On.";
    else if (listView == 2) empty = "No recently played games yet.";
    else empty = "No games found in\n\n    " + romDir + "\n\nCopy PS2 disc images (.iso, .chd, .cso or .cue with its .bin) there, for example over FTP with OunceShell, then choose Refresh. The folder can be changed in Settings.";
  }
  float listInnerW = innerW - 16;  // the scrollbar's gutter always kept: the list never changes width
  vector<string> emptyLines = empty.empty() ? vector<string>() : wrap(empty, listInnerW - 32, 17);
  // One size whatever the tab or the number of games: as many rows as fit the screen.
  float listH = maxRows() * ROW_H + 12;
  float head = LIST_HEAD;
  float winH = padY * 2 + 34 + 12 + 36 + 10 + head + listH + 12 + 40;
  float x = (gfx::UI_W - winW) / 2, y = roundf((gfx::UI_H - winH) / 2);
  gfx::rect(x, y, winW, winH, col(C_PANEL), 14, 1, col(C_LINE));
  float cx = x + padX, cy = y + padY;
  panelHeader(cx, cy, "SW2PS2", roms.empty() ? "" : std::to_string(roms.size()) + (roms.size() == 1 ? " game" : " games"));
  cy += 34 + 12;
  const string sortHint = string(singleJoyConConnected ? "Press stick: " : "Press Left Stick: ") + ROM_SORT_LABELS[romSortByView[listView]];
  textMid(sortHint, cx + innerW - gfx::textWidth(sortHint, 16), cy, 36, 16, col(C_DIM));
  drawTabBar(cx, cy, LIST_VIEWS, listView, focusArea == "tabs" ? listView : -1);  // (L and R cycle them)
  cy += 36 + 10;
  // Column headings
  // Columns from the right: Time played, Last Played, Favourite.
  const float metadataW = 128;
  const float timeC = cx + listInnerW - 18 - metadataW / 2;
  const float playedC = timeC - metadataW, favC = playedC - metadataW;
  if (!roms.empty()) {
    gfx::text("Game", cx + 22, cy + 1, 14, col(C_DIM), gfx::BOLD);
    gfx::text("Time Played", timeC - gfx::textWidth("Time Played", 14, gfx::BOLD) / 2, cy + 1, 14, col(C_DIM), gfx::BOLD);
    gfx::text("Last Played", playedC - gfx::textWidth("Last Played", 14, gfx::BOLD) / 2, cy + 1, 14, col(C_DIM), gfx::BOLD);
    gfx::text("Favourite", favC - gfx::textWidth("Favourite", 14, gfx::BOLD) / 2, cy + 1, 14, col(C_DIM), gfx::BOLD);
  }
  cy += head;  // (kept with no games too: the window keeps its size)
  // List
  gfx::rect(cx, cy, listInnerW, listH, col(C_BG), 10, 1, col(C_LINE));
  if (roms.empty()) {
    float ly = cy + 16;
    for (auto& l : emptyLines) { gfx::text(l, cx + 16, ly, 17, col(C_TEXT)); ly += 17 * 1.45f; }
  } else {
    gfx::clip((int)cx, (int)cy, (int)listInnerW, (int)listH);
    for (int i = 0; i < VIS && top + i < (int)roms.size(); i++) {
      const Rom& r = roms[top + i];
      float ry = cy + 6 + i * ROW_H, rx = cx + 6, rw = listInnerW - 12;
      bool active = top + i == selected && focusArea == "list";
      if (active) { gfx::rect(rx, ry, rw, ROW_H, col(C_SEL), 6); gfx::rect(rx, ry, 4, ROW_H, col(C_ACCENT), 2); }
      bool fav = contains(favourites, r.name);
      string played = playLabel(r.name);
      if (!played.empty()) textMid(played, timeC - gfx::textWidth(played, 16) / 2, ry, ROW_H, 16, col(C_DIM));
      if (fav) {
        float tw = gfx::textWidth("Favourite", 14) + 16;
        gfx::rect(favC - tw / 2, ry + 9, tw, 22, gfx::rgb(0xf2c94c), 4);
        textMid("Favourite", favC - tw / 2 + 8, ry + 9, 22, 14, gfx::rgb(0x1a1300));
      }
      const string& date = lastPlayedLabel(r.name);
      if (!date.empty()) textMid(date, playedC - gfx::textWidth(date, 16) / 2, ry, ROW_H, 16, col(C_DIM));
      textMid(gfx::ellipsize(stripNes(r.name), favC - metadataW / 2 - rx - 28, 20), rx + 16, ry, ROW_H, 20, col(C_TEXT));
    }
    gfx::noClip();
    scrollbar(cx + listInnerW + 8, cy, listH, (int)roms.size(), VIS, top);
  }
  cy += listH + 12;
  // Buttons
  float bw = (innerW - 30) / 3;
  for (int i = 0; i < 3; i++) {
    bool on = focusArea == "buttons" && i == btnIndex;
    float bx = cx + 5 + i * (bw + 10);
    string label = i == 2 && exitArmed ? "Confirm exit" : BROWSER_BUTTONS[i];
    gfx::rect(bx, cy, bw, 40, col(on ? C_SEL : C_BG), 8, 1, col(on ? C_ACCENT : C_LINE));
    float tw = gfx::textWidth(label, 18), kw = 24, lx = bx + (bw - tw - kw - 8) / 2;
    gfx::rect(lx, cy + 8, kw, kw, col(C_LINE), kw / 2);  // shortcut button badge
    if (singleJoyConConnected) drawFaceGlyph(BROWSER_KEYS[i], lx + (kw - glyphWidth(17)) / 2, cy + 8 + (kw - 17 * 1.25f) / 2, 17, col(C_TEXT));
    else { const string k(1, "ABXY"[BROWSER_KEYS[i]]); float kt = gfx::textWidth(k, 15); textMid(k, lx + (kw - kt) / 2, cy + 8, kw, 15, col(C_TEXT)); }
    textMid(label, lx + kw + 8, cy, 40, 18, col(i == 2 && exitArmed ? C_ACCENT : C_TEXT));
  }
  gfx::text("v" APP_VERSION, x + winW - padX - gfx::textWidth("v" APP_VERSION, 14), y + padY + 10, 14, col(C_DIM));
}

static const int SET_ROW_H = ROW_H;  // as the game list's rows
static void drawFolderPicker() {
  const float winW = 960, padX = 20, padY = 18, rowH = 34;
  int total = pickRows();
  int vis = std::max(5, std::min(total, (int)((gfx::UI_H * 0.9 - 36 - 50 - 40 - 14) / rowH)));
  if (pickSel < pickTop) pickTop = pickSel;
  if (pickSel >= pickTop + vis) pickTop = pickSel - vis + 1;
  bool scroll = total > vis;
  float listW = winW - 2 * padX - (scroll ? 16 : 0), listH = vis * rowH + 12;
  float winH = padY * 2 + 34 + 12 + 30 + listH;
  float x = (gfx::UI_W - winW) / 2, y = roundf((gfx::UI_H - winH) / 2);
  gfx::rect(x, y, winW, winH, col(C_PANEL), 14, 1, col(C_LINE));
  float cx = x + padX, cy = y + padY;
  panelHeader(cx, cy, pickForBios ? "BIOS folder" : "ROM folder", "{A}: open   {B}: cancel");
  cy += 34 + 12;
  string shown = pickPath.rfind("sdmc:", 0) == 0 ? pickPath.substr(5) : pickPath;
  if (shown.empty()) shown = "/";
  string note = std::to_string(pickGames) + (pickGames == 1 ? " game here" : " games here");
  textMid(gfx::ellipsize(shown, winW - 2 * padX - 200, 18, gfx::MONO), cx + 4, cy, 26, 18, col(C_TEXT), gfx::MONO);
  textMid(note, cx + winW - 2 * padX - gfx::textWidth(note, 16) - 4, cy, 26, 16, col(C_DIM));
  cy += 30;
  gfx::rect(cx, cy, listW, listH, col(C_BG), 10, 1, col(C_LINE));
  for (int k = 0; k < vis && pickTop + k < total; k++) {
    int i = pickTop + k;
    bool active = i == pickSel;
    float rx = cx + 6, ry = cy + 6 + k * rowH, rw = listW - 12;
    if (active) { gfx::rect(rx, ry, rw, rowH, col(C_SEL), 6); gfx::rect(rx, ry, 4, rowH, col(C_ACCENT), 2); }
    string label; Color c = col(C_TEXT);
    int up = pickAtRoot() ? 0 : 1;
    if (i == 0) { label = "Use this folder"; c = gfx::rgb(0x6ee26e); }
    else if (up && i == 1) label = ".. (up one level)";
    else label = pickDirs[i - 1 - up] + "/";
    textMid(gfx::ellipsize(label, rw - 32, 18), rx + 16, ry, rowH, 18, c, i == 0 ? gfx::BOLD : gfx::REGULAR);
  }
  scrollbar(cx + listW + 8, cy, listH, total, vis, pickTop);
}
static void drawRemap() {
  const float winW = 640, padX = 20, padY = 18, innerW = winW - 2 * padX, rowH = 34;
  const bool done = remapStep >= 16, connected = padConnected(remapWho);
  const float listH = 16 * rowH + 12;
  const float winH = padY * 2 + 34 + 12 + 30 + listH + 12 + 40;
  const float x = (gfx::UI_W - winW) / 2, y = roundf((gfx::UI_H - winH) / 2);
  gfx::rect(x, y, winW, winH, col(C_PANEL), 14, 1, col(C_LINE));
  float cx = x + padX, cy = y + padY;
  panelHeader(cx, cy, "Player " + std::to_string(remapWho + 1) + " buttons", remapForGame ? "for this game" : "for all games");
  cy += 34 + 12;
  string hint;
  if (!connected) hint = "No controller for player " + std::to_string(remapWho + 1) + " (connect one with Pair controllers). {B}: back";
  else if (!done && remapWaitRelease) hint = "Let go of all buttons...";
  else if (!done) hint = "Press the button for " + string(PS2_BUTTONS[remapStep].name) + ", or wait " +
                         std::to_string(std::max(1, (int)ceil((remapDeadline - nowMs()) / 1000))) + " s to leave it unmapped";
  else hint = "Save this layout?";
  textMid(gfx::ellipsize(hint, innerW, 18), cx, cy, 26, 18, col(C_TEXT));
  cy += 30;
  gfx::rect(cx, cy, innerW, listH, col(C_BG), 10, 1, col(C_LINE));
  for (int i = 0; i < 16; i++) {
    const float rx = cx + 6, ry = cy + 6 + i * rowH, rw = innerW - 12;
    const bool current = connected && !done && i == remapStep;
    if (current) { gfx::rect(rx, ry, rw, rowH, col(C_SEL), 6); gfx::rect(rx, ry, 4, rowH, col(C_ACCENT), 2); }
    textMid(PS2_BUTTONS[i].name, rx + 16, ry, rowH, 18, col(C_TEXT));
    const u32 style = connected ? padGetStyleSet(padStates[remapWho]) : 0;
    string v = i < remapStep ? (remapNew[i] >= 0 ? inputName(remapNew[i], style) : "Not mapped") : current ? "..." : "";
    if (!v.empty()) { float w = gfx::textWidth(v, 18); textMid(v, rx + rw - 12 - w, ry, rowH, 18, col(i < remapStep && remapNew[i] < 0 ? C_DIM : C_TEXT)); }
  }
  cy += listH + 12;
  static const char* OPTS[3] = {"Save", "Start again", "Cancel"};
  const float bw = (innerW - 20) / 3;
  for (int i = 0; i < 3; i++) {
    const bool on = done && i == remapPick;
    const float bx = cx + i * (bw + 10);
    gfx::rect(bx, cy, bw, 40, col(on ? C_SEL : C_BG), 8, 1, col(on ? C_ACCENT : C_LINE));
    const float tw = gfx::textWidth(OPTS[i], 18);
    textMid(OPTS[i], bx + (bw - tw) / 2, cy, 40, 18, col(done ? C_TEXT : C_DIM));
  }
}
static void drawSettings() {
  const float winW = 640, padX = 20, padY = 18, innerW = winW - 2 * padX;
  int n = (int)SETTINGS_ITEMS.size(), total = n + 1;
  // One size for every category (the largest, advanced shown); more rows than fit the screen scroll.
  int vis = std::max(5, std::min(std::max(total, settingsFixedRows()), (int)((gfx::UI_H * 0.9 - 36 - 50 - 14 - 46) / SET_ROW_H)));
  if (settingsIndex >= 0 && settingsIndex < settingsTop) settingsTop = settingsIndex;
  if (settingsIndex < 0) settingsTop = 0;
  if (settingsIndex >= settingsTop + vis) settingsTop = settingsIndex - vis + 1;
  float listW = innerW - 16, listH = vis * SET_ROW_H + 12;  // (scrollbar gutter always kept)
  float winH = padY * 2 + 34 + 12 + 36 + 10 + listH;
  float x = (gfx::UI_W - winW) / 2, y = roundf((gfx::UI_H - winH) / 2);
  gfx::rect(x, y, winW, winH, col(C_PANEL), 14, 1, col(C_LINE));
  float cx = x + padX, cy = y + padY;
  panelHeader(cx, cy, "Settings", "for all games");
  cy += 34 + 12;
  drawTabBar(cx, cy, SETTINGS_CATS, settingsCat, settingsIndex < 0 ? settingsCat : -1);
  cy += 36 + 10;
  gfx::rect(cx, cy, listW, listH, col(C_BG), 10, 1, col(C_LINE));
  for (int k = 0; k < vis; k++) {
    int i = settingsTop + k;
    if (i >= total) break;
    bool active = i == settingsIndex;
    float rx = cx + 6, ry = cy + 6 + k * SET_ROW_H, rw = listW - 12;
    if (active) { gfx::rect(rx, ry, rw, SET_ROW_H, col(C_SEL), 6); gfx::rect(rx, ry, 4, SET_ROW_H, col(C_ACCENT), 2); }
    if (i < n) {
      textMid(SETTINGS_ITEMS[i].label, rx + 16, ry, SET_ROW_H, 20, col(C_TEXT));
      vector<string> ch = SETTINGS_ITEMS[i].choices();
      valueBox(ch[SETTINGS_ITEMS[i].get()], rx + rw - 12, ry, SET_ROW_H, 20, active);
    } else textMid("Back", rx + 16, ry, SET_ROW_H, 20, col(C_TEXT));
  }
  scrollbar(cx + listW + 8, cy, listH, total, vis, settingsTop);
}

static void drawStatsBar() {
  if (statsLevel <= 0 || !running) return;
  string t = statsText.empty() ? " " : statsText;
  float tw = gfx::textWidth(t, 18, gfx::MONO), gw = statsLevel == 3 ? GRAPH_W + 12 : 0;
  float h = std::max(18 * LH, statsLevel == 3 ? (float)GRAPH_H : 0) + 8;
  gfx::rect(16, 12, tw + gw + 20, h, gfx::Color{0, 0, 0, 0.6f}, 6);
  textMid(t, 26, 12, h, 18, gfx::rgb(0x9dfc9d), gfx::MONO);
  if (statsLevel == 3 && graphImage) gfx::imageDraw(graphImage, 26 + tw + 12, 12 + (h - GRAPH_H) / 2, GRAPH_W, GRAPH_H);
}

static void drawGame(bool dim) {
  if (running) gfx::pictureDraw(picture, picX, picY, picW, picH, picCX, picCY, picCW, picCH);
  drawStatsBar();
  if (autoTest && !autoWaiting && running && !autoLabel.empty()) {
    string t = gfx::ellipsize(autoLabel, gfx::UI_W - 72, 18);
    float w = gfx::textWidth(t, 18), h = 18 * LH + 8;
    gfx::rect((gfx::UI_W - w) / 2 - 10, gfx::UI_H - 16 - h, w + 20, h, gfx::Color{0, 0, 0, 0.6f}, 6);
    textMid(t, (gfx::UI_W - w) / 2, gfx::UI_H - 16 - h, h, 18, gfx::rgb(0xffd479));
  }
  if (hintUntil && !dim) {
    const char* hint = singleJoyConConnected ? "SL + SR + stick press (or L + R + L3 + R3): Menu" : "L + R + L3 + R3: Menu";
    float w = gfx::textWidth(hint, 16);
    gfx::text(hint, gfx::UI_W - 16 - w, gfx::UI_H - 12 - 20, 16, gfx::rgb(0x8891a4));
  }
}

static void drawMenu() {
  if (dimInMenu) gfx::rect(0, 0, gfx::UI_W, gfx::UI_H, gfx::Color{0, 0, 0, 0.6f});
  // The game list's and Settings' layout: accent-bar title, tabs, rows of ROW_H in a list box.
  // The tabbed menu is as wide as Settings and keeps one size (its largest category); other menus
  // (cheats) fit their rows.
  const float panelW = menuHasTabs ? 640 : 460, padX = 20, padY = 18, innerW = panelW - 2 * padX;
  const float tabsH = menuHasTabs ? 36 + 10 : 0;
  int n = (int)menuItems.size();
  const int fit = (int)((gfx::UI_H * 0.9f - padY * 2 - 34 - 12 - tabsH - 12) / ROW_H);
  int vis = std::max(4, std::min(menuHasTabs ? std::max(n, menuFixedRows()) : n, fit));
  if (menuIndex >= 0 && menuIndex < menuTop) menuTop = menuIndex;
  if (menuIndex < 0) menuTop = 0;
  if (menuIndex >= menuTop + vis) menuTop = menuIndex - vis + 1;
  if (menuTop > n - vis) menuTop = std::max(0, n - vis);
  const bool gutter = menuHasTabs || n > vis;  // (tabbed: the scrollbar's gutter always kept)
  float listW = innerW - (gutter ? 16 : 0), listH = vis * ROW_H + 12;
  float h = padY * 2 + 34 + 12 + tabsH + listH;
  float x = (gfx::UI_W - panelW) / 2, y = roundf((gfx::UI_H - h) / 2);
  gfx::rect(x, y, panelW, h, col(C_PANEL), 14, 1, col(C_LINE));
  float cx = x + padX, cy = y + padY;
  panelHeader(cx, cy, gfx::ellipsize(menuTitle, innerW - 20, 26, gfx::BOLD), "");
  cy += 34 + 12;
  if (menuHasTabs) { drawTabBar(cx, cy, MENU_CATS, menuCat, menuIndex < 0 ? menuCat : -1); cy += tabsH; }
  gfx::rect(cx, cy, listW, listH, col(C_BG), 10, 1, col(C_LINE));
  for (int k = 0; k < vis && menuTop + k < n; k++) {
    int i = menuTop + k;
    bool active = i == menuIndex;
    float rx = cx + 6, ry = cy + 6 + k * ROW_H, rw = listW - 12;
    if (active) { gfx::rect(rx, ry, rw, ROW_H, col(C_SEL), 6); gfx::rect(rx, ry, 4, ROW_H, col(C_ACCENT), 2); }
    const MenuItem& m = menuItems[i];
    float vw = 0;
    if (m.option()) { vector<string> ch = m.choices(); int v = m.get(); vw = valueBox(ch[std::max(0, std::min((int)ch.size() - 1, v))], rx + rw - 12, ry, ROW_H, 20, active) + 16; }
    textMid(gfx::ellipsize(m.label, rw - 28 - vw, 20), rx + 16, ry, ROW_H, 20, col(C_TEXT));
  }
  if (n > vis) scrollbar(cx + listW + 8, cy, listH, n, vis, menuTop);
}

static void drawMessage() {
  gfx::rect(0, 0, gfx::UI_W, gfx::UI_H, gfx::Color{0, 0, 0, 0.75f});
  vector<string> lines = wrap(messageText, 700, 22);
  float lh = 22 * 1.45f, w = 760, h = 30 * 2 + lines.size() * lh + 18 + 18 * LH;
  float x = (gfx::UI_W - w) / 2, y = roundf((gfx::UI_H - h) / 2);
  gfx::rect(x, y, w, h, col(C_PANEL), 14, 1, col(C_LINE));
  float cy = y + 30;
  for (auto& l : lines) { textMid(l, x + 30, cy, lh, 22, col(C_TEXT)); cy += lh; }
  glyphText("Press {A} or {B} to continue", x + 30, cy + 18 - 2, 18, col(C_DIM));
}

static void drawToast() {
  if (!toastUntil) return;
  float w = glyphTextWidth(toastText, 20) + 36, h = 20 * LH + 20;
  float x = roundf((gfx::UI_W - w) / 2), y = gfx::UI_H - 28 - h;
  gfx::rect(x, y, w, h, gfx::rgb(0x0a0c12, 0.85f), 8);
  textMid(toastText, x + 18, y, h, 20, col(C_TEXT));
}

/* ---------------- Main loop ---------------- */

// One real (shown) SNES frame with input p/p2, run-ahead applied: the real frame
// runs without drawing and keeps its audio; then `ahead` frames run from a
// snapshot with the same input, the last one drawn, audio discarded, and the
// snapshot is restored. The picture shows the game `ahead` frames early.
// One real (shown) SNES frame. With run-ahead, the real frame runs without
// drawing (its audio kept) and a second emulator instance runs `ahead` frames
// from it with the same input; the picture comes from that instance.
static bool emulateShown(int p, int p2, int ahead, bool keepAudio) {
  if (sw2ax_game_booted && sw2ax_game_booted()) bootedFrames++; else bootedFrames = 0;
  if (pendingLoadSlot && !stateLoadReady()) {
    if (++pendingLoadTries > 3600) { record("state: slot %d not loaded: the game had not started after 60 s", pendingLoadSlot); showToast("State load failed"); pendingLoadSlot = 0; }
  } else if (pendingLoadSlot) {
    if (C->state_load(pendingLoadSize)) {
      C->audio_reset();
      record("state: slot %d loaded after %d frames", pendingLoadSlot, pendingLoadTries);
      showToast("Loaded slot " + std::to_string(pendingLoadSlot));
      pendingLoadSlot = 0;
      if (autoTest && autoAwaitingState) {
        autoActivityNext = 0;  // refresh during warmup, away from the measured window
        autoAwaitingState = false;
        autoRunStart = statsAt = nowMs();
        autoTestEnd = autoRunStart + AUTO_LOW_WARMUP_MS + autoMeasureSeconds * 1000;
        sessionLog = emuCount = 0;
        workMs = audioInCount = 0;
        resetAudioMeasurements();
        pendEma = AUDIO_TARGET_FRAMES[audioBuffer];
        audio::prefill(AUDIO_TARGET_FRAMES[audioBuffer]);
        autoFrameGaps.clear();
        autoGameIntervals.clear();
        autoPrevSwap = 0;
        autoLowWindowStarted = false;
        autoSkipFirstGameInterval = false;
        autoGameShotFrames = 0;
        autoGameShotPriming = true;
        if (autoMeasureVulkan && sw2ax_vk_test_counters) sw2ax_vk_test_counters(autoVulkanStart);
        if (autoTracePassEnds && sw2ax_vk_trace_pass_ends) sw2ax_vk_trace_pass_ends(true);
        if (autoTraceVuNotifications && sw2ax_trace_vu_notifications) sw2ax_trace_vu_notifications(true);
        if (autoTraceVifCopies && sw2ax_trace_vif_copies) sw2ax_trace_vif_copies(true);
        if (autoTraceFrameWork && sw2ax_trace_frame_work) sw2ax_trace_frame_work(true);
        if (autoTraceSpr && sw2ax_trace_spr) sw2ax_trace_spr(true);
#ifdef SW2_VK
        if (autoMeasureHandoff) vkp::measureHandoff(true);
#endif
        for (double& stage : phase) stage = 0;
        phaseN = 0;
        record("autotest: measuring %.1f s after %.1f s warmup from loaded state", autoMeasureSeconds, AUTO_LOW_WARMUP_MS / 1000);
        if (autoProfileAfterLoad) {
          autoProfileAfterLoad = false;
          profStart();
        }
      }
    }
    else if (++pendingLoadTries > 4800) { record("state: slot %d refused for 20 s", pendingLoadSlot); showToast("State load failed"); pendingLoadSlot = 0; }
  }
  if (!keepAudio) C->set_audio_discard(1);
  bool ok = C->frame(p, p2, ahead > 0 ? 0 : 1) >= 0;
  C->set_audio_discard(0);
  int n = C->audio_take();
  audioInCount += n;
  if (n && keepAudio) audio::push(C->audio(), n);
  if (ok && ahead > 0 && C->runahead(p, p2, ahead) < 0) { C->reconvert(); }
  return ok;
}

static bool otherPlayersMenuCombo = false;  // players 3-4 (not PS2 ports) holding the menu combo
static void runFrame(u64 bits, u64 bits2, bool behindMenu) {
  double t0 = nowMs();
  if (!behindMenu) {
    bool combo = menuCombo(bits) || menuCombo(bits2) || otherPlayersMenuCombo;  // any player opens the menu
    if (combo && !comboLatch) { comboLatch = true; lastBits = bits; menuCat = 0; openMenu(); return; }  // (opens on Game)
    if (!combo) comboLatch = false;
  }
  // Fast-forward (ZR hold or toggle) and rewind (hold ZL); both together open
  // the menu, so each acts only while the other is not held.
  bool both = menuCombo(bits);
  bool zr = false, zl = false;  // ZL/ZR are the PS2's L2/R2: no fast-forward or rewind here
  (void)both;
  if (ffMode == 2 && zr && !ffPrevHeld) ffLatched = !ffLatched;
  ffPrevHeld = zr;
  if (ffMode != 2) ffLatched = false;
  bool wasFast = fastForward;
  fastForward = ffMode == 1 ? zr : ffMode == 2 ? ffLatched : false;
  if (wasFast != fastForward && !fastForward) emuAcc = 0;
  bool wasRewinding = rewinding;
  rewinding = rewindOn && zl && snapsReady;
  if (wasRewinding && !rewinding) C->audio_reset();  // resume sound cleanly from the rewound point

  if (rewinding) {
    // Step back one state per refresh and show it: the stepped-back state is
    // run for one frame (audio discarded) to draw its picture, then restored.
    if (rewind_count() > 0 && rewind_pop()) {
      C->set_audio_discard(1);
      C->frame(0, 0, 1);
      C->audio_take();
      C->set_audio_discard(0);
      rewind_reload();
      showFrame();
      frameDrawn = true;
    }
    workMs += nowMs() - t0;
    return;
  }

  // How many SNES frames this refresh: one at 60 Hz (locked to the display, audio
  // stretched by the rate difference); by the clock at 120 Hz / VRR.
  // At 120 Hz / VRR the display does not tick at the NES rate, so emulation
  // follows the clock: the exact time since the previous refresh (no
  // smoothing; with VRR long and short refreshes alternate and a smoothed
  // average ran games too fast).
  // 120 Hz / VRR: the main loop already waits for each SNES frame's time slot
  // (see vrrWait), so exactly one frame per loop. A 60 Hz display that is
  // not ticking near 60 Hz falls back to following the clock.
  int due = 1;
  bool hz120 = !highRefresh() && (tickMs < 15.5 || tickMs > 18 || fabs(NES_RATE - 59.94) > 1);  // PAL games follow the clock
  if (hz120) {
    emuAcc += std::min(loopGapMs, 50.0) * NES_RATE / 1000;
    due = (int)emuAcc; emuAcc -= due;
    if (due > 3) { due = 3; emuAcc = 0; }
  }
  if (fastForward) {
    ffAcc += FF_FACTOR[ffSpeed] * (hz120 ? std::min(loopGapMs, 50.0) * NES_RATE / 1000 : 1);  // per shown frame
    due = (int)ffAcc; ffAcc -= due;
  }
  if (due <= 0) { workMs += nowMs() - t0; return; }

  int p = 0, p2 = 0;
  for (int i = 0; i < due; i++) {
    turbo++;
    setPs2Pad(0, bits, behindMenu); setPs2Pad(1, bits2, behindMenu);
    setPs2Pad(2, padStates[2] ? padGetButtons(padStates[2]) : 0, behindMenu); setPs2Pad(3, padStates[3] ? padGetButtons(padStates[3]) : 0, behindMenu);  // (the core reads them with the Multitap)
    if (verboseLog()) {  // debug: each player's PS2 buttons as sent, once a second while any is pressed
      static double inputLogAt = 0;
      const double tn = nowMs();
      if (tn >= inputLogAt && (ps2Input[0].buttons | ps2Input[1].buttons | ps2Input[2].buttons | ps2Input[3].buttons)) {
        inputLogAt = tn + 1000;
        record("input: p1 %04x p2 %04x p3 %04x p4 %04x (sticks p2 %d,%d)", ps2Input[0].buttons, ps2Input[1].buttons, ps2Input[2].buttons, ps2Input[3].buttons, ps2Input[1].lx, ps2Input[1].ly);
      }
    }
    bool last = i == due - 1;
    bool ok;
    if (last) ok = emulateShown(p, p2, fastForward ? 0 : runAhead, !fastForward);
    else { C->set_audio_discard(1); ok = C->frame(p, p2, 0) >= 0; C->audio_take(); C->set_audio_discard(0); }
    if (!ok) { quitGame(); showMessage("Something went wrong:\nEmulation stopped.", [] { mode = "browser"; }); return; }
    emuCount++;
    if (autoTest && !autoAwaitingState) autoMeasuredFrames++;
    // Rewind history: every frame.
    if (rewindOn && snapsReady) rewind_push();
  }
  {
    // Audio rate control (every frame): the queued amount, measured to the
    // sample and smoothed, is steered to the target by a PI loop around the
    // base ratio (60 Hz: display/NES rate difference; 120 Hz: 1, emulation
    // follows the clock). Corrections stay within +-0.5%, inaudible as pitch.
    double target = AUDIO_TARGET_FRAMES[audioBuffer];
    const double queued = audio::queuedFrames();
    pendEma += (queued - pendEma) * 0.05;
    double err = (target - pendEma) / target;
    audioInteg = std::max(-0.004, std::min(0.004, audioInteg + err * 0.00002));
    double base = (hz120 || highRefresh()) ? 1.0 : NES_RATE / std::max(59.5, std::min(60.5, 1000.0 / tickMs));
    double r = base * (1 + std::max(-0.005, std::min(0.005, err * 0.004 + audioInteg)));
    if (++ratioTicks >= 4) { ratioTicks = 0; setAudioRatio(round(r * 1e5) / 1e5); }
    // A sustained shortage is larger than the drift trim can cover. Ask SoundTouch
    // to extend playback while preserving pitch and leave the emulation clock alone.
    if (adaptiveAudio && &sw2_output_tempo && &sw2_audio_stretch && sw2_audio_stretch && !fastForward) {
      const double ratio = std::clamp(pendEma / target, 0.80, 1.0);
      const double targetTempo = ratio < 0.95 ? ratio : 1.0;
      const double rate = (targetTempo < outputTempo) ? 2.5 : 0.8;
      setOutputTempo(std::clamp(outputTempo + (targetTempo - outputTempo) * rate * std::min(loopGapMs, 50.0) / 1000.0, 0.80, 1.0));
    }
    minAudioQueue = std::min(minAudioQueue, queued);
    // Refill only on near-empty buffer with a small cushion to avoid silence blasts.
    if (queued < 100) {
      audioRefills++;
      audio::prefill(240);
    }
  }
  emuFrames += 1;
  showFrame();
  frameDrawn = true;
  double t1 = nowMs();
  workMs += t1 - t0;
  // Battery save: every 4 s, write when the save RAM changed.
  if (running && running->battery && t1 - running->sramCheck >= 4000) { running->sramCheck = t1; saveSram(false); }
}

static string dispLabel() {
  char b[48];
  if (dispKnown) snprintf(b, sizeof b, "%u Hz%s", dispHz, dispVrr ? " VRR" : "");
  else snprintf(b, sizeof b, "%.0f Hz", 1000.0 / tickMs);
  return b;
}
// CPU load of the core this thread runs on, from the kernel's idle-tick
// counter (all work on that core, not only ours); -1 if unavailable.
static u64 idleLast = 0, tickLast = 0;
static double coreLoad() {
  u64 idle = 0, tick = armGetSystemTick();
  if (R_FAILED(svcGetInfo(&idle, InfoType_IdleTickCount, INVALID_HANDLE, svcGetCurrentProcessorNumber()))) return -1;
  double load = -1;
  if (tickLast && tick > tickLast && idle >= idleLast) load = 1.0 - (double)(idle - idleLast) / (double)(tick - tickLast);
  idleLast = idle; tickLast = tick;
  return load < 0 ? -1 : std::min(1.0, load);
}
// Per-thread CPU time of the core's threads (rthreads registry), once a second
// in the log: "#index@core+entry offset: percent". The offset is the thread
// function's address relative to retro_run (resolve with nm on the .elf).
extern "C" int sw2_thread_info(int i, unsigned long long* ticks, int* core, void** entry);
extern "C" int sw2_thread_total(void);
extern "C" unsigned long long sw2_gs_stats[16];
// The ARMSX2 core's own counters (native/armsx2_compat.cpp).
extern "C" __attribute__((weak)) float sw2ax_fps();
extern "C" __attribute__((weak)) void sw2ax_register_exit_cleanup(int traces);
extern "C" __attribute__((weak)) void sw2ax_crash_watch(int);
extern "C" __attribute__((weak)) void sw2ax_iop_stats(char*, size_t);
extern "C" __attribute__((weak)) void sw2ax_event_stats(unsigned long long*);
extern "C" __attribute__((weak)) void sw2ax_event_sources(char*, size_t);
extern bool sw2_trace_ee_events __attribute__((weak));
extern "C" __attribute__((weak)) void sw2ax_memory(unsigned*, unsigned*, unsigned*);
extern "C" __attribute__((weak)) void sw2ax_frame_counts(unsigned long long*, unsigned long long*, unsigned long long*);
extern "C" __attribute__((weak)) int sw2_smc_off;
extern "C" int sw2_mfifo_precise_notify __attribute__((weak));
extern "C" int sw2_vu_kick_light __attribute__((weak));
extern "C" int sw2_iop_ee_nofence __attribute__((weak));
extern "C" int sw2_no_gs_hooks __attribute__((weak));
extern "C" __attribute__((weak)) int sw2_softprot;
extern "C" __attribute__((weak)) unsigned long long sw2_softprot_stats[4];
extern int sw2_iop_thread __attribute__((weak));
extern int sw2_iop_lag __attribute__((weak));
extern int sw2_gs_back __attribute__((weak));
extern int sw2_no_pacing __attribute__((weak));
extern int sw2_db_effects __attribute__((weak));
extern int sw2_shader_log __attribute__((weak));
extern "C" __attribute__((weak)) int sw2_prof_gs;
extern "C" __attribute__((weak)) void sw2ax_classify_batch(const unsigned long long*, const u32*, unsigned*, size_t);
extern "C" __attribute__((weak)) void sw2ax_exit_trace(const char*);
extern "C" __attribute__((weak)) void sw2ax_list_threads(const char*);
extern "C" __attribute__((weak)) float sw2ax_internal_fps();
extern "C" __attribute__((weak)) float sw2ax_ee_usage();
extern "C" __attribute__((weak)) float sw2ax_vu_usage();
extern "C" __attribute__((weak)) float sw2ax_gs_usage();
extern "C" void retro_run(void);
static void logThreads() {
  static unsigned long long prev[64], mainPrev; static u64 tickPrev;
  u64 now = armGetSystemTick(), mainTicks = 0;
  double dt = tickPrev ? (double)(now - tickPrev) : 0;
  svcGetInfo(&mainTicks, InfoType_ThreadTickCount, CUR_THREAD_HANDLE, (u64)-1);
  string line = "threads:";
  char b[96];
  if (dt > 0) { snprintf(b, sizeof b, " main@%d:%.0f%%", svcGetCurrentProcessorNumber(), (mainTicks - mainPrev) * 100.0 / dt); line += b; }
  mainPrev = mainTicks;
  for (int i = 0; i < sw2_thread_total() && i < 64; i++) {
    unsigned long long ticks; int core; void* entry;
    if (!sw2_thread_info(i, &ticks, &core, &entry)) continue;
    double pct = dt > 0 ? (ticks - prev[i]) * 100.0 / dt : 0;
    prev[i] = ticks;
    if (pct >= 1) { snprintf(b, sizeof b, " #%d@%d%+ld:%.0f%%", i, core, (long)((char*)entry - (char*)&retro_run), pct); line += b; }
  }
  tickPrev = now;
  if (sw2ax_ee_usage) { snprintf(b, sizeof b, " ARMSX2 EE:%.0f%% VU:%.0f%% GS:%.0f%%", sw2ax_ee_usage(), sw2ax_vu_usage(), sw2ax_gs_usage()); line += b; }
  if (sw2ax_memory) { unsigned u, t, hf; sw2ax_memory(&u, &t, &hf); snprintf(b, sizeof b, " mem %u/%u MB heap free %u MB", u, t, hf); line += b; }
  if (sw2ax_iop_stats) { char ib[320]; sw2ax_iop_stats(ib, sizeof ib); line += ib; }
  if (sw2ax_event_stats) { unsigned long long ev[4]; sw2ax_event_stats(ev); char eb[120]; snprintf(eb, sizeof eb, " | event tests %llu (counter %llu, interrupt %llu, neither %llu)", ev[0], ev[1], ev[2], ev[3]); line += eb; }
  if (sw2ax_frame_counts) {  // per second: game frames shown, screen refreshes that repeated one, frames never shown
    static unsigned long long ps, pr, pd; unsigned long long cs, cr, cd; sw2ax_frame_counts(&cs, &cr, &cd);
    snprintf(b, sizeof b, " frames new:%llu repeat:%llu dropped:%llu", cs - ps, cr - pr, cd - pd); line += b; ps = cs; pr = cr; pd = cd;
  }
  if (verboseLog() && sessionLog < 120) record("  %s", line.c_str());  // (thread detail: test runs only)
  {  // The core's wait timers (pcsx2/MTGS.c), in ms per second of wall time.
    static unsigned long long gp[16];
    auto ms = [&](int i) { return armTicksToNs(sw2_gs_stats[i] - gp[i]) / 1e6; };
    if (verboseLog() && sessionLog < 120) record("  waits per second: EE on GS %.0f ms (%llu times), EE at frame gate %.0f ms, GS idle %.0f ms, GS on VU1 %.0f ms, GL buffer fences %.0f ms, GL texture/finish %.0f ms, event tests %llu", ms(0), sw2_gs_stats[1] - gp[1], ms(4), ms(2), ms(3), ms(5), ms(6), sw2_gs_stats[9] - gp[9]);
    memcpy(gp, sw2_gs_stats, sizeof gp);
  }
}
/* ---------------- CPU speed auto-tuning ----------------
 * The best CPU speed for a game is the highest one that still runs it at full speed: lower
 * speeds cost the game frames, higher ones run it slow (and the audio stutters). Games start at
 * their known setting (measured list, community list, else 100%); while one plays, emulation
 * speed is checked every second, and when it stays clearly below full speed the game's CPU speed
 * goes down one step (4 of the last 5 seconds below 98.5%), live (the core recompiles: a short hitch), and is remembered for the next
 * start. Seconds below 50% (loading stalls) and the first seconds after a start, a state load or
 * a change don't count. A per-game choice in the in-game menu turns it off for that game. */
extern "C" __attribute__((weak)) float sw2ax_speed();
static int autoTuneTest = -1;  // test runs: "autotune": true shows the notice (default off)
// A game held below full speed starves the audio (rate control absorbs only ~0.5%), so it stutters.
// Tell the player once per session which settings help; change nothing ourselves -- a lower CPU
// speed can cost the game frames, so the trade is theirs to make.
static void slowdownNotice(double t) {
  if (!running || !sw2ax_speed || mode != "running" || slowNoticeShown) return;
  if (autoTest && autoTuneTest != 1) return;
  if (t < tuneQuietUntil) return;
  const double speed = sw2ax_speed() / 100.0;
  if (speed < 0.5) return;  // loading or a stall, not a steady shortfall
  tuneWin.push_back(speed); if (tuneWin.size() > 5) tuneWin.pop_front();
  if (tuneWin.size() < 5) return;
  int slow = 0; double sum = 0; for (double s : tuneWin) { sum += s; if (s < 0.985) slow++; }
  if (slow < 4 || sum / tuneWin.size() >= 0.98) return;
  slowNoticeShown = true;
  record("slowdown: %s at %.1f%% of full speed, notice shown", running->name.c_str(), 100 * sum / tuneWin.size());
  showToast(renderScale >= 4 ? "Slowdown detected. A lower resolution (Advanced options), CPU speed or more CPU cycle skip in the game menu may help"
                              : "Slowdown detected. A lower CPU speed or more CPU cycle skip in the game menu may help", 7000);
}
static void updateStats(double t) {
  if (!running || t - statsAt < 1000) return;
  slowdownNotice(t);
  logThreads();
  double secs = (t - statsAt) / 1000, avg, low;
  videoFps(avg, low);
  char buf[200];
  double gameFps = 0, emuFps = emuCount / secs;
  {  // The game's own frame rate: vsyncs with draws into the displayed buffer when the game shows
     // any (it renders and copies), else vsyncs with display registers written, else buffer flips.
    static unsigned long long blitPrev, regPrev, flipPrev;
    const unsigned long long blits = sw2_gs_stats[11] - blitPrev, regs = sw2_gs_stats[12] - regPrev, flipsN = sw2_gs_stats[10] - flipPrev;
    blitPrev = sw2_gs_stats[11]; regPrev = sw2_gs_stats[12]; flipPrev = sw2_gs_stats[10];
    gameFps = (blits ? blits : regs ? regs : flipsN) / secs;
    if (sw2ax_fps) { emuFps = sw2ax_fps(); gameFps = sw2ax_internal_fps(); }  // ARMSX2 core: its own counters
  }
  if (statsLevel == 1) snprintf(buf, sizeof buf, "%.0f fps", gameFps);
  else {
    double cpu = coreLoad(), gpu = gfx::gpuMs();
    char cpuS[24], gpuS[24];
    if (cpu >= 0) snprintf(cpuS, sizeof cpuS, "%d%%", (int)lround(cpu * 100)); else snprintf(cpuS, sizeof cpuS, "%d%%", (int)lround(100 * workMs / (t - statsAt)));
    if (gpu >= 0) snprintf(gpuS, sizeof gpuS, "%d%% (%.1f ms)", (int)lround(100 * gpu / tickMs), gpu); else snprintf(gpuS, sizeof gpuS, "n/a");
    // Players see the game's frame rate and the load; test runs also log the emulator's own rate
    // and the display's average and 1% low (tests/ read them from log.txt).
    if (autoTest && !autoPlayerStats) snprintf(buf, sizeof buf, "Game %.0f fps   Emu %.1f fps   Video avg %.1f fps   1%% low %.1f fps   CPU %s   GPU %s   %s", gameFps, emuFps, avg, low, cpuS, gpuS, dispLabel().c_str());
    else snprintf(buf, sizeof buf, "Game %.0f fps   CPU %s   GPU %s   %s", gameFps, cpuS, gpuS, dispLabel().c_str());
  }
  // The log: test runs get every second in detail (tests/ read it); players a short line every
  // 10 s for a game's first 2 minutes, enough for a bug report.
  if (verboseLog() && sessionLog < 120) { sessionLog++; record("t%d %s | wait %.2f block %.2f emu %.2f draw %.2f swap %.2f ms, audio q %.0f ms ratio %.4f in %.0f/s", sessionLog, buf,
                                               phase[0] / phaseN, phase[1] / phaseN, phase[2] / phaseN, phase[3] / phaseN, phase[4] / phaseN, pendEma / 48.0, audioRatio, audioInCount / secs); audioInCount = 0; }
  else if (!verboseLog() && sessionLog < 120) {
    if (++sessionLog % 10 == 0) record("t%d game %.0f fps, emu %.1f fps (%.0f%%), audio %.0f ms", sessionLog, gameFps, emuFps, sw2ax_speed ? sw2ax_speed() : 0.0, pendEma / 48.0);
    audioInCount = 0;
  }
  for (double& x : phase) x = 0; phaseN = 0;
  swapAcc = waitAcc = swapMax = 0; swapN = 0;
  statsText = buf;
  emuCount = 0; workMs = 0; statsAt = t;
}

// Probe (autotest {"probe": true}): which memory-aliasing system calls this
// process may use. A fastmem window needs one page of guest RAM visible at
// several addresses. Calls are only made when the loader hints the syscall
// as permitted (an unpermitted one ends the process).
#include <kaihen.h>
static void probeAliasing() {
  const struct { int id; const char* n; } svcs[] = {{0x13, "MapSharedMemory"}, {0x15, "CreateTransferMemory"}, {0x2C, "MapPhysicalMemory"}, {0x4B, "CreateCodeMemory"},
      {0x4C, "ControlCodeMemory"}, {0x50, "CreateSharedMemory"}, {0x51, "MapTransferMemory"}, {0x73, "SetProcessMemoryPermission"}, {0x74, "MapProcessMemory"},
      {0x75, "UnmapProcessMemory"}, {0x77, "MapProcessCodeMemory"}, {0x78, "UnmapProcessCodeMemory"}};
  string line = "probe: syscalls hinted:";
  for (auto& v : svcs) { char b[64]; snprintf(b, sizeof b, " %s(0x%x)=%d", v.n, v.id, (int)envIsSyscallHinted(v.id)); line += b; }
  record("%s", line.c_str());
  u8* heap = (u8*)memalign(0x200000, 0x400000);
  if (!heap) { record("probe: no heap"); return; }
  memset(heap, 0, 0x400000); heap[0x1234] = 0x5a;
  Handle self = INVALID_HANDLE;
  {
    Service ro, hen;
    Result rc = kaihenGetRoSession(&ro);
    if (R_SUCCEEDED(rc)) rc = kaihenOpenHomebrewSession(&ro, &hen);
    if (R_SUCCEEDED(rc)) { rc = henGetProcessHandle(&hen, &self); serviceClose(&hen); }
    record("probe: own process handle from kaiHEN: rc %x handle %x", rc, self);
  }
  if (envIsSyscallHinted(0x74)) {
    for (int attempt = 0; attempt < 4; attempt++) {
      Handle h = attempt & 1 ? self : CUR_PROCESS_HANDLE;
      if (h == INVALID_HANDLE) continue;
      virtmemLock();
      void* dst = attempt < 2 ? virtmemFindAslr(0x400000, 0x10000) : virtmemFindCodeMemory(0x400000, 0x10000);
      Result rc = dst ? svcMapProcessMemory(dst, h, (u64)heap, 0x400000) : (Result)-1;
      virtmemUnlock();
      MemoryInfo mi; u32 pi; svcQueryMemory(&mi, &pi, (u64)heap);
      record("probe: MapProcessMemory(%s, %s region) at %p: rc %x; source perm %x", attempt & 1 ? "kaiHEN handle" : "pseudo handle", attempt < 2 ? "aslr" : "code", dst, rc, mi.perm);
      if (R_SUCCEEDED(rc)) {
        volatile u8* a = (volatile u8*)dst;
        bool reads = a[0x1234] == 0x5a; a[0x2000] = 0x77; bool mirrors = heap[0x2000] == 0x77;
        record("probe:   alias reads %s, writes %s", reads ? "the source" : "WRONG DATA", mirrors ? "show in the source" : "do NOT show in the source");
        svcUnmapProcessMemory(dst, h, (u64)heap, 0x400000);
        break;
      }
    }
  }
  if (envIsSyscallHinted(0x15) && envIsSyscallHinted(0x51)) {
    Handle t; Result rc = svcCreateTransferMemory(&t, heap, 0x400000, Perm_Rw);
    record("probe: CreateTransferMemory rc %x", rc);
    if (R_SUCCEEDED(rc)) {
      virtmemLock(); void* dst = virtmemFindAslr(0x400000, 0x10000); Result r2 = svcMapTransferMemory(t, dst, 0x400000, Perm_Rw); virtmemUnlock();
      MemoryInfo mi; u32 pi; svcQueryMemory(&mi, &pi, (u64)heap);
      record("probe: MapTransferMemory at %p rc %x; source perm %x", dst, r2, mi.perm);
      if (R_SUCCEEDED(r2)) { svcUnmapTransferMemory(t, dst, 0x400000); }
      svcCloseHandle(t);
    }
  }
  if (self != INVALID_HANDLE) svcCloseHandle(self);
  free(heap);
  record("probe: done");
}


// Sampling profiler (autotest runs): the core's emulation thread stores which subsystem it is in
// (pcsx2/sw2_prof.h); this thread samples that, and the emulated program counter, about 5000
// times a second from 4 s into a run. The totals go to the log when the run ends.
#include <unordered_map>
extern "C" {
extern volatile unsigned sw2_prof_region;
unsigned sw2_prof_pc(int iop);
extern bool sw2_gs_spin_wait;
extern uint32_t sw2_ee_wait_cycles;
void sw2_profiler_reset_handle();
}
extern "C" unsigned long long sw2_int_bits[33];
static Thread profThread;
static volatile bool profOn = false;
static bool profStarted = false;
static u64 profRegion[24];
static std::unordered_map<u32, u32> profPc[2];
// Host program-counter samples of the busiest core thread (the EE), when the system lets this
// process pause a thread and read its registers (svcs 0x32/0x33): offsets into this image, to
// resolve with nm on sw2ps2.elf, and the share outside it (JIT code).
extern "C" unsigned sw2_thread_handle(int i);
extern "C" void sw2_absmem_stats(unsigned long long*, unsigned long long*, unsigned long long*, unsigned long long*);
static std::unordered_map<u32, u32> profHost;
static std::unordered_map<u64, u32> profHostCallers;
static bool autoProfileCallers = false;
static u64 profHostOut = 0, profHostTotal = 0;
static std::unordered_map<u64, u32> profJit;  // host pcs outside the image (JIT)
static std::unordered_map<u64, u32> profVu0Instructions;
extern "C" bool sw2ax_vu0_sample(u64, u32*, u32*) __attribute__((weak));
extern "C" u32 sw2_jit_guest_pc(u64 host, u64* block_start);
static Handle profEE = 0;
static Result profRc1 = 0, profRc2 = 0; static int profCtx = -1;
static void profMain(void*);
static void profMain(void*) {
  sw2_thread_total();  // (ARMSX2: asks the EE thread for its handle)
  for (int i = 0; i < 40 && profOn; i++) svcSleepThread(100000000ULL);
  u32 seed = 12345;
  const bool ctx = envIsSyscallHinted(0x32) && envIsSyscallHinted(0x33);
  profCtx = ctx ? 1 : 0;
  if (ctx) {  // the busiest registered thread over the last 0.2 s
    unsigned long long a[64] = {0}, b2[64] = {0}; int core; void* entry; int n = std::min(sw2_thread_total(), 64);
    for (int i = 0; i < n; i++) sw2_thread_info(i, &a[i], &core, &entry);
    svcSleepThread(200000000ULL);
    unsigned long long best = 0;
    for (int i = 0; i < n; i++) if (sw2_thread_info(i, &b2[i], &core, &entry) && b2[i] - a[i] > best) { best = b2[i] - a[i]; profEE = sw2_thread_handle(i); }
  }
  while (profOn) {
    if (profEE) {
      ThreadContext tc;
      Result r1 = svcSetThreadActivity(profEE, ThreadActivity_Paused);
      if (R_FAILED(r1)) profRc1 = r1;
      if (R_SUCCEEDED(r1)) {
        Result r = svcGetThreadContext3(&tc, profEE);
        if (R_FAILED(r)) profRc2 = r;
        svcSetThreadActivity(profEE, ThreadActivity_Runnable);
        if (R_SUCCEEDED(r)) {
          u64 pc = tc.pc.x; profHostTotal++;
          u32 vuOffset, vuInstruction;
          if (sw2ax_vu0_sample && sw2ax_vu0_sample(pc, &vuOffset, &vuInstruction))
            profVu0Instructions[(static_cast<u64>(vuOffset) << 32) | vuInstruction]++;
          // offsets from this function's runtime address (tools/hostpcs.py adds its link address)
          const u64 anchor = (u64)(void*)&profMain;
          if (pc + 0x10000000ULL > anchor && pc < anchor + 0x10000000ULL) {
            const u32 pcOffset = (u32)(pc - anchor + 0x10000000ULL);
            profHost[pcOffset]++;
            if (autoProfileCallers) {
              const u64 lr = tc.lr;
              const u32 lrOffset = lr + 0x10000000ULL > anchor && lr < anchor + 0x10000000ULL
                  ? (u32)(lr - anchor + 0x10000000ULL) : 0xffffffffu;
              profHostCallers[(u64(pcOffset) << 32) | lrOffset]++;
            }
          } else { profHostOut++; profJit[pc]++; }
        }
      }
    }
    unsigned r = sw2_prof_region;
    profRegion[r < 24 ? r : 0]++;
    if (r == 1) profPc[0][sw2_prof_pc(0)]++; else if (r == 2) profPc[1][sw2_prof_pc(1)]++;
    seed = seed * 1664525u + 1013904223u;
    svcSleepThread(150000ULL + (seed >> 16) % 100000u);
  }
}
static void profStart() {
  if (profStarted) return;
  sw2_profiler_reset_handle();
  memset(profRegion, 0, sizeof profRegion); profPc[0].clear(); profPc[1].clear(); profHost.clear(); profHostCallers.clear(); profJit.clear(); profVu0Instructions.clear(); profHostOut = profHostTotal = 0; profEE = 0;
  profOn = true;
  if (R_SUCCEEDED(threadCreate(&profThread, profMain, nullptr, nullptr, 0x10000, 0x20, 0)) && R_SUCCEEDED(threadStart(&profThread))) profStarted = true;
  else profOn = false;
}
static void profStop() {
  if (!profStarted) return;
  profOn = false; threadWaitForExit(&profThread); threadClose(&profThread); profStarted = false;
  static const char* NAMES[24] = {"?", "ee-code", "iop", "spu2", "gif", "events", "vif", "vu0", "counters", "ee-compile", "ee-validate", "gate-wait", "gs-wait", "vu1-wait", "iop-compile", "disc-read", "dma-irq", "iop-events", "?", "?", "?", "?", "?", "?"};
  u64 total = 0; for (u64 v : profRegion) total += v;
  if (!total) return;
  string line = "profile (" + std::to_string(total) + " samples):";
  char b[96];
  for (int i = 0; i < 24; i++) if (profRegion[i]) { snprintf(b, sizeof b, " %s %.1f%%", NAMES[i], profRegion[i] * 100.0 / total); line += b; }
  record("%s", line.c_str());
  {  // interrupt bits pending at the core's event tests over the run
    static unsigned long long prevBits[33];
    string l = "event tests by pending interrupt bit:"; char b2[48];
    for (int i = 0; i < 33; i++) { unsigned long long d = sw2_int_bits[i] - prevBits[i]; if (d) { snprintf(b2, sizeof b2, " %s%d:%llu", i == 32 ? "none" : "bit", i == 32 ? 0 : i, d); l += b2; } prevBits[i] = sw2_int_bits[i]; }
    record("%s", l.c_str());
  }
  { unsigned long long a, b3, c, d; sw2_absmem_stats(&a, &b3, &c, &d); record("adrp addressing: %llu near, %llu far; code %llx, data %llx", a, b3, c, d); }
  record("host pc sampling: ctx %d, thread %x, pause rc %x, context rc %x, samples %llu, anchor %p", profCtx, (unsigned)profEE, profRc1, profRc2, (unsigned long long)profHostTotal, (void*)&profMain);
  if (!profVu0Instructions.empty()) {
    vector<std::pair<u64, u32>> v(profVu0Instructions.begin(), profVu0Instructions.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& c) { return a.second > c.second; });
    line = "VU0 host instructions:";
    for (size_t i = 0; i < v.size() && i < 100; i++) {
      snprintf(b, sizeof b, " %x/%08x:%u", (unsigned)(v[i].first >> 32), (unsigned)v[i].first, v[i].second);
      line += b;
    }
    record("%s", line.c_str());
  }
  if (profHostTotal) {
    vector<std::pair<u32, u32>> v(profHost.begin(), profHost.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& c) { return a.second > c.second; });
    line = "host pcs (" + std::to_string(profHostTotal) + " samples, outside the image " + std::to_string(profHostOut * 100 / profHostTotal) + "%):";
    for (size_t i = 0; i < v.size() && i < 400; i++) { snprintf(b, sizeof b, " %x:%u", v[i].first, v[i].second); line += b; }
    record("%s", line.c_str());
  }
  if (autoProfileCallers && !profHostCallers.empty()) {
    vector<std::pair<u64, u32>> v(profHostCallers.begin(), profHostCallers.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& c) { return a.second > c.second; });
    line = "host caller pairs:";
    for (size_t i = 0; i < v.size() && i < 3000; i++) {
      snprintf(b, sizeof b, " %x/%x:%u", unsigned(v[i].first >> 32), unsigned(v[i].first), v[i].second);
      line += b;
    }
    record("%s", line.c_str());
  }
  if (!profJit.empty()) {  // JIT samples by block (EE blocks only; others show as guest 0)
    std::unordered_map<u64, std::pair<u32, u32>> byBlock;  // block start -> (guest pc, samples)
    if (sw2ax_classify_batch) {  // ARMSX2: EE block start PCs, other code by region (0xF000000r)
      vector<unsigned long long> pcs; vector<u32> cnt; for (auto& kv : profJit) { pcs.push_back(kv.first); cnt.push_back(kv.second); }
      vector<unsigned> out(pcs.size());
      sw2ax_classify_batch(pcs.data(), cnt.data(), out.data(), pcs.size());
      for (size_t i = 0; i < pcs.size(); i++) { auto& e = byBlock[out[i]]; e.first = out[i]; e.second += cnt[i]; }
    } else
    for (auto& kv : profJit) { u64 st = 0; u32 g = sw2_jit_guest_pc(kv.first, &st); auto& e = byBlock[st]; e.first = g; e.second += kv.second; }
    vector<std::pair<u64, std::pair<u32, u32>>> v(byBlock.begin(), byBlock.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& c) { return a.second.second > c.second.second; });
    line = "jit blocks (" + std::to_string(byBlock.size()) + "):";
    for (size_t i = 0; i < v.size() && i < 60; i++) { snprintf(b, sizeof b, " %08x:%.2f%%", v[i].second.first, v[i].second.second * 100.0 / profHostTotal); line += b; }
    record("%s", line.c_str());
  }
  for (int k = 0; k < 2; k++) {
    vector<std::pair<u32, u32>> v(profPc[k].begin(), profPc[k].end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& c) { return a.second > c.second; });
    line = string(k ? "profile iop pcs (" : "profile ee pcs (") + std::to_string(v.size()) + " distinct):";
    for (size_t i = 0; i < v.size() && i < 40; i++) { snprintf(b, sizeof b, " %08x:%.1f%%", v[i].first, v[i].second * 100.0 / total); line += b; }
    record("%s", line.c_str());
  }
}

// Clock probe (autotest {"clockProbe": true}): tries performance configurations and logs the
// clocks and a CPU timing for each one the system accepts.
static void probeClocks() {
  auto bench = [] { u64 t0 = armGetSystemTick(); volatile u64 a = 1; for (u32 i = 0; i < 20000000u; i++) a = a * 6364136223846793005ULL + 1442695040888963407ULL; return armTicksToNs(armGetSystemTick() - t0) / 1e6; };
  ApmPerformanceMode pm = ApmPerformanceMode_Normal; u32 orig = 0;
  Result r0 = apmGetPerformanceMode(&pm), r1 = apmGetPerformanceConfiguration(pm, &orig);
  record("clock probe: mode %d (rc %x), configuration %08x (rc %x)", (int)pm, r0, orig, r1);
  if (R_FAILED(r0) || R_FAILED(r1) || R_FAILED(clkrstInitialize())) { record("clock probe: unavailable"); return; }
  vector<u32> ids;
  for (u32 hi : {0x00010000u, 0x00020000u, 0x00030000u, 0x00040000u}) for (u32 i = 0; i < 12; i++) ids.push_back(hi + i);
  for (u32 i = 0; i < 0x30; i++) ids.push_back(0x92220000u + i);
  for (u32 id : ids) {
    record("clock probe: trying %08x", id);
    Result rc = apmSetPerformanceConfiguration(pm, id);
    if (R_FAILED(rc)) { record("  refused (rc %x)", rc); continue; }
    svcSleepThread(150000000ULL);
    u32 hz[3] = {0, 0, 0}; const PcvModuleId mods[3] = {PcvModuleId_CpuBus, PcvModuleId_GPU, PcvModuleId_EMC};
    for (int i = 0; i < 3; i++) { ClkrstSession ses; if (R_SUCCEEDED(clkrstOpenSession(&ses, mods[i], 3))) { clkrstGetClockRate(&ses, &hz[i]); clkrstCloseSession(&ses); } }
    record("  accepted: cpu %u MHz, gpu %u MHz, memory %u MHz, cpu test %.0f ms", hz[0] / 1000000, hz[1] / 1000000, hz[2] / 1000000, bench());
  }
  apmSetPerformanceConfiguration(pm, orig);
  clkrstExit();
  record("clock probe: done, restored %08x", orig);
}

// Starts run number autoRun of the automated test (see main); false when there are no more.
static Json autoJson;
static int autoRunBase = 0;
// Test runs can press player 1's buttons: "press": [{"at": 2.5, "button": "plus", "ms": 200}, ...]
// (seconds from the run's start; Switch button names a b x y l r zl zr plus minus up down left right).
struct AutoPress { double at, ms; u64 bits; };
static vector<AutoPress> autoPresses;
static u64 autoPressBits() {
  const double t = nowMs() - autoRunStart; u64 b = 0;
  for (auto& p : autoPresses) if (t >= p.at * 1000 && t < p.at * 1000 + p.ms) b |= p.bits;
  return b;
}
static u64 autoButtonBits(const string& n) {
  static const std::pair<const char*, u64> names[] = {{"a", HidNpadButton_A}, {"b", HidNpadButton_B}, {"x", HidNpadButton_X}, {"y", HidNpadButton_Y},
    {"l", HidNpadButton_L}, {"r", HidNpadButton_R}, {"zl", HidNpadButton_ZL}, {"zr", HidNpadButton_ZR}, {"plus", HidNpadButton_Plus},
    {"minus", HidNpadButton_Minus}, {"up", HidNpadButton_Up}, {"down", HidNpadButton_Down}, {"left", HidNpadButton_Left}, {"right", HidNpadButton_Right}};
  for (auto& kv : names) if (n == kv.first) return kv.second;
  return 0;
}  // run numbers before an app restart (test screenshots are named by run)  // this .nro, for restarting between test games
static int autoRun = 0;
static void startGame(const Rom& rom);
static int autoShaderPrev = -1;  // the player's shader while a test run overrides it
static int autoAudioBufferDefault = 1;
static int autoDisplayDefault = 1;
static int autoFrameDelayDefault = 0;
static bool autoStartRun() {
  const Json* runs = autoJson.get("runs");
  Json one = Json::object();
  const Json* j = runs && runs->isArr() ? (autoRun < (int)runs->a.size() ? &runs->a[autoRun] : nullptr) : (autoRun == 0 ? &one : nullptr);
  if (!j) return false;
  const Json* fixed = j->get("fixedSettings");
  autoFixedSettings = fixed && fixed->isBool() && fixed->b;
  autoCycleSkip = pick(j->get("cycleSkip"), CYCLE_SKIPS.size(), 0);
  frameDelay = autoFixedSettings ? 0 : autoFrameDelayDefault;
  const int testDisplay = autoFixedSettings ? pick(j->get("display"), DISPLAY_MODES.size(), 1) : autoDisplayDefault;
  if (displayMode != testDisplay) { displayMode = testDisplay; gfx::setHighRefresh(displayMode == 1); }
  const Json* vkMeasure = j->get("measureVulkan");
  const Json* vuBlocks = j->get("traceVu0Blocks");
  if (sw2ax_trace_vu0_blocks) sw2ax_trace_vu0_blocks(vuBlocks && vuBlocks->isBool() && vuBlocks->b);
  const Json* sprTrace = j->get("traceSpr");
  autoTraceSpr = sprTrace && sprTrace->isBool() && sprTrace->b;
  if (sw2ax_trace_spr) sw2ax_trace_spr(autoTraceSpr);
  const Json* frameWork = j->get("traceFrameWork");
  autoTraceFrameWork = frameWork && frameWork->isBool() && frameWork->b;
  if (sw2ax_trace_frame_work) sw2ax_trace_frame_work(autoTraceFrameWork);
  const Json* vifCopies = j->get("traceVifCopies");
  autoTraceVifCopies = vifCopies && vifCopies->isBool() && vifCopies->b;
  if (sw2ax_trace_vif_copies) sw2ax_trace_vif_copies(autoTraceVifCopies);
  const Json* vuNotifications = j->get("traceVuNotifications");
  autoTraceVuNotifications = vuNotifications && vuNotifications->isBool() && vuNotifications->b;
  if (sw2ax_trace_vu_notifications) sw2ax_trace_vu_notifications(autoTraceVuNotifications);
  autoMeasureVulkan = vkMeasure && vkMeasure->isBool() && vkMeasure->b;
  const Json* passEnds = j->get("vkTracePassEnds");
  autoTracePassEnds = passEnds && passEnds->isBool() && passEnds->b;
  if (sw2ax_vk_trace_pass_ends) sw2ax_vk_trace_pass_ends(autoTracePassEnds);
  const Json* handoff = j->get("measureHandoff");
  autoMeasureHandoff = handoff && handoff->isBool() && handoff->b;
  const Json* cached = j->get("vkCachedRings");
  const Json* tfxCache = j->get("vkTfxCache");
  const Json* canonicalPipelines = j->get("vkCanonicalPipelines");
  const Json* carryColourFeedback = j->get("vkCarryColourFeedback");
  if (sw2ax_vk_test_options)
    sw2ax_vk_test_options(autoMeasureVulkan, pick(j->get("vkReadbackKickPasses"), 65, 0),
                          cached && cached->isBool() && cached->b, tfxCache && tfxCache->isBool() && tfxCache->b,
                          canonicalPipelines && canonicalPipelines->isBool() && canonicalPipelines->b,
                          carryColourFeedback && carryColourFeedback->isBool() && carryColourFeedback->b);
  audioBuffer = pick(j->get("audioBuffer"), AUDIO_BUFFERS.size(), autoAudioBufferDefault);
  const Json* g = j->get("game") ? j->get("game") : autoJson.get("game"); const Json* sl = j && j->get("slot") ? j->get("slot") : autoJson.get("slot"); const Json* sec = autoJson.get("seconds");
  int slot = sl && sl->isNum() ? (int)sl->n : 1; double seconds = sec && sec->isNum() ? sec->n : 20;
  ps2Renderer = pick(j->get("renderer"), RENDERERS.size(), 0); ps2Resolution = pick(j->get("resolution"), RESOLUTIONS.size(), 0);
  ps2EeSpeed = pick(j->get("eeSpeed"), EE_SPEEDS.size(), 1);  // (as players get it: 75%)
  ps2Fpu = pick(j->get("fpu"), FPU_MODES.size(), 0);
  { const Json* rb = j->get("readback"); autoReadback = rb && rb->isNum() ? (int)rb->n : autoFixedSettings ? 0 : -1; }
  cpuClock = pick(j->get("clock"), CPU_CLOCKS.size(), 2);  // 0 normal, 1 boost mode, 2 maximum
  gpuClock = pick(j->get("gpu"), GPU_CLOCKS.size(), 0);
  if (&sw2_prof_gs) { const Json* pg = j->get("profgs"); sw2_prof_gs = pg && pg->isBool() && pg->b; }
  const Json* gsSpin = j->get("gsSpinWait");
  if (&sw2_gs_spin_wait) sw2_gs_spin_wait = gsSpin && gsSpin->isBool() && gsSpin->b;
  const Json* eeWait = j->get("eeWaitCycles");
  if (&sw2_ee_wait_cycles) {
    if (eeWait && eeWait->isNum())
      sw2_ee_wait_cycles = (uint32_t)eeWait->n;
    else
      sw2_ee_wait_cycles = 3072;
  }
  const Json* apr = j->get("audioPanicRefill");
  sw2_audio_panic_refill = !apr || !apr->isBool() || apr->b;
  if (&sw2_trace_ee_events) {
    const Json* trace = j->get("traceEvents");
    sw2_trace_ee_events = trace && trace->isBool() && trace->b;
  }
  if (sw2ax_crash_watch) { const Json* cw = j->get("crashwatch"); sw2ax_crash_watch(cw && cw->isBool() && cw->b ? autoRunBase + autoRun + 1 : 0); }  // file per run
  { const Json* mc = j->get("memclock"); memClockMax = mc && mc->isBool() && mc->b; memClockMhz = mc && mc->isNum() ? (int)mc->n : 0; }
  { const Json* gm = j->get("gpumhz"); gpuClockMhz = gm && gm->isNum() ? (int)gm->n : 0; }
  if (&sw2_shader_log) { const Json* sl = j->get("shaderlog"); sw2_shader_log = sl && sl->isBool() && sl->b; }
  if (&sw2_db_effects) { const Json* de = j->get("dbeffects"); sw2_db_effects = !(de && de->isBool() && !de->b); }
  if (&sw2_no_pacing) { const Json* np = j->get("nopacing"); sw2_no_pacing = !(np && np->isBool() && !np->b); }  // default off; "nopacing": false turns it back on
  if (&sw2_gs_back) { const Json* gb = j->get("gsback"); sw2_gs_back = gb && gb->isBool() && gb->b; }
  if (&sw2_iop_lag) { const Json* il = j->get("ioplag"); sw2_iop_lag = il && il->isNum() ? (int)il->n : 98304; }
  if (&sw2_iop_thread) { const Json* it = j->get("iopthread"); sw2_iop_thread = !(it && it->isBool() && !it->b); }  // default on
  if (&sw2_smc_off) { const Json* so = j->get("smcoff"); sw2_smc_off = so && so->isBool() && so->b; }  // test only
  if (&sw2_vuaot) { const Json* va = j->get("vuaot"); sw2_vuaot = va && va->isBool() && va->b; }
  { const Json* adapt = j->get("adaptiveAudio"); adaptiveAudio = !adapt || !adapt->isBool() || adapt->b; }
  if (&sw2_audio_stretch) { const Json* st = j->get("stretch"); sw2_audio_stretch = st && st->isBool() ? st->b : adaptiveAudio; }
  { const Json* ms = j->get("measured"); skipMeasured = ms && ms->isBool() && !ms->b; }
  { const Json* at = j->get("autotune"); autoTuneTest = at && at->isBool() ? (at->b ? 1 : 0) : -1; }
  const Json* preciseMfifo = j->get("mfifoPreciseNotify");
  if (&sw2_mfifo_precise_notify) sw2_mfifo_precise_notify = !preciseMfifo || !preciseMfifo->isBool() || preciseMfifo->b;
  const Json* kickLight = j->get("vuKickLight");
  { const Json* gh = j->get("noGsHooks"); if (&sw2_no_gs_hooks) sw2_no_gs_hooks = gh && gh->isBool() && gh->b; }
  { const Json* nf = j->get("iopNoFence"); if (&sw2_iop_ee_nofence) sw2_iop_ee_nofence = !(nf && nf->isBool() && !nf->b); }
  if (&sw2_vu_kick_light) sw2_vu_kick_light = kickLight && kickLight->isNum() ? (int)kickLight->n : kickLight && kickLight->isBool() && !kickLight->b ? 0 : 2;  // 2 default, 1 fenced, 0 always kick
  if (&sw2_softprot) { const Json* sp = j->get("softprot"); sw2_softprot = !(sp && sp->isBool() && !sp->b); }  // default on
  if (const Json* b = j->get("boost")) if (b->isBool()) cpuClock = b->b ? 1 : 0;  // older test files
  autoOptions.clear();
  if (const Json* o = j->get("options")) if (o->type == Json::OBJ) for (auto& kv : o->o) if (kv.second.isStr()) autoOptions.push_back({kv.first, kv.second.s});
  {  // test runs show the full stats with the frame-time graph unless they say otherwise
    const Json* st = j->get("stats");
    statsLevel = st && st->isNum() ? std::max(0, std::min((int)STATS_LEVELS.size() - 1, (int)st->n)) : 3;
    resetGraph();
  }
  {
    int total = runs && runs->isArr() ? (int)runs->a.size() : 1;
    string what = "CPU clock " + CPU_CLOCKS[cpuClock] + ", FPU " + FPU_MODES[ps2Fpu] + ", " + RESOLUTIONS[ps2Resolution] + ", " + RENDERERS[ps2Renderer] + ", CPU " + EE_SPEEDS[ps2EeSpeed];
    for (auto& kv : autoOptions) what += ", " + kv.first + "=" + kv.second;
    autoLabel = "Test " + std::to_string(autoRun + 1) + " of " + std::to_string(total) + " (" + std::to_string((int)seconds) + " s): " + what;
  }
  const Rom* found = nullptr;
  if (g && g->isStr()) for (auto& r : allRoms) if (r.name == g->s) found = &r;
  record("autotest: run %d: game '%s' %s, slot %d, %.0f s, %s", autoRun, g && g->isStr() ? g->s.c_str() : "", found ? "found" : "NOT FOUND", slot, seconds, j->dump().c_str());
  if (autoFixedSettings)
    record("autotest: fixed settings, readbacks %s, display %s, frame delay %d ms, audio buffer %s%s%s",
           autoReadback == 1 ? "fast" : "accurate",
           DISPLAY_MODES[displayMode].c_str(), FRAME_DELAY_MS[frameDelay], AUDIO_BUFFERS[audioBuffer].c_str(),
           sw2_gs_spin_wait ? ", GS spin-wait on" : "",
           sw2_ee_wait_cycles != 3072 ? (", eeWaitCycles=" + std::to_string(sw2_ee_wait_cycles)).c_str() : "",
           sw2_audio_panic_refill ? "" : ", audioSmooth on");
  if (!found) return false;
  Rom rom = *found;
  startGame(rom);
  if (!running) { record("autotest: the game did not start: %s", messageText.c_str()); return false; }
  if (slot > 0) loadState(false, slot);
  if (const Json* sh = j->get("shader")) if (sh->isNum()) {  // picture shader for this run (0 = none), not saved
    autoShaderPrev = shader;
    shader = std::max(0, std::min((int)SHADERS.size() - 1, (int)sh->n)); crtKey.clear(); applyCrt();
    record("autotest: shader %s", SHADERS[shader].c_str());
  }
  record("autotest: running (%s)", toastText.c_str());
  const Json* measure = j->get("measureAfterLoad");
  const bool measureAfterLoad = slot > 0 && measure && measure->isBool() && measure->b;
  if (measureAfterLoad && !pendingLoadSlot && toastText != "Loaded slot " + std::to_string(slot)) {
    record("autotest: failed to load slot %d for measurement", slot);
    quitGame();
    return false;
  }
  autoMeasureSeconds = seconds;
  autoAwaitingState = measureAfterLoad && pendingLoadSlot;
  autoMeasuredFrames = 0;
  autoFrameGaps.clear();
  autoGameIntervals.clear();
  autoPrevSwap = 0;
  autoLowWindowStarted = false;
  autoSkipFirstGameInterval = false;
  autoRunStart = nowMs();
  autoTestEnd = autoRunStart + (autoAwaitingState ? 65 : seconds + AUTO_LOW_WARMUP_MS / 1000) * 1000;
#ifdef SW2_VK
  vkp::measureHandoff(autoMeasureHandoff);
#endif
  if (autoMeasureVulkan && sw2ax_vk_test_counters) sw2ax_vk_test_counters(autoVulkanStart);
  sessionLog = 0;
  autoShots.clear();
  autoGameShotFrames = 0;
  autoGameShotPriming = true;
  { const Json* gs = j->get("shotGameFrame"); autoGameShotTarget = gs && gs->isNum() ? std::max(0, (int)gs->n) : 0; }
  { const Json* ps = j->get("playerStats"); autoPlayerStats = ps && ps->isBool() && ps->b; }
  autoMenuLoadAt = -1; autoMenuLoadStage = 0; autoLiveRes.clear(); autoEarlyLoadAt = -1; autoMenuOpenAt = -1; autoSaveAt = -1;
  if (const Json* sv = j->get("saveState")) if (sv->isArr() && sv->a.size() == 2 && sv->a[0].isNum() && sv->a[1].isNum()) { autoSaveAt = sv->a[0].n; autoSaveSlot = (int)sv->a[1].n; }
  if (const Json* mo = j->get("menuOpen")) if (mo->isNum()) autoMenuOpenAt = mo->n;
  if (const Json* el = j->get("earlyLoad")) if (el->isArr() && el->a.size() >= 1 && el->a[0].isNum()) { autoEarlyLoadAt = el->a[0].n; autoEarlyLoadSlot = el->a.size() > 1 && el->a[1].isNum() ? (int)el->a[1].n : 1; }
  autoLiveSet.clear();
  if (const Json* ls = j->get("liveSet")) if (ls->isArr()) for (auto& e : ls->a) if (e.isArr() && e.a.size() == 3 && e.a[0].isNum() && e.a[1].isStr() && e.a[2].isNum()) autoLiveSet.push_back({e.a[0].n, e.a[1].s, (int)e.a[2].n});
  if (const Json* lr = j->get("liveRes")) if (lr->isArr()) for (auto& e : lr->a) if (e.isArr() && e.a.size() == 2 && e.a[0].isNum() && e.a[1].isNum()) autoLiveRes.push_back({e.a[0].n, (int)e.a[1].n});
  if (const Json* ml = j->get("menuLoad")) if (ml->isArr() && ml->a.size() >= 1 && ml->a[0].isNum()) { autoMenuLoadAt = ml->a[0].n; autoMenuLoadSlot = ml->a.size() > 1 && ml->a[1].isNum() ? (int)ml->a[1].n : 1; }
  autoPresses.clear();
  if (const Json* pr = j->get("press")) if (pr->isArr()) for (auto& e : pr->a) if (e.type == Json::OBJ) {
    const Json* at = e.get("at"); const Json* bt = e.get("button"); const Json* ms = e.get("ms");
    if (at && at->isNum() && bt && bt->isStr()) autoPresses.push_back({at->n, ms && ms->isNum() ? ms->n : 150.0, autoButtonBits(bt->s)});
  }
  if (const Json* sh = j->get("shots")) if (sh->isArr()) for (auto& x : sh->a) if (x.isNum()) autoShots.push_back(x.n);
  if (const Json* sh = autoJson.get("shots")) if (sh->isArr() && autoShots.empty()) for (auto& x : sh->a) if (x.isNum()) autoShots.push_back(x.n);
  const Json* callers = j->get("profileCallers");
  autoProfileCallers = callers && callers->isBool() && callers->b;
  const Json* profile = j->get("profile");
  const bool profileEnabled = !profile || !profile->isBool() || profile->b;
  autoProfileAfterLoad = profileEnabled && autoAwaitingState;
  if (profileEnabled && !autoProfileAfterLoad) profStart();
  return true;
}
/* ---------------- Vibration ----------------
 * The core's DualShock 2 motors (bridge ps2_rumble): the large motor drives the controller's
 * low band, the small one its high band. Devices are set up again when the controller changes. */
extern "C" void ps2_rumble(int port, uint16_t* strong, uint16_t* weak);
struct Vib { HidVibrationDeviceHandle h[2]; int n = 0; u32 key = 0; uint16_t s = 0, w = 0; };
static Vib vibs[4];
// Player 1 reads controller 1 and the attached Joy-Cons together, so rumble follows whichever of the
// two was used last (buttons or a stick off centre), not just whether the Joy-Cons are attached.
static PadState vibNo1, vibHandheld;
static bool vibPadsReady = false, vibUseHandheld = false;
static bool padInUse(PadState* p) {
  if (padGetButtons(p)) return true;
  for (int i = 0; i < 2; i++) { HidAnalogStickState a = padGetStickPos(p, i); if (abs(a.x) > 8000 || abs(a.y) > 8000) return true; }
  return false;
}
static void vibTrackPlayer1() {
  if (!vibPadsReady) { padInitialize(&vibNo1, HidNpadIdType_No1); padInitialize(&vibHandheld, HidNpadIdType_Handheld); vibPadsReady = true; }
  padUpdate(&vibNo1); padUpdate(&vibHandheld);
  const bool no1 = padGetStyleSet(&vibNo1) != 0, hh = padGetStyleSet(&vibHandheld) != 0;
  if (padInUse(&vibNo1)) vibUseHandheld = false;
  else if (padInUse(&vibHandheld)) vibUseHandheld = true;
  if (!no1 && hh) vibUseHandheld = true;  // only one of them connected: that one
  else if (no1 && !hh) vibUseHandheld = false;
}
static void vibSend(int port, PadState* p, uint16_t strong, uint16_t weak) {
  Vib& v = vibs[port];
  if (port == 0) p = vibUseHandheld ? &vibHandheld : &vibNo1;
  u32 style = padGetStyleSet(p);
  HidNpadIdType id = port == 0 ? (vibUseHandheld ? HidNpadIdType_Handheld : HidNpadIdType_No1) : HidNpadIdType_No2;
  HidNpadStyleTag tag = id == HidNpadIdType_Handheld ? HidNpadStyleTag_NpadHandheld
                      : (style & HidNpadStyleTag_NpadFullKey) ? HidNpadStyleTag_NpadFullKey
                      : (style & HidNpadStyleTag_NpadJoyDual) ? HidNpadStyleTag_NpadJoyDual : (HidNpadStyleTag)0;
  u32 key = ((u32)id << 16) | (u32)tag;
  if (!tag) { v.n = 0; v.key = 0; return; }
  if (key != v.key) {
    v.key = key; v.n = R_SUCCEEDED(hidInitializeVibrationDevices(v.h, 2, id, tag)) ? 2 : 0; v.s = v.w = 1;  // force a send
  }
  if (!v.n || (strong == v.s && weak == v.w)) return;
  v.s = strong; v.w = weak;
  HidVibrationValue val[2];
  // Both PS2 motors combined onto the deep (low-frequency) rumble alone: the small motor, an on/off
  // buzz on the PS2, at half strength so it is still felt. The level scales the result.
  const float big = strong / 65535.0f, small = weak / 65535.0f;
  const float lo = std::min(1.0f, std::max(big, small * 0.5f) * VIB_GAIN[vibLevel]);
  for (auto& x : val) { x.amp_low = lo; x.freq_low = 160.0f; x.amp_high = 0.0f; x.freq_high = 320.0f; }
  hidSendVibrationValues(v.h, val, v.n);
}
static void updateVibration(bool active) {
  vibTrackPlayer1();
  for (int port = 0; port < 4; port++) {
    if (!padStates[port]) continue;
    uint16_t s = 0, w = 0;
    if (active && vibLevel != 0 && !autoTest) ps2_rumble(port, &s, &w);  // never during test runs
    vibSend(port, padStates[port], s, w);
  }
}

static void handleUi(u64 bits) {
  Nav ev = navEvents(bits);
  u64 pressed = ev.pressed;
  double t = nowMs();
  if (mode == "browser") {
    if (pressed & (HidNpadButton_StickL | (singleJoyConConnected ? (u64)HidNpadButton_StickR : 0))) {
      exitArmed = 0;
      int& romSort = romSortByView[listView];
      romSort = (romSort + 1) % ROM_SORT_LABELS.size();
      orderRoms();
      saveSettings();
      return;
    }
    // L/ZL and R/ZR cycle the All/Favourites/Recent tabs from anywhere.
    int tabStep = ((pressed & (B_R | HidNpadButton_ZR)) ? 1 : 0) - ((pressed & (B_L | HidNpadButton_ZL)) ? 1 : 0);
    if (tabStep) {
      int n = (int)LIST_VIEWS.size();
      listView = (listView + tabStep + n) % n; roms.clear(); orderRoms(); selected = 0;
      if (focusArea == "list" && roms.empty()) focusArea = "tabs";
    }
    // Y / X: the Settings and Refresh buttons (B is Exit, below).
    if (pressed & B_Y) { exitArmed = 0; mode = "settings"; settingsIndex = 0; settingsTop = 0; return; }
    if (pressed & B_X) { exitArmed = 0; scan(); showToast("List refreshed"); return; }
    if (exitArmed && (t > exitArmed || (pressed & ~(u64)(B_A | B_B)) || ev.up || ev.down || focusArea != "buttons" || btnIndex != 2)) exitArmed = 0;
    int VIS = std::max(1, std::min((int)roms.size(), maxRows()));
    if (focusArea == "tabs") {
      if (pressed & B_LEFT) { int v = std::max(0, listView - 1); if (v != listView) { listView = v; roms.clear(); orderRoms(); } }
      if (pressed & B_RIGHT) { int v = std::min((int)LIST_VIEWS.size() - 1, listView + 1); if (v != listView) { listView = v; roms.clear(); orderRoms(); } }
      if (ev.down) { focusArea = roms.empty() ? "buttons" : "list"; selected = 0; }
    } else if (focusArea == "list" && !roms.empty()) {
      if (ev.up) { if (selected == 0) focusArea = "tabs"; else selected--; }
      if (ev.down) { if (selected == (int)roms.size() - 1) focusArea = "buttons"; else selected++; }
      if (pressed & B_LEFT) selected = std::max(0, selected - VIS);
      if (pressed & B_RIGHT) selected = std::min((int)roms.size() - 1, selected + VIS);
      if ((pressed & B_A) && selected < (int)roms.size()) { startGame(roms[selected]); return; }
    } else {
      if (ev.up) { focusArea = roms.empty() ? "tabs" : "list"; if (!roms.empty()) selected = (int)roms.size() - 1; }
      if (pressed & B_LEFT) btnIndex = std::max(0, btnIndex - 1);
      if (pressed & B_RIGHT) btnIndex = std::min(2, btnIndex + 1);
      if (pressed & B_A) {
        if (btnIndex == 0) { mode = "settings"; settingsIndex = 0; settingsTop = 0; }
        else if (btnIndex == 1) { scan(); showToast("List refreshed"); }
        else if (exitArmed) quitApp = true;
        else exitArmed = t + 3000;
        return;
      }
    }
    if (pressed & B_B) { if (exitArmed) quitApp = true; else { focusArea = "buttons"; btnIndex = 2; exitArmed = t + 3000; } }
    if (roms.empty() && focusArea == "list") focusArea = "buttons";
  } else if (mode == "folders") {
    int total = pickRows(), up = pickAtRoot() ? 0 : 1;
    if (ev.up) pickSel = (pickSel - 1 + total) % total;
    if (ev.down) pickSel = (pickSel + 1) % total;
    if (pressed & B_A) {
      if (pickSel == 0) {
        if (pickForBios) {
          biosDir = pickAtRoot() ? "sdmc:" : pickPath; saveSettings(); mode = "settings";
          char found[512];
          if (ps2core_find_bios(biosDir.c_str(), found, sizeof found)) { string f = found; showToast("BIOS found: " + f.substr(f.rfind('/') + 1), 3500); }
          else showToast("No BIOS in this folder or its subfolders yet", 3500);
        } else {
        romDir = pickAtRoot() ? "sdmc:" : pickPath;
        saveSettings(); scan(); mode = "settings";
        showToast(std::to_string(allRoms.size()) + (allRoms.size() == 1 ? " game in " : " games in ") + (romDir.size() > 5 ? romDir.substr(5) : string("/")));
        }
      } else if (up && pickSel == 1) pickLoad(parentDir(pickPath));
      else pickLoad((pickAtRoot() ? string("sdmc:") : pickPath) + "/" + pickDirs[pickSel - 1 - up]);
    } else if (pressed & B_B) mode = "settings";
  } else if (mode == "remap") {
    if (remapStep < 16) {  // listening to the player's own controller
      if (!padConnected(remapWho)) { if (pressed & B_B) mode = remapForGame ? "menu" : "settings"; return; }
      const u64 held = padGetButtons(padStates[remapWho]) & MAPPABLE;
      if (remapWaitRelease) { if (!held) { remapWaitRelease = false; remapDeadline = t + REMAP_WAIT_MS; } return; }
      if (held) {  // this button now presses this PS2 button (and no other)
        const int b = __builtin_ctzll(held);
        for (int i = 0; i < 16; i++) if (remapNew[i] == b) remapNew[i] = -1;
        remapNew[remapStep++] = b; remapWaitRelease = true;
      } else if (t >= remapDeadline) { remapNew[remapStep++] = -1; remapDeadline = t + REMAP_WAIT_MS; }  // skipped: unmapped
      return;
    }
    if (ev.up) remapPick = (remapPick + 2) % 3;
    if (ev.down) remapPick = (remapPick + 1) % 3;
    if (pressed & (B_LEFT)) remapPick = std::max(0, remapPick - 1);
    if (pressed & (B_RIGHT)) remapPick = std::min(2, remapPick + 1);
    if (pressed & B_A) {
      if (remapPick == 0) {
        if (remapForGame) {  // this game's own layout (the other player's starts as the Settings one)
          auto it = gamePadMaps.find(running->name);
          if (it == gamePadMaps.end()) { GamePadMap gm; memcpy(gm.m, padMap, sizeof gm.m); it = gamePadMaps.emplace(running->name, gm).first; }
          memcpy(it->second.m[remapWho], remapNew, sizeof remapNew);
        } else memcpy(padMap[remapWho], remapNew, sizeof remapNew);
        saveSettings();
        showToast("Player " + std::to_string(remapWho + 1) + (remapForGame ? " buttons saved for this game" : " buttons saved"), 2500);
        mode = remapForGame ? "menu" : "settings";
      }
      else if (remapPick == 1) openRemap(remapWho, remapForGame);
      else mode = remapForGame ? "menu" : "settings";
    } else if (pressed & B_B) mode = remapForGame ? "menu" : "settings";
  } else if (mode == "settings") {
    {  // L / R (or ZL / ZR): the next / previous category
      int step = ((pressed & (B_R | HidNpadButton_ZR)) ? 1 : 0) - ((pressed & (B_L | HidNpadButton_ZL)) ? 1 : 0);
      if (step) { int n = (int)SETTINGS_CATS.size(); settingsCat = (settingsCat + step + n) % n; resetArmed = false; rebuildSettings(); if (settingsIndex >= 0) settingsIndex = 0; settingsTop = 0; return; }
    }
    int nItems = (int)SETTINGS_ITEMS.size();
    if (ev.up || ev.down || (pressed & B_B)) { resetArmed = false; resetMapArmed = false; }
    if (settingsIndex < 0) {  // on the category tabs, as the game list's: left / right switch straight away, down to the list
      int last = (int)SETTINGS_CATS.size() - 1;
      int v = (pressed & B_LEFT) ? std::max(0, settingsCat - 1) : (pressed & B_RIGHT) ? std::min(last, settingsCat + 1) : settingsCat;
      if (v != settingsCat) { settingsCat = v; rebuildSettings(); settingsTop = 0; }
      if (ev.down) settingsIndex = 0;
      if (pressed & B_B) { saveSettings(); mode = "browser"; }
      return;
    }
    if (ev.up) { if (settingsIndex == 0) { settingsIndex = -1; return; } settingsIndex--; }
    if (ev.down && settingsIndex < nItems) settingsIndex++;
    if (settingsIndex < nItems) {
      SettingItem it = SETTINGS_ITEMS[settingsIndex];  // copy: the advanced toggle rebuilds the list
      int n = (int)it.choices().size();
      settingsByA = (pressed & B_A) != 0;
      if (pressed & B_LEFT) it.set((it.get() - 1 + n) % n);
      else if (pressed & (B_RIGHT | B_A)) it.set((it.get() + 1) % n);
    } else if (pressed & B_A) { saveSettings(); mode = "browser"; return; }
    if (pressed & B_B) { saveSettings(); mode = "browser"; }
  } else if (mode == "menu") {
    bool lr = menuCombo(bits);
    if (lr && !comboLatch) { comboLatch = true; closeMenu(); return; }
    if (!lr) comboLatch = false;
    {  // L / R: the next / previous category -- on release, and only for a lone tap, so starting
       // the L + R + L3 + R3 close never switches category first
      static int tapStep = 0; static bool tapSpoilt = false;
      const u64 other = HidNpadButton_StickL | HidNpadButton_StickR;
      if (pressed & (B_L | B_R)) { tapStep = (pressed & B_R) ? 1 : -1; tapSpoilt = (pressed & B_L) && (pressed & B_R); }
      if (tapStep && ((bits & other) || ((bits & HidNpadButton_L) && (bits & HidNpadButton_R)))) tapSpoilt = true;
      if (tapStep && !(bits & (HidNpadButton_L | HidNpadButton_R))) {
        const int step = tapSpoilt ? 0 : tapStep; tapStep = 0;
        if (step && menuHasTabs) { int c = (int)MENU_CATS.size(); menuCat = (menuCat + step + c) % c; const bool onTabs = menuIndex < 0; openMenu(); if (onTabs) menuIndex = -1; return; }
      }
    }
    int n = (int)menuItems.size();
    if (menuIndex < 0) {  // on the category tabs, as the game list's: left / right switch straight away, down to the list
      int last = (int)MENU_CATS.size() - 1;
      int v = (pressed & B_LEFT) ? std::max(0, menuCat - 1) : (pressed & B_RIGHT) ? std::min(last, menuCat + 1) : menuCat;
      if (v != menuCat) { menuCat = v; openMenu(); menuIndex = -1; return; }
      if (ev.down) menuIndex = 0;
      if ((pressed & B_B) && mode == "menu") closeMenu();
      return;
    }
    if (menuHasTabs) {  // no wrapping, as the game list: up from the first row reaches the tabs
      if (ev.up) { if (menuIndex == 0) { menuIndex = -1; return; } menuIndex--; }
      if (ev.down && menuIndex < n - 1) menuIndex++;
    } else {
      if (ev.up) menuIndex = (menuIndex - 1 + n) % n;
      if (ev.down) menuIndex = (menuIndex + 1) % n;
    }
    MenuItem item = menuItems[menuIndex];  // copy: actions may replace the list
    if (item.option()) {
      if (pressed & B_LEFT) cycleMenuOption(-1);
      if (pressed & (B_RIGHT | B_A)) cycleMenuOption(1);
    } else if (pressed & B_A) item.action();
    if ((pressed & B_B) && mode == "menu") {
      if (menuHasTabs) closeMenu();
      else openMenu();
    }
  } else if (mode == "message") {
    if (pressed & (B_A | B_B)) { mode = "browser"; auto then = messageThen; messageThen = nullptr; if (then) then(); }
  }
}

int main(int argc, char** argv) {
  romfsInit();
  mkdir("sdmc:/switch", 0777); mkdir(DATA_DIR, 0777);
  // First launch only (no settings file yet): write the settings. No folders are created: the
  // game and BIOS folders (default /roms/ps2 and /bios/ps2) are the player's to make.
  bool firstLaunch = !exists(SETTINGS());
  if (firstLaunch) saveSettings();  // (sdmc:/roms/ps2 is the default game folder but is not created: people make their own)
  // Logging only for test runs and with Settings -> App -> Debug logging on (read here, before the
  // settings load further down); normal play writes no log files.
  const string appendMarker = string(DATA_DIR) + "/autotest.append";
  bool wantLog = exists(string(DATA_DIR) + "/autotest.json") || exists(appendMarker);
  { string t; Json v; if (!wantLog && readText(SETTINGS(), t) && Json::parse(t, v) && v.type == Json::OBJ) if (const Json* d = v.get("debugLog")) wantLog = d->isBool() && d->b; }
  if (wantLog) {  // a test run restarting the app between games keeps appending to the same log, and so
                  // does a game started in a fresh process (launch.json): its session joins the launch that chose it
    const bool append = exists(appendMarker) || exists(string(DATA_DIR) + "/launch.json");
    if (exists(appendMarker)) remove(appendMarker.c_str());
    openLog(append);
  }
  record("SW2PS2 %s", APP_VERSION);

  string error;
  if (sw2ax_register_exit_cleanup) sw2ax_register_exit_cleanup(wantLog ? 1 : 0);  // before the core mask change below
  {
    u64 coreMask = 0; svcGetInfo(&coreMask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
#ifdef SW2_ARMSX2
    // ARMSX2 runs the EE, VU and GS on their own threads on cores 0, 1 and 2; this thread
    // (input, the frame hand-off, presenting) moves off the EE's core when there is another.
    // (core 3 goes to the IOP thread)
    Result rc = (coreMask & 16) ? svcSetThreadCoreMask(CUR_THREAD_HANDLE, 4, 16) : 0;
    record("cores: process mask 0x%llx, main thread to core 4: rc 0x%x", (unsigned long long)coreMask, rc);
#else
    record("cores: process mask 0x%llx", (unsigned long long)coreMask);
#endif
  }
  record("startup: gfx");
  if (!gfx::init(error)) { record("gfx: %s", error.c_str()); if (logFile) fclose(logFile); romfsExit(); return 1; }
  record("startup: audio");
  string audioError;
  bool sound = audio::init(audioError);
  if (!sound) record("audio: %s", audioError.c_str());

  record("startup: pads, core setup");
  padConfigureInput(4, HidNpadStyleSet_NpadStandard);  // players 3-4 drive the menus; the PS2 has ports 1-2
  PadState pad1, pad2;
  padInitialize(&pad1, HidNpadIdType_No1, HidNpadIdType_Handheld);
  padInitialize(&pad2, HidNpadIdType_No2);
  PadState pad3, pad4; padInitialize(&pad3, HidNpadIdType_No3); padInitialize(&pad4, HidNpadIdType_No4);
  padStates[2] = &pad3; padStates[3] = &pad4;
  padStates[0] = &pad1; padStates[1] = &pad2;
  // (players 3-4 are set up just below)

  // The core's own output, per game: without the shortcut-binding noise (keyboard bindings that do not
  // exist here, ~50 lines each time settings apply); 6000 lines in test runs, 1500 otherwise.
  ps2core_setup(DATA_DIR, [](const char* line) {
    if (!strncmp(line, "Invalid binding", 15)) return;
    if (coreLogLines < (verboseLog() ? 6000 : 1500)) { coreLogLines++; record("[core] %s", line); }
  });

  record("startup: picture, shaders");
  W = 640; H = 448;
  picture = gfx::pictureCreate(2560, 2048);  // up to 4x the largest PS2 frame (640 x 512)
  {
    string slog;
    int n = gfx::loadShaders(SHADER_FILES, slog);
    record("gpu timer: %s", gfx::gpuTimerAvailable() ? "yes" : "no");
  record("shaders: %d/%d compiled%s%s", n, (int)SHADER_FILES.size(), slog.empty() ? "" : " - ", slog.c_str());
  }
  graphImage = gfx::imageCreate(GRAPH_W, GRAPH_H);
  resetGraph();
  // (built after loading settings: the advanced toggle decides which are shown)

  {  // Diagnostics (log only): the CPU's speed at the normal clock and in the system's boost mode.
    auto bench = [] { u64 t0 = armGetSystemTick(); volatile u64 a = 1; for (u32 i = 0; i < 20000000u; i++) a = a * 6364136223846793005ULL + 1442695040888963407ULL; return armTicksToNs(armGetSystemTick() - t0) / 1e6; };
    auto clocks = [](const char* when) {
      Result rc = clkrstInitialize();
      if (R_FAILED(rc)) { record("clocks (%s): clkrst unavailable (rc %x)", when, rc); return; }
      const struct { PcvModuleId id; const char* n; } mods[] = {{PcvModuleId_CpuBus, "cpu"}, {PcvModuleId_GPU, "gpu"}, {PcvModuleId_EMC, "memory"}};
      string line = string("clocks (") + when + "):";
      for (auto& m : mods) { ClkrstSession ses; char b[64]; u32 hz = 0; Result r = clkrstOpenSession(&ses, m.id, 3); if (R_SUCCEEDED(r)) { r = clkrstGetClockRate(&ses, &hz); clkrstCloseSession(&ses); } snprintf(b, sizeof b, " %s %u MHz (rc %x)", m.n, hz / 1000000, r); line += b; if (m.id == PcvModuleId_CpuBus && !strcmp(when, "normal") && R_SUCCEEDED(r)) clkNormalHz = hz; }
      clkrstExit();
      record("%s", line.c_str());
    };
    double n = bench();
    clocks("normal");
    Result rc = appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
    svcSleepThread(200000000ULL);
    double b = bench();
    clocks("boost");
    appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
    record("cpu test: %.0f ms normal, %.0f ms boost mode (rc %x)", n, b, rc);
    {  // which system calls this process may use (reading the hints is harmless; calling a forbidden one kills the app)
      string l = "permitted svcs:"; char b2[8];
      for (u32 i = 1; i < 0x80; i++) if (envIsSyscallHinted(i)) { snprintf(b2, sizeof b2, " %02x", i); l += b2; }
      record("%s", l.c_str());
    }
  }
  loadSettings();
  autoAudioBufferDefault = audioBuffer;
  autoDisplayDefault = displayMode;
  autoFrameDelayDefault = frameDelay;
  loadDateFormat();
  loadPlaytime();
  rebuildSettings();
  scan();
  if (!sound) showToast("Audio unavailable", 4000);
  {
    // Automated test: /switch/sw2ps2/autotest.json = {"game": "<file name>", "slot": 1, "seconds": 20,
    // "runs": [{"renderer": 0, "resolution": 0, "eeSpeed": 0, "boost": true, "options": {"pcsx2_x": "y"}}, ...],
    // "repeat": false}. For each run the game starts with those settings, the save state in the slot is
    // loaded (0 = none), statistics go to log.txt every second, and after the last run the app exits.
    // The file is renamed to autotest.done.json unless "repeat" is true.
    // "agent": true keeps the app waiting for the next autotest.json after the last run, and
    // "nro": "<file in /switch/sw2ps2>" names the build that must run the test: the app starts that
    // file in its place (through the homebrew loader) when it is not the one running. Together they
    // let a new build be uploaded and tested without anyone touching the console.
    if (argc > 0 && argv[0]) appSelfPath = argv[0];
    string t, path = string(DATA_DIR) + "/autotest.json";
    remove((string(DATA_DIR) + "/autotest.finished").c_str());
    if (readText(path, t) && Json::parse(t, autoJson) && autoJson.type == Json::OBJ) {
      const Json* nro = autoJson.get("nro");
      string self = argc > 0 && argv[0] ? argv[0] : "", want = nro && nro->isStr() && !nro->s.empty() ? string(DATA_DIR) + "/" + nro->s : "";
      appSelfPath = self;
      if (!want.empty() && exists(want) && self.size() >= nro->s.size() && self.compare(self.size() - nro->s.size(), nro->s.size(), nro->s) != 0) {
        Result rc = envHasNextLoad() ? envSetNextLoad(want.c_str(), ("\"" + want + "\"").c_str()) : -1;
        record("autotest: this is '%s'; handing over to '%s' (rc %x)", self.c_str(), want.c_str(), rc);
        if (R_SUCCEEDED(rc)) quitApp = true;
      }
      if (!quitApp) {
        const Json* rp = autoJson.get("repeat"); const Json* ag = autoJson.get("agent");
        if (!(rp && rp->isBool() && rp->b)) { remove((string(DATA_DIR) + "/autotest.done.json").c_str()); rename(path.c_str(), (string(DATA_DIR) + "/autotest.done.json").c_str()); }
        autoTest = true; autoRun = 0; autoAgent = ag && ag->isBool() && ag->b;
        { const Json* rb = autoJson.get("runBase"); autoRunBase = rb && rb->isNum() ? (int)rb->n : 0; }
        if (const Json* pr = autoJson.get("probe")) if (pr->isBool() && pr->b) probeAliasing();
        if (const Json* pr = autoJson.get("glprobe")) if (pr->isBool() && pr->b) record("gl probe: %s", gfx::probeDesktopGL().c_str());
        if (const Json* pr = autoJson.get("clockProbe")) if (pr->isBool() && pr->b) probeClocks();
        record("autotest: running as '%s'%s; iteration %s; built %s %s; next-load %s", self.c_str(), autoAgent ? " (agent)" : "", SW2_ITERATION_ID, __DATE__, __TIME__, envHasNextLoad() ? "available" : "unavailable");
        if (autoAgent) writeText(string(DATA_DIR) + "/autotest.started", self + "\n" + SW2_ITERATION_ID + "\n" + __DATE__ + " " + __TIME__ + "\n");
        if (!autoStartRun()) { if (autoAgent) autoWaiting = true; else quitApp = true; }
      }
    }
  }
  padUpdate(&pad1);
  lastBits = padGetButtons(&pad1);  // ignore the A press that launched the app
  {  // relaunched to start a game in a fresh process (relaunchFor)
    string lt; Json lj; const string lp = string(DATA_DIR) + "/launch.json";
    if (readText(lp, lt)) {
      remove(lp.c_str());
      const Json* dir = Json::parse(lt, lj) ? lj.get("dir") : nullptr; const Json* name = dir ? lj.get("name") : nullptr;
      if (!autoTest && dir && name && dir->isStr() && name->isStr() && exists(dir->s + "/" + name->s)) {
        Rom r; r.dir = dir->s; r.name = name->s;
        record("started for %s (fresh process)", r.name.c_str());
        startGame(r);
      }
    }
  }

  gfx::setHighRefresh(displayMode == 1);

  while (appletMainLoop() && !quitApp) {
    if (autoTest) {
      const double activityNow = nowMs();
      if (activityNow >= autoActivityNext) {
        const Result rc = appletReportUserIsActive();
        if (!autoActivityLogged) {
          if (R_SUCCEEDED(rc)) record("autotest: periodic activity reporting enabled");
          else record("autotest: unable to report activity (rc %08x)", rc);
          autoActivityLogged = true;
        }
        autoActivityNext = activityNow + 45000;
      }
    }
    if (autoTest && !autoWaiting && running && autoMenuLoadAt >= 0) {  // as a player would: menu, then Load state
      const double t = (nowMs() - autoRunStart) / 1000.0;
      if (autoMenuLoadStage == 0 && t >= autoMenuLoadAt) { record("autotest: opening the menu at %.1f s", t); openMenu(); autoMenuLoadStage = 1; }
      else if (autoMenuLoadStage == 1 && t >= autoMenuLoadAt + 0.8) { record("autotest: Load state slot %d from the menu at %.1f s", autoMenuLoadSlot, t); stateSlot = autoMenuLoadSlot; closeMenu(); loadState(false, stateSlot); autoMenuLoadStage = 2; }
    }
    if (autoTest && !autoWaiting && running && autoSaveAt >= 0 && nowMs() - autoRunStart >= autoSaveAt * 1000) {
      autoSaveAt = -1; stateSlot = autoSaveSlot; saveState(); record("autotest: saved slot %d to %s", autoSaveSlot, statePath(autoSaveSlot).c_str());
    }
    if (autoTest && !autoWaiting && running && autoMenuOpenAt >= 0 && nowMs() - autoRunStart >= autoMenuOpenAt * 1000) {
      autoMenuOpenAt = -1; menuCat = 0; openMenu(); record("autotest: menu opened");
    }
    if (autoTest && !autoWaiting && running && autoEarlyLoadAt >= 0 && nowMs() - autoRunStart >= autoEarlyLoadAt * 1000) {
      record("autotest: Load state slot %d at %.2f s after start", autoEarlyLoadSlot, (nowMs() - autoRunStart) / 1000.0);
      autoEarlyLoadAt = -1; loadState(false, autoEarlyLoadSlot);
    }
    if (autoTest && !autoWaiting && running && !autoLiveSet.empty() && nowMs() - autoRunStart >= autoLiveSet.front().at * 1000) {
      const AutoLiveSet a = autoLiveSet.front(); autoLiveSet.pop_front();
      record("autotest: live %s -> %d at %.1f s", a.what.c_str(), a.v, (nowMs() - autoRunStart) / 1000.0);
      if (a.what == "rate") { gameRate[running->name] = a.v; applyRateLive(); }
      else if (a.what == "skip") { gameSkip[running->name] = a.v; applySkipLive(); }
      else if (a.what == "readback") { autoReadback = -1; gameReadback[running->name] = a.v; applyReadbackLive(); }
      else if (a.what == "clock") { cpuClock = a.v; applySwitchClock(); }
      else if (a.what == "eeWait") { sw2_ee_wait_cycles = (uint32_t)a.v; }
      else if (a.what == "blending" && a.v >= 0 && a.v <= (int)BLENDING_LEVELS.size()) { applyBlendingChoice(a.v); }
      if (autoLiveSet.empty()) { gameRate.erase(running->name); gameSkip.erase(running->name); gameReadback.erase(running->name); }
    }
    if (autoTest && !autoWaiting && running && !autoLiveRes.empty() && nowMs() - autoRunStart >= autoLiveRes.front().first * 1000) {
      gameRes[running->name] = autoLiveRes.front().second + 1;  // in memory only: tests never save the player's settings
      record("autotest: resolution -> %s at %.1f s", RESOLUTIONS[autoLiveRes.front().second].c_str(), (nowMs() - autoRunStart) / 1000.0);
      autoLiveRes.pop_front(); applyResolutionLive();
      if (autoLiveRes.empty()) gameRes.erase(running->name);
    }
    if (autoTest && !autoWaiting && running && !autoShots.empty() && nowMs() - autoRunStart >= autoShots.front() * 1000) {
      char nm[48]; snprintf(nm, sizeof nm, "test-run%d-%03ds", autoRunBase + autoRun, (int)autoShots.front());
      captureName = nm; captureRequested = true; autoShots.erase(autoShots.begin());
    }
    if (autoTest && !autoWaiting && (!running || nowMs() > autoTestEnd)) {
      const double elapsed = (nowMs() - autoRunStart) / 1000;
      profStop();
      if (autoTraceSpr && sw2ax_trace_spr && sw2ax_spr_counts) {
        sw2ax_trace_spr(false);
        uint64_t c[32]{}; sw2ax_spr_counts(c);
        for (int d = 0; d < 2; d++)
          record("autotest: SPR %s size <=16 %llu <=32 %llu <=64 %llu <=128 %llu <=256 %llu <=512 %llu <=1024 %llu <=2048 %llu <=4096 %llu >4096 %llu; bytes %llu wrap %llu host unaligned %llu SPR unaligned %llu", d ? "from" : "to",
                 (unsigned long long)c[d*10], (unsigned long long)c[d*10+1], (unsigned long long)c[d*10+2], (unsigned long long)c[d*10+3], (unsigned long long)c[d*10+4], (unsigned long long)c[d*10+5], (unsigned long long)c[d*10+6], (unsigned long long)c[d*10+7], (unsigned long long)c[d*10+8], (unsigned long long)c[d*10+9],
                 (unsigned long long)c[20+d], (unsigned long long)c[22+d], (unsigned long long)c[24+d], (unsigned long long)c[26+d]);
        record("autotest: SPR to sampled %llu transfers (%llu bytes), unchanged %llu transfers (%llu bytes)",
               (unsigned long long)c[28], (unsigned long long)c[29], (unsigned long long)c[30], (unsigned long long)c[31]);
      }
      if (autoTraceFrameWork && sw2ax_trace_frame_work && sw2ax_frame_work_counts) {
        sw2ax_trace_frame_work(false);
        uint64_t c[12]{}; const uint64_t hz = sw2ax_frame_work_counts(c);
        for (int b = 0; b < 2; b++) {
          const double scale = c[b] && hz ? 1000.0 / (double(hz) * c[b]) : 0.0;
          record("autotest: frame timing %s: %llu frames, work %.3f ms, GS wait %.3f ms, VU1 wait %.3f ms, target %.3f ms",
                 b ? "late" : "on-time", (unsigned long long)c[b], c[2+b]*scale, c[4+b]*scale, c[6+b]*scale, c[8+b]*scale);
        }
        record("autotest: frame timing overran %llu, unmeasured %llu", (unsigned long long)c[10], (unsigned long long)c[11]);
      }
      if (autoTraceVifCopies && sw2ax_vif_copy_counts && sw2ax_trace_vif_copies) {
        sw2ax_trace_vif_copies(false);
        uint64_t counts[14]{};
        sw2ax_vif_copy_counts(counts);
        record("autotest: VIF1 copies direct %llu, reassembled %llu, partial %llu, staged bytes %llu",
               (unsigned long long)counts[0], (unsigned long long)counts[1],
               (unsigned long long)counts[2], (unsigned long long)counts[3]);
        record("autotest: VIF1 queue size <=16 %llu <=32 %llu <=64 %llu <=128 %llu <=256 %llu <=512 %llu <=1024 %llu >1024 %llu, payload bytes %llu, metadata bytes %llu",
               (unsigned long long)counts[4], (unsigned long long)counts[5], (unsigned long long)counts[6],
               (unsigned long long)counts[7], (unsigned long long)counts[8], (unsigned long long)counts[9],
               (unsigned long long)counts[10], (unsigned long long)counts[11],
               (unsigned long long)counts[12], (unsigned long long)counts[13]);
      }
      if (autoTraceVuNotifications && sw2ax_vu_notification_counts && sw2ax_trace_vu_notifications) {
        sw2ax_trace_vu_notifications(false);
        uint64_t counts[4]{};
        sw2ax_vu_notification_counts(counts);
        record("autotest: VU notifications sleeping %llu, spinning %llu, running %llu, dead %llu",
               (unsigned long long)counts[0], (unsigned long long)counts[1],
               (unsigned long long)counts[2], (unsigned long long)counts[3]);
      }
      if (autoAwaitingState || !running)
        record("autotest: run %d measurement incomplete", autoRun);
      else {
        record("autotest: run %d measured %.3f s, %u frame calls (%.3f calls/s)",
               autoRun, elapsed, autoMeasuredFrames, autoMeasuredFrames / elapsed);
        double emu1PercentLow = 0, emuWorstMs = 0;
        if (autoFrameGaps.size() >= 10) {
          size_t k = std::max<size_t>(1, autoFrameGaps.size() / 100);
          std::partial_sort(autoFrameGaps.begin(), autoFrameGaps.begin() + k, autoFrameGaps.end(), std::greater<float>());
          double worstSum = 0;
          for (size_t i = 0; i < k; i++) worstSum += autoFrameGaps[i];
          emu1PercentLow = 1000.0 / (worstSum / k);
          emuWorstMs = autoFrameGaps[0];
        }
        double game1PercentLow = 0, gameWorstMs = 0;
        if (autoGameIntervals.size() >= 10) {
          size_t k = std::max<size_t>(1, autoGameIntervals.size() / 100);
          std::partial_sort(autoGameIntervals.begin(), autoGameIntervals.begin() + k, autoGameIntervals.end(), std::greater<float>());
          double worstSum = 0;
          for (size_t i = 0; i < k; i++) worstSum += autoGameIntervals[i];
          game1PercentLow = 1000.0 / (worstSum / k);
          gameWorstMs = autoGameIntervals[0];
        }
        record("autotest: frame lows: display 1%% low %.1f fps (worst %.1f ms, %zu samples), game 1%% low %.1f fps (worst %.1f ms, %zu samples), after %.1f s warmup",
               emu1PercentLow, emuWorstMs, autoFrameGaps.size(), game1PercentLow, gameWorstMs, autoGameIntervals.size(), AUTO_LOW_WARMUP_MS / 1000);
        record("autotest: audio refills %u, minimum queue %.1f ms, final tempo %.3f",
               audioRefills, minAudioQueue / 48.0, outputTempo);
        if (autoMeasureVulkan && sw2ax_vk_test_counters) {
          uint64_t counts[9];
          sw2ax_vk_test_counters(counts);
          for (int i = 0; i < 9; i++) counts[i] -= autoVulkanStart[i];
          record("autotest: Vulkan submits %llu, early submits %llu, synchronous readbacks %llu, readback time %.2f ms",
                 (unsigned long long)counts[0], (unsigned long long)counts[1],
                 (unsigned long long)counts[2], counts[3] / 1e6);
          record("autotest: GS pipeline lookups %llu, hot-cache hits %llu, pipeline binds %llu, texture updates %llu, render passes %llu",
                 (unsigned long long)counts[4], (unsigned long long)counts[5], (unsigned long long)counts[6],
                 (unsigned long long)counts[7], (unsigned long long)counts[8]);
        }
        if (autoTracePassEnds && sw2ax_vk_pass_end_summary) {
          sw2ax_vk_trace_pass_ends(false);
          unsigned lines[12]{}, counts[12]{};
          const auto total = sw2ax_vk_pass_end_summary(lines, counts, 12);
          record("autotest: Vulkan pass ends attributed %llu", (unsigned long long)total);
          for (unsigned i = 0; i < 12 && counts[i]; i++)
            record("autotest: Vulkan pass end GSDeviceVK.cpp:%u count %u", lines[i], counts[i]);
          if (sw2ax_vk_target_break_summary) {
            unsigned reasons[16]{}, transitions[64]{};
            sw2ax_vk_target_break_summary(reasons, transitions);
            for (unsigned bits = 1; bits < 16; bits++) {
              if (reasons[bits])
                record("autotest: Vulkan target break colour=%u depth=%u feedback=%u missing_framebuffer=%u count %u",
                       bits & 1, (bits >> 1) & 1, (bits >> 2) & 1, (bits >> 3) & 1, reasons[bits]);
            }
            for (unsigned i = 0; i < 64; i++) {
              if (transitions[i])
                record("autotest: Vulkan feedback-only transition %u -> %u count %u", i / 8, i % 8, transitions[i]);
            }
          }
        }
#ifdef SW2_VK
        if (autoMeasureHandoff) {
          const auto handoff = vkp::handoffStats();
          record("autotest: handoff copies %llu, MiB %.1f, uploads %llu, submit CPU %.2f ms, fence waits %u (%.2f ms), map %.2f ms, GL upload CPU %.2f ms (max %.3f ms)",
                 (unsigned long long)handoff.copies, handoff.bytes / 1048576.0,
                 (unsigned long long)handoff.uploads, handoff.submitsNs / 1e6,
                 vkp::slotWaits(), handoff.waitsNs / 1e6, handoff.mapNs / 1e6,
                 handoff.uploadNs / 1e6, handoff.maxUploadNs / 1e6);
        }
#endif
      }
      record("autotest: run %d done", autoRun);
      if (sw2ax_event_sources && (!&sw2_trace_ee_events || sw2_trace_ee_events)) { char es[900]; sw2ax_event_sources(es, sizeof es); record("%s", es); }
      if (&sw2_softprot_stats) record("softprot: %s, pages protected %llu, write-map hits %llu, DMA clears %llu, queued clears %llu", sw2_softprot ? "on" : "off", sw2_softprot_stats[0], sw2_softprot_stats[1], sw2_softprot_stats[2], sw2_softprot_stats[3]);
      if (autoShaderPrev >= 0) { shader = autoShaderPrev; autoShaderPrev = -1; }  // before quitGame saves anything
      if (running) quitGame();
      autoRun++;
      if (const Json* rs = autoJson.get("restart")) if (rs->isBool() && rs->b) {
        // "restart": true -- each game in a fresh process: the remaining runs go back into
        // autotest.json and the app starts itself again (the next run is then run 0 there).
        const Json* runs = autoJson.get("runs");
        if (runs && runs->isArr() && autoRun < (int)runs->a.size()) {
          Json next = autoJson; Json rest = Json::array();
          for (size_t i = autoRun; i < runs->a.size(); i++) rest.a.push_back(runs->a[i]);
          next.set("runs", rest);
          next.set("runBase", Json::number(autoRunBase + autoRun));  // screenshots keep the original run numbers
          string selfPath = appSelfPath.empty() ? string(DATA_DIR) + "/sw2ps2.nro" : appSelfPath;
          if (writeText(string(DATA_DIR) + "/autotest.json", next.dump()) && writeText(string(DATA_DIR) + "/autotest.append", "1\n")) {
            record("autotest: restarting for the next game (%d left)", (int)rest.a.size());
            envSetNextLoad(selfPath.c_str(), ("\"" + selfPath + "\"").c_str());
            quitApp = true; break;
          }
        }
      }
      if (!autoStartRun()) {
        record("autotest: all done");
        if (!autoAgent) { quitApp = true; break; }
        // Agent: release the log for reading, say so with a marker file, and wait for the next test.
        if (logFile) { fclose(logFile); logFile = nullptr; }
        const Json* testId = autoJson.get("testId");
        const string marker = testId && testId->isStr() ? "done " + testId->s : "done";
        writeText(string(DATA_DIR) + "/autotest.finished", marker + " reuse-log\n");
        autoWaiting = true; autoWaitSince = nowMs(); autoPollAt = 0;
      }
    }
    if (autoWaiting) {
      double tw = nowMs();
      if (!autoWaitSince) autoWaitSince = tw;
      padUpdate(&pad1);
      if (padGetButtons(&pad1) & HidNpadButton_Plus) { quitApp = true; break; }
      if (tw >= autoPollAt) {
        autoPollAt = tw + 700;
        // The host checks that this changes, rather than trusting an old completion marker.
        writeText(string(DATA_DIR) + "/autotest.heartbeat", std::to_string(++autoHeartbeat) + " " + appSelfPath + "\n");
        string nt; Json nj;
        if (readText(string(DATA_DIR) + "/autotest.json", nt) && Json::parse(nt, nj) && nj.type == Json::OBJ) {
          const Json* nro = nj.get("nro");
          const Json* reuse = nj.get("reuse");
          if (!nro && reuse && reuse->isBool() && reuse->b) {
            const Json* testId = nj.get("testId");
            if (!testId || !testId->isStr() || testId->s.empty() || testId->s.size() > 32 ||
                testId->s.find_first_not_of("0123456789-") != string::npos) continue;
            const string request = string(DATA_DIR) + "/autotest.json";
            const string consumed = string(DATA_DIR) + "/autotest.done.json";
            remove(consumed.c_str());
            if (rename(request.c_str(), consumed.c_str()) != 0) continue;
            const string path = string(DATA_DIR) + "/autotest-" + testId->s + ".log";
            logFile = fopen(path.c_str(), "w");
            if (!logFile) {
              writeText(string(DATA_DIR) + "/autotest.finished", "cannot write test log " + testId->s + "\n");
              continue;
            }
            if (!autoReuseLogPath.empty()) remove(autoReuseLogPath.c_str());
            autoReuseLogPath = path;
            autoJson = std::move(nj);
            autoRun = autoRunBase = 0;
            autoWaiting = false;
            record("autotest: request %s", autoJson.get("testId")->s.c_str());
            record("autotest: reusing running build '%s' (agent)", appSelfPath.c_str());
            autoStartRun();
            continue;
          }
          // A request naming a different build requires a Homebrew Loader handoff.
          string self = argc > 0 && argv[0] ? argv[0] : "", want = nro && nro->isStr() && !nro->s.empty() ? string(DATA_DIR) + "/" + nro->s : self;
          if (!exists(want)) {
            writeText(string(DATA_DIR) + "/autotest.finished", "cannot restart: requested NRO does not exist\n");
            remove((string(DATA_DIR) + "/autotest.json").c_str());
            continue;
          }
          Result rc = envHasNextLoad() ? envSetNextLoad(want.c_str(), ("\"" + want + "\"").c_str()) : -1;
          char rcText[32]; snprintf(rcText, sizeof rcText, "%08x", rc);
          writeText(string(DATA_DIR) + "/autotest.handoff", "from " + self + "\nto " + want + "\nnext-load rc " + rcText + "\n");
          if (R_FAILED(rc)) {
            writeText(string(DATA_DIR) + "/autotest.finished", "cannot restart: next-load request failed (rc " + string(rcText) + ")\n");
            remove((string(DATA_DIR) + "/autotest.json").c_str());
            continue;
          }
          remove((string(DATA_DIR) + "/autotest.heartbeat").c_str());
          quitApp = true; break;
        }
      }
      gfx::beginFrame(col(C_BG));
      gfx::text("SW2PS2 tests complete", 60, 60, 34, col(C_TEXT), gfx::BOLD);
      gfx::text("Waiting for another test request. Press + to exit.", 60, 120, 22, col(C_DIM));
      gfx::endFrame(0);
      continue;
    }
    // Order for low latency: wait for the previous frame, start this frame
    // (the display wait happens here), optional frame delay, THEN read input
    // and emulate, so the picture shows input read as late as possible.
    double ta = nowMs();
    if (frameQueue == 1) gfx::waitPrevious();
    double tb = nowMs();
    loopGapMs = loopStartPrev ? tb - loopStartPrev : 16.69;  // time since the previous refresh
    loopStartPrev = tb;
    { static int glGen = 0; if (gfx::generation() != glGen) { glGen = gfx::generation(); crtKey.clear(); applyCrt(); resetGraph(); } }  // GL was built again
    bool gameView = running && (mode == "running" || mode == "menu" || mode == "remap");
    gfx::beginFrame(gameView ? gfx::Color{0, 0, 0, 1} : col(C_BG));
    double tc = nowMs();
    // 120 Hz / VRR: present once per SNES frame (60.0988 Hz, the same rate as the NES) on a clock, so a
    // VRR screen refreshes exactly with the game (even frame times, no audio
    // stretch); input is read right after the wait (late, like frame delay).
    if (highRefresh() && running && (mode == "running" || (mode == "menu" && !pauseInMenu))) {
      double period = 1000.0 / NES_RATE, tnow = nowMs();
      if (!vrrNext || tnow - vrrNext > period * 2) vrrNext = tnow;  // start or resync after a stall
      while (nowMs() < vrrNext - 0.25) svcSleepThread(250000);
      vrrNext += period;
    } else vrrNext = 0;
    if (mode == "running" && !highRefresh() && FRAME_DELAY_MS[frameDelay] && tickMs > 12) {
      double until = tc + FRAME_DELAY_MS[frameDelay];
      while (nowMs() < until) svcSleepThread(200000);
    }
    padUpdate(&pad1); padUpdate(&pad2); padUpdate(&pad3); padUpdate(&pad4);
    singleJoyConConnected = false;
    for (PadState* p : {&pad1, &pad2, &pad3, &pad4})
      if (padIsConnected(p)) { const u32 st = padGetStyleSet(p); if (st == HidNpadStyleTag_NpadJoyLeft || st == HidNpadStyleTag_NpadJoyRight) singleJoyConConnected = true; }
    u64 bits = padGetButtons(&pad1), bits2 = padGetButtons(&pad2), bits3 = padGetButtons(&pad3), bits4 = padGetButtons(&pad4);
    if (autoTest && running) bits |= autoPressBits();
    frameDrawn = false;
    const bool wasRunning = mode == "running";
    otherPlayersMenuCombo = menuCombo(bits3) || menuCombo(bits4);  // players 3-4 can open the menu too
    if (wasRunning) runFrame(bits, bits2, false);
    updateVibration(mode == "running" && running);
    if (!wasRunning) {
      if (mode == "menu" && running && !pauseInMenu) runFrame(0, 0, true);
      handleUi(uiBits(&pad1, bits) | uiBits(&pad2, bits2) | uiBits(&pad3, bits3) | uiBits(&pad4, bits4));  // every player's controller drives the menus
    }
    double t = nowMs();
    if (toastUntil && t > toastUntil) toastUntil = 0;
    if (hintUntil && t > hintUntil) hintUntil = 0;
    if (gameView != (running && (mode == "running" || mode == "menu" || mode == "remap"))) gfx::beginFrame(running ? gfx::Color{0, 0, 0, 1} : col(C_BG));
    if (running && (mode == "running" || mode == "menu" || mode == "remap")) {
      drawGame(mode != "running");
      if (mode == "menu") drawMenu();
      if (mode == "remap") { if (dimInMenu) gfx::rect(0, 0, gfx::UI_W, gfx::UI_H, gfx::Color{0, 0, 0, 0.6f}); drawRemap(); }
    } else if (mode == "settings") drawSettings();
    else if (mode == "remap") drawRemap();
    else if (mode == "folders") drawFolderPicker();
    else drawBrowser();
    if (mode == "message") drawMessage();
    if (running && mode == "running" && (rewinding || fastForward)) {
      string tag = rewinding ? (rewind_count() ? "<< Rewind" : "<< Rewind (start)") : ">> " + FF_SPEEDS[ffSpeed];
      float w = gfx::textWidth(tag, 22, gfx::BOLD) + 28;
      gfx::rect(gfx::UI_W - 16 - w, 12, w, 40, gfx::Color{0, 0, 0, 0.6f}, 8);
      textMid(tag, gfx::UI_W - 16 - w + 14, 12, 40, 22, col(C_TEXT), gfx::BOLD);
    }
    if (captureRequested && (mode == "running" || autoTest)) {  // (tests also capture the menu)
      captureRequested = false;
      char name[64]; time_t tt = time(nullptr); struct tm lt = *localtime(&tt);
      snprintf(name, sizeof name, "%04d%02d%02d-%02d%02d%02d.png", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
      if (!captureName.empty()) { snprintf(name, sizeof name, "%s.png", captureName.c_str()); captureName.clear(); }
      string dir = string(DATA_DIR) + "/screenshots";
      mkdir(dir.c_str(), 0777);
      bool ok = gfx::screenshot((dir + "/" + name).c_str());
      record("screenshot %s: %s", name, ok ? "saved" : "failed");
      showToast(ok ? string("Screenshot saved: ") + name : "Screenshot failed");
    }
    drawToast();
    double td = nowMs();
    gfx::endFrame(frameQueue);
    double te = nowMs();
    phase[0] += tb - ta; phase[1] += tc - tb; phase[2] += t - tc; phase[3] += td - t; phase[4] += te - td; phaseN++;

    double after = nowMs();
    const bool lowWindow = autoTest && !autoAwaitingState && after >= autoRunStart + AUTO_LOW_WARMUP_MS;
    bool lowJustStarted = false;
    if (lowWindow && !autoLowWindowStarted) {
      autoLowWindowStarted = true;
      autoSkipFirstGameInterval = true;
      autoPrevSwap = 0;
      lowJustStarted = true;
    }
    double lastGapForFree = lastSwap ? after - lastSwap : 0;
    if (lastSwap) {
      double gap = after - lastSwap;
      if (gap > 5 && gap < 21) tickMs += (gap - tickMs) * 0.01;
      if (frameDrawn) {
        noteShown(after);
        if (lowWindow && !lowJustStarted) {
          if (autoPrevSwap > 0) {
            float g = (float)(after - autoPrevSwap);
            if (g > 0) autoFrameGaps.push_back(g);
          }
          autoPrevSwap = after;
        }
      }
    } else if (frameDrawn) {
      noteShown(after);
      if (lowWindow) autoPrevSwap = after;
    }
    if (running && sw2_game_frame_intervals && (statsLevel == 3 || (autoTest && autoGameShotTarget > 0))) {  // game-frame events
      static unsigned gfSeq = 0; float iv[64];
      int n = sw2_game_frame_intervals(&gfSeq, iv, 64);
      if (graphSkipBacklog) { graphSkipBacklog = false; n = 0; }
      if (autoTest && !autoAwaitingState && autoGameShotTarget > 0) {
        if (autoGameShotPriming) autoGameShotPriming = false;
        else autoGameShotFrames += n;
        if (autoGameShotFrames >= autoGameShotTarget) {
          char nm[64]; snprintf(nm, sizeof nm, "test-run%d-gameframe%d", autoRunBase + autoRun, autoGameShotTarget);
          captureName = nm; captureRequested = true;
          record("autotest: game-frame screenshot target %d reached at %.3f s (%d frames)", autoGameShotTarget, (after - autoRunStart) / 1000, autoGameShotFrames);
          autoGameShotTarget = 0;
        }
      }
      for (int i = 0; i < n; i++) if (iv[i] > 0) {
        if (statsLevel == 3 && iv[i] < 1000) drawGraph(iv[i]);
        if (statsLevel == 3 && lowWindow && !lowJustStarted) {
          if (autoSkipFirstGameInterval) autoSkipFirstGameInterval = false;
          else autoGameIntervals.push_back(iv[i]);
        }
      }
    } else if (frameDrawn) {  // (no game-frame times: new frames from the core)
      static double lastShownAt = 0;
      if (statsLevel == 3 && lastShownAt > 0 && after - lastShownAt < 1000) drawGraph(after - lastShownAt);
      lastShownAt = after;
    }
    lastSwap = after;
    if (!vrrNext && lastGapForFree > 3 && lastGapForFree < 40) freeTickMs = freeTickMs ? freeTickMs + (lastGapForFree - freeTickMs) * 0.05 : lastGapForFree;
    pollDisplay(after);
    if (!autoTest && running && (mode == "running" || (mode == "menu" && !pauseInMenu)))
      playAccumSec += std::min(loopGapMs, 100.0) / 1000.0;
    if (running && after - playSavedAt >= 300000) { playSavedAt = after; flushPlaytime(running->name); }
    swapAcc += gfx::swapMs; waitAcc += gfx::waitMs; swapN++; if (gfx::swapMs > swapMax) swapMax = gfx::swapMs;
    updateStats(after);
  }

  if (running) quitGame();
  if (autoAgent) remove((string(DATA_DIR) + "/autotest.heartbeat").c_str());
  if (sw2ax_list_threads) sw2ax_list_threads("after the last game was unloaded");
  if (sw2ax_exit_trace) sw2ax_exit_trace("main: game unloaded, shutting down");
  saveSettings();
  gfx::imageDestroy(graphImage);
  gfx::pictureDestroy(picture);
  audio::shutdown();
  gfx::shutdown();
  record("exit");
  if (logFile) fclose(logFile);
  romfsExit();
  if (sw2ax_exit_trace) sw2ax_exit_trace("main: returning");
  return 0;
}
