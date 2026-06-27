static bool NumericMatchSatisfied(const Bunny::NumericMatch &match, uint32_t actual)
{
	if (!match.enabled)
		return true;
	switch (match.op) {
	case Bunny::NumericMatchOp::Equal:
		return actual == match.value;
	case Bunny::NumericMatchOp::NotEqual:
		return actual != match.value;
	case Bunny::NumericMatchOp::Less:
		return actual < match.value;
	case Bunny::NumericMatchOp::LessEqual:
		return actual <= match.value;
	case Bunny::NumericMatchOp::Greater:
		return actual > match.value;
	case Bunny::NumericMatchOp::GreaterEqual:
		return actual >= match.value;
	default:
		return false;
	}
}

static bool TextureOverrideMatchesDrawContext(
	const Bunny::TextureOverrideConfig &config,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance)
{
	if (config.hasMatchVertexCount && !NumericMatchSatisfied(config.matchVertexCount, vertexCount))
		return false;
	if (config.hasMatchIndexCount && !NumericMatchSatisfied(config.matchIndexCount, indexCount))
		return false;
	if (config.hasMatchInstanceCount && !NumericMatchSatisfied(config.matchInstanceCount, instanceCount))
		return false;
	if (config.hasMatchFirstVertex && !NumericMatchSatisfied(config.matchFirstVertex, firstVertex))
		return false;
	if (config.hasMatchFirstIndex && !NumericMatchSatisfied(config.matchFirstIndex, firstIndex))
		return false;
	if (config.hasMatchFirstInstance && !NumericMatchSatisfied(config.matchFirstInstance, firstInstance))
		return false;
	return true;
}

static bool TextureOverrideHasDrawKind(
	const Bunny::TextureOverrideConfig &config, Bunny::CommandListActionKind kind)
{
	for (const Bunny::CommandListAction &action : config.actions) {
		if (action.kind == kind)
			return true;
	}
	return false;
}

static bool TextureOverrideHasDrawContextMatch(const Bunny::TextureOverrideConfig &config)
{
	return config.hasMatchVertexCount || config.hasMatchIndexCount ||
		config.hasMatchInstanceCount || config.hasMatchFirstVertex ||
		config.hasMatchFirstIndex || config.hasMatchFirstInstance;
}

static void SetCompiledVertexResourceBinding(
	std::vector<DX12CompiledVertexResourceBinding> *bindings,
	uint32_t slot,
	const std::wstring &resource)
{
	if (!bindings)
		return;
	for (DX12CompiledVertexResourceBinding &binding : *bindings) {
		if (binding.slot != slot)
			continue;
		binding.resource = resource;
		return;
	}
	bindings->push_back({ slot, resource });
	std::sort(bindings->begin(), bindings->end(),
		[](const DX12CompiledVertexResourceBinding &lhs,
		   const DX12CompiledVertexResourceBinding &rhs) {
			return lhs.slot < rhs.slot;
		});
}

static std::vector<DX12CompiledVertexResourceBinding> CompileVertexResourceBindings(
	const std::map<uint32_t, std::wstring> &vertexResources)
{
	std::vector<DX12CompiledVertexResourceBinding> bindings;
	bindings.reserve(vertexResources.size());
	for (const auto &item : vertexResources)
		bindings.push_back({ item.first, item.second });
	return bindings;
}

static bool TextureOverrideMatchesIaBinding(
	const Bunny::TextureOverrideConfig &config, bool indexBuffer, uint32_t vertexSlot)
{
	if (config.hash == 0 && TextureOverrideHasDrawContextMatch(config) &&
	    config.indexBufferResource.empty() && config.vertexBufferResources.empty()) {
		if (indexBuffer)
			return !TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::Draw);
		return TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::Draw);
	}

	if (indexBuffer) {
		if (config.indexBufferResource.empty() &&
		    !TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::DrawIndexed))
			return false;
		return true;
	}

	if (config.vertexBufferResources.empty())
		return false;
	if (config.vertexBufferResources.find(vertexSlot) == config.vertexBufferResources.end())
		return false;
	return true;
}

static UINT64 MakeIaVertexIndexKey(uint32_t hash, uint32_t vertexSlot)
{
	return (static_cast<UINT64>(hash) << 32) | vertexSlot;
}

