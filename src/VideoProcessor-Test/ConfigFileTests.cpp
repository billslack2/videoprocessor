#include "pch.h"

#include <ConfigFile.h>
#include <MainConfigSchema.h>
#include <RendererConfigView.h>
#include <RendererProfileConfig.h>
#include "CppUnitTest.h"

#include <fstream>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
	TEST_CLASS(ConfigFileTests)
	{
	public:
		TEST_METHOD(IndexedShortcutKeyParsesOneBasedIndex)
		{
			unsigned int index = 0;
			Assert::IsTrue(ConfigFile::TryParseIndexedKey(
				"Render.12",
				"render",
				index));
			Assert::AreEqual(12u, index);
		}

		TEST_METHOD(IndexedShortcutKeyRejectsZeroAndMalformedValues)
		{
			unsigned int index = 99;
			Assert::IsFalse(ConfigFile::TryParseIndexedKey(
				"render.0",
				"render",
				index));
			Assert::IsFalse(ConfigFile::TryParseIndexedKey(
				"render.madvr",
				"render",
				index));
			Assert::IsFalse(ConfigFile::TryParseIndexedKey(
				"renderer.1",
				"render",
				index));
		}

		TEST_METHOD(IndexedShortcutKeyRejectsOverflow)
		{
			unsigned int index = 0;
			Assert::IsFalse(ConfigFile::TryParseIndexedKey(
				"render.999999999999999999999999999",
				"render",
				index));
		}

		TEST_METHOD(DuplicateKeyWarnsAndLastValueWins)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path =
				std::string(temporaryDirectory) + "VideoProcessor-duplicate-config-test.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[display]\ncontrast_recovery: 0\ncontrast_recovery: AUTO\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string value;
			Assert::IsTrue(config.TryGetString("display", "contrast_recovery", value));
			Assert::AreEqual("AUTO", value.c_str());
			Assert::AreEqual(static_cast<size_t>(1), config.GetWarnings().size());
			Assert::IsTrue(
				config.GetWarnings().front().find("duplicate [display] key 'contrast_recovery'") !=
				std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(DisplayRuleLutValuesPreserveBasePathAndBlankOverride)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path =
				std::string(temporaryDirectory) + "VideoProcessor-display-lut-config-test.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[display]\n"
					"lut: lut\\base-calibration.cube\n"
					"[display_rules.rec709]\n"
					"lut:\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string baseLut;
			std::string ruleLut;
			Assert::IsTrue(config.TryGetString("display", "lut", baseLut));
			Assert::IsTrue(config.TryGetString("display_rules.rec709", "lut", ruleLut));
			Assert::AreEqual("lut\\base-calibration.cube", baseLut.c_str());
			Assert::IsTrue(ruleLut.empty());
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(CommandLineConfigOptionAcceptsSeparateAndEqualsValues)
		{
			std::string value;
			std::string error;
			Assert::IsTrue(ConfigFile::TryParseCommandLineOption(
				{ "VideoProcessor.exe", "--config", "C:\\VP Test\\main.cfg" },
				"--config",
				value,
				error));
			Assert::AreEqual("C:\\VP Test\\main.cfg", value.c_str());
			Assert::IsTrue(error.empty());

			Assert::IsTrue(ConfigFile::TryParseCommandLineOption(
				{ "VideoProcessor.exe", "--vr_config=renderer-test.cfg" },
				"--vr_config",
				value,
				error));
			Assert::AreEqual("renderer-test.cfg", value.c_str());
			Assert::IsTrue(error.empty());
		}

		TEST_METHOD(CommandLineConfigOptionIsCaseInsensitive)
		{
			std::string value;
			std::string error;
			Assert::IsTrue(ConfigFile::TryParseCommandLineOption(
				{ "VideoProcessor.exe", "/VR_CONFIG", "renderer.cfg" },
				"--vr_config",
				value,
				error));
			Assert::AreEqual("renderer.cfg", value.c_str());
			Assert::IsTrue(error.empty());
		}

		TEST_METHOD(CommandLineConfigOptionRejectsMissingValue)
		{
			std::string value;
			std::string error;
			Assert::IsFalse(ConfigFile::TryParseCommandLineOption(
				{ "VideoProcessor.exe", "--vr_config", "--fullscreen" },
				"--vr_config",
				value,
				error));
			Assert::IsTrue(error.find("Missing value") != std::string::npos);
		}

		TEST_METHOD(RendererConfigSelectionDefaultsToPrimaryConfig)
		{
			std::string filename;
			std::string error;
			bool explicitSelection = true;
			bool compatibilityOverride = true;
			Assert::IsTrue(ConfigFile::TryResolveRendererConfigSelection(
				{ "VideoProcessor.exe" },
				filename,
				explicitSelection,
				compatibilityOverride,
				error));
			Assert::AreEqual(
				ConfigFile::DEFAULT_FILENAME, filename.c_str());
			Assert::IsFalse(explicitSelection);
			Assert::IsFalse(compatibilityOverride);
			Assert::IsTrue(error.empty());
		}

		TEST_METHOD(RendererConfigSelectionFollowsPrimaryConfig)
		{
			std::string filename;
			std::string error;
			bool explicitSelection = false;
			bool compatibilityOverride = true;
			Assert::IsTrue(ConfigFile::TryResolveRendererConfigSelection(
				{ "VideoProcessor.exe", "--config", "combined.cfg" },
				filename,
				explicitSelection,
				compatibilityOverride,
				error));
			Assert::AreEqual("combined.cfg", filename.c_str());
			Assert::IsTrue(explicitSelection);
			Assert::IsFalse(compatibilityOverride);
		}

		TEST_METHOD(RendererConfigSelectionExplicitOverrideWins)
		{
			std::string filename;
			std::string error;
			bool explicitSelection = false;
			bool compatibilityOverride = false;
			Assert::IsTrue(ConfigFile::TryResolveRendererConfigSelection(
				{ "VideoProcessor.exe", "--config=combined.cfg",
				  "--vr_config", "renderer.cfg" },
				filename,
				explicitSelection,
				compatibilityOverride,
				error));
			Assert::AreEqual("renderer.cfg", filename.c_str());
			Assert::IsTrue(explicitSelection);
			Assert::IsTrue(compatibilityOverride);
		}

		TEST_METHOD(CommandLineConfigOptionRejectsDuplicates)
		{
			std::string value;
			std::string error;
			Assert::IsFalse(ConfigFile::TryParseCommandLineOption(
				{
					"VideoProcessor.exe",
					"--config", "first.cfg",
					"--config=second.cfg"
				},
				"--config",
				value,
				error));
			Assert::IsTrue(error.find("Duplicate") != std::string::npos);
		}

		TEST_METHOD(ColonAssignmentsPreserveEqualityExpressions)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-colon-config-test.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[profile]\n"
					"when: $key==\"F5\"\n"
					"quality: high\n";
			}
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string value;
			Assert::IsTrue(config.TryGetString("profile", "when", value));
			Assert::AreEqual("$key==\"F5\"", value.c_str());
			Assert::IsTrue(config.TryGetString("profile", "quality", value));
			Assert::AreEqual("high", value.c_str());
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(LegacyEqualsAssignmentsRemainReadable)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-equals-compatibility-test.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[command_line]\nqueue_size=32\n";
			}
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string value;
			Assert::IsTrue(config.TryGetString(
				"command_line", "queue_size", value));
			Assert::AreEqual("32", value.c_str());
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(MainConfigSchemaUsesSharedTypedValidation)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-main-schema-valid.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[command_line]\n"
					"renderer: VideoProcessor Renderer (Alpha)\n"
					"queue_size: 32\n"
					"alpha_queue_size: 1\n"
					"fullscreen: true\n"
					"scene_correction_basic: false\n"
					"frame_offset: AUTO\n"
					"[queue_recovery]\n"
					"reset_after_render_restart_seconds: 3\n"
					"reset_queue_too_large_percent: 70\n"
					"[lldv]\n"
					"max_cll: 1000\n"
					"mastering_min_luminance: 0.001\n"
					"mastering_max_luminance: 4000\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.empty());
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(MainAndRendererSchemasAcceptOneCombinedConfig)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-combined-schema.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[command_line]\n"
					"renderer: VideoProcessor Renderer (Alpha)\n"
					"alpha_queue_size: 1\n"
					"[shortcuts]\nrender.6: A\n"
					"[general]\npersist_profile_selection: true\n"
					"[vpvr.display]\nquality: high\n"
					"[vpvr.general]\nswitch_refresh_rate: true\n";
				for (const char* group :
					{ "input", "scaling", "display", "viewport" })
				{
					file << "[profile_groups." << group << "]\n"
						"profiles: base\n"
						"default: base\n"
						"[profiles." << group << ".base]\n";
				}
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error));
			RendererProfileConfig::Model model;
			Assert::IsTrue(
				RendererProfileConfig::Read(config, model, error));
			Assert::AreEqual(static_cast<size_t>(4), model.groups.size());
			Assert::IsTrue(model.warnings.empty());
			Assert::IsTrue(MainConfigSchema::OwnsSection("command_line"));
			Assert::IsTrue(RendererProfileConfig::OwnsSection(
				"profiles.display.base"));
			Assert::IsFalse(MainConfigSchema::OwnsSection("unknown"));
			Assert::IsFalse(RendererProfileConfig::OwnsSection("unknown"));
			Assert::AreEqual(
				(path.substr(0, path.size() - 4) + ".state").c_str(),
				RendererProfileConfig::StatePath(config).c_str());
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(MainConfigSchemaRejectsForeignAndIllTypedKeys)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-main-schema-invalid.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[command_line]\nalpha_queue_size: 0\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("alpha_queue_size") != std::string::npos);

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[command_line]\nqueue_size: 0\n"
					"[queue_recovery]\nreset_queue_too_large_percent: 101\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("queue_size") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(MainConfigSchemaOwnsOnlyTheKnownLoggingKey)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-logging-schema.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				// Value validation intentionally belongs to the non-fatal
				// startup resolver, not the strict main schema.
				file << "[logging]\ndebug_log_retention: invalid\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsTrue(MainConfigSchema::OwnsSection("logging"));
			Assert::IsTrue(MainConfigSchema::Validate(config, error));

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[logging]\nunknown_logging_key: 10\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(
				error.find("unknown_logging_key") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererConfigViewReadsCanonicalNamespace)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vpvr-canonical.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n"
					"persist_profile_selection: true\n"
					"event_action_delay_seconds: 5\n"
					"[vpvr.display]\n"
					"quality: high\n"
					"sdr_target_nits: 100\n"
					"[vpvr.general]\n"
					"switch_refresh_rate: false\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererConfigView view(config);
			std::string error;
			std::vector<std::string> warnings;
			Assert::IsTrue(view.Validate(error, warnings));
			Assert::IsTrue(error.empty());
			Assert::IsTrue(warnings.empty());
			std::string quality;
			bool switchRefreshRate = true;
			Assert::IsTrue(view.TryGetDisplayString("quality", quality));
			Assert::AreEqual("high", quality.c_str());
			Assert::IsTrue(view.TryGetPolicyBool(
				"switch_refresh_rate", switchRefreshRate));
			Assert::IsFalse(switchRefreshRate);
			Assert::IsTrue(RendererProfileConfig::OwnsSection(
				"vpvr.display"));
			Assert::IsTrue(RendererProfileConfig::OwnsSection(
				"vpvr.general"));
			Assert::IsFalse(RendererProfileConfig::OwnsSection(
				"vpvr.unknown"));
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererConfigViewRejectsCanonicalDisplayCoexistence)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vpvr-display-conflict.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[vpvr.display]\nquality: high\n"
					"[display]\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			std::vector<std::string> warnings;
			Assert::IsFalse(
				RendererConfigView(config).Validate(error, warnings));
			Assert::IsTrue(error.find("vpvr.display") != std::string::npos);
			Assert::IsTrue(error.find("[display]") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererConfigViewRejectsPolicyKeyDuplicate)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vpvr-policy-conflict.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n"
					"persist_profile_selection: true\n"
					"switch_refresh_rate: true\n"
					"[vpvr.general]\n"
					"switch_refresh_rate: true\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			std::vector<std::string> warnings;
			Assert::IsFalse(
				RendererConfigView(config).Validate(error, warnings));
			Assert::IsTrue(
				error.find("switch_refresh_rate") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererConfigViewPreservesLegacyPrecedence)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vpvr-legacy-precedence.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[libplacebo]\n"
					"quality: fast\n"
					"sdr_target_nits: 80\n"
					"switch_refresh_rate: false\n"
					"[display]\n"
					"quality: high\n"
					"[general]\n"
					"switch_refresh_rate: true\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererConfigView view(config);
			std::string error;
			std::vector<std::string> warnings;
			Assert::IsTrue(view.Validate(error, warnings));
			Assert::AreEqual(static_cast<size_t>(3), warnings.size());
			std::string value;
			bool policy = false;
			Assert::IsTrue(view.TryGetDisplayString("quality", value));
			Assert::AreEqual("high", value.c_str());
			Assert::IsTrue(view.TryGetDisplayString(
				"sdr_target_nits", value));
			Assert::AreEqual("80", value.c_str());
			Assert::IsTrue(view.TryGetPolicyBool(
				"switch_refresh_rate", policy));
			Assert::IsTrue(policy);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererConfigViewWarnsOnlyForConsumedLegacySections)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vpvr-unused-legacy.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[display]\n"
					"quality: high\n"
					"[libplacebo]\n"
					"quality: fast\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			std::vector<std::string> warnings;
			Assert::IsTrue(
				RendererConfigView(config).Validate(error, warnings));
			Assert::AreEqual(static_cast<size_t>(1), warnings.size());
			Assert::IsTrue(
				warnings.front().find("[display]") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererConfigRejectsWrongCanonicalOwnership)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vpvr-wrong-owner.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[vpvr.display]\n"
					"switch_refresh_rate: true\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(
				RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(
				error.find("switch_refresh_rate") != std::string::npos);
			DeleteFileA(path.c_str());
		}
	};
}
