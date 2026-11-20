// SW2PS2: libretro frontend glue for the OunceSX2 core (statically linked).
#include <atomic>
#include <strings.h>
#include <string>
#include "bridge_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "libretro.h"
#ifdef SW2_VK
#include "../source/vkpresent.h"
// Vulkan display (ps2_set_vulkan): the core renders on a device made through its negotiation
// interface and the front end presents its frames (source/vkpresent.cpp).
static bool vkWanted, vkGranted;
static const void* vkNegotiation;
#endif

static ps2_log_fn logFn;
static std::string systemDir, saveDir;
static std::map<std::string, std::string> options;   // set by the front end
static std::map<std::string, std::string> defaults;  // the core's own default for every option it declares
static bool optionsDirty;
static bool loaded;
static enum retro_pixel_format pixFmt = RETRO_PIXEL_FORMAT_0RGB1555;
static std::vector<uint32_t> frame;
static int fw, fh, fpitch;
static unsigned frameSerial;
static std::vector<int16_t> audioBuf;
static ps2_pad pads[4];  // players 1-4 (3-4 only with the Multitap)
static double fps = 59.94;
static float aspect = 4.0f / 3.0f;
// Rumble: the core sets it from its emulation thread; the front end reads it each frame.
static std::atomic<uint16_t> rumbleNow[4][2];  // [player][RETRO_RUMBLE_STRONG / WEAK]
static bool setRumble(unsigned port, enum retro_rumble_effect effect, uint16_t strength) {
  if (port < 4 && (unsigned)effect < 2) rumbleNow[port][effect].store(strength, std::memory_order_relaxed);
  return true;
}
static std::string lastError;
static uintptr_t (*glFramebuffer)(void);
static void* (*glProc)(const char*);
static struct retro_hw_render_callback hw;
static bool hwActive, hwFrame;
static uintptr_t hwGetFramebuffer(void) { return glFramebuffer ? glFramebuffer() : 0; }
static retro_proc_address_t hwGetProc(const char* sym) { return glProc ? (retro_proc_address_t)glProc(sym) : NULL; }

static void logLine(int level, const char* fmt, ...) {
  if (!logFn) return;
  char b[1024]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  size_t n = strlen(b); while (n && (b[n - 1] == '\n' || b[n - 1] == '\r')) b[--n] = 0;
  logFn(level, b);
}
static void coreLog(enum retro_log_level level, const char* fmt, ...) {
  if (!logFn) return;
  char b[1024]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  size_t n = strlen(b); while (n && (b[n - 1] == '\n' || b[n - 1] == '\r')) b[--n] = 0;
  logFn((int)level, b);
}

static bool environment(unsigned cmd, void* data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback*)data)->log = coreLog; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char**)data = systemDir.c_str(); return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char**)data = saveDir.c_str(); return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: pixFmt = *(enum retro_pixel_format*)data; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable* v = (struct retro_variable*)data;
      auto it = options.find(v->key ? v->key : "");
      if (it == options.end()) { it = defaults.find(v->key ? v->key : ""); if (it == defaults.end()) { v->value = NULL; return false; } }
      v->value = it->second.c_str(); return true;
    }
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE: ((struct retro_rumble_interface*)data)->set_rumble_state = setRumble; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool*)data = optionsDirty; optionsDirty = false; return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned*)data = 0; return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES: {
      // "Description; default|other|..." per key: a core reads each option back and
      // expects its declared default when the user chose nothing (its built-in
      // fallbacks are not the same values).
      for (const struct retro_variable* v = (const struct retro_variable*)data; v && v->key; v++) {
        if (!v->value) continue;
        const char* semi = strstr(v->value, "; ");
        if (!semi) continue;
        const char* first = semi + 2; const char* bar = strchr(first, '|');
        defaults[v->key] = bar ? std::string(first, bar - first) : std::string(first);
      }
      logLine(1, "bridge: %d core options declared; mtvu default '%s', instant vu1 '%s'", (int)defaults.size(), defaults.count("pcsx2_mtvu") ? defaults["pcsx2_mtvu"].c_str() : "?", defaults.count("pcsx2_instant_vu1") ? defaults["pcsx2_instant_vu1"].c_str() : "?");
      return true;
    }
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
      return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: { const struct retro_game_geometry* g = (const struct retro_game_geometry*)data; if (g->aspect_ratio > 0) aspect = g->aspect_ratio; return true; }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: { const struct retro_system_av_info* a = (const struct retro_system_av_info*)data; if (a->timing.fps > 1) fps = a->timing.fps; if (a->geometry.aspect_ratio > 0) aspect = a->geometry.aspect_ratio; return true; }
    case RETRO_ENVIRONMENT_SET_MESSAGE: logLine(1, "[message] %s", ((const struct retro_message*)data)->msg); return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT: logLine(1, "[message] %s", ((const struct retro_message_ext*)data)->msg); return true;
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION: *(unsigned*)data = 1; return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
      struct retro_hw_render_callback* cb = (struct retro_hw_render_callback*)data;
