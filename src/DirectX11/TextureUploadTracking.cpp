#include "TextureUploadTracking.h"
#include "ResourceHash.h"
#include <atomic>
#include <new>
#include <algorithm>

namespace {
using Microsoft::WRL::ComPtr;
const GUID TextureUploadJournalGuid = {0x6bd188d4,0x3ca1,0x4627,{0xac,0xb7,0xb6,0x03,0xe3,0x49,0x54,0x42}};
struct __declspec(uuid("A698EC40-F52C-4D37-A3E7-93207CB83486")) TextureUploadJournalInterface : IUnknown {
    virtual const std::vector<TextureUploadEvent>& Events() const noexcept = 0;
    virtual bool UnknownPrefix() const noexcept = 0;
};
class TextureUploadJournal final : public TextureUploadJournalInterface {
    std::atomic<ULONG> refs{1};
    std::vector<TextureUploadEvent> events;
    bool unknown;
public:
    TextureUploadJournal(std::vector<TextureUploadEvent>&& input, bool prefix) noexcept : events(std::move(input)), unknown(prefix) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override
    {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid != IID_IUnknown && iid != __uuidof(TextureUploadJournalInterface)) return E_NOINTERFACE;
        *output = static_cast<TextureUploadJournalInterface*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG value = --refs; if (!value) delete this; return value; }
    const std::vector<TextureUploadEvent>& Events() const noexcept override { return events; }
    bool UnknownPrefix() const noexcept override { return unknown; }
};

bool TextureDesc(ID3D11Resource* resource, D3D11_TEXTURE2D_DESC& desc) noexcept
{
    if (!resource) return false;
    D3D11_RESOURCE_DIMENSION dimension;
    resource->GetType(&dimension);
    if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return false;
    static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
    return desc.MipLevels && desc.ArraySize;
}
bool Subresource(const D3D11_TEXTURE2D_DESC& desc, UINT index) noexcept
{
    return uint64_t(index) < uint64_t(desc.MipLevels) * desc.ArraySize;
}
UINT MipDimension(UINT value, UINT mip) noexcept { return (std::max)(1u, value >> (std::min)(mip, 31u)); }
bool ValidBox(const D3D11_BOX* box, const D3D11_TEXTURE2D_DESC& desc, UINT index) noexcept
{
    if (!box) return true;
    const UINT mip = index % desc.MipLevels;
    return box->left < box->right && box->top < box->bottom && box->front == 0 && box->back == 1 &&
        box->right <= MipDimension(desc.Width, mip) && box->bottom <= MipDimension(desc.Height, mip);
}
void Apply(const TextureUploadEvent& event)
{
    switch (event.kind) {
    case TextureUploadEventKind::Update:
        UpdateTextureSubresourceState(event.destination.Get(), event.destination_subresource, event.full, event.discard); break;
    case TextureUploadEventKind::Copy:
        UpdateTextureResourceStateFromCopy(event.destination.Get(), event.source.Get()); break;
    case TextureUploadEventKind::CopyRegion:
        UpdateTextureSubresourceStateFromCopy(event.destination.Get(), event.destination_subresource, event.x, event.y, event.z,
            event.source.Get(), event.source_subresource, event.has_box ? &event.box : nullptr, event.discard ? D3D11_COPY_DISCARD : 0); break;
    case TextureUploadEventKind::Resolve:
        UpdateTextureSubresourceStateFromResolve(event.destination.Get(), event.destination_subresource, event.source.Get(), event.source_subresource); break;
    case TextureUploadEventKind::Generate:
        UpdateTextureGenerateMipsState(event.view.Get()); break;
    }
}
ComPtr<TextureUploadJournalInterface> ReadJournal(ID3D11CommandList* list) noexcept
{
    ComPtr<TextureUploadJournalInterface> journal;
    if (!list) return journal;
    ComPtr<IUnknown> unknown;
    UINT bytes = sizeof(IUnknown*);
    if (SUCCEEDED(list->GetPrivateData(TextureUploadJournalGuid, &bytes, unknown.GetAddressOf())) && bytes == sizeof(IUnknown*) && unknown)
        unknown.As(&journal);
    return journal;
}
} // namespace

void TextureUploadTracker::Forget(ID3D11DeviceContext1* context) noexcept
{
    if (context && context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) InvalidateTextureUploadStates();
    else { pending.clear(); unknown_prefix = true; }
}

void TextureUploadTracker::Submit(ID3D11DeviceContext1* context, TextureUploadEvent&& event) noexcept
{
    if (!context) return;
    try {
        if (context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) Apply(event);
        else pending.push_back(std::move(event));
    } catch (...) {
        if (context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) InvalidateTextureUploadStates();
        else { pending.clear(); unknown_prefix = true; }
    }
}

void TextureUploadTracker::Update(ID3D11DeviceContext1* context, ID3D11Resource* destination, UINT index,
    const D3D11_BOX* box, bool discard) noexcept
{
    D3D11_TEXTURE2D_DESC desc{};
    if (!TextureDesc(destination, desc) || !Subresource(desc, index) || !ValidBox(box, desc, index)) return;
    TextureUploadEvent event; event.destination = destination; event.destination_subresource = index;
    event.full = box == nullptr; event.discard = discard;
    Submit(context, std::move(event));
}

