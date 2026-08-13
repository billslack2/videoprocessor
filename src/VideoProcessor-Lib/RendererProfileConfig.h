#pragma once

#include "ConfigFile.h"
#include "ConfigSchema.h"
#include "MainConfigSchema.h"
#include "RendererConfigView.h"
#include "DisplayRuleExpression.h"
#include "AspectRatio.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Parsed representation of the VP-0028 renderer-profile configuration.  This
// deliberately has no renderer, GUI, or Windows dependency so startup
// validation and selection can be tested before a renderer is created.
namespace RendererProfileConfig
{
	// Subtitle analysis is intentionally sampled every third rendered frame and
	// may take up to roughly 125 ms to reaffirm a cue. A shorter hold is retained
	// safely by the renderer for compatibility, but is too easy to misconfigure
	// and makes cue release needlessly sensitive to individual samples.
	constexpr double MIN_SUBTITLE_HOLD_SECONDS = 0.25;
	constexpr double MAX_SUBTITLE_HOLD_SECONDS = 30.0;
	constexpr int DEFAULT_SUBTITLE_TARGET_BUFFER_PIXELS = 10;
	constexpr int MAX_SUBTITLE_TARGET_BUFFER_PIXELS = 50;

	inline bool OwnsSection(const std::string& section)
	{
		return RendererConfigView::OwnsSection(section) ||
			section == "general" ||
			section == "profile_groups" ||
			section == "profiles.input" ||
			section == "profiles.scaling" ||
			section == "profiles.display" ||
			section == "profiles.viewport" ||
			section == "event_actions" ||
			section == "display_rules" ||
			section == "refresh_rate_commands" ||
			section.rfind("profile_groups.", 0) == 0 ||
			section.rfind("profiles.", 0) == 0 ||
			section.rfind("event_actions.", 0) == 0 ||
			section.rfind("actions.", 0) == 0 ||
			section.rfind("display_rules.", 0) == 0;
	}

	inline std::string StatePath(const ConfigFile& config)
	{
		std::string path = config.GetLoadedPath();
		if (path.empty())
			path = ConfigFile::DEFAULT_FILENAME;
		const size_t separator = path.find_last_of("\\/");
		const size_t extension = path.find_last_of('.');
		if (extension != std::string::npos &&
			(separator == std::string::npos || extension > separator))
			path.resize(extension);
		return path + ".state";
	}

	struct Profile
	{
		std::string group;
		std::string name;
		std::string when;
		DisplayRuleExpression::Expression whenExpression;
		int priority = 0;
		std::map<std::string, std::string> settings;
	};

	struct Group
	{
		std::string name;
		std::vector<std::string> profiles;
		std::string defaultSelection;
		std::string resetWhen;
		DisplayRuleExpression::Expression resetExpression;
		bool persistSelection = true;
	};

	struct Model
	{
		bool persistSelection = true;
		std::vector<std::string> warnings;
		std::vector<Group> groups;
		std::map<std::string, Profile> profiles;
		int eventActionDelaySeconds = 5;
		struct EventAction
		{
			std::string name;
			std::vector<std::string> events;
			std::string when;
			DisplayRuleExpression::Expression whenExpression;
			std::string program;
			std::string arguments;
			std::string workingDirectory;
			// The built-in renderer is identified by backend. A named target is
			// resolved through [renderer_alias] to this one-based UI selector
			// index; zero is reserved for the built-in and wildcard targets.
			std::string renderer = "vprenderer";
			int rendererSelectorIndex = 0;
			int delaySeconds = 5;
		};
		std::vector<EventAction> actions;
	};

	struct KeySelection
	{
		std::string group;
		std::string profile;
		bool resetToAutomatic = false;
	};

	struct AutomaticSelection
	{
		std::string group;
		std::string profile;
		bool configuredDefault = false;
	};

	struct ResolvedViewport
	{
		std::string group = "viewport";
		std::string profile = "default";
		AspectRatio screenAspect{ 16, 9, 16.0 / 9.0 };
		bool hasScreenAspect = false;
		std::string verticalAlignment = "center";
		AspectRatio anamorphicScale{ 1, 1, 1.0 };
		bool automaticCrop = false;
		bool subtitleFit = false;
		uint64_t subtitleHoldMilliseconds = 2000;
		uint64_t subtitleEngageDriftMilliseconds = 0;
		uint64_t subtitleReleaseDriftMilliseconds = 0;
		int subtitlePaddingPixels = 20;
		int subtitleTargetBufferPixels =
			DEFAULT_SUBTITLE_TARGET_BUFFER_PIXELS;
		uint64_t generation = 0;
	};

	// Queue is application-owned, unlike the renderer profile groups.  It is
	// nevertheless parsed with the same strict profile grammar and key-chord
	// expressions so selections compose across groups.
	struct ResolvedQueue
	{
		std::string group = "queue";
		std::string profile;
		bool hasQueueSize = false;
		size_t queueSize = 0;
		bool hasLeadFrames = false;
		size_t leadFrames = 0;
		bool hasTargetFrames = false;
		size_t targetFrames = 0;
		bool hasActivePictureLookaheadFrames = false;
		size_t activePictureLookaheadFrames = 0;
		bool hasStartupPrerollFrames = false;
		size_t startupPrerollFrames = 0;
		bool hasResetAfterRendererRestartSeconds = false;
		int resetAfterRendererRestartSeconds = 0;
		bool hasResetQueueTooLargePercent = false;
		int resetQueueTooLargePercent = 0;
	};

	// The source path reports LLDV metadata only when it synthesizes HDR data.
	// These are VP's established fallbacks for that synthesis, centralized so
	// configuration resolution and the dialog never drift between the legacy
	// and opt-in detection modes.
	struct LldvMetadata
	{
		double maxCll = 1000.0;
		double maxFall = 1000.0;
		double masteringMinLuminance = 0.0001;
		double masteringMaxLuminance = 1000.0;
	};

	inline LldvMetadata DefaultLldvMetadata(bool useNewLldvHeuristic)
	{
		if (useNewLldvHeuristic)
			return { 1000.0, 401.0, 0.001, 4000.0 };
		return {};
	}

	// LLDV is application-owned metadata policy, but it follows the same
	// ordered-profile selection model as queue. Values are optional so a
	// profile can inherit a baseline field or preserve VP's mode-specific
	// effective default when no profile field supplies it.
	struct ResolvedLldv
	{
		std::string group = "lldv";
		std::string profile;
		bool hasMaxCll = false;
		double maxCll = 0.0;
		bool hasMaxFall = false;
		double maxFall = 0.0;
		bool hasMasteringMinLuminance = false;
		double masteringMinLuminance = 0.0;
		bool hasMasteringMaxLuminance = false;
		double masteringMaxLuminance = 0.0;

		LldvMetadata EffectiveMetadata(bool useNewLldvHeuristic) const
		{
			LldvMetadata effective = DefaultLldvMetadata(
				useNewLldvHeuristic);
			if (hasMaxCll) effective.maxCll = maxCll;
			if (hasMaxFall) effective.maxFall = maxFall;
			if (hasMasteringMinLuminance)
				effective.masteringMinLuminance = masteringMinLuminance;
			if (hasMasteringMaxLuminance)
				effective.masteringMaxLuminance = masteringMaxLuminance;
			return effective;
		}
	};

	inline bool ValidateExpressionVariables(
		const DisplayRuleExpression::Expression& expression,
		const std::set<std::string>& allowed, const std::string& context,
		std::string& error)
	{
		for (const std::string& variable : expression.Variables())
			if (allowed.find(variable) == allowed.end())
			{
				error = context + " cannot use variable '$" + variable + "'";
				return false;
			}
		return true;
	}

	inline bool IsUnified(const ConfigFile& config)
	{
		if (config.HasSection(RendererConfigView::VPRENDERER_SECTION))
			return true;
		if (config.HasSection("profile_groups") || config.HasSection("general") ||
			config.HasSection("event_actions") || config.HasSection("profiles.input") ||
			config.HasSection("profiles.scaling") || config.HasSection("profiles.display") ||
			config.HasSection("profiles.viewport"))
			return true;
		for (const std::string& section : config.GetSectionNames())
			if (section.rfind("profile_groups.", 0) == 0 ||
				section.rfind("profiles.", 0) == 0 ||
				section.rfind("event_actions.", 0) == 0)
				return true;
		return false;
	}

	inline std::vector<std::string> SplitNames(const std::string& text)
	{
		std::vector<std::string> names;
		std::istringstream input(text);
		std::string name;
		while (std::getline(input, name, ','))
		{
			name = ConfigFile::NormalizeName(name);
			if (!name.empty()) names.push_back(name);
		}
		return names;
	}

