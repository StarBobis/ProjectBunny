// DX12Wrapper namespace entry points (proxy/present bootstrap).
// Split from DX12Presentation.h; keep declarations in the layer that owns them.
#pragma once

#include "../DX12Resources.h"
#include "../Config/DX12Config.h"

namespace DX12Wrapper
{
	bool WrapD3D12DeviceResult(REFIID riid, void **device);
	bool WrapD3D12FenceResult(REFIID riid, void **fence);
	bool WrapD3D12ResourceResult(REFIID riid, void **resource);
	bool WrapD3D12PipelineStateResult(REFIID riid, void **pipelineState);
	bool WrapD3D12DebugInterfaceResult(REFIID riid, void **object);
	bool WrapD3D12AuxiliaryResult(REFIID riid, void **object);
	bool WrapD3D12DeviceFactoryResult(REFIID riid, void **object);
	bool WrapDXGIFactoryResult(REFIID riid, void **factory);
	bool WrapDXGISwapChainResult(REFIID riid, void **swapChain);
	IUnknown *Unwrap(IUnknown *object);
	void Shutdown();
}


