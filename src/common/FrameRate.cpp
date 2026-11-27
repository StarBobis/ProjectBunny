#include "FrameRate.h"

#include <algorithm>
#include <cmath>

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
