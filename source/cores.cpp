// SW2PS2: LRPS2 adapter (core table) for the front end.
#include "core.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <string>
#include <vector>

#include "uigfx.h"
#include "../native/bridge_lrps2.h"

const char* core_rom_name = "";
Ps2Input ps2Input[2];
static std::string dataDir, loadError;
static void (*logLine)(const char*);
static bool gpuOn;
static void coreLog(int level, const char* line) { if (logLine) logLine(line); }

// The core's GL work happens in its own context (uigfx coreEnter/coreLeave).
struct InCore { InCore() { gfx::coreEnter(); } ~InCore() { gfx::coreLeave(); } };

void ps2core_setup(const char* dir, void (*log)(const char*)) {
  dataDir = dir; logLine = log;
  for (const char* sub : {"/system", "/system/pcsx2", "/system/pcsx2/bios", "/saves"}) mkdir((dataDir + sub).c_str(), 0777);
  ps2_init((dataDir + "/system").c_str(), (dataDir + "/saves").c_str(), coreLog);
}
void ps2core_option(const char* key, const char* value) { ps2_set_option(key, value); }
bool ps2core_find_bios(char* name, int size) {
  std::string dir = dataDir + "/system/pcsx2/bios", best;
  if (DIR* d = opendir(dir.c_str())) {
    while (dirent* e = readdir(d)) {
      struct stat st; std::string p = dir + "/" + e->d_name;
      if (e->d_name[0] != '.' && stat(p.c_str(), &st) == 0 && st.st_size >= 4 * 1024 * 1024 && st.st_size <= 8 * 1024 * 1024 && (best.empty() || std::string(e->d_name) < best)) best = e->d_name;
    }
    closedir(d);
  }
  if (best.empty()) return false;
  snprintf(name, size, "%s", best.c_str());
  return true;
}
bool ps2core_use_gpu(bool on, int scale, const char** error) {
  static std::string err;
  gfx::coreDestroy(); gpuOn = false;
  ps2_set_gl(NULL, NULL);
  if (!on) return true;
  if (!gfx::coreCreate(640 * scale, 512 * scale, err)) { if (error) *error = err.c_str(); return false; }
  ps2_set_gl([]() -> uintptr_t { return gfx::coreFramebuffer(); }, gfx::coreProc);
  gpuOn = true;
  return true;
}
bool ps2core_is_gpu() { return ps2_video_is_gpu() != 0; }
#ifdef SW2_VK
void ps2core_use_vulkan(bool on) { ps2_set_vulkan(on); }
bool ps2core_vulkan() { return ps2_vulkan_active() != 0; }
#endif
double ps2core_fps() { return ps2_fps(); }

/* ---------------- Audio: volume and rate trim ----------------
 * The core gives 48 kHz stereo. The front end steers the queued amount with a
 * ratio within +-1% (set_audio_ratio), applied here by cubic resampling. */
static std::vector<int16_t> out;
static int outFrames, volume = 256;
static bool discard;
static double rsPos, rsStep = 1.0;
static int16_t hist[4][2];
static inline float cubic(float y0, float y1, float y2, float y3, float t) {
  float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3, b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c = -0.5f * y0 + 0.5f * y2;
  return ((a * t + b) * t + c) * t + y1;
}
static inline int16_t clamp16(float v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v; }
static void pumpAudio() {
  const int16_t* in; int n = ps2_audio_take(&in);
  if (discard) return;
  if (out.size() < (size_t)(outFrames + n + 64) * 2) out.resize((size_t)(outFrames + n + 64) * 2);
  for (int i = 0; i < n; i++) {
    memmove(hist[0], hist[1], sizeof(int16_t) * 6);
    hist[3][0] = in[i * 2]; hist[3][1] = in[i * 2 + 1];
    while (rsPos < 1.0) {
      if ((size_t)(outFrames + 1) * 2 > out.size()) out.resize(out.size() + 4096);
      float t = (float)rsPos;
      out[outFrames * 2] = clamp16(cubic(hist[0][0], hist[1][0], hist[2][0], hist[3][0], t) * volume / 256);
      out[outFrames * 2 + 1] = clamp16(cubic(hist[0][1], hist[1][1], hist[2][1], hist[3][1], t) * volume / 256);
      outFrames++; rsPos += rsStep;
    }
    rsPos -= 1.0;
  }
}

