// Reuse the real signature parser in this test TU; the regression target does
// not also compile SignatureParser.cpp as a separate object.
#include "../../D3D_Shaders/SignatureParser.cpp"
#include <cstdlib>
#include <memory>
#include <stdexcept>

namespace {
void check_signature_audit(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

uint64_t audit_feature_flags(std::string text)
{
    size_t position = 0;
    const auto flags = parse_subshader_feature_info_comment(&text, &position, 0x10);
    check_signature_audit(position == 0, "F151: feature-comment parsing preserves existing rewind semantics");
    return flags;
}
}

void test_dx11_signature_feature_comment_regression()
{
    const std::string stop = "// terminal comment long enough to stop feature parsing\n";
    check_signature_audit(audit_feature_flags("//       Double-precision floating point\n//       Raw and Structured buffers\n" + stop) == 0x13,
        "F151: standard feature comments retain feature bits and preexisting flags");
    check_signature_audit(audit_feature_flags("//\t\t\t\t\t\t\tRaw and Structured buffers\n" + stop) == 0x12,
        "F151: existing nine-byte tab-padded comment format remains supported");
    std::fputs("F151: reject feature text without a real comment prefix\n", stderr); std::fflush(stderr);
    check_signature_audit(audit_feature_flags("123456789Raw and Structured buffers\n" + stop) == 0x10,
        "F151: arbitrary nine-byte prefixes must not set feature flags");
    for (const char* short_line : {"", "/\n", "//\n", "// x\n", "//       Unknown future feature\n"})
        check_signature_audit(audit_feature_flags(short_line) == 0x10, "F151: short/unknown comments do not read past the line or invent flags");
}

void test_dx11_signature_register_eof_regression()
{
    const char* registers[] = {"5", "+", "-", "oDepth", "0", "31"};
    const uint32_t expected[] = {5, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0, 31};
    std::string text;
    for (size_t i = 0; i < ARRAYSIZE(registers); ++i)
        text += "// AUDIT" + std::to_string(i) + " 0   xyzw " + registers[i] + " NONE float   xyzw\n";
    size_t position = 0;
    std::unique_ptr<void, decltype(&std::free)> section(
        parse_signature_section("ISGN", nullptr, "ISG1", &text, &position, false, 0), &std::free);
    check_signature_audit(section != nullptr, "F150: actual signature section allocation succeeds");
    const auto* bytes = static_cast<const unsigned char*>(section.get());
    section_header outer{}; sgn_header header{};
    std::memcpy(&outer, bytes, sizeof(outer));
    std::memcpy(&header, bytes + sizeof(outer), sizeof(header));
    check_signature_audit(!std::memcmp(outer.signature, "ISGN", 4) && header.num_entries == ARRAYSIZE(registers) &&
        outer.size >= sizeof(header) + header.num_entries * sizeof(sgn_entry_serialiased),
        "F150: complete first-stage scanning produces every expected real serialized entry");
    for (size_t i = 0; i < ARRAYSIZE(registers); ++i) {
        sgn_entry_serialiased entry{};
        std::memcpy(&entry, bytes + sizeof(outer) + sizeof(header) + i * sizeof(entry), sizeof(entry));
        std::fprintf(stderr, "F150: register token %s serialized as %08x\n", registers[i], entry.common.reg); std::fflush(stderr);
        check_signature_audit(entry.common.reg == expected[i], "F150: EOF conversion must not preserve the preceding register");
        check_signature_audit(entry.common.mask == 15 && entry.common.used == 15, "F150: mask/usage and normal register parsing remain intact");
    }
}
