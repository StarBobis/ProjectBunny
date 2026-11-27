#include "slot_range_test_support.h"
#include "FrameAnalysis.h"
#include "IniHandler.h"
#include "ShaderRegex.h"
#include "Input.h"
#include "Override.h"
#include "vkeys.h"
#include "tests/IniXInputCases.h"
#include "tests/IniKeyCycleCases.h"
#include "tests/IniKeyTransitionCases.h"
#include "HLSLDecompiler/DecompileHLSL.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <thread>
#include <d3d11shader.h>
#include "D3D_Shaders/stdafx.h"
#include "bounded_path_format.h"

extern std::vector<CommandList*> pending_post_command_lists;
extern unsigned f105_disassemble_calls;
extern bool f105_disassemble_fail;
std::array<unsigned char,4> render_dx11_audit_constant_pixel(dx11_slot_test::BatchDevice&, ID3D11PixelShader*);

void test_dx11_dx9_constant_reset_regression();
void test_dx11_customdata_length_regression();
void test_dx11_dx9_register_bounds_regression();
void test_dx11_dx9_dcl_const_without_ctab_regression();
void test_dx11_tokenizer_regression();
void check_dx11_dx9_sampler_dimension(const void*,size_t,unsigned);
void test_dx11_dx9_sampler_dimension_regression();
void test_dx11_assembler_double_index_regression();
void test_dx11_assembler_interpolation_regression();
void test_dx11_signature_register_eof_regression();
void test_dx11_signature_feature_comment_regression();
void test_dx11_shader_replacement_ownership_regression();
void test_dx11_finish_failure_regression();
void test_dx11_shader_diagnostic_failure_regression();
void test_dx11_texture_initial_debug_regression();
void test_dx11_texture_hash_layout_regression();
void test_dx11_empty_hunting_present_regression();
void test_dx11_f113_swap_chain_fallback_regression();
void test_dx11_f113_rc_regression();
void test_dx11_f113_ra_regression();
void test_dx11_swapchain_destructor_protocol_regression();
void test_dx11_reload_shader_filename_regression();
void test_dx11_masked_flags_regression();
void test_dx11_crash_help_regression();
void test_dx11_ini_enumeration_regression();
void test_dx11_ini_invalid_utf8_regression();
void test_dx11_stencil_diagnostic_regression();
void test_dx11_view_reference_null_diagnostic_regression();
void test_dx11_zbuffer_view_ownership_regression();
void test_dx11_hunting_shader_selection_regression();
void test_dx11_ini_lookup_performance();
void test_dx11_file_publication_regression();
void test_persist_write_failure_regression();
void test_dx11_device_initialization_regression();
void test_dx11_vertex_dump_bounds_regression();
void test_dx11_appended_vertex_layout_regression();
void test_dx11_decompiler_literal_regression();
void test_dx11_decompiler_negative_literal_regression();
void test_dx11_decompiler_next_line_regression();
void test_dx11_decompiler_next_line_bounded_regression();
void test_dx11_decompiler_skip_packing_regression();
void test_dx11_decompiler_f277_diagnostic_bounds_regression();
void test_dx11_decompiler_output_signature_regression();
void test_dx11_decompiler_special_output_regression();
void test_dx11_decompiler_immediate_regression();
void check_dx11_immediate_bytecode_precision(const void*, size_t, const void*, size_t);
bool dx11_immediate_bytecode_precision_matches(const void*, size_t, const void*, size_t);
void check_dx11_special_output_rows(const std::string&, const std::vector<std::string>&);
void check_dx11_special_output_failure(const std::string&);
void test_dx11_dx9_binding_lifetime_regression();
void test_dx11_decompiler_suffix_regression();
void test_dx11_decompiler_swizzle_slot_bound_regression();
void test_dx11_decompiler_texture_literal_regression();
void test_dx11_shader_regex_declaration_regression();
void test_dx11_shader_regex_temp_count_regression();
void check_dx11_decompiler_lifetime(const void*,size_t,const char*,size_t);
void test_dx11_decompiler_lifetime_regression();
void test_dx11_decode_dxbc_bounds_regression();
void test_dx11_decompiler_decode_bounds_regression();
void test_dx11_reflect_bounds_regression();
void test_dx11_resource_filename_regression();
void test_dx11_pool_access_regression();
void test_dx11_initial_data_ownership_regression();
void test_dx11_decompile_file_diagnostic_regression();
void test_dx11_parameter_conversion_regression();
void test_dx11_assembler_declaration_regression();
void test_dx11_assembler_word_progress_regression();
void test_dx11_precision_delimiter_review();
void test_dx11_assembler_line_bounds_regression();
void test_dx11_shader_tool_read_regression();
void test_dx11_shader_tool_write_regression();
void test_dx11_automatic_draw_count_regression();
void test_dx11_shader_hash_miss_regression();
void test_dx11_shader_header_file_regression();
void test_dx11_resource_override_validation_regression();
void test_dx11_resource_override_lifetime_regression(const std::filesystem::path&, const std::filesystem::path&);
void test_dx11_ini_upload_failure_regression();
void test_dx11_shader_model_bounds_regression();
void test_dx11_initialization_gate_reliability_regression();
void test_dx11_version_regression();
void test_dx11_decompiler_swizzle_count_regression();
void test_dx11_decompiler_f103_red_r1();
void test_dx11_decompiler_f103_red_r2();
void test_dx11_decompiler_f103_red_r3();
void test_dx11_decompiler_f103_red_r4();
void test_dx11_decompiler_f103_red_r5();
void test_dx11_decompiler_f103_red_m1();
void test_dx11_decompiler_f103_red_m2();
void test_dx11_decompiler_f103_red_m3();
void test_dx11_debug_layer_interfaces_regression();
void test_dx11_hook_lookup_regression();
void test_dx11_hook_release_regression();
void test_dx11_hook_install_regression();
void test_dx11_f197_override_lifetime_regression();
void test_dx11_texture_override_cache_regression();
void test_dx11_loader_file_bounds_regression();
void test_dx11_legacy_profiling_regression();
void test_dx11_auto_texture_check_regression();
void test_dx11_draw_dispatch_regression();
void test_f200();

#ifdef DX11_AUDIT_ASAN_LOCKS
void test_dx11_lock_cleanup_regression();
void test_dx11_lock_name_concurrency_regression();
#endif

namespace {
using namespace dx11_slot_test;

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require_batch(input.good(), "audit: read actual output file");
    return std::string(std::istreambuf_iterator<char>(input), {});
}

class AuditDumpScope {
    std::wstring old_path = G->ANALYSIS_PATH;
    bool old_analysis = G->analyse_frame, old_registered = G->frame_analysis_registered;
    bool old_foreground = G->check_foreground_window;
    std::wstring old_title = G->additionalForegroundWindowTitle;
    int old_hunting = G->hunting;
    FrameAnalysisOptions old_current = G->cur_analyse_options, old_default = G->def_analyse_options;
public:
    std::filesystem::path path;
    AuditDumpScope()
    {
        wchar_t executable[MAX_PATH]{};
        require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "audit: locate test host");
        const auto base = std::filesystem::weakly_canonical(std::filesystem::path(executable).parent_path());
        bool under_build = false;
        for (const auto& part : base) under_build |= _wcsicmp(part.c_str(), L"build") == 0;
        require_batch(under_build, "audit: outputs remain under build");
        path = base / (L"audit-dump-" + std::to_wstring(GetCurrentProcessId()));
        require_batch(!std::filesystem::exists(path) && std::filesystem::create_directory(path), "audit: exclusive output directory");
        wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, path.c_str());
        G->analyse_frame = G->frame_analysis_registered = true;
        G->check_foreground_window = true;
        G->additionalForegroundWindowTitle.clear();
        G->hunting = HUNTING_MODE_DISABLED;
        G->cur_analyse_options = G->def_analyse_options = FrameAnalysisOptions::INVALID;
    }
    ~AuditDumpScope()
    {
        G->analyse_frame = old_analysis; G->frame_analysis_registered = old_registered;
        G->check_foreground_window = old_foreground; G->additionalForegroundWindowTitle = std::move(old_title);
        G->hunting = old_hunting; G->cur_analyse_options = old_current; G->def_analyse_options = old_default;
        wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, old_path.c_str());
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    bool payload_contains(const char* expected)
    {
        for (const auto& file : std::filesystem::directory_iterator(path / L"deduped")) {
            if (file.path().extension() == L".txt" && read_file(file.path()).find(expected) != std::string::npos) return true;
        }
        return false;
    }
};

void test_f001()
{
    BatchDevice device;
    AuditDumpScope output;
    auto original = std::move(device.wrapper);
    auto analysis = std::make_unique<FrameAnalysisContext>(device.native.Get(), device.context.Get());
    analysis->SetHackerDevice(device.device);
    auto* context = analysis.get();
    device.wrapper = std::move(analysis);
    device.device->SetHackerContext(context);
    device.state.mHackerContext = context;
    try {
        auto vertices = buffer(device, 16, D3D11_BIND_VERTEX_BUFFER);
        const float values[] = {1, 2, 3, 4};
        device.context->UpdateSubresource(vertices.Get(), 0, nullptr, values, 0, 0);
        const auto options = static_cast<FrameAnalysisOptions>(static_cast<unsigned>(FrameAnalysisOptions::DUMP_VB) |
            static_cast<unsigned>(FrameAnalysisOptions::FMT_BUF_TXT) | static_cast<unsigned>(FrameAnalysisOptions::MONO));
        std::fputs("F001: non-draw VB dump with null call_info\n", stderr); std::fflush(stderr);
        context->FrameAnalysisDump(vertices.Get(), options, L"audit-nondraw", DXGI_FORMAT_UNKNOWN, 4, 0);
        require_batch(output.payload_contains("stride: 4\r\n"), "F001: non-draw path writes the real VB header and payload");
        require_batch(!output.payload_contains("first instance:"), "F001: absent draw info does not invent instance metadata");

        ID3D11Buffer* bound = vertices.Get(); UINT stride = 4, offset = 0;
        context->IASetVertexBuffers(0, 1, &bound, &stride, &offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->FrameAnalysisTrigger(options); context->Draw(4, 0);
        context->FrameAnalysisTrigger(options); context->DrawInstanced(4, 2, 0, 3);
        require_batch(output.payload_contains("first instance: 3\r\ninstance count: 2\r\n"),
            "F001: valid instanced draws retain both metadata values");
        context->FrameAnalysisDump(vertices.Get(), options, L"audit-zero-stride", DXGI_FORMAT_UNKNOWN, 0, 0);
        require_batch(output.payload_contains("stride: 0\r\n"), "F001: zero stride reaches its existing safe rejection");
    } catch (...) {
        device.wrapper = std::move(original); device.device->SetHackerContext(device.wrapper.get());
        device.state.mHackerContext = device.wrapper.get();
        throw;
    }
    device.wrapper = std::move(original); device.device->SetHackerContext(device.wrapper.get());
    device.state.mHackerContext = device.wrapper.get();
}

void test_f106()
{
    const char source[] = "float4 main():SV_Target{return float4(1,2,3,4);}";
    ComPtr<ID3DBlob> code, errors;
    require_batch(SUCCEEDED(D3DCompile(source, sizeof(source)-1, nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0,
        &code, &errors)), "F106: compile valid original DXBC using the real compiler");
    const auto* bytes = static_cast<const unsigned char*>(code->GetBufferPointer());
    std::vector<unsigned char> original(bytes, bytes + code->GetBufferSize());
    std::vector<unsigned char> assembly;
    require_batch(SUCCEEDED(disassembler(&original, &assembly, nullptr)), "F106: disassemble real valid shader");
    std::vector<char> text(assembly.begin(), assembly.end());
    const auto rebuilt = assembler(&text, original);
    BatchDevice device;
    ComPtr<ID3D11PixelShader> shader;
    require_batch(!rebuilt.empty() && SUCCEEDED(device.native->CreatePixelShader(rebuilt.data(), rebuilt.size(), nullptr, &shader)),
        "F106: valid round-trip bytecode remains accepted by WARP");
    for (const char* invalid : {"\n", "// no shader model\n", "", "ret\n", "ret\nret\nret\n", "unknown\nret\nret\n"}) {
        for (bool collect : {false, true}) {
            std::vector<char> input(invalid, invalid + std::strlen(invalid));
            std::vector<AssemblerParseError> warnings;
            bool rejected = false;
            std::fputs("F106: reject missing model before writing the token length\n", stderr); std::fflush(stderr);
            try { assembler(&input, original, collect ? &warnings : nullptr); }
            catch (const AssemblerParseError&) { rejected = true; }
            require_batch(rejected, "F106: missing model is a fatal parse error with or without warning collection");
        }
    }
    ComPtr<ID3DBlob> native_assembly;
    require_batch(SUCCEEDED(D3DDisassemble(code->GetBufferPointer(),code->GetBufferSize(),0,nullptr,&native_assembly)),
        "F106 correction: actual native assembly for warning-collector recovery");
    const std::string prefixed=std::string("ret\n")+static_cast<const char*>(native_assembly->GetBufferPointer());
    std::vector<char> recover_input(prefixed.begin(),prefixed.end()); std::vector<AssemblerParseError> recover_warnings;
    const auto recovered=assembler(&recover_input,original,&recover_warnings);
    shader.Reset();
    require_batch(recover_warnings.size()==1 && recover_warnings.front().line_no==1 &&
        SUCCEEDED(device.native->CreatePixelShader(recovered.data(),recovered.size(),nullptr,&shader)),
        "F106 correction: a rejected prefix does not prevent a later valid model from recovering under the existing collector policy");
    wchar_t executable[MAX_PATH]{};
    require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "F106: locate owned shader-tool fixture");
    const auto fixture = std::filesystem::path(executable).parent_path() / L"shader-fixture";
    require_batch(std::filesystem::create_directories(fixture / L"ShaderCache"), "F106: exclusive shader-tool fixture");
    const std::pair<std::filesystem::path, const std::vector<unsigned char>*> outputs[] = {
        {fixture / L"ShaderCache/0123456789abcdef-ps.bin", &original}, {fixture / L"expected.cbo", &rebuilt}};
    for (const auto& item : outputs) {
        std::ofstream output(item.first, std::ios::binary);
        output.write(reinterpret_cast<const char*>(item.second->data()), item.second->size());
        output.close();
        require_batch(output.good(), "F106: persist exact native-validated shader bytes for CLI integration");
    }
}

void test_dx11_disassembler_header_bounds_regression()
{
    auto put32 = [](std::vector<byte_t>& b, size_t off, DWORD v) {
        require_batch(off <= b.size() && b.size() - off >= sizeof(v),
            "F105: fixture word bounds");
        std::memcpy(b.data() + off, &v, sizeof(v));
    };
    auto reject = [](std::vector<byte_t> input, const char* what) {
        const auto original = input;
        std::vector<byte_t> output{0x41, 0x42, 0x43};
        const auto previous = output;
        const unsigned calls = f105_disassemble_calls;
        std::fprintf(stderr, "F105: reject %s\n", what); std::fflush(stderr);
        require_batch(FAILED(disassembler(&input, &output, nullptr)), what);
        require_batch(input == original && output == previous,
            "F105: failure leaves input/output untouched");
        require_batch(f105_disassemble_calls == calls,
            "F105: rejection happens before D3DDisassemble");
    };

    // T0/T1: no complete 32-byte fixed header.
    reject({}, "F105: empty input");
    for (size_t n = 1; n < 32; ++n)
        reject(std::vector<byte_t>(n, 0), "F105: short fixed header");

    std::vector<byte_t> header(32, 0);
    std::memcpy(header.data(), "DXBC", 4);
    put32(header, 20, 1);
    put32(header, 24, 32);
    // T2: zero chunks is no longer S_FALSE.
    put32(header, 28, 0);
    reject(header, "F105: zero chunks");
    // T3/T4: count must fit the 32-bit directory without wrapping 4*n.
    put32(header, 28, 0xFFFFFFFFu);
    reject(header, "F105: count 0xFFFFFFFF");
    put32(header, 28, 0x40000000u);
    reject(header, "F105: count 0x40000000");

    // T5/T6: directory offsets must leave an 8-byte chunk header in range.
    std::vector<byte_t> one(36, 0);
    std::memcpy(one.data(), "DXBC", 4);
    put32(one, 20, 1);
    put32(one, 24, 36);
    put32(one, 28, 1);
    put32(one, 32, 0xFFFFFFFFu);
    reject(one, "F105: directory offset 0xFFFFFFFF");
    put32(one, 32, 34);
    reject(one, "F105: directory offset ends 2 bytes early");

    // T7: chunk payload length must stay inside the buffer.
    std::vector<byte_t> chunk(48, 0);
    std::memcpy(chunk.data(), "DXBC", 4);
    put32(chunk, 20, 1);
    put32(chunk, 24, 48);
    put32(chunk, 28, 1);
    put32(chunk, 32, 36);
    std::memcpy(chunk.data() + 36, "SHEX", 4);
    put32(chunk, 40, 0xFFFFFFFFu);
    reject(chunk, "F105: chunk length overruns buffer");

    // T8: a container without a SHEX/SHDR code chunk is rejected.
    std::vector<byte_t> no_code(64, 0);
    std::memcpy(no_code.data(), "DXBC", 4);
    put32(no_code, 20, 1);
    put32(no_code, 24, 64);
    put32(no_code, 28, 2);
    put32(no_code, 32, 40);
    put32(no_code, 36, 52);
    std::memcpy(no_code.data() + 40, "ISGN", 4); put32(no_code, 44, 4);
    std::memcpy(no_code.data() + 52, "PRIV", 4); put32(no_code, 56, 4);
    reject(no_code, "F105: no SHEX/SHDR chunk");

    ComPtr<ID3DBlob> code, errors;
    const char source[] = "float4 main():SV_Target{return float4(0.25,0.5,0.75,1);}";
    require_batch(SUCCEEDED(D3DCompile(source, sizeof(source) - 1, nullptr, nullptr,
        nullptr, "main", "ps_5_0", 0, 0, &code, &errors)),
        "F105: compile real pixel shader");
    const auto* data = static_cast<const byte_t*>(code->GetBufferPointer());
    const std::vector<byte_t> original(data, data + code->GetBufferSize());
    DWORD n;
    std::memcpy(&n, original.data() + 28, 4);
    require_batch(n >= 1 && 32 + 4 * static_cast<size_t>(n) <= original.size(),
        "F105: real fixture has a complete directory");

    auto bad_magic = original;
    std::memcpy(bad_magic.data(), "DXB0", 4);
    reject(bad_magic, "F105: non-DXBC magic");

    // T9: a count in directory capacity whose extra entry is invalid.
    auto extra = original;
    put32(extra, 28, n + 1);
    put32(extra, 32 + 4 * static_cast<size_t>(n), 0xFFFFFFFFu);
    reject(extra, "F105: count+1 with invalid extra directory entry");

    // T10: the last directory entry points at the final two bytes.
    auto last_short = original;
    put32(last_short, 32 + 4 * static_cast<size_t>(n - 1),
        static_cast<DWORD>(last_short.size()) - 2);
    reject(last_short, "F105: last directory entry ends 2 bytes early");

    // T11: the code chunk's declared length overruns the container.
    auto long_code = original;
    bool code_chunk_found = false;
    for (DWORD i = 0; i < n; i++) {
        DWORD off;
        std::memcpy(&off, original.data() + 32 + 4 * static_cast<size_t>(i),
            sizeof(off));
        if (off > original.size() || original.size() - off < 8)
            continue;
        if (!std::memcmp(original.data() + off, "SHEX", 4) ||
                !std::memcmp(original.data() + off, "SHDR", 4)) {
            put32(long_code, off + 4, 0xFFFFFFFFu);
            code_chunk_found = true;
            break;
        }
    }
    require_batch(code_chunk_found, "F105: real fixture contains a code chunk");
    reject(long_code, "F105: code chunk length overruns buffer");

    // Green path: the same container still disassembles, once.
    std::vector<byte_t> input = original, text;
    const unsigned before = f105_disassemble_calls;
    require_batch(SUCCEEDED(disassembler(&input, &text, nullptr)),
        "F105: real shader still disassembles");
    const std::string text_string(text.begin(), text.end());
    require_batch(input == original && text_string.find("ps_5_0") != std::string::npos &&
        f105_disassemble_calls == before + 1,
        "F105: valid path preserves input, emits the model line and calls the SDK once");

    // Aliased input/output must still publish the complete text.
    std::vector<byte_t> alias = original;
    require_batch(SUCCEEDED(disassembler(&alias, &alias, nullptr)) && !alias.empty(),
        "F105: aliased input/output still publishes complete text");
    const std::string alias_string(alias.begin(), alias.end());
    require_batch(alias_string.find("ps_5_0") != std::string::npos,
        "F105: aliased output is disassembly text, not the original bytecode");

    // SDK failure propagation leaves the output untouched.
    std::vector<byte_t> failed{1, 2, 3};
    f105_disassemble_fail = true;
    const HRESULT hr = disassembler(&input, &failed, nullptr);
    f105_disassemble_fail = false;
    require_batch(hr == E_OUTOFMEMORY && failed == std::vector<byte_t>({1, 2, 3}),
        "F105: SDK failure propagates and leaves output untouched");
}

class AuditConfigScope {
    HINSTANCE old_module = migoto_handle;
    LogVerbosity old_log_level = gLogVerbosity;
    bool old_debug = gLogDebug, old_warnings = G->gShowWarnings, old_initialized = G->gConfigInitialized;
    std::wstring old_user = G->user_config;
    int old_dirty = G->user_config_dirty;
    bool old_automatic = G->clear_unknown_settings, old_clear = G->clear_saved_persist_vars;
    bool old_mod_runtime_optimizations = G->ModRuntimeOptimizationsEnabled();
    int old_hunting = G->hunting;
    std::wstring old_shader_path = G->SHADER_PATH, old_shader_cache = G->SHADER_CACHE_PATH;
public:
    std::filesystem::path config, settings;
    AuditConfigScope()
    {
        wchar_t executable[MAX_PATH]{};
        require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "audit config: locate host");
        const auto base = std::filesystem::path(executable).parent_path();
        bool under_build = false;
        for (const auto& part : base) under_build |= _wcsicmp(part.c_str(), L"build") == 0;
        require_batch(under_build, "audit config: owned directory remains under build");
        config = base / L"d3dx.ini";
        settings = base / (L"audit-user-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        HANDLE file = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        require_batch(file != INVALID_HANDLE_VALUE, "audit config: do not overwrite another fixture");
        CloseHandle(file);
        migoto_handle = nullptr;
    }
    ~AuditConfigScope()
    {
        ClearKeyBindings(); G->constants_command_list.clear(); G->post_constants_command_list.clear();
        registered_command_lists.clear(); explicitCommandListSections.clear();
        customResourcePools.clear(); customResources.clear(); persistent_variables.clear(); command_list_globals.clear();
        migoto_handle = old_module; gLogVerbosity = old_log_level; gLogDebug = old_debug;
        G->gShowWarnings = old_warnings; G->gConfigInitialized = old_initialized;
        G->user_config = old_user; G->user_config_dirty = old_dirty;
        G->clear_unknown_settings = old_automatic; G->clear_saved_persist_vars = old_clear;
        G->mod_runtime_optimizations = old_mod_runtime_optimizations;
        G->hunting = old_hunting;
        wcscpy_s(G->SHADER_PATH, MAX_PATH, old_shader_path.c_str()); wcscpy_s(G->SHADER_CACHE_PATH, MAX_PATH, old_shader_cache.c_str());
        DeleteFileW(config.c_str()); DeleteFileW(settings.c_str());
    }
    void load(const char* constants, int hunting = 0, bool eager = false, const char* include_lines = "",
        const char* rendering_lines = "")
    {
        std::ofstream output(config, std::ios::binary | std::ios::trunc);
        output << "[Logging]\ncalls=0\nshow_warnings=0\n[System]\nskip_early_includes_load=0\n";
        if (eager) output << "config_initialization_delay=-1\n";
        if (*rendering_lines) output << "[Rendering]\n" << rendering_lines;
        output << "[Hunting]\nhunting=" << hunting << "\n[Include]\nuser_config=" << settings.filename().string() << "\n" << include_lines << "[Constants]\n" << constants;
        output.close(); require_batch(output.good(), "audit config: write complete production INI");
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
        ClearKeyBindings(); LoadConfigFile();
    }
};

void test_f114()
{
    AuditConfigScope config;
    std::fputs("F114: load all-flag Constants declarations without a variable name\n", stderr); std::fflush(stderr);
    config.load("global=1\nglobal persist=2\nGLOBAL LOCKED=3\nglobal persist\n"
        "global $audit_valid=7\nglobal persist $audit_persist=9\n", 0, false, "", "mod_runtime_optimizations=0\n");
    require_batch(!G->ModRuntimeOptimizationsEnabled(), "runtime optimizations: main INI can select the original paths");
    require_batch(command_list_globals.size() == 2 && command_list_globals.at(L"$audit_valid").fval == 7 &&
        command_list_globals.at(L"$audit_persist").fval == 9 && persistent_variables.size() == 1,
        "F114: reject missing names and continue parsing valid ordinary/persistent declarations");
    config.load("LOCKED gLoBal $audit_locked=1.5\nglobal $audit_locked=99\n"
        "g l $audit_abbrev=4\nglobal $audit_later=$audit_locked+2\n"
        "global $1illegal=9\n$audit_locked=70\n");
    require_batch(command_list_globals.size() == 3 &&
        command_list_globals.at(L"$audit_locked").fval == 1.5f &&
        (command_list_globals.at(L"$audit_abbrev").flags & VariableFlags::LOCKED) &&
        command_list_globals.at(L"$audit_later").fval == 3.5f,
        "shared variable grammar: flag order, prefixes, names, locked writes and prior initializer");
    config.load("global $audit_valid=11\n");
    require_batch(G->ModRuntimeOptimizationsEnabled(), "runtime optimizations: removing the setting restores the default on reload");
    require_batch(command_list_globals.size() == 1 && command_list_globals.at(L"$audit_valid").fval == 11 &&
        persistent_variables.empty(), "F114: corrected config reload replaces declarations normally");
}

void test_f135()
{
    require_batch(ParseVKey(L"0x00") == 0 && ParseVKey(L"0xff") == 255 &&
        ParseVKey(L"0x000041") == 65 && ParseVKey(L"A") == 65 && ParseVKey(L"a") == 65 &&
        ParseVKey(L"VK_F1") == VK_F1 && ParseVKey(L"space") == VK_SPACE,
        "F135: valid hexadecimal, letter and named keys retain their values");
    for (const wchar_t* invalid : {L"0x100", L"0x", L"0xG", L"0x41junk", L"0x-1", L"0x10000000000000000"}) {
        std::fprintf(stderr, "F135: reject malformed or out-of-range hexadecimal VK\n"); std::fflush(stderr);
        require_batch(ParseVKey(invalid) == -1, "F135: reject missing digits, trailing garbage, negatives and overflow");
        bool rejected = false;
        try { VKInputButton button(invalid); }
        catch (const std::exception&) { rejected = true; }
        require_batch(rejected, "F135: the real input-button consumer rejects the invalid key");
    }
    migoto::test::CheckXInputButtons([](const std::wstring &name, const migoto::XInputSnapshot &pads) {
        XInputButton button(name.c_str(), pads);
        return button.CheckState();
    }, require_batch);
    InputButtonList keyboardAndPad(L"ctrl XB1_LEFT_TRIGGER>64 no_XB4_A");
    for (const wchar_t *invalid : {L"XB0_A", L"XB5_A", L"XB2_BAD", L"XB_LEFT_TRIGGE"}) {
        bool rejected = false;
        try { XInputButton pad(invalid); }
        catch (const std::exception&) { rejected = true; }
        require_batch(rejected, "DX11 rejects bad port, name and truncated trigger");
    }
    VKInputButton valid(L"no_0x41");
    require_batch(valid.vkey == 'A' && valid.invert, "DX11 negated hexadecimal button");
    InputButtonList keyboard(L"no_modifiers VK_F10");
    InputButtonList mixed(L"ctrl no_shift XB1_A");
    for (const wchar_t *chord : {L"", L"ctrl not_a_button F10"}) {
        bool rejected = false;
        try { InputButtonList binding(chord); }
        catch (const std::exception&) { rejected = true; }
        require_batch(rejected, "DX11 rejects an entire empty or malformed chord");
    }
}

