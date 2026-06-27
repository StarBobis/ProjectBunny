#include "DX12ImGuiOverlay.h"

#include <dxgi1_4.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "DX12Profiling.h"
#include "DX12ShaderDump.h"
#include "DX12ShaderHunt.h"
#include "DX12State.h"
#include "DX12UiActions.h"
#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

static constexpr UINT kFrameCount = 3;
static constexpr UINT kSrvDescriptorCount = 16;

template <typename T>
static void SafeRelease(T *&value)
{
	if (value) {
		value->Release();
		value = nullptr;
	}
}

struct FrameContext
{
	ID3D12CommandAllocator *allocator = nullptr;
	UINT64 fenceValue = 0;
};

struct DescriptorAllocator
{
	ID3D12DescriptorHeap *heap = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = {};
	D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = {};
	UINT increment = 0;
	std::vector<UINT> freeList;

	void Init(ID3D12Device *device, ID3D12DescriptorHeap *descriptorHeap)
	{
		heap = descriptorHeap;
		cpuStart = heap->GetCPUDescriptorHandleForHeapStart();
		gpuStart = heap->GetGPUDescriptorHandleForHeapStart();
		increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		freeList.clear();
		for (UINT i = kSrvDescriptorCount; i > 0; --i)
			freeList.push_back(i - 1);
	}

	void Reset()
	{
		heap = nullptr;
		freeList.clear();
	}

	void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE *cpu, D3D12_GPU_DESCRIPTOR_HANDLE *gpu)
	{
		if (!cpu || !gpu || freeList.empty()) {
			if (cpu)
				cpu->ptr = 0;
			if (gpu)
				gpu->ptr = 0;
			return;
		}
		const UINT index = freeList.back();
		freeList.pop_back();
		cpu->ptr = cpuStart.ptr + index * increment;
		gpu->ptr = gpuStart.ptr + index * increment;
	}

	void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
	{
		if (!increment || cpu.ptr < cpuStart.ptr || gpu.ptr < gpuStart.ptr)
			return;
		const UINT cpuIndex = static_cast<UINT>((cpu.ptr - cpuStart.ptr) / increment);
		const UINT gpuIndex = static_cast<UINT>((gpu.ptr - gpuStart.ptr) / increment);
		if (cpuIndex == gpuIndex && cpuIndex < kSrvDescriptorCount)
			freeList.push_back(cpuIndex);
	}
};

static SRWLOCK gUiLock = SRWLOCK_INIT;
static bool gInitialized = false;
static HWND gWindow = nullptr;
static WNDPROC gOriginalWndProc = nullptr;
static IDXGISwapChain *gSwapChain = nullptr;
static ID3D12Device *gDevice = nullptr;
static ID3D12CommandQueue *gCommandQueue = nullptr;
static ID3D12GraphicsCommandList *gCommandList = nullptr;
static ID3D12DescriptorHeap *gRtvHeap = nullptr;
static ID3D12DescriptorHeap *gSrvHeap = nullptr;
static ID3D12Fence *gFence = nullptr;
static HANDLE gFenceEvent = nullptr;
static UINT64 gFenceLastValue = 0;
static UINT gRtvDescriptorSize = 0;
static UINT gBufferCount = 0;
static DXGI_FORMAT gRtvFormat = DXGI_FORMAT_UNKNOWN;
static FrameContext gFrames[kFrameCount] = {};
static std::vector<ID3D12Resource*> gBackBuffers;
static std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> gRtvHandles;
static DescriptorAllocator gSrvAllocator;

static LRESULT CALLBACK DX12ImGuiWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
		return 1;
	return gOriginalWndProc ? CallWindowProcW(gOriginalWndProc, hwnd, msg, wparam, lparam) :
		DefWindowProcW(hwnd, msg, wparam, lparam);
}

static std::string WideToUtf8(const wchar_t *text)
{
	if (!text || !text[0])
		return std::string();
	const int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (needed <= 1)
		return std::string();
	std::string result(static_cast<size_t>(needed - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], needed, nullptr, nullptr);
	return result;
}

static void FreeBackBuffers()
{
	for (ID3D12Resource *resource : gBackBuffers)
		if (resource)
			resource->Release();
	gBackBuffers.clear();
	gRtvHandles.clear();
}

static void RestoreWndProc()
{
	if (gWindow && gOriginalWndProc) {
		SetWindowLongPtrW(gWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gOriginalWndProc));
		gOriginalWndProc = nullptr;
	}
	gWindow = nullptr;
}

static void CleanupLocked()
{
	RestoreWndProc();
	if (gInitialized) {
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}
	gInitialized = false;
	FreeBackBuffers();
	SafeRelease(gCommandList);
	for (FrameContext &frame : gFrames) {
		SafeRelease(frame.allocator);
		frame.fenceValue = 0;
	}
	SafeRelease(gRtvHeap);
	gSrvAllocator.Reset();
	SafeRelease(gSrvHeap);
	SafeRelease(gFence);
	if (gFenceEvent) {
		CloseHandle(gFenceEvent);
		gFenceEvent = nullptr;
	}
	SafeRelease(gCommandQueue);
	SafeRelease(gDevice);
	SafeRelease(gSwapChain);
	gBufferCount = 0;
	gRtvFormat = DXGI_FORMAT_UNKNOWN;
	gFenceLastValue = 0;
}

