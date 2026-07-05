#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <vector>

#include "DX12CompiledDispatchDecision.hpp"
#include "DX12CompiledDrawDecision.hpp"

class DX12PsoDecisionCacheDispatchEntry
{
public:
	bool valid = false;
	UINT64 age = 0;
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	bool activeShaderOverrides = false;
	bool activePreSkinProbe = false;
	DX12CompiledDispatchDecision decision;
};

class DX12PsoDecisionCacheDrawEntry
{
public:
	bool valid = false;
	UINT64 age = 0;
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	bool activeShaderOverrides = false;
	DX12CompiledDrawDecision decision;
};

class DX12PsoDecisionCacheUtils
{
public:
	static constexpr size_t CacheCapacity = 1024;

	inline static SRWLOCK Lock = SRWLOCK_INIT;
	inline static UINT64 Clock = 0;
	inline static UINT64 ReloadGeneration = 0;
	inline static std::vector<DX12PsoDecisionCacheDispatchEntry> DispatchEntries;
	inline static std::vector<DX12PsoDecisionCacheDrawEntry> DrawEntries;

	static void ClearIfReloadChanged(UINT64 reloadGeneration)
	{
		AcquireSRWLockExclusive(&DX12PsoDecisionCacheUtils::Lock);
		if (DX12PsoDecisionCacheUtils::ReloadGeneration != reloadGeneration) {
			DX12PsoDecisionCacheUtils::ReloadGeneration = reloadGeneration;
			DX12PsoDecisionCacheUtils::Clock = 0;
			DX12PsoDecisionCacheUtils::DispatchEntries.clear();
			DX12PsoDecisionCacheUtils::DrawEntries.clear();
		}
		ReleaseSRWLockExclusive(&DX12PsoDecisionCacheUtils::Lock);
	}

	static bool TryLoadDispatch(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		bool activeShaderOverrides,
		bool activePreSkinProbe,
		DX12CompiledDispatchDecision *decision)
	{
		if (!decision)
			return false;

		bool found = false;
		AcquireSRWLockExclusive(&DX12PsoDecisionCacheUtils::Lock);
		for (size_t index = 0; index < DX12PsoDecisionCacheUtils::DispatchEntries.size(); ++index) {
			DX12PsoDecisionCacheDispatchEntry &entry =
				DX12PsoDecisionCacheUtils::DispatchEntries[index];
			if (!entry.valid ||
				entry.pipelineState != pipelineState ||
				entry.reloadGeneration != reloadGeneration ||
				entry.activeShaderOverrides != activeShaderOverrides ||
				entry.activePreSkinProbe != activePreSkinProbe)
				continue;
			entry.age = ++DX12PsoDecisionCacheUtils::Clock;
			*decision = entry.decision;
			found = true;
			break;
		}
		ReleaseSRWLockExclusive(&DX12PsoDecisionCacheUtils::Lock);
		return found;
	}

	static void StoreDispatch(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		bool activeShaderOverrides,
		bool activePreSkinProbe,
		const DX12CompiledDispatchDecision &decision)
	{
		AcquireSRWLockExclusive(&DX12PsoDecisionCacheUtils::Lock);
		DX12PsoDecisionCacheDispatchEntry *entry =
			DX12PsoDecisionCacheUtils::SelectDispatchEntry();
		entry->valid = true;
		entry->age = ++DX12PsoDecisionCacheUtils::Clock;
		entry->pipelineState = pipelineState;
		entry->reloadGeneration = reloadGeneration;
		entry->activeShaderOverrides = activeShaderOverrides;
		entry->activePreSkinProbe = activePreSkinProbe;
		entry->decision = decision;
		ReleaseSRWLockExclusive(&DX12PsoDecisionCacheUtils::Lock);
	}

