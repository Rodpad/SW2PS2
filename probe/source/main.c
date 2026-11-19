// SW2PS2 probe: checks the system features the LRPS2 port depends on and
// writes the results to sdmc:/switch/sw2ps2/probe.log (flushed line by line,
// so a crash shows which step it was in).
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/stat.h>
#include <switch.h>
#include <kaihen.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <vulkan/vulkan.h>

static FILE* lg;
static void L(const char* f, ...) {
  va_list ap; va_start(ap, f); char b[1024]; vsnprintf(b, sizeof b, f, ap); va_end(ap);
  lg = fopen("sdmc:/switch/sw2ps2/probe.log", "a"); if (lg) { fputs(b, lg); fputc('\n', lg); fclose(lg); lg = NULL; }
}
static double us(u64 t0, u64 t1) { return armTicksToNs(t1 - t0) / 1000.0; }

typedef struct { u64 x[9]; u64 lr, sp, pc; u32 pstate, afsr0, afsr1, esr; u64 far; } ExcFrame;
static volatile u8* protPage; static volatile int faults, excLogged;
int sw2_exception(u32 desc, ExcFrame* f) {
  faults++;
  if (faults <= 3) L("  handler entered: desc 0x%x pc %lx far %lx esr %x sp %lx frame %p", desc, f->pc, f->far, f->esr, f->sp, f);
  if (protPage && (f->far & ~0xfffULL) == (u64)protPage) {
    Result r = svcSetMemoryPermission((void*)protPage, 0x1000, Perm_Rw); if (faults <= 3) L("  handler: unprotect rc %x, returning", r); if (R_SUCCEEDED(r)) return 1;
  }
  if (!excLogged) { excLogged = 1; L("UNHANDLED exception desc 0x%x pc %lx far %lx esr %x", desc, f->pc, f->far, f->esr); }
  return 0;
}

static void info(void) {
  static const struct { int id; const char* n; } ids[] = {
    {0, "core mask"}, {2, "alias base"}, {3, "alias size"}, {4, "heap base"}, {5, "heap size"}, {6, "total memory"}, {7, "used memory"},
    {12, "aslr base"}, {13, "aslr size"}, {14, "stack base"}, {15, "stack size"}, {21, "total non-system"}, {22, "used non-system"}, {36, "address space size"}};
  for (unsigned i = 0; i < sizeof ids / sizeof *ids; i++) { u64 v = 0; Result r = svcGetInfo(&v, ids[i].id, CUR_PROCESS_HANDLE, 0); L("  %-20s rc %x  0x%lx (%lu MB)", ids[i].n, r, v, v >> 20); }
  L("  envHasHeapOverride %d size %lu MB", envHasHeapOverride(), (unsigned long)(envGetHeapOverrideSize() >> 20));
  struct mallinfo mi = mallinfo(); L("  malloc arena %d MB, free %d MB", mi.arena >> 20, mi.fordblks >> 20);
  size_t got = 0; void* big[64]; int n = 0;
  for (; n < 64; n++) { big[n] = malloc(256 << 20); if (!big[n]) break; got += 256; }
  for (int i = 0; i < n; i++) free(big[i]);
  L("  malloc could take %zu MB in 256 MB blocks", got);
}

static void cpu(void) {
  u64 t0 = armGetSystemTick(); volatile u64 a = 1; for (u32 i = 0; i < 200000000u; i++) a = a * 6364136223846793005ULL + 1442695040888963407ULL;
  u64 t1 = armGetSystemTick(); L("  200M-iteration loop: %.0f ms (volatile LCG)", us(t0, t1) / 1000);
  L("  current core %d", svcGetCurrentProcessorNumber());
  for (int c = 0; c < 12; c++) { u64 tick = 0; Result r = svcGetInfo(&tick, InfoType_IdleTickCount, INVALID_HANDLE, c); if (R_SUCCEEDED(r)) L("  core %d present", c); }
}

static int th_ok; static void th(void* a) { th_ok = svcGetCurrentProcessorNumber() + 1; }
static void threads(void) {
  for (int c = 0; c < 8; c++) { Thread t; th_ok = 0; Result r = threadCreate(&t, th, NULL, NULL, 0x10000, 0x2C, c); if (R_SUCCEEDED(r)) { r = threadStart(&t); threadWaitForExit(&t); threadClose(&t); } L("  thread on core %d: rc %x ran on %d", c, r, th_ok - 1); }
}

