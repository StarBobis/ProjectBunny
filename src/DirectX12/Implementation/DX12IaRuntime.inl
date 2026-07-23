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
	std::unordered_map<uint32_t, std::vector<size_t>> *vertexTextureOverridePrefilterIndex,
	std::vector<size_t> *anyVertexTextureOverridePrefilterIndex,
	std::unordered_map<uint32_t, std::vector<size_t>> *mergedVertexTextureOverridePrefilterIndex)
{
	if (!iaTextureOverrides || !indexTextureOverrideIndex ||
		!vertexTextureOverrideIndex || !anyVertexTextureOverrideIndex ||
		!mergedVertexTextureOverrideIndex ||
		!indexTextureOverridePrefilterIndex ||
		!vertexTextureOverridePrefilterIndex ||
		!anyVertexTextureOverridePrefilterIndex ||
		!mergedVertexTextureOverridePrefilterIndex)
		return;

	iaTextureOverrides->clear();
	indexTextureOverrideIndex->clear();
	vertexTextureOverrideIndex->clear();
	anyVertexTextureOverrideIndex->clear();
	mergedVertexTextureOverrideIndex->clear();
	indexTextureOverridePrefilterIndex->clear();
	vertexTextureOverridePrefilterIndex->clear();
	anyVertexTextureOverridePrefilterIndex->clear();
	mergedVertexTextureOverridePrefilterIndex->clear();
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
				indexTextureOverridePrefilterIndex->push_back(index);
			}

			bool indexedSpecificVertexSlot = false;
			for (const auto &slotResource : config.vertexBufferResources)
			{
				if (!TextureOverrideMatchesIaBinding(config, false, slotResource.first))
					continue;
				(*vertexTextureOverrideIndex)[MakeIaVertexIndexKey(config.hash, slotResource.first)].push_back(index);
				(*vertexTextureOverridePrefilterIndex)[slotResource.first].push_back(index);
				indexedSpecificVertexSlot = true;
			}
			if (!indexedSpecificVertexSlot &&
				TextureOverrideMatchesIaBinding(config, false, 0))
			{
				(*anyVertexTextureOverrideIndex)[config.hash].push_back(index);
				anyVertexTextureOverridePrefilterIndex->push_back(index);
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
	for (auto &bucket : *vertexTextureOverridePrefilterIndex)
		sortCandidates(&bucket.second);
	sortCandidates(anyVertexTextureOverridePrefilterIndex);

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
	bool indexBuffer, uint32_t vertexSlot)
{
	if (indexBuffer)
		return &gIaIndexTextureOverridePrefilterIndex;

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
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	LONG64 frameIntervalUs = 0;
	if (s_hasLastFrame)
		frameIntervalUs = (t0.QuadPart - s_lastFrameTime.QuadPart) * 1000000 / freq.QuadPart;
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
	DX12LogManager::Fields("DX12Perf",
		"\"scope\":\"ModBeginFrame\",\"us\":%lld,\"frameIntervalUs\":%lld,\"frameDraws\":%ld,\"frameDispatches\":%ld",
		static_cast<long long>((t1.QuadPart - t0.QuadPart) * 1000000 / freq.QuadPart),
		static_cast<long long>(frameIntervalUs),
		DX12RuntimeState::TakeFrameDraws(),
		DX12RuntimeState::TakeFrameDispatches());
}

