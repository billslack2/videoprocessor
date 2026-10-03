/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

// AVX2-only kernels. Call only after CpuFeatures::SupportsAvx2Kernels().
// Compiled without /GL to preserve the baseline/AVX2 boundary.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <immintrin.h>
#include "CV210toP010VideoFrameFormatter.h"
#include "CV210toP010VideoFrameFormatter.Common.h"

#if !defined(__AVX2__)
#error This file requires /arch:AVX2
#endif

template<bool AverageChroma>
void CV210toP010VideoFrameFormatter::ProcessLineSegmentImpl(
    const uint8_t* srcData, uint32_t srcStride,
    uint16_t* dstY, uint16_t* dstUV,
    uint32_t width, uint32_t startLine, uint32_t endLine) noexcept
{
    const uint32_t pixelsPerIter = 12;
    const uint32_t numIters = width / pixelsPerIter;
    const uint32_t remainderPixels = width % pixelsPerIter;

    // Constants
    const __m256i mask_3ff = _mm256_set1_epi32(0x3FF);

    // Y permutation indices and shifts
    const __m256i y_idx0 = _mm256_setr_epi32(0, 1, 1, 2, 3, 3, 4, 5);
    const __m256i y_shift0 = _mm256_setr_epi32(10, 0, 20, 10, 0, 20, 10, 0);
    const __m256i y_idx1 = _mm256_setr_epi32(5, 6, 7, 7, 0, 0, 0, 0);
    const __m256i y_shift1 = _mm256_setr_epi32(20, 10, 0, 20, 0, 0, 0, 0);

    // UV permutation indices and shifts
    const __m256i uv_idx0 = _mm256_setr_epi32(0, 0, 1, 2, 2, 3, 4, 4);
    const __m256i uv_shift0 = _mm256_setr_epi32(0, 20, 10, 0, 20, 10, 0, 20);
    const __m256i uv_idx1 = _mm256_setr_epi32(5, 6, 6, 7, 0, 0, 0, 0);
    const __m256i uv_shift1 = _mm256_setr_epi32(10, 0, 20, 10, 0, 0, 0, 0);

    // Process line pairs (even, odd)
    for (uint32_t line = startLine; line < endLine; line += 2)
    {
        const uint32_t* src_even = reinterpret_cast<const uint32_t*>(srcData + static_cast<ptrdiff_t>(line) * srcStride);
        const uint32_t* src_odd = reinterpret_cast<const uint32_t*>(srcData + static_cast<ptrdiff_t>(line + 1) * srcStride);

        uint16_t* lineY_even = dstY + static_cast<ptrdiff_t>(line) * width;
        uint16_t* lineY_odd = dstY + static_cast<ptrdiff_t>(line + 1) * width;
        uint16_t* lineUV = dstUV + static_cast<ptrdiff_t>(line >> 1) * width;

        // Prefetch next lines to hide memory latency
        // MEMORY OPTIMIZATION: Deeper prefetch for Ryzen's large L3 cache (32MB on 5800G)
        // Prefetch 4-6 lines ahead instead of 2 to better utilize memory bandwidth
        // Modern DDR4 benefits from further-ahead prefetching to hide latency
        if (line + 6 < endLine)
        {
            _mm_prefetch(reinterpret_cast<const char*>(srcData + static_cast<ptrdiff_t>(line + 4) * srcStride), _MM_HINT_T0);
            _mm_prefetch(reinterpret_cast<const char*>(srcData + static_cast<ptrdiff_t>(line + 5) * srcStride), _MM_HINT_T0);
            _mm_prefetch(reinterpret_cast<const char*>(srcData + static_cast<ptrdiff_t>(line + 6) * srcStride), _MM_HINT_T0);
            _mm_prefetch(reinterpret_cast<const char*>(srcData + static_cast<ptrdiff_t>(line + 7) * srcStride), _MM_HINT_T0);
        }
        // Roll back prefetch optimization to original 2-line distance
        else if (line + 2 < endLine)
        {
            _mm_prefetch(reinterpret_cast<const char*>(srcData + static_cast<ptrdiff_t>(line + 2) * srcStride), _MM_HINT_T0);
            _mm_prefetch(reinterpret_cast<const char*>(srcData + static_cast<ptrdiff_t>(line + 3) * srcStride), _MM_HINT_T0);
        }

        for (uint32_t i = 0; i < numIters; i++)
        {
            // Load 32 bytes from even line (8 ints, 12 pixels)
            __m256i in_even = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_even));
            src_even += 8;

            // Load 32 bytes from odd line (8 ints, 12 pixels)
            __m256i in_odd = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_odd));
            src_odd += 8;

            // Process Y from even line (Y0..Y7)
            __m256i y0_even = _mm256_permutevar8x32_epi32(in_even, y_idx0);
            y0_even = _mm256_srlv_epi32(y0_even, y_shift0);
            y0_even = _mm256_and_si256(y0_even, mask_3ff);
            y0_even = _mm256_slli_epi32(y0_even, 6);

            // Process Y from even line (Y8..Y11)
            __m256i y1_even = _mm256_permutevar8x32_epi32(in_even, y_idx1);
            y1_even = _mm256_srlv_epi32(y1_even, y_shift1);
            y1_even = _mm256_and_si256(y1_even, mask_3ff);
            y1_even = _mm256_slli_epi32(y1_even, 6);

            // Process Y from odd line (Y0..Y7)
            __m256i y0_odd = _mm256_permutevar8x32_epi32(in_odd, y_idx0);
            y0_odd = _mm256_srlv_epi32(y0_odd, y_shift0);
            y0_odd = _mm256_and_si256(y0_odd, mask_3ff);
            y0_odd = _mm256_slli_epi32(y0_odd, 6);

            // Process Y from odd line (Y8..Y11)
            __m256i y1_odd = _mm256_permutevar8x32_epi32(in_odd, y_idx1);
            y1_odd = _mm256_srlv_epi32(y1_odd, y_shift1);
            y1_odd = _mm256_and_si256(y1_odd, mask_3ff);
            y1_odd = _mm256_slli_epi32(y1_odd, 6);

            // Store Y from even line (unaligned stores - safe for any pointer)
            {
                __m128i y0_lo = _mm256_castsi256_si128(y0_even);
                __m128i y0_hi = _mm256_extracti128_si256(y0_even, 1);
                __m128i packed0 = _mm_packus_epi32(y0_lo, y0_hi);

                __m128i y1_lo = _mm256_castsi256_si128(y1_even);
                __m128i packed1 = _mm_packus_epi32(y1_lo, y1_lo);

                _mm_storeu_si128(reinterpret_cast<__m128i*>(lineY_even), packed0);
                _mm_storel_epi64(reinterpret_cast<__m128i*>(lineY_even + 8), packed1);
                lineY_even += 12;
            }

            // Store Y from odd line
            {
                __m128i y0_lo = _mm256_castsi256_si128(y0_odd);
                __m128i y0_hi = _mm256_extracti128_si256(y0_odd, 1);
                __m128i packed0 = _mm_packus_epi32(y0_lo, y0_hi);

                __m128i y1_lo = _mm256_castsi256_si128(y1_odd);
                __m128i packed1 = _mm_packus_epi32(y1_lo, y1_lo);

                _mm_storeu_si128(reinterpret_cast<__m128i*>(lineY_odd), packed0);
                _mm_storel_epi64(reinterpret_cast<__m128i*>(lineY_odd + 8), packed1);
                lineY_odd += 12;
            }

            // LEGACY selects even-row chroma; AVERAGE also unpacks the odd row.
            __m256i uv0_even = _mm256_permutevar8x32_epi32(in_even, uv_idx0);
            uv0_even = _mm256_and_si256(
                _mm256_srlv_epi32(uv0_even, uv_shift0), mask_3ff);
            __m256i uv0 = uv0_even;
            if (AverageChroma)
            {
                __m256i uv0_odd = _mm256_permutevar8x32_epi32(in_odd, uv_idx0);
                uv0_odd = _mm256_and_si256(
                    _mm256_srlv_epi32(uv0_odd, uv_shift0), mask_3ff);
                uv0 = _mm256_srli_epi32(_mm256_add_epi32(
                    _mm256_add_epi32(uv0_even, uv0_odd),
                    _mm256_set1_epi32(1)), 1);
            }
            uv0 = _mm256_slli_epi32(uv0, 6);

            __m256i uv1_even = _mm256_permutevar8x32_epi32(in_even, uv_idx1);
            uv1_even = _mm256_and_si256(
                _mm256_srlv_epi32(uv1_even, uv_shift1), mask_3ff);
            __m256i uv1 = uv1_even;
            if (AverageChroma)
            {
                __m256i uv1_odd = _mm256_permutevar8x32_epi32(in_odd, uv_idx1);
                uv1_odd = _mm256_and_si256(
                    _mm256_srlv_epi32(uv1_odd, uv_shift1), mask_3ff);
                uv1 = _mm256_srli_epi32(_mm256_add_epi32(
                    _mm256_add_epi32(uv1_even, uv1_odd),
                    _mm256_set1_epi32(1)), 1);
            }
            uv1 = _mm256_slli_epi32(uv1, 6);

            // Store UV
            {
                __m128i uv0_lo = _mm256_castsi256_si128(uv0);
                __m128i uv0_hi = _mm256_extracti128_si256(uv0, 1);
                __m128i packed0 = _mm_packus_epi32(uv0_lo, uv0_hi);

                __m128i uv1_lo = _mm256_castsi256_si128(uv1);
                __m128i packed1 = _mm_packus_epi32(uv1_lo, uv1_lo);

                _mm_storeu_si128(reinterpret_cast<__m128i*>(lineUV), packed0);
                _mm_storel_epi64(reinterpret_cast<__m128i*>(lineUV + 8), packed1);
                lineUV += 12;
            }
        }

        // Decode at most two terminal packs and write only active pixels. This
        // is needed when the active width ends within DeckLink's padded v210 row.
        if (remainderPixels > 0)
        {
            uint32_t remaining = remainderPixels;
            while (remaining > 0)
            {
                const uint32_t pixelCount = std::min<uint32_t>(PIXELS_PER_PACK, remaining);
                const V210Pack evenPack = ReadV210Pack(src_even);
                const V210Pack oddPack = ReadV210Pack(src_odd);
                uint16_t* tailUV = lineUV;
                WriteV210PackToP010(evenPack, pixelCount, lineY_even, lineUV);
                uint16_t* noChroma = nullptr;
                WriteV210PackToP010(oddPack, pixelCount, lineY_odd, noChroma);
                if (AverageChroma)
                    AverageV210PackChromaIntoP010(oddPack, pixelCount, tailUV);
                remaining -= pixelCount;
            }
        }

    }
}