	inline bool ParseInteger(const std::string& text, int minimum, int maximum, int& value)
	{
		try
		{
			const std::string trimmed = ConfigFile::Trim(text);
			size_t consumed = 0;
			const long parsed = std::stol(trimmed, &consumed);
			if (consumed != trimmed.size() || parsed < minimum || parsed > maximum) return false;
			value = static_cast<int>(parsed);
			return true;
		}
		catch (const std::exception&) { return false; }
	}

	inline bool IsIdentifier(const std::string& value)
	{
		if (value.empty() || value.size() > 64 ||
			!std::isalpha(static_cast<unsigned char>(value.front()))) return false;
		return std::all_of(value.begin() + 1, value.end(), [](unsigned char c)
			{ return std::isalnum(c) || c == '_' || c == '-'; });
	}

	// Viewport section identifiers are intentionally machine-facing.  The
	// operator-facing label is separate metadata so it may contain spaces and
	// never becomes part of a runtime profile selection.
	inline bool IsReservedViewportIdentifier(const std::string& value)
	{
		// ResolveViewport("default") deliberately means the built-in fallback,
		// so accepting a section with this name would make it impossible to
		// resolve unambiguously.
		return ConfigFile::NormalizeName(value) == "default";
	}

	inline bool IsChoice(const std::string& value,
		std::initializer_list<const char*> choices)
	{
		const std::string normalized = ConfigFile::NormalizeName(value);
		for (const char* choice : choices)
			if (normalized == choice) return true;
		return false;
	}

	inline bool IsBoolean(const std::string& value)
	{
		return IsChoice(value, { "1", "0", "true", "false", "yes", "no", "on", "off" });
	}

	inline bool ParseBoolean(const std::string& text, bool& value)
	{
		const std::string normalized = ConfigFile::NormalizeName(text);
		if (normalized == "1" || normalized == "true" ||
			normalized == "yes" || normalized == "on")
		{
			value = true;
			return true;
		}
		if (normalized == "0" || normalized == "false" ||
			normalized == "no" || normalized == "off")
		{
			value = false;
			return true;
		}
		return false;
	}

	inline bool IsNumberInRange(const std::string& text, double minimum,
		double maximum, bool maximumInclusive = true)
	{
		double value = 0.0;
		if (!DisplayRuleExpression::ParseNumber(ConfigFile::Trim(text), value)) return false;
		return value >= minimum && (maximumInclusive ? value <= maximum : value < maximum);
	}

	inline bool IsAspectInRange(const std::string& text, double minimum, double maximum)
	{
		AspectRatio aspect;
		std::string error;
		return AspectRatioParser::Parse(
			text, minimum, maximum, aspect, error);
	}

	inline bool CanonicalizeKeyChord(const std::string& chord,
		std::string& canonical)
	{
		canonical.clear();
		std::string key;
		bool control = false;
		bool alt = false;
		bool shift = false;
		size_t start = 0;
		while (start <= chord.size())
		{
			const size_t end = chord.find('+', start);
			const std::string token = ConfigFile::Trim(chord.substr(start, end - start));
			if (token.empty()) return false;
			const std::string normalized = ConfigFile::NormalizeName(token);
			if (normalized == "ctrl" || normalized == "control") control = true;
			else if (normalized == "alt") alt = true;
			else if (normalized == "shift") shift = true;
			else
			{
				if (!key.empty()) return false;
				key = token;
			}
			if (end == std::string::npos) break;
			start = end + 1;
		}
		if (key.empty()) return false;
		const std::string normalized = ConfigFile::NormalizeName(key);
		std::string canonicalKey;
		if (normalized == "escape" || normalized == "esc") canonicalKey = "Esc";
		else if (normalized == "enter" || normalized == "return") canonicalKey = "Enter";
		else if (normalized.size() >= 2 && normalized[0] == 'f')
		{
			int number = 0;
			if (!ParseInteger(normalized.substr(1), 1, 24, number)) return false;
			canonicalKey = "F" + std::to_string(number);
		}
		else if (key.size() == 1 &&
			std::isalnum(static_cast<unsigned char>(key.front())) != 0)
		{
			canonicalKey.assign(1, static_cast<char>(
				std::toupper(static_cast<unsigned char>(key.front()))));
		}
		else return false;
		if (control) canonical += "Ctrl+";
		if (alt) canonical += "Alt+";
		if (shift) canonical += "Shift+";
		canonical += canonicalKey;
		return true;
	}

	inline bool IsRegistrableKeyChord(const std::string& chord)
	{
		std::string canonical;
		return CanonicalizeKeyChord(chord, canonical);
	}

	inline bool MergeShortcutIntoWhen(const std::string& shortcut,
		const std::string& owner, std::string& when, std::string& error)
	{
		const std::string chord = ConfigFile::Trim(shortcut);
		if (chord.empty()) return true;
		std::string canonicalChord;
		if (!CanonicalizeKeyChord(chord, canonicalChord))
		{
			error = owner + " shortcut must be one key with optional Ctrl, Alt, or Shift modifiers";
			return false;
		}
		std::string escaped;
		for (const char character : canonicalChord)
		{
			if (character == '\\' || character == '"') escaped.push_back('\\');
			escaped.push_back(character);
		}
		const std::string shortcutExpression = "${key}==\"" + escaped + "\"";
		when = ConfigFile::Trim(when);
		when = when.empty() ? shortcutExpression : "(" + when + ") || " + shortcutExpression;
		return true;
	}

	inline bool ValidateProfileSetting(const std::string& group, const std::string& key,
		const std::string& value, std::string& expected)
	{
		if (group == "input")
		{
			if (key == "tone_mapping") return IsChoice(value, { "auto", "spline", "bt2390", "st2094-40", "reinhard" });
			if (key == "gamut_mapping") return IsChoice(value, { "auto", "perceptual", "softclip", "relative", "desaturate" });
			if (key == "peak_detection") return IsChoice(value, { "auto", "off", "default", "high_quality", "on" });
			if (key == "contrast_recovery") return IsChoice(value, { "auto" }) || IsNumberInRange(value, 0.0, 1.0);
			if (key == "sdr_input_transfer") return IsChoice(value, { "auto", "bt1886", "srgb", "1.8", "2.0", "2.2", "2.4", "2.6", "2.8" });
			expected = "an input-owned setting"; return false;
		}
		if (group == "scaling")
		{
			if (key == "quality") return IsChoice(value, { "fast", "balanced", "high" });
			if (key == "upscaler") return IsChoice(value, { "auto", "ewa_lanczossharp", "ewa_lanczos", "bicubic", "bilinear" });
			if (key == "downscaler") return IsChoice(value, { "auto", "ewa_lanczos", "bicubic", "bilinear" });
			if (key == "sigmoid" || key == "dithering") return IsChoice(value, { "auto", "on", "off" });
			if (key == "deband_strength") return IsChoice(value, { "auto", "off", "light", "default" });
			expected = "a scaling-owned setting"; return false;
		}
		if (group == "display")
		{
			if (key == "output_path_profile") return IsChoice(value, { "legacy", "proposed", "custom" });
			if (key == "sdr_target_nits") return IsNumberInRange(value, 40.0, 500.0);
			if (key == "lut_reference_nits") return IsChoice(value, { "auto" }) || IsNumberInRange(value, 40.0, 500.0);
			if (key == "sdr_black_nits") return IsChoice(value, { "auto" }) || IsNumberInRange(value, 0.0, 500.0, false);
			if (key == "output_presentation") return IsChoice(value, { "auto", "composed", "direct" });
			if (key == "output_range" || key == "lut_reference_range") return IsChoice(value, { "auto", "full", "limited" });
			if (key == "output_gamma") return IsChoice(value, { "auto", "bt1886", "srgb", "1.8", "2.0", "2.2", "2.4", "2.6", "2.8" });
			if (key == "lut_reference_transfer") return IsChoice(value, { "auto", "srgb", "bt1886", "2.2", "2.4" });
			if (key == "sdr_target_primaries") return IsChoice(value, { "rec709", "bt2020" });
			if (key == "lut_reference_primaries") return IsChoice(value, { "auto", "rec709", "p3_d65", "bt2020" });
			if (key == "report_bt2020_to_display") return IsBoolean(value);
			if (key == "lut")
			{
				const std::string normalized = ConfigFile::NormalizeName(value);
				return normalized.empty() || (normalized.size() > 5 && normalized.substr(normalized.size() - 5) == ".cube");
			}
			expected = "a display-owned setting"; return false;
		}
		if (group == "viewport")
		{
			if (key == "screen_aspect")
				return IsAspectInRange(value, 1.0, 4.0);
			if (key == "vertical_alignment")
				return IsChoice(value, { "top", "center", "bottom" });
			if (key == "anamorphic_scale")
				return IsAspectInRange(value, 0.5, 2.0);
			if (key == "automatic_crop" || key == "subtitle_fit")
				return IsBoolean(value);
			if (key == "subtitle_hold_seconds")
				return IsNumberInRange(value, MIN_SUBTITLE_HOLD_SECONDS,
					MAX_SUBTITLE_HOLD_SECONDS);
			if (key == "subtitle_engage_drift_ms" ||
				key == "subtitle_release_drift_ms")
			{
				int parsed = 0; return ParseInteger(value, 0, 30000, parsed);
			}
			if (key == "subtitle_padding_pixels")
			{
				int parsed = 0; return ParseInteger(value, 0, 500, parsed);
			}
			if (key == "subtitle_target_buffer_pixels")
			{
				int parsed = 0; return ParseInteger(value, 0,
					MAX_SUBTITLE_TARGET_BUFFER_PIXELS, parsed);
			}
			expected = "a viewport-owned setting"; return false;
		}
		if (group == "queue")
		{
			int parsed = 0;
			if (key == "queue_size") return ParseInteger(value, 1, INT_MAX, parsed);
			if (key == "lead_frames" || key == "target_frames" ||
				key == "startup_preroll_frames" || key == "steady_reserve_frames")
				return ParseInteger(value, 0, 16, parsed);
			if (key == "active_picture_lookahead_frames")
				return ParseInteger(value, 0, 8, parsed);
			if (key == "reset_after_render_restart_seconds")
				return ParseInteger(value, 1, INT_MAX, parsed);
			if (key == "reset_queue_too_large_percent")
				return ParseInteger(value, 1, 200, parsed);
			expected = "a queue-owned setting"; return false;
		}
		if (group == "lldv")
		{
			if (key == "max_cll" || key == "max_fall" ||
				key == "mastering_min_luminance")
				return IsNumberInRange(value, 0.0,
					std::numeric_limits<double>::max());
			if (key == "mastering_max_luminance")
				return IsNumberInRange(value, 0.0,
					std::numeric_limits<double>::max(), false);
			expected = "an LLDV-owned setting"; return false;
		}
		expected = "a known setting"; return false;
	}

