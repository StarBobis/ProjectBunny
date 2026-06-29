#pragma once

#include <Windows.h>
#include <dxgi1_4.h>

class DXGIHookSignatures
{
public:
	using CreateSwapChain = HRESULT(STDMETHODCALLTYPE *)(
		IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
	using CreateSwapChainForHwnd = HRESULT(STDMETHODCALLTYPE *)(
		IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
		const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
	using CreateSwapChainForCoreWindow = HRESULT(STDMETHODCALLTYPE *)(
		IDXGIFactory2*, IUnknown*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*,
		IDXGIOutput*, IDXGISwapChain1**);
	using CreateSwapChainForComposition = HRESULT(STDMETHODCALLTYPE *)(
		IDXGIFactory2*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**);
	using Present = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain*, UINT, UINT);
	using Present1 = HRESULT(STDMETHODCALLTYPE *)(
		IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
	using ResizeBuffers = HRESULT(STDMETHODCALLTYPE *)(
		IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
	using CreateDXGIFactory = HRESULT(WINAPI *)(REFIID, void**);
	using CreateDXGIFactory2 = HRESULT(WINAPI *)(UINT, REFIID, void**);
};
