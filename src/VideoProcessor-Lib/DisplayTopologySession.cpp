#include <pch.h>

#include "DisplayTopologySession.h"

#include <DebugLog.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <vector>

namespace
{
constexpr const char* RECOVERY_KEY = "display_recovery.v1";
constexpr uint32_t RECOVERY_MAGIC = 0x31525056; // VPR1

struct RecoveryHeader
{
	uint32_t magic = RECOVERY_MAGIC;
	uint32_t version = 1;
	uint32_t pathSize = sizeof(DISPLAYCONFIG_PATH_INFO);
	uint32_t modeSize = sizeof(DISPLAYCONFIG_MODE_INFO);
	uint32_t pathCount = 0;
	uint32_t modeCount = 0;
	uint32_t checksum = 0;
};

struct Topology
{
	std::vector<DISPLAYCONFIG_PATH_INFO> paths;
	std::vector<DISPLAYCONFIG_MODE_INFO> modes;
};

std::string Trim(const std::string& value)
{
	size_t first = 0;
	while (first < value.size() && std::isspace(
		static_cast<unsigned char>(value[first])))
		++first;
	size_t last = value.size();
	while (last > first && std::isspace(
		static_cast<unsigned char>(value[last - 1])))
		--last;
	return value.substr(first, last - first);
}

bool ReadEntry(const std::string& line, std::string& key, std::string& value)
{
	const size_t colon = line.find(':');
	if (colon == std::string::npos)
		return false;
	key = Trim(line.substr(0, colon));
	value = Trim(line.substr(colon + 1));
	return !key.empty();
}

bool UpdateRecoveryEntry(const std::string& statePath,
	const std::string* value, std::string& error)
{
	error.clear();
	std::vector<std::string> lines;
	std::ifstream input(statePath);
	std::string line;
	while (std::getline(input, line))
	{
		std::string key;
		std::string ignored;
		if (ReadEntry(line, key, ignored) && key == RECOVERY_KEY)
			continue;
		lines.push_back(line);
	}

	if (lines.empty())
		lines.push_back("# Managed by VideoProcessor.");
	if (value)
		lines.push_back(std::string(RECOVERY_KEY) + ": " + *value);

	const std::string temporaryPath = statePath + ".display.tmp";
	std::ofstream output(temporaryPath, std::ios::out | std::ios::trunc);
	if (!output.is_open())
	{
		error = "cannot open display recovery temporary state '" +
			temporaryPath + "'";
		return false;
	}
	for (const std::string& current : lines)
		output << current << "\n";
	output.close();
	if (!output)
	{
		DeleteFileA(temporaryPath.c_str());
		error = "cannot write display recovery state '" + temporaryPath + "'";
		return false;
	}
	if (!MoveFileExA(temporaryPath.c_str(), statePath.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		const DWORD code = GetLastError();
		DeleteFileA(temporaryPath.c_str());
		error = "cannot commit display recovery state (Windows error " +
			std::to_string(code) + ")";
		return false;
	}
	return true;
}

uint32_t Checksum(const uint8_t* data, size_t size)
{
	uint32_t hash = 2166136261u;
	for (size_t i = 0; i < size; ++i)
	{
		hash ^= data[i];
		hash *= 16777619u;
	}
	return hash;
}

std::string HexEncode(const uint8_t* data, size_t size)
{
	static const char digits[] = "0123456789ABCDEF";
	std::string result(size * 2, '0');
	for (size_t i = 0; i < size; ++i)
	{
		result[i * 2] = digits[data[i] >> 4];
		result[i * 2 + 1] = digits[data[i] & 0x0f];
	}
	return result;
}

int HexDigit(char value)
{
	if (value >= '0' && value <= '9') return value - '0';
	if (value >= 'a' && value <= 'f') return value - 'a' + 10;
	if (value >= 'A' && value <= 'F') return value - 'A' + 10;
	return -1;
}

bool HexDecode(const std::string& text, std::vector<uint8_t>& bytes)
{
	if ((text.size() % 2) != 0)
		return false;
	bytes.resize(text.size() / 2);
	for (size_t i = 0; i < bytes.size(); ++i)
	{
		const int high = HexDigit(text[i * 2]);
		const int low = HexDigit(text[i * 2 + 1]);
		if (high < 0 || low < 0)
			return false;
		bytes[i] = static_cast<uint8_t>((high << 4) | low);
	}
	return true;
}

std::string Serialize(const Topology& topology)
{
	RecoveryHeader header;
	header.pathCount = static_cast<uint32_t>(topology.paths.size());
	header.modeCount = static_cast<uint32_t>(topology.modes.size());
	const size_t payloadSize = topology.paths.size() * sizeof(DISPLAYCONFIG_PATH_INFO) +
		topology.modes.size() * sizeof(DISPLAYCONFIG_MODE_INFO);
	std::vector<uint8_t> bytes(sizeof(header) + payloadSize);
	if (!topology.paths.empty())
		memcpy(bytes.data() + sizeof(header), topology.paths.data(),
			topology.paths.size() * sizeof(DISPLAYCONFIG_PATH_INFO));
	if (!topology.modes.empty())
		memcpy(bytes.data() + sizeof(header) +
			topology.paths.size() * sizeof(DISPLAYCONFIG_PATH_INFO),
			topology.modes.data(),
			topology.modes.size() * sizeof(DISPLAYCONFIG_MODE_INFO));
	header.checksum = Checksum(bytes.data() + sizeof(header), payloadSize);
	memcpy(bytes.data(), &header, sizeof(header));
	return HexEncode(bytes.data(), bytes.size());
}

bool Deserialize(const std::string& text, Topology& topology,
	std::string& error)
{
	std::vector<uint8_t> bytes;
	if (!HexDecode(text, bytes) || bytes.size() < sizeof(RecoveryHeader))
	{
		error = "display recovery record is not valid hexadecimal data";
		return false;
	}
	RecoveryHeader header;
	memcpy(&header, bytes.data(), sizeof(header));
	if (header.magic != RECOVERY_MAGIC || header.version != 1 ||
		header.pathSize != sizeof(DISPLAYCONFIG_PATH_INFO) ||
		header.modeSize != sizeof(DISPLAYCONFIG_MODE_INFO))
	{
		error = "display recovery record version or structure size is incompatible";
		return false;
	}
	const size_t pathBytes = static_cast<size_t>(header.pathCount) * header.pathSize;
	const size_t modeBytes = static_cast<size_t>(header.modeCount) * header.modeSize;
	if (header.pathCount == 0 || pathBytes > bytes.size() ||
		modeBytes > bytes.size() ||
		sizeof(header) + pathBytes + modeBytes != bytes.size())
	{
		error = "display recovery record length is invalid";
		return false;
	}
	if (Checksum(bytes.data() + sizeof(header), pathBytes + modeBytes) !=
		header.checksum)
	{
		error = "display recovery record checksum failed";
		return false;
	}
	topology.paths.resize(header.pathCount);
	topology.modes.resize(header.modeCount);
	memcpy(topology.paths.data(), bytes.data() + sizeof(header), pathBytes);
	if (modeBytes)
		memcpy(topology.modes.data(), bytes.data() + sizeof(header) + pathBytes,
			modeBytes);
	return true;
}

bool ReadRecovery(const std::string& statePath, std::string& value)
{
	std::ifstream input(statePath);
	std::string line;
	while (std::getline(input, line))
	{
		std::string key;
		if (ReadEntry(line, key, value) && key == RECOVERY_KEY)
			return !value.empty();
	}
	return false;
}

LONG QueryTopology(UINT32 flags, Topology& topology)
{
	LONG result = ERROR_SUCCESS;
	do
	{
		UINT32 pathCount = 0;
		UINT32 modeCount = 0;
		result = GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount);
		if (result != ERROR_SUCCESS)
			return result;
		topology.paths.resize(pathCount);
		topology.modes.resize(modeCount);
		result = QueryDisplayConfig(flags, &pathCount, topology.paths.data(),
			&modeCount, topology.modes.data(), nullptr);
		if (result == ERROR_SUCCESS)
		{
			topology.paths.resize(pathCount);
			topology.modes.resize(modeCount);
		}
	} while (result == ERROR_INSUFFICIENT_BUFFER);
	return result;
}

bool TargetName(const DISPLAYCONFIG_PATH_INFO& path, std::wstring& name)
{
	DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
	target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
	target.header.size = sizeof(target);
	target.header.adapterId = path.targetInfo.adapterId;
	target.header.id = path.targetInfo.id;
	if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS)
		return false;
	name = target.monitorFriendlyDeviceName;
	return !name.empty();
}

