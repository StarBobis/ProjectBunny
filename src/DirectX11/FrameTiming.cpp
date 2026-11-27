#include "FrameTiming.h"
#include "FrameClock.h"
#include "globals.h"
#include <cmath>

uint64_t GetSystemTicks() noexcept
{
    return migoto::GetSystemTicks();
}

void UpdateFrameTiming(uint64_t system_tick_count) noexcept
{
    G->gFrameTime = migoto::SecondsBetween(G->gSystemTickCount, system_tick_count);
    G->gTime = migoto::SecondsBetween(G->ticks_at_launch, system_tick_count);
    G->gSystemTickCount = system_tick_count;
    G->gFPSCounter.Update(system_tick_count);
}

uint32_t GetSpatialReuseRadius(uint32_t radius, float frame_time) noexcept
{
    // The 12/8/12 toroidal grid has maximum Chebyshev distance 2048.
    if (!radius || !std::isfinite(frame_time) || frame_time <= 0) return 0;
    const double scaled = static_cast<double>(radius) * static_cast<double>(frame_time) * 120.0;
    if (!std::isfinite(scaled) || scaled >= 2048.0) return 2048;
    // Below the cap all float intermediates are bounded. Preserve upstream's
    // float rounding at exact 60/120/240 FPS reference values before ceil.
    const float frame_scale = frame_time * 120.0f;
    return static_cast<uint32_t>(std::ceil(static_cast<double>(radius * frame_scale)));
}
