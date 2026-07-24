static CommandListBindingState* GetOrResolveBindingState(
	ID3D12GraphicsCommandList *commandList)
{
	// Fast path: TLS cache hit with matching shard generation
	if (tlsStateCache.commandList == commandList && tlsStateCache.state) {
		BindingShard &cachedShard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
		if (tlsStateCache.shardGeneration == cachedShard.generation) {
			return tlsStateCache.state;
		}
	}

	// Slow path: compute shard hash, lock, hashmap lookup
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	tlsStateCache.commandList = commandList;
	tlsStateCache.state = &state;
	tlsStateCache.shardGeneration = shard.generation;
	tlsStateCache.shardIndex = static_cast<size_t>(&shard - DX12BindingTrackerState::Shards);
	ReleaseSRWLockExclusive(&shard.lock);
	return &state;
}

static UINT64 MixPreSkinDependencyFingerprint(UINT64 fingerprint, UINT64 value)
{
	return fingerprint ^ (value + 0x9e3779b97f4a7c15ull +
		(fingerprint << 6) + (fingerprint >> 2));
}

static UINT64 BuildRuntimePreSkinDependencyFingerprint(
	UINT64 computeBindingSerial,
	ID3D12DescriptorHeap *heap,
	const UINT64 *slotVersions,
	const UINT64 *slotValueIds,
	size_t slotCount)
{
	UINT64 fingerprint = 14695981039346656037ull;
	fingerprint = MixPreSkinDependencyFingerprint(fingerprint, computeBindingSerial);
	fingerprint = MixPreSkinDependencyFingerprint(
		fingerprint,
		static_cast<UINT64>(reinterpret_cast<uintptr_t>(heap)));
	for (size_t index = 0; index < slotCount; ++index) {
		fingerprint = MixPreSkinDependencyFingerprint(fingerprint, slotVersions[index]);
		fingerprint = MixPreSkinDependencyFingerprint(fingerprint, slotValueIds[index]);
	}
	return fingerprint ? fingerprint : 1;
}

static bool TryGetRuntimePreSkinBindings(
	DX12SimulateContext *runtimeState,
	UINT64 computeBindingSerial,
	const std::vector<DX12CurrentComputeUavBinding> **uavs,
	const std::vector<DX12CurrentComputeUavBinding> **srvs,
	const std::vector<DX12CurrentComputeUavBinding> **cbvs,
	const std::vector<DX12CurrentRootConstants> **constants,
	UINT64 *dependencyFingerprint,
	bool *foundAny)
{
	if (!runtimeState || !runtimeState->resolvedCompute.bindingsValid ||
			runtimeState->resolvedCompute.bindingsHeap != runtimeState->cbvSrvUavHeap ||
			runtimeState->resolvedCompute.bindingsRootSignature !=
				runtimeState->computeRootSignature ||
			(runtimeState->resolvedCompute.bindingsUseRootValues &&
			 runtimeState->resolvedCompute.bindingsSerial != computeBindingSerial) ||
			(!runtimeState->resolvedCompute.bindingsUseRootValues &&
			 runtimeState->resolvedCompute.bindingsDescriptorVersion !=
				runtimeState->computeDescriptorBindingVersion))
		return false;

	const size_t observedCount = runtimeState->resolvedCompute.observedSlots.size();
	if (observedCount != runtimeState->resolvedCompute.observedSlotVersions.size() ||
			observedCount != runtimeState->resolvedCompute.observedSlotValueIds.size())
		return false;

	if (observedCount) {
		if (!runtimeState->resolvedCompute.bindingsHeap)
			return false;
		constexpr size_t StackIdentityCount = 64;
		std::array<UINT64, StackIdentityCount> stackSlotVersions;
		std::array<UINT64, StackIdentityCount> stackSlotValueIds;
		std::vector<UINT64> heapSlotVersions;
		std::vector<UINT64> heapSlotValueIds;
		UINT64 *currentSlotVersions = stackSlotVersions.data();
		UINT64 *currentSlotValueIds = stackSlotValueIds.data();
		if (observedCount > StackIdentityCount) {
			heapSlotVersions.resize(observedCount);
			heapSlotValueIds.resize(observedCount);
			currentSlotVersions = heapSlotVersions.data();
			currentSlotValueIds = heapSlotValueIds.data();
		}
		if (!DX12GetDescriptorSlotIdentities(
				runtimeState->resolvedCompute.bindingsHeap,
				runtimeState->resolvedCompute.observedSlots.data(),
				observedCount,
				currentSlotVersions,
				currentSlotValueIds))
			return false;
		for (size_t index = 0; index < observedCount; ++index) {
			if (currentSlotVersions[index] !=
					runtimeState->resolvedCompute.observedSlotVersions[index] ||
					currentSlotValueIds[index] !=
					runtimeState->resolvedCompute.observedSlotValueIds[index]) {
				return false;
			}
		}
	}

	if (uavs)
		*uavs = &runtimeState->resolvedCompute.uavs;
	if (srvs)
		*srvs = &runtimeState->resolvedCompute.srvs;
	if (cbvs)
		*cbvs = &runtimeState->resolvedCompute.cbvs;
	if (constants)
		*constants = &runtimeState->resolvedCompute.rootConstants;
	if (dependencyFingerprint)
		*dependencyFingerprint = runtimeState->resolvedCompute.bindingsDependencyFingerprint;
	if (foundAny)
		*foundAny = runtimeState->resolvedCompute.bindingsFoundAny;
	return true;
}

static bool TryLoadRuntimePreSkinBindings(
	DX12SimulateContext *runtimeState,
	UINT64 computeBindingSerial,
	std::vector<DX12CurrentComputeUavBinding> *uavs,
	std::vector<DX12CurrentComputeUavBinding> *srvs,
	std::vector<DX12CurrentComputeUavBinding> *cbvs,
	std::vector<DX12CurrentRootConstants> *constants,
	UINT64 *dependencyFingerprint,
	bool *foundAny)
{
	const std::vector<DX12CurrentComputeUavBinding> *cachedUavs = nullptr;
	const std::vector<DX12CurrentComputeUavBinding> *cachedSrvs = nullptr;
	const std::vector<DX12CurrentComputeUavBinding> *cachedCbvs = nullptr;
	const std::vector<DX12CurrentRootConstants> *cachedConstants = nullptr;
	if (!TryGetRuntimePreSkinBindings(
			runtimeState,
			computeBindingSerial,
			&cachedUavs,
			&cachedSrvs,
			&cachedCbvs,
			&cachedConstants,
			dependencyFingerprint,
			foundAny))
		return false;
	if (uavs && cachedUavs)
		*uavs = *cachedUavs;
	if (srvs && cachedSrvs)
		*srvs = *cachedSrvs;
	if (cbvs && cachedCbvs)
		*cbvs = *cachedCbvs;
	if (constants && cachedConstants)
		*constants = *cachedConstants;
	return true;
}

static void StoreRuntimePreSkinBindings(
	DX12SimulateContext *runtimeState,
	UINT64 computeBindingSerial,
	UINT64 dependencyFingerprint,
	bool usesRootValues,
	bool foundAny,
	const std::vector<UINT> &observedSlots,
	const std::vector<UINT64> &observedSlotVersions,
	const std::vector<UINT64> &observedSlotValueIds,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &constants)
{
	if (!runtimeState)
		return;
	runtimeState->resolvedCompute.bindingsValid = true;
	runtimeState->resolvedCompute.bindingsFoundAny = foundAny;
	runtimeState->resolvedCompute.bindingsUseRootValues = usesRootValues;
	runtimeState->resolvedCompute.bindingsSerial = computeBindingSerial;
	runtimeState->resolvedCompute.bindingsDescriptorVersion =
		runtimeState->computeDescriptorBindingVersion;
	runtimeState->resolvedCompute.bindingsDependencyFingerprint =
		dependencyFingerprint;
	runtimeState->resolvedCompute.bindingsHeap = runtimeState->cbvSrvUavHeap;
	runtimeState->resolvedCompute.bindingsRootSignature =
		runtimeState->computeRootSignature;
	runtimeState->resolvedCompute.observedSlots = observedSlots;
	runtimeState->resolvedCompute.observedSlotVersions = observedSlotVersions;
	runtimeState->resolvedCompute.observedSlotValueIds = observedSlotValueIds;
	runtimeState->resolvedCompute.uavs = uavs;
	runtimeState->resolvedCompute.srvs = srvs;
	runtimeState->resolvedCompute.cbvs = cbvs;
	runtimeState->resolvedCompute.rootConstants = constants;
}

bool DX12BindingTracker::GetSimulateContextPreSkinBindings(
	ID3D12GraphicsCommandList *commandList,
	const std::vector<DX12CurrentComputeUavBinding> **uavs,
	const std::vector<DX12CurrentComputeUavBinding> **srvs,
	const std::vector<DX12CurrentComputeUavBinding> **cbvs,
	const std::vector<DX12CurrentRootConstants> **constants,
	UINT64 *dependencyFingerprint,
	bool *foundAny)
{
	if (uavs)
		*uavs = nullptr;
	if (srvs)
		*srvs = nullptr;
	if (cbvs)
		*cbvs = nullptr;
	if (constants)
		*constants = nullptr;
	if (dependencyFingerprint)
		*dependencyFingerprint = 0;
	if (foundAny)
		*foundAny = false;
	if (!commandList)
		return false;

	DX12SimulateContext *context =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	if (!context || !context->computeBindingSerial)
		return false;

	bool cachedFoundAny = false;
	const bool loaded = TryGetRuntimePreSkinBindings(
		context,
		context->computeBindingSerial,
		uavs,
		srvs,
		cbvs,
		constants,
		dependencyFingerprint,
		&cachedFoundAny);
	if (foundAny)
		*foundAny = cachedFoundAny;
	return loaded;
}

static bool TryLoadRuntimeDescriptorBindings(
	DX12SimulateContext *runtimeState,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	UINT64 bindingQuerySerial,
	std::vector<DX12CurrentComputeUavBinding> *bindings,
	bool *foundAny)
{
	if (!runtimeState || !bindings)
		return false;

	const UINT cacheSlot =
		BindingTlsDescriptorQueryCache::CacheSlotIndex(compute, rangeType);
	if (cacheSlot == UINT_MAX ||
			cacheSlot >= runtimeState->resolvedDescriptorQueries.size())
		return false;

	DX12ResolvedDescriptorQueryRuntimeState &cache =
		runtimeState->resolvedDescriptorQueries[cacheSlot];
	if (!cache.valid ||
			cache.compute != compute ||
			cache.rangeType != static_cast<UINT>(rangeType) ||
			cache.bindingQuerySerial != bindingQuerySerial ||
			cache.bindingsHeap != runtimeState->cbvSrvUavHeap)
		return false;

	const size_t observedCount = cache.observedSlots.size();
	if (observedCount != cache.observedSlotVersions.size() ||
			observedCount != cache.observedSlotValueIds.size())
		return false;

	if (observedCount) {
		if (!cache.bindingsHeap)
			return false;
		constexpr size_t StackIdentityCount = 64;
		std::array<UINT64, StackIdentityCount> stackSlotVersions;
		std::array<UINT64, StackIdentityCount> stackSlotValueIds;
		std::vector<UINT64> heapSlotVersions;
		std::vector<UINT64> heapSlotValueIds;
		UINT64 *currentSlotVersions = stackSlotVersions.data();
		UINT64 *currentSlotValueIds = stackSlotValueIds.data();
		if (observedCount > StackIdentityCount) {
			heapSlotVersions.resize(observedCount);
			heapSlotValueIds.resize(observedCount);
			currentSlotVersions = heapSlotVersions.data();
			currentSlotValueIds = heapSlotValueIds.data();
		}
		if (!DX12GetDescriptorSlotIdentities(
				cache.bindingsHeap,
				cache.observedSlots.data(),
				observedCount,
				currentSlotVersions,
				currentSlotValueIds))
			return false;
		for (size_t index = 0; index < observedCount; ++index) {
			if (currentSlotVersions[index] != cache.observedSlotVersions[index] ||
					currentSlotValueIds[index] != cache.observedSlotValueIds[index]) {
				return false;
			}
		}
	}

	*bindings = cache.bindings;
	if (foundAny)
		*foundAny = cache.foundAny;
	return true;
}

static void StoreRuntimeDescriptorBindings(
	DX12SimulateContext *runtimeState,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	UINT64 bindingQuerySerial,
	bool foundAny,
	const std::vector<UINT> &observedSlots,
	const std::vector<UINT64> &observedSlotVersions,
	const std::vector<UINT64> &observedSlotValueIds,
	const std::vector<DX12CurrentComputeUavBinding> &bindings)
{
	if (!runtimeState)
		return;

	const UINT cacheSlot =
		BindingTlsDescriptorQueryCache::CacheSlotIndex(compute, rangeType);
	if (cacheSlot == UINT_MAX ||
			cacheSlot >= runtimeState->resolvedDescriptorQueries.size())
		return;

	DX12ResolvedDescriptorQueryRuntimeState &cache =
		runtimeState->resolvedDescriptorQueries[cacheSlot];
	cache.valid = true;
	cache.foundAny = foundAny;
	cache.compute = compute;
	cache.rangeType = static_cast<UINT>(rangeType);
	cache.bindingQuerySerial = bindingQuerySerial;
	cache.bindingsHeap = runtimeState->cbvSrvUavHeap;
	cache.observedSlots = observedSlots;
	cache.observedSlotVersions = observedSlotVersions;
	cache.observedSlotValueIds = observedSlotValueIds;
	cache.bindings = bindings;
}

static UINT64 GetRuntimeDescriptorQuerySerial(
	const DX12SimulateContext *runtimeState,
	bool compute)
{
	if (!runtimeState)
		return 0;
	const UINT64 serial = compute ?
		runtimeState->computeDescriptorQuerySerial :
		runtimeState->graphicsDescriptorQuerySerial;
	return serial ? serial : 1;
}

static bool BuildRuntimeBindingSnapshot(
	const DX12SimulateContext *runtimeState,
	CommandListBindingState *snapshot)
{
	if (!runtimeState || !snapshot)
		return false;

	*snapshot = CommandListBindingState();
	snapshot->pipelineState = runtimeState->pipelineState;
	snapshot->graphicsRootSignature = runtimeState->graphicsRootSignature;
	snapshot->computeRootSignature = runtimeState->computeRootSignature;
	snapshot->cbvSrvUavHeap = runtimeState->cbvSrvUavHeap;
	snapshot->samplerHeap = runtimeState->samplerHeap;
	snapshot->runtimeCbvSrvUavHeapResolved =
		runtimeState->runtimeCbvSrvUavHeapResolved;
	snapshot->runtimeCbvSrvUavHeap = runtimeState->runtimeCbvSrvUavHeap;
	snapshot->runtimeSamplerHeapResolved =
		runtimeState->runtimeSamplerHeapResolved;
	snapshot->runtimeSamplerHeap = runtimeState->runtimeSamplerHeap;
	snapshot->computeBindingSerial = runtimeState->computeBindingSerial;
	snapshot->bindingQuerySerial = runtimeState->bindingQuerySerial;
	snapshot->graphicsTableValidCount = runtimeState->graphicsRootTableValidCount;
	snapshot->computeTableValidCount = runtimeState->computeRootTableValidCount;
	memcpy(snapshot->graphicsTables,
		runtimeState->graphicsRootTables,
		sizeof(snapshot->graphicsTables));
	memcpy(snapshot->computeTables,
		runtimeState->computeRootTables,
		sizeof(snapshot->computeTables));
	memcpy(snapshot->graphicsRootDescriptors,
		runtimeState->graphicsRootDescriptors,
		sizeof(snapshot->graphicsRootDescriptors));
	memcpy(snapshot->computeRootDescriptors,
		runtimeState->computeRootDescriptors,
		sizeof(snapshot->computeRootDescriptors));
	memcpy(snapshot->graphicsRootConstants,
		runtimeState->graphicsRootConstants,
		sizeof(snapshot->graphicsRootConstants));
	memcpy(snapshot->computeRootConstants,
		runtimeState->computeRootConstants,
		sizeof(snapshot->computeRootConstants));
	snapshot->primitiveTopology = runtimeState->ia.primitiveTopology;
	if (runtimeState->ia.hasIndexBuffer) {
		snapshot->indexBuffer = runtimeState->ia.indexBuffer;
		snapshot->indexBufferValid = true;
		snapshot->indexBufferResource = runtimeState->ia.indexBufferResource;
		snapshot->indexBufferResolved = runtimeState->ia.indexBufferResolved;
	}
	for (UINT slot = 0; slot < ARRAYSIZE(runtimeState->ia.vertexBuffers); ++slot) {
		if (!runtimeState->ia.hasVertexBuffer[slot])
			continue;
		snapshot->vertexBuffers[slot] = runtimeState->ia.vertexBuffers[slot];
		snapshot->vertexBufferValid[slot] = true;
		snapshot->vertexBufferResources[slot] =
			runtimeState->ia.vertexBufferResources[slot];
		snapshot->vertexBufferResolved[slot] =
			runtimeState->ia.vertexBufferResolved[slot];
	}
	return true;
}

class DX12BindingTrackerQueryCacheUtils
{
public:
	static bool ReadDescriptorSlotIdentities(
		ID3D12DescriptorHeap *heap,
		const UINT *slots,
		size_t count,
		UINT64 *versions,
		UINT64 *valueIds)
	{
		return DX12GetDescriptorSlotIdentities(
			heap, slots, count, versions, valueIds);
	}

