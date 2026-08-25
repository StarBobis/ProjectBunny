#include "DX12RuntimeState.h"
#include "DX12LogManager.h"

// Runtime mode transitions are logged through the async log manager, which
// cannot be included from DX12RuntimeState.h (the log manager itself depends
// on the runtime state), so this one definition lives out of line.

void DX12RuntimeState::SetRuntimeMode(DX12RuntimeMode mode)
{
	const LONG value = mode == DX12RuntimeMode::Hunting ?
		static_cast<LONG>(DX12RuntimeMode::Hunting) :
		static_cast<LONG>(DX12RuntimeMode::Mod);
	const LONG old = runtimeMode.exchange(value);
	if (old != value) {
		DX12LogManager::Fields("DX12RuntimeMode",
			"\"mode\":\"%s\"",
			mode == DX12RuntimeMode::Hunting ? "Hunting" : "Mod");
	}
}
