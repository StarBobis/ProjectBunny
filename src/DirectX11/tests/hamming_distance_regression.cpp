#include "slot_range_test_support.h"
#include "IniHandler.h"
#include "Input.h"
#include "Override.h"
#include "ExpressionFunctions.h"
#include "Logging.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

extern std::vector<CommandList*> pending_post_command_lists;
namespace {
using namespace dx11_slot_test;

class HammingFiles {
    HINSTANCE module = migoto_handle;
    std::wstring user = G->user_config;
    int dirty = G->user_config_dirty;
    unsigned frame = G->frame_no;
    UINT hunting = G->hunting;
    bool analysis = G->analyse_frame;
    unsigned unknown = G->unknown_persist_vars_count;
    bool disabled = G->disable_input, input_initialized = G->disable_input_initialized;
    bool initialized = G->gConfigInitialized, warnings = G->gShowWarnings;
    bool clear_unknown = G->clear_unknown_settings, cleanup = G->clear_saved_persist_vars;
    InputDisableScope input_scope = G->input_disable_scope;
    LogVerbosity verbosity = gLogVerbosity;
    int debug = gLogDebug;
    std::vector<std::filesystem::path> owned;
public:
    std::filesystem::path config, settings, library;
    HammingFiles()
    {
        wchar_t executable[MAX_PATH]{};
        require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "Hamming: locate regression executable");
        const auto directory = std::filesystem::path(executable).parent_path();
        bool build = false;
        for (const auto& part : directory) build |= _wcsicmp(part.c_str(), L"build") == 0;
        require_batch(build, "Hamming: fixtures stay inside build directory");
        config = directory / L"d3dx.ini";
        const auto pid = std::to_wstring(GetCurrentProcessId());
        settings = directory / (L"hamming_settings_" + pid + L".ini");
        library = directory / (L"hamming reference_" + pid + L".ini");
        try {
            for (const auto& path : {config, settings, library}) {
                HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                require_batch(file != INVALID_HANDLE_VALUE, "Hamming: refuse to replace existing fixture");
                CloseHandle(file); owned.push_back(path);
            }
        } catch (...) { remove_files(); throw; }
        migoto_handle = nullptr;
    }
    void remove_files() { for (auto it = owned.rbegin(); it != owned.rend(); ++it) DeleteFileW(it->c_str()); }
    void load(uint64_t hash)
    {
        std::ofstream out(config, std::ios::binary | std::ios::trunc);
        out << "[Logging]\nlog_level=disabled\nshow_warnings=0\n[Hunting]\nhunting=0\n"
            "[Rendering]\ndhash_mip_size=0\ndhash_allow_rehash=0\n"
            "[System]\nskip_early_includes_load=0\nclear_unknown_settings=0\n[Include]\nuser_config="
            << settings.filename().string() << "\ninclude=" << library.filename().string() << "\n";
        out << std::hex << std::setfill('0')
            << "[ResourceHammingSame]\noriginal_dhash=" << std::setw(16) << hash << "\n"
            << "[ResourceHammingInverse]\noriginal_dhash=" << std::setw(16) << ~hash << "\n"
            << "[ResourceHammingThree]\noriginal_dhash=" << std::setw(16) << (hash ^ 0x8000000000000005ULL) << "\n"
            << "[ResourceHammingMaximum]\noriginal_dhash=ffffffffffffffff\n"
            << "[ResourceHammingZero]\noriginal_dhash=0000000000000000\n"
            << "[ResourceHammingOne]\noriginal_dhash=0000000000000001\n";
        out.close(); require_batch(out.good(), "Hamming: write actual uint64 metadata declarations");
        std::ofstream included(library, std::ios::binary | std::ios::trunc);
        included << "[ResourceHammingScoped]\noriginal_dhash=" << std::hex << std::setfill('0') << std::setw(16) << hash << "\n";
        included.close(); require_batch(included.good(), "Hamming: write actual included namespaced reference");
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
        ClearKeyBindings(); LoadConfigFile();
    }
    ~HammingFiles()
    {
        ClearKeyBindings(); pending_post_command_lists.clear(); CurrentTransition.Stop(); OverrideSave.Reset(nullptr);
        registered_command_lists.clear(); explicitCommandListSections.clear(); presetOverrides.clear();
        G->constants_command_list.clear(); G->post_constants_command_list.clear();
        customResourcePools.clear(); customResources.clear(); persistent_variables.clear(); command_list_globals.clear();
        migoto_handle = module; G->user_config = user; G->user_config_dirty = dirty; G->frame_no = frame;
        G->disable_input = disabled; G->disable_input_initialized = input_initialized; G->input_disable_scope = input_scope;
        G->gConfigInitialized = initialized; G->gShowWarnings = warnings;
        G->clear_unknown_settings = clear_unknown; G->clear_saved_persist_vars = cleanup;
        G->hunting = hunting; G->analyse_frame = analysis; G->unknown_persist_vars_count = unknown;
        gLogVerbosity = verbosity; gLogDebug = debug;
        remove_files();
    }
};

