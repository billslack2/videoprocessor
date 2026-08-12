#pragma once

#include "ConfigFile.h"

#include <string>
#include <vector>

// Renderer-owned namespace resolution for VP's built-in renderer. This view is
// intentionally read-only and separate from generic ConfigFile so application
// configuration does not acquire renderer-specific aliases.
class RendererConfigView
{
public:
	static constexpr const char* DISPLAY_SECTION = "vpvr.display";
	static constexpr const char* GENERAL_SECTION = "vpvr.general";
	// VP-0079 keeps all built-in renderer settings below one owner namespace.
	// Child sections are selectable variants; the root is the renderer baseline.
	static constexpr const char* VPRENDERER_SECTION = "vprenderer";
	static constexpr const char* LEGACY_DISPLAY_SECTION = "display";
	static constexpr const char* HISTORICAL_DISPLAY_SECTION = "libplacebo";

	explicit RendererConfigView(const ConfigFile& config) : m_config(config) {}

	static bool OwnsSection(const std::string& section)
	{
		return section == DISPLAY_SECTION ||
			section == GENERAL_SECTION ||
			section == VPRENDERER_SECTION ||
			section.rfind("vprenderer.", 0) == 0 ||
			section == LEGACY_DISPLAY_SECTION ||
			section == HISTORICAL_DISPLAY_SECTION;
	}

	static bool IsPolicyKey(const std::string& key)
	{
		return key == "switch_refresh_rate" ||
			key == "output_diagnostics" ||
			key == "diagnostic_disable_shader_cache" ||
			key == "diagnostic_disable_compute" ||
			key == "diagnostic_force_8bit_sdr_swapchain" ||
			key == "diagnostic_allow_limited_g22" ||
			key == "diagnostic_vp_owned_dxgi_presenter";
	}

	bool Validate(std::string& error,
		std::vector<std::string>& warnings) const
	{
		error.clear();
		const bool hasTargetRenderer =
			m_config.HasSection(VPRENDERER_SECTION);
		if (hasTargetRenderer)
		{
			if (m_config.HasSection(DISPLAY_SECTION) ||
				m_config.HasSection(GENERAL_SECTION) ||
				m_config.HasSection(LEGACY_DISPLAY_SECTION) ||
				m_config.HasSection(HISTORICAL_DISPLAY_SECTION))
			{
				error = "[vprenderer] cannot be combined with legacy built-in renderer sections";
				return false;
			}
			return true;
		}
		const bool hasCanonicalDisplay =
			m_config.HasSection(DISPLAY_SECTION);
		if (hasCanonicalDisplay &&
			m_config.HasSection(LEGACY_DISPLAY_SECTION))
		{
			error = "configuration cannot contain both [" +
				std::string(DISPLAY_SECTION) + "] and [" +
				LEGACY_DISPLAY_SECTION + "]";
			return false;
		}
		if (hasCanonicalDisplay &&
			m_config.HasSection(HISTORICAL_DISPLAY_SECTION))
		{
			error = "configuration cannot contain both [" +
				std::string(DISPLAY_SECTION) + "] and [" +
				HISTORICAL_DISPLAY_SECTION + "]";
			return false;
		}

		for (const char* key :
			{ "switch_refresh_rate", "output_diagnostics",
			  "diagnostic_disable_shader_cache", "diagnostic_disable_compute",
			  "diagnostic_force_8bit_sdr_swapchain",
			  "diagnostic_allow_limited_g22",
			  "diagnostic_vp_owned_dxgi_presenter" })
		{
			if (!HasKey(GENERAL_SECTION, key))
				continue;
			for (const char* legacy :
				{ "general", LEGACY_DISPLAY_SECTION,
				  HISTORICAL_DISPLAY_SECTION })
				if (HasKey(legacy, key))
				{
					error = "renderer policy key '" +
						std::string(key) + "' cannot appear in both [" +
						GENERAL_SECTION + "] and [" + legacy + "]";
					return false;
				}
		}

		if (!hasCanonicalDisplay &&
			SectionHasValues(LEGACY_DISPLAY_SECTION))
			warnings.push_back(
				"[display] is deprecated for built-in renderer settings; "
				"use [vpvr.display]");
		if (!hasCanonicalDisplay &&
			HistoricalSectionIsConsumed())
			warnings.push_back(
				"[libplacebo] is a historical built-in renderer fallback; "
				"use [vpvr.display]");

		bool usesLegacyGeneral = false;
		for (const char* key :
			{ "switch_refresh_rate", "output_diagnostics",
			  "diagnostic_disable_shader_cache", "diagnostic_disable_compute",
			  "diagnostic_force_8bit_sdr_swapchain",
			  "diagnostic_allow_limited_g22",
			  "diagnostic_vp_owned_dxgi_presenter" })
			usesLegacyGeneral = usesLegacyGeneral ||
				HasKey("general", key);
		if (usesLegacyGeneral)
			warnings.push_back(
				"built-in renderer policy keys in [general] are deprecated; "
				"use [vpvr.general]");
		return true;
	}

