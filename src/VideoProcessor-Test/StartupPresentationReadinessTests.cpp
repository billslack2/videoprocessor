#include "pch.h"
#include "CppUnitTest.h"
#include <StartupPresentationReadiness.h>
#include <RendererTransitionModel.h>
#include <vprenderer/LibplaceboRendererPluginApi.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace StartupPresentation;

namespace
{
    SubmissionInput Submitted(bool native = false)
    {
        SubmissionInput input;
        input.context = {7,3840,2160,11,19,23,29};
        input.selectedSequence = 100; input.tick = 1000;
        input.nativeBounds = {0,278,3840,1880};
        input.displayedBounds = {38,278,3802,1880};
        input.rendered = input.submitted = input.contextCurrent = input.poseQualified = true;
        input.nativeAuthorityConfirmed = native;
        return input;
    }
    RevealInput Query(const SnapshotV1& snapshot, uint64_t tick = 1000)
    {
        RevealInput input;
        input.rendererGeneration = 7; input.now = tick; input.existingFrameReady = true;
        input.queryStatus = QueryStatus::Available; input.snapshot = snapshot;
        return input;
    }
    RevealDecision Settle(RevealGate& gate,SnapshotV1 snapshot)
    {
        auto result=gate.Evaluate(Query(snapshot,snapshot.submittedTick));
        for (int i=0;i<2;++i) {
            ++snapshot.selectedSequence;++snapshot.submissionSerial;snapshot.submittedTick+=125;
            result=gate.Evaluate(Query(snapshot,snapshot.submittedTick));
        }
        return result;
    }
    uint32_t __cdecl BusyQuery(IVideoRenderer*, SnapshotV1*)
    {
        return static_cast<uint32_t>(QueryStatus::Busy);
    }
    uint32_t __cdecl FutureQuery(IVideoRenderer*, SnapshotV1* result)
    {
        result->version = SnapshotVersion + 1;
        return static_cast<uint32_t>(QueryStatus::Available);
    }
}

