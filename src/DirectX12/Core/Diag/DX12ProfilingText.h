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
	// Identify the frame the window closed on; the log's timeline line carries
	// them, the on-screen report does not.
	const char *api = nullptr;
	void *swapChain = nullptr;
	LONG hr = 0;

	LONG frames = 0;
	LONG present = 0;
	LONG frameDraws = 0;
	LONG frameDispatches = 0;
	LONG droppedFrameSummaries = 0;

	LONG64 frameIntervalUs = 0;
	LONG64 frameWallUs = 0;
	LONG64 outsidePresentUs = 0;
	LONG64 presentWallUs = 0;
	LONG64 maxFrameWallUs = 0;

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
};

class DX12ProfilingText
{
public:
	// Total glyph budget for one report. The overlay's per-frame vertex ring
	// holds more than this, but a report that long would be unreadable and
	// would make the one-frame layout hiccup noticeable.
	static constexpr int kMaxReportLines = 44;
	static constexpr size_t kMaxReportChars = 2800;
	static constexpr int kRowNameWidth = 30;

	// Returns the number of bytes written (excluding the terminator). The
	// result always fits `capacity` and is always NUL-terminated.
	static size_t Format(
		char *out, size_t capacity, DX12ProfilingHud::Mode mode,
		const DX12ProfilingWindow &window,
		const DX12ProfilingRow *rows, UINT rowCount);

private:
	struct Emitter
	{
		char *out;
		size_t capacity;
		size_t used;

		void Line(const char *fmt, ...);
		void Append(const char *text, size_t length);
		bool Full() const { return used + 2 >= capacity; }
	};

	static void FormatCompact(
		Emitter &e, const DX12ProfilingWindow &window,
		const DX12ProfilingRow *ranked, UINT rankedCount);
	static void FormatSummary(
		Emitter &e, const DX12ProfilingWindow &window,
		const DX12ProfilingRow *ranked, UINT rankedCount);
	static void FormatTop(
		Emitter &e, const DX12ProfilingWindow &window,
		const DX12ProfilingRow *ranked, UINT rankedCount);
	static void FormatSections(
		Emitter &e, const DX12ProfilingWindow &window,
		const DX12ProfilingRow *ranked, UINT rankedCount);
	static void FormatCounters(
		Emitter &e, const DX12ProfilingWindow &window,
		const DX12ProfilingRow *ranked, UINT rankedCount);

	static void Header(Emitter &e, const DX12ProfilingWindow &window, const char *title);
	static void RowLine(Emitter &e, const DX12ProfilingRow &row);
	static void RowPair(Emitter &e, const DX12ProfilingRow &left, const DX12ProfilingRow &right);
	static const char *DomainOf(const char *name);
};

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
	if (written > 0)
		Append(buffer, static_cast<size_t>(written));
}

inline void DX12ProfilingText::Header(
	Emitter &e, const DX12ProfilingWindow &window, const char *title)
{
	e.Line("DX12 Profiling %s  %.1ffps  frame %.2fms  draws %ld  present %.0fus\n",
		title, window.Fps(), window.FrameMs(),
		static_cast<long>(window.frameDraws),
		static_cast<double>(window.presentWallUs) /
			(window.frames > 0 ? window.frames : 1));
}

inline void DX12ProfilingText::RowLine(Emitter &e, const DX12ProfilingRow &row)
{
	e.Line("  %-*.*s %8lldus  x%-6lld max %lldus\n",
		kRowNameWidth, kRowNameWidth,
		row.name ? row.name : "?",
		static_cast<long long>(row.totalUs),
		static_cast<long long>(row.calls),
		static_cast<long long>(row.maxUs));
}

// Two ranked rows on one line. Compact mode uses this so the whole report
// stays under a dozen lines without dropping the interesting half.
inline void DX12ProfilingText::RowPair(
	Emitter &e, const DX12ProfilingRow &left, const DX12ProfilingRow &right)
{
	if (!right.name) {
		RowLine(e, left);
		return;
	}
	e.Line("  %-*.*s %7lldus | %-*.*s %7lldus\n",
		kRowNameWidth - 4, kRowNameWidth - 4,
		left.name ? left.name : "?",
		static_cast<long long>(left.totalUs),
		kRowNameWidth - 4, kRowNameWidth - 4,
		right.name ? right.name : "?",
		static_cast<long long>(right.totalUs));
}