void test_f176()
{
    AuditConfigScope config;
    config.load("", 0, true);
    test_dx11_resource_override_lifetime_regression(config.config, config.settings);
}

#include "ini_parameter_audit.inl"
#include "key_cycle_audit.inl"
#include "preset_trigger_state_audit.inl"

void test_f137()
{
    const auto saved = G->decompiler_settings;
    AuditConfigScope config;
    config.load("[Rendering]\nfix_ZRepair_DepthTexture1=FirstDepth.w\nfix_ZRepair_DepthTexture2=SecondDepth.z\n");
    for (const auto* invalid : {"x", ".x", "ab", "Depth.q", "Depth.X", "Depth.R", "Depthx", "Depth.xy", ".w"}) {
        const auto text = std::string("[Rendering]\nfix_ZRepair_DepthTexture1=") + invalid +
            "\nfix_ZRepair_DepthTexture2=" + invalid + "\n";
        std::fprintf(stderr, "F137: reject malformed texture selector %s before splitting\n", invalid); std::fflush(stderr);
        config.load(text.c_str());
        require_batch(G->decompiler_settings.ZRepair_DepthTexture1 == "FirstDepth" && G->decompiler_settings.ZRepair_DepthTextureReg1 == 'w' &&
            G->decompiler_settings.ZRepair_DepthTexture2 == "SecondDepth" && G->decompiler_settings.ZRepair_DepthTextureReg2 == 'z',
            "F137: both invalid selectors are rejected without publishing partial settings");
    }
    for (const char component : {'x','y','z','w','r','g','b','a'}) {
        const auto shader_source=std::string("float4 main():SV_Target{float4 zpos4=float4(0.25,0.5,0.75,1);float zpos=zpos4.")+component+";return zpos;}";
        ComPtr<ID3DBlob> code,errors;
        require_batch(SUCCEEDED(D3DCompile(shader_source.data(),shader_source.size(),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),
            "F137 correction: every accepted component is a native HLSL swizzle");
        const auto text = std::string("[Rendering]\nfix_ZRepair_DepthTexture1=  SceneDepthTexture.") + component +
            " \t\nfix_ZRepair_DepthTexture2=OtherDepth." + component + "\n";
        config.load(text.c_str());
        require_batch(G->decompiler_settings.ZRepair_DepthTexture1 == "SceneDepthTexture" &&
            G->decompiler_settings.ZRepair_DepthTexture2 == "OtherDepth" &&
            G->decompiler_settings.ZRepair_DepthTextureReg1 == component && G->decompiler_settings.ZRepair_DepthTextureReg2 == component,
            "F137: preserve exact texture names and all eight valid HLSL component suffixes");
    }
    config.load("[Rendering]\nfix_ZRepair_DepthTexture1= \t\nfix_ZRepair_DepthTexture2=\n");
    require_batch(G->decompiler_settings.ZRepair_DepthTexture1 == "SceneDepthTexture" &&
        G->decompiler_settings.ZRepair_DepthTexture2 == "OtherDepth", "F137: empty or omitted values retain existing configuration semantics");
    G->decompiler_settings = saved;
}

void test_f138()
{
    test_dx11_parameter_conversion_regression();
    AuditConfigScope config;
    std::string overflow;
    for (int i = 0; i < 87; ++i) overflow += "\xe4\xb8\xad";
    std::string settings = "[Rendering]\n";
    for (const auto* key : {"fix_ZRepair_DepthTexture1", "fix_ZRepair_DepthTexture2", "fix_ZRepair_ZPosCalc1",
            "fix_ZRepair_Dependencies1", "fix_ZRepair_Dependencies2", "fix_InvTransform"})
        settings += std::string(key) + "=" + overflow + "\n";
    config.load(settings.c_str());
    require_batch(G->decompiler_settings.ZRepair_ZPosCalc1.empty() &&
        G->decompiler_settings.ZRepair_Dependencies1.empty() && G->decompiler_settings.ZRepair_Dependencies2.empty() &&
        G->decompiler_settings.InvTransforms.empty(), "F138: real INI loading rejects oversized UTF-8 parameter bytes");
    config.load("[Rendering]\nfix_ZRepair_DepthTexture1=SceneDepthTexture.x\n"
        "fix_ZRepair_DepthTexture2=OtherDepth.y\nfix_ZRepair_ZPosCalc1= alpha beta \n"
        "fix_ZRepair_Dependencies1=a,b\nfix_ZRepair_Dependencies2=c,d\nfix_InvTransform=e,f\n");
    const auto& settings_ok = G->decompiler_settings;
    require_batch(settings_ok.ZRepair_DepthTexture1 == "SceneDepthTexture" && settings_ok.ZRepair_DepthTextureReg1 == 'x' &&
        settings_ok.ZRepair_DepthTexture2 == "OtherDepth" && settings_ok.ZRepair_DepthTextureReg2 == 'y' &&
        settings_ok.ZRepair_ZPosCalc1 == "alpha beta" && settings_ok.ZRepair_Dependencies1 == std::vector<std::string>{"a","b"} &&
        settings_ok.ZRepair_Dependencies2 == std::vector<std::string>{"c","d"} && settings_ok.InvTransforms == std::vector<std::string>{"e","f"},
        "F138: subsequent real configuration preserves normal depth selectors and all three lists");
}

void test_f143()
{
    test_dx11_assembler_declaration_regression();
    const char source[] = "Texture2D<float4> tex:register(t0);float4 main():SV_Target{return tex.Load(int3(0,0,0));}";
    ComPtr<ID3DBlob> code, errors, assembly;
    require_batch(SUCCEEDED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)) &&
        SUCCEEDED(D3DDisassemble(code->GetBufferPointer(),code->GetBufferSize(),0,nullptr,&assembly)),
        "F143: native compiler produces actual resource declaration fixture");
    const auto* data=static_cast<const unsigned char*>(code->GetBufferPointer());
    const std::vector<unsigned char> original(data,data+code->GetBufferSize());
    const std::string valid(static_cast<const char*>(assembly->GetBufferPointer()));
    std::vector<char> normal(valid.begin(),valid.end()); std::vector<AssemblerParseError> warnings;
    const auto rebuilt=assembler(&normal,original,&warnings);
    BatchDevice device; ComPtr<ID3D11PixelShader> shader;
    require_batch(warnings.empty() && SUCCEEDED(device.native->CreatePixelShader(rebuilt.data(),rebuilt.size(),nullptr,&shader)),
        "F143: native WARP accepts unchanged supported declaration round trip");
    const std::string token="(float,float,float,float)";
    const auto position=valid.find(token);
    require_batch(position!=std::string::npos, "F143: locate actual native resource type");
    std::string invalid=valid; invalid.replace(position,token.size(),"(invalid,invalid,invalid,invalid)");
    std::vector<char> bad(invalid.begin(),invalid.end()); bool rejected=false;
    try { assembler(&bad,original); } catch(const AssemblerParseError& error) {rejected=error.line_no>0;}
    require_batch(rejected, "F143: public assembly without warning collection reports a line-numbered parse error");
    warnings.clear();
    const auto with_warnings=assembler(&bad,original,&warnings);
    require_batch(warnings.size()==1 && warnings.front().line_no>0 && !with_warnings.empty(),
        "F143: preserve explicit warning-collection compatibility policy while reporting the rejected instruction");
}

void test_f142()
{
    test_dx11_assembler_word_progress_regression();
    const char source[]="float4 main():SV_Target{return 1;}";
    ComPtr<ID3DBlob> code,errors;
    require_batch(SUCCEEDED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),
        "F142: actual native DXBC for the public shader replacement parser");
    const auto* data=static_cast<const unsigned char*>(code->GetBufferPointer());
    const std::vector<unsigned char> original(data,data+code->GetBufferSize());
    const std::string malformed="ps_5_0\nmov r0.x, l(1.0\nret\n";
    std::vector<char> text(malformed.begin(),malformed.end());
    bool rejected=false;
    try {assembler(&text,original);} catch(const AssemblerParseError& error) {rejected=error.line_no==2;}
    require_batch(rejected,"F142: user assembly missing a delimiter reports line 2 instead of hanging");
    std::vector<AssemblerParseError> warnings;
    const auto warned=assembler(&text,original,&warnings);
    require_batch(warnings.size()==1 && warnings.front().line_no==2 && !warned.empty(),
        "F142: explicit warning collection terminates and follows the existing continue policy");
}

std::vector<unsigned char> audit_shader_code(const std::vector<unsigned char>& binary)
{
    require_batch(binary.size()>=32,"shader code review: complete DXBC header");
    uint32_t count=0; std::memcpy(&count,binary.data()+28,4);
    require_batch(count<=(binary.size()-32)/4,"shader code review: complete chunk table");
    for(uint32_t i=0;i<count;++i) {
        uint32_t offset=0,length=0; std::memcpy(&offset,binary.data()+32+4*i,4);
        require_batch(offset<=binary.size()-8,"shader code review: complete chunk header");
        std::memcpy(&length,binary.data()+offset+4,4);
        require_batch(length<=binary.size()-offset-8,"shader code review: complete chunk payload");
        if(!std::memcmp(binary.data()+offset,"SHEX",4) || !std::memcmp(binary.data()+offset,"SHDR",4))
            return {binary.begin()+offset+8,binary.begin()+offset+8+length};
    }
    throw std::runtime_error("shader code review: native shader has no code chunk");
}

void test_f145_review()
{
    test_dx11_precision_delimiter_review();
    for(const auto* source : {"float4 main():SV_Target{return float4(0.02083333395421505,1.0000001192092896,-12345.6787109375,1.1754943508222875e-38);}",
            "float4 main(float4 p:SV_Position):SV_Target{const float4 values[2]={float4(0.123456789,2.23456789,3,4),float4(5.3456789,6,7,8)};return values[(uint)p.x%2];}"}) {
        ComPtr<ID3DBlob> code,errors;
        require_batch(SUCCEEDED(D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),
            "F145 review: actual native compiler creates scalar/vector and indexed constant shaders");
        const auto* data=static_cast<const unsigned char*>(code->GetBufferPointer());
        std::vector<unsigned char> original(data,data+code->GetBufferSize()),text;
        require_batch(SUCCEEDED(disassembler(&original,&text,nullptr)),"F145 review: actual native disassembly and precision correction terminate");
        std::vector<char> input(text.begin(),text.end());
        const auto rebuilt=assembler(&input,original);
        require_batch(audit_shader_code(rebuilt)==audit_shader_code(original),"F145 review: all native instruction bits survive precision round trip");
        BatchDevice device; ComPtr<ID3D11PixelShader> shader;
        require_batch(SUCCEEDED(device.native->CreatePixelShader(rebuilt.data(),rebuilt.size(),nullptr,&shader)),
            "F145 review: native WARP accepts the complete corrected shader");
    }
}

void test_f146()
{
    test_dx11_assembler_line_bounds_regression();
    const char source[]="float4 main():SV_Target{return float4(0.125,0.25,0.5,1);}";
    ComPtr<ID3DBlob> code,errors;
    require_batch(SUCCEEDED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),
        "F146: actual original shader for bounded user assembly");
    const auto* data=static_cast<const unsigned char*>(code->GetBufferPointer());
    std::vector<unsigned char> original(data,data+code->GetBufferSize()),text;
    require_batch(SUCCEEDED(disassembler(&original,&text,nullptr)),"F146: actual assembly fixture");
    while(!text.empty() && (text.back()=='\n' || text.back()=='\r')) text.pop_back();
    std::vector<char> input(text.begin(),text.end());
    require_batch(!input.empty() && input.back()!=0 && input.back()!='\n',"F146: real user input has no NUL or final newline");
    const auto rebuilt=assembler(&input,original);
    require_batch(audit_shader_code(rebuilt)==audit_shader_code(original),"F146: no-newline assembly preserves exact native instruction bytes");
    BatchDevice device; ComPtr<ID3D11PixelShader> shader;
    require_batch(SUCCEEDED(device.native->CreatePixelShader(rebuilt.data(),rebuilt.size(),nullptr,&shader)),
        "F146: WARP accepts the complete bounded result");
    std::vector<char> empty; bool rejected=false;
    try {assembler(&empty,original);} catch(const AssemblerParseError&) {rejected=true;}
    require_batch(rejected,"F146: an empty public input reaches the existing missing-model error instead of reading null");
}

class AuditAnalysisDeviceScope {
    BatchDevice& device;
    std::unique_ptr<HackerContext> original;
public:
    FrameAnalysisContext* context=nullptr;
    explicit AuditAnalysisDeviceScope(BatchDevice& value):device(value)
    {
        auto analysis=std::make_unique<FrameAnalysisContext>(device.native.Get(),device.context.Get());
        analysis->SetHackerDevice(device.device); context=analysis.get();
        original=std::move(device.wrapper); device.wrapper=std::move(analysis);
        device.device->SetHackerContext(context); device.state.mHackerContext=context;
    }
    ~AuditAnalysisDeviceScope()
    {
        auto analysis=std::move(device.wrapper);
        device.wrapper=std::move(original);
        device.device->SetHackerContext(device.wrapper.get()); device.state.mHackerContext=device.wrapper.get();
        analysis.reset();
    }
};

void test_indexed_indirect_args_dump()
{
    BatchDevice device;
    AuditDumpScope output;
    AuditAnalysisDeviceScope analysis(device);
    auto* context = analysis.context;
    auto args = buffer(device, 40, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS);
    const int initial[] = {0, 0, 0, 0, 0, 3, 0, 7, -2, 11};
    const int recorded[] = {0, 0, 0, 0, 0, 9, 0, 13, -4, 17};
    device.context->UpdateSubresource(args.Get(), 0, nullptr, initial, 0, 0);

    auto read_args = [&](const wchar_t* extension) {
        std::vector<std::string> files;
        for (const auto& file : std::filesystem::recursive_directory_iterator(output.path)) {
            if (file.path().parent_path().filename() != L"deduped" &&
                file.path().extension() == extension &&
                file.path().filename().wstring().find(L"-indirect-args") != std::wstring::npos)
                files.push_back(read_file(file.path()));
        }
        return files;
    };
    context->DrawIndexedInstancedIndirect(args.Get(), 20);
    require_batch(read_args(L".buf").empty(), "indexed args: no dump without an enabled option");
    G->cur_analyse_options = FrameAnalysisOptions::DUMP_IB;
    context->FrameAnalysisDumpIndexedIndirectArgs(args.Get(), 2);
    context->FrameAnalysisDumpIndexedIndirectArgs(args.Get(), 24);
    require_batch(read_args(L".buf").empty(), "indexed args: reject misalignment and truncated records");
    context->DrawIndexedInstancedIndirect(args.Get(), 20);
    const auto initial_bytes = std::string(reinterpret_cast<const char*>(initial + 5), 20);
    require_batch(read_args(L".buf") == std::vector<std::string>{initial_bytes},
        "indexed args: immediate draw dumps exactly the record at the API offset");
    const auto text = read_args(L".txt");
    require_batch(text.size() == 1 && text[0].find("base vertex: -2") != std::string::npos &&
        text[0].find("first instance: 11") != std::string::npos,
        "indexed args: text decodes signed base vertex and start instance");

    ComPtr<ID3D11DeviceContext> deferred;
    ComPtr<ID3D11DeviceContext1> deferred1;
    require_batch(SUCCEEDED(device.native->CreateDeferredContext(0, &deferred)) &&
        SUCCEEDED(deferred.As(&deferred1)), "indexed args: create WARP deferred context");
    auto recorder = std::make_unique<FrameAnalysisContext>(device.native.Get(), deferred1.Get());
    recorder->SetHackerDevice(device.device);
    G->cur_analyse_options = FrameAnalysisOptions::DUMP_INDIRECT;
    G->cur_analyse_options |= FrameAnalysisOptions::DEFRD_CTX_IMM;
    deferred->UpdateSubresource(args.Get(), 0, nullptr, recorded, 0, 0);
    recorder->DrawIndexedInstancedIndirect(args.Get(), 20);
    deferred->UpdateSubresource(args.Get(), 0, nullptr, initial, 0, 0);
    ComPtr<ID3D11CommandList> commands;
    require_batch(SUCCEEDED(recorder->FinishCommandList(FALSE, &commands)),
        "indexed args: finish the deferred draw and copies");
    require_batch(read_args(L".buf").size() == 1, "indexed args: no deferred read before execution");
    context->ExecuteCommandList(commands.Get(), FALSE);
    const auto dumped = read_args(L".buf");
    const auto recorded_bytes = std::string(reinterpret_cast<const char*>(recorded + 5), 20);
    require_batch(dumped.size() == 2 && std::count(dumped.begin(), dumped.end(), recorded_bytes) == 1,
        "indexed args: capture the preceding update and exclude the update after the draw");
}

std::string audit_read_native_buffer(BatchDevice& device,ID3D11Buffer* input)
{
    D3D11_BUFFER_DESC desc{}; input->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=desc.StructureByteStride=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    require_batch(SUCCEEDED(device.native->CreateBuffer(&desc,nullptr,&staging)),"F169: real staging buffer");
    device.context->CopyResource(staging.Get(),input);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"F169: read the real uploaded bytes");
    const std::string bytes(static_cast<const char*>(mapped.pData),desc.ByteWidth);
    device.context->Unmap(staging.Get(),0);
    return bytes;
}

std::map<std::wstring,std::string> audit_update_dump_files(const std::filesystem::path& path)
{
    std::map<std::wstring,std::string> files;
    for(const auto& file:std::filesystem::directory_iterator(path)) {
        if(file.is_regular_file() && file.path().extension()==L".buf" && file.path().filename().wstring().find(L"-update")!=std::wstring::npos)
            files.emplace(file.path().filename().wstring(),read_file(file.path()));
    }
    return files;
}

void test_f169()
{
    AuditDumpScope output; BatchDevice device; AuditAnalysisDeviceScope analysis(device);
    auto target=buffer(device,16,D3D11_BIND_VERTEX_BUFFER);
    const auto options=static_cast<FrameAnalysisOptions>(FrameAnalysisOptions::DUMP_ON_UPDATE | FrameAnalysisOptions::FMT_BUF_BIN);
    G->cur_analyse_options=options;
    std::array<UINT,4> values{11,22,33,44};
    auto expected=[&] {return std::string(reinterpret_cast<const char*>(values.data()),sizeof(values));};
    auto verify_upload=[&] {require_batch(audit_read_native_buffer(device,target.Get())==expected(),"F169: capture controls never change native upload data");};
    auto files=audit_update_dump_files(output.path);
    auto verify_dump=[&] {
        const auto next=audit_update_dump_files(output.path);
        require_batch(next.size()==files.size()+1,"F169: exactly one new capture is emitted per enabled update");
        for(const auto& entry:files) require_batch(next.at(entry.first)==entry.second,"F169: previous captures remain unchanged");
        for(const auto& entry:next) if(!files.count(entry.first)) require_batch(entry.second==expected(),"F169: capture contains the complete updated bytes");
        files=next;
    };
    std::fputs("F169: UpdateSubresource1 must emit one dump of the completed native upload\n",stderr); std::fflush(stderr);
    analysis.context->UpdateSubresource1(target.Get(),0,nullptr,values.data(),0,0,0);
    verify_upload(); verify_dump();
    const UINT partial[]={77,88}; const D3D11_BOX box{4,0,0,12,1,1};
    values[1]=partial[0]; values[2]=partial[1];
    analysis.context->UpdateSubresource1(target.Get(),0,&box,partial,0,0,0);
    verify_upload(); verify_dump();
    G->analyse_frame=false; values[0]=99;
    analysis.context->UpdateSubresource1(target.Get(),0,nullptr,values.data(),0,0,0); verify_upload();
    require_batch(audit_update_dump_files(output.path)==files,"F169: disabled analysis adds no capture");
    G->analyse_frame=true; G->cur_analyse_options=FrameAnalysisOptions::FMT_BUF_BIN; values[0]=111;
    analysis.context->UpdateSubresource1(target.Get(),0,nullptr,values.data(),0,0,0); verify_upload();
    require_batch(audit_update_dump_files(output.path)==files,"F169: missing dump_on_update adds no capture");
    G->cur_analyse_options=options; values[0]=222;
    analysis.context->UpdateSubresource(target.Get(),0,nullptr,values.data(),0,0); verify_upload(); verify_dump();
    const auto blocked=output.path/L"blocked-parent";
    {std::ofstream file(blocked,std::ios::binary); file<<"not a directory"; file.close(); require_batch(file.good(),"F169: actual filesystem capture failure fixture");}
    wcscpy_s(G->ANALYSIS_PATH,MAX_PATH,blocked.c_str()); values[0]=333;
    analysis.context->UpdateSubresource1(target.Get(),0,nullptr,values.data(),0,0,0); verify_upload();
    require_batch(audit_update_dump_files(output.path)==files,"F169: failed capture does not replace earlier output");
    wcscpy_s(G->ANALYSIS_PATH,MAX_PATH,output.path.c_str()); values[0]=444;
    analysis.context->UpdateSubresource1(target.Get(),0,nullptr,values.data(),0,0,0); verify_upload(); verify_dump();
}

void test_f186()
{
    AuditConfigScope config;
    const auto cache=config.config.parent_path()/L"regex-cache-audit";
    require_batch(std::filesystem::create_directory(cache),"F186: exclusive on-disk cache fixture");
    const auto settings=[](const char* alpha) {
        return std::string("[Rendering]\ncache_shaders=1\ncache_directory=regex-cache-audit\nshader_hash=fnv\n")+
            "[ShaderRegexAuditTail]\nshader_model=ps_5_0\nfilter_index=7.25\n"
            "[ShaderRegexAuditTail.Pattern]\nmov\\s+o0\\.xyzw,\\s*l\\(0\\.250000,\\s*0\\.500000,\\s*0\\.750000,\\s*1\\.000000\\)\n"
            "[ShaderRegexAuditTail.Pattern.Replace]\nmov o0.xyzw, l(0.250000, 0.500000, 0.750000, "+alpha+")\n";
    };
    config.load(settings("0.500000").c_str(),0,true);
    require_batch(shader_regex_groups.size()==1,"F186: production parser accepts the real regex group");
    uint32_t jit = 0;
    char engine[64]{};
    require_batch(pcre2_config(PCRE2_CONFIG_VERSION, engine) > 0 && std::strncmp(engine, "10.49 ", 6) == 0 &&
        pcre2_config(PCRE2_CONFIG_JIT, &jit) == 0 && jit == 1, "F186: production PCRE2 10.49 with JIT");
    const uint32_t first_hash=shader_regex_hash;
    config.load(settings("0.750000").c_str(),0,true);
    std::fputs("F186: changing only a replacement line's UTF-16 tail must invalidate its cache key\n",stderr); std::fflush(stderr);
    require_batch(shader_regex_hash!=first_hash,"F186: complete INI text contributes to the real configuration hash");

    BatchDevice device; ComPtr<ID3DBlob> code,errors;
    const char source[]="float4 main():SV_Target{return float4(0.25,0.5,0.75,1);}";
    require_batch(SUCCEEDED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),"F186: real original shader");
    const auto* data=static_cast<const unsigned char*>(code->GetBufferPointer());
    const std::vector<unsigned char> original(data,data+code->GetBufferSize());
    const UINT64 shader_hash=fnv_64_buf(original.data(),original.size());
    wchar_t stem[64]{}; swprintf_s(stem,L"%016llx-ps_regex",shader_hash);
    const auto meta=cache/(std::wstring(stem)+L".dat");
    const auto create_patch=[&](const char* alpha,unsigned char expected_alpha) {
        config.load(settings(alpha).c_str(),0,true);
        auto binary=original; std::vector<unsigned char> assembly;
        require_batch(SUCCEEDED(disassembler(&binary,&assembly,nullptr)),"F186: production disassembly input");
        std::string text(assembly.begin(),assembly.end()),model="ps_5_0"; std::wstring tagline;
        const bool changed=apply_shader_regex_groups(&text,L"ps",&model,shader_hash,&tagline);
        if(!changed) {std::fprintf(stderr,"F186 native assembly:\n%s\n",text.c_str()); std::fflush(stderr);}
        require_batch(changed,"F186: real PCRE group patches the expected native instruction");
        std::vector<char> input(text.begin(),text.end());
        auto patched=assembler(&input,original);
        save_shader_regex_cache_bin(shader_hash,L"ps",&patched);
        ComPtr<ID3D11PixelShader> shader;
        require_batch(SUCCEEDED(device.native->CreatePixelShader(patched.data(),patched.size(),nullptr,&shader)),"F186: native WARP accepts the real regex output");
        require_batch(render_dx11_audit_constant_pixel(device,shader.Get())==std::array<unsigned char,4>{64,128,191,expected_alpha},
            "F186: real GPU output distinguishes the replacement tail");
        std::vector<unsigned char> cached; std::wstring cached_tagline;
        require_batch(load_shader_regex_cache(shader_hash,L"ps",&cached,&cached_tagline)==ShaderRegexCache::PATCH && cached==patched,
            "F186: current-version cache hit returns exact patched bytes");
        uint32_t version=0; std::ifstream file(meta,std::ios::binary); file.read(reinterpret_cast<char*>(&version),sizeof(version));
        require_batch(file.good() && version==3,"F186: writer persists the explicit new cache version");
        return patched;
    };
    const auto patched_a=create_patch("0.500000",128);
    config.load(settings("0.750000").c_str(),0,true);
    auto unchanged=original; std::wstring tagline=L"unchanged";
    require_batch(load_shader_regex_cache(shader_hash,L"ps",&unchanged,&tagline)==ShaderRegexCache::NO_CACHE && unchanged==original && tagline==L"unchanged",
        "F186: reload rejects stale configuration cache without altering output");
    require_batch(!G->mShaderOverrideMap.count(shader_hash),"F186: stale cache rejection links no old group/filter");
    {
        const uint32_t legacy[]={2,shader_regex_hash,0,0};
        std::ofstream file(meta,std::ios::binary|std::ios::trunc); file.write(reinterpret_cast<const char*>(legacy),sizeof(legacy)); file.close();
        require_batch(file.good(),"F186: isolated legacy-version metadata fixture");
    }
    require_batch(load_shader_regex_cache(shader_hash,L"ps",&unchanged,&tagline)==ShaderRegexCache::NO_CACHE && unchanged==original && tagline==L"unchanged",
        "F186: pre-PCRE2-10.49 cache is rejected even when its hash field matches the current configuration");
    const auto patched_b=create_patch("0.750000",191);
    require_batch(patched_a!=patched_b,"F186: changed replacement produces distinct real bytecode");
    const char no_match_source[]="float4 main():SV_Target{return float4(0.125,0.25,0.5,1);}";
    code.Reset();
    require_batch(SUCCEEDED(D3DCompile(no_match_source,sizeof(no_match_source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),"F186: genuine nonmatching original shader");
    data=static_cast<const unsigned char*>(code->GetBufferPointer());
    std::vector<unsigned char> no_match(data,data+code->GetBufferSize()),assembly;
    const UINT64 no_match_hash=fnv_64_buf(no_match.data(),no_match.size());
    require_batch(SUCCEEDED(disassembler(&no_match,&assembly,nullptr)),"F186: real nonmatching disassembly");
    std::string text(assembly.begin(),assembly.end()),model="ps_5_0"; tagline.clear();
    require_batch(!apply_shader_regex_groups(&text,L"ps",&model,no_match_hash,&tagline),"F186: nonmatching shader is not patched");
    require_batch(load_shader_regex_cache(no_match_hash,L"ps",&unchanged,&tagline)==ShaderRegexCache::NO_MATCH,"F186: new-version negative cache remains usable");
    wchar_t no_match_stem[64]{}; swprintf_s(no_match_stem,L"%016llx-ps_regex.dat",no_match_hash);
    {std::ofstream file(cache/no_match_stem,std::ios::binary|std::ios::trunc); const uint32_t truncated=3; file.write(reinterpret_cast<const char*>(&truncated),sizeof(truncated)); file.close(); require_batch(file.good(),"F186: owned truncated metadata fixture");}
    require_batch(load_shader_regex_cache(no_match_hash,L"ps",&unchanged,&tagline)==ShaderRegexCache::NO_CACHE,"F186: incomplete metadata remains a cache miss");
    config.load("",0,true);
}

std::filesystem::path f200_repository_root()
{
    wchar_t executable[MAX_PATH]{};
    require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "F200: locate regression host");
    std::filesystem::path candidate, build;
    for (const auto& component : std::filesystem::path(executable).parent_path()) {
        candidate /= component;
        if (!_wcsicmp(component.c_str(), L"build"))
            build = candidate;
    }
    require_batch(!build.empty(), "F200: regression host remains under build");
    return build.parent_path();
}

