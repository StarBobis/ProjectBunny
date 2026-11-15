// Compile the real component exactly once in this regression host. No global
// Windows hooks are installed; only the native Delete boundary is counted and
// immediately delegated to the real native operation.
#include "../lock.cpp"
#include <stdexcept>

namespace {
decltype(_DeleteCriticalSection) audit_native_delete = nullptr;
unsigned audit_native_deletes = 0;
void WINAPI audit_counted_delete(CRITICAL_SECTION* lock)
{
    ++audit_native_deletes;
    audit_native_delete(lock);
}
void check_lock_audit(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
}

void test_dx11_lock_cleanup_regression()
{
    check_lock_audit(lock_graph.empty() && cached_stacks.empty() && reported_stacks.empty(),
        "F115: diagnostic state must be isolated before cleanup testing");
    const DWORD previous_tls = tls_idx;
    const DWORD owned_tls = TlsAlloc();
    check_lock_audit(owned_tls != TLS_OUT_OF_INDEXES, "F115: allocate actual production TLS slot");
    tls_idx = owned_tls;
    auto* thread = get_tls();
    InitializeCriticalSection(&graph_lock);
    CRITICAL_SECTION target{}, retained{};
    InitializeCriticalSection(&target); InitializeCriticalSection(&retained);
    audit_native_delete = _DeleteCriticalSection;
    audit_native_deletes = 0;
    _DeleteCriticalSection = audit_counted_delete;
    cached_stacks.emplace(11, LockStack{{&target, 0, 11, nullptr, 0}});
    cached_stacks.emplace(22, LockStack{{&retained, 0, 0, nullptr, 0}, {&target, 0, 22, nullptr, 0}});
    cached_stacks.emplace(33, LockStack{{&retained, 0, 33, nullptr, 0}});
    reported_stacks.insert({11, 22, 33});
    lock_graph[&target].insert(&retained);
    lock_graph[&retained].insert(&target);
    std::fputs("F115: purge actual cached lock stacks under AddressSanitizer\n", stderr); std::fflush(stderr);
    DeleteCriticalSectionHook(&target);
    check_lock_audit(cached_stacks.size() == 1 && cached_stacks.count(33) &&
        reported_stacks.size() == 1 && reported_stacks.count(33),
        "F115: remove every target stack and report while retaining unrelated entries");
    check_lock_audit(!lock_graph.count(&target) && !lock_graph.at(&retained).count(&target) &&
        !thread->hooking_quirk_protection && audit_native_deletes == 1,
        "F115: purge graph edges, reset recursion protection and delete the native lock once");

    InitializeCriticalSection(&target);
    check_lock_audit(TlsSetValue(owned_tls, nullptr) != FALSE, "F115: exercise pre-TLS native deletion");
    DeleteCriticalSectionHook(&target);
    check_lock_audit(audit_native_deletes == 2, "F115: missing diagnostic TLS must not skip native deletion");
    check_lock_audit(TlsSetValue(owned_tls, thread) != FALSE, "F115: restore real thread state");

    InitializeCriticalSection(&target);
    thread->hooking_quirk_protection = true;
    DeleteCriticalSectionHook(&target);
    check_lock_audit(audit_native_deletes == 3 && thread->hooking_quirk_protection,
        "F115: recursion bypass still deletes natively and preserves the caller's protection flag");
    thread->hooking_quirk_protection = false;
    InitializeCriticalSection(&target);
    DeleteCriticalSectionHook(&target);
    check_lock_audit(audit_native_deletes == 4 && cached_stacks.count(33) && reported_stacks.count(33),
        "F115: deleting an untracked lock retains unrelated diagnostics");

    _DeleteCriticalSection = audit_native_delete;
    lock_graph.clear(); cached_stacks.clear(); reported_stacks.clear();
    audit_native_delete(&retained); audit_native_delete(&graph_lock);
    delete thread;
    TlsSetValue(owned_tls, nullptr); TlsFree(owned_tls); tls_idx = previous_tls;
}
