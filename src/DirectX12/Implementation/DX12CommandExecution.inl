static std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
LoadCommandExecutionRuntimeSnapshot()
{
	return std::atomic_load_explicit(
		&gCommandExecutionRuntimeSnapshot,
		std::memory_order_acquire);
}

static bool CommandListRuntimeEffectLocked(
	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot,
	const std::wstring &name)
{
	if (!snapshot)
		return true;
	auto it = snapshot->commandListRuntimeEffect.find(name);
	return it == snapshot->commandListRuntimeEffect.end() || it->second;
}

static bool CommandListRuntimeEffectLocked(
	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot,
	const std::wstring &name,
	bool includePost)
{
	if (!snapshot)
		return true;
	const auto &effects = includePost ?
		snapshot->commandListPostRuntimeEffect :
		snapshot->commandListPreRuntimeEffect;
	if (!effects.empty()) {
		auto it = effects.find(name);
		return it == effects.end() || it->second;
	}
	return CommandListRuntimeEffectLocked(snapshot, name);
}

static bool CommandListRuntimeEffect(const std::wstring &name)
{
	return CommandListRuntimeEffectLocked(
		LoadCommandExecutionRuntimeSnapshot(), name);
}

static bool CommandListRuntimeEffect(
	const std::wstring &name, bool includePost)
{
	return CommandListRuntimeEffectLocked(
		LoadCommandExecutionRuntimeSnapshot(), name, includePost);
}

static bool AnyTextureOverrideRuntimeEffectForPhase(bool includePost)
{
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadCommandExecutionRuntimeSnapshot();
	if (!snapshot)
		return true;
	return includePost ?
		snapshot->anyTextureOverridePostRuntimeEffect :
		snapshot->anyTextureOverridePreRuntimeEffect;
}

static std::shared_ptr<const Bunny::CommandListConfig>
SnapshotCommandListConfig(const std::wstring &name)
{
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadCommandExecutionRuntimeSnapshot();
	if (!snapshot)
		return nullptr;
	auto it = snapshot->commandLists.find(name);
	return it == snapshot->commandLists.end() ? nullptr : it->second;
}

static std::shared_ptr<const DX12CompiledTextureOverridePlan>
SnapshotCompiledTextureOverridePlan(const Bunny::TextureOverrideConfig &config)
{
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadCommandExecutionRuntimeSnapshot();
	if (!snapshot)
		return nullptr;
	auto planIt = snapshot->compiledTextureOverridePlans.find(config.section);
	return planIt == snapshot->compiledTextureOverridePlans.end() ?
		nullptr : planIt->second;
}

static std::shared_ptr<const DX12CompiledCommandListPlan>
SnapshotCompiledCommandListPlan(const std::wstring &name)
{
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadCommandExecutionRuntimeSnapshot();
	if (!snapshot)
		return nullptr;
	auto planIt = snapshot->compiledCommandListPlans.find(name);
	return planIt == snapshot->compiledCommandListPlans.end() ?
		nullptr : planIt->second;
}

static std::shared_ptr<const DX12CompiledShaderOverridePlan>
SnapshotCompiledShaderOverridePlan(const std::wstring &section)
{
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadCommandExecutionRuntimeSnapshot();
	if (!snapshot)
		return nullptr;
	auto planIt = snapshot->compiledShaderOverridePlans.find(section);
	return planIt == snapshot->compiledShaderOverridePlans.end() ?
		nullptr : planIt->second;
}

static void AppendResolvedDescriptorPrefilterSectionIds(
	const std::vector<DX12TriggeredTextureOverride> &source,
	std::vector<uint32_t> *sectionIds)
{
	if (!sectionIds)
		return;
	for (const DX12TriggeredTextureOverride &triggered : source) {
		if (!triggered.config || !triggered.sectionId)
			continue;
		sectionIds->push_back(triggered.sectionId);
	}
}

static bool HasPotentialResolvedDescriptorPrefilterCandidates(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount)
{
	if (!snapshot.textureOverridesWithoutDrawContext.empty() ||
	    !snapshot.textureOverridesGenericDrawContext.empty())
		return true;
	if (indexCount) {
		auto indexed = snapshot.textureOverridesByIndexCount.find(indexCount);
		if (indexed != snapshot.textureOverridesByIndexCount.end() && !indexed->second.empty())
			return true;
	}
	if (vertexCount) {
		auto vertex = snapshot.textureOverridesByVertexCount.find(vertexCount);
		if (vertex != snapshot.textureOverridesByVertexCount.end() && !vertex->second.empty())
			return true;
	}
	return false;
}

static void CollectResolvedDescriptorPrefilterSectionIds(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount,
	std::vector<uint32_t> *sectionIds)
{
	if (!sectionIds)
		return;
	sectionIds->clear();
	AppendResolvedDescriptorPrefilterSectionIds(
		snapshot.textureOverridesWithoutDrawContext,
		sectionIds);
	AppendResolvedDescriptorPrefilterSectionIds(
		snapshot.textureOverridesGenericDrawContext,
		sectionIds);
	if (indexCount) {
		auto indexed = snapshot.textureOverridesByIndexCount.find(indexCount);
		if (indexed != snapshot.textureOverridesByIndexCount.end())
			AppendResolvedDescriptorPrefilterSectionIds(indexed->second, sectionIds);
	}
	if (vertexCount) {
		auto vertex = snapshot.textureOverridesByVertexCount.find(vertexCount);
		if (vertex != snapshot.textureOverridesByVertexCount.end())
			AppendResolvedDescriptorPrefilterSectionIds(vertex->second, sectionIds);
	}
	std::sort(sectionIds->begin(), sectionIds->end());
	sectionIds->erase(std::unique(sectionIds->begin(), sectionIds->end()), sectionIds->end());
}

static DX12IaHashState EmptyIaHashState()
{
	DX12IaHashState state = {};
	return state;
}

class DX12TextureOverrideTargetCacheKey
{
public:
	uint32_t kind = 0;
	uint32_t stage = 0;
	uint32_t slot = 0;

	bool operator==(const DX12TextureOverrideTargetCacheKey &other) const
	{
		return kind == other.kind &&
			stage == other.stage &&
			slot == other.slot;
	}
};

class DX12TextureOverrideTargetCacheKeyHash
{
public:
	size_t operator()(const DX12TextureOverrideTargetCacheKey &key) const
	{
		UINT64 hash = 0x4d8b2f6d9c3145a7ull;
		hash = HashCombine64(hash, key.kind);
		hash = HashCombine64(hash, key.stage);
		hash = HashCombine64(hash, key.slot);
		return static_cast<size_t>(hash ^ (hash >> 32));
	}
};

class DX12DirectDescriptorPrefilterCache
{
public:
	UINT64 reloadGeneration = 0;
	ID3D12GraphicsCommandList *commandList = nullptr;
	UINT64 descriptorQuerySerial = 0;
	bool usesGraphicsRootRevisions = false;
	std::vector<UINT> graphicsRootParameters;
	std::vector<UINT64> graphicsRootRevisions;
	ID3D12DescriptorHeap *descriptorHeap = nullptr;
	UINT64 descriptorHeapVersion = 0;
	UINT64 textureHashGeneration = 0;
	UINT64 targetPlanFingerprint = 0;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	uint32_t firstInstance = 0;
	bool hasCandidate = false;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> matchesByTarget;

	void Clear()
	{
		reloadGeneration = 0;
		commandList = nullptr;
		descriptorQuerySerial = 0;
		usesGraphicsRootRevisions = false;
		graphicsRootParameters.clear();
		graphicsRootRevisions.clear();
		descriptorHeap = nullptr;
		descriptorHeapVersion = 0;
		textureHashGeneration = 0;
		targetPlanFingerprint = 0;
		vertexCount = 0;
		indexCount = 0;
		instanceCount = 0;
		firstVertex = 0;
		firstIndex = 0;
		firstInstance = 0;
		hasCandidate = false;
		matchesByTarget.clear();
	}

	void SetInvocation(
		UINT64 generation,
		ID3D12GraphicsCommandList *list,
		const DX12SimulateContext *runtimeState,
		UINT64 currentDescriptorQuerySerial,
		ID3D12DescriptorHeap *currentDescriptorHeap,
		UINT64 currentDescriptorHeapVersion,
		UINT64 currentTextureHashGeneration,
		UINT64 currentTargetPlanFingerprint,
		uint32_t drawVertexCount,
		uint32_t drawIndexCount,
		uint32_t drawInstanceCount,
		uint32_t drawFirstVertex,
		uint32_t drawFirstIndex,
		uint32_t drawFirstInstance)
	{
		if (MatchesInvocation(
				generation,
				list,
				runtimeState,
				currentDescriptorQuerySerial,
				currentDescriptorHeap,
				currentDescriptorHeapVersion,
				currentTextureHashGeneration,
				currentTargetPlanFingerprint,
				drawVertexCount,
				drawIndexCount,
				drawInstanceCount,
				drawFirstVertex,
				drawFirstIndex,
				drawFirstInstance)) {
			return;
		}
		reloadGeneration = generation;
		commandList = list;
		descriptorQuerySerial = currentDescriptorQuerySerial;
		usesGraphicsRootRevisions = false;
		graphicsRootParameters.clear();
		graphicsRootRevisions.clear();
		descriptorHeap = currentDescriptorHeap;
		descriptorHeapVersion = currentDescriptorHeapVersion;
		textureHashGeneration = currentTextureHashGeneration;
		targetPlanFingerprint = currentTargetPlanFingerprint;
		vertexCount = drawVertexCount;
		indexCount = drawIndexCount;
		instanceCount = drawInstanceCount;
		firstVertex = drawFirstVertex;
		firstIndex = drawFirstIndex;
		firstInstance = drawFirstInstance;
		hasCandidate = false;
		matchesByTarget.clear();
	}

	bool MatchesInvocation(
		UINT64 generation,
		ID3D12GraphicsCommandList *list,
		const DX12SimulateContext *runtimeState,
		UINT64 currentDescriptorQuerySerial,
		ID3D12DescriptorHeap *currentDescriptorHeap,
		UINT64 currentDescriptorHeapVersion,
		UINT64 currentTextureHashGeneration,
		UINT64 currentTargetPlanFingerprint,
		uint32_t drawVertexCount,
		uint32_t drawIndexCount,
		uint32_t drawInstanceCount,
		uint32_t drawFirstVertex,
		uint32_t drawFirstIndex,
		uint32_t drawFirstInstance) const
	{
		if (reloadGeneration != generation ||
			commandList != list ||
			descriptorHeap != currentDescriptorHeap ||
			descriptorHeapVersion != currentDescriptorHeapVersion ||
			textureHashGeneration != currentTextureHashGeneration ||
			targetPlanFingerprint != currentTargetPlanFingerprint ||
			vertexCount != drawVertexCount ||
			indexCount != drawIndexCount ||
			instanceCount != drawInstanceCount ||
			firstVertex != drawFirstVertex ||
			firstIndex != drawFirstIndex ||
			firstInstance != drawFirstInstance) {
			return false;
		}
		if (!usesGraphicsRootRevisions)
			return descriptorQuerySerial == currentDescriptorQuerySerial;
		if (!runtimeState ||
			graphicsRootParameters.size() != graphicsRootRevisions.size())
			return false;
		for (size_t index = 0; index < graphicsRootParameters.size(); ++index) {
			const UINT root = graphicsRootParameters[index];
			if (root >= runtimeState->graphicsRootBindingRevisions.size() ||
				runtimeState->graphicsRootBindingRevisions[root] !=
					graphicsRootRevisions[index])
				return false;
		}
		return true;
	}

	void StoreGraphicsRootDependencies(
		const DX12SimulateContext *runtimeState,
		bool dependenciesKnown,
		const std::array<bool, MaxRootParameters> &rootSeen)
	{
		usesGraphicsRootRevisions = dependenciesKnown && runtimeState;
		graphicsRootParameters.clear();
		graphicsRootRevisions.clear();
		if (!usesGraphicsRootRevisions)
			return;
		for (UINT root = 0; root < rootSeen.size(); ++root) {
			if (!rootSeen[root])
				continue;
			graphicsRootParameters.push_back(root);
			graphicsRootRevisions.push_back(
				runtimeState->graphicsRootBindingRevisions[root]);
		}
		if (graphicsRootParameters.empty())
			usesGraphicsRootRevisions = false;
	}

	bool MatchesInvocation(
		UINT64 generation,
		ID3D12GraphicsCommandList *list,
		uint32_t drawVertexCount,
		uint32_t drawIndexCount,
		uint32_t drawInstanceCount,
		uint32_t drawFirstVertex,
		uint32_t drawFirstIndex,
		uint32_t drawFirstInstance) const
	{
		return reloadGeneration == generation &&
			commandList == list &&
			vertexCount == drawVertexCount &&
			indexCount == drawIndexCount &&
			instanceCount == drawInstanceCount &&
			firstVertex == drawFirstVertex &&
			firstIndex == drawFirstIndex &&
			firstInstance == drawFirstInstance;
	}
};

static thread_local DX12DirectDescriptorPrefilterCache tlsDirectDescriptorPrefilterCache;

static void ResolveGraphicsSrvBindings(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Device *cachedDevice,
	const std::vector<DX12CurrentShaderResourceBinding> &bindings,
	std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings)
{
	if (!resolvedBindings)
		return;
	resolvedBindings->clear();
	if (bindings.empty())
		return;

	resolvedBindings->reserve(bindings.size());
	for (const DX12CurrentShaderResourceBinding &binding : bindings) {
		DX12ResolvedGraphicsSrvBinding resolved;
		resolved.binding = binding;
		resolved.resourceHash = HashDescriptorBindingResource(binding);
		resolved.descriptorHash =
			HashDescriptorBinding(commandList, cachedDevice, binding);
		resolvedBindings->push_back(std::move(resolved));
	}
}

static bool TryLoadResolvedGraphicsSrvBindings(
	DX12SimulateContext *runtimeState,
	UINT64 descriptorQuerySerial,
	std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings)
{
	if (!runtimeState || !resolvedBindings)
		return false;
	if (!runtimeState->resolvedGraphicsSrvs.valid ||
	    (!runtimeState->resolvedGraphicsSrvs.usesRootRevisions &&
	     runtimeState->resolvedGraphicsSrvs.descriptorQuerySerial != descriptorQuerySerial)) {
		return false;
	}
	if (runtimeState->resolvedGraphicsSrvs.usesRootRevisions) {
		if (runtimeState->resolvedGraphicsSrvs.rootParameters.size() !=
			runtimeState->resolvedGraphicsSrvs.rootRevisions.size())
			return false;
		for (size_t index = 0;
			 index < runtimeState->resolvedGraphicsSrvs.rootParameters.size();
			 ++index) {
			const UINT root = runtimeState->resolvedGraphicsSrvs.rootParameters[index];
			if (root >= runtimeState->graphicsRootBindingRevisions.size() ||
				runtimeState->graphicsRootBindingRevisions[root] !=
					runtimeState->resolvedGraphicsSrvs.rootRevisions[index])
				return false;
		}
	}
	*resolvedBindings = runtimeState->resolvedGraphicsSrvs.bindings;
	return true;
}

static void StoreResolvedGraphicsSrvBindings(
	DX12SimulateContext *runtimeState,
	UINT64 descriptorQuerySerial,
	const std::vector<DX12ResolvedGraphicsSrvBinding> &resolvedBindings)
{
	if (!runtimeState)
		return;
	runtimeState->resolvedGraphicsSrvs.valid = true;
	runtimeState->resolvedGraphicsSrvs.descriptorQuerySerial = descriptorQuerySerial;
	runtimeState->resolvedGraphicsSrvs.rootParameters.clear();
	runtimeState->resolvedGraphicsSrvs.rootRevisions.clear();
	runtimeState->resolvedGraphicsSrvs.usesRootRevisions = !resolvedBindings.empty();
	if (runtimeState->resolvedGraphicsSrvs.usesRootRevisions) {
		std::array<bool, MaxRootParameters> rootSeen = {};
		for (const DX12ResolvedGraphicsSrvBinding &binding : resolvedBindings) {
			const UINT root = binding.binding.rootParameterIndex;
			if (root >= runtimeState->graphicsRootBindingRevisions.size()) {
				runtimeState->resolvedGraphicsSrvs.usesRootRevisions = false;
				runtimeState->resolvedGraphicsSrvs.rootParameters.clear();
				runtimeState->resolvedGraphicsSrvs.rootRevisions.clear();
				break;
			}
			if (rootSeen[root])
				continue;
			rootSeen[root] = true;
			runtimeState->resolvedGraphicsSrvs.rootParameters.push_back(root);
			runtimeState->resolvedGraphicsSrvs.rootRevisions.push_back(
				runtimeState->graphicsRootBindingRevisions[root]);
		}
	}
	runtimeState->resolvedGraphicsSrvs.bindings = resolvedBindings;
}

static void FillSyntheticTextureBinding(
	const Bunny::CommandListTarget &target,
	ID3D12Resource *resource,
	DXGI_FORMAT format,
	UINT width,
	UINT height,
	DX12CurrentShaderResourceBinding *binding)
{
	if (!binding)
		return;
	*binding = DX12CurrentShaderResourceBinding();
	binding->shaderRegister = target.slot;
	binding->registerSpace = 0;
	binding->shaderVisibility =
		target.stage == Bunny::CommandListShaderStage::Vertex ?
			D3D12_SHADER_VISIBILITY_VERTEX :
		target.stage == Bunny::CommandListShaderStage::Pixel ?
			D3D12_SHADER_VISIBILITY_PIXEL :
			D3D12_SHADER_VISIBILITY_ALL;
	if (!resource)
		return;
	binding->hasDescriptor = true;
	binding->rootDescriptor = false;
	binding->descriptor.kind = "SRV";
	binding->descriptor.resource = resource;
	binding->descriptor.hasResourceDesc = true;
	binding->descriptor.resourceDesc.Dimension =
		D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	binding->descriptor.resourceDesc.Width = width;
	binding->descriptor.resourceDesc.Height = height;
	binding->descriptor.resourceDesc.DepthOrArraySize = 1;
	binding->descriptor.resourceDesc.MipLevels = 1;
	binding->descriptor.resourceDesc.Format = format;
	binding->descriptor.viewFormat = static_cast<UINT>(format);
}

static bool BuildResolvedSrvBindingsForResourceOverride(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	const Bunny::CommandListTarget &target,
	const std::wstring &resourceName,
	const std::vector<DX12ResolvedGraphicsSrvBinding> *baseBindings,
	std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings)
{
	if (!resolvedBindings)
		return false;
	resolvedBindings->clear();
	if (!device || !commandList || resourceName.empty())
		return false;

	DX12LoadedResource *resource =
		DX12DdsTextureLoadUtils::EnsureLoadedDrawTextureSrv(
			device, commandList, resourceName);
	if (!resource)
		return false;
	ID3D12Resource *texture = resource->drawSrvTextureResource ?
		resource->drawSrvTextureResource : resource->resource;
	uint32_t textureHash = 0;
	if (!texture || !DX12TryGetTextureHash(texture, &textureHash) || !textureHash)
		return false;

	if (baseBindings && !baseBindings->empty()) {
		for (const DX12ResolvedGraphicsSrvBinding &base : *baseBindings) {
			DX12ResolvedGraphicsSrvBinding resolved = base;
			resolved.resourceHash = textureHash;
			resolved.descriptorHash = textureHash;
			FillSyntheticTextureBinding(
				target,
				texture,
				resource->drawSrvFormat,
				resource->drawSrvWidth,
				resource->drawSrvHeight,
				&resolved.binding);
			resolvedBindings->push_back(std::move(resolved));
		}
		return !resolvedBindings->empty();
	}

	DX12ResolvedGraphicsSrvBinding synthetic;
	FillSyntheticTextureBinding(
		target,
		texture,
		resource->drawSrvFormat,
		resource->drawSrvWidth,
		resource->drawSrvHeight,
		&synthetic.binding);
	synthetic.resourceHash = textureHash;
	synthetic.descriptorHash = textureHash;
	resolvedBindings->push_back(std::move(synthetic));
	return true;
}

static bool FindDescriptorTextureOverridesFromResolvedBindings(
	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot,
	const Bunny::CommandListTarget &target,
	const std::vector<DX12ResolvedGraphicsSrvBinding> &resolvedBindings,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches)
{
	if (matches)
		matches->clear();
	if (!snapshot || resolvedBindings.empty())
		return false;

	const bool compute = target.stage == Bunny::CommandListShaderStage::Compute;
	if (!HasPotentialResolvedDescriptorPrefilterCandidates(
			*snapshot,
			vertexCount,
			indexCount))
		return false;

	static thread_local std::vector<uint32_t> tlsAllowedSectionIds;
	CollectResolvedDescriptorPrefilterSectionIds(
		*snapshot,
		vertexCount,
		indexCount,
		&tlsAllowedSectionIds);
	if (tlsAllowedSectionIds.empty())
		return false;

	std::vector<DX12TextureOverrideMatch> candidates;
	for (const DX12ResolvedGraphicsSrvBinding &resolved : resolvedBindings) {
		if (!DescriptorBindingMatchesTarget(resolved.binding, target))
			continue;
		if (resolved.resourceHash) {
			AppendTextureOverrideCandidateMatchesByHash(
				*snapshot,
				resolved.resourceHash,
				resolved.resourceHash,
				resolved.descriptorHash,
				&resolved.binding,
				compute,
				&tlsAllowedSectionIds,
				&candidates);
		}
		if (resolved.descriptorHash &&
		    resolved.descriptorHash != resolved.resourceHash) {
			AppendTextureOverrideCandidateMatchesByHash(
				*snapshot,
				resolved.descriptorHash,
				resolved.resourceHash,
				resolved.descriptorHash,
				&resolved.binding,
				compute,
				&tlsAllowedSectionIds,
				&candidates);
		}
	}

	return FilterDescriptorTextureOverrideCandidates(
		candidates,
		vertexCount,
		indexCount,
		instanceCount,
		firstVertex,
		firstIndex,
		firstInstance,
		matches);
}

static void AppendGraphicsSrvTextureOverrideCandidatesForTarget(
	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot,
	const DX12ResolvedGraphicsSrvBinding &resolved,
	const DX12TextureOverrideTargetCacheKey &cacheKey,
	const std::vector<uint32_t> &allowedSectionIds,
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> *matchesByTarget)
{
	if (!snapshot || !matchesByTarget)
		return;
	std::vector<DX12TextureOverrideMatch> &candidates =
		(*matchesByTarget)[cacheKey];
	if (resolved.resourceHash) {
		AppendTextureOverrideCandidateMatchesByHash(
			*snapshot,
			resolved.resourceHash,
			resolved.resourceHash,
			resolved.descriptorHash,
			&resolved.binding,
			false,
			&allowedSectionIds,
			&candidates);
	}
	if (resolved.descriptorHash &&
		resolved.descriptorHash != resolved.resourceHash) {
		AppendTextureOverrideCandidateMatchesByHash(
			*snapshot,
			resolved.descriptorHash,
			resolved.resourceHash,
			resolved.descriptorHash,
			&resolved.binding,
			false,
			&allowedSectionIds,
			&candidates);
	}
}