static void jit(void) {
  Jit j; u64 t0 = armGetSystemTick(); Result r = jitCreate(&j, 16 << 20); u64 t1 = armGetSystemTick();
  L("  jitCreate 16 MB: rc %x, %.0f us, type %d, rw %p rx %p", r, us(t0, t1), R_SUCCEEDED(r) ? j.type : -1, R_SUCCEEDED(r) ? j.rw_addr : 0, R_SUCCEEDED(r) ? j.rx_addr : 0);
  if (R_FAILED(r)) return;
  u32* rw = jitGetRwAddr(&j); int (*fn)(void) = jitGetRxAddr(&j);
  r = jitTransitionToWritable(&j); L("  toWritable rc %x", r);
  rw[0] = 0x52800540; rw[1] = 0xd65f03c0;  // mov w0,#42 ; ret
  t0 = armGetSystemTick(); r = jitTransitionToExecutable(&j); t1 = armGetSystemTick();
  L("  toExecutable (whole buffer) rc %x, %.0f us", r, us(t0, t1));
  if (R_SUCCEEDED(r)) L("  call -> %d (expect 42)", fn());
  // Range publish: patch a function at 1 MB and publish only its bytes.
  u32* p = rw + (1 << 20) / 4; p[0] = 0x52800560; p[1] = 0xd65f03c0;  // mov w0,#43
  KaihenJitRange rg = {1 << 20, 8};
  t0 = armGetSystemTick(); r = kaihenJitPublishRanges(&j, &rg, 1); t1 = armGetSystemTick();
  L("  publishRanges 8 bytes rc %x, %.0f us", r, us(t0, t1));
  if (R_SUCCEEDED(r)) { int (*f2)(void) = (int (*)(void))((u8*)jitGetRxAddr(&j) + (1 << 20)); L("  call -> %d (expect 43)", f2()); }
  if (R_SUCCEEDED(r)) {
    t0 = armGetSystemTick(); for (int i = 0; i < 200; i++) { p[0] = 0x52800000 | ((i & 0xff) << 5); kaihenJitPublishRanges(&j, &rg, 1); } t1 = armGetSystemTick();
    L("  200 x publish 8 bytes: %.1f us each", us(t0, t1) / 200);
    KaihenJitRange big = {2 << 20, 64 << 10}; memset((u8*)rw + (2 << 20), 0, 64 << 10);
    t0 = armGetSystemTick(); for (int i = 0; i < 20; i++) kaihenJitPublishRanges(&j, &big, 1); t1 = armGetSystemTick();
    L("  20 x publish 64 KB: %.1f us each", us(t0, t1) / 20);
    int (*f2)(void) = (int (*)(void))((u8*)jitGetRxAddr(&j) + (1 << 20)); L("  call after patches -> %d (expect 199)", f2());
  }
  // Can the RX view be written directly, or the RW view executed? (expected: no; only queried)
  MemoryInfo mi; u32 pi;
  svcQueryMemory(&mi, &pi, (u64)j.rx_addr); L("  rx view: type 0x%x perm %x attr %x size %lu KB", mi.type, mi.perm, mi.attr, mi.size >> 10);
  svcQueryMemory(&mi, &pi, (u64)j.rw_addr); L("  rw view: type 0x%x perm %x attr %x size %lu KB", mi.type, mi.perm, mi.attr, mi.size >> 10);
  Jit j2; r = jitCreate(&j2, 128 << 20); L("  second jitCreate 128 MB: rc %x", r); if (R_SUCCEEDED(r)) jitClose(&j2);
  jitClose(&j);
}

