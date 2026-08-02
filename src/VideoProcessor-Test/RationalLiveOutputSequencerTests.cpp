#include "pch.h"
#include "CppUnitTest.h"

#include <RationalLiveOutputSequencer.h>

#include <cmath>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
RationalLiveOutputTimestampInput Input(uint64_t epoch = 9)
{
	RationalLiveOutputTimestampInput input;
	input.epoch = epoch;
	input.ppmCorrection = -17;
	input.pipelineOffset = 0;
	input.presentationLead = 1800000;
	return input;
}

RationalLiveOutputTimestampInput DisplayInput(
	uint64_t epoch = 9, double rate = 60.0)
{
	RationalLiveOutputTimestampInput input = Input(epoch);
	input.cadence = RationalLiveOutputCadence::Display;
	input.displayRateHz = rate;
	return input;
}
}

namespace Tests
{
	TEST_CLASS(RationalLiveOutputSequencerTests)
	{
	public:
		TEST_METHOD(CommitsAContinuous5994OutputTimeline)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const RationalLiveOutputTimestampDecision first =
				sequencer.Preview(Input());
			Assert::IsTrue(first.valid);
			Assert::AreEqual<uint64_t>(0, first.outputSequence);
			Assert::AreEqual<VideoReferenceTime>(1800000, first.start);
			Assert::IsTrue(first.discontinuity);
			Assert::IsTrue(sequencer.Commit(first));

			const RationalLiveOutputTimestampDecision second =
				sequencer.Preview(Input());
			Assert::AreEqual<uint64_t>(1, second.outputSequence);
			Assert::AreEqual<VideoReferenceTime>(first.stop, second.start);
			Assert::IsTrue(second.stop > second.start);
			Assert::AreEqual<int64_t>(first.mediaStop, second.mediaStart);
			Assert::IsFalse(second.discontinuity);
		}

