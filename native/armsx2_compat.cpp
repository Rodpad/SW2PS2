// SW2PS2: glue between the front end and the OunceSX2 (ARMSX2) core: SW2PS2's switches the core
// reads, diagnostics the front end shows, and Horizon helpers (exit clean-up, crash watch).
#include <stddef.h>
#include <stdint.h>
#ifdef SW2_VK
#include <array>
#include <atomic>
#include "GS/Renderers/Common/GSMeasurementOverrides.h"

extern std::atomic<bool> sw2_vk_measure;
extern std::atomic<u64> sw2_vk_counters[9];
extern bool sw2_vk_tfx_cache;
extern bool sw2_vk_canonical_pipelines;
extern bool sw2_vk_carry_colour_feedback;
extern std::atomic<bool> sw2_vk_trace_pass_ends;
extern std::array<std::atomic<u32>, 16384> sw2_vk_pass_end_sites;
extern std::array<std::atomic<u32>, 16> sw2_vk_target_break_reasons;
extern std::array<std::atomic<u32>, 64> sw2_vk_feedback_transitions;

extern "C" void sw2ax_vk_test_options(bool measure, unsigned kick_passes, bool cached_rings, bool tfx_cache, bool canonical_pipelines, bool carry_colour_feedback) {
  g_gs_measurement_overrides.readback_kick_passes = kick_passes;
  g_gs_measurement_overrides.stream_rings_cached = cached_rings;
  sw2_vk_tfx_cache = tfx_cache;
  sw2_vk_canonical_pipelines = canonical_pipelines;
  sw2_vk_carry_colour_feedback = carry_colour_feedback;
  sw2_vk_measure.store(measure, std::memory_order_relaxed);
}

extern "C" void sw2ax_vk_test_counters(uint64_t* out) {
  for (unsigned i = 0; i < 9; i++) out[i] = sw2_vk_counters[i].load(std::memory_order_relaxed);
}

extern "C" void sw2ax_vk_trace_pass_ends(bool enabled) {
  if (enabled) {
    for (auto& count : sw2_vk_pass_end_sites) count.store(0, std::memory_order_relaxed);
    for (auto& count : sw2_vk_target_break_reasons) count.store(0, std::memory_order_relaxed);
    for (auto& count : sw2_vk_feedback_transitions) count.store(0, std::memory_order_relaxed);
  }
  sw2_vk_trace_pass_ends.store(enabled, std::memory_order_relaxed);
}

extern "C" void sw2ax_vk_target_break_summary(unsigned* reasons, unsigned* transitions) {
  for (unsigned i = 0; i < sw2_vk_target_break_reasons.size(); i++)
    reasons[i] = sw2_vk_target_break_reasons[i].load(std::memory_order_relaxed);
  for (unsigned i = 0; i < sw2_vk_feedback_transitions.size(); i++)
    transitions[i] = sw2_vk_feedback_transitions[i].load(std::memory_order_relaxed);
}

extern "C" uint64_t sw2ax_vk_pass_end_summary(unsigned* lines, unsigned* counts, unsigned capacity) {
  uint64_t total = 0;
  for (unsigned line = 0; line < sw2_vk_pass_end_sites.size(); line++) {
    const unsigned count = sw2_vk_pass_end_sites[line].load(std::memory_order_relaxed);
    total += count;
    if (!count || !capacity || count <= counts[capacity - 1]) continue;
    unsigned slot = capacity - 1;
    while (slot && count > counts[slot - 1]) {
      lines[slot] = lines[slot - 1];
      counts[slot] = counts[slot - 1];
      slot--;
    }
    lines[slot] = line;
    counts[slot] = count;
  }
  return total;
}
#endif
extern "C" {
volatile unsigned sw2_prof_region = 0;
unsigned long long sw2_gs_stats[16];
extern bool sw2_gs_spin_wait;
extern uint32_t sw2_ee_wait_cycles;
unsigned long long sw2_int_bits[33];
unsigned sw2_prof_pc(int) { return 0; }
int memjit_live_regions(size_t* bytes) { if (bytes) *bytes = 0; return 0; }
void memjit_get_stats(unsigned long long* n, unsigned long long* b, unsigned long long* t) { *n = *b = *t = 0; }
#ifndef SW2_ARMSX2
int sw2_thread_info(int, unsigned long long*, int*, void**) { return 0; }
int sw2_thread_total(void) { return 0; }
unsigned sw2_thread_handle(int) { return 0; }
bool sw2_gs_spin_wait = false;
uint32_t sw2_ee_wait_cycles = 3072;
#endif
uint32_t sw2_jit_guest_pc(uint64_t, uint64_t* s) { if (s) *s = 0; return 0; }
void sw2_absmem_stats(unsigned long long* a, unsigned long long* b, unsigned long long* c, unsigned long long* d) { *a = *b = *c = *d = 0; }
}

