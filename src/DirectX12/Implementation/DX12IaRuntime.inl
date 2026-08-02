static bool NumericMatchSatisfied(const Bunny::NumericMatch &match, uint32_t actual)
{
	if (!match.enabled)
		return true;
	switch (match.op)
	{
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

class DX12TextureOverrideDrawKeyUsage
{
public:
	bool useVertexCount = false;
	bool useIndexCount = false;
	bool useInstanceCount = false;
	bool useFirstVertex = false;
	bool useFirstIndex = false;
	bool useFirstInstance = false;

	void MergeConfig(const Bunny::TextureOverrideConfig &config)
	{
		useVertexCount = useVertexCount || config.hasMatchVertexCount;
		useIndexCount = useIndexCount || config.hasMatchIndexCount;
		useInstanceCount = useInstanceCount || config.hasMatchInstanceCount;
		useFirstVertex = useFirstVertex || config.hasMatchFirstVertex;
		useFirstIndex = useFirstIndex || config.hasMatchFirstIndex;
		useFirstInstance = useFirstInstance || config.hasMatchFirstInstance;
	}
};

static DX12TextureOverrideDrawKeyUsage BuildIaCandidateDrawKeyUsageLocked(
	const std::vector<size_t> *candidateIndexes)
{
	DX12TextureOverrideDrawKeyUsage usage;
	if (!candidateIndexes)
		return usage;
	for (size_t index : *candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const Bunny::TextureOverrideConfig *config = gIaTextureOverrides[index].config;
		if (!config)
			continue;
		usage.MergeConfig(*config);
	}
	return usage;
}

static DX12TextureOverrideDrawKeyUsage BuildTriggeredDrawKeyUsage(
	const std::vector<DX12TriggeredTextureOverride> *candidateTriggers)
{
	DX12TextureOverrideDrawKeyUsage usage;
	if (!candidateTriggers)
		return usage;
	for (const DX12TriggeredTextureOverride &triggered : *candidateTriggers) {
		if (!triggered.config)
			continue;
		usage.MergeConfig(*triggered.config);
	}
	return usage;
}

static void AppendDescriptorTriggeredPrefilterSectionIds(
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

static bool HasPotentialDescriptorTriggeredPrefilterCandidates(
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

static void CollectDescriptorTriggeredPrefilterSectionIds(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount,
	std::vector<uint32_t> *sectionIds)
{
	if (!sectionIds)
		return;
	sectionIds->clear();
	AppendDescriptorTriggeredPrefilterSectionIds(
		snapshot.textureOverridesWithoutDrawContext,
		sectionIds);
	AppendDescriptorTriggeredPrefilterSectionIds(
		snapshot.textureOverridesGenericDrawContext,
		sectionIds);
	if (indexCount) {
		auto indexed = snapshot.textureOverridesByIndexCount.find(indexCount);
		if (indexed != snapshot.textureOverridesByIndexCount.end())
			AppendDescriptorTriggeredPrefilterSectionIds(indexed->second, sectionIds);
	}
	if (vertexCount) {
		auto vertex = snapshot.textureOverridesByVertexCount.find(vertexCount);
		if (vertex != snapshot.textureOverridesByVertexCount.end())
			AppendDescriptorTriggeredPrefilterSectionIds(vertex->second, sectionIds);
	}
	std::sort(sectionIds->begin(), sectionIds->end());
	sectionIds->erase(std::unique(sectionIds->begin(), sectionIds->end()), sectionIds->end());
}

static bool TextureOverrideHasDrawKind(
	const Bunny::TextureOverrideConfig &config, Bunny::CommandListActionKind kind)
{
	for (const Bunny::CommandListAction &action : config.actions)
	{
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
	for (DX12CompiledVertexResourceBinding &binding : *bindings)
	{
		if (binding.slot != slot)
			continue;
		binding.resource = resource;
		return;
	}
	bindings->push_back({slot, resource});
	std::sort(bindings->begin(), bindings->end(),
			  [](const DX12CompiledVertexResourceBinding &lhs,
				 const DX12CompiledVertexResourceBinding &rhs)
			  {
				  return lhs.slot < rhs.slot;
			  });
}

static std::vector<DX12CompiledVertexResourceBinding> CompileVertexResourceBindings(
	const std::map<uint32_t, std::wstring> &vertexResources)
{
	std::vector<DX12CompiledVertexResourceBinding> bindings;
	bindings.reserve(vertexResources.size());
	for (const auto &item : vertexResources)
		bindings.push_back({item.first, item.second});
	return bindings;
}

static void AppendCompiledDrawSrvResourceBindings(
	std::vector<DX12CompiledDrawSrvResourceBinding> *bindings,
	const std::map<uint32_t, std::wstring> &resources,
	bool pixelStage)
{
	if (!bindings)
		return;
	for (const auto &item : resources) {
		DX12CompiledDrawSrvResourceBinding binding;
		binding.slot = item.first;
		binding.resource = item.second;
		binding.pixelStage = pixelStage;
		bindings->push_back(std::move(binding));
	}
}

static bool TextureOverrideMatchesIaBinding(
	const Bunny::TextureOverrideConfig &config, bool indexBuffer, uint32_t vertexSlot)
{
	if (config.hash == 0 && TextureOverrideHasDrawContextMatch(config) &&
		config.indexBufferResource.empty() && config.vertexBufferResources.empty())
	{
		if (indexBuffer)
			return !TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::Draw);
		return TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::Draw);
	}

	if (indexBuffer)
	{
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

static bool NumericMatchIsExactValue(const Bunny::NumericMatch &match)
{
	return match.enabled && match.op == Bunny::NumericMatchOp::Equal;
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
	std::unordered_map<UINT64, std::vector<size_t>> *mergedVertexTextureOverrideIndex,
	std::vector<size_t> *indexTextureOverridePrefilterIndex,
	std::unordered_map<uint32_t, std::vector<size_t>> *indexTextureOverridePrefilterByIndexCount,
	std::unordered_map<uint32_t, std::vector<size_t>> *vertexTextureOverridePrefilterIndex,
	std::vector<size_t> *anyVertexTextureOverridePrefilterIndex,
	std::unordered_map<uint32_t, std::vector<size_t>> *anyVertexTextureOverridePrefilterByVertexCount,
	std::unordered_map<uint32_t, std::vector<size_t>> *mergedVertexTextureOverridePrefilterIndex,
	std::unordered_map<UINT64, std::vector<size_t>> *vertexTextureOverridePrefilterByVertexCount,
	std::unordered_map<UINT64, std::vector<size_t>> *mergedVertexTextureOverridePrefilterByVertexCount)
{
	if (!iaTextureOverrides || !indexTextureOverrideIndex ||
		!vertexTextureOverrideIndex || !anyVertexTextureOverrideIndex ||
		!mergedVertexTextureOverrideIndex ||
		!indexTextureOverridePrefilterIndex ||
		!indexTextureOverridePrefilterByIndexCount ||
		!vertexTextureOverridePrefilterIndex ||
		!anyVertexTextureOverridePrefilterIndex ||
		!anyVertexTextureOverridePrefilterByVertexCount ||
		!mergedVertexTextureOverridePrefilterIndex ||
		!vertexTextureOverridePrefilterByVertexCount ||
		!mergedVertexTextureOverridePrefilterByVertexCount)
		return;

	iaTextureOverrides->clear();
	indexTextureOverrideIndex->clear();
	vertexTextureOverrideIndex->clear();
	anyVertexTextureOverrideIndex->clear();
	mergedVertexTextureOverrideIndex->clear();
	indexTextureOverridePrefilterIndex->clear();
	indexTextureOverridePrefilterByIndexCount->clear();
	vertexTextureOverridePrefilterIndex->clear();
	anyVertexTextureOverridePrefilterIndex->clear();
	anyVertexTextureOverridePrefilterByVertexCount->clear();
	mergedVertexTextureOverridePrefilterIndex->clear();
	vertexTextureOverridePrefilterByVertexCount->clear();
	mergedVertexTextureOverridePrefilterByVertexCount->clear();
	UINT64 order = 0;
	for (const auto &bucket : textureOverrides)
	{
		for (const Bunny::TextureOverrideConfig &config : bucket.second)
		{
			DX12IaTextureOverrideCandidate candidate;
			candidate.config = &config;
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

			if (TextureOverrideMatchesIaBinding(config, true, 0)) {
				(*indexTextureOverrideIndex)[config.hash].push_back(index);
				if (config.hasMatchIndexCount &&
				    NumericMatchIsExactValue(config.matchIndexCount)) {
					(*indexTextureOverridePrefilterByIndexCount)[
						config.matchIndexCount.value].push_back(index);
				} else {
					indexTextureOverridePrefilterIndex->push_back(index);
				}
			}

			bool indexedSpecificVertexSlot = false;
			for (const auto &slotResource : config.vertexBufferResources)
			{
				if (!TextureOverrideMatchesIaBinding(config, false, slotResource.first))
					continue;
				(*vertexTextureOverrideIndex)[MakeIaVertexIndexKey(config.hash, slotResource.first)].push_back(index);
				if (config.hasMatchVertexCount &&
				    NumericMatchIsExactValue(config.matchVertexCount)) {
					(*vertexTextureOverridePrefilterByVertexCount)[
						MakeIaVertexIndexKey(
							config.matchVertexCount.value,
							slotResource.first)].push_back(index);
				} else {
					(*vertexTextureOverridePrefilterIndex)[slotResource.first].push_back(index);
				}
				indexedSpecificVertexSlot = true;
			}
			if (!indexedSpecificVertexSlot &&
				TextureOverrideMatchesIaBinding(config, false, 0))
			{
				(*anyVertexTextureOverrideIndex)[config.hash].push_back(index);
				if (config.hasMatchVertexCount &&
				    NumericMatchIsExactValue(config.matchVertexCount)) {
					(*anyVertexTextureOverridePrefilterByVertexCount)[
						config.matchVertexCount.value].push_back(index);
				} else {
					anyVertexTextureOverridePrefilterIndex->push_back(index);
				}
			}
		}
	}

	auto sortCandidates = [iaTextureOverrides](std::vector<size_t> *indexes)
	{
		if (!indexes)
			return;
		std::sort(indexes->begin(), indexes->end(),
				  [iaTextureOverrides](size_t lhs, size_t rhs)
				  {
					  const DX12IaTextureOverrideCandidate &left = (*iaTextureOverrides)[lhs];
					  const DX12IaTextureOverrideCandidate &right = (*iaTextureOverrides)[rhs];
					  if (!left.config || !right.config)
						  return left.config != nullptr;
					  if (left.config->matchPriority != right.config->matchPriority)
						  return left.config->matchPriority > right.config->matchPriority;
					  const std::wstring leftSection = Bunny::IniDocumentText::ToLower(left.config->section);
					  const std::wstring rightSection = Bunny::IniDocumentText::ToLower(right.config->section);
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
	sortCandidates(indexTextureOverridePrefilterIndex);
	for (auto &bucket : *indexTextureOverridePrefilterByIndexCount)
		sortCandidates(&bucket.second);
	for (auto &bucket : *vertexTextureOverridePrefilterIndex)
		sortCandidates(&bucket.second);
	sortCandidates(anyVertexTextureOverridePrefilterIndex);
	for (auto &bucket : *anyVertexTextureOverridePrefilterByVertexCount)
		sortCandidates(&bucket.second);
	for (auto &bucket : *vertexTextureOverridePrefilterByVertexCount)
		sortCandidates(&bucket.second);

	for (const auto &bucket : *vertexTextureOverrideIndex)
	{
		std::vector<size_t> merged = bucket.second;
		const uint32_t hash = static_cast<uint32_t>(bucket.first >> 32);
		auto anySlot = anyVertexTextureOverrideIndex->find(hash);
		if (anySlot != anyVertexTextureOverrideIndex->end())
			merged.insert(merged.end(), anySlot->second.begin(), anySlot->second.end());
		sortCandidates(&merged);
		(*mergedVertexTextureOverrideIndex)[bucket.first] = std::move(merged);
	}

	for (const auto &bucket : *vertexTextureOverridePrefilterIndex)
	{
		std::vector<size_t> merged = bucket.second;
		if (!anyVertexTextureOverridePrefilterIndex->empty()) {
			merged.insert(
				merged.end(),
				anyVertexTextureOverridePrefilterIndex->begin(),
				anyVertexTextureOverridePrefilterIndex->end());
		}
		sortCandidates(&merged);
		(*mergedVertexTextureOverridePrefilterIndex)[bucket.first] =
			std::move(merged);
	}
	for (const auto &bucket : *vertexTextureOverridePrefilterByVertexCount)
	{
		std::vector<size_t> merged = bucket.second;
		const uint32_t vertexCount = static_cast<uint32_t>(bucket.first >> 32);
		auto anySlot = anyVertexTextureOverridePrefilterByVertexCount->find(vertexCount);
		if (anySlot != anyVertexTextureOverridePrefilterByVertexCount->end()) {
			merged.insert(
				merged.end(),
				anySlot->second.begin(),
				anySlot->second.end());
		}
		sortCandidates(&merged);
		(*mergedVertexTextureOverridePrefilterByVertexCount)[bucket.first] =
			std::move(merged);
	}
}

static const std::vector<size_t> *FindIaTextureOverrideCandidatesLocked(
	uint32_t hash, bool indexBuffer, uint32_t vertexSlot)
{
	if (indexBuffer)
	{
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

static const std::vector<size_t> *FindIaTextureOverridePrefilterCandidatesLocked(
	bool indexBuffer, uint32_t vertexSlot, uint32_t vertexCount, uint32_t indexCount)
{
	if (indexBuffer) {
		auto indexedByCount =
			gIaIndexTextureOverridePrefilterByIndexCount.find(indexCount);
		if (indexedByCount != gIaIndexTextureOverridePrefilterByIndexCount.end())
			return &indexedByCount->second;
		return &gIaIndexTextureOverridePrefilterIndex;
	}

	auto mergedByCount = gIaMergedVertexTextureOverridePrefilterByVertexCount.find(
		MakeIaVertexIndexKey(vertexCount, vertexSlot));
	if (mergedByCount != gIaMergedVertexTextureOverridePrefilterByVertexCount.end())
		return &mergedByCount->second;

	auto anySlotByCount =
		gIaAnyVertexTextureOverridePrefilterByVertexCount.find(vertexCount);
	if (anySlotByCount != gIaAnyVertexTextureOverridePrefilterByVertexCount.end())
		return &anySlotByCount->second;

	auto merged = gIaMergedVertexTextureOverridePrefilterIndex.find(vertexSlot);
	if (merged != gIaMergedVertexTextureOverridePrefilterIndex.end())
		return &merged->second;

	if (!gIaAnyVertexTextureOverridePrefilterIndex.empty())
		return &gIaAnyVertexTextureOverridePrefilterIndex;

	auto specific = gIaVertexTextureOverridePrefilterIndex.find(vertexSlot);
	return specific != gIaVertexTextureOverridePrefilterIndex.end() ?
		&specific->second : nullptr;
}

static bool AppendActionToCompiledPlan(
	const Bunny::CommandListAction &action,
	DX12CompiledCommandListPlan *plan)
{
	if (!plan)
		return false;

	auto targetNeedsDescriptorBindings = [](const Bunny::CommandListTarget &target) {
		return target.kind == Bunny::CommandListTargetKind::ConstantBuffer ||
			target.kind == Bunny::CommandListTargetKind::ShaderResource ||
			target.kind == Bunny::CommandListTargetKind::UnorderedAccessView;
	};
	auto appendUniqueDescriptorCheckTarget = [plan](const Bunny::CommandListTarget &target) {
		for (const Bunny::CommandListTarget &existing : plan->descriptorCheckTargets) {
			if (existing.kind == target.kind &&
				existing.stage == target.stage &&
				existing.slot == target.slot) {
				return;
			}
		}
		plan->descriptorCheckTargets.push_back(target);
		plan->hasDescriptorCheckTargets = true;
	};

	switch (action.kind)
	{
	case Bunny::CommandListActionKind::HandlingSkip:
		plan->handlingSkip = true;
		{
			DX12CompiledCommandListPlan::Op op;
			op.kind = DX12CompiledCommandListPlan::Op::Kind::HandlingSkip;
			plan->ops.push_back(std::move(op));
		}
		return true;
	case Bunny::CommandListActionKind::SetIndexBuffer:
		plan->indexResource = action.resource;
		{
			DX12CompiledCommandListPlan::Op op;
			op.kind = DX12CompiledCommandListPlan::Op::Kind::SetIndexBuffer;
			op.resource = action.resource;
			plan->ops.push_back(std::move(op));
		}
		return true;
	case Bunny::CommandListActionKind::SetVertexBuffer:
		SetCompiledVertexResourceBinding(
			&plan->vertexResources, action.target.slot, action.resource);
		{
			DX12CompiledCommandListPlan::Op op;
			op.kind = DX12CompiledCommandListPlan::Op::Kind::SetVertexBuffer;
			op.target = action.target;
			op.resource = action.resource;
			plan->ops.push_back(std::move(op));
		}
		return true;
	case Bunny::CommandListActionKind::SetShaderResource:
	{
		DX12CompiledCommandListPlan::Op op;
		op.kind = DX12CompiledCommandListPlan::Op::Kind::SetShaderResource;
		op.target = action.target;
		op.resource = action.resource;
		plan->ops.push_back(std::move(op));
		return true;
	}
	case Bunny::CommandListActionKind::SetConstantBuffer:
	{
		DX12CompiledCommandListPlan::Op op;
		op.kind = DX12CompiledCommandListPlan::Op::Kind::SetConstantBuffer;
		op.target = action.target;
		op.resource = action.resource;
		plan->ops.push_back(std::move(op));
		return true;
	}
	case Bunny::CommandListActionKind::SetUnorderedAccessView:
	{
		DX12CompiledCommandListPlan::Op op;
		op.kind = DX12CompiledCommandListPlan::Op::Kind::SetUnorderedAccessView;
		op.target = action.target;
		op.resource = action.resource;
		plan->ops.push_back(std::move(op));
		return true;
	}
	case Bunny::CommandListActionKind::Draw:
	case Bunny::CommandListActionKind::DrawIndexed:
	case Bunny::CommandListActionKind::DrawFromCaller:
	case Bunny::CommandListActionKind::DrawIndexedFromCaller:
	{
		DX12ModIaReplacement::DrawCall draw;
		draw.indexed = (action.kind == Bunny::CommandListActionKind::DrawIndexed ||
						action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller);
		draw.fromCaller = (action.kind == Bunny::CommandListActionKind::DrawFromCaller ||
						   action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller);
		if (!draw.fromCaller)
		{
			draw.count = action.args[0];
			draw.start = action.args[1];
			draw.baseVertex = draw.indexed ? static_cast<INT>(action.args[2]) : 0;
		}
		plan->draws.push_back(draw);
		{
			DX12CompiledCommandListPlan::Op op;
			op.kind = DX12CompiledCommandListPlan::Op::Kind::Draw;
			op.draw = draw;
			plan->ops.push_back(std::move(op));
		}
		return true;
	}
	case Bunny::CommandListActionKind::Dispatch:
	{
		DX12ModIaReplacement::DispatchCall dispatch;
		dispatch.groupsX = action.args[0];
		dispatch.groupsY = action.args[1];
		dispatch.groupsZ = action.args[2];
		plan->dispatches.push_back(dispatch);
		{
			DX12CompiledCommandListPlan::Op op;
			op.kind = DX12CompiledCommandListPlan::Op::Kind::Dispatch;
			op.dispatch = dispatch;
			plan->ops.push_back(std::move(op));
		}
		return true;
	}
	case Bunny::CommandListActionKind::CheckTextureOverride:
	{
		DX12CompiledCommandListPlan::Op op;
		op.kind = DX12CompiledCommandListPlan::Op::Kind::CheckTextureOverride;
		op.target = action.target;
		plan->ops.push_back(std::move(op));
		if (targetNeedsDescriptorBindings(action.target))
			appendUniqueDescriptorCheckTarget(action.target);
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
	dst->hasDescriptorCheckTargets =
		dst->hasDescriptorCheckTargets || src.hasDescriptorCheckTargets;
	if (!src.indexResource.empty())
		dst->indexResource = src.indexResource;
	for (const auto &item : src.vertexResources)
		SetCompiledVertexResourceBinding(&dst->vertexResources, item.slot, item.resource);
	for (const Bunny::CommandListTarget &target : src.descriptorCheckTargets) {
		bool exists = false;
		for (const Bunny::CommandListTarget &existing : dst->descriptorCheckTargets) {
			if (existing.kind == target.kind &&
				existing.stage == target.stage &&
				existing.slot == target.slot) {
				exists = true;
				break;
			}
		}
		if (!exists)
			dst->descriptorCheckTargets.push_back(target);
	}
	dst->draws.insert(dst->draws.end(), src.draws.begin(), src.draws.end());
	dst->dispatches.insert(dst->dispatches.end(), src.dispatches.begin(), src.dispatches.end());
	dst->ops.insert(dst->ops.end(), src.ops.begin(), src.ops.end());
}

static bool AppendCompiledCommandListLinksPlan(
	const std::vector<std::wstring> &links,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	DX12CompiledCommandListPlan *dst)
{
	if (!dst)
		return false;
	bool appended = false;
	for (const std::wstring &link : links) {
		auto commandPlan = compiledCommandListPlans.find(link);
		if (commandPlan == compiledCommandListPlans.end() ||
		    commandPlan->second.unsupported) {
			return false;
		}
		AppendCompiledCommandListPlan(dst, commandPlan->second);
		appended = true;
	}
	return appended;
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

static void AppendLegacyTextureOverrideStateToPlan(
	const Bunny::TextureOverrideConfig &config,
	DX12CompiledCommandListPlan *plan)
{
	if (!plan)
		return;

	if (config.handlingSkip) {
		DX12CompiledCommandListPlan::Op op;
		op.kind = DX12CompiledCommandListPlan::Op::Kind::HandlingSkip;
		plan->handlingSkip = true;
		plan->ops.push_back(std::move(op));
	}

	if (!config.indexBufferResource.empty()) {
		DX12CompiledCommandListPlan::Op op;
		op.kind = DX12CompiledCommandListPlan::Op::Kind::SetIndexBuffer;
		op.resource = config.indexBufferResource;
		plan->indexResource = config.indexBufferResource;
		plan->ops.push_back(std::move(op));
	}

	for (const auto &item : config.vertexBufferResources) {
		DX12CompiledCommandListPlan::Op op;
		op.kind = DX12CompiledCommandListPlan::Op::Kind::SetVertexBuffer;
		op.target.kind = Bunny::CommandListTargetKind::VertexBuffer;
		op.target.slot = item.first;
		op.resource = item.second;
		SetCompiledVertexResourceBinding(&plan->vertexResources, item.first, item.second);
		plan->ops.push_back(std::move(op));
	}
}

static bool AppendCompiledCommandListLinksToTexturePlan(
	const std::vector<std::wstring> &links,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	DX12CompiledTextureOverridePlan *dst)
{
	if (!dst)
		return false;
	bool appended = false;
	for (const std::wstring &link : links) {
		auto commandPlan = compiledCommandListPlans.find(link);
		if (commandPlan == compiledCommandListPlans.end() ||
		    commandPlan->second.unsupported) {
			return false;
		}
		AppendCompiledCommandListToTexturePlan(dst, commandPlan->second);
		appended = true;
	}
	return appended;
}

static void BuildCompiledShaderOverridePlanFromParts(
	bool handlingSkip,
	const Bunny::CommandListLinks &commandLists,
	const std::vector<Bunny::CommandListAction> &actions,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	DX12CompiledShaderOverridePlan *plan)
{
	if (!plan)
		return;
	*plan = DX12CompiledShaderOverridePlan();
	plan->mainPlan.handlingSkip = handlingSkip;
	if (!commandLists.pre.empty() &&
	    !AppendCompiledCommandListLinksPlan(
			commandLists.pre, compiledCommandListPlans, &plan->mainPlan)) {
		plan->unsupported = true;
		return;
	}
	if (!commandLists.main.empty() &&
	    !AppendCompiledCommandListLinksPlan(
			commandLists.main, compiledCommandListPlans, &plan->mainPlan)) {
		plan->unsupported = true;
		return;
	}
	for (const Bunny::CommandListAction &action : actions) {
		if (action.kind == Bunny::CommandListActionKind::Run) {
			auto commandPlan = compiledCommandListPlans.find(action.commandList);
			if (commandPlan == compiledCommandListPlans.end() ||
			    commandPlan->second.unsupported) {
				plan->unsupported = true;
				return;
			}
			AppendCompiledCommandListPlan(&plan->mainPlan, commandPlan->second);
			continue;
		}
		if (!AppendActionToCompiledPlan(action, &plan->mainPlan)) {
			plan->unsupported = true;
			return;
		}
	}
	if (!commandLists.post.empty()) {
		plan->hasPostCommandListPlan = true;
		plan->postCommandLists = commandLists.post;
		if (!AppendCompiledCommandListLinksPlan(
				commandLists.post, compiledCommandListPlans,
				&plan->postCommandListPlan)) {
			plan->unsupported = true;
			return;
		}
	}
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
	if (cached != compiledPlans->end())
	{
		if (cached->second.unsupported)
			return false;
		*outPlan = cached->second;
		return true;
	}
	if (!visiting->insert(name).second)
		return false;

	auto listIt = commandLists.find(name);
	if (listIt == commandLists.end())
	{
		visiting->erase(name);
		return false;
	}

	DX12CompiledCommandListPlan plan;
	for (const Bunny::CommandListAction &action : listIt->second.actions)
	{
		if (action.kind == Bunny::CommandListActionKind::Run)
		{
			DX12CompiledCommandListPlan childPlan;
			if (!CompileCommandListPlanRecursive(
					action.commandList, commandLists, compiledPlans, visiting, &childPlan))
			{
				plan.unsupported = true;
				break;
			}
			AppendCompiledCommandListPlan(&plan, childPlan);
			continue;
		}
		if (!AppendActionToCompiledPlan(action, &plan))
		{
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
	for (const auto &item : commandLists)
	{
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
	for (const auto &bucket : textureOverrides)
	{
		for (const Bunny::TextureOverrideConfig &config : bucket.second)
		{
			auto sectionIdIt = textureOverrideSectionIds.find(config.section);
			if (sectionIdIt == textureOverrideSectionIds.end() || !sectionIdIt->second)
				continue;

			DX12CompiledTextureOverridePlan plan;
			plan.hasActions = !config.actions.empty();
			plan.drawSrvResources.reserve(
				config.drawPsSrvResources.size() +
				config.drawVsSrvResources.size());
			AppendCompiledDrawSrvResourceBindings(
				&plan.drawSrvResources, config.drawPsSrvResources, true);
			AppendCompiledDrawSrvResourceBindings(
				&plan.drawSrvResources, config.drawVsSrvResources, false);
			plan.drawReplaceThisResource = config.drawReplaceThisResource;

			if (!config.commandLists.pre.empty()) {
				plan.hasPreCommandListPlan = true;
				if (!AppendCompiledCommandListLinksPlan(
						config.commandLists.pre,
						compiledCommandListPlans,
						&plan.preCommandListPlan)) {
					plan.hasUnsupportedCommandActions = true;
				}
			}

			const bool hasMainRuntimeWork =
				!config.actions.empty() ||
				!config.commandLists.main.empty();
			if (!hasMainRuntimeWork) {
				plan.handlingSkip = config.handlingSkip;
				plan.indexResource = config.indexBufferResource;
				plan.vertexResources =
					CompileVertexResourceBindings(config.vertexBufferResources);
			} else {
				plan.hasMainActionPlan = true;
				if (config.actions.empty()) {
					AppendLegacyTextureOverrideStateToPlan(config, &plan.mainActionPlan);
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
							AppendCompiledCommandListPlan(
								&plan.mainActionPlan, commandPlan->second);
							break;
						}
						default:
							if (!AppendActionToCompiledPlan(action, &plan.mainActionPlan))
								plan.hasUnsupportedCommandActions = true;
							break;
						}

						if (plan.hasUnsupportedCommandActions)
							break;
					}
				}

				if (!plan.hasUnsupportedCommandActions &&
				    !config.commandLists.main.empty() &&
				    !AppendCompiledCommandListLinksPlan(
						config.commandLists.main,
						compiledCommandListPlans,
						&plan.mainActionPlan)) {
					plan.hasUnsupportedCommandActions = true;
				}

				if (!plan.hasUnsupportedCommandActions) {
					plan.handlingSkip = plan.mainActionPlan.handlingSkip;
					plan.indexResource = plan.mainActionPlan.indexResource;
					plan.vertexResources = plan.mainActionPlan.vertexResources;
					plan.draws = plan.mainActionPlan.draws;
					plan.dispatches = plan.mainActionPlan.dispatches;
				} else {
					plan.hasMainActionPlan = false;
				}
			}

			if (!plan.hasUnsupportedCommandActions &&
			    !config.commandLists.post.empty()) {
				plan.hasPostCommandListPlan = true;
				if (!AppendCompiledCommandListLinksPlan(
						config.commandLists.post,
						compiledCommandListPlans,
						&plan.postCommandListPlan)) {
					plan.hasUnsupportedCommandActions = true;
				}
			}

			(*compiledPlans)[sectionIdIt->second] = std::move(plan);
		}
	}
}

static void BuildDx12CompiledShaderOverridePlans(
	const Bunny::ShaderOverrideMap &shaderOverrides,
	const Bunny::ShaderRegexMap &shaderRegexes,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	std::unordered_map<std::wstring, DX12CompiledShaderOverridePlan> *compiledPlans)
{
	if (!compiledPlans)
		return;
	compiledPlans->clear();
	for (const auto &item : shaderOverrides) {
		DX12CompiledShaderOverridePlan plan;
		BuildCompiledShaderOverridePlanFromParts(
			item.second.handlingSkip,
			item.second.commandLists,
			item.second.actions,
			compiledCommandListPlans,
			&plan);
		(*compiledPlans)[item.second.section] = std::move(plan);
	}
	for (const auto &item : shaderRegexes) {
		DX12CompiledShaderOverridePlan plan;
		BuildCompiledShaderOverridePlanFromParts(
			item.second.handlingSkip,
			item.second.commandLists,
			item.second.actions,
			compiledCommandListPlans,
			&plan);
		(*compiledPlans)[item.second.section] = std::move(plan);
	}
}

static void LogShaderOverrideCommandListLimited(
	const char *phase, const std::vector<const Bunny::ShaderOverrideConfig *> &configs,
	const DX12ModIaReplacement &replacement)
{
#if defined(_DEBUG)
	if (!DX12LogManager::DiagnosticsLoggingEnabled())
		return;
	static volatile LONG logCount = 0;
	if (InterlockedIncrement(&logCount) > 512)
		return;

	std::wstring sections;
	for (const Bunny::ShaderOverrideConfig *config : configs)
	{
		if (!config)
			continue;
		if (!sections.empty())
			sections += L";";
		sections += config->section;
	}

	char drawText[512] = {};
	size_t used = 0;
	for (const DX12ModIaReplacement::DrawCall &draw : replacement.draws)
	{
		int written = 0;
		if (draw.indexed)
		{
			written = sprintf_s(drawText + used, sizeof(drawText) - used,
								"%sDI:%u,%u,%d", used ? ";" : "", draw.count, draw.start,
								draw.baseVertex);
		}
		else
		{
			written = sprintf_s(drawText + used, sizeof(drawText) - used,
								"%sD:%u,%u", used ? ";" : "", draw.count, draw.start);
		}
		if (written <= 0)
			break;
		used += static_cast<size_t>(written);
		if (used >= sizeof(drawText) - 1)
			break;
	}

	DX12LogManager::DebugFields("DX12ShaderOverrideCommandList",
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
	producers.assign(gRecentComputeUavs.begin(), gRecentComputeUavs.end());
	ReleaseSRWLockShared(&gPreSkinLock);

	if (producers.empty())
	{
		DX12LogManager::DebugFields("DX12PreSkinningCandidate",
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

	for (auto it = producers.rbegin(); it != producers.rend(); ++it)
	{
		const DX12ComputeUavProducer &producer = *it;
		for (const DX12VertexLimitRaiseConfig &vlr : vlrs)
		{
			if (ProducerMatchesReplacementTarget(producer, vlr))
			{
				vlrLike++;
				break;
			}
		}
		for (const DX12IaBufferHash &buffer : iaState.vertexBuffers)
		{
			if (!ProducerMatchesIaBuffer(producer, buffer))
				continue;
			overlapping++;
			if (!hasBest)
			{
				best = producer;
				bestBuffer = buffer;
				hasBest = true;
			}
		}
	}

	if (hasBest && best.hasDescriptor)
	{
		const DX12DescriptorSummary &d = best.descriptor;
		DX12LogManager::DebugFields("DX12PreSkinningCandidate",
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

	DX12LogManager::DebugFields("DX12PreSkinningCandidate",
								  "\"status\":\"none\",\"recentUavs\":%zu,\"overlapCount\":%zu,\"vlrLike\":%zu,\"vbCount\":%zu",
								  producers.size(), overlapping, vlrLike, iaState.vertexBuffers.size());
#else
	(void)iaState;
#endif
}

static void ReleaseLoadedResourcesLocked()
{
	for (auto &item : gLoadedResources) {
		DX12RetiredLoadedResource retired;
		retired.resource = item.second;
		retired.retirePresent = DX12RuntimeState::GetPresentCount();
		gRetiredLoadedResources.push_back(retired);
		item.second = DX12LoadedResource();
	}
	gLoadedResources.clear();
}

static void MoveExpiredRetiredLoadedResourcesLocked(
	std::vector<DX12RetiredLoadedResource> *expired)
{
	if (!expired)
		return;
	expired->clear();
	if (InterlockedCompareExchange(&gActiveLoadedResourceSnapshots, 0, 0) != 0)
		return;
	const LONG present = DX12RuntimeState::GetPresentCount();
	auto it = gRetiredLoadedResources.begin();
	while (it != gRetiredLoadedResources.end()) {
		if (present - it->retirePresent < RetiredLoadedResourcePresentDelay) {
			++it;
			continue;
		}
		expired->push_back(*it);
		it = gRetiredLoadedResources.erase(it);
	}
}

static void ReleaseLoadedResourceObjects(DX12LoadedResource *resource)
{
	if (!resource)
		return;
	if (resource->resource) {
		resource->resource->Release();
		resource->resource = nullptr;
	}
	if (resource->uavResource) {
		RetirePreSkinResource(resource->uavResource);
		resource->uavResource = nullptr;
	}
	if (resource->uavHeap) {
		RetirePreSkinDescriptorHeap(resource->uavHeap);
		resource->uavHeap = nullptr;
	}
	if (resource->srvResource) {
		resource->srvResource->Release();
		resource->srvResource = nullptr;
	}
	if (resource->srvHeap) {
		resource->srvHeap->Release();
		resource->srvHeap = nullptr;
	}
	if (resource->drawSrvTextureResource) {
		RetirePreSkinResource(resource->drawSrvTextureResource);
		resource->drawSrvTextureResource = nullptr;
	}
	if (resource->drawSrvUploadResource) {
		RetirePreSkinResource(resource->drawSrvUploadResource);
		resource->drawSrvUploadResource = nullptr;
	}
	if (resource->drawSrvHeap) {
		RetirePreSkinDescriptorHeap(resource->drawSrvHeap);
		resource->drawSrvHeap = nullptr;
	}
	for (std::map<UINT64, ID3D12DescriptorHeap*>::iterator heapIt =
		     resource->drawSrvCompatibleHeaps.begin();
	     heapIt != resource->drawSrvCompatibleHeaps.end(); ++heapIt) {
		if (heapIt->second)
			RetirePreSkinDescriptorHeap(heapIt->second);
	}
	resource->drawSrvCompatibleHeaps.clear();
	resource->drawSrvCompatibleCpus.clear();
	resource->drawSrvCpu = {};
}

static DX12IaHashState EmptyIaHashState();
static bool ApplyPendingResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState,
	const std::vector<DX12PendingResourceViewRequest> &requests,
	DX12ModIaReplacement *replacement);
static bool RunNamedCommandList(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	const std::wstring &name,
	DX12ModIaReplacement *replacement,
	std::vector<DX12PendingResourceViewRequest> *pendingResourceViews);

static bool TextureOverrideMatchCsSatisfied(
	const Bunny::TextureOverrideConfig &config,
	const std::unordered_set<std::wstring> &activePreSkinSections)
{
	if (!config.hasMatchCs && !config.hasCsHash)
		return true;
	return activePreSkinSections.find(config.section) != activePreSkinSections.end();
}

static bool TextureOverrideDependsOnActivePreSkin(
	const Bunny::TextureOverrideConfig &config)
{
	return config.hasMatchCs || config.hasCsHash;
}

static const std::unordered_set<std::wstring> &EmptyActivePreSkinSections()
{
	static const std::unordered_set<std::wstring> empty;
	return empty;
}

static bool IaCandidateIndexesDependOnActivePreSkin(
	const std::vector<size_t> &candidateIndexes)
{
	for (size_t index : candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		if (candidate.config &&
		    TextureOverrideDependsOnActivePreSkin(*candidate.config))
			return true;
	}
	return false;
}

static bool IaTriggeredCandidatesDependOnActivePreSkin(
	const std::vector<DX12TriggeredTextureOverride> &candidateTriggers)
{
	for (const DX12TriggeredTextureOverride &triggered : candidateTriggers) {
		if (triggered.config &&
		    TextureOverrideDependsOnActivePreSkin(*triggered.config))
			return true;
	}
	return false;
}

void DX12ModBeginFrame(IDXGISwapChain *swapChain)
{
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);

	// Frame interval is the actual elapsed time between Present calls.
	static LARGE_INTEGER s_lastFrameTime = {};
	static bool s_hasLastFrame = false;
	static LARGE_INTEGER s_perfFrequency = {};
	if (s_perfFrequency.QuadPart == 0)
		QueryPerformanceFrequency(&s_perfFrequency);
	LONG64 frameIntervalUs = 0;
	if (s_hasLastFrame)
		frameIntervalUs = (t0.QuadPart - s_lastFrameTime.QuadPart) * 1000000 / s_perfFrequency.QuadPart;
	s_lastFrameTime = t0;
	s_hasLastFrame = true;

	DX12VmFrameVariablesUtils::UpdateForSwapChain(swapChain);
	ProcessDx12KeyBindingsForFrame();

	std::vector<DX12RetiredLoadedResource> expired;
	std::vector<ID3D12Resource*> releasePreSkinResources;
	AcquireSRWLockExclusive(&gPreSkinLock);
	// Track idle frames: if no active pre-skin overrides existed last frame, count idle.
	// If pre-skin was actively matching, reset idle counter immediately.
	if (gActivePreSkinTextureOverrides.empty()) {
		LONG idle = InterlockedIncrement(&gPreSkinBindingIdleFrames);
		if (idle > kPreSkinBindingIdleMax)
			InterlockedExchange(&gPreSkinBindingIdleFrames, kPreSkinBindingIdleMax);
	} else {
		InterlockedExchange(&gPreSkinBindingIdleFrames, 0);
	}
	ClearActivePreSkinTextureOverridesLocked(&releasePreSkinResources);
	// PreSkin CBV and SRV caches persist while the mod configuration is stable.
	// DX12ModRuntimeLoader clears them only when the mod is reloaded.
	// Per-frame clearing previously recreated descriptors and CBVs unnecessarily.
	// gPreSkinCbvReadCache.clear();
	// gPreSkinSrvNegativeCache.clear();
	// gPreSkinSrvPositiveCache.clear();
	// The PSO IA negative cache persists while configuration is unchanged.
	// DX12ModRuntimeLoader clears it only on a mod reload.
	// DX12IaReplacementExecutor::ClearPsoIaNegativeCache();
	ReleaseSRWLockExclusive(&gPreSkinLock);

	AcquireSRWLockExclusive(&gModLock);
	// Frame-local UAV state is epoch tagged by the writer. Avoid an O(number of
	// loaded mod resources) clear under gModLock on every Present.
	gPreSkinSectionAppliedPresent.clear();
	MoveExpiredRetiredLoadedResourcesLocked(&expired);
	ReleaseSRWLockExclusive(&gModLock);
	for (DX12RetiredLoadedResource &resource : expired)
		ReleaseLoadedResourceObjects(&resource.resource);
	ReleaseMovedPreSkinResources(&releasePreSkinResources);

	QueryPerformanceCounter(&t1);
	const LONG frameDraws = DX12RuntimeState::TakeFrameDraws();
	const LONG frameDispatches = DX12RuntimeState::TakeFrameDispatches();
	// Throttle the per-frame perf log line: one entry every 120 frames keeps
	// the data usable without paying a log write on every single Present.
	static LONG s_modBeginFrameLogCountdown = 0;
	if (--s_modBeginFrameLogCountdown <= 0) {
		s_modBeginFrameLogCountdown = 120;
		DX12LogManager::Fields("DX12Perf",
			"\"scope\":\"ModBeginFrame\",\"us\":%lld,\"frameIntervalUs\":%lld,\"frameDraws\":%ld,\"frameDispatches\":%ld",
			static_cast<long long>((t1.QuadPart - t0.QuadPart) * 1000000 / s_perfFrequency.QuadPart),
			static_cast<long long>(frameIntervalUs),
			frameDraws,
			frameDispatches);
	}
}

bool DX12ModPreparePresentReplacement(
	ID3D12GraphicsCommandList *commandList, DX12ModIaReplacement *replacement)
{
	if (!commandList || !replacement)
		return false;
	if (!DX12ModNeedsPresentReplacement())
		return false;

	const LONG present = DX12RuntimeState::GetPresentCount();
	for (;;) {
		const LONG observed = InterlockedCompareExchange(
			&gPresentCommandListExecutedPresent, 0, 0);
		if (observed == present)
			return false;
		if (InterlockedCompareExchange(
			    &gPresentCommandListExecutedPresent, present, observed) == observed)
			break;
	}

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device) {
		InterlockedCompareExchange(&gPresentCommandListExecutedPresent, present - 1, present);
		return false;
	}

	std::vector<DX12PendingResourceViewRequest> pendingResourceViews;
	bool changed = RunNamedCommandList(
		device, commandList, L"Present", replacement, &pendingResourceViews);
	DX12IaHashState emptyIa = EmptyIaHashState();
	changed |= ApplyPendingResourceViewsForCommandList(
		device, commandList, emptyIa, pendingResourceViews, replacement);
	device->Release();
	return changed;
}

bool DX12ModPresentReplacementPendingForCurrentPresent()
{
	if (!DX12ModNeedsPresentReplacement())
		return false;
	const LONG present = DX12RuntimeState::GetPresentCount();
	// Plain volatile probe: the value is only meaningful to the recording
	// thread and is published with interlocked ops, so a LOCK CMPXCHG here
	// (once per Draw while a Present mod is active) buys nothing.
	return gPresentCommandListExecutedPresent != present;
}

static DX12TextureOverrideLookupCacheKey MakeIaTextureOverrideLookupCacheKey(
	const Bunny::CommandListTarget &target, uint32_t hash,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	UINT64 preSkinGeneration,
	const DX12TextureOverrideDrawKeyUsage &usage)
{
	DX12TextureOverrideLookupCacheKey key = {};
	key.reloadGeneration = gReloadGeneration;
	key.preSkinGeneration = preSkinGeneration;
	key.targetKind = static_cast<uint32_t>(target.kind);
	key.shaderStage = static_cast<uint32_t>(target.stage);
	key.slot = target.slot;
	key.hash = hash;
	key.vertexCount = usage.useVertexCount ? vertexCount : 0;
	key.indexCount = usage.useIndexCount ? indexCount : 0;
	key.instanceCount = usage.useInstanceCount ? instanceCount : 0;
	key.firstVertex = usage.useFirstVertex ? firstVertex : 0;
	key.firstIndex = usage.useFirstIndex ? firstIndex : 0;
	key.firstInstance = usage.useFirstInstance ? firstInstance : 0;
	return key;
}

static std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
LoadTextureOverrideRuntimeSnapshot()
{
	return std::atomic_load_explicit(
		&gCommandExecutionRuntimeSnapshot,
		std::memory_order_acquire);
}

static const std::vector<DX12TriggeredTextureOverride> *
FindIaTriggeredTextureOverrideCandidates(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t hash, bool indexBuffer, uint32_t vertexSlot)
{
	if (indexBuffer) {
		auto indexed = snapshot.iaIndexTriggeredTextureOverrides.find(hash);
		return indexed != snapshot.iaIndexTriggeredTextureOverrides.end() ?
			&indexed->second : nullptr;
	}

	auto merged = snapshot.iaMergedVertexTriggeredTextureOverrides.find(
		MakeIaVertexIndexKey(hash, vertexSlot));
	return merged != snapshot.iaMergedVertexTriggeredTextureOverrides.end() ?
		&merged->second : nullptr;
}

static void CollectIaTriggeredTextureOverrideCandidates(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t hash,
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount,
	std::vector<DX12TriggeredTextureOverride> *candidateTriggers)
{
	if (!candidateTriggers)
		return;
	candidateTriggers->clear();

	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverridePrefilterCandidatesLocked(
			indexBuffer,
			vertexSlot,
			vertexCount,
			indexCount);
	if (!candidateIndexes || candidateIndexes->empty())
		return;

	candidateTriggers->reserve(candidateIndexes->size());
	for (size_t index : *candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		const Bunny::TextureOverrideConfig *config = candidate.config;
		if (!config || config->hash != hash)
			continue;
		if (!TextureOverrideMatchesIaBinding(*config, indexBuffer, vertexSlot))
			continue;
		auto configIt = snapshot.textureOverridesBySectionId.find(candidate.sectionId);
		if (configIt == snapshot.textureOverridesBySectionId.end() || !configIt->second)
			continue;
		DX12TriggeredTextureOverride triggered;
		triggered.config = configIt->second.get();
		triggered.sectionId = candidate.sectionId;
		triggered.preRuntimeEffect = candidate.preRuntimeEffect;
		triggered.postRuntimeEffect = candidate.postRuntimeEffect;
		triggered.indexBuffer = indexBuffer;
		triggered.vertexSlot = vertexSlot;
		triggered.executeCommands = true;
		triggered.executeDrawActions = true;
		candidateTriggers->push_back(std::move(triggered));
	}
}

static const Bunny::TextureOverrideConfig *ConfigFromIaCandidateIndexLocked(size_t index)
{
	if (index >= gIaTextureOverrides.size())
		return nullptr;
	return gIaTextureOverrides[index].config;
}

static bool TryFindCachedIaTextureOverrideLocked(
	const DX12TextureOverrideLookupCacheKey &key,
	bool *matched,
	std::vector<const Bunny::TextureOverrideConfig*> *configs)
{
	if (matched)
		*matched = false;
	if (configs)
		configs->clear();
	auto cached = gTextureOverrideLookupCache.find(key);
	if (cached == gTextureOverrideLookupCache.end())
		return false;
	if (matched)
		*matched = cached->second.matched;
	if (!cached->second.matched)
		return true;
	if (configs)
		configs->reserve(cached->second.iaCandidateIndexes.size());
	for (size_t index : cached->second.iaCandidateIndexes) {
		const Bunny::TextureOverrideConfig *cachedConfig =
			ConfigFromIaCandidateIndexLocked(index);
		if (!cachedConfig)
			return false;
		if (configs)
			configs->push_back(cachedConfig);
	}
	return true;
}

static void StoreIaTextureOverrideLookupCacheLocked(
	const DX12TextureOverrideLookupCacheKey &key, const std::vector<size_t> &candidateIndexes)
{
	DX12TextureOverrideLookupCacheEntry entry = {};
	entry.matched = !candidateIndexes.empty();
	entry.iaCandidateIndexes = candidateIndexes;
	auto existing = gTextureOverrideLookupCache.find(key);
	if (existing != gTextureOverrideLookupCache.end()) {
		existing->second = std::move(entry);
		return;
	}
	if (gTextureOverrideLookupCache.size() >= MaxTextureOverrideLookupCacheEntries)
		gTextureOverrideLookupCache.erase(gTextureOverrideLookupCache.begin());
	gTextureOverrideLookupCache.emplace(key, std::move(entry));
}

static bool FindIaTextureOverridesCachedLocked(
	const Bunny::CommandListTarget &target, uint32_t hash,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	bool indexBuffer, uint32_t vertexSlot,
	const std::unordered_set<std::wstring> &activePreSkinSections,
	UINT64 preSkinGeneration,
	std::vector<const Bunny::TextureOverrideConfig*> *configs)
{
	if (configs)
		configs->clear();
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverrideCandidatesLocked(hash, indexBuffer, vertexSlot);
	const bool dependsOnActivePreSkin =
		candidateIndexes &&
		IaCandidateIndexesDependOnActivePreSkin(*candidateIndexes);
	const UINT64 effectivePreSkinGeneration =
		dependsOnActivePreSkin ? preSkinGeneration : 0;
	const std::unordered_set<std::wstring> &effectiveActivePreSkinSections =
		dependsOnActivePreSkin ?
			activePreSkinSections :
			EmptyActivePreSkinSections();
	const DX12TextureOverrideDrawKeyUsage keyUsage =
		BuildIaCandidateDrawKeyUsageLocked(candidateIndexes);
	const DX12TextureOverrideLookupCacheKey key =
		MakeIaTextureOverrideLookupCacheKey(
			target, hash, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance,
			effectivePreSkinGeneration,
			keyUsage);
	bool cachedMatched = false;
	if (TryFindCachedIaTextureOverrideLocked(key, &cachedMatched, configs)) {
#if defined(_DEBUG)
		DX12LogManager::DebugFields("DX12IaOverrideLookup",
			"\"stage\":\"cache_hit\",\"target\":\"%s\",\"slot\":%u,\"hash\":\"%08x\",\"found\":%s",
			indexBuffer ? "ib" : "vb", vertexSlot, hash,
			cachedMatched ? "true" : "false");
#endif
		return cachedMatched;
	}
	if (!candidateIndexes || candidateIndexes->empty()) {
#if defined(_DEBUG)
		DX12LogManager::DebugFields("DX12IaOverrideLookup",
			"\"stage\":\"no_candidates\",\"target\":\"%s\",\"slot\":%u,\"hash\":\"%08x\"",
			indexBuffer ? "ib" : "vb", vertexSlot, hash);
#endif
		// Cache the hash as having no candidates until configuration reload.
		const UINT64 hashNoCandidateKey =
			(static_cast<UINT64>(hash)) |
			(indexBuffer ? (1ull << 32) : 0) |
			(static_cast<UINT64>(vertexSlot) << 33);
		gIaTextureOverrideHashNoCandidates.insert(hashNoCandidateKey);
		StoreIaTextureOverrideLookupCacheLocked(key, std::vector<size_t>());
		return false;
	}

	std::vector<size_t> matchedIndexes;
	size_t filteredByMatchCs = 0;
	size_t filteredByIaBinding = 0;
	size_t filteredByDrawContext = 0;
	for (size_t index : *candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		const Bunny::TextureOverrideConfig *config = candidate.config;
		if (!config)
			continue;
		if (!TextureOverrideMatchCsSatisfied(
				*config, effectiveActivePreSkinSections)) {
			filteredByMatchCs++;
			continue;
		}
		if (!TextureOverrideMatchesIaBinding(*config, indexBuffer, vertexSlot)) {
			filteredByIaBinding++;
			continue;
		}
		if (!TextureOverrideMatchesDrawContext(
			*config, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance)) {
			filteredByDrawContext++;
			continue;
		}
		matchedIndexes.push_back(index);
		if (configs)
			configs->push_back(config);
	}

#if defined(_DEBUG)
	DX12LogManager::DebugFields("DX12IaOverrideLookup",
		"\"stage\":\"candidates_filtered\",\"target\":\"%s\",\"slot\":%u,\"hash\":\"%08x\",\"candidates\":%zu,\"matched\":%zu,\"filteredByMatchCs\":%zu,\"filteredByIaBinding\":%zu,\"filteredByDrawContext\":%zu",
		indexBuffer ? "ib" : "vb", vertexSlot, hash,
		candidateIndexes->size(), matchedIndexes.size(),
		filteredByMatchCs, filteredByIaBinding, filteredByDrawContext);
#endif

	StoreIaTextureOverrideLookupCacheLocked(key, matchedIndexes);
	return !matchedIndexes.empty();
}

static bool FindIaTextureOverridesCached(
	const Bunny::CommandListTarget &target, uint32_t hash,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	bool indexBuffer, uint32_t vertexSlot,
	std::vector<const Bunny::TextureOverrideConfig*> *configs)
{
	if (configs)
		configs->clear();

	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadTextureOverrideRuntimeSnapshot();
	if (!snapshot)
		return false;

	static thread_local UINT64 tlsIaTriggeredCacheGeneration = 0;
	static thread_local std::unordered_map<
		DX12TextureOverrideLookupCacheKey,
		std::vector<DX12TriggeredTextureOverride>,
		DX12TextureOverrideLookupCacheKeyHash> tlsIaTriggeredCache;

	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	if (tlsIaTriggeredCacheGeneration != reloadGeneration) {
		tlsIaTriggeredCache.clear();
		tlsIaTriggeredCacheGeneration = reloadGeneration;
	}

	std::vector<DX12TriggeredTextureOverride> collectedCandidateTriggers;
	CollectIaTriggeredTextureOverrideCandidates(
		*snapshot,
		hash,
		indexBuffer,
		vertexSlot,
		vertexCount,
		indexCount,
		&collectedCandidateTriggers);
	const std::vector<DX12TriggeredTextureOverride> *candidateTriggers =
		&collectedCandidateTriggers;
	UINT64 preSkinGeneration = 0;
	const std::unordered_set<std::wstring> &activePreSkinSections =
		(candidateTriggers &&
		 IaTriggeredCandidatesDependOnActivePreSkin(*candidateTriggers)) ?
			GetActivePreSkinSectionsSnapshot(&preSkinGeneration) :
			EmptyActivePreSkinSections();
	const DX12TextureOverrideDrawKeyUsage keyUsage =
		BuildTriggeredDrawKeyUsage(candidateTriggers);
	const DX12TextureOverrideLookupCacheKey key =
		MakeIaTextureOverrideLookupCacheKey(
			target, hash, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, preSkinGeneration,
			keyUsage);

	auto cached = tlsIaTriggeredCache.find(key);
	if (cached != tlsIaTriggeredCache.end()) {
		if (configs) {
			configs->reserve(cached->second.size());
			for (const DX12TriggeredTextureOverride &triggered : cached->second) {
				if (triggered.config)
					configs->push_back(triggered.config);
			}
		}
		return !cached->second.empty();
	}

	if (!candidateTriggers || candidateTriggers->empty()) {
		tlsIaTriggeredCache.emplace(key, std::vector<DX12TriggeredTextureOverride>());
		return false;
	}

	std::vector<DX12TriggeredTextureOverride> matchedTriggers;
	matchedTriggers.reserve(candidateTriggers->size());
	for (const DX12TriggeredTextureOverride &triggered : *candidateTriggers) {
		const Bunny::TextureOverrideConfig *config = triggered.config;
		if (!config)
			continue;
		if (!TextureOverrideMatchCsSatisfied(*config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesDrawContext(
			*config, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance))
			continue;
		matchedTriggers.push_back(triggered);
		if (configs)
			configs->push_back(config);
	}

	if (tlsIaTriggeredCache.size() >= MaxTextureOverrideLookupCacheEntries)
		tlsIaTriggeredCache.clear();
	tlsIaTriggeredCache.emplace(key, matchedTriggers);
	return !matchedTriggers.empty();
}

class DX12IaTargetTextureOverrideCacheKey

{

public:
	UINT64 reloadGeneration = 0;
	UINT64 preSkinGeneration = 0;
	uint32_t targetKind = 0;
	uint32_t slot = 0;
	uint32_t primaryHash = 0;
	UINT64 bufferLocation = 0;
	uint32_t bufferSize = 0;
	uint32_t viewParam = 0;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	uint32_t firstInstance = 0;

	bool operator==(const DX12IaTargetTextureOverrideCacheKey &other) const
	{
		return reloadGeneration == other.reloadGeneration &&
			preSkinGeneration == other.preSkinGeneration &&
			targetKind == other.targetKind &&
			slot == other.slot &&
			primaryHash == other.primaryHash &&
			bufferLocation == other.bufferLocation &&
			bufferSize == other.bufferSize &&
			viewParam == other.viewParam &&
			vertexCount == other.vertexCount &&
			indexCount == other.indexCount &&
			instanceCount == other.instanceCount &&
			firstVertex == other.firstVertex &&
			firstIndex == other.firstIndex &&
			firstInstance == other.firstInstance;
	}
};

class DX12IaTargetTextureOverrideCacheKeyHash

{

public:
	size_t operator()(const DX12IaTargetTextureOverrideCacheKey &key) const
	{
		UINT64 hash = 0x9f4f2726179a98c3ull;
		hash = HashCombine64(hash, key.reloadGeneration);
		hash = HashCombine64(hash, key.preSkinGeneration);
		hash = HashCombine64(hash, key.targetKind);
		hash = HashCombine64(hash, key.slot);
		hash = HashCombine64(hash, key.primaryHash);
		hash = HashCombine64(hash, key.bufferLocation);
		hash = HashCombine64(hash, key.bufferSize);
		hash = HashCombine64(hash, key.viewParam);
		hash = HashCombine64(hash, key.vertexCount);
		hash = HashCombine64(hash, key.indexCount);
		hash = HashCombine64(hash, key.instanceCount);
		hash = HashCombine64(hash, key.firstVertex);
		hash = HashCombine64(hash, key.firstIndex);
		hash = HashCombine64(hash, key.firstInstance);
		return static_cast<size_t>(hash ^ (hash >> 32));
	}
};

static DX12IaTargetTextureOverrideCacheKey MakeIaTargetTextureOverrideCacheKey(
	const Bunny::CommandListTarget &target,
	uint32_t primaryHash,
	UINT64 bufferLocation,
	uint32_t bufferSize,
	uint32_t viewParam,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance)
{
	DX12IaTargetTextureOverrideCacheKey key = {};
	key.reloadGeneration = DX12ModGetReloadGenerationFast();
	key.preSkinGeneration = CurrentPreSkinActiveGeneration();
	key.targetKind = static_cast<uint32_t>(target.kind);
	key.slot = target.slot;
	key.primaryHash = primaryHash;
	key.bufferLocation = bufferLocation;
	key.bufferSize = bufferSize;
	key.viewParam = viewParam;
	key.vertexCount = vertexCount;
	key.indexCount = indexCount;
	key.instanceCount = instanceCount;
	key.firstVertex = firstVertex;
	key.firstIndex = firstIndex;
	key.firstInstance = firstInstance;
	return key;
}

static bool TryLoadIaTargetTextureOverrideCache(
	const DX12IaTargetTextureOverrideCacheKey &key,
	std::vector<DX12TextureOverrideMatch> *matches)
{
	static thread_local UINT64 tlsReloadGeneration = 0;
	static thread_local std::unordered_map<
		DX12IaTargetTextureOverrideCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12IaTargetTextureOverrideCacheKeyHash> tlsCache;

	if (tlsReloadGeneration != key.reloadGeneration) {
		tlsCache.clear();
		tlsReloadGeneration = key.reloadGeneration;
	}

	auto cached = tlsCache.find(key);
	if (cached == tlsCache.end())
		return false;
	if (matches)
		*matches = cached->second;
	return !cached->second.empty();
}

static void StoreIaTargetTextureOverrideCache(
	const DX12IaTargetTextureOverrideCacheKey &key,
	const std::vector<DX12TextureOverrideMatch> &matches)
{
	static thread_local UINT64 tlsReloadGeneration = 0;
	static thread_local std::unordered_map<
		DX12IaTargetTextureOverrideCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12IaTargetTextureOverrideCacheKeyHash> tlsCache;

	if (tlsReloadGeneration != key.reloadGeneration) {
		tlsCache.clear();
		tlsReloadGeneration = key.reloadGeneration;
	}

	if (tlsCache.size() >= MaxTextureOverrideLookupCacheEntries)
		tlsCache.clear();
	tlsCache[key] = matches;
}

static const DX12IaBufferHash *FindIaVertexSlot(const DX12IaHashState &iaState, uint32_t slot)
{
	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		if (buffer.slot == slot)
			return &buffer;
	}
	return nullptr;
}

static bool TargetStageIsCompute(Bunny::CommandListShaderStage stage)
{
	return stage == Bunny::CommandListShaderStage::Compute;
}

static D3D12_DESCRIPTOR_RANGE_TYPE DescriptorRangeTypeForTarget(
	Bunny::CommandListTargetKind kind)
{
	switch (kind) {
	case Bunny::CommandListTargetKind::ConstantBuffer:
		return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	case Bunny::CommandListTargetKind::ShaderResource:
		return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	case Bunny::CommandListTargetKind::UnorderedAccessView:
		return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	default:
		return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	}
}

static bool DescriptorBindingMatchesTarget(
	const DX12CurrentShaderResourceBinding &binding,
	const Bunny::CommandListTarget &target)
{
	if (binding.shaderRegister != target.slot || binding.registerSpace != 0)
		return false;
	if (binding.shaderVisibility == D3D12_SHADER_VISIBILITY_ALL)
		return true;
	if (target.stage == Bunny::CommandListShaderStage::Vertex)
		return binding.shaderVisibility == D3D12_SHADER_VISIBILITY_VERTEX;
	if (target.stage == Bunny::CommandListShaderStage::Pixel)
		return binding.shaderVisibility == D3D12_SHADER_VISIBILITY_PIXEL;
	if (target.stage == Bunny::CommandListShaderStage::Compute)
		return true;
	return false;
}

static uint32_t HashDescriptorBinding(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Device *cachedDevice,
	const DX12CurrentShaderResourceBinding &binding)
{
	if (binding.hasDescriptor) {
		// Content hash cached at descriptor write time (Create*View). The
		// fallback recompute costs device calls (CheckFeatureSupport /
		// GetCopyableFootprints) per binding per draw for texture views;
		// records without a cached hash (CBVs, untracked resources) still
		// take the fallback, so a missed cache is always safe.
		if (binding.descriptor.huntHash)
			return binding.descriptor.huntHash;
		ID3D12Device *device = AcquireModDeviceFast(commandList, cachedDevice);
		const bool deviceFromCache = (cachedDevice != nullptr);
		if (!device)
			return 0;
		const uint32_t hash =
			DX12TextureHashUtils::DescriptorHuntHash(device, binding);
		if (!deviceFromCache)
			device->Release();
		return hash;
	}
	if (binding.rootDescriptor && binding.gpuVirtualAddress) {
		DX12BufferResourceSummary summary = {};
		const UINT64 probeSize = 1;
		if (DX12ResolveBufferResourceByGpuVa(binding.gpuVirtualAddress, probeSize, &summary)) {
			const UINT64 size = summary.hasResourceDesc ?
				summary.resourceDesc.Width - summary.resourceOffset : summary.viewSize;
			return DX12HashBufferResourceView(
				&summary, binding.gpuVirtualAddress, size ? size : probeSize);
		}
		return DX12HashBufferResourceView(nullptr, binding.gpuVirtualAddress, probeSize);
	}
	return 0;
}

static uint32_t HashDescriptorBindingResource(const DX12CurrentShaderResourceBinding &binding)
{
	if (!binding.hasDescriptor || !binding.descriptor.hasResourceDesc)
		return 0;
	if (binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
		uint32_t hash = 0;
		if (DX12TryGetTextureHash(binding.descriptor.resource, &hash))
			return hash;
		DX12LogManager::DebugFields("DX12TextureHashCache",
			"\"stage\":\"runtime_hash_miss\",\"resource\":\"%p\",\"dimension\":%u,\"format\":%u,\"width\":%llu,\"height\":%u",
			binding.descriptor.resource,
			static_cast<UINT>(binding.descriptor.resourceDesc.Dimension),
			static_cast<UINT>(binding.descriptor.resourceDesc.Format),
			static_cast<unsigned long long>(binding.descriptor.resourceDesc.Width),
			binding.descriptor.resourceDesc.Height);
	}
	return 0;
}

class DX12DescriptorTextureOverrideCandidateCacheKey
{
public:
	UINT64 reloadGeneration = 0;
	const void *commandListIdentity = nullptr;
	uint32_t targetKind = 0;
	uint32_t shaderStage = 0;
	uint32_t slot = 0;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	UINT64 descriptorQuerySerial = 0;
	ID3D12DescriptorHeap *descriptorHeap = nullptr;
	UINT64 descriptorHeapVersion = 0;
	UINT64 textureHashGeneration = 0;

	bool operator==(const DX12DescriptorTextureOverrideCandidateCacheKey &other) const
	{
		return reloadGeneration == other.reloadGeneration &&
			commandListIdentity == other.commandListIdentity &&
			targetKind == other.targetKind &&
			shaderStage == other.shaderStage &&
			slot == other.slot &&
			vertexCount == other.vertexCount &&
			indexCount == other.indexCount &&
			descriptorQuerySerial == other.descriptorQuerySerial &&
			descriptorHeap == other.descriptorHeap &&
			descriptorHeapVersion == other.descriptorHeapVersion &&
			textureHashGeneration == other.textureHashGeneration;
	}
};

class DX12DescriptorTextureOverrideCandidateCacheKeyHash
{
public:
	size_t operator()(const DX12DescriptorTextureOverrideCandidateCacheKey &key) const
	{
		UINT64 hash = 0x7fb5d329728ea185ull;
		hash = HashCombine64(hash, key.reloadGeneration);
		hash = HashCombine64(hash, reinterpret_cast<uintptr_t>(key.commandListIdentity));
		hash = HashCombine64(hash, key.targetKind);
		hash = HashCombine64(hash, key.shaderStage);
		hash = HashCombine64(hash, key.slot);
		hash = HashCombine64(hash, key.vertexCount);
		hash = HashCombine64(hash, key.indexCount);
		hash = HashCombine64(hash, key.descriptorQuerySerial);
		hash = HashCombine64(hash, reinterpret_cast<uintptr_t>(key.descriptorHeap));
		hash = HashCombine64(hash, key.descriptorHeapVersion);
		hash = HashCombine64(hash, key.textureHashGeneration);
		return static_cast<size_t>(hash ^ (hash >> 32));
	}
};

static bool BuildDescriptorTextureOverrideCandidateCacheKey(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	const Bunny::CommandListTarget &target,
	bool compute,
	UINT64 reloadGeneration,
	uint32_t vertexCount,
	uint32_t indexCount,
	DX12DescriptorTextureOverrideCandidateCacheKey *key)
{
	if (!key)
		return false;
	*key = DX12DescriptorTextureOverrideCandidateCacheKey();
	key->reloadGeneration = reloadGeneration;
	key->commandListIdentity =
		runtimeState && runtimeState->commandListIdentity ?
			runtimeState->commandListIdentity :
			commandList;
	key->targetKind = static_cast<uint32_t>(target.kind);
	key->shaderStage = static_cast<uint32_t>(target.stage);
	key->slot = target.slot;
	key->vertexCount = vertexCount;
	key->indexCount = indexCount;
	if (!runtimeState)
		return false;
	key->descriptorQuerySerial = compute ?
		(runtimeState->computeDescriptorQuerySerial ?
			runtimeState->computeDescriptorQuerySerial : 1) :
		(runtimeState->graphicsDescriptorQuerySerial ?
			runtimeState->graphicsDescriptorQuerySerial : 1);
	key->descriptorHeap = runtimeState->cbvSrvUavHeap;
	key->descriptorHeapVersion =
		DX12GetDescriptorHeapVersion(runtimeState->cbvSrvUavHeap);
	key->textureHashGeneration = DX12GetTextureHashGeneration();
	return true;
}

static void AppendTextureOverrideCandidateMatchesByHash(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t hash,
	uint32_t resourceHash,
	uint32_t descriptorHash,
	const DX12CurrentShaderResourceBinding *binding,
	bool compute,
	const std::vector<uint32_t> *allowedSectionIds,
	std::vector<DX12TextureOverrideMatch> *matches)
{
	if (!hash || !matches)
		return;
	auto bucket = snapshot.textureOverridesByHash.find(hash);
	if (bucket == snapshot.textureOverridesByHash.end())
		return;

	for (const DX12TriggeredTextureOverride &triggered : bucket->second) {
		const Bunny::TextureOverrideConfig *config = triggered.config;
		if (!config)
			continue;
		if (allowedSectionIds && triggered.sectionId &&
		    !std::binary_search(
				allowedSectionIds->begin(),
				allowedSectionIds->end(),
				triggered.sectionId))
			continue;
		DX12TextureOverrideMatch match;
		match.config = config;
		match.preRuntimeEffect = triggered.preRuntimeEffect;
		match.postRuntimeEffect = triggered.postRuntimeEffect;
		match.matchedHash = hash;
		match.resourceHash = resourceHash;
		match.descriptorHash = descriptorHash;
		if (binding) {
			match.hasThisSrvTarget =
				binding->hasDescriptor &&
				binding->descriptor.kind == DescriptorKind::SRV &&
				!binding->rootDescriptor;
			match.compute = compute;
			match.thisSrvBinding = *binding;
		}
		matches->push_back(match);
	}
}

static bool FilterDescriptorTextureOverrideCandidates(
	const std::vector<DX12TextureOverrideMatch> &candidates,
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
	if (candidates.empty())
		return false;

	const std::unordered_set<std::wstring> &activePreSkinSections =
		GetActivePreSkinSectionsSnapshot();
	bool found = false;
	for (const DX12TextureOverrideMatch &candidate : candidates) {
		const Bunny::TextureOverrideConfig *config = candidate.config;
		if (!config)
			continue;
		if (!TextureOverrideMatchCsSatisfied(*config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesDrawContext(
				*config,
				vertexCount,
				indexCount,
				instanceCount,
				firstVertex,
				firstIndex,
				firstInstance))
			continue;
		if (matches)
			matches->push_back(candidate);
		found = true;
	}
	return found;
}

static bool FindTextureOverridesByHashLocked(
	uint32_t hash, uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	const std::unordered_set<std::wstring> &activePreSkinSections,
	std::vector<const Bunny::TextureOverrideConfig*> *configs)
{
	if (configs)
		configs->clear();
	if (!hash)
		return false;

	auto bucket = gTextureOverrides.find(hash);
	if (bucket == gTextureOverrides.end())
		return false;

	for (const Bunny::TextureOverrideConfig &config : bucket->second) {
		if (!TextureOverrideMatchCsSatisfied(config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesDrawContext(
			config, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance))
			continue;
		if (configs)
			configs->push_back(&config);
	}
	return configs && !configs->empty();
}

static bool FindTextureOverridesByHash(
	uint32_t hash, uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	std::vector<const Bunny::TextureOverrideConfig*> *configs)
{
	if (configs)
		configs->clear();
	if (!hash)
		return false;
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadTextureOverrideRuntimeSnapshot();
	if (!snapshot)
		return false;
	const std::unordered_set<std::wstring> &activePreSkinSections =
		GetActivePreSkinSectionsSnapshot();
	auto bucket = snapshot->textureOverridesByHash.find(hash);
	if (bucket == snapshot->textureOverridesByHash.end())
		return false;

	for (const DX12TriggeredTextureOverride &triggered : bucket->second) {
		const Bunny::TextureOverrideConfig *config = triggered.config;
		if (!config)
			continue;
		if (!TextureOverrideMatchCsSatisfied(*config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesDrawContext(
			*config, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance))
			continue;
		if (configs)
			configs->push_back(config);
	}
	return configs && !configs->empty();
}

static bool HasPotentialIaTextureOverrideCandidateLocked(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance)
{
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverridePrefilterCandidatesLocked(
			indexBuffer, vertexSlot, vertexCount, indexCount);
	if (!candidateIndexes || candidateIndexes->empty())
		return false;

	const bool dependsOnActivePreSkin =
		IaCandidateIndexesDependOnActivePreSkin(*candidateIndexes);
	const std::unordered_set<std::wstring> &activePreSkinSections =
		dependsOnActivePreSkin ?
			GetActivePreSkinSectionsSnapshot() :
			EmptyActivePreSkinSections();

	for (size_t index : *candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		const Bunny::TextureOverrideConfig *config = candidate.config;
		if (!config)
			continue;
		if (!TextureOverrideMatchCsSatisfied(*config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesIaBinding(*config, indexBuffer, vertexSlot))
			continue;
		if (!TextureOverrideMatchesDrawContext(
				*config,
				vertexCount,
				indexCount,
				instanceCount,
				firstVertex,
				firstIndex,
				firstInstance))
			continue;
		return true;
	}
	return false;
}

static bool HasPotentialHashedIaTextureOverrideCandidateLocked(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount)
{
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverridePrefilterCandidatesLocked(
			indexBuffer,
			vertexSlot,
			vertexCount,
			indexCount);
	if (!candidateIndexes || candidateIndexes->empty())
		return false;

	const bool dependsOnActivePreSkin =
		IaCandidateIndexesDependOnActivePreSkin(*candidateIndexes);
	const std::unordered_set<std::wstring> &activePreSkinSections =
		dependsOnActivePreSkin ?
			GetActivePreSkinSectionsSnapshot() :
			EmptyActivePreSkinSections();

	for (size_t index : *candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		const Bunny::TextureOverrideConfig *config = candidate.config;
		if (!config || !config->hash)
			continue;
		if (!TextureOverrideMatchCsSatisfied(*config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesIaBinding(*config, indexBuffer, vertexSlot))
			continue;
		return true;
	}

	return false;
}

bool DX12ModHasPotentialIaTextureOverrideTargetByCount(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount)
{
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverridePrefilterCandidatesLocked(
			indexBuffer,
			vertexSlot,
			vertexCount,
			indexCount);
	if (!candidateIndexes || candidateIndexes->empty())
		return false;

	const bool dependsOnActivePreSkin =
		IaCandidateIndexesDependOnActivePreSkin(*candidateIndexes);
	const std::unordered_set<std::wstring> &activePreSkinSections =
		dependsOnActivePreSkin ?
			GetActivePreSkinSectionsSnapshot() :
			EmptyActivePreSkinSections();

	for (size_t index : *candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		const Bunny::TextureOverrideConfig *config = candidate.config;
		if (!config)
			continue;
		if (!TextureOverrideMatchCsSatisfied(*config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesIaBinding(*config, indexBuffer, vertexSlot))
			continue;
		return true;
	}

	return false;
}

bool DX12ModHasPotentialHashedIaTextureOverrideTargetByCount(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount)
{
	return HasPotentialHashedIaTextureOverrideCandidateLocked(
		indexBuffer,
		vertexSlot,
		vertexCount,
		indexCount);
}

static void AppendTextureOverrideMatchesByHash(
	uint32_t hash,
	uint32_t resourceHash,
	uint32_t descriptorHash,
	const DX12CurrentShaderResourceBinding *binding,
	bool compute,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches)
{
	if (!hash || !matches)
		return;
	std::vector<const Bunny::TextureOverrideConfig*> configs;
	if (!FindTextureOverridesByHash(
		    hash, vertexCount, indexCount, instanceCount,
		    firstVertex, firstIndex, firstInstance, &configs))
		return;
	for (const Bunny::TextureOverrideConfig *config : configs) {
		if (!config)
			continue;
		DX12TextureOverrideMatch match;
		match.config = config;
		match.preRuntimeEffect =
			LookupRuntimeEffect(gTextureOverridePreRuntimeEffect, config->section);
		match.postRuntimeEffect =
			LookupRuntimeEffect(gTextureOverridePostRuntimeEffect, config->section);
		match.matchedHash = hash;
		match.resourceHash = resourceHash;
		match.descriptorHash = descriptorHash;
		if (binding) {
			match.hasThisSrvTarget =
				binding->hasDescriptor &&
				binding->descriptor.kind == DescriptorKind::SRV &&
				!binding->rootDescriptor;
			match.compute = compute;
			match.thisSrvBinding = *binding;
		}
		matches->push_back(match);
	}
}

static bool FindDescriptorTextureOverrides(
	ID3D12GraphicsCommandList *commandList,
	const Bunny::CommandListTarget &target,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches,
	const std::vector<DX12CurrentShaderResourceBinding> *preResolvedBindings = nullptr)
{
	if (matches)
		matches->clear();
	if (!commandList)
		return false;

	DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtrFast(commandList);
	ID3D12Device *cachedDevice = runtimeState ? runtimeState->device : nullptr;
	const bool compute = TargetStageIsCompute(target.stage);

	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	static thread_local UINT64 tlsDescriptorCandidateCacheGeneration = 0;
	static thread_local std::unordered_map<
		DX12DescriptorTextureOverrideCandidateCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12DescriptorTextureOverrideCandidateCacheKeyHash> tlsDescriptorCandidateCache;
	if (tlsDescriptorCandidateCacheGeneration != reloadGeneration) {
		tlsDescriptorCandidateCache.clear();
		tlsDescriptorCandidateCacheGeneration = reloadGeneration;
	}

	DX12DescriptorTextureOverrideCandidateCacheKey cacheKey = {};
	const bool canUseSerialCache =
		BuildDescriptorTextureOverrideCandidateCacheKey(
			commandList,
			runtimeState,
			target,
			compute,
			reloadGeneration,
			vertexCount,
			indexCount,
			&cacheKey);
	if (canUseSerialCache) {
		auto cachedCandidates = tlsDescriptorCandidateCache.find(cacheKey);
		if (cachedCandidates != tlsDescriptorCandidateCache.end()) {
			return FilterDescriptorTextureOverrideCandidates(
				cachedCandidates->second,
				vertexCount,
				indexCount,
				instanceCount,
				firstVertex,
				firstIndex,
				firstInstance,
				matches);
		}
	}

	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadTextureOverrideRuntimeSnapshot();
	if (!snapshot)
		return false;
	if (!HasPotentialDescriptorTriggeredPrefilterCandidates(
			*snapshot,
			vertexCount,
			indexCount)) {
		if (canUseSerialCache) {
			if (tlsDescriptorCandidateCache.size() >= MaxTextureOverrideLookupCacheEntries)
				tlsDescriptorCandidateCache.clear();
			tlsDescriptorCandidateCache.emplace(cacheKey, std::vector<DX12TextureOverrideMatch>());
		}
		return false;
	}
	static thread_local std::vector<uint32_t> tlsAllowedSectionIds;
	CollectDescriptorTriggeredPrefilterSectionIds(
		*snapshot,
		vertexCount,
		indexCount,
		&tlsAllowedSectionIds);
	if (tlsAllowedSectionIds.empty()) {
		if (canUseSerialCache) {
			if (tlsDescriptorCandidateCache.size() >= MaxTextureOverrideLookupCacheEntries)
				tlsDescriptorCandidateCache.clear();
			tlsDescriptorCandidateCache.emplace(cacheKey, std::vector<DX12TextureOverrideMatch>());
		}
		return false;
	}

	// DX11 consumes "current resources" at draw time rather than re-querying
	// per target. When the caller already has the current descriptor state for
	// this stage/range bucket, reuse it across all descriptor-trigger checks in
	// this draw.
	const bool canUsePreResolvedBindings = preResolvedBindings != nullptr;
	std::vector<DX12CurrentShaderResourceBinding> bindings;
	const std::vector<DX12CurrentShaderResourceBinding> *bindingsView =
		canUsePreResolvedBindings ? preResolvedBindings : &bindings;
	if (!canUsePreResolvedBindings) {
		if (!DX12BindingTracker::GetCurrentShaderResourceBindingsByRegister(
			    commandList,
			    compute,
			    DescriptorRangeTypeForTarget(target.kind),
			    target.slot,
			    0,
			    &bindings)) {
			if (canUseSerialCache) {
				if (tlsDescriptorCandidateCache.size() >= MaxTextureOverrideLookupCacheEntries)
					tlsDescriptorCandidateCache.clear();
				tlsDescriptorCandidateCache.emplace(cacheKey, std::vector<DX12TextureOverrideMatch>());
			}
			return false;
		}
	}
	if (!bindingsView || bindingsView->empty()) {
		if (canUseSerialCache) {
			if (tlsDescriptorCandidateCache.size() >= MaxTextureOverrideLookupCacheEntries)
				tlsDescriptorCandidateCache.clear();
			tlsDescriptorCandidateCache.emplace(cacheKey, std::vector<DX12TextureOverrideMatch>());
		}
		return false;
	}

	std::vector<DX12TextureOverrideMatch> candidates;
	std::vector<uint32_t> resourceHashes(bindingsView->size(), 0);
	for (size_t index = 0; index < bindingsView->size(); ++index) {
		const DX12CurrentShaderResourceBinding &binding = (*bindingsView)[index];
		if (!DescriptorBindingMatchesTarget(binding, target))
			continue;
		const uint32_t resourceHash =
			resourceHashes[index] ? resourceHashes[index] :
			(resourceHashes[index] = HashDescriptorBindingResource(binding));
		const uint32_t descriptorHash =
			HashDescriptorBinding(commandList, cachedDevice, binding);
#if defined(_DEBUG)
		DX12LogManager::Fields("DX12DrawSrvReplacement",
			"stage=this_candidate,targetSlot=%u,bindSpace=%s,slot=%u,root=%u,offset=%u,resourceHash=%08x,descriptorHash=%08x,resource=0x%p,dimension=%u,resourceFormat=%u,viewFormat=%u,width=%llu,height=%u",
			target.slot,
			compute ? "compute" : "graphics",
			binding.shaderRegister,
			binding.rootParameterIndex,
			binding.descriptorOffset,
			resourceHash,
			descriptorHash,
			binding.descriptor.resource,
			binding.descriptor.hasResourceDesc ? static_cast<UINT>(binding.descriptor.resourceDesc.Dimension) : 0,
			binding.descriptor.hasResourceDesc ? static_cast<UINT>(binding.descriptor.resourceDesc.Format) : 0,
			binding.descriptor.viewFormat,
			binding.descriptor.hasResourceDesc ? static_cast<unsigned long long>(binding.descriptor.resourceDesc.Width) : 0ull,
			binding.descriptor.hasResourceDesc ? binding.descriptor.resourceDesc.Height : 0);
#endif
		AppendTextureOverrideCandidateMatchesByHash(
			*snapshot,
			resourceHash,
			resourceHash,
			descriptorHash,
			&binding,
			compute,
			&tlsAllowedSectionIds,
			&candidates);
		if (descriptorHash && descriptorHash != resourceHash) {
			AppendTextureOverrideCandidateMatchesByHash(
				*snapshot,
				descriptorHash,
				resourceHash,
				descriptorHash,
				&binding,
				compute,
				&tlsAllowedSectionIds,
				&candidates);
		}
	}

	if (canUseSerialCache) {
		if (tlsDescriptorCandidateCache.size() >= MaxTextureOverrideLookupCacheEntries)
			tlsDescriptorCandidateCache.clear();
		tlsDescriptorCandidateCache.emplace(cacheKey, candidates);
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

class DX12IaResourceViewSnapshot

{

public:
	ID3D12Resource *resource = nullptr;
	DX12LoadedResource metadata;
	bool uavBarrierPending = false;
	bool transitionToVertex = false;
	D3D12_RESOURCE_STATES stateBefore = D3D12_RESOURCE_STATE_COMMON;
	bool retained = false;
};

static void ReleaseIaResourceViewSnapshot(DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot || !snapshot->resource)
		return;
	if (snapshot->retained)
		snapshot->resource->Release();
	snapshot->resource = nullptr;
	snapshot->retained = false;
}

static void AddRefIaResourceViewSnapshot(DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot || !snapshot->resource || snapshot->retained)
		return;
	snapshot->resource->AddRef();
	snapshot->retained = true;
}

class DX12LoadedResourceSnapshotReadScope

{

public:
	DX12LoadedResourceSnapshotReadScope()
	{
		InterlockedIncrement(&gActiveLoadedResourceSnapshots);
	}

	~DX12LoadedResourceSnapshotReadScope()
	{
		InterlockedDecrement(&gActiveLoadedResourceSnapshots);
	}
};

static void SnapshotIaResourceViewLocked(
	DX12LoadedResource *resource, DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot)
		return;
	ReleaseIaResourceViewSnapshot(snapshot);
	if (!resource)
		return;
	if (resource->uavResource && resource->uavValid &&
		resource->uavValidPresent == DX12RuntimeState::GetPresentCount()) {
		snapshot->resource = resource->uavResource;
		snapshot->metadata = *resource;
		snapshot->uavBarrierPending = resource->uavBarrierPending;
		if (resource->uavState != D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) {
			snapshot->transitionToVertex = true;
			snapshot->stateBefore = resource->uavState;
			resource->uavState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		}
		resource->uavBarrierPending = false;
		return;
	}
	if (resource->srvResource && resource->srvByteWidth > resource->byteWidth) {
		snapshot->resource = resource->srvResource;
		snapshot->metadata = *resource;
		return;
	}
	if (resource->resource) {
		snapshot->resource = resource->resource;
		snapshot->metadata = *resource;
	}
}

static UINT IaViewByteWidthForResource(const DX12LoadedResource &resource, ID3D12Resource *iaResource)
{
	UINT64 byteWidth = resource.byteWidth;
	if (resource.uavResource && iaResource == resource.uavResource && resource.uavByteWidth)
		byteWidth = resource.uavByteWidth;
	else if (resource.srvResource && iaResource == resource.srvResource && resource.srvByteWidth)
		byteWidth = resource.srvByteWidth;
	return static_cast<UINT>((std::min)(byteWidth, static_cast<UINT64>(UINT_MAX)));
}
static void LogIaResourceBindLimited(
	const char *target, uint32_t slot, const DX12LoadedResource &resource,
	ID3D12Resource *iaResource)
{
#if defined(_DEBUG)
	const bool usingUav = resource.uavResource && iaResource == resource.uavResource;
	const bool usingSrv = resource.srvResource && iaResource == resource.srvResource;
	DX12LogManager::DebugFields("DX12IaResourceBind",
		"\"target\":\"%s\",\"slot\":%u,\"resource\":\"%S\",\"source\":\"%s\",\"bytes\":%llu,\"viewBytes\":%u,\"uavBytes\":%llu,\"stride\":%u,\"gpuVa\":\"0x%llx\"",
		target ? target : "", slot, resource.name.c_str(),
		usingUav ? "uav" : (usingSrv ? "srv" : "upload"),
		static_cast<unsigned long long>(resource.byteWidth),
		IaViewByteWidthForResource(resource, iaResource),
		static_cast<unsigned long long>(resource.uavByteWidth),
		resource.stride,
		iaResource ? static_cast<unsigned long long>(iaResource->GetGPUVirtualAddress()) : 0ull);
#else
	(void)target;
	(void)slot;
	(void)resource;
	(void)iaResource;
#endif
}

static void ApplyIaResourceBarriers(
	ID3D12GraphicsCommandList *commandList, const DX12IaResourceViewSnapshot &snapshot)
{
	if (!commandList || !snapshot.resource)
		return;
	if (snapshot.uavBarrierPending) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = snapshot.resource;
		commandList->ResourceBarrier(1, &barrier);
	}
	if (snapshot.transitionToVertex) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = snapshot.resource;
		barrier.Transition.StateBefore = snapshot.stateBefore;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1, &barrier);
	}
}

static bool AppendResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList, const DX12IaHashState &iaState,
	const std::wstring &indexResource,
	const std::vector<DX12CompiledVertexResourceBinding> &vertexResources,
	DX12ModIaReplacement *replacement)
{
	if (!device || !commandList || !replacement)
		return false;

	if (!indexResource.empty())
		EnsureLoadedResourceForCommandList(device, indexResource);
	for (const auto &item : vertexResources)
		EnsureLoadedResourceForCommandList(device, item.resource);

	bool changed = false;
	if (!indexResource.empty()) {
		DX12IaResourceViewSnapshot snapshot;
		{
			DX12LoadedResourceSnapshotReadScope scope;
			AcquireSRWLockExclusive(&gModLock);
			auto loaded = gLoadedResources.find(indexResource);
			if (loaded != gLoadedResources.end())
				SnapshotIaResourceViewLocked(&loaded->second, &snapshot);
			ReleaseSRWLockExclusive(&gModLock);
			AddRefIaResourceViewSnapshot(&snapshot);
		}
		if (snapshot.resource) {
			ApplyIaResourceBarriers(commandList, snapshot);
			replacement->indexBuffer.BufferLocation = snapshot.resource->GetGPUVirtualAddress();
			replacement->indexBuffer.SizeInBytes =
				IaViewByteWidthForResource(snapshot.metadata, snapshot.resource);
			replacement->indexBuffer.Format =
				snapshot.metadata.format == DXGI_FORMAT_R16_UINT ||
				snapshot.metadata.format == DXGI_FORMAT_R32_UINT ?
				snapshot.metadata.format :
				(iaState.hasIndexBuffer && iaState.indexView.Format != DXGI_FORMAT_UNKNOWN ?
					iaState.indexView.Format : DXGI_FORMAT_R32_UINT);
			replacement->RetainResource(snapshot.resource);
			replacement->hasIndexBuffer = true;
			LogIaResourceBindLimited("ib", 0, snapshot.metadata, snapshot.resource);
			changed = true;
		}
		ReleaseIaResourceViewSnapshot(&snapshot);
	}

	if (!vertexResources.empty()) {
		uint32_t minSlot = vertexResources.front().slot;
		uint32_t maxSlot = vertexResources.back().slot;
		if (maxSlot >= minSlot && maxSlot - minSlot < 32) {
			if (replacement->vertexBuffers.empty() ||
			    minSlot < replacement->vertexBufferStartSlot ||
			    maxSlot >= replacement->vertexBufferStartSlot + replacement->vertexBuffers.size()) {
				uint32_t oldStart = replacement->vertexBuffers.empty() ?
					minSlot : replacement->vertexBufferStartSlot;
				uint32_t oldEnd = replacement->vertexBuffers.empty() ?
					maxSlot : replacement->vertexBufferStartSlot +
						static_cast<uint32_t>(replacement->vertexBuffers.size()) - 1;
				uint32_t newStart = (std::min)(oldStart, minSlot);
				uint32_t newEnd = (std::max)(oldEnd, maxSlot);
				if (newEnd - newStart >= 32)
					return changed;

				std::vector<D3D12_VERTEX_BUFFER_VIEW> resized(newEnd - newStart + 1);
				for (size_t i = 0; i < replacement->vertexBuffers.size(); ++i) {
					uint32_t slot = replacement->vertexBufferStartSlot + static_cast<uint32_t>(i);
					resized[slot - newStart] = replacement->vertexBuffers[i];
				}
				replacement->vertexBufferStartSlot = newStart;
				replacement->vertexBuffers.swap(resized);
			}

			for (const auto &item : vertexResources) {
				DX12IaResourceViewSnapshot snapshot;
				{
					DX12LoadedResourceSnapshotReadScope scope;
					AcquireSRWLockExclusive(&gModLock);
					auto loaded = gLoadedResources.find(item.resource);
					if (loaded != gLoadedResources.end())
						SnapshotIaResourceViewLocked(&loaded->second, &snapshot);
					ReleaseSRWLockExclusive(&gModLock);
					AddRefIaResourceViewSnapshot(&snapshot);
				}
				if (!snapshot.resource) {
					ReleaseIaResourceViewSnapshot(&snapshot);
					continue;
				}
				ApplyIaResourceBarriers(commandList, snapshot);
				D3D12_VERTEX_BUFFER_VIEW &view =
					replacement->vertexBuffers[item.slot - replacement->vertexBufferStartSlot];
				view.BufferLocation = snapshot.resource->GetGPUVirtualAddress();
				view.SizeInBytes = IaViewByteWidthForResource(snapshot.metadata, snapshot.resource);
				const DX12IaBufferHash *sourceSlot = FindIaVertexSlot(iaState, item.slot);
				view.StrideInBytes = snapshot.metadata.stride ? snapshot.metadata.stride :
					(sourceSlot ? sourceSlot->vertexView.StrideInBytes : 0);
				replacement->RetainResource(snapshot.resource);
				LogIaResourceBindLimited("vb", item.slot, snapshot.metadata, snapshot.resource);
				changed = true;
				ReleaseIaResourceViewSnapshot(&snapshot);
			}
		}
	}
	return changed;
}

static bool ApplyPendingResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState,
	const std::vector<DX12PendingResourceViewRequest> &requests,
	DX12ModIaReplacement *replacement)
{
	bool changed = false;
	for (const DX12PendingResourceViewRequest &request : requests) {
		changed |= AppendResourceViewsForCommandList(
			device, commandList, iaState,
			request.indexResource, request.vertexResources, replacement);
	}
	return changed;
}

static void AppendTextureOverrideMatches(
	const std::vector<const Bunny::TextureOverrideConfig*> &configs,
	uint32_t matchedHash,
	std::vector<DX12TextureOverrideMatch> *resolvedMatches,
	std::unordered_set<const Bunny::TextureOverrideConfig*> *seen)
{
	if (!resolvedMatches || !seen)
		return;
	for (const Bunny::TextureOverrideConfig *config : configs) {
		if (!config || !seen->insert(config).second)
			continue;
		DX12TextureOverrideMatch match;
		match.config = config;
		match.preRuntimeEffect =
			LookupRuntimeEffect(gTextureOverridePreRuntimeEffect, config->section);
		match.postRuntimeEffect =
			LookupRuntimeEffect(gTextureOverridePostRuntimeEffect, config->section);
		match.matchedHash = matchedHash;
		resolvedMatches->push_back(match);
	}
}

static void SortTextureOverrideMatches(
	std::vector<DX12TextureOverrideMatch> *matches)
{
	if (!matches)
		return;
	std::sort(matches->begin(), matches->end(),
		[](const DX12TextureOverrideMatch &lhs,
		   const DX12TextureOverrideMatch &rhs)
		{
			if (!lhs.config || !rhs.config)
				return lhs.config != nullptr;
			if (lhs.config->matchPriority != rhs.config->matchPriority)
				return lhs.config->matchPriority > rhs.config->matchPriority;
			const std::wstring leftSection =
				Bunny::IniDocumentText::ToLower(lhs.config->section);
			const std::wstring rightSection =
				Bunny::IniDocumentText::ToLower(rhs.config->section);
			if (leftSection != rightSection)
				return leftSection < rightSection;
			return lhs.matchedHash > rhs.matchedHash;
		});
}

static bool FindTargetTextureOverride(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState, const Bunny::CommandListTarget &target,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches,
	const std::vector<DX12CurrentShaderResourceBinding> *preResolvedGraphicsSrvs = nullptr)
{
	if (matches)
		matches->clear();
	if (target.kind == Bunny::CommandListTargetKind::IndexBuffer) {
		if (!iaState.hasIndexBuffer) {
#if defined(_DEBUG)
			DX12LogManager::DebugFields("DX12IaOverrideCheck",
				"\"target\":\"ib\",\"result\":\"no_ib\",\"hash\":\"%08x\"", iaState.indexHash);
#endif
			return false;
		}
		const DX12IaTargetTextureOverrideCacheKey cacheKey =
			MakeIaTargetTextureOverrideCacheKey(
				target,
				iaState.indexHash,
				iaState.indexView.BufferLocation,
				iaState.indexView.SizeInBytes,
				static_cast<uint32_t>(iaState.indexView.Format),
				vertexCount,
				indexCount,
				instanceCount,
				firstVertex,
				firstIndex,
				firstInstance);
		if (TryLoadIaTargetTextureOverrideCache(cacheKey, matches))
			return true;

		std::vector<DX12TextureOverrideMatch> resolvedMatches;
		std::unordered_set<const Bunny::TextureOverrideConfig*> seen;
		std::vector<const Bunny::TextureOverrideConfig*> configs;
		bool found = FindIaTextureOverridesCached(
			target, 0,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, true, 0, &configs);
		AppendTextureOverrideMatches(configs, 0, &resolvedMatches, &seen);

		configs.clear();
		uint32_t matchedHash = iaState.indexHash;
		bool hashedFound = iaState.indexHash && FindIaTextureOverridesCached(
			target, iaState.indexHash,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, true, 0, &configs);
		if (!hashedFound && iaState.indexHash) {
			const DX12IaRegionDraw draw = DX12IaRegionDraw::FromDrawArguments(
				indexCount != 0,
				vertexCount,
				indexCount,
				firstVertex,
				firstIndex,
				0);
			const uint32_t regionHash =
				DX12IaRegion::HashIndexBufferView(iaState.indexView, draw);
			if (regionHash && regionHash != iaState.indexHash) {
				hashedFound = FindIaTextureOverridesCached(
					target, regionHash,
					vertexCount, indexCount, instanceCount,
					firstVertex, firstIndex, firstInstance, true, 0, &configs);
				if (hashedFound)
					matchedHash = regionHash;
			}
		}
		AppendTextureOverrideMatches(configs, matchedHash, &resolvedMatches, &seen);
		SortTextureOverrideMatches(&resolvedMatches);
		found = !resolvedMatches.empty();
		StoreIaTargetTextureOverrideCache(cacheKey, resolvedMatches);
		if (matches)
			*matches = resolvedMatches;
#if defined(_DEBUG)
		DX12LogManager::DebugFields("DX12IaOverrideCheck",
			"\"target\":\"ib\",\"hash\":\"%08x\",\"found\":%s,\"configs\":%zu",
			matchedHash, found ? "true" : "false",
			resolvedMatches.size());
#endif
		return found;
	}

	if (target.kind == Bunny::CommandListTargetKind::VertexBuffer) {
		const DX12IaBufferHash *slot = FindIaVertexSlot(iaState, target.slot);
#if defined(_DEBUG)
		if (!slot) {
			DX12LogManager::DebugFields("DX12IaOverrideCheck",
				"\"target\":\"vb%u\",\"result\":\"no_slot\"", target.slot);
		}
#endif
		if (!slot)
			return false;
		const DX12IaTargetTextureOverrideCacheKey cacheKey =
			MakeIaTargetTextureOverrideCacheKey(
				target,
				slot->hash,
				slot->vertexView.BufferLocation,
				slot->vertexView.SizeInBytes,
				slot->vertexView.StrideInBytes,
				vertexCount,
				indexCount,
				instanceCount,
				firstVertex,
				firstIndex,
				firstInstance);
		if (TryLoadIaTargetTextureOverrideCache(cacheKey, matches))
			return true;

		std::vector<DX12TextureOverrideMatch> resolvedMatches;
		std::unordered_set<const Bunny::TextureOverrideConfig*> seen;
		std::vector<const Bunny::TextureOverrideConfig*> configs;
		bool found = FindIaTextureOverridesCached(
			target, 0,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, false, target.slot, &configs);
		AppendTextureOverrideMatches(configs, 0, &resolvedMatches, &seen);

		configs.clear();
		uint32_t matchedHash = slot->hash;
		bool hashedFound = slot->hash && FindIaTextureOverridesCached(
			target, slot->hash,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, false, target.slot, &configs);
		if (!hashedFound && slot->hash) {
			const DX12IaRegionDraw draw = DX12IaRegionDraw::FromDrawArguments(
				indexCount != 0,
				vertexCount,
				indexCount,
				firstVertex,
				firstIndex,
				0);
			const uint32_t regionHash =
				DX12IaRegion::HashVertexBufferView(target.slot, slot->vertexView, draw);
			if (regionHash && regionHash != slot->hash) {
				hashedFound = FindIaTextureOverridesCached(
					target, regionHash,
					vertexCount, indexCount, instanceCount,
					firstVertex, firstIndex, firstInstance, false, target.slot, &configs);
				if (hashedFound)
					matchedHash = regionHash;
			}
		}
		AppendTextureOverrideMatches(configs, matchedHash, &resolvedMatches, &seen);
		SortTextureOverrideMatches(&resolvedMatches);
		found = !resolvedMatches.empty();
		StoreIaTargetTextureOverrideCache(cacheKey, resolvedMatches);
		if (matches)
			*matches = resolvedMatches;
#if defined(_DEBUG)
		DX12LogManager::DebugFields("DX12IaOverrideCheck",
			"\"target\":\"vb%u\",\"hash\":\"%08x\",\"found\":%s,\"configs\":%zu",
			target.slot, matchedHash, found ? "true" : "false",
			resolvedMatches.size());
#endif
		return found;
	}

	if (target.kind == Bunny::CommandListTargetKind::ConstantBuffer ||
	    target.kind == Bunny::CommandListTargetKind::ShaderResource ||
	    target.kind == Bunny::CommandListTargetKind::UnorderedAccessView) {
		return FindDescriptorTextureOverrides(
			commandList, target, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, matches,
			preResolvedGraphicsSrvs);
	}

	return false;
}

bool DX12ModHasAnyPotentialDirectIaTextureOverrideCandidate(
	const DX12ActiveIaState &ia,
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

	if (targetPlan.RequiresIndexBuffer(indexed) &&
	    ia.hasIndexBuffer &&
	    HasPotentialIaTextureOverrideCandidateLocked(
			true,
			0,
			vertexCount,
			indexCount,
			instanceCount,
			firstVertex,
			firstIndex,
			firstInstance)) {
		return true;
	}

	for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
		if (!targetPlan.RequiresVertexBuffer(slot) || !ia.hasVertexBuffer[slot])
			continue;
		if (HasPotentialIaTextureOverrideCandidateLocked(
				false,
				slot,
				vertexCount,
				indexCount,
				instanceCount,
				firstVertex,
				firstIndex,
				firstInstance)) {
			return true;
		}
	}

	return false;
}

// Returns true when the prefilter tables prove no candidate can exist for this
// (ib/vb, slot, hash) tuple, ignoring draw-context filters (match_first_index
// etc.). The prefilter tables are queried with zero counts: callers must only
// use this when gIaPrefilterCountSensitive == 0 so the count-indexed tables are
// empty and the generic tables are authoritative. Candidates that depend on
// active pre-skin sections still resolve through the live snapshot, and any
// candidate that survives hash + match_cs + IA-binding checks counts as
// "possibly present" so the per-draw path stays authoritative.
static bool IaHashDefinitelyHasNoCandidate(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t hash)
{
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverridePrefilterCandidatesLocked(
			indexBuffer, vertexSlot, 0, 0);
	if (!candidateIndexes || candidateIndexes->empty())
		return true;

	const bool dependsOnActivePreSkin =
		IaCandidateIndexesDependOnActivePreSkin(*candidateIndexes);
	const std::unordered_set<std::wstring> &activePreSkinSections =
		dependsOnActivePreSkin ?
			GetActivePreSkinSectionsSnapshot() :
			EmptyActivePreSkinSections();

	for (size_t index : *candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate =
			gIaTextureOverrides[index];
		const Bunny::TextureOverrideConfig *config = candidate.config;
		if (!config || config->hash != hash)
			continue;
		if (!TextureOverrideMatchCsSatisfied(*config, activePreSkinSections))
			continue;
		if (!TextureOverrideMatchesIaBinding(
				*config, indexBuffer, vertexSlot))
			continue;
		return false;
	}
	return true;
}

// DX11-style IA-state-level negative check: the per-view hashes are computed
// (or reused from the resident cache) and matched against the candidate tables
// without any draw-context filtering. Only call when gIaPrefilterCountSensitive
// is 0; the result is then independent of vertex/index counts and offsets, so
// the draw path can cache it across draws for the same IA bindings.
bool DX12ModIaStateDefinitelyHasNoCandidate(
	const DX12ActiveIaState &ia,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed)
{
	if (!targetPlan.hasTextureOverrideChecks ||
	    targetPlan.hasDescriptorTargets ||
	    targetPlan.requireAllIaBuffers ||
	    !targetPlan.HasIaTargets()) {
		return false;
	}

	if (targetPlan.RequiresIndexBuffer(indexed) && ia.hasIndexBuffer) {
		const uint32_t indexHash =
			DX12CommandListRuntime::GetOrBuildCachedIndexViewHash(
				const_cast<DX12ActiveIaState*>(&ia));
		if (!IaHashDefinitelyHasNoCandidate(true, 0, indexHash))
			return false;
	}

	for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
		if (!targetPlan.RequiresVertexBuffer(slot) || !ia.hasVertexBuffer[slot])
			continue;
		const uint32_t vertexHash =
			DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(
				const_cast<DX12ActiveIaState*>(&ia), slot);
		if (!IaHashDefinitelyHasNoCandidate(false, slot, vertexHash))
			return false;
	}

	return true;
}

bool DX12ModIaPrefilterCountSensitive()
{
	return gIaPrefilterCountSensitive != 0;
}