static void AppendResolvedBindingsToDescriptorTextureOverrideCache(
	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot,
	const std::vector<DX12ResolvedGraphicsSrvBinding> &resolvedBindings,
	Bunny::CommandListTargetKind kind,
	bool compute,
	const std::vector<uint32_t> &allowedSectionIds,
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> *matchesByTarget)
{
	if (!snapshot || !matchesByTarget)
		return;
	for (const DX12ResolvedGraphicsSrvBinding &resolved : resolvedBindings) {
		if (resolved.binding.registerSpace != 0)
			continue;
		if (compute) {
			AppendGraphicsSrvTextureOverrideCandidatesForTarget(
				snapshot,
				resolved,
				DX12TextureOverrideTargetCacheKey{
					static_cast<uint32_t>(kind),
					static_cast<uint32_t>(Bunny::CommandListShaderStage::Compute),
					resolved.binding.shaderRegister
				},
				allowedSectionIds,
				matchesByTarget);
			continue;
		}
		if (resolved.binding.shaderVisibility == D3D12_SHADER_VISIBILITY_VERTEX ||
			resolved.binding.shaderVisibility == D3D12_SHADER_VISIBILITY_ALL) {
			AppendGraphicsSrvTextureOverrideCandidatesForTarget(
				snapshot,
				resolved,
				DX12TextureOverrideTargetCacheKey{
					static_cast<uint32_t>(kind),
					static_cast<uint32_t>(Bunny::CommandListShaderStage::Vertex),
					resolved.binding.shaderRegister
				},
				allowedSectionIds,
				matchesByTarget);
		}
		if (resolved.binding.shaderVisibility == D3D12_SHADER_VISIBILITY_PIXEL ||
			resolved.binding.shaderVisibility == D3D12_SHADER_VISIBILITY_ALL) {
			AppendGraphicsSrvTextureOverrideCandidatesForTarget(
				snapshot,
				resolved,
				DX12TextureOverrideTargetCacheKey{
					static_cast<uint32_t>(kind),
					static_cast<uint32_t>(Bunny::CommandListShaderStage::Pixel),
					resolved.binding.shaderRegister
				},
				allowedSectionIds,
				matchesByTarget);
		}
	}
}

static void BuildGraphicsSrvTextureOverrideCacheFromResolvedBindings(
	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot,
	const std::vector<DX12ResolvedGraphicsSrvBinding> &resolvedBindings,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance,
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> *matchesByTarget)
{
	if (!matchesByTarget)
		return;
	matchesByTarget->clear();
	if (!snapshot || resolvedBindings.empty())
		return;
	if (!HasPotentialResolvedDescriptorPrefilterCandidates(
			*snapshot,
			vertexCount,
			indexCount))
		return;

	static thread_local std::vector<uint32_t> tlsAllowedSectionIds;
	CollectResolvedDescriptorPrefilterSectionIds(
		*snapshot,
		vertexCount,
		indexCount,
		&tlsAllowedSectionIds);
	if (tlsAllowedSectionIds.empty())
		return;

	for (const DX12ResolvedGraphicsSrvBinding &resolved : resolvedBindings) {
		if (resolved.binding.registerSpace != 0)
			continue;
		if (resolved.binding.shaderVisibility == D3D12_SHADER_VISIBILITY_VERTEX ||
			resolved.binding.shaderVisibility == D3D12_SHADER_VISIBILITY_ALL) {
			AppendGraphicsSrvTextureOverrideCandidatesForTarget(
				snapshot,
				resolved,
				DX12TextureOverrideTargetCacheKey{
					static_cast<uint32_t>(Bunny::CommandListTargetKind::ShaderResource),
					static_cast<uint32_t>(Bunny::CommandListShaderStage::Vertex),
					resolved.binding.shaderRegister
				},
				tlsAllowedSectionIds,
				matchesByTarget);
		}
		if (resolved.binding.shaderVisibility == D3D12_SHADER_VISIBILITY_PIXEL ||
			resolved.binding.shaderVisibility == D3D12_SHADER_VISIBILITY_ALL) {
			AppendGraphicsSrvTextureOverrideCandidatesForTarget(
				snapshot,
				resolved,
				DX12TextureOverrideTargetCacheKey{
					static_cast<uint32_t>(Bunny::CommandListTargetKind::ShaderResource),
					static_cast<uint32_t>(Bunny::CommandListShaderStage::Pixel),
					resolved.binding.shaderRegister
				},
				tlsAllowedSectionIds,
				matchesByTarget);
		}
	}

	for (auto it = matchesByTarget->begin(); it != matchesByTarget->end();) {
		std::vector<DX12TextureOverrideMatch> filtered;
		FilterDescriptorTextureOverrideCandidates(
			it->second,
			vertexCount,
			indexCount,
			instanceCount,
			firstVertex,
			firstIndex,
			firstInstance,
			&filtered);
		if (filtered.empty()) {
			it = matchesByTarget->erase(it);
			continue;
		}
		it->second = std::move(filtered);
		++it;
	}
}

static void FillSyntheticBufferBinding(
	const Bunny::CommandListTarget &target,
	ID3D12Resource *resource,
	const std::string &kind,
	UINT64 viewBytes,
	UINT stride,
	DX12CurrentShaderResourceBinding *binding)
{
	if (!binding)
		return;
	*binding = DX12CurrentShaderResourceBinding();
	binding->shaderRegister = target.slot;
	binding->registerSpace = 0;
	binding->shaderVisibility =
		target.stage == Bunny::CommandListShaderStage::Vertex ?
			D3D12_SHADER_VISIBILITY_VERTEX :
		target.stage == Bunny::CommandListShaderStage::Pixel ?
			D3D12_SHADER_VISIBILITY_PIXEL :
			D3D12_SHADER_VISIBILITY_ALL;
	if (!resource)
		return;
	binding->hasDescriptor = true;
	binding->rootDescriptor = false;
	binding->descriptor.kind = kind;
	binding->descriptor.resource = resource;
	binding->descriptor.hasResourceDesc = true;
	binding->descriptor.resourceDesc.Dimension =
		D3D12_RESOURCE_DIMENSION_BUFFER;
	binding->descriptor.resourceDesc.Width = viewBytes;
	binding->descriptor.resourceDesc.Height = 1;
	binding->descriptor.resourceDesc.DepthOrArraySize = 1;
	binding->descriptor.resourceDesc.MipLevels = 1;
	binding->descriptor.resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	binding->descriptor.resourceDesc.SampleDesc.Count = 1;
	binding->descriptor.resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	binding->descriptor.gpuVirtualAddress = resource->GetGPUVirtualAddress();
	binding->descriptor.viewSize = viewBytes;
	binding->descriptor.bufferViewOffset = 0;
	binding->descriptor.bufferViewBytes = viewBytes;
	binding->descriptor.structureByteStride = stride;
	if (target.kind == Bunny::CommandListTargetKind::ConstantBuffer) {
		binding->descriptor.viewDimension = 0;
		binding->descriptor.cbvSize = static_cast<UINT>(
			(std::min)(viewBytes, static_cast<UINT64>(UINT_MAX)));
	} else if (target.kind == Bunny::CommandListTargetKind::ShaderResource) {
		binding->descriptor.viewDimension = D3D12_SRV_DIMENSION_BUFFER;
		binding->descriptor.firstElement = 0;
		binding->descriptor.numElements = stride ?
			static_cast<UINT>((std::min)(
				viewBytes / stride,
				static_cast<UINT64>(UINT_MAX))) : 0;
	} else if (target.kind == Bunny::CommandListTargetKind::UnorderedAccessView) {
		binding->descriptor.viewDimension = D3D12_UAV_DIMENSION_BUFFER;
		binding->descriptor.firstElement = 0;
		binding->descriptor.numElements = stride ?
			static_cast<UINT>((std::min)(
				viewBytes / stride,
				static_cast<UINT64>(UINT_MAX))) : 0;
	}
}

static bool BuildResolvedDescriptorBindingsForResourceOverride(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	const Bunny::CommandListTarget &target,
	const std::wstring &resourceName,
	const std::vector<DX12ResolvedGraphicsSrvBinding> *baseBindings,
	std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings)
{
	if (!resolvedBindings)
		return false;
	resolvedBindings->clear();
	if (!device || !commandList || resourceName.empty())
		return false;

	if (target.kind == Bunny::CommandListTargetKind::ShaderResource) {
		DX12LoadedResource *textureResource =
			DX12DdsTextureLoadUtils::EnsureLoadedDrawTextureSrv(
				device, commandList, resourceName);
		if (textureResource) {
			ID3D12Resource *texture = textureResource->drawSrvTextureResource ?
				textureResource->drawSrvTextureResource :
				textureResource->resource;
			uint32_t textureHash = 0;
			if (texture &&
			    DX12TryGetTextureHash(texture, &textureHash) &&
			    textureHash) {
				if (baseBindings && !baseBindings->empty()) {
					for (const DX12ResolvedGraphicsSrvBinding &base : *baseBindings) {
						DX12ResolvedGraphicsSrvBinding resolved = base;
						resolved.resourceHash = textureHash;
						resolved.descriptorHash = textureHash;
						FillSyntheticTextureBinding(
							target,
							texture,
							textureResource->drawSrvFormat,
							textureResource->drawSrvWidth,
							textureResource->drawSrvHeight,
							&resolved.binding);
						resolvedBindings->push_back(std::move(resolved));
					}
					return !resolvedBindings->empty();
				}

				DX12ResolvedGraphicsSrvBinding synthetic;
				FillSyntheticTextureBinding(
					target,
					texture,
					textureResource->drawSrvFormat,
					textureResource->drawSrvWidth,
					textureResource->drawSrvHeight,
					&synthetic.binding);
				synthetic.resourceHash = textureHash;
				synthetic.descriptorHash = textureHash;
				resolvedBindings->push_back(std::move(synthetic));
				return true;
			}
		}
	}

	DX12LoadedResource *resource = EnsureLoadedResourceForPreSkin(device, resourceName);
	if (!resource)
		return false;
	const UINT stride = resource->stride ? resource->stride : 4;
	ID3D12Resource *bindingResource = resource->resource;
	UINT64 viewBytes = resource->byteWidth;
	std::string kind(DescriptorKind::SRV);

	if (target.kind == Bunny::CommandListTargetKind::ShaderResource) {
		if (!EnsureResourceSrvLocked(device, resource, stride))
			return false;
		bindingResource = resource->srvResource ? resource->srvResource : resource->resource;
		viewBytes = resource->srvByteWidth ? resource->srvByteWidth : resource->byteWidth;
		kind = std::string(DescriptorKind::SRV);
	} else if (target.kind == Bunny::CommandListTargetKind::ConstantBuffer) {
		kind = std::string(DescriptorKind::CBV);
	} else if (target.kind == Bunny::CommandListTargetKind::UnorderedAccessView) {
		if (!EnsureResourceUavLocked(device, commandList, resource, stride, 0, true, nullptr))
			return false;
		bindingResource = resource->uavResource ? resource->uavResource : resource->resource;
		viewBytes = resource->uavByteWidth ? resource->uavByteWidth : resource->byteWidth;
		kind = std::string(DescriptorKind::UAV);
	} else {
		return false;
	}

	if (!bindingResource || !viewBytes)
		return false;

	DX12ResolvedGraphicsSrvBinding synthetic;
	FillSyntheticBufferBinding(
		target,
		bindingResource,
		kind,
		viewBytes,
		stride,
		&synthetic.binding);
	synthetic.resourceHash = HashDescriptorBindingResource(synthetic.binding);
	synthetic.descriptorHash =
		HashDescriptorBinding(commandList, device, synthetic.binding);
	resolvedBindings->push_back(std::move(synthetic));
	return true;
}

class DX12CommandListExecutorScratch
{
public:
	std::vector<std::wstring> commandListStack;
	std::unordered_set<std::wstring> commandListStackSet;
	std::vector<Bunny::CommandListTarget> descriptorTargetScratch;
	std::vector<DX12TextureOverrideMatch> targetMatchScratch;
	std::vector<DX12CurrentShaderResourceBinding> graphicsSrvBindings;
	std::vector<DX12ResolvedGraphicsSrvBinding> resolvedGraphicsSrvBindings;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12ResolvedGraphicsSrvBinding>,
		DX12TextureOverrideTargetCacheKeyHash> graphicsSrvBindingsByTarget;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> graphicsSrvTextureOverrideCache;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12ResolvedGraphicsSrvBinding>,
		DX12TextureOverrideTargetCacheKeyHash> shaderResourceOverrideBindingsByTarget;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> targetTextureOverrideCache;

	void Reset()
	{
		commandListStack.clear();
		commandListStackSet.clear();
		descriptorTargetScratch.clear();
		targetMatchScratch.clear();
		graphicsSrvBindings.clear();
		resolvedGraphicsSrvBindings.clear();
		graphicsSrvBindingsByTarget.clear();
		graphicsSrvTextureOverrideCache.clear();
		shaderResourceOverrideBindingsByTarget.clear();
		targetTextureOverrideCache.clear();
	}
};

class DX12CommandListExecutor
{
public:
	inline static thread_local DX12CommandListExecutorScratch ScratchPool[18] = {};
	inline static thread_local UINT ScratchDepth = 0;

	static DX12CommandListExecutorScratch *AcquireScratch(
		bool *pooled,
		DX12CommandListExecutorScratch *fallback)
	{
		if (ScratchDepth < ARRAYSIZE(ScratchPool)) {
			DX12CommandListExecutorScratch *scratch = &ScratchPool[ScratchDepth++];
			scratch->Reset();
			*pooled = true;
			return scratch;
		}
		fallback->Reset();
		*pooled = false;
		return fallback;
	}

	class CommandVmRunScope
	{
	public:
		MigotoCommandVmUtils::State *state = nullptr;
		size_t branchDepth = 0;
		bool flushPost = false;

		CommandVmRunScope(MigotoCommandVmUtils::State *vmState, bool flushPostAssignments)
			: state(vmState),
			  flushPost(flushPostAssignments)
		{
			if (!state)
				return;
			branchDepth = state->branches.size();
			state->PushLocalScope();
		}

		~CommandVmRunScope()
		{
			if (!state)
				return;
			if (state->branches.size() > branchDepth)
				state->branches.resize(branchDepth);
			if (flushPost)
				state->FlushPostAssignments();
			state->PopLocalScope();
		}
	};

	class BoolScope
	{
	public:
		BoolScope(bool *flag, bool value)
			: target(flag),
			  saved(flag ? *flag : false)
		{
			if (target)
				*target = value;
		}

		~BoolScope()
		{
			if (target)
				*target = saved;
		}

	private:
		bool *target = nullptr;
		bool saved = false;
	};

	DX12CommandListExecutor(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const DX12IaHashState &iaState,
		uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
		uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
		DX12ModIaReplacement *replacement,
		std::vector<DX12PendingResourceViewRequest> *pendingResourceViews = nullptr)
		: mDevice(device),
		  mCommandList(commandList),
		  mIaState(iaState),
		  mVertexCount(vertexCount),
		  mIndexCount(indexCount),
		  mInstanceCount(instanceCount),
		  mFirstVertex(firstVertex),
		  mFirstIndex(firstIndex),
		  mFirstInstance(firstInstance),
		  mReplacement(replacement),
		  mPendingResourceViews(pendingResourceViews),
		  mScratch(AcquireScratch(&mScratchPooled, &mFallbackScratch)),
		  mCommandListStack(mScratch->commandListStack),
		  mCommandListStackSet(mScratch->commandListStackSet),
		  mDescriptorTargetScratch(mScratch->descriptorTargetScratch),
		  mTargetMatchScratch(mScratch->targetMatchScratch),
		  mGraphicsSrvBindings(mScratch->graphicsSrvBindings),
		  mResolvedGraphicsSrvBindings(mScratch->resolvedGraphicsSrvBindings),
		  mGraphicsSrvBindingsByTarget(mScratch->graphicsSrvBindingsByTarget),
		  mGraphicsSrvTextureOverrideCache(mScratch->graphicsSrvTextureOverrideCache),
		  mShaderResourceOverrideBindingsByTarget(mScratch->shaderResourceOverrideBindingsByTarget),
		  mTargetTextureOverrideCache(mScratch->targetTextureOverrideCache)
	{
		mCurrentIaState = iaState;
		mCommandListStack.reserve(8);
		mCommandListStackSet.reserve(8);
		mDescriptorTargetScratch.reserve(8);
		mTargetMatchScratch.reserve(8);
		mGraphicsSrvBindings.reserve(32);
		mResolvedGraphicsSrvBindings.reserve(32);
		mGraphicsSrvBindingsByTarget.reserve(16);
		mGraphicsSrvTextureOverrideCache.reserve(16);
		mShaderResourceOverrideBindingsByTarget.reserve(16);
		mTargetTextureOverrideCache.reserve(16);
	}

	~DX12CommandListExecutor()
	{
		mScratch->Reset();
		if (mScratchPooled)
			--ScratchDepth;
	}

	void SetExecutionMode(bool executeCommands, bool executeDrawActions)
	{
		mExecuteCommands = executeCommands;
		mExecuteDrawActions = executeDrawActions;
	}

	void SetSkipDirectDescriptorTargets(bool skip)
	{
		mSkipDirectDescriptorTargets = skip;
	}

	void AdoptDirectDescriptorPrefilterCache()
	{
		const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
		if (!tlsDirectDescriptorPrefilterCache.MatchesInvocation(
				reloadGeneration,
				mCommandList,
				mVertexCount,
				mIndexCount,
				mInstanceCount,
				mFirstVertex,
				mFirstIndex,
				mFirstInstance)) {
			return;
		}
		for (const auto &entry : tlsDirectDescriptorPrefilterCache.matchesByTarget)
			mGraphicsSrvTextureOverrideCache[entry.first] = entry.second;
	}

	bool observedDescriptorTarget = false;
	bool observedDynamicCommandState = false;

	bool Changed() const
	{
		return mChanged;
	}

	void ClearChanged()
	{
		mChanged = false;
	}

	void MarkExternalChange()
	{
		MarkChanged();
	}

	void RunTextureOverride(
		const Bunny::TextureOverrideConfig &config, bool includePost)
	{
		DX12TextureOverrideMatch match;
		match.config = &config;
		RunTextureOverride(match, 0, includePost);
	}

	void RunCommandListLinks(
		const Bunny::CommandListLinks &links, bool includePost)
	{
		RunCommandListLinks(links, 0, includePost, nullptr);
	}

	void RunShaderOverride(const Bunny::ShaderOverrideConfig &config)
	{
		if (!mReplacement)
			return;
		BoolScope directDescriptorSkipScope(
			&mSkipDirectDescriptorTargetsActive,
			mSkipDirectDescriptorTargets);
		for (const std::wstring &list : config.commandLists.post)
			mReplacement->AddPostCommandList(list);
		const std::shared_ptr<const DX12CompiledShaderOverridePlan> compiledPlan =
			GetCompiledShaderOverridePlan(config.section);
		if (compiledPlan && !compiledPlan->unsupported) {
			RunCompiledShaderOverridePlan(*compiledPlan);
			return;
		}
		if (config.handlingSkip)
			SetSkip();
		RunCommandListLinks(config.commandLists, 0, false, nullptr);
		RunActions(config.actions, 0, false, nullptr);
	}

	void QueuePostTextureOverride(
		const DX12TextureOverrideMatch &match)
	{
		if (!mReplacement || !match.config || !match.postRuntimeEffect)
			return;
		DX12ModIaReplacement::QueuedTextureOverridePost queued;
		queued.config = match.config;
		queued.hasThisSrvTarget = match.hasThisSrvTarget;
		queued.compute = match.compute;
		queued.thisSrvBinding = match.thisSrvBinding;
		queued.matchedHash = match.matchedHash;
		queued.resourceHash = match.resourceHash;
		queued.descriptorHash = match.descriptorHash;
		mReplacement->AddQueuedPostTextureOverride(queued);
	}

	void RunQueuedPostTextureOverride(
		const DX12ModIaReplacement::QueuedTextureOverridePost &queued)
	{
		if (!queued.config)
			return;

		const std::shared_ptr<const DX12CompiledTextureOverridePlan> compiledPlan =
			GetCompiledTextureOverridePlan(*queued.config);
		const DX12CompiledTextureOverridePlan *compiledPlanView =
			compiledPlan.get();
		const bool compiledTextureOverride =
			compiledPlanView && !compiledPlanView->hasUnsupportedCommandActions;
		if (compiledTextureOverride) {
			if (compiledPlanView->hasPostCommandListPlan)
				ExecuteCompiledCommandListPlan(
					compiledPlanView->postCommandListPlan, 0, true);
			return;
		}

		if (queued.config->commandLists.post.empty())
			return;

		DX12TextureOverrideMatch match;
		match.config = queued.config;
		match.preRuntimeEffect = true;
		match.postRuntimeEffect = true;
		match.hasThisSrvTarget = queued.hasThisSrvTarget;
		match.compute = queued.compute;
		match.thisSrvBinding = queued.thisSrvBinding;
		match.matchedHash = queued.matchedHash;
		match.resourceHash = queued.resourceHash;
		match.descriptorHash = queued.descriptorHash;
		Bunny::CommandListLinks postLinks;
		postLinks.main = queued.config->commandLists.post;
		RunCommandListLinks(postLinks, 0, true, &match);
	}

	void RunPostShaderOverrideLists(
		const Bunny::ShaderOverrideConfig &config)
	{
		const std::shared_ptr<const DX12CompiledShaderOverridePlan> compiledPlan =
			GetCompiledShaderOverridePlan(config.section);
		if (compiledPlan && !compiledPlan->unsupported) {
			RunCompiledShaderOverridePostPlan(*compiledPlan);
			return;
		}
		Bunny::CommandListLinks postLinks;
		postLinks.main = config.commandLists.post;
		RunCommandListLinks(postLinks, 0, true, nullptr);
	}

public:
	void MarkChanged()
	{
		mChanged = true;
	}

	void SetSkip()
	{
		if (mReplacement)
			mReplacement->skip = true;
	}

	void AppendDraw(const Bunny::CommandListAction &action)
	{
		if (!mReplacement)
			return;
		DX12ModIaReplacement::DrawCall draw;
		draw.indexed =
			action.kind == Bunny::CommandListActionKind::DrawIndexed ||
			action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller;
		draw.fromCaller =
			action.kind == Bunny::CommandListActionKind::DrawFromCaller ||
			action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller;
		if (!draw.fromCaller) {
			draw.count = action.args[0];
			draw.start = action.args[1];
			draw.baseVertex = draw.indexed ? static_cast<INT>(action.args[2]) : 0;
		}
		mReplacement->draws.push_back(draw);
		MarkChanged();
	}

	void AppendDispatch(const Bunny::CommandListAction &action)
	{
		if (!mReplacement)
			return;
		DX12ModIaReplacement::DispatchCall dispatch;
		dispatch.groupsX = action.args[0];
		dispatch.groupsY = action.args[1];
		dispatch.groupsZ = action.args[2];
		mReplacement->dispatches.push_back(dispatch);
		MarkChanged();
	}

	void AppendResourceViews(
		const std::wstring &indexResource,
		const std::vector<DX12CompiledVertexResourceBinding> &vertexResources)
	{
		if (!indexResource.empty() || !vertexResources.empty())
			InvalidateTargetCaches();
		ApplyIaStateResourceOverrides(indexResource, vertexResources);
		if (mPendingResourceViews) {
			DX12PendingResourceViewRequest request;
			request.indexResource = indexResource;
			request.vertexResources = vertexResources;
			mPendingResourceViews->push_back(std::move(request));
			return;
		}
		mChanged |= AppendResourceViewsForCommandList(
			mDevice, mCommandList, mCurrentIaState, indexResource, vertexResources, mReplacement);
	}

