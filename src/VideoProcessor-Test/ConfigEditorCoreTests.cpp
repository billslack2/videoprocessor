#include "pch.h"

#include <ConfigEditorCore.h>
#include <ConfigurationApplyPolicy.h>
#include <RendererResetPolicy.h>
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
		TEST_METHOD(MissingConfigurationLoadsAsCreatableDocumentAndSavesAtomically)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			Assert::IsFalse(document.existedAtLoad);
			Assert::IsTrue(document.AddSection("vprenderer"));
			Assert::IsTrue(document.AddSection("general"));
			Assert::IsTrue(document.SetKnown(
				"general", "renderer", "VP Renderer"));

			ConfigEditorCore::SaveResult result;
			const bool saved = ConfigEditorCore::SaveSafely(document, result, error);
			const std::wstring saveMessage =
				L"A valid missing configuration must be created: " + error;
			Assert::IsTrue(saved, saveMessage.c_str());
			Assert::IsTrue(result.backupPath.empty(),
				L"The first save must not report a backup for a file that did not exist.");
			Assert::IsTrue(document.existedAtLoad,
				L"A successful first save must transition the document to existing.");
			Assert::IsTrue(ReadBytes(path).find(
				"renderer: VP Renderer") != std::string::npos);
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(MissingConfigurationCreationRefusesExternalCollision)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			WriteBytes(path, "[general]\r\nrenderer: External\r\n");
			ConfigEditorCore::SaveResult result;
			Assert::IsFalse(ConfigEditorCore::SaveSafely(
				document, result, error));
			Assert::IsTrue(error.find(L"created outside") != std::wstring::npos);
			Assert::AreEqual(std::string("[general]\r\nrenderer: External\r\n"),
				ReadBytes(path));
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(EmptyConfigurationAcceptsDiscoveredStartupDefaults)
		{
			const std::wstring path = MakeTemporaryConfigPath(L"vpc");
			WriteBytes(path, "");
			ConfigEditorCore::ConfigDocument document;
			std::wstring error;
			Assert::IsTrue(document.Load(path, error), error.c_str());
			Assert::IsTrue(document.existedAtLoad);
			Assert::IsTrue(document.loadedBytes.empty());
			Assert::IsTrue(document.AddSection("vprenderer"));
			Assert::IsTrue(document.AddSection("general"));
			Assert::IsTrue(document.SetKnown(
				"general", "capture_device", "DeckLink Test Device"));
			Assert::IsTrue(document.SetKnown(
				"general", "renderer", "VP Renderer"));

			ConfigEditorCore::SaveResult result;
			Assert::IsTrue(ConfigEditorCore::SaveSafely(
				document, result, error), error.c_str());
			Assert::IsFalse(result.backupPath.empty());
			const std::string saved = ReadBytes(path);
			Assert::IsTrue(saved.find("capture_device: DeckLink Test Device") !=
				std::string::npos);
			Assert::IsTrue(saved.find("renderer: VP Renderer") !=
				std::string::npos);
			DeleteFileW(result.backupPath.c_str());
			DeleteFileW(path.c_str());
		}

		TEST_METHOD(ConfigurationApplyPolicyClassifiesEveryDocumentedRestartCategory)
		{
			using ConfigurationApplyPolicy::Action;
			const char* restartSections[] = {
				// Startup, hardware, input processing, and general behavior.
				"command_line", "general", "renderer_alias", "decklink",
				"p010_conversion", "ppm_correction",
				"display_refresh_rate_override",
				// LLDV metadata and policy.
				"lldv", "lldv.cinema",
				// Shader definitions, rules, and legacy shader sections.
				"shader", "shader.nls.standard", "shaders", "shaders.legacy",
				// Renderer and viewport profiles, including legacy profile roots.
				"vprenderer", "vprenderer.rec709",
				"vprenderer.viewport.scope", "profiles", "profiles.viewport.scope",
				"viewport", "viewport.scope",
				// Completed-event actions are part of the staged renderer model.
				"actions.audio_delay_film", "event_actions.legacy"
			};
			for (const char* section : restartSections)
			{
				const Action actual = ConfigurationApplyPolicy::ClassifySection(section);
				if (actual != Action::RestartRenderer) Logger::WriteMessage(section);
				Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
					static_cast<int>(actual));
			}
		}

		TEST_METHOD(ConfigurationApplyPolicyClassifiesNoOpAndSaveOnlyContent)
		{
			using ConfigurationApplyPolicy::Action;
			Assert::AreEqual(static_cast<int>(Action::SaveOnly),
				static_cast<int>(ConfigurationApplyPolicy::ClassifySections({})));
			Assert::AreEqual(static_cast<int>(Action::SaveOnly),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges({})));
			const char* saveOnlySections[] = {
				"logging", "unrecognized", "unrecognized.extension"
			};
			for (const char* section : saveOnlySections)
			{
				const Action actual = ConfigurationApplyPolicy::ClassifySection(section);
				if (actual != Action::SaveOnly) Logger::WriteMessage(section);
				Assert::AreEqual(static_cast<int>(Action::SaveOnly),
					static_cast<int>(actual));
			}
			// Section names are case-insensitive in the configuration parser.
			Assert::AreEqual(static_cast<int>(Action::SaveOnly),
				static_cast<int>(ConfigurationApplyPolicy::ClassifySection("LOGGING")));
			Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
				static_cast<int>(ConfigurationApplyPolicy::ClassifySection("GENERAL")));
		}

		TEST_METHOD(ConfigurationApplyPolicyClassifiesShortcutOnlyChangesForLiveReload)
		{
			using ConfigurationApplyPolicy::Action;
			using ConfigurationApplyPolicy::Change;

			Assert::AreEqual(static_cast<int>(Action::ReloadShortcuts),
				static_cast<int>(ConfigurationApplyPolicy::ClassifySection("shortcuts")));
			Assert::AreEqual(static_cast<int>(Action::ReloadShortcuts),
				static_cast<int>(ConfigurationApplyPolicy::ClassifySection("SHORTCUTS")));
			Assert::AreEqual(std::string("Apply shortcuts live"), std::string(
				ConfigurationApplyPolicy::ActionLabel(Action::ReloadShortcuts)));

			const Change profileShortcuts[] = {
				{ "queue.low_latency", "shortcut" },
				{ "lldv.cinema", "SHORTCUT" },
				{ "vprenderer.rec709", "shortcut" },
				{ "vprenderer.viewport.scope", "shortcut" },
				{ "shader.nls.standard", "shortcut" },
				{ "shaders.legacy", "shortcut" },
				{ "profiles.queue.low_latency", "shortcut" },
				{ "profiles.lldv.cinema", "shortcut" },
				{ "profiles.renderer.madvr", "shortcut" },
				{ "profiles.viewport.scope", "shortcut" },
				{ "profiles.shader.nls", "shortcut" }
			};
			for (const Change& change : profileShortcuts)
			{
				Assert::IsTrue(ConfigurationApplyPolicy::IsShortcutAffectingChange(change));
				Assert::AreEqual(static_cast<int>(Action::ReloadShortcuts),
					static_cast<int>(ConfigurationApplyPolicy::ClassifyChange(change)));
			}

			// A non-shortcut value in the same profile retains the profile's normal
			// effect, and unknown content never becomes live merely by using this key.
			Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChange(
					{ "vprenderer.rec709", "quality" })));
			Assert::AreEqual(static_cast<int>(Action::SaveOnly),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChange(
					{ "manual.extension", "shortcut" })));

			Assert::AreEqual(static_cast<int>(Action::ReloadShortcuts),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges(
					{ { "logging", "level" }, { "SHORTCUTS", "renderer_restart" } })));
			Assert::AreEqual(static_cast<int>(Action::ResetQueues),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges(
					{ { "queue", "queue_size" }, { "shortcuts", "renderer_restart" } })));
		}

		TEST_METHOD(ConfigurationApplyPolicyKeepsStartupPresentationDefaultsForNextStart)
		{
			using ConfigurationApplyPolicy::Action;
			using ConfigurationApplyPolicy::Change;
			const Change startupDefaults[] = {
				{ "general", "noui" },
				{ "GENERAL", "NO_UI" },
				{ "general", "fullscreen" },
				{ "General", "WindowedFullscreenMode" },
				{ "GENERAL", "WINDOWED_FULLSCREEN_MODE" }
			};
			for (const Change& change : startupDefaults)
			{
				Assert::IsTrue(
					ConfigurationApplyPolicy::IsStartupPresentationDefaultChange(change));
				Assert::AreEqual(static_cast<int>(Action::SaveOnly),
					static_cast<int>(ConfigurationApplyPolicy::ClassifyChange(change)));
			}

			// Section-only callers remain conservative, and live-applicable General
			// values still require a coherent renderer restart.
			Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
				static_cast<int>(ConfigurationApplyPolicy::ClassifySection("general")));
			Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChange(
					{ "general", "renderer" })));
			Assert::AreEqual(static_cast<int>(Action::RestartCapture),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChange(
					{ "general", "capture_device" })));
			Assert::AreEqual(static_cast<int>(Action::RestartCapture),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChange(
					{ "command_line", "capture_input" })));
			Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChange(
					{ "general", "fullscreen_monitor_name" })));
			Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChange(
					{ "general", "fullscreen_monitor_session_mode" })));

			// Startup-only settings participate normally in strongest-action
			// precedence without causing an action by themselves.
			Assert::AreEqual(static_cast<int>(Action::SaveOnly),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges(
					{ { "logging", "level" }, { "general", "no_ui" } })));
			Assert::AreEqual(static_cast<int>(Action::ReloadShortcuts),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges(
					{ { "general", "fullscreen" },
						{ "shortcuts", "fullscreen_toggle" } })));
			Assert::AreEqual(static_cast<int>(Action::ResetQueues),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges(
					{ { "general", "windowed_fullscreen_mode" },
						{ "queue", "queue_size" } })));
			Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges(
					{ { "general", "noui" }, { "general", "renderer" } })));

			Assert::AreEqual(static_cast<int>(Action::SaveOnly),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges(
					{ { "general", "fullscreen" },
						{ "directshow", "frame_offset" } }, false)));
			Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges(
					{ { "general", "fullscreen" },
						{ "directshow", "frame_offset" } }, true)));
		}

		TEST_METHOD(ConfigurationApplyPolicyClassifiesQueueOnlyChangesAsReset)
		{
			using ConfigurationApplyPolicy::Action;
			const char* queueSections[] = {
				"queue", "queue.low_latency", "queue_recovery",
				"profiles.queue", "profiles.queue.low_latency"
			};
			for (const char* section : queueSections)
			{
				const Action actual = ConfigurationApplyPolicy::ClassifySection(section);
				if (actual != Action::ResetQueues) Logger::WriteMessage(section);
				Assert::AreEqual(static_cast<int>(Action::ResetQueues),
					static_cast<int>(actual));
			}
			Assert::AreEqual(static_cast<int>(Action::ResetQueues),
				static_cast<int>(ConfigurationApplyPolicy::ClassifySections(
					{ "logging", "queue.low_latency", "shortcuts" })));
			Assert::AreEqual(static_cast<int>(Action::ResetQueues),
				static_cast<int>(ConfigurationApplyPolicy::ClassifySection(
					"PROFILES.QUEUE.CINEMA")));
		}

		TEST_METHOD(ConfigurationApplyPolicyQueueActionUsesBackendOwnedResetScope)
		{
			using ConfigurationApplyPolicy::Action;
			using ConfigurationApplyPolicy::Change;
			const std::vector<Change> queuePolicy = {
				{ "queue.low_latency", "queue_size" },
				{ "queue.low_latency", "target_frames" }
			};
			Assert::AreEqual(static_cast<int>(Action::ResetQueues),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges(
					queuePolicy, false)));
			Assert::AreEqual(static_cast<int>(Action::ResetQueues),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges(
					queuePolicy, true)));

			// The action never reconstructs the renderer. Alpha flushes its owned
			// live queue, while DirectShow/madVR serializes the reset across its
			// downstream graph queues.
			Assert::IsFalse(QueuePolicyApplyRequiresGraphReset(false));
			Assert::IsTrue(QueuePolicyApplyRequiresGraphReset(true));
		}

		TEST_METHOD(ConfigurationApplyPolicyUsesTheActiveRendererForDirectShowChanges)
		{
			using ConfigurationApplyPolicy::Action;
			const char* directShowSections[] = {
				"directshow", "directshow.conversion", "directshow.ppm"
			};
			for (const char* section : directShowSections)
			{
				// Alpha owns presentation, so no DirectShow graph exists to rebuild.
				Assert::AreEqual(static_cast<int>(Action::SaveOnly),
					static_cast<int>(ConfigurationApplyPolicy::ClassifySection(
						section, false)));
				// DirectShow/madVR graph-construction changes require one rebuild.
				Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
					static_cast<int>(ConfigurationApplyPolicy::ClassifySection(
						section, true)));
			}

			// An irrelevant DirectShow edit must not hide the strongest eligible
			// action for the renderer that is actually active.
			Assert::AreEqual(static_cast<int>(Action::ResetQueues),
				static_cast<int>(ConfigurationApplyPolicy::ClassifySections(
					{ "directshow", "queue" }, false)));
			Assert::AreEqual(static_cast<int>(Action::ResetQueues),
				static_cast<int>(ConfigurationApplyPolicy::ClassifySections(
					{ "directshow.conversion", "profiles.queue.low_latency" }, false)));
			Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
				static_cast<int>(ConfigurationApplyPolicy::ClassifySections(
					{ "directshow", "queue" }, true)));
			Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
				static_cast<int>(ConfigurationApplyPolicy::ClassifySections(
					{ "directshow.conversion", "profiles.queue.low_latency" }, true)));

			// Shortcut replacement remains the eligible action under Alpha, while
			// a live DirectShow/madVR graph still requires the stronger restart.
			Assert::AreEqual(static_cast<int>(Action::ReloadShortcuts),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges(
					{ { "directshow", "frame_offset" },
						{ "shortcuts", "renderer_restart" } }, false)));
			Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges(
					{ { "directshow", "frame_offset" },
						{ "shortcuts", "renderer_restart" } }, true)));
		}

		TEST_METHOD(ConfigurationApplyPolicyChoosesTheStrongestMixedActionOnce)
		{
			using ConfigurationApplyPolicy::Action;
			using ConfigurationApplyPolicy::Change;
			Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
				static_cast<int>(ConfigurationApplyPolicy::ClassifySections(
					{ "logging", "queue", "vprenderer.primary", "shortcuts" })));
			Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
				static_cast<int>(ConfigurationApplyPolicy::ClassifySections(
					{ "queue", "general", "lldv", "shader.nls" }, false)));

			const Change representatives[] = {
				{ "general", "no_ui" },                  // SaveOnly
				{ "shortcuts", "renderer_restart" },     // ReloadShortcuts
				{ "queue.low_latency", "queue_size" },   // ResetQueues
				{ "vprenderer.rec709", "quality" }       // RestartRenderer
			};
			const Action expected[] = {
				Action::SaveOnly, Action::ReloadShortcuts,
				Action::ResetQueues, Action::RestartRenderer
			};
			for (size_t left = 0; left < ARRAYSIZE(representatives); ++left)
			{
				for (size_t right = 0; right < ARRAYSIZE(representatives); ++right)
				{
					const Action strongest = expected[std::max(left, right)];
					Assert::AreEqual(static_cast<int>(strongest),
						static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges(
							{ representatives[left], representatives[right] })));
					Assert::AreEqual(static_cast<int>(strongest),
						static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges(
							{ representatives[right], representatives[left] })));
				}
			}
			Assert::AreEqual(static_cast<int>(Action::RestartRenderer),
				static_cast<int>(ConfigurationApplyPolicy::ClassifyChanges(
					{ representatives[0], representatives[1], representatives[2],
						representatives[3], representatives[3] })));
			Assert::AreEqual(std::string("Takes effect next start"),
				std::string(ConfigurationApplyPolicy::ActionLabel(Action::SaveOnly)));
			Assert::AreEqual(std::string("Apply shortcuts live"),
				std::string(ConfigurationApplyPolicy::ActionLabel(Action::ReloadShortcuts)));
			Assert::AreEqual(std::string("Reset queues"),
				std::string(ConfigurationApplyPolicy::ActionLabel(Action::ResetQueues)));
			Assert::AreEqual(std::string("Restart renderer"),
				std::string(ConfigurationApplyPolicy::ActionLabel(Action::RestartRenderer)));
			Assert::AreEqual(std::string("Restart capture"),
				std::string(ConfigurationApplyPolicy::ActionLabel(Action::RestartCapture)));
		}

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
				"diagnostic_disable_shader_cache: false\ndiagnostic_disable_compute: false\n"
				"diagnostic_force_8bit_sdr_swapchain: false\ndiagnostic_allow_limited_g22: false\n"
				"default_screen_profile: normal\n"
				"[vprenderer.bt2020]\nshortcut: F5\nsdr_target_primaries: bt2020\nreport_bt2020_to_display: true\n"
				"[vprenderer.viewport.viewport_16x9]\nlabel: 16x9\nmode: normal\nscreen_aspect: 16:9\n"
				"automatic_crop: false\nsubtitle_fit: true\nsubtitle_hold_seconds: 2\n"
				"subtitle_engage_drift_ms: 0\nsubtitle_release_drift_ms: 0\nsubtitle_padding_pixels: 20\n"
				"subtitle_target_buffer_pixels: 10\n"
				"scope_screen_aspect: 2.35:1\nscope_automatic_crop: true\nscope_subtitle_fit: true\n"
				"scope_subtitle_hold_seconds: 2\nscope_subtitle_engage_drift_ms: 0\nscope_subtitle_release_drift_ms: 0\n"
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
				{ "vprenderer.rec709", "diagnostic_disable_compute", "false" },
				{ "vprenderer.rec709", "diagnostic_force_8bit_sdr_swapchain", "false" },
				{ "vprenderer.rec709", "diagnostic_allow_limited_g22", "false" },
				{ "vprenderer.rec709", "default_screen_profile", "normal" },

				{ "vprenderer.viewport.scope", "shortcut", "Ctrl+V" },
				{ "vprenderer.viewport.scope", "when", "${width} >= 1280" },
				{ "vprenderer.viewport", "screen_aspect", "21:10" },
				{ "vprenderer.viewport", "vertical_alignment", "bottom" },
				{ "vprenderer.viewport", "anamorphic_scale", "4:3" },
				{ "vprenderer.viewport", "automatic_crop", "true" },
				{ "vprenderer.viewport", "subtitle_fit", "true" },
				{ "vprenderer.viewport", "subtitle_hold_seconds", "3" },
				{ "vprenderer.viewport", "subtitle_engage_drift_ms", "100" },
				{ "vprenderer.viewport", "subtitle_release_drift_ms", "1000" },
				{ "vprenderer.viewport", "subtitle_padding_pixels", "24" },
				{ "vprenderer.viewport", "subtitle_target_buffer_pixels", "10" },

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
				{ "shortcuts", "toggle_noui", "Alt+U" },
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
