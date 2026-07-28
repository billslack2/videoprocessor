#include "pch.h"
#include "CppUnitTest.h"

#include <algorithm>
#include <fstream>

#include <video_frame_formatter/CNoopVideoFrameFormatter.h>
#include <video_frame_formatter/CDeckLinkRGBToP010VideoFrameFormatter.h>
#include <video_frame_formatter/CR210toRGB48VideoFrameFormatter.h>
#include <video_frame_formatter/CR12BtoRGB48VideoFrameFormatter.h>
#include <video_frame_formatter/CV210toP010VideoFrameFormatter.h>
#include <video_frame_formatter/CV210toP210VideoFrameFormatter.h>
#include <IntegerMath.h>
#include <AspectRatio.h>
#include <DisplayRuleExpression.h>
#include <RendererProfileConfig.h>


using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	namespace
	{
		void WriteR10Pixel(BYTE* destination, VideoFrameEncoding encoding,
			uint16_t red, uint16_t green, uint16_t blue)
		{
			const uint32_t word = (static_cast<uint32_t>(red) << 22) |
				(static_cast<uint32_t>(green) << 12) |
				(static_cast<uint32_t>(blue) << 2);
			if (encoding == VideoFrameEncoding::R10l)
			{
				destination[0] = static_cast<BYTE>(word);
				destination[1] = static_cast<BYTE>(word >> 8);
				destination[2] = static_cast<BYTE>(word >> 16);
				destination[3] = static_cast<BYTE>(word >> 24);
			}
			else
			{
				destination[0] = static_cast<BYTE>(word >> 24);
				destination[1] = static_cast<BYTE>(word >> 16);
				destination[2] = static_cast<BYTE>(word >> 8);
				destination[3] = static_cast<BYTE>(word);
			}
		}

		void WriteR12LPixelPair(BYTE* destination,
			uint16_t r0, uint16_t g0, uint16_t b0,
			uint16_t r1, uint16_t g1, uint16_t b1)
		{
			destination[0] = static_cast<BYTE>(r0);
			destination[1] = static_cast<BYTE>((r0 >> 8) | (g0 << 4));
			destination[2] = static_cast<BYTE>(g0 >> 4);
			destination[3] = static_cast<BYTE>(b0);
			destination[4] = static_cast<BYTE>((b0 >> 8) | (r1 << 4));
			destination[5] = static_cast<BYTE>(r1 >> 4);
			destination[6] = static_cast<BYTE>(g1);
			destination[7] = static_cast<BYTE>((g1 >> 8) | (b1 << 4));
			destination[8] = static_cast<BYTE>(b1 >> 4);
		}
	}

	TEST_CLASS(VideoFrameFormatterTests)
	{
	public:

		TEST_METHOD(CNoopVideoFrameFormatterTest)
		{
			CNoopVideoFrameFormatter vff;

			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(1920, 1080, false /* interlaced */, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::V210;  // Actual type not important

			vff.OnVideoState(vs);
			vff.OnVideoState(vs);

			Assert::AreEqual(5529600L, vff.GetOutFrameSize());
		}

		TEST_METHOD(CV210toP010VideoFrameFormatterTest)
		{
			CV210toP010VideoFrameFormatter vff;

			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(1920, 1080, false /* interlaced */, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::V210;  // Actual type not important

			vff.OnVideoState(vs);
			vff.OnVideoState(vs);

			Assert::AreEqual(6220800L, vff.GetOutFrameSize());
		}

		TEST_METHOD(CV210toP210VideoFrameFormatterTest)
		{
			CV210toP210VideoFrameFormatter vff;

			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(1920, 1080, false /* interlaced */, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::V210;  // Actual type not important

			vff.OnVideoState(vs);
			vff.OnVideoState(vs);

			Assert::AreEqual(8294400L, vff.GetOutFrameSize());
		}

		TEST_METHOD(CR210toRGB48VideoFrameFormatterGoldenTest)
		{
			CR210toRGB48VideoFrameFormatter vff;

			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(128, 100, false /* interlaced */, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::R210;

			vff.OnVideoState(vs);

			Assert::AreEqual(76800L, vff.GetOutFrameSize());

			std::vector<BYTE> input(vs->BytesPerFrame(), 0);
			// R210 is a big-endian 32-bit word: padding:2, R:10, G:10, B:10.
			// The first pixel below is R=1, G=2, B=3; the remaining padded rows are black.
			input[0] = 0x00;
			input[1] = 0x10;
			input[2] = 0x08;
			input[3] = 0x03;
			std::vector<BYTE> output(vff.GetOutFrameSize(), 0xFF);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			Assert::AreEqual(static_cast<BYTE>(0x40), output[0]);
			Assert::AreEqual(static_cast<BYTE>(0x00), output[1]);
			Assert::AreEqual(static_cast<BYTE>(0x80), output[2]);
			Assert::AreEqual(static_cast<BYTE>(0x00), output[3]);
			Assert::AreEqual(static_cast<BYTE>(0xC0), output[4]);
			Assert::AreEqual(static_cast<BYTE>(0x00), output[5]);

			// Maximum channel values must map to the full 16-bit endpoint, not 0xFFC0.
			input[0] = 0x3F;
			input[1] = 0xFF;
			input[2] = 0xFF;
			input[3] = 0xFF;
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			for (size_t i = 0; i < 6; ++i)
				Assert::AreEqual(static_cast<BYTE>(0xFF), output[i]);
		}

		TEST_METHOD(DisplayRuleExpressionTest)
		{
			const DisplayRuleExpression::ValueLookup values =
				[](const std::string& name, std::string& value)
				{
					if (name == "eotf") { value = "PQ"; return true; }
					if (name == "key") { value = "Ctrl+F4"; return true; }
					if (name == "source_rate") { value = "23"; return true; }
					if (name == "hdr_metadata") { value = "true"; return true; }
					return false;
				};

			std::string error;
			int specificity = 0;
			Assert::IsTrue(DisplayRuleExpression::Matches(
				"($eotf == PQ || $eotf == HLG) && $source_rate >= 23 && $source_rate < 31",
				values, specificity, error));
			Assert::AreEqual(3, specificity);
			Assert::IsTrue(DisplayRuleExpression::Matches(
				"$source_rate==23-24 && !$hdr_metadata==false", values, specificity, error));
			Assert::IsTrue(DisplayRuleExpression::Matches(
				"$key == \"Ctrl+F4\" || $key == \"Ctrl+F5\"", values, specificity, error));
			Assert::IsFalse(DisplayRuleExpression::Matches(
				"$key == \"Ctrl+F6\"", values, specificity, error));
			Assert::IsFalse(DisplayRuleExpression::Validate("$eotf > PQ", error));
			Assert::IsTrue(error.find("supports only = and !=") != std::string::npos);
			Assert::IsFalse(DisplayRuleExpression::Validate("$unknown == value", error));
			Assert::IsTrue(error.find("unknown variable") != std::string::npos);

			DisplayRuleExpression::Expression compiled;
			Assert::IsTrue(compiled.Compile(
				"$cadence==24000/1001 || $key==\"Ctrl+F5\"", error, true));
			Assert::AreEqual(static_cast<size_t>(1), compiled.KeyChords().size());
			Assert::AreEqual("ctrl+f5",
				ConfigFile::NormalizeName(compiled.KeyChords().front()).c_str());
			Assert::IsTrue(compiled.Matches(
				[](const std::string& name, std::string& value)
				{
					if (name == "cadence") { value = "23.976"; return true; }
					if (name == "key") { value = "none"; return true; }
					return false;
				}, specificity, error));
			Assert::IsFalse(compiled.Compile("$key==F5", error, true));
			Assert::IsFalse(compiled.Compile("$transfer==PQ|HLG", error, true));
		}

		TEST_METHOD(AspectRatioParserAcceptsAndNormalizesDocumentedForms)
		{
			struct Case
			{
				const char* text;
				uint64_t numerator;
				uint64_t denominator;
			};
			for (const Case& test : std::vector<Case>{
				{ "4:3", 4, 3 },
				{ "16:9", 16, 9 },
				{ "16x9", 16, 9 },
				{ "16X9", 16, 9 },
				{ " 16 : 9 ", 16, 9 },
				{ "2:1", 2, 1 },
				{ "2.2:1", 11, 5 },
				{ "2.35:1", 47, 20 },
				{ "1.7777778", 8888889, 5000000 },
				{ "2.35", 47, 20 } })
			{
				AspectRatio aspect;
				std::string error;
				Assert::IsTrue(AspectRatioParser::Parse(
					test.text, 1.0, 4.0, aspect, error),
					std::wstring(error.begin(), error.end()).c_str());
				Assert::AreEqual<uint64_t>(
					test.numerator, aspect.numerator);
				Assert::AreEqual<uint64_t>(
					test.denominator, aspect.denominator);
			}
		}

		TEST_METHOD(AspectRatioParserRejectsMalformedAndOutOfRangeForms)
		{
			for (const char* text : {
				"", "0", "0:1", "1:0", "-16:9", "16:", ":9",
				"16::9", "16x9:1", "16:9junk", "nan", "inf",
				"0.99", "4.01" })
			{
				AspectRatio aspect;
				std::string error;
				Assert::IsFalse(AspectRatioParser::Parse(
					text, 1.0, 4.0, aspect, error));
				Assert::IsFalse(error.empty());
			}
		}

		TEST_METHOD(RendererProfileConfigNormalizesDeprecatedViewportAliases)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0038-alias.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\npersist_profile_selection: false\n";
				for (const char* group : {
					"input", "scaling", "display", "viewport" })
				{
					file << "[profile_groups." << group <<
						"]\nprofiles: base\ndefault: base\n";
					file << "[profiles." << group << ".base]\n";
					if (std::string(group) == "viewport")
						file << "scope_screen_aspect: 2.35:1\n"
							"scope_subtitle_fit: true\n";
				}
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::Read(
				config, model, error));
			Assert::AreEqual(static_cast<size_t>(2), model.warnings.size());
			const auto profile = model.profiles.find("viewport.base");
			Assert::IsTrue(profile != model.profiles.end());
			Assert::AreEqual("2.35:1",
				profile->second.settings.at("screen_aspect").c_str());
			Assert::AreEqual("true",
				profile->second.settings.at("subtitle_fit").c_str());
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigRejectsDuplicateViewportAliases)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0038-duplicate.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n";
				for (const char* group : {
					"input", "scaling", "display", "viewport" })
				{
					file << "[profile_groups." << group <<
						"]\nprofiles: base\ndefault: base\n";
					file << "[profiles." << group << ".base]\n";
					if (std::string(group) == "viewport")
						file << "screen_aspect: 16:9\n"
							"scope_screen_aspect: 2.35:1\n";
				}
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(RendererProfileConfig::Read(
				config, model, error));
			Assert::IsTrue(error.find("defines both deprecated") !=
				std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigRejectsIncompleteUnifiedConfiguration)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) + "VideoProcessor-vp0028-incomplete.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n";
				file << "[profile_groups.input]\nprofiles: sdr\ndefault: auto\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find("profiles.input.sdr") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigRejectsConfigurationVersionKeys)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0028-version.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nconfig_version: 2\n";
			}
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find("unknown key 'config_version'") !=
				std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigReadsOrderedIndependentGroups)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) + "VideoProcessor-vp0028-model.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\npersist_profile_selection: false\n";
				for (const char* group : { "input", "scaling", "display", "viewport" })
				{
					file << "[profile_groups." << group << "]\nprofiles: base\ndefault: auto\n";
					file << "[profiles." << group << ".base]\nwhen: $key==\"F5\"\npriority: 10\n";
				}
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error));
			Assert::AreEqual(static_cast<size_t>(4), model.groups.size());
			Assert::AreEqual("input", model.groups[0].name.c_str());
			Assert::AreEqual("viewport", model.groups[3].name.c_str());
			Assert::IsFalse(model.persistSelection);
			const auto profile = model.profiles.find("display.base");
			Assert::IsTrue(profile != model.profiles.end());
			Assert::AreEqual(10, profile->second.priority);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigOneKeySelectsIndependentGroups)
		{
			RendererProfileConfig::Model model;
			for (const char* groupName : { "input", "display" })
			{
				RendererProfileConfig::Group group;
				group.name = groupName;
				group.profiles = { "selected" };
				model.groups.push_back(group);
				RendererProfileConfig::Profile profile;
				profile.group = groupName;
				profile.name = "selected";
				profile.when = "$key==\"F5\"";
				std::string compileError;
				Assert::IsTrue(profile.whenExpression.Compile(profile.when, compileError, true));
				model.profiles.emplace(std::string(groupName) + ".selected", profile);
			}
			std::vector<RendererProfileConfig::KeySelection> selections;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "F5",
				[](const std::string&, std::string&) { return false; }, selections, error));
			Assert::AreEqual(static_cast<size_t>(2), selections.size());
			Assert::AreEqual("input", selections[0].group.c_str());
			Assert::AreEqual("display", selections[1].group.c_str());
		}

		TEST_METHOD(RendererProfileConfigKeySelectionIgnoresOtherAutomaticBranches)
		{
			RendererProfileConfig::Model model;
			RendererProfileConfig::Group group;
			group.name = "display";
			group.profiles = { "f5", "f6" };
			model.groups.push_back(group);
			for (const auto& definition : std::vector<std::pair<std::string, std::string>>
				{ { "f5", "$transfer==PQ || $key==\"F5\"" },
				  { "f6", "$transfer==PQ || $key==\"F6\"" } })
			{
				RendererProfileConfig::Profile profile;
				profile.group = "display";
				profile.name = definition.first;
				profile.when = definition.second;
				std::string compileError;
				Assert::IsTrue(profile.whenExpression.Compile(profile.when, compileError, true));
				model.profiles.emplace("display." + profile.name, profile);
			}
			std::vector<RendererProfileConfig::KeySelection> selections;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "F5",
				[](const std::string& name, std::string& value)
					{ if (name == "transfer") { value = "PQ"; return true; } return false; },
				selections, error));
			Assert::AreEqual(static_cast<size_t>(1), selections.size());
			Assert::AreEqual("f5", selections[0].profile.c_str());
		}

		TEST_METHOD(RendererProfileConfigResetChordDoesNotSuppressOtherProfileKeys)
		{
			RendererProfileConfig::Model model;
			RendererProfileConfig::Group group;
			group.name = "display";
			group.profiles = { "rec709", "bt2020" };
			group.resetWhen = "$key==\"F4\"";
			std::string resetCompileError;
			Assert::IsTrue(group.resetExpression.Compile(group.resetWhen, resetCompileError, true));
			model.groups.push_back(group);
			for (const auto& definition : std::vector<std::pair<std::string, std::string>>
				{ { "rec709", "$key==\"F5\"" }, { "bt2020", "$key==\"F6\"" } })
			{
				RendererProfileConfig::Profile profile;
				profile.group = group.name;
				profile.name = definition.first;
				profile.when = definition.second;
				std::string compileError;
				Assert::IsTrue(profile.whenExpression.Compile(profile.when, compileError, true));
				model.profiles.emplace(group.name + "." + profile.name, profile);
			}

			std::vector<RendererProfileConfig::KeySelection> selections;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "F5",
				[](const std::string&, std::string&) { return false; }, selections, error));
			Assert::AreEqual(static_cast<size_t>(1), selections.size());
			Assert::AreEqual("rec709", selections[0].profile.c_str());
		}

		TEST_METHOD(RendererProfileConfigCheckedInExamplesPassStartupValidation)
		{
			for (const char* path : {
				"VideoProcessor.cfg",
				"docs\\examples\\VideoProcessorRenderer.unified.proposed.cfg",
				"docs\\examples\\VideoProcessorRenderer.unified.minimal.proposed.cfg",
				"docs\\examples\\VideoProcessorRenderer.from-legacy.proposed.cfg" })
			{
				std::string absolutePath = __FILE__;
				const size_t sourceDirectory = absolutePath.rfind("\\src\\");
				Assert::IsTrue(sourceDirectory != std::string::npos);
				absolutePath.resize(sourceDirectory + 1);
				absolutePath += path;
				ConfigFile config;
				Assert::IsTrue(config.Load(absolutePath));
				RendererProfileConfig::Model model;
				std::string error;
				if (!RendererProfileConfig::Read(config, model, error))
				{
					const std::string detail = std::string(path) + ": " + error;
					Assert::Fail(std::wstring(detail.begin(), detail.end()).c_str());
				}
				if (std::string(path) == "VideoProcessor.cfg" &&
					!MainConfigSchema::Validate(config, error))
				{
					const std::string detail = std::string(path) + ": " + error;
					Assert::Fail(std::wstring(detail.begin(), detail.end()).c_str());
				}
			}
		}

		TEST_METHOD(RendererProfileConfigRejectsWrongOwnerAndUnknownSetting)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) + "VideoProcessor-vp0028-owner.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n";
				for (const char* group : { "input", "scaling", "display", "viewport" })
				{
					file << "[profile_groups." << group << "]\nprofiles: base\ndefault: base\n";
					file << "[profiles." << group << ".base]\n";
					if (std::string(group) == "input") file << "mode: scope\n";
				}
			}
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find("input-owned") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigRejectsMixedLegacyAndUnifiedConfiguration)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) + "VideoProcessor-vp0028-mixed.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n[display_rules]\nrules: old\n";
			}
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find("legacy [display_rules]") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigResolvesAutomaticGroupsDeterministically)
		{
			RendererProfileConfig::Model model;
			RendererProfileConfig::Group input;
			input.name = "input";
			input.profiles = { "sdr", "pq", "pq_specific" };
			input.defaultSelection = "sdr";
			model.groups.push_back(input);
			for (const auto& definition : std::vector<std::pair<std::string, std::string>>
				{ { "sdr", "$transfer==SDR" }, { "pq", "$transfer==PQ" },
				  { "pq_specific", "$transfer==PQ && $width>=3840" } })
			{
				RendererProfileConfig::Profile profile;
				profile.group = "input";
				profile.name = definition.first;
				profile.when = definition.second;
				std::string compileError;
				Assert::IsTrue(profile.whenExpression.Compile(profile.when, compileError, true));
				profile.priority = 100;
				model.profiles.emplace("input." + profile.name, profile);
			}

			std::vector<RendererProfileConfig::AutomaticSelection> selections;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::SelectAutomatic(model,
				[](const std::string& name, std::string& value)
				{
					if (name == "transfer") { value = "PQ"; return true; }
					if (name == "width") { value = "3840"; return true; }
					return false;
				}, selections, error));
			Assert::AreEqual(static_cast<size_t>(1), selections.size());
			Assert::AreEqual("pq_specific", selections[0].profile.c_str());
			Assert::IsFalse(selections[0].configuredDefault);
		}

		TEST_METHOD(CR210toRGB48VideoFrameFormatter4KSmokeTest)
		{
			CR210toRGB48VideoFrameFormatter vff;
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(3840, 2160, false, 60000, 1001);
			vs->videoFrameEncoding = VideoFrameEncoding::R210;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(vs->BytesPerFrame(), 0);
			std::vector<BYTE> output(vff.GetOutFrameSize(), 0xFF);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			for (int i = 0; i < 5; ++i)
				Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			for (int i = 0; i < 30; ++i)
				Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));

			Assert::IsTrue(std::all_of(output.begin(), output.end(),
				[](BYTE value) { return value == 0; }));
			double currentUs = 0.0;
			double averageUs = 0.0;
			double maximumUs = 0.0;
			vff.GetConversionPerformance(currentUs, averageUs, maximumUs);
			wchar_t message[128];
			swprintf_s(message, L"Native R210 4K conversion current/avg/max: %.0f / %.0f / %.0f us",
				currentUs, averageUs, maximumUs);
			Logger::WriteMessage(message);
		}

		TEST_METHOD(CDeckLinkRGBToP010VideoFrameFormatterGoldenTest)
		{
			const VideoFrameEncoding encodings[] = {
				VideoFrameEncoding::R10b,
				VideoFrameEncoding::R10l,
				VideoFrameEncoding::R12L
			};

			for (const auto encoding : encodings)
			{
				CDeckLinkRGBToP010VideoFrameFormatter vff;
				VideoStateComPtr vs = new VideoState();
				vs->valid = true;
				vs->displayMode = std::make_shared<DisplayMode>(104, 100, false, 60000, 1001);
				vs->videoFrameEncoding = encoding;
				vs->colorspace = ColorSpace::REC_709;
				vff.OnVideoState(vs);
				Assert::AreEqual(31200L, vff.GetOutFrameSize());

				std::vector<BYTE> input(vs->BytesPerFrame(), 0);
				for (uint32_t line = 0; line < 100; ++line)
				{
					BYTE* row = input.data() + static_cast<size_t>(line) * vs->BytesPerRow();
					for (uint32_t x = 0; x < 104; x += 2)
					{
						if (encoding == VideoFrameEncoding::R12L)
							WriteR12LPixelPair(row, 4095, 0, 0, 4095, 0, 0);
						else
						{
							WriteR10Pixel(row, encoding, 1023, 0, 0);
							WriteR10Pixel(row + 4, encoding, 1023, 0, 0);
						}
						row += encoding == VideoFrameEncoding::R12L ? 9 : 8;
					}
				}

				std::vector<BYTE> output(vff.GetOutFrameSize(), 0);
				VideoFrame frame(input.data(), 1, 0, nullptr);
				Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
				const uint16_t* words = reinterpret_cast<const uint16_t*>(output.data());
				for (size_t i = 0; i < 104ULL * 100; ++i)
					Assert::AreEqual(217U << 6, static_cast<unsigned int>(words[i]));
				for (size_t i = 104ULL * 100; i < 104ULL * 150; i += 2)
				{
					Assert::AreEqual(395U << 6, static_cast<unsigned int>(words[i]));
					Assert::AreEqual(1023U << 6, static_cast<unsigned int>(words[i + 1]));
				}
			}
		}

		TEST_METHOD(CDeckLinkRGBToP010VideoFrameFormatter4KSmokeTest)
		{
			const VideoFrameEncoding encodings[] = {
				VideoFrameEncoding::R10b,
				VideoFrameEncoding::R10l,
				VideoFrameEncoding::R12L
			};
			for (const auto encoding : encodings)
			{
				CDeckLinkRGBToP010VideoFrameFormatter vff;
				VideoStateComPtr vs = new VideoState();
				vs->valid = true;
				vs->displayMode = std::make_shared<DisplayMode>(3840, 2160, false, 60000, 1001);
				vs->videoFrameEncoding = encoding;
				vs->colorspace = ColorSpace::BT_2020;
				vff.OnVideoState(vs);

				std::vector<BYTE> input(vs->BytesPerFrame(), 0);
				std::vector<BYTE> output(vff.GetOutFrameSize(), 0xFF);
				VideoFrame frame(input.data(), 1, 0, nullptr);
				for (int i = 0; i < 3; ++i)
					Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
				for (int i = 0; i < 15; ++i)
					Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));

				const uint16_t* words = reinterpret_cast<const uint16_t*>(output.data());
				Assert::AreEqual(0U, static_cast<unsigned int>(words[0]));
				Assert::AreEqual(0U, static_cast<unsigned int>(words[3840ULL * 2160 - 1]));
				Assert::AreEqual(512U << 6, static_cast<unsigned int>(words[3840ULL * 2160]));
				Assert::AreEqual(512U << 6, static_cast<unsigned int>(words[3840ULL * 2160 + 1]));

				double currentUs = 0.0;
				double averageUs = 0.0;
				double maximumUs = 0.0;
				vff.GetConversionPerformance(currentUs, averageUs, maximumUs);
				wchar_t message[160];
				swprintf_s(message, L"Packed RGB %s 4K to P010 current/avg/max: %.0f / %.0f / %.0f us",
					ToString(encoding), currentUs, averageUs, maximumUs);
				Logger::WriteMessage(message);
			}
		}

		TEST_METHOD(CR12BtoRGB48VideoFrameFormatterGoldenBlockTest)
		{
			CR12BtoRGB48VideoFrameFormatter vff;

			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(104, 100, false /* interlaced */, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::R12B;

			vff.OnVideoState(vs);

			Assert::AreEqual(62400L, vff.GetOutFrameSize());

			const BYTE input[] = {
				0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
				0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
				0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
				0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
				0x20, 0x21, 0x22, 0x23
			};
			const BYTE expected[] = {
				0x32, 0x20, 0x00, 0x01, 0x07, 0x70, 0x00, 0x06,
				0x54, 0x40, 0x00, 0x0B, 0xA9, 0x90, 0x00, 0x08,
				0xFE, 0xE0, 0x00, 0x0D, 0xC3, 0x30, 0x11, 0x12,
				0x10, 0x01, 0x11, 0x17, 0x65, 0x51, 0x11, 0x14,
				0xBA, 0xA1, 0x11, 0x19, 0x8F, 0xF1, 0x11, 0x1E,
				0xDC, 0xC1, 0x12, 0x23, 0x21, 0x12, 0x22, 0x20
			};
			std::vector<BYTE> inputFrame(104 * 100 * 36 / 8, 0);
			memcpy(inputFrame.data(), input, sizeof(input));
			std::vector<BYTE> output(vff.GetOutFrameSize(), 0);
			VideoFrame frame(inputFrame.data(), 1, 0, nullptr);

			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			for (size_t i = 0; i < sizeof(expected); ++i)
				Assert::AreEqual(expected[i], output[i]);
		}

		TEST_METHOD(CR12BtoRGB48VideoFrameFormatterBlackWhiteTest)
		{
			CR12BtoRGB48VideoFrameFormatter vff;
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(104, 100, false, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::R12B;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(104 * 100 * 36 / 8, 0);
			std::vector<BYTE> output(vff.GetOutFrameSize(), 0);
			VideoFrame blackFrame(input.data(), 1, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(blackFrame, output.data()));
			for (BYTE value : output)
				Assert::AreEqual(static_cast<BYTE>(0), value);

			memset(input.data(), 0xFF, input.size());
			VideoFrame whiteFrame(input.data(), 2, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(whiteFrame, output.data()));
			for (BYTE value : output)
				Assert::AreEqual(static_cast<BYTE>(0xFF), value);
		}

		TEST_METHOD(CR12BtoRGB48VideoFrameFormatterRejectsInvalidWidth)
		{
			CR12BtoRGB48VideoFrameFormatter vff;
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(101, 100, false, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::R12B;

			Assert::ExpectException<std::runtime_error>([&]() { vff.OnVideoState(vs); });
		}

		TEST_METHOD(CR12BtoRGB48VideoFrameFormatter4KSmokeTest)
		{
			CR12BtoRGB48VideoFrameFormatter vff;
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(3840, 2160, false, 60000, 1001);
			vs->videoFrameEncoding = VideoFrameEncoding::R12B;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(vs->BytesPerFrame(), 0);
			std::vector<BYTE> output(vff.GetOutFrameSize(), 0xFF);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			// Warm the reusable worker, then sample enough frames to expose scheduling spikes.
			for (int i = 0; i < 5; ++i)
				Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			for (int i = 0; i < 30; ++i)
				Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			// Checking the whole frame also covers the split-row boundary used by the worker.
			Assert::IsTrue(std::all_of(output.begin(), output.end(),
				[](BYTE value) { return value == 0; }));

			double currentUs = 0.0;
			double averageUs = 0.0;
			double maximumUs = 0.0;
			vff.GetConversionPerformance(currentUs, averageUs, maximumUs);
			wchar_t message[128];
			swprintf_s(message, L"Native R12B 4K conversion current/avg/max: %.0f / %.0f / %.0f us",
				currentUs, averageUs, maximumUs);
			Logger::WriteMessage(message);
		}
	};

	TEST_CLASS(U64MulDivTests)
	{
	public:

		TEST_METHOD(U64MulDivBasicRoundingTest)
		{
			// Test basic rounding behavior
			// 7 / 2 = 3.5, should round to 4
			Assert::AreEqual(4ULL, U64_MulDiv(7, 1, 2));
			
			// 6 / 2 = 3.0, should remain 3
			Assert::AreEqual(3ULL, U64_MulDiv(6, 1, 2));
			
			// 5 / 2 = 2.5, should round to 3
			Assert::AreEqual(3ULL, U64_MulDiv(5, 1, 2));
			
			// 4 / 2 = 2.0, should remain 2  
			Assert::AreEqual(2ULL, U64_MulDiv(4, 1, 2));
		}

		TEST_METHOD(U64MulDivZeroDivisorTest)
		{
			// Test zero divisor handling
			Assert::AreEqual(0ULL, U64_MulDiv(100, 50, 0));
		}

		TEST_METHOD(U64MulDivExactDivisionTest)
		{
			// Test exact divisions (no rounding needed)
			Assert::AreEqual(50ULL, U64_MulDiv(100, 1, 2));
			Assert::AreEqual(25ULL, U64_MulDiv(100, 1, 4));
			Assert::AreEqual(10ULL, U64_MulDiv(100, 1, 10));
		}

		TEST_METHOD(U64MulDivPPMTimingTest)
		{
			// Test PPM timing correction scenarios
			// Simulate 1 million ticks with 6 PPM correction: 1000006 / 1000000
			// Should be very close to 1000006 but rounded properly
			uint64_t result = U64_MulDiv(1000000, 1000006, 1000000);
			Assert::AreEqual(1000006ULL, result);

			// Test fractional PPM correction
			// 1000000 * 1000003 / 1000000 = 1000003.000, exact
			result = U64_MulDiv(1000000, 1000003, 1000000);
			Assert::AreEqual(1000003ULL, result);

			// Test case where rounding matters for PPM
			// Simulate: frameIndex * ticksPerSec / timeScale with PPM
			// 1 * 10000000 * 1001 / 24000 / 1000000 * 1000006
			// This creates fractional values where rounding is critical
			uint64_t frameIndex = 1;
			uint64_t ticksPerSec = 10000000;
			uint64_t frameDurationTicks = 1001;
			uint64_t timeScale = 24000;
			uint64_t trimNum = 1000006;
			uint64_t trimDen = 1000000;

			uint64_t t = frameIndex;
			t = U64_MulDiv(t, ticksPerSec, 1); // t = 10000000
			t = U64_MulDiv(t, frameDurationTicks, timeScale); // t ? 417083
			t = U64_MulDiv(t, trimNum, trimDen); // Apply PPM correction

			// Verify we get a reasonable result (exact value depends on rounding)
			// But should be close to 417083 * 1.000006 ? 417085
			Assert::IsTrue(t >= 417084ULL && t <= 417086ULL);
		}

		TEST_METHOD(U64MulDivLargeNumberTest)
		{
			// Test with large numbers to verify no overflow
			uint64_t large = 0x100000000ULL; // 2^32
			uint64_t result = U64_MulDiv(large, large, large);
			Assert::AreEqual(large, result);

			// Test near overflow conditions
			uint64_t veryLarge = 0x7FFFFFFFFFFFFFFFULL / 1000; // Close to max / 1000
			result = U64_MulDiv(veryLarge, 999, 1000);
			// Should be approximately veryLarge - veryLarge/1000
			Assert::IsTrue(result > 0);
		}

		TEST_METHOD(U64MulDivTimingAccuracyTest)
		{
			// Test timing accuracy for common video frame rates
			// 23.976 fps: timeScale=24000, frameDurationTicks=1001
			uint64_t ticksPerSec = 10000000; // 100ns ticks per second

			// Frame 0: should be 0
			uint64_t t0 = U64_MulDiv(0, ticksPerSec, 1);
			t0 = U64_MulDiv(t0, 1001, 24000);
			Assert::AreEqual(0ULL, t0);

			// Frame 1: 10,000,000 * 1001 / 24000 = 417083.333...
			uint64_t t1 = U64_MulDiv(1, ticksPerSec, 1);
			t1 = U64_MulDiv(t1, 1001, 24000);
			Assert::AreEqual(417083ULL, t1);

			// Frame 2: 20,000,000 * 1001 / 24000 = 834166.666...
			uint64_t t2 = U64_MulDiv(2, ticksPerSec, 1);
			t2 = U64_MulDiv(t2, 1001, 24000);
			Assert::AreEqual(834167ULL, t2);
		}
	};
}
