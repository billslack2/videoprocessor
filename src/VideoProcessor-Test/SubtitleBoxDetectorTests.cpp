#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleBoxDetector.h>
#include <SubtitleOpaqueCardRefinement.h>
#include <vector>
#include <RendererConfigView.h>
#include <RendererProfileConfig.h>
#include <SubtitleGeneratedGrayStyle.h>
#include <fstream>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests {
TEST_CLASS(SubtitleBoxDetectorTests) {
    static constexpr int W=640,H=360;
    static void Fill(std::vector<uint16_t>& f,int l,int t,int r,int b,int value) {
        for(int y=t;y<b;++y) for(int x=l;x<r;++x) f[y*W+x]=static_cast<uint16_t>(value<<6);
    }
    static void PanelToBar(std::vector<uint16_t>& f,int left,int top,int right) {
        Fill(f,left,top,right,315,64);
    }
    static std::vector<uint16_t> Frame() {
        std::vector<uint16_t> f(W*H*3/2,static_cast<uint16_t>(512<<6));
        Fill(f,0,0,W,H,64); Fill(f,0,45,W,315,200); return f;
    }
    static void Text(std::vector<uint16_t>& f,int l,int t,int count,int white=510) {
        // Outlined hollow letter forms, with disconnected inter-word spacing.
        for(int n=0;n<count;++n) {
            int x=l+n*13;
            Fill(f,x-1,t-1,x+9,t+15,64);
            Fill(f,x,t,x+8,t+14,white); Fill(f,x+2,t+2,x+6,t+12,64);
        }
    }
    static void JoinedArabicWord(std::vector<uint16_t>& f,int left,int top,
        int width,int height,int white=510) {
        // Script-neutral raster proxy: a joined horizontal body with repeated
        // vertical strokes and detached dots/diacritics. The body is one
        // connected component and is deliberately much wider than 2x height.
        const int joinY=top+height*2/3;
        Fill(f,left,joinY,left+width,joinY+3,white);
        for(int x=left+7;x<left+width-5;x+=20) {
            Fill(f,x,top+2,x+3,top+height-3,white);
            Fill(f,x+8,top+4,x+11,top+height-1,white);
        }
        Fill(f,left+2,top+height/2,left+5,top+height-2,white);
        Fill(f,left+width-5,top+3,left+width-2,top+height/2,white);
        for(int x=left+12;x<left+width-5;x+=31)
            Fill(f,x,top,x+2,top+2,white);
    }
    static void UnknownAccessoryGlyph(std::vector<uint16_t>& f,int left,int top,int shape) {
        if(shape==0) {
            // Two disconnected note-like symbols; recognition must use their
            // raster and relation to the caption, not a known character list.
            Fill(f,left+1,top+8,left+5,top+12,510);
            Fill(f,left+4,top+1,left+6,top+10,510);
            Fill(f,left+9,top+6,left+13,top+10,510);
            Fill(f,left+12,top,left+14,top+8,510);
        } else {
            // An unfamiliar compact mark with a different silhouette.
            Fill(f,left+1,top+5,left+4,top+8,510);
            Fill(f,left+3,top+2,left+11,top+5,510);
            Fill(f,left+9,top+1,left+12,top+11,510);
            Fill(f,left+5,top+9,left+12,top+12,510);
        }
    }
    static AnalysisLumaSource Source(const std::vector<uint16_t>& f,uint64_t gen=1) {
        AnalysisLumaSource s; s.data=reinterpret_cast<const uint8_t*>(f.data());
        s.dataBytes=f.size()*2; s.width=W;s.height=H;s.rowBytes=W*2;s.chromaRowBytes=W*2;
        s.format=AnalysisLumaFormat::P010;s.generation=gen;return s;
    }
public:
    TEST_METHOD(BlackRimCannotHideSceneInsideNearBarComponentEnvelope) {
        for(bool atTop:{false,true})for(bool blackInterior:{false,true})for(int width:{24,64}) {
            auto f=Frame();const int height=18;
            const int x=320-width/2,y=atTop?49:315-height-4;
            Fill(f,x-6,y-4,x+width+6,y+height+4,64);
            Fill(f,x,y,x+width,y+height,510);
            Fill(f,x+3,y+3,x+width-3,y+height-3,blackInterior?64:180);
            SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
            const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::AreEqual(blackInterior,result.detected,
                L"black exterior cannot conceal picture inside a shape; real black-backed connected glyphs stay eligible");
        }
    }

    TEST_METHOD(NearBarSceneOpeningCannotBorrowCrossingGlyphBackingException) {
        for(bool atTop:{false,true})for(bool backed:{false,true})for(int width:{24,64}) {
            auto f=Frame();const int height=18;
            const int x=320-width/2,y=atTop?45:315-height;
            // A scene opening touches the picture edge, has a dark interior,
            // and one black horizontal rail. No ink enters the encoded bar.
            Fill(f,x,y,x+width,y+height,510);
            Fill(f,x+2,y+2,x+width-2,y+height-2,64);
            Fill(f,x,atTop?y+height:y-3,x+width,atTop?y+height+3:y,64);
            if(backed) {
                Fill(f,x-5,y,x,y+height,64);
                Fill(f,x+width,y,x+width+5,y+height,64);
            }
            SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
            const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::AreEqual(backed,result.detected,
                L"a picture-only shape needs distributed backing; a real backed connected glyph remains eligible");
        }
    }

    TEST_METHOD(CompactConnectedBarGlyphCanSeedCaptionWithoutLatinWordAspect) {
        for(bool solid:{false,true})for(bool inPicture:{false,true}) {
            auto f=Frame();const int y=inPicture?280:322;
            Fill(f,306,y,334,y+24,510);
            if(!solid)Fill(f,310,y+4,330,y+20,64);
            SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
            if(!solid && !inPicture) {
                Assert::IsTrue(result.detected);
                Assert::IsTrue(result.lineBounds[0].left<=306 && result.lineBounds[0].right>=334);
            } else Assert::IsFalse(result.detected,L"solid patches and isolated picture glyphs cannot borrow bar authority");
        }
    }


    TEST_METHOD(AdjacentRasterMarksKeepCaptureBoundedToTheConfirmedCaption) {
        auto f=Frame();Text(f,195,325,18);
        // Adjacent non-letter and a separate far-away mark.
        UnknownAccessoryGlyph(f,430,326,1);Fill(f,515,326,518,329,510);
        SubtitleBoxDetector detector;SubtitleBoxResult result;
        for(int i=0;i<4;++i)result=detector.Analyze(Source(f),45,315,i+1,1);
        Assert::IsTrue(result.detected);
        Assert::IsTrue(result.lineBounds[0].right>=442,L"nearby detached mark must belong to capture");
        Assert::IsTrue(result.bounds.right<500,L"distant black-bar content must not chain into caption");
    }

    TEST_METHOD(NearBarProximityCannotLearnFontScaleFromOppositePictureEdge) {
        for(bool sceneShape:{false,true})for(int phase=0;phase<2;++phase) {
            auto f=Frame();Fill(f,180,268,470,313,64);
            Text(f,200,275,20);Text(f,240,296,12);
            // Large bright picture content is close to the opposite bar, but
            // never enters it and has no opaque subtitle backing. A one-row
            // scene change must not overwrite this bottom cue's glyph scale.
            if(sceneShape)Fill(f,250,46+phase,630,82+phase,510);
            SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
            const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
            Assert::IsTrue(result.bounds.left<=200 && result.bounds.right>=455);
            Assert::IsTrue(result.bounds.top<=275 && result.bounds.bottom>=310);
            Assert::IsTrue(result.bounds.top>250,L"opposite-edge scenery cannot join the subtitle");
        }
    }

    TEST_METHOD(OpaqueCaptionCalibrationIgnoresUnrelatedBarFlecks) {
        for(bool darkScene:{false,true})for(int variant:{0,1,2,3}) {
            auto f=Frame();if(darkScene)Fill(f,0,45,W,315,64);
            Fill(f,170,281,470,312,64);Text(f,180,289,21);
            Fill(f,12,330,14,332,variant==0?703:variant==1?1023:510);
            if(variant==2)f[W*H+(330/2)*W+12]=uint16_t(650<<6);
            if(variant==3)f[W*H+(330/2)*W+13]=uint16_t(650<<6);
            SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
            const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(result.detected,
                L"four unrelated bright/colored bar pixels must not define the caption palette");
            Assert::IsTrue(result.nearBarEligibilityMeasured && result.nearBarEligible);
            Assert::IsTrue(result.lineBounds[0].left<=180 && result.lineBounds[0].right>=448);
        }
    }
    TEST_METHOD(OpaqueCaptionCalibrationIgnoresDistantBrighterPictureText) {
        for(int pictureRow:{249,270}) {
            auto f=Frame();Fill(f,170,281,470,312,64);Text(f,180,289,21);
            Text(f,50,pictureRow,40,1023);
            SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
            const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
            Assert::IsTrue(result.lineBounds[0].top>=289 && result.lineBounds[0].bottom<=303,
                L"brighter unbacked content, inside or outside the corridor, cannot own the caption palette");
        }
    }
    TEST_METHOD(LocalCaptionPaletteKeepsNearestCredibleRowOnBlackScenery) {
        auto f=Frame();Fill(f,0,45,W,315,64);
        Text(f,180,289,21);Text(f,50,270,40,1023);
        SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);
        bool nearestPresent=false;
        for(int i=0;i<result.lineCount;++i)
            nearestPresent=nearestPresent || (result.lineBounds[i].top<=289 &&
                result.lineBounds[i].bottom>=303 && result.lineBounds[i].left<=180 &&
                result.lineBounds[i].right>=448);
        Assert::IsTrue(nearestPresent,
            L"a brighter larger backed row must not make the nearer valid caption disappear");
    }
    TEST_METHOD(UnbackedPictureFlecksCannotStarveOpaqueCaptionComponents) {
        for(int noise:{747,800}) {
            auto f=Frame();Fill(f,170,281,470,312,64);Text(f,180,289,21);
            for(int n=0;n<noise;++n) {
                const int x=10+(n%40)*15,y=50+(n/40)*2;
                Fill(f,x,y,x+1,y+1,510);
            }
            SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
            const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(result.detected);Assert::IsFalse(result.workLimit);
            Assert::IsTrue(result.diagnosticComponentCount<100,
                L"unbacked opposite-band texture must not consume caption identities");
            const auto ink=detector.InkSnapshot();Assert::IsTrue(ink && ink->rawEvidenceComplete);
            Assert::IsTrue(ink->Get(10,50),L"rejected ownership must not erase raw observation evidence");
            Assert::IsFalse(ink->Black(10,50));
        }
    }
    TEST_METHOD(NearBarAdmissionUsesUnpaddedFiniteCardEdgeAtNativeResolution) {
        for(int scale:{1,6})for(int gap:{19,20,21}) {
            const int width=W*scale,height=H*scale,pictureTop=45*scale,pictureBottom=315*scale;
            std::vector<uint16_t> f(size_t(width)*height*3/2,uint16_t(512<<6));
            auto fill=[&](int l,int t,int r,int b,int value) {
                for(int y=t;y<b;++y)for(int x=l;x<r;++x)f[size_t(y)*width+x]=uint16_t(value<<6);
            };
            fill(0,0,width,height,64);fill(0,pictureTop,width,pictureBottom,200);
            const int cardBottom=pictureBottom-gap,glyphTop=cardBottom-23*scale;
            fill(170*scale,cardBottom-31*scale,470*scale,cardBottom,64);
            for(int n=0;n<21;++n) {
                const int x=(180+n*13)*scale;
                fill(x,glyphTop,x+8*scale,glyphTop+14*scale,510);
                fill(x+2*scale,glyphTop+2*scale,x+6*scale,glyphTop+12*scale,64);
            }
            auto source=Source(f);source.width=width;source.height=height;
            source.rowBytes=source.chromaRowBytes=width*2;
            SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
            const auto result=detector.Analyze(source,pictureTop,pictureBottom,1,1);
            Assert::AreEqual(gap<=20,result.detected);
            Assert::IsTrue(result.nearBarEligibilityMeasured);
            Assert::AreEqual(gap<=20,result.nearBarEligible);
            Assert::AreEqual(gap,result.diagnosticNearBarGap,
                L"sampling phase and cleanup guard must not change the configured source-pixel distance");
            Assert::IsFalse(result.diagnosticNearBarGapInferred);
        }
    }
    TEST_METHOD(HiddenBlackCardEdgeUsesFiniteVerifiedSupportRatherThanCleanupPadding) {
        for(int gap:{15,19,20,30}) {
            auto f=Frame();Fill(f,0,45,W,315,64);
            const int cardBottom=315-gap;
            Fill(f,170,cardBottom-31,470,cardBottom,64);Text(f,180,cardBottom-23,21);
            SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
            const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::AreEqual(gap<=20,result.detected);
            if(result.detected) {
                Assert::IsTrue(result.nearBarEligibilityMeasured && result.nearBarEligible);
                Assert::IsTrue(result.diagnosticNearBarGapInferred,
                    L"an edge hidden in black scenery is explicitly inferred, not falsely measured");
                Assert::AreEqual(gap-5,result.diagnosticNearBarGap,
                    L"black support is bounded to one glyph height beyond the actual glyphs");
                Assert::IsTrue(result.sourcePanel.bottom<295,
                    L"eligibility evidence must not enlarge cleanup into the connected black scene");
            }
        }
    }

    TEST_METHOD(FiniteOpaqueCardRecoversBothRowsFromPartialWordSeed) {
        auto f=Frame();Fill(f,180,250,470,313,64);
        Text(f,200,260,18);Text(f,220,291,17);
        SubtitleOpaqueCardRefinement refined;
        Assert::IsTrue(RefineSubtitleOpaqueCard(Source(f),{380,291,436,305},14,
            45,315,100,375,512,512,refined));
        Assert::AreEqual(2,refined.glyphRowCount);
        Assert::IsTrue(refined.interior.left<=181 && refined.interior.right>=469);
        Assert::IsTrue(refined.glyphRows[0].left<=201 && refined.glyphRows[0].top<=261);
        Assert::IsTrue(refined.glyphRows[1].left<=221 && refined.glyphRows[1].right>=435);
        Assert::IsTrue(refined.sampledPixels<131072 && !refined.workLimit);
        // The same seed cannot turn an unbounded scene shadow into a card.
        Fill(f,0,230,W,315,64);Text(f,380,291,5);
        Assert::IsFalse(RefineSubtitleOpaqueCard(Source(f),{380,291,436,305},14,
            45,315,100,375,512,512,refined));
    }

    TEST_METHOD(NativeCardCompletionKeepsQuestionLoopAcrossUhdPhasesAndBoundaryRows) {
        const int w=3840,h=2160;
        for(int phaseX=0;phaseX<4;++phaseX)for(int phaseY=0;phaseY<4;++phaseY)for(int edge=0;edge<2;++edge) {
            std::vector<uint16_t> f(size_t(w)*h*3/2,uint16_t(512<<6));
            auto fill=[&](int left,int top,int right,int bottom,int code) {
                for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)f[size_t(y)*w+x]=uint16_t(code<<6);
            };
            fill(0,0,w,h,64);fill(0,262,w,1898,200);fill(1416,1820,2432,1898,64);
            for(int n=0;n<19;++n) {
                const int x=1444+n*48+phaseX;
                fill(x,1852+phaseY,x+32,1908+phaseY,700);
                fill(x+8,1860+phaseY,x+24,1900+phaseY,64);
            }
            const int x=2360+phaseX,y=1852+phaseY;
            // A thin outer loop and connecting stroke fall between coarse
            // sample columns. The dot can still establish the shorter group.
            fill(x,y,x+24,y+4,700);fill(x+23,y+3,x+32,y+4,700);
            fill(x+30,y+3,x+32,y+20,700);fill(x+10,y+18,x+32,y+20,700);
            fill(x+10,y+18,x+13,y+32,700);fill(x+10,y+44,x+13,y+47,700);
            auto source=Source(f);source.width=w;source.height=h;source.rowBytes=source.chromaRowBytes=w*2;
            SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
            const auto result=detector.Analyze(source,262+edge,1898-edge,1,1);
            Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
            Assert::IsTrue(result.capturePanelMeasured && result.capturePanel.Valid());
            Assert::IsTrue(result.lineBounds[0].right>=x+32,
                L"all native question-loop pixels inside the proved card must reach capture");
            Assert::IsTrue(result.lineBounds[0].right<result.capturePanel.right);
        }
    }

    TEST_METHOD(NativeCardCompletionCannotBorrowBoundaryWhiskersOrLongStripes) {
        for(bool touchesBoundary:{false,true}) {
            auto f=Frame();PanelToBar(f,180,298,465);Text(f,195,305,18);
            // Both begin close enough to be searched. One is a boundary-
            // connected picture whisker; one is a long thin stripe in the card.
            Fill(f,431,306,touchesBoundary?470:463,308,510);
            SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(result.detected);
            Assert::IsTrue(result.lineBounds[0].right<431,
                L"a cleanup fringe or long stripe must not grant glyph capture");
            Assert::IsTrue(result.capturePanel.right<result.sourcePanel.right);
        }
    }

    TEST_METHOD(CardBoundaryAntialiasingCannotCutAHoleThroughExistingGlyphs) {
        for(bool top:{false,true})for(int antialiasLuma:{110,200}) {
            auto f=Frame();PanelToBar(f,180,298,465);Text(f,195,305,18);
            // Keep the bright core and the first boundary row unchanged. Only
            // native glyph strokes in the next two rows are antialiased; their
            // coverage exceeds the old 8% weak-pixel allowance. Rejecting row316
            // leaves an ownership hole when AR places the true edge at317.
            for(int y=316;y<318;++y)for(int x=180;x<465;++x)
                if((f[y*W+x]>>6)==510)f[y*W+x]=static_cast<uint16_t>(antialiasLuma<<6);
            if(top)for(int y=0;y<H/2;++y)for(int x=0;x<W;++x)
                std::swap(f[y*W+x],f[(H-1-y)*W+x]);
            SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(result.detected);Assert::IsTrue(result.capturePanelMeasured);
            Assert::IsTrue(result.capturePanel.Valid());
            if(top) {
                Assert::AreEqual(42,result.capturePanel.top,
                    L"the three uncertain rows retain matching antialiased glyph strokes");
                Assert::IsTrue(result.lineBounds[0].top<=41);
                Assert::AreEqual(45,result.sourcePanel.top);
            } else {
                Assert::AreEqual(318,result.capturePanel.bottom,
                    L"the three uncertain rows retain matching antialiased glyph strokes");
                Assert::IsTrue(result.lineBounds[0].bottom>=319);
                Assert::AreEqual(315,result.sourcePanel.bottom);
            }
        }
    }

    TEST_METHOD(CardBoundaryKeepsCorroboratedJoinedScriptStrokes) {
        for(bool top:{false,true})for(int joinLuma:{510,200}) {
            auto f=Frame();PanelToBar(f,180,296,465);
            JoinedArabicWord(f,220,303,150,18);
            // This 150-pixel connected baseline crosses all three uncertain
            // rows. Its matching core on adjacent rows distinguishes it from
            // a new picture seam underneath otherwise separated letters.
            Fill(f,220,316,370,317,joinLuma);
            if(top)for(int y=0;y<H/2;++y)for(int x=0;x<W;++x)
                std::swap(f[y*W+x],f[(H-1-y)*W+x]);
            SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(result.detected);Assert::IsTrue(result.capturePanel.Valid());
            Assert::IsTrue(result.lineBounds[0].left<=220 && result.lineBounds[0].right>=370);
            if(top)Assert::AreEqual(42,result.capturePanel.top);
            else Assert::AreEqual(318,result.capturePanel.bottom);
        }
    }

    TEST_METHOD(CardBoundaryAntialiasingStillRejectsPartialSceneStripes) {
        for(bool top:{false,true})for(int stripeLuma:{200,510}) {
            auto f=Frame();PanelToBar(f,180,298,465);Text(f,195,305,18);
            // Most of this row remains black. A dim or bright forty-pixel scene
            // seam must fail the connected-run bound, even beneath real text.
            Fill(f,260,316,300,317,stripeLuma);
            if(top)for(int y=0;y<H/2;++y)for(int x=0;x<W;++x)
                std::swap(f[y*W+x],f[(H-1-y)*W+x]);
            SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(result.detected);Assert::IsTrue(result.capturePanel.Valid());
            if(top)Assert::AreEqual(44,result.capturePanel.top);
            else Assert::AreEqual(316,result.capturePanel.bottom);
        }
    }

    TEST_METHOD(CardBoundaryUncertaintyRequiresCurrentOpaqueContinuation) {
        for(int stripe:{0,1,2}) {
            auto f=Frame();PanelToBar(f,180,298,465);Text(f,195,305,18);
            if(stripe)Fill(f,200,315,450,316,stripe==1?200:510);
            SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
            if(stripe) {
                Assert::IsTrue(!result.detected || !result.capturePanel.Valid() || result.capturePanel.bottom<=315,
                    L"neither dim picture seams nor broad bright stripes authorize uncertain rows");
            } else {
                Assert::IsTrue(result.detected);Assert::IsTrue(result.capturePanelMeasured);
                Assert::AreEqual(318,result.capturePanel.bottom,
                    L"actual black backing with normal glyph strokes proves the three uncertain rows");
                Assert::AreEqual(315,result.sourcePanel.bottom,
                    L"capture proof must not mutate picture/cleanup geometry");
            }
        }
    }

    TEST_METHOD(SmallTerminalMarksInsideMeasuredCardDoNotNeedLetterHeight) {
        auto f=Frame();PanelToBar(f,180,289,470);Text(f,195,297,18);
        Fill(f,435,307,438,310,510);Fill(f,447,307,450,310,510);
        SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
        SubtitleBoxResult result;
        for(int i=0;i<4;++i)result=detector.Analyze(Source(f),45,315,i+1,1);
        Assert::IsTrue(result.detected);
        Assert::IsTrue(result.lineBounds[0].right>=450,
            L"measured opaque card must authorize adjacent punctuation below letter height");
        Assert::IsTrue(result.lineBounds[0].right<470);
    }

    TEST_METHOD(CardCleanupRetainsGuardOutsideMeasuredOpaqueBacking) {
        auto f=Frame();PanelToBar(f,180,298,465);Text(f,195,305,18);
        SubtitleBoxDetector detector;SubtitleBoxResult result;
        for(int i=0;i<4;++i)result=detector.Analyze(Source(f),45,315,i+1,1);
        Assert::IsTrue(result.detected);Assert::IsTrue(result.sourcePanel.Valid());
        Assert::IsTrue(result.sourcePanel.left<=178 && result.sourcePanel.right>=467,
            L"cleanup must include a native-pixel fringe beyond both measured card edges");
        Assert::AreEqual(315,result.sourcePanel.bottom);
    }

    TEST_METHOD(OptimizationModesPreserveExactDetectionAndInkAcrossFramesAndFormats) {
        unsigned found=0;size_t reused=0;
        auto rect=[](const SubtitleBoxRect& a,const SubtitleBoxRect& b){
            Assert::AreEqual(a.left,b.left);Assert::AreEqual(a.top,b.top);
            Assert::AreEqual(a.right,b.right);Assert::AreEqual(a.bottom,b.bottom);
        };
        for(int scale:{1,6})for(bool rgb:{false,true})for(int mode:{1,2}) {
            SubtitleBoxDetector baseline,optimized;optimized.SetOptimizationMode(mode);
            baseline.SetNearBarDistance(20*scale);optimized.SetNearBarDistance(20*scale);
            for(int frame=0;frame<16;++frame) {
                auto f=Frame();const int scenario=frame%8;
                if(scenario==1){PanelToBar(f,180,293,460);Text(f,195,298,18);}
                if(scenario==2){Fill(f,170,281,470,312,64);Text(f,180,289,21);}
                if(scenario==3){Fill(f,170,261,470,312,64);Text(f,180,266,21);Text(f,230,293,13);}
                if(scenario==4){PanelToBar(f,180,292,470);JoinedArabicWord(f,195,299,240,25);}
                if(scenario==5){Fill(f,170,35,470,70,64);Text(f,185,40,20);}
                if(scenario==6){PanelToBar(f,170,294,470);Text(f,200,301,17);UnknownAccessoryGlyph(f,178,302,0);}
                if(scenario==7)for(int y=270;y<H;++y)for(int x=0;x<W;++x)f[y*W+x]=uint16_t(((x*13+y*19+frame*7)%700+64)<<6);
                const int width=W*scale,height=H*scale;
                std::vector<uint16_t> planar(size_t(width)*height*3/2,uint16_t(512<<6));
                std::vector<uint8_t> packed(rgb?size_t(width)*height*4:0);
                for(int y=0;y<height;++y)for(int x=0;x<width;++x){
                    const auto value=f[(y/scale)*W+x/scale];planar[size_t(y)*width+x]=value;
                    if(rgb){const size_t i=(size_t(y)*width+x)*4;packed[i]=packed[i+1]=packed[i+2]=uint8_t(value>>8);packed[i+3]=255;}
                }
                AnalysisLumaSource source;source.data=rgb?packed.data():reinterpret_cast<const uint8_t*>(planar.data());
                source.dataBytes=rgb?packed.size():planar.size()*2;source.width=width;source.height=height;
                source.rowBytes=width*(rgb?4:2);source.chromaRowBytes=width*2;
                source.format=rgb?AnalysisLumaFormat::NativeRgb:AnalysisLumaFormat::P010;
                source.encoding=VideoFrameEncoding::BGRA_8BIT;source.generation=frame<8?1:2;
                const auto a=baseline.Analyze(source,45*scale,315*scale,frame+1,frame<12?1:2);
                const auto b=optimized.Analyze(source,45*scale,315*scale,frame+1,frame<12?1:2);
                found+=a.detected;reused+=optimized.SharedSampleCount();
                Assert::AreEqual(a.detected,b.detected);Assert::AreEqual(a.held,b.held);Assert::AreEqual(a.workLimit,b.workLimit);
                Assert::AreEqual(a.lineCount,b.lineCount);Assert::AreEqual(a.cue,b.cue);Assert::AreEqual(a.observations,b.observations);
                Assert::AreEqual(a.revised,b.revised);rect(a.bounds,b.bounds);rect(a.sourcePanel,b.sourcePanel);rect(a.anchor,b.anchor);
                Assert::IsTrue(a.signature==b.signature && a.anchorSignature==b.anchorSignature && a.lineSignatures==b.lineSignatures);
                for(int i=0;i<3;++i){rect(a.lineBounds[i],b.lineBounds[i]);rect(a.linePanels[i],b.linePanels[i]);
                    Assert::AreEqual(a.panelTopEdges[i].y,b.panelTopEdges[i].y);
                    Assert::AreEqual(a.panelTopEdges[i].left,b.panelTopEdges[i].left);
                    Assert::AreEqual(a.panelTopEdges[i].right,b.panelTopEdges[i].right);
                    Assert::AreEqual(a.panelTopEdges[i].supportBasisPoints,b.panelTopEdges[i].supportBasisPoints);}
                const auto ai=baseline.InkSnapshot(),bi=optimized.InkSnapshot();Assert::AreEqual(bool(ai),bool(bi));
                if(ai){Assert::IsTrue(ai->sourceRows==bi->sourceRows && ai->rawInk==bi->rawInk && ai->ownedInk==bi->ownedInk && ai->blackBacking==bi->blackBacking);}
            }
        }
        Assert::IsTrue(found>20);Assert::IsTrue(reused>0);
    }
    TEST_METHOD(LegacyOptimizationIgnoredAndParserAcceptsOnlyNamedModes) {
        for(const char* value:{"baseline","buffers","shared_samples","fast","3","true"}) {
            char directory[MAX_PATH]{},path[MAX_PATH]{};GetTempPathA(MAX_PATH,directory);GetTempFileNameA(directory,"vpo",0,path);
            {std::ofstream file(path);file<<"[vprenderer]\nsubtitle_detection_optimization: "<<value<<"\n";}
            ConfigFile config;Assert::IsTrue(config.Load(path));DeleteFileA(path);
            RendererProfileConfig::Model model;std::string error;
            int mode=0;const bool valid=ParseSubtitleDetectionOptimization(value,mode);
            Assert::IsTrue(RendererProfileConfig::Read(config,model,error));
            Assert::IsTrue(model.profiles.at("display.base").settings.count("subtitle_detection_optimization")==0);
            Assert::AreEqual(std::string(value)=="baseline" || std::string(value)=="buffers" || std::string(value)=="shared_samples",valid);
        }
    }
    TEST_METHOD(NearBarRequiresOpaqueBackingAndHonorsDistanceAndDisable) {
        for(bool upper:{false,true})for(int gap:{4,18,22,32}) {
            auto f=Frame();
            const int y=upper?45+gap:315-gap-14;
            Fill(f,200,y-4,440,y+18,64);Text(f,210,y,16);
            SubtitleBoxDetector strict;Assert::IsFalse(strict.Analyze(Source(f),45,315,1,1).detected);
            SubtitleBoxDetector nearDetector;nearDetector.SetNearBarDistance(20);
            const auto result=nearDetector.Analyze(Source(f),45,315,1,1);
            Assert::AreEqual(gap<26,result.detected);
            if(result.detected) {Assert::IsTrue(result.sourcePanel.Valid());Assert::AreEqual(1,result.lineCount);}
            nearDetector.SetNearBarDistance(0);Assert::IsFalse(nearDetector.Analyze(Source(f),45,315,1,1).detected);
            auto bare=Frame();Text(bare,210,y,16);
            nearDetector.SetNearBarDistance(20);Assert::IsFalse(nearDetector.Analyze(Source(bare),45,315,2,1).detected);
        }
    }
    TEST_METHOD(NearBarDoesNotTreatAnUnboundedDarkRunAsPanelProximity) {
        for(bool upper:{false,true})for(int glyphTop:{269,284}) {
            auto f=Frame();Fill(f,190,glyphTop-5,450,glyphTop+20,64);Text(f,210,glyphTop,16);
            // A wider scene shadow hides the lower card edge. The visible
            // opposite five-pixel inset, rather than the connected dark run,
            // caps inferred proximity. Mirror both the far and near example.
            Fill(f,150,glyphTop+14,490,315,64);
            if(upper)for(int y=0;y<H/2;++y)for(int x=0;x<W;++x)
                std::swap(f[y*W+x],f[(H-1-y)*W+x]);
            SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
            const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::AreEqual(glyphTop==284,result.detected);
            Assert::IsTrue(result.nearBarEligibilityMeasured);
            Assert::IsTrue(result.diagnosticNearBarGapInferred);
            Assert::AreEqual(glyphTop==284?12:27,result.diagnosticNearBarGap,
                L"the inferred inset comes only from measured opposite whitespace");
        }
    }
    TEST_METHOD(NearBarOppositeInsetCapKeepsNearbyCardsWithAsymmetricMargins) {
        for(bool upper:{false,true})for(int gap:{10,15}) {
            auto f=Frame();const int cardBottom=315-gap,glyphTop=cardBottom-23;
            // Five pixels above the glyphs, nine below: ordinary asymmetric
            // font whitespace remains eligible when the hidden card is nearby.
            Fill(f,190,glyphTop-5,450,cardBottom,64);Text(f,210,glyphTop,16);
            Fill(f,150,glyphTop+14,490,315,64);
            if(upper)for(int y=0;y<H/2;++y)for(int x=0;x<W;++x)
                std::swap(f[y*W+x],f[(H-1-y)*W+x]);
            SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
            const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(result.detected);Assert::IsTrue(result.diagnosticNearBarGapInferred);
            Assert::AreEqual(gap+4,result.diagnosticNearBarGap);
        }
    }
    TEST_METHOD(NearBarCollectsBothLinesWithoutExpandingThePictureBoundary) {
        auto f=Frame();Fill(f,170,261,470,312,64);
        Text(f,180,266,21);Text(f,230,293,13);
        SubtitleBoxDetector d;d.SetNearBarDistance(20);
        const auto result=d.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::IsTrue(result.bounds.top<=266 && result.bounds.bottom>=307);
        Assert::IsTrue(result.sourcePanel.bottom<=315);
    }
    TEST_METHOD(DiagnosticFlagLoadsFromRendererRoot) {
        char directory[MAX_PATH]{},path[MAX_PATH]{};
        Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
        Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
        { std::ofstream file(path);file<<"[vprenderer]\nsubtitle_bbox_test: true\n"; }
        ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
        RendererProfileConfig::Model model; std::string error;
        Assert::IsTrue(RendererProfileConfig::Read(config,model,error));
        Assert::IsTrue(loaded);bool enabled=false;
        Assert::IsTrue(RendererConfigView(config).TryGetDisplayBool("subtitle_bbox_test",enabled));
        Assert::IsTrue(enabled);
    }
    TEST_METHOD(CutPasteFlagLoadsFromRendererRoot) {
        char directory[MAX_PATH]{},path[MAX_PATH]{};
        Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
        Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
        { std::ofstream file(path);file<<"[vprenderer]\nsubtitle_cut_paste_test: true\n"; }
        ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
        RendererProfileConfig::Model model; std::string error;
        Assert::IsTrue(RendererProfileConfig::Read(config,model,error));
        Assert::IsTrue(loaded);bool enabled=false;
        Assert::IsTrue(RendererConfigView(config).TryGetDisplayBool("subtitle_cut_paste_test",enabled));
        Assert::IsTrue(enabled);
    }
    TEST_METHOD(LegacyBackgroundModesArePreservedButIgnored) {
        for(const char* value:{"rectangle","transparent","blend","black","dark_gray","generated_gray"}) {
            char directory[MAX_PATH]{},path[MAX_PATH]{};
            Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
            Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
            { std::ofstream file(path);file<<"[vprenderer]\nsubtitle_cut_paste_background: "<<value<<"\n"; }
            ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
            RendererProfileConfig::Model model;std::string error,mode;
            Assert::IsTrue(loaded && RendererProfileConfig::Read(config,model,error));
            Assert::IsTrue(RendererConfigView(config).TryGetDisplayString("subtitle_cut_paste_background",mode));
            Assert::AreEqual(std::string(value),mode);
        }
        char directory[MAX_PATH]{},path[MAX_PATH]{};
        Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
        Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
        { std::ofstream file(path);file<<"[vprenderer]\nsubtitle_cut_paste_background: neon\n"; }
        ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
        RendererProfileConfig::Model model;std::string error;
        Assert::IsTrue(loaded);
        Assert::IsTrue(RendererProfileConfig::Read(config,model,error));
        Assert::IsTrue(model.profiles.at("display.base").settings.count("subtitle_cut_paste_background")==0);
    }
    TEST_METHOD(GeneratedGrayBlurConfigurationAndNearBlackDefaults) {
        using namespace RendererProfileConfig;
        for(const auto value:{"0","1.5","3","8","8.1","15","29.9","30"}) {
            std::string reason;
            Assert::IsTrue(ValidateBaseSetting("subtitle_generated_gray_blur_px",value));
            Assert::IsTrue(ValidateProfileSetting("display","subtitle_generated_gray_blur_px",value,reason));
            Assert::IsTrue(ValidateCanonicalDisplaySetting("subtitle_generated_gray_blur_px",value));
        }
        for(const auto value:{"-0.1","30.1","31","nan","inf","3px"})
            Assert::IsFalse(ValidateBaseSetting("subtitle_generated_gray_blur_px",value));
        SubtitleGeneratedGrayStyle style;std::array<float,3> expected{};
        Assert::IsTrue(ParseSubtitleRgbHex("040404",expected));
        for(int i=0;i<3;++i)Assert::IsTrue(std::abs(style.color[i]-expected[i])<0.00000001f);
        Assert::AreEqual(0.85f,style.opacity);Assert::AreEqual(3.0f,style.blurPixels);
    }
    TEST_METHOD(GeneratedGrayStyleValidatesColorOpacityAndBorder) {
        const std::string good="[vprenderer.subtitles]\nsubtitle_generated_gray_color: 604A3C\n"
            "subtitle_generated_gray_opacity: 0.35\nsubtitle_generated_gray_blur_px: 30\nsubtitle_generated_gray_max_luminance: 0.45\n"
            "subtitle_generated_gray_border_color: 101010\n"
            "subtitle_generated_gray_border_opacity: 0.8\nsubtitle_generated_gray_border_width: 1.5\n";
        for(const auto& entry:std::vector<std::pair<std::string,bool>>{
            {good,true},
            {"[vprenderer.subtitles]\nsubtitle_generated_gray_color: 60ZZ3C\n",false},
            {"[vprenderer.subtitles]\nsubtitle_generated_gray_opacity: 1.1\n",false},
            {"[vprenderer.subtitles]\nsubtitle_generated_gray_max_luminance: 0\n",false},
            {"[vprenderer.subtitles]\nsubtitle_generated_gray_max_luminance: 1.01\n",false},
            {"[vprenderer.subtitles]\nsubtitle_generated_gray_border_color: 10101\n",false},
            {"[vprenderer.subtitles]\nsubtitle_generated_gray_border_opacity: -0.1\n",false},
            {"[vprenderer.subtitles]\nsubtitle_generated_gray_border_width: 8.1\n",false}}) {
            char directory[MAX_PATH]{},path[MAX_PATH]{};
            Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
            Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
            { std::ofstream file(path);file<<entry.first; }
            ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
            RendererProfileConfig::Model model;std::string error;
            Assert::IsTrue(loaded);
            Assert::AreEqual(entry.second,RendererProfileConfig::Read(config,model,error));
        }
        std::array<float,3> color{};
        Assert::IsTrue(ParseSubtitleRgbHex("FF8000",color));
        Assert::IsTrue(color[0]>0.999f && color[1]>0.20f && color[1]<0.22f && color[2]==0.0f);
    }
    TEST_METHOD(DiagnosticFlagStartupIgnoresRetiredValues) {
        for (const char* value : {"false", "not-a-bool"}) {
            char directory[MAX_PATH]{},path[MAX_PATH]{};
            Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
            Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
            { std::ofstream file(path);file<<"[vprenderer]\nsubtitle_bbox_test: "<<value<<"\n"; }
            ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
            Assert::IsTrue(loaded);
            RendererProfileConfig::Model model;std::string error;
            Assert::IsTrue(RendererProfileConfig::Read(config,model,error));
            Assert::IsTrue(model.profiles.at("display.base").settings.empty());
        }
    }
    TEST_METHOD(CutPasteFlagStartupIgnoresRetiredValues) {
        for (const char* value : {"false", "not-a-bool"}) {
            char directory[MAX_PATH]{},path[MAX_PATH]{};
            Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
            Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
            { std::ofstream file(path);file<<"[vprenderer]\nsubtitle_cut_paste_test: "<<value<<"\n"; }
            ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
            Assert::IsTrue(loaded);
            RendererProfileConfig::Model model;std::string error;
            Assert::IsTrue(RendererProfileConfig::Read(config,model,error));
            Assert::IsTrue(model.profiles.at("display.base").settings.empty());
        }
    }
    TEST_METHOD(NativeRgbAndP210HaveEquivalentBoxes) {
        auto f=Frame();PanelToBar(f,190,300,470);Text(f,200,310,20);SubtitleBoxDetector p010;
        auto expected=p010.Analyze(Source(f),45,315,0,1);Assert::IsTrue(expected.detected);
        std::vector<uint8_t> rgb(W*H*4,255);
        for(int i=0;i<W*H;++i) for(int c=0;c<3;++c) rgb[i*4+c]=static_cast<uint8_t>((f[i]>>6)/4);
        AnalysisLumaSource s;s.data=rgb.data();s.dataBytes=rgb.size();s.width=W;s.height=H;
        s.rowBytes=W*4;s.format=AnalysisLumaFormat::NativeRgb;s.encoding=VideoFrameEncoding::BGRA_8BIT;
        s.colorspace=ColorSpace::REC_709;s.generation=1;
        SubtitleBoxDetector native;auto actual=native.Analyze(s,45,315,0,1);
        Assert::IsTrue(actual.detected);Assert::AreEqual(expected.bounds.top,actual.bounds.top);
        Assert::AreEqual(expected.bounds.right,actual.bounds.right);
        f.resize(W*H*2,512<<6);s=Source(f);s.format=AnalysisLumaFormat::P210;
        SubtitleBoxDetector p210;actual=p210.Analyze(s,45,315,0,1);
        Assert::IsTrue(actual.detected);Assert::AreEqual(expected.bounds.left,actual.bounds.left);
    }
    TEST_METHOD(ChangedUpperLineIsInspectedWhileBottomLineIsUnchanged) {
        auto f=Frame();PanelToBar(f,100,285,520);Text(f,220,320,14);Text(f,210,295,16);SubtitleBoxDetector d;
        auto first=d.Analyze(Source(f),45,315,1,1);Assert::AreEqual(2,first.lineCount);
        f=Frame();PanelToBar(f,100,285,520);Text(f,220,320,14);Text(f,140,295,26);
        auto changed=d.Analyze(Source(f),45,315,2,1);
        Assert::IsTrue(changed.detected);Assert::IsTrue(changed.bounds.left<=140);
        Assert::AreNotEqual(first.cue,changed.cue);
    }
    TEST_METHOD(UhdShortTwoGlyphCueAppearsOnFirstFrame) {
        auto lowResolution=Frame();Text(lowResolution,309,320,2);
        const int scale=6,w=W*scale,h=H*scale;
        std::vector<uint16_t> big(static_cast<size_t>(w)*h*3/2,512<<6);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) big[static_cast<size_t>(y)*w+x]=lowResolution[(y/scale)*W+x/scale];
        auto s=Source(big);s.width=w;s.height=h;s.rowBytes=s.chromaRowBytes=w*2;
        SubtitleBoxDetector d;auto r=d.Analyze(s,45*scale,315*scale,1,1);
        Assert::IsTrue(r.detected);Assert::IsTrue(r.bounds.left<=309*scale);
        Assert::IsTrue(r.bounds.right>=330*scale);Assert::IsTrue(r.bounds.bottom>=334*scale);
    }
    TEST_METHOD(NarrowCenteredHiRasterIsNotRejectedByWordAspectRatio) {
        // Rasterized H and dotted i, from a 31x29 antialiased font glyph mask
        // thresholded at half intensity. Its width/height is close to one.
        const uint32_t rows[]={0x00000000,0x1e000000,0x3f06000c,0x3f0f001e,
            0x3e0e001e,0x1c0e001e,0x000e001e,0x000e001e,0x000e001e,0x000e001e,
            0x1e0e001e,0x1e0e001e,0x1e0f001e,0x1e0ffffe,0x1e0ffffe,0x1e0ffffe,
            0x1e0e001e,0x1e0e001e,0x1e0e001e,0x1e0e001e,0x1e0e001e,0x1e0e001e,
            0x1e0e001e,0x1e0e001e,0x1e0e001e,0x1e0e001e,0x1e0f001e,0x0c06000c,0};
        auto f=Frame();Fill(f,301,308,338,341,64);
        for(int y=0;y<29;++y) for(int x=0;x<31;++x)
            if(rows[y]&(uint32_t{1}<<x)) Fill(f,304+x,310+y,305+x,311+y,510);
        SubtitleBoxDetector detector;auto r=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(1,r.lineCount);
        Assert::IsTrue(r.bounds.left<=305 && r.bounds.top<=311);
        Assert::IsTrue(r.bounds.right>=334 && r.bounds.bottom>=338);
    }
    TEST_METHOD(CenteredJoinedArabicWordCrossingBarIsDetectedAsAFullLine) {
        auto f=Frame();PanelToBar(f,240,296,400);JoinedArabicWord(f,250,306,140,20);
        SubtitleBoxDetector detector;const auto r=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected,L"a wide joined word with actual lower-bar ink is a valid anchor");
        Assert::AreEqual(1,r.lineCount);
        Assert::IsTrue(r.anchor.Valid());
        Assert::IsTrue(r.anchor.top<315 && r.anchor.bottom>315);
        Assert::IsTrue(r.lineBounds[0].left<=250 && r.lineBounds[0].right>=390,
            L"component width limits must not truncate the connected word");
    }
    TEST_METHOD(JoinedArabicWordWiderThanEightGlyphHeightsIsNotClipped) {
        auto f=Frame();PanelToBar(f,190,296,450);JoinedArabicWord(f,200,306,240,20);
        SubtitleBoxDetector detector;const auto r=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected,
            L"a centered connected word crossing the bar should use the joined-script width allowance");
        Assert::AreEqual(1,r.lineCount);
        Assert::IsTrue(r.lineBounds[0].left<=200 && r.lineBounds[0].right>=440,
            L"the component, scale learner, and substantial-line filter must preserve the whole joined word");
    }
    TEST_METHOD(ShortArabicCompanionMayAlignToEitherEdgeOfBarAnchor) {
        for(int side : {-1,1}) {
            auto f=Frame();
            PanelToBar(f,180,270,470);
            JoinedArabicWord(f,200,306,240,20); // centered lower-bar anchor
            JoinedArabicWord(f,side<0?200:360,277,80,20); // same height, right/left aligned above it
            SubtitleBoxDetector detector;const auto r=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(r.detected);
            Assert::AreEqual(2,r.lineCount,
                L"the shorter RTL companion should be related by its shared text edge, not its center");
            bool foundCompanion=false;
            for(int i=0;i<r.lineCount;++i) {
                const auto& line=r.lineBounds[i];
                if(line.top<300 && (side<0 ? line.left<=200 && line.right>=280 :
                    line.left<=360 && line.right>=440)) foundCompanion=true;
            }
            Assert::IsTrue(foundCompanion,
                L"both right-aligned and left-aligned RTL companion extents must be kept");
        }
    }
    TEST_METHOD(LargeConnectedArabicWordSurvivesEveryUhdSamplingPhase) {
        constexpr int scale=6,width=W*scale,height=H*scale,step=4;
        for(int phaseY=0;phaseY<step;++phaseY) for(int phaseX=0;phaseX<step;++phaseX) {
            std::vector<uint16_t> f(width*height*3/2,uint16_t(512<<6));
            auto fill=[&](int left,int top,int right,int bottom,int value) {
                left=(std::max)(0,(std::min)(left,width));
                right=(std::max)(0,(std::min)(right,width));
                top=(std::max)(0,(std::min)(top,height));
                bottom=(std::max)(0,(std::min)(bottom,height));
                for(int y=top;y<bottom;++y)
                    std::fill_n(f.begin()+size_t(y)*width+left,right-left,uint16_t(value<<6));
            };
            fill(0,0,width,height,64);
            fill(0,45*scale,width,315*scale,200);
            const int left=200*scale+phaseX,top=306*scale+phaseY;
            const int glyphWidth=240*scale,glyphHeight=20*scale;
            fill(left-12*scale,top-10*scale,left+glyphWidth+12*scale,315*scale,64);
            fill(left,top+glyphHeight*2/3,left+glyphWidth,top+glyphHeight*2/3+3*scale,510);
            for(int x=left+7*scale;x<left+glyphWidth-5*scale;x+=20*scale) {
                fill(x,top+2*scale,x+3*scale,top+glyphHeight-3*scale,510);
                fill(x+8*scale,top+4*scale,x+11*scale,top+glyphHeight-1*scale,510);
            }
            fill(left+2*scale,top+glyphHeight/2,left+5*scale,top+glyphHeight-2*scale,510);
            fill(left+glyphWidth-5*scale,top+3*scale,left+glyphWidth-2*scale,
                top+glyphHeight/2,510);
            for(int x=left+12*scale;x<left+glyphWidth-5*scale;x+=31*scale)
                fill(x,top,x+2*scale,top+2*scale,510);
            auto source=Source(f);source.width=width;source.height=height;
            source.rowBytes=source.chromaRowBytes=width*2;
            SubtitleBoxDetector detector;
            const auto r=detector.Analyze(source,45*scale,315*scale,1,1);
            const std::wstring context=L"phaseX="+std::to_wstring(phaseX)+
                L" phaseY="+std::to_wstring(phaseY)+L" detected="+
                std::to_wstring(r.detected)+L" lines="+std::to_wstring(r.lineCount);
            Assert::IsTrue(r.detected,context.c_str());
            Assert::AreEqual(1,r.lineCount,context.c_str());
            Assert::IsTrue(r.lineBounds[0].left<=left+step &&
                r.lineBounds[0].right>=left+glyphWidth-step,context.c_str());
        }
    }
    TEST_METHOD(BrightPictureDetailFarAboveBarCannotExhaustSubtitleComponentBudget) {
        constexpr int width=3840,height=2160,pictureTop=276,pictureBottom=1884,step=4;
        std::vector<uint16_t> f(static_cast<size_t>(width)*height*3/2,uint16_t(512<<6));
        auto fill=[&](int left,int top,int right,int bottom,int value) {
            for(int y=top;y<bottom;++y)
                std::fill_n(f.begin()+size_t(y)*width+left,right-left,uint16_t(value<<6));
        };
        fill(0,0,width,height,64);
        fill(0,pictureTop,width,pictureBottom,200);
        // Many small, bright scene details sit farther above the bar than the
        // normal subtitle search corridor. A whole-quarter scan admits them
        // all as components and can hit the bounded-work limit before reaching
        // the real caption at the bar edge.
        for(int y=343;y<388;y+=4) for(int x=8;x<952;x+=4)
            fill(x*step,y*step,(x+2)*step,(y+2)*step,700);
        fill(1340,1830,2480,pictureBottom,64);
        // One centered line; its lower strokes cross the active-picture edge.
        for(int n=0;n<20;++n) {
            const int left=1360+n*56,top=1850;
            for(int y=0;y<48;++y) for(int x=0;x<32;++x) {
                const bool stroke=x<8 || x>=24 || y<8 || y>=40;
                f[static_cast<size_t>(top+y)*width+left+x]=uint16_t((stroke?700:64)<<6);
            }
        }
        auto source=Source(f);source.width=width;source.height=height;
        source.rowBytes=source.chromaRowBytes=width*2;
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(source,pictureTop,pictureBottom,1,1);
        Assert::IsFalse(result.workLimit,
            L"unrelated detail above the normal subtitle corridor must not starve bar-anchored text");
        Assert::IsTrue(result.detected,L"the centered line crossing the bar should remain discoverable");
        Assert::AreEqual(1,result.lineCount);
        Assert::IsTrue(result.bounds.bottom>=pictureBottom);
    }
    TEST_METHOD(WiderPictureSideArabicLineUnionsWithNarrowerBarAnchor) {
        auto f=Frame();
        PanelToBar(f,190,270,450);
        // Two separate connected words make a long RTL line; each individual
        // component remains within the learned per-word height envelope.
        JoinedArabicWord(f,222,277,90,20);
        JoinedArabicWord(f,328,277,90,20);
        JoinedArabicWord(f,265,305,110,20);
        SubtitleBoxDetector detector;const auto r=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(2,r.lineCount);
        Assert::IsTrue(r.anchor.Valid() && r.anchor.top<315 && r.anchor.bottom>315);
        Assert::IsTrue(r.bounds.left<=222 && r.bounds.right>=418,
            L"the bar-crossing line is a minimum span; a wider companion line must expand the union");
    }
    TEST_METHOD(DetachedLeadingDashBeyondHalfGlyphGapStaysInSubtitleBounds) {
        auto f=Frame();
        PanelToBar(f,110,270,430);
        Text(f,160,305,18); // accepted lower-bar anchor
        Text(f,170,280,18); // wider picture-side companion
        Fill(f,137,277,158,291,64);
        Fill(f,142,284,155,287,510); // detached hyphen, 15 px gap to the letters
        SubtitleBoxDetector detector;const auto r=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(2,r.lineCount);
        Assert::IsTrue(r.bounds.left<=138,
            L"a leading dash aligned with the selected line must travel with the complete subtitle crop");
    }
    TEST_METHOD(TwoSamplePixelLeadingDashUsesCaptionSpacing) {
        auto f=Frame();PanelToBar(f,180,270,450);
        Text(f,210,295,16);Text(f,220,320,14);
        Fill(f,194,302,196,303,510);
        SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::IsTrue(result.bounds.left<=194,L"a tiny sampled leading dash must use terminal-dash spacing");
    }
    TEST_METHOD(BracketedClosedCaptionKeepsNonLetterGlyphsAtBothEdges) {
        auto f=Frame();PanelToBar(f,185,296,445);Text(f,215,306,16);
        // Opening and closing brackets are disconnected from the first/last
        // letters by subtitle-sized word gaps, but belong to the visible cue.
        Fill(f,198,306,200,321,510);Fill(f,198,306,205,308,510);Fill(f,198,319,205,321,510);
        Fill(f,430,306,432,321,510);Fill(f,425,306,432,308,510);Fill(f,425,319,432,321,510);
        SubtitleBoxDetector detector;const auto r=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected,
            L"caption eligibility comes from the whole centered row and black-bar evidence, not letters alone");
        Assert::AreEqual(1,r.lineCount);
        Assert::IsTrue(r.lineBounds[0].left<=198 && r.lineBounds[0].right>=432,
            L"opening and closing bracket glyphs must move with their caption");
    }
    TEST_METHOD(ThinBracketedClosedCaptionSurvivesUhdSamplingPhases) {
        const int w=3840,h=2160,pictureTop=276,pictureBottom=1884;
        for(int phaseX=0;phaseX<4;++phaseX) for(int phaseY=0;phaseY<4;++phaseY) {
            std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
            for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                f[static_cast<size_t>(y)*w+x]=
                    (y>=pictureTop && y<pictureBottom?200:64)<<6;
            auto fill=[&](int left,int top,int right,int bottom,int value) {
                for(int y=top;y<bottom;++y) for(int x=left;x<right;++x)
                    f[static_cast<size_t>(y)*w+x]=value<<6;
            };
            const int glyphTop=1828+phaseY;
            fill(1250,1810,2580,pictureBottom,64);
            for(int n=0;n<30;++n) {
                const int left=1320+phaseX+n*40;
                // A hollow, connected capital-like glyph proxy.
                fill(left,glyphTop,left+28,glyphTop+64,700);
                fill(left+8,glyphTop+8,left+20,glyphTop+56,64);
            }
            const int openLeft=1270+phaseX,closeLeft=2534+phaseX;
            // Three-source-pixel strokes model thin punctuation that can fall
            // between samples at UHD. Every x/y phase must still capture it.
            fill(openLeft,glyphTop,openLeft+3,glyphTop+64,700);
            fill(openLeft,glyphTop,openLeft+24,glyphTop+3,700);
            fill(openLeft,glyphTop+61,openLeft+24,glyphTop+64,700);
            fill(closeLeft+21,glyphTop,closeLeft+24,glyphTop+64,700);
            fill(closeLeft,glyphTop,closeLeft+24,glyphTop+3,700);
            fill(closeLeft,glyphTop+61,closeLeft+24,glyphTop+64,700);
            auto source=Source(f);source.width=w;source.height=h;
            source.rowBytes=source.chromaRowBytes=w*2;
            SubtitleBoxDetector detector;
            const auto r=detector.Analyze(source,pictureTop,pictureBottom,1,1);
            const std::wstring context=L"phaseX="+std::to_wstring(phaseX)+
                L" phaseY="+std::to_wstring(phaseY)+L" detected="+
                std::to_wstring(r.detected)+L" lines="+std::to_wstring(r.lineCount)+
                L" left="+std::to_wstring(r.lineCount?r.lineBounds[0].left:-1)+
                L" right="+std::to_wstring(r.lineCount?r.lineBounds[0].right:-1);
            Assert::IsTrue(r.detected,context.c_str());
            Assert::AreEqual(1,r.lineCount,context.c_str());
            Assert::IsTrue(r.lineBounds[0].left<=openLeft+4 &&
                r.lineBounds[0].right>=closeLeft+20,context.c_str());
        }
    }
    TEST_METHOD(ThinWideDashSurvivesSamplingBeforeLineAttachment) {
        for(int scale : {1,3,6}) for(int thickness : {1,2}) {
            const int width=W*scale,height=H*scale;
            const int step=SubtitleBoxDetector::SamplingStep(width,height);
            for(int phase=0;phase<step;++phase) {
                auto base=Frame();PanelToBar(base,120,270,430);Text(base,160,305,18);Text(base,170,280,18);
                Fill(base,128,280,161,292,64);
                std::vector<uint16_t> f(width*height*3/2,uint16_t(512<<6));
                for(int y=0;y<height;++y)for(int x=0;x<width;++x)
                    f[y*width+x]=base[(y/scale)*W+x/scale];
                for(int y=286*scale+phase;y<(286+thickness)*scale+phase;++y)
                    for(int x=132*scale;x<154*scale;++x) f[y*width+x]=uint16_t(510<<6);
                auto source=Source(f);source.width=width;source.height=height;
                source.rowBytes=source.chromaRowBytes=width*2;
                SubtitleBoxDetector detector;
                const auto r=detector.Analyze(source,45*scale,315*scale,1,1);
                Assert::IsTrue(r.detected);
                Assert::IsTrue(r.bounds.left<=132*scale,
                    L"a thin leading dash must survive every sampling phase and join the caption");
            }
        }
    }
    TEST_METHOD(LargeTwoLineCaptionIncludesDashAcrossRasterScales) {
        for(int scale : {1,2,6}) {
            const int width=W*scale,height=H*scale;
            std::vector<uint16_t> f(width*height*3/2,uint16_t(512<<6));
            auto fill=[&](int l,int t,int r,int b,int v) {
                for(int y=t*scale;y<b*scale;++y)for(int x=l*scale;x<r*scale;++x)
                    f[y*width+x]=uint16_t(v<<6);
            };
            fill(0,0,W,H,64);fill(0,45,W,315,200);
            fill(4,226,636,335,64);
            auto text=[&](int left,int top,int count) {
                for(int n=0;n<count;++n) {
                    int x=left+n*26;
                    fill(x,top,x+18,top+36,510);fill(x+4,top+4,x+14,top+32,64);
                }
            };
            text(10,230,24);text(204,292,9);
            fill(174,308,194,310,510); // thin large-font dialogue dash
            auto source=Source(f);source.width=width;source.height=height;
            source.rowBytes=source.chromaRowBytes=width*2;
            SubtitleBoxDetector detector;
            const auto r=detector.Analyze(source,45*scale,315*scale,1,1);
            Assert::IsTrue(r.detected,L"large-font caption crossing the bar must be admitted");
            Assert::AreEqual(2,r.lineCount);
            Assert::IsTrue(r.bounds.left<=10*scale && r.bounds.top<=230*scale &&
                r.bounds.right>=626*scale && r.bounds.bottom>=328*scale,
                L"capture the wider picture-side line and all of the large bar line");
            bool lowerIncludesDash=false;
            for(int i=0;i<r.lineCount;++i)
                if(r.lineBounds[i].top>=280*scale && r.lineBounds[i].left<=174*scale)
                    lowerIncludesDash=true;
            Assert::IsTrue(lowerIncludesDash,L"the wider upper row cannot hide an omitted lower-row dash");
            const auto repeat=detector.Analyze(source,45*scale,315*scale,2,1);
            Assert::AreEqual(r.cue,repeat.cue);
        }
    }

    TEST_METHOD(ShortLowerLineDashCannotStretchUpperLineAndExcludeLowerText) {
        auto f=Frame();PanelToBar(f,100,270,520);Text(f,130,280,30);Text(f,270,307,10);
        Fill(f,247,312,262,323,64);Fill(f,251,317,257,319,510);
        SubtitleBoxDetector detector;
        for(uint64_t frame=1;frame<=8;++frame) {
            const auto r=detector.Analyze(Source(f),45,315,frame,1);
            Assert::IsTrue(r.detected);
            Assert::AreEqual(2,r.lineCount,
                L"a lower-row dash must not turn a two-line cue into one tall truncated line");
            Assert::IsTrue(r.bounds.bottom>=321);
            bool completeLowerLine=false;
            for(int line=0;line<r.lineCount;++line)
                if(r.lineBounds[line].top>=300 && r.lineBounds[line].left<=251 &&
                    r.lineBounds[line].right>=395 && r.lineBounds[line].bottom>=321)
                    completeLowerLine=true;
            Assert::IsTrue(completeLowerLine,L"lower-row dash and full glyph height must belong to the same line");
        }
    }

    TEST_METHOD(LowerRowBarMarkCannotTurnLargeCaptionIntoOneTallLine) {
        const int width=960,height=540;
        std::vector<uint16_t> frame(width*height*3/2,uint16_t(512<<6));
        auto fill=[&](int left,int top,int right,int bottom,int value) {
            for(int y=top;y<bottom;++y) for(int x=left;x<right;++x)
                frame[y*width+x]=uint16_t(value<<6);
        };
        fill(0,0,width,height,200);
        fill(0,0,width,69,64);fill(0,471,width,height,64);
        fill(20,400,width-20,471,64); // black caption card connected to the lower bar
        auto text=[&](int left,int top,int count) {
            for(int n=0;n<count;++n) {
                const int x=left+n*37;
                fill(x,top,x+24,top+32,510);
                fill(x+4,top+4,x+20,top+28,64);
            }
        };
        text(36,421,24);text(260,460,12);
        // A detached lower-row glyph fragment is in the bar and inside that
        // row's horizontal span. It must not make the picture-side row the bar
        // anchor and then exclude the complete lower row as an overlap.
        fill(300,493,320,499,510);
        auto source=Source(frame);source.width=width;source.height=height;
        source.rowBytes=source.chromaRowBytes=width*2;
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(source,69,471,1,1);
        Assert::IsTrue(result.detected);
        Assert::AreEqual(2,result.lineCount,
            L"a detached lower-row bar mark cannot collapse a large two-line caption");
        bool completeLower=false;
        for(int i=0;i<result.lineCount;++i)
            if(result.lineBounds[i].top>=460 && result.lineBounds[i].right>=691 &&
                result.lineBounds[i].bottom>=492)
                completeLower=true;
        Assert::IsTrue(completeLower);
    }

    TEST_METHOD(UnknownBarGlyphJoinsCaptionWithoutCharacterRecognition) {
        for(int shape : {0,1}) {
            auto f=Frame();PanelToBar(f,185,273,440);
            Text(f,220,285,16); // centered cue text, wholly above the bar edge
            UnknownAccessoryGlyph(f,201,306,shape); // detached mark touches lower bar
            SubtitleBoxDetector detector;
            const auto first=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(first.detected,
                L"a nearby unknown glyph entering the bar should join an established centered caption");
            Assert::IsTrue(first.anchor.Valid());
            Assert::IsTrue(first.bounds.left<=201 && first.bounds.bottom>=318,
                L"the moved bounds must include the whole detached symbol, not leave it behind");
            const auto repeated=detector.Analyze(Source(f),45,315,2,1);
            Assert::IsTrue(repeated.detected);
            Assert::AreEqual(first.cue,repeated.cue,
                L"the associated symbol must remain part of the same subtitle cue across frames");
            Assert::IsTrue(repeated.bounds.left<=201 && repeated.bounds.bottom>=318);
        }
    }
    TEST_METHOD(UnknownGlyphBesideCaptionFullyInBlackBarIsIncluded) {
        auto f=Frame();
        Text(f,220,320,16); // established caption wholly inside the lower bar
        UnknownAccessoryGlyph(f,201,347,0); // lower-row musical mark, also in bar
        SubtitleBoxDetector detector;
        const auto first=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(first.detected);
        const std::wstring boundsEvidence=L"bounds left="+std::to_wstring(first.bounds.left)+
            L" bottom="+std::to_wstring(first.bounds.bottom);
        Assert::IsTrue(first.bounds.left<=201 && first.bounds.bottom>=359,
            boundsEvidence.c_str());
        const auto repeated=detector.Analyze(Source(f),45,315,2,1);
        Assert::IsTrue(repeated.detected);
        Assert::AreEqual(first.cue,repeated.cue);
    }
    TEST_METHOD(OffCenterUnknownGlyphAndDistantGlyphCannotAuthorizeSubtitleMove) {
        auto glyphOnly=Frame();UnknownAccessoryGlyph(glyphOnly,84,306,0);
        SubtitleBoxDetector glyphOnlyDetector;
        Assert::IsFalse(glyphOnlyDetector.Analyze(Source(glyphOnly),45,315,1,1).detected,
            L"a detached symbol alone must not authorize moving picture pixels");

        auto centeredGlyphOnly=Frame();PanelToBar(centeredGlyphOnly,295,295,345);
        UnknownAccessoryGlyph(centeredGlyphOnly,(W-14)/2,306,0);
        SubtitleBoxDetector centeredGlyphOnlyDetector;
        const auto centeredCue=centeredGlyphOnlyDetector.Analyze(Source(centeredGlyphOnly),45,315,1,1);
        Assert::IsTrue(centeredCue.detected,
            L"an unknown symbol can be a subtitle cue when it is centered and has black-bar evidence");
        Assert::IsTrue(centeredCue.bounds.left<=(W-14)/2 && centeredCue.bounds.bottom>=318);
        const auto centeredRepeat=centeredGlyphOnlyDetector.Analyze(Source(centeredGlyphOnly),45,315,2,1);
        Assert::IsTrue(centeredRepeat.detected);
        Assert::AreEqual(centeredCue.cue,centeredRepeat.cue,
            L"a repeated symbol-only subtitle keeps the same cue identity");

        auto distant=Frame();Text(distant,220,285,16);
        UnknownAccessoryGlyph(distant,80,306,0);
        SubtitleBoxDetector distantDetector;
        Assert::IsFalse(distantDetector.Analyze(Source(distant),45,315,1,1).detected,
            L"a distant bar symbol must not be chained to unrelated picture-side text");
    }
    TEST_METHOD(PictureOnlyJoinedTextCannotUseOffCenterBarTitleAsAnchor) {
        auto f=Frame();
        JoinedArabicWord(f,250,277,140,20); // centered, but wholly in the picture
        JoinedArabicWord(f,18,306,120,20);  // genuine bar crossing, but corner title
        SubtitleBoxDetector detector;const auto r=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsFalse(r.detected,
            L"picture-side text and an unrelated off-center bar title cannot create a subtitle cue");
        Assert::IsFalse(r.bounds.Valid());
    }
    TEST_METHOD(LongChangedPictureLineRetainsDistinctDensityWithSameBarCompanion) {
        const int w=960,h=540;
        for(int count : {22,40,65}) {
            auto frame=[&](bool changed){
                std::vector<uint16_t> pixels(static_cast<size_t>(w)*h*3/2,512<<6);
                for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                    pixels[y*w+x]=(y>=68 && y<472?200:64)<<6;
                for(int y=425;y<472;++y) for(int x=8;x<w-8;++x)
                    pixels[y*w+x]=64<<6;
                auto word=[&](int y,int letters,bool hShape){
                    const int left=(w-(letters-1)*13-8)/2;
                    for(int n=0;n<letters;++n) for(int yy=-1;yy<=14;++yy) for(int xx=-1;xx<=8;++xx) {
                        const bool stroke=xx>=0 && xx<8 && yy>=0 && yy<14 &&
                            (xx<2 || xx>=6 || (hShape?(yy>=6 && yy<8):(yy<2 || yy>=12)));
                        pixels[(y+yy)*w+left+n*13+xx]=(stroke?510:64)<<6;
                    }
                };
                word(480,20,false);word(442,count,changed);return pixels;
            };
            auto oldPixels=frame(false),newPixels=frame(true);
            auto source=[&](const std::vector<uint16_t>& pixels){
                auto s=Source(pixels);s.width=w;s.height=h;s.rowBytes=s.chromaRowBytes=w*2;return s;
            };
            SubtitleBoxDetector beforeDetector,afterDetector;
            const auto before=beforeDetector.Analyze(source(oldPixels),68,472,1,1);
            const auto after=afterDetector.Analyze(source(newPixels),68,472,2,1);
            Assert::IsTrue(before.detected && after.detected);
            Assert::AreEqual(2,before.lineCount);Assert::AreEqual(2,after.lineCount);
            Assert::AreEqual(before.bounds.left,after.bounds.left);
            Assert::AreEqual(before.bounds.right,after.bounds.right);
            Assert::IsTrue(before.lineSignatures[0]==after.lineSignatures[0]);
            int difference=0,total=0;
            for(size_t i=0;i<before.lineSignatures[1].size();++i) {
                const int a=before.lineSignatures[1][i],b=after.lineSignatures[1][i];
                difference+=std::abs(a-b);total+=std::max(a,b);
            }
            Assert::IsTrue(difference*100>total*25);
        }
    }
    TEST_METHOD(NarrowPictureShapeCannotJoinValidBarSubtitle) {
        auto f=Frame();Text(f,200,320,20);
        for(int x : {310,323}) {
            Fill(f,x-1,284,x+9,310,64);
            Fill(f,x,285,x+8,309,510);Fill(f,x+2,287,x+6,307,64);
        }
        SubtitleBoxDetector detector;const auto r=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(1,r.lineCount);
        Assert::IsTrue(r.bounds.top>=315 && r.bounds.bottom>=334);
    }
    TEST_METHOD(IsolatedCornerTitlesDoNotQualifyInEitherBar) {
        for(int left : {20,520}) for(int top : {20,325}) {
            auto f=Frame();Text(f,left,top,8);SubtitleBoxDetector d;
            auto r=d.Analyze(Source(f),45,315,1,1);
            Assert::IsFalse(r.detected);Assert::IsFalse(r.bounds.Valid());
        }
    }
    TEST_METHOD(DiagnosticFlagNamedDisplayProfileIgnoresRetiredValues) {
        for(const char* value : {"true","false","not-a-bool"}) {
            char directory[MAX_PATH]{},path[MAX_PATH]{};
            Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
            Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
            { std::ofstream file(path);file<<"[vprenderer.profile_1]\nsubtitle_bbox_test: "<<value<<"\n"; }
            ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
            Assert::IsTrue(loaded);
            RendererProfileConfig::Model model;std::string error;
            const bool valid=true;
            Assert::AreEqual(valid,RendererProfileConfig::Read(config,model,error));
            if(valid) {
                const auto profile=model.profiles.find("display.profile_1");
                Assert::IsTrue(profile!=model.profiles.end());
                const auto setting=profile->second.settings.find("subtitle_bbox_test");
                Assert::IsTrue(setting==profile->second.settings.end());
            }
        }
    }
    TEST_METHOD(CutPasteFlagNamedDisplayProfileIgnoresRetiredValues) {
        for(const char* value : {"true","false","not-a-bool"}) {
            char directory[MAX_PATH]{},path[MAX_PATH]{};
            Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
            Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
            { std::ofstream file(path);file<<"[vprenderer.profile_1]\nsubtitle_cut_paste_test: "<<value<<"\n"; }
            ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
            Assert::IsTrue(loaded);
            RendererProfileConfig::Model model;std::string error;
            const bool valid=true;
            Assert::AreEqual(valid,RendererProfileConfig::Read(config,model,error));
            if(valid) {
                const auto profile=model.profiles.find("display.profile_1");
                Assert::IsTrue(profile!=model.profiles.end());
                const auto setting=profile->second.settings.find("subtitle_cut_paste_test");
                Assert::IsTrue(setting==profile->second.settings.end());
            }
        }
    }
    TEST_METHOD(CornerTitleCannotSuppressShortCenteredSubtitle) {
        for(int left : {20,455}) {
            auto f=Frame();Text(f,left,20,12);Text(f,309,325,2);
            SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(r.detected);Assert::AreEqual(1,r.lineCount);
            Assert::IsTrue(r.bounds.left<=309 && r.bounds.right>=330);
            Assert::IsTrue(r.bounds.top>=315 && r.bounds.bottom>=339);
        }
    }
    TEST_METHOD(UpperBoundaryAnchorIncludesLowerBarCompanionOnFirstFrame) {
        auto f=Frame();PanelToBar(f,140,295,495);Text(f,150,307,26);Text(f,240,333,12);
        SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(2,r.lineCount);
        Assert::IsTrue(r.bounds.left<=150 && r.bounds.right>=483);
        Assert::IsTrue(r.bounds.top<=307 && r.bounds.bottom>=347);
        Assert::AreEqual(uint32_t{1},r.observations);
    }
    TEST_METHOD(TwoBarLinesAreCompleteRegardlessOfStrongerAnchor) {
        for(bool upperIsWider : {false,true}) {
            auto f=Frame();
            Text(f,upperIsWider?150:240,317,upperIsWider?26:12);
            Text(f,upperIsWider?240:150,341,upperIsWider?12:26);
            SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(r.detected);Assert::AreEqual(2,r.lineCount);
            Assert::IsTrue(r.bounds.left<=150 && r.bounds.right>=483);
            Assert::IsTrue(r.bounds.top<=317 && r.bounds.bottom>=355);
        }
    }
    TEST_METHOD(TopBoundaryAnchorIncludesUpperBarCompanionOnFirstFrame) {
        auto f=Frame();Fill(f,140,45,495,65,64);Text(f,240,12,12);Text(f,150,38,26);
        SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(2,r.lineCount);
        Assert::IsTrue(r.bounds.left<=150 && r.bounds.right>=483);
        Assert::IsTrue(r.bounds.top<=12 && r.bounds.bottom>=52);
    }
    TEST_METHOD(CompanionCannotChainAnotherPictureLineIntoTheBox) {
        auto f=Frame();PanelToBar(f,180,275,500);Text(f,200,320,20);Text(f,240,283,12);Text(f,240,246,12);
        SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(2,r.lineCount);
        Assert::IsTrue(r.bounds.top<=283 && r.bounds.top>260);
        Assert::IsTrue(r.bounds.bottom>=334);
    }
    TEST_METHOD(CrossingOutlinedTextWithoutOpaqueCardIsRejected) {
        auto f=Frame();Text(f,200,310,20);
        SubtitleBoxDetector detector;
        Assert::IsFalse(detector.Analyze(Source(f),45,315,1,1).detected,
            L"bar entry alone cannot authorize picture text without its required opaque card");
    }
    TEST_METHOD(AdditionalCrossingCompanionCannotBypassCardProof) {
        auto f=Frame();Text(f,200,333,20);Text(f,240,307,12);
        SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
        Assert::IsTrue(result.bounds.top>=315);
        Assert::IsFalse(result.sourcePanel.Valid());
    }
    TEST_METHOD(UnbackedAlignedPictureTextCannotExpandBarCue) {
        auto f=Frame();Text(f,212,320,14);Text(f,160,282,22);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
        Assert::IsTrue(result.bounds.top>=315);
        Assert::IsFalse(result.sourcePanel.Valid());
    }
    TEST_METHOD(WiderUpperLineRequiresAndMeasuresWholeBlackCard) {
        auto f=Frame();PanelToBar(f,140,273,480);
        Text(f,212,320,14);Text(f,160,282,22);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::IsTrue(result.sourcePanel.Valid());
        Assert::IsTrue(result.sourcePanel.left<=140 && result.sourcePanel.right>=480);
        Assert::IsTrue(result.sourcePanel.top<=273 && result.sourcePanel.top>=270);
        Assert::AreEqual(315,result.sourcePanel.bottom);
    }
    TEST_METHOD(DarkSceneJoinedToCardDoesNotTurnSearchLimitIntoPanelEdge) {
        auto f=Frame();PanelToBar(f,140,273,480);
        // A robe covers the right card edge for most rows, with visible edge
        // samples remaining above it. Those are the actual measurements.
        Fill(f,480,282,640,315,64);
        Text(f,212,320,14);Text(f,160,282,22);
        SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::IsTrue(result.sourcePanel.right>=480 && result.sourcePanel.right<=484,
            L"an edge search hitting its cap must not authorize scenery replacement");
    }
    TEST_METHOD(CardMarginMeasurementIgnoresGlyphCutsAtEnvelopeEdges) {
        auto f=Frame();PanelToBar(f,190,299,465);Text(f,205,309,19);
        SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);
        Assert::IsTrue(result.sourcePanel.left<=190 && result.sourcePanel.right>=465,
            L"rows touching terminal glyphs must not be mistaken for narrower source-card edges");
    }
    TEST_METHOD(CrossingGlyphKeepsOpaqueTopStripWhenNeighborsOccludeSideStrips) {
        auto f=Frame();PanelToBar(f,275,299,370);Text(f,284,313,6);
        SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);
        Assert::IsTrue(result.bounds.left<=284 && result.bounds.right>=357);
    }
    TEST_METHOD(PictureGlyphOutsideCardCannotBorrowInsideLineWhitespace) {
        auto f=Frame();PanelToBar(f,210,281,440);
        Text(f,220,295,16);Text(f,220,320,16);
        // Letter-sized bright scene stroke outside the card, aligned to text.
        Fill(f,195,295,202,309,510);Fill(f,197,297,200,307,200);
        SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::IsTrue(result.bounds.left>=210,
            L"a large unsupported edge glyph cannot borrow the rest of the line's black card");
    }
    TEST_METHOD(UhdMovingSceneStrokesCannotBorrowAdjacentCaptionCard) {
        constexpr int scale=6,width=W*scale,height=H*scale;
        for(int phaseX=0;phaseX<4;++phaseX) for(int phaseY=0;phaseY<4;++phaseY) {
            auto clean=Frame();PanelToBar(clean,278,295,370);Text(clean,288,306,6);
            Fill(clean,282,306,284,323,510);Fill(clean,282,306,287,308,510);
            Fill(clean,282,321,287,323,510);
            Fill(clean,365,306,367,323,510);Fill(clean,361,306,367,308,510);
            Fill(clean,361,321,367,323,510);
            std::vector<uint16_t> pixels(width*height*3/2,uint16_t(512<<6));
            for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
                const int sx=(std::max)(0,(x-phaseX)/scale);
                const int sy=(std::max)(0,(y-phaseY)/scale);
                pixels[size_t(y)*width+x]=clean[sy*W+sx];
            }
            auto source=Source(pixels);source.width=width;source.height=height;
            source.rowBytes=source.chromaRowBytes=width*2;
            SubtitleBoxDetector detector;
            const auto baseline=detector.Analyze(source,45*scale+phaseY,315*scale+phaseY,1,1);
            Assert::IsTrue(baseline.detected);
            const auto baselineInk=detector.InkSnapshot();
            for(int offset=0;offset<4;++offset) {
                auto noisy=pixels;
                // Two silver droid strokes sit beside, not on, the real card.
                for(int n=0;n<2;++n) for(int y=302*scale+phaseY;y<313*scale+phaseY;++y)
                    for(int x=(267+n*5)*scale+offset;x<(269+n*5)*scale+offset;++x)
                        noisy[size_t(y)*width+x]=510<<6;
                source.data=reinterpret_cast<const uint8_t*>(noisy.data());
                const auto result=detector.Analyze(source,45*scale+phaseY,315*scale+phaseY,2+offset,1);
                Assert::IsTrue(result.detected);
                Assert::AreEqual(baseline.bounds.left,result.bounds.left);
                Assert::AreEqual(baseline.bounds.right,result.bounds.right);
                Assert::IsTrue(baseline.signature==result.signature);
                Assert::IsTrue(baselineInk->ownedInk==detector.InkSnapshot()->ownedInk,
                    L"adjacent moving scenery must never enter tracked subtitle ink");
            }
        }
    }
    TEST_METHOD(OffCenterUnbackedPictureTextCannotExpandBarAnchoredCue) {
        SubtitleBoxDetector detector;
        SubtitleBoxResult first;
        for(uint64_t sequence=1;sequence<=4;++sequence) {
            auto f=Frame();
            Text(f,220,320,14); // the genuine bar-crossing subtitle row
            Text(f,28+int(sequence%2),283,7); // unrelated corner title, no card
            const auto current=detector.Analyze(Source(f),45,315,sequence,1);
            Assert::IsTrue(current.detected);
            Assert::AreEqual(1,current.lineCount,
                L"an off-center picture title cannot join a centered bar-anchored cue");
            Assert::IsTrue(current.bounds.top>=315,
                L"unrelated picture text must not move the box upward");
            if(sequence==1) first=current;
            else {
                Assert::AreEqual(first.cue,current.cue);
                Assert::AreEqual(first.bounds.top,current.bounds.top);
            }
        }
    }
    TEST_METHOD(PictureCompanionInsideConnectedBlackCardIsIncludedOnFirstFrame) {
        auto f=Frame();
        Fill(f,160,275,480,315,64); // card touches the letterbox and covers both rows
        Text(f,220,320,14);
        Text(f,230,283,14);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);
        Assert::AreEqual(2,result.lineCount,
            L"a picture-side line remains eligible when it shares the bar-connected black card");
        Assert::IsTrue(result.bounds.top<=283 && result.bounds.bottom>=334);
    }
    TEST_METHOD(TwoLineCueStaysWholeWhenDarkSceneConnectsToFrameEdges) {
        auto f=Frame();
        Fill(f,0,302,W,315,64); // dark scene detail connects the panel to both frame edges
        Fill(f,120,270,520,315,64);
        Text(f,160,320,24);Text(f,190,282,20);
        SubtitleBoxDetector detector;
        SubtitleBoxResult first;
        for(uint64_t sequence=1;sequence<=8;++sequence) {
            const auto result=detector.Analyze(Source(f),45,315,sequence,1);
            Assert::IsTrue(result.detected);
            Assert::AreEqual(2,result.lineCount,
                L"a two-line cue must include its picture line when the dark scene connects to frame edges");
            Assert::IsTrue(result.bounds.top<=282 && result.bounds.bottom>=334);
            if(sequence==1) first=result;
            else {
                Assert::AreEqual(first.cue,result.cue);
                Assert::AreEqual(first.bounds.left,result.bounds.left);
                Assert::AreEqual(first.bounds.top,result.bounds.top);
                Assert::AreEqual(first.bounds.right,result.bounds.right);
                Assert::AreEqual(first.bounds.bottom,result.bounds.bottom);
            }
        }
    }
    TEST_METHOD(RoundedPanelCanBeginTwoSampleRowsInsideThePictureBoundary) {
        auto f=Frame();
        Fill(f,150,275,490,313,64);
        Fill(f,260,313,380,314,64);
        Fill(f,280,314,360,315,64);
        Text(f,220,320,14);Text(f,230,283,14);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);
        Assert::AreEqual(2,result.lineCount,
            L"rounded card corners may narrow the first one or two sampled rows at the seam");
        Assert::IsTrue(result.bounds.top<=283 && result.bounds.bottom>=334);
    }
    TEST_METHOD(BarConnectedPanelExtendsSearchFarEnoughForLargeCompanionLine) {
        auto f=Frame();
        Fill(f,150,220,490,315,64);
        auto largeText=[&](int left,int top,int count) {
            for(int n=0;n<count;++n) {
                const int x=left+n*22;
                Fill(f,x-1,top-1,x+17,top+31,64);
                Fill(f,x,top,x+16,top+30,510);
                Fill(f,x+4,top+4,x+12,top+26,64);
            }
        };
        largeText(191,320,12);largeText(191,230,12);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);
        const auto diagnostic=L"lines="+std::to_wstring(result.lineCount)+L" box="+
            std::to_wstring(result.bounds.left)+L","+std::to_wstring(result.bounds.top)+L"-"+
            std::to_wstring(result.bounds.right)+L","+std::to_wstring(result.bounds.bottom);
        Assert::AreEqual(2,result.lineCount,diagnostic.c_str());
        Assert::IsTrue(result.bounds.top<=230 && result.bounds.bottom>=351);
    }
    TEST_METHOD(PictureCompanionWithSmallRoundedCardEdgeSpillIsIncluded) {
        auto f=Frame();
        Fill(f,145,275,465,315,64);
        Fill(f,145,275,149,279,200);Fill(f,461,275,465,279,200); // rounded corners
        Text(f,220,320,14); // bar-crossing anchor
        Text(f,150,283,24); // wider companion remains on the complete rounded card
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);
        Assert::AreEqual(2,result.lineCount,
            L"rounded/partly occluded card edges must not discard a real picture-side line");
        Assert::IsTrue(result.bounds.left<=149 && result.bounds.right>=458);
    }
    TEST_METHOD(FarSideLabelCannotJoinWideCenteredSubtitle) {
        auto f=Frame();Text(f,100,320,34);Text(f,70,342,8);
        SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(1,r.lineCount);
        Assert::IsTrue(r.bounds.left<=100 && r.bounds.left>70);
        Assert::IsTrue(r.bounds.bottom>=334 && r.bounds.bottom<342);
    }
    TEST_METHOD(QueuedObservationKeepsBarAnchorEvidenceSeparateFromCompanion) {
        auto f=Frame();Text(f,240,320,12);SubtitleBoxDetector firstDetector;
        const auto first=firstDetector.Analyze(Source(f),45,315,1,1);
        PanelToBar(f,120,275,500);Text(f,150,295,25);SubtitleBoxDetector nextDetector;
        const auto next=nextDetector.Analyze(Source(f),45,315,2,1);
        Assert::IsTrue(first.detected && next.detected);
        Assert::AreEqual(first.anchor.left,next.anchor.left);
        Assert::AreEqual(first.anchor.top,next.anchor.top);
        Assert::AreEqual(first.anchor.right,next.anchor.right);
        Assert::AreEqual(first.anchor.bottom,next.anchor.bottom);
        Assert::IsTrue(first.anchorSignature==next.anchorSignature);
        Assert::IsTrue(first.signature!=next.signature);
        Assert::IsTrue(next.bounds.top<next.anchor.top);
    }
    TEST_METHOD(QueuedLineEvidenceSurvivesStrongerSecondLineChangingTheAnchor) {
        auto f=Frame();PanelToBar(f,230,297,410);Text(f,240,307,12);SubtitleBoxDetector firstDetector;
        const auto first=firstDetector.Analyze(Source(f),45,315,1,1);
        Text(f,150,333,26);SubtitleBoxDetector nextDetector;
        const auto next=nextDetector.Analyze(Source(f),45,315,2,1);
        Assert::IsTrue(first.detected && next.detected);Assert::AreEqual(2,next.lineCount);
        Assert::AreNotEqual(first.anchor.top,next.anchor.top);
        Assert::AreEqual(first.lineBounds[0].top,next.lineBounds[1].top);
        Assert::AreEqual(first.lineBounds[0].left,next.lineBounds[1].left);
        Assert::IsTrue(first.lineSignatures[0]==next.lineSignatures[1]);
    }
    TEST_METHOD(FirstFrameIncludesBarLineAndWiderPictureCompanion) {
        auto f=Frame();PanelToBar(f,120,275,500);Text(f,240,320,12);Text(f,150,295,25);
        SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(2,r.lineCount);
        Assert::IsTrue(r.bounds.left<=150 && r.bounds.right>=470 && r.bounds.top<=295 && r.bounds.bottom>=334);
        Assert::AreEqual(uint32_t{1},r.observations);
    }
    TEST_METHOD(StableCueHasNoBoxDriftThroughNoiseAndDuration) {
        auto f=Frame();PanelToBar(f,190,300,470);Text(f,200,310,20);SubtitleBoxDetector d;
        auto first=d.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(first.detected);
        for(int frame=2;frame<120;++frame) {
            auto n=f;for(int y=45;y<290;++y) for(int x=0;x<W;++x) n[y*W+x]=static_cast<uint16_t>((150+(x+frame)%60)<<6);
            auto r=d.Analyze(Source(n),45,315,frame,1);
            Assert::IsTrue(r.detected);Assert::AreEqual(first.cue,r.cue);
            Assert::AreEqual(first.bounds.left,r.bounds.left);Assert::AreEqual(first.bounds.top,r.bounds.top);
            Assert::AreEqual(first.bounds.right,r.bounds.right);Assert::AreEqual(first.bounds.bottom,r.bounds.bottom);
        }
    }
    TEST_METHOD(LatePictureLineIsNotPermanentlyFrozenOut) {
        auto f=Frame();Text(f,220,320,14);SubtitleBoxDetector d;
        auto first=d.Analyze(Source(f),45,315,1,1);Assert::IsTrue(first.detected);
        PanelToBar(f,120,275,500);Text(f,150,295,25);auto next=d.Analyze(Source(f),45,315,2,1);
        Assert::IsTrue(next.detected);Assert::IsTrue(next.revised);
        Assert::AreEqual(2,next.lineCount);Assert::IsTrue(next.bounds.top<=295);
    }
    TEST_METHOD(PictureOnlyAndBlankFramesDoNotAcquire) {
        auto f=Frame();Text(f,200,280,20);SubtitleBoxDetector d;
        Assert::IsFalse(d.Analyze(Source(f),45,315,1,1).detected);
        f=Frame();Assert::IsFalse(d.Analyze(Source(f),45,315,2,1).bounds.Valid());
    }
    TEST_METHOD(DetectorMissStillPublishesMatchingInkPlanes) {
        auto f=Frame();Fill(f,320,320,321,321,510);
        SubtitleBoxDetector detector;
        Assert::IsFalse(detector.Analyze(Source(f),45,315,1,1).detected);
        const auto ink=detector.InkSnapshot();
        Assert::IsTrue(ink!=nullptr);
        Assert::IsTrue(!ink->rawInk.empty());
        Assert::IsTrue(ink->rawInk.size()==ink->ownedInk.size(),
            L"a grouping miss must leave a complete ownership plane for presentation");
    }
    TEST_METHOD(PictureLineCannotBorrowRejectedBarPixelsInsideItsEnvelope) {
        auto f=Frame();Text(f,200,300,20);
        // One tiny accepted fragment has only two pixels in the bar. Isolated
        // one-pixel noise must not supply the remaining bar proof for the line.
        Fill(f,210,313,212,316,510);
        for(int x : {221,234,247,260,273,286,299,312}) Fill(f,x,315,x+1,316,510);
        SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
        Assert::IsFalse(r.detected);Assert::IsFalse(r.bounds.Valid());
    }
    TEST_METHOD(PaddingIntoBarDoesNotMakePictureTextEligible) {
        auto f=Frame();Text(f,200,300,20);
        // Bright isolated bar pixels elsewhere allow threshold estimation but
        // do not belong to this picture-only cue (whose padding crosses y=315).
        for(int x=10;x<50;x+=3) Fill(f,x,340,x+1,341,510);
        SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
        Assert::IsFalse(r.detected);Assert::IsFalse(r.bounds.Valid());
    }
    TEST_METHOD(PartialSampledTopBarRowQualifiesOnFirstFrame) {
        const int w=1920,h=1080,top=137,bottom=945;
        std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x)
            f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom ? 200:64)<<6;
        for(int y=top;y<190;++y) for(int x=588;x<1380;++x) f[static_cast<size_t>(y)*w+x]=64<<6;
        // Only the first source row of each letter is inside the real top bar.
        // Sampling at y=136 must count it, despite the unaligned boundary y=137.
        for(int n=0;n<20;++n) for(int y=0;y<42;++y) for(int x=0;x<24;++x)
            f[static_cast<size_t>(136+y)*w+600+n*39+x]=
                (x<6 || x>=18 || y<6 || y>=36 ? 510:64)<<6;
        auto source=Source(f);source.width=w;source.height=h;
        source.rowBytes=source.chromaRowBytes=w*2;
        SubtitleBoxDetector d;auto r=d.Analyze(source,top,bottom,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(uint32_t{1},r.observations);
        Assert::IsTrue(r.bounds.top<=136 && r.bounds.bottom>=178);
    }
    TEST_METHOD(FarSideTitleCannotPoisonShortCenteredSubtitleGlyphScale) {
        for(bool right:{false,true}) for(int height:{10,24}) {
            auto f=Frame();const int width=height*3/4;
            const int left=(640-(width*2+4))/2;
            for(int n=0;n<2;++n) {
                const int x=left+n*(width+4);
                Fill(f,x,320,x+width,320+height,510);
                Fill(f,x+2,322,x+width-2,318+height,64);
            }
            SubtitleBoxDetector detector;
            const auto withoutTitle=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(withoutTitle.detected);
            const int titleHeight=height==10?24:10,titleWidth=titleHeight/2;
            const int titleLeft=right?630-(titleWidth+3)*10:10;
            for(int n=0;n<10;++n) {
                const int x=titleLeft+n*(titleWidth+3);
                Fill(f,x,10,x+titleWidth,10+titleHeight,510);
                Fill(f,x+1,11,x+titleWidth-1,9+titleHeight,64);
            }
            detector.Reset();
            const auto result=detector.Analyze(Source(f),45,315,2,1);
            Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
            Assert::AreEqual(withoutTitle.bounds.left,result.bounds.left);
            Assert::AreEqual(withoutTitle.bounds.top,result.bounds.top);
            Assert::AreEqual(withoutTitle.bounds.right,result.bounds.right);
            Assert::AreEqual(withoutTitle.bounds.bottom,result.bounds.bottom);
        }
    }
    TEST_METHOD(CenteredScaleGroupPreservesOuterLettersOfWideCaption) {
        auto f=Frame();Text(f,50,320,40);
        for(int n=0;n<10;++n) {
            const int x=10+n*8;
            Fill(f,x,15,x+5,25,510);Fill(f,x+1,16,x+4,24,64);
        }
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
        Assert::IsTrue(result.bounds.left<=50 && result.bounds.right>=565);
    }
    TEST_METHOD(OneTinyDescenderBarSampleSurvivesEveryUhdSamplingPhase) {
        const int w=3840,h=2160;
        for(bool atTop:{false,true}) for(int phase=0;phase<4;++phase)
            for(bool entersBar:{false,true}) {
                const int top=276+phase,bottom=h-top;
                std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
                for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                    f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom?200:64)<<6;
                const int glyphTop=atTop?top+4:bottom-40;
                for(int y=(std::max)(top,glyphTop-12);y<(std::min)(bottom,glyphTop+64);++y)
                    for(int x=1430;x<2430;++x) f[static_cast<size_t>(y)*w+x]=64<<6;
                for(int n=0;n<20;++n) for(int y=0;y<36;++y) for(int x=0;x<32;++x)
                    f[static_cast<size_t>(glyphTop+y)*w+1450+n*48+x]=
                        (x<8 || x>=24 || y<8 || y>=28?510:64)<<6;
                const int tailLeft=atTop?1450:1478;
                const int tailTop=atTop?top-(entersBar?1:0):glyphTop+36;
                const int tailBottom=atTop?glyphTop:bottom+(entersBar?1:0);
                for(int y=tailTop;y<tailBottom;++y) for(int x=tailLeft;x<tailLeft+4;++x)
                    f[static_cast<size_t>(y)*w+x]=510<<6;
                auto source=Source(f);source.width=w;source.height=h;
                source.rowBytes=source.chromaRowBytes=w*2;
                SubtitleBoxDetector detector;
                Assert::AreEqual(entersBar,detector.Analyze(source,top,bottom,1,1).detected);
            }
    }
    TEST_METHOD(NarrowTwoGlyphCueUsesProvenSingleDescenderBarSample) {
        const int w=3840,h=2160;
        for(bool atTop:{false,true}) for(int phase=0;phase<4;++phase) {
            const int top=276+phase,bottom=h-top;
            std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
            for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom?200:64)<<6;
            const int glyphTop=atTop?top+4:bottom-40;
            for(int y=(std::max)(top,glyphTop-12);y<(std::min)(bottom,glyphTop+64);++y)
                for(int x=1880;x<1956;++x) f[static_cast<size_t>(y)*w+x]=64<<6;
            for(int n=0;n<2;++n) for(int y=0;y<36;++y) for(int x=0;x<16;++x)
                f[static_cast<size_t>(glyphTop+y)*w+1900+n*20+x]=
                    (x<4 || x>=12 || y<8 || y>=28?510:64)<<6;
            const int tailLeft=atTop?1900:1912;
            for(int y=atTop?top-1:glyphTop+36;y<(atTop?glyphTop:bottom+1);++y)
                for(int x=tailLeft;x<tailLeft+4;++x) f[static_cast<size_t>(y)*w+x]=510<<6;
            auto source=Source(f);source.width=w;source.height=h;
            source.rowBytes=source.chromaRowBytes=w*2;
            SubtitleBoxDetector detector;
            const auto result=detector.Analyze(source,top,bottom,1,1);
            const auto context=L"atTop="+std::to_wstring(atTop)+L" phase="+std::to_wstring(phase);
            Assert::IsTrue(result.detected,context.c_str());Assert::AreEqual(1,result.lineCount);
            Assert::IsTrue(result.lineBounds[0].left<=1900 && result.lineBounds[0].right>=1936,
                L"the non-crossing narrow neighbor must survive beside the actual descender anchor");
        }
    }
    TEST_METHOD(IsolatedTinyBarNoiseCannotAuthorizePictureOnlyLine) {
        const int w=3840,h=2160;
        for(int phase=0;phase<4;++phase) {
            const int top=276+phase,bottom=h-top;
            std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
            for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom?200:64)<<6;
            for(int n=0;n<20;++n) for(int y=0;y<36;++y) for(int x=0;x<32;++x)
                f[static_cast<size_t>(bottom-52+y)*w+1450+n*48+x]=
                    (x<8 || x>=24 || y<8 || y>=28?510:64)<<6;
            for(int x=1490;x<1494;++x) f[static_cast<size_t>(bottom)*w+x]=510<<6;
            auto source=Source(f);source.width=w;source.height=h;
            source.rowBytes=source.chromaRowBytes=w*2;
            SubtitleBoxDetector detector;
            Assert::IsFalse(detector.Analyze(source,top,bottom,1,1).detected);
        }
    }
    TEST_METHOD(DetachedQuestionMarkHeadAndDotJoinTheSameTextLine) {
        auto f=Frame();Text(f,200,320,12);
        Fill(f,352,320,370,322,510);Fill(f,352,322,354,325,510);
        Fill(f,368,322,370,326,510);Fill(f,356,325,370,327,510);
        Fill(f,356,327,358,329,510);Fill(f,356,332,358,334,510);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
        Assert::IsTrue(result.lineBounds[0].right>=370 && result.lineBounds[0].bottom>=334);
    }
    TEST_METHOD(QuestionDotMergeCannotStealOwnershipFromLaterGlyphs) {
        auto f=Frame();PanelToBar(f,0,235,465);
        // The card joins an unbounded dark scene at the left. New upper
        // strokes may be discovered, but there is no finite rectangle that
        // can independently authorize them as a third subtitle row.
        Text(f,240,320,12);Text(f,240,277,10);
        Fill(f,372,277,390,279,510);Fill(f,372,279,374,282,510);
        Fill(f,388,279,390,283,510);Fill(f,376,282,390,284,510);
        Fill(f,376,284,378,286,510);Fill(f,376,289,378,291,510);
        // The upper marks are outside the first search band. Card tracing
        // discovers them only after reconnecting the earlier question dot.
        // They are outside the finite card and cannot join this two-line cue.
        Text(f,405,240,3);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        const auto ink=detector.InkSnapshot();Assert::IsNotNull(ink.get());
        unsigned acceptedInk=0;
        for(int y=320;y<334;++y)for(int x=240;x<391;++x) {
            if(f[y*W+x]!=(510<<6))continue;
            ++acceptedInk;
            Assert::IsTrue(ink->Get(x,y,true),
                L"a later discovered component must not steal an accepted glyph's ownership label");
        }
        Assert::AreEqual(864u,acceptedInk);
        Assert::IsTrue(result.lineBounds[0].right>=391);
        Assert::IsFalse(ink->Get(405,240,true),
            L"a remote row must not borrow ownership from the accepted caption");
    }

    TEST_METHOD(QuestionMarkCannotBorrowAChainOfLowerNoiseDots) {
        auto f=Frame();Text(f,200,320,12);
        Fill(f,352,320,370,322,510);Fill(f,352,322,354,325,510);
        Fill(f,368,322,370,326,510);Fill(f,356,325,370,327,510);
        Fill(f,356,327,358,329,510);Fill(f,356,332,358,334,510);
        SubtitleBoxDetector detector;
        const auto clean=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(clean.detected);
        Fill(f,356,338,358,340,510);Fill(f,356,344,358,346,510);
        detector.Reset();
        const auto result=detector.Analyze(Source(f),45,315,2,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
        Assert::AreEqual(clean.bounds.right,result.bounds.right);
        Assert::AreEqual(clean.bounds.bottom,result.bounds.bottom);
    }
    TEST_METHOD(CompanionCentersFollowTheCueRatherThanTheRasterCenterAlone) {
        auto f=Frame();Text(f,186,320,21);Text(f,240,300,2);
        SubtitleBoxDetector detector;
        const auto offCenter=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(offCenter.detected);Assert::AreEqual(1,offCenter.lineCount);
        Assert::IsTrue(offCenter.bounds.top>=316);
        f=Frame();PanelToBar(f,140,270,550);Text(f,186,320,21);Text(f,164,294,28);
        detector.Reset();
        const auto legitimate=detector.Analyze(Source(f),45,315,2,1);
        Assert::IsTrue(legitimate.detected);Assert::AreEqual(2,legitimate.lineCount);
        Assert::IsTrue(legitimate.bounds.top<=294 && legitimate.bounds.right>=523);
    }
    TEST_METHOD(ThinPictureGlyphNativeCoresSurviveEveryUhdPhaseAndLowPq) {
        const int w=3840,h=2160,top=276,bottom=1884;
        for(int white:{180,700}) for(int phase=0;phase<4;++phase) {
            const int fringe=white==180?160:600;
            std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
            for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom?(white==180?90:200):64)<<6;
            for(int y=1780;y<bottom;++y) for(int x=1350;x<2500;++x)
                f[static_cast<size_t>(y)*w+x]=64<<6;
            for(int line=0;line<2;++line) for(int n=0;n<20;++n)
                for(int y=0;y<48;++y) for(int x=0;x<32;++x) {
                    const bool stroke=x<8 || x>=24 || y<8 || y>=40;
                    const int value=!stroke?64:line==0 || x%4==1?white:fringe;
                    const int glyphTop=line==0?1900:1818;
                    f[static_cast<size_t>(glyphTop+y)*w+1450+phase+n*48+x]=value<<6;
                }
            auto source=Source(f);source.width=w;source.height=h;
            source.rowBytes=source.chromaRowBytes=w*2;
            SubtitleBoxDetector detector;
            const auto result=detector.Analyze(source,top,bottom,1,1);
            Assert::IsTrue(result.detected);
            Assert::AreEqual(2,result.lineCount,(L"thin 4K companion missing at white="+
                std::to_wstring(white)+L" phase="+std::to_wstring(phase)).c_str());
            Assert::IsTrue(result.bounds.top<=1818 && result.bounds.bottom>=1948);
        }
    }
    TEST_METHOD(FlatGrayPicturePrefixCannotExtendFullBrightnessCaption) {
        const int w=3840,h=2160,top=276,bottom=1884;
        std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x)
            f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom?200:64)<<6;
        for(int y=1740;y<bottom;++y) for(int x=1100;x<2600;++x)
            f[static_cast<size_t>(y)*w+x]=64<<6;
        auto text=[&](int left,int glyphTop,int count,int white) {
            for(int n=0;n<count;++n) for(int y=0;y<48;++y) for(int x=0;x<32;++x)
                f[static_cast<size_t>(glyphTop+y)*w+left+n*48+x]=
                    (x<8 || x>=24 || y<8 || y>=40?white:64)<<6;
        };
        text(1450,1900,20,700);text(1660,1818,12,700);
        text(1200,1818,9,600); // Same baseline and color, but no native white core.
        auto source=Source(f);source.width=w;source.height=h;
        source.rowBytes=source.chromaRowBytes=w*2;
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(source,top,bottom,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::IsTrue(result.bounds.top<=1818 && result.bounds.left>=1440);
    }
    TEST_METHOD(EqualBrightnessPicturePrefixNeedsTheLearnedSubtitleCoreColor) {
        for(bool coloredSubtitle:{false,true}) {
            auto f=Frame();PanelToBar(f,180,270,470);Text(f,230,320,14,983);Text(f,240,295,12,983);
            auto color=[&](int left,int top,int right,int bottom,int u,int v) {
                for(int y=top/2;y<(bottom+1)/2;++y) for(int x=left/2;x<(right+1)/2;++x) {
                    f[W*H+y*W+x*2]=static_cast<uint16_t>(u<<6);
                    f[W*H+y*W+x*2+1]=static_cast<uint16_t>(v<<6);
                }
            };
            if(coloredSubtitle) {
                color(230,320,407,334,475,441);
                color(240,295,391,309,475,441);
            }
            Text(f,205,295,2,983);
            // Equal luma and within the loose antialias mask tolerance. This
            // color is either unrelated scenery or the actual learned text.
            color(205,295,226,309,475,441);
            SubtitleBoxDetector detector;
            const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
            if(coloredSubtitle) Assert::IsTrue(result.bounds.left<=205);
            else Assert::IsTrue(result.bounds.left>=226);
        }
    }
    TEST_METHOD(NativeBarGlyphCompletionDoesNotRequireRecognizableQuestionDot) {
        const int w=3840,h=2160,top=276,bottom=1884;
        for(int phaseX=0;phaseX<4;++phaseX) for(int phaseY=0;phaseY<4;++phaseY)
            for(bool nativeDot:{false,true}) {
                std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
                for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                    f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom?200:64)<<6;
                auto fill=[&](int left,int ytop,int right,int ybottom,int code) {
                    for(int y=ytop;y<ybottom;++y) for(int x=left;x<right;++x)
                        f[static_cast<size_t>(y)*w+x]=code<<6;
                };
                const int glyphTop=1900+phaseY;
                for(int n=0;n<12;++n) {
                    const int x=1600+phaseX+n*48;
                    fill(x,glyphTop,x+32,glyphTop+48,700);
                    fill(x+8,glyphTop+8,x+24,glyphTop+40,64);
                }
                const int headLeft=2172+phaseX;
                fill(headLeft,glyphTop,headLeft+32,glyphTop+8,700);
                fill(headLeft+24,glyphTop+8,headLeft+32,glyphTop+20,700);
                fill(headLeft+8,glyphTop+16,headLeft+32,glyphTop+24,700);
                fill(headLeft+8,glyphTop+24,headLeft+12,glyphTop+28,700);
                if(nativeDot) fill(headLeft+9,glyphTop+46,headLeft+10,glyphTop+47,700);
                auto source=Source(f);source.width=w;source.height=h;
                source.rowBytes=source.chromaRowBytes=w*2;
                SubtitleBoxDetector detector;
                const auto result=detector.Analyze(source,top,bottom,1,1);
                const std::wstring context=L"phaseX="+std::to_wstring(phaseX)+
                    L" phaseY="+std::to_wstring(phaseY)+L" nativeDot="+
                    std::to_wstring(nativeDot)+L" detected="+std::to_wstring(result.detected)+
                    L" lineCount="+std::to_wstring(result.lineCount)+L" right="+
                    std::to_wstring(result.lineCount?result.lineBounds[0].right:-1)+
                    L" questionLeft="+std::to_wstring(headLeft);
                Assert::IsTrue(result.detected,context.c_str());
                Assert::AreEqual(1,result.lineCount,context.c_str());
                // Nearby bright strokes on verified black backing belong to
                // the caption even without a recognizable question-mark dot.
                // Dot visibility must not decide whether the head is captured.
                Assert::IsTrue(result.lineBounds[0].right>=headLeft+32,context.c_str());
            }
    }
    TEST_METHOD(TinyTrailingPeriodJoinsConfirmedLettersButCannotSeedText) {
        auto f=Frame();PanelToBar(f,190,298,365);Text(f,200,308,12);
        Fill(f,355,320,356,322,510); // Two analysis pixels; not a letter component.
        SubtitleBoxDetector detector;
        auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::IsTrue(result.bounds.right>=356);
        f=Frame();
        for(int x=200;x<400;x+=8) Fill(f,x,320,x+1,322,510);
        Assert::IsFalse(detector.Analyze(Source(f),45,315,2,1).detected);
    }
    TEST_METHOD(UnassignedBrightShapeInsideLineDoesNotChangeItsSignature) {
        auto f=Frame();Text(f,180,320,5);Text(f,254,320,5);
        SubtitleBoxDetector detector;const auto first=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(first.detected);
        // This accepted small component lies in a word space, but its shape
        // belongs to neither the core letters nor the bounded punctuation set.
        Fill(f,242,324,252,327,510);
        const auto second=detector.Analyze(Source(f),45,315,2,1);
        Assert::IsTrue(second.detected);
        Assert::IsTrue(first.lineSignatures==second.lineSignatures);
    }
    TEST_METHOD(SmallerXHeightPictureLineCanJoinCapitalHeightBarLine) {
        auto f=Frame();PanelToBar(f,180,270,450);Text(f,220,320,15);
        for(int n=0;n<25;++n) {
            const int x=200+n*8;
            Fill(f,x,295,x+5,302,510);Fill(f,x+1,296,x+4,301,64);
        }
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::IsTrue(result.bounds.top<=295);
    }
    TEST_METHOD(FiniteLeadingAndTrailingDotClustersOwnAllDotsAcrossScales) {
        for(int scale:{1,2,4}) for(bool leading:{false,true}) {
            const int width=W*scale,height=H*scale;
            std::vector<uint16_t> f(size_t(width)*height*3/2,512<<6);
            auto fill=[&](int l,int t,int r,int b,int value) {
                for(int y=t*scale;y<b*scale;++y) for(int x=l*scale;x<r*scale;++x)
                    f[size_t(y)*width+x]=static_cast<uint16_t>(value<<6);
            };
            fill(0,0,W,H,64);fill(0,45,W,315,200);
            for(int n=0;n<12;++n) {
                const int x=200+n*13;fill(x,320,x+8,334,510);fill(x+2,322,x+6,332,64);
            }
            for(int n=0;n<3;++n) {
                const int x=leading?194-n*8:355+n*8;
                fill(x,332,x+2,334,510);
            }
            auto source=Source(f);source.width=width;source.height=height;
            source.rowBytes=source.chromaRowBytes=width*2;
            SubtitleBoxDetector detector;const auto result=detector.Analyze(source,45*scale,315*scale,1,1);
            Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
            if(leading)Assert::IsTrue(result.lineBounds[0].left-SubtitleBoxDetector::SamplingStep(width,height)+1<=178*scale);
            else Assert::IsTrue(result.lineBounds[0].right>=373*scale);
            const auto snapshot=detector.InkSnapshot();Assert::IsTrue(bool(snapshot));
            unsigned owned=0;
            for(auto word:snapshot->ownedInk)for(;word;word&=word-1)++owned;
            Assert::IsTrue(owned>0);
        }
    }
    TEST_METHOD(SpacedTerminalDotsMatchTheReportedMonospaceGeometry) {
        const int width=796,height=298;
        std::vector<uint16_t> f(size_t(width)*height*3/2,512<<6);
        auto fill=[&](int l,int t,int r,int b,int value) {
            for(int y=t;y<b;++y)for(int x=l;x<r;++x)f[size_t(y)*width+x]=value<<6;
        };
        fill(0,0,width,height,64);fill(0,0,width,136,200);
        for(int x=178;x<662;x+=22){
            fill(x,173,x+10,191,510);fill(x+3,176,x+7,188,64);
        }
        fill(652,178,662,191,510);fill(655,181,659,188,64);
        // Match the three independently sampled components from the screenshot.
        fill(670,187,673,191,510);fill(685,187,688,191,510);fill(699,187,703,191,510);
        auto source=Source(f);source.width=width;source.height=height;
        source.rowBytes=source.chromaRowBytes=width*2;
        SubtitleBoxDetector detector;const auto result=detector.Analyze(source,0,136,1,1);
        Assert::IsTrue(result.detected);Assert::IsTrue(result.lineBounds[0].right>=703);
    }
    TEST_METHOD(ClosingQuoteAfterPeriodJoinsItsOriginalRow) {
        auto f=Frame();Text(f,200,320,12);
        Fill(f,355,332,357,334,510);
        Fill(f,366,320,368,327,510);Fill(f,374,320,376,327,510);
        SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
        Assert::IsTrue(result.lineBounds[0].right>=376);
    }
    TEST_METHOD(TinyPictureSideDotsRemainOwnedWithTheirJoinedScriptRow) {
        auto f=Frame();PanelToBar(f,180,270,470);
        JoinedArabicWord(f,200,306,240,20);JoinedArabicWord(f,200,277,240,20);
        // A detached two-pixel diacritic above the connected script body.
        Fill(f,231,275,232,277,510);
        SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::IsTrue((std::min)(result.lineBounds[0].top,result.lineBounds[1].top)<=275);
    }
    TEST_METHOD(TrailingSpecksCannotChainBeyondOriginalLetterExtent) {
        auto f=Frame();PanelToBar(f,190,298,365);Text(f,200,308,12);
        for(int x=355;x<500;x+=4) Fill(f,x,320,x+1,322,510);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::IsTrue(result.bounds.right>=356);
        Assert::IsTrue(result.bounds.right<=363);
    }
    TEST_METHOD(PictureHighlightCannotChainIntoBarLettersAtAnotherBaseline) {
        auto f=Frame();PanelToBar(f,210,303,460);Text(f,220,308,18);
        // Thin bright picture shape near the letters, but on a higher baseline.
        for(int x=168;x<204;++x) Fill(f,x,300,x+1,302,510);
        Fill(f,168,292,170,302,510);
        SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::IsTrue(result.bounds.left>=216);
    }
    TEST_METHOD(DisconnectedLetterStrokeGroupsRejoinTheSameTextRow) {
        auto f=Frame();PanelToBar(f,160,270,410);Text(f,176,320,15);
        Text(f,200,295,5);
        Fill(f,266,295,269,302,510); // Upper stroke starts its own short group.
        Text(f,270,295,6); // Following letters establish the same baseline.
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::IsTrue(result.bounds.left<=200 && result.bounds.right>=343);
    }
    TEST_METHOD(WordSpaceLargerThanLetterHeightStillFormsOneLine) {
        auto f=Frame();Text(f,220,320,5);Text(f,300,320,5);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
        Assert::IsTrue(result.bounds.left<=220 && result.bounds.right>=360);
    }
    TEST_METHOD(WideWordSpaceKeepsBothSubtitleWordsInOneLine) {
        // A renderer may leave nearly four glyph heights between words. The
        // line must retain both extents while the genuine bar strokes remain
        // the acquisition anchor.
        auto f=Frame();Text(f,180,320,5);Text(f,295,320,5);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
        Assert::IsTrue(result.anchor.Valid());
        Assert::IsTrue(result.bounds.left<=180 && result.bounds.right>=355,
            L"a broad but baseline-aligned word gap must not truncate the subtitle line");
    }
    TEST_METHOD(BrightPictureSubtitleCompanionKeepsFullWidthFromBarAnchor) {
        auto f=Frame();
        // Simulate a bright stairway/background. The dark outline remains a
        // local glyph cue, while the lower row supplies real black-bar ink.
        Fill(f,0,45,W,315,470);
        PanelToBar(f,150,278,490);
        Text(f,195,288,20);Text(f,240,320,12);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::IsTrue(result.anchor.Valid() && result.anchor.bottom>315,
            L"the independently detected lower subtitle row must remain the bar anchor");
        Assert::IsTrue(result.bounds.left<=195,
            (L"bright-picture box left="+std::to_wstring(result.bounds.left)).c_str());
        Assert::IsTrue(result.bounds.right>=447,L"bright-picture companion right extent was lost");
        Assert::IsTrue(result.bounds.top<=288,L"bright-picture companion top extent was lost");
        Assert::IsTrue(result.bounds.bottom>=334,L"bar anchor right extent was lost");
    }
    TEST_METHOD(WideLowPictureHighlightCannotExtendRealLetters) {
        auto f=Frame();PanelToBar(f,235,298,480);Text(f,240,308,18);
        Fill(f,211,314,232,316,510);Fill(f,211,308,213,316,510);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::IsTrue(result.bounds.left>=236);
    }
    TEST_METHOD(OneSourceRowOfBarInkSurvivesEveryUhdSamplingPhase) {
        const int w=3840,h=2160;
        for(bool atTop : {false,true}) for(int phase=0;phase<4;++phase)
            for(bool entersBar : {false,true}) {
                const int top=276+phase,bottom=h-top;
                std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
                for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                    f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom?200:64)<<6;
                const int glyphTop=atTop ? top-(entersBar?1:0) : bottom-48+(entersBar?1:0);
                for(int y=(std::max)(top,glyphTop-12);y<(std::min)(bottom,glyphTop+64);++y)
                    for(int x=1430;x<2430;++x) f[static_cast<size_t>(y)*w+x]=64<<6;
                for(int n=0;n<20;++n) for(int y=0;y<48;++y) for(int x=0;x<32;++x)
                    f[static_cast<size_t>(glyphTop+y)*w+1450+n*48+x]=
                        (x<8 || x>=24 || y<8 || y>=40?510:64)<<6;
                auto source=Source(f);source.width=w;source.height=h;
                source.rowBytes=source.chromaRowBytes=w*2;
                SubtitleBoxDetector detector;
                const auto result=detector.Analyze(source,top,bottom,1,1);
                Assert::AreEqual(entersBar,result.detected);
                if(entersBar) {
                    Assert::IsTrue(result.bounds.top<=glyphTop);
                    Assert::IsTrue(result.bounds.bottom>=glyphTop+48);
                }
            }
    }
    TEST_METHOD(ThreeLineOpaqueCardRetainsTallCompanionAndBacking) {
        for(bool topSide : {false,true}) {
            auto f=Frame();PanelToBar(f,190,238,450);
            Text(f,220,244,15);Text(f,202,278,18);Text(f,240,311,12);
            if(topSide) for(int y=0;y<H/2;++y) for(int x=0;x<W;++x)
                std::swap(f[y*W+x],f[(H-1-y)*W+x]);
            SubtitleBoxDetector detector;
            const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(result.detected);
            Assert::AreEqual(3,result.lineCount);
            Assert::IsTrue(result.bounds.top <= (topSide?H-325:244));
            Assert::IsTrue(result.bounds.bottom >= (topSide?H-244:325));
        }
    }
    TEST_METHOD(LosingBarLineImmediatelyClearsBoxDespiteRemainingPictureLine) {
        auto f=Frame();Text(f,240,320,12);Text(f,150,295,25);
        SubtitleBoxDetector d;Assert::IsTrue(d.Analyze(Source(f),45,315,1,1).detected);
        Fill(f,0,315,W,H,64);
        auto r=d.Analyze(Source(f),45,315,2,1);
        Assert::IsFalse(r.detected);Assert::IsFalse(r.held);Assert::IsFalse(r.bounds.Valid());
    }
    TEST_METHOD(TopBarAndBoundaryWorkAtLowPqLuma) {
        for(int y: {20,38,310,325}) {
            auto f=Frame();Fill(f,190,(std::max)(45,y-10),470,(std::min)(315,y+24),64);Text(f,200,y,20,410);SubtitleBoxDetector d;
            auto r=d.Analyze(Source(f),45,315,1,1);Assert::IsTrue(r.detected);
            Assert::IsTrue(r.bounds.top<=y && r.bounds.bottom>=y+14);
        }
    }
    TEST_METHOD(HoldRequiresCurrentBarAnchorAndRemainsBounded) {
        auto f=Frame();PanelToBar(f,150,260,490);Text(f,200,320,20);SubtitleBoxDetector d;
        d.Analyze(Source(f),45,315,1,1);
        // Too many companion proposals cannot authorize new geometry. A short
        // diagnostic hold is allowed only while the original bar anchor exists.
        Text(f,200,300,20);Text(f,200,284,20);Text(f,200,268,20);
        auto r=d.Analyze(Source(f),45,315,2,1);Assert::IsFalse(r.detected);Assert::IsTrue(r.held);
        d.Analyze(Source(f),45,315,3,1);
        Assert::IsFalse(d.Analyze(Source(f),45,315,4,1).bounds.Valid());
    }
    TEST_METHOD(GenerationOrPictureChangeCannotHoldOldBox) {
        auto f=Frame();Text(f,200,320,20);SubtitleBoxDetector d;
        d.Analyze(Source(f),45,315,1,1);f=Frame();
        Assert::IsFalse(d.Analyze(Source(f,2),45,315,2,1).bounds.Valid());
        Text(f,200,320,20);d.Analyze(Source(f,2),45,315,3,1);
        Assert::IsFalse(d.Analyze(Source(f,2),0,H,4,1).bounds.Valid());
    }
    TEST_METHOD(InputPixelsRemainUntouchedAndRepeatedFrameDoesNotConfirm) {
        auto f=Frame();Text(f,200,320,20);const auto original=f;SubtitleBoxDetector d;
        auto first=d.Analyze(Source(f),45,315,1,1);
        auto repeat=d.Analyze(Source(f),45,315,1,1);
        Assert::AreEqual(first.observations,repeat.observations);Assert::IsTrue(f==original);
    }
}; }