	inline bool ValidateBaseSetting(const std::string& key, const std::string& value)
	{
		std::string ignored;
		for (const char* group : { "input", "scaling", "display", "viewport" })
			if (ValidateProfileSetting(group, key, value, ignored)) return true;
		if (key == "switch_refresh_rate" || key == "output_diagnostics" ||
			key == "diagnostic_disable_shader_cache" ||
			key == "diagnostic_disable_compute" ||
			key == "diagnostic_force_8bit_sdr_swapchain" ||
			key == "diagnostic_allow_limited_g22" ||
			key == "diagnostic_allow_full_g22" ||
			key == "diagnostic_vp_owned_dxgi_presenter") return IsBoolean(value);
		if (key == "deband") return IsChoice(value, { "auto", "on", "off" });
		return false;
	}

	inline bool ValidateCanonicalDisplaySetting(
		const std::string& key, const std::string& value)
	{
		std::string ignored;
		for (const char* group : { "input", "scaling", "display" })
			if (ValidateProfileSetting(group, key, value, ignored)) return true;
		return key == "deband" &&
			IsChoice(value, { "auto", "on", "off" });
	}

	inline bool ValidateCanonicalRendererSections(
		const ConfigFile& config, std::string& error)
	{
		const std::vector<ConfigSchema::KeyRule> policyRules = {
			ConfigSchema::Boolean("switch_refresh_rate"),
			ConfigSchema::Boolean("output_diagnostics"),
			ConfigSchema::Boolean("diagnostic_disable_shader_cache"),
			ConfigSchema::Boolean("diagnostic_disable_compute"),
			ConfigSchema::Boolean("diagnostic_force_8bit_sdr_swapchain"),
			ConfigSchema::Boolean("diagnostic_allow_limited_g22"),
			ConfigSchema::Boolean("diagnostic_allow_full_g22"),
			ConfigSchema::Boolean("diagnostic_vp_owned_dxgi_presenter")
		};
		if (!ConfigSchema::ValidateSection(config,
			RendererConfigView::GENERAL_SECTION, policyRules, error))
			return false;

		if (const auto* display = config.GetSectionValues(
			RendererConfigView::DISPLAY_SECTION))
			for (const auto& value : *display)
				if (!ValidateCanonicalDisplaySetting(
					value.first, value.second))
				{
					error = "[" +
						std::string(RendererConfigView::DISPLAY_SECTION) +
						"] key '" + value.first +
						"' is not a valid built-in renderer base setting";
					return false;
				}
		if (const auto* renderer = config.GetSectionValues(
			RendererConfigView::VPRENDERER_SECTION))
			for (const auto& value : *renderer)
				if (value.first != "when" && value.first != "shortcut" &&
					!((RendererConfigView::IsPolicyKey(value.first) &&
						ValidateBaseSetting(value.first, value.second)) ||
						ValidateCanonicalDisplaySetting(value.first, value.second)))
				{
					error = "[vprenderer] key '" + value.first +
						"' is not a valid built-in renderer base setting";
					return false;
				}
		return true;
	}

	// VP-0079 deliberately keeps the old resolver as the runtime engine, but
	// reads the concise owner/variant grammar into that engine's neutral model.
	// There is no profile registry and selections never survive a process exit.
	inline bool IsTargetModel(const ConfigFile& config)
	{
		if (config.HasSection(RendererConfigView::VPRENDERER_SECTION))
			return true;
		for (const std::string& section : config.GetSectionNames())
			if (section.rfind("vprenderer.", 0) == 0 ||
				section == "queue" || section.rfind("queue.", 0) == 0 ||
				section == "lldv" || section.rfind("lldv.", 0) == 0 ||
				section == "directshow" || section.rfind("directshow.", 0) == 0 ||
				section.rfind("shader.", 0) == 0 ||
				section.rfind("actions.", 0) == 0 ||
				section == "renderer_alias")
				return true;
		return false;
	}

	inline bool ValidateTargetRendererSetting(const std::string& key,
		const std::string& value)
	{
		// automatic_crop is viewport-owned. Reject it from renderer/display
		// variants so the same setting cannot acquire two owners.
		if (key == "automatic_crop") return false;
		return ValidateBaseSetting(key, value);
	}

	inline bool ParseTargetActionRun(const std::string& value,
		std::string& program, std::string& arguments)
	{
		const std::string run = ConfigFile::Trim(value);
		if (run.empty()) return false;
		const size_t separator = run.find_first_of(" \t");
		program = separator == std::string::npos ? run :
			run.substr(0, separator);
		arguments = separator == std::string::npos ? std::string() :
			ConfigFile::Trim(run.substr(separator + 1));
		const std::string normalized = ConfigFile::NormalizeName(program);
		return normalized.size() >= 4 &&
			(normalized.substr(normalized.size() - 4) == ".exe" ||
			 normalized.substr(normalized.size() - 4) == ".bat" ||
			 normalized.substr(normalized.size() - 4) == ".cmd");
	}

	inline bool ParseActionRenderer(const ConfigFile& config,
		const std::string& section, const std::string& value,
		Model::EventAction& action, std::string& error)
	{
		action.renderer = ConfigFile::NormalizeName(value);
		action.rendererSelectorIndex = 0;
		if (action.renderer == "vprenderer" || action.renderer == "*")
			return true;
		// A positive one-based renderer selector index is the canonical local
		// target used by the configuration editor. Aliases remain readable for
		// compatibility, but are not required for ordinary action configuration.
		if (ParseInteger(action.renderer, 1, INT_MAX,
			action.rendererSelectorIndex))
			return true;

		const auto* aliases = config.GetSectionValues("renderer_alias");
		if (!aliases)
		{
			error = "[" + section + "] renderer must be vprenderer, *, a positive renderer index, or a valid [renderer_alias] name";
			return false;
		}
		const auto alias = aliases->find(action.renderer);
		if (alias == aliases->end() ||
			!ParseInteger(alias->second, 1, INT_MAX,
			action.rendererSelectorIndex))
		{
			error = "[" + section + "] renderer must be vprenderer, *, a positive renderer index, or a valid [renderer_alias] name";
			return false;
		}
		return true;
	}

	inline bool IsActionSourceField(const std::string& field)
	{
		return field == "eotf" || field == "transfer" ||
			field == "colorspace" || field == "primaries" ||
			field == "format" || field == "resolution" ||
			field == "scan" ||
			field == "hdr_metadata" || field == "interlaced" ||
			field == "source_rate" || field == "cadence" ||
			field == "width" || field == "height";
	}

	inline bool IsActionProfileGroup(const std::string& group)
	{
		return group == "input" || group == "scaling" ||
			group == "display" || group == "viewport" || group == "queue" ||
			group == "lldv";
	}

