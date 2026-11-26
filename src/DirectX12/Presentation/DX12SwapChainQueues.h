#pragma once

#include <dxgi1_4.h>
#include <d3d12.h>

// IDXGIObject owns interface-valued private data. GetPrivateData returns a
// strong reference even if the swap chain's binding changes concurrently.
namespace DX12SwapChainQueues
{
	inline GUID Key(UINT index)
	{
		GUID key = { 0x34d9594a, 0x7c60, 0x4a56, { 0x90, 0x2c, 0xf4, 0x19, 0x78, 0xd5, 0x80, 0 } };
		key.Data4[7] = static_cast<BYTE>(index);
		return key;
	}

	inline void Bind(IDXGISwapChain *chain, UINT index, IUnknown *object)
	{
		ID3D12CommandQueue *queue = nullptr;
		if (object)
			object->QueryInterface(IID_PPV_ARGS(&queue));
		chain->SetPrivateDataInterface(Key(index), queue);
		if (queue)
			queue->Release();
	}

	inline ID3D12CommandQueue *Acquire(IDXGISwapChain *chain)
	{
		IDXGISwapChain3 *chain3 = nullptr;
		ID3D12CommandQueue *queue = nullptr;
		UINT size = sizeof(queue);
		if (SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&chain3)))) {
			const UINT index = chain3->GetCurrentBackBufferIndex();
			chain3->Release();
			if (SUCCEEDED(chain->GetPrivateData(Key(index + 1), &size, &queue)))
				return queue;
		}
		size = sizeof(queue);
		chain->GetPrivateData(Key(0), &size, &queue);
		return queue;
	}
}