bool DX12ModPreparePresentReplacement(
	ID3D12GraphicsCommandList *commandList, DX12ModIaReplacement *replacement)
{
	if (!commandList || !replacement)
		return false;
	if (!DX12ModNeedsPresentReplacement())
		return false;

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return false;

	const LONG present = DX12RuntimeState::GetPresentCount();
	for (;;) {
		const LONG observed = InterlockedCompareExchange(
			&gPresentCommandListExecutedPresent, 0, 0);
		if (observed == present) {
			device->Release();
			return false;
		}
		if (InterlockedCompareExchange(
			    &gPresentCommandListExecutedPresent, present, observed) == observed)
			break;
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

static DX12TextureOverrideLookupCacheKey MakeIaTextureOverrideLookupCacheKey(
	const Bunny::CommandListTarget &target, uint32_t hash,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	UINT64 preSkinGeneration)
{
	DX12TextureOverrideLookupCacheKey key = {};
	key.reloadGeneration = gReloadGeneration;
	key.preSkinGeneration = preSkinGeneration;
	key.targetKind = static_cast<uint32_t>(target.kind);
	key.shaderStage = static_cast<uint32_t>(target.stage);
	key.slot = target.slot;
	key.hash = hash;
	key.vertexCount = vertexCount;
	key.indexCount = indexCount;
	key.instanceCount = instanceCount;
	key.firstVertex = firstVertex;
	key.firstIndex = firstIndex;
	key.firstInstance = firstInstance;
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
	const DX12TextureOverrideLookupCacheKey key =
		MakeIaTextureOverrideLookupCacheKey(
			target, hash, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance,
			effectivePreSkinGeneration);
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
	if (!hash)
		return false;

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

	const std::vector<DX12TriggeredTextureOverride> *candidateTriggers =
		FindIaTriggeredTextureOverrideCandidates(
			*snapshot, hash, indexBuffer, vertexSlot);
	UINT64 preSkinGeneration = 0;
	const std::unordered_set<std::wstring> &activePreSkinSections =
		(candidateTriggers &&
		 IaTriggeredCandidatesDependOnActivePreSkin(*candidateTriggers)) ?
			GetActivePreSkinSectionsSnapshot(&preSkinGeneration) :
			EmptyActivePreSkinSections();
	const DX12TextureOverrideLookupCacheKey key =
		MakeIaTextureOverrideLookupCacheKey(
			target, hash, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, preSkinGeneration);

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
	const DX12CurrentShaderResourceBinding &binding)
{
	if (binding.hasDescriptor) {
		ID3D12Device *device = AcquireModDevice(commandList);
		const uint32_t hash =
			DX12TextureHashUtils::DescriptorHuntHash(device, binding);
		if (device)
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
	uint32_t firstInstance,
	const std::unordered_set<std::wstring> &activePreSkinSections)
{
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverridePrefilterCandidatesLocked(indexBuffer, vertexSlot);
	if (!candidateIndexes || candidateIndexes->empty())
		return false;

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
	std::vector<DX12TextureOverrideMatch> *matches)
{
	if (matches)
		matches->clear();
	if (!commandList)
		return false;

	std::vector<DX12CurrentShaderResourceBinding> bindings;
	const bool compute = TargetStageIsCompute(target.stage);
	if (!DX12BindingTracker::GetCurrentShaderResourceBindingsByRegister(
		    commandList,
		    compute,
		    DescriptorRangeTypeForTarget(target.kind),
		    target.slot,
		    0,
		    &bindings))
		return false;

	for (const DX12CurrentShaderResourceBinding &binding : bindings) {
		if (!DescriptorBindingMatchesTarget(binding, target))
			continue;
		const uint32_t resourceHash = HashDescriptorBindingResource(binding);
		const uint32_t descriptorHash = HashDescriptorBinding(commandList, binding);
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
		AppendTextureOverrideMatchesByHash(
			resourceHash, resourceHash, descriptorHash, &binding, compute,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, matches);
		if (descriptorHash && descriptorHash != resourceHash) {
			AppendTextureOverrideMatchesByHash(
				descriptorHash, resourceHash, descriptorHash, &binding, compute,
				vertexCount, indexCount, instanceCount,
				firstVertex, firstIndex, firstInstance, matches);
		}
	}
	return matches && !matches->empty();
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

static bool FindTargetTextureOverride(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState, const Bunny::CommandListTarget &target,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches)
{
	if (matches)
		matches->clear();
	if (target.kind == Bunny::CommandListTargetKind::IndexBuffer) {
		if (!iaState.hasIndexBuffer || !iaState.indexHash) {
#if defined(_DEBUG)
			DX12LogManager::DebugFields("DX12IaOverrideCheck",
				"\"target\":\"ib\",\"result\":\"no_ib\",\"hash\":\"%08x\"", iaState.indexHash);
#endif
			return false;
		}
		std::vector<const Bunny::TextureOverrideConfig*> configs;
		bool found = FindIaTextureOverridesCached(
			target, iaState.indexHash,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, true, 0, &configs);
		if (found && matches) {
			for (const Bunny::TextureOverrideConfig *config : configs) {
				if (!config)
					continue;
				DX12TextureOverrideMatch match;
				match.config = config;
				match.preRuntimeEffect =
					LookupRuntimeEffect(gTextureOverridePreRuntimeEffect, config->section);
				match.postRuntimeEffect =
					LookupRuntimeEffect(gTextureOverridePostRuntimeEffect, config->section);
				match.matchedHash = iaState.indexHash;
				matches->push_back(match);
			}
		}
#if defined(_DEBUG)
		DX12LogManager::DebugFields("DX12IaOverrideCheck",
			"\"target\":\"ib\",\"hash\":\"%08x\",\"found\":%s,\"configs\":%zu",
			iaState.indexHash, found ? "true" : "false",
			configs.size());
#endif
		return found;
	}

	if (target.kind == Bunny::CommandListTargetKind::VertexBuffer) {
		const DX12IaBufferHash *slot = FindIaVertexSlot(iaState, target.slot);
#if defined(_DEBUG)
		if (!slot || !slot->hash) {
			DX12LogManager::DebugFields("DX12IaOverrideCheck",
				"\"target\":\"vb%u\",\"result\":\"no_slot\"", target.slot);
		}
#endif
		if (!slot || !slot->hash)
			return false;
		std::vector<const Bunny::TextureOverrideConfig*> configs;
		bool found = FindIaTextureOverridesCached(
			target, slot->hash,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, false, target.slot, &configs);
		if (found && matches) {
			for (const Bunny::TextureOverrideConfig *config : configs) {
				if (!config)
					continue;
				DX12TextureOverrideMatch match;
				match.config = config;
				match.preRuntimeEffect =
					LookupRuntimeEffect(gTextureOverridePreRuntimeEffect, config->section);
				match.postRuntimeEffect =
					LookupRuntimeEffect(gTextureOverridePostRuntimeEffect, config->section);
				match.matchedHash = slot->hash;
				matches->push_back(match);
			}
		}
#if defined(_DEBUG)
		DX12LogManager::DebugFields("DX12IaOverrideCheck",
			"\"target\":\"vb%u\",\"hash\":\"%08x\",\"found\":%s,\"configs\":%zu",
			target.slot, slot->hash, found ? "true" : "false",
			configs.size());
#endif
		return found;
	}

	if (target.kind == Bunny::CommandListTargetKind::ConstantBuffer ||
	    target.kind == Bunny::CommandListTargetKind::ShaderResource ||
	    target.kind == Bunny::CommandListTargetKind::UnorderedAccessView) {
		return FindDescriptorTextureOverrides(
			commandList, target, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, matches);
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

	const std::unordered_set<std::wstring> &activePreSkinSections =
		GetActivePreSkinSectionsSnapshot();

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
			firstInstance,
			activePreSkinSections)) {
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
				firstInstance,
				activePreSkinSections)) {
			return true;
		}
	}

	return false;
}
