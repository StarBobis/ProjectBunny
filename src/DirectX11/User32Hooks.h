#pragma once

#include <windows.h>
#include <atomic>
#include <cstddef>
#include <type_traits>
#include "ReShade/hook.hpp"

struct User32Hook {
    const char* name;
    reshade::hook hook;
    void (*publish)(void*);
};

template<auto& Original, typename Fn>
User32Hook BindUser32Hook(const char* name, Fn replacement)
{
    using Pointer = decltype(Original.load());
    static_assert(std::is_same_v<Pointer, Fn>);
    return { name, {nullptr, nullptr, reinterpret_cast<void*>(replacement)},
        [](void* trampoline) { Original.store(reinterpret_cast<Pointer>(trampoline)); } };
}

bool InstallUser32Hooks(User32Hook* hooks, size_t count);
void OpenUser32HooksLog(HMODULE module);
void RemoveUser32Hooks();
