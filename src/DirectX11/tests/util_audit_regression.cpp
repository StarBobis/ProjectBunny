// Compile the real common implementation once in the regression target.
#include "../../common/util.cpp"
#include <stdexcept>

namespace {
class CrashHelpLogScope {
    FILE* saved = LogFile;
    LogVerbosity saved_level = gLogVerbosity;
public:
    FILE* capture = std::tmpfile();
    CrashHelpLogScope()
    {
        if (!capture) throw std::runtime_error("F286: open diagnostic capture");
        LogFile = capture; gLogVerbosity = LogVerbosity::INFO;
    }
    ~CrashHelpLogScope() { LogFile = saved; gLogVerbosity = saved_level; std::fclose(capture); }
};
}

void test_dx11_crash_help_regression()
{
    CrashHelpLogScope output;
    log_interactive_crash_handler_help();
    if (std::fseek(output.capture, 0, SEEK_SET)) throw std::runtime_error("F286: rewind real diagnostic stream");
    std::string text; char buffer[1024]; size_t count;
    while ((count = std::fread(buffer, 1, sizeof(buffer), output.capture)) != 0) text.append(buffer, count);
    for (const auto* supported : {"Ctrl+Alt+Q: Quit", "Ctrl+Alt+K: Kill process", "Ctrl+Alt+C: Continue execution",
            "Ctrl+Alt+B: Break into the debugger"})
        if (text.find(supported) == std::string::npos) throw std::runtime_error("F286: preserve supported crash-handler choices");
    if (text.find("Ctrl+Alt+W") != std::string::npos)
        throw std::runtime_error("F286: crash help advertises an unsupported windowed-mode key");
    LogFile = nullptr;
    log_interactive_crash_handler_help();
}
