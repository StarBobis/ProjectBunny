// Native WARP work with the production readback, shadow and overlay implementations.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include "ModRuntime/DX12ModRuntimeInternal.h"
#include "Presentation/DX12SwapChainQueues.h"
#include "tests/IniUnaryMathCases.h"
#include "tests/IniBitwiseCases.h"
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

void TestShaderRegexExclusions()
{
	Bunny::IniDocument ini;
	Require(ini.Parse(
		L"[ShaderRegexExcept]\nshader_model=ps_5_0\nexclude_hash=0XAbCd\n"
		L"exclude_hash=abcd\nexclude_hash=ffffffffffffffff\nhandling=skip\n"
		L"[ShaderRegexOther]\nshader_model=ps_5_0\n"
		L"[ShaderRegexInvalid]\nexclude_hash=1234567890abcdef0\nshader_model=ps_5_0\n"),
		"parse shader regex groups");
	std::vector<std::wstring> warnings;
	Bunny::MigotoShaderRegexParser::ParseSections(ini, &gShaderRegexes, warnings);
	Require(warnings.size() == 1 && gShaderRegexes.size() == 2, "discard invalid group only");
	const auto &excluded = gShaderRegexes.at(L"ShaderRegexExcept").excludedHashes;
	Require(excluded.size() == 2 && excluded.contains(UINT64_MAX), "full width and duplicate exclusions");

	std::unordered_set<std::wstring> seen;
	std::vector<Bunny::ShaderOverrideConfig> matches;
	AppendShaderRegexForShaderLocked(0xabcd, "ps_5_0", &matches, &seen);
	Require(matches.size() == 1 && matches.front().section == L"ShaderRegexOther",
		"exclusion is local to one regex group");
	AppendShaderRegexForShaderLocked(0xfedc, "ps_5_0", &matches, &seen);
	Require(matches.size() == 2 && matches.back().handlingSkip,
		"excluded shader cannot suppress a later eligible shader in the same PSO");

	Require(ini.Parse(L"[ShaderRegexExcept]\nshader_model=ps_5_0\n"), "reload regex");
	Bunny::MigotoShaderRegexParser::ParseSections(ini, &gShaderRegexes, warnings);
	seen.clear();
	matches.clear();
	AppendShaderRegexForShaderLocked(0xabcd, "ps_5_0", &matches, &seen);
	Require(matches.size() == 1, "reload drops obsolete exclusions");
	gShaderRegexes.clear();
}

void TestExpressionNumbers()
{
	Bunny::IniDocument ini;
	Require(ini.Parse(L"[Constants]\nglobal $mask = 0b101 + 0B11 * 2\n"
		L"global $power = 2**3**2\nglobal $neg = -2**2 + 4**-1\n"
		L"global $signed = 0 !== -0\nglobal $equal = $mask === 11\n"
		L"global $shift = 1 << 2\nglobal $flagged = ($mask ^ $shift) & 15\n"),
		"parse numeric expressions");
	Bunny::CommandListMap lists;
	Bunny::MigotoCommandListParser::ParseSections(ini, &lists);
	Bunny::DX12ConstantsVmUtils::Initialize(lists);
	MigotoCommandVmUtils::State vm;
	Require(vm.Evaluate(L"$mask") == 11 && vm.Evaluate(L"$power") == 512 &&
		vm.Evaluate(L"$neg") == 4.25, "binary literals and DX11 exponent precedence");
	Require(vm.Evaluate(L"$signed") == 1 && vm.Evaluate(L"$equal") == 1 &&
		vm.Evaluate(L"1 === 1.000000001") == 1, "bit comparison uses float32");
	Require(vm.Evaluate(L"$flagged") == 15, "bitwise operators execute through Constants with variable operands");
	for (const auto &test : migoto::test::BitwiseCases) {
		const std::wstring expression(test.expression);
		MigotoCommandVmUtils::ExpressionParser parser(expression, vm.Context());
		const auto actual = parser.Parse();
		if (!parser.ok || actual != test.expected)
			std::fprintf(stderr, "DX12 bitwise %ls produced %.9g, expected %.9g\n", test.expression, actual, test.expected);
		Require(parser.ok && actual == test.expected, "bitwise precedence, chaining, rounding and boundaries");
	}
	Require(vm.Evaluate(L"0 == -0") == 1 && vm.Evaluate(L"1 == 1.0000001") == 1,
		"ordinary DX12 equality retains its tolerance");
	const std::wstring maximum = L"0b" + std::wstring(64, L'1');
	Require(vm.Evaluate(maximum) == static_cast<double>(UINT64_MAX), "maximum binary literal");
	for (const std::wstring &invalid : {maximum + L"1", std::wstring(L"0b"),
		std::wstring(L"0b102"), std::wstring(L"2**"), std::wstring(L"1 & & 2"),
		std::wstring(L"1 ||| 2"), std::wstring(L"1 <<"), std::wstring(L"~")}) {
		MigotoCommandVmUtils::ExpressionParser parser(invalid, vm.Context());
		Require(parser.Parse() == 0 && !parser.ok, "malformed expression rejected");
	}
	MigotoCommandVmUtils::ClearGlobals();
}

