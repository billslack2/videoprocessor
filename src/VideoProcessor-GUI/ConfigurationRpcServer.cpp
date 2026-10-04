#include "pch.h"
#include <winsock2.h>
#include <ws2tcpip.h>

#include "ConfigurationRpcServer.h"
#include "ActiveProfileStatus.h"

#include <algorithm>
#include <array>
#include <limits>
#include <system_error>
#include <vector>

namespace
{
using namespace ConfigurationRpcProtocol;
constexpr DWORD UiWaitMilliseconds = 15000;
constexpr DWORD StartedUiWaitMilliseconds = 30000;
constexpr int SocketWaitMilliseconds = 5000;

Frame ErrorResponse(uint16_t requestOperation, const std::string& message)
{
	Frame result;
	result.operation = static_cast<uint16_t>(requestOperation |
		ResponseFlag | ErrorFlag);
	WriteString(result.payload, message);
	return result;
}

Frame ActiveProfileResponse(const Frame& request)
{
	if (request.payload.size() != 2 ||
		Read16(request.payload.data()) != ConfigurationCompatibilityVersion)
		return ErrorResponse(request.operation,
			"This VP uses an incompatible configuration model. Update VP and Config UI together.");
	LiveProfileStatus status;
	ActiveProfileStatus::Snapshot snapshot;
	status.available = ActiveProfileStatus::Read(GetCurrentProcessId(), snapshot);
	if (status.available)
	{
		const auto bounded = [](const auto& value)
		{
			return std::string(value, strnlen_s(value, sizeof(value)));
		};
		status.queue = bounded(snapshot.queue);
		status.renderer = bounded(snapshot.renderer);
		status.color = bounded(snapshot.color);
		status.scaling = bounded(snapshot.scaling);
		status.output = bounded(snapshot.output);
		status.viewport = bounded(snapshot.viewport);
		status.zoom = bounded(snapshot.zoom);
		status.shaderAvailable = ActiveProfileStatus::ShaderSetIsCurrent(snapshot);
		if (status.shaderAvailable)
			for (uint32_t index = 0; index < snapshot.shaderCount; ++index)
				status.shaders.push_back(bounded(snapshot.shaders[index]));
	}
	Frame response;
	response.operation = static_cast<uint16_t>(request.operation | ResponseFlag);
	if (!BuildLiveProfileStatus(status, response.payload))
		return ErrorResponse(request.operation, "Active-profile status is invalid.");
	return response;
}

bool PrivatePeer(const sockaddr_in& peer)
{
	const uint32_t address = ntohl(peer.sin_addr.s_addr);
	const uint8_t first = static_cast<uint8_t>(address >> 24);
	const uint8_t second = static_cast<uint8_t>(address >> 16);
	return first == 127 || first == 10 ||
		(first == 172 && second >= 16 && second <= 31) ||
		(first == 192 && second == 168) ||
		(first == 169 && second == 254);
}

bool ReceiveExact(SOCKET socket, uint8_t* data, size_t length)
{
	const ULONGLONG started = GetTickCount64();
	while (length != 0)
	{
		if (GetTickCount64() - started >= SocketWaitMilliseconds)
			return false;
		const int chunk = static_cast<int>(std::min<size_t>(length,
			static_cast<size_t>(std::numeric_limits<int>::max())));
		const int received = recv(socket, reinterpret_cast<char*>(data), chunk, 0);
		if (received <= 0) return false;
		data += received;
		length -= static_cast<size_t>(received);
	}
	return true;
}

bool SendExact(SOCKET socket, const uint8_t* data, size_t length)
{
	while (length != 0)
	{
		const int chunk = static_cast<int>(std::min<size_t>(length,
			static_cast<size_t>(std::numeric_limits<int>::max())));
		const int sent = send(socket, reinterpret_cast<const char*>(data), chunk, 0);
		if (sent <= 0) return false;
		data += sent;
		length -= static_cast<size_t>(sent);
	}
	return true;
}
}