class F200SentinelScope {
public:
    std::filesystem::path path;
    F200SentinelScope()
    {
        const auto parent = f200_repository_root() / L"_tmp";
        std::filesystem::create_directories(parent);
        path = parent / L"sr-sentinel";
        if (std::filesystem::exists(path)) {
            require_batch(path.filename() == L"sr-sentinel" && path.parent_path().filename() == L"_tmp",
                "F200: only clear the dedicated sentinel directory");
            std::filesystem::remove_all(path);
        }
        require_batch(std::filesystem::create_directory(path), "F200: reserve isolated _tmp/sr-sentinel");
    }
    ~F200SentinelScope()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

class F200WorkingDirectoryScope {
    wchar_t old_path[MAX_PATH]{};
public:
    explicit F200WorkingDirectoryScope(const std::filesystem::path& path)
    {
        const DWORD length = GetCurrentDirectoryW(MAX_PATH, old_path);
        require_batch(length > 0 && length < MAX_PATH, "F200: capture complete working directory");
        require_batch(SetCurrentDirectoryW(path.c_str()) != 0, "F200: enter isolated working directory");
    }
    ~F200WorkingDirectoryScope() { SetCurrentDirectoryW(old_path); }
};

class F200FlagScope {
    bool old_cache = G->CACHE_SHADERS, old_export = G->EXPORT_FIXED;
public:
    ~F200FlagScope() { G->CACHE_SHADERS = old_cache; G->EXPORT_FIXED = old_export; }
};

class F200LogScope {
    FILE *old_file = LogFile;
    LogVerbosity old_level = gLogVerbosity;
public:
    F200LogScope() { LogFile = stderr; gLogVerbosity = LogVerbosity::INFO; }
    ~F200LogScope() { LogFile = old_file; gLogVerbosity = old_level; }
};

std::map<std::wstring,std::string> f200_directory_files(const std::filesystem::path& path)
{
    std::map<std::wstring,std::string> files;
    for (const auto& entry : std::filesystem::directory_iterator(path))
        if (entry.is_regular_file())
            files.emplace(entry.path().filename().wstring(), read_file(entry.path()));
    return files;
}

std::map<std::wstring,std::string> f200_write_sentinels(const std::filesystem::path& path,const std::string& at_payload)
{
    const std::pair<const wchar_t*,std::string> values[] = {
        {L"in", "F200 in sentinel"}, {L"at", at_payload}, {L"xt", "F200 xt sentinel"}};
    std::map<std::wstring,std::string> expected;
    for (const auto& value : values) {
        std::ofstream file(path / std::filesystem::path(value.first), std::ios::binary | std::ios::trunc);
        file.write(value.second.data(), static_cast<std::streamsize>(value.second.size()));
        file.close();
        require_batch(file.good(), "F200: write isolated in/at/xt sentinel");
        expected.emplace(value.first, value.second);
    }
    return expected;
}

std::wstring f200_cache_path(const std::filesystem::path& base, size_t length)
{
    std::wstring path = base.wstring();
    require_batch(path.size() + 1 < length && length - path.size() - 1 <= 255,
        "F200: fixture can hold the exact boundary cache path");
    path.push_back(L'\\');
    path.append(length - path.size(), L'c');
    require_batch(path.size() == length, "F200: boundary cache path has the exact requested length");
    return path;
}

void test_f200()
{
    AuditConfigScope config;
    config.load("[ShaderRegexAuditTail]\nshader_model=ps_5_0\nfilter_index=7.25\n"
        "[ShaderRegexAuditTail.Pattern]\nmov\\s+o0\\.xyzw,\\s*l\\(0\\.250000,\\s*0\\.500000,\\s*0\\.750000,\\s*1\\.000000\\)\n"
        "[ShaderRegexAuditTail.Pattern.Replace]\nmov o0.xyzw, l(0.250000, 0.500000, 0.750000, 0.500000)\n",0,true);
    require_batch(shader_regex_groups.size()==1,"F200: production parser loads the real regex group");
    F200FlagScope flags;
    F200LogScope log;

    const char source[]="float4 main():SV_Target{return float4(0.25,0.5,0.75,1);}";
    ComPtr<ID3DBlob> code,errors;
    require_batch(SUCCEEDED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),
        "F200: real original shader");
    const auto* data=static_cast<const unsigned char*>(code->GetBufferPointer());
    const std::vector<unsigned char> original(data,data+code->GetBufferSize());
    const UINT64 shader_hash=fnv_64_buf(original.data(),original.size());
    std::vector<unsigned char> assembly;
    {
        auto binary=original;
        require_batch(SUCCEEDED(disassembler(&binary,&assembly,nullptr)),"F200: production disassembly input");
    }
    const auto patch_once=[&] {
        std::string text(assembly.begin(),assembly.end()),model="ps_5_0"; std::wstring tagline;
        require_batch(apply_shader_regex_groups(&text,L"ps",&model,shader_hash,&tagline),
            "F200: production ShaderRegex path reaches the cache writer");
    };

    wchar_t stem[64]{}; swprintf_s(stem,L"%016llx-ps_regex",shader_hash);
    const std::wstring file_stem(stem);
    F200SentinelScope sentinels;

    const auto green=sentinels.path/L"green-229";
    require_batch(std::filesystem::create_directory(green),"F200: reserve green fixture directory");
    const std::wstring green_cache=f200_cache_path(green,229);
    const std::filesystem::path green_cache_dir(green_cache);
    require_batch(std::filesystem::create_directories(green_cache_dir),"F200: create the exact 229-character cache directory");
    {
        F200WorkingDirectoryScope cwd(green);
        wcscpy_s(G->SHADER_CACHE_PATH, green_cache.c_str());
        G->CACHE_SHADERS=true; G->EXPORT_FIXED=false;
        std::vector<unsigned char> bytecode=original; std::wstring tagline=L"unchanged";
        require_batch(load_shader_regex_cache(shader_hash,L"ps",&bytecode,&tagline)==ShaderRegexCache::NO_CACHE &&
            bytecode==original && tagline==L"unchanged","F200: 229-character cache path is a miss before writing");
        patch_once();
        std::vector<unsigned char> bin=original;
        save_shader_regex_cache_bin(shader_hash,L"ps",&bin);
        std::vector<unsigned char> cached; std::wstring cached_tagline;
        require_batch(load_shader_regex_cache(shader_hash,L"ps",&cached,&cached_tagline)==ShaderRegexCache::PATCH &&
            cached==original,"F200: 229-character cache path writes and reads the exact bytecode");
        require_batch(std::filesystem::exists(green_cache_dir/(file_stem+L".dat")) &&
            std::filesystem::exists(green_cache_dir/(file_stem+L".bin")),
            "F200: 229-character cache path persists metadata and bytecode");
        require_batch(f200_directory_files(green).empty(),"F200: green path writes only inside the configured cache directory");
    }
    {
        F200WorkingDirectoryScope cwd(green);
        wcscpy_s(G->SHADER_CACHE_PATH, green_cache.c_str());
        G->CACHE_SHADERS=true; G->EXPORT_FIXED=true;
        patch_once();
        require_batch(std::filesystem::exists(green_cache_dir/(file_stem+L".txt")),
            "F200: 229-character cache path persists fixed assembly");
        require_batch(read_file(green_cache_dir/(file_stem+L".txt")).find("[ShaderRegexAuditTail]")!=std::string::npos,
            "F200: fixed assembly contains the matched section tagline");
    }

    const auto run_rejected=[&](unsigned length,bool with_sentinels,const char *rejection) {
        const auto dir=sentinels.path/(std::wstring(L"red-")+std::to_wstring(length)+(with_sentinels?L"-sentinels":L"-empty"));
        require_batch(std::filesystem::create_directory(dir),"F200: reserve red fixture directory");
        std::map<std::wstring,std::string> expected;
        if (with_sentinels) {
            // A valid version-2/zero-match header in CWD "at" would be returned
            // as NO_MATCH by the old path-1 corruption. NO_CACHE proves the
            // rejected load never opened it.
            const uint32_t valid_at[]={2,shader_regex_hash,0,0};
            expected=f200_write_sentinels(dir,std::string(reinterpret_cast<const char*>(valid_at),sizeof(valid_at)));
        }
        {
            F200WorkingDirectoryScope cwd(dir);
            const std::wstring cache(length,L'c');
            wcscpy_s(G->SHADER_CACHE_PATH, cache.c_str());
            G->CACHE_SHADERS=true; G->EXPORT_FIXED=false;
            std::vector<unsigned char> bytecode=original; std::wstring tagline=L"unchanged";
            require_batch(load_shader_regex_cache(shader_hash,L"ps",&bytecode,&tagline)==ShaderRegexCache::NO_CACHE &&
                bytecode==original && tagline==L"unchanged",
                (std::string("F200: ")+rejection+" rejects load as NO_CACHE without touching the working directory").c_str());
            patch_once();
            std::vector<unsigned char> bin=original;
            save_shader_regex_cache_bin(shader_hash,L"ps",&bin);
            G->EXPORT_FIXED=true;
            patch_once();
        }
        require_batch(f200_directory_files(dir)==expected,with_sentinels ?
            (std::string("F200: ")+rejection+" leaves in/at/xt sentinels byte-for-byte unchanged").c_str() :
            (std::string("F200: ")+rejection+" creates no in/at/xt names").c_str());
    };
    run_rejected(230u,true,"no-suffix-space path (n=257, max_prefix=256)");
    run_rejected(230u,false,"no-suffix-space path (n=257, max_prefix=256)");
    run_rejected(232u,true,"no-suffix-space path (n=259, max_prefix=256)");
    run_rejected(232u,false,"no-suffix-space path (n=259, max_prefix=256)");
    run_rejected(233u,true,"truncated path (n=-1)");
    run_rejected(233u,false,"truncated path (n=-1)");
}

void test_f200b()
{
    // F-200b: one rule for every path derived from storage_directory — the
    // complete name fits, or the operation is rejected. The formatter is the
    // same one d3dcWrapper.cpp and DX12Diag.h call.
    const uint32_t bhi = 0x11223344u, blo = 0x55667788u;
    const uint32_t shi = 0x99aabbccu, slo = 0xddeeff00u;
    wchar_t path[MAX_PATH];
    wchar_t expected[MAX_PATH];

    require_batch(FormatPathBounded(path, std::size(path), L"%ls\\%08lx%08lx-%ls_%08lx%08lx.txt",
        L"C:\\shaders", bhi, blo, L"ps_5_0", shi, slo), "F200b: legal export path fits");
    swprintf_s(expected, L"%ls\\%08lx%08lx-%ls_%08lx%08lx.txt",
        L"C:\\shaders", bhi, blo, L"ps_5_0", shi, slo);
    require_batch(!wcscmp(path, expected), "F200b: legal export path is byte-for-byte unchanged");

    // Export name overhead is 45 characters after the composed storage path:
    // "\" + 16 hash + "-" + "ps_5_0" + "_" + 16 hash + ".txt". 214 + 45 = 259
    // fits MAX_PATH; 215 + 45 = 260 needs 261 with the terminating NUL.
    const std::wstring exactShaderPath(214, L'a');
    require_batch(FormatPathBounded(path, std::size(path), L"%ls\\%08lx%08lx-%ls_%08lx%08lx.txt",
        exactShaderPath.c_str(), bhi, blo, L"ps_5_0", shi, slo),
        "F200b: 214-character composed storage path fits the export name exactly");
    const std::wstring tooLongShaderPath(215, L'a');
    require_batch(!FormatPathBounded(path, std::size(path), L"%ls\\%08lx%08lx-%ls_%08lx%08lx.txt",
        tooLongShaderPath.c_str(), bhi, blo, L"ps_5_0", shi, slo),
        "F200b: 215-character composed storage path is rejected instead of truncated");

    require_batch(FormatPathBounded(path, std::size(path), L"%ls\\%08lx%08lx-%ls_%08lx%08lx*replace.txt",
        L"C:\\shaders", bhi, blo, L"ps_5_0", shi, slo), "F200b: legal replacement search path fits");
    swprintf_s(expected, L"%ls\\%08lx%08lx-%ls_%08lx%08lx*replace.txt",
        L"C:\\shaders", bhi, blo, L"ps_5_0", shi, slo);
    require_batch(!wcscmp(path, expected), "F200b: legal replacement search path is byte-for-byte unchanged");

    require_batch(FormatPathBounded(path, std::size(path), L"%ls\\%ls", L"C:\\shaders", L"shader_replace.txt"),
        "F200b: legal replacement file path fits");
    swprintf_s(expected, L"%ls\\%ls", L"C:\\shaders", L"shader_replace.txt");
    require_batch(!wcscmp(path, expected), "F200b: legal replacement file path is byte-for-byte unchanged");

    // 300 characters cannot fit MAX_PATH. The preflight refuses before the
    // destination is touched, so neither the buffer nor its guards change.
    std::array<wchar_t, MAX_PATH + 16> guarded;
    guarded.fill(L'Z');
    wchar_t *const out = guarded.data() + 8;
    const std::wstring longShaderPath(300, L'a');
    require_batch(!FormatPathBounded(out, MAX_PATH, L"%ls\\%08lx%08lx-%ls_%08lx%08lx.txt",
        longShaderPath.c_str(), bhi, blo, L"ps_5_0", shi, slo),
        "F200b: 300-character composed storage path is rejected, not truncated");
    for (size_t i = 0; i < 8; ++i)
        require_batch(guarded[i] == L'Z', "F200b: rejected export path leaves the leading guard intact");
    for (size_t i = 8 + MAX_PATH; i < guarded.size(); ++i)
        require_batch(guarded[i] == L'Z', "F200b: rejected export path leaves the trailing guard intact");
    require_batch(!FormatPathBounded(out, MAX_PATH, L"%ls\\%08lx%08lx-%ls_%08lx%08lx*replace.txt",
        longShaderPath.c_str(), bhi, blo, L"ps_5_0", shi, slo),
        "F200b: long replacement search path is rejected, not truncated");
    require_batch(!FormatPathBounded(out, MAX_PATH, L"%ls\\%ls", longShaderPath.c_str(), longShaderPath.c_str()),
        "F200b: long replacement file path is rejected, not truncated");

    // F-200 #6 uses the same formatter for the TIMING-only history log name.
    wchar_t history[MAX_PATH];
    require_batch(!FormatPathBounded(history, std::size(history), L"%s\\d3d12_log_%llu.txt",
        longShaderPath.c_str(), 123456789ull), "F200b: long DX12 TIMING log directory is rejected, not truncated");
    require_batch(FormatPathBounded(history, std::size(history), L"%s\\d3d12_log_%llu.txt",
        L"C:\\game", 123456789ull), "F200b: legal DX12 TIMING log path fits");
    require_batch(!wcscmp(history, L"C:\\game\\d3d12_log_123456789.txt"),
        "F200b: legal DX12 TIMING log name is unchanged");
}

void test_f200c()
{
    // F-200c: the concatenation helpers behind #1/#2/#3/#9/#10/#11 and the
    // checked PathAppendW behind #5/#7/#8. Same contract as FormatPathBounded:
    // a complete result is byte-for-byte what wcscpy/wcscat/PathAppendW
    // produced; an overlong one is rejected before the CRT _s handler can fire,
    // and leaves the destination untouched.
    wchar_t concat[MAX_PATH];
    require_batch(CopyPathBounded(concat, std::size(concat), L"C:\\game\\"),
        "F200c: legal copy fits");
    require_batch(!wcscmp(concat, L"C:\\game\\"), "F200c: legal copy is byte-for-byte unchanged");
    require_batch(AppendPathBounded(concat, std::size(concat), L"d3dx.ini"),
        "F200c: legal append fits");
    {
        wchar_t legacy[MAX_PATH];
        wcscpy_s(legacy, L"C:\\game\\");
        wcscat_s(legacy, L"d3dx.ini");
        require_batch(!wcscmp(concat, legacy), "F200c: legal append is byte-for-byte unchanged");
    }

    // The exact limit is 259 characters + NUL; one more character is rejected
    // and the existing 259-character path is left intact.
    {
        wchar_t exact[MAX_PATH];
        wmemset(exact, L'p', MAX_PATH - 1);
        exact[MAX_PATH - 1] = 0;
        require_batch(CopyPathBounded(concat, std::size(concat), exact),
            "F200c: 259-character copy is the exact limit");
        require_batch(!AppendPathBounded(concat, std::size(concat), L"x"),
            "F200c: append past MAX_PATH is rejected, not truncated");
        require_batch(wcslen(concat) == MAX_PATH - 1 && concat[MAX_PATH - 2] == L'p',
            "F200c: rejected append leaves the 259-character path byte-for-byte intact");
    }

    // _s failure injection: the old wcscpy/wcscat template overloads would
    // reach the invalid parameter handler here. The helpers must return false
    // and touch nothing, including the guard bytes around the destination.
    {
        std::array<wchar_t, MAX_PATH + 16> guarded;
        guarded.fill(L'Z');
        wchar_t *const out = guarded.data() + 8;
        const std::wstring tooLong(300, L'a');
        require_batch(!CopyPathBounded(out, MAX_PATH, tooLong.c_str()),
            "F200c: 300-character _s copy failure is rejected, not truncated");
        bool untouched = true;
        for (size_t i = 0; i < MAX_PATH; ++i)
            untouched = untouched && out[i] == L'Z';
        require_batch(untouched, "F200c: rejected _s copy leaves the destination untouched");
        for (size_t i = 8 + MAX_PATH; i < guarded.size(); ++i)
            require_batch(guarded[i] == L'Z', "F200c: rejected _s copy leaves the trailing guard intact");
    }

    // #5/#7/#8/configPath use PathAppendW; a failed append must not leave a
    // half-built path behind for the next file API.
    wchar_t joined[MAX_PATH];
    require_batch(CopyPathBounded(joined, std::size(joined), L"C:\\game"),
        "F200c: legal join base fits");
    require_batch(PathAppendBounded(joined, std::size(joined), L"d3dx.ini"),
        "F200c: legal PathAppendW join fits");
    {
        wchar_t legacy[MAX_PATH];
        wcscpy_s(legacy, L"C:\\game");
        PathAppendW(legacy, L"d3dx.ini");
        require_batch(!wcscmp(joined, legacy), "F200c: legal join is byte-for-byte unchanged");
    }
    {
        const std::wstring longBase(250, L'b');
        wchar_t longJoined[MAX_PATH];
        require_batch(CopyPathBounded(longJoined, std::size(longJoined), longBase.c_str()),
            "F200c: 250-character base copy fits");
        require_batch(PathAppendBounded(longJoined, std::size(longJoined), L"x.ini"),
            "F200c: legal long join fits");
        wchar_t legacyLong[MAX_PATH];
        wcscpy_s(legacyLong, longBase.c_str());
        PathAppendW(legacyLong, L"x.ini");
        require_batch(!wcscmp(longJoined, legacyLong),
            "F200c: legal long join is byte-for-byte unchanged");
    }
    require_batch(!PathAppendBounded(joined, std::size(joined), std::wstring(300, L'a').c_str()),
        "F200c: PathAppendW failure is rejected, not truncated");
    require_batch(!wcscmp(joined, L"C:\\game\\d3dx.ini"),
        "F200c: rejected join leaves the previous path byte-for-byte intact");
}

ComPtr<ID3D11Texture2D> f168_rt_texture(BatchDevice& device, UINT width = 4, UINT height = 3)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> result;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &result)),
        "F-168: create real WARP render-target texture");
    return result;
}

// Read the real GPU result back through a staging copy. The view is unbound
// first so this helper can run while the fixture is still an active RT.
void f168_require_uniform_colour(BatchDevice& device, ID3D11Texture2D* texture, UINT expected, const char* message)
{
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    device.context->OMSetRenderTargets(0, nullptr, nullptr);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &staging)),
        "F-168: create real staging readback");
    device.context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "F-168: map real staging readback");
    bool uniform = true;
    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* row = reinterpret_cast<const UINT*>(static_cast<const unsigned char*>(mapped.pData) + mapped.RowPitch * y);
        for (UINT x = 0; x < desc.Width; ++x)
            uniform &= row[x] == expected;
    }
    device.context->Unmap(staging.Get(), 0);
    require_batch(uniform, message);
}

// R1: a destroyed resource's address stays in frame_analysis_seen_rts for the
// rest of the session. This is the deterministic B1 evidence and fences the
// still-open P1 address-reuse decision; flip the final assertion only when a
// P1 release-erase policy actually lands.
void test_dx11_f168_r1_stale_key()
{
    AuditDumpScope output;
    BatchDevice device;
    AuditAnalysisDeviceScope analysis(device);
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 4; desc.Height = 3;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture;
    require_batch(SUCCEEDED(device.device->CreateTexture2D(&desc, nullptr, &texture)),
        "F-168 R1: create a tracked WARP resource through the wrapper device");
    ComPtr<ID3D11RenderTargetView> view;
    require_batch(SUCCEEDED(device.native->CreateRenderTargetView(texture.Get(), nullptr, &view)),
        "F-168 R1: create the real render-target view");

    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    void* key = resource.Get();
    resource.Reset();

    G->analyse_frame = true;
    G->cur_analyse_options = FrameAnalysisOptions::CLEAR_RT;
    {
        CriticalSectionGuard(&G->mCriticalSection);
        G->frame_analysis_seen_rts.clear();
    }

    ID3D11RenderTargetView* raw = view.Get();
    analysis.context->OMSetRenderTargets(1, &raw, nullptr);
    {
        CriticalSectionGuard(&G->mCriticalSection);
        require_batch(G->frame_analysis_seen_rts.count(key) == 1,
            "F-168 R1: the production bind path records the resource address");
    }

    // The immediate context keeps a bound view alive; unbind before dropping
    // our references so the resource really reaches its final Release.
    device.context->OMSetRenderTargets(0, nullptr, nullptr);
    view.Reset();
    texture.Reset();
    {
        CriticalSectionGuard(&G->mCriticalSection);
        require_batch(G->frame_analysis_seen_rts.count(key) == 1,
            "F-168 R1: a destroyed resource address remains a seen key (P1 open, no erase path)");
        G->frame_analysis_seen_rts.clear();
    }
}

// R2: CLEAR_RT currently means once per session, not once per frame. The
// first bind of a hold session clears; the second frame's first bind does
// not. Deterministic single-thread evidence for the B3 semantics question.
void test_dx11_f168_r2_hold_second_frame()
{
    AuditDumpScope output;
    BatchDevice device;
    AuditAnalysisDeviceScope analysis(device);
    auto texture = f168_rt_texture(device);
    ComPtr<ID3D11RenderTargetView> view;
    require_batch(SUCCEEDED(device.native->CreateRenderTargetView(texture.Get(), nullptr, &view)),
        "F-168 R2: create the real render-target view");

    G->analyse_frame = true;
    G->cur_analyse_options = FrameAnalysisOptions::CLEAR_RT;
    {
        CriticalSectionGuard(&G->mCriticalSection);
        G->frame_analysis_seen_rts.clear();
    }

    const FLOAT nonzero[4] = {1, 1, 1, 1};
    ID3D11RenderTargetView* raw = view.Get();
    device.context->ClearRenderTargetView(raw, nonzero);
    f168_require_uniform_colour(device, texture.Get(), 0xffffffffu,
        "F-168 R2: fixture starts at a non-zero colour");

    analysis.context->OMSetRenderTargets(1, &raw, nullptr);
    f168_require_uniform_colour(device, texture.Get(), 0u,
        "F-168 R2: the first bind of the session clears the target");

    device.context->ClearRenderTargetView(raw, nonzero);
    f168_require_uniform_colour(device, texture.Get(), 0xffffffffu,
        "F-168 R2: the second frame fixture is non-zero again");

    analysis.context->OMSetRenderTargets(1, &raw, nullptr);
    f168_require_uniform_colour(device, texture.Get(), 0xffffffffu,
        "F-168 R2: hold mode does not clear the same resource on its second frame");

    {
        CriticalSectionGuard(&G->mCriticalSection);
        G->frame_analysis_seen_rts.clear();
    }
}

struct F168StressWorker {
    ComPtr<ID3D11DeviceContext1> native_context;
    std::unique_ptr<FrameAnalysisContext> analysis;
    std::vector<ComPtr<ID3D11Texture2D>> textures;
    std::vector<ComPtr<ID3D11RenderTargetView>> views;
};

void f168_prepare_stress_worker(BatchDevice& device, F168StressWorker& worker, UINT count)
{
    ComPtr<ID3D11DeviceContext> deferred;
    require_batch(SUCCEEDED(device.native->CreateDeferredContext(0, &deferred)),
        "F-168 S1: create a real WARP deferred context");
    require_batch(SUCCEEDED(deferred.As(&worker.native_context)),
        "F-168 S1: query ID3D11DeviceContext1 on the deferred context");
    worker.analysis = std::make_unique<FrameAnalysisContext>(device.native.Get(), worker.native_context.Get());
    worker.analysis->SetHackerDevice(device.device);
    for (UINT i = 0; i < count; ++i) {
        auto texture = f168_rt_texture(device);
        ComPtr<ID3D11RenderTargetView> view;
        require_batch(SUCCEEDED(device.native->CreateRenderTargetView(texture.Get(), nullptr, &view)),
            "F-168 S1: create a real per-thread render-target view");
        worker.textures.push_back(std::move(texture));
        worker.views.push_back(std::move(view));
    }
}

