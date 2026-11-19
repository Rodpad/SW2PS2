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
#include <map>
#include <malloc.h>
#include <functional>
#include <string>
#include <vector>

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

/* ---------------- State ---------------- */
// Defaults (the author's chosen setup, 2026-11-14).
static int aspectMode = 2, scaleMode = 1, cropMode = 0, crtStyle = 0, paletteMode = 0, statsLevel = 0;
static int runAhead = 0, frameDelay = 0, audioBuffer = 1, displayMode = 1, ffMode = 0, ffSpeed = 1;
static bool rewindOn = false;
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
    {"godofwarii", 3, 0, false},            // 49 -> 57 emu fps, 53 shown
    {"shadowofthecolossus", 1, 0, false},   // 35 -> 60 emu fps, 30 shown (moderate skip: 25)
    {"thesimpsonshitandrun", 1, 0, false},  // 33 -> 50 (PAL full speed), 38 shown (75% + max skip: 18)
    {"armoredcore3", 0, 0, true},           // 46 -> 60, all 60 shown (readbacks only, no CPU change)
    {"burnoutrevenge", 2, 3, false},        // the old list's setting
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
static int autoRate(const string& name) {
  const SpeedDefault* d = speedDefault(name); int r = d ? std::max(d->rate, ps2EeSpeedGlobal()) : ps2EeSpeedGlobal();
  return std::min(r, 3);
}
static int autoSkip(const string& name) { const SpeedDefault* d = speedDefault(name); return d ? std::max(d->skip, ps2CycleSkip) : ps2CycleSkip; }
static int effectiveSkip(const string& name) {
  auto it = gameSkip.find(name); if (it != gameSkip.end() && it->second > 0) return it->second - 1;
  if (const SpeedDefault* d = speedDefault(name)) return std::max(d->skip, ps2CycleSkip);
  return ps2CycleSkip;
}
static int ps2Renderer = 0, ps2Resolution = 1, ps2EeSpeed = 0, ps2Fpu = 0;
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
static const vector<string> ADVANCED_SETTINGS = {"Emulated CPU speed (next start)", "CPU boost mode (next start)", "Scale", "Overscan", "Signal", "Curvature", "Corner radius", "Corner softness", "Vignette", "Vignette size",
    "Brightness boost", "Stats", "Run-ahead", "Frame delay (60 Hz)", "Audio buffer", "Display", "Fast-forward speed", "Dim game in menu", "Audio filter", "Turbo speed"};
static const vector<string> ADVANCED_MENU = {"Run-ahead", "Stats", "Scale", "Overscan", "Signal", "Curvature", "Corner radius", "Corner softness", "Vignette",
    "Vignette size", "Brightness boost", "Settings for", "Cheats...", "Resolution"};
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
static vector<string> recentGames, favourites;
static const vector<string> VIB_LEVELS = {"Off", "Low", "Normal", "High"};
static const float VIB_GAIN[4] = {0.0f, 0.5f, 1.0f, 1.6f};
static int vibLevel = 2;  // index into VIB_LEVELS: Normal by default
static bool vibration = true;  // (settings before vibLevel: on = Normal, off = Off)
static vector<string> widescreenGames;  // games shown at 16:9, with the core's widescreen patch from their next start

struct Rom { string name, dir; };  // file name (.sfc, .smc or .zip) and its folder
static vector<Rom> allRoms, roms;
static int selected = 0, top = 0, listView = 0, btnIndex = 0;
static string focusArea = "list";
static double exitArmed = 0;

struct GameVals { int aspect, scale, crop, crt, palette, faces[NFACE], faces2p[NFACE], turbo, volume, filter, corner, cornerSoft, vignette, vignetteSize, curve, shader, signal, boost; };
struct Cheat { string code, desc; bool on; };
struct Patch { long offset; uint8_t orig; };
struct Running {
  string name, base, stateBase, settingsBase;  // base: <data>/<game> (cheats, saves); states and per-game settings in their own folders
  bool battery = false, perGame = false;
  uint32_t sramHash = 0;
  double sramCheck = 0;
  GameVals globalVals;
  Json slots = Json::object();
  vector<Cheat> cheats;
  vector<Patch> patches;
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
// Automated test (autotest.json, see main): overrides apply to one run and are not saved.
static bool autoTest = false, autoAgent = false, autoWaiting = false;
static double autoTestEnd = 0, autoWaitSince = 0, autoPollAt = 0;
static vector<std::pair<string, string>> autoOptions;  // extra core options for the current test run
static string autoLabel;
static std::deque<std::pair<double, int>> autoLiveRes;  // "liveRes": [[seconds, resolution index], ...]: change resolution mid-game, as from the menu
static double autoSaveAt = -1; static int autoSaveSlot = 4;  // "saveState": [seconds, slot]: save a state (tests the states folder)
static double autoMenuOpenAt = -1;  // "menuOpen": seconds: open the in-game menu and leave it open (for screenshots)
static double autoEarlyLoadAt = -1; static int autoEarlyLoadSlot = 1;  // "earlyLoad": [seconds, slot]: Load state straight after boot, no menu
static double autoMenuLoadAt = -1; static int autoMenuLoadSlot = 1, autoMenuLoadStage = 0;  // "menuLoad": [seconds, slot]: open the menu, then Load state from it
static vector<double> autoShots;  // seconds into the run at which to take screenshots ("shots")
static double autoRunStart = 0;  // shown at the bottom of the screen during a test run
static int coreLogLines = 0;
static void record(const char* fmt, ...) {
  if (!logFile) return;
  va_list ap; va_start(ap, fmt); vfprintf(logFile, fmt, ap); va_end(ap); fputc('\n', logFile); fflush(logFile);
}
void sw2_log_line(const char* line) { record("%s", line); }  // for other files (vkpresent.cpp)

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
  j.set("recent", strings(recentGames)); j.set("favourites", strings(favourites)); j.set("widescreen", strings(widescreenGames));
  j.set("romDir", Json::string(romDir)); j.set("biosDir", Json::string(biosDir));
  j.set("advanced", Json::boolean(showAdvanced));
  j.set("runAhead", Json::number(runAhead)); j.set("frameDelay", Json::number(frameDelay)); j.set("audioBuffer", Json::number(audioBuffer));
  j.set("display", Json::number(displayMode)); j.set("rewind", Json::boolean(rewindOn)); j.set("ffMode", Json::number(ffMode)); j.set("ffSpeed", Json::number(ffSpeed));
  j.set("ps2Renderer", Json::number(ps2Renderer)); j.set("ps2Resolution", Json::number(ps2Resolution)); j.set("ps2EeSpeed", Json::number(ps2EeSpeed)); j.set("ps2Fpu", Json::number(ps2Fpu)); j.set("ps2CycleSkip", Json::number(ps2CycleSkip));
  { Json rb = Json::object(); for (auto& kv : gameReadback) if (kv.second) rb.set(kv.first, Json::number(kv.second)); j.set("gameReadback", rb); }
  { Json r = Json::object(), k = Json::object(); for (auto& kv : gameRate) if (kv.second) r.set(kv.first, Json::number(kv.second)); for (auto& kv : gameSkip) if (kv.second) k.set(kv.first, Json::number(kv.second)); j.set("gameRate", r); j.set("gameSkip", k);
    Json rs = Json::object(); for (auto& kv : gameRes) if (kv.second) rs.set(kv.first, Json::number(kv.second)); j.set("gameRes", rs); j.set("res2x", Json::boolean(true)); j.set("zfastDefault", Json::boolean(true)); } j.set("fastReadback", strings(fastReadbackGames)); j.set("cpuClock", Json::number(cpuClock)); j.set("gpuClock", Json::number(gpuClock));
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
  ps2Renderer = 0;  // always the GPU (Vulkan) renderer: software was far too slow to be worth offering ps2Resolution = pick(v.get("ps2Resolution"), RESOLUTIONS.size(), ps2Resolution);
  ps2EeSpeed = pick(v.get("ps2EeSpeed"), EE_SPEEDS.size(), ps2EeSpeed);
  ps2Fpu = pick(v.get("ps2Fpu"), FPU_MODES.size(), ps2Fpu);
  ps2CycleSkip = pick(v.get("ps2CycleSkip"), CYCLE_SKIPS.size(), ps2CycleSkip);