	static void FillRuntimeTableFromHeapSummary(
		RootTableState *table,
		const DX12DescriptorHeapSummary &heap)
	{
		if (!table || !table->valid ||
				heap.increment == 0 || heap.gpuStart == 0 ||
				table->baseDescriptor.ptr < heap.gpuStart)
			return;

		const UINT64 tableBaseIndex =
			(table->baseDescriptor.ptr - heap.gpuStart) / heap.increment;
		if (tableBaseIndex >= heap.numDescriptors)
			return;

		table->runtimeHeapResolved = true;
		table->runtimeHeap = heap.heap;
		table->runtimeHeapType = heap.type;
		table->runtimeHeapCpuStart = heap.cpuStart;
		table->runtimeHeapGpuStart = heap.gpuStart;
		table->runtimeHeapIncrement = heap.increment;
		table->runtimeHeapNumDescriptors = heap.numDescriptors;
		table->runtimeHeapVersion = heap.version;
		table->runtimeTableBaseIndex = tableBaseIndex;
		table->runtimeTableCpuStart =
			heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
		table->runtimeTableGpuStart =
			heap.gpuStart + tableBaseIndex * heap.increment;
	}

	static void InvalidateRuntimeTable(
		RootTableState *table)
	{
		if (!table)
			return;

		table->runtimeHeapResolved = false;
		table->runtimeHeap = nullptr;
		table->runtimeHeapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		table->runtimeHeapCpuStart = 0;
		table->runtimeHeapGpuStart = 0;
		table->runtimeHeapIncrement = 0;
		table->runtimeHeapNumDescriptors = 0;
		table->runtimeHeapVersion = 0;
		table->runtimeTableBaseIndex = 0;
		table->runtimeTableCpuStart = 0;
		table->runtimeTableGpuStart = 0;
	}

	static void RefreshRuntimeTable(
		const CommandListBindingState &snapshot,
		RootTableState *table)
	{
		if (!table || !table->valid)
			return;

		DX12BindingTrackerQueryCacheUtils::InvalidateRuntimeTable(table);

		DX12DescriptorHeapSummary heap = {};
		if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
				snapshot, *table, &heap))
			return;
		DX12BindingTrackerQueryCacheUtils::FillRuntimeTableFromHeapSummary(
			table, heap);
	}

	static bool MakeHeapSummaryFromRuntimeTable(
		const RootTableState &table,
		DX12DescriptorHeapSummary *heap)
	{
		if (!heap || !table.valid || !table.runtimeHeapResolved ||
				table.runtimeHeapIncrement == 0 ||
				table.runtimeTableBaseIndex >= table.runtimeHeapNumDescriptors)
			return false;

		heap->heap = table.runtimeHeap;
		heap->type = table.runtimeHeapType;
		heap->numDescriptors = table.runtimeHeapNumDescriptors;
		heap->cpuStart = table.runtimeHeapCpuStart;
		heap->gpuStart = table.runtimeHeapGpuStart;
		heap->increment = table.runtimeHeapIncrement;
		heap->version = table.runtimeHeapVersion;
		return true;
	}

	static bool TryGetBoundCbvSrvUavHeap(
		const CommandListBindingState &snapshot,
		DX12DescriptorHeapSummary *heap)
	{
		if (!heap || !snapshot.cbvSrvUavHeap)
			return false;
		if (snapshot.runtimeCbvSrvUavHeapResolved &&
				snapshot.runtimeCbvSrvUavHeap.heap == snapshot.cbvSrvUavHeap &&
				snapshot.runtimeCbvSrvUavHeap.type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
				snapshot.runtimeCbvSrvUavHeap.increment != 0) {
			*heap = snapshot.runtimeCbvSrvUavHeap;
			return true;
		}
		if (!DX12FindDescriptorHeapByPtr(snapshot.cbvSrvUavHeap, heap))
			return false;
		return heap->type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
			heap->increment != 0;
	}

	static void StoreRuntimeHeapSummaries(
		CommandListBindingState *state,
		const DX12DescriptorHeapSummary *cbvSrvUavHeap,
		bool cbvSrvUavResolved,
		const DX12DescriptorHeapSummary *samplerHeap,
		bool samplerResolved)
	{
		if (!state)
			return;
		state->runtimeCbvSrvUavHeapResolved = cbvSrvUavResolved;
		state->runtimeCbvSrvUavHeap =
			(cbvSrvUavResolved && cbvSrvUavHeap) ?
			*cbvSrvUavHeap : DX12DescriptorHeapSummary();
		state->runtimeSamplerHeapResolved = samplerResolved;
		state->runtimeSamplerHeap =
			(samplerResolved && samplerHeap) ?
			*samplerHeap : DX12DescriptorHeapSummary();
	}

	static void RefreshAllRuntimeTables(CommandListBindingState *state)
	{
		if (!state)
			return;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			if (state->graphicsTables[root].valid)
				DX12BindingTrackerQueryCacheUtils::RefreshRuntimeTable(
					*state, &state->graphicsTables[root]);
			if (state->computeTables[root].valid)
				DX12BindingTrackerQueryCacheUtils::RefreshRuntimeTable(
					*state, &state->computeTables[root]);
		}
	}

	static bool ResolveTableHeap(
		const CommandListBindingState &snapshot,
		const RootTableState &table,
		DX12DescriptorHeapSummary *heap)
	{
		if (!heap || !table.valid)
			return false;
		if (DX12BindingTrackerQueryCacheUtils::MakeHeapSummaryFromRuntimeTable(
				table, heap))
			return true;
		DX12DescriptorHeapSummary boundHeap = {};
		if (DX12BindingTrackerQueryCacheUtils::TryGetBoundCbvSrvUavHeap(
				snapshot, &boundHeap) &&
			DX12BindingTrackerQuery::TableMatchesHeap(table, boundHeap)) {
			*heap = boundHeap;
			return true;
		}
		return DX12FindDescriptorHeapByGpuHandle(
			table.baseDescriptor, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap) &&
			heap->increment != 0;
	}

	static bool ResolveRuntimeTableHeap(
		const CommandListBindingState &snapshot,
		const RootTableState &table,
		DX12DescriptorHeapSummary *heap)
	{
		if (DX12BindingTrackerQueryCacheUtils::MakeHeapSummaryFromRuntimeTable(
				table, heap))
			return true;
		return DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
			snapshot, table, heap);
	}

	static bool EnsureQueryRootSignature(
		const CommandListBindingState &snapshot,
		bool compute,
		BindingEvent *event)
	{
		if (!event)
			return false;

		ID3D12RootSignature *rootSignature = compute ?
			event->computeRootSignature : event->graphicsRootSignature;
		if (rootSignature)
			return true;
		if (!snapshot.pipelineState)
			return false;

		if (event->shaderInfo.psoIndex == 0)
			DX12GetPipelineStateShaderInfo(snapshot.pipelineState, &event->shaderInfo);
		if (event->shaderInfo.psoIndex == 0)
			return false;

		ID3D12RootSignature *psoRootSignature = nullptr;
		if (!DX12GetPsoRootSignature(event->shaderInfo.psoIndex, &psoRootSignature) ||
			!psoRootSignature)
			return false;

		if (compute)
			event->computeRootSignature = psoRootSignature;
		else
			event->graphicsRootSignature = psoRootSignature;
		return true;
	}

	static void BuildEventForQuery(
		const CommandListBindingState &snapshot,
		bool compute,
		BindingEvent *event)
	{
		if (!event)
			return;
		*event = BindingEvent();
		event->pipelineState = snapshot.pipelineState;
		event->graphicsRootSignature = snapshot.graphicsRootSignature;
		event->computeRootSignature = snapshot.computeRootSignature;
		event->shaderInfo = snapshot.pipelineShaderInfo;
		DX12BindingTrackerQueryCacheUtils::EnsureQueryRootSignature(
			snapshot, compute, event);
	}

	static bool CollectDescriptorBindings(
		const CommandListBindingState &snapshot,
		bool compute,
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
		const char *descriptorKind,
		D3D12_ROOT_PARAMETER_TYPE rootDescriptorType,
		std::vector<DX12CurrentComputeUavBinding> *outBindings,
		ID3D12DescriptorHeap **observedSlotHeap = nullptr,
		std::vector<UINT> *observedSlots = nullptr)
	{
		if (!outBindings)
			return false;

		outBindings->clear();
		if (observedSlotHeap)
			*observedSlotHeap = nullptr;
		if (observedSlots)
			observedSlots->clear();
		BindingEvent event;
		BuildEventForQuery(snapshot, compute, &event);
		DX12RootSignatureSummary rootSignature;
		const bool hasRootSignature =
			DX12BindingTrackerQuery::GetRootSignatureForEvent(
				event, compute, &rootSignature);
		const bool completeDescriptorResourceInfo =
			rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV ||
			rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			const RootTableState &table = compute ?
				snapshot.computeTables[root] : snapshot.graphicsTables[root];
			if (table.valid) {
				const DX12RootParameterSummary *parameter =
					hasRootSignature ? DX12BindingTrackerQuery::FindRootParameter(rootSignature, root) : nullptr;
						if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
					for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
						if (range.rangeType != rangeType)
							continue;
						DX12DescriptorHeapSummary heap = {};
						if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
								snapshot, table, &heap))
							continue;
						const UINT64 tableBaseIndex =
							(table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
						const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(
							heap, tableBaseIndex, range);
						std::vector<SIZE_T> cpuHandles;
						std::vector<DX12DescriptorSummary> descriptors;
						std::vector<uint8_t> found;
						if (observedSlotHeap && heap.heap && !*observedSlotHeap)
							*observedSlotHeap = heap.heap;
						cpuHandles.reserve(count);
						for (UINT offset = 0; offset < count; ++offset) {
							const UINT64 descriptorIndex =
								tableBaseIndex + range.effectiveOffset + offset;
							if (descriptorIndex >= heap.numDescriptors)
								break;
							if (observedSlots)
								observedSlots->push_back(static_cast<UINT>(descriptorIndex));
							const SIZE_T cpuHandle =
								heap.cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap.increment;
							cpuHandles.push_back(cpuHandle);
						}
						descriptors.resize(cpuHandles.size());
						found.resize(cpuHandles.size());
						DX12FindDescriptorSummariesByCpuHandle(
							cpuHandles.data(), cpuHandles.size(),
							descriptors.data(), found.data(),
							completeDescriptorResourceInfo);
						for (UINT offset = 0; offset < cpuHandles.size(); ++offset) {
							if (!found[offset] || descriptors[offset].kind != descriptorKind)
								continue;
							DX12CurrentComputeUavBinding binding;
							binding.rootParameterIndex = root;
							binding.rangeIndex = range.rangeIndex;
							binding.shaderVisibility = range.shaderVisibility;
							binding.shaderRegister = range.baseShaderRegister + offset;
							binding.registerSpace = range.registerSpace;
							binding.descriptorOffset = range.effectiveOffset + offset;
							binding.descriptorIncrement = heap.increment;
							binding.tableCopyCount = count;
							binding.tableCpuStart =
								heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
							binding.tableGpuStart.ptr =
								heap.gpuStart + tableBaseIndex * heap.increment;
							binding.tableHeap = heap.heap;
							binding.descriptor = descriptors[offset];
							binding.hasDescriptor = true;
							outBindings->push_back(std::move(binding));
						}
					}
				} else {
					DX12DescriptorHeapSummary heap = {};
					if (DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
							snapshot, table, &heap)) {
						const UINT64 descriptorIndex =
							(table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
						if (observedSlotHeap && heap.heap && !*observedSlotHeap)
							*observedSlotHeap = heap.heap;
						if (observedSlots && descriptorIndex < heap.numDescriptors)
							observedSlots->push_back(static_cast<UINT>(descriptorIndex));
						const SIZE_T cpuHandle =
							heap.cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap.increment;
						DX12DescriptorSummary descriptor = {};
						const bool foundDescriptor = completeDescriptorResourceInfo ?
							DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &descriptor) :
							DX12FindDescriptorSummaryByCpuHandleLight(cpuHandle, &descriptor);
						if (foundDescriptor && descriptor.kind == descriptorKind) {
							DX12CurrentComputeUavBinding binding;
							binding.rootParameterIndex = root;
							binding.shaderVisibility = parameter ?
								parameter->shaderVisibility : D3D12_SHADER_VISIBILITY_ALL;
							binding.descriptorIncrement = heap.increment;
							binding.tableCopyCount = 1;
							binding.tableCpuStart = cpuHandle;
							binding.tableGpuStart = table.baseDescriptor;
							binding.tableHeap = heap.heap;
							binding.descriptor = descriptor;
							binding.hasDescriptor = true;
							outBindings->push_back(std::move(binding));
						}
					}
				}
			}

			const RootDescriptorState &rootDescriptor = compute ?
				snapshot.computeRootDescriptors[root] : snapshot.graphicsRootDescriptors[root];
			if (rootDescriptor.valid && rootDescriptor.type == rootDescriptorType) {
				DX12CurrentComputeUavBinding binding;
				binding.rootParameterIndex = root;
				if (hasRootSignature) {
					const DX12RootParameterSummary *parameter =
						DX12BindingTrackerQuery::FindRootParameter(rootSignature, root);
					if (parameter) {
						binding.shaderVisibility = parameter->shaderVisibility;
						binding.shaderRegister = parameter->shaderRegister;
						binding.registerSpace = parameter->registerSpace;
					}
				}
				binding.rootDescriptor = true;
				binding.gpuVirtualAddress = rootDescriptor.address;
				outBindings->push_back(std::move(binding));
			}
		}

		return !outBindings->empty();
	}

	static bool CollectDescriptorBindingsByRegister(
		const CommandListBindingState &snapshot,
		bool compute,
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
		UINT shaderRegister,
		UINT registerSpace,
		const char *descriptorKind,
		D3D12_ROOT_PARAMETER_TYPE rootDescriptorType,
		std::vector<DX12CurrentComputeUavBinding> *outBindings)
	{
		if (!outBindings)
			return false;

		outBindings->clear();
		BindingEvent event;
		BuildEventForQuery(snapshot, compute, &event);
		DX12RootSignatureSummary rootSignature;
		const bool hasRootSignature =
			DX12BindingTrackerQuery::GetRootSignatureForEvent(
				event, compute, &rootSignature);
		if (!hasRootSignature) {
			std::vector<DX12CurrentComputeUavBinding> allBindings;
			if (!CollectDescriptorBindings(
					snapshot, compute, rangeType, descriptorKind,
					rootDescriptorType, &allBindings))
				return false;
			for (const DX12CurrentComputeUavBinding &binding : allBindings) {
				if (binding.shaderRegister != shaderRegister ||
				    binding.registerSpace != registerSpace)
					continue;
				outBindings->push_back(binding);
			}
			return !outBindings->empty();
		}

		const bool completeDescriptorResourceInfo =
			rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV ||
			rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			const RootTableState &table = compute ?
				snapshot.computeTables[root] : snapshot.graphicsTables[root];
			const DX12RootParameterSummary *parameter =
				DX12BindingTrackerQuery::FindRootParameter(rootSignature, root);
			if (table.valid && parameter &&
			    parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
				for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
					if (range.rangeType != rangeType ||
					    range.registerSpace != registerSpace ||
					    shaderRegister < range.baseShaderRegister)
						continue;
					DX12DescriptorHeapSummary heap = {};
					if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
							snapshot, table, &heap))
						continue;
					const UINT64 tableBaseIndex =
						(table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
					const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(
						heap, tableBaseIndex, range);
					const UINT offset = shaderRegister - range.baseShaderRegister;
					if (offset >= count)
						continue;
					const UINT64 descriptorIndex =
						tableBaseIndex + range.effectiveOffset + offset;
					if (descriptorIndex >= heap.numDescriptors)
						continue;
					const SIZE_T cpuHandle =
						heap.cpuStart + static_cast<SIZE_T>(descriptorIndex) * heap.increment;
					DX12DescriptorSummary descriptor = {};
					const bool foundDescriptor = completeDescriptorResourceInfo ?
						DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &descriptor) :
						DX12FindDescriptorSummaryByCpuHandleLight(cpuHandle, &descriptor);
					if (!foundDescriptor || descriptor.kind != descriptorKind)
						continue;
					DX12CurrentComputeUavBinding binding;
					binding.rootParameterIndex = root;
					binding.rangeIndex = range.rangeIndex;
					binding.shaderVisibility = range.shaderVisibility;
					binding.shaderRegister = shaderRegister;
					binding.registerSpace = registerSpace;
					binding.descriptorOffset = range.effectiveOffset + offset;
					binding.descriptorIncrement = heap.increment;
					binding.tableCopyCount = count;
					binding.tableCpuStart =
						heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
					binding.tableGpuStart.ptr =
						heap.gpuStart + tableBaseIndex * heap.increment;
					binding.tableHeap = heap.heap;
					binding.descriptor = descriptor;
					binding.hasDescriptor = true;
					outBindings->push_back(std::move(binding));
				}
			}

			const RootDescriptorState &rootDescriptor = compute ?
				snapshot.computeRootDescriptors[root] : snapshot.graphicsRootDescriptors[root];
			if (!rootDescriptor.valid || rootDescriptor.type != rootDescriptorType || !parameter)
				continue;
			if (parameter->shaderRegister != shaderRegister ||
			    parameter->registerSpace != registerSpace)
				continue;
			DX12CurrentComputeUavBinding binding;
			binding.rootParameterIndex = root;
			binding.shaderVisibility = parameter->shaderVisibility;
			binding.shaderRegister = parameter->shaderRegister;
			binding.registerSpace = parameter->registerSpace;
			binding.rootDescriptor = true;
			binding.gpuVirtualAddress = rootDescriptor.address;
			outBindings->push_back(std::move(binding));
		}

		return !outBindings->empty();
	}

	static bool CollectComputeAll(
		const CommandListBindingState &snapshot,
		std::vector<DX12CurrentComputeUavBinding> *uavs,
		std::vector<DX12CurrentComputeUavBinding> *srvs,
		std::vector<DX12CurrentComputeUavBinding> *cbvs,
		std::vector<DX12CurrentRootConstants> *constants)
	{
		if (uavs) uavs->clear();
		if (srvs) srvs->clear();
		if (cbvs) cbvs->clear();
		if (constants) constants->clear();

		BindingEvent event;
		BuildEventForQuery(snapshot, true, &event);
		DX12RootSignatureSummary rootSignature;
		const bool hasRootSignature =
			DX12BindingTrackerQuery::GetRootSignatureForEvent(event, true, &rootSignature);

		// Root-signature-driven iteration: use declared parameter count as upper bound.
		const UINT maxRoot = hasRootSignature
			? static_cast<UINT>(rootSignature.parameters.size())
			: MaxRootParameters;

		static thread_local std::vector<SIZE_T> tlsCpuHandles;
		static thread_local std::vector<DX12DescriptorSummary> tlsDescriptors;
		static thread_local std::vector<uint8_t> tlsFound;

		for (UINT root = 0; root < maxRoot; ++root) {
			const RootTableState &table = snapshot.computeTables[root];
			if (table.valid) {
				const DX12RootParameterSummary *parameter =
					hasRootSignature ? DX12BindingTrackerQuery::FindRootParameter(rootSignature, root) : nullptr;
				if (parameter && parameter->parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
					DX12DescriptorHeapSummary heap = {};
					if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
							snapshot, table, &heap))
						continue;
					const UINT64 tableBase = (table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
					for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
						std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
						const char *kind = nullptr;
						bool completeDescriptorResourceInfo = false;
						if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV && uavs) { out = uavs; kind = "UAV"; }
						else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && srvs) {
							out = srvs;
							kind = "SRV";
							completeDescriptorResourceInfo = true;
						}
						else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV && cbvs) {
							out = cbvs;
							kind = "CBV";
							completeDescriptorResourceInfo = true;
						}
						else { continue; }

						const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(heap, tableBase, range);
						tlsCpuHandles.clear();
						tlsCpuHandles.reserve(count);
						for (UINT offset = 0; offset < count; ++offset) {
							const UINT64 di = tableBase + range.effectiveOffset + offset;
							if (di >= heap.numDescriptors) break;
							const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(di) * heap.increment;
							tlsCpuHandles.push_back(cpuHandle);
						}
						tlsDescriptors.resize(tlsCpuHandles.size());
						tlsFound.resize(tlsCpuHandles.size());
						DX12FindDescriptorSummariesByCpuHandle(
							tlsCpuHandles.data(), tlsCpuHandles.size(),
							tlsDescriptors.data(), tlsFound.data(),
							completeDescriptorResourceInfo);
						for (UINT offset = 0; offset < tlsCpuHandles.size(); ++offset) {
							if (!tlsFound[offset] || tlsDescriptors[offset].kind != kind)
								continue;
							DX12CurrentComputeUavBinding binding;
							binding.rootParameterIndex = root;
							binding.rangeIndex = range.rangeIndex;
							binding.shaderVisibility = range.shaderVisibility;
							binding.shaderRegister = range.baseShaderRegister + offset;
							binding.registerSpace = range.registerSpace;
							binding.descriptorOffset = range.effectiveOffset + offset;
							binding.descriptorIncrement = heap.increment;
							binding.tableCopyCount = count;
							binding.tableCpuStart = heap.cpuStart + static_cast<SIZE_T>(tableBase) * heap.increment;
							binding.tableGpuStart.ptr = heap.gpuStart + tableBase * heap.increment;
							binding.tableHeap = heap.heap;
							binding.descriptor = tlsDescriptors[offset];
							binding.hasDescriptor = true;
							out->push_back(std::move(binding));
						}
					}
				} else {
					DX12DescriptorHeapSummary heap = {};
					if (!DX12BindingTrackerQueryCacheUtils::ResolveTableHeap(
							snapshot, table, &heap))
						continue;
					const UINT64 di = (table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
					const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(di) * heap.increment;
					DX12DescriptorSummary desc = {};
					if (!DX12FindDescriptorSummaryByCpuHandleLight(cpuHandle, &desc))
						continue;
					std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
					bool completeDescriptorResourceInfo = false;
					if (desc.kind == DescriptorKind::UAV && uavs) out = uavs;
					else if (desc.kind == DescriptorKind::SRV && srvs) {
						if (!DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &desc))
							continue;
						out = srvs;
						completeDescriptorResourceInfo = true;
					}
					else if (desc.kind == DescriptorKind::CBV && cbvs) {
						if (!DX12FindDescriptorSummaryByCpuHandle(cpuHandle, &desc))
							continue;
						out = cbvs;
						completeDescriptorResourceInfo = true;
					}
					if (!out) continue;
					(void)completeDescriptorResourceInfo;
					DX12CurrentComputeUavBinding binding;
					binding.rootParameterIndex = root;
					binding.shaderVisibility = parameter ? parameter->shaderVisibility : D3D12_SHADER_VISIBILITY_ALL;
					binding.descriptorIncrement = heap.increment;
					binding.tableCopyCount = 1;
					binding.tableCpuStart = cpuHandle;
					binding.tableGpuStart = table.baseDescriptor;
					binding.tableHeap = heap.heap;
					binding.descriptor = desc;
					binding.hasDescriptor = true;
					out->push_back(std::move(binding));
				}
			}

			const RootDescriptorState &rd = snapshot.computeRootDescriptors[root];
			if (rd.valid) {
				std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
				if (rd.type == D3D12_ROOT_PARAMETER_TYPE_UAV && uavs) out = uavs;
				else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_SRV && srvs) out = srvs;
				else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_CBV && cbvs) out = cbvs;
				if (out) {
					DX12CurrentComputeUavBinding binding;
					binding.rootParameterIndex = root;
					if (hasRootSignature) {
						const DX12RootParameterSummary *p = DX12BindingTrackerQuery::FindRootParameter(rootSignature, root);
						if (p) {
							binding.shaderVisibility = p->shaderVisibility;
							binding.shaderRegister = p->shaderRegister;
							binding.registerSpace = p->registerSpace;
						}
					}
					binding.rootDescriptor = true;
					binding.gpuVirtualAddress = rd.address;
					out->push_back(std::move(binding));
				}
			}

			if (constants) {
				const RootConstantsState &rc = snapshot.computeRootConstants[root];
				if (rc.valid) {
					DX12CurrentRootConstants current;
					current.rootParameterIndex = root;
					current.maxSet = rc.maxSet;
					memcpy(current.values, rc.values, sizeof(current.values));
					memcpy(current.valueValid, rc.valueValid, sizeof(current.valueValid));
					constants->push_back(current);
				}
			}
		}

		return (uavs && !uavs->empty()) || (srvs && !srvs->empty()) ||
			(cbvs && !cbvs->empty()) || (constants && !constants->empty());
	}
};

