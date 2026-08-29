#include "pch.h"

#include <ApplicationInterface.h>
#include <ApplicationShutdownPolicy.h>
#include <BuildIdentityPolicy.h>
#include <ModernOperatorLayout.h>
#include <ModernOperatorStatusPolicy.h>
#include <ConfigFile.h>
#include <ConfigurationLiveApply.h>
#include <ConfigurationApplyPolicy.h>
#include <DisplayTopologySession.h>
#include <EventActionLauncher.h>
#include <MainConfigSchema.h>
#include <QueueConfiguration.h>
#include <QueueProfileRestartPolicy.h>
#include <ProfileChangeOverlay.h>
#include <blackmagic_decklink/BlackMagicDeckLinkTranslate.h>
#include <guid.h>
#include <microsoft_directshow/DirectShowTranslations.h>
#include <microsoft_directshow/MadVRShaderLoader.h>
#include <microsoft_directshow/video_renderers/DirectShowVideoRenderers.h>
#include <RendererConfigView.h>
#include <RendererProfileConfig.h>
#include <ShaderConfigValidation.h>
#include <UnifiedProfileRuntime.h>
#include <VideoConversionOverride.h>
#include "CppUnitTest.h"

#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
	TEST_CLASS(ConfigFileTests)
	{
	public:
		TEST_METHOD(VideoConversionOverrideAcceptsConfiguredDisabledValues)
		{
			VideoConversionOverride parsed =
				VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010;
			Assert::IsTrue(TryParseVideoConversionOverride(TEXT("NONE"), parsed));
			Assert::IsTrue(parsed ==
				VideoConversionOverride::VIDEOCONVERSION_NONE);
			Assert::IsTrue(TryParseVideoConversionOverride(TEXT("off"), parsed));
			Assert::IsTrue(parsed ==
				VideoConversionOverride::VIDEOCONVERSION_NONE);
			Assert::IsTrue(TryParseVideoConversionOverride(
				TEXT("V210_TO_P010"), parsed));
			Assert::IsTrue(parsed ==
				VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010);
			Assert::IsTrue(TryParseVideoConversionOverride(
				TEXT("uyvy_to_p010"), parsed));
			Assert::IsFalse(TryParseVideoConversionOverride(
				TEXT("NOT_A_CONVERSION"), parsed));
		}

		TEST_METHOD(ModernOperatorBuildIdentityUsesBranchForCleanBuildsAndCommitForDirtyBuilds)
		{
			Assert::AreEqual(std::wstring(L"v1.3.003-beta"),
				BuildIdentityPolicy::Format(
					L" v1.3.003-beta ", L"9a5dd0b",
					L"v1.1.016-beta-326-g9a5dd0b", false));
			Assert::AreEqual(std::wstring(L"ci/merge @ abc1234"),
				BuildIdentityPolicy::Format(L"ci/merge", L"abc1234", L"tag", true));
			Assert::AreEqual(std::wstring(L"v1.2.3-gabc1234"),
				BuildIdentityPolicy::Format(L"", L"abc1234",
					L"v1.2.3-gabc1234", false));
			Assert::AreEqual(std::wstring(L"v1.2.3-gabc1234 @ abc1234"),
				BuildIdentityPolicy::Format(L"", L"abc1234",
					L"v1.2.3-gabc1234", true));
			Assert::AreEqual(std::wstring(L"detached"),
				BuildIdentityPolicy::Format(L"", L"", L"", false));
		}

		TEST_METHOD(FullscreenActivationNeverStealsAnotherProcessForeground)
		{
			Assert::IsTrue(ConfigurationLiveApply::MayActivateFullscreen(
				42, 42, true));
			Assert::IsTrue(ConfigurationLiveApply::MayActivateFullscreen(
				42, 0, false));
			Assert::IsFalse(ConfigurationLiveApply::MayActivateFullscreen(
				42, 84, true));
			const auto editorPopup =
				ConfigurationLiveApply::ResolvePresentationFocus(42, 84, true);
			Assert::IsFalse(editorPopup.mayActivatePresentation);
			Assert::IsTrue(editorPopup.preserveForeground);
			Assert::IsFalse(editorPopup.consumeKeyboardMessage);
			const auto vpForeground =
				ConfigurationLiveApply::ResolvePresentationFocus(42, 42, true);
			Assert::IsTrue(vpForeground.mayActivatePresentation);
			Assert::IsFalse(vpForeground.preserveForeground);
			Assert::IsFalse(vpForeground.consumeKeyboardMessage);
			Assert::IsTrue(ConfigurationLiveApply::MayDispatchGlobalShortcut(
				42, 84, false));
			Assert::IsTrue(
				ConfigurationLiveApply::MayDispatchForegroundPresentationShortcut(
					true, true));
		}

		TEST_METHOD(ConfigurationEditorShortcutAlwaysStartsFreshReveal)
		{
			using Action = ConfigurationLiveApply::ConfigurationEditorToggleAction;
			Assert::IsTrue(Action::RevealOrActivate ==
				ConfigurationLiveApply::ResolveConfigurationEditorToggle(
					true, true, false));
			Assert::IsTrue(Action::RevealOrActivate ==
				ConfigurationLiveApply::ResolveConfigurationEditorToggle(
					true, false, false));
			Assert::IsTrue(Action::RevealOrActivate ==
				ConfigurationLiveApply::ResolveConfigurationEditorToggle(
					false, false, false));
			Assert::IsTrue(Action::RevealOrActivate ==
				ConfigurationLiveApply::ResolveConfigurationEditorToggle(
					false, false, true));
		}

		TEST_METHOD(ConfigurationEditorStaleHandleRediscoveryIsBounded)
		{
			Assert::IsTrue(ConfigurationLiveApply::
				ShouldRediscoverConfigurationEditor(true, 0, false));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldRediscoverConfigurationEditor(true, 0, true));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldRediscoverConfigurationEditor(true, 1, false));
			Assert::IsTrue(ConfigurationLiveApply::
				ConfigurationEditorRevealAcknowledged(true, 1));
			Assert::IsFalse(ConfigurationLiveApply::
				ConfigurationEditorRevealAcknowledged(true, 0));
			Assert::IsTrue(ConfigurationLiveApply::
				ConfigurationEditorToggleAction::RevealOrActivate ==
				ConfigurationLiveApply::ResolveConfigurationEditorToggle(
					true, false, true));
			Assert::IsFalse(ConfigurationLiveApply::
				ResolvePresentationFocus(42, 84, true).consumeKeyboardMessage);
		}

		TEST_METHOD(ConfigurationEditorRevealIntentSurvivesColdStartupTransitions)
		{
			using Outcome =
				ConfigurationLiveApply::ConfigurationEditorRevealOutcome;
			Assert::IsTrue(Outcome::Pending ==
				ConfigurationLiveApply::ResolveConfigurationEditorReveal(
					true, false, false, 1500, 20000));
			// A denied foreground fallback and arbitrary renderer/fullscreen
			// transitions do not alter the durable reveal state.
			Assert::IsTrue(Outcome::Pending ==
				ConfigurationLiveApply::ResolveConfigurationEditorReveal(
					true, false, false, 5000, 20000));
			Assert::IsTrue(ConfigurationLiveApply::
				ShouldRetryRevealForAssociation(true, true));
			Assert::IsTrue(Outcome::Complete ==
				ConfigurationLiveApply::ResolveConfigurationEditorReveal(
					true, true, true, 7000, 20000));
			Assert::IsTrue(Outcome::Expired ==
				ConfigurationLiveApply::ResolveConfigurationEditorReveal(
					true, false, false, 20000, 20000));
			Assert::IsTrue(Outcome::Expired ==
				ConfigurationLiveApply::ResolveConfigurationEditorReveal(
					true, false, false, 10, 20000, true));
		}

		TEST_METHOD(ConfigurationEditorActivationAckBreaksReverseCallbackCycle)
		{
			Assert::IsFalse(ConfigurationLiveApply::
				AssociationPublicationMayBlockActivateHandler());
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldRetryConfigurationEditorActivate(
					true, true, 250, 1500, true));
			Assert::IsTrue(ConfigurationLiveApply::
				ShouldRetryConfigurationEditorActivate(
					true, true, 1500, 1500, true));
			Assert::IsTrue(ConfigurationLiveApply::
				ShouldRetryConfigurationEditorActivate(
					true, false, 0, 1500, true));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldRetryConfigurationEditorActivate(
					false, false, 0, 1500, true));
		}

		TEST_METHOD(SessionRendererSelectionSurvivesLifecyclePublication)
		{
			using Source =
				ConfigurationLiveApply::RendererPublicationSource;
			auto decision = ConfigurationLiveApply::ResolveRendererPublication(
				L"DirectShow - madVR", L"VP Renderer", L"DirectShow - madVR",
				false, true);
			Assert::AreEqual(std::wstring(L"VP Renderer"), decision.renderer);
			Assert::IsTrue(Source::SessionOverride == decision.source);

			decision = ConfigurationLiveApply::ResolveRendererPublication(
				L"VP Renderer", L"DirectShow - madVR", L"VP Renderer",
				false, true);
			Assert::AreEqual(std::wstring(L"DirectShow - madVR"),
				decision.renderer);
			Assert::IsTrue(Source::SessionOverride == decision.source);

			decision = ConfigurationLiveApply::ResolveRendererPublication(
				L"VP Renderer", L"DirectShow - madVR", L"DirectShow - madVR",
				false, false);
			Assert::AreEqual(std::wstring(L"DirectShow - madVR"),
				decision.renderer);
			Assert::IsTrue(Source::SessionOverride == decision.source);

			decision = ConfigurationLiveApply::ResolveRendererPublication(
				L"VP Renderer", L"DirectShow - madVR", L"DirectShow - madVR",
				true, true);
			Assert::AreEqual(std::wstring(L"VP Renderer"), decision.renderer);
			Assert::IsTrue(Source::SavedConfiguration == decision.source);

			decision = ConfigurationLiveApply::ResolveRendererPublication(
				L"VP Renderer", L"", L"DirectShow - madVR", false, false);
			Assert::AreEqual(std::wstring(L"DirectShow - madVR"),
				decision.renderer);
			Assert::IsTrue(Source::AcceptedRuntime == decision.source);
		}

		TEST_METHOD(GlobalShortcutPolicyAllowsBareKeysOutsideConfig)
		{
			Assert::IsTrue(ConfigurationLiveApply::MayDispatchGlobalShortcut(
				42, 84, false));
			Assert::IsFalse(ConfigurationLiveApply::MayDispatchGlobalShortcut(
				42, 42, false));
			Assert::IsFalse(ConfigurationLiveApply::MayDispatchGlobalShortcut(
				42, 84, true));
			Assert::IsTrue(
				ConfigurationLiveApply::MayDispatchBackgroundAccelerator(
					false, false, false));
			Assert::IsTrue(
				ConfigurationLiveApply::MayDispatchBackgroundAccelerator(
					true, false, false));
			Assert::IsTrue(
				ConfigurationLiveApply::MayDispatchBackgroundAccelerator(
					false, true, false));
			Assert::IsTrue(
				ConfigurationLiveApply::MayDispatchBackgroundAccelerator(
					false, false, true));
			Assert::IsTrue(ConfigurationLiveApply::ShortcutModifiersMatch(
				true, false, true, true, false, true));
			Assert::IsFalse(ConfigurationLiveApply::ShortcutModifiersMatch(
				true, false, true, true, false, false));
			Assert::IsTrue(ConfigurationLiveApply::MayDispatchInjectedShortcut(
				true, 42, 84));
			Assert::IsFalse(ConfigurationLiveApply::MayDispatchInjectedShortcut(
				false, 42, 84));
			Assert::IsFalse(ConfigurationLiveApply::MayDispatchInjectedShortcut(
				true, 42, 42));
		}

		TEST_METHOD(ModernBackgroundAndConfigurationModalPoliciesAreFailSafe)
		{
			Assert::IsTrue(
				ConfigurationLiveApply::ShouldEnableBackgroundShortcuts(true, false, false));
			Assert::IsFalse(
				ConfigurationLiveApply::ShouldEnableBackgroundShortcuts(false, false, false));
			Assert::IsFalse(
				ConfigurationLiveApply::ShouldEnableBackgroundShortcuts(true, true, false));
			Assert::IsFalse(
				ConfigurationLiveApply::ShouldEnableBackgroundShortcuts(true, false, true));
			Assert::IsTrue(ConfigurationLiveApply::ShouldRequestPresentationFocus(
				true, false, true));
			Assert::IsFalse(ConfigurationLiveApply::ShouldRequestPresentationFocus(
				false, false, true));
			Assert::IsFalse(ConfigurationLiveApply::ShouldRequestPresentationFocus(
				true, true, true));
			Assert::IsTrue(ConfigurationLiveApply::ShouldReturnPresentationFocus(
				true, true, true));
			Assert::IsFalse(ConfigurationLiveApply::ShouldReturnPresentationFocus(
				false, true, true));
			Assert::IsTrue(
				ConfigurationLiveApply::ShouldSuppressFullscreenTopmost(false, true, false));
			Assert::IsTrue(
				ConfigurationLiveApply::ShouldSuppressFullscreenTopmost(false, false, true));
			Assert::IsTrue(ConfigurationLiveApply::MayEnterConfigurationModal(
				true, false, 84, 84));
			Assert::IsFalse(ConfigurationLiveApply::MayEnterConfigurationModal(
				true, false, 84, 42));
			Assert::IsFalse(ConfigurationLiveApply::MayEnterConfigurationModal(
				true, true, 84, 84));
			Assert::IsTrue(ConfigurationLiveApply::IsDuplicateBackgroundShortcut(
				33200, 33200, 100, 250));
			Assert::IsFalse(ConfigurationLiveApply::IsDuplicateBackgroundShortcut(
				33200, 33201, 100, 250));
			Assert::IsFalse(ConfigurationLiveApply::IsDuplicateBackgroundShortcut(
				33200, 33200, 251, 250));
			Assert::IsTrue(
				ConfigurationLiveApply::MayDispatchWhileConfigurationModal(
					true, true));
			Assert::IsTrue(
				ConfigurationLiveApply::MayDispatchWhileConfigurationModal(
					true, false));
			Assert::IsFalse(
				ConfigurationLiveApply::ShouldPromoteFullscreenAfterLiveFrame(
					true, true, false, false));
			Assert::IsTrue(
				ConfigurationLiveApply::ShouldPromoteFullscreenAfterLiveFrame(
					true, true, true, false));
			Assert::IsFalse(
				ConfigurationLiveApply::ShouldPromoteFullscreenAfterLiveFrame(
					true, true, true, true));
			Assert::IsTrue(
				ConfigurationLiveApply::ShouldRecreateFullscreenHostForBackendHandoff(
					true, false, true, true, false));
			Assert::IsFalse(
				ConfigurationLiveApply::ShouldRecreateFullscreenHostForBackendHandoff(
					true, true, true, true, false));
		}

		TEST_METHOD(AlphaHostReconstructionRevealsOnlyCurrentGenerationAndTarget)
		{
			using ConfigurationLiveApply::
				ShouldRetireWaitingSurfaceAfterLiveFrame;
			constexpr uintptr_t windowedTarget = 0x1001;
			constexpr uintptr_t fullscreenTarget = 0x2002;

			Assert::IsFalse(ShouldRetireWaitingSurfaceAfterLiveFrame(
				3, windowedTarget, 4, fullscreenTarget, 4, fullscreenTarget,
				true, true, true, false, false));
			Assert::IsFalse(ShouldRetireWaitingSurfaceAfterLiveFrame(
				4, windowedTarget, 4, fullscreenTarget, 4, fullscreenTarget,
				true, true, true, false, false));
			Assert::IsTrue(ShouldRetireWaitingSurfaceAfterLiveFrame(
				4, fullscreenTarget, 4, fullscreenTarget, 4, fullscreenTarget,
				true, true, true, false, false));

			// Editor visibility suppresses focus promotion, never retirement of a
			// verified current waiting surface.
			Assert::IsTrue(ShouldRetireWaitingSurfaceAfterLiveFrame(
				4, fullscreenTarget, 4, fullscreenTarget, 4, fullscreenTarget,
				true, true, true, false, true));
			Assert::IsFalse(
				ConfigurationLiveApply::ShouldPromoteFullscreenAfterLiveFrame(
					true, true, true, true));

			// The inverse fullscreen-to-windowed reconstruction uses the same
			// generation/target contract.
			Assert::IsFalse(ShouldRetireWaitingSurfaceAfterLiveFrame(
				4, fullscreenTarget, 5, windowedTarget, 5, windowedTarget,
				true, true, true, false, false));
			Assert::IsTrue(ShouldRetireWaitingSurfaceAfterLiveFrame(
				5, windowedTarget, 5, windowedTarget, 5, windowedTarget,
				true, true, true, false, false));
		}

