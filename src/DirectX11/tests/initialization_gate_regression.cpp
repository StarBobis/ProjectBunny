#include "D3D11Initialization.h"
#include "InitializationGate.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
void run_initialization_gate_regression()
{
    // Exercise the production resolver with actual system DLL export tables,
    // without loading the proxy or entering its configuration/loader hooks.
    HMODULE native = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    check(native != nullptr, "load system D3D11 export table");
    const auto exports = ResolveD3D11CreationExports(native);
    HMODULE without_exports = GetModuleHandleW(L"kernel32.dll");
    const auto missing = ResolveD3D11CreationExports(without_exports);
    const auto existing = ResolveD3D11CreationExports(without_exports,
        exports.create_device, exports.create_swap_chain);
    const bool exports_ok = exports.valid() && !missing.valid() && existing.valid() &&
        existing.create_device == exports.create_device && existing.create_swap_chain == exports.create_swap_chain;
    FreeLibrary(native);
    check(exports_ok, "require both creation exports while retaining existing hook entry points");

    InitializationGate retry;
    int attempts = 0;
    check(!retry.Run([&] { ++attempts; return false; }), "failure remains retryable");
    bool caught = false;
    try { retry.Run([&]() -> bool { ++attempts; throw std::runtime_error("expected"); }); }
    catch (const std::runtime_error&) { caught = true; }
    check(caught, "initializer exception is observable after resetting gate");
    check(retry.Run([&] {
        ++attempts;
        check(!retry.Run([] { throw std::runtime_error("recursive initializer ran"); return true; }),
            "same-thread reentry fails without exposing partial state");
        return true;
    }), "successful retry publishes readiness");
    check(retry.Run([&] { ++attempts; return false; }) && attempts == 3,
        "completed initialization never reruns");

    InitializationGate concurrent;
    std::promise<void> owner_entered, release_owner;
    auto entered = owner_entered.get_future();
    auto release = release_owner.get_future().share();
    std::atomic<int> callbacks{0}, returned{0}, incorrect{0}, started{0};
    int published_payload = 0; // visibility must come from the production gate.
    std::thread owner([&] {
        const bool ready = concurrent.Run([&] {
            ++callbacks;
            owner_entered.set_value();
            release.wait();
            published_payload = 417;
            return true;
        });
        if (!ready || published_payload != 417) ++incorrect;
        ++returned;
    });
    entered.wait();
    std::vector<std::thread> waiters;
    for (int i = 0; i < 7; ++i) waiters.emplace_back([&] {
        ++started;
        const bool ready = concurrent.Run([&] { ++callbacks; return false; });
        if (!ready || published_payload != 417) ++incorrect;
        ++returned;
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (started.load() != 7 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    const bool all_started = started.load() == 7;
    // Give each started caller a scheduling opportunity while the initializer
    // remains explicitly blocked; this is not a performance measurement.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const bool blocked = returned.load() == 0 && callbacks.load() == 1;
    release_owner.set_value();
    owner.join();
    for (auto& waiter : waiters) waiter.join();
    check(all_started && blocked, "concurrent callers cannot return before owner publishes");
    check(returned == 8 && callbacks == 1 && incorrect == 0,
        "all concurrent callers observe the same fully initialized payload");
}
}
void test_dx11_initialization_gate_regression()
{
    try {
        run_initialization_gate_regression();
        std::fputs("PASS: production initialization gate publication, reentry and retry\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL initialization gate: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
