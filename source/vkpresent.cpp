// SW2PS2: the libretro Vulkan frontend side for the ARMSX2 core (SW2_VK builds).
//
// The core renders the GS with Vulkan on a device it creates through the
// context-negotiation interface (create_device) and hands each finished frame
// over with set_image. The GL front end keeps the screen: OunceVK gives the
// default NWindow to one swapchain for the life of the process (a second owner,
// even after the first is gone, finds it in use or lost), so the frame is
// copied into host memory here and uploaded to the GL picture like a software
// frame. At 1x that is 1.2 MB a frame.
//
// The copy is asynchronous: a new frame's copy is submitted at once, and grab()
// hands over whichever copy has finished - the new frame itself when the GPU is
// already done with it (no added delay), otherwise the one before, with the new
// one following at the next call. The main thread never waits for the GPU
// unless both copy slots are still busy.
#ifdef SW2_VK
#include "vkpresent.h"

#include <switch.h>
#define VK_USE_PLATFORM_VI_NN
#include <vulkan/vulkan.h>
#include "libretro.h"
#include "libretro_vulkan.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <mutex>
#include <string>
#include <vector>

void sw2_log_line(const char* line);  // source/main.cpp
static void record(const char* fmt, ...) {
  char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  sw2_log_line(b);
}

namespace vkp {
namespace {
VkInstance instance;
VkPhysicalDevice gpu;
VkDevice device;
VkQueue queue;
uint32_t queueFamily;
VkCommandPool pool;

// One copy in flight per slot.
struct Slot {
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  void* mapped = nullptr;
  bool cached = false;
  bool busy = false;        // a copy was submitted and not yet handed over
  uint64_t serial = 0;      // which frame it holds
  unsigned w = 0, h = 0;
  VkFormat format = VK_FORMAT_UNDEFINED;
};
constexpr int kSlots = 2;
Slot slots[kSlots];
uint64_t submitted, shown;     // frame serials: last copy submitted, last handed over
std::vector<uint32_t> pixels;  // RGBA for a BGRA frame (swapped)
unsigned waits;                // times both slots were busy (diagnostics)
bool measure = false;
HandoffStats handoff{};

uint64_t elapsedNs(uint64_t start) { return armTicksToNs(armGetSystemTick() - start); }

std::recursive_mutex queueLock;
retro_hw_render_interface_vulkan iface;
retro_vulkan_image image;  // the core's latest frame
bool haveImage, newImage;
bool deviceUp;
unsigned frameW, frameH;

void ifSetImage(void*, const retro_vulkan_image* im, uint32_t, const VkSemaphore*, uint32_t) {
  if (im) { image = *im; haveImage = true; newImage = true; } else haveImage = false;
}
uint32_t ifSyncIndex(void*) { return 0; }
uint32_t ifSyncMask(void*) { return 1; }
void ifSetCommandBuffers(void*, uint32_t, const VkCommandBuffer*) {}
void ifWaitSync(void*) {}
void ifLock(void*) { queueLock.lock(); }
void ifUnlock(void*) { queueLock.unlock(); }
void ifSignal(void*, VkSemaphore) {}

bool fail(std::string& err, const char* what, VkResult r) { err = std::string(what) + " failed (" + std::to_string((int)r) + ")"; return false; }

void freeBuffer(Slot& s) {
  if (s.mapped) vkUnmapMemory(device, s.memory);
  if (s.buffer) vkDestroyBuffer(device, s.buffer, nullptr);
  if (s.memory) vkFreeMemory(device, s.memory, nullptr);
  s.mapped = nullptr; s.buffer = VK_NULL_HANDLE; s.memory = VK_NULL_HANDLE; s.size = 0;
}

bool ensureBuffer(Slot& s, VkDeviceSize size) {
  if (size <= s.size) return true;
  freeBuffer(s);
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = size; bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(device, &bi, nullptr, &s.buffer) != VK_SUCCESS) { s.buffer = VK_NULL_HANDLE; return false; }
  VkMemoryRequirements req; vkGetBufferMemoryRequirements(device, s.buffer, &req);
  VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(gpu, &mp);
  int pick = -1;
  for (int pass = 0; pass < 2 && pick < 0; pass++)  // host-cached first: the CPU reads it
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
      VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | (pass == 0 ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT : 0);
      if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) { pick = (int)i; break; }
    }
  if (pick < 0) { freeBuffer(s); return false; }
  s.cached = (mp.memoryTypes[pick].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0;
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size; ai.memoryTypeIndex = (uint32_t)pick;
  if (vkAllocateMemory(device, &ai, nullptr, &s.memory) != VK_SUCCESS) { s.memory = VK_NULL_HANDLE; freeBuffer(s); return false; }
  vkBindBufferMemory(device, s.buffer, s.memory, 0);
  if (vkMapMemory(device, s.memory, 0, VK_WHOLE_SIZE, 0, &s.mapped) != VK_SUCCESS) { s.mapped = nullptr; freeBuffer(s); return false; }
  s.size = size;
  record("vulkan: readback buffer %u KB (%s)", (unsigned)(size >> 10), s.cached ? "cached" : "uncached");
  return true;
}

