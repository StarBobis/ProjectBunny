#include "lock.h"

#include <cstdio>
#include <cstdlib>
#include <type_traits>

static void require_critical_guard(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL critical-section guard: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

class CriticalGuardFixture
{
public:
    CRITICAL_SECTION first;
    CRITICAL_SECTION second;

    CriticalGuardFixture()
    {
        // These local locks need no process-wide hook or TLS initialization.
        InitializeCriticalSection(&first);
        InitializeCriticalSection(&second);
    }

    ~CriticalGuardFixture()
    {
        DeleteCriticalSection(&second);
        DeleteCriticalSection(&first);
    }
};

struct CriticalGuardProbe
{
    CRITICAL_SECTION* lock;
    bool acquired = false;
};

static DWORD WINAPI try_critical_guard_from_thread(void* argument)
{
    auto* probe = static_cast<CriticalGuardProbe*>(argument);
    if (TryEnterCriticalSection(probe->lock)) {
        probe->acquired = true;
        LeaveCriticalSection(probe->lock);
    }
    return 0;
}

static bool critical_guard_available_to_other_thread(CRITICAL_SECTION* lock)
{
    CriticalGuardProbe probe{lock};
    HANDLE thread = CreateThread(nullptr, 0, try_critical_guard_from_thread, &probe, 0, nullptr);
    require_critical_guard(thread != nullptr, "create independent lock probe thread");
    const DWORD result = WaitForSingleObject(thread, 2000);
    // Failure exits the regression process before the stack-local probe/lock
    // can expire underneath a worker that has not finished.
    require_critical_guard(result == WAIT_OBJECT_0, "nonblocking lock probe completes within two seconds");
    CloseHandle(thread);
    return probe.acquired;
}

static bool critical_guard_early_return(CRITICAL_SECTION* lock)
{
    CriticalSectionGuard(lock);
    require_critical_guard(!critical_guard_available_to_other_thread(lock), "guard holds lock before early return");
    return true;
}

static void critical_guard_throw(CRITICAL_SECTION* lock)
{
    CriticalSectionGuard(lock);
    require_critical_guard(!critical_guard_available_to_other_thread(lock), "guard holds lock before exception");
    throw 42;
}

void test_dx11_critical_section_guard_regression()
{
    static_assert(!std::is_copy_constructible<CriticalSectionLockGuard>::value, "guard cannot copy ownership");
    static_assert(!std::is_copy_assignable<CriticalSectionLockGuard>::value, "guard cannot copy-assign ownership");
    static_assert(!std::is_move_constructible<CriticalSectionLockGuard>::value, "guard cannot move ownership");
    static_assert(!std::is_move_assignable<CriticalSectionLockGuard>::value, "guard cannot move-assign ownership");
    std::fputs("Testing production critical-section scope guards\n", stderr);
    CriticalGuardFixture fixture;
    require_critical_guard(critical_guard_available_to_other_thread(&fixture.first), "initial local lock is available");
    {
        // Both declarations must compile in one scope with distinct names.
        CriticalSectionGuard(&fixture.first);
        CriticalSectionGuard(&fixture.second);
        require_critical_guard(!critical_guard_available_to_other_thread(&fixture.first) &&
            !critical_guard_available_to_other_thread(&fixture.second), "same-scope guards own both locks");
        {
            CriticalSectionGuard(&fixture.first);
            require_critical_guard(!critical_guard_available_to_other_thread(&fixture.first), "recursive same-thread acquisition is held");
        }
        require_critical_guard(!critical_guard_available_to_other_thread(&fixture.first),
            "inner recursive destructor does not release outer ownership");
    }
    require_critical_guard(critical_guard_available_to_other_thread(&fixture.first) &&
        critical_guard_available_to_other_thread(&fixture.second), "normal scope exit balances both locks and recursion count");
    require_critical_guard(critical_guard_early_return(&fixture.first) &&
        critical_guard_available_to_other_thread(&fixture.first), "early return releases ownership");
    bool caught = false;
    try {
        critical_guard_throw(&fixture.first);
    } catch (int value) {
        caught = value == 42;
    }
    require_critical_guard(caught && critical_guard_available_to_other_thread(&fixture.first),
        "exception unwinding releases ownership for another thread");
    // Diagnostic stack tracking is process-hook/TLS dependent and deliberately
    // not enabled by this local-lock regression. Production guard entry still
    // goes through _EnterCriticalSectionPretty, matching the existing API.
}
