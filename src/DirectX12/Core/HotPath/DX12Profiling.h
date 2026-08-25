#pragma once

// Lightweight named-scope profiler for DX12.
// Split out of Core/DX12HotPath.h; include that umbrella header from existing code.
//
// Usage:
//   DX12Profiling::Start("SomeFunction");
//   ... work ...
//   DX12Profiling::End("SomeFunction");
// or:
//   DX12Profiling::Scope scope("SomeFunction");
//
// Timing (DX12_RELEASE_TIMING) builds only; every entry point compiles
// to nothing in Release/Debug. Hot path is lock-free: each thread owns
// a fixed open-addressing table keyed by the string-literal pointer.
// FlushBatch() is called once per present and emits one batched log
// block every kFlushIntervalPresents frames.
#include "../DX12CommandState.h"
#include "../DX12LogManager.h"

class DX12Profiling
{
public:
	static constexpr UINT kProfileThreadTables = 64;
	static constexpr UINT kProfileBucketsPerThread = 128; // power of two
	static constexpr UINT kProfileMergedBuckets = 256;    // power of two
	static constexpr UINT kProfileScopeStackDepth = 32;
	static constexpr UINT kFlushIntervalPresents = 60;

	struct ProfileBucket
	{
		const char *name = nullptr;
		LONG64 calls = 0;
		LONG64 totalUs = 0;
		LONG64 maxUs = 0;
	};

	struct ProfileThreadTable
	{
		ProfileBucket buckets[kProfileBucketsPerThread];
	};

	struct ProfileScopeEntry
	{
		ProfileBucket *bucket = nullptr;
		LONGLONG startQpc = 0;
	};

	static LONG64 ProfileFrequency()
	{
		static const LONG64 frequency = []() {
			LARGE_INTEGER value = {};
			QueryPerformanceFrequency(&value);
			return value.QuadPart > 0 ? value.QuadPart : 1;
		}();
		return frequency;
	}

#if defined(DX12_RELEASE_TIMING)
	static ProfileThreadTable *ProfileTable()
	{
		static thread_local ProfileThreadTable *table = AcquireProfileTable();
		return table;
	}

	static ProfileThreadTable *AcquireProfileTable()
	{
		const LONG index = InterlockedIncrement(&sProfileTableCount) - 1;
		// Threads beyond the pool share the last table; only diagnostics blur.
		if (index < 0 || index >= static_cast<LONG>(kProfileThreadTables))
			return &sProfileTables[kProfileThreadTables - 1];
		return &sProfileTables[index];
	}
#endif

	static ProfileBucket *FindProfileBucket(ProfileThreadTable *table, const char *name)
	{
		const UINT mask = kProfileBucketsPerThread - 1;
		UINT slot = static_cast<UINT>(
			((reinterpret_cast<UINT_PTR>(name) >> 4) * 2654435761u)) & mask;
		for (UINT probe = 0; probe < kProfileBucketsPerThread; ++probe) {
			ProfileBucket &bucket = table->buckets[(slot + probe) & mask];
			if (bucket.name == name)
				return &bucket;
			if (!bucket.name) {
				bucket.name = name;
				return &bucket;
			}
		}
		return nullptr; // table full: drop this sample (diagnostics only)
	}

	static void AccumulateProfileUs(ProfileBucket *bucket, LONGLONG startQpc, LONGLONG endQpc)
	{
		if (!bucket || endQpc <= startQpc)
			return;
		const LONG64 us = static_cast<LONG64>(
			(endQpc - startQpc) * 1000000 / ProfileFrequency());
		bucket->totalUs += us;
		if (us > bucket->maxUs)
			bucket->maxUs = us;
	}

	static void Start(const char *name)
	{
#if defined(DX12_RELEASE_TIMING)
		if (!name)
			return;
		ProfileBucket *bucket = FindProfileBucket(ProfileTable(), name);
		if (!bucket)
			return;
		++bucket->calls;
		LARGE_INTEGER now = {};
		QueryPerformanceCounter(&now);
		if (sProfileScopeDepth < kProfileScopeStackDepth) {
			ProfileScopeEntry &entry = sProfileScopeStack[sProfileScopeDepth++];
			entry.bucket = bucket;
			entry.startQpc = now.QuadPart;
		}
#else
		(void)name;
#endif
	}

