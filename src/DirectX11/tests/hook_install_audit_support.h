#pragma once

// F-182: deterministic harness for the hook installation state machine. Only
// the two Hooked* audit translation units include this header. It replaces
// cHookMgr and InitializeCriticalSectionPretty by macros around the
// #include of the production .cpp, so the production code is not changed for
// the test and no second definition of the production TU is linked.

#include <Windows.h>
#include "../lock.h"
#include "../DLLMainHook.h"

#include <atomic>
#include <cstdio>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace dx11_hook_install_audit {

inline void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

// A rendezvous at the production map-lock initialisation: an installer that
// reached it has already passed the state check, but orig_vtable has not been
// touched yet. The test uses this to force the legacy "both passed the check"
// interleaving and to prove that the state machine only admits one installer.
class InstallGate {
public:
    HANDLE arrival[2]{};
    HANDLE release[2]{};
    std::atomic<int> count{0};
    bool block = false;

    InstallGate()
    {
        for (int i = 0; i < 2; ++i) {
            arrival[i] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            release[i] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        }
    }
    ~InstallGate()
    {
        for (int i = 0; i < 2; ++i) {
            if (arrival[i]) CloseHandle(arrival[i]);
            if (release[i]) CloseHandle(release[i]);
        }
    }
    InstallGate(const InstallGate&) = delete;
    InstallGate& operator=(const InstallGate&) = delete;

    void reset(bool block_installation)
    {
        count.store(0);
        block = block_installation;
        for (int i = 0; i < 2; ++i) {
            ResetEvent(arrival[i]);
            ResetEvent(release[i]);
        }
    }

    void enter(CRITICAL_SECTION* lock)
    {
        _InitializeCriticalSectionPretty(lock, "audit_hook_map_lock");
        const int index = count.fetch_add(1);
        if (index < 2)
            SetEvent(arrival[index]);
        if (!block)
            return;
        const int slot = index < 2 ? index : 1;
        require(WaitForSingleObject(release[slot], 5000) == WAIT_OBJECT_0,
            "F182: installer leaves the gate within the bounded schedule");
    }

    bool wait_arrival(int index, DWORD milliseconds)
    {
        return WaitForSingleObject(arrival[index], milliseconds) == WAIT_OBJECT_0;
    }

    void release_one(int index)
    {
        SetEvent(release[index]);
    }
};

inline InstallGate install_gate;

// Substituted for InitializeCriticalSectionPretty only while the production
// Hooked*.cpp is included by the regression TU.
inline void install_gate_enter(CRITICAL_SECTION* lock)
{
    install_gate.enter(lock);
}

inline void sentinel_trampoline() {}
inline LPVOID sentinel_address()
{
    return reinterpret_cast<LPVOID>(&sentinel_trampoline);
}

// Per installer thread state used by the test double below.
inline thread_local int audit_hook_thread_index = -1;
inline thread_local unsigned audit_hook_thread_calls = 0;

// Test double for CNktHookLib. It records the complete call sequence, writes
// a sentinel trampoline into the caller's out parameter, can fail one chosen
// Hook call, and blocks at two well-defined points:
//  - after the call that completed the last hook of an installer;
//  - before the first Hook call of the second installer thread.
class AuditHookMgr {
public:
    struct Call {
        SIZE_T id;
        LPVOID target;
        LPVOID detour;
        LPVOID* out;
        DWORD flags;
        LPVOID prior;
    };

    std::mutex mutex;
    std::vector<Call> calls;
    std::vector<SIZE_T> unhooked;
    std::vector<SIZE_T> enabled;
    SIZE_T next_id = 1;
    size_t attempts = 0;
    long fail_at = 0;
    long block_after_call = 0;
    bool block_pre_second_thread = false;
    std::atomic<int> hook_threads{0};
    HANDLE blocked = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE unblock = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE second_pre = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE second_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    LPVOID* pending_out = nullptr;
    LPVOID pending_target = nullptr;

    ~AuditHookMgr()
    {
        for (HANDLE handle : {blocked, unblock, second_pre, second_release}) {
            if (handle) CloseHandle(handle);
        }
    }
    AuditHookMgr() = default;
    AuditHookMgr(const AuditHookMgr&) = delete;
    AuditHookMgr& operator=(const AuditHookMgr&) = delete;

    void reset()
    {
        std::lock_guard<std::mutex> lock(mutex);
        calls.clear();
        unhooked.clear();
        enabled.clear();
        next_id = 1;
        attempts = 0;
        fail_at = 0;
        block_after_call = 0;
        block_pre_second_thread = false;
        hook_threads.store(0);
        pending_out = nullptr;
        pending_target = nullptr;
        ResetEvent(blocked);
        ResetEvent(unblock);
        ResetEvent(second_pre);
        ResetEvent(second_release);
        audit_hook_thread_index = -1;
        audit_hook_thread_calls = 0;
    }