// ============================================================================
// CollectPreSkinBindings - lightweight replacement for GetCurrentComputeAll
// in the PreSkin hot path. Uses a cached root signature summary (parsed once
// per ID3D12RootSignature*) to iterate descriptor table ranges without
// re-parsing the blob on every call. Scans multiple descriptors per table
// (not just the first) to correctly capture UAVs at non-zero offsets.
//
// Cost: O(tables + descriptors_in_relevant_ranges) vs old O(all_descriptors).
// Typically ~50x faster (~0.5ms vs ~25ms) while producing identical results.
// ============================================================================
bool DX12BindingTracker::CollectPreSkinBindings(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *uavs,
	std::vector<DX12CurrentComputeUavBinding> *srvs,
	std::vector<DX12CurrentComputeUavBinding> *cbvs,
	std::vector<DX12CurrentRootConstants> *constants,
	UINT64 *dependencyFingerprint)
{
	if (dependencyFingerprint)
		*dependencyFingerprint = 0;
	if (!commandList)
		return false;

	if (uavs) uavs->clear();
	if (srvs) srvs->clear();
	if (cbvs) cbvs->clear();
	if (constants) constants->clear();

	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	const DX12CommandListTrackingState tracking =
		runtimeState ?
		DX12CommandListRuntime::GetTrackingState(runtimeState) :
		DX12CommandListTrackingState();
	const bool preferRuntimeSnapshot = runtimeState && !tracking.trackBindings;
	const UINT64 computeBindingSerial =
		runtimeState ?
		runtimeState->computeBindingSerial :
		DX12BindingTracker::GetComputeBindingSerial(commandList);
	bool cachedFoundAny = false;
	DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingCacheCall();
	if (runtimeState && computeBindingSerial &&
			TryLoadRuntimePreSkinBindings(
				runtimeState,
				computeBindingSerial,
				uavs,
				srvs,
				cbvs,
				constants,
				dependencyFingerprint,
				&cachedFoundAny)) {
		DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingCacheHit();
		return cachedFoundAny;
	}
	DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingCacheMiss();

	CommandListBindingState snapshot;
	DX12_HOTPATH_TIME_BEGIN(BindSnapshot);
	const bool snapshotReady = preferRuntimeSnapshot ?
		BuildRuntimeBindingSnapshot(runtimeState, &snapshot) :
		DX12BindingTracker::SnapshotBindingState(commandList, &snapshot);
	if (!snapshotReady) {
		DX12_HOTPATH_TIME_END(BindSnapshot);
		return false;
	}
	DX12_HOTPATH_TIME_END(BindSnapshot);

	// Cache root signature summary per ID3D12RootSignature* pointer.
	// Parse the blob once, reuse across all dispatches with the same signature.
	static std::unordered_map<ID3D12RootSignature*, DX12RootSignatureSummary> s_rootSigCache;
	static SRWLOCK s_rootSigCacheLock = SRWLOCK_INIT;
	static UINT64 s_rootSigCacheReloadGen = 0;
	UINT64 reloadGen = DX12ModGetReloadGeneration();
	AcquireSRWLockExclusive(&s_rootSigCacheLock);
	if (s_rootSigCacheReloadGen != reloadGen) {
		s_rootSigCache.clear();
		s_rootSigCacheReloadGen = reloadGen;
	}
	ReleaseSRWLockExclusive(&s_rootSigCacheLock);

	ID3D12RootSignature *computeRootSig = snapshot.computeRootSignature;
	if (!computeRootSig && snapshot.pipelineState) {
		DX12PsoShaderInfo shaderInfo = {};
		ID3D12RootSignature *psoRootSignature = nullptr;
		if (DX12GetPipelineStateShaderInfo(snapshot.pipelineState, &shaderInfo) &&
			shaderInfo.psoIndex &&
			DX12GetPsoRootSignature(shaderInfo.psoIndex, &psoRootSignature) &&
			psoRootSignature) {
			computeRootSig = psoRootSignature;
		}
	}
	DX12RootSignatureSummary rootSignatureSummary;
	const DX12RootSignatureSummary *cachedSummary = nullptr;
	if (computeRootSig) {
		AcquireSRWLockShared(&s_rootSigCacheLock);
		std::unordered_map<ID3D12RootSignature*, DX12RootSignatureSummary>::const_iterator sigIt =
			s_rootSigCache.find(computeRootSig);
		if (sigIt != s_rootSigCache.end()) {
			rootSignatureSummary = sigIt->second;
			cachedSummary = &rootSignatureSummary;
		}
		ReleaseSRWLockShared(&s_rootSigCacheLock);

		if (!cachedSummary) {
			DX12RootSignatureSummary parsed;
			if (DX12GetRootSignatureSummary(computeRootSig, &parsed)) {
				AcquireSRWLockExclusive(&s_rootSigCacheLock);
				if (s_rootSigCacheReloadGen != reloadGen) {
					s_rootSigCache.clear();
					s_rootSigCacheReloadGen = reloadGen;
				}
				std::unordered_map<ID3D12RootSignature*, DX12RootSignatureSummary>::iterator storeIt =
					s_rootSigCache.find(computeRootSig);
				if (storeIt == s_rootSigCache.end()) {
					s_rootSigCache[computeRootSig] = parsed;
					rootSignatureSummary = parsed;
				} else {
					rootSignatureSummary = storeIt->second;
				}
				ReleaseSRWLockExclusive(&s_rootSigCacheLock);
				cachedSummary = &rootSignatureSummary;
			}
		}
	}
	const bool hasParsedRootSignatureSummary =
		cachedSummary && cachedSummary->parsed;
	const UINT64 descriptorQuerySerialBefore = CurrentDescriptorQuerySerial();
	const UINT64 descriptorHeapVersionBefore =
		DX12GetDescriptorHeapVersion(snapshot.cbvSrvUavHeap);

	std::vector<DX12CurrentComputeUavBinding> localUavs;
	std::vector<DX12CurrentComputeUavBinding> localSrvs;
	std::vector<DX12CurrentComputeUavBinding> localCbvs;
	std::vector<DX12CurrentRootConstants> localConstants;
	bool allRuntimeDescriptorsStable = true;
	bool canStoreRuntimeBindings = runtimeState && computeBindingSerial != 0;
	ID3D12DescriptorHeap *observedSlotHeap = nullptr;
	std::vector<UINT> observedSlots;

	// Thread-local scratch buffers grow to the actual descriptor workload and
	// retain capacity across calls. Correctness is bounded by D3D12 metadata,
	// not by a local fixed-size array.
	static thread_local std::vector<SIZE_T> tlsCpuHandles;
	static thread_local std::vector<DX12DescriptorSummary> tlsDescriptors;
	static thread_local std::vector<uint8_t> tlsFound;
	static thread_local std::vector<uint8_t> tlsPending;
	static thread_local std::vector<UINT> tlsDescriptorOffsets;
	static thread_local std::vector<UINT64> tlsDescriptorIndices;
	static thread_local std::vector<UINT> tlsDescriptorSlots;

	DX12_HOTPATH_TIME_BEGIN(BindCollect);

	// Root-signature-driven iteration: use declared parameter count as upper bound.
	// If metadata is temporarily unavailable, keep the cheap root descriptor /
	// constant paths alive, but never turn that miss into an O(heap_size) scan.
	const UINT maxRoot = hasParsedRootSignatureSummary
		? static_cast<UINT>(cachedSummary->parameters.size())
		: MaxRootParameters;

	// Collect root constants directly from snapshot
	for (UINT root = 0; root < maxRoot; ++root) {
		const RootConstantsState &rc = snapshot.computeRootConstants[root];
		if (!rc.valid)
			continue;
		DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingConstant();
		DX12CurrentRootConstants current;
		current.rootParameterIndex = root;
		current.maxSet = rc.maxSet;
		memcpy(current.values, rc.values, sizeof(current.values));
		memcpy(current.valueValid, rc.valueValid, sizeof(current.valueValid));
		localConstants.push_back(current);
	}

	// Collect descriptor table bindings.
	// With root signature: iterate exactly the declared ranges (correct offset/count).
	// P5: Bulk descriptor lookup: collect ALL CPU handles from ALL ranges first,
	// then issue a SINGLE DX12FindDescriptorSummariesByCpuHandleNoWait call,
	// then distribute results back to per-range output vectors.
	// Without root signature: scan to the descriptor heap boundary, classify by kind.

	// Per-descriptor metadata for bulk lookup distribution.
	// Slot i tracks: output vector, kind string, root param index, range metadata, heap data.
	static thread_local std::vector<std::vector<DX12CurrentComputeUavBinding>*> tlsOutVec;
	static thread_local std::vector<const char*> tlsKind;
	static thread_local std::vector<UINT> tlsRootIndex;
	static thread_local std::vector<UINT> tlsRangeIndex;
	static thread_local std::vector<UINT> tlsRangeShaderVisibility;
	static thread_local std::vector<UINT> tlsRangeBaseShaderRegister;
	static thread_local std::vector<UINT> tlsRangeRegisterSpace;
	static thread_local std::vector<UINT> tlsRangeEffectiveOffset;
	static thread_local std::vector<UINT> tlsCount;
	static thread_local std::vector<UINT64> tlsTableBaseIndex;
	static thread_local std::vector<SIZE_T> tlsTableCpuStart;
	static thread_local std::vector<UINT64> tlsTableGpuStart;
	static thread_local std::vector<UINT> tlsTableIncrement;
	static thread_local std::vector<ID3D12DescriptorHeap*> tlsTableHeap;

	tlsCpuHandles.clear();
	tlsDescriptorOffsets.clear();
	tlsDescriptorIndices.clear();
	tlsDescriptorSlots.clear();
	tlsOutVec.clear();
	tlsKind.clear();
	tlsRootIndex.clear();
	tlsRangeIndex.clear();
	tlsRangeShaderVisibility.clear();
	tlsRangeBaseShaderRegister.clear();
	tlsRangeRegisterSpace.clear();
	tlsRangeEffectiveOffset.clear();
	tlsCount.clear();
	tlsTableBaseIndex.clear();
	tlsTableCpuStart.clear();
	tlsTableGpuStart.clear();
	tlsTableIncrement.clear();
	tlsTableHeap.clear();

	// Phase 1: Collect all CPU handles from root-signature ranges.
	size_t totalHandleCount = 0;
	ID3D12DescriptorHeap *slotLookupHeap = nullptr;
	bool canUseSlotLookup = true;
	for (UINT root = 0; root < maxRoot; ++root) {
		const RootTableState &table = snapshot.computeTables[root];
		if (!table.valid)
			continue;
		DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingRootTable();

		DX12DescriptorHeapSummary heap = {};
		if (!DX12BindingTrackerQueryCacheUtils::ResolveRuntimeTableHeap(snapshot, table, &heap)) {
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingRuntimeTable(false);
			allRuntimeDescriptorsStable = false;
			continue;
		}
		DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingRuntimeTable(
			table.runtimeHeapResolved);

		const UINT64 tableBaseIndex = (table.baseDescriptor.ptr - heap.gpuStart) / heap.increment;
		if (tableBaseIndex >= heap.numDescriptors)
			continue;

		const DX12RootParameterSummary *parameter =
			hasParsedRootSignatureSummary ?
				DX12BindingTrackerQuery::FindRootParameter(*cachedSummary, root) :
				nullptr;

		if (!parameter || parameter->parameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
			// Without parsed root-signature metadata, do not speculate about the
			// descriptor table extent. DX11-style fast rejection is preferable to
			// scanning an arbitrary portion of the heap on the dispatch hot path.
			continue;
		}

		// Precompute heap data shared by all ranges in this table.
		const SIZE_T tableCpuStart = heap.cpuStart + static_cast<SIZE_T>(tableBaseIndex) * heap.increment;
		const UINT64 tableGpuStart = heap.gpuStart + tableBaseIndex * heap.increment;

		for (const DX12RootDescriptorRangeSummary &range : parameter->ranges) {
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingRange();

			std::vector<DX12CurrentComputeUavBinding> *out = nullptr;
			const char *kind = nullptr;
			if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV) {
				out = &localUavs; kind = "UAV";
			} else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV) {
				out = &localSrvs; kind = "SRV";
			} else if (range.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
				out = &localCbvs; kind = "CBV";
			} else {
				continue;
			}

			const UINT count = DX12BindingTrackerQuery::DescriptorCountForRange(heap, tableBaseIndex, range);
			for (UINT offset = 0; offset < count; ++offset) {
				const UINT64 di = tableBaseIndex + range.effectiveOffset + offset;
				if (di >= heap.numDescriptors)
					break;
				const SIZE_T cpuHandle = heap.cpuStart + static_cast<SIZE_T>(di) * heap.increment;
				DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingFullDescriptorLookup();
				if (!heap.heap || di > static_cast<UINT64>(UINT_MAX)) {
					canUseSlotLookup = false;
				} else if (!slotLookupHeap) {
					slotLookupHeap = heap.heap;
				} else if (slotLookupHeap != heap.heap) {
					canUseSlotLookup = false;
				}
				if (canStoreRuntimeBindings) {
					if (!heap.heap || di > static_cast<UINT64>(UINT_MAX)) {
						canStoreRuntimeBindings = false;
					} else if (!observedSlotHeap) {
						observedSlotHeap = heap.heap;
					} else if (observedSlotHeap != heap.heap) {
						canStoreRuntimeBindings = false;
					}
					if (canStoreRuntimeBindings)
						observedSlots.push_back(static_cast<UINT>(di));
				}

				tlsCpuHandles.push_back(cpuHandle);
				tlsDescriptorSlots.push_back(static_cast<UINT>(di));
				tlsDescriptorOffsets.push_back(offset);
				tlsOutVec.push_back(out);
				tlsKind.push_back(kind);
				tlsRootIndex.push_back(root);
				tlsRangeIndex.push_back(range.rangeIndex);
				tlsRangeShaderVisibility.push_back(range.shaderVisibility);
				tlsRangeBaseShaderRegister.push_back(range.baseShaderRegister);
				tlsRangeRegisterSpace.push_back(range.registerSpace);
				tlsRangeEffectiveOffset.push_back(range.effectiveOffset);
				tlsCount.push_back(count);
				tlsTableBaseIndex.push_back(tableBaseIndex);
				tlsTableCpuStart.push_back(tableCpuStart);
				tlsTableGpuStart.push_back(tableGpuStart);
				tlsTableIncrement.push_back(heap.increment);
				tlsTableHeap.push_back(heap.heap);
				totalHandleCount++;
			}
		}
	}

	// Phase 2: Single bulk descriptor lookup for all root-signature ranges.
	if (totalHandleCount > 0) {
		tlsDescriptors.resize(totalHandleCount);
		tlsFound.resize(totalHandleCount);
		tlsPending.resize(totalHandleCount);
		if (canUseSlotLookup && slotLookupHeap) {
			memset(tlsPending.data(), 0, totalHandleCount * sizeof(uint8_t));
			DX12FindDescriptorSummariesByHeapSlot(
				slotLookupHeap,
				tlsDescriptorSlots.data(),
				totalHandleCount,
				tlsDescriptors.data(),
				tlsFound.data(),
				true); // needFullResource=true is the safe superset for UAV+SRV+CBV.
		} else {
			DX12FindDescriptorSummariesByCpuHandleNoWait(
				tlsCpuHandles.data(),
				totalHandleCount,
				tlsDescriptors.data(),
				tlsFound.data(),
				tlsPending.data(),
				true); // needFullResource=true is the safe superset for UAV+SRV+CBV.
		}

		LONG64 descriptorHits = 0;
		LONG64 descriptorMisses = 0;
		LONG64 descriptorPending = 0;
		for (size_t i = 0; i < totalHandleCount; ++i) {
			if (tlsPending[i]) {
				descriptorPending++;
				allRuntimeDescriptorsStable = false;
			} else if (tlsFound[i]) {
				descriptorHits++;
			} else {
				descriptorMisses++;
				allRuntimeDescriptorsStable = false;
			}
		}
		DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingDescriptorLookupResult(
			descriptorHits, descriptorMisses, descriptorPending);

		// Phase 3: Distribute results to per-range output vectors.
		for (size_t i = 0; i < totalHandleCount; ++i) {
			if (!tlsFound[i] || tlsDescriptors[i].kind != tlsKind[i])
				continue;

			std::vector<DX12CurrentComputeUavBinding> *out = tlsOutVec[i];

			DX12CurrentComputeUavBinding binding;
			binding.rootParameterIndex = tlsRootIndex[i];
			binding.rangeIndex = tlsRangeIndex[i];
			binding.shaderVisibility = tlsRangeShaderVisibility[i];
			binding.shaderRegister =
				tlsRangeBaseShaderRegister[i] + tlsDescriptorOffsets[i];
			binding.registerSpace = tlsRangeRegisterSpace[i];
			binding.descriptorOffset =
				tlsRangeEffectiveOffset[i] + tlsDescriptorOffsets[i];
			binding.descriptorIncrement = tlsTableIncrement[i];
			binding.tableCopyCount = tlsCount[i];
			binding.tableCpuStart = tlsTableCpuStart[i];
			binding.tableGpuStart.ptr = tlsTableGpuStart[i];
			binding.tableHeap = tlsTableHeap[i];
			binding.descriptor = tlsDescriptors[i];
			binding.hasDescriptor = true;
			out->push_back(std::move(binding));
		}
	}

	// Collect root descriptors (bare GPU VA, no heap needed)
	for (UINT root = 0; root < maxRoot; ++root) {
		const RootDescriptorState &rd = snapshot.computeRootDescriptors[root];
		if (!rd.valid)
			continue;
		DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingRootDescriptor();

		const DX12RootParameterSummary *parameter =
			hasParsedRootSignatureSummary ?
				DX12BindingTrackerQuery::FindRootParameter(*cachedSummary, root) :
				nullptr;

		DX12CurrentComputeUavBinding binding;
		binding.rootParameterIndex = root;
		binding.rootDescriptor = true;
		binding.gpuVirtualAddress = rd.address;
		if (parameter) {
			binding.shaderVisibility = parameter->shaderVisibility;
			binding.shaderRegister = parameter->shaderRegister;
			binding.registerSpace = parameter->registerSpace;
		}

		if (rd.type == D3D12_ROOT_PARAMETER_TYPE_UAV)
			localUavs.push_back(std::move(binding));
		else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_SRV)
			localSrvs.push_back(std::move(binding));
		else if (rd.type == D3D12_ROOT_PARAMETER_TYPE_CBV)
			localCbvs.push_back(std::move(binding));
	}

	DX12_HOTPATH_TIME_END(BindCollect);

	const bool foundAny = !localUavs.empty() || !localSrvs.empty() ||
		!localCbvs.empty() || !localConstants.empty();
	bool usesRootValues = !localConstants.empty();
	for (const DX12CurrentComputeUavBinding &binding : localUavs) {
		if (binding.rootDescriptor) {
			usesRootValues = true;
			break;
		}
	}
	if (!usesRootValues) {
		for (const DX12CurrentComputeUavBinding &binding : localSrvs) {
			if (binding.rootDescriptor) {
				usesRootValues = true;
				break;
			}
		}
	}
	if (!usesRootValues) {
		for (const DX12CurrentComputeUavBinding &binding : localCbvs) {
			if (binding.rootDescriptor) {
				usesRootValues = true;
				break;
			}
		}
	}
	const UINT64 descriptorHeapVersionAfter =
		DX12GetDescriptorHeapVersion(snapshot.cbvSrvUavHeap);
	const UINT64 descriptorQuerySerialAfter = CurrentDescriptorQuerySerial();
	const bool descriptorStateStable =
		descriptorHeapVersionAfter == descriptorHeapVersionBefore &&
		descriptorQuerySerialAfter == descriptorQuerySerialBefore;
	if (canStoreRuntimeBindings && allRuntimeDescriptorsStable && descriptorStateStable) {
		std::vector<UINT64> observedSlotVersions(observedSlots.size());
		std::vector<UINT64> observedSlotValueIds(observedSlots.size());
		bool observedSlotsValid = true;
		if (!observedSlots.empty()) {
			observedSlotsValid =
				observedSlotHeap &&
				DX12GetDescriptorSlotIdentities(
					observedSlotHeap,
					observedSlots.data(),
					observedSlots.size(),
					observedSlotVersions.data(),
					observedSlotValueIds.data());
		}
		if (observedSlotsValid) {
			const UINT64 runtimeDependencyFingerprint =
				BuildRuntimePreSkinDependencyFingerprint(
					usesRootValues ? computeBindingSerial :
						runtimeState->computeDescriptorBindingVersion,
					observedSlotHeap,
					observedSlotVersions.data(),
					observedSlotValueIds.data(),
					observedSlots.size());
			StoreRuntimePreSkinBindings(
				runtimeState,
				computeBindingSerial,
				runtimeDependencyFingerprint,
				usesRootValues,
				foundAny,
				observedSlots,
				observedSlotVersions,
				observedSlotValueIds,
				localUavs,
				localSrvs,
				localCbvs,
				localConstants);
			if (dependencyFingerprint)
				*dependencyFingerprint = runtimeDependencyFingerprint;
			DX12ResidentMaintenanceTelemetryUtils::RecordPreSkinBindingCacheStore();
		}
	} else if (!allRuntimeDescriptorsStable || !descriptorStateStable) {
		DX12LogManager::DebugFields("DX12PreSkinRuntimeDescriptorSnapshot",
			"event=skip_cache_unstable,commandList=%p,uavs=%zu,srvs=%zu,cbvs=%zu",
			commandList,
			localUavs.size(),
			localSrvs.size(),
			localCbvs.size());
	}

	if (uavs) *uavs = localUavs;
	if (srvs) *srvs = localSrvs;
	if (cbvs) *cbvs = localCbvs;
	if (constants) *constants = localConstants;

	return foundAny;
}

