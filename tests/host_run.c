/* SW2PS2 host harness: loads the LRPS2 libretro core (dylib) on the Mac, boots a
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
#include <unistd.h>
#include "libretro.h"

static void* core;
#define SYM(T, n) __typeof__(T) n = (__typeof__(T))dlsym(core, #n)
static unsigned fw, fh; static size_t fpitch; static const void* fdata; static enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_0RGB1555;
static long audioFrames; static int frameNo; static uint32_t lastHash; static int changes;
static int padMask;

static void logcb(enum retro_log_level l, const char* f, ...) { va_list ap; va_start(ap, f); if (l >= (getenv("VERBOSE") ? 0 : 1)) { printf("[core] "); vprintf(f, ap); } va_end(ap); fflush(stdout); }
static bool env(unsigned cmd, void* data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback*)data)->log = logcb; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char**)data = "host/system"; return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char**)data = "host/save"; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: fmt = *(enum retro_pixel_format*)data; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable* v = data; char k[128]; snprintf(k, sizeof k, "OPT_%s", v->key);
      const char* e = getenv(k);
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
  if (argc < 3) { printf("usage: host_run core game|- [frames] [dumpEvery]\n"); return 2; }
  int frames = argc > 3 ? atoi(argv[3]) : 600, every = argc > 4 ? atoi(argv[4]) : 0;
  core = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL); if (!core) { printf("dlopen: %s\n", dlerror()); return 1; }
  SYM(void (*)(retro_environment_t), retro_set_environment); SYM(void (*)(retro_video_refresh_t), retro_set_video_refresh);
  SYM(void (*)(retro_audio_sample_t), retro_set_audio_sample); SYM(void (*)(retro_audio_sample_batch_t), retro_set_audio_sample_batch);
  SYM(void (*)(retro_input_poll_t), retro_set_input_poll); SYM(void (*)(retro_input_state_t), retro_set_input_state);
  SYM(void (*)(void), retro_init); SYM(void (*)(void), retro_deinit); SYM(void (*)(void), retro_run); SYM(void (*)(void), retro_unload_game);
  SYM(bool (*)(const struct retro_game_info*), retro_load_game); SYM(void (*)(struct retro_system_av_info*), retro_get_system_av_info);
  SYM(size_t (*)(void), retro_serialize_size);
  retro_set_environment(env); retro_set_video_refresh(video); retro_set_audio_sample(asample); retro_set_audio_sample_batch(abatch);
  retro_set_input_poll(poll); retro_set_input_state(state);
  { int* ff = (int*)dlsym(core, "sw2_fast_fpu"); if (ff && getenv("FASTFPU")) { *ff = 1; printf("fast FPU on\n"); } }
  retro_init();
  struct retro_game_info gi = {0}; gi.path = argv[2];
  if (!retro_load_game(strcmp(argv[2], "-") ? &gi : NULL)) { printf("LOAD FAILED\n"); return 1; }
  struct retro_system_av_info av; retro_get_system_av_info(&av);
  printf("av: %ux%u, %.2f fps, %.0f Hz; state size %zu\n", av.geometry.base_width, av.geometry.base_height, av.timing.fps, av.timing.sample_rate, retro_serialize_size());
  SYM(bool (*)(const void*, size_t), retro_unserialize);
  if (getenv("STATE")) {  // load a save state (e.g. one made on the console) after a few frames
    for (int i = 0; i < 30; i++) retro_run();
    FILE* sf = fopen(getenv("STATE"), "rb");
    if (!sf) { printf("cannot open state\n"); return 1; }
    fseek(sf, 0, SEEK_END); long sn = ftell(sf); fseek(sf, 0, SEEK_SET);
    void* sb = malloc(sn); if (fread(sb, 1, sn, sf) != (size_t)sn) return 1; fclose(sf);
    // A core whose VM boots on its own thread refuses a state until the VM is up: retry (up to 60 s).
    bool ok = false; int tries = 0;
    for (; tries < 3000 && !(ok = retro_unserialize(sb, sn)); tries++) { retro_run(); usleep(20000); }
    printf("state: %ld bytes, load %s after %d tries\n", sn, ok ? "ok" : "FAILED", tries); free(sb);
  }
  const char* padEnv = getenv("PAD");
  struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0); struct timespec tl = t0;
  if (getenv("RUN_SECONDS")) {  // a core that runs on its own thread: keep calling retro_run for a while
    const double secs = atof(getenv("RUN_SECONDS"));
    struct timespec a0, a1; clock_gettime(CLOCK_MONOTONIC, &a0);
    // PADT="<t0>-<t1>:<mask>,..." (seconds from the start): buttons held in that window.
    const char* padt = getenv("PADT");
    for (;;) {
      clock_gettime(CLOCK_MONOTONIC, &a1);
      const double t = (a1.tv_sec - a0.tv_sec) + (a1.tv_nsec - a0.tv_nsec) / 1e9;
      if (t > secs) break;
      padMask = 0;
      if (padt) { const char* p = padt; while (*p) { double f0, f1; int m; if (sscanf(p, "%lf-%lf:%d", &f0, &f1, &m) == 3 && t >= f0 && t <= f1) padMask |= m; p = strchr(p, ','); if (!p) break; p++; } }
      retro_run(); usleep(2000);
    }
    frames = 0;
  }
  for (frameNo = 1; frameNo <= frames; frameNo++) {
    padMask = 0;
    if (padEnv) { const char* p = padEnv; while (*p) { int f0, f1, m; if (sscanf(p, "%d-%d:%d", &f0, &f1, &m) == 3 && frameNo >= f0 && frameNo <= f1) padMask |= m; p = strchr(p, ','); if (!p) break; p++; } }
    fdata = NULL; retro_run();
    if (fdata) { uint32_t h = 2166136261u; for (unsigned y = 0; y < fh; y += 4) { const uint32_t* row = (const uint32_t*)((const uint8_t*)fdata + y * fpitch); for (unsigned x = 0; x < fw; x += 4) h = (h ^ row[x]) * 16777619u; } if (h != lastHash) changes++; lastHash = h; }
    if (every && frameNo % every == 0 && getenv("DUMP")) dump(getenv("DUMP"));
    if (getenv("SAVE_AT") && frameNo == atoi(getenv("SAVE_AT"))) {  // SAVE_AT=<frame> SAVE_TO=<file>
      SYM(size_t (*)(void), retro_serialize_size); SYM(bool (*)(void*, size_t), retro_serialize);
      size_t n = retro_serialize_size(); void* b = malloc(n);
      bool ok = retro_serialize(b, n); FILE* o = fopen(getenv("SAVE_TO"), "wb");
      if (ok && o) fwrite(b, 1, n, o); if (o) fclose(o); free(b);
      printf("state saved at frame %d: %s\n", frameNo, ok ? "ok" : "FAILED");
    }
    if (frameNo % 300 == 0) { clock_gettime(CLOCK_MONOTONIC, &t1); double s = (t1.tv_sec - tl.tv_sec) + (t1.tv_nsec - tl.tv_nsec) / 1e9; printf("frame %d: %ux%u, %.1f fps, picture changed %d times, audio %ld\n", frameNo, fw, fh, 300 / s, changes, audioFrames); fflush(stdout); tl = t1; }
  }
  if (getenv("RAMDUMP")) {  // EE main RAM (32 MB) after the run
    SYM(void* (*)(unsigned), retro_get_memory_data);
    void* ram = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    FILE* rf = fopen(getenv("RAMDUMP"), "wb"); if (rf && ram) { fwrite(ram, 1, 32 << 20, rf); fclose(rf); printf("ram dumped\n"); }
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  printf("done: %d frames in %.1f s\n", frames, (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9);
  retro_unload_game(); retro_deinit();
  return 0;
}
