#pragma once

#include <video_frame_formatter/IVideoFrameFormatter.h>
#include <Windows.h>
#include <cstdint>
#include <array>

// DeckLink r210 carries the HDMI 8-bit RGB tunnel. Each pixel contains one
// 12-bit I sample and one alternating 12-bit chroma sample.
class CR210DolbyVisionTunnelFormatter final : public IVideoFrameFormatter
{
public:
	// Explicit dispatch also permits the scalar and SIMD paths to be qualified.
	enum class ConversionMethod { AUTO, SCALAR, AVX2 };
	void SetConversionMethod(ConversionMethod method) { m_conversionMethod = method; }
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
		auto metadata = m_metadata;
		metadata.allowSdrFallback = !m_tunnelConfirmed;
		metadata.requiresBlackFrame = m_tunnelConfirmed &&
			(!m_currentPictureHasValidHeader || !metadata.valid);
		return metadata;
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
	bool m_tunnelConfirmed = false;
	bool m_currentPictureHasValidHeader = false;
	uint32_t m_diagnosticFrames = 0;
	uint32_t m_crcFailedCopies = 0;
	uint32_t m_recoveredPackets = 0;
	uint32_t m_rejectedFrames = 0;
	uint32_t m_noMetadataFrames = 0;
	size_t m_lastPacketCount = 0;
	size_t m_lastMetadataLength = 0;
	uint32_t m_metadataIdTransitions = 0;
	uint8_t m_lastCurrentId = 0;
	uint8_t m_lastAffectedId = 0;
	bool m_haveDiagnosticIds = false;
	std::array<bool, 256> m_seenExtensionLevels{};
	DolbyVisionTunnelFrameMetadata m_metadata;
	DolbyVisionTunnelFrameMetadata m_pendingMetadata;
	bool m_hasPendingMetadata = false;
	uint8_t m_pendingAffectedId = 0;
	uint8_t m_pendingCurrentId = 0;
	uint64_t m_pendingAfterCounter = 0;
	uint8_t m_activeMetadataId = 0;
	bool m_hasActiveMetadataId = false;
	uint64_t m_endOfStreamAfterCounter = 0;
	bool m_hasEndOfStream = false;
	bool m_hasAVX2 = false;
	ConversionMethod m_conversionMethod = ConversionMethod::AUTO;
	PTP_WORK m_conversionWork[MAX_WORKERS] = {};
	uint32_t m_workerCount = 0;
	const uint8_t* m_workerSource = nullptr;
	uint16_t* m_workerOutput = nullptr;
	bool m_workerLeftAligned = false;
	uint32_t m_workerFirstRow[MAX_WORKERS] = {};
	uint32_t m_workerRowCount[MAX_WORKERS] = {};
};
