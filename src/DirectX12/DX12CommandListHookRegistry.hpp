#pragma once

#include <Windows.h>
#include <d3d12.h>

#include "DX12CommandListHookState.hpp"
#include "DX12OriginalFunctionRegistry.hpp"

class DX12CommandListHookRegistry
{
public:
	static bool ShouldHookCommandListVTable(void *vtable)
	{
		if (!vtable)
			return false;

		AcquireSRWLockShared(&DX12CommandListHookState::HookedVTableLock);
		const bool alreadyHooked =
			DX12CommandListHookState::HookedCommandListVTables.find(vtable) !=
			DX12CommandListHookState::HookedCommandListVTables.end();
		ReleaseSRWLockShared(&DX12CommandListHookState::HookedVTableLock);
		if (alreadyHooked)
			return false;

		bool shouldHook = false;
		AcquireSRWLockExclusive(&DX12CommandListHookState::HookedVTableLock);
		shouldHook = DX12CommandListHookState::HookedCommandListVTables.insert(vtable).second;
		ReleaseSRWLockExclusive(&DX12CommandListHookState::HookedVTableLock);
		return shouldHook;
	}

	static bool ShouldRegisterCommandListPointer(void *commandList)
	{
		if (!commandList)
			return false;

		AcquireSRWLockShared(&DX12CommandListHookState::RegisteredListLock);
		const bool alreadyRegistered =
			DX12CommandListHookState::RegisteredCommandLists.find(commandList) !=
			DX12CommandListHookState::RegisteredCommandLists.end();
		ReleaseSRWLockShared(&DX12CommandListHookState::RegisteredListLock);
		if (alreadyRegistered)
			return false;

		bool shouldRegister = false;
		AcquireSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
		shouldRegister = DX12CommandListHookState::RegisteredCommandLists.insert(commandList).second;
		ReleaseSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
		return shouldRegister;
	}

	static void ForgetRegisteredCommandListPointer(void *commandList)
	{
		if (!commandList)
			return;

		AcquireSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
		DX12CommandListHookState::RegisteredCommandLists.erase(commandList);
		ReleaseSRWLockExclusive(&DX12CommandListHookState::RegisteredListLock);
	}

	template <typename T>
	static T GetCommandQueueOriginal(ID3D12CommandQueue *queue, UINT slot, T fallback, const char *name)
	{
		return DX12OriginalFunctionRegistry::ResolveCached(
			queue, slot, fallback, name,
			DX12CommandListHookState::CommandQueueOriginals,
			ARRAYSIZE(DX12CommandListHookState::CommandQueueOriginals),
			&DX12CommandListHookState::VtableBaseCache);
	}

	template <typename T>
	static T GetCommandListOriginal(
		ID3D12GraphicsCommandList *commandList, UINT slot, T fallback, const char *name)
	{
		return DX12OriginalFunctionRegistry::ResolveCached(
			commandList, slot, fallback, name,
			DX12CommandListHookState::CommandListOriginals,
			ARRAYSIZE(DX12CommandListHookState::CommandListOriginals),
			&DX12CommandListHookState::VtableBaseCache);
	}
};
