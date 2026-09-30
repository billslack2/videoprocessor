#include "pch.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include "../VideoProcessor-GUI/ConfigurationRpcServer.h"
#include "CppUnitTest.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <thread>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace ConfigurationRpcProtocol;

namespace
{
	constexpr UINT RequestMessage = WM_APP + 168;

	struct SocketGuard
	{
		SOCKET value = INVALID_SOCKET;
		~SocketGuard() { if (value != INVALID_SOCKET) closesocket(value); }
	};

	bool ReceiveExact(SOCKET socket, uint8_t* bytes, size_t size)
	{
		while (size != 0)
		{
			const int received = recv(socket, reinterpret_cast<char*>(bytes),
				static_cast<int>(size), 0);
			if (received <= 0) return false;
			bytes += received;
			size -= received;
		}
		return true;
	}

	struct ServerHarness
	{
		ConfigurationRpcServer server;
		std::atomic<int> handled{ 0 };
		std::mutex mutex;
		std::condition_variable ready;
		HWND window = nullptr;
		bool initialized = false;
		std::thread ui;

		static LRESULT CALLBACK WindowProc(HWND window, UINT message,
			WPARAM wparam, LPARAM lparam)
		{
			if (message == WM_NCCREATE)
			{
				const auto create = reinterpret_cast<CREATESTRUCTW*>(lparam);
				SetWindowLongPtrW(window, GWLP_USERDATA,
					reinterpret_cast<LONG_PTR>(create->lpCreateParams));
			}
			const auto self = reinterpret_cast<ServerHarness*>(
				GetWindowLongPtrW(window, GWLP_USERDATA));
			if (message == RequestMessage && self)
			{
				const auto pending = self->server.TakePending(
					static_cast<uint64_t>(wparam));
				if (pending)
				{
					++self->handled;
					pending->response.operation = pending->request.operation |
						ResponseFlag;
					WriteString(pending->response.payload, "target-response");
					self->server.Complete(pending);
				}
				return 0;
			}
			if (message == WM_CLOSE)
			{
				DestroyWindow(window);
				return 0;
			}
			if (message == WM_DESTROY)
			{
				PostQuitMessage(0);
				return 0;
			}
			return DefWindowProcW(window, message, wparam, lparam);
		}

		ServerHarness()
		{
			ui = std::thread([this]
			{
				WNDCLASSW klass{};
				klass.lpfnWndProc = WindowProc;
				klass.hInstance = GetModuleHandleW(nullptr);
				klass.lpszClassName = L"VPConfigRpcServerTestWindow";
				RegisterClassW(&klass);
				const HWND created = CreateWindowExW(0, klass.lpszClassName,
					L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
					klass.hInstance, this);
				{
					std::lock_guard<std::mutex> lock(mutex);
					window = created;
					initialized = true;
				}
				ready.notify_one();
				if (created)
				{
					MSG message{};
					while (GetMessageW(&message, nullptr, 0, 0) > 0)
						DispatchMessageW(&message);
				}
			});
			std::unique_lock<std::mutex> lock(mutex);
			ready.wait(lock, [this] { return initialized; });
		}

		~ServerHarness()
		{
			server.Stop();
			if (window) PostMessageW(window, WM_CLOSE, 0, 0);
			if (ui.joinable()) ui.join();
		}

		bool Start()
		{
			if (!window) return false;
			std::string error;
			const bool started = server.Start(window, RequestMessage,
				{ "installation-42", "TEST-PC", "v1.3.005-beta" },
				error, 0, 0);
			if (!started) Logger::WriteMessage(error.c_str());
			return started;
		}
	};

	sockaddr_in Loopback(uint16_t port)
	{
		sockaddr_in result{};
		result.sin_family = AF_INET;
		result.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		result.sin_port = htons(port);
		return result;
	}

	SOCKET Connect(uint16_t port)
	{
		const SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (socketHandle == INVALID_SOCKET) return INVALID_SOCKET;
		const int timeout = 2000;
		setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
			reinterpret_cast<const char*>(&timeout), sizeof(timeout));
		const auto address = Loopback(port);
		if (connect(socketHandle, reinterpret_cast<const sockaddr*>(&address),
			sizeof(address)) == SOCKET_ERROR)
		{
			closesocket(socketHandle);
			return INVALID_SOCKET;
		}
		return socketHandle;
	}
}

