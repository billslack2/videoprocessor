#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Shared, Qt-independent wire contract for the configuration-only LAN API.
// Integers are big-endian. A TCP connection carries one request and one
// response; neither side may allocate from an unchecked network length.
namespace ConfigurationRpcProtocol
{
	constexpr uint16_t Version = 1;
	constexpr uint32_t MaximumPayloadBytes = 4 * 1024 * 1024;
	constexpr size_t HeaderBytes = 12;
	constexpr uint16_t ResponseFlag = 0x8000;
	constexpr uint16_t ErrorFlag = 0x4000;

	enum class Operation : uint16_t
	{
		GetConfig = 1,
		GetCapabilities = 2,
		ApplyConfig = 3,
		DiscoveryQuery = 4,
		DiscoveryReply = 5
	};

	struct Frame
	{
		uint16_t operation = 0;
		std::vector<uint8_t> payload;
	};

	inline void Write16(std::vector<uint8_t>& output, uint16_t value)
	{
		output.push_back(static_cast<uint8_t>(value >> 8));
		output.push_back(static_cast<uint8_t>(value));
	}

	inline void Write32(std::vector<uint8_t>& output, uint32_t value)
	{
		output.push_back(static_cast<uint8_t>(value >> 24));
		output.push_back(static_cast<uint8_t>(value >> 16));
		output.push_back(static_cast<uint8_t>(value >> 8));
		output.push_back(static_cast<uint8_t>(value));
	}

	inline uint16_t Read16(const uint8_t* bytes)
	{
		return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) |
			bytes[1]);
	}

	inline uint32_t Read32(const uint8_t* bytes)
	{
		return (static_cast<uint32_t>(bytes[0]) << 24) |
			(static_cast<uint32_t>(bytes[1]) << 16) |
			(static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
	}

	inline bool IsKnownOperation(uint16_t operation)
	{
		switch (operation & ~(ResponseFlag | ErrorFlag))
		{
		case static_cast<uint16_t>(Operation::GetConfig):
		case static_cast<uint16_t>(Operation::GetCapabilities):
		case static_cast<uint16_t>(Operation::ApplyConfig):
		case static_cast<uint16_t>(Operation::DiscoveryQuery):
		case static_cast<uint16_t>(Operation::DiscoveryReply):
			return true;
		default:
			return false;
		}
	}

	inline bool Encode(const Frame& frame, std::vector<uint8_t>& output)
	{
		if (!IsKnownOperation(frame.operation) ||
			frame.payload.size() > MaximumPayloadBytes)
			return false;
		output.clear();
		output.reserve(HeaderBytes + frame.payload.size());
		output.insert(output.end(), { 'V', 'P', 'C', 'R' });
		Write16(output, Version);
		Write16(output, frame.operation);
		Write32(output, static_cast<uint32_t>(frame.payload.size()));
		output.insert(output.end(), frame.payload.begin(), frame.payload.end());
		return true;
	}

	inline bool DecodeHeader(const uint8_t* bytes, size_t size,
		uint16_t& operation, uint32_t& payloadBytes)
	{
		if (!bytes || size < HeaderBytes || bytes[0] != 'V' ||
			bytes[1] != 'P' || bytes[2] != 'C' || bytes[3] != 'R' ||
			Read16(bytes + 4) != Version)
			return false;
		operation = Read16(bytes + 6);
		payloadBytes = Read32(bytes + 8);
		return IsKnownOperation(operation) &&
			payloadBytes <= MaximumPayloadBytes;
	}

	inline bool Decode(const uint8_t* bytes, size_t size, Frame& frame)
	{
		uint16_t operation = 0;
		uint32_t payloadBytes = 0;
		if (!DecodeHeader(bytes, size, operation, payloadBytes) ||
			size != HeaderBytes + static_cast<size_t>(payloadBytes))
			return false;
		frame.operation = operation;
		frame.payload.assign(bytes + HeaderBytes, bytes + size);
		return true;
	}

	inline bool WriteString(std::vector<uint8_t>& output,
		const std::string& value)
	{
		if (value.size() > MaximumPayloadBytes - 4 ||
			output.size() > MaximumPayloadBytes - 4 - value.size())
			return false;
		Write32(output, static_cast<uint32_t>(value.size()));
		output.insert(output.end(), value.begin(), value.end());
		return true;
	}

	inline bool ReadString(const std::vector<uint8_t>& input,
		size_t& cursor, std::string& value)
	{
		if (cursor > input.size() || input.size() - cursor < 4)
			return false;
		const uint32_t length = Read32(input.data() + cursor);
		cursor += 4;
		if (length > input.size() - cursor) return false;
		value.assign(reinterpret_cast<const char*>(input.data() + cursor),
			length);
		cursor += length;
		return true;
	}

	struct DiscoveryAdvertisement
	{
		std::string instanceId;
		std::string computerName;
		std::string vpVersion;
		uint16_t rpcPort = 0;
	};

	inline bool BuildDiscoveryReply(const DiscoveryAdvertisement& info,
		Frame& reply)
	{
		if (info.instanceId.empty() || info.instanceId.size() > 256 ||
			info.computerName.empty() || info.computerName.size() > 256 ||
			info.vpVersion.empty() || info.vpVersion.size() > 256 ||
			info.rpcPort == 0) return false;
		reply = {};
		reply.operation = static_cast<uint16_t>(Operation::DiscoveryReply);
		if (!WriteString(reply.payload, info.instanceId) ||
			!WriteString(reply.payload, info.computerName) ||
			!WriteString(reply.payload, info.vpVersion)) return false;
		Write16(reply.payload, info.rpcPort);
		return true;
	}

	inline bool ParseDiscoveryReply(const Frame& reply,
		DiscoveryAdvertisement& info)
	{
		if (reply.operation != static_cast<uint16_t>(Operation::DiscoveryReply))
			return false;
		DiscoveryAdvertisement parsed;
		size_t cursor = 0;
		if (!ReadString(reply.payload, cursor, parsed.instanceId) ||
			!ReadString(reply.payload, cursor, parsed.computerName) ||
			!ReadString(reply.payload, cursor, parsed.vpVersion) ||
			reply.payload.size() - cursor != 2) return false;
		parsed.rpcPort = Read16(reply.payload.data() + cursor);
		if (parsed.instanceId.empty() || parsed.instanceId.size() > 256 ||
			parsed.computerName.empty() || parsed.computerName.size() > 256 ||
			parsed.vpVersion.empty() || parsed.vpVersion.size() > 256 ||
			parsed.rpcPort == 0) return false;
		info = std::move(parsed);
		return true;
	}
}
