// DX12ModRuntimeLoad.cpp: mod runtime ini load/reload, config-driven index builders and runtime snapshots.
// Split from DX12ModRuntime.cpp; implementation unit for its module.
#include "DX12ModRuntimeInternal.h"

DXGI_FORMAT ParseDx12ResourceFormat(const std::wstring &format)
{
	std::wstring value = Bunny::IniDocumentText::ToLower(Bunny::IniDocumentText::Trim(format));
	if (value.empty())
		return DXGI_FORMAT_UNKNOWN;
	if (value.rfind(L"dxgi_format_", 0) == 0)
		value = value.substr(12);
	if (value == L"r16_uint")
		return DXGI_FORMAT_R16_UINT;
	if (value == L"r32_uint")
		return DXGI_FORMAT_R32_UINT;

	wchar_t *end = nullptr;
	unsigned long numeric = wcstoul(value.c_str(), &end, 10);
	if (end && *end == L'\0')
		return static_cast<DXGI_FORMAT>(numeric);
	return DXGI_FORMAT_UNKNOWN;
}


uint64_t HashCombine64(uint64_t hash, uint64_t value)
{
	hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
	return hash;
}


std::vector<DX12VertexLimitRaiseConfig> CollectVertexLimitRaiseConfigs(
	const Bunny::TextureOverrideMap &textureOverrides)
{
	std::vector<DX12VertexLimitRaiseConfig> configs;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (!config.hasVertexLimitRaise || !config.overrideByteWidth)
				continue;
			DX12VertexLimitRaiseConfig out;
			out.section = config.originalSection.empty() ? config.section : config.originalSection;
			out.hasMatchCs = config.hasMatchCs || config.hasCsHash;
			out.matchCs = config.hasCsHash ? config.csHash : config.matchCs;
			out.overrideByteStride = config.overrideByteStride;
			out.overrideVertexCount = config.overrideVertexCount;
			out.overrideByteWidth = config.overrideByteWidth;
			out.uavByteStride = config.uavByteStride;
			out.overrideNumElements = config.overrideNumElements;
			configs.push_back(out);
		}
	}
	return configs;
}


uint64_t GetPreSkinCsHash(const Bunny::TextureOverrideConfig &config)
{
	return config.hasCsHash ? config.csHash : config.matchCs;
}


bool IsComputePreSkinTextureOverride(const Bunny::TextureOverrideConfig &config)
{
	const bool hasCsHash = config.hasMatchCs || config.hasCsHash;
	return hasCsHash && !config.preSkinMatchCsSrvHashes.empty() &&
		!config.preSkinCsSrvResources.empty();
}


// Plan step 3.1: decide which TextureOverride sections are unconditional
// static texture replacements. A section qualifies when it only replaces the
// texture content (drawReplaceThisResource set), never observes the draw call
// (no draw-context match filters, no match_cs / pre-skin dependency), and runs
// no command lists or actions that could depend on the original texture.
bool TextureOverrideIsStaticReplacement(
	const Bunny::TextureOverrideConfig &config)
{
	if (config.drawReplaceThisResource.empty())
		return false;
	if (config.hasMatchVertexCount || config.hasMatchIndexCount ||
	    config.hasMatchInstanceCount || config.hasMatchFirstVertex ||
	    config.hasMatchFirstIndex || config.hasMatchFirstInstance ||
	    config.hasMatchCs || config.hasMatchUavBytes || config.hasCsHash ||
	    !config.preSkinMatchCsSrvHashes.empty() ||
	    !config.preSkinCsSrvResources.empty())
		return false;
	if (config.hasVertexLimitRaise)
		return false;
	if (!config.commandLists.pre.empty() ||
	    !config.commandLists.main.empty() ||
	    !config.commandLists.post.empty() ||
	    !config.actions.empty())
		return false;
	// Sections that also replace IB/VB or draw extra calls are not pure texture
	// swaps; keep those on the draw-time path.
	if (!config.indexBufferResource.empty() ||
	    !config.vertexBufferResources.empty() ||
	    !config.drawPsSrvResources.empty() ||
	    !config.drawVsSrvResources.empty())
		return false;
	return true;
}


void BuildDx12PreSkinTextureOverrideIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::vector<DX12PreSkinTextureOverrideCandidate> *preSkinTextureOverrides,
	std::unordered_map<uint32_t, std::vector<size_t>> *uavHashTextureOverrideIndex,
	std::unordered_map<uint64_t, std::vector<size_t>> *matchCsTextureOverrideIndex)
{
	if (!preSkinTextureOverrides || !uavHashTextureOverrideIndex ||
	    !matchCsTextureOverrideIndex)
		return;

	preSkinTextureOverrides->clear();
	uavHashTextureOverrideIndex->clear();
	matchCsTextureOverrideIndex->clear();
	size_t candidateCount = 0;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (IsComputePreSkinTextureOverride(config))
				++candidateCount;
		}
	}
	preSkinTextureOverrides->reserve(candidateCount);
	matchCsTextureOverrideIndex->reserve(candidateCount);
	UINT64 order = 0;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (!IsComputePreSkinTextureOverride(config))
				continue;

			DX12PreSkinTextureOverrideCandidate candidate;
			candidate.config = &config;
			candidate.hash = bucket.first;
			candidate.order = order++;
			candidate.identity = DX12PreSkinCandidateIdentityUtils::Build(
				config, bucket.first, candidate.order);
			const size_t index = preSkinTextureOverrides->size();
			preSkinTextureOverrides->push_back(std::move(candidate));
			(*matchCsTextureOverrideIndex)[GetPreSkinCsHash(config)].push_back(index);
		}
	}
}


void BuildDx12ExplicitMatchCsResourceIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::unordered_map<std::wstring, std::vector<std::wstring>> *resourceSections)
{
	if (!resourceSections)
		return;

	resourceSections->clear();
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (!config.hasMatchCs && !config.hasCsHash)
				continue;
			for (const auto &item : config.preSkinCsSrvResources) {
				if (item.second.empty())
					continue;
				(*resourceSections)[Bunny::IniDocumentText::ToLower(item.second)].push_back(config.section);
			}
		}
	}
}


bool HasPreSkinTextureOverrideCandidates(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::unordered_set<uint64_t> *matchCsHashes,
	bool *hasVlrWithoutMatchCs,
	bool *hasExplicitMatchCsUavBytes)
{
	if (matchCsHashes)
		matchCsHashes->clear();
	if (hasVlrWithoutMatchCs)
		*hasVlrWithoutMatchCs = false;
	if (hasExplicitMatchCsUavBytes)
		*hasExplicitMatchCsUavBytes = false;

	bool found = false;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (IsComputePreSkinTextureOverride(config)) {
				found = true;
				if (matchCsHashes)
					matchCsHashes->insert(GetPreSkinCsHash(config));
				continue;
			}
			if (config.hasVertexLimitRaise && config.overrideByteWidth) {
				found = true;
				if (hasVlrWithoutMatchCs)
					*hasVlrWithoutMatchCs = true;
			}
		}
	}
	return found;
}



bool ResourceBlockedByInactiveExplicitMatchCsLocked(
	const std::wstring &resourceName,
	const std::unordered_set<std::wstring> &activePreSkinSections)
{
	if (resourceName.empty())
		return false;

	auto indexed = gExplicitMatchCsResourceSections.find(Bunny::IniDocumentText::ToLower(resourceName));
	if (indexed == gExplicitMatchCsResourceSections.end())
		return false;

	for (const std::wstring &section : indexed->second) {
		if (activePreSkinSections.find(section) != activePreSkinSections.end())
			return false;
	}
	return true;
}


void BuildDx12VlrResourceCandidates(
	const Bunny::ResourceMap &resources,
	const std::unordered_map<std::wstring, std::vector<std::wstring>> &explicitMatchCsResourceSections,
	std::vector<std::wstring> *resourceCandidates)
{
	if (!resourceCandidates)
		return;

	resourceCandidates->clear();
	std::unordered_set<std::wstring> seen;
	for (const auto &item : resources) {
		if (!ResourceConfigLooksLikeBuffer(item.second))
			continue;
		if (explicitMatchCsResourceSections.find(Bunny::IniDocumentText::ToLower(item.first)) !=
		    explicitMatchCsResourceSections.end())
			continue;
		if (!seen.insert(item.first).second)
			continue;
		resourceCandidates->push_back(item.first);
	}
}


void DX12ModScheduleInitialLoad(const wchar_t *configPath)
{
	DX12ModLockTelemetryUtils::AcquireModExclusive(&gModLock);
	SetBasePaths(configPath);
	gPendingInitialConfigPath = configPath ? configPath : L"";
	gDeferredInitialModLoadStartTick = GetTickCount();
	InterlockedExchange(&gDeferredInitialModLoadState,
		gPendingInitialConfigPath.empty() ? 0 : 1);
	ReleaseSRWLockExclusive(&gModLock);
}


