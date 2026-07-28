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
				3840.0 / 1632.0, true };
		}

		ActivePictureBounds ImaxBounds()
		{
			return { 0, 48, 3840, 2112, 3840, 2160,
				3840.0 / 2064.0, true };
		}

		ActivePictureBounds FourByThreeBounds()
		{
			return { 480, 0, 3360, 2160, 3840, 2160,
				4.0 / 3.0, true };
		}

		ActivePictureTransitionDecision Observe(
			ActivePictureTransitionModel& model,
			const ActivePictureBounds& bounds,
			uint64_t frameNumber,
			ActivePictureClassification classification =
				ActivePictureClassification::BAR_CROP_TRUSTED)
		{
			return model.Observe({
				bounds, frameNumber, true, classification });
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
					Assert::IsTrue(probing.clearTransition);

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
			intrusion.symmetricBars = false;

			for (int count = 0; count < 12; ++count, ++frame)
			{
				const ActivePictureBounds bounds =
					count % 2 == 0 ? intrusion : ScopeBounds();
				const auto decision = Observe(model, bounds, frame);
				Assert::IsFalse(decision.publish);
				Assert::IsTrue(decision.stable);
			}
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
			credits.symmetricBars = false;

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
			asymmetric.symmetricBars = false;

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

		TEST_METHOD(FullRasterIsImmediateSafeStartupAuthority)
		{
			ActivePictureTransitionModel model;
			ActivePictureBounds full = {
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0, false
			};
			const auto decision = Observe(
				model, full, 1,
				ActivePictureClassification::FULL_RASTER_TRUSTED);
			Assert::IsTrue(decision.publish);
			Assert::IsTrue(decision.stable);
			Assert::AreEqual(0, decision.bounds.left);
			Assert::AreEqual(3840, decision.bounds.right);
		}

		TEST_METHOD(AsymmetricCandidateCannotCropEitherSide)
		{
			ActivePictureTransitionModel model;
			ActivePictureBounds full = {
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0, false
			};
			Observe(model, full, 1,
				ActivePictureClassification::FULL_RASTER_TRUSTED);
			ActivePictureBounds asymmetric = {
				132, 0, 3724, 2160, 3840, 2160,
				3592.0 / 2160.0, false
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
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0, false
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
						false
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
	};
}