static void BuildDx12IaTextureOverrideIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::unordered_map<std::wstring, bool> &textureOverridePreEffect,
	const std::unordered_map<std::wstring, bool> &textureOverridePostEffect,
	std::vector<DX12IaTextureOverrideCandidate> *iaTextureOverrides,
	std::unordered_map<uint32_t, std::vector<size_t>> *indexTextureOverrideIndex,
	std::unordered_map<UINT64, std::vector<size_t>> *vertexTextureOverrideIndex,
	std::unordered_map<uint32_t, std::vector<size_t>> *anyVertexTextureOverrideIndex,
	std::unordered_map<UINT64, std::vector<size_t>> *mergedVertexTextureOverrideIndex)
{
	if (!iaTextureOverrides || !indexTextureOverrideIndex ||
	    !vertexTextureOverrideIndex || !anyVertexTextureOverrideIndex ||
	    !mergedVertexTextureOverrideIndex)
		return;

	iaTextureOverrides->clear();
	indexTextureOverrideIndex->clear();
	vertexTextureOverrideIndex->clear();
	anyVertexTextureOverrideIndex->clear();
	mergedVertexTextureOverrideIndex->clear();
	UINT64 order = 0;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			DX12IaTextureOverrideCandidate candidate;
			candidate.config = config;
			candidate.order = order++;
			auto sectionId = textureOverrideSectionIds.find(config.section);
			candidate.sectionId =
				sectionId != textureOverrideSectionIds.end() ? sectionId->second : 0;
			candidate.preRuntimeEffect =
				LookupRuntimeEffect(textureOverridePreEffect, config.section);
			candidate.postRuntimeEffect =
				LookupRuntimeEffect(textureOverridePostEffect, config.section);
			const size_t index = iaTextureOverrides->size();
			iaTextureOverrides->push_back(std::move(candidate));

			if (TextureOverrideMatchesIaBinding(config, true, 0))
				(*indexTextureOverrideIndex)[config.hash].push_back(index);

			bool indexedSpecificVertexSlot = false;
			for (const auto &slotResource : config.vertexBufferResources) {
				if (!TextureOverrideMatchesIaBinding(config, false, slotResource.first))
					continue;
				(*vertexTextureOverrideIndex)[
					MakeIaVertexIndexKey(config.hash, slotResource.first)].push_back(index);
				indexedSpecificVertexSlot = true;
			}
			if (!indexedSpecificVertexSlot &&
			    TextureOverrideMatchesIaBinding(config, false, 0)) {
				(*anyVertexTextureOverrideIndex)[config.hash].push_back(index);
			}
		}
	}

	auto sortCandidates = [iaTextureOverrides](std::vector<size_t> *indexes) {
		if (!indexes)
			return;
		std::sort(indexes->begin(), indexes->end(),
			[iaTextureOverrides](size_t lhs, size_t rhs) {
				const DX12IaTextureOverrideCandidate &left = (*iaTextureOverrides)[lhs];
				const DX12IaTextureOverrideCandidate &right = (*iaTextureOverrides)[rhs];
				if (left.config.matchPriority != right.config.matchPriority)
					return left.config.matchPriority > right.config.matchPriority;
				const std::wstring leftSection = Bunny::ToLower(left.config.section);
				const std::wstring rightSection = Bunny::ToLower(right.config.section);
				if (leftSection != rightSection)
					return leftSection < rightSection;
				return left.order < right.order;
			});
	};

	for (auto &bucket : *indexTextureOverrideIndex)
		sortCandidates(&bucket.second);
	for (auto &bucket : *vertexTextureOverrideIndex)
		sortCandidates(&bucket.second);
	for (auto &bucket : *anyVertexTextureOverrideIndex)
		sortCandidates(&bucket.second);

	for (const auto &bucket : *vertexTextureOverrideIndex) {
		std::vector<size_t> merged = bucket.second;
		const uint32_t hash = static_cast<uint32_t>(bucket.first >> 32);
		auto anySlot = anyVertexTextureOverrideIndex->find(hash);
		if (anySlot != anyVertexTextureOverrideIndex->end())
			merged.insert(merged.end(), anySlot->second.begin(), anySlot->second.end());
		sortCandidates(&merged);
		(*mergedVertexTextureOverrideIndex)[bucket.first] = std::move(merged);
	}
}

static const std::vector<size_t> *FindIaTextureOverrideCandidatesLocked(
	uint32_t hash, bool indexBuffer, uint32_t vertexSlot)
{
	if (indexBuffer) {
		auto indexed = gIaIndexTextureOverrideIndex.find(hash);
		return indexed != gIaIndexTextureOverrideIndex.end() ? &indexed->second : nullptr;
	}

	auto merged = gIaMergedVertexTextureOverrideIndex.find(
		MakeIaVertexIndexKey(hash, vertexSlot));
	if (merged != gIaMergedVertexTextureOverrideIndex.end())
		return &merged->second;

	auto anySlot = gIaAnyVertexTextureOverrideIndex.find(hash);
	return anySlot != gIaAnyVertexTextureOverrideIndex.end() ? &anySlot->second : nullptr;
}

