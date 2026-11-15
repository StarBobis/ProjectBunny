#pragma once

#include <Windows.h>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <thread>

namespace dx11_hook_lookup_audit {
inline void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

class LookupSchedule {
public:
    CRITICAL_SECTION* lock;
    HANDLE unlocked = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    explicit LookupSchedule(CRITICAL_SECTION* target) : lock(target)
    {
        require(unlocked && resume, "F181: create deterministic lookup events");
    }
    ~LookupSchedule()
    {
        if (unlocked) CloseHandle(unlocked);
        if (resume) CloseHandle(resume);
    }
    LookupSchedule(const LookupSchedule&) = delete;
    LookupSchedule& operator=(const LookupSchedule&) = delete;
};

inline thread_local LookupSchedule* lookup_schedule = nullptr;

// Substituted only while compiling the two regression TUs. The native unlock
// happens before the checkpoint; production DLL code has no added branch.
inline void WINAPI leave_and_pause(CRITICAL_SECTION* lock)
{
    ::LeaveCriticalSection(lock);
    auto* schedule = lookup_schedule;
    if (!schedule || schedule->lock != lock) return;
    lookup_schedule = nullptr;
    require(SetEvent(schedule->unlocked) != FALSE, "F181: announce native unlock");
    require(WaitForSingleObject(schedule->resume, 5000) == WAIT_OBJECT_0,
        "F181: resume lookup within the bounded schedule");
}

template <class Map, class Interface, class Lookup, class Mutate>
void scheduled_lookup(Map& registry, CRITICAL_SECTION* lock, Interface* original,
    Interface* wrapper, Lookup lookup, Mutate mutate)
{
    LookupSchedule schedule(lock);
    Interface* result = nullptr;
    std::exception_ptr worker_error;
    std::thread reader([&] {
        try {
            lookup_schedule = &schedule;
            result = lookup(original);
            lookup_schedule = nullptr;
        } catch (...) {
            lookup_schedule = nullptr;
            worker_error = std::current_exception();
        }
    });
    const bool paused = WaitForSingleObject(schedule.unlocked, 5000) == WAIT_OBJECT_0;
    std::exception_ptr mutation_error;
    if (paused) {
        ::EnterCriticalSection(lock);
        try { mutate(registry); } catch (...) { mutation_error = std::current_exception(); }
        ::LeaveCriticalSection(lock);
    }
    SetEvent(schedule.resume);
    reader.join();
    require(paused, "F181: lookup reaches the post-unlock checkpoint");
    if (worker_error) std::rethrow_exception(worker_error);
    if (mutation_error) std::rethrow_exception(mutation_error);
    require(result == wrapper, "F181: concurrent registry changes preserve the borrowed result");

    bool released = false;
    std::thread observer([&] {
        released = TryEnterCriticalSection(lock) != FALSE;
        if (released) ::LeaveCriticalSection(lock);
    });
    observer.join();
    require(released, "F181: lookup releases the registry lock for another thread");
}

template <class Map, class Interface, class Lookup>
void exercise_registry(Map& registry, CRITICAL_SECTION* lock, Interface* original,
    Interface* wrapper, Interface* other_original, Interface* other_wrapper, Lookup lookup)
{
    require(registry.empty(), "F181: isolate the production hook registry");
    require(lookup(original) == nullptr, "F181: an unregistered live COM object is absent");
    registry.emplace(original, wrapper);
    registry.emplace(other_original, other_wrapper);
    require(lookup(original) == wrapper && lookup(other_original) == other_wrapper,
        "F181: registered live objects return their existing wrappers");
    for (unsigned round = 0; round != 16; ++round) {
        scheduled_lookup(registry, lock, original, wrapper, lookup, [](Map& map) {
            const auto before = map.bucket_count();
            map.rehash(before * 2);
            require(map.bucket_count() != before, "F181: the schedule forces a real rehash");
        });
        scheduled_lookup(registry, lock, original, wrapper, lookup, [&](Map& map) {
            require(map.erase(other_original) == 1, "F181: erase only the other live object's node");
            require(map.find(original) != map.end(), "F181: keep the looked-up node registered");
        });
        ::EnterCriticalSection(lock);
        registry.emplace(other_original, other_wrapper);
        ::LeaveCriticalSection(lock);
    }
    require(lookup(original) == wrapper && lookup(other_original) == other_wrapper,
        "F181: registry remains usable after all scheduled mutations");
    registry.clear();
    require(lookup(original) == nullptr, "F181: lookup miss also releases the registry lock");
    std::printf("F181: 16 rehash and 16 unrelated-node erase schedules; live COM references retained\n");
}
}
