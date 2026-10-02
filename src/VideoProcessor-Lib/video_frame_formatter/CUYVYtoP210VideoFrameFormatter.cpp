#include <pch.h>
#include <VideoFrame.h>
#include <VideoState.h>
#include <CpuFeatures.h>

#include "CUYVYtoP210VideoFrameFormatter.h"


CUYVYtoP210VideoFrameFormatter::CUYVYtoP210VideoFrameFormatter() :
	m_hasAVX2(CpuFeatures::SupportsAvx2Kernels())
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


LONG CUYVYtoP210VideoFrameFormatter::GetOutFrameSize() const
{
	const uint64_t bytes = static_cast<uint64_t>(m_width) * m_height *
		2U * sizeof(uint16_t);
	if (bytes > static_cast<uint64_t>(std::numeric_limits<LONG>::max()))
		throw std::runtime_error("UYVY P210 output frame is too large");
	return static_cast<LONG>(bytes);
}