namespace VideoProcessorTest
{
    TEST_CLASS(StartupPresentationReadinessTests)
    {
    public:
        TEST_METHOD(StartupWaitsForConsistentSubmittedShapeThroughInitialFlashes)
        {
            auto input=Submitted(true);
            RevealGate gate;gate.Arm(7);
            auto snapshot=AfterSubmission({},input);
            Assert::IsFalse(gate.Evaluate(Query(snapshot,input.tick)).release);
            // One apparent Scope, then fallback, then another short Scope.
            input.tick+=42;++input.selectedSequence;input.poseQualified=false;
            snapshot=AfterSubmission(snapshot,input);
            Assert::IsFalse(gate.Evaluate(Query(snapshot,input.tick)).release);
            input.tick+=42;++input.selectedSequence;input.poseQualified=true;
            snapshot=AfterSubmission(snapshot,input);
            Assert::IsFalse(gate.Evaluate(Query(snapshot,input.tick)).release);
            input.tick+=200;++input.selectedSequence;
            snapshot=AfterSubmission(snapshot,input);
            Assert::IsFalse(gate.Evaluate(Query(snapshot,input.tick)).release);
            input.tick+=50;++input.selectedSequence;
            snapshot=AfterSubmission(snapshot,input);
            Assert::IsTrue(gate.Evaluate(Query(snapshot,input.tick)).release);
        }
        TEST_METHOD(ReadySnapshotOrRepeatedSourceCannotAgeIntoStableStartupShape)
        {
            auto input=Submitted(true);
            RevealGate gate;gate.Arm(7);
            auto snapshot=AfterSubmission({},input);
            Assert::IsFalse(gate.Evaluate(Query(snapshot,1000)).release);
            Assert::IsFalse(gate.Evaluate(Query(snapshot,2000)).release);
            input.tick=2000;snapshot=AfterSubmission(snapshot,input);
            Assert::IsFalse(gate.Evaluate(Query(snapshot,2000)).release);
            Assert::IsTrue(gate.Evaluate(Query(snapshot,4000)).timedOut);
        }
        TEST_METHOD(SubmittedPoseAndContextChangesRestartOnlyTheStartupStabilityDwell)
        {
            for(int change=0;change<4;++change) {
                RevealGate gate;gate.Arm(7);auto input=Submitted(true);
                auto snapshot=AfterSubmission({},input);
                Assert::IsFalse(gate.Evaluate(Query(snapshot,input.tick)).release);
                input.tick+=200;++input.selectedSequence;
                snapshot=AfterSubmission(snapshot,input);
                Assert::IsFalse(gate.Evaluate(Query(snapshot,input.tick)).release);
                if(change==0)input.displayedBounds.top+=2;
                if(change==1)input.nativeBounds.bottom-=2;
                if(change==2)++input.context.viewportEpoch;
                if(change==3)++input.context.sourceGeneration;
                input.tick+=50;++input.selectedSequence;
                snapshot=AfterSubmission(snapshot,input);
                Assert::IsFalse(gate.Evaluate(Query(snapshot,input.tick)).release);
                Assert::IsTrue(Settle(gate,snapshot).release);
                Assert::AreEqual(uint64_t(1000),snapshot.firstSubmittedTick);
            }
        }
        TEST_METHOD(ProposalsAndFailedSubmissionCannotReleaseStartupCover)
        {
            const SnapshotV1 initial;
            for (int variant = 0; variant < 3; ++variant) {
                auto input = Submitted(true);
                if (variant == 0) input.rendered = false;
                if (variant == 1) input.submitted = false;
                if (variant == 2) input.contextCurrent = false;
                const auto snapshot = AfterSubmission(initial,input);
                Assert::IsFalse((snapshot.flags & (Ready | ActualSubmission)) != 0);
                RevealGate gate; gate.Arm(7);
                Assert::IsFalse(gate.Evaluate(Query(snapshot,10000)).release);
            }
        }
        TEST_METHOD(AnyConfirmedNativeAspectAndBarAxisCanRevealAfterSettling)
        {
            const Rect formats[] = {{0,278,3840,1880},{0,120,3840,2040},
                {0,384,3840,1776},{480,0,3360,2160},{480,120,3360,2040}};
            for (const auto& bounds : formats) {
                auto input = Submitted(true); input.nativeBounds = input.displayedBounds = bounds;
                const auto snapshot = AfterSubmission({},input);
                Assert::IsTrue((snapshot.flags & Ready) != 0);
                Assert::AreEqual(unsigned(Reason::ReadyNative),unsigned(snapshot.reason));
                RevealGate gate; gate.Arm(7);
                const auto decision = Settle(gate,snapshot);
                Assert::IsTrue(decision.release); Assert::IsFalse(decision.timedOut);
            }
        }
        TEST_METHOD(IndependentFullRasterIsReadyButConservativeFallbackIsNotAuthority)
        {
            auto input = Submitted(); input.nativeBounds = input.displayedBounds = {0,0,3840,2160};
            auto fallback = AfterSubmission({},input);
            Assert::IsFalse((fallback.flags & Ready) != 0);
            input.fullRasterAuthorityConfirmed = true;
            const auto confirmed = AfterSubmission(fallback,input);
            Assert::IsTrue((confirmed.flags & Ready) != 0);
            Assert::AreEqual(unsigned(Reason::ReadyFullRaster),unsigned(confirmed.reason));
        }
        TEST_METHOD(SelectedOwnerMustBeQualifiedButNoCaptionIsRequired)
        {
            auto input = Submitted(true); input.poseQualified = false;
            auto pending = AfterSubmission({},input);
            Assert::AreEqual(unsigned(Reason::AwaitingOwner),unsigned(pending.reason));
            RevealGate gate; gate.Arm(7);
            Assert::IsFalse(gate.Evaluate(Query(pending)).release);
            input.poseQualified = true; ++input.selectedSequence; input.tick += 42;
            const auto ready = AfterSubmission(pending,input);
            Assert::IsTrue(Settle(gate,ready).release);
        }
        TEST_METHOD(RepeatedPausedInputDoesNotInventGeometryAuthorityAndHasBoundedWait)
        {
            auto input = Submitted();
            auto snapshot = AfterSubmission({},input);
            RevealGate gate; gate.Arm(7);
            Assert::IsFalse(gate.Evaluate(Query(snapshot)).release);
            for (uint64_t tick = 1100; tick < 4000; tick += 100) {
                input.tick = tick; // Same selected input, no new authority proof.
                snapshot = AfterSubmission(snapshot,input);
                Assert::IsFalse((snapshot.flags & Ready) != 0);
                Assert::IsFalse(gate.Evaluate(Query(snapshot,tick)).release);
            }
            const auto expired = gate.Evaluate(Query(snapshot,4000));
            Assert::IsTrue(expired.release && expired.timedOut);
            Assert::AreEqual(uint64_t(3000),expired.elapsedMs);
        }
        TEST_METHOD(BlackSubmittedViewTimesOutButNoSignalCannotExposeRetiredSurface)
        {
            auto input = Submitted(); input.displayedBounds = {0,0,3840,2160};
            const auto black = AfterSubmission({},input);
            RevealGate gate; gate.Arm(7);
            Assert::IsFalse(gate.Evaluate(Query(black,3999)).release);
            Assert::IsTrue(gate.Evaluate(Query(black,4000)).timedOut);
            RevealGate noSignal; noSignal.Arm(7);
            auto empty = Reset(input.context);
            Assert::IsFalse(noSignal.Evaluate(Query(empty,10000)).release);
        }
        TEST_METHOD(ProfileAndTargetPrerequisitesCannotBeBypassedByOldReadySnapshot)
        {
            auto input = Submitted(true);
            const auto ready = AfterSubmission({},input);
            RevealGate gate; gate.Arm(7);
            auto query = Query(ready); query.existingFrameReady = false;
            Assert::IsFalse(gate.Evaluate(query).release); // Existing HWND/target binding rejected.
            query.existingFrameReady = true; query.rendererGeneration = 8;
            Assert::IsFalse(gate.Evaluate(query).release);
            query = Query(ready); query.snapshot.rendererGeneration = 6;
            Assert::IsFalse(gate.Evaluate(query).release);
            auto changed = input.context; ++changed.viewportEpoch;
            const auto pending = Reset(changed,true,ready);
            Assert::IsFalse(gate.Evaluate(Query(pending,10000)).release);
            Assert::AreEqual(ready.firstSubmittedTick,pending.firstSubmittedTick);
        }
        TEST_METHOD(SourceFormatViewportAndOutputChangesClearUnsubmittedReadiness)
        {
            const auto input = Submitted(true);
            const auto ready = AfterSubmission({},input);
            for (int change = 0; change < 5; ++change) {
                auto context = input.context;
                if (change == 0) ++context.sourceGeneration;
                if (change == 1) ++context.sourceFormatKey;
                if (change == 2) ++context.viewportEpoch;
                if (change == 3) ++context.outputContract;
                if (change == 4) context.rasterWidth = 1920;
                const auto changed = Reset(context,true,ready);
                Assert::IsFalse((changed.flags & (Ready | ActualSubmission)) != 0);
                Assert::AreEqual(ready.firstSubmittedTick,changed.firstSubmittedTick);
            }
        }
        TEST_METHOD(FullscreenProfileAndResetRetriesDoNotRestartGeometryDeadline)
        {
            auto input = Submitted(); auto snapshot = AfterSubmission({},input);
            RevealGate gate; gate.Arm(7);
            Assert::IsFalse(gate.Evaluate(Query(snapshot)).release);
            gate.Arm(7); // Idempotent even if a host retries while initially covered.
            auto context = input.context; ++context.viewportEpoch;
            snapshot = Reset(context,true,snapshot);
            input.context = context; input.selectedSequence = 101; input.tick = 3500;
            snapshot = AfterSubmission(snapshot,input);
            Assert::IsFalse(gate.Evaluate(Query(snapshot,3999)).release);
            // A covered renderer rebuild must provide a new current submission,
            // but cannot restart the GUI-owned original first-submit deadline.
            gate.Arm(8);
            auto stale = Query(snapshot,4000); stale.rendererGeneration = 8;
            Assert::IsFalse(gate.Evaluate(stale).release);
            input.context.rendererGeneration = 8; input.tick = 4000;
            snapshot = AfterSubmission(snapshot,input);
            auto current = Query(snapshot,4000); current.rendererGeneration = 8;
            Assert::IsTrue(gate.Evaluate(current).timedOut);
        }
        TEST_METHOD(ManualFixedOrAutomaticCropOffBypassesOnlyGeometryWait)
        {
            auto input = Submitted(); input.required = false;
            const auto snapshot = AfterSubmission({},input);
            RevealGate gate; gate.Arm(7);
            auto query = Query(snapshot); query.existingFrameReady = false;
            Assert::IsFalse(gate.Evaluate(query).release);
            query.existingFrameReady = true;
            const auto result = gate.Evaluate(query);
            Assert::IsTrue(result.release && !result.timedOut);
            Assert::AreEqual(unsigned(RevealReason::NotRequired),unsigned(result.reason));
        }
        TEST_METHOD(SceneCutsAndSubtitlesNeverRearmAnAlreadyRevealedRenderer)
        {
            auto input = Submitted(true); auto snapshot = AfterSubmission({},input);
            RevealGate gate; gate.Arm(7);
            Assert::IsTrue(Settle(gate,snapshot).release); gate.MarkRevealed();
            gate.Arm(7); // Same renderer cannot re-cover merely for later scene/caption changes.
            auto changed = input.context; ++changed.sourceGeneration;
            snapshot = Reset(changed,true,snapshot);
            Assert::IsTrue(gate.Evaluate(Query(snapshot,2000)).release);
            gate.Arm(8); auto next = Query(snapshot,2000); next.rendererGeneration = 8;
            Assert::IsTrue(gate.Evaluate(next).release); // Renderer-only replacement stays revealed.
            gate.BeginSession(); gate.Arm(8);
            Assert::IsFalse(gate.Evaluate(next).release); // New capture requires its own selected submission.
        }
        TEST_METHOD(MissingOldPluginExportBypassesWhileBusyExportKeepsWaiting)
        {
            SnapshotV1 snapshot;
            auto* unused = reinterpret_cast<IVideoRenderer*>(uintptr_t(1));
            Assert::AreEqual(unsigned(QueryStatus::Unsupported),unsigned(QueryOptionalStartupReadiness(nullptr,unused,snapshot)));
            RevealGate gate; gate.Arm(7);
            auto query = Query(snapshot); query.queryStatus = QueryStatus::Unsupported;
            Assert::IsTrue(gate.Evaluate(query).release);
            query.queryStatus = QueryOptionalStartupReadiness(BusyQuery,unused,snapshot);
            Assert::AreEqual(unsigned(QueryStatus::Busy),unsigned(query.queryStatus));
            Assert::IsFalse(gate.Evaluate(query).release);
            // Busy never proves a new/current source surface, even at expiry.
            query.now = 10000;
            Assert::IsFalse(gate.Evaluate(query).release);
            auto submitted = AfterSubmission({},Submitted());
            query = Query(submitted); Assert::IsFalse(gate.Evaluate(query).release);
            query.queryStatus = QueryStatus::Busy; query.now = 10000;
            Assert::IsFalse(gate.Evaluate(query).release);
            Assert::AreEqual(unsigned(QueryStatus::Unsupported),unsigned(QueryOptionalStartupReadiness(FutureQuery,unused,snapshot)));
            Assert::AreEqual(uint32_t(30),VP_LIBPLACEBO_PLUGIN_API_VERSION);
        }
        TEST_METHOD(WaitingGeometryDoesNotAdvanceCoordinatedShieldReleaseState)
        {
            RendererTransitionModel transition;
            transition.BeginReset(7,1);
            const auto key = transition.Key();
            transition.OnShieldAcquired(key,true);
            transition.OnResetStarted(key);
            transition.OnResetCompleted(key,true);
            Assert::AreEqual(unsigned(RendererTransitionState::AwaitingFrame),unsigned(transition.State()));
            RevealGate gate; gate.Arm(7);
            auto input = Submitted(); auto pending = AfterSubmission({},input);
            const auto waiting = gate.Evaluate(Query(pending));
            Assert::IsFalse(waiting.release);
            Assert::AreEqual(unsigned(RendererTransitionState::AwaitingFrame),unsigned(transition.State()));
            input.nativeAuthorityConfirmed = true;
            const auto ready = AfterSubmission(pending,input);
            Assert::IsTrue(Settle(gate,ready).release);
            const auto release = transition.OnFrameReady(key);
            Assert::IsFalse(release.empty());
            Assert::AreEqual(unsigned(RendererTransitionActionType::ReleaseShield),unsigned(release.front().type));
        }
    };
}
