#pragma once

#include <cstdint>

uint32_t CountExpressionBits(float value);
float RandomExpressionValue(float maximum);
void SeedExpressionRandom(uint32_t frame, uint32_t ticks);
// Read-only count of nonzero finite samples; seeding does not reset it.
uint32_t GetExpressionRandomSampleCount() noexcept;
