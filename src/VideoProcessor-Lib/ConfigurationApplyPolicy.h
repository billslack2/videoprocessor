#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

// The configuration editor and the running application use this same
// deliberately conservative policy.  It classifies persisted configuration
// changes; it is not a per-key hot-reload allow list.
namespace ConfigurationApplyPolicy
{
	enum class Action
	{
		SaveOnly,
		ApplyInterface,
		ReloadShortcuts,
		ResetQueues,
		ApplyProfiles,
		RestartRenderer,
		RestartCapture
	};

	struct Change
	{
		std::string section;
		std::string key;
	};

	inline bool HasPrefix(const std::string& value, const char* prefix)
	{
		const std::string needle(prefix);
		return value == needle ||
			(value.size() > needle.size() &&
				value.compare(0, needle.size(), needle) == 0 &&
				value[needle.size()] == '.');
	}

	inline std::string NormalizeSection(const std::string& section)
	{
		std::string normalized(section);
		std::transform(normalized.begin(), normalized.end(), normalized.begin(),
			[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
		return normalized;
	}

	inline bool IsShortcutAffectingChange(const Change& change)
	{
		const std::string section = NormalizeSection(change.section);
		if (section == "shortcuts") return true;
		const std::string key = NormalizeSection(change.key);
		if (key != "shortcut" && key != "cycle_shortcut") return false;

		// Ordered profile shortcuts select live queue, LLDV, renderer,
		// viewport, and shader profiles. Keep this list aligned with the profile
		// families accepted by the configuration model; an unknown section that
		// happens to contain a key named "shortcut" remains save-only.
		return HasPrefix(section, "queue") || HasPrefix(section, "lldv") ||
			HasPrefix(section, "vprenderer") || HasPrefix(section, "viewport") ||
			HasPrefix(section, "shader") || HasPrefix(section, "shaders") ||
			HasPrefix(section, "profiles.queue") ||
			HasPrefix(section, "profiles.lldv") ||
			HasPrefix(section, "profiles.renderer") ||
			HasPrefix(section, "profiles.viewport") ||
			HasPrefix(section, "profiles.shader");
	}

	inline bool IsStartupPresentationDefaultChange(const Change& change)
	{
		if (NormalizeSection(change.section) != "general") return false;
		const std::string key = NormalizeSection(change.key);
		return key == "noui" || key == "no_ui" || key == "fullscreen" ||
			key == "windowedfullscreenmode" ||
			key == "windowed_fullscreen_mode";
	}

	// Unlike the startup fullscreen choice, the monitor target is consulted
	// only when the next fullscreen host is constructed. A saved target change
	// can therefore update that pending selection without changing the current
	// fullscreen state.
	inline bool IsFullscreenMonitorSelectionChange(const Change& change)
	{
		const std::string section = NormalizeSection(change.section);
		return (section == "general" || section == "command_line") &&
			NormalizeSection(change.key) == "fullscreen_monitor_name";
	}

	// Output experiments change the D3D11 device/swapchain contract itself.
	// Treat them more strictly than the normal renderer-only settings: rebuilding
	// the capture graph also replaces the renderer and its ingress state, which
	// makes each test equivalent to a fresh capture/render initialization.
	inline bool IsOutputExperimentChange(const Change& change)
	{
		if (!HasPrefix(NormalizeSection(change.section), "vprenderer.output"))
			return false;
		const std::string key = NormalizeSection(change.key);
		return key == "output_path_profile" ||
			key == "output_presentation" ||
			key == "output_range" ||
			key == "output_transport_gamma" ||
			key == "output_diagnostics" ||
			key == "diagnostic_allow_limited_g22" ||
			key == "diagnostic_allow_full_g22" ||
			key == "diagnostic_disable_compute" ||
			key == "diagnostic_force_8bit_sdr_swapchain" ||
			key == "diagnostic_vp_owned_dxgi_presenter" ||
			key == "diagnostic_disable_shader_cache";
	}

	inline bool IsRenderingProfileSection(const std::string& rawSection)
	{
		const std::string section = NormalizeSection(rawSection);
		if (section == "vprenderer") return true;
		constexpr const char* prefix = "vprenderer.";
		if (section.rfind(prefix, 0) != 0) return false;
		return section.find('.', std::char_traits<char>::length(prefix)) ==
			std::string::npos;
	}

	// Screen and Zoom profile changes use the same renderer-owned, per-frame
	// application path as their F-key selections. Geometry, crop, and subtitle
	// placement therefore do not need to tear down the renderer.
	inline bool IsViewportProfileSection(const std::string& rawSection)
	{
		const std::string section = NormalizeSection(rawSection);
		return HasPrefix(section, "vprenderer.viewport") ||
			HasPrefix(section, "vprenderer.zoom");
	}

	// Scaling profiles alter only the per-frame libplacebo render parameters.
	// They therefore share the live profile path used by Screen profiles rather
	// than rebuilding the capture graph when a filter profile is selected.
	inline bool IsScalingProfileSection(const std::string& rawSection)
	{
		return HasPrefix(NormalizeSection(rawSection), "vprenderer.scaling");
	}

	// Profiles are selected by their source-file order, but only relative to
	// other profiles in the same family.  This gives configuration snapshots a
	// stable way to identify ordering changes without treating an unrelated
	// section insertion as a renderer change.
	inline std::string OrderedProfileGroup(const std::string& rawSection)
	{
		const std::string section = NormalizeSection(rawSection);
		if (section == "vprenderer.input_processing") return {};
		if (HasPrefix(section, "queue")) return "queue";
		if (HasPrefix(section, "lldv")) return "lldv";
		if (HasPrefix(section, "shader.nls")) return "shader.nls";
		if (HasPrefix(section, "shaders.nls")) return "shaders.nls";
		if (HasPrefix(section, "profiles.queue")) return "profiles.queue";
		if (HasPrefix(section, "profiles.lldv")) return "profiles.lldv";
		if (HasPrefix(section, "profiles.renderer")) return "profiles.renderer";
		if (HasPrefix(section, "profiles.viewport")) return "profiles.viewport";
		if (HasPrefix(section, "profiles.shader")) return "profiles.shader";
		if (IsViewportProfileSection(section))
			return HasPrefix(section, "vprenderer.zoom") ?
				"vprenderer.zoom" : "vprenderer.viewport";
		if (IsScalingProfileSection(section)) return "vprenderer.scaling";
		if (IsRenderingProfileSection(section)) return "vprenderer";
		return {};
	}

	inline Action ClassifySection(const std::string& section,
		bool directShowRendererActive = true)
	{
		const std::string normalized = NormalizeSection(section);
		if (normalized == "logging")
			return Action::SaveOnly;
		if (normalized == "shortcuts")
			return Action::ReloadShortcuts;
		// Queue profiles exist in both the current [queue.<name>] spelling and
		// the unified/legacy [profiles.queue.<name>] spelling. Match both before
		// the generic profiles family below so a queue-only edit never rebuilds
		// the renderer.
		if (HasPrefix(normalized, "queue") ||
			HasPrefix(normalized, "profiles.queue") ||
			normalized == "queue_recovery")
			return Action::ResetQueues;
		// Graph-construction settings are irrelevant while Alpha owns the
		// presentation. Persist them for the next DirectShow/madVR graph rather
		// than interrupting an unrelated renderer.
		if (HasPrefix(normalized, "directshow"))
			return directShowRendererActive ? Action::RestartRenderer :
				Action::SaveOnly;
		if (HasPrefix(normalized, "vprenderer.output"))
			return Action::RestartCapture;
		if (IsRenderingProfileSection(normalized) ||
			IsViewportProfileSection(normalized) ||
			IsScalingProfileSection(normalized))
			return Action::ApplyProfiles;

		// Every known renderer, input, profile, shader, LLDV, presentation, and
		// completed-event action section follows the coherent renderer-restart
		// contract. Unknown text intentionally stays save-only so we never guess
		// its runtime meaning.
		if (normalized == "command_line" || normalized == "general" ||
			normalized == "renderer_alias" ||
			normalized == "decklink" || normalized == "p010_conversion" ||
			normalized == "ppm_correction" ||
			normalized == "display_refresh_rate_override" ||
			HasPrefix(normalized, "lldv") || HasPrefix(normalized, "shader") ||
			HasPrefix(normalized, "shaders") || HasPrefix(normalized, "vprenderer") ||
			HasPrefix(normalized, "profiles") || HasPrefix(normalized, "viewport") ||
			HasPrefix(normalized, "actions") || HasPrefix(normalized, "event_actions"))
			return Action::RestartRenderer;
		return Action::SaveOnly;
	}

	inline Action ClassifyChange(const Change& change,
		bool directShowRendererActive = true)
	{
		// These values seed the process presentation state. Applying them to a
		// running session would unexpectedly override the user's current UI and
		// fullscreen choices, so they are deliberately next-start only.
		if (IsStartupPresentationDefaultChange(change)) return Action::SaveOnly;
		// This setting only controls which already-discovered renderers the
		// configuration editor exposes. The editor refreshes those controls
		// immediately, so saving the preference must not restart the renderer.
		if (NormalizeSection(change.section) == "general" &&
			NormalizeSection(change.key) == "hide_legacy_renderers")
			return Action::SaveOnly;
		if ((NormalizeSection(change.section) == "general" ||
			NormalizeSection(change.section) == "command_line") &&
			NormalizeSection(change.key) == "profile_change_display_seconds")
			return Action::ApplyInterface;
		if (IsShortcutAffectingChange(change)) return Action::ReloadShortcuts;
		if (IsOutputExperimentChange(change)) return Action::RestartCapture;
		const std::string section = NormalizeSection(change.section);
		const std::string key = NormalizeSection(change.key);
		if ((section == "general" || section == "command_line") &&
			(key == "capture_device" || key == "capture_input"))
			return Action::RestartCapture;
		return ClassifySection(change.section, directShowRendererActive);
	}

	inline Action ClassifyChanges(const std::vector<Change>& changes,
		bool directShowRendererActive = true)
	{
		Action result = Action::SaveOnly;
		for (const Change& change : changes)
			result = std::max(result, ClassifyChange(change,
				directShowRendererActive));
		return result;
	}

	inline Action ClassifySections(const std::vector<std::string>& sections,
		bool directShowRendererActive = true)
	{
		Action result = Action::SaveOnly;
		for (const std::string& section : sections)
			result = std::max(result, ClassifySection(section,
				directShowRendererActive));
		return result;
	}

	inline const char* ActionLabel(Action action)
	{
		switch (action)
		{
		case Action::RestartCapture: return "Restart capture";
		case Action::RestartRenderer: return "Restart renderer";
		case Action::ResetQueues: return "Reset queues";
		case Action::ApplyProfiles: return "Apply rendering live";
		case Action::ReloadShortcuts: return "Apply shortcuts live";
		case Action::ApplyInterface: return "Apply display live";
		default: return "Takes effect next start";
		}
	}
}
