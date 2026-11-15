// Included by the one production INI parser TU in the regression executable.
namespace {
struct IniLookupExpectation {
    const wchar_t* key;
    int integer;
    bool integer_found;
    int enumeration;
    bool enum_found;
};

std::wstring p01_section(const std::wstring& name, const std::wstring& ini_namespace)
{
    std::wstring result;
    check_ini_audit(get_namespaced_section_name_lower(&name, &ini_namespace, &result),
        "P01: resolve the real namespaced section");
    return result;
}

template <typename Function>
void p01_measure(const char* name, unsigned iterations, Function&& function)
{
    uint64_t sum = 0;
    for (unsigned i = 0; i < 2000; ++i) sum += function(i);
    LARGE_INTEGER frequency{}, begin{}, end{};
    check_ini_audit(QueryPerformanceFrequency(&frequency) && QueryPerformanceCounter(&begin), "P01: start monotonic measurement");
    for (unsigned i = 0; i < iterations; ++i) sum += function(i);
    check_ini_audit(QueryPerformanceCounter(&end), "P01: stop monotonic measurement");
    const double ns = static_cast<double>(end.QuadPart - begin.QuadPart) * 1.0e9 / frequency.QuadPart / iterations;
    std::printf("P01_BENCHMARK_JSON={\"name\":\"%s\",\"iterations\":%u,\"nsPerCall\":%.4f,\"checksum\":%llu}\n",
        name, iterations, ns, static_cast<unsigned long long>(sum));
}
}

void test_dx11_ini_lookup_performance()
{
    IniWarningScope warnings; IniSectionStateScope sections; IniAuditLogCapture log;
    dx11_slot_test::Registry variables;
    const std::wstring ini_namespace = L"p01_lookup.ini";
    const std::wstring variable = L"$p01_lookup_value";
    variables.variable(variable, 99);
    variables.variable(get_namespaced_var_name_lower(variable, &ini_namespace), 1);
    std::wistringstream text(L"[ShaderOverrideP01]\ntrue_value=TrUe\nyes_value=yES\non_value=ON\n"
        L"false_value=FALSE\nno_value=No\noff_value=oFF\ninteger_value=2\nnegative_value=-5\n"
        L"overflow_value=999999999999999999999\nexpression_value=(1+1)\nvariable_value=$P01_LOOKUP_VALUE\n"
        L"enum_value=overrule\nempty_value=\ninvalid_value=not_a_value\n");
    ParseIniStream(&text, &ini_namespace);
    const auto section = p01_section(L"ShaderOverrideP01", ini_namespace);
    const IniLookupExpectation cases[] = {
        {L"missing_value", 7, false, 2, false}, {L"empty_value", 7, false, 2, false},
        {L"TRUE_VALUE", 1, true, 1, true}, {L"yes_value", 1, true, 1, true}, {L"on_value", 1, true, 1, true},
        {L"false_value", 0, true, 0, true}, {L"no_value", 0, true, 0, true}, {L"off_value", 0, true, 0, true},
        {L"integer_value", 2, true, 2, true}, {L"negative_value", -5, true, 2, false},
        {L"overflow_value", INT_MAX, true, 2, false}, {L"expression_value", 2, true, 2, true},
        {L"variable_value", 1, true, 1, true}, {L"enum_value", 7, false, 2, true},
        {L"invalid_value", 7, false, 2, false}
    };
    for (const auto& test : cases) {
        bool found = !test.integer_found;
        check_ini_audit(GetIniBoolOrInt(section.c_str(), test.key, 7, &found) == test.integer && found == test.integer_found,
            "P01: preserve boolean/integer defaults, found status, expressions and namespaced snapshots");
        check_ini_audit(GetIniBoolOrInt(section.c_str(), test.key, 7, nullptr) == test.integer, "P01: optional found remains optional");
        found = !test.enum_found;
        check_ini_audit(GetIniBoolIntOrEnum(section.c_str(), test.key, 2, &found, nullptr,
            true_false_overrule, ARRAYSIZE(true_false_overrule), 0) == test.enumeration && found == test.enum_found,
            "P01: preserve enum precedence, fallback, bounds and found status");
    }
    const auto diagnostic = log.read();
    uint64_t digest = 14695981039346656037ull;
    for (unsigned char element : diagnostic) { digest ^= element; digest *= 1099511628211ull; }
    std::printf("P01_BEHAVIOR_DIGEST=%016llx\n", static_cast<unsigned long long>(digest));
    if (!GetEnvironmentVariableW(L"BUNNY_INI_LOOKUP_BENCHMARK", nullptr, 0)) return;
    check_ini_audit(GetModuleHandleW(L"clang_rt.asan_dynamic-x86_64.dll") == nullptr,
        "P01: benchmark requires the plain Release host, without ASan allocator interception");
    std::puts("P01_RUNTIME_SANITIZER=none");

    LogFile = nullptr; gLogVerbosity = LogVerbosity::DISABLED;
    std::wostringstream corpus;
    corpus << L"[Rendering]\ntrack_texture_updates=2\n";
    std::vector<std::wstring> corpus_sections;
    for (unsigned i = 0; i < 512; ++i) {
        const auto name = L"ShaderOverridePerformanceFixture" + std::to_wstring(i);
        corpus << L"[" << name << L"]\nallow_duplicate_hash=2\nnamed=overrule\n";
        corpus_sections.push_back(p01_section(name, ini_namespace));
    }
    std::wistringstream input(corpus.str());
    ParseIniStream(&input, &ini_namespace);
    const auto enumeration = [&](unsigned i, const wchar_t* key) {
        return GetIniBoolIntOrEnum(corpus_sections[i % corpus_sections.size()].c_str(), key, 0, nullptr, nullptr,
            true_false_overrule, ARRAYSIZE(true_false_overrule), 0);
    };
    p01_measure("numeric-dense-enum-synthetic", 100000, [&](unsigned i) { return enumeration(i, L"allow_duplicate_hash"); });
    p01_measure("missing-heavy-enum-synthetic", 100000, [&](unsigned i) {
        return enumeration(i, i % 20 < 18 ? L"missing_option" : (i % 20 == 18 ? L"named" : L"allow_duplicate_hash"));
    });
    p01_measure("numeric-bool-int-micro", 100000, [&](unsigned) {
        return GetIniBoolOrInt(L"Rendering", L"track_texture_updates", 0, nullptr);
    });
}
