// SW2PS2 bring-up: a plain game list (/roms/ps2), then the chosen game (or the
// BIOS alone) with the software renderer; shows the picture and writes sdmc:/switch/sw2ps2/log.txt. Press + to exit.
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <string>
#include <vector>
#include <switch.h>

#include "audio.h"
#include "uigfx.h"
#include "../native/bridge_lrps2.h"

#define DATA "sdmc:/switch/sw2ps2"
static void record(const char* fmt, ...) {
  char b[1100]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  FILE* f = fopen(DATA "/log.txt", "a"); if (f) { fputs(b, f); fputc('\n', f); fclose(f); }
}
static int coreLines;
static void coreLog(int level, const char* line) { if (coreLines < 4000) { coreLines++; record("[core %d] %s", level, line); } }
extern "C" void memjit_get_stats(unsigned long long* n, unsigned long long* bytes, unsigned long long* ticks);
static double cpuBench() {  // ms for a fixed integer workload (lower = faster CPU clock)
  u64 t0 = armGetSystemTick(); volatile u64 a = 1; for (u32 i = 0; i < 50000000u; i++) a = a * 6364136223846793005ULL + 1442695040888963407ULL;
  return armTicksToNs(armGetSystemTick() - t0) / 1e6;
}
static void logClocks(const char* when) {
  record("clocks (%s):", when);
  Result rc = clkrstInitialize(); record("  clkrstInitialize rc %x", rc);
  if (R_SUCCEEDED(rc)) {
    const struct { PcvModuleId id; const char* n; } mods[] = {{PcvModuleId_CpuBus, "cpu"}, {PcvModuleId_GPU, "gpu"}, {PcvModuleId_EMC, "memory"}};
    for (auto& m : mods) { ClkrstSession ses; Result r = clkrstOpenSession(&ses, m.id, 3); u32 hz = 0; if (R_SUCCEEDED(r)) { Result r2 = clkrstGetClockRate(&ses, &hz); record("  %s: rc %x, %u MHz", m.n, r2, hz / 1000000); clkrstCloseSession(&ses); } else record("  %s: open rc %x", m.n, r); }
    clkrstExit();
  }
}
static double nowMs() { return armTicksToNs(armGetSystemTick()) / 1e6; }

