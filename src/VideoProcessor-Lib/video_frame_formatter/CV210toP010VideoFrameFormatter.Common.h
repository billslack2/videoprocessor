/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once
#include <cstdint>

// Macros for V210 unpacking
#define V210_READ_PACK_BLOCK(a, b, c) \
    do {                              \
        val  = *src++;                \
        a = val & 0x3FF;              \
        b = (val >> 10) & 0x3FF;      \
        c = (val >> 20) & 0x3FF;      \
    } while (0)

#define PIXELS_PER_PACK 6
#define BYTES_PER_PACK (4 * sizeof(uint32_t))

namespace
{
    struct V210Pack
    {
        uint16_t y[PIXELS_PER_PACK];
        uint16_t u[PIXELS_PER_PACK / 2];
        uint16_t v[PIXELS_PER_PACK / 2];
    };

    inline V210Pack ReadV210Pack(const uint32_t*& source) noexcept
    {
        const uint32_t word0 = *source++;
        const uint32_t word1 = *source++;
        const uint32_t word2 = *source++;
        const uint32_t word3 = *source++;
        return {
            {
                static_cast<uint16_t>((word0 >> 10) & 0x3FF),
                static_cast<uint16_t>(word1 & 0x3FF),
                static_cast<uint16_t>((word1 >> 20) & 0x3FF),
                static_cast<uint16_t>((word2 >> 10) & 0x3FF),
                static_cast<uint16_t>(word3 & 0x3FF),
                static_cast<uint16_t>((word3 >> 20) & 0x3FF)
            },
            {
                static_cast<uint16_t>(word0 & 0x3FF),
                static_cast<uint16_t>((word1 >> 10) & 0x3FF),
                static_cast<uint16_t>((word2 >> 20) & 0x3FF)
            },
            {
                static_cast<uint16_t>((word0 >> 20) & 0x3FF),
                static_cast<uint16_t>(word2 & 0x3FF),
                static_cast<uint16_t>((word3 >> 10) & 0x3FF)
            }
        };
    }

    inline void WriteV210PackToP010(const V210Pack& pack, uint32_t pixelCount,
        uint16_t*& dstY, uint16_t*& dstUV) noexcept
    {
        for (uint32_t pixel = 0; pixel < pixelCount; ++pixel)
            *dstY++ = static_cast<uint16_t>(pack.y[pixel] << 6);

        if (dstUV)
        {
            for (uint32_t pair = 0; pair < pixelCount / 2; ++pair)
            {
                *dstUV++ = static_cast<uint16_t>(pack.u[pair] << 6);
                *dstUV++ = static_cast<uint16_t>(pack.v[pair] << 6);
            }
        }

    }

    inline uint16_t AverageP010Chroma(uint16_t evenValue,
        uint16_t oddCode) noexcept
    {
        const uint16_t evenCode = static_cast<uint16_t>(evenValue >> 6);
        return static_cast<uint16_t>(
            ((static_cast<uint32_t>(evenCode) + oddCode + 1U) >> 1) << 6);
    }

    inline void AverageV210PackChromaIntoP010(const V210Pack& oddPack,
        uint32_t pixelCount, uint16_t*& dstUV) noexcept
    {
        for (uint32_t pair = 0; pair < pixelCount / 2; ++pair)
        {
            dstUV[0] = AverageP010Chroma(dstUV[0], oddPack.u[pair]);
            dstUV[1] = AverageP010Chroma(dstUV[1], oddPack.v[pair]);
            dstUV += 2;
        }
    }
}
