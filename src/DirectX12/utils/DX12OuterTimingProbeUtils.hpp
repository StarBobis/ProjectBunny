#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdint.h>

// All OuterTiming probes disabled — 4M log lines/frame was excessive.
// Replaced with DX11-style accumulator pattern (DX12LogInfoTS on demand).
// Class kept as no-op stub so call sites don't need immediate cleanup.
// TODO: remove call sites and delete this file entirely.

class DX12OuterTimingProbeUtils
{
public:
	static LARGE_INTEGER Now()
	{
		LARGE_INTEGER v = {};
		return v;
	}

	static LONG64 DeltaUs(LONGLONG, LONGLONG)
	{
		return 0;
	}

	static LONG64 ElapsedUs(const LARGE_INTEGER &)
	{
		return 0;
	}

	static UINT64 FenceCompletedValue(ID3D12Fence *)
	{
		return 0;
	}

	static void LogPresentEnter(const char *, IDXGISwapChain *, UINT, UINT) {}
	static void LogPresentLeave(const char *, IDXGISwapChain *, HRESULT) {}
	static void LogPresentSlice(const char *, const char *, IDXGISwapChain *, HRESULT, LONG64) {}
	static void LogQueueExecuteSlice(const char *, ID3D12CommandQueue *, UINT, LONG64) {}
	static void LogQueueFenceSlice(const char *, const char *, ID3D12CommandQueue *, ID3D12Fence *, UINT64, UINT64, UINT64, HRESULT, LONG64) {}
	static void LogFenceSlice(const char *, const char *, ID3D12Fence *, UINT64, UINT64, UINT64, HRESULT, LONG64) {}
	static void LogEventWaitSlice(const char *, const char *, HANDLE, DWORD, DWORD, DWORD, LONG64) {}
	static void LogWorkerTask(const char *, const char *, UINT, UINT64, size_t, size_t, DWORD, LONG64) {}
	static void LogWorkerWait(const char *, const char *, const char *, UINT64, UINT64, size_t, LONG64) {}
};
