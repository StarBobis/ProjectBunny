#pragma once

// Texture-override matching: resolve which override applies to a draw's target
// and build the SRV bindings a replacement needs.
//
// This is the interface of ModRuntime/DX12TextureOverrideMatching.cpp. Those
// declarations used to sit in the middle of ModRuntime/DX12ModRuntimeInternal.h
// - the faux translation unit that splices fragments together at fixed lines -
// so a caller could only use them if it happened to be spliced in after that
// point.
//
// Include Core/DX12ModState.h (or its umbrella Core/DX12Core.h) for the state
// types; this header only owns the matching module's entry points.
#include "../Config/DX12Config.h"
#include "../DX12Core.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Ownership-taking variant of the published execution snapshot, for callers
// that must hold it beyond the current query (e.g. executor member caches). One
// refcount copy from the TLS cache; NOT for per-draw hot paths.
inline std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
SnapshotCommandExecutionRuntimeShared()
{
	return CommandExecutionRuntimeSnapshotTlsRef();
}

// Owning copy of one command list's config from the published snapshot.
inline std::shared_ptr<const Bunny::CommandListConfig>
SnapshotCommandListConfig(const std::wstring &name)
{
	const DX12CommandExecutionRuntimeSnapshot *snapshot =
		LoadCommandExecutionRuntimeSnapshot();
	if (!snapshot)
		return nullptr;
	auto it = snapshot->commandLists.find(name);
	return it == snapshot->commandLists.end() ? nullptr : it->second;
}

// Does any command list in the snapshot have a pre/post runtime effect? Gates
// whether a named command list needs to be walked at all.
bool CommandListRuntimeEffectLocked(
	const DX12CommandExecutionRuntimeSnapshot *snapshot,
	const std::wstring &name);
bool CommandListRuntimeEffectLocked(
	const DX12CommandExecutionRuntimeSnapshot *snapshot,
	const std::wstring &name,
	bool includePost);

bool CommandListRuntimeEffect(
	const std::wstring &name, bool includePost);

void AppendResolvedDescriptorPrefilterSectionIds(
	const std::vector<DX12TriggeredTextureOverride> &source,
	std::vector<uint32_t> *sectionIds);

// Cheap pre-test: are there any resolved descriptor-prefilter candidates for
// this draw shape at all? False lets the caller skip collecting section ids.
bool HasPotentialResolvedDescriptorPrefilterCandidates(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount);

void CollectResolvedDescriptorPrefilterSectionIds(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount,
	std::vector<uint32_t> *sectionIds);

// Which descriptor range type a command-list target kind maps to, and whether a
// binding matches that target at all.
D3D12_DESCRIPTOR_RANGE_TYPE DescriptorRangeTypeForTarget(
	Bunny::CommandListTargetKind kind);

bool DescriptorBindingMatchesTarget(
	const DX12CurrentShaderResourceBinding &binding,
	const Bunny::CommandListTarget &target);

// Find the texture overrides that match a command-list target for this draw.
bool FindTargetTextureOverride(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState, const Bunny::CommandListTarget &target,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	std::vector<DX12TextureOverrideMatch> *matches,
	const std::vector<DX12CurrentShaderResourceBinding> *preResolvedGraphicsSrvs = nullptr);

// Build the fully resolved graphics SRV bindings for one descriptor-target
// resource override, layering the replacement descriptor over baseBindings (or
// over the command list's current bindings when baseBindings is null).
bool BuildResolvedDescriptorBindingsForResourceOverride(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	const Bunny::CommandListTarget &target,
	const std::wstring &resourceName,
	const std::vector<DX12ResolvedGraphicsSrvBinding> *baseBindings,
	std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings);

// Resolve the command list's current graphics SRV bindings into replacement
// bindings, and cache the result on the runtime state for the descriptor query
// serial so repeated draws on the same bindings skip the work.
void ResolveGraphicsSrvBindings(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Device *cachedDevice,
	const std::vector<DX12CurrentShaderResourceBinding> &bindings,
	std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings);

bool TryLoadResolvedGraphicsSrvBindings(
	DX12SimulateContext *runtimeState,
	UINT64 descriptorQuerySerial,
	std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings);

void StoreResolvedGraphicsSrvBindings(
	DX12SimulateContext *runtimeState,
	UINT64 descriptorQuerySerial,
	const std::vector<DX12ResolvedGraphicsSrvBinding> &resolvedBindings);

// Look up the texture overrides that match a resolved binding set.
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
	std::vector<DX12TextureOverrideMatch> *matches);

void AppendResolvedBindingsToDescriptorTextureOverrideCache(
	const DX12CommandExecutionRuntimeSnapshot *snapshot,
	const std::vector<DX12ResolvedGraphicsSrvBinding> &resolvedBindings,
	Bunny::CommandListTargetKind kind,
	bool compute,
	const std::vector<uint32_t> &allowedSectionIds,
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> *matchesByTarget);