void f168_stress_bind(F168StressWorker& worker, std::atomic<int>& ready, std::atomic<bool>& go)
{
    ready.fetch_add(1, std::memory_order_release);
    while (!go.load(std::memory_order_acquire))
        std::this_thread::yield();
    for (auto& view : worker.views) {
        ID3D11RenderTargetView* raw = view.Get();
        worker.analysis->OMSetRenderTargets(1, &raw, nullptr);
    }
}

// S1: two real deferred FrameAnalysisContexts bind distinct RTs concurrently
// through the production OMSetRenderTargets path. This is a probabilistic
// stress for the insert/insert race (pre-P0: lost keys or a crash; post-P0:
// exact set size every round). The synthetic 4096-key prefill only widens the
// rehash window; those sentinel values are never dereferenced.
void test_dx11_f168_s1_seen_rts_stress()
{
    constexpr UINT kRounds = 8;
    constexpr UINT kPerWorker = 64;
    constexpr UINT kPrefill = 4096;

    AuditDumpScope output;
    BatchDevice device;
    const int previous_hunting = G->hunting;
    G->hunting = HUNTING_MODE_ENABLED;
    try {
        F168StressWorker workers[2];
        for (auto& worker : workers)
            f168_prepare_stress_worker(device, worker, kPerWorker);

        G->analyse_frame = true;
        G->cur_analyse_options = FrameAnalysisOptions::CLEAR_RT;
        for (UINT round = 0; round < kRounds; ++round) {
            {
                CriticalSectionGuard(&G->mCriticalSection);
                G->frame_analysis_seen_rts.clear();
                for (UINT i = 0; i < kPrefill; ++i)
                    G->frame_analysis_seen_rts.insert(reinterpret_cast<void*>(
                        static_cast<std::uintptr_t>(i) + 1));
            }

            std::atomic<int> ready{0};
            std::atomic<bool> go{false};
            std::thread first([&] { f168_stress_bind(workers[0], ready, go); });
            std::thread second([&] { f168_stress_bind(workers[1], ready, go); });
            while (ready.load(std::memory_order_acquire) != 2)
                std::this_thread::yield();
            go.store(true, std::memory_order_release);
            first.join();
            second.join();

            size_t observed = 0;
            {
                CriticalSectionGuard(&G->mCriticalSection);
                observed = G->frame_analysis_seen_rts.size();
            }
            require_batch(observed == kPrefill + 2 * kPerWorker,
                "F-168 S1: concurrent production binds lose no key (set size is exact)");
        }

        {
            CriticalSectionGuard(&G->mCriticalSection);
            G->frame_analysis_seen_rts.clear();
        }
        G->hunting = previous_hunting;
    } catch (...) {
        G->hunting = previous_hunting;
        throw;
    }
}

void test_dx11_f168_seen_rts_regression()
{
    test_dx11_f168_r1_stale_key();
    test_dx11_f168_r2_hold_second_frame();
}

void test_f199_review()
{
    AuditConfigScope config;
    config.load("[ShaderRegexAuditSingle]\nshader_model=ps_5_0\n[ShaderRegexAuditSingle.Pattern]\nbefore\n"
        "[ShaderRegexAuditSingle.Pattern.Replace]\nafter\n",0,true);
    require_batch(shader_regex_groups.size()==1 && shader_regex_groups.begin()->second.patterns.size()==1,
        "F199 review: supported INI syntax creates exactly one real pattern");
    config.load("[ShaderRegexAuditCase]\nshader_model=ps_5_0\n[ShaderRegexAuditCase.Pattern]\nbefore\n"
        "[ShaderRegexAuditCase.pattern]\nafter\n[ShaderRegexAuditCase.Pattern.Replace]\nresult\n",0,true);
    require_batch(shader_regex_groups.size()==1 && shader_regex_groups.begin()->second.patterns.size()==1,
        "F199 review: case variants are one case-insensitive INI section, not multiple patterns");
    config.load("[ShaderRegexAuditNamed]\nshader_model=ps_5_0\n[ShaderRegexAuditNamed.PatternA]\nbefore\n"
        "[ShaderRegexAuditNamed.PatternA.Replace]\nafter\n[ShaderRegexAuditNamed.PatternB]\nsecond\n"
        "[ShaderRegexAuditNamed.PatternB.Replace]\nthird\n",0,true);
    require_batch(shader_regex_groups.empty(),"F199 review: named multiple patterns are rejected by the actual parser");
    config.load("[ShaderRegexAuditA]\nshader_model=ps_5_0\n[ShaderRegexAuditA.Pattern]\nbefore\n"
        "[ShaderRegexAuditB]\nshader_model=ps_5_0\n[ShaderRegexAuditB.Pattern]\nafter\n",0,true);
    require_batch(shader_regex_groups.size()==2,"F199 review: separate INI groups stay separate");
    for(const auto& group:shader_regex_groups)
        require_batch(group.second.patterns.size()==1,"F199 review: no production group gained a second pattern");
    ShaderRegexGroup synthetic;
    std::string first="before",missing="does_not_exist";
    require_batch(synthetic.patterns[L"first"].compile(&first) && synthetic.patterns[L"second"].compile(&missing),
        "F199 review: explicit synthetic multi-pattern local probe");
    synthetic.patterns[L"first"].do_replace=true; synthetic.patterns[L"first"].replace="after";
    std::string text="before"; bool matched=false,patched=false;
    synthetic.apply_regex_patterns(&text,&matched,&patched);
    std::fprintf(stderr,"F199 synthetic-only observation: match=%d patch=%d changed=%d; not a production reachability proof\n",matched,patched,text!="before");
    config.load("",0,true);
}

namespace {
class AutoTextureOrderCommand : public CommandListCommand {
public:
    AutoTextureOrderCommand(std::vector<int>& order, int value) : order(order), value(value) {}
    void run(CommandListState*) override { order.push_back(value); }
private:
    std::vector<int>& order;
    int value;
};

class AutoTextureRecordThisCommand : public CommandListCommand {
public:
    explicit AutoTextureRecordThisCommand(std::vector<unsigned>& slots) : slots(slots) {}
    void run(CommandListState* state) override
    {
        slots.push_back(state && state->this_target ? state->this_target->slot : UINT_MAX);
    }
private:
    std::vector<unsigned>& slots;
};

class AutoTextureBindCommand : public CommandListCommand {
public:
    AutoTextureBindCommand(UINT slot, ID3D11ShaderResourceView* view) : slot(slot), view(view) {}
    void run(CommandListState* state) override
    {
        if (state && state->mOrigContext1)
            state->mOrigContext1->PSSetShaderResources(slot, 1, &view);
    }
private:
    UINT slot;
    ID3D11ShaderResourceView* view;
};

class CheckViewportWidthCommand : public CommandListCommand {
    float width;
public:
    explicit CheckViewportWidthCommand(float width) : width(width) {}
    void run(CommandListState* state) override
    {
        D3D11_VIEWPORT viewport{};
        UINT count = 1;
        state->mOrigContext1->RSGetViewports(&count, &viewport);
        dx11_slot_test::require_batch(count == 1 && viewport.Width == width,
            "custom shader: expected viewport width is visible in its calling scope");
    }
};

class CheckRenderTargetCommand final : public CommandListCommand {
    ID3D11RenderTargetView* expected;
public:
    explicit CheckRenderTargetCommand(ID3D11RenderTargetView* expected) : expected(expected) {}
    void run(CommandListState* state) override
    {
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> bound;
        state->mOrigContext1->OMGetRenderTargets(1, bound.GetAddressOf(), nullptr);
        dx11_slot_test::require_batch(bound.Get() == expected,
            "custom shader: render target matches the active calling scope");
    }
};

class BindComputeOutputCommand final : public CommandListCommand {
    ID3D11Resource* resource;
    ID3D11UnorderedAccessView* view;
public:
    BindComputeOutputCommand(ID3D11Resource* resource, ID3D11UnorderedAccessView* view) : resource(resource), view(view) {}
    void run(CommandListState* state) override
    {
        ResourceCopyTarget target;
        target.type = ResourceCopyTargetType::UNORDERED_ACCESS_VIEW;
        target.shader_type = L'c';
        target.SetResource(state, resource, view, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    }
};

static dx11_slot_test::ComPtr<ID3D11Buffer> CreateComputeTestBuffer(dx11_slot_test::BatchDevice& device,
    UINT flags, const std::array<UINT, 4>& data, bool structured = false)
{
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(data);
    desc.BindFlags = flags;
    if (structured) {
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(UINT);
    }
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = data.data();
    dx11_slot_test::ComPtr<ID3D11Buffer> buffer;
    dx11_slot_test::require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, &initial, &buffer)),
        "compute ref: create native buffer");
    return buffer;
}

static std::array<UINT, 4> ReadComputeTestBuffer(dx11_slot_test::BatchDevice& device, ID3D11Buffer* source)
{
    std::array<UINT, 4> result{};
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(result);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    dx11_slot_test::ComPtr<ID3D11Buffer> staging;
    dx11_slot_test::require_batch(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &staging)),
        "compute ref: allocate native readback buffer");
    device.context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    dx11_slot_test::require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "compute ref: read GPU dispatch results");
    std::memcpy(result.data(), mapped.pData, sizeof(result));
    device.context->Unmap(staging.Get(), 0);
    return result;
}

static void test_compute_references()
{
    using namespace dx11_slot_test;
    BatchDevice device;
    auto input_buffer = CreateComputeTestBuffer(device, D3D11_BIND_SHADER_RESOURCE, {5, 8, 11, 13});
    auto first_output = CreateComputeTestBuffer(device, D3D11_BIND_UNORDERED_ACCESS, {});
    auto second_output = CreateComputeTestBuffer(device, D3D11_BIND_UNORDERED_ACCESS, {});
    D3D11_TEXTURE2D_DESC color_desc{};
    color_desc.Width = color_desc.Height = 4;
    color_desc.MipLevels = color_desc.ArraySize = color_desc.SampleDesc.Count = 1;
    color_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    color_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    D3D11_TEXTURE2D_DESC depth_desc = color_desc;
    depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> color, depth;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&color_desc, nullptr, &color)) &&
        SUCCEEDED(device.native->CreateTexture2D(&depth_desc, nullptr, &depth)) &&
        SUCCEEDED(device.native->CreateRenderTargetView(color.Get(), nullptr, &rtv)) &&
        SUCCEEDED(device.native->CreateDepthStencilView(depth.Get(), nullptr, &dsv)),
        "compute ref: create the caller's color and depth targets");
    auto* color_target = rtv.Get();
    device.context->OMSetRenderTargets(1, &color_target, dsv.Get());
    const auto check_om_targets = [&]() {
        ComPtr<ID3D11RenderTargetView> color_view;
        ComPtr<ID3D11DepthStencilView> depth_view;
        device.context->OMGetRenderTargets(1, color_view.GetAddressOf(), depth_view.GetAddressOf());
        require_batch(color_view.Get() == rtv.Get() && depth_view.Get() == dsv.Get(),
            "compute ref: caller's RTV and DSV are preserved");
    };
    CustomResource input, output;
    const auto set_resource = [&](CustomResource& target, ID3D11Buffer* buffer, DXGI_FORMAT format) {
        if (target.resource) target.resource->Release();
        target.resource = buffer;
        buffer->AddRef();
        target.device = device.native.Get();
        target.is_null = false;
        target.format = format;
        target.buf_size = 4 * sizeof(UINT);
        target.stride = sizeof(UINT);
    };
    set_resource(input, input_buffer.Get(), DXGI_FORMAT_R32_UINT);
    set_resource(output, first_output.Get(), DXGI_FORMAT_R32_UINT);

    const auto bind = [](ResourceCopyTargetType type, unsigned slot, CustomResource* source) {
        auto operation = std::make_shared<ResourceCopyOperation>();
        operation->dst.type = type;
        operation->dst.shader_type = L'c';
        operation->dst.slot = slot;
        operation->options = ResourceCopyOptions::REFERENCE;
        operation->src.type = source ? ResourceCopyTargetType::CUSTOM_RESOURCE : ResourceCopyTargetType::EMPTY;
        if (source) operation->src.SetCustomResource(source);
        return operation;
    };
    auto input_ref = bind(ResourceCopyTargetType::SHADER_RESOURCE, 0, &input);
    auto output_ref = bind(ResourceCopyTargetType::UNORDERED_ACCESS_VIEW, 0, &output);
    auto unbind_output = bind(ResourceCopyTargetType::UNORDERED_ACCESS_VIEW, 0, nullptr);

    auto dispatch = std::make_shared<DrawCommand>();
    dispatch->type = DrawCommandType::DISPATCH;
    const std::wstring two = L"2", one = L"1", runtime_count = L"draw_number + 1", ini_namespace;
    CommandListScope scope;
    require_batch(dispatch->args[0].parse(&two, &ini_namespace, &scope) &&
        dispatch->args[1].parse(&one, &ini_namespace, &scope) && dispatch->args[2].parse(&one, &ini_namespace, &scope) &&
        dispatch->optimise(device.device), "compute ref: specialise a literal dispatch");

    const char source[] = "Buffer<uint> src : register(t0); RWBuffer<uint> dst : register(u0);"
        "[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { dst[id.x] += src[id.x]; }";
    ComPtr<ID3DBlob> bytecode;
    CustomShader shader;
    require_batch(SUCCEEDED(D3DCompile(source, sizeof(source), nullptr, nullptr, nullptr,
        "main", "cs_5_0", 0, 0, &bytecode, nullptr)) && SUCCEEDED(device.native->CreateComputeShader(
            bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader.cs)),
        "compute ref: create actual compute shader");
    shader.cs_override = true;
    shader.substantiated = true;
    shader.command_list.commands = {input_ref, bind(ResourceCopyTargetType::SHADER_RESOURCE, 1, nullptr),
        output_ref, dispatch, unbind_output};
    merge_shader_resource_batches(&shader.command_list);
    RunCustomShaderCommand run;
    run.custom_shader = &shader;
    ShaderResourceBatchStats stats;
    device.state.slot_batch_stats = &stats;

    run.run(&device.state);
    require_batch(stats.compute_ref_hits == 0 && stats.literal_dispatches == 1 && stats.set_calls == 1,
        "compute ref: cold path builds the views and batches the SRV slots");
    run.run(&device.state);
    require_batch(stats.compute_ref_hits == 2 && stats.literal_dispatches == 2 && stats.om_uav_probes == 2 &&
        stats.om_saves == 0 && stats.om_restores == 0 &&
        ReadComputeTestBuffer(device, first_output.Get()) == std::array<UINT, 4>{10, 16, 0, 0},
        "compute ref: both dispatches run while disjoint outputs need only one OM query each");
    check_om_targets();

    set_resource(output, second_output.Get(), DXGI_FORMAT_R32_UINT);
    require_batch(dispatch->args[0].parse(&runtime_count, &ini_namespace, &scope) && !dispatch->optimise(device.device),
        "compute ref: runtime dispatch expressions stay dynamic");
    run.run(&device.state);
    require_batch(stats.compute_ref_hits == 3 && stats.literal_dispatches == 2 &&
        ReadComputeTestBuffer(device, first_output.Get()) == std::array<UINT, 4>{10, 16, 0, 0} &&
        ReadComputeTestBuffer(device, second_output.Get()) == std::array<UINT, 4>{5, 0, 0, 0},
        "compute ref: replacement UAV bypasses the cache and dynamic dispatch uses its runtime count");

    D3D11_UNORDERED_ACCESS_VIEW_DESC om_view_desc{};
    om_view_desc.Format = DXGI_FORMAT_R32_UINT;
    om_view_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    om_view_desc.Buffer.NumElements = 4;
    ComPtr<ID3D11UnorderedAccessView> disjoint_uav, aliased_uav;
    require_batch(SUCCEEDED(device.native->CreateUnorderedAccessView(first_output.Get(), &om_view_desc, &disjoint_uav)) &&
        SUCCEEDED(device.native->CreateUnorderedAccessView(second_output.Get(), &om_view_desc, &aliased_uav)),
        "compute ref: create disjoint and aliased OM UAVs");
    const auto set_om_uav = [&](UINT slot, ID3D11UnorderedAccessView* view) {
        std::array<ID3D11UnorderedAccessView*, D3D11_1_UAV_SLOT_COUNT> views{};
        views[slot - 1] = view;
        device.context->OMSetRenderTargetsAndUnorderedAccessViews(1, &color_target, dsv.Get(), 1, slot, views.data(), nullptr);
    };
    const auto check_om_uav = [&](UINT slot, ID3D11UnorderedAccessView* expected) {
        ComPtr<ID3D11UnorderedAccessView> actual;
        device.context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, slot, 1, actual.GetAddressOf());
        require_batch(actual.Get() == expected, "compute ref: OM UAV has the expected native binding");
    };

    set_om_uav(1, disjoint_uav.Get());
    run.run(&device.state);
    require_batch(stats.om_uav_probes == 4 && stats.om_saves == 0 && stats.om_restores == 0,
        "compute ref: a disjoint OM UAV avoids save and restore");
    check_om_uav(1, disjoint_uav.Get());
    check_om_targets();

    set_om_uav(1, aliased_uav.Get());
    run.run(&device.state);
    require_batch(stats.om_uav_probes == 5 && stats.om_saves == 1 && stats.om_restores == 1,
        "compute ref: an overlapping OM UAV takes the snapshot and restore path");
    check_om_uav(1, aliased_uav.Get());
    check_om_targets();

    if (device.native->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1) {
        constexpr UINT extended_slot = 12;
        set_om_uav(extended_slot, disjoint_uav.Get());
        check_om_uav(extended_slot, disjoint_uav.Get());
        run.run(&device.state);
        require_batch(stats.om_uav_probes == 6 && stats.om_saves == 2 && stats.om_restores == 2,
            "compute ref: an extended OM UAV slot keeps the legacy restore behavior");
    }
    set_om_uav(1, nullptr);

    const unsigned hits_before_null = stats.compute_ref_hits;
    output.is_null = true;
    output_ref->run(&device.state);
    ComPtr<ID3D11UnorderedAccessView> bound;
    device.context->CSGetUnorderedAccessViews(0, 1, bound.GetAddressOf());
    require_batch(!bound && stats.compute_ref_hits == hits_before_null, "compute ref: a null source unbinds the slot");

    auto dual_bind_buffer = CreateComputeTestBuffer(device, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, {});
    set_resource(output, dual_bind_buffer.Get(), DXGI_FORMAT_R32_UINT);
    output_ref->run(&device.state);
    output_ref->run(&device.state);
    const unsigned hits_before_target_switch = stats.compute_ref_hits;
    unbind_output->run(&device.state);
    output_ref->dst.type = ResourceCopyTargetType::SHADER_RESOURCE;
    output_ref->run(&device.state);
    ComPtr<ID3D11ShaderResourceView> switched_view;
    device.context->CSGetShaderResources(0, 1, switched_view.GetAddressOf());
    require_batch(switched_view && stats.compute_ref_hits == hits_before_target_switch,
        "compute ref: changing the destination kind does not reuse a UAV as an SRV");
    output_ref->dst.type = ResourceCopyTargetType::UNORDERED_ACCESS_VIEW;

    auto counter_buffer = CreateComputeTestBuffer(device, D3D11_BIND_UNORDERED_ACCESS, {}, true);
    auto count_buffer = CreateComputeTestBuffer(device, 0, {});
    set_resource(output, counter_buffer.Get(), DXGI_FORMAT_UNKNOWN);
    D3D11_UNORDERED_ACCESS_VIEW_DESC counter_desc{};
    counter_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    counter_desc.Buffer.NumElements = 4;
    counter_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_COUNTER;
    ComPtr<ID3D11UnorderedAccessView> counter_view;
    require_batch(SUCCEEDED(device.native->CreateUnorderedAccessView(counter_buffer.Get(), &counter_desc, &counter_view)),
        "compute ref: create a counter UAV");
    output.view = counter_view.Get();
    output.view->AddRef();
    auto* raw_counter = counter_view.Get();
    UINT initial_count = 7;
    device.context->CSSetUnorderedAccessViews(0, 1, &raw_counter, &initial_count);
    unbind_output->run(&device.state);
    output_ref->run(&device.state);
    output_ref->run(&device.state);
    device.context->CopyStructureCount(count_buffer.Get(), 0, counter_view.Get());
    require_batch(ReadComputeTestBuffer(device, count_buffer.Get())[0] == 7 &&
        stats.compute_ref_hits == hits_before_target_switch,
        "compute ref: an explicit counter view keeps its identity and hidden count across binds");
    device.state.slot_batch_stats = nullptr;
}

class AutoTextureTestScope {
public:
    std::vector<AutoCheckTextureSlot> saved_slots = G->auto_check_texture_slots;
    std::vector<uint8_t> saved_has_check = G->auto_check_texture_slot_has_check;
    decltype(G->auto_check_graphics_targets) saved_graphics_targets;
    decltype(G->auto_check_compute_targets) saved_compute_targets;
    uint64_t saved_slots_generation = G->auto_check_texture_slots_generation;
    uint64_t saved_coverage_generation = G->auto_check_texture_coverage_generation;
    uint64_t saved_full_pass_count = G->auto_check_texture_full_pass_count;
    TextureOverrideMap saved_map;
    FuzzyTextureOverrides saved_fuzzy;
    decltype(G->mTextureOverrideDrawIndexMap) saved_index_map;
    decltype(G->mTextureOverrideDrawVertexMap) saved_vertex_map;
    const Profiling::Mode saved_mode = Profiling::mode.load();
    AutoTextureTestScope()
    {
        // Move the registries aside: unordered_map nodes and vector storage keep
        // their addresses, so TextureOverride* in the draw maps stay valid.
        saved_map.swap(G->mTextureOverrideMap);
        saved_fuzzy.swap(G->mFuzzyTextureOverrides);
        saved_index_map.swap(G->mTextureOverrideDrawIndexMap);
        saved_vertex_map.swap(G->mTextureOverrideDrawVertexMap);
        G->auto_check_texture_slots.clear();
        G->auto_check_texture_slot_has_check.clear();
        saved_graphics_targets.swap(G->auto_check_graphics_targets);
        saved_compute_targets.swap(G->auto_check_compute_targets);
        Profiling::mode = Profiling::Mode::NONE;
        InvalidateTextureOverrideCandidates();
    }
    ~AutoTextureTestScope()
    {
        G->auto_check_texture_slots = saved_slots;
        G->auto_check_texture_slot_has_check = saved_has_check;
        saved_graphics_targets.swap(G->auto_check_graphics_targets);
        saved_compute_targets.swap(G->auto_check_compute_targets);
        G->auto_check_texture_slots_generation = saved_slots_generation;
        G->auto_check_texture_coverage_generation = saved_coverage_generation;
        G->auto_check_texture_full_pass_count = saved_full_pass_count;
        Profiling::mode = saved_mode;
        InvalidateTextureOverrideCandidates();
        G->mTextureOverrideMap.swap(saved_map);
        G->mFuzzyTextureOverrides.swap(saved_fuzzy);
        G->mTextureOverrideDrawIndexMap.swap(saved_index_map);
        G->mTextureOverrideDrawVertexMap.swap(saved_vertex_map);
    }
};
} // namespace