void TextureUploadTracker::Copy(ID3D11DeviceContext1* context, ID3D11Resource* destination, ID3D11Resource* source) noexcept
{
    D3D11_TEXTURE2D_DESC dst{}, src{};
    if (!TextureDesc(destination, dst) || !TextureDesc(source, src) || destination == source ||
        dst.Width != src.Width || dst.Height != src.Height || dst.MipLevels != src.MipLevels ||
        dst.ArraySize != src.ArraySize || dst.SampleDesc.Count != src.SampleDesc.Count || dst.SampleDesc.Quality != src.SampleDesc.Quality) return;
    TextureUploadEvent event; event.kind = TextureUploadEventKind::Copy; event.destination = destination; event.source = source;
    Submit(context, std::move(event));
}

void TextureUploadTracker::CopyRegion(ID3D11DeviceContext1* context, ID3D11Resource* destination, UINT dst_index,
    UINT x, UINT y, UINT z, ID3D11Resource* source, UINT src_index, const D3D11_BOX* box, UINT flags, bool allow_overlap) noexcept
{
    D3D11_TEXTURE2D_DESC dst{}, src{};
    if (!TextureDesc(destination, dst) || !TextureDesc(source, src) || !Subresource(dst, dst_index) ||
        !Subresource(src, src_index) || !ValidBox(box, src, src_index) || z) return;
    if (destination == source && dst_index == src_index) {
        if (!allow_overlap || !context) return;
        ComPtr<ID3D11Device> device; context->GetDevice(&device);
        D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
        if (!device || FAILED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options))) ||
            !options.CopyWithOverlap) return;
    }
    const UINT width = box ? box->right - box->left : MipDimension(src.Width, src_index % src.MipLevels);
    const UINT height = box ? box->bottom - box->top : MipDimension(src.Height, src_index % src.MipLevels);
    const UINT dst_width = MipDimension(dst.Width, dst_index % dst.MipLevels), dst_height = MipDimension(dst.Height, dst_index % dst.MipLevels);
    if (x > dst_width || width > dst_width - x || y > dst_height || height > dst_height - y) return;
    TextureUploadEvent event; event.kind = TextureUploadEventKind::CopyRegion; event.destination = destination; event.source = source;
    event.destination_subresource = dst_index; event.source_subresource = src_index; event.x = x; event.y = y; event.z = z;
    event.has_box = box != nullptr; if (box) event.box = *box;
    event.discard = (flags & D3D11_COPY_DISCARD) != 0;
    Submit(context, std::move(event));
}

void TextureUploadTracker::Resolve(ID3D11DeviceContext1* context, ID3D11Resource* destination, UINT dst_index,
    ID3D11Resource* source, UINT src_index, DXGI_FORMAT format) noexcept
{
    D3D11_TEXTURE2D_DESC dst{}, src{};
    if (!TextureDesc(destination, dst) || !TextureDesc(source, src) || !Subresource(dst, dst_index) || !Subresource(src, src_index) ||
        src.SampleDesc.Count <= 1 || dst.SampleDesc.Count != 1 || dst.Usage != D3D11_USAGE_DEFAULT ||
        MipDimension(dst.Width, dst_index % dst.MipLevels) != MipDimension(src.Width, src_index % src.MipLevels) ||
        MipDimension(dst.Height, dst_index % dst.MipLevels) != MipDimension(src.Height, src_index % src.MipLevels)) return;
    ComPtr<ID3D11Device> device; source->GetDevice(&device);
    UINT support = 0;
    if (!device || FAILED(device->CheckFormatSupport(format, &support)) || !(support & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE)) return;
    TextureUploadEvent event; event.kind = TextureUploadEventKind::Resolve; event.destination = destination; event.source = source;
    event.destination_subresource = dst_index; event.source_subresource = src_index; Submit(context, std::move(event));
}

void TextureUploadTracker::Generate(ID3D11DeviceContext1* context, ID3D11ShaderResourceView* view) noexcept
{
    if (!view) return;
    ComPtr<ID3D11Resource> resource; view->GetResource(&resource);
    D3D11_TEXTURE2D_DESC desc{}; D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; view->GetDesc(&srv);
    if (!TextureDesc(resource.Get(), desc) || desc.SampleDesc.Count != 1 ||
        !(desc.BindFlags & D3D11_BIND_RENDER_TARGET) || !(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) ||
        !(desc.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS)) return;
    ComPtr<ID3D11Device> device; resource->GetDevice(&device);
    UINT support = 0;
    if (!device || FAILED(device->CheckFormatSupport(srv.Format, &support)) || !(support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN)) return;
    TextureUploadEvent event; event.kind = TextureUploadEventKind::Generate; event.view = view;
    Submit(context, std::move(event));
}

void TextureUploadTracker::Finish(ID3D11CommandList* list) noexcept
{
    if (list) {
        auto* journal = new (std::nothrow) TextureUploadJournal(std::move(pending), unknown_prefix);
        if (journal) {
            list->SetPrivateDataInterface(TextureUploadJournalGuid, journal);
            journal->Release();
        }
    }
    pending.clear(); unknown_prefix = false;
}

void TextureUploadTracker::Execute(ID3D11DeviceContext1* context, ID3D11CommandList* list) noexcept
{
    if (!context || !list) return;
    auto journal = ReadJournal(list);
    try {
        if (context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) {
            if (!journal || journal->UnknownPrefix()) InvalidateTextureUploadStates();
            if (journal) for (const auto& event : journal->Events()) Apply(event);
        } else {
            if (!journal || journal->UnknownPrefix()) { pending.clear(); unknown_prefix = true; }
            if (journal) pending.insert(pending.end(), journal->Events().begin(), journal->Events().end());
        }
    } catch (...) {
        if (context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) InvalidateTextureUploadStates();
        else { pending.clear(); unknown_prefix = true; }
    }
}