struct TargetIdentity
{
	LUID adapter = {};
	UINT32 id = 0;
	bool operator<(const TargetIdentity& other) const
	{
		if (adapter.HighPart != other.adapter.HighPart)
			return adapter.HighPart < other.adapter.HighPart;
		if (adapter.LowPart != other.adapter.LowPart)
			return adapter.LowPart < other.adapter.LowPart;
		return id < other.id;
	}
	bool operator==(const TargetIdentity& other) const
	{
		return adapter.HighPart == other.adapter.HighPart &&
			adapter.LowPart == other.adapter.LowPart && id == other.id;
	}
};

bool IsOnlyTargetActive(const TargetIdentity& target)
{
	Topology active;
	if (QueryTopology(QDC_ONLY_ACTIVE_PATHS, active) != ERROR_SUCCESS ||
		active.paths.size() != 1)
		return false;
	const auto& current = active.paths.front().targetInfo;
	return current.id == target.id &&
		current.adapterId.HighPart == target.adapter.HighPart &&
		current.adapterId.LowPart == target.adapter.LowPart;
}

bool WaitForOnlyTarget(const TargetIdentity& target)
{
	const ULONGLONG deadline = GetTickCount64() + 5000;
	do
	{
		if (IsOnlyTargetActive(target))
			return true;
		Sleep(100);
	} while (GetTickCount64() < deadline);
	return IsOnlyTargetActive(target);
}

