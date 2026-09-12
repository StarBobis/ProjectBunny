#pragma once

// ============================================================================
// DX12ProfilingText - the writer thread's report formatter.
//
// Turns one aggregation window into the ASCII block the on-screen HUD draws.
// It runs only on the writer thread, never inside a frame, and touches nothing
// but the plain data handed to it - so it has no dependency on DX12Diag.h and
// the profiler header can include it without a cycle.
//
// The text is pure ASCII because the overlay's glyph atlas covers 32..126
// only; producing wide characters here would just force a conversion on the
// render thread.
//
// Views:
//   Snapshot - the default. One screenshot has to be enough to see what is
//              slow, so it is laid out as three columns side by side: the
//              time table on the left, the hit-rate counters in the middle,
//              and the spike/frame block on the right. Rows are tagged with a
//              column marker byte (see DX12ProfilingLayout::kColumnMark) that
//              the overlay turns into an x offset, which is what keeps the
//              layout resolution independent.
//   Compact/Summary/Top/Sections/Counters - the earlier single-column views,
//              kept because they are still handy for one specific question and
//              because the mode numbers keep working across the switch.
//
// Unit rule, applied everywhere a duration is printed:
//   < 1000us -> "123 us" (integer),  >= 1000us -> "1.23 ms".
// Frame wall times are always milliseconds.
// ============================================================================

#include "DX12ProfilingHud.h"

#include <algorithm>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// One aggregation window's derived numbers, computed once per summary and
// shared by both sinks - the log's frame-timeline line and the on-screen
// report - so the two can never disagree, and so counters with reset side
// effects (the per-frame draw/dispatch shards) are consumed exactly once.
struct DX12ProfilingWindow
{
	// Identify the frame the window closed on.
	const char *api = nullptr;
	void *swapChain = nullptr;
	LONG hr = 0;

	LONG frames = 0;
	LONG present = 0;
	LONG frameDraws = 0;
	LONG frameDispatches = 0;
	LONG droppedFrameSummaries = 0;
	// The hook work the frames in this window actually did. Mirrored from
	// DX12HotPathState::HookWorkFlags; display only.
	UINT64 hookWorkFlags = 0;

	LONG64 frameIntervalUs = 0;
	LONG64 frameWallUs = 0;
	LONG64 outsidePresentUs = 0;
	LONG64 presentWallUs = 0;
	LONG64 maxFrameWallUs = 0;
	LONG64 maxPresentWallUs = 0;

	LONG64 presentKnownUs = 0;
	LONG64 presentOriginalUs = 0;
	LONG64 presentPostOriginalRuntimeUs = 0;
	LONG64 presentModBeginFrameUs = 0;
	LONG64 presentProfilingFlushUs = 0;
	LONG64 presentConsumeUiActionsUs = 0;
	LONG64 presentFrameCountersUs = 0;
	LONG64 presentCaptureBookkeepingUs = 0;
	LONG64 presentCaptureCompleteUs = 0;
	LONG64 presentHotPathUpdateUs = 0;
	LONG64 presentOverlayTextUs = 0;
	LONG64 presentOtherUs = 0;
	LONG64 presentWallMinusKnownUs = 0;
	LONG64 presentEnterCalls = 0;
	LONG64 presentLeaveCalls = 0;

	LONG64 queueExecuteStageSamples = 0;
	LONG64 queueWallCalls = 0;
	LONG64 queueWallCommandLists = 0;
	LONG64 queueEstimatedTotalUs = 0;
	LONG64 queueWrapperTotalUs = 0;
	LONG64 queueFlowTotalUs = 0;
	LONG64 queueOriginalUs = 0;
	LONG64 queueNotifyUs = 0;
	LONG64 queueRegisterUs = 0;
	LONG64 queueUnwrapUs = 0;
	LONG64 queueRebuildWorkFlagsUs = 0;
	LONG64 queueFenceCalls = 0;
	LONG64 queueSignalUs = 0;
	LONG64 queueWaitUs = 0;

	LONG64 fenceCalls = 0;
	LONG64 fenceTotalUs = 0;
	LONG64 eventWaitCalls = 0;
	LONG64 eventWaitTotalUs = 0;
	LONG64 systemWaitCalls = 0;
	LONG64 systemWaitTotalUs = 0;

	LONG64 workerTaskCalls = 0;
	LONG64 workerTaskUs = 0;
	LONG64 workerWaitCalls = 0;
	LONG64 workerWaitUs = 0;
	LONG64 workerIdleWaitUs = 0;
	LONG64 workerDrainWaitUs = 0;

	// The two totals the header publishes, both over the same rows the left
	// table ranks (no sub-keyed CPU-stage rows, no zero-time rows, the game's
	// own Present/Wait excluded):
	//
	//   ourAllUs  - every row's full time. This is the historical "SUM ours"
	//               and it stays on screen as the upper bound, because a
	//               nested scope is counted once by its own row and again by
	//               every row enclosing it.
	//   ourRootUs - only the samples taken at the top of the instrumented call
	//               tree (Bucket::rootUs), so each interval is counted exactly
	//               once. This is OUR TOTAL.
	//
	// Blocked time is in ourAllUs (those rows are on screen) but never in
	// ourRootUs: a worker thread sleeping for a task and a wait on a fence cost
	// wall time without costing CPU, and adding them to a total that answers
	// "how much of the frame did our hooks eat" would drown the answer.
	LONG64 ourRootUs = 0;
	LONG64 ourAllUs = 0;
	LONG ourRootRows = 0;
	LONG ourAllRows = 0;

	// Mean frame rate over the window. Zero when the window is empty.
	double Fps() const
	{
		return frameIntervalUs > 0
			? static_cast<double>(frames) * 1000000.0 /
				static_cast<double>(frameIntervalUs)
			: 0.0;
	}

	// Mean wall time from one frame start to the next, in milliseconds.
	double FrameMs() const
	{
		return frames > 0
			? static_cast<double>(frameIntervalUs) /
				static_cast<double>(frames) / 1000.0
			: 0.0;
	}

	// How long the window lasted, in milliseconds: the summed present-to-present
	// interval of the frames it holds.
	double WindowMs() const
	{
		return static_cast<double>(frameIntervalUs) / 1000.0;
	}

	// Frames the per-frame divisions use: an empty window must not divide by
	// zero, and every "per frame" number in the report goes through this.
	LONG FrameDivisor() const
	{
		return frames > 0 ? frames : 1;
	}
};

// A flattened bucket. The profiler's own Bucket carries a sub-key and a kind
// that the report never shows, so the formatter takes this instead and stays
// independent of the profiler's layout.
struct DX12ProfilingRow
{
	const char *name = nullptr;
	LONG64 calls = 0;
	LONG64 totalUs = 0;
	LONG64 maxUs = 0;
	LONG64 items = 0;
	// Time accumulated at the top of the instrumented call tree. Zero means
	// every sample of this row was taken inside another instrumented scope, so
	// it is already part of its parent's total - the HUD marks those rows.
	LONG64 rootUs = 0;
};

// One non-zero failure counter, while the middle column ranks them. A
// file-scope type because the project bans types declared inside functions.
struct DX12ProfilingFailureRow
{
	const char *label = nullptr;
	LONG64 calls = 0;
};

// Which buckets the report treats as ours. Defined once, here, because both
// sinks use it: the report formatter to decide which rows the table lists, the
// profiler's window builder to sum the two totals - and a second copy of these
// rules is exactly how an on-screen number and a logged one start disagreeing.
struct DX12ProfilingClassify
{
	// Work that is the game's or the OS's, seen from inside our hooks: the
	// game's own Present, the waits it performs, and the game's real
	// ExecuteCommandLists. It happens with or without this DLL loaded, so it is
	// kept out of the time table, out of the spike ranking and out of both
	// totals. It stays visible in the frame block of the right column.
	static bool ObservedOnly(const char *name)
	{
		if (!name)
			return true;
		if (!strcmp(name, "Present.original_present") ||
			!strcmp(name, "Present.original_present_skip") ||
			!strcmp(name, "Wait.system") ||
			!strcmp(name, "Wait.event"))
			return true;
		// The game's real ExecuteCommandLists, executed from inside our hook.
		if (!strncmp(name, "Queue.original", 14))
			return true;
		// Driver work timed from inside one of our wrappers: the paired
		// "<api>.Original" scope of every Create.*Wrapper is the call that goes
		// into the driver (CopyDescriptors, CreateHeap, CreateCommittedResource),
		// and WrapperLifetime.DriverRelease is the Release cascade a wrapper's
		// destructor triggers. Both are real time on the frame, but they are not
		// ours to optimise - and reading them as ours is what sent this session
		// after a global lock that turned out not to be the cost.
		const size_t length = strlen(name);
		static const char kOriginalSuffix[] = ".Original";
		constexpr size_t kOriginalLength = sizeof(kOriginalSuffix) - 1;
		if (length > kOriginalLength &&
			!strcmp(name + length - kOriginalLength, kOriginalSuffix))
			return true;
		return strstr(name, "WrapperLifetime.DriverRelease") != nullptr;
	}

	// Time a thread spent blocked rather than working: our own worker threads
	// idling for a task, and the wait buckets. Real wall time, no CPU - and on
	// a session where the collect queue has nothing to do, Worker.wait.idle
	// alone would cover a whole frame and make OUR TOTAL read ~100% no matter
	// what our code costs.
	static bool WaitOnly(const char *name)
	{
		if (!name)
			return false;
		return !strncmp(name, "Wait.", 5) || !strncmp(name, "Worker.wait", 11);
	}

	// A row that may contribute to OUR TOTAL.
	static bool OurTotalEligible(const char *name)
	{
		return !ObservedOnly(name) && !WaitOnly(name);
	}
};