	inline bool IsSupportedActionEvent(const std::string& event)
	{
		if (event == "refresh.applied" || event == "refresh.confirmed" ||
			event == "refresh.restored" || event == "state.committed" ||
			event == "profile.changed" || event == "renderer.ready")
			return true;
		const std::string changed = ".changed";
		const std::string source = "source.";
		if (event.size() > source.size() + changed.size() &&
			event.compare(0, source.size(), source) == 0 &&
			event.compare(event.size() - changed.size(), changed.size(), changed) == 0)
			return IsActionSourceField(event.substr(source.size(),
				event.size() - source.size() - changed.size()));
		const std::string profile = "profile.";
		if (event.size() > profile.size() + changed.size() &&
			event.compare(0, profile.size(), profile) == 0 &&
			event.compare(event.size() - changed.size(), changed.size(), changed) == 0)
			return IsActionProfileGroup(event.substr(profile.size(),
				event.size() - profile.size() - changed.size()));
		return false;
	}

	inline bool IsRefreshActionEvent(const std::string& event)
	{
		return event == "refresh.applied" || event == "refresh.confirmed" ||
			event == "refresh.restored";
	}

	inline bool IsActionProfileVariable(const std::string& variable,
		const std::string& prefix)
	{
		return variable.size() > prefix.size() &&
			variable.compare(0, prefix.size(), prefix) == 0 &&
			IsActionProfileGroup(variable.substr(prefix.size()));
	}

	inline bool IsActionSnapshotVariable(const std::string& variable)
	{
		if (variable == "event" || variable == "event_reason" ||
			variable == "viewport_profile" || variable == "screen_aspect" ||
			variable == "vertical_alignment" ||
			variable == "anamorphic_scale" || variable == "automatic_crop" ||
			variable == "subtitle_fit" || variable == "subtitle_hold_seconds" ||
			variable == "subtitle_engage_drift_ms" ||
			variable == "subtitle_release_drift_ms" ||
			variable == "subtitle_padding_pixels" ||
			variable == "subtitle_target_buffer_pixels" ||
			variable == "viewport_generation" || IsActionSourceField(variable))
			return true;
		return IsActionProfileVariable(variable, "profile.") ||
			IsActionProfileVariable(variable, "previous_profile.") ||
			(variable.size() > 9 && variable.compare(0, 9, "previous.") == 0 &&
				IsActionSourceField(variable.substr(9)));
	}

	inline bool IsActionVariableAvailableForEvents(
		const std::string& variable, const std::vector<std::string>& events)
	{
		for (const std::string& event : events)
		{
			const bool supported = IsRefreshActionEvent(event) ?
				(variable == "event" || variable == "event_reason" ||
					variable == "actual_refresh" ||
					variable == "requested_refresh" ||
					variable == "previous_refresh") :
				IsActionSnapshotVariable(variable);
			if (!supported)
				return false;
		}
		return true;
	}

	inline bool ValidateTargetActionExpression(
		const DisplayRuleExpression::Expression& expression,
		const std::vector<std::string>& events, const std::string& context,
		std::string& error)
	{
		for (const std::string& variable : expression.Variables())
			if (!IsActionVariableAvailableForEvents(variable, events))
			{
				error = context + " cannot use variable '$" + variable +
					"' with every event named by on=";
				return false;
			}
		return true;
	}

	inline bool ValidateActionArgumentTemplates(const std::string& arguments,
		const std::vector<std::string>& events, const std::string& context,
		std::string& error)
	{
		size_t cursor = 0;
		while ((cursor = arguments.find("${", cursor)) != std::string::npos)
		{
			const size_t close = arguments.find('}', cursor + 2);
			if (close == std::string::npos)
			{
				error = context + " has an unterminated ${variable} reference";
				return false;
			}
			const std::string variable = ConfigFile::NormalizeName(
				arguments.substr(cursor + 2, close - cursor - 2));
			if (variable.empty() ||
				!IsActionVariableAvailableForEvents(variable, events))
			{
				error = context + " cannot expand variable '${" + variable +
					"}' for every event named by on=";
				return false;
			}
			cursor = close + 1;
		}
		return true;
	}

