#include "McpAgent.h"

#include "HackerDevice.h"
#include "HackerContext.h"
#include "HackerInputLayout.h"
#include "CommandList.h"
#include "ResourceHash.h"
#include "DX11Version.h"
#include "input.h"
#include "lock.h"
#include "profiling.h"

#include <sddl.h>
#include <wrl/client.h>
#include <algorithm>
#include <charconv>
#include <deque>
#include <mutex>
#include <string_view>

using Microsoft::WRL::ComPtr;

namespace Dx11Mcp {
std::atomic<bool> enabled{false};
std::atomic<bool> capture_armed{false};
std::atomic<bool> texture_dhash_armed{false};

namespace {
constexpr DWORD PipeSize = 64 * 1024;
constexpr UINT BlobLimit = 64 * 1024 * 1024;
constexpr UINT ChunkLimit = 60 * 1024;
constexpr uint64_t Any = UINT64_MAX;
const GUID ShaderRecordGuid = {0x13ac641d, 0x6def, 0x4a3d, {0x82, 0xcc, 0x83, 0x32, 0x56, 0x13, 0xbb, 0xf8}};
const GUID BufferRecordGuid = {0xeaec831c, 0xbb21, 0x4d21, {0x89, 0x4f, 0xff, 0xa1, 0x48, 0xb9, 0x30, 0x66}};
std::atomic<uint64_t> next_id{1};

std::string Utf8(std::wstring_view value)
{
	if (value.empty()) return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	std::string result(n, '\0');
	if (n) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), n, nullptr, nullptr);
	return result;
}

std::wstring Wide(std::string_view value)
{
	if (value.empty()) return {};
	const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
	std::wstring result(n, L'\0');
	if (n) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), n);
	return result;
}

std::string Quote(std::string_view value)
{
	constexpr char digits[] = "0123456789abcdef";
	std::string out = "\"";
	for (unsigned char c : value) {
		if (c == '"' || c == '\\') out += '\\';
		if (c < 0x20) {
			out += "\\u00";
			out += digits[c >> 4];
			out += digits[c & 15];
		} else {
			out += static_cast<char>(c);
		}
	}
	return out + '"';
}

std::string Hash(uint64_t value, bool wide = false)
{
	char buf[24];
	sprintf_s(buf, wide ? "\"%016llx\"" : "\"%08llx\"", static_cast<unsigned long long>(value));
	return buf;
}

bool Number(std::string_view field, uint64_t& value, int base = 10)
{
	if (base == 16 && field.substr(0, 2) == "0x") field.remove_prefix(2);
	const auto result = std::from_chars(field.data(), field.data() + field.size(), value, base);
	return result.ec == std::errc{} && result.ptr == field.data() + field.size();
}

std::string LogPath()
{
	wchar_t module[MAX_PATH]{};
	const DWORD length = GetModuleFileNameW(migoto_handle, module, _countof(module));
	if (!length || length >= _countof(module)) return {};
	const auto separator = wcsrchr(module, L'\\');
	return separator ? Utf8(std::wstring(module, separator + 1) + L"d3d11_log.txt") : std::string{};
}

struct Pipe {
	HANDLE handle = INVALID_HANDLE_VALUE;
	bool connected = false;
	ULONGLONG last_activity = 0;
	std::string pending;

	~Pipe() { Close(); }
	void Close()
	{
		if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
		handle = INVALID_HANDLE_VALUE;
		connected = false;
		pending.clear();
	}
	void Disconnect()
	{
		DisconnectNamedPipe(handle);
		connected = false;
		pending.clear();
	}
	bool Open()
	{
		// Only the game account can open this local endpoint.
		HANDLE token;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
		DWORD size = 0;
		GetTokenInformation(token, TokenUser, nullptr, 0, &size);
		std::vector<unsigned char> user(size);
		const auto got_user = size && GetTokenInformation(token, TokenUser, user.data(), size, &size);
		CloseHandle(token);
		LPWSTR sid = nullptr;
		if (!got_user || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid))
			return false;
		const std::wstring acl = std::wstring(L"D:P(A;;GA;;;") + sid + L")";
		LocalFree(sid);
		PSECURITY_DESCRIPTOR sd = nullptr;
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &sd, nullptr))
			return false;
		SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
		const auto name = L"\\\\.\\pipe\\ProjectBunny-DX11-" + std::to_wstring(GetCurrentProcessId());
		handle = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
			1, PipeSize, 4096, 0, &sa);
		LocalFree(sd);
		return handle != INVALID_HANDLE_VALUE;
	}
};

struct ShaderRecord {
	uint64_t id, hash;
	std::wstring stage, name;
	std::vector<unsigned char> bytes;
};
std::mutex shader_lock;
std::deque<ShaderRecord> shaders;
size_t shader_bytes = 0;

enum class CaptureKind { Draw, Index, Vertex, Constant, Srv, Uav, Stream, Indirect, TextureDhash };
constexpr std::string_view KindNames[] = {"draw", "ib", "vb", "cb", "srv", "uav", "so", "indirect"};
constexpr std::string_view Stages[] = {"vs", "hs", "ds", "gs", "ps", "cs"};

struct Capture {
	uint64_t id = 0, hash = Any, slot = Any, draw = Any, resource = Any;
	UINT offset = 0, length = 0, frames_left = 0, injected_ordinal = 0;
	DWORD thread = 0;
	CaptureKind kind = CaptureKind::Draw;
	std::string stage, source, hash_mode, status, details;
	std::vector<unsigned char> bytes;
	std::vector<UINT> texture_slots;
	UINT texture_index_count = 0, texture_first_index = 0;
	uint32_t texture_ib_resource = 0;
	uint64_t texture_ps = 0;

	bool after = false;
	bool indexed_range = false;

	bool Wants(CaptureKind test) const { return kind == CaptureKind::Draw || kind == test; }
	bool WantsStage(std::string_view test) const { return stage.empty() || stage == test; }
	bool WantsSlot(UINT test) const { return slot == Any || slot == test; }
};
Pipe pipe;
Capture capture;
struct ProfileSnapshot {
	uint64_t id = 0;
	int mode = 0;
	bool frozen = false;
	std::string text;
};
ProfileSnapshot profile;
// GPU readback and input callbacks can reenter OnDraw on the presenting
// thread. Other rendering threads must not wait for that work to finish.
std::recursive_mutex render_lock;
std::atomic<bool> service_active{false};
std::atomic<HackerContext*> capture_context{nullptr};
ULONGLONG retry_open = 0;

void Complete(std::string status)
{
	capture.status = std::move(status);
	capture_context.store(nullptr, std::memory_order_release);
	capture_armed.store(false, std::memory_order_release);
	texture_dhash_armed.store(false, std::memory_order_release);
}

