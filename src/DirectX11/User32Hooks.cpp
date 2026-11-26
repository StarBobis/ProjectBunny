#include "User32Hooks.h"
#include "ReShade/dll_log.hpp"
#include <MinHook.h>
#include <mutex>
#include <system_error>

namespace {
std::mutex installation_mutex;
bool initialized = false;
bool logging = false;
}

void OpenUser32HooksLog(HMODULE module)
{
    if (!module)
        return;
    std::lock_guard lock(installation_mutex);
    if (logging)
        return;
    wchar_t module_path[MAX_PATH];
    const auto length = GetModuleFileNameW(module, module_path, MAX_PATH);
    if (!length || length == MAX_PATH)
        return;
    const auto directory = std::filesystem::path(module_path).parent_path();
    for (unsigned i = 0; i <= 10; ++i) {
        std::error_code error;
        const auto name = i ? L"hooks_log" + std::to_wstring(i) + L".txt" : L"hooks_log.txt";
        if (reshade::log::open_log_file(directory / name, error)) {
            logging = true;
            return;
        }
        if (error.value() != ERROR_SHARING_VIOLATION)
            return;
    }
}

bool InstallUser32Hooks(User32Hook* bindings, size_t count)
{
    if (!bindings || !count)
        return false;
    std::lock_guard lock(installation_mutex);
    if (!initialized) {
        if (MH_Initialize() != MH_OK)
            return false;
        initialized = true;
    }
    const auto user32 = GetModuleHandleW(L"user32.dll");
    if (!user32)
        return false;
    for (size_t i = 0; i < count; ++i) {
        auto& binding = bindings[i];
        if (binding.hook.installed())
            continue;
        binding.hook.target = reinterpret_cast<void*>(GetProcAddress(user32, binding.name));
        const auto status = binding.hook.install();
        if (status != reshade::hook::status::success) {
            reshade::log::message(reshade::log::level::error, "Failed to prepare %s (%d)", binding.name, int(status));
            return false;
        }
        binding.publish(binding.hook.trampoline);
    }
    size_t queued = 0;
    while (queued < count && bindings[queued].hook.enable())
        ++queued;
    if (queued == count && reshade::hook::apply_queued_actions()) {
        reshade::log::message(reshade::log::level::info, "Installed %zu user32 hooks (%s)", count, bindings[0].name);
        return true;
    }
    // MinHook can apply part of a queue before an error. Keep the original
    // trampolines valid and clear this group's requests before a later retry.
    bool restored = true;
    for (size_t i = 0; i < count; ++i)
        restored &= bindings[i].hook.disable();
    if (!reshade::hook::apply_queued_actions() || !restored)
        reshade::log::message(reshade::log::level::error, "Could not restore all hooks in %s", bindings[0].name);
    return false;
}

void RemoveUser32Hooks()
{
    // Explicit DLL unload, after callers have stopped using its entry points.
    if (initialized)
        MH_Uninitialize();
}