	inline bool ReadTarget(const ConfigFile& config, Model& model,
		std::string& error)
	{
		model = {};
		model.persistSelection = false;
		error.clear();
		if (!config.GetWarnings().empty())
		{
			error = "VP-0079 configuration is not strict: " +
				config.GetWarnings().front();
			return false;
		}

		RendererConfigView rendererConfig(config);
		if (!rendererConfig.Validate(error, model.warnings) ||
			!ValidateCanonicalRendererSections(config, error))
			return false;

		for (const char* legacy : { "command_line", "profile_groups", "profiles",
			"event_actions", "shaders", "display_rules", "refresh_rate_commands",
			"vpvr.display", "vpvr.general", "display", "libplacebo" })
			if (config.HasSection(legacy))
			{
				error = "VP-0079 configuration cannot include legacy [" +
					std::string(legacy) + "]";
				return false;
			}

		struct GroupSpec
		{
			const char* name;
			const char* section;
			bool inheritRoot;
		};
		const GroupSpec specs[] = {
			{ "input", "vprenderer.input", true },
			{ "scaling", "vprenderer.scaling", true },
			{ "display", "vprenderer", false },
			{ "viewport", "vprenderer.viewport", true },
			{ "queue", "queue", true },
			{ "lldv", "lldv", true }
		};
		const std::set<std::string> expressionVariables = {
			"eotf", "transfer", "colorspace", "primaries", "format",
			"hdr_metadata", "interlaced", "scan", "source_rate", "cadence",
			"width", "height", "resolution", "key"
		};

		for (const GroupSpec& spec : specs)
		{
			const std::string section(spec.section);
			const auto* rootValues = config.GetSectionValues(section);
			const std::string prefix = section + ".";
			std::vector<std::string> variants;
			for (const std::string& candidate : config.GetSectionNames())
			{
				if (candidate.rfind(prefix, 0) != 0)
					continue;
				const std::string name = candidate.substr(prefix.size());
				// Nested roots owned by the built-in renderer are independent
				// groups, rather than display variants.
				if (std::string(spec.name) == "display" &&
					(name == "input" || name == "scaling" || name == "viewport" ||
					 name.rfind("input.", 0) == 0 ||
					 name.rfind("scaling.", 0) == 0 ||
					 name.rfind("viewport.", 0) == 0))
					continue;
				if (name.find('.') != std::string::npos || !IsIdentifier(name) ||
					(std::string(spec.name) == "viewport" &&
						IsReservedViewportIdentifier(name)))
				{
					error = "[" + candidate + "] must be exactly one named variant";
					return false;
				}
				variants.push_back(name);
			}
			if (!rootValues && variants.empty())
				continue;
			const bool namedBaseline = !rootValues && !variants.empty();
			const std::string baselineName = namedBaseline ? variants.front() : "base";
			const auto* baselineValues = namedBaseline ?
				config.GetSectionValues(prefix + baselineName) : rootValues;

			Group group;
			group.name = spec.name;
			group.defaultSelection = baselineName;
			group.persistSelection = false;
			group.profiles.push_back(baselineName);
			Profile base;
			base.group = group.name;
			base.name = baselineName;
			std::string baseShortcut;
			std::string resetShortcut;
			if (baselineValues)
				for (const auto& entry : *baselineValues)
				{
					// label is configuration-editor metadata.  It is intentionally
					// accepted only for viewport sections and never participates in
					// profile inheritance, runtime resolution, or action variables.
					if (std::string(spec.name) == "viewport" && entry.first == "label")
						continue;
					// screen_aspect is the single source of truth for the physical
					// screen shape. `mode` was a legacy normal/scope label and has
					// never changed the runtime geometry, so retain compatibility
					// without letting it masquerade as an applied profile setting.
					if (std::string(spec.name) == "viewport" && entry.first == "mode")
					{
						std::string expected;
						if (!ValidateProfileSetting("viewport", entry.first,
							entry.second, expected))
						{
							error = "[" + (namedBaseline ? prefix + baselineName : section) +
								"] key 'mode' must be normal or scope";
							return false;
						}
						model.warnings.push_back(
							"[" + (namedBaseline ? prefix + baselineName : section) +
							"] key 'mode' is deprecated and ignored; use screen_aspect");
						continue;
					}
					if (entry.first == "when")
					{
						if (namedBaseline)
							base.when = entry.second;
						else
							group.resetWhen = entry.second;
						continue;
					}
					if (entry.first == "shortcut")
					{
						if (namedBaseline) baseShortcut = entry.second;
						else resetShortcut = entry.second;
						continue;
					}
					if (std::string(spec.name) == "queue")
					{
						std::string expected;
					if (!ValidateProfileSetting("queue", entry.first,
						entry.second, expected))
						{
							error = "[" + section + "] key '" + entry.first +
								"' is not a valid queue setting";
							return false;
						}
					}
					else if (std::string(spec.name) == "viewport")
					{
						std::string expected;
						if (!ValidateProfileSetting("viewport", entry.first,
							entry.second, expected))
						{
							error = "[" + (namedBaseline ? prefix + baselineName : section) +
								"] key '" + entry.first + "' value '" + entry.second +
								"' is not a valid viewport setting";
							return false;
						}
					}
					else if (std::string(spec.name) == "display")
					{
						if (!RendererConfigView::IsPolicyKey(entry.first) &&
							!ValidateTargetRendererSetting(entry.first, entry.second))
						{
							error = "[" + section + "] key '" + entry.first +
								"' is not a valid built-in renderer setting";
							return false;
						}
					}
					else
					{
						std::string expected;
						if (!ValidateProfileSetting(spec.name, entry.first,
							entry.second, expected))
						{
							error = "[" + section + "] key '" + entry.first +
								"' is not a valid " + spec.name + " setting";
							return false;
						}
					}
					if (spec.inheritRoot || namedBaseline)
						base.settings.emplace(entry.first, entry.second);
				}
			if (!MergeShortcutIntoWhen(resetShortcut, "[" + section + "]", group.resetWhen, error) ||
				!MergeShortcutIntoWhen(baseShortcut, "[" + prefix + baselineName + "]", base.when, error))
				return false;
			if (!group.resetWhen.empty() &&
				(!group.resetExpression.Compile(group.resetWhen, error, true) ||
				 !ValidateExpressionVariables(group.resetExpression, { "key" },
					"[" + section + "] when=", error)))
				return false;
			if (!base.when.empty() &&
				(!base.whenExpression.Compile(base.when, error, true) ||
				 !ValidateExpressionVariables(base.whenExpression,
					expressionVariables, "[" + prefix + baselineName + "] when=", error)))
				return false;
			model.profiles.emplace(group.name + "." + baselineName, base);

			for (const std::string& variant : variants)
			{
				if (namedBaseline && variant == baselineName)
					continue;
				const std::string variantSection = prefix + variant;
				const auto* values = config.GetSectionValues(variantSection);
				Profile profile = base;
				profile.name = variant;
				profile.when.clear();
				profile.whenExpression = {};
				profile.priority = 0;
				std::string profileShortcut;
				for (const auto& entry : *values)
				{
					if (std::string(spec.name) == "viewport" && entry.first == "label")
						continue;
					if (std::string(spec.name) == "viewport" && entry.first == "mode")
					{
						std::string expected;
						if (!ValidateProfileSetting("viewport", entry.first,
							entry.second, expected))
						{
							error = "[" + variantSection +
								"] key 'mode' must be normal or scope";
							return false;
						}
						model.warnings.push_back("[" + variantSection +
							"] key 'mode' is deprecated and ignored; use screen_aspect");
						continue;
					}
					if (entry.first == "when") { profile.when = entry.second; continue; }
					if (entry.first == "shortcut") { profileShortcut = entry.second; continue; }
					if (entry.first == "priority")
					{
						if (!ParseInteger(entry.second, -100000, 100000, profile.priority))
						{
							error = "[" + variantSection + "] priority must be an integer";
							return false;
						}
						model.warnings.push_back("[" + variantSection +
							"] priority is deprecated and ignored; file order is priority");
						continue;
					}
					std::string expected;
					const bool valid = std::string(spec.name) == "display" ?
						((RendererConfigView::IsPolicyKey(entry.first) &&
							ValidateBaseSetting(entry.first, entry.second)) ||
							ValidateTargetRendererSetting(entry.first, entry.second)) :
						ValidateProfileSetting(spec.name, entry.first,
							entry.second, expected);
					if (!valid)
					{
						error = "[" + variantSection + "] key '" + entry.first +
							"' value '" + entry.second + "' is not valid for " + spec.name;
						return false;
					}
					profile.settings[entry.first] = entry.second;
				}
				if (!MergeShortcutIntoWhen(profileShortcut, "[" + variantSection + "]", profile.when, error))
					return false;
				if (!profile.when.empty() &&
					(!profile.whenExpression.Compile(profile.when, error, true) ||
					 !ValidateExpressionVariables(profile.whenExpression,
						expressionVariables, "[" + variantSection + "] when=", error)))
					return false;
				group.profiles.push_back(variant);
				model.profiles.emplace(group.name + "." + variant,
					std::move(profile));
			}
			model.groups.push_back(std::move(group));
		}

		for (const std::string& section : config.GetSectionNames())
		{
			if (section.rfind("actions.", 0) != 0)
				continue;
			const std::string name = section.substr(8);
			if (!IsIdentifier(name))
			{
				error = "[" + section + "] has an invalid action name";
				return false;
			}
			const auto* values = config.GetSectionValues(section);
			Model::EventAction action;
			action.name = name;
			bool enabled = true;
			std::string enabledText;
			if (config.TryGetString(section, "enabled", enabledText) &&
				!ParseBoolean(enabledText, enabled))
			{
				error = "[" + section + "] enabled must be true or false";
				return false;
			}
			for (const auto& entry : *values)
				if (entry.first != "enabled" && entry.first != "on" &&
					entry.first != "when" && entry.first != "run" &&
					entry.first != "renderer")
				{
					error = "[" + section + "] unknown key '" + entry.first + "'";
					return false;
				}
			// Disabled actions are intentional drafts. Preserve their text in
			// ConfigFile, but do not require or compile executable settings and
			// do not publish them to either runtime dispatcher.
			if (!enabled)
				continue;
			std::string events;
			if (!config.TryGetString(section, "on", events) ||
				(action.events = SplitNames(events)).empty())
			{
				error = "[" + section + "] requires on="; return false;
			}
			for (const std::string& event : action.events)
				if (!IsSupportedActionEvent(event))
				{
					error = "[" + section + "] unsupported event '" + event + "'";
					return false;
				}
			if (config.TryGetString(section, "when", action.when) &&
				!action.when.empty() &&
				(!action.whenExpression.Compile(action.when, error, true) ||
				 !ValidateTargetActionExpression(action.whenExpression,
					action.events, "[" + section + "] when=", error)))
			{
				return false;
			}
			std::string run;
			if (!config.TryGetString(section, "run", run) ||
				!ParseTargetActionRun(run, action.program, action.arguments))
			{
				error = "[" + section + "] run= must begin with an .exe, .bat, or .cmd path";
				return false;
			}
			if (!ValidateActionArgumentTemplates(action.arguments, action.events,
				"[" + section + "] run=", error))
				return false;
			std::string renderer;
			if (config.TryGetString(section, "renderer", renderer) &&
				!ParseActionRenderer(config, section, renderer, action, error))
				return false;
			model.actions.push_back(std::move(action));
		}

		for (const std::string& section : config.GetSectionNames())
			if (!MainConfigSchema::OwnsSection(section) &&
				!RendererConfigView::OwnsSection(section) &&
				section.rfind("actions.", 0) != 0 &&
				section.rfind("shader.", 0) != 0)
			{
				error = "VP-0079 configuration has unknown section [" + section + "]";
				return false;
			}
		return true;
	}