		TEST_METHOD(SkippedLivePicturesConsumePresentationButNotMediaTime)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			auto firstInput = Input();
			firstInput.sourceFrameNumberValid = true;
			firstInput.sourceFrameNumber = 100;
			RationalLiveOutputTimestampDecision previous =
				sequencer.Preview(firstInput);
			Assert::IsTrue(sequencer.Commit(previous));
			auto afterDiscardInput = Input();
			afterDiscardInput.sourceFrameNumberValid = true;
			afterDiscardInput.sourceFrameNumber = 102;
			const RationalLiveOutputTimestampDecision afterDiscard =
				sequencer.Preview(afterDiscardInput);
			Assert::AreEqual<uint64_t>(1, afterDiscard.outputSequence);
			Assert::AreEqual<uint32_t>(1, afterDiscard.sourceGapSlotsBefore);
			Assert::AreEqual<uint32_t>(2, afterDiscard.presentationSlotsConsumed);
			Assert::AreEqual<int64_t>(previous.mediaStop, afterDiscard.mediaStart);
			Assert::AreEqual<VideoReferenceTime>(
				afterDiscardInput.presentationLead +
				VideoTimingController::RationalTimestamp(
					2, 1001, 60000, afterDiscardInput.ppmCorrection),
				afterDiscard.start);
			Assert::IsFalse(afterDiscard.discontinuity);
		}

		TEST_METHOD(FailedDeliveryRetriesTheSameCaptureGap)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			auto firstInput = Input();
			firstInput.sourceFrameNumberValid = true;
			firstInput.sourceFrameNumber = 100;
			const auto first = sequencer.Preview(firstInput);
			Assert::IsTrue(sequencer.Commit(first));

			auto gapInput = Input();
			gapInput.sourceFrameNumberValid = true;
			gapInput.sourceFrameNumber = 102;
			const auto attempted = sequencer.Preview(gapInput);
			const auto retry = sequencer.Preview(gapInput);
			Assert::AreEqual<VideoReferenceTime>(attempted.start, retry.start);
			Assert::AreEqual<uint32_t>(1, retry.sourceGapSlotsBefore);
			Assert::IsTrue(sequencer.Commit(retry));
		}

		TEST_METHOD(RepeatedSourceFrameDoesNotCreateACaptureGap)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			auto input = DisplayInput();
			input.sourceFrameNumberValid = true;
			input.sourceFrameNumber = 100;
			const auto first = sequencer.Preview(input);
			Assert::IsTrue(sequencer.Commit(first));
			const auto repeated = sequencer.Preview(input);
			Assert::AreEqual<uint32_t>(0, repeated.sourceGapSlotsBefore);
			Assert::AreEqual<VideoReferenceTime>(first.stop, repeated.start);
			Assert::IsTrue(sequencer.Commit(repeated));

			input.sourceFrameNumber = 101;
			const auto next = sequencer.Preview(input);
			Assert::AreEqual<uint32_t>(0, next.sourceGapSlotsBefore);
			Assert::AreEqual<VideoReferenceTime>(repeated.stop, next.start);
		}

		TEST_METHOD(CaptureAndRendererGapsComposeExactly)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			auto firstInput = DisplayInput();
			firstInput.sourceFrameNumberValid = true;
			firstInput.sourceFrameNumber = 100;
			const auto first = sequencer.Preview(firstInput);
			Assert::IsTrue(sequencer.Commit(first));

			auto combined = DisplayInput();
			combined.sourceFrameNumberValid = true;
			combined.sourceFrameNumber = 102;
			combined.presentationGapSlotsBefore = 1;
			const auto decision = sequencer.Preview(combined);
			Assert::AreEqual<uint32_t>(1, decision.sourceGapSlotsBefore);
			Assert::AreEqual<uint32_t>(3, decision.presentationSlotsConsumed);
			Assert::AreEqual<int64_t>(first.mediaStop, decision.mediaStart);
		}

		TEST_METHOD(PeriodicLatestWinsDiscardsRemainOnThe5994LiveTimeline)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			auto input = Input();
			input.sourceFrameNumberValid = true;
			const uint64_t firstFrame = 100;
			input.sourceFrameNumber = firstFrame;
			for (uint64_t delivery = 0; delivery < 10000; ++delivery)
			{
				if (delivery != 0)
					input.sourceFrameNumber += (delivery % 600 == 0) ? 2 : 1;
				const auto decision = sequencer.Preview(input);
				const uint64_t expectedSlot =
					input.sourceFrameNumber - firstFrame;
				Assert::AreEqual<VideoReferenceTime>(
					input.presentationLead + VideoTimingController::RationalTimestamp(
						expectedSlot, 1001, 60000, input.ppmCorrection),
					decision.start);
				Assert::IsTrue(sequencer.Commit(decision));
			}
		}

		TEST_METHOD(LatestWinsDiscardConsumesOne23976PresentationSlot)
		{
			RationalLiveOutputSequencer sequencer(24000, 1001, 417083);
			auto input = Input();
			input.sourceFrameNumberValid = true;
			input.sourceFrameNumber = 100;
			const auto first = sequencer.Preview(input);
			Assert::IsTrue(sequencer.Commit(first));
			input.sourceFrameNumber = 102;
			const auto afterGap = sequencer.Preview(input);
			Assert::AreEqual<uint32_t>(1, afterGap.sourceGapSlotsBefore);
			Assert::AreEqual<VideoReferenceTime>(
				input.presentationLead + VideoTimingController::RationalTimestamp(
					2, 1001, 24000, input.ppmCorrection),
				afterGap.start);
		}

		TEST_METHOD(SteadyLatestWinsReplacementKeepsDeliveredCadenceContinuous)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			auto input = Input();
			input.sourceFrameNumberValid = true;
			input.accountSourceGap = false;
			input.sourceFrameNumber = 100;
			const auto first = sequencer.Preview(input);
			Assert::IsTrue(sequencer.Commit(first));

			// VP's steady latest-wins queue deliberately replaced three stale
			// live pictures. The next delivered picture owns the next display
			// slot; it must not manufacture three empty renderer slots.
			input.sourceFrameNumber = 104;
			const auto replacement = sequencer.Preview(input);
			Assert::AreEqual<uint64_t>(3,
				replacement.observedSourceGapSlotsBefore);
			Assert::AreEqual<uint32_t>(0,
				replacement.sourceGapSlotsBefore);
			Assert::AreEqual<uint32_t>(1,
				replacement.presentationSlotsConsumed);
			Assert::IsTrue(replacement.sourceGapSuppressed);
			Assert::AreEqual<VideoReferenceTime>(
				first.stop, replacement.start);
		}

		TEST_METHOD(IntentionalCatchUpRebaselinesWithoutAddingLead)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			auto input = Input();
			input.sourceFrameNumberValid = true;
			input.sourceFrameNumber = 120;
			const auto first = sequencer.Preview(input);
			Assert::IsTrue(sequencer.Commit(first));

			input.sourceFrameNumber = 159;
			input.sourceGapSlotsToSuppress = 38;
			input.minimumPresentationStartValid = true;
			input.minimumPresentationStart = first.stop + 5000000;
			const auto caughtUp = sequencer.Preview(input);
			Assert::AreEqual<uint64_t>(38, caughtUp.observedSourceGapSlotsBefore);
			Assert::AreEqual<uint32_t>(0, caughtUp.sourceGapSlotsBefore);
			Assert::IsTrue(caughtUp.sourceGapSuppressed);
			Assert::AreEqual<VideoReferenceTime>(
				input.minimumPresentationStart, caughtUp.start);
			Assert::IsTrue(sequencer.Commit(caughtUp));

			input.sourceFrameNumber = 161;
			input.sourceGapSlotsToSuppress = 0;
			input.minimumPresentationStartValid = false;
			const auto steadyGap = sequencer.Preview(input);
			Assert::AreEqual<uint32_t>(1, steadyGap.sourceGapSlotsBefore);
			Assert::IsFalse(steadyGap.sourceGapSuppressed);
			Assert::AreEqual<VideoReferenceTime>(
				input.minimumPresentationStart +
					VideoTimingController::RationalTimestamp(
						2, 1001, 60000, input.ppmCorrection),
				steadyGap.start);
		}

		TEST_METHOD(FailedCatchUpDeliveryKeepsSuppressionTransactional)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			auto input = Input();
			input.sourceFrameNumberValid = true;
			input.sourceFrameNumber = 100;
			Assert::IsTrue(sequencer.Commit(sequencer.Preview(input)));

			input.sourceFrameNumber = 140;
			input.sourceGapSlotsToSuppress = 39;
			const auto attempted = sequencer.Preview(input);
			const auto retry = sequencer.Preview(input);
			Assert::IsTrue(attempted.sourceGapSuppressed);
			Assert::AreEqual<VideoReferenceTime>(attempted.start, retry.start);
			Assert::AreEqual<uint64_t>(39, retry.observedSourceGapSlotsBefore);
			Assert::IsTrue(sequencer.Commit(retry));
		}

		TEST_METHOD(SameCounterRepeatDoesNotConsumePendingGapSuppression)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			auto input = Input();
			input.sourceFrameNumberValid = true;
			input.sourceFrameNumber = 100;
			Assert::IsTrue(sequencer.Commit(sequencer.Preview(input)));

			input.sourceGapSlotsToSuppress = 39;
			const auto sameCounterRepeat = sequencer.Preview(input);
			Assert::IsFalse(sameCounterRepeat.sourceGapSuppressed);
			Assert::AreEqual<uint64_t>(0,
				sameCounterRepeat.observedSourceGapSlotsBefore);
			Assert::IsTrue(sequencer.Commit(sameCounterRepeat));

			input.sourceFrameNumber = 140;
			const auto postTrim = sequencer.Preview(input);
			Assert::IsTrue(postTrim.sourceGapSuppressed);
			Assert::AreEqual<uint64_t>(39,
				postTrim.observedSourceGapSlotsBefore);
			Assert::AreEqual<uint32_t>(0, postTrim.sourceGapSlotsBefore);
		}

		TEST_METHOD(SceneDropSuppressesOnlyItsOneIntentionalSlot)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			auto input = Input();
			input.sourceFrameNumberValid = true;
			input.sourceFrameNumber = 100;
			Assert::IsTrue(sequencer.Commit(sequencer.Preview(input)));

			input.sourceFrameNumber = 104;
			input.sourceGapSlotsToSuppress = 1;
			const auto decision = sequencer.Preview(input);
			Assert::AreEqual<uint64_t>(3,
				decision.observedSourceGapSlotsBefore);
			Assert::AreEqual<uint64_t>(1,
				decision.intentionalSourceGapSlotsSuppressed);
			Assert::AreEqual<uint32_t>(2, decision.sourceGapSlotsBefore);
			Assert::IsFalse(decision.materialSourceGapSuppressed);
		}

		TEST_METHOD(MaterialForwardJumpCannotCreateFarFuturePts)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			auto input = Input();
			input.sourceFrameNumberValid = true;
			input.sourceFrameNumber = 100;
			const auto first = sequencer.Preview(input);
			Assert::IsTrue(sequencer.Commit(first));

			input.sourceFrameNumber = 400;
			const auto material = sequencer.Preview(input);
			Assert::AreEqual<uint64_t>(299, material.observedSourceGapSlotsBefore);
			Assert::AreEqual<uint32_t>(0, material.sourceGapSlotsBefore);
			Assert::IsTrue(material.sourceGapSuppressed);
			Assert::AreEqual<VideoReferenceTime>(first.stop, material.start);
		}

		TEST_METHOD(SteadyMaterialRunningGapReanchorsThenRemainsContiguous)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			auto input = Input();
			input.sourceFrameNumberValid = true;
			input.accountSourceGap = false;
			input.sourceFrameNumber = 100;
			Assert::IsTrue(sequencer.Commit(sequencer.Preview(input)));

			input.sourceFrameNumber = 400;
			input.minimumPresentationStartValid = true;
			input.minimumPresentationStart = 9000000;
			const auto caughtUp = sequencer.Preview(input);
			Assert::IsTrue(caughtUp.observedSourceGapMaterial);
			Assert::IsTrue(caughtUp.sourceGapSuppressed);
			Assert::AreEqual<VideoReferenceTime>(9000000, caughtUp.start);
			Assert::IsTrue(sequencer.Commit(caughtUp));

			input.sourceFrameNumber = 401;
			input.minimumPresentationStartValid = false;
			const auto next = sequencer.Preview(input);
			Assert::AreEqual<VideoReferenceTime>(caughtUp.stop, next.start);
		}

		TEST_METHOD(MaterialSourceDiscontinuityCanReanchorToRunningGraph)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			auto input = Input();
			input.sourceFrameNumberValid = true;
			input.sourceFrameNumber = 100;
			Assert::IsTrue(sequencer.Commit(sequencer.Preview(input)));

			input.sourceFrameNumber = 400;
			input.sourceDiscontinuity = true;
			input.accountSourceGap = false;
			input.minimumPresentationStartValid = true;
			input.minimumPresentationStart = 7000000;
			const auto resumed = sequencer.Preview(input);
			Assert::IsTrue(resumed.observedSourceGapMaterial);
			Assert::IsTrue(resumed.sourceGapSuppressed);
			Assert::AreEqual<uint32_t>(0, resumed.sourceGapSlotsBefore);
			Assert::AreEqual<VideoReferenceTime>(7000000, resumed.start);
			Assert::IsTrue(resumed.discontinuity);
		}

		TEST_METHOD(CounterRollbackRebaselinesSourceIdentity)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			auto input = Input();
			input.sourceFrameNumberValid = true;
			input.sourceFrameNumber = 100;
			Assert::IsTrue(sequencer.Commit(sequencer.Preview(input)));

			input.sourceFrameNumber = 1;
			input.sourceDiscontinuity = true;
			input.accountSourceGap = false;
			const auto reset = sequencer.Preview(input);
			Assert::AreEqual<uint64_t>(0, reset.observedSourceGapSlotsBefore);
			Assert::IsTrue(sequencer.Commit(reset));

			input.sourceFrameNumber = 3;
			input.sourceDiscontinuity = false;
			input.accountSourceGap = true;
			input.sourceGapSlotsToSuppress = 0;
			const auto nextGap = sequencer.Preview(input);
			Assert::AreEqual<uint32_t>(1, nextGap.sourceGapSlotsBefore);
		}

		TEST_METHOD(FailedDeliveryDoesNotAdvanceTheSequence)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const RationalLiveOutputTimestampDecision attempted =
				sequencer.Preview(Input());
			const RationalLiveOutputTimestampDecision retry =
				sequencer.Preview(Input());
			Assert::AreEqual<uint64_t>(attempted.outputSequence, retry.outputSequence);
			Assert::AreEqual<VideoReferenceTime>(attempted.start, retry.start);
			Assert::IsTrue(sequencer.Commit(retry));
		}

		TEST_METHOD(NewEpochStartsANewDirectShowSegmentSequence)
		{
			RationalLiveOutputSequencer sequencer(24000, 1001, 417083);
			const RationalLiveOutputTimestampDecision first =
				sequencer.Preview(Input(9));
			Assert::IsTrue(sequencer.Commit(first));
			const RationalLiveOutputTimestampDecision next =
				sequencer.Preview(Input(10));
			Assert::AreEqual<uint64_t>(0, next.outputSequence);
			Assert::IsTrue(next.discontinuity);
		}

		TEST_METHOD(SameEpochResetDoesNotRestartTheTimeline)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const auto first = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(first));
			sequencer.ResetToEpoch(9);
			const auto next = sequencer.Preview(Input());
			Assert::AreEqual<uint64_t>(1, next.outputSequence);
			Assert::AreEqual<VideoReferenceTime>(first.stop, next.start);
			Assert::IsFalse(next.discontinuity);
		}

		TEST_METHOD(Rational23976RemainsExactAcrossLongCommittedRun)
		{
			RationalLiveOutputSequencer sequencer(24000, 1001, 417083);
			RationalLiveOutputTimestampInput input = Input();
			const uint64_t frames = 10000;
			for (uint64_t index = 0; index < frames; ++index)
			{
				const auto decision = sequencer.Preview(input);
				Assert::IsTrue(sequencer.Commit(decision));
			}
			const auto next = sequencer.Preview(input);
			Assert::AreEqual<VideoReferenceTime>(
				input.presentationLead + VideoTimingController::RationalTimestamp(
					frames, 1001, 24000, input.ppmCorrection),
				next.start);
		}

		TEST_METHOD(Display23976RemainsExactAcrossFourHours)
		{
			RationalLiveOutputSequencer sequencer(24000, 1001, 417083);
			const double displayRate = 24000.0 / 1001.0;
			const auto input = DisplayInput(9, displayRate);
			const uint64_t frames = 345254;
			for (uint64_t index = 0; index < frames; ++index)
			{
				const auto decision = sequencer.Preview(input);
				Assert::IsTrue(sequencer.Commit(decision));
			}
			const auto next = sequencer.Preview(input);
			const VideoReferenceTime expected = input.presentationLead +
				static_cast<VideoReferenceTime>(llround(
					(static_cast<long double>(frames) *
						VideoTimingController::kReferenceTimeTicksPerSecond) /
					displayRate));
			Assert::AreEqual<VideoReferenceTime>(expected, next.start);
		}

		TEST_METHOD(PpmChangeRebasesAtTheLastCommittedStop)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const auto first = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(first));
			auto corrected = Input();
			corrected.ppmCorrection = 25;
			const auto next = sequencer.Preview(corrected);
			Assert::AreEqual<VideoReferenceTime>(first.stop, next.start);
			Assert::AreEqual<int64_t>(first.mediaStop, next.mediaStart);
		}

		TEST_METHOD(NormalSceneNormalRebasesAtCommittedStop)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const auto normal = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(normal));

			const auto scene = sequencer.Preview(DisplayInput());
			Assert::AreEqual<VideoReferenceTime>(normal.stop, scene.start);
			Assert::AreEqual<int64_t>(normal.mediaStop, scene.mediaStart);
			Assert::IsTrue(sequencer.Commit(scene));

			const auto resumed = sequencer.Preview(Input());
			Assert::AreEqual<VideoReferenceTime>(scene.stop, resumed.start);
			Assert::AreEqual<int64_t>(scene.mediaStop, resumed.mediaStart);
			Assert::IsFalse(resumed.discontinuity);
		}

		TEST_METHOD(SourceDiscontinuityIsCarriedWithoutResettingEpoch)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const auto first = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(first));
			auto sourceGap = Input();
			sourceGap.sourceDiscontinuity = true;
			const auto marked = sequencer.Preview(sourceGap);
			Assert::IsTrue(marked.discontinuity);
			Assert::AreEqual<VideoReferenceTime>(first.stop, marked.start);
			Assert::IsTrue(sequencer.Commit(marked));
			const auto following = sequencer.Preview(Input());
			Assert::IsFalse(following.discontinuity);
		}

		TEST_METHOD(RendererGapConsumesPresentationButNotMediaSlots)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const auto first = sequencer.Preview(DisplayInput());
			Assert::IsTrue(sequencer.Commit(first));
			auto gap = DisplayInput();
			gap.presentationGapSlotsBefore = 1;
			const auto afterGap = sequencer.Preview(gap);
			// 60 Hz timestamps are independently rounded: the skipped interval
			// lands at round(2 * 10,000,000 / 60), not two truncated durations.
			Assert::AreEqual<VideoReferenceTime>(2133333, afterGap.start);
			Assert::AreEqual<int64_t>(first.mediaStop, afterGap.mediaStart);
			Assert::AreEqual<uint32_t>(2, afterGap.presentationSlotsConsumed);
			Assert::IsTrue(sequencer.Commit(afterGap));
			const auto next = sequencer.Preview(DisplayInput());
			Assert::AreEqual<VideoReferenceTime>(afterGap.stop, next.start);
			Assert::AreEqual<int64_t>(afterGap.mediaStop, next.mediaStart);
		}

		TEST_METHOD(RendererGapOnCadenceRebaseDoesNotCreateASecondGap)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const auto normal = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(normal));

			auto sceneGap = DisplayInput();
			sceneGap.presentationGapSlotsBefore = 1;
			const auto afterGap = sequencer.Preview(sceneGap);
			Assert::IsTrue(afterGap.start > normal.stop);
			Assert::IsTrue(sequencer.Commit(afterGap));

			const auto following = sequencer.Preview(DisplayInput());
			Assert::AreEqual<VideoReferenceTime>(afterGap.stop, following.start);
			Assert::AreEqual<int64_t>(afterGap.mediaStop, following.mediaStart);
		}

		TEST_METHOD(FailedScenePreviewDoesNotAdvanceTheUnifiedTimeline)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const auto first = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(first));
			const auto attempted = sequencer.Preview(DisplayInput());
			const auto retry = sequencer.Preview(DisplayInput());
			Assert::AreEqual<VideoReferenceTime>(attempted.start, retry.start);
			Assert::AreEqual<int64_t>(attempted.mediaStart, retry.mediaStart);
			Assert::IsTrue(sequencer.Commit(retry));
		}
	};
}
