#include "ExpressionFunctions.h"

#include <atomic>
#include <cmath>
#include <limits>

namespace {
std::atomic<uint64_t> random_seed{0};
std::atomic<uint32_t> random_call_counter{0};

uint32_t HashExpressionRandom(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352d;
    value ^= value >> 15;
    value *= 0x846ca68b;
    return value ^ (value >> 16);
}
}

uint32_t CountExpressionBits(float value)
{
    if (!std::isfinite(value))
        return 0;
    // Define the formerly undefined float-to-unsigned cases as truncation and
    // modulo 2^32, including the usual two's-complement result for negatives.
    double wrapped = std::fmod(std::trunc(static_cast<double>(value)), 4294967296.0);
    if (wrapped < 0)
        wrapped += 4294967296.0;
    uint32_t bits = static_cast<uint32_t>(wrapped);
    uint32_t count = 0;
    while (bits) {
        bits &= bits - 1;
        ++count;
    }
    return count;
}

void SeedExpressionRandom(uint32_t frame, uint32_t ticks)
{
    // Publish both frame inputs together. Worker evaluations never read mutable
    // Globals fields and cannot observe a half-updated frame/tick pair.
    random_seed.store((uint64_t(frame) << 32) | ticks, std::memory_order_relaxed);
}

uint32_t GetExpressionRandomSampleCount() noexcept
{
    return random_call_counter.load(std::memory_order_relaxed);
}

float RandomExpressionValue(float maximum)
{
    if (maximum == 0)
        return 0;
    if (!std::isfinite(maximum))
        return std::numeric_limits<float>::quiet_NaN();
    const uint64_t snapshot = random_seed.load(std::memory_order_relaxed);
    uint32_t seed = static_cast<uint32_t>(snapshot >> 32);
    seed += 0x9e3779b9u * random_call_counter.fetch_add(1, std::memory_order_relaxed);
    seed ^= static_cast<uint32_t>(snapshot);
    const float normalized = (HashExpressionRandom(seed) & 0x00ffffffu) / 16777216.0f;
    const float magnitude = std::fabs(maximum);
    float result = normalized * magnitude;
    // Subnormal rounding can reach the excluded endpoint even though the
    // normalized sample is below one. Keep the upstream half-open interval.
    if (result >= magnitude)
        result = std::nextafter(magnitude, 0.0f);
    return std::copysign(result, maximum);
}
