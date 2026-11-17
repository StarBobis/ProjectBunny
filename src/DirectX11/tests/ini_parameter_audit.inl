// Included where the real configuration fixture and production parsers exist.
class IniParameterStateScope {
    decltype(G->iniParams) old_params = G->iniParams;
    int old_reserved = G->iniParamsReserved;
    bool old_run = G->constants_run;
public:
    IniParameterStateScope()
    {
        require_batch(CurrentTransition.params.empty() && CurrentTransition.vars.empty() && pending_post_command_lists.empty(),
            "F007: no unrelated active transitions in the fixture");
        G->iniParams.clear(); G->iniParamsReserved = 0;
    }
    ~IniParameterStateScope()
    {
        OverrideSave.Reset(nullptr); pending_post_command_lists.clear();
        G->iniParams = std::move(old_params); G->iniParamsReserved = old_reserved; G->constants_run = old_run;
    }
};
class IniParameterDeviceResources {
    BatchDevice& device;
public:
    explicit IniParameterDeviceResources(BatchDevice& owner) : device(owner) {}
    ~IniParameterDeviceResources()
    {
        OverrideSave.Reset(nullptr); pending_post_command_lists.clear();
        device.wrapper->ClearState();
        if (device.device->mIniResourceView) { device.device->mIniResourceView->Release(); device.device->mIniResourceView = nullptr; }
        if (device.device->mIniTexture) { device.device->mIniTexture->Release(); device.device->mIniTexture = nullptr; }
    }
};
struct IniParameterNameExpectation {
    const wchar_t* name;
    int index;
    float DirectX::XMFLOAT4::*component;
};

std::vector<DirectX::XMFLOAT4> read_ini_parameter_texture(BatchDevice& device)
{
    require_batch(device.device->mIniTexture != nullptr, "F007: production IniParams texture exists");
    D3D11_TEXTURE1D_DESC desc{}; device.device->mIniTexture->GetDesc(&desc);
    require_batch(desc.Width == G->iniParams.size() && desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT,
        "F007: GPU parameter extent agrees with the real CPU reservation");
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture1D> staging;
    require_batch(SUCCEEDED(device.native->CreateTexture1D(&desc, nullptr, &staging)), "F007: actual parameter readback texture");
    device.context->CopyResource(staging.Get(), device.device->mIniTexture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "F007: read actual uploaded parameters");
    std::vector<DirectX::XMFLOAT4> values(desc.Width);
    std::memcpy(values.data(), mapped.pData, values.size() * sizeof(values[0]));
    device.context->Unmap(staging.Get(), 0);
    return values;
}

