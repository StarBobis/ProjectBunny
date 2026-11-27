#include "McpApmiProducer.h"
#include "DLLMainHook.h"

#include <wincrypt.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>

extern HINSTANCE migoto_handle;

namespace Dx11Mcp::ApmiProducer {
namespace {
constexpr unsigned RecordLimit = 2048;
constexpr unsigned NativeUnwindLimit = 48;

struct Packet {
    const void* buffer;
    const void* pose;
    bool operator==(const Packet& other) const { return buffer == other.buffer && pose == other.pose; }
};

struct Cursor {
    const void* stream = nullptr;
    uint32_t position = 0;
    bool known = false;
};

struct Record {
    Packet packet;
    Cursor cursor;
    std::shared_ptr<const Enqueue> enqueue;
    bool paired = false;
};

struct Capture {
    uint64_t id = 0;
    unsigned frames = 0;
    Totals totals;
    std::vector<Record> records;
    std::unordered_map<const void*, uint64_t> queues;
};

struct DecodeScope {
    uint64_t id = 0;
    Cursor cursor;
};

struct InlineScope {
    uint64_t id = 0;
    Packet packet{};
    std::shared_ptr<const Enqueue> enqueue;
};

struct UploadScope {
    uint64_t id = 0;
    const void* data = nullptr;
    unsigned bytes = 0;
    Upload result;
};

std::mutex capture_lock;
Capture capture;
std::atomic<uint64_t> active{0};
thread_local const DecodeScope* decoded = nullptr;
thread_local const InlineScope* synchronous = nullptr;
thread_local const UploadScope* backend = nullptr;

template<class T>
class ThreadScope {
    const T*& slot;
    const T* old;
public:
    ThreadScope(const T*& scope, const T* value) : slot(scope), old(scope) { slot = value; }
    ~ThreadScope() { slot = old; }
    ThreadScope(const ThreadScope&) = delete;
    ThreadScope& operator=(const ThreadScope&) = delete;
};

template<class T>
bool ReadField(const void* object, size_t offset, T& value) noexcept
{
    if (!object) return false;
    __try {
        memcpy(&value, static_cast<const unsigned char*>(object) + offset, sizeof(value));
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
        GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

int64_t Timestamp() noexcept
{
    LARGE_INTEGER ticks{};
    QueryPerformanceCounter(&ticks);
    return ticks.QuadPart;
}

Cursor ReadCursor(const void* stream, bool writer) noexcept
{
    // UnityPlayer ce1544f8: these are the local cursor, limit and base
    // used when publishing a straight-line read or write. Leave refill
    // paths without a position until their segment transition is observed.
    const size_t offset = writer ? 0x148 : 0x108;
    uint32_t cursor, limit, base;
    if (!ReadField(stream, offset, cursor) || !ReadField(stream, offset + 4, limit) ||
        !ReadField(stream, offset + 12, base) || cursor > limit || limit - cursor < 4)
        return {stream};
    return {stream, base + cursor, true};
}

void CaptureStack(Enqueue& record, unsigned limit)
{
    void* addresses[NativeUnwindLimit];
    const auto count = CaptureStackBackTrace(1, NativeUnwindLimit, addresses, nullptr);
    record.stack_limited = count == NativeUnwindLimit;
    for (unsigned i = 0; i < count; ++i) {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(addresses[i]), &module)) {
            ++record.unresolved_frames;
            continue;
        }
        if (module == migoto_handle) continue;
        if (record.frames.size() == limit) {
            record.stack_limited = true;
            break;
        }
        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(module, path, _countof(path));
        char utf8[128]{};
        auto* slash = length && length < _countof(path) ? wcsrchr(path, L'\\') : nullptr;
        if (!length || length >= _countof(path) ||
            !WideCharToMultiByte(CP_UTF8, 0, slash ? slash + 1 : path, -1, utf8, _countof(utf8), nullptr, nullptr)) {
            ++record.unresolved_frames;
            continue;
        }
        record.frames.push_back({utf8, reinterpret_cast<uintptr_t>(addresses[i]) - reinterpret_cast<uintptr_t>(module)});
    }
}

std::shared_ptr<const Enqueue> RecordProducer(uint64_t id, Packet packet, Cursor cursor) noexcept
{
    std::lock_guard lock(capture_lock);
    if (capture.id != id || active.load(std::memory_order_relaxed) != id) return {};
    if (capture.records.size() == RecordLimit) {
        ++capture.totals.omitted;
        return {};
    }
    try {
        auto row = std::make_shared<Enqueue>();
        row->id = capture.records.size() + 1;
        row->qpc = Timestamp();
        row->thread = GetCurrentThreadId();
        row->position_known = cursor.known;
        row->position = cursor.position;
        if (cursor.stream)
            row->queue_id = capture.queues.try_emplace(cursor.stream, capture.queues.size() + 1).first->second;
        CaptureStack(*row, capture.frames);
        capture.records.push_back({packet, cursor, row});
        ++capture.totals.enqueued;
        return row;
    } catch (...) {
        ++capture.totals.omitted;
        return {};
    }
}

Upload FindEnqueue(uint64_t id, Packet packet, Cursor cursor) noexcept
{
    Upload result;
    result.state = "producer_not_observed";
    result.consumer_qpc = Timestamp();
    result.consumer_thread = GetCurrentThreadId();
    result.position = cursor.position;
    result.position_known = cursor.known;
    std::lock_guard lock(capture_lock);
    if (capture.id != id || active.load(std::memory_order_relaxed) != id) return result;
    ++capture.totals.consumed;
    if (synchronous && synchronous->id == id && synchronous->packet == packet && synchronous->enqueue) {
        result.enqueue = synchronous->enqueue;
        result.state = "synchronous_call";
        capture.records[result.enqueue->id - 1].paired = true;
        ++capture.totals.paired;
        return result;
    }

    Record* exact = nullptr;
    unsigned exact_count = 0, candidates = 0;
    std::shared_ptr<const Enqueue> candidate;
    for (auto& row : capture.records) {
        if (!cursor.stream || cursor.stream != row.cursor.stream) continue;
        if (cursor.known && row.cursor.known && cursor.position == row.cursor.position) {
            exact = &row;
            ++exact_count;
        }
        if (!row.paired && row.packet == packet && (!cursor.known || !row.cursor.known)) {
            candidate = row.enqueue;
            ++candidates;
        }
    }
    if (exact_count == 1 && !exact->paired && exact->packet == packet) {
        exact->paired = true;
        result.state = "queue_position_and_references";
        result.enqueue = exact->enqueue;
        ++capture.totals.paired;
    } else if (exact_count || candidates > 1) {
        result.state = "ambiguous_queue_evidence";
        ++capture.totals.ambiguous;
    } else if (candidates == 1) {
        result.state = "references_candidate";
        result.enqueue = std::move(candidate);
        ++capture.totals.candidates;
    }
    return result;
}

#ifdef _WIN64
using SubmitPose = void (*)(void*, void*, void*);
using DecodeCommand = uint32_t (*)(void*, void*);
SubmitPose producer_original, backend_original;
DecodeCommand decoder_original;
uintptr_t worker_return = 0, sync_return = 0;

void ProducerHook(void* device, void* buffer, void* pose)
{
    const auto id = active.load(std::memory_order_acquire);
    if (!id) {
        producer_original(device, buffer, pose);
        return;
    }
    unsigned char queued = 0;
    const void* stream = nullptr;
    const bool mode_known = ReadField(device, 0x2550, queued);
    if (!mode_known || (queued && !ReadField(device, 0x2558, stream))) {
        producer_original(device, buffer, pose);
        return;
    }
    const Packet packet{buffer, pose};
    const InlineScope mark{id, packet, RecordProducer(id, packet, ReadCursor(stream, true))};
    ThreadScope scope(synchronous, queued ? nullptr : &mark);
    producer_original(device, buffer, pose);
}

uint32_t DecoderHook(void* worker, void* stream)
{
    const auto id = active.load(std::memory_order_acquire);
    if (!id) return decoder_original(worker, stream);
    const DecodeScope mark{id, ReadCursor(stream, false)};
    ThreadScope scope(decoded, &mark);
    return decoder_original(worker, stream);
}

void BackendHook(void* device, void* buffer, void* pose)
{
    const auto id = active.load(std::memory_order_acquire);
    const auto return_address = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool from_worker = id && decoded && decoded->id == id && (!worker_return || return_address == worker_return);
    const bool inline_call = id && synchronous && synchronous->id == id && (!sync_return || return_address == sync_return);
    if (!from_worker && !inline_call) {
        backend_original(device, buffer, pose);
        return;
    }
    const void* data = nullptr;
    size_t count = 0;
    const bool known = ReadField(pose, 8, data) && ReadField(pose, 16, count) && count && count <= UINT32_MAX / 64;
    const UploadScope mark{id, data, known ? static_cast<unsigned>(count * 64) : 0,
        FindEnqueue(id, {buffer, pose}, from_worker ? decoded->cursor : Cursor{})};
    ThreadScope scope(backend, &mark);
    backend_original(device, buffer, pose);
}

bool FileHashMatches(const wchar_t* path) noexcept
{
    constexpr std::array<unsigned char, 32> expected{
        0xce, 0x15, 0x44, 0xf8, 0x53, 0xcf, 0x32, 0xba, 0x91, 0xc7, 0x05, 0xcd, 0xc4, 0xf6, 0xc1, 0x68,
        0xa8, 0xa4, 0x14, 0x7e, 0x05, 0xbb, 0x62, 0xb9, 0x38, 0xa5, 0x5a, 0x13, 0x93, 0xa4, 0xa7, 0x0e};
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    LARGE_INTEGER size{};
    bool valid = GetFileSizeEx(file, &size) && size.QuadPart == 31940200 &&
        CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);
    unsigned char data[32 * 1024];
    while (valid) {
        DWORD count = 0;
        valid = ReadFile(file, data, sizeof(data), &count, nullptr) != FALSE;
        if (!valid || !count) break;
        valid = CryptHashData(hash, data, count, 0) != FALSE;
    }
    std::array<unsigned char, 32> digest{};
    DWORD bytes = static_cast<DWORD>(digest.size());
    valid = valid && CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &bytes, 0) && digest == expected;
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    return valid;
}