bool ApplyTopology(const Topology& topology, bool validate,
	bool allowChanges, LONG& result)
{
	UINT32 flags = (validate ? SDC_VALIDATE : SDC_APPLY) |
		SDC_USE_SUPPLIED_DISPLAY_CONFIG;
	if (allowChanges)
		flags |= SDC_ALLOW_CHANGES;
	result = SetDisplayConfig(static_cast<UINT32>(topology.paths.size()),
		const_cast<DISPLAYCONFIG_PATH_INFO*>(topology.paths.data()),
		static_cast<UINT32>(topology.modes.size()),
		topology.modes.empty() ? nullptr :
			const_cast<DISPLAYCONFIG_MODE_INFO*>(topology.modes.data()), flags);
	return result == ERROR_SUCCESS;
}

std::set<TargetIdentity> ActiveTargets(const Topology& topology)
{
	std::set<TargetIdentity> result;
	for (const auto& path : topology.paths)
	{
		TargetIdentity identity;
		identity.adapter = path.targetInfo.adapterId;
		identity.id = path.targetInfo.id;
		result.insert(identity);
	}
	return result;
}

bool WaitForTargets(const Topology& expected)
{
	const auto expectedTargets = ActiveTargets(expected);
	const ULONGLONG deadline = GetTickCount64() + 5000;
	do
	{
		Topology active;
		if (QueryTopology(QDC_ONLY_ACTIVE_PATHS, active) == ERROR_SUCCESS &&
			ActiveTargets(active) == expectedTargets)
			return true;
		Sleep(100);
	} while (GetTickCount64() < deadline);
	return false;
}
}

namespace DisplayTopologySession
{
	bool HasPendingRecovery(const std::string& statePath)
	{
		std::string value;
		return ReadRecovery(statePath, value);
	}

