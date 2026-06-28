#pragma once

#include <Windows.h>
#include <d3d12.h>

#include "DX12LogManager.hpp"

class DX12CommandQueueState
{
public:
	SRWLOCK lock;
	ID3D12CommandQueue *queue = nullptr;
	volatile LONG nonDirectLogs = 0;

	DX12CommandQueueState()
	{
		InitializeSRWLock(&lock);
	}

	static DX12CommandQueueState &Get()
	{
		static DX12CommandQueueState state;
		return state;
	}

	static void Set(ID3D12CommandQueue *queue)
	{
		DX12CommandQueueState &state = Get();
		if (queue) {
			D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
			if (desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
				if (InterlockedIncrement(&state.nonDirectLogs) <= 8)
					DX12LogManager::JsonFunc("DX12CommandQueueState",
						"\"event\":\"ignore_non_direct\",\"queue\":\"%p\",\"type\":%d",
						queue, static_cast<int>(desc.Type));
				return;
			}
			DX12LogManager::JsonFunc("DX12CommandQueueState",
				"\"event\":\"accept_direct\",\"queue\":\"%p\",\"type\":%d",
				queue, static_cast<int>(desc.Type));
		}

		if (queue)
			queue->AddRef();

		AcquireSRWLockExclusive(&state.lock);
		ID3D12CommandQueue *oldQueue = state.queue;
		state.queue = queue;
		ReleaseSRWLockExclusive(&state.lock);

		if (oldQueue)
			oldQueue->Release();
	}

	static ID3D12CommandQueue *Acquire()
	{
		DX12CommandQueueState &state = Get();
		AcquireSRWLockShared(&state.lock);
		ID3D12CommandQueue *queue = state.queue;
		if (queue)
			queue->AddRef();
		ReleaseSRWLockShared(&state.lock);
		return queue;
	}
};