void test_dx11_auto_texture_check_regression()
{
    using namespace dx11_slot_test;

    test_compute_references();

    // Configuration parsing, inclusive range expansion, aliases and rejection.
    std::vector<AutoCheckTextureSlot> slots;
    require_batch(ParseAutoCheckTextureSlots(L"ps-t[0:2] vb1 ib", &slots),
        "auto-tex: parse ps range, vertex buffer and index buffer");
    require_batch(slots.size() == 5, "auto-tex: ps-t[0:2] expands to three configured slots");
    require_batch(slots[0].type == ResourceCopyTargetType::SHADER_RESOURCE &&
        slots[0].shader_type == L'p' && slots[0].slot == 0, "auto-tex: range starts at ps-t0");
    require_batch(slots[1].slot == 1 && slots[2].slot == 2, "auto-tex: range expands in ascending order");
    require_batch(slots[3].type == ResourceCopyTargetType::VERTEX_BUFFER && slots[3].slot == 1,
        "auto-tex: vb1 is a vertex buffer slot");
    require_batch(slots[4].type == ResourceCopyTargetType::INDEX_BUFFER && slots[4].slot == 0,
        "auto-tex: ib is the index buffer slot");

    require_batch(ParseAutoCheckTextureSlots(L"ps-t[0:9]", &slots) && slots.size() == 10 &&
        slots.front().slot == 0 && slots.back().slot == 9,
        "auto-tex: documented ps-t[0:9] range expands to ten slots");

    require_batch(ParseAutoCheckTextureSlots(L"PS-T0 ps-t0 cs-t7 vs-t3", &slots) && slots.size() == 3,
        "auto-tex: tokens are case-insensitive and duplicates collapse");
    require_batch(slots[0].shader_type == L'p' && slots[1].shader_type == L'c' && slots[2].shader_type == L'v',
        "auto-tex: ps/vs/cs keep their pipeline stage");

    require_batch(ParseAutoCheckTextureSlots(L" \t\r\n ", &slots) && slots.empty(),
        "auto-tex: empty value leaves the feature disabled");
    require_batch(!ParseAutoCheckTextureSlots(L"ps-t[0:128]", &slots) && slots.empty(),
        "auto-tex: shader range past the D3D11 limit is rejected");
    require_batch(!ParseAutoCheckTextureSlots(L"ps-t[9:0]", &slots) && slots.empty(),
        "auto-tex: reversed range is rejected");
    require_batch(!ParseAutoCheckTextureSlots(L"vb32", &slots) && slots.empty(),
        "auto-tex: vertex buffer slot past the D3D11 limit is rejected");
    require_batch(!ParseAutoCheckTextureSlots(L"hs-t0", &slots) && slots.empty(),
        "auto-tex: unsupported stage is rejected instead of silently ignored");
    require_batch(!ParseAutoCheckTextureSlots(L"ps-t0x", &slots) && slots.empty(),
        "auto-tex: trailing garbage is rejected");

    AutoTextureTestScope scope;

    // A.6 predicate: only plain static slots listed in the configuration.
    AutoCheckTextureSlot ps0;
    ps0.type = ResourceCopyTargetType::SHADER_RESOURCE;
    ps0.shader_type = L'p';
    ps0.slot = 0;
    G->auto_check_texture_slots.push_back(ps0);
    AutoCheckTextureSlot ps1;
    ps1.type = ResourceCopyTargetType::SHADER_RESOURCE;
    ps1.shader_type = L'p';
    ps1.slot = 1;
    G->auto_check_texture_slots.push_back(ps1);
    AutoCheckTextureSlot cs0;
    cs0.type = ResourceCopyTargetType::SHADER_RESOURCE;
    cs0.shader_type = L'c';
    cs0.slot = 0;
    G->auto_check_texture_slots.push_back(cs0);
    AutoCheckTextureSlot cs1;
    cs1.type = ResourceCopyTargetType::SHADER_RESOURCE;
    cs1.shader_type = L'c';
    cs1.slot = 1;
    G->auto_check_texture_slots.push_back(cs1);
    AutoCheckTextureSlot vb1;
    vb1.type = ResourceCopyTargetType::VERTEX_BUFFER;
    vb1.slot = 1;
    G->auto_check_texture_slots.push_back(vb1);
    AutoCheckTextureSlot ib;
    ib.type = ResourceCopyTargetType::INDEX_BUFFER;
    ib.slot = 0;
    G->auto_check_texture_slots.push_back(ib);

    ResourceCopyTarget target;
    target.type = ResourceCopyTargetType::SHADER_RESOURCE;
    target.shader_type = L'p';
    target.slot = 0;
    require_batch(IsAutoCheckTextureSlotCovered(target), "auto-tex: configured ps-t0 is covered");
    target.shader_type = L'v';
    require_batch(!IsAutoCheckTextureSlotCovered(target), "auto-tex: vs-t0 is not covered by ps-t0");
    target.shader_type = L'p';
    target.evaluation_mode = ResourceCopyTargetEvaluationMode::SLOT_RANGE;
    require_batch(!IsAutoCheckTextureSlotCovered(target), "auto-tex: dynamic range targets stay on the legacy path");
    target.evaluation_mode = ResourceCopyTargetEvaluationMode::RESOURCE;

    ResourceCopyTarget vb_target;
    vb_target.type = ResourceCopyTargetType::VERTEX_BUFFER;
    vb_target.slot = 1;
    require_batch(IsAutoCheckTextureSlotCovered(vb_target), "auto-tex: configured vb1 is covered");
    vb_target.slot = 2;
    require_batch(!IsAutoCheckTextureSlotCovered(vb_target), "auto-tex: unconfigured vb2 is not covered");
    ResourceCopyTarget ib_target;
    ib_target.type = ResourceCopyTargetType::INDEX_BUFFER;
    require_batch(IsAutoCheckTextureSlotCovered(ib_target), "auto-tex: configured ib is covered");

    // Real batch entry: adjacent covered checks merge into one command that
    // runs at the first check's original list position. Execute it against a
    // WARP context and verify hit order, this, post phase, and the dirty
    // binding re-query after a TO changes a later slot.
    CommandList command_list;
    auto check0 = std::make_shared<CheckTextureOverrideCommand>();
    check0->target.type = ResourceCopyTargetType::SHADER_RESOURCE;
    check0->target.shader_type = L'p';
    check0->target.slot = 0;
    auto check1 = std::make_shared<CheckTextureOverrideCommand>();
    check1->target.type = ResourceCopyTargetType::SHADER_RESOURCE;
    check1->target.shader_type = L'p';
    check1->target.slot = 1;
    command_list.commands.push_back(check0);
    command_list.commands.push_back(check1);
    MergeAutoCheckTextureOverrides(&command_list);
    require_batch(command_list.commands.size() == 1,
        "auto-tex: adjacent covered checks merge into one batch command");
    auto* batch = dynamic_cast<AutoCheckTextureOverrideCommand*>(command_list.commands.front().get());
    require_batch(batch && batch->targets.size() == 2,
        "auto-tex: merged command owns both original checks in order");

    BatchDevice device;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 8;
    desc.Height = 8;
    desc.ArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture0, texture1;
    require_batch(SUCCEEDED(device.device->CreateTexture2D(&desc, nullptr, &texture0)) &&
        SUCCEEDED(device.device->CreateTexture2D(&desc, nullptr, &texture1)),
        "auto-tex: create two tracked textures through the real wrapper device");
    ComPtr<ID3D11ShaderResourceView> view0, view1;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(texture0.Get(), nullptr, &view0)) &&
        SUCCEEDED(device.native->CreateShaderResourceView(texture1.Get(), nullptr, &view1)),
        "auto-tex: create actual shader resource views");
    ID3D11ShaderResourceView* bound0 = view0.Get();
    ID3D11ShaderResourceView* bound1 = view1.Get();
    device.context->PSSetShaderResources(0, 1, &bound0);
    device.context->PSSetShaderResources(1, 1, &bound1);

    const uint32_t hash0 = 0x5a17c0de;
    const uint32_t hash1 = 0x5a17c0df;
    auto set_hash = [&](ID3D11Resource* resource, uint32_t hash) {
        CriticalSectionLockGuard state_lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        CriticalSectionLockGuard resources_lock(&G->mResourcesLock, __FUNCTION__, __LINE__);
        auto found = G->mResources.find(resource);
        require_batch(found != G->mResources.end(), "auto-tex: wrapper resource is registered in mResources");
        found->second.hash = hash;
        found->second.texture_override_candidates_valid = false;
    };
    set_hash(texture0.Get(), hash0);
    set_hash(texture1.Get(), hash1);

    std::vector<int> order;
    std::vector<unsigned> this_slots;
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        auto& list0 = G->mTextureOverrideMap[hash0];
        list0.emplace_back();
        TextureOverride& to0 = list0.back();
        to0.ini_section = L"AutoTextureCheckAudit0";
        to0.command_list.commands.push_back(std::make_shared<AutoTextureRecordThisCommand>(this_slots));
        to0.command_list.commands.push_back(std::make_shared<AutoTextureOrderCommand>(order, 10));
        to0.post_command_list.commands.push_back(std::make_shared<AutoTextureOrderCommand>(order, 11));

        auto& list1 = G->mTextureOverrideMap[hash1];
        list1.emplace_back();
        TextureOverride& to1 = list1.back();
        to1.ini_section = L"AutoTextureCheckAudit1";
        to1.command_list.commands.push_back(std::make_shared<AutoTextureOrderCommand>(order, 20));
        to1.post_command_list.commands.push_back(std::make_shared<AutoTextureOrderCommand>(order, 21));
    }
    InvalidateTextureOverrideCandidates();

    // Replacement mode with zero check commands: coverage is all zero, and
    // the real independent entry must still run pre and post TO lists.
    {
        auto saved_registered = registered_command_lists;
        registered_command_lists.clear();
        BuildAutoCheckSlotCoverage();
        registered_command_lists = saved_registered;
    }
    require_batch(G->auto_check_texture_slot_has_check.size() == G->auto_check_texture_slots.size(),
        "auto-tex: coverage vector matches configured slots");
    for (uint8_t has_check : G->auto_check_texture_slot_has_check)
        require_batch(!has_check, "auto-tex: no command-list check means fallback slot");

    DrawCallInfo fallback_info;
    CommandListState fallback_state;
    fallback_state.mHackerDevice = device.device;
    fallback_state.mHackerContext = device.wrapper.get();
    fallback_state.mOrigDevice1 = device.native.Get();
    fallback_state.mOrigContext1 = device.context.Get();
    fallback_state.call_info = &fallback_info;
    order.clear();
    this_slots.clear();
    AutoCheckTextureOverridesIndependent(&fallback_state, false);
    require_batch(order == std::vector<int>({10, 20, 11, 21}) &&
        this_slots == std::vector<unsigned>({0}),
        "auto-tex: independent pass triggers pre/post TOs and this with zero check commands");
    order.clear();
    this_slots.clear();
    G->mod_runtime_optimizations = false;
    AutoCheckTextureOverridesIndependent(&fallback_state, false);
    G->mod_runtime_optimizations = true;
    require_batch(order == std::vector<int>({10, 20, 11, 21}) &&
        this_slots == std::vector<unsigned>({0}),
        "runtime optimizations: legacy target selection preserves auto-check order and this");

    // Old check + auto both present: the independent pass must skip slots that
    // already have a merged check; the merged command then runs each TO once.
    {
        auto saved_registered = registered_command_lists;
        registered_command_lists.clear();
        registered_command_lists.push_back(&command_list);
        BuildAutoCheckSlotCoverage();
        registered_command_lists = saved_registered;
    }
    require_batch(G->auto_check_texture_slot_has_check.size() == G->auto_check_texture_slots.size() &&
        G->auto_check_texture_slot_has_check[0] && G->auto_check_texture_slot_has_check[1],
        "auto-tex: merged checks mark their slots as covered");

    DrawCallInfo exclusion_info;
    CommandListState exclusion_state;
    exclusion_state.mHackerDevice = device.device;
    exclusion_state.mHackerContext = device.wrapper.get();
    exclusion_state.mOrigDevice1 = device.native.Get();
    exclusion_state.mOrigContext1 = device.context.Get();
    exclusion_state.call_info = &exclusion_info;
    order.clear();
    this_slots.clear();
    AutoCheckTextureOverridesIndependent(&exclusion_state, false);
    require_batch(order.empty() && this_slots.empty(),
        "auto-tex: independent pass skips slots that already have a check command");

    DrawCallInfo call_info;
    CommandListState state;
    state.mHackerDevice = device.device;
    state.mHackerContext = device.wrapper.get();
    state.mOrigDevice1 = device.native.Get();
    state.mOrigContext1 = device.context.Get();
    state.call_info = &call_info;
    state.post = false;

    AutoCheckTextureOverrides(&state, batch->targets);
    require_batch(order == std::vector<int>({10, 20}) && this_slots == std::vector<unsigned>({0}),
        "auto-tex: batch executes both TO pre lists in target order with this set to each slot");

    state.post = true;
    AutoCheckTextureOverrides(&state, batch->targets);
    require_batch(order == std::vector<int>({10, 20, 11, 21}),
        "auto-tex: batch executes post lists only in the post phase, in target order");

    // Exercise config-time batching with AutoCheck disabled and the real direct
    // execution path. Matching the first slot can change the second binding.
    {
        auto saved_slots = std::move(G->auto_check_texture_slots);
        auto saved_coverage = std::move(G->auto_check_texture_slot_has_check);
        auto saved_graphics = std::move(G->auto_check_graphics_targets);
        auto saved_compute = std::move(G->auto_check_compute_targets);
        auto saved_registered = std::move(registered_command_lists);
        CommandList direct;
        direct.commands = {check0, check1};
        registered_command_lists.push_back(&direct);
        optimise_command_lists(device.device);
        registered_command_lists = std::move(saved_registered);
        G->auto_check_texture_slots = std::move(saved_slots);
        G->auto_check_texture_slot_has_check = std::move(saved_coverage);
        G->auto_check_graphics_targets = std::move(saved_graphics);
        G->auto_check_compute_targets = std::move(saved_compute);
        require_batch(direct.commands.size() == 1, "direct checks: adjacent static PS slots are merged");

        order.clear();
        this_slots.clear();
        state.post = false;
        direct.commands.front()->run(&state);
        state.post = true;
        direct.commands.front()->run(&state);
        require_batch(order == std::vector<int>({10, 20, 11, 21}) && this_slots == std::vector<unsigned>({0}),
            "direct checks: preserve pre/post order and this");

        auto& pre = G->mTextureOverrideMap[hash0].front().command_list.commands;
        pre.push_back(std::make_shared<AutoTextureBindCommand>(1, nullptr));
        order.clear();
        this_slots.clear();
        state.post = false;
        direct.commands.front()->run(&state);
        require_batch(order == std::vector<int>({10}), "direct checks: do not use a prefetched view after a TO rebinds it");
        pre.pop_back();
        device.context->PSSetShaderResources(1, 1, &bound1);

        ID3D11ShaderResourceView* empty[] = {nullptr, nullptr};
        device.context->PSSetShaderResources(0, 2, empty);
        ShaderResourceBatchStats stats;
        state.slot_batch_stats = &stats;
        order.clear();
        direct.commands.front()->run(&state);
        require_batch(order.empty() && stats.get_calls == 1, "direct checks: two empty slots use one binding query");
        G->mod_runtime_optimizations = false;
        direct.commands.front()->run(&state);
        G->mod_runtime_optimizations = true;
        require_batch(order.empty() && stats.get_calls == 1,
            "runtime optimizations: disabling the batch runs the original checks individually");
        state.slot_batch_stats = nullptr;
        device.context->PSSetShaderResources(0, 1, &bound0);
        device.context->PSSetShaderResources(1, 1, &bound1);
    }

    {
        // Execute the real set_viewport copy through a nested CustomShader.
        // The child must restore its parent's viewport and the caller's must
        // also survive the outer call, which does not change it directly.
        CustomShader outer, inner;
        D3D11_VIEWPORT original{3.0f, 7.0f, 96.0f, 48.0f, 0.1f, 0.8f};
        device.context->RSSetViewports(1, &original);
        auto copy = std::make_shared<ResourceCopyOperation>();
        copy->src = check0->target;
        copy->dst = check0->target;
        copy->dst.slot = 2;
        copy->options = ResourceCopyOptions::REFERENCE;
        copy->options |= ResourceCopyOptions::SET_VIEWPORT;
        inner.command_list.commands = {copy, std::make_shared<CheckViewportWidthCommand>(8.0f)};
        auto run_inner = std::make_shared<RunCustomShaderCommand>();
        run_inner->custom_shader = &inner;
        outer.command_list.commands = {run_inner, std::make_shared<CheckViewportWidthCommand>(96.0f)};
        RunCustomShaderCommand run_outer;
        run_outer.custom_shader = &outer;
        run_outer.run(&state);
        D3D11_VIEWPORT after{};
        UINT count = 1;
        device.context->RSGetViewports(&count, &after);
        require_batch(count == 1 && !std::memcmp(&original, &after, sizeof(after)),
            "custom shader: caller's full viewport is restored");

        // The immutable merge source can be unchanged while associated dynamic
        // blend parameters are different for each draw.
        inner.blend_override = 2;
        std::memset(&inner.blend_mask, 0xff, sizeof(inner.blend_mask));
        FLOAT first[] = {0.0f, 0.25f, 0.5f, 0.75f};
        FLOAT second[] = {1.0f, 0.75f, 0.5f, 0.25f};
        inner.merge_blend_states(nullptr, first, 0x55, device.native.Get());
        auto* blend_state = inner.blend_state;
        inner.merge_blend_states(nullptr, second, 0xaa, device.native.Get());
        require_batch(blend_state && inner.blend_state == blend_state && inner.blend_sample_mask == 0xaa &&
            !std::memcmp(inner.blend_factor, second, sizeof(second)),
            "custom shader: a repeated merge refreshes dynamic blend parameters");
    }

    {
        auto output_desc = desc;
        output_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> output;
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11UnorderedAccessView> uav;
        require_batch(SUCCEEDED(device.native->CreateTexture2D(&output_desc, nullptr, &output)) &&
            SUCCEEDED(device.native->CreateRenderTargetView(output.Get(), nullptr, &rtv)) &&
            SUCCEEDED(device.native->CreateUnorderedAccessView(output.Get(), nullptr, &uav)),
            "custom shader: create real aliased OM/CS output views");
        auto* raw = rtv.Get();
        device.context->OMSetRenderTargets(1, &raw, nullptr);
        const auto original_target = std::make_shared<CheckRenderTargetCommand>(raw);

        CustomShader outer, inner;
        RunCustomShaderCommand run_outer;
        run_outer.custom_shader = &outer;
        ShaderResourceBatchStats stats;
        state.slot_batch_stats = &stats;
        outer.command_list.commands = {std::make_shared<BindComputeOutputCommand>(nullptr, nullptr), original_target};
        run_outer.run(&state);
        require_batch(stats.om_saves == 0 && stats.om_restores == 0,
            "custom shader: a null compute UAV bind needs no OM snapshot");
        G->mod_runtime_optimizations = false;
        run_outer.run(&state);
        G->mod_runtime_optimizations = true;
        require_batch(stats.om_saves == 1 && stats.om_restores == 1 && stats.om_uav_probes == 0,
            "runtime optimizations: disabling lazy OM uses the original save and restore");
        stats = {};

        auto clear_output = std::make_shared<ResourceCopyOperation>();
        clear_output->src.type = ResourceCopyTargetType::EMPTY;
        clear_output->dst.type = ResourceCopyTargetType::RENDER_TARGET;
        clear_output->dst.slot = 0;
        inner.command_list.commands = {clear_output, std::make_shared<CheckRenderTargetCommand>(nullptr)};
        auto run_inner = std::make_shared<RunCustomShaderCommand>();
        run_inner->custom_shader = &inner;
        outer.command_list.commands = {run_inner, original_target};
        run_outer.run(&state);
        require_batch(stats.om_saves == 1 && stats.om_restores == 1,
            "custom shader: only the nested output writer saves and restores OM");

        outer.command_list.commands = {std::make_shared<BindComputeOutputCommand>(output.Get(), uav.Get())};
        run_outer.run(&state);
        original_target->run(&state);
        require_batch(stats.om_saves == 2 && stats.om_restores == 2,
            "custom shader: retain OM restoration for a compute UAV alias");
        state.slot_batch_stats = nullptr;
        device.context->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11UnorderedAccessView* empty = nullptr;
        device.context->CSSetUnorderedAccessViews(0, 1, &empty, nullptr);
    }

    // A.4: 2000 draws, 20 candidate resources, 1980 no-candidate resources.
    // The whole batch must be skipped for the no-candidate draws; the
    // per-resource candidate cache is invalidated on reload and recomputed for
    // a new resource.
    const uint32_t hash_a4 = 0x5a17c0e0;
    const uint32_t hash_none = 0x5a17c0e1;
    std::vector<ComPtr<ID3D11Texture2D>> a4_textures(21);
    std::vector<ComPtr<ID3D11ShaderResourceView>> a4_views(21);
    for (int i = 0; i < 21; ++i) {
        require_batch(SUCCEEDED(device.device->CreateTexture2D(&desc, nullptr, &a4_textures[i])),
            "auto-tex A.4: create candidate/non-candidate texture");
        require_batch(SUCCEEDED(device.native->CreateShaderResourceView(a4_textures[i].Get(), nullptr, &a4_views[i])),
            "auto-tex A.4: create candidate/non-candidate view");
        set_hash(a4_textures[i].Get(), i < 20 ? hash_a4 : hash_none);
    }
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        auto& list = G->mTextureOverrideMap[hash_a4];
        list.emplace_back();
        list.back().ini_section = L"AutoTextureCheckAuditA4";
        list.back().command_list.commands.push_back(std::make_shared<AutoTextureOrderCommand>(order, 40));
        list.back().post_command_list.commands.push_back(std::make_shared<AutoTextureOrderCommand>(order, 41));
    }
    InvalidateTextureOverrideCandidates();

    auto a4_check = std::make_shared<CheckTextureOverrideCommand>();
    a4_check->target.type = ResourceCopyTargetType::SHADER_RESOURCE;
    a4_check->target.shader_type = L'p';
    a4_check->target.slot = 0;
    std::vector<std::shared_ptr<CheckTextureOverrideCommand>> a4_targets{a4_check};
    DrawCallInfo a4_call_info;
    CommandListState a4_state;
    a4_state.mHackerDevice = device.device;
    a4_state.mHackerContext = device.wrapper.get();
    a4_state.mOrigDevice1 = device.native.Get();
    a4_state.mOrigContext1 = device.context.Get();
    a4_state.call_info = &a4_call_info;
    a4_state.post = false;

    order.clear();
    G->auto_check_texture_full_pass_count = 0;
    for (int i = 0; i < 2000; ++i) {
        ID3D11ShaderResourceView* a4_view = a4_views[i < 20 ? i : 20].Get();
        device.context->PSSetShaderResources(0, 1, &a4_view);
        AutoCheckTextureOverrides(&a4_state, a4_targets);
    }
    require_batch(order.size() == 20 && G->auto_check_texture_full_pass_count == 20,
        "auto-tex A.4: only 20 candidate draws run the batch; 1980 no-candidate draws skip");

    // Cache invalidation: add a TO for the formerly no-candidate hash, then
    // invalidate candidates; the next draw must recompute and run.
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        auto& list = G->mTextureOverrideMap[hash_none];
        list.emplace_back();
        list.back().ini_section = L"AutoTextureCheckAuditA4Late";
        list.back().command_list.commands.push_back(std::make_shared<AutoTextureOrderCommand>(order, 50));
    }
    InvalidateTextureOverrideCandidates();
    ID3D11ShaderResourceView* none_view = a4_views[20].Get();
    device.context->PSSetShaderResources(0, 1, &none_view);
    order.clear();
    AutoCheckTextureOverrides(&a4_state, a4_targets);
    require_batch(order == std::vector<int>({50}),
        "auto-tex A.4: reload/invalidate recomputes the resource candidate cache");

    // A new resource with the same hash must not inherit a stale negative flag.
    ComPtr<ID3D11Texture2D> fresh_none;
    ComPtr<ID3D11ShaderResourceView> fresh_none_view;
    require_batch(SUCCEEDED(device.device->CreateTexture2D(&desc, nullptr, &fresh_none)) &&
        SUCCEEDED(device.native->CreateShaderResourceView(fresh_none.Get(), nullptr, &fresh_none_view)),
        "auto-tex A.4: create fresh resource");
    set_hash(fresh_none.Get(), hash_none);
    ID3D11ShaderResourceView* fresh_bound = fresh_none_view.Get();
    device.context->PSSetShaderResources(0, 1, &fresh_bound);
    order.clear();
    AutoCheckTextureOverrides(&a4_state, a4_targets);
    require_batch(order == std::vector<int>({50}),
        "auto-tex A.4: a new resource computes its candidate flag from scratch");

    // A.4 IB: reuse the mod's IB hash + match_first_index judgment. 20 draws
    // with FirstIndex matching run the full pass; 1980 with a different
    // FirstIndex must be skipped even though the IB hash is in the same table.
    const uint32_t ib_hash = 0x9040e2c4;
    const uint32_t ib_hash_none = 0x9040e2c5;
    ComPtr<ID3D11Buffer> ib_buffer, ib_buffer_none;
    D3D11_BUFFER_DESC ib_desc = {};
    ib_desc.ByteWidth = 1024;
    ib_desc.Usage = D3D11_USAGE_DEFAULT;
    ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&ib_desc, nullptr, &ib_buffer)) &&
        SUCCEEDED(device.device->CreateBuffer(&ib_desc, nullptr, &ib_buffer_none)),
        "auto-tex A.4: create tracked index buffers");
    set_hash(ib_buffer.Get(), ib_hash);
    set_hash(ib_buffer_none.Get(), ib_hash_none);
    device.context->IASetIndexBuffer(ib_buffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        auto& list = G->mTextureOverrideMap[ib_hash];
        list.emplace_back();
        TextureOverride& to = list.back();
        to.ini_section = L"AutoTextureCheckAuditIB";
        to.has_draw_context_match = true;
        to.match_index_count.op = FuzzyMatchOp::EQUAL;
        to.match_index_count.val = 6;
        to.match_index_count.mask = 0xffffffff;
        to.match_first_index.op = FuzzyMatchOp::EQUAL;
        to.match_first_index.val = 0;
        to.match_first_index.mask = 0xffffffff;
        to.command_list.commands.push_back(std::make_shared<AutoTextureOrderCommand>(order, 70));
        to.post_command_list.commands.push_back(std::make_shared<AutoTextureOrderCommand>(order, 71));
    }
    InvalidateTextureOverrideCandidates();

    auto ib_check = std::make_shared<CheckTextureOverrideCommand>();
    ib_check->target.type = ResourceCopyTargetType::INDEX_BUFFER;
    ib_check->target.slot = 0;
    std::vector<std::shared_ptr<CheckTextureOverrideCommand>> ib_targets{ib_check};
    DrawCallInfo ib_call_info(DrawCall::DrawIndexed, 0, 6, 0, 0, 0, 0, nullptr, 0);
    CommandListState ib_state;
    ib_state.mHackerDevice = device.device;
    ib_state.mHackerContext = device.wrapper.get();
    ib_state.mOrigDevice1 = device.native.Get();
    ib_state.mOrigContext1 = device.context.Get();
    ib_state.call_info = &ib_call_info;
    ib_state.post = false;
    const bool saved_region_hashes = G->track_region_hashes;
    G->track_region_hashes = false;
    order.clear();
    G->auto_check_texture_full_pass_count = 0;
    for (int i = 0; i < 2000; ++i) {
        device.context->IASetIndexBuffer(i < 20 ? ib_buffer.Get() : ib_buffer_none.Get(),
            DXGI_FORMAT_R16_UINT, 0);
        ib_call_info.FirstIndex = 0;
        AutoCheckTextureOverrides(&ib_state, ib_targets);
    }
    require_batch(order.size() == 20 && G->auto_check_texture_full_pass_count == 20,
        "auto-tex A.4 IB: 20 hash hits run the batch; 1980 no-candidate IBs skip");

    // The same candidate IB hash with a different match_first_index must still
    // skip (the mod's LOD sections rely on this exact filter).
    device.context->IASetIndexBuffer(ib_buffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    ib_call_info.FirstIndex = 1;
    AutoCheckTextureOverrides(&ib_state, ib_targets);
    require_batch(order.size() == 20 && G->auto_check_texture_full_pass_count == 20,
        "auto-tex A.4 IB: hash hit with mismatched match_first_index is skipped");

    // VertexLimitRaise / count replacement changes the live call_info. The
    // candidate gate and batch matching must use the draw-start snapshot.
    ib_call_info.FirstIndex = 0;
    ib_call_info.IndexCount = 6;
    device.wrapper->auto_check_texture_scopes.emplace_back();
    {
        AutoCheckTextureDrawScope& scope = device.wrapper->auto_check_texture_scopes.back();
        scope.call_info = &ib_call_info;
        scope.original_call_info = ib_call_info;
    }
    ib_call_info.IndexCount = 9999; // simulate VertexLimitRaise/replacement
    device.context->IASetIndexBuffer(ib_buffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    order.clear();
    AutoCheckTextureOverrides(&ib_state, ib_targets);
    require_batch(order == std::vector<int>({70}),
        "auto-tex A.4 IB: candidate gate uses original IndexCount snapshot after VertexLimitRaise");
    device.wrapper->auto_check_texture_scopes.pop_back();
    ib_call_info.IndexCount = 6;
    ib_call_info.FirstIndex = 0;
    G->track_region_hashes = saved_region_hashes;
    device.context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

    device.context->PSSetShaderResources(0, 1, &bound0);
    device.context->PSSetShaderResources(1, 1, &bound1);

    // Make TO0's pre list rebind PS1 to texture0. The batch must re-query the
    // next target instead of matching the stale prefetch snapshot.
    {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        auto& to0 = G->mTextureOverrideMap[hash0].front();
        to0.command_list.commands.push_back(std::make_shared<AutoTextureBindCommand>(1, view0.Get()));
    }
    order.clear();
    this_slots.clear();
    state.post = false;
    AutoCheckTextureOverrides(&state, batch->targets);
    require_batch(order == std::vector<int>({10, 10}) &&
        this_slots == std::vector<unsigned>({0, 1}),
        "auto-tex: after a TO rebinds a later slot, the batch re-queries it in-lock and matches the current binding");

    // cs-tN replacement mode and merged mode both use the dispatch context.
    ID3D11ShaderResourceView* cs_bound0 = view0.Get();
    ID3D11ShaderResourceView* cs_bound1 = view1.Get();
    device.context->CSSetShaderResources(0, 1, &cs_bound0);
    device.context->CSSetShaderResources(1, 1, &cs_bound1);

    DrawCallInfo dispatch_info;
    dispatch_info.type = DrawCall::Dispatch;
    CommandListState cs_state;
    cs_state.mHackerDevice = device.device;
    cs_state.mHackerContext = device.wrapper.get();
    cs_state.mOrigDevice1 = device.native.Get();
    cs_state.mOrigContext1 = device.context.Get();
    cs_state.call_info = &dispatch_info;
    order.clear();
    this_slots.clear();
    AutoCheckTextureOverridesIndependent(&cs_state, true);
    require_batch(order == std::vector<int>({10, 20, 11, 21}) &&
        this_slots == std::vector<unsigned>({0}),
        "auto-tex: dispatch independent pass triggers cs-tN pre/post with zero check commands");

    CommandList cs_list;
    auto cs_check0 = std::make_shared<CheckTextureOverrideCommand>();
    cs_check0->target.type = ResourceCopyTargetType::SHADER_RESOURCE;
    cs_check0->target.shader_type = L'c';
    cs_check0->target.slot = 0;
    auto cs_check1 = std::make_shared<CheckTextureOverrideCommand>();
    cs_check1->target.type = ResourceCopyTargetType::SHADER_RESOURCE;
    cs_check1->target.shader_type = L'c';
    cs_check1->target.slot = 1;
    cs_list.commands.push_back(cs_check0);
    cs_list.commands.push_back(cs_check1);
    MergeAutoCheckTextureOverrides(&cs_list);
    auto* cs_batch = dynamic_cast<AutoCheckTextureOverrideCommand*>(cs_list.commands.front().get());
    require_batch(cs_batch && cs_batch->targets.size() == 2,
        "auto-tex: adjacent covered cs-tN checks merge for compute dispatch");
    order.clear();
    this_slots.clear();
    AutoCheckTextureOverrides(&cs_state, cs_batch->targets);
    require_batch(order == std::vector<int>({10, 20}) && this_slots == std::vector<unsigned>({0}),
        "auto-tex: merged cs-tN batch runs from a dispatch call context and preserves target order");

    // A.4 fuzzy: a fuzzy matcher that matches the descriptor must prevent a
    // false negative whole-batch skip.
    ID3D11ShaderResourceView* null_views[2] = {nullptr, nullptr};
    device.context->CSSetShaderResources(0, 2, null_views);
    ID3D11ShaderResourceView* null_ps1 = nullptr;
    device.context->PSSetShaderResources(1, 1, &null_ps1);

    const uint32_t hash_fuzzy = 0x5a17c0e2;
    ComPtr<ID3D11Texture2D> fuzzy_texture;
    ComPtr<ID3D11ShaderResourceView> fuzzy_view;
    require_batch(SUCCEEDED(device.device->CreateTexture2D(&desc, nullptr, &fuzzy_texture)) &&
        SUCCEEDED(device.native->CreateShaderResourceView(fuzzy_texture.Get(), nullptr, &fuzzy_view)),
        "auto-tex A.4: create fuzzy-only resource");
    set_hash(fuzzy_texture.Get(), hash_fuzzy);
    ID3D11ShaderResourceView* fuzzy_bound = fuzzy_view.Get();
    device.context->PSSetShaderResources(0, 1, &fuzzy_bound);

    auto fuzzy_matcher = std::make_shared<FuzzyMatchResourceDesc>(L"AutoTextureCheckAuditFuzzy");
    fuzzy_matcher->texture_override->command_list.commands.push_back(
        std::make_shared<AutoTextureOrderCommand>(order, 60));
    fuzzy_matcher->texture_override->post_command_list.commands.push_back(
        std::make_shared<AutoTextureOrderCommand>(order, 61));
    G->mFuzzyTextureOverrides.insert(fuzzy_matcher);
    InvalidateTextureOverrideCandidates();

    auto saved_registered = registered_command_lists;
    registered_command_lists.clear();
    BuildAutoCheckSlotCoverage();
    registered_command_lists = std::move(saved_registered);

    DrawCallInfo fuzzy_call_info;
    CommandListState fuzzy_state;
    fuzzy_state.mHackerDevice = device.device;
    fuzzy_state.mHackerContext = device.wrapper.get();
    fuzzy_state.mOrigDevice1 = device.native.Get();
    fuzzy_state.mOrigContext1 = device.context.Get();
    fuzzy_state.call_info = &fuzzy_call_info;
    fuzzy_state.post = false;
    order.clear();
    this_slots.clear();
    AutoCheckTextureOverridesIndependent(&fuzzy_state, false);
    require_batch(order == std::vector<int>({60, 61}),
        "auto-tex A.4: a matching fuzzy override is not falsely skipped by the hash-only cache");
}

extern "C" size_t dx11_audit_context1_vtable_entries(void);
extern "C" size_t dx11_candidate_ps_get_slot(void);
extern "C" size_t dx11_candidate_vb_get_slot(void);
extern "C" size_t dx11_candidate_ib_get_slot(void);

namespace {
// Observe native binding queries without replacing their results or semantics.
class CandidateBindingSpy {
    using PSGet = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext1*, UINT, UINT, ID3D11ShaderResourceView**);
    using VBGet = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext1*, UINT, UINT, ID3D11Buffer**, UINT*, UINT*);
    using IBGet = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext1*, ID3D11Buffer**, DXGI_FORMAT*, UINT*);
    ID3D11DeviceContext1* context;
    void** original;
    std::vector<void*> table;
    static thread_local CandidateBindingSpy* active;
    static void STDMETHODCALLTYPE ps(ID3D11DeviceContext1* self, UINT first, UINT count, ID3D11ShaderResourceView** views)
    {
        ++active->ps_calls; active->ps_first = first; active->ps_count = count;
        reinterpret_cast<PSGet>(active->original[dx11_candidate_ps_get_slot()])(self, first, count, views);
    }
    static void STDMETHODCALLTYPE vb(ID3D11DeviceContext1* self, UINT first, UINT count, ID3D11Buffer** buffers, UINT* strides, UINT* offsets)
    {
        ++active->vb_calls; active->vb_first = first; active->vb_count = count;
        reinterpret_cast<VBGet>(active->original[dx11_candidate_vb_get_slot()])(self, first, count, buffers, strides, offsets);
    }
    static void STDMETHODCALLTYPE ib(ID3D11DeviceContext1* self, ID3D11Buffer** buffer, DXGI_FORMAT* format, UINT* offset)
    {
        ++active->ib_calls;
        reinterpret_cast<IBGet>(active->original[dx11_candidate_ib_get_slot()])(self, buffer, format, offset);
    }
