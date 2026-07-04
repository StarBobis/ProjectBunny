#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

namespace DX12Wrapper
{
	bool WrapD3D12DeviceResult(REFIID riid, void **device);
	bool WrapD3D12CommandAllocatorResult(REFIID riid, void **allocator);
	bool WrapD3D12FenceResult(REFIID riid, void **fence);
	bool WrapD3D12DescriptorHeapResult(REFIID riid, void **heap);
	bool WrapD3D12ResourceResult(REFIID riid, void **resource);
	bool WrapD3D12HeapResult(REFIID riid, void **heap);
	bool WrapD3D12PipelineStateResult(REFIID riid, void **pipelineState);
	bool WrapD3D12RootSignatureResult(REFIID riid, void **rootSignature);
	bool WrapD3D12QueryHeapResult(REFIID riid, void **queryHeap);
	bool WrapD3D12CommandSignatureResult(REFIID riid, void **commandSignature);
	bool WrapD3D12LifetimeTrackerResult(REFIID riid, void **tracker);
	bool WrapD3D12ProtectedResourceSessionResult(REFIID riid, void **session);
	bool WrapD3D12MetaCommandResult(REFIID riid, void **metaCommand);
	bool WrapD3D12StateObjectResult(REFIID riid, void **stateObject);
	bool WrapD3D12PipelineLibraryResult(REFIID riid, void **library);
	bool WrapD3D12ShaderCacheSessionResult(REFIID riid, void **session);
	bool WrapD3D12DeviceFactoryResult(REFIID riid, void **object);
	bool WrapDXGIFactoryResult(REFIID riid, void **factory);
	bool WrapDXGISwapChainResult(REFIID riid, void **swapChain);
	IUnknown *Unwrap(IUnknown *object);
	void Shutdown();
}
