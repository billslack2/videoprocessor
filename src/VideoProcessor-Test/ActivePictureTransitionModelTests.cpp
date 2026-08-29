#include "pch.h"

#include <ActivePictureTransitionModel.h>
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
	namespace
	{
		ActivePictureBounds ScopeBounds()
		{
			return { 0, 264, 3840, 1896, 3840, 2160,
				3840.0 / 1632.0,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
		}

		ActivePictureBounds ImaxBounds()
		{
			return { 0, 48, 3840, 2112, 3840, 2160,
				3840.0 / 2064.0,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
		}

		ActivePictureBounds FourByThreeBounds()
		{
			return { 480, 0, 3360, 2160, 3840, 2160,
				4.0 / 3.0,
				ActivePictureBounds::BarAxes::LEFT_RIGHT };
		}

		ActivePictureBounds LoggedScopeBounds()
		{
			return { 0, 276, 3840, 1884, 3840, 2160,
				3840.0 / 1608.0,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
		}

		ActivePictureBounds LoggedWindowboxBounds()
		{
			return { 492, 276, 3348, 1884, 3840, 2160,
				2856.0 / 1608.0,
				ActivePictureBounds::BarAxes::BOTH };
		}

		ActivePictureTransitionDecision Observe(
			ActivePictureTransitionModel& model,
			const ActivePictureBounds& bounds,
			uint64_t frameNumber,
			ActivePictureClassification classification =
				ActivePictureClassification::BAR_CROP_TRUSTED,
			double framesPerSecond = 60.0)
		{
			return model.Observe({
				bounds, frameNumber, true, classification, framesPerSecond });
		}

		uint64_t Establish(
			ActivePictureTransitionModel& model,
			const ActivePictureBounds& bounds,
			uint64_t interval = 1)
		{
			uint64_t frame = 1;
			for (uint8_t count = 0;
				count < ActivePictureTransitionModel::INITIAL_CONFIRMATIONS;
				++count, frame += interval)
				Observe(model, bounds, frame);
			return frame;
		}
	}

	TEST_CLASS(ActivePictureTransitionModelTests)
	{
	public:
		TEST_METHOD(LookaheadPublicationBecomesTheLiveStableGeometry)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());

			ActivePictureTransitionModel preview;
			Establish(preview, ScopeBounds());
			Observe(preview, ImaxBounds(), frame++);
			const ActivePictureTransitionDecision published =
				Observe(preview, ImaxBounds(), frame++);
			Assert::IsTrue(published.publish);
			Assert::IsTrue(model.AdoptPublishedDecision(published,
				ActivePictureClassification::BAR_CROP_TRUSTED));

			const ActivePictureTransitionDecision next =
				Observe(model, ImaxBounds(), frame);
			Assert::IsFalse(next.publish);
			Assert::IsTrue(next.stable);
			Assert::AreEqual(ImaxBounds().top, next.bounds.top);
			Assert::AreEqual(ImaxBounds().bottom, next.bounds.bottom);
		}

		TEST_METHOD(LookaheadCannotInjectProvisionalGeometry)
		{
			ActivePictureTransitionModel model;
			ActivePictureTransitionDecision decision;
			decision.publish = true;
			decision.stable = true;
			decision.bounds = ImaxBounds();
			Assert::IsFalse(model.AdoptPublishedDecision(decision,
				ActivePictureClassification::PROVISIONAL));
			Assert::IsFalse(model.Observe({}).stable);
		}

		TEST_METHOD(AnalysisCadenceIsNormalizedAcrossFrameRateFamilies)
		{
			const double rates[] = {
				23.976, 24.0, 25.0, 29.97, 30.0, 59.94, 60.0
			};
			for (const double rate : rates)
			{
				const uint64_t interval =
					ActivePictureTransitionModel::AnalysisIntervalFrames(rate);
				Assert::IsTrue(interval >= 1);
				Assert::IsTrue(
					static_cast<double>(interval) / rate <= 0.1001);
			}
		}

		TEST_METHOD(AnalysisCadenceRejectsRepeatedOrOutOfOrderSourceSequence)
		{
			ActivePictureTransitionModel model;
			Assert::IsTrue(model.ShouldAnalyze(100, 60.0));
			Assert::IsFalse(model.ShouldAnalyze(100, 60.0));
			Assert::IsFalse(model.ShouldAnalyze(99, 60.0));
			Assert::IsTrue(model.ShouldAnalyze(105, 60.0));
		}

		TEST_METHOD(InitialGeometryRequiresFourConsistentObservations)
		{
			ActivePictureTransitionModel model;
			for (uint8_t count = 1;
				count < ActivePictureTransitionModel::INITIAL_CONFIRMATIONS;
				++count)
			{
				const auto decision = Observe(model, ScopeBounds(), count);
				Assert::IsFalse(decision.publish);
				Assert::AreEqual(
					static_cast<unsigned int>(count),
					static_cast<unsigned int>(decision.matchingCandidates));
			}
			const auto decision = Observe(
				model, ScopeBounds(),
				ActivePictureTransitionModel::INITIAL_CONFIRMATIONS);
			Assert::IsTrue(decision.publish);
			Assert::IsTrue(decision.stable);
		}

		TEST_METHOD(ClearScopeImaxCutsRequireTwoTrustedObservations)
		{
			const double rates[] = {
				23.976, 24.0, 25.0, 29.97, 30.0, 59.94, 60.0
			};
			for (const double rate : rates)
			{
				const ActivePictureBounds before[] = {
					ScopeBounds(), ImaxBounds()
				};
				const ActivePictureBounds after[] = {
					ImaxBounds(), ScopeBounds()
				};
				for (int direction = 0; direction < 2; ++direction)
				{
					ActivePictureTransitionModel model;
					const uint64_t interval =
						ActivePictureTransitionModel::
							AnalysisIntervalFrames(rate);
					const uint64_t firstFrame =
						Establish(model, before[direction], interval);

					const auto probing =
						Observe(model, after[direction], firstFrame);
					Assert::IsFalse(probing.publish);
					Assert::IsTrue(probing.stable);
					Assert::IsFalse(probing.clearTransition);

					const auto stable = Observe(
						model, after[direction], firstFrame + interval);
					Assert::IsTrue(stable.publish);
					Assert::IsTrue(stable.stable);
					Assert::IsTrue(
						static_cast<double>(
							stable.decisionLatencyFrames) /
							rate <= 0.1001);
				}
			}
		}

		TEST_METHOD(ThreeSecondNestedWindowboxBlipRetainsScopeAtEveryFrameRate)
		{
			const double rates[] = {
				23.976, 24.0, 25.0, 29.97, 30.0, 59.94, 60.0
			};
			for (const double rate : rates)
			{
				ActivePictureTransitionModel model;
				uint64_t frame = Establish(model, LoggedScopeBounds());
				const uint64_t blipFrames = static_cast<uint64_t>(
					std::ceil(rate * 3.0));
				for (uint64_t count = 0; count < blipFrames;
					++count, ++frame)
				{
					const auto decision = Observe(model,
						LoggedWindowboxBounds(), frame,
						ActivePictureClassification::BAR_CROP_TRUSTED,
						rate);
					Assert::IsFalse(decision.publish);
					Assert::IsTrue(decision.stable);
					Assert::AreEqual(LoggedScopeBounds().left,
						decision.stableBounds.left);
					Assert::AreEqual(LoggedScopeBounds().right,
						decision.stableBounds.right);
				}

				const auto recovered = Observe(model,
					LoggedScopeBounds(), frame,
					ActivePictureClassification::BAR_CROP_TRUSTED,
					rate);
				Assert::IsFalse(recovered.publish);
				Assert::IsTrue(recovered.stable);

				Assert::IsFalse(Observe(model, ImaxBounds(), ++frame,
					ActivePictureClassification::BAR_CROP_TRUSTED,
					rate).publish);
				Assert::IsTrue(Observe(model, ImaxBounds(), ++frame,
					ActivePictureClassification::BAR_CROP_TRUSTED,
					rate).publish);
				const auto recurrence = Observe(model,
					LoggedWindowboxBounds(), ++frame,
					ActivePictureClassification::PROVISIONAL, rate);
				Assert::AreEqual(std::string(
					"provisional geometry lacks affirmative crop authority"),
					recurrence.reason);
			}
		}

		TEST_METHOD(PersistentNestedWindowboxEventuallyCommitsByFourPointTwoSeconds)
		{
			const double rates[] = {
				23.976, 24.0, 25.0, 29.97, 30.0, 59.94, 60.0
			};
			for (const double rate : rates)
			{
				ActivePictureTransitionModel model;
				uint64_t frame = Establish(model, LoggedScopeBounds());
				const uint64_t firstCandidate = frame;
				bool published = false;
				while (!published &&
					frame - firstCandidate <= static_cast<uint64_t>(
						std::ceil(rate * 4.2)))
				{
					const auto decision = Observe(model,
						LoggedWindowboxBounds(), frame++,
						ActivePictureClassification::BAR_CROP_TRUSTED,
						rate);
					published = decision.publish;
				}
				Assert::IsTrue(published);
			}
		}

		TEST_METHOD(ProvisionalRecentWindowboxRestoresAuthorityAndStillWaits)
		{
			constexpr double rate = 60.0;
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, LoggedScopeBounds());
			bool published = false;
			while (!published)
			{
				published = Observe(model, LoggedWindowboxBounds(), frame++,
					ActivePictureClassification::BAR_CROP_TRUSTED,
					rate).publish;
			}

			Assert::IsFalse(Observe(model, LoggedScopeBounds(), frame++,
				ActivePictureClassification::BAR_CROP_TRUSTED,
				rate).publish);
			Assert::IsTrue(Observe(model, LoggedScopeBounds(), frame++,
				ActivePictureClassification::BAR_CROP_TRUSTED,
				rate).publish);

			ActivePictureBounds provisionalWindowbox =
				LoggedWindowboxBounds();
			provisionalWindowbox.trustedBarAxes =
				ActivePictureBounds::BarAxes::NONE;
			for (int count = 0; count < 180; ++count, ++frame)
			{
				const auto decision = Observe(model, provisionalWindowbox,
					frame, ActivePictureClassification::PROVISIONAL, rate);
				Assert::IsFalse(decision.publish);
				Assert::AreEqual(LoggedScopeBounds().left,
					decision.stableBounds.left);
				Assert::AreEqual(LoggedScopeBounds().right,
					decision.stableBounds.right);
				Assert::AreEqual(std::string(
					"recent nested crop awaiting sustained confirmation"),
					decision.reason);
			}
		}

		TEST_METHOD(BriefBlackFadePreservesLastStableMapping)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());
			for (int count = 0; count < 8; ++count, ++frame)
			{
				const auto decision =
					model.Observe({ {}, frame, false });
				Assert::IsFalse(decision.publish);
				Assert::IsTrue(decision.stable);
			}
			const auto recovered = Observe(model, ScopeBounds(), frame);
			Assert::IsFalse(recovered.publish);
			Assert::IsTrue(recovered.stable);
		}

		TEST_METHOD(SubtitleLogoAndEdgeIntrusionsDoNotChangeMode)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());
			ActivePictureBounds intrusion = ScopeBounds();
			intrusion.bottom -= 28;
			intrusion.aspectRatio =
				static_cast<double>(intrusion.right - intrusion.left) /
				(intrusion.bottom - intrusion.top);
			intrusion.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;

			for (int count = 0; count < 12; ++count, ++frame)
			{
				const ActivePictureBounds bounds =
					count % 2 == 0 ? intrusion : ScopeBounds();
				const auto decision = Observe(model, bounds, frame);
				Assert::IsFalse(decision.publish);
				Assert::IsTrue(decision.stable);
			}
		}

		TEST_METHOD(PersistentLocalizedOverlayNeverBecomesProgramAspect)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());
			ActivePictureBounds overlayEnvelope = ScopeBounds();
			overlayEnvelope.top = 80;
			overlayEnvelope.aspectRatio =
				static_cast<double>(overlayEnvelope.right - overlayEnvelope.left) /
				(overlayEnvelope.bottom - overlayEnvelope.top);
			overlayEnvelope.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;

			// Duration alone cannot promote a localized/asymmetric overlay to
			// program-aspect authority. The presentation layer may fit this
			// envelope, while NLS retains the stable scope geometry.
			for (int count = 0; count < 60; ++count, ++frame)
			{
				const auto decision = Observe(model, overlayEnvelope, frame,
					ActivePictureClassification::PROVISIONAL);
				Assert::IsFalse(decision.publish);
				Assert::IsTrue(decision.stable);
				Assert::AreEqual(ScopeBounds().top,
					decision.stableBounds.top);
				Assert::AreEqual(ScopeBounds().bottom,
					decision.stableBounds.bottom);
			}

			const auto recovered = Observe(model, ScopeBounds(), frame);
			Assert::IsFalse(recovered.publish);
			Assert::IsTrue(recovered.stable);
			Assert::AreEqual(ScopeBounds().top, recovered.bounds.top);
			Assert::AreEqual(ScopeBounds().bottom, recovered.bounds.bottom);
		}

		TEST_METHOD(DarkCenterCreditsAndUnavailableFramesDoNotChangeMode)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());
			ActivePictureBounds credits = ScopeBounds();
			credits.left += 22;
			credits.right -= 8;
			credits.aspectRatio =
				static_cast<double>(credits.right - credits.left) /
				(credits.bottom - credits.top);
			credits.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;

			for (int count = 0; count < 60; ++count, ++frame)
			{
				ActivePictureTransitionDecision decision;
				if (count % 3 == 0)
					decision = model.Observe({ {}, frame, false });
				else
					decision = Observe(
						model,
						count % 3 == 1 ? credits : ScopeBounds(),
						frame);
				Assert::IsFalse(decision.publish);
				Assert::IsTrue(decision.stable);
			}
		}

		TEST_METHOD(AmbiguousSustainedTransitionNeverAcquiresCropAuthority)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());
			ActivePictureBounds asymmetric = ImaxBounds();
			asymmetric.top += 24;
			asymmetric.aspectRatio =
				static_cast<double>(asymmetric.right - asymmetric.left) /
				(asymmetric.bottom - asymmetric.top);
			asymmetric.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;

			for (uint8_t count = 1; count < 64; ++count, ++frame)
			{
				const auto decision = Observe(
					model, asymmetric, frame,
					ActivePictureClassification::PROVISIONAL);
				Assert::IsFalse(decision.publish);
				Assert::IsTrue(decision.stable);
			}
			const auto decision = Observe(
				model, asymmetric, frame,
				ActivePictureClassification::PROVISIONAL);
			Assert::IsFalse(decision.publish);
			Assert::IsTrue(decision.stable);
			Assert::AreEqual(0.0, decision.confidence, 0.000001);
		}

		TEST_METHOD(PreviouslyTrustedGeometryReacquiresOnAdjacentFrames)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds(), 2);
			Observe(model, ImaxBounds(), frame);
			const auto imax = Observe(model, ImaxBounds(), frame + 2);
			Assert::IsTrue(imax.publish);

			const uint64_t returnFrame = frame + 4;
			const auto probing = Observe(
				model, ScopeBounds(), returnFrame,
				ActivePictureClassification::PROVISIONAL);
			Assert::IsFalse(probing.publish);
			Assert::IsTrue(probing.stable);
			Assert::IsTrue(model.ShouldAnalyze(returnFrame + 1, 23.976));

			const auto reacquired = Observe(
				model, ScopeBounds(), returnFrame + 1,
				ActivePictureClassification::PROVISIONAL);
			Assert::IsTrue(reacquired.publish);
			Assert::IsTrue(reacquired.stable);
			Assert::AreEqual(
				static_cast<unsigned long long>(1),
				static_cast<unsigned long long>(
					reacquired.decisionLatencyFrames));
		}

		TEST_METHOD(RecentTrustedGeometryRecursAcrossThreeAspectModes)
		{
			// Feature content commonly alternates among a small set of real modes.
			// A short, generation-local history lets a dark/provisional return to a
			// known scope mode confirm promptly without treating novel geometry as
			// a guess.
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds(), 2);
			Assert::IsFalse(Observe(model, ImaxBounds(), frame).publish);
			frame += 2;
			Assert::IsTrue(Observe(model, ImaxBounds(), frame).publish);
			frame += 2;
			Assert::IsFalse(Observe(model, FourByThreeBounds(), frame).publish);
			frame += 2;
			Assert::IsTrue(Observe(model, FourByThreeBounds(), frame).publish);
			frame += 2;

			const auto recall = Observe(model, ScopeBounds(), frame,
				ActivePictureClassification::PROVISIONAL);
			Assert::IsFalse(recall.publish);
			Assert::IsTrue(recall.stable);
			Assert::AreEqual(std::string("recent trusted geometry candidate"),
				recall.reason);
			Assert::IsTrue(model.ShouldAnalyze(frame + 1, 23.976));

			const auto confirmed = Observe(model, ScopeBounds(), frame + 1,
				ActivePictureClassification::PROVISIONAL);
			Assert::IsTrue(confirmed.publish);
			Assert::AreEqual(ScopeBounds().top, confirmed.bounds.top);
			Assert::AreEqual(ScopeBounds().bottom, confirmed.bounds.bottom);
		}

		TEST_METHOD(RecentTrustedGeometryExpiresAfterThreeNewModes)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());
			const ActivePictureBounds full = {
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0, ActivePictureBounds::BarAxes::NONE };
			auto switchTo = [&](const ActivePictureBounds& bounds,
				ActivePictureClassification classification =
					ActivePictureClassification::BAR_CROP_TRUSTED)
			{
				Assert::IsFalse(Observe(model, bounds, frame++, classification).publish);
				Assert::IsTrue(Observe(model, bounds, frame++, classification).publish);
			};

			switchTo(ImaxBounds());
			switchTo(FourByThreeBounds());
			switchTo(full, ActivePictureClassification::FULL_RASTER_TRUSTED);
			switchTo(ImaxBounds());

			// Scope is now more than three real modes old. A provisional sample
			// must stay an ordinary untrusted candidate rather than reviving a
			// stale assumption from much earlier in the feature.
			const auto stale = Observe(model, ScopeBounds(), frame,
				ActivePictureClassification::PROVISIONAL);
			Assert::IsFalse(stale.publish);
			Assert::IsTrue(stale.stable);
			Assert::AreEqual(std::string(
				"provisional geometry lacks affirmative crop authority"),
				stale.reason);
		}

		TEST_METHOD(ResetDiscardsPreviouslyTrustedGeometry)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());
			Observe(model, ImaxBounds(), frame++);
			Assert::IsTrue(Observe(model, ImaxBounds(), frame++).publish);
			model.Reset();

			for (int count = 0; count < 8; ++count)
			{
				const auto decision = Observe(
					model, ScopeBounds(), frame++,
					ActivePictureClassification::PROVISIONAL);
				Assert::IsFalse(decision.publish);
				Assert::IsFalse(decision.stable);
			}
		}

		TEST_METHOD(WorkerRestartResetRepublishesUnchangedTrustedGeometry)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());

			// Once stable, identical frames deliberately do not republish.
			Assert::IsFalse(
				Observe(model, ScopeBounds(), frame++).publish);

			// A replacement conversion worker must reset its retained model when
			// the output pin has cleared the externally published rectangle.
			model.Reset();
			for (uint8_t count = 1;
				count < ActivePictureTransitionModel::INITIAL_CONFIRMATIONS;
				++count)
			{
				Assert::IsFalse(
					Observe(model, ScopeBounds(), frame++).publish);
			}
			const auto republished = Observe(model, ScopeBounds(), frame);
			Assert::IsTrue(republished.publish);
			Assert::IsTrue(republished.stable);
			Assert::AreEqual(ScopeBounds().top, republished.bounds.top);
			Assert::AreEqual(ScopeBounds().bottom, republished.bounds.bottom);
		}

		TEST_METHOD(ResetBetweenAnalysisAndPublicationRejectsOldGeneration)
		{
			ActivePicturePublicationGate gate;
			const uint64_t analyzedGeneration = gate.Generation();
			int visibleAuthority = 7;

			gate.Reset([&visibleAuthority]()
			{
				visibleAuthority = 0;
			});
			const bool published = gate.TryPublish(
				analyzedGeneration, [&visibleAuthority]()
				{
					visibleAuthority = 9;
				});

			Assert::IsFalse(published);
			Assert::AreEqual(0, visibleAuthority);
			Assert::AreEqual<uint64_t>(1, gate.Generation());
		}

		TEST_METHOD(ResetClearsAnAtomicCurrentGenerationPublication)
		{
			ActivePicturePublicationGate gate;
			int visibleAuthority = 0;
			Assert::IsTrue(gate.TryPublish(gate.Generation(),
				[&visibleAuthority]()
				{
					visibleAuthority = 9;
				}));
			gate.Reset([&visibleAuthority]()
			{
				visibleAuthority = 0;
			});

			Assert::AreEqual(0, visibleAuthority);
			Assert::AreEqual<uint64_t>(1, gate.Generation());
		}

		TEST_METHOD(FullRasterIsImmediateSafeStartupAuthority)
		{
			ActivePictureTransitionModel model;
			ActivePictureBounds full = {
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0,
				ActivePictureBounds::BarAxes::NONE
			};
			const auto decision = Observe(
				model, full, 1,
				ActivePictureClassification::FULL_RASTER_TRUSTED);
			Assert::IsTrue(decision.publish);
			Assert::IsTrue(decision.stable);
			Assert::AreEqual(0, decision.bounds.left);
			Assert::AreEqual(3840, decision.bounds.right);
		}

		TEST_METHOD(FullRasterTransitionRetainsStableCropUntilConfirmed)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());
			const ActivePictureBounds full = {
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0, ActivePictureBounds::BarAxes::NONE };
			const auto probing = Observe(model, full, frame,
				ActivePictureClassification::FULL_RASTER_TRUSTED);
			Assert::IsFalse(probing.publish);
			Assert::IsTrue(probing.stable);
			Assert::IsFalse(probing.clearTransition);
			Assert::AreEqual(ScopeBounds().top, probing.stableBounds.top);

			const auto repeated = Observe(model, full, frame,
				ActivePictureClassification::FULL_RASTER_TRUSTED);
			Assert::IsFalse(repeated.publish);
			Assert::AreEqual(1U,
				static_cast<unsigned int>(repeated.matchingCandidates));

			const auto confirmed = Observe(model, full, frame + 1,
				ActivePictureClassification::FULL_RASTER_TRUSTED);
			Assert::IsTrue(confirmed.publish);
			Assert::AreEqual(0, confirmed.bounds.top);
			Assert::AreEqual(2160, confirmed.bounds.bottom);
		}

		TEST_METHOD(AsymmetricCandidateCannotCropEitherSide)
		{
			ActivePictureTransitionModel model;
			ActivePictureBounds full = {
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0,
				ActivePictureBounds::BarAxes::NONE
			};
			Observe(model, full, 1,
				ActivePictureClassification::FULL_RASTER_TRUSTED);
			ActivePictureBounds asymmetric = {
				132, 0, 3724, 2160, 3840, 2160,
				3592.0 / 2160.0, ActivePictureBounds::BarAxes::NONE
			};
			for (uint64_t frame = 2; frame < 64; ++frame)
			{
				const auto decision = Observe(
					model, asymmetric, frame,
					ActivePictureClassification::BAR_CROP_TRUSTED);
				Assert::IsFalse(decision.publish);
				Assert::IsTrue(decision.stable);
				Assert::AreEqual(0, decision.stableBounds.left);
				Assert::AreEqual(3840, decision.stableBounds.right);
			}
		}

		TEST_METHOD(RecordedFalseCandidateSequenceCannotContractFullRaster)
		{
			ActivePictureTransitionModel model;
			ActivePictureBounds full = {
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0,
				ActivePictureBounds::BarAxes::NONE
			};
			Observe(model, full, 1,
				ActivePictureClassification::FULL_RASTER_TRUSTED);
			const int candidates[][2] = {
				{ 0, 3764 }, { 0, 3724 }, { 0, 3740 },
				{ 0, 3724 }, { 132, 3724 }
			};
			uint64_t frame = 2;
			for (int repeat = 0; repeat < 12; ++repeat)
			{
				for (const auto& candidate : candidates)
				{
					ActivePictureBounds bounds = {
						candidate[0], 0, candidate[1], 2160,
						3840, 2160,
						static_cast<double>(
							candidate[1] - candidate[0]) / 2160.0,
						ActivePictureBounds::BarAxes::NONE
					};
					const auto decision = Observe(
						model, bounds, frame++,
						ActivePictureClassification::PROVISIONAL);
					Assert::IsFalse(decision.publish);
					Assert::IsTrue(decision.stable);
					Assert::AreEqual(0, decision.stableBounds.left);
					Assert::AreEqual(3840, decision.stableBounds.right);
				}
			}
		}

		TEST_METHOD(OscillatingClearCandidatesPreserveStableGeometry)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());
			ActivePictureBounds alternate = ImaxBounds();
			alternate.top += 80;
			alternate.bottom -= 80;
			alternate.aspectRatio =
				static_cast<double>(alternate.right - alternate.left) /
				(alternate.bottom - alternate.top);

			for (int count = 0; count < 12; ++count, ++frame)
			{
				const ActivePictureBounds bounds =
					count % 2 == 0 ? ImaxBounds() : alternate;
				const auto decision = Observe(model, bounds, frame);
				Assert::IsFalse(decision.publish);
				Assert::IsTrue(decision.stable);
			}
		}

		TEST_METHOD(FixedImaxAndFourByThreeControlsRemainStableWithNoise)
		{
			const ActivePictureBounds controls[] = {
				ImaxBounds(), FourByThreeBounds()
			};
			for (const auto& control : controls)
			{
				ActivePictureTransitionModel model;
				uint64_t frame = Establish(model, control);
				for (int count = 0; count < 240; ++count, ++frame)
				{
					ActivePictureBounds noisy = control;
					const int offset = count % 3 - 1;
					noisy.left += offset;
					noisy.right += offset;
					const auto decision = Observe(model, noisy, frame);
					Assert::IsFalse(decision.publish);
					Assert::IsTrue(decision.stable);
				}
			}
		}

		TEST_METHOD(MinorTrustedGeometryChangeStaysWithinTwoPercentDeadband)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());
			ActivePictureBounds minor = ScopeBounds();
			// 21px per edge is below 2% of 2160 and the combined 42px
			// height change remains inside the aggregate axis allowance.
			minor.top += 21;
			minor.bottom -= 21;
			minor.aspectRatio = static_cast<double>(minor.right - minor.left) /
				(minor.bottom - minor.top);

			for (int count = 0; count < 24; ++count, ++frame)
			{
				const auto decision = Observe(model, minor, frame);
				Assert::IsFalse(decision.publish);
				Assert::IsTrue(decision.stable);
				Assert::AreEqual(264, decision.bounds.top);
				Assert::AreEqual(1896, decision.bounds.bottom);
			}
		}

		TEST_METHOD(TrustedGeometryBeyondDeadbandStillTransitions)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());
			ActivePictureBounds changed = ScopeBounds();
			// 44px per edge exceeds 2% of 2160 and is therefore allowed
			// to take the normal two-observation trusted transition path.
			changed.top += 44;
			changed.bottom -= 44;
			changed.aspectRatio =
				static_cast<double>(changed.right - changed.left) /
				(changed.bottom - changed.top);

			const auto probing = Observe(model, changed, frame++);
			Assert::IsFalse(probing.publish);
			Assert::IsTrue(probing.stable);
			const auto committed = Observe(model, changed, frame);
			Assert::IsTrue(committed.publish);
			Assert::IsTrue(committed.stable);
			Assert::AreEqual(308, committed.bounds.top);
			Assert::AreEqual(1852, committed.bounds.bottom);
		}

		TEST_METHOD(DeadbandCannotBeRaisedBeyondFivePercent)
		{
			ActivePictureTransitionModel model;
			model.SetStableGeometryDeadbandPercent(99.0);
			uint64_t frame = Establish(model, ScopeBounds());
			const auto probing = Observe(model, ImaxBounds(), frame++);
			Assert::IsFalse(probing.publish);
			const auto committed = Observe(model, ImaxBounds(), frame);
			Assert::IsTrue(committed.publish);
		}

		TEST_METHOD(SceneCandidateResetPreservesStableButCannotBridgeConfirmation)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());
			ActivePictureBounds full = {
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0,
				ActivePictureBounds::BarAxes::NONE };

			const auto beforeCut = Observe(model, full, frame++,
				ActivePictureClassification::FULL_RASTER_TRUSTED);
			Assert::IsFalse(beforeCut.publish);
			model.ResetCandidateEvidence();
			const auto repeatedCutFrame = Observe(model, full, frame - 1,
				ActivePictureClassification::FULL_RASTER_TRUSTED);
			Assert::IsFalse(repeatedCutFrame.publish);
			Assert::AreEqual(0U, static_cast<unsigned int>(
				repeatedCutFrame.matchingCandidates));

			const auto firstAfterCut = Observe(model, full, frame++,
				ActivePictureClassification::FULL_RASTER_TRUSTED);
			Assert::IsFalse(firstAfterCut.publish);
			Assert::IsTrue(firstAfterCut.stable);
			Assert::AreEqual(ScopeBounds().top,
				firstAfterCut.stableBounds.top);
			const auto secondAfterCut = Observe(model, full, frame,
				ActivePictureClassification::FULL_RASTER_TRUSTED);
			Assert::IsTrue(secondAfterCut.publish);
			const auto repeatedPublication = Observe(model, full, frame,
				ActivePictureClassification::FULL_RASTER_TRUSTED);
			Assert::IsFalse(repeatedPublication.publish);
			Assert::IsTrue(repeatedPublication.stable);
		}
	};
}
