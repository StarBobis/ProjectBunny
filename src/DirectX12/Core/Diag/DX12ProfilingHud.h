#pragma once

// ============================================================================
// DX12ProfilingHud - the bridge between the profiler's aggregation pass and
// the on-screen text overlay, plus the mode/freeze/config state both sides
// read.
//
// The profiler records into per-thread bucket tables while the game runs and
// the writer thread drains, merges and *formats* them once per interval. None
// of that may happen on the render thread, so the finished report is published
// here as one immutable text block and the render thread only ever copies it
// out.
//
// Render-thread cost:
//   - report unchanged (the common case, ~59 frames in 60): one acquire load
//     and a compare. No lock, no allocation, no formatting.
//   - report changed: a shared SRW acquisition and one memcpy of at most
//     kTextBytes. The writer publishes at most once per interval, so the lock
//     is never contended and the worst case is bounded - which is why a plain
//     lock beats a seqlock here, whose retry loop would put an unbounded worst
//     case on the render thread.
//
// This header deliberately depends on nothing but Windows and <atomic> so the
// profiler header can include it without a cycle: the mode it stores *is* the
// sampling gate (see DX12DiagProf::Enabled), and the pump-thread start that a
// mode change implies is done by DX12ProfilingSetMode in Core/Diag/DX12Diag.h,
// which has both halves in scope.
// ============================================================================

#include <Windows.h>

#include <atomic>
#include <stddef.h>
#include <string.h>

// ============================================================================
// Build stamp.
//
// __DATE__ is "Mmm dd yyyy" (the day is space padded) and __TIME__ is
// "hh:mm:ss"; the HUD prints "yyyy-mm-dd hh:mm:ss". It is compiled per
// translation unit, so it identifies the DLL a screenshot came from rather
// than the moment the game started - which is the only thing that makes a
// screenshot actionable ("is this the build with the fix?").
// ============================================================================
struct DX12BuildStamp
{
	static constexpr int kTextBytes = 20; // "2026-09-12 08:47:22" + NUL
	static constexpr const char *kMonths[12] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
	};

	char text[kTextBytes] = {};

	constexpr DX12BuildStamp()
	{
		const int day = (__DATE__[4] == ' ' ? 0 : __DATE__[4] - '0') * 10 +
			(__DATE__[5] - '0');
		int month = 1;
		for (int i = 0; i < 12; ++i) {
			if (__DATE__[0] == kMonths[i][0] &&
				__DATE__[1] == kMonths[i][1] &&
				__DATE__[2] == kMonths[i][2]) {
				month = i + 1;
				break;
			}
		}
		const int year = Digit(__DATE__[7]) * 1000 + Digit(__DATE__[8]) * 100 +
			Digit(__DATE__[9]) * 10 + Digit(__DATE__[10]);
		Put(0, year, 4);
		text[4] = '-';
		Put(5, month, 2);
		text[7] = '-';
		Put(8, day, 2);
		text[10] = ' ';
		Put(11, Digit(__TIME__[0]) * 10 + Digit(__TIME__[1]), 2);
		text[13] = ':';
		Put(14, Digit(__TIME__[3]) * 10 + Digit(__TIME__[4]), 2);
		text[16] = ':';
		Put(17, Digit(__TIME__[6]) * 10 + Digit(__TIME__[7]), 2);
		text[19] = '\0';
	}

	static constexpr int Digit(char c)
	{
		return c >= '0' && c <= '9' ? c - '0' : 0;
	}

	constexpr void Put(int at, int value, int digits)
	{
		for (int i = digits - 1; i >= 0; --i) {
			text[at + i] = static_cast<char>('0' + value % 10);
			value /= 10;
		}
	}
};

inline constexpr DX12BuildStamp kDX12BuildStamp = {};

