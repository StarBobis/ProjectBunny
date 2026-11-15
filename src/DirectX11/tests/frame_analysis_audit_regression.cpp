// Compile production frame analysis once in the regression host. The actual
// DLL continues compiling FrameAnalysis.cpp without test hooks.
#include "../FrameAnalysis.cpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

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
    dump_vb_elem(zero_stride_element.file, memory.data, 32, &element, 1, 0, 0, 0, 0);
    check_vertex_dump(zero_stride_element.text().find("DATA: 1") != std::string::npos,
        "F008: element bounds do not invent a width-versus-stride rejection (public stride-zero policy stays unchanged)");
    VertexDumpCapture no_bytes;
    dump_vb_elem(no_bytes.file, memory.data + 32, 0, &element, 1, 0, 0, 0, 4);
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
