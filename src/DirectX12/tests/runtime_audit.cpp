// Native WARP work with the production readback, shadow and overlay implementations.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include "ModRuntime/DX12ModRuntimeInternal.h"
#include "Presentation/DX12SwapChainQueues.h"
#include <wrl/client.h>
#include <d3d12sdklayers.h>
#include <thread>
#include <chrono>
#include <stdexcept>

namespace
{
using Microsoft::WRL::ComPtr;

void Require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void Check(HRESULT hr)
{
	if (FAILED(hr)) {
		fprintf(stderr, "HRESULT 0x%08lx\n", hr);
		throw std::runtime_error("D3D12 call failed");
	}
}

ComPtr<ID3D12CommandQueue> Queue(ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type)
{
	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Type = type;
	ComPtr<ID3D12CommandQueue> queue;
	Check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)));
	return queue;
}

void Drain(ID3D12Device *device, ID3D12CommandQueue *queue)
{
	ComPtr<ID3D12Fence> fence;
	Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
	Check(queue->Signal(fence.Get(), 1));
	HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	Require(event != nullptr, "completion event");
	Check(fence->SetEventOnCompletion(1, event));
	DWORD result = WaitForSingleObject(event, 5000);
	CloseHandle(event);
	Require(result == WAIT_OBJECT_0 && fence->GetCompletedValue() == 1, "GPU completion");
}

struct List
{
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList> list;

	List(ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type)
	{
		Check(device->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator)));
		Check(device->CreateCommandList(0, type, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
	}
	~List() { DX12ReadbackForgetRecording(list.Get()); }

	void Submit(ID3D12CommandQueue *queue)
	{
		ID3D12CommandList *batch[] = {list.Get()};
		DX12ReadbackExecute(queue, 1, batch, [](ID3D12CommandQueue *q, UINT n, ID3D12CommandList *const *lists) {
			q->ExecuteCommandLists(n, lists);
		});
	}
};

ComPtr<ID3D12Resource> Buffer(ID3D12Device *device, D3D12_HEAP_TYPE type, UINT64 width = 256)
{
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = type;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = width;
	desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ComPtr<ID3D12Resource> resource;
	const auto state = type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ :
		D3D12_RESOURCE_STATE_COPY_DEST;
	Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
		IID_PPV_ARGS(&resource)));
	return resource;
}

void Write(ID3D12Resource *resource, UINT value)
{
	void *ptr = nullptr;
	D3D12_RANGE noRead = {};
	Check(resource->Map(0, &noRead, &ptr));
	memcpy(ptr, &value, sizeof(value));
	D3D12_RANGE written = {0, sizeof(value)};
	resource->Unmap(0, &written);
}

bool HasValue(ID3D12Resource *target, UINT value)
{
	const uint8_t *bytes = nullptr;
	size_t size = 0;
	return DX12TryGetBufferContent(target->GetGPUVirtualAddress(), sizeof(value), &bytes, &size) &&
		size >= sizeof(value) && !memcmp(bytes, &value, sizeof(value));
}

void TestReadback(ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type)
{
	auto queue = Queue(device, type);
	auto upload = Buffer(device, D3D12_HEAP_TYPE_UPLOAD);
	auto target = Buffer(device, D3D12_HEAP_TYPE_DEFAULT, sizeof(UINT));
	auto desc = target->GetDesc();
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	RecordResource(target.Get(), &desc, &heap, nullptr, D3D12_RESOURCE_STATE_COPY_DEST);

	List a(device, type), b(device, type);
	Write(upload.Get(), 17);
	a.list->CopyBufferRegion(target.Get(), 0, upload.Get(), 0, 4);
	DX12ReadbackRecordBuffer(a.list.Get(), target.Get(), 0, upload.Get(), 0, 4);
	Check(a.list->Close());
	Check(b.list->Close());
	b.Submit(queue.Get());
	Require(!DX12ReadbackAwaitingPublication(), "unrelated list consumed a capture");
	a.Submit(queue.Get());
	Drain(device, queue.Get());
	DX12ReadbackPublishReady();
	Require(HasValue(target.Get(), 17), "buffer snapshot value");

	Write(upload.Get(), 23);
	a.Submit(queue.Get());
	Require(!HasValue(target.Get(), 17), "old cached value visible during a queued write");
	Drain(device, queue.Get());
	Write(upload.Get(), 42);
	a.Submit(queue.Get());
	Drain(device, queue.Get());
	// Reset may occur before the CPU worker reads the old recording.
	DX12ReadbackForgetRecording(a.list.Get());
	Check(a.allocator->Reset());
	Check(a.list->Reset(a.allocator.Get(), nullptr));
	Write(upload.Get(), 99);
	a.list->CopyBufferRegion(target.Get(), 0, upload.Get(), 0, 4);
	DX12ReadbackRecordBuffer(a.list.Get(), target.Get(), 0, upload.Get(), 0, 4);
	Check(a.list->Close());
	DX12ReadbackPublishReady();
	Require(HasValue(target.Get(), 42), "repeat/reset aliased the previous readback");
	DX12ReadbackForgetRecording(a.list.Get());
	b.Submit(queue.Get());
	Require(!DX12ReadbackAwaitingPublication(), "discarded recording published");
	Drain(device, queue.Get());
	DX12InvalidateBufferContent(target.Get());
}

