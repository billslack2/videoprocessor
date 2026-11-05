#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/AlphaPresentationTelemetry.h>
#include <SourceQueueDisposition.h>
#include <thread>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(AlphaPresentationTelemetryTests)
	{
	public:
        TEST_METHOD(SourceQueueDispositionsRetainAllInstancesAndExcludeRepeatCopies)
        {
            SourceQueueDispositionMeter process;
            const unsigned omissions[5]={2,2774,894,206,2};
            const unsigned capacity[5]={0,7,11,0,0};
            for(unsigned instance=0;instance<5;++instance) {
                // Real saved five-instance totals. A new renderer/continuity
                // does not recreate this process-owned meter or subtract IDs.
                for(unsigned n=0;n<omissions[instance];++n)
                    process.Retire(n<capacity[instance]?SourceQueueDisposition::Capacity:SourceQueueDisposition::Recovery);
                process.Retire(SourceQueueDisposition::Lifecycle,true);
                process.Retire(SourceQueueDisposition::Recovery,true);
            }
            auto result=process.Read();
            Assert::AreEqual(uint64_t(3878),result.RetiredSources());
            Assert::AreEqual(uint64_t(3860),result.recovery);
            Assert::AreEqual(uint64_t(18),result.capacity);
            Assert::AreEqual(uint64_t(10),result.repeatCopies);
            // Retirement after the final HUD poll still contributes; later
            // polls, resync/clock discontinuities and renderer recreation do not.
            process.Retire(SourceQueueDisposition::Lifecycle);
            process.Retire(SourceQueueDisposition::Lifecycle);
            process.Retire(SourceQueueDisposition::DueSource);
            for(unsigned poll=0;poll<100;++poll) {
                result=process.Read();
                Assert::AreEqual(uint64_t(3881),result.RetiredSources());
                Assert::AreEqual(uint64_t(2),result.lifecycle);
                Assert::AreEqual(uint64_t(1),result.dueSource);
            }
        }
        TEST_METHOD(SourceQueueDispositionsAreAtomicAcrossCaptureAndRetirement)
        {
            SourceQueueDispositionMeter process;
            std::vector<std::thread> producers;
            for(unsigned t=0;t<4;++t)producers.emplace_back([&] {
                for(unsigned n=0;n<2000;++n) {
                    process.Retire(SourceQueueDisposition::Recovery);
                    process.Retire(SourceQueueDisposition::Lifecycle,true);
                }
            });
            for(auto& t:producers)t.join();
            Assert::AreEqual(uint64_t(8000),process.Read().RetiredSources());
            Assert::AreEqual(uint64_t(8000),process.Read().repeatCopies);
        }
        TEST_METHOD(OptionalQueueDispositionSnapshotsDoNotInventOrAccumulatePolls)
        {
            SourceQueueDispositionSnapshotV1 current;current.reportingModules=1;current.recovery=10;
            auto wrong=current;wrong.version=2;
            Assert::IsFalse(CopySourceQueueDispositionSnapshot(current,&wrong));
            Assert::AreEqual(uint32_t(2),wrong.version);
            wrong=current;--wrong.size;
            Assert::IsFalse(CopySourceQueueDispositionSnapshot(current,&wrong));
            Assert::IsFalse(CopySourceQueueDispositionSnapshot(current,nullptr));
            const auto supported=+[](SourceQueueDispositionSnapshotV1* out)->bool {
                SourceQueueDispositionSnapshotV1 value;value.reportingModules=1;value.recovery=10;
                return CopySourceQueueDispositionSnapshot(value,out);
            };
            const auto unsupported=+[](SourceQueueDispositionSnapshotV1* out)->bool {
                out->version=2;out->recovery=999;return true;
            };
            for(unsigned poll=0;poll<100;++poll) {
                SourceQueueDispositionSnapshotV1 total;
                AddSourceQueueDispositionModule(total,supported);
                AddSourceQueueDispositionModule(total,supported);
                Assert::AreEqual(uint64_t(20),total.RetiredSources());
                Assert::AreEqual(uint32_t(2),total.reportingModules);
                AddSourceQueueDispositionModule(total,nullptr);
                AddSourceQueueDispositionModule(total,unsupported);
                Assert::AreEqual(uint32_t(2),total.unavailableModules);
                Assert::AreEqual(uint64_t(20),total.RetiredSources());
            }
        }
        TEST_METHOD(PresentTimingMeasuresOneCallWithoutChangingItsResult)
        {
            AlphaPresentationCallTiming timing;
            int calls=0,clocks=0;
            const int32_t failed=static_cast<int32_t>(0x887A0001u);
            const auto invoke=[&]{++calls;return failed;};
            const auto clock=[&]() -> int64_t {return ++clocks==1?1100:1394;};
            Assert::AreEqual(failed,MeasureAlphaPresentationCall(invoke,clock,&timing));
            Assert::AreEqual(1,calls);Assert::AreEqual(2,clocks);
            Assert::AreEqual(failed,timing.result);
            Assert::IsTrue(timing.Within(1000,1400,1000));
            Assert::AreEqual(int64_t(100),timing.startedQpc-1000);
            Assert::AreEqual(int64_t(294),timing.endedQpc-timing.startedQpc);
            Assert::AreEqual(int64_t(6),1400-timing.endedQpc);
            Assert::IsFalse(timing.Within(1200,1400,1000));
            Assert::IsFalse(timing.Within(1000,1300,1000));
            Assert::IsFalse(timing.Within(1000,1400,0));
            Assert::IsFalse(AlphaPresentationCallTiming{}.Within(1000,1400,1000));
            calls=clocks=0;
            Assert::AreEqual(failed,MeasureAlphaPresentationCall(invoke,clock,nullptr));
            Assert::AreEqual(1,calls);Assert::AreEqual(0,clocks);
        }

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
	};
}
