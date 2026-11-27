#pragma once

#include <d3d11_1.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class HackerContext;
class HackerDevice;
struct DrawCallInfo;

namespace Dx11Mcp::Provenance {
extern std::atomic<bool> Armed;

// Shared by live capture and provenance. The ID belongs to the COM buffer,
// not to a resource hash or a recyclable address.
uint64_t BufferId(ID3D11Buffer* buffer);
void RegisterShader(ID3D11DeviceChild*, uint64_t hash) noexcept;
void OnPresent(HackerDevice*, HackerContext*, unsigned frame) noexcept;
void OnDisable() noexcept;
void OnContextDestroyed(HackerContext*) noexcept;
std::string Request(HackerDevice*, HackerContext*, const std::vector<std::string_view>&);

void Create(HackerDevice*, ID3D11Buffer*, const D3D11_SUBRESOURCE_DATA*, UINT input_size,
    uint32_t input_crc32c) noexcept;
void Update(HackerContext*, ID3D11Resource*, UINT subresource, const D3D11_BOX*, const void*,
    bool version1, UINT flags = 0) noexcept;
void Map(HackerContext*, ID3D11Resource*, UINT subresource, D3D11_MAP,
    const D3D11_MAPPED_SUBRESOURCE&) noexcept;
void Unmap(HackerContext*, ID3D11Resource*, UINT subresource) noexcept;
void Copy(HackerContext*, ID3D11Resource* dst, UINT dst_subresource, UINT x, UINT y, UINT z,
    ID3D11Resource* src, UINT src_subresource, const D3D11_BOX*, const char* api, UINT flags = 0) noexcept;
void Discard(HackerContext*, ID3D11Resource*) noexcept;
void ViewWrite(HackerContext*, ID3D11View*, const D3D11_RECT*, UINT count, const char* api, bool discard) noexcept;
void ClearUav(HackerContext*, ID3D11UnorderedAccessView*, const char* api) noexcept;
void CopyCount(HackerContext*, ID3D11Buffer* dst, UINT offset, ID3D11UnorderedAccessView*) noexcept;
void Finish(HackerContext*, ID3D11CommandList*, bool restore) noexcept;
void Execute(HackerContext*, ID3D11CommandList*) noexcept;
void Draw(HackerContext*, const DrawCallInfo&, bool injected = false) noexcept;

// Deferred getters are not available. These setters record the wrapper's
// observed bindings, which carry a distinct confidence label in the result.
void DeferredSrvs(HackerContext*, UINT start, UINT count, ID3D11ShaderResourceView* const*) noexcept;
void DeferredUavs(HackerContext*, UINT start, UINT count, ID3D11UnorderedAccessView* const*) noexcept;
void DeferredIndex(HackerContext*, ID3D11Buffer*, DXGI_FORMAT, UINT offset) noexcept;
void DeferredVertex(HackerContext*, UINT start, UINT count, ID3D11Buffer* const*,
    const UINT* strides, const UINT* offsets) noexcept;
void DeferredShader(HackerContext*, ID3D11ComputeShader*) noexcept;
void DeferredClear(HackerContext*) noexcept;
}
