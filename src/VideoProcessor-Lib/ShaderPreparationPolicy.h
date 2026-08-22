#pragma once

#include <ConfigFile.h>
#include <RendererProfileConfig.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace ShaderPreparationPolicy
{
	inline bool HasCompletedCacheLifetime(bool cacheExists,
		uint64_t cacheBytes, uint64_t cacheCreationTicks,
		bool clearRequested, bool statusReady,
		bool explicitClearInvalidationPolicy,
		uint64_t statusCompletedTicks)
	{
		return cacheExists && cacheBytes > 0 && !clearRequested && statusReady &&
			(explicitClearInvalidationPolicy ||
				cacheCreationTicks <= statusCompletedTicks);
	}

	using Section = std::map<std::string, std::string>;
	using Snapshot = std::map<std::string, Section>;

	inline bool IsMetadataKey(const std::string& key)
	{
		const std::string normalized = ConfigFile::NormalizeName(key);
		return normalized == "name" || normalized == "label" ||
			normalized == "shortcut" || normalized == "use_rule" ||
			normalized == "when" || normalized == "priority";
	}

	inline bool IsRenderingProfileSection(const std::string& section)
	{
		const std::string normalized = ConfigFile::NormalizeName(section);
		if (normalized == "vprenderer") return true;
		constexpr const char* prefix = "vprenderer.";
		if (normalized.rfind(prefix, 0) != 0) return false;
		const std::string name = normalized.substr(11);
		return !name.empty() && name.find('.') == std::string::npos &&
			!RendererProfileConfig::IsRendererChildNamespace(name);
	}

	inline bool IsShaderSection(const std::string& section)
	{
		const std::string normalized = ConfigFile::NormalizeName(section);
		return normalized == "shader" || normalized.rfind("shader.", 0) == 0 ||
			normalized == "shaders" || normalized.rfind("shaders.", 0) == 0;
	}

	inline Section ProcessingSettings(const Section& section)
	{
		Section result;
		for (const auto& setting : section)
			if (!IsMetadataKey(setting.first)) result.insert(setting);
		return result;
	}

	inline bool ShouldPrepare(const Snapshot& previous, const Snapshot& current)
	{
		std::multiset<Section> removedRenderingProfiles;
		for (const auto& before : previous)
			if (IsRenderingProfileSection(before.first) &&
				current.find(before.first) == current.end())
				removedRenderingProfiles.insert(ProcessingSettings(before.second));

		for (const auto& after : current)
		{
			const bool rendering = IsRenderingProfileSection(after.first);
			const bool shader = IsShaderSection(after.first);
			if (!rendering && !shader) continue;

			const auto before = previous.find(after.first);
			if (before == previous.end())
			{
				const Section processing = ProcessingSettings(after.second);
				if (processing.empty()) continue;
				if (rendering)
				{
					const auto renamed = removedRenderingProfiles.find(processing);
					if (renamed != removedRenderingProfiles.end())
					{
						removedRenderingProfiles.erase(renamed);
						continue;
					}
				}
				return true;
			}

			std::set<std::string> keys;
			for (const auto& setting : before->second) keys.insert(setting.first);
			for (const auto& setting : after.second) keys.insert(setting.first);
			for (const std::string& key : keys)
			{
				if (IsMetadataKey(key)) continue;
				const auto oldValue = before->second.find(key);
				const auto newValue = after.second.find(key);
				if (oldValue == before->second.end() ||
					newValue == after.second.end() ||
					oldValue->second != newValue->second)
					return true;
			}
		}
		// Removed profiles and shaders no longer need cache coverage.
		return false;
	}
}
