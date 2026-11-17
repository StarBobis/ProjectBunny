#include "slot_range_test_support.h"
#include "IniHandler.h"
#include "Input.h"
#include "Override.h"
#include "ExpressionFunctions.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

extern std::vector<CommandList*> pending_post_command_lists;

namespace {
using namespace dx11_slot_test;

const unsigned char expected_saved[] = {0x44, 0x33, 0x22, 0x11};
const unsigned char expected_lazy[] = {0x88, 0x77, 0x66, 0x55};

class OwnershipConfig {
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
    OwnershipConfig()
    {
        wchar_t executable[MAX_PATH] = {};
        require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0,
            "F109: locate regression host");
        const auto base = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& part : base)
            in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        require_batch(in_build, "F109: fixtures must remain under build/");
        config = base / L"d3dx.ini";
        settings = base / (L"initial_data_ownership_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        require_batch(!std::filesystem::exists(settings), "F109: refuse existing settings fixture");
        HANDLE file = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        require_batch(file != INVALID_HANDLE_VALUE, "F109: refuse existing d3dx.ini");
        CloseHandle(file);
        owns_config = true;
        file = CreateFileW(settings.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            DeleteFileW(config.c_str());
            owns_config = false;
            throw std::runtime_error("F109: refuse existing initial-data user fixture");
        }
        CloseHandle(file);
        owns_settings = true;
        migoto_handle = nullptr;
    }

    void load()
    {
        std::ofstream out(config, std::ios::binary | std::ios::trunc);
        out << "[Logging]\nlog_level=disabled\nshow_warnings=0\n[Hunting]\nhunting=0\n"
            "[System]\nskip_early_includes_load=0\n[Include]\nuser_config=" << settings.filename().string() << "\n"
            "[PoolF109Saved]\npool_size=8\ntype=Buffer\nformat=R32_UINT\ndata=0x11223344\n"
            "[PoolF109Alias]\npool_size=8\n"
            "[PoolF109Lazy]\npool_size=2\ntype=Buffer\nformat=R32_UINT\ndata=0x55667788\n"
            "[PoolF109Grow]\npool_size=2\ntype=Buffer\nformat=R32_UINT\ndata=0x11223344\nbyte_width=16\n";
        out.close();
        require_batch(out.good(), "F109: write complete production INI");
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
        ClearKeyBindings();
        LoadConfigFile();
    }

    ~OwnershipConfig()
    {
        ClearKeyBindings();
        pending_post_command_lists.clear();
        CurrentTransition.Stop();
        OverrideSave.Reset(nullptr);
        presetOverrides.clear();
        G->constants_command_list.clear();
        G->post_constants_command_list.clear();
        registered_command_lists.clear();
        explicitCommandListSections.clear();
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
        if (owns_config)
            DeleteFileW(config.c_str());
        if (owns_settings)
            DeleteFileW(settings.c_str());
    }
};

void copy_alias(BatchDevice& device)
{
    CommandList commands;
    require_batch(parse(commands, L"PoolF109Alias", L"copy_desc PoolF109Saved"),
        "F109: parse actual copy_desc pool command");
    execute(commands, device);
}

std::vector<unsigned char> read_buffer(BatchDevice& device, ID3D11Resource* resource, UINT expected_size)
{
    ComPtr<ID3D11Buffer> buffer;
    require_batch(resource && SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&buffer))),
        "F109: buffer resource exists for readback");
    D3D11_BUFFER_DESC desc{};
    buffer->GetDesc(&desc);
    require_batch(desc.ByteWidth == expected_size, "F109: buffer ByteWidth matches expected growth");
    D3D11_BUFFER_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    staging_desc.StructureByteStride = 0;
    ComPtr<ID3D11Buffer> staging;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&staging_desc, nullptr, &staging)),
        "F109: create real staging buffer");
    device.context->CopyResource(staging.Get(), buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "F109: map actual buffer bytes");
    std::vector<unsigned char> bytes(expected_size);
    std::memcpy(bytes.data(), mapped.pData, expected_size);
    device.context->Unmap(staging.Get(), 0);
    return bytes;
}

void check_grown_bytes(const std::vector<unsigned char>& bytes, const unsigned char (&prefix)[4], const char* message)
{
    require_batch(bytes.size() == 16 && !std::memcmp(bytes.data(), prefix, 4), message);
    for (size_t i = 4; i < bytes.size(); ++i)
        require_batch(bytes[i] == 0, message);
}

// R1: the copy_desc destination template must own an independent snapshot.
void test_copy_desc_snapshot(BatchDevice& device)
{
    auto& source = customResources.at(L"poolf109saved_template");
    auto& target = customResources.at(L"poolf109alias_template");
    require_batch(source.initial_data && source.initial_data_size == 4 &&
        !std::memcmp(source.initial_data, expected_saved, 4),
        "F109 R1: parsed pool template carries data=0x11223344");
    require_batch(!target.initial_data && target.initial_data_size == 0,
        "F109 R1: alias template starts empty");
    copy_alias(device);
    void* source_data = source.initial_data;
    void* target_data = target.initial_data;
    const bool independent = source_data && target_data && target_data != source_data;
    const bool same_bytes = independent && source.initial_data_size == target.initial_data_size &&
        !std::memcmp(target_data, source_data, source.initial_data_size);

    // Detach before asserting: the old implementation aliases both pointers, and
    // its destructor would turn the deterministic red into a double-free crash.
    if (target_data && target_data != source_data)
        std::free(target_data);
    if (source_data)
        std::free(source_data);
    source.initial_data = nullptr;
    source.initial_data_size = 0;
    target.initial_data = nullptr;
    target.initial_data_size = 0;

    require_batch(independent && same_bytes,
        "F109 R1: copy_desc pool template owns an independent initial-data snapshot");
}

