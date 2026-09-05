#include "LibplaceboRenderParameters.h"

#include <Windows.h>

#include <type_traits>

namespace
{
	template<typename T>
	const T* ReadLibplaceboData(const char* exportName, std::string& error)
	{
		HMODULE module = GetModuleHandleW(L"libplacebo-360.dll");
		if (!module)
		{
			error = "libplacebo runtime was not preloaded";
			return nullptr;
		}

		const T* value = reinterpret_cast<const T*>(
			GetProcAddress(module, exportName));
		if (!value)
		{
			error = std::string("libplacebo runtime is missing data export ") +
			exportName;
			return nullptr;
		}
		return value;
	}

	const char* QualityPreset(const std::string& quality)
	{
		if (quality == "fast") return "pl_render_fast_params";
		if (quality == "balanced") return "pl_render_default_params";
		return "pl_render_high_quality_params";
	}

	const char* ToneMapping(const std::string& toneMapping)
	{
		if (toneMapping == "bt2390") return "pl_tone_map_bt2390";
		if (toneMapping == "st2094-40") return "pl_tone_map_st2094_40";
		if (toneMapping == "reinhard") return "pl_tone_map_reinhard";
		return "pl_tone_map_spline";
	}

	const char* GamutMapping(const std::string& gamutMapping)
	{
		if (gamutMapping == "softclip") return "pl_gamut_map_softclip";
		if (gamutMapping == "relative") return "pl_gamut_map_relative";
		if (gamutMapping == "desaturate") return "pl_gamut_map_desaturate";
		return "pl_gamut_map_perceptual";
	}

	const char* Upscaler(const std::string& upscaler)
	{
		if (upscaler == "none") return nullptr;
		if (upscaler == "nearest") return "pl_filter_nearest";
		if (upscaler == "oversample") return "pl_filter_oversample";
		if (upscaler == "gaussian") return "pl_filter_gaussian";
		if (upscaler == "catmull_rom") return "pl_filter_catmull_rom";
		if (upscaler == "lanczos") return "pl_filter_lanczos";
		if (upscaler == "ewa_lanczos") return "pl_filter_ewa_lanczos";
		if (upscaler == "ewa_lanczos4sharpest") return "pl_filter_ewa_lanczos4sharpest";
		if (upscaler == "bicubic") return "pl_filter_bicubic";
		if (upscaler == "bilinear") return "pl_filter_bilinear";
		return "pl_filter_ewa_lanczossharp";
	}

	const char* Downscaler(const std::string& downscaler)
	{
		if (downscaler == "box") return "pl_filter_box";
		if (downscaler == "hermite") return "pl_filter_hermite";
		if (downscaler == "gaussian") return "pl_filter_gaussian";
		if (downscaler == "catmull_rom") return "pl_filter_catmull_rom";
		if (downscaler == "mitchell") return "pl_filter_mitchell";
		if (downscaler == "lanczos") return "pl_filter_lanczos";
		if (downscaler == "bicubic") return "pl_filter_bicubic";
		if (downscaler == "bilinear") return "pl_filter_bilinear";
		return nullptr;
	}

	const pl_error_diffusion_kernel* ErrorDiffusionKernel(const std::string& value)
	{
		if (value == "error_diffusion_simple") return &pl_error_diffusion_simple;
		if (value == "error_diffusion_false_fs") return &pl_error_diffusion_false_fs;
		if (value == "error_diffusion_sierra_lite") return &pl_error_diffusion_sierra_lite;
		if (value == "error_diffusion_floyd_steinberg") return &pl_error_diffusion_floyd_steinberg;
		if (value == "error_diffusion_atkinson") return &pl_error_diffusion_atkinson;
		if (value == "error_diffusion_jarvis_judice_ninke") return &pl_error_diffusion_jarvis_judice_ninke;
		if (value == "error_diffusion_stucki") return &pl_error_diffusion_stucki;
		if (value == "error_diffusion_burkes") return &pl_error_diffusion_burkes;
		if (value == "error_diffusion_sierra2") return &pl_error_diffusion_sierra2;
		if (value == "error_diffusion_sierra3") return &pl_error_diffusion_sierra3;
		return nullptr;
	}
}