std::string DescribeCapture()
{
	const bool fallback = capture.status == "no_matching_draw" || capture.status == "unsupported" ||
		capture.status == "mapped" || capture.status == "readback_failed";
	return "{\"id\":" + std::to_string(capture.id) + ",\"state\":" + Quote(capture.status) +
		",\"bytes\":" + std::to_string(capture.bytes.size()) + ",\"capture\":" +
		(capture.details.empty() ? "null" : capture.details) +
		",\"fallback\":" + (fallback ? "\"native_f8\"" : "null") + "}";
}

std::string ArmCapture(HackerContext* context, Capture next, UINT frames)
{
	next.id = next_id.fetch_add(1, std::memory_order_relaxed);
	next.frames_left = frames;
	next.thread = GetCurrentThreadId();
	next.status = "pending";
	capture = std::move(next);
	capture_context.store(context, std::memory_order_release);
	capture_armed.store(true, std::memory_order_release);
	texture_dhash_armed.store(capture.kind == CaptureKind::TextureDhash, std::memory_order_release);
	return DescribeCapture();
}

HRESULT ReadBuffer(HackerContext* wrapper, ID3D11Buffer* buffer, UINT begin, UINT length,
	std::vector<unsigned char>& bytes)
{
	if (wrapper->IsBufferMapped(buffer)) return DXGI_ERROR_WAS_STILL_DRAWING;
	auto* context = wrapper->GetPassThroughOrigContext1();
	D3D11_BUFFER_DESC source_desc{};
	buffer->GetDesc(&source_desc);
	if (source_desc.MiscFlags & (D3D11_RESOURCE_MISC_TILED | D3D11_RESOURCE_MISC_HW_PROTECTED |
		D3D11_RESOURCE_MISC_RESTRICTED_CONTENT))
		return DXGI_ERROR_UNSUPPORTED;
	D3D11_BUFFER_DESC staging_desc{};
	staging_desc.ByteWidth = length;
	staging_desc.Usage = D3D11_USAGE_STAGING;
	staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Device> device;
	context->GetDevice(&device);
	ComPtr<ID3D11Buffer> staging;
	HRESULT hr = device->CreateBuffer(&staging_desc, nullptr, &staging);
	if (FAILED(hr)) return hr;
	bytes.resize(length);
	D3D11_BOX box{begin, 0, 0, begin + length, 1, 1};
	context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, buffer, 0, &box);
	D3D11_MAPPED_SUBRESOURCE mapping{};
	hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapping);
	if (FAILED(hr)) {
		bytes.clear();
		return hr;
	}
	memcpy(bytes.data(), mapping.pData, length);
	context->Unmap(staging.Get(), 0);
	return S_OK;
}

uint64_t BufferId(ID3D11Buffer* buffer)
{
	uint64_t id = 0;
	UINT length = sizeof(id);
	if (SUCCEEDED(buffer->GetPrivateData(BufferRecordGuid, &length, &id)) && length == sizeof(id))
		return id;
	id = next_id.fetch_add(1, std::memory_order_relaxed);
	return SUCCEEDED(buffer->SetPrivateData(BufferRecordGuid, sizeof(id), &id)) ? id : 0;
}

struct BufferBinding {
	ComPtr<ID3D11Buffer> buffer;
	CaptureKind kind;
	std::string_view stage;
	UINT slot, offset, stride;
	DXGI_FORMAT format;
};

void AddBuffer(std::vector<BufferBinding>& out, ID3D11Buffer* buffer, CaptureKind kind,
	std::string_view stage, UINT slot, UINT offset = 0, UINT stride = 0, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN)
{
	ComPtr<ID3D11Buffer> owned;
	owned.Attach(buffer);
	if (owned) out.push_back({std::move(owned), kind, stage, slot, offset, stride, format});
}

template<typename View>
void AddView(std::vector<BufferBinding>& out, View* view, CaptureKind kind, std::string_view stage, UINT slot)
{
	ComPtr<View> owned;
	owned.Attach(view);
	if (!owned) return;
	ComPtr<ID3D11Resource> resource;
	owned->GetResource(&resource);
	ComPtr<ID3D11Buffer> buffer;
	if (resource && SUCCEEDED(resource.As(&buffer))) {
		typename std::conditional_t<std::is_same_v<View, ID3D11ShaderResourceView>,
			D3D11_SHADER_RESOURCE_VIEW_DESC, D3D11_UNORDERED_ACCESS_VIEW_DESC> desc{};
		owned->GetDesc(&desc);
		AddBuffer(out, buffer.Detach(), kind, stage, slot, 0, 0, desc.Format);
	}
}

using GetCb = void (STDMETHODCALLTYPE ID3D11DeviceContext::*)(UINT, UINT, ID3D11Buffer**);
using GetSrv = void (STDMETHODCALLTYPE ID3D11DeviceContext::*)(UINT, UINT, ID3D11ShaderResourceView**);
constexpr GetCb CBs[] = {&ID3D11DeviceContext::VSGetConstantBuffers, &ID3D11DeviceContext::HSGetConstantBuffers,
	&ID3D11DeviceContext::DSGetConstantBuffers, &ID3D11DeviceContext::GSGetConstantBuffers,
	&ID3D11DeviceContext::PSGetConstantBuffers, &ID3D11DeviceContext::CSGetConstantBuffers};
constexpr GetSrv SRVs[] = {&ID3D11DeviceContext::VSGetShaderResources, &ID3D11DeviceContext::HSGetShaderResources,
	&ID3D11DeviceContext::DSGetShaderResources, &ID3D11DeviceContext::GSGetShaderResources,
	&ID3D11DeviceContext::PSGetShaderResources, &ID3D11DeviceContext::CSGetShaderResources};

