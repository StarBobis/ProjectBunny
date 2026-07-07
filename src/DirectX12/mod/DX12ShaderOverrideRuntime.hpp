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
			DX12LogManager::DebugJsonFunc("DX12ShaderRegexMatch",
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

// PSO-Create-Time pre-compiled override plan: snapshots all shader override configs
// for a PSO so draw-time discovery (lock+lookup+copy+iterate) is eliminated.
// Populated in RecordGraphicsPipelineState; consumed in PrepareShaderOverrideReplacement.
class DX12CompiledPsoOverridePlan
{
public:
	bool valid = false;
	bool alwaysMiss = false;
	UINT64 generation = 0;
	std::vector<Bunny::ShaderOverrideConfig> configSnapshot;
};

static std::unordered_map<ID3D12PipelineState*, DX12CompiledPsoOverridePlan> gPsoOverridePlans;
static SRWLOCK gPsoOverridePlansLock = SRWLOCK_INIT;

static bool TryLoadCompiledPsoPlan(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	std::vector<Bunny::ShaderOverrideConfig> *configSnapshot,
	bool *alwaysMiss)
{
	if (!configSnapshot || !alwaysMiss)
		return false;
	*alwaysMiss = false;
	configSnapshot->clear();
	if (!pipelineState)
		return false;

	AcquireSRWLockShared(&gPsoOverridePlansLock);
	auto it = gPsoOverridePlans.find(pipelineState);
	if (it == gPsoOverridePlans.end() || it->second.generation != reloadGeneration) {
		ReleaseSRWLockShared(&gPsoOverridePlansLock);
		return false;
	}
	*alwaysMiss = it->second.alwaysMiss;
	if (!it->second.alwaysMiss)
		*configSnapshot = it->second.configSnapshot;
	ReleaseSRWLockShared(&gPsoOverridePlansLock);
	return true;
}

static void StoreCompiledPsoPlan(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot)
{
	// Never store empty plans: PSOs created before mod loads would cache "alwaysMiss"
	// and never recover when the mod later loads matching configs.
	if (!pipelineState || configSnapshot.empty())
		return;

	AcquireSRWLockExclusive(&gPsoOverridePlansLock);
	gPsoOverridePlans[pipelineState] = {true, false, reloadGeneration, configSnapshot};
	ReleaseSRWLockExclusive(&gPsoOverridePlansLock);
}

static void ClearCompiledPsoPlans()
{
	AcquireSRWLockExclusive(&gPsoOverridePlansLock);
	gPsoOverridePlans.clear();
	ReleaseSRWLockExclusive(&gPsoOverridePlansLock);
}

class DX12ShaderOverrideIaTargetPlanUtils
{
public:
	static bool CommandListRuntimeEffectLocked(const std::wstring &name)
	{
		std::unordered_map<std::wstring, bool>::const_iterator found =
			gCommandListRuntimeEffect.find(name);
		return found == gCommandListRuntimeEffect.end() || found->second;
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
			targetPlan->RequireDescriptorTarget();
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
		bool collectTextureOverrideTargets);

	static void CollectActionsLocked(
		const std::vector<Bunny::CommandListAction> &actions,
		UINT depth,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets)
	{
		if (!targetPlan || depth > 16)
			return;

		for (const Bunny::CommandListAction &action : actions) {
			switch (action.kind) {
			case Bunny::CommandListActionKind::Run:
				CollectCommandListLocked(
					action.commandList, depth + 1, commandListStack,
					targetPlan, textureOverrideTargetsCollected,
					collectTextureOverrideTargets);
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
			case Bunny::CommandListActionKind::SetIndexBuffer:
				targetPlan->RequireIndexBufferView();
				break;
			case Bunny::CommandListActionKind::SetVertexBuffer:
				targetPlan->RequireVertexBuffer(action.target.slot);
				break;
			default:
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
		bool collectTextureOverrideTargets)
	{
		if (!targetPlan || depth > 16)
			return;

		for (const std::wstring &name : links.pre) {
			CollectCommandListLocked(
				name, depth + 1, commandListStack, targetPlan,
				textureOverrideTargetsCollected, collectTextureOverrideTargets);
		}
		for (const std::wstring &name : links.main) {
			CollectCommandListLocked(
				name, depth + 1, commandListStack, targetPlan,
				textureOverrideTargetsCollected, collectTextureOverrideTargets);
		}
		if (!includePost)
			return;
		for (const std::wstring &name : links.post) {
			CollectCommandListLocked(
				name, depth + 1, commandListStack, targetPlan,
				textureOverrideTargetsCollected, collectTextureOverrideTargets);
		}
	}

	static void CollectShaderOverrideConfigLocked(
		const Bunny::ShaderOverrideConfig &config,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected)
	{
		CollectCommandListLinksLocked(
			config.commandLists, true, 0, commandListStack, targetPlan,
			textureOverrideTargetsCollected, true);
		CollectActionsLocked(
			config.actions, 0, commandListStack, targetPlan,
			textureOverrideTargetsCollected, true);
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
					targetPlan, textureOverrideTargetsCollected, false);
				CollectActionsLocked(
					config.actions, 0, commandListStack, targetPlan,
					textureOverrideTargetsCollected, false);
			}
		}
	}

	static DX12IaHashTargetPlan BuildDrawPlanLocked(
		const DX12ShaderOverridePsoMatchCache &cache)
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
				&textureOverrideTargetsCollected);
		}
		for (const Bunny::ShaderOverrideConfig &config : cache.regexDrawConfigs) {
			CollectShaderOverrideConfigLocked(
				config, &commandListStack, &targetPlan,
				&textureOverrideTargetsCollected);
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
	bool collectTextureOverrideTargets)
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
			textureOverrideTargetsCollected, collectTextureOverrideTargets);
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

