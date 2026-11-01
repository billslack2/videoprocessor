#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/AutoPictureAlignment.h>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests {
using namespace AutoPictureAlignment;
TEST_CLASS(AutoPictureAlignmentTests) {
public:
    TEST_METHOD(VisibleScopeEdgeDiffersFromLegacyRasterEdge) {
        Rect output{0,0,1920,1080};
        for (auto a:{Alignment::TOP,Alignment::BOTTOM}) {
            auto screen=FitScreen(true,true,2.35,1,output,a,280);
            auto legacy=FitScreen(false,true,2.35,1,output,a,0);
            Assert::IsTrue(std::abs(screen.rect.top-legacy.rect.top)>100);
            auto fit=AnamorphicPresentation::FitPicture(2.4,1,screen,a);
            ActivePicture active{{0,140,1920,940},true,false};
            auto d=Place(true,a,0,output,screen.rect,active.bounds,fit.picture,active);
            double edge=a==Alignment::TOP?d.picture.top:d.picture.bottom;
            Assert::AreEqual(a==Alignment::TOP?screen.rect.top:screen.rect.bottom,edge,0.51);
            Assert::AreEqual(fit.picture.bottom-fit.picture.top,d.picture.bottom-d.picture.top,0.00001);
        }
    }
    TEST_METHOD(AsymmetricEncodedBarsMapBeforeSignedTrim) {
        Rect output{0,0,1920,1080}, screen{0,130,1920,950},source{0,0,1920,1080},dest{0,130,1920,950};
        ActivePicture active{{0,50,1920,1000},true,false};
        for (auto a:{Alignment::TOP,Alignment::BOTTOM}) {
            auto base=Place(true,a,0,output,screen,source,dest,active);
            for(int trim:{-12,12}) {
                auto d=Place(true,a,trim,output,screen,source,dest,active);
                Assert::AreEqual(base.picture.top+trim,d.picture.top,0.00001);
                Assert::AreEqual(820.0,d.picture.bottom-d.picture.top,0.00001);
            }
        }
    }
    TEST_METHOD(SafetyLimitsPreserveOutputAndSubtitleEnvelope) {
        Rect output{0,0,1920,1080},screen{0,130,1920,950},source{0,0,1920,1080},dest=screen;
        ActivePicture active{{0,50,1920,850},true,true};
        auto d=Place(true,Alignment::BOTTOM,100000,output,screen,source,dest,active);
        Assert::AreEqual(std::string("subtitle-envelope"),std::string(d.reason));
        Assert::AreEqual(dest.top,d.picture.top,0.00001);
        active.protectEnvelope=false;
        d=Place(true,Alignment::BOTTOM,100000,output,screen,source,dest,active);
        Assert::AreEqual(output.bottom,d.picture.bottom,0.00001);
        d=Place(true,Alignment::TOP,-100000,output,screen,source,dest,active);
        Assert::AreEqual(output.top,d.picture.top,0.00001);
    }
    TEST_METHOD(UnknownCenterManualAndFullRasterDoNotInventGeometry) {
        Rect r{0,0,1920,1080}; ActivePicture active{r,false,false};
        auto d=Place(true,Alignment::TOP,40,r,r,r,r,active);
        Assert::AreEqual(std::string("geometry-unavailable"),std::string(d.reason));
        active.available=true;
        Assert::AreEqual(r.top,Place(true,Alignment::CENTER,40,r,r,r,r,active).picture.top);
        Assert::AreEqual(r.top,Place(false,Alignment::TOP,40,r,r,r,r,active).picture.top);
        Assert::AreEqual(r.top,Place(true,Alignment::TOP,40,r,r,r,r,active).picture.top);
    }
    TEST_METHOD(ArbitraryAspectsLensResolutionAndNoAccumulation) {
        for (double lens:{0.75,1.0,1.33}) for (int height:{1080,2160,1081})
        for (double aspect:{1.333,1.777,2.137,2.35,2.417,3.0})
        for (auto a:{Alignment::TOP,Alignment::BOTTOM}) {
            Rect output{0,0,1920.0,height*1.0};
            auto screen=FitScreen(true,true,2.35,lens,output,a,280);
            auto fit=AnamorphicPresentation::FitPicture(aspect,lens,screen,a);
            ActivePicture active{{0,0,3840,3840/aspect},true,false};
            auto d=Place(true,a,3,output,screen.rect,active.bounds,fit.picture,active);
            auto repeat=Place(true,a,3,output,screen.rect,active.bounds,fit.picture,active);
            Assert::AreEqual(d.picture.top,repeat.picture.top);
            Assert::AreEqual(fit.picture.right-fit.picture.left,d.picture.right-d.picture.left,0.00001);
            Assert::AreEqual(fit.picture.bottom-fit.picture.top,d.picture.bottom-d.picture.top,0.00001);
            Assert::IsTrue(d.picture.top>=output.top-0.00001 && d.picture.bottom<=output.bottom+0.00001);
        }
    }
    TEST_METHOD(NonlinearInteriorEdgesInvertBothNlsCurves) {
        for(double scale:{0.8,1.0,1.32,1.5}) for(double curve:{1.0,2.0,2.9})
        for(double protection:{0.0,0.2,0.45}) for(double source:{0.0,0.05,0.3,0.5,0.9,1.0}) {
            Warp w{scale,curve,protection,true};
            Assert::AreEqual(source,SampleCoordinate(OutputCoordinate(source,w),w),0.000001);
        }
        Rect out{0,0,1920,1080},screen{0,130,1920,950},src{0,0,1920,1080};
        ActivePicture active{{0,80,1920,1000},true,false}; Warp w{1.2,2,0.2,true};
        auto d=Place(true,Alignment::BOTTOM,0,out,screen,src,screen,active,w);
        double edge=d.picture.top+OutputCoordinate(1000.0/1080,w)*820;
        Assert::AreEqual(screen.bottom,edge,0.51);
        w.known=false;
        Assert::AreEqual(std::string("unknown-nls-mapping"),std::string(Place(true,Alignment::TOP,0,out,screen,src,screen,active,w).reason));
    }
    TEST_METHOD(TallerThanScreenPinsOneEdgeWithoutChangingSize)
    {
        Rect output{0,0,1920,1080},screen{0,130,1920,950},source{0,0,1920,900},dest{0,90,1920,990};
        ActivePicture active{source,true,false};
        auto top=Place(true,Alignment::TOP,0,output,screen,source,dest,active);
        auto bottom=Place(true,Alignment::BOTTOM,0,output,screen,source,dest,active);
        Assert::AreEqual(screen.top,top.picture.top);
        Assert::AreEqual(screen.bottom,bottom.picture.bottom);
        Assert::AreEqual(900.0,top.picture.bottom-top.picture.top);
        Assert::AreEqual(900.0,bottom.picture.bottom-bottom.picture.top);
        Rect tooTall{0,0,1920,1200};
        auto limited=Place(true,Alignment::BOTTOM,0,output,screen,source,tooTall,active);
        Assert::AreEqual(std::string("picture-exceeds-output"),std::string(limited.reason));
        Assert::AreEqual(1200.0,limited.picture.bottom);
    }
    TEST_METHOD(SubtitleReleaseAndGeometryRecoveryRestoreRestingPosition) {
        Rect out{0,0,1920,1080},screen{0,130,1920,950},src{0,0,1920,1080};
        ActivePicture active{{0,80,1920,850},true,false};
        auto resting=Place(true,Alignment::BOTTOM,8,out,screen,src,screen,active);
        active.protectEnvelope=true;auto caption=Place(true,Alignment::BOTTOM,8,out,screen,src,screen,active);
        Assert::IsTrue(caption.picture.top!=resting.picture.top);
        active.available=false;
        Assert::AreEqual(screen.top,Place(true,Alignment::BOTTOM,8,out,screen,src,screen,active).picture.top);
        active.available=true;active.protectEnvelope=false;
        Assert::AreEqual(resting.picture.top,Place(true,Alignment::BOTTOM,8,out,screen,src,screen,active).picture.top);
    }
}; }