std::vector<BufferBinding> Bindings(ID3D11DeviceContext* context, const DrawCallInfo& call, bool compute)
{
	std::vector<BufferBinding> result;
	if (!compute && capture.Wants(CaptureKind::Index)) {
		ID3D11Buffer* buffer = nullptr;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		UINT offset = 0;
		context->IAGetIndexBuffer(&buffer, &format, &offset);
		AddBuffer(result, buffer, CaptureKind::Index, "ia", 0, offset, format == DXGI_FORMAT_R32_UINT ? 4 : 2, format);
	}
	if (!compute && capture.Wants(CaptureKind::Vertex)) {
		ID3D11Buffer* buffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT]{};
		UINT stride[_countof(buffers)]{}, offset[_countof(buffers)]{};
		context->IAGetVertexBuffers(0, _countof(buffers), buffers, stride, offset);
		for (UINT i = 0; i < _countof(buffers); ++i)
			AddBuffer(result, buffers[i], CaptureKind::Vertex, "ia", i, offset[i], stride[i]);
	}
	if (!compute && capture.Wants(CaptureKind::Stream)) {
		ID3D11Buffer* buffers[D3D11_SO_BUFFER_SLOT_COUNT]{};
		context->SOGetTargets(_countof(buffers), buffers);
		for (UINT i = 0; i < _countof(buffers); ++i) AddBuffer(result, buffers[i], CaptureKind::Stream, "so", i);
	}
	const size_t first = compute ? 5 : 0, last = compute ? 6 : 5;
	for (size_t i = first; i < last; ++i) {
		if (!capture.WantsStage(Stages[i])) continue;
		if (capture.Wants(CaptureKind::Constant)) {
			ID3D11Buffer* buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT]{};
			(context->*CBs[i])(0, _countof(buffers), buffers);
			for (UINT j = 0; j < _countof(buffers); ++j)
				AddBuffer(result, buffers[j], CaptureKind::Constant, Stages[i], j);
		}
		if (capture.Wants(CaptureKind::Srv)) {
			ID3D11ShaderResourceView* views[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
			(context->*SRVs[i])(0, _countof(views), views);
			for (UINT j = 0; j < _countof(views); ++j) AddView(result, views[j], CaptureKind::Srv, Stages[i], j);
		}
	}
	const auto uav_stage = compute ? "cs" : "om";
	if (capture.Wants(CaptureKind::Uav) && capture.WantsStage(uav_stage)) {
		ComPtr<ID3D11Device> device;
		context->GetDevice(&device);
		ID3D11UnorderedAccessView* views[D3D11_1_UAV_SLOT_COUNT]{};
		const UINT count = device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1 ? _countof(views) :
			D3D11_PS_CS_UAV_REGISTER_COUNT;
		if (compute) context->CSGetUnorderedAccessViews(0, count, views);
		else context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, count, views);
		for (UINT i = 0; i < count; ++i) AddView(result, views[i], CaptureKind::Uav, uav_stage, i);
	}
	if (capture.Wants(CaptureKind::Indirect) && call.indirect_buffer && *call.indirect_buffer) {
		(*call.indirect_buffer)->AddRef();
		AddBuffer(result, *call.indirect_buffer, CaptureKind::Indirect, "", 0, call.args_offset);
	}
	return result;
}

std::string DescribeBuffer(const BufferBinding& binding, const D3D11_BUFFER_DESC& desc,
	uint64_t id, uint32_t full_hash, uint32_t region_hash, UINT region_offset, UINT region_length)
{
	return "{\"kind\":" + Quote(KindNames[static_cast<size_t>(binding.kind)]) +
		",\"stage\":" + Quote(binding.stage) + ",\"slot\":" + std::to_string(binding.slot) +
		",\"resource_id\":" + std::to_string(id) + ",\"byte_width\":" + std::to_string(desc.ByteWidth) +
		",\"bind_flags\":" + std::to_string(desc.BindFlags) + ",\"usage\":" + std::to_string(desc.Usage) +
		",\"binding_offset\":" + std::to_string(binding.offset) + ",\"stride\":" + std::to_string(binding.stride) +
		",\"format\":" + std::to_string(binding.format) + ",\"resource_hash\":" + (full_hash ? Hash(full_hash) : "null") +
		",\"region_hash\":" + (region_hash ? Hash(region_hash) : "null") +
		",\"region_offset\":" + std::to_string(region_offset) +
		",\"region_bytes\":" + std::to_string(region_length) + "}";
}

template<class T>
void ShaderBinding(std::string& out, ID3D11DeviceContext* context, std::string_view stage,
	void (STDMETHODCALLTYPE ID3D11DeviceContext::*get)(T**, ID3D11ClassInstance**, UINT*))
{
	ComPtr<T> shader;
	(context->*get)(&shader, nullptr, nullptr);
	if (!shader) return;
	uint64_t id = 0;
	UINT size = sizeof(id);
	shader->GetPrivateData(ShaderRecordGuid, &size, &id);
	uint64_t hunting_hash = 0;
	{
		CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
		auto it = G->mShaders.find(shader.Get());
		if (it != G->mShaders.end()) hunting_hash = it->second;
	}
	if (out.back() != '{') out += ',';
	out += Quote(stage) + ":{\"catalog_id\":" + (id ? std::to_string(id) : "null") +
		",\"hunting_hash\":" + (hunting_hash ? Hash(hunting_hash, true) : "null") + "}";
}

std::string DescribeInputLayout(HackerContext* context)
{
	ComPtr<ID3D11InputLayout> value;
	context->IAGetInputLayout(&value);
	ComPtr<HackerInputLayout> layout;
	if (!value || FAILED(value.As(&layout))) return "null";
	std::string out = "{\"hash\":" + Hash(layout->GetLayoutHash(), true) + ",\"elements\":[";
	const auto elements = layout->GetElements();
	for (UINT i = 0; i < layout->GetElementCount(); ++i) {
		if (i) out += ',';
		const auto& element = elements[i];
		out += "{\"semantic\":" + Quote(element.SemanticName ? element.SemanticName : "") +
			",\"index\":" + std::to_string(element.SemanticIndex) + ",\"slot\":" + std::to_string(element.InputSlot) +
			",\"offset\":" + std::to_string(element.AlignedByteOffset) + ",\"format\":" + std::to_string(element.Format) +
			",\"class\":" + std::to_string(element.InputSlotClass) +
			",\"instance_step\":" + std::to_string(element.InstanceDataStepRate) + "}";
	}
	return out + "]}";
}

std::string DescribeDraw(HackerContext* wrapper, ID3D11DeviceContext* context, const DrawCallInfo& call,
	UINT ordinal, bool injected)
{
	std::string out = "{\"frame\":" + std::to_string(G->frame_no) + ",\"draw\":" + std::to_string(ordinal) +
		",\"phase\":" + Quote(capture.after ? "after" : "before") +
		",\"injected\":" + (injected ? "true" : "false") + ",\"type\":" + std::to_string(static_cast<int>(call.type)) +
		",\"vertex_count\":" + std::to_string(call.VertexCount) + ",\"index_count\":" + std::to_string(call.IndexCount) +
		",\"instance_count\":" + std::to_string(call.InstanceCount) + ",\"first_index\":" + std::to_string(call.FirstIndex) +
		",\"first_vertex\":" + std::to_string(call.FirstVertex) + ",\"first_instance\":" + std::to_string(call.FirstInstance) +
		",\"dispatch\":[" + std::to_string(call.ThreadGroupCountX) + "," + std::to_string(call.ThreadGroupCountY) +
		"," + std::to_string(call.ThreadGroupCountZ) + "],\"shaders\":{";
	if (call.type == DrawCall::Dispatch || call.type == DrawCall::DispatchIndirect) {
		ShaderBinding(out, context, "cs", &ID3D11DeviceContext::CSGetShader);
	} else {
		ShaderBinding(out, context, "vs", &ID3D11DeviceContext::VSGetShader);
		ShaderBinding(out, context, "hs", &ID3D11DeviceContext::HSGetShader);
		ShaderBinding(out, context, "ds", &ID3D11DeviceContext::DSGetShader);
		ShaderBinding(out, context, "gs", &ID3D11DeviceContext::GSGetShader);
		ShaderBinding(out, context, "ps", &ID3D11DeviceContext::PSGetShader);
		D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		context->IAGetPrimitiveTopology(&topology);
		return out + "},\"topology\":" + std::to_string(topology) + ",\"input_layout\":" + DescribeInputLayout(wrapper);
	}
	return out + "}";
}