void DX12ModMaybeRunDeferredInitialLoad()
{
	// Diagnostic probe: log the state machine inputs on every call so a
	// "mod never loads" report can be traced without guessing.
	{
		DWORD probeElapsed = 0;
		LONG probeState = InterlockedCompareExchange(&gDeferredInitialModLoadState, 0, 0);
		DX12LogManager::Fields("DX12ModDeferredProbe",
			"\"state\":%ld,\"present\":%ld,\"elapsedMs\":%lu,\"delayMs\":%lu",
			probeState,
			DX12RuntimeState::GetPresentCount(),
			probeElapsed = GetTickCount() - gDeferredInitialModLoadStartTick,
			gDeferredInitialModLoadDelayMs);
	}
	if (InterlockedCompareExchange(&gDeferredInitialModLoadState, 2, 1) != 1)
		return;

	std::wstring configPath;
	DWORD delayMs = 0;
	DWORD startTick = 0;
	DX12ModLockTelemetryUtils::AcquireModShared(&gModLock);
	configPath = gPendingInitialConfigPath;
	delayMs = gDeferredInitialModLoadDelayMs;
	startTick = gDeferredInitialModLoadStartTick;
	ReleaseSRWLockShared(&gModLock);

	if (configPath.empty()) {
		InterlockedExchange(&gDeferredInitialModLoadState, 0);
		return;
	}

	const LONG present = DX12RuntimeState::GetPresentCount();
	const DWORD elapsedMs = GetTickCount() - startTick;
	if (present <= 2 || elapsedMs < delayMs) {
		InterlockedExchange(&gDeferredInitialModLoadState, 1);
		return;
	}

	DX12LogManager::Fields("DX12ModRuntimeDeferredLoad",
		"\"status\":\"begin\",\"path\":\"%S\",\"present\":%ld,\"elapsedMs\":%lu,\"delayMs\":%lu",
		configPath.c_str(), present, elapsedMs, delayMs);

	DX12OverlayStatusState::SetStatus(L"DX12 startup reload running");
	const bool ok = DX12ModRuntimeReload();
	if (ok)
		DX12ModRefreshRuntimeStateAfterReload();

	DX12ModLockTelemetryUtils::AcquireModExclusive(&gModLock);
	if (ok)
		gPendingInitialConfigPath.clear();
	else
		gPendingInitialConfigPath = configPath;
	ReleaseSRWLockExclusive(&gModLock);

	DX12LogManager::Fields("DX12ModRuntimeDeferredLoad",
		"\"status\":\"done\",\"result\":\"%s\",\"present\":%ld,\"elapsedMs\":%lu",
		ok ? "ok" : "failed",
		DX12RuntimeState::GetPresentCount(),
		GetTickCount() - startTick);
	InterlockedExchange(&gDeferredInitialModLoadState, ok ? 0 : 1);
}


bool ReadFileBytes(const wchar_t *path, std::vector<unsigned char> *data)
{
	if (!path || !data)
		return false;

	FILE *file = _wfsopen(path, L"rb", _SH_DENYNO);
	if (!file)
		return false;

	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return false;
	}
	long size = ftell(file);
	if (size <= 0) {
		fclose(file);
		return false;
	}
	if (fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return false;
	}

	data->resize(static_cast<size_t>(size));
	size_t read = fread(data->data(), 1, data->size(), file);
	fclose(file);
	if (read != data->size()) {
		data->clear();
		return false;
	}
	return true;
}


void DX12ModSetSafeMode(bool safeMode)
{
	InterlockedExchange(&gDx12SafeMode, safeMode ? 1 : 0);
}


bool IsVlrBufferResizeCandidate(
	const D3D12_RESOURCE_DESC &desc, const DX12VertexLimitRaiseConfig &vlr)
{
	if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || desc.Width == 0)
		return false;
	if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
		return false;
	if (desc.Width >= vlr.overrideByteWidth)
		return false;

	const UINT64 tolerance = std::max<UINT64>(
		static_cast<UINT64>(vlr.overrideByteStride) * 4096,
		vlr.overrideByteWidth / 4);
	if (desc.Width + tolerance < vlr.overrideByteWidth)
		return false;

	return true;
}


bool AdjustBufferResourceDescCommon(D3D12_RESOURCE_DESC *desc, const char *source)
{
	if (!desc)
		return false;
	if (gHasVertexLimitRaiseConfigs == 0)
		return false;

	DX12VertexLimitRaiseConfig match;
	bool found = false;
	if (!TryAcquireSRWLockShared(&gModLock))
		return false;
	for (const DX12VertexLimitRaiseConfig &vlr : gVertexLimitRaiseConfigs) {
		if (!IsVlrBufferResizeCandidate(*desc, vlr))
			continue;
		if (!found || vlr.overrideByteWidth > match.overrideByteWidth) {
			match = vlr;
			found = true;
		}
	}
	ReleaseSRWLockShared(&gModLock);

	if (!found)
		return false;

	desc->Width = match.overrideByteWidth;
	return true;
}


bool DX12ModAdjustBufferResourceDesc(D3D12_RESOURCE_DESC *desc, const char *source)
{
	return AdjustBufferResourceDescCommon(desc, source);
}


bool DX12ModAdjustBufferResourceDesc1(D3D12_RESOURCE_DESC1 *desc, const char *source)
{
	if (!desc)
		return false;
	D3D12_RESOURCE_DESC desc0 = {};
	desc0.Dimension = desc->Dimension;
	desc0.Alignment = desc->Alignment;
	desc0.Width = desc->Width;
	desc0.Height = desc->Height;
	desc0.DepthOrArraySize = desc->DepthOrArraySize;
	desc0.MipLevels = desc->MipLevels;
	desc0.Format = desc->Format;
	desc0.SampleDesc = desc->SampleDesc;
	desc0.Layout = desc->Layout;
	desc0.Flags = desc->Flags;
	if (!AdjustBufferResourceDescCommon(&desc0, source))
		return false;
	desc->Width = desc0.Width;
	return true;
}


bool CommandListHasRuntimeEffect(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting)
{
	if (!effectCache || !visiting)
		return true;

	auto cached = effectCache->find(name);
	if (cached != effectCache->end())
		return cached->second;
	if (!visiting->insert(name).second)
		return true;

	bool effect = false;
	auto it = commandLists.find(name);
	if (it != commandLists.end()) {
		for (const Bunny::CommandListAction &action : it->second.actions) {
			if (CommandListActionHasRuntimeEffect(
				    action, commandLists, effectCache, visiting)) {
				effect = true;
				break;
			}
		}
	}
	visiting->erase(name);
	(*effectCache)[name] = effect;
	return effect;
}


bool CommandListActionHasTextureOverrideTrigger(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting)
{
	switch (action.kind) {
	case Bunny::CommandListActionKind::Run:
		return CommandListHasTextureOverrideTrigger(
			action.commandList, commandLists, triggerCache, visiting);
	case Bunny::CommandListActionKind::CheckTextureOverride:
		return true;
	default:
		return false;
	}
}


bool CommandListHasTextureOverrideTrigger(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting)
{
	if (!triggerCache || !visiting)
		return false;

	auto cached = triggerCache->find(name);
	if (cached != triggerCache->end())
		return cached->second;
	if (!visiting->insert(name).second)
		return false;

	bool triggered = false;
	auto it = commandLists.find(name);
	if (it != commandLists.end()) {
		for (const Bunny::CommandListAction &action : it->second.actions) {
			if (CommandListActionHasTextureOverrideTrigger(
				    action, commandLists, triggerCache, visiting)) {
				triggered = true;
				break;
			}
		}
	}
	visiting->erase(name);
	(*triggerCache)[name] = triggered;
	return triggered;
}


bool CommandListLinksHaveTextureOverrideTrigger(
	const std::vector<std::wstring> &links,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	std::set<std::wstring> visiting;
	for (const std::wstring &link : links) {
		if (CommandListHasTextureOverrideTrigger(
			    link, commandLists, triggerCache, &visiting))
			return true;
	}
	return false;
}


bool TargetNeedsDescriptorBindings(const Bunny::CommandListTarget &target)
{
	return target.kind == Bunny::CommandListTargetKind::ConstantBuffer ||
		target.kind == Bunny::CommandListTargetKind::ShaderResource ||
		target.kind == Bunny::CommandListTargetKind::UnorderedAccessView;
}


bool TargetNeedsIaBindings(const Bunny::CommandListTarget &target)
{
	return target.kind == Bunny::CommandListTargetKind::IndexBuffer ||
		target.kind == Bunny::CommandListTargetKind::VertexBuffer;
}