	bool TryGetDisplayString(const std::string& key,
		std::string& value) const
	{
		if (m_config.HasSection(VPRENDERER_SECTION))
			return m_config.TryGetString(VPRENDERER_SECTION, key, value);
		if (m_config.HasSection(DISPLAY_SECTION))
			return m_config.TryGetString(DISPLAY_SECTION, key, value);
		if (m_config.TryGetString(LEGACY_DISPLAY_SECTION, key, value))
			return true;
		return m_config.TryGetString(
			HISTORICAL_DISPLAY_SECTION, key, value);
	}

	bool TryGetDisplayBool(const std::string& key, bool& value) const
	{
		const char* section = DisplaySectionForKey(key);
		return section != nullptr &&
			m_config.TryGetBool(section, key, value);
	}

	bool TryGetPolicyString(const std::string& key,
		std::string& value) const
	{
		const char* section = PolicySectionForKey(key);
		return section != nullptr &&
			m_config.TryGetString(section, key, value);
	}

	bool TryGetPolicyBool(const std::string& key, bool& value) const
	{
		const char* section = PolicySectionForKey(key);
		return section != nullptr &&
			m_config.TryGetBool(section, key, value);
	}

private:
	bool HasKey(const std::string& section,
		const std::string& key) const
	{
		std::string ignored;
		return m_config.TryGetString(section, key, ignored);
	}

	bool SectionHasValues(const std::string& section) const
	{
		const auto* values = m_config.GetSectionValues(section);
		return values != nullptr && !values->empty();
	}

	bool HistoricalSectionIsConsumed() const
	{
		const auto* values =
			m_config.GetSectionValues(HISTORICAL_DISPLAY_SECTION);
		if (!values)
			return false;
		for (const auto& value : *values)
		{
			if (IsPolicyKey(value.first))
			{
				if (!HasKey("general", value.first) &&
					!HasKey(LEGACY_DISPLAY_SECTION, value.first))
					return true;
			}
			else if (!HasKey(LEGACY_DISPLAY_SECTION, value.first))
				return true;
		}
		return false;
	}

	const char* DisplaySectionForKey(const std::string& key) const
	{
		if (m_config.HasSection(VPRENDERER_SECTION))
			return HasKey(VPRENDERER_SECTION, key) ? VPRENDERER_SECTION : nullptr;
		if (m_config.HasSection(DISPLAY_SECTION))
			return HasKey(DISPLAY_SECTION, key) ?
				DISPLAY_SECTION : nullptr;
		if (HasKey(LEGACY_DISPLAY_SECTION, key))
			return LEGACY_DISPLAY_SECTION;
		if (HasKey(HISTORICAL_DISPLAY_SECTION, key))
			return HISTORICAL_DISPLAY_SECTION;
		return nullptr;
	}

	const char* PolicySectionForKey(const std::string& key) const
	{
		if (HasKey(VPRENDERER_SECTION, key))
			return VPRENDERER_SECTION;
		if (HasKey(GENERAL_SECTION, key))
			return GENERAL_SECTION;
		if (HasKey("general", key))
			return "general";
		if (HasKey(LEGACY_DISPLAY_SECTION, key))
			return LEGACY_DISPLAY_SECTION;
		if (HasKey(HISTORICAL_DISPLAY_SECTION, key))
			return HISTORICAL_DISPLAY_SECTION;
		return nullptr;
	}

	const ConfigFile& m_config;
};