void ReadBoundBuffer(HackerContext* context, const BufferBinding& binding, UINT total,
	UINT offset, UINT requested_length)
{
	if (offset >= total || (requested_length && requested_length > total - offset)) {
		Complete("range_out_of_bounds");
		return;
	}
	const UINT length = requested_length ? requested_length : total - offset;
	if (length > BlobLimit) {
		Complete("range_too_large");
		return;
	}
	const auto result = ReadBuffer(context, binding.buffer.Get(), offset, length, capture.bytes);
	if (result == DXGI_ERROR_WAS_STILL_DRAWING) Complete("mapped");
	else if (result == DXGI_ERROR_UNSUPPORTED) Complete("unsupported");
	else Complete(SUCCEEDED(result) ? "ready" : "readback_failed");
}

bool UsesDirectIndices(const DrawCallInfo& call)
{
	return call.IndexCount && (call.type == DrawCall::DrawIndexed ||
		(call.type == DrawCall::DrawIndexedInstanced && call.InstanceCount));
}

ProfileSnapshot ReadProfile()
{
	std::wstring text;
	ProfileSnapshot snapshot;
	{
		CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
		text = Profiling::text;
		snapshot.mode = static_cast<int>(Profiling::mode.load());
		snapshot.frozen = Profiling::freeze;
	}
	snapshot.text = Utf8(text);
	return snapshot;
}

std::string DescribeProfile(const ProfileSnapshot& snapshot)
{
	return "{\"snapshot_id\":" + std::to_string(snapshot.id) + ",\"mode\":" + std::to_string(snapshot.mode) +
		",\"frozen\":" + (snapshot.frozen ? "true" : "false") +
		",\"total_bytes\":" + std::to_string(snapshot.text.size()) + "}";
}

std::string ProtocolError(std::string_view code, std::string_view message)
{
	return "{\"code\":" + Quote(code) + ",\"error\":" + Quote(message) + "}";
}

template<typename T>
std::string HuntingSet(const std::set<T>& set, T selected, uint64_t offset, bool wide)
{
	std::string out = "{\"selected\":" + (selected && selected != static_cast<T>(-1) ?
		Hash(selected, wide) : "null") + ",\"total\":" + std::to_string(set.size()) + ",\"hashes\":[";
	auto it = set.begin();
	std::advance(it, std::min<uint64_t>(offset, set.size()));
	for (unsigned count = 0; count < 256 && it != set.end(); ++count, ++it) {
		if (count) out += ',';
		out += Hash(*it, wide);
	}
	return out + "]}";
}

std::string Hunting(std::string_view kind, uint64_t offset)
{
	CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
	if (kind == "ib") return HuntingSet(G->mVisitedIndexBuffers, G->mSelectedIndexBuffer, offset, false);
	if (kind == "vb") return HuntingSet(G->mVisitedVertexBuffers, G->mSelectedVertexBuffer, offset, false);
	if (kind == "vs") return HuntingSet(G->mVisitedVertexShaders, G->mSelectedVertexShader, offset, true);
	if (kind == "ps") return HuntingSet(G->mVisitedPixelShaders, G->mSelectedPixelShader, offset, true);
	if (kind == "cs") return HuntingSet(G->mVisitedComputeShaders, G->mSelectedComputeShader, offset, true);
	if (kind == "gs") return HuntingSet(G->mVisitedGeometryShaders, G->mSelectedGeometryShader, offset, true);
	if (kind == "hs") return HuntingSet(G->mVisitedHullShaders, G->mSelectedHullShader, offset, true);
	if (kind == "ds") return HuntingSet(G->mVisitedDomainShaders, G->mSelectedDomainShader, offset, true);
	if (kind == "rt") {
		CriticalSectionLockGuard resources(&G->mResourcesLock, __FUNCTION__, __LINE__);
		const auto selected = G->mResources.find(G->mSelectedRenderTarget);
		const auto hash = selected == G->mResources.end() ? 0 : selected->second.hash;
		return "{\"selected\":" + (hash ? Hash(hash) : "null") + ",\"position\":" +
			std::to_string(G->mSelectedRenderTargetPos) + ",\"total\":" + std::to_string(G->mVisitedRenderTargets.size()) + "}";
	}
	return "{\"error\":\"unknown hunting type\"}";
}

std::string ShaderCatalog(uint64_t offset)
{
	std::lock_guard lock(shader_lock);
	std::string out = "{\"total\":" + std::to_string(shaders.size()) + ",\"shaders\":[";
	for (size_t i = std::min<uint64_t>(offset, shaders.size()); i < shaders.size() && i - offset < 48; ++i) {
		const auto& shader = shaders[i];
		if (i != offset) out += ',';
		out += "{\"id\":" + std::to_string(shader.id) + ",\"hash\":" + Hash(shader.hash, true) +
			",\"stage\":" + Quote(Utf8(shader.stage)) + ",\"name\":" + Quote(Utf8(shader.name)) +
			",\"bytes\":" + std::to_string(shader.bytes.size()) + "}";
	}
	return out + "]}";
}

std::string Invoke(HackerDevice* device, std::string_view key, std::string_view phase, bool confirm)
{
	if (phase != "press" && phase != "release" && phase != "tap") return "{\"error\":\"invalid phase\"}";
	const auto name = Wide(key);
	const bool wipe = _wcsicmp(name.c_str(), L"Hunting.wipe_user_config") == 0 ||
		_wcsicmp(name.c_str(), L"ctrl+alt+f10") == 0;
	if (wipe && !confirm) return "{\"error\":\"Ctrl+Alt+F10 clears persistent user settings; confirmation required\"}";
	const auto action = [&](const std::wstring& id) {
		if (phase == "release") return InvokeInputBinding(device, id, true);
		const bool invoked = InvokeInputBinding(device, id, false);
		if (invoked && phase == "tap") InvokeInputBinding(device, id, true);
		return invoked;
	};
	bool invoked = false;
	if (_wcsicmp(name.c_str(), L"f10") == 0) {
		invoked = action(L"Hunting.reload_config");
		invoked = action(L"Hunting.reload_fixes") || invoked;
	} else if (_wcsicmp(name.c_str(), L"f8") == 0) {
		invoked = action(L"Hunting.analyse_frame");
	} else if (_wcsicmp(name.c_str(), L"ctrl+f9") == 0) {
		invoked = action(L"Hunting.monitor_performance");
	} else if (wipe) {
		invoked = action(L"Hunting.wipe_user_config");
	} else {
		invoked = action(name);
	}
	return invoked ? "{\"dispatched\":true}" : "{\"error\":\"binding is unavailable or disabled by input scope\"}";
}

