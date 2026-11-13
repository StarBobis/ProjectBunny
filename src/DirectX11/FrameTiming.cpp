#include "FrameTiming.h"
#include "globals.h"
#include <algorithm>
#include <chrono>
#include <cmath>

uint64_t GetSystemTicks() noexcept
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

FPSCounter::FPSCounter(float smoothing, float min_fps) noexcept
    : m_smoothing(std::isfinite(smoothing) ? (std::clamp)(smoothing, 0.0f, 1.0f) : 0.1f),
      m_max_delta(static_cast<uint64_t>(1000000.0 / (std::isfinite(min_fps) ?
          (std::max)(static_cast<double>(min_fps), 0.001) : 1.0)))
{
}

void FPSCounter::Update(uint64_t system_tick_count) noexcept
{
    if (!m_initialized) {
        m_last_tick = system_tick_count;
        m_initialized = true;
        return;
    }
    const uint64_t delta = system_tick_count >= m_last_tick ? system_tick_count - m_last_tick : 0;
    m_last_tick = system_tick_count;
    if (!delta || delta > m_max_delta) return;
    const float frame_time = static_cast<float>(static_cast<double>(delta) / 1000000.0);
    if (m_average_frame_time == 0.0f) m_average_frame_time = frame_time;
    else m_average_frame_time += (frame_time - m_average_frame_time) * m_smoothing;
    m_fps = 1.0f / m_average_frame_time;
}

void UpdateFrameTiming(uint64_t system_tick_count) noexcept
{
    const uint64_t delta = system_tick_count >= G->gSystemTickCount ? system_tick_count - G->gSystemTickCount : 0;
    const uint64_t elapsed = system_tick_count >= G->ticks_at_launch ? system_tick_count - G->ticks_at_launch : 0;
    G->gFrameTime = static_cast<float>(static_cast<double>(delta) / 1000000.0);
    G->gTime = static_cast<float>(static_cast<double>(elapsed) / 1000000.0);
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
