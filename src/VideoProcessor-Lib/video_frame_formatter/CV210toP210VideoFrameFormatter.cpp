/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include <immintrin.h>
#include <intrin.h>

#include "CV210toP210VideoFrameFormatter.h"

//
// The v210 unpacking follows FFmpeg's GPL-licensed v210dec.c implementation:
// https://github.com/FFmpeg/FFmpeg/blob/n4.4.8/libavcodec/v210dec.c
//


#define P210_WRITE_VALUE(d, v) (*d++ = (v << 6))


#define PIXELS_PER_PACK 6
#define BYTES_PER_PACK (4 * sizeof(uint32_t))

namespace
{
	bool CpuSupportsAVX2() noexcept
	{
		int cpuInfo[4] = {};
		__cpuid(cpuInfo, 0);
		if (cpuInfo[0] < 7) return false;
		__cpuid(cpuInfo, 1);
		if ((cpuInfo[2] & ((1 << 27) | (1 << 28))) !=
			((1 << 27) | (1 << 28))) return false;
		if ((_xgetbv(0) & 0x6) != 0x6) return false;
		__cpuidex(cpuInfo, 7, 0);
		return (cpuInfo[1] & (1 << 5)) != 0;
	}

	inline void StoreTwelveSamples(uint16_t* destination,
		__m256i firstEight, __m256i finalFour) noexcept
	{
		const __m128i first = _mm_packus_epi32(
			_mm256_castsi256_si128(firstEight),
			_mm256_extracti128_si256(firstEight, 1));
		const __m128i last = _mm_packus_epi32(
			_mm256_castsi256_si128(finalFour),
			_mm256_castsi256_si128(finalFour));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(destination), first);
		_mm_storel_epi64(reinterpret_cast<__m128i*>(destination + 8), last);
	}
}


CV210toP210VideoFrameFormatter::CV210toP210VideoFrameFormatter() :
	m_hasAVX2(CpuSupportsAVX2())
{
}


void CV210toP210VideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
	if (!videoState)
		throw std::runtime_error("Null video state is not allowed");

    if (videoState->videoFrameEncoding != VideoFrameEncoding::V210)
        throw std::runtime_error("Can only handle V210 input");

    m_height = videoState->displayMode->FrameHeight();
    m_width = videoState->displayMode->FrameWidth();
	if (m_width == 0 || m_height == 0 || (m_width & 1) != 0)
		throw std::runtime_error("P210 output requires a positive, even width");

	// DeckLink's v210 rows are 128-byte aligned. Do not assume the active
	// width is a multiple of a six-pixel v210 pack: the final pack can contain
	// unused padding components.
	m_sourceStride = videoState->BytesPerRow();
	const uint32_t packedBytes =
		((m_width + PIXELS_PER_PACK - 1) / PIXELS_PER_PACK) * BYTES_PER_PACK;
	if (m_sourceStride < packedBytes)
		throw std::runtime_error("v210 input row is smaller than the frame width");
}


bool CV210toP210VideoFrameFormatter::FormatVideoFrame(
	const VideoFrame& inFrame,
	BYTE* outBuffer)
{
	// Read V210
	// https://wiki.multimedia.cx/index.php/V210

	// Write P210
    // 10bpp per component, data in the high bits, zeros in the low bits (we assume little-endian native)
	// https://docs.microsoft.com/en-us/windows/win32/medfound/10-bit-and-16-bit-yuv-video-formats

	const uint32_t pixels = m_height * m_width;
	uint16_t* destinationY = reinterpret_cast<uint16_t*>(outBuffer);
	uint16_t* destinationUV = destinationY + pixels;
	const auto* sourceFrame = static_cast<const uint8_t*>(inFrame.GetData());
	if (m_hasAVX2 && m_conversionMethod != ConversionMethod::SCALAR)
		ConvertAVX2(sourceFrame, destinationY, destinationUV);
	else
		ConvertScalar(sourceFrame, destinationY, destinationUV);
	return true;
}


void CV210toP210VideoFrameFormatter::ConvertScalar(
	const uint8_t* sourceFrame, uint16_t* destinationY,
	uint16_t* destinationUV) const
{
	const uint32_t packsPerLine =
		(m_width + PIXELS_PER_PACK - 1) / PIXELS_PER_PACK;
	for (uint32_t line = 0; line < m_height; ++line)
	{
		const uint32_t* source = reinterpret_cast<const uint32_t*>(
			sourceFrame + static_cast<size_t>(line) * m_sourceStride);
		uint16_t* y = destinationY + static_cast<size_t>(line) * m_width;
		uint16_t* uv = destinationUV + static_cast<size_t>(line) * m_width;
		for (uint32_t pack = 0; pack < packsPerLine; ++pack)
		{
			const uint32_t word0 = *source++;
			const uint32_t word1 = *source++;
			const uint32_t word2 = *source++;
			const uint32_t word3 = *source++;
			const uint16_t luma[PIXELS_PER_PACK] = {
				static_cast<uint16_t>((word0 >> 10) & 0x3ff),
				static_cast<uint16_t>(word1 & 0x3ff),
				static_cast<uint16_t>((word1 >> 20) & 0x3ff),
				static_cast<uint16_t>((word2 >> 10) & 0x3ff),
				static_cast<uint16_t>(word3 & 0x3ff),
				static_cast<uint16_t>((word3 >> 20) & 0x3ff) };
			const uint16_t chroma[PIXELS_PER_PACK] = {
				static_cast<uint16_t>(word0 & 0x3ff),
				static_cast<uint16_t>((word0 >> 20) & 0x3ff),
				static_cast<uint16_t>((word1 >> 10) & 0x3ff),
				static_cast<uint16_t>(word2 & 0x3ff),
				static_cast<uint16_t>((word2 >> 20) & 0x3ff),
				static_cast<uint16_t>((word3 >> 10) & 0x3ff) };
			const uint32_t remaining = m_width - pack * PIXELS_PER_PACK;
			const uint32_t pixelCount = (std::min)(
				static_cast<uint32_t>(PIXELS_PER_PACK), remaining);
			for (uint32_t pixel = 0; pixel < pixelCount; ++pixel)
				P210_WRITE_VALUE(y, luma[pixel]);
			for (uint32_t sample = 0; sample < pixelCount; ++sample)
				P210_WRITE_VALUE(uv, chroma[sample]);
		}
	}
}