static bool AppendActionToCompiledPlan(
	const Bunny::CommandListAction &action,
	DX12CompiledCommandListPlan *plan)
{
	if (!plan)
		return false;

	switch (action.kind) {
	case Bunny::CommandListActionKind::HandlingSkip:
		plan->handlingSkip = true;
		return true;
	case Bunny::CommandListActionKind::SetIndexBuffer:
		plan->indexResource = action.resource;
		return true;
	case Bunny::CommandListActionKind::SetVertexBuffer:
		SetCompiledVertexResourceBinding(
			&plan->vertexResources, action.target.slot, action.resource);
		return true;
	case Bunny::CommandListActionKind::Draw:
	case Bunny::CommandListActionKind::DrawIndexed:
	case Bunny::CommandListActionKind::DrawFromCaller:
	case Bunny::CommandListActionKind::DrawIndexedFromCaller: {
		DX12ModIaReplacement::DrawCall draw;
		draw.indexed = (action.kind == Bunny::CommandListActionKind::DrawIndexed ||
		                action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller);
		draw.fromCaller = (action.kind == Bunny::CommandListActionKind::DrawFromCaller ||
		                   action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller);
		if (!draw.fromCaller) {
			draw.count = action.args[0];
			draw.start = action.args[1];
			draw.baseVertex = draw.indexed ? static_cast<INT>(action.args[2]) : 0;
		}
		plan->draws.push_back(draw);
		return true;
	}
	case Bunny::CommandListActionKind::Dispatch: {
		DX12ModIaReplacement::DispatchCall dispatch;
		dispatch.groupsX = action.args[0];
		dispatch.groupsY = action.args[1];
		dispatch.groupsZ = action.args[2];
		plan->dispatches.push_back(dispatch);
		return true;
	}
	default:
		return false;
	}
}

static void AppendCompiledCommandListPlan(
	DX12CompiledCommandListPlan *dst, const DX12CompiledCommandListPlan &src)
{
	if (!dst)
		return;
	dst->handlingSkip = dst->handlingSkip || src.handlingSkip;
	if (!src.indexResource.empty())
		dst->indexResource = src.indexResource;
	for (const auto &item : src.vertexResources)
		SetCompiledVertexResourceBinding(&dst->vertexResources, item.slot, item.resource);
	dst->draws.insert(dst->draws.end(), src.draws.begin(), src.draws.end());
	dst->dispatches.insert(dst->dispatches.end(), src.dispatches.begin(), src.dispatches.end());
}

static void AppendCompiledCommandListToTexturePlan(
	DX12CompiledTextureOverridePlan *dst, const DX12CompiledCommandListPlan &src)
{
	if (!dst)
		return;
	dst->handlingSkip = dst->handlingSkip || src.handlingSkip;
	if (!src.indexResource.empty())
		dst->indexResource = src.indexResource;
	for (const auto &item : src.vertexResources)
		SetCompiledVertexResourceBinding(&dst->vertexResources, item.slot, item.resource);
	dst->draws.insert(dst->draws.end(), src.draws.begin(), src.draws.end());
	dst->dispatches.insert(dst->dispatches.end(), src.dispatches.begin(), src.dispatches.end());
}

static bool CompileCommandListPlanRecursive(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> *compiledPlans,
	std::unordered_set<std::wstring> *visiting,
	DX12CompiledCommandListPlan *outPlan)
{
	if (!compiledPlans || !visiting || !outPlan)
		return false;

	auto cached = compiledPlans->find(name);
	if (cached != compiledPlans->end()) {
		if (cached->second.unsupported)
			return false;
		*outPlan = cached->second;
		return true;
	}
	if (!visiting->insert(name).second)
		return false;

	auto listIt = commandLists.find(name);
	if (listIt == commandLists.end()) {
		visiting->erase(name);
		return false;
	}

	DX12CompiledCommandListPlan plan;
	for (const Bunny::CommandListAction &action : listIt->second.actions) {
		if (action.kind == Bunny::CommandListActionKind::Run) {
			DX12CompiledCommandListPlan childPlan;
			if (!CompileCommandListPlanRecursive(
				action.commandList, commandLists, compiledPlans, visiting, &childPlan)) {
				plan.unsupported = true;
				break;
			}
			AppendCompiledCommandListPlan(&plan, childPlan);
			continue;
		}
		if (action.kind == Bunny::CommandListActionKind::CheckTextureOverride ||
		    !AppendActionToCompiledPlan(action, &plan)) {
			plan.unsupported = true;
			break;
		}
	}

	visiting->erase(name);
	(*compiledPlans)[name] = plan;
	if (plan.unsupported)
		return false;
	*outPlan = plan;
	return true;
}