void DX12BindingTracker::DrainPendingEvents(const char *reason)
{
	DX12BindingCollectQueue::DrainForSnapshot(reason ? reason : "BindingTracker");
}

UINT64 DX12BindingTracker::CurrentCaptureSessionId()
{
	return DX12CaptureLedger::CurrentCaptureSessionId();
}

UINT64 DX12BindingTracker::CaptureFenceSequence()
{
	return DX12BindingCollectQueue::LastSubmittedSequence();
}

bool DX12BindingTracker::PopulateActiveEventSource(
	DX12FrameEventSource *source,
	const char *reason)
{
	if (!source)
		return false;

	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	const UINT64 effectiveCaptureSessionId = captureSessionId != 0 ?
		captureSessionId :
		DX12CaptureLedger::LastCaptureSessionId();
	if (effectiveCaptureSessionId == 0) {
		*source = DX12FrameEventSource();
		return false;
	}
	return DX12BindingTracker::PopulateCaptureEventSource(
		source,
		effectiveCaptureSessionId,
		reason ? reason : "PopulateActiveEventSource");
}

bool DX12BindingTracker::PopulateCaptureEventSource(
	DX12FrameEventSource *source,
	UINT64 captureSessionId,
	const char *reason)
{
	if (!source)
		return false;
	if (captureSessionId == 0) {
		*source = DX12FrameEventSource();
		return false;
	}

	const UINT64 targetSequence = DX12BindingTracker::CaptureFenceSequence();
	DX12BindingCollectQueue::WaitForSubmittedSequence(
		targetSequence,
		reason ? reason : "PopulateCaptureEventSource");

	*source = DX12FrameEventSource();

	DX12CaptureLedgerSnapshot ledgerSnapshot;
	DX12CaptureLedger::GetSnapshot(&ledgerSnapshot);
	if (ledgerSnapshot.captureSessionId != captureSessionId) {
		DX12FrameAnalysis::LogJsonFunc("BindingCaptureSessionMismatch",
			"\"requested_capture_session_id\":%llu,\"snapshot_capture_session_id\":%llu,\"target_sequence\":%llu",
			static_cast<unsigned long long>(captureSessionId),
			static_cast<unsigned long long>(ledgerSnapshot.captureSessionId),
			static_cast<unsigned long long>(targetSequence));
		return false;
	}

	source->captureSessionId = ledgerSnapshot.captureSessionId;
	source->ownedEvents = std::move(ledgerSnapshot.events);
	source->ownedOutputEvents = std::move(ledgerSnapshot.outputEvents);
	source->events = &source->ownedEvents;
	source->outputEvents = &source->ownedOutputEvents;
	source->droppedEvents = ledgerSnapshot.droppedEvents;
	source->droppedOutputEvents = ledgerSnapshot.droppedOutputEvents;

	DX12CaptureLedger::LogSnapshotHealth(
		reason ? reason : "PopulateCaptureEventSource",
		ledgerSnapshot);
	return !source->ownedEvents.empty() || !source->ownedOutputEvents.empty() ||
		source->droppedEvents != 0 || source->droppedOutputEvents != 0;
}

void DX12BindingTracker::ReleaseRetainedIaResources(CommandListBindingState *state)
{
	if (!state)
		return;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (state->vertexBufferResolved[slot] && state->vertexBufferResources[slot].resource) {
			DX12ReleaseRetainedResource(state->vertexBufferResources[slot].resource);
			state->vertexBufferResources[slot] = {};
			state->vertexBufferResolved[slot] = false;
		}
	}
	if (state->indexBufferResolved && state->indexBufferResource.resource) {
		DX12ReleaseRetainedResource(state->indexBufferResource.resource);
		state->indexBufferResource = {};
		state->indexBufferResolved = false;
	}
}

void DX12BindingTracker::RetainEventIaResources(BindingEvent *event)
{
	if (!event)
		return;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (!event->vertexBufferResolved[slot] || !event->vertexBufferResources[slot].resource)
			continue;
		if (!DX12RetainResource(event->vertexBufferResources[slot].resource)) {
			event->vertexBufferResources[slot] = {};
			event->vertexBufferResolved[slot] = false;
		}
	}
	if (event->indexBufferResolved && event->indexBufferResource.resource &&
		!DX12RetainResource(event->indexBufferResource.resource)) {
		event->indexBufferResource = {};
		event->indexBufferResolved = false;
	}
}

void DX12BindingTracker::ReleaseEventIaResources(BindingEvent *event)
{
	if (!event)
		return;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (event->vertexBufferResolved[slot] && event->vertexBufferResources[slot].resource)
			DX12ReleaseRetainedResource(event->vertexBufferResources[slot].resource);
		event->vertexBufferResources[slot] = {};
		event->vertexBufferResolved[slot] = false;
	}
	if (event->indexBufferResolved && event->indexBufferResource.resource)
		DX12ReleaseRetainedResource(event->indexBufferResource.resource);
	event->indexBufferResource = {};
	event->indexBufferResolved = false;
}

void DX12BindingTracker::RetainStateIaResourcesForAsync(CommandListBindingState *state)
{
	if (!state)
		return;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (!state->vertexBufferResolved[slot] || !state->vertexBufferResources[slot].resource)
			continue;
		if (!DX12RetainResource(state->vertexBufferResources[slot].resource)) {
			state->vertexBufferResources[slot] = {};
			state->vertexBufferResolved[slot] = false;
		}
	}
	if (state->indexBufferResolved && state->indexBufferResource.resource &&
		!DX12RetainResource(state->indexBufferResource.resource)) {
		state->indexBufferResource = {};
		state->indexBufferResolved = false;
	}
}

void DX12BindingTracker::RefreshPipelineShaderInfo(CommandListBindingState *state)
{
	if (!state)
		return;

	state->pipelineShaderInfo = {};
	state->pipelineShaderInfoValid = false;
	if (!state->pipelineState)
		return;

	if (DX12GetPipelineStateShaderInfo(state->pipelineState, &state->pipelineShaderInfo))
		state->pipelineShaderInfoValid = true;
}