// R2: a lazily initialised pool element must own an independent snapshot.
void test_lazy_element_snapshot()
{
    auto& pool = customResourcePools.at(L"poolf109lazy");
    CustomResource* element = pool.GetResource(0, false, false, true);
    require_batch(element && element->initial_data && pool.resource_template->initial_data,
        "F109 R2: lazy element and template both carry initial data");
    void* template_data = pool.resource_template->initial_data;
    void* element_data = element->initial_data;
    const bool independent = element_data != template_data;
    const bool same_bytes = independent &&
        element->initial_data_size == pool.resource_template->initial_data_size &&
        !std::memcmp(element_data, template_data, element->initial_data_size) &&
        !std::memcmp(element_data, expected_lazy, 4);

    if (element_data && element_data != template_data)
        std::free(element_data);
    if (template_data)
        std::free(template_data);
    element->initial_data = nullptr;
    element->initial_data_size = 0;
    pool.resource_template->initial_data = nullptr;
    pool.resource_template->initial_data_size = 0;

    require_batch(independent && same_bytes,
        "F109 R2: lazy pool element owns an independent initial-data snapshot");
}

// G1: byte_width growth must realloc only the element's own snapshot and keep
// the template intact for later elements.
void test_byte_width_growth(BatchDevice& device)
{
    auto& pool = customResourcePools.at(L"poolf109grow");
    CustomResource* resource_template = pool.resource_template;
    void* template_data = resource_template->initial_data;
    require_batch(template_data && resource_template->initial_data_size == 4 &&
        !std::memcmp(template_data, expected_saved, 4),
        "F109 G1: grow template starts with four data bytes");

    CustomResource* element0 = pool.GetResource(0, false, false, true);
    require_batch(element0 && element0->initial_data != template_data && element0->initial_data_size == 4,
        "F109 G1: element 0 starts from an independent four-byte snapshot");
    element0->Substantiate(device.native.Get(), D3D11_BIND_SHADER_RESOURCE,
        static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
    require_batch(element0->resource != nullptr, "F109 G1: element 0 substantiated a real WARP buffer");
    require_batch(element0->initial_data_size == 16,
        "F109 G1: successful realloc synchronises initial_data_size to 16");
    check_grown_bytes(read_buffer(device, element0->resource, 16), expected_saved,
        "F109 G1: element 0 readback is 44 33 22 11 followed by zeroes");

    require_batch(resource_template->initial_data == template_data &&
        resource_template->initial_data_size == 4 &&
        !std::memcmp(template_data, expected_saved, 4),
        "F109 G1: growing element 0 leaves the template snapshot untouched");

    CustomResource* element1 = pool.GetResource(1, false, false, true);
    require_batch(element1 && element1 != element0 &&
        element1->initial_data && element1->initial_data != template_data &&
        element1->initial_data_size == 4 && !std::memcmp(element1->initial_data, expected_saved, 4),
        "F109 G1: element 1 lazily copies the still-valid template snapshot");
    element1->Substantiate(device.native.Get(), D3D11_BIND_SHADER_RESOURCE,
        static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
    require_batch(element1->resource != nullptr && element1->initial_data_size == 16,
        "F109 G1: element 1 grows and records its own size");
    check_grown_bytes(read_buffer(device, element1->resource, 16), expected_saved,
        "F109 G1: element 1 readback matches element 0 after independent growth");
}

// G2: the production reload/exit sequence must free the two snapshots
// independently instead of double-freeing a shared pointer.
void test_reload_heap_safety(BatchDevice& device, OwnershipConfig& config)
{
    for (unsigned round = 0; round < 2; ++round) {
        config.load();
        copy_alias(device);
        auto& source = customResources.at(L"poolf109saved_template");
        auto& target = customResources.at(L"poolf109alias_template");
        require_batch(source.initial_data && target.initial_data &&
            source.initial_data != target.initial_data &&
            source.initial_data_size == target.initial_data_size &&
            !std::memcmp(source.initial_data, target.initial_data, source.initial_data_size),
            "F109 G2: reload round keeps independent equal snapshots");
        // Exact IniHandler reload order: pools first, then resources.
        customResourcePools.clear();
        customResources.clear();
    }
}

void run_initial_data_ownership_regression()
{
    BatchDevice device;
    OwnershipConfig config;
    config.load();
    test_copy_desc_snapshot(device);
    test_lazy_element_snapshot();
    test_byte_width_growth(device);
    test_reload_heap_safety(device, config);
}
} // namespace

void test_dx11_initial_data_ownership_regression()
{
    try {
        run_initial_data_ownership_regression();
        std::fputs("PASS: initial_data independent snapshots, byte_width growth and reload heap safety\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: initial_data ownership regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
