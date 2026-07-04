bool DX12ModReplaceShaderBytecode(
	const char *stage, const D3D12_SHADER_BYTECODE &source,
	D3D12_SHADER_BYTECODE *replacement, std::vector<unsigned char> *storage)
{
	if (!DX12ModHasActiveShaderOverrides())
		return false;
	if (!stage || !source.pShaderBytecode || source.BytecodeLength == 0 ||
	    !replacement || !storage)
		return false;

	const uint64_t hash = DX12ModHashShaderBytecode(
		source.pShaderBytecode, source.BytecodeLength);

	std::wstring section;
	std::wstring shaderFixesDir;
	AcquireSRWLockShared(&gModLock);
	auto it = gShaderOverrides.find(hash);
	if (it == gShaderOverrides.end()) {
		ReleaseSRWLockShared(&gModLock);
		return false;
	}
	section = it->second.section;
	shaderFixesDir = gShaderFixesDir;
	ReleaseSRWLockShared(&gModLock);

	wchar_t path[MAX_PATH];
	swprintf_s(path, L"%s\\%016llx-%S.bin",
		shaderFixesDir.c_str(), static_cast<unsigned long long>(hash), stage);

	std::vector<unsigned char> bytes;
	if (!ReadFileBytes(path, &bytes)) {
		DX12LogManager::DebugJsonFunc("DX12ShaderOverrideMissingReplacement",
			"\"section\":\"%S\",\"stage\":\"%s\",\"hash\":\"%016llx\",\"path\":\"%S\"",
			section.c_str(), stage, static_cast<unsigned long long>(hash), path);
		return false;
	}

	storage->swap(bytes);
	replacement->pShaderBytecode = storage->data();
	replacement->BytecodeLength = storage->size();
	DX12LogManager::DebugJsonFunc("DX12ShaderOverrideApplied",
		"\"section\":\"%S\",\"stage\":\"%s\",\"hash\":\"%016llx\",\"path\":\"%S\",\"bytes\":%zu",
		section.c_str(), stage, static_cast<unsigned long long>(hash), path, storage->size());
	return true;
}

bool DX12ModHasShaderOverride(uint64_t hash)
{
	AcquireSRWLockShared(&gModLock);
	bool found = gShaderOverrides.find(hash) != gShaderOverrides.end();
	ReleaseSRWLockShared(&gModLock);
	return found;
}

bool DX12ModHasActiveShaderOverrides()
{
	return (gHasShaderOverrides != 0 || gHasShaderRegexes != 0) &&
		InterlockedCompareExchange(&gDx12SafeMode, 0, 0) == 0;
}

bool DX12ModNeedsShaderDescriptorTracking()
{
	return gHasShaderDescriptorTextureOverrideTriggers != 0 &&
		InterlockedCompareExchange(&gDx12SafeMode, 0, 0) == 0;
}

bool DX12ModHasActiveTextureOverrides()
{
	return gHasTextureOverrides != 0 &&
		gHasShaderTriggeredTextureOverrides != 0 &&
		InterlockedCompareExchange(&gDx12SafeMode, 0, 0) == 0;
}

bool DX12ModHasAnyActiveOverrides()
{
	return InterlockedCompareExchange(&gDx12SafeMode, 0, 0) == 0 &&
		(gHasShaderOverrides != 0 ||
		 gHasShaderRegexes != 0 ||
		 (gHasTextureOverrides != 0 && gHasShaderTriggeredTextureOverrides != 0));
}

bool DX12ModNeedsPresentReplacement()
{
	return gHasPresentRuntimeEffect != 0 &&
		InterlockedCompareExchange(&gDx12SafeMode, 0, 0) == 0;
}

bool DX12ModNeedsPreSkinningUavProbe()
{
	return gHasTextureOverrides != 0 &&
		gHasShaderTriggeredTextureOverrides != 0 &&
		gHasPreSkinTextureOverrideCandidates != 0 &&
		InterlockedCompareExchange(&gDx12SafeMode, 0, 0) == 0;
}

// Returns true when pre-skin binding tracking is actively needed.
// Automatically disables after kPreSkinBindingIdleMax consecutive frames
// with zero active overrides, saving ~1500+ lock acquisitions per frame.
// Note: idle counter is reset when a matching pre-skin CS dispatch is probed.
bool DX12ModNeedsPreSkinningBindingTracking()
{
	if (gHasPreSkinTextureOverrideCandidates == 0)
		return false;
	if (!DX12ModHasActivePreSkinTextureOverrides())
		return gPreSkinBindingIdleFrames < kPreSkinBindingIdleMax;
	return true;
}

bool DX12ModHasActivePreSkinTextureOverrides()
{
	if (InterlockedCompareExchange(&gDx12SafeMode, 0, 0) != 0)
		return false;

	bool active = false;
	AcquireSRWLockShared(&gPreSkinLock);
	active = !gActivePreSkinTextureOverrides.empty();
	ReleaseSRWLockShared(&gPreSkinLock);
	return active;
}

