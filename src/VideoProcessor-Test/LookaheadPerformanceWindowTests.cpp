#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/LookaheadPerformanceWindow.h>
#include <limits>
#include <type_traits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    namespace
    {
        using Window = LookaheadPerformanceWindow;
        using Stage = Window::Stage;
        using Outcome = Window::WorkOutcome;
        using Stop = Window::StopReason;
        using Proof = Window::ProofKind;
        using Reject = Window::Rejection;
        using Async = Window::AsyncOutcome;
        template <typename T> size_t Index(T value) { return static_cast<size_t>(value); }
        void Equal(uint64_t expected, uint64_t actual) { Assert::AreEqual(expected, actual); }
    }

    TEST_CLASS(LookaheadPerformanceWindowTests)
    {
    public:
        TEST_METHOD(CachedProofCanBeUsedWithoutNewWorkAdmission)
        {
            Window window;
            window.ObserveDepth(5, 3, 3);
            window.RecordCache(Stage::ExpansionProof, true, false);
            window.RecordPublished(Proof::BufferedExpansion);
            window.RecordAdopted(Proof::BufferedExpansion, 2, 83.416);
            const auto& stage = window.stages[Index(Stage::ExpansionProof)];
            Equal(1, stage.cacheHits);
            Equal(1, stage.zeroAdmissionHits);
            Equal(0, stage.workCalls);
            Equal(0, stage.elapsedMs.count);
            const auto& proof = window.proofs[Index(Proof::BufferedExpansion)];
            Equal(1, proof.published);
            Equal(1, proof.adopted);
            Assert::AreEqual(2.0, proof.effectiveLeadFrames.Average(), 0.000001);
            Assert::AreEqual(83.416, proof.effectiveLeadMs.Average(), 0.000001);
        }

        TEST_METHOD(ReducedReadyPrefixAndInsufficientProofDepthAreDifferent)
        {
            Window window;
            window.ObserveDepth(6, 3, 3); // Short prefix still satisfies this proof.
            window.ObserveDepth(6, 2, 3); // This proof cannot finish from two frames.
            window.ObserveDepth(6, 0, 3);
            Equal(3, window.depth.samples);
            Equal(18, window.depth.selectedTotal);
            Equal(5, window.depth.readyTotal);
            Equal(9, window.depth.requiredTotal);
            Equal(3, window.depth.readyShortfall);
            Equal(2, window.depth.requiredShortfall);
            Equal(1, window.depth.noReady);
            Equal(3, window.depth.readyPeak);
            Equal(0, window.proofs[Index(Proof::BufferedExpansion)].published);
            // A caller error must remain visible rather than look like spare input.
            window.ObserveDepth(2, 3, 3);
            Equal(1, window.depth.invalid);
            Equal(5, window.depth.readyTotal);
        }

        TEST_METHOD(CompletedInvalidAsyncResultIsNotLateOrUsefulProof)
        {
            Window window;
            window.RecordAsync(Proof::BufferedExpansion, Async::Invalid);
            window.RecordAsync(Proof::BufferedExpansion, Async::Invalid);
            window.RecordAsync(Proof::BufferedExpansion, Async::NoChange);
            window.RecordAsync(Proof::BufferedExpansion, Async::Late);
            const auto& proof = window.proofs[Index(Proof::BufferedExpansion)];
            Equal(2, proof.async[Index(Async::Invalid)]);
            Equal(1, proof.async[Index(Async::NoChange)]);
            Equal(1, proof.async[Index(Async::Late)]);
            Equal(0, proof.async[Index(Async::Ready)]);
            Equal(0, proof.published);
            Equal(0, proof.adopted);
            Equal(0, proof.rejected); // A completed invalid job was never published.
        }

        TEST_METHOD(CancelledPrefixRetainsIndependentWorkWithoutPublishingAuthority)
        {
            Window window;
            window.RecordWork(Stage::Measurements, Outcome::Complete, 0.4);
            window.RecordWork(Stage::Retention, Outcome::Cancelled, 1.7, Stop::Deadline);
            window.RecordRetainedMeasurements(3, true);
            Equal(3, window.independentRetained);
            Equal(3, window.independentRetainedAfterCancellation);
            const auto& retention = window.stages[Index(Stage::Retention)];
            Equal(1, retention.cancelled);
            Equal(1, retention.stops[Index(Stop::Deadline)]);
            Assert::AreEqual(1.7, retention.elapsedMs.total, 0.000001);
            Equal(0, window.proofs[Index(Proof::BufferedExpansion)].published);
            Equal(0, window.proofs[Index(Proof::BufferedExpansion)].adopted);
            // A later admitted attempt can reuse the retained measurements.
            window.RecordCache(Stage::Measurements, true, true);
            window.RecordWork(Stage::Retention, Outcome::Complete, 0.3);
            window.RecordPublished(Proof::BufferedExpansion);
            Equal(1, retention.cancelled);
            Equal(1, retention.completed);
            Equal(3, window.independentRetainedAfterCancellation);
            Equal(1, window.proofs[Index(Proof::BufferedExpansion)].published);
            Equal(0, window.proofs[Index(Proof::BufferedExpansion)].adopted);
        }

        TEST_METHOD(CompletedPublishedRejectedAndAdoptedAreIndependentEvents)
        {
            Window window;
            window.RecordWork(Stage::ExpansionProof, Outcome::Complete, 0.2);
            const auto& proof = window.proofs[Index(Proof::ScheduledDecision)];
            Equal(0, proof.published);
            window.RecordPublished(Proof::ScheduledDecision);
            Equal(0, proof.adopted);
            window.RecordRejected(Proof::ScheduledDecision, Reject::ReferenceMismatch);
            Equal(1, proof.published);
            Equal(1, proof.rejected);
            Equal(1, proof.rejections[Index(Reject::ReferenceMismatch)]);
            Equal(0, proof.adopted);
            // A separately accepted proof is observable without erasing rejection.
            window.RecordAdopted(Proof::BufferedExpansion);
            Equal(0, proof.adopted);
            Equal(1, window.proofs[Index(Proof::BufferedExpansion)].adopted);
        }

        TEST_METHOD(NoAdmissionAndInputNotReadyDoNotFabricateEvaluationCost)
        {
            Window window;
            window.RecordCache(Stage::Retention, false, false);
            window.RecordStop(Stage::Retention, Stop::ZeroAdmission);
            window.RecordStop(Stage::Measurements, Stop::InputNotReady);
            const auto& retention = window.stages[Index(Stage::Retention)];
            Equal(1, retention.cacheMisses);
            Equal(0, retention.zeroAdmissionHits);
            Equal(0, retention.workCalls);
            Equal(0, retention.elapsedMs.count);
            Equal(1, retention.stops[Index(Stop::ZeroAdmission)]);
            Equal(0, retention.stops[Index(Stop::InputNotReady)]);
            Equal(1, window.stages[Index(Stage::Measurements)].stops[Index(Stop::InputNotReady)]);
            window.RecordWork(Stage::InwardCaption, Outcome::Deferred, 0.05, Stop::QueueLock);
            Equal(1, window.stages[Index(Stage::InwardCaption)].deferred);
            Equal(1, window.stages[Index(Stage::InwardCaption)].stops[Index(Stop::QueueLock)]);
            Equal(0, window.stages[Index(Stage::InwardCaption)].cancelled);
        }

        TEST_METHOD(LeadCanBeLateAndUnmeasuredLeadDoesNotBecomeZero)
        {
            Window window;
            window.RecordAdopted(Proof::InwardConfirmation, 1, 41.708);
            window.RecordAdopted(Proof::InwardConfirmation, -1, -41.708);
            window.RecordAdopted(Proof::InwardConfirmation); // Lead unavailable.
            const auto& proof = window.proofs[Index(Proof::InwardConfirmation)];
            Equal(3, proof.adopted);
            Equal(2, proof.effectiveLeadFrames.count);
            Equal(2, proof.effectiveLeadMs.count);
            Assert::AreEqual(-1.0, proof.effectiveLeadFrames.minimum);
            Assert::AreEqual(1.0, proof.effectiveLeadFrames.maximum);
            Assert::AreEqual(0.0, proof.effectiveLeadMs.Average(), 0.000001);
        }

        TEST_METHOD(InvalidCostSamplesCannotPoisonPerformanceAverages)
        {
            Window window;
            window.RecordWork(Stage::Retention, Outcome::Complete, 0.25);
            window.RecordWork(Stage::Retention, Outcome::Cancelled,
                (std::numeric_limits<double>::quiet_NaN)(), Stop::Deadline);
            window.RecordWork(Stage::Retention, Outcome::Complete,
                (std::numeric_limits<double>::infinity)());
            window.RecordWork(Stage::Retention, Outcome::Deferred, -1, Stop::Other);
            const auto& stage = window.stages[Index(Stage::Retention)];
            Equal(4, stage.workCalls);
            Equal(1, stage.elapsedMs.count);
            Equal(3, stage.invalidElapsedSamples);
            Equal(3, window.invalidEvents);
            Assert::AreEqual(0.25, stage.elapsedMs.Average());
            Assert::AreEqual(0.25, stage.elapsedMs.maximum);
            Equal(1, stage.cancelled);
        }

        TEST_METHOD(StorageIsFixedAndInvalidEnumsCannotIndexPastCounters)
        {
            static_assert(std::is_trivially_copyable<Window>::value,
                "Telemetry snapshots must copy without owner-managed allocation");
            static_assert(sizeof(Window) < 4096, "Keep the rendering-owner window small");
            Window window;
            const auto invalidStage = static_cast<Stage>(255);
            const auto invalidProof = static_cast<Proof>(255);
            window.RecordCache(invalidStage, true, true);
            window.RecordWork(Stage::Retention, static_cast<Outcome>(255), 1);
            window.RecordStop(Stage::Measurements, static_cast<Stop>(255));
            window.RecordPublished(invalidProof);
            window.RecordRejected(Proof::GuardedReturn, static_cast<Reject>(255));
            window.RecordAsync(Proof::GuardedReturn, static_cast<Async>(255));
            Equal(6, window.invalidEvents);
            Equal(0, window.stages[Index(Stage::Retention)].workCalls);
            Equal(0, window.proofs[Index(Proof::GuardedReturn)].rejected);
            for (unsigned i = 0; i < 10000; ++i)
                window.RecordCache(Stage::Measurements, true, true);
            Equal(10000, window.stages[Index(Stage::Measurements)].cacheHits);
            Equal(0, window.depth.samples);
        }

        TEST_METHOD(ResetAllowsIndependentWindowsWithoutChangingCopiedEvidence)
        {
            Window window;
            window.ObserveDepth(5, 3, 3);
            window.RecordWork(Stage::RememberedProof, Outcome::Complete, 0.8);
            window.RecordRetainedMeasurements(3, false);
            window.RecordPublished(Proof::GuardedReturn);
            window.RecordAdopted(Proof::GuardedReturn, 2, 83.416);
            const Window snapshot = window;
            window.Reset();
            Equal(0, window.depth.samples);
            Equal(0, window.independentRetained);
            Equal(0, window.proofs[Index(Proof::GuardedReturn)].published);
            Equal(0, window.stages[Index(Stage::RememberedProof)].elapsedMs.count);
            Equal(1, snapshot.depth.samples);
            Equal(3, snapshot.independentRetained);
            Equal(1, snapshot.proofs[Index(Proof::GuardedReturn)].adopted);
            Assert::AreEqual(0.8, snapshot.stages[Index(Stage::RememberedProof)].elapsedMs.Average());
            window.ObserveDepth(4, 4, 2);
            Equal(4, window.depth.readyTotal);
            Equal(3, snapshot.depth.readyTotal);
        }
    };
}
