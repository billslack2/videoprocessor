#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// One request per connection. The client never opens a target-side path.
class ConfigurationRpcClient
{
public:
	struct Capabilities
	{
		uint16_t compatibilityVersion = 0;
		std::string vpVersion;
		std::vector<std::string> captureDevices;
		std::map<std::string, std::vector<std::string>> captureConnections;
		std::vector<std::string> monitors;
		std::vector<std::string> filteredRenderers;
		std::vector<std::string> allRenderers;
		std::vector<std::string> luts;
	};
	struct ApplyResult
	{
		std::string action;
		uint8_t status = 0;
	};

	ConfigurationRpcClient(std::string host, uint16_t port = 41686);
	bool GetConfig(std::string& targetPath, std::string& bytes,
		std::string& error) const;
	bool GetCapabilities(Capabilities& capabilities,
		std::string& error) const;
	bool ApplyConfig(const std::string& baseline,
		const std::string& candidate, ApplyResult& result,
		std::string& error) const;

private:
	bool Request(uint16_t operation, const std::vector<uint8_t>& payload,
		std::vector<uint8_t>& response, std::string& error) const;
	std::string host_;
	uint16_t port_;
};
