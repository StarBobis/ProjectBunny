#include "McpDiagnostics.h"

#include <Windows.h>
#include <cstdio>
#include <cwchar>
#include <cstddef>

namespace Dx11Diagnostics {
namespace {
constexpr wchar_t MappingName[] = L"Local\\ProjectBunny-DX11-Monitor-v1";
constexpr uint32_t Magic = 0x4d444250;
constexpr uint32_t Capacity = 4096;
constexpr uint32_t MessageSize = 512;

struct alignas(8) Record {
    volatile LONG busy;
    uint32_t kind;
    volatile LONG64 sequence;
    uint32_t pid, tid;
    uint64_t tick, process_start;
    uint32_t detail, length;
    char message[MessageSize];
};

struct alignas(8) Region {
    uint32_t magic, version, capacity, record_size;
    volatile LONG64 head;
    volatile LONG active, level;
    volatile LONG64 observer_tick;
    Record records[Capacity];
};

static_assert(sizeof(Record) == 560 && offsetof(Record, message) == 48);
static_assert(offsetof(Region, records) == 40);

std::atomic<Region*> channel{nullptr};
HANDLE keep_name = nullptr;
std::atomic_flag opening = ATOMIC_FLAG_INIT;
std::atomic<bool> first_frame{true};
std::atomic<unsigned> config_loads{0};
std::atomic<ULONGLONG> next_heartbeat{0};
constexpr ULONGLONG PresentHeartbeatMs = 1000; // Keep in sync with the bridge's Present threshold.
std::atomic<ULONGLONG> next_identity{0};
std::atomic<ULONGLONG> next_attach{0};
uint64_t process_start;

bool IsActive(Region* region) noexcept
{
    if (!region || region->active != 1) return false;
    const auto observed = InterlockedCompareExchange64(&region->observer_tick, 0, 0);
    return observed > 0 && GetTickCount64() - static_cast<uint64_t>(observed) <= 2000;
}

void Write(Region* region, Event kind, const char* message, uint32_t detail = 0) noexcept
{
    if (!IsActive(region)) return;
    const auto sequence = InterlockedIncrement64(&region->head);
    auto& record = region->records[(sequence - 1) % Capacity];
    if (InterlockedCompareExchange(&record.busy, 1, 0) != 0) return;
    // A delayed producer must never overwrite a newer event in the same slot.
    if (record.sequence < sequence) {
        InterlockedExchange64(&record.sequence, 0);
        record.kind = static_cast<uint32_t>(kind);
        record.pid = GetCurrentProcessId();
        record.tid = GetCurrentThreadId();
        record.tick = GetTickCount64();
        record.process_start = process_start;
        record.detail = detail;
        uint32_t length = 0;
        for (; length < MessageSize && message[length]; ++length)
            record.message[length] = message[length];
        record.length = length;
        InterlockedExchange64(&record.sequence, sequence);
    }
    InterlockedExchange(&record.busy, 0);
}

bool TryAttach() noexcept
{
    if (channel.load(std::memory_order_acquire)) return false;
    if (opening.test_and_set(std::memory_order_acquire)) return false;
    HANDLE mapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE, MappingName);
    auto* view = mapping ? static_cast<Region*>(
        MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(Region))) : nullptr;
    const bool accepted = view && view->magic == Magic && view->version == 1 &&
        view->capacity == Capacity && view->record_size == sizeof(Record);
    if (accepted) {
        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
            process_start = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        keep_name = mapping;
        channel.store(view, std::memory_order_release);
        attached.store(true, std::memory_order_release);
    } else {
        if (view) UnmapViewOfFile(view);
        if (mapping) CloseHandle(mapping);
    }
    opening.clear(std::memory_order_release);
    return accepted;
}

void Identity(Event kind, uint32_t detail) noexcept
{
    wchar_t image[MAX_PATH]{};
    char utf8[MessageSize]{};
    const auto length = GetModuleFileNameW(nullptr, image, _countof(image));
    if (length && length < _countof(image)) {
        const auto end = image + length;
        const wchar_t* name = end;
        while (name > image && name[-1] != L'\\') --name;
        WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof(utf8), nullptr, nullptr);
    }
    Write(channel.load(std::memory_order_acquire), kind, utf8, detail);
}
}