bool CommandListHasDescriptorTextureOverrideTrigger(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting)
{
	if (!triggerCache || !visiting)
		return false;

	auto cached = triggerCache->find(name);
	if (cached != triggerCache->end())
		return cached->second;
	if (!visiting->insert(name).second)
		return false;

	bool triggered = false;
	auto it = commandLists.find(name);
	if (it != commandLists.end()) {
		for (const Bunny::CommandListAction &action : it->second.actions) {
			if (CommandListActionHasDescriptorTextureOverrideTrigger(
				    action, commandLists, triggerCache, visiting)) {
				triggered = true;
				break;
			}
		}
	}
	visiting->erase(name);
	(*triggerCache)[name] = triggered;
	return triggered;
}


bool CommandListHasIaTextureOverrideTrigger(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting)
{
	if (!triggerCache || !visiting)
		return false;

	auto cached = triggerCache->find(name);
	if (cached != triggerCache->end())
		return cached->second;
	if (!visiting->insert(name).second)
		return false;

	bool triggered = false;
	auto it = commandLists.find(name);
	if (it != commandLists.end()) {
		for (const Bunny::CommandListAction &action : it->second.actions) {
			if (CommandListActionHasIaTextureOverrideTrigger(
				    action, commandLists, triggerCache, visiting)) {
				triggered = true;
				break;
			}
		}
	}
	visiting->erase(name);
	(*triggerCache)[name] = triggered;
	return triggered;
}


bool CommandListActionHasDescriptorTextureOverrideTrigger(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting)
{
	switch (action.kind) {
	case Bunny::CommandListActionKind::Run:
		return CommandListHasDescriptorTextureOverrideTrigger(
			action.commandList, commandLists, triggerCache, visiting);
	case Bunny::CommandListActionKind::CheckTextureOverride:
		return TargetNeedsDescriptorBindings(action.target);
	default:
		return false;
	}
}


bool CommandListActionHasIaTextureOverrideTrigger(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache,
	std::set<std::wstring> *visiting)
{
	switch (action.kind) {
	case Bunny::CommandListActionKind::Run:
		return CommandListHasIaTextureOverrideTrigger(
			action.commandList, commandLists, triggerCache, visiting);
	case Bunny::CommandListActionKind::CheckTextureOverride:
		return TargetNeedsIaBindings(action.target);
	default:
		return false;
	}
}


bool CommandListLinksHaveDescriptorTextureOverrideTrigger(
	const std::vector<std::wstring> &links,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	std::set<std::wstring> visiting;
	for (const std::wstring &link : links) {
		if (CommandListHasDescriptorTextureOverrideTrigger(
			    link, commandLists, triggerCache, &visiting))
			return true;
	}
	return false;
}


bool CommandListLinksHaveIaTextureOverrideTrigger(
	const std::vector<std::wstring> &links,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	std::set<std::wstring> visiting;
	for (const std::wstring &link : links) {
		if (CommandListHasIaTextureOverrideTrigger(
			    link, commandLists, triggerCache, &visiting))
			return true;
	}
	return false;
}


bool ShaderOverrideHasTextureOverrideTrigger(
	const Bunny::ShaderOverrideConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	std::set<std::wstring> visiting;
	for (const Bunny::CommandListAction &action : config.actions) {
		if (CommandListActionHasTextureOverrideTrigger(
			    action, commandLists, triggerCache, &visiting))
			return true;
	}
	return CommandListLinksHaveTextureOverrideTrigger(
		config.commandLists.pre, commandLists, triggerCache) ||
		CommandListLinksHaveTextureOverrideTrigger(
			config.commandLists.main, commandLists, triggerCache) ||
		CommandListLinksHaveTextureOverrideTrigger(
			config.commandLists.post, commandLists, triggerCache);
}


bool ShaderOverridesHaveTextureOverrideTrigger(
	const Bunny::ShaderOverrideMap &shaderOverrides,
	const Bunny::CommandListMap &commandLists)
{
	std::unordered_map<std::wstring, bool> triggerCache;
	for (const auto &item : shaderOverrides) {
		if (ShaderOverrideHasTextureOverrideTrigger(
			    item.second, commandLists, &triggerCache))
			return true;
	}
	return false;
}


bool ShaderOverrideHasDescriptorTextureOverrideTrigger(
	const Bunny::ShaderOverrideConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	std::set<std::wstring> visiting;
	for (const Bunny::CommandListAction &action : config.actions) {
		if (CommandListActionHasDescriptorTextureOverrideTrigger(
			    action, commandLists, triggerCache, &visiting))
			return true;
	}
	return CommandListLinksHaveDescriptorTextureOverrideTrigger(
		config.commandLists.pre, commandLists, triggerCache) ||
		CommandListLinksHaveDescriptorTextureOverrideTrigger(
			config.commandLists.main, commandLists, triggerCache) ||
		CommandListLinksHaveDescriptorTextureOverrideTrigger(
			config.commandLists.post, commandLists, triggerCache);
}


bool ShaderOverridesHaveDescriptorTextureOverrideTrigger(
	const Bunny::ShaderOverrideMap &shaderOverrides,
	const Bunny::CommandListMap &commandLists)
{
	std::unordered_map<std::wstring, bool> triggerCache;
	for (const auto &item : shaderOverrides) {
		if (ShaderOverrideHasDescriptorTextureOverrideTrigger(
			    item.second, commandLists, &triggerCache))
			return true;
	}
	return false;
}


bool ShaderOverrideHasIaTextureOverrideTrigger(
	const Bunny::ShaderOverrideConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	std::set<std::wstring> visiting;
	for (const Bunny::CommandListAction &action : config.actions) {
		if (CommandListActionHasIaTextureOverrideTrigger(
			    action, commandLists, triggerCache, &visiting))
			return true;
	}
	return CommandListLinksHaveIaTextureOverrideTrigger(
		config.commandLists.pre, commandLists, triggerCache) ||
		CommandListLinksHaveIaTextureOverrideTrigger(
			config.commandLists.main, commandLists, triggerCache) ||
		CommandListLinksHaveIaTextureOverrideTrigger(
			config.commandLists.post, commandLists, triggerCache);
}


bool ShaderOverridesHaveIaTextureOverrideTrigger(
	const Bunny::ShaderOverrideMap &shaderOverrides,
	const Bunny::CommandListMap &commandLists)
{
	std::unordered_map<std::wstring, bool> triggerCache;
	for (const auto &item : shaderOverrides) {
		if (ShaderOverrideHasIaTextureOverrideTrigger(
			    item.second, commandLists, &triggerCache))
			return true;
	}
	return false;
}


bool ShaderRegexHasTextureOverrideTrigger(
	const Bunny::ShaderRegexConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	if (config.hasPattern)
		return false;
	std::set<std::wstring> visiting;
	for (const Bunny::CommandListAction &action : config.actions) {
		if (CommandListActionHasTextureOverrideTrigger(
			    action, commandLists, triggerCache, &visiting))
			return true;
	}
	return CommandListLinksHaveTextureOverrideTrigger(
		config.commandLists.pre, commandLists, triggerCache) ||
		CommandListLinksHaveTextureOverrideTrigger(
			config.commandLists.main, commandLists, triggerCache) ||
		CommandListLinksHaveTextureOverrideTrigger(
			config.commandLists.post, commandLists, triggerCache);
}


bool ShaderRegexesHaveTextureOverrideTrigger(
	const Bunny::ShaderRegexMap &shaderRegexes,
	const Bunny::CommandListMap &commandLists)
{
	std::unordered_map<std::wstring, bool> triggerCache;
	for (const auto &item : shaderRegexes) {
		if (ShaderRegexHasTextureOverrideTrigger(
			    item.second, commandLists, &triggerCache))
			return true;
	}
	return false;
}


bool ShaderRegexesHaveExecutableNoPatternGroup(
	const Bunny::ShaderRegexMap &shaderRegexes)
{
	for (const auto &item : shaderRegexes) {
		if (!item.second.hasPattern)
			return true;
	}
	return false;
}


bool ShaderRegexHasDescriptorTextureOverrideTrigger(
	const Bunny::ShaderRegexConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	if (config.hasPattern)
		return false;
	std::set<std::wstring> visiting;
	for (const Bunny::CommandListAction &action : config.actions) {
		if (CommandListActionHasDescriptorTextureOverrideTrigger(
			    action, commandLists, triggerCache, &visiting))
			return true;
	}
	return CommandListLinksHaveDescriptorTextureOverrideTrigger(
		config.commandLists.pre, commandLists, triggerCache) ||
		CommandListLinksHaveDescriptorTextureOverrideTrigger(
			config.commandLists.main, commandLists, triggerCache) ||
		CommandListLinksHaveDescriptorTextureOverrideTrigger(
			config.commandLists.post, commandLists, triggerCache);
}


