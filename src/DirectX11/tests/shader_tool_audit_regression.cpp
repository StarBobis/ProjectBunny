// Compile the actual standalone tool in this test TU with its entry and log
// globals renamed; its file-reading algorithm is not copied into a mock.
#include "../../D3D_Shaders/stdafx.h"
#include <map>
#include <iostream>
#include <direct.h>
#include <memory>
#include <stdexcept>
#include <filesystem>
#include <limits>
#include <climits>
namespace {
int __cdecl audit_tool_seek(FILE*,__int64,int);
__int64 __cdecl audit_tool_tell(FILE*);
size_t __cdecl audit_tool_read(void*,size_t,size_t,FILE*);
int __cdecl audit_tool_close(FILE*);
}
#undef _tmain
#define _tmain audit_shader_tool_main
#define LogFile audit_shader_tool_log
#define gLogDebug audit_shader_tool_debug
#define _fseeki64 audit_tool_seek
#define _ftelli64 audit_tool_tell
#define fread audit_tool_read
#define fclose audit_tool_close
#include "../../D3D_Shaders/Shaders.cpp"
#undef _tmain
#undef LogFile
#undef gLogDebug
#undef _fseeki64
#undef _ftelli64
#undef fread
#undef fclose

namespace {
void require_tool(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
enum class ToolReadFault {NoFault,SeekEndFailure,RewindFailure,NegativeLength,HugeLength,ShortRead,CloseFailure,LargeRewindFailure};
ToolReadFault tool_fault=ToolReadFault::NoFault;
unsigned tool_seek_calls=0,tool_close_calls=0;
const auto native_tool_seek=&_fseeki64;
const auto native_tool_tell=&_ftelli64;
const auto native_tool_read=&fread;
const auto native_tool_close=&fclose;
class ToolReadFaultScope {
    ToolReadFault saved=tool_fault;
public:
    explicit ToolReadFaultScope(ToolReadFault fault) {tool_fault=fault; tool_seek_calls=tool_close_calls=0;}
    ~ToolReadFaultScope() {tool_fault=saved;}
};
int __cdecl audit_tool_seek(FILE* stream,__int64 offset,int origin)
{
    ++tool_seek_calls;
    if((tool_fault==ToolReadFault::SeekEndFailure && tool_seek_calls==1) ||
        ((tool_fault==ToolReadFault::RewindFailure || tool_fault==ToolReadFault::LargeRewindFailure) && tool_seek_calls==2)) return -1;
    return native_tool_seek(stream,offset,origin);
}
__int64 __cdecl audit_tool_tell(FILE* stream)
{
    if(tool_fault==ToolReadFault::NegativeLength) return -1;
    if(tool_fault==ToolReadFault::HugeLength) return (std::numeric_limits<__int64>::max)();
    if(tool_fault==ToolReadFault::LargeRewindFailure) return static_cast<__int64>(INT_MAX)+1;
    return native_tool_tell(stream);
}
size_t __cdecl audit_tool_read(void* output,size_t width,size_t count,FILE* stream)
{
    if(tool_fault==ToolReadFault::ShortRead && count) --count;
    return native_tool_read(output,width,count,stream);
}
int __cdecl audit_tool_close(FILE* stream)
{
    ++tool_close_calls;
    const int result=native_tool_close(stream);
    return tool_fault==ToolReadFault::CloseFailure ? EOF : result;
}
void create_tool_input(const char* name,const std::string& content)
{
    HANDLE file=CreateFileA(name,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    require_tool(file!=INVALID_HANDLE_VALUE,"tool audit: exclusive native input fixture");
    DWORD written=0;
    const bool complete=content.empty() || (WriteFile(file,content.data(),static_cast<DWORD>(content.size()),&written,nullptr) && written==content.size());
    const bool closed=CloseHandle(file)!=FALSE;
    require_tool(complete && closed,"tool audit: complete native input fixture");
}
class ToolDirectoryScope {
    std::filesystem::path saved=std::filesystem::current_path();
public:
    ToolDirectoryScope()
    {
        bool under_build=false;
        for(const auto& part:saved) under_build|=_wcsicmp(part.c_str(),L"build")==0;
        require_tool(under_build,"tool audit: working directory belongs to the build fixtures");
        const auto path=saved/L"standalone-tool-input-audit";
        require_tool(std::filesystem::create_directory(path),"tool audit: create an exclusive child directory");
        std::filesystem::current_path(path);
    }
    ~ToolDirectoryScope() {std::error_code error; std::filesystem::current_path(saved,error); if(error) std::abort();}
};
}

void test_dx11_shader_tool_read_regression()
{
    ToolDirectoryScope directory;
    const std::string contents("A\0BC\xff\x01Z",7);
    create_tool_input("input.bin",contents); create_tool_input("empty.txt","");
    bool rejected=false;
    std::fputs("F148: missing input must fail before invoking the shader parser\n",stderr); std::fflush(stderr);
    try {(void)readFile("missing.bin");} catch(const std::exception&) {rejected=true;}
    require_tool(rejected,"F148: missing input cannot masquerade as a successful empty vector");
    const auto normal=readFile("input.bin");
    require_tool(std::string(reinterpret_cast<const char*>(normal.data()),normal.size())==contents && readFile("empty.txt").empty(),
        "F148: exact binary bytes and successful zero-byte reads retain their meanings");
    for(const auto fault:{ToolReadFault::SeekEndFailure,ToolReadFault::RewindFailure,ToolReadFault::NegativeLength,
            ToolReadFault::HugeLength,ToolReadFault::ShortRead,ToolReadFault::CloseFailure,ToolReadFault::LargeRewindFailure}) {
        ToolReadFaultScope active(fault); rejected=false;
        try {(void)readFile("input.bin");} catch(const std::exception&) {rejected=true;}
        std::fprintf(stderr,"F148 fault %d: rejected=%d seeks=%u closes=%u\n",static_cast<int>(fault),rejected,tool_seek_calls,tool_close_calls);
        std::fflush(stderr);
        require_tool(rejected && tool_close_calls==1,"F148: seek/tell/size/allocation/short-read/close failure reports an error and closes exactly once");
        if(fault==ToolReadFault::LargeRewindFailure)
            require_tool(tool_seek_calls==2,"F148: 2-GiB length remains representable and reaches checked rewind without allocating it");
    }
    tool_fault=ToolReadFault::NoFault;
    const auto recovered=readFile("input.bin");
    require_tool(recovered==normal,"F148: real reading recovers after each scoped fault");
}