std::atomic<bool> attached{false};

void OnDllAttach() noexcept
{
    // The bridge creates the channel before launch. No worker, file I/O,
    // allocation or wait is needed under the loader lock.
    if (TryAttach()) Identity(Event::Attach, 0);
}

void OnDllDetach(bool terminating) noexcept
{
    auto* view = channel.load(std::memory_order_relaxed);
    Write(view, Event::Detach, terminating ? "process_terminating" : "dll_unloaded");
    attached.store(false, std::memory_order_release);
    channel.store(nullptr, std::memory_order_release);
    if (view) UnmapViewOfFile(view);
    if (keep_name) CloseHandle(keep_name);
}

void OnPresent(unsigned frame) noexcept
{
    const auto tick = GetTickCount64();
    if (auto retry = next_attach.load(std::memory_order_relaxed);
        !attached.load(std::memory_order_relaxed) && tick >= retry &&
        next_attach.compare_exchange_strong(retry, tick + 1000, std::memory_order_relaxed)) {
        if (TryAttach()) Identity(Event::Attach, 1);
    }
    if (first_frame.load(std::memory_order_relaxed) && first_frame.exchange(false, std::memory_order_relaxed))
        Phase("first_present");
    if (!attached.load(std::memory_order_relaxed)) return;
    if (auto due = next_heartbeat.load(std::memory_order_relaxed);
        tick >= due && next_heartbeat.compare_exchange_strong(due, tick + PresentHeartbeatMs, std::memory_order_relaxed)) {
        // A new observer may have missed the original attach in a full ring.
        if (auto identity_due = next_identity.load(std::memory_order_relaxed);
            tick >= identity_due && IsActive(channel.load(std::memory_order_acquire)) &&
            next_identity.compare_exchange_strong(identity_due, tick + 3000, std::memory_order_relaxed))
            Identity(Event::Identity, 0);
        Write(channel.load(std::memory_order_acquire), Event::Present, "present", frame);
    }
}

void Phase(const char* message) noexcept
{
    Write(channel.load(std::memory_order_acquire), Event::Phase, message);
}

void Failure(const char* message, uint32_t code) noexcept
{
    Write(channel.load(std::memory_order_acquire), Event::Failure, message, code);
}

bool WantsLog(unsigned level) noexcept
{
    auto* region = channel.load(std::memory_order_acquire);
    if (!region || region->active != 1) return false;
    const auto requested = region->level;
    // Keep initialization INFO even with the default runtime warning filter.
    const bool loading = first_frame.load(std::memory_order_relaxed) ||
        config_loads.load(std::memory_order_relaxed) != 0;
    const bool accepted = requested >= static_cast<LONG>(level) ||
        (loading && level <= static_cast<unsigned>(Event::Info));
    return accepted && IsActive(region);
}

void RecordLog(unsigned level, const char* format, va_list args) noexcept
{
    if (!WantsLog(level)) return;
    char text[MessageSize]{};
    va_list copy;
    va_copy(copy, args);
    _vsnprintf_s(text, _countof(text), _TRUNCATE, format, copy);
    va_end(copy);
    Write(channel.load(std::memory_order_acquire), static_cast<Event>(level), text);
}

void RecordLog(unsigned level, const wchar_t* format, va_list args) noexcept
{
    if (!WantsLog(level)) return;
    wchar_t wide[MessageSize / 4]{};
    char text[MessageSize]{};
    va_list copy;
    va_copy(copy, args);
    _vsnwprintf_s(wide, _countof(wide), _TRUNCATE, format, copy);
    va_end(copy);
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, _countof(text), nullptr, nullptr);
    Write(channel.load(std::memory_order_acquire), static_cast<Event>(level), text);
}

ConfigLoad::ConfigLoad() noexcept
{
    config_loads.fetch_add(1, std::memory_order_relaxed);
    Phase("loading_config");
}

ConfigLoad::~ConfigLoad() noexcept
{
    Phase("config_load_finished");
    config_loads.fetch_sub(1, std::memory_order_relaxed);
}
}
