#include <pch.h>

#include "DisplayTopologySession.h"

#include <DebugLog.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
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
	input.close();

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

bool CopyReferencedMode(const Topology& source, UINT32& modeIndex,
	Topology& destination, std::map<UINT32, UINT32>& remapped)
{
	if (modeIndex == DISPLAYCONFIG_PATH_MODE_IDX_INVALID)
		return true;
	if (modeIndex >= source.modes.size())
		return false;
	const auto existing = remapped.find(modeIndex);
	if (existing != remapped.end())
	{
		modeIndex = existing->second;
		return true;
	}
	const UINT32 newIndex = static_cast<UINT32>(destination.modes.size());
	destination.modes.push_back(source.modes[modeIndex]);
	remapped.emplace(modeIndex, newIndex);
	modeIndex = newIndex;
	return true;
}

bool BuildExactSinglePathTopology(const Topology& source,
	const DISPLAYCONFIG_PATH_INFO& selected, Topology& destination)
{
	// Keep the mode table exactly as CCD returned it.  A path's mode indices
	// are defined against this table; reducing or remapping it caused Windows
	// to reject the otherwise valid active path with ERROR_INVALID_PARAMETER.
	destination = source;
	destination.paths.clear();
	destination.paths.push_back(selected);
	auto& path = destination.paths.front();
	path.flags |= DISPLAYCONFIG_PATH_ACTIVE;
	return path.sourceInfo.modeInfoIdx != DISPLAYCONFIG_PATH_MODE_IDX_INVALID &&
		path.targetInfo.modeInfoIdx != DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
}

bool SourceGdiName(const DISPLAYCONFIG_PATH_INFO& path, std::wstring& name)
{
	DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
	source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
	source.header.size = sizeof(source);
	source.header.adapterId = path.sourceInfo.adapterId;
	source.header.id = path.sourceInfo.id;
	if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS)
		return false;
	name = source.viewGdiDeviceName;
	return !name.empty();
}

