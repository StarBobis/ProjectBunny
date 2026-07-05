#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdint.h>

#include "DX12LogManager.hpp"
#include "DX12RuntimeState.hpp"

class DX12OuterTimingProbeUtils
{
public:
	inline static volatile LONG64 LastPresentEnterTicks = 0;
	inline static volatile LONG64 LastPresentLeaveTicks = 0;

	static LARGE_INTEGER Now()
	{
		LARGE_INTEGER value = {};
		QueryPerformanceCounter(&value);
		return value;
	}

	static LONG64 DeltaUs(LONGLONG beginTicks, LONGLONG endTicks)
	{
		LARGE_INTEGER frequency = {};
		QueryPerformanceFrequency(&frequency);
		if (frequency.QuadPart == 0 || endTicks < beginTicks)
			return 0;
		return static_cast<LONG64>(
			(endTicks - beginTicks) * 1000000LL / frequency.QuadPart);
	}

	static LONG64 ElapsedUs(const LARGE_INTEGER &begin)
	{
		LARGE_INTEGER end = Now();
		return DeltaUs(begin.QuadPart, end.QuadPart);
	}

	static UINT64 FenceCompletedValue(ID3D12Fence *fence)
	{
		if (!fence)
			return 0;
		return fence->GetCompletedValue();
	}

	static void LogPresentEnter(
		const char *api,
		IDXGISwapChain *swapChain,
		UINT syncInterval,
		UINT flags)
	{
		LARGE_INTEGER now = Now();
		LONG64 previousEnter = InterlockedExchange64(
			&LastPresentEnterTicks,
			static_cast<LONG64>(now.QuadPart));
		LONG64 previousLeave = InterlockedCompareExchange64(
			&LastPresentLeaveTicks,
			0,
			0);
		LONG64 enterGapUs = previousEnter ? DeltaUs(previousEnter, now.QuadPart) : 0;
		LONG64 idleGapUs = previousLeave ? DeltaUs(previousLeave, now.QuadPart) : 0;
		DX12LogManager::JsonFunc("DX12OuterTiming",
			"\"domain\":\"present\",\"api\":\"%s\",\"stage\":\"enter\",\"present\":%ld,\"threadId\":%lu,\"swapchain\":\"%p\",\"syncInterval\":%u,\"flags\":\"0x%x\",\"enterGapUs\":%lld,\"idleGapUs\":%lld",
			api ? api : "",
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			swapChain,
			syncInterval,
			flags,
			static_cast<long long>(enterGapUs),
			static_cast<long long>(idleGapUs));
		DX12LogManager::JsonFunc("DX12OuterTiming",
			"\"domain\":\"game_body\",\"api\":\"%s\",\"stage\":\"between_present_leave_and_enter\",\"present\":%ld,\"threadId\":%lu,\"swapchain\":\"%p\",\"elapsedUs\":%lld",
			api ? api : "",
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			swapChain,
			static_cast<long long>(idleGapUs));
	}

	static void LogPresentLeave(const char *api, IDXGISwapChain *swapChain, HRESULT hr)
	{
		LARGE_INTEGER now = Now();
		InterlockedExchange64(&LastPresentLeaveTicks, static_cast<LONG64>(now.QuadPart));
		DX12LogManager::JsonFunc("DX12OuterTiming",
			"\"domain\":\"present\",\"api\":\"%s\",\"stage\":\"leave\",\"present\":%ld,\"threadId\":%lu,\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
			api ? api : "",
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			swapChain,
			hr);
	}

	static void LogPresentSlice(
		const char *api,
		const char *stage,
		IDXGISwapChain *swapChain,
		HRESULT hr,
		LONG64 elapsedUs)
	{
		DX12LogManager::JsonFunc("DX12OuterTiming",
			"\"domain\":\"present\",\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"threadId\":%lu,\"swapchain\":\"%p\",\"hr\":\"0x%lx\",\"elapsedUs\":%lld",
			api ? api : "",
			stage ? stage : "",
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			swapChain,
			hr,
			static_cast<long long>(elapsedUs));
	}

	static void LogQueueExecuteSlice(
		const char *stage,
		ID3D12CommandQueue *queue,
		UINT numCommandLists,
		LONG64 elapsedUs)
	{
		DX12LogManager::JsonFunc("DX12OuterTiming",
			"\"domain\":\"queue\",\"api\":\"ExecuteCommandLists\",\"stage\":\"%s\",\"present\":%ld,\"threadId\":%lu,\"queue\":\"%p\",\"commandLists\":%u,\"elapsedUs\":%lld",
			stage ? stage : "",
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			queue,
			numCommandLists,
			static_cast<long long>(elapsedUs));
	}

