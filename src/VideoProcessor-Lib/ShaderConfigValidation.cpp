/* Copyright(C) 2026 Bill Slack. GPLv3. */

#include "ShaderConfigValidation.h"

#include <ConfigFile.h>
#include <RendererProfileConfig.h>

#include <climits>
#include <cmath>
#include <stdexcept>

namespace
{
	bool Bounded(const std::string& raw, double low, double high)
	{
		try
		{
			size_t used = 0;
			const std::string text = ConfigFile::Trim(raw);
			const double value = std::stod(text, &used);
			return used == text.size() && std::isfinite(value) &&
				value >= low && value <= high;
		}
		catch (...) { return false; }
	}

	bool ValidNlsValue(const std::string& name, const std::string& raw)
	{
		const std::string value = ConfigFile::NormalizeName(raw);
		if (name == "geometry") return value == "classic" ||
			value == "protected" || value == "center protected" ||
			value == "center_protected" || value == "centerprotected";
		if (name == "quality") return value == "low" || value == "medium" ||
			value == "high" || value == "very high" || value == "very_high" ||
			value == "veryhigh";
		if (name == "strength") return Bounded(raw, 0.0, 1.0);
		if (name == "center_protection") return Bounded(raw, 0.0, 0.45);
		if (name == "horizontal_center_protection" ||
			name == "vertical_center_protection") return Bounded(raw, 0.0, 0.45);
		if (name == "curve") return Bounded(raw, 0.5, 4.0);
		if (name == "axis_balance") return Bounded(raw, 0.0, 1.0);
		if (name == "max_center_zoom") return Bounded(raw, 1.0, 1.25);
		return false;
	}

	bool ValidShortcut(const ConfigFile& config, const std::string& section,
		std::string& reason)
	{
		std::string when, shortcut;
		config.TryGetString(section, "when", when);
		config.TryGetString(section, "shortcut", shortcut);
		return RendererProfileConfig::MergeShortcutIntoWhen(
			shortcut, "[" + section + "]", when, reason);
	}
}

bool ShaderConfigValidation::Validate(const ConfigFile& config,
	std::string& reason)
{
	reason.clear();
	const std::vector<std::string> sections = config.GetSectionNames();
	bool target = false;
	for (const std::string& section : sections)
		if (section.rfind("shader.", 0) == 0) target = true;
	if (!target) return true;

	for (const std::string& section : sections)
	{
		if (section.rfind("shader.", 0) != 0) continue;
		const std::string tail = section.substr(7);
		if (tail.empty()) continue;
		const bool member = tail.find('.') != std::string::npos;
		if (!member)
		{
			std::string type;
			bool multi = false;
			if (config.TryGetString(section, "type", type))
			{
				type = ConfigFile::NormalizeName(type);
				if (type == "multi") multi = true;
				else if (type != "single")
				{
					reason = "[" + section + "] type must be single or multi";
					return false;
				}
			}
			if (!ValidShortcut(config, section, reason)) return false;
			const auto* root = config.GetSectionValues(section);
			const bool effect = root && (root->find("shader_type") != root->end() ||
				root->find("hlsl_file") != root->end() ||
				root->find("glsl_file") != root->end());
			bool children = false;
			for (const std::string& child : sections)
			{
				const std::string prefix = section + ".";
				if (child.rfind(prefix, 0) != 0) continue;
				const std::string name = child.substr(prefix.size());
				if (name.empty() || name.find('.') != std::string::npos)
				{
					reason = "[" + child + "] must be exactly one shader member deep";
					return false;
				}
				children = true;
				if (!ValidShortcut(config, child, reason)) return false;
			}
			if (effect && children)
			{
				reason = "[" + section + "] cannot define an effect and child members";
				return false;
			}
			if (multi && effect)
			{
				reason = "[" + section + "] type=multi requires child members";
				return false;
			}
		}

		const auto* values = config.GetSectionValues(section);
		if (!values) continue;
		if (member && values->find("type") != values->end())
		{
			reason = "[" + section + "] type is valid only on the shader group";
			return false;
		}
		const bool rootEffect = values->find("shader_type") != values->end() ||
			values->find("hlsl_file") != values->end() ||
			values->find("glsl_file") != values->end();
		if (!member && !rootEffect) continue;
		std::string rawType;
		if (!config.TryGetString(section, "shader_type", rawType))
		{
			reason = "[" + section + "] requires shader_type";
			return false;
		}
		const std::string shaderType = ConfigFile::NormalizeName(rawType);
		if (shaderType != "nls" && shaderType != "custom")
		{
			reason = "[" + section + "] shader_type must be nls or custom";
			return false;
		}
		if (shaderType == "nls")
		{
			for (const char* rawName :
				{ "strength", "geometry", "center_protection", "curve", "quality",
				  "axis_balance", "max_center_zoom",
				  "horizontal_center_protection",
				  "vertical_center_protection" })
			{
				const std::string name(rawName), aliasName = "param_" + name;
				const auto typed = values->find(name), alias = values->find(aliasName);
				if (typed != values->end() && alias != values->end())
				{
					reason = "[" + section + "] cannot define both " + name +
						" and " + aliasName;
					return false;
				}
				const auto selected = typed != values->end() ? typed : alias;
				if (selected != values->end() && !ValidNlsValue(name, selected->second))
				{
					reason = "[" + section + "] has invalid NLS " + name +
						" value '" + selected->second + "'";
					return false;
				}
			}
			std::string tolerance;
			if (config.TryGetString(section, "tolerance_percent", tolerance) &&
				!Bounded(tolerance, 0.0, 50.0))
			{
				reason = "[" + section + "] tolerance_percent must be from 0 through 50";
				return false;
			}
			std::string direction;
			if (config.TryGetString(section, "aspect_direction", direction))
			{
				direction = ConfigFile::NormalizeName(direction);
				if (direction != "narrower_only" && direction != "wider_only" &&
					direction != "any")
				{
					reason = "[" + section + "] aspect_direction must be narrower_only, wider_only, or any";
					return false;
				}
			}
			std::string cropPercent;
			if (config.TryGetString(section, "vprenderer_max_crop_percent",
				cropPercent) && !Bounded(cropPercent, 0.0, 10.0))
			{
				reason = "[" + section + "] vprenderer_max_crop_percent must be from 0 through 10";
				return false;
			}
			std::string cropPreference;
			if (config.TryGetString(section, "vprenderer_crop_preference",
				cropPreference))
			{
				cropPreference = ConfigFile::NormalizeName(cropPreference);
				if (cropPreference != "preserve_image" &&
					cropPreference != "minimize_distortion")
				{
					reason = "[" + section + "] vprenderer_crop_preference must be preserve_image or minimize_distortion";
					return false;
				}
			}
		}
		std::string order;
		if (config.TryGetString(section, "order", order))
		{
			try
			{
				size_t used = 0;
				const std::string text = ConfigFile::Trim(order);
				const long parsed = std::stol(text, &used);
				if (used != text.size() || parsed < 0 || parsed > INT_MAX)
					throw std::out_of_range("order");
			}
			catch (...)
			{
				reason = "[" + section + "] order must be a non-negative integer";
				return false;
			}
		}
		std::string stage;
		if (config.TryGetString(section, "stage", stage))
		{
			stage = ConfigFile::NormalizeName(stage);
			if (stage != "pre_resize" && stage != "pre" &&
				stage != "post_resize" && stage != "post")
			{
				reason = "[" + section + "] stage must be pre_resize or post_resize";
				return false;
			}
		}
	}
	return true;
}