	static void End(const char *name)
	{
#if defined(DX12_RELEASE_TIMING)
		(void)name; // pairing is positional; mismatched names only blur diagnostics
		if (sProfileScopeDepth == 0)
			return;
		const ProfileScopeEntry entry = sProfileScopeStack[--sProfileScopeDepth];
		LARGE_INTEGER now = {};
		QueryPerformanceCounter(&now);
		AccumulateProfileUs(entry.bucket, entry.startQpc, now.QuadPart);
#else
		(void)name;
#endif
	}

	static void Count(const char *name)
	{
#if defined(DX12_RELEASE_TIMING)
		if (!name)
			return;
		ProfileBucket *bucket = FindProfileBucket(ProfileTable(), name);
		if (bucket)
			++bucket->calls;
#else
		(void)name;
#endif
	}

	static void AddTimeUs(const char *name, LONG64 elapsedUs)
	{
#if defined(DX12_RELEASE_TIMING)
		if (!name || elapsedUs < 0)
			return;
		ProfileBucket *bucket = FindProfileBucket(ProfileTable(), name);
		if (!bucket)
			return;
		++bucket->calls;
		bucket->totalUs += elapsedUs;
		if (elapsedUs > bucket->maxUs)
			bucket->maxUs = elapsedUs;
#else
		(void)name;
		(void)elapsedUs;
#endif
	}

	class Scope
	{
	public:
		explicit Scope(const char *name)
		{
#if defined(DX12_RELEASE_TIMING)
			mBucket = name ? FindProfileBucket(ProfileTable(), name) : nullptr;
			if (!mBucket)
				return;
			++mBucket->calls;
			LARGE_INTEGER now = {};
			QueryPerformanceCounter(&now);
			mStartQpc = now.QuadPart;
#else
			(void)name;
#endif
		}

		~Scope()
		{
#if defined(DX12_RELEASE_TIMING)
			if (!mBucket)
				return;
			LARGE_INTEGER now = {};
			QueryPerformanceCounter(&now);
			AccumulateProfileUs(mBucket, mStartQpc, now.QuadPart);
#endif
		}

		Scope(const Scope &) = delete;
		Scope &operator=(const Scope &) = delete;

	private:
#if defined(DX12_RELEASE_TIMING)
		ProfileBucket *mBucket = nullptr;
		LONGLONG mStartQpc = 0;
#endif
	};