namespace VideoProcessorTest
{
	TEST_CLASS(ConfigurationRpcServerTests)
	{
	public:
		TEST_METHOD(LoopbackRequestRunsOnOwnerThreadAndReturnsFramedResponse)
		{
			ServerHarness harness;
			Assert::IsTrue(harness.Start());
			Assert::IsTrue(harness.server.BoundTcpPort() != 0);
			SocketGuard client{ Connect(harness.server.BoundTcpPort()) };
			Assert::IsTrue(client.value != INVALID_SOCKET);
			Frame request;
			request.operation = static_cast<uint16_t>(Operation::GetConfig);
			std::vector<uint8_t> wire;
			Assert::IsTrue(Encode(request, wire));
			Assert::AreEqual(static_cast<int>(wire.size()), send(client.value,
				reinterpret_cast<const char*>(wire.data()),
				static_cast<int>(wire.size()), 0));
			std::array<uint8_t, HeaderBytes> header{};
			Assert::IsTrue(ReceiveExact(client.value, header.data(), header.size()));
			uint16_t operation = 0;
			uint32_t payloadSize = 0;
			Assert::IsTrue(DecodeHeader(header.data(), header.size(),
				operation, payloadSize));
			Assert::IsTrue(payloadSize < 100);
			wire.assign(header.begin(), header.end());
			wire.resize(HeaderBytes + payloadSize);
			Assert::IsTrue(ReceiveExact(client.value, wire.data() + HeaderBytes,
				payloadSize));
			Frame response;
			Assert::IsTrue(Decode(wire.data(), wire.size(), response));
			Assert::AreEqual(static_cast<int>(
				static_cast<uint16_t>(Operation::GetConfig) |
				ResponseFlag), static_cast<int>(response.operation));
			size_t cursor = 0;
			std::string reply;
			Assert::IsTrue(ReadString(response.payload, cursor, reply));
			Assert::AreEqual(std::string("target-response"), reply);
			Assert::AreEqual(1, harness.handled.load());
		}

		TEST_METHOD(InvalidAndOversizedHeadersNeverReachOwner)
		{
			ServerHarness harness;
			Assert::IsTrue(harness.Start());
			for (const uint32_t size : { 0u, MaximumPayloadBytes + 1 })
			{
				SocketGuard client{ Connect(harness.server.BoundTcpPort()) };
				Assert::IsTrue(client.value != INVALID_SOCKET);
				Frame request;
				request.operation = static_cast<uint16_t>(Operation::GetConfig);
				std::vector<uint8_t> wire;
				Assert::IsTrue(Encode(request, wire));
				if (size == 0) wire[4] = 0xff;
				else
				{
					wire[8] = static_cast<uint8_t>(size >> 24);
					wire[9] = static_cast<uint8_t>(size >> 16);
					wire[10] = static_cast<uint8_t>(size >> 8);
					wire[11] = static_cast<uint8_t>(size);
				}
				Assert::AreEqual(static_cast<int>(wire.size()), send(client.value,
					reinterpret_cast<const char*>(wire.data()),
					static_cast<int>(wire.size()), 0));
				shutdown(client.value, SD_SEND);
				char one = 0;
				Assert::AreEqual(0, recv(client.value, &one, 1, 0));
			}
			Assert::AreEqual(0, harness.handled.load());
		}

		TEST_METHOD(DiscoveryAdvertisesActualListenerPort)
		{
			ServerHarness harness;
			Assert::IsTrue(harness.Start());
			SocketGuard client{ socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) };
			Assert::IsTrue(client.value != INVALID_SOCKET);
			const int timeout = 2000;
			setsockopt(client.value, SOL_SOCKET, SO_RCVTIMEO,
				reinterpret_cast<const char*>(&timeout), sizeof(timeout));
			Frame query;
			query.operation = static_cast<uint16_t>(Operation::DiscoveryQuery);
			std::vector<uint8_t> wire;
			Assert::IsTrue(Encode(query, wire));
			const auto address = Loopback(harness.server.BoundDiscoveryPort());
			Assert::AreEqual(static_cast<int>(wire.size()), sendto(client.value,
				reinterpret_cast<const char*>(wire.data()),
				static_cast<int>(wire.size()), 0,
				reinterpret_cast<const sockaddr*>(&address), sizeof(address)));
			std::array<uint8_t, 512> replyBytes{};
			const int count = recv(client.value,
				reinterpret_cast<char*>(replyBytes.data()),
				static_cast<int>(replyBytes.size()), 0);
			Assert::IsTrue(count > 0);
			Frame reply;
			Assert::IsTrue(Decode(replyBytes.data(), count, reply));
			DiscoveryAdvertisement parsed;
			Assert::IsTrue(ParseDiscoveryReply(reply, parsed));
			Assert::AreEqual(std::string("installation-42"), parsed.instanceId);
			Assert::AreEqual(std::string("TEST-PC"), parsed.computerName);
			Assert::AreEqual(static_cast<int>(harness.server.BoundTcpPort()),
				static_cast<int>(parsed.rpcPort));
			Assert::AreEqual(0, harness.handled.load());
		}
	};
}
