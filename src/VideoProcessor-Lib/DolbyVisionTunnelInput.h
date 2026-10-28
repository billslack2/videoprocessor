#pragma once

#include <VideoState.h>
#include <cstdint>

// The HDMI RGB8 tunnel must reach the decoder without color/range conversion.
// Supporting a memory layout does not identify DV: only a valid packet can.
inline bool DolbyVisionTunnelEncodingSupported(VideoFrameEncoding encoding)
{
    return encoding == VideoFrameEncoding::R210 ||
        encoding == VideoFrameEncoding::ARGB_8BIT ||
        encoding == VideoFrameEncoding::BGRA_8BIT;
}

inline bool DolbyVisionTunnelCanEnable(bool requested, bool compatibleRenderer,
    const VideoState& capture)
{
    return requested && compatibleRenderer && capture.valid &&
        capture.eotf == EOTF::SDR && capture.rgbTunnelBytesPreserved &&
        DolbyVisionTunnelEncodingSupported(capture.videoFrameEncoding);
}

// Canonical r210-shaped word. RGB8 layouts occupy the LOW eight bits of each
// ten-bit lane. R210 retains its original lanes for CRC-based alignment probing.
inline uint32_t ReadDolbyVisionTunnelWord(const uint8_t* pixel,
    VideoFrameEncoding encoding)
{
    if (encoding == VideoFrameEncoding::ARGB_8BIT)
        return (uint32_t(pixel[1]) << 20) | (uint32_t(pixel[2]) << 10) | pixel[3];
    if (encoding == VideoFrameEncoding::BGRA_8BIT)
        return (uint32_t(pixel[2]) << 20) | (uint32_t(pixel[1]) << 10) | pixel[0];
    return (uint32_t(pixel[0]) << 24) | (uint32_t(pixel[1]) << 16) |
        (uint32_t(pixel[2]) << 8) | pixel[3];
}