template<bool AverageChroma>
bool CV210toP010VideoFrameFormatter::ConvertV210ToP010_SIMDImpl(
    const uint8_t* srcData,
    uint32_t srcStride,
    uint16_t* dstY,
    uint16_t* dstUV,
    uint32_t width,
    uint32_t height) noexcept
{


    const uint32_t pixelsPerIter = 12;
    const uint32_t numIters = width / pixelsPerIter;
    const uint32_t remainderPixels = width % pixelsPerIter;

    // Constants
    const __m256i mask_3ff = _mm256_set1_epi32(0x3FF);

    // Y permutation indices and shifts
    const __m256i y_idx0 = _mm256_setr_epi32(0, 1, 1, 2, 3, 3, 4, 5);
    const __m256i y_shift0 = _mm256_setr_epi32(10, 0, 20, 10, 0, 20, 10, 0);
    const __m256i y_idx1 = _mm256_setr_epi32(5, 6, 7, 7, 0, 0, 0, 0);
    const __m256i y_shift1 = _mm256_setr_epi32(20, 10, 0, 20, 0, 0, 0, 0);

    // UV permutation indices and shifts
    const __m256i uv_idx0 = _mm256_setr_epi32(0, 0, 1, 2, 2, 3, 4, 4);
    const __m256i uv_shift0 = _mm256_setr_epi32(0, 20, 10, 0, 20, 10, 0, 20);
    const __m256i uv_idx1 = _mm256_setr_epi32(5, 6, 6, 7, 0, 0, 0, 0);
    const __m256i uv_shift1 = _mm256_setr_epi32(10, 0, 20, 10, 0, 0, 0, 0);

    // Process line pairs (even, odd)
    for (uint32_t line = 0; line < height; line += 2)
    {
        const uint32_t* src_even = reinterpret_cast<const uint32_t*>(srcData + static_cast<ptrdiff_t>(line) * srcStride);
        const uint32_t* src_odd = reinterpret_cast<const uint32_t*>(srcData + static_cast<ptrdiff_t>(line + 1) * srcStride);

        uint16_t* lineY_even = dstY + static_cast<ptrdiff_t>(line) * width;
        uint16_t* lineY_odd = dstY + static_cast<ptrdiff_t>(line + 1) * width;
        uint16_t* lineUV = dstUV + static_cast<ptrdiff_t>(line >> 1) * width;

        for (uint32_t i = 0; i < numIters; i++)
        {
            // Load 32 bytes from even line (8 ints, 12 pixels)
            __m256i in_even = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_even));
            src_even += 8;

            // Load 32 bytes from odd line (8 ints, 12 pixels)
            __m256i in_odd = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_odd));
            src_odd += 8;

            // Process Y from even line (Y0..Y7)
            __m256i y0_even = _mm256_permutevar8x32_epi32(in_even, y_idx0);
            y0_even = _mm256_srlv_epi32(y0_even, y_shift0);
            y0_even = _mm256_and_si256(y0_even, mask_3ff);
            y0_even = _mm256_slli_epi32(y0_even, 6);

            // Process Y from even line (Y8..Y11)
            __m256i y1_even = _mm256_permutevar8x32_epi32(in_even, y_idx1);
            y1_even = _mm256_srlv_epi32(y1_even, y_shift1);
            y1_even = _mm256_and_si256(y1_even, mask_3ff);
            y1_even = _mm256_slli_epi32(y1_even, 6);

            // Process Y from odd line (Y0..Y7)
            __m256i y0_odd = _mm256_permutevar8x32_epi32(in_odd, y_idx0);
            y0_odd = _mm256_srlv_epi32(y0_odd, y_shift0);
            y0_odd = _mm256_and_si256(y0_odd, mask_3ff);
            y0_odd = _mm256_slli_epi32(y0_odd, 6);

            // Process Y from odd line (Y8..Y11)
            __m256i y1_odd = _mm256_permutevar8x32_epi32(in_odd, y_idx1);
            y1_odd = _mm256_srlv_epi32(y1_odd, y_shift1);
            y1_odd = _mm256_and_si256(y1_odd, mask_3ff);
            y1_odd = _mm256_slli_epi32(y1_odd, 6);

            // Store Y from even line (unaligned stores - safe for any pointer)
            {
                __m128i y0_lo = _mm256_castsi256_si128(y0_even);
                __m128i y0_hi = _mm256_extracti128_si256(y0_even, 1);
                __m128i packed0 = _mm_packus_epi32(y0_lo, y0_hi);

                __m128i y1_lo = _mm256_castsi256_si128(y1_even);
                __m128i packed1 = _mm_packus_epi32(y1_lo, y1_lo);

                _mm_storeu_si128(reinterpret_cast<__m128i*>(lineY_even), packed0);
                _mm_storel_epi64(reinterpret_cast<__m128i*>(lineY_even + 8), packed1);
                lineY_even += 12;
            }

            // Store Y from odd line
            {
                __m128i y0_lo = _mm256_castsi256_si128(y0_odd);
                __m128i y0_hi = _mm256_extracti128_si256(y0_odd, 1);
                __m128i packed0 = _mm_packus_epi32(y0_lo, y0_hi);

                __m128i y1_lo = _mm256_castsi256_si128(y1_odd);
                __m128i packed1 = _mm_packus_epi32(y1_lo, y1_lo);

                _mm_storeu_si128(reinterpret_cast<__m128i*>(lineY_odd), packed0);
                _mm_storel_epi64(reinterpret_cast<__m128i*>(lineY_odd + 8), packed1);
                lineY_odd += 12;
            }

            // LEGACY selects even-row chroma; AVERAGE also unpacks the odd row.
            __m256i uv0_even = _mm256_permutevar8x32_epi32(in_even, uv_idx0);
            uv0_even = _mm256_and_si256(
                _mm256_srlv_epi32(uv0_even, uv_shift0), mask_3ff);
            __m256i uv0 = uv0_even;
            if (AverageChroma)
            {
                __m256i uv0_odd = _mm256_permutevar8x32_epi32(in_odd, uv_idx0);
                uv0_odd = _mm256_and_si256(
                    _mm256_srlv_epi32(uv0_odd, uv_shift0), mask_3ff);
                uv0 = _mm256_srli_epi32(_mm256_add_epi32(
                    _mm256_add_epi32(uv0_even, uv0_odd),
                    _mm256_set1_epi32(1)), 1);
            }
            uv0 = _mm256_slli_epi32(uv0, 6);

            __m256i uv1_even = _mm256_permutevar8x32_epi32(in_even, uv_idx1);
            uv1_even = _mm256_and_si256(
                _mm256_srlv_epi32(uv1_even, uv_shift1), mask_3ff);
            __m256i uv1 = uv1_even;
            if (AverageChroma)
            {
                __m256i uv1_odd = _mm256_permutevar8x32_epi32(in_odd, uv_idx1);
                uv1_odd = _mm256_and_si256(
                    _mm256_srlv_epi32(uv1_odd, uv_shift1), mask_3ff);
                uv1 = _mm256_srli_epi32(_mm256_add_epi32(
                    _mm256_add_epi32(uv1_even, uv1_odd),
                    _mm256_set1_epi32(1)), 1);
            }
            uv1 = _mm256_slli_epi32(uv1, 6);

            // Store UV
            {
                __m128i uv0_lo = _mm256_castsi256_si128(uv0);
                __m128i uv0_hi = _mm256_extracti128_si256(uv0, 1);
                __m128i packed0 = _mm_packus_epi32(uv0_lo, uv0_hi);

                __m128i uv1_lo = _mm256_castsi256_si128(uv1);
                __m128i packed1 = _mm_packus_epi32(uv1_lo, uv1_lo);

                _mm_storeu_si128(reinterpret_cast<__m128i*>(lineUV), packed0);
                _mm_storel_epi64(reinterpret_cast<__m128i*>(lineUV + 8), packed1);
                lineUV += 12;
            }
        }

        // Decode at most two terminal packs and write only active pixels. This
        // is needed when the active width ends within DeckLink's padded v210 row.
        if (remainderPixels > 0)
        {
            uint32_t remaining = remainderPixels;
            while (remaining > 0)
            {
                const uint32_t pixelCount = std::min<uint32_t>(PIXELS_PER_PACK, remaining);
                const V210Pack evenPack = ReadV210Pack(src_even);
                const V210Pack oddPack = ReadV210Pack(src_odd);
                uint16_t* tailUV = lineUV;
                WriteV210PackToP010(evenPack, pixelCount, lineY_even, lineUV);
                uint16_t* noChroma = nullptr;
                WriteV210PackToP010(oddPack, pixelCount, lineY_odd, noChroma);
                if (AverageChroma)
                    AverageV210PackChromaIntoP010(oddPack, pixelCount, tailUV);
                remaining -= pixelCount;
            }
        }
    }

    return true;
}

