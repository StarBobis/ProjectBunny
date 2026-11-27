#include "DX12RuntimeState.h"
#include "Diag/DX12Diag.h"
#include "ExpressionFunctions.h"

LONG DX12RuntimeState::IncrementPresentCount(uint64_t ticks)
{
	const auto frame = static_cast<uint32_t>(presentCount.fetch_add(1, std::memory_order_relaxed)) + 1u;
	SeedExpressionRandom(frame, static_cast<uint32_t>(ticks));
	return static_cast<LONG>(frame);
}

// Runtime mode transitions are logged through the unified diagnostics facade,
// which cannot be included from DX12RuntimeState.h (the diagnostics header
// itself depends on the runtime state), so this one definition lives out of
// line. In non-TIMING builds the log call compiles to nothing.

void DX12RuntimeState::SetRuntimeMode(DX12RuntimeMode mode)
{
	const LONG value = mode == DX12RuntimeMode::Hunting ?
		static_cast<LONG>(DX12RuntimeMode::Hunting) :
		static_cast<LONG>(DX12RuntimeMode::Mod);
	const LONG old = runtimeMode.exchange(value);
	if (old != value) {
		DX12_LOG("DX12RuntimeMode",
			"\"mode\":\"%s\"",
			mode == DX12RuntimeMode::Hunting ? "Hunting" : "Mod");
	}
}