bool ShaderRegexesHaveDescriptorTextureOverrideTrigger(
	const Bunny::ShaderRegexMap &shaderRegexes,
	const Bunny::CommandListMap &commandLists)
{
	std::unordered_map<std::wstring, bool> triggerCache;
	for (const auto &item : shaderRegexes) {
		if (ShaderRegexHasDescriptorTextureOverrideTrigger(
			    item.second, commandLists, &triggerCache))
			return true;
	}
	return false;
}


bool ShaderRegexHasIaTextureOverrideTrigger(
	const Bunny::ShaderRegexConfig &config,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *triggerCache)
{
	if (config.hasPattern)
		return false;
	std::set<std::wstring> visiting;
	for (const Bunny::CommandListAction &action : config.actions) {
		if (CommandListActionHasIaTextureOverrideTrigger(
			    action, commandLists, triggerCache, &visiting))
			return true;
	}
	return CommandListLinksHaveIaTextureOverrideTrigger(
		config.commandLists.pre, commandLists, triggerCache) ||
		CommandListLinksHaveIaTextureOverrideTrigger(
			config.commandLists.main, commandLists, triggerCache) ||
		CommandListLinksHaveIaTextureOverrideTrigger(
			config.commandLists.post, commandLists, triggerCache);
}


bool ShaderRegexesHaveIaTextureOverrideTrigger(
	const Bunny::ShaderRegexMap &shaderRegexes,
	const Bunny::CommandListMap &commandLists)
{
	std::unordered_map<std::wstring, bool> triggerCache;
	for (const auto &item : shaderRegexes) {
		if (ShaderRegexHasIaTextureOverrideTrigger(
			    item.second, commandLists, &triggerCache))
			return true;
	}
	return false;
}


bool TextureOverridesHaveDrawSrvReplacement(
	const Bunny::TextureOverrideMap &textureOverrides)
{
	for (Bunny::TextureOverrideMap::const_iterator bucket = textureOverrides.begin();
	     bucket != textureOverrides.end(); ++bucket) {
		const Bunny::TextureOverrideList &configs = bucket->second;
		for (size_t index = 0; index < configs.size(); ++index) {
			const Bunny::TextureOverrideConfig &config = configs[index];
			if (!config.drawPsSrvResources.empty() ||
			    !config.drawVsSrvResources.empty() ||
			    !config.drawReplaceThisResource.empty())
				return true;
		}
	}
	return false;
}



bool CompiledShaderOverridePlansNeedResidentIaState(
	const std::unordered_map<std::wstring, DX12CompiledShaderOverridePlan> &plans)
{
	for (const auto &entry : plans) {
		if (entry.second.NeedsResidentIaState())
			return true;
	}
	return false;
}


bool CommandListActionHasRuntimeEffect(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting)
{
	switch (action.kind) {
	case Bunny::CommandListActionKind::Run:
		return CommandListHasRuntimeEffect(
			action.commandList, commandLists, effectCache, visiting);
	case Bunny::CommandListActionKind::CheckTextureOverride:
	case Bunny::CommandListActionKind::HandlingSkip:
	case Bunny::CommandListActionKind::SetIndexBuffer:
	case Bunny::CommandListActionKind::SetVertexBuffer:
	case Bunny::CommandListActionKind::SetShaderResource:
	case Bunny::CommandListActionKind::SetConstantBuffer:
	case Bunny::CommandListActionKind::SetUnorderedAccessView:
	case Bunny::CommandListActionKind::Draw:
	case Bunny::CommandListActionKind::DrawIndexed:
	case Bunny::CommandListActionKind::DrawIndexedAuto:
	case Bunny::CommandListActionKind::DrawFromCaller:
	case Bunny::CommandListActionKind::DrawIndexedFromCaller:
	case Bunny::CommandListActionKind::Dispatch:
		return true;
	}
	return true;
}


bool CommandListLinksHaveRuntimeEffect(
	const std::vector<std::wstring> &links,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache)
{
	std::set<std::wstring> visiting;
	for (const std::wstring &link : links) {
		if (CommandListHasRuntimeEffect(link, commandLists, effectCache, &visiting))
			return true;
	}
	return false;
}


bool TextureOverrideActionsHaveRuntimeEffect(
	const Bunny::TextureOverrideConfig &config,
	bool executeDrawActions,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache)
{
	if (config.actions.empty())
		return !config.indexBufferResource.empty() ||
			!config.vertexBufferResources.empty() ||
			(executeDrawActions && config.handlingSkip);

	std::set<std::wstring> visiting;
	for (const Bunny::CommandListAction &action : config.actions) {
		switch (action.kind) {
		case Bunny::CommandListActionKind::Run:
			if (CommandListHasRuntimeEffect(
				    action.commandList, commandLists, effectCache, &visiting))
				return true;
			break;
		case Bunny::CommandListActionKind::CheckTextureOverride:
		case Bunny::CommandListActionKind::SetIndexBuffer:
		case Bunny::CommandListActionKind::SetVertexBuffer:
		case Bunny::CommandListActionKind::SetShaderResource:
		case Bunny::CommandListActionKind::SetConstantBuffer:
		case Bunny::CommandListActionKind::SetUnorderedAccessView:
		case Bunny::CommandListActionKind::Dispatch:
			return true;
		case Bunny::CommandListActionKind::HandlingSkip:
		case Bunny::CommandListActionKind::Draw:
		case Bunny::CommandListActionKind::DrawIndexed:
		case Bunny::CommandListActionKind::DrawIndexedAuto:
		case Bunny::CommandListActionKind::DrawFromCaller:
		case Bunny::CommandListActionKind::DrawIndexedFromCaller:
			if (executeDrawActions)
				return true;
			break;
		}
	}
	return false;
}


void BuildDx12RuntimeEffectIndexes(
	const Bunny::TextureOverrideMap &textureOverrides,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *commandListEffect,
	std::unordered_map<std::wstring, bool> *textureOverridePreEffect,
	std::unordered_map<std::wstring, bool> *textureOverridePostEffect)
{
	if (!commandListEffect || !textureOverridePreEffect || !textureOverridePostEffect)
		return;

	commandListEffect->clear();
	textureOverridePreEffect->clear();
	textureOverridePostEffect->clear();

	for (const auto &item : commandLists) {
		std::set<std::wstring> visiting;
		CommandListHasRuntimeEffect(item.first, commandLists, commandListEffect, &visiting);
	}

	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			const bool preEffect =
				!config.drawPsSrvResources.empty() ||
				!config.drawVsSrvResources.empty() ||
				!config.drawReplaceThisResource.empty() ||
				CommandListLinksHaveRuntimeEffect(
					config.commandLists.pre, commandLists, commandListEffect) ||
				CommandListLinksHaveRuntimeEffect(
					config.commandLists.main, commandLists, commandListEffect) ||
				TextureOverrideActionsHaveRuntimeEffect(
					config, true, commandLists, commandListEffect);
			const bool postEffect =
				CommandListLinksHaveRuntimeEffect(
					config.commandLists.post, commandLists, commandListEffect);
			(*textureOverridePreEffect)[config.section] = preEffect;
			(*textureOverridePostEffect)[config.section] = postEffect;
		}
	}
}


bool AnyRuntimeEffectInMap(
	const std::unordered_map<std::wstring, bool> &effects)
{
	for (const auto &item : effects) {
		if (item.second)
			return true;
	}
	return false;
}


bool CommandListActionHasPhaseRuntimeEffect(
	const Bunny::CommandListAction &action,
	bool includePost,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting,
	bool anyTextureOverridePreEffect,
	bool anyTextureOverridePostEffect)
{
	switch (action.kind) {
	case Bunny::CommandListActionKind::Run:
		return CommandListHasPhaseRuntimeEffect(
			action.commandList, includePost, commandLists, effectCache, visiting,
			anyTextureOverridePreEffect, anyTextureOverridePostEffect);
	case Bunny::CommandListActionKind::CheckTextureOverride:
		return includePost ? anyTextureOverridePostEffect :
			anyTextureOverridePreEffect;
	case Bunny::CommandListActionKind::HandlingSkip:
	case Bunny::CommandListActionKind::SetIndexBuffer:
	case Bunny::CommandListActionKind::SetVertexBuffer:
	case Bunny::CommandListActionKind::SetShaderResource:
	case Bunny::CommandListActionKind::SetConstantBuffer:
	case Bunny::CommandListActionKind::SetUnorderedAccessView:
	case Bunny::CommandListActionKind::Draw:
	case Bunny::CommandListActionKind::DrawIndexed:
	case Bunny::CommandListActionKind::DrawIndexedAuto:
	case Bunny::CommandListActionKind::DrawFromCaller:
	case Bunny::CommandListActionKind::DrawIndexedFromCaller:
	case Bunny::CommandListActionKind::Dispatch:
		return true;
	default:
		return false;
	}
}


