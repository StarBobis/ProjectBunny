#include "slot_range_test_support.h"
#include "FrameAnalysis.h"
#include "ShaderBindings.h"
#include "util.h"
#include "log.h"
#include <DDSTextureLoader.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <clocale>
#include <optional>
#include <mutex>
#include <objbase.h>

namespace {
using namespace dx11_slot_test;

// Same production background gate as dump_range_regression::DumpOutputScope.
// No controller polling thread or apartment-state workaround is introduced.
class DumpOutputScope {
    std::wstring saved_path = G->ANALYSIS_PATH;
    bool saved_analysis = G->analyse_frame, saved_registered = G->frame_analysis_registered;
    bool saved_foreground = G->check_foreground_window;
    std::wstring saved_title = G->additionalForegroundWindowTitle;
    int saved_hunting = G->hunting;
    unsigned saved_frame = G->frame_no, saved_ini = G->IniParamsReg;
    FrameAnalysisOptions saved_current = G->cur_analyse_options, saved_default = G->def_analyse_options;
    std::string saved_locale = std::setlocale(LC_CTYPE, nullptr);
public:
    std::filesystem::path path;
    DumpOutputScope()
    {
        DWORD foreground_process = 0;
        const HWND foreground = GetForegroundWindow();
        if (foreground) GetWindowThreadProcessId(foreground, &foreground_process);
        require_batch(!foreground || foreground_process != GetCurrentProcessId(),
            "PS dump: headless regression process is in the background");
        wchar_t executable[MAX_PATH] = {};
        require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "PS dump: locate actual test binary");
        const auto base = std::filesystem::weakly_canonical(std::filesystem::path(executable).parent_path());
        bool build = false;
        for (const auto& part : base) build |= _wcsicmp(part.c_str(), L"build") == 0;
        require_batch(build, "PS dump: output must remain under build directory");
        path = base / (L"shader_dump_filter_" + std::to_wstring(GetCurrentProcessId()));
        require_batch(path.parent_path() == base && !std::filesystem::exists(path) && std::filesystem::create_directory(path),
            "PS dump: create exclusive owned build subtree");
        wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, path.c_str());
        G->check_foreground_window = true;
        G->additionalForegroundWindowTitle.clear();
        G->analyse_frame = true;
        G->frame_analysis_registered = true;
        G->hunting = HUNTING_MODE_DISABLED;
        G->IniParamsReg = 11;
        G->frame_no = 100;
        G->cur_analyse_options = G->def_analyse_options = FrameAnalysisOptions::INVALID;
    }
    ~DumpOutputScope()
    {
        G->analyse_frame = saved_analysis; G->frame_analysis_registered = saved_registered;
        G->check_foreground_window = saved_foreground;
        G->additionalForegroundWindowTitle = std::move(saved_title);
        G->hunting = saved_hunting; G->frame_no = saved_frame; G->IniParamsReg = saved_ini;
        G->cur_analyse_options = saved_current; G->def_analyse_options = saved_default;
        wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, saved_path.c_str());
        std::setlocale(LC_CTYPE, saved_locale.c_str());
        std::error_code error;
        std::filesystem::remove_all(path, error); // Checked exclusive child of the actual build directory.
        if (error) std::fprintf(stderr, "PS dump cleanup: %s\n", error.message().c_str());
    }
    std::filesystem::path select(const wchar_t* name)
    {
        const auto child = path / name;
        require_batch(child.parent_path() == path && std::filesystem::create_directory(child),
            "PS dump: exclusive case directory");
        wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, child.c_str());
        return child;
    }
};