template<size_t N>
bool Witness(const void* base, size_t offset, const char (&bytes)[N]) noexcept
{
    char mapped[N - 1];
    return ReadField(base, offset, mapped) && memcmp(mapped, bytes, sizeof(mapped)) == 0;
}

const char* InstallHooks(void* producer, void* decoder, void* consume) noexcept
{
    CNktHookLib::HOOK_INFO hooks[]{
        {0, producer, reinterpret_cast<void*>(ProducerHook), nullptr},
        {0, decoder, reinterpret_cast<void*>(DecoderHook), nullptr},
        {0, consume, reinterpret_cast<void*>(BackendHook), nullptr},
    };
    if (cHookMgr.Hook(hooks, _countof(hooks), NKTHOOKLIB_DontEnableHooks) != ERROR_SUCCESS) {
        for (const auto& hook : hooks)
            if (hook.nHookId) cHookMgr.Unhook(hook.nHookId);
        return "TRACE_APMI_HOOK_INSTALL_FAILED";
    }
    producer_original = reinterpret_cast<SubmitPose>(hooks[0].lpCallOriginal);
    decoder_original = reinterpret_cast<DecodeCommand>(hooks[1].lpCallOriginal);
    backend_original = reinterpret_cast<SubmitPose>(hooks[2].lpCallOriginal);
    if (cHookMgr.EnableHook(hooks, _countof(hooks), TRUE) != ERROR_SUCCESS)
        return "TRACE_APMI_HOOK_ENABLE_FAILED";
    return nullptr;
}
#endif
} // namespace