void test_f007()
{
    const IniParameterNameExpectation valid[] = {
        {L"x", 0, &DirectX::XMFLOAT4::x}, {L"Y", 0, &DirectX::XMFLOAT4::y},
        {L"z000003", 3, &DirectX::XMFLOAT4::z}, {L"w225", 225, &DirectX::XMFLOAT4::w},
        {L"x100000", 100000, &DirectX::XMFLOAT4::x}, {L"X2147483646", INT_MAX-1, &DirectX::XMFLOAT4::x},
        {L"x+3", 3, &DirectX::XMFLOAT4::x}, {L"w  +4", 4, &DirectX::XMFLOAT4::w}, {L"x-0", 0, &DirectX::XMFLOAT4::x}
    };
    for (const auto& item : valid) {
        int index = -99; float DirectX::XMFLOAT4::*component = nullptr;
        require_batch(ParseIniParamName(item.name, &index, &component) && index == item.index && component == item.component,
            "F007: preserve component case, positive scanf forms, zero, leading zeros and the DX11 range");
    }
    for (const auto* invalid : {L"x-1", L"y-2147483648", L"z2147483647", L"w2147483648", L"x4294967295",
            L"x4294967296", L"x18446744073709551615", L"x99999999999999999999999999", L"z1trailing", L"", L"abc", L"x ", L"x+", L" x1"}) {
        int index = 9; float DirectX::XMFLOAT4::*component = &DirectX::XMFLOAT4::x;
        std::fputs("F007: reject negative/overflowing or incomplete parameter name\n", stderr); std::fflush(stderr);
        require_batch(!ParseIniParamName(invalid, &index, &component), "F007: invalid parameter cannot enter any consumer or overflow index+1");
    }
    int index = 9; float DirectX::XMFLOAT4::*component = nullptr;
    require_batch(!ParseIniParamName(nullptr, &index, &component) && !ParseIniParamName(L"x", nullptr, &component) &&
        !ParseIniParamName(L"x", &index, nullptr), "F007: reject missing parser inputs/outputs");

    IniParameterStateScope state; AuditConfigScope config;
    config.load("x-1=100\ny4294967295=100\nz2147483647=100\nw999999999999999999999999=100\n"
        "x=3\ny=9\nx3=5\n"
        "[PresetParamBounds]\nx-1=7\ny4294967295=7\nx3=8\n"
        "[PresetPendingLarge]\nx3=30\ntransition=100000\n"
        "[KeyParamBounds]\nkey=VK_F24\ntype=cycle\nx-1=1,2\ny4294967295=1,2\nx3=2,4\n", 0, true);
    require_batch(G->iniParamsReserved == 4, "F007: Constants, presets and cycle parsing reserve only valid indices");
    auto& preset = presetOverrides.at(L"presetparambounds");
    require_batch(preset.mOverrideParams.size() == 1 && preset.mOverrideParams.begin()->first.idx == 3,
        "F007: invalid preset names never reach saved overrides or transitions");
    BatchDevice device; IniParameterDeviceResources resources(device);
    require_batch(SUCCEEDED(device.device->CreateIniParamResources()), "F007: actual production parameter allocation");
    device.wrapper->Bind3DMigotoResources(); device.wrapper->InitIniParams();
    require_batch(G->iniParams.size() == 4 && G->iniParams[0].x == 3 && G->iniParams[3].x == 5,
        "F007: actual Constants list executes valid assignments after rejecting malformed names");

    CommandList commands; const std::wstring ns;
    std::wstring value = L"x3+x-1";
    require_batch(ParseCommandListIniParamOverride(L"CommandListParamBounds", L"z2", &value, &commands, &ns),
        "F007: subtraction remains expression syntax and valid indexed reads still parse");
    RunCommandList(device.device, device.wrapper.get(), &commands, nullptr, false);
    require_batch(G->iniParams[2].z == 7 && read_ini_parameter_texture(device)[2].z == 7,
        "F007: expression reads and override writes reach the correct CPU/GPU components");
    for (const auto* bad : {L"x-1", L"y4294967295", L"z2147483647"}) {
        CommandList invalid; std::wstring one = L"1";
        require_batch(!ParseCommandListIniParamOverride(L"CommandListParamBounds", bad, &one, &invalid, &ns) && invalid.commands.empty(),
            "F007: public assignment parser does not publish an invalid command");
    }
    CommandListExpression invalid_read; std::wstring bad_read = L"x4294967295";
    require_batch(!invalid_read.parse(&bad_read, &ns, nullptr) && G->iniParamsReserved == 4,
        "F007: overflowing expression operand does not reserve or publish an indexed reader");

    require_batch(!preset.MatchesCurrent(device.device), "F007: preset matcher reads the valid current component");
    preset.Activate(device.device, true); CurrentTransition.UpdateTransitions(device.device);
    require_batch(preset.MatchesCurrent(device.device) && G->iniParams[3].x == 8 && read_ini_parameter_texture(device)[3].x == 8,
        "F007: saved override reads and scheduled transition writes use valid indices");
    preset.Deactivate(device.device); CurrentTransition.UpdateTransitions(device.device);
    require_batch(G->iniParams[3].x == 5, "F007: deactivation restores the saved parameter");
    KeyOverrideCycle cycle; cycle.ParseIniSection(L"KeyParamBounds");
    cycle.DownEvent(device.device); CurrentTransition.UpdateTransitions(device.device);
    require_batch(G->iniParams[3].x == 2, "F007: cycle first value excludes invalid indexed keys");
    cycle.DownEvent(device.device); CurrentTransition.UpdateTransitions(device.device);
    require_batch(G->iniParams[3].x == 4 && read_ini_parameter_texture(device)[3].x == 4, "F007: smart cycle matches and advances valid values");
    presetOverrides.at(L"presetpendinglarge").Activate(device.device, true);
    require_batch(!CurrentTransition.params.empty(), "F007: real pending transition exists before shrinking reload");

    std::ofstream output(config.config, std::ios::binary | std::ios::trunc);
    output << "[Logging]\ncalls=0\nshow_warnings=0\n[System]\nskip_early_includes_load=0\nconfig_initialization_delay=-1\n"
        "[Hunting]\nhunting=0\n[Include]\nuser_config=" << config.settings.filename().string() << "\n[Constants]\nx=11\n";
    output.close(); require_batch(output.good(), "F007: write corrected smaller configuration");
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.config.c_str());
    ReloadConfig(device.device);
    require_batch(G->iniParamsReserved == 1 && G->iniParams.size() == 1 && CurrentTransition.params.empty() && OverrideSave.params.empty(),
        "F007: production reload clears old transitions/saves before shrinking CPU/GPU storage");
    CurrentTransition.UpdateTransitions(device.device);
    require_batch(G->iniParams[0].x == 11 && read_ini_parameter_texture(device)[0].x == 11,
        "F007: later transition update cannot write a stale larger index after reload");
}