static bool CreateRenderTargetsLocked()
{
	FreeBackBuffers();
	if (!gSwapChain || !gDevice || !gBufferCount || !gRtvHeap)
		return false;

	gBackBuffers.resize(gBufferCount, nullptr);
	gRtvHandles.resize(gBufferCount);
	D3D12_CPU_DESCRIPTOR_HANDLE handle = gRtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < gBufferCount; ++i) {
		if (FAILED(gSwapChain->GetBuffer(i, IID_PPV_ARGS(&gBackBuffers[i]))))
			return false;
		gRtvHandles[i] = handle;
		gDevice->CreateRenderTargetView(gBackBuffers[i], nullptr, handle);
		handle.ptr += gRtvDescriptorSize;
	}
	return true;
}

static bool InitForSwapChainLocked(IDXGISwapChain *swapChain)
{
	if (gInitialized && gSwapChain == swapChain)
		return true;
	CleanupLocked();
	if (!swapChain)
		return false;

	DXGI_SWAP_CHAIN_DESC desc = {};
	if (FAILED(swapChain->GetDesc(&desc)) || !desc.OutputWindow || !IsWindow(desc.OutputWindow))
		return false;
	ID3D12Device *device = nullptr;
	if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device))) || !device)
		return false;
	ID3D12CommandQueue *queue = DX12AcquireCommandQueue();
	if (!queue) {
		device->Release();
		return false;
	}

	gSwapChain = swapChain;
	gSwapChain->AddRef();
	gDevice = device;
	gCommandQueue = queue;
	gWindow = desc.OutputWindow;
	gBufferCount = desc.BufferCount ? desc.BufferCount : 2;
	gRtvFormat = desc.BufferDesc.Format == DXGI_FORMAT_UNKNOWN ?
		DXGI_FORMAT_R8G8B8A8_UNORM : desc.BufferDesc.Format;

	for (FrameContext &frame : gFrames) {
		if (FAILED(gDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator))))
			return false;
	}
	if (FAILED(gDevice->CreateCommandList(
		    0, D3D12_COMMAND_LIST_TYPE_DIRECT, gFrames[0].allocator, nullptr,
		    IID_PPV_ARGS(&gCommandList))))
		return false;
	gCommandList->Close();

	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
	rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDesc.NumDescriptors = gBufferCount;
	if (FAILED(gDevice->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&gRtvHeap))))
		return false;
	gRtvDescriptorSize = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
	srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvDesc.NumDescriptors = kSrvDescriptorCount;
	srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (FAILED(gDevice->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&gSrvHeap))))
		return false;
	gSrvAllocator.Init(gDevice, gSrvHeap);

	if (FAILED(gDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gFence))))
		return false;
	gFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!gFenceEvent)
		return false;
	if (!CreateRenderTargetsLocked())
		return false;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.IniFilename = nullptr;
	ImGui::StyleColorsDark();
	ImGuiStyle &style = ImGui::GetStyle();
	style.WindowRounding = 4.0f;
	style.FrameRounding = 3.0f;
	style.Colors[ImGuiCol_WindowBg].w = 0.86f;

	if (!ImGui_ImplWin32_Init(gWindow))
		return false;
	ImGui_ImplDX12_InitInfo initInfo = {};
	initInfo.Device = gDevice;
	initInfo.CommandQueue = gCommandQueue;
	initInfo.NumFramesInFlight = kFrameCount;
	initInfo.RTVFormat = gRtvFormat;
	initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
	initInfo.SrvDescriptorHeap = gSrvHeap;
	initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE *cpu, D3D12_GPU_DESCRIPTOR_HANDLE *gpu) {
		gSrvAllocator.Alloc(cpu, gpu);
	};
	initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
		gSrvAllocator.Free(cpu, gpu);
	};
	if (!ImGui_ImplDX12_Init(&initInfo))
		return false;

	gOriginalWndProc = reinterpret_cast<WNDPROC>(
		SetWindowLongPtrW(gWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DX12ImGuiWndProc)));
	gInitialized = true;
	DX12LogJsonFunc("DX12ImGuiOverlay", "\"event\":\"initialized\",\"swapchain\":\"%p\",\"hwnd\":\"%p\",\"buffers\":%u",
		swapChain, gWindow, gBufferCount);
	return true;
}

