#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/OutputPresentationPublication.h>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests {
TEST_CLASS(OutputPresentationPublicationTests) {
public:
    TEST_METHOD(ResetRequestCoalescesMonotonicallyWithoutReapplyingOldSerials) {
        OutputPresentationResetRequest request;
        Assert::AreEqual(uint64_t(0),request.PendingAfter(0));
        request.Request(10);request.Request(0);request.Request(7);request.Request(10);
        Assert::AreEqual(uint64_t(10),request.PendingAfter(0));
        Assert::AreEqual(uint64_t(0),request.PendingAfter(10));
        request.Request(12);request.Request(11);
        Assert::AreEqual(uint64_t(12),request.PendingAfter(10));
        Assert::AreEqual(uint64_t(0),request.PendingAfter(12));
    }
    TEST_METHOD(ResetAcknowledgementInvalidatesOldPayloadWithoutWaitingForPublication) {
        OutputPresentationPublication publication;
        AlphaOutputPresentationSnapshot old;
        old.availability=AlphaOutputPresentationAvailability::Observing;
        old.timingStatus=AlphaPresentationTimingStatus::Available;
        old.observedLongHolds=3;old.excessRefreshes=7;old.verifiedIntervals=100;
        publication.Publish(old,100);
        publication.AcknowledgeReset(4); // No successful payload Publish is required.
        auto result=publication.Snapshot(101);
        Assert::IsTrue(result.supported);
        Assert::IsTrue(result.state==RendererOutputPresentationState::Unavailable);
        Assert::IsTrue(result.reason==RendererOutputPresentationReason::NoSwapchain);
        Assert::AreEqual(uint64_t(4),result.appliedResetSerial);
        Assert::AreEqual(uint64_t(0),result.observedLongHolds);
        Assert::AreEqual(uint64_t(0),result.excessRefreshes);
        Assert::AreEqual(uint64_t(0),result.verifiedIntervals);
        publication.Publish(old,102,3); // Delayed pre-reset publication cannot restore totals.
        publication.AcknowledgeReset(2);
        result=publication.Snapshot(103);
        Assert::AreEqual(uint64_t(4),result.appliedResetSerial);
        Assert::AreEqual(uint64_t(0),result.observedLongHolds);
    }
    TEST_METHOD(FreshEvidenceAndContinuityLossKeepTheAppliedResetAcknowledgement) {
        OutputPresentationPublication publication;
        publication.AcknowledgeReset(9);
        AlphaOutputPresentationSnapshot current;
        current.availability=AlphaOutputPresentationAvailability::Observing;
        current.timingStatus=AlphaPresentationTimingStatus::Available;
        current.observedLongHolds=1;current.excessRefreshes=2;current.verifiedIntervals=4;
        publication.Publish(current,100,9);
        auto result=publication.Snapshot(101);
        Assert::AreEqual(uint64_t(9),result.appliedResetSerial);
        Assert::AreEqual(uint64_t(1),result.observedLongHolds);
        Assert::IsTrue(result.state==RendererOutputPresentationState::Observing);
        current.availability=AlphaOutputPresentationAvailability::Unavailable;
        current.timingStatus=AlphaPresentationTimingStatus::NoSwapchain;
        publication.Publish(current,0,9);
        result=publication.Snapshot(10000);
        Assert::AreEqual(uint64_t(9),result.appliedResetSerial);
        Assert::AreEqual(uint64_t(1),result.observedLongHolds);
        Assert::IsTrue(result.reason==RendererOutputPresentationReason::NoSwapchain);
        publication.AcknowledgeReset(10);
        result=publication.Snapshot(10001);
        Assert::AreEqual(uint64_t(10),result.appliedResetSerial);
        Assert::AreEqual(uint64_t(0),result.observedLongHolds);
    }
    TEST_METHOD(NoEvidenceIsNotAZeroDropResult) {
        OutputPresentationPublication publication;
        Assert::IsFalse(publication.Snapshot(100).supported);
        AlphaOutputPresentationSnapshot observed;
        publication.Publish(observed, 0);
        const auto result = publication.Snapshot(100);
        Assert::IsTrue(result.supported);
        Assert::IsTrue(result.state == RendererOutputPresentationState::Unavailable);
        Assert::IsTrue(result.reason == RendererOutputPresentationReason::NoSwapchain);
    }
    TEST_METHOD(StalePollingPreservesKnownIncidentsWithoutClaimingCurrentCoverage) {
        OutputPresentationPublication publication;
        AlphaOutputPresentationSnapshot observed;
        observed.availability = AlphaOutputPresentationAvailability::Observing;
        observed.timingStatus = AlphaPresentationTimingStatus::Available;
        observed.observedLongHolds = 2; observed.excessRefreshes = 3; observed.verifiedIntervals = 50;
        publication.Publish(observed, 100);
        Assert::IsTrue(publication.Snapshot(1100).state == RendererOutputPresentationState::Observing);
        const auto stale = publication.Snapshot(1101);
        Assert::IsTrue(stale.reason == RendererOutputPresentationReason::Stale);
        Assert::AreEqual(uint64_t(2), stale.observedLongHolds);
        Assert::AreEqual(uint64_t(3), stale.excessRefreshes);
        Assert::AreEqual(uint64_t(50), stale.verifiedIntervals);
        publication.Publish(observed, 100); // Duplicate poll cannot refresh witness age.
        Assert::IsTrue(publication.Snapshot(1200).reason == RendererOutputPresentationReason::Stale);
        publication.Publish(observed, 1200);
        Assert::IsTrue(publication.Snapshot(1201).state == RendererOutputPresentationState::Observing);
    }
    TEST_METHOD(UnsupportedAndDisjointReasonsSurviveStaleClockWithoutLosingTotals) {
        OutputPresentationPublication publication;
        AlphaOutputPresentationSnapshot observed;
        observed.availability = AlphaOutputPresentationAvailability::Unavailable;
        observed.observedLongHolds = 1; observed.excessRefreshes = 4; observed.verifiedIntervals = 40;
        observed.timingStatus = AlphaPresentationTimingStatus::FrameStatisticsUnavailable;
        publication.Publish(observed, 1);
        auto result = publication.Snapshot(10000);
        Assert::IsTrue(result.reason == RendererOutputPresentationReason::Unsupported);
        Assert::AreEqual(uint64_t(1), result.observedLongHolds);
        observed.timingStatus = AlphaPresentationTimingStatus::Disjoint;
        publication.Publish(observed, 1);
        result = publication.Snapshot(10000);
        Assert::IsTrue(result.reason == RendererOutputPresentationReason::Disjoint);
        Assert::AreEqual(uint64_t(4), result.excessRefreshes);
    }
};
}