// ============================================================================
// HUD LAYOUT - every knob of the on-screen report lives here.
//
// Tune these and nothing else:
//   kHudFontScale  - the one font knob. The glyph atlas is rasterized once at
//                    16px (DX12NativeOverlay's BuildFontAtlas) and the HUD
//                    draws its quads at this fraction of a cell, so 0.80 means
//                    ~12.8px tall text with 6.4px wide cells. Lower it for more
//                    rows per screen (each 0.05 is worth ~4 rows at 1080p);
//                    raise it towards 1.0 if the downscaled text looks too
//                    soft, and lower the atlas height instead if you want both
//                    crisp text and many rows.
//   kHudRowGapPx   - extra blank pixels between rows, on top of the cell.
//   kColumnX       - left edge of each column as a fraction of the usable
//                    width, so the layout is resolution independent: 1080p and
//                    1440p differ only in how many characters fit per column.
//   kTopN          - rows in the right column's spike table.
//   kRefreshFrames - redraw cadence. 1 = every frame; the report itself only
//                    changes once per profiling interval, and the overlay
//                    already skips all geometry work while the text is
//                    unchanged, so raising this saves nothing measurable - it
//                    is here for the case where the HUD's own cost shows up.
//
// The row budget is derived from kHudFontScale so the report cannot grow past
// a 1080p screen: raising the font lowers the row count automatically.
// ============================================================================
struct DX12ProfilingLayout
{
	static constexpr float kHudFontScale = 0.80f;
	static constexpr float kHudRowGapPx = 1.4f;
	// The atlas cell in BuildFontAtlas, plus a pixel of external leading: the
	// budget below assumes the taller of the two GDI metrics so it can only
	// under-count the rows a screen holds, never over-count them.
	static constexpr float kBaseCellHeightPx = 17.0f;
	// Row pitch on screen, and the smallest screen the layout is budgeted for.
	static constexpr float kHudRowHeightPx =
		kBaseCellHeightPx * kHudFontScale + kHudRowGapPx;
	static constexpr float kBudgetScreenHeightPx = 1080.0f;
	static constexpr float kScreenMarginPx = 8.0f + 4.0f; // margin + panel padding

	static constexpr int kColumnCount = 3;
	static constexpr float kColumnX[kColumnCount] = { 0.0f, 0.35f, 0.66f };

	static constexpr int kTopN = 12;
	static constexpr int kRefreshFrames = 1;
	// Full-width lines above the three columns: identity, the two totals, the
	// frame cross-check, and the two legend lines. kTimeTableMaxRows is
	// derived from this, so the report's total row count does not change when
	// a line is added here - the left (elastic) table just gets one row less.
	static constexpr int kTitleLines = 5;
	static constexpr int kPreSkinFailureRows = 6;

	// Column text budget. The narrowest column is the right one: at 1080p
	// (1920px) it starts at 0.66 of the usable width and a cell is
	// 8*0.80 = 6.4px, so ~100 characters fit. The budget is a quarter lower so
	// a 1600px-wide screen, or a larger kHudFontScale, still cannot make two
	// columns touch.
	static constexpr int kMaxColumnChars = 76;
	// Whole-report byte budget: every column's bytes together, since the report
	// is one block. The overlay's vertex ring holds one quad per character, so
	// this stays clear of its kMaxChars and leaves it room for the hunting text
	// drawn in the same frame.
	static constexpr size_t kMaxReportChars = 11500;

	// Rows each column may carry. The left (time) table is the only elastic
	// one, so it gets the whole screen budget - raise kHudFontScale and this
	// falls automatically instead of running off the bottom of the screen. The
	// middle and right columns have a fixed content list; their caps sit just
	// above it and exist so a future row added there cannot push the report
	// past the byte budget.
	static constexpr int kRowsOnBudgetScreenPx = static_cast<int>(
		(kBudgetScreenHeightPx - 2.0f * kScreenMarginPx) / kHudRowHeightPx);
	static constexpr int kTimeTableMaxRows = kRowsOnBudgetScreenPx - kTitleLines;
	static constexpr int kRatioColumnMaxRows = 56;
	static constexpr int kFrameColumnMaxRows = 28;
	static constexpr int kMaxReportLines = kTitleLines + kTimeTableMaxRows;

	// Column tag. A report line is one screen row; each of its up to three
	// column segments is introduced by (kColumnMark + index). The report is
	// plain ASCII otherwise (the glyph atlas covers 32..126 only), so a control
	// byte can never collide with a glyph and a segment can be placed without
	// parsing the words in it. A row with no marker at all is a full-width
	// title line.
	static constexpr unsigned char kColumnMark = 1;
};

class DX12ProfilingHud
{
public:
	enum class Mode : LONG
	{
		Off = 0,
		Snapshot,    // the full-screen three-column view (see DX12ProfilingText)
		Compact,     // fps/frame/present/queue/HUD self-cost plus the top stages
		Summary,     // a fixed set of Present./Queue./Stage./Wait. fields
		Top,         // every bucket ranked by total time
		Sections,    // the same buckets grouped by domain prefix
		Counters,    // item-only buckets and the per-frame draw/dispatch counts

		ModeCount,
	};