std::string Process(HackerDevice* device, HackerContext* wrapper, std::string_view request)
{
	std::vector<std::string_view> fields;
	while (true) {
		const auto tab = request.find('\t');
		fields.push_back(request.substr(0, tab));
		if (tab == request.npos) break;
		request.remove_prefix(tab + 1);
	}
	const auto operation = fields.front();
	uint64_t id = 0, offset = 0, length = 0;
	if (operation == "STATUS" && fields.size() == 1)
		return "{\"pid\":" + std::to_string(GetCurrentProcessId()) + ",\"protocol\":1,\"version\":\"" VER_FILE_VERSION_STR
			"\",\"frame\":" + std::to_string(G->frame_no) + ",\"hunting\":" + std::to_string(G->hunting) +
			",\"region_enabled\":" + (G->track_region_hashes ? "true" : "false") +
			",\"features\":[\"profiling_chunks\",\"indexed_range\",\"custom_resource_errors\",\"texture_dhash\"]" +
			",\"capture_coverage\":{\"context\":\"present_immediate\",\"deferred\":\"native_f8\",\"textures\":\"native_f8\"}" +
			",\"capture_active\":" + (G->analyse_frame ? "true" : "false") +
			",\"frame_analysis_dir\":" + Quote(Utf8(G->ANALYSIS_PATH)) + ",\"log_path\":" +
			Quote(LogPath()) + "}";
	if (operation == "KEYS" && fields.size() == 2 && Number(fields[1], offset)) {
		const auto bindings = ListInputBindings();
		std::string out = "{\"total\":" + std::to_string(bindings.size()) + ",\"bindings\":[";
		for (size_t i = std::min<uint64_t>(offset, bindings.size()); i < bindings.size() && i - offset < 48; ++i) {
			if (i != offset) out += ',';
			out += "{\"id\":" + Quote(Utf8(bindings[i].id)) + ",\"key\":" + Quote(Utf8(bindings[i].keys)) + "}";
		}
		return out + "]}";
	}
	if (operation == "KEY" && fields.size() == 4) return Invoke(device, fields[1], fields[2], fields[3] == "1");
	if (operation == "HUNT" && fields.size() == 3 && Number(fields[2], offset)) return Hunting(fields[1], offset);
	if (operation == "PROFILE" && fields.size() == 1) {
		const auto snapshot = ReadProfile();
		return "{\"mode\":" + std::to_string(snapshot.mode) +
			",\"frozen\":" + (snapshot.frozen ? "true" : "false") + ",\"text\":" + Quote(snapshot.text) + "}";
	}
	if (operation == "PROFILE_BEGIN" && fields.size() == 1) {
		profile = ReadProfile();
		profile.id = next_id.fetch_add(1, std::memory_order_relaxed);
		return DescribeProfile(profile);
	}
	if (operation == "PROFILE_META" && fields.size() == 2 && Number(fields[1], id)) {
		if (!id || id != profile.id) return ProtocolError("PROFILE_SNAPSHOT_EXPIRED", "Profiling snapshot was replaced");
		return DescribeProfile(profile);
	}
	if (operation == "PROFILE_READ" && fields.size() == 4 && Number(fields[1], id) &&
		Number(fields[2], offset) && Number(fields[3], length)) {
		if (!id || id != profile.id) return ProtocolError("PROFILE_SNAPSHOT_EXPIRED", "Profiling snapshot was replaced");
		const size_t size = profile.text.size();
		if (offset > size || length < 4 || length > ChunkLimit)
			return ProtocolError("PROFILE_RANGE_INVALID", "Invalid profiling offset or chunk size");
		const auto continuation = [&](size_t index) {
			return index < size && (static_cast<unsigned char>(profile.text[index]) & 0xc0) == 0x80;
		};
		if (continuation(static_cast<size_t>(offset)))
			return ProtocolError("PROFILE_RANGE_INVALID", "Offset must start at a UTF-8 character");
		size_t end = static_cast<size_t>(offset) + (std::min<uint64_t>)(length, size - offset);
		while (continuation(end)) --end;
		return "B" + profile.text.substr(static_cast<size_t>(offset), end - static_cast<size_t>(offset));
	}
	if (operation == "SHADERS" && fields.size() == 2 && Number(fields[1], offset)) return ShaderCatalog(offset);
	if (operation == "CUSTOMS" && fields.size() == 2 && Number(fields[1], offset)) {
		CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
		std::vector<std::wstring_view> names;
		names.reserve(customResources.size());
		for (const auto& entry : customResources) names.push_back(entry.first);
		std::sort(names.begin(), names.end());
		std::string out = "{\"total\":" + std::to_string(names.size()) + ",\"resources\":[";
		for (size_t i = std::min<uint64_t>(offset, names.size()); i < names.size() && i - offset < 48; ++i) {
			if (i != offset) out += ',';
			out += Quote(Utf8(names[i]));
		}
		return out + "]}";
	}
	if (operation == "TEXTURE_DHASH") {
		if (capture_armed.load()) return "{\"error\":\"a capture is pending\"}";
		Capture next;
		uint64_t backing = 0, indices = 0, first = 0, frames = 0;
		if (fields.size() < 9 || fields.size() > 16 || !Number(fields[1], next.hash, 16) ||
			!next.hash || next.hash > UINT32_MAX || (fields[2] != "resource" && fields[2] != "region") ||
			!Number(fields[3], backing, 16) || backing > UINT32_MAX || (fields[2] == "region" && !backing) ||
			!Number(fields[4], indices) || !indices || indices > UINT_MAX ||
			!Number(fields[5], first) || first > UINT_MAX || !Number(fields[6], next.texture_ps, 16) ||
			!Number(fields[7], frames) || !frames || frames > 120)
			return ProtocolError("TEXTURE_DHASH_SELECTOR_INVALID", "An index hash, draw range, and one to eight PS slots are required; a region also needs its backing IB hash");
		for (size_t i = 8; i < fields.size(); ++i) {
			uint64_t slot;
			if (!Number(fields[i], slot) || slot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT ||
				std::find(next.texture_slots.begin(), next.texture_slots.end(), slot) != next.texture_slots.end())
				return ProtocolError("TEXTURE_DHASH_SELECTOR_INVALID", "PS slots must be unique integers from 0 through 127");
			next.texture_slots.push_back(static_cast<UINT>(slot));
		}
		next.kind = CaptureKind::TextureDhash;
		next.hash_mode = fields[2];
		next.texture_ib_resource = static_cast<uint32_t>(backing);
		next.texture_index_count = static_cast<UINT>(indices);
		next.texture_first_index = static_cast<UINT>(first);
		return ArmCapture(wrapper, std::move(next), static_cast<UINT>(frames));
	}
	if (operation == "POLL" && fields.size() == 2 && Number(fields[1], id) && id == capture.id && id)
		return DescribeCapture();
	if (operation == "CANCEL" && fields.size() == 2 && Number(fields[1], id) && id == capture.id && id) {
		if (capture_armed.load()) Complete("cancelled");
		return DescribeCapture();
	}
	if (operation == "BYTES" && fields.size() == 4 && Number(fields[1], id) && Number(fields[2], offset) &&
		Number(fields[3], length) && id == capture.id && id && capture.status == "ready" && length <= ChunkLimit &&
		offset <= capture.bytes.size() && length <= capture.bytes.size() - offset) {
		return length ? "B" + std::string(reinterpret_cast<const char*>(capture.bytes.data() + offset),
			static_cast<size_t>(length)) : "B";
	}
	if (operation == "SHADER" && fields.size() == 2 && Number(fields[1], id)) {
		if (capture_armed.load()) return "{\"error\":\"a capture is pending\"}";
		std::lock_guard lock(shader_lock);
		for (const auto& shader : shaders) {
			if (shader.id != id) continue;
			capture = {};
			capture.id = next_id.fetch_add(1, std::memory_order_relaxed);
			capture.bytes = shader.bytes;
			capture.details = "{\"shader_id\":" + std::to_string(id) + ",\"hash\":" + Hash(shader.hash, true) +
				",\"stage\":" + Quote(Utf8(shader.stage)) + ",\"name\":" + Quote(Utf8(shader.name)) + "}";
			Complete("ready");
			return DescribeCapture();
		}
		return "{\"error\":\"unknown shader id or evicted bytecode\"}";
	}
	if (operation == "CUSTOM" && fields.size() == 4 && Number(fields[2], offset) && Number(fields[3], length) &&
		offset <= UINT_MAX && length <= BlobLimit) {
		if (capture_armed.load()) return "{\"error\":\"a capture is pending\"}";
		ComPtr<ID3D11Resource> resource;
		const auto name = Wide(fields[1]);
		{
			CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
			const auto it = std::find_if(customResources.begin(), customResources.end(), [&](const auto& entry) {
				return _wcsicmp(entry.first.c_str(), name.c_str()) == 0;
			});
			if (it == customResources.end())
				return ProtocolError("CUSTOM_RESOURCE_NOT_FOUND", "No loaded Resource section has this name");
			const auto& entry = it->second;
			if (!entry.resource) {
				if (!entry.substantiated)
					return ProtocolError("CUSTOM_RESOURCE_UNINITIALIZED", "Resource declaration has no initialized instance");
				return ProtocolError("CUSTOM_RESOURCE_NO_INSTANCE", "Resource has no backing D3D11 object");
			}
			if (entry.is_null)
				return ProtocolError("CUSTOM_RESOURCE_NULL", "Resource currently resolves to null");
			resource = entry.resource;
		}
		ComPtr<ID3D11Buffer> buffer;
		if (FAILED(resource.As(&buffer)))
			return ProtocolError("CUSTOM_RESOURCE_NOT_BUFFER", "Live resource is not a D3D11 buffer");
		auto* context = wrapper->GetPassThroughOrigContext1();
		ComPtr<ID3D11Device> source, target;
		buffer->GetDevice(&source);
		context->GetDevice(&target);
		if (source.Get() != target.Get())
			return ProtocolError("CUSTOM_RESOURCE_WRONG_DEVICE", "Live buffer belongs to another D3D11 device");
		D3D11_BUFFER_DESC desc{};
		buffer->GetDesc(&desc);
		capture = {};
		capture.id = next_id.fetch_add(1, std::memory_order_relaxed);
		capture.offset = static_cast<UINT>(offset);
		capture.length = static_cast<UINT>(length);
		capture.details = "{\"name\":" + Quote(fields[1]) + ",\"frame\":" + std::to_string(G->frame_no) +
			",\"byte_width\":" + std::to_string(desc.ByteWidth) + ",\"offset\":" + std::to_string(offset) + "}";
		ReadBoundBuffer(wrapper, {std::move(buffer), CaptureKind::Srv, "", 0, 0, 0, DXGI_FORMAT_UNKNOWN},
			desc.ByteWidth, capture.offset, capture.length);
		return DescribeCapture();
	}
	if (operation == "ARM" && (fields.size() == 13 || fields.size() == 14)) {
		if (capture_armed.load()) return "{\"error\":\"a capture is pending\"}";
		Capture next;
		const auto kind = std::find(std::begin(KindNames), std::end(KindNames), fields[1]);
		uint64_t frames = 0;
		if (kind == std::end(KindNames) || !Number(fields[2], next.hash, 16) ||
			!Number(fields[4], next.slot) || !Number(fields[5], next.draw) || !Number(fields[6], frames) ||
			!Number(fields[7], offset) || !Number(fields[8], length) || !Number(fields[10], next.resource) ||
			frames < 1 || frames > 120 || offset > UINT_MAX || length > BlobLimit ||
			(next.hash != Any && (next.hash == 0 || next.hash > UINT32_MAX)) ||
			(fields[9] != "resource" && fields[9] != "region" && fields[9] != "auto") ||
			(fields[11] != "any" && fields[11] != "game" && fields[11] != "mod") ||
			(fields[12] != "auto" && fields[12] != "before" && fields[12] != "after"))
			return "{\"error\":\"invalid capture selector or size\"}";
		next.kind = static_cast<CaptureKind>(kind - std::begin(KindNames));
		if (fields.size() == 14) {
			next.indexed_range = fields[13] == "1";
			if ((fields[13] != "0" && fields[13] != "1") ||
				(next.indexed_range && (next.kind != CaptureKind::Index || offset || length)))
				return ProtocolError("INDEXED_RANGE_INVALID", "Indexed range requires kind=ib without a byte slice");
		}
		next.stage = fields[3];
		next.offset = static_cast<UINT>(offset);
		next.length = static_cast<UINT>(length);
		next.hash_mode = fields[9];
		next.source = fields[11];
		next.after = fields[12] == "after" ||
			(fields[12] == "auto" && (next.kind == CaptureKind::Uav || next.kind == CaptureKind::Stream));
		return ArmCapture(wrapper, std::move(next), static_cast<UINT>(frames));
	}
	return "{\"error\":\"invalid request, unknown or replaced capture id\"}";
}
}

