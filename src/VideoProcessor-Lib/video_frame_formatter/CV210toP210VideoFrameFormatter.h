/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <video_frame_formatter/IVideoFrameFormatter.h>


 /**
  * Video frame formatter which reads V210 and write to P210
  * (packed to planar conversion)
  */
class CV210toP210VideoFrameFormatter:
	public IVideoFrameFormatter
{
public:
	enum class ConversionMethod { AUTO, SCALAR, AVX2 };
	CV210toP210VideoFrameFormatter();

	virtual ~CV210toP210VideoFrameFormatter() {}

	// IVideoFrameFormatter
	void OnVideoState(VideoStateComPtr& videoState) override;
	bool FormatVideoFrame(const VideoFrame& inFrame, BYTE* outBuffer) override;
	LONG GetOutFrameSize() const override;
	VideoFrameFormatterOutputContract GetOutputContract() const override
	{
		return { VideoFrameSampleRange::LIMITED, 10, 6 };
	}
	void SetConversionMethod(ConversionMethod method) { m_conversionMethod = method; }

private:
	uint32_t m_height = 0;
	uint32_t m_width = 0;
	uint32_t m_sourceStride = 0;
	bool m_hasAVX2 = false;
	ConversionMethod m_conversionMethod = ConversionMethod::AUTO;
	void ConvertScalar(const uint8_t* sourceFrame, uint16_t* destinationY,
		uint16_t* destinationUV) const;
	void ConvertAVX2(const uint8_t* sourceFrame, uint16_t* destinationY,
		uint16_t* destinationUV) const;
};