	// Called once per present. Every kFlushIntervalPresents frames the
	// per-thread tables are merged and emitted as one batched log block.
	static void FlushBatch()
	{
#if defined(DX12_RELEASE_TIMING)
		if (InterlockedIncrement(&sProfileFlushTick) <
			static_cast<LONG>(kFlushIntervalPresents))
			return;
		InterlockedExchange(&sProfileFlushTick, 0);

		// Merge all registered thread tables. Racing increments can be
		// missed for one interval; diagnostics only, same trade the old
		// per-frame merge made.
		ProfileBucket merged[kProfileMergedBuckets] = {};
		const UINT mergedMask = kProfileMergedBuckets - 1;
		UINT mergedCount = 0;
		const LONG tableCount = InterlockedCompareExchange(&sProfileTableCount, 0, 0);
		const LONG tablesToScan = tableCount < static_cast<LONG>(kProfileThreadTables)
			? tableCount : static_cast<LONG>(kProfileThreadTables);
		for (LONG t = 0; t < tablesToScan; ++t) {
			ProfileThreadTable &table = sProfileTables[t];
			for (UINT i = 0; i < kProfileBucketsPerThread; ++i) {
				ProfileBucket &src = table.buckets[i];
				if (!src.name)
					continue;
				const LONG64 calls = src.calls;
				const LONG64 totalUs = src.totalUs;
				const LONG64 maxUs = src.maxUs;
				src.calls = 0;
				src.totalUs = 0;
				src.maxUs = 0;
				if (!calls && !totalUs)
					continue;
				UINT slot = static_cast<UINT>(
					((reinterpret_cast<UINT_PTR>(src.name) >> 4) * 2654435761u)) & mergedMask;
				ProfileBucket *dst = nullptr;
				for (UINT probe = 0; probe < kProfileMergedBuckets; ++probe) {
					ProfileBucket &candidate = merged[(slot + probe) & mergedMask];
					if (candidate.name == src.name) {
						dst = &candidate;
						break;
					}
					if (!candidate.name) {
						candidate.name = src.name;
						dst = &candidate;
						++mergedCount;
						break;
					}
				}
				if (!dst)
					continue; // merged table full: drop
				dst->calls += calls;
				dst->totalUs += totalUs;
				if (maxUs > dst->maxUs)
					dst->maxUs = maxUs;
			}
		}
		if (!mergedCount)
			return;

		// Sort by totalUs descending.
		ProfileBucket *sorted[kProfileMergedBuckets] = {};
		UINT sortedCount = 0;
		for (UINT i = 0; i < kProfileMergedBuckets && sortedCount < mergedCount; ++i) {
			if (merged[i].name)
				sorted[sortedCount++] = &merged[i];
		}
		for (UINT i = 1; i < sortedCount; ++i) {
			ProfileBucket *key = sorted[i];
			UINT j = i;
			while (j > 0 && sorted[j - 1]->totalUs < key->totalUs) {
				sorted[j] = sorted[j - 1];
				--j;
			}
			sorted[j] = key;
		}

		char timePlain[32] = {};
		DX12LogUtils::MakeLogTimePlain(timePlain, sizeof(timePlain));
		char block[16384];
		int offset = snprintf(block, sizeof(block),
			"time=%s,func=DX12Profiling,frames=%u\n"
			"name,calls,totalUs,avgUs,maxUs\n",
			timePlain, kFlushIntervalPresents);
		for (UINT i = 0; i < sortedCount && offset > 0 &&
			static_cast<size_t>(offset) < sizeof(block) - 256; ++i) {
			const ProfileBucket &bucket = *sorted[i];
			offset += snprintf(block + offset, sizeof(block) - static_cast<size_t>(offset),
				"%s,%lld,%lld,%lld,%lld\n",
				bucket.name,
				static_cast<long long>(bucket.calls),
				static_cast<long long>(bucket.totalUs),
				static_cast<long long>(bucket.calls ? bucket.totalUs / bucket.calls : 0),
				static_cast<long long>(bucket.maxUs));
		}
		if (offset > 0)
			DX12LogManager::EnqueueLogLine(block);
#endif
	}

#if defined(DX12_RELEASE_TIMING)
	inline static ProfileThreadTable sProfileTables[kProfileThreadTables] = {};
	inline static volatile LONG sProfileTableCount = 0;
	inline static volatile LONG sProfileFlushTick = 0;
	inline static thread_local ProfileScopeEntry sProfileScopeStack[kProfileScopeStackDepth] = {};
	inline static thread_local UINT sProfileScopeDepth = 0;
#endif

	// -------------------------------------------------------------------
	// Compatibility API kept while the remaining instrumentation headers
	// are migrated (docs/DX12ProfilingRefactorPlan.md). Everything here
	// forwards to the engine above and compiles away outside TIMING.
	// -------------------------------------------------------------------
	static bool IsActive()
	{
#if defined(DX12_RELEASE_TIMING)
		return true;
#else
		return false;
#endif
	}

	// Legacy hot-path early-out gate. Counters were historically off until
	// a hotkey enabled them; keep that fast path identical in Release.
	static constexpr bool ShouldCollectCounters() { return false; }

	static void BeginFrame() { FlushBatch(); }
	static void EndFrame() { FlushBatch(); }

	static void RecordIaDrawOverrideCheck() { Count("IaDrawOverrideCheck"); }
	static void RecordIaHashStateResult(bool hit) { Count(hit ? "IaHashStateHit" : "IaHashStateMiss"); }
	static void RecordIaApplied() { Count("IaApplied"); }
	static void RecordIaHuntIaUpdate() { Count("IaHuntIaUpdate"); }
	static void RecordIaReplacementDraw(bool indexed)
	{
		Count("IaReplacementDraw");
		if (indexed)
			Count("IaReplacementIndexedDraw");
	}
	static void RecordIaReplacementDispatch() { Count("IaReplacementDispatch"); }
	static void RecordPreSkinCsTest(bool hit)
	{
		(void)hit;
		Count("PreSkinCsTest");
	}
	static void RecordPreSkinUavTest(bool hit)
	{
		Count("PreSkinUavTest");
		if (hit)
			Count("PreSkinUavHit");
	}
	static void RecordPreSkinApplied() { Count("PreSkinApplied"); }
	static void RecordBindingQueueTaskUs(LONG64 elapsedUs) { AddTimeUs("BindingQueueTask", elapsedUs); }
	static void RecordBindingQueueDrainUs(LONG64 elapsedUs, LONG pending)
	{
		(void)pending;
		AddTimeUs("BindingQueueDrain", elapsedUs);
	}
};
