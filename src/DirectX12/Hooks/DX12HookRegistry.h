#pragma once

// Hook registry/manager and command-list hook slot types.
// Split out of DX12Hooks.h; include that umbrella header from existing code.

#include "../DX12Core.h"

#include "Nektra/NktHookLib.h"
#include "util_min.h"
#include "crc32c.h"

#include <d3dcompiler.h>

#include <climits>
#include <condition_variable>
#include <limits>
#include <map>
#include <mutex>
#include <thread>

class DX12HookRegistry
{
public:
	SRWLOCK lock;
	CNktHookLib hookManager;
	std::unordered_set<void*> hookedFunctions;
	std::unordered_map<void*, void*> originalFunctions;
	volatile LONG hooksFinalized = 0;
	// Vtable method hooks are never installed in wrapper-only mode.  Keeping
	// the counter lets original-resolution cache raw vtable entries as positive
	// results (zero means no method code in d3d12.dll/dxgi.dll was patched).
	volatile LONG vtableHooksInstalled = 0;

	DX12HookRegistry()
	{
		InitializeSRWLock(&lock);
		// Install patches with all other threads suspended. Nektra's default
		// inline hook rewrites the target's entry bytes non-atomically; if the
		// game executes the function during the rewrite, the torn call can
		// corrupt a wait/sync/init sequence and leave the game in a broken
		// state (observed as permanent 100ms timeout polling, GPU pegged at
		// 99%, flicker) - randomly, depending on whether a thread happened to
		// be inside the patched function at install time. Suspending threads
		// during installation removes that race for every export hook.
		hookManager.SetSuspendThreadsWhileHooking(TRUE);
	}

	static DX12HookRegistry &Get()
	{
		static DX12HookRegistry state;
		return state;
	}

	static bool RememberHook(void *target)
	{
		DX12HookRegistry &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		bool inserted = state.hookedFunctions.insert(target).second;
		ReleaseSRWLockExclusive(&state.lock);
		return inserted;
	}

	static void RememberOriginal(void *target, void *original)
	{
		if (!target || !original)
			return;

		DX12HookRegistry &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		state.originalFunctions[target] = original;
		ReleaseSRWLockExclusive(&state.lock);
	}

	static DWORD HookFunction(void **original, void *target, void *hook, const char *name)
	{
		if (!target || !hook)
			return ERROR_PROC_NOT_FOUND;
		if (!RememberHook(target))
			return ERROR_SUCCESS;

		SIZE_T hookId = 0;
		DWORD result = Get().hookManager.Hook(&hookId, original, target, hook);
		if (result == ERROR_SUCCESS && original)
			RememberOriginal(target, *original);
		DX12_LOG(name ? name : "HookInstall",
			"\"event\":\"HookInstall\",\"status\":\"%s\",\"target\":\"%p\",\"original\":\"%p\",\"result\":\"0x%lx\"",
			result == ERROR_SUCCESS ? "installed" : "failed",
			target, original ? *original : nullptr, result);
		return result;
	}

	static void FinalizeHooks()
	{
		InterlockedExchange(&Get().hooksFinalized, 1);
	}

	static bool AnyVtableHooksInstalled()
	{
		return InterlockedCompareExchange(&Get().vtableHooksInstalled, 0, 0) != 0;
	}

	static void *GetOriginalFunction(void *target)
	{
		if (!target)
			return nullptr;

		// Wrapper mode never patches vtables: an unhooked vtable entry IS
		// the original implementation, and the registry only ever contains
		// entries for actually patched targets. Skipping the lookup removes
		// a shared SRW lock acquire plus an unordered_map find from every
		// hot hook call (CopyDescriptors / Create*View run thousands of
		// times per frame).
		if (!AnyVtableHooksInstalled())
			return nullptr;

		DX12HookRegistry &state = Get();
		AcquireSRWLockShared(&state.lock);
		auto it = state.originalFunctions.find(target);
		void *original = it != state.originalFunctions.end() ? it->second : nullptr;
		ReleaseSRWLockShared(&state.lock);
		return original;
	}

	static void UnhookAll()
	{
		Get().hookManager.UnhookAll();
	}

};