ConfigurationRpcServer::Pending::Pending()
{
	completion = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

ConfigurationRpcServer::Pending::~Pending()
{
	if (completion) CloseHandle(static_cast<HANDLE>(completion));
}

ConfigurationRpcServer::~ConfigurationRpcServer()
{
	Stop();
}

bool ConfigurationRpcServer::Start(void* dialogWindow,
	unsigned int requestMessage, const DiscoveryInfo& info,
	std::string& error, uint16_t tcpPort, uint16_t discoveryPort)
{
	if (worker_.joinable() || discoveryWorker_.joinable() ||
		socketsStarted_ || !dialogWindow ||
		!IsWindow(static_cast<HWND>(dialogWindow)) || !requestMessage ||
		info.instanceId.empty() || info.computerName.empty() ||
		info.vpVersion.empty())
	{
		error = "Invalid configuration RPC server state.";
		return false;
	}
	WSADATA data{};
	if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
	{
		error = "Winsock initialization failed.";
		return false;
	}
	socketsStarted_ = true;
	const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listener == INVALID_SOCKET)
	{
		error = "Could not create the configuration RPC socket.";
		Stop();
		return false;
	}
	BOOL exclusive = TRUE;
	if (setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
		reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR)
	{
		error = "Could not reserve the configuration RPC port.";
		closesocket(listener);
		Stop();
		return false;
	}
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(tcpPort);
	if (bind(listener, reinterpret_cast<const sockaddr*>(&address),
		sizeof(address)) == SOCKET_ERROR || listen(listener, 8) == SOCKET_ERROR)
	{
		error = "Could not bind configuration RPC port 41686.";
		closesocket(listener);
		Stop();
		return false;
	}
	sockaddr_in boundTcp{};
	int boundTcpSize = sizeof(boundTcp);
	if (getsockname(listener, reinterpret_cast<sockaddr*>(&boundTcp),
		&boundTcpSize) == SOCKET_ERROR)
	{
		error = "Could not inspect the configuration RPC port.";
		closesocket(listener);
		Stop();
		return false;
	}
	const SOCKET discovery = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (discovery == INVALID_SOCKET ||
		setsockopt(discovery, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
			reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR)
	{
		error = "Could not reserve the configuration discovery port.";
		if (discovery != INVALID_SOCKET) closesocket(discovery);
		closesocket(listener);
		Stop();
		return false;
	}
	address.sin_port = htons(discoveryPort);
	if (bind(discovery, reinterpret_cast<const sockaddr*>(&address),
		sizeof(address)) == SOCKET_ERROR)
	{
		error = "Could not bind configuration discovery port 41687.";
		closesocket(discovery);
		closesocket(listener);
		Stop();
		return false;
	}
	sockaddr_in boundDiscovery{};
	int boundDiscoverySize = sizeof(boundDiscovery);
	if (getsockname(discovery, reinterpret_cast<sockaddr*>(&boundDiscovery),
		&boundDiscoverySize) == SOCKET_ERROR)
	{
		error = "Could not inspect the configuration discovery port.";
		closesocket(discovery);
		closesocket(listener);
		Stop();
		return false;
	}
	Frame advertisement;
	if (!BuildDiscoveryReply({ info.instanceId, info.computerName,
		info.vpVersion, ntohs(boundTcp.sin_port) }, advertisement) ||
		!Encode(advertisement, advertisement_))
	{
		error = "Configuration RPC discovery identity is invalid or too large.";
		closesocket(discovery);
		closesocket(listener);
		Stop();
		return false;
	}
	boundTcpPort_ = ntohs(boundTcp.sin_port);
	boundDiscoveryPort_ = ntohs(boundDiscovery.sin_port);
	dialogWindow_ = dialogWindow;
	requestMessage_ = requestMessage;
	stopping_.store(false);
	listener_.store(static_cast<uintptr_t>(listener));
	discovery_.store(static_cast<uintptr_t>(discovery));
	try
	{
		worker_ = std::thread(&ConfigurationRpcServer::Run, this);
		discoveryWorker_ = std::thread(
			&ConfigurationRpcServer::RunDiscovery, this);
	}
	catch (const std::system_error&)
	{
		error = "Could not start configuration RPC worker threads.";
		Stop();
		return false;
	}
	return true;
}

void ConfigurationRpcServer::Stop()
{
	stopping_.store(true);
	const uintptr_t listener = listener_.exchange(static_cast<uintptr_t>(-1));
	if (listener != static_cast<uintptr_t>(-1))
		closesocket(static_cast<SOCKET>(listener));
	const uintptr_t discovery = discovery_.exchange(static_cast<uintptr_t>(-1));
	if (discovery != static_cast<uintptr_t>(-1))
		closesocket(static_cast<SOCKET>(discovery));
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (auto& entry : pending_)
			if (entry.second->completion)
				SetEvent(static_cast<HANDLE>(entry.second->completion));
	}
	if (worker_.joinable()) worker_.join();
	if (discoveryWorker_.joinable()) discoveryWorker_.join();
	{
		std::lock_guard<std::mutex> lock(mutex_);
		pending_.clear();
	}
	if (socketsStarted_)
	{
		WSACleanup();
		socketsStarted_ = false;
	}
	boundTcpPort_ = 0;
	boundDiscoveryPort_ = 0;
}

