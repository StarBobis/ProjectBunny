// The regression target substitutes this TU for IniHandler.cpp, compiling the
// production parser once and testing its private entry points directly.
#include "../IniHandler.cpp"
#include <stdexcept>

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
