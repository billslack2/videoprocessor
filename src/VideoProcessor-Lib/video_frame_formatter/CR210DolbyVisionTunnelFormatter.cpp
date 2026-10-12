/*
 * Copyright(C) 2026 Bill Slack
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <pch.h>
#include <VideoFrame.h>
#include <VideoState.h>
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

	struct ParsedPacket
	{
		DolbyVisionTunnelFrameMetadata metadata;
		uint8_t affectedId = 0;
		uint8_t currentId = 0;
		bool noMetadata = false;
		bool endOfStream = false;
	};

	bool ParsePacket(const uint8_t* packet, ParsedPacket& result)
	{
		if (PacketCrc(packet) != 0)
			return false;
		const uint8_t kind = packet[0] >> 6;
		if (kind != 0 && kind != 1)
			return false; // A complete or first DM packet contains the matrices.
		if ((packet[0] & 0x3EU) != 0 || (packet[2] & 0xFEU) != 0)
			return false; // Unsupported metadata type/version.
		result.affectedId = packet[1] >> 4;
		result.currentId = packet[1] & 15U;
		result.endOfStream = (packet[2] & 1U) != 0;
		result.noMetadata = (packet[0] & 1U) != 0;
		if (result.noMetadata)
			return true;
		if (result.affectedId != result.currentId &&
			result.affectedId != ((result.currentId + 1U) & 15U))
			return false;
		const uint16_t length = ReadUnsigned16(packet + 3);
		if (length < 71 || (kind == 0 ? length > 119 : length <= 119))
			return false;
		// First 71 bytes of the DM structure fit in every first packet.
		const uint8_t* dm = packet + 5;
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
		// Level 1 carries scene maximum and average PQ values. These
		// are used by libplacebo's dynamic peak mapping when present in
		// the first packet. Other extension levels remain untouched.
		size_t extension = 71;
		for (unsigned block = 0; block < dm[70] && extension + 5 <=
			(static_cast<size_t>(length) < 119 ? length : 119); ++block)
		{
			const uint32_t payloadLength = ReadBigEndian32(dm + extension);
			const uint8_t level = dm[extension + 4];
			const size_t end = extension + 5 + payloadLength;
			if (end > length || end > 119)
				break;
			if (level == 1 && payloadLength == 6)
			{
				const uint16_t sceneMax = ReadUnsigned16(dm + extension + 7);
				const uint16_t sceneAvg = ReadUnsigned16(dm + extension + 9);
				if (sceneMax <= 4095 && sceneAvg <= 4095)
				{
					parsed.sceneMaxPq = sceneMax;
					parsed.sceneAvgPq = sceneAvg;
				}
			}
			extension = end;
		}
		result.metadata = parsed;
		return true;
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
	m_metadata = {};
	m_pendingMetadata = {};
	m_hasPendingMetadata = false;
}

bool CR210DolbyVisionTunnelFormatter::FormatVideoFrame(
	const VideoFrame& frame, BYTE* output)
{
	if (!frame.GetData() || !output || m_outputSize <= 0)
		return false;
	const auto* source = static_cast<const uint8_t*>(frame.GetData());
	// A packet can describe the immediate next picture. Apply that saved
	// metadata only when processing the next captured frame.
	if (m_hasPendingMetadata)
	{
		m_metadata = m_pendingMetadata;
		m_hasPendingMetadata = false;
	}
	auto readWord = [this, source](size_t pixel)
	{
		const size_t y = pixel / m_width;
		const size_t x = pixel % m_width;
		return ReadBigEndian32(source + y * m_stride + x * 4);
	};

	// DeckLink may preserve RGB8 in either end of each 10-bit channel.
	// Accept an alignment only when the Dolby DM packet passes its CRC.
	if (static_cast<uint64_t>(m_width) * m_height >= kPacketBits * kRepeats)
	{
		std::array<uint8_t, kPacketBytes> packet{};
		for (const ByteAlignment candidate :
			{ ByteAlignment::LEFT, ByteAlignment::RIGHT })
		{
			for (size_t copy = 0; copy < kRepeats; ++copy)
			{
				packet.fill(0);
				for (size_t bit = 0; bit < kPacketBits; ++bit)
				{
					const Rgb8 rgb = DecodeRgb(
						readWord(copy * kPacketBits + bit),
						candidate == ByteAlignment::LEFT);
					const uint8_t metadataBit = Parity(
						(static_cast<uint32_t>(rgb.r) << 16) |
						(static_cast<uint32_t>(rgb.g) << 8) | rgb.b);
					packet[bit / 8] |= static_cast<uint8_t>(
						metadataBit << (7 - bit % 8));
				}
				ParsedPacket parsed;
				if (!ParsePacket(packet.data(), parsed))
					continue;
				if (m_alignment != candidate)
					DebugLog::Log("Dolby Vision RGB tunnel: CRC-validated r210 RGB8 alignment=%s",
						candidate == ByteAlignment::LEFT ? "high8" : "low8");
				m_alignment = candidate;
				parsed.metadata.rgbBitAlignment =
					candidate == ByteAlignment::LEFT ? 1 : 2;
				if (parsed.noMetadata)
					m_metadata = {};
				else if (parsed.affectedId == parsed.currentId)
					m_metadata = parsed.metadata;
				else
				{
					m_pendingMetadata = parsed.metadata;
					m_hasPendingMetadata = true;
				}
				if (parsed.endOfStream)
				{
					m_pendingMetadata = {};
					m_hasPendingMetadata = true;
				}
				m_missingPacketFrames = 0;
				m_missingPacketReported = parsed.noMetadata;
				goto metadata_complete;
			}
		}
	}
metadata_complete:
	if (!m_metadata.valid && !m_missingPacketReported &&
		++m_missingPacketFrames >= 24)
	{
		DebugLog::Log("Dolby Vision RGB tunnel: no CRC-valid HDMI DM packet; using SDR RGB8 passthrough when alignment is known");
		m_missingPacketReported = true;
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
		// Keep the renderer blank until the tunnel is confirmed. Do not
		// interpret arbitrary RGB as Dolby Vision opponent color.
		memset(output, 0, static_cast<size_t>(m_outputSize));
		return true;
	}

	// P216-style semiplanar output: 12-bit intensity in the high bits of
	// each 16-bit Y sample, alternating 12-bit chroma in Cb/Cr pairs.
	const size_t pixels = static_cast<size_t>(m_width) * m_height;
	auto* yPlane = reinterpret_cast<uint16_t*>(output);
	auto* uvPlane = yPlane + pixels;
	const bool leftAligned = m_alignment == ByteAlignment::LEFT;
	for (uint32_t row = 0; row < m_height; ++row)
	{
		const uint8_t* inputRow = source + static_cast<size_t>(row) * m_stride;
		uint16_t* y = yPlane + static_cast<size_t>(row) * m_width;
		uint16_t* uv = uvPlane + static_cast<size_t>(row) * m_width;
		for (uint32_t x = 0; x < m_width; ++x)
		{
			const Rgb8 rgb = DecodeRgb(ReadBigEndian32(inputRow + x * 4),
				leftAligned);
			y[x] = static_cast<uint16_t>(
				((static_cast<unsigned>(rgb.g) << 4) | (rgb.b & 15U)) << 4);
			uv[x] = static_cast<uint16_t>(
				((static_cast<unsigned>(rgb.r) << 4) | (rgb.b >> 4)) << 4);
		}
	}
	return true;
}