void DX12BindingTracker::InvalidatePipelineShaderInfo(CommandListBindingState *state)
{
	if (!state)
		return;
	state->pipelineShaderInfo = {};
	state->pipelineShaderInfoValid = false;
}

void DX12BindingTracker::BumpBindingQuerySerial(CommandListBindingState *state)
{
	if (!state)
		return;
	if (state->bindingQuerySerial == UINT64_MAX)
		state->bindingQuerySerial = 1;
	else
		++state->bindingQuerySerial;
}

bool DX12BindingTracker::ResolvePipelineShaderInfoOnDemand(CommandListBindingState *state)
{
	if (!state || !state->pipelineState)
		return false;
	if (state->pipelineShaderInfoValid)
		return true;
	DX12BindingTracker::RefreshPipelineShaderInfo(state);
	return state->pipelineShaderInfoValid;
}

bool DX12BindingTracker::SnapshotBindingState(
	ID3D12GraphicsCommandList *commandList,
	CommandListBindingState *snapshot,
	UINT64 *bindingQuerySerial)
{
	if (!commandList || !snapshot)
		return false;

	const DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	const DX12CommandListTrackingState tracking =
		runtimeState ?
			DX12CommandListRuntime::GetTrackingState(
				const_cast<DX12SimulateContext*>(runtimeState)) :
			DX12CommandListTrackingState();
	if (runtimeState && !tracking.trackBindings) {
		if (!BuildRuntimeBindingSnapshot(runtimeState, snapshot))
			return false;
		if (bindingQuerySerial)
			*bindingQuerySerial =
				snapshot->bindingQuerySerial ? snapshot->bindingQuerySerial : 1;
		return true;
	}

	bool found = false;
	UINT64 serial = 0;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		*snapshot = it->second;
		serial = it->second.bindingQuerySerial;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (runtimeState) {
		CommandListBindingState runtimeSnapshot;
		if (BuildRuntimeBindingSnapshot(runtimeState, &runtimeSnapshot)) {
			if (!found) {
				*snapshot = runtimeSnapshot;
				serial = runtimeSnapshot.bindingQuerySerial;
				found = true;
			} else {
				snapshot->graphicsRootSignature =
					runtimeSnapshot.graphicsRootSignature;
				snapshot->computeRootSignature =
					runtimeSnapshot.computeRootSignature;
				snapshot->cbvSrvUavHeap = runtimeSnapshot.cbvSrvUavHeap;
				snapshot->samplerHeap = runtimeSnapshot.samplerHeap;
				snapshot->runtimeCbvSrvUavHeapResolved =
					runtimeSnapshot.runtimeCbvSrvUavHeapResolved;
				snapshot->runtimeCbvSrvUavHeap =
					runtimeSnapshot.runtimeCbvSrvUavHeap;
				snapshot->runtimeSamplerHeapResolved =
					runtimeSnapshot.runtimeSamplerHeapResolved;
				snapshot->runtimeSamplerHeap =
					runtimeSnapshot.runtimeSamplerHeap;
				snapshot->graphicsTableValidCount =
					runtimeSnapshot.graphicsTableValidCount;
				snapshot->computeTableValidCount =
					runtimeSnapshot.computeTableValidCount;
				memcpy(
					snapshot->graphicsTables,
					runtimeSnapshot.graphicsTables,
					sizeof(snapshot->graphicsTables));
				memcpy(
					snapshot->computeTables,
					runtimeSnapshot.computeTables,
					sizeof(snapshot->computeTables));
				memcpy(
					snapshot->graphicsRootDescriptors,
					runtimeSnapshot.graphicsRootDescriptors,
					sizeof(snapshot->graphicsRootDescriptors));
				memcpy(
					snapshot->computeRootDescriptors,
					runtimeSnapshot.computeRootDescriptors,
					sizeof(snapshot->computeRootDescriptors));
				memcpy(
					snapshot->graphicsRootConstants,
					runtimeSnapshot.graphicsRootConstants,
					sizeof(snapshot->graphicsRootConstants));
				memcpy(
					snapshot->computeRootConstants,
					runtimeSnapshot.computeRootConstants,
					sizeof(snapshot->computeRootConstants));
				serial ^= runtimeSnapshot.bindingQuerySerial;
			}
		}
	}
	if (bindingQuerySerial)
		*bindingQuerySerial = serial ? serial : 1;
	return found;
}

void DX12BindingTracker::ReleaseResolvedIaResource(
	DX12BufferResourceSummary *summary, bool *resolved)
{
	if (!summary || !resolved)
		return;
	if (*resolved && summary->resource)
		DX12ReleaseRetainedResource(summary->resource);
	*summary = {};
	*resolved = false;
}

// Resolve all deferred (unresolved) IA resources in state at draw-snapshot time.
// This is the counterpart to the "defer resource resolution" optimization in
// SetVertexBuffers / SetIndexBuffer. Called inside the shard lock during
// RecordDrawInstanced / RecordDrawIndexedInstanced just before state snapshot.
static void ResolveDeferredIaResources(CommandListBindingState *state)
{
	if (!state)
		return;
	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (state->vertexBufferValid[slot] && !state->vertexBufferResolved[slot]) {
			DX12BindingTracker::ResolveAndRetainIaResource(
				state->vertexBuffers[slot].BufferLocation,
				state->vertexBuffers[slot].SizeInBytes,
				&state->vertexBufferResources[slot],
				&state->vertexBufferResolved[slot]);
		}
	}
	if (state->indexBufferValid && !state->indexBufferResolved) {
		DX12BindingTracker::ResolveAndRetainIaResource(
			state->indexBuffer.BufferLocation,
			state->indexBuffer.SizeInBytes,
			&state->indexBufferResource,
			&state->indexBufferResolved);
	}
}

void DX12BindingTracker::ResolveAndRetainIaResource(
	UINT64 gpuVirtualAddress, UINT64 size,
	DX12BufferResourceSummary *summary, bool *resolved)
{
	if (!summary || !resolved) {
		return;
	}

	*summary = {};
	*resolved = false;
	if (!gpuVirtualAddress || !size)
		return;
	if (!DX12ResolveBufferResourceByGpuVa(gpuVirtualAddress, size, summary))
		return;
	if (summary->resource && !DX12RetainResource(summary->resource)) {
		*summary = {};
		return;
	}
	*resolved = true;
}

bool DX12BindingTracker::BuildEvent(
	BindingEvent *event,
	const char *kind,
	ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state,
	UINT64 descriptorEpoch,
	UINT64 captureSessionId)
{
	if (!event)
		return false;
	*event = BindingEvent();
	event->kind = kind ? kind : "";
	event->captureSessionId = captureSessionId;
	const bool isDrawKind = kind && (!strcmp(kind, "draw") || !strcmp(kind, "draw_indexed"));
	const bool isDispatchKind = kind && !strcmp(kind, "dispatch");
	event->drawId = isDrawKind ? state.drawSerial : 0;
	event->dispatchId = isDispatchKind ? state.dispatchSerial : 0;
	event->descriptorEpoch = descriptorEpoch;
	event->commandList = commandList;
	event->pipelineState = state.pipelineState;
	event->graphicsRootSignature = state.graphicsRootSignature;
	event->computeRootSignature = state.computeRootSignature;
	event->cbvSrvUavHeap = state.cbvSrvUavHeap;
	event->samplerHeap = state.samplerHeap;
	if (isDrawKind) {
		// Draw events only need graphics state
		memcpy(event->graphicsTables, state.graphicsTables, sizeof(event->graphicsTables));
		memcpy(event->graphicsRootDescriptors, state.graphicsRootDescriptors, sizeof(event->graphicsRootDescriptors));
		memcpy(event->graphicsRootConstants, state.graphicsRootConstants, sizeof(event->graphicsRootConstants));
		memcpy(event->vertexBuffers, state.vertexBuffers, sizeof(event->vertexBuffers));
		memcpy(event->vertexBufferValid, state.vertexBufferValid, sizeof(event->vertexBufferValid));
		memcpy(event->vertexBufferResources, state.vertexBufferResources, sizeof(event->vertexBufferResources));
		memcpy(event->vertexBufferResolved, state.vertexBufferResolved, sizeof(event->vertexBufferResolved));
	} else if (isDispatchKind) {
		// Dispatch events only need compute state
		memcpy(event->computeTables, state.computeTables, sizeof(event->computeTables));
		memcpy(event->computeRootDescriptors, state.computeRootDescriptors, sizeof(event->computeRootDescriptors));
		memcpy(event->computeRootConstants, state.computeRootConstants, sizeof(event->computeRootConstants));
	} else {
		// Other event kinds: copy everything
		memcpy(event->graphicsTables, state.graphicsTables, sizeof(event->graphicsTables));
		memcpy(event->computeTables, state.computeTables, sizeof(event->computeTables));
		memcpy(event->graphicsRootDescriptors, state.graphicsRootDescriptors, sizeof(event->graphicsRootDescriptors));
		memcpy(event->computeRootDescriptors, state.computeRootDescriptors, sizeof(event->computeRootDescriptors));
		memcpy(event->graphicsRootConstants, state.graphicsRootConstants, sizeof(event->graphicsRootConstants));
		memcpy(event->computeRootConstants, state.computeRootConstants, sizeof(event->computeRootConstants));
		memcpy(event->vertexBuffers, state.vertexBuffers, sizeof(event->vertexBuffers));
		memcpy(event->vertexBufferValid, state.vertexBufferValid, sizeof(event->vertexBufferValid));
		memcpy(event->vertexBufferResources, state.vertexBufferResources, sizeof(event->vertexBufferResources));
		memcpy(event->vertexBufferResolved, state.vertexBufferResolved, sizeof(event->vertexBufferResolved));
	}
	event->indexBuffer = state.indexBuffer;
	event->indexBufferValid = state.indexBufferValid;
	event->indexBufferResource = state.indexBufferResource;
	event->indexBufferResolved = state.indexBufferResolved;
	event->primitiveTopology = state.primitiveTopology;
	if (state.pipelineState) {
		if (state.pipelineShaderInfoValid) {
			event->shaderInfo = state.pipelineShaderInfo;
		} else {
			DX12GetPipelineStateShaderInfo(state.pipelineState, &event->shaderInfo);
		}
		ID3D12RootSignature *psoRootSignature = nullptr;
		if (DX12GetPsoRootSignature(event->shaderInfo.psoIndex, &psoRootSignature)) {
			if (!event->graphicsRootSignature)
				event->graphicsRootSignature = psoRootSignature;
			if (!event->computeRootSignature)
				event->computeRootSignature = psoRootSignature;
		}
	}
	return true;
}

bool DX12BindingTracker::BuildEvent(
	BindingEvent *event, const char *kind, ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state)
{
	return DX12BindingTracker::BuildEvent(
		event,
		kind,
		commandList,
		state,
		CurrentDescriptorEpoch(),
		DX12BindingTracker::CurrentCaptureSessionId());
}

void DX12BindingTracker::StoreEventLocked(const BindingEvent &event)
{
	DX12BindingTracker::RecordTemplateEvent(event.commandList, event);
}

void DX12BindingTracker::StoreEventLocked(
	const char *kind, ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state)
{
	BindingEvent event;
	if (!DX12BindingTracker::BuildEvent(&event, kind, commandList, state))
		return;
	DX12BindingTracker::StoreEventLocked(event);
}

void DX12BindingTracker::StoreOutputEventLocked(
	const char *kind, ID3D12GraphicsCommandList *commandList,
	const CommandListBindingState &state, UINT slot, SIZE_T cpuHandle, UINT64 drawId, UINT64 captureSessionId)
{
	if (!cpuHandle)
		return;
	const UINT64 dispatchId = 0;
	OutputBindingEvent event;
	event.serial = 0;
	event.captureSessionId = captureSessionId;
	event.kind = kind ? kind : "";
	event.commandList = commandList;
	event.pipelineState = state.pipelineState;
	event.drawId = drawId;
	event.dispatchId = dispatchId;
	event.slot = slot;
	event.cpuHandle = cpuHandle;
	if (state.pipelineState) {
		if (state.pipelineShaderInfoValid)
			event.shaderInfo = state.pipelineShaderInfo;
		else
			DX12GetPipelineStateShaderInfo(state.pipelineState, &event.shaderInfo);
	}
	DX12BindingTracker::RecordTemplateOutputEvent(commandList, event);
}

void DX12BindingTracker::StoreCurrentOutputTargetsLocked(
	ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state, UINT64 captureSessionId)
{
	if (captureSessionId == 0)
		captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	for (UINT i = 0; i < MaxRenderTargetSlots; ++i) {
		if (state.renderTargetValid[i])
			DX12BindingTracker::StoreOutputEventLocked("RTV", commandList, state, i,
				state.renderTargets[i].ptr, state.drawSerial, captureSessionId);
	}
	if (state.depthStencilValid)
		DX12BindingTracker::StoreOutputEventLocked("DSV", commandList, state, 0,
			state.depthStencil.ptr, state.drawSerial, captureSessionId);
}

void DX12BindingTracker::ReleaseRecordedEvent(CommandListRecordedEvent *event)
{
	if (!event)
		return;
	if (event->kind == CommandListRecordedEventKind::Binding)
		DX12BindingTracker::ReleaseEventIaResources(&event->binding);
}

void DX12BindingTracker::ReleaseRecordedEventsLocked(
	BindingShard *shard, ID3D12GraphicsCommandList *commandList)
{
	if (!shard || !commandList)
		return;
	auto it = shard->recordedEvents.find(commandList);
	if (it == shard->recordedEvents.end())
		return;
	for (CommandListRecordedEvent &event : it->second)
		DX12BindingTracker::ReleaseRecordedEvent(&event);
	shard->recordedEvents.erase(it);
}

void DX12BindingTracker::RecordTemplateEvent(
	ID3D12GraphicsCommandList *commandList, const BindingEvent &event)
{
	if (!commandList)
		return;
	CommandListRecordedEvent recorded;
	recorded.kind = CommandListRecordedEventKind::Binding;
	recorded.binding = event;
	recorded.binding.commandList = commandList;
	recorded.binding.captureSessionId = 0;
	recorded.binding.serial = 0;
	DX12BindingTracker::RetainEventIaResources(&recorded.binding);
	const std::string kind = recorded.binding.kind;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::vector<CommandListRecordedEvent> &events = shard.recordedEvents[commandList];
	events.push_back(std::move(recorded));
	const size_t templateCount = events.size();
	ReleaseSRWLockExclusive(&shard.lock);

	DX12FrameAnalysis::LogJsonFunc("BindingRecordTemplateEvent",
		"\"commandList\":\"%p\",\"kind\":\"%s\",\"templates\":%zu",
		commandList, kind.c_str(), templateCount);
}

void DX12BindingTracker::RecordTemplateOutputEvent(
	ID3D12GraphicsCommandList *commandList, const OutputBindingEvent &event)
{
	if (!commandList)
		return;
	CommandListRecordedEvent recorded;
	recorded.kind = CommandListRecordedEventKind::Output;
	recorded.output = event;
	recorded.output.commandList = commandList;
	recorded.output.captureSessionId = 0;
	recorded.output.serial = 0;
	const std::string kind = recorded.output.kind;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	std::vector<CommandListRecordedEvent> &events = shard.recordedEvents[commandList];
	events.push_back(std::move(recorded));
	const size_t templateCount = events.size();
	ReleaseSRWLockExclusive(&shard.lock);

	DX12FrameAnalysis::LogJsonFunc("BindingRecordTemplateOutputEvent",
		"\"commandList\":\"%p\",\"kind\":\"%s\",\"templates\":%zu",
		commandList, kind.c_str(), templateCount);
}

void DX12BindingTracker::StoreEventInCaptureLedger(const BindingEvent &event)
{
	BindingEvent stored = event;
	DX12BindingTracker::RetainEventIaResources(&stored);
	stored.serial = 0;
	const DX12CaptureLedgerRecordResult ledgerResult =
		DX12CaptureLedger::RecordEvent(&stored);
	if (ledgerResult == DX12CaptureLedgerRecordResult::Stored)
		return;
	DX12BindingTracker::ReleaseEventIaResources(&stored);
	DX12FrameAnalysis::LogJsonFunc("BindingLedgerEventSkipped",
		"\"commandList\":\"%p\",\"kind\":\"%s\",\"captureSessionId\":%llu,\"result\":%u",
		stored.commandList,
		stored.kind.c_str(),
		static_cast<unsigned long long>(stored.captureSessionId),
		static_cast<UINT>(ledgerResult));
}

void DX12BindingTracker::StoreOutputEventInCaptureLedger(const OutputBindingEvent &event)
{
	OutputBindingEvent stored = event;
	stored.serial = 0;
	const DX12CaptureLedgerRecordResult ledgerResult =
		DX12CaptureLedger::TryStoreOutputEvent(&stored);
	if (ledgerResult == DX12CaptureLedgerRecordResult::Stored ||
		ledgerResult == DX12CaptureLedgerRecordResult::Duplicate)
		return;
	DX12FrameAnalysis::LogJsonFunc("BindingLedgerOutputSkipped",
		"\"commandList\":\"%p\",\"kind\":\"%s\",\"captureSessionId\":%llu,\"result\":%u",
		stored.commandList,
		stored.kind.c_str(),
		static_cast<unsigned long long>(stored.captureSessionId),
		static_cast<UINT>(ledgerResult));
}

