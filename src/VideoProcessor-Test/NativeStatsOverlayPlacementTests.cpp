#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/NativeStatsOverlayPlacement.h>
#include <vprenderer/NativeStatsOverlayBitmap.h>
#include <vprenderer/AnamorphicPresentation.h>

#include <array>
#include <cmath>
#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace NativeStatsOverlayPlacement;

namespace
{
    AnamorphicPresentation::Screen ScreenFit(double width, double height, double aspect, double lens = 1.0)
    {
        return AnamorphicPresentation::FitScreen(true, aspect, lens, { 0, 0, width, height },
            AlphaSourceCrop::VerticalPictureAlignment::CENTER, 0);
    }
    Rect ScreenRect(const AnamorphicPresentation::Screen& screen)
    {
        return { float(screen.rect.left), float(screen.rect.top), float(screen.rect.right), float(screen.rect.bottom) };
    }
    void Contained(const Result& result)
    {
        Assert::IsTrue(result.panel.IsValid());Assert::IsTrue(std::isfinite(result.scale));
        Assert::IsTrue(result.panel.left >= result.visiblePicture.left - .001f);
        Assert::IsTrue(result.panel.top >= result.visiblePicture.top - .001f);
        Assert::IsTrue(result.panel.right <= result.visiblePicture.right + .001f);
        Assert::IsTrue(result.panel.bottom <= result.visiblePicture.bottom + .001f);
    }
}

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

		TEST_METHOD(StatsFlatAndScopeCompensateExternalOpticalZoom)
        {
            const auto flat=ScreenFit(3840,2160,16.0/9.0),scope=ScreenFit(3840,2160,2.35);
            const float flatH=ScreenRect(flat).Height(),scopeH=ScreenRect(scope).Height();
            const auto a=ResolveStatsSizing(true,flatH,2160),b=ResolveStatsSizing(true,scopeH,2160);
            Assert::AreEqual(2160.0f,flatH,.001f);Assert::AreEqual(float(3840.0/2.35),scopeH,.001f);
            Assert::AreEqual(1.25f,a.scale,.00001f);Assert::AreEqual(.9456265f,b.scale,.00001f);
            const float opticalZoom=flatH/scopeH;
            Assert::AreEqual(1.321875f,opticalZoom,.00001f);
            Assert::AreEqual(a.scale,b.scale*opticalZoom,.00001f);
            Assert::AreEqual(a.insetPixels,b.insetPixels*opticalZoom,.0001f);
        }

        TEST_METHOD(ScreenResolutionAndRawAspectMatrixKeepsProjectedSize)
        {
            const std::array<std::array<double,2>,6> rasters={{{1920,1080},{2560,1440},{3840,2160},{4096,2160},{1920,1200},{2048,858}}};
            for(const auto& raster:rasters)for(double aspect:{16.0/9.0,2.35,2.4}) {
                const auto screen=ScreenFit(raster[0],raster[1],aspect);Assert::IsTrue(screen.valid);
                const Rect rect=ScreenRect(screen);const auto sizing=ResolveStatsSizing(true,rect.Height(),float(raster[1]));
                const auto placement=Place(rect,{0,0,float(raster[0]),float(raster[1])},578,600,sizing.insetPixels,sizing.scale);
                Contained(placement);Assert::IsFalse(placement.insetClamped);
                Assert::AreEqual(600.0f*1.25f/2160.0f,placement.panel.Height()/rect.Height(),.000001f);
                Assert::AreEqual(40.0f/2160.0f,(rect.bottom-placement.panel.bottom)/rect.Height(),.000001f);
            }
        }

        TEST_METHOD(HeightLimitedScreenFitDoesNotInventAnExtraAspectCorrection)
        {
            const auto flat=ScreenFit(2048,858,16.0/9.0),scope=ScreenFit(2048,858,2.35);
            Assert::AreEqual(858.0f,ScreenRect(flat).Height(),.001f);
            Assert::AreEqual(858.0f,ScreenRect(scope).Height(),.001f);
            Assert::AreEqual(ResolveStatsSizing(true,858,858).scale,
                ResolveStatsSizing(true,ScreenRect(scope).Height(),858).scale,.000001f);
        }

        TEST_METHOD(MovieFitCropAndSoftwareZoomDoNotChangeNominalSize)
        {
            const auto screen=ScreenFit(3840,2160,2.35);const Rect output{0,0,3840,2160};
            const auto sizing=ResolveStatsSizing(true,ScreenRect(screen).Height(),2160);
            for(double movieAspect:{4.0/3.0,16.0/9.0,2.0,2.35,2.4}) {
                const auto fitted=AnamorphicPresentation::FitPicture(movieAspect,1.0,screen,
                    AlphaSourceCrop::VerticalPictureAlignment::CENTER);Assert::IsTrue(fitted.valid);
                Rect picture{float(fitted.picture.left),float(fitted.picture.top),float(fitted.picture.right),float(fitted.picture.bottom)};
                const auto placed=Place(picture,output,578,600,sizing.insetPixels,sizing.scale);
                Contained(placed);Assert::IsFalse(placed.insetClamped);Assert::AreEqual(sizing.scale,placed.scale,.000001f);
            }
            for(const Rect zoom:std::array<Rect,3>{{{-200,-100,4040,2260},{0,400,3840,1760},{300,200,3540,1960}}}) {
                const auto placed=Place(zoom,output,578,600,sizing.insetPixels,sizing.scale);
                Contained(placed);Assert::IsFalse(placed.insetClamped);Assert::AreEqual(sizing.scale,placed.scale,.000001f);
            }
        }

        TEST_METHOD(AnamorphicLensCorrectsWidthAndHorizontalMarginExactlyOnce)
        {
            for(double lens:{1.0,1.321875}) {
                const auto screen=ScreenFit(3840,2160,2.35,lens);const Rect rect=ScreenRect(screen);
                const auto sizing=ResolveStatsSizing(true,rect.Height(),2160);
                const auto result=Place(rect,{0,0,3840,2160},AnamorphicPresentation::OverlayWidth(578,lens),600,
                    sizing.insetPixels,sizing.scale,AnamorphicPresentation::OverlayWidth(sizing.insetPixels,lens));
                Contained(result);Assert::IsFalse(result.insetClamped);
                Assert::AreEqual(578.0/600.0,result.panel.Width()*lens/result.panel.Height(),.000001);
                Assert::AreEqual(600.0*1.25/2160.0,double(result.panel.Height()/rect.Height()),.000001);
                Assert::AreEqual(double(rect.bottom-result.panel.bottom),(rect.right-result.panel.right)*lens,.001);
            }
            // A lens that fills this scope screen vertically needs no additional
            // CIH squeeze beyond the fitted screen height already used above.
            const auto lensScreen=ScreenFit(3840,2160,2.35,1.321875);
            Assert::AreEqual(2160.0f,ScreenRect(lensScreen).Height(),.001f);
            Assert::AreEqual(1.25f,ResolveStatsSizing(true,ScreenRect(lensScreen).Height(),2160).scale,.000001f);
        }

        TEST_METHOD(SweepAndProfileUseSameStableScreenGeometryFactor)
        {
            const float flatH=ScreenRect(ScreenFit(3840,2160,16.0/9.0)).Height();
            const float scopeH=ScreenRect(ScreenFit(3840,2160,2.35)).Height();const float zoom=flatH/scopeH;
            const auto flatSweep=ResolveSweepSizing(true,flatH,2160),scopeSweep=ResolveSweepSizing(true,scopeH,2160);
            Assert::AreEqual(flatSweep.scale,scopeSweep.scale*zoom,.000001f);
            Assert::AreEqual(flatSweep.insetPixels,scopeSweep.insetPixels*zoom,.00001f);
            for(float movieHeight:{400.0f,1080.0f,1634.0f,2160.0f}) {
                const auto flatProfile=ResolveProfileSizing(true,flatH,2160,movieHeight);
                const auto scopeProfile=ResolveProfileSizing(true,scopeH,2160,movieHeight);
                Assert::AreEqual(2.55f,flatProfile.scale,.000001f);Assert::AreEqual(90.0f,flatProfile.insetPixels,.00001f);
                Assert::AreEqual(flatProfile.scale,scopeProfile.scale*zoom,.000001f);
                Assert::AreEqual(flatProfile.insetPixels,scopeProfile.insetPixels*zoom,.00001f);
            }
        }

        TEST_METHOD(ScaledSweepAndProfilePlacementRetainPhysicalMarginsAndTypography)
        {
            const Rect output{0,0,3840,2160};
            for(double lens:{1.0,1.321875}) {
                const auto screen=ScreenFit(3840,2160,2.35,lens);const auto fitted=AnamorphicPresentation::FitPicture(4.0/3.0,lens,screen,
                    AlphaSourceCrop::VerticalPictureAlignment::CENTER);
                const Rect picture{float(fitted.picture.left),float(fitted.picture.top),float(fitted.picture.right),float(fitted.picture.bottom)};
                const auto sweep=ResolveSweepSizing(true,ScreenRect(screen).Height(),2160);
                const float sweepInsetX=AnamorphicPresentation::OverlayWidth(sweep.insetPixels,lens);
                const auto topRight=PlaceTopRight(picture,output,AnamorphicPresentation::OverlayWidth(860,lens),112,
                    sweep.insetPixels,sweep.scale,sweepInsetX);Contained(topRight);Assert::IsFalse(topRight.insetClamped);
                Assert::AreEqual(double(topRight.panel.top-picture.top),(picture.right-topRight.panel.right)*lens,.001);
                Assert::AreEqual(112.0/2160.0,double(topRight.panel.Height()/ScreenRect(screen).Height()),.000001);
                const auto profile=ResolveProfileSizing(true,ScreenRect(screen).Height(),2160,picture.Height());
                const float profileInsetX=AnamorphicPresentation::OverlayWidth(profile.insetPixels,lens);
                const auto topLeft=PlaceTopLeft(picture,output,AnamorphicPresentation::OverlayWidth(760,lens)*profile.scale,
                    88*profile.scale,profile.insetPixels,profileInsetX,1.0f,profileInsetX);Contained(topLeft);Assert::IsFalse(topLeft.insetClamped);
                Assert::AreEqual(double(topLeft.panel.top-picture.top),(topLeft.panel.left-picture.left)*lens,.001);
                Assert::AreEqual(88.0*2.55/2160.0,double(topLeft.panel.Height()/ScreenRect(screen).Height()),.000001);
            }
        }

        TEST_METHOD(DisabledScalingPreservesLegacyStatsAndBannerSizing)
        {
            for(float height:{720.0f,1080.0f,1440.0f,2160.0f}) {
                const auto stats=ResolveStatsSizing(false,height*.75f,height),sweep=ResolveSweepSizing(false,height*.75f,height);
                Assert::AreEqual(1.0f,stats.scale);Assert::AreEqual(40.0f,stats.insetPixels);
                Assert::AreEqual(1.0f,sweep.scale);Assert::AreEqual(40.0f,sweep.insetPixels);
                const auto profile=ResolveProfileSizing(false,height*.75f,height,height*.5f);
                Assert::AreEqual(ProfileOverlayScale(height*.5f),profile.scale);
                Assert::AreEqual(ProfileOverlayTopInset(height*.5f),profile.insetPixels);
            }
            const auto sizing=ResolveStatsSizing(false,1634,2160);const Rect output{0,0,3840,2160};
            const auto old=Place(output,output,578,600),current=Place(output,output,578,600,sizing.insetPixels,sizing.scale);
            Assert::AreEqual(old.panel.left,current.panel.left);Assert::AreEqual(old.panel.top,current.panel.top);
        }

        TEST_METHOD(SizingRejectsInvalidGeometryAndUsesDocumentedFallback)
        {
            const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
            for(float bad:{0.0f,-1.0f,nan,inf}) {
                const auto fallback=ResolveStatsSizing(true,bad,1080);Assert::AreEqual(.625f,fallback.scale);
                Assert::AreEqual(20.0f,fallback.insetPixels);
                const auto both=ResolveStatsSizing(true,bad,bad);Assert::AreEqual(1.25f,both.scale);
                const auto disabled=ResolveStatsSizing(false,bad,bad);Assert::AreEqual(1.0f,disabled.scale);
            }
            Assert::AreEqual(.625f,ResolveStatsSizing(true,2160,1080).scale);
            Assert::AreEqual(.625f,ResolveStatsSizing(true,1080,nan).scale);
        }

        TEST_METHOD(PlacementRejectsNonfiniteRectsAndDimensions)
        {
            const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
            const Rect output{0,0,1920,1080};
            for(const Rect bad:std::array<Rect,4>{{{nan,0,1920,1080},{0,0,inf,1080},{100,0,0,1080},{0,0,0,0}}}) {
                const auto fallback=Place(bad,output,578,600);Assert::IsTrue(fallback.usedOutputFallback);Contained(fallback);
                const auto noOutput=Place(output,bad,578,600);Assert::IsFalse(noOutput.panel.IsValid());Assert::AreEqual(0.0f,noOutput.scale);
            }
            for(float bad:{0.0f,-1.0f,nan,inf}) {
                Assert::IsFalse(Place(output,output,bad,600).panel.IsValid());
                Assert::IsFalse(Place(output,output,578,bad).panel.IsValid());
            }
        }

        TEST_METHOD(NonfiniteScaleAndInsetsAlwaysProduceFiniteContainedPanels)
        {
            const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
            const Rect output{0,0,1920,1080};
            for(float bad:{0.0f,-1.0f,nan,inf}) {
                const auto placed=Place(output,output,578,600,bad,bad,bad);Contained(placed);
                Assert::IsTrue(placed.scale<=1.0f);Contained(PlaceTopRight(output,output,578,600,bad,bad,bad));
                Contained(PlaceTopLeft(output,output,578,600,bad,bad,bad,bad));
            }
        }

        TEST_METHOD(TinyVisiblePictureMayUniformlyShrinkPhysicalSizingForContainment)
        {
            const Rect output{0,0,3840,2160},tiny{120,200,220,250};
            const auto sizing=ResolveStatsSizing(true,1634,2160);
            const auto placed=Place(tiny,output,578,600,sizing.insetPixels,sizing.scale);Contained(placed);
            Assert::IsTrue(placed.insetClamped);Assert::IsTrue(placed.scale<sizing.scale);
            Assert::AreEqual(578.0f/600.0f,placed.panel.Width()/placed.panel.Height(),.00001f);
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
