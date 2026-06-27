#include "DX12DeviceHooks.h"

#include <d3d12.h>

#include <vector>

#include "DX12CommandListHooks.h"
#include "DX12HookManager.h"
#include "DX12ModRuntime.h"
#include "DX12ResourceTracker.h"
#include "DX12ShaderDump.h"
#include "DX12State.h"

typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_GRAPHICS_PIPELINE_STATE)(
	ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_COMPUTE_PIPELINE_STATE)(
	ID3D12Device*, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_PIPELINE_STATE)(
	ID3D12Device2*, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_DEVICE_FACTORY_CREATE_DEVICE)(
	ID3D12DeviceFactory*, IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

static PFN_CREATE_GRAPHICS_PIPELINE_STATE gOrigCreateGraphicsPipelineState = nullptr;
static PFN_CREATE_COMPUTE_PIPELINE_STATE gOrigCreateComputePipelineState = nullptr;
static PFN_CREATE_PIPELINE_STATE gOrigCreatePipelineState = nullptr;
static PFN_DEVICE_FACTORY_CREATE_DEVICE gOrigDeviceFactoryCreateDevice = nullptr;

static void LogDeviceHookCall(const char *api, const void *object)
{
	if (!DX12ShouldLogHookCall(api))
		return;
	DX12LogDebugJsonFunc("DX12HookCall",
		"\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\"",
		api ? api : "", DX12GetPresentCount(), object);
}

static void LogDeviceOriginalFallback(const char *api, const void *object, UINT slot, const void *fallback)
{
	if (!DX12ShouldLogHookCall(api))
		return;
	DX12LogDebugJsonFunc("DX12FallbackPath",
		"\"kind\":\"original_lookup\",\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\","
		"\"slot\":%u,\"fallback\":\"%p\"",
		api ? api : "", DX12GetPresentCount(), object, slot, fallback);
}

struct DX12GraphicsPsoReplacement
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	D3D12_SHADER_BYTECODE vs = {};
	D3D12_SHADER_BYTECODE ps = {};
	std::vector<unsigned char> vsBytes;
	std::vector<unsigned char> psBytes;
	bool changed = false;
};

struct DX12ComputePsoReplacement
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
	D3D12_SHADER_BYTECODE cs = {};
	std::vector<unsigned char> csBytes;
	bool changed = false;
};

