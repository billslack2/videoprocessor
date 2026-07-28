#include "pch.h"

#include <ConfigFile.h>
#include <MainConfigSchema.h>
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
					"[general]\npersist_profile_selection: true\n";
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
	};
}
