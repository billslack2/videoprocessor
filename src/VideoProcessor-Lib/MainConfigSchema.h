#pragma once

#include "ConfigSchema.h"
#include "QueueConfiguration.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <string>
#include <vector>

// Startup-only schema for VideoProcessor.cfg.  It shares the strict validation
// engine with renderer configuration without making application settings
// dynamically selectable by renderer profile rules.
namespace MainConfigSchema
{
	inline bool IsRendererAliasName(const std::string& name)
	{
		if (name.empty() || name.size() > 64 || name == "vprenderer" ||
			!std::isalpha(static_cast<unsigned char>(name.front())))
			return false;
		return std::all_of(name.begin() + 1, name.end(), [](unsigned char c)
			{ return std::isalnum(c) || c == '_' || c == '-'; });
	}

	inline bool OwnsSection(const std::string& section)
	{
		return section == "command_line" ||
			section == "general" ||
			section == "renderer_alias" ||
			section == "queue" ||
			section.rfind("queue.", 0) == 0 ||
			section == "directshow" ||
			section == "vprenderer.input_processing" ||
			section == "directshow.conversion" ||
			section == "directshow.ppm" ||
			section == "queue_recovery" ||
			section == "lldv" ||
			section.rfind("lldv.", 0) == 0 ||
			section == "logging" ||
			section == "decklink" ||
			section == "shortcuts" ||
			section == "p010_conversion" ||
			section == "ppm_correction" ||
			section == "display_refresh_rate_override" ||
			section == "shaders" ||
			section.rfind("shaders.", 0) == 0 ||
			section.rfind("shader.", 0) == 0;
	}

