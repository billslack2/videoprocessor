#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/AlphaPresentationTelemetry.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(AlphaPresentationTelemetryTests)
	{
	public:
		static AlphaPresentationRecord Record(
			uint64_t sequence, uint64_t generation = 1)
		{
			AlphaPresentationRecord record;
			record.generation = generation;
			record.sourceSequence = sequence;
			record.presentId = static_cast<uint32_t>(sequence);
			return record;
		}

		static AlphaDxgiPresentationSample Sample(
			uint32_t count, int64_t qpc, uint64_t generation = 1)
		{
			AlphaDxgiPresentationSample sample;
			sample.generation = generation;
			sample.timingStatus = AlphaPresentationTimingStatus::Available;
			sample.available = true;
			sample.presentCount = count;
			sample.presentRefreshCount = count;
			sample.syncRefreshCount = count;
			sample.syncQpc = qpc;
			sample.qpcFrequency = 1000;
			return sample;
		}

		TEST_METHOD(OsdRateIsAvailableBeforeStableEvidenceAndResetsOnDisjoint)
		{
			AlphaPresentationTelemetry telemetry;
			telemetry.Observe(Sample(1, 1000));
			telemetry.Observe(Sample(2, 1040));
			Assert::AreEqual(25.0, telemetry.Snapshot().observedDisplayHz, 0.001);
			Assert::AreEqual(0.0, telemetry.Snapshot().measuredDisplayHz);
			Assert::IsTrue(telemetry.Snapshot().evidence == AlphaPresentationEvidence::Warming);
			auto disjoint = Sample(3, 1080);
			disjoint.disjoint = true;
			telemetry.Observe(disjoint);
			Assert::AreEqual(0.0, telemetry.Snapshot().observedDisplayHz);
		}
		TEST_METHOD(BoundsRetainedCorrelationRecords)
		{
			AlphaPresentationTelemetry telemetry(3);
			for (uint64_t sequence = 1; sequence <= 5; ++sequence)
				telemetry.RecordSubmission(Record(sequence));

			Assert::AreEqual<size_t>(3, telemetry.Snapshot().retainedRecords);
			Assert::AreEqual(
				static_cast<uint64_t>(3),
				telemetry.RecordsForTesting().front().sourceSequence);
		}

		TEST_METHOD(NewGenerationCannotCorrelateOldRecords)
		{
			AlphaPresentationTelemetry telemetry;
			telemetry.RecordSubmission(Record(1, 1));
			telemetry.RecordSubmission(Record(7, 2));

			const AlphaPresentationSnapshot snapshot = telemetry.Snapshot();
			Assert::AreEqual(static_cast<uint64_t>(2), snapshot.generation);
			Assert::AreEqual<size_t>(1, snapshot.retainedRecords);
			Assert::AreEqual(static_cast<uint64_t>(7),
				telemetry.RecordsForTesting().front().sourceSequence);
		}

		TEST_METHOD(CorrelatesPresentIdAndReportsSequenceDebt)
		{
			AlphaPresentationTelemetry telemetry;
			telemetry.RecordSubmission(Record(1));
			telemetry.RecordSubmission(Record(2));
			telemetry.RecordSubmission(Record(3));
			telemetry.Observe(Sample(2, 1000));

			const AlphaPresentationSnapshot snapshot = telemetry.Snapshot();
			Assert::AreEqual(static_cast<uint64_t>(2),
				snapshot.lastPresentedSequence);
			Assert::AreEqual(static_cast<uint64_t>(1),
				snapshot.sourceToPresentDebt);
			Assert::IsTrue(telemetry.RecordsForTesting()[0].presented);
			Assert::IsTrue(telemetry.RecordsForTesting()[1].presented);
			Assert::IsFalse(telemetry.RecordsForTesting()[2].presented);
		}

		TEST_METHOD(DebtCountsOutstandingPresentsRatherThanSequenceGaps)
		{
			AlphaPresentationTelemetry telemetry;
			AlphaPresentationRecord first = Record(2);
			first.presentId = 1;
			AlphaPresentationRecord second = Record(9);
			second.presentId = 2;
			telemetry.RecordSubmission(first);
			telemetry.RecordSubmission(second);
			telemetry.Observe(Sample(2, 1000));

			Assert::AreEqual(static_cast<uint64_t>(0),
				telemetry.Snapshot().sourceToPresentDebt);
		}

		TEST_METHOD(RequiresStabilizedMeasuredCadence)
		{
			AlphaPresentationTelemetry telemetry;
			for (uint32_t index = 1; index <= 20; ++index)
			{
				telemetry.RecordSubmission(Record(index));
				telemetry.Observe(Sample(index, index * 100));
			}

			const AlphaPresentationSnapshot snapshot = telemetry.Snapshot();
			Assert::AreEqual(static_cast<int>(AlphaPresentationEvidence::Stable),
				static_cast<int>(snapshot.evidence));
			Assert::AreEqual(10.0, snapshot.measuredDisplayHz, 0.001);
		}

		TEST_METHOD(RepeatedFrameStatisticsDoNotAdvanceWarming)
		{
			AlphaPresentationTelemetry telemetry;
			telemetry.Observe(Sample(1, 100));
			telemetry.Observe(Sample(2, 200));
			telemetry.Observe(Sample(2, 200));
			Assert::AreEqual(static_cast<uint32_t>(2),
				telemetry.Snapshot().cadenceSamples);
			Assert::IsTrue(telemetry.Snapshot().evidence ==
				AlphaPresentationEvidence::Warming);
			telemetry.Observe(Sample(3, 300));
			Assert::AreEqual(static_cast<uint32_t>(3),
				telemetry.Snapshot().cadenceSamples);
		}

		TEST_METHOD(RepeatedFrameStatisticsPreserveStableEvidence)
		{
			AlphaPresentationTelemetry telemetry;
			for (uint32_t index = 1; index <= 10; ++index)
				telemetry.Observe(Sample(index, index * 100));
			telemetry.Observe(Sample(10, 1000));
			Assert::IsTrue(telemetry.Snapshot().evidence ==
				AlphaPresentationEvidence::Stable);
			Assert::AreEqual(static_cast<uint32_t>(10),
				telemetry.Snapshot().cadenceSamples);
			telemetry.Observe(Sample(11, 1100));
			Assert::IsTrue(telemetry.Snapshot().evidence ==
				AlphaPresentationEvidence::Stable);
			Assert::AreEqual(static_cast<uint32_t>(11),
				telemetry.Snapshot().cadenceSamples);
		}

		TEST_METHOD(StaleFrameStatisticsEventuallyLoseStableEvidence)
		{
			AlphaPresentationTelemetry telemetry;
			for (uint32_t index = 1; index <= 10; ++index)
				telemetry.Observe(Sample(index, index * 100));
			for (uint32_t duplicate = 0; duplicate < 17; ++duplicate)
				telemetry.Observe(Sample(10, 1000));
			Assert::IsTrue(telemetry.Snapshot().evidence ==
				AlphaPresentationEvidence::Unavailable);
			Assert::AreEqual(static_cast<uint32_t>(0),
				telemetry.Snapshot().cadenceSamples);
			telemetry.Observe(Sample(11, 1100));
			Assert::IsTrue(telemetry.Snapshot().evidence ==
				AlphaPresentationEvidence::Warming);
			Assert::AreEqual(static_cast<uint32_t>(1),
				telemetry.Snapshot().cadenceSamples);
		}

		TEST_METHOD(InconsistentFrameStatisticsStillInvalidateCadence)
		{
			AlphaPresentationTelemetry telemetry;
			for (uint32_t index = 1; index <= 10; ++index)
				telemetry.Observe(Sample(index, index * 100));
			AlphaDxgiPresentationSample inconsistent = Sample(11, 1100);
			inconsistent.syncRefreshCount = 10;
			telemetry.Observe(inconsistent);
			Assert::IsTrue(telemetry.Snapshot().evidence ==
				AlphaPresentationEvidence::Disjoint);
			Assert::AreEqual(static_cast<uint32_t>(0),
				telemetry.Snapshot().cadenceSamples);
		}

		TEST_METHOD(AcceptsCadenceMatchingExpectedOutput)
		{
			AlphaPresentationTelemetry telemetry;
			for (uint32_t index = 1; index <= 10; ++index)
			{
				AlphaDxgiPresentationSample sample =
					Sample(index * 6, index * 100);
				sample.expectedDisplayHz = 60.0;
				telemetry.Observe(sample);
			}

			const AlphaPresentationSnapshot snapshot = telemetry.Snapshot();
			Assert::AreEqual(static_cast<int>(AlphaPresentationEvidence::Stable),
				static_cast<int>(snapshot.evidence));
			Assert::AreEqual(60.0, snapshot.measuredDisplayHz, 0.001);
		}

		TEST_METHOD(RejectsCadenceFromDifferentOutput)
		{
			AlphaPresentationTelemetry telemetry;
			for (uint32_t index = 1; index <= 10; ++index)
			{
				AlphaDxgiPresentationSample sample =
					Sample(index * 6, index * 100);
				sample.expectedDisplayHz = 60.0;
				telemetry.Observe(sample);
			}
			AlphaDxgiPresentationSample wrongOutput = Sample(75, 1200);
			wrongOutput.expectedDisplayHz = 60.0;
			telemetry.Observe(wrongOutput);

			const AlphaPresentationSnapshot snapshot = telemetry.Snapshot();
			Assert::AreEqual(static_cast<int>(AlphaPresentationEvidence::Disjoint),
				static_cast<int>(snapshot.evidence));
			Assert::AreEqual(0.0, snapshot.measuredDisplayHz);
			Assert::AreEqual(static_cast<uint32_t>(0), snapshot.cadenceSamples);
		}

		TEST_METHOD(DisjointEvidenceFailsClosedUntilRestabilized)
		{
			AlphaPresentationTelemetry telemetry;
			for (uint32_t index = 1; index <= 10; ++index)
				telemetry.Observe(Sample(index, index * 100));

			AlphaDxgiPresentationSample disjoint;
			disjoint.generation = 1;
			disjoint.disjoint = true;
			telemetry.Observe(disjoint);

			const AlphaPresentationSnapshot snapshot = telemetry.Snapshot();
			Assert::AreEqual(static_cast<int>(AlphaPresentationEvidence::Disjoint),
				static_cast<int>(snapshot.evidence));
			Assert::AreEqual(0.0, snapshot.measuredDisplayHz);
			Assert::AreEqual(static_cast<uint32_t>(0), snapshot.cadenceSamples);
		}

		TEST_METHOD(UnavailableEvidenceFailsClosed)
		{
			AlphaPresentationTelemetry telemetry;
			telemetry.RecordSubmission(Record(1));
			AlphaDxgiPresentationSample unavailable;
			unavailable.generation = 1;
			telemetry.Observe(unavailable);

			Assert::AreEqual(
				static_cast<int>(AlphaPresentationEvidence::Unavailable),
				static_cast<int>(telemetry.Snapshot().evidence));
		}

		TEST_METHOD(ReportsUnavailableTimingApiSeparatelyFromWarming)
		{
			AlphaPresentationTelemetry telemetry;
			telemetry.RecordSubmission(Record(1));
			AlphaDxgiPresentationSample unavailable;
			unavailable.generation = 1;
			unavailable.timingStatus =
				AlphaPresentationTimingStatus::FrameStatisticsUnavailable;
			unavailable.frameStatisticsResult = static_cast<int32_t>(0x887A0004L);
			telemetry.Observe(unavailable);

			const AlphaPresentationSnapshot snapshot = telemetry.Snapshot();
			Assert::AreEqual(static_cast<int>(AlphaPresentationEvidence::Unavailable),
				static_cast<int>(snapshot.evidence));
			Assert::AreEqual(
				static_cast<int>(AlphaPresentationTimingStatus::FrameStatisticsUnavailable),
				static_cast<int>(snapshot.timingStatus));
			Assert::AreEqual(static_cast<int32_t>(0x887A0004L),
				snapshot.frameStatisticsResult);
		}

		TEST_METHOD(ReportsTimingApiFailureSeparatelyFromUnsupportedMode)
		{
			AlphaPresentationTelemetry telemetry;
			AlphaDxgiPresentationSample failed;
			failed.generation = 1;
			failed.timingStatus = AlphaPresentationTimingStatus::FrameStatisticsFailed;
			failed.frameStatisticsResult = static_cast<int32_t>(0x80004005L);
			telemetry.Observe(failed);

			const AlphaPresentationSnapshot snapshot = telemetry.Snapshot();
			Assert::AreEqual(
				static_cast<int>(AlphaPresentationTimingStatus::FrameStatisticsFailed),
				static_cast<int>(snapshot.timingStatus));
			Assert::AreEqual(static_cast<int32_t>(0x80004005L),
				snapshot.frameStatisticsResult);
		}

		TEST_METHOD(ArrivalToScreenIsMeasuredOnlyOnTheNewestPresentsOwnRefresh)
		{
			AlphaPresentationTelemetry telemetry;
			Assert::IsTrue(
				telemetry.Snapshot().lastPresentedArrivalToScreenMs < 0.0);
			for (uint64_t sequence = 1; sequence <= 4; ++sequence)
			{
				AlphaPresentationRecord record = Record(sequence);
				record.enqueueQpc = 860 + static_cast<int64_t>(sequence) * 40;
				record.cadenceRepeat = sequence == 4;
				telemetry.RecordSubmission(record);
			}

			// Present 1 on its own refresh: arrival 900, screen 1000.
			telemetry.Observe(Sample(1, 1000));
			Assert::AreEqual(100.0,
				telemetry.Snapshot().lastPresentedArrivalToScreenMs, 0.001);

			// Presents 2 and 3 are reached past, 4 is a cadence repeat.
			telemetry.Observe(Sample(4, 1080));
			Assert::AreEqual(100.0,
				telemetry.Snapshot().lastPresentedArrivalToScreenMs, 0.001);

			// The sync sample is a later refresh than the present's.
			AlphaPresentationRecord fifth = Record(5);
			fifth.enqueueQpc = 1060;
			telemetry.RecordSubmission(fifth);
			AlphaDxgiPresentationSample later = Sample(5, 1120);
			later.syncRefreshCount = 6;
			telemetry.Observe(later);
			Assert::AreEqual(100.0,
				telemetry.Snapshot().lastPresentedArrivalToScreenMs, 0.001);

			AlphaPresentationRecord sixth = Record(6);
			sixth.enqueueQpc = 1100;
			telemetry.RecordSubmission(sixth);
			AlphaDxgiPresentationSample own = Sample(7, 1160);
			own.presentCount = 6;
			telemetry.Observe(own);
			Assert::AreEqual(60.0,
				telemetry.Snapshot().lastPresentedArrivalToScreenMs, 0.001);
		}

		TEST_METHOD(ArrivalToScreenIsUnknownAfterEachReset)
		{
			AlphaPresentationTelemetry telemetry;
			AlphaPresentationRecord first = Record(1);
			first.enqueueQpc = 900;
			telemetry.RecordSubmission(first);
			telemetry.Observe(Sample(1, 1000));
			Assert::AreEqual(100.0,
				telemetry.Snapshot().lastPresentedArrivalToScreenMs, 0.001);

			auto disjoint = Sample(2, 1040);
			disjoint.disjoint = true;
			telemetry.Observe(disjoint);
			Assert::IsTrue(
				telemetry.Snapshot().lastPresentedArrivalToScreenMs < 0.0);

			AlphaPresentationRecord second = Record(2);
			second.enqueueQpc = 1000;
			telemetry.RecordSubmission(second);
			telemetry.Observe(Sample(2, 1080));
			Assert::AreEqual(80.0,
				telemetry.Snapshot().lastPresentedArrivalToScreenMs, 0.001);

			telemetry.RecordSubmission(Record(3, 2));
			Assert::IsTrue(
				telemetry.Snapshot().lastPresentedArrivalToScreenMs < 0.0);
		}

		TEST_METHOD(ArrivalToScreenLapsesAfterSixteenSamplesWithoutANewOne)
		{
			AlphaPresentationTelemetry telemetry;
			AlphaPresentationRecord first = Record(1);
			first.enqueueQpc = 900;
			telemetry.RecordSubmission(first);
			telemetry.Observe(Sample(1, 1000));
			Assert::AreEqual(100.0,
				telemetry.Snapshot().lastPresentedArrivalToScreenMs, 0.001);

			// Presents 2 to 16 reach the screen with no record to measure:
			// fifteen samples without a new time.
			for (uint32_t count = 2; count <= 16; ++count)
				telemetry.Observe(
					Sample(count, 1000 + 40 * static_cast<int64_t>(count - 1)));
			Assert::AreEqual(100.0,
				telemetry.Snapshot().lastPresentedArrivalToScreenMs, 0.001);

			telemetry.Observe(Sample(17, 1640));
			Assert::IsTrue(
				telemetry.Snapshot().lastPresentedArrivalToScreenMs < 0.0);

			AlphaPresentationRecord next = Record(18);
			next.enqueueQpc = 1610;
			telemetry.RecordSubmission(next);
			telemetry.Observe(Sample(18, 1680));
			Assert::AreEqual(70.0,
				telemetry.Snapshot().lastPresentedArrivalToScreenMs, 0.001);
		}

		TEST_METHOD(ARepeatedReportCountsTowardTheArrivalToScreenLapse)
		{
			AlphaPresentationTelemetry telemetry;
			AlphaPresentationRecord first = Record(1);
			first.enqueueQpc = 900;
			telemetry.RecordSubmission(first);
			telemetry.Observe(Sample(1, 1000));
			for (uint32_t count = 2; count <= 16; ++count)
				telemetry.Observe(
					Sample(count, 1000 + 40 * static_cast<int64_t>(count - 1)));
			Assert::AreEqual(100.0,
				telemetry.Snapshot().lastPresentedArrivalToScreenMs, 0.001);

			// The same report again brings no new time: the sixteenth sample.
			// The cadence evidence itself is not reset by one repeat.
			telemetry.Observe(Sample(16, 1600));
			const auto snapshot = telemetry.Snapshot();
			Assert::IsTrue(snapshot.lastPresentedArrivalToScreenMs < 0.0);
			Assert::IsTrue(
				snapshot.evidence == AlphaPresentationEvidence::Stable);
		}
	};
}