bool CommandListHasPhaseRuntimeEffect(
	const std::wstring &name,
	bool includePost,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting,
	bool anyTextureOverridePreEffect,
	bool anyTextureOverridePostEffect)
{
	if (!effectCache || !visiting)
		return true;

	auto cached = effectCache->find(name);
	if (cached != effectCache->end())
		return cached->second;
	if (!visiting->insert(name).second)
		return true;

	bool effect = false;
	auto it = commandLists.find(name);
	if (it != commandLists.end()) {
		for (const Bunny::CommandListAction &action : it->second.actions) {
			if (CommandListActionHasPhaseRuntimeEffect(
				    action, includePost, commandLists, effectCache, visiting,
				    anyTextureOverridePreEffect, anyTextureOverridePostEffect)) {
				effect = true;
				break;
			}
		}
	}
	visiting->erase(name);
	(*effectCache)[name] = effect;
	return effect;
}


void BuildDx12CommandListPhaseRuntimeEffectIndexes(
	const Bunny::CommandListMap &commandLists,
	const std::unordered_map<std::wstring, bool> &textureOverridePreEffect,
	const std::unordered_map<std::wstring, bool> &textureOverridePostEffect,
	std::unordered_map<std::wstring, bool> *commandListPreEffect,
	std::unordered_map<std::wstring, bool> *commandListPostEffect)
{
	if (!commandListPreEffect || !commandListPostEffect)
		return;

	commandListPreEffect->clear();
	commandListPostEffect->clear();
	const bool anyTextureOverridePreRuntimeEffect =
		AnyRuntimeEffectInMap(textureOverridePreEffect);
	const bool anyTextureOverridePostRuntimeEffect =
		AnyRuntimeEffectInMap(textureOverridePostEffect);

	for (const auto &item : commandLists) {
		std::set<std::wstring> visiting;
		CommandListHasPhaseRuntimeEffect(
			item.first, false, commandLists, commandListPreEffect, &visiting,
			anyTextureOverridePreRuntimeEffect,
			anyTextureOverridePostRuntimeEffect);
	}
	for (const auto &item : commandLists) {
		std::set<std::wstring> visiting;
		CommandListHasPhaseRuntimeEffect(
			item.first, true, commandLists, commandListPostEffect, &visiting,
			anyTextureOverridePreRuntimeEffect,
			anyTextureOverridePostRuntimeEffect);
	}
}


bool LookupRuntimeEffect(
	const std::unordered_map<std::wstring, bool> &effects,
	const std::wstring &section)
{
	auto effect = effects.find(section);
	return effect == effects.end() || effect->second;
}


void BuildDx12TextureOverrideSectionIds(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::unordered_map<std::wstring, uint32_t> *sectionIds)
{
	if (!sectionIds)
		return;

	sectionIds->clear();
	uint32_t nextId = 1;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (sectionIds->find(config.section) != sectionIds->end())
				continue;
			(*sectionIds)[config.section] = nextId++;
		}
	}
}


