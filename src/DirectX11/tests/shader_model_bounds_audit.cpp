#include "slot_range_test_support.h"
#include "ShaderRegex.h"

#include <array>
#include <cstdint>
#include <limits>

namespace {
using namespace dx11_slot_test;
using Bytes = std::vector<unsigned char>;

void write_word(Bytes& bytes, size_t offset, uint32_t value)
{
    require_batch(offset <= bytes.size() && bytes.size() - offset >= sizeof(value), "F201: bounded metadata fixture write");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

Bytes metadata(const std::vector<uint32_t>& versions)
{
    const size_t table_end = 32 + 4 * versions.size();
    Bytes bytes(table_end + 12 * versions.size());
    std::memcpy(bytes.data(), "DXBC", 4);
    write_word(bytes, 20, 1);
    write_word(bytes, 24, static_cast<uint32_t>(bytes.size()));
    write_word(bytes, 28, static_cast<uint32_t>(versions.size()));
    for (size_t i = 0; i < versions.size(); ++i) {
        const size_t offset = table_end + 12 * i;
        write_word(bytes, 32 + 4 * i, static_cast<uint32_t>(offset));
        std::memcpy(bytes.data() + offset, i % 2 ? "SHEX" : "SHDR", 4);
        write_word(bytes, offset + 4, 4);
        write_word(bytes, offset + 8, versions[i]);
    }
    return bytes;
}

void expect_model(const void* bytes, size_t size, const char* expected, const char* message)
{
    std::string model = "unchanged";
    const bool found = get_shader_model_from_bytecode(bytes, size, &model);
    require_batch(expected ? found && model == expected : !found && model == "unchanged", message);
}

void expect_model(const Bytes& bytes, const char* expected, const char* message)
{
    expect_model(bytes.data(), bytes.size(), expected, message);
}

// Reserve address space so each wrapping uint32_t offset lands in a known
// inaccessible page rather than depending on the process's other allocations.
class GuardedOffsets {
    void* allocation = nullptr;
public:
    unsigned char* data = nullptr;
    explicit GuardedOffsets(const Bytes& bytes)
    {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        const size_t page = info.dwPageSize;
        require_batch(bytes.size() < page, "F201: metadata fits one committed page");
        const size_t reserve = static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) + 1 + 2 * page;
        allocation = VirtualAlloc(nullptr, reserve, MEM_RESERVE, PAGE_NOACCESS);
        require_batch(allocation != nullptr, "F201: reserve guarded offset address space without committing it");
        if (!VirtualAlloc(allocation, page, MEM_COMMIT, PAGE_READWRITE)) {
            VirtualFree(allocation, 0, MEM_RELEASE);
            allocation = nullptr;
            throw std::runtime_error("F201: commit only the metadata fixture page");
        }
        data = static_cast<unsigned char*>(allocation) + page - bytes.size();
        if (!bytes.empty()) std::memcpy(data, bytes.data(), bytes.size());
    }
    ~GuardedOffsets() { if (allocation) VirtualFree(allocation, 0, MEM_RELEASE); }
    GuardedOffsets(const GuardedOffsets&) = delete;
    GuardedOffsets& operator=(const GuardedOffsets&) = delete;
};

void test_metadata_bounds()
{
    const auto valid = metadata({0x50});
    expect_model(valid, "ps_5_0", "F201: an exactly bounded SHDR version token remains sufficient");
    expect_model(nullptr, valid.size(), nullptr, "F201: null data preserves failure output");
    require_batch(!get_shader_model_from_bytecode(valid.data(), valid.size(), nullptr), "F201: null output is rejected");

    for (size_t size = 0; size < valid.size(); ++size) {
        GuardedOffsets truncated(Bytes(valid.begin(), valid.begin() + size));
        expect_model(truncated.data, size, nullptr, "F201: every truncated header, table and version token is rejected");
    }
    GuardedOffsets guarded(valid);
    expect_model(guarded.data, valid.size(), "ps_5_0", "F201: final version byte at the guard boundary is readable");

    Bytes unaligned(valid.size() + 1);
    std::copy(valid.begin(), valid.end(), unaligned.begin() + 1);
    expect_model(unaligned.data() + 1, valid.size(), "ps_5_0", "F201: unaligned input retains memcpy-based metadata reads");

    auto altered = valid;
    altered[0] = 'x';
    expect_model(altered, nullptr, "F201: wrong container tag fails without changing output");
    for (const uint32_t count : {0u, 5u, 0x80000000u, 0xffffffffu}) {
        altered = valid;
        write_word(altered, 28, count);
        expect_model(altered, nullptr, "F201: absent or incomplete chunk tables reject wide counts before iteration");
    }

    for (const uint32_t offset : {37u, 44u, 48u, 0xfffffff4u, 0xfffffff8u, 0xfffffffcu, 0xffffffffu}) {
        altered = valid;
        write_word(altered, 32, offset);
        GuardedOffsets bad(altered);
        std::fprintf(stderr, "F201: reject offset 0x%08x in %zu bytes\n", offset, altered.size());
        std::fflush(stderr);
        expect_model(bad.data, altered.size(), nullptr, "F201: short and wrapping chunk offsets never read outside the supplied size");
    }

    for (uint32_t kind = 0; kind <= 6; ++kind) {
        const std::array<const char*, 7> models{{"ps_5_1", "vs_5_1", "gs_5_1", "hs_5_1", "ds_5_1", "cs_5_1", "xx_5_1"}};
        expect_model(metadata({(kind << 16) | 0x51}), models[kind], "F201: preserve all stage mappings including unknown xx");
    }
    expect_model(metadata({0xffffffffu}), "xx_15_15", "F201: preserve complete version-nibble extraction");
    auto multiple = metadata({0x10050, 0x50050});
    expect_model(multiple, "cs_5_0", "F201: last code chunk remains the preferred model");
    write_word(multiple, 36, 0xfffffff8u);
    GuardedOffsets skipped(multiple);
    expect_model(skipped.data, multiple.size(), "vs_5_0", "F201: invalid last offset is skipped before selecting earlier code");
    write_word(multiple, 36, 52);
    std::memcpy(multiple.data() + 52, "RDEF", 4);
    expect_model(multiple, "vs_5_0", "F201: non-code last chunk is skipped");
    altered = valid;
    std::memcpy(altered.data() + 36, "RDEF", 4);
    expect_model(altered, nullptr, "F201: a complete container without a code chunk leaves output unchanged");

    // This API extracts metadata; it does not validate checksums, the declared
    // container length, payload length or the instruction stream.
    altered = valid;
    write_word(altered, 20, 0);
    write_word(altered, 24, 0);
    write_word(altered, 40, 0xffffffffu);
    expect_model(altered, "ps_5_0", "F201: retain existing metadata-only extraction policy");
}

void test_native_models()
{
    BatchDevice device;
    const char pixel[] = "float4 main():SV_Target{return float4(0.25,0.5,0.75,1);}";
    for (const char* target : {"ps_4_0", "ps_5_0"}) {
        ComPtr<ID3DBlob> code, errors;
        require_batch(SUCCEEDED(D3DCompile(pixel, sizeof(pixel) - 1, nullptr, nullptr, nullptr, "main", target, 0, 0, &code, &errors)),
            "F201: native compiler creates SHDR/SHEX pixel shaders");
        expect_model(code->GetBufferPointer(), code->GetBufferSize(), target, "F201: real native bytecode retains its exact shader model");
        ComPtr<ID3D11PixelShader> shader;
        require_batch(SUCCEEDED(device.native->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader)),
            "F201: actual WARP accepts the unmodified native pixel bytecode");
    }

