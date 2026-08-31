#include "pch.h"
#include "CppUnitTest.h"

#include "ConfigFile.h"
#include "RendererProfileConfig.h"
#include <vprenderer/LibplaceboRenderParameters.h>

#include <Windows.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace LibplaceboRenderParameters;

namespace
{
	std::wstring Wide(const std::string& text)
	{
		return std::wstring(text.begin(), text.end());
	}

	template<typename T>
	const T* NativeData(const char* exportName)
	{
		HMODULE module = GetModuleHandleW(L"libplacebo-360.dll");
		if (!module)
			module = LoadLibraryW(L"libplacebo-360.dll");
		Assert::IsTrue(module != nullptr,
			L"The test project must load the libplacebo runtime.");
		const T* data = reinterpret_cast<const T*>(
			GetProcAddress(module, exportName));
		Assert::IsTrue(data != nullptr,
			Wide(std::string("Missing libplacebo data export: ") + exportName).c_str());
		return data;
	}

	void BuildOrFail(const Settings& settings, bool hasDisplayLut,
		Projection& projection)
	{
		std::string error;
		Assert::IsTrue(Build(settings, hasDisplayLut, projection, error),
			Wide(error).c_str());
	}

	void AssertSameData(const void* expected, const void* actual, size_t size,
		const wchar_t* message)
	{
		Assert::AreEqual(0, std::memcmp(expected, actual, size), message);
	}

	template<typename T>
	void AssertPointer(const T* expected, const T* actual,
		const wchar_t* message)
	{
		Assert::AreEqual(reinterpret_cast<uintptr_t>(expected),
			reinterpret_cast<uintptr_t>(actual), message);
	}

	class TemporaryConfigFile
	{
	public:
		TemporaryConfigFile()
		{
			char directory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(directory), directory) > 0);
			char path[MAX_PATH] = {};
			Assert::IsTrue(GetTempFileNameA(directory, "vpp", 0, path) != 0);
			m_path = path;
		}

		~TemporaryConfigFile()
		{
			if (!m_path.empty())
				DeleteFileA(m_path.c_str());
		}

		const std::string& Path() const { return m_path; }

		void Write(const char* contents) const
		{
			std::ofstream output(m_path, std::ios::binary | std::ios::trunc);
			Assert::IsTrue(static_cast<bool>(output));
			output << contents;
		}

	private:
		std::string m_path;
	};

	const std::string& RequiredProfileSetting(
		const RendererProfileConfig::Profile& profile, const char* key)
	{
		const auto setting = profile.settings.find(key);
		Assert::IsTrue(setting != profile.settings.end(),
			Wide(std::string("The selected profile is missing '") + key + "'.").c_str());
		return setting->second;
	}

	Toggle ToggleFromProfileSetting(const std::string& value)
	{
		const std::string normalized = ConfigFile::NormalizeName(value);
		if (normalized == "true" || normalized == "on" ||
			normalized == "high_quality")
			return Toggle::On;
		if (normalized == "false" || normalized == "off")
			return Toggle::Off;
		return Toggle::Auto;
	}

	PeakDetection PeakDetectionFromProfileSetting(const std::string& value)
	{
		const std::string normalized = ConfigFile::NormalizeName(value);
		if (normalized == "high_quality") return PeakDetection::HighQuality;
		if (normalized == "default" || normalized == "on" ||
			normalized == "true")
		{
			return PeakDetection::Standard;
		}
		if (normalized == "off" || normalized == "false")
			return PeakDetection::Off;
		return PeakDetection::Auto;
	}

	// This represents the renderer's final hand-off: the profile resolver has
	// already inherited baseline settings and selected one display profile. The
	// projection boundary consumes only these resolved values.
	Settings SettingsFromResolvedDisplayProfile(
		const RendererProfileConfig::Profile& profile)
	{
		Settings settings;
		settings.quality = RequiredProfileSetting(profile, "quality");
		settings.toneMapping = RequiredProfileSetting(profile, "tone_mapping");
		settings.gamutMapping = RequiredProfileSetting(profile, "gamut_mapping");
		settings.peakDetection = PeakDetectionFromProfileSetting(
			RequiredProfileSetting(profile, "peak_detection"));
		settings.upscaler = RequiredProfileSetting(profile, "upscaler");
		settings.downscaler = RequiredProfileSetting(profile, "downscaler");
		const auto debandStrength = profile.settings.find("deband_strength");
		if (debandStrength != profile.settings.end())
			settings.debandStrength = ConfigFile::NormalizeName(debandStrength->second);
		const auto deband = profile.settings.find("deband");
		if (deband != profile.settings.end())
			settings.deband = ToggleFromProfileSetting(deband->second);
		settings.sigmoid = ToggleFromProfileSetting(
			RequiredProfileSetting(profile, "sigmoid"));
		settings.dithering = ConfigFile::NormalizeName(
			RequiredProfileSetting(profile, "dithering"));
		if (settings.dithering == "on") settings.dithering = "blue_noise";
		const std::string contrast = RequiredProfileSetting(profile,
			"contrast_recovery");
		settings.hasContrastRecovery = ConfigFile::NormalizeName(contrast) != "auto";
		settings.contrastRecovery = settings.hasContrastRecovery ?
			std::stof(contrast) : 0.0f;
		return settings;
	}
}