class DumpFixture {
    std::unique_ptr<HackerContext> original;
    ShaderHashType hash_type = G->shader_hash_type;
    std::wstring shader_path = G->SHADER_PATH, cache_path = G->SHADER_CACHE_PATH;
    FILE* log = LogFile;
    std::unordered_map<UINT64, std::optional<ShaderBindings>> saved_cache;
    std::vector<UINT64> owned_overrides;
    std::vector<ComPtr<ID3D11DeviceChild>> shaders;
public:
    BatchDevice device;
    DumpOutputScope output;
    FrameAnalysisContext* analysis = nullptr;
    DumpFixture()
    {
        auto context = std::make_unique<FrameAnalysisContext>(device.native.Get(), device.context.Get());
        context->SetHackerDevice(device.device);
        analysis = context.get();
        original = std::move(device.wrapper);
        device.wrapper = std::move(context);
        device.device->SetHackerContext(device.wrapper.get());
        device.state.mHackerContext = device.wrapper.get();
        G->shader_hash_type = ShaderHashType::FNV;
        G->SHADER_PATH[0] = G->SHADER_CACHE_PATH[0] = 0;
        LogFile = nullptr;
    }
    ~DumpFixture()
    {
        device.context->ClearState();
        for (const auto& shader : shaders) CleanupShaderMaps(shader.Get());
        shaders.clear();
        device.wrapper = std::move(original); // Close analysis log before output is removed.
        device.device->SetHackerContext(device.wrapper.get());
        device.state.mHackerContext = device.wrapper.get();
        for (auto hash : owned_overrides) G->mShaderOverrideMap.erase(hash);
        {
            std::lock_guard<std::mutex> guard(G->mShaderBindingsMutex);
            for (const auto& item : saved_cache) {
                if (item.second) G->mShaderBindingsCache[item.first] = *item.second;
                else G->mShaderBindingsCache.erase(item.first);
            }
        }
        G->shader_hash_type = hash_type;
        wcscpy_s(G->SHADER_PATH, shader_path.c_str());
        wcscpy_s(G->SHADER_CACHE_PATH, cache_path.c_str());
        LogFile = log;
    }
    UINT64 track(ID3DBlob* code)
    {
        const auto hash = fnv_64_buf(code->GetBufferPointer(), code->GetBufferSize());
        require_batch(!G->mShaderOverrideMap.count(hash), "PS dump: own unique shader override enables actual hash tracking");
        {
            std::lock_guard<std::mutex> guard(G->mShaderBindingsMutex);
            const auto it = G->mShaderBindingsCache.find(hash);
            saved_cache.emplace(hash, it == G->mShaderBindingsCache.end() ? std::optional<ShaderBindings>{} : it->second);
        }
        owned_overrides.push_back(hash);
        G->mShaderOverrideMap[hash]; // Empty real override; no commands or rendering alteration.
        return hash;
    }
    ComPtr<ID3D11PixelShader> pixel(ID3DBlob* code)
    {
        ComPtr<ID3D11PixelShader> shader;
        require_batch(SUCCEEDED(device.device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader)),
            "PS dump: create actual wrapped PS and cache original declarations");
        shaders.push_back(shader);
        return shader;
    }
    void vertex(ID3DBlob* code)
    {
        track(code);
        ComPtr<ID3D11VertexShader> shader;
        require_batch(SUCCEEDED(device.device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader)),
            "PS dump: create actual wrapped VS");
        shaders.push_back(shader);
        device.wrapper->VSSetShader(shader.Get(), nullptr, 0);
    }
    void compute(ID3DBlob* code)
    {
        track(code);
        ComPtr<ID3D11ComputeShader> shader;
        require_batch(SUCCEEDED(device.device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader)),
            "PS dump: create actual wrapped CS");
        shaders.push_back(shader);
        device.wrapper->CSSetShader(shader.Get(), nullptr, 0);
    }
    void dump(bool compute = false)
    {
        analysis->FrameAnalysisTrigger(static_cast<FrameAnalysisOptions>(
            static_cast<unsigned>(FrameAnalysisOptions::DUMP_SRV) |
            static_cast<unsigned>(FrameAnalysisOptions::FMT_2D_DDS) |
            static_cast<unsigned>(FrameAnalysisOptions::MONO)));
        if (compute) analysis->Dispatch(1, 1, 1);
        else analysis->Draw(0, 0); // Exercises actual public draw/dump path; no rendering result claimed.
    }
};

ComPtr<ID3DBlob> compile_dump(const char* source, const char* profile)
{
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(source, strlen(source), "ShaderDumpFilterRegression", nullptr, nullptr,
        "main", profile, D3DCOMPILE_OPTIMIZATION_LEVEL0, 0, &code, &errors);
    if (FAILED(hr) && errors) std::fprintf(stderr, "PS dump %s: %.*s\n", profile,
        static_cast<int>(errors->GetBufferSize()), static_cast<const char*>(errors->GetBufferPointer()));
    require_batch(SUCCEEDED(hr), "PS dump: compile real shader declaration fixture");
    return code;
}

ULONG dump_references(IUnknown* object)
{
    object->AddRef();
    return object->Release();
}

bool slot_label(const std::wstring& name, wchar_t stage, UINT slot)
{
    const auto needle = std::wstring(1, stage) + L"s-t" + std::to_wstring(slot);
    const auto position = name.find(needle);
    if (position == std::wstring::npos) return false;
    const auto end = position + needle.size();
    return end == name.size() || !iswdigit(name[end]);
}