template <typename T>
static T GetDeviceOriginal(void *object, UINT slot, T fallback, const char *name)
{
	if (object) {
		void **vtable = *reinterpret_cast<void***>(object);
		if (vtable) {
			void *original = DX12GetOriginalFunction(vtable[slot]);
			if (original)
				return reinterpret_cast<T>(original);
		}
	}
	if (fallback) {
		LogDeviceOriginalFallback(name, object, slot, reinterpret_cast<const void*>(fallback));
		return fallback;
	}
	DX12LogJsonFunc(name ? name : "ID3D12Device::Unknown",
		"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
		object, slot);
	return nullptr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateGraphicsPipelineState(
	ID3D12Device *device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
	REFIID riid, void **pipelineState)
{
	LogDeviceHookCall("ID3D12Device::CreateGraphicsPipelineState", device);
	if (DX12IsInternalReplay())
		return DX12CreateGraphicsPipelineStateOriginal(device, desc, riid, pipelineState);
	DX12GraphicsPsoReplacement replacement;
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC *activeDesc = desc;
	if (desc) {
		replacement.desc = *desc;
		if (DX12ModReplaceShaderBytecode("vs", desc->VS, &replacement.vs, &replacement.vsBytes)) {
			replacement.desc.VS = replacement.vs;
			replacement.changed = true;
		}
		if (DX12ModReplaceShaderBytecode("ps", desc->PS, &replacement.ps, &replacement.psBytes)) {
			replacement.desc.PS = replacement.ps;
			replacement.changed = true;
		}
		if (replacement.changed)
			activeDesc = &replacement.desc;
	}

	HRESULT hr = DX12CreateGraphicsPipelineStateOriginal(device, activeDesc, riid, pipelineState);
	if (SUCCEEDED(hr) && activeDesc) {
		ID3D12PipelineState *created =
			pipelineState ? static_cast<ID3D12PipelineState*>(*pipelineState) : nullptr;
		if (desc)
			DX12ModRecordGraphicsPipelineState(device, created, desc);
		DX12RecordGraphicsPipelineState(
			created, activeDesc);
	}
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateComputePipelineState(
	ID3D12Device *device, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
	REFIID riid, void **pipelineState)
{
	LogDeviceHookCall("ID3D12Device::CreateComputePipelineState", device);
	if (DX12IsInternalReplay())
		return DX12CreateComputePipelineStateOriginal(device, desc, riid, pipelineState);
	DX12ComputePsoReplacement replacement;
	const D3D12_COMPUTE_PIPELINE_STATE_DESC *activeDesc = desc;
	if (desc) {
		replacement.desc = *desc;
		if (DX12ModReplaceShaderBytecode("cs", desc->CS, &replacement.cs, &replacement.csBytes)) {
			replacement.desc.CS = replacement.cs;
			replacement.changed = true;
			activeDesc = &replacement.desc;
		}
	}

	HRESULT hr = DX12CreateComputePipelineStateOriginal(device, activeDesc, riid, pipelineState);
	if (SUCCEEDED(hr) && activeDesc) {
		ID3D12PipelineState *created =
			pipelineState ? static_cast<ID3D12PipelineState*>(*pipelineState) : nullptr;
		if (desc)
			DX12ModRecordComputePipelineState(device, created, desc);
		DX12RecordComputePipelineState(
			created, activeDesc);
	}
	return hr;
}

HRESULT DX12CreateGraphicsPipelineStateOriginal(
	ID3D12Device *device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
	REFIID riid, void **pipelineState)
{
	auto original = GetDeviceOriginal(
		device, 10, gOrigCreateGraphicsPipelineState, "ID3D12Device::CreateGraphicsPipelineState");
	return original ? original(device, desc, riid, pipelineState) : E_FAIL;
}

HRESULT DX12CreateComputePipelineStateOriginal(
	ID3D12Device *device, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
	REFIID riid, void **pipelineState)
{
	auto original = GetDeviceOriginal(
		device, 11, gOrigCreateComputePipelineState, "ID3D12Device::CreateComputePipelineState");
	return original ? original(device, desc, riid, pipelineState) : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE HookedCreatePipelineState(
	ID3D12Device2 *device, const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
	REFIID riid, void **pipelineState)
{
	LogDeviceHookCall("ID3D12Device2::CreatePipelineState", device);
	auto original = GetDeviceOriginal(
		device, 47, gOrigCreatePipelineState, "ID3D12Device2::CreatePipelineState");
	if (DX12IsInternalReplay())
		return original ? original(device, desc, riid, pipelineState) : E_FAIL;
	HRESULT hr = original ? original(device, desc, riid, pipelineState) : E_FAIL;
	if (SUCCEEDED(hr) && desc)
		DX12RecordPipelineStateStream(
			pipelineState ? static_cast<ID3D12PipelineState*>(*pipelineState) : nullptr, desc);
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedDeviceFactoryCreateDevice(
	ID3D12DeviceFactory *factory, IUnknown *adapter, D3D_FEATURE_LEVEL featureLevel,
	REFIID riid, void **device)
{
	LogDeviceHookCall("ID3D12DeviceFactory::CreateDevice", factory);
	auto original = GetDeviceOriginal(
		factory, 9, gOrigDeviceFactoryCreateDevice, "ID3D12DeviceFactory::CreateDevice");
	HRESULT hr = original ? original(factory, adapter, featureLevel, riid, device) : E_FAIL;
	DX12LogJsonFunc("ID3D12DeviceFactory::CreateDevice",
		"\"adapter\":\"%p\",\"featureLevel\":\"0x%x\",\"hr\":\"0x%lx\",\"device\":\"%p\"",
		adapter, featureLevel, hr, device ? *device : nullptr);
	if (SUCCEEDED(hr) && device && *device)
		DX12HookDevice(static_cast<IUnknown*>(*device));
	return hr;
}

void DX12HookDevice(IUnknown *device)
{
	if (!device)
		return;

	ID3D12Device *baseDevice = nullptr;
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(&baseDevice))))
		return;

	// IUnknown(0-2) + ID3D12Object(3-6) + ID3D12Device methods:
	// GetNodeCount(7), CreateCommandQueue(8), CreateCommandAllocator(9).
	constexpr UINT CreateGraphicsPipelineStateIndex = 10;
	constexpr UINT CreateComputePipelineStateIndex = 11;
	DX12VTableHook deviceHooks[] = {
		{CreateGraphicsPipelineStateIndex, reinterpret_cast<void**>(&gOrigCreateGraphicsPipelineState),
			HookedCreateGraphicsPipelineState, "ID3D12Device::CreateGraphicsPipelineState"},
		{CreateComputePipelineStateIndex, reinterpret_cast<void**>(&gOrigCreateComputePipelineState),
			HookedCreateComputePipelineState, "ID3D12Device::CreateComputePipelineState"},
	};
	DX12InstallVTableHooks(baseDevice, deviceHooks);
	DX12HookResourceMetadata(baseDevice);
	DX12HookCommandListCreation(baseDevice);
	DX12LogJsonFunc("DeviceHooksReady", "\"device\":\"%p\"", baseDevice);

	baseDevice->Release();

	ID3D12Device2 *device2 = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device2)))) {
		// IUnknown + ID3D12Object + ID3D12Device(37 methods) + ID3D12Device1(3 methods).
		constexpr UINT CreatePipelineStateIndex = 47;
		DX12VTableHook device2Hooks[] = {
			{CreatePipelineStateIndex, reinterpret_cast<void**>(&gOrigCreatePipelineState),
				HookedCreatePipelineState, "ID3D12Device2::CreatePipelineState"},
		};
		DX12InstallVTableHooks(device2, device2Hooks);
		device2->Release();
	}
	// Command-list hooks only log/track metadata and forward calls unchanged.
}