static bool ShaderOverrideNegativeCacheEnabled()
{
	return DX12ModHasActiveShaderOverrides() &&
		InterlockedCompareExchange(&gHasShaderDescriptorTextureOverrideTriggers, 0, 0) == 0;
}

static DX12ShaderOverrideNegativeCacheKey MakeShaderOverrideNegativeCacheKey(
	ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex)
{
	DX12ShaderOverrideNegativeCacheKey key = {};
	key.reloadGeneration = static_cast<UINT64>(InterlockedCompareExchange64(
		reinterpret_cast<volatile LONG64*>(&gShaderOverrideNegativeCacheGeneration), 0, 0));
	key.preSkinGeneration = CurrentPreSkinActiveGeneration();
	key.pipelineState = pipelineState;
	key.vertexCount = vertexCount;
	key.indexCount = indexCount;
	key.instanceCount = instanceCount;
	key.firstVertex = firstVertex;
	key.firstIndex = firstIndex;
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
	const UINT64 hash = HashShaderOverrideNegativeCacheKey(key);
	AcquireSRWLockShared(&gShaderOverrideNegativeCacheLock);
	auto it = gShaderOverrideNegativeCache.find(hash);
	const bool found = it != gShaderOverrideNegativeCache.end() &&
		it->second == key;
	ReleaseSRWLockShared(&gShaderOverrideNegativeCacheLock);
	return found;
}

static void StoreShaderOverrideNegativeCache(
	const DX12ShaderOverrideNegativeCacheKey &key)
{
	const UINT64 hash = HashShaderOverrideNegativeCacheKey(key);
	AcquireSRWLockExclusive(&gShaderOverrideNegativeCacheLock);
	if (gShaderOverrideNegativeCache.size() >= ShaderOverrideNegativeCacheMaxEntries)
		gShaderOverrideNegativeCache.clear();
	gShaderOverrideNegativeCache[hash] = key;
	ReleaseSRWLockExclusive(&gShaderOverrideNegativeCacheLock);
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
			DX12LogManager::DebugJsonFunc("DX12ShaderOverridePsoMatchCache",
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
	matchCache.drawTargetPlan =
		DX12ShaderOverrideIaTargetPlanUtils::BuildDrawPlanLocked(matchCache);
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
	targetPlan.RequireAll();
	bool cached = false;
	bool targetPlanResolved = false;

	AcquireSRWLockShared(&gModLock);
	cached = TryFindCachedShaderOverridesForPsoLocked(
		pipelineState, false, &configs);
	if (cached) {
		targetPlanResolved =
			TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
				pipelineState, &targetPlan);
	}
	ReleaseSRWLockShared(&gModLock);

	if (!cached) {
		AcquireSRWLockExclusive(&gModLock);
		FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
		cached = TryFindCachedShaderOverridesForPsoLocked(
			pipelineState, false, &configs);
		if (cached) {
			targetPlanResolved =
				TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
					pipelineState, &targetPlan);
		}
		ReleaseSRWLockExclusive(&gModLock);
	}

	const bool shaderOverrides = cached ? !configs.empty() : true;
	if (!targetPlanResolved)
		targetPlan.RequireAll();

	const bool shaderOverrideSkip =
		DX12ModShouldSkipPipelineState(pipelineState, false);
	const bool descriptorTrackingNeeded =
		DX12ModNeedsShaderDescriptorTracking();
	decision->SetFromTargetPlan(
		pipelineState, reloadGeneration, activeShaderOverrides,
		shaderOverrides, shaderOverrideSkip, targetPlan,
		descriptorTrackingNeeded);
	return true;
}

