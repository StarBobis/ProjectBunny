#pragma once

#include <objbase.h>

// COM work happens on arbitrary game threads. Keep initialization and its
// matching release on that thread, outside DllMain/TLS teardown. An existing
// STA is usable as-is; a successful S_FALSE still owns one matching release.
class ComScope
{
public:
    ComScope() noexcept : result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope()
    {
        if (SUCCEEDED(result))
            CoUninitialize();
    }

    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

    explicit operator bool() const noexcept
    {
        return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result;
};