void TestQueueHandoff(ID3D12Device *device)
{
	auto copy = Queue(device, D3D12_COMMAND_LIST_TYPE_COPY);
	auto direct = Queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto first = Buffer(device, D3D12_HEAP_TYPE_UPLOAD);
	auto next = Buffer(device, D3D12_HEAP_TYPE_UPLOAD);
	auto target = Buffer(device, D3D12_HEAP_TYPE_DEFAULT, 4);
	auto desc = target->GetDesc();
	RecordResource(target.Get(), &desc, nullptr, nullptr, D3D12_RESOURCE_STATE_COPY_DEST);
	Write(first.Get(), 11);
	Write(next.Get(), 22);
	List a(device, D3D12_COMMAND_LIST_TYPE_COPY), b(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto record = [&](List &commands, ID3D12Resource *source) {
		commands.list->CopyBufferRegion(target.Get(), 0, source, 0, 4);
		DX12ReadbackRecordBuffer(commands.list.Get(), target.Get(), 0, source, 0, 4);
		Check(commands.list->Close());
	};
	record(b, next.Get());
	record(a, first.Get());
	ComPtr<ID3D12Fence> order, gate;
	Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&order)));
	Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
	a.Submit(copy.Get());
	Check(copy->Signal(order.Get(), 1));
	Check(direct->Wait(order.Get(), 1));
	Check(direct->Wait(gate.Get(), 1));
	b.Submit(direct.Get());
	Drain(device, copy.Get());
	DX12ReadbackPublishReady();
	const bool hidden = !HasValue(target.Get(), 11);
	Check(gate->Signal(1));
	Drain(device, direct.Get());
	DX12ReadbackPublishReady();
	Require(hidden && HasValue(target.Get(), 22), "cross-queue overwrite exposed stale data");
	DX12InvalidateBufferContent(target.Get());
}

