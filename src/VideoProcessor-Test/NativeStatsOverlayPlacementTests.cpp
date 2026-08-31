#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/NativeStatsOverlayPlacement.h>
#include <vprenderer/NativeStatsOverlayBitmap.h>

#include <array>
#include <cmath>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace NativeStatsOverlayPlacement;

namespace Tests
{
	TEST_CLASS(NativeStatsOverlayPlacementTests)
	{
	public:
		TEST_METHOD(FullFrameUsesBottomRightInset)
		{
			const Result result = Place(
				{ 0.0f, 0.0f, 3840.0f, 2160.0f },
				{ 0.0f, 0.0f, 3840.0f, 2160.0f }, 420.0f, 600.0f);
			Assert::IsTrue(std::fabs(result.panel.left - 3380.0f) < 0.01f);
			Assert::IsTrue(std::fabs(result.panel.top - 1520.0f) < 0.01f);
			Assert::IsTrue(std::fabs(result.panel.right - 3800.0f) < 0.01f);
			Assert::IsTrue(std::fabs(result.panel.bottom - 2120.0f) < 0.01f);
			Assert::IsFalse(result.insetClamped);
		}

		TEST_METHOD(ScopeViewportAnchorsInsideTheFittedPicture)
		{
			const Result result = Place(
				{ 0.0f, 283.0f, 3840.0f, 1917.0f },
				{ 0.0f, 0.0f, 3840.0f, 2160.0f }, 420.0f, 600.0f);
			Assert::IsTrue(std::fabs(result.panel.top - 1277.0f) < 0.01f);
			Assert::IsTrue(std::fabs(result.panel.bottom - 1877.0f) < 0.01f);
		}

		TEST_METHOD(SweepBannerAnchorsAtTheTopRightOfTheActivePicture)
		{
			const Result result = PlaceTopRight(
				{ 0.0f, 0.0f, 3840.0f, 2160.0f },
				{ 0.0f, 0.0f, 3840.0f, 2160.0f }, 860.0f, 112.0f);
			Assert::IsTrue(std::fabs(result.panel.left - 2940.0f) < 0.01f);
			Assert::IsTrue(std::fabs(result.panel.top - 40.0f) < 0.01f);
			Assert::IsTrue(std::fabs(result.panel.right - 3800.0f) < 0.01f);
			Assert::IsTrue(std::fabs(result.panel.bottom - 152.0f) < 0.01f);
		}

		TEST_METHOD(SweepBannerFollowsTheScopeFittedPicture)
		{
			const Result result = PlaceTopRight(
				{ 0.0f, 283.0f, 3840.0f, 1917.0f },
				{ 0.0f, 0.0f, 3840.0f, 2160.0f }, 860.0f, 112.0f);
			Assert::IsTrue(std::fabs(result.panel.top - 323.0f) < 0.01f);
			Assert::IsTrue(std::fabs(result.panel.bottom - 435.0f) < 0.01f);
			Assert::IsTrue(std::fabs(result.visiblePicture.top - 283.0f) < 0.01f);
		}

		TEST_METHOD(ProfileBannerAnchorsAtTopLeftInsideScopePicture)
		{
			const Result result = PlaceTopLeft(
				{ 0.0f, 283.0f, 3840.0f, 1917.0f },
				{ 0.0f, 0.0f, 3840.0f, 2160.0f }, 760.0f, 88.0f,
				ProfileOverlayTopInset(1634.0f), ProfileOverlayLeftInset(1634.0f));
			Assert::IsTrue(std::fabs(result.panel.left - 75.3889f) < 0.01f);
			Assert::IsTrue(std::fabs(result.panel.top - 358.3889f) < 0.01f);
			Assert::IsTrue(std::fabs(result.panel.right - 835.3889f) < 0.01f);
			Assert::IsTrue(std::fabs(result.panel.bottom - 446.3889f) < 0.01f);
		}

		TEST_METHOD(ProfileBannerScalesWithVisiblePictureResolution)
		{
			Assert::IsTrue(std::fabs(ProfileOverlayScale(1080.0f) - 1.275f) <
				0.01f);
			Assert::IsTrue(std::fabs(ProfileOverlayScale(1440.0f) - 1.7f) <
				0.01f);
			Assert::IsTrue(std::fabs(ProfileOverlayScale(2160.0f) - 2.55f) <
				0.01f);
			Assert::IsTrue(std::fabs(ProfileOverlayScale(720.0f) - 0.85f) <
				0.01f);
			Assert::IsTrue(std::fabs(ProfileOverlayLeftInset(1080.0f) - 60.0f) <
				0.01f);
			Assert::IsTrue(std::fabs(ProfileOverlayLeftInset(2160.0f) - 90.0f) <
				0.01f);
			Assert::IsTrue(std::fabs(ProfileOverlayLeftInset(720.0f) - 50.0f) <
				0.01f);
			Assert::IsTrue(std::fabs(ProfileOverlayTopInset(1080.0f) - 60.0f) <
				0.01f);
			Assert::IsTrue(std::fabs(ProfileOverlayTopInset(2160.0f) - 90.0f) <
				0.01f);
		}

		TEST_METHOD(ProfileBannerScalesFromVisibleZoomPicture)
		{
			const Rect output{ 0.0f, 0.0f, 2560.0f, 1440.0f };
			const Rect zoomCrop{ -120.0f, 175.0f, 2680.0f, 1265.0f };
			const Rect visible = Intersect(zoomCrop, output);
			Assert::IsTrue(std::fabs(visible.Height() - 1090.0f) < 0.01f);
			Assert::IsTrue(std::fabs(ProfileOverlayScale(visible.Height()) -
				1.2868f) < 0.01f);

			const Rect localPicture{
				visible.left - zoomCrop.left, visible.top - zoomCrop.top,
				visible.right - zoomCrop.left, visible.bottom - zoomCrop.top };
			const Rect localOutput{
				output.left - zoomCrop.left, output.top - zoomCrop.top,
				output.right - zoomCrop.left, output.bottom - zoomCrop.top };
			const Result result = PlaceTopLeft(localPicture, localOutput,
				760.0f * ProfileOverlayScale(visible.Height()),
				88.0f * ProfileOverlayScale(visible.Height()),
				ProfileOverlayTopInset(visible.Height()),
				ProfileOverlayLeftInset(visible.Height()));
			const Rect absolutePanel{
				result.panel.left + zoomCrop.left,
				result.panel.top + zoomCrop.top,
				result.panel.right + zoomCrop.left,
				result.panel.bottom + zoomCrop.top };
			Assert::IsTrue(absolutePanel.left >= visible.left);
			Assert::IsTrue(absolutePanel.top >= visible.top);
			Assert::IsTrue(absolutePanel.right <= visible.right);
			Assert::IsTrue(absolutePanel.bottom <= visible.bottom);
			Assert::IsTrue(std::fabs(absolutePanel.left - 60.2778f) < 0.01f);
			Assert::IsTrue(std::fabs(absolutePanel.top - 235.2778f) < 0.01f);
		}

		TEST_METHOD(LetterboxAndPillarboxUseTheirActualPictureBounds)
		{
			const Result letterbox = Place(
				{ 0.0f, 420.0f, 3840.0f, 1740.0f },
				{ 0.0f, 0.0f, 3840.0f, 2160.0f }, 420.0f, 600.0f);
			Assert::IsTrue(std::fabs(letterbox.panel.top - 1100.0f) < 0.01f);
			Assert::IsTrue(std::fabs(letterbox.panel.bottom - 1700.0f) < 0.01f);

			const Result pillarbox = Place(
				{ 480.0f, 0.0f, 3360.0f, 2160.0f },
				{ 0.0f, 0.0f, 3840.0f, 2160.0f }, 420.0f, 600.0f);
			Assert::IsTrue(std::fabs(pillarbox.panel.left - 2900.0f) < 0.01f);
			Assert::IsTrue(std::fabs(pillarbox.panel.right - 3320.0f) < 0.01f);
		}

		TEST_METHOD(FixedCropUsesTheFinalVisiblePictureInsteadOfThePhysicalScreen)
		{
			// A 2.00:1 fixed crop fitted to a 2.35:1 CIH screen leaves
			// pillarbox mattes. The OSD must follow the 2.00:1 visible picture,
			// never the wider physical screen rectangle.
			const Rect screen{ 0.0f, 0.0f, 3840.0f, 1634.0f };
			const Rect fixedCropPicture{ 286.0f, 0.0f, 3554.0f, 1634.0f };
			const Result result = Place(fixedCropPicture, screen, 420.0f, 600.0f);
			Assert::IsTrue(std::fabs(result.panel.left - 3094.0f) < 0.01f);
			Assert::IsTrue(std::fabs(result.panel.right - 3514.0f) < 0.01f);
			Assert::IsTrue(std::fabs(result.panel.top - 994.0f) < 0.01f);
			Assert::IsTrue(std::fabs(result.panel.bottom - 1594.0f) < 0.01f);
		}

		TEST_METHOD(SmallPictureScalesUniformlyAndRemainsContained)
		{
			const Result result = Place(
				{ 0.0f, 0.0f, 320.0f, 180.0f },
				{ 0.0f, 0.0f, 320.0f, 180.0f }, 420.0f, 600.0f);
			Assert::IsTrue(result.insetClamped);
			Assert::IsTrue(result.scale > 0.0f && result.scale < 1.0f);
			Assert::IsTrue(result.panel.left >= result.visiblePicture.left);
			Assert::IsTrue(result.panel.top >= result.visiblePicture.top);
			Assert::IsTrue(result.panel.right <= result.visiblePicture.right);
			Assert::IsTrue(result.panel.bottom <= result.visiblePicture.bottom);
			Assert::IsTrue(std::fabs(result.panel.Width() / result.panel.Height() -
				420.0f / 600.0f) < 0.001f);
		}

		TEST_METHOD(HalftoneTargetReceivesSourceAlphaAfterScaling)
		{
			std::array<uint8_t, 16> source = {
				0, 0, 0, 10, 0, 0, 0, 20,
				0, 0, 0, 30, 0, 0, 0, 40 };
			std::array<uint8_t, 64> target{};
			Assert::IsTrue(
				NativeStatsOverlayBitmap::RestoreScaledAlphaNearest(
					source.data(), 2, 2, 8, target.data(), 4, 4, 16));
			Assert::AreEqual(static_cast<unsigned char>(10), target[3]);
			Assert::AreEqual(static_cast<unsigned char>(20), target[15]);
			Assert::AreEqual(static_cast<unsigned char>(30), target[51]);
			Assert::AreEqual(static_cast<unsigned char>(40), target[63]);
		}

		TEST_METHOD(AlphaRestorationHonorsPaddingAndRejectsInvalidBuffers)
		{
			std::array<uint8_t, 24> source{};
			source[3] = 90;
			source[7] = 100;
			source[15] = 110;
			source[19] = 120;
			std::array<uint8_t, 40> target;
			target.fill(0xEE);
			Assert::IsTrue(
				NativeStatsOverlayBitmap::RestoreScaledAlphaNearest(
					source.data(), 2, 2, 12, target.data(), 4, 2, 20));
			Assert::AreEqual(static_cast<unsigned char>(90), target[3]);
			Assert::AreEqual(static_cast<unsigned char>(100), target[15]);
			Assert::AreEqual(static_cast<unsigned char>(110), target[23]);
			Assert::AreEqual(static_cast<unsigned char>(120), target[35]);
			Assert::AreEqual(static_cast<unsigned char>(0xEE), target[16]);
			Assert::AreEqual(static_cast<unsigned char>(0xEE), target[36]);
			Assert::IsFalse(
				NativeStatsOverlayBitmap::RestoreScaledAlphaNearest(
					nullptr, 2, 2, 8, target.data(), 4, 2, 16));
		}
	};
}
