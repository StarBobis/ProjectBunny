// The regression target substitutes this TU for IniHandler.cpp, compiling the
// production parser once and testing its private entry points directly.
#include "../IniHandler.cpp"
#include <clocale>
#include <stdexcept>
#include <filesystem>
#include "slot_range_test_support.h"

namespace {
void check_ini_audit(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class IniWarningScope {
    bool saved = ini_warned;
public:
    ~IniWarningScope() { ini_warned = saved; }
};

class IniSectionStateScope {
    IniSections saved;
public:
    IniSectionStateScope() { saved.swap(ini_sections); }
    ~IniSectionStateScope() { ini_sections.clear(); saved.swap(ini_sections); }
};

class IniAuditLogCapture {
    FILE* saved = LogFile;
    LogVerbosity saved_level = gLogVerbosity;
    bool saved_warnings = G->gShowWarnings;
public:
    FILE* capture = std::tmpfile();
    IniAuditLogCapture()
    {
        check_ini_audit(capture != nullptr, "F295: create real log capture");
        LogFile = capture; gLogVerbosity = LogVerbosity::INFO; G->gShowWarnings = false;
    }
    std::string read()
    {
        check_ini_audit(!std::fflush(capture) && !std::fseek(capture, 0, SEEK_SET), "F295: flush and rewind diagnostic stream");
        std::string text; char buffer[1024]; size_t count;
        while ((count = std::fread(buffer, 1, sizeof(buffer), capture)) != 0) text.append(buffer, count);
        check_ini_audit(!std::fseek(capture, 0, SEEK_END), "F295: restore diagnostic append position");
        return text;
    }
    ~IniAuditLogCapture()
    {
        LogFile = saved; gLogVerbosity = saved_level; G->gShowWarnings = saved_warnings; std::fclose(capture);
    }
};

// LogOverlayW converts wide messages to the current LC_CTYPE encoding, and the
// fixture path contains the build directory's non-ASCII characters. Production
// LoadConfigFile selects this locale before parsing, so mirror that here.
class IniUtf8LocaleScope {
    std::string saved;
public:
    IniUtf8LocaleScope()
    {
        const char* current = std::setlocale(LC_CTYPE, nullptr);
        check_ini_audit(current != nullptr, "F185: query current locale");
        saved = current;
        check_ini_audit(std::setlocale(LC_CTYPE, "en_US.UTF-8") != nullptr,
            "F185: use the production config-load UTF-8 locale");
    }
    ~IniUtf8LocaleScope()
    {
        if (!std::setlocale(LC_CTYPE, saved.c_str())) std::abort();
    }
};

bool contains_ini_audit_text(const std::string& bytes, const std::string& text)
{
    if (bytes.find(text) != std::string::npos) return true;
    std::string wide;
    for (const auto character : text) { wide.push_back(character); wide.push_back('\0'); }
    return bytes.find(wide) != std::string::npos;
}

class IniEnumerationFixture {
    std::filesystem::path base;
public:
    std::filesystem::path root;
    std::vector<pcre2_code*> exclusions;
    IniEnumerationFixture()
    {
        wchar_t executable[MAX_PATH]{};
        const auto length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        check_ini_audit(length && length < MAX_PATH, "F294: locate test host");
        base = std::filesystem::canonical(std::filesystem::path(executable).parent_path());
        bool in_build = false;
        for (const auto& part : base) in_build |= !_wcsicmp(part.c_str(), L"build");
        check_ini_audit(in_build, "F294: fixture remains inside build");
        root = base / L"ini-enumeration-audit";
        check_ini_audit(std::filesystem::create_directory(root), "F294: reserve exclusive enumeration fixture");
    }
    void file(const wchar_t* name, unsigned value)
    {
        const auto path = root / name;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output << "[Present]\nx" << value << " = " << value << "\n";
        output.close(); check_ini_audit(output.good(), "F294: write complete enumeration input");
    }
    ~IniEnumerationFixture()
    {
        free_globbing_vector(exclusions);
        std::error_code error;
        const auto resolved = std::filesystem::canonical(root, error);
        if (error || resolved != root || resolved.parent_path() != base) {
            std::fputs("FAIL F294: refuse unsafe fixture cleanup\n", stderr); std::abort();
        }
        std::filesystem::remove_all(root, error);
        if (error) { std::fputs("FAIL F294: fixture cleanup\n", stderr); std::abort(); }
    }
};

template <typename T>
FuzzyMatch parse_audit_flags(const wchar_t* text, unsigned value, unsigned mask,
    EnumName_t<const wchar_t*, T>* names)
{
    FuzzyMatch field;
    field.val = 0xa5a5a5a5; field.mask = 0x5a5a5a5a;
    check_ini_audit(parse_masked_flags_field(text, &field.val, &field.mask, names),
        "F187: accept valid numeric or named flag syntax");
    check_ini_audit(field.val == value && field.mask == mask,
        "F187: retain explicit masks and provide the full default mask");
    field.op = FuzzyMatchOp::EQUAL;
    return field;
}
}

void test_dx11_masked_flags_regression()
{
    IniWarningScope warnings;
    std::fputs("F187: explicit mask must reach the real fuzzy matcher\n", stderr); std::fflush(stderr);
    auto masked = parse_audit_flags(L"0x1 / 0x3", 1, 3, CustomResourceBindFlagNames);
    check_ini_audit(masked.matches_uint(1) && masked.matches_uint(5) && !masked.matches_uint(3) && !masked.matches_uint(0),
        "F187: flags outside an explicit mask cannot change the match result");
    auto exact = parse_audit_flags(L"0x1", 1, UINT_MAX, CustomResourceBindFlagNames);
    check_ini_audit(exact.matches_uint(1) && !exact.matches_uint(5), "F187: omitted mask requires exact flags");
    for (const auto* text : {L"", L"0", L"0x0"})
        parse_audit_flags(text, 0, UINT_MAX, CustomResourceBindFlagNames);
    auto any = parse_audit_flags(L"0x0 / 0x0", 0, 0, CustomResourceBindFlagNames);
    check_ini_audit(any.matches_uint(0) && any.matches_uint(UINT_MAX), "F187: explicit zero mask is preserved");
    parse_audit_flags(L"0xffffffff / 0xffffffff", UINT_MAX, UINT_MAX, CustomResourceBindFlagNames);
    parse_audit_flags(L"vertex_buffer index_buffer", 3, UINT_MAX, CustomResourceBindFlagNames);
    auto named = parse_audit_flags(L"+vertex_buffer -index_buffer", 1, 3, CustomResourceBindFlagNames);
    check_ini_audit(named.matches_uint(5) && !named.matches_uint(3), "F187: signed named flags retain partial-mask semantics");
    parse_audit_flags(L"+read -write", D3D11_CPU_ACCESS_READ, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE,
        ResourceCPUAccessFlagNames);
    parse_audit_flags(L"0x2 / 0x3", 2, 3, ResourceMiscFlagNames);
    for (const auto* bad : {L"0x", L"0x1junk", L"0x1 /", L"0x1 / 0x", L"0x1 / 0x2junk", L"0x1 / rubbish",
            L"vertex_buffer vertex_buffer", L"+", L"-", L"not_a_flag"}) {
        unsigned value = 0xa5a5a5a5, mask = 0x5a5a5a5a;
        check_ini_audit(!parse_masked_flags_field(std::wstring(bad), &value, &mask, CustomResourceBindFlagNames),
            "F187: incomplete numeric fields, trailing junk and invalid named flags are rejected");
    }
}

void test_dx11_ini_enumeration_regression()
{
    IniWarningScope warnings; IniSectionStateScope sections; IniEnumerationFixture files;
    files.file(L"mods/c.InI", 3); files.file(L"mods/b.INI", 2); files.file(L"mods/A.ini", 1);
    files.file(L"mods/.ini", 0); files.file(L"mods/\u4e2d\u6587.INI", 4);
    files.file(L"mods/Achild/z.ini", 5); files.file(L"mods/zchild/A.INI", 6);
    for (const auto* ignored : {L"mods/a", L"mods/ab", L"mods/abc", L"mods/readme.txt", L"mods/skip.INI",
            L"mods/skip-dir/hidden.ini"}) files.file(ignored, 99);
    std::vector<std::wstring> patterns{L"skip*"};
    files.exclusions = globbing_vector_to_regex(patterns);
    check_ini_audit(files.exclusions.size() == 1, "F294: compile actual exclusion glob");
    std::wstring prefix = files.root.wstring() + L"\\";
    ParseIniFilesRecursive(&prefix[0], L"mods", files.exclusions, (files.root / L"mods").wstring());
    const auto& lines = ini_sections.at(L"Present").kv_vec;
    check_ini_audit(lines.size() == 7, "F294: all INI suffix cases load while short, unrelated and excluded names do not");
    const wchar_t* paths[] = {L"mods\\.ini", L"mods\\A.ini", L"mods\\b.INI", L"mods\\c.InI", L"mods\\\u4e2d\u6587.INI",
        L"mods\\Achild\\z.ini", L"mods\\zchild\\A.INI"};
    for (size_t i = 0; i < lines.size(); ++i)
        check_ini_audit(lines[i].second == std::to_wstring(i) && lines[i].ini_namespace == paths[i],
            "F294: preserve case-insensitive file ordering, directory recursion ordering and exact namespace paths");
}

void test_dx11_ini_invalid_utf8_regression()
{
    IniWarningScope warnings; IniSectionStateScope sections; IniEnumerationFixture files;
    IniUtf8LocaleScope locale; IniAuditLogCapture log;

    static const char valid_fixture[] = "\xEF\xBB\xBF[System]\r\nvalue = 3\r\n; \xE4\xB8\xAD\xE6\x96\x87\r\n";
    const auto valid_path = files.root / L"valid-bom-crlf.ini";
    {
        std::ofstream output(valid_path, std::ios::binary | std::ios::trunc);
        output.write(valid_fixture, sizeof(valid_fixture) - 1);
        output.close();
        check_ini_audit(output.good(), "F185: write complete valid UTF-8 BOM/CRLF fixture");
    }
    ParseNamespacedIniFile(valid_path.c_str(), nullptr);
    check_ini_audit(GetIniInt(L"System", L"value", -1, nullptr) == 3,
        "F185: valid UTF-8 with BOM and CRLF still parses unchanged");
    check_ini_audit(!contains_ini_audit_text(log.read(), "Invalid UTF-8"),
        "F185: valid UTF-8 must not raise the invalid encoding warning");

    static const char fixture[] =
        "[Present]\n"
        "before = 1\n"
        "; \xE4\xB8\xAD\xE6\x96\x87\n"
        "bad = \xC3\x28\n"
        "after = 2\n"
        "[AfterInvalid]\n"
        "value = 7\n";
    const auto path = files.root / L"invalid-utf8.ini";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(fixture, sizeof(fixture) - 1);
        output.close();
        check_ini_audit(output.good(), "F185: write complete invalid UTF-8 fixture");
    }

