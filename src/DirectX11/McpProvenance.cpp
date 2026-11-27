#include "McpProvenance.h"
#include "McpAgent.h"
#include "McpApmiProducer.h"
#include "HackerContext.h"
#include "HackerDevice.h"
#include "ResourceHash.h"
#include "DrawCallInfo.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace Dx11Mcp::Provenance {
std::atomic<bool> Armed{false};

namespace {
constexpr size_t EventLimit = 8192;
constexpr size_t ContextLimit = 64;
constexpr size_t ByteBudget = 4 * 1024 * 1024;
constexpr UINT HashLimit = 16 * 1024 * 1024;
constexpr size_t HashBudget = 128 * 1024 * 1024;
constexpr UINT SampleLimit = 60 * 1024;
constexpr UINT CallerFrameLimit = 32;
constexpr UINT CallerUnwindLimit = 48;
constexpr size_t CallerEventLimit = 128;
const GUID BufferGuid = {0xeaec831c, 0xbb21, 0x4d21, {0x89, 0x4f, 0xff, 0xa1, 0x48, 0xb9, 0x30, 0x66}};
const GUID CreationGuid = {0xd89b2018, 0x80cc, 0x4d4b, {0x9d, 0x5d, 0x03, 0x70, 0xa7, 0xc1, 0xf9, 0xe7}};
const GUID ListGuid = {0x60730876, 0x4302, 0x4892, {0xaf, 0xd3, 0xe3, 0xd5, 0x26, 0xa0, 0x13, 0xe9}};
const GUID CsHashGuid = {0xd6facc1a, 0xc26a, 0x4df2, {0x9c, 0x65, 0xe1, 0x2e, 0x15, 0xf1, 0x39, 0xa2}};
std::mutex identity_lock, trace_lock;
std::atomic<uint64_t> next_record{1};
std::atomic<unsigned> observed_frame{0};
uint64_t next_buffer = 1, next_capture = 1;

template<class T>
bool GetPrivate(ID3D11DeviceChild* object, const GUID& guid, T& value)
{
    UINT size = sizeof(T);
    return object && SUCCEEDED(object->GetPrivateData(guid, &size, &value)) && size == sizeof(T);
}

struct Stamp {
    uint64_t record = 0;
    unsigned frame = 0;
    DWORD thread = 0;
    int64_t qpc = 0;
};

Stamp Record()
{
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    return {next_record.fetch_add(1, std::memory_order_relaxed),
        observed_frame.load(std::memory_order_relaxed), GetCurrentThreadId(), qpc.QuadPart};
}

struct Creation {
    Stamp stamp;
    UINT initial_bytes = 0, requested_bytes = 0;
    uint32_t crc = 0;
};

struct Buffer {
    uint64_t id = 0;
    uint32_t hash = 0;
    D3D11_BUFFER_DESC desc{};
    Creation creation;
};

Buffer Describe(ID3D11Resource* resource)
{
    Buffer result;
    if (!resource) return result;
    D3D11_RESOURCE_DIMENSION dimension;
    resource->GetType(&dimension);
    if (dimension != D3D11_RESOURCE_DIMENSION_BUFFER) return result;
    auto* buffer = static_cast<ID3D11Buffer*>(resource);
    result.id = BufferId(buffer);
    result.hash = GetOrigResourceHash(resource);
    buffer->GetDesc(&result.desc);
    GetPrivate(buffer, CreationGuid, result.creation);
    return result;
}

struct Range {
    UINT begin = 0, end = 0;
    UINT Size() const { return end - begin; }
};

enum class Role { Index, Vertex, Srv, Uav };
const char* RoleName(Role role)
{
    switch (role) {
    case Role::Index: return "ib";
    case Role::Vertex: return "vb0";
    case Role::Srv: return "cs_srv";
    case Role::Uav: return "cs_uav";
    }
    return "";
}

struct Binding {
    Buffer buffer;
    Range range;
    Role role = Role::Index;
    UINT slot = 0, stride = 0, first_element = 0, elements = 0, view_flags = 0, offset = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool range_known = false;
};

Binding Input(ID3D11Buffer* buffer, Role role, DXGI_FORMAT format, UINT offset, UINT stride)
{
    Binding result{Describe(buffer), {}, role};
    result.format = format;
    result.stride = stride;
    result.offset = offset;
    if (result.buffer.id && offset < result.buffer.desc.ByteWidth) {
        result.range = {offset, result.buffer.desc.ByteWidth};
        result.range_known = true;
    }
    return result;
}

template<class View>
Binding ViewBinding(View* view, Role role, UINT slot)
{
    Binding result{};
    result.role = role;
    result.slot = slot;
    if (!view) return result;
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    result.buffer = Describe(resource.Get());
    if (!result.buffer.id) return result;
    if constexpr (std::is_same_v<View, ID3D11ShaderResourceView>) {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
        view->GetDesc(&desc);
        result.format = desc.Format;
        if (desc.ViewDimension == D3D11_SRV_DIMENSION_BUFFER) {
            result.first_element = desc.Buffer.FirstElement;
            result.elements = desc.Buffer.NumElements;
        } else if (desc.ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX) {
            result.first_element = desc.BufferEx.FirstElement;
            result.elements = desc.BufferEx.NumElements;
            result.view_flags = desc.BufferEx.Flags;
        }
    } else {
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
        view->GetDesc(&desc);
        result.format = desc.Format;
        if (desc.ViewDimension == D3D11_UAV_DIMENSION_BUFFER) {
            result.first_element = desc.Buffer.FirstElement;
            result.elements = desc.Buffer.NumElements;
            result.view_flags = desc.Buffer.Flags;
        }
    }
    const bool raw = result.view_flags & 1;
    result.stride = raw ? 4 : result.format == DXGI_FORMAT_UNKNOWN
        ? result.buffer.desc.StructureByteStride : dxgi_format_size(result.format);
    const uint64_t begin = uint64_t(result.first_element) * result.stride;
    const uint64_t end = begin + uint64_t(result.elements) * result.stride;
    result.range_known = begin < end && end <= result.buffer.desc.ByteWidth;
    if (result.range_known) {
        result.offset = static_cast<UINT>(begin);
        result.range = {result.offset, static_cast<UINT>(end)};
    }
    return result;
}

enum class Kind { Create, Update, Map, Unmap, Copy, Discard, Dispatch, Draw, Gap };
struct CallerFrame {
    std::array<char, 128> module{};
    uintptr_t rva = 0;
    USHORT stack_index = 0;
};

struct CallerStack {
    std::vector<CallerFrame> frames;
    USHORT unwound = 0, unresolved = 0;
    bool native_limit = false, frame_limit = false;
};

struct Event {
    Kind kind = Kind::Gap;
    const char* api = "unobserved_command_list";
    Stamp stamp, map_stamp;
    uint64_t context = 0, shader = 0, map_record = 0;
    Buffer destination, source;
    Range written, source_range;
    std::vector<Binding> bindings;
    std::shared_ptr<std::vector<char>> sample;
    uint32_t crc = 0;
    UINT flags = 0, first_index = 0, index_count = 0, cpu_bytes = 0;
    std::array<UINT, 3> groups{};
    const char* cpu = "not_applicable";
    const char* sample_state = "not_requested";
    std::array<char, 128> caller{};
    uintptr_t caller_offset = 0;
    std::shared_ptr<const CallerStack> caller_stack;
    ApmiProducer::Upload unity_producer;
    const char* caller_frames_state = "not_applicable";
    bool range_known = true, indirect = false, injected = false, deferred = false;
    bool discard = false;
};

struct Submission {
    Event event;
    unsigned frame;
    uint64_t execution = 0, list = 0, submit_context = 0;
    size_t list_order = 0;
    DWORD thread = 0;
    int64_t qpc = 0;
};

struct Mapping {
    const void* data;
    Stamp stamp;
    D3D11_MAP type;
};

struct DeferredBindings {
    std::array<Binding, 3> srv{};
    std::array<Binding, D3D11_1_UAV_SLOT_COUNT> uav{};
    Binding index, vertex;
    uint64_t shader = 0;
};

struct Context {
    uint64_t id = 0;
    bool finished = false, lost = false;
    std::unique_ptr<DeferredBindings> bindings;
    std::vector<Event> pending;
    std::unordered_map<uint64_t, Mapping> mappings;
};

struct ListToken { uint64_t capture, id; };
struct Journal {
    std::vector<Event> events;
    bool lost;
};

struct Selector {
    uint64_t resource = 0, shader = 0, sample_resource = 0;
    uint32_t ib = 0, uav = 0;
    UINT sample_bytes = 0;
    uint64_t first_index = UINT64_MAX, index_count = UINT64_MAX;
    uint64_t caller_resource = 0;
    UINT caller_frames = 0;
    UINT producer_frames = 0;
    std::vector<uint32_t> ib_hashes;
};

struct Trace {
    uint64_t id = 0, next_list = 1, next_execution = 1;
    HackerDevice* device = nullptr;
    HackerContext* presenter = nullptr;
    Selector selector;
    UINT frames_left = 0, start_frame = 0, end_frame = 0, frame = 0;
    const char* state = "idle";
    size_t dropped = 0, records = 0, gaps = 0, samples = 0, hashed = 0;
    size_t dispatches = 0, draws = 0, cpu_uploads = 0;
    size_t caller_attempts = 0, caller_limited = 0;
    ApmiProducer::Totals producer_totals;
    std::unordered_map<HackerContext*, Context> contexts;
    std::unordered_map<uint64_t, Journal> journals;
    std::vector<Submission> submitted;
    std::vector<size_t> matches;
} trace;

Context* UseContext(HackerContext* context)
{
    auto it = trace.contexts.find(context);
    if (it != trace.contexts.end()) return &it->second;
    if (trace.contexts.size() >= ContextLimit) {
        ++trace.dropped;
        return nullptr;
    }
    auto& active = trace.contexts[context];
    active.id = next_record.fetch_add(1, std::memory_order_relaxed);
    return &active;
}

template<class Fn>
void Observe(HackerContext* context, Fn&& fn) noexcept
{
    if (!Armed.load(std::memory_order_acquire) || !context) return;
    std::lock_guard lock(trace_lock);
    if (!Armed.load(std::memory_order_relaxed) || trace.device != context->GetHackerDevice()) return;
    Context* active = nullptr;
    try {
        active = UseContext(context);
        if (active) fn(*active);
    } catch (...) {
        ++trace.dropped;
        if (active) active->lost = true;
    }
}

bool IsMatch(const Event& event)
{
    const auto& selector = trace.selector;
    if (selector.ib || !selector.ib_hashes.empty()) {
        if (event.kind != Kind::Draw) return false;
        if (selector.first_index != UINT64_MAX && (event.indirect || event.first_index != selector.first_index))
            return false;
        if (selector.index_count != UINT64_MAX && (event.indirect || event.index_count != selector.index_count))
            return false;
        if (event.bindings.empty() || event.bindings.front().role != Role::Index) return false;
        const auto hash = event.bindings.front().buffer.hash;
        return selector.ib ? hash == selector.ib :
            std::find(selector.ib_hashes.begin(), selector.ib_hashes.end(), hash) != selector.ib_hashes.end();
    }
    if (selector.shader && (event.kind != Kind::Dispatch || event.shader != selector.shader)) return false;
    if (selector.uav && (event.kind != Kind::Dispatch ||
        std::none_of(event.bindings.begin(), event.bindings.end(), [&](const Binding& binding) {
            return binding.role == Role::Uav && binding.buffer.hash == selector.uav;
        }))) return false;
    if (!selector.resource) return selector.shader || selector.uav;
    return event.destination.id == selector.resource || event.source.id == selector.resource ||
        std::any_of(event.bindings.begin(), event.bindings.end(), [&](const Binding& binding) {
            return binding.buffer.id == selector.resource;
        });
}

void Submit(Event event, uint64_t submit_context, uint64_t execution = 0, uint64_t list = 0,
    size_t order = 0, Stamp execution_stamp = {})
{
    if (trace.submitted.size() == EventLimit) {
        ++trace.dropped;
        return;
    }
    const bool match = IsMatch(event);
    const bool gap = event.kind == Kind::Gap;
    const auto thread = execution_stamp.record ? execution_stamp.thread : event.stamp.thread;
    const auto qpc = execution_stamp.record ? execution_stamp.qpc : event.stamp.qpc;
    trace.submitted.push_back({std::move(event), trace.frame, execution, list, submit_context, order, thread, qpc});
    if (gap) ++trace.gaps;
    const auto& submitted = trace.submitted.back().event;
    if (submitted.kind == Kind::Dispatch) ++trace.dispatches;
    if (submitted.kind == Kind::Draw) ++trace.draws;
    if (submitted.cpu_bytes) ++trace.cpu_uploads;
    if (match) trace.matches.push_back(trace.submitted.size() - 1);
}

void Add(HackerContext* context, Context& active, Event event)
{
    if (trace.records >= EventLimit) {
        ++trace.dropped;
        active.lost = true;
        return;
    }
    ++trace.records;
    if (!event.stamp.record) event.stamp = Record();
    event.context = active.id;
    event.deferred = context->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED;
    if (event.deferred) active.pending.push_back(std::move(event));
    else Submit(std::move(event), active.id);
}

bool ModuleName(HMODULE module, std::array<char, 128>& result) noexcept
{
    wchar_t path[MAX_PATH];
    const auto length = GetModuleFileNameW(module, path, _countof(path));
    if (!length || length >= _countof(path)) return false;
    const auto* basename = wcsrchr(path, L'\\');
    return WideCharToMultiByte(CP_UTF8, 0, basename ? basename + 1 : path, -1, result.data(),
        static_cast<int>(result.size()), nullptr, nullptr) != 0;
}

// Capture the recording thread's native return addresses while the upload
// caller is still on the stack, including deferred uploads.
__declspec(noinline) void Caller(Event& event, bool has_upload) noexcept
{
    const bool selected = has_upload && trace.selector.caller_resource == event.destination.id;
    const bool wants_stack = trace.selector.caller_frames && selected;
    std::shared_ptr<CallerStack> captured;
    event.caller_frames_state = !has_upload ? "not_applicable" :
        !trace.selector.caller_frames ? "disabled" : "resource_not_selected";
    if (wants_stack) {
        if (trace.caller_attempts == CallerEventLimit) {
            ++trace.caller_limited;
            event.caller_frames_state = "event_limit";
        } else {
            ++trace.caller_attempts;
            try {
                captured = std::make_shared<CallerStack>();
                captured->frames.reserve(trace.selector.caller_frames);
            } catch (const std::bad_alloc&) {
                captured.reset();
                event.caller_frames_state = "allocation_failed";
            }
        }
    }

    void* stack[CallerUnwindLimit];
    const auto count = CaptureStackBackTrace(1, captured ? CallerUnwindLimit : 16, stack, nullptr);
    HMODULE last_module = nullptr;
    std::array<char, 128> module_name{};
    for (USHORT i = 0; i < count; ++i) {
        HMODULE module = nullptr;
        CallerFrame frame;
        frame.stack_index = i;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(stack[i]), &module))
            module = nullptr;
        if (module && module == migoto_handle)
            continue;
        if (module && module != last_module) {
            last_module = module;
            module_name.fill(0);
            if (!ModuleName(module, module_name)) module_name.fill(0);
        }
        if (module && module_name[0]) {
            frame.module = module_name;
            frame.rva = reinterpret_cast<uintptr_t>(stack[i]) - reinterpret_cast<uintptr_t>(module);
            if (!event.caller[0]) {
                event.caller = frame.module;
                event.caller_offset = frame.rva;
            }
        } else if (captured) {
            ++captured->unresolved;
        }
        if (captured) {
            if (captured->frames.size() < trace.selector.caller_frames)
                captured->frames.push_back(frame);
            else
                captured->frame_limit = true;
        } else if (event.caller[0]) {
            break;
        }
    }
    if (!captured) return;
    captured->unwound = count;
    captured->native_limit = count == CallerUnwindLimit;
    event.caller_frames_state = !count ? "unwind_empty" : captured->frames.empty() ? "no_external_frames" :
        (captured->native_limit || captured->frame_limit || captured->unresolved) ? "partial" : "captured";
    event.caller_stack = std::move(captured);
}

