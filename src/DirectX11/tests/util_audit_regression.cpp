// Compile the real common implementation once in the regression target.
#include "../../common/util.cpp"
#include <stdexcept>
#include <clocale>

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

namespace {
class ParameterLocaleScope {
    std::string saved;
public:
    ParameterLocaleScope()
    {
        const char* locale=std::setlocale(LC_CTYPE,nullptr);
        if(!locale) throw std::runtime_error("F138: query existing locale");
        saved=locale;
    }
    ~ParameterLocaleScope() { if(!std::setlocale(LC_CTYPE,saved.c_str())) std::abort(); }
};
}

void test_dx11_parameter_conversion_regression()
{
    ParameterLocaleScope restore;
    if(!std::setlocale(LC_CTYPE,"C")) throw std::runtime_error("F138: establish actual C conversion context");
    wchar_t seed[]=L"previous successful value";
    if(std::string(readStringParameter(seed))!="previous successful value") throw std::runtime_error("F138: normal ASCII seed");
    wchar_t unrepresentable[]=L"\u4e2d\u6587";
    std::fputs("F138: failed conversion must not reuse an earlier static string\n",stderr); std::fflush(stderr);
    if(*readStringParameter(unrepresentable)) throw std::runtime_error("F138: conversion failure must produce an empty terminated result");
    std::fputs("F138: C conversion rejected; checking UTF-8 capacity\n",stderr); std::fflush(stderr);
    if(!std::setlocale(LC_CTYPE,"en_US.UTF-8")) throw std::runtime_error("F138: establish production UTF-8 conversion context");
    wchar_t ascii[]=L" \talpha beta \t";
    if(std::string(readStringParameter(ascii))!="alpha beta") throw std::runtime_error("F138: preserve successful trimming and content");
    wchar_t empty[]=L""; wchar_t spaces[]=L" \t  ";
    if(*readStringParameter(empty) || *readStringParameter(spaces)) throw std::runtime_error("F138: empty/blank values remain empty");
    std::wstring exact(MAX_PATH-1,L'a');
    if(std::string(readStringParameter(&exact[0]))!=std::string(MAX_PATH-1,'a')) throw std::runtime_error("F138: exact-fit text retains every byte and NUL");
    std::fputs("F138: exact fit passed; checking one-byte overflow\n",stderr); std::fflush(stderr);
    std::wstring too_long(MAX_PATH,L'b');
    if(*readStringParameter(&too_long[0])) throw std::runtime_error("F138: capacity exhaustion is not silently truncated or unterminated");
    std::wstring unicode(85,L'\u4e2d'); unicode+=L'a';
    const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,unicode.c_str(),-1,nullptr,0,nullptr,nullptr);
    if(size<=0) throw std::runtime_error("F138: independent native UTF-8 expectation");
    std::string expected(size,'\0');
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,unicode.c_str(),-1,&expected[0],size,nullptr,nullptr)) throw std::runtime_error("F138: independent UTF-8 bytes");
    expected.pop_back();
    if(std::string(readStringParameter(&unicode[0]))!=expected) throw std::runtime_error("F138: preserve valid production UTF-8 bytes");
    std::wstring multibyte_overflow(87,L'\u4e2d');
    if(*readStringParameter(&multibyte_overflow[0])) throw std::runtime_error("F138: multibyte capacity failure returns a terminated empty string");
    std::wstring multibyte_exact_capacity(86,L'\u4e2d'); multibyte_exact_capacity+=L"ab";
    if(*readStringParameter(&multibyte_exact_capacity[0])) throw std::runtime_error("F138: exact byte capacity without room for NUL is rejected");
    wchar_t invalid_utf16[]={static_cast<wchar_t>(0xd800),0};
    if(*readStringParameter(invalid_utf16)) throw std::runtime_error("F138: invalid UTF-16 cannot leak old bytes");
    wchar_t next[]=L"next";
    if(std::string(readStringParameter(next))!="next") throw std::runtime_error("F138: later conversion recovers without a stale suffix");
}