	inline bool Validate(const ConfigFile& config, std::string& error)
	{
		error.clear();
		const std::vector<ConfigSchema::KeyRule> commandLineRules = {
			ConfigSchema::Boolean("fullscreen"),
			ConfigSchema::Boolean("windowedfullscreenmode"),
			ConfigSchema::Boolean("windowed_fullscreen_mode"),
			ConfigSchema::Any("fullscreen_monitor_name"),
			ConfigSchema::Choice("fullscreen_monitor_session_mode",
				{ "existing", "target-only" }),
			ConfigSchema::Any("renderer"),
			ConfigSchema::Any("hide_legacy_renderers"),
			ConfigSchema::Integer("queue_size", 1, INT_MAX),
			ConfigSchema::Any("capture_device"),
			{
				"frame_offset",
				[](const std::string& value)
				{
					if (ConfigFile::NormalizeName(value) == "auto") return true;
					return ConfigSchema::Integer("value", 0, INT_MAX).validator(value);
				},
				"AUTO or a non-negative integer"
			},
			ConfigSchema::Any("video_conversion"),
			ConfigSchema::Any("container_colorspace"),
			ConfigSchema::Any("hdr_colorspace"),
			ConfigSchema::Any("hdr_luminance"),
			ConfigSchema::Any("renderer_start_stop_time_method"),
			ConfigSchema::Any("renderer_nominal_range"),
			ConfigSchema::Any("renderer_transfer_function"),
			ConfigSchema::Any("renderer_transfer_matrix"),
			ConfigSchema::Any("renderer_primaries"),
			ConfigSchema::Boolean("scene_detect"),
			ConfigSchema::Boolean("scene"),
			ConfigSchema::Boolean("disable_detection_features"),
			ConfigSchema::Any("scene_correction_mode"),
			ConfigSchema::Boolean("scene_correction_basic"),
			ConfigSchema::Choice("subtitle_reposition",
				{ "true", "false", "basic", "advanced", "on", "off", "1", "0" }),
			ConfigSchema::Boolean("newlldv"),
			ConfigSchema::Boolean("new_lldv"),
			// Interface resolution deliberately validates this value non-fatally:
			// malformed persisted values log and fall back to Classic rather than
			// making capture startup fail.
			ConfigSchema::Any("interface"),
			ConfigSchema::Boolean("noui"),
			ConfigSchema::Boolean("no_ui"),
			ConfigSchema::Boolean("startminimized"),
			ConfigSchema::Boolean("start_minimized"),
			ConfigSchema::Any("capture_input"),
			ConfigSchema::Boolean("switch_refresh_rate"),
			ConfigSchema::Boolean("persist_profile_selection"),
			ConfigSchema::Integer("profile_change_display_seconds", 0, 60)
		};
		if (!ConfigSchema::ValidateSection(
			config, "command_line", commandLineRules, error))
			return false;
		// [general] was renderer-profile state in the old layout. It becomes
		// application startup configuration only when the VP-0079 renderer root
		// is present, so retain validation compatibility for old profile files.
		bool hasUnifiedRenderer = config.HasSection("vprenderer");
		if (!hasUnifiedRenderer)
			for (const std::string& section : config.GetSectionNames())
				if (section.rfind("vprenderer.", 0) == 0)
				{
					hasUnifiedRenderer = true;
					break;
				}
		if (hasUnifiedRenderer &&
			!ConfigSchema::ValidateSection(
				config, "general", commandLineRules, error))
			return false;

		// Queue policy is deliberately expressed in whole frames and capped so
		// a config typo cannot create an impractically deep live queue. The old
		// names remain accepted for configuration-file compatibility.
		const std::vector<ConfigSchema::KeyRule> queueRules = {
			ConfigSchema::Any("when"),
			ConfigSchema::Any("shortcut"),
			ConfigSchema::Any("cycle_shortcut"),
			ConfigSchema::Integer("queue_size", 1, INT_MAX),
			ConfigSchema::Integer("lead_frames", 0, 16),
			ConfigSchema::Integer("target_frames", 0, 16),
			ConfigSchema::Integer("active_picture_lookahead_frames", 0, 8),
			ConfigSchema::Integer("startup_preroll_frames", 0, 16),
			ConfigSchema::Integer("steady_reserve_frames", 0, 16),
			ConfigSchema::Integer("reset_after_render_restart_seconds", 1, INT_MAX),
			ConfigSchema::Integer("reset_queue_too_large_percent", 1, 200)
		};
		std::string defaultQueueSection;
		const bool hasDefaultQueue = QueueConfiguration::ResolveDefaultSection(
			config, defaultQueueSection);
		for (const std::string& section : config.GetSectionNames())
			if (section == "queue" ||
				QueueConfiguration::IsDirectNamedQueueSection(section))
			{
				if (!ConfigSchema::ValidateSection(config, section, queueRules, error))
					return false;
			}

		const std::vector<ConfigSchema::KeyRule> directShowRules = {
			ConfigSchema::Integer("presentation_lead_frames", 0, 16),
			commandLineRules[9],  // frame_offset
			commandLineRules[10], // video_conversion
			commandLineRules[11], // container_colorspace
			commandLineRules[12], // hdr_colorspace
			commandLineRules[13], // hdr_luminance
			commandLineRules[14], // renderer_start_stop_time_method
			commandLineRules[15], // renderer_nominal_range
			commandLineRules[16], // renderer_transfer_function
			commandLineRules[17], // renderer_transfer_matrix
			commandLineRules[18]  // renderer_primaries
		};
		if (!ConfigSchema::ValidateSection(
			config, "directshow", directShowRules, error))
			return false;
		const std::vector<ConfigSchema::KeyRule> vpRendererInputRules = {
			commandLineRules[10], // video_conversion
			commandLineRules[11], // container_colorspace
			commandLineRules[12], // hdr_colorspace
			commandLineRules[13]  // hdr_luminance
		};
		if (!ConfigSchema::ValidateSection(
			config, "vprenderer.input_processing", vpRendererInputRules, error))
			return false;
		// [general] is the shared input-policy default. [directshow] and
		// [vprenderer.input_processing] may independently override any of its fields, so overlap
		// is deliberate and resolved by the runtime rather than rejected here.

		const std::vector<ConfigSchema::KeyRule> deckLinkRules = {
			ConfigSchema::Choice("rgb_8bit_packing", { "AUTO", "ARGB", "BGRA" }),
			ConfigSchema::Choice("rgb_10bit_packing", { "AUTO", "R210", "R10B", "R10L" }),
			ConfigSchema::Choice("rgb_12bit_packing", { "AUTO", "R12B", "R12L" })
		};
		if (!ConfigSchema::ValidateSection(
			config, "decklink", deckLinkRules, error))
			return false;
		std::string foregroundOnly;
		if (config.TryGetString("shortcuts", "foreground_only", foregroundOnly) &&
			!ConfigSchema::Boolean("foreground_only").validator(foregroundOnly))
		{
			error = "[shortcuts] key 'foreground_only' must be a Boolean";
			return false;
		}
		const std::vector<ConfigSchema::KeyRule> conversionRules = {
			ConfigSchema::Choice("conversion_method",
				{ "auto", "simd", "optimized", "standard" }),
			ConfigSchema::Integer("min_core_count", 1, INT_MAX),
			ConfigSchema::Integer("max_core_count", 1, INT_MAX)
		};
		if (!ConfigSchema::ValidateSection(
			config, "directshow.conversion", conversionRules, error))
			return false;
		const std::vector<ConfigSchema::KeyRule> ppmRules = {
			{
				"ppm",
				[](const std::string& value)
				{
					const std::string normalized = ConfigFile::NormalizeName(value);
					if (normalized == "auto") return true;
					try
					{
						size_t consumed = 0;
						const long long parsed = std::stoll(
							ConfigFile::Trim(value), &consumed);
						return consumed == ConfigFile::Trim(value).size() &&
							parsed >= -1000000 && parsed <= 1000000;
					}
					catch (const std::exception&) { return false; }
				},
				"AUTO or an integer from -1000000 to 1000000"
			}
		};
		if (!ConfigSchema::ValidateSection(
			config, "directshow.ppm", ppmRules, error))
			return false;
		if (const auto* aliases = config.GetSectionValues("renderer_alias"))
			for (const auto& alias : *aliases)
			{
				if (!IsRendererAliasName(alias.first))
				{
					error = "[renderer_alias] alias '" + alias.first +
						"' must be an identifier other than vprenderer";
					return false;
				}
				if (!ConfigSchema::Integer("alias", 1, INT_MAX).validator(
					alias.second))
				{
					error = "[renderer_alias] alias '" + alias.first +
						"' must be a positive renderer-selector index";
					return false;
				}
			}

		std::string value;
		std::string legacyValue;
		for (const std::string& section : config.GetSectionNames())
			if ((section == "queue" ||
				QueueConfiguration::IsDirectNamedQueueSection(section)) &&
				config.TryGetString(section, "target_frames", value) &&
				config.TryGetString(section, "steady_reserve_frames", legacyValue))
			{
				error = "cannot specify both [" + section +
					"] target_frames and legacy steady_reserve_frames";
				return false;
			}
		if (hasDefaultQueue && config.TryGetString(
			defaultQueueSection, "lead_frames", value) &&
			config.TryGetString(
				"directshow", "presentation_lead_frames", legacyValue))
		{
			error = "cannot specify both [" + defaultQueueSection +
				"] lead_frames and legacy "
				"[directshow] presentation_lead_frames";
			return false;
		}

		const std::vector<ConfigSchema::KeyRule> recoveryRules = {
			ConfigSchema::Integer("reset_after_render_restart_seconds", 1, INT_MAX),
			ConfigSchema::Integer("reset_queue_too_large_percent", 1, 200)
		};
		if (!ConfigSchema::ValidateSection(
			config, "queue_recovery", recoveryRules, error))
			return false;
		for (const char* key : { "reset_after_render_restart_seconds",
			"reset_queue_too_large_percent" })
		{
			std::string canonicalValue;
			std::string legacyValue;
			if (hasDefaultQueue && config.TryGetString(
				defaultQueueSection, key, canonicalValue) &&
				config.TryGetString("queue_recovery", key, legacyValue))
			{
				error = "cannot specify both [" + defaultQueueSection + "] " +
					key + " and legacy [queue_recovery] " + key;
				return false;
			}
		}

		const std::vector<ConfigSchema::KeyRule> lldvRules = {
			ConfigSchema::Any("when"),
			ConfigSchema::Any("shortcut"),
			ConfigSchema::Any("cycle_shortcut"),
			ConfigSchema::NumberAtLeast("max_cll", 0.0),
			ConfigSchema::NumberAtLeast("max_fall", 0.0),
			ConfigSchema::NumberAtLeast("mastering_min_luminance", 0.0),
			ConfigSchema::NumberAtLeast("mastering_max_luminance", 0.0, false)
		};
		for (const std::string& section : config.GetSectionNames())
			if (section == "lldv" ||
				(section.rfind("lldv.", 0) == 0 &&
				 section.size() > sizeof("lldv.") - 1 &&
				 section.find('.', sizeof("lldv.") - 1) == std::string::npos))
				if (!ConfigSchema::ValidateSection(config, section, lldvRules,
					error))
					return false;

		// Debug-log retention deliberately defaults safely instead of making an
		// otherwise usable configuration fatal. Resolution validates 1-200
		// before logger startup; the schema still rejects unknown logging keys.
		const std::vector<ConfigSchema::KeyRule> loggingRules = {
			ConfigSchema::Boolean("enabled"),
			ConfigSchema::Boolean("debug"),
			ConfigSchema::Any("debug_log_retention")
		};
		return ConfigSchema::ValidateSection(
			config, "logging", loggingRules, error);
	}
}