bool DX12ModRuntimeLoad(
	const wchar_t *configPath,
	std::wstring *statusMessage,
	bool *hasWarnings,
	bool *hasErrors)
{
	Bunny::MigotoIniLoadResult iniLoad;
	Bunny::ShaderOverrideMap shaderOverrides;
	Bunny::ShaderRegexMap shaderRegexes;
	Bunny::TextureOverrideMap textureOverrides;
	Bunny::ResourceMap resources;
	Bunny::CommandListMap commandLists;
	std::vector<Bunny::MigotoKeyBindingUtils::Binding> keyBindings;
	std::unordered_map<std::wstring, bool> commandListRuntimeEffect;
	std::unordered_map<std::wstring, bool> commandListPreRuntimeEffect;
	std::unordered_map<std::wstring, bool> commandListPostRuntimeEffect;
	std::unordered_map<std::wstring, bool> textureOverridePreRuntimeEffect;
	std::unordered_map<std::wstring, bool> textureOverridePostRuntimeEffect;
	std::unordered_map<std::wstring, uint32_t> textureOverrideSectionIds;
	std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> compiledTextureOverridePlans;
	std::unordered_map<std::wstring, DX12CompiledShaderOverridePlan> compiledShaderOverridePlans;
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> compiledCommandListPlans;
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> commandExecutionRuntimeSnapshot;
	bool presentRuntimeEffect = false;
	std::vector<DX12IaTextureOverrideCandidate> iaTextureOverrides;
	std::unordered_map<uint32_t, std::vector<size_t>> iaIndexTextureOverrideIndex;
	std::unordered_map<UINT64, std::vector<size_t>> iaVertexTextureOverrideIndex;
	std::unordered_map<uint32_t, std::vector<size_t>> iaAnyVertexTextureOverrideIndex;
	std::unordered_map<UINT64, std::vector<size_t>> iaMergedVertexTextureOverrideIndex;
	std::vector<size_t> iaIndexTextureOverridePrefilterIndex;
	std::unordered_map<uint32_t, std::vector<size_t>> iaIndexTextureOverridePrefilterByIndexCount;
	std::unordered_map<uint32_t, std::vector<size_t>> iaVertexTextureOverridePrefilterIndex;
	std::vector<size_t> iaAnyVertexTextureOverridePrefilterIndex;
	std::unordered_map<uint32_t, std::vector<size_t>> iaMergedVertexTextureOverridePrefilterIndex;
	std::unordered_map<uint32_t, std::vector<size_t>> iaAnyVertexTextureOverridePrefilterByVertexCount;
	std::unordered_map<UINT64, std::vector<size_t>> iaVertexTextureOverridePrefilterByVertexCount;
	std::unordered_map<UINT64, std::vector<size_t>> iaMergedVertexTextureOverridePrefilterByVertexCount;
	std::vector<DX12PreSkinTextureOverrideCandidate> preSkinTextureOverrides;
	std::unordered_map<uint32_t, std::vector<size_t>> preSkinUavHashTextureOverrideIndex;
	std::unordered_map<uint64_t, std::vector<size_t>> preSkinMatchCsTextureOverrideIndex;
	std::unordered_map<std::wstring, std::vector<std::wstring>> explicitMatchCsResourceSections;
	std::vector<std::wstring> vlrResourceCandidates;
	std::unordered_set<uint64_t> preSkinMatchCsHashes;
	bool hasPreSkinVlrWithoutMatchCs = false;
	size_t loadedShaderOverrideCount = 0;
	size_t loadedShaderRegexCount = 0;
	size_t loadedTextureOverrideCount = 0;
	size_t loadedResourceCount = 0;
	size_t loadedCommandListCount = 0;
	bool safeModeActive = false;
	bool activeShaderOverrides = false;
	bool activeTextureOverrides = false;
	bool preSkinProbeEnabled = false;
	bool loadedPreSkinTextureOverrideCandidates = false;
	bool loadedShaderTriggeredTextureOverrides = false;
	bool loadedPresentRuntimeEffect = false;
	std::wstring loadedShaderFixesDir;
	std::vector<DX12VertexLimitRaiseConfig> loadedVertexLimitRaiseConfigs;
	UINT64 loadedGeneration = 0;
	
	SetBasePaths(configPath);
	if (!Bunny::MigotoIniLoader::LoadWithIncludes(configPath, &iniLoad)) {
		DX12LogManager::Fields("DX12ModRuntime",
			"\"status\":\"config_load_failed\",\"path\":\"%S\",\"error\":\"%S\"",
			configPath ? configPath : L"", iniLoad.document.Error().c_str());
		if (statusMessage)
			*statusMessage = L"DX12 mod config load failed: " + iniLoad.document.Error();
		if (hasWarnings)
			*hasWarnings = true;
		if (hasErrors)
			*hasErrors = true;
		return false;
	}

	const bool hasIniErrors = !iniLoad.errors.empty();
	Bunny::MigotoShaderOverrideParser::ParseSections(iniLoad.document, &shaderOverrides);
	Bunny::MigotoShaderRegexParser::ParseSections(iniLoad.document, &shaderRegexes);
	Bunny::MigotoTextureOverrideParser::ParseSections(iniLoad.document, &textureOverrides);
	Bunny::MigotoResourceParser::ParseSections(iniLoad.document, &resources);
	Bunny::MigotoCommandListParser::ParseSections(iniLoad.document, &commandLists);
	Bunny::MigotoKeyBindingUtils::ParseSections(iniLoad.document, &keyBindings);
	DX12ConstantsVmInitializerUtils::Initialize(commandLists);


	std::vector<DX12VertexLimitRaiseConfig> vertexLimitRaiseConfigs =
		CollectVertexLimitRaiseConfigs(textureOverrides);
	bool hasPreSkinTextureOverrideCandidates =
		HasPreSkinTextureOverrideCandidates(
			textureOverrides, &preSkinMatchCsHashes,
			&hasPreSkinVlrWithoutMatchCs,
			nullptr);
	bool hasShaderTriggeredTextureOverrides =
		!textureOverrides.empty() &&
		(ShaderOverridesHaveTextureOverrideTrigger(shaderOverrides, commandLists) ||
		 ShaderRegexesHaveTextureOverrideTrigger(shaderRegexes, commandLists));
	bool hasDrawSrvTextureReplacementOverrides =
		TextureOverridesHaveDrawSrvReplacement(textureOverrides);
	bool hasExecutableShaderRegexes =
		ShaderRegexesHaveExecutableNoPatternGroup(shaderRegexes);
	bool hasShaderIaTextureOverrideTriggers =
		!textureOverrides.empty() &&
		(ShaderOverridesHaveIaTextureOverrideTrigger(shaderOverrides, commandLists) ||
		 ShaderRegexesHaveIaTextureOverrideTrigger(shaderRegexes, commandLists));


	bool hasShaderDescriptorTextureOverrideTriggers =
		hasShaderTriggeredTextureOverrides &&
		(ShaderOverridesHaveDescriptorTextureOverrideTrigger(shaderOverrides, commandLists) ||
		 ShaderRegexesHaveDescriptorTextureOverrideTrigger(shaderRegexes, commandLists));
	BuildDx12RuntimeEffectIndexes(
		textureOverrides, commandLists,
		&commandListRuntimeEffect,
		&textureOverridePreRuntimeEffect,
		&textureOverridePostRuntimeEffect);
	BuildDx12CommandListPhaseRuntimeEffectIndexes(
		commandLists,
		textureOverridePreRuntimeEffect,
		textureOverridePostRuntimeEffect,
		&commandListPreRuntimeEffect,
		&commandListPostRuntimeEffect);
	{
		std::set<std::wstring> visiting;
		presentRuntimeEffect =
			CommandListHasRuntimeEffect(
				L"Present", commandLists, &commandListRuntimeEffect, &visiting);
	}
	BuildDx12TextureOverrideSectionIds(
		textureOverrides, &textureOverrideSectionIds);
	BuildDx12IaTextureOverrideIndex(
		textureOverrides,
		textureOverrideSectionIds,
		textureOverridePreRuntimeEffect,
		textureOverridePostRuntimeEffect,
		&iaTextureOverrides,
		&iaIndexTextureOverrideIndex,
		&iaVertexTextureOverrideIndex,
		&iaAnyVertexTextureOverrideIndex,
		&iaMergedVertexTextureOverrideIndex,
		&iaIndexTextureOverridePrefilterIndex,
		&iaIndexTextureOverridePrefilterByIndexCount,
		&iaVertexTextureOverridePrefilterIndex,
		&iaAnyVertexTextureOverridePrefilterIndex,
		&iaAnyVertexTextureOverridePrefilterByVertexCount,
		&iaMergedVertexTextureOverridePrefilterIndex,
		&iaVertexTextureOverridePrefilterByVertexCount,
		&iaMergedVertexTextureOverridePrefilterByVertexCount);
	BuildDx12CompiledCommandListPlans(
		commandLists, &compiledCommandListPlans);
	BuildDx12CompiledTextureOverridePlans(
		textureOverrides, textureOverrideSectionIds, compiledCommandListPlans,
		&compiledTextureOverridePlans);
	BuildDx12CompiledShaderOverridePlans(
		shaderOverrides, shaderRegexes, compiledCommandListPlans,
		&compiledShaderOverridePlans);
	// Plan step 3.1: write-time static texture replacement table. Built from
	// the parsed configs before the swap-in below publishes them.
	std::shared_ptr<DX12StaticTextureReplacementTable> staticTextureReplacements =
		BuildDx12StaticTextureReplacementTable(
			textureOverrides, textureOverrideSectionIds);
	staticTextureReplacements->reloadGeneration = gReloadGeneration + 1;
	// Plan step 3.2: bind-time static IA replacement table. Disabled entirely
	// while any shader override reads IA checktextureoverride results.
	std::shared_ptr<DX12StaticIaReplacementTable> staticIaReplacements =
		BuildDx12StaticIaReplacementTable(
			textureOverrides, hasShaderIaTextureOverrideTriggers);
	staticIaReplacements->reloadGeneration = gReloadGeneration + 1;
	// Any draw-count-indexed prefilter table makes the "has candidate" answer
	// count-dependent; publish that before the tables are swapped in below so
	// the draw path can choose IA-state-level negative caching.
	const bool iaPrefilterCountSensitive =
		!iaIndexTextureOverridePrefilterByIndexCount.empty() ||
		!iaAnyVertexTextureOverridePrefilterByVertexCount.empty() ||
		!iaVertexTextureOverridePrefilterByVertexCount.empty() ||
		!iaMergedVertexTextureOverridePrefilterByVertexCount.empty();
	bool hasIaResidentStateRequirements =
		hasShaderIaTextureOverrideTriggers ||
		CompiledShaderOverridePlansNeedResidentIaState(compiledShaderOverridePlans);
	commandExecutionRuntimeSnapshot = BuildDx12CommandExecutionRuntimeSnapshot(
		commandLists,
		commandListRuntimeEffect,
		commandListPreRuntimeEffect,
		commandListPostRuntimeEffect,
		textureOverrides,
		textureOverridePreRuntimeEffect,
		textureOverridePostRuntimeEffect,
		textureOverrideSectionIds,
		iaTextureOverrides,
		iaIndexTextureOverrideIndex,
		iaMergedVertexTextureOverrideIndex,
		compiledCommandListPlans,
		compiledTextureOverridePlans,
		compiledShaderOverridePlans);
	BuildDx12PreSkinTextureOverrideIndex(
		textureOverrides,
		&preSkinTextureOverrides,
		&preSkinUavHashTextureOverrideIndex,
		&preSkinMatchCsTextureOverrideIndex);
	BuildDx12ExplicitMatchCsResourceIndex(
		textureOverrides, &explicitMatchCsResourceSections);
	BuildDx12VlrResourceCandidates(
		resources, explicitMatchCsResourceSections, &vlrResourceCandidates);

	DX12ModLockTelemetryUtils::AcquireModExclusive(&gModLock);
	gShaderOverrides.swap(shaderOverrides);
	gShaderRegexes.swap(shaderRegexes);
	gTextureOverrides.swap(textureOverrides);
	gModResources.swap(resources);
	gCommandLists.swap(commandLists);
	gKeyBindings.swap(keyBindings);
	gVlrResourceCandidates.swap(vlrResourceCandidates);
	gCommandListRuntimeEffect.swap(commandListRuntimeEffect);
	gCommandListPreRuntimeEffect.swap(commandListPreRuntimeEffect);
	gCommandListPostRuntimeEffect.swap(commandListPostRuntimeEffect);
	gTextureOverridePreRuntimeEffect.swap(textureOverridePreRuntimeEffect);
	gTextureOverridePostRuntimeEffect.swap(textureOverridePostRuntimeEffect);
	gTextureOverrideSectionIds.swap(textureOverrideSectionIds);
	gIaTextureOverrides.swap(iaTextureOverrides);
	gIaIndexTextureOverrideIndex.swap(iaIndexTextureOverrideIndex);
	gIaVertexTextureOverrideIndex.swap(iaVertexTextureOverrideIndex);
	gIaAnyVertexTextureOverrideIndex.swap(iaAnyVertexTextureOverrideIndex);
	gIaMergedVertexTextureOverrideIndex.swap(iaMergedVertexTextureOverrideIndex);
	gIaIndexTextureOverridePrefilterIndex.swap(iaIndexTextureOverridePrefilterIndex);
	gIaIndexTextureOverridePrefilterByIndexCount.swap(
		iaIndexTextureOverridePrefilterByIndexCount);
	gIaVertexTextureOverridePrefilterIndex.swap(iaVertexTextureOverridePrefilterIndex);
	gIaAnyVertexTextureOverridePrefilterIndex.swap(iaAnyVertexTextureOverridePrefilterIndex);
	gIaAnyVertexTextureOverridePrefilterByVertexCount.swap(
		iaAnyVertexTextureOverridePrefilterByVertexCount);
	gIaMergedVertexTextureOverridePrefilterIndex.swap(iaMergedVertexTextureOverridePrefilterIndex);
	gIaVertexTextureOverridePrefilterByVertexCount.swap(
		iaVertexTextureOverridePrefilterByVertexCount);
	gIaMergedVertexTextureOverridePrefilterByVertexCount.swap(
		iaMergedVertexTextureOverridePrefilterByVertexCount);
	gTextureOverrideLookupCache.clear();
	gIaTextureOverrideHashNoCandidates.clear();
	InterlockedExchange(&gIaPrefilterCountSensitive,
		iaPrefilterCountSensitive ? 1 : 0);
	DX12ModClearIaReplacementRuntimeCaches();
	gCompiledCommandListPlans.swap(compiledCommandListPlans);
	gCompiledTextureOverridePlans.swap(compiledTextureOverridePlans);
	gCommandExecutionRuntimeSnapshot.store(
		commandExecutionRuntimeSnapshot,
		std::memory_order_release);
	// Publish the write-time static texture replacement table (plan step 3.1)
	// and drop resolved descriptor handles cached against the old table.
	gStaticTextureReplacementSnapshot.store(
		std::shared_ptr<const DX12StaticTextureReplacementTable>(
			staticTextureReplacements),
		std::memory_order_release);
	InterlockedExchange(&gHasStaticTextureReplacements,
		staticTextureReplacements->enabled ? 1 : 0);
	DX12NoteStaticTextureReplacementTablePublished();
	gStaticIaReplacementSnapshot.store(
		std::shared_ptr<const DX12StaticIaReplacementTable>(
			staticIaReplacements),
		std::memory_order_release);
	InterlockedExchange(&gHasStaticIaReplacements,
		staticIaReplacements->enabled ? 1 : 0);
	gPreSkinTextureOverrides.swap(preSkinTextureOverrides);
	gPreSkinUavHashTextureOverrideIndex.swap(preSkinUavHashTextureOverrideIndex);
	gPreSkinMatchCsTextureOverrideIndex.swap(preSkinMatchCsTextureOverrideIndex);
	gExplicitMatchCsResourceSections.swap(explicitMatchCsResourceSections);
	gVertexLimitRaiseConfigs.swap(vertexLimitRaiseConfigs);
	InterlockedExchange(
		&gHasVertexLimitRaiseConfigs,
		gVertexLimitRaiseConfigs.empty() ? 0 : 1);
	gPreSkinMatchCsHashes.swap(preSkinMatchCsHashes);
	gHasPreSkinVlrWithoutMatchCs = hasPreSkinVlrWithoutMatchCs;
	ReleaseLoadedResourcesLocked();
	gShaderOverridePsoMatchCache.clear();
	gPresentCommandListExecutedPresent = -1;
	AcquireSRWLockExclusive(&gPreSkinSrvProbeCacheLock);
	gPreSkinSrvNegativeCache.clear();
	gPreSkinSrvStableNegativeCache.clear();
	gPreSkinSrvPositiveCache.clear();
	gPreSkinSrvStablePositiveCache.clear();
	ReleaseSRWLockExclusive(&gPreSkinSrvProbeCacheLock);
	AcquireSRWLockExclusive(&gPreSkinDescriptorPatchAbortLock);
	gPreSkinDescriptorPatchAbortCache.clear();
	ReleaseSRWLockExclusive(&gPreSkinDescriptorPatchAbortLock);
	InterlockedExchange(&gPreSkinDescriptorAbortCacheHitLogCount, 0);
	AcquireSRWLockExclusive(&gPreSkinUavMatchCacheLock);
	gPreSkinUavMatchCache.clear();
	ReleaseSRWLockExclusive(&gPreSkinUavMatchCacheLock);
	AcquireSRWLockExclusive(&gPreSkinMatchCsProbeLogLock);
	gPreSkinMatchCsProbeKeys.clear();
	InterlockedExchange(&gPreSkinMatchCsProbeLogCount, 0);
	ReleaseSRWLockExclusive(&gPreSkinMatchCsProbeLogLock);
	ClearPreSkinStickyMatchCache();
	BumpPreSkinActiveGeneration();
	gPreSkinSrvCacheGeneration.fetch_add(1, std::memory_order_relaxed);
	InterlockedExchange(&gHasShaderOverrides, gShaderOverrides.empty() ? 0 : 1);
	InterlockedExchange(&gHasShaderRegexes, hasExecutableShaderRegexes ? 1 : 0);
	InterlockedExchange(&gHasTextureOverrides, gTextureOverrides.empty() ? 0 : 1);
	InterlockedExchange(
		&gHasPreSkinTextureOverrideCandidates,
		hasPreSkinTextureOverrideCandidates ? 1 : 0);
	InterlockedExchange(
		&gHasShaderTriggeredTextureOverrides,
		hasShaderTriggeredTextureOverrides ? 1 : 0);
	InterlockedExchange(
		&gHasShaderDescriptorTextureOverrideTriggers,
		hasShaderDescriptorTextureOverrideTriggers ? 1 : 0);
	InterlockedExchange(
		&gHasDrawSrvTextureReplacementOverrides,
		hasDrawSrvTextureReplacementOverrides ? 1 : 0);
	InterlockedExchange(
		&gHasIaResidentStateRequirements,
		hasIaResidentStateRequirements ? 1 : 0);
	InterlockedExchange(&gHasPresentRuntimeEffect, presentRuntimeEffect ? 1 : 0);
	gLoaded = true;
	++gReloadGeneration;
	InterlockedExchange64(
		reinterpret_cast<volatile LONG64*>(&gShaderOverrideNegativeCacheGeneration),
		static_cast<LONG64>(gReloadGeneration));
	loadedShaderOverrideCount = gShaderOverrides.size();
	loadedShaderRegexCount = gShaderRegexes.size();
	loadedTextureOverrideCount = gTextureOverrides.size();
	loadedResourceCount = gModResources.size();
	loadedCommandListCount = gCommandLists.size();
	loadedPreSkinTextureOverrideCandidates = hasPreSkinTextureOverrideCandidates;
	loadedShaderTriggeredTextureOverrides = hasShaderTriggeredTextureOverrides;
	loadedPresentRuntimeEffect = presentRuntimeEffect;
	safeModeActive = InterlockedCompareExchange(&gDx12SafeMode, 0, 0) != 0;
	activeShaderOverrides = !safeModeActive &&
		(!gShaderOverrides.empty() || hasExecutableShaderRegexes);
	activeTextureOverrides =
		!safeModeActive && !gTextureOverrides.empty() &&
		hasShaderTriggeredTextureOverrides;
	preSkinProbeEnabled =
		!safeModeActive && hasShaderTriggeredTextureOverrides &&
		hasPreSkinTextureOverrideCandidates;
	loadedShaderFixesDir = gShaderFixesDir;
	loadedVertexLimitRaiseConfigs = gVertexLimitRaiseConfigs;
	loadedGeneration = gReloadGeneration;
	for (auto &item : gPsoRecords) {
		if (item.second.replacement) {
			item.second.replacement->Release();
			item.second.replacement = nullptr;
		}
		item.second.replacementGeneration = 0;
		item.second.skipGeneration = 0;
	}
	ReleaseSRWLockExclusive(&gModLock);
	ClearPreSkinRuntimeState();

	DX12LogManager::Fields("DX12ModRuntime",
		"\"status\":\"loaded\",\"path\":\"%S\",\"iniFiles\":%zu,\"errors\":%zu,\"warnings\":%zu,\"shaderOverrides\":%zu,\"shaderRegexes\":%zu,\"textureOverrides\":%zu,\"resources\":%zu,\"commandLists\":%zu,\"safeMode\":%s,\"activeShaderOverrides\":%s,\"activeTextureOverrides\":%s,\"shaderTriggeredTextureOverrides\":%s,\"descriptorTextureOverrideTriggers\":%s,\"preSkinCandidates\":%s,\"preSkinProbeEnabled\":%s,\"presentRuntimeEffect\":%s,\"shaderFixes\":\"%S\",\"generation\":%llu",
		configPath ? configPath : L"", iniLoad.loadedFiles.size(), iniLoad.errors.size(), iniLoad.warnings.size(),
		loadedShaderOverrideCount, loadedShaderRegexCount, loadedTextureOverrideCount, loadedResourceCount, loadedCommandListCount,
		safeModeActive ? "true" : "false",
		activeShaderOverrides ? "true" : "false",
		activeTextureOverrides ? "true" : "false",
		loadedShaderTriggeredTextureOverrides ? "true" : "false",
		hasShaderDescriptorTextureOverrideTriggers ? "true" : "false",
		loadedPreSkinTextureOverrideCandidates ? "true" : "false",
		preSkinProbeEnabled ? "true" : "false",
		loadedPresentRuntimeEffect ? "true" : "false",
		loadedShaderFixesDir.c_str(),
		static_cast<unsigned long long>(loadedGeneration));
	if (safeModeActive &&
	    (loadedShaderOverrideCount || loadedShaderRegexCount ||
	     loadedTextureOverrideCount || loadedPresentRuntimeEffect)) {
		DX12LogManager::Fields("DX12ModRuntimeDisabled",
			"\"reason\":\"safe_mode\",\"shaderOverrides\":%zu,\"shaderRegexes\":%zu,\"textureOverrides\":%zu,\"preSkinCandidates\":%s,\"presentRuntimeEffect\":%s",
			loadedShaderOverrideCount, loadedShaderRegexCount, loadedTextureOverrideCount,
			loadedPreSkinTextureOverrideCandidates ? "true" : "false",
			loadedPresentRuntimeEffect ? "true" : "false");
	}
	for (const std::wstring &loadedFile : iniLoad.loadedFiles) {
		DX12LogManager::Fields("DX12ModRuntimeIni",
			"\"status\":\"loaded\",\"path\":\"%S\"", loadedFile.c_str());
	}
	for (const std::wstring &error : iniLoad.errors) {
		DX12LogManager::Fields("DX12ModRuntimeIni",
			"\"status\":\"error\",\"message\":\"%S\"", error.c_str());
	}
	for (const std::wstring &warning : iniLoad.warnings) {
		DX12LogManager::Fields("DX12ModRuntimeIni",
			"\"status\":\"warning\",\"message\":\"%S\"", warning.c_str());
	}
	for (const DX12VertexLimitRaiseConfig &vlr : loadedVertexLimitRaiseConfigs) {
		DX12LogManager::Fields("DX12VertexLimitRaiseConfig",
			"\"section\":\"%S\",\"overrideByteStride\":%u,\"overrideVertexCount\":%u,\"overrideByteWidth\":%llu,\"uavByteStride\":%u,\"overrideNumElements\":%llu",
			vlr.section.c_str(), vlr.overrideByteStride, vlr.overrideVertexCount,
			static_cast<unsigned long long>(vlr.overrideByteWidth),
			vlr.uavByteStride, static_cast<unsigned long long>(vlr.overrideNumElements));
	}
	if (statusMessage) {
		wchar_t text[256];
		if (hasIniErrors) {
			swprintf_s(text, L"F10 DX12 mod config reloaded with %zu error(s)",
				iniLoad.errors.size());
		} else if (iniLoad.warnings.empty()) {
			swprintf_s(text, L"F10 DX12 mod config reloaded: %zu ini, %zu TextureOverride, %zu Resource",
				iniLoad.loadedFiles.size(), loadedTextureOverrideCount, loadedResourceCount);
		} else {
			swprintf_s(text, L"F10 DX12 mod config reloaded with %zu warning(s)",
				iniLoad.warnings.size());
		}
		*statusMessage = text;
	}
	if (hasWarnings)
		*hasWarnings = hasIniErrors || !iniLoad.warnings.empty();
	if (hasErrors)
		*hasErrors = hasIniErrors;
	return true;
}