std::shared_ptr<ConfigurationRpcServer::Pending>
ConfigurationRpcServer::TakePending(uint64_t id)
{
	std::lock_guard<std::mutex> lock(mutex_);
	const auto found = pending_.find(id);
	if (found == pending_.end() || stopping_.load()) return {};
	found->second->started = true;
	return found->second;
}

void ConfigurationRpcServer::Complete(const std::shared_ptr<Pending>& pending)
{
	if (pending && pending->completion)
		SetEvent(static_cast<HANDLE>(pending->completion));
}

ConfigurationRpcProtocol::Frame ConfigurationRpcServer::Dispatch(
	const Frame& request)
{
	const auto pending = std::make_shared<Pending>();
	if (!pending->completion)
		return ErrorResponse(request.operation,
			"Could not allocate a configuration RPC completion event.");
	pending->request = request;
	uint64_t id = 0;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopping_.load())
			return ErrorResponse(request.operation, "VideoProcessor is stopping.");
		id = nextId_++;
		pending_.emplace(id, pending);
	}
	if (!PostMessageW(static_cast<HWND>(dialogWindow_), requestMessage_,
		static_cast<WPARAM>(id), 0))
	{
		std::lock_guard<std::mutex> lock(mutex_);
		pending_.erase(id);
		return ErrorResponse(request.operation,
			"The VideoProcessor configuration owner is unavailable.");
	}
	DWORD waited = WaitForSingleObject(static_cast<HANDLE>(pending->completion),
		UiWaitMilliseconds);
	if (waited == WAIT_TIMEOUT)
	{
		bool started = false;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			started = pending->started;
			if (!started) pending_.erase(id);
		}
		if (started)
			waited = WaitForSingleObject(static_cast<HANDLE>(pending->completion),
				StartedUiWaitMilliseconds);
	}
	{
		std::lock_guard<std::mutex> lock(mutex_);
		pending_.erase(id);
	}
	if (waited != WAIT_OBJECT_0 || stopping_.load())
		return ErrorResponse(request.operation,
			"Configuration request timed out; its outcome may be unknown. Refresh the target before retrying.");
	return pending->response;
}

