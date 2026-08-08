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
		if (upscaler == "ewa_lanczos") return "pl_filter_ewa_lanczos";
		if (upscaler == "bicubic") return "pl_filter_bicubic";
		if (upscaler == "bilinear") return "pl_filter_bilinear";
		return "pl_filter_ewa_lanczossharp";
	}

	const char* Downscaler(const std::string& downscaler)
	{
		if (downscaler == "bicubic") return "pl_filter_bicubic";
		if (downscaler == "bilinear") return "pl_filter_bilinear";
		return "pl_filter_ewa_lanczos";
	}
}

namespace LibplaceboRenderParameters
{
	bool Build(const Settings& settings, bool hasDisplayLut,
		Projection& projection, std::string& error)
	{
		projection = Projection{};
		error.clear();

		projection.qualityPresetExport = QualityPreset(settings.quality);
		const pl_render_params* preset = ReadLibplaceboData<pl_render_params>(
			projection.qualityPresetExport.c_str(), error);
		if (!preset) return false;
		projection.renderParams = *preset;

		// A target display LUT has a known incompatible path with the preset's
		// error-diffusion shader. It remains an output-LUT decision, not a user
		// dithering preference.
		if (hasDisplayLut)
			projection.renderParams.error_diffusion = nullptr;
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

		if (!setToggle(settings.sigmoid, "pl_sigmoid_default_params",
			projection.renderParams.sigmoid_params, projection.sigmoidParams) ||
			!setToggle(settings.peakDetection,
				"pl_peak_detect_high_quality_params",
				projection.renderParams.peak_detect_params,
				projection.peakDetectParams) ||
			!setDeband() ||
			!setToggle(settings.dithering, "pl_dither_default_params",
				projection.renderParams.dither_params, projection.ditherParams))
		{
			return false;
		}

		if (settings.upscaler != "auto")
		{
			projection.upscalerExport = Upscaler(settings.upscaler);
			projection.renderParams.upscaler =
				ReadLibplaceboData<pl_filter_config>(
					projection.upscalerExport.c_str(), error);
			if (!projection.renderParams.upscaler) return false;
		}

		if (settings.downscaler != "auto")
		{
			projection.downscalerExport = Downscaler(settings.downscaler);
			projection.renderParams.downscaler =
				ReadLibplaceboData<pl_filter_config>(
					projection.downscalerExport.c_str(), error);
			if (!projection.renderParams.downscaler) return false;
		}

		return true;
	}
}