/* ---------------- Core table ---------------- */
static uint8_t* romAlloc(long) { static uint8_t dummy[16]; return dummy; }  // discs are opened by path
static const char* load() {
  InCore in;
  const char* e = ps2_load(core_rom_name && core_rom_name[0] ? core_rom_name : NULL);
  outFrames = 0; rsPos = 0; memset(hist, 0, sizeof hist);
  return e;
}
static void unload() { { InCore in; ps2_unload(); } gfx::coreDestroy(); gpuOn = false; ps2_set_gl(NULL, NULL); }
static void noop() {}
static void reset(int) { InCore in; ps2_reset(); }
static int frame(int, int, int) {
  ps2_pad a = {ps2Input[0].buttons, ps2Input[0].lx, ps2Input[0].ly, ps2Input[0].rx, ps2Input[0].ry};
  ps2_pad b = {ps2Input[1].buttons, ps2Input[1].lx, ps2Input[1].ly, ps2Input[1].rx, ps2Input[1].ry};
  { InCore in; ps2_run(&a, &b); }
  pumpAudio();
  return 0;
}
static void setAudio(int vol, int) { volume = vol < 0 ? 0 : vol > 512 ? 512 : vol; }
static void setAudioRatio(double r) { if (r < 0.99) r = 0.99; if (r > 1.01) r = 1.01; rsStep = 1.0 / r; }
static int audioTake() { int n = outFrames; outFrames = 0; return n; }
static int16_t* audioData() { if (out.empty()) out.resize(4096); return out.data(); }
static void audioReset() { outFrames = 0; rsPos = 0; memset(hist, 0, sizeof hist); }
static void setAudioDiscard(int on) { discard = on != 0; }
static uint32_t* video() { return (uint32_t*)ps2_video(); }
static int videoPitch() { return ps2_video_pitch(); }
static int width() { return ps2_video_width(); }
static int height() { return ps2_video_height(); }
static int hasBattery() { return 0; }  // memory cards are files the core writes itself
static uint8_t* sram() { return NULL; }
static int sramSize() { return 0; }
static std::vector<char> state;
static long stateSave() {
  InCore in;
  long sz = ps2_state_size();
  if (sz <= 0) return 0;
  state.resize(sz);
  return ps2_state_save(state.data(), sz) ? sz : 0;
}
static char* stateBuffer(long n) { if (n <= 0) return NULL; state.resize(n); return state.data(); }
static int stateLoad(long n) { InCore in; return ps2_state_load(state.data(), n); }
static int runahead(int, int, int) { return -1; }
static int setCheats(const char**, int) { return 0; }
static int cheatValid(const char*) { return 0; }

const CoreApi PS2_CORE = {
#ifdef SW2_ARMSX2
    "ARMSX2", ".armsx2",  // its states are a different format: kept apart from LRPS2's
#else
    "LRPS2", "",
#endif
    romAlloc, unload, load, noop, reset, noop, frame,
    setAudio, setAudioRatio, audioTake, audioData, audioReset, setAudioDiscard,
    video, videoPitch, width, height,
    hasBattery, sram, sramSize, stateSave, stateBuffer, stateLoad,
    runahead, setCheats, cheatValid,
};

// Rewind does not exist on this core (see core.h).
bool rewind_init(long) { return false; }
void rewind_clear() {}
bool rewind_push() { return false; }
bool rewind_pop() { return false; }
bool rewind_reload() { return false; }
int rewind_count() { return 0; }
long rewind_bytes() { return 0; }
