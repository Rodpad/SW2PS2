// SW2PS2: the OunceSX2 core (native/bridge_core.cpp, ps2_*) behind the same
// function table the SW2NES/SW2SNES front end uses, so the front end keeps its
// structure. See cores.cpp. Run-ahead and rewind are unavailable
// (a PS2 state is ~46 MB).
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

struct CoreApi {
  const char* name;
  const char* stateTag;  // save-state file tag
  uint8_t* (*rom_alloc)(long);
  void (*unload)();
  const char* (*load)();  // the ROM in rom_alloc's buffer; rom_name set first
  void (*power)();
  void (*reset)(int);
  void (*reconvert)();
  int (*frame)(int, int, int);
  void (*set_audio)(int, int);
  void (*set_audio_ratio)(double);
  int (*audio_take)();
  int16_t* (*audio)();
  void (*audio_reset)();
  void (*set_audio_discard)(int);
  uint32_t* (*video)();     // RGBA, video_pitch() pixels per row
  int (*video_pitch)();
  int (*width)();           // size of the last frame (256/512 x 224/239/448/478)
  int (*height)();
  int (*has_battery)();
  uint8_t* (*sram)();
  int (*sram_size)();
  long (*state_save)();
  char* (*state_buffer)(long);
  int (*state_load)(long);
  int (*runahead)(int, int, int);
};
extern const CoreApi PS2_CORE;
extern const char* core_rom_name;  // full path of the disc image, set before load()

// PS2 extras (cores.cpp).
struct Ps2Input { uint32_t buttons; int16_t lx, ly, rx, ry; };  // libretro joypad bits + sticks
extern Ps2Input ps2Input[4];           // read by frame(); the int pad arguments are ignored
void ps2core_setup(const char* dataDir, void (*log)(const char* line));  // once at start
void ps2core_option(const char* key, const char* value);                 // core option (pcsx2_*)
bool ps2core_use_gpu(bool on, int scale, const char** error);            // before load(): GPU renderer target
#ifdef SW2_VK
void ps2core_use_vulkan(bool on);     // before load(): Vulkan renderer + display (vkpresent)
bool ps2core_vulkan();                // the loaded game shows through the Vulkan display
#endif
bool ps2core_is_gpu();                 // the last frame is in the GPU target (video() is stale)
double ps2core_fps();                  // 59.94 (NTSC) or 50 (PAL) for the loaded game
bool ps2core_find_bios(const char* dir, char* path, int size);  // a BIOS file (4-8 MB) in dir or its subfolders: its full path

// Cheats for the current disc revision. Returns the matching custom PNACH path,
// or an empty string until the core identifies the disc.
struct Ps2Cheat { std::string name; bool bootOnly; };
std::string ps2core_cheats(std::vector<Ps2Cheat>& cheats);
void ps2core_reload_cheats();

// Rewind history (delta-compressed states; see cores.cpp).
bool rewind_init(long budgetBytes);
void rewind_clear();
bool rewind_push();      // saves the current state
bool rewind_pop();       // steps back one state and loads it
bool rewind_reload();    // loads the current state again (after drawing it)
int rewind_count();
long rewind_bytes();
