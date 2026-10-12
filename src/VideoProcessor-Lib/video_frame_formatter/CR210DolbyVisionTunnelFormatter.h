#pragma once

#include <video_frame_formatter/IVideoFrameFormatter.h>
#include <Windows.h>
#include <cstdint>

// DeckLink r210 carries the HDMI 8-bit RGB tunnel. Each pixel contains one
// 12-bit I sample and one alternating 12-bit chroma sample.
class CR210DolbyVisionTunnelFormatter final : public IVideoFrameFormatter
{
public:
	CR210DolbyVisionTunnelFormatter();
	~CR210DolbyVisionTunnelFormatter() override;
	CR210DolbyVisionTunnelFormatter(const CR210DolbyVisionTunnelFormatter&) = delete;
	CR210DolbyVisionTunnelFormatter& operator=(const CR210DolbyVisionTunnelFormatter&) = delete;
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
	static constexpr uint32_t MAX_WORKERS = 2;
	enum class ByteAlignment { UNKNOWN, LEFT, RIGHT };
	void ConvertRows(const uint8_t* source, uint16_t* output,
		uint32_t firstRow, uint32_t rowCount, bool leftAligned) const;
	void ConvertRowsAVX2(const uint8_t* source, uint16_t* output,
		uint32_t firstRow, uint32_t rowCount, bool leftAligned) const;
	static void CALLBACK ConversionWorkCallback(
		PTP_CALLBACK_INSTANCE instance, PVOID context, PTP_WORK work);
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
	bool m_hasAVX2 = false;
	PTP_WORK m_conversionWork[MAX_WORKERS] = {};
	uint32_t m_workerCount = 0;
	const uint8_t* m_workerSource = nullptr;
	uint16_t* m_workerOutput = nullptr;
	bool m_workerLeftAligned = false;
	uint32_t m_workerFirstRow[MAX_WORKERS] = {};
	uint32_t m_workerRowCount[MAX_WORKERS] = {};
};
