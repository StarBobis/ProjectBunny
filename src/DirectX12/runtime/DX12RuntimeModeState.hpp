#pragma once

#include "DX12LogManager.hpp"
#include "DX12RuntimeState.hpp"

inline void DX12RuntimeState::SetRuntimeMode(DX12RuntimeMode mode)
{
	const LONG value = mode == DX12RuntimeMode::Hunting ?
		static_cast<LONG>(DX12RuntimeMode::Hunting) :
		static_cast<LONG>(DX12RuntimeMode::Mod);
	const LONG old = InterlockedExchange(&runtimeMode, value);
	if (old != value) {
		DX12LogManager::JsonFunc("DX12RuntimeMode",
			"\"mode\":\"%s\"",
			mode == DX12RuntimeMode::Hunting ? "Hunting" : "Mod");
	}
}
