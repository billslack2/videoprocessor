#pragma once

#include <video_frame_formatter/IVideoFrameFormatter.h>
#include <cstdint>

// DeckLink r210 carries the HDMI 8-bit RGB tunnel. Each pixel contains one
// 12-bit I sample and one alternating 12-bit chroma sample.
class CR210DolbyVisionTunnelFormatter final : public IVideoFrameFormatter
{
public:
	void OnVideoState(VideoStateComPtr& state) override;
	bool FormatVideoFrame(const VideoFrame& frame, BYTE* output) override;
	LONG GetOutFrameSize() const override { return m_outputSize; }
	VideoFrameFormatterOutputContract GetOutputContract() const override
	{
		return { m_metadata.fullRange ? VideoFrameSampleRange::FULL :
			VideoFrameSampleRange::LIMITED, 12, 4 };
	}
	DolbyVisionTunnelFrameMetadata GetDolbyVisionTunnelMetadata() const override
	{
		return m_metadata;
	}

private:
	enum class ByteAlignment { UNKNOWN, LEFT, RIGHT };
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	uint32_t m_stride = 0;
	LONG m_outputSize = 0;
	ByteAlignment m_alignment = ByteAlignment::UNKNOWN;
	uint32_t m_missingPacketFrames = 0;
	bool m_missingPacketReported = false;
	DolbyVisionTunnelFrameMetadata m_metadata;
	DolbyVisionTunnelFrameMetadata m_pendingMetadata;
	bool m_hasPendingMetadata = false;
};