void CV210toP010VideoFrameFormatter::ProcessAdvancedSegmentAVX2(
    const uint8_t* srcData, uint32_t srcStride, uint16_t* dstY, uint16_t* dstUV,
    uint32_t width, uint32_t startLine, uint32_t endLine) noexcept
{
    static constexpr int weights[12] = {
        60, 247, -557, -1092, 2220, 7314, 7314, 2220, -1092, -557, 247, 60
    };
    const auto sample = [](const uint32_t* row, uint32_t component) {
        return static_cast<int>((row[component / 3] >> ((component % 3) * 10)) & 1023U);
    };
    for (uint32_t line = startLine; line < endLine; line += 2)
    {
        const uint32_t* rows[12];
        for (int tap = 0; tap < 12; ++tap)
        {
            const int sourceLine = (std::max)(0, (std::min)(
                static_cast<int>(m_height) - 1, static_cast<int>(line) + tap - 5));
            rows[tap] = reinterpret_cast<const uint32_t*>(
                srcData + static_cast<size_t>(sourceLine) * srcStride);
        }
        auto* y0 = dstY + static_cast<size_t>(line) * width;
        auto* y1 = y0 + width;
        auto* uv = dstUV + static_cast<size_t>(line / 2) * width;
        uint32_t x = 0;

        {
            const __m256i mask = _mm256_set1_epi32(1023);
            const __m256i yi0 = _mm256_setr_epi32(0, 1, 1, 2, 3, 3, 4, 5);
            const __m256i ys0 = _mm256_setr_epi32(10, 0, 20, 10, 0, 20, 10, 0);
            const __m256i yi1 = _mm256_setr_epi32(5, 6, 7, 7, 0, 0, 0, 0);
            const __m256i ys1 = _mm256_setr_epi32(20, 10, 0, 20, 0, 0, 0, 0);
            const __m256i ci0 = _mm256_setr_epi32(0, 0, 1, 2, 2, 3, 4, 4);
            const __m256i cs0 = _mm256_setr_epi32(0, 20, 10, 0, 20, 10, 0, 20);
            const __m256i ci1 = _mm256_setr_epi32(5, 6, 6, 7, 0, 0, 0, 0);
            const __m256i cs1 = _mm256_setr_epi32(10, 0, 20, 10, 0, 0, 0, 0);
            const auto unpack = [&](const __m256i packed, const __m256i indices, const __m256i shifts) {
                return _mm256_and_si256(_mm256_srlv_epi32(
                    _mm256_permutevar8x32_epi32(packed, indices), shifts), mask);
            };
            const auto store = [](uint16_t* out, __m256i first, __m256i last) {
                const __m128i a = _mm_packus_epi32(_mm256_castsi256_si128(first),
                    _mm256_extracti128_si256(first, 1));
                const __m128i b = _mm_packus_epi32(_mm256_castsi256_si128(last),
                    _mm256_castsi256_si128(last));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(out), a);
                _mm_storel_epi64(reinterpret_cast<__m128i*>(out + 8), b);
            };
            for (; x + 12 <= width; x += 12)
            {
                const uint32_t offset = (x / 6) * 4;
                const __m256i even = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rows[5] + offset));
                const __m256i odd = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rows[6] + offset));
                store(y0 + x, _mm256_slli_epi32(unpack(even, yi0, ys0), 6),
                    _mm256_slli_epi32(unpack(even, yi1, ys1), 6));
                store(y1 + x, _mm256_slli_epi32(unpack(odd, yi0, ys0), 6),
                    _mm256_slli_epi32(unpack(odd, yi1, ys1), 6));
                __m256i sum0 = _mm256_set1_epi32(8192);
                __m256i sum1 = sum0;
                for (int tap = 0; tap < 12; ++tap)
                {
                    const __m256i packed = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rows[tap] + offset));
                    const __m256i weight = _mm256_set1_epi32(weights[tap]);
                    sum0 = _mm256_add_epi32(sum0, _mm256_mullo_epi32(unpack(packed, ci0, cs0), weight));
                    sum1 = _mm256_add_epi32(sum1, _mm256_mullo_epi32(unpack(packed, ci1, cs1), weight));
                }
                const auto finish = [&](const __m256i sum) {
                    return _mm256_slli_epi32(_mm256_min_epi32(mask, _mm256_max_epi32(
                        _mm256_setzero_si256(), _mm256_srai_epi32(sum, 14))), 6);
                };
                store(uv + x, finish(sum0), finish(sum1));
            }
        }
        for (; x < width; ++x)
        {
            y0[x] = static_cast<uint16_t>(sample(rows[5], x * 2 + 1) << 6);
            y1[x] = static_cast<uint16_t>(sample(rows[6], x * 2 + 1) << 6);
            int sum = 8192;
            for (int tap = 0; tap < 12; ++tap)
                sum += weights[tap] * sample(rows[tap], x * 2);
            uv[x] = static_cast<uint16_t>((std::min)(1023, (std::max)(0, sum) / 16384) << 6);
        }
    }
}
template void CV210toP010VideoFrameFormatter::ProcessLineSegmentImpl<true>(const uint8_t*, uint32_t, uint16_t*, uint16_t*, uint32_t, uint32_t, uint32_t) noexcept;
template bool CV210toP010VideoFrameFormatter::ConvertV210ToP010_SIMDImpl<true>(const uint8_t*, uint32_t, uint16_t*, uint16_t*, uint32_t, uint32_t) noexcept;
template void CV210toP010VideoFrameFormatter::ProcessLineSegmentImpl<false>(const uint8_t*, uint32_t, uint16_t*, uint16_t*, uint32_t, uint32_t, uint32_t) noexcept;
template bool CV210toP010VideoFrameFormatter::ConvertV210ToP010_SIMDImpl<false>(const uint8_t*, uint32_t, uint16_t*, uint16_t*, uint32_t, uint32_t) noexcept;

