#include "D3D11Initialization.h"
#include "InitializationGate.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {
void check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

enum class GateFault { NONE, OWNER_ENTRY_TIMEOUT, WAITER_START_TIMEOUT, THREAD_START_FAILURE, UNCOOPERATIVE };
struct GateCounts {
    std::atomic<int> callbacks{0}, returned{0}, incorrect{0}, started{0}, finished{0};
    int spawned = 0, joined = 0;
};

class GateHandle {
public:
    HANDLE value;
    explicit GateHandle(HANDLE handle) : value(handle) {}
    ~GateHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    GateHandle(const GateHandle&) = delete;
    GateHandle& operator=(const GateHandle&) = delete;
};

class InitializationWorkers {
    GateFault fault;
    GateCounts& counts;
    HANDLE entered_event;
    InitializationGate concurrent;
    std::promise<void> owner_entered, release_owner;
    std::future<void> entered = owner_entered.get_future();
    std::shared_future<void> release = release_owner.get_future().share();
    std::vector<std::thread> workers;
    std::vector<std::future<void>> completed;
    bool released = false;
    int published_payload = 0; // Only the production gate publishes this payload.

    template<class Work>
    void Start(Work&& work, bool fail_start = false)
    {
        std::packaged_task<void()> task([&, work = std::forward<Work>(work)]() mutable {
            try { work(); }
            catch (...) { ++counts.finished; throw; }
            ++counts.finished;
        });
        auto done = task.get_future();
        // Replace one thread-constructor failure without exhausting machine resources.
        if (fail_start)
            throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again), "injected thread start");
        workers.emplace_back(std::move(task));
        ++counts.spawned;
        completed.push_back(std::move(done));
    }

public:
    InitializationWorkers(GateFault mode, GateCounts& observations, HANDLE event)
        : fault(mode), counts(observations), entered_event(event)
    {
        workers.reserve(8);
        completed.reserve(8);
    }
    InitializationWorkers(const InitializationWorkers&) = delete;
    InitializationWorkers& operator=(const InitializationWorkers&) = delete;
    void ReleaseOwner()
    {
        if (!released) { release_owner.set_value(); released = true; }
    }
    ~InitializationWorkers()
    {
        // Release before joining, including partial construction and failed checks.
        // A broken gate/callback can still refuse to finish; the parent owns a
        // bounded child process rather than detaching workers that reference us.
        ReleaseOwner();
        for (auto& worker : workers) {
            if (worker.joinable()) { worker.join(); ++counts.joined; }
        }
    }
    void Run()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        Start([&] {
            if (fault == GateFault::OWNER_ENTRY_TIMEOUT) release.wait();
            const bool ready = concurrent.Run([&] {
                ++counts.callbacks;
                owner_entered.set_value();
                check(SetEvent(entered_event) != FALSE, "signal owner entry");
                if (fault == GateFault::UNCOOPERATIVE) Sleep(INFINITE);
                release.wait();
                published_payload = 417;
                return true;
            });
            if (!ready || published_payload != 417) ++counts.incorrect;
            ++counts.returned;
        });
        const auto owner_deadline = fault == GateFault::OWNER_ENTRY_TIMEOUT ?
            std::chrono::steady_clock::now() + std::chrono::milliseconds(50) : deadline;
        check(entered.wait_until(owner_deadline) == std::future_status::ready, "owner entry deadline");
        for (int i = 0; i < 7; ++i) {
            if (i == 3 && fault == GateFault::WAITER_START_TIMEOUT) break;
            Start([&] {
                ++counts.started;
                const bool ready = concurrent.Run([&] { ++counts.callbacks; return false; });
                if (!ready || published_payload != 417) ++counts.incorrect;
                ++counts.returned;
            }, i == 3 && fault == GateFault::THREAD_START_FAILURE);
        }
        const auto start_deadline = fault == GateFault::WAITER_START_TIMEOUT ?
            std::chrono::steady_clock::now() + std::chrono::milliseconds(50) : deadline;
        while (counts.started != 7 && std::chrono::steady_clock::now() < start_deadline)
            std::this_thread::yield();
        check(counts.started == 7, "waiter start deadline");
        // Started counts attempts before Run, not admission to its wait. Give
        // callers a scheduling opportunity while the owner remains blocked.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(counts.returned == 0 && counts.callbacks == 1,
            "concurrent callers cannot return before owner publishes");
        ReleaseOwner();
        for (auto& done : completed) {
            check(done.wait_until(deadline) == std::future_status::ready, "worker completion deadline");
            done.get();
        }
        check(counts.returned == 8 && counts.callbacks == 1 && counts.incorrect == 0,
            "all concurrent callers observe the same fully initialized payload");
    }
};

void run_gate_concurrency(GateFault fault, GateCounts& counts, HANDLE entered_event)
{
    InitializationWorkers workers(fault, counts, entered_event);
    workers.Run();
}