int main(int argc, char** argv) {
  mkdir(DATA, 0777); mkdir(DATA "/bios", 0777); mkdir(DATA "/system", 0777); mkdir(DATA "/system/pcsx2", 0777); mkdir(DATA "/system/pcsx2/bios", 0777); mkdir(DATA "/saves", 0777);
  remove(DATA "/log.txt");
  record("SW2PS2 bring-up %s", APP_VERSION);
  romfsInit();
  std::string error;
  if (!gfx::init(error)) { record("gfx: %s", error.c_str()); return 1; }
  std::string audioError; bool sound = audio::init(audioError); if (!sound) record("audio: %s", audioError.c_str());
  padConfigureInput(1, HidNpadStyleSet_NpadStandard); PadState pad; padInitializeDefault(&pad); padUpdate(&pad);

  // CPU clock: measure, try the system's boost mode, measure again.
  double benchNormal = cpuBench(); record("cpu bench normal: %.0f ms", benchNormal);
  logClocks("normal");
  Result boostRc = appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad); record("appletSetCpuBoostMode(FastLoad) rc %x", boostRc);
  svcSleepThread(300000000ULL);
  double benchBoost = cpuBench(); record("cpu bench boost: %.0f ms", benchBoost);
  logClocks("boost");
  bool boost = false; appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
  // BIOS: the first 4 MB file in system/pcsx2/bios.
  std::string bios;
  if (DIR* d = opendir(DATA "/system/pcsx2/bios")) { while (dirent* e = readdir(d)) { std::string p = std::string(DATA "/system/pcsx2/bios/") + e->d_name; struct stat st; if (stat(p.c_str(), &st) == 0 && st.st_size >= 4 * 1024 * 1024 && st.st_size <= 8 * 1024 * 1024 && bios.empty()) bios = e->d_name; } closedir(d); }
  // Game list: disc images in /roms/ps2. Up/Down, A starts, Y boots the BIOS alone, + exits.
  std::vector<std::string> games;
  mkdir("sdmc:/roms", 0777); mkdir("sdmc:/roms/ps2", 0777);
  if (DIR* d = opendir("sdmc:/roms/ps2")) { while (dirent* e = readdir(d)) { std::string n = e->d_name; size_t dot = n.rfind('.'); std::string ext = dot == std::string::npos ? "" : n.substr(dot); for (auto& c : ext) c = tolower(c); if (ext == ".iso" || ext == ".chd" || ext == ".cso" || ext == ".bin") games.push_back(n); } closedir(d); }
  std::sort(games.begin(), games.end());
  record("bios '%s', %d games", bios.c_str(), (int)games.size());
  std::string status = bios.empty() ? "No BIOS in /switch/sw2ps2/system/pcsx2/bios" : "";
  std::string game; int sel = 0, scale = 1; bool chosen = bios.empty(), quit = false, gpu = true;
  while (!chosen && appletMainLoop()) {
    padUpdate(&pad); u64 down = padGetButtonsDown(&pad);
    if (down & HidNpadButton_Plus) { quit = true; break; }
    if ((down & HidNpadButton_AnyUp) && sel > 0) sel--;
    if ((down & HidNpadButton_AnyDown) && sel + 1 < (int)games.size()) sel++;
    if ((down & HidNpadButton_A) && !games.empty()) { game = "sdmc:/roms/ps2/" + games[sel]; chosen = true; }
    if ((down & HidNpadButton_X) && !games.empty()) { game = "sdmc:/roms/ps2/" + games[sel]; chosen = true; gpu = false; }
    if (down & (HidNpadButton_ZL | HidNpadButton_ZR)) boost = !boost;
    if (down & (HidNpadButton_L | HidNpadButton_R)) scale = scale == 1 ? 2 : scale == 2 ? 4 : 1;
    if (down & HidNpadButton_Y) chosen = true;
    gfx::beginFrame(gfx::Color{0.05f, 0.05f, 0.08f, 1});
    gfx::text("SW2PS2 bring-up " APP_VERSION, 40, 30, 30, gfx::rgb(0xffffff), gfx::BOLD);
    { char hint[200]; snprintf(hint, sizeof hint, "Games in /roms/ps2  |  A: start (GPU renderer, %dx resolution; L/R changes)   X: start (software renderer)   Y: BIOS only   +: exit", scale); gfx::text(hint, 40, 76, 18, gfx::rgb(0x9aa3b5)); }
    { char bl[200]; snprintf(bl, sizeof bl, "CPU test: %.0f ms normal, %.0f ms in boost mode (rc %x)   |   ZL/ZR: boost mode for the game: %s", benchNormal, benchBoost, boostRc, boost ? "ON" : "off"); gfx::text(bl, 40, 100, 18, gfx::rgb(0xe8c040)); }
    if (games.empty()) gfx::text("No games found (.iso, .chd, .cso, .bin).", 40, 130, 22, gfx::rgb(0xffb0b0));
    int top = sel > 16 ? sel - 16 : 0;
    for (int i = top; i < (int)games.size() && i < top + 20; i++) {
      float y = 150 + (i - top) * 34;
      if (i == sel) gfx::rect(30, y - 4, gfx::UI_W - 60, 34, gfx::rgb(0x2a3350), 6);
      gfx::text(gfx::ellipsize(games[i], gfx::UI_W - 100, 22), 44, y, 22, gfx::rgb(i == sel ? 0xffffff : 0xc0c6d4));
    }
    gfx::endFrame(0);
  }
  if (quit) { audio::shutdown(); gfx::shutdown(); romfsExit(); return 0; }
  if (chosen && !bios.empty()) {
    gfx::beginFrame(gfx::Color{0.05f, 0.05f, 0.08f, 1}); gfx::text("Starting...", 40, 30, 30, gfx::rgb(0xffffff)); gfx::endFrame(0);
    record("game '%s', boost %d", game.c_str(), (int)boost);
    appletSetCpuBoostMode(boost ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
    ps2_init(DATA "/system", DATA "/saves", coreLog);
    ps2_set_option("pcsx2_bios", bios.c_str());
    if (gpu) { std::string ge; if (!gfx::coreCreate(640 * scale, 512 * scale, ge)) { record("gpu: %s", ge.c_str()); gpu = false; } }
    record("renderer: %s, scale %d, core context %s", gpu ? "OpenGL" : "software", scale, gfx::coreIsSurfaceless() ? "surfaceless" : "on the window surface");
    if (gpu) ps2_set_gl([]() -> uintptr_t { return gfx::coreFramebuffer(); }, gfx::coreProc);
    ps2_set_option("pcsx2_renderer", gpu ? "OpenGL" : "Software (SW)");
    ps2_set_option("pcsx2_upscale_multiplier", scale == 1 ? "1x (Native)" : scale == 2 ? "2x" : "4x");
    ps2_set_option("pcsx2_sw_renderer_threads", "3");
    ps2_set_option("pcsx2_fastboot", game.empty() ? "disabled" : "enabled");
    double t0 = nowMs();
    gfx::coreEnter();
    const char* e = ps2_load(game.empty() ? NULL : game.c_str());
    gfx::coreLeave();
    record("ps2_load: %s (%.0f ms)", e ? e : "ok", nowMs() - t0);
    if (e) status = e;
  }
  gfx::Picture* pic = gfx::pictureCreate(2560, 2048);
  { std::string slog; int n = gfx::loadShaders({"none.glsl", "bilinear.glsl"}, slog); record("shaders: %d compiled %s", n, slog.c_str()); gfx::pictureSetShader(pic, 1); }
  double enterMs = 0, runMs = 0, leaveMs = 0, worstMs = 0, copyMs = 0, loopAt = nowMs(), loopWorst = 0; char breakdown[200] = "";
  enum { GRAPH_N = 240 }; static float graphLoop[GRAPH_N], graphCore[GRAPH_N]; int graphPos = 0; float lastCoreMs = 0;
  int frames = 0, shown = 0; unsigned lastSerial = 0; double statAt = nowMs(), emuMs = 0, fpsEmu = 0, fpsPic = 0, msPerFrame = 0; long audioTotal = 0; int loggedStats = 0;
  while (appletMainLoop()) {
    padUpdate(&pad); u64 held = padGetButtons(&pad);
    if ((held & HidNpadButton_Plus) && (held & HidNpadButton_Minus)) break;  // + and - together exit
    if (ps2_loaded()) {
      ps2_pad p = {0, 0, 0, 0, 0};
      // libretro joypad ids: B0 Y1 SELECT2 START3 UP4 DOWN5 LEFT6 RIGHT7 A8 X9 L10 R11 L2 12 R2 13 L3 14 R3 15
      const struct { u64 sw; int id; } map[] = {{HidNpadButton_B, 0}, {HidNpadButton_Y, 1}, {HidNpadButton_Minus, 2}, {HidNpadButton_Plus, 3}, {HidNpadButton_Up, 4}, {HidNpadButton_Down, 5}, {HidNpadButton_Left, 6}, {HidNpadButton_Right, 7}, {HidNpadButton_A, 8}, {HidNpadButton_X, 9}, {HidNpadButton_L, 10}, {HidNpadButton_R, 11}, {HidNpadButton_ZL, 12}, {HidNpadButton_ZR, 13}, {HidNpadButton_StickL, 14}, {HidNpadButton_StickR, 15}};
      for (auto& m : map) if (held & m.sw) p.buttons |= 1u << m.id;
      HidAnalogStickState l = padGetStickPos(&pad, 0), r = padGetStickPos(&pad, 1);
      p.lx = l.x; p.ly = -l.y; p.rx = r.x; p.ry = -r.y;
      double t0 = nowMs(); gfx::coreEnter(); double t1 = nowMs(); ps2_run(&p, NULL); double t2 = nowMs(); gfx::coreLeave(); double t3 = nowMs(); emuMs += t3 - t0; enterMs += t1 - t0; runMs += t2 - t1; leaveMs += t3 - t2; if (t2 - t1 > worstMs) worstMs = t2 - t1; lastCoreMs = (float)(t3 - t0); frames++;
      const int16_t* a; int n = ps2_audio_take(&a); audioTotal += n; if (sound && n) audio::push(a, n);
      if (ps2_frame_serial() != lastSerial) { lastSerial = ps2_frame_serial(); shown++; int w = ps2_video_width(), h = ps2_video_height(); if (ps2_video_is_gpu()) { double c0 = nowMs(); gfx::pictureFromCore(pic, w, h); copyMs += nowMs() - c0; } else if (w <= 2560 && h <= 2048) gfx::pictureUpload(pic, ps2_video(), w, h, ps2_video_pitch()); }
    }
    double t = nowMs();
    if (t - loopAt > loopWorst) loopWorst = t - loopAt;
    graphLoop[graphPos] = (float)(t - loopAt); graphCore[graphPos] = lastCoreMs; graphPos = (graphPos + 1) % GRAPH_N;
    loopAt = t;
    if (t - statAt >= 2000) {
      fpsEmu = frames * 1000.0 / (t - statAt); fpsPic = shown * 1000.0 / (t - statAt); msPerFrame = frames ? emuMs / frames : 0;
      if (loggedStats < 200) { loggedStats++; unsigned long long jn, jb, jt; memjit_get_stats(&jn, &jb, &jt); record("stats: %.1f emu fps, %.1f pictures/s, %.1f ms per frame, %dx%d, audio %ld; jit publishes %llu, %llu KB, %.0f ms total", fpsEmu, fpsPic, msPerFrame, ps2_video_width(), ps2_video_height(), audioTotal, jn, jb >> 10, armTicksToNs(jt) / 1e6); }
      if (frames) snprintf(breakdown, sizeof breakdown, "switch in %.2f, core %.2f (worst %.1f), switch out %.2f, copy %.2f ms; slowest loop %.1f ms", enterMs / frames, runMs / frames, worstMs, leaveMs / frames, copyMs / frames, loopWorst);
      if (loggedStats < 200) record("  %s", breakdown);
      frames = shown = 0; emuMs = 0; statAt = t; enterMs = runMs = leaveMs = worstMs = copyMs = loopWorst = 0;
    }
    gfx::beginFrame(gfx::Color{0.05f, 0.05f, 0.08f, 1});
    int w = ps2_video_width(), h = ps2_video_height();
    if (w > 0 && h > 0 && w <= 2560 && h <= 2048) gfx::pictureDraw(pic, (1920 - 1440) / 2, 0, 1440, 1080, 0, 0, w, h);
    char line[256]; snprintf(line, sizeof line, "SW2PS2 bring-up  |  %.1f fps emulated, %.1f pictures/s, %.1f ms per frame  |  %dx%d  |  + and - together: exit", fpsEmu, fpsPic, msPerFrame, w, h);
    gfx::rect(0, 0, gfx::UI_W, 30, gfx::Color{0, 0, 0, 0.6f});
    gfx::text(line, 12, 4, 17, gfx::rgb(0xffffff));
    gfx::rect(0, 30, gfx::UI_W, 26, gfx::Color{0, 0, 0, 0.6f}); gfx::text(breakdown, 12, 33, 15, gfx::rgb(0xc0c6d4));
    {  // Frame-time graph (last 240 frames): white = whole loop, green = time in the core; lines at 16.7 and 33.3 ms.
      const float gx = 12, gy = gfx::UI_H - 130, gw = GRAPH_N * 2, gh = 110, msMax = 50;
      gfx::rect(gx - 4, gy - 4, gw + 8, gh + 8, gfx::Color{0, 0, 0, 0.6f}, 4);
      for (int i = 0; i < GRAPH_N; i++) {
        int k = (graphPos + i) % GRAPH_N;
        float l = graphLoop[k] > msMax ? msMax : graphLoop[k], c = graphCore[k] > msMax ? msMax : graphCore[k];
        gfx::rect(gx + i * 2, gy + gh - l / msMax * gh, 2, l / msMax * gh, graphLoop[k] > 17.5f ? gfx::rgb(0xff6060) : gfx::rgb(0xd0d0d0));
        gfx::rect(gx + i * 2, gy + gh - c / msMax * gh, 2, c / msMax * gh, gfx::rgb(0x40c070));
      }
      gfx::rect(gx, gy + gh - 16.7f / msMax * gh, gw, 1, gfx::rgb(0xffff80));
      gfx::rect(gx, gy + gh - 33.3f / msMax * gh, gw, 1, gfx::rgb(0xffa040));
    }
    if (!status.empty()) gfx::text(status, 12, 60, 22, gfx::rgb(0xffb0b0));
    gfx::endFrame(0);
  }
  appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
  record("exiting");
  gfx::coreEnter(); ps2_unload(); gfx::coreLeave(); gfx::coreDestroy();
  record("unloaded");
  audio::shutdown(); gfx::shutdown(); romfsExit();
  return 0;
}