	static constexpr size_t kTextBytes = 16 * 1024;
	static constexpr int kMaxTopRows = 160;
	// dx12_profiling_interval_ms. This is now the *fallback* refresh interval,
	// not the length of a window: the window is counted in frames (see
	// kWindowFrames) and this value only decides how long a session running too
	// slowly to reach that frame count may go before it is reported anyway. Its
	// default and its clamp range are unchanged, so an existing d3dx.ini keeps
	// working - it asks for a report every 500ms and gets one per window
	// instead. The value can only make a window *longer* (see
	// WindowFallbackUs), never shorter than kWindowFrames frames.
	static constexpr LONG kDefaultIntervalMs = 500;
	static constexpr LONG kMinIntervalMs = 100;
	static constexpr LONG kMaxIntervalMs = 10000;
	static constexpr int kDefaultTopRows = 24;

	// --- aggregation window --------------------------------------------------
	// One report covers exactly this many presented frames. Counting frames
	// instead of milliseconds is what makes a report comparable with the one
	// before it and with a log line from another session: a wall-clock window
	// held 15 frames at 30fps and 72 at 144fps, and the maximum of a 6-frame
	// sample is a different statistic from the maximum of a 72-frame one, so
	// every `max` (and every window total) moved with the frame rate rather
	// than with the code being measured. At 500ms the HUD's own `max` columns
	// were the noisiest thing on screen - the exact opposite of what they are
	// for.
	//
	// The fallback below only exists so a session that renders at a few fps
	// still refreshes: a window is reported once it has this many frames, or -
	// if that takes longer than kWindowFrames frames at kWindowFallbackFps -
	// once it has at least kWindowMinFrames frames. That floor is what keeps
	// the fallback from degenerating back into the 15-frame window it replaced.
	static constexpr LONG kWindowFrames = 60;
	static constexpr LONG kWindowMinFrames = kWindowFrames / 2;
	static constexpr LONG kWindowFallbackFps = 20;

	// Longest a window may last before it is reported regardless of its frame
	// count: the target frame count at kWindowFallbackFps (3s for 60 frames),
	// or the configured dx12_profiling_interval_ms when that asks for less
	// frequent reports. It is never shorter than the target, so the configured
	// interval can no longer shrink a window below kWindowFrames frames.
	static LONG64 WindowFallbackUs()
	{
		const LONG64 atFallbackFps = static_cast<LONG64>(kWindowFrames) * 1000000 /
			kWindowFallbackFps;
		const LONG64 configured = static_cast<LONG64>(IntervalMs()) * 1000;
		return configured > atFallbackFps ? configured : atFallbackFps;
	}

	// --- state ---------------------------------------------------------------
	static Mode GetMode()
	{
		return static_cast<Mode>(sMode.load(std::memory_order_relaxed));
	}

	static bool IsEnabled()
	{
		return sMode.load(std::memory_order_relaxed) !=
			static_cast<LONG>(Mode::Off);
	}

	static bool IsFrozen()
	{
		return sFrozen.load(std::memory_order_relaxed) != 0;
	}

	// Fallback window duration in milliseconds (the old "report every N ms"
	// knob). See WindowFallbackUs for how it is combined with kWindowFrames.
	static LONG IntervalMs()
	{
		return sIntervalMs.load(std::memory_order_relaxed);
	}

	static int TopRows()
	{
		return sTopRows.load(std::memory_order_relaxed);
	}

	// Whether the bulk "name,calls,totalUs,avgUs,maxUs,items" table is still
	// written to the log. The on-screen HUD shows the same numbers, so once it
	// is on the table is redundant log bulk - but the per-domain troubleshooting
	// sections (frame timeline, api wall, hot path, create, cpu stage, lock,
	// counters) are unaffected and keep flowing.
	static bool LogBucketTable()
	{
		return sLogBucketTable.load(std::memory_order_relaxed) != 0;
	}

	static void SetMode(Mode mode)
	{
		const LONG raw = static_cast<LONG>(mode);
		sMode.store(raw >= 0 && raw < static_cast<LONG>(Mode::ModeCount) ? raw : 0,
			std::memory_order_relaxed);
	}

	static void CycleMode()
	{
		const LONG next = (sMode.load(std::memory_order_relaxed) + 1) %
			static_cast<LONG>(Mode::ModeCount);
		sMode.store(next, std::memory_order_relaxed);
	}

	// Freezing keeps the report the screen is showing. The profiler keeps
	// sampling and the log keeps flowing; only the display stops changing, so
	// the numbers under the cursor do not move while they are being read.
	static void ToggleFreeze()
	{
		sFrozen.store(sFrozen.load(std::memory_order_relaxed) ? 0 : 1,
			std::memory_order_relaxed);
	}

