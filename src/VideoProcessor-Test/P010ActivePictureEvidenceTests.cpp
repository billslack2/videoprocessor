#include "pch.h"

#include <P010ActivePictureEvidence.h>
#include "CppUnitTest.h"

#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
	namespace
	{
		void WriteCode(uint8_t* target, int code)
		{
			const uint16_t packed = static_cast<uint16_t>(code << 6);
			target[0] = static_cast<uint8_t>(packed & 0xff);
			target[1] = static_cast<uint8_t>(packed >> 8);
		}

		struct P010Frame
		{
			int width;
			int height;
			size_t pitch;
			int chromaRows;
			std::vector<uint8_t> bytes;

			P010Frame(int frameWidth, int frameHeight, size_t padding = 0,
				bool fullHeightChroma = false) :
				width(frameWidth),
				height(frameHeight),
				pitch(static_cast<size_t>(frameWidth) * 2 + padding),
				chromaRows(fullHeightChroma ? frameHeight : frameHeight / 2),
				bytes(pitch * frameHeight + pitch * chromaRows, 0)
			{
				Fill(300, 512, 512);
			}

			void Fill(int y, int u, int v)
			{
				for (int row = 0; row < height; ++row)
					for (int x = 0; x < width; ++x)
						WriteCode(bytes.data() + static_cast<size_t>(row) *
							pitch + x * 2, y);
				const size_t uvOffset = pitch * height;
				for (int row = 0; row < chromaRows; ++row)
					for (int x = 0; x < width; x += 2)
					{
						uint8_t* pixel = bytes.data() + uvOffset +
							static_cast<size_t>(row) * pitch + x * 2;
						WriteCode(pixel, u);
						WriteCode(pixel + 2, v);
					}
			}

			void BlackOutside(int left, int top, int right, int bottom,
				int y = 64, int u = 512, int v = 512)
			{
				for (int row = 0; row < height; ++row)
					for (int x = 0; x < width; ++x)
						if (x < left || x >= right ||
							row < top || row >= bottom)
							WriteCode(bytes.data() +
								static_cast<size_t>(row) * pitch + x * 2, y);
				const size_t uvOffset = pitch * height;
				for (int row = 0; row < height / 2; ++row)
					for (int x = 0; x < width; x += 2)
						if (x < left || x >= right ||
							row * 2 < top || row * 2 >= bottom)
						{
							uint8_t* pixel = bytes.data() + uvOffset +
								static_cast<size_t>(row) * pitch + x * 2;
							WriteCode(pixel, u);
							WriteCode(pixel + 2, v);
						}
			}

			void FillRectangle(int left, int top, int right, int bottom,
				int y, int u = 512, int v = 512)
			{
				for (int row = top; row < bottom; ++row)
					for (int x = left; x < right; ++x)
						WriteCode(bytes.data() +
							static_cast<size_t>(row) * pitch + x * 2, y);
				const size_t uvOffset = pitch * height;
				for (int row = top / 2; row < (bottom + 1) / 2; ++row)
					for (int x = left & ~1; x < right; x += 2)
					{
						uint8_t* pixel = bytes.data() + uvOffset +
							static_cast<size_t>(row) * pitch + x * 2;
						WriteCode(pixel, u);
						WriteCode(pixel + 2, v);
					}
			}

			P010PlaneView View(size_t lengthAdjustment = 0) const
			{
				const size_t p010Bytes = pitch * height + pitch * (height / 2);
				return { bytes.data(), p010Bytes - lengthAdjustment,
					width, height, pitch, pitch };
			}

			AnalysisLumaSource P210Source() const
			{
				return { bytes.data(), bytes.size(), width, height, pitch, pitch,
					AnalysisLumaFormat::P210, VideoFrameEncoding::V210,
					ColorSpace::REC_709, 1 };
			}
		};

		ActivePictureBounds ScopePresentation(int width, int height,
			int top, int bottom)
		{
			return { 0, top, width, bottom, width, height,
				static_cast<double>(width) / (bottom - top), true };
		}
	}

	TEST_CLASS(P010ActivePictureEvidenceTests)
	{
	public:
		TEST_METHOD(FullRasterIsTrustedImmediately)
		{
			P010Frame frame(320, 180, 16);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::IsTrue(evidence.available);
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureClassification::FULL_RASTER_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::AreEqual(0, evidence.trustedBounds.left);
			Assert::AreEqual(180, evidence.trustedBounds.bottom);
		}

		TEST_METHOD(ScopeBarsHaveTrustedOpposingLumaAndChromaEvidence)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::IsTrue(evidence.available);
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::IsTrue(evidence.top.trusted);
			Assert::IsTrue(evidence.bottom.trusted);
			Assert::IsTrue(evidence.trustedBounds.top >= 20);
			Assert::IsTrue(evidence.trustedBounds.bottom <= 160);
			Assert::IsTrue(evidence.lumaSamples < 30000);
		}

		TEST_METHOD(DarkCinematicScopeFrameKeepsTrustedBars)
		{
			P010Frame frame(3840, 2160);
			// Model a low-key movie shot: most of the picture sits only slightly
			// above encoded black, with uneven shadow color and a few localized
			// practical lights. The bars themselves remain neutral and coherent.
			frame.Fill(74, 476, 548);
			frame.BlackOutside(0, 280, 3840, 1880);
			frame.FillRectangle(240, 280, 1008, 1880, 96, 500, 524);
			frame.FillRectangle(1512, 280, 2280, 1880, 112, 520, 504);
			frame.FillRectangle(2784, 280, 3504, 1880, 92, 488, 536);
			frame.FillRectangle(1752, 576, 2088, 1032, 640, 512, 512);

			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::IsTrue(evidence.available);
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::IsTrue(evidence.top.trusted);
			Assert::IsTrue(evidence.bottom.trusted);
			Assert::AreEqual(280, evidence.trustedBounds.top);
			Assert::AreEqual(1880, evidence.trustedBounds.bottom);
			Assert::IsTrue(evidence.lumaSamples < 30000);
		}

		TEST_METHOD(FourByThreePillarboxIsTrusted)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(40, 0, 280, 180);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::IsTrue(evidence.left.trusted);
			Assert::IsTrue(evidence.right.trusted);
			Assert::IsTrue(evidence.trustedBounds.left >= 38);
			Assert::IsTrue(evidence.trustedBounds.right <= 282);
		}

		TEST_METHOD(SourceBakedTopControlExpandsTheMeasuredScopeEnvelope)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			frame.FillRectangle(0, 8, 320, 18, 600);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::IsTrue(evidence.available);
			Assert::IsTrue(evidence.proposedBounds.top <= 10);
			Assert::IsTrue(evidence.proposedBounds.top < 22);
			Assert::AreNotEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
		}

		TEST_METHOD(SourceBakedSideControlExpandsTheMeasuredPillarEnvelope)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(40, 0, 280, 180);
			frame.FillRectangle(10, 0, 30, 180, 600);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::IsTrue(evidence.available);
			Assert::IsTrue(evidence.proposedBounds.left <= 12);
			Assert::IsTrue(evidence.proposedBounds.left < 40);
			Assert::AreNotEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
		}

		TEST_METHOD(TrustedVerticalBarsIgnoreUntrustedHorizontalArtwork)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			// A dark one-sided feature spans the active picture. It can look
			// like a left bar, but has no trusted opposing right boundary.
			frame.FillRectangle(0, 22, 20, 158, 64);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::IsTrue(evidence.top.trusted);
			Assert::IsTrue(evidence.bottom.trusted);
			Assert::IsFalse(
				evidence.left.trusted && evidence.right.trusted);
			Assert::AreNotEqual(0, evidence.proposedBounds.left);
			Assert::AreEqual(0, evidence.trustedBounds.left);
			Assert::AreEqual(320, evidence.trustedBounds.right);
			Assert::IsTrue(evidence.trustedBounds.symmetricBars);
		}

		TEST_METHOD(SmallImaxStyleBarsRequireAndPassBoundaryEvidence)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 6, 320, 174);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::AreEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
		}

		TEST_METHOD(AsymmetricOrColoredEdgeCannotAuthorizeCrop)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 150, 64, 400, 620);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::AreNotEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
		}

		TEST_METHOD(DarkArtworkDoesNotBecomeTrustedBars)
		{
			P010Frame frame(320, 180);
			frame.Fill(80, 430, 590);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::AreNotEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
		}

		TEST_METHOD(RejectsShortPlaneAndInvalidPitchWithoutReadingPastBounds)
		{
			P010Frame frame(320, 180, 16);
			auto shortView = frame.View(1);
			auto evidence = ExtractP010ActivePictureEvidence(shortView);
			Assert::IsFalse(evidence.available);
			auto badPitch = frame.View();
			badPitch.lumaPitchBytes = 100;
			evidence = ExtractP010ActivePictureEvidence(badPitch);
			Assert::IsFalse(evidence.available);
		}

		TEST_METHOD(AdversarialBlackFrameStaysInsideFixedLumaBudget)
		{
			P010Frame frame(3840, 2160);
			frame.Fill(64, 512, 512);
			const auto evidence =
				ExtractP010ActivePictureEvidence(frame.View());
			Assert::IsTrue(evidence.lumaSamples < 30000);
			Assert::AreNotEqual(
				static_cast<int>(
					ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
		}

		TEST_METHOD(CleanScopeBarsAreSafeForTrustedPresentationRetention)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			const auto retention =
				EvaluateP010ActivePicturePresentationRetention(frame.View(),
					ScopePresentation(320, 180, 22, 158));

			Assert::IsTrue(retention.analysisValid);
			Assert::IsTrue(retention.presentationValid);
			Assert::IsTrue(retention.proposedBoundsAvailable);
			Assert::IsTrue(retention.proposedBoundsContained);
			Assert::IsTrue(retention.excludedBandsPixelSafe);
			Assert::IsTrue(retention.currentlyPixelSafe);
			Assert::IsFalse(retention.globalNearBlack);
		}

		TEST_METHOD(ContainedDarkProvisionalFrameRetainsWithoutInnerContrast)
		{
			P010Frame frame(320, 180);
			frame.Fill(120, 512, 512);
			// Six-line limited-range bars have safe pixels but only 16 codes of
			// inner contrast, below the acquisition requirement for small bars.
			frame.BlackOutside(0, 6, 320, 174, 104, 512, 512);
			const auto retention =
				EvaluateP010ActivePicturePresentationRetention(frame.View(),
					ScopePresentation(320, 180, 6, 174));

			Assert::AreEqual(
				static_cast<int>(ActivePictureClassification::PROVISIONAL),
				static_cast<int>(retention.activePicture.classification));
			Assert::IsFalse(retention.globalNearBlack);
			Assert::IsTrue(retention.proposedBoundsContained);
			Assert::IsFalse(retention.excludedTop.trusted);
			Assert::IsTrue(retention.excludedBandsPixelSafe);
			Assert::IsTrue(retention.currentlyPixelSafe);
		}

		TEST_METHOD(ColoredOrVisibleExcludedBandsRejectRetention)
		{
			const ActivePictureBounds presentation =
				ScopePresentation(320, 180, 22, 158);
			P010Frame colored(320, 180);
			colored.BlackOutside(0, 22, 320, 158, 64, 400, 620);
			auto retention =
				EvaluateP010ActivePicturePresentationRetention(colored.View(),
					presentation);
			Assert::IsTrue(retention.proposedBoundsContained);
			Assert::IsFalse(retention.excludedBandsPixelSafe);
			Assert::IsFalse(retention.currentlyPixelSafe);

			P010Frame visible(320, 180);
			visible.BlackOutside(0, 22, 320, 158);
			visible.FillRectangle(0, 8, 320, 18, 600);
			retention = EvaluateP010ActivePicturePresentationRetention(
				visible.View(), presentation);
			Assert::IsFalse(retention.excludedBandsPixelSafe);
			Assert::IsFalse(retention.currentlyPixelSafe);
		}

		ActivePictureBounds PillarPresentation(int width, int height,
			int left, int right)
		{
			return { left, 0, right, height, width, height,
				static_cast<double>(right - left) / height, true };
		}

		TEST_METHOD(NarrowTopOverlayProducesMinimalOutwardVisibleEnvelope)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			// Narrow enough that the whole-row bar detector still sees a black
			// row, like sparse text/icons in a source-baked volume control.
			frame.FillRectangle(120, 8, 136, 18, 600);
			const auto retention =
				EvaluateP010ActivePicturePresentationRetention(frame.View(),
					ScopePresentation(320, 180, 22, 158));

			Assert::IsFalse(retention.excludedBandsPixelSafe);
			Assert::IsFalse(retention.currentlyPixelSafe);
			Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
			Assert::IsTrue(retention.outwardVisibleBounds.top <= 8);
			Assert::AreEqual(158, retention.outwardVisibleBounds.bottom);
			Assert::AreEqual(0, retention.outwardVisibleBounds.left);
			Assert::AreEqual(320, retention.outwardVisibleBounds.right);
		}

		TEST_METHOD(NativeP210TopOverlayUsesTheSameOutwardFitPolicy)
		{
			P010Frame frame(320, 180, 0, true);
			frame.BlackOutside(0, 22, 320, 158);
			frame.FillRectangle(120, 8, 136, 18, 600);
			const auto retention = EvaluateActivePicturePresentationRetention(
				frame.P210Source(), ScopePresentation(320, 180, 22, 158));

			Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
			Assert::IsTrue(retention.outwardVisibleBounds.top <= 8);
			Assert::AreEqual(158, retention.outwardVisibleBounds.bottom);
		}

		TEST_METHOD(BottomAndSideOverlaysExpandOnlyTheirOccupiedEdges)
		{
			P010Frame bottom(320, 180);
			bottom.BlackOutside(0, 22, 320, 158);
			bottom.FillRectangle(120, 162, 136, 172, 600);
			auto retention = EvaluateP010ActivePicturePresentationRetention(
				bottom.View(), ScopePresentation(320, 180, 22, 158));
			Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
			Assert::AreEqual(22, retention.outwardVisibleBounds.top);
			Assert::IsTrue(retention.outwardVisibleBounds.bottom >= 172);

			P010Frame side(320, 180);
			side.BlackOutside(40, 0, 280, 180);
			side.FillRectangle(8, 70, 18, 86, 600);
			retention = EvaluateP010ActivePicturePresentationRetention(
				side.View(), PillarPresentation(320, 180, 40, 280));
			Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
			Assert::IsTrue(retention.outwardVisibleBounds.left <= 8);
			Assert::AreEqual(280, retention.outwardVisibleBounds.right);
		}

		TEST_METHOD(IsolatedTopBarNoiseCannotAuthorizeAnOutwardEnvelope)
		{
			P010Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			frame.FillRectangle(120, 8, 136, 9, 600);
			const auto retention =
				EvaluateP010ActivePicturePresentationRetention(frame.View(),
					ScopePresentation(320, 180, 22, 158));

			Assert::IsFalse(retention.outwardVisibleBoundsAvailable);
		}

		TEST_METHOD(AllBlackFrameIsValidNearBlackWithoutGeometry)
		{
			P010Frame frame(320, 180);
			frame.Fill(64, 512, 512);
			const auto retention =
				EvaluateP010ActivePicturePresentationRetention(frame.View(),
					ScopePresentation(320, 180, 22, 158));

			Assert::IsTrue(retention.analysisValid);
			Assert::IsTrue(retention.globalNearBlack);
			Assert::IsTrue(retention.globalLumaP90 <= 64.0);
			Assert::IsFalse(retention.activePicture.available);
			Assert::IsFalse(retention.proposedBoundsAvailable);
			Assert::IsTrue(retention.excludedBandsPixelSafe);
			Assert::IsTrue(retention.currentlyPixelSafe);
		}

		TEST_METHOD(InvalidSourceCannotProveNearBlackOrPixelSafety)
		{
			P010Frame frame(320, 180, 16);
			const auto retention =
				EvaluateP010ActivePicturePresentationRetention(frame.View(1),
					ScopePresentation(320, 180, 22, 158));

			Assert::IsFalse(retention.analysisValid);
			Assert::IsFalse(retention.globalNearBlack);
			Assert::IsFalse(retention.excludedBandsPixelSafe);
			Assert::IsFalse(retention.currentlyPixelSafe);
		}
	};
}
