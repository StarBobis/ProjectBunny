#pragma once

// DX12Profiler scopes/counters and DX12_PROFILE_* / DX12_HOTPATH_LOG_* macros.
// Split out of DX12Hooks.h; include that umbrella header from existing code.

#include "../DX12Core.h"

#include "Nektra/NktHookLib.h"
#include "util_min.h"
#include "crc32c.h"

#include <d3dcompiler.h>

#include <climits>
#include <condition_variable>
#include <limits>
#include <map>
#include <mutex>
#include <thread>

// ---------------------------------------------------------------------------
// DX12Profiler - single instrumentation engine.
//
// All three profiling styles (DX12Profiling counters, API wall buckets and
// hot-path stages) share one RAII scope and one per-frame flush. Existing
// macro families remain as thin aliases so call sites did not need to change;
// new code should use DX12_PROFILE_SCOPE / DX12_PROFILE_API / DX12_PROFILE_STAGE.
// ---------------------------------------------------------------------------
class DX12Profiler
{
public:
	enum Kind
	{
		Counter = 0,
		Api,
		Stage,
		ApiStage,   // one QPC pair feeding both the API bucket and a hot-path stage
	};

	class Scope
	{
	public:
		Kind kind;
		int apiBucket;
		int stage;
		DX12Profiling::Counter *counter;
		DX12Profiling::State state = {};
		LARGE_INTEGER begin = {};
		bool active;

		Scope(Kind kindValue, DX12Profiling::Counter *counterValue)
			: kind(kindValue), apiBucket(0), stage(0), counter(counterValue),
			  active(true)
		{
			DX12Profiling::Start(&state, counter);
		}

		Scope(Kind kindValue, int index)
			: kind(kindValue), apiBucket(index), stage(index), counter(nullptr),
			  active(true)
		{
			if (kind == Api) {
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
				begin = DX12OuterTimingProbeUtils::Now();
				DX12OuterTimingProbeUtils::LogApiScopeBegin(apiBucket, begin.QuadPart);
#else
				active = false;
#endif
			} else if (kind == Stage) {
#if DX12_HOTPATH_TIME_ENABLED
				DX12HotPathTimingUtils::Begin(static_cast<DX12HotPathStage::Id>(stage));
#else
				active = false;
#endif
			} else {
				active = false;
			}
		}

		Scope(Kind kindValue, int apiValue, int stageValue)
			: kind(kindValue), apiBucket(apiValue), stage(stageValue),
			  counter(nullptr), active(true)
		{
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
#if DX12_HOTPATH_TIME_ENABLED
			begin = DX12OuterTimingProbeUtils::Now();
			DX12OuterTimingProbeUtils::LogApiScopeBegin(apiBucket, begin.QuadPart);
			DX12HotPathTimingUtils::BeginAt(static_cast<DX12HotPathStage::Id>(stage), begin);
#else
			active = false;
#endif
#else
			active = false;
#endif
		}

		~Scope()
		{
			Finish();
		}

		void Pause()
		{
			if (!active || kind != Counter)
				return;
			DX12Profiling::End(&state);
			active = false;
		}

		void Resume()
		{
			if (active || kind != Counter)
				return;
			DX12Profiling::Start(&state, state.counter);
			active = true;
		}

		void Finish()
		{
			if (!active)
				return;
			switch (kind) {
			case Counter:
				DX12Profiling::End(&state);
				break;
			case Api:
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
				{
					const LARGE_INTEGER end = DX12OuterTimingProbeUtils::Now();
					DX12OuterTimingProbeUtils::LogApiScopeEnd(
						apiBucket, begin.QuadPart, end.QuadPart);
				}
#endif
				break;
			case Stage:
#if DX12_HOTPATH_TIME_ENABLED
				DX12HotPathTimingUtils::End(static_cast<DX12HotPathStage::Id>(stage));
#endif
				break;
			case ApiStage:
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
#if DX12_HOTPATH_TIME_ENABLED
				{
					const LARGE_INTEGER end = DX12OuterTimingProbeUtils::Now();
					DX12OuterTimingProbeUtils::LogApiScopeEnd(
						apiBucket, begin.QuadPart, end.QuadPart);
					DX12HotPathTimingUtils::EndAt(
						static_cast<DX12HotPathStage::Id>(stage),
						begin.QuadPart,
						end.QuadPart);
				}
#endif
#endif
				break;
			}
			active = false;
		}
	};

	static void BeginStage(int stage)
	{
#if DX12_HOTPATH_TIME_ENABLED
		DX12HotPathTimingUtils::Begin(static_cast<DX12HotPathStage::Id>(stage));
#else
		(void)stage;
#endif
	}

	static void EndStage(int stage)
	{
#if DX12_HOTPATH_TIME_ENABLED
		DX12HotPathTimingUtils::End(static_cast<DX12HotPathStage::Id>(stage));
#else
		(void)stage;
#endif
	}

	// Single per-frame flush: profiling counters, hot-path stages, create/
	// descriptor/resident telemetry and frame CPU diagnostics. API wall
	// buckets are flushed from LogPresentLeave via LogFrameTimeline.
	static void FlushFrame()
	{
		DX12Profiling::EndFrame();
#if DX12_HOTPATH_TIME_ENABLED
		DX12HotPathTimingUtils::LogAndResetAll();
#endif
	}
};

// Unified instrumentation macros for new code. Existing macro families
// (DX12_PROFILE_FUNCTION / DX12_HOTPATH_TIME_*) remain as compatibility
// aliases and all route through DX12Profiler above.
#define DX12_PROFILE_SCOPE(counter) \
	DX12Profiler::Scope _profile_scope( \
		DX12Profiler::Counter, &DX12Profiling::Get().counter)

#define DX12_PROFILE_API(api) \
	DX12Profiler::Scope _profile_api( \
		DX12Profiler::Api, static_cast<int>(api))

#define DX12_PROFILE_STAGE(stage) \
	DX12Profiler::Scope _profile_stage( \
		DX12Profiler::Stage, static_cast<int>(stage))

#if DX12_HOTPATH_TIME_ENABLED

#define DX12_HOTPATH_TIME_BEGIN(stage) \
	DX12Profiler::BeginStage(static_cast<int>(DX12HotPathStage::stage))

#define DX12_HOTPATH_TIME_END(stage) \
	DX12Profiler::EndStage(static_cast<int>(DX12HotPathStage::stage))

#define DX12_HOTPATH_TIME_SCOPE(stage) \
	DX12Profiler::Scope __hotpathTimer##__LINE__( \
		DX12Profiler::Stage, static_cast<int>(DX12HotPathStage::stage))

#define DX12_HOTPATH_LOG_AND_RESET() \
	DX12HotPathTimingUtils::LogAndResetAll()

#else

#define DX12_HOTPATH_TIME_BEGIN(stage) do {} while (0)
#define DX12_HOTPATH_TIME_END(stage) do {} while (0)
#define DX12_HOTPATH_TIME_SCOPE(stage) do {} while (0)
#define DX12_HOTPATH_LOG_AND_RESET() do {} while (0)

#endif