void TestTexture(ID3D12Device *device, bool partial, bool enhanced = false)
{
	auto queue = Queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	List producer(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto upload = Buffer(device, D3D12_HEAP_TYPE_UPLOAD, 1024);
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = 8;
	desc.Height = 2;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
	ComPtr<ID3D12Resource> target;
	if (enhanced) {
		D3D12_FEATURE_DATA_D3D12_OPTIONS12 options = {};
		if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options, sizeof(options))) ||
			!options.EnhancedBarriersSupported) {
			puts("Enhanced-barrier execution unavailable on this WARP adapter");
			return;
		}
		ComPtr<ID3D12Device10> device10;
		Check(device->QueryInterface(IID_PPV_ARGS(&device10)));
		D3D12_RESOURCE_DESC1 desc1 = {};
		desc1.Dimension = desc.Dimension;
		desc1.Width = desc.Width;
		desc1.Height = desc.Height;
		desc1.MipLevels = desc1.DepthOrArraySize = desc1.SampleDesc.Count = 1;
		desc1.Format = desc.Format;
		Check(device10->CreateCommittedResource3(&heap, D3D12_HEAP_FLAG_NONE, &desc1,
			D3D12_BARRIER_LAYOUT_COPY_DEST, nullptr, nullptr, 0, nullptr, IID_PPV_ARGS(&target)));
	} else {
		Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&target)));
	}
	D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.pResource = target.Get();
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.pResource = upload.Get();
	device->GetCopyableFootprints(&desc, 0, 1, 0, &src.PlacedFootprint, nullptr, nullptr, nullptr);
	const UINT pitch = src.PlacedFootprint.Footprint.RowPitch;
	std::vector<BYTE> data(1024, 0xcc);
	for (UINT row = 0; row < desc.Height; ++row)
		for (UINT i = 0; i < desc.Width * 4; ++i)
			data[row * pitch + i] = static_cast<BYTE>(i + row);
	void *mapped = nullptr;
	Check(upload->Map(0, nullptr, &mapped));
	memcpy(mapped, data.data(), data.size());
	upload->Unmap(0, nullptr);
	producer.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	DX12ReadbackRecordTextureCopy(producer.list.Get(), target.Get(), 0, 0, 0, &src, nullptr);
	if (partial) {
		// Replace the second half of each row with the first half. Hashing the
		// partial source or including row padding gives a different result.
		const D3D12_BOX half = {0, 0, 0, 4, 2, 1};
		producer.list->CopyTextureRegion(&dst, 4, 0, 0, &src, &half);
		DX12ReadbackRecordTextureCopy(producer.list.Get(), target.Get(), 4, 0, 0, &src, &half);
		for (UINT row = 0; row < desc.Height; ++row)
			memcpy(data.data() + row * pitch + 16, data.data() + row * pitch, 16);
		if (enhanced) {
			ComPtr<ID3D12GraphicsCommandList7> list7;
			Check(producer.list.As(&list7));
			D3D12_TEXTURE_BARRIER barriers[2] = {};
			auto &first = barriers[0];
			first.pResource = target.Get();
			first.SyncBefore = first.SyncAfter = D3D12_BARRIER_SYNC_ALL;
			first.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
			first.AccessAfter = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
			first.LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_DEST;
			first.LayoutAfter = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
			barriers[1] = first;
			std::swap(barriers[1].LayoutBefore, barriers[1].LayoutAfter);
			std::swap(barriers[1].AccessBefore, barriers[1].AccessAfter);
			D3D12_BARRIER_GROUP group = {};
			group.Type = D3D12_BARRIER_TYPE_TEXTURE;
			group.NumBarriers = 2;
			group.pTextureBarriers = barriers;
			DX12ReadbackForwardEnhancedBarriers(producer.list.Get(), list7.Get(), 1, &group);
		} else {
			D3D12_RESOURCE_BARRIER barriers[2] = {};
			for (auto &barrier : barriers) {
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource = target.Get();
			}
			barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			DX12ReadbackForwardBarriers(producer.list.Get(), 2, barriers, nullptr);
		}
	}
	Check(producer.list->Close());
	uint32_t expected = 0;
	Require(DX12TextureHashUtils::Texture2DTopMipHashFromRows(desc, data.data(), pitch, 32, 2,
		&expected), "expected texture hash");
	uint32_t actual = 0;
	Require(!DX12TryGetTextureHash(target.Get(), &actual), "unsubmitted texture was published");
	DX12ResourceCollectQueue::Start();
	producer.Submit(queue.Get());
	Drain(device, queue.Get());
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (std::chrono::steady_clock::now() < deadline &&
		(!DX12TryGetTextureHash(target.Get(), &actual) || expected != actual))
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	DX12ResourceCollectQueue::Stop();
	Require(actual == expected, "worker published incorrect texture hash");
	DX12InvalidateTextureHash(target.Get());
}