bool ReadCpu(const void* bytes, UINT size, uint32_t* crc, void* sample, size_t length) noexcept
{
    __try {
        if (crc) *crc = crc32c_hw(0, bytes, size);
        if (length) memcpy(sample, bytes, length);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
        GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

void Collect(Event& event, const void* data, UINT size, bool hash)
{
    if (!data || !size) {
        if (hash) event.cpu = "no_cpu_source";
        return;
    }
    event.cpu_bytes = size;
    if (hash) {
        if (size > HashLimit) event.cpu = "hash_event_limit";
        else if (size > HashBudget - trace.hashed) event.cpu = "hash_capture_limit";
        else {
            trace.hashed += size;
            event.cpu = ReadCpu(data, size, &event.crc, nullptr, 0) ? "hashed" : "cpu_read_failed";
        }
    }
    if (!trace.selector.sample_bytes || trace.selector.sample_resource != event.destination.id) return;
    const auto length = (std::min)({size_t(size), size_t(trace.selector.sample_bytes), ByteBudget - trace.samples});
    if (!length) {
        event.sample_state = "sample_capture_limit";
        return;
    }
    auto bytes = std::make_shared<std::vector<char>>(length);
    if (!ReadCpu(data, size, nullptr, bytes->data(), length)) {
        event.sample_state = "cpu_read_failed";
        return;
    }
    event.sample_state = length == size ? "saved" : "sample_truncated";
    event.sample = std::move(bytes);
    trace.samples += length;
}

bool BoxRange(const D3D11_BOX* box, UINT bytes, Range& range)
{
    if (box && (box->left >= box->right || box->right > bytes || box->top || box->bottom != 1 ||
        box->front || box->back != 1)) return false;
    range = box ? Range{box->left, box->right} : Range{0, bytes};
    return range.Size() != 0;
}

void Complete(const char* state) noexcept
{
    Armed.store(false, std::memory_order_release);
    trace.end_frame = trace.frame;
    trace.frames_left = 0;
    if (trace.selector.producer_frames) trace.producer_totals = ApmiProducer::End();
    trace.state = state;
    trace.contexts.clear();
    trace.journals.clear();
}

Binding DrawIndex(Binding binding, const DrawCallInfo& call)
{
    if (!binding.range_known || !call.IndexCount || !binding.stride) {
        binding.range_known = false;
        return binding;
    }
    const uint64_t first = uint64_t(binding.range.begin) + uint64_t(call.FirstIndex) * binding.stride;
    const uint64_t end = first + uint64_t(call.IndexCount) * binding.stride;
    binding.range_known = end <= binding.range.end;
    if (binding.range_known) binding.range = {static_cast<UINT>(first), static_cast<UINT>(end)};
    return binding;
}
} // namespace

uint64_t BufferId(ID3D11Buffer* buffer)
{
    std::lock_guard lock(identity_lock);
    uint64_t id = 0;
    if (GetPrivate(buffer, BufferGuid, id)) return id;
    id = next_buffer++;
    return SUCCEEDED(buffer->SetPrivateData(BufferGuid, sizeof(id), &id)) ? id : 0;
}

void RegisterShader(ID3D11DeviceChild* shader, uint64_t hash) noexcept
{
    shader->SetPrivateData(CsHashGuid, sizeof(hash), &hash);
}

void OnPresent(HackerDevice* device, HackerContext* wrapper, unsigned frame) noexcept
{
    // Present closes the preceding frame. Subsequent API calls belong to
    // the next G->frame_no, including deferred calls on worker threads.
    observed_frame.store(frame + 1, std::memory_order_relaxed);
    if (!Armed.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(trace_lock);
    if (trace.device != device || trace.presenter != wrapper || !Armed.load(std::memory_order_relaxed)) return;
    if (!--trace.frames_left)
        Complete(trace.matches.empty() ? "no_match" : "ready");
    else
        trace.frame = frame + 1;
}

void OnDisable() noexcept
{
    std::lock_guard lock(trace_lock);
    if (Armed.load(std::memory_order_relaxed) && trace.selector.producer_frames) ApmiProducer::End();
    Armed.store(false, std::memory_order_release);
    trace = {};
}

void OnContextDestroyed(HackerContext* context) noexcept
{
    if (!Armed.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(trace_lock);
    trace.contexts.erase(context);
    if (trace.presenter == context) Complete("device_changed");
}

void Create(HackerDevice* device, ID3D11Buffer* buffer, const D3D11_SUBRESOURCE_DATA* initial,
    UINT input_size, uint32_t input_crc32c) noexcept
{
    // The original CreateBuffer path already hashes the caller's bytes.
    // Store that immutable observation on the resource for later sessions.
    const void* data = initial ? initial->pSysMem : nullptr;
    const Creation creation{Record(), data ? input_size : 0, input_size, input_crc32c};
    buffer->SetPrivateData(CreationGuid, sizeof(creation), &creation);
    if (!Armed.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(trace_lock);
    if (trace.device != device || !Armed.load(std::memory_order_relaxed)) return;
    try {
        Event event;
        event.kind = Kind::Create;
        event.api = "CreateBuffer";
        event.stamp = creation.stamp;
        event.destination = Describe(buffer);
        event.written = {0, event.destination.desc.ByteWidth};
        event.range_known = creation.initial_bytes == event.written.end;
        event.cpu = !data ? "no_initial_data" : event.range_known ? "hashed" : "creation_size_overridden";
        event.crc = creation.crc;
        Caller(event, data && creation.initial_bytes);
        Collect(event, data, creation.initial_bytes, false);
        Submit(std::move(event), 0);
    } catch (...) {
        ++trace.dropped;
    }
}

void Update(HackerContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box,
    const void* data, bool version1, UINT flags) noexcept
{
    if (subresource) return;
    Observe(context, [&](Context& active) {
        Event event;
        event.destination = Describe(resource);
        if (!event.destination.id || !BoxRange(box, event.destination.desc.ByteWidth, event.written)) return;
        event.kind = Kind::Update;
        event.api = version1 ? "UpdateSubresource1" : "UpdateSubresource";
        event.stamp = Record();
        event.flags = flags;
        event.discard = (flags & D3D11_COPY_DISCARD) != 0;
        Caller(event, data != nullptr);
        const auto requested = event.destination.creation.requested_bytes;
        if (requested && event.written.end > requested) {
            event.cpu = "destination_exceeds_original_buffer_size";
            data = nullptr;
        }
        // On runtimes emulating command lists, legacy boxed updates may
        // advance pSrcData by the box origin. Do not guess their source.
        if (data && !version1 && box && box->left && context->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED) {
            ComPtr<ID3D11Device> device;
            resource->GetDevice(&device);
            D3D11_FEATURE_DATA_THREADING caps{};
            if (FAILED(device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &caps, sizeof(caps))) ||
                !caps.DriverCommandLists) {
                event.cpu = "legacy_deferred_box_source_uncertain";
                data = nullptr;
            }
        }
        if (data) Collect(event, data, event.written.Size(), true);
        else if (event.cpu == std::string_view("not_applicable")) event.cpu = "no_cpu_source";
        if (trace.selector.producer_frames)
            event.unity_producer = ApmiProducer::ObserveUpload(trace.id, data, event.written.Size());
        Add(context, active, std::move(event));
    });
}

void Map(HackerContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type,
    const D3D11_MAPPED_SUBRESOURCE& mapping) noexcept
{
    if (subresource || type == D3D11_MAP_READ || !mapping.pData) return;
    Observe(context, [&](Context& active) {
        Event event;
        event.destination = Describe(resource);
        if (!event.destination.id) return;
        if (active.mappings.size() >= 64) {
            ++trace.dropped;
            return;
        }
        event.kind = Kind::Map;
        event.api = "Map";
        event.flags = type;
        event.stamp = Record();
        active.mappings.insert_or_assign(event.destination.id, Mapping{mapping.pData, event.stamp, type});
        // Map is a CPU recording observation. Only Unmap publishes a
        // possible write to the execution history.
        event.context = active.id;
        event.deferred = context->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED;
        if (event.deferred) return;
        Submit(std::move(event), active.id);
    });
}

void Unmap(HackerContext* context, ID3D11Resource* resource, UINT subresource) noexcept
{
    if (subresource) return;
    Observe(context, [&](Context& active) {
        Event event;
        event.destination = Describe(resource);
        if (!event.destination.id) return;
        const auto entry = active.mappings.find(event.destination.id);
        if (entry == active.mappings.end()) return;
        const auto mapping = entry->second;
        active.mappings.erase(entry);
        event.kind = Kind::Unmap;
        event.api = "Unmap";
        event.stamp = Record();
        event.flags = mapping.type;
        event.map_record = mapping.stamp.record;
        event.map_stamp = mapping.stamp;
        event.discard = mapping.type == D3D11_MAP_WRITE_DISCARD;
        // D3D11 supplies no dirty range. Hash the mapped snapshot and keep
        // its size separate from the unknown set of bytes actually written.
        event.written = {0, event.destination.desc.ByteWidth};
        event.range_known = false;
        Caller(event, mapping.data != nullptr);
        Collect(event, mapping.data, event.written.Size(), true);
        Add(context, active, std::move(event));
    });
}

void Copy(HackerContext* context, ID3D11Resource* dst, UINT dst_subresource, UINT x, UINT y, UINT z,
    ID3D11Resource* src, UINT src_subresource, const D3D11_BOX* box, const char* api, UINT flags) noexcept
{
    if (dst_subresource || src_subresource || y || z) return;
    Observe(context, [&](Context& active) {
        Event event;
        event.source = Describe(src);
        event.destination = Describe(dst);
        if (!event.source.id || !event.destination.id ||
            !BoxRange(box, event.source.desc.ByteWidth, event.source_range) ||
            uint64_t(x) + event.source_range.Size() > event.destination.desc.ByteWidth) return;
        if (std::string_view(api) == "CopyResource" &&
            event.source.desc.ByteWidth != event.destination.desc.ByteWidth) return;
        event.kind = Kind::Copy;
        event.api = api;
        event.flags = flags;
        event.discard = (flags & D3D11_COPY_DISCARD) != 0;
        event.written = {x, x + event.source_range.Size()};
        Add(context, active, std::move(event));
    });
}

void Discard(HackerContext* context, ID3D11Resource* resource) noexcept
{
    Observe(context, [&](Context& active) {
        Event event;
        event.destination = Describe(resource);
        if (!event.destination.id) return;
        event.kind = Kind::Discard;
        event.api = "DiscardResource";
        event.written = {0, event.destination.desc.ByteWidth};
        event.discard = true;
        Add(context, active, std::move(event));
    });
}

void ClearUav(HackerContext* context, ID3D11UnorderedAccessView* view, const char* api) noexcept
{
    Observe(context, [&](Context& active) {
        const auto binding = ViewBinding(view, Role::Uav, 0);
        if (!binding.buffer.id) return;
        Event event;
        event.destination = binding.buffer;
        event.kind = Kind::Update;
        event.api = api;
        event.written = binding.range_known ? binding.range : Range{0, binding.buffer.desc.ByteWidth};
        event.range_known = binding.range_known;
        Add(context, active, std::move(event));
    });
}

void CopyCount(HackerContext* context, ID3D11Buffer* dst, UINT offset, ID3D11UnorderedAccessView* view) noexcept
{
    Observe(context, [&](Context& active) {
        Event event;
        event.destination = Describe(dst);
        if (!event.destination.id || uint64_t(offset) + 4 > event.destination.desc.ByteWidth) return;
        event.kind = Kind::Update;
        event.api = "CopyStructureCount";
        event.written = {offset, offset + 4};
        event.source = ViewBinding(view, Role::Uav, 0).buffer;
        event.cpu = "gpu_uav_counter";
        Add(context, active, std::move(event));
    });
}

void Finish(HackerContext* context, ID3D11CommandList* list, bool restore) noexcept
{
    Observe(context, [&](Context& active) {
        if (!list) {
            active.pending.clear();
            active.lost = true;
        } else if (trace.journals.size() < EventLimit) {
            const ListToken token{trace.id, trace.next_list++};
            trace.journals.emplace(token.id, Journal{std::move(active.pending), !active.finished || active.lost});
            if (FAILED(list->SetPrivateData(ListGuid, sizeof(token), &token))) ++trace.dropped;
            active.pending.clear();
            active.finished = true;
            active.lost = false;
        } else {
            active.pending.clear();
            ++trace.dropped;
        }
        if (!restore) active.bindings.reset();
    });
}

void Execute(HackerContext* context, ID3D11CommandList* list) noexcept
{
    Observe(context, [&](Context& active) {
        ListToken token{};
        const bool tagged = GetPrivate(list, ListGuid, token) && token.capture == trace.id;
        const auto entry = tagged ? trace.journals.find(token.id) : trace.journals.end();
        const auto execution = trace.next_execution++;
        const auto stamp = Record();
        if (entry == trace.journals.end() || entry->second.lost) {
            Event gap;
            gap.api = entry == trace.journals.end() ? "unobserved_command_list" : "incomplete_command_list_recording";
            gap.stamp = stamp;
            gap.context = active.id;
            Submit(std::move(gap), active.id, execution, tagged ? token.id : 0);
        }
        if (entry == trace.journals.end()) return;
        size_t order = 0;
        for (const auto& event : entry->second.events)
            Submit(event, active.id, execution, token.id, ++order, stamp);
    });
}
namespace {
template<class Fn>
void SetDeferred(HackerContext* context, Fn&& fn) noexcept
{
    if (context->GetType() != D3D11_DEVICE_CONTEXT_DEFERRED) return;
    Observe(context, [&](Context& active) {
        if (!active.bindings) active.bindings = std::make_unique<DeferredBindings>();
        fn(*active.bindings);
    });
}

bool Overlaps(const Binding& a, const Binding& b)
{
    return a.buffer.id && a.buffer.id == b.buffer.id &&
        (!a.range_known || !b.range_known || (a.range.begin < b.range.end && b.range.begin < a.range.end));
}

void ReadImmediate(HackerContext* context, const DrawCallInfo& call, Event& event)
{
    auto* native = context->GetPassThroughOrigContext1();
    if (event.kind == Kind::Dispatch) {
        ComPtr<ID3D11ComputeShader> shader;
        native->CSGetShader(&shader, nullptr, nullptr);
        GetPrivate(shader.Get(), CsHashGuid, event.shader);

        ID3D11ShaderResourceView* views[3]{};
        native->CSGetShaderResources(0, _countof(views), views);
        for (UINT slot = 0; slot < _countof(views); ++slot) {
            ComPtr<ID3D11ShaderResourceView> view;
            view.Attach(views[slot]);
            event.bindings.push_back(ViewBinding(view.Get(), Role::Srv, slot));
        }

        ComPtr<ID3D11Device> device;
        native->GetDevice(&device);
        ID3D11UnorderedAccessView* outputs[D3D11_1_UAV_SLOT_COUNT]{};
        const UINT count = device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1
            ? _countof(outputs) : D3D11_PS_CS_UAV_REGISTER_COUNT;
        native->CSGetUnorderedAccessViews(0, count, outputs);
        for (UINT slot = 0; slot < count; ++slot) {
            ComPtr<ID3D11UnorderedAccessView> view;
            view.Attach(outputs[slot]);
            if (view) {
                auto binding = ViewBinding(view.Get(), Role::Uav, slot);
                if (binding.buffer.id) event.bindings.push_back(std::move(binding));
            }
        }
        return;
    }

    ComPtr<ID3D11Buffer> ib, vb;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT offset = 0, stride = 0;
    native->IAGetIndexBuffer(&ib, &format, &offset);
    const UINT index_stride = format == DXGI_FORMAT_R32_UINT ? 4 : format == DXGI_FORMAT_R16_UINT ? 2 : 0;
    event.bindings.push_back(DrawIndex(Input(ib.Get(), Role::Index, format, offset, index_stride), call));
    native->IAGetVertexBuffers(0, 1, &vb, &stride, &offset);
    event.bindings.push_back(Input(vb.Get(), Role::Vertex, DXGI_FORMAT_UNKNOWN, offset, stride));
}

void ReadDeferred(const DeferredBindings* bindings, const DrawCallInfo& call, Event& event)
{
    static const DeferredBindings empty;
    if (!bindings) bindings = &empty;
    if (event.kind == Kind::Dispatch) {
        event.shader = bindings->shader;
        for (UINT slot = 0; slot < bindings->srv.size(); ++slot) {
            auto binding = bindings->srv[slot];
            binding.role = Role::Srv;
            binding.slot = slot;
            event.bindings.push_back(binding);
        }
        for (const auto& binding : bindings->uav)
            if (binding.buffer.id) event.bindings.push_back(binding);
    } else {
        event.bindings.push_back(DrawIndex(bindings->index, call));
        auto vertex = bindings->vertex;
        vertex.role = Role::Vertex;
        event.bindings.push_back(vertex);
    }
}
} // namespace

void DeferredSrvs(HackerContext* context, UINT start, UINT count, ID3D11ShaderResourceView* const* views) noexcept
{
    if (start >= 3) return;
    SetDeferred(context, [&](DeferredBindings& bindings) {
        for (UINT i = 0; i < count && i < bindings.srv.size() - start; ++i) {
            auto binding = ViewBinding(views ? views[i] : nullptr, Role::Srv, start + i);
            if (std::any_of(bindings.uav.begin(), bindings.uav.end(), [&](const Binding& uav) { return Overlaps(uav, binding); }))
                binding.buffer = {};
            bindings.srv[start + i] = binding;
        }
    });
}

void DeferredUavs(HackerContext* context, UINT start, UINT count, ID3D11UnorderedAccessView* const* views) noexcept
{
    if (start >= D3D11_1_UAV_SLOT_COUNT) return;
    SetDeferred(context, [&](DeferredBindings& bindings) {
        for (UINT i = 0; i < count && i < bindings.uav.size() - start; ++i) {
            auto binding = ViewBinding(views ? views[i] : nullptr, Role::Uav, start + i);
            for (auto& srv : bindings.srv)
                if (Overlaps(srv, binding)) srv = {};
            bindings.uav[start + i] = binding;
        }
    });
}

void DeferredIndex(HackerContext* context, ID3D11Buffer* buffer, DXGI_FORMAT format, UINT offset) noexcept
{
    SetDeferred(context, [&](DeferredBindings& bindings) {
        const UINT stride = format == DXGI_FORMAT_R32_UINT ? 4 : format == DXGI_FORMAT_R16_UINT ? 2 : 0;
        bindings.index = Input(buffer, Role::Index, format, offset, stride);
    });
}

void DeferredVertex(HackerContext* context, UINT start, UINT count, ID3D11Buffer* const* buffers,
    const UINT* strides, const UINT* offsets) noexcept
{
    if (start || !count) return;
    SetDeferred(context, [&](DeferredBindings& bindings) {
        bindings.vertex = Input(buffers ? buffers[0] : nullptr, Role::Vertex, DXGI_FORMAT_UNKNOWN,
            offsets ? offsets[0] : 0, strides ? strides[0] : 0);
    });
}

void DeferredShader(HackerContext* context, ID3D11ComputeShader* shader) noexcept
{
    SetDeferred(context, [&](DeferredBindings& bindings) {
        bindings.shader = 0;
        GetPrivate(shader, CsHashGuid, bindings.shader);
    });
}

void DeferredClear(HackerContext* context) noexcept
{
    Observe(context, [&](Context& active) { active.bindings.reset(); });
}

void Draw(HackerContext* context, const DrawCallInfo& call, bool injected) noexcept
{
    const bool dispatch = call.type == DrawCall::Dispatch || call.type == DrawCall::DispatchIndirect;
    const bool indirect = call.type == DrawCall::DispatchIndirect || call.type == DrawCall::DrawIndexedInstancedIndirect;
    if (!dispatch && call.type != DrawCall::DrawIndexed && call.type != DrawCall::DrawIndexedInstanced &&
        call.type != DrawCall::DrawIndexedInstancedIndirect) return;
    if (dispatch && !indirect && (!call.ThreadGroupCountX || !call.ThreadGroupCountY || !call.ThreadGroupCountZ)) return;
    if (!dispatch && !indirect && (!call.IndexCount || (call.type == DrawCall::DrawIndexedInstanced && !call.InstanceCount))) return;
    Observe(context, [&](Context& active) {
        Event event;
        event.kind = dispatch ? Kind::Dispatch : Kind::Draw;
        event.api = dispatch ? (indirect ? "DispatchIndirect" : "Dispatch") : (indirect ? "DrawIndexedInstancedIndirect" :
            call.type == DrawCall::DrawIndexedInstanced ? "DrawIndexedInstanced" : "DrawIndexed");
        event.first_index = call.FirstIndex;
        event.index_count = call.IndexCount;
        event.groups = {call.ThreadGroupCountX, call.ThreadGroupCountY, call.ThreadGroupCountZ};
        event.indirect = indirect;
        event.injected = injected;
        const bool deferred = context->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED;
        if (deferred && injected) {
            event.kind = Kind::Gap;
            event.api = "deferred_injected_bindings_unobserved";
        } else if (deferred) {
            ReadDeferred(active.bindings.get(), call, event);
        } else {
            ReadImmediate(context, call, event);
        }
        Add(context, active, std::move(event));
    });
}

namespace {
std::string Quote(std::string_view value)
{
    std::string result = "\"";
    for (const auto c : value) {
        if (c == '\\' || c == '"') result += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) result += c;
    }
    return result + '"';
}

std::string Hex(uint64_t value, bool wide = false)
{
    char bytes[20];
    sprintf_s(bytes, wide ? "\"%016llx\"" : "\"%08llx\"", static_cast<unsigned long long>(value));
    return bytes;
}

std::string Error(const char* code)
{
    return "{\"error\":" + Quote(code) + ",\"code\":" + Quote(code) + "}";
}

std::string RangeJson(Range range)
{
    return "{\"offset\":" + std::to_string(range.begin) + ",\"bytes\":" + std::to_string(range.Size()) + "}";
}

std::string BufferJson(const Buffer& buffer)
{
    if (!buffer.id) return "null";
    const auto& d = buffer.desc;
    const auto& c = buffer.creation;
    return "{\"resource_id\":" + std::to_string(buffer.id) + ",\"resource_hash\":" +
        (buffer.hash ? Hex(buffer.hash) : "null") + ",\"byte_width\":" + std::to_string(d.ByteWidth) +
        ",\"structure_stride\":" + std::to_string(d.StructureByteStride) +
        ",\"bind_flags\":" + std::to_string(d.BindFlags) + ",\"misc_flags\":" + std::to_string(d.MiscFlags) +
        ",\"usage\":" + std::to_string(d.Usage) + ",\"cpu_access_flags\":" + std::to_string(d.CPUAccessFlags) +
        ",\"creation\":" + (c.stamp.record
            ? "{\"record\":" + std::to_string(c.stamp.record) + ",\"frame\":" + std::to_string(c.stamp.frame) +
                ",\"thread\":" + std::to_string(c.stamp.thread) + ",\"qpc\":" + std::to_string(c.stamp.qpc) +
                ",\"requested_bytes\":" + std::to_string(c.requested_bytes) + ",\"initial_bytes\":" + std::to_string(c.initial_bytes) +
                ",\"initial_crc32c\":" + (c.initial_bytes ? Hex(c.crc) : "null") + "}"
            : "null") + "}";
}

std::string BindingJson(const Binding& binding)
{
    return "{\"role\":" + Quote(RoleName(binding.role)) + ",\"slot\":" + std::to_string(binding.slot) +
        ",\"buffer\":" + BufferJson(binding.buffer) + ",\"range\":" +
        (binding.range_known ? RangeJson(binding.range) : "null") + ",\"format\":" + std::to_string(binding.format) +
        ",\"binding_offset\":" + std::to_string(binding.offset) + ",\"stride\":" + std::to_string(binding.stride) +
        ",\"first_element\":" + std::to_string(binding.first_element) +
        ",\"elements\":" + std::to_string(binding.elements) + ",\"view_flags\":" + std::to_string(binding.view_flags) +
        ",\"range_kind\":" + Quote(binding.role == Role::Vertex ? "vb_available_window" :
            binding.role == Role::Uav ? "uav_view_window" : binding.role == Role::Srv ? "srv_view_window" : "draw_indices") + "}";
}

std::string Summary(size_t index)
{
    const auto& entry = trace.submitted[index];
    const auto& event = entry.event;
    std::string result = "{\"submission\":" + std::to_string(index + 1) + ",\"api\":" + Quote(event.api) +
        ",\"frame\":" + std::to_string(entry.frame) + ",\"record\":" + std::to_string(event.stamp.record) +
        ",\"resource_id\":" + std::to_string(event.destination.id) +
        ",\"cs_hash\":" + (event.shader ? Hex(event.shader, true) : "null");
    if (event.kind == Kind::Draw && !event.bindings.empty()) {
        result += ",\"ib_resource_id\":" + std::to_string(event.bindings.front().buffer.id) +
            ",\"ib_hash\":" + Hex(event.bindings.front().buffer.hash) +
            ",\"first_index\":" + (event.indirect ? "null" : std::to_string(event.first_index)) +
            ",\"index_count\":" + (event.indirect ? "null" : std::to_string(event.index_count));
    }
    return result + "}";
}

std::string EventJson(size_t index)
{
    const auto& entry = trace.submitted[index];
    const auto& event = entry.event;
    auto result = Summary(index);
    result.pop_back();
    result += ",\"context_id\":" + std::to_string(event.context) +
        ",\"submit_context_id\":" + std::to_string(entry.submit_context) +
        ",\"record_frame\":" + std::to_string(event.stamp.frame) +
        ",\"record_thread\":" + std::to_string(event.stamp.thread) +
        ",\"record_qpc\":" + std::to_string(event.stamp.qpc) +
        ",\"submit_thread\":" + std::to_string(entry.thread) +
        ",\"submit_qpc\":" + std::to_string(entry.qpc) +
        ",\"execute_id\":" + std::to_string(entry.execution) + ",\"command_list_id\":" + std::to_string(entry.list) +
        ",\"list_order\":" + std::to_string(entry.list_order) +
        ",\"deferred\":" + (event.deferred ? "true" : "false") +
        ",\"injected\":" + (event.injected ? "true" : "false") +
        ",\"binding_source\":" + Quote(event.deferred ? "deferred_setters" : "immediate_state") +
        ",\"destination\":" + BufferJson(event.destination) + ",\"source\":" + BufferJson(event.source) +
        ",\"destination_range\":" + (event.range_known && event.written.Size() ? RangeJson(event.written) : "null") +
        ",\"source_range\":" + (event.source_range.Size() ? RangeJson(event.source_range) : "null") +
        ",\"map_record\":" + std::to_string(event.map_record) +
        ",\"map_frame\":" + std::to_string(event.map_stamp.frame) +
        ",\"map_thread\":" + std::to_string(event.map_stamp.thread) +
        ",\"map_qpc\":" + std::to_string(event.map_stamp.qpc) +
        ",\"flags\":" + std::to_string(event.flags) +
        ",\"discard\":" + (event.discard ? "true" : "false") +
        ",\"cpu_source_state\":" + Quote(event.cpu) + ",\"cpu_crc32c\":" +
        (std::string_view(event.cpu) == "hashed" ? Hex(event.crc) : "null") +
        ",\"cpu_source_bytes\":" + std::to_string(event.cpu_bytes) +
        ",\"caller_module\":" + (event.caller[0] ? Quote(event.caller.data()) : "null") +
        ",\"caller_offset\":" + (event.caller[0] ? Hex(event.caller_offset, true) : "null") +
        ",\"sample_state\":" + Quote(event.sample_state) +
        ",\"sample_bytes\":" + std::to_string(event.sample ? event.sample->size() : 0) +
        ",\"groups\":" + (event.kind == Kind::Dispatch && !event.indirect
            ? "[" + std::to_string(event.groups[0]) + "," + std::to_string(event.groups[1]) + "," +
                std::to_string(event.groups[2]) + "]" : "null") + ",\"bindings\":[";
    bool first = true;
    for (const auto& binding : event.bindings) {
        if (!first) result += ',';
        first = false;
        result += BindingJson(binding);
    }
    result += "],\"unity_producer\":";
    if (trace.selector.producer_frames && event.kind == Kind::Update) {
        const auto& producer = event.unity_producer;
        result += "{\"state\":" + Quote(producer.state) +
            ",\"consumer_qpc\":" + std::to_string(producer.consumer_qpc) +
            ",\"consumer_thread\":" + std::to_string(producer.consumer_thread) +
            ",\"consumer_position\":" + (producer.position_known ? std::to_string(producer.position) : "null");
        if (producer.enqueue) {
            const auto& entry = *producer.enqueue;
            result += ",\"enqueue_id\":" + std::to_string(entry.id) +
                ",\"queue_id\":" + std::to_string(entry.queue_id) +
                ",\"producer_qpc\":" + std::to_string(entry.qpc) +
                ",\"producer_thread\":" + std::to_string(entry.thread) +
                ",\"producer_position\":" + (entry.position_known ? std::to_string(entry.position) : "null") +
                ",\"stack_limited\":" + (entry.stack_limited ? "true" : "false") +
                ",\"unresolved_frames\":" + std::to_string(entry.unresolved_frames) + ",\"frames\":[";
            bool first_frame = true;
            for (const auto& frame : entry.frames) {
                if (!first_frame) result += ',';
                first_frame = false;
                result += "{\"module\":" + Quote(frame.module) + ",\"rva\":" + Hex(frame.rva, true) + "}";
            }
            result += ']';
        }
        result += '}';
    } else {
        result += "null";
    }
    result += ",\"caller_frames_state\":" + Quote(event.caller_frames_state) + ",\"caller_unwind\":";
    if (event.caller_stack) {
        const auto& stack = *event.caller_stack;
        result += "{\"native_frames\":" + std::to_string(stack.unwound) +
            ",\"unresolved_frames\":" + std::to_string(stack.unresolved) +
            ",\"native_limit_reached\":" + (stack.native_limit ? "true" : "false") +
            ",\"frame_limit_reached\":" + (stack.frame_limit ? "true" : "false") + "}";
    } else {
        result += "null";
    }
    result += ",\"caller_frames\":[";
    first = true;
    if (event.caller_stack) {
        for (const auto& frame : event.caller_stack->frames) {
            if (!first) result += ',';
            first = false;
            result += "{\"stack_index\":" + std::to_string(frame.stack_index) +
                ",\"module\":" + (frame.module[0] ? Quote(frame.module.data()) : "null") +
                ",\"rva\":" + (frame.module[0] ? Hex(frame.rva, true) : "null") + "}";
        }
    }
    return result + "]}";
}

struct Write {
    size_t event;
    Range range;
    bool whole_barrier = false;
    bool uncertain = false;
};

struct Evidence {
    size_t parent, writer;
    uint64_t resource;
    Range range;
    const char* role;
    const char* state;
    UINT slot;
};

constexpr size_t NoWriter = SIZE_MAX;
constexpr size_t ChainLimit = 96;

class Chain {
    std::unordered_map<uint64_t, std::vector<Write>> writes;
    std::vector<size_t> gaps;
public:
    std::vector<Evidence> nodes;
    bool truncated = false;

    Chain()
    {
        for (size_t i = 0; i < trace.submitted.size(); ++i) {
            const auto& event = trace.submitted[i].event;
            if (event.kind == Kind::Gap) gaps.push_back(i);
            if (event.destination.id && event.written.Size() &&
                (event.kind == Kind::Create || event.kind == Kind::Update || event.kind == Kind::Copy ||
                    event.kind == Kind::Unmap || event.kind == Kind::Discard)) {
                writes[event.destination.id].push_back({i, event.written, event.discard, !event.range_known});
            }
            if (event.kind == Kind::Dispatch) {
                for (const auto& binding : event.bindings) {
                    if (binding.role != Role::Uav) continue;
                    writes[binding.buffer.id].push_back({i, binding.range_known ? binding.range :
                        Range{0, binding.buffer.desc.ByteWidth}, false, !binding.range_known || event.indirect});
                }
            }
        }
    }

    void Note(const Buffer& buffer, Range range, size_t parent, size_t writer,
        const char* role, UINT slot, const char* state)
    {
        if (nodes.size() == ChainLimit) {
            truncated = true;
            return;
        }
        nodes.push_back({parent, writer, buffer.id, range, role, state, slot});
    }

    void Inputs(const Event& event, size_t before, size_t parent, unsigned depth)
    {
        for (const auto& binding : event.bindings) {
            if (binding.role != Role::Srv) continue;
            ResolveBinding(binding, before, parent, depth);
        }
    }

    void ResolveBinding(const Binding& binding, size_t before, size_t parent, unsigned depth = 0,
        bool expand_dispatch = true)
    {
        if (binding.range_known)
            Resolve(binding.buffer, binding.range, before, parent, RoleName(binding.role), binding.slot, depth, expand_dispatch);
        else
            Note(binding.buffer, {}, parent, NoWriter, RoleName(binding.role), binding.slot,
                binding.buffer.id ? "binding_range_unknown" : "buffer_binding_not_observed");
    }

    void Resolve(const Buffer& buffer, Range range, size_t before, size_t parent, const char* role,
        UINT slot = 0, unsigned depth = 0, bool expand_dispatch = true)
    {
        if (!range.Size()) return;
        if (depth >= 12 || nodes.size() == ChainLimit) {
            truncated = true;
            return;
        }
        const auto gap_it = std::lower_bound(gaps.begin(), gaps.end(), before);
        const auto gap = gap_it == gaps.begin() ? NoWriter : *std::prev(gap_it);
        const auto it = writes.find(buffer.id);
        if (it != writes.end()) {
            const auto& history = it->second;
            auto candidate = std::lower_bound(history.begin(), history.end(), before,
                [](const Write& entry, size_t bound) { return entry.event < bound; });
            while (candidate != history.begin()) {
                --candidate;
                if (gap != NoWriter && candidate->event <= gap) break;
                const auto& event = trace.submitted[candidate->event].event;
                const Range overlap{(std::max)(range.begin, candidate->range.begin), (std::min)(range.end, candidate->range.end)};
                if (overlap.begin >= overlap.end) {
                    if (!candidate->whole_barrier) continue;
                    Note(buffer, range, parent, candidate->event, role, slot, "discarded");
                    return;
                }
                const char* state = "write_observed";
                if (event.kind == Kind::Unmap) state = "mapped_snapshot_write_range_unknown";
                else if (event.kind == Kind::Discard) state = "discarded";
                else if (event.kind == Kind::Create && !event.destination.creation.initial_bytes) state = "no_initial_data";
                else if (candidate->uncertain) state = "write_range_uncertain";
                else if (event.kind == Kind::Dispatch) state = "dispatch_uav_view";
                else if (event.kind == Kind::Copy) state = "copied";
                const size_t node = nodes.size() + 1;
                Note(buffer, overlap, parent, candidate->event, role, slot, state);
                if (event.kind == Kind::Copy) {
                    const UINT begin = event.source_range.begin + overlap.begin - event.written.begin;
                    Resolve(event.source, {begin, begin + overlap.Size()}, candidate->event, node, "copy_source", 0,
                        depth + 1, expand_dispatch);
                } else if (event.kind == Kind::Dispatch && expand_dispatch) {
                    Inputs(event, candidate->event, node, depth + 1);
                }
                for (auto remainder : {Range{range.begin, overlap.begin}, Range{overlap.end, range.end}}) {
                    if (!remainder.Size()) continue;
                    if (candidate->whole_barrier)
                        Note(buffer, remainder, parent, candidate->event, role, slot, "discarded");
                    else
                        Resolve(buffer, remainder, candidate->event, parent, role, slot, depth + 1, expand_dispatch);
                }
                return;
            }
        }
        const char* state = gap != NoWriter ? "command_list_history_gap" :
            buffer.creation.initial_bytes ? "initial_data_before_observation_window" : "no_prior_write_observed";
        Note(buffer, range, parent, gap, role, slot, state);
    }
};

std::string DescribeChain(size_t index)
{
    const auto& anchor = trace.submitted[index].event;
    Chain chain;
    std::vector<size_t> consumers;
    if (anchor.kind == Kind::Dispatch) {
        for (size_t i = index + 1; i < trace.submitted.size() && consumers.size() < 8; ++i) {
            const auto& event = trace.submitted[i].event;
            if (event.kind != Kind::Draw || event.bindings.size() < 2) continue;
            chain.nodes.clear();
            chain.ResolveBinding(event.bindings[1], i, 0, 0, false);
            if (std::any_of(chain.nodes.begin(), chain.nodes.end(), [index](const Evidence& node) {
                return node.writer == index;
            })) consumers.push_back(i);
        }
        chain.nodes.clear();
        chain.Inputs(anchor, index, 0, 0);
    } else if (anchor.kind == Kind::Draw) {
        for (const auto& binding : anchor.bindings) chain.ResolveBinding(binding, index, 0);
    } else if (anchor.kind == Kind::Copy) {
        chain.Resolve(anchor.source, anchor.source_range, index, 0, "copy_source");
    }

    std::string result = "{\"anchor\":" + Summary(index) + ",\"consumers\":[";
    const char* separator = "";
    for (auto consumer : consumers) {
        result += separator + Summary(consumer);
        separator = ",";
    }
    result += "],\"consumer_state\":" + Quote(anchor.kind != Kind::Dispatch ? "not_applicable" :
        consumers.empty() ? "no_vb0_consumer_observed" : consumers.size() == 8 ? "consumer_limit" : "observed");
    result += ",\"nodes\":[";
    separator = "";
    for (size_t i = 0; i < chain.nodes.size(); ++i) {
        const auto& node = chain.nodes[i];
        result += separator + ("{\"node\":" + std::to_string(i + 1) + ",\"parent\":" + std::to_string(node.parent) +
            ",\"submission\":" + (node.writer == NoWriter ? "null" : std::to_string(node.writer + 1)) +
            ",\"resource_id\":" + std::to_string(node.resource) + ",\"range\":" + RangeJson(node.range) +
            ",\"role\":" + Quote(node.role) + ",\"slot\":" + std::to_string(node.slot) +
            ",\"state\":" + Quote(node.state) + "}");
        separator = ",";
    }
    return result + "],\"truncated\":" + ((chain.truncated || trace.dropped) ? "true" : "false") + "}";
}

std::string Status()
{
    LARGE_INTEGER qpc_frequency{};
    QueryPerformanceFrequency(&qpc_frequency);
    const auto producer = trace.selector.producer_frames && Armed.load(std::memory_order_relaxed)
        ? ApmiProducer::ReadTotals() : trace.producer_totals;
    return "{\"trace_id\":" + std::to_string(trace.id) + ",\"state\":" + Quote(trace.state) +
        ",\"start_frame\":" + std::to_string(trace.start_frame) + ",\"end_frame\":" + std::to_string(trace.end_frame) +
        ",\"frames_left\":" + std::to_string(trace.frames_left) + ",\"matches\":" + std::to_string(trace.matches.size()) +
        ",\"events\":" + std::to_string(trace.submitted.size()) + ",\"dropped\":" + std::to_string(trace.dropped) +
        ",\"command_list_gaps\":" + std::to_string(trace.gaps) + ",\"sample_bytes\":" + std::to_string(trace.samples) +
        ",\"dispatches\":" + std::to_string(trace.dispatches) + ",\"indexed_draws\":" + std::to_string(trace.draws) +
        ",\"cpu_uploads\":" + std::to_string(trace.cpu_uploads) + ",\"qpc_frequency\":" + std::to_string(qpc_frequency.QuadPart) +
        ",\"caller_frames_requested\":" + std::to_string(trace.selector.caller_frames) +
        ",\"caller_resource_id\":" + std::to_string(trace.selector.caller_resource) +
        ",\"caller_capture_attempts\":" + std::to_string(trace.caller_attempts) +
        ",\"caller_capture_limited\":" + std::to_string(trace.caller_limited) +
        ",\"producer_frames_requested\":" + std::to_string(trace.selector.producer_frames) +
        ",\"unity_producer_counts\":{\"enqueued\":" + std::to_string(producer.enqueued) +
            ",\"omitted\":" + std::to_string(producer.omitted) +
            ",\"consumed\":" + std::to_string(producer.consumed) +
            ",\"paired_packets\":" + std::to_string(producer.paired) +
            ",\"candidates\":" + std::to_string(producer.candidates) +
            ",\"ambiguous\":" + std::to_string(producer.ambiguous) + "}" +
        ",\"miss_reason\":" + (std::string_view(trace.state) != "no_match" ? "null" :
            Quote(trace.dropped ? "observation_truncated" : trace.gaps ? "selector_absent_with_command_list_gaps" :
                "selector_not_observed")) +
        ",\"cpu_hash_algorithm\":\"crc32c\",\"unity_bone_identity\":\"unavailable_at_d3d11\"}";
}

bool Parse(std::string_view field, uint64_t& value, int base = 10)
{
    if (field.empty()) return false;
    const auto end = field.data() + field.size();
    const auto parsed = std::from_chars(field.data(), end, value, base);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}
} // namespace

std::string Request(HackerDevice* device, HackerContext* context, const std::vector<std::string_view>& fields)
{
    const auto op = fields.front();
    std::lock_guard lock(trace_lock);
    if (op == "TRACE_ARM" && (fields.size() == 10 || (fields.size() >= 12 && fields.size() <= 29))) {
        uint64_t ib, uav, shader, resource, frames, sample_resource, sample_bytes, first, count;
        uint64_t caller_frames = 0, caller_resource = 0, producer_frames = 0;
        const bool group = fields.size() > 13;
        if (!Parse(fields[1], ib, 16) || !Parse(fields[2], uav, 16) || !Parse(fields[3], shader, 16) ||
            !Parse(fields[4], resource) || !Parse(fields[5], frames) || !Parse(fields[6], sample_resource) ||
            !Parse(fields[7], sample_bytes) || !Parse(fields[8], first) || !Parse(fields[9], count) ||
            ib > UINT32_MAX || uav > UINT32_MAX || (!group && !ib && !uav && !shader && !resource) ||
            (group && (ib || uav || shader || resource)) ||
            (ib && (uav || shader || resource)) || (!ib && (first != UINT64_MAX || count != UINT64_MAX)) ||
            (first > UINT_MAX && first != UINT64_MAX) || (count > UINT_MAX && count != UINT64_MAX) ||
            !frames || frames > 120 || sample_bytes > SampleLimit || (!!sample_resource != !!sample_bytes))
            return Error("TRACE_SELECTOR_INVALID");
        if (fields.size() >= 12 &&
            (!Parse(fields[10], caller_frames) || !Parse(fields[11], caller_resource) ||
                caller_frames > CallerFrameLimit || (caller_frames && !caller_resource && !resource) ||
                (!caller_frames && caller_resource)))
            return Error("TRACE_CALLER_SELECTOR_INVALID");
        if (fields.size() >= 13 &&
            (!Parse(fields[12], producer_frames) || producer_frames > ApmiProducer::MaxFrames))
            return Error("TRACE_PRODUCER_SELECTOR_INVALID");
        if (Armed.load(std::memory_order_relaxed)) return Error("TRACE_ALREADY_PENDING");
        Trace next;
        next.id = next_capture++;
        next.device = device;
        next.presenter = context;
        next.selector = {resource, shader, sample_resource, static_cast<uint32_t>(ib), static_cast<uint32_t>(uav),
            static_cast<UINT>(sample_bytes), first, count,
            caller_frames ? (caller_resource ? caller_resource : resource) : 0, static_cast<UINT>(caller_frames),
            static_cast<UINT>(producer_frames), {}};
        for (size_t i = 13; i < fields.size(); ++i) {
            uint64_t hash;
            auto& group_hashes = next.selector.ib_hashes;
            if (!Parse(fields[i], hash, 16) || !hash || hash > UINT32_MAX ||
                std::find(group_hashes.begin(), group_hashes.end(), hash) != group_hashes.end())
                return Error("TRACE_IB_GROUP_INVALID");
            group_hashes.push_back(static_cast<uint32_t>(hash));
        }
        next.frames_left = static_cast<UINT>(frames);
        next.frame = next.start_frame = observed_frame.load(std::memory_order_relaxed);
        next.state = "pending";
        next.submitted.reserve(EventLimit);
        next.matches.reserve(EventLimit);
        if (producer_frames) {
            if (const char* error = ApmiProducer::Install()) return Error(error);
            ApmiProducer::Begin(next.id, static_cast<unsigned>(producer_frames));
        }
        trace = std::move(next);
        Armed.store(true, std::memory_order_release);
        return Status();
    }

    uint64_t id = 0;
    if (fields.size() < 2 || !Parse(fields[1], id) || !id || id != trace.id)
        return Error("TRACE_EXPIRED");
    if (op == "TRACE_STATUS" && fields.size() == 2) return Status();
    if (op == "TRACE_CANCEL" && fields.size() == 2) {
        if (Armed.load(std::memory_order_relaxed)) Complete("cancelled");
        return Status();
    }
    if (Armed.load(std::memory_order_relaxed)) return Error("TRACE_PENDING");

    uint64_t value = 0;
    if (fields.size() < 3 || !Parse(fields[2], value)) return Error("TRACE_REQUEST_INVALID");
    if (op == "TRACE_LIST" && fields.size() == 3) {
        const size_t begin = static_cast<size_t>((std::min<uint64_t>)(value, trace.matches.size()));
        const size_t end = (std::min)(begin + 32, trace.matches.size());
        std::string result = "{\"total\":" + std::to_string(trace.matches.size()) + ",\"next_offset\":" +
            std::to_string(end) + ",\"matches\":[";
        for (size_t i = begin; i < end; ++i) {
            if (i != begin) result += ',';
            result += Summary(trace.matches[i]);
        }
        return result + "]}";
    }
    if (!value || value > trace.submitted.size()) return Error("TRACE_EVENT_MISSING");
    const size_t index = static_cast<size_t>(value - 1);
    if (op == "TRACE_EVENT" && fields.size() == 3) return EventJson(index);
    if (op == "TRACE_CHAIN" && fields.size() == 3) {
        if (!std::binary_search(trace.matches.begin(), trace.matches.end(), index)) return Error("TRACE_ANCHOR_MISSING");
        return DescribeChain(index);
    }
    if (op == "TRACE_BYTES" && fields.size() == 3) {
        const auto& bytes = trace.submitted[index].event.sample;
        if (!bytes) return Error("TRACE_SAMPLE_MISSING");
        return "B" + std::string(bytes->data(), bytes->size());
    }
    return Error("TRACE_REQUEST_INVALID");
}
}
