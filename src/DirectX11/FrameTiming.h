#pragma once
#include "FrameRate.h"

// All timing values passed to these methods are monotonic microseconds.
uint64_t GetSystemTicks() noexcept;

void UpdateFrameTiming(uint64_t system_tick_count) noexcept;
uint32_t GetSpatialReuseRadius(uint32_t radius, float frame_time) noexcept;
