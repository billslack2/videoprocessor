#pragma once

#include "ConfigSchema.h"

#include <climits>
#include <string>
#include <vector>

// Startup-only schema for VideoProcessor.cfg.  It shares the strict validation
// engine with renderer configuration without making application settings
// dynamically selectable by renderer profile rules.
namespace MainConfigSchema
{
	inline bool OwnsSection(const std::string& section)
	{
		return section == "command_line" ||
			section == "queue_recovery" ||
			section == "lldv" ||
			section == "shortcuts" ||
			section == "p010_conversion" ||
			section == "ppm_correction" ||
			section == "display_refresh_rate_override" ||
			section == "shaders" ||
			section.rfind("shaders.", 0) == 0;
	}

	inline bool Validate(const ConfigFile& config, std::string& error)
	{
		error.clear();
		const std::vector<ConfigSchema::KeyRule> commandLineRules = {
			ConfigSchema::Boolean("fullscreen"),
			ConfigSchema::Boolean("windowedfullscreenmode"),
			ConfigSchema::Boolean("windowed_fullscreen_mode"),
			ConfigSchema::Any("renderer"),
			ConfigSchema::Integer("queue_size", 1, INT_MAX),
			ConfigSchema::Integer("alpha_queue_size", 1, INT_MAX),
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
			ConfigSchema::Any("scene_correction_mode"),
			ConfigSchema::Boolean("scene_correction_basic"),
			ConfigSchema::Choice("subtitle_reposition",
				{ "true", "false", "basic", "advanced", "on", "off", "1", "0" }),
			ConfigSchema::Boolean("newlldv"),
			ConfigSchema::Boolean("new_lldv"),
			ConfigSchema::Boolean("noui"),
			ConfigSchema::Boolean("no_ui"),
			ConfigSchema::Boolean("startminimized"),
			ConfigSchema::Boolean("start_minimized")
		};
		if (!ConfigSchema::ValidateSection(
			config, "command_line", commandLineRules, error))
			return false;

		const std::vector<ConfigSchema::KeyRule> recoveryRules = {
			ConfigSchema::Integer("reset_after_render_restart_seconds", 1, INT_MAX),
			ConfigSchema::Integer("reset_queue_too_large_percent", 1, 100)
		};
		if (!ConfigSchema::ValidateSection(
			config, "queue_recovery", recoveryRules, error))
			return false;

		const std::vector<ConfigSchema::KeyRule> lldvRules = {
			ConfigSchema::NumberAtLeast("max_cll", 0.0),
			ConfigSchema::NumberAtLeast("max_fall", 0.0),
			ConfigSchema::NumberAtLeast("mastering_min_luminance", 0.0),
			ConfigSchema::NumberAtLeast("mastering_max_luminance", 0.0, false)
		};
		return ConfigSchema::ValidateSection(config, "lldv", lldvRules, error);
	}
}