// The bucket-name domain is everything before the first '.', which is the
// convention every prefix in this codebase follows ("Present.", "Queue.",
// "Stage.", "Api.", "Create.", "Lock.", "Wait.", "Fence.", "Worker.", "Desc.",
// "Rm."). Names without a dot are plain scopes and group under "(scope)".
inline const char *DX12ProfilingText::DomainOf(const char *name)
{
	if (!name)
		return "(scope)";
	const char *dot = strchr(name, '.');
	if (!dot || dot == name)
		return "(scope)";
	return name;
}

inline void DX12ProfilingText::FormatCompact(
	Emitter &e, const DX12ProfilingWindow &window,
	const DX12ProfilingRow *ranked, UINT rankedCount)
{
	Header(e, window, "Compact");
	e.Line("present %.0fus | queue %.0fus | hud %.0fus | waits %.0fus | fences %.0fus\n",
		static_cast<double>(window.presentWallUs) /
			(window.frames > 0 ? window.frames : 1),
		static_cast<double>(window.queueEstimatedTotalUs) /
			(window.frames > 0 ? window.frames : 1),
		static_cast<double>(window.presentOverlayTextUs) /
			(window.frames > 0 ? window.frames : 1),
		static_cast<double>(window.systemWaitTotalUs + window.eventWaitTotalUs) /
			(window.frames > 0 ? window.frames : 1),
		static_cast<double>(window.fenceTotalUs) /
			(window.frames > 0 ? window.frames : 1));
	e.Line("outside-present %.2fms | wall %.2fms | max %.2fms | dispatches %ld\n",
		static_cast<double>(window.outsidePresentUs) /
			(window.frames > 0 ? window.frames : 1) / 1000.0,
		static_cast<double>(window.frameWallUs) /
			(window.frames > 0 ? window.frames : 1) / 1000.0,
		static_cast<double>(window.maxFrameWallUs) / 1000.0,
		static_cast<long>(window.frameDispatches));

	const UINT shown = rankedCount < 12 ? rankedCount : 12;
	for (UINT i = 0; i + 1 < shown; i += 2)
		RowPair(e, ranked[i], ranked[i + 1]);
	if (shown & 1)
		RowLine(e, ranked[shown - 1]);
	if (rankedCount > shown)
		e.Line("  ... %u more rows (Top view lists them)\n", rankedCount - shown);
}

