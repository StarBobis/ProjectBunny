#pragma once

// DX12Profiler scopes/counters and DX12_PROFILE_* / DX12_HOTPATH_LOG_* macros.
// Split out of DX12Hooks.h; include that umbrella header from existing code.

#include "../DX12Core.h"
#include "../ModRuntime/DX12ModLockTelemetry.h"

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
// DX12Profiler - thin routing shell over DX12Profiling.
//
// All scope kinds funnel into DX12Profiling (named buckets, batched flush);
// Api/ApiStage additionally feed the outer timing probe until that header is
// migrated (docs/DX12ProfilingRefactorPlan.md). Existing macro families
// remain as thin aliases so call sites did not need to change.
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
		const char *name;
		LARGE_INTEGER begin = {};
		bool active;

		Scope(Kind kindValue, const char *scopeName)
			: kind(kindValue), apiBucket(0), stage(0), name(scopeName),
			  active(true)
		{
			if (kind == Counter)
				DX12Profiling::Start(name);
			else
				active = false;
		}

		Scope(Kind kindValue, int index)
			: kind(kindValue), apiBucket(index), stage(index), name(nullptr),
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
				name = DX12HotPathStage::Name(static_cast<DX12HotPathStage::Id>(stage));
				DX12Profiling::Start(name);
			} else {
				active = false;
			}
		}

		Scope(Kind kindValue, int apiValue, int stageValue)
			: kind(kindValue), apiBucket(apiValue), stage(stageValue), active(true)
		{
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
			begin = DX12OuterTimingProbeUtils::Now();
			DX12OuterTimingProbeUtils::LogApiScopeBegin(apiBucket, begin.QuadPart);
			name = DX12HotPathStage::Name(static_cast<DX12HotPathStage::Id>(stage));
			DX12Profiling::Start(name);
#else
			name = nullptr;
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
			DX12Profiling::End(name);
			active = false;
		}

		void Resume()
		{
			if (active || kind != Counter)
				return;
			DX12Profiling::Start(name);
			active = true;
		}

		void Finish()
		{
			if (!active)
				return;
			switch (kind) {
			case Counter:
				DX12Profiling::End(name);
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
				DX12Profiling::End(name);
				break;
			case ApiStage:
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
				{
					const LARGE_INTEGER end = DX12OuterTimingProbeUtils::Now();
					DX12OuterTimingProbeUtils::LogApiScopeEnd(
						apiBucket, begin.QuadPart, end.QuadPart);
				}
				DX12Profiling::End(name);
#endif
				break;
			}
			active = false;
		}
	};

	static void BeginStage(int stage)
	{
		DX12Profiling::Start(DX12HotPathStage::Name(static_cast<DX12HotPathStage::Id>(stage)));
	}

	static void EndStage(int stage)
	{
		DX12Profiling::End(DX12HotPathStage::Name(static_cast<DX12HotPathStage::Id>(stage)));
	}

	// Single per-frame flush: batched profiling block plus the legacy
	// telemetry logs (until stage 4 of the refactor migrates them).
	static void FlushFrame()
	{
		DX12Profiling::FlushBatch();
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		DX12CreateTimingTelemetryUtils::LogAndReset();
		DX12DescriptorShadowTelemetryUtils::LogAndReset();
		DX12ModLockTelemetryUtils::LogAndReset();
		DX12ResidentMaintenanceTelemetryUtils::LogAndReset();
#endif
	}
};

// Unified instrumentation macros for new code. Existing macro families
// (DX12_PROFILE_FUNCTION / DX12_HOTPATH_TIME_*) remain as compatibility
// aliases and all route through DX12Profiler above.
#define DX12_PROFILE_SCOPE(counter) \
	DX12Profiler::Scope _profile_scope( \
		DX12Profiler::Counter, #counter)

#define DX12_PROFILE_API(api) \
	DX12Profiler::Scope _profile_api( \
		DX12Profiler::Api, static_cast<int>(api))

#define DX12_PROFILE_STAGE(stage) \
	DX12Profiler::Scope _profile_stage( \
		DX12Profiler::Stage, static_cast<int>(stage))

#define DX12_HOTPATH_TIME_BEGIN(stage) \
	DX12Profiler::BeginStage(static_cast<int>(DX12HotPathStage::stage))

#define DX12_HOTPATH_TIME_END(stage) \
	DX12Profiler::EndStage(static_cast<int>(DX12HotPathStage::stage))

#define DX12_HOTPATH_TIME_SCOPE(stage) \
	DX12Profiler::Scope __hotpathTimer##__LINE__( \
		DX12Profiler::Stage, static_cast<int>(DX12HotPathStage::stage))

#define DX12_HOTPATH_LOG_AND_RESET() \
	DX12Profiler::FlushFrame()