void CV210toP210VideoFrameFormatter::ConvertAVX2(
	const uint8_t* sourceFrame, uint16_t* destinationY,
	uint16_t* destinationUV) const
{
	const __m256i mask = _mm256_set1_epi32(0x3ff);
	const __m256i yIndex0 = _mm256_setr_epi32(0, 1, 1, 2, 3, 3, 4, 5);
	const __m256i yShift0 = _mm256_setr_epi32(10, 0, 20, 10, 0, 20, 10, 0);
	const __m256i yIndex1 = _mm256_setr_epi32(5, 6, 7, 7, 0, 0, 0, 0);
	const __m256i yShift1 = _mm256_setr_epi32(20, 10, 0, 20, 0, 0, 0, 0);
	const __m256i uvIndex0 = _mm256_setr_epi32(0, 0, 1, 2, 2, 3, 4, 4);
	const __m256i uvShift0 = _mm256_setr_epi32(0, 20, 10, 0, 20, 10, 0, 20);
	const __m256i uvIndex1 = _mm256_setr_epi32(5, 6, 6, 7, 0, 0, 0, 0);
	const __m256i uvShift1 = _mm256_setr_epi32(10, 0, 20, 10, 0, 0, 0, 0);
	const uint32_t vectorWidth = m_width - (m_width % 12U);

	for (uint32_t line = 0; line < m_height; ++line)
	{
		const uint8_t* source = sourceFrame + static_cast<size_t>(line) * m_sourceStride;
		uint16_t* y = destinationY + static_cast<size_t>(line) * m_width;
		uint16_t* uv = destinationUV + static_cast<size_t>(line) * m_width;
		uint32_t x = 0;
		for (; x < vectorWidth; x += 12)
		{
			const __m256i packed = _mm256_loadu_si256(
				reinterpret_cast<const __m256i*>(source));
			__m256i yFirst = _mm256_and_si256(
				_mm256_srlv_epi32(_mm256_permutevar8x32_epi32(packed, yIndex0),
					yShift0), mask);
			__m256i yLast = _mm256_and_si256(
				_mm256_srlv_epi32(_mm256_permutevar8x32_epi32(packed, yIndex1),
					yShift1), mask);
			__m256i uvFirst = _mm256_and_si256(
				_mm256_srlv_epi32(_mm256_permutevar8x32_epi32(packed, uvIndex0),
					uvShift0), mask);
			__m256i uvLast = _mm256_and_si256(
				_mm256_srlv_epi32(_mm256_permutevar8x32_epi32(packed, uvIndex1),
					uvShift1), mask);
			StoreTwelveSamples(y, _mm256_slli_epi32(yFirst, 6),
				_mm256_slli_epi32(yLast, 6));
			StoreTwelveSamples(uv, _mm256_slli_epi32(uvFirst, 6),
				_mm256_slli_epi32(uvLast, 6));
			source += 32;
			y += 12;
			uv += 12;
		}

		for (; x < m_width; x += PIXELS_PER_PACK)
		{
			const auto* words = reinterpret_cast<const uint32_t*>(source);
			const uint32_t word0 = words[0];
			const uint32_t word1 = words[1];
			const uint32_t word2 = words[2];
			const uint32_t word3 = words[3];
			const uint16_t luma[PIXELS_PER_PACK] = {
				static_cast<uint16_t>((word0 >> 10) & 0x3ff),
				static_cast<uint16_t>(word1 & 0x3ff),
				static_cast<uint16_t>((word1 >> 20) & 0x3ff),
				static_cast<uint16_t>((word2 >> 10) & 0x3ff),
				static_cast<uint16_t>(word3 & 0x3ff),
				static_cast<uint16_t>((word3 >> 20) & 0x3ff) };
			const uint16_t chroma[PIXELS_PER_PACK] = {
				static_cast<uint16_t>(word0 & 0x3ff),
				static_cast<uint16_t>((word0 >> 20) & 0x3ff),
				static_cast<uint16_t>((word1 >> 10) & 0x3ff),
				static_cast<uint16_t>(word2 & 0x3ff),
				static_cast<uint16_t>((word2 >> 20) & 0x3ff),
				static_cast<uint16_t>((word3 >> 10) & 0x3ff) };
			const uint32_t pixelCount = (std::min)(
				static_cast<uint32_t>(PIXELS_PER_PACK), m_width - x);
			for (uint32_t sample = 0; sample < pixelCount; ++sample)
			{
				*y++ = static_cast<uint16_t>(luma[sample] << 6);
				*uv++ = static_cast<uint16_t>(chroma[sample] << 6);
			}
			source += BYTES_PER_PACK;
		}
	}
}


LONG CV210toP210VideoFrameFormatter::GetOutFrameSize() const
{
    const LONG pixels = m_height * m_width;

    return
        (pixels * sizeof(uint16_t)) +  // Every pixel 1 y
        (pixels / 2 * (2 * sizeof(uint16_t)));  // Every 2 pixels 2 16-bit numbers
}
