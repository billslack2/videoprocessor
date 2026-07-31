#include "pch.h"
#include "CppUnitTest.h"

#include <DisplayRefreshRateEstimator.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
void AddSeconds(DisplayRefreshRateEstimator& estimator, int firstSecond,
	int lastSecond, uint64_t intervalsPerSecond)
{
	for (int second = firstSecond; second <= lastSecond; ++second)
		estimator.Observe(second * 1000, 1000, intervalsPerSecond);
}
}

namespace Tests
{
	TEST_CLASS(DisplayRefreshRateEstimatorTests)
	{
	public:
		TEST_METHOD(QuarantineDiscardsInitialSyncSamplesBeforeReadinessEvidence)
		{
			DisplayRefreshRateEstimator estimator(1000);
			AddSeconds(estimator, 1, 15, 30);

			const DisplayRefreshRateEstimatorSnapshot snapshot =
				estimator.Snapshot();

			Assert::IsTrue(snapshot.quarantineComplete);
			Assert::AreEqual(9.0, snapshot.evidenceSeconds, 0.000001);
			Assert::IsFalse(snapshot.readinessEvidenceReady);

			estimator.Observe(16000, 1000, 30);
			estimator.Observe(17000, 1000, 30);
			Assert::IsTrue(estimator.Snapshot().readinessEvidenceReady);
		}

		TEST_METHOD(PhaseEvidenceUsesLongerCleanHistoryThanReadiness)
		{
			DisplayRefreshRateEstimator estimator(1000);
			AddSeconds(estimator, 1, 36, 60);

			const DisplayRefreshRateEstimatorSnapshot snapshot =
				estimator.Snapshot();

			Assert::IsTrue(snapshot.readinessEvidenceReady);
			Assert::IsTrue(snapshot.phaseEvidenceReady);
			Assert::AreEqual(60.0, snapshot.readinessRateHz, 0.000001);
			Assert::AreEqual(60.0, snapshot.phaseRateHz, 0.000001);
		}

		TEST_METHOD(RecentRateDominatesLongHistoryAndStepIsFlaggedPromptly)
		{
			DisplayRefreshRateEstimator estimator(1000);
			AddSeconds(estimator, 1, 65, 50);
			AddSeconds(estimator, 66, 95, 60);

			const DisplayRefreshRateEstimatorSnapshot snapshot =
				estimator.Snapshot();

			Assert::AreEqual(60.0, snapshot.fastRateHz, 0.000001);
			Assert::IsTrue(snapshot.phaseRateHz > 55.0);
			Assert::IsTrue(snapshot.phaseRateHz < 60.0);
			Assert::IsTrue(snapshot.materialRateChangeDetected);
			Assert::IsFalse(snapshot.phaseEvidenceReady);
		}

		TEST_METHOD(ResetDropsAllPreviousRateEvidence)
		{
			DisplayRefreshRateEstimator estimator(1000);
			AddSeconds(estimator, 1, 40, 60);
			Assert::IsTrue(estimator.Snapshot().phaseEvidenceReady);

			estimator.Reset();
			AddSeconds(estimator, 100, 116, 24);
			const DisplayRefreshRateEstimatorSnapshot snapshot =
				estimator.Snapshot();

			Assert::AreEqual(24.0, snapshot.readinessRateHz, 0.000001);
			Assert::IsTrue(snapshot.readinessEvidenceReady);
			Assert::IsFalse(snapshot.phaseEvidenceReady);
		}
	};
}