    ParseNamespacedIniFile(path.c_str(), nullptr);
    const auto messages = log.read();

    check_ini_audit(GetIniInt(L"Present", L"before", -1, nullptr) == 1,
        "F185: valid lines before the invalid byte still parse");
    check_ini_audit(GetIniInt(L"Present", L"after", -1, nullptr) == 2 &&
        GetIniInt(L"AfterInvalid", L"value", -1, nullptr) == 7,
        "F185: invalid UTF-8 must not truncate the rest of the file");
    check_ini_audit(contains_ini_audit_text(messages, "Invalid UTF-8") &&
        contains_ini_audit_text(messages, "invalid-utf8.ini") &&
        contains_ini_audit_text(messages, "line 4"),
        "F185: decode failure is reported with the file name and the first bad line");
}

void test_dx11_stencil_diagnostic_regression()
{
    IniWarningScope warnings; IniSectionStateScope sections; IniAuditLogCapture log;
    ParseIniExcerpt(L"[CustomShaderAuditStencilBad]\nstencil_front=broken\nstencil_back=broken\n");
    CustomShader invalid;
    std::fputs("F295: malformed front/back diagnostics must name their actual keys\n", stderr); std::fflush(stderr);
    ParseDepthStencilState(&invalid, L"CustomShaderAuditStencilBad");
    const auto messages = log.read();
    check_ini_audit(contains_ini_audit_text(messages, "Unrecognised stencil_front=broken") &&
        contains_ini_audit_text(messages, "Unrecognised stencil_back=broken"),
        "F295: both malformed stencil settings report their actual key names");
    check_ini_audit(invalid.depth_stencil_desc.FrontFace.StencilPassOp == D3D11_STENCIL_OP_KEEP &&
        invalid.depth_stencil_desc.BackFace.StencilPassOp == D3D11_STENCIL_OP_KEEP,
        "F295: malformed values retain the existing default operation behavior");
    ParseIniExcerpt(L"[CustomShaderAuditStencilValid]\nstencil_enable=true\n"
        L"stencil_front=less replace incr_sat decr\nstencil_back=always keep invert zero\n");
    CustomShader valid;
    ParseDepthStencilState(&valid, L"CustomShaderAuditStencilValid");
    const auto& desc = valid.depth_stencil_desc;
    check_ini_audit(desc.StencilEnable && valid.depth_stencil_override == 1 &&
        desc.FrontFace.StencilFunc == D3D11_COMPARISON_LESS && desc.FrontFace.StencilPassOp == D3D11_STENCIL_OP_REPLACE &&
        desc.FrontFace.StencilDepthFailOp == D3D11_STENCIL_OP_INCR_SAT && desc.FrontFace.StencilFailOp == D3D11_STENCIL_OP_DECR &&
        desc.BackFace.StencilFunc == D3D11_COMPARISON_ALWAYS && desc.BackFace.StencilPassOp == D3D11_STENCIL_OP_KEEP &&
        desc.BackFace.StencilDepthFailOp == D3D11_STENCIL_OP_INVERT && desc.BackFace.StencilFailOp == D3D11_STENCIL_OP_ZERO,
        "F295: legal front/back operations retain their original parsed values");
    dx11_slot_test::BatchDevice device;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> state;
    check_ini_audit(SUCCEEDED(device.native->CreateDepthStencilState(&desc, &state)) && state,
        "F295: native WARP accepts the actual parsed descriptor");
}

#include "ini_lookup_performance.inl"