	// `intervalMs` keeps the dx12_profiling_interval_ms meaning it always had -
	// "I want a report at least this often" - but it can only lengthen a
	// window, never shorten it below kWindowFrames frames.
	static void ApplyConfig(Mode mode, LONG intervalMs, int topRows, bool logBucketTable)
	{
		SetMode(mode);
		if (intervalMs < kMinIntervalMs)
			intervalMs = kMinIntervalMs;
		if (intervalMs > kMaxIntervalMs)
			intervalMs = kMaxIntervalMs;
		sIntervalMs.store(intervalMs, std::memory_order_relaxed);
		if (topRows < 1)
			topRows = 1;
		if (topRows > kMaxTopRows)
			topRows = kMaxTopRows;
		sTopRows.store(topRows, std::memory_order_relaxed);
		sLogBucketTable.store(logBucketTable ? 1 : 0, std::memory_order_relaxed);
	}

	static const char *ModeName(Mode mode)
	{
		switch (mode) {
		case Mode::Snapshot: return "Snapshot";
		case Mode::Compact:  return "Compact";
		case Mode::Summary:  return "Summary";
		case Mode::Top:      return "Top";
		case Mode::Sections: return "Sections";
		case Mode::Counters: return "Counters";
		default:             return "Off";
		}
	}

	// --- hot-path flags mirror ----------------------------------------------
	// The header line must state which work the hooks are doing, which is the
	// DX12HotPathState::HookWorkFlags word. That header cannot be included from
	// here (DX12HotPathState pulls in DX12Diag, which includes this file), so
	// the word is mirrored on every publish instead - one relaxed store on a
	// path that already stores four atomics and only runs when the state
	// actually changed. Nothing reads this for control flow: it is display
	// data, and the profiler's own gates keep using the real word.
	static UINT64 HookWorkFlags()
	{
		return sHookWorkFlags.load(std::memory_order_relaxed);
	}

	static void PublishHookWorkFlags(UINT64 flags)
	{
		sHookWorkFlags.store(flags, std::memory_order_relaxed);
	}

	// --- report transport ----------------------------------------------------
	// Writer thread only. A frozen HUD keeps whatever the screen already shows.
	static void Publish(const char *text, size_t length)
	{
		if (!text || !length || IsFrozen())
			return;
		if (length >= kTextBytes)
			length = kTextBytes - 1;

		AcquireSRWLockExclusive(&sLock);
		memcpy(sText, text, length);
		sText[length] = '\0';
		sLength = length;
		// The version must move *after* the bytes, otherwise a consumer that
		// observes the new version could copy the previous report.
		InterlockedIncrement64(&sVersion);
		ReleaseSRWLockExclusive(&sLock);
	}

	// Render thread only. Returns false without touching the lock while the
	// published report is the one the caller already holds.
	static bool AcquireLatest(
		UINT64 *version, char *out, size_t outCapacity, size_t *outLength)
	{
		if (!version || !out || !outCapacity)
			return false;
		const LONG64 published = InterlockedCompareExchange64(&sVersion, 0, 0);
		if (published <= 0 || static_cast<UINT64>(published) == *version)
			return false;

		AcquireSRWLockShared(&sLock);
		// Holding the shared lock means the writer cannot be mid-copy, so the
		// length and the bytes are from the same report. The version stamped
		// on the caller is whatever the buffer currently holds, which is what
		// was just copied.
		size_t copy = sLength;
		if (copy >= outCapacity)
			copy = 0;
		if (copy) {
			memcpy(out, sText, copy);
			out[copy] = '\0';
			*outLength = copy;
			*version = static_cast<UINT64>(
				InterlockedCompareExchange64(&sVersion, 0, 0));
		}
		ReleaseSRWLockShared(&sLock);
		return copy != 0;
	}

	static UINT64 CurrentVersion()
	{
		const LONG64 value = InterlockedCompareExchange64(&sVersion, 0, 0);
		return value > 0 ? static_cast<UINT64>(value) : 0;
	}

private:
	inline static SRWLOCK sLock = SRWLOCK_INIT;
	inline static char sText[kTextBytes] = {};
	inline static size_t sLength = 0;
	inline static volatile LONG64 sVersion = 0;
	inline static std::atomic<LONG> sMode{ static_cast<LONG>(Mode::Off) };
	inline static std::atomic<LONG> sFrozen{ 0 };
	inline static std::atomic<LONG> sIntervalMs{ kDefaultIntervalMs };
	inline static std::atomic<LONG> sTopRows{ kDefaultTopRows };
	// Default on: the whole point of the change is that the bulk table moves to
	// the screen. Troubleshooting sections are not affected either way.
	inline static std::atomic<LONG> sLogBucketTable{ 0 };
	inline static std::atomic<UINT64> sHookWorkFlags{ 0 };
};