void TestRenderPass(ID3D12Device *device)
{
	List native(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	ComPtr<ID3D12GraphicsCommandList> wrapper = native.list;
	void *pointer = wrapper.Detach();
	Require(DX12Wrapper::WrapD3D12DeviceResult(__uuidof(ID3D12GraphicsCommandList), &pointer),
		"render-pass wrapper");
	wrapper.Attach(static_cast<ID3D12GraphicsCommandList*>(pointer));
	ComPtr<ID3D12GraphicsCommandList4> pass;
	Check(wrapper.As(&pass));
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.Width = desc.Height = 4;
	desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
	ComPtr<ID3D12Resource> texture;
	Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)));
	DX12MarkTextureHashWanted(texture.Get());
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	pass->BeginRenderPass(0, nullptr, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
	pass->ResourceBarrier(1, &barrier);
	pass->EndRenderPass();
	Check(pass->Close());
	auto queue = Queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	native.Submit(queue.Get());
	Drain(device, queue.Get());
	Require(!DX12ReadbackAwaitingPublication(), "injected a copy inside a render pass");
	DX12ReadbackForgetWantedTexture(texture.Get(), true);
}

void TestWrapperState(ID3D12Device *device)
{
	List native(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	ComPtr<ID3D12GraphicsCommandList> wrapped = native.list;
	void *pointer = wrapped.Detach();
	Require(DX12Wrapper::WrapD3D12DeviceResult(__uuidof(ID3D12GraphicsCommandList), &pointer),
		"command list wrapper");
	wrapped.Attach(static_cast<ID3D12GraphicsCommandList*>(pointer));
	auto *state = DX12CommandListRuntime::GetStatePtrFast(native.list.Get());
	Require(state != nullptr, "registered wrapper state");
	DX12CommandListRuntime::RememberPrimitiveTopology(state, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	wrapped->ClearState(nullptr);
	Require(state->ia.primitiveTopology == D3D_PRIMITIVE_TOPOLOGY_UNDEFINED, "stale IA after ClearState");
	gHasPresentRuntimeEffect = 1;
	const auto flags = DX12HotPathState::SourceState::Capture();
	gPresentCommandListExecutedPresent = DX12RuntimeState::GetPresentCount() - 1;
	const auto original = [](ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT) {};
	DX12WrapperRuntimeUtils::DrawInstanced(native.list.Get(), state, 3, 1, 0, 0,
		original, flags.BuildHookWorkFlags());
	Require(!DX12ModPresentReplacementPendingForCurrentPresent(), "Present VM missed the first draw");
	gHasPresentRuntimeEffect = 0;
	Check(wrapped->Close());
	Require(flags.NeedsRecordWork() && !flags.NeedsModDrawWork() &&
		(flags.BuildHookWorkFlags() & DX12HotPathState::FLAG_PRESENT_REPLACEMENT),
		"Present-only configuration skipped");
}

void TestUploadFailure(ID3D12Device *device)
{
	using Upload = DX12DdsTextureLoadUtils;
	auto texture = Buffer(device, D3D12_HEAP_TYPE_DEFAULT);
	auto staging = Buffer(device, D3D12_HEAP_TYPE_UPLOAD);
	Upload::UploadChannelSlot slot;
	Require(Upload::FinishUploadLocked(slot, texture.Get(), staging.Get(), 1, S_OK),
		"successful upload publication");
	Require(!Upload::FinishUploadLocked(slot, texture.Get(), staging.Get(), 2, E_FAIL) &&
		Upload::s_uploadPendingFenceValue == 1 && slot.busyUntilFence == 2,
		"failed signal advertised or released its slot");
	texture.Reset();
	staging.Reset();
	Require(slot.unfencedTexture && slot.unfencedUpload &&
		slot.unfencedTexture->GetDesc().Width == 256, "unfenced operands released");
	slot.ReleaseUnfencedResources();
	Upload::s_uploadHasPending.store(false);
}

void TestBarrierGroups(ID3D12Device *device)
{
	auto raw = Buffer(device, D3D12_HEAP_TYPE_DEFAULT);
	ComPtr<ID3D12Resource> wrapped = raw;
	void *result = wrapped.Detach();
	Require(DX12Wrapper::WrapD3D12ResourceResult(__uuidof(ID3D12Resource), &result), "wrapped resource");
	wrapped.Attach(static_cast<ID3D12Resource*>(result));
	D3D12_BUFFER_BARRIER barriers[2] = {};
	barriers[0].pResource = barriers[1].pResource = raw.Get();
	D3D12_BARRIER_GROUP groups[2] = {};
	groups[0].Type = D3D12_BARRIER_TYPE_GLOBAL;
	D3D12_GLOBAL_BARRIER global = {};
	groups[0].NumBarriers = 1;
	groups[0].pGlobalBarriers = &global;
	groups[1].Type = D3D12_BARRIER_TYPE_BUFFER;
	groups[1].NumBarriers = 2;
	groups[1].pBufferBarriers = barriers;
	std::vector<D3D12_BARRIER_GROUP> storage;
	std::vector<std::vector<D3D12_TEXTURE_BARRIER>> textures;
	std::vector<std::vector<D3D12_BUFFER_BARRIER>> buffers;
	const D3D12_BARRIER_GROUP *effective = nullptr;
	DX12WrapperRuntimeUtils::UnwrapBarrierGroups(groups, 2, &storage, &textures, &buffers, &effective);
	Require(effective == groups && !storage.capacity() && !textures.capacity() && !buffers.capacity(),
		"allocated for native/global barriers");
	barriers[1].pResource = wrapped.Get();
	DX12WrapperRuntimeUtils::UnwrapBarrierGroups(groups, 2, &storage, &textures, &buffers, &effective);
	Require(effective != groups && effective[1].pBufferBarriers[1].pResource == raw.Get() &&
		effective[0].pGlobalBarriers == &global && barriers[1].pResource == wrapped.Get(),
		"mixed barrier unwrapping");
}

void TestSwapChain(IDXGIFactory4 *factory, ID3D12Device *device)
{
	auto first = Queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto second = Queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = desc.Height = 64;
	desc.BufferCount = 2;
	desc.SampleDesc.Count = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	ComPtr<IDXGIFactory4> wrappedFactory = factory;
	void *factoryResult = wrappedFactory.Detach();
	Require(DX12Wrapper::WrapDXGIFactoryResult(__uuidof(IDXGIFactory4), &factoryResult), "DXGI wrapper");
	wrappedFactory.Attach(static_cast<IDXGIFactory4*>(factoryResult));
	ComPtr<ID3D12CommandQueue> wrapped = first;
	void *queueResult = wrapped.Detach();
	Require(DX12Wrapper::WrapD3D12DeviceResult(__uuidof(ID3D12CommandQueue), &queueResult), "queue wrapper");
	wrapped.Attach(static_cast<ID3D12CommandQueue*>(queueResult));
	ComPtr<IDXGISwapChain1> chain;
	Check(wrappedFactory->CreateSwapChainForComposition(wrapped.Get(), &desc, nullptr, &chain));
	ComPtr<ID3D12CommandQueue> held;
	held.Attach(DX12SwapChainQueues::Acquire(chain.Get()));
	Require(held.Get() == first.Get(), "creation queue");
	ComPtr<IDXGISwapChain3> chain3;
	Check(chain.As(&chain3));
	UINT masks[] = {1, 1};
	IUnknown *queues[] = {second.Get(), second.Get()};
	Check(chain3->ResizeBuffers1(2, 64, 64, desc.Format, 0, masks, queues));
	ComPtr<ID3D12CommandQueue> current;
	current.Attach(DX12SwapChainQueues::Acquire(chain.Get()));
	Require(current.Get() == second.Get() && held.Get() == first.Get(), "back-buffer queue ownership");
}

void TestOverlay(ID3D12Device *device)
{
	auto queue = Queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto otherQueue = Queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto *channel = Bunny::DX12PresentOverlayChannel::ForDevice(device);
	Require(channel != nullptr, "overlay channel");
	ComPtr<ID3D12Fence> gate;
	Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
	Check(queue->Wait(gate.Get(), 1));
	bool filled = true;
	for (int i = 0; i < 2; ++i)
		filled = filled && channel->BeginRecord(true) && channel->Submit(queue.Get());
	const auto begin = std::chrono::steady_clock::now();
	const bool skipped = channel->BeginRecord(false) == nullptr;
	const bool grew = channel->BeginRecord(true) && channel->Submit(otherQueue.Get());
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	Check(gate->Signal(1));
	Drain(device, otherQueue.Get());
	Require(filled && skipped && grew && elapsed < std::chrono::milliseconds(500),
		"HUD skip and mod recording during GPU backlog");
	Bunny::DX12PresentOverlayChannel::ClearAll();
}

void TestDescriptors(ID3D12Device *device, bool benchmark)
{
	constexpr auto type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = 256;
	desc.Type = type;
	ComPtr<ID3D12DescriptorHeap> source, dest;
	Check(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&source)));
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	Check(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&dest)));
	for (auto *heap : {source.Get(), dest.Get()}) {
		void *ptr = heap;
		const auto heapDesc = heap->GetDesc();
		DX12ResourceTrackerHookRuntime::RecordCreatedDescriptorHeap(S_OK, device, &heapDesc, &ptr);
	}
	const UINT stride = device->GetDescriptorHandleIncrementSize(type);
	D3D12_CPU_DESCRIPTOR_HANDLE src[8], dst[8];
	for (UINT i = 0; i < 8; ++i) {
		src[i].ptr = source->GetCPUDescriptorHandleForHeapStart().ptr + i * 13 * stride;
		dst[i].ptr = dest->GetCPUDescriptorHandleForHeapStart().ptr + (7 - i) * stride;
		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format = DXGI_FORMAT_R32_UINT;
		srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Buffer.NumElements = i + 1;
		device->CreateShaderResourceView(nullptr, &srv, src[i]);
		DX12ResourceTrackerHookRuntime::RecordCreatedShaderResourceView(device, nullptr, &srv, src[i]);
	}
	D3D12_CPU_DESCRIPTOR_HANDLE reversed[8];
	std::reverse_copy(src, src + 8, reversed);
	for (bool changing : {false, true}) {
		const int calls = benchmark ? 50000 : 2;
		for (int pass = 0; pass < (benchmark ? 5 : 1); ++pass) {
			const auto begin = std::chrono::steady_clock::now();
			for (int i = 0; i < calls; ++i) {
				const auto *sources = changing && !(i % 2) ? reversed : src;
				DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(
					device, 8, dst, nullptr, 8, sources, nullptr, type);
			}
			if (benchmark) {
				auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin);
				printf("descriptor %s ns/call %.1f\n", changing ? "changed" : "unchanged", ns.count() / calls);
			}
		}
	}
	for (UINT i = 0; i < 8; ++i) {
		DescriptorRecord record;
		Require(DX12DescriptorHeapShadowUtils::FindLatestDescriptor(dst[i].ptr, &record) &&
			record.srv.Buffer.NumElements == i + 1, "scattered descriptor value");
	}
}
}