// Every counter the report names, in the order the fields are defined. The
// table below is the single place a bucket name is written down, so a rename
// in the profiler shows up as a field that never matches rather than as a
// wrong number.
enum class DX12ProfilingField : int
{
	// IA negative cache.
	IaNegCacheHit = 0,
	IaNegVerdictRuns,
	IaNegVerdictNegative,
	IaNegVerdictGated,
	// Pre-skin verdict layers.
	PreSkinVerdictDispatchHits,
	PreSkinVerdictEarlyHits,
	PreSkinVerdictDescriptorHits,
	PreSkinVerdictResidentHits,
	PreSkinVerdictTemplateHits,
	PreSkinVerdictTemplateFingerprintHits,
	PreSkinVerdictGeometryHits,
	PreSkinVerdictKeyMatches,
	PreSkinVerdictBindingStateInvalid,
	PreSkinVerdictIdentityProbeRuns,
	PreSkinVerdictIdentityProbeHits,
	PreSkinPlanCacheLookups,
	PreSkinPlanCacheNegativeHits,
	// Pre-skin binding store.
	PreSkinBindingCacheCalls,
	PreSkinBindingCacheHits,
	PreSkinBindingCacheMisses,
	PreSkinBindingCacheStores,
	PreSkinStoreCommitted,
	PreSkinStoreStillInvalidAfterCollect,
	PreSkinValidatePass,
	// Descriptor copy.
	DescCopyChanged,
	DescCopyUnchanged,
	DescCopyDirectPath,
	DescCopyLockedPath,
	DescriptorHeapChanged,
	DescResolveFailure,
	DescHeapCacheHit,
	DescCopySlots,
	DescCopyBlocks,
	// Draw/dispatch fast paths.
	FastForward,
	DrawNoopFastPathAttempts,
	DrawNoopFastPathForwarded,
	DrawNoopFastPathNonNoopDecision,
	IaBindFastPath,
	IaBindSlowPath,
	IaPreSkinRedirectGated,
	PreSkinRedirectNegMiss,
	PreSkinRedirectNegHit,
	PreSkinRedirectViewNegHit,
	// Frame block (right column). Present.original_present and the wait
	// buckets are the game's own cost, observed rather than spent by us; the
	// queue buckets are the ExecuteCommandLists wrapper.
	PresentOriginalPresent,
	PresentOverlayText,
	PresentProfilingFlush,
	WaitSystem,
	WaitEvent,
	FenceOp,
	QueueWrapperTotal,
	QueueWait,
	QueueSignal,
	// Pre-skin failure counters. Listed only while non-zero: a healthy session
	// would otherwise spend a dozen rows saying nothing.
	PreSkinStoreNoSnapshot,
	PreSkinStoreNotStable,
	PreSkinStoreRuntimeDescriptorsUnstable,
	PreSkinStoreObservedSlotsInvalid,
	PreSkinValidateNoBindings,
	PreSkinValidateHeap,
	PreSkinValidateRootSignature,
	PreSkinValidateRootDescriptor,
	PreSkinValidateConstants,
	PreSkinValidateSlotCountMismatch,
	PreSkinValidateNoObservedHeap,
	PreSkinValidateTableUnresolved,
	PreSkinValidateSlotOutOfRange,
	PreSkinValidateHeapRecordUnresolved,
	PreSkinValidateSlotIdentityUnreadable,
	PreSkinValidateSlotValueId,
	PreSkinValidateSlotVersion,
	PreSkinValidateSerialChurn,
	PreSkinValidateNoContext,
	PreSkinValidateNoSerial,

	Count,
};

struct DX12ProfilingFieldDef
{
	const char *name;    // exact bucket name as recorded by the profiler
	const char *label;   // short label used on screen
	DX12ProfilingField id;
	bool failureOnly;    // only listed while non-zero
};

struct DX12ProfilingFieldValue
{
	LONG64 calls = 0;
	LONG64 items = 0;
	LONG64 totalUs = 0;
	LONG64 maxUs = 0;
};

// The bucket names the Snapshot view reads. Every string here was checked
// against a DX12_PROF_* call site; a name that no longer exists simply reads
// zero and is called out in the HUD's own documentation as a stale entry.
inline constexpr DX12ProfilingFieldDef kDX12ProfilingFields[] = {
	{ "Rm.IaNegCacheHit", "IaNegCacheHit", DX12ProfilingField::IaNegCacheHit, false },
	{ "Rm.IaNegVerdictRuns", "IaNegVerdictRuns", DX12ProfilingField::IaNegVerdictRuns, false },
	{ "Rm.IaNegVerdictNegative", "IaNegVerdictNegative", DX12ProfilingField::IaNegVerdictNegative, false },
	{ "Rm.IaNegVerdictGated", "IaNegVerdictGated", DX12ProfilingField::IaNegVerdictGated, false },

	{ "Rm.PreSkinVerdictDispatchHits", "VerdictDispatchHits", DX12ProfilingField::PreSkinVerdictDispatchHits, false },
	{ "Rm.PreSkinVerdictEarlyHits", "VerdictEarlyHits", DX12ProfilingField::PreSkinVerdictEarlyHits, false },
	{ "Rm.PreSkinVerdictDescriptorHits", "VerdictDescriptorHits", DX12ProfilingField::PreSkinVerdictDescriptorHits, false },
	{ "Rm.PreSkinVerdictResidentHits", "VerdictResidentHits", DX12ProfilingField::PreSkinVerdictResidentHits, false },
	{ "Rm.PreSkinVerdictTemplateHits", "VerdictTemplateHits", DX12ProfilingField::PreSkinVerdictTemplateHits, false },
	{ "Rm.PreSkinVerdictTemplateFingerprintHits", "VerdictTemplateFpHits", DX12ProfilingField::PreSkinVerdictTemplateFingerprintHits, false },
	{ "Rm.PreSkinVerdictGeometryHits", "VerdictGeometryHits", DX12ProfilingField::PreSkinVerdictGeometryHits, false },
	{ "Rm.PreSkinVerdictKeyMatches", "VerdictKeyMatches", DX12ProfilingField::PreSkinVerdictKeyMatches, false },
	{ "Rm.PreSkinVerdictBindingStateInvalid", "VerdictBindingInvalid", DX12ProfilingField::PreSkinVerdictBindingStateInvalid, false },
	{ "Rm.PreSkinVerdictIdentityProbeRuns", "IdentityProbeRuns", DX12ProfilingField::PreSkinVerdictIdentityProbeRuns, false },
	{ "Rm.PreSkinVerdictIdentityProbeHits", "IdentityProbeHits", DX12ProfilingField::PreSkinVerdictIdentityProbeHits, false },
	{ "Rm.PreSkinPlanCacheLookups", "PlanCacheLookups", DX12ProfilingField::PreSkinPlanCacheLookups, false },
	{ "Rm.PreSkinPlanCacheNegativeHits", "PlanCacheNegHits", DX12ProfilingField::PreSkinPlanCacheNegativeHits, false },

	{ "Rm.PreSkinBindingCacheCalls", "BindingCacheCalls", DX12ProfilingField::PreSkinBindingCacheCalls, false },
	{ "Rm.PreSkinBindingCacheHits", "BindingCacheHits", DX12ProfilingField::PreSkinBindingCacheHits, false },
	{ "Rm.PreSkinBindingCacheMisses", "BindingCacheMisses", DX12ProfilingField::PreSkinBindingCacheMisses, false },
	{ "Rm.PreSkinBindingCacheStores", "BindingCacheStores", DX12ProfilingField::PreSkinBindingCacheStores, false },
	{ "Rm.PreSkinStoreCommitted", "StoreCommitted", DX12ProfilingField::PreSkinStoreCommitted, false },
	{ "Rm.PreSkinStoreStillInvalidAfterCollect", "StoreStillInvalidAfter", DX12ProfilingField::PreSkinStoreStillInvalidAfterCollect, false },
	{ "Rm.PreSkinValidatePass", "StoreValidatePass", DX12ProfilingField::PreSkinValidatePass, false },

	{ "Rm.DescCopyChanged", "DescCopyChanged", DX12ProfilingField::DescCopyChanged, false },
	{ "Rm.DescCopyUnchanged", "DescCopyUnchanged", DX12ProfilingField::DescCopyUnchanged, false },
	{ "Rm.DescCopyDirectPath", "DescCopyDirectPath", DX12ProfilingField::DescCopyDirectPath, false },
	{ "Rm.DescCopyLockedPath", "DescCopyLockedPath", DX12ProfilingField::DescCopyLockedPath, false },
	{ "Rm.DescriptorHeapChanged", "DescriptorHeapChanged", DX12ProfilingField::DescriptorHeapChanged, false },
	{ "Desc.ResolveFailure", "Desc.ResolveFailure", DX12ProfilingField::DescResolveFailure, false },
	{ "Lock.Desc.HeapCacheHit", "Lock.HeapCacheHit", DX12ProfilingField::DescHeapCacheHit, false },
	{ "Desc.CopySlots", "Desc.CopySlots(items)", DX12ProfilingField::DescCopySlots, false },
	{ "Desc.CopyBlocks", "Desc.CopyBlocks(items)", DX12ProfilingField::DescCopyBlocks, false },

	{ "FastForward", "FastForward", DX12ProfilingField::FastForward, false },
	{ "Rm.DrawNoopFastPathAttempts", "DrawNoopFastPathAttempts", DX12ProfilingField::DrawNoopFastPathAttempts, false },
	{ "Rm.DrawNoopFastPathForwarded", "DrawNoopFastPathForwarded", DX12ProfilingField::DrawNoopFastPathForwarded, false },
	{ "Rm.DrawNoopFastPathNonNoopDecision", "DrawNoopNonNoopDecision", DX12ProfilingField::DrawNoopFastPathNonNoopDecision, false },
	{ "Rm.IaBindFastPath", "IaBindFastPath", DX12ProfilingField::IaBindFastPath, false },
	{ "Rm.IaBindSlowPath", "IaBindSlowPath", DX12ProfilingField::IaBindSlowPath, false },
	{ "Rm.IaPreSkinRedirectGated", "IaPreSkinRedirectGated", DX12ProfilingField::IaPreSkinRedirectGated, false },
	{ "Rm.PreSkinRedirectNegMiss", "PreSkinRedirectNegMiss", DX12ProfilingField::PreSkinRedirectNegMiss, false },
	{ "Rm.PreSkinRedirectNegHit", "PreSkinRedirectNegHit", DX12ProfilingField::PreSkinRedirectNegHit, false },
	{ "Rm.PreSkinRedirectViewNegHit", "PreSkinRedirectViewNegHit", DX12ProfilingField::PreSkinRedirectViewNegHit, false },

	{ "Present.original_present", "Present.original", DX12ProfilingField::PresentOriginalPresent, false },
	{ "Present.overlay_text_record", "HUD text record", DX12ProfilingField::PresentOverlayText, false },
	{ "Present.profiling_and_hotpath_flush", "profiling flush", DX12ProfilingField::PresentProfilingFlush, false },
	{ "Wait.system", "Wait.system", DX12ProfilingField::WaitSystem, false },
	{ "Wait.event", "Wait.event", DX12ProfilingField::WaitEvent, false },
	{ "Fence.op", "Fence.op", DX12ProfilingField::FenceOp, false },
	{ "Queue.wrapper_total", "ExecuteCmdLists", DX12ProfilingField::QueueWrapperTotal, false },
	{ "Queue.Wait", "Queue.Wait", DX12ProfilingField::QueueWait, false },
	{ "Queue.Signal", "Queue.Signal", DX12ProfilingField::QueueSignal, false },

	{ "Rm.PreSkinStoreNoSnapshot", "StoreNoSnapshot", DX12ProfilingField::PreSkinStoreNoSnapshot, true },
	{ "Rm.PreSkinStoreNotStable", "StoreNotStable", DX12ProfilingField::PreSkinStoreNotStable, true },
	{ "Rm.PreSkinStoreRuntimeDescriptorsUnstable", "StoreRuntimeDescUnstable", DX12ProfilingField::PreSkinStoreRuntimeDescriptorsUnstable, true },
	{ "Rm.PreSkinStoreObservedSlotsInvalid", "StoreObservedSlotsInvalid", DX12ProfilingField::PreSkinStoreObservedSlotsInvalid, true },
	{ "Rm.PreSkinValidateNoBindings", "ValidNoBindings", DX12ProfilingField::PreSkinValidateNoBindings, true },
	{ "Rm.PreSkinValidateHeap", "ValidHeap", DX12ProfilingField::PreSkinValidateHeap, true },
	{ "Rm.PreSkinValidateRootSignature", "ValidRootSignature", DX12ProfilingField::PreSkinValidateRootSignature, true },
	{ "Rm.PreSkinValidateRootDescriptor", "ValidRootDescriptor", DX12ProfilingField::PreSkinValidateRootDescriptor, true },
	{ "Rm.PreSkinValidateConstants", "ValidConstants", DX12ProfilingField::PreSkinValidateConstants, true },
	{ "Rm.PreSkinValidateSlotCountMismatch", "ValidSlotCountMismatch", DX12ProfilingField::PreSkinValidateSlotCountMismatch, true },
	{ "Rm.PreSkinValidateNoObservedHeap", "ValidNoObservedHeap", DX12ProfilingField::PreSkinValidateNoObservedHeap, true },
	{ "Rm.PreSkinValidateTableUnresolved", "ValidTableUnresolved", DX12ProfilingField::PreSkinValidateTableUnresolved, true },
	{ "Rm.PreSkinValidateSlotOutOfRange", "ValidSlotOutOfRange", DX12ProfilingField::PreSkinValidateSlotOutOfRange, true },
	{ "Rm.PreSkinValidateHeapRecordUnresolved", "ValidHeapRecordUnresolved", DX12ProfilingField::PreSkinValidateHeapRecordUnresolved, true },
	{ "Rm.PreSkinValidateSlotIdentityUnreadable", "ValidSlotIdentityUnreadable", DX12ProfilingField::PreSkinValidateSlotIdentityUnreadable, true },
	{ "Rm.PreSkinValidateSlotValueId", "ValidSlotValueId", DX12ProfilingField::PreSkinValidateSlotValueId, true },
	{ "Rm.PreSkinValidateSlotVersion", "ValidSlotVersion", DX12ProfilingField::PreSkinValidateSlotVersion, true },
	{ "Rm.PreSkinValidateSerialChurn", "ValidSerialChurn", DX12ProfilingField::PreSkinValidateSerialChurn, true },
	{ "Rm.PreSkinValidateNoContext", "ValidNoContext", DX12ProfilingField::PreSkinValidateNoContext, true },
	{ "Rm.PreSkinValidateNoSerial", "ValidNoSerial", DX12ProfilingField::PreSkinValidateNoSerial, true },
};