	bool RestorePending(const std::string& statePath, const char* reason,
		bool& restored, std::string& error)
	{
		restored = false;
		std::string encoded;
		if (!ReadRecovery(statePath, encoded))
			return true;
		Topology topology;
		if (!Deserialize(encoded, topology, error))
		{
			DebugLog::Log("Display topology restore rejected: reason=%s error=%s record_retained=1",
				reason, error.c_str());
			return false;
		}
		LONG result = ERROR_SUCCESS;
		if (!ApplyTopology(topology, true, false, result) ||
			!ApplyTopology(topology, false, false, result) ||
			!WaitForTargets(topology))
		{
			error = result == ERROR_SUCCESS ?
				"restored display targets were not confirmed" :
				"SetDisplayConfig restore failed with Windows error " +
					std::to_string(result);
			DebugLog::Log("Display topology restore failed: reason=%s windows_error=%ld record_retained=1",
				reason, result);
			return false;
		}
		if (!UpdateRecoveryEntry(statePath, nullptr, error))
		{
			DebugLog::Log("Display topology restored but recovery record could not clear: reason=%s error=%s",
				reason, error.c_str());
			return false;
		}
		restored = true;
		DebugLog::Log("Display topology restored: reason=%s paths=%zu modes=%zu record_cleared=1",
			reason, topology.paths.size(), topology.modes.size());
		return true;
	}

	bool BeginTargetOnly(const std::wstring& friendlyName,
		const std::string& statePath, std::string& error)
	{
		if (friendlyName.empty())
		{
			error = "target-only display session requires fullscreen_monitor_name";
			return false;
		}
		Topology current;
		LONG result = QueryTopology(QDC_ONLY_ACTIVE_PATHS, current);
		if (result != ERROR_SUCCESS || current.paths.empty())
		{
			error = "cannot capture active display topology (Windows error " +
				std::to_string(result) + ")";
			return false;
		}
		Topology all;
		result = QueryTopology(QDC_ALL_PATHS, all);
		if (result != ERROR_SUCCESS)
		{
			error = "cannot enumerate connected display paths (Windows error " +
				std::to_string(result) + ")";
			return false;
		}

		std::set<TargetIdentity> identities;
		const DISPLAYCONFIG_PATH_INFO* selected = nullptr;
		TargetIdentity selectedIdentity;
		for (const auto& path : all.paths)
		{
			if (!path.targetInfo.targetAvailable)
				continue;
			std::wstring name;
			if (!TargetName(path, name) || _wcsicmp(name.c_str(), friendlyName.c_str()) != 0)
				continue;
			TargetIdentity identity;
			identity.adapter = path.targetInfo.adapterId;
			identity.id = path.targetInfo.id;
			identities.insert(identity);
			if (!selected)
			{
				selected = &path;
				selectedIdentity = identity;
			}
		}
		if (identities.size() != 1 || !selected)
		{
			error = identities.empty() ?
				"configured display target is not connected" :
				"configured display target name is ambiguous";
			return false;
		}

		const std::string encoded = Serialize(current);
		if (!UpdateRecoveryEntry(statePath, &encoded, error))
			return false;
		DebugLog::Log("Display topology snapshot persisted: target='%S' paths=%zu modes=%zu state=%s",
			friendlyName.c_str(), current.paths.size(), current.modes.size(),
			statePath.c_str());

		Topology targetOnly;
		targetOnly.paths.push_back(*selected);
		auto& targetPath = targetOnly.paths.front();
		targetPath.flags |= DISPLAYCONFIG_PATH_ACTIVE;
		targetPath.sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
		targetPath.targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
		// No mode table asks CCD best-mode logic for this one connected path.
		result = ERROR_SUCCESS;
		if (!ApplyTopology(targetOnly, true, true, result) ||
			!ApplyTopology(targetOnly, false, true, result) ||
			!WaitForOnlyTarget(selectedIdentity))
		{
			error = result == ERROR_SUCCESS ?
				"configured target did not become the only active display" :
				"SetDisplayConfig target-only failed with Windows error " +
					std::to_string(result);
			bool restored = false;
			std::string restoreError;
			RestorePending(statePath, "target-only-failure", restored, restoreError);
			if (!restoreError.empty())
				error += "; restore: " + restoreError;
			return false;
		}
		DebugLog::Log("Target-only display topology active: target='%S' recovery_pending=1",
			friendlyName.c_str());
		return true;
	}
}