void OnPresent(HackerDevice* device, HackerContext* context)
{
	if (!enabled.load(std::memory_order_acquire) && !service_active.load(std::memory_order_relaxed)) return;
	std::unique_lock lock(render_lock, std::try_to_lock);
	if (!lock.owns_lock()) return;
	if (!enabled.load(std::memory_order_acquire)) {
		pipe.Close();
		capture_context.store(nullptr, std::memory_order_release);
		capture_armed.store(false, std::memory_order_release);
		texture_dhash_armed.store(false, std::memory_order_release);
		capture = {};
		profile = {};
		service_active.store(false, std::memory_order_release);
		return;
	}
	if (!context) return;
	service_active.store(true, std::memory_order_relaxed);
	if (capture_armed.load(std::memory_order_relaxed)) {
		const auto owner = capture_context.load(std::memory_order_acquire);
		if (!owner) {
			Complete("device_changed");
		} else if (owner == context) {
			capture.thread = GetCurrentThreadId();
			capture.injected_ordinal = 0;
			if (--capture.frames_left == 0)
				Complete("no_matching_draw");
		}
	}
	const auto now = GetTickCount64();
	try {
		if (pipe.handle == INVALID_HANDLE_VALUE) {
			if (now < retry_open) return;
			retry_open = now + 2000;
			if (!pipe.Open()) return;
		}
		if (!pipe.connected) {
			const BOOL connected = ConnectNamedPipe(pipe.handle, nullptr);
			const auto error = connected ? ERROR_SUCCESS : GetLastError();
			if (error == ERROR_NO_DATA) pipe.Disconnect();
			if (error != ERROR_PIPE_CONNECTED) return;
			pipe.connected = true;
			pipe.last_activity = now;
		}
		if (now - pipe.last_activity > 15000) {
			pipe.Disconnect();
			return;
		}
		if (pipe.pending.empty()) {
			char buffer[4096];
			DWORD count = 0;
			if (!ReadFile(pipe.handle, buffer, sizeof(buffer), &count, nullptr)) {
				if (GetLastError() != ERROR_NO_DATA) pipe.Disconnect();
				return;
			}
			pipe.last_activity = now;
			if (!count) return;
			pipe.pending = Process(device, context, {buffer, count});
			if (pipe.pending.size() > PipeSize)
				pipe.pending = "{\"error\":\"response exceeds transport limit, narrow the query\"}";
		}
		DWORD written = 0;
		if (!WriteFile(pipe.handle, pipe.pending.data(), static_cast<DWORD>(pipe.pending.size()), &written, nullptr)) {
			pipe.Disconnect();
		} else if (written == pipe.pending.size()) {
			pipe.pending.clear();
		}
	} catch (const std::exception&) {
		pipe.Disconnect();
	}
}