void ConfigurationRpcServer::HandleClient(uintptr_t rawSocket)
{
	const SOCKET client = static_cast<SOCKET>(rawSocket);
	const int timeout = SocketWaitMilliseconds;
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
		reinterpret_cast<const char*>(&timeout), sizeof(timeout));
	setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
		reinterpret_cast<const char*>(&timeout), sizeof(timeout));
	std::array<uint8_t, HeaderBytes> header{};
	if (!ReceiveExact(client, header.data(), header.size())) return;
	uint16_t operation = 0;
	uint32_t payloadBytes = 0;
	if (!DecodeHeader(header.data(), header.size(), operation, payloadBytes) ||
		(operation & (ResponseFlag | ErrorFlag)) != 0 ||
		operation < static_cast<uint16_t>(Operation::GetConfig) ||
		(operation > static_cast<uint16_t>(Operation::ApplyConfig) &&
		 operation != static_cast<uint16_t>(Operation::GetActiveProfileStatus) &&
		 operation != static_cast<uint16_t>(Operation::SelectProfile) &&
		 operation != static_cast<uint16_t>(Operation::RunAction)))
		return;
	std::vector<uint8_t> wire(header.begin(), header.end());
	wire.resize(HeaderBytes + payloadBytes);
	if (payloadBytes != 0 && !ReceiveExact(client,
		wire.data() + HeaderBytes, payloadBytes)) return;
	Frame request;
	if (!Decode(wire.data(), wire.size(), request)) return;
	Frame response = operation == static_cast<uint16_t>(Operation::GetActiveProfileStatus) ?
		ActiveProfileResponse(request) : Dispatch(request);
	std::vector<uint8_t> encoded;
	if (Encode(response, encoded)) SendExact(client, encoded.data(), encoded.size());
}

void ConfigurationRpcServer::HandleDiscovery(uintptr_t rawSocket)
{
	std::array<uint8_t, 512> datagram{};
	sockaddr_in peer{};
	int peerSize = sizeof(peer);
	const int size = recvfrom(static_cast<SOCKET>(rawSocket),
		reinterpret_cast<char*>(datagram.data()),
		static_cast<int>(datagram.size()), 0,
		reinterpret_cast<sockaddr*>(&peer), &peerSize);
	if (size < 0 || !PrivatePeer(peer)) return;
	Frame query;
	if (!Decode(datagram.data(), static_cast<size_t>(size), query) ||
		query.operation != static_cast<uint16_t>(Operation::DiscoveryQuery) ||
		!query.payload.empty()) return;
	sendto(static_cast<SOCKET>(rawSocket),
		reinterpret_cast<const char*>(advertisement_.data()),
		static_cast<int>(advertisement_.size()), 0,
		reinterpret_cast<const sockaddr*>(&peer), peerSize);
}

void ConfigurationRpcServer::Run()
{
	while (!stopping_.load())
	{
		const uintptr_t current = listener_.load();
		if (current == static_cast<uintptr_t>(-1)) break;
		fd_set ready;
		FD_ZERO(&ready);
		FD_SET(static_cast<SOCKET>(current), &ready);
		timeval timeout{};
		timeout.tv_usec = 200000;
		const int selected = select(0, &ready, nullptr, nullptr, &timeout);
		if (selected <= 0) continue;
		sockaddr_in peer{};
		int peerSize = sizeof(peer);
		const SOCKET client = accept(static_cast<SOCKET>(current),
			reinterpret_cast<sockaddr*>(&peer), &peerSize);
		if (client == INVALID_SOCKET) continue;
		if (PrivatePeer(peer)) HandleClient(static_cast<uintptr_t>(client));
		closesocket(client);
	}
}

void ConfigurationRpcServer::RunDiscovery()
{
	while (!stopping_.load())
	{
		const uintptr_t current = discovery_.load();
		if (current == static_cast<uintptr_t>(-1)) break;
		fd_set ready;
		FD_ZERO(&ready);
		FD_SET(static_cast<SOCKET>(current), &ready);
		timeval timeout{};
		timeout.tv_usec = 200000;
		if (select(0, &ready, nullptr, nullptr, &timeout) > 0)
			HandleDiscovery(current);
	}
}
