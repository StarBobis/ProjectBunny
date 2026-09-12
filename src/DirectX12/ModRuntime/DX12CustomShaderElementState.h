#pragma once

// Custom-shader element registry state.
//
// [CustomShaderX] sections with stage files / render state are rebuilt on every
// config load into a mutable map, and published once per generation as an
// immutable snapshot that recording threads read instead. The command-list
// executor builds custom draws from the snapshot, so these are declared here
// rather than in ModRuntime/DX12ModRuntimeInternal.h - the faux translation
// unit that splices fragments together at fixed lines, which left the executor
// unable to name them.
#include "../Config/DX12Config.h"
#include "DX12CustomShaderElement.h"

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>

// Mutable registry, rebuilt on every load. Only the load path writes it; the
// executor reads the published snapshot below.
inline std::unordered_map<std::wstring, Bunny::DX12CustomShaderElementSpec>
	gCustomShaderElements;

using DX12CustomShaderElementMap =
	std::unordered_map<std::wstring, Bunny::DX12CustomShaderElementSpec>;

// Immutable per-generation snapshot published with the other runtime
// snapshots; recording threads read this instead of the mutable registry.
inline std::atomic<std::shared_ptr<const DX12CustomShaderElementMap>>
	gCustomShaderElementSnapshot;