	void ApplyIaStateResourceOverrides(
		const std::wstring &indexResource,
		const std::vector<DX12CompiledVertexResourceBinding> &vertexResources)
	{
		if (!mDevice)
			return;
		if (!indexResource.empty()) {
			DX12LoadedResource *resource =
				EnsureLoadedResourceForPreSkin(mDevice, indexResource);
			if (resource && resource->resource) {
				const DXGI_FORMAT indexFormat =
					resource->format == DXGI_FORMAT_R16_UINT ||
					resource->format == DXGI_FORMAT_R32_UINT ?
						resource->format :
						(mCurrentIaState.hasIndexBuffer &&
						 mCurrentIaState.indexView.Format != DXGI_FORMAT_UNKNOWN ?
							mCurrentIaState.indexView.Format :
							DXGI_FORMAT_R32_UINT);
				mCurrentIaState.hasIndexBuffer = true;
				mCurrentIaState.indexView.BufferLocation =
					resource->resource->GetGPUVirtualAddress();
				mCurrentIaState.indexView.SizeInBytes = static_cast<UINT>(
					(std::min)(resource->byteWidth, static_cast<UINT64>(UINT_MAX)));
				mCurrentIaState.indexView.Format = indexFormat;
				mCurrentIaState.indexHash = DX12HashIaBufferView(
					mCurrentIaState.indexView.BufferLocation,
					mCurrentIaState.indexView.SizeInBytes,
					0,
					static_cast<UINT>(indexFormat),
					0);
			}
		}

		for (const DX12CompiledVertexResourceBinding &item : vertexResources) {
			DX12LoadedResource *resource =
				EnsureLoadedResourceForPreSkin(mDevice, item.resource);
			if (!resource || !resource->resource || item.slot >= 32)
				continue;
			const UINT stride =
				resource->stride ? resource->stride :
				FindIaVertexSlot(mCurrentIaState, item.slot) ?
					FindIaVertexSlot(mCurrentIaState, item.slot)->vertexView.StrideInBytes :
					0;
			DX12IaBufferHash *existing = nullptr;
			for (DX12IaBufferHash &buffer : mCurrentIaState.vertexBuffers) {
				if (buffer.slot == item.slot) {
					existing = &buffer;
					break;
				}
			}
			DX12IaBufferHash created = {};
			if (!existing) {
				created.slot = item.slot;
				mCurrentIaState.vertexBuffers.push_back(created);
				existing = &mCurrentIaState.vertexBuffers.back();
			}
			existing->slot = item.slot;
			existing->vertexView.BufferLocation =
				resource->resource->GetGPUVirtualAddress();
			existing->vertexView.SizeInBytes = static_cast<UINT>(
				(std::min)(resource->byteWidth, static_cast<UINT64>(UINT_MAX)));
			existing->vertexView.StrideInBytes = stride;
			existing->hash = DX12HashIaBufferView(
				existing->vertexView.BufferLocation,
				existing->vertexView.SizeInBytes,
				existing->vertexView.StrideInBytes,
				0,
				item.slot);
		}
	}

	bool ApplySetDescriptorResourceAction(
		const Bunny::CommandListTarget &target,
		const std::wstring &resourceName)
	{
		if ((target.kind != Bunny::CommandListTargetKind::ShaderResource &&
		     target.kind != Bunny::CommandListTargetKind::ConstantBuffer &&
		     target.kind != Bunny::CommandListTargetKind::UnorderedAccessView) ||
		    resourceName.empty()) {
			return false;
		}
		const DX12TextureOverrideTargetCacheKey cacheKey = {
			static_cast<uint32_t>(target.kind),
			static_cast<uint32_t>(target.stage),
			target.slot
		};
		const std::vector<DX12ResolvedGraphicsSrvBinding> *baseBindings =
			target.stage == Bunny::CommandListShaderStage::Compute ?
				nullptr :
				GetResolvedGraphicsSrvBindingsForTarget(target);
		std::vector<DX12ResolvedGraphicsSrvBinding> resolvedBindings;
		if (!BuildResolvedDescriptorBindingsForResourceOverride(
				mDevice,
				mCommandList,
				target,
				resourceName,
				baseBindings,
				&resolvedBindings)) {
			return false;
		}
		mShaderResourceOverrideBindingsByTarget[cacheKey] =
			std::move(resolvedBindings);
		InvalidateGraphicsSrvBindingCaches();
		InvalidateTargetCaches();
		return true;
	}

	bool FindTargetTextureOverride(
		const Bunny::CommandListTarget &target,
		std::vector<DX12TextureOverrideMatch> *matches)
	{
		const DX12TextureOverrideTargetCacheKey cacheKey = {
			static_cast<uint32_t>(target.kind),
			static_cast<uint32_t>(target.stage),
			target.slot
		};
		auto cached = mTargetTextureOverrideCache.find(cacheKey);
		if (cached != mTargetTextureOverrideCache.end()) {
			if (matches)
				*matches = cached->second;
			return !cached->second.empty();
		}

		const bool descriptorTarget =
			target.kind == Bunny::CommandListTargetKind::ConstantBuffer ||
			target.kind == Bunny::CommandListTargetKind::ShaderResource ||
			target.kind == Bunny::CommandListTargetKind::UnorderedAccessView;
		if (descriptorTarget) {
			auto overridden = mShaderResourceOverrideBindingsByTarget.find(cacheKey);
			auto descriptorCached = mGraphicsSrvTextureOverrideCache.find(cacheKey);
			if (descriptorCached != mGraphicsSrvTextureOverrideCache.end()) {
				mTargetTextureOverrideCache.emplace(cacheKey, descriptorCached->second);
				if (matches)
					*matches = descriptorCached->second;
				return !descriptorCached->second.empty();
			}
			if (!mGraphicsSrvTextureOverrideCacheResolved)
				BuildDescriptorTextureOverrideCache();
			descriptorCached = mGraphicsSrvTextureOverrideCache.find(cacheKey);
			if (descriptorCached != mGraphicsSrvTextureOverrideCache.end()) {
				mTargetTextureOverrideCache.emplace(cacheKey, descriptorCached->second);
				if (matches)
					*matches = descriptorCached->second;
				return !descriptorCached->second.empty();
			}
			if (overridden != mShaderResourceOverrideBindingsByTarget.end()) {
				mTargetTextureOverrideCache.emplace(
					cacheKey,
					std::vector<DX12TextureOverrideMatch>());
				return false;
			}
			if (mSkipDirectDescriptorTargetsActive)
				return false;
			observedDescriptorTarget = true;
			mTargetTextureOverrideCache.emplace(
				cacheKey,
				std::vector<DX12TextureOverrideMatch>());
			return false;
		}
		const bool found = ::FindTargetTextureOverride(
			mCommandList, mCurrentIaState, target, mVertexCount, mIndexCount, mInstanceCount,
			mFirstVertex, mFirstIndex, mFirstInstance, matches,
			nullptr);
		mTargetTextureOverrideCache.emplace(
			cacheKey,
			matches ? *matches : std::vector<DX12TextureOverrideMatch>());
		return found;
	}

	void RunTextureOverride(
		const DX12TextureOverrideMatch &match, int depth, bool includePost)
	{
		const Bunny::TextureOverrideConfig *config = match.config;
		if (!mReplacement || depth > 16 || !config)
			return;
		BoolScope directDescriptorSkipScope(
			&mSkipDirectDescriptorTargetsActive,
			false);

		const std::shared_ptr<const DX12CompiledTextureOverridePlan> compiledPlan =
			GetCompiledTextureOverridePlan(*config);
		const DX12CompiledTextureOverridePlan *compiledPlanView =
			compiledPlan.get();
		const bool compiledTextureOverride =
			compiledPlanView && !compiledPlanView->hasUnsupportedCommandActions;
		if (includePost) {
			if (!match.postRuntimeEffect)
				return;
			if (compiledTextureOverride) {
				if (compiledPlanView->hasPostCommandListPlan) {
					ExecuteCompiledCommandListPlan(
						compiledPlanView->postCommandListPlan, depth + 1, true);
				}
			} else {
				for (const std::wstring &list : config->commandLists.post) {
					Bunny::CommandListLinks links;
					links.main.push_back(list);
					RunCommandListLinks(links, depth + 1, true, &match);
				}
			}
			return;
		}

		QueuePostTextureOverride(match);
		if (!match.preRuntimeEffect)
			return;

		if (compiledTextureOverride) {
			if (compiledPlanView->hasPreCommandListPlan)
				ExecuteCompiledCommandListPlan(
					compiledPlanView->preCommandListPlan, depth + 1, false);
			RunCompiledTextureOverridePlan(*compiledPlanView, depth, includePost);
		} else {
			if (mExecuteCommands) {
				for (const std::wstring &list : config->commandLists.pre) {
					Bunny::CommandListLinks links;
					links.main.push_back(list);
					RunCommandListLinks(links, depth + 1, false, &match);
				}
			}
			RunTextureOverrideActions(*config, depth, includePost, &match);
			if (mExecuteCommands) {
				for (const std::wstring &list : config->commandLists.main) {
					Bunny::CommandListLinks links;
					links.main.push_back(list);
					RunCommandListLinks(links, depth + 1, false, &match);
				}

			}
		}

		const std::vector<DX12CurrentShaderResourceBinding> *graphicsSrvBindings =
			GetGraphicsSrvBindings();
		const bool drawSrvChanged = compiledPlanView ?
			DX12DrawSrvReplacementUtils::ApplyCompiledPlan(
				mDevice, mCommandList, *compiledPlanView, &match, mReplacement,
				graphicsSrvBindings) :
			DX12DrawSrvReplacementUtils::ApplyConfig(
				mDevice, mCommandList, *config, &match, mReplacement,
				graphicsSrvBindings);
		if (drawSrvChanged)
			MarkChanged();
	}

	bool RunCompiledTextureOverridePlan(
		const DX12CompiledTextureOverridePlan &plan, int depth, bool includePost)
	{
		if (plan.hasMainActionPlan) {
			ExecuteCompiledCommandListPlan(plan.mainActionPlan, depth, includePost);
			return true;
		}
		AppendResourceViews(plan.indexResource, plan.vertexResources);
		const bool executeActions = mExecuteCommands || mExecuteDrawActions;
		if (executeActions && plan.handlingSkip)
			SetSkip();
		if (executeActions) {
			for (const DX12ModIaReplacement::DrawCall &draw : plan.draws) {
				mReplacement->draws.push_back(draw);
				MarkChanged();
			}
			for (const DX12ModIaReplacement::DispatchCall &dispatch : plan.dispatches) {
				mReplacement->dispatches.push_back(dispatch);
				MarkChanged();
			}
		}
		return true;
	}

	void RunCompiledShaderOverridePlan(const DX12CompiledShaderOverridePlan &plan)
	{
		if (plan.unsupported)
			return;
		BoolScope directDescriptorSkipScope(
			&mSkipDirectDescriptorTargetsActive,
			mSkipDirectDescriptorTargets);
		if (mReplacement) {
			for (const std::wstring &list : plan.postCommandLists)
				mReplacement->AddPostCommandList(list);
		}
		ExecuteCompiledCommandListPlan(plan.mainPlan, 0, false);
	}

	void CollectDescriptorCheckTargetsFromCompiledPlans(
		const std::vector<DX12CompiledShaderOverridePlan> &plans)
	{
		mDescriptorTargetScratch.clear();
		for (const DX12CompiledShaderOverridePlan &plan : plans) {
			if (!plan.mainPlan.hasDescriptorCheckTargets)
				continue;
			for (const Bunny::CommandListTarget &target :
				 plan.mainPlan.descriptorCheckTargets) {
				bool exists = false;
				for (const Bunny::CommandListTarget &existing :
					 mDescriptorTargetScratch) {
					if (existing.kind == target.kind &&
						existing.stage == target.stage &&
						existing.slot == target.slot) {
						exists = true;
						break;
					}
				}
				if (!exists)
					mDescriptorTargetScratch.push_back(target);
			}
		}
	}

	void RunCompiledShaderOverridePostPlan(const DX12CompiledShaderOverridePlan &plan)
	{
		if (plan.unsupported || !plan.hasPostCommandListPlan)
			return;
		ExecuteCompiledCommandListPlan(plan.postCommandListPlan, 0, true);
	}

	void BuildDescriptorTextureOverrideCacheForTargets(
		const std::vector<Bunny::CommandListTarget> *targets)
	{
		mGraphicsSrvTextureOverrideCacheResolved = true;
		mDescriptorTextureOverrideCachePrebuilt = true;
		mGraphicsSrvTextureOverrideCache.clear();
		if (!mCommandList)
			return;

		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		if (!snapshot)
			return;
		if (!HasPotentialResolvedDescriptorPrefilterCandidates(
				*snapshot,
				mVertexCount,
				mIndexCount))
			return;

		static thread_local std::vector<uint32_t> tlsAllowedSectionIds;
		CollectResolvedDescriptorPrefilterSectionIds(
			*snapshot,
			mVertexCount,
			mIndexCount,
			&tlsAllowedSectionIds);
		if (tlsAllowedSectionIds.empty())
			return;

		auto appendTarget = [this, &snapshot](const Bunny::CommandListTarget &target) {
			const DX12TextureOverrideTargetCacheKey cacheKey = {
				static_cast<uint32_t>(target.kind),
				static_cast<uint32_t>(target.stage),
				target.slot
			};
			auto overridden = mShaderResourceOverrideBindingsByTarget.find(cacheKey);
			const std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindingsView = nullptr;
			std::vector<DX12ResolvedGraphicsSrvBinding> resolvedBindings;
			if (overridden != mShaderResourceOverrideBindingsByTarget.end()) {
				resolvedBindingsView = &overridden->second;
			} else if (target.kind == Bunny::CommandListTargetKind::ShaderResource &&
				(target.stage == Bunny::CommandListShaderStage::Vertex ||
				 target.stage == Bunny::CommandListShaderStage::Pixel)) {
				resolvedBindingsView = GetResolvedGraphicsSrvBindingsForTarget(target);
			} else {
				std::vector<DX12CurrentShaderResourceBinding> bindings;
				if (!DX12BindingTracker::GetCurrentShaderResourceBindingsByRegister(
						mCommandList,
						target.stage == Bunny::CommandListShaderStage::Compute,
						DescriptorRangeTypeForTarget(target.kind),
						target.slot,
						0,
						&bindings) || bindings.empty()) {
					return;
				}
				ResolveGraphicsSrvBindings(
					mCommandList,
					mDevice,
					bindings,
					&resolvedBindings);
				resolvedBindingsView = &resolvedBindings;
			}
			if (!resolvedBindingsView || resolvedBindingsView->empty())
				return;

			std::vector<DX12TextureOverrideMatch> resolvedMatches;
			FindDescriptorTextureOverridesFromResolvedBindings(
				snapshot,
				target,
				*resolvedBindingsView,
				mVertexCount,
				mIndexCount,
				mInstanceCount,
				mFirstVertex,
				mFirstIndex,
				mFirstInstance,
				&resolvedMatches);
			if (!resolvedMatches.empty())
				mGraphicsSrvTextureOverrideCache[cacheKey] = std::move(resolvedMatches);
		};

		if (targets) {
			if (targets->empty())
				return;
			for (const Bunny::CommandListTarget &target : *targets)
				appendTarget(target);
			return;
		}

		BuildDescriptorTextureOverrideCache();
	}

	void PrimeDescriptorTextureOverrideCacheForTargets(
		const std::vector<Bunny::CommandListTarget> *targets)
	{
		if (mDescriptorTextureOverrideCachePrebuilt)
			return;
		BuildDescriptorTextureOverrideCacheForTargets(targets);
	}

	void CollectDescriptorCheckTargetsFromActions(
		const std::vector<Bunny::CommandListAction> &actions)
	{
		mDescriptorTargetScratch.clear();
		for (const Bunny::CommandListAction &action : actions) {
			if (action.kind != Bunny::CommandListActionKind::CheckTextureOverride)
				continue;
			if (action.target.kind != Bunny::CommandListTargetKind::ConstantBuffer &&
				action.target.kind != Bunny::CommandListTargetKind::ShaderResource &&
				action.target.kind != Bunny::CommandListTargetKind::UnorderedAccessView)
				continue;
			bool exists = false;
			for (const Bunny::CommandListTarget &existing : mDescriptorTargetScratch) {
				if (existing.kind == action.target.kind &&
					existing.stage == action.target.stage &&
					existing.slot == action.target.slot) {
					exists = true;
					break;
				}
			}
			if (!exists)
				mDescriptorTargetScratch.push_back(action.target);
		}
	}

	void RunCheckTextureOverrideTarget(
		const Bunny::CommandListTarget &target,
		int depth,
		bool includePost)
	{
		if (!HasTextureOverrideRuntimeEffect(includePost))
			return;
		mTargetMatchScratch.clear();
		if (!FindTargetTextureOverride(target, &mTargetMatchScratch))
			return;
		for (const DX12TextureOverrideMatch &match : mTargetMatchScratch)
			RunTextureOverride(match, depth + 1, includePost);
	}

	bool RunCompiledCommandListPlan(
		const std::wstring &name, int depth, bool includePost)
	{
		if (!mReplacement)
			return false;
		if (!HasCommandListRuntimeEffect(name, includePost))
			return false;
		if (!EnterCommandList(name))
			return true;
		const std::shared_ptr<const DX12CompiledCommandListPlan> plan =
			GetCompiledCommandListPlan(name);
		if (!plan) {
			LeaveCommandList();
			return false;
		}
		ExecuteCompiledCommandListPlan(*plan, depth, includePost);
		LeaveCommandList();
		return true;
	}

	void ExecuteCompiledCommandListPlan(
		const DX12CompiledCommandListPlan &plan, int depth, bool includePost)
	{
		const bool executeActions = mReplacement && (mExecuteCommands || mExecuteDrawActions);
		if (plan.hasDescriptorCheckTargets && !plan.descriptorCheckTargets.empty())
			PrimeDescriptorTextureOverrideCacheForTargets(&plan.descriptorCheckTargets);
		if (!plan.ops.empty()) {
			for (const DX12CompiledCommandListPlan::Op &op : plan.ops) {
				switch (op.kind) {
				case DX12CompiledCommandListPlan::Op::Kind::HandlingSkip:
					if (executeActions)
						SetSkip();
					break;
				case DX12CompiledCommandListPlan::Op::Kind::SetIndexBuffer:
					AppendResourceViews(
						op.resource,
						std::vector<DX12CompiledVertexResourceBinding>());
					break;
				case DX12CompiledCommandListPlan::Op::Kind::SetVertexBuffer:
					AppendResourceViews(
						L"",
						std::vector<DX12CompiledVertexResourceBinding>{
							{ op.target.slot, op.resource }});
					break;
				case DX12CompiledCommandListPlan::Op::Kind::SetShaderResource:
					ApplySetDescriptorResourceAction(op.target, op.resource);
					break;
				case DX12CompiledCommandListPlan::Op::Kind::SetConstantBuffer:
					ApplySetDescriptorResourceAction(op.target, op.resource);
					break;
				case DX12CompiledCommandListPlan::Op::Kind::SetUnorderedAccessView:
					ApplySetDescriptorResourceAction(op.target, op.resource);
					break;
				case DX12CompiledCommandListPlan::Op::Kind::Draw:
					if (executeActions) {
						mReplacement->draws.push_back(op.draw);
						MarkChanged();
					}
					break;
				case DX12CompiledCommandListPlan::Op::Kind::Dispatch:
					if (executeActions) {
						mReplacement->dispatches.push_back(op.dispatch);
						MarkChanged();
					}
					break;
				case DX12CompiledCommandListPlan::Op::Kind::CheckTextureOverride:
					if (!mExecuteCommands)
						break;
					RunCheckTextureOverrideTarget(op.target, depth, includePost);
					break;
				}
			}
			return;
		}
		AppendResourceViews(plan.indexResource, plan.vertexResources);
		if (plan.handlingSkip)
			SetSkip();
		for (const DX12ModIaReplacement::DrawCall &draw : plan.draws) {
			mReplacement->draws.push_back(draw);
			MarkChanged();
		}
		for (const DX12ModIaReplacement::DispatchCall &dispatch : plan.dispatches) {
			mReplacement->dispatches.push_back(dispatch);
			MarkChanged();
		}
	}

	const std::vector<DX12CurrentShaderResourceBinding> *GetGraphicsSrvBindings()
	{
		if (!mCommandList)
			return nullptr;
		if (!mGraphicsSrvBindingsResolved) {
			mGraphicsSrvBindingsResolved = true;
			// DX11 draw-time matching consumes the current bound resources, not a
			// stripped patch layout. TextureOverride matching and draw SRV patching
			// therefore share the same fully resolved current SRV snapshot.
			DX12BindingTracker::GetCurrentShaderResourceBindings(
				mCommandList,
				false,
				D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				&mGraphicsSrvBindings);
			for (const auto &overrideEntry : mShaderResourceOverrideBindingsByTarget) {
				const DX12TextureOverrideTargetCacheKey &key = overrideEntry.first;
				if (key.kind != static_cast<uint32_t>(Bunny::CommandListTargetKind::ShaderResource) ||
				    key.stage == static_cast<uint32_t>(Bunny::CommandListShaderStage::Compute)) {
					continue;
				}
				Bunny::CommandListTarget target;
				target.kind = Bunny::CommandListTargetKind::ShaderResource;
				target.stage = static_cast<Bunny::CommandListShaderStage>(key.stage);
				target.slot = key.slot;
				mGraphicsSrvBindings.erase(
					std::remove_if(
						mGraphicsSrvBindings.begin(),
						mGraphicsSrvBindings.end(),
						[&target](const DX12CurrentShaderResourceBinding &binding) {
							return DescriptorBindingMatchesTarget(binding, target);
						}),
					mGraphicsSrvBindings.end());
				for (const DX12ResolvedGraphicsSrvBinding &resolved : overrideEntry.second)
					mGraphicsSrvBindings.push_back(resolved.binding);
			}
		}
		return &mGraphicsSrvBindings;
	}

	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &
	GetTextureOverrideRuntimeSnapshot()
	{
		if (!mTextureOverrideRuntimeSnapshotResolved) {
			mTextureOverrideRuntimeSnapshotResolved = true;
			mTextureOverrideRuntimeSnapshot = LoadTextureOverrideRuntimeSnapshot();
		}
		return mTextureOverrideRuntimeSnapshot;
	}

	std::shared_ptr<const DX12CompiledTextureOverridePlan>
	GetCompiledTextureOverridePlan(const Bunny::TextureOverrideConfig &config)
	{
		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		if (!snapshot)
			return nullptr;
		auto found = snapshot->compiledTextureOverridePlans.find(config.section);
		return found == snapshot->compiledTextureOverridePlans.end() ?
			nullptr : found->second;
	}

	std::shared_ptr<const DX12CompiledShaderOverridePlan>
	GetCompiledShaderOverridePlan(const std::wstring &section)
	{
		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		if (!snapshot)
			return nullptr;
		auto found = snapshot->compiledShaderOverridePlans.find(section);
		return found == snapshot->compiledShaderOverridePlans.end() ?
			nullptr : found->second;
	}

	std::shared_ptr<const DX12CompiledCommandListPlan>
	GetCompiledCommandListPlan(const std::wstring &name)
	{
		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		if (!snapshot)
			return nullptr;
		auto found = snapshot->compiledCommandListPlans.find(name);
		return found == snapshot->compiledCommandListPlans.end() ?
			nullptr : found->second;
	}

	bool HasTextureOverrideRuntimeEffect(bool includePost)
	{
		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		return !snapshot || (includePost ?
			snapshot->anyTextureOverridePostRuntimeEffect :
			snapshot->anyTextureOverridePreRuntimeEffect);
	}

	bool HasCommandListRuntimeEffect(const std::wstring &name, bool includePost)
	{
		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		return !snapshot || CommandListRuntimeEffectLocked(snapshot, name, includePost);
	}

	const std::vector<DX12ResolvedGraphicsSrvBinding> *GetResolvedGraphicsSrvBindings()
	{
		if (!mResolvedGraphicsSrvBindingsResolved) {
			mResolvedGraphicsSrvBindingsResolved = true;
			DX12SimulateContext *runtimeState =
				mCommandList ?
					DX12CommandListRuntime::GetStatePtrFast(mCommandList) :
					nullptr;
			const UINT64 descriptorQuerySerial =
				(runtimeState && runtimeState->graphicsDescriptorQuerySerial) ?
					runtimeState->graphicsDescriptorQuerySerial : 1;
			if (!TryLoadResolvedGraphicsSrvBindings(
					runtimeState,
					descriptorQuerySerial,
					&mResolvedGraphicsSrvBindings)) {
				const std::vector<DX12CurrentShaderResourceBinding> *bindings =
					GetGraphicsSrvBindings();
				if (bindings) {
					ResolveGraphicsSrvBindings(
						mCommandList,
						mDevice,
						*bindings,
						&mResolvedGraphicsSrvBindings);
					StoreResolvedGraphicsSrvBindings(
						runtimeState,
						descriptorQuerySerial,
						mResolvedGraphicsSrvBindings);
				}
			}
		}
		return &mResolvedGraphicsSrvBindings;
	}

