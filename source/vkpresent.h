// SW2PS2: libretro Vulkan frontend side (SW2_VK builds). See vkpresent.cpp.
#pragma once
#include <stdint.h>
#include <string>
namespace vkp {
bool prepare(std::string& err);  // the instance (kept for the app's life)
// After retro_load_game: the core's device (negotiation).
bool begin(const void* negotiationInterface, std::string& err);
void* hwInterface();          // retro_hw_render_interface_vulkan* for GET_HW_RENDER_INTERFACE
void setFrameSize(unsigned w, unsigned h);
bool active();                // a device is up
// The core's latest frame in host memory (RGBA, w pixels per row), copied when it is new.
const uint32_t* grab(int* w, int* h);  // nullptr: nothing newer finished yet (keep the picture)
unsigned slotWaits();         // times a new frame found both copy slots busy
void end();                   // after the core unloads: the device and what was made on it
}
