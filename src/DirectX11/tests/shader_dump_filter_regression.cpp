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

std::string read_dump_text(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    require_batch(file.good(), "dump text: actual output exists");
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void exercise_dump_text(DumpFixture& fixture)
{
    auto& device = fixture.device;
    const auto folder = fixture.output.select(L"\u4e2d\u6587_dump_text");
    // Both entry points must encode Unicode independently of the ambient locale.
    // Include a surrogate pair and a wide argument in the narrow printf format.
    for (const char* locale : {"C", ".UTF8"}) {
        require_batch(std::setlocale(LC_CTYPE, locale) != nullptr, "dump text: set test locale");
        const std::string before = std::setlocale(LC_CTYPE, nullptr);
        fixture.analysis->FrameAnalysisLog("narrow %S %s\n", L"\u4e2d\u6587\U0001f430", "ASCII");
        fixture.analysis->FrameAnalysisLogW(L"wide %ls %hs\n", L"\u4e2d\u6587\U0001f430", "ASCII");
        require_batch(before == std::setlocale(LC_CTYPE, nullptr), "dump text: writer preserves caller locale");
    }
    auto code = compile_dump("[numthreads(2,1,1)]void main(){}", "cs_5_0");
    fixture.compute(code.Get());
    auto source = buffer(device, 64, D3D11_BIND_SHADER_RESOURCE);
    float values[16]; std::fill(std::begin(values), std::end(values), 1.0f);
    device.context->UpdateSubresource(source.Get(), 0, nullptr, values, 0, 0);
    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = DXGI_FORMAT_R32_FLOAT; desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; desc.Buffer.NumElements = 16;
    ComPtr<ID3D11ShaderResourceView> view;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(source.Get(), &desc, &view)), "dump text: real buffer SRV");
    set_slot(device, L'c', 0, view.Get()); set_slot(device, L'c', 127, view.Get());
    auto cb = buffer(device, 64, D3D11_BIND_CONSTANT_BUFFER);
    device.context->UpdateSubresource(cb.Get(), 0, nullptr, values, 0, 0);
    ID3D11Buffer* constant = cb.Get(); device.wrapper->CSSetConstantBuffers(0, 1, &constant);
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        fixture.analysis->FrameAnalysisTrigger(static_cast<FrameAnalysisOptions>(
            static_cast<unsigned>(FrameAnalysisOptions::DUMP_SRV) | static_cast<unsigned>(FrameAnalysisOptions::DUMP_CB) |
            static_cast<unsigned>(FrameAnalysisOptions::FMT_BUF_BIN) | static_cast<unsigned>(FrameAnalysisOptions::FMT_BUF_TXT) |
            static_cast<unsigned>(FrameAnalysisOptions::MONO)));
        fixture.analysis->Dispatch(1, 1, 1);
    }
    set_slot(device, L'c', 0, nullptr); set_slot(device, L'c', 127, nullptr);
    constant = nullptr; device.wrapper->CSSetConstantBuffers(0, 1, &constant);
    unsigned generic = 0, constants = 0;
    for (const auto& item : std::filesystem::directory_iterator(folder / L"deduped")) {
        if (item.path().extension() != L".txt") continue;
        const auto name = item.path().filename().wstring();
        const auto content = read_dump_text(item.path());
        require_batch(name.find(L'?') == std::wstring::npos && content.find('?') == std::string::npos,
            "dump text: filenames and payload labels contain no question marks");
        if (name.find(L"-buf0.txt") != std::wstring::npos || name.find(L"-buf127.txt") != std::wstring::npos) {
            const auto label = name.find(L"-buf127.txt") != std::wstring::npos ? "buf127[0].x: 1" : "buf0[0].x: 1";
            require_batch(content.find(label) != std::string::npos, "dump text: generic filename and data label agree");
            const std::string prefix = name.find(L"-buf127.txt") != std::wstring::npos ? "buf127" : "buf0";
            std::string expected;
            for (unsigned i = 0; i < 4; ++i)
                for (const char component : std::string("xyzw"))
                    expected += prefix + "[" + std::to_string(i) + "]." + component + ": 1\r\n";
            require_batch(content == expected, "dump text: full generic payload is byte-for-byte unchanged");
            ++generic;
        }
        if (name.find(L"-cb0.txt") != std::wstring::npos) {
            require_batch(content.find("cb0[0].x: 1") != std::string::npos, "dump text: CB compatibility preserved");
            ++constants;
        }
    }
    require_batch(generic == 2 && constants == 1, "dump text: repeated dumps reuse correct slot-specific text payloads");
    unsigned linked = 0;
    for (const auto& item : std::filesystem::directory_iterator(folder)) {
        if (item.path().extension() != L".txt" || (!slot_label(item.path().filename().wstring(), L'c', 0) &&
            !slot_label(item.path().filename().wstring(), L'c', 127))) continue;
        require_batch(read_dump_text(item.path()).find("].x: 1") != std::string::npos, "dump text: outer linked file resolves to actual payload");
        ++linked;
    }
    require_batch(linked == 4, "dump text: both slots linked for both dispatches");
    G->analyse_frame = false; fixture.analysis->FrameAnalysisLog("close\n");
    const auto log = read_dump_text(folder / L"log.txt");
    const std::string unicode = "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x90\xb0";
    for (const char* prefix : {"narrow ", "wide "}) {
        const auto needle = std::string(prefix) + unicode + " ASCII\r\n";
        const auto first = log.find(needle);
        require_batch(first != std::string::npos && log.find(needle, first + needle.size()) != std::string::npos,
            "dump text: exact UTF-8 bytes for both log APIs in both ambient locales");
    }
    require_batch(log.find("Unable to create") == std::string::npos, "dump text: no file creation failures");
    for (const char* stage : {"capture-inclusive", "map-wait", "hash", "text-save", "link"})
        require_batch(log.find(std::string("Dump timing ") + stage + ": calls=") != std::string::npos,
            "dump timing: real capture reports stage counts and elapsed time on close");
    // Reopen a new session after closing the previous log.
    const auto next = fixture.output.select(L"utf8_reopen"); G->analyse_frame = true;
    fixture.analysis->FrameAnalysisLogW(L"reopened %ls\n", L"\u4e2d\u6587");
    G->analyse_frame = false; fixture.analysis->FrameAnalysisLog("close\n");
    require_batch(read_dump_text(next / L"log.txt").find("reopened " + unicode.substr(0, 6)) != std::string::npos,
        "dump text: reopened log retains UTF-8 encoding");
    const auto blocker = next / L"log_barrier";
    { std::ofstream file(blocker, std::ios::binary); file << "owned log barrier"; }
    wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, blocker.c_str()); G->analyse_frame = true;
    fixture.analysis->FrameAnalysisLogW(L"cannot open %ls\n", L"\u4e2d\u6587");
    require_batch(read_dump_text(blocker) == "owned log barrier", "dump text: log open failure preserves existing file");
    wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, next.c_str());
}