// How many rows of the table above are failure counters. The middle column
// ranks them, so it needs one slot per candidate; deriving the count from the
// table means a new failure counter cannot silently overflow that ranking.
inline constexpr int CountFailureFields()
{
	int count = 0;
	for (const DX12ProfilingFieldDef &def : kDX12ProfilingFields) {
		if (def.failureOnly)
			++count;
	}
	return count;
}

inline constexpr int kDX12ProfilingFailureFieldCount = CountFailureFields();

// One window's worth of the counters above, filled in a single pass over the
// ranked rows. Matching is a first-character test followed by strcmp against
// ~60 names, run once per published report on the writer thread - the cost is
// bounded and off the frame path, which is why it does not need a hash table.
struct DX12ProfilingFields
{
	DX12ProfilingFieldValue value[static_cast<int>(DX12ProfilingField::Count)] = {};

	void Fill(const DX12ProfilingRow *rows, UINT rowCount)
	{
		for (UINT row = 0; row < rowCount; ++row) {
			const char *name = rows[row].name;
			if (!name)
				continue;
			for (const DX12ProfilingFieldDef &def : kDX12ProfilingFields) {
				if (def.name[0] != name[0] || strcmp(def.name, name) != 0)
					continue;
				DX12ProfilingFieldValue &slot =
					value[static_cast<int>(def.id)];
				slot.calls = rows[row].calls;
				slot.items = rows[row].items;
				slot.totalUs = rows[row].totalUs;
				slot.maxUs = rows[row].maxUs;
				break;
			}
		}
	}

	const DX12ProfilingFieldValue &Get(DX12ProfilingField id) const
	{
		return value[static_cast<int>(id)];
	}
};

class DX12ProfilingText
{
public:
	// Widest name field a ranked row uses; RowNameWidth() lowers it on a screen
	// too narrow to spend this much on a name.
	static constexpr int kRowNameWidth = 30;

	// Returns the number of bytes written (excluding the terminator). The
	// result always fits `capacity` and is always NUL-terminated.
	static size_t Format(
		char *out, size_t capacity, DX12ProfilingHud::Mode mode,
		const DX12ProfilingWindow &window,
		const DX12ProfilingRow *rows, UINT rowCount);

private:
	using Layout = DX12ProfilingLayout;

	// --- the report buffer ---------------------------------------------------
	struct Emitter
	{
		char *out;
		size_t capacity;
		size_t used;
		// Characters one full-width line may carry, from the measured budget.
		int lineChars;

		void Line(const char *fmt, ...);
		void ColumnSegment(int column, const char *text);
		void Newline();
		void Append(const char *text, size_t length);
		bool Full() const { return used + 2 >= capacity; }
	};

	// --- one staged column ---------------------------------------------------
	// Every page builds its columns independently and interleaves them
	// afterwards, so a column that runs out of content stops contributing to a
	// row instead of shifting the ones below it. Lines are staged, not streamed,
	// and each column carries its own row cap and measured text width.
	struct Column
	{
		// Sized for the whole report rather than for one column's cap: the
		// per-column cap lives in `capacity`, the measured width in `maxChars`,
		// and the difference costs a few kilobytes of writer-thread stack.
		char text[Layout::kMaxReportLines][Layout::kMaxColumnChars + 1];
		int capacity;
		int maxChars;
		int count = 0;

		Column(int maxRows, int chars) : capacity(maxRows),
			maxChars(chars > 0 && chars <= Layout::kMaxColumnChars
				? chars : Layout::kMaxColumnChars) {}

		void Line(const char *fmt, ...);
		int Room() const { return capacity - count; }
		bool Full() const { return count >= capacity; }
	};

	// --- unit formatting -----------------------------------------------------
	// "123 us" below a millisecond, "1.23 ms" from there up. Every duration in
	// the report goes through one of these, so the rule cannot drift between
	// columns.
	static void FormatUs(char *out, size_t capacity, LONG64 us);
	// The same rule, compact ("252us" / "8.64ms"), for rows that also carry a
	// bucket name.
	static void FormatUsCompact(char *out, size_t capacity, LONG64 us);
	static void FormatUsPerFrame(
		char *out, size_t capacity, LONG64 totalUs, LONG frames);
	static void FormatMs2(char *out, size_t capacity, LONG64 us);
	// "name=12.3%": a numerator's share of its denominator as one string, so
	// no two rows can format the same ratio differently. A non-zero ratio below
	// 1% keeps two decimals - a resolution failure at 0.02% must not print as
	// "0.0%", which reads as none.
	static void FormatShare(
		char *out, size_t capacity, const char *label,
		LONG64 numerator, LONG64 denominator);
	// Call counts per frame: two decimals while the number is small enough to
	// be interesting, an integer once it is not, so the field never widens.
	static void FormatPerFrame(
		char *out, size_t capacity, LONG64 count, LONG frames);
	static double Percent(LONG64 numerator, LONG64 denominator);
	static bool Flagged(LONG64 maxUs) { return maxUs >= 1000; }

	// A bucket that measures the game rather than this mod stays visible in the
	// right column's frame block but is kept out of the time table and out of
	// the spike ranking. See DX12ProfilingClassify, which both sinks share.
	static bool ObservedOnly(const char *name)
	{
		return DX12ProfilingClassify::ObservedOnly(name);
	}
	static bool StartsWith(const char *name, const char *prefix);

	// --- one screen's worth of report ---------------------------------------
	// The measured budget plus the row count that fits both the screen and the
	// report's character ceiling. Every page takes one of these, which is why no
	// page can lay itself out for a screen it is not drawn on.
	struct Page
	{
		DX12ProfilingBudget budget;
		int rows = 0;

		int ColumnChars(int column) const
		{
			return column >= 0 && column < Layout::kColumnCount
				? budget.columnChars[column] : 0;
		}
	};

	static void FormatOverview(
		Emitter &e, const Page &page, const DX12ProfilingWindow &window,
		const DX12ProfilingRow *rows, UINT rowCount);
	static void FormatBuckets(
		Emitter &e, const Page &page, const DX12ProfilingWindow &window,
		const DX12ProfilingRow *rows, UINT rowCount);
	static void FormatCounters(
		Emitter &e, const Page &page, const DX12ProfilingWindow &window,
		const DX12ProfilingRow *rows, UINT rowCount);

	static void BuildTimeTable(
		Column &column, const DX12ProfilingWindow &window,
		const DX12ProfilingRow *rows, UINT rowCount, UINT *shownRows);
	static void BuildRatioColumn(
		Column &column, const DX12ProfilingWindow &window,
		const DX12ProfilingFields &fields);
	static void BuildFrameColumn(
		Column &column, const DX12ProfilingWindow &window,
		const DX12ProfilingRow *rows, UINT rowCount,
		const DX12ProfilingFields &fields);
	static UINT SelectByMax(
		const DX12ProfilingRow *rows, UINT rowCount,
		const DX12ProfilingRow **top, UINT topCapacity);

	// Row flags. Both are two characters wide so a row can carry either, both
	// or neither without the columns drifting.
	static void RowFlags(char *out, size_t capacity, const DX12ProfilingRow &row);
	// "name <perFrame>/f x<calls>/f max <max> <flags>": the ranked-row format the
	// Buckets and Counters pages share, so one bucket cannot be printed two ways
	// on one screen.
	static void CountRow(char *out, size_t capacity, const DX12ProfilingRow &row,
		LONG frames, int width);
	// "name <calls>/f <max> <flags>": the same idea for the Rm.* verification
	// counters, whose per-frame number is a rate rather than a cost.
	static void CounterRow(char *out, size_t capacity, const DX12ProfilingRow &row,
		LONG frames, int width);
	// Characters a ranked row can spend on its name, given the column budget the
	// render thread measured. The name is what shrinks when the screen is narrow;
	// the numbers never do.
	static int RowNameWidth(int width);

	// Page furniture. Every page starts with the same title block, so a
	// screenshot says which page, which build and which window it is.
	static void PageTitle(Emitter &e, const DX12ProfilingWindow &window);
	static void OverviewTitle(
		Emitter &e, const DX12ProfilingWindow &window, UINT shownRows);
	static void OverviewLegend(Emitter &e);
	static void UnitsLegend(Emitter &e);

	// Writes three staged columns as interleaved rows: screen row `r` carries
	// row `r` of each column, and a column that runs out simply stops
	// contributing to that row.
	static void EmitColumns(
		Emitter &e, const Column *left, const Column *middle, const Column *right);
	static const char *DomainOf(const char *name);
};

// ----------------------------------------------------------------------------
// Emitter / Column plumbing.
// ----------------------------------------------------------------------------
inline void DX12ProfilingText::Emitter::Append(const char *text, size_t length)
{
	if (!text || !length || Full())
		return;
	size_t room = capacity - used - 1;
	if (length > room)
		length = room;
	memcpy(out + used, text, length);
	used += length;
	out[used] = '\0';
}

