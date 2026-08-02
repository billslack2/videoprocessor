#pragma once

#include <video_frame_formatter/IVideoFrameFormatter.h>

// Lossless packed UYVY/HDYC 4:2:2 to planar P210. Unlike the P010 formatter,
// this preserves the independently captured chroma row instead of averaging it
// with the following line.
class CUYVYtoP210VideoFrameFormatter : public IVideoFrameFormatter
{
public:
	void OnVideoState(VideoStateComPtr& videoState) override;
	bool FormatVideoFrame(const VideoFrame& inFrame, BYTE* outBuffer) override;
	LONG GetOutFrameSize() const override;
	VideoFrameFormatterOutputContract GetOutputContract() const override
	{
		return { VideoFrameSampleRange::LIMITED, 8, 8 };
	}

private:
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	uint32_t m_sourceStride = 0;
};