void require_dump(DumpFixture& fixture, const std::filesystem::path& folder, wchar_t stage,
    UINT slot, UINT colour, UINT width, bool present)
{
    unsigned found = 0;
    for (const auto& item : std::filesystem::directory_iterator(folder)) {
        if (_wcsicmp(item.path().extension().c_str(), L".dds") || !slot_label(item.path().filename().wstring(), stage, slot)) continue;
        ++found;
        ComPtr<ID3D11Resource> loaded;
        require_batch(SUCCEEDED(DirectX::CreateDDSTextureFromFile(fixture.device.native.Get(), item.path().c_str(), &loaded, nullptr)),
            "PS dump: load actual emitted DDS through production DDS loader");
        ComPtr<ID3D11Texture2D> texture;
        require_batch(SUCCEEDED(loaded.As(&texture)), "PS dump: emitted payload is a real texture2D");
        D3D11_TEXTURE2D_DESC desc = {};
        texture->GetDesc(&desc);
        require_batch(desc.Width == width && desc.Height == 3, "PS dump: output extent identifies correct source resource");
        require_pixels(fixture.device, loaded.Get(), colour);
    }
    require_batch(found == (present ? 1u : 0u), "PS dump: actual DDS presence matches declared-stage/filter policy");
}

void exercise_dump_filter(DumpFixture& fixture)
{
    auto& device = fixture.device;
    const auto ps_code = compile_dump(
        "Texture2D<float4> left:register(t3);Texture2D<float4> right:register(t7);"
        "float4 main(float4 p:SV_Position):SV_Target{return left.Load(int3(0,0,0))+right.Load(int3(0,0,0));}", "ps_5_0");
    const auto ps_hash = fixture.track(ps_code.Get());
    auto ps = fixture.pixel(ps_code.Get());
    ShaderBindings declarations;
    require_batch(device.device->GetShaderBindings(ps_hash, &declarations) &&
        declarations.resources[3].type == ShaderResourceType::TYPED && declarations.resources[7].type == ShaderResourceType::TYPED &&
        declarations.resources[4].type == ShaderResourceType::NONE && declarations.resources[127].type == ShaderResourceType::NONE,
        "PS dump: real shader creation publishes the intended sparse declarations");
    auto vs_code = compile_dump("float4 main(uint id:SV_VertexID):SV_Position{return float4(0,0,0,1);}", "vs_5_0");
    fixture.vertex(vs_code.Get());
    device.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const UINT slots[] = {3, 4, 7, 11, 127};
    const UINT colours[] = {0xff302010u, 0xff604020u, 0xff808040u, 0xfff0c090u, 0xffc0a070u};
    const UINT widths[] = {4, 5, 6, 8, 7};
    std::array<ComPtr<ID3D11Texture2D>, 5> textures;
    std::array<ComPtr<ID3D11ShaderResourceView>, 5> views;
    for (UINT i = 0; i < textures.size(); ++i) {
        textures[i] = texture(device, colours[i], widths[i]);
        views[i] = srv(device, textures[i].Get());
    }
    auto bind = [&]() {
        device.wrapper->PSSetShader(ps.Get(), nullptr, 0);
        for (UINT i = 0; i < views.size(); ++i) set_slot(device, L'p', slots[i], views[i].Get());
    };
    auto references = [&]() {
        std::array<ULONG, 5> counts;
        for (UINT i = 0; i < views.size(); ++i) counts[i] = dump_references(views[i].Get());
        return counts;
    };
    auto verify_refs = [&](const std::array<ULONG, 5>& before) {
        require_batch(references() == before, "PS dump: declared/undeclared/internal/far-slot Get refs are balanced");
        for (UINT i = 0; i < views.size(); ++i)
            require_batch(get_slot(device, L'p', slots[i]).Get() == views[i].Get(),
                "PS dump: actual dump preserves every native SRV binding");
    };
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        bind();
        const auto refs = references();
        const auto folder = fixture.output.select(repeat ? L"declared_repeat" : L"declared");
        fixture.dump();
        for (UINT i = 0; i < views.size(); ++i)
            require_dump(fixture, folder, L'p', slots[i], colours[i], widths[i], slots[i] == 3 || slots[i] == 7);
        verify_refs(refs);
    }

    // Missing cache uses the legacy dump-all path. A present-but-empty cache
    // below is intentionally a different case, and skips every PS slot.
    {
        std::lock_guard<std::mutex> guard(G->mShaderBindingsMutex);
        G->mShaderBindingsCache.erase(ps_hash);
    }
    bind();
    auto refs = references();
    auto folder = fixture.output.select(L"cache_missing");
    fixture.dump();
    for (UINT i = 0; i < views.size(); ++i)
        require_dump(fixture, folder, L'p', slots[i], colours[i], widths[i], slots[i] != 11);
    verify_refs(refs);
    {
        std::lock_guard<std::mutex> guard(G->mShaderBindingsMutex);
        G->mShaderBindingsCache[ps_hash] = declarations;
    }

    auto empty_code = compile_dump("float4 main():SV_Target{return float4(0.28125,0.53125,0.78125,1);}", "ps_5_0");
    const auto empty_hash = fixture.track(empty_code.Get());
    auto empty_ps = fixture.pixel(empty_code.Get());
    ShaderBindings empty;
    require_batch(device.device->GetShaderBindings(empty_hash, &empty), "PS dump: declaration-free PS has a present cache entry");
    for (const auto& item : empty.resources)
        require_batch(item.type == ShaderResourceType::NONE, "PS dump: actual empty shader declares no SRVs");
    device.wrapper->PSSetShader(empty_ps.Get(), nullptr, 0);
    for (UINT i = 0; i < views.size(); ++i) set_slot(device, L'p', slots[i], views[i].Get());
    refs = references();
    folder = fixture.output.select(L"cached_empty");
    fixture.dump();
    for (UINT i = 0; i < views.size(); ++i)
        require_dump(fixture, folder, L'p', slots[i], colours[i], widths[i], false);
    verify_refs(refs);

    // Unbound declared slot emits no payload, while another declared slot
    // positively proves that dumping actually ran.
    bind();
    set_slot(device, L'p', 7, nullptr);
    folder = fixture.output.select(L"declared_unbound");
    fixture.dump();
    require_dump(fixture, folder, L'p', 3, colours[0], widths[0], true);
    require_dump(fixture, folder, L'p', 7, colours[2], widths[2], false);

    bind();
    set_slot(device, L'v', 4, views[1].Get());
    folder = fixture.output.select(L"vertex_unfiltered");
    fixture.dump();
    require_dump(fixture, folder, L'v', 4, colours[1], widths[1], true);
    require_dump(fixture, folder, L'p', 4, colours[1], widths[1], false);
    require_dump(fixture, folder, L'p', 3, colours[0], widths[0], true);
    set_slot(device, L'v', 4, nullptr);

    auto cs_code = compile_dump("[numthreads(1,1,1)]void main(){}", "cs_5_0");
    fixture.compute(cs_code.Get());
    set_slot(device, L'c', 4, views[1].Get());
    folder = fixture.output.select(L"compute_unfiltered");
    fixture.dump(true);
    require_dump(fixture, folder, L'c', 4, colours[1], widths[1], true);
    set_slot(device, L'c', 4, nullptr);

    // Exercise an actual writer failure against an owned file in place of a
    // directory. It must not overwrite that file or leak fetched view refs.
    bind();
    refs = references();
    folder = fixture.output.select(L"write_failure");
    const auto blocker = folder / L"not_a_directory";
    HANDLE file = CreateFileW(blocker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    require_batch(file != INVALID_HANDLE_VALUE, "PS dump: exclusively create owned failure barrier file");
    const char marker[] = "owned dump failure marker";
    DWORD written = 0;
    const bool wrote = WriteFile(file, marker, sizeof(marker) - 1, &written, nullptr) != FALSE && written == sizeof(marker) - 1;
    CloseHandle(file);
    require_batch(wrote, "PS dump: write complete failure marker");
    wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, blocker.c_str());
    fixture.dump();
    verify_refs(refs);
    std::ifstream check(blocker, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
    require_batch(content == marker, "PS dump: failed DDS writes leave owned barrier file byte-for-byte intact");
    wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, folder.c_str());
}
} // namespace

void test_dx11_shader_dump_filter_regression()
{
    APTTYPE before_type = APTTYPE_CURRENT;
    APTTYPEQUALIFIER before_qualifier = APTTYPEQUALIFIER_NONE;
    const HRESULT before = CoGetApartmentType(&before_type, &before_qualifier);
    std::filesystem::path owned;
    {
        DumpFixture fixture;
        owned = fixture.output.path;
        exercise_dump_filter(fixture);
    }
    require_batch(!std::filesystem::exists(owned), "PS dump: all actual output and log files are cleaned from the build subtree");
    APTTYPE after_type = APTTYPE_CURRENT;
    APTTYPEQUALIFIER after_qualifier = APTTYPEQUALIFIER_NONE;
    const HRESULT after = CoGetApartmentType(&after_type, &after_qualifier);
    require_batch(after == before && (FAILED(before) || (before_type == after_type && before_qualifier == after_qualifier)),
        "PS dump: real DDS and background-gated input paths preserve the calling thread COM apartment contract");
}