inline void DX12ProfilingText::FormatSummary(
	Emitter &e, const DX12ProfilingWindow &window,
	const DX12ProfilingRow *ranked, UINT rankedCount)
{
	(void)ranked;
	(void)rankedCount;
	const LONG frames = window.frames > 0 ? window.frames : 1;
	const double inv = 1.0 / static_cast<double>(frames);
	Header(e, window, "Summary");
	e.Line("per-frame averages over %ld frames\n", static_cast<long>(window.frames));
	e.Line("  present    total %7.1fus | original %7.1fus | modBegin %6.1fus\n",
		static_cast<double>(window.presentWallUs) * inv,
		static_cast<double>(window.presentOriginalUs) * inv,
		static_cast<double>(window.presentModBeginFrameUs) * inv);
	e.Line("             overlay %7.1fus | hud %7.1fus | capture %6.1fus\n",
		static_cast<double>(window.presentOtherUs) * inv,
		static_cast<double>(window.presentOverlayTextUs) * inv,
		static_cast<double>(window.presentCaptureBookkeepingUs) * inv);
	e.Line("             uiAct %6.1fus | counters %6.1fus | postRun %6.1fus\n",
		static_cast<double>(window.presentConsumeUiActionsUs) * inv,
		static_cast<double>(window.presentFrameCountersUs) * inv,
		static_cast<double>(window.presentPostOriginalRuntimeUs) * inv);
	e.Line("             unexplained %6.1fus\n",
		static_cast<double>(window.presentWallMinusKnownUs) * inv);
	e.Line("  queue      total %7.1fus | calls %5lld | lists %5lld\n",
		static_cast<double>(window.queueEstimatedTotalUs) * inv,
		static_cast<long long>(window.queueWallCalls) / frames,
		static_cast<long long>(window.queueWallCommandLists) / frames);
	e.Line("             signal %6.1fus | wait %6.1fus | original %6.1fus\n",
		static_cast<double>(window.queueSignalUs) * inv,
		static_cast<double>(window.queueWaitUs) * inv,
		static_cast<double>(window.queueOriginalUs) * inv);
	e.Line("  fences     %5lld calls %6.1fus | eventWait %5lld %6.1fus\n",
		static_cast<long long>(window.fenceCalls) / frames,
		static_cast<double>(window.fenceTotalUs) * inv,
		static_cast<long long>(window.eventWaitCalls) / frames,
		static_cast<double>(window.eventWaitTotalUs) * inv);
	e.Line("  sys waits  %5lld calls %6.1fus\n",
		static_cast<long long>(window.systemWaitCalls) / frames,
		static_cast<double>(window.systemWaitTotalUs) * inv);
	e.Line("  worker     tasks %5lld %6.1fus | idle %6.1fus | drain %6.1fus\n",
		static_cast<long long>(window.workerTaskCalls) / frames,
		static_cast<double>(window.workerTaskUs) * inv,
		static_cast<double>(window.workerIdleWaitUs) * inv,
		static_cast<double>(window.workerDrainWaitUs) * inv);
	e.Line("  frame      wall %7.2fms | outside %7.2fms | max %6.2fms\n",
		static_cast<double>(window.frameWallUs) * inv / 1000.0,
		static_cast<double>(window.outsidePresentUs) * inv / 1000.0,
		static_cast<double>(window.maxFrameWallUs) / 1000.0);
	if (window.presentKnownUs > window.frameWallUs)
		e.Line("  note: present slices exceed frame wall (counter overlap)\n");
	e.Line("  timing     %5.1fus/frame (this report is not free)\n",
		static_cast<double>(window.presentProfilingFlushUs +
			window.presentFrameCountersUs) * inv);
}

inline void DX12ProfilingText::FormatTop(
	Emitter &e, const DX12ProfilingWindow &window,
	const DX12ProfilingRow *ranked, UINT rankedCount)
{
	Header(e, window, "Top");
	e.Line("  %-*.*s %9s %8s %9s\n", kRowNameWidth, kRowNameWidth,
		"bucket", "totalUs", "calls", "maxUs");
	const int top = DX12ProfilingHud::TopRows();
	const UINT shown = rankedCount < static_cast<UINT>(top)
		? rankedCount : static_cast<UINT>(top);
	for (UINT i = 0; i < shown; ++i)
		RowLine(e, ranked[i]);
	if (rankedCount > shown)
		e.Line("  ... %u more rows\n", rankedCount - shown);
}

inline void DX12ProfilingText::FormatSections(
	Emitter &e, const DX12ProfilingWindow &window,
	const DX12ProfilingRow *ranked, UINT rankedCount)
{
	Header(e, window, "Sections");
	// Ranked is already sorted by total time, so a per-domain pass naturally
	// lists each domain's most expensive buckets first without a second sort.
	static const char *const kDomains[] = {
		"Present.", "Queue.", "Stage.", "Api.", "Create.",
		"Lock.", "Wait.", "Fence.", "Worker.", "Desc.", "Rm.",
	};
	const int perDomain = DX12ProfilingHud::TopRows() / 4 + 2;
	int linesLeft = kMaxReportLines - 4;

	for (const char *domain : kDomains) {
		if (linesLeft <= 1)
			break;
		const size_t length = strlen(domain);
		int shown = 0;
		for (UINT i = 0; i < rankedCount && shown < perDomain; ++i) {
			const char *name = ranked[i].name;
			if (!name || strncmp(name, domain, length) != 0)
				continue;
			if (!shown)
				e.Line("%s\n", domain);
			--linesLeft;
			++shown;
			RowLine(e, ranked[i]);
			if (linesLeft <= 1)
				break;
		}
	}

	// Everything without a known prefix, which is where the plain scopes live.
	int shown = 0;
	for (UINT i = 0; i < rankedCount && shown < perDomain && linesLeft > 1; ++i) {
		if (strcmp(DomainOf(ranked[i].name), "(scope)") != 0)
			continue;
		if (!shown)
			e.Line("(scope)\n");
		--linesLeft;
		++shown;
		RowLine(e, ranked[i]);
	}
}

