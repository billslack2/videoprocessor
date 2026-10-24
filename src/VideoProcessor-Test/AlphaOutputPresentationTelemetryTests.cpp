#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/AlphaOutputPresentationTelemetry.h>
#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests {
TEST_CLASS(AlphaOutputPresentationTelemetryTests) {
public:
    static AlphaOutputSubmission Submission(uint32_t id,uint32_t maximum=1,
        double hz=60.0,uint64_t generation=1,uint64_t epoch=1) {
        AlphaOutputSubmission result;
        result.generation=generation;result.presentationEpoch=epoch;
        result.presentId=id;result.intendedMaximumRefreshes=maximum;result.expectedDisplayHz=hz;
        return result;
    }
    static AlphaDxgiPresentationSample Sample(uint32_t id,uint32_t refresh,
        double hz=60.0,uint64_t generation=1) {
        AlphaDxgiPresentationSample result;
        result.generation=generation;result.timingStatus=AlphaPresentationTimingStatus::Available;
        result.available=true;result.presentCount=id;result.presentRefreshCount=refresh;
        result.syncRefreshCount=refresh;result.qpcFrequency=1000000;
        result.syncQpc=1+static_cast<int64_t>(refresh*1000000.0/hz);
        result.expectedDisplayHz=hz;return result;
    }
    static void Counts(const AlphaOutputPresentationTelemetry& telemetry,
        uint64_t holds,uint64_t excess,uint64_t verified) {
        const auto result=telemetry.Snapshot();
        Assert::AreEqual(holds,result.observedLongHolds);
        Assert::AreEqual(excess,result.excessRefreshes);
        Assert::AreEqual(verified,result.verifiedIntervals);
    }
    static void SeedLongHold(AlphaOutputPresentationTelemetry& telemetry) {
        Assert::IsTrue(telemetry.RecordSubmission(Submission(1)));
        Assert::IsTrue(telemetry.Observe(Sample(1,1)));
        Assert::IsTrue(telemetry.RecordSubmission(Submission(2)));
        Assert::IsTrue(telemetry.Observe(Sample(2,4)));
        Counts(telemetry,1,2,1);
    }

    TEST_METHOD(ConservativeIntentAllowsNaturalCadenceWithoutRatioFudge) {
        Assert::AreEqual(1u,AlphaOutputPresentationTelemetry::IntendedMaximumRefreshes(60,60));
        Assert::AreEqual(2u,AlphaOutputPresentationTelemetry::IntendedMaximumRefreshes(60,59.94));
        Assert::AreEqual(3u,AlphaOutputPresentationTelemetry::IntendedMaximumRefreshes(60,24));
        Assert::AreEqual(3u,AlphaOutputPresentationTelemetry::IntendedMaximumRefreshes(59.95,23.976));
        Assert::AreEqual(1u,AlphaOutputPresentationTelemetry::IntendedMaximumRefreshes(59.95,59.95));
        Assert::AreEqual(1u,AlphaOutputPresentationTelemetry::IntendedMaximumRefreshes(60,120));
        for(double invalid:{0.0,-1.0,9.0,501.0,
            (std::numeric_limits<double>::infinity)(),(std::numeric_limits<double>::quiet_NaN)()}) {
            Assert::AreEqual(0u,AlphaOutputPresentationTelemetry::IntendedMaximumRefreshes(invalid,60));
            Assert::AreEqual(0u,AlphaOutputPresentationTelemetry::IntendedMaximumRefreshes(60,invalid));
        }
    }
    TEST_METHOD(EveryRefreshIsVerifiedAndRepeatedPollsDoNotRenewEvidence) {
        AlphaOutputPresentationTelemetry telemetry;
        for(uint32_t id=1;id<=60;++id) {
            Assert::IsTrue(telemetry.RecordSubmission(Submission(id)));
            Assert::IsTrue(telemetry.Observe(Sample(id,id)));
            const auto before=telemetry.Snapshot();
            for(int repeat=0;repeat<8;++repeat)Assert::IsFalse(telemetry.Observe(Sample(id,id)));
            Assert::AreEqual(before.observationGaps,telemetry.Snapshot().observationGaps);
        }
        Counts(telemetry,0,0,59);
        Assert::IsTrue(telemetry.Snapshot().availability==AlphaOutputPresentationAvailability::Observing);
    }
    TEST_METHOD(NaturalTwentyFourToSixtyHoldsAreNotLongHolds) {
        AlphaOutputPresentationTelemetry telemetry;
        uint32_t refresh=1;
        for(uint32_t id=1;id<=30;++id) {
            if(id>1)refresh+=id%2?2:3;
            Assert::IsTrue(telemetry.RecordSubmission(Submission(id,3)));
            Assert::IsTrue(telemetry.Observe(Sample(id,refresh)));
        }
        Counts(telemetry,0,0,29);
    }
    TEST_METHOD(FullRateOutputsCountOnlyObservedExcessRefreshes) {
        AlphaOutputPresentationTelemetry telemetry;
        uint32_t refresh=1;uint64_t excess=0;
        for(uint32_t id=1;id<=12;++id) {
            if(id>1) { const auto interval=id%2?2u:3u;refresh+=interval;excess+=interval-1; }
            Assert::IsTrue(telemetry.RecordSubmission(Submission(id,1)));
            Assert::IsTrue(telemetry.Observe(Sample(id,refresh)));
        }
        Counts(telemetry,11,excess,11);
    }
    TEST_METHOD(OneLongHoldIsNotCountedAgainByDuplicateStatistics) {
        AlphaOutputPresentationTelemetry telemetry;
        SeedLongHold(telemetry);
        const auto before=telemetry.Snapshot();
        for(int poll=0;poll<100;++poll)Assert::IsFalse(telemetry.Observe(Sample(2,4)));
        Counts(telemetry,1,2,1);
        Assert::AreEqual(before.observationGaps,telemetry.Snapshot().observationGaps);
        telemetry.RecordSubmission(Submission(3));telemetry.Observe(Sample(3,5));
        Counts(telemetry,1,2,2);
    }
    TEST_METHOD(SkippedObservedIdsRemainUnknownAndNeverBecomeDrops) {
        AlphaOutputPresentationTelemetry telemetry;
        for(uint32_t id=1;id<=5;++id)telemetry.RecordSubmission(Submission(id));
        Assert::IsTrue(telemetry.Observe(Sample(1,1)));
        Assert::IsTrue(telemetry.Observe(Sample(4,10)));
        Counts(telemetry,0,0,0);
        Assert::AreEqual(uint64_t(1),telemetry.Snapshot().observationGaps);
        Assert::IsTrue(telemetry.Snapshot().availability==AlphaOutputPresentationAvailability::Limited);
        Assert::IsTrue(telemetry.Observe(Sample(5,11)));
        Counts(telemetry,0,0,1);
    }
    TEST_METHOD(SubmissionCountAloneNeverProvesDisplayOrLongHolds) {
        AlphaOutputPresentationTelemetry telemetry;
        for(uint32_t id=1;id<=100;++id)telemetry.RecordSubmission(Submission(id));
        Counts(telemetry,0,0,0);
        Assert::IsTrue(telemetry.Snapshot().availability==AlphaOutputPresentationAvailability::Warming);
        Assert::IsTrue(telemetry.Observe(Sample(100,400)));
        Counts(telemetry,0,0,0);
    }
    TEST_METHOD(UnmatchedAndWrongGenerationObservationsBreakContinuity) {
        for(int mutation=0;mutation<2;++mutation) {
            AlphaOutputPresentationTelemetry telemetry;
            telemetry.RecordSubmission(Submission(1));telemetry.Observe(Sample(1,1));
            auto unknown=Sample(2,20);if(mutation)unknown.generation=2;
            Assert::IsFalse(telemetry.Observe(unknown));
            telemetry.RecordSubmission(Submission(3));telemetry.Observe(Sample(3,21));
            telemetry.RecordSubmission(Submission(4));telemetry.Observe(Sample(4,22));
            Counts(telemetry,0,0,1);
            Assert::IsTrue(telemetry.Snapshot().observationGaps>0);
        }
    }
    TEST_METHOD(EvictedRecordsCannotConfirmAnInterval) {
        AlphaOutputPresentationTelemetry telemetry;
        telemetry.RecordSubmission(Submission(1));telemetry.Observe(Sample(1,1));
        for(uint32_t id=2;id<=AlphaOutputPresentationTelemetry::Capacity+1;++id)
            telemetry.RecordSubmission(Submission(id));
        // Even the copied previous anchor must still be in the bounded ledger.
        Assert::IsTrue(telemetry.Observe(Sample(2,30)));
        Counts(telemetry,0,0,0);
        Assert::IsFalse(telemetry.Observe(Sample(1,1)));
        Counts(telemetry,0,0,0);
    }
    TEST_METHOD(DuplicateOrBackwardSubmissionCannotReplaceOriginalIntent) {
        for(int mutation=0;mutation<3;++mutation) {
            AlphaOutputPresentationTelemetry telemetry;
            telemetry.RecordSubmission(Submission(2));telemetry.Observe(Sample(2,2));
            auto duplicate=Submission(mutation==0?1u:2u,mutation==1?50u:1u,mutation==2?120.0:60.0);
            Assert::IsFalse(telemetry.RecordSubmission(duplicate));
            Assert::IsFalse(telemetry.Observe(Sample(2,20)));
            Counts(telemetry,0,0,0);
            telemetry.RecordSubmission(Submission(3));telemetry.Observe(Sample(3,21));
            telemetry.RecordSubmission(Submission(4));telemetry.Observe(Sample(4,22));
            Counts(telemetry,0,0,1);
        }
    }
    TEST_METHOD(ExcludedIntentAndZeroIdsCannotReuseAnOldObservedId) {
        for(int mutation=0;mutation<3;++mutation) {
            AlphaOutputPresentationTelemetry telemetry;
            telemetry.RecordSubmission(Submission(1));telemetry.Observe(Sample(1,1));
            auto excluded=Submission(2);
            if(mutation==0)excluded.presentId=0;
            if(mutation==1)excluded.intendedMaximumRefreshes=0;
            if(mutation==2)excluded.expectedDisplayHz=0;
            Assert::IsFalse(telemetry.RecordSubmission(excluded));
            Assert::IsFalse(telemetry.Observe(Sample(1,20)));
            telemetry.RecordSubmission(Submission(2));telemetry.Observe(Sample(2,21));
            telemetry.RecordSubmission(Submission(3));telemetry.Observe(Sample(3,22));
            Counts(telemetry,0,0,1);
        }
    }
    TEST_METHOD(SmallQualifiedRateUpdatesRemainUsableAndDoNotMakeDuplicatePollsNew) {
        AlphaOutputPresentationTelemetry telemetry;
        telemetry.RecordSubmission(Submission(1,1,59.94987));
        auto first=Sample(1,1,59.94987);telemetry.Observe(first);
        first.expectedDisplayHz=59.951;
        Assert::IsFalse(telemetry.Observe(first));
        telemetry.RecordSubmission(Submission(2,1,59.951));
        auto second=Sample(2,2,59.95);second.expectedDisplayHz=59.951;
        Assert::IsTrue(telemetry.Observe(second));
        Counts(telemetry,0,0,1);
    }
    TEST_METHOD(ChangedEpochGenerationRateOrIntentCannotBridgeOldCoverage) {
        for(int mutation=0;mutation<4;++mutation) {
            AlphaOutputPresentationTelemetry telemetry;
            SeedLongHold(telemetry);
            auto next=Submission(3);
            if(mutation==0)next.generation=2;
            if(mutation==1)next.presentationEpoch=2;
            if(mutation==2)next.expectedDisplayHz=120;
            if(mutation==3)next.intendedMaximumRefreshes=3;
            Assert::IsTrue(telemetry.RecordSubmission(next));
            auto observed=Sample(3,100,next.expectedDisplayHz,next.generation);
            Assert::IsTrue(telemetry.Observe(observed));
            Counts(telemetry,1,2,1);
            next.presentId=4;telemetry.RecordSubmission(next);
            telemetry.Observe(Sample(4,101,next.expectedDisplayHz,next.generation));
            Counts(telemetry,1,2,2);
        }
    }
    TEST_METHOD(DisjointUnsupportedAndFailurePreserveCountsButRequireNewAnchor) {
        for(auto status:{AlphaPresentationTimingStatus::NoSwapchain,
            AlphaPresentationTimingStatus::FrameStatisticsUnavailable,
            AlphaPresentationTimingStatus::FrameStatisticsFailed,
            AlphaPresentationTimingStatus::Disjoint}) {
            AlphaOutputPresentationTelemetry telemetry;
            SeedLongHold(telemetry);
            auto failed=Sample(3,10);failed.timingStatus=status;
            failed.available=false;failed.disjoint=status==AlphaPresentationTimingStatus::Disjoint;
            failed.frameStatisticsResult=-123;
            Assert::IsFalse(telemetry.Observe(failed));
            const auto failure=telemetry.Snapshot();
            Assert::IsTrue(failure.timingStatus==status);
            Assert::AreEqual(int32_t(-123),failure.frameStatisticsResult);
            for(int poll=0;poll<8;++poll)Assert::IsFalse(telemetry.Observe(failed));
            Assert::AreEqual(failure.observationGaps,telemetry.Snapshot().observationGaps);
            Counts(telemetry,1,2,1);
            telemetry.RecordSubmission(Submission(4));telemetry.Observe(Sample(4,11));
            telemetry.RecordSubmission(Submission(5));telemetry.Observe(Sample(5,12));
            Counts(telemetry,1,2,2);
        }
    }
    TEST_METHOD(QueuedLateScanoutWithEarlierSyncClockRemainsCountable) {
        AlphaOutputPresentationTelemetry telemetry;
        telemetry.RecordSubmission(Submission(1));
        auto first=Sample(1,10);first.presentRefreshCount=20;
        Assert::IsTrue(telemetry.Observe(first));
        telemetry.RecordSubmission(Submission(2));
        auto late=Sample(2,11);late.presentRefreshCount=23;
        // SyncRefresh/QPC can describe submission while PresentRefresh names
        // the later actual scanout. They are not the same timestamp.
        Assert::IsTrue(telemetry.Observe(late));
        Counts(telemetry,1,2,1);
        Assert::IsTrue(telemetry.Snapshot().availability==AlphaOutputPresentationAvailability::Observing);
    }
    TEST_METHOD(ContradictoryQpcOrRefreshPointCannotSeedAPhantomLongHold) {
        for(int mutation=0;mutation<5;++mutation) {
            AlphaOutputPresentationTelemetry telemetry;
            telemetry.RecordSubmission(Submission(1));telemetry.Observe(Sample(1,10));
            telemetry.RecordSubmission(Submission(2));
            auto bad=Sample(2,11);
            if(mutation==0)bad.syncQpc=1; // backwards
            if(mutation==1)bad.syncRefreshCount=10;
            if(mutation==2)bad.presentRefreshCount=1;
            if(mutation==3)bad.qpcFrequency*=2;
            if(mutation==4)bad.syncQpc+=500000; // cadence contradicts qualified display
            Assert::IsFalse(telemetry.Observe(bad));
            telemetry.RecordSubmission(Submission(3));telemetry.Observe(Sample(3,100));
            telemetry.RecordSubmission(Submission(4));telemetry.Observe(Sample(4,101));
            Counts(telemetry,0,0,1);
        }
    }
    TEST_METHOD(RepeatedPresentWithAdvancingSyncIsNotARepeatCounter) {
        AlphaOutputPresentationTelemetry telemetry;
        telemetry.RecordSubmission(Submission(1));telemetry.Observe(Sample(1,1));
        auto changed=Sample(1,20);changed.presentRefreshCount=1;
        Assert::IsFalse(telemetry.Observe(changed));
        Counts(telemetry,0,0,0);
        telemetry.RecordSubmission(Submission(2));telemetry.Observe(Sample(2,21));
        telemetry.RecordSubmission(Submission(3));telemetry.Observe(Sample(3,22));
        Counts(telemetry,0,0,1);
    }
    TEST_METHOD(WrappedPresentAndRefreshCountersNeverCreateHugeIncidents) {
        AlphaOutputPresentationTelemetry telemetry;
        const auto maximum=(std::numeric_limits<uint32_t>::max)();
        telemetry.RecordSubmission(Submission(maximum));telemetry.Observe(Sample(maximum,maximum));
        Assert::IsFalse(telemetry.RecordSubmission(Submission(1)));
        Assert::IsFalse(telemetry.Observe(Sample(1,1)));
        Counts(telemetry,0,0,0);
        telemetry.ResetContinuity();
        telemetry.RecordSubmission(Submission(1));telemetry.Observe(Sample(1,maximum));
        telemetry.RecordSubmission(Submission(2));
        auto wrapped=Sample(2,1);wrapped.syncQpc=Sample(1,maximum).syncQpc+16667;
        Assert::IsFalse(telemetry.Observe(wrapped));
        Counts(telemetry,0,0,0);
    }
    TEST_METHOD(InvalidRawTimingCannotGrantCoverageFromAValidSubmission) {
        for(int mutation=0;mutation<6;++mutation) {
            AlphaOutputPresentationTelemetry telemetry;
            telemetry.RecordSubmission(Submission(1));telemetry.Observe(Sample(1,1));
            telemetry.RecordSubmission(Submission(2));
            auto bad=Sample(2,3);
            if(mutation==0)bad.available=false;
            if(mutation==1)bad.qpcFrequency=0;
            if(mutation==2)bad.syncQpc=0;
            if(mutation==3)bad.expectedDisplayHz=0;
            if(mutation==4)bad.expectedDisplayHz=120;
            if(mutation==5)bad.disjoint=true;
            Assert::IsFalse(telemetry.Observe(bad));
            Counts(telemetry,0,0,0);
            Assert::IsTrue(telemetry.Snapshot().availability!=AlphaOutputPresentationAvailability::Observing);
        }
    }
    TEST_METHOD(ContinuityResetKeepsSessionIncidentsAndSessionResetClearsThem) {
        AlphaOutputPresentationTelemetry telemetry;
        SeedLongHold(telemetry);
        telemetry.RecordSubmission(Submission(4));telemetry.Observe(Sample(4,6));
        const auto before=telemetry.Snapshot();
        Assert::IsTrue(before.observationGaps>0);
        telemetry.ResetContinuity();
        Counts(telemetry,1,2,1);
        Assert::AreEqual(before.observationGaps,telemetry.Snapshot().observationGaps);
        Assert::IsTrue(telemetry.Snapshot().availability==AlphaOutputPresentationAvailability::Unavailable);
        Assert::IsFalse(telemetry.Observe(Sample(2,4)));
        Counts(telemetry,1,2,1);
        telemetry.RecordSubmission(Submission(1,1,60,2));telemetry.Observe(Sample(1,1,60,2));
        telemetry.RecordSubmission(Submission(2,1,60,2));telemetry.Observe(Sample(2,2,60,2));
        Counts(telemetry,1,2,2);
        telemetry.ResetSession();
        Counts(telemetry,0,0,0);
        Assert::AreEqual(uint64_t(0),telemetry.Snapshot().observationGaps);
        Assert::IsTrue(telemetry.Snapshot().availability==AlphaOutputPresentationAvailability::Unavailable);
    }
};
}