// Symbols OunceSX2 provides from its own app (switch/app), which SW2PS2 does not build.
#ifdef SW2_ARMSX2
#include "common/HTTPDownloader.h"
namespace HorizonFastmem { bool HasResumableFaultHandler() { return true; } }  // lets PageFaultHandler::Install succeed; fastmem stays off (its system calls are not permitted) and RAM code pages are checked on entry instead
std::unique_ptr<HTTPDownloader> HTTPDownloader::Create(std::string) { return {}; }  // no downloads
extern "C" int flock(int, int) { return 0; }  // single process: nothing to lock against
#endif

#ifdef SW2_ARMSX2
#include <EGL/egl.h>
// The core's GL context code resolves EGL through this (no dynamic libraries on Horizon).
extern "C" void* sw2_egl_get_proc(const char* name) { return (void*)eglGetProcAddress(name); }
#endif

#ifdef SW2_ARMSX2
#include "pcsx2/PerformanceMetrics.h"
// For the front end's stats line: the core's own measurements (it runs on its own thread, so the
// front end's frame counters only see the frames handed to it).
extern "C" float sw2ax_fps() { return PerformanceMetrics::GetFPS(); }
extern "C" float sw2ax_internal_fps() { return PerformanceMetrics::GetInternalFPS(); }
#include "pcsx2/Config.h"
extern "C" int sw2ax_ee_cycle_rate() { return EmuConfig.Speedhacks.EECycleRate; }  // -3 50% .. 0 100%: what the core runs now
extern "C" float sw2ax_speed() { return PerformanceMetrics::GetSpeed(); }  // % of full speed (50 or 60 Hz)
extern "C" float sw2ax_ee_usage() { return PerformanceMetrics::GetCPUThreadUsage(); }
extern "C" float sw2ax_vu_usage() { return PerformanceMetrics::GetVUThreadUsage(); }
extern "C" float sw2ax_gs_usage() { return PerformanceMetrics::GetGSThreadUsage(); }
#endif