	const std::vector<DX12ResolvedGraphicsSrvBinding> *
	GetResolvedGraphicsSrvBindingsForTarget(const Bunny::CommandListTarget &target)
	{
		if (target.kind != Bunny::CommandListTargetKind::ShaderResource ||
		    (target.stage != Bunny::CommandListShaderStage::Vertex &&
		     target.stage != Bunny::CommandListShaderStage::Pixel)) {
			return nullptr;
		}
		if (!mGraphicsSrvBindingsByTargetResolved) {
			mGraphicsSrvBindingsByTargetResolved = true;
			const std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings =
				GetResolvedGraphicsSrvBindings();
			if (resolvedBindings) {
				for (const DX12ResolvedGraphicsSrvBinding &resolved :
				     *resolvedBindings) {
					if (resolved.binding.registerSpace != 0)
						continue;
					if (resolved.binding.shaderVisibility ==
					    D3D12_SHADER_VISIBILITY_VERTEX ||
					    resolved.binding.shaderVisibility ==
					        D3D12_SHADER_VISIBILITY_ALL) {
						mGraphicsSrvBindingsByTarget[DX12TextureOverrideTargetCacheKey{
							static_cast<uint32_t>(
								Bunny::CommandListTargetKind::ShaderResource),
							static_cast<uint32_t>(
								Bunny::CommandListShaderStage::Vertex),
							resolved.binding.shaderRegister
						}].push_back(resolved);
					}
					if (resolved.binding.shaderVisibility ==
					    D3D12_SHADER_VISIBILITY_PIXEL ||
					    resolved.binding.shaderVisibility ==
					        D3D12_SHADER_VISIBILITY_ALL) {
						mGraphicsSrvBindingsByTarget[DX12TextureOverrideTargetCacheKey{
							static_cast<uint32_t>(
								Bunny::CommandListTargetKind::ShaderResource),
							static_cast<uint32_t>(
								Bunny::CommandListShaderStage::Pixel),
							resolved.binding.shaderRegister
						}].push_back(resolved);
					}
				}
			}
		}
		const DX12TextureOverrideTargetCacheKey cacheKey = {
			static_cast<uint32_t>(target.kind),
			static_cast<uint32_t>(target.stage),
			target.slot
		};
		auto found = mGraphicsSrvBindingsByTarget.find(cacheKey);
		return found == mGraphicsSrvBindingsByTarget.end() ?
			nullptr : &found->second;
	}

	void BuildDescriptorTextureOverrideCache()
	{
		mGraphicsSrvTextureOverrideCacheResolved = true;
		mGraphicsSrvTextureOverrideCache.clear();
		if (!mCommandList)
			return;

		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		if (!snapshot)
			return;
		if (!HasPotentialResolvedDescriptorPrefilterCandidates(
				*snapshot,
				mVertexCount,
				mIndexCount)) {
			return;
		}

		static thread_local std::vector<uint32_t> tlsAllowedSectionIds;
		CollectResolvedDescriptorPrefilterSectionIds(
			*snapshot,
			mVertexCount,
			mIndexCount,
			&tlsAllowedSectionIds);
		if (tlsAllowedSectionIds.empty())
			return;

		auto appendCurrentBindings = [this, &snapshot](
			D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
			Bunny::CommandListTargetKind kind,
			bool compute) {
			if (kind == Bunny::CommandListTargetKind::ShaderResource && !compute) {
				const std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings =
					GetResolvedGraphicsSrvBindings();
				if (resolvedBindings && !resolvedBindings->empty()) {
					AppendResolvedBindingsToDescriptorTextureOverrideCache(
						snapshot,
						*resolvedBindings,
						kind,
						false,
						tlsAllowedSectionIds,
						&mGraphicsSrvTextureOverrideCache);
				}
				return;
			}

			std::vector<DX12CurrentShaderResourceBinding> bindings;
			if (!DX12BindingTracker::GetCurrentShaderResourceBindings(
					mCommandList,
					compute,
					rangeType,
					&bindings) || bindings.empty()) {
				return;
			}

			std::vector<DX12ResolvedGraphicsSrvBinding> resolvedBindings;
			ResolveGraphicsSrvBindings(
				mCommandList,
				mDevice,
				bindings,
				&resolvedBindings);
			if (resolvedBindings.empty())
				return;
			AppendResolvedBindingsToDescriptorTextureOverrideCache(
				snapshot,
				resolvedBindings,
				kind,
				compute,
				tlsAllowedSectionIds,
				&mGraphicsSrvTextureOverrideCache);
		};

		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			Bunny::CommandListTargetKind::ShaderResource,
			false);
		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
			Bunny::CommandListTargetKind::ConstantBuffer,
			false);
		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
			Bunny::CommandListTargetKind::UnorderedAccessView,
			false);
		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			Bunny::CommandListTargetKind::ShaderResource,
			true);
		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
			Bunny::CommandListTargetKind::ConstantBuffer,
			true);
		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
			Bunny::CommandListTargetKind::UnorderedAccessView,
			true);

		for (const auto &overrideEntry : mShaderResourceOverrideBindingsByTarget) {
			Bunny::CommandListTarget target;
			target.kind = static_cast<Bunny::CommandListTargetKind>(overrideEntry.first.kind);
			target.stage = static_cast<Bunny::CommandListShaderStage>(overrideEntry.first.stage);
			target.slot = overrideEntry.first.slot;
			std::vector<DX12TextureOverrideMatch> resolvedMatches;
			if (!overrideEntry.second.empty()) {
				FindDescriptorTextureOverridesFromResolvedBindings(
					snapshot,
					target,
					overrideEntry.second,
					mVertexCount,
					mIndexCount,
					mInstanceCount,
					mFirstVertex,
					mFirstIndex,
					mFirstInstance,
					&resolvedMatches);
			}
			if (resolvedMatches.empty()) {
				mGraphicsSrvTextureOverrideCache.erase(overrideEntry.first);
				continue;
			}
			mGraphicsSrvTextureOverrideCache[overrideEntry.first] =
				std::move(resolvedMatches);
		}
	}

	bool FindGraphicsSrvTextureOverrides(
		const Bunny::CommandListTarget &target,
		std::vector<DX12TextureOverrideMatch> *matches)
	{
		if (matches)
			matches->clear();
		const DX12TextureOverrideTargetCacheKey cacheKey = {
			static_cast<uint32_t>(target.kind),
			static_cast<uint32_t>(target.stage),
			target.slot
		};
		if (!mGraphicsSrvTextureOverrideCacheResolved) {
			BuildDescriptorTextureOverrideCache();
		}
		auto cached = mGraphicsSrvTextureOverrideCache.find(cacheKey);
		if (cached != mGraphicsSrvTextureOverrideCache.end()) {
			if (matches)
				*matches = cached->second;
			return !cached->second.empty();
		}
		return false;
	}

	void InvalidateTargetCaches()
	{
		mDescriptorTextureOverrideCachePrebuilt = false;
		mGraphicsSrvTextureOverrideCacheResolved = false;
		mGraphicsSrvTextureOverrideCache.clear();
		mTargetTextureOverrideCache.clear();
	}

	void InvalidateGraphicsSrvBindingCaches()
	{
		mGraphicsSrvBindingsResolved = false;
		mResolvedGraphicsSrvBindingsResolved = false;
		mGraphicsSrvBindingsByTargetResolved = false;
		mGraphicsSrvBindings.clear();
		mResolvedGraphicsSrvBindings.clear();
		mGraphicsSrvBindingsByTarget.clear();
	}

	void RunTextureOverrideActions(
		const Bunny::TextureOverrideConfig &config, int depth, bool includePost,
		const DX12TextureOverrideMatch *thisMatch)
	{
		RunActions(config.actions, depth, includePost, thisMatch);
	}

	void RunCommandList(
		const Bunny::CommandListConfig &commandList, int depth, bool includePost,
		const DX12TextureOverrideMatch *thisMatch)
	{
		if (depth > 16)
			return;
		if (RunCompiledCommandListPlan(commandList.section, depth, includePost))
			return;
		if (!EnterCommandList(commandList.section))
			return;
		CollectDescriptorCheckTargetsFromActions(commandList.actions);
		if (!mDescriptorTargetScratch.empty())
			PrimeDescriptorTextureOverrideCacheForTargets(&mDescriptorTargetScratch);
		CommandVmRunScope vmScope(&mVm, includePost);
		for (const Bunny::CommandListAction &action : commandList.actions) {
			if (ApplyVmAction(action, includePost))
				continue;
			if (!mVm.Active())
				continue;
			switch (action.kind) {
			case Bunny::CommandListActionKind::Run: {
				const std::shared_ptr<const Bunny::CommandListConfig> config =
					SnapshotCommandListConfig(action.commandList);
				if (config)
					RunCommandList(*config, depth + 1, includePost, thisMatch);
				break;
			}
			case Bunny::CommandListActionKind::CheckTextureOverride: {
				if (!mReplacement)
					break;
				RunCheckTextureOverrideTarget(action.target, depth, includePost);
				break;
			}
			case Bunny::CommandListActionKind::HandlingSkip:
				if (mReplacement)
					SetSkip();
				break;
			case Bunny::CommandListActionKind::SetIndexBuffer:
				if (mReplacement)
					AppendResourceViews(action.resource, std::vector<DX12CompiledVertexResourceBinding>());
				break;
			case Bunny::CommandListActionKind::SetVertexBuffer:
				if (mReplacement)
					AppendResourceViews(
						L"", std::vector<DX12CompiledVertexResourceBinding>{
							{ action.target.slot, action.resource }});
				break;
			case Bunny::CommandListActionKind::SetShaderResource:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::SetConstantBuffer:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::SetUnorderedAccessView:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::Draw:
			case Bunny::CommandListActionKind::DrawIndexed:
			case Bunny::CommandListActionKind::DrawFromCaller:
			case Bunny::CommandListActionKind::DrawIndexedFromCaller:
				if (mReplacement)
					AppendDraw(action);
				break;
			case Bunny::CommandListActionKind::Dispatch:
				if (mReplacement)
					AppendDispatch(action);
				break;
			case Bunny::CommandListActionKind::ControlFlow:
			case Bunny::CommandListActionKind::VariableAssignment:
			case Bunny::CommandListActionKind::PostVariableAssignment:
			case Bunny::CommandListActionKind::LocalVariableAssignment:
			case Bunny::CommandListActionKind::ResourceAssignment:
			case Bunny::CommandListActionKind::SetShader:
			case Bunny::CommandListActionKind::SetShaderConstant:
			case Bunny::CommandListActionKind::SetRenderState:
				break;
			}
		}
		LeaveCommandList();
	}

	void RunActions(
		const std::vector<Bunny::CommandListAction> &actions, int depth, bool includePost,
		const DX12TextureOverrideMatch *thisMatch)
	{
		if (depth > 16)
			return;

		const bool executeActions = mReplacement && (mExecuteCommands || mExecuteDrawActions);
		CollectDescriptorCheckTargetsFromActions(actions);
		if (!mDescriptorTargetScratch.empty())
			PrimeDescriptorTextureOverrideCacheForTargets(&mDescriptorTargetScratch);
		CommandVmRunScope vmScope(&mVm, includePost);
		for (const Bunny::CommandListAction &action : actions) {
			if (ApplyVmAction(action, includePost))
				continue;
			if (!mVm.Active())
				continue;
			switch (action.kind) {
			case Bunny::CommandListActionKind::Run: {
				if (!mExecuteCommands)
					break;
				const std::shared_ptr<const Bunny::CommandListConfig> config =
					SnapshotCommandListConfig(action.commandList);
				if (config)
					RunCommandList(*config, depth + 1, includePost, thisMatch);
				break;
			}
			case Bunny::CommandListActionKind::CheckTextureOverride: {
				if (!mReplacement || !mExecuteCommands)
					break;
				RunCheckTextureOverrideTarget(action.target, depth, includePost);
				break;
			}
			case Bunny::CommandListActionKind::HandlingSkip:
				if (executeActions)
					SetSkip();
				break;
			case Bunny::CommandListActionKind::SetIndexBuffer:
				if (mReplacement)
					AppendResourceViews(action.resource, std::vector<DX12CompiledVertexResourceBinding>());
				break;
			case Bunny::CommandListActionKind::SetVertexBuffer:
				if (mReplacement)
					AppendResourceViews(
						L"", std::vector<DX12CompiledVertexResourceBinding>{
							{ action.target.slot, action.resource }});
				break;
			case Bunny::CommandListActionKind::SetShaderResource:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::SetConstantBuffer:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::SetUnorderedAccessView:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::Draw:
			case Bunny::CommandListActionKind::DrawIndexed:
			case Bunny::CommandListActionKind::DrawFromCaller:
			case Bunny::CommandListActionKind::DrawIndexedFromCaller:
				if (executeActions)
					AppendDraw(action);
				break;
			case Bunny::CommandListActionKind::Dispatch:
				if (executeActions)
					AppendDispatch(action);
				break;
			case Bunny::CommandListActionKind::ControlFlow:
			case Bunny::CommandListActionKind::VariableAssignment:
			case Bunny::CommandListActionKind::PostVariableAssignment:
			case Bunny::CommandListActionKind::LocalVariableAssignment:
			case Bunny::CommandListActionKind::ResourceAssignment:
			case Bunny::CommandListActionKind::SetShader:
			case Bunny::CommandListActionKind::SetShaderConstant:
			case Bunny::CommandListActionKind::SetRenderState:
				break;
			}
		}
	}

	bool ApplyVmAction(const Bunny::CommandListAction &action, bool includePost)
	{
		switch (action.kind) {
		case Bunny::CommandListActionKind::ControlFlow:
			observedDynamicCommandState = true;
			mVm.ApplyControlFlow(action.rawLine.empty() ? action.key : action.rawLine);
			return true;
		case Bunny::CommandListActionKind::VariableAssignment:
			observedDynamicCommandState = true;
			if (mVm.Active())
				mVm.Assign(action.key, action.value);
			return true;
		case Bunny::CommandListActionKind::PostVariableAssignment:
			observedDynamicCommandState = true;
			if (includePost && mVm.Active())
				mVm.QueuePostAssign(action.key, action.value);
			return true;
		case Bunny::CommandListActionKind::LocalVariableAssignment:
			observedDynamicCommandState = true;
			if (mVm.Active())
				mVm.DeclareLocal(action.key, action.value);
			return true;
		case Bunny::CommandListActionKind::SetShaderConstant:
			observedDynamicCommandState = true;
			if (mVm.Active())
				mVm.AssignSymbol(action.key, action.value);
			return true;
		default:
			return false;
		}
	}

	void RunCommandListLinks(
		const Bunny::CommandListLinks &links, int depth, bool includePost,
		const DX12TextureOverrideMatch *thisMatch)
	{
		if (depth > 16)
			return;

		for (const std::wstring &list : links.pre) {
			if (!HasCommandListRuntimeEffect(list, false))
				continue;
			if (RunCompiledCommandListPlan(list, depth + 1, false))
				continue;
			const std::shared_ptr<const Bunny::CommandListConfig> config =
				SnapshotCommandListConfig(list);
			if (config)
				RunCommandList(*config, depth + 1, false, thisMatch);
		}
		for (const std::wstring &list : links.main) {
			if (!HasCommandListRuntimeEffect(list, includePost))
				continue;
			if (RunCompiledCommandListPlan(list, depth + 1, includePost))
				continue;
			const std::shared_ptr<const Bunny::CommandListConfig> config =
				SnapshotCommandListConfig(list);
			if (config)
				RunCommandList(*config, depth + 1, includePost, thisMatch);
		}
		if (includePost) {
			for (const std::wstring &list : links.post) {
				if (!HasCommandListRuntimeEffect(list, true))
					continue;
				if (RunCompiledCommandListPlan(list, depth + 1, true))
					continue;
				const std::shared_ptr<const Bunny::CommandListConfig> config =
					SnapshotCommandListConfig(list);
				if (config)
					RunCommandList(*config, depth + 1, true, thisMatch);
			}
		}
	}

	bool EnterCommandList(const std::wstring &name)
	{
		if (name.empty())
			return true;
		std::pair<std::unordered_set<std::wstring>::iterator, bool> inserted =
			mCommandListStackSet.insert(name);
		if (inserted.second) {
			mCommandListStack.push_back(name);
			return true;
		}
#if defined(_DEBUG)
		DX12LogManager::Fields("DX12CommandListCycleSuppressed",
			"\"section\":\"%S\",\"depth\":%zu", name.c_str(),
			mCommandListStack.size());
#endif
		return false;
	}

	void LeaveCommandList()
	{
		if (!mCommandListStack.empty()) {
			mCommandListStackSet.erase(mCommandListStack.back());
			mCommandListStack.pop_back();
		}
	}

	ID3D12Device *mDevice = nullptr;
	ID3D12GraphicsCommandList *mCommandList = nullptr;
	const DX12IaHashState &mIaState;
	DX12IaHashState mCurrentIaState;
	uint32_t mVertexCount = 0;
	uint32_t mIndexCount = 0;
	uint32_t mInstanceCount = 0;
	uint32_t mFirstVertex = 0;
	uint32_t mFirstIndex = 0;
	uint32_t mFirstInstance = 0;
	DX12ModIaReplacement *mReplacement = nullptr;
	std::vector<DX12PendingResourceViewRequest> *mPendingResourceViews = nullptr;
	bool mChanged = false;
	bool mExecuteCommands = true;
	bool mExecuteDrawActions = true;
	bool mSkipDirectDescriptorTargets = false;
	bool mSkipDirectDescriptorTargetsActive = false;
	bool mDescriptorTextureOverrideCachePrebuilt = false;
	bool mGraphicsSrvBindingsResolved = false;
	bool mResolvedGraphicsSrvBindingsResolved = false;
	bool mGraphicsSrvBindingsByTargetResolved = false;
	bool mGraphicsSrvTextureOverrideCacheResolved = false;
	bool mTextureOverrideRuntimeSnapshotResolved = false;
	DX12CommandListExecutorScratch mFallbackScratch;
	bool mScratchPooled = false;
	DX12CommandListExecutorScratch *mScratch = nullptr;
	std::vector<std::wstring> &mCommandListStack;
	std::unordered_set<std::wstring> &mCommandListStackSet;
	std::vector<Bunny::CommandListTarget> &mDescriptorTargetScratch;
	std::vector<DX12TextureOverrideMatch> &mTargetMatchScratch;
	std::vector<DX12CurrentShaderResourceBinding> &mGraphicsSrvBindings;
	std::vector<DX12ResolvedGraphicsSrvBinding> &mResolvedGraphicsSrvBindings;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12ResolvedGraphicsSrvBinding>,
		DX12TextureOverrideTargetCacheKeyHash> &mGraphicsSrvBindingsByTarget;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> &mGraphicsSrvTextureOverrideCache;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12ResolvedGraphicsSrvBinding>,
		DX12TextureOverrideTargetCacheKeyHash> &mShaderResourceOverrideBindingsByTarget;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> &mTargetTextureOverrideCache;
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
		mTextureOverrideRuntimeSnapshot;
	MigotoCommandVmUtils::State mVm;
};

static bool RunNamedCommandList(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	const std::wstring &name,
	DX12ModIaReplacement *replacement,
	std::vector<DX12PendingResourceViewRequest> *pendingResourceViews)
{
	if (!device || !commandList || !replacement)
		return false;
	if (!CommandListRuntimeEffect(name, true))
		return false;

	DX12IaHashState emptyIa = EmptyIaHashState();
	DX12CommandListExecutor executor(
		device, commandList, emptyIa, 0, 0, 0, 0, 0, 0,
		replacement, pendingResourceViews);
	executor.RunCommandListLinks(Bunny::CommandListLinks{ {}, { name }, {} }, true);
	return executor.Changed();
}

static bool RunNamedCommandListVmOnly(const std::wstring &name, bool includePost)
{
	if (name.empty())
		return false;
	if (!CommandListRuntimeEffect(name, includePost))
		return false;

	DX12IaHashState emptyIa = EmptyIaHashState();
	DX12CommandListExecutor executor(
		nullptr, nullptr, emptyIa, 0, 0, 0, 0, 0, 0, nullptr, nullptr);
	executor.RunCommandListLinks(Bunny::CommandListLinks{ {}, { name }, {} }, includePost);
	return true;
}

static void ProcessDx12KeyBindingsForFrame()
{
	std::vector<Bunny::MigotoKeyBindingUtils::RuntimeAction> actions;
	AcquireSRWLockExclusive(&gModLock);
	if (gLoaded)
		Bunny::MigotoKeyBindingUtils::ProcessBindings(&gKeyBindings, &actions);
	ReleaseSRWLockExclusive(&gModLock);

#if defined(_DEBUG)
	if (!actions.empty())
		DX12LogManager::DebugFields("DX12KeyBindings",
			"\"actions\":%zu", actions.size());
#endif

	for (const Bunny::MigotoKeyBindingUtils::RuntimeAction &action : actions)
		RunNamedCommandListVmOnly(action.commandList, action.includePost);
}

static void AppendShaderOverrideForHashLocked(
	uint64_t hash, std::vector<const Bunny::ShaderOverrideConfig*> *configs,
	std::unordered_set<std::wstring> *seen)
{
	if (!hash || !configs || !seen)
		return;
	auto it = gShaderOverrides.find(hash);
	if (it == gShaderOverrides.end())
		return;
	if (!seen->insert(it->second.section).second)
		return;
	configs->push_back(&it->second);
}

static bool ShaderModelMatchesRegex(
	const Bunny::ShaderRegexConfig &regex, const std::string &shaderModel)
{
	if (regex.hasPattern || shaderModel.empty())
		return false;
	for (const std::string &model : regex.shaderModels) {
		if (model == shaderModel)
			return true;
	}
	return false;
}

static constexpr LONG ShaderOverrideHotPathLogLimit = 512;
static volatile LONG gShaderOverridePsoCacheHitLogCount = 0;
static volatile LONG gShaderOverrideNegativeCacheHitLogCount = 0;
static volatile LONG gShaderOverrideNegativeCacheStoreLogCount = 0;
static volatile LONG gShaderOverrideNegativeCacheDescriptorBlockedLogCount = 0;
static volatile LONG gShaderOverrideNegativeCacheDynamicBlockedLogCount = 0;

static bool AllowShaderOverrideHotPathLog(volatile LONG *counter)
{
#if defined(_DEBUG)
	if (!DX12LogManager::DiagnosticsLoggingEnabled())
		return false;
	return InterlockedIncrement(counter) <= ShaderOverrideHotPathLogLimit;
#else
	(void)counter;
	return false;
#endif
}

static Bunny::ShaderOverrideConfig MakeShaderOverrideFromRegex(
	const Bunny::ShaderRegexConfig &regex, uint64_t hash)
{
	Bunny::ShaderOverrideConfig config;
	config.section = regex.section;
	config.originalSection = regex.originalSection;
	config.sourcePath = regex.sourcePath;
	config.sourceDir = regex.sourceDir;
	config.iniNamespace = regex.iniNamespace;
	config.hash = hash;
	config.handlingSkip = regex.handlingSkip;
	config.commandLists = regex.commandLists;
	config.actions = regex.actions;
	return config;
}

static void AppendShaderRegexForShaderLocked(
	uint64_t hash,
	const std::string &shaderModel,
	std::vector<Bunny::ShaderOverrideConfig> *regexConfigs,
	std::unordered_set<std::wstring> *seen)
{
	if (!hash || shaderModel.empty() || !regexConfigs || !seen)
		return;

	for (const auto &item : gShaderRegexes) {
		const Bunny::ShaderRegexConfig &regex = item.second;
		if (!ShaderModelMatchesRegex(regex, shaderModel))
			continue;
		if (!seen->insert(regex.section).second)
			continue;
		regexConfigs->push_back(MakeShaderOverrideFromRegex(regex, hash));
#if defined(_DEBUG)
		if (DX12LogManager::DiagnosticsLoggingEnabled()) {
			DX12LogManager::DebugFields("DX12ShaderRegexMatch",
				"\"section\":\"%S\",\"hash\":\"%016llx\",\"model\":\"%S\"",
				regex.section.c_str(), static_cast<unsigned long long>(hash),
				std::wstring(shaderModel.begin(), shaderModel.end()).c_str());
		}
#endif
	}
}

