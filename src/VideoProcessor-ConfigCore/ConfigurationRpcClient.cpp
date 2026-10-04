#include <winsock2.h>
#include <ws2tcpip.h>

#include "ConfigurationRpcClient.h"
#include "ConfigurationRpcProtocol.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace
{
using namespace ConfigurationRpcProtocol;

struct WinsockSession
{
	bool ready = false;
	WinsockSession()
	{
		WSADATA data{};
		ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
	}
	~WinsockSession() { if (ready) WSACleanup(); }
};

struct SocketOwner
{
	SOCKET value = INVALID_SOCKET;
	~SocketOwner() { if (value != INVALID_SOCKET) closesocket(value); }
};

bool SendExact(SOCKET socket, const uint8_t* bytes, size_t size)
{
	while (size != 0)
	{
		const int count = send(socket, reinterpret_cast<const char*>(bytes),
			static_cast<int>(std::min<size_t>(size, INT_MAX)), 0);
		if (count <= 0) return false;
		bytes += count;
		size -= count;
	}
	return true;
}

bool ReceiveExact(SOCKET socket, uint8_t* bytes, size_t size)
{
	while (size != 0)
	{
		const int count = recv(socket, reinterpret_cast<char*>(bytes),
			static_cast<int>(std::min<size_t>(size, INT_MAX)), 0);
		if (count <= 0) return false;
		bytes += count;
		size -= count;
	}
	return true;
}

bool ReadList(const std::vector<uint8_t>& bytes, size_t& cursor,
	std::vector<std::string>& values)
{
	if (cursor > bytes.size() || bytes.size() - cursor < 4) return false;
	const uint32_t count = Read32(bytes.data() + cursor);
	cursor += 4;
	if (count > (bytes.size() - cursor) / 4) return false;
	for (uint32_t index = 0; index < count; ++index)
	{
		std::string value;
		if (!ReadString(bytes, cursor, value)) return false;
		values.push_back(std::move(value));
	}
	return true;
}
}

ConfigurationRpcClient::ConfigurationRpcClient(std::string host,
	uint16_t port) : host_(std::move(host)), port_(port)
{
}