void DX12BindingTracker::SubmitRecordedEvent(
	const CommandListRecordedEvent &recorded, UINT64 captureSessionId)
{
	if (captureSessionId == 0)
		return;
	if (recorded.kind == CommandListRecordedEventKind::Binding) {
		BindingEvent event = recorded.binding;
		event.captureSessionId = captureSessionId;
		event.serial = 0;
		DX12BindingTracker::StoreEventInCaptureLedger(event);
		return;
	}
	OutputBindingEvent event = recorded.output;
	event.captureSessionId = captureSessionId;
	event.serial = 0;
	DX12BindingTracker::StoreOutputEventInCaptureLedger(event);
}

void DX12BindingTracker::SubmitCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (captureSessionId == 0)
		return;
	const UINT64 targetSequence = DX12BindingCollectQueue::LastSubmittedSequence();
	if (targetSequence != 0) {
		DX12BindingCollectQueue::WaitForSubmittedSequence(
			targetSequence,
			"SubmitCommandList");
	}

	std::vector<CommandListRecordedEvent> events;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.recordedEvents.find(commandList);
	if (it != shard.recordedEvents.end()) {
		events = it->second;
		for (CommandListRecordedEvent &event : events) {
			if (event.kind == CommandListRecordedEventKind::Binding)
				DX12BindingTracker::RetainEventIaResources(&event.binding);
		}
	}
	ReleaseSRWLockShared(&shard.lock);

	for (const CommandListRecordedEvent &event : events)
		DX12BindingTracker::SubmitRecordedEvent(event, captureSessionId);
	for (CommandListRecordedEvent &event : events)
		DX12BindingTracker::ReleaseRecordedEvent(&event);

	DX12FrameAnalysis::LogJsonFunc("BindingSubmitCommandList",
		"\"commandList\":\"%p\",\"captureSessionId\":%llu,\"templates\":%zu",
		commandList,
		static_cast<unsigned long long>(captureSessionId),
		events.size());
}

bool DX12BindingTracker::RegisterCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return false;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	const bool inserted = shard.states.try_emplace(commandList).second;
	if (inserted) {
		// Bump generation to invalidate any TLS-cached state pointers
		// that may be invalidated by unordered_map rehash.
		shard.generation++;
	}
	ReleaseSRWLockExclusive(&shard.lock);
	return inserted;
}

bool DX12BindingTracker::UnregisterCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return false;

	bool removedState = false;
	bool removedEvents = false;
	size_t recordedEventCount = 0;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	auto eventIt = shard.recordedEvents.find(commandList);
	if (eventIt != shard.recordedEvents.end()) {
		recordedEventCount = eventIt->second.size();
		removedEvents = true;
	}
	DX12BindingTracker::ReleaseRecordedEventsLocked(&shard, commandList);

	auto stateIt = shard.states.find(commandList);
	if (stateIt != shard.states.end()) {
		DX12BindingTracker::ReleaseRetainedIaResources(&stateIt->second);
		shard.states.erase(stateIt);
		removedState = true;
	}
	if (removedState || removedEvents)
		shard.generation++;
	ReleaseSRWLockExclusive(&shard.lock);

	if (tlsGraphicsTableCache.cl == commandList)
		tlsGraphicsTableCache.cl = nullptr;
	if (tlsComputeTableCache.cl == commandList)
		tlsComputeTableCache.cl = nullptr;
	if (tlsGraphicsDescCache.cl == commandList)
		tlsGraphicsDescCache.cl = nullptr;
	if (tlsComputeDescCache.cl == commandList)
		tlsComputeDescCache.cl = nullptr;
	for (BindingTlsDescriptorQueryCache &queryCache : tlsDescriptorQueryCaches) {
		if (queryCache.commandList == commandList)
			queryCache.commandList = nullptr;
	}
	if (tlsComputeAllQueryCache.commandList == commandList)
		tlsComputeAllQueryCache.commandList = nullptr;
	if (tlsStateCache.commandList == commandList) {
		tlsStateCache.commandList = nullptr;
		tlsStateCache.state = nullptr;
	}

	if (removedState || removedEvents) {
		DX12LogManager::Fields("DX12BindingTracker",
			"stage=unregister_command_list,commandList=%p,state=%u,recordedEvents=%zu",
			commandList,
			removedState ? 1u : 0u,
			recordedEventCount);
	}
	return removedState || removedEvents;
}

void DX12BindingTracker::ResetCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState)
{
	if (!commandList)
		return;

	const DX12CommandListTrackingState tracking =
		DX12CommandListStateCapture::GetTrackingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	DX12BindingTracker::ReleaseRecordedEventsLocked(&shard, commandList);
	DX12BindingTracker::ReleaseRetainedIaResources(&state);
	state = CommandListBindingState();
	state.pipelineState = initialState;
	if (tracking.recordBindingEvents)
		DX12BindingTracker::RefreshPipelineShaderInfo(&state);
	ReleaseSRWLockExclusive(&shard.lock);

	tlsGraphicsTableCache.cl = nullptr;
	tlsComputeTableCache.cl = nullptr;
	tlsGraphicsDescCache.cl = nullptr;
	tlsComputeDescCache.cl = nullptr;
	for (BindingTlsDescriptorQueryCache &queryCache : tlsDescriptorQueryCaches)
		queryCache.commandList = nullptr;
	tlsComputeAllQueryCache.commandList = nullptr;
	tlsStateCache.commandList = nullptr;
	tlsStateCache.state = nullptr;
}

void DX12BindingTracker::SetPipelineState(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (!commandList)
		return;

	DX12ResidentMaintenanceTelemetryUtils::RecordPsoCall();
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	if (state.pipelineState == pipelineState) {
		DX12ResidentMaintenanceTelemetryUtils::RecordPsoNoChange();
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	state.pipelineState = pipelineState;
	DX12BindingTracker::InvalidatePipelineShaderInfo(&state);
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	DX12ResidentMaintenanceTelemetryUtils::RecordPsoChanged();
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::SetGraphicsRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (!commandList)
		return;

	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	if (state.graphicsRootSignature == rootSignature) {
		DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootSignature(false);
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	state.graphicsRootSignature = rootSignature;
	// D3D12 root arguments become stale when the root signature changes. Keep
	// Binding State subject to the same rule so a table from the previous root
	// signature can never be matched or replayed against the new layout.
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		state.graphicsTables[root] = RootTableState();
		state.graphicsRootDescriptors[root] = RootDescriptorState();
		state.graphicsRootConstants[root] = RootConstantsState();
	}
	state.graphicsTableValidCount = 0;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootSignature(true);
	ReleaseSRWLockExclusive(&shard.lock);
	tlsGraphicsTableCache.cl = nullptr;
	tlsGraphicsDescCache.cl = nullptr;
}

void DX12BindingTracker::SetComputeRootSignature(
	ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature)
{
	if (!commandList)
		return;

	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	if (state.computeRootSignature == rootSignature) {
		DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootSignature(false);
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	state.computeRootSignature = rootSignature;
	for (UINT root = 0; root < MaxRootParameters; ++root) {
		state.computeTables[root] = RootTableState();
		state.computeRootDescriptors[root] = RootDescriptorState();
		state.computeRootConstants[root] = RootConstantsState();
	}
	state.computeTableValidCount = 0;
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootSignature(true);
	ReleaseSRWLockExclusive(&shard.lock);
	tlsComputeTableCache.cl = nullptr;
	tlsComputeDescCache.cl = nullptr;
}

void DX12BindingTracker::RecordStateEvent(ID3D12GraphicsCommandList *commandList, const char *kind)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot))
		return;
	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueStateEvent(
			commandList,
			snapshot,
			kind ? kind : "state",
			descriptorEpoch,
			captureSessionId))
		return;

	BindingEvent event;
	if (DX12BindingTracker::BuildEvent(
			&event,
			kind ? kind : "state",
			commandList,
			snapshot,
			descriptorEpoch,
			captureSessionId))
		DX12BindingTracker::StoreEventLocked(event);
}

bool DX12BindingTracker::SetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList, UINT count,
	ID3D12DescriptorHeap *const *heaps)
{
	if (!commandList)
		return false;

	DX12ResidentMaintenanceTelemetryUtils::RecordDescriptorHeapResolve(count);
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;
	for (UINT i = 0; i < count && heaps; ++i) {
		ID3D12DescriptorHeap *heap = heaps[i];
		if (!heap)
			continue;
		D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
		if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			cbvSrvUavHeap = heap;
		else if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
			samplerHeap = heap;
	}

	return DX12BindingTracker::SetDescriptorHeaps(commandList, cbvSrvUavHeap, samplerHeap);
}

bool DX12BindingTracker::SetDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap *cbvSrvUavHeap,
	ID3D12DescriptorHeap *samplerHeap)
{
	if (!commandList)
		return false;

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	if (state.cbvSrvUavHeap == cbvSrvUavHeap &&
	    state.samplerHeap == samplerHeap) {
		DX12ResidentMaintenanceTelemetryUtils::RecordDescriptorHeapsNoChange();
		ReleaseSRWLockExclusive(&shard.lock);
		return false;
	}
	UINT invalidatedGraphicsTables = state.graphicsTableValidCount;
	UINT invalidatedComputeTables = state.computeTableValidCount;
	if (state.graphicsTableValidCount || state.computeTableValidCount) {
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			if (state.graphicsTables[root].valid)
				state.graphicsTables[root] = RootTableState();
			if (state.computeTables[root].valid)
				state.computeTables[root] = RootTableState();
		}
		state.graphicsTableValidCount = 0;
		state.computeTableValidCount = 0;
	}
	state.cbvSrvUavHeap = cbvSrvUavHeap;
	state.samplerHeap = samplerHeap;
	// Keep the hot path to pointer swaps only. Heap summaries are resolved
	// lazily by the query path when a capture or replacement actually needs to
	// inspect descriptor contents.
	DX12BindingTrackerQueryCacheUtils::StoreRuntimeHeapSummaries(
		&state,
		nullptr,
		false,
		nullptr,
		false);
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	tlsGraphicsTableCache.cl = nullptr;
	tlsComputeTableCache.cl = nullptr;
	DX12ResidentMaintenanceTelemetryUtils::RecordDescriptorHeapsChanged(
		invalidatedGraphicsTables, invalidatedComputeTables);
	DX12LogManager::DebugFields("DX12BindingDescriptorHeapsChanged",
		"commandList=%p,cbvSrvUavHeap=%p,samplerHeap=%p,invalidatedGraphicsTables=%u,invalidatedComputeTables=%u",
		commandList,
		cbvSrvUavHeap,
		samplerHeap,
		invalidatedGraphicsTables,
		invalidatedComputeTables);
	return true;
}

void DX12BindingTracker::SetGraphicsRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;
	DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootTableCall();
	if (tlsGraphicsTableCache.cl == commandList &&
		tlsGraphicsTableCache.valid[rootParameterIndex] &&
		tlsGraphicsTableCache.gpuHandle[rootParameterIndex] == baseDescriptor.ptr)
	{
		DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootTableTlsHit();
		return;
	}

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootTableState &stateTable = state.graphicsTables[rootParameterIndex];
	if (stateTable.valid && stateTable.baseDescriptor.ptr == baseDescriptor.ptr) {
		ReleaseSRWLockExclusive(&shard.lock);
		if (tlsGraphicsTableCache.cl != commandList) {
			memset(tlsGraphicsTableCache.valid, 0, sizeof(tlsGraphicsTableCache.valid));
			memset(tlsGraphicsTableCache.gpuHandle, 0, sizeof(tlsGraphicsTableCache.gpuHandle));
			tlsGraphicsTableCache.cl = commandList;
		}
		tlsGraphicsTableCache.valid[rootParameterIndex] = true;
		tlsGraphicsTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
		DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootTableNoChange();
		return;
	}
	if (!stateTable.valid)
		++state.graphicsTableValidCount;
	stateTable = RootTableState();
	stateTable.valid = true;
	stateTable.rootParameterIndex = rootParameterIndex;
	stateTable.baseDescriptor = baseDescriptor;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	if (tlsGraphicsTableCache.cl != commandList) {
		memset(tlsGraphicsTableCache.valid, 0, sizeof(tlsGraphicsTableCache.valid));
		memset(tlsGraphicsTableCache.gpuHandle, 0, sizeof(tlsGraphicsTableCache.gpuHandle));
		tlsGraphicsTableCache.cl = commandList;
	}
	tlsGraphicsTableCache.valid[rootParameterIndex] = true;
	tlsGraphicsTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
	DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootTableChanged();
}

void DX12BindingTracker::SetComputeRootDescriptorTable(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;
	DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootTableCall();
	if (tlsComputeTableCache.cl == commandList &&
		tlsComputeTableCache.valid[rootParameterIndex] &&
		tlsComputeTableCache.gpuHandle[rootParameterIndex] == baseDescriptor.ptr)
	{
		DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootTableTlsHit();
		return;
	}

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootTableState &table = state.computeTables[rootParameterIndex];
	if (table.valid && table.baseDescriptor.ptr == baseDescriptor.ptr) {
		ReleaseSRWLockExclusive(&shard.lock);
		if (tlsComputeTableCache.cl != commandList) {
			memset(tlsComputeTableCache.valid, 0, sizeof(tlsComputeTableCache.valid));
			memset(tlsComputeTableCache.gpuHandle, 0, sizeof(tlsComputeTableCache.gpuHandle));
			tlsComputeTableCache.cl = commandList;
		}
		tlsComputeTableCache.valid[rootParameterIndex] = true;
		tlsComputeTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
		DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootTableNoChange();
		return;
	}
	if (!table.valid)
		++state.computeTableValidCount;
	table = RootTableState();
	table.valid = true;
	table.rootParameterIndex = rootParameterIndex;
	table.baseDescriptor = baseDescriptor;
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	if (tlsComputeTableCache.cl != commandList) {
		memset(tlsComputeTableCache.valid, 0, sizeof(tlsComputeTableCache.valid));
		memset(tlsComputeTableCache.gpuHandle, 0, sizeof(tlsComputeTableCache.gpuHandle));
		tlsComputeTableCache.cl = commandList;
	}
	tlsComputeTableCache.valid[rootParameterIndex] = true;
	tlsComputeTableCache.gpuHandle[rootParameterIndex] = baseDescriptor.ptr;
	DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootTableChanged();
}

