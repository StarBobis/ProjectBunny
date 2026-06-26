#include "DX12Overlay.h"

#include <algorithm>
#include <d2d1_1.h>
#include <d3d11_1.h>
#include <d3d11on12.h>
#include <dwrite.h>
#include <dxgi1_4.h>

#include "DX12State.h"

namespace
{
	template <typename T>
	void SafeRelease(T *&ptr)
	{
		if (ptr) {
			ptr->Release();
			ptr = nullptr;
		}
	}

	struct DX12D2DOverlayState
	{
		static constexpr UINT MaxBuffers = 8;

		IDXGISwapChain3 *swapChain = nullptr;
		ID3D12Device *device = nullptr;
		ID3D12CommandQueue *queue = nullptr;
		ID3D11Device *d3d11Device = nullptr;
		ID3D11DeviceContext *d3d11Context = nullptr;
		ID3D11On12Device *d3d11On12 = nullptr;
		ID2D1Factory1 *d2dFactory = nullptr;
		ID2D1Device *d2dDevice = nullptr;
		ID2D1DeviceContext *d2dContext = nullptr;
		IDWriteFactory *dwriteFactory = nullptr;
		IDWriteTextFormat *textFormat = nullptr;
		ID2D1SolidColorBrush *textBrush = nullptr;
		ID2D1SolidColorBrush *shadowBrush = nullptr;
		ID2D1SolidColorBrush *backgroundBrush = nullptr;
		ID3D11Resource *wrappedBuffers[MaxBuffers] = {};
		ID2D1Bitmap1 *d2dTargets[MaxBuffers] = {};
		UINT bufferCount = 0;
		UINT width = 0;
		UINT height = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		bool initialized = false;
	};

	static DX12D2DOverlayState gOverlay;
	static SRWLOCK gOverlayLock = SRWLOCK_INIT;

	static void ResetOverlayResourcesLocked()
	{
		for (UINT i = 0; i < DX12D2DOverlayState::MaxBuffers; ++i) {
			SafeRelease(gOverlay.d2dTargets[i]);
			SafeRelease(gOverlay.wrappedBuffers[i]);
		}
		SafeRelease(gOverlay.backgroundBrush);
		SafeRelease(gOverlay.shadowBrush);
		SafeRelease(gOverlay.textBrush);
		SafeRelease(gOverlay.textFormat);
		SafeRelease(gOverlay.dwriteFactory);
		SafeRelease(gOverlay.d2dContext);
		SafeRelease(gOverlay.d2dDevice);
		SafeRelease(gOverlay.d2dFactory);
		SafeRelease(gOverlay.d3d11On12);
		SafeRelease(gOverlay.d3d11Context);
		SafeRelease(gOverlay.d3d11Device);
		SafeRelease(gOverlay.queue);
		SafeRelease(gOverlay.device);
		SafeRelease(gOverlay.swapChain);
		gOverlay.bufferCount = 0;
		gOverlay.width = 0;
		gOverlay.height = 0;
		gOverlay.format = DXGI_FORMAT_UNKNOWN;
		gOverlay.initialized = false;
	}

	static D2D1_COLOR_F D2DColorFromColorRef(COLORREF color, float alpha = 1.0f)
	{
		return D2D1::ColorF(
			GetRValue(color) / 255.0f,
			GetGValue(color) / 255.0f,
			GetBValue(color) / 255.0f,
			alpha);
	}

	static bool GetSwapChainDesc(IDXGISwapChain *swapChain, DXGI_SWAP_CHAIN_DESC *desc)
	{
		if (!swapChain || !desc)
			return false;
		ZeroMemory(desc, sizeof(*desc));
		if (FAILED(swapChain->GetDesc(desc)))
			return false;
		if (!desc->BufferCount || desc->BufferCount > DX12D2DOverlayState::MaxBuffers)
			return false;
		return true;
	}

