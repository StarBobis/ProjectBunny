// Compile production frame analysis once in the regression host. The actual
// DLL continues compiling FrameAnalysis.cpp without test hooks.
#include "../FrameAnalysis.cpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <array>
#include "slot_range_test_support.h"

void test_dx11_vertex_dump_gpu_regression();

namespace {
void check_vertex_dump(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
class GuardedVertexBytes {
    uint8_t* allocation = nullptr;
public:
    uint8_t* data = nullptr;
    explicit GuardedVertexBytes(size_t bytes)
    {
        SYSTEM_INFO system{}; GetSystemInfo(&system);
        check_vertex_dump(bytes <= system.dwPageSize, "F008: guarded byte range fits one page");
        allocation = static_cast<uint8_t*>(VirtualAlloc(nullptr, system.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        check_vertex_dump(allocation != nullptr, "F008: allocate actual guarded memory");
        DWORD protection = 0;
        if (!VirtualProtect(allocation + system.dwPageSize, system.dwPageSize, PAGE_NOACCESS, &protection)) {
            VirtualFree(allocation, 0, MEM_RELEASE); allocation = nullptr;
            throw std::runtime_error("F008: establish inaccessible trailing page");
        }
        data = allocation + system.dwPageSize - bytes;
    }
    ~GuardedVertexBytes() { if (allocation) VirtualFree(allocation, 0, MEM_RELEASE); }
};
class VertexDumpCapture {
public:
    FILE* file = std::tmpfile();
    VertexDumpCapture() { check_vertex_dump(file != nullptr, "F008: capture actual decoder output"); }
    ~VertexDumpCapture() { std::fclose(file); }
    std::string text()
    {
        check_vertex_dump(!std::fflush(file) && !std::fseek(file, 0, SEEK_SET), "F008: flush and rewind actual text");
        std::string result; char block[1024]; size_t count;
        while ((count = std::fread(block, 1, sizeof(block), file)) != 0) result.append(block, count);
        return result;
    }
};

struct NativeVertexCapture {
    Microsoft::WRL::ComPtr<HackerInputLayout> layout;
    std::array<float,4> value{};
};
NativeVertexCapture capture_native_vertex(dx11_slot_test::BatchDevice& device, const char* vertex_source,
    const D3D11_INPUT_ELEMENT_DESC* elements, UINT element_count, const void* bytes, UINT byte_count, UINT stride)
{
    using Microsoft::WRL::ComPtr;
    const char geometry_source[] = "struct V{float4 p:SV_Position;};[maxvertexcount(1)]void main(point V v[1],inout PointStream<V> s){s.Append(v[0]);}";
    ComPtr<ID3DBlob> vs_code, gs_code, errors;
    check_vertex_dump(SUCCEEDED(D3DCompile(vertex_source, std::strlen(vertex_source), nullptr, nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &vs_code, &errors)), "F324: compile actual vertex inputs");
    check_vertex_dump(SUCCEEDED(D3DCompile(geometry_source, sizeof(geometry_source)-1, nullptr, nullptr, nullptr,
        "main", "gs_5_0", 0, 0, &gs_code, &errors)), "F324: compile actual stream-output capture");
    NativeVertexCapture result;
    ComPtr<ID3D11InputLayout> layout;
    check_vertex_dump(SUCCEEDED(device.device->CreateInputLayout(elements, element_count, vs_code->GetBufferPointer(), vs_code->GetBufferSize(), &layout)) &&
        SUCCEEDED(layout.As(&result.layout)), "F324: production cache creates a real native-valid tracked layout");
    ComPtr<ID3D11VertexShader> vertex;
    check_vertex_dump(SUCCEEDED(device.native->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &vertex)),
        "F324: native vertex shader");
    D3D11_SO_DECLARATION_ENTRY declaration{0,"SV_Position",0,0,4,0};
    UINT output_stride = sizeof(result.value);
    ComPtr<ID3D11GeometryShader> geometry;
    check_vertex_dump(SUCCEEDED(device.native->CreateGeometryShaderWithStreamOutput(gs_code->GetBufferPointer(), gs_code->GetBufferSize(),
        &declaration, 1, &output_stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &geometry)), "F324: native stream-output declaration");
    D3D11_BUFFER_DESC desc{}; desc.ByteWidth = byte_count; desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = bytes;
    ComPtr<ID3D11Buffer> input, output, staging;
    check_vertex_dump(SUCCEEDED(device.native->CreateBuffer(&desc, &initial, &input)), "F324: actual vertex bytes");
    desc.ByteWidth = output_stride; desc.BindFlags = D3D11_BIND_STREAM_OUTPUT;
    const std::array<float,4> sentinel{-101,-102,-103,-104};
    D3D11_SUBRESOURCE_DATA output_initial{}; output_initial.pSysMem = sentinel.data();
    check_vertex_dump(SUCCEEDED(device.native->CreateBuffer(&desc, &output_initial, &output)), "F324: actual initialized stream-output buffer");
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check_vertex_dump(SUCCEEDED(device.native->CreateBuffer(&desc, nullptr, &staging)), "F324: actual output readback");
    device.context->ClearState();
    ID3D11Buffer* bound_input = input.Get(); UINT offset = 0;
    device.context->IASetInputLayout(result.layout->GetOrigInputLayout());
    device.context->IASetVertexBuffers(0, 1, &bound_input, &stride, &offset);
    device.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    device.context->VSSetShader(vertex.Get(), nullptr, 0); device.context->GSSetShader(geometry.Get(), nullptr, 0);
    ID3D11Buffer* bound_output = output.Get();
    device.context->SOSetTargets(1, &bound_output, &offset); device.context->Draw(1, 0);
    device.context->ClearState(); device.context->CopyResource(staging.Get(), output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check_vertex_dump(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "F324: observe native GPU vertex values");
    std::memcpy(result.value.data(), mapped.pData, sizeof(result.value)); device.context->Unmap(staging.Get(), 0);
    return result;
}
}

void test_dx11_vertex_dump_bounds_regression()
{
    GuardedVertexBytes memory(32);
    const float values[] = {1,2,3,4,5,6,7,8};
    std::memcpy(memory.data, values, sizeof(values));
    D3D11_MAPPED_SUBRESOURCE mapped{}; mapped.pData = memory.data;
    D3D11_INPUT_ELEMENT_DESC element{"DATA",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    VertexDumpCapture overlap;
    std::fputs("F008: overlapping float4 elements at the end of the real readable mapping\n", stderr); std::fflush(stderr);
    dump_vb_known_layout(overlap.file, &mapped, &element, 1, 32, 0, 0, 0, 0, 4);
    const auto text = overlap.text();
    check_vertex_dump(text.find("DATA: 1, 2, 3, 4") != std::string::npos &&
        text.find("DATA: 5, 6, 7, 8") != std::string::npos,
        "F008: retain every complete overlapping element, including fields wider than stride");
    check_vertex_dump(text.find("exceeds mapped buffer") != std::string::npos && text.find("vb0[5]+000 DATA:") == std::string::npos,
        "F008: incomplete trailing elements are diagnosed without reading the guard page or emitting partial values");

    element.Format = DXGI_FORMAT_R32_FLOAT; element.AlignedByteOffset = 12;
    VertexDumpCapture beyond_stride;
    dump_vb_known_layout(beyond_stride.file, &mapped, &element, 1, 32, 0, 0, 0, 0, 4);
    const auto offset_text = beyond_stride.text();
    check_vertex_dump(offset_text.find("vb0[0]+012 DATA: 4") != std::string::npos &&
        offset_text.find("vb0[4]+012 DATA: 8") != std::string::npos,
        "F008: explicit offsets beyond stride remain readable while inside the mapped byte range");
    element.AlignedByteOffset = 28;
    VertexDumpCapture exact_end;
    dump_vb_known_layout(exact_end.file, &mapped, &element, 1, 32, 0, 0, 0, 1, 32);
    check_vertex_dump(exact_end.text().find("DATA: 8") != std::string::npos, "F008: exact-end complete elements are retained");
    element.AlignedByteOffset = UINT_MAX - 1;
    VertexDumpCapture overflow_offset;
    dump_vb_known_layout(overflow_offset.file, &mapped, &element, 1, 32, 0, 0, 0, 1, 32);
    check_vertex_dump(overflow_offset.text().find("exceeds mapped buffer") != std::string::npos,
        "F008: extreme offsets cannot wrap the width check or create an invalid read pointer");
    element.AlignedByteOffset = 0;
    VertexDumpCapture range;
    dump_vb_known_layout(range.file, &mapped, &element, 1, 32, 0, 8, 1, 1, 4);
    check_vertex_dump(range.text().find("vb0[0]+000 DATA: 4") != std::string::npos,
        "F008: existing binding offset, first and count selection remain intact");
    VertexDumpCapture zero_stride_element;
    uint64_t element_offset = 0;
    dump_vb_elem(zero_stride_element.file, memory.data, 32, &element, 1, 0, 0, 0, 0, element_offset);
    check_vertex_dump(zero_stride_element.text().find("DATA: 1") != std::string::npos,
        "F008: element bounds do not invent a width-versus-stride rejection (public stride-zero policy stays unchanged)");
    VertexDumpCapture no_bytes;
    element_offset = 0;
    dump_vb_elem(no_bytes.file, memory.data + 32, 0, &element, 1, 0, 0, 0, 4, element_offset);
    check_vertex_dump(no_bytes.text().find("exceeds mapped buffer") != std::string::npos, "F008: zero available bytes never dereference the inaccessible page");

    D3D11_INPUT_ELEMENT_DESC mixed[] = {
        {"BAD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,20,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"GOOD",0,DXGI_FORMAT_R32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0}};
    VertexDumpCapture mixed_output;
    dump_vb_known_layout(mixed_output.file, &mapped, mixed, 2, 32, 0, 0, 0, 1, 32);
    check_vertex_dump(mixed_output.text().find("GOOD: 1") != std::string::npos, "F008: a truncated field does not suppress other complete fields");

    element.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; element.InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
    element.InstanceDataStepRate = 1;
    VertexDumpCapture instances;
    dump_vb_instance_data(instances.file, &mapped, &element, 1, 32, 0, 0, 0, 8, 4);
    check_vertex_dump(instances.text().find("exceeds mapped buffer") != std::string::npos, "F008: instance reads have the same real-byte boundary");
    element.InstanceDataStepRate = 2;
    VertexDumpCapture stepped;
    dump_vb_instance_data(stepped.file, &mapped, &element, 1, 32, 0, 0, 0, 8, 4);
    const auto stepped_text = stepped.text();
    check_vertex_dump(stepped_text.find("DATA: 4, 5, 6, 7") != std::string::npos &&
        stepped_text.find("exceeds mapped buffer") == std::string::npos, "F008: complete stepped instance data is preserved");
    test_dx11_vertex_dump_gpu_regression();
}

void test_dx11_appended_vertex_layout_regression()
{
    dx11_slot_test::BatchDevice device;
    const char source[] = "float4 main(float2 a:DATA0,float2 b:DATA1):SV_Position{return float4(a,b);}";
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"DATA",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"DATA",1,DXGI_FORMAT_R32G32_FLOAT,0,D3D11_APPEND_ALIGNED_ELEMENT,D3D11_INPUT_PER_VERTEX_DATA,0}};
    const float bytes[] = {1,2,3,4};
    const auto native = capture_native_vertex(device, source, elements, ARRAYSIZE(elements), bytes, sizeof(bytes), sizeof(bytes));
    check_vertex_dump(native.value == std::array<float,4>{1,2,3,4}, "F324: native APPEND layout really reads the second pair at byte eight");
    check_vertex_dump(native.layout->GetElements()[1].AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT,
        "F324: the production wrapper retains APPEND metadata for the decoder to resolve");
    const auto original_hash = native.layout->GetLayoutHash();
    std::fprintf(stderr, "F324 native stream output: %.0f, %.0f, %.0f, %.0f\n", native.value[0],native.value[1],native.value[2],native.value[3]);
    GuardedVertexBytes memory(sizeof(bytes)); std::memcpy(memory.data, bytes, sizeof(bytes));
    D3D11_MAPPED_SUBRESOURCE mapped{}; mapped.pData = memory.data;
    VertexDumpCapture dump;
    dump_vb_known_layout(dump.file, &mapped, native.layout->GetElements(), native.layout->GetElementCount(), sizeof(bytes), 0, 0, 0, 1, sizeof(bytes));
    const auto text = dump.text();
    std::fprintf(stderr, "F324 actual text decoder:%s\n", text.c_str());
    check_vertex_dump(text.find("+008 DATA1: 3, 4") != std::string::npos,
        "F324: text decoding must agree with the observed native APPEND vertex values");
    check_vertex_dump(native.layout->GetLayoutHash() == original_hash && native.layout->GetElements()[1].AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT,
        "F324: diagnostic decoding cannot mutate cached layout identity or original descriptors");

    D3D11_INPUT_ELEMENT_DESC all_append[] = {elements[0],elements[1]};
    all_append[0].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
    const auto native_append = capture_native_vertex(device, source, all_append, 2, bytes, sizeof(bytes), sizeof(bytes));
    check_vertex_dump(native_append.value == native.value, "F324: native first-element APPEND starts at zero");
    GuardedVertexBytes two_vertices(32);
    const float two_values[] = {1,2,3,4,5,6,7,8}; std::memcpy(two_vertices.data, two_values, sizeof(two_values));
    mapped.pData = two_vertices.data;
    VertexDumpCapture repeated;
    dump_vb_known_layout(repeated.file, &mapped, native_append.layout->GetElements(), 2, 32, 0, 0, 0, 2, 16);
    const auto repeated_text = repeated.text();
    check_vertex_dump(repeated_text.find("vb0[1]+008 DATA1: 7, 8") != std::string::npos,
        "F324: append cursor resets at each vertex, including an appended first field");

    const char aligned_source[] = "float4 main(uint a:TINY,float b:DATA):SV_Position{return float4(a,b,0,1);}";
    D3D11_INPUT_ELEMENT_DESC aligned[] = {
        {"TINY",0,DXGI_FORMAT_R16_UINT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"DATA",0,DXGI_FORMAT_R32_FLOAT,0,D3D11_APPEND_ALIGNED_ELEMENT,D3D11_INPUT_PER_VERTEX_DATA,0}};
    std::array<unsigned char,8> padded{}; const uint16_t tiny_value = 7; const float number = 3.5f;
    std::memcpy(padded.data(), &tiny_value, sizeof(tiny_value)); std::memcpy(padded.data()+4, &number, sizeof(number));
    const auto native_aligned = capture_native_vertex(device, aligned_source, aligned, 2, padded.data(), static_cast<UINT>(padded.size()), static_cast<UINT>(padded.size()));
    check_vertex_dump(native_aligned.value == std::array<float,4>{7,3.5f,0,1}, "F324: native 16-bit to 32-bit append alignment");
    GuardedVertexBytes padded_mapping(8); std::memcpy(padded_mapping.data, padded.data(), padded.size()); mapped.pData = padded_mapping.data;
    VertexDumpCapture aligned_output;
    dump_vb_known_layout(aligned_output.file, &mapped, native_aligned.layout->GetElements(), 2, 8, 0, 0, 0, 1, 8);
    check_vertex_dump(aligned_output.text().find("+004 DATA: 3.5") != std::string::npos, "F324: text alignment matches the observed native field");

    D3D11_INPUT_ELEMENT_DESC slots[] = {
        {"A",0,DXGI_FORMAT_R32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"OTHER",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"B",0,DXGI_FORMAT_R32_FLOAT,0,D3D11_APPEND_ALIGNED_ELEMENT,D3D11_INPUT_PER_VERTEX_DATA,0}};
    mapped.pData = two_vertices.data;
    VertexDumpCapture slot_output;
    dump_vb_known_layout(slot_output.file, &mapped, slots, 3, 32, 0, 0, 0, 1, 8);
    check_vertex_dump(slot_output.text().find("+004 B: 2") != std::string::npos, "F324: other input slots cannot advance this slot's cursor");
    D3D11_INPUT_ELEMENT_DESC instance[] = {slots[0],slots[2]};
    for (auto& field : instance) { field.InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA; field.InstanceDataStepRate = 2; }
    VertexDumpCapture instance_output;
    dump_vb_instance_data(instance_output.file, &mapped, instance, 2, 32, 0, 0, 0, 4, 8);
    check_vertex_dump(instance_output.text().find("vb0[1]+004 B: 4") != std::string::npos, "F324: instance cursor resets while preserving divisor selection");

    D3D11_INPUT_ELEMENT_DESC truncated[] = {
        {"BAD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NEXT",0,DXGI_FORMAT_R32_FLOAT,0,D3D11_APPEND_ALIGNED_ELEMENT,D3D11_INPUT_PER_VERTEX_DATA,0}};
    GuardedVertexBytes tail(12); std::memcpy(tail.data, two_values, 12); mapped.pData = tail.data;
    VertexDumpCapture truncated_output;
    dump_vb_known_layout(truncated_output.file, &mapped, truncated, 2, 12, 0, 0, 0, 1, 12);
    const auto truncated_text = truncated_output.text();
    check_vertex_dump(truncated_text.find("exceeds mapped buffer") != std::string::npos && truncated_text.find(" NEXT:") == std::string::npos,
        "F324: unreadable elements still advance layout placement without exposing a false in-range appended field");
    truncated[0].AlignedByteOffset = UINT_MAX - 1;
    mapped.pData = two_vertices.data;
    VertexDumpCapture wide_offset;
    dump_vb_known_layout(wide_offset.file, &mapped, truncated, 2, 32, 0, 0, 0, 1, 32);
    check_vertex_dump(wide_offset.text().find(" NEXT:") == std::string::npos, "F324: invalid large layout offsets cannot wrap the append cursor into mapped bytes");
}
