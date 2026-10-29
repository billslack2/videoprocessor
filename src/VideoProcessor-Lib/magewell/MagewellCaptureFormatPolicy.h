#pragma once
#include <cstdint>
#include <cmath>

// Card-side payload budget, not renderer output refresh. Keep source precision
// and all 4:2:2 chroma rows where the transfer fits. Never silently select 8-bit.
inline bool MagewellNeedsBandwidthSavingCapture(uint32_t width, uint32_t height,
    uint32_t frameDuration100ns, bool interlaced) noexcept
{
    if (!width || !height || !frameDuration100ns || interlaced) return false;
    constexpr double payloadBudgetBytesPerSecond = 1600000000.0;
    const double rate = 10000000.0 / frameDuration100ns;
    return double(width) * height * 4.0 * rate > payloadBudgetBytesPerSecond;
}
