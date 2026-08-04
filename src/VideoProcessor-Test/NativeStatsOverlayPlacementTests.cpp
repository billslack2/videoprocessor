#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/NativeStatsOverlayPlacement.h>

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
	};
}