void DX12HookDeviceFactory(IUnknown *factory)
{
	if (!factory)
		return;

	ID3D12DeviceFactory *deviceFactory = nullptr;
	if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&deviceFactory))))
		return;

	constexpr UINT CreateDeviceIndex = 9;
	DX12VTableHook factoryHooks[] = {
		{CreateDeviceIndex, reinterpret_cast<void**>(&gOrigDeviceFactoryCreateDevice),
			HookedDeviceFactoryCreateDevice, "ID3D12DeviceFactory::CreateDevice"},
	};
	DX12InstallVTableHooks(deviceFactory, factoryHooks);
	deviceFactory->Release();
}

void DX12HookDeviceFromCommandQueue(IUnknown *commandQueue)
{
	if (!commandQueue)
		return;

	ID3D12CommandQueue *queue = nullptr;
	if (FAILED(commandQueue->QueryInterface(IID_PPV_ARGS(&queue))))
		return;

	DX12SetCommandQueue(queue);

	ID3D12Device *device = nullptr;
	HRESULT hr = queue->GetDevice(IID_PPV_ARGS(&device));
	DX12LogJsonFunc("ID3D12CommandQueue::GetDevice",
		"\"hr\":\"0x%lx\",\"device\":\"%p\"", hr, device);
	DX12HookCommandQueue(queue);
	if (SUCCEEDED(hr) && device) {
		DX12HookDevice(device);
		device->Release();
	}
	queue->Release();
}