const char* Install() noexcept
{
#ifndef _WIN64
    return "TRACE_APMI_REQUIRES_X64";
#else
    static std::mutex install_lock;
    static bool attempted = false;
    static const char* result = nullptr;
    std::lock_guard lock(install_lock);
    if (attempted) return result;
    wchar_t executable[MAX_PATH], path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, executable, _countof(executable));
    const auto* basename = length && length < _countof(executable) ? wcsrchr(executable, L'\\') : nullptr;
    if (!basename || _wcsicmp(basename + 1, L"AzurPromilia.exe") != 0) return "TRACE_APMI_WRONG_EXECUTABLE";
    HMODULE unity = nullptr;
    if (!GetModuleHandleExW(0, L"UnityPlayer.dll", &unity)) return "TRACE_APMI_MODULE_UNAVAILABLE";
    const DWORD module_length = GetModuleFileNameW(unity, path, _countof(path));
    if (!module_length || module_length >= _countof(path) || !FileHashMatches(path) ||
        !Witness(unity, 0xa129f0, "\x48\x89\x5c\x24\x18") ||
        !Witness(unity, 0xa12a6a, "\xc7\x04\x01\x4b\x27\x00\x00") ||
        !Witness(unity, 0xa12b40, "\x44\x89\x81\xc0\x00\x00\x00") ||
        !Witness(unity, 0x11126b0, "\x40\x55\x53\x56\x57\x41\x54") ||
        !Witness(unity, 0x11125b8, "\x44\x8b\x83\x14\x01\x00\x00\x44\x03\x83\x08\x01\x00\x00") ||
        !Witness(unity, 0x111e163, "\x41\xff\xd2") ||
        !Witness(unity, 0x740a80, "\x48\x89\x5c\x24\x08") ||
        !Witness(unity, 0x93dd24, "\xff\x90\x80\x01\x00\x00")) {
        FreeLibrary(unity);
        return "TRACE_APMI_UNSUPPORTED_IMAGE";
    }
    // Keep the verified image mapped for the process lifetime of its hooks.
    auto* base = reinterpret_cast<unsigned char*>(unity);
    worker_return = reinterpret_cast<uintptr_t>(base + 0x111e166);
    sync_return = reinterpret_cast<uintptr_t>(base + 0xa12b81);
    attempted = true;
    result = InstallHooks(base + 0xa129f0, base + 0x11126b0, base + 0x740a80);
    if (result && !producer_original) FreeLibrary(unity);
    return result;