inline void DX12ProfilingText::Emitter::Line(const char *fmt, ...)
{
	if (Full())
		return;
	char buffer[512];
	va_list args;
	va_start(args, fmt);
	const int written = _vsnprintf_s(
		buffer, sizeof(buffer), _TRUNCATE, fmt, args);
	va_end(args);
	if (written <= 0)
		return;
	size_t length = static_cast<size_t>(written);
	// Cut, not wrapped: a full-width line longer than the screen would run off
	// its right edge with no sign that anything was lost, so it is cut at the
	// measured width and the cut is marked.
	if (lineChars > 0 && length > static_cast<size_t>(lineChars) + 1) {
		buffer[lineChars] = '>';
		buffer[lineChars + 1] = '\n';
		buffer[lineChars + 2] = '\0';
		length = static_cast<size_t>(lineChars) + 2;
	}
	Append(buffer, length);
}

// One screen row carries up to three column segments, each introduced by a
// marker byte: the glyph atlas covers printable ASCII only, so a control byte
// can never be mistaken for text and the overlay can place a segment without
// parsing the words in it. A row with no marker is a full-width title.
inline void DX12ProfilingText::Emitter::ColumnSegment(int column, const char *text)
{
	if (Full() || !text)
		return;
	const char marker = static_cast<char>(Layout::kColumnMark + column);
	Append(&marker, 1);
	Append(text, strlen(text));
}

inline void DX12ProfilingText::Emitter::Newline()
{
	Append("\n", 1);
}

inline void DX12ProfilingText::Column::Line(const char *fmt, ...)
{
	if (Full())
		return;
	va_list args;
	va_start(args, fmt);
	// A staged line is exactly one screen row: the renderer splits the report by
	// newline and interleaves the columns row by row, so a newline inside a
	// staged line would push every row of that column down by one and break the
	// alignment of the whole page. Stripped rather than documented because that
	// failure looks like a layout bug three functions away.
	const int written = _vsnprintf_s(
		text[count], static_cast<size_t>(maxChars) + 1, _TRUNCATE, fmt, args);
	va_end(args);
	if (written < 0 && maxChars > 0) {
		// A cut line says so where it was cut: a number that lost its tail must
		// not read as a number that was never there.
		text[count][maxChars - 1] = '>';
		text[count][maxChars] = '\0';
	} else {
		size_t length = strlen(text[count]);
		while (length > 0 && (text[count][length - 1] == '\n' ||
			text[count][length - 1] == '\r')) {
			text[count][--length] = '\0';
		}
	}
	++count;
}

// ----------------------------------------------------------------------------
// Unit formatting.
// ----------------------------------------------------------------------------
inline void DX12ProfilingText::FormatUs(char *out, size_t capacity, LONG64 us)
{
	if (us <= 0) {
		_snprintf_s(out, capacity, _TRUNCATE, "0 us");
		return;
	}
	if (us < 1000) {
		_snprintf_s(out, capacity, _TRUNCATE, "%lld us",
			static_cast<long long>(us));
		return;
	}
	_snprintf_s(out, capacity, _TRUNCATE, "%.2f ms", static_cast<double>(us) / 1000.0);
}

// The same rule without the space, for the ranked rows: there the unit has to
// share the line with a name that is longer than any number on it.
inline void DX12ProfilingText::FormatUsCompact(char *out, size_t capacity, LONG64 us)
{
	if (us <= 0) {
		_snprintf_s(out, capacity, _TRUNCATE, "0us");
		return;
	}
	if (us < 1000) {
		_snprintf_s(out, capacity, _TRUNCATE, "%lldus", static_cast<long long>(us));
		return;
	}
	_snprintf_s(out, capacity, _TRUNCATE, "%.2fms", static_cast<double>(us) / 1000.0);
}

inline void DX12ProfilingText::FormatUsPerFrame(
	char *out, size_t capacity, LONG64 totalUs, LONG frames)
{
	if (totalUs <= 0 || frames <= 0) {
		_snprintf_s(out, capacity, _TRUNCATE, "0 us");
		return;
	}
	// Rounded to the microsecond first: a row that took 1.4us per frame must
	// not print as "1 us" and then fail to add up in the SUM line.
	const LONG64 perFrame = (totalUs + frames / 2) / frames;
	FormatUs(out, capacity, perFrame);
}

inline void DX12ProfilingText::FormatMs2(char *out, size_t capacity, LONG64 us)
{
	_snprintf_s(out, capacity, _TRUNCATE, "%.2f ms", static_cast<double>(us) / 1000.0);
}

inline void DX12ProfilingText::FormatShare(
	char *out, size_t capacity, const char *label,
	LONG64 numerator, LONG64 denominator)
{
	const double percent = Percent(numerator, denominator);
	_snprintf_s(out, capacity, _TRUNCATE,
		numerator > 0 && percent < 1.0 ? "%s=%.2f%%" : "%s=%.1f%%",
		label ? label : "share", percent);
}

inline void DX12ProfilingText::FormatPerFrame(
	char *out, size_t capacity, LONG64 count, LONG frames)
{
	if (frames <= 0) {
		_snprintf_s(out, capacity, _TRUNCATE, "0/f");
		return;
	}
	const double perFrame = static_cast<double>(count) / static_cast<double>(frames);
	if (perFrame >= 1000.0 || count >= 1000000) {
		_snprintf_s(out, capacity, _TRUNCATE, "%lld/f",
			static_cast<long long>(count / frames));
		return;
	}
	_snprintf_s(out, capacity, _TRUNCATE, "%.2f/f", perFrame);
}

inline double DX12ProfilingText::Percent(LONG64 numerator, LONG64 denominator)
{
	return denominator > 0
		? 100.0 * static_cast<double>(numerator) /
			static_cast<double>(denominator)
		: 0.0;
}

inline bool DX12ProfilingText::StartsWith(const char *name, const char *prefix)
{
	if (!name || !prefix)
		return false;
	return strncmp(name, prefix, strlen(prefix)) == 0;
}

// ----------------------------------------------------------------------------
// Snapshot: left column, the time table.
// ----------------------------------------------------------------------------
// Row flags, in the two characters every row ends with: "!" for a single call
// that reached a millisecond, "~" for a row that is entirely nested - every
// sample of it was taken inside another row, so it is visible but contributes
// nothing to OUR TOTAL. A row can be both, which is why they are two fixed
// positions rather than a trailing suffix.
inline void DX12ProfilingText::RowFlags(
	char *out, size_t capacity, const DX12ProfilingRow &row)
{
	if (!out || capacity < 3)
		return;
	out[0] = Flagged(row.maxUs) ? '!' : ' ';
	out[1] = (row.totalUs > 0 && row.rootUs <= 0) ? '~' : ' ';
	out[2] = '\0';
}

inline void DX12ProfilingText::BuildTimeTable(
	Column &column, const DX12ProfilingWindow &window,
	const DX12ProfilingRow *rows, UINT rowCount, UINT *shownRows)
{
	const LONG frames = window.FrameDivisor();
	// Five rows are reserved before the loop: the column header, the two SUM
	// lines (root and all-buckets) and the "... more rows" note, plus one
	// spare, so a SUM line can never be the row that falls off the column.
	const int budget = column.Room() - 5;

	column.Line("%-*.*s %9s %9s %9s %8s %s",
		kRowNameWidth, kRowNameWidth, "bucket", "ms/frame", "calls/fr",
		"avg", "max", "f");

	*shownRows = 0;
	UINT hidden = 0;
	for (UINT i = 0; i < rowCount; ++i) {
		const DX12ProfilingRow &row = rows[i];
		if (row.totalUs <= 0 || ObservedOnly(row.name))
			continue;
		if (static_cast<int>(*shownRows) >= budget) {
			++hidden;
			continue;
		}
		char perFrame[16] = {};
		char calls[16] = {};
		char avg[16] = {};
		char peak[16] = {};
		char flags[4] = {};
		FormatUsPerFrame(perFrame, sizeof(perFrame), row.totalUs, frames);
		FormatPerFrame(calls, sizeof(calls), row.calls, frames);
		FormatUs(avg, sizeof(avg), row.calls > 0 ? row.totalUs / row.calls : 0);
		FormatUs(peak, sizeof(peak), row.maxUs);
		RowFlags(flags, sizeof(flags), row);
		column.Line("%-*.*s %9s %9s %9s %8s %s",
			kRowNameWidth, kRowNameWidth, row.name ? row.name : "?",
			perFrame, calls, avg, peak, flags);
		++*shownRows;
	}

	if (hidden)
		column.Line("  ... +%u more rows, ranked by ms/frame", hidden);

	if (window.ourAllRows <= 0) {
		column.Line("  (this window recorded no timed buckets)");
		return;
	}

	// The two totals are the ones the header publishes, computed once in
	// BuildWindowReport so the table footer and the title cannot disagree.
	char root[16] = {};
	char all[16] = {};
	FormatUs(root, sizeof(root),
		frames > 0 ? window.ourRootUs / frames : 0);
	FormatUs(all, sizeof(all), frames > 0 ? window.ourAllUs / frames : 0);
	const LONG64 frameWallUs = window.frameWallUs;
	column.Line("SUM ours  root %s %.1f%% | all %s %.1f%% of frameWall",
		root,
		frameWallUs > 0
			? 100.0 * static_cast<double>(window.ourRootUs) /
				static_cast<double>(frameWallUs)
			: 0.0,
		all,
		frameWallUs > 0
			? 100.0 * static_cast<double>(window.ourAllUs) /
				static_cast<double>(frameWallUs)
			: 0.0);
	column.Line("  rows  root %ld, all %ld, shown %u",
		static_cast<long>(window.ourRootRows),
		static_cast<long>(window.ourAllRows),
		*shownRows);
}