#ifdef SW2_VK
      if (cb->context_type == RETRO_HW_CONTEXT_VULKAN) {
        if (!vkWanted) { logLine(1, "bridge: Vulkan context refused (OpenGL display)"); return false; }
        hw = *cb; hwActive = true; vkGranted = true;
        logLine(1, "bridge: Vulkan %u.%u context accepted", cb->version_major, cb->version_minor);
        return true;
      }
      if (vkWanted) { logLine(1, "bridge: hardware context type %d refused (Vulkan display)", (int)cb->context_type); return false; }
#endif
#ifdef SW2_GL_DESKTOP
      // Desktop OpenGL build: the front end's contexts are desktop GL 4.6 core (Mesa/zink).
      bool gles = cb->context_type == RETRO_HW_CONTEXT_OPENGL_CORE && (cb->version_major < 4 || (cb->version_major == 4 && cb->version_minor <= 6));
#else
      bool gles = cb->context_type == RETRO_HW_CONTEXT_OPENGLES_VERSION || cb->context_type == RETRO_HW_CONTEXT_OPENGLES3;
#endif
      if (!gles || !glFramebuffer) { logLine(1, "bridge: hardware context type %d refused", (int)cb->context_type); return false; }
      cb->get_current_framebuffer = hwGetFramebuffer; cb->get_proc_address = hwGetProc;
      hw = *cb; hwActive = true;
      logLine(1, "bridge: OpenGL (type %d) %u.%u context accepted (depth %d, stencil %d, bottom-left %d)", (int)cb->context_type, cb->version_major, cb->version_minor, (int)cb->depth, (int)cb->stencil, (int)cb->bottom_left_origin);
      return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT: return true;
#ifdef SW2_VK
    case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE: vkNegotiation = data; return vkWanted;
    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE: *(const void**)data = vkp::hwInterface(); return vkp::hwInterface() != nullptr;
#endif
#ifdef SW2_VK
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
      if (vkWanted) { *(enum retro_hw_context_type*)data = RETRO_HW_CONTEXT_VULKAN; return true; }
      *(enum retro_hw_context_type*)data = glFramebuffer ? RETRO_HW_CONTEXT_OPENGLES_VERSION : RETRO_HW_CONTEXT_NONE; return true;
#elif defined(SW2_GL_DESKTOP)
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: *(enum retro_hw_context_type*)data = glFramebuffer ? RETRO_HW_CONTEXT_OPENGL_CORE : RETRO_HW_CONTEXT_NONE; return true;
#else
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: *(enum retro_hw_context_type*)data = glFramebuffer ? RETRO_HW_CONTEXT_OPENGLES_VERSION : RETRO_HW_CONTEXT_NONE; return true;
#endif
    default: return false;
  }
}

