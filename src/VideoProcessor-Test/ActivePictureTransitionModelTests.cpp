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
		TEST_METHOD(WomenInBlueFourPixelNoiseDoesNotPublishAnAspectChange)
		{
			ActivePictureTransitionModel model;
			ActivePictureBounds old = { 0, 208, 3840, 1948, 3840, 2160,
				3840.0 / 1740.0, ActivePictureBounds::BarAxes::TOP_BOTTOM };
			uint64_t frame = Establish(model, old);
			auto observed = old;
			observed.bottom = 1952;
			observed.aspectRatio = 3840.0 / 1744.0;
			for (int n = 0; n < 1440; ++n)
			{
				const auto d = Observe(model, observed, frame++);
				Assert::IsFalse(d.publish);
				Assert::AreEqual(old.bottom, d.stableBounds.bottom);
				Assert::AreEqual(old.aspectRatio, d.stableBounds.aspectRatio);
			}
		}

		TEST_METHOD(MaterialMovingAllSidedInsetRetainsEstablishedFraming)
		{
			for (double rate : { 23.976, 24.0, 59.94, 60.0 })
			{
				ActivePictureTransitionModel model;
				uint64_t frame = Establish(model, ScopeBounds());
				const auto first = frame;
				const auto interval = ActivePictureTransitionModel::AnalysisIntervalFrames(rate);
				for (; frame - first <= uint64_t(std::ceil(rate * 8.0)); frame += interval)
				{
					const int move = 2 * int((frame - first) * 20.0 / rate);
					auto bounds = ScopeBounds();
					// Remain well beyond the AR deadband. Duration and movement do
					// not turn a four-sided composition into a format boundary.
					bounds.left = 160 + move; bounds.right = 3680 - move;
					bounds.top += move / 2; bounds.bottom -= move / 2;
					bounds.trustedBarAxes = ActivePictureBounds::BarAxes::BOTH;
					bounds.aspectRatio = double(bounds.right - bounds.left) / (bounds.bottom - bounds.top);
					const auto decision = Observe(model, bounds, frame,
						ActivePictureClassification::BAR_CROP_TRUSTED, rate);
					Assert::IsFalse(decision.publish);
					Assert::AreEqual(ScopeBounds().left, decision.stableBounds.left);
					Assert::AreEqual(ScopeBounds().top, decision.stableBounds.top);
				}
			}
		}

		TEST_METHOD(MovingInsetReturningWithinAspectBandDoesNotPublishAtOldTimer)
		{
			for (double rate : {23.976,24.0,59.94,60.0})
			{
				ActivePictureTransitionModel model;
				uint64_t frame = Establish(model, ScopeBounds());
				const auto first = frame;
				const auto interval = ActivePictureTransitionModel::AnalysisIntervalFrames(rate);
				for (; frame-first <= uint64_t(std::ceil(rate*5.0)); frame += interval)
				{
					const int move = 2*int((frame-first)*20.0/rate);
					auto bounds = ScopeBounds();
					bounds.left = 80+move; bounds.right = 3760-move;
					bounds.top += move/2; bounds.bottom -= move/2;
					bounds.trustedBarAxes = ActivePictureBounds::BarAxes::BOTH;
					bounds.aspectRatio = double(bounds.right-bounds.left)/(bounds.bottom-bounds.top);
					const auto d = Observe(model,bounds,frame,ActivePictureClassification::BAR_CROP_TRUSTED,rate);
					Assert::IsFalse(d.publish);
					Assert::AreEqual(ScopeBounds().left,d.stableBounds.left);
					Assert::AreEqual(ScopeBounds().top,d.stableBounds.top);
				}
			}
		}

		TEST_METHOD(ContainedInsetStaysHeldAcrossMissingEvidenceAndSceneReset)
		{
			for (int interruption = 0; interruption < 3; ++interruption)
			{
				ActivePictureTransitionModel model;
				uint64_t frame = Establish(model, ScopeBounds());
				for (int n = 0; n < 72; ++n)
					Assert::IsFalse(Observe(model, LoggedWindowboxBounds(), frame++,
						ActivePictureClassification::BAR_CROP_TRUSTED, 24).publish);
				if (interruption == 0) frame += 24;
				if (interruption == 1) model.ResetCandidateEvidence();
				if (interruption == 2) model.Observe({ {}, frame++, false });
				for (int n = 0; n < 72; ++n)
					Assert::IsFalse(Observe(model, LoggedWindowboxBounds(), frame++,
						ActivePictureClassification::BAR_CROP_TRUSTED, 24).publish);
			}
		}

		TEST_METHOD(InitiallyAcquiredWindowboxRejectsSmallAlternatingEdgeNoise)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, LoggedWindowboxBounds());
			for (int n = 0; n < 240; ++n)
			{
				auto noisy = LoggedWindowboxBounds();
				noisy.top += n % 2 ? 12 : -12;
				noisy.bottom += n % 2 ? -12 : 12;
				noisy.aspectRatio = double(noisy.right - noisy.left) / (noisy.bottom - noisy.top);
				Assert::IsFalse(Observe(model, noisy, frame++,
					ActivePictureClassification::BAR_CROP_TRUSTED, 24).publish);
			}
		}

		TEST_METHOD(NovelCandidateCannotInheritRecentGeometryAuthority)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());
			Observe(model, ImaxBounds(), frame++);
			Assert::IsTrue(Observe(model, ImaxBounds(), frame++).publish);
			Assert::IsFalse(Observe(model, ScopeBounds(), frame++,
				ActivePictureClassification::PROVISIONAL).publish);
			Assert::IsFalse(Observe(model, FourByThreeBounds(), frame++).publish);
			const auto d = Observe(model, FourByThreeBounds(), frame++);
			Assert::IsTrue(d.publish);
			Assert::IsFalse(d.knownTrustedGeometryReacquired);
		}

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

		TEST_METHOD(PersistentAllSidedWindowboxRetainsEstablishedScope)
		{
			const double rates[] = {
				23.976, 24.0, 25.0, 29.97, 30.0, 59.94, 60.0
			};
			for (const double rate : rates)
			{
				ActivePictureTransitionModel model;
				uint64_t frame = Establish(model, LoggedScopeBounds());
				const uint64_t firstCandidate = frame;
				while (frame - firstCandidate <= static_cast<uint64_t>(
					std::ceil(rate * 8.0)))
				{
					const auto decision = Observe(model,
						LoggedWindowboxBounds(), frame++,
						ActivePictureClassification::BAR_CROP_TRUSTED,
						rate);
					Assert::IsFalse(decision.publish);
					Assert::AreEqual(LoggedScopeBounds().left,
						decision.stableBounds.left);
					Assert::AreEqual(LoggedScopeBounds().top,
						decision.stableBounds.top);
				}
			}
		}

		TEST_METHOD(ProvisionalRecentWindowboxCannotOverrideEstablishedScope)
		{
			constexpr double rate = 60.0;
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, LoggedScopeBounds());
			for (int count = 0; count < 300; ++count, ++frame)
			{
				const auto held = Observe(model, LoggedWindowboxBounds(), frame,
					ActivePictureClassification::BAR_CROP_TRUSTED, rate);
				Assert::IsFalse(held.publish);
				Assert::AreEqual(LoggedScopeBounds().left, held.stableBounds.left);
			}

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
					"provisional geometry lacks affirmative crop authority"),
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
			Assert::IsTrue(reacquired.knownTrustedGeometryReacquired);
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(reacquired.authoritativeClassification));
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
			Assert::IsTrue(confirmed.knownTrustedGeometryReacquired);
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(confirmed.authoritativeClassification));
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


		TEST_METHOD(QueuedPublicationRequiresTheExactLiveStableReference)
		{
			ActivePictureTransitionModel live, preview;
			uint64_t frame = Establish(live, ScopeBounds());
			Establish(preview, FourByThreeBounds());
			Observe(preview, ImaxBounds(), frame++);
			const auto queued = Observe(preview, ImaxBounds(), frame++);
			Assert::IsTrue(queued.publish);
			ActivePicturePublicationAdmission admission;
			Assert::IsFalse(live.AdoptPublishedDecision(queued,
				ActivePictureClassification::BAR_CROP_TRUSTED, false, &admission));
			Assert::AreEqual(static_cast<int>(ActivePicturePublicationAdmission::STABLE_REFERENCE_MISMATCH),
				static_cast<int>(admission));
			const auto firstCurrent = Observe(live, ImaxBounds(), frame++);
			Assert::IsFalse(firstCurrent.publish);
			Assert::AreEqual(ScopeBounds().top, firstCurrent.stableBounds.top);
			Assert::IsTrue(Observe(live, ImaxBounds(), frame++).publish);
		}

		TEST_METHOD(QueuedInitialAndAxisReferencesCannotBeInvented)
		{
			ActivePictureTransitionModel preview;
			ActivePictureTransitionDecision initial;
			for (uint64_t frame = 1; frame <= 4; ++frame)
				initial = Observe(preview, ScopeBounds(), frame);
			Assert::IsTrue(initial.publish);
			ActivePictureTransitionModel empty;
			auto nonInitial = initial; nonInitial.stableBounds = ImaxBounds();
			Assert::IsFalse(empty.AdoptPublishedDecision(nonInitial, ActivePictureClassification::BAR_CROP_TRUSTED));
			Assert::IsTrue(empty.AdoptPublishedDecision(initial, ActivePictureClassification::BAR_CROP_TRUSTED));
			Assert::IsFalse(empty.AdoptPublishedDecision(initial, ActivePictureClassification::BAR_CROP_TRUSTED));
			Observe(preview, ImaxBounds(), 5);
			auto outward = Observe(preview, ImaxBounds(), 6);
			outward.stableBounds.trustedBarAxes = ActivePictureBounds::BarAxes::BOTH;
			Assert::IsFalse(empty.AdoptPublishedDecision(outward, ActivePictureClassification::BAR_CROP_TRUSTED));
			outward.stableBounds.trustedBarAxes = ScopeBounds().trustedBarAxes;
			Assert::IsTrue(empty.AdoptPublishedDecision(outward, ActivePictureClassification::BAR_CROP_TRUSTED));
		}

		TEST_METHOD(QueuedPublicationCannotBypassEitherLiveDeadband)
		{
			for (int inset : {16, 22})
			{
				ActivePictureTransitionModel live;
				const ActivePictureBounds anchor = {0,280,3840,1880,3840,2160,2.4,ActivePictureBounds::BarAxes::TOP_BOTTOM};
				uint64_t frame = Establish(live, anchor);
				auto minor = anchor; minor.top += inset; minor.bottom -= inset;
				minor.aspectRatio = 3840.0 / (minor.bottom - minor.top);
				ActivePictureTransitionDecision queued;
				queued.publish = queued.stable = true;
				queued.bounds = minor; queued.stableBounds = anchor;
				ActivePicturePublicationAdmission admission;
				Assert::IsFalse(live.AdoptPublishedDecision(queued,
					ActivePictureClassification::BAR_CROP_TRUSTED, false, &admission));
				Assert::AreEqual(static_cast<int>(inset == 16
					? ActivePicturePublicationAdmission::STABLE_GEOMETRY_RETAINED
					: ActivePicturePublicationAdmission::STABLE_ASPECT_RETAINED), static_cast<int>(admission));
				const auto held = Observe(live, minor, frame);
				Assert::IsFalse(held.publish);
				Assert::AreEqual(anchor.top, held.bounds.top);
			}
		}

		TEST_METHOD(AspectDeadbandUsesAcceptedPixelsWithoutRollingDrift)
		{
			ActivePictureTransitionModel model;
			const ActivePictureBounds anchor = {0,280,3840,1880,3840,2160,2.4,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			uint64_t frame = Establish(model, anchor);
			for (int inset : {8, 16, 22, 38})
			{
				auto probeBounds = anchor; probeBounds.top += inset; probeBounds.bottom -= inset;
				// Cached aspect can lag measured pixels; it must not drive this rule.
				probeBounds.aspectRatio = 9.0;
				for (int i = 0; i < 120; ++i)
				{
					const auto held = Observe(model, probeBounds, frame++);
					Assert::IsFalse(held.publish);
					Assert::AreEqual(anchor.top, held.bounds.top);
				}
			}
			auto larger = anchor; larger.top += 40; larger.bottom -= 40;
			larger.aspectRatio = 3840.0 / (larger.bottom - larger.top);
			Assert::IsFalse(Observe(model, larger, frame++).publish);
			Assert::IsTrue(Observe(model, larger, frame++).publish);
		}

		TEST_METHOD(WomenInBlueThreePercentSameAxisChangeRetainsEstablishedFormat)
		{
			const ActivePictureBounds anchor = {0,208,3840,1952,3840,2160,
				3840.0/1744.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			const ActivePictureBounds observed = {0,232,3840,1924,3840,2160,
				3840.0/1692.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, anchor);
			for (int i = 0; i < 240; ++i)
			{
				const auto held = Observe(model, observed, frame++);
				Assert::IsFalse(held.publish);
				Assert::AreEqual(anchor.top, held.stableBounds.top);
				Assert::AreEqual(anchor.bottom, held.stableBounds.bottom);
			}
		}

		TEST_METHOD(RecentHistoryCannotBypassEstablishedAspectDeadband)
		{
			ActivePictureTransitionModel model;
			const ActivePictureBounds anchor = {0,280,3840,1880,3840,2160,2.4,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			auto earlier = anchor; earlier.top += 22; earlier.bottom -= 22;
			earlier.aspectRatio = 3840.0 / (earlier.bottom - earlier.top);
			uint64_t frame = Establish(model, earlier);
			Observe(model, anchor, frame++);
			Assert::IsTrue(Observe(model, anchor, frame++).publish);
			for (int i = 0; i < 120; ++i)
			{
				const auto held = Observe(model, earlier, frame++, ActivePictureClassification::PROVISIONAL);
				Assert::IsFalse(held.publish);
				Assert::AreEqual(anchor.top, held.stableBounds.top);
			}
		}

		TEST_METHOD(AllSidedInsetRetainsFramingButExpansionAndTranslationProceed)
		{
			const ActivePictureBounds anchor = {0,280,3840,1880,3840,2160,2.4,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			const ActivePictureBounds inset = {188,468,3652,1692,3840,2160,3464.0/1224.0,ActivePictureBounds::BarAxes::BOTH};
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, anchor);
			for (int i = 0; i < 360; ++i)
			{
				const auto held = Observe(model, inset, frame++);
				Assert::IsFalse(held.publish);
				Assert::AreEqual(anchor.left, held.bounds.left);
			}
			ActivePictureTransitionDecision queued;
			queued.publish = queued.stable = true;
			queued.bounds = inset; queued.stableBounds = anchor;
			ActivePicturePublicationAdmission admission;
			Assert::IsFalse(model.AdoptPublishedDecision(queued,
				ActivePictureClassification::BAR_CROP_TRUSTED, false, &admission));
			Assert::AreEqual(static_cast<int>(
				ActivePicturePublicationAdmission::CONTAINED_COMPOSITION_RETAINED),
				static_cast<int>(admission));
			// A fresh source may acquire this inset normally.
			model.Reset(); frame = Establish(model, inset);
			Observe(model, anchor, frame++);
			Assert::IsTrue(Observe(model, anchor, frame++).publish);
			model.Reset(); frame = Establish(model, inset);
			auto shifted = inset; shifted.left -= 160; shifted.right -= 160;
			Observe(model, shifted, frame++);
			Assert::IsTrue(Observe(model, shifted, frame++).publish);
		}

		TEST_METHOD(RememberedWindowboxCannotBypassCompositionHold)
		{
			const auto inset = LoggedWindowboxBounds();
			const auto anchor = LoggedScopeBounds();
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, inset);
			Observe(model, anchor, frame++);
			Assert::IsTrue(Observe(model, anchor, frame++).publish);
			for (bool provisional : {false,true})
			{
				auto observed = inset;
				if (provisional) observed.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
				for (int n = 0; n < 600; ++n)
				{
					const auto held = Observe(model, observed, frame++, provisional
						? ActivePictureClassification::PROVISIONAL : ActivePictureClassification::BAR_CROP_TRUSTED);
					Assert::IsFalse(held.publish || held.knownTrustedGeometryReacquired);
					Assert::AreEqual(anchor.left, held.stableBounds.left);
					Assert::AreEqual(anchor.right, held.stableBounds.right);
				}
			}
		}

		TEST_METHOD(FailedReplayAllSidedInsetNeverReplacesTrustedMovieFrame)
		{
			const ActivePictureBounds anchor = {0,232,3840,1924,3840,2160,
				3840.0/1692.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			ActivePictureTransitionModel model;
			Establish(model, anchor);
			for (uint64_t sequence = 2174; sequence <= 2270; ++sequence)
			{
				const int step = static_cast<int>(sequence - 2174);
				ActivePictureBounds inset = {
					192 - 4 * step / 96,
					440 + 28 * step / 96,
					3648 + 4 * step / 96,
					1720 - 28 * step / 96,
					3840,2160,0.0,ActivePictureBounds::BarAxes::BOTH};
				inset.aspectRatio = static_cast<double>(inset.right - inset.left) /
					(inset.bottom - inset.top);
				const auto held = Observe(model, inset, sequence,
					ActivePictureClassification::BAR_CROP_TRUSTED, 24.0);
				Assert::IsFalse(held.publish);
				Assert::AreEqual(anchor.left, held.stableBounds.left);
				Assert::AreEqual(anchor.top, held.stableBounds.top);
				Assert::AreEqual(anchor.bottom, held.stableBounds.bottom);
			}
			const ActivePictureBounds recovered = {0,208,3840,1952,3840,2160,
				3840.0/1744.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			Assert::IsFalse(Observe(model, recovered, 2271,
				ActivePictureClassification::BAR_CROP_TRUSTED, 24.0).publish);
			const auto published = Observe(model, recovered, 2272,
				ActivePictureClassification::BAR_CROP_TRUSTED, 24.0);
			Assert::IsTrue(published.publish);
			Assert::AreEqual(recovered.top, published.bounds.top);
			Assert::AreEqual(recovered.bottom, published.bounds.bottom);
		}

		TEST_METHOD(AspectDeadbandPreservesMaterialImaxAndFullRasterTransitions)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());
			Observe(model, ImaxBounds(), frame++);
			Assert::IsTrue(Observe(model, ImaxBounds(), frame++).publish);
			const ActivePictureBounds full = {0,0,3840,2160,3840,2160,16.0/9.0,ActivePictureBounds::BarAxes::NONE};
			Observe(model, full, frame++, ActivePictureClassification::FULL_RASTER_TRUSTED);
			Assert::IsTrue(Observe(model, full, frame++, ActivePictureClassification::FULL_RASTER_TRUSTED).publish);
			Observe(model, ScopeBounds(), frame++);
			Assert::IsTrue(Observe(model, ScopeBounds(), frame++).publish);
		}

		TEST_METHOD(TwoTwentyAndOneFortyThreeCanTransitionInBothDirections)
		{
			const ActivePictureBounds scope = {0,207,3840,1953,3840,2160,
				3840.0/1746.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			const ActivePictureBounds tall = {376,0,3464,2160,3840,2160,
				3088.0/2160.0,ActivePictureBounds::BarAxes::LEFT_RIGHT};
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, scope);
			Assert::IsFalse(Observe(model, tall, frame++).publish);
			const auto toTall = Observe(model, tall, frame++);
			Assert::IsTrue(toTall.publish);
			Assert::AreEqual(tall.left, toTall.bounds.left);
			Assert::IsFalse(Observe(model, scope, frame++).publish);
			const auto toScope = Observe(model, scope, frame++);
			Assert::IsTrue(toScope.publish);
			Assert::AreEqual(scope.top, toScope.bounds.top);
		}

		TEST_METHOD(TwoThirtyFiveAndOneNinetyCanTransitionInBothDirections)
		{
			const ActivePictureBounds scope = {0,263,3840,1897,3840,2160,
				3840.0/1634.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			const ActivePictureBounds imax = {0,69,3840,2091,3840,2160,
				3840.0/2022.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, scope);
			Assert::IsFalse(Observe(model, imax, frame++).publish);
			const auto toImax = Observe(model, imax, frame++);
			Assert::IsTrue(toImax.publish);
			Assert::AreEqual(imax.top, toImax.bounds.top);
			Assert::IsFalse(Observe(model, scope, frame++).publish);
			const auto toScope = Observe(model, scope, frame++);
			Assert::IsTrue(toScope.publish);
			Assert::AreEqual(scope.top, toScope.bounds.top);
		}

		TEST_METHOD(TwoTwentyAndTwoThirtyFiveTransitionBothWaysAtFivePercent)
		{
			const ActivePictureBounds wide = {0,264,3840,1896,3840,2160,
				3840.0/1632.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			const ActivePictureBounds normal = {0,208,3840,1952,3840,2160,
				3840.0/1744.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
			for (double rate : {23.976,24.0,25.0,50.0,59.94,60.0})
			{
				ActivePictureTransitionModel model;
				uint64_t frame = Establish(model, normal);
				const auto interval = ActivePictureTransitionModel::AnalysisIntervalFrames(rate);
				for (const auto& target : {wide,normal,wide,normal})
				{
					Assert::IsFalse(Observe(model,target,frame,ActivePictureClassification::BAR_CROP_TRUSTED,rate).publish);
					frame += interval;
					const auto accepted = Observe(model,target,frame,ActivePictureClassification::BAR_CROP_TRUSTED,rate);
					Assert::IsTrue(accepted.publish);
					Assert::AreEqual(target.top,accepted.bounds.top);
					Assert::AreEqual(target.bottom,accepted.bounds.bottom);
					frame += interval;
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

		TEST_METHOD(TrustedGeometryBeyondAspectDeadbandStillTransitions)
		{
			ActivePictureTransitionModel model;
			uint64_t frame = Establish(model, ScopeBounds());
			ActivePictureBounds changed = ScopeBounds();
			// 84px per edge changes aspect by more than 10% and is therefore
			// allowed to take the normal two-observation transition path.
			changed.top += 84;
			changed.bottom -= 84;
			changed.aspectRatio =
				static_cast<double>(changed.right - changed.left) /
				(changed.bottom - changed.top);

			const auto probing = Observe(model, changed, frame++);
			Assert::IsFalse(probing.publish);
			Assert::IsTrue(probing.stable);
			const auto committed = Observe(model, changed, frame);
			Assert::IsTrue(committed.publish);
			Assert::IsTrue(committed.stable);
			Assert::AreEqual(348, committed.bounds.top);
			Assert::AreEqual(1812, committed.bounds.bottom);
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

		TEST_METHOD(BootstrapResetCannotAcquireFromPausedFrameDuplicates)
		{
			ActivePictureTransitionModel model;
			const uint64_t pausedFrame = 5000;
			const auto blocked = Observe(model, ScopeBounds(), pausedFrame,
				ActivePictureClassification::PROVISIONAL);
			Assert::IsFalse(blocked.publish);

			model.ResetCandidateEvidence();
			const auto duplicate = Observe(model, ScopeBounds(), pausedFrame,
				ActivePictureClassification::BAR_CROP_TRUSTED);
			Assert::IsFalse(duplicate.publish);
			Assert::AreEqual(0U, static_cast<unsigned int>(
				duplicate.matchingCandidates));

			ActivePictureTransitionDecision acquired;
			for (uint8_t count = 0;
				count < ActivePictureTransitionModel::INITIAL_CONFIRMATIONS;
				++count)
			{
				acquired = Observe(model, ScopeBounds(),
					pausedFrame + 1 + count);
			}
			Assert::IsTrue(acquired.publish);
			Assert::IsTrue(acquired.stable);
		}
	};
}