// A "label  value/f  extra" row. `count` is whichever of the bucket's two
// counters the caller means: calls for the COUNT/ADD_US counters, items for
// the ADD_ITEMS ones (Desc.CopySlots, Desc.CopyBlocks).
//
// Every numerator/denominator pair this column has is printed as a percentage
// on the row that carries the numerator, and always through FormatShare, so a
// hit rate reads the same way wherever it appears. The denominator is named in
// the label ("of runs", "of calls", "of stores", "of verdicts") rather than
// left to the reader to infer from which row happens to sit above.
inline void DX12ProfilingText::BuildRatioColumn(
	Column &column, const DX12ProfilingWindow &window,
	const DX12ProfilingFields &fields)
{
	const LONG frames = window.FrameDivisor();
	// 26 + 1 + 9 + 1 + 39 = 76, the column budget. Labels longer than 26 are
	// truncated rather than allowed to push the value out of its field.
	constexpr int kLabel = 26;

	auto row = [&](const char *label, LONG64 count, const char *extra) {
		char perFrame[16] = {};
		FormatPerFrame(perFrame, sizeof(perFrame), count, frames);
		column.Line("%-*.*s %9s %s", kLabel, kLabel, label, perFrame,
			extra ? extra : "");
	};
	auto field = [&](DX12ProfilingField id) -> const DX12ProfilingFieldValue & {
		return fields.Get(id);
	};
	auto share = [](char *out, size_t capacity, const char *label,
			LONG64 numerator, LONG64 denominator) {
		FormatShare(out, capacity, label, numerator, denominator);
	};
	// Same ratio with the denominator spelled out after it: "hit=78.3% of
	// (hit+runs)". Every ratio in this column names its denominator, because
	// which row happens to sit above is not a definition.
	auto shareOf = [](char *out, size_t capacity, const char *label,
			LONG64 numerator, LONG64 denominator, const char *of) {
		char ratio[24] = {};
		FormatShare(ratio, sizeof(ratio), label, numerator, denominator);
		_snprintf_s(out, capacity, _TRUNCATE, "%s %s", ratio, of);
	};

	column.Line("RATIOS / COUNTS   (every count is per frame of %ld)",
		static_cast<long>(window.frames));

	column.Line("-- IA negative cache --");
	{
		const DX12ProfilingFieldValue &hit = field(DX12ProfilingField::IaNegCacheHit);
		const DX12ProfilingFieldValue &runs = field(DX12ProfilingField::IaNegVerdictRuns);
		const DX12ProfilingFieldValue &negative = field(DX12ProfilingField::IaNegVerdictNegative);
		const DX12ProfilingFieldValue &gated = field(DX12ProfilingField::IaNegVerdictGated);
		char extra[48] = {};
		shareOf(extra, sizeof(extra), "hit", hit.calls, hit.calls + runs.calls,
			"of (hit+runs)");
		row("IaNegCacheHit", hit.calls, extra);
		row("IaNegVerdictRuns", runs.calls, "");
		shareOf(extra, sizeof(extra), "neg", negative.calls, runs.calls, "of runs");
		row("IaNegVerdictNegative", negative.calls, extra);
		shareOf(extra, sizeof(extra), "gated", gated.calls, runs.calls, "of runs");
		row("IaNegVerdictGated", gated.calls, extra);
	}

	column.Line("-- PreSkin verdict --");
	{
		// VerdictDispatchHits is the whole verdict path; the seven layer hits
		// below it should add up to roughly this, so each one is shown as its
		// share of it. A layer whose share grows is the layer carrying the
		// traffic.
		const LONG64 verdicts = field(DX12ProfilingField::PreSkinVerdictDispatchHits).calls;
		auto verdictRow = [&](const char *label, DX12ProfilingField id,
				const char *extraLabel, LONG64 extraNumerator) {
			char text[48] = {};
			share(text, sizeof(text), "share", field(id).calls, verdicts);
			if (extraLabel) {
				char more[24] = {};
				share(more, sizeof(more), extraLabel, extraNumerator, field(id).calls);
				const size_t used = strlen(text);
				_snprintf_s(text + used, sizeof(text) - used, _TRUNCATE, " %s", more);
			}
			row(label, field(id).calls, text);
		};
		const char *const labels[] = {
			"VerdictDispatchHits", "VerdictEarlyHits", "VerdictDescriptorHits",
			"VerdictResidentHits", "VerdictTemplateHits", "VerdictTemplateFpHits",
		};
		const DX12ProfilingField ids[] = {
			DX12ProfilingField::PreSkinVerdictDispatchHits,
			DX12ProfilingField::PreSkinVerdictEarlyHits,
			DX12ProfilingField::PreSkinVerdictDescriptorHits,
			DX12ProfilingField::PreSkinVerdictResidentHits,
			DX12ProfilingField::PreSkinVerdictTemplateHits,
			DX12ProfilingField::PreSkinVerdictTemplateFingerprintHits,
		};
		for (int i = 0; i < 6; ++i)
			verdictRow(labels[i], ids[i], nullptr, 0);
		// The one verdict layer with a second denominator of its own: how many
		// of the draws that matched a verdict key actually hit geometry.
		verdictRow("VerdictGeometryHits", DX12ProfilingField::PreSkinVerdictGeometryHits,
			"geom/key", field(DX12ProfilingField::PreSkinVerdictKeyMatches).calls);
		verdictRow("VerdictKeyMatches", DX12ProfilingField::PreSkinVerdictKeyMatches,
			nullptr, 0);
		verdictRow("VerdictBindingInvalid",
			DX12ProfilingField::PreSkinVerdictBindingStateInvalid, nullptr, 0);
	}

	{
		const DX12ProfilingFieldValue &probeRuns = field(DX12ProfilingField::PreSkinVerdictIdentityProbeRuns);
		const DX12ProfilingFieldValue &probeHits = field(DX12ProfilingField::PreSkinVerdictIdentityProbeHits);
		char extra[48] = {};
		share(extra, sizeof(extra), "hits/runs", probeHits.calls, probeRuns.calls);
		row("IdentityProbeRuns", probeRuns.calls, "");
		row("IdentityProbeHits", probeHits.calls, extra);
	}
	{
		const DX12ProfilingFieldValue &lookups = field(DX12ProfilingField::PreSkinPlanCacheLookups);
		const DX12ProfilingFieldValue &negHits = field(DX12ProfilingField::PreSkinPlanCacheNegativeHits);
		char extra[48] = {};
		char hitShare[24] = {};
		share(hitShare, sizeof(hitShare), "negHit", negHits.calls, lookups.calls);
		_snprintf_s(extra, sizeof(extra), _TRUNCATE, "%s of lookups (%.2f/f)",
			hitShare,
			static_cast<double>(negHits.calls) / static_cast<double>(frames));
		row("PlanCacheLookups", lookups.calls, extra);
	}

	column.Line("-- PreSkin store --");
	{
		const DX12ProfilingFieldValue &calls = field(DX12ProfilingField::PreSkinBindingCacheCalls);
		const DX12ProfilingFieldValue &hits = field(DX12ProfilingField::PreSkinBindingCacheHits);
		const DX12ProfilingFieldValue &misses = field(DX12ProfilingField::PreSkinBindingCacheMisses);
		const DX12ProfilingFieldValue &stores = field(DX12ProfilingField::PreSkinBindingCacheStores);
		const DX12ProfilingFieldValue &committed = field(DX12ProfilingField::PreSkinStoreCommitted);
		const DX12ProfilingFieldValue &stillInvalid =
			field(DX12ProfilingField::PreSkinStoreStillInvalidAfterCollect);
		const DX12ProfilingFieldValue &validatePass = field(DX12ProfilingField::PreSkinValidatePass);
		char extra[48] = {};
		share(extra, sizeof(extra), "Stores/Calls", stores.calls, calls.calls);
		row("BindingCacheCalls", calls.calls, extra);
		shareOf(extra, sizeof(extra), "hit", hits.calls, calls.calls, "of calls");
		row("BindingCacheHits", hits.calls, extra);
		shareOf(extra, sizeof(extra), "miss", misses.calls, calls.calls, "of calls");
		row("BindingCacheMisses", misses.calls, extra);
		shareOf(extra, sizeof(extra), "committed", committed.calls, stores.calls,
			"of stores");
		row("BindingCacheStores", stores.calls, extra);
		row("StoreCommitted", committed.calls, "");
		shareOf(extra, sizeof(extra), "stillInvalid", stillInvalid.calls, stores.calls,
			"of stores");
		row("StoreStillInvalidAfter", stillInvalid.calls, extra);
		shareOf(extra, sizeof(extra), "passed", validatePass.calls, stores.calls,
			"of stores");
		row("StoreValidatePass", validatePass.calls, extra);
	}
	// Failures: non-zero only, ranked by how often they happen. A healthy
	// session must not spend a dozen rows saying nothing, and the rows it does
	// get must be the ones that are actually firing - the table's own order is
	// store-then-validate, which says nothing about severity.
	{
		DX12ProfilingFailureRow ranked[kDX12ProfilingFailureFieldCount] = {};
		int nonZero = 0;
		int zero = 0;
		for (const DX12ProfilingFieldDef &def : kDX12ProfilingFields) {
			if (!def.failureOnly)
				continue;
			const LONG64 calls = fields.Get(def.id).calls;
			if (!calls) {
				++zero;
				continue;
			}
			int at = nonZero++;
			while (at > 0 && ranked[at - 1].calls < calls) {
				ranked[at] = ranked[at - 1];
				--at;
			}
			ranked[at].label = def.label;
			ranked[at].calls = calls;
		}

		const int shown = nonZero < Layout::kPreSkinFailureRows
			? nonZero : Layout::kPreSkinFailureRows;
		for (int i = 0; i < shown; ++i)
			row(ranked[i].label, ranked[i].calls, "<-- FAILURE");
		if (!nonZero)
			column.Line("  store/validate failures: none this window");
		else if (nonZero > shown)
			column.Line("  (+%d more non-zero, past the top-%d cut)",
				nonZero - shown, Layout::kPreSkinFailureRows);
		if (nonZero && zero)
			column.Line("  (+%d more failure counters, all zero)", zero);
	}

	column.Line("-- descriptor copy --");
	{
		const DX12ProfilingFieldValue &changed = field(DX12ProfilingField::DescCopyChanged);
		const DX12ProfilingFieldValue &unchanged = field(DX12ProfilingField::DescCopyUnchanged);
		const DX12ProfilingFieldValue &direct = field(DX12ProfilingField::DescCopyDirectPath);
		const DX12ProfilingFieldValue &locked = field(DX12ProfilingField::DescCopyLockedPath);
		const DX12ProfilingFieldValue &slots = field(DX12ProfilingField::DescCopySlots);
		const DX12ProfilingFieldValue &blocks = field(DX12ProfilingField::DescCopyBlocks);
		const DX12ProfilingFieldValue &heapChanged = field(DX12ProfilingField::DescriptorHeapChanged);
		const DX12ProfilingFieldValue &resolveFailure = field(DX12ProfilingField::DescResolveFailure);
		const LONG64 copyCalls = changed.calls + unchanged.calls;
		char extra[48] = {};
		share(extra, sizeof(extra), "changed/(ch+un)", changed.calls, copyCalls);
		row("DescCopyChanged", changed.calls, extra);
		shareOf(extra, sizeof(extra), "unchanged", unchanged.calls, copyCalls,
			"of copies");
		row("DescCopyUnchanged", unchanged.calls, extra);
		{
			char directShare[24] = {};
			shareOf(directShare, sizeof(directShare), "direct", direct.calls, copyCalls,
				"of copies");
			_snprintf_s(extra, sizeof(extra), _TRUNCATE, "%s locked=%.2f/f",
				directShare,
				static_cast<double>(locked.calls) / static_cast<double>(frames));
			row("DescCopyDirectPath", direct.calls, extra);
		}
		row("DescCopyLockedPath", locked.calls, "");
		{
			char failShare[24] = {};
			share(failShare, sizeof(failShare), "fail", resolveFailure.calls, heapChanged.calls);
			_snprintf_s(extra, sizeof(extra), _TRUNCATE, "ResolveFailure=%.2f/f (%s)",
				static_cast<double>(resolveFailure.calls) / static_cast<double>(frames),
				failShare);
			row("DescriptorHeapChanged", heapChanged.calls, extra);
		}
		row("Lock.HeapCacheHit",
			field(DX12ProfilingField::DescHeapCacheHit).calls, "");
		{
			_snprintf_s(extra, sizeof(extra), _TRUNCATE, "slots/copy=%.1f",
				copyCalls > 0 ? static_cast<double>(slots.items) /
					static_cast<double>(copyCalls) : 0.0);
			row("Desc.CopySlots(items)", slots.items, extra);
		}
		_snprintf_s(extra, sizeof(extra), _TRUNCATE, "blocks/call=%.2f",
			copyCalls > 0 ? static_cast<double>(blocks.items) /
				static_cast<double>(copyCalls) : 0.0);
		row("Desc.CopyBlocks(items)", blocks.items, extra);
	}

	column.Line("-- draw / dispatch fast path --");
	row("FastForward", field(DX12ProfilingField::FastForward).calls, "");
	{
		const DX12ProfilingFieldValue &attempts = field(DX12ProfilingField::DrawNoopFastPathAttempts);
		const DX12ProfilingFieldValue &forwarded = field(DX12ProfilingField::DrawNoopFastPathForwarded);
		const DX12ProfilingFieldValue &nonNoop =
			field(DX12ProfilingField::DrawNoopFastPathNonNoopDecision);
		char extra[48] = {};
		share(extra, sizeof(extra), "forwarded", forwarded.calls, attempts.calls);
		row("DrawNoopFastPathAttempts", attempts.calls, "");
		row("DrawNoopFastPathForwarded", forwarded.calls, extra);
		share(extra, sizeof(extra), "nonNoop", nonNoop.calls, attempts.calls);
		row("DrawNoopNonNoopDecision", nonNoop.calls, extra);
	}
	{
		const DX12ProfilingFieldValue &fast = field(DX12ProfilingField::IaBindFastPath);
		const DX12ProfilingFieldValue &slow = field(DX12ProfilingField::IaBindSlowPath);
		const LONG64 iaBinds = fast.calls + slow.calls;
		char extra[48] = {};
		share(extra, sizeof(extra), "fast", fast.calls, iaBinds);
		row("IaBindFastPath", fast.calls, extra);
		row("IaBindSlowPath", slow.calls, "");
		share(extra, sizeof(extra), "gated",
			field(DX12ProfilingField::IaPreSkinRedirectGated).calls, iaBinds);
		row("IaPreSkinRedirectGated",
			field(DX12ProfilingField::IaPreSkinRedirectGated).calls, extra);
	}
	{
		const DX12ProfilingFieldValue &miss = field(DX12ProfilingField::PreSkinRedirectNegMiss);
		const DX12ProfilingFieldValue &hit = field(DX12ProfilingField::PreSkinRedirectNegHit);
		const DX12ProfilingFieldValue &view = field(DX12ProfilingField::PreSkinRedirectViewNegHit);
		const LONG64 decisions = miss.calls + hit.calls + view.calls;
		char extra[48] = {};
		share(extra, sizeof(extra), "negHit/(hit+miss)", hit.calls, hit.calls + miss.calls);
		row("PreSkinRedirectNegHit", hit.calls, extra);
		row("PreSkinRedirectNegMiss", miss.calls, "");
		share(extra, sizeof(extra), "view", view.calls, decisions);
		row("PreSkinRedirectViewNegHit", view.calls, extra);
	}
}