	static void LogQueueFenceSlice(
		const char *api,
		const char *stage,
		ID3D12CommandQueue *queue,
		ID3D12Fence *fence,
		UINT64 value,
		UINT64 completedBefore,
		UINT64 completedAfter,
		HRESULT hr,
		LONG64 elapsedUs)
	{
		DX12LogManager::JsonFunc("DX12OuterTiming",
			"\"domain\":\"queue_fence\",\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"threadId\":%lu,\"queue\":\"%p\",\"fence\":\"%p\",\"value\":%llu,\"completedBefore\":%llu,\"completedAfter\":%llu,\"hr\":\"0x%lx\",\"elapsedUs\":%lld",
			api ? api : "",
			stage ? stage : "",
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			queue,
			fence,
			static_cast<unsigned long long>(value),
			static_cast<unsigned long long>(completedBefore),
			static_cast<unsigned long long>(completedAfter),
			hr,
			static_cast<long long>(elapsedUs));
	}

	static void LogFenceSlice(
		const char *api,
		const char *stage,
		ID3D12Fence *fence,
		UINT64 value,
		UINT64 completedBefore,
		UINT64 completedAfter,
		HRESULT hr,
		LONG64 elapsedUs)
	{
		DX12LogManager::JsonFunc("DX12OuterTiming",
			"\"domain\":\"fence\",\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"threadId\":%lu,\"fence\":\"%p\",\"value\":%llu,\"completedBefore\":%llu,\"completedAfter\":%llu,\"hr\":\"0x%lx\",\"elapsedUs\":%lld",
			api ? api : "",
			stage ? stage : "",
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			fence,
			static_cast<unsigned long long>(value),
			static_cast<unsigned long long>(completedBefore),
			static_cast<unsigned long long>(completedAfter),
			hr,
			static_cast<long long>(elapsedUs));
	}

	static void LogEventWaitSlice(
		const char *api,
		const char *stage,
		HANDLE eventHandle,
		DWORD waitResult,
		DWORD waitError,
		DWORD timeoutMs,
		LONG64 elapsedUs)
	{
		DX12LogManager::JsonFunc("DX12OuterTiming",
			"\"domain\":\"event_wait\",\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"threadId\":%lu,\"event\":\"%p\",\"waitResult\":%lu,\"waitError\":%lu,\"timeoutMs\":%lu,\"elapsedUs\":%lld",
			api ? api : "",
			stage ? stage : "",
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			eventHandle,
			waitResult,
			waitError,
			timeoutMs,
			static_cast<long long>(elapsedUs));
	}

	static void LogWorkerTask(
		const char *queueName,
		const char *eventName,
		UINT taskKind,
		UINT64 sequence,
		size_t itemCount,
		size_t pending,
		DWORD enqueueThreadId,
		LONG64 elapsedUs)
	{
		DX12LogManager::JsonFunc("DX12OuterTiming",
			"\"domain\":\"worker\",\"queue\":\"%s\",\"event\":\"%s\",\"present\":%ld,\"workerThreadId\":%lu,\"enqueueThreadId\":%lu,\"kind\":%u,\"sequence\":%llu,\"items\":%zu,\"pending\":%zu,\"elapsedUs\":%lld",
			queueName ? queueName : "",
			eventName ? eventName : "",
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			enqueueThreadId,
			taskKind,
			static_cast<unsigned long long>(sequence),
			itemCount,
			pending,
			static_cast<long long>(elapsedUs));
	}

	static void LogWorkerWait(
		const char *queueName,
		const char *reason,
		const char *mode,
		UINT64 targetSequence,
		UINT64 completedSequence,
		size_t pending,
		LONG64 elapsedUs)
	{
		DX12LogManager::JsonFunc("DX12OuterTiming",
			"\"domain\":\"worker_wait\",\"queue\":\"%s\",\"reason\":\"%s\",\"mode\":\"%s\",\"present\":%ld,\"threadId\":%lu,\"targetSequence\":%llu,\"completedSequence\":%llu,\"pending\":%zu,\"elapsedUs\":%lld",
			queueName ? queueName : "",
			reason ? reason : "",
			mode ? mode : "",
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			static_cast<unsigned long long>(targetSequence),
			static_cast<unsigned long long>(completedSequence),
			pending,
			static_cast<long long>(elapsedUs));
	}
};
