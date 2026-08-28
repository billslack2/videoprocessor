#include "pch.h"
#include "CppUnitTest.h"

#include <RendererHealth.h>
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
	};


	TEST_CLASS(RendererHealthTrackerTests)
	{
	public:
		TEST_METHOD(WarmsBeforeReportingGood)
		{
			RendererHealthTracker tracker;
			for (uint64_t index = 0;
				index < RendererHealthTracker::WARMING_FRAME_COUNT - 1;
				++index)
			{
				tracker.RecordSuccessfulFrame(2.0, 1.0);
			}

			Assert::AreEqual(
				static_cast<int>(RendererHealthState::Warming),
				static_cast<int>(tracker.Snapshot(1000, 0).state));

			tracker.RecordSuccessfulFrame(2.0, 1.0);
			Assert::AreEqual(
				static_cast<int>(RendererHealthState::Good),
				static_cast<int>(tracker.Snapshot(1001, 0).state));
		}

		TEST_METHOD(ReportsRenderAndSubmitAveragesAndPeaks)
		{
			RendererHealthTracker tracker;
			tracker.RecordSuccessfulFrame(2.0, 1.0);
			tracker.RecordSuccessfulFrame(6.0, 3.0);

			const RendererHealthSnapshot snapshot = tracker.Snapshot(1000, 0);
			Assert::AreEqual(static_cast<uint64_t>(2), snapshot.framesRendered);
			Assert::AreEqual(4.0, snapshot.renderAverageMs, 0.001);
			Assert::AreEqual(6.0, snapshot.renderPeakMs, 0.001);
			Assert::AreEqual(2.0, snapshot.submitAverageMs, 0.001);
			Assert::AreEqual(3.0, snapshot.submitPeakMs, 0.001);
		}

		TEST_METHOD(RecentDropTemporarilyDegradesHealth)
		{
			RendererHealthTracker tracker;
			for (uint64_t index = 0;
				index < RendererHealthTracker::WARMING_FRAME_COUNT;
				++index)
			{
				tracker.RecordSuccessfulFrame(2.0, 1.0);
			}
			Assert::AreEqual(
				static_cast<int>(RendererHealthState::Good),
				static_cast<int>(tracker.Snapshot(1000, 0).state));

			RendererHealthSnapshot snapshot = tracker.Snapshot(2000, 1);
			Assert::AreEqual(
				static_cast<int>(RendererHealthState::Degraded),
				static_cast<int>(snapshot.state));
			Assert::AreEqual(static_cast<uint64_t>(1), snapshot.droppedFrames);

			snapshot = tracker.Snapshot(
				2000 + RendererHealthTracker::ISSUE_VISIBILITY_MS + 1, 1);
			Assert::AreEqual(
				static_cast<int>(RendererHealthState::Good),
				static_cast<int>(snapshot.state));
		}

		TEST_METHOD(StallCountDurationAndHealthAreReported)
		{
			RendererHealthTracker tracker;
			for (uint64_t index = 0;
				index < RendererHealthTracker::WARMING_FRAME_COUNT;
				++index)
			{
				tracker.RecordSuccessfulFrame(2.0, 1.0);
			}
			tracker.RecordStall(2000, 64.5);

			const RendererHealthSnapshot snapshot = tracker.Snapshot(2001, 0);
			Assert::AreEqual(static_cast<uint64_t>(1), snapshot.timesStalled);
			Assert::AreEqual(64.5, snapshot.stalledMs, 0.001);
			Assert::AreEqual(
				static_cast<int>(RendererHealthState::Degraded),
				static_cast<int>(snapshot.state));
		}

		TEST_METHOD(DroppedCounterResetDoesNotCreateFalseIssue)
		{
			RendererHealthTracker tracker;
			tracker.Reset(5);
			for (uint64_t index = 0;
				index < RendererHealthTracker::WARMING_FRAME_COUNT;
				++index)
			{
				tracker.RecordSuccessfulFrame(2.0, 1.0);
			}

			const RendererHealthSnapshot snapshot = tracker.Snapshot(1000, 2);
			Assert::AreEqual(static_cast<uint64_t>(2), snapshot.droppedFrames);
			Assert::AreEqual(
				static_cast<int>(RendererHealthState::Good),
				static_cast<int>(snapshot.state));
		}
	};
}
