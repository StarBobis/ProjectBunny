#include "slot_range_test_support.h"
#include "IniHandler.h"
#include "Input.h"
#include "Override.h"
#include "ExpressionFunctions.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>
extern std::vector<CommandList*> pending_post_command_lists;
namespace {
using namespace dx11_slot_test;
void check(bool value, const char* message) { require_batch(value, message); }
class OriginalConfig {
    std::filesystem::path config, settings;
    HINSTANCE saved_module = migoto_handle;
    std::wstring saved_user_config = G->user_config;
    int saved_dirty = G->user_config_dirty;
    unsigned saved_frame = G->frame_no;
    bool saved_disable = G->disable_input;
    bool saved_input_initialized = G->disable_input_initialized;
    InputDisableScope saved_scope = G->input_disable_scope;
    bool owns_config = false;
    bool owns_settings = false;
public:
    OriginalConfig()
    {
        wchar_t executable[MAX_PATH] = {};
        check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate metadata-test executable");
        const auto base = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& part : base) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        check(in_build, "metadata fixtures must remain in build/");
        config = base / L"d3dx.ini";
        settings = base / (L"original_metadata_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        check(!std::filesystem::exists(settings), "refuse existing metadata settings fixture");
        HANDLE file = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(file != INVALID_HANDLE_VALUE, "refuse existing metadata d3dx.ini");
        CloseHandle(file);
        owns_config = true;
        file = CreateFileW(settings.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            DeleteFileW(config.c_str()); owns_config = false;
            throw std::runtime_error("refuse existing original metadata user fixture");
        }
        CloseHandle(file); owns_settings = true;
        migoto_handle = nullptr;
    }

    void load()
    {
        std::ofstream out(config, std::ios::binary | std::ios::trunc);
        out << "[Logging]\nlog_level=disabled\nshow_warnings=0\n[Hunting]\nhunting=0\n"
            "[System]\nskip_early_includes_load=0\n[Include]\nuser_config=" << settings.filename().string() << "\n"
            "[ResourceOriginalOmitted]\n"
            "[ResourceOriginalValid]\noriginal_format=DXGI_FORMAT_R32_UINT\noriginal_dhash=fedcba9876543210\n"
            "[ResourceOriginalMaximum]\noriginal_format=R16_UINT\noriginal_dhash=ffffffffffffffff\n"
            "[ResourceOriginalInvalid]\noriginal_format=definitely_invalid\noriginal_dhash=1234garbage\n"
            "[ResourceOriginalOverflow]\noriginal_dhash=12345678901234567\n";
        out.close();
        check(out.good(), "write actual resource metadata INI");
        ClearKeyBindings();
        LoadConfigFile();

    }