public:
    unsigned ps_calls = 0, vb_calls = 0, ib_calls = 0, ps_first = 0, ps_count = 0;
    unsigned vb_first = 0, vb_count = 0;
    explicit CandidateBindingSpy(ID3D11DeviceContext1* value) : context(value), original(*reinterpret_cast<void***>(value)),
        table(original, original + dx11_audit_context1_vtable_entries())
    {
        dx11_slot_test::require_batch(!active, "candidate: one native spy per thread");
        table[dx11_candidate_ps_get_slot()] = reinterpret_cast<void*>(&ps);
        table[dx11_candidate_vb_get_slot()] = reinterpret_cast<void*>(&vb);
        table[dx11_candidate_ib_get_slot()] = reinterpret_cast<void*>(&ib);
        active = this;
        *reinterpret_cast<void***>(context) = table.data();
    }
    ~CandidateBindingSpy() { *reinterpret_cast<void***>(context) = original; active = nullptr; }
};
thread_local CandidateBindingSpy* CandidateBindingSpy::active = nullptr;

struct CandidateRoutingCase {
    const char* name;
    bool query, match, enabled, unused_query;
};
constexpr CandidateRoutingCase candidate_routing_cases[] = {
    {"legacy_hit", false, true, true, false},
    {"legacy_miss", false, false, true, false},
    {"explicit_hit", true, true, true, false},
    {"explicit_miss", true, false, true, false},
    {"explicit_disabled", true, true, false, false},
    {"legacy_with_unused_query", false, true, true, true},
};

struct CandidatePerfWorkload {
    const char* name;
    unsigned matches;
    bool has_rules, range_miss, fuzzy;
};
constexpr CandidatePerfWorkload candidate_workloads[] = {
    {"hash_miss", 0, true, false, false},
    {"sparse_hash", 20, true, false, false},
    {"all_hit", 2000, true, false, false},
    {"no_rules", 0, false, false, false},
    {"range_miss", 0, true, true, false},
    {"sparse_with_fuzzy", 20, true, false, true},
};

template<class Function>
void measure_candidate_slice(const char* scenario, const char* operation, unsigned sample, Function&& function)
{
    const unsigned iterations = 20000;
    uint64_t checksum = 0;
    for (unsigned i = 0; i < 500; ++i) checksum += function();
    LARGE_INTEGER frequency{}, begin{}, end{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    for (unsigned i = 0; i < iterations; ++i) checksum += function();
    QueryPerformanceCounter(&end);
    std::printf("TO_CANDIDATE_COST={\"scenario\":\"%s\",\"operation\":\"%s\",\"sample\":%u,\"iterations\":%u,\"nsPerCall\":%.3f,\"checksum\":%llu}\n",
        scenario, operation, sample, iterations,
        double(end.QuadPart - begin.QuadPart) * 1e9 / frequency.QuadPart / iterations,
        static_cast<unsigned long long>(checksum));
}
}


void test_dx11_to_candidate_regression()
{
    using namespace dx11_slot_test;
    AuditConfigScope config;
    BatchDevice device;
    DrawCallInfo info(DrawCall::DrawIndexed, 9, 6, 3, 2, 0, 4, nullptr, 0);
    device.state.call_info = &info;
    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = 256;
    buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER | D3D11_BIND_VERTEX_BUFFER;
    ComPtr<ID3D11Buffer> buffer;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&buffer_desc, nullptr, &buffer)), "candidate: tracked buffer");
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 8;
    desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    require_batch(SUCCEEDED(device.device->CreateTexture2D(&desc, nullptr, &texture)), "candidate: tracked texture");
    auto view = srv(device, texture.Get());
    auto set_hash = [](ID3D11Resource* resource, uint32_t hash) {
        CriticalSectionLockGuard state_lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        CriticalSectionLockGuard resources_lock(&G->mResourcesLock, __FUNCTION__, __LINE__);
        auto found = G->mResources.find(resource);
        require_batch(found != G->mResources.end(), "candidate: registered resource");
        found->second.hash = hash;
    };
    set_hash(texture.Get(), 0x5a17c0de);
    set_hash(buffer.Get(), 0x9040e2c4);
    const char* exact =
        "global $candidate_commands=0\nglobal $candidate_slot=127\n"
        "[TextureOverrideCandidateTexture]\nhash=5a17c0de\nfilter_index=0\n"
        "match_index_count=6\nmatch_first_index=0\nmatch_vertex_count=9\n"
        "match_first_vertex=2\nmatch_instance_count=3\nmatch_first_instance=4\n"
        "$candidate_commands=$candidate_commands+1\npost $candidate_commands=$candidate_commands+1\n"
        "[TextureOverrideCandidateBuffer]\nhash=9040e2c4\nmatch_first_index=0\n";
    config.load(exact, 0, true);
    CommandListScope scope;
    const std::wstring ini_namespace;
    auto evaluate = [&](const std::wstring& text) {
        CommandListExpression expression;
        if (!expression.parse(&text, &ini_namespace, &scope))
            throw std::runtime_error("candidate: parse resource expression " + std::string(text.begin(), text.end()));
        require_batch(!expression.static_evaluate(nullptr), "candidate: query is dynamic");
        return expression.evaluate(&device.state);
    };
    for (const wchar_t stage : {L'p', L'v', L'c', L'h', L'd', L'g'}) {
        set_slot(device, stage, 127, view.Get());
        const std::wstring target = std::wstring(1, stage) + L"s-t127";
        require_batch(evaluate(target + L"->tocandidate") == 1, "candidate: any shader stage/slot can be selected");
        set_slot(device, stage, 127, nullptr);
        require_batch(evaluate(target + L"->tocandidate") == 0, "candidate: unbound slot");
    }
    set_slot(device, L'p', 127, view.Get());
    require_batch(evaluate(L"ps-t127") == 0 && evaluate(L"ps-t127->tocandidate") == 1,
        "candidate: membership is independent of filter_index zero");
    const ULONG view_refs = view->AddRef(); view->Release();
    for (unsigned i = 0; i < 3; ++i) evaluate(L"ps-t127->tocandidate");
    const ULONG final_view_refs = view->AddRef(); view->Release();
    require_batch(view_refs == final_view_refs, "candidate: repeated queries balance native view references");
    require_batch(evaluate(L"ps-t[$candidate_slot]->tocandidate") == 1, "candidate: dynamic slot selection");
    command_list_globals.at(L"$candidate_slot").fval = 128;
    require_batch(evaluate(L"ps-t[$candidate_slot]->tocandidate") == 0, "candidate: invalid dynamic slot fails closed");
    for (auto field : {&DrawCallInfo::IndexCount, &DrawCallInfo::FirstIndex, &DrawCallInfo::VertexCount,
        &DrawCallInfo::FirstVertex, &DrawCallInfo::InstanceCount, &DrawCallInfo::FirstInstance}) {
        const UINT saved = info.*field;
        ++(info.*field);
        require_batch(evaluate(L"ps-t127->tocandidate") == 0, "candidate: all six draw restrictions apply");
        info.*field = saved;
    }
    device.state.post = true;
    set_slot(device, L'p', 127, nullptr);
    require_batch(evaluate(L"ps-t127->tocandidate") == 0, "candidate: post observes current bindings");
    set_slot(device, L'p', 127, view.Get());
    require_batch(evaluate(L"ps-t127->tocandidate") == 1, "candidate: post query can be independently true");
    device.state.post = false;
    ID3D11Buffer* vertex = buffer.Get();
    const UINT stride = 4, offset = 0;
    device.context->IASetVertexBuffers(7, 1, &vertex, &stride, &offset);
    device.context->IASetIndexBuffer(buffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    require_batch(evaluate(L"vb7->tocandidate || ps-t127->tocandidate") == 1 && evaluate(L"ib->tocandidate") == 1,
        "candidate: INI chooses buffer slots and combines conditions");
    Registry registry;
    registry.resource(L"resourcecandidatequery", device, texture.Get());
    require_batch(evaluate(L"resourcecandidatequery->width") == 8, "candidate: native custom-resource fixture");
    require_batch(evaluate(L"resourcecandidatequery->tocandidate") == 1, "candidate: explicit custom resource");
    require_batch(command_list_globals.at(L"$candidate_commands").fval == 0, "candidate: queries execute no pre/post TO command");
    set_hash(texture.Get(), 0x5a17c0df);
    require_batch(evaluate(L"ps-t127->tocandidate") == 0, "candidate: changed hash invalidates cached match");
    set_hash(texture.Get(), 0x5a17c0de);
    require_batch(evaluate(L"ps-t127->tocandidate") == 1, "candidate: returning hash recomputes match");

    const char* fuzzy =
        "global $candidate_commands=0\n[TextureOverrideCandidateFuzzy]\n"
        "match_type=Texture2D\nmatch_width=res_width\nmatch_height=8\nmatch_index_count=6\n"
        "$candidate_commands=$candidate_commands+1\n";
    config.load(fuzzy, 0, true);
    const auto resolution = G->mResolutionInfo;
    G->mResolutionInfo.width = 8;
    require_batch(evaluate(L"ps-t127->tocandidate") == 1, "candidate: fuzzy-only TO is not suppressed");
    G->mResolutionInfo.width = 16;
    require_batch(evaluate(L"ps-t127->tocandidate") == 0, "candidate: dynamic resolution mismatch");
    G->mResolutionInfo.width = 8;
    info.IndexCount = 7;
    require_batch(evaluate(L"ps-t127->tocandidate") == 0, "candidate: fuzzy draw restrictions");
    info.IndexCount = 6;
    require_batch(command_list_globals.at(L"$candidate_commands").fval == 0, "candidate: fuzzy query has no command side effects");
    G->mResolutionInfo = resolution;
    config.load("[TextureOverrideRegionCandidate]\nhash=12345678\nmatch_index_count=6\n", 0, true);
    require_batch(evaluate(L"ib->tocandidate") == 0, "candidate: whole-resource hash miss without region tracking");
    G->track_region_hashes = true;
    require_batch(evaluate(L"ib->tocandidate") == 1 && evaluate(L"vb7->tocandidate") == 1,
        "candidate: regional buffer checks conservatively need full matching");
    ResourceCopyTarget current_target;
    current_target.type = ResourceCopyTargetType::VERTEX_BUFFER;
    current_target.slot = 7;
    device.state.this_target = &current_target;
    require_batch(evaluate(L"this->tocandidate") == 1 && device.state.this_target == &current_target,
        "candidate: this preserves the selected buffer's regional semantics without changing it");
    device.state.this_target = nullptr;
    require_batch(evaluate(L"ps-t127->tocandidate") == 0,
        "candidate: region tracking does not broaden shader resource queries");
    config.load("", 0, true);
    require_batch(evaluate(L"ib->tocandidate") == 0 && evaluate(L"ps-t127->tocandidate") == 0,
        "candidate: reload removes retired exact/fuzzy candidates");
    config.load("[ShaderOverrideNoCandidateCall]\nhash=0000000000000000\nhandling=skip\n"
        "[TextureOverrideCandidateSpy]\nhash=5a17c0de\n"
        "[CommandListUnusedCandidate]\nif ps-t127->tocandidate\nx=1\nendif\n", 0, true);
    {
        CandidateBindingSpy spy(device.context.Get());
        device.wrapper->DrawIndexed(6, 0, 0);
        require_batch(spy.ps_calls == 0 && spy.vb_calls == 0 && spy.ib_calls == 0,
            "candidate: an unused INI query causes no binding scan at draw start");
        require_batch(evaluate(L"ps-t127->tocandidate") == 1 && spy.ps_calls == 1 &&
            spy.ps_first == 127 && spy.ps_count == 1 && spy.vb_calls == 0 && spy.ib_calls == 0,
            "candidate: an evaluated query reads only its specified target");
    }
    for (const auto& test : candidate_routing_cases) {
        const std::string text = std::string("global $compat_enabled=") + (test.enabled ? "1" : "0") +
            "\nglobal $compat_pre=0\nglobal $compat_post=0\n"
            "[ShaderOverrideCandidateCompatibility]\nhash=0000000000000000\nrun=CommandListCandidateCompatibility\n"
            "[CommandListCandidateCompatibility]\nif $compat_enabled\nif " +
            (test.query ? "ps-t127->tocandidate" : "1") +
            "\nchecktextureoverride=ps-t127\nendif\nendif\nhandling=skip\n"
            "[TextureOverrideCandidateCompatibility]\nhash=5a17c0de\n"
            "$compat_pre=$compat_pre+1\npost $compat_post=$compat_post+1\n" +
            (test.unused_query ? "[CommandListUnusedCompatibilityQuery]\nif ib->tocandidate\nx=1\nendif\n" : "");
        config.load(text.c_str(), 0, true);
        set_hash(texture.Get(), test.match ? 0x5a17c0de : 0x5a17c0df);
        set_slot(device, L'p', 127, view.Get());
        Profiling::mode = Profiling::Mode::SUMMARY;
        Profiling::texture_override_candidates_lookup_overhead.clear();
        CandidateBindingSpy spy(device.context.Get());
        device.wrapper->DrawIndexed(6, 0, 0);
        const unsigned full_checks = test.enabled && (!test.query || test.match) ? 4 : 0;
        const unsigned candidate_queries = test.enabled && test.query ? 4 : 0;
        const unsigned phase_commands = test.enabled && test.match ? 2 : 0;
        require_batch(spy.ps_calls == full_checks + candidate_queries && spy.vb_calls == 0 && spy.ib_calls == 0,
            "candidate compatibility: only the configured route issues native queries");
        require_batch(Profiling::texture_override_candidates_lookup_overhead.count == full_checks,
            "candidate compatibility: new misses skip the old full lookup; legacy has no extra lookup");
        require_batch(command_list_globals.at(L"$compat_pre").fval == phase_commands &&
            command_list_globals.at(L"$compat_post").fval == phase_commands,
            "candidate compatibility: each shader-stage entry executes TO pre/post commands once");
        const unsigned observed_checks = Profiling::texture_override_candidates_lookup_overhead.count;
        std::printf("TO_COMPATIBILITY={\"case\":\"%s\",\"nativePSReads\":%u,\"candidateQueries\":%u,\"fullChecks\":%u,\"preCommands\":%u,\"postCommands\":%u}\n",
            test.name, spy.ps_calls, spy.ps_calls - observed_checks, observed_checks,
            static_cast<unsigned>(command_list_globals.at(L"$compat_pre").fval),
            static_cast<unsigned>(command_list_globals.at(L"$compat_post").fval));
    }
    config.load("", 0, true);
    Profiling::mode = Profiling::Mode::NONE;
    clear_command_list_profiling();
}

void test_dx11_draw_override_gate()
{
    using namespace dx11_slot_test;
    AuditConfigScope config;
    BatchDevice device;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 8;
    desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    require_batch(SUCCEEDED(device.device->CreateTexture2D(&desc, nullptr, &texture)), "draw guard: tracked texture");
    auto view = srv(device, texture.Get());
    const char* body =
        "global $gate_pre=0\nglobal $gate_post=0\nglobal $gate_cleanup=0\n"
        "[ShaderOverrideDrawGate]\nhash=0000000000000000\nrun=CommandListDrawGate\nhandling=skip\n"
        "[CommandListDrawGate]\nif draw_has_override\n"
        "pre $gate_pre=$gate_pre+1\npre ps-t9=null\npost $gate_post=$gate_post+1\nendif\n"
        "post $gate_cleanup=$gate_cleanup+1\n"
        "[TextureOverrideDrawGateFuzzy]\nmatch_type=Texture2D\nmatch_width=8\n";
    const auto value = [](const wchar_t* name) { return command_list_globals.at(name).fval; };
    const char* ranges = "[Rendering]\ndraw_has_override_slots=vb[6:8] ps-t[8:10]\n";
    config.load(body, 0, true, ranges);
    set_slot(device, L'p', 9, view.Get());
    set_slot(device, L'p', 127, view.Get());
    {
        CandidateBindingSpy spy(device.context.Get());
        device.wrapper->DrawIndexed(6, 0, 0);
        require_batch(spy.vb_calls == 1 && spy.vb_first == 6 && spy.vb_count == 3 &&
            spy.ps_calls == 1 && spy.ps_first == 8 && spy.ps_count == 3 && spy.ib_calls == 0,
            "draw guard: one getter per configured range across both shader entries and phases");
        require_batch(value(L"$gate_pre") == 2 && value(L"$gate_post") == 2 && value(L"$gate_cleanup") == 2,
            "draw guard: fuzzy match and pre/post reuse survive pre unbinding the only candidate");
        device.wrapper->DrawIndexed(6, 0, 0);
        require_batch(spy.vb_calls == 2 && spy.ps_calls == 2 && value(L"$gate_pre") == 2 &&
            value(L"$gate_post") == 2 && value(L"$gate_cleanup") == 4,
            "draw guard: fresh draw rejects the unconfigured slot and still runs post cleanup");
    }
    for (const char* settings : {"[Rendering]\ndraw_has_override_slots=vb32\n", ""}) {
        config.load(body, 0, true, settings);
        CandidateBindingSpy spy(device.context.Get());
        device.wrapper->DrawIndexed(6, 0, 0);
        require_batch(spy.vb_calls == 0 && spy.ps_calls == 0 && spy.ib_calls == 0 &&
            value(L"$gate_pre") == 2 && value(L"$gate_post") == 2,
            "draw guard: missing or rejected configuration falls back to direct checks after reload");
    }
    config.load("[ShaderOverrideNoGate]\nhash=0000000000000000\nhandling=skip\n", 0, true, ranges);
    {
        CandidateBindingSpy spy(device.context.Get());
        device.wrapper->DrawIndexed(6, 0, 0);
        require_batch(spy.vb_calls == 0 && spy.ps_calls == 0 && spy.ib_calls == 0,
            "draw guard: configured but unused predicate does not read bindings");
    }
    const std::string vertexBody = std::string(body) +
        "[TextureOverrideDrawGateVertex]\nhash=6a0d36f2\n";
    config.load(vertexBody.c_str(), 0, true, "[Rendering]\ndraw_has_override_slots=vb[6:8]\n");
    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = 256;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    ComPtr<ID3D11Buffer> vertexBuffers[3];
    ID3D11Buffer* bound[] = {nullptr, nullptr, nullptr};
    for (unsigned slot = 0; slot < ARRAYSIZE(bound); ++slot) {
        require_batch(SUCCEEDED(device.device->CreateBuffer(&bufferDesc, nullptr, &vertexBuffers[slot])),
            "draw guard: create tracked vertex buffer");
        bound[slot] = vertexBuffers[slot].Get();
    }
    const UINT strides[] = {4, 4, 4}, offsets[] = {0, 0, 0};
    {
        CriticalSectionLockGuard lock(&G->mResourcesLock, __FUNCTION__, __LINE__);
        for (auto* buffer : bound)
            G->mResources.at(buffer).hash = 0xdeadc0de;
    }
    device.context->IASetVertexBuffers(6, ARRAYSIZE(bound), bound, strides, offsets);
    const auto cachedMiss = [&] {
        CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
        return VertexBufferRangeHasNoCachedOverrideCandidates(bound, ARRAYSIZE(bound));
    };
    require_batch(!cachedMiss(), "draw guard: a cold vertex range uses the original lookup");
    device.wrapper->DrawIndexed(6, 0, 0);
    require_batch(cachedMiss(), "draw guard: warm negative vertex range can bypass per-buffer checks");
    device.wrapper->DrawIndexed(6, 0, 0);
    require_batch(value(L"$gate_pre") == 0 && value(L"$gate_post") == 0 && value(L"$gate_cleanup") == 4,
        "draw guard: warm vertex misses preserve the original draw and cleanup decisions");
    {
        CriticalSectionLockGuard lock(&G->mResourcesLock, __FUNCTION__, __LINE__);
        G->mResources.at(bound[2]).hash = 0x6a0d36f2;
    }
    require_batch(!cachedMiss(), "draw guard: changed hash on the same buffer invalidates the fast miss");
    device.wrapper->DrawIndexed(6, 0, 0);
    require_batch(value(L"$gate_pre") == 2 && value(L"$gate_post") == 2 && value(L"$gate_cleanup") == 6,
        "draw guard: a new match in the final vertex slot reaches the original pre and post checks");
    config.load("", 0, true);
    clear_command_list_profiling();
}