void OnContextDestroyed(HackerContext* context) noexcept
{
	// Retire the borrowed pointer without waiting on a rendering thread.
	// The next Present cancels any pending capture before serving the pipe.
	capture_context.compare_exchange_strong(context, nullptr, std::memory_order_acq_rel);
}

void OnOriginalDraw(HackerContext* context, const DrawCallInfo& call)
{
	if (context != capture_context.load(std::memory_order_acquire))
		return;
	std::unique_lock lock(render_lock, std::try_to_lock);
	if (!lock.owns_lock() || capture.kind != CaptureKind::TextureDhash ||
		context != capture_context.load(std::memory_order_acquire) || capture.thread != GetCurrentThreadId() ||
		!UsesDirectIndices(call) || call.IndexCount != capture.texture_index_count ||
		call.FirstIndex != capture.texture_first_index)
		return;

	try {
		auto* native = context->GetPassThroughOrigContext1();
		ComPtr<ID3D11Buffer> ib;
		DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
		UINT binding_offset = 0;
		native->IAGetIndexBuffer(&ib, &index_format, &binding_offset);
		if (!ib) return;
		const auto shader = context->mCurrentPixelShaderHandle;
		uint32_t backing_hash;
		uint64_t ps_hash = 0;
		UINT mip_size;
		bool rehash;
		{
			CriticalSectionLockGuard globals_lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
			backing_hash = GetResourceHash(ib.Get());
			if (shader) {
				const auto found = G->mShaders.find(shader);
				if (found != G->mShaders.end()) ps_hash = found->second;
			}
			mip_size = static_cast<UINT>((std::max)(0, G->dhash_mip_size));
			rehash = G->dhash_allow_rehash;
		}
		if ((capture.texture_ps && capture.texture_ps != ps_hash) ||
			(capture.texture_ib_resource && capture.texture_ib_resource != backing_hash))
			return;
		if (capture.hash_mode == "region") {
			const UINT stride = index_format == DXGI_FORMAT_R16_UINT ? 2 :
				(index_format == DXGI_FORMAT_R32_UINT ? 4 : 0);
			const uint64_t begin = binding_offset + uint64_t(call.FirstIndex) * stride;
			const uint64_t length = uint64_t(call.IndexCount) * stride;
			D3D11_BUFFER_DESC desc{};
			ib->GetDesc(&desc);
			if (!stride || begin >= desc.ByteWidth || length > desc.ByteWidth - begin || length > BlobLimit) return;
			std::vector<unsigned char> bytes;
			if (FAILED(ReadBuffer(context, ib.Get(), static_cast<UINT>(begin), static_cast<UINT>(length), bytes)) ||
				crc32c_hw(0, bytes.data(), bytes.size()) != capture.hash)
				return;
		} else if (capture.hash != backing_hash) {
			return;
		}

		std::string out = "{\"frame\":" + std::to_string(G->frame_no) +
			",\"draw\":" + std::to_string(context->GetDrawNumber()) +
			",\"phase\":\"original_before_overrides\",\"ib_hash\":" + Hash(capture.hash) +
			",\"hash_mode\":" + Quote(capture.hash_mode) + ",\"ib_resource_hash\":" + Hash(backing_hash) +
			",\"ps_hash\":" + (ps_hash ? Hash(ps_hash, true) : "null") +
			",\"index_count\":" + std::to_string(call.IndexCount) +
			",\"first_index\":" + std::to_string(call.FirstIndex) + ",\"slots\":[";
		for (UINT slot : capture.texture_slots) {
			if (out.back() != '[') out += ',';
			out += "{\"slot\":" + std::to_string(slot);
			ComPtr<ID3D11ShaderResourceView> view;
			native->PSGetShaderResources(slot, 1, &view);
			if (!view) {
				out += ",\"state\":\"unbound\"}";
				continue;
			}
			ComPtr<ID3D11Resource> resource;
			ComPtr<ID3D11Texture2D> texture;
			view->GetResource(&resource);
			if (!resource || FAILED(resource.As(&texture))) {
				out += ",\"state\":\"not_texture2d\"}";
				continue;
			}
			D3D11_TEXTURE2D_DESC desc{};
			D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{};
			texture->GetDesc(&desc);
			view->GetDesc(&view_desc);
			out += ",\"width\":" + std::to_string(desc.Width) + ",\"height\":" + std::to_string(desc.Height) +
				",\"format\":" + std::to_string(view_desc.Format) + ",\"format_name\":" + Quote(TexFormatStr(view_desc.Format));
			uint64_t dhash;
			const bool supported = !(desc.MiscFlags & (D3D11_RESOURCE_MISC_TILED |
				D3D11_RESOURCE_MISC_HW_PROTECTED | D3D11_RESOURCE_MISC_RESTRICTED_CONTENT));
			if (supported && GetDhash(context, resource.Get(), mip_size, rehash, dhash, view.Get())) {
				out += ",\"state\":\"ready\",\"dhash\":" + Hash(dhash, true) + "}";
			} else {
				out += ",\"state\":\"unavailable\"}";
			}
		}
		capture.details = out + "]}";
		Complete("ready");
	} catch (const std::exception&) {
		Complete("readback_failed");
	}
}

