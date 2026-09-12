#pragma once

// Module-level configuration and resource-map state, plus the file-read helper
// the DDS loader needs.
//
// These used to be defined in ModRuntime/DX12ModRuntimeInternal.h, and because
// that file is also the faux translation unit that splices several code
// fragments together, anything it defined was only visible to code placed after
// that definition. That is what forced those fragments to be spliced at a fixed
// line instead of carrying their own includes: they read gModLock, gBaseDir,
// gModResources and gTextureOverrides, which live here.
//
// Declaring them in a header included up front removes that ordering
// dependency, so each fragment can be read and compiled on its own.
//
// Include Core/DX12ModState.h (or its umbrella Core/DX12Core.h).
#include "../Config/DX12Config.h"
#include "../../ModRuntime/DX12ResourceAliasStore.h"

#include <Windows.h>
#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

// The one lock that guards the published configuration and the resource map.
inline SRWLOCK gModLock = SRWLOCK_INIT;

// Directory of the loaded config; the root every relative resource path is
// resolved against. Written by SetBasePaths under gModLock.
inline std::wstring gBaseDir;

inline std::wstring gShaderFixesDir;

inline Bunny::TextureOverrideMap gTextureOverrides;

inline Bunny::ResourceMap gModResources;

// Runtime resource alias table for "ResourceX = ref ResourceY" directives
// (phase 3); cleared on reload with the other config globals.
inline Bunny::DX12ResourceAliasStore gResourceAliases;

inline std::vector<std::wstring> gVlrResourceCandidates;

inline std::unordered_map<std::wstring, bool> gCommandListPreRuntimeEffect;

inline std::unordered_map<std::wstring, bool> gTextureOverridePreRuntimeEffect;

inline std::unordered_map<std::wstring, bool> gTextureOverridePostRuntimeEffect;

// Per-PSO shader-override match result, rebuilt on a PSO change or reload.
// Declared here rather than in the mod-runtime internal header because
// DX12ShaderOverrideIaTargetPlanUtils.h walks drawConfigs by value, so it needs
// the complete type rather than a forward declaration, and a header can only
// use what it can include.
class DX12ShaderOverridePsoMatchCache
{
public:
	UINT64 generation = 0;
	std::vector<const Bunny::ShaderOverrideConfig*> drawConfigs;
	std::vector<const Bunny::ShaderOverrideConfig*> dispatchConfigs;
	std::vector<Bunny::ShaderOverrideConfig> regexDrawConfigs;
	std::vector<Bunny::ShaderOverrideConfig> regexDispatchConfigs;
	DX12IaHashTargetPlan drawTargetPlan;
	DX12IaHashTargetPlan drawDirectTargetPlan;
	DX12DrawMatchUsage drawMatchUsage;
	bool drawDirectTextureChecksOnly = false;
};

inline std::unordered_map<ID3D12PipelineState*, DX12ShaderOverridePsoMatchCache> gShaderOverridePsoMatchCache;

// Read a whole file into memory. Defined in ModRuntime/DX12ModRuntimeLoad.cpp;
// declared here because the DDS and WIC loaders need it without depending on
// where it happens to sit in the mod-runtime internal header.
bool ReadFileBytes(const wchar_t *path, std::vector<unsigned char> *data);

// Resolve a [ResourceX] section's filename against the config directory (or the
// section's own sourceDir) and read it. Defined in
// ModRuntime/DX12ModRuntimeLoad.cpp; declared here for the same reason as
// ReadFileBytes - the DDS loader calls it on its fallback path.
bool LoadResourceBytes(
	const Bunny::ResourceConfig &config, const std::wstring &baseDir,
	std::vector<unsigned char> *bytes, std::wstring *path);

// Same resolution against the published gBaseDir, for callers that already hold
// gModLock and must not re-read a base directory that a reload could swap.
// Defined in ModRuntime/DX12ModRuntimeLoad.cpp.
std::wstring ResolveResourcePath(
	const Bunny::ResourceConfig &config, const std::wstring &baseDir,
	bool rootFallback);
