#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleOpaqueCardRefinement.h>
#include <vector>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests {
TEST_CLASS(SubtitleOpaqueCardRefinementTests) {
    struct Frame {
        const int width=3840,height=2160;
        std::vector<uint8_t> pixels;
        Frame():pixels(size_t(width)*height*4,255) {
            Fill({0,0,width,263},0); Fill({0,1897,width,height},0);
        }
        void Fill(SubtitleBoxRect r,uint8_t level) {
            for(int y=r.top;y<r.bottom;++y) for(int x=r.left;x<r.right;++x) {
                const size_t p=(size_t(y)*width+x)*4;
                pixels[p]=pixels[p+1]=pixels[p+2]=level;pixels[p+3]=255;
            }
        }
        void Text(int left,int right,int top,int bottom) {
            for(int x=left;x<right;x+=34) Fill({x,top,(std::min)(right,x+15),bottom},220);
        }
        AnalysisLumaSource Source() const {
            AnalysisLumaSource s;s.data=pixels.data();s.dataBytes=pixels.size();
            s.width=width;s.height=height;s.rowBytes=width*4;
            s.format=AnalysisLumaFormat::NativeRgb;s.encoding=VideoFrameEncoding::BGRA_8BIT;
            s.colorspace=ColorSpace::REC_709;s.generation=1;return s;
        }
    };
    static bool Refine(const Frame& frame, SubtitleOpaqueCardRefinement& result) {
        return RefineSubtitleOpaqueCard(frame.Source(),{1903,1825,2094,1876},48,
            263,1897,28,550,512,512,result);
    }
public:
    TEST_METHOD(PartialTailSeedRecoversBothLinesInsideFiniteOpaqueCard) {
        Frame f;f.Fill({1457,1715,2390,1897},0);
        f.Text(1489,2354,1737,1785);f.Text(1751,2089,1825,1873);
        SubtitleOpaqueCardRefinement r;Assert::IsTrue(Refine(f,r));
        Assert::AreEqual(2,r.glyphRowCount);
        Assert::AreEqual(1457,r.interior.left);Assert::AreEqual(2390,r.interior.right);
        Assert::AreEqual(1715,r.interior.top);Assert::AreEqual(1897,r.interior.bottom);
        Assert::IsTrue(r.glyphRows[0].left<=1493 && r.glyphRows[0].right>=2350);
        Assert::IsTrue(r.glyphRows[1].left<=1755 && r.glyphRows[1].right>=2069);
        Assert::IsTrue(r.sampledPixels<20000 && !r.workLimit);
    }
    TEST_METHOD(UnboundedBlackSceneDoesNotManufactureCardEdges) {
        Frame f;f.Fill({0,263,3840,1897},0);f.Text(1751,2089,1825,1873);
        SubtitleOpaqueCardRefinement r;Assert::IsFalse(Refine(f,r));
        Assert::IsFalse(r.interior.Valid());Assert::IsTrue(r.sampledPixels<20000);
    }
    TEST_METHOD(SteppedBackingDoesNotAuthorizeItsRectangularUnion) {
        Frame f;f.Fill({1750,1715,2100,1800},0);f.Fill({1457,1800,2390,1897},0);
        f.Text(1780,2060,1737,1785);f.Text(1751,2089,1825,1873);
        SubtitleOpaqueCardRefinement r;Assert::IsFalse(Refine(f,r));
        Assert::IsFalse(r.interior.Valid());
    }
    TEST_METHOD(BrightPictureBoundaryStripCannotBecomeGlyphRow) {
        Frame f;f.Fill({1457,1715,2390,1894},0);
        f.Text(1489,2354,1737,1785);f.Text(1751,2089,1825,1873);
        f.Fill({1457,1894,2390,1897},180);
        SubtitleOpaqueCardRefinement r;Assert::IsTrue(Refine(f,r));
        Assert::AreEqual(1894,r.interior.bottom);Assert::AreEqual(2,r.glyphRowCount);
        Assert::IsTrue(r.glyphRows[1].bottom<1894);
    }
    TEST_METHOD(FiniteBackingAboveBarRetainsItsActualBottomEdge) {
        Frame f;f.Fill({1750,1815,2120,1889},0);f.Text(1770,2090,1825,1873);
        SubtitleOpaqueCardRefinement r;Assert::IsTrue(Refine(f,r));
        Assert::AreEqual(1889,r.interior.bottom);Assert::AreEqual(1,r.glyphRowCount);
        Assert::IsTrue(r.interior.bottom<1897);
    }
    TEST_METHOD(OptionalInnerSideBudgetExhaustionPreservesCompletedProof) {
        Frame f;f.Fill({200,1690,3400,1850},0);
        // This one-pixel whitespace seam independently nominates an inner
        // side. The first full-width proof completes before that optional
        // second trial consumes the remaining shared sample budget.
        f.Fill({205,1766,206,1800},220);
        for(int x=1400;x<2400;x+=40)f.Fill({x,1730,x+12,1766},220);
        SubtitleOpaqueCardRefinement r;
        Assert::IsTrue(RefineSubtitleOpaqueCard(f.Source(),{1400,1730,2400,1766},36,
            263,1897,28,550,512,512,r));
        Assert::IsTrue(r.workLimit);
        Assert::IsTrue(r.sampledPixels>131072 && r.sampledPixels<133000);
        Assert::AreEqual(200,r.interior.left);Assert::AreEqual(3400,r.interior.right);
        Assert::AreEqual(1690,r.interior.top);Assert::AreEqual(1850,r.interior.bottom);
        Assert::AreEqual(1,r.glyphRowCount);
        Assert::IsTrue(r.glyphRows[0].left<=1402 && r.glyphRows[0].right>=2370);
    }
    TEST_METHOD(InnerSideTrialPreservesDetachedDotAcrossSamplingPhases) {
        Frame f;f.Fill({200,1720,1800,1850},0);f.Fill({206,1660,1800,1720},0);
        // Dense neutral strokes close the wider lower proof while the
        // independently measured inner side proves the taller backing.
        f.Fill({600,1700,1400,1720},220);f.Fill({1000,1720,1001,1730},220);
        for(int x=600;x<1400;x+=40)f.Fill({x,1730,x+12,1767},220);
        // The first trial samples row 1768; the inner trial samples 1769.
        // This detached punctuation pixel must survive the improved proof.
        f.Fill({704,1768,705,1769},220);
        SubtitleOpaqueCardRefinement r;
        Assert::IsTrue(RefineSubtitleOpaqueCard(f.Source(),{600,1730,1400,1766},36,
            263,1897,28,550,512,512,r));
        Assert::IsFalse(r.workLimit);
        Assert::AreEqual(206,r.interior.left);Assert::AreEqual(1660,r.interior.top);
        Assert::AreEqual(1,r.glyphRowCount);
        Assert::IsTrue(r.glyphRows[0].left<=704 && r.glyphRows[0].right>704);
        Assert::IsTrue(r.glyphRows[0].top<=1768 && r.glyphRows[0].bottom>1768,
            L"A later sampling phase cannot revoke already proved punctuation");
    }
    TEST_METHOD(OffCenterWordSeedRecoversCardWithoutPromotingWeakOrColoredInk) {
        const int w=640,h=360;
        std::vector<uint16_t> pixels(size_t(w)*h*3/2,uint16_t(512<<6));
        auto fill=[&](int l,int t,int r,int b,int level) {
            for(int y=t;y<b;++y)for(int x=l;x<r;++x)pixels[y*w+x]=uint16_t(level<<6);
        };
        auto text=[&](int left,int top,int count) {
            for(int n=0;n<count;++n) {
                const int x=left+n*13;
                fill(x,top,x+8,top+14,700);fill(x+2,top+2,x+6,top+12,64);
            }
        };
        fill(0,0,w,h,200);fill(0,0,w,45,64);fill(0,315,w,h,64);
        fill(180,250,470,313,64);text(200,260,18);text(220,291,17);
        AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels.data());
        source.dataBytes=pixels.size()*2;source.width=w;source.height=h;
        source.rowBytes=w*2;source.chromaRowBytes=w*2;source.format=AnalysisLumaFormat::P010;
        SubtitleOpaqueCardRefinement r;
        Assert::IsTrue(RefineSubtitleOpaqueCard(source,{380,291,436,305},14,
            45,315,100,375,512,512,r));
        Assert::AreEqual(180,r.interior.left);Assert::AreEqual(470,r.interior.right);
        Assert::AreEqual(2,r.glyphRowCount);
        Assert::IsTrue(r.glyphRows[0].left<=201 && r.glyphRows[1].left<=221);
        for(bool colored:{false,true}) {
            fill(180,250,470,313,64);text(240,291,16);
            fill(200,291,215,305,colored?700:600);
            for(int y=291/2;y<(305+1)/2;++y)for(int x=200/2;x<(215+1)/2;++x) {
                const size_t uv=size_t(w)*h+y*w+x*2;
                pixels[uv]=uint16_t((colored?475:512)<<6);
                pixels[uv+1]=uint16_t((colored?441:512)<<6);
            }
            Assert::IsTrue(RefineSubtitleOpaqueCard(source,{380,291,436,305},14,
                45,315,100,375,512,512,r,650));
            Assert::AreEqual(1,r.glyphRowCount);
            Assert::IsTrue(r.glyphRows[0].left>=240,
                L"Card antialias coverage cannot promote weak or differently colored glyph candidates");
        }
    }};
}
