#!/usr/bin/env python3
"""One-off: derive SW2PS2's source/main.cpp from SW2SNES's (the shared front end).
Kept for reference; the result is edited by hand afterwards."""
import sys
s = open('/Users/rodpad/SW2SNES/source/main.cpp').read()
def rep(a, b, n=1):
    global s
    assert s.count(a) == n, (a[:70], s.count(a))
    s = s.replace(a, b)

rep('// SW2SNES for hbmenu (Switch 2 homebrew, kaiHEN): SNES emulator front end.', '// SW2PS2 for hbmenu (Switch 2 homebrew, kaiHEN): PlayStation 2 emulator front end\n// (the SW2NES/SW2SNES front end on the LRPS2 core).')
rep('static const CoreApi* C = &SNES9X_CORE;', 'static const CoreApi* C = &PS2_CORE;')
rep('"sdmc:/roms/snes"', '"sdmc:/roms/ps2"')
rep('"sdmc:/switch/sw2snes"', '"sdmc:/switch/sw2ps2"')
rep('static const double NES_RATE = 60.0988;', 'static double NES_RATE = 59.94;  // the running game\'s frame rate (59.94 NTSC, 50 PAL)')
rep('static bool isRomExt(const string& n) { return hasExt(n, ".sfc") || hasExt(n, ".smc") || hasExt(n, ".fig") || hasExt(n, ".swc"); }',
    'static bool isRomExt(const string& n) { return hasExt(n, ".iso") || hasExt(n, ".chd") || hasExt(n, ".cso"); }')
rep('static string stripNes(const string& n) { return isRomExt(n) || hasExt(n, ".zip") ? n.substr(0, n.size() - 4) : n; }', 'static string stripNes(const string& n) { return isRomExt(n) ? n.substr(0, n.size() - 4) : n; }')
rep('        if (isRomExt(n) || hasExt(n, ".zip")) found.push_back(Rom{n, dir});', '        if (isRomExt(n)) found.push_back(Rom{n, dir});')
rep('SW2SNES exits', 'SW2PS2 exits')
rep('"SW2SNES"', '"SW2PS2"')
rep('record("SW2SNES %s", APP_VERSION);', 'record("SW2PS2 %s", APP_VERSION);')
rep('Copy .nes (or zipped) games there', 'Copy PS2 disc images (.iso, .chd or .cso) there')

# Defaults.
rep('static int aspectMode = 2, scaleMode = 0, cropMode = 2, crtStyle = 0, paletteMode = 0, statsLevel = 0;', 'static int aspectMode = 2, scaleMode = 1, cropMode = 0, crtStyle = 0, paletteMode = 0, statsLevel = 0;')
rep('static int runAhead = 1, frameDelay = 2, audioBuffer = 0, displayMode = 1, ffMode = 1, ffSpeed = 1;', 'static int runAhead = 0, frameDelay = 0, audioBuffer = 1, displayMode = 0, ffMode = 0, ffSpeed = 1;')
rep('static bool rewindOn = true;', 'static bool rewindOn = false;\n// PS2 options (apply when a game starts).\nstatic const vector<string> RENDERERS = {"GPU", "Software"}, RESOLUTIONS = {"1x (native)", "2x", "4x"};\nstatic const char* RESOLUTION_VALUES[3] = {"1x (Native)", "2x", "4x"};\nstatic const int RESOLUTION_SCALE[3] = {1, 2, 4};\nstatic const vector<string> EE_SPEEDS = {"100%", "75%", "60%", "50%"};\nstatic const char* EE_SPEED_VALUES[4] = {"100% (Normal Speed)", "75% (Underclock)", "60% (Underclock)", "50% (Underclock)"};\nstatic int ps2Renderer = 0, ps2Resolution = 0, ps2EeSpeed = 0;\nstatic bool cpuBoost = false;\nstatic int renderScale = 1;  // the running game\'s resolution multiplier')
rep('static int cornerRadius = 1, cornerSoftness = 1, vignette = 1, vignetteSize = 0, curvature = 0, shader = 6, signalMode = 0, boost = 0;', 'static int cornerRadius = 0, cornerSoftness = 1, vignette = 0, vignetteSize = 0, curvature = 0, shader = 2, signalMode = 0, boost = 0;')
rep('static const vector<string> ASPECTS = {"1:1", "8:7", "4:3", "Stretch"}', 'static const vector<string> ASPECTS = {"1:1", "16:9", "4:3", "Stretch"}')
rep('double par = aspectMode == 1 ? 8.0 / 7.0 : aspectMode == 2', 'double par = aspectMode == 1 ? (16.0 / 9.0) * ch / cw : aspectMode == 2')
rep('static int logW() { return W > 256 ? W / 2 : W; }', 'static int logW() { return std::max(1, W / renderScale); }')
rep('static int logH() { return H > 240 ? H / 2 : H; }', 'static int logH() { return std::max(1, H / renderScale); }')
rep('static int W = 256, H = 224;', 'static int W = 640, H = 448;')
rep('  W = 256; H = 224;\n  picture = gfx::pictureCreate(512, 478);  // the largest SNES frame (512 x 478 interlaced)', '  W = 640; H = 448;\n  picture = gfx::pictureCreate(2560, 2048);  // up to 4x the largest PS2 frame (640 x 512)')