static void BuildShaderOverrideConfigPointers(
	DX12ShaderOverridePsoMatchCache *cache,
	bool dispatch,
	std::vector<const Bunny::ShaderOverrideConfig*> *configs)
{
	if (!cache || !configs)
		return;
	configs->clear();
	const std::vector<const Bunny::ShaderOverrideConfig*> &direct =
		dispatch ? cache->dispatchConfigs : cache->drawConfigs;
	configs->insert(configs->end(), direct.begin(), direct.end());
	std::vector<Bunny::ShaderOverrideConfig> &regex =
		dispatch ? cache->regexDispatchConfigs : cache->regexDrawConfigs;
	for (Bunny::ShaderOverrideConfig &config : regex)
		configs->push_back(&config);
}

static void CopyShaderOverrideConfigs(
	const std::vector<const Bunny::ShaderOverrideConfig*> &configs,
	std::vector<Bunny::ShaderOverrideConfig> *snapshot)
{
	if (!snapshot)
		return;
	snapshot->clear();
	snapshot->reserve(configs.size());
	for (const Bunny::ShaderOverrideConfig *config : configs) {
		if (config)
			snapshot->push_back(*config);
	}
}

static void MergeTextureOverrideDrawMatchUsage(
	const Bunny::TextureOverrideConfig &config,
	DX12DrawMatchUsage *usage)
{
	if (!usage)
		return;
	usage->useVertexCount = usage->useVertexCount || config.hasMatchVertexCount;
	usage->useIndexCount = usage->useIndexCount || config.hasMatchIndexCount;
	usage->useInstanceCount = usage->useInstanceCount || config.hasMatchInstanceCount;
	usage->useFirstVertex = usage->useFirstVertex || config.hasMatchFirstVertex;
	usage->useFirstIndex = usage->useFirstIndex || config.hasMatchFirstIndex;
	usage->useFirstInstance = usage->useFirstInstance || config.hasMatchFirstInstance;
}

static void AppendUniqueDrawMatchUsageTargetLocked(
	const Bunny::CommandListTarget &target,
	std::vector<Bunny::CommandListTarget> *targets)
{
	if (!targets)
		return;
	for (const Bunny::CommandListTarget &existing : *targets) {
		if (existing.kind == target.kind &&
			existing.stage == target.stage &&
			existing.slot == target.slot) {
			return;
		}
	}
	targets->push_back(target);
}

static bool DrawMatchUsageCommandListRuntimeEffectLocked(const std::wstring &name)
{
	std::unordered_map<std::wstring, bool>::const_iterator found =
		gCommandListPreRuntimeEffect.find(name);
	return found == gCommandListPreRuntimeEffect.end() || found->second;
}

static void CollectCheckTextureOverrideTargetsFromActionsLocked(
	const std::vector<Bunny::CommandListAction> &actions,
	UINT depth,
	std::unordered_set<std::wstring> *commandListStack,
	std::vector<Bunny::CommandListTarget> *targets)
{
	if (depth > 16)
		return;
	for (const Bunny::CommandListAction &action : actions) {
		switch (action.kind) {
		case Bunny::CommandListActionKind::CheckTextureOverride:
			AppendUniqueDrawMatchUsageTargetLocked(action.target, targets);
			break;
		case Bunny::CommandListActionKind::Run: {
			if (action.commandList.empty() ||
				!DrawMatchUsageCommandListRuntimeEffectLocked(action.commandList)) {
				break;
			}
			if (commandListStack &&
				commandListStack->find(action.commandList) !=
					commandListStack->end()) {
				break;
			}
			if (commandListStack)
				commandListStack->insert(action.commandList);
			Bunny::CommandListMap::const_iterator found =
				gCommandLists.find(action.commandList);
			if (found != gCommandLists.end()) {
				CollectCheckTextureOverrideTargetsFromActionsLocked(
					found->second.actions,
					depth + 1,
					commandListStack,
					targets);
			}
			if (commandListStack)
				commandListStack->erase(action.commandList);
			break;
		}
		default:
			break;
		}
	}
}

static void CollectCheckTextureOverrideTargetsFromShaderOverrideLocked(
	const Bunny::ShaderOverrideConfig &config,
	std::vector<Bunny::CommandListTarget> *targets)
{
	std::unordered_set<std::wstring> commandListStack;
	CollectCheckTextureOverrideTargetsFromActionsLocked(
		config.actions,
		0,
		&commandListStack,
		targets);
	for (const std::wstring &name : config.commandLists.pre) {
		if (!name.empty()) {
			commandListStack.insert(name);
			Bunny::CommandListMap::const_iterator found = gCommandLists.find(name);
			if (DrawMatchUsageCommandListRuntimeEffectLocked(name) &&
				found != gCommandLists.end()) {
				CollectCheckTextureOverrideTargetsFromActionsLocked(
					found->second.actions,
					1,
					&commandListStack,
					targets);
			}
			commandListStack.erase(name);
		}
	}
	for (const std::wstring &name : config.commandLists.main) {
		if (!name.empty()) {
			commandListStack.insert(name);
			Bunny::CommandListMap::const_iterator found = gCommandLists.find(name);
			if (DrawMatchUsageCommandListRuntimeEffectLocked(name) &&
				found != gCommandLists.end()) {
				CollectCheckTextureOverrideTargetsFromActionsLocked(
					found->second.actions,
					1,
					&commandListStack,
					targets);
			}
			commandListStack.erase(name);
		}
	}
}

static void MergeAllTextureOverrideDrawMatchUsageLocked(
	DX12DrawMatchUsage *usage)
{
	if (!usage)
		return;
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadCommandExecutionRuntimeSnapshot();
	if (snapshot) {
		std::unordered_map<uint32_t, std::shared_ptr<const Bunny::TextureOverrideConfig>>::const_iterator it =
			snapshot->textureOverridesBySectionId.begin();
		for (; it != snapshot->textureOverridesBySectionId.end(); ++it) {
			if (it->second)
				MergeTextureOverrideDrawMatchUsage(*it->second, usage);
		}
		return;
	}
	Bunny::TextureOverrideMap::const_iterator bucket = gTextureOverrides.begin();
	for (; bucket != gTextureOverrides.end(); ++bucket) {
		const std::vector<Bunny::TextureOverrideConfig> &configs = bucket->second;
		for (const Bunny::TextureOverrideConfig &config : configs)
			MergeTextureOverrideDrawMatchUsage(config, usage);
	}
}

static void MergeDescriptorTextureOverrideDrawMatchUsageLocked(
	DX12DrawMatchUsage *usage)
{
	if (!usage)
		return;
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadCommandExecutionRuntimeSnapshot();
	if (snapshot) {
		std::unordered_set<const Bunny::TextureOverrideConfig*> seenConfigs;
		std::unordered_map<uint32_t, std::vector<DX12TriggeredTextureOverride>>::const_iterator bucket =
			snapshot->textureOverridesByHash.begin();
		for (; bucket != snapshot->textureOverridesByHash.end(); ++bucket) {
			if (!bucket->first)
				continue;
			const std::vector<DX12TriggeredTextureOverride> &triggers = bucket->second;
			for (const DX12TriggeredTextureOverride &triggered : triggers) {
				if (!triggered.config || !seenConfigs.insert(triggered.config).second)
					continue;
				MergeTextureOverrideDrawMatchUsage(*triggered.config, usage);
			}
		}
		return;
	}
	Bunny::TextureOverrideMap::const_iterator bucket = gTextureOverrides.begin();
	for (; bucket != gTextureOverrides.end(); ++bucket) {
		if (!bucket->first)
			continue;
		const std::vector<Bunny::TextureOverrideConfig> &configs = bucket->second;
		for (const Bunny::TextureOverrideConfig &config : configs)
			MergeTextureOverrideDrawMatchUsage(config, usage);
	}
}

static void MergeIaTextureOverrideDrawMatchUsageLocked(
	bool indexBuffer,
	uint32_t vertexSlot,
	DX12DrawMatchUsage *usage)
{
	if (!usage)
		return;
	for (size_t index = 0; index < gIaTextureOverrides.size(); ++index) {
		const Bunny::TextureOverrideConfig *config = gIaTextureOverrides[index].config;
		if (!config)
			continue;
		if (!TextureOverrideMatchesIaBinding(*config, indexBuffer, vertexSlot))
			continue;
		MergeTextureOverrideDrawMatchUsage(*config, usage);
	}
}

static DX12DrawMatchUsage BuildTextureOverrideDrawMatchUsageLocked(
	const std::vector<Bunny::CommandListTarget> &targets)
{
	DX12DrawMatchUsage usage;
	for (const Bunny::CommandListTarget &target : targets) {
		switch (target.kind) {
		case Bunny::CommandListTargetKind::IndexBuffer:
			MergeIaTextureOverrideDrawMatchUsageLocked(true, 0, &usage);
			break;
		case Bunny::CommandListTargetKind::VertexBuffer:
			MergeIaTextureOverrideDrawMatchUsageLocked(false, target.slot, &usage);
			break;
		case Bunny::CommandListTargetKind::ConstantBuffer:
		case Bunny::CommandListTargetKind::ShaderResource:
		case Bunny::CommandListTargetKind::UnorderedAccessView:
			MergeDescriptorTextureOverrideDrawMatchUsageLocked(&usage);
			break;
		default:
			break;
		}
	}
	return usage;
}

static DX12DrawMatchUsage BuildDrawMatchUsageLocked(
	const std::vector<const Bunny::ShaderOverrideConfig*> &configs)
{
	std::vector<Bunny::CommandListTarget> targets;
	for (const Bunny::ShaderOverrideConfig *config : configs) {
		if (config)
			CollectCheckTextureOverrideTargetsFromShaderOverrideLocked(*config, &targets);
	}
	return BuildTextureOverrideDrawMatchUsageLocked(targets);
}

static DX12DrawMatchUsage BuildDrawMatchUsageLocked(
	const std::vector<Bunny::ShaderOverrideConfig> &configs)
{
	std::vector<Bunny::CommandListTarget> targets;
	for (const Bunny::ShaderOverrideConfig &config : configs) {
		CollectCheckTextureOverrideTargetsFromShaderOverrideLocked(config, &targets);
	}
	return BuildTextureOverrideDrawMatchUsageLocked(targets);
}

class DX12ShaderOverrideConfigView
{
public:
	void SetOwnedSnapshot(
		const std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> &snapshot)
	{
		mOwnedSnapshot = snapshot;
		mFallbackSnapshot.clear();
	}

	std::vector<Bunny::ShaderOverrideConfig> *MutableFallbackSnapshot()
	{
		mOwnedSnapshot.reset();
		return &mFallbackSnapshot;
	}

	const std::vector<Bunny::ShaderOverrideConfig> &Get() const
	{
		return mOwnedSnapshot ? *mOwnedSnapshot : mFallbackSnapshot;
	}

private:
	std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> mOwnedSnapshot;
	std::vector<Bunny::ShaderOverrideConfig> mFallbackSnapshot;
};

// PSO-Create-Time pre-compiled override plan: snapshots all shader override configs
// for a PSO so draw-time discovery and per-draw deep copy are eliminated.
// Populated in RecordGraphicsPipelineState; consumed in PrepareShaderOverrideReplacement.
class DX12CompiledPsoOverridePlan
{
public:
	bool valid = false;
	bool alwaysMiss = false;
	UINT64 generation = 0;
	bool drawDecisionMetadataValid = false;
	std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> configSnapshot;
	std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> compiledPlanSnapshot;
	std::shared_ptr<const DX12ModIaReplacement> staticReplacementSnapshot;
	DX12IaHashTargetPlan drawTargetPlan;
	DX12IaHashTargetPlan drawDirectTargetPlan;
	DX12DrawMatchUsage drawMatchUsage;
	DX12DrawMatchUsage drawDirectDrawMatchUsage;
	bool drawDirectTextureChecksOnly = false;
	bool staticReplacementReady = false;
};

static std::unordered_map<ID3D12PipelineState*, DX12CompiledPsoOverridePlan> gPsoOverridePlans;
static SRWLOCK gPsoOverridePlansLock = SRWLOCK_INIT;

class DX12CompiledPsoPlanTlsCache
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	bool valid = false;
	bool alwaysMiss = false;
	std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> configSnapshot;
	std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> compiledPlanSnapshot;

	~DX12CompiledPsoPlanTlsCache()
	{
		if (pipelineState)
			pipelineState->Release();
	}

	void Store(
		ID3D12PipelineState *newPipelineState,
		UINT64 newReloadGeneration,
		bool newAlwaysMiss,
		const std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> &newConfigSnapshot,
		const std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> &newCompiledPlanSnapshot)
	{
		if (pipelineState != newPipelineState) {
			if (pipelineState)
				pipelineState->Release();
			pipelineState = newPipelineState;
			if (pipelineState)
				pipelineState->AddRef();
		}
		reloadGeneration = newReloadGeneration;
		valid = true;
		alwaysMiss = newAlwaysMiss;
		configSnapshot = newConfigSnapshot;
		compiledPlanSnapshot = newCompiledPlanSnapshot;
	}
};

static thread_local DX12CompiledPsoPlanTlsCache gCompiledPsoPlanTlsCache;

static bool TryLoadCompiledPsoPlan(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> *configSnapshot,
	std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> *compiledPlanSnapshot,
	bool *alwaysMiss)
{
	if (!configSnapshot || !compiledPlanSnapshot || !alwaysMiss)
		return false;
	*alwaysMiss = false;
	configSnapshot->reset();
	compiledPlanSnapshot->reset();
	if (!pipelineState)
		return false;
	if (gCompiledPsoPlanTlsCache.valid &&
		gCompiledPsoPlanTlsCache.pipelineState == pipelineState &&
		gCompiledPsoPlanTlsCache.reloadGeneration == reloadGeneration) {
		*alwaysMiss = gCompiledPsoPlanTlsCache.alwaysMiss;
		if (!*alwaysMiss) {
			*configSnapshot = gCompiledPsoPlanTlsCache.configSnapshot;
			*compiledPlanSnapshot = gCompiledPsoPlanTlsCache.compiledPlanSnapshot;
		}
		return true;
	}

	AcquireSRWLockShared(&gPsoOverridePlansLock);
	auto it = gPsoOverridePlans.find(pipelineState);
	if (it == gPsoOverridePlans.end() || it->second.generation != reloadGeneration) {
		ReleaseSRWLockShared(&gPsoOverridePlansLock);
		return false;
	}
	const bool cachedAlwaysMiss = it->second.alwaysMiss;
	const std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> cachedConfigSnapshot =
		it->second.configSnapshot;
	const std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> cachedCompiledPlanSnapshot =
		it->second.compiledPlanSnapshot;
	ReleaseSRWLockShared(&gPsoOverridePlansLock);
	gCompiledPsoPlanTlsCache.Store(
		pipelineState,
		reloadGeneration,
		cachedAlwaysMiss,
		cachedConfigSnapshot,
		cachedCompiledPlanSnapshot);
	*alwaysMiss = cachedAlwaysMiss;
	if (!*alwaysMiss) {
		*configSnapshot = cachedConfigSnapshot;
		*compiledPlanSnapshot = cachedCompiledPlanSnapshot;
	}
	return true;
}

static bool TryLoadCompiledPsoDrawDecisionMetadata(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	DX12IaHashTargetPlan *drawTargetPlan,
	DX12IaHashTargetPlan *drawDirectTargetPlan,
	DX12DrawMatchUsage *drawMatchUsage,
	DX12DrawMatchUsage *drawDirectDrawMatchUsage,
	bool *drawDirectTextureChecksOnly)
{
	if (!pipelineState ||
		!drawTargetPlan ||
		!drawDirectTargetPlan ||
		!drawMatchUsage ||
		!drawDirectDrawMatchUsage ||
		!drawDirectTextureChecksOnly) {
		return false;
	}

	AcquireSRWLockShared(&gPsoOverridePlansLock);
	auto it = gPsoOverridePlans.find(pipelineState);
	if (it == gPsoOverridePlans.end() ||
		it->second.generation != reloadGeneration ||
		!it->second.drawDecisionMetadataValid) {
		ReleaseSRWLockShared(&gPsoOverridePlansLock);
		return false;
	}
	*drawTargetPlan = it->second.drawTargetPlan;
	*drawDirectTargetPlan = it->second.drawDirectTargetPlan;
	*drawMatchUsage = it->second.drawMatchUsage;
	*drawDirectDrawMatchUsage = it->second.drawDirectDrawMatchUsage;
	*drawDirectTextureChecksOnly = it->second.drawDirectTextureChecksOnly;
	ReleaseSRWLockShared(&gPsoOverridePlansLock);
	return true;
}

bool DX12ModTryLoadStaticReplacementForPso(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	DX12ModIaReplacement *replacement)
{
	if (!replacement || !pipelineState)
		return false;
	AcquireSRWLockShared(&gPsoOverridePlansLock);
	auto it = gPsoOverridePlans.find(pipelineState);
	if (it == gPsoOverridePlans.end() ||
	    it->second.generation != reloadGeneration ||
	    !it->second.staticReplacementReady ||
	    !it->second.staticReplacementSnapshot) {
		ReleaseSRWLockShared(&gPsoOverridePlansLock);
		return false;
	}
	*replacement = *it->second.staticReplacementSnapshot;
	ReleaseSRWLockShared(&gPsoOverridePlansLock);
	return true;
}

std::shared_ptr<const DX12ModIaReplacement>
DX12ModFindStaticReplacementSnapshotForPso(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration)
{
	if (!pipelineState)
		return nullptr;
	AcquireSRWLockShared(&gPsoOverridePlansLock);
	auto it = gPsoOverridePlans.find(pipelineState);
	if (it == gPsoOverridePlans.end() ||
	    it->second.generation != reloadGeneration ||
	    !it->second.staticReplacementReady ||
	    !it->second.staticReplacementSnapshot) {
		ReleaseSRWLockShared(&gPsoOverridePlansLock);
		return nullptr;
	}
	std::shared_ptr<const DX12ModIaReplacement> snapshot =
		it->second.staticReplacementSnapshot;
	ReleaseSRWLockShared(&gPsoOverridePlansLock);
	return snapshot;
}

static std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>>
BuildCompiledShaderOverridePlanSnapshot(
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot)
{
	if (configSnapshot.empty())
		return nullptr;
	std::shared_ptr<std::vector<DX12CompiledShaderOverridePlan>> compiledPlans =
		std::make_shared<std::vector<DX12CompiledShaderOverridePlan>>();
	compiledPlans->reserve(configSnapshot.size());
	for (const Bunny::ShaderOverrideConfig &config : configSnapshot) {
		DX12CompiledShaderOverridePlan plan;
		BuildCompiledShaderOverridePlanFromParts(
			config.handlingSkip,
			config.commandLists,
			config.actions,
			gCompiledCommandListPlans,
			&plan);
		if (plan.unsupported)
			return nullptr;
		compiledPlans->push_back(std::move(plan));
	}
	return compiledPlans;
}

static std::shared_ptr<const DX12ModIaReplacement>
BuildStaticReplacementSnapshotFromCompiledPlans(
	const std::vector<DX12CompiledShaderOverridePlan> &plans);

static void StoreCompiledPsoPlan(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot,
	const DX12IaHashTargetPlan *drawTargetPlan = nullptr,
	const DX12IaHashTargetPlan *drawDirectTargetPlan = nullptr,
	const DX12DrawMatchUsage *drawMatchUsage = nullptr,
	const DX12DrawMatchUsage *drawDirectDrawMatchUsage = nullptr,
	const bool *drawDirectTextureChecksOnly = nullptr)
{
	// Never store empty plans: PSOs created before mod loads would cache "alwaysMiss"
	// and never recover when the mod later loads matching configs.
	if (!pipelineState || configSnapshot.empty())
		return;

	AcquireSRWLockExclusive(&gPsoOverridePlansLock);
	DX12CompiledPsoOverridePlan plan;
	plan.valid = true;
	plan.alwaysMiss = false;
	plan.generation = reloadGeneration;
	plan.configSnapshot = std::make_shared<const std::vector<Bunny::ShaderOverrideConfig>>(configSnapshot);
	if (drawTargetPlan && drawDirectTargetPlan && drawMatchUsage &&
		drawDirectDrawMatchUsage && drawDirectTextureChecksOnly) {
		plan.drawTargetPlan = *drawTargetPlan;
		plan.drawDirectTargetPlan = *drawDirectTargetPlan;
		plan.drawMatchUsage = *drawMatchUsage;
		plan.drawDirectDrawMatchUsage = *drawDirectDrawMatchUsage;
		plan.drawDirectTextureChecksOnly = *drawDirectTextureChecksOnly;
		plan.drawDecisionMetadataValid = true;
	}
	plan.compiledPlanSnapshot =
		BuildCompiledShaderOverridePlanSnapshot(configSnapshot);
	if (plan.compiledPlanSnapshot && !plan.compiledPlanSnapshot->empty()) {
		plan.staticReplacementSnapshot =
			BuildStaticReplacementSnapshotFromCompiledPlans(
				*plan.compiledPlanSnapshot);
		plan.staticReplacementReady = plan.staticReplacementSnapshot != nullptr;
	}
	gPsoOverridePlans[pipelineState] = plan;
	ReleaseSRWLockExclusive(&gPsoOverridePlansLock);
}

static void StoreStaticReplacementForPso(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	const DX12ModIaReplacement &replacement)
{
	if (!pipelineState)
		return;
	AcquireSRWLockExclusive(&gPsoOverridePlansLock);
	auto it = gPsoOverridePlans.find(pipelineState);
	if (it != gPsoOverridePlans.end() &&
	    it->second.generation == reloadGeneration) {
		it->second.staticReplacementSnapshot =
			std::make_shared<const DX12ModIaReplacement>(replacement);
		it->second.staticReplacementReady = true;
	}
	ReleaseSRWLockExclusive(&gPsoOverridePlansLock);
}

static void ClearCompiledPsoPlans()
{
	AcquireSRWLockExclusive(&gPsoOverridePlansLock);
	gPsoOverridePlans.clear();
	ReleaseSRWLockExclusive(&gPsoOverridePlansLock);
}

static bool ReplacementHasAnyEffect(const DX12ModIaReplacement &replacement)
{
	return replacement.skip ||
		!replacement.draws.empty() ||
		!replacement.dispatches.empty() ||
		!replacement.drawSrvPatches.empty() ||
		!replacement.postCommandLists.empty() ||
		!replacement.queuedPostTextureOverrides.empty();
}

static bool ReplacementIsStaticTemplateCandidate(
	const DX12CommandListExecutor &executor,
	const std::vector<DX12PendingResourceViewRequest> &pendingResourceViews,
	const DX12ModIaReplacement &replacement)
{
	if (executor.observedDescriptorTarget ||
	    executor.observedDynamicCommandState ||
	    !pendingResourceViews.empty() ||
	    !replacement.drawSrvPatches.empty() ||
	    !replacement.postCommandLists.empty() ||
	    !replacement.queuedPostTextureOverrides.empty()) {
		return false;
	}
	for (const DX12ModIaReplacement::DrawCall &draw : replacement.draws) {
		if (draw.fromCaller)
			return false;
	}
	return true;
}