#ifdef SW2_ARMSX2
#include "HorizonThreadWrap.h"
#include "common/Horizon/Horizon.h"
#include "pcsx2/Memory.h"
#include <switch.h>
// On hbmenu the next program runs in this same process: a core thread still running at exit
// (GS, VU, disc reader...) would go on executing freed code and crash whatever runs next.
// As OunceSX2's own app does (switch/app/main.cpp), they are paused and their stacks and the
// JIT mappings released, last thing at exit; the main thread's cores are put back too.
extern "C" void (*__stdio_exit_handler)(void);
static u64 s_sw2_initial_mask = 0; static s32 s_sw2_initial_core = -1;
extern "C" void sw2ax_crash_watch(int on);
extern "C" void sw2ax_register_exit_cleanup(int traces)
{
	svcGetThreadCoreMask(&s_sw2_initial_core, &s_sw2_initial_mask, CUR_THREAD_HANDLE);
	// exit trace: sdmc:/switch/sw2ps2/exit.txt, one launch's worth (the previous launch's kept as exit.prev.txt;
	// it used to grow forever)
	// (traces only with logging on -- debug logging or a test run: normal play writes no files)
	if (traces)
	{
		remove("sdmc:/switch/sw2ps2/exit.prev.txt");
		rename("sdmc:/switch/sw2ps2/exit.txt", "sdmc:/switch/sw2ps2/exit.prev.txt");
		Horizon::BreadcrumbInit("sdmc:/switch/sw2ps2");
	}
	Horizon::ExitTrace("---- SW2PS2 ARMSX2 started");
	std::atexit([]() {
		Horizon::ExitTrace("atexit clean-up: begin");
		sw2ax_crash_watch(0);  // its thread must not outlive the program (hbloader runs the next one in this process)
		HorizonThreadWrap::ReleaseLiveThreads("at exit", true);
		SysMemory::ReleaseAll(); Horizon::ExitTrace("memory map released at exit");  // kept across games (Memory.cpp)
		char line[96];
		snprintf(line, sizeof line, "code mappings released at exit: %zu", Horizon::ReleaseLeftoverCodeMappings());
		Horizon::ExitTrace(line);
		if (s_sw2_initial_mask) svcSetThreadCoreMask(CUR_THREAD_HANDLE, s_sw2_initial_core, s_sw2_initial_mask);
		// newlib's own stdio exit handler walks every FILE taking its lock; a paused thread can
		// hold one (the disc reader reads its image through stdio). OunceSX2 swaps it the same way.
		__stdio_exit_handler = []() { Horizon::ExitTrace("stdio exit: flush"); std::fflush(nullptr); Horizon::ExitTrace("stdio exit: done"); };
		Horizon::ExitTrace("atexit clean-up: done");
	});
}
extern "C" void sw2ax_exit_trace(const char* m) { Horizon::ExitTrace(m); }
#include "pcsx2/VMManager.h"
// Whether the game itself is running (the BIOS has handed over to its ELF). A state loaded before
// that switches the core from "BIOS" to the game mid-load, and a game whose database settings need
// the GS device rebuilt (God of War II) crashed there (2026-11-19).
extern "C" int sw2ax_game_booted() { return VMManager::HasValidVM() && VMManager::Internal::HasBootedELF() ? 1 : 0; }
extern "C" void sw2ax_list_threads(const char* when) { HorizonThreadWrap::ReleaseLiveThreads(when, false);
}
#endif

#ifdef SW2_ARMSX2
// The profiler samples the EE (CPU) thread: its kernel handle is taken on that thread itself.
static volatile unsigned s_sw2_ee_handle = 0;
extern "C" void sw2_profiler_reset_handle() { s_sw2_ee_handle = 0; }
// After an unload: close the core's disc again (a disc reader thread was found still running
// at exit), then list what threads remain.
extern void DoCDVDclose();
extern "C" void sw2ax_after_unload()
{
	HorizonThreadWrap::ReleaseLiveThreads("after unload, before the disc close", false);
	DoCDVDclose();
	HorizonThreadWrap::ReleaseLiveThreads("after unload and the disc close", false);
	s_sw2_ee_handle = 0;
}
#endif

#ifdef SW2_ARMSX2
#include "pcsx2/Host.h"
#include "pcsx2/MTGS.h"
// sw2_prof_gs: sample the GS thread instead (set per test run by the front end).
extern "C" { int sw2_prof_gs = 0; }
extern "C" int sw2_thread_total(void)
{
	if (!s_sw2_ee_handle)
	{
		if (sw2_prof_gs)
			Host::RunOnGSThread([]() { s_sw2_ee_handle = threadGetCurHandle(); });
		else
			Host::RunOnCPUThread([]() { s_sw2_ee_handle = threadGetCurHandle(); }, false);
	}
	return s_sw2_ee_handle ? 1 : 0;
}
extern "C" int sw2_thread_info(int i, unsigned long long* ticks, int* core, void** entry)
{
	if (i != 0 || !s_sw2_ee_handle) return 0;
	u64 t = 0; svcGetInfo(&t, InfoType_ThreadTickCount, s_sw2_ee_handle, ~0ULL);
	*ticks = t ? t : 1; *core = 0; *entry = nullptr; return 1;
}
extern "C" unsigned sw2_thread_handle(int i) { return i == 0 ? s_sw2_ee_handle : 0; }
#endif

