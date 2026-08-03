#include "pch.h"

#include <ActivePictureDecisionTimeline.h>
#include "CppUnitTest.h"

#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
	namespace
	{
		ActivePictureBounds ScopeBounds()
		{
			return { 0, 276, 3840, 1884, 3840, 2160, 2.388, true };
		}

		ActivePictureBounds FullBounds()
		{
			return { 0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0, true };
		}

		ActivePictureObservation Trusted(
			uint64_t frame, const ActivePictureBounds& bounds)
		{
			ActivePictureObservation observation;
			observation.bounds = bounds;
			observation.frameNumber = frame;
			observation.available = true;
			observation.classification =
				bounds.top == 0 ?
				ActivePictureClassification::FULL_RASTER_TRUSTED :
				ActivePictureClassification::BAR_CROP_TRUSTED;
			return observation;
		}

		ActivePictureFrameIdentity Identity(
			uint64_t generation, uint64_t sequence, uint64_t frame)
		{
			return { generation, sequence, frame, frame * 1000 };
		}

		void EstablishScope(ActivePictureDecisionTimeline& timeline,
			uint64_t generation, uint64_t& sequence)
		{
			ActivePictureFrameDecision published;
			for (uint64_t frame = 1;
				frame <= ActivePictureTransitionModel::INITIAL_CONFIRMATIONS;
				++frame)
			{
				const bool didPublish = timeline.SubmitScheduledObservation(
					Identity(generation, sequence++, frame),
					Trusted(frame, ScopeBounds()), 0, 0, published);
				Assert::AreEqual(
					frame == ActivePictureTransitionModel::INITIAL_CONFIRMATIONS,
					didPublish);
			}
		}
	}

	TEST_CLASS(ActivePictureDecisionTimelineTests)
	{
	public:
		TEST_METHOD(ZeroLookaheadPublishesAtObservationIdentity)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(7);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishScope(timeline, 7, sequence);

			Assert::IsFalse(timeline.SubmitScheduledObservation(
				Identity(7, sequence++, 10), Trusted(10, FullBounds()),
				0, 8, published));
			const auto confirmation = Identity(7, sequence++, 11);
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				confirmation, Trusted(11, FullBounds()), 0, 8, published));
			Assert::AreEqual(confirmation.acceptedSequence,
				published.effectiveIdentity.acceptedSequence);
			Assert::AreEqual<uint8_t>(0, published.effectiveLookahead);
			Assert::IsFalse(published.late);
		}

		TEST_METHOD(ZeroLookaheadMatchesTransitionModelDecisions)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(14);
			ActivePictureTransitionModel baseline;
			std::vector<ActivePictureObservation> observations;
			for (uint64_t frame = 1; frame <= 4; ++frame)
				observations.push_back(Trusted(frame, ScopeBounds()));
			ActivePictureObservation dark;
			dark.frameNumber = 5;
			observations.push_back(dark);
			observations.push_back(Trusted(6, FullBounds()));
			observations.push_back(Trusted(7, FullBounds()));
			ActivePictureObservation provisional = Trusted(8, ScopeBounds());
			provisional.classification =
				ActivePictureClassification::PROVISIONAL;
			observations.push_back(provisional);

			uint64_t sequence = 1;
			for (const auto& observation : observations)
			{
				const ActivePictureTransitionDecision expected =
					baseline.Observe(observation);
				ActivePictureFrameDecision actual;
				const bool didPublish = timeline.SubmitScheduledObservation(
					Identity(14, sequence++, observation.frameNumber),
					observation, 0, 8, actual);
				Assert::AreEqual(expected.publish, didPublish);
				if (didPublish)
				{
					Assert::AreEqual(expected.bounds.left,
						actual.transition.bounds.left);
					Assert::AreEqual(expected.bounds.top,
						actual.transition.bounds.top);
					Assert::AreEqual(expected.bounds.right,
						actual.transition.bounds.right);
					Assert::AreEqual(expected.bounds.bottom,
						actual.transition.bounds.bottom);
					Assert::AreEqual(
						static_cast<int>(expected.state),
						static_cast<int>(actual.transition.state));
				}
			}
		}

		TEST_METHOD(BufferedConfirmationBackdatesToPendingCandidate)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(3);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishScope(timeline, 3, sequence);

			const auto candidate = Identity(3, sequence++, 10);
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				candidate, Trusted(10, FullBounds()), 4, 4, published));
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				Identity(3, sequence++, 11), Trusted(11, FullBounds()),
				4, 4, published));
			Assert::AreEqual(candidate.acceptedSequence,
				published.effectiveIdentity.acceptedSequence);
			Assert::AreEqual<uint8_t>(4, published.effectiveLookahead);
			Assert::IsFalse(published.late);
		}

		TEST_METHOD(ConsumedCandidateCannotBeBackdated)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(4);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishScope(timeline, 4, sequence);

			const auto candidate = Identity(4, sequence++, 10);
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				candidate, Trusted(10, FullBounds()), 4, 4, published));
			timeline.MarkConsumed(candidate);
			const auto confirmation = Identity(4, sequence++, 11);
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				confirmation, Trusted(11, FullBounds()), 4, 4, published));
			Assert::AreEqual(confirmation.acceptedSequence,
				published.effectiveIdentity.acceptedSequence);
			Assert::IsTrue(published.late);
		}

		TEST_METHOD(ScheduledCadenceGapsRemainEligibleForLookahead)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(9);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishScope(timeline, 9, sequence);

			const auto candidate = Identity(9, sequence, 20);
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				candidate, Trusted(20, FullBounds()), 4, 4, published));
			++sequence;
			Assert::IsTrue(timeline.TrackAcceptedFrame(
				Identity(9, sequence++, 21)));
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				Identity(9, sequence, 22), Trusted(22, FullBounds()),
				4, 4, published));
			Assert::AreEqual(candidate.acceptedSequence,
				published.effectiveIdentity.acceptedSequence);
		}

		TEST_METHOD(InwardCropCannotBackdateAcrossUnknownFrames)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(10);
			ActivePictureFrameDecision published;
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				Identity(10, 1, 1), Trusted(1, FullBounds()),
				4, 4, published));
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				Identity(10, 2, 2), Trusted(2, ScopeBounds()),
				4, 4, published));
			const auto confirmation = Identity(10, 3, 3);
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				confirmation, Trusted(3, ScopeBounds()),
				4, 4, published));
			Assert::AreEqual(confirmation.acceptedSequence,
				published.effectiveIdentity.acceptedSequence);
			Assert::IsTrue(published.late);
		}

		TEST_METHOD(InitialBarAcquisitionCannotBackdateFromImplicitFullRaster)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(15);
			ActivePictureFrameDecision published;
			for (uint64_t frame = 1; frame < 4; ++frame)
			{
				Assert::IsFalse(timeline.SubmitScheduledObservation(
					Identity(15, frame, frame), Trusted(frame, ScopeBounds()),
					8, 8, published));
			}
			const auto confirmation = Identity(15, 4, 4);
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				confirmation, Trusted(4, ScopeBounds()), 8, 8, published));
			Assert::AreEqual(confirmation.acceptedSequence,
				published.effectiveIdentity.acceptedSequence);
		}

		TEST_METHOD(StaleTransportGenerationIsRejectedWithoutRollback)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(12);
			ActivePictureFrameDecision published;
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				Identity(13, 1, 1), Trusted(1, FullBounds()),
				0, 0, published));
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				Identity(12, 2, 2), Trusted(2, ScopeBounds()),
				8, 8, published));
			Assert::AreEqual<uint64_t>(13, timeline.TransportGeneration());
		}

		TEST_METHOD(DuplicateObservationCannotConfirmATransition)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(16);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishScope(timeline, 16, sequence);
			const auto candidate = Identity(16, sequence, 10);
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				candidate, Trusted(10, FullBounds()), 8, 8, published));
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				candidate, Trusted(10, FullBounds()), 8, 8, published));
		}

		TEST_METHOD(OlderAcceptedFrameCanBeAnalyzedAfterNewerFramesArrive)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(17);
			const auto first = Identity(17, 1, 1);
			Assert::IsTrue(timeline.TrackAcceptedFrame(first));
			Assert::IsTrue(timeline.TrackAcceptedFrame(Identity(17, 2, 2)));
			Assert::IsTrue(timeline.TrackAcceptedFrame(Identity(17, 3, 3)));
			ActivePictureFrameDecision published;
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				first, Trusted(1, FullBounds()), 2, 2, published));
			Assert::AreEqual<uint64_t>(1,
				published.observationIdentity.acceptedSequence);
		}

		TEST_METHOD(DiscardedCandidateCannotReceiveFutureConfirmation)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(18);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishScope(timeline, 18, sequence);
			const auto candidate = Identity(18, sequence++, 10);
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				candidate, Trusted(10, FullBounds()), 4, 4, published));
			timeline.MarkDiscarded(candidate, 10);
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				Identity(18, sequence++, 11), Trusted(11, FullBounds()),
				4, 4, published));
		}

		TEST_METHOD(RequestIsClampedBySafeAvailabilityAndMaximum)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(5);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishScope(timeline, 5, sequence);

			Assert::IsFalse(timeline.SubmitScheduledObservation(
				Identity(5, sequence++, 10), Trusted(10, FullBounds()),
				99, 1, published));
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				Identity(5, sequence++, 11), Trusted(11, FullBounds()),
				99, 1, published));
			Assert::AreEqual<uint8_t>(
				ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES,
				published.configuredLookahead);
			Assert::AreEqual<uint8_t>(1, published.effectiveLookahead);
		}

		TEST_METHOD(AcceptedSequenceGapBreaksSpeculativeBackdating)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(6);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishScope(timeline, 6, sequence);

			Assert::IsFalse(timeline.SubmitScheduledObservation(
				Identity(6, sequence, 10), Trusted(10, FullBounds()),
				8, 8, published));
			sequence += 2;
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				Identity(6, sequence++, 11), Trusted(11, FullBounds()),
				8, 8, published));
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				Identity(6, sequence++, 12), Trusted(12, FullBounds()),
				8, 8, published));
			Assert::AreEqual<uint64_t>(sequence - 2,
				published.effectiveIdentity.acceptedSequence);
		}
	};
}
