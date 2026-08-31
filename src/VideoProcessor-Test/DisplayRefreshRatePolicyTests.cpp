#include "pch.h"
#include "CppUnitTest.h"

#include <DisplayRefreshRatePolicy.h>

#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(DisplayRefreshRatePolicyTests)
	{
	public:
		static DisplayRefreshRateInput StableInput(
			double candidateRateHz,
			double nominalRateHz)
		{
			DisplayRefreshRateInput input;
			input.candidateRateHz = candidateRateHz;
			input.rawWaitRateHz = candidateRateHz;
			input.nominalRateHz = nominalRateHz;
			input.minimumWaitIntervalMs = 1000.0 / nominalRateHz * 0.98;
			input.maximumWaitIntervalMs = 1000.0 / nominalRateHz * 2.05;
			input.compensatedIntervals = 1800;
			input.rawWaitIntervals = 1798;
			input.startupObservationSeconds = 2.0;
			input.readinessObservationSeconds = 10.0;
			input.fresh = true;
			input.stable = true;
			return input;
		}

		static int Decision(DisplayRefreshRateDecision value)
		{
			return static_cast<int>(value);
		}

		static int Reason(DisplayRefreshRateReason value)
		{
			return static_cast<int>(value);
		}

		TEST_METHOD(AcceptsClean23976WithoutSnappingPrecision)
		{
			const auto result =
				EvaluateDisplayRefreshRate(StableInput(23.976431, 23.976));

			Assert::AreEqual(Decision(DisplayRefreshRateDecision::Accepted),
				Decision(result.decision));
			Assert::AreEqual(23.976431, result.selectedRateHz, 0.0000001);
		}

		TEST_METHOD(AcceptsClean5994And60Families)
		{
			const auto fractional =
				EvaluateDisplayRefreshRate(StableInput(59.940539, 59.941));
			const auto integer =
				EvaluateDisplayRefreshRate(StableInput(60.000137, 60.0));

			Assert::AreEqual(Decision(DisplayRefreshRateDecision::Accepted),
				Decision(fractional.decision));
			Assert::AreEqual(Decision(DisplayRefreshRateDecision::Accepted),
				Decision(integer.decision));
		}

		TEST_METHOD(AcceptsExplainedSchedulerGapsWithoutSnapping)
		{
			auto input = StableInput(59.94091, 59.941);
			input.rawWaitRateHz = 57.942955;
			input.minimumWaitIntervalMs = 16.584;
			input.maximumWaitIntervalMs = 49.559;
			input.compensatedIntervals = 1275;
			input.rawWaitIntervals = 1232;

			const auto result = EvaluateDisplayRefreshRate(input);

			Assert::AreEqual(Decision(DisplayRefreshRateDecision::Accepted),
				Decision(result.decision));
			Assert::AreEqual(59.94091, result.selectedRateHz, 0.0000001);
		}

		TEST_METHOD(StartupWithoutSamplesWarmsInsteadOfSelectingZero)
		{
			DisplayRefreshRateInput input;
			input.nominalRateHz = 23.976;

			const auto result = EvaluateDisplayRefreshRate(input);

			Assert::AreEqual(Decision(DisplayRefreshRateDecision::Warming),
				Decision(result.decision));
			Assert::AreEqual(0.0, result.selectedRateHz);
		}

		TEST_METHOD(UnstableCandidateRemainsInWarmup)
		{
			auto input = StableInput(59.9405, 59.941);
			input.stable = false;

			const auto result = EvaluateDisplayRefreshRate(input);

			Assert::AreEqual(Decision(DisplayRefreshRateDecision::Warming),
				Decision(result.decision));
			Assert::AreEqual(Reason(DisplayRefreshRateReason::Stabilizing),
				Reason(result.reason));
			Assert::IsTrue(result.readinessValidated);
			Assert::IsTrue(result.startupValidated);
			Assert::AreEqual(59.9405, result.startupRateHz, 0.0000001);
			Assert::AreEqual(59.9405, result.readinessRateHz, 0.0000001);
		}

		TEST_METHOD(RejectedCandidateCannotBecomeAnOutputReadinessSignal)
		{
			auto input = StableInput(59.9405, 23.976);
			input.rawWaitRateHz = 59.9405;
			input.stable = false;

			const auto result = EvaluateDisplayRefreshRate(input);

			Assert::AreEqual(Decision(DisplayRefreshRateDecision::Quarantined),
				Decision(result.decision));
			Assert::IsFalse(result.readinessValidated);
			Assert::AreEqual(0.0, result.readinessRateHz);
		}

		TEST_METHOD(ShortObservationCannotBecomeAnOutputReadinessSignal)
		{
			auto input = StableInput(59.9405, 59.941);
			input.stable = false;
			input.readinessObservationSeconds = 9.999;

			const auto result = EvaluateDisplayRefreshRate(input);

			Assert::AreEqual(Decision(DisplayRefreshRateDecision::Warming),
				Decision(result.decision));
			Assert::IsFalse(result.readinessValidated);
			Assert::AreEqual(0.0, result.readinessRateHz);
		}

		TEST_METHOD(IncidentStyleDoubleRateIsQuarantinedAndRecalculated)
		{
			auto input = StableInput(47.947828, 23.976);
			input.rawWaitRateHz = 23.928594;
			input.minimumWaitIntervalMs = 23.374;
			input.maximumWaitIntervalMs = 81.932;
			input.compensatedIntervals = 567;
			input.rawWaitIntervals = 283;
			input.stable = false;

			const auto result = EvaluateDisplayRefreshRate(input);

			Assert::AreEqual(Decision(DisplayRefreshRateDecision::Quarantined),
				Decision(result.decision));
			Assert::AreEqual(Reason(DisplayRefreshRateReason::HarmonicMismatch),
				Reason(result.reason));
			Assert::IsTrue(result.shouldRecalculate);
			Assert::AreEqual(0.0, result.selectedRateHz);
		}

		TEST_METHOD(NumericalStabilityCannotBlessHarmonicCandidate)
		{
			auto input = StableInput(47.952291, 23.976);
			input.rawWaitRateHz = 23.971201;
			input.minimumWaitIntervalMs = 23.374;
			input.maximumWaitIntervalMs = 81.932;
			input.compensatedIntervals = 4881;
			input.rawWaitIntervals = 2440;

			const auto result = EvaluateDisplayRefreshRate(input);

			Assert::AreEqual(Decision(DisplayRefreshRateDecision::Quarantined),
				Decision(result.decision));
			Assert::AreEqual(Reason(DisplayRefreshRateReason::HarmonicMismatch),
				Reason(result.reason));
		}

		TEST_METHOD(StaleAndNonFiniteCandidatesFailClosed)
		{
			auto stale = StableInput(59.94, 59.941);
			stale.fresh = false;
			auto invalid = stale;
			invalid.fresh = true;
			invalid.candidateRateHz =
				std::numeric_limits<double>::quiet_NaN();

			const auto staleResult = EvaluateDisplayRefreshRate(stale);
			const auto invalidResult = EvaluateDisplayRefreshRate(invalid);

			Assert::AreEqual(Decision(DisplayRefreshRateDecision::Unavailable),
				Decision(staleResult.decision));
			Assert::AreEqual(Decision(DisplayRefreshRateDecision::Quarantined),
				Decision(invalidResult.decision));
		}

		TEST_METHOD(RealRefreshChangeRejectsOldRateThenAcceptsNewRate)
		{
			auto oldInput = StableInput(59.9405, 23.976);
			oldInput.rawWaitRateHz = 59.9405;
			const auto oldResult = EvaluateDisplayRefreshRate(oldInput);
			const auto newResult =
				EvaluateDisplayRefreshRate(StableInput(23.9763, 23.976));

			Assert::AreEqual(Decision(DisplayRefreshRateDecision::Quarantined),
				Decision(oldResult.decision));
			Assert::IsTrue(oldResult.shouldRecalculate);
			Assert::AreEqual(Decision(DisplayRefreshRateDecision::Accepted),
				Decision(newResult.decision));
		}

		TEST_METHOD(ExactRationalComparisonAcceptsEquivalentFractionsOnly)
		{
			Assert::IsTrue(DisplayRefreshRatesExactlyEqual(
				{ 60000, 1001 }, { 120000, 2002 }));
			Assert::IsFalse(DisplayRefreshRatesExactlyEqual(
				{ 60000, 1001 }, { 59940, 1000 }));
			Assert::IsFalse(DisplayRefreshRatesExactlyEqual(
				{ 60000, 0 }, { 60000, 1001 }));
		}

		TEST_METHOD(ModeSelectorPrefersEquivalentExactRational)
		{
			const DisplayRefreshModeSelection selection =
				SelectDisplayRefreshMode({ 24000, 1001 },
					{ { 24, 1 }, { 120000, 5005 }, { 60000, 1000 } });

			Assert::AreEqual(
				static_cast<int>(DisplayRefreshModeSelectionPath::ExactOrClose),
				static_cast<int>(selection.path));
			Assert::AreEqual(120000u, selection.selected.numerator);
			Assert::AreEqual(5005u, selection.selected.denominator);
			Assert::AreEqual(0.0, selection.differenceHz, 0.0000001);
		}

		TEST_METHOD(ModeSelectorUsesClosestInRangeFallback)
		{
			const DisplayRefreshModeSelection selection =
				SelectDisplayRefreshMode({ 24000, 1001 },
					{ { 24, 1 }, { 60, 1 }, { 50, 1 } });

			Assert::AreEqual(
				static_cast<int>(DisplayRefreshModeSelectionPath::ClosestInRange),
				static_cast<int>(selection.path));
			Assert::AreEqual(24u, selection.selected.numerator);
			Assert::AreEqual(1u, selection.selected.denominator);
		}

		TEST_METHOD(ModeSelectorRejectsUnrelatedFallbackFamilies)
		{
			const DisplayRefreshModeSelection selection =
				SelectDisplayRefreshMode({ 24000, 1001 },
					{ { 50, 1 }, { 60, 1 } });

			Assert::AreEqual(
				static_cast<int>(DisplayRefreshModeSelectionPath::None),
				static_cast<int>(selection.path));
		}

		TEST_METHOD(ModeSelectorTiesPreferHigherRefresh)
		{
			const DisplayRefreshModeSelection selection =
				SelectDisplayRefreshMode({ 60, 1 },
					{ { 5999, 100 }, { 6001, 100 } });

			Assert::AreEqual(
				static_cast<int>(DisplayRefreshModeSelectionPath::ExactOrClose),
				static_cast<int>(selection.path));
			Assert::AreEqual(6001u, selection.selected.numerator);
		}

		TEST_METHOD(RestoreEquivalenceAllowsTightDriverRoundingOnly)
		{
			Assert::IsTrue(DisplayRefreshRatesEquivalentForRestore(
				{ 60000, 1001 }, { 59951, 1000 }));
			Assert::IsFalse(DisplayRefreshRatesEquivalentForRestore(
				{ 60000, 1001 }, { 60000, 1000 }));
			Assert::IsFalse(DisplayRefreshRatesEquivalentForRestore(
				{ 60000, 0 }, { 59951, 1000 }));
		}

		TEST_METHOD(RestoreVerificationRequiresConsecutiveEquivalentObservations)
		{
			DisplayRefreshRestoreVerifier verifier({ 60000, 1001 });
			Assert::IsFalse(verifier.Observe(true, { 59951, 1000 }));
			Assert::IsFalse(verifier.Observe(false, {}));
			Assert::AreEqual(0u, verifier.ConsecutiveMatches());
			Assert::IsFalse(verifier.Observe(true, { 120000, 2002 }));
			Assert::IsTrue(verifier.Observe(true, { 59951, 1000 }));
		}

		TEST_METHOD(RefreshSwitchOwnershipExcludesEmbeddedPreview)
		{
			Assert::IsTrue(
				ShouldSwitchRefreshRateForPresentationTarget(false));
			Assert::IsFalse(
				ShouldSwitchRefreshRateForPresentationTarget(true));
		}

		TEST_METHOD(RefreshSwitchModeControlsEmbeddedPresentation)
		{
			Assert::IsFalse(ShouldSwitchRefreshRateForPresentationTarget(false,
				RefreshRateSwitchMode::Never));
			Assert::IsFalse(ShouldSwitchRefreshRateForPresentationTarget(true,
				RefreshRateSwitchMode::FullscreenOnly));
			Assert::IsTrue(ShouldSwitchRefreshRateForPresentationTarget(false,
				RefreshRateSwitchMode::FullscreenOnly));
			Assert::IsTrue(ShouldSwitchRefreshRateForPresentationTarget(true,
				RefreshRateSwitchMode::Always));
		}
	};
}
