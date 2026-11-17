#include "D3D11DebugInfo.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#if !_DEBUG_LAYER
#error This isolated audit must compile the production debug-layer helper.
#endif

extern "C" size_t dx11_audit_report_live_device_slot(void);

namespace {
using Microsoft::WRL::ComPtr;
void require_debug(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

class NativeDebugRuntime {
    HMODULE module = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
public:
    PFN_D3D11_CREATE_DEVICE create = module ? reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(module, "D3D11CreateDevice")) : nullptr;
    NativeDebugRuntime() { require_debug(module && create, "F292: real system D3D11 creation export"); }
    ~NativeDebugRuntime() { FreeLibrary(module); }
};

ULONG native_references(IUnknown* value)
{
    const auto references = value->AddRef();
    value->Release();
    return references;
}

using DebugQuery = HRESULT (STDMETHODCALLTYPE *)(ID3D11Debug*, REFIID, void**);
using DebugReport = HRESULT (STDMETHODCALLTYPE *)(ID3D11Debug*, D3D11_RLDO_FLAGS);
class DebugObservation;
DebugObservation* active_debug_observation = nullptr;
HRESULT STDMETHODCALLTYPE observe_debug_query(ID3D11Debug*, REFIID, void**);
HRESULT STDMETHODCALLTYPE observe_debug_report(ID3D11Debug*, D3D11_RLDO_FLAGS);

class DebugObservation {
    void** table;
    void* saved_query;
    void* saved_report;
    static void replace(void** slot, void* replacement)
    {
        DWORD old = 0;
        require_debug(VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old) != FALSE, "F292: protect existing native method slot");
        *slot = replacement;
        DWORD ignored = 0;
        if (!VirtualProtect(slot, sizeof(*slot), old, &ignored)) std::abort();
    }
public:
    ID3D11Debug* target;
    DebugQuery query;
    DebugReport report;
    unsigned queue_queries = 0, injected_failures = 0, reports = 0;
    bool fail_next_queue = false;
    D3D11_RLDO_FLAGS reported_flags{};
    explicit DebugObservation(ID3D11Debug* debug) : target(debug)
    {
        require_debug(!active_debug_observation, "F292: one scoped observer");
        table = *reinterpret_cast<void***>(debug);
        saved_query = table[0]; saved_report = table[dx11_audit_report_live_device_slot()];
        query = reinterpret_cast<DebugQuery>(saved_query);
        report = reinterpret_cast<DebugReport>(saved_report);
        active_debug_observation = this;
        try {
            replace(table, reinterpret_cast<void*>(observe_debug_query));
            replace(table + dx11_audit_report_live_device_slot(), reinterpret_cast<void*>(observe_debug_report));
        } catch (...) {
            replace(table, saved_query); active_debug_observation = nullptr; throw;
        }
    }
    ~DebugObservation()
    {
        try {
            replace(table + dx11_audit_report_live_device_slot(), saved_report);
            replace(table, saved_query);
        } catch (...) { std::abort(); }
        active_debug_observation = nullptr;
    }
};

HRESULT STDMETHODCALLTYPE observe_debug_query(ID3D11Debug* self, REFIID iid, void** output)
{
    auto& observation = *active_debug_observation;
    if (self == observation.target && iid == __uuidof(ID3D11InfoQueue)) {
        ++observation.queue_queries;
        if (observation.fail_next_queue) {
            observation.fail_next_queue = false;
            ++observation.injected_failures;
            *output = nullptr;
            return E_NOINTERFACE;
        }
    }
    return observation.query(self, iid, output);
}
HRESULT STDMETHODCALLTYPE observe_debug_report(ID3D11Debug* self, D3D11_RLDO_FLAGS flags)
{
    auto& observation = *active_debug_observation;
    if (self == observation.target) {
        ++observation.reports;
        observation.reported_flags = flags;
    }
    return observation.report(self, flags);
}
}

void test_dx11_debug_layer_interfaces_regression()
{
    NativeDebugRuntime runtime;
    ComPtr<ID3D11Device> normal;
    ComPtr<ID3D11DeviceContext> normal_context;
    require_debug(SUCCEEDED(runtime.create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &normal, nullptr, &normal_context)), "F292: real non-debug WARP device");
    require_debug(normal->GetCreationFlags() == 0, "F292: native non-debug creation flags remain unchanged");
    ComPtr<ID3D11Debug> absent;
    const auto missing = normal.As(&absent);
    require_debug(missing == E_NOINTERFACE && !absent, "F292: native non-debug device lacks ID3D11Debug");
    const auto normal_references = native_references(normal.Get());
    ShowDebugInfo(nullptr);
    std::fprintf(stderr, "F292 _DEBUG_LAYER=1: native non-debug QI=%08lx; calling production helper\n", static_cast<unsigned long>(missing));
    std::fflush(stderr);
    ShowDebugInfo(normal.Get());
    require_debug(native_references(normal.Get()) == normal_references, "F292: failed debug QI leaves native references unchanged");

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const auto created = runtime.create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
    if (created == DXGI_ERROR_SDK_COMPONENT_MISSING) {
        std::fprintf(stderr, "SKIP F292 debug-success/queue-failure: native debug components missing, hr=%08lx\n",
            static_cast<unsigned long>(created));
        return;
    }
    require_debug(SUCCEEDED(created), "F292: debug WARP creation failed for a reason other than missing SDK components");
    require_debug((device->GetCreationFlags() & D3D11_CREATE_DEVICE_DEBUG) != 0, "F292: native debug device was requested explicitly by the audit");
    ComPtr<ID3D11Debug> debug;
    ComPtr<ID3D11InfoQueue> queue;
    require_debug(SUCCEEDED(device.As(&debug)) && SUCCEEDED(debug.As(&queue)), "F292: genuine native debug and infoqueue interfaces");
    require_debug(SUCCEEDED(debug->ReportLiveDeviceObjects(D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL)), "F292: warm native live-object reporting");
    const auto debug_references = native_references(debug.Get());
    const auto queue_references = native_references(queue.Get());
    DebugObservation observation(debug.Get());
    queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, FALSE);
    queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, FALSE);
    for (unsigned i = 0; i < 8; ++i) ShowDebugInfo(device.Get());
    require_debug(queue->GetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION) && queue->GetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR),
        "F292: successful helper preserves both native break settings");
    require_debug(observation.queue_queries >= 8 && observation.reports == 8 &&
        observation.reported_flags == (D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL), "F292: actual queue queries and native live reporting remain active");
    require_debug(native_references(debug.Get()) == debug_references && native_references(queue.Get()) == queue_references,
        "F292: repeated successful calls release both queried native interfaces");
    observation.fail_next_queue = true;
    ShowDebugInfo(device.Get());
    require_debug(observation.injected_failures == 1 && !observation.fail_next_queue && observation.reports == 9,
        "F292: secondary QI failure is reached and still reports real live objects");
    require_debug(native_references(debug.Get()) == debug_references && native_references(queue.Get()) == queue_references,
        "F292: secondary QI failure releases its successful debug query");
    queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, FALSE);
    queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, FALSE);
    std::fprintf(stderr, "PASS F292 native debug-success: queries=%u reports=%u injected=%u debug_refs=%lu queue_refs=%lu\n",
        observation.queue_queries, observation.reports, observation.injected_failures, debug_references, queue_references);
}
