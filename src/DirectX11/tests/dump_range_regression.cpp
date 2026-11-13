#include "slot_range_test_support.h"
#include "FrameAnalysis.h"
#include <filesystem>
#include <fstream>
#include <clocale>
using namespace dx11_slot_test;

namespace {
class DumpOutputScope {
    std::wstring saved_path = G->ANALYSIS_PATH;
    bool saved_analysis = G->analyse_frame;
    bool saved_registered = G->frame_analysis_registered;
    bool saved_foreground_check = G->check_foreground_window;
    std::wstring saved_additional_title = G->additionalForegroundWindowTitle;
    int saved_hunting = G->hunting;
    unsigned saved_frame = G->frame_no;
    FrameAnalysisOptions saved_current = G->cur_analyse_options;
    FrameAnalysisOptions saved_default = G->def_analyse_options;
    std::string saved_locale = std::setlocale(LC_CTYPE, nullptr);
public:
    std::filesystem::path path;
    DumpOutputScope()
    {
        DWORD foreground_process = 0;
        const HWND foreground = GetForegroundWindow();
        if (foreground) GetWindowThreadProcessId(foreground, &foreground_process);
        require_batch(!foreground || foreground_process != GetCurrentProcessId(),
            "dump: headless regression process has no foreground window");
        wchar_t executable[MAX_PATH] = {};
        require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "dump: locate actual test binary");
        const auto base = std::filesystem::weakly_canonical(std::filesystem::path(executable).parent_path());
        bool in_build = false;
        for (const auto& part : base) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        require_batch(in_build, "dump: all native output remains under build/");
        path = base / (L"range_dump_" + std::to_wstring(GetCurrentProcessId()));
        require_batch(path.parent_path() == base && !std::filesystem::exists(path) && std::filesystem::create_directory(path),
            "dump: create exclusive child fixture directory");
        wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, path.c_str());
        // The real writer polls input to permit cancellation. This headless
        // fixture runs in the background, so use its production foreground gate
        // rather than initializing unrelated controller/COM system workers.
        G->check_foreground_window = true; G->additionalForegroundWindowTitle.clear();
        G->analyse_frame = true; G->frame_analysis_registered = true; G->hunting = 1;
        G->frame_no = 100; G->cur_analyse_options = G->def_analyse_options = FrameAnalysisOptions::INVALID;
    }
    ~DumpOutputScope()
    {
        G->analyse_frame = saved_analysis; G->frame_analysis_registered = saved_registered;
        G->check_foreground_window = saved_foreground_check;
        G->additionalForegroundWindowTitle = std::move(saved_additional_title);
        G->hunting = saved_hunting; G->frame_no = saved_frame;
        G->cur_analyse_options = saved_current; G->def_analyse_options = saved_default;
        wcscpy_s(G->ANALYSIS_PATH, MAX_PATH, saved_path.c_str());
        std::setlocale(LC_CTYPE, saved_locale.c_str());
        // The constructor verified this fixed, exclusive path is a child of
        // the build directory. remove_all does not follow directory symlinks.
        std::error_code error; std::filesystem::remove_all(path, error);
        if (error) std::fprintf(stderr, "dump fixture cleanup: %s\n", error.message().c_str());
    }
};
class DumpFixture {
    std::unique_ptr<HackerContext> original;
public:
    BatchDevice device;
    DumpOutputScope output;
    DumpFixture()
    {
        auto analysis = std::make_unique<FrameAnalysisContext>(device.native.Get(), device.context.Get());
        analysis->SetHackerDevice(device.device);
        original = std::move(device.wrapper); device.wrapper = std::move(analysis);
        device.device->SetHackerContext(device.wrapper.get()); device.state.mHackerContext = device.wrapper.get();
    }
    ~DumpFixture()
    {
        // Close the real frame-analysis log before the output scope removes it.
        device.wrapper = std::move(original);
        device.device->SetHackerContext(device.wrapper.get()); device.state.mHackerContext = device.wrapper.get();
    }
};

bool parse_dump(CommandList& output, std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), ::towlower);
    const std::wstring ns;
    CommandList pre, post; CommandListScope scope; scope.emplace_front();
    pre.scope = post.scope = &scope;
    return ParseCommandListGeneralCommands(L"CommandListDumpRange", L"dump", &value, &output, &pre, &post, &ns);
}