// ----------------------------------------------------------------------------
// Snapshot: right column, spikes and frame health.
// ----------------------------------------------------------------------------
// Top-N by max, insertion into a fixed array: the rows arrive ranked by total
// time, which says nothing about which one spiked once.
inline UINT DX12ProfilingText::SelectByMax(
	const DX12ProfilingRow *rows, UINT rowCount,
	const DX12ProfilingRow **top, UINT topCapacity)
{
	if (!top || !topCapacity)
		return 0;
	UINT count = 0;
	for (UINT i = 0; i < rowCount; ++i) {
		const DX12ProfilingRow &row = rows[i];
		if (row.maxUs <= 0 || ObservedOnly(row.name))
			continue;
		if (count == topCapacity && top[count - 1]->maxUs >= row.maxUs)
			continue;
		UINT at = count < topCapacity ? count++ : count - 1;
		while (at > 0 && top[at - 1]->maxUs < row.maxUs) {
			top[at] = top[at - 1];
			--at;
		}
		top[at] = &row;
	}
	return count;
}

inline void DX12ProfilingText::BuildFrameColumn(
	Column &column, const DX12ProfilingWindow &window,
	const DX12ProfilingRow *rows, UINT rowCount,
	const DX12ProfilingFields &fields)
{
	const LONG frames = window.FrameDivisor();

	// The window is counted in frames (DX12ProfilingHud::kWindowFrames), and a
	// fallback reports a shorter one only when the session is too slow to reach
	// it - so the header states the count it actually got, the target and the
	// elapsed time, which is everything needed to read a window maximum.
	column.Line("SPIKES / FRAME   (window %ld of %ld frames, %.0f ms)",
		static_cast<long>(window.frames),
		static_cast<long>(DX12ProfilingHud::kWindowFrames),
		window.WindowMs());

	column.Line("-- top by max (ours) --");
	column.Line("%-*.*s %9s %9s %s", kRowNameWidth, kRowNameWidth, "bucket", "max",
		"calls/fr", "f");
	const DX12ProfilingRow *top[Layout::kTopN] = {};
	const UINT topCount = SelectByMax(rows, rowCount, top, Layout::kTopN);
	for (UINT i = 0; i < topCount; ++i) {
		char peak[16] = {};
		char calls[16] = {};
		char flags[4] = {};
		FormatUs(peak, sizeof(peak), top[i]->maxUs);
		FormatPerFrame(calls, sizeof(calls), top[i]->calls, frames);
		RowFlags(flags, sizeof(flags), *top[i]);
		column.Line("%-*.*s %9s %9s %s", kRowNameWidth, kRowNameWidth,
			top[i]->name ? top[i]->name : "?", peak, calls, flags);
	}
	if (!topCount)
		column.Line("  (nothing timed in this window)");

	column.Line("-- frame --");
	{
		char text[64] = {};
		_snprintf_s(text, sizeof(text), _TRUNCATE, "%.2f ms mean / %.2f ms max",
			static_cast<double>(window.frameWallUs) / frames / 1000.0,
			static_cast<double>(window.maxFrameWallUs) / 1000.0);
		column.Line("%-19.19s %s", "frameWall", text);

		_snprintf_s(text, sizeof(text), _TRUNCATE, "%.2f ms mean",
			static_cast<double>(window.outsidePresentUs) / frames / 1000.0);
		column.Line("%-19.19s %s", "outsidePresent", text);

		_snprintf_s(text, sizeof(text), _TRUNCATE, "%.2f ms mean / %.2f ms max",
			static_cast<double>(window.presentWallUs) / frames / 1000.0,
			static_cast<double>(window.maxPresentWallUs) / 1000.0);
		column.Line("%-19.19s %s", "Present (hook wall)", text);

		const DX12ProfilingFieldValue &original = fields.Get(DX12ProfilingField::PresentOriginalPresent);
		_snprintf_s(text, sizeof(text), _TRUNCATE, "%.2f ms/f  max %.2f ms  game's own",
			static_cast<double>(original.totalUs) / frames / 1000.0,
			static_cast<double>(original.maxUs) / 1000.0);
		column.Line("%-19.19s %s", "Present.original", text);

		_snprintf_s(text, sizeof(text), _TRUNCATE, "max %.2f ms  game's wait",
			static_cast<double>(fields.Get(DX12ProfilingField::WaitSystem).maxUs) / 1000.0);
		{
			char perFrame[16] = {};
			FormatPerFrame(perFrame, sizeof(perFrame),
				fields.Get(DX12ProfilingField::WaitSystem).calls, frames);
			column.Line("%-19.19s %9s  %s", "Wait.system", perFrame, text);
		}
		_snprintf_s(text, sizeof(text), _TRUNCATE, "max %.2f ms  game's wait",
			static_cast<double>(fields.Get(DX12ProfilingField::WaitEvent).maxUs) / 1000.0);
		{
			char perFrame[16] = {};
			FormatPerFrame(perFrame, sizeof(perFrame),
				fields.Get(DX12ProfilingField::WaitEvent).calls, frames);
			column.Line("%-19.19s %9s  %s", "Wait.event", perFrame, text);
		}

		char perFrame[16] = {};
		FormatPerFrame(perFrame, sizeof(perFrame),
			fields.Get(DX12ProfilingField::FenceOp).calls, frames);
		_snprintf_s(text, sizeof(text), _TRUNCATE, "max %.2f ms",
			static_cast<double>(fields.Get(DX12ProfilingField::FenceOp).maxUs) / 1000.0);
		column.Line("%-19.19s %9s  %s", "Fence.op", perFrame, text);

		FormatPerFrame(perFrame, sizeof(perFrame),
			fields.Get(DX12ProfilingField::QueueWrapperTotal).calls, frames);
		_snprintf_s(text, sizeof(text), _TRUNCATE, "lists %.2f/f  max %.2f ms",
			static_cast<double>(fields.Get(DX12ProfilingField::QueueWrapperTotal).items) / frames,
			static_cast<double>(fields.Get(DX12ProfilingField::QueueWrapperTotal).maxUs) / 1000.0);
		column.Line("%-19.19s %9s  %s", "ExecuteCmdLists", perFrame, text);

		FormatPerFrame(perFrame, sizeof(perFrame),
			fields.Get(DX12ProfilingField::QueueWait).calls, frames);
		_snprintf_s(text, sizeof(text), _TRUNCATE, "max %.2f ms",
			static_cast<double>(fields.Get(DX12ProfilingField::QueueWait).maxUs) / 1000.0);
		column.Line("%-19.19s %9s  %s", "Queue.Wait", perFrame, text);

		FormatPerFrame(perFrame, sizeof(perFrame),
			fields.Get(DX12ProfilingField::QueueSignal).calls, frames);
		_snprintf_s(text, sizeof(text), _TRUNCATE, "max %.2f ms",
			static_cast<double>(fields.Get(DX12ProfilingField::QueueSignal).maxUs) / 1000.0);
		column.Line("%-19.19s %9s  %s", "Queue.Signal", perFrame, text);

		const DX12ProfilingFieldValue &hud = fields.Get(DX12ProfilingField::PresentOverlayText);
		const DX12ProfilingFieldValue &flush = fields.Get(DX12ProfilingField::PresentProfilingFlush);
		_snprintf_s(text, sizeof(text), _TRUNCATE, "%.2f ms/f  max %.2f ms  (this panel)",
			static_cast<double>(hud.totalUs) / frames / 1000.0,
			static_cast<double>(hud.maxUs) / 1000.0);
		column.Line("%-19.19s %s", "HUD text record", text);

		_snprintf_s(text, sizeof(text), _TRUNCATE, "%.2f ms/f  max %.2f ms  (collector)",
			static_cast<double>(flush.totalUs) / frames / 1000.0,
			static_cast<double>(flush.maxUs) / 1000.0);
		column.Line("%-19.19s %s", "profiling flush", text);

		if (window.droppedFrameSummaries)
			column.Line("%-19.19s %ld  <-- window is incomplete",
				"dropped summaries",
				static_cast<long>(window.droppedFrameSummaries));
	}
}

