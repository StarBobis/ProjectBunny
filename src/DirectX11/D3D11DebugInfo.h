#pragma once

#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <wrl/client.h>

#if _DEBUG_LAYER
// Only where _DEBUG_LAYER=1.  This enables the debug layer for the GPU
// itself, which will show GPU errors, warnings, and leaks in the console output.
// This call shows that the layer is active, and the initial LiveObjectState.
// And enable debugger breaks for errors that we might be introducing.

static void ShowDebugInfo(ID3D11Device *origDevice)
{
	if (!origDevice) return;
	Microsoft::WRL::ComPtr<ID3D11Debug> d3dDebug;
	if (FAILED(origDevice->QueryInterface(IID_PPV_ARGS(d3dDebug.GetAddressOf())))) return;
	Microsoft::WRL::ComPtr<ID3D11InfoQueue> d3dInfoQueue;
	if (SUCCEEDED(d3dDebug->QueryInterface(IID_PPV_ARGS(d3dInfoQueue.GetAddressOf())))) {
		d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
		d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
	}
	d3dDebug->ReportLiveDeviceObjects(D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL);
}


#endif