static bool CompiledCommandListPlanIsStaticTemplateCandidate(
	const DX12CompiledCommandListPlan &plan)
{
	if (plan.unsupported || plan.NeedsResidentIaState())
		return false;
	if (!plan.ops.empty()) {
		for (const DX12CompiledCommandListPlan::Op &op : plan.ops) {
			switch (op.kind) {
			case DX12CompiledCommandListPlan::Op::Kind::HandlingSkip:
			case DX12CompiledCommandListPlan::Op::Kind::Draw:
			case DX12CompiledCommandListPlan::Op::Kind::Dispatch:
				if (op.kind == DX12CompiledCommandListPlan::Op::Kind::Draw &&
				    op.draw.fromCaller) {
					return false;
				}
				break;
			default:
				return false;
			}
		}
	}
	for (const DX12ModIaReplacement::DrawCall &draw : plan.draws) {
		if (draw.fromCaller)
			return false;
	}
	return true;
}

static bool CompiledShaderOverridePlanIsStaticTemplateCandidate(
	const DX12CompiledShaderOverridePlan &plan)
{
	if (plan.unsupported)
		return false;
	if (plan.hasPostCommandListPlan || !plan.postCommandLists.empty())
		return false;
	return CompiledCommandListPlanIsStaticTemplateCandidate(plan.mainPlan);
}

static std::shared_ptr<const DX12ModIaReplacement>
BuildStaticReplacementSnapshotFromCompiledPlans(
	const std::vector<DX12CompiledShaderOverridePlan> &plans)
{
	for (const DX12CompiledShaderOverridePlan &plan : plans) {
		if (!CompiledShaderOverridePlanIsStaticTemplateCandidate(plan))
			return nullptr;
	}

	DX12ModIaReplacement replacement;
	DX12IaHashState emptyIa = EmptyIaHashState();
	DX12CommandListExecutor executor(
		nullptr, nullptr, emptyIa, 0, 0, 0, 0, 0, 0, &replacement, nullptr);
	executor.SetExecutionMode(true, true);
	for (const DX12CompiledShaderOverridePlan &plan : plans)
		executor.RunCompiledShaderOverridePlan(plan);
	if (!ReplacementHasAnyEffect(replacement))
		return nullptr;
	return std::make_shared<const DX12ModIaReplacement>(replacement);
}

class DX12ShaderOverrideIaTargetPlanUtils
{
public:
	static bool CommandListRuntimeEffectLocked(const std::wstring &name)
	{
		std::unordered_map<std::wstring, bool>::const_iterator found =
			gCommandListPreRuntimeEffect.find(name);
		return found == gCommandListPreRuntimeEffect.end() || found->second;
	}

	static void RequireTargetLocked(
		const Bunny::CommandListTarget &target,
		DX12IaHashTargetPlan *targetPlan)
	{
		if (!targetPlan)
			return;

		switch (target.kind) {
		case Bunny::CommandListTargetKind::IndexBuffer:
			targetPlan->RequireIndexBuffer();
			break;
		case Bunny::CommandListTargetKind::VertexBuffer:
			targetPlan->RequireVertexBuffer(target.slot);
			break;
		case Bunny::CommandListTargetKind::ConstantBuffer:
		case Bunny::CommandListTargetKind::ShaderResource:
		case Bunny::CommandListTargetKind::UnorderedAccessView:
			targetPlan->RequireDescriptorTarget(
				static_cast<uint32_t>(target.kind),
				static_cast<uint32_t>(target.stage),
				target.slot);
			break;
		default:
			targetPlan->RequireAll();
			break;
		}
	}

	static void CollectCommandListLocked(
		const std::wstring &name,
		UINT depth,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets,
		bool *hasDirectNonTextureEffect);

	static void CollectActionsLocked(
		const std::vector<Bunny::CommandListAction> &actions,
		UINT depth,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets,
		bool *hasDirectNonTextureEffect)
	{
		if (!targetPlan || depth > 16)
			return;

		for (const Bunny::CommandListAction &action : actions) {
			switch (action.kind) {
			case Bunny::CommandListActionKind::Run:
				CollectCommandListLocked(
					action.commandList, depth + 1, commandListStack,
					targetPlan, textureOverrideTargetsCollected,
					collectTextureOverrideTargets,
					hasDirectNonTextureEffect);
				break;
			case Bunny::CommandListActionKind::CheckTextureOverride:
				RequireTargetLocked(action.target, targetPlan);
				if (collectTextureOverrideTargets &&
				    textureOverrideTargetsCollected &&
				    !*textureOverrideTargetsCollected) {
					*textureOverrideTargetsCollected = true;
					CollectAllTextureOverrideTargetsLocked(
						commandListStack, targetPlan,
						textureOverrideTargetsCollected);
				}
				break;
			default:
				if (hasDirectNonTextureEffect)
					*hasDirectNonTextureEffect = true;
				break;
			}
		}
	}

	static void CollectCommandListLinksLocked(
		const Bunny::CommandListLinks &links,
		bool includePost,
		UINT depth,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets,
		bool *hasDirectNonTextureEffect)
	{
		if (!targetPlan || depth > 16)
			return;

		for (const std::wstring &name : links.pre) {
			CollectCommandListLocked(
				name, depth + 1, commandListStack, targetPlan,
				textureOverrideTargetsCollected, collectTextureOverrideTargets,
				hasDirectNonTextureEffect);
		}
		for (const std::wstring &name : links.main) {
			CollectCommandListLocked(
				name, depth + 1, commandListStack, targetPlan,
				textureOverrideTargetsCollected, collectTextureOverrideTargets,
				hasDirectNonTextureEffect);
		}
		if (!includePost)
			return;
		for (const std::wstring &name : links.post) {
			CollectCommandListLocked(
				name, depth + 1, commandListStack, targetPlan,
				textureOverrideTargetsCollected, collectTextureOverrideTargets,
				hasDirectNonTextureEffect);
		}
	}

	static void CollectShaderOverrideConfigLocked(
		const Bunny::ShaderOverrideConfig &config,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets,
		bool includePostLinks,
		bool *hasDirectNonTextureEffect)
	{
		CollectCommandListLinksLocked(
			config.commandLists, includePostLinks, 0, commandListStack, targetPlan,
			textureOverrideTargetsCollected, collectTextureOverrideTargets,
			hasDirectNonTextureEffect);
		CollectActionsLocked(
			config.actions, 0, commandListStack, targetPlan,
			textureOverrideTargetsCollected, collectTextureOverrideTargets,
			hasDirectNonTextureEffect);
	}

	static void CollectAllTextureOverrideTargetsLocked(
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected)
	{
		if (!targetPlan)
			return;
		if (textureOverrideTargetsCollected)
			*textureOverrideTargetsCollected = true;

		Bunny::TextureOverrideMap::const_iterator bucket = gTextureOverrides.begin();
		for (; bucket != gTextureOverrides.end(); ++bucket) {
			const Bunny::TextureOverrideList &configs = bucket->second;
			for (size_t index = 0; index < configs.size(); ++index) {
				const Bunny::TextureOverrideConfig &config = configs[index];
				CollectCommandListLinksLocked(
					config.commandLists, true, 0, commandListStack,
					targetPlan, textureOverrideTargetsCollected, false, nullptr);
				CollectActionsLocked(
					config.actions, 0, commandListStack, targetPlan,
					textureOverrideTargetsCollected, false, nullptr);
			}
		}
	}

	static DX12IaHashTargetPlan BuildDrawPlanLocked(
		const DX12ShaderOverridePsoMatchCache &cache,
		bool collectTextureOverrideTargets,
		bool *hasDirectNonTextureEffect = nullptr)
	{
		DX12IaHashTargetPlan targetPlan;
		targetPlan.ResetKnown();
		bool textureOverrideTargetsCollected = false;
		std::unordered_set<std::wstring> commandListStack;

		for (const Bunny::ShaderOverrideConfig *config : cache.drawConfigs) {
			if (!config)
				continue;
			CollectShaderOverrideConfigLocked(
				*config, &commandListStack, &targetPlan,
				collectTextureOverrideTargets ? &textureOverrideTargetsCollected : nullptr,
				collectTextureOverrideTargets,
				false,
				hasDirectNonTextureEffect);
		}
		for (const Bunny::ShaderOverrideConfig &config : cache.regexDrawConfigs) {
			CollectShaderOverrideConfigLocked(
				config, &commandListStack, &targetPlan,
				collectTextureOverrideTargets ? &textureOverrideTargetsCollected : nullptr,
				collectTextureOverrideTargets,
				false,
				hasDirectNonTextureEffect);
		}
		return targetPlan;
	}

	static DX12IaHashTargetPlan BuildDrawPlanLocked(
		const std::vector<Bunny::ShaderOverrideConfig> &configs,
		bool collectTextureOverrideTargets,
		bool *hasDirectNonTextureEffect = nullptr)
	{
		DX12IaHashTargetPlan targetPlan;
		targetPlan.ResetKnown();
		bool textureOverrideTargetsCollected = false;
		std::unordered_set<std::wstring> commandListStack;

		for (const Bunny::ShaderOverrideConfig &config : configs) {
			CollectShaderOverrideConfigLocked(
				config, &commandListStack, &targetPlan,
				collectTextureOverrideTargets ? &textureOverrideTargetsCollected : nullptr,
				collectTextureOverrideTargets,
				false,
				hasDirectNonTextureEffect);
		}
		return targetPlan;
	}
};

inline void DX12ShaderOverrideIaTargetPlanUtils::CollectCommandListLocked(
	const std::wstring &name,
	UINT depth,
	std::unordered_set<std::wstring> *commandListStack,
	DX12IaHashTargetPlan *targetPlan,
	bool *textureOverrideTargetsCollected,
	bool collectTextureOverrideTargets,
	bool *hasDirectNonTextureEffect)
{
	if (!targetPlan || name.empty() || depth > 16)
		return;
	if (!CommandListRuntimeEffectLocked(name))
		return;
	if (commandListStack) {
		if (commandListStack->find(name) != commandListStack->end())
			return;
		commandListStack->insert(name);
	}

	Bunny::CommandListMap::const_iterator found = gCommandLists.find(name);
	if (found != gCommandLists.end()) {
		CollectActionsLocked(
			found->second.actions, depth + 1, commandListStack, targetPlan,
			textureOverrideTargetsCollected, collectTextureOverrideTargets,
			hasDirectNonTextureEffect);
	}

	if (commandListStack)
		commandListStack->erase(name);
}

static void LogShaderOverrideCommandListSnapshotLimited(
	const char *phase, const std::vector<Bunny::ShaderOverrideConfig> &configs,
	const DX12ModIaReplacement &replacement)
{
#if defined(_DEBUG)
	std::vector<const Bunny::ShaderOverrideConfig*> pointers;
	pointers.reserve(configs.size());
	for (const Bunny::ShaderOverrideConfig &config : configs)
		pointers.push_back(&config);
	LogShaderOverrideCommandListLimited(phase, pointers, replacement);
#else
	(void)phase;
	(void)configs;
	(void)replacement;
#endif
}

static bool ShaderOverrideNegativeCacheEnabled(
	bool allowDescriptorIndependentNegativeCache)
{
	return allowDescriptorIndependentNegativeCache &&
		DX12ModHasActiveShaderOverrides();
}

static DX12ShaderOverrideNegativeCacheKey MakeShaderOverrideNegativeCacheKey(
	ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	const DX12DrawMatchUsage &usage)
{
	DX12ShaderOverrideNegativeCacheKey key = {};
	key.reloadGeneration = static_cast<UINT64>(InterlockedCompareExchange64(
		reinterpret_cast<volatile LONG64*>(&gShaderOverrideNegativeCacheGeneration), 0, 0));
	key.preSkinGeneration = CurrentPreSkinActiveGeneration();
	key.pipelineState = pipelineState;
	key.vertexCount = usage.useVertexCount ? vertexCount : 0;
	key.indexCount = usage.useIndexCount ? indexCount : 0;
	key.instanceCount = usage.useInstanceCount ? instanceCount : 0;
	key.firstVertex = usage.useFirstVertex ? firstVertex : 0;
	key.firstIndex = usage.useFirstIndex ? firstIndex : 0;
	key.firstInstance = usage.useFirstInstance ? firstInstance : 0;
	key.hasIndexBuffer = iaState.hasIndexBuffer;
	key.indexHash = iaState.hasIndexBuffer ? iaState.indexHash : 0;
	if (iaState.hasIndexBuffer) {
		key.indexBufferSize = iaState.indexView.SizeInBytes;
		key.indexBufferFormat = static_cast<uint32_t>(iaState.indexView.Format);
	}
	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		if (buffer.slot < key.vertexHashes.size()) {
			key.vertexHashes[buffer.slot] = buffer.hash;
			key.vertexPresent[buffer.slot] = 1;
			key.vertexBufferSizes[buffer.slot] = buffer.vertexView.SizeInBytes;
			key.vertexBufferStrides[buffer.slot] = buffer.vertexView.StrideInBytes;
		}
	}
	return key;
}

static UINT64 HashShaderOverrideNegativeCacheKey(
	const DX12ShaderOverrideNegativeCacheKey &key)
{
	UINT64 hash = 0x28cb7a8d31e7b4c5ull;
	hash = HashCombine64(hash, key.reloadGeneration);
	hash = HashCombine64(hash, key.preSkinGeneration);
	hash = HashCombine64(hash, reinterpret_cast<uintptr_t>(key.pipelineState));
	hash = HashCombine64(hash, key.vertexCount);
	hash = HashCombine64(hash, key.indexCount);
	hash = HashCombine64(hash, key.instanceCount);
	hash = HashCombine64(hash, key.firstVertex);
	hash = HashCombine64(hash, key.firstIndex);
	hash = HashCombine64(hash, key.firstInstance);
	hash = HashCombine64(hash, key.hasIndexBuffer ? 1 : 0);
	hash = HashCombine64(hash, key.indexHash);
	hash = HashCombine64(hash, key.indexBufferSize);
	hash = HashCombine64(hash, key.indexBufferFormat);
	for (size_t index = 0; index < key.vertexHashes.size(); ++index) {
		hash = HashCombine64(hash, key.vertexHashes[index]);
		hash = HashCombine64(hash, key.vertexPresent[index]);
		hash = HashCombine64(hash, key.vertexBufferSizes[index]);
		hash = HashCombine64(hash, key.vertexBufferStrides[index]);
	}
	return hash ? hash : 1;
}

static bool TryFindShaderOverrideNegativeCache(
	const DX12ShaderOverrideNegativeCacheKey &key)
{
	static thread_local UINT64 tlsGeneration = 0;
	static thread_local std::unordered_map<
		UINT64,
		DX12ShaderOverrideNegativeCacheKey> tlsCache;
	if (tlsGeneration != key.reloadGeneration) {
		tlsCache.clear();
		tlsGeneration = key.reloadGeneration;
	}
	const UINT64 hash = HashShaderOverrideNegativeCacheKey(key);
	auto it = tlsCache.find(hash);
	return it != tlsCache.end() && it->second == key;
}

static void StoreShaderOverrideNegativeCache(
	const DX12ShaderOverrideNegativeCacheKey &key)
{
	static thread_local UINT64 tlsGeneration = 0;
	static thread_local std::unordered_map<
		UINT64,
		DX12ShaderOverrideNegativeCacheKey> tlsCache;
	if (tlsGeneration != key.reloadGeneration) {
		tlsCache.clear();
		tlsGeneration = key.reloadGeneration;
	}
	const UINT64 hash = HashShaderOverrideNegativeCacheKey(key);
	auto existing = tlsCache.find(hash);
	if (existing == tlsCache.end() &&
	    tlsCache.size() >= MaxShaderOverrideNegativeCacheEntries) {
		tlsCache.clear();
	}
	tlsCache[hash] = key;
}

static void ClearShaderOverrideNegativeCache()
{
	InterlockedIncrement64(
		reinterpret_cast<volatile LONG64*>(&gShaderOverrideNegativeCacheGeneration));
}

static bool TryFindCachedShaderOverridesForPsoLocked(
	ID3D12PipelineState *pipelineState, bool dispatch,
	std::vector<const Bunny::ShaderOverrideConfig*> *configs)
{
	if (!configs)
		return true;
	configs->clear();
	if (!pipelineState)
		return true;

	auto cached = gShaderOverridePsoMatchCache.find(pipelineState);
	if (cached != gShaderOverridePsoMatchCache.end() &&
	    cached->second.generation == gReloadGeneration) {
		BuildShaderOverrideConfigPointers(&cached->second, dispatch, configs);
#if defined(_DEBUG)
		if (AllowShaderOverrideHotPathLog(&gShaderOverridePsoCacheHitLogCount)) {
			DX12LogManager::DebugFields("DX12ShaderOverridePsoMatchCache",
				"\"status\":\"hit\",\"pso\":\"%p\",\"dispatch\":%s,\"matches\":%zu",
				pipelineState, dispatch ? "true" : "false", configs->size());
		}
#endif
		return true;
	}
	return false;
}

// Public draw-path PSO plan lookup used before IA hash work.
// This avoids building IA hashes for PSOs with no matching shader overrides.
static bool TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
	ID3D12PipelineState *pipelineState,
	DX12IaHashTargetPlan *targetPlan)
{
	if (!targetPlan || !pipelineState)
		return false;
	std::unordered_map<ID3D12PipelineState*, DX12ShaderOverridePsoMatchCache>::iterator cached =
		gShaderOverridePsoMatchCache.find(pipelineState);
	if (cached == gShaderOverridePsoMatchCache.end() ||
	    cached->second.generation != gReloadGeneration)
		return false;
	*targetPlan = cached->second.drawTargetPlan;
	return true;
}

static bool TryFindCachedShaderOverrideDirectIaTargetPlanForPsoLocked(
	ID3D12PipelineState *pipelineState,
	DX12IaHashTargetPlan *targetPlan)
{
	if (!targetPlan || !pipelineState)
		return false;
	std::unordered_map<ID3D12PipelineState*, DX12ShaderOverridePsoMatchCache>::iterator cached =
		gShaderOverridePsoMatchCache.find(pipelineState);
	if (cached == gShaderOverridePsoMatchCache.end() ||
	    cached->second.generation != gReloadGeneration)
		return false;
	*targetPlan = cached->second.drawDirectTargetPlan;
	return true;
}

static bool TryFindCachedShaderOverrideDirectTextureChecksOnlyForPsoLocked(
	ID3D12PipelineState *pipelineState,
	bool *directTextureChecksOnly)
{
	if (!directTextureChecksOnly || !pipelineState)
		return false;
	std::unordered_map<ID3D12PipelineState*, DX12ShaderOverridePsoMatchCache>::iterator cached =
		gShaderOverridePsoMatchCache.find(pipelineState);
	if (cached == gShaderOverridePsoMatchCache.end() ||
	    cached->second.generation != gReloadGeneration)
		return false;
	*directTextureChecksOnly = cached->second.drawDirectTextureChecksOnly;
	return true;
}

static bool TryFindCachedShaderOverrideDrawMatchUsageForPsoLocked(
	ID3D12PipelineState *pipelineState,
	DX12DrawMatchUsage *drawMatchUsage)
{
	if (!drawMatchUsage || !pipelineState)
		return false;
	std::unordered_map<ID3D12PipelineState*, DX12ShaderOverridePsoMatchCache>::iterator cached =
		gShaderOverridePsoMatchCache.find(pipelineState);
	if (cached == gShaderOverridePsoMatchCache.end() ||
		cached->second.generation != gReloadGeneration)
		return false;
	*drawMatchUsage = cached->second.drawMatchUsage;
	return true;
}

bool DX12ModHasShaderOverridesForPso(ID3D12PipelineState *pipelineState)
{
	if (!pipelineState)
		return false;
	std::vector<const Bunny::ShaderOverrideConfig*> configs;
	bool cached = false;
	AcquireSRWLockShared(&gModLock);
	cached = TryFindCachedShaderOverridesForPsoLocked(pipelineState, false, &configs);
	ReleaseSRWLockShared(&gModLock);
	return !cached || !configs.empty();
}

static void FindShaderOverridesForPsoLocked(
	ID3D12PipelineState *pipelineState, bool dispatch,
	std::vector<const Bunny::ShaderOverrideConfig*> *configs)
{
	if (TryFindCachedShaderOverridesForPsoLocked(pipelineState, dispatch, configs))
		return;

	DX12PsoShaderInfo info = {};
	if (!DX12GetPipelineStateShaderInfo(pipelineState, &info))
		return;

	std::unordered_set<std::wstring> seen;
	std::unordered_set<std::wstring> regexSeen;
	DX12ShaderOverridePsoMatchCache matchCache;
	matchCache.generation = gReloadGeneration;
	if (dispatch) {
		if (info.hasCS) {
			AppendShaderOverrideForHashLocked(info.cs, &matchCache.dispatchConfigs, &seen);
			AppendShaderRegexForShaderLocked(
				info.cs, info.csModel,
				&matchCache.regexDispatchConfigs, &regexSeen);
		}
		DX12ShaderOverridePsoMatchCache &stored =
			gShaderOverridePsoMatchCache[pipelineState];
		stored = std::move(matchCache);
		BuildShaderOverrideConfigPointers(&stored, dispatch, configs);
		return;
	}

	if (info.hasVS) {
		AppendShaderOverrideForHashLocked(info.vs, &matchCache.drawConfigs, &seen);
		AppendShaderRegexForShaderLocked(
			info.vs, info.vsModel,
			&matchCache.regexDrawConfigs, &regexSeen);
	}
	if (info.hasPS) {
		AppendShaderOverrideForHashLocked(info.ps, &matchCache.drawConfigs, &seen);
		AppendShaderRegexForShaderLocked(
			info.ps, info.psModel,
			&matchCache.regexDrawConfigs, &regexSeen);
	}
	bool drawDirectHasNonTextureEffect = false;
	matchCache.drawDirectTargetPlan =
		DX12ShaderOverrideIaTargetPlanUtils::BuildDrawPlanLocked(
			matchCache,
			false,
			&drawDirectHasNonTextureEffect);
	std::vector<const Bunny::ShaderOverrideConfig*> drawUsageConfigs =
		matchCache.drawConfigs;
	for (Bunny::ShaderOverrideConfig &config : matchCache.regexDrawConfigs)
		drawUsageConfigs.push_back(&config);
	matchCache.drawMatchUsage = BuildDrawMatchUsageLocked(drawUsageConfigs);
	matchCache.drawDirectTextureChecksOnly = !drawDirectHasNonTextureEffect;
	// Keep the draw-decision miss path focused on the first-step CTO targets.
	// Nested TextureOverride CTOs are now executed by the compiled command-list op
	// stream on actual matches, so globally unioning every reachable TO target
	// here only inflates descriptor/IA requirements for negative draws.
	matchCache.drawTargetPlan = matchCache.drawDirectTargetPlan;
	gShaderOverridePsoMatchCache[pipelineState] = std::move(matchCache);
	BuildShaderOverrideConfigPointers(
		&gShaderOverridePsoMatchCache[pipelineState], dispatch, configs);
}

bool DX12ModCollectShaderOverrideIaHashTargets(
	ID3D12PipelineState *pipelineState,
	DX12IaHashTargetPlan *targetPlan)
{
	if (!targetPlan)
		return false;
	targetPlan->RequireAll();
	if (!pipelineState || !DX12ModHasActiveShaderOverrides())
		return false;

	AcquireSRWLockShared(&gModLock);
	const bool cachedPlan =
		TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
			pipelineState, targetPlan);
	ReleaseSRWLockShared(&gModLock);
	if (cachedPlan)
		return true;

	std::vector<const Bunny::ShaderOverrideConfig*> configs;
	AcquireSRWLockExclusive(&gModLock);
	FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
	const bool builtPlan =
		TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
			pipelineState, targetPlan);
	ReleaseSRWLockExclusive(&gModLock);
	return builtPlan;
}

