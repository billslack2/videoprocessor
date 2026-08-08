#include "pch.h"

#include <ConfigEditorCore.h>
#include "CppUnitTest.h"

#include <algorithm>
#include <fstream>
#include <iterator>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
	std::wstring MakeTemporaryConfigPath(const wchar_t* prefix)
	{
		wchar_t directory[MAX_PATH] = {};
		if (GetTempPathW(ARRAYSIZE(directory), directory) == 0)
			return {};
		wchar_t path[MAX_PATH] = {};
		if (GetTempFileNameW(directory, prefix, 0, path) == 0)
			return {};
		DeleteFileW(path);
		return path;
	}

	void WriteBytes(const std::wstring& path, const std::string& text)
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(text.data(), static_cast<std::streamsize>(text.size()));
	}

	std::string ReadBytes(const std::wstring& path)
	{
		std::ifstream input(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>());
	}
}

namespace VideoProcessorTest
{
	TEST_CLASS(ConfigEditorCoreTests)
	{
	public:
		TEST_METHOD(ConfigEditorCoreLoadsAndValidatesCurrentDeployedFixture)
		{
			std::string sourcePath = __FILE__;
			std::replace(sourcePath.begin(), sourcePath.end(), '/', '\\');
			const std::string marker =
				"\\src\\VideoProcessor-Test\\ConfigEditorCoreTests.cpp";
			const size_t markerPosition = sourcePath.rfind(marker);
			Assert::IsTrue(markerPosition != std::string::npos);
			sourcePath.resize(markerPosition);
			sourcePath +=
				"\\test-fixtures\\deployed-VideoProcessor-20260807-current.cfg";
			const std::wstring path(sourcePath.begin(), sourcePath.end());

			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			Assert::IsTrue(ConfigEditorCore::ValidateCandidate(document, error),
				error.c_str());
		}

		TEST_METHOD(ConfigEditorCorePreservesCommentsUnknownLinesAndTerminalEnding)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			Assert::IsFalse(path.empty());
			const std::string original =
				"# preserve this comment\r\n"
				"[queue]\r\n"
				"queue_size = 32 ; keep this explanation\r\n"
				"manual_setting: value # untouched\r\n"
				"\r\n"
				"[shader.manual]\r\n"
				"when: ${key}==\"F9\"\r\n";
			WriteBytes(path, original);

			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			Assert::IsTrue(document.SetExisting("queue", "queue_size", "1"));
			const std::string expected =
				"# preserve this comment\r\n"
				"[queue]\r\n"
				"queue_size = 1 ; keep this explanation\r\n"
				"manual_setting: value # untouched\r\n"
				"\r\n"
				"[shader.manual]\r\n"
				"when: ${key}==\"F9\"\r\n";
			Assert::AreEqual(expected, document.Serialize());
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(ConfigEditorCoreNoOpRoundTripIsByteIdentical)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			const std::string original =
				"\xEF\xBB\xBF# UTF-8 BOM and CRLF must survive\r\n"
				"[general]\r\n"
				"fullscreen = true ; retained\r\n"
				"\r\n"
				"[manual.empty]\r\n";
			WriteBytes(path, original);
			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			Assert::AreEqual(original, document.Serialize());
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(ConfigEditorCoreSafeSaveCreatesBackupAfterValidation)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			Assert::IsFalse(path.empty());
			const std::string original =
				"# retained comment\n"
				"[queue]\n"
				"queue_size: 32\n";
			WriteBytes(path, original);

			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			Assert::IsTrue(document.SetExisting("queue", "queue_size", "1"));
			ConfigEditorCore::SaveResult result;
			const bool saved = ConfigEditorCore::SaveSafely(document, result, error);
			if (!saved) Logger::WriteMessage(error.c_str());
			Assert::IsTrue(saved, error.c_str());
			Assert::IsFalse(result.backupPath.empty());
			Assert::AreEqual(original, ReadBytes(result.backupPath));
			Assert::AreEqual(std::string("# retained comment\n[queue]\nqueue_size: 1\n"),
				ReadBytes(path));
			DeleteFileW(result.backupPath.c_str());
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(ConfigEditorCoreSafeSaveLeavesOriginalWhenValidationFails)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			Assert::IsFalse(path.empty());
			const std::string original = "[queue]\nqueue_size: 32\n";
			WriteBytes(path, original);

			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			Assert::IsTrue(document.SetExisting("queue", "queue_size", "0"));
			ConfigEditorCore::SaveResult result;
			Assert::IsFalse(ConfigEditorCore::SaveSafely(document, result, error));
			Assert::IsFalse(error.empty());
			Assert::IsTrue(result.backupPath.empty());
			Assert::AreEqual(original, ReadBytes(path));
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(ConfigEditorCoreProfileLifecyclePreservesSectionContentAndOrder)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			Assert::IsFalse(path.empty());
			WriteBytes(path,
				"# profile document\n"
				"[queue]\n"
				"# baseline note\n"
				"queue_size: 32\n"
				"[queue.low_latency]\n"
				"shortcut: Shift+l\n"
				"queue_size: 1\n");

			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			Assert::IsTrue(document.RenameSection("queue", "queue.normal"));
			Assert::IsTrue(document.MoveSectionBefore("queue.low_latency", "queue.normal"));
			Assert::IsTrue(document.SetKnown("queue.low_latency", "target_frames", "1"));
			const std::string serialized = document.Serialize();
			Assert::IsTrue(serialized.find("[queue.low_latency]") < serialized.find("[queue.normal]"));
			Assert::IsTrue(serialized.find("# baseline note\nqueue_size: 32") != std::string::npos);
			Assert::IsTrue(serialized.find("target_frames: 1") != std::string::npos);
			Assert::IsTrue(ConfigEditorCore::ValidateCandidate(document, error), error.c_str());
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(ConfigEditorCorePreservesExplicitlyDisabledShortcut)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			Assert::IsFalse(path.empty());
			WriteBytes(path,
				"[shortcuts]\n"
				"fullscreen_toggle: Alt+Enter\n");

			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			Assert::IsTrue(document.SetKnown("shortcuts", "fullscreen_toggle", ""));
			Assert::AreEqual(std::string("[shortcuts]\nfullscreen_toggle: \n"),
				document.Serialize());
			Assert::IsTrue(ConfigEditorCore::ValidateCandidate(document, error), error.c_str());
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(ConfigEditorCoreEditsShippedNlsWithoutRewritingCustomShaders)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			Assert::IsFalse(path.empty());
			const std::string originalCustom =
				"[shader.nls.unbound]\n"
				"shader_type: custom\n"
				"glsl_file: My Hand Tuned Shader.glsl\n"
				"private_parameter: keep exactly this # manual\n";
			WriteBytes(path,
				"[shader.nls]\nshortcut: n\n"
				"[shader.nls.standard]\n"
				"shortcut: Shift+n\nshader_type: nls\n"
				"geometry: classic\nstrength: 1.0\ncurve: 2.0\n"
				"tolerance_percent: 5\n" + originalCustom);

			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			Assert::IsTrue(document.SetKnown(
				"shader.nls.standard", "strength", "0.85"));
			Assert::IsTrue(document.SetKnown(
				"shader.nls.standard", "shortcut", "Ctrl+Shift+N"));
			const std::string serialized = document.Serialize();
			Assert::IsTrue(serialized.find("strength: 0.85") != std::string::npos);
			Assert::IsTrue(serialized.find("shortcut: Ctrl+Shift+N") !=
				std::string::npos);
			Assert::IsTrue(serialized.find(originalCustom) != std::string::npos);
			Assert::IsTrue(ConfigEditorCore::ValidateCandidate(document, error),
				error.c_str());
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(ConfigEditorCoreRejectsInvalidStructuredNlsValues)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			Assert::IsFalse(path.empty());
			WriteBytes(path,
				"[shader.nls]\n"
				"[shader.nls.standard]\n"
				"shader_type: nls\n"
				"geometry: classic\n"
				"strength: 1.25\n");

			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			Assert::IsFalse(ConfigEditorCore::ValidateCandidate(document, error));
			Assert::IsTrue(error.find(L"strength") != std::wstring::npos,
				error.c_str());
			Assert::IsTrue(document.SetKnown(
				"shader.nls.standard", "strength", "0.75"));
			Assert::IsTrue(document.SetKnown(
				"shader.nls.standard", "type", "multi"));
			Assert::IsFalse(ConfigEditorCore::ValidateCandidate(document, error));
			Assert::IsTrue(error.find(L"shader group") != std::wstring::npos,
				error.c_str());
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(ConfigEditorCorePreservesProfileHeaderCase)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			Assert::IsFalse(path.empty());
			WriteBytes(path,
				"[queue.Low_Latency]\n"
				"queue_size: 2\n");

			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			const std::vector<std::string> sections =
				document.SectionNamesWithPrefix("QUEUE");
			Assert::AreEqual(static_cast<size_t>(1), sections.size());
			Assert::AreEqual(std::string("queue.Low_Latency"), sections.front());
			Assert::IsTrue(document.RenameSection(
				"queue.low_latency", "queue.HDR_Cinema"));
			Assert::AreEqual(std::string(
				"[queue.HDR_Cinema]\nqueue_size: 2\n"), document.Serialize());
			Assert::IsTrue(document.RenameSection(
				"queue.hdr_cinema", "queue.HDR_CINEMA"));
			Assert::AreEqual(std::string(
				"[queue.HDR_CINEMA]\nqueue_size: 2\n"), document.Serialize());
			Assert::IsTrue(ConfigEditorCore::ValidateCandidate(document, error), error.c_str());
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(ConfigEditorCoreValidatesEveryEditableOrderedProfileSurface)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			Assert::IsFalse(path.empty());
			WriteBytes(path,
				"[queue.normal]\nqueue_size: 32\nlead_frames: 1\ntarget_frames: 4\n"
				"active_picture_lookahead_frames: 0\nstartup_preroll_frames: 0\n"
				"reset_after_render_restart_seconds: 5\nreset_queue_too_large_percent: 75\n"
				"[queue.low]\nshortcut: Shift+l\nqueue_size: 1\nlead_frames: 0\n"
				"startup_preroll_frames: 1\ntarget_frames: 1\nactive_picture_lookahead_frames: 2\n"
				"reset_after_render_restart_seconds: 2\nreset_queue_too_large_percent: 60\n"
				"[vprenderer.primary]\nquality: high\noutput_presentation: auto\noutput_range: full\n"
				"output_gamma: srgb\nsdr_target_nits: 203\nsdr_black_nits: auto\n"
				"tone_mapping: auto\ngamut_mapping: auto\npeak_detection: auto\ncontrast_recovery: auto\n"
				"upscaler: auto\ndownscaler: auto\ndeband: auto\ndeband_strength: off\n"
				"sigmoid: auto\ndithering: auto\nsdr_input_transfer: auto\nsdr_target_primaries: rec709\n"
				"lut: calibration.cube\nlut_reference_nits: auto\nlut_reference_range: auto\n"
				"lut_reference_transfer: auto\nlut_reference_primaries: auto\n"
				"report_bt2020_to_display: false\nswitch_refresh_rate: true\noutput_diagnostics: false\n"
				"diagnostic_disable_shader_cache: false\ndefault_screen_profile: normal\n"
				"[vprenderer.bt2020]\nshortcut: F5\nsdr_target_primaries: bt2020\nreport_bt2020_to_display: true\n"
				"[vprenderer.viewport.viewport_16x9]\nlabel: 16x9\nmode: normal\nscreen_aspect: 16:9\n"
				"automatic_crop: false\nsubtitle_fit: true\nsubtitle_hold_seconds: 2\n"
				"subtitle_release_drift_seconds: 0\nsubtitle_padding_pixels: 20\n"
				"scope_screen_aspect: 2.35:1\nscope_automatic_crop: true\nscope_subtitle_fit: true\n"
				"scope_subtitle_hold_seconds: 2\nscope_subtitle_release_drift_seconds: 0\n"
				"scope_subtitle_padding_pixels: 20\n"
				"[lldv.standard]\nmax_cll: 1000\nmax_fall: 401\nmastering_min_luminance: 0.001\n"
				"mastering_max_luminance: 4000\n");

			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			Assert::IsTrue(ConfigEditorCore::ValidateCandidate(document, error), error.c_str());
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(ConfigEditorCoreRejectsExternalChangeBeforeSaving)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			WriteBytes(path, "[general]\nfullscreen: false\n"
				"[vprenderer.primary]\nquality: high\n");
			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			Assert::IsTrue(document.SetKnown("general", "fullscreen", "true"));
			WriteBytes(path, "[general]\nfullscreen: false\n# external edit\n"
				"[vprenderer.primary]\nquality: high\n");
			ConfigEditorCore::SaveResult result;
			Assert::IsFalse(ConfigEditorCore::SaveSafely(document, result, error));
			Assert::IsTrue(error.find(L"changed outside") != std::wstring::npos,
				error.c_str());
			Assert::AreEqual(std::string(
				"[general]\nfullscreen: false\n# external edit\n"
				"[vprenderer.primary]\nquality: high\n"), ReadBytes(path));
			Assert::IsTrue(result.backupPath.empty());
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(ConfigEditorCoreSupportsRepeatedSafeSavesFromOneDocument)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			WriteBytes(path, "[general]\nfullscreen: false\n"
				"[vprenderer.primary]\nquality: high\n");
			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			ConfigEditorCore::SaveResult first;
			Assert::IsTrue(document.SetKnown("general", "fullscreen", "true"));
			Assert::IsTrue(ConfigEditorCore::SaveSafely(document, first, error), error.c_str());
			ConfigEditorCore::SaveResult second;
			Assert::IsTrue(document.SetKnown("general", "startminimized", "true"));
			Assert::IsTrue(ConfigEditorCore::SaveSafely(document, second, error), error.c_str());
			Assert::AreEqual(std::string(
				"[general]\nfullscreen: true\nstartminimized: true\n"
				"[vprenderer.primary]\nquality: high\n"), ReadBytes(path));
			DeleteFileW(first.backupPath.c_str());
			DeleteFileW(second.backupPath.c_str());
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(ConfigEditorCoreLeavesLockedConfigurationUntouched)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			const std::string original = "[general]\nfullscreen: false\n"
				"[vprenderer.primary]\nquality: high\n";
			WriteBytes(path, original);
			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			Assert::IsTrue(document.SetKnown("general", "fullscreen", "true"));
			HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			Assert::IsTrue(locked != INVALID_HANDLE_VALUE);
			ConfigEditorCore::SaveResult result;
			Assert::IsFalse(ConfigEditorCore::SaveSafely(document, result, error));
			Assert::IsTrue(error.find(L"replace") != std::wstring::npos ||
				error.find(L"backup") != std::wstring::npos, error.c_str());
			CloseHandle(locked);
			Assert::AreEqual(original, ReadBytes(path));
			if (!result.backupPath.empty()) DeleteFileW(result.backupPath.c_str());
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(ConfigEditorCoreLeavesReadOnlyConfigurationUntouched)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			const std::string original = "[general]\nfullscreen: false\n"
				"[vprenderer.primary]\nquality: high\n";
			WriteBytes(path, original);
			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			Assert::IsTrue(document.SetKnown("general", "fullscreen", "true"));
			Assert::IsTrue(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE);
			ConfigEditorCore::SaveResult result;
			Assert::IsFalse(ConfigEditorCore::SaveSafely(document, result, error));
			Assert::IsTrue(error.find(L"replace") != std::wstring::npos ||
				error.find(L"backup") != std::wstring::npos, error.c_str());
			Assert::IsTrue(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL) != FALSE);
			Assert::AreEqual(original, ReadBytes(path));
			if (!result.backupPath.empty()) DeleteFileW(result.backupPath.c_str());
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(ConfigEditorCoreRoundTripsEveryEditorOwnedKey)
		{
			std::string sourcePath = __FILE__;
			std::replace(sourcePath.begin(), sourcePath.end(), '/', '\\');
			const std::string marker =
				"\\src\\VideoProcessor-Test\\ConfigEditorCoreTests.cpp";
			const size_t markerPosition = sourcePath.rfind(marker);
			Assert::IsTrue(markerPosition != std::string::npos);
			sourcePath.resize(markerPosition);
			sourcePath +=
				"\\test-fixtures\\deployed-VideoProcessor-20260807-current.cfg";
			const std::wstring source(sourcePath.begin(), sourcePath.end());
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			WriteBytes(path, ReadBytes(source));

			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			// The UI migrates this legacy shared-input spelling when its canonical
			// General control is edited.
			document.RemoveKnown("directshow", "video_conversion");

			struct Edit { const char* section; const char* key; const char* value; };
			const Edit edits[] = {
				{ "general", "capture_device", "Test Capture Device" },
				{ "general", "capture_input", "HDMI" },
				{ "general", "renderer", "VideoProcessor Renderer (Alpha)" },
				{ "general", "hide_legacy_renderers", "true" },
				{ "general", "fullscreen", "false" },
				{ "general", "windowed_fullscreen_mode", "false" },
				{ "general", "startminimized", "false" },
				{ "general", "scene_detect", "true" },
				{ "general", "disable_detection_features", "true" },
				{ "general", "scene_correction_basic", "true" },
				{ "general", "fullscreen_monitor_name", "Test Display" },
				{ "general", "video_conversion", "V210_TO_P010" },
				{ "general", "container_colorspace", "BT2020" },
				{ "general", "hdr_colorspace", "FOLLOW_INPUT_LLDV" },
				{ "general", "hdr_luminance", "FOLLOW_INPUT_LLDV" },
				{ "general", "newlldv", "true" },

				{ "queue.low_latency", "shortcut", "Ctrl+Q" },
				{ "queue.low_latency", "when", "${width} >= 1920" },
				{ "queue", "queue_size", "48" },
				{ "queue", "lead_frames", "3" },
				{ "queue", "startup_preroll_frames", "2" },
				{ "queue", "target_frames", "4" },
				{ "queue", "active_picture_lookahead_frames", "1" },
				{ "queue", "reset_after_render_restart_seconds", "6" },
				{ "queue", "reset_queue_too_large_percent", "200" },

				{ "vprenderer.rec709", "shortcut", "Ctrl+R" },
				{ "vprenderer.rec709", "when", "${eotf} == \"SDR\"" },
				{ "vprenderer.rec709", "quality", "balanced" },
				{ "vprenderer.rec709", "output_presentation", "composed" },
				{ "vprenderer.rec709", "output_range", "limited" },
				{ "vprenderer.rec709", "output_gamma", "srgb" },
				{ "vprenderer.rec709", "sdr_input_transfer", "bt1886" },
				{ "vprenderer.rec709", "sdr_target_primaries", "REC709" },
				{ "vprenderer.rec709", "sdr_target_nits", "220" },
				{ "vprenderer.rec709", "sdr_black_nits", "0.005" },
				{ "vprenderer.rec709", "tone_mapping", "bt2390" },
				{ "vprenderer.rec709", "gamut_mapping", "softclip" },
				{ "vprenderer.rec709", "peak_detection", "high_quality" },
				{ "vprenderer.rec709", "contrast_recovery", "0.25" },
				{ "vprenderer.rec709", "upscaler", "ewa_lanczos" },
				{ "vprenderer.rec709", "downscaler", "bicubic" },
				{ "vprenderer.rec709", "deband", "on" },
				{ "vprenderer.rec709", "deband_strength", "light" },
				{ "vprenderer.rec709", "sigmoid", "on" },
				{ "vprenderer.rec709", "dithering", "on" },
				{ "vprenderer.rec709", "lut", "calibration.cube" },
				{ "vprenderer.rec709", "lut_reference_nits", "203" },
				{ "vprenderer.rec709", "lut_reference_range", "full" },
				{ "vprenderer.rec709", "lut_reference_transfer", "2.4" },
				{ "vprenderer.rec709", "lut_reference_primaries", "REC709" },
				{ "vprenderer.rec709", "report_bt2020_to_display", "false" },
				{ "vprenderer.rec709", "switch_refresh_rate", "true" },
				{ "vprenderer.rec709", "output_diagnostics", "true" },
				{ "vprenderer.rec709", "diagnostic_disable_shader_cache", "false" },
				{ "vprenderer.rec709", "default_screen_profile", "normal" },

				{ "vprenderer.viewport.scope", "shortcut", "Ctrl+V" },
				{ "vprenderer.viewport.scope", "when", "${width} >= 1280" },
				{ "vprenderer.viewport", "screen_aspect", "21:10" },
				{ "vprenderer.viewport", "anamorphic_scale", "4:3" },
				{ "vprenderer.viewport", "automatic_crop", "true" },
				{ "vprenderer.viewport", "subtitle_fit", "true" },
				{ "vprenderer.viewport", "subtitle_hold_seconds", "3" },
				{ "vprenderer.viewport", "subtitle_release_drift_seconds", "1" },
				{ "vprenderer.viewport", "subtitle_padding_pixels", "24" },

				{ "directshow", "renderer_start_stop_time_method", "RATIONAL_RATIONAL" },
				{ "directshow", "frame_offset", "75" },
				{ "directshow", "renderer_nominal_range", "FULL" },
				{ "directshow", "renderer_transfer_function", "PQ" },
				{ "directshow", "renderer_transfer_matrix", "BT2020_10" },
				{ "directshow", "renderer_primaries", "BT2020" },

				{ "lldv", "max_cll", "1200" },
				{ "lldv", "max_fall", "450" },
				{ "lldv", "mastering_min_luminance", "0.001" },
				{ "lldv", "mastering_max_luminance", "4000" },

				{ "shader.nls.standard", "shortcut", "Ctrl+N" },
				{ "shader.nls.standard", "when", "${width} >= 1920" },
				{ "shader.nls.standard", "label", "Verified Stretch" },
				{ "shader.nls.standard", "stage", "post_resize" },
				{ "shader.nls.standard", "order", "12" },
				{ "shader.nls.standard", "hlsl_file", "NLS.hlsl" },
				{ "shader.nls.standard", "glsl_file", "NLS.glsl" },
				{ "shader.nls.standard", "quality", "high" },
				{ "shader.nls.standard", "geometry", "classic" },
				{ "shader.nls.standard", "strength", "0.85" },
				{ "shader.nls.standard", "curve", "1.75" },
				{ "shader.nls.standard", "tolerance_percent", "6" },
				{ "shader.nls.protected", "center_protection", "0.4" },

				{ "actions.audio_delay_film", "renderer", "*" },
				{ "actions.audio_delay_film", "on", "refresh.applied,refresh.restored" },
				{ "actions.audio_delay_film", "when", "${actual_refresh} <= 30" },
				{ "actions.audio_delay_film", "run", "C:\\Tools\\verified-action.cmd 42" },

				{ "shortcuts", "config_editor", "Ctrl+E" },
				{ "shortcuts", "fullscreen_toggle", "Ctrl+F" },
				{ "shortcuts", "fullscreen_exit", "Esc" },
				{ "shortcuts", "toggle_stats_overlay", "Ctrl+I" },
				{ "shortcuts", "auto_set", "Ctrl+Shift+A" },
				{ "shortcuts", "pq_set", "Ctrl+Shift+P" },
				{ "shortcuts", "renderer_restart", "Shift+R" },
				{ "shortcuts", "renderer_reset", "R" },
				{ "shortcuts", "capture_1", "Ctrl+1" },
				{ "shortcuts", "capture_2", "Ctrl+2" },
				{ "shortcuts", "capture_3", "Ctrl+3" },
				{ "shortcuts", "capture_4", "Ctrl+4" },
				{ "shortcuts", "video_conversion_off", "V" },
				{ "shortcuts", "video_conversion_p010", "Shift+V" }
			};
			for (const Edit& edit : edits)
				Assert::IsTrue(document.SetKnown(edit.section, edit.key, edit.value));
			ConfigEditorCore::SaveResult result;
			const bool allFieldsSaved = ConfigEditorCore::SaveSafely(document, result, error);
			if (!allFieldsSaved) Logger::WriteMessage(error.c_str());
			Assert::IsTrue(allFieldsSaved, error.c_str());

			ConfigEditorCore::ConfigDocument reloaded;
			Assert::IsTrue(reloaded.Load(path, error), error.c_str());
			for (const Edit& edit : edits)
				Assert::AreEqual(std::string(edit.value),
					reloaded.Get(edit.section, edit.key));
			Assert::IsTrue(ConfigEditorCore::ValidateCandidate(reloaded, error),
				error.c_str());
			DeleteFileW(result.backupPath.c_str());
			DeleteFileW(path.c_str());
		}
	};
}