bool ConfigurationRpcClient::Request(uint16_t operation,
	const std::vector<uint8_t>& payload,
	std::vector<uint8_t>& response, std::string& error) const
{
	response.clear();
	if (host_.empty() || port_ == 0)
	{
		error = "A VP target address is required.";
		return false;
	}
	Frame request;
	request.operation = operation;
	Write16(request.payload, ConfigurationCompatibilityVersion);
	request.payload.insert(request.payload.end(), payload.begin(), payload.end());
	std::vector<uint8_t> wire;
	if (!Encode(request, wire))
	{
		error = "Configuration request exceeds the RPC limit.";
		return false;
	}
	WinsockSession winsock;
	if (!winsock.ready)
	{
		error = "Windows networking could not start.";
		return false;
	}
	addrinfo hints{};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	addrinfo* addresses = nullptr;
	const std::string service = std::to_string(port_);
	if (getaddrinfo(host_.c_str(), service.c_str(), &hints, &addresses) != 0)
	{
		error = "Could not resolve the VP target address.";
		return false;
	}
	SocketOwner socket;
	for (addrinfo* address = addresses; address; address = address->ai_next)
	{
		SocketOwner attempt;
		attempt.value = ::socket(address->ai_family,
			address->ai_socktype, address->ai_protocol);
		if (attempt.value == INVALID_SOCKET) continue;
		u_long nonblocking = 1;
		ioctlsocket(attempt.value, FIONBIO, &nonblocking);
		const int connected = connect(attempt.value, address->ai_addr,
			static_cast<int>(address->ai_addrlen));
		if (connected == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
			continue;
		fd_set writable;
		FD_ZERO(&writable);
		FD_SET(attempt.value, &writable);
		timeval timeout{ 3, 0 };
		if (select(0, nullptr, &writable, nullptr, &timeout) <= 0)
			continue;
		int socketError = 0;
		int optionLength = sizeof(socketError);
		if (getsockopt(attempt.value, SOL_SOCKET, SO_ERROR,
			reinterpret_cast<char*>(&socketError), &optionLength) == SOCKET_ERROR ||
			socketError != 0) continue;
		nonblocking = 0;
		ioctlsocket(attempt.value, FIONBIO, &nonblocking);
		socket.value = attempt.value;
		attempt.value = INVALID_SOCKET;
		break;
	}
	freeaddrinfo(addresses);
	if (socket.value == INVALID_SOCKET)
	{
		error = "Could not connect to VP. Check its address, network, and firewall.";
		return false;
	}
	const int timeout = operation == static_cast<uint16_t>(Operation::ApplyConfig) ||
		operation == static_cast<uint16_t>(Operation::SelectProfile) ||
		operation == static_cast<uint16_t>(Operation::RunAction) ?
		50000 : operation == static_cast<uint16_t>(Operation::GetActiveProfileStatus) ?
		1500 : 10000;
	setsockopt(socket.value, SOL_SOCKET, SO_RCVTIMEO,
		reinterpret_cast<const char*>(&timeout), sizeof(timeout));
	setsockopt(socket.value, SOL_SOCKET, SO_SNDTIMEO,
		reinterpret_cast<const char*>(&timeout), sizeof(timeout));
	if (!SendExact(socket.value, wire.data(), wire.size()))
	{
		error = "Could not send the configuration request.";
		return false;
	}
	std::array<uint8_t, HeaderBytes> header{};
	if (!ReceiveExact(socket.value, header.data(), header.size()))
	{
		error = "VP did not complete the configuration version handshake. Update VP and Config UI together, or retry when VP is running.";
		return false;
	}
	if (header[0] == 'V' && header[1] == 'P' &&
		header[2] == 'C' && header[3] == 'R' &&
		Read16(header.data() + 4) != Version)
	{
		error = "This VP uses an incompatible configuration RPC version. Update VP and Config UI together.";
		return false;
	}
	uint16_t responseOperation = 0;
	uint32_t payloadBytes = 0;
	if (!DecodeHeader(header.data(), header.size(),
		responseOperation, payloadBytes) ||
		(responseOperation & ~ErrorFlag) != (operation | ResponseFlag))
	{
		error = "VP returned an invalid configuration response.";
		return false;
	}
	wire.assign(header.begin(), header.end());
	wire.resize(HeaderBytes + payloadBytes);
	if (payloadBytes != 0 && !ReceiveExact(socket.value,
		wire.data() + HeaderBytes, payloadBytes))
	{
		error = "VP returned an incomplete configuration response.";
		return false;
	}
	Frame decoded;
	if (!Decode(wire.data(), wire.size(), decoded))
	{
		error = "VP returned a malformed configuration response.";
		return false;
	}
	if ((decoded.operation & ErrorFlag) != 0)
	{
		size_t cursor = 0;
		if (!ReadString(decoded.payload, cursor, error) ||
			cursor != decoded.payload.size())
			error = "VP rejected the configuration request without a valid reason.";
		return false;
	}
	response = std::move(decoded.payload);
	return true;
}

bool ConfigurationRpcClient::GetConfig(std::string& targetPath,
	std::string& bytes, std::string& error) const
{
	Capabilities compatibility;
	if (!GetCapabilities(compatibility, error)) return false;
	std::vector<uint8_t> response;
	if (!Request(static_cast<uint16_t>(Operation::GetConfig), {},
		response, error)) return false;
	size_t cursor = 0;
	if (!ReadString(response, cursor, targetPath) || targetPath.empty() ||
		!ReadString(response, cursor, bytes) || cursor != response.size())
	{
		error = "VP returned an invalid configuration document.";
		return false;
	}
	return true;
}

bool ConfigurationRpcClient::GetCapabilities(Capabilities& capabilities,
	std::string& error) const
{
	std::vector<uint8_t> response;
	if (!Request(static_cast<uint16_t>(Operation::GetCapabilities), {},
		response, error)) return false;
	Capabilities parsed;
	size_t cursor = 0;
	if (response.size() < 2) goto invalid;
	parsed.compatibilityVersion = Read16(response.data());
	cursor = 2;
	if (!ReadString(response, cursor, parsed.vpVersion) ||
		parsed.vpVersion.empty()) goto invalid;
	if (parsed.compatibilityVersion != ConfigurationCompatibilityVersion)
	{
		error = "This VP uses an incompatible configuration model. Update VP and Config UI together.";
		return false;
	}
	if (!ReadList(response, cursor, parsed.captureDevices) ||
		cursor > response.size() || response.size() - cursor < 4)
		goto invalid;
	{
		const uint32_t count = Read32(response.data() + cursor);
		cursor += 4;
		if (count > (response.size() - cursor) / 8) goto invalid;
		for (uint32_t index = 0; index < count; ++index)
		{
			std::string device;
			std::vector<std::string> connections;
			if (!ReadString(response, cursor, device) ||
				!ReadList(response, cursor, connections)) goto invalid;
			parsed.captureConnections.emplace(std::move(device),
				std::move(connections));
		}
	}
	if (!ReadList(response, cursor, parsed.monitors) ||
		!ReadList(response, cursor, parsed.filteredRenderers) ||
		!ReadList(response, cursor, parsed.allRenderers) ||
		!ReadList(response, cursor, parsed.luts) ||
		cursor != response.size()) goto invalid;
	capabilities = std::move(parsed);
	return true;
invalid:
	error = "VP returned invalid capability data.";
	return false;
}

bool ConfigurationRpcClient::GetActiveProfileStatus(LiveProfileStatus& status,
	std::string& error) const
{
	std::vector<uint8_t> response;
	if (!Request(static_cast<uint16_t>(Operation::GetActiveProfileStatus), {},
		response, error)) return false;
	if (!ParseLiveProfileStatus(response, status))
	{
		error = "VP returned invalid active-profile status.";
		return false;
	}
	return true;
}

bool ConfigurationRpcClient::SelectProfile(const std::string& group,
	const std::string& profile, bool enabled, std::string& error) const
{
	Frame request;
	if (group.empty() || profile.empty() || group.size() > 64 ||
		profile.size() > 64 || !WriteString(request.payload, group) ||
		!WriteString(request.payload, profile))
	{
		error = "Select a saved profile.";
		return false;
	}
	request.payload.push_back(enabled ? 1 : 0);
	std::vector<uint8_t> response;
	if (!Request(static_cast<uint16_t>(Operation::SelectProfile),
		request.payload, response, error)) return false;
	if (!response.empty())
	{
		error = "VP returned an invalid profile selection response.";
		return false;
	}
	return true;
}

bool ConfigurationRpcClient::RunAction(const std::string& action,
	std::string& error) const
{
	Frame request;
	if (action.empty() || action.size() > 64 ||
		!WriteString(request.payload, action))
	{
		error = "Select a saved action.";
		return false;
	}
	std::vector<uint8_t> response;
	if (!Request(static_cast<uint16_t>(Operation::RunAction),
		request.payload, response, error)) return false;
	if (!response.empty())
	{
		error = "VP returned an invalid action response.";
		return false;
	}
	return true;
}

bool ConfigurationRpcClient::ApplyConfig(const std::string& baseline,
	const std::string& candidate, ApplyResult& result,
	std::string& error) const
{
	Capabilities compatibility;
	if (!GetCapabilities(compatibility, error)) return false;
	Frame request;
	if (!WriteString(request.payload, baseline) ||
		!WriteString(request.payload, candidate))
	{
		error = "The configuration document exceeds the RPC limit.";
		return false;
	}
	std::vector<uint8_t> response;
	if (!Request(static_cast<uint16_t>(Operation::ApplyConfig),
		request.payload, response, error)) return false;
	size_t cursor = 0;
	ApplyResult parsed;
	if (!ReadString(response, cursor, parsed.action) ||
		response.size() - cursor != 1 || response[cursor] > 2)
	{
		error = "VP returned an invalid Apply result.";
		return false;
	}
	parsed.status = response[cursor];
	result = std::move(parsed);
	return true;
}