	static bool CreateD2DTargetsLocked(IDXGISwapChain3 *swapChain, const DXGI_SWAP_CHAIN_DESC &desc)
	{
		D3D11_RESOURCE_FLAGS d3d11Flags = {};
		d3d11Flags.BindFlags = D3D11_BIND_RENDER_TARGET;

		D2D1_BITMAP_PROPERTIES1 bitmapProps = D2D1::BitmapProperties1(
			D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
			D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));

		for (UINT i = 0; i < desc.BufferCount; ++i) {
			ID3D12Resource *backBuffer = nullptr;
			HRESULT hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));
			if (FAILED(hr) || !backBuffer)
				return false;

			hr = gOverlay.d3d11On12->CreateWrappedResource(
				backBuffer,
				&d3d11Flags,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_PRESENT,
				IID_PPV_ARGS(&gOverlay.wrappedBuffers[i]));
			backBuffer->Release();
			if (FAILED(hr) || !gOverlay.wrappedBuffers[i])
				return false;

			IDXGISurface *surface = nullptr;
			hr = gOverlay.wrappedBuffers[i]->QueryInterface(IID_PPV_ARGS(&surface));
			if (FAILED(hr) || !surface)
				return false;

			hr = gOverlay.d2dContext->CreateBitmapFromDxgiSurface(
				surface, &bitmapProps, &gOverlay.d2dTargets[i]);
			surface->Release();
			if (FAILED(hr) || !gOverlay.d2dTargets[i])
				return false;
		}
		return true;
	}

	static bool InitializeOverlayLocked(IDXGISwapChain *swapChain, ID3D12CommandQueue *queue)
	{
		DXGI_SWAP_CHAIN_DESC desc = {};
		if (!GetSwapChainDesc(swapChain, &desc) || !queue)
			return false;

		IDXGISwapChain3 *swapChain3 = nullptr;
		if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))) || !swapChain3)
			return false;

		ID3D12Device *device = nullptr;
		if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || !device) {
			swapChain3->Release();
			return false;
		}

		ResetOverlayResourcesLocked();
		gOverlay.swapChain = swapChain3;
		gOverlay.device = device;
		gOverlay.queue = queue;
		gOverlay.queue->AddRef();
		gOverlay.bufferCount = desc.BufferCount;
		gOverlay.width = desc.BufferDesc.Width;
		gOverlay.height = desc.BufferDesc.Height;
		gOverlay.format = desc.BufferDesc.Format;

		IUnknown *queues[] = { gOverlay.queue };
		HRESULT hr = D3D11On12CreateDevice(
			gOverlay.device,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			nullptr,
			0,
			queues,
			1,
			0,
			&gOverlay.d3d11Device,
			&gOverlay.d3d11Context,
			nullptr);
		if (FAILED(hr) || !gOverlay.d3d11Device || !gOverlay.d3d11Context)
			return false;

		hr = gOverlay.d3d11Device->QueryInterface(IID_PPV_ARGS(&gOverlay.d3d11On12));
		if (FAILED(hr) || !gOverlay.d3d11On12)
			return false;

		D2D1_FACTORY_OPTIONS factoryOptions = {};
		hr = D2D1CreateFactory(
			D2D1_FACTORY_TYPE_SINGLE_THREADED,
			__uuidof(ID2D1Factory1),
			&factoryOptions,
			reinterpret_cast<void**>(&gOverlay.d2dFactory));
		if (FAILED(hr) || !gOverlay.d2dFactory)
			return false;

		IDXGIDevice *dxgiDevice = nullptr;
		hr = gOverlay.d3d11Device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
		if (FAILED(hr) || !dxgiDevice)
			return false;
		hr = gOverlay.d2dFactory->CreateDevice(dxgiDevice, &gOverlay.d2dDevice);
		dxgiDevice->Release();
		if (FAILED(hr) || !gOverlay.d2dDevice)
			return false;

		hr = gOverlay.d2dDevice->CreateDeviceContext(
			D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
			&gOverlay.d2dContext);
		if (FAILED(hr) || !gOverlay.d2dContext)
			return false;

		hr = DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(&gOverlay.dwriteFactory));
		if (FAILED(hr) || !gOverlay.dwriteFactory)
			return false;

		hr = gOverlay.dwriteFactory->CreateTextFormat(
			L"Microsoft YaHei UI",
			nullptr,
			DWRITE_FONT_WEIGHT_BOLD,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			18.0f,
			L"zh-cn",
			&gOverlay.textFormat);
		if (FAILED(hr) || !gOverlay.textFormat)
			return false;
		gOverlay.textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

		hr = gOverlay.d2dContext->CreateSolidColorBrush(
			D2D1::ColorF(0.0f, 1.0f, 0.0f, 1.0f), &gOverlay.textBrush);
		if (FAILED(hr) || !gOverlay.textBrush)
			return false;
		hr = gOverlay.d2dContext->CreateSolidColorBrush(
			D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.85f), &gOverlay.shadowBrush);
		if (FAILED(hr) || !gOverlay.shadowBrush)
			return false;
		hr = gOverlay.d2dContext->CreateSolidColorBrush(
			D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.55f), &gOverlay.backgroundBrush);
		if (FAILED(hr) || !gOverlay.backgroundBrush)
			return false;

		if (!CreateD2DTargetsLocked(swapChain3, desc))
			return false;

		gOverlay.initialized = true;
		DX12LogJsonFunc("OverlayD2DInit",
			"\"status\":\"ok\",\"swapchain\":\"%p\",\"queue\":\"%p\",\"buffers\":%u,\"format\":%u",
			swapChain, queue, desc.BufferCount, static_cast<UINT>(desc.BufferDesc.Format));
		return true;
	}

	static bool OverlayMatchesLocked(IDXGISwapChain *swapChain, ID3D12CommandQueue *queue)
	{
		if (!gOverlay.initialized || !gOverlay.swapChain || !gOverlay.queue)
			return false;
		if (gOverlay.swapChain != swapChain || gOverlay.queue != queue)
			return false;

		DXGI_SWAP_CHAIN_DESC desc = {};
		if (!GetSwapChainDesc(swapChain, &desc))
			return false;
		return desc.BufferCount == gOverlay.bufferCount &&
			desc.BufferDesc.Width == gOverlay.width &&
			desc.BufferDesc.Height == gOverlay.height &&
			desc.BufferDesc.Format == gOverlay.format;
	}

	static UINT CountTextLines(const wchar_t *text)
	{
		if (!text || !text[0])
			return 0;
		UINT lines = 1;
		for (const wchar_t *p = text; *p; ++p) {
			if (*p == L'\n')
				++lines;
		}
		return lines;
	}
}