    const char vertex[] = "float4 main(float4 p:POSITION):SV_Position{return p;}";
    ComPtr<ID3DBlob> code, errors;
    require_batch(SUCCEEDED(D3DCompile(vertex, sizeof(vertex) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, &errors)),
        "F201: native vertex compiler fixture");
    expect_model(code->GetBufferPointer(), code->GetBufferSize(), "vs_5_0", "F201: native vertex model extraction");
    ComPtr<ID3D11VertexShader> vs;
    require_batch(SUCCEEDED(device.native->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs)),
        "F201: actual WARP accepts native vertex bytecode");

    const char compute[] = "RWStructuredBuffer<uint> outbuf:register(u0);[numthreads(1,1,1)]void main(uint3 id:SV_DispatchThreadID){outbuf[id.x]=7;}";
    code.Reset(); errors.Reset();
    require_batch(SUCCEEDED(D3DCompile(compute, sizeof(compute) - 1, nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &code, &errors)),
        "F201: native compute compiler fixture");
    expect_model(code->GetBufferPointer(), code->GetBufferSize(), "cs_5_0", "F201: native compute model extraction");
    ComPtr<ID3D11ComputeShader> cs;
    require_batch(SUCCEEDED(device.native->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &cs)),
        "F201: actual WARP accepts native compute bytecode");
}
}

void test_dx11_shader_model_bounds_regression()
{
    test_native_models();
    test_metadata_bounds();
}