class DX12HookManager
{
public:
	static DWORD InstallExportHook(HMODULE module, const char *exportName, void **original, void *hook)
	{
		if (!module || !exportName)
			return ERROR_PROC_NOT_FOUND;

		return DX12HookRegistry::HookFunction(original, GetProcAddress(module, exportName), hook, exportName);
	}
};


class DX12CommandListHookSlotTypes
{
public:
	class ID3D12GraphicsCommandList
	{
	public:
		static constexpr UINT Close = 9;
		static constexpr UINT Reset = 10;
		static constexpr UINT ClearState = 11;
		static constexpr UINT DrawInstanced = 12;
		static constexpr UINT DrawIndexedInstanced = 13;
		static constexpr UINT Dispatch = 14;
		static constexpr UINT CopyBufferRegion = 15;
		static constexpr UINT CopyTextureRegion = 16;
		static constexpr UINT CopyResource = 17;
		static constexpr UINT CopyTiles = 18;
		static constexpr UINT ResolveSubresource = 19;
		static constexpr UINT IaSetPrimitiveTopology = 20;
		static constexpr UINT RsSetViewports = 21;
		static constexpr UINT RsSetScissorRects = 22;
		static constexpr UINT OmSetBlendFactor = 23;
		static constexpr UINT OmSetStencilRef = 24;
		static constexpr UINT SetPipelineState = 25;
		static constexpr UINT ResourceBarrier = 26;
		static constexpr UINT ExecuteBundle = 27;
		static constexpr UINT SetDescriptorHeaps = 28;
		static constexpr UINT SetComputeRootSignature = 29;
		static constexpr UINT SetGraphicsRootSignature = 30;
		static constexpr UINT SetComputeRootDescriptorTable = 31;
		static constexpr UINT SetGraphicsRootDescriptorTable = 32;
		static constexpr UINT SetComputeRoot32BitConstant = 33;
		static constexpr UINT SetGraphicsRoot32BitConstant = 34;
		static constexpr UINT SetComputeRoot32BitConstants = 35;
		static constexpr UINT SetGraphicsRoot32BitConstants = 36;
		static constexpr UINT SetComputeRootConstantBufferView = 37;
		static constexpr UINT SetGraphicsRootConstantBufferView = 38;
		static constexpr UINT SetComputeRootShaderResourceView = 39;
		static constexpr UINT SetGraphicsRootShaderResourceView = 40;
		static constexpr UINT SetComputeRootUnorderedAccessView = 41;
		static constexpr UINT SetGraphicsRootUnorderedAccessView = 42;
		static constexpr UINT IaSetIndexBuffer = 43;
		static constexpr UINT IaSetVertexBuffers = 44;
		static constexpr UINT SoSetTargets = 45;
		static constexpr UINT OmSetRenderTargets = 46;
		static constexpr UINT ClearDepthStencilView = 47;
		static constexpr UINT ClearRenderTargetView = 48;
		static constexpr UINT ClearUnorderedAccessViewUint = 49;
		static constexpr UINT ClearUnorderedAccessViewFloat = 50;
		static constexpr UINT DiscardResource = 51;
		static constexpr UINT BeginQuery = 52;
		static constexpr UINT EndQuery = 53;
		static constexpr UINT ResolveQueryData = 54;
		static constexpr UINT SetPredication = 55;
		static constexpr UINT SetMarker = 56;
		static constexpr UINT BeginEvent = 57;
		static constexpr UINT EndEvent = 58;
		static constexpr UINT ExecuteIndirect = 59;
	};

	class ID3D12CommandQueue
	{
	public:
		static constexpr UINT UpdateTileMappings = 8;
		static constexpr UINT CopyTileMappings = 9;
		static constexpr UINT ExecuteCommandLists = 10;
		static constexpr UINT SetMarker = 11;
		static constexpr UINT BeginEvent = 12;
		static constexpr UINT EndEvent = 13;
		static constexpr UINT Signal = 14;
		static constexpr UINT Wait = 15;
		static constexpr UINT GetTimestampFrequency = 16;
		static constexpr UINT GetClockCalibration = 17;
	};

	class ID3D12Device
	{
	public:
		static constexpr UINT CreateCommandQueue = 8;
		static constexpr UINT CreateCommandAllocator = 9;
		static constexpr UINT CreateCommandList = 12;
	};

	class ID3D12Device4
	{
	public:
		static constexpr UINT CreateCommandList1 = 52;
	};
};


