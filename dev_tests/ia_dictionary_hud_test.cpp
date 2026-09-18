#include "Core/Diag/DX12ProfilingText.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
static void Check(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main() {
    DX12ProfilingRow rows[] = {
        {"Rm.IaDictionaryReject", 54000, 0, 0, 0},
        {"Rm.IaNegCacheHit", 600, 0, 0, 0},
        {"Rm.IaNegVerdictRuns", 5400, 0, 0, 0},
        {"Rm.IaNegVerdictNegative", 5300, 0, 0, 0}
    };
    DX12ProfilingFields fields;
    fields.Fill(rows, ARRAYSIZE(rows));
    Check(fields.Get(DX12ProfilingField::IaDictionaryReject).calls == 54000,
        "new counter is wired into production field mapping");
    DX12ProfilingWindow window;
    window.frames = 60;
    window.frameWallUs = 1500000;
    char text[DX12ProfilingHud::kTextBytes];
    const auto length = DX12ProfilingText::Format(text, sizeof(text),
        DX12ProfilingHud::Mode::Overview, window, rows, ARRAYSIZE(rows));
    Check(length > 0 && length < sizeof(text) && text[length] == '\0', "bounded Overview output");
    Check(std::strstr(text, "IaDictionaryReject") != nullptr, "dictionary rejection visible on Overview");
    Check(std::strstr(text, "90.0%") != nullptr, "dictionary denominator includes all probes");
    Check(std::strstr(text, "10.0%") != nullptr, "fallback hit rate does not miscount dictionary as cache hits");
    for (auto mode : {DX12ProfilingHud::Mode::Buckets, DX12ProfilingHud::Mode::Counters}) {
        const auto size = DX12ProfilingText::Format(text, sizeof(text), mode, window, rows, ARRAYSIZE(rows));
        Check(size > 0 && size < sizeof(text) && text[size] == '\0', "other pages remain bounded");
    }
    std::puts("PASS production HUD dictionary/fallback field mapping, denominators and page bounds");
}
