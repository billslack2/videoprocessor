#include "pch.h"

#include <ConfigFile.h>
#include <DisplayTopologySession.h>
#include <EventActionLauncher.h>
#include <MainConfigSchema.h>
#include <microsoft_directshow/MadVRShaderLoader.h>
#include <microsoft_directshow/video_renderers/DirectShowVideoRenderers.h>
#include <RendererConfigView.h>
#include <RendererProfileConfig.h>
#include <UnifiedProfileRuntime.h>
#include "CppUnitTest.h"

#include <fstream>
#include <map>
#include <sstream>
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

		TEST_METHOD(LegacyRendererPolicyDefaultsToHiddenAndAcceptsExplicitFalse)
		{
			ConfigFile missingConfiguration;
			Assert::IsTrue(DirectShowHideLegacyRenderers(missingConfiguration));

			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path =
				std::string(temporaryDirectory) + "VideoProcessor-legacy-renderer-policy-test.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nhide_legacy_renderers: false\n";
			}

			ConfigFile configuration;
			Assert::IsTrue(configuration.Load(path));
			Assert::IsFalse(DirectShowHideLegacyRenderers(configuration));
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(LegacyRendererPolicyFallsBackToHiddenForMalformedValue)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path =
				std::string(temporaryDirectory) + "VideoProcessor-legacy-renderer-invalid-policy-test.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nhide_legacy_renderers: unexpected\n";
			}

			ConfigFile configuration;
			Assert::IsTrue(configuration.Load(path));
			Assert::IsTrue(DirectShowHideLegacyRenderers(configuration));
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(IndexedShortcutKeyRejectsZeroAndMalformedValues)
		{
			unsigned int index = 99;
			Assert::IsFalse(ConfigFile::TryParseIndexedKey(
				"render.0",
				"render",
				index));
			Assert::IsFalse(ConfigFile::TryParseIndexedKey(
				"render.external",
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
					"fullscreen: true\n"
					"disable_detection_features: true\n"
					"scene_correction_basic: false\n"
					"frame_offset: AUTO\n"
					"[queue]\n"
					"queue_size: 32\n"
					"lead_frames: 1\n"
					"target_frames: 2\n"
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

		TEST_METHOD(MainConfigSchemaValidatesTargetOnlyDisplaySessionMode)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-display-session-schema.cfg";
			ConfigFile config;
			std::string error;
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n"
					"fullscreen_monitor_name: EPSON PJ\n"
					"fullscreen_monitor_session_mode: target-only\n"
					"[vprenderer]\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsTrue(MainConfigSchema::Validate(config, error));
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n"
					"fullscreen_monitor_session_mode: dangerous\n"
					"[vprenderer]\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("fullscreen_monitor_session_mode") !=
				std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(DisplayRecoveryDetectionIsStateFileScoped)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-display-recovery-detection.state";
			DeleteFileA(path.c_str());
			Assert::IsFalse(
				DisplayTopologySession::HasPendingRecovery(path));
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "# Managed by VideoProcessor.\n"
					"profile.viewport: scope\n"
					"display_recovery.v1: DEADBEEF\n";
			}
			Assert::IsTrue(
				DisplayTopologySession::HasPendingRecovery(path));
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
			Assert::IsTrue(MainConfigSchema::OwnsSection("queue"));
			Assert::IsTrue(MainConfigSchema::OwnsSection("directshow"));
			Assert::IsTrue(RendererProfileConfig::OwnsSection(
				"profiles.display.base"));
			Assert::IsFalse(MainConfigSchema::OwnsSection("unknown"));
			Assert::IsFalse(RendererProfileConfig::OwnsSection("unknown"));
			Assert::AreEqual(
				(path.substr(0, path.size() - 4) + ".state").c_str(),
				RendererProfileConfig::StatePath(config).c_str());
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(Vp0079OwnerVariantsResolveWithoutPersistedProfileState)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0079-config.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n"
					"renderer: VideoProcessor Renderer (Alpha)\n"
					"fullscreen: true\n"
					"[renderer_alias]\nvp: 1\nmadvr: 2\n"
					"[queue]\nwhen: $key==\"l\"\nqueue_size: 32\nlead_frames: 4\ntarget_frames: 3\nactive_picture_lookahead_frames: 2\n"
					"[queue.low_latency]\nwhen: $key==\"L\"\nqueue_size: 1\ntarget_frames: 1\n"
					"[directshow]\nvideo_conversion: V210_TO_P010\nframe_offset: 90\n"
					"[directshow.conversion]\nconversion_method: SIMD\nmin_core_count: 1\nmax_core_count: 2\n"
					"[directshow.ppm]\nppm: -17\n"
					"[vprenderer]\nwhen: $key==\"F4\"\nquality: high\nswitch_refresh_rate: true\n"
					"[vprenderer.rec709]\nwhen: $key==\"F5\"\ntone_mapping: spline\n"
					"[vprenderer.viewport]\nwhen: $key==\"F3\"\n"
					"[vprenderer.viewport.scope]\nwhen: $key==\"F2\"\nscreen_aspect: 2.35:1\nautomatic_crop: true\nsubtitle_fit: true\n"
					"[actions.audio_delay_film]\non: refresh.applied,refresh.confirmed\nwhen: $actual_refresh<=30\nrun: C:\\Videoprocessor\\audio\\audio_delay.bat 100\n"
					"[shader.nls]\nwhen: $key==\"n\"\n"
					"[shader.nls.standard]\nwhen: $key==\"Shift+n\"\nshader_type: nls\nglsl_file: NLS.glsl\n"
					"[shader.cleanup]\ntype: multi\nwhen: $key==\"d\"\n"
					"[shader.cleanup.deband]\nwhen: $key==\"D\"\nshader_type: custom\nhlsl_file: Deband.hlsl\nstage: pre_resize\norder: 30\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());
			RendererProfileConfig::Model model;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsFalse(model.persistSelection);
			Assert::AreEqual(static_cast<size_t>(3), model.groups.size());

			std::vector<RendererProfileConfig::KeySelection> selections;
			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "L",
				[](const std::string&, std::string&) { return false; },
				selections, error));
			Assert::AreEqual(static_cast<size_t>(1), selections.size());
			Assert::AreEqual("queue", selections.front().group.c_str());
			Assert::AreEqual("low_latency", selections.front().profile.c_str());

			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "F2",
				[](const std::string&, std::string&) { return false; },
				selections, error));
			Assert::AreEqual("viewport", selections.front().group.c_str());
			Assert::AreEqual("scope", selections.front().profile.c_str());
			RendererProfileConfig::ResolvedViewport viewport;
			Assert::IsTrue(RendererProfileConfig::ResolveViewport(model,
				"scope", 1, viewport, error));
			Assert::AreEqual(47ull, viewport.screenAspect.numerator);
			Assert::AreEqual(20ull, viewport.screenAspect.denominator);
			Assert::IsTrue(viewport.automaticCrop);
			Assert::IsTrue(viewport.subtitleFit);

			UnifiedProfileRuntime::Runtime runtime;
			Assert::IsTrue(runtime.Initialize(config,
				[](const std::string&, std::string&) { return false; }, error));
			Assert::IsTrue(runtime.StatePath().empty());
			UnifiedProfileRuntime::SelectionResult result;
			Assert::IsTrue(runtime.SelectKey("L",
				[](const std::string&, std::string&) { return false; },
				result, error));
			Assert::IsTrue(result.changed);
			Assert::IsTrue(result.snapshot->queue.hasQueueSize);
			Assert::AreEqual(static_cast<size_t>(1),
				result.snapshot->queue.queueSize);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(UnifiedActionsPublishCommittedSourceAndProfileEvents)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-unified-actions.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nrenderer: VideoProcessor Renderer (Alpha)\n"
					"[vprenderer]\nquality: high\n"
					"[vprenderer.viewport]\nscreen_aspect: 16:9\n"
					"[vprenderer.viewport.scope]\nwhen: $eotf==\"pq\"\n"
					"screen_aspect: 2.35:1\nautomatic_crop: true\n"
					"[actions.committed_scope_pq]\n"
					"on: state.committed\n"
					"when: $eotf==\"pq\" && $profile.viewport==\"scope\" && $previous.eotf==\"sdr\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n"
					"[actions.eotf_pq]\n"
					"on: source.eotf.changed\n"
					"when: $eotf==\"pq\" && $previous.eotf==\"sdr\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n"
					"[actions.eotf_context]\n"
					"on: source.eotf.changed\n"
					"when: $primaries==\"rec709\"\n"
					"run: C:\\Windows\\System32\\cmd.exe ${eotf} ${primaries} ${previous.eotf} ${profile.viewport} ${event} ${event_reason}\n"
					"[actions.rec709]\n"
					"on: source.primaries.changed\n"
					"when: $primaries==\"rec709\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n"
					"[actions.scope_entered]\n"
					"on: profile.viewport.changed\n"
					"when: $profile.viewport==\"scope\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n"
					"[actions.scope_left]\n"
					"on: profile.viewport.changed\n"
					"when: $previous_profile.viewport==\"scope\" && $profile.viewport==\"base\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n"
					"[actions.renderer_scope_ready]\n"
					"on: renderer.ready\n"
					"when: $event_reason==\"renderer_ready\" && $profile.viewport==\"scope\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			RendererProfileConfig::Model model;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::AreEqual(static_cast<size_t>(7), model.actions.size());

			auto source = [](const char* eotf, const char* primaries)
			{
				return [eotf, primaries](const std::string& name,
					std::string& value)
				{
					if (name == "eotf" || name == "transfer")
					{
						value = eotf;
						return true;
					}
					if (name == "primaries" || name == "colorspace")
					{
						value = primaries;
						return true;
					}
					return false;
				};
			};
			auto hasInvocation = [](const std::vector<
				UnifiedProfileRuntime::ActionInvocation>& actions,
				const char* actionName, const char* event)
			{
				return std::any_of(actions.begin(), actions.end(),
					[actionName, event](const UnifiedProfileRuntime::ActionInvocation& action)
					{
						return action.action.name == actionName &&
							action.event == event;
					});
			};

			UnifiedProfileRuntime::Runtime runtime;
			Assert::IsTrue(runtime.Initialize(config, source("sdr", "bt2020"), error),
				std::wstring(error.begin(), error.end()).c_str());
			UnifiedProfileRuntime::RefreshResult entered;
			Assert::IsTrue(runtime.Refresh(source("pq", "rec709"), entered, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(entered.changed);
			Assert::AreEqual("scope", entered.snapshot->viewport.profile.c_str());
			Assert::IsTrue(hasInvocation(entered.actions,
				"committed_scope_pq", "state.committed"));
			Assert::IsTrue(hasInvocation(entered.actions,
				"eotf_pq", "source.eotf.changed"));
			const auto context = std::find_if(entered.actions.begin(),
				entered.actions.end(), [](const UnifiedProfileRuntime::ActionInvocation& action)
				{
					return action.action.name == "eotf_context" &&
						action.event == "source.eotf.changed";
				});
			Assert::IsTrue(context != entered.actions.end());
			Assert::AreEqual(
				"pq rec709 sdr scope source.eotf.changed source",
				context->action.arguments.c_str());
			Assert::IsTrue(hasInvocation(entered.actions,
				"rec709", "source.primaries.changed"));
			Assert::IsTrue(hasInvocation(entered.actions,
				"scope_entered", "profile.viewport.changed"));

			std::vector<UnifiedProfileRuntime::ActionInvocation> ready;
			Assert::IsTrue(runtime.CollectActionInvocations("renderer.ready",
				"renderer_ready", nullptr, entered.snapshot, ready, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(hasInvocation(ready, "renderer_scope_ready",
				"renderer.ready"));

			UnifiedProfileRuntime::RefreshResult left;
			Assert::IsTrue(runtime.Refresh(source("sdr", "bt2020"), left, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(left.changed);
			Assert::AreEqual("base", left.snapshot->viewport.profile.c_str());
			Assert::IsTrue(hasInvocation(left.actions, "scope_left",
				"profile.viewport.changed"));
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(EventActionArgumentsExpandAllSupportedValues)
		{
			RendererProfileConfig::Model::EventAction action;
			action.arguments =
				"${eotf}|${previous.eotf}|${profile.viewport}|${event}|${event_reason}";
			RendererProfileConfig::Model::EventAction expanded;
			std::string error;
			Assert::IsTrue(EventActionLauncher::ExpandArgumentVariables(action,
				[](const std::string& variable, std::string& value)
				{
					const std::map<std::string, std::string> values = {
						{ "eotf", "pq" },
						{ "previous.eotf", "sdr" },
						{ "profile.viewport", "scope" },
						{ "event", "source.eotf.changed" },
						{ "event_reason", "source" }
					};
					const auto found = values.find(variable);
					if (found == values.end()) return false;
					value = found->second;
					return true;
				}, expanded, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::AreEqual("pq|sdr|scope|source.eotf.changed|source",
				expanded.arguments.c_str());

			action.arguments = "${missing}";
			Assert::IsFalse(EventActionLauncher::ExpandArgumentVariables(action,
				[](const std::string&, std::string&) { return false; },
				expanded, error));
			Assert::IsTrue(error.find("unavailable") != std::string::npos);
		}

		TEST_METHOD(UnifiedActionsRejectArgumentVariableForWrongEvent)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-invalid-unified-action-argument.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[actions.invalid]\n"
					"on: source.eotf.changed\n"
					"when: $eotf==\"pq\"\n"
					"run: C:\\Windows\\System32\\cmd.exe ${actual_refresh}\n";
			}
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find("cannot expand variable") !=
				std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(UnifiedActionsRejectValueEncodedEventNames)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-invalid-unified-action.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nrenderer: VideoProcessor Renderer (Alpha)\n"
					"[actions.invalid]\n"
					"on: source.eotf.pq\n"
					"when: $eotf==\"pq\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n";
			}
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find("unsupported event") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(UnifiedActionsRouteByBuiltInRendererAliasOrWildcard)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-unified-action-renderer.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[renderer_alias]\ncinema_renderer: 2\n"
					"[actions.built_in]\n"
					"on: renderer.ready\n"
					"when: $event_reason==\"renderer_ready\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n"
					"[actions.named]\n"
					"renderer: cinema_renderer\n"
					"on: source.eotf.changed\n"
					"when: $eotf==\"pq\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n"
					"[actions.all]\n"
					"renderer: *\n"
					"on: state.committed\n"
					"when: $eotf==\"sdr\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());
			RendererProfileConfig::Model model;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::AreEqual(static_cast<size_t>(3), model.actions.size());
			Assert::AreEqual("vprenderer", model.actions[0].renderer.c_str());
			Assert::AreEqual(0, model.actions[0].rendererAliasIndex);
			Assert::AreEqual("cinema_renderer", model.actions[1].renderer.c_str());
			Assert::AreEqual(2, model.actions[1].rendererAliasIndex);
			Assert::AreEqual("*", model.actions[2].renderer.c_str());
			Assert::AreEqual(0, model.actions[2].rendererAliasIndex);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(UnifiedActionsRejectUnknownRendererAliasAndLegacyScope)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-invalid-unified-action-renderer.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[actions.unknown_renderer]\n"
					"renderer: not_configured\n"
					"on: renderer.ready\n"
					"when: $event_reason==\"renderer_ready\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find("renderer") != std::string::npos);

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[actions.legacy_scope]\n"
					"scope: vprenderer\n"
					"on: renderer.ready\n"
					"when: $event_reason==\"renderer_ready\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n";
			}
			Assert::IsTrue(config.Load(path));
			error.clear();
			Assert::IsFalse(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find("unknown key 'scope'") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(Vp0079FirstNamedVariantIsDefaultAndInheritedBaseline)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0079-first-variant.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nrenderer: VideoProcessor Renderer (Alpha)\n"
					"[vprenderer.rec709]\nwhen: $key==\"F4\"\nquality: high\n"
					"sdr_target_primaries: REC709\nreport_bt2020_to_display: false\n"
					"switch_refresh_rate: true\n"
					"[vprenderer.bt2020]\nwhen: $key==\"F5\"\n"
					"sdr_target_primaries: BT2020\nreport_bt2020_to_display: true\n"
					"[directshow.ppm]\nppm: AUTO\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			const std::vector<std::string> sections = config.GetSectionNames();
			Assert::AreEqual(static_cast<size_t>(4), sections.size());
			Assert::AreEqual("general", sections[0].c_str());
			Assert::AreEqual("vprenderer.rec709", sections[1].c_str());
			Assert::AreEqual("vprenderer.bt2020", sections[2].c_str());
			Assert::AreEqual("directshow.ppm", sections[3].c_str());

			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());
			RendererProfileConfig::Model model;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::AreEqual(static_cast<size_t>(1), model.groups.size());
			const RendererProfileConfig::Group& display = model.groups.front();
			Assert::AreEqual("display", display.name.c_str());
			Assert::AreEqual("rec709", display.defaultSelection.c_str());
			Assert::AreEqual("rec709", display.profiles[0].c_str());
			Assert::AreEqual("bt2020", display.profiles[1].c_str());

			const auto bt2020 = model.profiles.find("display.bt2020");
			Assert::IsTrue(bt2020 != model.profiles.end());
			Assert::AreEqual("high", bt2020->second.settings.at("quality").c_str());
			Assert::AreEqual("bt2020",
				ConfigFile::NormalizeName(
					bt2020->second.settings.at("sdr_target_primaries")).c_str());
			Assert::AreEqual("true",
				ConfigFile::NormalizeName(
					bt2020->second.settings.at("report_bt2020_to_display")).c_str());

			std::vector<RendererProfileConfig::AutomaticSelection> automatic;
			Assert::IsTrue(RendererProfileConfig::SelectAutomatic(model,
				[](const std::string&, std::string&) { return false; },
				automatic, error));
			Assert::AreEqual(static_cast<size_t>(1), automatic.size());
			Assert::AreEqual("rec709", automatic.front().profile.c_str());
			Assert::IsTrue(automatic.front().configuredDefault);

			std::vector<RendererProfileConfig::KeySelection> selections;
			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "F4",
				[](const std::string&, std::string&) { return false; },
				selections, error));
			Assert::AreEqual("rec709", selections.front().profile.c_str());
			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "F5",
				[](const std::string&, std::string&) { return false; },
				selections, error));
			Assert::AreEqual("bt2020", selections.front().profile.c_str());
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(CheckedInVp0079ConfigurationPassesStartupSchemas)
		{
			std::string path = __FILE__;
			std::replace(path.begin(), path.end(), '/', '\\');
			const std::string marker =
				"\\src\\VideoProcessor-Test\\ConfigFileTests.cpp";
			const size_t markerPosition = path.rfind(marker);
			Assert::IsTrue(markerPosition != std::string::npos);
			path.resize(markerPosition);
			path += "\\VideoProcessor.cfg";

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			Assert::IsTrue(config.GetWarnings().empty());
			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());
			RendererProfileConfig::Model model;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsFalse(model.persistSelection);
			// The shipped sample must not invoke machine-local commands.
			Assert::AreEqual(static_cast<size_t>(0), model.actions.size());
		}

		TEST_METHOD(Vp0079EmptyShaderRootResolvesAsExplicitOff)
		{
			Assert::IsFalse(MadVRShaderLoader::RuleSelectorsEqual(
				"@shader-key:N", "@shader-key:n"));
			Assert::IsFalse(MadVRShaderLoader::RuleSelectorsEqual(
				"@shader-key:Shift+n", "@shader-key:n"));
			Assert::IsTrue(MadVRShaderLoader::RuleSelectorsEqual(
				"Legacy_NLS", "legacy_nls"));
			Assert::AreEqual("@shader-key:P",
				MadVRShaderLoader::CanonicalizeRuleSelector(
					" @shader-key:P ").c_str());
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0079-empty-shader-root.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[shader.nls]\n"
					"when: $key==\"n\"\n"
					"[shader.nls.standard]\n"
					"when: $key==\"Shift+n\"\n"
					"shader_type: nls\n"
					"glsl_file: NLS.glsl\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::vector<ConfiguredShaderRule> selection;
			std::string error;
			Assert::IsTrue(MadVRShaderLoader::ResolveConfiguredRuleSelection(
				config, "@shader-key:n", ShaderRendererBackend::LIBPLACEBO,
				selection, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::AreEqual(static_cast<size_t>(1), selection.size());
			Assert::IsTrue(selection.front().none,
				L"The empty shader root must explicitly turn Alpha NLS off");
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(MainConfigSchemaAcceptsLegacyQueueNamesButRejectsAmbiguousAliases)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-legacy-queue-schema.cfg";
			ConfigFile config;
			std::string error;

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[queue]\n"
					"startup_preroll_frames: 0\n"
					"steady_reserve_frames: 2\n"
					"[directshow]\n"
					"presentation_lead_frames: 1\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsTrue(MainConfigSchema::Validate(config, error));

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[queue]\n"
					"target_frames: 4\n"
					"steady_reserve_frames: 2\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("both") != std::string::npos);

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[queue]\nlead_frames: 1\n"
					"[directshow]\npresentation_lead_frames: 1\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("both") != std::string::npos);
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
				file << "[command_line]\nalpha_queue_size: 1\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("alpha_queue_size") != std::string::npos);

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[command_line]\nqueue_size: 0\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("queue_size") != std::string::npos);

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[queue]\nqueue_size: 0\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("queue_size") != std::string::npos);

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[queue]\ntarget_frames: 17\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("target_frames") != std::string::npos);

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[queue]\nactive_picture_lookahead_frames: 9\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("active_picture_lookahead_frames") !=
				std::string::npos);

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[queue]\nlead_frames: 17\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(
				error.find("lead_frames") != std::string::npos);
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

		TEST_METHOD(CanonicalRendererAcceptsDocumentedPeakAndLutValues)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0049-documented-renderer-values.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[vpvr.display]\n"
					"peak_detection: high_quality\n"
					"lut_reference_primaries: P3_D65\n"
					"lut_reference_transfer: BT1886\n"
					"lut_reference_range: AUTO\n"
					"lut_reference_nits: AUTO\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsTrue(
				RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(ConfigurationReferenceMatchesPublicFieldInventory)
		{
			auto readFile = [](const std::string& path)
			{
				std::ifstream file(path, std::ios::in | std::ios::binary);
				std::ostringstream contents;
				contents << file.rdbuf();
				std::string text = contents.str();
				text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
				return text;
			};
			auto sourceRoot = []()
			{
				std::string path = __FILE__;
				std::replace(path.begin(), path.end(), '/', '\\');
				const std::string marker =
					"\\src\\VideoProcessor-Test\\ConfigFileTests.cpp";
				const size_t markerPosition = path.rfind(marker);
				if (markerPosition != std::string::npos)
					return path.substr(0, markerPosition);

				char currentDirectory[MAX_PATH] = {};
				GetCurrentDirectoryA(ARRAYSIZE(currentDirectory), currentDirectory);
				path = currentDirectory;
				for (int parent = 0; parent < 8; ++parent)
				{
					if (GetFileAttributesA(
						(path + "\\CONFIGURATION.html").c_str()) !=
						INVALID_FILE_ATTRIBUTES)
						return path;
					const size_t separator = path.find_last_of("\\/");
					if (separator == std::string::npos)
						break;
					path.resize(separator);
				}
				return std::string();
			}();
			Assert::IsFalse(sourceRoot.empty(),
				L"Could not locate the VideoProcessor source root");

			const std::string inventoryText = readFile(
				sourceRoot + "\\docs\\configuration-public-fields.tsv");
			const std::string valueInventoryText = readFile(
				sourceRoot + "\\docs\\configuration-public-values.tsv");
			const std::string html = readFile(
				sourceRoot + "\\CONFIGURATION.html");
			Assert::IsFalse(inventoryText.empty());
			Assert::IsFalse(valueInventoryText.empty());
			Assert::IsFalse(html.empty());

			struct InventoryField
			{
				std::string token;
				std::string anchor;
				bool requiresAutoExplanation = false;
			};
			std::vector<InventoryField> inventory;
			std::set<std::string> inventoryTokens;
			std::set<std::string> inventoryAnchors;
			std::map<std::string, std::string> inventoryAnchorsByToken;
			std::istringstream inventoryStream(inventoryText);
			std::string line;
			while (std::getline(inventoryStream, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line.empty() || line.front() == '#')
					continue;
				std::vector<std::string> columns;
				std::istringstream lineStream(line);
				std::string column;
				while (std::getline(lineStream, column, '\t'))
					columns.push_back(column);
				Assert::AreEqual(static_cast<size_t>(4), columns.size(),
					L"Every public-field inventory line needs four columns");
				Assert::IsTrue(inventoryTokens.insert(columns[0]).second,
					std::wstring(columns[0].begin(), columns[0].end()).c_str());
				inventoryAnchors.insert(columns[1]);
				inventoryAnchorsByToken.emplace(columns[0], columns[1]);
				inventory.push_back(
					{ columns[0], columns[1], columns[3] == "yes" });
			}
			Assert::IsTrue(inventory.size() > 50,
				L"The public inventory unexpectedly lost the core VP-0079 fields");

			struct InventoryValue
			{
				std::string field;
				std::string value;
			};
			std::vector<InventoryValue> valueInventory;
			std::set<std::string> valueTokens;
			std::istringstream valueInventoryStream(valueInventoryText);
			while (std::getline(valueInventoryStream, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line.empty() || line.front() == '#')
					continue;
				const size_t separator = line.find('\t');
				Assert::IsTrue(separator != std::string::npos &&
					line.find('\t', separator + 1) == std::string::npos,
					L"Every public-value inventory line needs two columns");
				const std::string field = line.substr(0, separator);
				std::string value = line.substr(separator + 1);
				if (!value.empty() && value.back() == '\r')
					value.pop_back();
				Assert::IsTrue(inventoryTokens.find(field) != inventoryTokens.end(),
					std::wstring(field.begin(), field.end()).c_str());
				const std::string token = field + "\t" + value;
				Assert::IsTrue(valueTokens.insert(token).second,
					std::wstring(token.begin(), token.end()).c_str());
				valueInventory.push_back({ field, value });
			}
			Assert::IsTrue(valueInventory.size() > 50,
				L"The public-value inventory unexpectedly lost enum coverage");

			std::set<std::string> documentedTokens;
			size_t attribute = 0;
			while ((attribute = html.find("data-fields=\"", attribute)) !=
				std::string::npos)
			{
				attribute += strlen("data-fields=\"");
				const size_t end = html.find('"', attribute);
				Assert::IsTrue(end != std::string::npos);
				std::string values = html.substr(attribute, end - attribute);
				auto replaceAll = [&values](
					const std::string& from, const std::string& to)
				{
					size_t position = 0;
					while ((position = values.find(from, position)) !=
						std::string::npos)
					{
						values.replace(position, from.size(), to);
						position += to.size();
					}
				};
				replaceAll("&lt;", "<");
				replaceAll("&gt;", ">");
				replaceAll("&amp;", "&");
				std::istringstream valueStream(values);
				std::string value;
				while (valueStream >> value)
					Assert::IsTrue(documentedTokens.insert(value).second,
						std::wstring(value.begin(), value.end()).c_str());
				attribute = end + 1;
			}
			Assert::IsTrue(documentedTokens == inventoryTokens,
				L"CONFIGURATION.html data-fields differ from the public inventory");

			for (const std::string& anchor : inventoryAnchors)
			{
				const std::string marker = "id=\"" + anchor + "\"";
				const size_t first = html.find(marker);
				Assert::IsTrue(first != std::string::npos,
					std::wstring(anchor.begin(), anchor.end()).c_str());
				Assert::IsTrue(html.find(marker, first + marker.size()) ==
					std::string::npos,
					L"Configuration anchors must be unique");
			}
			for (const InventoryField& field : inventory)
			{
				if (!field.requiresAutoExplanation)
					continue;
				const size_t anchor = html.find(
					"id=\"" + field.anchor + "\"");
				const size_t articleEnd = html.find("</article>", anchor);
				Assert::IsTrue(anchor != std::string::npos &&
					articleEnd != std::string::npos &&
					html.substr(anchor, articleEnd - anchor).find(
						"AUTO behavior:") != std::string::npos,
					std::wstring(field.token.begin(), field.token.end()).c_str());
			}
			for (const InventoryValue& documentedValue : valueInventory)
			{
				const std::string& anchor =
					inventoryAnchorsByToken.at(documentedValue.field);
				const size_t article = html.find("id=\"" + anchor + "\"");
				const size_t articleEnd = html.find("</article>", article);
				const std::string expected = "<code>" + documentedValue.value +
					"</code>";
				const std::string diagnostic = documentedValue.field + "=" +
					documentedValue.value;
				Assert::IsTrue(article != std::string::npos &&
					articleEnd != std::string::npos &&
					html.substr(article, articleEnd - article).find(expected) !=
						std::string::npos,
					std::wstring(diagnostic.begin(), diagnostic.end()).c_str());
			}

			size_t link = 0;
			while ((link = html.find("href=\"#", link)) != std::string::npos)
			{
				link += strlen("href=\"#");
				const size_t end = html.find('"', link);
				Assert::IsTrue(end != std::string::npos);
				const std::string target = html.substr(link, end - link);
				Assert::IsTrue(
					html.find("id=\"" + target + "\"") != std::string::npos,
					std::wstring(target.begin(), target.end()).c_str());
				link = end + 1;
			}

			size_t example = 0;
			size_t exampleCount = 0;
			while ((example = html.find(
				"<pre data-ini-example=\"", example)) != std::string::npos)
			{
				const size_t contentStart = html.find('>', example);
				const size_t contentEnd = html.find("</pre>", contentStart);
				Assert::IsTrue(contentStart != std::string::npos &&
					contentEnd != std::string::npos);
				std::string configuration = html.substr(
					contentStart + 1, contentEnd - contentStart - 1);
				auto decode = [&configuration](
					const std::string& entity, const std::string& value)
				{
					size_t position = 0;
					while ((position = configuration.find(entity, position)) !=
						std::string::npos)
					{
						configuration.replace(position, entity.size(), value);
						position += value.size();
					}
				};
				decode("&lt;", "<");
				decode("&gt;", ">");
				decode("&amp;", "&");

				char temporaryDirectory[MAX_PATH] = {};
				Assert::IsTrue(GetTempPathA(
					ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
				const std::string path = std::string(temporaryDirectory) +
					"VideoProcessor-vp0049-html-example-" +
					std::to_string(exampleCount) + ".cfg";
				{
					std::ofstream file(path, std::ios::out | std::ios::trunc);
					file << configuration;
				}

				ConfigFile config;
				Assert::IsTrue(config.Load(path));
				std::string error;
				Assert::IsTrue(
					MainConfigSchema::Validate(config, error),
					std::wstring(error.begin(), error.end()).c_str());
				RendererProfileConfig::Model model;
				if (!RendererProfileConfig::Read(config, model, error))
				{
					Logger::WriteMessage((L"Configuration reference example " +
						std::to_wstring(exampleCount) + L" is invalid: " +
						std::wstring(error.begin(), error.end())).c_str());
					Assert::Fail(std::wstring(error.begin(), error.end()).c_str());
				}
				DeleteFileA(path.c_str());
				++exampleCount;
				example = contentEnd + strlen("</pre>");
			}
			Assert::IsTrue(exampleCount >= 1,
				L"No configuration example is marked for validation");

			for (const char* excluded :
				{ "[profile_groups", "[profiles.", "[event_actions]",
				  "[shaders]" })
				Assert::IsTrue(html.find(excluded) == std::string::npos,
					std::wstring(excluded, excluded + strlen(excluded)).c_str());
		}
	};
}
