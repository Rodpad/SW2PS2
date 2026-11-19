// SW2PS2: the OunceSX2 libretro core behind a small C API (bridge_core.cpp is
// the libretro "frontend" side: environment, video, audio and input callbacks).
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ps2_log_fn)(int level, const char* line);

// Directories: system holds pcsx2/bios/<bios files>; save holds memory cards.
void ps2_init(const char* systemDir, const char* saveDir, ps2_log_fn log);
void ps2_set_option(const char* key, const char* value);  // core option (pcsx2_*); before ps2_load unless the core polls it
// Boots a disc image (path) or the BIOS alone (NULL). Returns NULL or an error.
const char* ps2_load(const char* path);
void ps2_unload(void);
int ps2_loaded(void);
void ps2_reset(void);
// GPU rendering: give the bridge the frontend's GL target before ps2_load;
// every bridge call that reaches the core (load, run, unload, reset, states)
// must then be made with the core's GL context current. NULL fb = software only.
void ps2_set_gl(uintptr_t (*framebuffer)(void), void* (*proc)(const char* name));
int ps2_video_is_gpu(void);  // the last frame is in the GL target (ps2_video() is not updated)
#ifdef SW2_VK
void ps2_set_vulkan(int on);  // before ps2_load: the core renders with Vulkan and the front end presents (vkpresent)
int ps2_vulkan_active(void);  // the loaded game got its Vulkan display
#endif

// One frame. Pad: buttons = libretro joypad bits (RETRO_DEVICE_ID_JOYPAD_*),
// sticks -32768..32767 (lx, ly, rx, ry).
typedef struct { uint32_t buttons; int16_t lx, ly, rx, ry; } ps2_pad;
void ps2_run(const ps2_pad* p1, const ps2_pad* p2);
// Last frame (RGBA, video_pitch pixels per row); frame_serial changes when the core showed a new one.
const uint32_t* ps2_video(void);
int ps2_video_width(void);
int ps2_video_height(void);
int ps2_video_pitch(void);
unsigned ps2_frame_serial(void);
double ps2_fps(void);          // 59.94 or 50
float ps2_aspect(void);        // display aspect ratio the core reports
void ps2_rumble(int port, uint16_t* strong, uint16_t* weak);  // current motor strengths (0-65535)
// Audio produced since the last take (48 kHz stereo interleaved).
int ps2_audio_take(const int16_t** out);
// Save states (in memory). size 0 = unsupported right now.
long ps2_state_size(void);
int ps2_state_save(void* buf, long size);
int ps2_state_load(const void* buf, long size);

#ifdef __cplusplus
}
#endif