namespace LibplaceboRenderParameters
{
	void ApplyDisplayBitDepth(const std::string& displayBitDepth,
		pl_color_repr& targetRepresentation)
	{
		int requestedDepth = 0;
		if (displayBitDepth == "8") requestedDepth = 8;
		else if (displayBitDepth == "10") requestedDepth = 10;
		else return;

		const int sampleDepth = targetRepresentation.bits.sample_depth;
		if (sampleDepth <= 0) return;

		const int effectiveDepth = requestedDepth < sampleDepth ?
			requestedDepth : sampleDepth;
		targetRepresentation.bits.color_depth = effectiveDepth;
		targetRepresentation.bits.bit_shift = sampleDepth - effectiveDepth;
	}

	bool Build(const Settings& settings, bool /*hasDisplayLut*/,
		Projection& projection, std::string& error)
	{
		projection = Projection{};
		error.clear();

		projection.qualityPresetExport = QualityPreset(settings.quality);
		const pl_render_params* preset = ReadLibplaceboData<pl_render_params>(
			projection.qualityPresetExport.c_str(), error);
		if (!preset) return false;
		projection.renderParams = *preset;
		projection.renderParams.dynamic_constants = settings.dynamicConstants;

		projection.renderParams.lut = nullptr;
		projection.renderParams.lut_type = PL_LUT_UNKNOWN;

		if (projection.renderParams.color_map_params)
			projection.colorMapParams = *projection.renderParams.color_map_params;
		else
		{
			const pl_color_map_params* defaults =
				ReadLibplaceboData<pl_color_map_params>(
					"pl_color_map_default_params", error);
			if (!defaults) return false;
			projection.colorMapParams = *defaults;
		}

		if (settings.toneMapping != "auto")
		{
			projection.toneMappingExport = ToneMapping(settings.toneMapping);
			const pl_tone_map_function* function =
				ReadLibplaceboData<pl_tone_map_function>(
					projection.toneMappingExport.c_str(), error);
			if (!function) return false;
			projection.colorMapParams.tone_mapping_function = function;
		}

		if (settings.gamutMapping != "auto")
		{
			projection.gamutMappingExport = GamutMapping(settings.gamutMapping);
			const pl_gamut_map_function* function =
				ReadLibplaceboData<pl_gamut_map_function>(
					projection.gamutMappingExport.c_str(), error);
			if (!function) return false;
			projection.colorMapParams.gamut_mapping = function;
		}

		if (settings.hasContrastRecovery)
			projection.colorMapParams.contrast_recovery = settings.contrastRecovery;
		projection.renderParams.color_map_params = &projection.colorMapParams;

		auto setToggle = [&error](Toggle toggle, const char* exportName,
			auto& destination, auto& parameter) -> bool
		{
			if (toggle == Toggle::Off)
			{
				destination = nullptr;
				return true;
			}
			if (toggle == Toggle::On)
			{
				const auto* defaults = ReadLibplaceboData<std::decay_t<decltype(parameter)>>(
					exportName, error);
				if (!defaults) return false;
				parameter = *defaults;
				destination = &parameter;
				return true;
			}
			if (destination)
			{
				parameter = *destination;
				destination = &parameter;
			}
			return true;
		};

		auto setDeband = [&]() -> bool
		{
			if (settings.debandStrength == "off")
			{
				projection.renderParams.deband_params = nullptr;
				return true;
			}
			if (settings.debandStrength == "light" ||
				settings.debandStrength == "default")
			{
				const pl_deband_params* defaults =
					ReadLibplaceboData<pl_deband_params>(
						"pl_deband_default_params", error);
				if (!defaults) return false;
				projection.debandParams = *defaults;
				if (settings.debandStrength == "light")
				{
					// libplacebo's documented default is the full-strength choice.
					// Light keeps the same radius/iteration count but reduces both
					// the detail-removal threshold and added grain.
					projection.debandParams.threshold *= 0.5f;
					projection.debandParams.grain *= 0.5f;
				}
				projection.renderParams.deband_params = &projection.debandParams;
				return true;
			}
			return setToggle(settings.deband, "pl_deband_default_params",
				projection.renderParams.deband_params, projection.debandParams);
		};

		auto setPeakDetection = [&]() -> bool
		{
			if (settings.peakDetection == PeakDetection::Off)
			{
				projection.renderParams.peak_detect_params = nullptr;
				return true;
			}
			if (settings.peakDetection == PeakDetection::Auto)
			{
				if (projection.renderParams.peak_detect_params)
				{
					projection.peakDetectParams =
						*projection.renderParams.peak_detect_params;
					projection.renderParams.peak_detect_params =
						&projection.peakDetectParams;
				}
				return true;
			}

			const char* exportName = settings.peakDetection ==
				PeakDetection::HighQuality ?
				"pl_peak_detect_high_quality_params" :
				"pl_peak_detect_default_params";
			const pl_peak_detect_params* parameters =
				ReadLibplaceboData<pl_peak_detect_params>(exportName, error);
			if (!parameters) return false;
			projection.peakDetectParams = *parameters;
			projection.renderParams.peak_detect_params =
				&projection.peakDetectParams;
			return true;
		};

		auto setDithering = [&]() -> bool
		{
			if (settings.dithering == "off")
			{
				// libplacebo treats error diffusion as a separate, preferred
				// dithering path. Both pointers must be cleared for Off to mean Off.
				projection.renderParams.dither_params = nullptr;
				projection.renderParams.error_diffusion = nullptr;
				return true;
			}
			if (const pl_error_diffusion_kernel* kernel =
				ErrorDiffusionKernel(settings.dithering))
			{
				// This needs compute-shader and image-storage support. Target-frame
				// calibration LUTs run earlier and do not displace this final stage.
				projection.renderParams.dither_params = nullptr;
				projection.renderParams.error_diffusion = kernel;
				return true;
			}
			if (settings.dithering != "auto")
			{
				const pl_dither_params* defaults =
					ReadLibplaceboData<pl_dither_params>(
						"pl_dither_default_params", error);
				if (!defaults) return false;
				projection.ditherParams = *defaults;
				if (settings.dithering == "ordered_lut")
					projection.ditherParams.method = PL_DITHER_ORDERED_LUT;
				else if (settings.dithering == "ordered_fixed")
					projection.ditherParams.method = PL_DITHER_ORDERED_FIXED;
				else if (settings.dithering == "white_noise")
					projection.ditherParams.method = PL_DITHER_WHITE_NOISE;
				else
					projection.ditherParams.method = PL_DITHER_BLUE_NOISE;
				projection.renderParams.dither_params = &projection.ditherParams;
				// An explicit method selects ordinary, gamma-aware dithering instead
				// of inheriting a preset's error-diffusion kernel.
				projection.renderParams.error_diffusion = nullptr;
				return true;
			}
			if (projection.renderParams.dither_params)
			{
				projection.ditherParams = *projection.renderParams.dither_params;
				projection.renderParams.dither_params = &projection.ditherParams;
			}
			return true;
		};

		if (!setToggle(settings.sigmoid, "pl_sigmoid_default_params",
			projection.renderParams.sigmoid_params, projection.sigmoidParams) ||
			!setPeakDetection() ||
			!setDeband() ||
			!setDithering())
		{
			return false;
		}

		if (settings.upscaler != "auto")
		{
			const char* exportName = Upscaler(settings.upscaler);
			projection.upscalerExport = exportName ? exportName : "none";
			if (exportName)
			{
				projection.renderParams.upscaler =
					ReadLibplaceboData<pl_filter_config>(exportName, error);
				if (!projection.renderParams.upscaler) return false;
			}
			else
				projection.renderParams.upscaler = nullptr;
		}

		if (settings.downscaler != "auto")
		{
			if (settings.downscaler == "gpu")
			{
				projection.downscalerExport = "gpu";
				projection.renderParams.downscaler = nullptr;
				return true;
			}
			const char* exportName = Downscaler(settings.downscaler);
			// Removed or otherwise unsupported legacy values behave exactly as an
			// omitted setting: retain the quality preset's Auto downscaler.
			if (!exportName)
			{
				projection.downscalerExport = "auto";
				return true;
			}
			projection.downscalerExport = exportName;
			projection.renderParams.downscaler =
				ReadLibplaceboData<pl_filter_config>(exportName, error);
			if (!projection.renderParams.downscaler) return false;
		}

		return true;
	}
}
