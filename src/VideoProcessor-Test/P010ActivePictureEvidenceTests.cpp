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
			std::vector<uint8_t> bytes;

			P010Frame(int frameWidth, int frameHeight, size_t padding = 0) :
				width(frameWidth),
				height(frameHeight),
				pitch(static_cast<size_t>(frameWidth) * 2 + padding),
				bytes(pitch * frameHeight +
					pitch * (frameHeight / 2), 0)
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
				for (int row = 0; row < height / 2; ++row)
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

			P010PlaneView View(size_t lengthAdjustment = 0) const
			{
				return { bytes.data(), bytes.size() - lengthAdjustment,
					width, height, pitch, pitch };
			}
		};
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
	};
}