void DX12BindingTracker::SetPrimitiveTopology(
	ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (!commandList)
		return;

	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	if (state.primitiveTopology == topology) {
		DX12ResidentMaintenanceTelemetryUtils::RecordPrimitiveTopology(false);
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}
	state.primitiveTopology = topology;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	DX12ResidentMaintenanceTelemetryUtils::RecordPrimitiveTopology(true);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::SetIndexBuffer(
	ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view)
{
	if (!commandList)
		return;

	// Use TLS-cached state pointer. Resource resolution deferred to draw time.
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;

	const bool newValid = view != nullptr;
	const bool same =
		state.indexBufferValid == newValid &&
		(!newValid ||
			(state.indexBuffer.BufferLocation == view->BufferLocation &&
				state.indexBuffer.SizeInBytes == view->SizeInBytes &&
				state.indexBuffer.Format == view->Format));
	if (same) {
		DX12ResidentMaintenanceTelemetryUtils::RecordIndexBuffer(false);
		ReleaseSRWLockExclusive(&shard.lock);
		return;
	}

	const bool hadResolved = state.indexBufferResolved;
	DX12BindingTracker::ReleaseResolvedIaResource(
		&state.indexBufferResource, &state.indexBufferResolved);
	if (hadResolved)
		DX12ResidentMaintenanceTelemetryUtils::RecordIndexBufferResolvedReleased();
	if (view) {
		state.indexBuffer = *view;
		state.indexBufferValid = true;
	} else {
		state.indexBuffer = {};
		state.indexBufferValid = false;
	}
	// Resource resolution DEFERRED to draw time; see SetVertexBuffers comment.
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	DX12ResidentMaintenanceTelemetryUtils::RecordIndexBuffer(true);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::SetVertexBuffers(
	ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count,
	const D3D12_VERTEX_BUFFER_VIEW *views)
{
	if (!commandList || startSlot >= MaxVertexBufferSlots)
		return;

	const UINT trackedCount = min(count, MaxVertexBufferSlots - startSlot);
	DX12ResidentMaintenanceTelemetryUtils::RecordVertexBufferCall(trackedCount);

	// Use TLS-cached state pointer to avoid ShardFor() hash + HashMap lookup.
	// Still acquire lock for mutation safety (negligible cost when uncontended,
	// ~20ns vs ~11us of the old eager resource resolution per slot).
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	// Re-validate after lock: unordered_map may have rehashed while we were
	// waiting on the lock (extremely rare, but generation guard handles it).
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;

	bool anyChanged = false;
	for (UINT i = 0; i < trackedCount; ++i) {
		const UINT slot = startSlot + i;
		D3D12_VERTEX_BUFFER_VIEW newView = {};
		bool newValid = false;
		if (views) {
			newView = views[i];
			newValid = newView.BufferLocation != 0 && newView.SizeInBytes != 0;
		}
		const bool same =
			state.vertexBufferValid[slot] == newValid &&
			state.vertexBuffers[slot].BufferLocation == newView.BufferLocation &&
			state.vertexBuffers[slot].SizeInBytes == newView.SizeInBytes &&
			state.vertexBuffers[slot].StrideInBytes == newView.StrideInBytes;
		if (same) {
			DX12ResidentMaintenanceTelemetryUtils::RecordVertexBufferSlot(false);
			continue;
		}

		const bool hadResolved = state.vertexBufferResolved[slot];
		DX12BindingTracker::ReleaseResolvedIaResource(
			&state.vertexBufferResources[slot],
			&state.vertexBufferResolved[slot]);
		if (hadResolved)
			DX12ResidentMaintenanceTelemetryUtils::RecordVertexBufferResolvedReleased();
		state.vertexBuffers[slot] = newView;
		state.vertexBufferValid[slot] = newValid;
		DX12ResidentMaintenanceTelemetryUtils::RecordVertexBufferSlot(true);
		anyChanged = true;
		// Resource resolution DEFERRED to draw time (RecordDrawInstanced /
		// RecordDrawIndexedInstanced). This is a ~90% cost reduction per call:
		//   Old: ReleaseResolvedIaResource + ResolveAndRetainIaResource
		//        -> DX12ReleaseRetainedResource + DX12ResolveBufferResourceByGpuVa + DX12RetainResource
		//   New: trivial struct copy (D3D12_VERTEX_BUFFER_VIEW = 3x UINT64)
	}
	if (anyChanged)
		DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::RecordOutputTargets(
	ID3D12GraphicsCommandList *commandList, UINT numRenderTargetDescriptors,
	const D3D12_CPU_DESCRIPTOR_HANDLE *renderTargetDescriptors,
	BOOL singleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *depthStencilDescriptor)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	bool found = false;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (!found)
		snapshot = CommandListBindingState();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	for (UINT i = 0; i < numRenderTargetDescriptors && renderTargetDescriptors; ++i) {
		D3D12_CPU_DESCRIPTOR_HANDLE handle = renderTargetDescriptors[i];
		if (singleHandleToDescriptorRange && i > 0) {
			DX12DescriptorHeapSummary heap = {};
			if (!DX12FindDescriptorHeapByCpuHandle(
					renderTargetDescriptors[0], D3D12_DESCRIPTOR_HEAP_TYPE_RTV, &heap) ||
				!heap.increment)
				continue;
			handle.ptr = renderTargetDescriptors[0].ptr +
				static_cast<SIZE_T>(i) * heap.increment;
		}
		if (DX12BindingCollectQueue::EnqueueOutputEvent(
				commandList,
				snapshot,
				"RTV",
				i,
				handle.ptr,
				0,
				captureSessionId))
			continue;
		DX12BindingTracker::StoreOutputEventLocked(
			"RTV",
			commandList,
			snapshot,
			i,
			handle.ptr,
			0,
			captureSessionId);
	}
	if (depthStencilDescriptor &&
		!DX12BindingCollectQueue::EnqueueOutputEvent(commandList, snapshot, "DSV", 0,
			depthStencilDescriptor->ptr, 0, captureSessionId)) {
		DX12BindingTracker::StoreOutputEventLocked("DSV", commandList, snapshot, 0,
			depthStencilDescriptor->ptr, 0, captureSessionId);
	}

	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	for (UINT i = 0; i < MaxRenderTargetSlots; ++i) {
		state.renderTargets[i] = {};
		state.renderTargetValid[i] = false;
	}
	for (UINT i = 0; i < numRenderTargetDescriptors && i < MaxRenderTargetSlots; ++i) {
		D3D12_CPU_DESCRIPTOR_HANDLE handle = {};
		if (renderTargetDescriptors) {
			handle = renderTargetDescriptors[i];
			if (singleHandleToDescriptorRange && i > 0) {
				DX12DescriptorHeapSummary heap = {};
				if (DX12FindDescriptorHeapByCpuHandle(
						renderTargetDescriptors[0], D3D12_DESCRIPTOR_HEAP_TYPE_RTV, &heap) &&
					heap.increment) {
					handle.ptr = renderTargetDescriptors[0].ptr +
						static_cast<SIZE_T>(i) * heap.increment;
				}
			}
		}
		state.renderTargets[i] = handle;
		state.renderTargetValid[i] = handle.ptr != 0;
	}
	if (depthStencilDescriptor) {
		state.depthStencil = *depthStencilDescriptor;
		state.depthStencilValid = depthStencilDescriptor->ptr != 0;
	} else {
		state.depthStencil = {};
		state.depthStencilValid = false;
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::RecordRenderTargetClear(
	ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView)
{
	if (!commandList || !renderTargetView.ptr)
		return;

	CommandListBindingState snapshot;
	bool found = false;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (!found)
		return;
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueOutputEvent(
			commandList,
			snapshot,
			"RTV",
			0,
			renderTargetView.ptr,
			0,
			captureSessionId))
		return;

	DX12BindingTracker::StoreOutputEventLocked(
		"RTV",
		commandList,
		snapshot,
		0,
		renderTargetView.ptr,
		0,
		captureSessionId);
}

void DX12BindingTracker::RecordDepthStencilClear(
	ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView)
{
	if (!commandList || !depthStencilView.ptr)
		return;

	CommandListBindingState snapshot;
	bool found = false;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (!found)
		return;
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueOutputEvent(
			commandList,
			snapshot,
			"DSV",
			0,
			depthStencilView.ptr,
			0,
			captureSessionId))
		return;

	DX12BindingTracker::StoreOutputEventLocked(
		"DSV",
		commandList,
		snapshot,
		0,
		depthStencilView.ptr,
		0,
		captureSessionId);
}

bool DX12BindingTracker::GetCurrentIaState(
	ID3D12GraphicsCommandList *commandList, DX12CurrentIaState *state)
{
	if (!commandList || !state)
		return false;

	*state = DX12CurrentIaState();
	CommandListBindingState snapshot;
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot))
		return false;

	for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
		if (!snapshot.vertexBufferValid[slot])
			continue;
		const D3D12_VERTEX_BUFFER_VIEW &view = snapshot.vertexBuffers[slot];
		DX12CurrentIaBuffer buffer;
		buffer.role = "VB";
		buffer.gpuVa = view.BufferLocation;
		buffer.size = view.SizeInBytes;
		buffer.stride = view.StrideInBytes;
		buffer.slot = slot;
		buffer.resolved = snapshot.vertexBufferResolved[slot];
		buffer.resource = snapshot.vertexBufferResources[slot];
		if (!buffer.resolved)
			buffer.resolved = DX12ResolveBufferResourceByGpuVa(
				buffer.gpuVa, buffer.size, &buffer.resource);
		state->vertexBuffers.push_back(buffer);
	}

	if (snapshot.indexBufferValid && snapshot.indexBuffer.BufferLocation &&
		snapshot.indexBuffer.SizeInBytes) {
		state->hasIndexBuffer = true;
		state->indexBuffer.role = "IB";
		state->indexBuffer.gpuVa = snapshot.indexBuffer.BufferLocation;
		state->indexBuffer.size = snapshot.indexBuffer.SizeInBytes;
		state->indexBuffer.format = static_cast<UINT>(snapshot.indexBuffer.Format);
		state->indexBuffer.resolved = snapshot.indexBufferResolved;
		state->indexBuffer.resource = snapshot.indexBufferResource;
		if (!state->indexBuffer.resolved)
			state->indexBuffer.resolved = DX12ResolveBufferResourceByGpuVa(
				state->indexBuffer.gpuVa, state->indexBuffer.size, &state->indexBuffer.resource);
	}

	return true;
}


bool DX12BindingTracker::GetCurrentDescriptors(
	ID3D12GraphicsCommandList *commandList,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	const char *descriptorKind,
	D3D12_ROOT_PARAMETER_TYPE rootDescriptorType,
	std::vector<DX12CurrentComputeUavBinding> *uavs)
{
	if (!commandList || !uavs)
		return false;

	uavs->clear();
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	const DX12CommandListTrackingState tracking =
		runtimeState ?
			DX12CommandListRuntime::GetTrackingState(runtimeState) :
			DX12CommandListTrackingState();
	const bool preferRuntimeSnapshot = runtimeState && !tracking.trackBindings;
	if (preferRuntimeSnapshot) {
		const UINT64 runtimeBindingQuerySerial =
			GetRuntimeDescriptorQuerySerial(runtimeState, compute);
		bool cachedFoundAny = false;
		if (TryLoadRuntimeDescriptorBindings(
				runtimeState,
				compute,
				rangeType,
				runtimeBindingQuerySerial,
				uavs,
				&cachedFoundAny)) {
			return cachedFoundAny;
		}
	}

	CommandListBindingState snapshot;
	UINT64 bindingQuerySerial = 0;
	const bool snapshotReady = preferRuntimeSnapshot ?
		BuildRuntimeBindingSnapshot(runtimeState, &snapshot) :
		DX12BindingTracker::SnapshotBindingState(
			commandList, &snapshot, &bindingQuerySerial);
	if (!snapshotReady)
		return false;
	if (preferRuntimeSnapshot)
		bindingQuerySerial = GetRuntimeDescriptorQuerySerial(runtimeState, compute);
	const ID3D12DescriptorHeap *cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
	const UINT64 cbvSrvUavHeapVersion = DX12GetDescriptorHeapVersion(
		snapshot.cbvSrvUavHeap);
	const UINT cacheSlot = BindingTlsDescriptorQueryCache::CacheSlotIndex(
		compute, rangeType);
	if (cacheSlot == UINT_MAX)
		return false;
	BindingTlsDescriptorQueryCache &queryCache = tlsDescriptorQueryCaches[cacheSlot];
	if (queryCache.commandList == commandList &&
		queryCache.compute == compute &&
		queryCache.rangeType == static_cast<UINT>(rangeType) &&
		queryCache.bindingQuerySerial == bindingQuerySerial &&
		queryCache.cbvSrvUavHeap == cbvSrvUavHeap &&
		queryCache.cbvSrvUavHeapVersion == cbvSrvUavHeapVersion) {
		*uavs = queryCache.bindings;
		const UINT64 heapVersionAfterHit = DX12GetDescriptorHeapVersion(
			snapshot.cbvSrvUavHeap);
		if (heapVersionAfterHit != cbvSrvUavHeapVersion) {
			uavs->clear();
		} else {
	#if defined(_DEBUG)
			DX12LogManager::DebugFields("DX12DescriptorQueryCache",
			"\"stage\":\"hit\",\"slot\":%u,\"compute\":%s,\"rangeType\":%u,\"serial\":%llu,\"heapVersion\":%llu,\"bindings\":%zu",
			cacheSlot, compute ? "true" : "false", static_cast<UINT>(rangeType),
			static_cast<unsigned long long>(bindingQuerySerial),
			static_cast<unsigned long long>(cbvSrvUavHeapVersion), uavs->size());
	#endif
			return !uavs->empty();
		}
	}

	ID3D12DescriptorHeap *observedSlotHeap = nullptr;
	std::vector<UINT> observedSlots;
	const bool foundAny = DX12BindingTrackerQueryCacheUtils::CollectDescriptorBindings(
		snapshot,
		compute,
		rangeType,
		descriptorKind,
		rootDescriptorType,
		uavs,
		&observedSlotHeap,
		&observedSlots);
	const UINT64 cbvSrvUavHeapVersionAfter = DX12GetDescriptorHeapVersion(
		snapshot.cbvSrvUavHeap);
	if (cbvSrvUavHeapVersionAfter == cbvSrvUavHeapVersion) {
		queryCache.commandList = commandList;
		queryCache.compute = compute;
		queryCache.rangeType = static_cast<UINT>(rangeType);
		queryCache.bindingQuerySerial = bindingQuerySerial;
		queryCache.cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
		queryCache.cbvSrvUavHeapVersion = cbvSrvUavHeapVersionAfter;
		queryCache.bindings = *uavs;
	} else {
		queryCache.commandList = nullptr;
	}
	if (preferRuntimeSnapshot &&
			runtimeState &&
			bindingQuerySerial != 0 &&
			cbvSrvUavHeapVersionAfter == cbvSrvUavHeapVersion) {
		std::vector<UINT64> observedSlotVersions(observedSlots.size());
		std::vector<UINT64> observedSlotValueIds(observedSlots.size());
		bool observedSlotsValid = true;
		if (!observedSlots.empty()) {
			observedSlotsValid =
				observedSlotHeap &&
				DX12GetDescriptorSlotIdentities(
					observedSlotHeap,
					observedSlots.data(),
					observedSlots.size(),
					observedSlotVersions.data(),
					observedSlotValueIds.data());
		}
		if (observedSlotsValid) {
			StoreRuntimeDescriptorBindings(
				runtimeState,
				compute,
				rangeType,
				bindingQuerySerial,
				foundAny,
				observedSlots,
				observedSlotVersions,
				observedSlotValueIds,
				*uavs);
		}
	}
	#if defined(_DEBUG)
	DX12LogManager::DebugFields("DX12DescriptorQueryCache",
		"\"stage\":\"miss\",\"slot\":%u,\"compute\":%s,\"rangeType\":%u,\"serial\":%llu,\"heapVersion\":%llu,\"bindings\":%zu",
		cacheSlot, compute ? "true" : "false", static_cast<UINT>(rangeType),
		static_cast<unsigned long long>(bindingQuerySerial),
		static_cast<unsigned long long>(cbvSrvUavHeapVersionAfter), uavs->size());
	#endif
	return foundAny;
}

bool DX12BindingTracker::GetCurrentComputeUavs(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *uavs)
{
	return DX12BindingTracker::GetCurrentDescriptors(
		commandList, true, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, "UAV",
		D3D12_ROOT_PARAMETER_TYPE_UAV, uavs);
}

bool DX12BindingTracker::GetCurrentComputeSrvs(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *srvs)
{
	return DX12BindingTracker::GetCurrentDescriptors(
		commandList, true, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, "SRV",
		D3D12_ROOT_PARAMETER_TYPE_SRV, srvs);
}

bool DX12BindingTracker::GetCurrentComputeCbvs(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *cbvs)
{
	return DX12BindingTracker::GetCurrentDescriptors(
		commandList, true, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, "CBV",
		D3D12_ROOT_PARAMETER_TYPE_CBV, cbvs);
}

bool DX12BindingTracker::GetCurrentComputeAll(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentComputeUavBinding> *uavs,
	std::vector<DX12CurrentComputeUavBinding> *srvs,
	std::vector<DX12CurrentComputeUavBinding> *cbvs,
	std::vector<DX12CurrentRootConstants> *constants)
{
	if (!commandList)
		return false;
	if (uavs) uavs->clear();
	if (srvs) srvs->clear();
	if (cbvs) cbvs->clear();
	if (constants) constants->clear();

	CommandListBindingState snapshot;
	UINT64 bindingQuerySerial = 0;
	DX12_HOTPATH_TIME_BEGIN(BindSnapshot);
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot, &bindingQuerySerial)) {
		DX12_HOTPATH_TIME_END(BindSnapshot);
		return false;
	}
	DX12_HOTPATH_TIME_END(BindSnapshot);
	const ID3D12DescriptorHeap *cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
	const UINT64 cbvSrvUavHeapVersion = DX12GetDescriptorHeapVersion(
		snapshot.cbvSrvUavHeap);
	// Use computeTables content hash as the cache key: unchanged bindings keep the hash stable across dispatches.
	// Heap pointers can change on SetDescriptorHeaps, so they are not suitable as the key.
	UINT64 tableHash = 0;
	for (UINT r = 0; r < MaxRootParameters; ++r) {
		const RootTableState &t = snapshot.computeTables[r];
		if (!t.valid) continue;
		tableHash ^= static_cast<UINT64>(r);
		tableHash ^= t.baseDescriptor.ptr;
		tableHash ^= tableHash >> 33;
		tableHash *= 0xff51afd7ed558ccdull;
	}
	if (tlsComputeAllQueryCache.commandList == commandList &&
		tlsComputeAllQueryCache.computeTableHash == tableHash &&
		tlsComputeAllQueryCache.bindingQuerySerial == bindingQuerySerial &&
		tlsComputeAllQueryCache.cbvSrvUavHeap == cbvSrvUavHeap &&
		tlsComputeAllQueryCache.cbvSrvUavHeapVersion == cbvSrvUavHeapVersion) {
		if (uavs) *uavs = tlsComputeAllQueryCache.uavs;
		if (srvs) *srvs = tlsComputeAllQueryCache.srvs;
		if (cbvs) *cbvs = tlsComputeAllQueryCache.cbvs;
		if (constants) *constants = tlsComputeAllQueryCache.constants;
		if (DX12GetDescriptorHeapVersion(snapshot.cbvSrvUavHeap) ==
				cbvSrvUavHeapVersion) {
			return (uavs && !uavs->empty()) || (srvs && !srvs->empty()) ||
				(cbvs && !cbvs->empty()) || (constants && !constants->empty());
		}
		if (uavs) uavs->clear();
		if (srvs) srvs->clear();
		if (cbvs) cbvs->clear();
		if (constants) constants->clear();
	}

	std::vector<DX12CurrentComputeUavBinding> cachedUavs;
	std::vector<DX12CurrentComputeUavBinding> cachedSrvs;
	std::vector<DX12CurrentComputeUavBinding> cachedCbvs;
	std::vector<DX12CurrentRootConstants> cachedConstants;
	DX12_HOTPATH_TIME_BEGIN(BindCollect);
	const bool foundAny = DX12BindingTrackerQueryCacheUtils::CollectComputeAll(
		snapshot, &cachedUavs, &cachedSrvs, &cachedCbvs, &cachedConstants);
	DX12_HOTPATH_TIME_END(BindCollect);
	const UINT64 cbvSrvUavHeapVersionAfter = DX12GetDescriptorHeapVersion(
		snapshot.cbvSrvUavHeap);
	if (cbvSrvUavHeapVersionAfter == cbvSrvUavHeapVersion) {
		tlsComputeAllQueryCache.commandList = commandList;
		tlsComputeAllQueryCache.computeTableHash = tableHash;
		tlsComputeAllQueryCache.bindingQuerySerial = bindingQuerySerial;
		tlsComputeAllQueryCache.cbvSrvUavHeap = snapshot.cbvSrvUavHeap;
		tlsComputeAllQueryCache.cbvSrvUavHeapVersion = cbvSrvUavHeapVersionAfter;
		tlsComputeAllQueryCache.uavs = cachedUavs;
		tlsComputeAllQueryCache.srvs = cachedSrvs;
		tlsComputeAllQueryCache.cbvs = cachedCbvs;
		tlsComputeAllQueryCache.constants = cachedConstants;
	} else {
		tlsComputeAllQueryCache.commandList = nullptr;
	}
	if (uavs) *uavs = cachedUavs;
	if (srvs) *srvs = cachedSrvs;
	if (cbvs) *cbvs = cachedCbvs;
	if (constants) *constants = cachedConstants;
	return foundAny;
}

