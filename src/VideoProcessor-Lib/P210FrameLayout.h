#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

// Native P210: tightly packed little-endian 16-bit Y rows, followed by
// full-height interleaved UV rows. Ten sample bits occupy bits 15..6.
struct P210FrameLayout
{
    size_t rowBytes = 0;
    size_t planeBytes = 0;
    size_t frameBytes = 0;
};
inline bool GetP210FrameLayout(uint32_t width, uint32_t height,
    P210FrameLayout& layout) noexcept
{
    layout = {};
    if (!width || !height || (width & 1u)) return false;
    const uint64_t row = uint64_t(width) * 2;
    const uint64_t maxPlane = std::numeric_limits<uint32_t>::max() / 2u;
    if (row > maxPlane / height) return false;
    const uint64_t plane = row * height;
    layout.rowBytes = static_cast<size_t>(row);
    layout.planeBytes = static_cast<size_t>(plane);
    layout.frameBytes = static_cast<size_t>(plane * 2);
    return true;
}
struct P210PlaneView
{
    const uint8_t* y = nullptr;
    const uint8_t* uv = nullptr;
    P210FrameLayout layout;
};
inline bool GetP210PlaneView(const void* pixels, size_t bytes,
    uint32_t width, uint32_t height, P210PlaneView& view) noexcept
{
    view = {};
    P210FrameLayout layout;
    if (!pixels || !GetP210FrameLayout(width, height, layout) ||
        bytes < layout.frameBytes) return false;
    view.y = static_cast<const uint8_t*>(pixels);
    view.uv = view.y + layout.planeBytes;
    view.layout = layout;
    return true;
}