static void mapping(void) {
  size_t sz = 64 << 20; u8* heap = memalign(0x200000, sz);
  L("  64 MB heap block at %p", heap); if (!heap) return;
  memset(heap, 0, sz); heap[0x5000] = 0x77;
  // Mirror of heap pages in the stack region (svcMapMemory).
  virtmemLock(); u8* dst = virtmemFindStack(sz, 0x10000); Result r = dst ? svcMapMemory(dst, heap, sz) : -1; virtmemUnlock();
  L("  svcMapMemory 64 MB mirror at %p: rc %x", dst, r);
  if (R_SUCCEEDED(r)) { MemoryInfo mi; u32 pi; svcQueryMemory(&mi, &pi, (u64)heap); L("  source after map: perm %x attr %x (source is %s)", mi.perm, mi.attr, mi.perm ? "still accessible" : "inaccessible"); L("  mirror[0x5000] = %x", dst[0x5000]); svcUnmapMemory(dst, heap, sz); }
  // One page of the heap mapped at a chosen address.
  virtmemLock(); dst = virtmemFindStack(0x100000, 0x10000); r = dst ? svcMapMemory(dst + 0x3000, heap + 0x5000, 0x1000) : -1; virtmemUnlock();
  L("  single page map: rc %x%s", r, R_SUCCEEDED(r) && dst[0x3000] == 0x77 ? ", reads back" : "");
  if (R_SUCCEEDED(r)) svcUnmapMemory(dst + 0x3000, heap + 0x5000, 0x1000);
  // How big a window can be found?
  for (int gb = 1; gb <= 8; gb *= 2) { virtmemLock(); void* a = virtmemFindAslr((u64)gb << 30, 0); void* s = virtmemFindStack((u64)gb << 30, 0); virtmemUnlock(); L("  %d GB window: aslr %p, stack %p", gb, a, s); }
  // Page permissions on heap memory.
  L("  permissions..."); r = svcSetMemoryPermission(heap + 0x10000, 0x1000, Perm_R); L("  set heap page read-only: rc %x", r);
  if (R_SUCCEEDED(r)) { u64 t0 = armGetSystemTick(); for (int i = 0; i < 1000; i++) { svcSetMemoryPermission(heap + 0x10000, 0x1000, Perm_Rw); svcSetMemoryPermission(heap + 0x10000, 0x1000, Perm_R); } u64 t1 = armGetSystemTick(); L("  permission change: %.2f us each", us(t0, t1) / 2000); svcSetMemoryPermission(heap + 0x10000, 0x1000, Perm_Rw); }
  r = svcSetMemoryPermission(heap + 0x20000, 0x1000, Perm_None); L("  set heap page no-access: rc %x", r); if (R_SUCCEEDED(r)) svcSetMemoryPermission(heap + 0x20000, 0x1000, Perm_Rw);
  free(heap);
}

static void shared(void) {
  // Shared memory mapped twice.
  L("  calling svcCreateSharedMemory..."); Handle h; Result r = svcCreateSharedMemory(&h, 0x200000, Perm_Rw, Perm_Rw); L("  svcCreateSharedMemory 2 MB: rc %x", r);
  if (R_SUCCEEDED(r)) {
    L("  mapping..."); virtmemLock(); u8* a = virtmemFindAslr(0x200000, 0x10000); Result r1 = svcMapSharedMemory(h, a, 0x200000, Perm_Rw); VirtmemReservation* rv = R_SUCCEEDED(r1) ? virtmemAddReservation(a, 0x200000) : NULL;
    u8* b = virtmemFindAslr(0x200000, 0x10000); Result r2 = svcMapSharedMemory(h, b, 0x200000, Perm_Rw); virtmemUnlock();
    L("  shared map 1 at %p rc %x, map 2 at %p rc %x", a, r1, b, r2);
    if (R_SUCCEEDED(r1) && R_SUCCEEDED(r2)) { a[123] = 0x5a; L("  mirror %s", b[123] == 0x5a ? "works" : "does NOT mirror"); }
    if (R_SUCCEEDED(r2)) svcUnmapSharedMemory(h, b, 0x200000);
    if (R_SUCCEEDED(r1)) { svcUnmapSharedMemory(h, a, 0x200000); virtmemLock(); virtmemRemoveReservation(rv); virtmemUnlock(); }
    svcCloseHandle(h);
  }
}

