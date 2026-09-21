// DX12TextureOverrideMatching.cpp: texture override matching: IA/descriptor candidate lookup and resolved bindings.
// Split from DX12ModRuntime.cpp; implementation unit for its module.
#include "DX12ModRuntimeInternal.h"
#include <bit>


bool NumericMatchSatisfied(const Bunny::NumericMatch &match, uint32_t actual)
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


bool TextureOverrideMatchesDrawContext(
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


DX12TextureOverrideDrawKeyUsage BuildTriggeredDrawKeyUsage(
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


void AppendDescriptorTriggeredPrefilterSectionIds(
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


bool HasPotentialDescriptorTriggeredPrefilterCandidates(
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


void CollectDescriptorTriggeredPrefilterSectionIds(
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


bool TextureOverrideHasDrawKind(
	const Bunny::TextureOverrideConfig &config, Bunny::CommandListActionKind kind)
{
	for (const Bunny::CommandListAction &action : config.actions)
	{
		if (action.kind == kind)
			return true;
	}
	return false;
}


bool TextureOverrideHasDrawContextMatch(const Bunny::TextureOverrideConfig &config)
{
	return config.hasMatchVertexCount || config.hasMatchIndexCount ||
		   config.hasMatchInstanceCount || config.hasMatchFirstVertex ||
		   config.hasMatchFirstIndex || config.hasMatchFirstInstance;
}


void SetCompiledVertexResourceBinding(
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


std::vector<DX12CompiledVertexResourceBinding> CompileVertexResourceBindings(
	const std::map<uint32_t, std::wstring> &vertexResources)
{
	std::vector<DX12CompiledVertexResourceBinding> bindings;
	bindings.reserve(vertexResources.size());
	for (const auto &item : vertexResources)
		bindings.push_back({item.first, item.second});
	return bindings;
}


void AppendCompiledDrawSrvResourceBindings(
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


bool TextureOverrideMatchesIaBinding(
	const Bunny::TextureOverrideConfig &config, bool indexBuffer, uint32_t vertexSlot)
{
	if (config.hash == 0 && TextureOverrideHasDrawContextMatch(config) &&
		config.indexBufferResource.empty() && config.vertexBufferResources.empty())
	{
		if (indexBuffer)
			return !TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::Draw);
		return TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::Draw);
	}

	// "hash + handling = skip" is the classic 3DMigoto skip section (e.g.
	// [TextureOverride_IB_xxxxxxxx]): it binds no replacement buffer and
	// issues no draw, so the resource/draw-kind requirements below would
	// never let it match — yet skipping the original draw is precisely its
	// job. A skip section matches whichever IA resource its hash names.
	if (config.handlingSkip)
		return true;

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


UINT64 MakeIaVertexIndexKey(uint32_t hash, uint32_t vertexSlot)
{
	return (static_cast<UINT64>(hash) << 32) | vertexSlot;
}


bool NumericMatchIsExactValue(const Bunny::NumericMatch &match)
{
	return match.enabled && match.op == Bunny::NumericMatchOp::Equal;
}


void BuildDx12IaTextureOverrideIndex(
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


const std::vector<size_t> *FindIaTextureOverridePrefilterCandidatesLocked(
	bool indexBuffer, uint32_t vertexSlot, uint32_t vertexCount, uint32_t indexCount,
	std::vector<size_t> *scratch)
{
	if (scratch)
		scratch->clear();

	if (indexBuffer) {
		auto indexedByCount =
			gIaIndexTextureOverridePrefilterByIndexCount.find(indexCount);
		if (indexedByCount != gIaIndexTextureOverridePrefilterByIndexCount.end()) {
			// The exact-count bucket and the generic list (candidates without
			// an exact match_index_count) are mutually exclusive in the build,
			// but generic overrides match every draw count. Returning only the
			// exact-count bucket starves generic overrides whenever a draw
			// happens to use a configured exact count, which shows up as
			// intermittent missing matches (flicker). The buckets are disjoint
			// by construction, so no dedup is required.
			if (!scratch)
				return &indexedByCount->second;
			*scratch = indexedByCount->second;
			if (!gIaIndexTextureOverridePrefilterIndex.empty())
				scratch->insert(scratch->end(),
					gIaIndexTextureOverridePrefilterIndex.begin(),
					gIaIndexTextureOverridePrefilterIndex.end());
			return scratch;
		}
		return &gIaIndexTextureOverridePrefilterIndex;
	}

	auto mergedByCount = gIaMergedVertexTextureOverridePrefilterByVertexCount.find(
		MakeIaVertexIndexKey(vertexCount, vertexSlot));
	if (mergedByCount != gIaMergedVertexTextureOverridePrefilterByVertexCount.end()) {
		// Same rationale as the index side: also consider the generic merged
		// candidates for this slot, which match any vertex count.
		if (!scratch)
			return &mergedByCount->second;
		*scratch = mergedByCount->second;
		auto mergedGeneric =
			gIaMergedVertexTextureOverridePrefilterIndex.find(vertexSlot);
		if (mergedGeneric != gIaMergedVertexTextureOverridePrefilterIndex.end())
			scratch->insert(scratch->end(),
				mergedGeneric->second.begin(),
				mergedGeneric->second.end());
		return scratch;
	}

	auto anySlotByCount =
		gIaAnyVertexTextureOverridePrefilterByVertexCount.find(vertexCount);
	if (anySlotByCount != gIaAnyVertexTextureOverridePrefilterByVertexCount.end()) {
		// Merge in the generic any-slot candidates, which match any count.
		if (!scratch)
			return &anySlotByCount->second;
		*scratch = anySlotByCount->second;
		if (!gIaAnyVertexTextureOverridePrefilterIndex.empty())
			scratch->insert(scratch->end(),
				gIaAnyVertexTextureOverridePrefilterIndex.begin(),
				gIaAnyVertexTextureOverridePrefilterIndex.end());
		return scratch;
	}

	auto merged = gIaMergedVertexTextureOverridePrefilterIndex.find(vertexSlot);
	if (merged != gIaMergedVertexTextureOverridePrefilterIndex.end())
		return &merged->second;

	if (!gIaAnyVertexTextureOverridePrefilterIndex.empty())
		return &gIaAnyVertexTextureOverridePrefilterIndex;

	auto specific = gIaVertexTextureOverridePrefilterIndex.find(vertexSlot);
	return specific != gIaVertexTextureOverridePrefilterIndex.end() ?
		&specific->second : nullptr;
}


void CollectIaTriggeredTextureOverrideCandidates(
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

	std::vector<size_t> prefilterScratch;
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverridePrefilterCandidatesLocked(
			indexBuffer,
			vertexSlot,
			vertexCount,
			indexCount,
			&prefilterScratch);
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



bool FindIaTextureOverridesCached(
	const Bunny::CommandListTarget &target, uint32_t hash,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	bool indexBuffer, uint32_t vertexSlot,
	std::vector<const Bunny::TextureOverrideConfig*> *configs)
{
	if (configs)
		configs->clear();

	const DX12CommandExecutionRuntimeSnapshot *snapshot =
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


DX12IaTargetTextureOverrideCacheKey MakeIaTargetTextureOverrideCacheKey(
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


bool TryLoadIaTargetTextureOverrideCache(
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


void StoreIaTargetTextureOverrideCache(
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


const DX12IaBufferHash *FindIaVertexSlot(const DX12IaHashState &iaState, uint32_t slot)
{
	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		if (buffer.slot == slot)
			return &buffer;
	}
	return nullptr;
}


bool TargetStageIsCompute(Bunny::CommandListShaderStage stage)
{
	return stage == Bunny::CommandListShaderStage::Compute;
}


D3D12_DESCRIPTOR_RANGE_TYPE DescriptorRangeTypeForTarget(
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


bool DescriptorBindingMatchesTarget(
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


uint32_t HashDescriptorBinding(
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


uint32_t HashDescriptorBindingResource(const DX12CurrentShaderResourceBinding &binding)
{
	if (!binding.hasDescriptor || !binding.descriptor.hasResourceDesc)
		return 0;
	if (binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
		uint32_t hash = 0;
		if (DX12TryGetTextureHash(binding.descriptor.resource, &hash))
			return hash;
		// Self-heal the single-shot upload-time hash gate: a capture lost to
		// queue pressure, an unrecognized upload shape, or an upload that ran
		// before tracking was enabled would otherwise keep this texture
		// unmatched for the whole session. The request is deduped and
		// cooldown-gated, so this costs one map lookup per call in steady
		// state and the hash appears a frame or two after the first check.
		DX12RequestTextureHashOnDemand(
			binding.descriptor.resource, &binding.descriptor.resourceDesc);
	}
	return 0;
}


bool BuildDescriptorTextureOverrideCandidateCacheKey(
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


void AppendTextureOverrideCandidateMatchesByHash(
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


bool FilterDescriptorTextureOverrideCandidates(
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



bool HasPotentialIaTextureOverrideCandidateLocked(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance,
	std::vector<size_t> *prefilterScratch)
{
	std::vector<size_t> localScratch;
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverridePrefilterCandidatesLocked(
			indexBuffer, vertexSlot, vertexCount, indexCount,
			prefilterScratch ? prefilterScratch : &localScratch);
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


bool HasPotentialHashedIaTextureOverrideCandidateLocked(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount)
{
	std::vector<size_t> prefilterScratch;
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverridePrefilterCandidatesLocked(
			indexBuffer,
			vertexSlot,
			vertexCount,
			indexCount,
			&prefilterScratch);
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
	std::vector<size_t> prefilterScratch;
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverridePrefilterCandidatesLocked(
			indexBuffer,
			vertexSlot,
			vertexCount,
			indexCount,
			&prefilterScratch);
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



// ---------------------------------------------------------------------------
// TIMING-build diagnostics for FindDescriptorTextureOverrides.
//
// The function has seven silent exits and two silent caches. In steady state
// the exit stage for a given (kind, stage, slot) target is constant, so a
// scene switch that breaks texture replacement appears as a stage transition
// (e.g. computed_matched -> computed_no_match with texNoHash>0 means the new
// scene's textures never got a content hash; -> bindings_query_failed means
// the binding tracker lost the heap). Only transitions are logged, a target
// that toggles every draw is sampled after the first few flips, and a sparse
// heartbeat proves the pipeline is still being reached when no transition
// happens at all. Empty inline in non-TIMING builds.
// ---------------------------------------------------------------------------
enum DX12DescriptorOverrideMatchStage : uint32_t
{
	DX12DescOverrideStage_CacheHitEmpty = 0,
	DX12DescOverrideStage_CacheHitMatches,
	DX12DescOverrideStage_NoSnapshot,
	DX12DescOverrideStage_NoPrefilterCandidates,
	DX12DescOverrideStage_NoAllowedSections,
	DX12DescOverrideStage_BindingsQueryFailed,
	DX12DescOverrideStage_BindingsEmpty,
	DX12DescOverrideStage_ComputedNoMatch,
	DX12DescOverrideStage_ComputedMatched,
	DX12DescOverrideStage_Count
};

static const char *DX12DescriptorOverrideMatchStageName(uint32_t stage)
{
	switch (stage) {
	case DX12DescOverrideStage_CacheHitEmpty: return "cache_hit_empty";
	case DX12DescOverrideStage_CacheHitMatches: return "cache_hit_matches";
	case DX12DescOverrideStage_NoSnapshot: return "no_snapshot";
	case DX12DescOverrideStage_NoPrefilterCandidates: return "no_prefilter_candidates";
	case DX12DescOverrideStage_NoAllowedSections: return "no_allowed_sections";
	case DX12DescOverrideStage_BindingsQueryFailed: return "bindings_query_failed";
	case DX12DescOverrideStage_BindingsEmpty: return "bindings_empty";
	case DX12DescOverrideStage_ComputedNoMatch: return "computed_no_match";
	case DX12DescOverrideStage_ComputedMatched: return "computed_matched";
	default: return "unknown";
	}
}

struct DX12DescriptorOverrideStageLogEntry
{
	uint32_t lastStage = DX12DescOverrideStage_Count;
	uint32_t toggles = 0;
};

// Per-target check state for the resolved-bindings path (the executor's
// checktextureoverride evaluation). The fingerprint folds every
// target-matching binding's (resourceHash, descriptorHash) pair, so a line is
// logged not only on matched/no_match flips but whenever the underlying hash
// state moves - e.g. the async readback finally publishing a texture's hash.
struct DX12ResolvedOverrideCheckLogEntry
{
	uint64_t lastFingerprint = 0;
	bool lastFound = false;
	uint32_t toggles = 0;
	bool seen = false;
};

static void DX12LogResolvedOverrideCheck(
	uint32_t stage,
	const Bunny::CommandListTarget &target,
	const std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings,
	size_t candidateCount,
	bool found,
	const std::vector<DX12TextureOverrideMatch> *matches)
{
#if DX12_TIMING_BUILD
	static thread_local std::unordered_map<
		uint64_t, DX12ResolvedOverrideCheckLogEntry> s_checkLogByTarget;
	uint64_t fingerprint = 14695981039361056037ull;
	uint32_t targetBindings = 0;
	uint32_t texNoHash = 0;
	if (resolvedBindings) {
		for (const DX12ResolvedGraphicsSrvBinding &resolved : *resolvedBindings) {
			if (!DescriptorBindingMatchesTarget(resolved.binding, target))
				continue;
			++targetBindings;
			fingerprint ^=
				(static_cast<uint64_t>(resolved.resourceHash) << 32) ^
				resolved.descriptorHash ^ targetBindings;
			fingerprint *= 1099511628211ull;
			if (!resolved.resourceHash && resolved.binding.hasDescriptor &&
				resolved.binding.descriptor.hasResourceDesc &&
				resolved.binding.descriptor.resource &&
				resolved.binding.descriptor.resourceDesc.Dimension ==
					D3D12_RESOURCE_DIMENSION_TEXTURE2D)
				++texNoHash;
		}
	}
	const uint64_t key =
		(static_cast<uint64_t>(static_cast<uint32_t>(target.kind)) << 40) |
		(static_cast<uint64_t>(static_cast<uint32_t>(target.stage)) << 32) |
		target.slot;
	DX12ResolvedOverrideCheckLogEntry &entry = s_checkLogByTarget[key];
	if (entry.seen && entry.lastFound == found &&
		entry.lastFingerprint == fingerprint)
		return;
	entry.seen = true;
	entry.lastFound = found;
	entry.lastFingerprint = fingerprint;
	++entry.toggles;
	if (entry.toggles > 4 && (entry.toggles & 0x3F) != 0)
		return;
	DX12_LOG("DX12TextureOverrideCheck",
		"stage=%s,kind=%u,shaderStage=%u,slot=%u,found=%d,bindings=%u,"
		"texNoHash=%u,candidates=%zu,firstHash=%08x,fingerprint=%llx,"
		"toggles=%u",
		DX12DescriptorOverrideMatchStageName(stage),
		static_cast<uint32_t>(target.kind),
		static_cast<uint32_t>(target.stage),
		target.slot,
		found ? 1 : 0,
		targetBindings,
		texNoHash,
		candidateCount,
		(found && matches && !matches->empty()) ?
			matches->front().matchedHash : 0,
		static_cast<unsigned long long>(fingerprint),
		entry.toggles);
	// Detail lines naming the exact resources that carry no content hash -
	// the pointer is the correlation key against the DX12TextureHashTrack /
	// DX12Readback* lines that explain WHY the hash is missing.
	if (!texNoHash || !resolvedBindings)
		return;
	uint32_t detailed = 0;
	for (const DX12ResolvedGraphicsSrvBinding &resolved : *resolvedBindings) {
		if (detailed >= 4)
			break;
		if (!DescriptorBindingMatchesTarget(resolved.binding, target) ||
			resolved.resourceHash ||
			!resolved.binding.hasDescriptor ||
			!resolved.binding.descriptor.hasResourceDesc ||
			!resolved.binding.descriptor.resource ||
			resolved.binding.descriptor.resourceDesc.Dimension !=
				D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			continue;
		++detailed;
		DX12_LOG("DX12TextureOverrideCheck",
			"stage=binding_nohash,kind=%u,shaderStage=%u,slot=%u,"
			"resource=%p,width=%llu,height=%u,mips=%u,format=%u,"
			"descriptorHash=%08x",
			static_cast<uint32_t>(target.kind),
			static_cast<uint32_t>(target.stage),
			target.slot,
			resolved.binding.descriptor.resource,
			static_cast<unsigned long long>(
				resolved.binding.descriptor.resourceDesc.Width),
			resolved.binding.descriptor.resourceDesc.Height,
			resolved.binding.descriptor.resourceDesc.MipLevels,
			static_cast<UINT>(
				resolved.binding.descriptor.resourceDesc.Format),
			resolved.descriptorHash);
	}
#else
	(void)stage;
	(void)target;
	(void)resolvedBindings;
	(void)candidateCount;
	(void)found;
	(void)matches;
#endif
}


static void DX12LogDescriptorOverrideMatchStage(
	uint32_t stage,
	const Bunny::CommandListTarget &target,
	uint32_t vertexCount,
	uint32_t indexCount,
	size_t bindingCount,
	uint32_t textureBindingsWithoutHash,
	size_t candidateCount,
	uint32_t firstMatchedHash)
{
#if DX12_TIMING_BUILD
	static thread_local std::unordered_map<
		uint64_t, DX12DescriptorOverrideStageLogEntry> s_stageLogByTarget;
	static thread_local uint32_t s_callsSinceHeartbeat = 0;
	const uint64_t key =
		(static_cast<uint64_t>(static_cast<uint32_t>(target.kind)) << 40) |
		(static_cast<uint64_t>(static_cast<uint32_t>(target.stage)) << 32) |
		target.slot;
	// Liveness heartbeat: silence in this log means the draw pipeline stopped
	// reaching descriptor matching at all (a negative cache upstream, or the
	// hook gates dropped out) - a very different failure from a stage change.
	if ((++s_callsSinceHeartbeat & 0x3FFF) == 1) {
		DX12_LOG("DX12TextureOverrideMatch",
			"stage=heartbeat,currentStage=%s",
			DX12DescriptorOverrideMatchStageName(stage));
	}
	DX12DescriptorOverrideStageLogEntry &entry = s_stageLogByTarget[key];
	if (entry.lastStage == stage)
		return;
	entry.lastStage = stage;
	++entry.toggles;
	if (entry.toggles > 4 && (entry.toggles & 0x3F) != 0)
		return;
	DX12_LOG("DX12TextureOverrideMatch",
		"stage=%s,kind=%u,shaderStage=%u,slot=%u,vertexCount=%u,"
		"indexCount=%u,bindings=%zu,texNoHash=%u,candidates=%zu,"
		"firstHash=%08x,toggles=%u",
		DX12DescriptorOverrideMatchStageName(stage),
		static_cast<uint32_t>(target.kind),
		static_cast<uint32_t>(target.stage),
		target.slot,
		vertexCount,
		indexCount,
		bindingCount,
		textureBindingsWithoutHash,
		candidateCount,
		firstMatchedHash,
		entry.toggles);
#else
	(void)stage;
	(void)target;
	(void)vertexCount;
	(void)indexCount;
	(void)bindingCount;
	(void)textureBindingsWithoutHash;
	(void)candidateCount;
	(void)firstMatchedHash;
#endif
}


bool FindDescriptorTextureOverrides(
	ID3D12GraphicsCommandList *commandList,
	const Bunny::CommandListTarget &target,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches,
	const std::vector<DX12CurrentShaderResourceBinding> *preResolvedBindings)
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
			const bool found = FilterDescriptorTextureOverrideCandidates(
				cachedCandidates->second,
				vertexCount,
				indexCount,
				instanceCount,
				firstVertex,
				firstIndex,
				firstInstance,
				matches);
			DX12LogDescriptorOverrideMatchStage(
				found ?
					DX12DescOverrideStage_CacheHitMatches :
					DX12DescOverrideStage_CacheHitEmpty,
				target, vertexCount, indexCount, 0, 0,
				cachedCandidates->second.size(),
				(found && matches && !matches->empty()) ?
					matches->front().matchedHash : 0);
			return found;
		}
	}

	const DX12CommandExecutionRuntimeSnapshot *snapshot =
		LoadTextureOverrideRuntimeSnapshot();
	if (!snapshot) {
		DX12LogDescriptorOverrideMatchStage(
			DX12DescOverrideStage_NoSnapshot,
			target, vertexCount, indexCount, 0, 0, 0, 0);
		return false;
	}
	if (!HasPotentialDescriptorTriggeredPrefilterCandidates(
			*snapshot,
			vertexCount,
			indexCount)) {
		if (canUseSerialCache) {
			if (tlsDescriptorCandidateCache.size() >= MaxTextureOverrideLookupCacheEntries)
				tlsDescriptorCandidateCache.clear();
			tlsDescriptorCandidateCache.emplace(cacheKey, std::vector<DX12TextureOverrideMatch>());
		}
		DX12LogDescriptorOverrideMatchStage(
			DX12DescOverrideStage_NoPrefilterCandidates,
			target, vertexCount, indexCount, 0, 0, 0, 0);
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
		DX12LogDescriptorOverrideMatchStage(
			DX12DescOverrideStage_NoAllowedSections,
			target, vertexCount, indexCount, 0, 0, 0, 0);
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
			DX12LogDescriptorOverrideMatchStage(
				DX12DescOverrideStage_BindingsQueryFailed,
				target, vertexCount, indexCount, 0, 0, 0, 0);
			return false;
		}
	}
	if (!bindingsView || bindingsView->empty()) {
		if (canUseSerialCache) {
			if (tlsDescriptorCandidateCache.size() >= MaxTextureOverrideLookupCacheEntries)
				tlsDescriptorCandidateCache.clear();
			tlsDescriptorCandidateCache.emplace(cacheKey, std::vector<DX12TextureOverrideMatch>());
		}
		DX12LogDescriptorOverrideMatchStage(
			DX12DescOverrideStage_BindingsEmpty,
			target, vertexCount, indexCount, 0, 0, 0, 0);
		return false;
	}

	std::vector<DX12TextureOverrideMatch> candidates;
	uint32_t textureBindingsWithoutHash = 0;
	for (size_t index = 0; index < bindingsView->size(); ++index) {
		const DX12CurrentShaderResourceBinding &binding = (*bindingsView)[index];
		if (!DescriptorBindingMatchesTarget(binding, target))
			continue;
		// HashDescriptorBindingResource is called once per surviving binding, so
		// the per-index memo array that used to hold this value was written and
		// read inside the same iteration and never consulted again.
		const uint32_t resourceHash = HashDescriptorBindingResource(binding);
#if DX12_TIMING_BUILD
		// Scene-switch diagnostic counter: target-matching texture bindings
		// whose resource has no published content hash. texNoHash>0 on a
		// computed_no_match stage is the signature of the hash pipeline
		// (not the matching pipeline) losing the scene's textures.
		if (!resourceHash && binding.hasDescriptor &&
			binding.descriptor.hasResourceDesc &&
			binding.descriptor.resource &&
			binding.descriptor.resourceDesc.Dimension ==
				D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			++textureBindingsWithoutHash;
#endif
		const uint32_t descriptorHash =
			HashDescriptorBinding(commandList, cachedDevice, binding);
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

	const bool found = FilterDescriptorTextureOverrideCandidates(
		candidates,
		vertexCount,
		indexCount,
		instanceCount,
		firstVertex,
		firstIndex,
		firstInstance,
		matches);
	DX12LogDescriptorOverrideMatchStage(
		found ?
			DX12DescOverrideStage_ComputedMatched :
			DX12DescOverrideStage_ComputedNoMatch,
		target, vertexCount, indexCount,
		bindingsView ? bindingsView->size() : 0,
		textureBindingsWithoutHash,
		candidates.size(),
		(found && matches && !matches->empty()) ?
			matches->front().matchedHash : 0);
	return found;
}


void AppendTextureOverrideMatches(
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


// Section-name ordering used by SortTextureOverrideMatches. Comparing the
// lowered characters pairwise is exactly `ToLower(lhs) < ToLower(rhs)`
// (IniDocumentText::ToLower only maps each character through towlower), but it
// does not allocate a lowered copy of both section names per comparison - the
// comparator previously paid two heap allocations for every one of the
// O(n log n) comparisons, and section names always exceed the SSO buffer.
static bool LowerSectionLess(const std::wstring &lhs, const std::wstring &rhs)
{
	const size_t shared = lhs.size() < rhs.size() ? lhs.size() : rhs.size();
	for (size_t index = 0; index < shared; ++index) {
		const wchar_t left = static_cast<wchar_t>(std::towlower(lhs[index]));
		const wchar_t right = static_cast<wchar_t>(std::towlower(rhs[index]));
		if (left != right)
			return left < right;
	}
	return lhs.size() < rhs.size();
}


void SortTextureOverrideMatches(
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
			if (LowerSectionLess(lhs.config->section, rhs.config->section))
				return true;
			if (LowerSectionLess(rhs.config->section, lhs.config->section))
				return false;
			return lhs.matchedHash > rhs.matchedHash;
		});
}


bool FindTargetTextureOverride(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState, const Bunny::CommandListTarget &target,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches,
	const std::vector<DX12CurrentShaderResourceBinding> *preResolvedGraphicsSrvs)
{
	if (matches)
		matches->clear();
	if (target.kind == Bunny::CommandListTargetKind::IndexBuffer) {
		if (!iaState.hasIndexBuffer) {
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
		return found;
	}

	if (target.kind == Bunny::CommandListTargetKind::VertexBuffer) {
		const DX12IaBufferHash *slot = FindIaVertexSlot(iaState, target.slot);
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

	// Reuse one scratch vector across the index-buffer probe and all
	// vertex-buffer slot probes so the merged prefilter buckets do not
	// allocate a fresh buffer for every slot.
	std::vector<size_t> prefilterScratch;

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
			&prefilterScratch)) {
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
				&prefilterScratch)) {
			return true;
		}
	}

	return false;
}


// Lookup in the reload-built hash index. With count-sensitive tables disabled,
// these buckets contain the complete count-independent candidate set. Both
// callers fail open when count-sensitive tables are enabled.
static const std::vector<size_t> *FindIaHashCandidateIndexes(
	bool indexBuffer, uint32_t vertexSlot, uint32_t hash)
{
	// Shared by the dictionary-only rejection and the full match_cs verdict.
	// A merged bucket supersedes (rather than supplements) the any-slot bucket.
	const std::vector<size_t> *candidateIndexes = nullptr;
	if (indexBuffer) {
		auto found = gIaIndexTextureOverrideIndex.find(hash);
		if (found != gIaIndexTextureOverrideIndex.end())
			candidateIndexes = &found->second;
	} else {
		auto merged = gIaMergedVertexTextureOverrideIndex.find(
			MakeIaVertexIndexKey(hash, vertexSlot));
		if (merged != gIaMergedVertexTextureOverrideIndex.end()) {
			candidateIndexes = &merged->second;
		} else {
			auto anySlot = gIaAnyVertexTextureOverrideIndex.find(hash);
			if (anySlot != gIaAnyVertexTextureOverrideIndex.end())
				candidateIndexes = &anySlot->second;
		}
	}
	return candidateIndexes;
}

bool DX12ModIaSnapshotDefinitelyAbsent(const DX12IaStateNegativeHashSnapshot &hashes)
{
	if (gIaPrefilterCountSensitive != 0)
		return false;
	if (hashes.indexHashValid) {
		const auto *candidates = FindIaHashCandidateIndexes(true, 0, hashes.indexHash);
		if (candidates && !candidates->empty())
			return false;
	}
	for (UINT mask = hashes.vertexHashMask; mask; mask &= mask - 1) {
		const UINT slot = std::countr_zero(mask);
		const auto *candidates = FindIaHashCandidateIndexes(false, slot, hashes.vertexHashes[slot]);
		if (candidates && !candidates->empty())
			return false;
	}
	return true;
}

bool IaHashDefinitelyHasNoCandidate(
	bool indexBuffer, uint32_t vertexSlot, uint32_t hash)
{
	if (gIaPrefilterCountSensitive != 0)
		return false;
	const auto *candidateIndexes = FindIaHashCandidateIndexes(indexBuffer, vertexSlot, hash);
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


// Count-independent prefilter probe on its own: "could this binding match any
// override at all?". Used as a gate in front of the shared IA view hash resolve,
// so it deliberately mirrors IaHashDefinitelyHasNoCandidate's own lookup
// arguments (the same counts and the same scratch shape), otherwise the gate
// could disagree with the probe it is meant to short-circuit.
//
// Emptiness-only form of FindIaTextureOverridePrefilterCandidatesLocked with
// vertexCount/indexCount pinned to 0. That function's scratch form copies the
// per-count bucket and appends the generic list only so the caller can test the
// result for emptiness; every call therefore built and tore down a temporary
// vector (and reallocated it whenever a per-count bucket existed) to answer a
// question that is just "is either of those two lists non-empty". Lookup order
// and the "exact-count bucket plus generic list" semantics are preserved.
static bool IaHasAnyPrefilterCandidate(bool indexBuffer, uint32_t vertexSlot)
{
	if (indexBuffer) {
		const std::unordered_map<uint32_t, std::vector<size_t>>::const_iterator
			indexedByCount =
				gIaIndexTextureOverridePrefilterByIndexCount.find(0);
		if (indexedByCount != gIaIndexTextureOverridePrefilterByIndexCount.end() &&
		    !indexedByCount->second.empty())
			return true;
		return !gIaIndexTextureOverridePrefilterIndex.empty();
	}

	const std::unordered_map<UINT64, std::vector<size_t>>::const_iterator
		mergedByCount =
			gIaMergedVertexTextureOverridePrefilterByVertexCount.find(
				MakeIaVertexIndexKey(0, vertexSlot));
	if (mergedByCount != gIaMergedVertexTextureOverridePrefilterByVertexCount.end()) {
		if (!mergedByCount->second.empty())
			return true;
		const std::unordered_map<uint32_t, std::vector<size_t>>::const_iterator
			mergedGeneric =
				gIaMergedVertexTextureOverridePrefilterIndex.find(vertexSlot);
		return mergedGeneric != gIaMergedVertexTextureOverridePrefilterIndex.end() &&
			!mergedGeneric->second.empty();
	}

	const std::unordered_map<uint32_t, std::vector<size_t>>::const_iterator
		anySlotByCount =
			gIaAnyVertexTextureOverridePrefilterByVertexCount.find(0);
	if (anySlotByCount != gIaAnyVertexTextureOverridePrefilterByVertexCount.end()) {
		if (!anySlotByCount->second.empty())
			return true;
		return !gIaAnyVertexTextureOverridePrefilterIndex.empty();
	}

	const std::unordered_map<uint32_t, std::vector<size_t>>::const_iterator merged =
		gIaMergedVertexTextureOverridePrefilterIndex.find(vertexSlot);
	if (merged != gIaMergedVertexTextureOverridePrefilterIndex.end())
		return !merged->second.empty();

	if (!gIaAnyVertexTextureOverridePrefilterIndex.empty())
		return true;

	const std::unordered_map<uint32_t, std::vector<size_t>>::const_iterator specific =
		gIaVertexTextureOverridePrefilterIndex.find(vertexSlot);
	return specific != gIaVertexTextureOverridePrefilterIndex.end() &&
		!specific->second.empty();
}


// Build the hash inputs that the IA-state-level negative verdict actually
// probes. The prefilter gates are stable for a reload generation, so doing this
// before the resident-cache lookup lets the cache key on validated hash values
// instead of the coarse global resolve epoch.
void DX12ModBuildIaStateNegativeHashSnapshot(
	const DX12ActiveIaState &ia,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	DX12IaStateNegativeHashSnapshot *snapshot)
{
	if (!snapshot)
		return;
	*snapshot = DX12IaStateNegativeHashSnapshot();
	if (!targetPlan.hasTextureOverrideChecks ||
	    targetPlan.hasDescriptorTargets ||
	    !targetPlan.HasIaTargets() ||
	    DX12ModIaPrefilterCountSensitive())
		return;

	const UINT64 resolveGeneration = DX12GetIaHashResolveGeneration();
	const bool probeIndexBuffer =
		targetPlan.RequiresIndexBuffer(indexed) && ia.hasIndexBuffer;
	if (probeIndexBuffer && IaHasAnyPrefilterCandidate(true, 0)) {
		snapshot->indexHashValid = true;
		snapshot->indexHash =
			ia.indexBufferHashValid &&
			ia.indexBufferHashResolveGeneration == resolveGeneration ?
				ia.indexBufferHash :
				DX12CommandListRuntime::GetOrBuildCachedIndexViewHash(
					const_cast<DX12ActiveIaState*>(&ia));
	}
	for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
		if (!targetPlan.RequiresVertexBuffer(slot) || !ia.hasVertexBuffer[slot])
			continue;
		if (!IaHasAnyPrefilterCandidate(false, slot))
			continue;
		snapshot->vertexHashes[slot] =
			ia.vertexBufferHashValid[slot] &&
			ia.vertexBufferHashResolveGeneration[slot] == resolveGeneration ?
				ia.vertexBufferHashes[slot] :
				DX12CommandListRuntime::GetOrBuildCachedVertexViewHash(
					const_cast<DX12ActiveIaState*>(&ia), slot);
		snapshot->vertexHashMask |= 1u << slot;
	}
}


// DX11-style IA-state-level negative check: the per-view hashes are computed
// (or reused from the resident cache) and matched against the candidate tables
// without any draw-context filtering. Only call when gIaPrefilterCountSensitive
// is 0; the result is then independent of vertex/index counts and offsets, so
// the draw path can cache it across draws for the same IA bindings.
bool DX12ModIaStateDefinitelyHasNoCandidate(
	const DX12ActiveIaState &ia,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	const DX12IaStateNegativeHashSnapshot *precomputedHashes)
{
	if (!targetPlan.hasTextureOverrideChecks ||
	    targetPlan.hasDescriptorTargets ||
	    !targetPlan.HasIaTargets()) {
		return false;
	}
	// requireAllIaBuffers is handled below: every bound buffer is probed
	// against the candidate table, so an all-IA plan still resolves to
	// "definitely no candidate" when none of the bound views match.
	DX12IaStateNegativeHashSnapshot computedHashes;
	const DX12IaStateNegativeHashSnapshot *hashes = precomputedHashes;
	if (!hashes) {
		DX12_PROF_STAGE_SCOPE(IaNegVerdictHashes);
		DX12ModBuildIaStateNegativeHashSnapshot(
			ia, targetPlan, indexed, &computedHashes);
		hashes = &computedHashes;
	}
	const bool indexHasCandidates = hashes->indexHashValid;
	const uint32_t vertexSlotMask = hashes->vertexHashMask;

	// Every required binding was prefiltered away, which is the whole verdict.
	if (!indexHasCandidates && !vertexSlotMask) {
		DX12_PROF_COUNT("Rm.IaNegVerdictGated");
		return true;
	}
	{
		DX12_PROF_STAGE_SCOPE(IaNegVerdictProbes);
		if (indexHasCandidates &&
		    !IaHashDefinitelyHasNoCandidate(true, 0, hashes->indexHash))
			return false;
		for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
			if ((vertexSlotMask & (1u << slot)) == 0)
				continue;
			if (!IaHashDefinitelyHasNoCandidate(
					false, slot, hashes->vertexHashes[slot]))
				return false;
		}
	}

	return true;
}


bool DX12ModIaPrefilterCountSensitive()
{
	return gIaPrefilterCountSensitive != 0;
}


bool PrefilterSrvCacheTryLoad(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT stageIndex,
	UINT64 reloadGeneration,
	std::vector<DX12ResolvedGraphicsSrvBinding> *outHashed)
{
	PrefilterSrvResolutionCacheEntry &cache = tlsPrefilterSrvResolutionCache;
	if (!cache.valid || !outHashed || !runtimeState ||
	    cache.commandList != commandList ||
	    cache.reloadGeneration != reloadGeneration ||
	    cache.textureHashGeneration != DX12GetTextureHashGeneration() ||
	    cache.graphicsRootSignature != runtimeState->graphicsRootSignature)
		return false;

	// Root tables must still hold the exact GPU handles observed at fill.
	for (UINT s = 0; s < 2; ++s) {
		for (const DX12CurrentShaderResourceBinding &binding : cache.bindings[s]) {
			if (binding.rootParameterIndex >= MaxRootParameters)
				return false;
			const RootTableState &table =
				runtimeState->graphicsRootTables[binding.rootParameterIndex];
			if (!table.valid ||
			    table.baseDescriptor.ptr != binding.tableGpuStart.ptr)
				return false;
		}
	}

	// Observed descriptor slots must still hold the same published content.
	// The query-serial gate skips the slot reads entirely when no descriptor
	// write happened since the fill (the common case between draws).
	if (!cache.observedSlots.empty()) {
		const UINT64 serialBefore = CurrentDescriptorQuerySerial();
		if (cache.descriptorQuerySerial != serialBefore) {
			if (!cache.observedHeap)
				return false;
			constexpr size_t StackIdentityCount = 64;
			std::array<UINT64, StackIdentityCount> stackVersions;
			std::array<UINT64, StackIdentityCount> stackValueIds;
			static thread_local std::vector<UINT64> tlsVersions;
			static thread_local std::vector<UINT64> tlsValueIds;
			UINT64 *versions = stackVersions.data();
			UINT64 *valueIds = stackValueIds.data();
			const size_t count = cache.observedSlots.size();
			if (count > StackIdentityCount) {
				tlsVersions.resize(count);
				tlsValueIds.resize(count);
				versions = tlsVersions.data();
				valueIds = tlsValueIds.data();
			}
			if (!DX12GetDescriptorSlotIdentities(
					cache.observedHeap,
					cache.observedSlots.data(),
					count,
					versions,
					valueIds))
				return false;
			for (size_t index = 0; index < count; ++index) {
				if (versions[index] != cache.observedSlotVersions[index] ||
				    valueIds[index] != cache.observedSlotValueIds[index])
					return false;
			}
			const UINT64 serialAfter = CurrentDescriptorQuerySerial();
			if (serialAfter != serialBefore)
				return false;
			cache.descriptorQuerySerial = serialAfter;
		}
	}

	*outHashed = cache.hashed[stageIndex];
	return true;
}


void PrefilterSrvCacheStore(
	ID3D12GraphicsCommandList *commandList,
	DX12SimulateContext *runtimeState,
	UINT stageIndex,
	UINT64 reloadGeneration,
	const std::vector<DX12CurrentShaderResourceBinding> &bindings,
	const std::vector<DX12ResolvedGraphicsSrvBinding> &hashed)
{
	PrefilterSrvResolutionCacheEntry &cache = tlsPrefilterSrvResolutionCache;
	cache.valid = false;
	if (!runtimeState || bindings.empty() || hashed.empty())
		return;

	cache.bindings[stageIndex] = bindings;
	cache.hashed[stageIndex] = hashed;
	cache.commandList = commandList;
	cache.reloadGeneration = reloadGeneration;
	cache.textureHashGeneration = DX12GetTextureHashGeneration();
	cache.graphicsRootSignature = runtimeState->graphicsRootSignature;
	cache.descriptorQuerySerial = CurrentDescriptorQuerySerial();
	cache.observedHeap = nullptr;
	cache.observedSlots.clear();
	cache.observedSlotVersions.clear();
	cache.observedSlotValueIds.clear();

	// Cache only when every binding can be identity-validated: table-based
	// bindings with a resolvable CPU handle and a published slot identity.
	// Any binding that fails (inline root descriptor, unresolvable handle,
	// unpublished slot, multiple heaps) disables caching for the entry.
	ID3D12DescriptorHeap *observedHeap = nullptr;
	for (const DX12CurrentShaderResourceBinding &binding : bindings) {
		if (binding.rootDescriptor || binding.tableCpuStart == 0 ||
		    binding.descriptorIncrement == 0)
			return;
		const SIZE_T cpuHandle = binding.tableCpuStart +
			static_cast<SIZE_T>(binding.descriptorOffset) *
				binding.descriptorIncrement;
		DX12DescriptorHeapShadowUtils::ResolvedHeapSlot resolved;
		if (!DX12DescriptorHeapShadowUtils::ResolveHeapSlotFast(
				cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, &resolved) ||
		    !resolved.heap || !resolved.heap->stateLocks)
			return;
		DescriptorHeapRecord::StateLocks::SlotIdentity identity;
		if (!resolved.heap->stateLocks->ObserveSlotIdentity(
				resolved.slot, &identity) ||
		    identity.valueId == 0)
			return;
		if (!observedHeap) {
			observedHeap = resolved.heap->heap;
		} else if (observedHeap != resolved.heap->heap) {
			return;
		}
		cache.observedSlots.push_back(resolved.slot);
		cache.observedSlotVersions.push_back(identity.version);
		cache.observedSlotValueIds.push_back(identity.valueId);
	}
	if (!observedHeap || cache.observedSlots.empty())
		return;
	cache.observedHeap = observedHeap;
	cache.valid = true;
}


void ResolveGraphicsSrvBindings(
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


bool TryLoadResolvedGraphicsSrvBindings(
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


void StoreResolvedGraphicsSrvBindings(
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


void FillSyntheticTextureBinding(
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



bool FindDescriptorTextureOverridesFromResolvedBindings(
	const DX12CommandExecutionRuntimeSnapshot *snapshot,
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
			indexCount)) {
		DX12LogResolvedOverrideCheck(
			DX12DescOverrideStage_NoPrefilterCandidates,
			target, &resolvedBindings, 0, false, nullptr);
		return false;
	}

	static thread_local std::vector<uint32_t> tlsAllowedSectionIds;
	CollectResolvedDescriptorPrefilterSectionIds(
		*snapshot,
		vertexCount,
		indexCount,
		&tlsAllowedSectionIds);
	if (tlsAllowedSectionIds.empty()) {
		DX12LogResolvedOverrideCheck(
			DX12DescOverrideStage_NoAllowedSections,
			target, &resolvedBindings, 0, false, nullptr);
		return false;
	}

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

	const bool found = FilterDescriptorTextureOverrideCandidates(
		candidates,
		vertexCount,
		indexCount,
		instanceCount,
		firstVertex,
		firstIndex,
		firstInstance,
		matches);
	DX12LogResolvedOverrideCheck(
		found ?
			DX12DescOverrideStage_ComputedMatched :
			DX12DescOverrideStage_ComputedNoMatch,
		target, &resolvedBindings, candidates.size(), found, matches);
	return found;
}


void AppendGraphicsSrvTextureOverrideCandidatesForTarget(
	const DX12CommandExecutionRuntimeSnapshot *snapshot,
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


void AppendResolvedBindingsToDescriptorTextureOverrideCache(
	const DX12CommandExecutionRuntimeSnapshot *snapshot,
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



void FillSyntheticBufferBinding(
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


bool BuildResolvedDescriptorBindingsForResourceOverride(
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
		DX12MarkStage("drawsrv_resolve");
#if DX12_TIMING_BUILD
		// LogFlush on purpose: the first draw-time SRV load of a session
		// does file I/O + GPU upload on the recording thread; if it deadlocks,
		// TLS-buffered log lines would never publish and the trail would
		// end without naming the resource.
		DX12_LOG_FLUSH("DX12DrawSrvReplacement",
			"stage=resolve_enter,resource=%S", resourceName.c_str());
#endif
		DX12LoadedResource *textureResource =
			DX12DdsTextureLoadUtils::EnsureLoadedDrawTextureSrv(
				device, commandList, resourceName);
		DX12MarkStage("drawsrv_resolved");
		DX12_LOG_FLUSH("DX12DrawSrvReplacement",
			"stage=resolve_done,resource=%S,ok=%d",
			resourceName.c_str(),
			(textureResource && textureResource->drawSrvCpu.ptr) ? 1 : 0);
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
