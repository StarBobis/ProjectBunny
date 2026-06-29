#pragma once

#include <Windows.h>
#include <dxgi1_4.h>

class DXGIHookCallbacks
{
public:
	static HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void **factory);
	static HRESULT WINAPI HookedCreateDXGIFactory1(REFIID riid, void **factory);
	static HRESULT WINAPI HookedCreateDXGIFactory2(UINT flags, REFIID riid, void **factory);
	static HRESULT STDMETHODCALLTYPE HookedCreateSwapChain(
		IDXGIFactory *factory, IUnknown *device, DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **swapChain);
	static HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForHwnd(
		IDXGIFactory2 *factory, IUnknown *device, HWND window, const DXGI_SWAP_CHAIN_DESC1 *desc,
		const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreenDesc, IDXGIOutput *output, IDXGISwapChain1 **swapChain);
	static HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForCoreWindow(
		IDXGIFactory2 *factory, IUnknown *device, IUnknown *window, const DXGI_SWAP_CHAIN_DESC1 *desc,
		IDXGIOutput *output, IDXGISwapChain1 **swapChain);
	static HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForComposition(
		IDXGIFactory2 *factory, IUnknown *device, const DXGI_SWAP_CHAIN_DESC1 *desc,
		IDXGIOutput *output, IDXGISwapChain1 **swapChain);
	static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain *swapChain, UINT syncInterval, UINT flags);
	static HRESULT STDMETHODCALLTYPE HookedPresent1(
		IDXGISwapChain1 *swapChain, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS *presentParameters);
	static HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
		IDXGISwapChain *swapChain, UINT bufferCount, UINT width, UINT height,
		DXGI_FORMAT newFormat, UINT swapChainFlags);
};