void barrier(VkCommandBuffer cb, VkImage im, VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst,
             VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.srcAccessMask = src; b.dstAccessMask = dst; b.oldLayout = from; b.newLayout = to;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = im; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(cb, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
}

bool done(Slot& s) { return vkGetFenceStatus(device, s.fence) == VK_SUCCESS; }

// Submits the copy of the core's current image into slot s.
bool submitCopy(Slot& s) {
  const VkDeviceSize size = (VkDeviceSize)frameW * frameH * 4;
  if (!ensureBuffer(s, size)) return false;
  VkImage src = image.create_info.image;
  vkResetCommandBuffer(s.cmd, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(s.cmd, &bi);
  barrier(s.cmd, src, image.image_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
          VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  VkBufferImageCopy c{};
  c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  c.imageExtent = {frameW, frameH, 1};
  vkCmdCopyImageToBuffer(s.cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s.buffer, 1, &c);
  barrier(s.cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.image_layout, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  VkBufferMemoryBarrier hb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  hb.srcQueueFamilyIndex = hb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  hb.buffer = s.buffer; hb.size = VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(s.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &hb, 0, nullptr);
  vkEndCommandBuffer(s.cmd);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1; si.pCommandBuffers = &s.cmd;
  vkResetFences(device, 1, &s.fence);
  VkResult r;
  { std::lock_guard<std::recursive_mutex> l(queueLock); r = vkQueueSubmit(queue, 1, &si, s.fence); }
  if (r != VK_SUCCESS) { record("vulkan: readback submit failed (%d)", (int)r); return false; }
  s.busy = true; s.serial = ++submitted; s.w = frameW; s.h = frameH; s.format = image.create_info.format;
  return true;
}

// The finished copy in s, as RGBA.
const uint32_t* take(Slot& s, int* w, int* h) {
  s.busy = false; shown = s.serial;
  if (s.cached) {
    VkMappedMemoryRange mr{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    mr.memory = s.memory; mr.size = VK_WHOLE_SIZE;
    vkInvalidateMappedMemoryRanges(device, 1, &mr);
  }
  *w = (int)s.w; *h = (int)s.h;
  if (s.format == VK_FORMAT_B8G8R8A8_UNORM || s.format == VK_FORMAT_B8G8R8A8_SRGB) {
    pixels.resize((size_t)s.w * s.h);
    const uint32_t* p = (const uint32_t*)s.mapped;
    for (size_t i = 0, n = pixels.size(); i < n; i++) { uint32_t v = p[i]; pixels[i] = 0xff000000u | ((v & 0xff) << 16) | (v & 0xff00) | ((v >> 16) & 0xff); }
    return pixels.data();
  }
  return (const uint32_t*)s.mapped;  // uploaded straight from the mapped copy (no second copy)
}
}  // namespace

bool prepare(std::string& err) {
  if (instance) return true;
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "ARMSX2"; app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  VkResult r = vkCreateInstance(&ici, nullptr, &instance);
  if (r != VK_SUCCESS) { instance = VK_NULL_HANDLE; return fail(err, "vkCreateInstance", r); }
  uint32_t n = 1; r = vkEnumeratePhysicalDevices(instance, &n, &gpu);
  if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || !n) { vkDestroyInstance(instance, nullptr); instance = VK_NULL_HANDLE; return fail(err, "no Vulkan GPU", r); }
  record("vulkan: instance ready");
  return true;
}

bool begin(const void* negotiation, std::string& err) {
  const auto* neg = (const retro_hw_render_context_negotiation_interface_vulkan*)negotiation;
  if (!neg || !neg->create_device) { err = "the core gave no Vulkan negotiation interface"; return false; }
  if (!prepare(err)) return false;
  retro_vulkan_context ctx{};
  record("vulkan: create_device");
  if (!neg->create_device(&ctx, instance, gpu, VK_NULL_HANDLE, vkGetInstanceProcAddr, nullptr, 0, nullptr, 0, nullptr)) {
    err = "the core could not create its Vulkan device"; return false;
  }
  device = ctx.device; queue = ctx.queue; queueFamily = ctx.queue_family_index; gpu = ctx.gpu;
  deviceUp = true;
  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = queueFamily;
  vkCreateCommandPool(device, &pci, nullptr, &pool);
  for (Slot& s : slots) {
    s = Slot();
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &cai, &s.cmd);
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(device, &fci, nullptr, &s.fence);
  }
  submitted = shown = 0; waits = 0;
  iface = {};
  iface.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
  iface.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
  iface.handle = &iface;
  iface.instance = instance; iface.gpu = gpu; iface.device = device;
  iface.get_device_proc_addr = vkGetDeviceProcAddr; iface.get_instance_proc_addr = vkGetInstanceProcAddr;
  iface.queue = queue; iface.queue_index = queueFamily;
  iface.set_image = ifSetImage; iface.get_sync_index = ifSyncIndex; iface.get_sync_index_mask = ifSyncMask;
  iface.set_command_buffers = ifSetCommandBuffers; iface.wait_sync_index = ifWaitSync;
  iface.lock_queue = ifLock; iface.unlock_queue = ifUnlock; iface.set_signal_semaphore = ifSignal;
  haveImage = newImage = false;
  record("vulkan: device ready (queue family %u)", queueFamily);
  return true;
}

void setFrameSize(unsigned w, unsigned h) { frameW = w; frameH = h; }
void* hwInterface() { return deviceUp ? &iface : nullptr; }
bool active() { return deviceUp; }
unsigned slotWaits() { return waits; }
void measureHandoff(bool enabled) { measure = enabled; handoff = {}; waits = 0; }
bool measuringHandoff() { return measure; }
void recordUpload(uint64_t ns) {
  handoff.uploads++;
  handoff.uploadNs += ns;
  if (ns > handoff.maxUploadNs) handoff.maxUploadNs = ns;
}
HandoffStats handoffStats() { return handoff; }

const uint32_t* grab(int* w, int* h) {
  if (!deviceUp) return nullptr;
  // A new frame: copy it into a free slot (waiting only when both are still busy: the older one
  // is then handed over first, so no frame is skipped that has been copied).
  if (newImage && haveImage && image.create_info.image && frameW && frameH) {
    newImage = false;
    Slot* freeSlot = nullptr;
    for (Slot& s : slots) if (!s.busy) { freeSlot = &s; break; }
    if (!freeSlot) {  // both busy: drop the older copy (a newer frame supersedes it)
      Slot* older = slots[0].serial < slots[1].serial ? &slots[0] : &slots[1];
      const uint64_t start = measure ? armGetSystemTick() : 0;
      vkWaitForFences(device, 1, &older->fence, VK_TRUE, UINT64_MAX);
      if (measure) handoff.waitsNs += elapsedNs(start);
      older->busy = false; waits++;
      freeSlot = older;
    }
    const uint64_t start = measure ? armGetSystemTick() : 0;
    const bool submittedCopy = submitCopy(*freeSlot);
    if (measure) {
      handoff.submitsNs += elapsedNs(start);
      if (submittedCopy) {
        handoff.copies++;
        handoff.bytes += (uint64_t)frameW * frameH * 4;
      }
    }
  }
  // Hand over the newest finished copy that is newer than the last one shown.
  Slot* best = nullptr;
  for (Slot& s : slots)
    if (s.busy && s.serial > shown && done(s) && (!best || s.serial > best->serial)) best = &s;
  if (!best) return nullptr;
  for (Slot& s : slots) if (s.busy && s.serial < best->serial) s.busy = false;  // older copies are stale now
  const uint64_t start = measure ? armGetSystemTick() : 0;
  const auto* pixels = take(*best, w, h);
  if (measure) handoff.mapNs += elapsedNs(start);
  return pixels;
}

void end() {
  if (device) {
    { std::lock_guard<std::recursive_mutex> l(queueLock); vkQueueWaitIdle(queue); }
    for (Slot& s : slots) {
      freeBuffer(s);
      if (s.fence) vkDestroyFence(device, s.fence, nullptr);
      s = Slot();
    }
    if (pool) vkDestroyCommandPool(device, pool, nullptr);
    pool = VK_NULL_HANDLE;
    // The device stays for the session: the core hands the same one to the next game
    // (VKLibretro.cpp; OunceVK does not get everything back from a destroyed device).
    vkDeviceWaitIdle(device);
    device = VK_NULL_HANDLE;
  }
  deviceUp = false; haveImage = newImage = false; iface = {};
  pixels.clear(); pixels.shrink_to_fit();
}
}  // namespace vkp
#endif