static void BuildDx12CompiledCommandListPlans(
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> *compiledPlans)
{
	if (!compiledPlans)
		return;

	compiledPlans->clear();
	for (const auto &item : commandLists) {
		std::unordered_set<std::wstring> visiting;
		DX12CompiledCommandListPlan plan;
		CompileCommandListPlanRecursive(
			item.first, commandLists, compiledPlans, &visiting, &plan);
	}
}

static void BuildDx12CompiledTextureOverridePlans(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> *compiledPlans)
{
	if (!compiledPlans)
		return;

	compiledPlans->clear();
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			auto sectionIdIt = textureOverrideSectionIds.find(config.section);
			if (sectionIdIt == textureOverrideSectionIds.end() || !sectionIdIt->second)
				continue;

			DX12CompiledTextureOverridePlan plan;
			plan.hasActions = !config.actions.empty();
			if (config.actions.empty()) {
				plan.handlingSkip = config.handlingSkip;
				plan.indexResource = config.indexBufferResource;
				plan.vertexResources =
					CompileVertexResourceBindings(config.vertexBufferResources);
			} else {
				for (const Bunny::CommandListAction &action : config.actions) {
					switch (action.kind) {
					case Bunny::CommandListActionKind::Run: {
						auto commandPlan = compiledCommandListPlans.find(action.commandList);
						if (commandPlan == compiledCommandListPlans.end() ||
						    commandPlan->second.unsupported) {
							plan.hasUnsupportedCommandActions = true;
							break;
						}
						AppendCompiledCommandListToTexturePlan(&plan, commandPlan->second);
						break;
					}
					case Bunny::CommandListActionKind::CheckTextureOverride:
						plan.hasUnsupportedCommandActions = true;
						break;
					case Bunny::CommandListActionKind::HandlingSkip:
						plan.handlingSkip = true;
						break;
					case Bunny::CommandListActionKind::SetIndexBuffer:
						plan.indexResource = action.resource;
						break;
					case Bunny::CommandListActionKind::SetVertexBuffer:
						SetCompiledVertexResourceBinding(
							&plan.vertexResources, action.target.slot, action.resource);
						break;
					case Bunny::CommandListActionKind::Draw:
					case Bunny::CommandListActionKind::DrawIndexed:
					case Bunny::CommandListActionKind::DrawFromCaller:
					case Bunny::CommandListActionKind::DrawIndexedFromCaller: {
						DX12ModIaReplacement::DrawCall draw;
						draw.indexed = (action.kind == Bunny::CommandListActionKind::DrawIndexed ||
						                action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller);
						draw.fromCaller = (action.kind == Bunny::CommandListActionKind::DrawFromCaller ||
						                   action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller);
						if (!draw.fromCaller) {
							draw.count = action.args[0];
							draw.start = action.args[1];
							draw.baseVertex = draw.indexed ? static_cast<INT>(action.args[2]) : 0;
						}
						plan.draws.push_back(draw);
						break;
					}
					case Bunny::CommandListActionKind::Dispatch: {
						DX12ModIaReplacement::DispatchCall dispatch;
						dispatch.groupsX = action.args[0];
						dispatch.groupsY = action.args[1];
						dispatch.groupsZ = action.args[2];
						plan.dispatches.push_back(dispatch);
						break;
					}
					}
				}
			}
			(*compiledPlans)[sectionIdIt->second] = std::move(plan);
		}
	}
}

