#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/PassiveDisplayStatistics.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    using namespace PassiveDisplayStatistics;

    OutputIntent MakeIntent(uint64_t serial, int64_t startQpc, double pts,
        double outputHz = 60.0, double displayHz = 60.0, uint64_t resetEpoch = 0)
    {
        OutputIntent intent{};
        intent.processId = 42;
        intent.threadId = 7;
        intent.swapchainIdentity = static_cast<uintptr_t>(0x1234);
        intent.swapchainLifetime = 1;
        intent.rendererEpoch = 3;
        intent.sourceEpoch = 5;
        intent.viewportEpoch = 9;
        intent.resetEpoch = resetEpoch;
        intent.outputSerial = serial;
        intent.submitStartQpc = startQpc;
        intent.submitEndQpc = startQpc + 40;
        intent.qpcFrequency = 60000;
        intent.plannedOutputPts = pts;
        intent.scheduledOutputHz = outputHz;
        intent.displayHz = displayHz;
        intent.intentionalCorrection = false;
        intent.renderFailed = false;
        return intent;
    }

    DisplayEvent MakeEvent(const OutputIntent& intent, int64_t screenQpc,
        Disposition disposition = Disposition::Displayed)
    {
        DisplayEvent event{};
        event.processId = intent.processId;
        event.threadId = intent.threadId;
        event.swapchainIdentity = intent.swapchainIdentity;
        event.presentStartQpc = intent.submitStartQpc + 10;
        event.screenQpc = screenQpc;
        event.qpcFrequency = intent.qpcFrequency;
        event.disposition = disposition;
        event.presentMode = 1;
        event.displayHz = intent.displayHz;
        event.displayRateValid = true;
        return event;
    }

    void Record(Meter& meter, const OutputIntent& intent, const DisplayEvent& event)
    {
        Assert::IsTrue(meter.SubmitIntent(intent));
        meter.ObserveEvent(event);
        meter.Drain();
    }
}