#ifdef SW2_ARMSX2
// Profiler: sampled JIT PCs to EE block start PCs (or 0xF000000r for region r: 2 IOP, 3 VU0,
// 4 VU1, 5 VIF unpack, 0 unknown), worked out on the CPU thread so the block table is stable.
#include "GS/Renderers/OpenGL/GLLibretro.h"
#include "common/Console.h"
#include <memory>
#include <atomic>
#include <vector>
#include <algorithm>
extern "C" u32 sw2ax_classify_host_pc(u64 pc, u32* ee_block);
extern "C" u32 sw2ax_copy_ee_block(u32 guest_pc, u32* guest, u32 guest_capacity, u32* host, u32 host_capacity, u32* guest_words);
extern "C" u32 sw2ax_map_vu0_block(u64 pc);
extern "C" void sw2ax_describe_vu0_block(u64 pc, char* out, size_t size);
extern "C" u32 sw2ax_copy_vu0_host_block(u64 pc, u32* out, u32 capacity);
extern "C" bool sw2ax_vu0_sample(u64 pc, u32* offset, u32* instruction)
{
	const u64 begin = reinterpret_cast<u64>(SysMemory::GetVU0Rec());
	const u64 end = reinterpret_cast<u64>(SysMemory::GetVU0RecEnd());
	if (pc < begin || pc >= end || (pc & 3))
		return false;
	*offset = static_cast<u32>(pc - begin);
	*instruction = *reinterpret_cast<const u32*>(pc);
	return true;
}
extern "C" void sw2ax_classify_batch(const unsigned long long* pcs, const u32* counts, unsigned* out, size_t n)
{
	// On the CPU thread so the block table holds still; given up after 3 s (it once never came
	// round to it), leaving the regions unclassified. The copies outlive a give-up.
	struct Job { std::vector<unsigned long long> pcs; std::vector<unsigned> out; std::vector<u32> counts; std::atomic<bool> done{false}; };
	auto job = std::make_shared<Job>();
	job->pcs.assign(pcs, pcs + n); job->out.assign(n, 0xF0000000u);
	job->counts.assign(counts, counts + n);
	GLLibretro::SetPacing(false);
	Host::RunOnCPUThread([job]() {
		for (size_t i = 0; i < job->pcs.size(); i++)
		{
			u32 blk = 0;
			const u32 region = sw2ax_classify_host_pc(job->pcs[i], &blk);
			if (region == 3)
			{
				const u32 mapped = sw2ax_map_vu0_block(job->pcs[i]);
				job->out[i] = mapped ? mapped : 0xF0000003u;
			}
			else
				job->out[i] = region == 1 ? blk : (0xF0000000u | region);
		}
		std::vector<std::pair<u32, size_t>> hot_blocks;
		for (size_t i = 0; i < job->out.size(); i++)
		{
			if ((job->out[i] & 0xF0000000u) != 0xE0000000u)
				continue;
			auto it = std::find_if(hot_blocks.begin(), hot_blocks.end(), [&](const auto& item) {
				return job->out[item.second] == job->out[i];
			});
			if (it == hot_blocks.end())
				hot_blocks.emplace_back(job->counts[i], i);
			else
				it->first += job->counts[i];
		}
		std::sort(hot_blocks.begin(), hot_blocks.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
		for (size_t i = 0; i < hot_blocks.size() && i < 8; i++)
		{
			char description[700]{};
			sw2ax_describe_vu0_block(job->pcs[hot_blocks[i].second], description, sizeof(description));
			Console.WriteLn("SW2 VU0 block samples %u: %s", hot_blocks[i].first, description);
			if (i < 2)
			{
				u32 words[1024]{};
				const u32 count = sw2ax_copy_vu0_host_block(job->pcs[hot_blocks[i].second], words, 1024);
				for (u32 start = 0; start < count; start += 16)
				{
					char line[192]{};
					int pos = std::snprintf(line, sizeof(line), "SW2 VU0 host %zu %04x:", i, start * 4);
					for (u32 j = start; j < count && j < start + 16 && pos > 0 && static_cast<size_t>(pos) < sizeof(line); j++)
						pos += std::snprintf(line + pos, sizeof(line) - pos, " %08x", words[j]);
					Console.WriteLn("%s", line);
				}
			}
		}
		std::vector<std::pair<u32, u32>> hot_ee_blocks;
		for (size_t i = 0; i < job->out.size(); i++)
		{
			const u32 block = job->out[i];
			if (!block || block >= 0xE0000000u)
				continue;
			auto it = std::find_if(hot_ee_blocks.begin(), hot_ee_blocks.end(), [&](const auto& item) {
				return item.second == block;
			});
			if (it == hot_ee_blocks.end())
				hot_ee_blocks.emplace_back(job->counts[i], block);
			else
				it->first += job->counts[i];
		}
		std::sort(hot_ee_blocks.begin(), hot_ee_blocks.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
		u64 scanned_samples = 0, paired_samples = 0;
		u32 scanned_blocks = 0, paired_blocks = 0;
		for (const auto& entry : hot_ee_blocks)
		{
			u32 guest[256]{}, guest_count = 0, unused_host = 0;
			sw2ax_copy_ee_block(entry.second, guest, 256, &unused_host, 0, &guest_count);
			if (!guest_count)
				continue;
			scanned_samples += entry.first;
			scanned_blocks++;
			bool found = false;
			for (u32 i = 0; i < guest_count && !found; i++)
			{
				const u32 load = guest[i];
				const u32 load_op = load >> 26;
				if (load_op != 32 && load_op != 33 && load_op != 35 && load_op != 36 && load_op != 37 && load_op != 39)
					continue;
				for (u32 j = i + 1; j < guest_count; j++)
				{
					const u32 store = guest[j];
					const u32 store_op = store >> 26;
					if ((store_op == 40 || store_op == 41 || store_op == 43) &&
						(load & 0x03e0ffffu) == (store & 0x03e0ffffu))
					{
						found = true;
						break;
					}
				}
			}
			if (found)
			{
				paired_samples += entry.first;
				paired_blocks++;
			}
		}
		Console.WriteLn("SW2 EE syntactic load-store same-address blocks %u/%u, samples %llu/%llu",
			paired_blocks, scanned_blocks, static_cast<unsigned long long>(paired_samples),
			static_cast<unsigned long long>(scanned_samples));
		for (size_t i = 0; i < hot_ee_blocks.size() && i < 3; i++)
		{
			u32 guest[256]{}, host[2048]{}, guest_count = 0;
			const u32 host_count = sw2ax_copy_ee_block(hot_ee_blocks[i].second, guest, 256, host, 2048, &guest_count);
			Console.WriteLn("SW2 EE block %zu guest %08x samples %u guest_words %u host_words %u",
				i, hot_ee_blocks[i].second, hot_ee_blocks[i].first, guest_count, host_count);
			for (int kind = 0; kind < 2; kind++)
			{
				const u32* words = kind ? host : guest;
				const u32 count = kind ? host_count : guest_count;
				for (u32 start = 0; start < count; start += 16)
				{
					char line[192]{};
					int pos = std::snprintf(line, sizeof(line), "SW2 EE %s %zu %04x:", kind ? "host" : "guest", i, start * 4);
					for (u32 j = start; j < count && j < start + 16 && pos > 0 && static_cast<size_t>(pos) < sizeof(line); j++)
						pos += std::snprintf(line + pos, sizeof(line) - pos, " %08x", words[j]);
					Console.WriteLn("%s", line);
				}
			}
		}
		job->done.store(true);
	}, false);
	for (int i = 0; i < 300 && !job->done.load(); i++) svcSleepThread(10000000ULL);
	if (job->done.load()) std::copy(job->out.begin(), job->out.end(), out);
	else std::fill(out, out + n, 0xF0000009u);  // (not classified)
	GLLibretro::SetPacing(true);
}
#endif

#ifdef SW2_ARMSX2
int sw2_gs_back = 0;
int sw2_audio_stretch = 1;  // SPU2 time-stretching (PCSX2 default on)
char sw2_bios_dir[512] = "";  // the BIOS folder ("/bios/ps2"), set by the front end before each game
int sw2_vuaot = 0;  // the core's VU background compiler (vu-aot live tier): off -- a second game in the same session crashed after a state load with it on (2026-11-19), and it gained nothing measurable
int sw2_iop_lag = 98304;  // IOP thread lag bound in EE cycles: one frame let replies land late at boot, 24576 stalled FFX
int sw2_db_effects = 1;
int sw2_blending_explicit = 0;
int sw2_shader_log = 0;
int sw2_no_pacing = 1;  // frame hand-off pacing off: same smoothness measured, no frame held to the next tick
int sw2_iop_thread = 1;  // on: upstream ARMSX2 IOP thread, lag 98304, no WFE waits on the Switch; read by the core's config set-up (pcsx2-libretro/Main.cpp) at each game load
#endif

#ifdef SW2_ARMSX2
extern "C" void sw2ax_frame_counts(unsigned long long* shown, unsigned long long* repeats, unsigned long long* dropped)
{
	u64 a, b, c; GLLibretro::SW2FrameCounts(&a, &b, &c); *shown = a; *repeats = b; *dropped = c;
}
#endif

#ifdef SW2_ARMSX2
// Crash watch (tests): every ~50 ms each emulator thread is paused for a moment and its PC and
// LR read; the newest samples are rewritten to sdmc:/switch/sw2ps2/crashwatch.txt each ~200 ms,
// so after a crash that kills the app the file says where every thread was. Image addresses are
// given as offsets from the module base (0x8000000 at run time, as the ELF is linked at 0).
#include <malloc.h>
#include "pcsx2/R5900.h"
#include "pcsx2/R3000A.h"
static Thread s_cw_thread; static volatile bool s_cw_on = false; static Handle s_cw_main = 0; static int s_cw_run = 0;
static void CrashWatchMain(void*)
{
	struct Sample { unsigned h; u64 pc, lr; };
	std::vector<Sample> last;
	int tick = 0;
	while (s_cw_on)
	{
		unsigned hs[48]; size_t n = HorizonThreadWrap::GetLiveThreadHandles(hs, 46);
		if (s_cw_main) hs[n++] = s_cw_main;
		std::vector<Sample> now;
		for (size_t i = 0; i < n; i++)
		{
			if (R_FAILED(svcSetThreadActivity(hs[i], ThreadActivity_Paused))) continue;
			ThreadContext tc; const Result r = svcGetThreadContext3(&tc, hs[i]);
			svcSetThreadActivity(hs[i], ThreadActivity_Runnable);
			if (R_SUCCEEDED(r)) now.push_back({hs[i], tc.pc.x, tc.lr});
		}
		last.swap(now);
		if (++tick % 4 == 0)
		{
			char path[96]; snprintf(path, sizeof path, "sdmc:/switch/sw2ps2/crashwatch-%d.txt", s_cw_run);
			FILE* f = fopen(path, "w");
			if (f)
			{
				u64 used = 0, total = 0;
				svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
				svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
				struct mallinfo mi = mallinfo();
				fprintf(f, "tick %d, main %x, process memory %llu / %llu MB, malloc in use %d MB, malloc free %d MB\n", tick, s_cw_main,
					(unsigned long long)(used >> 20), (unsigned long long)(total >> 20), mi.uordblks >> 20, mi.fordblks >> 20);
				for (const Sample& smp : last)
					fprintf(f, "thread %x pc %llx lr %llx\n", smp.h, (unsigned long long)smp.pc, (unsigned long long)smp.lr);
				// The emulated CPUs (racy reads, for a hint only): where the game's own code was.
				fprintf(f, "ee pc %08x cycle %u ra %08x | iop pc %08x cycle %u\n", cpuRegs.pc, cpuRegs.cycle,
					cpuRegs.GPR.n.ra.UL[0], psxRegs.pc, psxRegs.cycle);
				fclose(f);
			}
		}
		svcSleepThread(50000000ULL);
	}
}
extern "C" void sw2ax_crash_watch(int on)
{
	if (on && !s_cw_on)
	{
		s_cw_run = on;
		s_cw_main = threadGetCurHandle();
		s_cw_on = true;
		if (R_FAILED(threadCreate(&s_cw_thread, CrashWatchMain, nullptr, nullptr, 0x10000, 0x1F, 5)) || R_FAILED(threadStart(&s_cw_thread)))
			s_cw_on = false;
	}
	else if (!on && s_cw_on)
	{
		s_cw_on = false; threadWaitForExit(&s_cw_thread); threadClose(&s_cw_thread);
	}
}
#endif

#ifdef SW2_ARMSX2
extern "C" void sw2ax_memory(unsigned* used_mb, unsigned* total_mb, unsigned* heap_free_mb)
{
	u64 used = 0, total = 0;
	svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
	svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
	struct mallinfo mi = mallinfo();
	*used_mb = (unsigned)(used >> 20); *total_mb = (unsigned)(total >> 20); *heap_free_mb = (unsigned)(mi.fordblks >> 20);
}
#endif

#ifdef SW2_ARMSX2
#include "pcsx2/IopThread/IopThreadVm.h"
// Per-second view of the IOP thread's statistics (deltas), for the stats line.
extern "C" void sw2ax_iop_stats(char* out, size_t n)
{
	out[0] = 0;
	if (!IopThread::g_thread.IsRunning()) return;
	static IopThread::Stats prev{};
	const IopThread::Stats s = IopThread::g_thread.GetStats();
	auto d = [](u64 a, u64 b) { return (unsigned long long)(a >= b ? a - b : a); };
	snprintf(out, n, " iop: replies %llu late %llu idle-late %llu (%llu kcyc, max %llu kcyc) | EE lag waits %llu (%llu ms) holds %llu (%llu ms) | IOP idle %llu ms parks %llu wakes %llu",
		d(s.replies, prev.replies), d(s.late_replies, prev.late_replies), d(s.idle_overshoots, prev.idle_overshoots),
		d(s.idle_overshoot_cycles, prev.idle_overshoot_cycles) / 1000, (unsigned long long)s.idle_overshoot_max / 1000,
		d(s.lag_spins, prev.lag_spins), d(s.lag_stall_ns, prev.lag_stall_ns) / 1000000,
		d(s.emu_hold_requests, prev.emu_hold_requests), d(s.emu_hold_stall_ns, prev.emu_hold_stall_ns) / 1000000,
		d(s.idle_ns, prev.idle_ns) / 1000000, d(s.parks, prev.parks), d(s.wakes_sent, prev.wakes_sent));
	prev = s;
}
#endif
int sw2_osd_stats = 0;  // the core's performance overlay level (Vulkan display; front-end stats level)
#ifdef SW2_ARMSX2
extern u64 g_sw2EvStats[4];
// Event tests since the last call: total, with a counter due, with an interrupt pending, with neither.
extern "C" void sw2ax_event_stats(unsigned long long out[4])
{
	static u64 prev[4];
	for (int i = 0; i < 4; i++) { out[i] = g_sw2EvStats[i] - prev[i]; prev[i] = g_sw2EvStats[i]; }
}
#endif
#ifdef SW2_ARMSX2
#include <algorithm>
#include <cstring>
// Event tests with nothing due, by the code that scheduled them (return addresses, relative to
// cpuSetNextEvent so they resolve against the ELF), and interrupt bits pending at tests (totals).
struct Sw2EvSrc { uptr addr; u64 n; };
extern Sw2EvSrc g_sw2EvSrc[64];
extern u64 g_sw2EvIntBits[32];
extern void cpuSetNextEvent(u64, s32);
extern "C" void sw2ax_event_sources(char* out, size_t size)
{
	int n = snprintf(out, size, "event setters (rel. cpuSetNextEvent):");
	Sw2EvSrc v[64]; memcpy(v, g_sw2EvSrc, sizeof v);
	std::sort(v, v + 64, [](const Sw2EvSrc& a, const Sw2EvSrc& b) { return a.n > b.n; });
	const uptr anchor = reinterpret_cast<uptr>(&cpuSetNextEvent);
	for (int i = 0; i < 12 && v[i].addr && n < (int)size - 40; i++)
		n += snprintf(out + n, size - n, " %+lld:%llu", (long long)(v[i].addr - anchor), (unsigned long long)v[i].n);
	n += snprintf(out + n, size - n, " | interrupt bits:");
	for (int b = 0; b < 32 && n < (int)size - 24; b++)
		if (g_sw2EvIntBits[b]) n += snprintf(out + n, size - n, " %d:%llu", b, (unsigned long long)g_sw2EvIntBits[b]);
}
#endif