	inline bool Read(const ConfigFile& config, Model& model, std::string& error)
	{
		if (IsTargetModel(config))
			return ReadTarget(config, model, error);
		model = {};
		error.clear();
		RendererConfigView rendererConfig(config);
		if (!rendererConfig.Validate(error, model.warnings) ||
			!ValidateCanonicalRendererSections(config, error))
			return false;
		if (!IsUnified(config))
		{
			if (config.HasSection("display_rules"))
				model.warnings.push_back(
					"[display_rules] is legacy-only; migrate to "
					"[profile_groups.display] and [profiles.display.*]");
			return true; // Legacy remains a separate, unchanged compatibility path.
		}

		if (!config.GetWarnings().empty())
		{
			error = "unified renderer configuration is not strict: " + config.GetWarnings().front();
			return false;
		}

		for (const char* legacySection : { "display_rules", "refresh_rate_commands" })
			if (config.HasSection(legacySection))
			{
				error = "unified renderer configuration cannot include legacy [" +
					std::string(legacySection) + "]";
				return false;
			}
		if (const auto* shortcuts = config.GetSectionValues("shortcuts"))
			for (const auto& shortcut : *shortcuts)
				if (shortcut.first == "screen_profile_normal" ||
					shortcut.first == "screen_profile_scope" ||
					shortcut.first == "display_rules_auto")
				{
					error = "unified renderer configuration cannot include "
						"legacy [shortcuts] key '" + shortcut.first + "'";
					return false;
				}

		std::string value;
		const std::vector<ConfigSchema::KeyRule> generalRules = {
			ConfigSchema::Boolean("persist_profile_selection"),
			ConfigSchema::Boolean("switch_refresh_rate"),
			ConfigSchema::Integer("event_action_delay_seconds", 0, 30),
			ConfigSchema::Boolean("output_diagnostics"),
			ConfigSchema::Boolean("diagnostic_disable_shader_cache"),
			ConfigSchema::Boolean("diagnostic_disable_compute"),
			ConfigSchema::Boolean("diagnostic_force_8bit_sdr_swapchain"),
			ConfigSchema::Boolean("diagnostic_allow_limited_g22"),
			ConfigSchema::Boolean("diagnostic_allow_full_g22"),
			ConfigSchema::Boolean("diagnostic_vp_owned_dxgi_presenter")
		};
		if (!ConfigSchema::ValidateSection(config, "general", generalRules, error))
			return false;
		bool persist = true;
		config.TryGetBool("general", "persist_profile_selection", persist);
		model.persistSelection = persist;
		if (config.TryGetString("general", "event_action_delay_seconds", value))
			ParseInteger(value, 0, 30, model.eventActionDelaySeconds);
		for (const char* displaySection :
			{ RendererConfigView::LEGACY_DISPLAY_SECTION,
			  RendererConfigView::HISTORICAL_DISPLAY_SECTION })
		{
			const auto* display = config.GetSectionValues(displaySection);
			if (!display)
				continue;
			const std::set<std::string> baseKeys = {
				"sdr_target_nits", "sdr_black_nits", "switch_refresh_rate",
				"quality", "tone_mapping", "gamut_mapping", "peak_detection",
				"contrast_recovery", "upscaler", "downscaler", "deband",
				"deband_strength", "sigmoid", "dithering", "output_presentation",
				"output_range", "output_gamma", "output_path_profile",
				"sdr_target_primaries", "report_bt2020_to_display",
				"sdr_input_transfer", "output_diagnostics",
				"diagnostic_disable_shader_cache", "diagnostic_disable_compute",
				"diagnostic_force_8bit_sdr_swapchain",
				"diagnostic_allow_limited_g22",
				"diagnostic_allow_full_g22",
				"diagnostic_vp_owned_dxgi_presenter", "screen_aspect",
				"vertical_alignment",
				"automatic_crop", "subtitle_fit",
				"subtitle_hold_seconds", "subtitle_engage_drift_ms",
				"subtitle_release_drift_ms",
				"subtitle_padding_pixels", "subtitle_target_buffer_pixels"
			};
			std::vector<ConfigSchema::KeyRule> displayRules;
			for (const std::string& key : baseKeys)
				displayRules.push_back({
					key,
					[key](const std::string& value)
						{ return ValidateBaseSetting(key, value); },
					"a valid renderer value"
				});
			if (!ConfigSchema::ValidateSection(
				config, displaySection, displayRules, error))
				return false;
		}

		const std::vector<std::string> expectedGroups = { "input", "scaling", "display", "viewport", "queue" };
		for (const std::string& groupName : expectedGroups)
		{
			const std::string section = "profile_groups." + groupName;
			const std::string profilePrefix = "profiles." + groupName + ".";
			if (!config.HasSection(section))
			{
				for (const std::string& configured :
					config.GetSectionNames())
					if (configured.rfind(profilePrefix, 0) == 0)
					{
						error = "[" + configured +
							"] is orphaned because [" + section +
							"] is not declared";
						return false;
					}
				continue; // An undeclared group contributes no overrides.
			}
			std::string profileList;
			if (!config.TryGetString(section, "profiles", profileList))
			{
				error = "unified configuration requires " + section + " profiles=";
				return false;
			}

			Group group;
			group.name = groupName;
			group.profiles = SplitNames(profileList);
			if (group.profiles.empty())
			{
				error = section + " profiles= must not be empty";
				return false;
			}
			std::set<std::string> seen;
			for (const std::string& profileName : group.profiles)
			{
				if (!IsIdentifier(profileName) || profileName == "auto" || profileName == "none" ||
					profileName == "default")
				{
					error = section + " contains invalid or reserved profile identifier '" + profileName + "'";
					return false;
				}
				if (!seen.insert(profileName).second)
				{
					error = section + " profiles= contains duplicate '" + profileName + "'";
					return false;
				}
			}

			if (!config.TryGetString(section, "default", group.defaultSelection))
			{
				error = section + " requires default=";
				return false;
			}
			group.defaultSelection = ConfigFile::NormalizeName(group.defaultSelection);
			if (group.defaultSelection != "auto" && seen.find(group.defaultSelection) == seen.end())
			{
				error = section + " default= must be auto or a listed profile";
				return false;
			}
			config.TryGetString(section, "when", group.resetWhen);
			if (!group.resetWhen.empty())
			{
				if (!group.resetExpression.Compile(group.resetWhen, error, true) ||
					!ValidateExpressionVariables(group.resetExpression, { "key" },
						"[" + section + "] when=", error))
					return false;
			}
			group.persistSelection = model.persistSelection;
			if (config.TryGetString(section, "persist_profile_selection", value) &&
				!config.TryGetBool(section, "persist_profile_selection", group.persistSelection))
			{
				error = section + " persist_profile_selection must be true or false";
				return false;
			}
			if (const auto* groupValues = config.GetSectionValues(section))
				for (const auto& value : *groupValues)
					if (value.first != "profiles" && value.first != "default" &&
						value.first != "when" && value.first != "persist_profile_selection")
					{
						error = "[" + section + "] unknown key '" + value.first + "'";
						return false;
					}

			for (const std::string& profileName : group.profiles)
			{
				const std::string profileSection = "profiles." + groupName + "." + profileName;
				const auto* values = config.GetSectionValues(profileSection);
				if (values == nullptr)
				{
					error = section + " lists '" + profileName + "' but [" + profileSection + "] is missing";
					return false;
				}
				Profile profile;
				profile.group = groupName;
				profile.name = profileName;
				for (const auto& value : *values)
				{
					if (value.first == "when") profile.when = value.second;
					else if (value.first == "priority")
					{
						if (!ParseInteger(value.second, -100000, 100000, profile.priority))
						{
							error = "[" + profileSection + "] priority must be an integer from -100000 to 100000";
							return false;
						}
						model.warnings.push_back("[" + profileSection +
							"] priority is deprecated and ignored; declared profile order is priority");
					}
					else
					{
						std::string settingKey = value.first;
						std::string expected;
						if (!ValidateProfileSetting(groupName, settingKey, value.second, expected))
						{
							error = "[" + profileSection + "] key '" + settingKey +
								"' has invalid value '" + value.second + "'; expected " + expected;
							return false;
						}
						profile.settings.emplace(settingKey, value.second);
					}
				}
				if (!profile.when.empty() &&
					(!profile.whenExpression.Compile(profile.when, error, true) ||
					 !ValidateExpressionVariables(profile.whenExpression,
						{ "eotf", "transfer", "colorspace", "primaries", "format",
						  "hdr_metadata", "interlaced", "scan", "source_rate",
						  "cadence", "width", "height", "resolution", "key" },
						"[" + profileSection + "] when=", error)))
					return false;
				model.profiles.emplace(groupName + "." + profileName, std::move(profile));
			}
			std::map<std::string, std::string> chordOwners;
			auto registerChords = [&](const DisplayRuleExpression::Expression& expression,
				const std::string& owner) -> bool
			{
				for (const std::string& chord : expression.KeyChords())
				{
					if (!IsRegistrableKeyChord(chord))
					{
						error = owner + " uses unregistrable key chord '" + chord + "'";
						return false;
					}
					std::string canonical;
					if (!CanonicalizeKeyChord(chord, canonical))
					{
						error = owner + " uses unregistrable key chord '" + chord + "'";
						return false;
					}
					const auto existing = chordOwners.find(canonical);
					if (existing != chordOwners.end() && existing->second != owner)
					{
						error = "key chord '" + chord + "' selects both " +
							existing->second + " and " + owner + " in group '" +
							groupName + "'";
						return false;
					}
					chordOwners[canonical] = owner;
				}
				return true;
			};
			if (!group.resetWhen.empty() &&
				!registerChords(group.resetExpression, "[" + section + "] when="))
				return false;
			for (const std::string& profileName : group.profiles)
			{
				const Profile& profile = model.profiles.at(groupName + "." + profileName);
				if (!profile.when.empty() &&
					!registerChords(profile.whenExpression,
						"[profiles." + groupName + "." + profileName + "]"))
					return false;
			}
			model.groups.push_back(std::move(group));
		}

		std::set<std::string> expectedSections = { "display", "general", "event_actions" };
		for (const Group& group : model.groups)
		{
			expectedSections.insert("profile_groups." + group.name);
			for (const std::string& profile : group.profiles)
				expectedSections.insert("profiles." + group.name + "." + profile);
		}

		std::string actionList;
		if (config.TryGetString("event_actions", "actions", actionList))
		{
			const std::vector<std::string> names = SplitNames(actionList);
			std::set<std::string> seenActions;
			for (const std::string& name : names)
			{
				if (!IsIdentifier(name) || !seenActions.insert(name).second)
				{
					error = "[event_actions] contains invalid or duplicate action '" + name + "'";
					return false;
				}
				const std::string section = "event_actions." + name;
				expectedSections.insert(section);
				const auto* values = config.GetSectionValues(section);
				if (!values) { error = "[" + section + "] is missing"; return false; }
				Model::EventAction action;
				action.name = name;
				action.delaySeconds = model.eventActionDelaySeconds;
				bool enabled = true;
				std::string enabledText;
				if (config.TryGetString(section, "enabled", enabledText) &&
					!ParseBoolean(enabledText, enabled))
				{
					error = "[" + section + "] enabled must be true or false";
					return false;
				}
				for (const auto& value : *values)
					if (value.first != "enabled" && value.first != "on" &&
						value.first != "when" && value.first != "program" &&
						value.first != "arguments" && value.first != "working_directory" &&
						value.first != "delay_seconds" && value.first != "renderer")
					{
						error = "[" + section + "] unknown key '" + value.first + "'";
						return false;
					}
				if (!enabled)
					continue;
				std::string events;
				if (!config.TryGetString(section, "on", events) ||
					(action.events = SplitNames(events)).empty())
				{
					error = "[" + section + "] requires non-empty on="; return false;
				}
				for (const std::string& event : action.events)
					if (!IsSupportedActionEvent(event))
					{
						error = "[" + section + "] unsupported event '" + event + "'"; return false;
					}
				if (config.TryGetString(section, "when", action.when) &&
					!action.when.empty() &&
					(!action.whenExpression.Compile(action.when, error, true) ||
					 !ValidateTargetActionExpression(action.whenExpression,
						action.events, "[" + section + "] when=", error)))
				{
					return false;
				}
				if (!config.TryGetString(section, "program", action.program) ||
					ConfigFile::Trim(action.program).empty())
				{
					error = "[" + section + "] requires program="; return false;
				}
				const std::string program = ConfigFile::NormalizeName(action.program);
				if (program.size() < 4 ||
					(program.substr(program.size() - 4) != ".exe" &&
					 program.substr(program.size() - 4) != ".bat" &&
					 program.substr(program.size() - 4) != ".cmd"))
				{
					error = "[" + section + "] program must end in .exe, .bat, or .cmd"; return false;
				}
				config.TryGetString(section, "arguments", action.arguments);
				config.TryGetString(section, "working_directory", action.workingDirectory);
				if (!ValidateActionArgumentTemplates(action.arguments,
					action.events, "[" + section + "] arguments=", error))
					return false;
				std::string renderer;
				if (config.TryGetString(section, "renderer", renderer) &&
					!ParseActionRenderer(config, section, renderer, action, error))
					return false;
				std::string delay;
				if (config.TryGetString(section, "delay_seconds", delay) &&
					!ParseInteger(delay, 0, 30, action.delaySeconds))
				{
					error = "[" + section + "] delay_seconds must be a whole number from 0 to 30"; return false;
				}
				model.actions.push_back(std::move(action));
			}
		}
		if (const auto* eventValues = config.GetSectionValues("event_actions"))
			for (const auto& value : *eventValues)
				if (value.first != "actions")
				{
					error = "[event_actions] unknown key '" + value.first + "'"; return false;
				}
		for (const std::string& section : config.GetSectionNames())
			if (expectedSections.find(section) == expectedSections.end() &&
				!MainConfigSchema::OwnsSection(section) &&
				!RendererConfigView::OwnsSection(section))
			{
				error = "unified renderer configuration has unknown or orphan section [" + section + "]";
				return false;
			}
		return true;
	}