ComPtr<ID3D11Texture2D> hamming_texture(BatchDevice& device, UINT rgba, bool tracked, bool populated = true)
{
    std::vector<UINT> pixels(9 * 8, rgba);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 9; desc.Height = 8; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{}; data.pSysMem = pixels.data(); data.SysMemPitch = 9 * sizeof(UINT);
    ComPtr<ID3D11Texture2D> result;
    const HRESULT hr = tracked ? device.device->CreateTexture2D(&desc, populated ? &data : nullptr, &result) :
        device.native->CreateTexture2D(&desc, populated ? &data : nullptr, &result);
    require_batch(SUCCEEDED(hr), "Hamming: actual tracked/native texture creation");
    return result;
}

CommandListExpression hamming_expression(const std::wstring& text, const std::wstring& ns = {})
{
    CommandListScope scope; scope.emplace_front();
    CommandListExpression result;
    require_batch(result.parse(&text, &ns, &scope), "Hamming: actual resource member expression parses");
    return result;
}

float distance(BatchDevice& device, const std::wstring& source, const std::wstring& reference, const std::wstring& ns = {})
{
    auto expression = hamming_expression(source + L"->hammingdistance(" + reference + L")", ns);
    return expression.evaluate(&device.state);
}

void exercise_hamming(BatchDevice& device, HammingFiles& files, ID3D11Texture2D* texture, uint64_t hash)
{
    require_batch(customResources.at(L"resourcehammingsame").original_dhash == hash &&
        customResources.at(L"resourcehammingmaximum").original_dhash == UINT64_MAX,
        "Hamming: actual INI retains complete uint64 reference hashes including maximum");
    Registry registry;
    registry.resource(L"resourcehammingsource", device, texture);
    auto zero = hamming_texture(device, 0, true);
    registry.resource(L"resourcehammingblack", device, zero.Get());
    require_batch(distance(device, L"resourcehammingsource", L"resourcehammingsame") == 0 &&
        distance(device, L"resourcehammingsource", L"resourcehamminginverse") == 64 &&
        distance(device, L"resourcehammingsource", L"resourcehammingthree") == 3 &&
        distance(device, L"resourcehammingsource", L"resourcehammingmaximum") == 24,
        "Hamming: real GPU source versus exact INI references gives distances zero/64/three/24");
    require_batch(distance(device, L"resourcehammingblack", L"resourcehammingzero") == 0 &&
        distance(device, L"resourcehammingblack", L"resourcehammingone") == 64 &&
        distance(device, L"resourcehammingsource", L"resourcehammingzero") == 64,
        "Hamming: valid zero hash differs maximally from nonzero without being treated as failure");
    auto source_view = srv(device, texture); set_slot(device, L'p', 0, source_view.Get());
    require_batch(distance(device, L"ps-t0", L"resourcehammingsame") == 0 &&
        distance(device, L"resourcehammingsource", L"ps-t0") == -1 &&
        distance(device, L"ps-t7", L"resourcehammingsame") == -2,
        "Hamming: pipeline source works; reference without custom metadata is unknown; absent source is not-found");
    auto untracked = hamming_texture(device, 0xffc08040, false);
    auto no_upload = hamming_texture(device, 0xffc08040, true, false);
    registry.resource(L"resourcehamminguntracked", device, untracked.Get());
    registry.resource(L"resourcehammingnoupload", device, no_upload.Get());
    auto native_buffer = buffer(device, 64, D3D11_BIND_VERTEX_BUFFER);
    registry.resource(L"resourcehammingbuffer", device, native_buffer.Get());
    require_batch(distance(device, L"resourcehamminguntracked", L"resourcehammingsame") == -1 &&
        distance(device, L"resourcehammingnoupload", L"resourcehammingsame") == -1 &&
        distance(device, L"resourcehammingbuffer", L"resourcehammingsame") == -1,
        "Hamming: untracked/no-upload/buffer inputs report inability to compute rather than hash zero");
    ComPtr<ID3D11DeviceContext> deferred;
    require_batch(SUCCEEDED(device.native->CreateDeferredContext(0, &deferred)), "Hamming: actual deferred context");
    ComPtr<ID3D11DeviceContext1> deferred1;
    require_batch(SUCCEEDED(deferred.As(&deferred1)), "Hamming: actual deferred context 11.1");
    HackerContext wrapper(device.native.Get(), deferred1.Get()); wrapper.SetHackerDevice(device.device);
    CommandListState deferred_state;
    deferred_state.mHackerDevice = device.device; deferred_state.mHackerContext = &wrapper;
    deferred_state.mOrigDevice1 = device.native.Get(); deferred_state.mOrigContext1 = deferred1.Get();
    auto warmed = hamming_expression(L"resourcehammingsource->hammingdistance(resourcehammingsame)");
    require_batch(warmed.evaluate(&deferred_state) == -1,
        "Hamming: deferred context remains unsupported even with source hash cache already warm");

    const std::wstring ns = files.library.filename().wstring(), plain = L"ResourceHammingScoped";
    std::wstring full;
    require_batch(get_namespaced_section_name_lower(&plain, &ns, &full) && customResources.count(full),
        "Hamming: actual include produced namespaced reference with space in namespace");
    require_batch(distance(device, L"resourcehammingsource", L"resourcehammingscoped", ns) == 0 &&
        distance(device, L"resourcehammingsource", full) == 0,
        "Hamming: relative and explicit namespaced reference member arguments resolve the INI hash");
    auto& pool = registry.pool(L"poolhammingrefs", device, PoolIndexType::RING, 2);
    auto* first = pool.GetResource(0, false, false, true);
    auto* second = pool.GetResource(1, false, false, true);
    require_batch(first && second, "Hamming: actual lazy reference pool elements");
    first->CopyMetadataFrom(customResources.at(L"resourcehammingsame"));
    second->CopyMetadataFrom(customResources.at(L"resourcehammingthree"));
    auto& index = registry.variable(L"$hamming_ref_index", 0);
    auto dynamic = hamming_expression(L"resourcehammingsource->hammingdistance(poolhammingrefs[$hamming_ref_index])");
    require_batch(dynamic.evaluate(&device.state) == 0, "Hamming: dynamic reference first pool element");
    index.fval = 1;
    require_batch(dynamic.evaluate(&device.state) == 3 && dynamic.evaluate(&device.state) == 3,
        "Hamming: repeated execution observes updated dynamic reference without consuming or mutating shared syntax");
    auto index_buffer = buffer(device, 8, D3D11_BIND_VERTEX_BUFFER);
    registry.resource(L"resourcehammingindex", device, index_buffer.Get(), DXGI_FORMAT_UNKNOWN, 4, 8);
    require_batch(distance(device, L"resourcehammingsource", L"poolhammingrefs[(resourcehammingindex->size/4)-1]") == 3,
        "Hamming: nested resource getter inside reference pool argument remains balanced and dynamic");
    ResourceCopyTarget target;
    CommandListScope scope; scope.emplace_front(); const std::wstring empty;
    require_batch(target.ParseTarget(L"resourcehammingsource->hammingdistance(poolhammingrefs[$hamming_ref_index])", true, &empty, &scope),
        "Hamming: parse target with shared resource member argument");
    ResourceCopyTarget copied = target;
    index.fval = 0;
    require_batch(target.GetResourceHammingDistance(&device.state) == 0 && copied.GetResourceHammingDistance(&device.state) == 0,
        "Hamming: target copies retain shared reference metadata syntax");
    index.fval = 1;
    require_batch(copied.GetResourceHammingDistance(&device.state) == 3 && target.GetResourceHammingDistance(&device.state) == 3,
        "Hamming: evaluating either shared copy does not freeze or mutate the original member argument");
    auto& source_pool = registry.pool(L"poolhammingsources", device, PoolIndexType::RING, 1);
    auto* source_element = source_pool.GetResource(0, false, false, true);
    require_batch(source_element != nullptr, "Hamming: lazy source pool element");
    assign(device, source_element, texture);
    auto once = hamming_expression(L"poolhammingsources[random(1)]->hammingdistance(poolhammingrefs[random(1)])");
    const uint32_t before = GetExpressionRandomSampleCount();
    require_batch(once.evaluate(&device.state) == 0 && GetExpressionRandomSampleCount() == before + 2,
        "Hamming: source and reference dynamic resource selectors each resolve exactly once");
    const auto pool_count = pool.GetPoolSize();
    const auto variables = command_list_globals.size();
    for (const auto* invalid : {L"resourcehammingsource->hammingdistance()", L"resourcehammingsource->hammingdistance(resourcehammingsame,resourcehammingone)",
        L"resourcehammingsource->hammingdistance(poolhammingrefs[*])", L"resourcehammingsource->hammingdistance(poolhammingrefs[0:1])",
        L"resourcehammingsource->hammingdistance(ps-t[0:1])", L"resourcehammingsource->hammingdistance(ffffffffffffffff)",
        L"resourcehammingsource->hammingdistance(resourcehammingsame)garbage", L"resourcehammingsource->hammingdistance(poolhammingrefs[(1])"}) {
        CommandListExpression failed;
        const std::wstring text = invalid;
        require_batch(!failed.parse(&text, &empty, &scope), "Hamming: malformed/full-range/inline-hash member argument is rejected");
    }
    require_batch(pool.GetPoolSize() == pool_count && command_list_globals.size() == variables &&
        target.GetResourceHammingDistance(&device.state) == 3,
        "Hamming: failed parses do not allocate reference slots/register locals or mutate existing shared targets");
    require_batch(customResources.at(L"resourcehammingsame").resource == nullptr &&
        customResources.at(L"resourcehammingsame").original_dhash == hash,
        "Hamming: reference is declaration metadata and is never substantiated or GPU-hashed");
    device.context->ClearState();
}
} // namespace

void test_dx11_hamming_distance_regression()
{
    ResourceCopyTarget missing_state;
    require_batch(missing_state.GetResourceHammingDistance(nullptr) == -2,
        "Hamming: missing command state reports missing resource without dereferencing it");
    BatchDevice device;
    auto texture = hamming_texture(device, 0xffc08040, true);
    uint64_t hash = 0;
    require_batch(SUCCEEDED(device.wrapper->ComputeDHash(texture.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM, hash)) &&
        hash == 0x000f00ff0fffffffULL, "Hamming: real source GPU hash matches exact flat RGBA thermometer fixture");
    HammingFiles files;
    files.load(hash);
    exercise_hamming(device, files, texture.Get(), hash);
    device.context->ClearState(); device.context->Flush();
    std::fprintf(stderr, "[PASS] Hamming resource member real INI/GPU/zero/errors/dynamic/shared syntax\n");
}