void OnDraw(HackerContext* context, const DrawCallInfo& call, bool injected, bool after)
{
	if (context != capture_context.load(std::memory_order_acquire))
		return;
	std::unique_lock lock(render_lock, std::try_to_lock);
	if (!lock.owns_lock() || !capture_armed.load(std::memory_order_relaxed) ||
		context != capture_context.load(std::memory_order_acquire) ||
		capture.thread != GetCurrentThreadId() || capture.kind == CaptureKind::TextureDhash)
		return;
	const bool compute = call.type == DrawCall::Dispatch || call.type == DrawCall::DispatchIndirect;
	if (injected && !after) ++capture.injected_ordinal;
	if (after != capture.after) return;
	const UINT index = injected ? capture.injected_ordinal :
		(compute ? context->GetDispatchNumber() : context->GetDrawNumber());
	if ((capture.draw != Any && capture.draw != index) || (capture.source == "game" && injected) ||
		(capture.source == "mod" && !injected)) return;
	if (capture.indexed_range && !UsesDirectIndices(call)) return;
	try {
		auto* native = context->GetPassThroughOrigContext1();
		std::string buffers = "[";
		for (const auto& binding : Bindings(native, call, compute)) {
			if (!capture.WantsSlot(binding.slot) || !capture.WantsStage(binding.stage)) continue;
			const auto id = BufferId(binding.buffer.Get());
			if (capture.resource != Any && capture.resource != id) continue;
			uint32_t full_hash;
			{
				CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
				full_hash = GetResourceHash(binding.buffer.Get());
			}
			D3D11_BUFFER_DESC desc{};
			binding.buffer->GetDesc(&desc);
			auto draw_call = call;
			UINT region_offset = 0, region_length = 0;
			uint32_t region_hash = 0;
			const bool check_region = capture.hash != Any && (capture.hash_mode == "region" ||
				(capture.hash_mode == "auto" && capture.hash != full_hash));
			if (check_region && G->track_region_hashes && full_hash && !context->IsBufferMapped(binding.buffer.Get())) {
				if (binding.kind == CaptureKind::Index && binding.offset && call.IndexCount) {
					region_offset = GetIndexBufferRegionOffset(binding.format, &draw_call, binding.offset);
					region_length = GetIndexBufferRegionSize(binding.format, &draw_call);
				} else if (binding.kind == CaptureKind::Vertex && binding.stride) {
					region_offset = GetVertexBufferRegionOffset(binding.stride, &draw_call, binding.offset);
					region_length = GetVertexBufferRegionSize(binding.stride, &draw_call);
				}
				if (region_length && region_offset < desc.ByteWidth && region_length <= BlobLimit) {
					region_length = (std::min)(region_length, desc.ByteWidth - region_offset);
					region_hash = GetRegionHash(context, binding.buffer.Get(), region_offset, region_length);
				}
			}
			const bool matched_region = region_hash && capture.hash == region_hash;
			if (capture.hash != Any && !matched_region &&
				(capture.hash_mode == "region" || capture.hash != full_hash)) continue;
			if (matched_region) {
				// The Hunting caches may refer to an earlier version. Verify the
				// selected region's bytes before attributing the draw to this hash.
				std::vector<unsigned char> observed;
				if (FAILED(ReadBuffer(context, binding.buffer.Get(), region_offset, region_length, observed)) ||
					crc32c_hw(0, observed.data(), observed.size()) != region_hash)
					continue;
			}
			const auto description = DescribeBuffer(binding, desc, id, full_hash, region_hash, region_offset, region_length);
			if (capture.kind == CaptureKind::Draw) {
				if (buffers.size() > 12000) break;
				if (buffers.size() > 1) buffers += ',';
				buffers += description;
				continue;
			}
			capture.details = DescribeDraw(context, native, call, index, injected) + ",\"buffer\":" + description +
				",\"matched_by\":" + Quote(matched_region ? "hunting_region" :
					(capture.hash == Any ? "selector" : "hunting_resource"));
			UINT read_offset = capture.offset, read_length = capture.length;
			if (capture.indexed_range) {
				const UINT width = binding.format == DXGI_FORMAT_R16_UINT ? 2 :
					(binding.format == DXGI_FORMAT_R32_UINT ? 4 : 0);
				const uint64_t begin = binding.offset + uint64_t(call.FirstIndex) * width;
				const uint64_t length = uint64_t(call.IndexCount) * width;
				capture.details += ",\"index_range\":{\"offset\":" + std::to_string(begin) +
					",\"length\":" + std::to_string(length) + ",\"index_width\":" + std::to_string(width) + "}";
				if (!width || begin >= desc.ByteWidth || length > desc.ByteWidth - begin) {
					capture.details += "}";
					Complete(width ? "range_out_of_bounds" : "unsupported");
					return;
				}
				read_offset = static_cast<UINT>(begin);
				read_length = static_cast<UINT>(length);
			}
			capture.details += ",\"offset\":" + std::to_string(read_offset) + "}";
			ReadBoundBuffer(context, binding, desc.ByteWidth, read_offset, read_length);
			return;
		}
		if (capture.kind == CaptureKind::Draw && (buffers.size() > 1 || (capture.hash == Any && capture.resource == Any))) {
			capture.details = DescribeDraw(context, native, call, index, injected) + ",\"buffers\":" + buffers +
				"],\"bindings_truncated\":" + (buffers.size() > 12000 ? "true" : "false") + "}";
			Complete("ready");
		}
	} catch (const std::exception&) {
		capture.bytes.clear();
		Complete("readback_failed");
	}
}

void RememberShader(ID3D11DeviceChild* shader, uint64_t hash, const wchar_t* stage,
	const void* bytes, size_t size, const wchar_t* name)
{
	if (!shader || !bytes || !size || size > BlobLimit) return;
	try {
		const auto first = static_cast<const unsigned char*>(bytes);
		const std::wstring label = name ? name : L"";
		uint64_t id = 0;
		{
			std::lock_guard lock(shader_lock);
			const auto existing = std::find_if(shaders.rbegin(), shaders.rend(), [&](const auto& item) {
				return item.hash == hash && item.stage == stage && item.name == label &&
					item.bytes.size() == size && std::equal(item.bytes.begin(), item.bytes.end(), first);
			});
			if (existing != shaders.rend()) {
				id = existing->id;
			} else {
				while (!shaders.empty() && (shaders.size() >= 512 || shader_bytes + size > BlobLimit)) {
					shader_bytes -= shaders.front().bytes.size();
					shaders.pop_front();
				}
				id = next_id.fetch_add(1, std::memory_order_relaxed);
				shaders.push_back({id, hash, stage, label, {first, first + size}});
				shader_bytes += size;
			}
		}
		shader->SetPrivateData(ShaderRecordGuid, sizeof(id), &id);
	} catch (const std::exception&) {
		// Shader creation has already succeeded. A failed optional cache must not affect the caller.
	}
}
}
