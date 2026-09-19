#pragma once

// ============================================================================
// TIMING-only crash self-diagnosis.
//
// Why this exists: a crash inside the graphics driver leaves nothing in
// d3d12_log.txt. The DX12 runtime never installed a crash handler, so the only
// evidence is a WER minidump, and a raw stack scan of that dump cannot say
// which wrapper call was in flight - two identical access violations in
// nvwgf2umx.dll (0xC0000005, read at 0x000000A0, reached through D3D12Core)
// had to be attributed by hand, from outside the process.
//
// What this adds, in TIMING builds only (the source file is not even compiled
// into a normal Release binary):
//
//   1. A last-scope breadcrumb ring. DX12DiagProf::Scope / ScopePair - i.e.
//      every instrumented Api./Create./Stage. region the wrapper enters -
//      records one fixed-size entry (name pointer, thread, tick). After a crash
//      the ring names the wrapper call that led into the driver, which a stack
//      alone cannot do once the driver has overwritten the return addresses.
//   2. An unhandled-exception filter (DX12CrashReport.cpp) that appends a
//      self-contained report to the same d3d12_log.txt the user already reads:
//      exception record, faulting registers, stack walk symbolized against the
//      local PDB, every thread's RIP, the breadcrumb tail, and a minidump next
//      to the log.
//   3. The previously installed filter is still called afterwards, so a game's
//      own crash reporter keeps working exactly as before.
//
// Outside a TIMING build the breadcrumb is an empty inline (the compiler folds
// the call away) and the report filter does not exist.
// ============================================================================

#include <windows.h>

#include <atomic>

namespace DX12CrashBreadcrumbDetail
{
	// One entry per instrumented wrapper scope. Scope names are string literals
	// owned by this module, so an entry only needs the pointer.
	struct Entry
	{
		const char *scope = nullptr;
		DWORD threadId = 0;
		DWORD tick = 0;
	};

	// Holds a whole startup create sequence, and in a steady-state render loop
	// the last few frames - the tail is what identifies the call in flight.
	inline constexpr unsigned kCapacity = 512;

	// A reader may observe one torn entry (a scope name paired with the next
	// writer's thread id): the ring is a diagnostic tail, not a data structure
	// anything depends on, and a seqlock here would cost the render thread a
	// second atomic for a label that is already corroborated by the stack.
	inline Entry gEntries[kCapacity] = {};
	inline std::atomic<unsigned> gWriteIndex{ 0 };
}

#if defined(DX12_RELEASE_TIMING)

// Records that the wrapper is entering `scope`. Called from the profiling scope
// constructors, so it runs on the render thread: one relaxed RMW plus a
// fixed-size store, never a lock, never an allocation.
inline void DX12CrashBreadcrumb(const char *scope)
{
	if (!scope)
		return;
	using namespace DX12CrashBreadcrumbDetail;
	const unsigned index = gWriteIndex.fetch_add(1, std::memory_order_relaxed);
	Entry &entry = gEntries[index % kCapacity];
	entry.scope = scope;
	entry.threadId = GetCurrentThreadId();
	entry.tick = GetTickCount();
}

// Installs the crash report filter. Called once, as early as possible in
// DX12Runtime::Initialize.
void DX12CrashReportInstall();

#else

inline void DX12CrashBreadcrumb(const char *) {}
inline void DX12CrashReportInstall() {}

#endif // DX12_RELEASE_TIMING
