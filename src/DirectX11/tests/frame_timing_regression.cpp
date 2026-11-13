#include "FrameTiming.h"
#include "CommandList.h"
#include "globals.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

extern uint32_t PackCellCoords(float, float, float, float);

static void timing_check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL frame timing: %s\n", message); std::exit(EXIT_FAILURE); }
}

static void test_timing_counter()
{
    const auto first = GetSystemTicks();
    timing_check(GetSystemTicks() >= first, "production steady-clock microseconds are monotonic");
    FPSCounter counter(0.5f, 1.0f);
    timing_check(counter.GetFPS() == 0, "FPS starts at zero");
    counter.Update(1000000);
    timing_check(counter.GetFPS() == 0, "first FPS update establishes baseline");
    counter.Update(1010000);
    timing_check(std::fabs(counter.GetFPS() - 100.0f) < 0.001f, "first valid frame initializes average");
    counter.Update(1030000);
    timing_check(std::fabs(counter.GetFPS() - (1.0f / 0.015f)) < 0.001f, "EMA averages frame time rather than FPS");
    const float before = counter.GetFPS();
    counter.Update(1030000);
    counter.Update(3030001);
    timing_check(counter.GetFPS() == before, "zero delta and long pause leave FPS unchanged");
    counter.Update(3040001);
    timing_check(std::fabs(counter.GetFPS() - 80.0f) < 0.001f, "long-pause baseline moves before resume");
    counter.Update(100);
    timing_check(std::fabs(counter.GetFPS() - 80.0f) < 0.001f, "backward tick cannot unsigned-underflow FPS");
}

static void test_frame_parameters()
{
    const auto saved_launch = G->ticks_at_launch;
    const auto saved_tick = G->gSystemTickCount;
    const auto saved_time = G->gTime;
    const auto saved_frame = G->gFrameTime;
    const auto saved_counter = G->gFPSCounter;
    G->ticks_at_launch = 9000000000ull;
    G->gSystemTickCount = G->ticks_at_launch;
    G->gFPSCounter = FPSCounter{};
    UpdateFrameTiming(G->ticks_at_launch + 10000);
    timing_check(std::fabs(G->gFrameTime - 0.01f) < 1e-7f && G->gTime == G->gFrameTime,
        "first frame uses launch tick instead of system uptime");
    UpdateFrameTiming(G->ticks_at_launch + 20000);
    timing_check(std::fabs(G->gTime - 0.02f) < 1e-7f && std::fabs(G->gFPSCounter.GetFPS() - 100) < 0.001f,
        "real frame update publishes seconds and EMA FPS");
    CommandListScope scope;
    const std::wstring ns;
    const std::wstring text = L"time + frame_time";
    CommandListExpression expression;
    float value = -1;
    timing_check(expression.parse(&text, &ns, &scope) && !expression.static_evaluate(&value) && value == -1 &&
        expression.static_evaluate(nullptr, nullptr, true) && expression.static_evaluate(&value, nullptr, true) &&
        std::fabs(value - 0.03f) < 1e-7f, "frame time explicit snapshot supports null probe and default stays dynamic");
    expression.optimise(nullptr);
    UpdateFrameTiming(G->ticks_at_launch + 40000);
    CommandListState state;
    timing_check(std::fabs(expression.evaluate(&state) - 0.06f) < 1e-7f, "optimise does not freeze frame timing");
    const std::wstring fps_text = L"fps";
    CommandListExpression fps;
    timing_check(fps.parse(&fps_text, &ns, &scope) && !fps.static_evaluate(nullptr) &&
        !fps.static_evaluate(nullptr, nullptr, true) && fps.evaluate(&state) == G->gFPSCounter.GetFPS(),
        "FPS remains runtime-only according to upstream contract");
    UpdateFrameTiming(G->ticks_at_launch - 1);
    timing_check(G->gFrameTime == 0 && G->gTime == 0, "backward timestamp safely publishes zero delta/elapsed");
    G->ticks_at_launch = saved_launch; G->gSystemTickCount = saved_tick;
    G->gTime = saved_time; G->gFrameTime = saved_frame; G->gFPSCounter = saved_counter;
}

