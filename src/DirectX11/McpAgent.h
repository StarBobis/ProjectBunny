#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

class HackerDevice;
class HackerContext;
struct DrawCallInfo;
struct ID3D11DeviceChild;
struct ID3D11Resource;

namespace Dx11Mcp {
extern std::atomic<bool> enabled;
extern std::atomic<bool> capture_armed;
extern std::atomic<bool> texture_dhash_armed;

void OnPresent(HackerDevice* device, HackerContext* context);
void OnContextDestroyed(HackerContext* context) noexcept;
void OnOriginalDraw(HackerContext* context, const DrawCallInfo& call);
void OnDraw(HackerContext* context, const DrawCallInfo& call, bool injected = false, bool after = false);
void RememberShader(ID3D11DeviceChild* shader, uint64_t hash, const wchar_t* stage,
    const void* bytes, size_t size, const wchar_t* name = nullptr);
}
