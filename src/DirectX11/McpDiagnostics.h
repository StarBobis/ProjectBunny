#pragma once

#include <atomic>
#include <cstdint>
#include <cstdarg>

namespace Dx11Diagnostics {
enum class Event : uint32_t {
    Warning = 1, Info = 2, Debug = 3,
    Phase = 10, Present = 11, Attach = 12, Detach = 13, Failure = 14, Identity = 15,
};

extern std::atomic<bool> attached;

void OnDllAttach() noexcept;
void OnDllDetach(bool terminating) noexcept;
void OnPresent(unsigned frame) noexcept;
void Phase(const char* message) noexcept;
void Failure(const char* message, uint32_t code) noexcept;
bool WantsLog(unsigned level) noexcept;
void RecordLog(unsigned level, const char* format, va_list args) noexcept;
void RecordLog(unsigned level, const wchar_t* format, va_list args) noexcept;

// Initial includes and mod compilation can occur in a config reload after
// the first Present. Capture their informational messages as startup data.
class ConfigLoad {
public:
    ConfigLoad() noexcept;
    ~ConfigLoad() noexcept;
    ConfigLoad(const ConfigLoad&) = delete;
    ConfigLoad& operator=(const ConfigLoad&) = delete;
};
}