  if (const Json* r = v.get("gameRate")) if (r->type == Json::OBJ) { gameRate.clear(); for (auto& kv : r->o) if (kv.second.isNum()) gameRate[kv.first] = pick(&kv.second, EE_SPEEDS.size() + 1, 0); }
  if (const Json* r = v.get("gameReadback")) if (r->type == Json::OBJ) { gameReadback.clear(); for (auto& kv : r->o) if (kv.second.isNum()) gameReadback[kv.first] = pick(&kv.second, 3, 0); }
  if (const Json* r = v.get("gameRes")) if (r->type == Json::OBJ) { gameRes.clear(); for (auto& kv : r->o) if (kv.second.isNum()) gameRes[kv.first] = pick(&kv.second, RESOLUTIONS.size() + 1, 0); }
  if (!v.get("zfastDefault") && shader == 2) shader = 6;  // zfast CRT became the default shader (2026-11-19); once, for settings saved on the old default
  if (!v.get("res2x") && ps2Resolution == 0) ps2Resolution = 1;  // 2x became the default (measured free in every game, 2026-11-19); once, for settings saved before
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
static Json defaultSettings;  // the built-in defaults, captured before the settings file is read
static bool displaySaved = false;  // the file chose a display mode (otherwise 120 Hz only where available)
static void loadSettings() {
  defaultSettings = settingsJson(gameValues());
  string t; Json v;
  if (!readText(SETTINGS(), t) || !Json::parse(t, v) || v.type != Json::OBJ) return;
  applySettingsJson(v);
  displaySaved = v.get("display") != nullptr;
  if (const Json* d = v.get("frameDelay")) if (d->isNum() && d->n >= FRAME_DELAYS.size()) frameDelay = (int)FRAME_DELAYS.size() - 1;  // 12 ms -> 8 ms cap
}
// Settings -> Reset all settings: back to the built-in defaults (recent games,
// favourites and per-game settings files are kept).
static void resetAllSettings() {
  vector<string> rec = recentGames, fav = favourites, ws = widescreenGames, fr = fastReadbackGames;
  applySettingsJson(defaultSettings);
  recentGames = rec; favourites = fav; widescreenGames = ws; fastReadbackGames = fr;
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
static void scan() {
  allRoms.clear();
  // One entry per game name (a .nes file wins over a .zip of the same name).
  for (const string& dir : {romDir}) {
    vector<Rom> found;
    if (DIR* d = opendir(dir.c_str())) {
      while (struct dirent* e = readdir(d)) {
        string n = e->d_name;
        if (e->d_type == DT_DIR || n[0] == '.') continue;
        if (isRomExt(n)) found.push_back(Rom{n, dir});
      }
      closedir(d);
    }
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
  const bool wide = running && contains(widescreenGames, running->name) && aspectMode == 2;  // 4:3 becomes 16:9 for a widescreen game
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
    if (const uint32_t* px = vkp::grab(&vw, &vh)) gfx::pictureUpload(picture, px, vw, vh, vw);
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
// Frame-time graph: 240 frames, 2 px each, scrolling left; 16.7 ms in the middle.
enum { GRAPH_STEP = 2, GRAPH_FRAMES = 240, GRAPH_W = GRAPH_STEP * GRAPH_FRAMES, GRAPH_H = 40 };
static const double GRAPH_MAX_MS = 33.4;
static uint32_t graphPix[GRAPH_W * GRAPH_H];
static gfx::Image* graphImage = nullptr;
static int graphPrevY = -1;
static void resetGraph() {
  graphPrevY = -1;
  int mid = GRAPH_H - (int)lround(16.7 / GRAPH_MAX_MS * GRAPH_H);
  for (int i = 0; i < GRAPH_W * GRAPH_H; i++) graphPix[i] = 0;
  for (int x = 0; x < GRAPH_W; x++) graphPix[mid * GRAPH_W + x] = 0x4dffffff;
}
static void drawGraph(double ms) {
  int S = GRAPH_STEP, mid = GRAPH_H - (int)lround(16.7 / GRAPH_MAX_MS * GRAPH_H);
  int y = GRAPH_H - 1 - (int)lround(std::min(ms, GRAPH_MAX_MS) / GRAPH_MAX_MS * (GRAPH_H - 2));
  int y0 = graphPrevY < 0 ? y : graphPrevY, lo = std::min(y0, y), hi = std::max(y0, y);
  for (int r = 0; r < GRAPH_H; r++) {
    uint32_t* row = graphPix + r * GRAPH_W;
    memmove(row, row + S, (GRAPH_W - S) * 4);
    // a line: the first column joins the previous point, the second holds the new value
    row[GRAPH_W - 2] = (r >= lo && r <= hi) ? 0xff6ee26e : r == mid ? 0x4dffffff : 0;
    row[GRAPH_W - 1] = r == y ? 0xff6ee26e : r == mid ? 0x4dffffff : 0;
  }
  graphPrevY = y;
  if (graphImage) gfx::imageUpload(graphImage, graphPix);
}

static double phase[5] = {0, 0, 0, 0, 0};  // per-second sums: fence wait, frame start (display wait), emulation, drawing, swap
static int phaseN = 0, sessionLog = 0;
static double swapAcc = 0, waitAcc = 0, swapMax = 0;
static int swapN = 0;
static double audioInCount = 0;  // audio frames the core produced (per-second stats)
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

/* ---------------- Cheats ---------------- */
// SNES Game Genie ("XXXX-XXXX") and Pro Action Replay ("XXXXXXXX") codes,
// decoded and applied by Snes9x's cheat engine.
static string normCode(const string& in) {
  string t;
  for (char c : in) if (isxdigit((unsigned char)c) || c == '-' || c == ':') t += (char)toupper((unsigned char)c);
  return t;
}
static bool codeValid(const string& c) { return !c.empty() && C->cheat_valid(c.c_str()); }
static int applyCheats() {
  if (!running) return 0;
  vector<string> on; vector<const char*> ptrs;
  for (auto& c : running->cheats) if (c.on && codeValid(c.code)) on.push_back(c.code);
  for (auto& c : on) ptrs.push_back(c.c_str());
  return C->set_cheats(ptrs.data(), (int)ptrs.size());
}
static void saveCheats() {
  Json a = Json::array();
  for (auto& c : running->cheats) { Json o = Json::object(); o.set("code", Json::string(c.code)); o.set("desc", Json::string(c.desc)); o.set("on", Json::boolean(c.on)); a.a.push_back(o); }
  if (!writeText(running->base + ".cheats.json", a.dump())) record("Cheats save failed");
}
static void loadCheats(const string& romName, const string& romDir) {
  running->cheats.clear(); running->patches.clear();
  string t; Json j;
  if (readText(running->base + ".cheats.json", t) && Json::parse(t, j) && j.isArr())
    for (auto& c : j.a) {
      const Json* code = c.get("code");
      if (!code || !code->isStr() || !codeValid(code->s)) continue;
      const Json* d = c.get("desc"); const Json* on = c.get("on");
      running->cheats.push_back(Cheat{code->s, d && d->isStr() ? d->s : "", on && on->isBool() && on->b});
    }
  // Codes next to the ROM: <game>.gg, one "CODE description" per line.
  if (readText(romDir + "/" + stripNes(romName) + ".gg", t)) {
    size_t pos = 0;
    while (pos <= t.size()) {
      size_t e = t.find('\n', pos); if (e == string::npos) e = t.size();
      string line = t.substr(pos, e - pos); pos = e + 1;
      while (!line.empty() && isspace((unsigned char)line.back())) line.pop_back();
      size_t s = 0; while (s < line.size() && isspace((unsigned char)line[s])) s++;
      size_t k = s; while (k < line.size() && !isspace((unsigned char)line[k])) k++;
      string code = normCode(line.substr(s, k - s));
      if (!codeValid(code)) continue;
      string desc = line.substr(k); size_t ds = desc.find_first_not_of(" \t"); desc = ds == string::npos ? "" : desc.substr(ds);
      if (desc.size() > 40) desc.resize(40);
      bool have = false; for (auto& c : running->cheats) if (c.code == code) have = true;
      if (!have) running->cheats.push_back(Cheat{code, desc, false});
    }
  }
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
  if (!ps2core_find_bios(biosDir.c_str(), bios, sizeof bios)) {
    const string shown = biosDir.rfind("sdmc:", 0) == 0 ? biosDir.substr(5) : biosDir;
    showMessage("No PS2 BIOS found.\n\nCopy the BIOS dump from your own PS2 to\n\n    " + shown + "/\n\non the SD card (or choose another folder in Settings -> App -> BIOS folder),\nthen start the game again.", [] { mode = "browser"; });
    return;
  }
  if (&sw2_bios_dir) snprintf(sw2_bios_dir, 512, "%s", biosDir.rfind("sdmc:", 0) == 0 ? biosDir.c_str() + 5 : biosDir.c_str());  // the core wants "/bios/ps2"
#ifdef SW2_VK
  bool gpu = ps2Renderer == 2;  // OpenGL
  const bool vulkan = ps2Renderer == 0;  // the core renders with Vulkan; vkpresent brings its frames to the GL picture
  ps2core_use_vulkan(vulkan);
  sw2_osd_stats = 0;  // the front end's own stats show over a Vulkan game too (its frame comes to the GL picture)
#else
  bool gpu = ps2Renderer == 0;
  const bool vulkan = false;
#endif
  const int res = effectiveRes(rom.name);
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
  int rate = effectiveRate(rom.name), skip = effectiveSkip(rom.name);
  tuneQuietUntil = nowMs() + 12000; slowNoticeShown = false; tuneWin.clear();
  ps2core_option("pcsx2_ee_cycle_rate", EE_SPEED_VALUES[rate]);
  ps2core_option("pcsx2_ee_cycle_skip", CYCLE_SKIP_VALUES[skip]);
  { bool fast;
    auto rb = gameReadback.find(rom.name);
    if (autoReadback >= 0) fast = autoReadback == 1;
    else if (rb != gameReadback.end() && rb->second > 0) fast = rb->second == 2;
    else if (contains(fastReadbackGames, rom.name)) fast = true;
    else { const SpeedDefault* d = speedDefault(rom.name); fast = d && d->fastReadback; }
    ps2core_option("pcsx2_hw_download_mode", fast ? "Unsynchronized" : "Accurate"); }
  ps2core_option("pcsx2_widescreen_hint", contains(widescreenGames, rom.name) ? "enabled (16:9)" : "disabled");  // the core's patch, for games without their own widescreen setting
  for (auto& o : autoOptions) ps2core_option(o.first.c_str(), o.second.c_str());
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
  appletSetCpuBoostMode(cpuClock == 1 ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
  if (cpuClock == 2) clockMaxApply(true);
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
  if (readText(running->settingsBase + ".settings.json", t) && Json::parse(t, j) && j.type == Json::OBJ) { setGameValuesJson(j); running->perGame = true; }
  recentGames.erase(std::remove(recentGames.begin(), recentGames.end(), rom.name), recentGames.end());
  recentGames.insert(recentGames.begin(), rom.name);
  if ((int)recentGames.size() > RECENT_MAX) recentGames.resize(RECENT_MAX);
  saveSettings();
  applyEmuOptions();
  loadCheats(rom.name, rom.dir);
  if (applyCheats()) showToast("Cheats active");
  fitPicture(); crtKey.clear(); applyCrt();
  hintUntil = nowMs() + 6000;
  bootedFrames = 0;
  if (autoLoadState) loadState(true, newestSlot());
  resetClock(); mode = "running"; comboLatch = true;
  resetRewind(); emuAcc = 0; ffLatched = false; fastForward = false;
  setAudioRatio(1); audioInteg = 0; pendEma = AUDIO_TARGET_FRAMES[audioBuffer];
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
static PadState* padStates[2];
static void setPs2Pad(int player, u64 held, bool blank) {
  Ps2Input& p = ps2Input[player];
  p.buttons = 0; p.lx = p.ly = p.rx = p.ry = 0;
  if (blank || !padStates[player]) return;
  // libretro joypad ids: B0 Y1 SELECT2 START3 UP4 DOWN5 LEFT6 RIGHT7 A8 X9 L10 R11 L2 12 R2 13 L3 14 R3 15
  static const struct { u64 sw; int id; } map[] = {{HidNpadButton_B, 0}, {HidNpadButton_Y, 1}, {HidNpadButton_Minus, 2}, {HidNpadButton_Plus, 3},
      {HidNpadButton_Up, 4}, {HidNpadButton_Down, 5}, {HidNpadButton_Left, 6}, {HidNpadButton_Right, 7}, {HidNpadButton_A, 8}, {HidNpadButton_X, 9},
      {HidNpadButton_L, 10}, {HidNpadButton_R, 11}, {HidNpadButton_ZL, 12}, {HidNpadButton_ZR, 13}, {HidNpadButton_StickL, 14}, {HidNpadButton_StickR, 15}};
  for (auto& m : map) if (held & m.sw) p.buttons |= 1u << m.id;
  HidAnalogStickState l = padGetStickPos(padStates[player], 0), r = padGetStickPos(padStates[player], 1);
  p.lx = (int16_t)l.x; p.ly = (int16_t)std::max(-32767, -l.y); p.rx = (int16_t)r.x; p.ry = (int16_t)std::max(-32767, -r.y);
}
// In-game menu: L + R with both sticks clicked (games use L3 + R3 alone, e.g.
// Katamari Damacy); every button still belongs to the game. In the menu a lone tap of L / R
// cycles its categories (see the "menu" input).
static const u64 MENU_COMBO = HidNpadButton_L | HidNpadButton_R | HidNpadButton_StickL | HidNpadButton_StickR;
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
static const vector<string> MENU_CATS = {"Game", "Emulation", "Picture"};
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
static void openCodeEditor();
static void rebuildSettings();
static void openCheatsMenu() {
  vector<MenuItem> items;
  for (size_t i = 0; i < running->cheats.size(); i++) {
    Cheat& c = running->cheats[i];
    items.push_back(opt(c.code + (c.desc.empty() ? "" : "  " + c.desc), [] { return OFF_ON; },
                        [i] { return running->cheats[i].on ? 1 : 0; }, [i](int v) { running->cheats[i].on = v != 0; },
                        [] { applyCheats(); saveCheats(); }));
  }
  items.push_back(act("Add code", [] { openCodeEditor(); }));
  if (!running->cheats.empty()) items.push_back(act("Remove last code", [] { running->cheats.pop_back(); applyCheats(); saveCheats(); openCheatsMenu(); }));
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
      opt("CPU speed (next start)", [] { vector<string> a = {"Auto (" + EE_SPEEDS[autoRate(running->name)] + ")"}; for (auto& x : EE_SPEEDS) a.push_back(x); return a; },
          [] { auto it = gameRate.find(running->name); return it == gameRate.end() ? 0 : it->second; },
          [](int v) { gameRate[running->name] = v; }, [] { saveSettings(); showToast("Applies from this game's next start", 3000); }),
      opt("CPU cycle skip (next start)", [] { vector<string> a = {"Auto (" + CYCLE_SKIPS[autoSkip(running->name)] + ")"}; for (auto& x : CYCLE_SKIPS) a.push_back(x); return a; },
          [] { auto it = gameSkip.find(running->name); return it == gameSkip.end() ? 0 : it->second; },
          [](int v) { gameSkip[running->name] = v; }, [] { saveSettings(); showToast("Applies from this game's next start", 3000); }),
      opt("GPU readbacks (next start)", [] { const SpeedDefault* d = speedDefault(running->name); return vector<string>{string("Auto (") + (d && d->fastReadback ? "Fast" : "Accurate") + ")", "Accurate", "Fast"}; },
          [] { auto it = gameReadback.find(running->name); if (it != gameReadback.end()) return it->second; return contains(fastReadbackGames, running->name) ? 2 : 0; },
          [](int v) { fastReadbackGames.erase(std::remove(fastReadbackGames.begin(), fastReadbackGames.end(), running->name), fastReadbackGames.end()); gameReadback[running->name] = v; },
          [] { saveSettings(); showToast("Applies from this game's next start", 3000); }),
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
    static const vector<string> EMU = {"CPU speed (next start)", "CPU cycle skip (next start)", "GPU readbacks (next start)", "Resolution", "Widescreen", "Stats"};
    static const vector<string> PIC = {"Aspect", "Scale", "Overscan", "Signal", "Shader", "Brightness boost", "Curvature", "Corner radius", "Corner softness",
                                       "Vignette", "Vignette size", "Settings for"};
    auto catOf = [](const string& l) { return contains(EMU, l) ? 1 : contains(PIC, l) ? 2 : 0; };
    for (auto& m : items) if (m.label != "Run-ahead" && m.label != "Cheats..." && (adv || !contains(ADVANCED_MENU, m.label)) && catOf(m.label) == cat) kept.push_back(m);
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

/* ---------------- Cheat code editor ---------------- */
// Eight hex digits; Y switches between Game Genie (XXXX-XXXX) and Pro Action
// Replay (XXXXXXXX). Left/Right move, Up/Down change the digit.
static const char* HEX = "0123456789ABCDEF";
static int ggLetters[8], ggPos = 0, ggType = 0;  // 0 Game Genie, 1 Pro Action Replay
static void openCodeEditor() { for (int i = 0; i < 8; i++) ggLetters[i] = 0; ggPos = 0; ggType = 0; mode = "ggedit"; }
static string ggEditCode() {
  string s; for (int i = 0; i < 8; i++) { if (ggType == 0 && i == 4) s += '-'; s += HEX[ggLetters[i]]; }
  return s;
}
static void closeCodeEditor() { mode = "menu"; openCheatsMenu(); }
static void handleCodeEditor(const Nav& ev) {
  if (ev.pressed & B_LEFT) ggPos = std::max(0, ggPos - 1);
  if (ev.pressed & B_RIGHT) ggPos = std::min(7, ggPos + 1);
  if (ev.up) ggLetters[ggPos] = (ggLetters[ggPos] + 1) % 16;
  if (ev.down) ggLetters[ggPos] = (ggLetters[ggPos] + 15) % 16;
  if (ev.pressed & B_Y) ggType ^= 1;
  if (ev.pressed & B_A) {
    string code = ggEditCode();
    if (!codeValid(code)) { showToast("That code is not valid"); return; }
    running->cheats.push_back(Cheat{code, "", true}); applyCheats(); saveCheats(); showToast("Code " + code + " added");
    closeCodeEditor();
  } else if (ev.pressed & B_B) closeCodeEditor();
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
static bool settingsByA = false;  // the row's action came from A (not left/right): for rows that do something
struct SettingItem { string label; std::function<vector<string>()> choices; std::function<int()> get; std::function<void(int)> set; };
static vector<SettingItem> settingsItems() {
  return {
      {"ROM folder", [] { return vector<string>{romDir.rfind("sdmc:", 0) == 0 ? romDir.substr(5) : romDir}; }, [] { return 0; }, [](int) { openFolderPicker(); }},
      {"BIOS folder", [] { return vector<string>{biosDir.rfind("sdmc:", 0) == 0 ? biosDir.substr(5) : biosDir}; }, [] { return 0; }, [](int) { openFolderPicker(true); }},
      {"Resolution (next start)", [] { return RESOLUTIONS; }, [] { return ps2Resolution; }, [](int v) { ps2Resolution = v; }},
      {"Emulated CPU speed (next start)", [] { return EE_SPEEDS; }, [] { return ps2EeSpeed; }, [](int v) { ps2EeSpeed = v; }},
      {"Emulated CPU cycle skip (next start)", [] { return CYCLE_SKIPS; }, [] { return ps2CycleSkip; }, [](int v) { ps2CycleSkip = v; }},
      {"Vibration", [] { return VIB_LEVELS; }, [] { return vibLevel; }, [](int v) { vibLevel = v; }},
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
  static const vector<string> EMU = {"Resolution (next start)", "Emulated CPU speed (next start)", "Emulated CPU cycle skip (next start)", "Switch 2 CPU clock (next start)", "Auto-load save state"};
  static const vector<string> PIC = {"Aspect", "Scale", "Overscan", "Signal", "Shader", "Brightness boost", "Curvature", "Corner radius", "Corner softness", "Vignette", "Vignette size", "Display", "Frame delay (60 Hz)"};
  static const vector<string> SND = {"Volume", "Audio buffer", "Vibration"};
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
    if (!contains(NOT_HERE, it.label) && (adv || !contains(ADVANCED_SETTINGS, it.label)) && settingsCatOf(it.label) == cat) items.push_back(it);
  if (adv && cat == 3)
    items.push_back({"Reset all settings", [] { return vector<string>{"Press A", "Press A again to confirm"}; }, [] { return resetArmed ? 1 : 0; },
                     [](int v) { if (resetArmed) { resetArmed = false; resetAllSettings(); gfx::setHighRefresh(displayMode == 1); rebuildSettings(); settingsIndex = 0; settingsTop = 0; showToast("All settings reset to defaults"); } else resetArmed = true; }});
  return items;
}

/* ---------------- Drawing helpers ---------------- */
static const float LH = 1.15f;  // CSS "normal" line height, approximately
// Text vertically centred in a box.
static float textMid(const string& s, float x, float y, float h, float size, Color c, gfx::Font f = gfx::REGULAR) {
  return gfx::text(s, x, y + (h - size * 1.25f) / 2, size, c, f);
}
// "< value >" selector, right-aligned at xr; returns its width.
static float valueBox(const string& t, float xr, float y, float h, float size, bool active) {
  Color c = col(active ? C_TEXT : C_DIM);
  float wl = gfx::textWidth("<", size, gfx::BOLD), wt = gfx::textWidth(t, size), wr = gfx::textWidth(">", size, gfx::BOLD);
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
static const char* BROWSER_KEYS[] = {"Y", "X", "B"};  // gamepad shortcut for each button
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
  drawTabBar(cx, cy, LIST_VIEWS, listView, focusArea == "tabs" ? listView : -1);  // (L and R cycle them)
  cy += 36 + 10;
  // Column headings
  // Columns from the right: Time played, Recent, Favourite (centred tags).
  const float TIME_W = 150, REC_W = 100, FAV_W = 110;
  const float colR = cx + listInnerW - 18, recC = colR - TIME_W - REC_W / 2, favC = recC - REC_W / 2 - FAV_W / 2;
  if (!roms.empty()) {
    gfx::text("Game", cx + 22, cy + 1, 14, col(C_DIM), gfx::BOLD);
    float hw = gfx::textWidth("Time played", 14, gfx::BOLD);
    gfx::text("Time played", colR - hw, cy + 1, 14, col(C_DIM), gfx::BOLD);
    gfx::text("Recent", recC - gfx::textWidth("Recent", 14, gfx::BOLD) / 2, cy + 1, 14, col(C_DIM), gfx::BOLD);
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
      bool fav = contains(favourites, r.name), rec = contains(recentGames, r.name);
      string played = playLabel(r.name);
      if (!played.empty()) { float pw = gfx::textWidth(played, 16); textMid(played, rx + rw - 12 - pw, ry, ROW_H, 16, col(C_DIM)); }
      if (fav) {
        float tw = gfx::textWidth("Favourite", 14) + 16;
        gfx::rect(favC - tw / 2, ry + 9, tw, 22, gfx::rgb(0xf2c94c), 4);
        textMid("Favourite", favC - tw / 2 + 8, ry + 9, 22, 14, gfx::rgb(0x1a1300));
      }
      if (rec) {
        float tw = gfx::textWidth("Recent", 14) + 16;
        gfx::rect(recC - tw / 2, ry + 9, tw, 22, Color{0, 0, 0, 0}, 4, 1, col(C_LINE));
        textMid("Recent", recC - tw / 2 + 8, ry + 9, 22, 14, col(C_DIM));
      }
      textMid(gfx::ellipsize(stripNes(r.name), favC - FAV_W / 2 - rx - 28, 20), rx + 16, ry, ROW_H, 20, col(C_TEXT));
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
    float kt = gfx::textWidth(BROWSER_KEYS[i], 15);
    textMid(BROWSER_KEYS[i], lx + (kw - kt) / 2, cy + 8, kw, 15, col(C_TEXT));
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
  panelHeader(cx, cy, pickForBios ? "BIOS folder" : "ROM folder", "A: open   B: cancel");
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
    float w = gfx::textWidth("L + R + L3 + R3: Menu", 16);
    gfx::text("L + R + L3 + R3: Menu", gfx::UI_W - 16 - w, gfx::UI_H - 12 - 20, 16, gfx::rgb(0x8891a4));
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

static void drawCodeEditor() {
  gfx::rect(0, 0, gfx::UI_W, gfx::UI_H, gfx::Color{0, 0, 0, 0.6f});
  string code = ggEditCode();
  string info = string(ggType == 0 ? "Game Genie" : "Pro Action Replay") + "   " + code + (codeValid(code) ? "" : "   (not valid)");
  string help = "Left/Right: move   Up/Down: digit   Y: code type   A: save   B: cancel";
  float cellsW = 8 * 52, w = std::max(cellsW, gfx::textWidth(help, 16)) + 56, h = 24 + 24 * LH + 16 + 54 + 14 + 2 * 16 * LH + 24;
  float x = (gfx::UI_W - w) / 2, y = roundf((gfx::UI_H - h) / 2);
  gfx::rect(x, y, w, h, col(C_PANEL), 14, 1, col(C_LINE));
  string title = "Enter cheat code";
  gfx::text(title, x + (w - gfx::textWidth(title, 24, gfx::BOLD)) / 2, y + 22, 24, col(C_TEXT), gfx::BOLD);
  float cy = y + 24 + 24 * LH + 16, cx = x + (w - cellsW) / 2;
  for (int i = 0; i < 8; i++) {
    float bx = cx + i * 52 + 4;
    gfx::rect(bx, cy, 44, 54, col(C_BG), 6, 2, col(i == ggPos ? C_ACCENT : C_LINE));
    string s(1, HEX[ggLetters[i]]);
    textMid(s, bx + (44 - gfx::textWidth(s, 30, gfx::MONO)) / 2, cy, 54, 30, col(C_TEXT), gfx::MONO);
  }
  cy += 54 + 14;
  gfx::text(info, x + (w - gfx::textWidth(info, 16)) / 2, cy, 16, col(C_DIM));
  gfx::text(help, x + (w - gfx::textWidth(help, 16)) / 2, cy + 16 * LH, 16, col(C_DIM));
}

static void drawMessage() {
  gfx::rect(0, 0, gfx::UI_W, gfx::UI_H, gfx::Color{0, 0, 0, 0.75f});
  vector<string> lines = wrap(messageText, 700, 22);
  float lh = 22 * 1.45f, w = 760, h = 30 * 2 + lines.size() * lh + 18 + 18 * LH;
  float x = (gfx::UI_W - w) / 2, y = roundf((gfx::UI_H - h) / 2);
  gfx::rect(x, y, w, h, col(C_PANEL), 14, 1, col(C_LINE));
  float cy = y + 30;
  for (auto& l : lines) { textMid(l, x + 30, cy, lh, 22, col(C_TEXT)); cy += lh; }
  gfx::text("Press A or B to continue", x + 30, cy + 18 - 2, 18, col(C_DIM));
}

static void drawToast() {
  if (!toastUntil) return;
  float w = gfx::textWidth(toastText, 20) + 36, h = 20 * LH + 20;
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
    if (C->state_load(pendingLoadSize)) { C->audio_reset(); record("state: slot %d loaded after %d frames", pendingLoadSlot, pendingLoadTries); showToast("Loaded slot " + std::to_string(pendingLoadSlot)); pendingLoadSlot = 0; }
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

static void runFrame(u64 bits, u64 bits2, bool behindMenu) {
  double t0 = nowMs();
  if (!behindMenu) {
    bool combo = (bits & MENU_COMBO) == MENU_COMBO;
    if (combo && !comboLatch) { comboLatch = true; lastBits = bits; menuCat = 0; openMenu(); return; }  // (opens on Game)
    if (!combo) comboLatch = false;
  }
  // Fast-forward (ZR hold or toggle) and rewind (hold ZL); both together open
  // the menu, so each acts only while the other is not held.
  bool both = (bits & MENU_COMBO) == MENU_COMBO;
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
    bool last = i == due - 1;
    bool ok;
    if (last) ok = emulateShown(p, p2, fastForward ? 0 : runAhead, !fastForward);
    else { C->set_audio_discard(1); ok = C->frame(p, p2, 0) >= 0; C->audio_take(); C->set_audio_discard(0); }
    if (!ok) { quitGame(); showMessage("Something went wrong:\nEmulation stopped.", [] { mode = "browser"; }); return; }
    emuCount++;
    // Rewind history: every frame.
    if (rewindOn && snapsReady) rewind_push();
  }
  {
    // Audio rate control (every frame): the queued amount, measured to the
    // sample and smoothed, is steered to the target by a PI loop around the
    // base ratio (60 Hz: display/NES rate difference; 120 Hz: 1, emulation
    // follows the clock). Corrections stay within +-0.5%, inaudible as pitch.
    double target = AUDIO_TARGET_FRAMES[audioBuffer];
    pendEma += (audio::queuedFrames() - pendEma) * 0.05;
    double err = (target - pendEma) / target;
    audioInteg = std::max(-0.004, std::min(0.004, audioInteg + err * 0.00002));
    double base = (hz120 || highRefresh()) ? 1.0 : NES_RATE / std::max(59.5, std::min(60.5, 1000.0 / tickMs));
    double r = base * (1 + std::max(-0.005, std::min(0.005, err * 0.004 + audioInteg)));
    if (++ratioTicks >= 4) { ratioTicks = 0; setAudioRatio(round(r * 1e5) / 1e5); }
    // Ran dry (a long stall): refill with a little silence instead of crackling.
    if (audio::queuedFrames() < 200) { audio::prefill((int)target / 2); pendEma = target / 2; }
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
extern "C" __attribute__((weak)) void sw2ax_register_exit_cleanup();
extern "C" __attribute__((weak)) void sw2ax_crash_watch(int);
extern "C" __attribute__((weak)) void sw2ax_iop_stats(char*, size_t);
extern "C" __attribute__((weak)) void sw2ax_event_stats(unsigned long long*);
extern "C" __attribute__((weak)) void sw2ax_event_sources(char*, size_t);
extern "C" __attribute__((weak)) void sw2ax_memory(unsigned*, unsigned*, unsigned*);
extern "C" __attribute__((weak)) void sw2ax_frame_counts(unsigned long long*, unsigned long long*, unsigned long long*);
extern "C" __attribute__((weak)) int sw2_smc_off;
extern "C" __attribute__((weak)) int sw2_softprot;
extern int sw2_audio_stretch __attribute__((weak));
extern "C" __attribute__((weak)) unsigned long long sw2_softprot_stats[4];
extern int sw2_iop_thread __attribute__((weak));
extern int sw2_iop_lag __attribute__((weak));
extern int sw2_gs_back __attribute__((weak));
extern int sw2_no_pacing __attribute__((weak));
extern int sw2_db_effects __attribute__((weak));
extern int sw2_shader_log __attribute__((weak));
extern "C" __attribute__((weak)) int sw2_prof_gs;
extern "C" __attribute__((weak)) void sw2ax_classify_batch(const unsigned long long*, unsigned*, size_t);
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
  if (sessionLog < 120) record("  %s", line.c_str());
  {  // The core's wait timers (pcsx2/MTGS.c), in ms per second of wall time.
    static unsigned long long gp[16];
    auto ms = [&](int i) { return armTicksToNs(sw2_gs_stats[i] - gp[i]) / 1e6; };
    if (sessionLog < 120) record("  waits per second: EE on GS %.0f ms (%llu times), EE at frame gate %.0f ms, GS idle %.0f ms, GS on VU1 %.0f ms, GL buffer fences %.0f ms, GL texture/finish %.0f ms, event tests %llu", ms(0), sw2_gs_stats[1] - gp[1], ms(4), ms(2), ms(3), ms(5), ms(6), sw2_gs_stats[9] - gp[9]);
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
  if (statsLevel == 1) snprintf(buf, sizeof buf, "%.1f fps", avg);
  else {
    double cpu = coreLoad(), gpu = gfx::gpuMs();
    char cpuS[24], gpuS[24];
    if (cpu >= 0) snprintf(cpuS, sizeof cpuS, "%d%%", (int)lround(cpu * 100)); else snprintf(cpuS, sizeof cpuS, "%d%%", (int)lround(100 * workMs / (t - statsAt)));
    if (gpu >= 0) snprintf(gpuS, sizeof gpuS, "%d%% (%.1f ms)", (int)lround(100 * gpu / tickMs), gpu); else snprintf(gpuS, sizeof gpuS, "n/a");
    // The game's own frame rate: vsyncs with draws into the displayed buffer when the game shows
    // any (it renders and copies), else vsyncs with display registers written, else buffer flips.
    static unsigned long long blitPrev, regPrev, flipPrev;
    const unsigned long long blits = sw2_gs_stats[11] - blitPrev, regs = sw2_gs_stats[12] - regPrev, flipsN = sw2_gs_stats[10] - flipPrev;
    blitPrev = sw2_gs_stats[11]; regPrev = sw2_gs_stats[12]; flipPrev = sw2_gs_stats[10];
    double gameFps = (blits ? blits : regs ? regs : flipsN) / secs;
    double emuFps = emuCount / secs;
    if (sw2ax_fps) { emuFps = sw2ax_fps(); gameFps = sw2ax_internal_fps(); }  // ARMSX2 core: its own counters
    snprintf(buf, sizeof buf, "Game %.0f fps   Emu %.1f fps   Video avg %.1f fps   1%% low %.1f fps   CPU %s   GPU %s   %s", gameFps, emuFps, avg, low, cpuS, gpuS, dispLabel().c_str());
  }
  if (sessionLog < 120) { sessionLog++; record("t%d %s | wait %.2f block %.2f emu %.2f draw %.2f swap %.2f ms, audio q %.0f ms ratio %.4f in %.0f/s", sessionLog, buf,
                                               phase[0] / phaseN, phase[1] / phaseN, phase[2] / phaseN, phase[3] / phaseN, phase[4] / phaseN, pendEma / 48.0, audioRatio, audioInCount / secs); audioInCount = 0; }
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
extern "C" { extern volatile unsigned sw2_prof_region; unsigned sw2_prof_pc(int iop); }
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
static u64 profHostOut = 0, profHostTotal = 0;
static std::unordered_map<u64, u32> profJit;  // host pcs outside the image (JIT)
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
          // offsets from this function's runtime address (tools/hostpcs.py adds its link address)
          const u64 anchor = (u64)(void*)&profMain;
          if (pc + 0x10000000ULL > anchor && pc < anchor + 0x10000000ULL) profHost[(u32)(pc - anchor + 0x10000000ULL)]++;
          else { profHostOut++; profJit[pc]++; }
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
  memset(profRegion, 0, sizeof profRegion); profPc[0].clear(); profPc[1].clear(); profHost.clear(); profJit.clear(); profHostOut = profHostTotal = 0; profEE = 0;
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
  if (profHostTotal) {
    vector<std::pair<u32, u32>> v(profHost.begin(), profHost.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& c) { return a.second > c.second; });
    line = "host pcs (" + std::to_string(profHostTotal) + " samples, outside the image " + std::to_string(profHostOut * 100 / profHostTotal) + "%):";
    for (size_t i = 0; i < v.size() && i < 400; i++) { snprintf(b, sizeof b, " %x:%u", v[i].first, v[i].second); line += b; }
    record("%s", line.c_str());
  }
  if (!profJit.empty()) {  // JIT samples by block (EE blocks only; others show as guest 0)
    std::unordered_map<u64, std::pair<u32, u32>> byBlock;  // block start -> (guest pc, samples)
    if (sw2ax_classify_batch) {  // ARMSX2: EE block start PCs, other code by region (0xF000000r)
      vector<unsigned long long> pcs; vector<u32> cnt; for (auto& kv : profJit) { pcs.push_back(kv.first); cnt.push_back(kv.second); }
      vector<unsigned> out(pcs.size());
      sw2ax_classify_batch(pcs.data(), out.data(), pcs.size());
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
static bool autoStartRun() {
  const Json* runs = autoJson.get("runs");
  Json one = Json::object();
  const Json* j = runs && runs->isArr() ? (autoRun < (int)runs->a.size() ? &runs->a[autoRun] : nullptr) : (autoRun == 0 ? &one : nullptr);
  if (!j) return false;
  const Json* g = j->get("game") ? j->get("game") : autoJson.get("game"); const Json* sl = j && j->get("slot") ? j->get("slot") : autoJson.get("slot"); const Json* sec = autoJson.get("seconds");
  int slot = sl && sl->isNum() ? (int)sl->n : 1; double seconds = sec && sec->isNum() ? sec->n : 20;
  ps2Renderer = pick(j->get("renderer"), RENDERERS.size(), 0); ps2Resolution = pick(j->get("resolution"), RESOLUTIONS.size(), 0);
  ps2EeSpeed = pick(j->get("eeSpeed"), EE_SPEEDS.size(), 0);
  ps2Fpu = pick(j->get("fpu"), FPU_MODES.size(), 0);
  { const Json* rb = j->get("readback"); autoReadback = rb && rb->isNum() ? (int)rb->n : -1; }
  cpuClock = pick(j->get("clock"), CPU_CLOCKS.size(), 2);  // 0 normal, 1 boost mode, 2 maximum
  gpuClock = pick(j->get("gpu"), GPU_CLOCKS.size(), 0);
  if (&sw2_prof_gs) { const Json* pg = j->get("profgs"); sw2_prof_gs = pg && pg->isBool() && pg->b; }
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
  if (&sw2_audio_stretch) { const Json* st = j->get("stretch"); if (st && st->isBool()) sw2_audio_stretch = st->b ? 1 : 0; }
  { const Json* ms = j->get("measured"); skipMeasured = ms && ms->isBool() && !ms->b; }
  { const Json* at = j->get("autotune"); autoTuneTest = at && at->isBool() ? (at->b ? 1 : 0) : -1; }
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
  autoTestEnd = nowMs() + seconds * 1000; sessionLog = 0;
  autoRunStart = nowMs(); autoShots.clear();
  autoMenuLoadAt = -1; autoMenuLoadStage = 0; autoLiveRes.clear(); autoEarlyLoadAt = -1; autoMenuOpenAt = -1; autoSaveAt = -1;
  if (const Json* sv = j->get("saveState")) if (sv->isArr() && sv->a.size() == 2 && sv->a[0].isNum() && sv->a[1].isNum()) { autoSaveAt = sv->a[0].n; autoSaveSlot = (int)sv->a[1].n; }
  if (const Json* mo = j->get("menuOpen")) if (mo->isNum()) autoMenuOpenAt = mo->n;
  if (const Json* el = j->get("earlyLoad")) if (el->isArr() && el->a.size() >= 1 && el->a[0].isNum()) { autoEarlyLoadAt = el->a[0].n; autoEarlyLoadSlot = el->a.size() > 1 && el->a[1].isNum() ? (int)el->a[1].n : 1; }
  if (const Json* lr = j->get("liveRes")) if (lr->isArr()) for (auto& e : lr->a) if (e.isArr() && e.a.size() == 2 && e.a[0].isNum() && e.a[1].isNum()) autoLiveRes.push_back({e.a[0].n, (int)e.a[1].n});
  if (const Json* ml = j->get("menuLoad")) if (ml->isArr() && ml->a.size() >= 1 && ml->a[0].isNum()) { autoMenuLoadAt = ml->a[0].n; autoMenuLoadSlot = ml->a.size() > 1 && ml->a[1].isNum() ? (int)ml->a[1].n : 1; }
  autoPresses.clear();
  if (const Json* pr = j->get("press")) if (pr->isArr()) for (auto& e : pr->a) if (e.type == Json::OBJ) {
    const Json* at = e.get("at"); const Json* bt = e.get("button"); const Json* ms = e.get("ms");
    if (at && at->isNum() && bt && bt->isStr()) autoPresses.push_back({at->n, ms && ms->isNum() ? ms->n : 150.0, autoButtonBits(bt->s)});
  }
  if (const Json* sh = j->get("shots")) if (sh->isArr()) for (auto& x : sh->a) if (x.isNum()) autoShots.push_back(x.n);
  if (const Json* sh = autoJson.get("shots")) if (sh->isArr() && autoShots.empty()) for (auto& x : sh->a) if (x.isNum()) autoShots.push_back(x.n);
  profStart();
  return true;
}
/* ---------------- Vibration ----------------
 * The core's DualShock 2 motors (bridge ps2_rumble): the large motor drives the controller's
 * low band, the small one its high band. Devices are set up again when the controller changes. */
extern "C" void ps2_rumble(int port, uint16_t* strong, uint16_t* weak);
struct Vib { HidVibrationDeviceHandle h[2]; int n = 0; u32 key = 0; uint16_t s = 0, w = 0; };
static Vib vibs[2];
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
  for (int port = 0; port < 2; port++) {
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
          char found[256]; showToast(ps2core_find_bios(biosDir.c_str(), found, sizeof found) ? string("BIOS found: ") + found : string("No BIOS in this folder yet"), 3500);
        } else {
        romDir = pickAtRoot() ? "sdmc:" : pickPath;
        saveSettings(); scan(); mode = "settings";
        showToast(std::to_string(allRoms.size()) + (allRoms.size() == 1 ? " game in " : " games in ") + (romDir.size() > 5 ? romDir.substr(5) : string("/")));
        }
      } else if (up && pickSel == 1) pickLoad(parentDir(pickPath));
      else pickLoad((pickAtRoot() ? string("sdmc:") : pickPath) + "/" + pickDirs[pickSel - 1 - up]);
    } else if (pressed & B_B) mode = "settings";
  } else if (mode == "settings") {
    {  // L / R (or ZL / ZR): the next / previous category
      int step = ((pressed & (B_R | HidNpadButton_ZR)) ? 1 : 0) - ((pressed & (B_L | HidNpadButton_ZL)) ? 1 : 0);
      if (step) { int n = (int)SETTINGS_CATS.size(); settingsCat = (settingsCat + step + n) % n; resetArmed = false; rebuildSettings(); if (settingsIndex >= 0) settingsIndex = 0; settingsTop = 0; return; }
    }
    int nItems = (int)SETTINGS_ITEMS.size();
    if (ev.up || ev.down || (pressed & B_B)) resetArmed = false;
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
    bool lr = (bits & MENU_COMBO) == MENU_COMBO;
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
    if ((pressed & B_B) && mode == "menu") closeMenu();
  } else if (mode == "ggedit") {
    handleCodeEditor(ev);
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
  {  // a test run restarting the app between games keeps appending to the same log
    const string marker = string(DATA_DIR) + "/autotest.append";
    const bool append = exists(marker);
    if (append) remove(marker.c_str());
    else {  // the previous session's log is kept as log.prev.txt (a test's results survive a relaunch)
      remove((string(DATA_DIR) + "/log.prev.txt").c_str());
      rename((string(DATA_DIR) + "/log.txt").c_str(), (string(DATA_DIR) + "/log.prev.txt").c_str());
    }
    logFile = fopen((string(DATA_DIR) + "/log.txt").c_str(), append ? "a" : "w");
  }
  record("SW2PS2 %s", APP_VERSION);

  string error;
  if (sw2ax_register_exit_cleanup) sw2ax_register_exit_cleanup();  // before the core mask change below
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
  padConfigureInput(2, HidNpadStyleSet_NpadStandard);
  PadState pad1, pad2;
  padInitialize(&pad1, HidNpadIdType_No1, HidNpadIdType_Handheld);
  padInitialize(&pad2, HidNpadIdType_No2);
  padStates[0] = &pad1; padStates[1] = &pad2;
  {  // pre-release: a build works for a week. Its first start says so; once expired it says that and quits.
    extern const long long sw2_build_time;
    const long long expires = sw2_build_time + 7LL * 24 * 3600;
    char until[64]; { time_t e = (time_t)expires; struct tm lt = *localtime(&e); strftime(until, sizeof until, "%d %B %Y, %H:%M", &lt); }
    record("pre-release: built %lld, works until %s", sw2_build_time, until);
    // A screen of lines until any button (after letting go of the one that launched us), or 30 s.
    auto notice = [&](const vector<string>& lines, const string& prompt) {
      const double endAt = nowMs() + 30000; bool released = false;
      while (appletMainLoop() && nowMs() < endAt) {
        padUpdate(&pad1);
        if (!padGetButtons(&pad1)) released = true; else if (released) break;
        gfx::beginFrame(col(C_BG));
        gfx::text("SW2PS2 " APP_VERSION " pre-release", 80, 120, 40, col(C_TEXT), gfx::BOLD);
        float y = 200; for (auto& l : lines) { gfx::text(l, 80, y, 26, col(C_TEXT)); y += 40; }
        gfx::text(prompt, 80, y + 40, 24, col(C_DIM));
        gfx::endFrame(0);
      }
    };
    const string seenPath = string(DATA_DIR) + "/prerelease.txt";
    if ((long long)time(nullptr) > expires) {
      record("pre-release: expired, quitting");
      notice({"This pre-release build has expired.", string("It stopped working on ") + until + ".", "Please download a newer build."}, "Press any button to exit.");
      quitApp = true;
    } else if (access((string(DATA_DIR) + "/autotest.json").c_str(), F_OK) != 0) {  // (not on test runs)
      vector<uint8_t> seen; const string stamp = std::to_string(sw2_build_time);
      if (!readFile(seenPath, seen) || string(seen.begin(), seen.end()) != stamp) {  // first start of this build
        notice({"This is a pre-release build. It works for a week from when it was built,", string("until ") + until + ". After that it will stop working: please get a newer build."}, "Press any button to continue.");
        writeFile(seenPath, stamp.data(), stamp.size());
      }
    }
  }
  ps2core_setup(DATA_DIR, [](const char* line) { if (coreLogLines < 6000) { coreLogLines++; record("[core] %s", line); } });

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
        record("autotest: running as '%s'%s", self.c_str(), autoAgent ? " (agent)" : "");
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
      profStop();
      record("autotest: run %d done", autoRun);
      if (sw2ax_event_sources) { char es[900]; sw2ax_event_sources(es, sizeof es); record("%s", es); }
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
        writeText(string(DATA_DIR) + "/autotest.finished", "done\n");
        autoWaiting = true; autoWaitSince = nowMs(); autoPollAt = 0;
      }
    }
    if (autoWaiting) {
      double tw = nowMs();
      if (!autoWaitSince) autoWaitSince = tw;
      padUpdate(&pad1);
      if ((padGetButtons(&pad1) & HidNpadButton_Plus) || tw - autoWaitSince > 45 * 60000.0) { quitApp = true; break; }
      if (tw >= autoPollAt) {
        autoPollAt = tw + 700;
        string nt; Json nj;
        if (readText(string(DATA_DIR) + "/autotest.json", nt) && Json::parse(nt, nj) && nj.type == Json::OBJ) {
          // Start again from the top (in the named build, or this one) to run it.
          const Json* nro = nj.get("nro");
          string self = argc > 0 && argv[0] ? argv[0] : "", want = nro && nro->isStr() && !nro->s.empty() ? string(DATA_DIR) + "/" + nro->s : self;
          Result rc = envHasNextLoad() ? envSetNextLoad(want.c_str(), ("\"" + want + "\"").c_str()) : -1;
          if (R_FAILED(rc)) writeText(string(DATA_DIR) + "/autotest.finished", "cannot restart: the homebrew loader has no next-load support\n");
          quitApp = true; break;
        }
      }
      gfx::beginFrame(col(C_BG));
      gfx::text("SW2PS2 test agent", 60, 60, 34, col(C_TEXT), gfx::BOLD);
      gfx::text("Waiting for the next test from the computer. Hold + to exit.", 60, 120, 22, col(C_DIM));
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
    bool gameView = running && (mode == "running" || mode == "menu" || mode == "ggedit");
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
    padUpdate(&pad1); padUpdate(&pad2);
    u64 bits = padGetButtons(&pad1), bits2 = padGetButtons(&pad2);
    if (autoTest && running) bits |= autoPressBits();
    frameDrawn = false;
    const bool wasRunning = mode == "running";
    if (wasRunning) runFrame(bits, bits2, false);
    updateVibration(mode == "running" && running);
    if (!wasRunning) {
      if (mode == "menu" && running && !pauseInMenu) runFrame(0, 0, true);
      handleUi(bits);
    }
    double t = nowMs();
    if (toastUntil && t > toastUntil) toastUntil = 0;
    if (hintUntil && t > hintUntil) hintUntil = 0;
    if (gameView != (running && (mode == "running" || mode == "menu" || mode == "ggedit"))) gfx::beginFrame(running ? gfx::Color{0, 0, 0, 1} : col(C_BG));
    if (running && (mode == "running" || mode == "menu" || mode == "ggedit")) {
      drawGame(mode != "running");
      if (mode == "menu") drawMenu();
      if (mode == "ggedit") drawCodeEditor();
    } else if (mode == "settings") drawSettings();
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
    double lastGapForFree = lastSwap ? after - lastSwap : 0;
    if (lastSwap) {
      double gap = after - lastSwap;
      if (gap > 5 && gap < 21) tickMs += (gap - tickMs) * 0.01;
      if (frameDrawn) noteShown(after);
    } else if (frameDrawn) noteShown(after);
    if (frameDrawn) {  // the graph plots the time between new game frames, not between display refreshes
      static double lastShownAt = 0;
      if (statsLevel == 3 && lastShownAt > 0 && after - lastShownAt < 1000) drawGraph(after - lastShownAt);
      lastShownAt = after;
    }
    lastSwap = after;
    if (!vrrNext && lastGapForFree > 3 && lastGapForFree < 40) freeTickMs = freeTickMs ? freeTickMs + (lastGapForFree - freeTickMs) * 0.05 : lastGapForFree;
    pollDisplay(after);
    if (running && (mode == "running" || (mode == "menu" && !pauseInMenu))) playAccumSec += std::min(loopGapMs, 100.0) / 1000.0;
    if (running && after - playSavedAt >= 300000) { playSavedAt = after; flushPlaytime(running->name); }
    swapAcc += gfx::swapMs; waitAcc += gfx::waitMs; swapN++; if (gfx::swapMs > swapMax) swapMax = gfx::swapMs;
    updateStats(after);
  }

  if (running) quitGame();
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