void dumped_bytes(const std::filesystem::path& folder, const std::wstring& target, const std::vector<unsigned char>& expected)
{
    size_t found = 0;
    for (const auto& item : std::filesystem::directory_iterator(folder)) {
        const auto name = item.path().filename().wstring();
        if (item.path().extension() != L".buf" || name.find(target) == std::wstring::npos) continue;
        std::ifstream file(item.path(), std::ios::binary);
        std::vector<unsigned char> actual((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        require_batch(actual == expected, "dump: actual named binary payload equals every source byte");
        ++found;
    }
    require_batch(found == 1, "dump: exactly one actual file has the expected raw range label");
}

void exercise_dump(DumpFixture& fixture)
{
    auto& device = fixture.device; Registry registry;
    auto a = buffer(device, 64, D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE);
    auto b = buffer(device, 64, D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE);
    std::vector<unsigned char> bytes_a(64), bytes_b(64);
    for (UINT i = 0; i < 64; ++i) { bytes_a[i] = static_cast<unsigned char>(i * 37 + 11); bytes_b[i] = static_cast<unsigned char>(i * 51 + 7); }
    device.context->UpdateSubresource(b.Get(), 0, nullptr, bytes_b.data(), 0, 0);
    auto& pool = registry.pool(L"pooldump_read", device, PoolIndexType::FIFO, 4);
    std::vector<CustomResource*> keys;
    for (UINT i = 0; i < 4; ++i) {
        auto* resource = pool.GetResource(static_cast<float>(100 + i), false, false, true);
        assign(device, resource, i % 2 ? b.Get() : a.Get(), nullptr, 0, 64, DXGI_FORMAT_R32_UINT);
        keys.push_back(resource);
    }
    G->frame_no = 101;
    CommandList negative;
    require_batch(parse_dump(negative, L"buf pooldump_read[-1:1]"), "dump: parser accepts negative wrapped pool range");
    execute(negative, device);
    dumped_bytes(fixture.output.path, L"pooldump_read[-1_1]--1", bytes_b);
    dumped_bytes(fixture.output.path, L"pooldump_read[-1_1]-0", bytes_a);
    dumped_bytes(fixture.output.path, L"pooldump_read[-1_1]-1", bytes_b);
    CommandList overflow;
    require_batch(parse_dump(overflow, L"buf pooldump_read[3:6]"), "dump: parser accepts positive wrapped pool range");
    execute(overflow, device);
    dumped_bytes(fixture.output.path, L"pooldump_read[3_6]-3", bytes_b);
    dumped_bytes(fixture.output.path, L"pooldump_read[3_6]-4", bytes_a);
    dumped_bytes(fixture.output.path, L"pooldump_read[3_6]-5", bytes_b);
    dumped_bytes(fixture.output.path, L"pooldump_read[3_6]-6", bytes_a);
    for (UINT i = 0; i < 4; ++i) {
        unsigned frame = 0;
        require_batch(pool.GetResource(static_cast<float>(100 + i), false, false, false) == keys[i] &&
            pool.GetLastUpdateFrame(static_cast<float>(100 + i), false, &frame) && frame == 100,
            "dump: physical reads retain keys and do not force assignment refresh");
    }
    // Standard slot-range command reaches the same real FrameAnalysis writer.
    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {}; desc.Format = DXGI_FORMAT_R32_UINT;
    desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; desc.Buffer.NumElements = 16;
    ComPtr<ID3D11ShaderResourceView> va, vb;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(a.Get(), &desc, &va)) &&
        SUCCEEDED(device.native->CreateShaderResourceView(b.Get(), &desc, &vb)), "dump: native slot source views");
    set_slot(device, L'p', 3, va.Get()); set_slot(device, L'p', 4, vb.Get());
    CommandList slots;
    require_batch(parse_dump(slots, L"buf ps-t[3:4]"), "dump: parser accepts slot range"); execute(slots, device);
    dumped_bytes(fixture.output.path, L"ps-t[3_4]-3", bytes_a);
    dumped_bytes(fixture.output.path, L"ps-t[3_4]-4", bytes_b);
    CommandList huge;
    require_batch(parse_dump(huge, L"buf pooldump_read[1e20:1e20]"), "dump: finite huge equal bounds remain valid");
    auto dump = std::dynamic_pointer_cast<FrameAnalysisDumpCommand>(huge.commands.front());
    require_batch(dump != nullptr, "dump: actual parsed frame analysis command");
    // Keep the test's target prefix short enough to isolate label conversion
    // from the unrelated MAX_PATH filename fallback policy.
    dump->target_name = L"huge-range"; execute(huge, device);
    wchar_t label[96] = {}; swprintf_s(label, _countof(label), L"%.0f", static_cast<double>(1e20f));
    dumped_bytes(fixture.output.path, std::wstring(L"huge-range-") + label, bytes_a);
    CommandList rejected;
    require_batch(!parse_dump(rejected, L"buf pooldump_read") && !parse_dump(rejected, L"buf ps-t"), "dump: whole pool/bare slot rejected");
    const auto file_count = static_cast<size_t>(std::distance(std::filesystem::directory_iterator(fixture.output.path), std::filesystem::directory_iterator()));
    G->analyse_frame = false; execute(slots, device);
    require_batch(static_cast<size_t>(std::distance(std::filesystem::directory_iterator(fixture.output.path), std::filesystem::directory_iterator())) == file_count,
        "dump: analyse_frame gate prevents actual extra files");
}
} // namespace

void test_dx11_dump_range_regression()
{
    std::filesystem::path output;
    { DumpFixture fixture; output = fixture.output.path; exercise_dump(fixture); }
    require_batch(!std::filesystem::exists(output), "dump: all real output/log/dedup files cleaned under exclusive build fixture");
}
