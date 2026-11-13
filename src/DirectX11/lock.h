#pragma once

#include <windows.h>
#include <vector>

// This version of EnterCriticalSection will use the function and line number
// in any lock stacks dumped when potential deadlock hazards are detected:
#define EnterCriticalSectionPretty(lock) \
	_EnterCriticalSectionPretty(lock, __FUNCTION__, __LINE__)
void _EnterCriticalSectionPretty(CRITICAL_SECTION *lock, char *function, int line);

// Use this when initialising a critical section in 3DMigoto to give it a nice
// name in lock stack dumps rather than using its address.
//
// **AVOID CALLING THIS FROM GLOBAL CONSTRUCTORS**
// https://yosefk.com/c++fqa/ctors.html#fqa-10.12
#define InitializeCriticalSectionPretty(lock) \
	_InitializeCriticalSectionPretty(lock, #lock)
void _InitializeCriticalSectionPretty(CRITICAL_SECTION *lock, char *lock_name);

void enable_lock_dependency_checks();
struct held_lock_info {
	CRITICAL_SECTION *lock;
	uintptr_t ret;
	size_t stack_hash;
	char *function;
	int line;
};
typedef std::vector<held_lock_info> LockStack;

// Scope ownership of the existing diagnostic-aware critical-section entry.
class CriticalSectionLockGuard
{
public:
	CriticalSectionLockGuard(CRITICAL_SECTION* lock, char* function, int line)
		: m_lock(lock)
	{
		_EnterCriticalSectionPretty(lock, function, line);
	}

	~CriticalSectionLockGuard() noexcept
	{
		LeaveCriticalSection(m_lock);
	}

	CriticalSectionLockGuard(const CriticalSectionLockGuard&) = delete;
	CriticalSectionLockGuard& operator=(const CriticalSectionLockGuard&) = delete;
	CriticalSectionLockGuard(CriticalSectionLockGuard&&) = delete;
	CriticalSectionLockGuard& operator=(CriticalSectionLockGuard&&) = delete;

private:
	CRITICAL_SECTION* m_lock;
};

// Expand __LINE__ before token pasting so separate guards can share a scope.
#define MIGOTO_LOCK_CONCAT_DETAIL(a, b) a##b
#define MIGOTO_LOCK_CONCAT(a, b) MIGOTO_LOCK_CONCAT_DETAIL(a, b)
#define CriticalSectionGuard(lock) \
	CriticalSectionLockGuard MIGOTO_LOCK_CONCAT(lock_critical_section_, __LINE__)(lock, __FUNCTION__, __LINE__)