	// A physical key event is deliberately resolved across every independent
	// group.  Thus one chord may select, for example, both a display profile and
	// a viewport profile.  Ambiguity is only an error within a single group.
	inline bool SelectForKey(const Model& model, const std::string& key,
		const DisplayRuleExpression::ValueLookup& sourceValues,
		std::vector<KeySelection>& selections, std::string& error)
	{
		selections.clear();
		error.clear();
		std::string canonicalKey;
		if (!CanonicalizeKeyChord(key, canonicalKey))
		{
			error = "key '" + key + "' is not a registrable shortcut";
			return false;
		}
		auto matchingChord = [&](const DisplayRuleExpression::Expression& expression,
			std::string& declaredChord)
		{
			for (const std::string& chord : expression.KeyChords())
			{
				std::string canonical;
				if (CanonicalizeKeyChord(chord, canonical) && canonical == canonicalKey)
				{
					declaredChord = chord;
					return true;
				}
			}
			return false;
		};
		auto matches = [&](const DisplayRuleExpression::Expression& expression,
			const std::string& declaredChord, int& specificity)
		{
			const DisplayRuleExpression::ValueLookup values =
				[&](const std::string& name, std::string& value)
				{
					if (name == "key") { value = declaredChord; return true; }
					return sourceValues(name, value);
				};
			return expression.Matches(values, specificity, error);
		};

		for (const Group& group : model.groups)
		{
			if (!group.resetWhen.empty())
			{
				std::string declaredChord;
				if (matchingChord(group.resetExpression, declaredChord))
				{
					int specificity = 0;
					const bool matchesReset = matches(group.resetExpression,
						declaredChord, specificity);
					if (!matchesReset && !error.empty()) return false;
					if (matchesReset)
					{
						selections.push_back({ group.name,
							group.defaultSelection, false });
						continue;
					}
				}
			}

			std::string selected;
			for (const std::string& profileName : group.profiles)
			{
				const Profile& profile = model.profiles.at(group.name + "." + profileName);
				std::string declaredChord;
				if (!matchingChord(profile.whenExpression, declaredChord)) continue;
				int specificity = 0;
				const bool matchesProfile = matches(profile.whenExpression,
					declaredChord, specificity);
				if (!matchesProfile && !error.empty()) return false;
				if (!matchesProfile)
					continue;
				selected = profileName;
				break;
			}
			if (!selected.empty())
				selections.push_back({ group.name, selected, false });
		}
		return true;
	}

	inline bool ResolveViewport(const Model& model,
		const std::string& profileName, uint64_t generation,
		ResolvedViewport& viewport, std::string& error)
	{
		viewport = {};
		viewport.generation = generation;
		error.clear();
		if (profileName.empty() || profileName == "default")
			return true;

		const auto profile = model.profiles.find(
			"viewport." + ConfigFile::NormalizeName(profileName));
		if (profile == model.profiles.end())
		{
			error = "viewport profile '" + profileName + "' does not exist";
			return false;
		}
		viewport.profile = profile->second.name;
		const auto& settings = profile->second.settings;
		auto value = settings.find("screen_aspect");
		if (value != settings.end())
		{
			if (!AspectRatioParser::Parse(value->second, 1.0, 4.0,
				viewport.screenAspect, error))
			{
				error = "[profiles.viewport." + viewport.profile +
					"] screen_aspect: " + error;
				return false;
			}
			viewport.hasScreenAspect = true;
		}
		value = settings.find("vertical_alignment");
		if (value != settings.end())
		{
			const std::string alignment = ConfigFile::NormalizeName(value->second);
			if (!IsChoice(alignment, { "top", "center", "bottom" }))
			{
				error = "[profiles.viewport." + viewport.profile +
					"] vertical_alignment must be top, center, or bottom";
				return false;
			}
			viewport.verticalAlignment = alignment;
		}
		value = settings.find("anamorphic_scale");
		if (value != settings.end() &&
			!AspectRatioParser::Parse(value->second, 0.5, 2.0,
				viewport.anamorphicScale, error))
		{
			error = "[profiles.viewport." + viewport.profile +
				"] anamorphic_scale: " + error;
			return false;
		}
		value = settings.find("automatic_crop");
		if (value != settings.end() &&
			!ParseBoolean(value->second, viewport.automaticCrop))
		{
			error = "[profiles.viewport." + viewport.profile +
				"] automatic_crop is invalid";
			return false;
		}
		value = settings.find("subtitle_fit");
		if (value != settings.end() &&
			!ParseBoolean(value->second, viewport.subtitleFit))
		{
			error = "[profiles.viewport." + viewport.profile +
				"] subtitle_fit is invalid";
			return false;
		}
		value = settings.find("subtitle_hold_seconds");
		if (value != settings.end())
		{
			double seconds = 0.0;
			if (!DisplayRuleExpression::ParseNumber(
				ConfigFile::Trim(value->second), seconds) ||
				seconds < MIN_SUBTITLE_HOLD_SECONDS ||
				seconds > MAX_SUBTITLE_HOLD_SECONDS)
			{
				error = "[profiles.viewport." + viewport.profile +
					"] subtitle_hold_seconds is invalid";
				return false;
			}
			viewport.subtitleHoldMilliseconds =
				static_cast<uint64_t>(std::llround(seconds * 1000.0));
		}
		value = settings.find("subtitle_engage_drift_ms");
		if (value != settings.end())
		{
			int milliseconds = 0;
			if (!ParseInteger(value->second, 0, 30000, milliseconds))
			{
				error = "[profiles.viewport." + viewport.profile +
					"] subtitle_engage_drift_ms is invalid";
				return false;
			}
			viewport.subtitleEngageDriftMilliseconds =
				static_cast<uint64_t>(milliseconds);
		}
		value = settings.find("subtitle_release_drift_ms");
		if (value != settings.end())
		{
			int milliseconds = 0;
			if (!ParseInteger(value->second, 0, 30000, milliseconds))
			{
				error = "[profiles.viewport." + viewport.profile +
					"] subtitle_release_drift_ms is invalid";
				return false;
			}
			viewport.subtitleReleaseDriftMilliseconds =
				static_cast<uint64_t>(milliseconds);
		}
		value = settings.find("subtitle_padding_pixels");
		if (value != settings.end() &&
			!ParseInteger(value->second, 0, 500,
				viewport.subtitlePaddingPixels))
		{
			error = "[profiles.viewport." + viewport.profile +
				"] subtitle_padding_pixels is invalid";
			return false;
		}
		value = settings.find("subtitle_target_buffer_pixels");
		if (value != settings.end() &&
			!ParseInteger(value->second, 0,
				MAX_SUBTITLE_TARGET_BUFFER_PIXELS,
				viewport.subtitleTargetBufferPixels))
		{
			error = "[profiles.viewport." + viewport.profile +
				"] subtitle_target_buffer_pixels is invalid";
			return false;
		}
		return true;
	}