static void test_spatial_frame_scaling()
{
    timing_check(GetSpatialReuseRadius(2, 1.0f / 60) == 4 && GetSpatialReuseRadius(2, 1.0f / 120) == 2 &&
        GetSpatialReuseRadius(2, 1.0f / 240) == 1, "reference FPS radius scaling preserves upstream float rounding");
    timing_check(GetSpatialReuseRadius(UINT32_MAX, (std::numeric_limits<float>::max)()) == 2048 &&
        GetSpatialReuseRadius(2, std::numeric_limits<float>::quiet_NaN()) == 0 &&
        GetSpatialReuseRadius(2, std::numeric_limits<float>::infinity()) == 0 &&
        GetSpatialReuseRadius(2, -1) == 0 && GetSpatialReuseRadius(2, 0) == 0,
        "scaled radius rejects invalid frame times and caps before unsigned conversion");
    const auto saved_frame_time = G->gFrameTime;
    const auto saved_frame = G->frame_no;
    const std::wstring name = L"poolframe_timing_regression";
    timing_check(!customResourcePools.count(name), "unique timing pool fixture");
    auto& pool = customResourcePools[name];
    pool.name = name;
    auto& resource = customResources[name + L"_template"];
    resource.pool = &pool; pool.resource_template = &resource;
    pool.variable_template = std::make_unique<CommandListVariable>(name + L"_template", -3, VariableFlags::NONE);
    pool.lazy_initialization = true; pool.index_type = PoolIndexType::SPATIAL;
    pool.spatial_radius = 2; pool.Initialize(2);
    const float key = BitCastToFloat(PackCellCoords(100, 0, 0, 1));
    const float nearby = BitCastToFloat(PackCellCoords(103, 0, 0, 1));
    const float distant_key = BitCastToFloat(PackCellCoords(110, 0, 0, 1));
    G->frame_no = 87; G->gFrameTime = 1.0f / 120;
    auto* stored = pool.GetVariable(key, false, false, true);
    timing_check(stored != nullptr, "allocate actual spatial variable through assignment");
    stored->fval = 55;
    unsigned frame = 0;
    G->gFrameTime = 1.0f / 60;
    timing_check(pool.GetVariable(nearby, false, false, false) == stored &&
        pool.GetLastUpdateFrame(nearby, false, &frame) && frame == 87 && !pool.UsesMissingTemplate(nearby, false),
        "60 FPS neighborhood agrees across read, last-frame query and missing-template classification");
    for (float fps : {120.0f, 240.0f}) {
        // The 60-FPS read rekeys the slot; restart each threshold comparison
        // from the same original cell rather than querying an exact hit.
        pool.ResetPool();
        stored = pool.GetVariable(key, false, false, true);
        stored->fval = 55;
        G->gFrameTime = 1.0f / fps;
        timing_check(pool.GetVariable(nearby, false, false, false) == pool.variable_template.get() &&
            !pool.GetLastUpdateFrame(nearby, false, &frame) && pool.UsesMissingTemplate(nearby, false),
            "higher FPS makes distant cell a nonallocating read miss consistently");
    }
    timing_check(pool.GetVariable(distant_key, false, false, false) == pool.variable_template.get() &&
        pool.GetVariable(key, false, false, false) == stored && stored->fval == 55,
        "read misses leave original keyed value intact");
    G->gFrameTime = 1.0f / 60; G->frame_no = 88;
    timing_check(pool.GetVariable(nearby, false, false, true) == stored &&
        pool.GetLastUpdateFrame(nearby, false, &frame) && frame == 88,
        "assignment rekeys reused spatial neighbor and updates frame");
    customResourcePools.erase(name);
    customResources.erase(name + L"_template");
    customResources.erase(name + L"_0"); customResources.erase(name + L"_1");
    command_list_globals.erase(L"$" + name + L"[0]"); command_list_globals.erase(L"$" + name + L"[1]");
    G->gFrameTime = saved_frame_time; G->frame_no = saved_frame;
}

void test_dx11_frame_timing_regression()
{
    test_timing_counter();
    test_frame_parameters();
    test_spatial_frame_scaling();
    std::fputs("PASS: frame timing, EMA FPS, snapshots and FPS-scaled spatial lookup\n", stderr);
}