void exercise_dump_mesh_text(DumpFixture& fixture)
{
    auto& device = fixture.device;
    auto code = compile_dump("struct I{float3 p:POSITION;float2 uv:TEXCOORD2;};"
        "float4 main(I v):SV_Position{return float4(v.p+v.uv.x,1);}", "vs_5_0");
    fixture.vertex(code.Get());
    D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    ComPtr<ID3D11InputLayout> layout;
    require_batch(SUCCEEDED(device.device->CreateInputLayout(elements, 2, code->GetBufferPointer(), code->GetBufferSize(), &layout)),
        "dump mesh text: create actual cached layout");
    device.wrapper->IASetInputLayout(layout.Get());
    auto vertices = buffer(device, 60, D3D11_BIND_VERTEX_BUFFER);
    float values[15]; for (unsigned i = 0; i < 15; ++i) values[i] = static_cast<float>(i + 1);
    device.context->UpdateSubresource(vertices.Get(), 0, nullptr, values, 0, 0);
    ID3D11Buffer* vb = vertices.Get(); UINT stride = 20, offset = 0;
    device.wrapper->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    device.wrapper->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for (const bool wide : {false, true}) {
        G->analyse_frame = false; fixture.analysis->FrameAnalysisLog("close\n");
        const auto folder = fixture.output.select(wide ? L"mesh32" : L"mesh16"); G->analyse_frame = true;
        auto indices = buffer(device, wide ? 12 : 6, D3D11_BIND_INDEX_BUFFER);
        const uint32_t i32[] = {2, 0, 1}; const uint16_t i16[] = {2, 0, 1};
        device.context->UpdateSubresource(indices.Get(), 0, nullptr, wide ? static_cast<const void*>(i32) : i16, 0, 0);
        device.wrapper->IASetIndexBuffer(indices.Get(), wide ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
        fixture.analysis->FrameAnalysisTrigger(static_cast<FrameAnalysisOptions>(
            static_cast<unsigned>(FrameAnalysisOptions::DUMP_VB) | static_cast<unsigned>(FrameAnalysisOptions::DUMP_IB) |
            static_cast<unsigned>(FrameAnalysisOptions::FMT_BUF_TXT) | static_cast<unsigned>(FrameAnalysisOptions::MONO)));
        fixture.analysis->DrawIndexed(3, 0, 0);
        unsigned vb_files = 0, ib_files = 0;
        for (const auto& file : std::filesystem::directory_iterator(folder / L"deduped")) {
            if (file.path().extension() != L".txt") continue;
            const auto text = read_dump_text(file.path());
            const auto name = file.path().filename().wstring();
            if (name.find(L"-ib") != std::wstring::npos) {
                const std::string expected = std::string("byte offset: 0\r\nfirst index: 0\r\nindex count: 3\r\ntopology: trianglelist\r\nformat: DXGI_FORMAT_") +
                    (wide ? "R32_UINT" : "R16_UINT") + "\r\n\r\n2 0 1\r\n";
                require_batch(text == expected, "dump mesh text: exact R16/R32 grouping, spacing and CRLF bytes"); ++ib_files;
            } else if (name.find(L"-vb0") != std::wstring::npos) {
                std::string expected = "vertex-data:\r\n";
                for (unsigned vertex = 0; vertex < 3; ++vertex) {
                    expected += "\r\nvb0[" + std::to_string(vertex) + "]+000 POSITION: ";
                    for (unsigned c = 0; c < 3; ++c) expected += (c ? ", " : "") + std::to_string(vertex * 5 + c + 1);
                    expected += "\r\nvb0[" + std::to_string(vertex) + "]+012 TEXCOORD2: " +
                        std::to_string(vertex * 5 + 4) + ", " + std::to_string(vertex * 5 + 5) + "\r\n";
                }
                const auto data = text.find("vertex-data:");
                require_batch(data != std::string::npos && text.substr(data) == expected,
                    "dump mesh text: exact VB labels for zero/nonzero semantic indices and formatted data"); ++vb_files;
            }
        }
        require_batch(vb_files == 1 && ib_files == 1, "dump mesh text: real indexed draw emits both payloads");
    }
    vb = nullptr; device.wrapper->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    device.wrapper->IASetIndexBuffer(nullptr, DXGI_FORMAT_R16_UINT, 0);
    device.wrapper->IASetInputLayout(nullptr);
    G->analyse_frame = false; fixture.analysis->FrameAnalysisLog("close\n"); G->analyse_frame = true;
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
        exercise_dump_text(fixture);
        exercise_dump_mesh_text(fixture);
        exercise_dump_filter(fixture);
    }
    require_batch(!std::filesystem::exists(owned), "PS dump: all actual output and log files are cleaned from the build subtree");
    APTTYPE after_type = APTTYPE_CURRENT;
    APTTYPEQUALIFIER after_qualifier = APTTYPEQUALIFIER_NONE;
    const HRESULT after = CoGetApartmentType(&after_type, &after_qualifier);
    require_batch(after == before && (FAILED(before) || (before_type == after_type && before_qualifier == after_qualifier)),
        "PS dump: real DDS and background-gated input paths preserve the calling thread COM apartment contract");
}