void test_dx11_to_candidate_performance()
{
    using namespace dx11_slot_test;
    AuditConfigScope config;
    BatchDevice device;
    D3D11_BUFFER_DESC bytes{};
    bytes.ByteWidth = 256;
    bytes.BindFlags = D3D11_BIND_INDEX_BUFFER | D3D11_BIND_VERTEX_BUFFER;
    ComPtr<ID3D11Buffer> buffer;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&bytes, nullptr, &buffer)), "candidate perf: real tracked buffer");
    ComPtr<ID3D11Buffer> miss_buffer;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&bytes, nullptr, &miss_buffer)), "candidate perf: nonmatching tracked buffer");
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 8;
    desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    require_batch(SUCCEEDED(device.device->CreateTexture2D(&desc, nullptr, &texture)), "candidate perf: real tracked texture");
    auto view = srv(device, texture.Get());
    {
        CriticalSectionLockGuard lock(&G->mResourcesLock, __FUNCTION__, __LINE__);
        G->mResources.at(buffer.Get()).hash = 0x9040e2c4;
        G->mResources.at(miss_buffer.Get()).hash = 0xdeadc0de;
        G->mResources.at(texture.Get()).hash = 0x5a17c0de;
    }
    const UINT strides[] = {4, 4, 4}, offsets[] = {0, 0, 0};
    set_slot(device, L'p', 0, view.Get());
    const char* names[] = {"direct", "existing_filter", "candidate_one", "candidate_three", "candidate_all", "draw_gate"};
    const char* conditions[] = {"1", "ib", "ib->tocandidate",
        "ib->tocandidate || vb0->tocandidate || ps-t0->tocandidate",
        "ib->tocandidate || vb0->tocandidate || vb1->tocandidate || vb2->tocandidate || ps-t0->tocandidate",
        "draw_has_override"};
    const unsigned policies = static_cast<unsigned>(ARRAYSIZE(names));
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    for (const auto& workload : candidate_workloads) {
        const unsigned hits = workload.matches;
        for (bool summary : {false, true}) {
            for (unsigned sample = 0; sample < 5; ++sample) {
                for (unsigned order = 0; order < policies; ++order) {
                    const unsigned variant = sample % 2 ? policies - 1 - order : order;
                    const std::string text =
                        "global $candidate_hits=0\nglobal $candidate_posts=0\n"
                        "[ShaderOverrideCandidatePerf]\nhash=0000000000000000\nrun=CommandListCandidatePerf\n"
                        "[CommandListCandidatePerf]\nif " + std::string(conditions[variant]) + "\n"
                        "checktextureoverride=ib\nchecktextureoverride=vb0\nchecktextureoverride=vb1\n"
                        "checktextureoverride=vb2\nchecktextureoverride=ps-t0\nendif\nhandling=skip\n" +
                        (!workload.has_rules ? std::string() :
                        "[TextureOverrideCandidatePerf]\nhash=9040e2c4\nmatch_first_index=0\nmatch_index_count=6\n"
                        "$candidate_hits=$candidate_hits+1\npost $candidate_posts=$candidate_posts+1\n") +
                        (workload.fuzzy ? "[TextureOverrideCandidatePerfFuzzy]\nmatch_type=Texture2D\nmatch_width=16\n" : "");
                    config.load(text.c_str(), 0, true, variant == policies - 1 ?
                        "[Rendering]\ndraw_has_override_slots=ib vb[0:2] ps-t0\n" : "");
                    Profiling::mode = summary ? Profiling::Mode::SUMMARY : Profiling::Mode::NONE;
                    auto draw = [&](unsigned i) {
                        ID3D11Buffer* current = workload.range_miss || i < hits || !workload.has_rules ? buffer.Get() : miss_buffer.Get();
                        device.context->IASetIndexBuffer(current, DXGI_FORMAT_R16_UINT, 0);
                        ID3D11Buffer* buffers[] = {current, miss_buffer.Get(), miss_buffer.Get()};
                        device.context->IASetVertexBuffers(0, 3, buffers, strides, offsets);
                        device.wrapper->DrawIndexed(6, workload.range_miss ? 6 : 0, 0);
                    };
                    for (unsigned i = 0; i < 128; ++i) draw(i);
                    command_list_globals.at(L"$candidate_hits").fval = 0;
                    command_list_globals.at(L"$candidate_posts").fval = 0;
                    Profiling::texture_override_candidates_lookup_overhead.clear();
                    LARGE_INTEGER begin{}, end{};
                    QueryPerformanceCounter(&begin);
                    for (unsigned i = 0; i < 2000; ++i)
                        draw(i);
                    QueryPerformanceCounter(&end);
                    require_batch(command_list_globals.at(L"$candidate_hits").fval == hits * 4 &&
                        command_list_globals.at(L"$candidate_posts").fval == hits * 4,
                        "candidate perf: all policies preserve identical TO command execution");
                    std::printf("TO_CANDIDATE_PERF={\"scenario\":\"%s\",\"matchedDraws\":%u,\"policy\":\"%s\",\"summary\":%s,\"sample\":%u,\"draws\":2000,\"nsPerDraw\":%.3f,\"fullLookups\":%u}\n",
                        workload.name, hits, names[variant], summary ? "true" : "false", sample,
                        double(end.QuadPart - begin.QuadPart) * 1e9 / frequency.QuadPart / 2000,
                        Profiling::texture_override_candidates_lookup_overhead.count);
                }
            }
        }
    }
    config.load("", 0, true);
    Profiling::mode = Profiling::Mode::NONE;
    clear_command_list_profiling();
}

void test_dx11_to_candidate_cost()
{
    using namespace dx11_slot_test;
    AuditConfigScope config;
    BatchDevice device;
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = 256;
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ComPtr<ID3D11Buffer> buffer;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&desc, nullptr, &buffer)), "candidate cost: tracked native IB");
    device.context->IASetIndexBuffer(buffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    DrawCallInfo info(DrawCall::DrawIndexed, 0, 6, 1, 0, 0, 0, nullptr, 0);
    device.state.call_info = &info;
    ResourceCopyTarget target;
    target.type = ResourceCopyTargetType::INDEX_BUFFER;
    CheckTextureOverrideCommand check;
    check.target = target;
    CommandListScope scope;
    const std::wstring ini_namespace, expression_text = L"ib->tocandidate";
    CommandListExpression expression;
    require_batch(expression.parse(&expression_text, &ini_namespace, &scope), "candidate cost: real resource expression");
    const char* scenarios[] = {"hash_miss", "draw_miss", "fuzzy_miss", "hash_hit", "fuzzy_hit"};
    for (unsigned scenario = 0; scenario < ARRAYSIZE(scenarios); ++scenario) {
        const std::string text = "[TextureOverrideCandidateCostExact]\nhash=9040e2c4\nmatch_first_index=0\n" +
            std::string(scenario == 2 ? "[TextureOverrideCandidateCostFuzzy]\nmatch_type=Texture2D\nmatch_width=16\n" :
                scenario == 4 ? "[TextureOverrideCandidateCostFuzzy]\nmatch_type=Buffer\nmatch_byte_width=256\n" : "");
        config.load(text.c_str(), 0, true);
        Profiling::mode = Profiling::Mode::NONE;
        info.FirstIndex = scenario == 1 ? 6 : 0;
        {
            CriticalSectionLockGuard lock(&G->mResourcesLock, __FUNCTION__, __LINE__);
            G->mResources.at(buffer.Get()).hash = scenario == 1 || scenario == 3 ? 0x9040e2c4 : 0xdeadc0de;
        }
        const bool expected = scenario == 3 || scenario == 4;
        require_batch(target.GetResourceTOCandidate(&device.state) == expected,
            "candidate cost: preserve exact, fuzzy and draw-restriction results");
        for (unsigned sample = 0; sample < 5; ++sample) {
            measure_candidate_slice(scenarios[scenario], "global_lock", sample, [&] {
                CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
                return 1u;
            });
            measure_candidate_slice(scenarios[scenario], "binding", sample, [&] {
                ComPtr<ID3D11Buffer> bound;
                device.context->IAGetIndexBuffer(bound.GetAddressOf(), nullptr, nullptr);
                return unsigned(bound != nullptr);
            });
            {
                // Only this slice excludes the caller's global lock and native
                // binding fetch; the helper's resource-table work is included.
                CriticalSectionLockGuard lock(&G->mCriticalSection, __FUNCTION__, __LINE__);
                measure_candidate_slice(scenarios[scenario], "cached_match", sample, [&] {
                    return unsigned(HasTextureOverrideCandidate(buffer.Get(), &info, false));
                });
            }
            measure_candidate_slice(scenarios[scenario], "getter", sample, [&] {
                return unsigned(target.GetResourceTOCandidate(&device.state));
            });
            measure_candidate_slice(scenarios[scenario], "expression", sample, [&] {
                return unsigned(expression.evaluate(&device.state));
            });
            measure_candidate_slice(scenarios[scenario], "direct_check", sample, [&] {
                check.run(&device.state);
                return 1u;
            });
        }
    }
    config.load("", 0, true);
    clear_command_list_profiling();
}


struct AuditCase { const char* id; void (*run)(); };
const AuditCase audit_cases[] = {{"F-003", test_dx11_file_publication_regression},
    {"F-003b", test_persist_write_failure_regression},
    {"F-163", test_dx11_loader_file_bounds_regression},
    {"F-005", test_dx11_device_initialization_regression},
    {"F-007", test_f007},
    {"F-008", test_dx11_vertex_dump_bounds_regression},
    {"F-324", test_dx11_appended_vertex_layout_regression},
    {"F-100", test_dx11_decompiler_literal_regression},
    {"F-281", test_dx11_decompiler_negative_literal_regression},
    {"F-278", test_dx11_decompiler_next_line_regression},
    {"F-278B", test_dx11_decompiler_next_line_bounded_regression},
    {"F-273", test_dx11_decompiler_skip_packing_regression},
    {"F-277", test_dx11_decompiler_f277_diagnostic_bounds_regression},
    {"F-274", test_dx11_decompiler_output_signature_regression},
    {"F-275", test_dx11_decompiler_special_output_regression},
    {"F-276", test_dx11_dx9_binding_lifetime_regression},
    {"F-279", test_dx11_decompiler_suffix_regression},
    {"F-326", test_dx11_decompiler_swizzle_slot_bound_regression},
    {"F-280", test_dx11_decompiler_texture_literal_regression},
    {"F-282", test_dx11_decompiler_immediate_regression},
    {"F-103-RED-R4", test_dx11_decompiler_f103_red_r4},
    {"F-103-RED-R5", test_dx11_decompiler_f103_red_r5},
    {"F-103-RED-M3", test_dx11_decompiler_f103_red_m3},
#ifdef DX11_AUDIT_ASAN_DECOMPILER
    {"F-103-RED-R1", test_dx11_decompiler_f103_red_r1},
    {"F-103-RED-R2", test_dx11_decompiler_f103_red_r2},
    {"F-103-RED-R3", test_dx11_decompiler_f103_red_r3},
    {"F-103-RED-M1", test_dx11_decompiler_f103_red_m1},
    {"F-103-RED-M2", test_dx11_decompiler_f103_red_m2},
#endif
    {"F-296", test_dx11_shader_regex_declaration_regression},
    {"F-297", test_dx11_shader_regex_temp_count_regression},
    {"P-02", test_dx11_tokenizer_regression},
    {"F-283", test_dx11_decompiler_lifetime_regression},
    {"F-104", test_dx11_decode_dxbc_bounds_regression},
    {"F-101", test_dx11_decompiler_decode_bounds_regression},
    {"F-128", test_dx11_reflect_bounds_regression},
    {"F-108", test_dx11_resource_filename_regression},
    {"F-110", test_dx11_pool_access_regression},
    {"F-111", test_dx11_decompile_file_diagnostic_regression},
    {"F-112", test_dx11_zbuffer_view_ownership_regression},
    {"F-137", test_f137},
    {"F-138", test_f138},
    {"F-143", test_f143},
    {"F-142", test_f142},
    {"F-145", test_f145_review},
    {"F-146", test_f146},
    {"F-148", test_dx11_shader_tool_read_regression},
    {"F-149", test_dx11_shader_tool_write_regression},
    {"F-155", test_dx11_automatic_draw_count_regression},
    {"F-169", test_f169},
    {"F-170", test_dx11_shader_hash_miss_regression},
    {"F-171", test_dx11_shader_header_file_regression},
    {"F-175", test_dx11_resource_override_validation_regression},
    {"F-176", test_f176},
    {"F-186", test_f186},
    {"F-168", test_dx11_f168_seen_rts_regression},
    {"F-168-R1", test_dx11_f168_r1_stale_key},
    {"F-168-R2", test_dx11_f168_r2_hold_second_frame},
    {"F-200", test_f200},
    {"F-200b", test_f200b},
    {"F-200c", test_f200c},
    {"F-194", test_dx11_ini_upload_failure_regression},
    {"F-199", test_f199_review},
    {"F-201", test_dx11_shader_model_bounds_regression},
    {"F-203", test_dx11_initialization_gate_reliability_regression},
    {"F-900", test_dx11_version_regression},
    {"F-292", test_dx11_debug_layer_interfaces_regression},
    {"F-196", test_dx11_legacy_profiling_regression},
    {"INDEXED-INDIRECT-DUMP", test_indexed_indirect_args_dump},
    {"F-001", test_f001}, {"F-105", test_dx11_disassembler_header_bounds_regression},
    {"F-106", test_f106}, {"F-114", test_f114},
    {"F-127", test_dx11_dx9_constant_reset_regression},
    {"F-124", test_dx11_customdata_length_regression},
    {"F-125", test_dx11_dx9_register_bounds_regression},
    {"F-126", test_dx11_dx9_dcl_const_without_ctab_regression},
    {"F-135", test_f135},
    {"F-140", test_dx11_assembler_double_index_regression},
    {"F-144", test_dx11_assembler_interpolation_regression},
    {"F-150", test_dx11_signature_register_eof_regression},
    {"F-151", test_dx11_signature_feature_comment_regression},
    {"F-153", test_dx11_shader_replacement_ownership_regression},
    {"F-167", test_dx11_finish_failure_regression},
    {"F-172", test_dx11_shader_diagnostic_failure_regression},
    {"F-177", test_dx11_texture_initial_debug_regression},
    {"F-136", test_dx11_texture_hash_layout_regression},
    {"F-181", test_dx11_hook_lookup_regression},
    {"F-002", test_dx11_hook_release_regression},
    {"F-182", test_dx11_hook_install_regression},
    {"F-109", test_dx11_initial_data_ownership_regression},
    {"F-197", test_dx11_f197_override_lifetime_regression},
    {"F-193", test_f193},
    {"F-195", test_f195},
    {"F-195-EXCLUDE", test_f195_exclude_lost_window},
    {"F-195-CONCURRENCY", test_f195_concurrent_guard},
    {"F-272", test_dx11_decompiler_swizzle_count_regression},
    {"F-179", test_dx11_empty_hunting_present_regression},
    {"F-113", test_dx11_f113_swap_chain_fallback_regression},
    {"F-113-RC", test_dx11_f113_rc_regression},
    {"F-113-RA", test_dx11_f113_ra_regression},
    {"F-006", test_dx11_swapchain_destructor_protocol_regression},
    {"F-184", test_dx11_reload_shader_filename_regression},
    {"F-185", test_dx11_ini_invalid_utf8_regression},
    {"F-187", test_dx11_masked_flags_regression},
    {"F-286", test_dx11_crash_help_regression},
    {"F-285", test_dx11_dx9_sampler_dimension_regression},
    {"F-294", test_dx11_ini_enumeration_regression},
    {"F-295", test_dx11_stencil_diagnostic_regression},
    {"F-298", test_dx11_view_reference_null_diagnostic_regression},
    {"F-293", test_dx11_hunting_shader_selection_regression},
    {"P-01", test_dx11_ini_lookup_performance},
    {"P-AUTO-TEX", test_dx11_auto_texture_check_regression},
    {"P-SHADER-STAGE", test_dx11_draw_dispatch_regression},
    {"P-TO-CANDIDATE", test_dx11_to_candidate_regression},
    {"P-TO-DRAW-GATE", test_dx11_draw_override_gate},
    {"P-TO-CANDIDATE-PERF", test_dx11_to_candidate_performance},
    {"P-TO-CANDIDATE-COST", test_dx11_to_candidate_cost},
#ifdef DX11_AUDIT_ASAN_LOCKS
    {"F-115", test_dx11_lock_cleanup_regression},
    {"F-190", test_dx11_lock_name_concurrency_regression},
#endif
};
}

void with_dx11_audit_hunting_configuration(void (*scenario)())
{
    AuditConfigScope config;
    // Use the supported eager-initialization option and the actual loader;
    // do not fake the gConfigInitialized state used by Present.
    config.load("", HUNTING_MODE_ENABLED, true);
    require_batch(G->gConfigInitialized && G->hunting == HUNTING_MODE_ENABLED,
        "hunting audit: real eager configuration initialization completed");
    scenario();
}

bool test_dx11_audit_item(const char* id)
{
    // F-168 S1 is a probabilistic stress (pre-fix it may deliberately crash);
    // keep it out of --full and require an explicit --audit-item F-168-S1.
    if (!std::strcmp(id, "F-168-S1")) { test_dx11_f168_s1_seen_rts_stress(); return true; }
    if (!std::strcmp(id, "P-TO-CACHE")) { test_dx11_texture_override_cache_regression(); return true; }
    for (const auto& test : audit_cases) {
        if (!std::strcmp(test.id, id)) { test.run(); return true; }
    }
    return false;
}

void test_dx11_audit_regressions()
{
    for (const auto& test : audit_cases) {
        test.run();
        std::printf("PASS audit item: %s\n", test.id);
    }
}

void test_dx11_vertex_dump_gpu_regression()
{
    using namespace dx11_slot_test;
    AuditDumpScope output; BatchDevice device;
    auto original = std::move(device.wrapper);
    auto analysis = std::make_unique<FrameAnalysisContext>(device.native.Get(), device.context.Get());
    analysis->SetHackerDevice(device.device);
    auto* context = analysis.get(); device.wrapper = std::move(analysis);
    device.device->SetHackerContext(context); device.state.mHackerContext = context;
    try {
        const char source[] = "float4 main(float4 p:DATA):SV_Position{return p;}";
        ComPtr<ID3DBlob> code, errors;
        require_batch(SUCCEEDED(D3DCompile(source, sizeof(source)-1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, &errors)),
            "F008: compile actual input signature");
        ComPtr<ID3D11VertexShader> shader;
        require_batch(SUCCEEDED(device.native->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader)),
            "F008: native shader for actual public frame analysis");
        D3D11_INPUT_ELEMENT_DESC desc{"DATA",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        ComPtr<ID3D11InputLayout> layout;
        require_batch(SUCCEEDED(device.device->CreateInputLayout(&desc, 1, code->GetBufferPointer(), code->GetBufferSize(), &layout)),
            "F008: real cached production input layout");
        auto vertices = buffer(device, 32, D3D11_BIND_VERTEX_BUFFER);
        const float data[] = {1,2,3,4,5,6,7,8};
        device.context->UpdateSubresource(vertices.Get(), 0, nullptr, data, 0, 0);
        ID3D11Buffer* bound = vertices.Get(); UINT stride = 4, offset = 0;
        context->IASetInputLayout(layout.Get()); context->IASetVertexBuffers(0, 1, &bound, &stride, &offset);
        context->VSSetShader(shader.Get(), nullptr, 0); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        const auto options = static_cast<FrameAnalysisOptions>(static_cast<unsigned>(FrameAnalysisOptions::DUMP_VB) |
            static_cast<unsigned>(FrameAnalysisOptions::FMT_BUF_TXT) | static_cast<unsigned>(FrameAnalysisOptions::MONO));
        context->FrameAnalysisTrigger(options); context->Draw(8, 0);
        require_batch(output.payload_contains("DATA: 1, 2, 3, 4") && output.payload_contains("DATA: 5, 6, 7, 8") &&
            output.payload_contains("exceeds mapped buffer"), "F008: actual WARP buffer staging and public dump preserve complete fields and diagnose the tail");
    } catch (...) {
        device.wrapper = std::move(original); device.device->SetHackerContext(device.wrapper.get()); device.state.mHackerContext = device.wrapper.get();
        throw;
    }
    device.wrapper = std::move(original); device.device->SetHackerContext(device.wrapper.get()); device.state.mHackerContext = device.wrapper.get();
}

void test_dx11_decompiler_public_literal_regression()
{
    using namespace dx11_slot_test;
    const char source[] = "float4 main():SV_Target{return float4(0.25,0.5,0.75,1);}";
    ComPtr<ID3DBlob> code, errors, assembly;
    require_batch(SUCCEEDED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),
        "F100: native compiler produces a real literal shader");
    require_batch(SUCCEEDED(D3DDisassemble(code->GetBufferPointer(),code->GetBufferSize(),0,nullptr,&assembly)),
        "F100: actual native assembly input");
    std::string text(static_cast<const char*>(assembly->GetBufferPointer()));
    DecompilerSettings settings;
    ParseParameters params{}; params.bytecode = code->GetBufferPointer(); params.bytecodeSize = code->GetBufferSize(); params.decompiled = text.c_str();
    params.decompiledSize = text.size(); params.G = &settings;
    bool patched = false, failed = false; std::string model;
    const auto hlsl = DecompileBinaryHLSL(params,patched,model,failed);
    require_batch(!failed && !hlsl.empty(), "F100: real public decompiler preserves a normal four-component shader");
    ComPtr<ID3DBlob> rebuilt;
    require_batch(SUCCEEDED(D3DCompile(hlsl.data(),hlsl.size(),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&rebuilt,&errors)),
        "F100: regenerated HLSL compiles with the actual compiler");
    BatchDevice device; ComPtr<ID3D11PixelShader> original, replacement;
    require_batch(SUCCEEDED(device.native->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&original)) &&
        SUCCEEDED(device.native->CreatePixelShader(rebuilt->GetBufferPointer(),rebuilt->GetBufferSize(),nullptr,&replacement)),
        "F100: native device accepts original and decompiled shaders");
    const auto expected = render_dx11_audit_constant_pixel(device, original.Get());
    require_batch(expected == std::array<unsigned char,4>{64,128,191,255} &&
        render_dx11_audit_constant_pixel(device,replacement.Get()) == expected, "F100: actual rendered literal values remain unchanged");
    const auto begin = text.find("l(");
    require_batch(begin != std::string::npos, "F100: real disassembly contains the fixture literal");
    const auto end = text.find(')',begin);
    require_batch(end != std::string::npos, "F100: fixture has a complete literal");
    text.replace(begin,end-begin+1,"l(1,2)");
    params.decompiled = text.c_str(); params.decompiledSize = text.size();
    failed = false; patched = false;
    (void)DecompileBinaryHLSL(params,patched,model,failed);
    require_batch(failed, "F100: unavailable components propagate the existing public error flag used by native shader fallback");
}

#include "ibfe_swizzle_audit.inl"