void Check(HRESULT hr)
{
	if (FAILED(hr)) {
		fprintf(stderr, "HRESULT 0x%08lx\n", hr);
		throw std::runtime_error("D3D12 call failed");
	}
}

void TestUnaryMath()
{
	MigotoCommandVmUtils::State vm;
	for (const auto &test : migoto::test::UnaryMathCases) {
		const double result = vm.Evaluate(test.expression);
		Require(std::isfinite(result) &&
			std::abs(result - test.expected) <= 1e-5 * (1 + std::abs(test.expected)),
			"shared DX11 unary math case");
	}
	Require(std::isnan(vm.Evaluate(L"sin(inf)")) && std::isnan(vm.Evaluate(L"sqrt(-1)")) &&
		std::isinf(vm.Evaluate(L"rsqrt(0)")), "IEEE math domain and pole results");
	vm.PushLocalScope();
	vm.DeclareLocal(L"local $input", L"-3");
	Require(vm.Evaluate(L"ABS($input) + SQRT(4)") == 5, "case insensitive names and VM variables");
	for (std::wstring invalid : {L"sqrt()", L"sin(1, 2)", L"unknown(2)", L"abs(-2"}) {
		MigotoCommandVmUtils::ExpressionParser parser(invalid, vm.Context());
		Require(parser.Parse() == 0 && !parser.ok, "invalid unary call rejected");
	}
}

void TestFrameTiming()
{
	MigotoCommandVmUtils::State vm;
	Require(vm.Evaluate(L"time + frame_time + frame_number + fps") == 0,
		"timing operands start at zero before the first Present");
	const uint64_t start = migoto::GetSystemTicks();
	DX12RuntimeState::BeginPresent(start);
	const double firstTime = vm.Evaluate(L"time");
	Require(firstTime >= 0 && vm.Evaluate(L"frame_time") == firstTime && vm.Evaluate(L"fps") == 0,
		"first Present measures time since launch and initializes the FPS counter");
	DX12RuntimeState::IncrementPresentCount(start);
	DX12RuntimeState::BeginPresent(start + 10000);
	Require(vm.Evaluate(L"frame_number") == 1 && vm.Evaluate(L"frame_time") == 0.01f &&
		std::abs(vm.Evaluate(L"fps") - 100) < 0.001, "second Present publishes delta in seconds and FPS");
	const std::wstring operand = L"frame_number";
	MigotoCommandVmUtils::ExpressionParser ongoing(operand, vm.Context());
	Require(ongoing.LookupIdentifier(L"FRAME_TIME") == 0.01f, "capture this expression's timing sample");
	DX12RuntimeState::IncrementPresentCount(start + 10000);
	Require(ongoing.Parse() == 1 && vm.Evaluate(operand) == 2, "a running expression sees one complete frame");
	DX12RuntimeState::BeginPresent(start + 30000);
	const double smoothedFps = vm.Evaluate(L"fps");
	Require(vm.Evaluate(L"frame_time") == 0.02f && std::abs(smoothedFps - 1 / 0.011) < 0.001 &&
		std::abs(vm.Evaluate(L"time") - firstTime - 0.03) < 1e-5, "elapsed time and DX11 EMA smoothing");
	DX12RuntimeState::BeginPresent(start + 30000);
	Require(vm.Evaluate(L"frame_time") == 0 && vm.Evaluate(L"fps") == smoothedFps, "duplicate tick preserves FPS");
	DX12RuntimeState::BeginPresent(start + 2030000);
	Require(vm.Evaluate(L"frame_time") == 2 && vm.Evaluate(L"fps") == smoothedFps, "long frame preserves FPS");

	Bunny::IniDocument ini;
	Require(ini.Parse(L"[Constants]\nglobal $observed = frame_number + frame_time\n"), "parse frame operands");
	Bunny::CommandListMap lists;
	Bunny::MigotoCommandListParser::ParseSections(ini, &lists);
	Bunny::DX12ConstantsVmUtils::Initialize(lists);
	Require(vm.Evaluate(L"$observed") == 4 && vm.Evaluate(L"fps") == smoothedFps,
		"reloading Constants preserves the frame sample");
	DX12RuntimeState::BeginPresent(0);
	Require(vm.Evaluate(L"time + frame_time") == 0 && vm.Evaluate(L"fps") == smoothedFps,
		"backwards timestamp clamps time at zero");
	MigotoCommandVmUtils::ClearGlobals();
}

