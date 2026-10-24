#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/OutputPresentationPublication.h>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests {
TEST_CLASS(OutputPresentationPublicationTests) {
public:
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