bool ApplyMaximumMode(const DISPLAYCONFIG_PATH_INFO& path, std::string& error)
{
	std::wstring device;
	if (!SourceGdiName(path, device))
	{
		error = "cannot resolve display source name";
		return false;
	}
	DEVMODEW best = {};
	bool found = false;
	for (DWORD index = 0;; ++index)
	{
		DEVMODEW candidate = {};
		candidate.dmSize = sizeof(candidate);
		if (!EnumDisplaySettingsExW(device.c_str(), index, &candidate, 0))
			break;
		if (candidate.dmBitsPerPel < 32 || candidate.dmPelsWidth == 0 ||
			candidate.dmPelsHeight == 0 || candidate.dmDisplayFrequency == 0)
			continue;
		const uint64_t area = static_cast<uint64_t>(candidate.dmPelsWidth) * candidate.dmPelsHeight;
		const uint64_t bestArea = static_cast<uint64_t>(best.dmPelsWidth) * best.dmPelsHeight;
		if (!found || candidate.dmDisplayFrequency > best.dmDisplayFrequency ||
			(candidate.dmDisplayFrequency == best.dmDisplayFrequency && area > bestArea))
		{
			best = candidate;
			found = true;
		}
	}
	if (!found || ChangeDisplaySettingsExW(device.c_str(), &best, nullptr,
		CDS_TEST, nullptr) != DISP_CHANGE_SUCCESSFUL ||
		ChangeDisplaySettingsExW(device.c_str(), &best, nullptr, 0, nullptr) !=
		DISP_CHANGE_SUCCESSFUL)
	{
		error = "cannot apply maximum compatible display mode";
		return false;
	}
	DebugLog::Log("Display maximum mode applied: device=%S resolution=%lux%lu refresh=%luHz",
		device.c_str(), best.dmPelsWidth, best.dmPelsHeight,
		best.dmDisplayFrequency);
	return true;
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

bool SameRational(const DISPLAYCONFIG_RATIONAL& left,
	const DISPLAYCONFIG_RATIONAL& right)
{
	return left.Numerator == right.Numerator &&
		left.Denominator == right.Denominator;
}

UINT32 SourceModeIndex(const DISPLAYCONFIG_PATH_INFO& path)
{
	return (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) != 0 ?
		path.sourceInfo.sourceModeInfoIdx : path.sourceInfo.modeInfoIdx;
}

UINT32 TargetModeIndex(const DISPLAYCONFIG_PATH_INFO& path)
{
	return (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) != 0 ?
		path.targetInfo.targetModeInfoIdx : path.targetInfo.modeInfoIdx;
}

const DISPLAYCONFIG_MODE_INFO* ReferencedMode(const Topology& topology,
	UINT32 index, DISPLAYCONFIG_MODE_INFO_TYPE type)
{
	if (index == DISPLAYCONFIG_PATH_MODE_IDX_INVALID ||
		index >= topology.modes.size())
	{
		return nullptr;
	}
	const DISPLAYCONFIG_MODE_INFO& mode = topology.modes[index];
	return mode.infoType == type ? &mode : nullptr;
}

bool SameSourceMode(const DISPLAYCONFIG_MODE_INFO* left,
	const DISPLAYCONFIG_MODE_INFO* right)
{
	if (!left || !right)
		return left == right;
	const auto& first = left->sourceMode;
	const auto& second = right->sourceMode;
	return first.width == second.width && first.height == second.height &&
		first.pixelFormat == second.pixelFormat &&
		first.position.x == second.position.x &&
		first.position.y == second.position.y;
}

bool SameTargetMode(const DISPLAYCONFIG_MODE_INFO* left,
	const DISPLAYCONFIG_MODE_INFO* right)
{
	if (!left || !right)
		return left == right;
	const auto& first = left->targetMode.targetVideoSignalInfo;
	const auto& second = right->targetMode.targetVideoSignalInfo;
	return first.pixelRate == second.pixelRate &&
		SameRational(first.hSyncFreq, second.hSyncFreq) &&
		SameRational(first.vSyncFreq, second.vSyncFreq) &&
		first.activeSize.cx == second.activeSize.cx &&
		first.activeSize.cy == second.activeSize.cy &&
		first.totalSize.cx == second.totalSize.cx &&
		first.totalSize.cy == second.totalSize.cy &&
		first.videoStandard == second.videoStandard &&
		first.scanLineOrdering == second.scanLineOrdering;
}

bool SamePathState(const Topology& expectedTopology,
	const DISPLAYCONFIG_PATH_INFO& expected,
	const Topology& actualTopology,
	const DISPLAYCONFIG_PATH_INFO& actual)
{
	constexpr UINT32 relevantPathFlags = DISPLAYCONFIG_PATH_ACTIVE |
		DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE;
	return (expected.flags & relevantPathFlags) ==
			(actual.flags & relevantPathFlags) &&
		expected.sourceInfo.adapterId.HighPart ==
			actual.sourceInfo.adapterId.HighPart &&
		expected.sourceInfo.adapterId.LowPart ==
			actual.sourceInfo.adapterId.LowPart &&
		expected.sourceInfo.id == actual.sourceInfo.id &&
		expected.targetInfo.adapterId.HighPart ==
			actual.targetInfo.adapterId.HighPart &&
		expected.targetInfo.adapterId.LowPart ==
			actual.targetInfo.adapterId.LowPart &&
		expected.targetInfo.id == actual.targetInfo.id &&
		expected.targetInfo.outputTechnology ==
			actual.targetInfo.outputTechnology &&
		expected.targetInfo.rotation == actual.targetInfo.rotation &&
		expected.targetInfo.scaling == actual.targetInfo.scaling &&
		SameRational(expected.targetInfo.refreshRate,
			actual.targetInfo.refreshRate) &&
		expected.targetInfo.scanLineOrdering ==
			actual.targetInfo.scanLineOrdering &&
		SameSourceMode(
			ReferencedMode(expectedTopology, SourceModeIndex(expected),
				DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE),
			ReferencedMode(actualTopology, SourceModeIndex(actual),
				DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)) &&
		SameTargetMode(
			ReferencedMode(expectedTopology, TargetModeIndex(expected),
				DISPLAYCONFIG_MODE_INFO_TYPE_TARGET),
			ReferencedMode(actualTopology, TargetModeIndex(actual),
				DISPLAYCONFIG_MODE_INFO_TYPE_TARGET));
}

bool IsExactTopologyActive(const Topology& expected, const Topology& actual)
{
	if (expected.paths.size() != actual.paths.size() ||
		ActiveTargets(expected) != ActiveTargets(actual))
	{
		return false;
	}
	for (const auto& expectedPath : expected.paths)
	{
		const auto matching = std::find_if(actual.paths.begin(),
			actual.paths.end(), [&](const DISPLAYCONFIG_PATH_INFO& path)
			{
				return expectedPath.targetInfo.adapterId.HighPart ==
						path.targetInfo.adapterId.HighPart &&
					expectedPath.targetInfo.adapterId.LowPart ==
						path.targetInfo.adapterId.LowPart &&
					expectedPath.targetInfo.id == path.targetInfo.id;
			});
		if (matching == actual.paths.end() ||
			!SamePathState(expected, expectedPath, actual, *matching))
		{
			return false;
		}
	}
	return true;
}

void LogTopologyDifference(const Topology& expected, const Topology& actual)
{
	DebugLog::Log(
		"Display topology verification mismatch: kind=summary "
		"expected_paths=%zu actual_paths=%zu expected_targets=%zu "
		"actual_targets=%zu",
		expected.paths.size(), actual.paths.size(),
		ActiveTargets(expected).size(), ActiveTargets(actual).size());
	for (const auto& expectedPath : expected.paths)
	{
		const auto matching = std::find_if(actual.paths.begin(),
			actual.paths.end(), [&](const DISPLAYCONFIG_PATH_INFO& path)
			{
				return expectedPath.targetInfo.adapterId.HighPart ==
						path.targetInfo.adapterId.HighPart &&
					expectedPath.targetInfo.adapterId.LowPart ==
						path.targetInfo.adapterId.LowPart &&
					expectedPath.targetInfo.id == path.targetInfo.id;
			});
		if (matching == actual.paths.end())
		{
			DebugLog::Log(
				"Display topology verification mismatch: kind=missing-target "
				"adapter=%ld:%lu target_id=%u",
				expectedPath.targetInfo.adapterId.HighPart,
				expectedPath.targetInfo.adapterId.LowPart,
				expectedPath.targetInfo.id);
			continue;
		}
		if (SamePathState(expected, expectedPath, actual, *matching))
			continue;

		const DISPLAYCONFIG_MODE_INFO* expectedSource = ReferencedMode(
			expected, SourceModeIndex(expectedPath),
			DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE);
		const DISPLAYCONFIG_MODE_INFO* actualSource = ReferencedMode(
			actual, SourceModeIndex(*matching),
			DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE);
		const DISPLAYCONFIG_MODE_INFO* expectedTarget = ReferencedMode(
			expected, TargetModeIndex(expectedPath),
			DISPLAYCONFIG_MODE_INFO_TYPE_TARGET);
		const DISPLAYCONFIG_MODE_INFO* actualTarget = ReferencedMode(
			actual, TargetModeIndex(*matching),
			DISPLAYCONFIG_MODE_INFO_TYPE_TARGET);
		DebugLog::Log(
			"Display topology verification mismatch: kind=path-state "
			"adapter=%ld:%lu target_id=%u "
			"source_id_expected=%u source_id_actual=%u "
			"flags_expected=0x%08lx flags_actual=0x%08lx "
			"source_expected=%ux%u@%ld,%ld source_actual=%ux%u@%ld,%ld "
			"refresh_expected=%u/%u refresh_actual=%u/%u "
			"rotation_expected=%d rotation_actual=%d "
			"scaling_expected=%d scaling_actual=%d "
			"active_expected=%ux%u active_actual=%ux%u",
			expectedPath.targetInfo.adapterId.HighPart,
			expectedPath.targetInfo.adapterId.LowPart,
			expectedPath.targetInfo.id,
			expectedPath.sourceInfo.id, matching->sourceInfo.id,
			static_cast<unsigned long>(expectedPath.flags),
			static_cast<unsigned long>(matching->flags),
			expectedSource ? expectedSource->sourceMode.width : 0,
			expectedSource ? expectedSource->sourceMode.height : 0,
			expectedSource ? expectedSource->sourceMode.position.x : 0,
			expectedSource ? expectedSource->sourceMode.position.y : 0,
			actualSource ? actualSource->sourceMode.width : 0,
			actualSource ? actualSource->sourceMode.height : 0,
			actualSource ? actualSource->sourceMode.position.x : 0,
			actualSource ? actualSource->sourceMode.position.y : 0,
			expectedPath.targetInfo.refreshRate.Numerator,
			expectedPath.targetInfo.refreshRate.Denominator,
			matching->targetInfo.refreshRate.Numerator,
			matching->targetInfo.refreshRate.Denominator,
			static_cast<int>(expectedPath.targetInfo.rotation),
			static_cast<int>(matching->targetInfo.rotation),
			static_cast<int>(expectedPath.targetInfo.scaling),
			static_cast<int>(matching->targetInfo.scaling),
			expectedTarget ? expectedTarget->targetMode.targetVideoSignalInfo.activeSize.cx : 0,
			expectedTarget ? expectedTarget->targetMode.targetVideoSignalInfo.activeSize.cy : 0,
			actualTarget ? actualTarget->targetMode.targetVideoSignalInfo.activeSize.cx : 0,
			actualTarget ? actualTarget->targetMode.targetVideoSignalInfo.activeSize.cy : 0);
	}
}

bool WaitForExactTopology(const Topology& expected)
{
	const ULONGLONG started = GetTickCount64();
	const ULONGLONG deadline = started + 5000;
	unsigned int consecutiveMatches = 0;
	do
	{
		Topology active;
		if (QueryTopology(QDC_ONLY_ACTIVE_PATHS, active) == ERROR_SUCCESS &&
			IsExactTopologyActive(expected, active))
		{
			if (++consecutiveMatches >= 2)
			{
				DebugLog::Log(
					"Display topology verification: result=stable-exact "
					"observations=%u elapsed_ms=%llu paths=%zu",
					consecutiveMatches,
					static_cast<unsigned long long>(GetTickCount64() - started),
					expected.paths.size());
				return true;
			}
		}
		else
		{
			consecutiveMatches = 0;
		}
		Sleep(100);
	} while (GetTickCount64() < deadline);
	Topology finalObservation;
	if (QueryTopology(QDC_ONLY_ACTIVE_PATHS, finalObservation) == ERROR_SUCCESS)
		LogTopologyDifference(expected, finalObservation);
	else
		DebugLog::Log(
			"Display topology verification mismatch: kind=query-failed "
			"elapsed_ms=%llu",
			static_cast<unsigned long long>(GetTickCount64() - started));
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
		DebugLog::Log(
			"Display topology restore audit: phase=record-decoded reason=%s "
			"paths=%zu modes=%zu",
			reason, topology.paths.size(), topology.modes.size());
		LONG result = ERROR_SUCCESS;
		if (!ApplyTopology(topology, true, false, result) ||
			!ApplyTopology(topology, false, false, result) ||
			!WaitForExactTopology(topology))
		{
			error = result == ERROR_SUCCESS ?
				"exact restored display topology was not confirmed" :
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
		const DISPLAYCONFIG_PATH_INFO* activeSelected = nullptr;
		for (const auto& path : current.paths)
		{
			if (path.targetInfo.id == selectedIdentity.id &&
				path.targetInfo.adapterId.HighPart == selectedIdentity.adapter.HighPart &&
				path.targetInfo.adapterId.LowPart == selectedIdentity.adapter.LowPart)
			{
				activeSelected = &path;
				break;
			}
		}
		const Topology& selectedTopology = activeSelected ? current : all;
		const DISPLAYCONFIG_PATH_INFO& selectedPath = activeSelected ? *activeSelected : *selected;
		if (!BuildExactSinglePathTopology(selectedTopology, selectedPath, targetOnly) ||
			targetOnly.modes.empty())
		{
			error = "configured target has no complete display mode; refusing Windows best-mode fallback";
			bool restored = false;
			std::string restoreError;
			RestorePending(statePath, "target-only-missing-mode", restored, restoreError);
			return false;
		}
		result = ERROR_SUCCESS;
		if (!ApplyTopology(targetOnly, true, false, result) ||
			!ApplyTopology(targetOnly, false, false, result) ||
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
		std::string maximumModeError;
		if (!ApplyMaximumMode(targetOnly.paths.front(), maximumModeError))
		{
			error = maximumModeError;
			bool restored = false;
			std::string restoreError;
			RestorePending(statePath, "target-only-maximum-mode-failure", restored, restoreError);
			if (!restoreError.empty())
				error += "; restore: " + restoreError;
			return false;
		}
		DebugLog::Log("Target-only display topology active: target='%S' recovery_pending=1",
			friendlyName.c_str());
		return true;
	}
}