bool DX12ModCompileDrawDecision(
	ID3D12PipelineState *pipelineState,
	DX12CompiledDrawDecision *decision)
{
	if (!decision)
		return false;

	const bool activeShaderOverrides = DX12ModHasActiveShaderOverrides();
	const UINT64 reloadGeneration = DX12ModGetReloadGeneration();
	if (!pipelineState || !activeShaderOverrides) {
		decision->SetNoopNeverMatches(
			pipelineState, reloadGeneration, activeShaderOverrides);
		return true;
	}

	std::vector<const Bunny::ShaderOverrideConfig*> configs;
	DX12IaHashTargetPlan targetPlan;
	DX12IaHashTargetPlan directTargetPlan;
	targetPlan.RequireAll();
	directTargetPlan.RequireAll();
	bool cached = false;
	bool compiledMetadataHit = false;
	bool targetPlanResolved = false;
	bool directTargetPlanResolved = false;
	bool directTextureChecksOnly = false;
	DX12DrawMatchUsage drawMatchUsage;
	DX12DrawMatchUsage directDrawMatchUsage;

	compiledMetadataHit = TryLoadCompiledPsoDrawDecisionMetadata(
		pipelineState,
		reloadGeneration,
		&targetPlan,
		&directTargetPlan,
		&drawMatchUsage,
		&directDrawMatchUsage,
		&directTextureChecksOnly);
	if (compiledMetadataHit) {
		targetPlanResolved = true;
		directTargetPlanResolved = true;
		cached = true;
	}

	if (!compiledMetadataHit) {
		AcquireSRWLockShared(&gModLock);
		cached = TryFindCachedShaderOverridesForPsoLocked(
			pipelineState, false, &configs);
		if (cached) {
			targetPlanResolved =
				TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
					pipelineState, &targetPlan);
			directTargetPlanResolved =
				TryFindCachedShaderOverrideDirectIaTargetPlanForPsoLocked(
					pipelineState, &directTargetPlan);
			TryFindCachedShaderOverrideDirectTextureChecksOnlyForPsoLocked(
				pipelineState, &directTextureChecksOnly);
			TryFindCachedShaderOverrideDrawMatchUsageForPsoLocked(
				pipelineState, &drawMatchUsage);
		}
		ReleaseSRWLockShared(&gModLock);
	}

	if (!cached) {
		AcquireSRWLockExclusive(&gModLock);
		FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
		cached = TryFindCachedShaderOverridesForPsoLocked(
			pipelineState, false, &configs);
		if (cached) {
			targetPlanResolved =
				TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
					pipelineState, &targetPlan);
			directTargetPlanResolved =
				TryFindCachedShaderOverrideDirectIaTargetPlanForPsoLocked(
					pipelineState, &directTargetPlan);
			TryFindCachedShaderOverrideDirectTextureChecksOnlyForPsoLocked(
				pipelineState, &directTextureChecksOnly);
			TryFindCachedShaderOverrideDrawMatchUsageForPsoLocked(
				pipelineState, &drawMatchUsage);
		}
		ReleaseSRWLockExclusive(&gModLock);
	}

	const bool shaderOverrides = compiledMetadataHit ? true : (cached ? !configs.empty() : true);
	if (!compiledMetadataHit) {
		if (!cached)
			drawMatchUsage = BuildDrawMatchUsageLocked(configs);
		directDrawMatchUsage = drawMatchUsage;
	}
	if (!targetPlanResolved)
		targetPlan.RequireAll();
	if (!directTargetPlanResolved)
		directTargetPlan = targetPlan;

	const bool shaderOverrideSkip =
		DX12ModShouldSkipPipelineState(pipelineState, false);
	const bool descriptorTrackingNeeded =
		DX12ModNeedsShaderDescriptorTracking();
	decision->SetFromTargetPlan(
		pipelineState, reloadGeneration, activeShaderOverrides,
		shaderOverrides, shaderOverrideSkip, targetPlan, directTargetPlan,
		drawMatchUsage, directDrawMatchUsage,
		directTextureChecksOnly,
		descriptorTrackingNeeded);
	return true;
}

bool DX12ModHasAnyDirectIaTextureOverrideCandidate(
	const DX12IaHashState &iaState,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance)
{
	if (!targetPlan.hasTextureOverrideChecks ||
	    targetPlan.hasDescriptorTargets ||
	    targetPlan.requireAllIaBuffers ||
	    !targetPlan.HasIaTargets()) {
		return true;
	}

	Bunny::CommandListTarget target;
	target.stage = Bunny::CommandListShaderStage::Unknown;
	if (targetPlan.RequiresIndexBuffer(indexed) &&
	    iaState.hasIndexBuffer) {
		target.kind = Bunny::CommandListTargetKind::IndexBuffer;
		target.slot = 0;
		if (FindIaTextureOverridesCached(
			    target,
			    0,
			    vertexCount,
			    indexCount,
			    instanceCount,
			    firstVertex,
				firstIndex,
				firstInstance,
				true,
				0,
				nullptr) ||
			FindIaTextureOverridesCached(
			    target,
			    iaState.indexHash,
			    vertexCount,
			    indexCount,
			    instanceCount,
			    firstVertex,
				    firstIndex,
				    firstInstance,
				    true,
				    0,
				    nullptr)) {
				return true;
			}
		}

	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		if (!targetPlan.RequiresVertexBuffer(buffer.slot))
			continue;

		target.kind = Bunny::CommandListTargetKind::VertexBuffer;
		target.slot = buffer.slot;
		if (FindIaTextureOverridesCached(
			    target,
			    0,
			    vertexCount,
			    indexCount,
			    instanceCount,
			    firstVertex,
				firstIndex,
				firstInstance,
				false,
				buffer.slot,
				nullptr) ||
			FindIaTextureOverridesCached(
			    target,
			    buffer.hash,
			    vertexCount,
			    indexCount,
			    instanceCount,
			    firstVertex,
				    firstIndex,
				    firstInstance,
				    false,
				    buffer.slot,
				    nullptr)) {
				return true;
			}
	}
	return false;
}

bool DX12ModHasAnyDirectDescriptorTextureOverrideCandidate(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaHashTargetPlan &targetPlan,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance)
{
	if (!commandList ||
	    !targetPlan.hasTextureOverrideChecks ||
	    !targetPlan.hasDescriptorTargets ||
	    targetPlan.descriptorTargets.empty()) {
		tlsDirectDescriptorPrefilterCache.Clear();
		return false;
	}

	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	const UINT64 descriptorQuerySerial =
		runtimeState && runtimeState->graphicsDescriptorQuerySerial ?
			runtimeState->graphicsDescriptorQuerySerial : 1;
	ID3D12DescriptorHeap *descriptorHeap =
		runtimeState ? runtimeState->cbvSrvUavHeap : nullptr;
	const UINT64 descriptorHeapVersion =
		DX12GetDescriptorHeapVersion(descriptorHeap);
	const UINT64 textureHashGeneration = DX12GetTextureHashGeneration();
	const UINT64 targetPlanFingerprint = targetPlan.Fingerprint();
	tlsDirectDescriptorPrefilterCache.SetInvocation(
		reloadGeneration,
		commandList,
		runtimeState,
		descriptorQuerySerial,
		descriptorHeap,
		descriptorHeapVersion,
		textureHashGeneration,
		targetPlanFingerprint,
		vertexCount,
		indexCount,
		instanceCount,
		firstVertex,
		firstIndex,
		firstInstance);
	if (!tlsDirectDescriptorPrefilterCache.matchesByTarget.empty() ||
		!tlsDirectDescriptorPrefilterCache.hasCandidate) {
		const bool cacheMatches = tlsDirectDescriptorPrefilterCache.MatchesInvocation(
			reloadGeneration,
			commandList,
			runtimeState,
			descriptorQuerySerial,
			descriptorHeap,
			descriptorHeapVersion,
			textureHashGeneration,
			targetPlanFingerprint,
			vertexCount,
			indexCount,
			instanceCount,
			firstVertex,
			firstIndex,
			firstInstance);
		if (cacheMatches &&
			(!tlsDirectDescriptorPrefilterCache.matchesByTarget.empty() ||
			 tlsDirectDescriptorPrefilterCache.hasCandidate)) {
			return tlsDirectDescriptorPrefilterCache.hasCandidate;
		}
	}

	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadTextureOverrideRuntimeSnapshot();
	if (!snapshot ||
		!HasPotentialResolvedDescriptorPrefilterCandidates(
			*snapshot,
			vertexCount,
			indexCount)) {
		return false;
	}

	static thread_local std::vector<uint32_t> tlsAllowedSectionIds;
	CollectResolvedDescriptorPrefilterSectionIds(
		*snapshot,
		vertexCount,
		indexCount,
		&tlsAllowedSectionIds);
	if (tlsAllowedSectionIds.empty())
		return false;

	ID3D12Device *cachedDevice = runtimeState ? runtimeState->device : nullptr;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> candidateMatchesByTarget;
	std::vector<DX12CurrentShaderResourceBinding> resolvedBindings[2][3];
	std::vector<DX12ResolvedGraphicsSrvBinding> hashedBindings[2][3];
	bool resolvedBindingsLoaded[2][3] = {};
	bool resolvedBindingsFound[2][3] = {};
	bool graphicsRootDependenciesKnown = runtimeState != nullptr;
	std::array<bool, MaxRootParameters> graphicsRootSeen = {};
	std::vector<DX12TextureOverrideMatch> matches;
	bool hasCandidate = false;
	for (const DX12IaHashTargetPlan::DescriptorTarget &descriptorTarget :
	     targetPlan.descriptorTargets) {
		Bunny::CommandListTarget target;
		target.kind = static_cast<Bunny::CommandListTargetKind>(
			descriptorTarget.kind);
		target.stage = static_cast<Bunny::CommandListShaderStage>(
			descriptorTarget.stage);
		target.slot = descriptorTarget.slot;
		const bool compute = TargetStageIsCompute(target.stage);
		const D3D12_DESCRIPTOR_RANGE_TYPE rangeType =
			DescriptorRangeTypeForTarget(target.kind);
		int rangeIndex = -1;
		switch (rangeType) {
		case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
			rangeIndex = 0;
			break;
		case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
			rangeIndex = 1;
			break;
		case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
			rangeIndex = 2;
			break;
		default:
			continue;
		}
		const int stageIndex = compute ? 1 : 0;
		if (!resolvedBindingsLoaded[stageIndex][rangeIndex]) {
			resolvedBindingsLoaded[stageIndex][rangeIndex] = true;
			resolvedBindingsFound[stageIndex][rangeIndex] =
				DX12BindingTracker::GetCurrentShaderResourceBindings(
					commandList,
					compute,
					rangeType,
					&resolvedBindings[stageIndex][rangeIndex]) &&
				!resolvedBindings[stageIndex][rangeIndex].empty();
			if (resolvedBindingsFound[stageIndex][rangeIndex]) {
				if (compute) {
					graphicsRootDependenciesKnown = false;
				} else {
					for (const DX12CurrentShaderResourceBinding &binding :
						 resolvedBindings[stageIndex][rangeIndex]) {
						if (binding.rootParameterIndex >= graphicsRootSeen.size()) {
							graphicsRootDependenciesKnown = false;
							break;
						}
						graphicsRootSeen[binding.rootParameterIndex] = true;
					}
				}
				ResolveGraphicsSrvBindings(
					commandList,
					cachedDevice,
					resolvedBindings[stageIndex][rangeIndex],
					&hashedBindings[stageIndex][rangeIndex]);
				AppendResolvedBindingsToDescriptorTextureOverrideCache(
					snapshot,
					hashedBindings[stageIndex][rangeIndex],
					target.kind,
					compute,
					tlsAllowedSectionIds,
					&candidateMatchesByTarget);
			} else {
				graphicsRootDependenciesKnown = false;
			}
		}
		if (!resolvedBindingsFound[stageIndex][rangeIndex])
			continue;
		const DX12TextureOverrideTargetCacheKey cacheKey = {
			static_cast<uint32_t>(target.kind),
			static_cast<uint32_t>(target.stage),
			target.slot
		};
		std::unordered_map<
			DX12TextureOverrideTargetCacheKey,
			std::vector<DX12TextureOverrideMatch>,
			DX12TextureOverrideTargetCacheKeyHash>::iterator matchIt =
			candidateMatchesByTarget.find(cacheKey);
		if (matchIt == candidateMatchesByTarget.end()) {
			tlsDirectDescriptorPrefilterCache.matchesByTarget.emplace(
				cacheKey,
				std::vector<DX12TextureOverrideMatch>());
			continue;
		}
		if (FilterDescriptorTextureOverrideCandidates(
				matchIt->second,
				vertexCount,
				indexCount,
				instanceCount,
				firstVertex,
				firstIndex,
				firstInstance,
				&matches)) {
			tlsDirectDescriptorPrefilterCache.matchesByTarget[cacheKey] = matches;
				tlsDirectDescriptorPrefilterCache.hasCandidate = true;
				hasCandidate = true;
				break;
		}
		tlsDirectDescriptorPrefilterCache.matchesByTarget[cacheKey] = matches;
	}

	tlsDirectDescriptorPrefilterCache.StoreGraphicsRootDependencies(
		runtimeState,
		graphicsRootDependenciesKnown,
		graphicsRootSeen);
	return hasCandidate;
}

static void BackfillCompiledPsoPlanFromFallback(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot)
{
	if (!pipelineState || configSnapshot.empty() ||
		DX12ModGetReloadGenerationFast() != reloadGeneration) {
		return;
	}

	AcquireSRWLockShared(&gModLock);
	if (DX12ModGetReloadGenerationFast() == reloadGeneration) {
		bool drawDirectHasNonTextureEffect = false;
		DX12IaHashTargetPlan drawDirectTargetPlan =
			DX12ShaderOverrideIaTargetPlanUtils::BuildDrawPlanLocked(
				configSnapshot,
				false,
				&drawDirectHasNonTextureEffect);
		DX12IaHashTargetPlan drawTargetPlan = drawDirectTargetPlan;
		DX12DrawMatchUsage drawMatchUsage =
			BuildDrawMatchUsageLocked(configSnapshot);
		DX12DrawMatchUsage directDrawMatchUsage = drawMatchUsage;
		const bool drawDirectTextureChecksOnly =
			!drawDirectHasNonTextureEffect;
		StoreCompiledPsoPlan(
			pipelineState,
			reloadGeneration,
			configSnapshot,
			&drawTargetPlan,
			&drawDirectTargetPlan,
			&drawMatchUsage,
			&directDrawMatchUsage,
			&drawDirectTextureChecksOnly);
	}
	ReleaseSRWLockShared(&gModLock);
}

bool DX12ModPrepareShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	bool hasActiveShaderOverrides,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	const DX12DrawMatchUsage &drawMatchUsage,
	DX12ModIaReplacement *replacement,
	bool allowDescriptorIndependentNegativeCache,
	ID3D12Device *cachedDevice,
	bool skipDirectDescriptorTargets,
	std::shared_ptr<const DX12ModIaReplacement> *generatedStaticReplacement)
{
	if (!replacement)
		return false;
	if (generatedStaticReplacement)
		generatedStaticReplacement->reset();
	if (!commandList || !pipelineState || !hasActiveShaderOverrides)
		return false;

	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();

	const bool negativeCacheEnabled = ShaderOverrideNegativeCacheEnabled(
		allowDescriptorIndependentNegativeCache);
	DX12ShaderOverrideNegativeCacheKey negativeCacheKey = {};
	if (negativeCacheEnabled) {
		negativeCacheKey = MakeShaderOverrideNegativeCacheKey(
			pipelineState, iaState,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance,
			drawMatchUsage);
		if (TryFindShaderOverrideNegativeCache(negativeCacheKey)) {
#if defined(_DEBUG)
			if (AllowShaderOverrideHotPathLog(&gShaderOverrideNegativeCacheHitLogCount)) {
				DX12LogManager::DebugFields("DX12ShaderOverrideNegativeCache",
					"\"status\":\"hit\",\"pso\":\"%p\"", pipelineState);
			}
#endif
			return false;
		}
	}

	// Try the precompiled PSO plan first (zero-lock, zero-discovery).
	// Eliminates per-draw lock+lookup+copy for PSOs recorded after mod loading.
	DX12ShaderOverrideConfigView configView;
	std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> compiledSnapshot;
	std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> compiledPlanSnapshot;
	bool alwaysMiss = false;
	bool compiledPlanHit = TryLoadCompiledPsoPlan(
		pipelineState, reloadGeneration,
		&compiledSnapshot, &compiledPlanSnapshot, &alwaysMiss);
	if (compiledPlanHit && alwaysMiss)
		return false;
	if (compiledPlanHit) {
		configView.SetOwnedSnapshot(compiledSnapshot);
		if (configView.Get().empty())
			compiledPlanHit = false;
	}
	bool cached = compiledPlanHit;

	const bool fallbackPlanResolved = !compiledPlanHit;
	if (!compiledPlanHit) {
		// Fallback: legacy PSO config cache lookup (first draw after mod reload).
		std::vector<const Bunny::ShaderOverrideConfig*> configs;
		AcquireSRWLockShared(&gModLock);
		cached = TryFindCachedShaderOverridesForPsoLocked(pipelineState, false, &configs);
		if (cached)
			CopyShaderOverrideConfigs(configs, configView.MutableFallbackSnapshot());
		ReleaseSRWLockShared(&gModLock);

		if (cached && configView.Get().empty())
			return false;

		if (!cached) {
			AcquireSRWLockExclusive(&gModLock);
			FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
			CopyShaderOverrideConfigs(configs, configView.MutableFallbackSnapshot());
			ReleaseSRWLockExclusive(&gModLock);
		}
		if (configView.Get().empty())
			return false;
	}
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot = configView.Get();
	if (fallbackPlanResolved) {
		BackfillCompiledPsoPlanFromFallback(
			pipelineState,
			reloadGeneration,
			configSnapshot);
	}

	ID3D12Device *device = AcquireModDeviceFast(commandList, cachedDevice);
	const bool deviceFromCache = (cachedDevice != nullptr);
	if (!device)
		return false;
	std::vector<DX12PendingResourceViewRequest> pendingResourceViews;
	DX12CommandListExecutor executor(
		device, commandList, iaState,
		vertexCount, indexCount, instanceCount,
		firstVertex, firstIndex, firstInstance,
		replacement, &pendingResourceViews);
	executor.SetExecutionMode(true, true);
	executor.SetSkipDirectDescriptorTargets(skipDirectDescriptorTargets);
	executor.AdoptDirectDescriptorPrefilterCache();
	if (compiledPlanSnapshot && !compiledPlanSnapshot->empty()) {
		executor.CollectDescriptorCheckTargetsFromCompiledPlans(
			*compiledPlanSnapshot);
		if (!executor.mDescriptorTargetScratch.empty()) {
			executor.PrimeDescriptorTextureOverrideCacheForTargets(
				&executor.mDescriptorTargetScratch);
		}
		for (const DX12CompiledShaderOverridePlan &plan : *compiledPlanSnapshot)
			executor.RunCompiledShaderOverridePlan(plan);
	} else {
		for (const Bunny::ShaderOverrideConfig &config : configSnapshot)
			executor.RunShaderOverride(config);
	}
	const bool resourcesChanged = ApplyPendingResourceViewsForCommandList(
		device, commandList, iaState, pendingResourceViews, replacement);
	if (resourcesChanged)
		executor.MarkExternalChange();
	LogShaderOverrideCommandListSnapshotLimited("pre", configSnapshot, *replacement);
	const bool changed = executor.Changed() || ReplacementHasAnyEffect(*replacement);
	if (!deviceFromCache)
		device->Release();

	if (ReplacementIsStaticTemplateCandidate(
			executor,
			pendingResourceViews,
			*replacement)) {
		StoreStaticReplacementForPso(
			pipelineState,
			reloadGeneration,
			*replacement);
		if (generatedStaticReplacement) {
			*generatedStaticReplacement =
				std::make_shared<const DX12ModIaReplacement>(*replacement);
		}
	}

	const bool negativeCacheStoreSafe = negativeCacheEnabled && !changed &&
		!skipDirectDescriptorTargets &&
		pendingResourceViews.empty() && !executor.observedDescriptorTarget &&
		!executor.observedDynamicCommandState;
	if (negativeCacheStoreSafe) {
		StoreShaderOverrideNegativeCache(negativeCacheKey);
#if defined(_DEBUG)
		if (AllowShaderOverrideHotPathLog(&gShaderOverrideNegativeCacheStoreLogCount)) {
			DX12LogManager::DebugFields("DX12ShaderOverrideNegativeCache",
				"\"status\":\"store\",\"pso\":\"%p\"", pipelineState);
		}
#endif
	} else if (negativeCacheEnabled && !changed && executor.observedDescriptorTarget) {
#if defined(_DEBUG)
		if (AllowShaderOverrideHotPathLog(
				&gShaderOverrideNegativeCacheDescriptorBlockedLogCount)) {
			DX12LogManager::DebugFields("DX12ShaderOverrideNegativeCache",
				"\"status\":\"descriptor_blocked\",\"pso\":\"%p\"",
				pipelineState);
		}
#endif
	} else if (negativeCacheEnabled && !changed && executor.observedDynamicCommandState) {
#if defined(_DEBUG)
		if (AllowShaderOverrideHotPathLog(
				&gShaderOverrideNegativeCacheDynamicBlockedLogCount)) {
			DX12LogManager::DebugFields("DX12ShaderOverrideNegativeCache",
				"\"status\":\"dynamic_state_blocked\",\"pso\":\"%p\"",
				pipelineState);
		}
#endif
	}

	return changed;
}

void DX12ModRunPostShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	DX12ModIaReplacement *replacement)
{
	if (!replacement || !commandList || !pipelineState || !DX12ModHasActiveShaderOverrides())
		return;
	if (replacement->postCommandLists.empty() &&
	    replacement->queuedPostTextureOverrides.empty())
		return;

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return;

	std::vector<DX12PendingResourceViewRequest> pendingResourceViews;
	DX12CommandListExecutor executor(
		device, commandList, iaState,
		vertexCount, indexCount, instanceCount,
		firstVertex, firstIndex, firstInstance,
		replacement, &pendingResourceViews);
	executor.SetExecutionMode(true, true);
	for (const std::wstring &list : replacement->postCommandLists)
		executor.RunCommandListLinks(Bunny::CommandListLinks{ {}, { list }, {} }, true);
	for (const DX12ModIaReplacement::QueuedTextureOverridePost &queued :
	     replacement->queuedPostTextureOverrides)
		executor.RunQueuedPostTextureOverride(queued);
	const bool resourcesChanged = ApplyPendingResourceViewsForCommandList(
		device, commandList, iaState, pendingResourceViews, replacement);
	if (resourcesChanged)
		executor.MarkExternalChange();
	device->Release();
}

static bool ShaderOverrideHasSkipLocked(uint64_t hash)
{
	auto it = gShaderOverrides.find(hash);
	return it != gShaderOverrides.end() && it->second.handlingSkip;
}

static bool ShaderBytecodeHasSkipLocked(const D3D12_SHADER_BYTECODE &bytecode)
{
	if (!bytecode.pShaderBytecode || !bytecode.BytecodeLength)
		return false;
	return ShaderOverrideHasSkipLocked(
		DX12ModHashShaderBytecode(bytecode.pShaderBytecode, bytecode.BytecodeLength));
}

static bool StoredPsoHasSkipLocked(const DX12StoredPso &record, bool dispatch)
{
	if (dispatch) {
		return record.kind == DX12PsoKind::Compute &&
			ShaderBytecodeHasSkipLocked(record.computeDesc.CS);
	}

	return record.kind == DX12PsoKind::Graphics &&
		(ShaderBytecodeHasSkipLocked(record.graphicsDesc.VS) ||
		 ShaderBytecodeHasSkipLocked(record.graphicsDesc.PS));
}

static void UpdateStoredPsoSkipLocked(DX12StoredPso *record)
{
	if (!record || record->skipGeneration == gReloadGeneration)
		return;

	record->skipDraw = false;
	record->skipDispatch = false;
	if (record->kind == DX12PsoKind::Compute) {
		record->skipDispatch = ShaderBytecodeHasSkipLocked(record->computeDesc.CS);
	} else {
		record->skipDraw =
			ShaderBytecodeHasSkipLocked(record->graphicsDesc.VS) ||
			ShaderBytecodeHasSkipLocked(record->graphicsDesc.PS);
	}
	record->skipGeneration = gReloadGeneration;
}