static void LogShaderOverrideCommandListLimited(
	const char *phase, const std::vector<const Bunny::ShaderOverrideConfig*> &configs,
	const DX12ModIaReplacement &replacement)
{
#if defined(_DEBUG)
	if (!DX12DiagnosticsLoggingEnabled())
		return;
	static volatile LONG logCount = 0;
	if (InterlockedIncrement(&logCount) > 512)
		return;

	std::wstring sections;
	for (const Bunny::ShaderOverrideConfig *config : configs) {
		if (!config)
			continue;
		if (!sections.empty())
			sections += L";";
		sections += config->section;
	}

	char drawText[512] = {};
	size_t used = 0;
	for (const DX12ModIaReplacement::DrawCall &draw : replacement.draws) {
		int written = 0;
		if (draw.indexed) {
			written = sprintf_s(drawText + used, sizeof(drawText) - used,
				"%sDI:%u,%u,%d", used ? ";" : "", draw.count, draw.start,
				draw.baseVertex);
		} else {
			written = sprintf_s(drawText + used, sizeof(drawText) - used,
				"%sD:%u,%u", used ? ";" : "", draw.count, draw.start);
		}
		if (written <= 0)
			break;
		used += static_cast<size_t>(written);
		if (used >= sizeof(drawText) - 1)
			break;
	}

	DX12LogDebugJsonFunc("DX12ShaderOverrideCommandList",
		"\"phase\":\"%s\",\"matches\":%zu,\"skip\":%s,\"draws\":%zu,\"drawList\":\"%s\",\"sections\":\"%S\"",
		phase ? phase : "", configs.size(), replacement.skip ? "true" : "false",
		replacement.draws.size(), drawText, sections.c_str());
#else
	(void)phase;
	(void)configs;
	(void)replacement;
#endif
}

static void LogPreSkinningCandidatesLimited(const DX12IaHashState &iaState)
{
#if defined(_DEBUG)
	std::vector<DX12ComputeUavProducer> producers;
	AcquireSRWLockShared(&gPreSkinLock);
	producers = gRecentComputeUavs;
	ReleaseSRWLockShared(&gPreSkinLock);

	if (producers.empty()) {
		DX12LogDebugJsonFunc("DX12PreSkinningCandidate",
			"\"status\":\"none\",\"recentUavs\":0,\"overlapCount\":0,\"vlrLike\":0,\"vbCount\":%zu",
			iaState.vertexBuffers.size());
		return;
	}

	std::vector<DX12VertexLimitRaiseConfig> vlrs;
	AcquireSRWLockShared(&gModLock);
	vlrs = gVertexLimitRaiseConfigs;
	ReleaseSRWLockShared(&gModLock);

	size_t overlapping = 0;
	size_t vlrLike = 0;
	DX12ComputeUavProducer best = {};
	DX12IaBufferHash bestBuffer = {};
	bool hasBest = false;

	for (auto it = producers.rbegin(); it != producers.rend(); ++it) {
		const DX12ComputeUavProducer &producer = *it;
		for (const DX12VertexLimitRaiseConfig &vlr : vlrs) {
			if (ProducerMatchesReplacementTarget(producer, vlr)) {
				vlrLike++;
				break;
			}
		}
		for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
			if (!ProducerMatchesIaBuffer(producer, buffer))
				continue;
			overlapping++;
			if (!hasBest) {
				best = producer;
				bestBuffer = buffer;
				hasBest = true;
			}
		}
	}

	if (hasBest && best.hasDescriptor) {
		const DX12DescriptorSummary &d = best.descriptor;
		DX12LogDebugJsonFunc("DX12PreSkinningCandidate",
			"\"status\":\"matched\",\"recentUavs\":%zu,\"overlapCount\":%zu,\"vlrLike\":%zu,\"vbSlot\":%u,\"vbHash\":\"%08x\",\"producerSerial\":%llu,\"root\":%u,\"range\":%u,\"uRegister\":%u,\"space\":%u,\"resource\":\"%p\",\"resourceWidth\":%llu,\"viewBytes\":%llu,\"viewOffset\":%llu,\"numElements\":%u,\"stride\":%u",
			producers.size(), overlapping, vlrLike, bestBuffer.slot, bestBuffer.hash,
			static_cast<unsigned long long>(best.serial),
			best.rootParameterIndex, best.rangeIndex, best.shaderRegister, best.registerSpace,
			best.descriptor.resource,
			static_cast<unsigned long long>(d.hasResourceDesc ? d.resourceDesc.Width : 0),
			static_cast<unsigned long long>(d.viewSize),
			static_cast<unsigned long long>(d.resourceOffset),
			d.numElements, d.structureByteStride);
		return;
	}

	DX12LogDebugJsonFunc("DX12PreSkinningCandidate",
		"\"status\":\"none\",\"recentUavs\":%zu,\"overlapCount\":%zu,\"vlrLike\":%zu,\"vbCount\":%zu",
		producers.size(), overlapping, vlrLike, iaState.vertexBuffers.size());
#else
	(void)iaState;
#endif
}
