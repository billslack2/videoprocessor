/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <cstdint>

#include <VideoFrame.h>
#include <VideoState.h>


enum class VideoFrameSampleRange
{
	UNKNOWN,
	FULL,
	LIMITED,
};


// Describes the code values written by a formatter. Consumers must use this
// instead of inferring range or precision from the captured input format.
struct VideoFrameFormatterOutputContract
{
	VideoFrameSampleRange sampleRange = VideoFrameSampleRange::UNKNOWN;
	uint8_t colorDepth = 0;
	uint8_t bitShift = 0;

	bool IsValid() const
	{
		return sampleRange != VideoFrameSampleRange::UNKNOWN &&
			colorDepth != 0;
	}
};


/**
 * Interface for a video frame formatter, this can render from one
 * format to another.
 */
class IVideoFrameFormatter
{
public:

	virtual ~IVideoFrameFormatter() {}

	// New video state, must be called before FormatVideoFrame()
	virtual void OnVideoState(VideoStateComPtr& videoState) = 0;

	// Handle video frame
	// Can only be called after OnVideoState()
	// Returns true if something was converted, false if not
	virtual bool FormatVideoFrame(const VideoFrame& inFrame, BYTE* outBuffer) = 0;

	// Get size of frame that will be put in FormatVideoFrame()'s outBuffer, in bytes
	// Can only be called after OnVideoState()
	virtual LONG GetOutFrameSize() const = 0;

	// Get the sample-range and precision contract for FormatVideoFrame output.
	virtual VideoFrameFormatterOutputContract GetOutputContract() const
	{
		return {};
	}
	
	// Get conversion performance metrics (optional, default implementation returns no data)
	// currentUs: Latest frame conversion time in microseconds
	// avg10s: Average conversion time over last 10 seconds (?s)
	// max10s: Maximum conversion time over last 10 seconds (?s)
	virtual void GetConversionPerformance(double& currentUs, double& avg10s, double& max10s) const
	{
		currentUs = 0.0;
		avg10s = 0.0;
		max10s = 0.0;
	}
};