    DWORD Hook(SIZE_T* id, LPVOID* original, LPVOID target, LPVOID detour,
        DWORD flags = 0)
    {
        bool wait_blocked = false;
        bool wait_second = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const size_t call_index = ++attempts;
            if (fail_at > 0 && call_index == static_cast<size_t>(fail_at))
                return ERROR_INVALID_PARAMETER;
            if (audit_hook_thread_index < 0)
                audit_hook_thread_index = hook_threads.fetch_add(1);
            ++audit_hook_thread_calls;

            Call call{next_id++, target, detour, original, flags, *original};
            calls.push_back(call);
            *original = sentinel_address();
            *id = call.id;

            if (block_pre_second_thread && audit_hook_thread_index == 1 &&
                    audit_hook_thread_calls == 1) {
                pending_out = original;
                pending_target = target;
                wait_second = true;
            }
            if (block_after_call > 0 &&
                    call_index == static_cast<size_t>(block_after_call)) {
                wait_blocked = true;
            }
        }
        if (wait_second) {
            SetEvent(second_pre);
            require(WaitForSingleObject(second_release, 5000) == WAIT_OBJECT_0,
                "F182: second installer resumes within the bounded schedule");
        }
        if (wait_blocked) {
            SetEvent(blocked);
            require(WaitForSingleObject(unblock, 5000) == WAIT_OBJECT_0,
                "F182: first installer resumes within the bounded schedule");
        }
        return ERROR_SUCCESS;
    }

    DWORD Unhook(SIZE_T id)
    {
        std::lock_guard<std::mutex> lock(mutex);
        unhooked.push_back(id);
        return ERROR_SUCCESS;
    }

    DWORD EnableHook(SIZE_T id, BOOL)
    {
        std::lock_guard<std::mutex> lock(mutex);
        enabled.push_back(id);
        return ERROR_SUCCESS;
    }

    bool wait_blocked(DWORD milliseconds)
    {
        return WaitForSingleObject(blocked, milliseconds) == WAIT_OBJECT_0;
    }

    void release_blocked()
    {
        SetEvent(unblock);
    }

    bool wait_second_pre(DWORD milliseconds)
    {
        return WaitForSingleObject(second_pre, milliseconds) == WAIT_OBJECT_0;
    }

    void release_second_pre()
    {
        SetEvent(second_release);
    }

    // Called while the second installer is blocked before its first Hook call:
    // its preceding memcpy already replaced the first installer's trampolines
    // with the fake vtable, which is the F-182 corruption window.
    bool second_saw_overwritten_table()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return pending_out && *pending_out == pending_target;
    }

    size_t blocked_hook_count()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return calls.size();
    }
};

inline AuditHookMgr audit_hook_mgr;

// T-1: two threads call install(). The gate admits only the first installer;
// the second must wait for Ready and never run Hook/memcpy. On the legacy code
// both pass the gate, both run the installation body, the second memcpy
// overwrites the first trampolines, and the count assertions fail.
template <class Install>
void run_double_install(Install install, long expected_hooks, const char* component)
{
    install_gate.reset(true);
    audit_hook_mgr.reset();
    audit_hook_mgr.block_after_call = expected_hooks;
    audit_hook_mgr.block_pre_second_thread = true;

    std::exception_ptr first_error, second_error;
    std::thread first([&] {
        try { install(); } catch (...) { first_error = std::current_exception(); }
    });
    require(install_gate.wait_arrival(0, 5000),
        "F182 T-1: the first installer reaches the gate");
    std::thread second([&] {
        try { install(); } catch (...) { second_error = std::current_exception(); }
    });
    const bool second_installer = install_gate.wait_arrival(1, 1500);
    install_gate.release_one(0);
    require(audit_hook_mgr.wait_blocked(5000),
        "F182 T-1: the first installer completes its last hook");

    bool overwritten = false;
    if (second_installer) {
        install_gate.release_one(1);
        if (audit_hook_mgr.wait_second_pre(5000))
            overwritten = audit_hook_mgr.second_saw_overwritten_table();
        audit_hook_mgr.release_second_pre();
    }
    audit_hook_mgr.release_blocked();
    first.join();
    second.join();
    if (first_error) std::rethrow_exception(first_error);
    if (second_error) std::rethrow_exception(second_error);

    std::fprintf(stderr,
        "F182 T-1 %s: gate arrivals=%d, Hook calls=%zu, enabled=%zu, second installer=%d\n",
        component, install_gate.count.load(), audit_hook_mgr.calls.size(),
        audit_hook_mgr.enabled.size(), second_installer ? 1 : 0);

    require(install_gate.count.load() == 1,
        "F182 T-1: exactly one installer enters the installation body");
    require(!overwritten,
        "F182 T-1: a second installer must not overwrite orig_vtable");
    require(audit_hook_mgr.calls.size() == static_cast<size_t>(expected_hooks),
        "F182 T-1: each hook is created exactly once, never twice");
    require(audit_hook_mgr.enabled.size() == static_cast<size_t>(expected_hooks),
        "F182 T-1: every created hook is enabled exactly once");
    for (const auto& call : audit_hook_mgr.calls) {
        require(*call.out == sentinel_address(),
            "F182 T-1: every orig_vtable entry holds its trampoline");
        require(call.flags == NKTHOOKLIB_DontEnableHooks,
            "F182 T-1: hooks are created disabled and published later");
    }
}

} // namespace dx11_hook_install_audit