void test_dx11_decompiler_public_signature_regression()
{
    using namespace dx11_slot_test;
    BatchDevice device;
    const char* sources[] = {
        "float4 main():SV_Target{return float4(0.25,0.5,0.75,1);}",
        "void main(){}",
        "void main(float4 p:TEXCOORD){clip(p.x);}",
        "float main():SV_Depth{return 0.5;}",
        "struct O{float4 c:SV_Target;float d:SV_Depth;}; O main(){O o;o.c=float4(0.25,0.5,0.75,1);o.d=0.5;return o;}"
    };
    for (size_t index=0; index<std::size(sources); ++index) {
        ComPtr<ID3DBlob> code, errors, assembly;
        require_batch(SUCCEEDED(D3DCompile(sources[index],std::strlen(sources[index]),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),
            "F274: native compiler accepts each normal, empty, input-only and special-semantic fixture");
        ComPtr<ID3D11PixelShader> original;
        require_batch(SUCCEEDED(device.native->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&original)),
            "F274: each original fixture is a valid native pixel shader");
        require_batch(SUCCEEDED(D3DDisassemble(code->GetBufferPointer(),code->GetBufferSize(),0,nullptr,&assembly)),
            "F274: real native disassembly feeds the public decompiler");
        std::string text(static_cast<const char*>(assembly->GetBufferPointer()));
        DecompilerSettings settings;
        ParseParameters params{}; params.bytecode=code->GetBufferPointer(); params.bytecodeSize=code->GetBufferSize(); params.decompiled=text.c_str();
        params.decompiledSize=text.size(); params.G=&settings;
        bool patched=false, failed=false; std::string model;
        const auto hlsl=DecompileBinaryHLSL(params,patched,model,failed);
        require_batch(!failed && !hlsl.empty(), "F274: public decompiler preserves valid zero-output and special-semantic shaders");
        ComPtr<ID3DBlob> rebuilt;
        const auto compiled=D3DCompile(hlsl.data(),hlsl.size(),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&rebuilt,&errors);
        if (FAILED(compiled)) {
            std::fprintf(stderr,"F274 native fixture %zu assembly:\n%s\nHLSL:\n%s\nCompiler:\n%s\n",index,text.c_str(),hlsl.c_str(),
                errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no diagnostics");
        }
        require_batch(SUCCEEDED(compiled), "F274: regenerated complete signature recompiles with the native compiler");
        ComPtr<ID3D11PixelShader> replacement;
        require_batch(SUCCEEDED(device.native->CreatePixelShader(rebuilt->GetBufferPointer(),rebuilt->GetBufferSize(),nullptr,&replacement)),
            "F274: native WARP accepts the regenerated shader");
        if (index==0) {
            const auto expected=render_dx11_audit_constant_pixel(device,original.Get());
            require_batch(expected==std::array<unsigned char,4>{64,128,191,255} &&
                render_dx11_audit_constant_pixel(device,replacement.Get())==expected,
                "F274: normal output rendering keeps all component values");
            const auto output=text.find("// Output signature:");
            const auto row=text.find("// SV_Target",output);
            require_batch(output!=std::string::npos && row!=std::string::npos, "F274: native normal output row is available for fault injection");
            const auto row_end=text.find('\n',row);
            require_batch(row_end!=std::string::npos, "F274: native normal output row is complete");
            for (const auto& malformed : {text.substr(0,row)+"// unrecognized\n"+text.substr(row_end+1),
                    text.substr(0,row_end+1)+"// unrecognized\n"+text.substr(row_end+1),
                    text.substr(0,output)+"// unknown output section:"+text.substr(output+std::strlen("// Output signature:"))}) {
                params.decompiled=malformed.c_str(); params.decompiledSize=malformed.size(); failed=false;
                const auto rejected=DecompileBinaryHLSL(params,patched,model,failed);
                require_batch(failed && rejected.empty(),
                    "F274: malformed output metadata sets the public error flag and returns no partial HLSL to either consumer");
            }
        }
        if (index==3) {
            const auto row=text.find("// SV_Depth");
            require_batch(row!=std::string::npos, "F274: real special-semantic row is available for fault injection");
            const auto row_end=text.find('\n',row);
            require_batch(row_end!=std::string::npos, "F274: native special-semantic row is complete");
            for (const auto* bad_row : {"// SV_Depth 0 N/A oDepth\n", "// SV_Depth 0 N/A oDepth DEPTH unrecognized YES\n"}) {
                const auto malformed=text.substr(0,row)+bad_row+text.substr(row_end+1);
                params.decompiled=malformed.c_str(); params.decompiledSize=malformed.size(); failed=false;
                require_batch(DecompileBinaryHLSL(params,patched,model,failed).empty() && failed,
                    "F274: special-semantic parse failures cannot publish partial HLSL");
            }
        }
    }
    const char legacy_source[]="float4 tint : register(c3); float4 main():COLOR0 { return tint; }";
    ComPtr<ID3DBlob> legacy_code,legacy_errors,legacy_assembly;
    require_batch(SUCCEEDED(D3DCompile(legacy_source,sizeof(legacy_source)-1,nullptr,nullptr,nullptr,"main","ps_3_0",0,0,&legacy_code,&legacy_errors)) &&
        SUCCEEDED(D3DDisassemble(legacy_code->GetBufferPointer(),legacy_code->GetBufferSize(),0,nullptr,&legacy_assembly)),
        "F274: native compiler and disassembler produce an unmodified legacy SM3 fixture");
    const std::string legacy_text(static_cast<const char*>(legacy_assembly->GetBufferPointer()));
    require_batch(legacy_text.find("// Output signature:")==std::string::npos,
        "F274: real native SM3 metadata has no DXBC output signature table");
    DecompilerSettings legacy_settings;
    ParseParameters legacy_params{}; legacy_params.bytecode=legacy_code->GetBufferPointer(); legacy_params.bytecodeSize=legacy_code->GetBufferSize(); legacy_params.decompiled=legacy_text.c_str();
    legacy_params.decompiledSize=legacy_text.size(); legacy_params.G=&legacy_settings;
    bool legacy_patched=false,legacy_failed=false; std::string legacy_model;
    const auto legacy_hlsl=DecompileBinaryHLSL(legacy_params,legacy_patched,legacy_model,legacy_failed);
    require_batch(!legacy_failed && !legacy_patched && legacy_model=="ps_3_0" && legacy_hlsl.find("tint")!=std::string::npos,
        "F274: native DX9 bytecode retains the existing public diagnostic/export model and register naming without a DXBC signature table");
}

namespace {
struct SpecialOutputAuditFixture {
    const char* name;
    const char* source;
    std::vector<std::string> registers;
    ComPtr<ID3DBlob> code;
    std::string text;
};

void check_special_output_reflection(ID3DBlob* original, ID3DBlob* rebuilt)
{
    ComPtr<ID3D11ShaderReflection> before,after;
    require_batch(SUCCEEDED(D3DReflect(original->GetBufferPointer(),original->GetBufferSize(),
        __uuidof(ID3D11ShaderReflection),reinterpret_cast<void**>(before.GetAddressOf()))) &&
        SUCCEEDED(D3DReflect(rebuilt->GetBufferPointer(),rebuilt->GetBufferSize(),
        __uuidof(ID3D11ShaderReflection),reinterpret_cast<void**>(after.GetAddressOf()))),
        "F275: reflect both native shader signatures independently");
    D3D11_SHADER_DESC original_desc{},rebuilt_desc{};
    require_batch(SUCCEEDED(before->GetDesc(&original_desc)) && SUCCEEDED(after->GetDesc(&rebuilt_desc)) &&
        original_desc.OutputParameters==rebuilt_desc.OutputParameters,
        "F275: regenerated shader retains the native output count");
    for (UINT i=0;i<original_desc.OutputParameters;++i) {
        D3D11_SIGNATURE_PARAMETER_DESC expected{};
        require_batch(SUCCEEDED(before->GetOutputParameterDesc(i,&expected)),"F275: original output descriptor");
        bool found=false;
        for (UINT j=0;j<rebuilt_desc.OutputParameters;++j) {
            D3D11_SIGNATURE_PARAMETER_DESC actual{};
            require_batch(SUCCEEDED(after->GetOutputParameterDesc(j,&actual)),"F275: rebuilt output descriptor");
            found |= !std::strcmp(actual.SemanticName,expected.SemanticName) && actual.SemanticIndex==expected.SemanticIndex &&
                actual.SystemValueType==expected.SystemValueType && actual.ComponentType==expected.ComponentType && actual.Mask==expected.Mask;
        }
        require_batch(found,"F275: regenerated shader preserves each special semantic, scalar type and mask");
    }
}
}

void test_dx11_decompiler_special_output_regression()
{
    std::vector<SpecialOutputAuditFixture> fixtures = {
        {"depth", "float main():SV_Depth{return 0.25;}", {"oDepth"}},
        {"coverage", "uint main():SV_Coverage{return 1;}", {"oMask"}},
        {"depth-ge", "float main():SV_DepthGreaterEqual{return 0.5;}", {"oDepthGE"}},
        {"depth-le", "float main():SV_DepthLessEqual{return 0.5;}", {"oDepthLE"}},
        {"mixed", "struct O{float4 color:SV_Target;float depth:SV_Depth;uint mask:SV_Coverage;};"
            "O main(){O o;o.color=float4(0.25,0.5,0.75,1);o.depth=0.5;o.mask=1;return o;}", {"o0","oDepth","oMask"}}
    };
    for (auto& fixture : fixtures) {
        ComPtr<ID3DBlob> errors, assembly;
        require_batch(SUCCEEDED(D3DCompile(fixture.source,std::strlen(fixture.source),nullptr,nullptr,nullptr,
            "main","ps_5_0",0,0,&fixture.code,&errors)), "F275: compile actual special system-output shader");
        require_batch(SUCCEEDED(D3DDisassemble(fixture.code->GetBufferPointer(),fixture.code->GetBufferSize(),
            0,nullptr,&assembly)), "F275: obtain native D3DDisassemble signature layout");
        fixture.text = static_cast<const char*>(assembly->GetBufferPointer());
        const auto header = fixture.text.find("// Output signature:");
        const auto end = fixture.text.find("\n//\n", header + 21);
        require_batch(header != std::string::npos && end != std::string::npos,
            "F275: native fixture includes output signature table");
        std::fprintf(stderr,"F275 native %s:\n%s\n",fixture.name,fixture.text.substr(header,end-header).c_str());
    }
    auto reordered=fixtures.back();
    reordered.name="mixed-specials-first";
    const auto ordinary=reordered.text.find("// SV_Target");
    const auto ordinary_end=reordered.text.find('\n',ordinary)+1;
    const auto ordinary_row=reordered.text.substr(ordinary,ordinary_end-ordinary);
    reordered.text.erase(ordinary,ordinary_end-ordinary);
    const auto table_end=reordered.text.find("\n//\n",ordinary);
    require_batch(table_end!=std::string::npos,"F275: native mixed signature table terminator");
    reordered.text.insert(table_end+1,ordinary_row);
    fixtures.push_back(std::move(reordered));
    BatchDevice device;
    for (const auto& fixture : fixtures) {
        check_dx11_special_output_rows(fixture.text,fixture.registers);
        DecompilerSettings settings;
        ParseParameters params{}; params.bytecode=fixture.code->GetBufferPointer(); params.bytecodeSize=fixture.code->GetBufferSize(); params.G=&settings;
        params.decompiled=fixture.text.c_str(); params.decompiledSize=fixture.text.size();
        for (bool zero : {false,true}) {
            params.ZeroOutput=zero;
            bool patched=false,failed=false; std::string model;
            const auto hlsl=DecompileBinaryHLSL(params,patched,model,failed);
            require_batch(!failed && !hlsl.empty() && model=="ps_5_0",
                "F275: public decompiler accepts native special outputs in both modes");
            ComPtr<ID3DBlob> rebuilt,errors;
            const auto hr=D3DCompile(hlsl.data(),hlsl.size(),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&rebuilt,&errors);
            if (FAILED(hr)) std::fprintf(stderr,"F275 recompile %s zero=%d:\n%s\n%s\n",fixture.name,zero,hlsl.c_str(),
                errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no compiler diagnostic");
            require_batch(SUCCEEDED(hr),"F275: public regenerated HLSL recompiles using the native compiler");
            check_special_output_reflection(fixture.code.Get(),rebuilt.Get());
            ComPtr<ID3D11PixelShader> shader;
            require_batch(SUCCEEDED(device.native->CreatePixelShader(rebuilt->GetBufferPointer(),rebuilt->GetBufferSize(),nullptr,&shader)),
                "F275: WARP accepts regenerated special-output shader");
        }
    }
    const auto& mixed=fixtures[4];
    for (const auto* incomplete : {"// SV_Coverage 0 N/A\n", "// SV_Coverage 0 N/A oMask\n",
            "// SV_Coverage 0 N/A oMask COVERAGE\n"}) {
        auto text=mixed.text;
        const auto row=text.find("// SV_Coverage");
        const auto row_end=text.find('\n',row)+1;
        text.replace(row,row_end-row,incomplete);
        check_dx11_special_output_failure(text);
        DecompilerSettings settings;
        ParseParameters params{}; params.bytecode=mixed.code->GetBufferPointer(); params.bytecodeSize=mixed.code->GetBufferSize(); params.G=&settings;
        params.decompiled=text.c_str(); params.decompiledSize=text.size();
        for (bool zero : {false,true}) {
            params.ZeroOutput=zero;
            bool patched=false,failed=false; std::string model;
            (void)DecompileBinaryHLSL(params,patched,model,failed);
            require_batch(failed,"F275: malformed special output propagates public error flag in both modes");
        }
    }
}

std::string dx11_audit_dx9_binding_disassembly()
{
    using namespace dx11_slot_test;
    const char source[] =
        "float4 tint : register(c3); sampler2D diffuse : register(s2); bool enabled : register(b1);"
        "float4 main(float2 uv:TEXCOORD0):COLOR0 { [branch] if(enabled) return tex2Dlod(diffuse,float4(uv,0,0))*tint; return tint; }";
    ComPtr<ID3DBlob> code, errors, assembly;
    const HRESULT result = D3DCompile(source, sizeof(source)-1, nullptr, nullptr, nullptr, "main", "ps_3_0", 0, 0, &code, &errors);
    if (FAILED(result)) std::fprintf(stderr, "F276 native SM3 compilation: 0x%08lx %s\n", result,
        errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no compiler diagnostic");
    require_batch(SUCCEEDED(result),
        "F276: native compiler produces real SM3 sampler/constant/bool bindings");
    require_batch(SUCCEEDED(D3DDisassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, &assembly)),
        "F276: native disassembler produces the actual legacy Registers table");
    return std::string(static_cast<const char*>(assembly->GetBufferPointer()));
}

void test_dx11_decompiler_public_dx9_binding_regression()
{
    using namespace dx11_slot_test;
    const char source[] = "float4 tint : register(c3); float4 main():COLOR0 { return tint; }";
    ComPtr<ID3DBlob> code, errors, assembly;
    require_batch(SUCCEEDED(D3DCompile(source, sizeof(source)-1, nullptr, nullptr, nullptr, "main", "ps_3_0", 0, 0, &code, &errors)) &&
        SUCCEEDED(D3DDisassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, &assembly)),
        "F276: native SM3 bytecode and matching disassembly for the public decoder");
    const std::string text(static_cast<const char*>(assembly->GetBufferPointer()));
    DecompilerSettings settings;
    ParseParameters params{};
    params.bytecode = code->GetBufferPointer(); params.bytecodeSize = code->GetBufferSize(); params.decompiled = text.c_str();
    params.decompiledSize = text.size(); params.G = &settings;
    bool patched = false, failed = false; std::string model;
    const auto hlsl = DecompileBinaryHLSL(params, patched, model, failed);
    require_batch(!failed && !patched && model == "ps_3_0" && hlsl.find("tint") != std::string::npos,
        "F276: public production decoder retains SM3 register naming and source shader model");
}

void test_dx11_decompiler_public_suffix_regression()
{
    using namespace dx11_slot_test;
    const char seed[] = "float4 main():SV_Target{return float4(0.25,0.5,0.75,1);}";
    ComPtr<ID3DBlob> seed_code, errors;
    require_batch(SUCCEEDED(D3DCompile(seed,sizeof(seed)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&seed_code,&errors)),
        "F279: native compiler supplies the actual DXBC container and output signature");
    const auto* data = static_cast<const unsigned char*>(seed_code->GetBufferPointer());
    const std::vector<unsigned char> original(data, data + seed_code->GetBufferSize());
    BatchDevice device;
    for (const auto* opcode : {"ubfe", "bfi", "swapc"}) {
        for (const auto* mask : {"x", "xy", "xyz", "xyzw"}) {
            for (const auto* suffix : {"x", "wzyx"}) {
                std::string text = "ps_5_0\ndcl_output o0.xyzw\ndcl_temps 5\n"
                    "mov r0.xyzw, l(4,4,4,4)\nmov r1.xyzw, l(0,0,0,0)\n"
                    "mov r2.xyzw, l(10,11,12,13)\nmov r3.xyzw, l(3,5,7,9)\nmov r4.xyzw, l(0,0,0,0)\n";
                text += std::string(opcode) + " r4." + mask + ", ";
                if (!std::strcmp(opcode,"swapc")) text += std::string("r3.") + mask + ", ";
                // Literal bit widths/offsets avoid the separate float-register
                // shift-count code path. SWAPC selects its condition through
                // its own loop; its two value operands exercise GetSuffix.
                if (!std::strcmp(opcode,"swapc")) text += std::string("r0.xyzw, r1.") + suffix + ", r2." + suffix;
                else text += std::string("l(4,4,4,4), l(0,0,0,0), r2.") + suffix;
                if (!std::strcmp(opcode,"bfi")) text += std::string(", r3.") + suffix;
                text += "\nutof r4.xyzw, r4.xyzw\nmul o0.xyzw, r4.xyzw, l(0.0625,0.0625,0.0625,0.0625)\nret\n";
                std::vector<char> assembly(text.begin(),text.end());
                const auto binary = assembler(&assembly,original);
                ComPtr<ID3D11PixelShader> native, replacement;
                require_batch(SUCCEEDED(device.native->CreatePixelShader(binary.data(),binary.size(),nullptr,&native)),
                    "F279: native WARP validates each actual scalar/vector UBFE, BFI and SWAPC fixture");
                std::vector<unsigned char> disassembly;
                auto bytecode = binary;
                require_batch(SUCCEEDED(disassembler(&bytecode,&disassembly,nullptr)), "F279: real production disassembly");
                const std::string native_text(disassembly.begin(),disassembly.end());
                DecompilerSettings settings;
                ParseParameters params{}; params.bytecode = bytecode.data(); params.bytecodeSize = bytecode.size(); params.decompiled = native_text.c_str();
                params.decompiledSize = native_text.size(); params.G = &settings;
                bool patched = false, failed = false; std::string model;
                const auto hlsl = DecompileBinaryHLSL(params,patched,model,failed);
                require_batch(!failed && !hlsl.empty(), "F279: all supported consumer fixtures retain public decompilation");
                ComPtr<ID3DBlob> rebuilt;
                const auto compiled = D3DCompile(hlsl.data(),hlsl.size(),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&rebuilt,&errors);
                if (FAILED(compiled)) std::fprintf(stderr,"F279 %s .%s source .%s:\n%s\n%s\n",opcode,mask,suffix,hlsl.c_str(),
                    errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no compiler diagnostic");
                require_batch(SUCCEEDED(compiled), "F279: actual public decompiler output recompiles for every consumer and mask");
                require_batch(SUCCEEDED(device.native->CreatePixelShader(rebuilt->GetBufferPointer(),rebuilt->GetBufferSize(),nullptr,&replacement)),
                    "F279: native WARP accepts the regenerated consumer shader");
                require_batch(render_dx11_audit_constant_pixel(device,native.Get()) == render_dx11_audit_constant_pixel(device,replacement.Get()),
                    "F279: each consumer preserves the rendered scalar/vector component values");
            }
        }
    }
}

void test_dx11_decompiler_public_texture_literal_regression()
{
    using namespace dx11_slot_test;
    const char* sources[] = {
        "Texture2D<float4> tex:register(t0); SamplerState samp:register(s0);"
        "float4 main():SV_Target{return tex.SampleLevel(samp,float2(0.25,0.75),0);}",
        "Texture3D<float4> tex:register(t0); SamplerState samp:register(s0);"
        "float4 main():SV_Target{return tex.SampleLevel(samp,float3(0.25,0.75,0.5),0);}",
        "cbuffer C{float3 floatCoords;}; TextureCube<float4> tex:register(t0); SamplerState samp:register(s0);"
        "float4 main():SV_Target{return tex.SampleLevel(samp,floatCoords,0);}",
        "cbuffer C{float3 float2Indices;}; TextureCube<float4> tex:register(t0); SamplerState samp:register(s0);"
        "float4 main():SV_Target{return tex.SampleLevel(samp,float2Indices,0);}",
        "cbuffer C{float floatCoords;}; Texture1D<float4> tex:register(t0); SamplerState samp:register(s0);"
        "float4 main():SV_Target{return tex.SampleLevel(samp,floatCoords,0);}",
        "cbuffer C{float float123;}; Texture1D<float4> tex:register(t0); SamplerState samp:register(s0);"
        "float4 main():SV_Target{return tex.SampleLevel(samp,float123,0);}",
    };
    BatchDevice device;
    for (const auto* source : sources) {
        ComPtr<ID3DBlob> code, errors, assembly;
        require_batch(SUCCEEDED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr,
            "main", "ps_5_0", 0, 0, &code, &errors)), "F280: native compiler produces an actual texture-sampling shader");
        require_batch(SUCCEEDED(D3DDisassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, &assembly)),
            "F280: native disassembler supplies the public decompiler input");
        const std::string text(static_cast<const char*>(assembly->GetBufferPointer()));
        const bool named_coordinate = std::strstr(source, "cbuffer") != nullptr;
        const char* coordinate_name = std::strstr(source, "float2Indices") ? "float2Indices" :
            std::strstr(source, "float123") ? "float123" : "floatCoords";
        require_batch(text.find("sample_l") != std::string::npos && (named_coordinate ?
            text.find(coordinate_name) != std::string::npos && text.find("cb0[0]") != std::string::npos :
            text.find("l(0.250000, 0.750000") != std::string::npos),
            "F280: real disassembly contains the expected immediate or named-cbuffer sampling coordinates");
        DecompilerSettings settings;
        ParseParameters params{};
        params.bytecode = code->GetBufferPointer(); params.bytecodeSize = code->GetBufferSize(); params.decompiled = text.c_str();
        params.decompiledSize = text.size(); params.G = &settings;
        bool patched = false, failed = false; std::string model;
        const auto hlsl = DecompileBinaryHLSL(params, patched, model, failed);
        if (failed) std::fprintf(stderr, "F280 rejected native shader:\n%s\n", hlsl.c_str());
        require_batch(!failed && !hlsl.empty() && model == "ps_5_0",
            "F280: public decompilation accepts native literal and named-cbuffer coordinates");
        ComPtr<ID3DBlob> rebuilt;
        require_batch(SUCCEEDED(D3DCompile(hlsl.data(), hlsl.size(), nullptr, nullptr, nullptr,
            "main", "ps_5_0", 0, 0, &rebuilt, &errors)), "F280: native compiler accepts the regenerated texture-sampling HLSL");
        ComPtr<ID3D11PixelShader> original, replacement;
        require_batch(SUCCEEDED(device.native->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &original)) &&
            SUCCEEDED(device.native->CreatePixelShader(rebuilt->GetBufferPointer(), rebuilt->GetBufferSize(), nullptr, &replacement)),
            "F280: actual WARP accepts both original and regenerated sampling shaders");
        std::fprintf(stderr, "F280: native/public/recompiled/WARP %s coordinates PASS\n",
            named_coordinate ? coordinate_name : "immediate");
    }
}

void test_dx11_decompiler_public_immediate_regression()
{
    BatchDevice device;
    const char special_source[]="float4 main():SV_Target{return asfloat(uint4(0x7f800000,0xff800000,0x7fc00000,0));}";
    ComPtr<ID3DBlob> special_code,special_errors,special_assembly;
    require_batch(SUCCEEDED(D3DCompile(special_source,sizeof(special_source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,
        &special_code,&special_errors)) && SUCCEEDED(D3DDisassemble(special_code->GetBufferPointer(),special_code->GetBufferSize(),
        0,nullptr,&special_assembly)),"F282: native exceptional-value compatibility probe");
    const std::string special_text=static_cast<const char*>(special_assembly->GetBufferPointer());
    DecompilerSettings special_settings;
    ParseParameters special_params{}; special_params.bytecode=special_code->GetBufferPointer(); special_params.bytecodeSize=special_code->GetBufferSize(); special_params.G=&special_settings;
    special_params.decompiled=special_text.c_str(); special_params.decompiledSize=special_text.size();
    bool special_patched=false,special_failed=false; std::string special_model;
    const auto special_hlsl=DecompileBinaryHLSL(special_params,special_patched,special_model,special_failed);
    ComPtr<ID3DBlob> special_rebuilt;
    const auto special_hr=D3DCompile(special_hlsl.data(),special_hlsl.size(),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,
        &special_rebuilt,&special_errors);
    const bool special_bits=SUCCEEDED(special_hr) &&
        dx11_immediate_bytecode_precision_matches(special_code->GetBufferPointer(),special_code->GetBufferSize(),
            special_rebuilt->GetBufferPointer(),special_rebuilt->GetBufferSize());
    std::fprintf(stderr,"F282 exceptional probe (observed compatibility, not a claimed supported round-trip): error=%d recompile=0x%08x bits_preserved=%d\n%s\n%s\n",
        special_failed,static_cast<unsigned>(special_hr),special_bits,special_text.c_str(),special_hlsl.c_str());
    const char* sources[]={
        "float4 main():SV_Target{return float4(0.123456789,-0.333333343,3.14159274,1e20);}",
        "float4 main():SV_Target{return float4(1e-20,3.402823466e38,-0.0,1.17549435e-38);}",
        "float main():SV_Target{return -0.333333343;}"
    };
    for (const auto* source : sources) {
        ComPtr<ID3DBlob> code,errors,assembly;
        require_batch(SUCCEEDED(D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)),
            "F282: compile native scalar/vector precision fixture");
        require_batch(SUCCEEDED(D3DDisassemble(code->GetBufferPointer(),code->GetBufferSize(),0,nullptr,&assembly)),
            "F282: obtain actual rounded native immediate text");
        const std::string text=static_cast<const char*>(assembly->GetBufferPointer());
        const auto literal=text.find("l(");
        require_batch(literal!=std::string::npos,"F282: native shader contains an immediate operand");
        std::fprintf(stderr,"F282 native literal: %s\n",text.substr(literal,text.find('\n',literal)-literal).c_str());
        DecompilerSettings settings;
        ParseParameters params{}; params.bytecode=code->GetBufferPointer(); params.bytecodeSize=code->GetBufferSize(); params.G=&settings;
        params.decompiled=text.c_str(); params.decompiledSize=text.size();
        bool patched=false,failed=false; std::string model;
        const auto hlsl=DecompileBinaryHLSL(params,patched,model,failed);
        require_batch(!failed && !hlsl.empty(),"F282: real public decompiler accepts native precision fixtures");
        ComPtr<ID3DBlob> rebuilt;
        const auto hr=D3DCompile(hlsl.data(),hlsl.size(),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&rebuilt,&errors);
        if (FAILED(hr)) std::fprintf(stderr,"F282 generated HLSL:\n%s\n%s\n",hlsl.c_str(),
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no compiler diagnostic");
        require_batch(SUCCEEDED(hr),"F282: generated precision HLSL recompiles natively");
        check_dx11_immediate_bytecode_precision(code->GetBufferPointer(),code->GetBufferSize(),
            rebuilt->GetBufferPointer(),rebuilt->GetBufferSize());
        ComPtr<ID3D11PixelShader> shader;
        require_batch(SUCCEEDED(device.native->CreatePixelShader(rebuilt->GetBufferPointer(),rebuilt->GetBufferSize(),nullptr,&shader)),
            "F282: WARP accepts precision-preserving regenerated shader");
        for (const auto* invalid : {"l(bad)","l(0.123457junk)","l(0.123457,bad)"}) {
            auto damaged=text;
            const auto close=damaged.find(')',literal);
            damaged.replace(literal,close-literal+1,invalid);
            params.decompiled=damaged.c_str(); params.decompiledSize=damaged.size();
            failed=false; patched=false;
            (void)DecompileBinaryHLSL(params,patched,model,failed);
            std::fprintf(stderr,"F282 reject %s: public error=%d\n",invalid,failed);
            require_batch(failed,"F282: bad native immediate text propagates public failure instead of silently repairing it");
        }
    }
}

void test_dx11_dx9_sampler_dimension_regression()
{
    const std::pair<const char*,D3D_SRV_DIMENSION> cases[] = {
        {"sampler3D Surface:register(s3);float4 main(float3 uv:TEXCOORD):COLOR0{return tex3D(Surface,uv);}",D3D_SRV_DIMENSION_TEXTURE3D},
        {"sampler2D Surface:register(s3);float4 main(float2 uv:TEXCOORD):COLOR0{return tex2D(Surface,uv);}",D3D_SRV_DIMENSION_TEXTURE2D},
        {"samplerCUBE Surface:register(s3);float4 main(float3 uv:TEXCOORD):COLOR0{return texCUBE(Surface,uv);}",D3D_SRV_DIMENSION_TEXTURECUBE},
        {"sampler1D Surface:register(s3);float4 main(float uv:TEXCOORD):COLOR0{return tex1D(Surface,uv);}",D3D_SRV_DIMENSION_TEXTURE1D},
        {"sampler Surface:register(s3);float4 main(float2 uv:TEXCOORD):COLOR0{return tex2D(Surface,uv);}",D3D_SRV_DIMENSION_TEXTURE2D}
    };
    for (const auto& fixture : cases) {
        Microsoft::WRL::ComPtr<ID3DBlob> code,errors,assembly;
        const auto hr=D3DCompile(fixture.first,std::strlen(fixture.first),nullptr,nullptr,nullptr,"main","ps_3_0",0,0,&code,&errors);
        if (FAILED(hr) && errors) std::fprintf(stderr,"F285: compiler: %s\n",static_cast<const char*>(errors->GetBufferPointer()));
        dx11_slot_test::require_batch(SUCCEEDED(hr),"F285: native compiler generates valid unmodified DX9 sampler bytecode with CTAB");
        dx11_slot_test::require_batch(SUCCEEDED(D3DDisassemble(code->GetBufferPointer(),code->GetBufferSize(),0,nullptr,&assembly)),
            "F285: native disassembler accepts every compiled DX9 sampler fixture");
        check_dx11_dx9_sampler_dimension(code->GetBufferPointer(),code->GetBufferSize(),static_cast<unsigned>(fixture.second));
    }
    test_dx11_decompiler_public_literal_regression();
}

void test_dx11_decompiler_lifetime_regression()
{
    using namespace dx11_slot_test;
    const char source[]="struct Inner{float4 color;};struct Outer{Inner nested;};cbuffer Params:register(b0){Outer data;}"
        "float4 main():SV_Target{return data.nested.color;}";
    ComPtr<ID3DBlob> code,errors,assembly;
    require_batch(SUCCEEDED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&errors)) &&
        SUCCEEDED(D3DDisassemble(code->GetBufferPointer(),code->GetBufferSize(),0,nullptr,&assembly)),
        "F283: native compiler/disassembler generate genuine nested-structure reflection input");
    const std::string text(static_cast<const char*>(assembly->GetBufferPointer()));
    DecompilerSettings settings;
    ParseParameters params{}; params.bytecode=code->GetBufferPointer(); params.bytecodeSize=code->GetBufferSize(); params.decompiled=text.c_str(); params.decompiledSize=text.size(); params.G=&settings;
    bool patched=false,failed=false; std::string model;
    const auto retained=DecompileBinaryHLSL(params,patched,model,failed);
    require_batch(!failed && !retained.empty() && model=="ps_5_0","F283: normal native fixture decompiles before lifetime probes");
    ComPtr<ID3DBlob> rebuilt;
    auto compiled=D3DCompile(retained.data(),retained.size(),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&rebuilt,&errors);
    if (FAILED(compiled)) std::fprintf(stderr,"F283 regenerated HLSL:\n%s\n%s\n",retained.c_str(),errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no diagnostic");
    require_batch(SUCCEEDED(compiled),"F283: nested fixture's existing normal output is valid native HLSL");
    check_dx11_decompiler_lifetime(code->GetBufferPointer(),code->GetBufferSize(),text.data(),text.size());
    rebuilt.Reset(); errors.Reset();
    require_batch(SUCCEEDED(D3DCompile(retained.data(),retained.size(),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&rebuilt,&errors)),
        "F283: returned HLSL remains independently usable after repeated real Decode and destruction");
    BatchDevice device; ComPtr<ID3D11PixelShader> original,replacement;
    require_batch(SUCCEEDED(device.native->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&original)) &&
        SUCCEEDED(device.native->CreatePixelShader(rebuilt->GetBufferPointer(),rebuilt->GetBufferSize(),nullptr,&replacement)),
        "F283: native WARP accepts original and retained/recompiled output shaders");
    test_dx11_decompiler_public_signature_regression();
}



