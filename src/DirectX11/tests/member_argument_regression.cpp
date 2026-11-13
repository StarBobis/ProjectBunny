#include "CommandList.h"
#include <stdexcept>
#include <cstdint>

namespace {
void require_member_argument(bool result, const char* message)
{
    if (!result) throw std::runtime_error(message);
}
}

void test_dx11_member_argument_regression()
{
    const std::wstring ns;
    CommandListScope scope;
    scope.emplace_front();
    for (const auto* text : {L"0", L"0x0", L"ffffffffffffffff", L"0xffffffffffffffff", L"123456789abcdef0"}) {
        CommandArgumentReader reader(L"hash", text, L"MemberArguments", &ns, &scope);
        uint64_t result = 123;
        const uint64_t expected = text[0] == L'f' || (std::wstring(text).size() > 2 && text[0] == L'0' && text[2] == L'f') ? UINT64_MAX :
            text[0] == L'1' ? UINT64_C(0x123456789abcdef0) : 0;
        require_member_argument(reader.GetHash(&result) && reader.Finished() && result == expected,
            "member Hash: exact uint64 token preserves all 64 bits including zero");
    }
    for (const auto* text : {L"", L"0x", L"0X12", L"ABCDEF", L"10000000000000000", L"0x10000000000000000",
        L"-1", L"1.0", L"gg", L"123z", L"0xz"}) {
        CommandArgumentReader reader(L"hash", text, L"MemberArguments", &ns, &scope);
        uint64_t result = UINT64_MAX;
        require_member_argument(!reader.GetHash(&result) && result == 0,
            "member Hash: malformed/overflow token fails with reset output");
        result = UINT64_MAX;
        require_member_argument(!reader.GetHash(&result) && result == 0,
            "member Hash: repeated failed read does not consume invalid token");
    }
    CommandArgumentReader sequence(L"hash", L"0xffffffffffffffff, 0x0, 123z", L"MemberArguments", &ns, &scope);
    uint64_t result = 0;
    require_member_argument(sequence.GetHash(&result) && result == UINT64_MAX && !sequence.Finished() &&
        sequence.ConsumeSeparator(SeparatorMode::Comma) && sequence.GetHash(&result) && result == 0 &&
        sequence.ConsumeSeparator(SeparatorMode::Comma), "member Hash: comma-separated tokens retain reader position");
    result = UINT64_MAX;
    require_member_argument(!sequence.GetHash(&result) && result == 0 && !sequence.Error().empty(),
        "member Hash: trailing invalid token reports error without a partial value");
    CommandArgumentReader whitespace(L"hash", L"a b", L"MemberArguments", &ns, &scope);
    require_member_argument(whitespace.GetHash(&result) && result == 10 && !whitespace.Finished(),
        "member Hash: a token read does not silently accept remaining arguments");
}