bool DX12ModPrepareShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex,
	DX12ModIaReplacement *replacement,
	bool allowNegativeCache,
	ID3D12Device *cachedDevice)
{
	if (!replacement)
		return false;
	*replacement = DX12ModIaReplacement();
	if (!commandList || !pipelineState || !DX12ModHasActiveShaderOverrides())
		return false;

	const bool negativeCacheEnabled =
		allowNegativeCache && ShaderOverrideNegativeCacheEnabled();
	DX12ShaderOverrideNegativeCacheKey negativeCacheKey = {};
	if (negativeCacheEnabled) {
		negativeCacheKey = MakeShaderOverrideNegativeCacheKey(
			pipelineState, iaState,
			vertexCount, indexCount, instanceCount, firstVertex, firstIndex);
		if (TryFindShaderOverrideNegativeCache(negativeCacheKey))
			return false;
	}

	// 方案D: Try PSO pre-compiled plan first (zero-lock, zero-discovery).
	// Eliminates per-draw lock+lookup+copy for PSOs recorded after mod loading.
	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	std::vector<Bunny::ShaderOverrideConfig> configSnapshot;
	bool alwaysMiss = false;
	const bool compiledPlanHit = TryLoadCompiledPsoPlan(
		pipelineState, reloadGeneration, &configSnapshot, &alwaysMiss);
	if (compiledPlanHit && alwaysMiss)
		return false;
	bool cached = compiledPlanHit;

	if (!compiledPlanHit) {
		// Fallback: legacy PSO config cache lookup (first draw after mod reload).
		std::vector<const Bunny::ShaderOverrideConfig*> configs;
		AcquireSRWLockShared(&gModLock);
		cached = TryFindCachedShaderOverridesForPsoLocked(pipelineState, false, &configs);
		if (cached)
			CopyShaderOverrideConfigs(configs, &configSnapshot);
		ReleaseSRWLockShared(&gModLock);

		if (cached && configSnapshot.empty())
			return false;

		if (!cached) {
			AcquireSRWLockExclusive(&gModLock);
			FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
			CopyShaderOverrideConfigs(configs, &configSnapshot);
			ReleaseSRWLockExclusive(&gModLock);
		}
		if (configSnapshot.empty())
			return false;
	}

	ID3D12Device *device = AcquireModDeviceFast(commandList, cachedDevice);
	const bool deviceFromCache = (cachedDevice != nullptr);
	if (!device)
		return false;
	std::vector<DX12PendingResourceViewRequest> pendingResourceViews;
	DX12CommandListExecutor executor(
		device, commandList, iaState,
		vertexCount, indexCount, instanceCount,
		firstVertex, firstIndex, replacement, &pendingResourceViews);
	executor.SetExecutionMode(true, true);
	for (const Bunny::ShaderOverrideConfig &config : configSnapshot) {
		executor.RunShaderOverride(config);
	}
	const bool resourcesChanged = ApplyPendingResourceViewsForCommandList(
		device, commandList, iaState, pendingResourceViews, replacement);
	if (resourcesChanged)
		executor.MarkExternalChange();
	LogShaderOverrideCommandListSnapshotLimited("pre", configSnapshot, *replacement);
	bool changed = executor.Changed() || replacement->skip || !replacement->draws.empty() ||
		!replacement->dispatches.empty();
	if (!deviceFromCache)
		device->Release();

	if (negativeCacheEnabled && !changed && pendingResourceViews.empty())
		StoreShaderOverrideNegativeCache(negativeCacheKey);

	return changed;
}

void DX12ModRunPostShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex,
	DX12ModIaReplacement *replacement)
{
	if (!replacement || !commandList || !pipelineState || !DX12ModHasActiveShaderOverrides())
		return;

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return;

	std::vector<const Bunny::ShaderOverrideConfig*> configs;
	std::vector<Bunny::ShaderOverrideConfig> configSnapshot;
	AcquireSRWLockExclusive(&gModLock);
	FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
	CopyShaderOverrideConfigs(configs, &configSnapshot);
	ReleaseSRWLockExclusive(&gModLock);
	if (!configSnapshot.empty()) {
		std::vector<DX12PendingResourceViewRequest> pendingResourceViews;
		DX12CommandListExecutor executor(
			device, commandList, iaState,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, replacement, &pendingResourceViews);
		executor.SetExecutionMode(true, true);
		for (const Bunny::ShaderOverrideConfig &config : configSnapshot) {
			executor.RunPostShaderOverrideLists(config);
		}
		const bool resourcesChanged = ApplyPendingResourceViewsForCommandList(
			device, commandList, iaState, pendingResourceViews, replacement);
		if (resourcesChanged)
			executor.MarkExternalChange();
		LogShaderOverrideCommandListSnapshotLimited("post", configSnapshot, *replacement);
	}
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

	// 方案D: Eager PSO config compilation + pre-compiled plan store.
	// Eliminates per-draw lock+lookup+copy for subsequent draws.
	if (DX12ModHasActiveShaderOverrides()) {
		std::vector<const Bunny::ShaderOverrideConfig*> configs;
		FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
		std::vector<Bunny::ShaderOverrideConfig> snapshot;
		CopyShaderOverrideConfigs(configs, &snapshot);
		StoreCompiledPsoPlan(pipelineState, gReloadGeneration, snapshot);
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

	// 方案D: Eager PSO config compilation for compute pipelines too.
	if (DX12ModHasActiveShaderOverrides()) {
		std::vector<const Bunny::ShaderOverrideConfig*> configs;
		FindShaderOverridesForPsoLocked(pipelineState, true, &configs);
		std::vector<Bunny::ShaderOverrideConfig> snapshot;
		CopyShaderOverrideConfigs(configs, &snapshot);
		StoreCompiledPsoPlan(pipelineState, gReloadGeneration, snapshot);
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
	DX12LogManager::DebugJsonFunc("DX12ReplacementPsoCreate",
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
	DX12LogManager::DebugJsonFunc("DX12ReplacementPsoCreate",
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