bool DX12ModRuntimeReload()
{
	std::wstring configPath;
	DX12ModLockTelemetryUtils::AcquireModShared(&gModLock);
	configPath = gConfigPath;
	ReleaseSRWLockShared(&gModLock);
	if (configPath.empty()) {
		DX12LogManager::Fields("DX12ModRuntimeReload", "\"status\":\"skipped\",\"reason\":\"empty_config_path\"");
		return false;
	}
	DX12LogManager::Fields("DX12ModRuntimeReload", "\"status\":\"begin\",\"path\":\"%S\"", configPath.c_str());
	std::wstring status;
	bool hasWarnings = false;
	bool hasErrors = false;
	if (!DX12ModRuntimeLoad(configPath.c_str(), &status, &hasWarnings, &hasErrors)) {
		DX12OverlayStatusState::SetError(status.c_str());
		return false;
	}
	if (hasErrors)
		DX12OverlayStatusState::SetError(status.c_str());
	else if (hasWarnings)
		DX12OverlayStatusState::SetWarning(status.c_str());
	else
		DX12OverlayStatusState::SetStatus(status.c_str());
	return true;
}



std::wstring ResolveResourcePath(
	const Bunny::ResourceConfig &config, const std::wstring &baseDir,
	bool rootFallback)
{
	if (config.filename.empty())
		return L"";

	wchar_t path[MAX_PATH];
	wcsncpy_s(path, config.filename.c_str(), _TRUNCATE);
	if (PathIsRelativeW(path)) {
		wchar_t combined[MAX_PATH];
		const std::wstring &base = rootFallback || config.sourceDir.empty() ? baseDir : config.sourceDir;
		wcsncpy_s(combined, base.c_str(), _TRUNCATE);
		PathAppendW(combined, config.filename.c_str());
		return combined;
	}
	return path;
}


