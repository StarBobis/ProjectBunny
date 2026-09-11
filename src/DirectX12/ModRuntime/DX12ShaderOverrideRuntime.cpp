// DX12ShaderOverrideRuntime.cpp: shader override runtime: PSO records, override plans, caches and pipeline replacement.
// Split from DX12ModRuntime.cpp; implementation unit for its module.
#include "DX12ModRuntimeInternal.h"


void ReleaseStoredPso(DX12StoredPso *record)
{
	if (!record)
		return;
	if (record->replacement) {
		record->replacement->Release();
		record->replacement = nullptr;
	}
	if (record->device) {
		record->device->Release();
		record->device = nullptr;
	}
	if (record->graphicsRootSignature) {
		record->graphicsRootSignature->Release();
		record->graphicsRootSignature = nullptr;
	}
	if (record->computeRootSignature) {
		record->computeRootSignature->Release();
		record->computeRootSignature = nullptr;
	}
}


void StoreShaderBytecode(
	const D3D12_SHADER_BYTECODE &source, std::vector<unsigned char> *storage,
	D3D12_SHADER_BYTECODE *target)
{
	if (!storage || !target)
		return;

	*target = {};
	storage->clear();
	if (!source.pShaderBytecode || source.BytecodeLength == 0)
		return;

	storage->resize(source.BytecodeLength);
	memcpy(storage->data(), source.pShaderBytecode, source.BytecodeLength);
	target->pShaderBytecode = storage->data();
	target->BytecodeLength = storage->size();
}


void DeepCopyGraphicsDesc(
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC *source, DX12StoredPso *record)
{
	record->graphicsDesc = *source;
	record->graphicsRootSignature = source->pRootSignature;
	if (record->graphicsRootSignature)
		record->graphicsRootSignature->AddRef();
	StoreShaderBytecode(source->VS, &record->vsBytecode, &record->graphicsDesc.VS);
	StoreShaderBytecode(source->PS, &record->psBytecode, &record->graphicsDesc.PS);
	StoreShaderBytecode(source->DS, &record->dsBytecode, &record->graphicsDesc.DS);
	StoreShaderBytecode(source->HS, &record->hsBytecode, &record->graphicsDesc.HS);
	StoreShaderBytecode(source->GS, &record->gsBytecode, &record->graphicsDesc.GS);
}


void DeepCopyComputeDesc(
	const D3D12_COMPUTE_PIPELINE_STATE_DESC *source, DX12StoredPso *record)
{
	record->computeDesc = *source;
	record->computeRootSignature = source->pRootSignature;
	if (record->computeRootSignature)
		record->computeRootSignature->AddRef();
	StoreShaderBytecode(source->CS, &record->csBytecode, &record->computeDesc.CS);
}