static void DrawControlPanel()
{
	ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(640.0f, 0.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("3DMigoto DX12");

	wchar_t status[1024] = {};
	DX12GetOverlayStatus(status, ARRAYSIZE(status));
	const std::string statusUtf8 = WideToUtf8(status);
	const ImVec4 statusColor = DX12HuntIsEnabled() ?
		ImVec4(0.2f, 1.0f, 0.3f, 1.0f) : ImVec4(0.8f, 0.9f, 1.0f, 1.0f);
	ImGui::TextColored(statusColor, "%s", statusUtf8.empty() ? "3DMigoto DX12 hook alive" : statusUtf8.c_str());

	ImGui::Separator();
	if (ImGui::Button(DX12HuntIsEnabled() ? "Disable Hunting" : "Enable Hunting"))
		DX12UiRequestHuntToggle();
	ImGui::SameLine();
	if (ImGui::Button("Reset Selection"))
		DX12UiRequestHuntResetSelection();
	ImGui::SameLine();
	if (ImGui::Button("Copy Selected Hash"))
		DX12UiRequestHuntCopySelectedHash();

	ImGui::Separator();
	ImGui::TextUnformatted("Shader");
	if (ImGui::Button("VS Previous")) DX12UiRequestHuntPreviousVS();
	ImGui::SameLine();
	if (ImGui::Button("VS Next")) DX12UiRequestHuntNextVS();
	if (ImGui::Button("PS Previous")) DX12UiRequestHuntPreviousPS();
	ImGui::SameLine();
	if (ImGui::Button("PS Next")) DX12UiRequestHuntNextPS();
	if (ImGui::Button("CS Previous")) DX12UiRequestHuntPreviousCS();
	ImGui::SameLine();
	if (ImGui::Button("CS Next")) DX12UiRequestHuntNextCS();

	ImGui::Separator();
	ImGui::TextUnformatted("Input Assembler");
	if (ImGui::Button("IB Previous")) DX12UiRequestHuntPreviousIB();
	ImGui::SameLine();
	if (ImGui::Button("IB Next")) DX12UiRequestHuntNextIB();
	if (ImGui::Button("VB Previous")) DX12UiRequestHuntPreviousVB();
	ImGui::SameLine();
	if (ImGui::Button("VB Next")) DX12UiRequestHuntNextVB();

	ImGui::Separator();
	if (ImGui::Button("Frame Analysis"))
		DX12UiRequestFrameAnalysis();
	ImGui::SameLine();
	if (ImGui::Button("Shader Dump"))
		DX12UiRequestShaderDump();
	ImGui::SameLine();
	if (ImGui::Button("Reload Mods"))
		DX12UiRequestModReload();
	ImGui::SameLine();
	if (ImGui::Button(DX12Profiling::IsActive() ? "Disable Profiling" : "Enable Profiling"))
		DX12UiRequestProfilingToggle();

	if (DX12ShaderDumpIsBusy())
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Shader dump is busy");

	ImGui::End();
}

static bool RenderLocked(IDXGISwapChain *swapChain)
{
	if (!InitForSwapChainLocked(swapChain))
		return false;
	IDXGISwapChain3 *swapChain3 = nullptr;
	UINT bufferIndex = 0;
	if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3)))) {
		bufferIndex = swapChain3->GetCurrentBackBufferIndex();
		swapChain3->Release();
	}
	if (bufferIndex >= gBackBuffers.size())
		bufferIndex = DX12GetPresentCount() % gBufferCount;

	FrameContext &frame = gFrames[DX12GetPresentCount() % kFrameCount];
	if (frame.fenceValue && gFence && gFence->GetCompletedValue() < frame.fenceValue)
		return false;

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	DrawControlPanel();
	ImGui::Render();

	DX12InternalReplayScope replayScope;
	if (FAILED(frame.allocator->Reset()))
		return false;
	if (FAILED(gCommandList->Reset(frame.allocator, nullptr)))
		return false;

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = gBackBuffers[bufferIndex];
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	gCommandList->ResourceBarrier(1, &barrier);
	gCommandList->OMSetRenderTargets(1, &gRtvHandles[bufferIndex], FALSE, nullptr);
	ID3D12DescriptorHeap *heaps[] = { gSrvHeap };
	gCommandList->SetDescriptorHeaps(1, heaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gCommandList);
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	gCommandList->ResourceBarrier(1, &barrier);
	if (FAILED(gCommandList->Close()))
		return false;

	ID3D12CommandList *lists[] = { gCommandList };
	gCommandQueue->ExecuteCommandLists(1, lists);
	if (SUCCEEDED(gCommandQueue->Signal(gFence, ++gFenceLastValue)))
		frame.fenceValue = gFenceLastValue;
	return true;
}

bool DX12ImGuiOverlayRender(IDXGISwapChain *swapChain)
{
	AcquireSRWLockExclusive(&gUiLock);
	const bool ok = RenderLocked(swapChain);
	ReleaseSRWLockExclusive(&gUiLock);
	return ok;
}

void DX12ImGuiOverlayInvalidate(IDXGISwapChain *swapChain)
{
	AcquireSRWLockExclusive(&gUiLock);
	if (!swapChain || swapChain == gSwapChain)
		FreeBackBuffers();
	ReleaseSRWLockExclusive(&gUiLock);
}

void DX12ImGuiOverlayShutdown()
{
	AcquireSRWLockExclusive(&gUiLock);
	CleanupLocked();
	ReleaseSRWLockExclusive(&gUiLock);
}