bool DX12ModShouldSkipPipelineState(ID3D12PipelineState *pipelineState, bool dispatch)
{
	if (!pipelineState || !DX12ModHasActiveShaderOverrides())
		return false;

	bool skip = false;
	AcquireSRWLockShared(&gModLock);
	auto record = gPsoRecords.find(pipelineState);
	if (record != gPsoRecords.end()) {
		if (record->second.skipGeneration == gReloadGeneration) {
			skip = dispatch ? record->second.skipDispatch : record->second.skipDraw;
			ReleaseSRWLockShared(&gModLock);
			return skip;
		}
	}
	ReleaseSRWLockShared(&gModLock);

	AcquireSRWLockExclusive(&gModLock);
	record = gPsoRecords.find(pipelineState);
	if (record != gPsoRecords.end()) {
		UpdateStoredPsoSkipLocked(&record->second);
		skip = dispatch ? record->second.skipDispatch : record->second.skipDraw;
		ReleaseSRWLockExclusive(&gModLock);
		return skip;
	}
	ReleaseSRWLockExclusive(&gModLock);

	DX12PsoShaderInfo info = {};
	if (!DX12GetPipelineStateShaderInfo(pipelineState, &info))
		return false;

	AcquireSRWLockShared(&gModLock);
	if (dispatch) {
		skip = info.hasCS && ShaderOverrideHasSkipLocked(info.cs);
	} else {
		skip = (info.hasVS && ShaderOverrideHasSkipLocked(info.vs)) ||
			(info.hasPS && ShaderOverrideHasSkipLocked(info.ps));
	}
	ReleaseSRWLockShared(&gModLock);
	return skip;
}

UINT64 DX12ModGetReloadGeneration()
{
	AcquireSRWLockShared(&gModLock);
	UINT64 generation = gReloadGeneration;
	ReleaseSRWLockShared(&gModLock);
	return generation;
}

UINT64 DX12ModGetReloadGenerationFast()
{
	return static_cast<UINT64>(InterlockedCompareExchange64(
		reinterpret_cast<volatile LONG64*>(&gReloadGeneration), 0, 0));
}

void DX12ModRecordGraphicsPipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc)
{
	if (!device || !pipelineState || !desc)
		return;

	DX12StoredPso record;
	record.kind = DX12PsoKind::Graphics;
	record.device = device;
	record.device->AddRef();
	DeepCopyGraphicsDesc(desc, &record);

	AcquireSRWLockExclusive(&gModLock);
	auto existing = gPsoRecords.find(pipelineState);
	if (existing != gPsoRecords.end())
		ReleaseStoredPso(&existing->second);

	// Eagerly compile PSO configuration and store the precompiled plan.
	// Eliminates per-draw lock+lookup+copy for subsequent draws.
	if (DX12ModHasActiveShaderOverrides()) {
		std::vector<const Bunny::ShaderOverrideConfig*> configs;
		FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
		std::vector<Bunny::ShaderOverrideConfig> snapshot;
		CopyShaderOverrideConfigs(configs, &snapshot);
		bool drawDirectHasNonTextureEffect = false;
		DX12IaHashTargetPlan drawDirectTargetPlan =
			DX12ShaderOverrideIaTargetPlanUtils::BuildDrawPlanLocked(
				snapshot,
				false,
				&drawDirectHasNonTextureEffect);
		DX12IaHashTargetPlan drawTargetPlan = drawDirectTargetPlan;
		DX12DrawMatchUsage drawMatchUsage = BuildDrawMatchUsageLocked(snapshot);
		DX12DrawMatchUsage directDrawMatchUsage = drawMatchUsage;
		const bool drawDirectTextureChecksOnly = !drawDirectHasNonTextureEffect;
		StoreCompiledPsoPlan(
			pipelineState,
			gReloadGeneration,
			snapshot,
			&drawTargetPlan,
			&drawDirectTargetPlan,
			&drawMatchUsage,
			&directDrawMatchUsage,
			&drawDirectTextureChecksOnly);
	}

	gPsoRecords[pipelineState] = record;
	ReleaseSRWLockExclusive(&gModLock);
}

void DX12ModRecordComputePipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc)
{
	if (!device || !pipelineState || !desc)
		return;

	DX12StoredPso record;
	record.kind = DX12PsoKind::Compute;
	record.device = device;
	record.device->AddRef();
	DeepCopyComputeDesc(desc, &record);

	AcquireSRWLockExclusive(&gModLock);
	auto existing = gPsoRecords.find(pipelineState);
	if (existing != gPsoRecords.end())
		ReleaseStoredPso(&existing->second);

	// Eagerly compile configuration for compute pipelines as well.
	if (DX12ModHasActiveShaderOverrides()) {
		std::vector<const Bunny::ShaderOverrideConfig*> configs;
		FindShaderOverridesForPsoLocked(pipelineState, true, &configs);
		std::vector<Bunny::ShaderOverrideConfig> snapshot;
		CopyShaderOverrideConfigs(configs, &snapshot);
		StoreCompiledPsoPlan(
			pipelineState,
			gReloadGeneration,
			snapshot,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr);
	}

	gPsoRecords[pipelineState] = record;
	ReleaseSRWLockExclusive(&gModLock);
}

static bool HasShaderOverrideLocked(uint64_t hash)
{
	return gShaderOverrides.find(hash) != gShaderOverrides.end();
}

static bool GraphicsPsoNeedsReplacementLocked(const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc)
{
	if (desc.VS.pShaderBytecode && desc.VS.BytecodeLength &&
	    HasShaderOverrideLocked(DX12ModHashShaderBytecode(desc.VS.pShaderBytecode, desc.VS.BytecodeLength)))
		return true;
	if (desc.PS.pShaderBytecode && desc.PS.BytecodeLength &&
	    HasShaderOverrideLocked(DX12ModHashShaderBytecode(desc.PS.pShaderBytecode, desc.PS.BytecodeLength)))
		return true;
	return false;
}

static bool ComputePsoNeedsReplacementLocked(const D3D12_COMPUTE_PIPELINE_STATE_DESC &desc)
{
	return desc.CS.pShaderBytecode && desc.CS.BytecodeLength &&
		HasShaderOverrideLocked(DX12ModHashShaderBytecode(desc.CS.pShaderBytecode, desc.CS.BytecodeLength));
}

static ID3D12PipelineState *CreateGraphicsReplacement(DX12StoredPso *record)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = record->graphicsDesc;
	D3D12_SHADER_BYTECODE vs = {};
	D3D12_SHADER_BYTECODE ps = {};
	std::vector<unsigned char> vsBytes;
	std::vector<unsigned char> psBytes;
	bool changed = false;

	if (DX12ModReplaceShaderBytecode("vs", desc.VS, &vs, &vsBytes)) {
		desc.VS = vs;
		changed = true;
	}
	if (DX12ModReplaceShaderBytecode("ps", desc.PS, &ps, &psBytes)) {
		desc.PS = ps;
		changed = true;
	}
	if (!changed)
		return nullptr;

	ID3D12PipelineState *replacement = nullptr;
	HRESULT hr = DX12DeviceHooks::CreateGraphicsPipelineStateOriginal(
		record->device, &desc, IID_PPV_ARGS(&replacement));
	DX12LogManager::DebugFields("DX12ReplacementPsoCreate",
		"\"kind\":\"graphics\",\"hr\":\"0x%lx\",\"pso\":\"%p\"",
		hr, replacement);
	return SUCCEEDED(hr) ? replacement : nullptr;
}

static ID3D12PipelineState *CreateComputeReplacement(DX12StoredPso *record)
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc = record->computeDesc;
	D3D12_SHADER_BYTECODE cs = {};
	std::vector<unsigned char> csBytes;
	if (!DX12ModReplaceShaderBytecode("cs", desc.CS, &cs, &csBytes))
		return nullptr;
	desc.CS = cs;

	ID3D12PipelineState *replacement = nullptr;
	HRESULT hr = DX12DeviceHooks::CreateComputePipelineStateOriginal(
		record->device, &desc, IID_PPV_ARGS(&replacement));
	DX12LogManager::DebugFields("DX12ReplacementPsoCreate",
		"\"kind\":\"compute\",\"hr\":\"0x%lx\",\"pso\":\"%p\"",
		hr, replacement);
	return SUCCEEDED(hr) ? replacement : nullptr;
}

ID3D12PipelineState *DX12ModGetReplacementPipelineState(ID3D12PipelineState *pipelineState)
{
	if (!pipelineState || !DX12ModHasActiveShaderOverrides())
		return nullptr;

	DX12StoredPso createRecord;
	bool shouldCreate = false;
	UINT64 generation = 0;

	AcquireSRWLockShared(&gModLock);
	std::unordered_map<ID3D12PipelineState*, DX12StoredPso>::iterator cached =
		gPsoRecords.find(pipelineState);
	if (cached == gPsoRecords.end()) {
		ReleaseSRWLockShared(&gModLock);
		return nullptr;
	}
	generation = gReloadGeneration;
	if (cached->second.replacement && cached->second.replacementGeneration == generation) {
		ID3D12PipelineState *replacement = cached->second.replacement;
		ReleaseSRWLockShared(&gModLock);
		return replacement;
	}
	if (cached->second.replacementNoopGeneration == generation) {
		ReleaseSRWLockShared(&gModLock);
		return nullptr;
	}
	ReleaseSRWLockShared(&gModLock);

	AcquireSRWLockExclusive(&gModLock);
	std::unordered_map<ID3D12PipelineState*, DX12StoredPso>::iterator it =
		gPsoRecords.find(pipelineState);
	if (it == gPsoRecords.end()) {
		ReleaseSRWLockExclusive(&gModLock);
		return nullptr;
	}

	DX12StoredPso &record = it->second;
	generation = gReloadGeneration;
	if (record.replacement && record.replacementGeneration == generation) {
		ID3D12PipelineState *replacement = record.replacement;
		ReleaseSRWLockExclusive(&gModLock);
		return replacement;
	}
	if (record.replacementNoopGeneration == generation) {
		ReleaseSRWLockExclusive(&gModLock);
		return nullptr;
	}

	if (record.replacement) {
		record.replacement->Release();
		record.replacement = nullptr;
		record.replacementGeneration = 0;
	}

	bool needsReplacement = record.kind == DX12PsoKind::Graphics ?
		GraphicsPsoNeedsReplacementLocked(record.graphicsDesc) :
		ComputePsoNeedsReplacementLocked(record.computeDesc);
	if (!needsReplacement) {
		record.replacementNoopGeneration = generation;
		ReleaseSRWLockExclusive(&gModLock);
		return nullptr;
	}
	record.replacementNoopGeneration = 0;

	createRecord.kind = record.kind;
	createRecord.device = record.device;
	createRecord.graphicsRootSignature = record.graphicsRootSignature;
	createRecord.computeRootSignature = record.computeRootSignature;
	createRecord.graphicsDesc = record.graphicsDesc;
	createRecord.computeDesc = record.computeDesc;
	createRecord.vsBytecode = record.vsBytecode;
	createRecord.psBytecode = record.psBytecode;
	createRecord.dsBytecode = record.dsBytecode;
	createRecord.hsBytecode = record.hsBytecode;
	createRecord.gsBytecode = record.gsBytecode;
	createRecord.csBytecode = record.csBytecode;
	if (!createRecord.vsBytecode.empty())
		createRecord.graphicsDesc.VS.pShaderBytecode = createRecord.vsBytecode.data();
	if (!createRecord.psBytecode.empty())
		createRecord.graphicsDesc.PS.pShaderBytecode = createRecord.psBytecode.data();
	if (!createRecord.dsBytecode.empty())
		createRecord.graphicsDesc.DS.pShaderBytecode = createRecord.dsBytecode.data();
	if (!createRecord.hsBytecode.empty())
		createRecord.graphicsDesc.HS.pShaderBytecode = createRecord.hsBytecode.data();
	if (!createRecord.gsBytecode.empty())
		createRecord.graphicsDesc.GS.pShaderBytecode = createRecord.gsBytecode.data();
	if (!createRecord.csBytecode.empty())
		createRecord.computeDesc.CS.pShaderBytecode = createRecord.csBytecode.data();
	if (createRecord.device)
		createRecord.device->AddRef();
	if (createRecord.graphicsRootSignature)
		createRecord.graphicsRootSignature->AddRef();
	if (createRecord.computeRootSignature)
		createRecord.computeRootSignature->AddRef();
	shouldCreate = true;
	ReleaseSRWLockExclusive(&gModLock);

	if (!shouldCreate)
		return nullptr;

	ID3D12PipelineState *newReplacement = createRecord.kind == DX12PsoKind::Graphics ?
		CreateGraphicsReplacement(&createRecord) :
		CreateComputeReplacement(&createRecord);
	if (createRecord.device)
		createRecord.device->Release();
	if (createRecord.graphicsRootSignature)
		createRecord.graphicsRootSignature->Release();
	if (createRecord.computeRootSignature)
		createRecord.computeRootSignature->Release();
	if (!newReplacement)
		return nullptr;

	AcquireSRWLockExclusive(&gModLock);
	it = gPsoRecords.find(pipelineState);
	if (it == gPsoRecords.end() || gReloadGeneration != generation) {
		ReleaseSRWLockExclusive(&gModLock);
		newReplacement->Release();
		return nullptr;
	}
	if (it->second.replacement)
		it->second.replacement->Release();
	it->second.replacement = newReplacement;
	it->second.replacementGeneration = generation;
	ReleaseSRWLockExclusive(&gModLock);
	return newReplacement;
}


bool DX12ModApplyDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement,
	bool executeOriginalDraw)
{
	if (!commandList || !replacement || replacement->drawSrvPatches.empty())
		return false;
	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return false;
	const bool applied = DX12DrawSrvReplacementUtils::ApplyPending(
		device, commandList, replacement, executeOriginalDraw);
	device->Release();
	return applied;
}


void DX12ModRestoreDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement)
{
	DX12DrawSrvReplacementUtils::Restore(commandList, replacement);
}

void DX12ModClearIaReplacementRuntimeCaches()
{
	DX12IaReplacementExecutor::ClearPsoDrawFastRejectCache();
	ClearShaderOverrideNegativeCache();
	ClearCompiledPsoPlans();
}
// PreSkin DynamicUAV output resize map.
// Maps original game-created output UAV resources to resized buffers
// when the mod vertex count exceeds the original capacity.
// The IASetVertexBuffers hook reads this map to redirect vb0 reads.

class DX12PreSkinOutputResizeEntry
{
public:
	ID3D12Resource *resizedResource = nullptr;
	UINT64 originalGpuVa = 0;
	UINT64 originalByteWidth = 0;
	UINT64 resizedByteWidth = 0;
	UINT stride = 0;
	UINT64 resizedGpuVa = 0;
};

static std::unordered_map<UINT64, DX12PreSkinOutputResizeEntry>
	gPreSkinOutputResizeMap;
using DX12PreSkinOutputResizeEntryMap =
	std::unordered_map<UINT64, DX12PreSkinOutputResizeEntry>;
static std::unordered_map<IUnknown*, DX12PreSkinOutputResizeEntryMap>
	gPreSkinOutputResizeCommandListMaps;
static SRWLOCK gPreSkinOutputResizeLock = SRWLOCK_INIT;
static std::atomic<bool> gHasPreSkinOutputResizeBindings{false};
static std::atomic<UINT64> gPreSkinOutputResizeGeneration{1};

static void BumpPreSkinOutputResizeGeneration()
{
	UINT64 current = gPreSkinOutputResizeGeneration.load(std::memory_order_relaxed);
	for (;;) {
		UINT64 next = current == UINT64_MAX ? 1 : current + 1;
		if (gPreSkinOutputResizeGeneration.compare_exchange_weak(
				current,
				next,
				std::memory_order_release,
				std::memory_order_relaxed))
			return;
	}
}

static IUnknown *PreSkinOutputResizeCommandListIdentity(
	ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;
	IUnknown *identity = DX12CommandListRuntime::GetCommandListIdentity(commandList);
	return identity ? identity : commandList;
}

static ID3D12Resource *StorePreSkinOutputResizeEntryLocked(
	DX12PreSkinOutputResizeEntryMap *entries,
	const DX12PreSkinOutputResizeEntry &entry)
{
	if (!entries || !entry.originalGpuVa || !entry.resizedResource)
		return nullptr;
	ID3D12Resource *oldResource = nullptr;
	auto oldIt = entries->find(entry.originalGpuVa);
	if (oldIt != entries->end())
		oldResource = oldIt->second.resizedResource;
	entry.resizedResource->AddRef();
	(*entries)[entry.originalGpuVa] = entry;
	return oldResource;
}

void DX12ModRegisterPreSkinOutputResize(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *originalResource, ID3D12Resource *resizedResource,
	UINT64 originalByteWidth, UINT64 resizedByteWidth, UINT stride)
{
	if (!originalResource || !resizedResource)
		return;
	const UINT64 originalGpuVa = originalResource->GetGPUVirtualAddress();
	const UINT64 resizedGpuVa = resizedResource->GetGPUVirtualAddress();
	if (!originalGpuVa || !resizedGpuVa)
		return;
	if (!originalByteWidth) {
		const D3D12_RESOURCE_DESC desc = originalResource->GetDesc();
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			originalByteWidth = desc.Width;
	}
	if (!originalByteWidth || !resizedByteWidth)
		return;
	DX12PreSkinOutputResizeEntry entry;
	entry.resizedResource = resizedResource;
	entry.originalGpuVa = originalGpuVa;
	entry.originalByteWidth = originalByteWidth;
	entry.resizedByteWidth = resizedByteWidth;
	entry.stride = stride;
	entry.resizedGpuVa = resizedGpuVa;

	// A global "latest output" is ambiguous while the engine records multiple
	// frames and shadow passes in parallel. Keep the producer command list's
	// exact output beside the compatibility map so its later IA bindings cannot
	// be redirected to a different frame's PreSkin result.
	ID3D12Resource *oldGlobalResource = nullptr;
	ID3D12Resource *oldCommandListResource = nullptr;
	IUnknown *commandListIdentity =
		PreSkinOutputResizeCommandListIdentity(commandList);
	AcquireSRWLockExclusive(&gPreSkinOutputResizeLock);
	oldGlobalResource =
		StorePreSkinOutputResizeEntryLocked(&gPreSkinOutputResizeMap, entry);
	if (commandListIdentity) {
		oldCommandListResource = StorePreSkinOutputResizeEntryLocked(
			&gPreSkinOutputResizeCommandListMaps[commandListIdentity], entry);
	}
	gHasPreSkinOutputResizeBindings.store(true, std::memory_order_release);
	BumpPreSkinOutputResizeGeneration();
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeLock);
	if (oldGlobalResource)
		oldGlobalResource->Release();
	if (oldCommandListResource)
		oldCommandListResource->Release();
}

static DX12PreSkinOutputResizeEntry *FindPreSkinOutputResizeEntryLocked(
	DX12PreSkinOutputResizeEntryMap *entries,
	UINT64 viewGpuVa,
	UINT64 *sourceOffset)
{
	if (sourceOffset)
		*sourceOffset = 0;
	if (!entries)
		return nullptr;
	auto exactIt = entries->find(viewGpuVa);
	if (exactIt != entries->end())
		return &exactIt->second;
	for (auto &item : *entries) {
		DX12PreSkinOutputResizeEntry &candidate = item.second;
		if (!candidate.originalGpuVa || !candidate.originalByteWidth ||
		    viewGpuVa < candidate.originalGpuVa)
			continue;
		const UINT64 candidateOffset = viewGpuVa - candidate.originalGpuVa;
		if (candidateOffset >= candidate.originalByteWidth)
			continue;
		if (sourceOffset)
			*sourceOffset = candidateOffset;
		return &candidate;
	}
	return nullptr;
}

bool DX12ModResolvePreSkinOutputResizeByGpuVa(
	ID3D12GraphicsCommandList *commandList,
	UINT64 viewGpuVa, DX12PreSkinOutputResizeBinding *binding)
{
	if (!viewGpuVa || !binding)
		return false;
	binding->Clear();
	IUnknown *commandListIdentity =
		PreSkinOutputResizeCommandListIdentity(commandList);
	AcquireSRWLockShared(&gPreSkinOutputResizeLock);
	DX12PreSkinOutputResizeEntry *entry = nullptr;
	UINT64 sourceOffset = 0;
	if (commandListIdentity) {
		auto local = gPreSkinOutputResizeCommandListMaps.find(commandListIdentity);
		if (local != gPreSkinOutputResizeCommandListMaps.end()) {
			entry = FindPreSkinOutputResizeEntryLocked(
				&local->second, viewGpuVa, &sourceOffset);
		}
	}
	if (!entry)
		entry = FindPreSkinOutputResizeEntryLocked(
			&gPreSkinOutputResizeMap, viewGpuVa, &sourceOffset);
	if (!entry || !entry->resizedResource || sourceOffset >= entry->resizedByteWidth) {
		ReleaseSRWLockShared(&gPreSkinOutputResizeLock);
		return false;
	}
	entry->resizedResource->AddRef();
	binding->resizedResource = entry->resizedResource;
	binding->retainedResource = true;
	binding->originalGpuVa = entry->originalGpuVa;
	binding->originalByteWidth = entry->originalByteWidth;
	binding->resizedGpuVa = entry->resizedGpuVa;
	binding->resizedByteWidth = entry->resizedByteWidth;
	binding->sourceOffset = sourceOffset;
	binding->stride = entry->stride;
	binding->uavBarrierPending = false;
	binding->transitionToVertex = false;
	binding->stateBefore = D3D12_RESOURCE_STATE_COMMON;
	ReleaseSRWLockShared(&gPreSkinOutputResizeLock);
	DX12ModTrackPreSkinOutputResizeCacheUse(
		commandList, binding->resizedResource);
	return true;
}

UINT64 DX12ModGetPreSkinOutputResizeGeneration()
{
	return gPreSkinOutputResizeGeneration.load(std::memory_order_acquire);
}

bool DX12ModHasPreSkinOutputResizeBindings()
{
	return gHasPreSkinOutputResizeBindings.load(std::memory_order_acquire);
}

void DX12ModForgetPreSkinOutputResizesForCommandList(
	ID3D12GraphicsCommandList *commandList)
{
	DX12ModAbandonPreSkinOutputResizeCacheUses(commandList);
	IUnknown *identity = PreSkinOutputResizeCommandListIdentity(commandList);
	if (!identity)
		return;
	std::vector<ID3D12Resource*> releaseResources;
	AcquireSRWLockExclusive(&gPreSkinOutputResizeLock);
	auto found = gPreSkinOutputResizeCommandListMaps.find(identity);
	if (found != gPreSkinOutputResizeCommandListMaps.end()) {
		releaseResources.reserve(found->second.size());
		for (auto &item : found->second) {
			if (item.second.resizedResource)
				releaseResources.push_back(item.second.resizedResource);
		}
		gPreSkinOutputResizeCommandListMaps.erase(found);
		BumpPreSkinOutputResizeGeneration();
	}
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeLock);
	for (ID3D12Resource *resource : releaseResources)
		resource->Release();
}

void DX12ModClearPreSkinOutputResizeMap()
{
	std::vector<ID3D12Resource*> releaseResources;
	AcquireSRWLockExclusive(&gPreSkinOutputResizeLock);
	for (auto &item : gPreSkinOutputResizeMap) {
		if (item.second.resizedResource)
			releaseResources.push_back(item.second.resizedResource);
		item.second.resizedResource = nullptr;
	}
	for (auto &commandList : gPreSkinOutputResizeCommandListMaps) {
		for (auto &item : commandList.second) {
			if (item.second.resizedResource)
				releaseResources.push_back(item.second.resizedResource);
			item.second.resizedResource = nullptr;
		}
	}
	gPreSkinOutputResizeMap.clear();
	gPreSkinOutputResizeCommandListMaps.clear();
	gHasPreSkinOutputResizeBindings.store(false, std::memory_order_release);
	BumpPreSkinOutputResizeGeneration();
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeLock);
	for (ID3D12Resource *resource : releaseResources)
		resource->Release();
}
