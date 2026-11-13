#pragma once
#include <cstdint>

// All timing values passed to these methods are monotonic microseconds.
uint64_t GetSystemTicks() noexcept;

class FPSCounter {
public:
    FPSCounter(float smoothing = 0.1f, float min_fps = 1.0f) noexcept;
    void Update(uint64_t system_tick_count) noexcept;
    float GetFPS() const noexcept { return m_fps; }
private:
    float m_smoothing;
    uint64_t m_max_delta;
    uint64_t m_last_tick = 0;
    float m_average_frame_time = 0.0f;
    float m_fps = 0.0f;
    bool m_initialized = false;
};

void UpdateFrameTiming(uint64_t system_tick_count) noexcept;
uint32_t GetSpatialReuseRadius(uint32_t radius, float frame_time) noexcept;