uint64_t DX12ModHashShaderBytecode(const void *data, size_t size)
{
	const unsigned char *bytes = static_cast<const unsigned char*>(data);
	uint64_t hash = 14695981039346656037ull;
	for (size_t i = 0; i < size; ++i) {
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}


bool DX12ModHasShaderOverride(uint64_t hash)
{
	DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
	bool found = gShaderOverrides.find(hash) != gShaderOverrides.end();
	ReleaseSRWLockShared(&gModLock);
	return found;
}


bool DX12ModHasActiveShaderOverrides()
{
	return (DX12ModGetHotPathFlags() & DX12_MOD_HOT_SHADER_OVERRIDES) != 0;
}


bool DX12ModNeedsShaderDescriptorTracking()
{
	return (DX12ModGetHotPathFlags() &
			DX12_MOD_HOT_SHADER_DESCRIPTOR_TRACK) != 0;
}


bool DX12ModHasActiveTextureOverrides()
{
	return gHasTextureOverrides != 0 &&
		   gHasShaderTriggeredTextureOverrides != 0 &&
		   gDx12SafeMode == 0;
}


bool DX12ModHasConfiguredTextureOverrides()
{
	return gHasTextureOverrides != 0 &&
		   gDx12SafeMode == 0;
}


bool DX12ModHasAnyActiveOverrides()
{
	return gDx12SafeMode == 0 &&
		   (gHasShaderOverrides != 0 ||
			gHasShaderRegexes != 0 ||
			(gHasTextureOverrides != 0 && gHasShaderTriggeredTextureOverrides != 0));
}


bool DX12ModNeedsPresentReplacement()
{
	return (DX12ModGetHotPathFlags() &
			DX12_MOD_HOT_PRESENT_REPLACEMENT) != 0;
}


UINT32 DX12ModGetHotPathFlags()
{
	if (gDx12SafeMode != 0)
		return 0;

	UINT32 flags = 0;
	const bool hasShaderOverrides =
		gHasShaderOverrides != 0 || gHasShaderRegexes != 0;
	const bool hasTextureOverrides =
		gHasTextureOverrides != 0 &&
		gHasShaderTriggeredTextureOverrides != 0;
	const bool hasDrawOverrideWork =
		hasShaderOverrides || hasTextureOverrides;

	if (hasShaderOverrides)
		flags |= DX12_MOD_HOT_SHADER_OVERRIDES;
	if (hasDrawOverrideWork && gHasIaResidentStateRequirements != 0)
		flags |= DX12_MOD_HOT_IA_RESIDENT_STATE;
	if (gHasPresentRuntimeEffect != 0)
		flags |= DX12_MOD_HOT_PRESENT_REPLACEMENT;
	if (hasTextureOverrides &&
		gHasPreSkinTextureOverrideCandidates != 0)
	{
		flags |= DX12_MOD_HOT_PRE_SKIN_PROBE;
		const bool hasActivePreSkinOverrides =
			gHasActivePreSkinTextureOverridesFast != 0;
		if (hasActivePreSkinOverrides ||
			gPreSkinBindingIdleFrames < kPreSkinBindingIdleMax)
		{
			flags |= DX12_MOD_HOT_PRE_SKIN_BINDING_TRACK;
		}
	}
	if (hasTextureOverrides &&
		gHasDrawSrvTextureReplacementOverrides != 0)
		flags |= DX12_MOD_HOT_DRAW_SRV_SLOT_REPLACEMENT;
	if (gHasShaderDescriptorTextureOverrideTriggers != 0)
	{
		flags |= DX12_MOD_HOT_SHADER_DESCRIPTOR_TRACK;
	}
	// Plan step 3.2: bind-time static IA replacements keep their staleness
	// bookkeeping on DX12ActiveIaState (checked lazily by
	// RestoreStaleStaticIaBindings once per content/resolve generation change).
	// IA resident state keeps the recorded views fresh. FLAG_SHADER_OVERRIDES
	// is deliberately NOT forced here: static IA replacement is unrelated to
	// shader overrides, and forcing the flag made every draw enter the full
	// HandleDrawOverrides chain (plan fix B).
	if (gHasStaticIaReplacements != 0)
		flags |= DX12_MOD_HOT_IA_RESIDENT_STATE;
	return flags;
}


bool ShaderModelMatchesRegex(
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


bool AllowShaderOverrideHotPathLog(volatile LONG *counter)
{
	(void)counter;
	return false;
}


Bunny::ShaderOverrideConfig MakeShaderOverrideFromRegex(
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


void AppendShaderRegexForShaderLocked(
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
	}
}


void BuildShaderOverrideConfigPointers(
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


void CopyShaderOverrideConfigs(
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


void MergeTextureOverrideDrawMatchUsage(
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


void AppendUniqueDrawMatchUsageTargetLocked(
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


bool DrawMatchUsageCommandListRuntimeEffectLocked(const std::wstring &name)
{
	std::unordered_map<std::wstring, bool>::const_iterator found =
		gCommandListPreRuntimeEffect.find(name);
	return found == gCommandListPreRuntimeEffect.end() || found->second;
}


void CollectCheckTextureOverrideTargetsFromActionsLocked(
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


void CollectCheckTextureOverrideTargetsFromShaderOverrideLocked(
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



void MergeDescriptorTextureOverrideDrawMatchUsageLocked(
	DX12DrawMatchUsage *usage)
{
	if (!usage)
		return;
	const DX12CommandExecutionRuntimeSnapshot *snapshot =
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


void MergeIaTextureOverrideDrawMatchUsageLocked(
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


DX12DrawMatchUsage BuildTextureOverrideDrawMatchUsageLocked(
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


DX12DrawMatchUsage BuildDrawMatchUsageLocked(
	const std::vector<const Bunny::ShaderOverrideConfig*> &configs)
{
	std::vector<Bunny::CommandListTarget> targets;
	for (const Bunny::ShaderOverrideConfig *config : configs) {
		if (config)
			CollectCheckTextureOverrideTargetsFromShaderOverrideLocked(*config, &targets);
	}
	return BuildTextureOverrideDrawMatchUsageLocked(targets);
}


DX12DrawMatchUsage BuildDrawMatchUsageLocked(
	const std::vector<Bunny::ShaderOverrideConfig> &configs)
{
	std::vector<Bunny::CommandListTarget> targets;
	for (const Bunny::ShaderOverrideConfig &config : configs) {
		CollectCheckTextureOverrideTargetsFromShaderOverrideLocked(config, &targets);
	}
	return BuildTextureOverrideDrawMatchUsageLocked(targets);
}


void ClearPsoOverridesIfFormatChanged()
{
	if (gPsoOverridePlansFormatVersion !=
		DX12PsoDecisionCacheUtils::CacheFormatVersionCurrent) {
		AcquireSRWLockExclusive(&gPsoOverridePlansLock);
		gPsoOverridePlans.clear();
		gPsoOverridePlansFormatVersion =
			DX12PsoDecisionCacheUtils::CacheFormatVersionCurrent;
		ReleaseSRWLockExclusive(&gPsoOverridePlansLock);
	}
}


bool TryLoadCompiledPsoPlan(
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

	ClearPsoOverridesIfFormatChanged();
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


bool TryLoadCompiledPsoDrawDecisionMetadata(
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


std::shared_ptr<const DX12ModIaReplacement> DX12ModFindStaticReplacementSnapshotForPso(
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


void StoreCompiledPsoPlan(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot,
	const DX12IaHashTargetPlan *drawTargetPlan,
	const DX12IaHashTargetPlan *drawDirectTargetPlan,
	const DX12DrawMatchUsage *drawMatchUsage,
	const DX12DrawMatchUsage *drawDirectDrawMatchUsage,
	const bool *drawDirectTextureChecksOnly)
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


void StoreStaticReplacementForPso(
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


void ClearCompiledPsoPlans()
{
	AcquireSRWLockExclusive(&gPsoOverridePlansLock);
	gPsoOverridePlans.clear();
	ReleaseSRWLockExclusive(&gPsoOverridePlansLock);
}


bool ReplacementHasAnyEffect(const DX12ModIaReplacement &replacement)
{
	return replacement.skip ||
		!replacement.draws.empty() ||
		!replacement.dispatches.empty() ||
		!replacement.drawSrvPatches.empty() ||
		!replacement.postCommandLists.empty() ||
		!replacement.queuedPostTextureOverrides.empty();
}


bool ReplacementIsStaticTemplateCandidate(
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


bool CompiledCommandListPlanIsStaticTemplateCandidate(
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


bool CompiledShaderOverridePlanIsStaticTemplateCandidate(
	const DX12CompiledShaderOverridePlan &plan)
{
	if (plan.unsupported)
		return false;
	if (plan.hasPostCommandListPlan || !plan.postCommandLists.empty())
		return false;
	return CompiledCommandListPlanIsStaticTemplateCandidate(plan.mainPlan);
}


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


bool ShaderOverrideNegativeCacheEnabled(
	bool allowDescriptorIndependentNegativeCache)
{
	return allowDescriptorIndependentNegativeCache &&
		DX12ModHasActiveShaderOverrides();
}


DX12ShaderOverrideNegativeCacheKey MakeShaderOverrideNegativeCacheKey(
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


UINT64 HashShaderOverrideNegativeCacheKey(
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


bool TryFindShaderOverrideNegativeCache(
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


void StoreShaderOverrideNegativeCache(
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


void ClearShaderOverrideNegativeCache()
{
	InterlockedIncrement64(
		reinterpret_cast<volatile LONG64*>(&gShaderOverrideNegativeCacheGeneration));
}


bool TryFindCachedShaderOverridesForPsoLocked(
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
		return true;
	}
	return false;
}


// Public draw-path PSO plan lookup used before IA hash work.
// This avoids building IA hashes for PSOs with no matching shader overrides.
bool TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
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


bool TryFindCachedShaderOverrideDirectIaTargetPlanForPsoLocked(
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


bool TryFindCachedShaderOverrideDirectTextureChecksOnlyForPsoLocked(
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


bool TryFindCachedShaderOverrideDrawMatchUsageForPsoLocked(
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
	DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
	cached = TryFindCachedShaderOverridesForPsoLocked(pipelineState, false, &configs);
	ReleaseSRWLockShared(&gModLock);
	return !cached || !configs.empty();
}


void FindShaderOverridesForPsoLocked(
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

	DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
	const bool cachedPlan =
		TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
			pipelineState, targetPlan);
	ReleaseSRWLockShared(&gModLock);
	if (cachedPlan)
		return true;

	std::vector<const Bunny::ShaderOverrideConfig*> configs;
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gModLock, "Lock.modExclusive");
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
	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
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

	ClearPsoOverridesIfFormatChanged();
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
		DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
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
		DX12_LOCK_ACQUIRE_EXCLUSIVE(&gModLock, "Lock.modExclusive");
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

	const bool shaderOverrides = compiledMetadataHit ? true : !configs.empty();
	if (!compiledMetadataHit) {
		if (!cached)
			drawMatchUsage = BuildDrawMatchUsageLocked(configs);
		directDrawMatchUsage = drawMatchUsage;
	}
	if (!targetPlanResolved) {
		// Only PSOs that actually matched override configs need the conservative
		// RequireAll plan.  Unmatched PSOs get an empty plan so the decision can
		// become NoopNeverMatches and fast-reject every draw on this PSO.
		if (shaderOverrides)
			targetPlan.RequireAll();
		else
			targetPlan.ResetKnown();
	}
	if (!directTargetPlanResolved) {
		if (shaderOverrides)
			directTargetPlan.RequireAll();
		else
			directTargetPlan.ResetKnown();
	}

	bool skipDraw = false;
	bool skipDispatch = false;
	DX12ModGetPipelineStateSkipFlags(
		pipelineState, nullptr, &skipDraw, &skipDispatch);
	const bool shaderOverrideSkip = skipDraw;
	const bool descriptorTrackingNeeded =
		DX12ModNeedsShaderDescriptorTracking();
	decision->SetFromTargetPlan(
		pipelineState, reloadGeneration, activeShaderOverrides,
		shaderOverrides, shaderOverrideSkip, targetPlan, directTargetPlan,
		drawMatchUsage, directDrawMatchUsage,
		directTextureChecksOnly,
		descriptorTrackingNeeded);
	// Plan step 3.1: when every descriptor-relevant hashed texture override is
	// an unconditional static replacement handled at descriptor write time,
	// this decision's descriptor targets are already satisfied by the patched
	// descriptors, so the draw path can skip SRV binding resolution entirely.
	decision->descriptorTargetsStatic =
		DX12StaticTextureReplacementAllDescriptorOverridesStatic();
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
		// Query the real hash first: hash-triggered configs (the common case)
		// either match here or miss both times, so ordering the wildcard
		// (hash==0) probe after the real one saves one full lookup+snapshot
		// cycle per draw for every config that does match. OR semantics make
		// the order irrelevant for the result.
		if (FindIaTextureOverridesCached(
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
				    nullptr) ||
			FindIaTextureOverridesCached(
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
			    buffer.hash,
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
			    0,
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

	const DX12CommandExecutionRuntimeSnapshot *snapshot =
		LoadTextureOverrideRuntimeSnapshot();
	if (!snapshot ||
		!HasPotentialResolvedDescriptorPrefilterCandidates(
			*snapshot,
			vertexCount,
			indexCount)) {
		return false;
	}

	thread_local std::vector<uint32_t> tlsAllowedSectionIds;
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
			// SRV-range graphics targets: reuse the cached resolution when the
			// root tables and observed descriptor slots are unchanged. The
			// cached result is PSO/draw-argument independent - the compiled
			// target plan is applied below, per draw.
			const bool srvGraphicsCacheable =
				!compute && rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			if (srvGraphicsCacheable &&
			    PrefilterSrvCacheTryLoad(
				    commandList,
				    runtimeState,
				    stageIndex,
				    reloadGeneration,
				    &hashedBindings[stageIndex][rangeIndex])) {
				resolvedBindingsFound[stageIndex][rangeIndex] =
					!hashedBindings[stageIndex][rangeIndex].empty();
				if (resolvedBindingsFound[stageIndex][rangeIndex]) {
					for (const DX12ResolvedGraphicsSrvBinding &resolved :
					     hashedBindings[stageIndex][rangeIndex]) {
						if (resolved.binding.rootParameterIndex >=
						    graphicsRootSeen.size()) {
							graphicsRootDependenciesKnown = false;
							break;
						}
						graphicsRootSeen[
							resolved.binding.rootParameterIndex] = true;
					}
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
			} else {
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
					if (srvGraphicsCacheable) {
						PrefilterSrvCacheStore(
							commandList,
							runtimeState,
							stageIndex,
							reloadGeneration,
							resolvedBindings[stageIndex][rangeIndex],
							hashedBindings[stageIndex][rangeIndex]);
					}
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


void BackfillCompiledPsoPlanFromFallback(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot)
{
	if (!pipelineState || configSnapshot.empty() ||
		DX12ModGetReloadGenerationFast() != reloadGeneration) {
		return;
	}

	DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
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
		DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
		cached = TryFindCachedShaderOverridesForPsoLocked(pipelineState, false, &configs);
		if (cached)
			CopyShaderOverrideConfigs(configs, configView.MutableFallbackSnapshot());
		ReleaseSRWLockShared(&gModLock);

		if (cached && configView.Get().empty())
			return false;

		if (!cached) {
			DX12_LOCK_ACQUIRE_EXCLUSIVE(&gModLock, "Lock.modExclusive");
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
	} else if (negativeCacheEnabled && !changed && executor.observedDescriptorTarget) {
	} else if (negativeCacheEnabled && !changed && executor.observedDynamicCommandState) {
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


bool ShaderOverrideHasSkipLocked(uint64_t hash)
{
	auto it = gShaderOverrides.find(hash);
	return it != gShaderOverrides.end() && it->second.handlingSkip;
}


bool ShaderBytecodeHasSkipLocked(const D3D12_SHADER_BYTECODE &bytecode)
{
	if (!bytecode.pShaderBytecode || !bytecode.BytecodeLength)
		return false;
	return ShaderOverrideHasSkipLocked(
		DX12ModHashShaderBytecode(bytecode.pShaderBytecode, bytecode.BytecodeLength));
}



void UpdateStoredPsoSkipLocked(DX12StoredPso *record)
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


bool DX12ModGetPipelineStateSkipFlags(
	ID3D12PipelineState *pipelineState,
	const DX12PsoShaderInfo *shaderInfo,
	bool *skipDraw,
	bool *skipDispatch)
{
	if (skipDraw)
		*skipDraw = false;
	if (skipDispatch)
		*skipDispatch = false;
	if (!pipelineState || !DX12ModHasActiveShaderOverrides())
		return false;

	DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
	auto record = gPsoRecords.find(pipelineState);
	if (record != gPsoRecords.end()) {
		if (record->second.skipGeneration == gReloadGeneration) {
			if (skipDraw)
				*skipDraw = record->second.skipDraw;
			if (skipDispatch)
				*skipDispatch = record->second.skipDispatch;
			ReleaseSRWLockShared(&gModLock);
			return true;
		}
	}
	ReleaseSRWLockShared(&gModLock);

	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gModLock, "Lock.modExclusive");
	record = gPsoRecords.find(pipelineState);
	if (record != gPsoRecords.end()) {
		UpdateStoredPsoSkipLocked(&record->second);
		if (skipDraw)
			*skipDraw = record->second.skipDraw;
		if (skipDispatch)
			*skipDispatch = record->second.skipDispatch;
		ReleaseSRWLockExclusive(&gModLock);
		return true;
	}
	ReleaseSRWLockExclusive(&gModLock);

	DX12PsoShaderInfo queriedInfo = {};
	if (!shaderInfo) {
		if (!DX12GetPipelineStateShaderInfo(pipelineState, &queriedInfo))
			return false;
		shaderInfo = &queriedInfo;
	}

	DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
	if (skipDispatch)
		*skipDispatch = shaderInfo->hasCS && ShaderOverrideHasSkipLocked(shaderInfo->cs);
	if (skipDraw)
		*skipDraw = (shaderInfo->hasVS && ShaderOverrideHasSkipLocked(shaderInfo->vs)) ||
			(shaderInfo->hasPS && ShaderOverrideHasSkipLocked(shaderInfo->ps));
	ReleaseSRWLockShared(&gModLock);
	return true;
}


bool DX12ModShouldSkipPipelineState(ID3D12PipelineState *pipelineState, bool dispatch)
{
	bool skipDraw = false;
	bool skipDispatch = false;
	if (!DX12ModGetPipelineStateSkipFlags(
			pipelineState, nullptr, &skipDraw, &skipDispatch))
		return false;
	return dispatch ? skipDispatch : skipDraw;
}


UINT64 DX12ModGetReloadGeneration()
{
	DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
	UINT64 generation = gReloadGeneration;
	ReleaseSRWLockShared(&gModLock);
	return generation;
}


UINT64 DX12ModGetReloadGenerationFast()
{
	// gReloadGeneration is volatile; an aligned 64-bit load is atomic on x64.
	// This runs on every Draw/Dispatch resident fast-reject, so avoid the
	// LOCK CMPXCHG that a read-modify-write helper would cost.
	return gReloadGeneration;
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

	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gModLock, "Lock.modExclusive");
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

	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gModLock, "Lock.modExclusive");
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


bool HasShaderOverrideLocked(uint64_t hash)
{
	return gShaderOverrides.find(hash) != gShaderOverrides.end();
}


bool GraphicsPsoNeedsReplacementLocked(const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc)
{
	if (desc.VS.pShaderBytecode && desc.VS.BytecodeLength &&
	    HasShaderOverrideLocked(DX12ModHashShaderBytecode(desc.VS.pShaderBytecode, desc.VS.BytecodeLength)))
		return true;
	if (desc.PS.pShaderBytecode && desc.PS.BytecodeLength &&
	    HasShaderOverrideLocked(DX12ModHashShaderBytecode(desc.PS.pShaderBytecode, desc.PS.BytecodeLength)))
		return true;
	return false;
}


bool ComputePsoNeedsReplacementLocked(const D3D12_COMPUTE_PIPELINE_STATE_DESC &desc)
{
	return desc.CS.pShaderBytecode && desc.CS.BytecodeLength &&
		HasShaderOverrideLocked(DX12ModHashShaderBytecode(desc.CS.pShaderBytecode, desc.CS.BytecodeLength));
}


ID3D12PipelineState *CreateGraphicsReplacement(DX12StoredPso *record)
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
	return SUCCEEDED(hr) ? replacement : nullptr;
}


ID3D12PipelineState *CreateComputeReplacement(DX12StoredPso *record)
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
	return SUCCEEDED(hr) ? replacement : nullptr;
}


ID3D12PipelineState *DX12ModGetReplacementPipelineState(ID3D12PipelineState *pipelineState)
{
	if (!pipelineState || !DX12ModHasActiveShaderOverrides())
		return nullptr;

	DX12StoredPso createRecord;
	bool shouldCreate = false;
	UINT64 generation = 0;

	DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
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

	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gModLock, "Lock.modExclusive");
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

	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gModLock, "Lock.modExclusive");
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


void DX12ModClearIaReplacementRuntimeCaches()
{
	DX12IaReplacementExecutor::ClearPsoDrawFastRejectCache();
	ClearShaderOverrideNegativeCache();
	ClearCompiledPsoPlans();
}
