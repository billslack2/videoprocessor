#pragma once

// Maps the resolved VP Renderer settings to the exact libplacebo parameter
// structures passed to pl_render_image.  Keeping this boundary separate makes
// the mapping independently testable without creating a D3D11 renderer.

#include <libplacebo/renderer.h>

#include <string>

namespace LibplaceboRenderParameters
{
	// "Auto" deliberately means retain the selected quality preset's value.
	enum class Toggle
	{
		Auto,
		On,
		Off
	};

	// Peak detection has two deliberately distinct native parameter sets.
	// Do not collapse Standard and HighQuality into Toggle::On: the latter has
	// a much higher percentile and a substantially higher cost.
	enum class PeakDetection
	{
		Auto,
		Standard,
		HighQuality,
		Off
	};

	struct Settings
	{
		std::string quality = "high";
		std::string toneMapping = "auto";
		std::string gamutMapping = "auto";
		PeakDetection peakDetection = PeakDetection::Auto;
		bool hasContrastRecovery = false;
		float contrastRecovery = 0.0f;
		std::string upscaler = "auto";
		std::string downscaler = "auto";
		// Canonical setting for new configuration. "Auto" preserves the
		// quality preset, "off" disables it, and the other values select an
		// explicit parameter set. `deband` remains for legacy configuration.
		std::string debandStrength = "auto";
		Toggle deband = Toggle::Auto;
		Toggle sigmoid = Toggle::Auto;
		Toggle dithering = Toggle::Auto;
	};

	struct Projection
	{
		// These names identify the data export read from libplacebo. They are
		// intentionally retained for tests and diagnostics; the actual
		// structures below are the values sent to libplacebo.
		std::string qualityPresetExport;
		std::string toneMappingExport;
		std::string gamutMappingExport;
		std::string upscalerExport;
		std::string downscalerExport;

		pl_render_params renderParams{};
		pl_color_map_params colorMapParams{};
		pl_peak_detect_params peakDetectParams{};
		pl_sigmoid_params sigmoidParams{};
		pl_deband_params debandParams{};
		pl_dither_params ditherParams{};
	};

	// `hasDisplayLut` preserves VP's compatible-LUT behavior: error diffusion
	// from a quality preset is removed before render parameters are used.
	// Returns false only when the expected libplacebo runtime data cannot be
	// obtained. The caller receives a human-readable error in `error`.
	bool Build(const Settings& settings, bool hasDisplayLut,
		Projection& projection, std::string& error);

	// Applies the user-declared native display precision to an already resolved
	// target representation. The storage depth remains unchanged; lower display
	// precision is represented MSB-aligned so libplacebo dithers and writes the
	// correct full-range values into a higher-depth RGB swapchain.
	void ApplyDisplayBitDepth(const std::string& displayBitDepth,
		pl_color_repr& targetRepresentation);
}