namespace VideoProcessorTest
{
	TEST_CLASS(LibplaceboRenderParametersTests)
	{
	public:
		TEST_METHOD(EveryQualityPresetProjectsTheNativeLibplaceboPreset)
		{
			const struct
			{
				const char* value;
				const char* exportName;
			} cases[] = {
				{ "fast", "pl_render_fast_params" },
				{ "balanced", "pl_render_default_params" },
				{ "high", "pl_render_high_quality_params" }
			};

			for (const auto& testCase : cases)
			{
				Settings settings;
				settings.quality = testCase.value;
				Projection projection;
				BuildOrFail(settings, false, projection);

				Assert::IsTrue(projection.qualityPresetExport == testCase.exportName);
				const pl_render_params* native =
					NativeData<pl_render_params>(testCase.exportName);
				AssertPointer(native->upscaler, projection.renderParams.upscaler,
					L"The selected quality preset must supply its upscaler.");
				AssertPointer(native->downscaler, projection.renderParams.downscaler,
					L"The selected quality preset must supply its downscaler.");
				AssertPointer(native->error_diffusion,
					projection.renderParams.error_diffusion,
					L"The selected quality preset must retain its error-diffusion setting.");
			}
		}

		TEST_METHOD(AutoScalersFollowTheNativeQualityPreset)
		{
			const struct
			{
				const char* quality;
				const char* upscaler;
				const char* downscaler;
			} cases[] = {
				{ "high", "pl_filter_ewa_lanczossharp", "pl_filter_hermite" },
				{ "balanced", "pl_filter_lanczos", "pl_filter_hermite" },
				{ "fast", nullptr, nullptr }
			};

			for (const auto& testCase : cases)
			{
				Settings settings;
				settings.quality = testCase.quality;
				Projection projection;
				BuildOrFail(settings, false, projection);
				if (testCase.upscaler)
				{
					AssertPointer(NativeData<pl_filter_config>(testCase.upscaler),
						projection.renderParams.upscaler,
						L"Auto upscaler does not match the selected native quality preset.");
					AssertPointer(NativeData<pl_filter_config>(testCase.downscaler),
						projection.renderParams.downscaler,
						L"Auto downscaler does not match the selected native quality preset.");
				}
				else
				{
					Assert::IsNull(projection.renderParams.upscaler,
						L"Fast Auto upscaler must use libplacebo's built-in sampling.");
					Assert::IsNull(projection.renderParams.downscaler,
						L"Fast Auto downscaler must use libplacebo's built-in sampling.");
				}
			}
		}

		TEST_METHOD(DynamicConstantsFollowInteractivePresentationPolicy)
		{
			Settings settings;
			settings.dynamicConstants = true;
			Projection projection;
			BuildOrFail(settings, false, projection);
			Assert::IsTrue(projection.renderParams.dynamic_constants,
				L"Interactive presentation must keep geometry out of compiled shader keys.");

			settings.dynamicConstants = false;
			BuildOrFail(settings, false, projection);
			Assert::IsFalse(projection.renderParams.dynamic_constants,
				L"Non-interactive callers must retain the static-constant policy.");
		}

		TEST_METHOD(EveryToneMappingChoicePointsAtTheNativeFunction)
		{
			const struct
			{
				const char* value;
				const char* exportName;
			} cases[] = {
				{ "spline", "pl_tone_map_spline" },
				{ "bt2390", "pl_tone_map_bt2390" },
				{ "st2094-40", "pl_tone_map_st2094_40" },
				{ "reinhard", "pl_tone_map_reinhard" }
			};

			for (const auto& testCase : cases)
			{
				Settings settings;
				settings.toneMapping = testCase.value;
				Projection projection;
				BuildOrFail(settings, false, projection);

				Assert::IsTrue(projection.toneMappingExport == testCase.exportName);
				AssertPointer(NativeData<pl_tone_map_function>(testCase.exportName),
					projection.colorMapParams.tone_mapping_function,
					L"Tone mapping must use the selected native libplacebo function.");
			}
		}

		TEST_METHOD(EveryGamutMappingChoicePointsAtTheNativeFunction)
		{
			const struct
			{
				const char* value;
				const char* exportName;
			} cases[] = {
				{ "perceptual", "pl_gamut_map_perceptual" },
				{ "softclip", "pl_gamut_map_softclip" },
				{ "relative", "pl_gamut_map_relative" },
				{ "desaturate", "pl_gamut_map_desaturate" }
			};

			for (const auto& testCase : cases)
			{
				Settings settings;
				settings.gamutMapping = testCase.value;
				Projection projection;
				BuildOrFail(settings, false, projection);

				Assert::IsTrue(projection.gamutMappingExport == testCase.exportName);
				AssertPointer(NativeData<pl_gamut_map_function>(testCase.exportName),
					projection.colorMapParams.gamut_mapping,
					L"Gamut mapping must use the selected native libplacebo function.");
			}
		}

		TEST_METHOD(EveryScalerChoicePointsAtTheNativeLibplaceboFilter)
		{
			const struct
			{
				const char* value;
				const char* exportName;
			} upscalers[] = {
				{ "nearest", "pl_filter_nearest" },
				{ "oversample", "pl_filter_oversample" },
				{ "gaussian", "pl_filter_gaussian" },
				{ "catmull_rom", "pl_filter_catmull_rom" },
				{ "lanczos", "pl_filter_lanczos" },
				{ "ewa_lanczossharp", "pl_filter_ewa_lanczossharp" },
				{ "ewa_lanczos", "pl_filter_ewa_lanczos" },
				{ "ewa_lanczos4sharpest", "pl_filter_ewa_lanczos4sharpest" },
				{ "bicubic", "pl_filter_bicubic" },
				{ "bilinear", "pl_filter_bilinear" }
			};
			const struct
			{
				const char* value;
				const char* exportName;
			} downscalers[] = {
				{ "box", "pl_filter_box" },
				{ "hermite", "pl_filter_hermite" },
				{ "gaussian", "pl_filter_gaussian" },
				{ "catmull_rom", "pl_filter_catmull_rom" },
				{ "mitchell", "pl_filter_mitchell" },
				{ "lanczos", "pl_filter_lanczos" },
				{ "bicubic", "pl_filter_bicubic" },
				{ "bilinear", "pl_filter_bilinear" }
			};

			for (const auto& testCase : upscalers)
			{
				Settings settings;
				settings.upscaler = testCase.value;
				Projection projection;
				BuildOrFail(settings, false, projection);
				Assert::IsTrue(projection.upscalerExport == testCase.exportName);
				AssertPointer(NativeData<pl_filter_config>(testCase.exportName),
					projection.renderParams.upscaler,
					L"Upscaling must use the selected native libplacebo filter.");
			}

			for (const auto& testCase : downscalers)
			{
				Settings settings;
				settings.downscaler = testCase.value;
				Projection projection;
				BuildOrFail(settings, false, projection);
				Assert::IsTrue(projection.downscalerExport == testCase.exportName);
				AssertPointer(NativeData<pl_filter_config>(testCase.exportName),
					projection.renderParams.downscaler,
					L"Downscaling must use the selected native libplacebo filter.");
			}

			Settings gpuUpscaler;
			gpuUpscaler.upscaler = "none";
			Projection gpuUpscalerProjection;
			BuildOrFail(gpuUpscaler, false, gpuUpscalerProjection);
			Assert::IsTrue(gpuUpscalerProjection.upscalerExport == "none");
			Assert::IsNull(gpuUpscalerProjection.renderParams.upscaler,
				L"Use GPU must select libplacebo's built-in upscaling path.");

			Settings gpuDownscaler;
			gpuDownscaler.downscaler = "gpu";
			Projection gpuDownscalerProjection;
			BuildOrFail(gpuDownscaler, false, gpuDownscalerProjection);
			Assert::IsTrue(gpuDownscalerProjection.downscalerExport == "gpu");
			Assert::IsNull(gpuDownscalerProjection.renderParams.downscaler,
				L"Use GPU must select libplacebo's built-in downscaling path.");

			const pl_render_params* highQuality =
				NativeData<pl_render_params>("pl_render_high_quality_params");
			for (const char* removed : { "none", "ewa_lanczos" })
			{
				Settings legacy;
				legacy.upscaler = "lanczos";
				legacy.downscaler = removed;
				Projection projection;
				BuildOrFail(legacy, false, projection);
				Assert::IsTrue(projection.downscalerExport == "auto");
				AssertPointer(highQuality->downscaler,
					projection.renderParams.downscaler,
					L"A removed downscaler must fall back to the quality preset, not the upscaler.");
			}
		}

		TEST_METHOD(ExplicitToggleValuesBecomeTheExpectedNativeParameters)
		{
			Settings settings;
			settings.sigmoid = Toggle::On;
			settings.peakDetection = PeakDetection::HighQuality;
			settings.deband = Toggle::On;
			settings.dithering = "blue_noise";
			Projection projection;
			BuildOrFail(settings, false, projection);

			AssertPointer(&projection.sigmoidParams,
				projection.renderParams.sigmoid_params,
				L"Enabled sigmoid scaling must supply sigmoid parameters.");
			AssertSameData(NativeData<pl_sigmoid_params>("pl_sigmoid_default_params"),
				projection.renderParams.sigmoid_params, sizeof(pl_sigmoid_params),
				L"Enabled sigmoid scaling must copy libplacebo's default sigmoid parameters.");
			AssertPointer(&projection.peakDetectParams,
				projection.renderParams.peak_detect_params,
				L"Enabled peak detection must supply peak-detection parameters.");
			AssertSameData(NativeData<pl_peak_detect_params>(
				"pl_peak_detect_high_quality_params"),
				projection.renderParams.peak_detect_params,
				sizeof(pl_peak_detect_params),
				L"Enabled peak detection must copy libplacebo's high-quality parameters.");
			settings.peakDetection = PeakDetection::Standard;
			BuildOrFail(settings, false, projection);
			AssertSameData(NativeData<pl_peak_detect_params>(
				"pl_peak_detect_default_params"),
				projection.renderParams.peak_detect_params,
				sizeof(pl_peak_detect_params),
				L"Standard peak detection must use libplacebo's ordinary parameters, not the high-quality set.");
			AssertPointer(&projection.debandParams,
				projection.renderParams.deband_params,
				L"Enabled debanding must supply debanding parameters.");
			AssertSameData(NativeData<pl_deband_params>("pl_deband_default_params"),
				projection.renderParams.deband_params, sizeof(pl_deband_params),
				L"Enabled debanding must copy libplacebo's default parameters.");
			AssertPointer(&projection.ditherParams,
				projection.renderParams.dither_params,
				L"Enabled dithering must supply dithering parameters.");
			AssertSameData(NativeData<pl_dither_params>("pl_dither_default_params"),
				projection.renderParams.dither_params, sizeof(pl_dither_params),
				L"Enabled dithering must copy libplacebo's default parameters.");
			Assert::IsTrue(projection.renderParams.error_diffusion == nullptr,
				L"Explicit dithering must not be replaced by a preset's error diffusion.");

			const std::pair<const char*, const pl_error_diffusion_kernel*> errorDiffusion[] =
			{
				{ "error_diffusion_simple", &pl_error_diffusion_simple },
				{ "error_diffusion_false_fs", &pl_error_diffusion_false_fs },
				{ "error_diffusion_sierra_lite", &pl_error_diffusion_sierra_lite },
				{ "error_diffusion_floyd_steinberg", &pl_error_diffusion_floyd_steinberg },
				{ "error_diffusion_atkinson", &pl_error_diffusion_atkinson },
				{ "error_diffusion_jarvis_judice_ninke", &pl_error_diffusion_jarvis_judice_ninke },
				{ "error_diffusion_stucki", &pl_error_diffusion_stucki },
				{ "error_diffusion_burkes", &pl_error_diffusion_burkes },
				{ "error_diffusion_sierra2", &pl_error_diffusion_sierra2 },
				{ "error_diffusion_sierra3", &pl_error_diffusion_sierra3 },
			};
			for (const auto& selection : errorDiffusion)
			{
				settings.dithering = selection.first;
				BuildOrFail(settings, false, projection);
				Assert::IsTrue(projection.renderParams.dither_params == nullptr,
					L"Explicit error diffusion must not retain ordinary dithering.");
				Assert::IsTrue(projection.renderParams.error_diffusion == selection.second,
					L"The selected error-diffusion kernel was not forwarded to libplacebo.");
				BuildOrFail(settings, true, projection);
				Assert::IsTrue(projection.renderParams.error_diffusion == selection.second,
					L"A calibration 3D LUT must preserve the selected final error-diffusion stage.");
			}

			settings.sigmoid = Toggle::Off;
			settings.peakDetection = PeakDetection::Off;
			settings.deband = Toggle::Off;
			settings.dithering = "off";
			BuildOrFail(settings, false, projection);
			Assert::IsTrue(projection.renderParams.sigmoid_params == nullptr);
			Assert::IsTrue(projection.renderParams.peak_detect_params == nullptr);
			Assert::IsTrue(projection.renderParams.deband_params == nullptr);
			Assert::IsTrue(projection.renderParams.dither_params == nullptr);
			Assert::IsTrue(projection.renderParams.error_diffusion == nullptr,
				L"Disabled dithering must also disable the preferred error-diffusion path.");
		}

		TEST_METHOD(DisplayBitDepthTargetsDitheringWithoutChangingSwapchainStorage)
		{
			pl_color_repr target{};
			target.bits.sample_depth = 10;
			target.bits.color_depth = 10;

			ApplyDisplayBitDepth("auto", target);
			Assert::AreEqual(10, target.bits.sample_depth);
			Assert::AreEqual(10, target.bits.color_depth);
			Assert::AreEqual(0, target.bits.bit_shift);

			ApplyDisplayBitDepth("8", target);
			Assert::AreEqual(10, target.bits.sample_depth,
				L"Display precision must not change the physical swapchain format.");
			Assert::AreEqual(8, target.bits.color_depth);
			Assert::AreEqual(2, target.bits.bit_shift,
				L"Eight-bit values in a ten-bit RGB target must be MSB-aligned.");

			ApplyDisplayBitDepth("10", target);
			Assert::AreEqual(10, target.bits.sample_depth);
			Assert::AreEqual(10, target.bits.color_depth);
			Assert::AreEqual(0, target.bits.bit_shift);

			pl_color_repr eightBitTarget{};
			eightBitTarget.bits.sample_depth = 8;
			eightBitTarget.bits.color_depth = 8;
			ApplyDisplayBitDepth("10", eightBitTarget);
			Assert::AreEqual(8, eightBitTarget.bits.color_depth,
				L"A requested display depth must be clamped to actual storage precision.");
			Assert::AreEqual(0, eightBitTarget.bits.bit_shift);
		}

		TEST_METHOD(DebandingStrengthUsesOneCanonicalAndDistinctNativeSetting)
		{
			Settings settings;
			Projection projection;

			settings.debandStrength = "off";
			BuildOrFail(settings, false, projection);
			Assert::IsTrue(projection.renderParams.deband_params == nullptr,
				L"Explicitly disabled debanding must not reach libplacebo.");

			settings.debandStrength = "default";
			BuildOrFail(settings, false, projection);
			const pl_deband_params* defaults =
				NativeData<pl_deband_params>("pl_deband_default_params");
			AssertPointer(&projection.debandParams,
				projection.renderParams.deband_params,
				L"Default debanding must use stable projection storage.");
			AssertSameData(defaults, projection.renderParams.deband_params,
				sizeof(pl_deband_params),
				L"Default debanding must match libplacebo's documented defaults.");

			settings.debandStrength = "light";
			BuildOrFail(settings, false, projection);
			Assert::IsTrue(projection.renderParams.deband_params != nullptr,
				L"Light debanding must remain enabled.");
			Assert::IsTrue(std::fabs(projection.debandParams.threshold -
				defaults->threshold * 0.5f) < 0.0001f,
				L"Light debanding must reduce the detail-removal threshold.");
			Assert::IsTrue(std::fabs(projection.debandParams.grain -
				defaults->grain * 0.5f) < 0.0001f,
				L"Light debanding must reduce added grain.");
		}

		TEST_METHOD(ContrastRecoveryAndCalibrationLutPreserveRenderParameters)
		{
			Settings settings;
			settings.hasContrastRecovery = true;
			settings.contrastRecovery = 1.75f;
			Projection projection;
			BuildOrFail(settings, false, projection);
			Assert::IsTrue(std::fabs(
				projection.colorMapParams.contrast_recovery - 1.75f) < 0.0001f);
			Assert::IsTrue(projection.renderParams.lut == nullptr);
			Assert::AreEqual(static_cast<int>(PL_LUT_UNKNOWN),
				static_cast<int>(projection.renderParams.lut_type));

			const pl_render_params* highQuality =
				NativeData<pl_render_params>("pl_render_high_quality_params");
			BuildOrFail(settings, true, projection);
			Assert::IsTrue(projection.renderParams.error_diffusion ==
				highQuality->error_diffusion,
				L"A calibration LUT must not alter final error diffusion.");
		}

		TEST_METHOD(CalibrationLutDoesNotReplaceDtmOrPeakDetection)
		{
			Settings settings;
			settings.toneMapping = "bt2390";
			settings.gamutMapping = "relative";
			settings.peakDetection = PeakDetection::HighQuality;
			Projection withoutLut;
			Projection withLut;
			BuildOrFail(settings, false, withoutLut);
			BuildOrFail(settings, true, withLut);

			Assert::IsNotNull(withLut.renderParams.color_map_params);
			Assert::IsNotNull(withLut.renderParams.peak_detect_params);
			Assert::IsTrue(withLut.colorMapParams.tone_mapping_function ==
				withoutLut.colorMapParams.tone_mapping_function);
			Assert::IsTrue(withLut.colorMapParams.gamut_mapping ==
				withoutLut.colorMapParams.gamut_mapping);
			Assert::IsTrue(withLut.renderParams.lut == nullptr);
			Assert::AreEqual(static_cast<int>(PL_LUT_UNKNOWN),
				static_cast<int>(withLut.renderParams.lut_type));
		}

		TEST_METHOD(SelectedVPRendererProfileProjectsResolvedValuesToLibplacebo)
		{
			// Test-only configuration. The first named [vprenderer.*] section is
			// the ordered baseline; the selected cinema profile inherits it, then
			// overrides only the values it changes. No installed configuration is
			// read or modified by this test.
			TemporaryConfigFile fixture;
			fixture.Write(
				"[vprenderer.normal]\n"
				"shortcut: F4\n"
				"quality: balanced\n"
				"tone_mapping: spline\n"
				"gamut_mapping: perceptual\n"
				"peak_detection: default\n"
				"upscaler: ewa_lanczos\n"
				"downscaler: gpu\n"
				"deband_strength: auto\n"
				"sigmoid: off\n"
				"dithering: on\n"
				"contrast_recovery: auto\n"
				"\n"
				"[vprenderer.cinema]\n"
				"shortcut: F5\n"
				"tone_mapping: bt2390\n"
				"gamut_mapping: softclip\n"
				"peak_detection: high_quality\n"
				"upscaler: bicubic\n"
				"deband_strength: light\n"
				"sigmoid: on\n"
				"dithering: off\n"
				"contrast_recovery: 0.40\n");

			ConfigFile config;
			Assert::IsTrue(config.Load(fixture.Path()));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				Wide(error).c_str());

			std::vector<RendererProfileConfig::KeySelection> selections;
			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "F5",
				[](const std::string&, std::string&) { return false; },
				selections, error), Wide(error).c_str());
			Assert::AreEqual(static_cast<size_t>(1), selections.size());
			Assert::AreEqual("display", selections.front().group.c_str());
			Assert::AreEqual("cinema", selections.front().profile.c_str());

			const auto selected = model.profiles.find("display.cinema");
			Assert::IsTrue(selected != model.profiles.end());
			// `quality` and `downscaler` intentionally come from the first named
			// profile, proving that this exercises the resolver's inheritance path.
			Assert::AreEqual("balanced",
				RequiredProfileSetting(selected->second, "quality").c_str());
			Assert::AreEqual("gpu",
				RequiredProfileSetting(selected->second, "downscaler").c_str());

			Projection projection;
			BuildOrFail(SettingsFromResolvedDisplayProfile(selected->second),
				false, projection);

			Assert::IsTrue(projection.qualityPresetExport ==
				"pl_render_default_params");
			Assert::IsTrue(projection.toneMappingExport == "pl_tone_map_bt2390");
			Assert::IsTrue(projection.gamutMappingExport == "pl_gamut_map_softclip");
			Assert::IsTrue(projection.upscalerExport == "pl_filter_bicubic");
			Assert::IsTrue(projection.downscalerExport == "gpu");
			Assert::IsNull(projection.renderParams.downscaler,
				L"The inherited Use GPU downscaler must retain built-in sampling.");
			AssertPointer(&projection.peakDetectParams,
				projection.renderParams.peak_detect_params,
				L"The selected profile must enable peak detection.");
			AssertSameData(NativeData<pl_peak_detect_params>(
				"pl_peak_detect_high_quality_params"),
				projection.renderParams.peak_detect_params,
				sizeof(pl_peak_detect_params),
				L"The selected profile must use libplacebo's high-quality peak detection.");
			AssertPointer(&projection.sigmoidParams,
				projection.renderParams.sigmoid_params,
				L"The selected profile must enable sigmoid scaling.");
			Assert::IsTrue(projection.renderParams.deband_params != nullptr,
				L"The selected profile must enable debanding.");
			const pl_deband_params* debandDefaults =
				NativeData<pl_deband_params>("pl_deband_default_params");
			Assert::IsTrue(std::fabs(projection.debandParams.threshold -
				debandDefaults->threshold * 0.5f) < 0.0001f,
				L"The selected profile's light debanding must reach libplacebo.");
			Assert::IsTrue(projection.renderParams.dither_params == nullptr,
				L"The selected profile must disable dithering.");
			Assert::IsTrue(std::fabs(
				projection.colorMapParams.contrast_recovery - 0.40f) < 0.0001f);
		}
	};
}