#endif
}

void Begin(uint64_t trace_id, unsigned frames)
{
    std::lock_guard lock(capture_lock);
    Capture next;
    next.id = trace_id;
    next.frames = frames;
    next.records.reserve(RecordLimit);
    capture = std::move(next);
    active.store(trace_id, std::memory_order_release);
}

Totals ReadTotals() noexcept
{
    std::lock_guard lock(capture_lock);
    return capture.totals;
}

Totals End() noexcept
{
    active.store(0, std::memory_order_release);
    std::lock_guard lock(capture_lock);
    const auto result = capture.totals;
    capture = {};
    return result;
}

Upload ObserveUpload(uint64_t trace_id, const void* source, unsigned bytes) noexcept
{
    if (!backend || backend->id != trace_id) return {};
    if (!source || source != backend->data || bytes != backend->bytes) {
        Upload mismatch = backend->result;
        mismatch.state = "backend_upload_arguments_differ";
        mismatch.enqueue.reset();
        return mismatch;
    }
    return backend->result;
}

#ifdef DX11_APMI_PRODUCER_TEST
const char* InstallFixture(void* producer, void* decoder, void* backend) noexcept
{
    return InstallHooks(producer, decoder, backend);
}
#endif
} // namespace Dx11Mcp::ApmiProducer