	inline bool ResolveQueue(const Model& model, const std::string& profileName,
		ResolvedQueue& queue, std::string& error)
	{
		queue = {};
		const auto profile = model.profiles.find("queue." + profileName);
		if (profile == model.profiles.end())
		{
			error = "queue profile '" + profileName + "' is not defined";
			return false;
		}
		queue.profile = profileName;
		const auto& settings = profile->second.settings;
		auto resolveSize = [&](const char* key, int minimum, int maximum,
			bool& configured, size_t& resolved) -> bool
		{
			const auto setting = settings.find(key);
			if (setting == settings.end()) return true;
			int value = 0;
			if (!ParseInteger(setting->second, minimum, maximum, value))
			{
				error = "[profiles.queue." + profileName + "] " + key + " is invalid";
				return false;
			}
			configured = true;
			resolved = static_cast<size_t>(value);
			return true;
		};
		auto resolveInteger = [&](const char* key, int minimum, int maximum,
			bool& configured, int& resolved) -> bool
		{
			const auto setting = settings.find(key);
			if (setting == settings.end()) return true;
			if (!ParseInteger(setting->second, minimum, maximum, resolved))
			{
				error = "[profiles.queue." + profileName + "] " + key + " is invalid";
				return false;
			}
			configured = true;
			return true;
		};
		if (!resolveSize("queue_size", 1, INT_MAX,
			queue.hasQueueSize, queue.queueSize) ||
			!resolveSize("lead_frames", 0, 16,
				queue.hasLeadFrames, queue.leadFrames) ||
			!resolveSize("target_frames", 0, 16,
				queue.hasTargetFrames, queue.targetFrames) ||
			!resolveSize("active_picture_lookahead_frames", 0, 8,
				queue.hasActivePictureLookaheadFrames,
				queue.activePictureLookaheadFrames) ||
			!resolveSize("startup_preroll_frames", 0, 16,
				queue.hasStartupPrerollFrames, queue.startupPrerollFrames) ||
			!resolveInteger("reset_after_render_restart_seconds", 1, INT_MAX,
				queue.hasResetAfterRendererRestartSeconds,
				queue.resetAfterRendererRestartSeconds) ||
			!resolveInteger("reset_queue_too_large_percent", 1, 200,
				queue.hasResetQueueTooLargePercent,
				queue.resetQueueTooLargePercent))
			return false;
		// steady_reserve_frames remains a read-compatible alias. Canonical
		// target_frames wins only after duplicate validation has rejected both.
		if (!queue.hasTargetFrames && !resolveSize("steady_reserve_frames", 0, 16,
			queue.hasTargetFrames, queue.targetFrames))
			return false;
		return true;
	}

	inline bool ResolveLldv(const Model& model, const std::string& profileName,
		ResolvedLldv& lldv, std::string& error)
	{
		lldv = {};
		error.clear();
		if (profileName.empty())
			return true;
		const auto profile = model.profiles.find(
			"lldv." + ConfigFile::NormalizeName(profileName));
		if (profile == model.profiles.end())
		{
			error = "LLDV profile '" + profileName + "' is not defined";
			return false;
		}
		lldv.profile = profile->second.name;
		const auto& settings = profile->second.settings;
		auto resolve = [&](const char* key, bool& configured,
			double& destination, bool strictlyPositive) -> bool
		{
			const auto value = settings.find(key);
			if (value == settings.end())
				return true;
			double parsed = 0.0;
			if (!DisplayRuleExpression::ParseNumber(
				ConfigFile::Trim(value->second), parsed) ||
				!std::isfinite(parsed) || parsed < 0.0 ||
				(strictlyPositive && parsed <= 0.0))
			{
				error = "[lldv." + lldv.profile + "] " + key +
					" is invalid";
				return false;
			}
			configured = true;
			destination = parsed;
			return true;
		};
		return resolve("max_cll", lldv.hasMaxCll, lldv.maxCll, false) &&
			resolve("max_fall", lldv.hasMaxFall, lldv.maxFall, false) &&
			resolve("mastering_min_luminance",
				lldv.hasMasteringMinLuminance,
				lldv.masteringMinLuminance, false) &&
			resolve("mastering_max_luminance",
				lldv.hasMasteringMaxLuminance,
				lldv.masteringMaxLuminance, true);
	}

	// Select every group from one immutable source snapshot. The first matching
	// non-default profile in declared/file order wins. If nothing matches, the
	// configured first/default profile is selected; default=auto intentionally
	// leaves that group without an override.
	inline bool SelectAutomatic(const Model& model,
		const DisplayRuleExpression::ValueLookup& sourceValues,
		std::vector<AutomaticSelection>& selections, std::string& error)
	{
		selections.clear();
		error.clear();
		const DisplayRuleExpression::ValueLookup values =
			[&](const std::string& name, std::string& value)
			{
				if (name == "key") { value = "none"; return true; }
				return sourceValues(name, value);
			};

		for (const Group& group : model.groups)
		{
			const Profile* selected = nullptr;
			for (const std::string& profileName : group.profiles)
			{
				if (profileName == group.defaultSelection) continue;
				const Profile& profile = model.profiles.at(group.name + "." + profileName);
				if (profile.when.empty()) continue;
				int specificity = 0;
				const bool matches = profile.whenExpression.Matches(
					values, specificity, error);
				if (!matches && !error.empty()) return false;
				if (!matches) continue;
				selected = &profile;
				break;
			}
			if (selected)
			{
				selections.push_back({ group.name, selected->name, false });
			}
			else if (group.defaultSelection != "auto")
			{
				selections.push_back({ group.name, group.defaultSelection, true });
			}
		}
		return true;
	}

	// Extract application-local accelerators from the same configuration
	// expressions used for selection. `${key}` is deliberately restricted to a
	// quoted equality literal so registration is finite and deterministic.
	inline bool CollectKeyChords(const Model& model, std::vector<std::string>& chords,
		std::string& error)
	{
		chords.clear();
		error.clear();
		auto collect = [&chords](const DisplayRuleExpression::Expression& expression)
		{
			for (const std::string& chord : expression.KeyChords())
			{
				std::string canonical;
				if (CanonicalizeKeyChord(chord, canonical))
					chords.push_back(canonical);
			}
		};
		for (const Group& group : model.groups)
		{
			if (!group.resetWhen.empty()) collect(group.resetExpression);
			for (const std::string& name : group.profiles)
			{
				const Profile& profile = model.profiles.at(group.name + "." + name);
				if (!profile.when.empty()) collect(profile.whenExpression);
			}
		}
		std::sort(chords.begin(), chords.end());
		chords.erase(std::unique(chords.begin(), chords.end(), [](const std::string& left, const std::string& right)
			{ return left == right; }), chords.end());
		return true;
	}
}