bool DX12ModShouldProbePreSkinningForCs(uint64_t computeShaderHash)
{
	if (InterlockedCompareExchange(&gDx12SafeMode, 0, 0) != 0 ||
	    gHasTextureOverrides == 0 ||
	    gHasShaderTriggeredTextureOverrides == 0 ||
	    gHasPreSkinTextureOverrideCandidates == 0)
		return false;

	bool shouldProbe = false;
	AcquireSRWLockShared(&gModLock);
	if (gHasPreSkinVlrWithoutMatchCs) {
		shouldProbe = true;
	} else if (computeShaderHash) {
		shouldProbe = gPreSkinMatchCsHashes.find(computeShaderHash) !=
			gPreSkinMatchCsHashes.end();
	}
	ReleaseSRWLockShared(&gModLock);
	// Reset binding tracking idle counter: as long as a matching CS dispatch
	// runs, keep binding tracking active so the probe can access bindings.
	if (shouldProbe)
		InterlockedExchange(&gPreSkinBindingIdleFrames, 0);
	DX12Profiling::RecordPreSkinCsTest(shouldProbe);
	return shouldProbe;
}

bool DX12ModShouldTrackPreSkinBindingsForCs(uint64_t computeShaderHash)
{
	return DX12ModShouldProbePreSkinningForCs(computeShaderHash);
}

bool DX12ModPreSkinningUavProducerMayMatch(
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs)
{
	(void)uavs;
	return DX12ModShouldProbePreSkinningForCs(computeShaderHash);
}

bool DX12ModHasCachedPreSkinningUavMatch(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	bool *matched)
{
	if (!matched || !commandList || !computeShaderHash || computeBindingSerial == 0)
		return false;

	AcquireSRWLockShared(&gModLock);
	auto cached = gPreSkinUavMatchCache.find(commandList);
	if (cached != gPreSkinUavMatchCache.end() &&
	    cached->second.valid &&
	    cached->second.computeShaderHash == computeShaderHash &&
	    cached->second.computeBindingSerial == computeBindingSerial) {
		*matched = cached->second.matched;
		ReleaseSRWLockShared(&gModLock);
		return true;
	}
	ReleaseSRWLockShared(&gModLock);
	return false;
}

static void UpdatePreSkinningUavMatchCacheLocked(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	bool matched,
	const DX12PreSkinDispatchOverride *dispatchOverride)
{
	DX12PreSkinUavMatchCacheEntry &entry = gPreSkinUavMatchCache[commandList];
	entry.computeShaderHash = computeShaderHash;
	entry.computeBindingSerial = computeBindingSerial;
	entry.valid = true;
	entry.matched = matched;
	if (dispatchOverride)
		entry.dispatchOverride = *dispatchOverride;
}

void DX12ModStoreCachedPreSkinningUavMatch(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	bool matched,
	const DX12PreSkinDispatchOverride *dispatchOverride)
{
	if (!commandList || !computeShaderHash || computeBindingSerial == 0)
		return;

	AcquireSRWLockExclusive(&gModLock);
	UpdatePreSkinningUavMatchCacheLocked(
		commandList, computeShaderHash, computeBindingSerial, matched, dispatchOverride);
	ReleaseSRWLockExclusive(&gModLock);
}

bool DX12ModGetCachedPreSkinProbeResult(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	DX12PreSkinProbeCacheEntry *result)
{
	if (!result || !commandList || !computeShaderHash || computeBindingSerial == 0)
		return false;

	AcquireSRWLockShared(&gModLock);
	auto cached = gPreSkinUavMatchCache.find(commandList);
	if (cached != gPreSkinUavMatchCache.end() &&
	    cached->second.valid &&
	    cached->second.computeShaderHash == computeShaderHash &&
	    cached->second.computeBindingSerial == computeBindingSerial) {
		result->applied = cached->second.matched;
		result->dispatchOverride = cached->second.dispatchOverride;
		ReleaseSRWLockShared(&gModLock);
		return true;
	}
	ReleaseSRWLockShared(&gModLock);
	return false;
}

void DX12ModRecordComputeUavs(
	ID3D12GraphicsCommandList *commandList, const std::vector<DX12CurrentComputeUavBinding> &uavs)
{
	if (!commandList || uavs.empty() || !DX12ModNeedsPreSkinningUavProbe())
		return;

	AcquireSRWLockExclusive(&gPreSkinLock);
	for (const DX12CurrentComputeUavBinding &uav : uavs) {
		DX12ComputeUavProducer producer;
		producer.serial = ++gComputeUavSerial;
		producer.rootParameterIndex = uav.rootParameterIndex;
		producer.rangeIndex = uav.rangeIndex;
		producer.shaderRegister = uav.shaderRegister;
		producer.registerSpace = uav.registerSpace;
		producer.descriptorOffset = uav.descriptorOffset;
		producer.rootDescriptor = uav.rootDescriptor;
		producer.gpuVirtualAddress = uav.gpuVirtualAddress;
		producer.hasDescriptor = uav.hasDescriptor;
		producer.descriptor = uav.descriptor;
		gRecentComputeUavs.push_back(std::move(producer));
	}
	if (gRecentComputeUavs.size() > MaxRecentComputeUavs) {
		gRecentComputeUavs.erase(
			gRecentComputeUavs.begin(),
			gRecentComputeUavs.begin() +
			static_cast<ptrdiff_t>(gRecentComputeUavs.size() - MaxRecentComputeUavs));
	}
	ReleaseSRWLockExclusive(&gPreSkinLock);
}

