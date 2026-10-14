/*
 * Copyright(C) 2026 Bill Slack
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <pch.h>
#include <VideoFrame.h>
#include <VideoState.h>
#include <CpuFeatures.h>
#include "CR210DolbyVisionTunnelFormatter.h"

#include <array>
#include <limits>

namespace
{
	// HDMI 1.4a section 6.5.4: RGB8 tunnel -> 12-bit 4:2:2.
	// ETSI GS CCM 001 sections 6.3-6.4: parity-scrambled metadata packets.
	constexpr size_t kPacketBits = 1024;
	constexpr size_t kPacketBytes = 128;
	constexpr size_t kRepeats = 3;
	constexpr size_t kMaxMetadataBytes = 0x2F00;
	constexpr uint32_t kPacketLossLimit = 24;
	constexpr uint32_t kDiagnosticInterval = 600;

	uint32_t ReadBigEndian32(const uint8_t* bytes)
	{
		return (static_cast<uint32_t>(bytes[0]) << 24) |
			(static_cast<uint32_t>(bytes[1]) << 16) |
			(static_cast<uint32_t>(bytes[2]) << 8) |
			static_cast<uint32_t>(bytes[3]);
	}

	int16_t ReadSigned16(const uint8_t* bytes)
	{
		return static_cast<int16_t>(
			(static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
	}

	uint16_t ReadUnsigned16(const uint8_t* bytes)
	{
		return static_cast<uint16_t>(
			(static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
	}

	uint8_t Parity(uint32_t bits)
	{
		bits ^= bits >> 16;
		bits ^= bits >> 8;
		bits ^= bits >> 4;
		bits ^= bits >> 2;
		bits ^= bits >> 1;
		return static_cast<uint8_t>(bits & 1U);
	}

	uint32_t PacketCrc(const uint8_t* bytes)
	{
		// ISO/IEC 13818-1 CRC-32, specified by ETSI for the 128-byte packet.
		uint32_t crc = 0xFFFFFFFFU;
		for (size_t i = 0; i < kPacketBytes; ++i)
		{
			crc ^= static_cast<uint32_t>(bytes[i]) << 24;
			for (int bit = 0; bit < 8; ++bit)
				crc = (crc & 0x80000000U) ?
					(crc << 1) ^ 0x04C11DB7U : crc << 1;
		}
		return crc;
	}

	struct Rgb8 { uint8_t r, g, b; };

	Rgb8 DecodeRgb(uint32_t packed, bool leftAligned)
	{
		const unsigned shift = leftAligned ? 2U : 0U;
		return {
			static_cast<uint8_t>(((packed >> 20) & 0x3FFU) >> shift),
			static_cast<uint8_t>(((packed >> 10) & 0x3FFU) >> shift),
			static_cast<uint8_t>((packed & 0x3FFU) >> shift)
		};
	}

	// Equivalent to the original RGB8 unpack and 12-bit I/C reconstruction.
	// Keep each shift constant inside the hot loop for the scalar fallback.
	template<unsigned Shift>
	void ConvertRowsScalar(const uint8_t* source, uint16_t* output,
		uint32_t width, uint32_t height, uint32_t stride,
		uint32_t firstRow, uint32_t rowCount)
	{
		const size_t planePixels = static_cast<size_t>(width) * height;
		for (uint32_t row = firstRow; row < firstRow + rowCount; ++row)
		{
			const uint8_t* input = source + static_cast<size_t>(row) * stride;
			uint16_t* y = output + static_cast<size_t>(row) * width;
			uint16_t* uv = output + planePixels + static_cast<size_t>(row) * width;
			for (uint32_t x = 0; x < width; ++x)
			{
				const uint32_t p = ReadBigEndian32(input + static_cast<size_t>(x) * 4);
				y[x] = static_cast<uint16_t>(
					((p >> (2 + Shift)) & 0xFF00U) |
					((p << (4 - Shift)) & 0xF0U));
				uv[x] = static_cast<uint16_t>(
					((p >> (12 + Shift)) & 0xFF00U) |
					((p >> Shift) & 0xF0U));
			}
		}
	}

	struct ParsedPacket
	{
		DolbyVisionTunnelFrameMetadata metadata;
		uint8_t affectedId = 0;
		uint8_t currentId = 0;
		bool noMetadata = false;
		bool endOfStream = false;
	};

	// Header validation is separate from metadata decoding: a CRC-valid header
	// proves a DV transport even when a later packet/body is damaged.
	bool ParseHeader(const uint8_t* packet, ParsedPacket& result)
	{
		if ((packet[0] & 0x3EU) != 0 || (packet[2] & 0xFEU) != 0)
			return false;
		result.affectedId = packet[1] >> 4;
		result.currentId = packet[1] & 15U;
		result.endOfStream = (packet[2] & 1U) != 0;
		result.noMetadata = (packet[0] & 1U) != 0;
		return result.affectedId == result.currentId ||
			result.affectedId == ((result.currentId + 1U) & 15U);
	}

	bool ParseMetadata(const uint8_t* dm, size_t length, ParsedPacket& result,
		std::array<bool, 256>& extensionLevels)
	{
		if (length < 71)
			return false;
		if (dm[60] != 12 || (dm[61] != 0 && dm[61] != 2))
			return false; // This unpacker accepts 12-bit opponent color only.
		DolbyVisionTunnelFrameMetadata parsed;
		parsed.valid = true;
		parsed.fullRange = dm[63] != 0;
		parsed.sourceMinPq = ReadUnsigned16(dm + 64);
		parsed.sourceMaxPq = ReadUnsigned16(dm + 66);
		for (size_t row = 0; row < 3; ++row)
		{
			for (size_t col = 0; col < 3; ++col)
			{
				const size_t index = row * 3 + col;
				parsed.nonlinear[row][col] =
					static_cast<float>(ReadSigned16(dm + 2 + index * 2)) /
					8192.0f;
				parsed.linear[row][col] =
					static_cast<float>(ReadSigned16(dm + 32 + index * 2)) /
					16384.0f;
			}
			parsed.nonlinearOffset[row] =
				static_cast<float>(ReadBigEndian32(dm + 20 + row * 4)) /
				268435456.0f;
		}
		// Reassembly has already validated the complete declared body. Unknown
		// extension levels are bounded/skipped; they are not display trims.
		size_t extension = 71;
		for (unsigned block = 0; block < dm[70]; ++block)
		{
			if (length - extension < 5)
				return false;
			const uint32_t payloadLength = ReadBigEndian32(dm + extension);
			const uint8_t level = dm[extension + 4];
			if (payloadLength > length - extension - 5)
				return false;
			const size_t end = extension + 5 + payloadLength;
			extensionLevels[level] = true;
			if (level == 1)
			{
				if (payloadLength != 6)
					return false;
				const uint16_t sceneMin = ReadUnsigned16(dm + extension + 5);
				const uint16_t sceneMax = ReadUnsigned16(dm + extension + 7);
				const uint16_t sceneAvg = ReadUnsigned16(dm + extension + 9);
				if (sceneMin > 4095 || sceneMax > 4095 || sceneAvg > 4095)
					return false;
				parsed.hasSceneMetadata = true;
				parsed.sceneMaxPq = sceneMax;
				parsed.sceneAvgPq = sceneAvg;
			}
			extension = end;
		}
		result.metadata = parsed;
		return true;
	}
}

CR210DolbyVisionTunnelFormatter::CR210DolbyVisionTunnelFormatter()
{
	m_hasAVX2 = CpuFeatures::SupportsAvx2Kernels();
	for (uint32_t i = 0; i < MAX_WORKERS; ++i)
	{
		m_conversionWork[i] = CreateThreadpoolWork(ConversionWorkCallback, this, nullptr);
		if (!m_conversionWork[i])
			break;
		++m_workerCount;
	}
}

CR210DolbyVisionTunnelFormatter::~CR210DolbyVisionTunnelFormatter()
{
	for (uint32_t i = 0; i < m_workerCount; ++i)
	{
		WaitForThreadpoolWorkCallbacks(m_conversionWork[i], TRUE);
		CloseThreadpoolWork(m_conversionWork[i]);
	}
}

void CR210DolbyVisionTunnelFormatter::OnVideoState(VideoStateComPtr& state)
{
	if (!state || !state->displayMode ||
		state->videoFrameEncoding != VideoFrameEncoding::R210 ||
		!state->dolbyVisionTunnel)
		throw std::runtime_error("Dolby Vision tunnel requires enabled R210 capture");
	const auto width = state->displayMode->FrameWidth();
	const auto height = state->displayMode->FrameHeight();
	if (width <= 0 || height <= 0 || (width & 1) != 0)
		throw std::runtime_error("Dolby Vision tunnel requires positive even width");
	const uint64_t outputSize =
		static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4U;
	if (outputSize > static_cast<uint64_t>(std::numeric_limits<LONG>::max()))
		throw std::runtime_error("Dolby Vision tunnel output frame too large");
	m_width = static_cast<uint32_t>(width);
	m_height = static_cast<uint32_t>(height);
	m_stride = state->BytesPerRow();
	if (m_stride < m_width * 4U)
		throw std::runtime_error("R210 stride is shorter than the active width");
	m_outputSize = static_cast<LONG>(outputSize);
	m_alignment = ByteAlignment::UNKNOWN;
	m_missingPacketFrames = 0;
	m_missingPacketReported = false;
	m_tunnelConfirmed = false;
	m_diagnosticFrames = 0;
	m_crcFailedCopies = 0;
	m_recoveredPackets = 0;
	m_rejectedFrames = 0;
	m_noMetadataFrames = 0;
	m_lastPacketCount = 0;
	m_lastMetadataLength = 0;
	m_metadataIdTransitions = 0;
	m_lastCurrentId = m_lastAffectedId = 0;
	m_haveDiagnosticIds = false;
	m_seenExtensionLevels.fill(false);
	m_metadata = {};
	m_pendingMetadata = {};
	m_hasPendingMetadata = false;
	m_pendingAffectedId = 0;
	m_pendingCurrentId = 0;
	m_pendingAfterCounter = 0;
	m_activeMetadataId = 0;
	m_hasActiveMetadataId = false;
	m_endOfStreamAfterCounter = 0;
	m_hasEndOfStream = false;
	DebugLog::Log("Dolby Vision RGB tunnel unpack: %s, row workers=%u",
		m_hasAVX2 ? "AVX2" : "scalar", m_workerCount);
}

bool CR210DolbyVisionTunnelFormatter::FormatVideoFrame(
	const VideoFrame& frame, BYTE* output)
{
	if (!frame.GetData() || !output || m_outputSize <= 0)
		return false;
	const auto* source = static_cast<const uint8_t*>(frame.GetData());
	// EOS changes the immediate next picture to SDR even when its packet
	// has no metadata body. A source counter gap can skip that picture, but
	// every later captured picture is still beyond the HDR stream boundary.
	if (m_hasEndOfStream &&
		frame.GetCounter() > m_endOfStreamAfterCounter)
	{
		m_metadata = {};
		m_hasActiveMetadataId = false;
		m_hasPendingMetadata = false;
		m_hasEndOfStream = false;
		m_tunnelConfirmed = false;
		DebugLog::Log("Dolby Vision RGB tunnel: EOS boundary; allowing SDR RGB passthrough");
	}
	// A packet may announce new DM metadata for the immediate next picture.
	// A contiguous source counter proves that this is that picture. If VP
	// skipped it, wait for a CRC-valid packet naming the affected metadata ID.
	if (m_hasPendingMetadata &&
		frame.GetCounter() == m_pendingAfterCounter + 1U)
	{
		m_metadata = m_pendingMetadata;
		m_activeMetadataId = m_pendingAffectedId;
		m_hasActiveMetadataId = true;
		m_hasPendingMetadata = false;
	}
	auto readWord = [this, source](size_t pixel)
	{
		const size_t y = pixel / m_width;
		const size_t x = pixel % m_width;
		return ReadBigEndian32(source + y * m_stride + x * 4);
	};

	bool acceptedMetadata = false;
	bool supportedHeader = false;
	const char* failureReason = "missing/CRC-invalid first packet";
	// Each packet occupies 3072 pixels: three independent CRC-protected
	// copies. Never span pictures or include row padding in the raster.
	const size_t availablePackets = static_cast<size_t>(m_width) * m_height /
		(kPacketBits * kRepeats);
	auto readPacket = [&](size_t index, ByteAlignment candidate,
		std::array<uint8_t, kPacketBytes>& packet)
	{
		if (index >= availablePackets)
			return false;
		for (size_t copy = 0; copy < kRepeats; ++copy)
		{
			packet.fill(0);
			for (size_t bit = 0; bit < kPacketBits; ++bit)
			{
				const Rgb8 rgb = DecodeRgb(readWord(
					(index * kRepeats + copy) * kPacketBits + bit),
					candidate == ByteAlignment::LEFT);
				packet[bit / 8] |= static_cast<uint8_t>(Parity(
					(static_cast<uint32_t>(rgb.r) << 16) |
					(static_cast<uint32_t>(rgb.g) << 8) | rgb.b) << (7 - bit % 8));
			}
			if (PacketCrc(packet.data()) == 0)
			{
				if (copy != 0)
					++m_recoveredPackets;
				return true;
			}
			++m_crcFailedCopies;
		}
		return false;
	};

	// Prefer the current alignment so normal low8 capture does not probe a
	// known-wrong layout on every picture. Only CRC can confirm the tunnel.
	const ByteAlignment firstCandidate = m_alignment == ByteAlignment::RIGHT ?
		ByteAlignment::RIGHT : ByteAlignment::LEFT;
	for (const ByteAlignment candidate : { firstCandidate,
		firstCandidate == ByteAlignment::LEFT ? ByteAlignment::RIGHT : ByteAlignment::LEFT })
	{
		std::array<uint8_t, kPacketBytes> packet{};
		ParsedPacket parsed;
		if (!readPacket(0, candidate, packet) ||
			(packet[0] >> 6) > 1 || !ParseHeader(packet.data(), parsed))
			continue;
		supportedHeader = true;
		if (m_haveDiagnosticIds && (parsed.currentId != m_lastCurrentId ||
			parsed.affectedId != m_lastAffectedId))
			++m_metadataIdTransitions;
		m_lastCurrentId = parsed.currentId;
		m_lastAffectedId = parsed.affectedId;
		m_haveDiagnosticIds = true;
		if (!m_tunnelConfirmed || m_alignment != candidate)
			DebugLog::Log("Dolby Vision RGB tunnel: CRC-validated r210 RGB8 alignment=%s",
				candidate == ByteAlignment::LEFT ? "high8" : "low8");
		m_tunnelConfirmed = true;
		m_alignment = candidate;
		// Header information remains usable even if a later packet fails.
		if (m_hasPendingMetadata)
		{
			if (parsed.currentId == m_pendingAffectedId)
			{
				m_metadata = m_pendingMetadata;
				m_activeMetadataId = m_pendingAffectedId;
				m_hasActiveMetadataId = true;
				m_hasPendingMetadata = false;
			}
			else if (parsed.currentId != m_pendingCurrentId)
				m_hasPendingMetadata = false;
		}
		if (m_hasActiveMetadataId && parsed.currentId != m_activeMetadataId)
		{
			m_metadata = {};
			m_hasActiveMetadataId = false;
		}
		if (parsed.endOfStream)
		{
			m_hasPendingMetadata = false;
			m_endOfStreamAfterCounter = frame.GetCounter();
			m_hasEndOfStream = true;
		}

		size_t length = 0;
		size_t packetCount = 1;
		std::array<bool, 256> extensionLevels{};
		if (!parsed.noMetadata)
		{
			length = ReadUnsigned16(packet.data() + 3);
			if (length < 71 || length > kMaxMetadataBytes ||
				((packet[0] >> 6) == 0 ? length > 119 : length <= 119))
			{
				failureReason = "invalid declared metadata length/type";
				break;
			}
			packetCount += length > 119 ? (length - 119 + 120) / 121 : 0;
			if (packetCount > availablePackets)
			{
				failureReason = "declared packets exceed picture raster";
				break;
			}
			// Bounded stack storage avoids a per-picture heap allocation.
			std::array<uint8_t, kMaxMetadataBytes> body;
			size_t copied = (std::min)(length, size_t(119));
			memcpy(body.data(), packet.data() + 5, copied);
			const uint8_t headerFlags = packet[0] & 0x3FU;
			const uint8_t headerIds = packet[1];
			const uint8_t headerEos = packet[2];
			bool complete = true;
			for (size_t index = 1; index < packetCount; ++index)
			{
				const unsigned expectedKind = index + 1 == packetCount ? 3 : 2;
				if (!readPacket(index, candidate, packet) ||
					(packet[0] >> 6) != expectedKind ||
					(packet[0] & 0x3FU) != headerFlags ||
					packet[1] != headerIds || packet[2] != headerEos)
				{
					complete = false;
					failureReason = "missing/invalid continuation packet";
					break;
				}
				const size_t chunk = (std::min)(length - copied, size_t(121));
				memcpy(body.data() + copied, packet.data() + 3, chunk);
				copied += chunk;
			}
			if (!complete || copied != length)
				break;
			if (!ParseMetadata(body.data(), length, parsed, extensionLevels))
			{
				failureReason = "unsupported/malformed metadata body";
				break;
			}
		}

		if (m_missingPacketReported && m_missingPacketFrames >= kPacketLossLimit)
			DebugLog::Log("Dolby Vision RGB tunnel: metadata transport recovered after %u rejected/missing pictures; fresh_DM=%s",
				m_missingPacketFrames, parsed.noMetadata ? "no" : "yes");
		if (!parsed.noMetadata && (packetCount != m_lastPacketCount ||
			length != m_lastMetadataLength))
		{
			DebugLog::Log("Dolby Vision RGB tunnel: complete DM bytes=%zu packets=%zu current_id=%u affected_id=%u",
				length, packetCount, parsed.currentId, parsed.affectedId);
			m_lastPacketCount = packetCount;
			m_lastMetadataLength = length;
		}
		for (size_t level = 0; level < extensionLevels.size(); ++level)
		{
			if (extensionLevels[level] && !m_seenExtensionLevels[level])
			{
				DebugLog::Log("Dolby Vision RGB tunnel: extension L%zu %s",
					level, level == 1 ? "scene PQ values applied" : "recognized in transport; not applied");
				m_seenExtensionLevels[level] = true;
			}
		}
		parsed.metadata.rgbBitAlignment = candidate == ByteAlignment::LEFT ? 1 : 2;
		if (parsed.noMetadata)
			++m_noMetadataFrames; // Healthy transport; active DM remains valid.
		else if (parsed.affectedId == parsed.currentId)
		{
			m_metadata = parsed.metadata;
			m_activeMetadataId = parsed.currentId;
			m_hasActiveMetadataId = true;
		}
		else if (!parsed.endOfStream)
		{
			m_pendingMetadata = parsed.metadata;
			m_pendingAffectedId = parsed.affectedId;
			m_pendingCurrentId = parsed.currentId;
			m_pendingAfterCounter = frame.GetCounter();
			m_hasPendingMetadata = true;
		}
		acceptedMetadata = true;
		break;
	}
	if (acceptedMetadata)
	{
		m_missingPacketFrames = 0;
		m_missingPacketReported = false;
	}
	else
	{
		++m_rejectedFrames;
		if (m_missingPacketFrames != (std::numeric_limits<uint32_t>::max)())
			++m_missingPacketFrames;
		if (!m_missingPacketReported && m_missingPacketFrames >= kPacketLossLimit)
		{
			DebugLog::Log("Dolby Vision RGB tunnel: %u pictures without usable metadata (last_header=%s reason=%s); %s",
				m_missingPacketFrames, supportedHeader ? "CRC-valid" : "missing/CRC-invalid", failureReason,
				m_tunnelConfirmed ? "suspending DV until fresh DM; SDR fallback blocked" :
				"no DV lock; allowing SDR RGB8 when alignment is known");
			m_missingPacketReported = true;
		}
		if (m_tunnelConfirmed && m_missingPacketFrames >= kPacketLossLimit)
		{
			m_metadata = {};
			m_hasActiveMetadataId = false;
			m_hasPendingMetadata = false;
		}
	}
	if (++m_diagnosticFrames >= kDiagnosticInterval)
	{
		DebugLog::Log("Dolby Vision RGB tunnel: last %u pictures: rejected=%u no_md=%u CRC_failed_copies=%u repeat_recoveries=%u id_transitions=%u current_id=%u affected_id=%u active_DM=%s",
			m_diagnosticFrames, m_rejectedFrames, m_noMetadataFrames,
			m_crcFailedCopies, m_recoveredPackets, m_metadataIdTransitions,
			m_lastCurrentId, m_lastAffectedId, m_metadata.valid ? "yes" : "no");
		m_diagnosticFrames = m_rejectedFrames = m_noMetadataFrames = 0;
		m_crcFailedCopies = m_recoveredPackets = m_metadataIdTransitions = 0;
	}
	if (m_alignment == ByteAlignment::UNKNOWN)
	{
		// Alignment alone does not identify Dolby Vision. It does let
		// ordinary 8-bit RGB menus use the SDR passthrough path.
		const size_t sampleCount = (std::min)(
			static_cast<size_t>(m_width) * m_height, size_t(4096));
		size_t high8 = 0, low8 = 0;
		for (size_t pixel = 0; pixel < sampleCount; ++pixel)
		{
			const uint32_t word = readWord(pixel);
			const uint32_t r = (word >> 20) & 0x3FFU;
			const uint32_t g = (word >> 10) & 0x3FFU;
			const uint32_t b = word & 0x3FFU;
			high8 += ((r | g | b) & 3U) == 0;
			low8 += ((r | g | b) & 0x300U) == 0;
		}
		if (sampleCount > 0 && high8 * 100 >= sampleCount * 98 &&
			high8 > low8 * 2)
			m_alignment = ByteAlignment::LEFT;
		else if (sampleCount > 0 && low8 * 100 >= sampleCount * 98 &&
			low8 > high8 * 2)
			m_alignment = ByteAlignment::RIGHT;
	}
	if (!m_metadata.valid)
		m_metadata.rgbBitAlignment =
			m_alignment == ByteAlignment::LEFT ? 1 :
			m_alignment == ByteAlignment::RIGHT ? 2 : 0;
	if (m_alignment == ByteAlignment::UNKNOWN || !m_metadata.valid)
	{
		// The renderer may use SDR only before DV lock or after explicit EOS.
		// Invalid metadata after DV lock must not expose the packed RGB pixels.
		memset(output, 0, static_cast<size_t>(m_outputSize));
		return true;
	}

	// P216-style semiplanar output, with exact 12-bit I/C codes.
	const bool leftAligned = m_alignment == ByteAlignment::LEFT;
	auto* outputWords = reinterpret_cast<uint16_t*>(output);
	if (m_workerCount > 0 &&
		static_cast<uint64_t>(m_width) * m_height >= 1920ULL * 1080ULL)
	{
		m_workerSource = source;
		m_workerOutput = outputWords;
		m_workerLeftAligned = leftAligned;
		const uint32_t rowsPerSegment = m_height / (m_workerCount + 1);
		uint32_t firstRow = 0;
		for (uint32_t i = 0; i < m_workerCount; ++i)
		{
			m_workerFirstRow[i] = firstRow;
			m_workerRowCount[i] = rowsPerSegment;
			firstRow += rowsPerSegment;
			SubmitThreadpoolWork(m_conversionWork[i]);
		}
		ConvertRows(source, outputWords, firstRow, m_height - firstRow, leftAligned);
		for (uint32_t i = 0; i < m_workerCount; ++i)
			WaitForThreadpoolWorkCallbacks(m_conversionWork[i], FALSE);
	}
	else
		ConvertRows(source, outputWords, 0, m_height, leftAligned);
	return true;
}

void CR210DolbyVisionTunnelFormatter::ConvertRows(
	const uint8_t* source, uint16_t* output, uint32_t firstRow,
	uint32_t rowCount, bool leftAligned) const
{
	if (m_hasAVX2 && m_conversionMethod != ConversionMethod::SCALAR)
	{
		ConvertRowsAVX2(source, output, firstRow, rowCount, leftAligned);
		return;
	}
	if (leftAligned)
		ConvertRowsScalar<2>(source, output, m_width, m_height, m_stride,
			firstRow, rowCount);
	else
		ConvertRowsScalar<0>(source, output, m_width, m_height, m_stride,
			firstRow, rowCount);
}

void CALLBACK CR210DolbyVisionTunnelFormatter::ConversionWorkCallback(
	PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK work)
{
	auto* formatter = static_cast<CR210DolbyVisionTunnelFormatter*>(context);
	uint32_t index = 0;
	while (index < formatter->m_workerCount &&
		formatter->m_conversionWork[index] != work)
		++index;
	if (index == formatter->m_workerCount)
		return;
	formatter->ConvertRows(formatter->m_workerSource, formatter->m_workerOutput,
		formatter->m_workerFirstRow[index], formatter->m_workerRowCount[index],
		formatter->m_workerLeftAligned);
}
