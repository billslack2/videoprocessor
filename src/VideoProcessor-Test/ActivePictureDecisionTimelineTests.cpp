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
			return { 0, 276, 3840, 1884, 3840, 2160, 2.388,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
		}

		ActivePictureBounds ShallowScopeBounds()
		{
			return { 0, 68, 3840, 2092, 3840, 2160, 1.8972,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
		}

		ActivePictureBounds Stable220Bounds()
		{
			return { 0, 208, 3840, 1952, 3840, 2160, 2.2018,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
		}

		ActivePictureBounds Candidate235Bounds()
		{
			return { 0, 260, 3840, 1896, 3840, 2160, 2.3472,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
		}

		ActivePictureBounds ShallowSideBounds()
		{
			return { 68, 0, 3772, 2160, 3840, 2160, 1.7148,
				ActivePictureBounds::BarAxes::LEFT_RIGHT };
		}

		ActivePictureBounds SideBounds()
		{
			return { 276, 0, 3564, 2160, 3840, 2160, 1.5222,
				ActivePictureBounds::BarAxes::LEFT_RIGHT };
		}

		ActivePictureBounds FullBounds()
		{
			return { 0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0,
				ActivePictureBounds::BarAxes::NONE };
		}

		ActivePictureObservation Trusted(
			uint64_t frame, const ActivePictureBounds& bounds)
		{
			ActivePictureObservation observation;
			observation.bounds = bounds;
			observation.frameNumber = frame;
			observation.available = true;
			observation.classification =
				bounds.left == 0 && bounds.top == 0 &&
				bounds.right == bounds.rasterWidth &&
				bounds.bottom == bounds.rasterHeight
				? ActivePictureClassification::FULL_RASTER_TRUSTED
				: ActivePictureClassification::BAR_CROP_TRUSTED;
			return observation;
		}

		ActivePictureFrameIdentity Identity(
			uint64_t generation, uint64_t sequence, uint64_t frame)
		{
			return { generation, sequence, frame, frame * 1000 };
		}

		ActivePictureFrameDecision ScheduledDecision(
			const ActivePictureFrameIdentity& currentIdentity,
			const ActivePictureBounds& bounds)
		{
			ActivePictureFrameDecision decision;
			decision.effectiveIdentity = currentIdentity;
			decision.observationIdentity = currentIdentity;
			++decision.observationIdentity.acceptedSequence;
			++decision.observationIdentity.sourceFrameNumber;
			decision.observationIdentity.captureTimestamp += 1000;
			decision.transition.publish = true;
			decision.transition.stable = true;
			decision.transition.bounds = bounds;
			return decision;
		}

		void EstablishGeometry(ActivePictureDecisionTimeline& timeline,
			uint64_t generation, uint64_t& sequence,
			const ActivePictureBounds& bounds)
		{
			ActivePictureFrameDecision published;
			for (uint64_t frame = 1;
				frame <= ActivePictureTransitionModel::INITIAL_CONFIRMATIONS;
				++frame)
			{
				const bool didPublish = timeline.SubmitScheduledObservation(
					Identity(generation, sequence++, frame),
					Trusted(frame, bounds), 0, 0, published);
				Assert::AreEqual(
					frame == ActivePictureTransitionModel::INITIAL_CONFIRMATIONS,
					didPublish);
			}
		}

		void EstablishScope(ActivePictureDecisionTimeline& timeline,
			uint64_t generation, uint64_t& sequence)
		{
			EstablishGeometry(timeline, generation, sequence, ScopeBounds());
		}

		void RecordLookaheadEvidence(
			ActivePictureDecisionTimeline& timeline,
			const ActivePictureFrameIdentity& identity,
			const ActivePictureObservation& observation,
			bool nearBlackEvaluated = true,
			bool nearBlack = false)
		{
			Assert::IsTrue(timeline.TrackLookaheadEvidence(
				identity, observation, nearBlackEvaluated, nearBlack));
		}

		ActivePictureFrameDecision PublishExactInwardDecision(
			ActivePictureDecisionTimeline& timeline,
			uint64_t generation,
			uint64_t& sequence,
			uint64_t firstFrame = 600)
		{
			ActivePictureFrameDecision published;
			EstablishGeometry(
				timeline, generation, sequence, ShallowScopeBounds());
			const auto candidate =
				Identity(generation, sequence++, firstFrame);
			const auto skipped =
				Identity(generation, sequence++, firstFrame + 1);
			const auto confirmation =
				Identity(generation, sequence++, firstFrame + 2);
			Assert::IsTrue(timeline.TrackAcceptedFrame(candidate));
			Assert::IsTrue(timeline.TrackAcceptedFrame(skipped));
			Assert::IsTrue(timeline.TrackAcceptedFrame(confirmation));
			RecordLookaheadEvidence(timeline, candidate,
				Trusted(firstFrame, ScopeBounds()));
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				candidate, Trusted(firstFrame, ScopeBounds()),
				5, 4, published));
			RecordLookaheadEvidence(timeline, skipped,
				Trusted(firstFrame + 1, ScopeBounds()));
			RecordLookaheadEvidence(timeline, confirmation,
				Trusted(firstFrame + 2, ScopeBounds()));
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				confirmation, Trusted(firstFrame + 2, ScopeBounds()),
				5, 4, published));
			Assert::AreEqual(
				static_cast<int>(ActivePictureDecisionAssociation::EXACT_INWARD),
				static_cast<int>(published.association));
			return published;
		}
	}

	TEST_CLASS(ActivePictureDecisionTimelineTests)
	{
	public:
		TEST_METHOD(ScheduledDecisionAcceptsOnlyCurrentTrustedContext)
		{
			const ActivePictureFrameIdentity current = {
				17, 23, 29, 31000, 37, 41, 17 };
			const ActivePictureFrameDecision decision =
				ScheduledDecision(current, FullBounds());
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureScheduledDecisionValidation::ACCEPTED),
				static_cast<int>(ValidateActivePictureScheduledDecision(
					decision, current, FullBounds(),
					ActivePictureClassification::FULL_RASTER_TRUSTED)));
		}

		TEST_METHOD(ScheduledDecisionRejectsStaleFormatViewportAndRenderer)
		{
			const ActivePictureFrameIdentity current = {
				17, 23, 29, 31000, 37, 41, 17 };
			for (int field = 0; field < 4; ++field)
			{
				ActivePictureFrameDecision decision =
					ScheduledDecision(current, FullBounds());
				auto changeContext = [field](
					ActivePictureFrameIdentity& identity)
				{
					switch (field)
					{
					case 0: ++identity.transportGeneration; break;
					case 1: ++identity.sourceFormatGeneration; break;
					case 2: ++identity.viewportGeneration; break;
					default: ++identity.rendererGeneration; break;
					}
				};

				changeContext(decision.observationIdentity);
				Assert::AreEqual(
					static_cast<int>(ActivePictureScheduledDecisionValidation::
						OBSERVATION_CONTEXT_MISMATCH),
					static_cast<int>(ValidateActivePictureScheduledDecision(
						decision, current, FullBounds(),
						ActivePictureClassification::FULL_RASTER_TRUSTED)));

				decision = ScheduledDecision(current, FullBounds());
				changeContext(decision.effectiveIdentity);
				Assert::AreEqual(
					static_cast<int>(ActivePictureScheduledDecisionValidation::
						EFFECTIVE_IDENTITY_MISMATCH),
					static_cast<int>(ValidateActivePictureScheduledDecision(
						decision, current, FullBounds(),
						ActivePictureClassification::FULL_RASTER_TRUSTED)));
			}
		}

		TEST_METHOD(ScheduledDecisionRejectsMismatchedOrProvisionalBounds)
		{
			const ActivePictureFrameIdentity current = {
				17, 23, 29, 31000, 37, 41, 17 };
			const ActivePictureFrameDecision decision =
				ScheduledDecision(current, FullBounds());
			Assert::AreEqual(
				static_cast<int>(ActivePictureScheduledDecisionValidation::
					TRUSTED_BOUNDS_MISMATCH),
				static_cast<int>(ValidateActivePictureScheduledDecision(
					decision, current, ScopeBounds(),
					ActivePictureClassification::BAR_CROP_TRUSTED)));
			Assert::AreEqual(
				static_cast<int>(ActivePictureScheduledDecisionValidation::
					NON_AUTHORITATIVE),
				static_cast<int>(ValidateActivePictureScheduledDecision(
					decision, current, FullBounds(),
					ActivePictureClassification::PROVISIONAL)));

			ActivePictureBounds sameCoordinatesWithoutAuthority = ScopeBounds();
			sameCoordinatesWithoutAuthority.trustedBarAxes =
				ActivePictureBounds::BarAxes::NONE;
			const ActivePictureFrameDecision scopeDecision =
				ScheduledDecision(current, ScopeBounds());
			Assert::AreEqual(
				static_cast<int>(ActivePictureScheduledDecisionValidation::
					TRUSTED_BOUNDS_MISMATCH),
				static_cast<int>(ValidateActivePictureScheduledDecision(
					scopeDecision, current, sameCoordinatesWithoutAuthority,
					ActivePictureClassification::BAR_CROP_TRUSTED)));
		}

		TEST_METHOD(RuntimeIdentityMatchRequiresEveryQueueEpochField)
		{
			ActivePictureFrameIdentity expected = {
				7, 11, 13, 17000, 19, 23, 29 };
			Assert::IsTrue(SameActivePictureFrameIdentity(expected, expected));

			for (int field = 0; field < 7; ++field)
			{
				ActivePictureFrameIdentity changed = expected;
				switch (field)
				{
				case 0: ++changed.transportGeneration; break;
				case 1: ++changed.acceptedSequence; break;
				case 2: ++changed.sourceFrameNumber; break;
				case 3: ++changed.captureTimestamp; break;
				case 4: ++changed.sourceFormatGeneration; break;
				case 5: ++changed.viewportGeneration; break;
				default: ++changed.rendererGeneration; break;
				}
				Assert::IsFalse(
					SameActivePictureFrameIdentity(expected, changed));
			}
		}

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
			Assert::AreEqual(
				static_cast<int>(ActivePictureDecisionAssociation::OUTWARD),
				static_cast<int>(published.association));
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

		TEST_METHOD(ExactInwardProofBackdatesRecordedEternalsReplay)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(20);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishGeometry(
				timeline, 20, sequence, ShallowScopeBounds());

			const auto candidate = Identity(20, sequence++, 602);
			const auto skipped = Identity(20, sequence++, 603);
			const auto confirmation = Identity(20, sequence++, 604);
			Assert::IsTrue(timeline.TrackAcceptedFrame(candidate));
			Assert::IsTrue(timeline.TrackAcceptedFrame(skipped));
			Assert::IsTrue(timeline.TrackAcceptedFrame(confirmation));
			RecordLookaheadEvidence(
				timeline, candidate, Trusted(602, ScopeBounds()));
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				candidate, Trusted(602, ScopeBounds()), 5, 4, published));
			RecordLookaheadEvidence(
				timeline, skipped, Trusted(603, ScopeBounds()));
			RecordLookaheadEvidence(
				timeline, confirmation, Trusted(604, ScopeBounds()));
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				confirmation, Trusted(604, ScopeBounds()), 5, 4, published));

			Assert::AreEqual(candidate.acceptedSequence,
				published.effectiveIdentity.acceptedSequence);
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureDecisionAssociation::EXACT_INWARD),
				static_cast<int>(published.association));
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureInwardProofValidation::ACCEPTED),
				static_cast<int>(published.inwardProof));
			Assert::AreEqual<uint8_t>(3, published.proofFrameCount);
			Assert::IsFalse(published.late);
			Assert::IsTrue(timeline.IsDecisionCurrent(published));
		}

		TEST_METHOD(UnscheduledEvidenceCannotAddAConfirmationVote)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(21);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishGeometry(
				timeline, 21, sequence, ShallowScopeBounds());

			const auto candidate = Identity(21, sequence++, 602);
			const auto skipped = Identity(21, sequence++, 603);
			const auto confirmation = Identity(21, sequence++, 604);
			Assert::IsTrue(timeline.TrackAcceptedFrame(candidate));
			Assert::IsTrue(timeline.TrackAcceptedFrame(skipped));
			Assert::IsTrue(timeline.TrackAcceptedFrame(confirmation));
			RecordLookaheadEvidence(
				timeline, candidate, Trusted(602, ScopeBounds()));
			RecordLookaheadEvidence(
				timeline, skipped, Trusted(603, ScopeBounds()));
			RecordLookaheadEvidence(
				timeline, confirmation, Trusted(604, ScopeBounds()));

			Assert::IsFalse(timeline.SubmitScheduledObservation(
				candidate, Trusted(602, ScopeBounds()), 5, 4, published));
		}

		TEST_METHOD(InwardProofRequiresEveryAcceptedFrameEvidence)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(22);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishGeometry(
				timeline, 22, sequence, ShallowScopeBounds());

			const auto candidate = Identity(22, sequence++, 602);
			const auto skipped = Identity(22, sequence++, 603);
			const auto confirmation = Identity(22, sequence++, 604);
			Assert::IsTrue(timeline.TrackAcceptedFrame(candidate));
			Assert::IsTrue(timeline.TrackAcceptedFrame(skipped));
			Assert::IsTrue(timeline.TrackAcceptedFrame(confirmation));
			RecordLookaheadEvidence(
				timeline, candidate, Trusted(602, ScopeBounds()));
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				candidate, Trusted(602, ScopeBounds()), 5, 4, published));
			RecordLookaheadEvidence(
				timeline, confirmation, Trusted(604, ScopeBounds()));
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				confirmation, Trusted(604, ScopeBounds()), 5, 4, published));

			Assert::AreEqual(confirmation.acceptedSequence,
				published.effectiveIdentity.acceptedSequence);
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureInwardProofValidation::MISSING_EVIDENCE),
				static_cast<int>(published.inwardProof));
			Assert::IsTrue(published.late);
		}

		TEST_METHOD(InwardProofRejectsUntrustedAndNearBlackFrames)
		{
			auto runReplay = [](
				ActivePictureObservation intervening,
				bool nearBlackEvaluated,
				bool nearBlack)
			{
				ActivePictureDecisionTimeline timeline;
				timeline.Reset(23);
				uint64_t sequence = 1;
				ActivePictureFrameDecision published;
				EstablishGeometry(
					timeline, 23, sequence, ShallowScopeBounds());
				const auto candidate = Identity(23, sequence++, 602);
				const auto skipped = Identity(23, sequence++, 603);
				const auto confirmation = Identity(23, sequence++, 604);
				Assert::IsTrue(timeline.TrackAcceptedFrame(candidate));
				Assert::IsTrue(timeline.TrackAcceptedFrame(skipped));
				Assert::IsTrue(timeline.TrackAcceptedFrame(confirmation));
				RecordLookaheadEvidence(
					timeline, candidate, Trusted(602, ScopeBounds()));
				Assert::IsFalse(timeline.SubmitScheduledObservation(
					candidate, Trusted(602, ScopeBounds()), 5, 4, published));
				RecordLookaheadEvidence(timeline, skipped, intervening,
					nearBlackEvaluated, nearBlack);
				RecordLookaheadEvidence(
					timeline, confirmation, Trusted(604, ScopeBounds()));
				Assert::IsTrue(timeline.SubmitScheduledObservation(
					confirmation, Trusted(604, ScopeBounds()), 5, 4, published));
				return published.inwardProof;
			};

			ActivePictureObservation provisional =
				Trusted(603, ScopeBounds());
			provisional.classification =
				ActivePictureClassification::PROVISIONAL;
			Assert::AreEqual(
				static_cast<int>(ActivePictureInwardProofValidation::
					EVIDENCE_NOT_TRUSTED),
				static_cast<int>(runReplay(provisional, true, false)));

			ActivePictureObservation unavailable;
			unavailable.frameNumber = 603;
			Assert::AreEqual(
				static_cast<int>(ActivePictureInwardProofValidation::
					EVIDENCE_NOT_TRUSTED),
				static_cast<int>(runReplay(unavailable, false, false)));

			Assert::AreEqual(
				static_cast<int>(ActivePictureInwardProofValidation::
					EVIDENCE_NOT_TRUSTED),
				static_cast<int>(runReplay(
					Trusted(603, FullBounds()), true, false)));
			Assert::AreEqual(
				static_cast<int>(ActivePictureInwardProofValidation::
					EVIDENCE_NOT_TRUSTED),
				static_cast<int>(runReplay(
					Trusted(603, ScopeBounds()), false, false)));
			Assert::AreEqual(
				static_cast<int>(ActivePictureInwardProofValidation::
					EVIDENCE_NEAR_BLACK),
				static_cast<int>(runReplay(
					Trusted(603, ScopeBounds()), true, true)));
		}

		TEST_METHOD(InwardProofRequiresExactBoundsRasterAndAxes)
		{
			auto runReplay = [](const ActivePictureBounds& interveningBounds)
			{
				ActivePictureDecisionTimeline timeline;
				timeline.Reset(24);
				uint64_t sequence = 1;
				ActivePictureFrameDecision published;
				EstablishGeometry(
					timeline, 24, sequence, ShallowScopeBounds());
				const auto candidate = Identity(24, sequence++, 602);
				const auto skipped = Identity(24, sequence++, 603);
				const auto confirmation = Identity(24, sequence++, 604);
				Assert::IsTrue(timeline.TrackAcceptedFrame(candidate));
				Assert::IsTrue(timeline.TrackAcceptedFrame(skipped));
				Assert::IsTrue(timeline.TrackAcceptedFrame(confirmation));
				RecordLookaheadEvidence(
					timeline, candidate, Trusted(602, ScopeBounds()));
				Assert::IsFalse(timeline.SubmitScheduledObservation(
					candidate, Trusted(602, ScopeBounds()), 5, 4, published));
				RecordLookaheadEvidence(timeline, skipped,
					Trusted(603, interveningBounds));
				RecordLookaheadEvidence(
					timeline, confirmation, Trusted(604, ScopeBounds()));
				Assert::IsTrue(timeline.SubmitScheduledObservation(
					confirmation, Trusted(604, ScopeBounds()), 5, 4, published));
				return published.inwardProof;
			};

			ActivePictureBounds shifted = ScopeBounds();
			shifted.top += 2;
			ActivePictureBounds raster = ScopeBounds();
			++raster.rasterHeight;
			ActivePictureBounds axes = ScopeBounds();
			axes.trustedBarAxes = ActivePictureBounds::BarAxes::BOTH;
			for (const ActivePictureBounds& mismatch :
				{ shifted, raster, axes })
			{
				Assert::AreEqual(
					static_cast<int>(ActivePictureInwardProofValidation::
						EVIDENCE_BOUNDS_MISMATCH),
					static_cast<int>(runReplay(mismatch)));
			}
		}

		TEST_METHOD(InwardProofRequiresConfiguredAndAvailableLead)
		{
			auto runReplay = [](uint8_t configured, uint8_t available)
			{
				ActivePictureDecisionTimeline timeline;
				timeline.Reset(25);
				uint64_t sequence = 1;
				ActivePictureFrameDecision published;
				EstablishGeometry(
					timeline, 25, sequence, ShallowScopeBounds());
				const auto candidate = Identity(25, sequence++, 602);
				const auto skipped = Identity(25, sequence++, 603);
				const auto confirmation = Identity(25, sequence++, 604);
				Assert::IsTrue(timeline.TrackAcceptedFrame(candidate));
				Assert::IsTrue(timeline.TrackAcceptedFrame(skipped));
				Assert::IsTrue(timeline.TrackAcceptedFrame(confirmation));
				RecordLookaheadEvidence(
					timeline, candidate, Trusted(602, ScopeBounds()));
				Assert::IsFalse(timeline.SubmitScheduledObservation(
					candidate, Trusted(602, ScopeBounds()),
					configured, available, published));
				RecordLookaheadEvidence(
					timeline, skipped, Trusted(603, ScopeBounds()));
				RecordLookaheadEvidence(
					timeline, confirmation, Trusted(604, ScopeBounds()));
				Assert::IsTrue(timeline.SubmitScheduledObservation(
					confirmation, Trusted(604, ScopeBounds()),
					configured, available, published));
				return published;
			};

			for (const ActivePictureFrameDecision& published :
				{ runReplay(1, 4), runReplay(5, 1) })
			{
				Assert::AreEqual(
					static_cast<int>(ActivePictureInwardProofValidation::
						INSUFFICIENT_LEAD),
					static_cast<int>(published.inwardProof));
				Assert::AreEqual(
					published.observationIdentity.acceptedSequence,
					published.effectiveIdentity.acceptedSequence);
			}
		}

		TEST_METHOD(MixedDirectionCropCannotUseExactInwardProof)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(26);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishGeometry(
				timeline, 26, sequence, ShallowScopeBounds());
			ActivePictureBounds mixed = ScopeBounds();
			mixed.bottom = 2100;
			mixed.aspectRatio = 3840.0 / (mixed.bottom - mixed.top);
			const auto candidate = Identity(26, sequence++, 602);
			const auto confirmation = Identity(26, sequence++, 603);
			Assert::IsTrue(timeline.TrackAcceptedFrame(candidate));
			Assert::IsTrue(timeline.TrackAcceptedFrame(confirmation));
			RecordLookaheadEvidence(
				timeline, candidate, Trusted(602, mixed));
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				candidate, Trusted(602, mixed), 5, 4, published));
			RecordLookaheadEvidence(
				timeline, confirmation, Trusted(603, mixed));
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				confirmation, Trusted(603, mixed), 5, 4, published));

			Assert::AreEqual(
				static_cast<int>(ActivePictureDecisionAssociation::CONFIRMATION),
				static_cast<int>(published.association));
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureInwardProofValidation::NOT_APPLICABLE),
				static_cast<int>(published.inwardProof));
			Assert::IsTrue(published.late);
		}

		TEST_METHOD(BothAxesCropCannotUseExactInwardProof)
		{
			ActivePictureBounds stable = ShallowScopeBounds();
			stable.left = 20;
			stable.right = 3820;
			stable.trustedBarAxes = ActivePictureBounds::BarAxes::BOTH;
			stable.aspectRatio = static_cast<double>(
				stable.right - stable.left) / (stable.bottom - stable.top);
			ActivePictureBounds target = ScopeBounds();
			target.left = 40;
			target.right = 3800;
			target.trustedBarAxes = ActivePictureBounds::BarAxes::BOTH;
			target.aspectRatio = static_cast<double>(
				target.right - target.left) / (target.bottom - target.top);

			ActivePictureDecisionTimeline timeline;
			timeline.Reset(30);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishGeometry(timeline, 30, sequence, stable);
			const auto candidate = Identity(30, sequence++, 700);
			const auto confirmation = Identity(30, sequence++, 701);
			Assert::IsTrue(timeline.TrackAcceptedFrame(candidate));
			Assert::IsTrue(timeline.TrackAcceptedFrame(confirmation));
			RecordLookaheadEvidence(
				timeline, candidate, Trusted(700, target));
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				candidate, Trusted(700, target), 5, 4, published));
			RecordLookaheadEvidence(
				timeline, confirmation, Trusted(701, target));
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				confirmation, Trusted(701, target), 5, 4, published));

			Assert::AreEqual(
				static_cast<int>(ActivePictureDecisionAssociation::CONFIRMATION),
				static_cast<int>(published.association));
			Assert::AreEqual(confirmation.acceptedSequence,
				published.effectiveIdentity.acceptedSequence);
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureInwardProofValidation::NOT_APPLICABLE),
				static_cast<int>(published.inwardProof));
		}

		TEST_METHOD(OrthogonalInsetCannotUseSingleAxisInwardProof)
		{
			ActivePictureTransitionDecision vertical;
			vertical.stableBounds = ShallowScopeBounds();
			vertical.bounds = ScopeBounds();
			vertical.bounds.left = 10;
			vertical.bounds.right = 3830;
			Assert::IsFalse(IsExactInwardActivePictureAssociationGeometry(
				vertical, ActivePictureClassification::BAR_CROP_TRUSTED));

			ActivePictureTransitionDecision horizontal;
			horizontal.stableBounds = ShallowSideBounds();
			horizontal.bounds = SideBounds();
			horizontal.bounds.top = 10;
			horizontal.bounds.bottom = 2150;
			Assert::IsFalse(IsExactInwardActivePictureAssociationGeometry(
				horizontal, ActivePictureClassification::BAR_CROP_TRUSTED));
		}

		TEST_METHOD(LeftRightExactInwardProofBackdatesCandidate)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(35);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishGeometry(
				timeline, 35, sequence, ShallowSideBounds());
			const auto candidate = Identity(35, sequence++, 720);
			const auto skipped = Identity(35, sequence++, 721);
			const auto confirmation = Identity(35, sequence++, 722);
			Assert::IsTrue(timeline.TrackAcceptedFrame(candidate));
			Assert::IsTrue(timeline.TrackAcceptedFrame(skipped));
			Assert::IsTrue(timeline.TrackAcceptedFrame(confirmation));
			RecordLookaheadEvidence(
				timeline, candidate, Trusted(720, SideBounds()));
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				candidate, Trusted(720, SideBounds()), 5, 4, published));
			RecordLookaheadEvidence(
				timeline, skipped, Trusted(721, SideBounds()));
			RecordLookaheadEvidence(
				timeline, confirmation, Trusted(722, SideBounds()));
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				confirmation, Trusted(722, SideBounds()), 5, 4, published));

			Assert::AreEqual(candidate.acceptedSequence,
				published.effectiveIdentity.acceptedSequence);
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureDecisionAssociation::EXACT_INWARD),
				static_cast<int>(published.association));
		}

		TEST_METHOD(LaterContinuityBreakPreservesEarlierProvenDecision)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(27);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishGeometry(
				timeline, 27, sequence, ShallowScopeBounds());
			const auto candidate = Identity(27, sequence++, 602);
			const auto confirmation = Identity(27, sequence++, 603);
			Assert::IsTrue(timeline.TrackAcceptedFrame(candidate));
			Assert::IsTrue(timeline.TrackAcceptedFrame(confirmation));
			RecordLookaheadEvidence(
				timeline, candidate, Trusted(602, ScopeBounds()));
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				candidate, Trusted(602, ScopeBounds()), 5, 4, published));
			RecordLookaheadEvidence(
				timeline, confirmation, Trusted(603, ScopeBounds()));
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				confirmation, Trusted(603, ScopeBounds()), 5, 4, published));
			Assert::IsTrue(timeline.IsDecisionCurrent(published));

			timeline.BreakContinuity(604);
			Assert::IsTrue(timeline.IsDecisionCurrent(published));
		}

		TEST_METHOD(LookaheadDepthChangesFromFiveInvalidateQueuedDecision)
		{
			for (const uint8_t reducedDepth : { uint8_t{ 0 }, uint8_t{ 1 } })
			{
				ActivePictureDecisionTimeline timeline;
				timeline.Reset(31 + reducedDepth);
				uint64_t sequence = 1;
				const ActivePictureFrameDecision published =
					PublishExactInwardDecision(
						timeline, 31 + reducedDepth, sequence);
				Assert::IsTrue(timeline.IsDecisionCurrent(published));

				// The renderer calls this while changing the configured depth.
				timeline.InvalidateLookaheadPolicy(reducedDepth == 0);
				Assert::IsFalse(timeline.IsDecisionCurrent(published));
			}
		}

		TEST_METHOD(LookaheadDepthChangePreservesVotesCadenceAndProofEvidence)
		{
			for (const uint8_t targetDepth : { uint8_t{ 1 }, uint8_t{ 4 } })
			{
				const uint64_t generation = 36 + targetDepth;
				ActivePictureDecisionTimeline baseline;
				ActivePictureDecisionTimeline changed;
				baseline.Reset(generation);
				changed.Reset(generation);
				uint64_t baselineSequence = 1;
				uint64_t changedSequence = 1;
				EstablishGeometry(baseline, generation, baselineSequence,
					ShallowScopeBounds());
				EstablishGeometry(changed, generation, changedSequence,
					ShallowScopeBounds());

				const auto baselineCandidate =
					Identity(generation, baselineSequence++, 740);
				const auto changedCandidate =
					Identity(generation, changedSequence++, 740);
				Assert::IsTrue(
					baseline.TrackAcceptedFrame(baselineCandidate));
				Assert::IsTrue(changed.TrackAcceptedFrame(changedCandidate));
				RecordLookaheadEvidence(baseline, baselineCandidate,
					Trusted(740, ScopeBounds()));
				RecordLookaheadEvidence(changed, changedCandidate,
					Trusted(740, ScopeBounds()));
				ActivePictureFrameDecision baselineDecision;
				ActivePictureFrameDecision changedDecision;
				Assert::IsFalse(baseline.SubmitScheduledObservation(
					baselineCandidate, Trusted(740, ScopeBounds()),
					targetDepth, 1, baselineDecision));
				Assert::IsFalse(changed.SubmitScheduledObservation(
					changedCandidate, Trusted(740, ScopeBounds()),
					5, 4, changedDecision));

				changed.InvalidateLookaheadPolicy();

				const auto baselineConfirmation =
					Identity(generation, baselineSequence++, 741);
				const auto changedConfirmation =
					Identity(generation, changedSequence++, 741);
				Assert::IsTrue(
					baseline.TrackAcceptedFrame(baselineConfirmation));
				Assert::IsTrue(
					changed.TrackAcceptedFrame(changedConfirmation));
				RecordLookaheadEvidence(baseline, baselineConfirmation,
					Trusted(741, ScopeBounds()));
				RecordLookaheadEvidence(changed, changedConfirmation,
					Trusted(741, ScopeBounds()));
				const bool baselinePublished =
					baseline.SubmitScheduledObservation(
						baselineConfirmation, Trusted(741, ScopeBounds()),
						targetDepth, 1, baselineDecision);
				const bool changedPublished =
					changed.SubmitScheduledObservation(
						changedConfirmation, Trusted(741, ScopeBounds()),
						targetDepth, 1, changedDecision);

				Assert::AreEqual(baselinePublished, changedPublished);
				Assert::IsTrue(changedPublished);
				Assert::AreEqual(baselineDecision.transition.bounds.top,
					changedDecision.transition.bounds.top);
				Assert::AreEqual(
					static_cast<int>(baselineDecision.transition.state),
					static_cast<int>(changedDecision.transition.state));
				Assert::AreEqual(
					static_cast<int>(baselineDecision.association),
					static_cast<int>(changedDecision.association));
				Assert::AreEqual(
					static_cast<int>(
						ActivePictureDecisionAssociation::EXACT_INWARD),
					static_cast<int>(changedDecision.association));
			}
		}

		TEST_METHOD(LookaheadZeroCrossingCannotReuseStaleCandidate)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(39);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishGeometry(
				timeline, 39, sequence, ShallowScopeBounds());
			const auto staleCandidate = Identity(39, sequence++, 760);
			Assert::IsTrue(timeline.TrackAcceptedFrame(staleCandidate));
			RecordLookaheadEvidence(
				timeline, staleCandidate, Trusted(760, ScopeBounds()));
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				staleCandidate, Trusted(760, ScopeBounds()), 5, 4, published));

			timeline.InvalidateLookaheadPolicy(true); // 5 -> 0
			timeline.InvalidateLookaheadPolicy(true); // 0 -> 5

			const auto freshCandidate = Identity(39, sequence++, 761);
			Assert::IsTrue(timeline.TrackAcceptedFrame(freshCandidate));
			RecordLookaheadEvidence(
				timeline, freshCandidate, Trusted(761, ScopeBounds()));
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				freshCandidate, Trusted(761, ScopeBounds()), 5, 4, published));
			const auto confirmation = Identity(39, sequence++, 762);
			Assert::IsTrue(timeline.TrackAcceptedFrame(confirmation));
			RecordLookaheadEvidence(
				timeline, confirmation, Trusted(762, ScopeBounds()));
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				confirmation, Trusted(762, ScopeBounds()), 5, 4, published));

			Assert::AreNotEqual(staleCandidate.acceptedSequence,
				published.effectiveIdentity.acceptedSequence);
			Assert::AreEqual(freshCandidate.acceptedSequence,
				published.effectiveIdentity.acceptedSequence);
		}

		TEST_METHOD(LaterContinuityWiringPathsPreserveEarlierProvenDecision)
		{
			for (int path = 0; path < 4; ++path)
			{
				const uint64_t generation = 40 + path;
				ActivePictureDecisionTimeline timeline;
				timeline.Reset(generation);
				uint64_t sequence = 1;
				const ActivePictureFrameDecision published =
					PublishExactInwardDecision(
						timeline, generation, sequence);
				Assert::IsTrue(timeline.IsDecisionCurrent(published));
				ActivePictureFrameIdentity next =
					Identity(generation, sequence, 800 + path);
				switch (path)
				{
				case 0:
					next.acceptedSequence += 1;
					Assert::IsTrue(timeline.TrackAcceptedFrame(next));
					break;
				case 1:
					next.sourceFormatGeneration = 1;
					Assert::IsTrue(timeline.TrackAcceptedFrame(next));
					break;
				case 2:
					next.viewportGeneration = 1;
					Assert::IsTrue(timeline.TrackAcceptedFrame(next));
					break;
				default:
					next.rendererGeneration = 1;
					Assert::IsTrue(timeline.TrackAcceptedFrame(next));
					break;
				}
				Assert::IsTrue(timeline.IsDecisionCurrent(published));
			}
		}

		TEST_METHOD(DiscardAtTargetAndTransportResetInvalidateQueuedDecision)
		{
			for (int path = 0; path < 2; ++path)
			{
				const uint64_t generation = 46 + path;
				ActivePictureDecisionTimeline timeline;
				timeline.Reset(generation);
				uint64_t sequence = 1;
				const ActivePictureFrameDecision published =
					PublishExactInwardDecision(
						timeline, generation, sequence);
				Assert::IsTrue(timeline.IsDecisionCurrent(published));
				if (path == 0)
				{
					timeline.MarkDiscarded(
						published.effectiveIdentity, 900);
				}
				else
				{
					timeline.Reset(generation + 100);
				}
				Assert::IsFalse(timeline.IsDecisionCurrent(published));
			}
		}

		TEST_METHOD(InvalidDecisionCannotBeResurrectedByLaterBoundary)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(49);
			uint64_t sequence = 1;
			const ActivePictureFrameDecision published =
				PublishExactInwardDecision(timeline, 49, sequence);
			ActivePictureFrameIdentity insideDecision =
				published.effectiveIdentity;
			++insideDecision.acceptedSequence;
			++insideDecision.sourceFrameNumber;
			insideDecision.captureTimestamp += 1000;
			timeline.MarkDiscarded(insideDecision, 850);
			Assert::IsFalse(timeline.IsDecisionCurrent(published));

			timeline.BreakContinuity(851);
			Assert::IsFalse(timeline.IsDecisionCurrent(published));
		}

		TEST_METHOD(DelayedPreviewCannotRebuildProofAcrossDiscontinuity)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(50);
			uint64_t sequence = 1;
			ActivePictureFrameDecision published;
			EstablishGeometry(
				timeline, 50, sequence, ShallowScopeBounds());
			const auto beforeBreak = Identity(50, sequence++, 602);
			Assert::IsTrue(timeline.TrackAcceptedFrame(beforeBreak));
			timeline.BreakContinuity(603);
			const auto afterBreak = Identity(50, sequence++, 603);
			const auto confirmation = Identity(50, sequence++, 604);
			Assert::IsTrue(timeline.TrackAcceptedFrame(afterBreak));
			Assert::IsTrue(timeline.TrackAcceptedFrame(confirmation));

			Assert::IsFalse(timeline.TrackLookaheadEvidence(
				beforeBreak, Trusted(602, ScopeBounds()), true, false));
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				beforeBreak, Trusted(602, ScopeBounds()), 5, 4, published));
			RecordLookaheadEvidence(
				timeline, afterBreak, Trusted(603, ScopeBounds()));
			Assert::IsFalse(timeline.SubmitScheduledObservation(
				afterBreak, Trusted(603, ScopeBounds()), 5, 4, published));
			RecordLookaheadEvidence(
				timeline, confirmation, Trusted(604, ScopeBounds()));
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				confirmation, Trusted(604, ScopeBounds()), 5, 4, published));

			Assert::AreNotEqual(beforeBreak.acceptedSequence,
				published.effectiveIdentity.acceptedSequence);
			Assert::IsTrue(timeline.IsDecisionCurrent(published));
		}

		TEST_METHOD(ConflictingDuplicateEvidenceIsRejected)
		{
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(28);
			const auto identity = Identity(28, 1, 1);
			Assert::IsTrue(timeline.TrackAcceptedFrame(identity));
			const ActivePictureObservation trusted =
				Trusted(1, ScopeBounds());
			RecordLookaheadEvidence(timeline, identity, trusted);
			Assert::IsTrue(timeline.TrackLookaheadEvidence(
				identity, trusted, true, false));
			Assert::IsFalse(timeline.TrackLookaheadEvidence(
				identity, Trusted(1, ShallowScopeBounds()), true, false));
		}

		TEST_METHOD(ExactProofDoesNotChangeTransitionDecisionSetOrOrder)
		{
			ActivePictureDecisionTimeline baseline;
			ActivePictureDecisionTimeline proven;
			baseline.Reset(29);
			proven.Reset(29);
			uint64_t baselineSequence = 1;
			uint64_t provenSequence = 1;
			EstablishGeometry(
				baseline, 29, baselineSequence, Stable220Bounds());
			EstablishGeometry(
				proven, 29, provenSequence, Stable220Bounds());

			std::vector<ActivePictureBounds> replay = {
				Candidate235Bounds(), Candidate235Bounds(),
				Candidate235Bounds(), Candidate235Bounds(),
				Stable220Bounds(), Stable220Bounds() };
			uint64_t frame = 100;
			for (const ActivePictureBounds& bounds : replay)
			{
				const auto baselineIdentity =
					Identity(29, baselineSequence++, frame);
				const auto provenIdentity =
					Identity(29, provenSequence++, frame);
				ActivePictureFrameDecision baselineDecision;
				ActivePictureFrameDecision provenDecision;
				Assert::IsTrue(proven.TrackAcceptedFrame(provenIdentity));
				RecordLookaheadEvidence(
					proven, provenIdentity, Trusted(frame, bounds));
				const bool baselinePublished =
					baseline.SubmitScheduledObservation(
						baselineIdentity, Trusted(frame, bounds),
						0, 0, baselineDecision);
				const bool provenPublished =
					proven.SubmitScheduledObservation(
						provenIdentity, Trusted(frame, bounds),
						5, 5, provenDecision);
				Assert::AreEqual(baselinePublished, provenPublished);
				if (baselinePublished)
				{
					Assert::AreEqual(
						baselineDecision.transition.bounds.left,
						provenDecision.transition.bounds.left);
					Assert::AreEqual(
						baselineDecision.transition.bounds.top,
						provenDecision.transition.bounds.top);
					Assert::AreEqual(
						baselineDecision.transition.bounds.right,
						provenDecision.transition.bounds.right);
					Assert::AreEqual(
						baselineDecision.transition.bounds.bottom,
						provenDecision.transition.bounds.bottom);
					Assert::AreEqual(
						static_cast<int>(baselineDecision.transition.state),
						static_cast<int>(provenDecision.transition.state));
				}
				++frame;
			}
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