// ----------------------------------------------------------------------------
// Page furniture.
//
// The title is the part of a screenshot that has to survive being read hours
// later: which page it is, which build produced it, and which window it
// describes. Every page starts with it, which is why no page has to be
// recognised by remembering which columns it had.
// ----------------------------------------------------------------------------
// Page furniture.
//
// The title is the part of a screenshot that has to survive being read hours
// later: which page it is, which build produced it, and which window it
// describes. Every page starts with it, and it is kept inside one screen line -
// a title that gets cut is a title that hides the field the reader needs.
// ----------------------------------------------------------------------------
inline void DX12ProfilingText::PageTitle(
	Emitter &e, const DX12ProfilingWindow &window)
{
	const DX12ProfilingHud::Mode mode = DX12ProfilingHud::GetMode();
	e.Line("== page %d/%d %s == build %s | fps=%.1f frameWall=%.2fms (max %.2fms) | "
		"present=%ld window=%ldf/%.0fms | flags=0x%llx | F11 next\n",
		DX12ProfilingHud::PageNumber(mode), DX12ProfilingHud::kPageCount,
		DX12ProfilingHud::ModeName(mode), kDX12BuildStamp.text,
		window.Fps(), window.FrameMs(), static_cast<double>(window.maxFrameWallUs) / 1000.0,
		static_cast<long>(window.present), static_cast<long>(window.frames),
		window.WindowMs(),
		static_cast<unsigned long long>(window.hookWorkFlags));
}

// The two lines that make a frame auditable: what our scopes add up to, and the
// wall time they have to fit into. The second line exists to be argued with -
// "our buckets exceed the frame" has to be traceable to double counting or to
// parallel threads instead of being taken on faith.
inline void DX12ProfilingText::OverviewTitle(
	Emitter &e, const DX12ProfilingWindow &window, UINT shownRows)
{
	const LONG frames = window.FrameDivisor();
	char ours[16] = {};
	char all[16] = {};
	char frameWall[16] = {};
	char outside[16] = {};
	char present[16] = {};
	char unknown[16] = {};
	char outsideMinus[16] = {};
	FormatUsPerFrame(ours, sizeof(ours), window.ourRootUs, window.frames);
	FormatUsPerFrame(all, sizeof(all), window.ourAllUs, window.frames);
	FormatMs2(frameWall, sizeof(frameWall), window.frameWallUs / frames);
	FormatMs2(outside, sizeof(outside), window.outsidePresentUs / frames);
	FormatMs2(present, sizeof(present), window.presentWallUs / frames);
	FormatMs2(unknown, sizeof(unknown), window.presentWallMinusKnownUs / frames);
	// Negative on purpose when our root total does not fit: that is the signal
	// the line exists for, so it must not be clamped away.
	FormatMs2(outsideMinus, sizeof(outsideMinus),
		(window.outsidePresentUs - window.ourRootUs) / frames);

	const double rootPct = window.frameWallUs > 0
		? 100.0 * static_cast<double>(window.ourRootUs) /
			static_cast<double>(window.frameWallUs)
		: 0.0;
	const double allPct = window.frameWallUs > 0
		? 100.0 * static_cast<double>(window.ourAllUs) /
			static_cast<double>(window.frameWallUs)
		: 0.0;

	e.Line("OUR TOTAL=%s (%.1f%% of frameWall, %ld/%ld rows) | ALL BUCKETS=%s "
		"(%.1f%%, double counted) | draws %ld/f dispatch %ld/f%s\n",
		ours, rootPct,
		static_cast<long>(window.ourRootRows), static_cast<long>(window.ourAllRows),
		all, allPct,
		static_cast<long>(window.frameDraws),
		static_cast<long>(window.frameDispatches),
		window.droppedFrameSummaries ? " | DROPPED FRAMES" : "");
	e.Line("frameWall=%s | outsidePresent=%s | Present hook=%s (unknown %s) | "
		"outsidePresent-OUR TOTAL=%s | rows %u/%ld%s\n",
		frameWall, outside, present, unknown, outsideMinus,
		shownRows, static_cast<long>(window.ourAllRows),
		window.ourRootUs > window.outsidePresentUs
			? "  <-- root > outside: duplicate or parallel work" : "");
}

inline void DX12ProfilingText::OverviewLegend(Emitter &e)
{
	UnitsLegend(e);
	e.Line("OUR TOTAL = top-level scopes once (nested excluded, game's Present/Wait "
		"excluded) | ALL BUCKETS = every row summed, double counted, >100%% normal\n");
}

inline void DX12ProfilingText::UnitsLegend(Emitter &e)
{
	e.Line("units: <1000us -> us, >=1000us -> x.xx ms | /f = per frame | xN = calls/frame | "
		"! max >= 1ms | ~ fully nested | > line cut at column width\n");
}

// ----------------------------------------------------------------------------
// Ranked rows.
//
// Same numbers, two shapes: a duration first (the Buckets page, which ranks by
// cost) or a rate first (the Rm.* counters, which are about how often a gate
// fires). Both are sized against the column budget the render thread measured,
// and what gives way on a narrow screen is the name field - never a number.
// ----------------------------------------------------------------------------
inline int DX12ProfilingText::RowNameWidth(int width)
{
	// " " + per-frame + calls + peak + flags is 24 columns with the compact
	// units; the name gets the rest, between a readable floor and the ceiling
	// that keeps every page's rows aligned.
	int nameWidth = width - 24;
	if (nameWidth > kRowNameWidth)
		nameWidth = kRowNameWidth;
	if (nameWidth < 12)
		nameWidth = 12;
	return nameWidth;
}

inline void DX12ProfilingText::CountRow(char *out, size_t capacity,
	const DX12ProfilingRow &row, LONG frames, int width)
{
	if (!out || capacity < 8)
		return;
	const LONG divisor = frames > 0 ? frames : 1;
	char perFrame[16] = {};
	char maxUs[16] = {};
	char flags[4] = {};
	FormatUsCompact(perFrame, sizeof(perFrame), row.totalUs / divisor);
	FormatUsCompact(maxUs, sizeof(maxUs), row.maxUs);
	RowFlags(flags, sizeof(flags), row);
	const int nameWidth = RowNameWidth(width);
	_snprintf_s(out, capacity, _TRUNCATE, "%-*.*s %6s/f x%-4lld %6s %s",
		nameWidth, nameWidth, row.name ? row.name : "?",
		perFrame, static_cast<long long>(row.calls) / divisor, maxUs, flags);
}

inline void DX12ProfilingText::CounterRow(char *out, size_t capacity,
	const DX12ProfilingRow &row, LONG frames, int width)
{
	if (!out || capacity < 8)
		return;
	const LONG divisor = frames > 0 ? frames : 1;
	char maxUs[16] = {};
	char flags[4] = {};
	FormatUsCompact(maxUs, sizeof(maxUs), row.maxUs);
	RowFlags(flags, sizeof(flags), row);
	const int nameWidth = RowNameWidth(width);
	_snprintf_s(out, capacity, _TRUNCATE, "%-*.*s %7lld/f %6s %s",
		nameWidth, nameWidth, row.name ? row.name : "?",
		static_cast<long long>(row.calls) / divisor, maxUs, flags);
}

inline void DX12ProfilingText::EmitColumns(
	Emitter &e, const Column *left, const Column *middle, const Column *right)
{
	const Column *columns[Layout::kColumnCount] = { left, middle, right };
	int total = left->count > middle->count ? left->count : middle->count;
	if (right->count > total)
		total = right->count;
	for (int row = 0; row < total; ++row) {
		for (int column = 0; column < Layout::kColumnCount; ++column) {
			if (row >= columns[column]->count)
				continue;
			e.ColumnSegment(column, columns[column]->text[row]);
		}
		e.Newline();
	}
}

// ----------------------------------------------------------------------------
// Page 1: where the frame went.
//
// Left ranks the scopes we time, middle answers "what share of the frame is
// each domain", right carries the work that happens once per Present. This is
// the page a screenshot is taken from: it answers "is the mod the reason this
// frame is slow" without opening the log.
// ----------------------------------------------------------------------------
inline void DX12ProfilingText::FormatOverview(
	Emitter &e, const Page &page, const DX12ProfilingWindow &window,
	const DX12ProfilingRow *rows, UINT rowCount)
{
	DX12ProfilingFields fields;
	fields.Fill(rows, rowCount);

	// Staged rather than streamed: the columns have different lengths, and
	// staging is what lets a column that runs out stop contributing instead of
	// shifting the rows below it.
	Column left(page.rows, page.ColumnChars(0));
	Column middle(page.rows, page.ColumnChars(1));
	Column right(page.rows, page.ColumnChars(2));
	UINT shownRows = 0;
	BuildTimeTable(left, window, rows, rowCount, &shownRows);
	BuildRatioColumn(middle, window, fields);
	BuildFrameColumn(right, window, rows, rowCount, fields);

	PageTitle(e, window);
	OverviewTitle(e, window, shownRows);
	OverviewLegend(e);
	EmitColumns(e, &left, &middle, &right);
}

// ----------------------------------------------------------------------------
// Page 2: every bucket, ranked.
//
// The whole table on one screen, cut into three equal runs rather than dealt out
// round-robin: column 0 holds the most expensive buckets, and reading down a
// column and on to the next keeps the ranking monotone. A section header is
// emitted where the domain changes inside a column, not once per domain - the
// ranking is global, so a domain's rows are not adjacent.
// ----------------------------------------------------------------------------
inline void DX12ProfilingText::FormatBuckets(
	Emitter &e, const Page &page, const DX12ProfilingWindow &window,
	const DX12ProfilingRow *rows, UINT rowCount)
{
	PageTitle(e, window);
	UnitsLegend(e);

	Column left(page.rows, page.ColumnChars(0));
	Column middle(page.rows, page.ColumnChars(1));
	Column right(page.rows, page.ColumnChars(2));
	Column *columns[Layout::kColumnCount] = { &left, &middle, &right };

	const LONG frames = window.FrameDivisor();
	const int perColumn = page.rows > 0 ? page.rows : 1;
	const UINT capacity = static_cast<UINT>(perColumn) *
		static_cast<UINT>(Layout::kColumnCount);
	const UINT total = rowCount < capacity ? rowCount : capacity;
	const char *openDomain[Layout::kColumnCount] = {};
	for (UINT i = 0; i < total; ++i) {
		const int index = static_cast<int>(i) / perColumn;
		if (index >= Layout::kColumnCount)
			break;
		Column &target = *columns[index];
		if (target.Full())
			continue;
		const char *domain = DomainOf(rows[i].name);
		if (domain && strcmp(domain, openDomain[index] ? openDomain[index] : "") != 0) {
			if (target.Room() > 1) {
				target.Line("%s", domain);
				openDomain[index] = domain;
			}
		}
		char line[192] = {};
		CountRow(line, sizeof(line), rows[i], frames, target.maxChars);
		target.Line("%s", line);
	}

	EmitColumns(e, &left, &middle, &right);
}