bool DX12BindingTracker::GetCurrentShaderResourceBindings(
	ID3D12GraphicsCommandList *commandList,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	std::vector<DX12CurrentShaderResourceBinding> *bindings)
{
	const char *descriptorKind = nullptr;
	D3D12_ROOT_PARAMETER_TYPE rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	switch (rangeType) {
	case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
		descriptorKind = "CBV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		break;
	case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
		descriptorKind = "SRV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		break;
	case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
		descriptorKind = "UAV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		break;
	default:
		if (bindings)
			bindings->clear();
		return false;
	}

	return DX12BindingTracker::GetCurrentDescriptors(
		commandList, compute, rangeType, descriptorKind,
		rootDescriptorType, bindings);
}

bool DX12BindingTracker::GetCurrentShaderResourceBindingsByRegister(
	ID3D12GraphicsCommandList *commandList,
	bool compute,
	D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
	UINT shaderRegister,
	UINT registerSpace,
	std::vector<DX12CurrentShaderResourceBinding> *bindings)
{
	if (!bindings)
		return false;
	bindings->clear();

	const char *descriptorKind = nullptr;
	D3D12_ROOT_PARAMETER_TYPE rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	switch (rangeType) {
	case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
		descriptorKind = "CBV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		break;
	case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
		descriptorKind = "SRV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		break;
	case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
		descriptorKind = "UAV";
		rootDescriptorType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		break;
	default:
		return false;
	}

	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	const DX12CommandListTrackingState tracking =
		runtimeState ?
			DX12CommandListRuntime::GetTrackingState(runtimeState) :
			DX12CommandListTrackingState();
	if (runtimeState && !tracking.trackBindings) {
		CommandListBindingState snapshot;
		if (!BuildRuntimeBindingSnapshot(runtimeState, &snapshot))
			return false;
		return DX12BindingTrackerQueryCacheUtils::CollectDescriptorBindingsByRegister(
			snapshot,
			compute,
			rangeType,
			shaderRegister,
			registerSpace,
			descriptorKind,
			rootDescriptorType,
			bindings);
	}

	CommandListBindingState snapshot;
	if (!DX12BindingTracker::SnapshotBindingState(commandList, &snapshot, nullptr))
		return false;

	return DX12BindingTrackerQueryCacheUtils::CollectDescriptorBindingsByRegister(
		snapshot,
		compute,
		rangeType,
		shaderRegister,
		registerSpace,
		descriptorKind,
		rootDescriptorType,
		bindings);
}

bool DX12BindingTracker::GetCurrentDescriptorHeaps(
	ID3D12GraphicsCommandList *commandList,
	ID3D12DescriptorHeap **cbvSrvUavHeap,
	ID3D12DescriptorHeap **samplerHeap)
{
	return DX12CommandListRuntime::GetDescriptorHeaps(
		commandList, cbvSrvUavHeap, samplerHeap);
}

void DX12BindingTracker::RecordDrawInstanced(
	ID3D12GraphicsCommandList *commandList, UINT vertexCountPerInstance,
	UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	const UINT64 drawSerial = static_cast<UINT64>(InterlockedIncrement64(&DX12BindingTrackerState::GlobalDrawSerial));
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	state.drawSerial = drawSerial;
	// Resolve deferred VB/IB resources before snapshot (they were NOT resolved
	// eagerly in SetVertexBuffers/SetIndexBuffer; deferred to here for ~90%
	// per-call cost reduction).
	ResolveDeferredIaResources(&state);
	snapshot = state;
	ReleaseSRWLockExclusive(&shard.lock);
	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueDrawEvent(
			commandList,
			snapshot,
			"draw",
			descriptorEpoch,
			captureSessionId,
			vertexCountPerInstance,
			0,
			instanceCount,
			startVertexLocation,
			0,
			0,
			startInstanceLocation))
		return;

	BindingEvent event;
	if (!DX12BindingTracker::BuildEvent(
			&event,
			"draw",
			commandList,
			snapshot,
			descriptorEpoch,
			captureSessionId)) {
		return;
	}
	event.vertexCountPerInstance = vertexCountPerInstance;
	event.instanceCount = instanceCount;
	event.startVertexLocation = startVertexLocation;
	event.startInstanceLocation = startInstanceLocation;
	DX12BindingTracker::StoreEventLocked(event);
	DX12BindingTracker::StoreCurrentOutputTargetsLocked(commandList, snapshot, captureSessionId);
}

void DX12BindingTracker::RecordDrawIndexedInstanced(
	ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance,
	UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation,
	UINT startInstanceLocation)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	const UINT64 drawSerial = static_cast<UINT64>(InterlockedIncrement64(&DX12BindingTrackerState::GlobalDrawSerial));
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	state.drawSerial = drawSerial;
	// Resolve deferred VB/IB resources before snapshot; see RecordDrawInstanced.
	ResolveDeferredIaResources(&state);
	snapshot = state;
	ReleaseSRWLockExclusive(&shard.lock);
	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueDrawEvent(
			commandList,
			snapshot,
			"draw_indexed",
			descriptorEpoch,
			captureSessionId,
			0,
			indexCountPerInstance,
			instanceCount,
			0,
			startIndexLocation,
			baseVertexLocation,
			startInstanceLocation))
		return;

	BindingEvent event;
	if (!DX12BindingTracker::BuildEvent(
			&event,
			"draw_indexed",
			commandList,
			snapshot,
			descriptorEpoch,
			captureSessionId)) {
		return;
	}
	event.indexCountPerInstance = indexCountPerInstance;
	event.instanceCount = instanceCount;
	event.startIndexLocation = startIndexLocation;
	event.baseVertexLocation = baseVertexLocation;
	event.startInstanceLocation = startInstanceLocation;
	DX12BindingTracker::StoreEventLocked(event);
	DX12BindingTracker::StoreCurrentOutputTargetsLocked(commandList, snapshot, captureSessionId);
}

void DX12BindingTracker::RecordDispatch(
	ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX,
	UINT threadGroupCountY, UINT threadGroupCountZ)
{
	if (!commandList)
		return;

	CommandListBindingState snapshot;
	const UINT64 dispatchSerial =
		static_cast<UINT64>(InterlockedIncrement64(&DX12BindingTrackerState::GlobalDispatchSerial));
	CommandListBindingState *statePtr = GetOrResolveBindingState(commandList);
	BindingShard &shard = DX12BindingTrackerState::Shards[tlsStateCache.shardIndex];
	AcquireSRWLockExclusive(&shard.lock);
	if (tlsStateCache.shardGeneration != shard.generation) {
		statePtr = &shard.states[commandList];
		tlsStateCache.state = statePtr;
		tlsStateCache.shardGeneration = shard.generation;
	}
	CommandListBindingState &state = *statePtr;
	state.dispatchSerial = dispatchSerial;
	snapshot = state;
	ReleaseSRWLockExclusive(&shard.lock);
	const UINT64 descriptorEpoch = CurrentDescriptorEpoch();
	const UINT64 captureSessionId = DX12BindingTracker::CurrentCaptureSessionId();
	if (DX12BindingCollectQueue::EnqueueDispatchEvent(
			commandList,
			snapshot,
			descriptorEpoch,
			captureSessionId,
			threadGroupCountX,
			threadGroupCountY,
			threadGroupCountZ))
		return;

	BindingEvent event;
	if (!DX12BindingTracker::BuildEvent(
			&event,
			"dispatch",
			commandList,
			snapshot,
			descriptorEpoch,
			captureSessionId)) {
		return;
	}
	event.threadGroupCountX = threadGroupCountX;
	event.threadGroupCountY = threadGroupCountY;
	event.threadGroupCountZ = threadGroupCountZ;
	DX12BindingTracker::StoreEventLocked(event);
}

void DX12BindingTracker::BeginFrame()
{
	InterlockedExchange64(&DX12BindingTrackerState::GlobalDrawSerial, 0);
	InterlockedExchange64(&DX12BindingTrackerState::GlobalDispatchSerial, 0);
}

void DX12BindingTracker::SetGraphicsRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;
	DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootDescriptorCall();
	if (tlsGraphicsDescCache.cl == commandList &&
		tlsGraphicsDescCache.valid[rootParameterIndex] &&
		tlsGraphicsDescCache.address[rootParameterIndex] == address &&
		tlsGraphicsDescCache.type[rootParameterIndex] == type)
	{
		DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootDescriptorTlsHit();
		return;
	}

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootDescriptorState &descriptor = state.graphicsRootDescriptors[rootParameterIndex];
	const bool newValid = address != 0;
	if (descriptor.valid == newValid &&
		descriptor.rootParameterIndex == rootParameterIndex &&
		descriptor.type == type &&
		descriptor.address == address) {
		ReleaseSRWLockExclusive(&shard.lock);
		if (tlsGraphicsDescCache.cl != commandList) {
			memset(tlsGraphicsDescCache.valid, 0, sizeof(tlsGraphicsDescCache.valid));
			memset(tlsGraphicsDescCache.address, 0, sizeof(tlsGraphicsDescCache.address));
			memset(tlsGraphicsDescCache.type, 0, sizeof(tlsGraphicsDescCache.type));
			tlsGraphicsDescCache.cl = commandList;
		}
		tlsGraphicsDescCache.valid[rootParameterIndex] = true;
		tlsGraphicsDescCache.address[rootParameterIndex] = address;
		tlsGraphicsDescCache.type[rootParameterIndex] = type;
		DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootDescriptorNoChange();
		return;
	}
	descriptor.valid = newValid;
	descriptor.rootParameterIndex = rootParameterIndex;
	descriptor.type = type;
	descriptor.address = address;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	if (tlsGraphicsDescCache.cl != commandList) {
		memset(tlsGraphicsDescCache.valid, 0, sizeof(tlsGraphicsDescCache.valid));
		memset(tlsGraphicsDescCache.address, 0, sizeof(tlsGraphicsDescCache.address));
		memset(tlsGraphicsDescCache.type, 0, sizeof(tlsGraphicsDescCache.type));
		tlsGraphicsDescCache.cl = commandList;
	}
	tlsGraphicsDescCache.valid[rootParameterIndex] = true;
	tlsGraphicsDescCache.address[rootParameterIndex] = address;
	tlsGraphicsDescCache.type[rootParameterIndex] = type;
	DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootDescriptorChanged();
}

void DX12BindingTracker::SetComputeRootDescriptor(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;
	DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootDescriptorCall();
	if (tlsComputeDescCache.cl == commandList &&
		tlsComputeDescCache.valid[rootParameterIndex] &&
		tlsComputeDescCache.address[rootParameterIndex] == address &&
		tlsComputeDescCache.type[rootParameterIndex] == type)
	{
		DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootDescriptorTlsHit();
		return;
	}

	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootDescriptorState &descriptor = state.computeRootDescriptors[rootParameterIndex];
	const bool newValid = address != 0;
	if (descriptor.valid == newValid &&
		descriptor.rootParameterIndex == rootParameterIndex &&
		descriptor.type == type &&
		descriptor.address == address) {
		ReleaseSRWLockExclusive(&shard.lock);
		if (tlsComputeDescCache.cl != commandList) {
			memset(tlsComputeDescCache.valid, 0, sizeof(tlsComputeDescCache.valid));
			memset(tlsComputeDescCache.address, 0, sizeof(tlsComputeDescCache.address));
			memset(tlsComputeDescCache.type, 0, sizeof(tlsComputeDescCache.type));
			tlsComputeDescCache.cl = commandList;
		}
		tlsComputeDescCache.valid[rootParameterIndex] = true;
		tlsComputeDescCache.address[rootParameterIndex] = address;
		tlsComputeDescCache.type[rootParameterIndex] = type;
		DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootDescriptorNoChange();
		return;
	}
	descriptor.valid = newValid;
	descriptor.rootParameterIndex = rootParameterIndex;
	descriptor.type = type;
	descriptor.address = address;
	++state.computeBindingSerial;
	DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
	if (tlsComputeDescCache.cl != commandList) {
		memset(tlsComputeDescCache.valid, 0, sizeof(tlsComputeDescCache.valid));
		memset(tlsComputeDescCache.address, 0, sizeof(tlsComputeDescCache.address));
		memset(tlsComputeDescCache.type, 0, sizeof(tlsComputeDescCache.type));
		tlsComputeDescCache.cl = commandList;
	}
	tlsComputeDescCache.valid[rootParameterIndex] = true;
	tlsComputeDescCache.address[rootParameterIndex] = address;
	tlsComputeDescCache.type[rootParameterIndex] = type;
	DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootDescriptorChanged();
}

bool DX12BindingTracker::SetRootConstantsLocked(
	RootConstantsState *constants, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values)
{
	if (!constants || !values || destOffset >= 64 || count == 0)
		return false;
	const UINT *src = static_cast<const UINT*>(values);
	const UINT copyCount = min(count, 64 - destOffset);
	bool changed = !constants->valid ||
		constants->rootParameterIndex != rootParameterIndex;
	constants->valid = true;
	constants->rootParameterIndex = rootParameterIndex;
	for (UINT i = 0; i < copyCount; ++i) {
		const UINT slot = destOffset + i;
		changed = changed || !constants->valueValid[slot] ||
			constants->values[slot] != src[i];
		constants->values[slot] = src[i];
		constants->valueValid[slot] = true;
		if (slot + 1 > constants->maxSet)
			constants->maxSet = slot + 1;
	}
	return changed;
}

void DX12BindingTracker::SetGraphicsRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT value)
{
	DX12BindingTracker::SetGraphicsRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, 1, &value);
}

void DX12BindingTracker::SetComputeRoot32BitConstant(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT value)
{
	DX12BindingTracker::SetComputeRoot32BitConstants(
		commandList, rootParameterIndex, destOffset, 1, &value);
}

void DX12BindingTracker::SetGraphicsRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;

	DX12ResidentMaintenanceTelemetryUtils::RecordGraphicsRootConstants(count);
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootConstantsState &constants = state.graphicsRootConstants[rootParameterIndex];
	if (DX12BindingTracker::SetRootConstantsLocked(
			&constants, rootParameterIndex, destOffset, count, values))
		DX12BindingTracker::BumpBindingQuerySerial(&state);
	ReleaseSRWLockExclusive(&shard.lock);
}

void DX12BindingTracker::SetComputeRoot32BitConstants(
	ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex,
	UINT destOffset, UINT count, const void *values)
{
	if (!commandList || rootParameterIndex >= MaxRootParameters)
		return;

	DX12ResidentMaintenanceTelemetryUtils::RecordComputeRootConstants(count);
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockExclusive(&shard.lock);
	CommandListBindingState &state = shard.states[commandList];
	RootConstantsState &constants = state.computeRootConstants[rootParameterIndex];
	if (DX12BindingTracker::SetRootConstantsLocked(
			&constants, rootParameterIndex, destOffset, count, values)) {
		++state.computeBindingSerial;
		DX12BindingTracker::BumpBindingQuerySerial(&state);
	}
	ReleaseSRWLockExclusive(&shard.lock);
}

bool DX12BindingTracker::GetCurrentComputeRootConstants(
	ID3D12GraphicsCommandList *commandList,
	std::vector<DX12CurrentRootConstants> *constants)
{
	if (!commandList || !constants)
		return false;

	constants->clear();
	CommandListBindingState snapshot;
	bool found = false;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end()) {
		snapshot = it->second;
		found = true;
	}
	ReleaseSRWLockShared(&shard.lock);
	if (!found)
		return false;

	for (UINT root = 0; root < MaxRootParameters; ++root) {
		const RootConstantsState &source = snapshot.computeRootConstants[root];
		if (!source.valid)
			continue;
		DX12CurrentRootConstants current;
		current.rootParameterIndex = root;
		current.maxSet = source.maxSet;
		memcpy(current.values, source.values, sizeof(current.values));
		memcpy(current.valueValid, source.valueValid, sizeof(current.valueValid));
		constants->push_back(current);
	}
	return !constants->empty();
}

UINT64 DX12BindingTracker::GetComputeBindingSerial(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return 0;

	UINT64 serial = 0;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end())
		serial = it->second.computeBindingSerial;
	ReleaseSRWLockShared(&shard.lock);
	return serial;
}

UINT64 DX12BindingTracker::GetBindingQuerySerial(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return 0;

	UINT64 serial = 0;
	BindingShard &shard = DX12BindingTrackerState::ShardFor(commandList);
	AcquireSRWLockShared(&shard.lock);
	auto it = shard.states.find(commandList);
	if (it != shard.states.end())
		serial = it->second.bindingQuerySerial;
	ReleaseSRWLockShared(&shard.lock);
	return serial;
}