static void videoRefresh(const void* data, unsigned w, unsigned h, size_t pitch) {
  if (!data || !w || !h) return;
  if (data == RETRO_HW_FRAME_BUFFER_VALID) {
#ifdef SW2_VK
    if (vkGranted) vkp::setFrameSize(w, h);
#endif
    fw = w; fh = h; fpitch = w; hwFrame = true; frameSerial++; return;
  }
  hwFrame = false;
  if ((int)w != fw || (int)h != fh || frame.size() != (size_t)w * h) { fw = w; fh = h; fpitch = w; frame.assign((size_t)w * h, 0xff000000u); }
  if (pixFmt == RETRO_PIXEL_FORMAT_XRGB8888) {
    for (unsigned y = 0; y < h; y++) {
      const uint32_t* s = (const uint32_t*)((const uint8_t*)data + y * pitch); uint32_t* d = &frame[(size_t)y * w];
      for (unsigned x = 0; x < w; x++) { uint32_t p = s[x]; d[x] = 0xff000000u | ((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff); }  // BGRX -> RGBA bytes
    }
  } else {  // RGB565
    for (unsigned y = 0; y < h; y++) {
      const uint16_t* s = (const uint16_t*)((const uint8_t*)data + y * pitch); uint32_t* d = &frame[(size_t)y * w];
      for (unsigned x = 0; x < w; x++) { uint32_t p = s[x], r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31; d[x] = 0xff000000u | ((b << 3 | b >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (r << 3 | r >> 2); }
    }
  }
  frameSerial++;
}
static void audioSample(int16_t l, int16_t r) { audioBuf.push_back(l); audioBuf.push_back(r); }
static size_t audioBatch(const int16_t* d, size_t frames) { audioBuf.insert(audioBuf.end(), d, d + frames * 2); return frames; }
static void inputPoll(void) {}
static int16_t inputState(unsigned port, unsigned device, unsigned index, unsigned id) {
  if (port > 3) return 0;
  const ps2_pad& p = pads[port];
  if (device == RETRO_DEVICE_JOYPAD) { if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)p.buttons; return (p.buttons >> id) & 1; }
  if (device == RETRO_DEVICE_ANALOG) {
    if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT) return id == RETRO_DEVICE_ID_ANALOG_X ? p.lx : p.ly;
    if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) return id == RETRO_DEVICE_ID_ANALOG_X ? p.rx : p.ry;
    if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON) return ((p.buttons >> id) & 1) ? 0x7fff : 0;
  }
  return 0;
}

