#include <pch.h>
#include <immintrin.h>
#include <intrin.h>

#include "CUYVYtoP210VideoFrameFormatter.h"

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

	inline __m128i GatherAlternatingBytes(__m256i packed,
		const __m256i& shuffle) noexcept
	{
		const __m256i selected = _mm256_shuffle_epi8(packed, shuffle);
		return _mm_unpacklo_epi64(_mm256_castsi256_si128(selected),
			_mm256_extracti128_si256(selected, 1));
	}
}


CUYVYtoP210VideoFrameFormatter::CUYVYtoP210VideoFrameFormatter() :
	m_hasAVX2(CpuSupportsAVX2())
{
}

void CUYVYtoP210VideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
	if (!videoState || !videoState->displayMode)
		throw std::runtime_error("UYVY P210 conversion requires a valid video state");
	if (videoState->videoFrameEncoding != VideoFrameEncoding::UYVY &&
		videoState->videoFrameEncoding != VideoFrameEncoding::HDYC)
	{
		throw std::runtime_error("UYVY P210 converter received an unsupported encoding");
	}

	m_width = videoState->displayMode->FrameWidth();
	m_height = videoState->displayMode->FrameHeight();
	if (m_width == 0 || m_height == 0 || (m_width & 1) != 0)
		throw std::runtime_error("UYVY P210 conversion requires a positive, even width");
	m_sourceStride = videoState->BytesPerRow();
	if (m_sourceStride < m_width * 2U)
		throw std::runtime_error("UYVY input row is smaller than the frame width");
}

bool CUYVYtoP210VideoFrameFormatter::FormatVideoFrame(
	const VideoFrame& inFrame, BYTE* outBuffer)
{
	if (!inFrame.GetData() || !outBuffer)
		throw std::runtime_error("UYVY P210 conversion requires valid input and output buffers");

	const auto* sourceFrame = static_cast<const uint8_t*>(inFrame.GetData());
	auto* destinationY = reinterpret_cast<uint16_t*>(outBuffer);
	auto* destinationUV = destinationY + static_cast<size_t>(m_width) * m_height;
	if (m_hasAVX2 && m_conversionMethod != ConversionMethod::SCALAR)
		ConvertAVX2(sourceFrame, destinationY, destinationUV);
	else
		ConvertScalar(sourceFrame, destinationY, destinationUV);
	return true;
}


void CUYVYtoP210VideoFrameFormatter::ConvertScalar(
	const uint8_t* sourceFrame, uint16_t* destinationY,
	uint16_t* destinationUV) const
{
	for (uint32_t line = 0; line < m_height; ++line)
	{
		const auto* source = sourceFrame + static_cast<size_t>(line) * m_sourceStride;
		auto* y = destinationY + static_cast<size_t>(line) * m_width;
		auto* uv = destinationUV + static_cast<size_t>(line) * m_width;
		for (uint32_t x = 0; x < m_width; x += 2)
		{
			// UYVY: U0, Y0, V0, Y1. P210 carries the same 4:2:2 pair
			// at every source row, using the high bits of 16-bit components.
			*uv++ = static_cast<uint16_t>(*source++) << 8;
			*y++ = static_cast<uint16_t>(*source++) << 8;
			*uv++ = static_cast<uint16_t>(*source++) << 8;
			*y++ = static_cast<uint16_t>(*source++) << 8;
		}
	}
}


void CUYVYtoP210VideoFrameFormatter::ConvertAVX2(
	const uint8_t* sourceFrame, uint16_t* destinationY,
	uint16_t* destinationUV) const
{
	const __m256i yShuffle = _mm256_setr_epi8(
		1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1,
		1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1);
	const __m256i uvShuffle = _mm256_setr_epi8(
		0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1,
		0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1);
	const uint32_t vectorWidth = m_width & ~15U;

	for (uint32_t line = 0; line < m_height; ++line)
	{
		const uint8_t* source = sourceFrame + static_cast<size_t>(line) * m_sourceStride;
		uint16_t* y = destinationY + static_cast<size_t>(line) * m_width;
		uint16_t* uv = destinationUV + static_cast<size_t>(line) * m_width;
		uint32_t x = 0;
		for (; x < vectorWidth; x += 16)
		{
			const __m256i packed = _mm256_loadu_si256(
				reinterpret_cast<const __m256i*>(source));
			const __m256i y16 = _mm256_slli_epi16(
				_mm256_cvtepu8_epi16(GatherAlternatingBytes(packed, yShuffle)), 8);
			const __m256i uv16 = _mm256_slli_epi16(
				_mm256_cvtepu8_epi16(GatherAlternatingBytes(packed, uvShuffle)), 8);
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(y), y16);
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(uv), uv16);
			source += 32;
			y += 16;
			uv += 16;
		}
		for (; x < m_width; x += 2)
		{
			*uv++ = static_cast<uint16_t>(*source++) << 8;
			*y++ = static_cast<uint16_t>(*source++) << 8;
			*uv++ = static_cast<uint16_t>(*source++) << 8;
			*y++ = static_cast<uint16_t>(*source++) << 8;
		}
	}
}

LONG CUYVYtoP210VideoFrameFormatter::GetOutFrameSize() const
{
	const uint64_t bytes = static_cast<uint64_t>(m_width) * m_height *
		2U * sizeof(uint16_t);
	if (bytes > static_cast<uint64_t>(std::numeric_limits<LONG>::max()))
		throw std::runtime_error("UYVY P210 output frame is too large");
	return static_cast<LONG>(bytes);
}