// ----------------------------------------------------------------------------
// Page 3: counters and the per-frame one-shot costs.
//
// Left is what a frame costs besides the buckets, middle is the counters that
// carry item counts (bytes, slots, descriptors) plus the plain window tallies,
// right is the Rm.* verification counters - the ones that say whether a
// replacement gate is doing its job - ranked by how often they fire rather than
// by how much time they took.
// ----------------------------------------------------------------------------
inline void DX12ProfilingText::FormatCounters(
	Emitter &e, const Page &page, const DX12ProfilingWindow &window,
	const DX12ProfilingRow *rows, UINT rowCount)
{
	PageTitle(e, window);
	UnitsLegend(e);

	Column left(page.rows, page.ColumnChars(0));
	Column middle(page.rows, page.ColumnChars(1));
	Column right(page.rows, page.ColumnChars(2));
	const LONG frames = window.FrameDivisor();

	// Every duration below goes through FormatUsPerFrame, so this page obeys the
	// same unit rule as the tables: micro below a millisecond, milliseconds from
	// there up. A column of raw microseconds next to rows that print "4.12 ms" is
	// the one thing the reader cannot do in their head at a glance.
	{
		char total[16] = {}, original[16] = {}, overlay[16] = {}, hud[16] = {};
		FormatUsPerFrame(total, sizeof(total), window.presentWallUs, frames);
		FormatUsPerFrame(original, sizeof(original), window.presentOriginalUs, frames);
		FormatUsPerFrame(overlay, sizeof(overlay), window.presentOtherUs, frames);
		FormatUsPerFrame(hud, sizeof(hud), window.presentOverlayTextUs, frames);
		left.Line("one-shot per frame");
		left.Line("present %10s | original %10s", total, original);
		left.Line("        overlay %10s | hud %10s", overlay, hud);
	}
	{
		char capture[16] = {}, uiAct[16] = {}, counters[16] = {}, postRun[16] = {};
		FormatUsPerFrame(capture, sizeof(capture),
			window.presentCaptureBookkeepingUs, frames);
		FormatUsPerFrame(uiAct, sizeof(uiAct),
			window.presentConsumeUiActionsUs, frames);
		FormatUsPerFrame(counters, sizeof(counters),
			window.presentFrameCountersUs, frames);
		FormatUsPerFrame(postRun, sizeof(postRun),
			window.presentPostOriginalRuntimeUs, frames);
		left.Line("        capture %10s | uiAct %10s", capture, uiAct);
		left.Line("        counters %9s | postRun %9s", counters, postRun);
	}
	{
		char modBegin[16] = {}, unexplained[16] = {};
		FormatUsPerFrame(modBegin, sizeof(modBegin),
			window.presentModBeginFrameUs, frames);
		FormatUsPerFrame(unexplained, sizeof(unexplained),
			window.presentWallMinusKnownUs, frames);
		left.Line("        modBegin %9s | unexplained %9s", modBegin, unexplained);
	}
	{
		char total[16] = {}, signal[16] = {}, wait[16] = {}, original[16] = {};
		FormatUsPerFrame(total, sizeof(total), window.queueEstimatedTotalUs, frames);
		FormatUsPerFrame(signal, sizeof(signal), window.queueSignalUs, frames);
		FormatUsPerFrame(wait, sizeof(wait), window.queueWaitUs, frames);
		FormatUsPerFrame(original, sizeof(original), window.queueOriginalUs, frames);
		left.Line("queue   %10s | signal %10s", total, signal);
		left.Line("        wait %10s | original %10s", wait, original);
		left.Line("        calls %5lld/f | lists %5lld/f",
			static_cast<long long>(window.queueWallCalls) / frames,
			static_cast<long long>(window.queueWallCommandLists) / frames);
	}
	{
		char fences[16] = {}, events[16] = {}, sys[16] = {};
		FormatUsPerFrame(fences, sizeof(fences), window.fenceTotalUs, frames);
		FormatUsPerFrame(events, sizeof(events), window.eventWaitTotalUs, frames);
		FormatUsPerFrame(sys, sizeof(sys), window.systemWaitTotalUs, frames);
		left.Line("fences  %5lld calls | %10s",
			static_cast<long long>(window.fenceCalls) / frames, fences);
		left.Line("eventW  %5lld calls | %10s",
			static_cast<long long>(window.eventWaitCalls) / frames, events);
		// Not our locks: the game's own thread waits, shown so a frame that is
		// slow because a worker sat on a wait is not read as our cost.
		left.Line("sysW    %5lld calls | %10s  (thread waits)",
			static_cast<long long>(window.systemWaitCalls) / frames, sys);
	}
	{
		char tasks[16] = {}, idle[16] = {}, drain[16] = {};
		FormatUsPerFrame(tasks, sizeof(tasks), window.workerTaskUs, frames);
		FormatUsPerFrame(idle, sizeof(idle), window.workerIdleWaitUs, frames);
		FormatUsPerFrame(drain, sizeof(drain), window.workerDrainWaitUs, frames);
		left.Line("worker  %5lld tasks | %10s",
			static_cast<long long>(window.workerTaskCalls) / frames, tasks);
		left.Line("        idle %10s | drain %10s", idle, drain);
	}
	{
		char flush[16] = {};
		FormatUsPerFrame(flush, sizeof(flush),
			window.presentProfilingFlushUs + window.presentFrameCountersUs, frames);
		left.Line("frame   wall %7.2fms | outside %7.2fms | max %7.2fms",
			static_cast<double>(window.frameWallUs) /
				static_cast<double>(frames) / 1000.0,
			static_cast<double>(window.outsidePresentUs) /
				static_cast<double>(frames) / 1000.0,
			static_cast<double>(window.maxFrameWallUs) / 1000.0);
		left.Line("timing  %10s/frame (this report is not free)", flush);
	}

	const int nameWidth = RowNameWidth(middle.maxChars);
	middle.Line("window tallies");
	middle.Line("draws %ld/f | dispatches %ld/f | lists %lld/f",
		static_cast<long>(window.frameDraws), static_cast<long>(window.frameDispatches),
		static_cast<long long>(window.queueWallCommandLists) / frames);
	middle.Line("present enter %lld | leave %lld",
		static_cast<long long>(window.presentEnterCalls),
		static_cast<long long>(window.presentLeaveCalls));
	middle.Line("queue stage samples %lld | worker idle waits %lld",
		static_cast<long long>(window.queueExecuteStageSamples),
		static_cast<long long>(window.workerWaitCalls));
	middle.Line("dropped frame summaries %ld",
		static_cast<long>(window.droppedFrameSummaries));
	middle.Line("counters with items (ranked by items per frame)");
	middle.Line("%-*.*s %8s/f %7s/f", nameWidth, nameWidth, "counter", "items", "calls");
	// `rows` arrives ranked by total time, but this column is about item counts -
	// bytes, slots, lists, descriptors - so it selects the buckets that carry one
	// and ranks them by it. Without that it would just restate page 2.
	static constexpr UINT kMaxSelected = 64;
	const DX12ProfilingRow *selected[kMaxSelected] = {};
	UINT selectedCount = 0;
	for (UINT i = 0; i < rowCount && selectedCount < kMaxSelected; ++i) {
		if (rows[i].items)
			selected[selectedCount++] = &rows[i];
	}
	std::sort(selected, selected + selectedCount,
		[](const DX12ProfilingRow *lhs, const DX12ProfilingRow *rhs) {
			return lhs->items > rhs->items;
		});
	if (!selectedCount)
		middle.Line("  (this window recorded no item counts)");
	for (UINT i = 0; i < selectedCount && !middle.Full(); ++i) {
		middle.Line("%-*.*s %8lld %7lld", nameWidth, nameWidth,
			selected[i]->name ? selected[i]->name : "?",
			static_cast<long long>(selected[i]->items) / frames,
			static_cast<long long>(selected[i]->calls) / frames);
	}

	right.Line("verification counters (Rm.*, ranked by calls)");
	right.Line("%-*.*s %8s/f %6s", nameWidth, nameWidth, "counter", "calls", "max");
	selectedCount = 0;
	for (UINT i = 0; i < rowCount && selectedCount < kMaxSelected; ++i) {
		if (rows[i].name && StartsWith(rows[i].name, "Rm."))
			selected[selectedCount++] = &rows[i];
	}
	std::sort(selected, selected + selectedCount,
		[](const DX12ProfilingRow *lhs, const DX12ProfilingRow *rhs) {
			return lhs->calls > rhs->calls;
		});
	if (!selectedCount)
		right.Line("  (no Rm.* counter fired in this window)");
	for (UINT i = 0; i < selectedCount && !right.Full(); ++i) {
		char line[192] = {};
		CounterRow(line, sizeof(line), *selected[i], frames, right.maxChars);
		right.Line("%s", line);
	}

	EmitColumns(e, &left, &middle, &right);
}


// Domain of a bucket name: the prefix up to the first dot, which is what the
// section headers on page 2 group by. Names without a dot are plain scopes.
inline const char *DX12ProfilingText::DomainOf(const char *name)
{
	if (!name)
		return "(scope)";
	const char *dot = strchr(name, '.');
	if (!dot || dot == name)
		return "(scope)";
	return name;
}

inline size_t DX12ProfilingText::Format(
	char *out, size_t capacity, DX12ProfilingHud::Mode mode,
	const DX12ProfilingWindow &window,
	const DX12ProfilingRow *rows, UINT rowCount)
{
	if (!out || capacity < 64)
		return 0;
	out[0] = '\0';

	// The render thread measures the screen it draws on; formatting against a
	// guessed resolution is what put three columns on top of each other on a
	// 1280x720 window and pushed a third of the rows below the bottom edge.
	const DX12ProfilingBudget budget = DX12ProfilingHud::GetBudget();

	// Rows each column may carry: the screen's budget, further bounded by the
	// report's character ceiling, so a dense page asks for fewer rows instead of
	// having the overlay drop the glyphs its ring cannot hold.
	Page page;
	page.budget = budget;
	int perRowChars = Layout::kColumnCount; // one marker byte per segment
	for (int column = 0; column < Layout::kColumnCount; ++column)
		perRowChars += page.ColumnChars(column);
	const size_t titleChars = static_cast<size_t>(Layout::kTitleLines) *
		static_cast<size_t>(budget.lineChars);
	const size_t columnsChars = Layout::kMaxReportChars > titleChars
		? Layout::kMaxReportChars - titleChars : 0;
	const int rowsByChars = static_cast<int>(
		columnsChars / static_cast<size_t>(perRowChars > 0 ? perRowChars : 1));
	int rowCap = budget.rows;
	if (rowCap > rowsByChars)
		rowCap = rowsByChars;
	if (rowCap < 1)
		rowCap = 1;
	page.rows = rowCap;

	// One report is one published block, and the ceiling is what keeps it inside
	// the overlay's glyph ring. It is a hard cap on the bytes, not on the text:
	// page.rows above already made every page fit it.
	if (capacity > Layout::kMaxReportChars + 1)
		capacity = Layout::kMaxReportChars + 1;
	Emitter e{ out, capacity, 0, budget.lineChars };

	switch (mode) {
	case DX12ProfilingHud::Mode::Overview:
		FormatOverview(e, page, window, rows, rowCount);
		break;
	case DX12ProfilingHud::Mode::Buckets:
		FormatBuckets(e, page, window, rows, rowCount);
		break;
	case DX12ProfilingHud::Mode::Counters:
		FormatCounters(e, page, window, rows, rowCount);
		break;
	default:
		return 0;
	}
	return e.used;
}