	static bool TryLoadDraw(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		bool activeShaderOverrides,
		DX12CompiledDrawDecision *decision)
	{
		if (!decision)
			return false;

		bool found = false;
		AcquireSRWLockExclusive(&DX12PsoDecisionCacheUtils::Lock);
		for (size_t index = 0; index < DX12PsoDecisionCacheUtils::DrawEntries.size(); ++index) {
			DX12PsoDecisionCacheDrawEntry &entry =
				DX12PsoDecisionCacheUtils::DrawEntries[index];
			if (!entry.valid ||
				entry.pipelineState != pipelineState ||
				entry.reloadGeneration != reloadGeneration ||
				entry.activeShaderOverrides != activeShaderOverrides)
				continue;
			entry.age = ++DX12PsoDecisionCacheUtils::Clock;
			*decision = entry.decision;
			found = true;
			break;
		}
		ReleaseSRWLockExclusive(&DX12PsoDecisionCacheUtils::Lock);
		return found;
	}

	static void StoreDraw(
		ID3D12PipelineState *pipelineState,
		UINT64 reloadGeneration,
		bool activeShaderOverrides,
		const DX12CompiledDrawDecision &decision)
	{
		AcquireSRWLockExclusive(&DX12PsoDecisionCacheUtils::Lock);
		DX12PsoDecisionCacheDrawEntry *entry =
			DX12PsoDecisionCacheUtils::SelectDrawEntry();
		entry->valid = true;
		entry->age = ++DX12PsoDecisionCacheUtils::Clock;
		entry->pipelineState = pipelineState;
		entry->reloadGeneration = reloadGeneration;
		entry->activeShaderOverrides = activeShaderOverrides;
		entry->decision = decision;
		ReleaseSRWLockExclusive(&DX12PsoDecisionCacheUtils::Lock);
	}

	static DX12PsoDecisionCacheDispatchEntry *SelectDispatchEntry()
	{
		if (DX12PsoDecisionCacheUtils::DispatchEntries.size() <
			DX12PsoDecisionCacheUtils::CacheCapacity) {
			DX12PsoDecisionCacheDispatchEntry entry;
			DX12PsoDecisionCacheUtils::DispatchEntries.push_back(entry);
			return &DX12PsoDecisionCacheUtils::DispatchEntries[
				DX12PsoDecisionCacheUtils::DispatchEntries.size() - 1];
		}

		size_t targetIndex = 0;
		UINT64 oldestAge = DX12PsoDecisionCacheUtils::DispatchEntries[0].age;
		for (size_t index = 1; index < DX12PsoDecisionCacheUtils::DispatchEntries.size(); ++index) {
			if (DX12PsoDecisionCacheUtils::DispatchEntries[index].age < oldestAge) {
				oldestAge = DX12PsoDecisionCacheUtils::DispatchEntries[index].age;
				targetIndex = index;
			}
		}
		return &DX12PsoDecisionCacheUtils::DispatchEntries[targetIndex];
	}

	static DX12PsoDecisionCacheDrawEntry *SelectDrawEntry()
	{
		if (DX12PsoDecisionCacheUtils::DrawEntries.size() <
			DX12PsoDecisionCacheUtils::CacheCapacity) {
			DX12PsoDecisionCacheDrawEntry entry;
			DX12PsoDecisionCacheUtils::DrawEntries.push_back(entry);
			return &DX12PsoDecisionCacheUtils::DrawEntries[
				DX12PsoDecisionCacheUtils::DrawEntries.size() - 1];
		}

		size_t targetIndex = 0;
		UINT64 oldestAge = DX12PsoDecisionCacheUtils::DrawEntries[0].age;
		for (size_t index = 1; index < DX12PsoDecisionCacheUtils::DrawEntries.size(); ++index) {
			if (DX12PsoDecisionCacheUtils::DrawEntries[index].age < oldestAge) {
				oldestAge = DX12PsoDecisionCacheUtils::DrawEntries[index].age;
				targetIndex = index;
			}
		}
		return &DX12PsoDecisionCacheUtils::DrawEntries[targetIndex];
	}
};
