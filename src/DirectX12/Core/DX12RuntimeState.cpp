#include "DX12RuntimeState.h"
#include "Diag/DX12Diag.h"

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
