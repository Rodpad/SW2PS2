/* SW2PS2 host harness (load/unload cycles, see main): loads the LRPS2 libretro core (dylib) on the Mac, boots a
 * game headless with the software renderer and reports frames/audio.
 *   host_run <core.dylib> <game.iso|-> [frames] [dumpEvery]
 * Env: RENDERER (default "Software (SW)"), OPT_<key>=value overrides core options,
 * STATE=file loads a save state after 30 frames; DUMP=prefix writes raw frames (see raw2png.py), PAD="frame:mask,..." presses buttons. */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libretro.h"

static void* core;
#define SYM(T, n) __typeof__(T) n = (__typeof__(T))dlsym(core, #n)
static unsigned fw, fh; static size_t fpitch; static const void* fdata; static enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_0RGB1555;
static long audioFrames; static int frameNo; static uint32_t lastHash; static int changes;
static int padMask;
/* Options of the current cycle: "key=value,key=value" */
static char cycleOpts[1024];
static const char* cycleOpt(const char* key) {
  static char val[256]; size_t kl = strlen(key); const char* p = cycleOpts;
  while (*p) {
    const char* e = strchr(p, ','); size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n > kl && !strncmp(p, key, kl) && p[kl] == '=') { size_t vl = n - kl - 1; if (vl >= sizeof val) vl = sizeof val - 1; memcpy(val, p + kl + 1, vl); val[vl] = 0; return val; }
    if (!e) break; p = e + 1;
  }
  return NULL;
}

static void logcb(enum retro_log_level l, const char* f, ...) { va_list ap; va_start(ap, f); if (l >= (getenv("VERBOSE") ? 0 : 1)) { printf("[core] "); vprintf(f, ap); } va_end(ap); fflush(stdout); }
static bool env(unsigned cmd, void* data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback*)data)->log = logcb; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char**)data = "host/system"; return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char**)data = "host/save"; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: fmt = *(enum retro_pixel_format*)data; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable* v = data; char k[128]; snprintf(k, sizeof k, "OPT_%s", v->key);
      const char* e = cycleOpt(v->key); if (!e) e = getenv(k);
      if (!e && !strcmp(v->key, "pcsx2_renderer")) e = getenv("RENDERER") ? getenv("RENDERER") : "Software (SW)";
      if (getenv("VERBOSE")) printf("[env] get %s -> %s\n", v->key, e ? e : "(default)");
      v->value = e; return e != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool*)data = false; return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: printf("[env] SET_HW_RENDER refused (context type %d)\n", ((struct retro_hw_render_callback*)data)->context_type); return false;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned*)data = 0; return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES: case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: return true;
    default: if (getenv("VERBOSE")) printf("[env] cmd %u unanswered\n", cmd & 0xffff); return false;
  }
}
static void video(const void* d, unsigned w, unsigned h, size_t p) { if (d) { fdata = d; fw = w; fh = h; fpitch = p; } }
static void asample(int16_t l, int16_t r) { audioFrames++; }
static size_t abatch(const int16_t* d, size_t n) { audioFrames += n; return n; }
static void poll(void) {}
static int16_t state(unsigned port, unsigned dev, unsigned idx, unsigned id) {
  if (port == 0 && dev == RETRO_DEVICE_JOYPAD) { if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return padMask; return (padMask >> id) & 1; }
  return 0;
}
static void dump(const char* prefix) {
  if (!fdata || fmt != RETRO_PIXEL_FORMAT_XRGB8888) return;
  char path[512]; snprintf(path, sizeof path, "%s_%05d.raw", prefix, frameNo);
  FILE* o = fopen(path, "wb"); if (!o) return;
  fprintf(o, "%u %u\n", fw, fh);
  for (unsigned y = 0; y < fh; y++) { const uint8_t* row = (const uint8_t*)fdata + y * fpitch; for (unsigned x = 0; x < fw; x++) { uint8_t px[4] = {row[x * 4 + 2], row[x * 4 + 1], row[x * 4], 255}; fwrite(px, 1, 4, o); } }
  fclose(o);
}
int main(int argc, char** argv) {
  /* host_cycle <core> <game> <frames per cycle> "opts;opts;..." : like the app, each cycle is
   * retro_init, load, state (STATE), run, unload, retro_deinit, in one process. */
  if (argc < 5) { printf("usage: host_cycle core game frames \"k=v,k=v;...\"\n"); return 2; }
  int frames = atoi(argv[3]);
  core = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL); if (!core) { printf("dlopen: %s\n", dlerror()); return 1; }
  SYM(void (*)(retro_environment_t), retro_set_environment); SYM(void (*)(retro_video_refresh_t), retro_set_video_refresh);
  SYM(void (*)(retro_audio_sample_t), retro_set_audio_sample); SYM(void (*)(retro_audio_sample_batch_t), retro_set_audio_sample_batch);
  SYM(void (*)(retro_input_poll_t), retro_set_input_poll); SYM(void (*)(retro_input_state_t), retro_set_input_state);
  SYM(void (*)(void), retro_init); SYM(void (*)(void), retro_deinit); SYM(void (*)(void), retro_run); SYM(void (*)(void), retro_unload_game);
  SYM(bool (*)(const struct retro_game_info*), retro_load_game); SYM(bool (*)(const void*, size_t), retro_unserialize);
  retro_set_environment(env); retro_set_video_refresh(video); retro_set_audio_sample(asample); retro_set_audio_sample_batch(abatch);
  retro_set_input_poll(poll); retro_set_input_state(state);
  void* sb = NULL; long sn = 0;
  if (getenv("STATE")) { FILE* sf = fopen(getenv("STATE"), "rb"); if (!sf) return 1; fseek(sf, 0, SEEK_END); sn = ftell(sf); fseek(sf, 0, SEEK_SET); sb = malloc(sn); if (fread(sb, 1, sn, sf) != (size_t)sn) return 1; fclose(sf); }
  char* list = strdup(argv[4]); int cyc = 0;
  for (char* tok = list; tok; cyc++) {
    char* next = strchr(tok, ';'); if (next) *next++ = 0;
    snprintf(cycleOpts, sizeof cycleOpts, "%s", tok);
    printf("=== cycle %d: options '%s'\n", cyc, cycleOpts); fflush(stdout);
    retro_init();
    struct retro_game_info gi = {0}; gi.path = argv[2];
    if (!retro_load_game(&gi)) { printf("LOAD FAILED\n"); return 1; }
    for (int i = 0; i < 30; i++) retro_run();
    if (sb) printf("state load %s\n", retro_unserialize(sb, sn) ? "ok" : "FAILED");
    for (frameNo = 1; frameNo <= frames; frameNo++) retro_run();
    printf("cycle %d ran %d frames, audio %ld\n", cyc, frames, audioFrames); fflush(stdout);
    retro_unload_game(); retro_deinit();
    tok = next;
  }
  printf("all cycles done\n");
  return 0;
}