#if 0
		TEST_METHOD(ConfigurationEditorPresentationLeaseUsesActualWindowZOrder)
		{
			struct Windows
			{
				HWND host = nullptr;
				HWND editor = nullptr;
				HWND popup = nullptr;
				~Windows()
				{
					if (popup) DestroyWindow(popup);
					if (editor) DestroyWindow(editor);
					if (host) DestroyWindow(host);
				}
			} windows;
			windows.host = CreateWindowExW(WS_EX_TOPMOST, L"STATIC",
				L"VP fullscreen test host", WS_POPUP | WS_VISIBLE,
				0, 0, 320, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
			windows.editor = CreateWindowExW(0, L"STATIC",
				L"VP configuration test editor", WS_POPUP | WS_VISIBLE,
				16, 16, 160, 90, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
			Assert::IsNotNull(windows.host);
			Assert::IsNotNull(windows.editor);
			windows.popup = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC",
				L"VP configuration combo popup", WS_POPUP | WS_VISIBLE,
				32, 32, 120, 80, windows.editor, nullptr,
				GetModuleHandleW(nullptr), nullptr);
			Assert::IsNotNull(windows.popup);
			std::vector<RECT> monitorRects;
			EnumDisplayMonitors(nullptr, nullptr,
				[](HMONITOR, HDC, LPRECT rect, LPARAM context) -> BOOL
				{
					reinterpret_cast<std::vector<RECT>*>(context)->push_back(*rect);
					return TRUE;
				}, reinterpret_cast<LPARAM>(&monitorRects));
			if (monitorRects.size() > 1)
			{
				const RECT& second = monitorRects[1];
				SetWindowPos(windows.host, nullptr, second.left, second.top,
					320, 180, SWP_NOACTIVATE | SWP_NOZORDER);
			}
			RECT hostRectBefore = {};
			GetWindowRect(windows.host, &hostRectBefore);
			const LONG_PTR hostStyleBefore =
				GetWindowLongPtrW(windows.host, GWL_STYLE);
			constexpr bool fullscreenRequested = true;
			constexpr uint32_t rendererGeneration = 17;
			const uintptr_t selectedTarget = ConfigurationLiveApply::
				SelectConfigurationEditorPresentationTarget(true,
					reinterpret_cast<uintptr_t>(windows.host), 0, 0);
			Assert::AreEqual(reinterpret_cast<uintptr_t>(windows.host),
				selectedTarget);
			Assert::AreEqual(
				reinterpret_cast<uintptr_t>(MonitorFromWindow(windows.host,
					MONITOR_DEFAULTTONEAREST)),
				reinterpret_cast<uintptr_t>(MonitorFromWindow(
					reinterpret_cast<HWND>(selectedTarget),
					MONITOR_DEFAULTTONEAREST)));
			Assert::IsTrue(ConfigurationLiveApply::
				IsValidatedPresentationTargetProcess(42, 42, true));
			Assert::IsFalse(ConfigurationLiveApply::
				IsValidatedPresentationTargetProcess(42, 84, true));
			Assert::IsFalse(ConfigurationLiveApply::
				ConfigurationEditorRuntimeUsesRecurringLease());
			Assert::IsTrue(ConfigurationLiveApply::
				ShouldRequestConfigurationEditorOneShotReassert(true, true));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldRequestConfigurationEditorOneShotReassert(false, true));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldRequestConfigurationEditorOneShotReassert(true, false));
			SetActiveWindow(windows.editor);
			const bool editorBecameForeground =
				SetForegroundWindow(windows.editor) != FALSE;

			Assert::IsTrue(ConfigurationEditorZOrder::Apply(
				windows.host, windows.editor));
			Assert::AreEqual(reinterpret_cast<uintptr_t>(windows.editor),
				reinterpret_cast<uintptr_t>(GetActiveWindow()));
			if (editorBecameForeground)
				Assert::AreEqual(reinterpret_cast<uintptr_t>(windows.editor),
					reinterpret_cast<uintptr_t>(GetForegroundWindow()));
			Assert::IsTrue(ConfigurationLiveApply::
				ShouldRestoreConfigurationEditorForeground(true, 42, 42));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldRestoreConfigurationEditorForeground(true, 42, 84));
			Assert::IsTrue(ConfigurationEditorZOrder::IsAbove(
				windows.editor, windows.host));
			Assert::IsTrue((GetWindowLongPtrW(
				windows.editor, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
			Assert::IsTrue((GetWindowLongPtrW(
				windows.host, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
			RECT hostRectAfter = {};
			GetWindowRect(windows.host, &hostRectAfter);
			Assert::IsTrue(EqualRect(&hostRectBefore, &hostRectAfter) != FALSE);
			Assert::AreEqual(hostStyleBefore,
				GetWindowLongPtrW(windows.host, GWL_STYLE));
			Assert::IsTrue(fullscreenRequested);
			Assert::AreEqual<uint32_t>(17, rendererGeneration);

			// A same-process combo popup owns Config interaction. Lease maintenance
			// must not collapse it back to the main window; only a VP foreground
			// takeover causes the main editor to be reasserted.
			SetActiveWindow(windows.popup);
			const LONG_PTR editorExStyleBeforeWatchdog =
				GetWindowLongPtrW(windows.editor, GWL_EXSTYLE);
			const LONG_PTR popupExStyleBeforeWatchdog =
				GetWindowLongPtrW(windows.popup, GWL_EXSTYLE);
			const bool popupAboveEditorBeforeWatchdog =
				ConfigurationEditorZOrder::IsAbove(
					windows.popup, windows.editor);
			for (int tick = 0; tick < 5; ++tick)
			{
				Assert::IsTrue(ConfigurationEditorZOrder::
					MaintainPresentationHost(windows.host));
			}
			Assert::AreEqual(reinterpret_cast<uintptr_t>(windows.popup),
				reinterpret_cast<uintptr_t>(GetActiveWindow()));
			Assert::AreEqual(editorExStyleBeforeWatchdog,
				GetWindowLongPtrW(windows.editor, GWL_EXSTYLE));
			Assert::AreEqual(popupExStyleBeforeWatchdog,
				GetWindowLongPtrW(windows.popup, GWL_EXSTYLE));
			Assert::AreEqual(popupAboveEditorBeforeWatchdog,
				ConfigurationEditorZOrder::IsAbove(
					windows.popup, windows.editor));
			SetActiveWindow(windows.host);
			Assert::IsTrue(ConfigurationLiveApply::
				ShouldRestoreConfigurationEditorForeground(true, 42, 42));
			SetActiveWindow(windows.editor);
			Assert::AreEqual(reinterpret_cast<uintptr_t>(windows.editor),
				reinterpret_cast<uintptr_t>(GetActiveWindow()));

			// Simulate renderer reconstruction or the delayed fullscreen pass
			// attempting to reassert the presentation host.
			SetWindowPos(windows.host, HWND_TOPMOST, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			Assert::IsTrue(ConfigurationEditorZOrder::Apply(
				windows.host, windows.editor));
			Assert::IsTrue(ConfigurationEditorZOrder::IsAbove(
				windows.editor, windows.host));
			Assert::AreEqual(reinterpret_cast<uintptr_t>(windows.editor),
				reinterpret_cast<uintptr_t>(GetActiveWindow()));
			if (editorBecameForeground)
				Assert::AreEqual(reinterpret_cast<uintptr_t>(windows.editor),
					reinterpret_cast<uintptr_t>(GetForegroundWindow()));

			ShowWindow(windows.host, SW_MINIMIZE);
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldExecuteDeferredPresentationActivation(
					true, true, true, true, true, true));
			ShowWindow(windows.editor, SW_HIDE);
			Assert::IsTrue(ConfigurationLiveApply::
				ShouldExecuteDeferredPresentationActivation(
					true, false, true, true, true, true));
			ShowWindow(windows.host, SW_RESTORE);
			SetActiveWindow(windows.host);
			Assert::AreEqual(reinterpret_cast<uintptr_t>(windows.host),
				reinterpret_cast<uintptr_t>(GetActiveWindow()));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldExecuteDeferredPresentationActivation(
					false, false, true, true, true, true));

			Assert::IsTrue(ConfigurationEditorZOrder::Release(windows.editor));
			Assert::IsFalse((GetWindowLongPtrW(
				windows.editor, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
			RECT hostRectAfterHide = {};
			GetWindowRect(windows.host, &hostRectAfterHide);
			Assert::IsTrue(EqualRect(&hostRectBefore, &hostRectAfterHide) != FALSE);
			Assert::AreEqual(hostStyleBefore,
				GetWindowLongPtrW(windows.host, GWL_STYLE));
			Assert::IsTrue((GetWindowLongPtrW(
				windows.host, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
			Assert::AreEqual<uint32_t>(17, rendererGeneration);
		}
#endif

		TEST_METHOD(ConfigurationEditorUsesNativeOwnerWithoutRuntimePolling)
		{
			struct Windows
			{
				HWND host = nullptr;
				HWND editor = nullptr;
				HWND popup = nullptr;
				~Windows()
				{
					if (popup) DestroyWindow(popup);
					if (editor) DestroyWindow(editor);
					if (host) DestroyWindow(host);
				}
			} windows;
			windows.host = CreateWindowExW(WS_EX_TOPMOST, L"STATIC",
				L"VP presentation owner", WS_POPUP | WS_VISIBLE,
				0, 0, 320, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
			windows.editor = CreateWindowExW(0, L"STATIC", L"Config",
				WS_POPUP | WS_VISIBLE, 16, 16, 160, 90,
				nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
			Assert::IsNotNull(windows.host);
			Assert::IsNotNull(windows.editor);
			SetWindowLongPtrW(windows.editor, GWLP_HWNDPARENT,
				reinterpret_cast<LONG_PTR>(windows.host));
			windows.popup = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC",
				L"Config combo", WS_POPUP | WS_VISIBLE, 32, 32, 120, 80,
				windows.editor, nullptr, GetModuleHandleW(nullptr), nullptr);
			Assert::IsNotNull(windows.popup);

			Assert::AreEqual(reinterpret_cast<uintptr_t>(windows.host),
				reinterpret_cast<uintptr_t>(GetWindow(windows.editor, GW_OWNER)));
			Assert::AreEqual(reinterpret_cast<uintptr_t>(windows.editor),
				reinterpret_cast<uintptr_t>(GetWindow(windows.popup, GW_OWNER)));
			Assert::IsFalse(ConfigurationLiveApply::
				ConfigurationEditorRuntimeUsesRecurringLease());

			RECT hostBefore = {};
			GetWindowRect(windows.host, &hostBefore);
			const LONG_PTR hostStyle = GetWindowLongPtrW(windows.host, GWL_STYLE);
			const LONG_PTR editorExStyle =
				GetWindowLongPtrW(windows.editor, GWL_EXSTYLE);
			const LONG_PTR popupExStyle =
				GetWindowLongPtrW(windows.popup, GWL_EXSTYLE);
			SetActiveWindow(windows.popup);
			for (int formerWatchdogTick = 0; formerWatchdogTick < 5;
				++formerWatchdogTick)
			{
				// Native-owner architecture intentionally performs no runtime call.
			}
			Assert::AreEqual(reinterpret_cast<uintptr_t>(windows.popup),
				reinterpret_cast<uintptr_t>(GetActiveWindow()));
			Assert::AreEqual(editorExStyle,
				GetWindowLongPtrW(windows.editor, GWL_EXSTYLE));
			Assert::AreEqual(popupExStyle,
				GetWindowLongPtrW(windows.popup, GWL_EXSTYLE));
			RECT hostAfter = {};
			GetWindowRect(windows.host, &hostAfter);
			Assert::IsTrue(EqualRect(&hostBefore, &hostAfter) != FALSE);
			Assert::AreEqual(hostStyle,
				GetWindowLongPtrW(windows.host, GWL_STYLE));
			Assert::AreEqual(reinterpret_cast<uintptr_t>(windows.host),
				ConfigurationLiveApply::SelectConfigurationEditorPresentationTarget(
					true, reinterpret_cast<uintptr_t>(windows.host), 0, 0));
			Assert::IsTrue(ConfigurationLiveApply::
				IsValidatedPresentationTargetProcess(42, 42, true));
			Assert::IsFalse(ConfigurationLiveApply::
				IsValidatedPresentationTargetProcess(42, 84, true));
		}

		TEST_METHOD(ConfigurationBoundaryDefersRendererForNewerCaptureState)
		{
			Assert::IsTrue(ConfigurationLiveApply::
				MayConstructRendererAfterConfigurationBoundary(41, 41));
			Assert::IsFalse(ConfigurationLiveApply::
				MayConstructRendererAfterConfigurationBoundary(41, 42));
			Assert::IsFalse(ConfigurationLiveApply::
				MayConstructRendererAfterConfigurationBoundary(0, 1));
		}

		TEST_METHOD(ConfigurationEditorOwnerSurvivesMissingOrChangingRenderer)
		{
			constexpr uintptr_t host = 0x1000;
			Assert::AreEqual(static_cast<uintptr_t>(0), ConfigurationLiveApply::
				SelectConfigurationEditorNativeOwner(host, 0));
			Assert::AreEqual(static_cast<uintptr_t>(0), ConfigurationLiveApply::
				SelectConfigurationEditorNativeOwner(host, 0x2000));
			Assert::AreEqual(static_cast<uintptr_t>(0), ConfigurationLiveApply::
				SelectConfigurationEditorNativeOwner(host, 0x3000));
		}

		TEST_METHOD(FreshRendererConsumesLatchedRestartIntent)
		{
			Assert::IsTrue(ConfigurationLiveApply::
				ShouldConsumeRestartForFreshRenderer(false, true));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldConsumeRestartForFreshRenderer(true, true));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldConsumeRestartForFreshRenderer(false, false));
		}

		TEST_METHOD(AutoFrameOffsetWaitsForUsableCaptureMode)
		{
			Assert::IsFalse(ConfigurationLiveApply::
				HasUsableCaptureModeForAutoOffset(false, false, false));
			Assert::IsFalse(ConfigurationLiveApply::
				HasUsableCaptureModeForAutoOffset(true, false, true));
			Assert::IsFalse(ConfigurationLiveApply::
				HasUsableCaptureModeForAutoOffset(true, true, false));
			Assert::IsTrue(ConfigurationLiveApply::
				HasUsableCaptureModeForAutoOffset(true, true, true));
		}

		TEST_METHOD(OmittedHardwareSelectionsUseFirstDiscoveredValues)
		{
			Assert::IsTrue(ConfigurationLiveApply::
				ShouldSelectFirstDiscoveredValue(false, -1, 2));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldSelectFirstDiscoveredValue(true, -1, 2));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldSelectFirstDiscoveredValue(false, 0, 2));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldSelectFirstDiscoveredValue(false, -1, 0));
		}

		TEST_METHOD(FailedReloadRestoresAcceptedRendererSelection)
		{
			Assert::IsTrue(ConfigurationLiveApply::
				ShouldRestoreAcceptedRendererAfterReload(false, true, true));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldRestoreAcceptedRendererAfterReload(true, true, true));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldRestoreAcceptedRendererAfterReload(false, false, true));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldRestoreAcceptedRendererAfterReload(false, true, false));
		}

		TEST_METHOD(ShortcutTableStagesWithoutRendererRestart)
		{
			Assert::IsTrue(ConfigurationLiveApply::
				ShouldStageShortcutTable(true, false));
			Assert::IsTrue(ConfigurationLiveApply::
				ShouldStageShortcutTable(false, true));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldStageShortcutTable(false, false));
		}

		TEST_METHOD(ConfigurationPublicationPreservesTransientPresentation)
		{
			const auto windowedVideoOnly = ConfigurationLiveApply::
				PreserveSessionPresentation(true, false);
			Assert::IsTrue(windowedVideoOnly.videoOnly);
			Assert::IsFalse(windowedVideoOnly.fullscreen);

			const auto fullscreenWithUi = ConfigurationLiveApply::
				PreserveSessionPresentation(false, true);
			Assert::IsFalse(fullscreenWithUi.videoOnly);
			Assert::IsTrue(fullscreenWithUi.fullscreen);
			Assert::IsFalse(ConfigurationLiveApply::
				SessionPresentationControlIsPersistent());
		}

		TEST_METHOD(FullscreenToggleUsesRequestedAndEffectiveState)
		{
			using Action = ConfigurationLiveApply::FullscreenToggleAction;
			using Direction = ConfigurationLiveApply::
				FullscreenTransitionDirection;
			const auto pendingRequest = ConfigurationLiveApply::
				ResolveFullscreenToggle(true, false, Direction::None);
			const auto pendingEnter = ConfigurationLiveApply::
				ResolveFullscreenToggle(true, false, Direction::Entering);
			const auto pendingExit = ConfigurationLiveApply::
				ResolveFullscreenToggle(false, false, Direction::Exiting);
			const auto active = ConfigurationLiveApply::
				ResolveFullscreenToggle(true, true, Direction::None);
			const auto inactive = ConfigurationLiveApply::
				ResolveFullscreenToggle(false, false, Direction::None);
			Assert::IsTrue(pendingRequest == Action::ExitFullscreen);
			Assert::IsTrue(pendingEnter == Action::CancelPending);
			Assert::IsTrue(pendingExit == Action::EnterFullscreen);
			Assert::IsTrue(active == Action::ExitFullscreen);
			Assert::IsTrue(inactive == Action::EnterFullscreen);
			Assert::IsFalse(ConfigurationLiveApply::
				FullscreenRequestedAfterToggle(pendingEnter));
			Assert::IsFalse(ConfigurationLiveApply::
				FullscreenRequestedAfterToggle(active));
			Assert::IsTrue(ConfigurationLiveApply::
				FullscreenRequestedAfterToggle(inactive));
			Assert::IsFalse(ConfigurationLiveApply::
				EffectiveFullscreenToggleActive(true, false));
			Assert::IsTrue(ConfigurationLiveApply::
				EffectiveFullscreenToggleActive(false, true));
			Assert::IsFalse(ConfigurationLiveApply::
				EffectiveFullscreenToggleActive(false, false));
		}

		TEST_METHOD(RightAltFullscreenAndModifiedEnterAreFailSafe)
		{
			Assert::IsTrue(ConfigurationLiveApply::
				FullscreenShortcutModifiersMatch(
					false, true, false, false, true, false, false));
			Assert::IsTrue(ConfigurationLiveApply::
				FullscreenShortcutModifiersMatch(
					false, true, false, true, true, false, true));
			Assert::IsFalse(ConfigurationLiveApply::
				FullscreenShortcutModifiersMatch(
					false, true, false, true, true, false, false));
			Assert::IsTrue(ConfigurationLiveApply::
				FullscreenShortcutModifiersMatch(
					true, true, false, true, true, false, true));
			Assert::IsFalse(ConfigurationLiveApply::
				FullscreenShortcutModifiersMatch(
					false, true, true, true, true, false, true));

			Assert::IsTrue(ConfigurationLiveApply::
				ShouldConsumeUnmatchedModifiedEnter(
					true, 0x0d, true, true, false));
			Assert::IsTrue(ConfigurationLiveApply::
				ShouldConsumeUnmatchedModifiedEnter(
					true, 0x0d, false, false, true));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldConsumeUnmatchedModifiedEnter(
					true, 0x0d, false, false, false));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldConsumeUnmatchedModifiedEnter(
					false, 0x0d, true, true, false));
			Assert::IsFalse(ConfigurationLiveApply::
				ShouldConsumeUnmatchedModifiedEnter(
					true, 'R', true, true, false));
		}

		TEST_METHOD(VideoOnlyRetainsForegroundEscapeShortcuts)
		{
			Assert::AreEqual(static_cast<int>('U'), static_cast<int>(
				ConfigurationLiveApply::VideoOnlyToggleDefaultKey));
			Assert::AreEqual(0x0c, static_cast<int>(
				ConfigurationLiveApply::VideoOnlyToggleDefaultModifiers));
			Assert::AreEqual(0x0d, static_cast<int>(
				ConfigurationLiveApply::ViewToggleDefaultKey));
			Assert::AreEqual(0x10, static_cast<int>(
				ConfigurationLiveApply::ViewToggleDefaultModifiers));
			Assert::IsTrue(ConfigurationLiveApply::
				MayDispatchForegroundPresentationShortcut(true, true));
			Assert::IsFalse(ConfigurationLiveApply::
				MayDispatchForegroundPresentationShortcut(false, true));
		}

		TEST_METHOD(WindowCloseAlwaysAdvancesSafeShutdown)
		{
			Assert::IsTrue(ApplicationShutdownPolicy::
				IsCloseSystemCommand(0xf060));
			Assert::IsTrue(ApplicationShutdownPolicy::
				IsCloseSystemCommand(0xf061));
			Assert::IsFalse(ApplicationShutdownPolicy::
				IsCloseSystemCommand(0xf020));
			Assert::IsTrue(ApplicationShutdownPolicy::
				MayFinalizeCaptureAfterStopReturns(true, true));
			Assert::IsFalse(ApplicationShutdownPolicy::
				MayFinalizeCaptureAfterStopReturns(false, true));
			Assert::IsFalse(ApplicationShutdownPolicy::
				MayFinalizeCaptureAfterStopReturns(true, false));

			using Source = ApplicationShutdownPolicy::CloseSource;
			using Lifecycle = ApplicationShutdownPolicy::Lifecycle;
			for (const Source source : {
				Source::MainDialog, Source::OperatorView,
				Source::WindowedVideoHost, Source::FullscreenHost,
				Source::RendererSurface, Source::StatsOverlay,
				Source::OwnedTopLevel })
			{
				for (const Lifecycle lifecycle : {
					Lifecycle::Running, Lifecycle::StoppingRenderer,
					Lifecycle::RetiringRenderer, Lifecycle::Terminating })
				{
					const auto decision = ApplicationShutdownPolicy::
						ResolveClose(source, lifecycle);
					Assert::IsTrue(decision.routeToCoordinator);
					Assert::IsTrue(decision.consumeOriginal);
					Assert::IsTrue(decision.preserveSourceSurface);
					Assert::AreEqual(
						lifecycle == Lifecycle::Terminating,
						decision.advanceExistingTermination);
				}
			}
			Assert::IsTrue(ApplicationShutdownPolicy::
				IsAltF4(0x0104, 0x73, true));
			Assert::IsFalse(ApplicationShutdownPolicy::
				IsAltF4(0x0104, 0x73, false));
			Assert::IsFalse(ApplicationShutdownPolicy::
				IsAltF4(0x0104, 'R', true));
		}

		TEST_METHOD(ApplicationInterfaceDefaultsToModern)
		{
			const auto selection = ApplicationInterface::Resolve(
				false, {}, {});
			Assert::IsTrue(selection.mode == ApplicationInterface::Mode::Modern);
			Assert::IsTrue(selection.source ==
				ApplicationInterface::Source::DefaultModern);
			Assert::IsTrue(selection.warning.empty());
		}

		TEST_METHOD(ApplicationInterfaceConfigurationSelectsEitherUi)
		{
			const auto classic = ApplicationInterface::Resolve(false, {},
				ApplicationInterface::ParsePreference(
					true, " CLASSIC ", "configured"));
			const auto modern = ApplicationInterface::Resolve(false, {},
				ApplicationInterface::ParsePreference(
					true, "MoDeRn", "configured"));
			Assert::IsTrue(classic.mode == ApplicationInterface::Mode::Classic);
			Assert::IsTrue(modern.mode == ApplicationInterface::Mode::Modern);
			Assert::IsTrue(modern.source ==
				ApplicationInterface::Source::Configuration);
		}

		TEST_METHOD(ApplicationInterfaceCommandLineOverridesConfiguration)
		{
			const auto commandLine = ApplicationInterface::ParseCommandLine(
				{ L"VideoProcessor.exe", L"/InTeRfAcE", L"modern" });
			const auto configuration = ApplicationInterface::ParsePreference(
				true, "classic", "configured");
			const auto selection = ApplicationInterface::Resolve(
				false, commandLine, configuration);
			Assert::IsTrue(selection.mode == ApplicationInterface::Mode::Modern);
			Assert::IsTrue(selection.source ==
				ApplicationInterface::Source::CommandLine);
		}

		TEST_METHOD(ApplicationInterfaceInvalidCommandLineFallsBackToConfiguration)
		{
			const auto commandLine = ApplicationInterface::ParseCommandLine(
				{ L"VideoProcessor.exe", L"/interface", L"future" });
			const auto configuration = ApplicationInterface::ParsePreference(
				true, "modern", "configured");
			const auto selection = ApplicationInterface::Resolve(
				false, commandLine, configuration);
			Assert::IsTrue(selection.mode == ApplicationInterface::Mode::Modern);
			Assert::IsTrue(selection.source ==
				ApplicationInterface::Source::Configuration);
			Assert::IsTrue(selection.warning.find("expected classic or modern") !=
				std::string::npos);
		}

		TEST_METHOD(ApplicationInterfaceMissingCommandLineValueFallsBackToModern)
		{
			const auto commandLine = ApplicationInterface::ParseCommandLine(
				{ L"VideoProcessor.exe", L"/interface", L"/fullscreen" });
			const auto selection = ApplicationInterface::Resolve(
				false, commandLine, {});
			Assert::IsTrue(selection.mode == ApplicationInterface::Mode::Modern);
			Assert::IsTrue(selection.warning.find("missing value") !=
				std::string::npos);
		}

		TEST_METHOD(ApplicationInterfaceNoUiAlwaysWins)
		{
			const auto commandLine = ApplicationInterface::ParseCommandLine(
				{ L"VideoProcessor.exe", L"/interface", L"modern" });
			const auto selection = ApplicationInterface::Resolve(
				true, commandLine, ApplicationInterface::ParsePreference(
					true, "classic", "configured"));
			Assert::IsTrue(selection.mode == ApplicationInterface::Mode::None);
			Assert::IsTrue(selection.source == ApplicationInterface::Source::NoUi);
		}

		TEST_METHOD(ApplicationInterfaceValuesAreCaseInsensitiveAndTrimmed)
		{
			const auto classic = ApplicationInterface::ParsePreference(
				true, "  ClAsSiC\t", "configured");
			const auto modern = ApplicationInterface::ParseCommandLine(
				{ L"VideoProcessor.exe", L"/INTERFACE", L"MoDeRn" });
			Assert::IsTrue(classic.valid);
			Assert::IsTrue(classic.mode == ApplicationInterface::Mode::Classic);
			Assert::IsTrue(modern.valid);
			Assert::IsTrue(modern.mode == ApplicationInterface::Mode::Modern);
		}

		TEST_METHOD(ApplicationInterfaceDuplicateOptionWarnsAndUsesConfiguration)
		{
			const auto commandLine = ApplicationInterface::ParseCommandLine(
				{ L"VideoProcessor.exe", L"/interface", L"modern",
				  L"/interface", L"classic" });
			const auto selection = ApplicationInterface::Resolve(false,
				commandLine, ApplicationInterface::ParsePreference(
					true, "modern", "configured"));
			Assert::IsFalse(commandLine.valid);
			Assert::IsTrue(selection.mode == ApplicationInterface::Mode::Modern);
			Assert::IsTrue(selection.source ==
				ApplicationInterface::Source::Configuration);
			Assert::IsFalse(selection.warning.empty());
		}

		TEST_METHOD(ApplicationInterfaceNoUiSuppressesInvalidSelection)
		{
			const auto commandLine = ApplicationInterface::ParseCommandLine(
				{ L"VideoProcessor.exe", L"/interface", L"future" });
			const auto selection = ApplicationInterface::Resolve(true,
				commandLine, ApplicationInterface::ParsePreference(
					true, "modern", "configured"));
			Assert::IsTrue(selection.mode == ApplicationInterface::Mode::None);
			Assert::IsTrue(selection.source == ApplicationInterface::Source::NoUi);
			Assert::IsFalse(selection.warning.empty());
		}

		TEST_METHOD(ModernOperatorLayoutMatchesApprovedDefaultGeometry)
		{
			const auto layout = ModernOperatorLayout::Calculate(1600, 671);
			Assert::AreEqual(16, layout.information.x);
			Assert::AreEqual(70, layout.information.y);
			Assert::AreEqual(512, layout.information.width);
			Assert::AreEqual(544, layout.preview.x);
			Assert::AreEqual(70, layout.preview.y);
			Assert::AreEqual(1040, layout.preview.width);
			Assert::AreEqual(585, layout.preview.height);
			Assert::AreEqual(655,
				layout.preview.y + layout.preview.height);
		}

		TEST_METHOD(ModernOperatorLayoutScalesApprovedGeometryWithDpi)
		{
			const auto layout = ModernOperatorLayout::Calculate(2400, 1007, 144);
			Assert::AreEqual(24, layout.information.x);
			Assert::AreEqual(105, layout.information.y);
			Assert::AreEqual(768, layout.information.width);
			Assert::AreEqual(820, layout.preview.x);
			Assert::AreEqual(107, layout.preview.y);
			Assert::AreEqual(1552, layout.preview.width);
			Assert::AreEqual(873, layout.preview.height);
		}

		TEST_METHOD(ModernOperatorHeaderControlsAreCompactAndDpiAligned)
		{
			const auto normal = ModernOperatorLayout::
				CalculateHeaderControls(1600);
			Assert::AreEqual(16, 1600 -
				(normal.configuration.x + normal.configuration.width));
			Assert::AreEqual(8, normal.configuration.x -
				(normal.fullscreen.x + normal.fullscreen.width));
			Assert::AreEqual(8, normal.fullscreen.x -
				(normal.videoOnly.x + normal.videoOnly.width));
			Assert::AreEqual(32, normal.configuration.width);
			Assert::AreEqual(90, normal.fullscreen.width);
			Assert::AreEqual(86, normal.videoOnly.width);

			const auto highDpi = ModernOperatorLayout::
				CalculateHeaderControls(2400, 144);
			Assert::AreEqual(24, 2400 -
				(highDpi.configuration.x + highDpi.configuration.width));
			Assert::AreEqual(12, highDpi.configuration.x -
				(highDpi.fullscreen.x + highDpi.fullscreen.width));
			Assert::AreEqual(highDpi.configuration.y, highDpi.fullscreen.y);
			Assert::AreEqual(highDpi.fullscreen.y, highDpi.videoOnly.y);
		}

		TEST_METHOD(ModernCapturedVideoNeverLeaksTemplatePlaceholders)
		{
			for (const wchar_t* placeholder : {
				L"", L"<display mode>", L"<pixel format>",
				L"<color space>", L"<colorspace>", L"<eotf>",
				L"<encoding>", L"<bd>" })
			Assert::AreEqual(std::wstring(L"---"),
				ModernOperatorStatusPolicy::NormalizeCapturedValue(placeholder));
			Assert::AreEqual(std::wstring(L"3840x2160p - 4K UHDTV"),
				ModernOperatorStatusPolicy::NormalizeCapturedValue(
					L"3840x2160p - 4K UHDTV"));
			Assert::AreEqual(std::wstring(L"P010"),
				ModernOperatorStatusPolicy::NormalizeCapturedValue(L"P010"));
			Assert::AreEqual(std::wstring(L"BT.2020"),
				ModernOperatorStatusPolicy::NormalizeCapturedValue(L"BT.2020"));
			Assert::AreEqual(std::wstring(L"PQ"),
				ModernOperatorStatusPolicy::NormalizeCapturedValue(L"PQ"));
		}

		TEST_METHOD(ModernOperatorPreviewRemainsSixteenByNineWhenResized)
		{
			const auto layout = ModernOperatorLayout::Calculate(1900, 900);
			Assert::IsTrue(layout.preview.x >= 544);
			Assert::IsTrue(layout.preview.y >= 70);
			Assert::IsTrue(layout.preview.x + layout.preview.width <= 1884);
			Assert::IsTrue(layout.preview.y + layout.preview.height <= 884);
			Assert::AreEqual(
				layout.preview.width * 9,
				layout.preview.height * 16);
		}

		TEST_METHOD(ModernOperatorPreviewStaysBoundedAtTwoHundredPercentDpi)
		{
			const auto layout = ModernOperatorLayout::Calculate(3200, 1342, 192);
			Assert::AreEqual(layout.preview.width * 9,
				layout.preview.height * 16);
			Assert::IsTrue(layout.preview.x >= 1088);
			Assert::IsTrue(layout.preview.y >= 140);
			Assert::IsTrue(layout.preview.x + layout.preview.width <= 3168);
			Assert::IsTrue(layout.preview.y + layout.preview.height <= 1310);
		}

		TEST_METHOD(NoUiVideoGeometryIsSixteenByNineAtDefaultAndMinimum)
		{
			Assert::AreEqual(NoUiLayout::DefaultClientWidth * 9,
				NoUiLayout::DefaultClientHeight * 16);
			Assert::AreEqual(NoUiLayout::MinimumClientWidth * 9,
				NoUiLayout::MinimumClientHeight * 16);
			Assert::AreEqual(960, NoUiLayout::DefaultClientWidth);
			Assert::AreEqual(540, NoUiLayout::DefaultClientHeight);
			Assert::AreEqual(320, NoUiLayout::MinimumClientWidth);
			Assert::AreEqual(180, NoUiLayout::MinimumClientHeight);
		}

		TEST_METHOD(VideoOnlyFillsClientWithoutChangingOuterWindowBounds)
		{
			const ModernOperatorLayout::Rect outer = { 37, 52, 1600, 900 };
			const auto video = NoUiLayout::FillClient(1584, 839);
			Assert::AreEqual(0, video.x);
			Assert::AreEqual(0, video.y);
			Assert::AreEqual(1584, video.width);
			Assert::AreEqual(839, video.height);
			Assert::IsTrue(NoUiLayout::PreservesOuterBounds(outer, outer));
			Assert::IsFalse(NoUiLayout::PreservesOuterBounds(
				outer, { 37, 52, 960, 540 }));
		}

		TEST_METHOD(VideoOnlyImmediatelyReflowsVideoBoundsInBothDirections)
		{
			const ModernOperatorLayout::Rect outer = { 37, 52, 1600, 900 };
			const ModernOperatorLayout::Rect operatorPreview = {
				544, 70, 1040, 585 };
			const auto videoOnly = NoUiLayout::ResolveVideoBounds(
				true, 1600, 839, operatorPreview);
			const auto operatorView = NoUiLayout::ResolveVideoBounds(
				false, 1600, 839, operatorPreview);
			Assert::AreEqual(0, videoOnly.x);
			Assert::AreEqual(0, videoOnly.y);
			Assert::AreEqual(1600, videoOnly.width);
			Assert::AreEqual(839, videoOnly.height);
			Assert::AreEqual(operatorPreview.x, operatorView.x);
			Assert::AreEqual(operatorPreview.y, operatorView.y);
			Assert::AreEqual(operatorPreview.width, operatorView.width);
			Assert::AreEqual(operatorPreview.height, operatorView.height);
			Assert::IsTrue(NoUiLayout::PreservesOuterBounds(outer, outer));
		}

		TEST_METHOD(DeckLinkEightBitYuvUsesHdycOnlyForRec709)
		{
			Assert::AreEqual(static_cast<int>(VideoFrameEncoding::HDYC),
				static_cast<int>(Translate(
					bmdFormat8BitYUV, ColorSpace::REC_709)));
			for (const ColorSpace colorSpace : {
				ColorSpace::UNKNOWN, ColorSpace::REC_601_525,
				ColorSpace::REC_601_576, ColorSpace::REC_601_625,
				ColorSpace::BT_2020 })
			{
				Assert::AreEqual(static_cast<int>(VideoFrameEncoding::UYVY),
					static_cast<int>(Translate(bmdFormat8BitYUV, colorSpace)));
			}
			Assert::IsTrue(IsEqualGUID(
				TranslateToMediaSubType(VideoFrameEncoding::HDYC),
				MEDIASUBTYPE_HDYC));
			Assert::IsTrue(IsEqualGUID(
				TranslateToMediaSubType(VideoFrameEncoding::UYVY),
				MEDIASUBTYPE_UYVY));
		}

		TEST_METHOD(DeckLinkCapturePackingDefaultsRemainCanonical)
		{
			const DeckLinkCaptureFormatPreferences preferences;
			const auto rgb8 = static_cast<BMDDetectedVideoInputFormatFlags>(
				bmdDetectedVideoInputRGB444 | bmdDetectedVideoInput8BitDepth);
			const auto rgb10 = static_cast<BMDDetectedVideoInputFormatFlags>(
				bmdDetectedVideoInputRGB444 | bmdDetectedVideoInput10BitDepth);
			const auto rgb12 = static_cast<BMDDetectedVideoInputFormatFlags>(
				bmdDetectedVideoInputRGB444 | bmdDetectedVideoInput12BitDepth);
			Assert::AreEqual(static_cast<unsigned int>(bmdFormat8BitARGB),
				static_cast<unsigned int>(PreferredDeckLinkCapturePixelFormat(
					rgb8, preferences)));
			Assert::AreEqual(static_cast<unsigned int>(bmdFormat10BitRGB),
				static_cast<unsigned int>(PreferredDeckLinkCapturePixelFormat(
					rgb10, preferences)));
			Assert::AreEqual(static_cast<unsigned int>(bmdFormat12BitRGB),
				static_cast<unsigned int>(PreferredDeckLinkCapturePixelFormat(
					rgb12, preferences)));
		}

		TEST_METHOD(DeckLinkCapturePackingConfigurationExposesEveryRgbVariant)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-decklink-packing.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[decklink]\n"
					"rgb_8bit_packing: BGRA\n"
					"rgb_10bit_packing: R10L\n"
					"rgb_12bit_packing: R12L\n";
			}
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error));
			DeckLinkCaptureFormatPreferences preferences;
			Assert::IsTrue(ReadDeckLinkCaptureFormatPreferences(
				config, preferences, error));
			Assert::AreEqual(static_cast<unsigned int>(bmdFormat8BitBGRA),
				static_cast<unsigned int>(preferences.rgb8));
			Assert::AreEqual(static_cast<unsigned int>(bmdFormat10BitRGBXLE),
				static_cast<unsigned int>(preferences.rgb10));
			Assert::AreEqual(static_cast<unsigned int>(bmdFormat12BitRGBLE),
				static_cast<unsigned int>(preferences.rgb12));
			Assert::AreEqual(static_cast<int>(VideoFrameEncoding::BGRA_8BIT),
				static_cast<int>(Translate(preferences.rgb8, ColorSpace::REC_709)));
			Assert::AreEqual(static_cast<int>(VideoFrameEncoding::R10l),
				static_cast<int>(Translate(preferences.rgb10, ColorSpace::REC_709)));
			Assert::AreEqual(static_cast<int>(VideoFrameEncoding::R12L),
				static_cast<int>(Translate(preferences.rgb12, ColorSpace::REC_709)));
			const auto rgb8 = static_cast<BMDDetectedVideoInputFormatFlags>(
				bmdDetectedVideoInputRGB444 | bmdDetectedVideoInput8BitDepth);
			const auto rgb10 = static_cast<BMDDetectedVideoInputFormatFlags>(
				bmdDetectedVideoInputRGB444 | bmdDetectedVideoInput10BitDepth);
			const auto rgb12 = static_cast<BMDDetectedVideoInputFormatFlags>(
				bmdDetectedVideoInputRGB444 | bmdDetectedVideoInput12BitDepth);
			Assert::AreEqual(static_cast<unsigned int>(bmdFormat8BitBGRA),
				static_cast<unsigned int>(PreferredDeckLinkCapturePixelFormat(
					rgb8, preferences)));
			Assert::AreEqual(static_cast<unsigned int>(bmdFormat10BitRGBXLE),
				static_cast<unsigned int>(PreferredDeckLinkCapturePixelFormat(
					rgb10, preferences)));
			Assert::AreEqual(static_cast<unsigned int>(bmdFormat12BitRGBLE),
				static_cast<unsigned int>(PreferredDeckLinkCapturePixelFormat(
					rgb12, preferences)));

			// The other 10-bit alternative is independently accepted.
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[decklink]\nrgb_10bit_packing: R10B\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsTrue(ReadDeckLinkCaptureFormatPreferences(
				config, preferences, error));
			Assert::AreEqual(static_cast<unsigned int>(bmdFormat10BitRGBX),
				static_cast<unsigned int>(preferences.rgb10));
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(DeckLinkCapturePackingFallsBackWhenDeviceRejectsPreference)
		{
			DeckLinkCaptureFormatPreferences preferences;
			preferences.rgb8 = bmdFormat8BitBGRA;
			preferences.rgb10 = bmdFormat10BitRGBX;
			preferences.rgb12 = bmdFormat12BitRGBLE;
			const auto rgb10 = static_cast<BMDDetectedVideoInputFormatFlags>(
				bmdDetectedVideoInputRGB444 | bmdDetectedVideoInput10BitDepth);

			bool usedFallback = false;
			BMDPixelFormat selected = ResolveDeckLinkCapturePixelFormat(
				rgb10, preferences,
				[](BMDPixelFormat format)
					{ return format == bmdFormat10BitRGBX; }, usedFallback);
			Assert::IsFalse(usedFallback);
			Assert::AreEqual(static_cast<unsigned int>(bmdFormat10BitRGBX),
				static_cast<unsigned int>(selected));

			selected = ResolveDeckLinkCapturePixelFormat(rgb10, preferences,
				[](BMDPixelFormat) { return false; }, usedFallback);
			Assert::IsTrue(usedFallback);
			Assert::AreEqual(static_cast<unsigned int>(bmdFormat10BitRGB),
				static_cast<unsigned int>(selected));
		}

		TEST_METHOD(DeckLinkCapturePackingSchemaRejectsInvalidDepthVariant)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-decklink-invalid-packing.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[decklink]\nrgb_8bit_packing: R12L\n";
			}
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("rgb_8bit_packing") != std::string::npos);
			DeleteFileA(path.c_str());
		}

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

		TEST_METHOD(Vp0079OwnerVariantsPersistAndRestoreProfileState)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0079-config.cfg";
			const std::string statePath = path.substr(0, path.size() - 4) +
				".state";
			DeleteFileA(statePath.c_str());
			DeleteFileA((statePath + ".tmp").c_str());
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n"
					"renderer: VideoProcessor Renderer (Alpha)\n"
					"fullscreen: true\n"
					"[renderer_alias]\nvp: 1\nmadvr: 2\n"
					"[queue]\nwhen: $key==\"l\"\nqueue_size: 32\nlead_frames: 4\ntarget_frames: 3\nactive_picture_lookahead_frames: 2\n"
					"[queue.low_latency]\nwhen: $key==\"Shift+L\"\nqueue_size: 1\ntarget_frames: 1\n"
					"[directshow]\nvideo_conversion: V210_TO_P010\nframe_offset: 90\n"
					"[directshow.conversion]\nconversion_method: SIMD\nmin_core_count: 1\nmax_core_count: 2\n"
					"[directshow.ppm]\nppm: -17\n"
					"[vprenderer]\nwhen: $key==\"F4\"\nquality: high\nswitch_refresh_rate: true\n"
					"[vprenderer.rec709]\nwhen: $key==\"F5\"\ntone_mapping: spline\n"
					"[vprenderer.viewport]\nwhen: $key==\"F3\"\n"
					"[vprenderer.viewport.scope]\nwhen: $key==\"F2\"\nscreen_aspect: 2.1:1\nvertical_alignment: TOP\nautomatic_crop: true\nsubtitle_fit: true\n"
					"[actions.audio_delay_film]\non: refresh.applied,refresh.confirmed\nwhen: $actual_refresh<=30\nrun: C:\\Videoprocessor\\audio\\audio_delay.bat 100\n"
					"[shader.nls]\nwhen: $key==\"n\"\n"
					"[shader.nls.standard]\nwhen: $key==\"Shift+n\"\nshader_type: nls\nglsl_file: NLS.glsl\n"
					"[shader.cleanup]\ntype: multi\nwhen: $key==\"d\"\n"
					"[shader.cleanup.deband]\nwhen: $key==\"Shift+D\"\nshader_type: custom\nhlsl_file: Deband.hlsl\nstage: pre_resize\norder: 30\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());
			RendererProfileConfig::Model model;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(model.persistSelection);
			Assert::AreEqual(static_cast<size_t>(3), model.groups.size());

			std::vector<RendererProfileConfig::KeySelection> selections;
			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "Shift+L",
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
			Assert::AreEqual(21ull, viewport.screenAspect.numerator);
			Assert::AreEqual(10ull, viewport.screenAspect.denominator);
			Assert::IsTrue(viewport.hasScreenAspect);
			Assert::AreEqual("top", viewport.verticalAlignment.c_str());
			Assert::IsTrue(viewport.automaticCrop);
			Assert::IsTrue(viewport.subtitleFit);

			UnifiedProfileRuntime::Runtime runtime;
			Assert::IsTrue(runtime.Initialize(config,
				[](const std::string&, std::string&) { return false; }, error));
			Assert::AreEqual(statePath.c_str(), runtime.StatePath().c_str());
			UnifiedProfileRuntime::SelectionResult result;
			Assert::IsTrue(runtime.SelectKey("L",
				[](const std::string&, std::string&) { return false; },
				result, error));
			Assert::IsTrue(result.changed);
			Assert::IsTrue(result.snapshot->queue.hasQueueSize);
			Assert::AreEqual(static_cast<size_t>(32),
				result.snapshot->queue.queueSize);

			Assert::IsTrue(runtime.SelectKey("F2",
				[](const std::string&, std::string&) { return false; },
				result, error));
			Assert::IsTrue(result.changed);
			Assert::AreEqual("scope", result.snapshot->viewport.profile.c_str());

			std::ifstream state(statePath);
			const std::string stateContents(
				(std::istreambuf_iterator<char>(state)),
				std::istreambuf_iterator<char>());
			Assert::IsTrue(stateContents.find("profile.viewport: scope") !=
				std::string::npos);

			ConfigFile restoredConfig;
			Assert::IsTrue(restoredConfig.Load(path));
			UnifiedProfileRuntime::Runtime restoredRuntime;
			Assert::IsTrue(restoredRuntime.Initialize(restoredConfig,
				[](const std::string&, std::string&) { return false; }, error));
			Assert::AreEqual("scope",
				restoredRuntime.GetSnapshot()->viewport.profile.c_str());
			DeleteFileA(path.c_str());
			DeleteFileA(statePath.c_str());
			DeleteFileA((statePath + ".tmp").c_str());
		}

		TEST_METHOD(UnifiedQueueProfileRestartPolicyUsesCommittedLatestQueueSelection)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-queue-profile-restart.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[queue]\n"
					"when: $key==\"F1\"\n"
					"queue_size: 32\n"
					"[queue.low_latency]\n"
					"when: $key==\"F2\"\n"
					"queue_size: 4\n"
					"[vprenderer.viewport]\n"
					"screen_aspect: 16:9\n"
					"[vprenderer.viewport.scope]\n"
					"when: $key==\"F3\"\n"
					"screen_aspect: 2.35:1\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			UnifiedProfileRuntime::Runtime runtime;
			Assert::IsTrue(runtime.Initialize(config,
				[](const std::string&, std::string&) { return false; }, error),
				std::wstring(error.begin(), error.end()).c_str());

			UnifiedProfileRuntime::SelectionResult queueSelection;
			Assert::IsTrue(runtime.SelectKey("F2",
				[](const std::string&, std::string&) { return false; },
				queueSelection, error));
			Assert::IsTrue(queueSelection.changed);
			Assert::IsTrue(runtime.GetSnapshot() == queueSelection.snapshot);
			Assert::IsTrue(QueueProfileRestartPolicy::
				RequiresResetAfterManualSelection(true,
					queueSelection.snapshot->queue.profile,
					queueSelection.changed));

			QueueProfileRestartPolicy::PendingRequest pending;
			Assert::IsTrue(QueueProfileRestartPolicy::EnqueueResult::Queued ==
				QueueProfileRestartPolicy::Enqueue(pending,
					queueSelection.snapshot->generation,
					queueSelection.snapshot->queue.profile, "shortcut:F2"));

			UnifiedProfileRuntime::SelectionResult reselect;
			Assert::IsTrue(runtime.SelectKey("F2",
				[](const std::string&, std::string&) { return false; }, reselect,
				error));
			Assert::IsFalse(reselect.changed);
			Assert::IsFalse(QueueProfileRestartPolicy::
				RequiresResetAfterManualSelection(true,
					reselect.snapshot->queue.profile, reselect.changed));

			UnifiedProfileRuntime::SelectionResult viewportSelection;
			Assert::IsTrue(runtime.SelectKey("F3",
				[](const std::string&, std::string&) { return false; },
				viewportSelection, error));
			Assert::IsTrue(viewportSelection.changed);
			Assert::IsFalse(QueueProfileRestartPolicy::
				RequiresResetAfterManualSelection(false,
					viewportSelection.snapshot->queue.profile,
					viewportSelection.changed));
			Assert::IsFalse(QueueProfileRestartPolicy::
				RequiresResetAfterManualSelection(false, "base", true));

			UnifiedProfileRuntime::SelectionResult firstRapidSelection;
			Assert::IsTrue(runtime.SelectKey("F1",
				[](const std::string&, std::string&) { return false; },
				firstRapidSelection, error));
			Assert::IsTrue(QueueProfileRestartPolicy::
				RequiresResetAfterManualSelection(true,
					firstRapidSelection.snapshot->queue.profile,
					firstRapidSelection.changed));
			Assert::IsTrue(QueueProfileRestartPolicy::EnqueueResult::Coalesced ==
				QueueProfileRestartPolicy::Enqueue(pending,
					firstRapidSelection.snapshot->generation,
					firstRapidSelection.snapshot->queue.profile, "shortcut:F1"));

			UnifiedProfileRuntime::SelectionResult finalRapidSelection;
			Assert::IsTrue(runtime.SelectKey("F2",
				[](const std::string&, std::string&) { return false; },
				finalRapidSelection, error));
			Assert::IsTrue(QueueProfileRestartPolicy::
				RequiresResetAfterManualSelection(true,
					finalRapidSelection.snapshot->queue.profile,
					finalRapidSelection.changed));
			Assert::IsTrue(QueueProfileRestartPolicy::EnqueueResult::Coalesced ==
				QueueProfileRestartPolicy::Enqueue(pending,
					finalRapidSelection.snapshot->generation,
					finalRapidSelection.snapshot->queue.profile, "shortcut:F2"));

			QueueProfileRestartPolicy::PendingRequest finalRequest;
			Assert::IsTrue(QueueProfileRestartPolicy::Consume(pending,
				finalRequest));
			Assert::AreEqual(finalRapidSelection.snapshot->queue.profile.c_str(),
				finalRequest.profile.c_str());
			Assert::IsFalse(QueueProfileRestartPolicy::Consume(pending,
				finalRequest));
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(FreshAlphaConstructionConsumesMatchingPendingProfileReset)
		{
			QueueProfileRestartPolicy::PendingRequest pending;
			Assert::IsTrue(QueueProfileRestartPolicy::EnqueueResult::Queued ==
				QueueProfileRestartPolicy::Enqueue(
					pending, 8, "vp_60", "shortcut:Shift+V"));

			QueueProfileRestartPolicy::FreshConstructionEvidence evidence;
			evidence.freshConstruction = true;
			evidence.running = true;
			evidence.rendererGeneration = 4;
			evidence.appliedSnapshotGeneration = 10;
			evidence.appliedProfile = "vp_60";
			evidence.currentSnapshotGeneration = 10;
			evidence.currentProfile = "vp_60";

			QueueProfileRestartPolicy::PendingRequest satisfied;
			Assert::IsTrue(QueueProfileRestartPolicy::
				ConsumeIfSatisfiedByFreshConstruction(
					pending, evidence, satisfied));
			Assert::IsFalse(pending.pending);
			Assert::IsTrue(satisfied.pending);
			Assert::AreEqual<uint64_t>(8, satisfied.snapshotGeneration);
			Assert::AreEqual("vp_60", satisfied.profile.c_str());
			Assert::AreEqual("shortcut:Shift+V", satisfied.source.c_str());
		}

		TEST_METHOD(RejectedQueueProfileResetSubmissionRestoresExactIdentity)
		{
			QueueProfileRestartPolicy::PendingRequest pending;
			QueueProfileRestartPolicy::Enqueue(
				pending, 23, "madvr_queue", "shortcut:Shift+M");
			QueueProfileRestartPolicy::PendingRequest dispatched;
			Assert::IsTrue(QueueProfileRestartPolicy::Consume(
				pending, dispatched));
			Assert::IsTrue(QueueProfileRestartPolicy::RestoreConsumedIfEmpty(
				pending, dispatched));
			Assert::AreEqual<uint64_t>(23, pending.snapshotGeneration);
			Assert::AreEqual("madvr_queue", pending.profile.c_str());
			Assert::AreEqual("shortcut:Shift+M", pending.source.c_str());

			QueueProfileRestartPolicy::PendingRequest newer;
			QueueProfileRestartPolicy::Enqueue(
				newer, 24, "vp_queue", "shortcut:Shift+V");
			Assert::IsFalse(QueueProfileRestartPolicy::RestoreConsumedIfEmpty(
				newer, dispatched));
			Assert::AreEqual<uint64_t>(24, newer.snapshotGeneration);
			Assert::AreEqual("vp_queue", newer.profile.c_str());
		}

		TEST_METHOD(FreshDirectShowConstructionRequiresCompleteConsistentAudit)
		{
			QueueProfileRestartPolicy::PendingRequest pending;
			QueueProfileRestartPolicy::Enqueue(
				pending, 14, "madvr_queue", "shortcut:Shift+M");

			QueueProfileRestartPolicy::FreshConstructionEvidence evidence;
			evidence.freshConstruction = true;
			evidence.running = true;
			evidence.directShow = true;
			evidence.rendererGeneration = 5;
			evidence.appliedSnapshotGeneration = 14;
			evidence.appliedProfile = "madvr_queue";
			evidence.currentSnapshotGeneration = 14;
			evidence.currentProfile = "madvr_queue";

			QueueProfileRestartPolicy::PendingRequest satisfied;
			Assert::IsFalse(QueueProfileRestartPolicy::
				ConsumeIfSatisfiedByFreshConstruction(
					pending, evidence, satisfied));
			Assert::IsTrue(pending.pending);
			Assert::AreEqual("shortcut:Shift+M", pending.source.c_str());

			evidence.directShowAuditComplete = true;
			Assert::IsFalse(QueueProfileRestartPolicy::
				ConsumeIfSatisfiedByFreshConstruction(
					pending, evidence, satisfied));
			Assert::IsTrue(pending.pending);

			evidence.directShowAuditConsistent = true;
			Assert::IsTrue(QueueProfileRestartPolicy::
				ConsumeIfSatisfiedByFreshConstruction(
					pending, evidence, satisfied));
			Assert::AreEqual("shortcut:Shift+M", satisfied.source.c_str());
		}

		TEST_METHOD(FreshConstructionDoesNotConsumeNewerOrChangedProfileIntent)
		{
			QueueProfileRestartPolicy::PendingRequest pending;
			QueueProfileRestartPolicy::Enqueue(
				pending, 15, "madvr_queue", "rule-context:renderer-shortcut");
			const QueueProfileRestartPolicy::PendingRequest original = pending;

			QueueProfileRestartPolicy::FreshConstructionEvidence evidence;
			evidence.rendererGeneration = 5;
			evidence.appliedSnapshotGeneration = 15;
			evidence.appliedProfile = "madvr_queue";
			evidence.currentSnapshotGeneration = 15;
			evidence.currentProfile = "madvr_queue";

			QueueProfileRestartPolicy::PendingRequest satisfied;
			Assert::IsFalse(QueueProfileRestartPolicy::
				ConsumeIfSatisfiedByFreshConstruction(
					pending, evidence, satisfied));
			Assert::AreEqual(original.source.c_str(), pending.source.c_str());

			evidence.freshConstruction = true;
			Assert::IsFalse(QueueProfileRestartPolicy::
				ConsumeIfSatisfiedByFreshConstruction(
					pending, evidence, satisfied));
			Assert::AreEqual(original.source.c_str(), pending.source.c_str());

			evidence.running = true;
			evidence.appliedSnapshotGeneration = 14;
			evidence.currentSnapshotGeneration = 14;
			Assert::IsFalse(QueueProfileRestartPolicy::
				ConsumeIfSatisfiedByFreshConstruction(
					pending, evidence, satisfied));
			Assert::AreEqual(original.snapshotGeneration,
				pending.snapshotGeneration);
			Assert::AreEqual(original.profile.c_str(), pending.profile.c_str());
			Assert::AreEqual(original.source.c_str(), pending.source.c_str());

			evidence.appliedSnapshotGeneration = 15;
			evidence.currentSnapshotGeneration = 16;
			Assert::IsFalse(QueueProfileRestartPolicy::
				ConsumeIfSatisfiedByFreshConstruction(
					pending, evidence, satisfied));
			Assert::IsTrue(pending.pending);

			evidence.appliedSnapshotGeneration = 16;
			evidence.appliedProfile = "vp_60";
			evidence.currentProfile = "vp_60";
			Assert::IsFalse(QueueProfileRestartPolicy::
				ConsumeIfSatisfiedByFreshConstruction(
					pending, evidence, satisfied));
			Assert::IsTrue(pending.pending);
			Assert::AreEqual("rule-context:renderer-shortcut",
				pending.source.c_str());
		}

		TEST_METHOD(UnifiedProfileRuntimeReloadsEditedViewportAndKeepsSelection)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-live-profile-reload.cfg";
			auto writeConfiguration = [&path](const char* scopeAspect)
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[vprenderer.viewport]\n"
					"when: $key==\"F3\"\n"
					"screen_aspect: 16:9\n"
					"[vprenderer.viewport.scope]\n"
					"when: $key==\"F2\"\n"
					"screen_aspect: " << scopeAspect << "\n";
			};

			writeConfiguration("2.35:1");
			ConfigFile initialConfig;
			Assert::IsTrue(initialConfig.Load(path));
			std::string error;
			UnifiedProfileRuntime::Runtime runtime;
			Assert::IsTrue(runtime.Initialize(initialConfig,
				[](const std::string&, std::string&) { return false; }, error),
				std::wstring(error.begin(), error.end()).c_str());
			UnifiedProfileRuntime::SelectionResult selected;
			Assert::IsTrue(runtime.SelectKey("F2",
				[](const std::string&, std::string&) { return false; },
				selected, error));
			Assert::AreEqual("scope",
				selected.snapshot->viewport.profile.c_str());

			writeConfiguration("2.40:1");
			ConfigFile editedConfig;
			Assert::IsTrue(editedConfig.Load(path));
			UnifiedProfileRuntime::RefreshResult reloaded;
			Assert::IsTrue(runtime.Reload(editedConfig,
				[](const std::string&, std::string&) { return false; },
				reloaded, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(reloaded.changed);
			Assert::AreEqual("scope",
				reloaded.snapshot->viewport.profile.c_str());
			Assert::AreEqual(12ull,
				reloaded.snapshot->viewport.screenAspect.numerator);
			Assert::AreEqual(5ull,
				reloaded.snapshot->viewport.screenAspect.denominator);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(ZoomShortcutNeverChangesSelectedScreenProfile)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-independent-zoom.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[vprenderer.viewport]\n"
					"screen_aspect: 16:9\n"
					"[vprenderer.viewport.scope]\n"
					"when: $key==\"F2\"\n"
					"screen_aspect: 2.35:1\n"
					"[vprenderer.zoom]\n"
					"crop_narrower_content_to_fill_screen: true\n"
					"[vprenderer.zoom.scope]\n"
					"when: $key==\"F2\"\n"
					"crop_narrower_content_to_fill_screen: false\n"
					"[vprenderer.zoom.scope_and_crop]\n"
					"when: $key==\"Shift+2\"\n"
					"crop_narrower_content_aspect_limit: 2.20:1\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			UnifiedProfileRuntime::Runtime runtime;
			Assert::IsTrue(runtime.Initialize(config,
				[](const std::string&, std::string&) { return false; }, error),
				std::wstring(error.begin(), error.end()).c_str());
			UnifiedProfileRuntime::SelectionResult selected;
			Assert::IsTrue(runtime.SelectKey("F2",
				[](const std::string&, std::string&) { return false; },
				selected, error));
			Assert::AreEqual("scope", selected.snapshot->viewport.profile.c_str());
			Assert::AreEqual("scope", selected.snapshot->viewport.zoomProfile.c_str());
			Assert::AreEqual(2.35, selected.snapshot->viewport.screenAspect.value,
				0.000001);
			Assert::IsFalse(selected.snapshot->viewport.cropNarrowerContentToFillScreen);

			Assert::IsTrue(runtime.SelectKey("Shift+2",
				[](const std::string&, std::string&) { return false; },
				selected, error));
			Assert::AreEqual("scope", selected.snapshot->viewport.profile.c_str());
			Assert::AreEqual("scope_and_crop",
				selected.snapshot->viewport.zoomProfile.c_str());
			Assert::AreEqual(2.35, selected.snapshot->viewport.screenAspect.value,
				0.000001);
			Assert::IsTrue(selected.snapshot->viewport.cropNarrowerContentToFillScreen);
			Assert::IsTrue(selected.snapshot->viewport.hasCropNarrowerContentAspectLimit);
			Assert::AreEqual(2.20,
				selected.snapshot->viewport.cropNarrowerContentAspectLimit.value,
				0.000001);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(Vp0103VerticalAlignmentDefaultsValidatesAndPublishes)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0103-vertical-alignment.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nrenderer: VideoProcessor Renderer (Alpha)\n"
					"profile_update_mode: never\n"
					"[vprenderer.viewport]\nscreen_aspect: 16:9\n"
					"[vprenderer.viewport.scope]\nwhen: $key==\"F2\"\n"
					"screen_aspect: 16:9\nvertical_alignment: BOTTOM\n"
					"crop_narrower_content_to_fill_screen: true\n"
					"crop_narrower_content_aspect_limit: 2.20:1\n"
					"crop_wider_content_to_fill_screen: true\n"
					"crop_wider_content_aspect_limit: 2.76:1\n"
					"subtitle_fit: true\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			RendererProfileConfig::Model model;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			RendererProfileConfig::ResolvedViewport base;
			Assert::IsTrue(RendererProfileConfig::ResolveViewport(
				model, "base", 1, base, error));
			Assert::AreEqual("center", base.verticalAlignment.c_str());
			RendererProfileConfig::ResolvedViewport scope;
			Assert::IsTrue(RendererProfileConfig::ResolveViewport(
				model, "scope", 2, scope, error));
			Assert::AreEqual("bottom", scope.verticalAlignment.c_str());
			Assert::IsTrue(scope.cropNarrowerContentToFillScreen);
			Assert::IsTrue(scope.hasCropNarrowerContentAspectLimit);
			Assert::AreEqual(2.20,
				scope.cropNarrowerContentAspectLimit.value, 0.000001);
			Assert::IsTrue(scope.cropWiderContentToFillScreen);
			Assert::IsTrue(scope.hasCropWiderContentAspectLimit);
			Assert::AreEqual(2.76,
				scope.cropWiderContentAspectLimit.value, 0.000001);

			UnifiedProfileRuntime::Runtime runtime;
			Assert::IsTrue(runtime.Initialize(config,
				[](const std::string&, std::string&) { return false; }, error));
			UnifiedProfileRuntime::SelectionResult selected;
			Assert::IsTrue(runtime.SelectKey("F2",
				[](const std::string&, std::string&) { return false; },
				selected, error));
			std::string published;
			Assert::IsTrue(selected.snapshot->variables.Lookup(
				"vertical_alignment", published));
			Assert::AreEqual("bottom", published.c_str());
			const StateVariables::Value* narrowerAspectLimit =
				selected.snapshot->variables.Find(
					"$crop_narrower_content_aspect_limit");
			Assert::IsNotNull(narrowerAspectLimit);
			Assert::IsTrue(narrowerAspectLimit->type ==
				StateVariables::ValueType::Aspect);
			Assert::AreEqual(2.20, narrowerAspectLimit->number, 0.000001);
			const StateVariables::Value* widerAspectLimit =
				selected.snapshot->variables.Find(
					"$crop_wider_content_aspect_limit");
			Assert::IsNotNull(widerAspectLimit);
			Assert::IsTrue(widerAspectLimit->type ==
				StateVariables::ValueType::Aspect);
			Assert::AreEqual(2.76, widerAspectLimit->number, 0.000001);

			// Optional limits are best-effort. A legacy or malformed value must not
			// reject the complete renderer configuration.
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nrenderer: VideoProcessor Renderer (Alpha)\n"
					"[vprenderer.viewport]\n"
					"screen_aspect: 2.35:1\n"
					"crop_narrower_content_aspect_limit: 2.20:1\n"
					"crop_wider_content_aspect_limit: 2.76:1\n"
					"[vprenderer.viewport.scope]\nwhen: $key==\"F2\"\n"
					"crop_narrower_content_aspect_limit: legacy-missing\n"
					"crop_wider_content_aspect_limit: not-a-ratio\n";
			}
			Assert::IsTrue(config.Load(path));
			error.clear();
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(RendererProfileConfig::ResolveViewport(
				model, "scope", 3, scope, error));
			Assert::IsFalse(scope.hasCropNarrowerContentAspectLimit);
			Assert::IsFalse(scope.hasCropWiderContentAspectLimit);

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[vprenderer.viewport]\n"
					"crop_narrower_content_aspect_limit: not-a-ratio\n"
					"crop_wider_content_aspect_limit: 4.1:1\n";
			}
			Assert::IsTrue(config.Load(path));
			error.clear();
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(RendererProfileConfig::ResolveViewport(
				model, "base", 4, base, error));
			Assert::IsFalse(base.hasCropNarrowerContentAspectLimit);
			Assert::IsFalse(base.hasCropWiderContentAspectLimit);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(Vp0098RejectsRemovedSecondScreenAspect)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0098-single-screen-aspect.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[vprenderer.viewport.scope]\n"
					"when: $key==\"F2\"\n"
					"screen_aspect: 2.35:1\n"
					"physical_screen_aspect: 2.35:1\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find("physical_screen_aspect") !=
				std::string::npos);
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
					"[queue]\nqueue_size: 16\n"
					"[queue.low_latency]\nwhen: $eotf==\"pq\"\nqueue_size: 1\n"
					"[vprenderer]\nquality: high\n"
					"[vprenderer.color.rec709]\nshortcut: F5\n"
					"sdr_target_primaries: REC709\n"
					"[vprenderer.color.bt2020]\nshortcut: F6\n"
					"sdr_target_primaries: BT2020\n"
					"[vprenderer.viewport]\nlabel: 16x9\nscreen_aspect: 16:9\n"
					"[vprenderer.viewport.scope]\nwhen: $eotf==\"pq\"\n"
					"label: Scope\nscreen_aspect: 2.35:1\nautomatic_crop: true\n"
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
					"[actions.scope_visible_name]\n"
					"on: profile.viewport.changed\n"
					"when: $screen_config==\"Scope\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n"
					"[actions.scope_left]\n"
					"on: profile.viewport.changed\n"
					"when: $previous_profile.viewport==\"scope\" && $profile.viewport==\"base\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n"
					"[actions.queue_canonical]\n"
					"on: profile.queue.changed\n"
					"when: ${profile.queue}==\"low_latency\"\n"
					"run: C:\\Windows\\System32\\cmd.exe ${profile.queue}\n"
					"[actions.queue_ui_label]\n"
					"on: profile.queue.changed\n"
					"when: ${profile.queue}==\"Low Latency\"\n"
					"run: C:\\Windows\\System32\\cmd.exe ${profile.queue}\n"
					"[actions.queue_ui_label_left]\n"
					"on: profile.queue.changed\n"
					"when: ${previous_profile.queue}==\"Low Latency\" && ${profile.queue}==\"base\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n"
					"[actions.color_bt2020]\n"
					"on: profile.color.changed\n"
					"when: $profile.color==\"bt2020\" && $previous_profile.color==\"rec709\"\n"
					"coalesce_role: color-state\n"
					"delay_seconds: 0\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n"
					"[actions.renderer_scope_ready]\n"
					"on: renderer.ready\n"
					"when: $event_reason==\"renderer_ready\" && $profile.viewport==\"scope\"\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n"
					"[actions.refresh_values]\n"
					"on: refresh.applied\n"
					"when: $requested_refresh==\"59.94\"\n"
					"run: C:\\Windows\\System32\\cmd.exe ${actual_refresh} ${requested_refresh} ${previous_refresh}\n"
					"[actions.any_commit]\n"
					"on: state.committed\n"
					"run: C:\\Windows\\System32\\cmd.exe /c exit 0\n"
					"[actions.incomplete_draft]\n"
					"enabled: false\n"
					"on: not.a.real.event\n"
					"when: (\n"
					"run: sd\n"
					"renderer: unavailable\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			RendererProfileConfig::Model model;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::AreEqual(static_cast<size_t>(14), model.actions.size());
			const auto configuredColorAction = std::find_if(model.actions.begin(),
				model.actions.end(), [](const auto& action)
				{
					return action.name == "color_bt2020";
				});
			Assert::IsTrue(configuredColorAction != model.actions.end());
			Assert::AreEqual("color-state",
				configuredColorAction->coalesceRole.c_str());
			Assert::AreEqual(0, configuredColorAction->delaySeconds);

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
				"any_commit", "state.committed"));
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
			Assert::IsTrue(hasInvocation(entered.actions,
				"scope_visible_name", "profile.viewport.changed"));
			std::string visibleScreenConfig;
			Assert::IsTrue(entered.snapshot->LookupVariable(
				"screen_config", visibleScreenConfig));
			Assert::AreEqual("Scope", visibleScreenConfig.c_str());
			Assert::IsTrue(hasInvocation(entered.actions,
				"queue_canonical", "profile.queue.changed"));
			const auto queueUiLabel = std::find_if(entered.actions.begin(),
				entered.actions.end(), [](const UnifiedProfileRuntime::ActionInvocation& action)
				{
					return action.action.name == "queue_ui_label" &&
						action.event == "profile.queue.changed";
				});
			Assert::IsTrue(queueUiLabel != entered.actions.end());
			Assert::AreEqual("low_latency", queueUiLabel->action.arguments.c_str());

			std::vector<UnifiedProfileRuntime::ActionInvocation> ready;
			Assert::IsTrue(runtime.CollectActionInvocations("renderer.ready",
				"renderer_ready", nullptr, entered.snapshot, ready, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(hasInvocation(ready, "renderer_scope_ready",
				"renderer.ready"));

			std::vector<UnifiedProfileRuntime::ActionInvocation> refresh;
			const EventActionLauncher::ActionValueLookup refreshValues =
				[](const std::string& variable, std::string& value)
				{
					const std::map<std::string, std::string> values = {
						{ "actual_refresh", "59.9401" },
						{ "requested_refresh", "59.94" },
						{ "previous_refresh", "23.976" }
					};
					const auto found = values.find(variable);
					if (found == values.end()) return false;
					value = found->second;
					return true;
				};
			Assert::IsTrue(runtime.CollectActionInvocations("refresh.applied",
				"refresh", nullptr, entered.snapshot, refresh, error, refreshValues),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(hasInvocation(refresh, "refresh_values",
				"refresh.applied"));
			Assert::AreEqual("59.9401 59.94 23.976",
				refresh.front().action.arguments.c_str());

			UnifiedProfileRuntime::RefreshResult left;
			Assert::IsTrue(runtime.Refresh(source("sdr", "bt2020"), left, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(left.changed);
			Assert::AreEqual("base", left.snapshot->viewport.profile.c_str());
			Assert::IsTrue(hasInvocation(left.actions, "scope_left",
				"profile.viewport.changed"));
			Assert::IsTrue(hasInvocation(left.actions, "queue_ui_label_left",
				"profile.queue.changed"));

			// Establish the color selection explicitly before exercising the
			// Rec.709-to-BT.2020 transition. Persisted selections from a previous
			// test run are intentionally a fallback and must not make this action
			// assertion dependent on the temporary state file.
			UnifiedProfileRuntime::SelectionResult rec709Selection;
			Assert::IsTrue(runtime.SelectKey("F5", source("sdr", "bt2020"),
				rec709Selection, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(rec709Selection.changed);
			Assert::AreEqual("rec709", rec709Selection.snapshot->
				effectiveSelections.at("color").c_str());

			UnifiedProfileRuntime::SelectionResult colorSelection;
			Assert::IsTrue(runtime.SelectKey("F6", source("sdr", "bt2020"),
				colorSelection, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(colorSelection.changed);
			Assert::AreEqual("bt2020", colorSelection.snapshot->
				effectiveSelections.at("color").c_str());
			Assert::IsTrue(hasInvocation(colorSelection.actions, "color_bt2020",
				"profile.color.changed"));
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(PendingEventActionsCoalesceToNewestTrigger)
		{
			RendererProfileConfig::Model::EventAction action;
			action.name = "Renderer Nits";
			action.renderer = "VP Renderer";
			action.rendererSelectorIndex = 2;
			const std::string identity =
				EventActionLauncher::ActionIdentity(action);

			EventActionLauncher::PendingActionCoalescer coalescer;
			const uint64_t first = coalescer.Schedule(identity);
			const uint64_t second = coalescer.Schedule(identity);
			Assert::IsFalse(coalescer.Claim(identity, first));
			Assert::IsTrue(coalescer.Claim(identity, second));
			Assert::IsFalse(coalescer.Claim(identity, second));

			RendererProfileConfig::Model::EventAction sameAction = action;
			sameAction.rendererSelectorIndex = 3;
			const std::string sameIdentity =
				EventActionLauncher::ActionIdentity(sameAction);
			Assert::AreEqual(identity.c_str(), sameIdentity.c_str());
			const uint64_t other = coalescer.Schedule(sameIdentity);
			coalescer.CancelAll();
			Assert::IsFalse(coalescer.Claim(sameIdentity, other));

			RendererProfileConfig::Model::EventAction rec709 = action;
			rec709.name = "Rec709";
			rec709.coalesceRole = "Color State";
			RendererProfileConfig::Model::EventAction bt2020 = rec709;
			bt2020.name = "BT2020";
			const std::string colorIdentity =
				EventActionLauncher::ActionIdentity(rec709);
			Assert::AreEqual(colorIdentity.c_str(),
				EventActionLauncher::ActionIdentity(bt2020).c_str());
			const uint64_t oldColor = coalescer.Schedule(colorIdentity);
			const uint64_t newColor = coalescer.Schedule(colorIdentity);
			Assert::IsFalse(coalescer.Claim(colorIdentity, oldColor));
			Assert::IsTrue(coalescer.Claim(colorIdentity, newColor));
		}

		TEST_METHOD(ProfileChangeOverlayUsesFriendlyOrderedLabels)
		{
			const auto defaultTiming = ProfileChangeOverlay::ResolveTiming(5);
			Assert::IsTrue(defaultTiming.Enabled());
			Assert::AreEqual(5000ull, defaultTiming.totalMilliseconds);
			Assert::AreEqual(4125ull, defaultTiming.holdMilliseconds);
			Assert::AreEqual(875ull, defaultTiming.fadeMilliseconds);
			const auto disabledTiming = ProfileChangeOverlay::ResolveTiming(0);
			Assert::IsFalse(disabledTiming.Enabled());
			Assert::AreEqual(0ull, disabledTiming.totalMilliseconds);

			const std::map<std::string, std::string> previous = {
				{ "display", "rec709_169_med" },
				{ "color", "bt2020" },
				{ "viewport", "viewport_16x9" }
			};
			const std::map<std::string, std::string> current = {
				{ "display", "rec709_scope_med" },
				{ "color", "rec709" },
				{ "viewport", "scope" }
			};
			const auto items = ProfileChangeOverlay::CollectChanges(
				previous, current, "Scope");
			Assert::AreEqual(static_cast<size_t>(3), items.size());
			Assert::AreEqual("Rendering", items[0].label.c_str());
			Assert::AreEqual("Rec709 Scope Med", items[0].value.c_str());
			Assert::AreEqual("Color", items[1].label.c_str());
			Assert::AreEqual("Rec709", items[1].value.c_str());
			Assert::AreEqual("Screen", items[2].label.c_str());
			Assert::AreEqual("Scope", items[2].value.c_str());
		}

		TEST_METHOD(ProfileOverlayIncludesQueueAndConfiguredNlsState)
		{
			const std::map<std::string, std::string> previous = {
				{ "display", "rec709_scope_med" },
				{ "queue", "standard" },
				{ "nls", "off" }
			};
			const std::map<std::string, std::string> current = {
				{ "display", "rec709_scope_med" },
				{ "queue", "low_latency" },
				{ "nls", "on:Nonlinear Stretch Protected" }
			};
			const auto changed = ProfileChangeOverlay::CollectChanges(
				previous, current);
			Assert::AreEqual(static_cast<size_t>(2), changed.size());
			Assert::AreEqual("Queue", changed[0].label.c_str());
			Assert::AreEqual("Low Latency", changed[0].value.c_str());
			Assert::AreEqual("NLS", changed[1].label.c_str());
			Assert::AreEqual("Nonlinear Stretch Protected",
				changed[1].value.c_str());
			Assert::IsTrue(changed[1].indicator ==
				ProfileChangeOverlay::Item::Indicator::On);

			const auto all = ProfileChangeOverlay::CollectAll(current);
			Assert::AreEqual(static_cast<size_t>(3), all.size());
			Assert::AreEqual("Rendering", all[0].label.c_str());
			Assert::AreEqual("Queue", all[1].label.c_str());
			Assert::AreEqual("NLS", all[2].label.c_str());

			const auto disabled = ProfileChangeOverlay::CollectChanges(
				current, previous);
			Assert::AreEqual(static_cast<size_t>(2), disabled.size());
			Assert::AreEqual("Off", disabled[1].value.c_str());
			Assert::IsTrue(disabled[1].indicator ==
				ProfileChangeOverlay::Item::Indicator::Off);
		}

		TEST_METHOD(ProfileOverlayOmitsGroupsWithoutAChoice)
		{
			const std::map<std::string, std::string> selections = {
				{ "display", "cinema" },
				{ "queue", "normal" },
				{ "scaling", "sharp" },
				{ "nls", "on:Protected" }
			};
			const std::map<std::string, size_t> optionCounts = {
				{ "display", 3 },
				{ "queue", 1 },
				{ "scaling", 0 },
				{ "nls", 2 }
			};
			const auto filtered =
				ProfileChangeOverlay::FilterSingleOptionGroups(
					selections, optionCounts);
			Assert::AreEqual(static_cast<size_t>(2), filtered.size());
			Assert::IsTrue(filtered.find("display") != filtered.end());
			Assert::IsTrue(filtered.find("nls") != filtered.end());
			Assert::IsTrue(filtered.find("queue") == filtered.end());
			Assert::IsTrue(filtered.find("scaling") == filtered.end());
		}

		TEST_METHOD(ProfileChangeDisplayDurationIsBoundedAndLive)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory),
				temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-profile-display-duration-test.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nprofile_change_display_seconds: 60\n"
					"[vprenderer]\nquality: high\n";
			}
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(ConfigurationApplyPolicy::Action::ApplyInterface ==
				ConfigurationApplyPolicy::ClassifyChange(
					{ "general", "profile_change_display_seconds" }));
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nprofile_change_display_seconds: 61\n"
					"[vprenderer]\nquality: high\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("profile_change_display_seconds") !=
				std::string::npos);
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
					"[actions.indexed]\n"
					"renderer: 3\n"
					"on: renderer.ready\n"
					"when: $event_reason==\"renderer_ready\"\n"
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
			Assert::AreEqual(static_cast<size_t>(4), model.actions.size());
			Assert::AreEqual("vprenderer", model.actions[0].renderer.c_str());
			Assert::AreEqual(0, model.actions[0].rendererSelectorIndex);
			Assert::AreEqual("cinema_renderer", model.actions[1].renderer.c_str());
			Assert::AreEqual(2, model.actions[1].rendererSelectorIndex);
			Assert::AreEqual("3", model.actions[2].renderer.c_str());
			Assert::AreEqual(3, model.actions[2].rendererSelectorIndex);
			Assert::AreEqual("*", model.actions[3].renderer.c_str());
			Assert::AreEqual(0, model.actions[3].rendererSelectorIndex);
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

		TEST_METHOD(Vp0097NamedViewportsUseFileOrderAndIgnoreLabels)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0097-named-viewports.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nrenderer: VideoProcessor Renderer (Alpha)\n"
					"[vprenderer]\nquality: balanced\n"
					"[vprenderer.viewport.scope_cinema]\n"
					"label: Scope Cinema 2.35:1\n"
					"mode: scope\n"
					"screen_aspect: 2.35:1\n"
					"automatic_crop: true\n"
					"[vprenderer.viewport.flat]\n"
					"label: Flat View\n"
					"when: $key==\"F2\"\n"
					"subtitle_fit: true\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());
			RendererProfileConfig::Model model;
			const bool read = RendererProfileConfig::Read(config, model, error);
			if (!read)
				Logger::WriteMessage(("RendererProfileConfig::Read: " + error).c_str());
			Assert::IsTrue(read, std::wstring(error.begin(), error.end()).c_str());
			const auto viewport = std::find_if(model.groups.begin(), model.groups.end(),
				[](const RendererProfileConfig::Group& group)
				{ return group.name == "viewport"; });
			Assert::IsTrue(viewport != model.groups.end());
			Assert::AreEqual("scope_cinema", viewport->defaultSelection.c_str());
			Assert::AreEqual("scope_cinema", viewport->profiles[0].c_str());
			Assert::AreEqual("flat", viewport->profiles[1].c_str());
			const auto flat = model.profiles.find("viewport.flat");
			Assert::IsTrue(flat != model.profiles.end());
			Assert::AreEqual("2.35:1", flat->second.settings.at("screen_aspect").c_str());
			Assert::IsTrue(flat->second.settings.find("label") == flat->second.settings.end());
			const auto scopeCinema = model.profiles.find("viewport.scope_cinema");
			Assert::IsTrue(scopeCinema != model.profiles.end());
			Assert::IsTrue(scopeCinema->second.settings.find("mode") ==
				scopeCinema->second.settings.end(),
				L"Legacy viewport mode must never masquerade as an applied setting.");
			Assert::IsTrue(std::any_of(model.warnings.begin(), model.warnings.end(),
				[](const std::string& warning)
				{ return warning.find("key 'mode' is deprecated and ignored") != std::string::npos; }),
				L"The model must warn that screen_aspect replaces viewport mode.");

			std::vector<RendererProfileConfig::AutomaticSelection> automatic;
			Assert::IsTrue(RendererProfileConfig::SelectAutomatic(model,
				[](const std::string&, std::string&) { return false; }, automatic, error));
			const auto automaticViewport = std::find_if(automatic.begin(), automatic.end(),
				[](const RendererProfileConfig::AutomaticSelection& selection)
				{ return selection.group == "viewport"; });
			Assert::IsTrue(automaticViewport != automatic.end());
			Assert::AreEqual("scope_cinema", automaticViewport->profile.c_str());
			Assert::IsTrue(automaticViewport->configuredDefault);

			std::vector<RendererProfileConfig::KeySelection> selections;
			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "F2",
				[](const std::string&, std::string&) { return false; }, selections, error));
			const auto selectedViewport = std::find_if(selections.begin(), selections.end(),
				[](const RendererProfileConfig::KeySelection& selection)
				{ return selection.group == "viewport"; });
			Assert::IsTrue(selectedViewport != selections.end());
			Assert::AreEqual("flat", selectedViewport->profile.c_str());
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(Vp0123InputProcessingChildIsNotADisplayProfile)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0123-input-processing-child.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nrenderer: VideoProcessor Renderer (Alpha)\n"
					"[vprenderer]\nquality: balanced\n"
					"[vprenderer.input_processing]\n"
					"video_conversion: none\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());
			RendererProfileConfig::Model model;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			const auto display = std::find_if(model.groups.begin(), model.groups.end(),
				[](const RendererProfileConfig::Group& group)
				{ return group.name == "display"; });
			Assert::IsTrue(display != model.groups.end());
			Assert::AreEqual(static_cast<size_t>(1), display->profiles.size());
			Assert::AreEqual("base", display->profiles.front().c_str());
			const auto base = model.profiles.find("display.base");
			Assert::IsTrue(base != model.profiles.end());
			Assert::IsTrue(base->second.settings.find("video_conversion") ==
				base->second.settings.end(),
				L"Input processing was misclassified as display-profile state.");
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RenderingProfilesUseExactParsedNames)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-rendering-profile-names.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nrenderer: VideoProcessor Renderer (Alpha)\n"
					"[vprenderer.Rec709_Scope_Med]\nquality: high\n"
					"[vprenderer.BT2020_Scope_High]\nquality: high\n"
					"[vprenderer.viewport.scope]\nscreen_aspect: 2.35:1\n"
					"[vprenderer.output.Default]\n"
					"[vprenderer.input_processing]\nvideo_conversion: none\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::vector<std::string> profiles;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::CollectRenderingProfileNames(
				config, profiles, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::AreEqual(static_cast<size_t>(2), profiles.size());
			// ConfigFile defines profile identifiers as normalized, case-insensitive
			// names. Consumers must use the model's identifiers verbatim instead
			// of deriving a second list from section text.
			Assert::AreEqual("rec709_scope_med", profiles[0].c_str());
			Assert::AreEqual("bt2020_scope_high", profiles[1].c_str());
			RendererProfileConfig::Model model;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(model.profiles.find("display." + profiles[0]) !=
				model.profiles.end());
			Assert::IsTrue(model.profiles.find("display." + profiles[1]) !=
				model.profiles.end());
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(Vp0097ShortcutKeyCombinesWithOptionalProfileRule)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0097-shortcut-profile.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nrenderer: VideoProcessor Renderer (Alpha)\n"
					"[queue]\nshortcut: l\nqueue_size: 32\nlead_frames: 4\ntarget_frames: 3\n"
					"[queue.low_latency]\nwhen: $source_rate > 30\nshortcut: Shift+L\nqueue_size: 1\ntarget_frames: 1\n"
					"[vprenderer]\nshortcut: F4\nquality: high\n"
					"[vprenderer.cinema]\nwhen: $transfer == \"PQ\"\nshortcut: F6\n"
					"sdr_target_primaries: BT2020\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());
			RendererProfileConfig::Model model;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());

			std::vector<RendererProfileConfig::KeySelection> selections;
			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "F6",
				[](const std::string&, std::string&) { return false; }, selections, error));
			Assert::IsTrue(std::any_of(selections.begin(), selections.end(),
				[](const RendererProfileConfig::KeySelection& selected)
					{ return selected.group == "display" && selected.profile == "cinema"; }));
			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "Shift+L",
				[](const std::string&, std::string&) { return false; }, selections, error));
			Assert::IsTrue(std::any_of(selections.begin(), selections.end(),
				[](const RendererProfileConfig::KeySelection& selected)
					{ return selected.group == "queue" && selected.profile == "low_latency"; }));
			std::string canonical;
			Assert::IsTrue(RendererProfileConfig::CanonicalizeKeyChord(
				"Control+f2", canonical));
			Assert::AreEqual("Ctrl+F2", canonical.c_str());
			Assert::IsTrue(RendererProfileConfig::CanonicalizeKeyChord(
				"ShIfT+l", canonical));
			Assert::AreEqual("Shift+L", canonical.c_str());
			Assert::IsTrue(RendererProfileConfig::CanonicalizeKeyChord("l", canonical));
			Assert::AreEqual("L", canonical.c_str());
			Assert::IsTrue(RendererProfileConfig::CanonicalizeKeyChord("L", canonical));
			Assert::AreEqual("L", canonical.c_str());
			Assert::IsTrue(RendererProfileConfig::CanonicalizeKeyChord(
				"cTrL+sHiFt+l", canonical));
			Assert::AreEqual("Ctrl+Shift+L", canonical.c_str());

			std::vector<RendererProfileConfig::AutomaticSelection> automatic;
			Assert::IsTrue(RendererProfileConfig::SelectAutomatic(model,
				[](const std::string& variable, std::string& value)
				{
					if (variable == "transfer") { value = "PQ"; return true; }
					if (variable == "source_rate") { value = "24"; return true; }
					return false;
				}, automatic, error));
			Assert::IsTrue(std::any_of(automatic.begin(), automatic.end(),
				[](const RendererProfileConfig::AutomaticSelection& selected)
					{ return selected.group == "display" && selected.profile == "cinema"; }));

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[vprenderer]\nquality: high\n"
					"[vprenderer.bad]\nshortcut: Ctrl+Shift\nquality: balanced\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsFalse(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find("shortcut") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(AutomaticQueueProfilesMayUseRendererAndActualRefresh)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory),
				temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-queue-renderer-refresh.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[queue]\nqueue_size: 16\n"
					"[queue.madvr]\nwhen: ${renderer}==\"madVR\"\nqueue_size: 32\n"
					"[queue.vp_60]\nwhen: ${renderer}==\"VP Renderer\" && ${actual_refresh}>=59\nqueue_size: 8\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			auto selectQueue = [&](const char* renderer, const char* refresh)
			{
				std::vector<RendererProfileConfig::AutomaticSelection> selections;
				Assert::IsTrue(RendererProfileConfig::SelectAutomatic(model,
					[renderer, refresh](const std::string& variable, std::string& value)
					{
						if (variable == "renderer") { value = renderer; return true; }
						if (variable == "actual_refresh") { value = refresh; return true; }
						return false;
					}, selections, error));
				const auto queue = std::find_if(selections.begin(), selections.end(),
					[](const RendererProfileConfig::AutomaticSelection& selected)
					{ return selected.group == "queue"; });
				Assert::IsTrue(queue != selections.end());
				return queue->profile;
			};
			Assert::AreEqual("madvr", selectQueue("madVR", "59.94").c_str());
			Assert::AreEqual("vp_60", selectQueue("VP Renderer", "59.94").c_str());
			Assert::AreEqual("base", selectQueue("VP Renderer", "23.976").c_str());
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(AutomaticQueueRuleOverridesPersistedQueueSelection)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory),
				temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-automatic-queue-overrides-state.cfg";
			const std::string statePath = path.substr(0,
				path.find_last_of('.')) + ".state";
			DeleteFileA(path.c_str());
			DeleteFileA(statePath.c_str());
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[queue.base]\nqueue_size: 16\n"
					"[queue.vp_24]\nwhen: ${renderer}==\"VP Renderer\" && ${source_rate}<=30\nqueue_size: 8\n"
					"[queue.vp_60]\nwhen: ${renderer}==\"VP Renderer\" && ${source_rate}>30\nqueue_size: 32\n"
					"[queue.low_latency]\nshortcut: Shift+L\nqueue_size: 1\n";
			}
			{
				std::ofstream file(statePath, std::ios::out | std::ios::trunc);
				file << "# Managed by VideoProcessor.\nprofile.queue: vp_24\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			UnifiedProfileRuntime::Runtime runtime;
			std::string error;
			Assert::IsTrue(runtime.Initialize(config,
				[](const std::string& variable, std::string& value)
				{
					if (variable == "renderer") { value = "VP Renderer"; return true; }
					if (variable == "source_rate") { value = "59"; return true; }
					return false;
				}, error), std::wstring(error.begin(), error.end()).c_str());
			const std::shared_ptr<const UnifiedProfileRuntime::Snapshot> snapshot =
				runtime.GetSnapshot();
			Assert::IsTrue(snapshot != nullptr);
			Assert::AreEqual("vp_24", snapshot->manualSelections.at("queue").c_str());
			Assert::AreEqual("vp_60", snapshot->effectiveSelections.at("queue").c_str());

			UnifiedProfileRuntime::Runtime fallbackRuntime;
			Assert::IsTrue(fallbackRuntime.Initialize(config,
				[](const std::string& variable, std::string& value)
				{
					if (variable == "renderer") { value = "VP Renderer"; return true; }
					return false;
				}, error), std::wstring(error.begin(), error.end()).c_str());
			const std::shared_ptr<const UnifiedProfileRuntime::Snapshot> fallback =
				fallbackRuntime.GetSnapshot();
			Assert::IsTrue(fallback != nullptr);
			Assert::AreEqual("vp_24", fallback->effectiveSelections.at("queue").c_str());

			UnifiedProfileRuntime::SelectionResult selection;
			Assert::IsTrue(runtime.SelectKey("Shift+L",
				[](const std::string& variable, std::string& value)
				{
					if (variable == "renderer") { value = "VP Renderer"; return true; }
					if (variable == "source_rate") { value = "59"; return true; }
					return false;
				}, selection, error), std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(selection.snapshot != nullptr);
			Assert::AreEqual("low_latency",
				selection.snapshot->effectiveSelections.at("queue").c_str());
			UnifiedProfileRuntime::RefreshResult refreshed;
			Assert::IsTrue(runtime.Refresh(
				[](const std::string& variable, std::string& value)
				{
					if (variable == "renderer") { value = "VP Renderer"; return true; }
					if (variable == "source_rate") { value = "59"; return true; }
					return false;
				}, refreshed, error), std::wstring(error.begin(), error.end()).c_str());
			Assert::AreEqual("low_latency",
				refreshed.snapshot->effectiveSelections.at("queue").c_str());

			UnifiedProfileRuntime::RefreshResult reapplied;
			std::vector<std::string> clearedGroups;
			Assert::IsTrue(runtime.ReapplyRules(
				[](const std::string& variable, std::string& value)
				{
					if (variable == "renderer") { value = "VP Renderer"; return true; }
					if (variable == "source_rate") { value = "59"; return true; }
					return false;
				}, reapplied, clearedGroups, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(reapplied.changed);
			Assert::AreEqual(static_cast<size_t>(1), clearedGroups.size());
			Assert::AreEqual("queue", clearedGroups.front().c_str());
			Assert::AreEqual("vp_60",
				reapplied.snapshot->effectiveSelections.at("queue").c_str());
			Assert::AreEqual("low_latency",
				reapplied.snapshot->manualSelections.at("queue").c_str());

			std::ifstream persistedState(statePath);
			const std::string persistedText((std::istreambuf_iterator<char>(persistedState)),
				std::istreambuf_iterator<char>());
			Assert::IsTrue(persistedText.find("profile.queue: low_latency") !=
				std::string::npos);

			UnifiedProfileRuntime::RefreshResult idempotent;
			clearedGroups.clear();
			Assert::IsTrue(runtime.ReapplyRules(
				[](const std::string& variable, std::string& value)
				{
					if (variable == "renderer") { value = "VP Renderer"; return true; }
					if (variable == "source_rate") { value = "59"; return true; }
					return false;
				}, idempotent, clearedGroups, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsFalse(idempotent.changed);
			Assert::IsTrue(clearedGroups.empty());
			Assert::AreEqual("vp_60",
				idempotent.snapshot->effectiveSelections.at("queue").c_str());

			UnifiedProfileRuntime::RefreshResult rememberedFallback;
			Assert::IsTrue(runtime.Refresh(
				[](const std::string& variable, std::string& value)
				{
					if (variable == "renderer") { value = "VP Renderer"; return true; }
					return false;
				}, rememberedFallback, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(rememberedFallback.changed);
			Assert::AreEqual("low_latency", rememberedFallback.snapshot->
				effectiveSelections.at("queue").c_str());

			UnifiedProfileRuntime::Runtime restartedRuntime;
			Assert::IsTrue(restartedRuntime.Initialize(config,
				[](const std::string& variable, std::string& value)
				{
					if (variable == "renderer") { value = "VP Renderer"; return true; }
					if (variable == "source_rate") { value = "59"; return true; }
					return false;
				}, error), std::wstring(error.begin(), error.end()).c_str());
			Assert::AreEqual("vp_60", restartedRuntime.GetSnapshot()->
				effectiveSelections.at("queue").c_str());
			DeleteFileA(statePath.c_str());
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(Vp0097LegacyViewportRootRetainsPrecedenceAndRejectsDefaultId)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0097-legacy-viewport.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nrenderer: VideoProcessor Renderer (Alpha)\n"
					"[vprenderer]\nquality: balanced\n"
					"[vprenderer.viewport.scope]\nwhen: $key==\"F2\"\n"
					"screen_aspect: 2.35:1\n"
					"[vprenderer.viewport]\nlabel: Legacy base\n"
					"screen_aspect: 16:9\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			RendererProfileConfig::Model model;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			const auto viewport = std::find_if(model.groups.begin(), model.groups.end(),
				[](const RendererProfileConfig::Group& group)
				{ return group.name == "viewport"; });
			Assert::IsTrue(viewport != model.groups.end());
			Assert::AreEqual("base", viewport->defaultSelection.c_str());
			Assert::AreEqual("base", viewport->profiles[0].c_str());

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nrenderer: VideoProcessor Renderer (Alpha)\n"
					"[vprenderer]\nquality: balanced\n"
					"[vprenderer.viewport.default]\nscreen_aspect: 16:9\n";
			}
			Assert::IsTrue(config.Load(path));
			error.clear();
			Assert::IsFalse(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find("default") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(Vp0097ProfileFileOrderIsPriorityAndRootShortcutSelectsDefault)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0097-ordered-profiles.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[vprenderer]\nshortcut: F4\nquality: balanced\n"
					"[vprenderer.first]\nwhen: ${transfer} == \"PQ\"\n"
					"priority: -100\nquality: fast\n"
					"[vprenderer.second]\nwhen: ${transfer} == \"PQ\"\n"
					"priority: 100\nquality: high\n";
			}
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsFalse(model.warnings.empty());

			std::vector<RendererProfileConfig::AutomaticSelection> automatic;
			Assert::IsTrue(RendererProfileConfig::SelectAutomatic(model,
				[](const std::string& variable, std::string& value)
				{
					if (variable == "transfer") { value = "PQ"; return true; }
					return false;
				}, automatic, error));
			const auto display = std::find_if(automatic.begin(), automatic.end(),
				[](const RendererProfileConfig::AutomaticSelection& selection)
					{ return selection.group == "display"; });
			Assert::IsTrue(display != automatic.end());
			Assert::AreEqual("first", display->profile.c_str());

			std::vector<RendererProfileConfig::KeySelection> keys;
			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "f4",
				[](const std::string&, std::string&) { return false; }, keys, error));
			const auto root = std::find_if(keys.begin(), keys.end(),
				[](const RendererProfileConfig::KeySelection& selection)
					{ return selection.group == "display"; });
			Assert::IsTrue(root != keys.end());
			Assert::AreEqual("base", root->profile.c_str());
			Assert::IsFalse(root->resetToAutomatic);
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
			std::ifstream shippedFile(path, std::ios::in | std::ios::binary);
			std::ostringstream shippedText;
			shippedText << shippedFile.rdbuf();
			const std::string shipped = shippedText.str();
			Assert::IsTrue(shipped.find("\nwhen:") == std::string::npos);
			Assert::IsTrue(shipped.find("[actions.") == std::string::npos);
			Assert::IsTrue(shipped.find("[event_actions.") == std::string::npos);
			Assert::IsTrue(shipped.find("[display_rules.") == std::string::npos);

			std::string value;
			Assert::IsTrue(config.TryGetString(
				"general", "capture_device", value));
			Assert::AreEqual(
				"DeckLink Quad HDMI Recorder (1)", value.c_str());
			Assert::IsTrue(config.TryGetString("general", "renderer", value));
			Assert::AreEqual("VP Renderer", value.c_str());
			bool enabled = true;
			Assert::IsTrue(config.TryGetBool("general", "fullscreen", enabled));
			Assert::IsFalse(enabled);
			Assert::IsTrue(config.TryGetBool(
				"general", "windowed_fullscreen_mode", enabled));
			Assert::IsFalse(enabled);
			Assert::IsTrue(config.TryGetBool("general", "noui", enabled));
			Assert::IsFalse(enabled);
			Assert::IsTrue(config.TryGetBool(
				"general", "startminimized", enabled));
			Assert::IsFalse(enabled);
			Assert::IsTrue(config.TryGetBool(
				"general", "scene_detect", enabled));
			Assert::IsFalse(enabled);
			const std::pair<const char*, const char*> shortcuts[] = {
				{ "queue.normal", "Shift+Q" },
				{ "queue.low_latency", "Shift+L" },
				{ "vprenderer.color.rec709", "F5" },
				{ "vprenderer.color.bt2020", "F6" },
				{ "vprenderer.viewport.viewport_16x9", "F3" },
				{ "vprenderer.viewport.scope", "F2" },
				{ "shader.nls", "N" },
				{ "shader.nls.standard", "Shift+N" },
				{ "shader.nls.protected", "Shift+P" }
			};
			for (const auto& expected : shortcuts)
			{
				Assert::IsTrue(config.TryGetString(
					expected.first, "shortcut", value));
				Assert::AreEqual(expected.second, value.c_str());
			}
			Assert::IsTrue(config.TryGetString(
				"shader.nls.standard", "stage", value));
			Assert::AreEqual("pre_resize", value.c_str(),
				L"Bundled madVR NLS must run before madVR enlarges the presentation surface");
			Assert::IsTrue(config.TryGetString(
				"shader.nls.standard", "order", value));
			Assert::AreEqual("10", value.c_str());
			Assert::IsTrue(config.TryGetString("shader.standard", "type", value));
			Assert::AreEqual("multi", value.c_str());
			const std::pair<const char*, const char*> standardShaders[] = {
				{ "shader.standard.debanding_mild", "Debanding mild.hlsl" },
				{ "shader.standard.denoise", "Denoise.hlsl" },
				{ "shader.standard.adaptive_sharpen", "Adaptive sharpen.hlsl" },
				{ "shader.standard.invert", "Invert.hlsl" }
			};
			for (const auto& expected : standardShaders)
			{
				Assert::IsTrue(config.TryGetString(expected.first, "hlsl_file", value));
				Assert::AreEqual(expected.second, value.c_str());
				Assert::IsFalse(config.TryGetString(expected.first, "shortcut", value),
					L"Shipped standard shaders must remain opt-in");
				Assert::IsFalse(config.TryGetString(expected.first, "when", value),
					L"Shipped standard shaders must not auto-select");
			}
			std::string shaderError;
			Assert::IsTrue(ShaderConfigValidation::Validate(config, shaderError),
				std::wstring(shaderError.begin(), shaderError.end()).c_str());
			std::vector<ConfiguredShaderRule> configuredShaders;
			Assert::IsTrue(MadVRShaderLoader::ResolveConfiguredRuleSelection(
				config, "@shader-key:", ShaderRendererBackend::MADVR,
				configuredShaders,
				shaderError),
				std::wstring(shaderError.begin(), shaderError.end()).c_str());
			Assert::IsFalse(std::any_of(configuredShaders.begin(), configuredShaders.end(),
				[](const ConfiguredShaderRule& rule)
				{ return rule.name.rfind("standard.", 0) == 0; }),
				L"An unselected shipped standard shader must not enter the renderer chain");
			Assert::IsTrue(config.TryGetString(
				"shortcuts", "render.1", value));
			Assert::AreEqual("Shift+A", value.c_str());
			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());
			RendererProfileConfig::Model model;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(model.persistSelection);
			// The shipped sample must not invoke machine-local commands.
			Assert::AreEqual(static_cast<size_t>(0), model.actions.size());

			const auto viewportGroup = std::find_if(
				model.groups.begin(), model.groups.end(),
				[](const RendererProfileConfig::Group& group)
				{
					return group.name == "viewport";
				});
			Assert::IsTrue(viewportGroup != model.groups.end());
			Assert::AreEqual("viewport_16x9",
				viewportGroup->defaultSelection.c_str());
			Assert::AreEqual("viewport_16x9",
				viewportGroup->profiles.front().c_str());
			RendererProfileConfig::ResolvedViewport viewport;
			Assert::IsTrue(RendererProfileConfig::ResolveViewport(
				model, viewportGroup->defaultSelection, 1, viewport, error));
			Assert::AreEqual(16ull, viewport.screenAspect.numerator);
			Assert::AreEqual(9ull, viewport.screenAspect.denominator);
			Assert::IsFalse(viewport.automaticCrop);
			Assert::IsFalse(viewport.subtitleFit);
			Assert::IsTrue(RendererProfileConfig::ResolveViewport(
				model, "scope", 2, viewport, error));
			Assert::AreEqual(47ull, viewport.screenAspect.numerator);
			Assert::AreEqual(20ull, viewport.screenAspect.denominator);
			Assert::IsTrue(viewport.automaticCrop);
			Assert::IsTrue(viewport.subtitleFit);
		}

		TEST_METHOD(Vp0079EmptyShaderRootResolvesAsExplicitOff)
		{
			Assert::IsTrue(MadVRShaderLoader::RuleSelectorsEqual(
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
					"shortcut: n\n"
					"[shader.nls.standard]\n"
					"shortcut: Shift+n\n"
					"shader_type: nls\n"
					"glsl_file: NLS.glsl\n"
					"[shader.nls.unbound]\n"
					"shader_type: custom\n"
					"hlsl_file: Unbound.hlsl\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::vector<ConfiguredShaderRule> selection;
			std::vector<std::string> activeSections;
			std::string error;
			Assert::IsTrue(MadVRShaderLoader::ResolveConfiguredRuleSelection(
				config, "@shader-key:n", ShaderRendererBackend::LIBPLACEBO,
				selection, activeSections, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::AreEqual(static_cast<size_t>(1), selection.size());
			Assert::IsTrue(selection.front().none,
				L"The empty shader root must explicitly turn Alpha NLS off");
			Assert::AreEqual(static_cast<size_t>(1), activeSections.size());
			Assert::AreEqual("shader.nls", activeSections.front().c_str());

			selection.clear();
			activeSections.clear();
			error.clear();
			Assert::IsTrue(MadVRShaderLoader::ResolveConfiguredRuleSelection(
				config, "@shader-key:Shift+N", ShaderRendererBackend::LIBPLACEBO,
				selection, activeSections, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::AreEqual(static_cast<size_t>(1), selection.size());
			Assert::IsTrue(selection.front().nls,
				L"A shader member shortcut must select its NLS effect");
			Assert::AreEqual(static_cast<size_t>(1), activeSections.size());
			Assert::AreEqual("shader.nls.standard",
				activeSections.front().c_str());
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(Vp0099MaximumStretchRatioIsValidatedAndPublished)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0099-max-stretch.cfg";

			auto resolve = [&](const char* configured,
				double expected, bool valid)
			{
				{
					std::ofstream file(path, std::ios::out | std::ios::trunc);
					file << "[shader.nls]\n"
						"when: $key==\"n\"\n"
						"[shader.nls.standard]\n"
						"when: $key==\"Shift+n\"\n"
						"shader_type: nls\n"
						"glsl_file: NLS.glsl\n"
						"hlsl_file: NLS.hlsl\n";
					if (configured)
						file << "max_stretch_ratio: " << configured << "\n";
				}
				ConfigFile config;
				Assert::IsTrue(config.Load(path));
				std::vector<ConfiguredShaderRule> selection;
				std::string error;
				const bool resolved =
					MadVRShaderLoader::ResolveConfiguredRuleSelection(
						config, "@shader-key:Shift+n", ShaderRendererBackend::LIBPLACEBO,
						selection, error);
				Assert::AreEqual(valid, resolved);
				if (valid)
				{
					Assert::AreEqual(static_cast<size_t>(1), selection.size());
					Assert::AreEqual(expected,
						selection.front().maximumStretchRatio, 0.000001);
					std::vector<ConfiguredShaderRule> madvrSelection;
					Assert::IsTrue(
						MadVRShaderLoader::ResolveConfiguredRuleSelection(
							config, "@shader-key:Shift+n", ShaderRendererBackend::MADVR,
							madvrSelection, error));
					Assert::AreEqual(static_cast<size_t>(1),
						madvrSelection.size());
					Assert::AreEqual(
						selection.front().maximumStretchRatio,
						madvrSelection.front().maximumStretchRatio, 0.000001);
				}
			};

			resolve(nullptr, NLS_DEFAULT_MAXIMUM_STRETCH_RATIO, true);
			resolve("1.0", 1.0, true);
			resolve("1.35", 1.35, true);
			resolve("1.5", 1.5, true);
			resolve("0.99", 0.0, false);
			resolve("1.5001", 0.0, false);
			resolve("not-a-number", 0.0, false);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(Vp0099TypedNlsDefaultsToExpansionOnly)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0099-expansion-only.cfg";

			auto resolve = [&](const char* direction,
				NlsAspectDirection expectedDirection)
			{
				{
					std::ofstream file(path, std::ios::out | std::ios::trunc);
					file << "[shader.nls]\n"
						"when: $key==\"n\"\n"
						"[shader.nls.standard]\n"
						"when: $key==\"Shift+n\"\n"
						"shader_type: nls\n"
						"glsl_file: NLS.glsl\n"
						"hlsl_file: NLS.hlsl\n";
					if (direction)
						file << "aspect_direction: " << direction << "\n";
				}
				ConfigFile config;
				Assert::IsTrue(config.Load(path));
				for (const ShaderRendererBackend backend : {
					ShaderRendererBackend::LIBPLACEBO,
					ShaderRendererBackend::MADVR })
				{
					std::vector<ConfiguredShaderRule> selection;
					std::string error;
					Assert::IsTrue(
						MadVRShaderLoader::ResolveConfiguredRuleSelection(
							config, "@shader-key:Shift+n", backend,
							selection, error));
					Assert::AreEqual(static_cast<size_t>(1), selection.size());
					Assert::AreEqual(static_cast<int>(expectedDirection),
						static_cast<int>(selection.front().aspectDirection));
				}
			};

			resolve(nullptr, NlsAspectDirection::NARROWER_ONLY);
			resolve("narrower_only", NlsAspectDirection::NARROWER_ONLY);
			resolve("wider_only", NlsAspectDirection::WIDER_ONLY);
			resolve("any", NlsAspectDirection::ANY);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(Vp0089AndVp0131TypedAdvancedNlsSettingsResolveForAlpha)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0089-vp0131-advanced-nls.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[shader.nls]\nshortcut: n\n"
					"[shader.nls.plus]\nshortcut: b\n"
					"shader_type: nls\n"
					"glsl_file: NLSPlus.glsl\n"
					"axis_balance: 0.25\n"
					"max_center_zoom: 1.08\n"
					"horizontal_center_protection: 0.35\n"
					"vertical_center_protection: 0.25\n"
					"aspect_direction: wider_only\n"
					"vprenderer_max_crop_percent: 6\n"
					"vprenderer_crop_preference: minimize_distortion\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(ShaderConfigValidation::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());
			std::vector<ConfiguredShaderRule> selection;
			Assert::IsTrue(MadVRShaderLoader::ResolveConfiguredRuleSelection(
				config, "@shader-key:b", ShaderRendererBackend::LIBPLACEBO,
				selection, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::AreEqual(static_cast<size_t>(1), selection.size());
			Assert::AreEqual("NLSPlus.glsl",
				selection.front().filename.c_str());
			Assert::AreEqual(static_cast<int>(NlsAspectDirection::WIDER_ONLY),
				static_cast<int>(selection.front().aspectDirection));
			Assert::AreEqual(6.0,
				selection.front().vpRendererMaximumCropPercent, 0.000001);
			Assert::AreEqual(static_cast<int>(
				NlsPresentationCropPreference::MINIMIZE_DISTORTION),
				static_cast<int>(selection.front().vpRendererCropPreference));
			Assert::AreEqual(0.25, std::stod(selection.front().parameters.at(
				"axis_balance")), 0.000001);
			Assert::AreEqual(1.08, std::stod(selection.front().parameters.at(
				"max_center_zoom")), 0.000001);
			Assert::AreEqual(0.35, std::stod(selection.front().parameters.at(
				"horizontal_center_protection")), 0.000001);
			Assert::AreEqual(0.25, std::stod(selection.front().parameters.at(
				"vertical_center_protection")), 0.000001);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(Vp0089NlsPlusNewParametersHaveBackwardCompatibleDefaults)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0089-nls-plus-upgrade.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[shader.nls]\nshortcut: n\n"
					"[shader.nls.plus]\nshortcut: b\n"
					"shader_type: nls\n"
					"glsl_file: NLSPlus.glsl\n"
					"axis_balance: 0.5\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			std::vector<ConfiguredShaderRule> selection;
			Assert::IsTrue(MadVRShaderLoader::ResolveConfiguredRuleSelection(
				config, "@shader-key:b", ShaderRendererBackend::LIBPLACEBO,
				selection, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::AreEqual(static_cast<size_t>(1), selection.size());
			Assert::AreEqual(1.08, std::stod(selection.front().parameters.at(
				"max_center_zoom")), 0.000001);
			Assert::AreEqual(0.35, std::stod(selection.front().parameters.at(
				"horizontal_center_protection")), 0.000001);
			Assert::AreEqual(0.25, std::stod(selection.front().parameters.at(
				"vertical_center_protection")), 0.000001);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(Vp0089AndVp0131InvalidAdvancedNlsSettingsAreRejected)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0089-vp0131-invalid-nls.cfg";
			auto rejects = [&](const char* setting)
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[shader.nls]\nshortcut: n\n"
					"[shader.nls.plus]\nshortcut: b\n"
					"shader_type: nls\n"
					"glsl_file: NLSPlus.glsl\n" << setting << "\n";
				file.close();
				ConfigFile config;
				Assert::IsTrue(config.Load(path));
				std::string error;
				Assert::IsFalse(ShaderConfigValidation::Validate(config, error));
			};
			rejects("axis_balance: 1.01");
			rejects("max_center_zoom: 1.251");
			rejects("horizontal_center_protection: 0.451");
			rejects("vertical_center_protection: -0.001");
			rejects("aspect_direction: sideways");
			rejects("vprenderer_max_crop_percent: 10.01");
			rejects("vprenderer_crop_preference: automatic");
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

		TEST_METHOD(DirectShowSchemaOwnsAllRendererOverridesAndRejectsStartupKeys)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-directshow-schema.cfg";
			ConfigFile config;
			std::string error;

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[directshow]\n"
					"frame_offset: AUTO\n"
					"renderer_transfer_matrix: BT2020_10\n"
					"renderer_primaries: BT2020\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n"
					"capture_input: HDMI\n"
					"video_conversion: V210_TO_P010\n"
					"container_colorspace: BT2020\n"
					"hdr_colorspace: FOLLOW_INPUT_LLDV\n"
					"hdr_luminance: FOLLOW_INPUT_LLDV\n"
					"[vprenderer]\nquality: high\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());

			// General is the shared default and DirectShow is now an explicit
			// override, so both fields are valid together.
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nvideo_conversion: V210_TO_P010\n"
					"container_colorspace: BT2020\n"
					"[directshow]\nvideo_conversion: NONE\n"
					"container_colorspace: REC709\n"
					"[vprenderer]\nquality: high\n"
					"video_conversion: V210_TO_P010\n"
					"hdr_colorspace: FOLLOW_INPUT_LLDV\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());
			RendererProfileConfig::Model rendererModel;
			Assert::IsTrue(RendererProfileConfig::Read(config, rendererModel, error),
				std::wstring(error.begin(), error.end()).c_str());

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[directshow]\ncapture_device: not-valid-here\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("capture_device") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(QueueRecoveryUsesDefaultQueueAndRejectsLegacyDuplicates)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-queue-recovery-migration.cfg";
			ConfigFile config;
			std::string error;
			std::string value;
			std::string section;

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[queue.normal]\n"
					"queue_size: 32\n"
					"reset_after_render_restart_seconds: 7\n"
					"[queue.low_latency]\n"
					"queue_size: 1\n"
					"[queue_recovery]\n"
					"reset_queue_too_large_percent: 70\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());
			RendererProfileConfig::Model model;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			const auto queue = std::find_if(model.groups.begin(), model.groups.end(),
				[](const RendererProfileConfig::Group& group)
				{ return group.name == "queue"; });
			Assert::IsTrue(queue != model.groups.end());
			Assert::AreEqual("normal", queue->defaultSelection.c_str());
			Assert::IsTrue(QueueConfiguration::TryGetRecoveryString(config,
				"reset_after_render_restart_seconds", value, &section));
			Assert::AreEqual("7", value.c_str());
			Assert::AreEqual("queue.normal", section.c_str());
			Assert::IsTrue(QueueConfiguration::TryGetRecoveryString(config,
				"reset_queue_too_large_percent", value, &section));
			Assert::AreEqual("70", value.c_str());
			Assert::AreEqual("queue_recovery", section.c_str());

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[queue.named]\nqueue_size: 1\n"
					"[queue]\n"
					"reset_after_render_restart_seconds: 4\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsTrue(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(QueueConfiguration::TryGetRecoveryString(config,
				"reset_after_render_restart_seconds", value, &section));
			Assert::AreEqual("4", value.c_str());
			Assert::AreEqual("queue", section.c_str());

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[queue]\n"
					"reset_after_render_restart_seconds: 4\n"
					"[queue_recovery]\n"
					"reset_after_render_restart_seconds: 8\n";
			}
			Assert::IsTrue(config.Load(path));
			error.clear();
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("queue_recovery") != std::string::npos);
			Assert::IsTrue(error.find("reset_after_render_restart_seconds") !=
				std::string::npos);

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[queue]\n"
					"reset_after_render_restart_seconds: 4\n"
					"[queue.low_latency]\n"
					"lead_frames: 0\n"
					"startup_preroll_frames: 1\n"
					"target_frames: 1\n"
					"active_picture_lookahead_frames: 2\n"
					"reset_after_render_restart_seconds: 2\n"
					"reset_queue_too_large_percent: 200\n";
			}
			Assert::IsTrue(config.Load(path));
			error.clear();
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			RendererProfileConfig::ResolvedQueue resolved;
			Assert::IsTrue(RendererProfileConfig::ResolveQueue(
				model, "low_latency", resolved, error));
			Assert::AreEqual(size_t{ 0 }, resolved.leadFrames);
			Assert::AreEqual(size_t{ 1 }, resolved.startupPrerollFrames);
			Assert::AreEqual(size_t{ 1 }, resolved.targetFrames);
			Assert::AreEqual(size_t{ 2 }, resolved.activePictureLookaheadFrames);
			Assert::AreEqual(2, resolved.resetAfterRendererRestartSeconds);
			Assert::AreEqual(200, resolved.resetQueueTooLargePercent);

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[queue]\nreset_queue_too_large_percent: 201\n";
			}
			Assert::IsTrue(config.Load(path));
			error.clear();
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("1 to 200") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(LldvProfilesUseOrderedSelectionAndInheritBaselineMetadata)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-lldv-profiles.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[lldv.cinema]\n"
					"max_cll: 1000\n"
					"mastering_min_luminance: 0.001\n"
					"mastering_max_luminance: 4000\n"
					"[lldv.hdr]\n"
					"when: ${eotf}==\"HDR\"\n"
					"max_fall: 401\n"
					"[lldv.pq]\n"
					"when: ${transfer}==\"PQ\"\n"
					"shortcut: Shift+l\n"
					"max_fall: 500\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error),
				std::wstring(error.begin(), error.end()).c_str());
			RendererProfileConfig::Model model;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error),
				std::wstring(error.begin(), error.end()).c_str());
			const auto lldvGroup = std::find_if(model.groups.begin(),
				model.groups.end(), [](const RendererProfileConfig::Group& group)
				{ return group.name == "lldv"; });
			Assert::IsTrue(lldvGroup != model.groups.end());
			Assert::AreEqual("cinema", lldvGroup->defaultSelection.c_str());
			Assert::AreEqual(static_cast<size_t>(3), lldvGroup->profiles.size());
			Assert::AreEqual("hdr", lldvGroup->profiles[1].c_str());
			std::vector<std::string> chords;
			Assert::IsTrue(RendererProfileConfig::CollectKeyChords(model,
				chords, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(std::find(chords.begin(), chords.end(), "Shift+L") !=
				chords.end());

			UnifiedProfileRuntime::Runtime runtime;
			const auto hdrAndPq = [](const std::string& variable,
				std::string& value)
			{
				if (variable == "eotf") { value = "HDR"; return true; }
				if (variable == "transfer") { value = "PQ"; return true; }
				return false;
			};
			Assert::IsTrue(runtime.Initialize(config, hdrAndPq, error),
				std::wstring(error.begin(), error.end()).c_str());
			const auto initial = runtime.GetSnapshot();
			Assert::IsTrue(initial != nullptr);
			Assert::AreEqual("hdr", initial->lldv.profile.c_str());
			Assert::IsTrue(initial->lldv.hasMaxCll);
			Assert::IsTrue(std::abs(initial->lldv.maxCll - 1000.0) < 1e-12);
			Assert::IsTrue(initial->lldv.hasMaxFall);
			Assert::IsTrue(std::abs(initial->lldv.maxFall - 401.0) < 1e-12);
			Assert::IsTrue(std::abs(initial->lldv.masteringMaxLuminance -
				4000.0) < 1e-12);

			UnifiedProfileRuntime::SelectionResult selection;
			Assert::IsTrue(runtime.SelectKey("Shift+l", hdrAndPq,
				selection, error),
				std::wstring(error.begin(), error.end()).c_str());
			Assert::IsTrue(selection.changed);
			Assert::AreEqual("pq", selection.snapshot->lldv.profile.c_str());
			Assert::IsTrue(std::abs(selection.snapshot->lldv.maxCll -
				1000.0) < 1e-12);
			Assert::IsTrue(std::abs(selection.snapshot->lldv.maxFall -
				500.0) < 1e-12);
			Assert::IsTrue(std::abs(selection.snapshot->lldv.
				masteringMinLuminance - 0.001) < 1e-12);
			Assert::AreEqual("pq",
				selection.snapshot->effectiveSelections.at("lldv").c_str());

			const RendererProfileConfig::LldvMetadata legacyDefaults =
				RendererProfileConfig::DefaultLldvMetadata(false);
			const RendererProfileConfig::LldvMetadata newDefaults =
				RendererProfileConfig::DefaultLldvMetadata(true);
			Assert::IsTrue(std::abs(legacyDefaults.maxFall - 1000.0) < 1e-12);
			Assert::IsTrue(std::abs(legacyDefaults.masteringMinLuminance -
				0.0001) < 1e-12);
			Assert::IsTrue(std::abs(newDefaults.maxFall - 401.0) < 1e-12);
			Assert::IsTrue(std::abs(newDefaults.masteringMaxLuminance -
				4000.0) < 1e-12);

			DeleteFileA(path.c_str());
		}

		TEST_METHOD(LegacyLldvRootRemainsTheBaselineProfile)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-lldv-root.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[lldv]\nmax_cll: 900\n"
					"mastering_max_luminance: 3000\n";
			}
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			std::string error;
			Assert::IsTrue(MainConfigSchema::Validate(config, error));
			UnifiedProfileRuntime::Runtime runtime;
			Assert::IsTrue(runtime.Initialize(config,
				[](const std::string&, std::string&) { return false; }, error),
				std::wstring(error.begin(), error.end()).c_str());
			const auto snapshot = runtime.GetSnapshot();
			Assert::AreEqual("base", snapshot->lldv.profile.c_str());
			Assert::IsTrue(std::abs(snapshot->lldv.maxCll - 900.0) < 1e-12);
			Assert::IsTrue(std::abs(snapshot->lldv.masteringMaxLuminance -
				3000.0) < 1e-12);
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

		TEST_METHOD(MainConfigSchemaValidatesForegroundOnlyShortcutPolicy)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-shortcut-focus-schema.cfg";
			ConfigFile config;
			std::string error;

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[shortcuts]\nforeground_only: true\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsTrue(MainConfigSchema::Validate(config, error));

			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[shortcuts]\nforeground_only: sometimes\n";
			}
			Assert::IsTrue(config.Load(path));
			Assert::IsFalse(MainConfigSchema::Validate(config, error));
			Assert::IsTrue(error.find("foreground_only") != std::string::npos);
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
					"profile_update_mode: never\n"
					"live_profile_updates: true\n"
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
			std::string profileUpdateMode;
			bool liveProfileUpdates = false;
			bool switchRefreshRate = true;
			Assert::IsTrue(view.TryGetDisplayString("quality", quality));
			Assert::AreEqual("high", quality.c_str());
			Assert::IsTrue(view.TryGetPolicyString(
				"profile_update_mode", profileUpdateMode));
			Assert::AreEqual("never", profileUpdateMode.c_str());
			Assert::IsTrue(view.TryGetPolicyBool(
				"live_profile_updates", liveProfileUpdates));
			Assert::IsTrue(liveProfileUpdates);
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
