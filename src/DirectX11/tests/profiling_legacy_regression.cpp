#include "profiling.h"
#include "Globals.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <future>
#include <stdexcept>
#include <thread>

void test_dx11_profiling_generation_regression();

namespace {
void require_legacy(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
LONGLONG counter()
{
    LARGE_INTEGER value;
    require_legacy(QueryPerformanceCounter(&value) != FALSE, "F196: real performance counter");
    return value.QuadPart;
}
class LegacyProfileFixture {
    Profiling::Mode mode = Profiling::mode.load();
    unsigned frame = G->frame_no;
    INT64 interval = Profiling::interval;
    bool analysis = G->analyse_frame;
public:
    LegacyProfileFixture() { G->analyse_frame = false; Profiling::mode = Profiling::Mode::SUMMARY; Profiling::clear(); }
    ~LegacyProfileFixture()
    {
        Profiling::clear(); Profiling::mode = mode;
        G->frame_no = frame; Profiling::interval = interval; G->analyse_frame = analysis;
    }
};

class LegacyWorker {
    std::promise<void> entered, proceed;
    std::future<void> entry = entered.get_future();
    std::shared_future<void> continuation = proceed.get_future().share();
    std::future<void> done;
    std::thread worker;
    bool released = false;
public:
    explicit LegacyWorker(std::function<void(LegacyWorker&)> work)
    {
        std::packaged_task<void()> task([&, work = std::move(work)] { work(*this); });
        done = task.get_future();
        worker = std::thread(std::move(task));
    }
    LegacyWorker(const LegacyWorker&) = delete;
    LegacyWorker& operator=(const LegacyWorker&) = delete;
    ~LegacyWorker() { Release(); if (worker.joinable()) worker.join(); }
    void EnterAndWait() { entered.set_value(); continuation.wait(); }
    void WaitEntered()
    {
        require_legacy(entry.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
            "F196: worker reached the production sample");
    }
    void Release() { if (!released) { proceed.set_value(); released = true; } }
    void Join()
    {
        Release();
        require_legacy(done.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
            "F196: worker completed after release");
        worker.join(); done.get();
    }
};

void test_retired_legacy_sample()
{
    Profiling::clear();
    LegacyWorker worker([](LegacyWorker& gate) {
        Profiling::State state{};
        Profiling::start(&state);
        gate.EnterAndWait();
        Profiling::end(&state, &Profiling::draw_overhead);
    });
    worker.WaitEntered();
    Profiling::clear();
    {
        Profiling::Sample current(Profiling::draw_overhead);
        current.Finish(true);
    }
    const auto expected = Profiling::draw_overhead.cpu.QuadPart;
    worker.Join();
    require_legacy(Profiling::draw_overhead.cpu.QuadPart == expected &&
        Profiling::draw_overhead.count == 1 && Profiling::draw_overhead.hits == 1,
        "F196: legacy end from before clear cannot alter the new Sample window");
    std::fputs("PASS F196: controlled legacy start -> clear/current Finish -> old end\n", stderr);
}

void end_with_bounds(Profiling::State& state)
{
    const auto start = state.start_time.QuadPart;
    const auto previous = Profiling::draw_overhead.cpu.QuadPart;
    const auto before = counter();
    Profiling::end(&state, &Profiling::draw_overhead);
    const auto after = counter();
    const auto added = Profiling::draw_overhead.cpu.QuadPart - previous;
    require_legacy(added >= before - start && added <= after - start,
        "F196: legacy end preserves its complete QPC elapsed interval");
}

void test_pairing_and_reentry()
{
    Profiling::clear();
    Profiling::mode = Profiling::Mode::NONE;
    const auto identity = Profiling::GetCollectionToken();
    const auto references = identity.use_count();
    Profiling::State idle{};
    Profiling::end(&idle, &Profiling::draw_overhead);
    require_legacy(!idle.started && !Profiling::draw_overhead.cpu.QuadPart && identity.use_count() == references,
        "F196: an unstarted sample does not capture a collection");
    Profiling::mode = Profiling::Mode::SUMMARY;
    Profiling::draw_overhead.count = 7; Profiling::draw_overhead.hits = 3;
    Profiling::State outer{}, inner{};
    Profiling::start(&outer);
    Profiling::start(&inner);
    end_with_bounds(inner);
    end_with_bounds(outer);
    const auto completed = Profiling::draw_overhead.cpu.QuadPart;
    Profiling::end(&outer, &Profiling::draw_overhead);
    require_legacy(Profiling::draw_overhead.cpu.QuadPart == completed &&
        Profiling::draw_overhead.count == 7 && Profiling::draw_overhead.hits == 3,
        "F196: duplicate end is inert and legacy samples do not add count/hit events");
    Profiling::start(&outer); // Present reuses one stack State for its second segment.
    Profiling::mode = Profiling::Mode::NONE;
    end_with_bounds(outer);
    require_legacy(!outer.started, "F196: paired end still finishes after a mode switch without clear");
    Profiling::mode = Profiling::Mode::SUMMARY;
    Profiling::clear();
}

void test_end_before_clear_and_retired_finish()
{
    Profiling::Sample old_lookup(Profiling::draw_overhead);
    LegacyWorker worker([](LegacyWorker& gate) {
        Profiling::State state{};
        Profiling::start(&state);
        gate.EnterAndWait();
        Profiling::end(&state, &Profiling::draw_overhead);
    });
    worker.WaitEntered(); worker.Join();
    require_legacy(Profiling::draw_overhead.cpu.QuadPart > 0, "F196: current legacy end contributes before clear");
    Profiling::clear(); old_lookup.Finish(true);
    require_legacy(!Profiling::draw_overhead.cpu.QuadPart && !Profiling::draw_overhead.count && !Profiling::draw_overhead.hits,
        "F196: clear resets completed legacy data and retires the older Sample Finish");
}

struct EndInterval {
    LONGLONG start = 0, before = 0, after = 0;
};
void test_current_legacy_and_finish()
{
    Profiling::clear();
    EndInterval legacy;
    LegacyWorker worker([&](LegacyWorker& gate) {
        Profiling::State state{};
        Profiling::start(&state); legacy.start = state.start_time.QuadPart;
        gate.EnterAndWait();
        legacy.before = counter();
        Profiling::end(&state, &Profiling::draw_overhead);
        legacy.after = counter();
    });
    worker.WaitEntered();
    const auto sample_start_before = counter();
    Profiling::Sample lookup(Profiling::draw_overhead);
    const auto sample_start_after = counter();
    // Give both real samples a nonzero interval; this is not a benchmark.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    worker.Release();
    const auto sample_end_before = counter();
    lookup.Finish(true);
    const auto sample_end_after = counter();
    worker.Join();
    const auto lower = legacy.before - legacy.start + sample_end_before - sample_start_after;
    const auto upper = legacy.after - legacy.start + sample_end_after - sample_start_before;
    require_legacy(Profiling::draw_overhead.cpu.QuadPart >= lower && Profiling::draw_overhead.cpu.QuadPart <= upper &&
        Profiling::draw_overhead.count == 1 && Profiling::draw_overhead.hits == 1,
        "F196: current legacy end and Sample Finish aggregate both native elapsed intervals");
}
}

void test_dx11_legacy_profiling_regression()
{
    LegacyProfileFixture fixture;
    test_retired_legacy_sample();
    test_pairing_and_reentry();
    test_end_before_clear_and_retired_finish();
    test_current_legacy_and_finish();
    test_dx11_profiling_generation_regression();
    std::fputs("PASS F196: legacy lifetime, paired elapsed intervals and controlled clear/Finish ordering\n", stderr);
}