namespace Tests
{
    TEST_CLASS(PassiveDisplayStatisticsTests)
    {
    public:
        TEST_METHOD(HoldWitnessRetainsExactEvidenceAcrossGapsAndClearsOnReset)
        {
            Meter meter;
            const auto first=MakeIntent(1,1000,0.0);
            const auto second=MakeIntent(2,2000,1.0/60.0);
            Record(meter,first,MakeEvent(first,10000));
            Record(meter,second,MakeEvent(second,13000));
            auto snapshot=meter.ReadSnapshot();
            Assert::AreEqual(uint64_t(1),snapshot.holdIncidents);
            Assert::AreEqual(uint64_t(1),snapshot.lastHold.previousOutput);
            Assert::AreEqual(uint64_t(2),snapshot.lastHold.output);
            Assert::AreEqual(int64_t(10000),snapshot.lastHold.previousScreenQpc);
            Assert::AreEqual(int64_t(13000),snapshot.lastHold.screenQpc);
            Assert::AreEqual(uint64_t(1),snapshot.lastHold.expectedRefreshes);
            Assert::AreEqual(uint64_t(3),snapshot.lastHold.actualRefreshes);
            meter.ReportGap();
            Assert::AreEqual(uint64_t(2),meter.ReadSnapshot().lastHold.output);
            meter.RequestReset(1,second.submitEndQpc); meter.Drain();
            Assert::AreEqual(uint64_t(0),meter.ReadSnapshot().lastHold.output);
            Assert::AreEqual(uint64_t(0),meter.ReadSnapshot().holdIncidents);
        }
        TEST_METHOD(TinyPositivePtsIntervalCannotNormalizeToZeroRefreshAllowance)
        {
            Meter meter;
            const auto first=MakeIntent(1,1000,0.0);
            const auto second=MakeIntent(2,1100,1e-18);
            Record(meter,first,MakeEvent(first,10000));
            Record(meter,second,MakeEvent(second,11000));
            Assert::AreEqual(uint64_t(0),meter.ReadSnapshot().excessRefreshes);
        }

        TEST_METHOD(RenderProducerAtomicSpecializationsAreLockFree)
        {
            Meter meter;
            Assert::IsTrue(meter.ProducerAtomicsLockFree());
        }

        TEST_METHOD(SpscRingRejectsImmediatelyOnOverflowAndRecovers)
        {
            SpscIntentRing<2> ring;
            const OutputIntent first = MakeIntent(1, 1000, 0.0);
            const OutputIntent second = MakeIntent(2, 1100, 1.0 / 60.0);
            const OutputIntent third = MakeIntent(3, 1200, 2.0 / 60.0);

            Assert::IsTrue(ring.TryPush(first));
            Assert::IsTrue(ring.TryPush(second));
            Assert::IsFalse(ring.TryPush(third));
            Assert::AreEqual(third.outputSerial, ring.ConsumeOverflowOutputSerial());
            Assert::AreEqual(static_cast<uint64_t>(0),
                ring.ConsumeOverflowOutputSerial());
            Assert::AreEqual(static_cast<size_t>(2), ring.ApproximateSize());

            OutputIntent popped{};
            Assert::IsTrue(ring.TryPop(popped));
            Assert::AreEqual(first.outputSerial, popped.outputSerial);
            Assert::IsTrue(ring.TryPop(popped));
            Assert::AreEqual(second.outputSerial, popped.outputSerial);
            Assert::IsFalse(ring.TryPop(popped));
            Assert::IsTrue(ring.TryPush(third));
            Assert::IsTrue(ring.TryPop(popped));
            Assert::AreEqual(third.outputSerial, popped.outputSerial);
        }

        TEST_METHOD(EventMayArriveBeforeIntentAndDuplicateCannotCountTwice)
        {
            Meter meter;
            const OutputIntent first = MakeIntent(1, 1000, 0.0);
            const OutputIntent second = MakeIntent(2, 1100, 1.0 / 60.0);
            const DisplayEvent firstEvent = MakeEvent(first, 10000);
            meter.ObserveEvent(firstEvent);
            meter.Drain();
            Assert::AreEqual(static_cast<size_t>(1), meter.ReadSnapshot().pendingEvents);

            Assert::IsTrue(meter.SubmitIntent(first));
            meter.Drain();
            Assert::AreEqual(static_cast<uint64_t>(1),
                meter.ReadSnapshot().matchedOutcomes);

            Record(meter, second, MakeEvent(second, 11000));
            meter.ObserveEvent(MakeEvent(second, 11000));
            meter.Drain();

            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(2), snapshot.matchedOutcomes);
            Assert::AreEqual(static_cast<uint64_t>(2), snapshot.matchedDisplayed);
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.verifiedIntervals);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.coverageGaps);
        }

        TEST_METHOD(OutOfOrderEventArrivalIsProcessedInOutputSerialOrder)
        {
            Meter meter;
            const OutputIntent first = MakeIntent(1, 1000, 0.0);
            const OutputIntent second = MakeIntent(2, 1100, 1.0 / 60.0);
            Assert::IsTrue(meter.SubmitIntent(first));
            Assert::IsTrue(meter.SubmitIntent(second));

            meter.ObserveEvent(MakeEvent(second, 11000));
            meter.Drain();
            Assert::AreEqual(static_cast<uint64_t>(0),
                meter.ReadSnapshot().matchedOutcomes);

            meter.ObserveEvent(MakeEvent(first, 10000));
            meter.Drain();
            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(2), snapshot.matchedOutcomes);
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.verifiedIntervals);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.coverageGaps);
        }
        TEST_METHOD(UnmatchedEventWaitsForBoundedReorderWindow)
        {
            Meter meter;
            const OutputIntent delayed = MakeIntent(1, 1000, 0.0);
            const OutputIntent later = MakeIntent(2, 1100, 1.0 / 60.0);
            meter.ObserveEvent(MakeEvent(delayed, 10000));
            Assert::IsTrue(meter.SubmitIntent(later));
            meter.Drain();

            const Snapshot waiting = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(0), waiting.coverageGaps);
            Assert::AreEqual(static_cast<size_t>(1), waiting.pendingEvents);
            Assert::AreEqual(static_cast<size_t>(1), waiting.pendingIntents);

            meter.Drain(121041, 60000);
            const Snapshot expired = meter.ReadSnapshot();
            Assert::IsTrue(expired.coverageGaps > 0);
            Assert::AreEqual(static_cast<uint64_t>(0), expired.confirmedDrops);
            Assert::AreEqual(static_cast<uint64_t>(0), expired.matchedOutcomes);
        }
        TEST_METHOD(OverlappingSubmissionWindowsAreAmbiguousAndNeverCount)
        {
            Meter meter;
            OutputIntent first = MakeIntent(1, 1000, 0.0);
            first.submitEndQpc = 1200;
            OutputIntent second = MakeIntent(2, 1050, 1.0 / 60.0);
            second.submitEndQpc = 1200;
            const DisplayEvent ambiguous = MakeEvent(first, 10000);
            DisplayEvent event = ambiguous;
            event.presentStartQpc = 1100;

            Assert::IsTrue(meter.SubmitIntent(first));
            Assert::IsTrue(meter.SubmitIntent(second));
            meter.ObserveEvent(event);
            meter.Drain();

            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.matchedOutcomes);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.confirmedDrops);
            Assert::IsTrue(snapshot.coverageGaps > 0);
        }

        TEST_METHOD(ThreadAndSwapchainMustMatchBeforeAnOutcomeCanCount)
        {
            const OutputIntent intent = MakeIntent(1, 1000, 0.0);
            DisplayEvent wrongThread = MakeEvent(intent, 10000);
            wrongThread.threadId++;
            Meter threadMeter;
            Assert::IsTrue(threadMeter.SubmitIntent(intent));
            threadMeter.ObserveEvent(wrongThread);
            threadMeter.Drain(121041, 60000);
            Assert::AreEqual(static_cast<uint64_t>(0),
                threadMeter.ReadSnapshot().matchedOutcomes);
            Assert::IsTrue(threadMeter.ReadSnapshot().coverageGaps > 0);

            Meter swapchainMeter;
            DisplayEvent wrongSwapchain = MakeEvent(intent, 10000);
            wrongSwapchain.swapchainIdentity++;
            Assert::IsTrue(swapchainMeter.SubmitIntent(intent));
            swapchainMeter.ObserveEvent(wrongSwapchain);
            swapchainMeter.Drain(121041, 60000);
            Assert::AreEqual(static_cast<uint64_t>(0),
                swapchainMeter.ReadSnapshot().matchedOutcomes);
            Assert::IsTrue(swapchainMeter.ReadSnapshot().coverageGaps > 0);
        }

        TEST_METHOD(ProcessAndSubmissionIntervalMustMatchExactly)
        {
            const OutputIntent intent = MakeIntent(1, 1000, 0.0);

            Meter processMeter;
            DisplayEvent wrongProcess = MakeEvent(intent, 10000);
            wrongProcess.processId++;
            Assert::IsTrue(processMeter.SubmitIntent(intent));
            processMeter.ObserveEvent(wrongProcess);
            processMeter.Drain();
            Assert::AreEqual(static_cast<uint64_t>(0),
                processMeter.ReadSnapshot().matchedOutcomes);

            Meter rangeMeter;
            DisplayEvent outsideRange = MakeEvent(intent, 10000);
            outsideRange.presentStartQpc = intent.submitEndQpc + 1;
            Assert::IsTrue(rangeMeter.SubmitIntent(intent));
            rangeMeter.ObserveEvent(outsideRange);
            rangeMeter.Drain();
            Assert::AreEqual(static_cast<uint64_t>(0),
                rangeMeter.ReadSnapshot().matchedOutcomes);
        }

        TEST_METHOD(RendererViewportAndSwapchainLifetimeChangesBreakIntervals)
        {
            Meter meter;
            OutputIntent first = MakeIntent(1, 1000, 0.0);
            OutputIntent rendererChanged = MakeIntent(2, 1100, 1.0 / 60.0);
            rendererChanged.rendererEpoch++;
            OutputIntent viewportChanged = MakeIntent(3, 1200, 2.0 / 60.0);
            viewportChanged.rendererEpoch = rendererChanged.rendererEpoch;
            viewportChanged.viewportEpoch++;
            OutputIntent lifetimeChanged = MakeIntent(4, 1300, 3.0 / 60.0);
            lifetimeChanged.rendererEpoch = rendererChanged.rendererEpoch;
            lifetimeChanged.viewportEpoch = viewportChanged.viewportEpoch;
            lifetimeChanged.swapchainLifetime++;
            OutputIntent stable = MakeIntent(5, 1400, 4.0 / 60.0);
            stable.rendererEpoch = rendererChanged.rendererEpoch;
            stable.viewportEpoch = viewportChanged.viewportEpoch;
            stable.swapchainLifetime = lifetimeChanged.swapchainLifetime;

            Record(meter, first, MakeEvent(first, 10000));
            Record(meter, rendererChanged, MakeEvent(rendererChanged, 11000));
            Record(meter, viewportChanged, MakeEvent(viewportChanged, 12000));
            Record(meter, lifetimeChanged, MakeEvent(lifetimeChanged, 13000));
            Record(meter, stable, MakeEvent(stable, 14000));

            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.verifiedIntervals);
            Assert::IsTrue(snapshot.coverageGaps >= 3);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.holdIncidents);
        }
        TEST_METHOD(DisplayEventsClassifyNormal24To60CadenceWithoutFalseHolds)
        {
            Meter meter;
            const double frame = 1.0 / 24.0;
            Record(meter, MakeIntent(1, 1000, 0.0, 24.0, 60.0),
                MakeEvent(MakeIntent(1, 1000, 0.0, 24.0, 60.0), 10000));
            Record(meter, MakeIntent(2, 1100, frame, 24.0, 60.0),
                MakeEvent(MakeIntent(2, 1100, frame, 24.0, 60.0), 12000));
            Record(meter, MakeIntent(3, 1200, frame * 2.0, 24.0, 60.0),
                MakeEvent(MakeIntent(3, 1200, frame * 2.0, 24.0, 60.0), 15000));

            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(2), snapshot.verifiedIntervals);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.holdIncidents);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.excessRefreshes);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.coverageGaps);
        }

        TEST_METHOD(ExcessRefreshesAboveCeilCadenceAllowanceAreLowerBoundHolds)
        {
            Meter meter;
            const double frame = 1.0 / 24.0;
            const OutputIntent first = MakeIntent(1, 1000, 0.0, 24.0, 60.0);
            const OutputIntent second = MakeIntent(2, 1100, frame, 24.0, 60.0);
            const OutputIntent third = MakeIntent(3, 1200, frame * 2.0, 24.0, 60.0);
            Record(meter, first, MakeEvent(first, 10000));
            Record(meter, second, MakeEvent(second, 12000));
            Record(meter, third, MakeEvent(third, 16000));

            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.holdIncidents);
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.excessRefreshes);
        }

        TEST_METHOD(PresentModeChangeBreaksHoldAnchor)
        {
            Meter meter;
            const double frame = 1.0 / 24.0;
            const OutputIntent first = MakeIntent(1, 1000, 0.0, 24.0, 60.0);
            const OutputIntent modeChanged = MakeIntent(2, 1100, frame, 24.0, 60.0);
            const OutputIntent stableMode = MakeIntent(3, 1200,
                frame * 2.0, 24.0, 60.0);

            Record(meter, first, MakeEvent(first, 10000));

            DisplayEvent transitionEvent = MakeEvent(modeChanged, 17000);
            transitionEvent.presentMode = 2; // composed copy -> independent flip
            Record(meter, modeChanged, transitionEvent);

            DisplayEvent stableEvent = MakeEvent(stableMode, 19000);
            stableEvent.presentMode = 2;
            Record(meter, stableMode, stableEvent);

            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(3), snapshot.matchedDisplayed);
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.verifiedIntervals);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.holdIncidents);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.excessRefreshes);
            Assert::IsTrue(snapshot.coverageGaps > 0);
        }

        TEST_METHOD(LongTimelineExact60HzMissedRefreshSurvivesPtsRounding)
        {
            Meter meter;
            const double startPts = 36000.0;
            const OutputIntent first = MakeIntent(1, 1000, startPts, 60.0, 60.0);
            const OutputIntent second = MakeIntent(2, 1100,
                startPts + 1.0 / 60.0, 60.0, 60.0);

            Record(meter, first, MakeEvent(first, 10000));
            // Two scanouts for one exact 60 Hz output interval means one excess refresh.
            Record(meter, second, MakeEvent(second, 12000));

            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.verifiedIntervals);
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.holdIncidents);
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.excessRefreshes);
        }

        TEST_METHOD(LongTimeline5994To60KeepsConservativeTwoRefreshAllowance)
        {
            Meter meter;
            const double startPts = 36000.0;
            const OutputIntent first = MakeIntent(1, 1000, startPts, 59.94, 60.0);
            const OutputIntent second = MakeIntent(2, 1100,
                startPts + 1.0 / 59.94, 59.94, 60.0);

            Record(meter, first, MakeEvent(first, 10000));
            Record(meter, second, MakeEvent(second, 12000));

            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.verifiedIntervals);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.holdIncidents);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.excessRefreshes);
        }
        TEST_METHOD(SmoothMotionLikeUniqueOutputPtsAtRefreshRateDoesNotLookRepeated)
        {
            Meter meter;
            for (uint64_t serial = 1; serial <= 6; ++serial)
            {
                const int64_t startQpc = 1000 + static_cast<int64_t>(serial) * 100;
                const double pts = static_cast<double>(serial - 1) / 60.0;
                const OutputIntent intent = MakeIntent(serial, startQpc, pts, 60.0, 60.0);
                const DisplayEvent event = MakeEvent(intent,
                    10000 + static_cast<int64_t>(serial - 1) * 1000);
                Record(meter, intent, event);
            }

            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(5), snapshot.verifiedIntervals);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.holdIncidents);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.excessRefreshes);
        }

        TEST_METHOD(DiscardedIsConfirmedDropButUnknownAndLostOnlyBreakCoverage)
        {
            Meter meter;
            const OutputIntent first = MakeIntent(1, 1000, 0.0);
            const OutputIntent second = MakeIntent(2, 1100, 1.0 / 60.0);
            const OutputIntent third = MakeIntent(3, 1200, 2.0 / 60.0);

            Record(meter, first, MakeEvent(first, 0, Disposition::Discarded));
            Record(meter, second, MakeEvent(second, 0, Disposition::Unknown));
            Record(meter, third, MakeEvent(third, 0, Disposition::Lost));

            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(3), snapshot.matchedOutcomes);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.matchedDisplayed);
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.confirmedDrops);
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.unexpectedDrops);
            Assert::IsTrue(snapshot.coverageGaps >= 3);
        }

        TEST_METHOD(IntentionalDiscardIsRawOnlyAndRenderFailureBreaksAnchorButNotDrop)
        {
            Meter meter;
            const OutputIntent first = MakeIntent(1, 1000, 0.0);
            OutputIntent cadenceCorrection = MakeIntent(2, 1100, 1.0 / 60.0);
            cadenceCorrection.intentionalCorrection = true;
            OutputIntent renderFailed = MakeIntent(3, 1200, 2.0 / 60.0);
            renderFailed.renderFailed = true;
            const OutputIntent afterFailure = MakeIntent(4, 1300, 3.0 / 60.0);
            const OutputIntent stable = MakeIntent(5, 1400, 4.0 / 60.0);
            OutputIntent failedDiscard = MakeIntent(6, 1500, 5.0 / 60.0);
            failedDiscard.intentionalCorrection = true;
            failedDiscard.renderFailed = true;

            Record(meter, first, MakeEvent(first, 10000));
            Record(meter, cadenceCorrection,
                MakeEvent(cadenceCorrection, 0, Disposition::Discarded));
            Record(meter, renderFailed, MakeEvent(renderFailed, 12000));
            Record(meter, afterFailure, MakeEvent(afterFailure, 13000));
            Record(meter, stable, MakeEvent(stable, 14000));
            Record(meter, failedDiscard,
                MakeEvent(failedDiscard, 0, Disposition::Discarded));

            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(6), snapshot.matchedOutcomes);
            Assert::AreEqual(static_cast<uint64_t>(2), snapshot.confirmedDrops);
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.unexpectedDrops);
            // No interval spans either the cadence correction or renderer failure.
            // The only verified interval is the stable pair after the failure.
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.verifiedIntervals);
        }

        TEST_METHOD(MissingFinalEventExpiresAsGapWithoutInventingDrop)
        {
            Meter meter;
            const OutputIntent intent = MakeIntent(1, 1000, 0.0);
            Assert::IsTrue(meter.SubmitIntent(intent));
            meter.Drain();
            meter.Drain(121041, 60000);

            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.matchedOutcomes);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.confirmedDrops);
            Assert::IsTrue(snapshot.coverageGaps > 0);
            Assert::AreEqual(static_cast<size_t>(0), snapshot.pendingIntents);
        }

        TEST_METHOD(ContextChangeAndIntentionalCorrectionBreakTheAnchor)
        {
            Meter meter;
            const OutputIntent first = MakeIntent(1, 1000, 0.0);
            OutputIntent changedSource = MakeIntent(2, 1100, 1.0 / 60.0);
            changedSource.sourceEpoch++;
            OutputIntent nextSource = MakeIntent(3, 1200, 2.0 / 60.0);
            nextSource.sourceEpoch++;
            OutputIntent correction = MakeIntent(4, 1300, 3.0 / 60.0);
            correction.intentionalCorrection = true;
            const OutputIntent afterCorrection = MakeIntent(5, 1400, 4.0 / 60.0);
            const OutputIntent stable = MakeIntent(6, 1500, 5.0 / 60.0);

            Record(meter, first, MakeEvent(first, 10000));
            Record(meter, changedSource, MakeEvent(changedSource, 11000));
            Record(meter, nextSource, MakeEvent(nextSource, 12000));
            Record(meter, correction, MakeEvent(correction, 13000));
            Record(meter, afterCorrection, MakeEvent(afterCorrection, 14000));
            Record(meter, stable, MakeEvent(stable, 15000));

            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(2), snapshot.verifiedIntervals);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.holdIncidents);
            Assert::IsTrue(snapshot.coverageGaps >= 2);
        }

        TEST_METHOD(CoalescedResetPublicationUsesLatestQpcCutoff)
        {
            Meter meter;
            meter.RequestReset(1, 1000);
            meter.RequestReset(2, 2000);
            meter.Drain();

            const Snapshot reset = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(2), reset.appliedResetSerial);
            Assert::IsFalse(reset.resetPending);

            // A delayed event between the two requested cutoffs is pre-reset
            // evidence for the latest reset and must not enter the reorder set.
            OutputIntent oldIntent = MakeIntent(1, 1400, 0.0, 60.0, 60.0, 2);
            DisplayEvent delayed = MakeEvent(oldIntent, 10000);
            delayed.presentStartQpc = 1500;
            meter.ObserveEvent(delayed);
            meter.Drain();

            const Snapshot afterDelayedEvent = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<size_t>(0), afterDelayedEvent.pendingEvents);
            Assert::AreEqual(static_cast<uint64_t>(0),
                afterDelayedEvent.matchedOutcomes);
        }

        TEST_METHOD(PostResetEventObservedBeforeDrainSurvivesResetApplication)
        {
            Meter meter;
            meter.RequestReset(1, 1000);

            OutputIntent afterReset = MakeIntent(1, 1400, 0.0,
                60.0, 60.0, 1);
            afterReset.submitEndQpc = 1600;
            DisplayEvent event = MakeEvent(afterReset, 10000);
            event.presentStartQpc = 1500;
            meter.ObserveEvent(event);
            Assert::AreEqual(static_cast<size_t>(1),
                meter.ReadSnapshot().pendingEvents);
            Assert::IsTrue(meter.SubmitIntent(afterReset));

            meter.Drain();
            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.appliedResetSerial);
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.matchedOutcomes);
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.matchedDisplayed);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.coverageGaps);
        }

        TEST_METHOD(ResetDrainSkipsOldIntentAndKeepsPostResetIntent)
        {
            Meter meter;
            const OutputIntent beforeReset = MakeIntent(1, 1000, 0.0);
            Assert::IsTrue(meter.SubmitIntent(beforeReset));

            meter.RequestReset(1, beforeReset.submitEndQpc);
            const OutputIntent afterReset = MakeIntent(2, 1100,
                1.0 / 60.0, 60.0, 60.0, 1);
            Assert::IsTrue(meter.SubmitIntent(afterReset));
            meter.Drain();

            const Snapshot afterDrain = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(1),
                afterDrain.appliedResetSerial);
            Assert::AreEqual(static_cast<uint64_t>(0), afterDrain.matchedOutcomes);
            Assert::AreEqual(static_cast<size_t>(1), afterDrain.pendingIntents);

            meter.ObserveEvent(MakeEvent(afterReset, 11000));
            meter.Drain();
            const Snapshot afterEvent = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(1), afterEvent.matchedOutcomes);
            Assert::AreEqual(static_cast<uint64_t>(1), afterEvent.matchedDisplayed);
            Assert::AreEqual(static_cast<uint64_t>(0), afterEvent.coverageGaps);
        }

        TEST_METHOD(ResetWatermarkCutsQueuedAndLateOldEvidence)
        {
            Meter meter;
            const double frame = 1.0 / 24.0;
            const OutputIntent first = MakeIntent(1, 1000, 0.0, 24.0, 60.0);
            const OutputIntent second = MakeIntent(2, 1100, frame, 24.0, 60.0);
            Record(meter, first, MakeEvent(first, 10000));
            Record(meter, second, MakeEvent(second, 14000));
            Assert::AreEqual(static_cast<uint64_t>(1),
                meter.ReadSnapshot().holdIncidents);

            meter.RequestReset(1, second.submitEndQpc);
            meter.ObserveEvent(MakeEvent(second, 14000));
            meter.Drain();
            meter.ObserveEvent(MakeEvent(second, 14000));
            Assert::IsTrue(meter.SubmitIntent(second));
            meter.Drain();

            const Snapshot cleared = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(1), cleared.appliedResetSerial);
            Assert::AreEqual(static_cast<uint64_t>(0), cleared.matchedOutcomes);
            Assert::AreEqual(static_cast<uint64_t>(0), cleared.confirmedDrops);
            Assert::AreEqual(static_cast<uint64_t>(0), cleared.holdIncidents);

            const OutputIntent afterReset = MakeIntent(3, 2000, 0.0, 24.0, 60.0, 1);
            Record(meter, afterReset, MakeEvent(afterReset, 20000));
            const Snapshot resumed = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(1), resumed.matchedOutcomes);
            Assert::AreEqual(static_cast<uint64_t>(1), resumed.matchedDisplayed);
            Assert::AreEqual(static_cast<uint64_t>(0), resumed.confirmedDrops);
            Assert::AreEqual(static_cast<uint64_t>(0), resumed.holdIncidents);
        }

        TEST_METHOD(HealthyIntervalsRestoreObservingWhileGapHistoryRemains)
        {
            Meter meter;
            OutputIntent first = MakeIntent(1, 1000, 0.0);
            OutputIntent second = MakeIntent(2, 1100, 1.0 / 60.0);
            Record(meter, first, MakeEvent(first, 10000));
            Record(meter, second, MakeEvent(second, 11000));
            Assert::IsTrue(meter.ReadSnapshot().state == CoverageState::Observing);

            meter.ReportGap();
            Assert::IsTrue(meter.ReadSnapshot().state == CoverageState::Limited);
            const uint64_t gaps = meter.ReadSnapshot().coverageGaps;

            OutputIntent third = MakeIntent(3, 1200, 2.0 / 60.0);
            OutputIntent fourth = MakeIntent(4, 1300, 3.0 / 60.0);
            Record(meter, third, MakeEvent(third, 12000));
            Record(meter, fourth, MakeEvent(fourth, 13000));

            const Snapshot recovered = meter.ReadSnapshot();
            Assert::IsTrue(recovered.state == CoverageState::Observing);
            Assert::AreEqual(gaps, recovered.coverageGaps);
            Assert::AreEqual(static_cast<uint64_t>(4), recovered.matchedDisplayed);
        }
        TEST_METHOD(CollectorGapAndUnavailableStateRetainCounts)
        {
            Meter meter;
            const OutputIntent first = MakeIntent(1, 1000, 0.0);
            const OutputIntent second = MakeIntent(2, 1100, 1.0 / 60.0);
            Record(meter, first, MakeEvent(first, 10000));
            Record(meter, second, MakeEvent(second, 11000));
            Assert::AreEqual(static_cast<uint64_t>(2),
                meter.ReadSnapshot().matchedDisplayed);

            meter.ReportGap();
            const Snapshot afterGap = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(2), afterGap.matchedDisplayed);
            Assert::IsTrue(afterGap.coverageGaps > 0);

            meter.MarkUnavailable();
            Assert::IsTrue(meter.ReadSnapshot().state == CoverageState::Unavailable);
            Assert::AreEqual(static_cast<uint64_t>(2),
                meter.ReadSnapshot().matchedDisplayed);
            meter.MarkAvailable();
            Assert::IsTrue(meter.ReadSnapshot().state == CoverageState::Limited);
        }

        TEST_METHOD(MatchedDisplayedCountDistinguishesValidZeroFromUnavailable)
        {
            Meter meter;
            OutputIntent intent = MakeIntent(1, 1000, 0.0);
            DisplayEvent event = MakeEvent(intent, 10000);
            event.displayRateValid = false;
            event.displayHz = 0.0;
            Record(meter, intent, event);

            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.matchedOutcomes);
            Assert::AreEqual(static_cast<uint64_t>(1), snapshot.matchedDisplayed);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.verifiedIntervals);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.confirmedDrops);
            Assert::IsTrue(snapshot.coverageGaps > 0);
        }

        TEST_METHOD(ResetDropsPreResetOverflowButPreservesPostResetOverflow)
        {
            Meter meter;
            for (uint64_t serial = 1; serial <= Meter::Capacity + 1; ++serial)
            {
                const OutputIntent intent = MakeIntent(serial,
                    1000 + static_cast<int64_t>(serial) * 100,
                    static_cast<double>(serial) / 60.0);
                Assert::IsTrue(meter.SubmitIntent(intent) ==
                    (serial <= Meter::Capacity));
            }

            // The rejected intent's serial was included in the producer watermark.
            meter.RequestReset(1, 40000);
            meter.Drain();
            Snapshot afterReset = meter.ReadSnapshot();
            Assert::AreEqual(static_cast<uint64_t>(1), afterReset.appliedResetSerial);
            Assert::AreEqual(static_cast<uint64_t>(0), afterReset.coverageGaps);
            Assert::AreEqual(static_cast<uint64_t>(0), afterReset.confirmedDrops);
            Assert::AreEqual(static_cast<size_t>(0), afterReset.pendingIntents);

            const uint64_t firstPostResetSerial = Meter::Capacity + 2;
            for (uint64_t offset = 0; offset <= Meter::Capacity; ++offset)
            {
                const uint64_t serial = firstPostResetSerial + offset;
                OutputIntent intent = MakeIntent(serial,
                    50000 + static_cast<int64_t>(offset) * 100,
                    static_cast<double>(offset) / 60.0, 60.0, 60.0, 1);
                Assert::IsTrue(meter.SubmitIntent(intent) ==
                    (offset < Meter::Capacity));
            }
            meter.Drain();

            const Snapshot afterPostResetOverflow = meter.ReadSnapshot();
            Assert::IsTrue(afterPostResetOverflow.coverageGaps > 0);
            Assert::AreEqual(static_cast<size_t>(Meter::Capacity),
                afterPostResetOverflow.pendingIntents);
            Assert::AreEqual(static_cast<uint64_t>(0),
                afterPostResetOverflow.confirmedDrops);
        }

        TEST_METHOD(RingOverflowMarksMeterCoverageGap)
        {
            Meter meter;
            for (uint64_t serial = 1; serial <= Meter::Capacity + 1; ++serial)
            {
                const OutputIntent intent = MakeIntent(serial,
                    1000 + static_cast<int64_t>(serial) * 100,
                    static_cast<double>(serial) / 60.0);
                const bool accepted = meter.SubmitIntent(intent);
                Assert::IsTrue(accepted == (serial <= Meter::Capacity));
            }
            meter.Drain();

            const Snapshot snapshot = meter.ReadSnapshot();
            Assert::IsTrue(snapshot.coverageGaps > 0);
            Assert::AreEqual(static_cast<size_t>(Meter::Capacity),
                snapshot.pendingIntents);
            Assert::AreEqual(static_cast<uint64_t>(0), snapshot.confirmedDrops);
        }
    };
}