int main(int argc, char **argv)
{
	try {
		const bool benchmark = argc == 2 && !strcmp(argv[1], "--descriptors");
		ComPtr<ID3D12Debug> debug;
		if (!benchmark && SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
			debug->EnableDebugLayer();
		ComPtr<IDXGIFactory4> factory;
		Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
		ComPtr<IDXGIAdapter> warp;
		Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
		ComPtr<ID3D12Device> device;
		Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
		TestDescriptors(device.Get(), benchmark);
		if (benchmark)
			return 0;
		TestReadback(device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
		TestReadback(device.Get(), D3D12_COMMAND_LIST_TYPE_COPY);
		TestQueueHandoff(device.Get());
		TestTexture(device.Get(), false);
		TestTexture(device.Get(), true);
		TestTexture(device.Get(), true, true);
		TestUploadFailure(device.Get());
		TestBarrierGroups(device.Get());
		TestWrapperState(device.Get());
		TestRenderPass(device.Get());
		TestSwapChain(factory.Get(), device.Get());
		TestOverlay(device.Get());
		if (debug) {
			ComPtr<ID3D12InfoQueue> info;
			Check(device.As(&info));
			for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
				SIZE_T size = 0;
				Check(info->GetMessage(i, nullptr, &size));
				std::vector<BYTE> storage(size);
				auto *msg = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
				Check(info->GetMessage(i, msg, &size));
				Require(msg->Severity > D3D12_MESSAGE_SEVERITY_ERROR, msg->pDescription);
			}
			puts("D3D12 debug layer checked");
		}
		Check(device->GetDeviceRemovedReason());
		puts("PASS DX12 WARP runtime audit");
		return 0;
	} catch (const std::exception &error) {
		fprintf(stderr, "FAIL %s\n", error.what());
		return 1;
	}
}