std::wstring ResolveResourcePathLocked(
	const Bunny::ResourceConfig &config, bool rootFallback)
{
	return ResolveResourcePath(config, gBaseDir, rootFallback);
}


bool ResourceConfigLooksLikeBuffer(const Bunny::ResourceConfig &config)
{
	std::wstring type = Bunny::IniDocumentText::ToLower(Bunny::IniDocumentText::Trim(config.type));
	if (type.empty())
		return true;
	return type == L"buffer" ||
		type == L"structuredbuffer" ||
		type == L"appendstructuredbuffer" ||
		type == L"consumestructuredbuffer" ||
		type == L"byteaddressbuffer" ||
		type == L"rwbuffer" ||
		type == L"rwstructuredbuffer" ||
		type == L"rwbyteaddressbuffer";
}


bool LoadResourceBytes(
	const Bunny::ResourceConfig &config, const std::wstring &baseDir,
	std::vector<unsigned char> *bytes, std::wstring *path)
{
	if (!bytes)
		return false;
	bytes->clear();

	std::wstring resolvedPath = ResolveResourcePath(config, baseDir, false);
	if (!resolvedPath.empty()) {
		if (path)
			*path = resolvedPath;
		if (ReadFileBytes(resolvedPath.c_str(), bytes))
			return true;

		if (!config.sourceDir.empty()) {
			std::wstring fallbackPath = ResolveResourcePath(config, baseDir, true);
			if (fallbackPath != resolvedPath) {
				if (path)
					*path = fallbackPath;
				const bool fallbackOk = ReadFileBytes(fallbackPath.c_str(), bytes);
				DX12LogManager::Fields("DX12FallbackPath",
					"\"kind\":\"resource_path_root\",\"api\":\"DX12ModRuntime::LoadResourceBytes\","
					"\"section\":\"%S\",\"primary\":\"%S\",\"fallback\":\"%S\",\"success\":%s",
					config.section.c_str(), resolvedPath.c_str(), fallbackPath.c_str(),
					fallbackOk ? "true" : "false");
				return fallbackOk;
			}
		}
		return false;
	}

	if (config.data.empty())
		return false;

	std::wstring data = Bunny::IniDocumentText::Trim(config.data);
	if (data.rfind(L"0x", 0) == 0 || data.rfind(L"0X", 0) == 0)
		data = data.substr(2);
	data.erase(std::remove_if(data.begin(), data.end(), iswspace), data.end());
	if (data.empty() || (data.size() % 2) != 0)
		return false;

	for (size_t i = 0; i < data.size(); i += 2) {
		wchar_t text[3] = {data[i], data[i + 1], 0};
		wchar_t *end = nullptr;
		unsigned long parsed = wcstoul(text, &end, 16);
		if (!end || *end)
			return false;
		bytes->push_back(static_cast<unsigned char>(parsed));
	}
	return !bytes->empty();
}


bool AppendActionToCompiledPlan(
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
	case Bunny::CommandListActionKind::DrawIndexedAuto:
	case Bunny::CommandListActionKind::DrawFromCaller:
	case Bunny::CommandListActionKind::DrawIndexedFromCaller:
	{
		DX12ModIaReplacement::DrawCall draw;
		draw.indexed = (action.kind == Bunny::CommandListActionKind::DrawIndexed ||
						action.kind == Bunny::CommandListActionKind::DrawIndexedAuto ||
						action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller);
		draw.fromCaller = (action.kind == Bunny::CommandListActionKind::DrawFromCaller ||
						   action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller);
		draw.autoCount = (action.kind == Bunny::CommandListActionKind::DrawIndexedAuto);
		if (!draw.fromCaller && !draw.autoCount)
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


void AppendCompiledCommandListPlan(
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


bool AppendCompiledCommandListLinksPlan(
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


void AppendLegacyTextureOverrideStateToPlan(
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



void BuildCompiledShaderOverridePlanFromParts(
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


bool CompileCommandListPlanRecursive(
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


void BuildDx12CompiledCommandListPlans(
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


void BuildDx12CompiledTextureOverridePlans(
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
			plan.hash = config.hash;
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


void BuildDx12CompiledShaderOverridePlans(
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
