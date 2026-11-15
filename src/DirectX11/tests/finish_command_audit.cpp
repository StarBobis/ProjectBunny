#include "slot_range_test_support.h"
#include "FrameAnalysis.h"
#include <filesystem>
#include <fstream>

extern "C" size_t dx11_audit_context1_vtable_entries(void);
extern "C" size_t dx11_audit_finish_command_slot(void);

namespace {
using namespace dx11_slot_test;
class FinishFailureScope;
FinishFailureScope* active_finish = nullptr;
unsigned injected_finish_calls = 0;
HRESULT WINAPI fail_finish(ID3D11DeviceContext1*, BOOL, ID3D11CommandList**);

class FinishFailureScope {
    ID3D11DeviceContext1* context;
    void** original = nullptr;
    std::vector<void*> shadow;
    bool installed = false;
public:
    explicit FinishFailureScope(ID3D11DeviceContext1* value) : context(value)
    {
        require_batch(!active_finish, "F167: only one process-local fault injection is active");
        std::memcpy(&original, context, sizeof(original));
        shadow.assign(original, original + dx11_audit_context1_vtable_entries());
        shadow.at(dx11_audit_finish_command_slot()) = reinterpret_cast<void*>(&fail_finish);
        void** table = shadow.data();
        std::memcpy(context, &table, sizeof(table));
        installed = true; active_finish = this; injected_finish_calls = 0;
    }
    void restore() noexcept
    {
        if (installed) { std::memcpy(context, &original, sizeof(original)); installed = false; }
        active_finish = nullptr;
    }
    ~FinishFailureScope() { restore(); }
};
HRESULT WINAPI fail_finish(ID3D11DeviceContext1*, BOOL, ID3D11CommandList**)
{
    ++injected_finish_calls;
    // Restore before returning so no native method executes through a partial
    // public-interface vtable. The documented failed output is left untouched.
    active_finish->restore();
    return E_OUTOFMEMORY;
}

class FinishOutputScope {
    std::wstring old_path = G->ANALYSIS_PATH, old_title = G->additionalForegroundWindowTitle;
    bool old_analysis = G->analyse_frame, old_registered = G->frame_analysis_registered, old_foreground = G->check_foreground_window;
    int old_hunting = G->hunting;
    FrameAnalysisOptions old_current = G->cur_analyse_options, old_default = G->def_analyse_options;
public:
    std::filesystem::path path;
    FinishOutputScope()
    {
        wchar_t executable[MAX_PATH]{};
        require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "F167: locate fixture");
        const auto base = std::filesystem::weakly_canonical(std::filesystem::path(executable).parent_path());
        bool build = false; for (const auto& part : base) build |= _wcsicmp(part.c_str(), L"build") == 0;
        require_batch(build, "F167: output stays under build");
        path = base / L"finish-audit";
        require_batch(!std::filesystem::exists(path) && std::filesystem::create_directory(path), "F167: exclusive output");
        wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, path.c_str());
        G->analyse_frame = G->frame_analysis_registered = true; G->check_foreground_window = true;
        G->additionalForegroundWindowTitle.clear(); G->hunting = HUNTING_MODE_DISABLED;
        G->cur_analyse_options = G->def_analyse_options = FrameAnalysisOptions::INVALID;
    }
    ~FinishOutputScope()
    {
        wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, old_path.c_str()); G->additionalForegroundWindowTitle = old_title;
        G->analyse_frame = old_analysis; G->frame_analysis_registered = old_registered; G->check_foreground_window = old_foreground;
        G->hunting = old_hunting; G->cur_analyse_options = old_current; G->def_analyse_options = old_default;
        std::error_code error; std::filesystem::remove_all(path, error);
    }
    unsigned buffers(const std::vector<unsigned char>* expected = nullptr)
    {
        unsigned count = 0;
        for (const auto& file : std::filesystem::recursive_directory_iterator(path)) {
            if (file.path().extension() != L".buf") continue;
            ++count;
            if (expected) {
                std::ifstream input(file.path(), std::ios::binary);
                std::vector<unsigned char> actual((std::istreambuf_iterator<char>(input)), {});
                require_batch(actual == *expected, "F167: recovered command list dumps the actual recorded bytes");
            }
        }
        return count;
    }
};
}

void test_dx11_finish_failure_regression()
{
    using namespace dx11_slot_test;
    BatchDevice device; FinishOutputScope output;
    auto previous = std::move(device.wrapper);
    auto immediate = std::make_unique<FrameAnalysisContext>(device.native.Get(), device.context.Get());
    immediate->SetHackerDevice(device.device);
    auto* executor = immediate.get(); device.wrapper = std::move(immediate);
    device.device->SetHackerContext(executor); device.state.mHackerContext = executor;
    try {
        ComPtr<ID3D11DeviceContext1> native_deferred;
        require_batch(SUCCEEDED(device.native->CreateDeferredContext1(0, &native_deferred)), "F167: actual deferred context");
        FrameAnalysisContext deferred(device.native.Get(), native_deferred.Get()); deferred.SetHackerDevice(device.device);
        ComPtr<ID3D11CommandList> stale;
        require_batch(SUCCEEDED(native_deferred->FinishCommandList(FALSE, &stale)), "F167: valid prior output value");
        std::vector<unsigned char> expected(64); for (unsigned i=0;i<expected.size();++i) expected[i]=static_cast<unsigned char>(i*3+7);
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth=static_cast<UINT>(expected.size());
        D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=expected.data();
        ComPtr<ID3D11Buffer> source;
        require_batch(SUCCEEDED(device.native->CreateBuffer(&desc,&initial,&source)), "F167: real dump source");
        deferred.FrameAnalysisDump(source.Get(),FrameAnalysisOptions::FMT_BUF_BIN,L"f167-recorded",DXGI_FORMAT_UNKNOWN,0,0);
        ID3D11CommandList* output_slot=stale.Get();
        std::fputs("F167: inject failed Finish with an unchanged valid stale output\n",stderr); std::fflush(stderr);
        { FinishFailureScope failure(native_deferred.Get());
          require_batch(deferred.FinishCommandList(FALSE,&output_slot)==E_OUTOFMEMORY && injected_finish_calls==1 && output_slot==stale.Get(),
            "F167: real wrapper preserves the injected failure and unspecified output"); }
        executor->ExecuteCommandList(stale.Get(),FALSE);
        require_batch(output.buffers()==0,"F167: failed Finish must not publish pending dumps under a stale command-list output");
        ComPtr<ID3D11CommandList> recovered;
        require_batch(SUCCEEDED(deferred.FinishCommandList(FALSE,&recovered)) && recovered,"F167: normal Finish recovers after injected failure");
        executor->ExecuteCommandList(recovered.Get(),FALSE);
        require_batch(output.buffers(&expected)>0,"F167: deferred dump is retained until the successful recording executes");
    } catch (...) {
        device.wrapper=std::move(previous); device.device->SetHackerContext(device.wrapper.get()); device.state.mHackerContext=device.wrapper.get();
        throw;
    }
    device.wrapper=std::move(previous); device.device->SetHackerContext(device.wrapper.get()); device.state.mHackerContext=device.wrapper.get();
}
