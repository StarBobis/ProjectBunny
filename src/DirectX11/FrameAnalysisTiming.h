#pragma once

#include <Windows.h>
#include <array>
#include <cstdio>

enum class DumpStage : size_t {
    Capture, Staging, Map, Hash, Text, Binary, TextureSave, Link, Count
};

struct DumpStageTiming {
    LONGLONG ticks = 0;
    LONGLONG maximum = 0;
    unsigned long long calls = 0;
};

// Per-context, synchronous diagnostics. Capture and texture-save are inclusive;
// their nested stages must not be added to them to estimate total elapsed time.
class FrameAnalysisTiming {
    std::array<DumpStageTiming, static_cast<size_t>(DumpStage::Count)> stages{};
public:
    void add(DumpStage stage, LONGLONG ticks)
    {
        auto& entry = stages[static_cast<size_t>(stage)];
        entry.ticks += ticks;
        if (ticks > entry.maximum) entry.maximum = ticks;
        ++entry.calls;
    }
    void write(FILE* file, unsigned draw)
    {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        const double milliseconds = 1000.0 / frequency.QuadPart;
        const char* names[] = {"capture-inclusive", "buffer-create", "map-wait", "hash",
            "text-save", "binary-save", "texture-save-inclusive", "link"};
        for (size_t i = 0; i < stages.size(); ++i) {
            const auto& entry = stages[i];
            if (entry.calls)
                std::fprintf(file, "%06u 3DMigoto Dump timing %s: calls=%llu total_ms=%.3f max_ms=%.3f\n",
                    draw, names[i], entry.calls, entry.ticks * milliseconds, entry.maximum * milliseconds);
        }
        stages = {};
    }
};

class DumpTimer {
    FrameAnalysisTiming& timing;
    DumpStage stage;
    LARGE_INTEGER start;
public:
    DumpTimer(FrameAnalysisTiming& timing, DumpStage stage) : timing(timing), stage(stage)
    {
        QueryPerformanceCounter(&start);
    }
    ~DumpTimer()
    {
        LARGE_INTEGER end;
        QueryPerformanceCounter(&end);
        timing.add(stage, end.QuadPart - start.QuadPart);
    }
    DumpTimer(const DumpTimer&) = delete;
    DumpTimer& operator=(const DumpTimer&) = delete;
};
