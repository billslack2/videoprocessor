#pragma once
#include <cstdint>

// Delivery is distinct from the HDMI timing clock and renderer queue drops.
struct CaptureDeliveryStats
{
    double expectedFps = 0.0;
    double deliveredFps = 0.0;
    double windowSeconds = 0.0; // zero until a complete observation window
    uint64_t recentMissedFrames = 0;
    bool losslessRgb24Capture = false;
};

inline uint64_t CaptureMissedInputFrames(int64_t previous, int64_t current,
    uint32_t period) noexcept
{
    if (previous <= 0 || current <= previous || !period) return 0;
    const uint64_t delta = uint64_t(current) - uint64_t(previous);
    // Round hardware timestamp intervals, avoiding floating-point/overflow.
    const uint64_t intervals = delta / period + ((delta % period) * 2 >= period);
    return intervals > 1 ? intervals - 1 : 0;
}