    ~OriginalConfig()
    {
        ClearKeyBindings();
        pending_post_command_lists.clear();
        CurrentTransition.Stop();
        OverrideSave.Reset(nullptr);
        presetOverrides.clear();
        G->constants_command_list.clear();
        G->post_constants_command_list.clear();
        customResourcePools.clear();
        customResources.clear();
        persistent_variables.clear();
        command_list_globals.clear();
        migoto_handle = saved_module;
        G->user_config = saved_user_config;
        G->user_config_dirty = saved_dirty;
        G->frame_no = saved_frame;
        G->disable_input = saved_disable;
        G->disable_input_initialized = saved_input_initialized;
        G->input_disable_scope = saved_scope;
        if (owns_config) DeleteFileW(config.c_str());
        if (owns_settings) DeleteFileW(settings.c_str());
    }
};


float original_format(BatchDevice& device, const std::wstring& target)
{
    const std::wstring text = target + L"->originalformat", ns;
    CommandListScope scope;
    CommandListExpression expression;
    check(expression.parse(&text, &ns, &scope), "original metadata: real member expression parses");
    return expression.evaluate(&device.state);
}
void pair(const CustomResource& resource, DXGI_FORMAT format, UINT64 hash, const char* message)
{
    check(resource.original_format == format && resource.original_dhash == hash, message);
}
void metadata_copy(BatchDevice& device, const std::wstring& destination, const std::wstring& source)
{
    CommandList commands;
    if (!parse(commands, destination, source)) {
        std::fwprintf(stderr, L"Original metadata directive rejected: %ls = %ls\n", destination.c_str(), source.c_str());
        throw std::runtime_error("original metadata: parse actual resource-copy directive");
    }
    execute(commands, device);
}

void exercise(BatchDevice& device)
{
    const auto unspecified = static_cast<DXGI_FORMAT>(-1);
    CustomResource constructed;
    pair(constructed, unspecified, 0, "original metadata: constructor sentinel and exact uint64 default");
    pair(customResources.at(L"resourceoriginalomitted"), unspecified, 0, "original metadata: INI omitted defaults");
    pair(customResources.at(L"resourceoriginalvalid"), DXGI_FORMAT_R32_UINT, 0xfedcba9876543210ULL,
        "original metadata: real INI full 64-bit value without float conversion");
    pair(customResources.at(L"resourceoriginalmaximum"), DXGI_FORMAT_R16_UINT, UINT64_MAX,
        "original metadata: both format spellings and maximum hash");
    pair(customResources.at(L"resourceoriginalinvalid"), unspecified, 0, "original metadata: malformed complete literals rejected");
    pair(customResources.at(L"resourceoriginaloverflow"), unspecified, 0, "original metadata: 17-digit overflow rejected");
    check(original_format(device, L"resourceoriginalomitted") == -1 &&
        original_format(device, L"resourceoriginalvalid") == DXGI_FORMAT_R32_UINT &&
        original_format(device, L"ps-t0") == -1, "original metadata: sentinel and declaration getter before allocation");

    Registry registry;
    auto pixels = texture(device, 0x90abcdef);
    auto view = srv(device, pixels.Get());
    auto& source = registry.resource(L"resourceoriginalsource", device, pixels.Get());
    source.original_format = DXGI_FORMAT_R16_UINT; source.original_dhash = 0xfedcba9876543210ULL;
    auto& destination = registry.resource(L"resourceoriginaldestination", device);
    for (const auto* option : {L"ref ", L"copy ", L"copy_desc "}) {
        metadata_copy(device, L"resourceoriginaldestination", std::wstring(option) + L"resourceoriginalsource");
        pair(destination, source.original_format, source.original_dhash, "original metadata: actual custom ref/copy/copy_desc preserves pair");
        check(destination.resource != nullptr, "original metadata: successful copy publishes native resource");
    }
    metadata_copy(device, L"resourceoriginaldestination", L"null");
    pair(destination, source.original_format, source.original_dhash, "original metadata: null retains reference declaration pair");
    destination.ResetRuntimeState();
    pair(destination, source.original_format, source.original_dhash, "original metadata: runtime reset retains reference declaration pair");
    set_slot(device, L'p', 0, view.Get());
    destination.max_copies_per_frame = 1;
    destination.frame_no = G->frame_no;
    destination.copies_this_frame = 1;
    metadata_copy(device, L"resourceoriginaldestination", L"copy ps-t0");
    pair(destination, source.original_format, source.original_dhash,
        "original metadata: rejected maxcopies operation cannot publish new pair");
    destination.max_copies_per_frame = 0;
    metadata_copy(device, L"resourceoriginaldestination", L"ref ps-t0");
    pair(destination, unspecified, 0, "original metadata: successful pipeline capture resets absent reference pair");

    CustomResource copied;
    copied.CopyMetadataFrom(source);
    pair(copied, source.original_format, source.original_dhash, "original metadata: declaration/template CopyMetadataFrom");
    auto& pool = registry.pool(L"pooloriginal", device, PoolIndexType::RING, 2);
    pool.resource_template->CopyMetadataFrom(source);
    auto* first = pool.GetResource(0, false, false, true);
    auto* second = pool.GetResource(1, false, false, true);
    check(first && second, "original metadata: lazily instantiate real pool slots");
    pair(*first, source.original_format, source.original_dhash, "original metadata: lazy pool clones original pair");
    metadata_copy(device, L"pooloriginal[0]", L"ref resourceoriginalsource");
    metadata_copy(device, L"pooloriginal[1]", L"ref resourceoriginalsource");
    second->original_format = DXGI_FORMAT_R32_FLOAT; second->original_dhash = 0x123456789abcdef0ULL;
    auto alternate = texture(device, 0x31415926);
    assign(device, second, alternate.Get());
    const auto before = GetExpressionRandomSampleCount();
    metadata_copy(device, L"resourceoriginaldestination", L"ref pooloriginal[random(2)]");
    check(GetExpressionRandomSampleCount() == before + 1,
        "original metadata: source selection and metadata acquisition sample dynamic pool once");
    auto* selected = destination.resource == first->resource ? first : second;
    check(destination.resource == selected->resource, "original metadata: destination owns selected native resource");
    pair(destination, selected->original_format, selected->original_dhash, "original metadata: pair belongs to same dynamically selected resource");

    ResourceCopyTarget this_target;
    this_target.type = ResourceCopyTargetType::CUSTOM_RESOURCE;
    this_target.SetCustomResource(&source);
    device.state.this_target = &this_target;
    metadata_copy(device, L"resourceoriginaldestination", L"ref this");
    pair(destination, source.original_format, source.original_dhash, "original metadata: THIS source forwards selected snapshot");
    this_target.SetCustomResource(&destination);
    metadata_copy(device, L"this", L"ref resourceoriginalsource");
    pair(destination, source.original_format, source.original_dhash, "original metadata: THIS destination publishes pair");
    device.state.this_target = nullptr;

    metadata_copy(device, L"pooloriginal[0:1]", L"ref ps-t[0:1]");
    pair(*first, unspecified, 0, "original metadata: range fast fetch pipeline success resets pair");
    pair(*second, DXGI_FORMAT_R32_FLOAT, 0x123456789abcdef0ULL,
        "original metadata: null range source retains prior pair");
    metadata_copy(device, L"pooloriginal[*]", L"copy_desc resourceoriginalsource");
    pair(*first, source.original_format, source.original_dhash, "original metadata: whole pool copy_desc first slot");
    pair(*second, source.original_format, source.original_dhash, "original metadata: whole pool copy_desc last slot");
}
} // namespace
void test_dx11_original_resource_metadata_regression()
{
    try {
        BatchDevice device;
        OriginalConfig config;
        config.load();
        exercise(device);
        std::fputs("PASS: original resource metadata INI, exact hashes, declaration/reset and actual copy/pool/THIS/range paths\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: original resource metadata: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