DWORD WINAPI DX12OverlayThread(void*)
{
	return 0;
}

void DX12EnsureOverlayWindow()
{
}

bool DX12UpdateOverlayWindowForSwapChain(IDXGISwapChain *swapChain)
{
	return DX12RenderOverlayToSwapChain(swapChain);
}

bool DX12RenderOverlayToSwapChain(IDXGISwapChain *swapChain)
{
	if (!swapChain)
		return false;

	DX12ClearExpiredOverlayStatus();
	wchar_t text[1024] = {};
	DX12GetOverlayStatus(text, ARRAYSIZE(text));
	if (!text[0])
		return false;

	ID3D12CommandQueue *queue = DX12AcquireCommandQueue();
	if (!queue)
		return false;

	AcquireSRWLockExclusive(&gOverlayLock);
	bool ready = OverlayMatchesLocked(swapChain, queue);
	if (!ready)
		ready = InitializeOverlayLocked(swapChain, queue);
	queue->Release();

	if (!ready) {
		ResetOverlayResourcesLocked();
		ReleaseSRWLockExclusive(&gOverlayLock);
		return false;
	}

	UINT frameIndex = gOverlay.swapChain->GetCurrentBackBufferIndex();
	if (frameIndex >= gOverlay.bufferCount ||
		!gOverlay.wrappedBuffers[frameIndex] || !gOverlay.d2dTargets[frameIndex]) {
		ResetOverlayResourcesLocked();
		ReleaseSRWLockExclusive(&gOverlayLock);
		return false;
	}

	ID3D11Resource *resource = gOverlay.wrappedBuffers[frameIndex];
	gOverlay.d3d11On12->AcquireWrappedResources(&resource, 1);
	gOverlay.d2dContext->SetTarget(gOverlay.d2dTargets[frameIndex]);
	gOverlay.d2dContext->BeginDraw();

	IDWriteTextLayout *layout = nullptr;
	const UINT lineCount = CountTextLines(text);
	const float maxWidth = 1800.0f;
	const float maxHeight = (std::max)(80.0f, lineCount * 24.0f + 24.0f);
	HRESULT layoutHr = gOverlay.dwriteFactory->CreateTextLayout(
		text,
		static_cast<UINT32>(wcslen(text)),
		gOverlay.textFormat,
		maxWidth,
		maxHeight,
		&layout);

	DWRITE_TEXT_METRICS metrics = {};
	float boxWidth = 900.0f;
	float boxHeight = maxHeight;
	if (SUCCEEDED(layoutHr) && layout) {
		if (SUCCEEDED(layout->GetMetrics(&metrics))) {
			boxWidth = metrics.widthIncludingTrailingWhitespace + 18.0f;
			boxHeight = metrics.height + 14.0f;
		}
	}

	boxWidth = (std::max)(220.0f, (std::min)(boxWidth, maxWidth));
	boxHeight = (std::max)(34.0f, (std::min)(boxHeight, maxHeight));

	// 顶部居中：根据 swapchain 实际宽度计算水平偏移
	const float screenW = static_cast<float>(gOverlay.width > 0 ? gOverlay.width : 1920);
	const float boxX = (std::max)(4.0f, (screenW - boxWidth) * 0.5f);
	D2D1_RECT_F background = D2D1::RectF(boxX, 6.0f, boxX + boxWidth, 6.0f + boxHeight);
	gOverlay.d2dContext->FillRectangle(background, gOverlay.backgroundBrush);

	const COLORREF color = DX12GetOverlayStatusColor();
	gOverlay.textBrush->SetColor(D2DColorFromColorRef(color));
	const float textX = boxX + 6.0f;
	if (layout) {
		gOverlay.d2dContext->DrawTextLayout(
			D2D1::Point2F(textX + 1.0f, 12.0f), layout, gOverlay.shadowBrush,
			D2D1_DRAW_TEXT_OPTIONS_CLIP);
		gOverlay.d2dContext->DrawTextLayout(
			D2D1::Point2F(textX, 11.0f), layout, gOverlay.textBrush,
			D2D1_DRAW_TEXT_OPTIONS_CLIP);
	} else {
		D2D1_RECT_F textRect = D2D1::RectF(textX, 11.0f, textX + maxWidth, 11.0f + maxHeight);
		gOverlay.d2dContext->DrawTextW(
			text, static_cast<UINT32>(wcslen(text)), gOverlay.textFormat,
			textRect, gOverlay.textBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}

	HRESULT drawHr = gOverlay.d2dContext->EndDraw();
	if (layout)
		layout->Release();
	gOverlay.d3d11On12->ReleaseWrappedResources(&resource, 1);
	gOverlay.d3d11Context->Flush();

	if (drawHr == D2DERR_RECREATE_TARGET || FAILED(drawHr)) {
		ResetOverlayResourcesLocked();
		ReleaseSRWLockExclusive(&gOverlayLock);
		return false;
	}

	ReleaseSRWLockExclusive(&gOverlayLock);
	return true;
}

void DX12DrawSwapChainText(IDXGISwapChain *swapChain)
{
	DX12RenderOverlayToSwapChain(swapChain);
}

void DX12CloseOverlayWindow()
{
	AcquireSRWLockExclusive(&gOverlayLock);
	ResetOverlayResourcesLocked();
	DX12SetOverlayWindow(nullptr);
	ReleaseSRWLockExclusive(&gOverlayLock);
}