void run_gate_child(const wchar_t* scenario, bool expect_timeout = false)
{
    wchar_t executable[32768]{};
    const auto length = GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    check(length && length < ARRAYSIZE(executable), "F203: complete child executable path");
    static unsigned sequence = 0;
    const auto event_name = L"Local\\BunnyF203_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(++sequence);
    GateHandle entered(CreateEventW(nullptr, TRUE, FALSE, event_name.c_str()));
    check(entered.value != nullptr && GetLastError() != ERROR_ALREADY_EXISTS, "F203: reserve child entry event");
    GateHandle job(CreateJobObjectW(nullptr, nullptr));
    check(job.value != nullptr, "F203: create owned child job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    check(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE,
        "F203: configure child lifetime");
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --initialization-gate-case " + scenario + L" " + event_name + L" --fixture-isolated";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE); startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(executable, &command[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
        nullptr, nullptr, &startup, &process) != FALSE, "F203: launch child scenario");
    GateHandle child(process.hProcess), thread(process.hThread);
    if (!AssignProcessToJobObject(job.value, child.value) || ResumeThread(thread.value) == DWORD(-1)) {
        TerminateProcess(child.value, EXIT_FAILURE); WaitForSingleObject(child.value, 5000);
        throw std::runtime_error("F203: attach and resume child scenario");
    }
    const bool reached = !expect_timeout || WaitForSingleObject(entered.value, 5000) == WAIT_OBJECT_0;
    const auto waited = WaitForSingleObject(child.value, expect_timeout ? 150 : 10000);
    if (waited != WAIT_OBJECT_0) {
        check(TerminateJobObject(job.value, ERROR_TIMEOUT) != FALSE, "F203: terminate stuck owned child");
        check(WaitForSingleObject(child.value, 5000) == WAIT_OBJECT_0, "F203: collect terminated child");
    }
    DWORD code = EXIT_FAILURE;
    check(GetExitCodeProcess(child.value, &code) != FALSE, "F203: collect child exit status");
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    const auto cleanup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    do {
        check(QueryInformationJobObject(job.value, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr) != FALSE,
            "F203: query owned process cleanup");
        if (!accounting.ActiveProcesses) break;
        Sleep(1);
    } while (std::chrono::steady_clock::now() < cleanup_deadline);
    std::fprintf(stderr, "F203 child %ls: exit=%lu timeout=%d active=%lu\n", scenario, code,
        waited == WAIT_TIMEOUT, accounting.ActiveProcesses);
    check(!accounting.ActiveProcesses, "F203: owned child has exited without live descendants");
    check(reached, "F203: uncooperative callback reached real gate before deadline");
    check(expect_timeout ? waited == WAIT_TIMEOUT && code == ERROR_TIMEOUT : waited == WAIT_OBJECT_0 && code == EXIT_SUCCESS,
        "F203: child scenario completed with expected cleanup/status");
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
    run_gate_child(L"normal");
}
}

int run_dx11_initialization_gate_case(const char* scenario, const char* entered_event)
{
    GateCounts counts;
    GateHandle event(OpenEventA(EVENT_MODIFY_STATE, FALSE, entered_event));
    try {
        check(event.value != nullptr, "F203: open parent entry event");
        GateFault fault;
        const char* expected = nullptr;
        if (!std::strcmp(scenario, "normal")) fault = GateFault::NONE;
        else if (!std::strcmp(scenario, "owner-timeout")) { fault = GateFault::OWNER_ENTRY_TIMEOUT; expected = "owner entry deadline"; }
        else if (!std::strcmp(scenario, "waiter-timeout")) { fault = GateFault::WAITER_START_TIMEOUT; expected = "waiter start deadline"; }
        else if (!std::strcmp(scenario, "thread-start-failure")) { fault = GateFault::THREAD_START_FAILURE; expected = "injected thread start"; }
        else if (!std::strcmp(scenario, "uncooperative")) fault = GateFault::UNCOOPERATIVE;
        else throw std::runtime_error("F203: unknown child scenario");
        bool failed_as_expected = false;
        try { run_gate_concurrency(fault, counts, event.value); }
        catch (const std::exception& error) {
            check(expected && std::strstr(error.what(), expected), "F203: precise cooperative failure diagnostic");
            failed_as_expected = true;
            std::fprintf(stderr, "F203 recovered %s: %s\n", scenario, error.what());
        }
        check(failed_as_expected == (expected != nullptr), "F203: expected local failure was observed");
        check(counts.spawned && counts.finished == counts.spawned && counts.joined == counts.spawned &&
            counts.returned == counts.spawned && counts.callbacks == 1 && counts.incorrect == 0,
            "F203: release owner and join every started worker before returning");
        std::fprintf(stderr, "F203 cleaned %s: spawned=%d finished=%d joined=%d\n",
            scenario, counts.spawned, counts.finished.load(), counts.joined);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL F203 child %s: %s\n", scenario, error.what());
        return EXIT_FAILURE;
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

void test_dx11_initialization_gate_reliability_regression()
{
    run_initialization_gate_regression();
    run_gate_child(L"owner-timeout");
    run_gate_child(L"waiter-timeout");
    run_gate_child(L"thread-start-failure");
    run_gate_child(L"uncooperative", true);
}
