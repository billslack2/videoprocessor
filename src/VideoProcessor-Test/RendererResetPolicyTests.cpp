#include "pch.h"
#include "CppUnitTest.h"

#include <BoundedDeliveryGate.h>
#include <MadVRShaderTransactionPolicy.h>
#include <RendererResetPolicy.h>
#include <SubtitleProfileUpdatePolicy.h>
#include <sstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    struct SubtitleUpdateSettingsFixture
    {
        int subtitleCutPasteBackground = 0;
        int subtitleGeneratedGray = 0;
        int subtitleBoxPadding = 0;
        int subtitlePaddingPolicy = 0;
        int subtitleMode = 0;
        int subtitleMoveInset = 0;
        int subtitleTextReductionPercent = 0;
        int subtitleRoundedCorners = 0;
        int subtitleFloatBackground = 0;
        int subtitleBackgroundGuard = 0;
        int subtitleTemporalFill = 0;
        int subtitleSpatialFill = 0;
        int scopeSubtitleFit = 0;
        int configuredScreenAspect = 0;
        int configuredScreenTarget = 0;
        int verticalAlignment = 0;
        int screenEdgePadding = 0;
        int anamorphicScale = 0;
        int automaticSourceCropRequested = 0;
        int automaticSourceCrop = 0;
        int cropNarrowerContentToFillScreen = 0;
        int cropNarrowerContentAspectLimitConfigured = 0;
        int cropNarrowerContentAspectLimit = 0;
        int cropWiderContentToFillScreen = 0;
        int cropWiderContentAspectLimitConfigured = 0;
        int cropWiderContentAspectLimit = 0;
        int fixedCropAspectConfigured = 0;
        int fixedCropAspect = 0;
        int subtitleDetectionOptimization = 0;
        int subtitleScriptHint = 0;
        int subtitleNearBarDistance = 0;
        int subtitleHoldMs = 0;
        int scopeSubtitleHoldMs = 0;
        int scopeSubtitleEngageDriftMs = 0;
        int scopeSubtitleReleaseDriftMs = 0;
        int scopeSubtitlePaddingPixels = 0;
        int scopeSubtitleTargetBufferPixels = 0;
        int hdrPeakAnalysisPictureOnly = 0;
        int hdrPeakAnalysisMotionCompensation = 0;
        int hdrPeakAnalysisHeightPercent = 0;
        int hdrPeakAnalysisPosition = 0;
        int detector = 0, viewport = 0, format = 0, futureSetting = 0;
        static std::string Fingerprint(const SubtitleUpdateSettingsFixture& s)
        {
            std::ostringstream out;
            out << s.subtitleCutPasteBackground << '|';
            out << s.subtitleGeneratedGray << '|';
            out << s.subtitleBoxPadding << '|';
            out << s.subtitlePaddingPolicy << '|';
            out << s.subtitleMode << '|';
            out << s.subtitleMoveInset << '|';
            out << s.subtitleTextReductionPercent << '|';
            out << s.subtitleRoundedCorners << '|';
            out << s.subtitleFloatBackground << '|';
            out << s.subtitleBackgroundGuard << '|';
            out << s.subtitleTemporalFill << '|';
            out << s.subtitleSpatialFill << '|';
            out << s.scopeSubtitleFit << '|';
            out << s.configuredScreenAspect << '|';
            out << s.configuredScreenTarget << '|';
            out << s.verticalAlignment << '|';
            out << s.screenEdgePadding << '|';
            out << s.anamorphicScale << '|';
            out << s.automaticSourceCropRequested << '|';
            out << s.automaticSourceCrop << '|';
            out << s.cropNarrowerContentToFillScreen << '|';
            out << s.cropNarrowerContentAspectLimitConfigured << '|';
            out << s.cropNarrowerContentAspectLimit << '|';
            out << s.cropWiderContentToFillScreen << '|';
            out << s.cropWiderContentAspectLimitConfigured << '|';
            out << s.cropWiderContentAspectLimit << '|';
            out << s.fixedCropAspectConfigured << '|';
            out << s.fixedCropAspect << '|';
            out << s.subtitleDetectionOptimization << '|';
            out << s.subtitleScriptHint << '|';
            out << s.subtitleNearBarDistance << '|';
            out << s.subtitleHoldMs << '|';
            out << s.scopeSubtitleHoldMs << '|';
            out << s.scopeSubtitleEngageDriftMs << '|';
            out << s.scopeSubtitleReleaseDriftMs << '|';
            out << s.scopeSubtitlePaddingPixels << '|';
            out << s.scopeSubtitleTargetBufferPixels << '|';
            out << s.hdrPeakAnalysisPictureOnly << '|';
            out << s.hdrPeakAnalysisMotionCompensation << '|';
            out << s.hdrPeakAnalysisHeightPercent << '|';
            out << s.hdrPeakAnalysisPosition << '|';
            out << s.detector << '|' << s.viewport << '|' << s.format << '|' << s.futureSetting;
            return out.str();
        }
    };

	TEST_CLASS(RendererResetPolicyTests)
	{
	public:
        TEST_METHOD(SubtitlePresentationFieldsPreservePictureContractOnly)
        {
            using S = SubtitleUpdateSettingsFixture;
            const S before;
            int S::* fields[] = {&S::subtitleCutPasteBackground,&S::subtitleGeneratedGray,&S::subtitleBoxPadding,&S::subtitlePaddingPolicy,&S::subtitleMode,&S::subtitleMoveInset,&S::subtitleTextReductionPercent,&S::subtitleRoundedCorners,&S::subtitleFloatBackground,&S::subtitleBackgroundGuard,&S::subtitleTemporalFill,&S::subtitleSpatialFill,&S::scopeSubtitleFit,&S::scopeSubtitleHoldMs,&S::scopeSubtitleEngageDriftMs,&S::scopeSubtitleReleaseDriftMs,&S::scopeSubtitlePaddingPixels,&S::scopeSubtitleTargetBufferPixels,&S::hdrPeakAnalysisPictureOnly,&S::hdrPeakAnalysisMotionCompensation,&S::hdrPeakAnalysisHeightPercent,&S::hdrPeakAnalysisPosition};
            for (const auto field : fields)
            {
                auto after = before;
                after.*field = 1;
                Assert::IsTrue(IsSubtitlePresentationOnlyUpdate(before, after, S::Fingerprint));
                after.viewport = 1;
                Assert::IsFalse(IsSubtitlePresentationOnlyUpdate(before, after, S::Fingerprint));
            }
        }
        TEST_METHOD(SubtitleUpdateRejectsDetectorFormatUnknownAndNoChange)
        {
            using S = SubtitleUpdateSettingsFixture;
            const S before;
            Assert::IsFalse(IsSubtitlePresentationOnlyUpdate(before, before, S::Fingerprint));
            for (int which = 0; which < 4; ++which)
            {
                auto after = before;
                after.subtitleMode = 2;
                if (which == 0) after.detector = 1;
                if (which == 1) after.viewport = 1;
                if (which == 2) after.format = 1;
                if (which == 3) after.futureSetting = 1;
                Assert::IsFalse(IsSubtitlePresentationOnlyUpdate(before, after, S::Fingerprint));
            }
        }
        TEST_METHOD(SubtitleOffClassicBlackAndReconstructionTransitionsArePresentationOnly)
        {
            using S = SubtitleUpdateSettingsFixture;
            for (int from = 0; from < 4; ++from)
                for (int to = 0; to < 4; ++to)
                {
                    S before, after;
                    before.subtitleMode = from; before.scopeSubtitleFit = from != 0;
                    after.subtitleMode = to; after.scopeSubtitleFit = to != 0;
                    after.subtitleGeneratedGray = to == 3;
                    before.subtitleGeneratedGray = from == 3;
                    before.scopeSubtitleEngageDriftMs = from ? 500 : 0;
                    after.scopeSubtitleEngageDriftMs = to ? 500 : 0;
                    before.scopeSubtitleReleaseDriftMs = from ? 2000 : 0;
                    after.scopeSubtitleReleaseDriftMs = to ? 2000 : 0;
                    before.scopeSubtitlePaddingPixels = from > 1 ? 20 : 0;
                    after.scopeSubtitlePaddingPixels = to > 1 ? 20 : 0;
                    before.hdrPeakAnalysisMotionCompensation = from == 2;
                    after.hdrPeakAnalysisMotionCompensation = to == 2;
                    Assert::AreEqual(from != to, IsSubtitlePresentationOnlyUpdate(before, after, S::Fingerprint));
                }
        }
        TEST_METHOD(SubtitleUpdateRequiresCommittedViewportEvenAfterMailboxConsumption)
        {
            Assert::IsFalse(SubtitleUpdateHasCommittedContext(true, 3, 4));
            Assert::IsFalse(SubtitleUpdateHasCommittedContext(false, 3, 4));
            Assert::IsTrue(SubtitleUpdateHasCommittedContext(false, 4, 4));
            Assert::IsFalse(SubtitleUpdateHasCommittedContext(true, 4, 4));
        }
        TEST_METHOD(SubtitleContextGuardRejectsFieldsEvenWhenFingerprintOmitsThem)
        {
            using S = SubtitleUpdateSettingsFixture;
            const S before;
            int S::* fields[] = {&S::configuredScreenAspect,&S::configuredScreenTarget,&S::verticalAlignment,&S::screenEdgePadding,&S::anamorphicScale,&S::automaticSourceCropRequested,&S::automaticSourceCrop,&S::cropNarrowerContentToFillScreen,&S::cropNarrowerContentAspectLimitConfigured,&S::cropNarrowerContentAspectLimit,&S::cropWiderContentToFillScreen,&S::cropWiderContentAspectLimitConfigured,&S::cropWiderContentAspectLimit,&S::fixedCropAspectConfigured,&S::fixedCropAspect,&S::subtitleDetectionOptimization,&S::subtitleScriptHint,&S::subtitleNearBarDistance,&S::subtitleHoldMs};
            for (const auto field : fields)
            {
                auto after = before;
                after.subtitleMode = 1;
                after.*field = 1;
                Assert::IsFalse(IsSubtitlePresentationOnlyUpdate(before, after,
                    [](const S& s) { return std::to_string(s.subtitleMode); }));
            }
        }
		TEST_METHOD(DisplayTransitionCannotDelayPostStartReset)
		{
			const int postStartPriority =
				RendererResetPriority(
					RendererResetReason::PostRendererStart);
			const int displayPriority =
				RendererResetPriority(
					RendererResetReason::DisplayTransition);
			Assert::IsFalse(RendererResetShouldReplace(
				displayPriority, 33000,
				postStartPriority, 5000));
		}

		TEST_METHOD(RefreshTransitionReplacesDelayedDisplayFallback)
		{
			const int refreshPriority = RendererResetPriority(
				RendererResetReason::RefreshTransition);
			const int displayPriority = RendererResetPriority(
				RendererResetReason::DisplayTransition);
			Assert::IsTrue(RendererResetShouldReplace(
				refreshPriority, 5000,
				displayPriority, 35000));
		}

		TEST_METHOD(HostTransitionUsesTheSameQueueOnlyPriority)
		{
			Assert::AreEqual(
				RendererResetPriority(RendererResetReason::RefreshTransition),
				RendererResetPriority(RendererResetReason::HostTransition));
			Assert::IsTrue(RendererResetShouldReplace(
				RendererResetPriority(RendererResetReason::HostTransition), 5000,
				RendererResetPriority(RendererResetReason::DisplayTransition),
				35000));
		}

		TEST_METHOD(CriticalRecoveryPreemptsDisplaySettle)
		{
			Assert::IsTrue(RendererResetShouldReplace(
				RendererResetPriority(
					RendererResetReason::LivenessRecovery),
				0,
				RendererResetPriority(
					RendererResetReason::DisplayTransition),
				33000));
		}

		TEST_METHOD(EqualPriorityUsesEarliestDeadline)
		{
			const int priority =
				RendererResetPriority(
					RendererResetReason::DisplayTransition);
			Assert::IsTrue(RendererResetShouldReplace(
				priority, 5000, priority, 33000));
			Assert::IsFalse(RendererResetShouldReplace(
				priority, 33000, priority, 5000));
		}

		TEST_METHOD(QueuePolicyApplyUsesBackendOwnedSafeResetScope)
		{
			Assert::IsTrue(QueuePolicyApplyRequiresGraphReset(true));
			Assert::IsFalse(QueuePolicyApplyRequiresGraphReset(false));
		}

		TEST_METHOD(AlphaQueueCapacityOscillationDoesNotRequestRecovery)
		{
			constexpr std::size_t capacity = 4;
			constexpr std::size_t resetDelaySeconds = 5;
			for (std::size_t elapsedSeconds = 0;
				elapsedSeconds <= resetDelaySeconds;
				++elapsedSeconds)
			{
				const std::size_t rawQueueSize =
					elapsedSeconds % 2 == 0 ? capacity : capacity - 1;
				Assert::IsFalse(AlphaQueueOverflowRequiresRecovery(
					true, rawQueueSize, 0, capacity));
			}
		}

		TEST_METHOD(AlphaQueueRecoveryRequiresActualOverflow)
		{
			constexpr std::size_t capacity = 4;
			Assert::IsFalse(AlphaQueueOverflowRequiresRecovery(
				true, capacity, 0, capacity));
			Assert::IsFalse(AlphaQueueOverflowRequiresRecovery(
				true, 0, capacity, capacity));
			Assert::IsTrue(AlphaQueueOverflowRequiresRecovery(
				true, capacity + 1, 0, capacity));
			Assert::IsTrue(AlphaQueueOverflowRequiresRecovery(
				true, 0, capacity + 1, capacity));
		}

		TEST_METHOD(NonAlphaQueueDoesNotUseAlphaOverflowRecovery)
		{
			Assert::IsFalse(AlphaQueueOverflowRequiresRecovery(
				false, 8, 0, 4));
			Assert::IsFalse(AlphaQueueOverflowRequiresRecovery(
				false, 0, 8, 4));
		}

		TEST_METHOD(BlockedDeliveryCannotStarveGraphOwnerControl)
		{
			std::timed_mutex gate;
			std::atomic_bool gateHeld{false};
			std::thread delivery([&]()
				{
					std::lock_guard<std::timed_mutex> lock(gate);
					gateHeld.store(true, std::memory_order_release);
					std::this_thread::sleep_for(
						std::chrono::milliseconds(50));
				});
			while (!gateHeld.load(std::memory_order_acquire))
				std::this_thread::yield();

			bool executed = false;
			const auto started = std::chrono::steady_clock::now();
			const bool blockedResult = TryRunBoundedDeliveryTransaction(
				gate, std::chrono::milliseconds(5),
				[&]() { executed = true; });
			const auto elapsed = std::chrono::duration_cast<
				std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - started);
			delivery.join();

			Assert::IsFalse(blockedResult);
			Assert::IsFalse(executed);
			Assert::IsTrue(elapsed < std::chrono::milliseconds(40));

			Assert::IsTrue(TryRunBoundedDeliveryTransaction(
				gate, std::chrono::milliseconds(5),
				[&]() { executed = true; }));
			Assert::IsTrue(executed);
		}

		TEST_METHOD(DeferredShaderIntentCannotBecomeAFalseDuplicate)
		{
			Assert::IsTrue(ShouldCoalesceMadVRShaderRequest(true, false));
			Assert::IsFalse(ShouldCoalesceMadVRShaderRequest(true, true));
			Assert::IsFalse(ShouldCoalesceMadVRShaderRequest(false, false));
		}

		TEST_METHOD(NlsGeometryVariantsShareStructuralPreflight)
		{
			std::map<std::string, std::string> first = {
				{ "active_left", "0.00000000" },
				{ "active_top", "0.03148000" },
				{ "active_right", "1.00000000" },
				{ "active_bottom", "0.96852000" },
				{ "stretch_ratio", "1.23865000" },
				{ "warp_axis", "0" },
				{ "safe_fit", "0" },
			};
			auto second = first;
			second["active_top"] = "0.00000000";
			second["active_bottom"] = "1.00000000";
			second["stretch_ratio"] = "1.32188000";

			NormalizeMadVRNlsRuntimeParametersForPreflight(first);
			NormalizeMadVRNlsRuntimeParametersForPreflight(second);
			Assert::IsTrue(first == second);

			second["warp_axis"] = "1";
			Assert::IsFalse(first == second);
		}

		TEST_METHOD(DynamicVideoAspectIsGeometryOnly)
		{
			Assert::AreEqual(
				static_cast<int>(DirectShowGraphEventImpact::GeometryOnly),
				static_cast<int>(ClassifyDirectShowGraphEvent(0x0e)));
			Assert::AreEqual(
				static_cast<int>(DirectShowGraphEventImpact::DisplayTransition),
				static_cast<int>(ClassifyDirectShowGraphEvent(0x16)));
			Assert::AreEqual(
				static_cast<int>(DirectShowGraphEventImpact::None),
				static_cast<int>(ClassifyDirectShowGraphEvent(0x12)));
		}

		TEST_METHOD(RapidRendererSelectionDefersAcrossLifecycleBoundaries)
		{
			Assert::AreEqual(
				static_cast<int>(
					RendererRestartDispatch::DeferUntilConstructionCompletes),
				static_cast<int>(ClassifyRendererRestartDispatch(true, false)));
			Assert::AreEqual(
				static_cast<int>(
					RendererRestartDispatch::DeferUntilConstructionCompletes),
				static_cast<int>(ClassifyRendererRestartDispatch(true, true)));
			Assert::AreEqual(
				static_cast<int>(
					RendererRestartDispatch::DeferUntilRetirementCompletes),
				static_cast<int>(ClassifyRendererRestartDispatch(false, true)));
			Assert::AreEqual(
				static_cast<int>(RendererRestartDispatch::DispatchNow),
				static_cast<int>(ClassifyRendererRestartDispatch(false, false)));
		}

		TEST_METHOD(DisplayTransitionOwnsConcurrentSourceGapRecovery)
		{
			Assert::IsTrue(
				RendererResetPriority(
					RendererResetReason::DisplayTransition) >
				RendererResetPriority(
					RendererResetReason::SourceGapRecovery));
		}

		TEST_METHOD(OutputReadinessPreemptsDisplayTransitionButNotCriticalRecovery)
		{
			const int outputReadinessPriority =
				RendererResetPriority(RendererResetReason::OutputReadiness);
			Assert::IsTrue(outputReadinessPriority >
				RendererResetPriority(RendererResetReason::DisplayTransition));
			Assert::IsTrue(outputReadinessPriority <
				RendererResetPriority(RendererResetReason::LivenessRecovery));
		}

		TEST_METHOD(ManualResetHasDeterministicTopPriority)
		{
			Assert::IsTrue(
				RendererResetPriority(RendererResetReason::Manual) >
				RendererResetPriority(
					RendererResetReason::LivenessRecovery));
		}

		TEST_METHOD(FullscreenRetargetConsumesTheFinalMatchingIntent)
		{
			// An entering retarget remains valid if repeated keyboard/API toggles
			// ultimately return to fullscreen before the first new frame arrives.
			Assert::IsFalse(FullscreenRetargetRequiresCoveredRebuild(
				false, true));
			Assert::IsTrue(FullscreenRetargetRequiresCoveredRebuild(
				false, false));
		}

		TEST_METHOD(FullscreenRetargetDefersOnlyAnOppositeFinalIntent)
		{
			// The symmetric exit case must keep its active target unless the final
			// requested state is fullscreen again.
			Assert::IsFalse(FullscreenRetargetRequiresCoveredRebuild(
				true, false));
			Assert::IsTrue(FullscreenRetargetRequiresCoveredRebuild(
				true, true));
		}

		TEST_METHOD(DirectShowToAlphaBackendHandoffIsDetected)
		{
			Assert::IsTrue(IsDirectShowToAlphaBackendHandoff(true, false));
			Assert::IsFalse(IsDirectShowToAlphaBackendHandoff(false, false));
			Assert::IsFalse(IsDirectShowToAlphaBackendHandoff(true, true));
		}

		TEST_METHOD(FreshAlphaQueueReprimesForEveryDeterministicTransition)
		{
			Assert::IsFalse(AlphaFreshStartRequiresDelayedReprime(
				AlphaFreshStartTransition::None));
			Assert::IsTrue(AlphaFreshStartRequiresDelayedReprime(
				AlphaFreshStartTransition::BackendHandoff));
			Assert::IsTrue(AlphaFreshStartRequiresDelayedReprime(
				AlphaFreshStartTransition::HostTransition));
			Assert::IsTrue(AlphaFreshStartRequiresDelayedReprime(
				AlphaFreshStartTransition::RefreshTransition));
		}
	};
}
