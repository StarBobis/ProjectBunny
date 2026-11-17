namespace {
class TextureInitialDebugLogScope {
    FILE* saved_file=LogFile;
    LogVerbosity saved_level=gLogVerbosity;
public:
    FILE* capture=std::tmpfile();
    explicit TextureInitialDebugLogScope(bool enabled=true)
    {
        require_batch(capture!=nullptr,"F177: actual diagnostic file capture");
        LogFile=capture; gLogVerbosity=enabled ? LogVerbosity::DEBUG : LogVerbosity::INFO;
    }
    ~TextureInitialDebugLogScope() {LogFile=saved_file; gLogVerbosity=saved_level; std::fclose(capture);}
    std::string text()
    {
        require_batch(!std::fflush(capture) && !std::fseek(capture,0,SEEK_SET),"F177: read completed native diagnostic stream");
        std::string result; char buffer[1024]; size_t count=0;
        while((count=std::fread(buffer,1,sizeof(buffer),capture))!=0) result.append(buffer,count);
        return result;
    }
};
class GuardedTextureInitialSpan {
    void* allocation=nullptr;
public:
    uint8_t* data=nullptr;
    explicit GuardedTextureInitialSpan(size_t size)
    {
        SYSTEM_INFO info{}; GetSystemInfo(&info);
        require_batch(size<=info.dwPageSize,"F177: texture fixture fits its accessible page");
        allocation=VirtualAlloc(nullptr,info.dwPageSize*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        require_batch(allocation!=nullptr,"F177: actual guarded texture data");
        auto* end=static_cast<uint8_t*>(allocation)+info.dwPageSize; DWORD previous=0;
        if(!VirtualProtect(end,info.dwPageSize,PAGE_NOACCESS,&previous)) {
            VirtualFree(allocation,0,MEM_RELEASE); allocation=nullptr;
            throw std::runtime_error("F177: protect the byte after the declared texture span");
        }
        data=end-size;
        for(size_t i=0;i<size;++i) data[i]=static_cast<uint8_t>(0xa0+i);
    }
    ~GuardedTextureInitialSpan() {if(allocation) VirtualFree(allocation,0,MEM_RELEASE);}
};
struct TextureInitialDebugCase {
    DXGI_FORMAT format;
    UINT width,height,pitch;
    size_t readable;
};

void verify_initial_debug_layout(const TextureInitialDebugCase& test)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=test.width; desc.Height=test.height;
    desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1; desc.Format=test.format;
    GuardedTextureInitialSpan bytes(test.readable);
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=bytes.data; initial.SysMemPitch=test.pitch;
    TextureInitialDebugLogScope log;
    LogDebugTexture2DInitialData(&desc,&initial);
    const auto text=log.text();
    if(!test.readable) {
        require_batch(text.find("[data bytes unavailable]\n")!=std::string::npos,
            "F177: unproved or invalid layout logs metadata without accessing data");
        return;
    }
    std::string payload;
    for(size_t i=0;i<test.readable;++i) {char value[4]{}; std::snprintf(value,sizeof(value)," %02X",static_cast<unsigned>(bytes.data[i])); payload+=value;}
    require_batch(text.size()>=payload.size()+1 && text.substr(text.size()-payload.size()-1)==payload+"\n",
        "F177: exact legal preview bytes survive format, final-row and padding boundaries");
}
}

void test_dx11_texture_initial_debug_regression()
{
    BatchDevice device;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=desc.Height=desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8_UNORM; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    GuardedTextureInitialSpan bytes(1);
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=bytes.data; initial.SysMemPitch=1;
    for(const bool enabled:{false,true}) {
        TextureInitialDebugLogScope log(enabled);
        ComPtr<ID3D11Texture2D> result;
        std::fprintf(stderr,"F177: native 1-byte R8 input with debug=%d ends at an inaccessible page\n",enabled); std::fflush(stderr);
        require_batch(SUCCEEDED(device.device->CreateTexture2D(&desc,&initial,&result)) && result,
            "F177: optional debug bytes cannot break actual native texture creation");
        D3D11_TEXTURE2D_DESC staging_desc=desc;
        staging_desc.BindFlags=0; staging_desc.Usage=D3D11_USAGE_STAGING; staging_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        require_batch(SUCCEEDED(device.native->CreateTexture2D(&staging_desc,nullptr,&staging)),"F177: native readback resource");
        device.context->CopyResource(staging.Get(),result.Get()); D3D11_MAPPED_SUBRESOURCE mapped{};
        require_batch(SUCCEEDED(device.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"F177: actual created texture readback");
        const bool unchanged=*static_cast<const uint8_t*>(mapped.pData)==0xa0;
        device.context->Unmap(staging.Get(),0);
        require_batch(unchanged,"F177: diagnostics preserve actual texture bytes");
        const auto text=log.text();
        require_batch(enabled ? text.find(" A0\n")!=std::string::npos : text.find("  pInitialData = ")==std::string::npos,
            "F177: debug-enabled output retains the one legal byte, disabled output does not read diagnostic data");
    }
    const TextureInitialDebugCase layouts[]={
        {DXGI_FORMAT_R8_UNORM,1,1,4096,1},
        {DXGI_FORMAT_R8_UNORM,2,3,2,6},
        {DXGI_FORMAT_R8_UNORM,2,3,8,2},
        {DXGI_FORMAT_R8G8B8A8_UNORM,4,1,16,16},
        {DXGI_FORMAT_R8G8B8A8_UNORM,2,2,8,16},
        {DXGI_FORMAT_BC1_UNORM,1,1,8,8},
        {DXGI_FORMAT_BC7_UNORM,1,1,16,16},
        {DXGI_FORMAT_YUY2,2,1,4,4},
        {DXGI_FORMAT_NV12,2,2,2,6},
        {DXGI_FORMAT_P010,2,2,4,12},
        {DXGI_FORMAT_NV11,4,1,4,4},
        {DXGI_FORMAT_420_OPAQUE,2,2,2,0},
        {DXGI_FORMAT_UNKNOWN,1,1,1,0},
        {DXGI_FORMAT_R8_UNORM,0,1,1,0},
        {DXGI_FORMAT_R8G8B8A8_UNORM,1,1,1,0},
        {DXGI_FORMAT_R8_UNORM,UINT_MAX,UINT_MAX,UINT_MAX,0},
        {DXGI_FORMAT_NV12,3,2,4,0}};
    for(const auto& test:layouts) verify_initial_debug_layout(test);
    GuardedTextureInitialSpan inaccessible(0);
    D3D11_SUBRESOURCE_DATA no_read{}; no_read.pSysMem=inaccessible.data; no_read.SysMemPitch=1;
    {
        TextureInitialDebugLogScope disabled(false);
        LogDebugTexture2DInitialData(&desc,&no_read);
        require_batch(disabled.text().empty(),"F177: disabled diagnostic function does not touch inaccessible data");
    }
    {
        TextureInitialDebugLogScope enabled;
        LogFile=nullptr; LogDebugTexture2DInitialData(&desc,&no_read); LogFile=enabled.capture;
        require_batch(enabled.text().empty(),"F177: absent log file does not touch inaccessible data");
    }
    verify_initial_debug_layout({DXGI_FORMAT_R8G8B8A8_UNORM,4,1,16,16});
}