# Frame display.
rep('''  int w = C->width(), h = C->height();
  if (w != W || h != H) { W = w; H = h; fitPicture(); crtKey.clear(); applyCrt(); }
  gfx::pictureUpload(picture, C->video(), W, H, C->video_pitch());''',
'''  int w = C->width(), h = C->height();
  if (w <= 0 || h <= 0 || w > 2560 || h > 2048) return;  // nothing shown yet
  if (w != W || h != H) { W = w; H = h; fitPicture(); crtKey.clear(); applyCrt(); }
  if (ps2core_is_gpu()) gfx::pictureFromCore(picture, W, H);
  else if (C->video()) gfx::pictureUpload(picture, C->video(), W, H, C->video_pitch());''')

# Save states are large.
rep('if (!readFile(statePath(n), d, 1024 * 1024))', 'if (!readFile(statePath(n), d, 256L * 1024 * 1024))')

# Game start: a disc image is opened by path, with the PS2 options.
a = s.index('  vector<uint8_t> data;\n  string readErr;\n  if (!readRom(rom, data, readErr))'); b = s.index('  running = new Running();')
s = s[:a] + '''  static string romPath;
  romPath = rom.dir + "/" + rom.name;
  char bios[256];
  if (!ps2core_find_bios(bios, sizeof bios)) {
    showMessage("No PS2 BIOS found.\\n\\nCopy the BIOS dump from your own PS2 to\\n\\n    /switch/sw2ps2/system/pcsx2/bios/\\n\\non the SD card, then start the game again.", [] { mode = "browser"; });
    return;
  }
  bool gpu = ps2Renderer == 0;
  renderScale = gpu ? RESOLUTION_SCALE[ps2Resolution] : 1;
  const char* gpuErr = nullptr;
  if (gpu && !ps2core_use_gpu(true, renderScale, &gpuErr)) { record("GPU renderer unavailable: %s", gpuErr ? gpuErr : "?"); gpu = false; renderScale = 1; }
  if (!gpu) ps2core_use_gpu(false, 1, nullptr);
  ps2core_option("pcsx2_bios", bios);
  ps2core_option("pcsx2_fastboot", "enabled");
  ps2core_option("pcsx2_renderer", gpu ? "OpenGL" : "Software (SW)");
  ps2core_option("pcsx2_upscale_multiplier", gpu ? RESOLUTION_VALUES[ps2Resolution] : "1x (Native)");
  ps2core_option("pcsx2_sw_renderer_threads", "3");
  ps2core_option("pcsx2_ee_cycle_rate", EE_SPEED_VALUES[ps2EeSpeed]);
  record("Starting %s: BIOS %s, renderer %s, %dx, EE %s, boost %d", rom.name.c_str(), bios, gpu ? "GPU" : "software", renderScale, EE_SPEEDS[ps2EeSpeed].c_str(), (int)cpuBoost);
  appletSetCpuBoostMode(cpuBoost ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
  // Loading takes a moment: say so.
  gfx::beginFrame(col(C_BG)); gfx::text("Starting " + stripNes(rom.name) + "...", 60, 60, 30, col(C_TEXT)); gfx::endFrame(0);
  gfx::beginFrame(col(C_BG)); gfx::text("Starting " + stripNes(rom.name) + "...", 60, 60, 30, col(C_TEXT));
  core_rom_name = romPath.c_str();
  if (const char* err = C->load()) {
    C->unload();
    appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
    showMessage(err, [] { mode = "browser"; });
    return;
  }
  NES_RATE = ps2core_fps() > 1 ? ps2core_fps() : 59.94;
''' + s[b:]
rep('  W = C->width(); H = C->height();\n  record("Core %s, battery', '  if (C->width() > 0) { W = C->width(); H = C->height(); }\n  record("Core %s, battery')
rep('  C->unload();\n  if (running->perGame) setGameValues(running->globalVals);', '  C->unload();\n  appletSetCpuBoostMode(ApmCpuBoostMode_Normal);\n  if (running->perGame) setGameValues(running->globalVals);')

