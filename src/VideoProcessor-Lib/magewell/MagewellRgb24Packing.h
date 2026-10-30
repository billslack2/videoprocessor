#pragma once
#include <cstddef>
#include <cstdint>

namespace MagewellRgb24Packing
{
// Expand B,G,R into B,G,R,255. No colour/range conversion. Buffers must not
// overlap. Validate every extent before writing, including padded row tails.
bool Expand(const uint8_t* source, size_t sourceBytes, uint32_t sourceStride,
    uint8_t* destination, size_t destinationBytes, uint32_t destinationStride,
    uint32_t width, uint32_t height, bool allowSimd = true) noexcept;
void ExpandAvx2(const uint8_t* source, uint32_t sourceStride,
    uint8_t* destination, uint32_t destinationStride,
    uint32_t width, uint32_t height) noexcept;
}
