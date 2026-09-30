#pragma once

#include <ConfigurationRpcProtocol.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The socket worker never invokes VP runtime code. It posts a numbered
// request to the dialog and waits for the dialog to complete it on the UI
// thread, where configuration and renderer state are owned.
class ConfigurationRpcServer
{
public:
	static constexpr uint16_t Port = 41686;
	static constexpr uint16_t DiscoveryPort = 41687;
	struct DiscoveryInfo
	{
		std::string instanceId;
		std::string computerName;
		std::string vpVersion;
	};

	struct Pending
	{
		ConfigurationRpcProtocol::Frame request;
		ConfigurationRpcProtocol::Frame response;
		void* completion = nullptr;
		bool started = false;
		Pending();
		~Pending();
		Pending(const Pending&) = delete;
		Pending& operator=(const Pending&) = delete;
	};

	ConfigurationRpcServer() = default;
	~ConfigurationRpcServer();
	ConfigurationRpcServer(const ConfigurationRpcServer&) = delete;
	ConfigurationRpcServer& operator=(const ConfigurationRpcServer&) = delete;

	bool Start(void* dialogWindow, unsigned int requestMessage,
		const DiscoveryInfo& info,
		std::string& error);
	void Stop();
	std::shared_ptr<Pending> TakePending(uint64_t id);
	void Complete(const std::shared_ptr<Pending>& pending);

private:
	void Run();
	void RunDiscovery();
	void HandleClient(uintptr_t clientSocket);
	void HandleDiscovery(uintptr_t socket);
	ConfigurationRpcProtocol::Frame Dispatch(
		const ConfigurationRpcProtocol::Frame& request);
	std::atomic<bool> stopping_{ false };
	std::atomic<uintptr_t> listener_{ static_cast<uintptr_t>(-1) };
	std::atomic<uintptr_t> discovery_{ static_cast<uintptr_t>(-1) };
	std::vector<uint8_t> advertisement_;
	std::thread worker_;
	std::thread discoveryWorker_;
	void* dialogWindow_ = nullptr;
	unsigned int requestMessage_ = 0;
	std::mutex mutex_;
	uint64_t nextId_ = 1;
	std::map<uint64_t, std::shared_ptr<Pending>> pending_;
	bool socketsStarted_ = false;
};