void TestRandomExpression()
{
	Bunny::IniDocument ini;
	Require(ini.Parse(L"[Constants]\nglobal $bound = 8\nglobal $sample = RaNdOm(-$bound)\n"
		L"[CommandListBadDraw]\ndrawinstanced = 3,random(2),0,1+\n"), "parse random configuration");
	const uint32_t before = GetExpressionRandomSampleCount();
	Bunny::CommandListMap lists;
	Bunny::MigotoCommandListParser::ParseSections(ini, &lists);
	Require(lists.at(L"CommandListBadDraw").actions.empty() && GetExpressionRandomSampleCount() == before,
		"invalid draw arguments do not sample random during load or playback");
	Bunny::DX12ConstantsVmUtils::Initialize(lists);
	MigotoCommandVmUtils::State vm;
	const double sample = vm.Evaluate(L"$sample");
	Require(GetExpressionRandomSampleCount() == before + 1 && sample <= 0 && sample > -8,
		"Constants evaluates a variable random bound exactly once");
	Require(vm.Evaluate(L"random(0)") == 0 && std::isnan(vm.Evaluate(L"random(inf)")) &&
		std::isnan(vm.Evaluate(L"random(nan)")) && GetExpressionRandomSampleCount() == before + 1,
		"zero and nonfinite bounds do not consume samples");
	for (const std::wstring invalid : {L"random()", L"random(2,3)", L"random(2) +",
		L"random(2) + typo(3)", L"random(random(2)", L"random + 1"}) {
		MigotoCommandVmUtils::ExpressionParser parser(invalid, vm.Context());
		Require(parser.Parse() == 0 && !parser.ok, "malformed random expression rejected");
	}
	Require(GetExpressionRandomSampleCount() == before + 1, "invalid expressions leave the sample stream untouched");

	// Replay the next two native samples at the same generator inputs through the VM.
	const auto seedNext = []() {
		SeedExpressionRandom(73u - 0x9e3779b9u * GetExpressionRandomSampleCount(), 41);
	};
	seedNext();
	const float inner = RandomExpressionValue(8.0f);
	const float expected = RandomExpressionValue(inner);
	Require(inner > 0, "nested random fixture has a nonzero bound");
	seedNext();
	Require(vm.Evaluate(L"random(random($bound))") == expected && GetExpressionRandomSampleCount() == before + 5,
		"nested VM calls consume the shared samples in argument order");
	Require(vm.Evaluate(L"1 || random(1)") == 1 && GetExpressionRandomSampleCount() == before + 6,
		"observable random on the right of a logical operator runs once");
	const uint32_t frame = static_cast<uint32_t>(DX12RuntimeState::GetPresentCount()) + 1u;
	const uint64_t ticks = 0x12345678abcdef01ull;
	// Offset the reference frame by one call so the following sample has
	// the same hash input after the real frame boundary advances the seed.
	SeedExpressionRandom(frame + 0x9e3779b9u, static_cast<uint32_t>(ticks));
	const float nextFrame = RandomExpressionValue(16);
	Require(static_cast<uint32_t>(DX12RuntimeState::IncrementPresentCount(ticks)) == frame &&
		vm.Evaluate(L"random(16)") == nextFrame && GetExpressionRandomSampleCount() == before + 8,
		"frame and low clock bits seed the next sample without resetting the ordinal");
	MigotoCommandVmUtils::ClearGlobals();
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
	const LONG frameBeforePresent = DX12RuntimeState::GetPresentCount();
	const auto samplesBeforePresent = GetExpressionRandomSampleCount();
	Check(chain->Present(0, 0));
	Require(DX12RuntimeState::GetPresentCount() == frameBeforePresent + 1 &&
		GetExpressionRandomSampleCount() == samplesBeforePresent, "wrapped WARP Present advances the frame without sampling");
	MigotoCommandVmUtils::State vm;
	Require(vm.Evaluate(L"frame_number") == frameBeforePresent + 1, "Present updates the INI frame number");
	const double presentTime = vm.Evaluate(L"time");
	DXGI_PRESENT_PARAMETERS parameters{};
	Check(chain->Present1(0, DXGI_PRESENT_TEST, &parameters));
	Require(vm.Evaluate(L"frame_number") == frameBeforePresent + 1 && vm.Evaluate(L"time") == presentTime,
		"test presentation leaves the frame snapshot untouched");
	Check(chain->Present1(0, 0, &parameters));
	Require(vm.Evaluate(L"frame_number") == frameBeforePresent + 2 && vm.Evaluate(L"time") >= presentTime,
		"wrapped Present1 publishes its clock and frame number");
	Drain(device, first.Get());
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

// Capture the native draw arguments at the hook's callback boundary.
std::vector<std::array<int64_t, 6>> recordedDraws;

DX12IaReplacementExecutorCallbacks RecordingDrawCallbacks()
{
	DX12IaReplacementExecutorCallbacks callbacks;
	callbacks.drawInstanced = [](ID3D12GraphicsCommandList*, UINT count, UINT instances, UINT start, UINT first) {
		recordedDraws.push_back({0, count, instances, start, 0, first});
	};
	callbacks.drawIndexedInstanced = [](ID3D12GraphicsCommandList*, UINT count, UINT instances,
		UINT start, INT base, UINT first) {
		recordedDraws.push_back({1, count, instances, start, base, first});
	};
	return callbacks;
}

void TestInstancedDraws(ID3D12Device *device)
{
	Bunny::IniDocument ini;
	Require(ini.Parse(
		L"[CommandListLiteral]\ndrawinstanced=9,2,3,4\ndrawindexedinstanced=8,5,2,-3,7\n"
		L"draw=4,1\ndrawindexedinstanced=0,0,0,-2147483648,4294967295\n"
		L"[CommandListDynamic]\nlocal $start = 2\n"
		L"drawindexedinstanced=6, $instances+1, $start, -$start, 11\n"
		L"drawinstanced=4,nan,0,0\ndrawinstanced=4,4294967296.0,0,0\n"
		L"drawinstanced=4,1+,0,0\n"
		L"[CommandListInvalid]\ndrawinstanced=2,1,0\ndrawindexedinstanced=1,2,3,4,5,6\n"
		L"drawinstanced=2,-1,0,0\ndrawindexedinstanced=1,1,0,2147483648,0\n"
		L"[CustomShaderCopies]\nvs=copy-vs.hlsl\nps=copy-ps.hlsl\ndrawinstanced=0,$instances,3,9\n"
		L"[CustomShaderIndexed]\nvs=copy-vs.hlsl\nps=copy-ps.hlsl\ndrawindexedinstanced=4,1,0,0,0\n"
		L"[CustomShaderDefault]\nvs=copy-vs.hlsl\nps=copy-ps.hlsl\n"),
		"instanced draw config");
	Bunny::CommandListMap lists;
	Bunny::MigotoCommandListParser::ParseSections(ini, &lists);
	Require(lists.at(L"CommandListInvalid").actions.empty(), "reject incorrect arity and literal bounds");
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> plans;
	BuildDx12CompiledCommandListPlans(lists, &plans);
	Require(!plans.at(L"CommandListLiteral").unsupported && plans.at(L"CommandListDynamic").unsupported,
		"literal draw plans compile, expression draws require execution");

	List native(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	const DX12IaHashState ia{};
	DX12ModIaReplacement replacement;
	DX12CommandListExecutor executor(device, native.list.Get(), ia, 0, 0, 13, 0, 0, 21, &replacement);
	executor.SetExecutionMode(true, true);
	executor.ExecuteCompiledCommandListPlan(plans.at(L"CommandListLiteral"), 0, false);
	for (int instances : {2, 6}) {
		MigotoCommandVmUtils::SetGlobal(L"$instances", instances);
		executor.RunActions(lists.at(L"CommandListDynamic").actions, 0, false, nullptr);
	}
	Require(executor.observedDynamicCommandState, "expression draws bypass cross-draw caching");
	const auto callbacks = RecordingDrawCallbacks();
	DX12IaDrawInvocation original;
	original.instanceCount = 13;
	original.firstInstance = 21;
	recordedDraws.clear();
	const DX12ActiveIaState activeIa{};
	DX12IaReplacementDrawExecutor(native.list.Get(), callbacks).ExecuteReplacementDraws(replacement, activeIa, &original);
	const std::vector<std::array<int64_t, 6>> expected{
		{0,9,2,3,0,4}, {1,8,5,2,-3,7}, {0,4,13,1,0,21}, {1,0,0,0,INT32_MIN,UINT32_MAX},
		{1,6,3,2,-2,11}, {1,6,7,2,-2,11}};
	Require(recordedDraws == expected, "explicit instance parameters, dynamic values and inherited legacy draw");
	auto elements = std::make_shared<DX12CustomShaderElementMap>();
	Bunny::DX12CustomShaderElementParser::BuildRegistry(lists, elements.get());
	const auto previous = gCustomShaderElementSnapshot.exchange(elements);
	for (const auto *section : {L"CustomShaderCopies", L"CustomShaderIndexed", L"CustomShaderDefault"})
		executor.RunCommandList(lists.at(section), 0, false, nullptr);
	gCustomShaderElementSnapshot.store(previous);
	const auto &records = replacement.customElementRecords;
	Require(records.size() == 2, "standalone indexed draw without an index binding skips recording");
	Require(records[0].drawCount == 0 && records[0].drawStart == 3 &&
		records[0].drawInstanceCount == 6 && records[0].drawFirstInstance == 9,
		"standalone element preserves explicit zero count and evaluated instance range");
	Require(records[1].drawCount == 4 && records[1].drawInstanceCount == 1 &&
		records[1].drawFirstInstance == 0, "standalone element default quad");
	Check(native.list->Close());
	MigotoCommandVmUtils::ClearGlobals();
}

void TestAutomaticDraws(ID3D12Device *device)
{
	Bunny::IniDocument ini;
	Require(ini.Parse(L"[Present]\nrun=CommandListAuto\n[CommandListAuto]\n"
		L"draw=auto\ndrawindexed=auto\ndrawindexedinstanced=auto\n"), "automatic draw config");
	Bunny::CommandListMap lists;
	Bunny::MigotoCommandListParser::ParseSections(ini, &lists);
	const bool needsIa = AutomaticDrawsNeedIaState(lists, {}, {}, {});
	Require(needsIa, "Present-only mod needs resident IA through a nested list");
	lists.erase(L"Present");
	Require(!AutomaticDrawsNeedIaState(lists, {}, {}, {}), "unused automatic draws leave IA tracking idle");
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> plans;
	BuildDx12CompiledCommandListPlans(lists, &plans);
	const auto &plan = plans.at(L"CommandListAuto");
	Require(!plan.unsupported, "automatic draws compile without freezing their counts");

	const LONG previousIa = InterlockedExchange(&gHasIaResidentStateRequirements, needsIa);
	const LONG previousPresent = InterlockedExchange(&gHasPresentRuntimeEffect, 1);
	DX12HotPathState::Update();
	List native(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	ComPtr<ID3D12GraphicsCommandList> wrapped = native.list;
	void *pointer = wrapped.Detach();
	Require(DX12Wrapper::WrapD3D12DeviceResult(__uuidof(ID3D12GraphicsCommandList), &pointer), "auto draw wrapper");
	wrapped.Attach(static_cast<ID3D12GraphicsCommandList*>(pointer));
	auto *state = DX12CommandListRuntime::GetStatePtrFast(native.list.Get());
	Require(state != nullptr, "auto draw wrapper state");
	auto buffer = Buffer(device, D3D12_HEAP_TYPE_UPLOAD);
	const auto address = buffer->GetGPUVirtualAddress();
	D3D12_VERTEX_BUFFER_VIEW vb{address + 16, 35, 8};
	D3D12_INDEX_BUFFER_VIEW ib{address + 32, 18, DXGI_FORMAT_R16_UINT};
	wrapped->IASetVertexBuffers(0, 1, &vb);
	wrapped->IASetIndexBuffer(&ib);

	const DX12IaHashState hashes{};
	DX12ModIaReplacement replacement;
	DX12CommandListExecutor vm(device, native.list.Get(), hashes, 0, 0, 0, 0, 0, 0, &replacement);
	vm.SetExecutionMode(true, true);
	vm.ExecuteCompiledCommandListPlan(plan, 0, false);
	auto callbacks = RecordingDrawCallbacks();
	callbacks.setIndexBuffer = [](ID3D12GraphicsCommandList *list, const D3D12_INDEX_BUFFER_VIEW *view) {
		list->IASetIndexBuffer(view);
	};
	callbacks.setVertexBuffers = [](ID3D12GraphicsCommandList *list, UINT start, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views) {
		list->IASetVertexBuffers(start, count, views);
	};
	const DX12IaReplacementDrawExecutor draws(native.list.Get(), callbacks);
	const auto caller = DX12IaDrawInvocation::ForDrawInstanced(7, 3, 0, 5);
	const auto execute = [&](const DX12IaDrawInvocation *original) {
		recordedDraws.clear();
		draws.ExecuteReplacementDraws(replacement, state->ia, original);
	};
	using Draws = std::vector<std::array<int64_t, 6>>;
	execute(&caller);
	Require(recordedDraws == Draws{{0,4,1,0,0,0}, {1,9,1,0,0,0}, {1,9,3,0,0,5}},
		"effective subviews supply truncated counts, only instanced auto inherits the caller");
	execute(nullptr);
	Require(recordedDraws == Draws{{0,4,1,0,0,0}, {1,9,1,0,0,0}}, "no calling draw for instanced auto");

	replacement.vertexBuffers = {{}, {address, 40, 4}};
	replacement.hasIndexBuffer = true;
	replacement.indexBuffer = {address, 20, DXGI_FORMAT_R32_UINT};
	DX12IaReplacementBindingExecutor binding(native.list.Get(), callbacks);
	const DX12ActiveIaState originalIa = state->ia;
	binding.Apply(replacement);
	execute(&caller);
	Require(recordedDraws == Draws{{0,4,1,0,0,0}, {1,5,1,0,0,0}, {1,5,3,0,0,5}},
		"sparse VB replacement preserves slot zero and IB replacement changes count");
	binding.Restore(replacement, originalIa);
	replacement.vertexBuffers.clear();
	replacement.hasIndexBuffer = false;
	vb.StrideInBytes = 0;
	wrapped->IASetVertexBuffers(0, 1, &vb);
	wrapped->IASetIndexBuffer(nullptr);
	execute(&caller);
	Require(recordedDraws.empty(), "zero stride and unbound index buffer skip automatic draws");
	replacement.hasIndexBuffer = true;
	replacement.indexBuffer = {address, 20, DXGI_FORMAT_UNKNOWN};
	replacement.vertexBuffers = {{address, 3, 4}};
	execute(&caller);
	Require(recordedDraws.empty(), "invalid index format and incomplete vertex skip automatic draws");
	replacement.indexBuffer = {};
	replacement.vertexBuffers = {{address, 24, 4}};
	execute(&caller);
	Require(recordedDraws == Draws{{0,6,1,0,0,0}}, "new slot-zero replacement restores automatic vertex count");

	Check(wrapped->Close());
	InterlockedExchange(&gHasPresentRuntimeEffect, previousPresent);
	InterlockedExchange(&gHasIaResidentStateRequirements, previousIa);
	DX12HotPathState::Update();
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
		TestShaderRegexExclusions();
		TestExpressionNumbers();
		TestUnaryMath();
		TestFrameTiming();
		TestRandomExpression();
		TestInstancedDraws(device.Get());
		TestAutomaticDraws(device.Get());
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