extern "C" {

void ps2_init(const char* sys, const char* save, ps2_log_fn log) {
  systemDir = sys; saveDir = save; logFn = log;
  retro_set_environment(environment); retro_set_video_refresh(videoRefresh); retro_set_audio_sample(audioSample);
  retro_set_audio_sample_batch(audioBatch); retro_set_input_poll(inputPoll); retro_set_input_state(inputState);
}
void ps2_set_gl(uintptr_t (*framebuffer)(void), void* (*proc)(const char*)) { glFramebuffer = framebuffer; glProc = proc; }
int ps2_video_is_gpu(void) { return hwFrame; }
#ifdef SW2_VK
void ps2_set_vulkan(int on) { vkWanted = on != 0; }
int ps2_vulkan_active(void) { return vkGranted; }
#endif
void ps2_set_option(const char* key, const char* value) {
#ifdef SW2_ARMSX2
  // The front end's option names and values ("pcsx2_*", from the earlier core), as the core's (it ignores the others).
  static std::string mapped;
  auto pct = [](const char* v) { std::string t(v); size_t sp = t.find(' '); return sp == std::string::npos ? t : t.substr(0, sp); };  // "75% (Underclock)" -> "75%"
  if (!strcmp(key, "pcsx2_hw_download_mode")) key = "armsx2_hw_download_mode";
  else if (!strcmp(key, "pcsx2_ee_cycle_rate")) { key = "armsx2_ee_cycle_rate"; mapped = pct(value); value = mapped.c_str(); }
  else if (!strcmp(key, "pcsx2_ee_cycle_skip")) {
    key = "armsx2_ee_cycle_skip";
    value = !strncmp(value, "Mild", 4) ? "mild" : !strncmp(value, "Moderate", 8) ? "moderate" : !strncmp(value, "Maximum", 7) ? "maximum" : "disabled";
  }
  else if (!strcmp(key, "pcsx2_upscale_multiplier")) { key = "armsx2_upscale"; mapped = pct(value); value = mapped.c_str(); }  // "1x (Native)" -> "1x"
  else if (!strcmp(key, "pcsx2_widescreen_hint")) { key = "armsx2_widescreen_patches"; value = !strncmp(value, "enabled", 7) ? "enabled" : "disabled"; }
  else if (!strcmp(key, "pcsx2_fastboot")) key = "armsx2_fast_boot";
#endif
  options[key] = value; optionsDirty = true;
}
#ifdef SW2_ARMSX2
extern "C" void sw2ax_after_unload();
#endif
#ifdef SW2_ARMSX2
// ARMSX2 opens no .cue: the image its FILE line names (single-track PS2 discs).
static std::string sw2CueTarget(const char* path) {
  std::string p = path ? path : "";
  if (p.size() < 4 || strcasecmp(p.c_str() + p.size() - 4, ".cue")) return p;
  FILE* f = fopen(p.c_str(), "r"); if (!f) return p;
  char line[512]; std::string out = p;
  while (fgets(line, sizeof line, f)) {
    const char* q1 = strchr(line, '"'); const char* q2 = q1 ? strchr(q1 + 1, '"') : nullptr;
    if (strncasecmp(line, "FILE", 4) == 0 && q1 && q2) { size_t slash = p.rfind('/'); out = p.substr(0, slash + 1) + std::string(q1 + 1, q2); break; }
  }
  fclose(f); return out;
}
#endif
const char* ps2_load(const char* path) {
#ifdef SW2_ARMSX2
  static std::string sw2Image; sw2Image = sw2CueTarget(path); if (path) path = sw2Image.c_str();
#endif
  if (loaded) ps2_unload();
  hwActive = false; hwFrame = false;
#ifdef SW2_VK
  vkGranted = false; vkNegotiation = nullptr;
#endif
  logLine(1, "bridge: retro_init");
  retro_init();
  struct retro_game_info gi; memset(&gi, 0, sizeof gi); gi.path = path;
  logLine(1, "bridge: retro_load_game(%s)", path ? path : "BIOS only");
  if (!retro_load_game(path ? &gi : NULL)) { retro_deinit(); lastError = "The core could not start this game (see log.txt)."; return lastError.c_str(); }
  struct retro_system_av_info av; memset(&av, 0, sizeof av); retro_get_system_av_info(&av);
  if (av.timing.fps > 1) fps = av.timing.fps;
  if (av.geometry.aspect_ratio > 0) aspect = av.geometry.aspect_ratio;
  logLine(1, "bridge: loaded, %ux%u, %.2f fps, %.0f Hz", av.geometry.base_width, av.geometry.base_height, av.timing.fps, av.timing.sample_rate);
  retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD); retro_set_controller_port_device(1, RETRO_DEVICE_JOYPAD);  // the core's DualShock 2 (sticks included)
  loaded = true; audioBuf.clear();
#ifdef SW2_VK
  if (vkGranted) {
    std::string err;
    if (!vkp::begin(vkNegotiation, err)) {
      logLine(1, "bridge: Vulkan display failed: %s", err.c_str());
      loaded = true;  // the caller unloads (ps2_unload: the core, then vkp::end)
      lastError = "The Vulkan display could not start: " + err; return lastError.c_str();
    }
  }
#endif
  if (hwActive && hw.context_reset) { logLine(1, "bridge: context_reset"); hw.context_reset(); logLine(1, "bridge: context_reset done"); }
  return NULL;
}
// The game is unloaded with the GL context still current, which closes the
// core's renderer; context_destroy is not called: it is for a context lost
// while a game keeps running, and it saves the renderer's state to restore on
// the next context_reset, which would then be applied to the next game.
void ps2_unload(void) {
  if (!loaded) return;
  retro_unload_game(); retro_deinit(); loaded = false; hwActive = false;
#ifdef SW2_VK
  if (vkGranted) { vkp::end(); vkGranted = false; }
#endif
#ifdef SW2_ARMSX2
  sw2ax_after_unload();
#endif
  // Every game starts from the core's defaults plus what the front end sets for it.
  options.clear(); optionsDirty = true;
  for (auto& p : rumbleNow) for (auto& m : p) m.store(0);
}
int ps2_loaded(void) { return loaded; }
void ps2_reset(void) { if (loaded) retro_reset(); }
void ps2_run(const ps2_pad* players) {  // 4 pads, players 1-4
  if (!loaded) return;
  for (int i = 0; i < 4; i++) pads[i] = players[i];
  retro_run();
}
const uint32_t* ps2_video(void) { return frame.empty() ? NULL : frame.data(); }
int ps2_video_width(void) { return fw; }
int ps2_video_height(void) { return fh; }
int ps2_video_pitch(void) { return fpitch; }
unsigned ps2_frame_serial(void) { return frameSerial; }
double ps2_fps(void) { return fps; }
float ps2_aspect(void) { return aspect; }
void ps2_rumble(int port, uint16_t* strong, uint16_t* weak) {
  *strong = rumbleNow[port & 3][RETRO_RUMBLE_STRONG].load(std::memory_order_relaxed);
  *weak = rumbleNow[port & 3][RETRO_RUMBLE_WEAK].load(std::memory_order_relaxed);
}
static std::vector<int16_t> audioOut;
int ps2_audio_take(const int16_t** out) { audioOut.swap(audioBuf); audioBuf.clear(); *out = audioOut.data(); return (int)(audioOut.size() / 2); }
long ps2_state_size(void) { return loaded ? (long)retro_serialize_size() : 0; }
int ps2_state_save(void* buf, long size) { return loaded && retro_serialize(buf, (size_t)size); }
int ps2_state_load(const void* buf, long size) { return loaded && retro_unserialize(buf, (size_t)size); }

}
