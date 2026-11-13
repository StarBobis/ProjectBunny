#include "CommandList.h"
#include "HackerContext.h"
#include "IniHandler.h"
#include "globals.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

class NamespaceCallbackFiles {
public:
    std::filesystem::path config, library, settings;
    std::vector<std::filesystem::path> owned;
    HINSTANCE module = migoto_handle;
    std::wstring old_user = G->user_config;
    int dirty = G->user_config_dirty;
    bool initialized = G->gConfigInitialized;
    bool automatic_cleanup = G->clear_unknown_settings;
    bool cleanup_armed = G->clear_saved_persist_vars;

    NamespaceCallbackFiles()
    {
        wchar_t executable[MAX_PATH]{};
        check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate callback fixture executable");
        const auto directory = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& part : directory) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        check(in_build, "callback files must stay in build directory");
        config = directory / L"d3dx.ini";
        library = directory / (L"callback_namespace_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        settings = directory / (L"callback_namespace_settings_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        check(!std::filesystem::exists(settings), "callback user settings must not preexist");
    }
    void initialize()
    {
        create(config); create(library);
        write(config, "[Logging]\ncalls=0\nshow_warnings=0\n[System]\nskip_early_includes_load=0\n"
            "[Include]\ninclude=" + library.filename().string() + "\nuser_config=" + settings.filename().string() +
            "\n[Hunting]\nhunting=0\n[CommandListCallbackNamespaceSource]\nx=2\n");
        write(library, "[CommandListFilled]\nx=1\n[CommandListPostOnly]\npost x=3\n[CommandListEmpty]\n");
        migoto_handle = nullptr;
    }
    ~NamespaceCallbackFiles()
    {
        G->constants_command_list.clear(); G->post_constants_command_list.clear();
        explicitCommandListSections.clear();
        customResourcePools.clear(); customResources.clear();
        persistent_variables.clear(); command_list_globals.clear();
        migoto_handle = module; G->user_config = old_user; G->user_config_dirty = dirty;
        G->gConfigInitialized = initialized; G->clear_unknown_settings = automatic_cleanup;
        G->clear_saved_persist_vars = cleanup_armed;
        for (const auto& path : owned) DeleteFileW(path.c_str());
    }
    void create(const std::filesystem::path& path)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(file != INVALID_HANDLE_VALUE, "refuse to replace existing callback fixture");
        CloseHandle(file); owned.push_back(path);
    }
    static void write(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text; file.close(); check(file.good(), "write owned callback configuration");
    }
};

std::wstring section(const wchar_t* name, const std::wstring& ns)
{
    const std::wstring plain(name);
    std::wstring result;
    check(get_namespaced_section_name_lower(&plain, &ns, &result), "resolve actual namespace section spelling");
    return result;
}

bool parse(CommandList& commands, const std::wstring& destination, const wchar_t* source, const std::wstring* ns)
{
    std::wstring value = source;
    return ParseCommandListGeneralCommands(L"CallbackNamespaceRegression", destination.c_str(), &value,
        &commands, &commands, &commands, ns);
}

void run_namespace_callback_regression()
{
    NamespaceCallbackFiles files;
    files.initialize();
    LoadConfigFile();
    const std::wstring local_namespace = files.library.filename().wstring();
    const std::wstring foreign_namespace = L"callback_namespace_other.ini";
    const std::wstring filled_name = section(L"commandlistfilled", local_namespace);
    const std::wstring post_name = section(L"commandlistpostonly", local_namespace);
    const std::wstring empty_name = section(L"commandlistempty", local_namespace);
    auto& filled = explicitCommandListSections.at(filled_name);
    auto& post = explicitCommandListSections.at(post_name);
    auto& empty = explicitCommandListSections.at(empty_name);
    auto& source = explicitCommandListSections.at(L"commandlistcallbacknamespacesource");
    std::wstring actual_namespace;
    check(get_section_namespace(filled.command_list.ini_section.c_str(), &actual_namespace) &&
        actual_namespace == local_namespace, "real included INI owns namespace metadata");
    check(!filled.command_list.commands.empty() && post.command_list.commands.empty() &&
        !post.post_command_list.commands.empty(), "real INI registers pre and post-only command bodies");
    for (const auto& name : {filled_name, post_name}) {
        auto& target = explicitCommandListSections.at(name);
        const bool before_pre = target.command_list.runtime_populated;
        const bool before_post = target.post_command_list.runtime_populated;
        CommandList rejected;
        check(!parse(rejected, name, L"ref commandlistcallbacknamespacesource", &foreign_namespace) &&
            rejected.commands.empty() && target.command_list.runtime_populated == before_pre &&
            target.post_command_list.runtime_populated == before_post &&
            target.command_list.ResolveCommandList() == &target.command_list,
            "foreign namespace cannot override either phase of a nonempty body or mutate rejected destination");
    }
    const std::wstring global_namespace;
    CommandList rejected_global;
    check(!parse(rejected_global, filled_name, L"ref commandlistcallbacknamespacesource", &global_namespace),
        "global caller cannot override a foreign namespaced body");
    HackerContext context(nullptr, nullptr);
    CommandListState state; state.mHackerContext = &context;
    CommandList same;
    check(parse(same, filled_name, L"ref commandlistcallbacknamespacesource", &local_namespace),
        "same namespace can explicitly override its own nonempty callback");
    same.commands.front()->run(&state);
    check(filled.command_list.ResolveCommandList() == &source.command_list &&
        filled.post_command_list.ResolveCommandList() == &source.post_command_list,
        "accepted callback updates both runtime references");
    CommandList still_rejected;
    check(!parse(still_rejected, filled_name, L"ref commandlistcallbacknamespacesource", &foreign_namespace) &&
        filled.command_list.ResolveCommandList() == &source.command_list,
        "foreign rejection retains previous runtime reference");
    CommandList clear;
    check(parse(clear, filled_name, L"null", &foreign_namespace), "foreign null reset remains permitted");
    clear.commands.front()->run(&state);
    check(filled.command_list.ResolveCommandList() == &filled.command_list, "null restores original body reference");
    CommandList fill_empty;
    check(parse(fill_empty, empty_name, L"ref commandlistcallbacknamespacesource", &foreign_namespace),
        "foreign namespace can populate an empty callback");
    fill_empty.commands.front()->run(&state);
    check(empty.command_list.ResolveCommandList() == &source.command_list, "empty callback points to source after execution");
    CommandList global;
    const std::wstring global_ref = L"ref " + filled_name;
    check(parse(global, L"commandlistcallbacknamespacesource", global_ref.c_str(), &foreign_namespace),
        "global section without an owning namespace retains cross-namespace override behavior");
}
} // namespace

void test_dx11_callback_namespace_regression()
{
    try {
        run_namespace_callback_regression();
        std::fputs("PASS: actual INI namespace callback override restrictions and null reset\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL callback namespace: %s\n", error.what()); std::exit(EXIT_FAILURE);
    }
}