# Input: a DualShock 2 per player.
rep('// In-game menu: ZL + ZR (L and R are SNES buttons), as on Nintendo\'s own SNES app.\nstatic const u64 MENU_COMBO = HidNpadButton_ZL | HidNpadButton_ZR;',
'''// PS2 pads: every Switch button is a DualShock 2 button in the same place
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
// In-game menu: both sticks clicked (L3 + R3); every other button belongs to the game.
static const u64 MENU_COMBO = HidNpadButton_StickL | HidNpadButton_StickR;''')
rep('    p = snesPad(bits); p2 = behindMenu ? 0 : snesPad(bits2, faceMap2);', '    setPs2Pad(0, bits, behindMenu); setPs2Pad(1, bits2, behindMenu);')
rep('  bool zr = !behindMenu && !both && (bits & HidNpadButton_ZR), zl = !behindMenu && !both && (bits & HidNpadButton_ZL);', '  bool zr = false, zl = false;  // ZL/ZR are the PS2\'s L2/R2: no fast-forward or rewind here\n  (void)both;')
rep('  bool hz120 = !highRefresh() && (tickMs < 15.5 || tickMs > 18);', '  bool hz120 = !highRefresh() && (tickMs < 15.5 || tickMs > 18 || fabs(NES_RATE - 59.94) > 1);  // PAL games follow the clock')
rep('"ZL + ZR: Menu"', '"L3 + R3: Menu"', 2)
rep('open the menu (ZL + ZR)', 'open the menu (click both sticks)')
rep('  padInitialize(&pad2, HidNpadIdType_No2);\n', '  padInitialize(&pad2, HidNpadIdType_No2);\n  padStates[0] = &pad1; padStates[1] = &pad2;\n  ps2core_setup(DATA_DIR, [](const char* line) { if (coreLogLines < 6000) { coreLogLines++; record("[core] %s", line); } });\n')
rep('static FILE* logFile = nullptr;', 'static FILE* logFile = nullptr;\nstatic int coreLogLines = 0;')

# Settings: PS2 rows; rows for features this core does not have are left out.
rep('      {"Aspect", [] { return ASPECTS; }, [] { return aspectMode; }, [](int v) { aspectMode = v; }},\n      {"Scale", [] { return SCALES; }',
'''      {"Renderer (next start)", [] { return RENDERERS; }, [] { return ps2Renderer; }, [](int v) { ps2Renderer = v; }},
      {"Resolution (next start)", [] { return RESOLUTIONS; }, [] { return ps2Resolution; }, [](int v) { ps2Resolution = v; }},
      {"Emulated CPU speed (next start)", [] { return EE_SPEEDS; }, [] { return ps2EeSpeed; }, [](int v) { ps2EeSpeed = v; }},
      {"CPU boost mode (next start)", [] { return OFF_ON; }, [] { return cpuBoost ? 1 : 0; }, [](int v) { cpuBoost = v != 0; }},
      {"Aspect", [] { return ASPECTS; }, [] { return aspectMode; }, [](int v) { aspectMode = v; }},
      {"Scale", [] { return SCALES; }''')
rep('  for (auto& it : settingsItems())\n    if (showAdvanced || !contains(ADVANCED_SETTINGS, it.label)) SETTINGS_ITEMS.push_back(it);',
'''  // Not on this core: run-ahead, rewind, fast-forward, turbo and button remapping.
  static const vector<string> NOT_HERE = {"Run-ahead", "Rewind (hold ZL)", "Fast-forward", "Fast-forward speed", "Turbo speed", "Button mapping for", "Audio filter",
      "Switch A button", "Switch B button", "Switch X button", "Switch Y button", "Switch L button", "Switch R button"};
  for (auto& it : settingsItems())
    if (!contains(NOT_HERE, it.label) && (showAdvanced || !contains(ADVANCED_SETTINGS, it.label))) SETTINGS_ITEMS.push_back(it);''')
rep('  if (!showAdvanced) {\n    vector<MenuItem> basic;\n    for (auto& m : items) if (!contains(ADVANCED_MENU, m.label)) basic.push_back(m);\n    items = basic;\n  }',
'''  {
    vector<MenuItem> kept;
    for (auto& m : items) if (m.label != "Run-ahead" && m.label != "Cheats..." && (showAdvanced || !contains(ADVANCED_MENU, m.label))) kept.push_back(m);
    items = kept;
  }''')
rep('static const vector<string> ADVANCED_SETTINGS = {"Scale",', 'static const vector<string> ADVANCED_SETTINGS = {"Emulated CPU speed (next start)", "CPU boost mode (next start)", "Scale",')
rep('  j.set("autoLoad", Json::boolean(autoLoadState));', '  j.set("ps2Renderer", Json::number(ps2Renderer)); j.set("ps2Resolution", Json::number(ps2Resolution)); j.set("ps2EeSpeed", Json::number(ps2EeSpeed)); j.set("cpuBoost", Json::boolean(cpuBoost));\n  j.set("autoLoad", Json::boolean(autoLoadState));')
rep('  if (const Json* b = v.get("rewind")) if (b->isBool()) rewindOn = b->b;', '  ps2Renderer = pick(v.get("ps2Renderer"), RENDERERS.size(), ps2Renderer); ps2Resolution = pick(v.get("ps2Resolution"), RESOLUTIONS.size(), ps2Resolution);\n  ps2EeSpeed = pick(v.get("ps2EeSpeed"), EE_SPEEDS.size(), ps2EeSpeed);\n  if (const Json* b = v.get("cpuBoost")) if (b->isBool()) cpuBoost = b->b;\n  runAhead = 0; ffMode = 0; rewindOn = false;  // not on this core')
open('source/main.cpp', 'w').write(s)
print('ok', len(s))