inline void DX12ProfilingText::FormatCounters(
	Emitter &e, const DX12ProfilingWindow &window,
	const DX12ProfilingRow *ranked, UINT rankedCount)
{
	Header(e, window, "Counters");
	const LONG frames = window.frames > 0 ? window.frames : 1;
	e.Line("  draws %ld/frame | dispatches %ld/frame | dropped summaries %ld\n",
		static_cast<long>(window.frameDraws), static_cast<long>(window.frameDispatches),
		static_cast<long>(window.droppedFrameSummaries));
	e.Line("  present enter %lld | leave %lld | queue lists %lld | queue calls %lld\n",
		static_cast<long long>(window.presentEnterCalls),
		static_cast<long long>(window.presentLeaveCalls),
		static_cast<long long>(window.queueWallCommandLists),
		static_cast<long long>(window.queueWallCalls));
	e.Line("  queue stage samples %lld | worker tasks %lld | idle waits %lld\n",
		static_cast<long long>(window.queueExecuteStageSamples),
		static_cast<long long>(window.workerTaskCalls),
		static_cast<long long>(window.workerWaitCalls));
	e.Line("  fence calls %lld | event waits %lld | system waits %lld\n",
		static_cast<long long>(window.fenceCalls),
		static_cast<long long>(window.eventWaitCalls),
		static_cast<long long>(window.systemWaitCalls));
	e.Line("  %-*.*s %9s %8s %9s\n", kRowNameWidth, kRowNameWidth,
		"counter (items != 0)", "items", "calls", "maxUs");

	// `ranked` arrives ordered by total time, but this view is about item
	// counts - slots, bytes, lists, descriptors - so it selects the buckets
	// that actually carry one and ranks them by it. Without that it would just
	// restate the Top view.
	static constexpr UINT kMaxCounters = 64;
	const DX12ProfilingRow *selected[kMaxCounters] = {};
	UINT selectedCount = 0;
	for (UINT i = 0; i < rankedCount && selectedCount < kMaxCounters; ++i) {
		if (ranked[i].items)
			selected[selectedCount++] = &ranked[i];
	}
	std::sort(selected, selected + selectedCount,
		[](const DX12ProfilingRow *lhs, const DX12ProfilingRow *rhs) {
			return lhs->items > rhs->items;
		});

	int linesLeft = kMaxReportLines - 7;
	for (UINT i = 0; i < selectedCount && linesLeft > 0; ++i) {
		e.Line("  %-*.*s %9lld %8lld %9lld\n", kRowNameWidth, kRowNameWidth,
			selected[i]->name ? selected[i]->name : "?",
			static_cast<long long>(selected[i]->items) / frames,
			static_cast<long long>(selected[i]->calls) / frames,
			static_cast<long long>(selected[i]->maxUs));
		--linesLeft;
	}
	if (!selectedCount)
		e.Line("  (this window recorded no item counts)\n");
}

inline size_t DX12ProfilingText::Format(
	char *out, size_t capacity, DX12ProfilingHud::Mode mode,
	const DX12ProfilingWindow &window,
	const DX12ProfilingRow *rows, UINT rowCount)
{
	if (!out || capacity < 64)
		return 0;
	out[0] = '\0';
	Emitter e{ out, capacity, 0 };

	// `rows` arrives ranked by total time, which is the order every view but
	// Counters wants; Counters re-ranks by item count because that is what it
	// is about.
	const DX12ProfilingRow *ranked = rows;
	UINT rankedCount = rowCount;

	switch (mode) {
	case DX12ProfilingHud::Mode::Compact:
		FormatCompact(e, window, ranked, rankedCount);
		break;
	case DX12ProfilingHud::Mode::Summary:
		FormatSummary(e, window, ranked, rankedCount);
		break;
	case DX12ProfilingHud::Mode::Top:
		FormatTop(e, window, ranked, rankedCount);
		break;
	case DX12ProfilingHud::Mode::Sections:
		FormatSections(e, window, ranked, rankedCount);
		break;
	case DX12ProfilingHud::Mode::Counters:
		FormatCounters(e, window, ranked, rankedCount);
		break;
	default:
		return 0;
	}
	return e.used;
}