static void gpu(void) {
  L("  eglGetDisplay..."); EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  L("  eglInitialize...");
  if (!eglInitialize(d, NULL, NULL)) { L("  eglInitialize failed"); }
  else {
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint ca[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE}; EGLConfig cfg; EGLint n = 0;
    eglChooseConfig(d, ca, &cfg, 1, &n); L("  configs %d; window surface...", n);
    EGLSurface s = eglCreateWindowSurface(d, cfg, nwindowGetDefault(), NULL); L("  surface %s; context...", s != EGL_NO_SURFACE ? "ok" : "FAILED");
    const EGLint xa[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE}; EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, xa);
    L("  context %s; make current...", c != EGL_NO_CONTEXT ? "ok" : "FAILED");
    if (c != EGL_NO_CONTEXT && eglMakeCurrent(d, s, s, c)) {
      L("  GL_VERSION  %s", glGetString(GL_VERSION)); L("  GL_RENDERER %s", glGetString(GL_RENDERER)); L("  GLSL        %s", glGetString(GL_SHADING_LANGUAGE_VERSION));
      GLint v = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v); L("  max texture %d", v);
      GLint ne = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &ne); char line[900] = "  ext:";
      for (int i = 0; i < ne; i++) { const char* e = (const char*)glGetStringi(GL_EXTENSIONS, i); if (strlen(line) + strlen(e) + 2 > 800) { L("%s", line); strcpy(line, "  ext:"); } strcat(line, " "); strcat(line, e); }
      L("%s", line);
      eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (c != EGL_NO_CONTEXT) eglDestroyContext(d, c);
    if (s != EGL_NO_SURFACE) eglDestroySurface(d, s);
    L("  eglTerminate..."); eglTerminate(d);
  }
  VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &ai; VkInstance inst;
  L("  vulkan..."); uint32_t iv = 0; vkEnumerateInstanceVersion(&iv); L("  Vulkan instance version %u.%u.%u", VK_VERSION_MAJOR(iv), VK_VERSION_MINOR(iv), VK_VERSION_PATCH(iv));
  VkResult vr = vkCreateInstance(&ici, NULL, &inst); L("  vkCreateInstance: %d", vr);
  if (vr == VK_SUCCESS) {
    uint32_t n = 4; VkPhysicalDevice pd[4]; vkEnumeratePhysicalDevices(inst, &n, pd); L("  physical devices: %u", n);
    if (n) {
      VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(pd[0], &pr);
      L("  device '%s', api %u.%u.%u, driver %x, maxImageDimension2D %u", pr.deviceName, VK_VERSION_MAJOR(pr.apiVersion), VK_VERSION_MINOR(pr.apiVersion), VK_VERSION_PATCH(pr.apiVersion), pr.driverVersion, pr.limits.maxImageDimension2D);
      VkPhysicalDeviceFeatures ft; vkGetPhysicalDeviceFeatures(pd[0], &ft);
      L("  features: dualSrcBlend %d, geometryShader %d, samplerAnisotropy %d, fragmentStoresAndAtomics %d, depthClamp %d, wideLines %d, largePoints %d, sampleRateShading %d, textureCompressionBC %d", ft.dualSrcBlend, ft.geometryShader, ft.samplerAnisotropy, ft.fragmentStoresAndAtomics, ft.depthClamp, ft.wideLines, ft.largePoints, ft.sampleRateShading, ft.textureCompressionBC);
      VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd[0], &mp); for (uint32_t i = 0; i < mp.memoryHeapCount; i++) L("  memory heap %u: %lu MB", i, (unsigned long)(mp.memoryHeaps[i].size >> 20));
      uint32_t ne = 0; vkEnumerateDeviceExtensionProperties(pd[0], NULL, &ne, NULL); VkExtensionProperties* ex = calloc(ne, sizeof *ex); vkEnumerateDeviceExtensionProperties(pd[0], NULL, &ne, ex);
      char line[900] = "  vk ext:"; for (uint32_t i = 0; i < ne; i++) { if (strlen(line) + strlen(ex[i].extensionName) + 2 > 800) { L("%s", line); strcpy(line, "  vk ext:"); } strcat(line, " "); strcat(line, ex[i].extensionName); } L("%s", line); free(ex);
    }
    vkDestroyInstance(inst, NULL);
  }
}

static void exceptions(void) {
  u8* heap = memalign(0x1000, 0x10000); memset(heap, 0, 0x10000);
  protPage = heap + 0x4000; Result r = svcSetMemoryPermission((void*)protPage, 0x1000, Perm_R); L("  protect page rc %x; writing to it now...", r);
  if (R_FAILED(r)) return;
  u64 t0 = armGetSystemTick(); ((volatile u8*)protPage)[5] = 0x99; u64 t1 = armGetSystemTick();
  L("  write resumed after %d fault(s), value %x, %.0f us", faults, protPage[5], us(t0, t1));
  // Registers must survive: run a loop with many live values across faults.
  double acc = 1.5; u64 k = 7; int bad = 0; t0 = armGetSystemTick();
  for (int i = 0; i < 200; i++) { svcSetMemoryPermission((void*)protPage, 0x1000, Perm_R); double before = acc * 1.25 + i; u64 kb = k * 31 + i; ((volatile u8*)protPage)[i] = (u8)i; acc = before; k = kb; if (protPage[i] != (u8)i) bad++; }
  t1 = armGetSystemTick(); L("  200 fault round trips: %.1f us each, %d faults total, %d bad, acc %.3f k %lu", us(t0, t1) / 200, faults, bad, acc, k);
  protPage = NULL; free(heap);
}

int main(int argc, char** argv) {
  mkdir("sdmc:/switch/sw2ps2", 0777); remove("sdmc:/switch/sw2ps2/probe.log");
  L("SW2PS2 probe 6");
  L("[memory]"); info();
  L("[jit]"); jit();
  L("[mapping]"); mapping();
  L("[gpu]"); gpu();
  L("[exceptions]"); exceptions();
  L("[shared memory]"); shared();
  L("[done]");
  return 0;
}
