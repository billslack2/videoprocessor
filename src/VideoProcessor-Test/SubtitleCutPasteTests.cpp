#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleCutPaste.h>
#include <SubtitleTemporalFill.h>
#include <SubtitleCutPasteShader.h>
#include <SubtitleBackingLip.h>
#include <SubtitleBackingPresentation.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/gpu.h>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <chrono>
#include <SubtitlePreviewMode.h>
#include <climits>
#include <ConfigFile.h>
#include <RendererProfileConfig.h>
#include <RendererConfigView.h>
#include <fstream>
#include <string>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests {
TEST_CLASS(SubtitleCutPasteTests) {
    static SubtitleBoxPreview Preview() {
        SubtitleBoxPreview p;p.available=true;p.policyGeneration=7;p.continuityGeneration=8;
        p.current.analyzed=p.current.barAuthority=true;
        p.current.width=640;p.current.height=360;p.current.pictureTop=45;p.current.pictureBottom=315;
        p.current.identity={1,1,1,1000,2,3,4};return p;
    }
    static SubtitleBoxRect MirrorSubtitleRect(SubtitleBoxRect r) {
        return {r.left,360-r.bottom,r.right,360-r.top};
    }
    static void ConfigureProofFrame(SubtitleBoxPreview& p,const SubtitleBoxResult& text,
        bool top,bool local,uint64_t sequence,const SubtitleBoxRect& localBounds={},
        int measuredPlane=-1,int selectedPlane=-1) {
        p.available=true;p.policyGeneration=7;p.continuityGeneration=8;
        p.current.identity.acceptedSequence=sequence;
        p.current.analyzed=p.current.barAuthority=true;
        p.current.width=640;p.current.height=360;
        const int measured=measuredPlane>=0?measuredPlane:(top?45:315);
        const int selected=selectedPlane>=0?selectedPlane:(top?45:315);
        p.current.pictureTop=top?measured:45;p.current.pictureBottom=top?315:measured;
        p.current.nearBarDistance=20;
        p.current.policyGeneration=p.policyGeneration;p.current.continuityGeneration=p.continuityGeneration;
        p.current.text=text;
        p.current.sharedPicture.required=true;
        p.current.sharedPicture.bounds=top?
            ActivePictureBounds{0,selected,640,315,640,360,0.0,ActivePictureBounds::BarAxes::TOP_BOTTOM}:
            ActivePictureBounds{0,45,640,selected,640,360,0.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
        p.current.sharedPicture.identity=p.current.identity;
        p.current.barEvidence.available=true;
        p.current.barEvidence.top=local&&top?0:(top?measured:45);
        p.current.barEvidence.bottom=local&&!top?360:(top?315:measured);
        p.current.barEvidence.captionBackedTop=local&&top;
        p.current.barEvidence.captionBackedBottom=local&&!top;
        p.current.glyphLocalTop=local&&top;
        p.current.glyphLocalBottom=local&&!top;
        p.current.glyphLocalVerifiedBounds=local?localBounds:SubtitleBoxRect{};
    }
    static SubtitleBoxResult Text() {
        SubtitleBoxResult r;r.detected=true;r.cue=1;r.bounds={200,295,400,330};return r;
    }
public:
    TEST_METHOD(CurrentCompleteBackingCannotGrowUnchangedCueIntoScenery) {
        for(bool top:{false,true})for(bool rounded:{false,true})for(int reduction:{0,30,75}) {
            auto p=Preview();p.current.width=3840;p.current.height=2160;
            p.current.pictureTop=276;p.current.pictureBottom=1884;
            auto text=Text();text.bounds={966,1670,2870,1934};text.lineCount=2;
            text.lineBounds[0]={970,1677,2858,1776};text.lineBounds[1]={970,1821,2863,1924};
            text.capturePanelMeasured=true;
            const auto mirror=[&](SubtitleBoxRect r) {return top?SubtitleBoxRect{r.left,2160-r.bottom,r.right,2160-r.top}:r;};
            text.bounds=mirror(text.bounds);for(int i=0;i<2;++i)text.lineBounds[i]=mirror(text.lineBounds[i]);
            SubtitleCutPastePresentation presentation;SubtitleLayoutAnchor anchor;
            const SubtitleBoxPadding padding{60,40,21};
            const auto compose=[&](SubtitleBoxRect local,SubtitleBoxRect proof) {
                text.sourcePanel=mirror(local);text.provenSourcePanel=mirror(proof);text.capturePanel=text.sourcePanel;
                p.current.text=text;++p.current.identity.acceptedSequence;
                auto g=presentation.Consume(text,p,padding,20);
                g=ApplySubtitleTextReduction(FitSubtitleToVisiblePicture(g,{0,276,3840,1884},20),
                    reduction,padding,rounded,false);
                return anchor.Consume(g,text,p,padding,20,false);
            };
            const auto first=compose({870,1634,2974,1884},{870,1640,2974,1884});
            Assert::IsTrue(first.valid);
            const auto larger=compose({792,1166,3530,1884},{792,1166,3530,1884});
            Assert::IsTrue(larger.valid);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(first.generatedCleanup,larger.generatedCleanup,0));
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(first.destination,larger.destination,0));
            Assert::AreEqual(first.glyphTranslateX,larger.glyphTranslateX);
            Assert::AreEqual(first.glyphTranslateY,larger.glyphTranslateY);
            const auto restored=compose({870,1634,2974,1884},{870,1640,2974,1884});
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(first.generatedCleanup,restored.generatedCleanup,0),
                L"Current six-pixel local fringe remains intact");
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(first.destination,restored.destination,0));
        }
    }
    TEST_METHOD(CurrentCompleteCorrectionShrinksDecorationWithoutMovingGlyphs) {
        for(bool top:{false,true})for(bool rounded:{false,true})for(int reduction:{0,30,75}) {
            auto p=Preview();p.current.width=3840;p.current.height=2160;
            p.current.pictureTop=276;p.current.pictureBottom=1884;
            auto text=Text();text.bounds={966,1670,2870,1934};text.lineCount=2;
            text.lineBounds[0]={970,1677,2858,1776};text.lineBounds[1]={970,1821,2863,1924};
            text.capturePanelMeasured=true;
            const auto mirror=[&](SubtitleBoxRect r) {return top?SubtitleBoxRect{r.left,2160-r.bottom,r.right,2160-r.top}:r;};
            text.bounds=mirror(text.bounds);for(int i=0;i<2;++i)text.lineBounds[i]=mirror(text.lineBounds[i]);
            SubtitleCutPastePresentation presentation;SubtitleLayoutAnchor anchor;
            const SubtitleBoxPadding padding{60,40,21};
            const auto compose=[&](SubtitleBoxRect local,SubtitleBoxRect proof,int freshness=0) {
                text.sourcePanel=mirror(local);text.provenSourcePanel=mirror(proof);text.capturePanel=text.sourcePanel;
                p.current.text=text;++p.current.identity.acceptedSequence;
                p.current.pendingRefresh=freshness==1;p.current.analysisRefresh=freshness==2;
                p.current.text.held=freshness==3;p.current.text.workLimit=freshness==4;
                auto g=presentation.Consume(text,p,padding,20);
                g=ApplySubtitleTextReduction(FitSubtitleToVisiblePicture(g,{0,276,3840,1884},20),
                    reduction,padding,rounded,false);
                return anchor.Consume(g,text,p,padding,20,false);
            };
            const auto oversized=compose({792,1166,3530,1884},{792,1166,3530,1884});
            Assert::IsTrue(oversized.valid);
            const auto localOnly=compose({870,1634,2974,1884},{});
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(oversized.destination,localOnly.destination,0),
                L"An incomplete local estimate cannot shrink accepted decoration");
            for(int freshness=1;freshness<=4;++freshness) {
                const auto incomplete=compose({870,1634,2974,1884},{870,1640,2974,1884},freshness);
                Assert::IsTrue(incomplete.valid && !incomplete.currentBackingCorrection);
                Assert::IsTrue(SubtitleBoxLookahead::SameLine(oversized.destination,incomplete.destination,0),
                    L"Pending, refreshed, held or incomplete text cannot mint a current complete correction");
            }
            const auto corrected=compose({870,1634,2974,1884},{870,1640,2974,1884});
            Assert::IsTrue(corrected.valid && corrected.currentBackingCorrection);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(corrected.generatedCleanup,mirror({870,1634,2974,1884}),0));
            Assert::IsTrue(corrected.destination.bottom-corrected.destination.top<oversized.destination.bottom-oversized.destination.top);
            Assert::IsTrue(corrected.destination.right-corrected.destination.left<oversized.destination.right-oversized.destination.left);
            Assert::AreEqual(oversized.glyphTranslateX,corrected.glyphTranslateX);
            Assert::AreEqual(oversized.glyphTranslateY,corrected.glyphTranslateY);
            const auto repeat=compose({870,1634,2974,1884},{});
            Assert::IsFalse(repeat.currentBackingCorrection,L"The correction receipt cannot survive into an unproved later call");
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(corrected.destination,repeat.destination,0));
            // Tracker-confirmed new picture-side glyphs may acquire their real
            // current card; the blank-growth restriction is not a size clamp.
            text.revised=true;text.lineCount=3;text.lineBounds[2]=mirror({1100,1420,2700,1510});
            text.bounds=mirror({966,1410,2870,1934});
            const auto revised=compose({792,1390,3530,1884},{792,1390,3530,1884});
            Assert::IsTrue(revised.valid);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(revised.generatedCleanup,mirror({792,1390,3530,1884}),0));
            p.current.analyzed=false;
            const auto unanalyzed=compose({870,1634,2974,1884},{870,1640,2974,1884});
            Assert::IsFalse(unanalyzed.valid || unanalyzed.currentBackingCorrection);
        }
    }
    TEST_METHOD(TemporalCleanRunRequiresConsecutiveCertifiedSourceFrames) {
        unsigned count=0;
        for(int i=0;i<7;++i)count=SubtitleTemporalFill::AdvanceCleanRun(count,true,false,true);
        Assert::AreEqual(7u,count);
        count=SubtitleTemporalFill::AdvanceCleanRun(count,true,false,false);
        Assert::AreEqual(0u,count,L"Unknown current or future source must break the clean run.");
        count=SubtitleTemporalFill::AdvanceCleanRun(count,true,false,true);
        Assert::AreEqual(1u,count);
        for(int i=0;i<30;++i)count=SubtitleTemporalFill::AdvanceCleanRun(count,true,false,true);
        Assert::AreEqual(8u,count,L"Saturate once the donor capture threshold is met.");
        Assert::AreEqual(0u,SubtitleTemporalFill::AdvanceCleanRun(count,false,false,true));
    }
    TEST_METHOD(TemporalCleanRunIgnoresRepeatedDisplayButHonorsDisableAndDiscontinuity) {
        for(bool clean:{false,true}) {
            Assert::AreEqual(7u,SubtitleTemporalFill::AdvanceCleanRun(7,true,true,clean));
            Assert::AreEqual(0u,SubtitleTemporalFill::AdvanceCleanRun(7,false,true,clean),
                L"A disabled or discontinuous source clears the run even on repeat.");
        }
        auto count=SubtitleTemporalFill::AdvanceCleanRun(7,true,true,true);
        count=SubtitleTemporalFill::AdvanceCleanRun(count,true,false,true);
        Assert::AreEqual(8u,count,L"Only a new certified source reaches the threshold.");
    }
    TEST_METHOD(VaryingBackingMeasurementsCannotBreatheAnUnchangedCaption) {
        // Sequence modeled on the reported UHD playback: its glyphs matched 100%,
        // but changing blank margins repeatedly grew the display to 1719 px.
        const std::array<SubtitleBoxRect,8> measured{{
            {1174,1702,2670,1906},{1174,1702,2678,1906},
            {1174,1702,2682,1906},{1174,1702,2714,1906},
            {1174,1702,2746,1906},{1174,1702,2766,1906},
            {1174,1702,2778,1906},{1174,1702,2670,1906}}};
        for(bool top:{false,true})for(bool floating:{false,true})for(int reduction:{0,30,75}) {
            auto p=Preview();p.current.width=3840;p.current.height=2160;
            p.current.pictureTop=254;p.current.pictureBottom=1906;
            auto text=Text();text.bounds={1226,1742,2610,2002};text.lineCount=2;
            text.lineBounds[0]={1230,1892,2600,1992};text.lineBounds[1]={1302,1748,2540,1848};
            auto mirror=[](SubtitleBoxRect r) {return SubtitleBoxRect{r.left,2160-r.bottom,r.right,2160-r.top};};
            if(top) {text.bounds=mirror(text.bounds);for(int i=0;i<2;++i)text.lineBounds[i]=mirror(text.lineBounds[i]);}
            text.capturePanelMeasured=true;
            SubtitleCutPastePresentation presentation;SubtitleLayoutAnchor anchor;
            const SubtitleBoxPadding padding{73,60,25};
            SubtitleCutPasteGeometry first;
            for(size_t frame=0;frame<measured.size();++frame) {
                text.sourcePanel=top?mirror(measured[frame]):measured[frame];
                text.capturePanel=text.sourcePanel;
                p.current.identity.acceptedSequence=frame+1;p.current.text=text;
                auto g=presentation.Consume(text,p,padding,15);
                Assert::AreEqual(text.capturePanel.right,g.pictureCapture.right,L"current extraction stays fresh");
                g=ApplySubtitleTextReduction(FitSubtitleToVisiblePicture(g,{0,254,3840,1906},15),reduction,padding,true,floating);
                g=anchor.Consume(g,text,p,padding,15,floating);
                Assert::IsTrue(g.valid);
                if(frame==0)first=g;
                Assert::IsTrue(SubtitleBoxLookahead::SameLine(first.destination,g.destination,0),L"blank-edge jitter must not resize the card");
                Assert::IsTrue(SubtitleBoxLookahead::SameLine(first.generatedCleanup,g.generatedCleanup,0));
                Assert::AreEqual(first.glyphTranslateX,g.glyphTranslateX);Assert::AreEqual(first.glyphTranslateY,g.glyphTranslateY);
            }
            Assert::AreEqual(uint64_t(6),presentation.HeldBackingMeasurements());
        }
    }
    TEST_METHOD(StableFootprintStillAllowsConfirmedPunctuationAndPictureLineRecovery) {
        for(bool top:{false,true})for(int direction:{0,1,2}) {
            auto p=Preview();auto text=Text();text.lineCount=1;text.lineBounds[0]=text.bounds;
            text.sourcePanel={190,280,410,315};text.capturePanel=text.sourcePanel;text.capturePanelMeasured=true;
            auto mirror=[](SubtitleBoxRect r) {return SubtitleBoxRect{r.left,360-r.bottom,r.right,360-r.top};};
            if(top) {text.bounds=mirror(text.bounds);text.lineBounds[0]=text.bounds;text.sourcePanel=mirror(text.sourcePanel);text.capturePanel=text.sourcePanel;}
            SubtitleCutPastePresentation presentation;SubtitleLayoutAnchor anchor;
            const SubtitleBoxPadding padding{12,18,10};
            auto compose=[&]() {
                p.current.text=text;++p.current.identity.acceptedSequence;
                auto g=presentation.Consume(text,p,padding,15);
                g=ApplySubtitleTextReduction(FitSubtitleToVisiblePicture(g,{0,45,640,315},15),75,padding,true,true);
                return anchor.Consume(g,text,p,padding,15,true);
            };
            const auto first=compose();Assert::IsTrue(first.valid);
            // Corresponds to the tracker's current-plus-queued glyph correction.
            text.revised=true;
            if(direction==0) {text.bounds.left=170;text.sourcePanel.left=160;}
            if(direction==1) {text.bounds.right=435;text.sourcePanel.right=445;}
            if(direction==2) {
                if(top) {text.bounds.bottom=90;text.sourcePanel.bottom=100;}
                else {text.bounds.top=270;text.sourcePanel.top=260;}
            }
            text.lineBounds[0]=text.bounds;text.capturePanel=text.sourcePanel;
            const auto corrected=compose();Assert::IsTrue(corrected.valid);
            Assert::IsTrue(corrected.generatedCleanup.left<=text.sourcePanel.left && corrected.generatedCleanup.right>=text.sourcePanel.right &&
                corrected.generatedCleanup.top<=text.sourcePanel.top && corrected.generatedCleanup.bottom>=text.sourcePanel.bottom);
            const float glyphLeft=corrected.glyphScale*corrected.content.left+corrected.glyphTranslateX;
            const float glyphRight=corrected.glyphScale*corrected.content.right+corrected.glyphTranslateX;
            const float glyphTop=corrected.glyphScale*corrected.content.top+corrected.glyphTranslateY;
            const float glyphBottom=corrected.glyphScale*corrected.content.bottom+corrected.glyphTranslateY;
            Assert::IsTrue(corrected.destination.left<=glyphLeft && corrected.destination.right>=glyphRight &&
                corrected.destination.top<=glyphTop && corrected.destination.bottom>=glyphBottom,
                L"the display envelope contains the current glyphs independently of cleanup authority");
            Assert::IsTrue(corrected.extendToBar,L"recovered picture-side backing must meet the physical bar");
            text.revised=false;text.sourcePanel={};text.capturePanel={};
            const auto weak=compose();Assert::IsTrue(weak.valid && !weak.pictureCapture.Valid());
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(corrected.generatedCleanup,weak.generatedCleanup,0));
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(corrected.destination,weak.destination,0));
        }
    }
    TEST_METHOD(BarOnlyCueCannotBorrowFreshSceneMarginsButConfirmedIntrusionCanGrow) {
        auto p=Preview();auto text=Text();text.bounds={200,322,400,340};text.lineCount=1;text.lineBounds[0]=text.bounds;
        text.capturePanelMeasured=true;
        SubtitleCutPastePresentation presentation;
        const auto first=presentation.Consume(text,p,10,15);Assert::IsTrue(first.valid && !first.generatedCleanup.Valid());
        text.sourcePanel=text.capturePanel={180,280,420,315};
        const auto ambiguous=presentation.Consume(text,p,10,15);
        Assert::IsFalse(ambiguous.generatedCleanup.Valid(),L"a black scene patch without added caption ink cannot authorize picture removal");
        Assert::IsTrue(ambiguous.pictureCapture.Valid(),L"current capture authority remains independent");
        text.revised=true;text.bounds.top=text.lineBounds[0].top=290;
        const auto corrected=presentation.Consume(text,p,10,15);
        Assert::IsTrue(corrected.generatedCleanup.Valid() && corrected.pictureIntrusion);
    }
    TEST_METHOD(BackingFootprintReleasesOnCueAndContextChanges) {
        for(int change=0;change<5;++change) {
            auto p=Preview();auto text=Text();text.sourcePanel={180,270,420,315};
            SubtitleCutPastePresentation presentation;presentation.Consume(text,p,10);
            text.sourcePanel={190,285,410,315};
            if(change==0)++text.cue;
            if(change==1)++p.policyGeneration;
            if(change==2)++p.continuityGeneration;
            if(change==3)++p.current.identity.sourceFormatGeneration;
            if(change==4)p.current.discontinuity=true;
            const auto fresh=presentation.Consume(text,p,10);
            Assert::IsTrue(fresh.valid && SubtitleBoxLookahead::SameLine(text.sourcePanel,fresh.generatedCleanup,0));
        }
    }
    TEST_METHOD(FinalAnchorKeepsTextFixedWhenCurrentMeasurementsVary) {
        for(bool top:{false,true})for(bool floating:{false,true})for(int reduction:{0,25,75}) {
            auto p=Preview();auto text=Text();text.lineCount=1;text.lineBounds[0]=text.bounds;
            SubtitleLayoutAnchor anchor;SubtitleCutPastePresentation presentation;
            const SubtitleBoxPadding padding{12,18,10};
            SubtitleCutPasteGeometry first;
            for(int frame=0;frame<40;++frame) {
                const int extra=frame<30?frame*2:0;
                text.sourcePanel=text.capturePanel={190,290,410+extra,315};text.capturePanelMeasured=true;
                text.bounds=text.lineBounds[0]=top?SubtitleBoxRect{200,30,400,65}:SubtitleBoxRect{200,295,400,330};
                if(top)text.sourcePanel=text.capturePanel={190,45,410+extra,70};
                p.current.identity.acceptedSequence=frame+1;p.current.text=text;
                auto fresh=presentation.Consume(text,p,padding,15);
                fresh=ApplySubtitleTextReduction(FitSubtitleToVisiblePicture(fresh,{0,45,640,315},15),
                    reduction,padding,true,floating);
                const auto result=anchor.Consume(fresh,text,p,padding,15,floating);
                Assert::IsTrue(result.valid,(L"top="+std::to_wstring(top)+L" floating="+std::to_wstring(floating)+L" reduction="+std::to_wstring(reduction)+L" frame="+std::to_wstring(frame)).c_str());
                if(frame==0)first=result;
                Assert::AreEqual(first.glyphTranslateX,result.glyphTranslateX);
                Assert::AreEqual(first.glyphTranslateY,result.glyphTranslateY);
                Assert::AreEqual(first.glyphScale,result.glyphScale);
                Assert::AreEqual(fresh.source.right,result.source.right,L"capture must not freeze with placement");
                Assert::AreEqual(fresh.pictureCapture.right,result.pictureCapture.right);
                Assert::AreEqual(fresh.generatedCleanup.right,result.generatedCleanup.right);
                Assert::IsTrue(SubtitleBoxLookahead::SameLine(first.destination,result.destination,0),
                    L"same glyphs keep a glyph-sized display envelope as backing measurements vary");
                Assert::IsTrue(result.generatedCleanup.Valid() &&
                    result.generatedCleanup.right==fresh.generatedCleanup.right,
                    L"cleanup remains authorized by the current measured card independently of display placement");
            }
        }
    }
    TEST_METHOD(FinalAnchorIgnoresUnconfirmedLayoutJitterButReleasesOnExplicitChanges) {
        auto p=Preview();auto text=Text();SubtitleLayoutAnchor anchor;
        auto first=ComputeSubtitleCutPaste(text.bounds,640,360,45,315,12,15);
        first=ApplySubtitleTextReduction(FitSubtitleToVisiblePicture(first,{0,45,640,315},15),25);
        const auto acquired=anchor.Consume(first,text,p,{},15);
        auto candidate=first;candidate.destination.right+=40;candidate.glyphTranslateX+=20;
        candidate.generatedCleanup={};
        auto held=anchor.Consume(candidate,text,p,{},15);
        Assert::AreEqual(acquired.glyphTranslateX,held.glyphTranslateX);
        Assert::AreEqual(acquired.destination.right,held.destination.right);
        for(int change=0;change<7;++change) {
            SubtitleLayoutAnchor freshAnchor;freshAnchor.Consume(first,text,p,{},15);
            auto changedText=text;auto changedPreview=p;auto next=candidate;int offset=15;bool floating=false;
            if(change==0)changedText.revised=true;
            if(change==1)++changedText.cue;
            if(change==2)++changedPreview.policyGeneration;
            if(change==3)next.placementLimits.left+=10;
            if(change==4)++offset;
            if(change==5)next.glyphScale=0.5f;
            if(change==6)floating=true;
            auto accepted=freshAnchor.Consume(next,changedText,changedPreview,{},offset,floating);
            Assert::AreEqual(next.glyphTranslateX,accepted.glyphTranslateX);
        }
    }
    TEST_METHOD(FinalAnchorBackgroundIntrusionDoesNotRecenterFloatingText) {
        auto p=Preview();auto text=Text();text.bounds={200,320,400,340};
        SubtitleLayoutAnchor anchor;SubtitleBoxRect empty;
        auto first=ComputeSubtitleCutPaste(text.bounds,640,360,45,315,12,15,&empty);
        first=ApplySubtitleTextReduction(FitSubtitleToVisiblePicture(first,{0,45,640,315},15),25,{},true,true);
        const auto acquired=anchor.Consume(first,text,p,{},15,true);
        Assert::IsFalse(acquired.extendToBar);
        auto candidate=first;candidate.generatedCleanup={190,300,440,315};candidate.extendToBar=true;
        candidate.glyphTranslateX+=20;
        const auto held=anchor.Consume(candidate,text,p,{},15,true);
        Assert::IsTrue(held.extendToBar);
        Assert::AreEqual(acquired.glyphTranslateX,held.glyphTranslateX);
        Assert::IsTrue(held.destination.right>=452,
            L"confirmed cleanup intrusion expands the background with a margin, without moving glyphs");
        Assert::AreEqual(440,held.generatedCleanup.right,
            L"intrusion proof still grants the exact wider cleanup footprint");
    }
    TEST_METHOD(MeasuredCardOverlayCoversLoggedThreeLineCleanupAtEveryReduction) {
        // Paused UHD regression: cleanup=1229,1495-2614,1884, but the
        // displayed panel was only 1394,1503-2442,1870 after 25% reduction.
        for(bool top:{false,true})for(int percent:{0,25,50,75})for(bool rounded:{false,true}) {
            const auto mirror=[](SubtitleBoxRect r){return SubtitleBoxRect{r.left,2160-r.bottom,r.right,2160-r.top};};
            const SubtitleBoxRect card=top?mirror({1229,1495,2614,1884}):SubtitleBoxRect{1229,1495,2614,1884};
            const SubtitleBoxRect ink=top?mirror({1250,1520,2580,1950}):SubtitleBoxRect{1250,1520,2580,1950};
            const SubtitleBoxRect visible=top?mirror({0,268,3840,1892}):SubtitleBoxRect{0,268,3840,1892};
            auto base=ComputeSubtitleCutPaste(ink,3840,2160,276,1884,{20,30,20},22,&card,0,&card);
            base=FitSubtitleToVisiblePicture(base,visible,22);Assert::IsTrue(base.valid);
            auto fallback=base;fallback.cleanupLimit=card;
            const auto glyphOnly=ApplySubtitleTextReduction(fallback,percent,{20,30,20},rounded,true);
            const auto out=ApplySubtitleTextReduction(base,percent,{20,30,20},rounded,true);
            const int margin=rounded?12:2;
            Assert::IsTrue(out.valid && out.extendToBar);
            Assert::IsTrue(out.destination.left<=card.left-margin && out.destination.right>=card.right+margin);
            Assert::IsTrue(top?out.destination.bottom>=card.bottom+margin:out.destination.top<=card.top-margin);
            Assert::AreEqual(glyphOnly.glyphTranslateX,out.glyphTranslateX);
            Assert::AreEqual(glyphOnly.glyphTranslateY,out.glyphTranslateY);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(base.generatedCleanup,out.generatedCleanup,0));
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(base.pictureCapture,out.pictureCapture,0));
            if(percent==75)Assert::IsTrue(glyphOnly.destination.right-glyphOnly.destination.left<out.destination.right-out.destination.left,
                L"ambiguous local proof uses glyph padding; measured cards retain full native coverage");
        }
    }
    TEST_METHOD(FreshCardCoverageCanGrowWithoutMovingAnchoredGlyphs) {
        for(bool top:{false,true}) {
            auto p=Preview();auto text=Text();text.lineCount=1;text.lineBounds[0]=text.bounds;
            auto g=ComputeSubtitleCutPaste(text.bounds,640,360,45,315,10,15);
            if(top) {
                text.bounds=text.lineBounds[0]=MirrorSubtitleRect(text.bounds);
                g=ComputeSubtitleCutPaste(text.bounds,640,360,45,315,10,15);
            }
            g.layoutCoverage={};g.generatedCleanup={};
            g=ApplySubtitleTextReduction(g,50,10,true,false);
            SubtitleLayoutAnchor anchor;
            const auto first=anchor.Consume(g,text,p,10,15,false);
            auto fresh=g;fresh.layoutCoverage=fresh.generatedCleanup=top?SubtitleBoxRect{160,45,460,90}:SubtitleBoxRect{160,270,460,315};
            fresh.glyphTranslateX+=5;fresh.glyphTranslateY+=3;
            const auto covered=anchor.Consume(fresh,text,p,10,15,false);
            Assert::AreEqual(first.glyphTranslateX,covered.glyphTranslateX);
            Assert::AreEqual(first.glyphTranslateY,covered.glyphTranslateY);
            Assert::IsTrue(covered.destination.left<=148 && covered.destination.right>=472);
            Assert::IsTrue(top?covered.destination.bottom>=102:covered.destination.top<=258);
            auto local=fresh;local.cleanupLimit=local.generatedCleanup;
            local.layoutCoverage={0,45,640,315};
            const auto held=anchor.Consume(local,text,p,10,15,false);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(covered.destination,held.destination,0),
                L"unknown outer boundaries must neither grow nor shrink the established panel");
            Assert::AreEqual(first.glyphTranslateY,held.glyphTranslateY);
        }
    }
    TEST_METHOD(CoverageMarginProtectsMaximumRadiusAtViewportSides) {
        for(bool top:{false,true})for(bool right:{false,true}) {
            SubtitleCutPasteGeometry g;g.valid=true;g.fromTopBar=top;
            g.pictureTop=100;g.pictureBottom=2060;g.placementLimits={0,100,1920,2060};
            g.destination=top?SubtitleBoxRect{300,130,700,420}:SubtitleBoxRect{300,1740,700,2030};
            g.layoutCoverage=top?SubtitleBoxRect{0,100,1000,460}:SubtitleBoxRect{0,1700,1000,2060};
            if(right) {
                g.layoutCoverage={920,g.layoutCoverage.top,1920,g.layoutCoverage.bottom};
                g.destination={1220,g.destination.top,1620,g.destination.bottom};
            }
            const auto out=CoverMeasuredSubtitleBacking(g);
            const float radius=(std::min)(28.0f,0.18f*(std::min)(
                out.destination.right-out.destination.left,out.destination.bottom-out.destination.top));
            Assert::AreEqual(28.0f,radius,L"exercise the maximum native shader corner radius");
            // Exact opaque-mask condition at the outermost visible card corner;
            // use the continuous edge (stricter than the first pixel center).
            const float sideDistance=right?out.destination.right+6.0f-g.layoutCoverage.right:
                g.layoutCoverage.left-(out.destination.left-6.0f);
            const float verticalDistance=top?out.destination.bottom-g.layoutCoverage.bottom:
                g.layoutCoverage.top-out.destination.top;
            const float dx=(std::max)(0.0f,radius-sideDistance);
            const float dy=(std::max)(0.0f,radius-verticalDistance);
            Assert::IsTrue(std::sqrt(dx*dx+dy*dy)-radius<=-0.75f,
                L"selected-viewport clipping must not expose repaired card through rounded antialiasing");
        }
    }
    TEST_METHOD(MeasuredCoverageClipsOnlyDisplayAtVisibleViewport) {
        auto g=ComputeSubtitleCutPaste({200,300,440,335},640,360,45,315,10,0);
        g=FitSubtitleToVisiblePicture(g,{220,70,620,290});
        g.layoutCoverage=g.generatedCleanup={180,260,640,315};
        const auto out=ApplySubtitleTextReduction(g,75,10,true,false);
        Assert::IsTrue(out.valid && out.destination.left==220 && out.destination.right==620);
        Assert::AreEqual(248,out.destination.top);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(g.generatedCleanup,out.generatedCleanup,0),
            L"covering only the visible portion cannot rewrite source cleanup authority");
    }
    TEST_METHOD(TextReductionKeepsSourceAuthorityAndPaddingAtEitherEdge) {
        const SubtitleBoxPadding padding{12,18,10};
        auto sameRect=[](const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
            return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
        };
        for(bool top:{false,true})for(int kind:{0,1,2})for(int gap:{0,15}) {
            SubtitleBoxRect content=kind==0?SubtitleBoxRect{200,320,440,340}:
                kind==1?SubtitleBoxRect{200,300,440,330}:SubtitleBoxRect{200,287,440,305};
            SubtitleBoxRect card=kind==0?SubtitleBoxRect{}:
                kind==1?SubtitleBoxRect{190,292,450,315}:SubtitleBoxRect{190,282,450,309};
            if(top) {
                content={content.left,360-content.bottom,content.right,360-content.top};
                if(card.Valid())card={card.left,360-card.bottom,card.right,360-card.top};
            }
            auto base=ComputeSubtitleCutPaste(content,640,360,45,315,padding,gap,&card,20,&card,true);
            base.glyphLines[0]=content;
            base=FitSubtitleToVisiblePicture(base,{20,47,620,313},gap);
            Assert::IsTrue(base.valid);
            for(int percent:{0,25,50,75}) {
                const auto scaled=ApplySubtitleTextReduction(base,percent,padding);
                Assert::IsTrue(scaled.valid);Assert::AreEqual(float(100-percent)/100.0f,scaled.glyphScale);
                Assert::IsTrue(sameRect(base.source,scaled.source) && sameRect(base.content,scaled.content) &&
                    sameRect(base.pictureCapture,scaled.pictureCapture) && sameRect(base.generatedCleanup,scaled.generatedCleanup) &&
                    sameRect(base.glyphLines[0],scaled.glyphLines[0]),
                    L"display reduction cannot alter capture or original-card cleanup");
                Assert::AreEqual(base.pictureTop,scaled.pictureTop);Assert::AreEqual(base.pictureBottom,scaled.pictureBottom);
                const float left=scaled.glyphScale*content.left+scaled.glyphTranslateX;
                const float right=scaled.glyphScale*content.right+scaled.glyphTranslateX;
                const float above=scaled.glyphScale*content.top+scaled.glyphTranslateY;
                const float below=scaled.glyphScale*content.bottom+scaled.glyphTranslateY;
                Assert::IsTrue(left>=scaled.destination.left && right<=scaled.destination.right &&
                    above>=scaled.destination.top && below<=scaled.destination.bottom);
                Assert::IsTrue(above-scaled.destination.top>=padding.top-0.01f &&
                    scaled.destination.bottom-below>=padding.bottom-0.01f,
                    L"absolute headroom and bar-facing padding survive text reduction");
                Assert::IsTrue(scaled.destination.left>=20 && scaled.destination.right<=620 &&
                    scaled.destination.top>=47 && scaled.destination.bottom<=313);
                if(card.Valid()) {
                    const auto& cleanup=scaled.generatedCleanup;
                    Assert::IsTrue(cleanup.Valid() && scaled.extendToBar,
                        L"original picture cleanup remains independently authorized after glyph reduction");
                }
                Assert::IsTrue(sameRect(base.generatedCleanup,scaled.generatedCleanup),
                    L"glyph-size changes preserve the exact source cleanup authority");
                if(!card.Valid()) {
                    Assert::IsTrue(std::abs((left+right)-(scaled.destination.left+scaled.destination.right))<0.01f &&
                        std::abs((above+below)-(scaled.destination.top+scaled.destination.bottom))<0.01f,
                        L"bar-only glyphs remain centered inside the padded box");
                } else {
                    Assert::IsTrue(scaled.destination.left<=card.left-12 && scaled.destination.right>=card.right+12,
                        L"reduced glyphs must not shrink the overlay inside the accepted native card");
                    Assert::IsTrue(top?scaled.destination.bottom>=card.bottom+12:scaled.destination.top<=card.top-12,
                        L"picture-facing overlay covers the original cleanup with a margin");
                }
            }
        }
    }
    TEST_METHOD(TextReductionUsesFrozenEnvelopeRatherThanFreshInkExtrema) {
        const SubtitleBoxRect card{180,280,460,315};
        auto base=ComputeSubtitleCutPaste({200,300,440,335},640,360,45,315,{20,20,10},15,&card,0,&card);
        const auto a=ApplySubtitleTextReduction(base,75,{20,20,10});
        auto corrected=base;corrected.content.left-=2;corrected.content.top-=1;
        corrected.glyphLines[0]=corrected.content;
        const auto b=ApplySubtitleTextReduction(corrected,75,{20,20,10});
        Assert::AreEqual(a.glyphTranslateX,b.glyphTranslateX);Assert::AreEqual(a.glyphTranslateY,b.glyphTranslateY);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(a.destination,b.destination,0));
        Assert::AreEqual(0.25f,ApplySubtitleTextReduction(base,100,{20,20,10}).glyphScale);
        Assert::AreEqual(1.0f,ApplySubtitleTextReduction(base,-1,{20,20,10}).glyphScale);
        const auto cropped=FitSubtitleToVisiblePicture(base,{220,70,620,290},15);
        const auto reduced=ApplySubtitleTextReduction(cropped,75,{20,20,10});
        Assert::IsTrue(reduced.valid);Assert::IsTrue(reduced.destination.left>=220 && reduced.destination.bottom<=290);
        Assert::AreEqual(cropped.generatedCleanup.left,reduced.generatedCleanup.left,
            L"viewport clipping constrains display only, never source cleanup");
    }
    TEST_METHOD(FloatingShapeMirrorsBothBarsAndPreservesOffsetAtEveryScale) {
        for(bool top:{false,true})for(bool rounded:{false,true})for(bool floating:{false,true})
        for(int reduction:{0,25,50,75})for(int gap:{0,15,35}) {
            const SubtitleBoxRect ink=top?SubtitleBoxRect{200,12,430,34}:SubtitleBoxRect{200,326,430,348};
            const SubtitleBoxRect empty{};
            auto g=ComputeSubtitleCutPaste(ink,640,360,45,315,{12,18,7},gap,&empty,0,&empty);
            g=FitSubtitleToVisiblePicture(g,{0,45,640,315},gap);
            g=ApplySubtitleTextReduction(g,reduction,{12,18,7},rounded,floating);
            Assert::IsTrue(g.valid && g.centeredGlyphMapping);
            Assert::AreEqual(rounded,g.roundedCorners);Assert::AreEqual(!floating,g.extendToBar);
            Assert::AreEqual(gap,top?g.destination.top-45:315-g.destination.bottom);
            const float cy=(ink.top+ink.bottom)*g.glyphScale+2*g.glyphTranslateY;
            Assert::IsTrue(std::abs(cy-g.destination.top-g.destination.bottom)<0.01f);
        }
    }
    TEST_METHOD(PictureIntrusionDisablesFloatUntilCueChanges) {
        auto p=Preview();auto text=Text();text.sourcePanel={190,287,410,315};
        SubtitleCutPastePresentation presentation;
        auto crossing=presentation.Consume(text,p,{10,10,10},15);
        Assert::IsTrue(crossing.valid && crossing.pictureIntrusion);
        text.bounds={200,322,400,340};text.sourcePanel={};
        auto weaker=presentation.Consume(text,p,{10,10,10},15);
        Assert::IsTrue(weaker.valid && weaker.pictureIntrusion);
        auto placed=ApplySubtitleTextReduction(weaker,75,{10,10,10},true,true);
        Assert::IsTrue(placed.valid && placed.extendToBar);
        ++text.cue;
        auto fresh=presentation.Consume(text,p,{10,10,10},15);
        placed=ApplySubtitleTextReduction(fresh,75,{10,10,10},true,true);
        Assert::IsTrue(placed.valid && !placed.extendToBar);
    }
    TEST_METHOD(CenteredBoxDoesNotBreatheWhenCardMeasurementShrinks) {
        auto p=Preview();auto text=Text();text.bounds={200,310,400,330};text.sourcePanel={185,275,415,315};
        SubtitleCutPastePresentation presentation;
        auto first=presentation.Consume(text,p,{10,12,6},15);
        auto a=ApplySubtitleTextReduction(first,75,{10,12,6},true,true);
        text.sourcePanel={195,290,405,315};
        auto second=presentation.Consume(text,p,{10,12,6},15);
        auto b=ApplySubtitleTextReduction(second,75,{10,12,6},true,true);
        Assert::IsTrue(a.valid && b.valid);
        Assert::AreEqual(a.glyphTranslateX,b.glyphTranslateX);Assert::AreEqual(a.glyphTranslateY,b.glyphTranslateY);
        Assert::AreEqual(a.destination.top,b.destination.top);Assert::AreEqual(a.destination.bottom,b.destination.bottom);
        Assert::AreEqual(275,second.generatedCleanup.top,L"an unchanged opaque cue retains its acquired cleanup footprint");
    }
    TEST_METHOD(FreshCrossingInkOverridesFloatEvenWithFrozenBarOnlyDisplay) {
        auto p=Preview();auto text=Text();text.bounds={200,323,400,340};text.sourcePanel={};
        const auto frozen=text.bounds;SubtitleCutPastePresentation presentation;
        auto first=presentation.Consume(text,p,{10,10,10},15,&frozen);
        Assert::IsFalse(first.pictureIntrusion);
        text.lineCount=1;text.lineBounds[0]={200,311,400,340};
        auto next=presentation.Consume(text,p,{10,10,10},15,&frozen);
        const auto g=ApplySubtitleTextReduction(next,0,{10,10,10},true,true);
        Assert::IsTrue(g.valid && g.pictureIntrusion && g.extendToBar);
        Assert::IsFalse(g.generatedCleanup.Valid(),L"display extension does not grant cleanup authority");
    }
    TEST_METHOD(CenteredLargeCueKeepsLargestFittingSymmetricMargins) {
        SubtitleCutPasteGeometry g;g.valid=true;g.source={50,50,250,311};g.content={60,90,240,290};
        g.layoutContent=g.content;g.destination={50,45,250,306};g.placementLimits={0,45,640,315};
        g.pictureTop=45;g.pictureBottom=315;
        const auto out=ApplySubtitleTextReduction(g,0,{10,40,21},false,true);
        Assert::IsTrue(out.valid);
        Assert::AreEqual(45,out.destination.top);Assert::AreEqual(306,out.destination.bottom);
        const float t=90+out.glyphTranslateY,b=290+out.glyphTranslateY;
        Assert::IsTrue(std::abs((t-out.destination.top)-(out.destination.bottom-b))<0.01f);
    }
    TEST_METHOD(DetectedHiddenBlackCardEligibilityReachesPlacementWithoutGrowingCapture) {
        constexpr int width=640,height=360;
        for(bool top:{false,true}) {
            std::vector<uint16_t> pixels(width*height*3/2,uint16_t(512<<6));
            for(int y=0;y<height;++y)for(int x=0;x<width;++x)pixels[y*width+x]=uint16_t(64<<6);
            auto fill=[&](int l,int t,int r,int b,int value) {
                for(int y=t;y<b;++y)for(int x=l;x<r;++x)pixels[y*width+x]=uint16_t(value<<6);
            };
            // Opaque caption whose backing edge is invisible on black scenery.
            for(int n=0;n<21;++n) {
                const int x=180+n*13;fill(x,272,x+8,286,510);fill(x+2,274,x+6,284,64);
            }
            if(top)for(int y=0;y<height/2;++y)for(int x=0;x<width;++x)
                std::swap(pixels[y*width+x],pixels[(height-1-y)*width+x]);
            AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels.data());
            source.dataBytes=pixels.size()*2;source.width=width;source.height=height;
            source.rowBytes=source.chromaRowBytes=width*2;source.format=AnalysisLumaFormat::P010;source.generation=1;
            SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
            auto text=detector.Analyze(source,45,315,1,1);
            Assert::IsTrue(text.detected && text.nearBarEligibilityMeasured && text.nearBarEligible);
            Assert::IsTrue(text.diagnosticNearBarGapInferred);
            auto preview=Preview();preview.current.nearBarDistance=20;preview.current.text=text;
            SubtitleCutPastePresentation placement;
            const auto geometry=placement.Consume(text,preview,SubtitleBoxPadding(4),5);
            Assert::IsTrue(geometry.valid,L"accepted inferred backing must not be rejected again by cleanup geometry");
            Assert::AreEqual(top,geometry.fromTopBar);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(text.capturePanel,geometry.pictureCapture,0),
                L"placement authority must not enlarge extraction authority");
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(text.sourcePanel,geometry.generatedCleanup,0),
                L"inferred proximity must not expand cleanup through connected scenery");
            ++preview.current.identity.acceptedSequence;text.nearBarEligible=false;
            Assert::IsFalse(placement.Consume(text,preview,SubtitleBoxPadding(40),5).valid,
                L"display padding and cached geometry cannot override a detector rejection");
        }
    }
    TEST_METHOD(NativeCardBoundaryProofPreservesCachedPlacementAcrossSampledEdgeJitter) {
        auto p=Preview();auto t=Text();
        t.sourcePanel={190,290,410,315};t.capturePanel={194,294,406,317};t.capturePanelMeasured=true;
        SubtitleCutPastePresentation placement;
        const auto first=placement.Consume(t,p,SubtitleBoxPadding(10));
        Assert::IsTrue(first.valid);Assert::AreEqual(317,first.pictureCapture.bottom);
        ++p.current.identity.acceptedSequence;p.current.pictureBottom=316;
        const auto second=placement.Consume(t,p,SubtitleBoxPadding(10));
        Assert::AreEqual(first.destination.bottom,second.destination.bottom,
            L"unchanged native proof must not defeat frozen placement when a sampled edge jitters");
        Assert::AreEqual(first.pictureBottom,second.pictureBottom);
        Assert::AreEqual(317,second.pictureCapture.bottom);
        ++p.current.identity.acceptedSequence;t.capturePanel.bottom=318;
        const auto changed=placement.Consume(t,p,SubtitleBoxPadding(10));
        Assert::AreEqual(318,changed.pictureCapture.bottom,
            L"new native boundary proof must still invalidate the cached capture geometry");
    }
    TEST_METHOD(MeasuredCardInteriorDoesNotInheritCleanupFringeAndInvalidatesCache) {
        auto p=Preview();auto t=Text();
        t.sourcePanel={180,270,420,315};t.capturePanel={190,280,410,310};t.capturePanelMeasured=true;
        t.lineCount=1;t.lineBounds[0]={200,295,400,330};
        SubtitleCutPastePresentation placement;
        const auto first=placement.Consume(t,p,SubtitleBoxPadding(20));
        Assert::IsTrue(first.valid);Assert::AreEqual(180,first.generatedCleanup.left);
        Assert::AreEqual(190,first.pictureCapture.left);Assert::AreEqual(310,first.pictureCapture.bottom);
        ++p.current.identity.acceptedSequence;t.capturePanel.right=405;
        const auto changed=placement.Consume(t,p,SubtitleBoxPadding(20));
        Assert::AreEqual(405,changed.pictureCapture.right,L"interior-only changes must invalidate geometry");
        ++p.current.identity.acceptedSequence;t.capturePanel={};
        const auto empty=placement.Consume(t,p,SubtitleBoxPadding(20));
        Assert::IsFalse(empty.pictureCapture.Valid(),L"an explicitly empty interior cannot fall back to cleanup fringe");
        Assert::IsTrue(empty.generatedCleanup.Valid());
    }
    TEST_METHOD(PhysicalEdgeReconciliationDoesNotPromotePictureSliversToBarGlyphs) {
        const SubtitleBoxRect card{190,280,410,315};
        const auto bottom=ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315,10,0,&card,0,&card);
        const auto fitted=FitSubtitleToVisiblePicture(bottom,{0,43,640,318},5);
        Assert::IsTrue(fitted.valid);Assert::AreEqual(318,fitted.pictureBottom);Assert::AreEqual(43,fitted.pictureTop);
        Assert::AreEqual(315,fitted.pictureCapture.bottom,L"edge agreement is not new black-card authority");
        Assert::AreEqual(313,fitted.destination.bottom);
        Assert::AreEqual(315,bottom.pictureBottom,L"the detector geometry is not modified");
        const SubtitleBoxRect topCard{190,45,410,80};
        const auto top=ComputeSubtitleCutPaste({200,20,400,60},640,360,45,315,10,0,&topCard,0,&topCard);
        const auto mirrored=FitSubtitleToVisiblePicture(top,{0,42,640,315},5);
        Assert::IsTrue(mirrored.valid);Assert::AreEqual(42,mirrored.pictureTop);
        Assert::AreEqual(45,mirrored.pictureCapture.top);Assert::AreEqual(47,mirrored.destination.top);
        const auto cropped=FitSubtitleToVisiblePicture(bottom,{0,70,640,290},5);
        Assert::IsTrue(cropped.valid);Assert::AreEqual(315,cropped.pictureBottom);
        Assert::AreEqual(45,cropped.pictureTop,L"a real crop only changes destination placement");
    }
    TEST_METHOD(AttachedCleanupFollowsPhysicalEdgeWithoutExpandingGlyphCapture) {
        for(int delta=-3;delta<=3;++delta) {
            const SubtitleBoxRect card{190,280,410,315};
            const auto base=ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315,10,0,&card,0,&card);
            const auto fitted=FitSubtitleToVisiblePicture(base,{0,45,640,315+delta},5);
            Assert::IsTrue(fitted.valid);
            Assert::AreEqual(315+delta,fitted.generatedCleanup.bottom);
            Assert::AreEqual(315,fitted.pictureCapture.bottom);
            const SubtitleBoxRect topCard{190,45,410,80};
            const auto top=ComputeSubtitleCutPaste({200,20,400,60},640,360,45,315,10,0,&topCard,0,&topCard);
            const auto mirrored=FitSubtitleToVisiblePicture(top,{0,45+delta,640,315},5);
            Assert::AreEqual(45+delta,mirrored.generatedCleanup.top);
            Assert::AreEqual(45,mirrored.pictureCapture.top);
        }
        const SubtitleBoxRect finite{190,280,410,310};
        const auto base=ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315,10,0,&finite,0,&finite);
        Assert::AreEqual(310,FitSubtitleToVisiblePicture(base,{0,45,640,318},5).generatedCleanup.bottom,
            L"a finite card's deliberate gap must not become edge-attached");
        Assert::AreEqual(315,FitSubtitleToVisiblePicture(
            ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315,10),{0,70,640,290},5).generatedCleanup.bottom,
            L"a real viewport crop must not change source cleanup");
    }
    TEST_METHOD(CurrentCaptureBeyondFrozenDisplayPaddingGrowsOnceWithoutClipping) {
        auto p=Preview();auto t=Text();t.bounds={196,294,404,336};
        t.lineCount=1;t.lineBounds[0]={200,300,400,330};p.current.text=t;
        SubtitlePaddingGuard guard;SubtitleCutPastePresentation placement;
        const auto pad=guard.Consume(t,p,SubtitleBoxPadding(0),SubtitlePaddingPolicy{true});
        auto display=guard.DisplayBounds();const auto first=placement.Consume(t,p,pad,0,&display);
        Assert::IsTrue(first.valid);
        ++p.current.identity.acceptedSequence;t.lineBounds[0].right=450;p.current.text=t;
        const auto samePad=guard.Consume(t,p,SubtitleBoxPadding(0),SubtitlePaddingPolicy{true});
        display=guard.DisplayBounds();const auto grown=placement.Consume(t,p,samePad,0,&display);
        Assert::IsTrue(grown.valid);Assert::IsTrue(grown.source.right>=grown.content.right);
        Assert::IsTrue(grown.glyphLines[0].right>=450);
        Assert::AreEqual(first.source.left-first.destination.left,grown.source.left-grown.destination.left,
            L"horizontal expansion must not shift the existing glyphs");
        Assert::AreEqual(first.source.top-first.destination.top,grown.source.top-grown.destination.top);
        ++p.current.identity.acceptedSequence;t.lineBounds[0].right=400;
        const auto retained=placement.Consume(t,p,samePad,0,&display);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(grown.destination,retained.destination,0),
            L"a corrected current capture cannot make the panel shrink on the next measurement");
    }
    TEST_METHOD(MinimumHeadroomUsesFrozenGlyphTopWithoutExpandingCapture) {
        auto p=Preview();auto t=Text();t.lineCount=1;t.lineBounds[0]=t.bounds;p.current.text=t;
        SubtitlePaddingPolicy policy{true};policy.minimumHeadroomPixels=30;
        SubtitlePaddingGuard guard;SubtitleCutPastePresentation presentation;
        const auto pad=guard.Consume(t,p,SubtitleBoxPadding(0),policy);
        auto display=guard.DisplayBounds();
        auto g=presentation.Consume(t,p,pad,0,&display);
        Assert::IsTrue(g.valid);Assert::IsTrue(g.content.top-g.source.top>=30);
        const auto originalContent=g.content;const auto originalTop=g.destination.top;
        ++p.current.identity.acceptedSequence;t.lineBounds[0].top+=2;t.bounds.top+=2;
        const auto nextPad=guard.Consume(t,p,SubtitleBoxPadding(0),policy);display=guard.DisplayBounds();
        const auto next=presentation.Consume(t,p,nextPad,0,&display);
        Assert::AreEqual(originalTop,next.destination.top,L"measurement jitter must not shrink the displayed panel");
        Assert::IsTrue(next.content.top>=originalContent.top,L"headroom is not extra glyph capture");
        std::string reason;
        Assert::IsTrue(RendererProfileConfig::ValidateProfileSetting("subtitles","minimum_headroom_pixels","200",reason));
        Assert::IsFalse(RendererProfileConfig::ValidateProfileSetting("subtitles","minimum_headroom_pixels","201",reason));
    }
    TEST_METHOD(SwitchingPaddingModeReleasesPreviousDisplayEnvelopeImmediately) {
        auto p=Preview();auto t=Text();t.bounds={180,285,420,335};
        t.lineCount=1;t.lineBounds[0]={200,295,400,330};p.current.text=t;
        SubtitleCutPastePresentation presentation;
        const auto fixed=presentation.Consume(t,p,SubtitleBoxPadding(0));
        const auto glyphs=SubtitleGlyphCaptureBounds(t,640,360);
        const auto relative=presentation.Consume(t,p,SubtitleBoxPadding(0),0,&glyphs);
        Assert::IsTrue(relative.valid);Assert::IsTrue(relative.source.left>fixed.source.left);
        Assert::AreEqual(glyphs.left,relative.source.left);
        const auto back=presentation.Consume(t,p,SubtitleBoxPadding(0));
        Assert::AreEqual(fixed.source.left,back.source.left);
    }
    TEST_METHOD(ConfirmedDetachedExtensionCanExpandDisplayBeforeRegrouping) {
        auto p=Preview();auto t=Text();t.lineCount=1;t.lineBounds[0]=t.bounds;p.current.text=t;
        SubtitlePaddingGuard guard;guard.Consume(t,p,{70,40,21},SubtitlePaddingPolicy{true});
        ++p.current.identity.acceptedSequence;t.bounds.right+=20;t.revised=true;
        guard.Consume(t,p,{70,40,21},SubtitlePaddingPolicy{true});
        Assert::AreEqual(t.bounds.right,guard.DisplayBounds().right);
    }
    TEST_METHOD(GlyphPaddingScalesWithLineHeightNotMultilineHeight) {
        for(int scale:{1,2,4}) for(int rows:{1,2,3}) {
            auto p=Preview();auto t=Text();t.bounds={100,60,500,300};t.lineCount=rows;
            for(int i=0;i<rows;++i)t.lineBounds[i]={120,60+i*90,460,60+i*90+20*scale};
            p.current.text=t;SubtitlePaddingGuard guard;
            const auto padding=guard.Consume(t,p,{70,40,21},SubtitlePaddingPolicy{true});
            Assert::AreEqual(20*scale,guard.GlyphHeight());
            Assert::AreEqual(15*scale,padding.sides);
            Assert::AreEqual(7*scale,padding.top);Assert::AreEqual(5*scale,padding.bottom);
        }
    }
    TEST_METHOD(DetachedMarksStayInDisplayBoundsWithoutDefiningFontScale) {
        auto p=Preview();auto t=Text();t.bounds={100,280,500,335};t.lineCount=3;
        t.lineBounds[0]={120,285,480,325};
        t.lineBounds[1]={105,330,115,334};t.lineBounds[2]={488,280,494,285};
        p.current.text=t;SubtitlePaddingGuard guard;
        const auto padding=guard.Consume(t,p,{70,40,21},SubtitlePaddingPolicy{true});
        Assert::AreEqual(40,guard.GlyphHeight());Assert::AreEqual(30,padding.sides);
        Assert::IsTrue(guard.DisplayBounds().left<=105 && guard.DisplayBounds().right>=494);
        Assert::IsTrue(guard.DisplayBounds().top<=280 && guard.DisplayBounds().bottom>=334);
    }
    TEST_METHOD(GlyphPaddingFreezesBoundsThroughNoisyHeightsAndHeldFrames) {
        auto p=Preview();auto t=Text();t.bounds={190,290,410,336};t.lineCount=1;
        t.lineBounds[0]={200,295,400,330};p.current.text=t;
        SubtitlePaddingGuard guard;SubtitleCutPastePresentation presentation;
        const auto initial=guard.Consume(t,p,{70,40,21},SubtitlePaddingPolicy{true});
        auto bounds=guard.DisplayBounds();
        const auto first=presentation.Consume(t,p,initial,0,&bounds);Assert::IsTrue(first.valid);
        for(int frame=0;frame<40;++frame) {
            ++p.current.identity.acceptedSequence;
            t.lineBounds[0]={200+frame%3,295+frame%2,400-frame%2,330-frame%3};
            t.held=frame%4==0;p.current.text=t;
            const auto padding=guard.Consume(t,p,{70,40,21},SubtitlePaddingPolicy{true});
            Assert::IsTrue(initial==padding);bounds=guard.DisplayBounds();
            const auto next=presentation.Consume(t,p,padding,0,&bounds);
            Assert::AreEqual(first.destination.left,next.destination.left);
            Assert::AreEqual(first.destination.top,next.destination.top);
            Assert::AreEqual(first.destination.right,next.destination.right);
            Assert::AreEqual(first.destination.bottom,next.destination.bottom);
        }
    }
    TEST_METHOD(ConfirmedTextExpansionGrowsBoxButKeepsAcquiredPadding) {
        auto p=Preview();auto t=Text();t.bounds={200,300,400,330};t.lineCount=1;t.lineBounds[0]=t.bounds;
        p.current.text=t;SubtitlePaddingGuard guard;
        const auto first=guard.Consume(t,p,{70,40,21},SubtitlePaddingPolicy{true});
        const auto old=guard.DisplayBounds();
        ++p.current.identity.acceptedSequence;t.bounds={150,245,450,330};t.lineCount=2;
        t.lineBounds[1]={150,245,450,290};t.revised=true;p.current.text=t;
        Assert::IsTrue(first==guard.Consume(t,p,{70,40,21},SubtitlePaddingPolicy{true}));
        const auto expanded=guard.DisplayBounds();
        Assert::IsTrue(expanded.top<old.top && expanded.left<old.left && expanded.right>old.right);
        t.revised=false;t.lineCount=1;t.lineBounds[0]={210,305,390,325};++p.current.identity.acceptedSequence;
        guard.Consume(t,p,{70,40,21},SubtitlePaddingPolicy{true});
        Assert::AreEqual(expanded.top,guard.DisplayBounds().top);
        Assert::AreEqual(expanded.right,guard.DisplayBounds().right);
        ++t.cue;++p.current.identity.acceptedSequence;p.current.text=t;
        const auto newCue=guard.Consume(t,p,{70,40,21},SubtitlePaddingPolicy{true});
        Assert::AreEqual(20,guard.GlyphHeight());Assert::AreEqual(15,newCue.sides);
        Assert::IsTrue(guard.DisplayBounds().top>expanded.top);
    }
    TEST_METHOD(GlyphMetricUsesQueuedSameCueAndRejectsFutureReplacement) {
        auto p=Preview();auto t=Text();t.bounds={200,295,400,335};t.lineCount=1;
        t.lineBounds[0]={200,300,400,330};t.lineSignatures[0].fill(80);p.current.text=t;
        p.followingCount=3;
        for(int i=0;i<3;++i) {p.following[i]=p.current;p.following[i].identity.acceptedSequence+=i+1;
            p.following[i].text.lineBounds[0].bottom=332;}
        SubtitlePaddingGuard guard;guard.Consume(t,p,{70,40,21},SubtitlePaddingPolicy{true});
        Assert::AreEqual(32,guard.GlyphHeight());
        guard.Reset();p.following[0].text.lineBounds[0]={20,180,600,280};
        guard.Consume(t,p,{70,40,21},SubtitlePaddingPolicy{true});
        Assert::AreEqual(30,guard.GlyphHeight());
        // Missing line metrics uses fixed fallback once; it must not jump when measurements return.
        guard.Reset();t.lineCount=0;p.current.text=t;p.followingCount=0;
        const auto fallback=guard.Consume(t,p,{70,40,21},SubtitlePaddingPolicy{true});
        Assert::IsTrue(fallback==SubtitleBoxPadding(70,40,21));
        t.lineCount=1;++p.current.identity.acceptedSequence;
        Assert::IsTrue(fallback==guard.Consume(t,p,{70,40,21},SubtitlePaddingPolicy{true}));
    }
    TEST_METHOD(GlyphPolicyChangesAndDiscontinuitiesResetTheFrozenMetrics) {
        auto p=Preview();auto t=Text();t.lineCount=1;t.lineBounds[0]={200,300,400,330};p.current.text=t;
        SubtitlePaddingGuard guard;SubtitlePaddingPolicy policy{true};
        guard.Consume(t,p,{70,40,21},policy);
        policy.sidesPercent=100;
        Assert::AreEqual(30,guard.Consume(t,p,{70,40,21},policy).sides);
        policy.glyphRelative=false;
        Assert::IsTrue(guard.Consume(t,p,{70,40,21},policy)==SubtitleBoxPadding(70,40,21));
        policy.glyphRelative=true;t.lineBounds[0].top=290;p.current.text=t;p.current.discontinuity=true;
        Assert::AreEqual(40,SubtitleTypicalLineHeight(t));
        guard.Consume(t,p,{70,40,21},policy);Assert::AreEqual(40,guard.GlyphHeight());
        p.available=false;guard.Consume(t,p,{70,40,21},policy);
        Assert::IsFalse(guard.DisplayBounds().Valid());
    }
    TEST_METHOD(GlyphPaddingDoesNotExpandCaptureOrCleanupAndStaysBounded) {
        auto p=Preview();auto t=Text();t.lineCount=1;t.lineBounds[0]={210,300,390,327};
        t.sourcePanel={190,288,410,315};p.current.text=t;
        SubtitlePaddingGuard guard;SubtitlePaddingPolicy policy{true};policy.sidesPercent=200;
        const auto padding=guard.Consume(t,p,{70,40,21},policy);const auto display=guard.DisplayBounds();
        SubtitleCutPastePresentation presentation;const auto g=presentation.Consume(t,p,padding,0,&display);
        Assert::IsTrue(g.valid);Assert::AreEqual(190,g.generatedCleanup.left);
        Assert::AreEqual(410,g.pictureCapture.right);
        Assert::AreEqual(327,g.content.bottom);Assert::IsTrue(g.source.left<g.content.left);
        const auto capped=SubtitleRelativePadding(INT_MAX,policy);
        Assert::AreEqual(500,capped.sides);
        policy.sidesPercent=policy.topPercent=policy.bottomPercent=0;
        Assert::IsTrue(SubtitleRelativePadding(80,policy)==SubtitleBoxPadding(0));
    }
    TEST_METHOD(GlyphPaddingConfigurationAcceptsBothModesAndBoundsPercentages) {
        using namespace RendererProfileConfig;
        for(const auto mode:{"glyph","fixed"}) {
            Assert::IsTrue(ValidateBaseSetting("subtitle_box_padding_mode",mode));
            Assert::IsTrue(ValidateCanonicalDisplaySetting("subtitle_box_padding_mode",mode));
        }
        Assert::IsFalse(ValidateBaseSetting("subtitle_box_padding_mode","automatic"));
        for(const auto key:{"subtitle_box_padding_sides_percent","subtitle_box_padding_top_percent","subtitle_box_padding_bottom_percent"}) {
            for(const auto good:{"0","35","75","200"}) {
                std::string reason;Assert::IsTrue(ValidateProfileSetting("display",key,good,reason));
                Assert::IsTrue(ValidateCanonicalDisplaySetting(key,good));
            }
            for(const auto bad:{"-1","201","nan","0.5","50px"})Assert::IsFalse(ValidateBaseSetting(key,bad));
        }
    }
    TEST_METHOD(GlyphCaptureExcludesCleanupFringeWithoutChangingPanelPlacement) {
        auto p=Preview();p.current.nearBarDistance=20;
        auto t=Text();t.bounds={190,270,450,315};t.sourcePanel={185,268,455,315};
        t.lineCount=2;t.lineBounds[0]={200,275,430,289};t.lineBounds[1]={193,296,442,307};
        SubtitleCutPastePresentation presentation;
        const auto g=presentation.Consume(t,p,SubtitleBoxPadding(20));
        Assert::IsTrue(g.valid);
        Assert::AreEqual(191,g.content.left);Assert::AreEqual(307,g.content.bottom);
        Assert::AreEqual(315,g.generatedCleanup.bottom);
        Assert::AreEqual(335,g.source.bottom);
        const auto wide=presentation.Consume(t,p,SubtitleBoxPadding(40));
        Assert::AreEqual(g.content.bottom,wide.content.bottom);
        Assert::AreEqual(g.content.left,wide.content.left);
    }
    TEST_METHOD(RelocationOverridesClassicHandlingThroughoutHotkeyCycle) {
        SubtitlePreviewState state;
        for(int repeat=0;repeat<3;++repeat) {
            Assert::IsFalse(SubtitlePreviewOverridesClassicHandling(state.Current()));
            Assert::IsTrue(SubtitlePreviewOverridesClassicHandling(state.Cycle()));
            Assert::IsTrue(SubtitlePreviewOverridesClassicHandling(state.Cycle()));
            Assert::IsFalse(SubtitlePreviewOverridesClassicHandling(state.Cycle()));
        }
        state.Cycle();state.ResetForSourceChange();
        Assert::IsFalse(SubtitlePreviewOverridesClassicHandling(state.Current()));
    }
    TEST_METHOD(GlyphLinesExcludeTrailingSamplingCellAndKeepDetachedPunctuation) {
        for(int width:{640,1920,2560,3840}) {
            SubtitleBoxResult text;text.bounds={90,90,600,280};text.lineCount=2;
            text.lineBounds[0]={100,100,580,150};text.lineBounds[1]={120,200,550,250};
            const auto lines=SubtitleGlyphCaptureLines(text,width,width*9/16);
            const int step=SubtitleBoxDetector::SamplingStep(width,width*9/16);
            Assert::AreEqual(100-step-1,lines[0].left);
            Assert::AreEqual(150,lines[0].bottom);Assert::AreEqual(250,lines[1].bottom);
            Assert::AreEqual(582,lines[0].right);
        }
    }
    TEST_METHOD(NearBarBackingCoversGapWithoutEnlargingGlyphCapture) {
        const SubtitleBoxRect panel{190,270,450,310};
        const auto g=ComputeSubtitleCutPaste({200,280,440,303},640,360,45,315,5,0,&panel,20);
        Assert::IsTrue(g.valid);Assert::AreEqual(315,g.generatedCleanup.bottom);
        Assert::AreEqual(310,g.pictureCapture.bottom);Assert::AreEqual(303,g.content.bottom);
        const SubtitleBoxRect upper{190,50,450,90};
        const auto top=ComputeSubtitleCutPaste({200,55,440,85},640,360,45,315,5,0,&upper,20);
        Assert::IsTrue(top.valid);Assert::AreEqual(45,top.generatedCleanup.top);
        Assert::AreEqual(50,top.pictureCapture.top);
    }
    TEST_METHOD(NearBarPlacementDoesNotPullGlyphsTowardEitherBar) {
        for(bool top:{false,true}) {
            const SubtitleBoxRect text=top?SubtitleBoxRect{200,55,440,75}:SubtitleBoxRect{200,280,440,300};
            const auto g=ComputeSubtitleCutPaste(text,640,360,45,315,SubtitleBoxPadding(5),0,&text,20);
            Assert::IsTrue(g.valid);
            Assert::AreEqual(g.source.top,g.destination.top);
            const auto fitted=FitSubtitleToVisiblePicture(g,{0,45,640,315},0);
            Assert::AreEqual(fitted.source.top,fitted.destination.top);
            Assert::AreEqual(top?45:315,top?fitted.destination.top:fitted.destination.bottom);
            Assert::AreEqual(text.top,g.content.top);
            Assert::AreEqual(text.bottom,g.content.bottom);
        }
        // Logged 4K near-bar caption used to be pulled down 21 source pixels.
        const SubtitleBoxRect text{1446,1970,2386,2050},panel{1410,1948,2418,2080};
        const auto g=ComputeSubtitleCutPaste(text,3840,2160,68,2092,{70,40,21},0,&panel,20);
        Assert::IsTrue(g.valid);
        Assert::AreEqual(0,g.destination.top-g.source.top);
        Assert::AreEqual(2092,g.destination.bottom);
    }
    TEST_METHOD(DisplayWidthCoversMeasuredBackingWithoutExpandingGlyphOwnership) {
        const SubtitleBoxRect text{200,295,400,330},panel{160,280,440,315};
        const auto g=ComputeSubtitleCutPaste(text,640,360,45,315,{5,5,5},0,&panel);
        Assert::IsTrue(g.valid);
        Assert::AreEqual(160,g.source.left);Assert::AreEqual(440,g.source.right);
        Assert::AreEqual(160,g.destination.left);Assert::AreEqual(440,g.destination.right);
        Assert::AreEqual(200,g.content.left);Assert::AreEqual(400,g.content.right);
    }
    TEST_METHOD(SameCueKeepsCleanupFootprintWhileCaptureUsesCurrentPanel) {
        auto p=Preview();auto r=Text();SubtitleCutPastePresentation state;
        r.sourcePanel={160,280,440,315};
        const auto initial=state.Consume(r,p,{5,5,5});
        r.sourcePanel={180,282,420,315};++p.current.identity.acceptedSequence;
        const auto narrower=state.Consume(r,p,{5,5,5});
        Assert::AreEqual(initial.destination.left,narrower.destination.left);
        Assert::AreEqual(initial.destination.right,narrower.destination.right);
        Assert::AreEqual(160,narrower.generatedCleanup.left);
        Assert::AreEqual(420,narrower.pictureCapture.right);
        Assert::AreEqual(initial.destination.top,narrower.destination.top);
        ++r.cue;++p.current.identity.acceptedSequence;
        const auto replacement=state.Consume(r,p,{5,5,5});
        Assert::AreEqual(180,replacement.destination.left);
        Assert::AreEqual(420,replacement.destination.right);
    }
    TEST_METHOD(NearBarPlacementSupportsEitherEdgeAndKeepsPhysicalBarBoundary) {
        for(bool top:{false,true}) {
            const SubtitleBoxRect text=top?SubtitleBoxRect{200,55,440,75}:SubtitleBoxRect{200,280,440,300};
            const auto moved=ComputeSubtitleCutPaste(text,640,360,45,315,SubtitleBoxPadding(10),0,&text,20);
            Assert::IsTrue(moved.valid);Assert::AreEqual(top,moved.fromTopBar);
            Assert::AreEqual(45,moved.pictureTop);Assert::AreEqual(315,moved.pictureBottom);
            Assert::IsFalse(ComputeSubtitleCutPaste(text,640,360,45,315,SubtitleBoxPadding(10),0,&text,0).valid);
        }
    }
    TEST_METHOD(SubtitlePreviewCycleAndResyncRestoreNormalConfiguration)
    {
        SubtitlePreviewState state;
        Assert::AreEqual(0,static_cast<int>(state.Current()));
        for(int repetition=0;repetition<2;++repetition) {
            Assert::AreEqual(3,static_cast<int>(state.Cycle()));
            Assert::AreEqual(2,static_cast<int>(state.Cycle()));
            Assert::AreEqual(0,static_cast<int>(state.Cycle()));
        }
        state.Cycle();state.ResetForSourceChange();
        Assert::AreEqual(0,static_cast<int>(state.Current()));
        Assert::AreEqual(3,static_cast<int>(state.Cycle()));
        state.Cycle();state.ResetForSourceChange();
        Assert::AreEqual(0,static_cast<int>(state.Current()));
    }
    TEST_METHOD(MovementModesKeepConfiguredAspectWhileGreenBoxUsesFullRaster) {
        Assert::IsFalse(SubtitlePreviewRequiresFullRaster(SubtitlePreviewMode::None));
        Assert::IsFalse(SubtitlePreviewRequiresFullRaster(SubtitlePreviewMode::BlackBackground));
        Assert::IsFalse(SubtitlePreviewRequiresFullRaster(SubtitlePreviewMode::GeneratedGrayBackground));
        Assert::IsTrue(SubtitlePreviewRequiresFullRaster(SubtitlePreviewMode::GreenBox));
    }
    TEST_METHOD(SubtitlePlacementFollowsChangingVisiblePictureWithoutChangingCapture) {
        const auto bottom=ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315,10);
        for(const auto visible:{SubtitleBoxRect{0,45,640,315},SubtitleBoxRect{0,70,640,290}}) {
            const auto fitted=FitSubtitleToVisiblePicture(bottom,visible,5);
            Assert::IsTrue(fitted.valid);Assert::AreEqual(visible.bottom-5,fitted.destination.bottom);
            Assert::AreEqual(bottom.source.top,fitted.source.top);
            Assert::AreEqual(bottom.source.bottom,fitted.source.bottom);
            Assert::AreEqual(bottom.generatedCleanup.bottom,fitted.generatedCleanup.bottom);
        }
        const auto top=ComputeSubtitleCutPaste({200,20,400,60},640,360,45,315,10);
        const auto fitted=FitSubtitleToVisiblePicture(top,{0,70,640,290},5);
        Assert::IsTrue(fitted.valid);Assert::AreEqual(75,fitted.destination.top);
        Assert::IsFalse(FitSubtitleToVisiblePicture(bottom,{0,100,640,120},5).valid);
        Assert::IsFalse(FitSubtitleToVisiblePicture(bottom,{250,45,350,315}).valid);
    }
    TEST_METHOD(SubtitlePreviewRequiresControlShiftT)
    {
        for(bool control:{false,true}) for(bool shift:{false,true}) for(bool alt:{false,true}) {
            Assert::AreEqual(control && shift && !alt,IsSubtitlePreviewShortcut('T',control,shift,alt));
            Assert::IsFalse(IsSubtitlePreviewShortcut('R',control,shift,alt));
        }
    }

    TEST_METHOD(ConfiguredSubtitleShortcutReplacesDefaultAndSupportsDisable) {
        Assert::IsTrue(IsSubtitlePreviewShortcut('Y',true,false,true,'Y',true,false,true));
        Assert::IsFalse(IsSubtitlePreviewShortcut('T',true,true,false,'Y',true,false,true));
        Assert::IsFalse(IsSubtitlePreviewShortcut('Y',true,true,true,'Y',true,false,true));
        Assert::IsFalse(IsSubtitlePreviewShortcut('T',true,true,false,0));
    }
    TEST_METHOD(ContentChangeInvalidatesCacheEvenWhenPaddingClipsToSameSource) {
        auto p=Preview();auto r=Text();SubtitleCutPastePresentation state;
        r.bounds={2,295,638,330};
        const auto first=state.Consume(r,p);Assert::IsTrue(first.valid);
        r.bounds.left=4;r.bounds.right=636;
        const auto next=state.Consume(r,p);Assert::IsTrue(next.valid);
        Assert::AreEqual(first.source.left,next.source.left);
        Assert::AreEqual(first.source.right,next.source.right);
        Assert::AreEqual(4,next.content.left);Assert::AreEqual(636,next.content.right);
        Assert::AreEqual(295,next.content.top);Assert::AreEqual(330,next.content.bottom);
    }
    TEST_METHOD(IndependentPaddingAndInsetChangesInvalidateCachedPlacement) {
        auto p=Preview();auto r=Text();SubtitleCutPastePresentation state;
        const auto initial=state.Consume(r,p);Assert::IsTrue(initial.valid);
        const auto wider=state.Consume(r,p,SubtitleBoxPadding(80,40,21),0);
        Assert::AreEqual(120,wider.source.left);Assert::AreEqual(480,wider.source.right);
        Assert::AreEqual(initial.destination.top,wider.destination.top);
        const auto taller=state.Consume(r,p,SubtitleBoxPadding(80,60,21),0);
        Assert::AreEqual(wider.destination.top-20,taller.destination.top);
        const auto bottom=state.Consume(r,p,SubtitleBoxPadding(80,60,31),0);
        Assert::AreEqual(360,bottom.source.bottom);
        Assert::AreEqual(taller.source.bottom+9,bottom.source.bottom);
        const auto inset=state.Consume(r,p,SubtitleBoxPadding(80,60,31),10);
        Assert::AreEqual(bottom.destination.bottom-10,inset.destination.bottom);
        Assert::AreEqual(200,r.bounds.left);Assert::AreEqual(295,r.bounds.top);
    }
    TEST_METHOD(AsymmetricPaddingClampsWithoutOverflowAndRejectsNegativeComponents) {
        auto r=ExpandSubtitleBox({2,3,638,359},640,360,SubtitleBoxPadding(INT_MAX,INT_MAX,0));
        Assert::AreEqual(0,r.left);Assert::AreEqual(0,r.top);
        Assert::AreEqual(640,r.right);Assert::AreEqual(359,r.bottom);
        for (const auto padding : {SubtitleBoxPadding(-1,0,0),SubtitleBoxPadding(0,-1,0),SubtitleBoxPadding(0,0,-1)}) {
            Assert::IsFalse(ExpandSubtitleBox({20,30,100,80},640,360,padding).Valid());
            Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315,padding).valid);
        }
    }
    TEST_METHOD(LegacyGeometryIsPreservedButIgnored) {
        for(const char* key:{"subtitle_box_padding_sides","subtitle_box_padding_top","subtitle_box_padding_bottom","subtitle_move_inset"}) {
            for(const char* value:{"0","30","60","500","-1","501","2.5","many"}) {
                char directory[MAX_PATH]{},path[MAX_PATH]{};
                Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
                Assert::IsTrue(GetTempFileNameA(directory,"vpp",0,path)!=0);
                {std::ofstream file(path);file<<"[vprenderer]\n"<<key<<": "<<value<<"\n";}
                ConfigFile config;Assert::IsTrue(config.Load(path));DeleteFileA(path);
                RendererProfileConfig::Model model;std::string error,actual;
                const bool expected=std::string(value)=="0" || std::string(value)=="30" ||
                    std::string(value)=="60" || std::string(value)=="500";
                Assert::IsTrue(RendererProfileConfig::Read(config,model,error));
                Assert::IsTrue(model.profiles.at("display.base").settings.count(key)==0);
                if(expected) {
                    Assert::IsTrue(RendererConfigView(config).TryGetDisplayString(key,actual));
                    Assert::AreEqual(std::string(value),actual);
                }
            }
        }
    }
    TEST_METHOD(PlacementFreezesBarFringeButResetsForNewCueOrContext) {
        auto p=Preview();auto r=Text();SubtitleCutPastePresentation state;
        const auto first=state.Consume(r,p);Assert::IsTrue(first.valid);
        p.current.pictureBottom=316;++p.current.identity.acceptedSequence;
        const auto stable=state.Consume(r,p);
        Assert::AreEqual(first.destination.bottom,stable.destination.bottom);
        Assert::AreEqual(315,stable.pictureBottom);
        ++r.cue;const auto next=state.Consume(r,p);
        Assert::AreEqual(316,next.destination.bottom);Assert::AreEqual(316,next.pictureBottom);
        p.current.pictureBottom=315;++p.current.identity.viewportGeneration;
        Assert::AreEqual(315,state.Consume(r,p).destination.bottom);
        p.current.pictureBottom=316;++p.policyGeneration;
        Assert::AreEqual(316,state.Consume(r,p).destination.bottom);
        p.current.pictureBottom=315;++p.continuityGeneration;
        Assert::AreEqual(315,state.Consume(r,p).destination.bottom);
    }
    TEST_METHOD(PlacementNeverSurvivesLostCueOrFreshBarAuthority) {
        auto p=Preview();auto r=Text();SubtitleCutPastePresentation state;
        Assert::IsTrue(state.Consume(r,p).valid);
        p.current.barAuthority=false;Assert::IsFalse(state.Consume(r,p).valid);
        p.current.barAuthority=true;p.current.pictureBottom=316;
        Assert::AreEqual(316,state.Consume(r,p).destination.bottom);
        r.detected=false;Assert::IsFalse(state.Consume(r,p).valid);
        r.detected=true;p.current.pictureBottom=315;
        Assert::AreEqual(315,state.Consume(r,p).destination.bottom);
        p.current.discontinuity=true;p.current.pictureBottom=316;
        Assert::AreEqual(316,state.Consume(r,p).destination.bottom);
    }
    TEST_METHOD(DefaultPaddingIncludesWideBlackPanelAndDetachedPunctuation) {
        const auto r=ExpandSubtitleBox({200,295,400,330},640,360);
        Assert::AreEqual(140,r.left);Assert::AreEqual(255,r.top);
        Assert::AreEqual(460,r.right);Assert::AreEqual(351,r.bottom);
    }
    TEST_METHOD(PaddingStopsBeforeNearbyUnclaimedGlyphRows) {
        SubtitleInkSnapshot ink;ink.width=32;ink.height=40;ink.step=2;
        ink.sourceRows.resize(ink.height);
        for(int y=0;y<ink.height;++y)ink.sourceRows[static_cast<size_t>(y)]=y*ink.step;
        ink.rawInk.assign((static_cast<size_t>(ink.width)*ink.height+63)/64,0);
        for(int y=15;y<=20;++y)for(int x=10;x<=25;++x) {
            const size_t pixel=static_cast<size_t>(y)*ink.width+x;
            ink.rawInk[pixel/64]|=uint64_t{1}<<(pixel%64);
        }
        const SubtitleBoxRect detected{20,60,60,80};
        const auto safe=LimitSubtitlePaddingAtGlyphInk(detected,100,100,
            SubtitleBoxPadding(30,40,21),&ink);
        Assert::AreEqual(18,safe.top,
            L"top padding should stop after the neighboring text row, leaving a sampling-step gap");
        Assert::AreEqual(30,safe.sides);Assert::AreEqual(21,safe.bottom);
        const auto expanded=ExpandSubtitleBox(detected,100,100,safe);
        Assert::IsTrue(expanded.top>=42,
            L"the padded border must not overlap the neighboring glyph row ending at source row 40");

        const auto clear=LimitSubtitlePaddingAtGlyphInk(detected,100,100,
            SubtitleBoxPadding(30,40,21),nullptr);
        Assert::AreEqual(40,clear.top,
            L"configured padding remains unchanged when current ink provides no collision evidence");
    }
    TEST_METHOD(PaddingGuardKeepsItsSafeMarginAcrossBriefInkLoss) {
        auto p=Preview();auto r=Text();r.bounds={20,60,60,80};
        p.current.text=r;p.current.text.detected=true;
        auto ink=std::make_shared<SubtitleInkSnapshot>();
        ink->width=32;ink->height=40;ink->step=2;ink->sourceRows.resize(ink->height);
        for(int y=0;y<ink->height;++y)ink->sourceRows[static_cast<size_t>(y)]=y*ink->step;
        ink->rawInk.assign((static_cast<size_t>(ink->width)*ink->height+63)/64,0);
        for(int y=15;y<=20;++y)for(int x=10;x<=25;++x) {
            const size_t pixel=static_cast<size_t>(y)*ink->width+x;
            ink->rawInk[pixel/64]|=uint64_t{1}<<(pixel%64);
        }
        p.current.ink=ink;
        SubtitlePaddingGuard guard;const SubtitleBoxPadding requested(30,40,21);
        const auto acquired=guard.Consume(r,p,requested);
        Assert::AreEqual(18,acquired.top);

        r.detected=false;r.held=true;p.current.text.detected=false;p.current.ink.reset();
        p.current.barAuthority=false;
        const auto held=guard.Consume(r,p,requested);
        Assert::AreEqual(acquired.top,held.top,
            L"a short detector miss must not restore the full padding and flash over nearby glyphs");

        r.detected=true;r.held=false;++r.cue;
        const auto nextCue=guard.Consume(r,p,requested);
        Assert::AreEqual(requested.top,nextCue.top,
            L"a new cue starts with the configured margin and its own current-ink evidence");
    }
    TEST_METHOD(PaddingGuardUsesLookaheadOnceAndCannotShrinkDuringSameCue) {
        auto p=Preview();auto r=Text();r.bounds={20,60,60,80};r.lineCount=1;
        r.lineBounds[0]=r.bounds;r.lineSignatures[0].fill(80);p.current.text=r;
        auto ink=std::make_shared<SubtitleInkSnapshot>();
        ink->width=50;ink->height=50;ink->step=2;ink->sourceRows.resize(50);
        for(int y=0;y<50;++y)ink->sourceRows[y]=y*2;
        ink->rawInk.resize((2500+63)/64);
        p.current.ink=ink;
        auto nearby=std::make_shared<SubtitleInkSnapshot>(*ink);
        for(int x=10;x<25;++x) {const size_t bit=20*50+x;nearby->rawInk[bit/64]|=uint64_t{1}<<(bit%64);}
        p.followingCount=1;p.following[0]=p.current;
        ++p.following[0].identity.acceptedSequence;p.following[0].ink=nearby;
        SubtitlePaddingGuard guard;const SubtitleBoxPadding requested(30,40,21);
        const auto first=guard.Consume(r,p,requested);Assert::AreEqual(18,first.top);
        // Moving scenery gets closer after acquisition; the visible margin must not shrink.
        auto closer=std::make_shared<SubtitleInkSnapshot>(*ink);
        for(int x=10;x<25;++x) {const size_t bit=27*50+x;closer->rawInk[bit/64]|=uint64_t{1}<<(bit%64);}
        p.current.ink=closer;++p.current.identity.acceptedSequence;p.followingCount=0;
        Assert::AreEqual(first.top,guard.Consume(r,p,requested).top);
        p.current.ink=ink;++p.current.identity.acceptedSequence;
        Assert::AreEqual(first.top,guard.Consume(r,p,requested).top);
        ++r.cue;p.current.text=r;
        Assert::AreEqual(requested.top,guard.Consume(r,p,requested).top);
        // An unrelated upcoming subtitle cannot limit the new cue's margin.
        guard.Reset();p.followingCount=1;p.following[0]=p.current;
        ++p.following[0].identity.acceptedSequence;p.following[0].ink=closer;
        p.following[0].text.lineBounds[0]={80,60,120,80};
        Assert::AreEqual(requested.top,guard.Consume(r,p,requested).top);
        // Explicit setting changes recalculate immediately using current evidence.
        p.followingCount=0;p.current.ink=closer;
        Assert::AreEqual(4,guard.Consume(r,p,SubtitleBoxPadding(30,41,21)).top);
    }
    TEST_METHOD(PictureCaptureDoesNotBorrowGlyphEnvelopeOrDisplayPadding) {
        const SubtitleBoxRect glyphs{20,40,44,57},card{22,42,42,56},empty{};
        for(int padding:{0,12}) {
            const auto g=ComputeSubtitleCutPaste(glyphs,64,64,8,56,SubtitleBoxPadding(padding),0,&card);
            Assert::IsTrue(g.valid);Assert::AreEqual(22,g.pictureCapture.left);
            Assert::AreEqual(42,g.pictureCapture.top);Assert::AreEqual(42,g.pictureCapture.right);
            Assert::AreEqual(56,g.pictureCapture.bottom);
            Assert::AreEqual(card.left,g.generatedCleanup.left);
            Assert::AreEqual(card.top,g.generatedCleanup.top);
            Assert::AreEqual(card.right,g.generatedCleanup.right);
            Assert::AreEqual(card.bottom,g.generatedCleanup.bottom);
        }
        const auto noCard=ComputeSubtitleCutPaste(glyphs,64,64,8,56,0,0,&empty);
        Assert::IsFalse(noCard.pictureCapture.Valid());
        Assert::IsFalse(noCard.generatedCleanup.Valid(),
            L"glyph fringe crossing into picture cannot authorize cleanup without a measured card");
    }
    TEST_METHOD(PaddingClipsEveryRasterEdgeWithoutOverflow) {
        const auto r=ExpandSubtitleBox({2,3,638,359},640,360);
        Assert::AreEqual(0,r.left);Assert::AreEqual(0,r.top);
        Assert::AreEqual(640,r.right);Assert::AreEqual(360,r.bottom);
        const auto huge=ExpandSubtitleBox({1,1,639,359},640,360,INT_MAX);
        Assert::AreEqual(0,huge.left);Assert::AreEqual(640,huge.right);
        Assert::AreEqual(0,huge.top);Assert::AreEqual(360,huge.bottom);
        Assert::IsFalse(ExpandSubtitleBox({700,20,710,30},640,360).Valid());
    }
    TEST_METHOD(MovePreservesSizeAndReachesTheActivePictureBoundary) {
        const auto g=ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315);
        Assert::IsTrue(g.valid);
        Assert::AreEqual(315,g.destination.bottom);Assert::AreEqual(219,g.destination.top);
        Assert::AreEqual(g.source.left,g.destination.left);
        Assert::AreEqual(g.source.right,g.destination.right);
        Assert::AreEqual(g.source.bottom-g.source.top,g.destination.bottom-g.destination.top);
    }
    TEST_METHOD(MeasuredSourceCardCleanupIsIndependentOfDisplayPadding) {
        const SubtitleBoxRect glyphs{200,295,400,330};
        const SubtitleBoxRect card{175,278,428,315};
        for(const int margin:{0,5,30}) {
            const auto g=ComputeSubtitleCutPaste(glyphs,640,360,45,315,
                SubtitleBoxPadding(margin),0,&card);
            Assert::IsTrue(g.valid);
            Assert::AreEqual(card.left,g.generatedCleanup.left);
            Assert::AreEqual(card.top,g.generatedCleanup.top);
            Assert::AreEqual(card.right,g.generatedCleanup.right);
            Assert::AreEqual(315,g.generatedCleanup.bottom);
            Assert::AreEqual(glyphs.left,g.content.left);
            Assert::AreEqual(glyphs.bottom,g.content.bottom);
        }
        const SubtitleBoxRect noCard{};
        const auto barOnly=ComputeSubtitleCutPaste({200,320,400,335},640,360,45,315,
            SubtitleBoxPadding(60),0,&noCard);
        Assert::IsTrue(barOnly.valid);
        Assert::IsFalse(barOnly.generatedCleanup.Valid(),
            L"display padding alone must not authorize erasing picture content");
    }
    TEST_METHOD(ConfirmedGlyphCorrectionCanExpandTheCleanupFootprint) {
        auto p=Preview();auto r=Text();SubtitleCutPastePresentation state;
        r.sourcePanel={180,275,420,315};
        const auto first=state.Consume(r,p,SubtitleBoxPadding(0));
        Assert::IsTrue(first.valid);
        r.sourcePanel={170,270,430,315};r.revised=true;
        const auto changed=state.Consume(r,p,SubtitleBoxPadding(0));
        Assert::AreEqual(170,changed.generatedCleanup.left);
        Assert::AreEqual(270,changed.generatedCleanup.top);
        Assert::AreEqual(430,changed.generatedCleanup.right);
        Assert::AreEqual(first.destination.top,changed.destination.top,
            L"refining the source card must not move the displayed glyphs");
        r.sourcePanel={};r.revised=false;
        const auto absent=state.Consume(r,p,SubtitleBoxPadding(0));
        Assert::AreEqual(170,absent.generatedCleanup.left,
            L"weaker card measurement cannot expose an accepted opaque backing again");
        Assert::IsFalse(absent.pictureCapture.Valid(),L"held cleanup must never grant stale extraction authority");
        ++r.cue;
        const auto replaced=state.Consume(r,p,SubtitleBoxPadding(0));
        Assert::IsFalse(replaced.generatedCleanup.Valid(),L"a new cue never inherits the previous footprint");
    }
    TEST_METHOD(WholeCuePlacementCleansBothOriginalPictureLineAndMovesBothLines) {
        // The upper subtitle line is already over the picture; the lower line
        // crosses into the letterbox and anchors the cue. Use their union as
        // the source so the generated panel cannot cover an un-moved upper line.
        const SubtitleBoxRect upper{150,280,490,300};
        const SubtitleBoxRect lower{212,320,390,335};
        const SubtitleBoxRect cue{150,280,490,335};
        const auto g=ComputeSubtitleCutPaste(cue,640,360,45,315,
            SubtitleBoxPadding(20,10,5));
        Assert::IsTrue(g.valid);
        Assert::IsTrue(g.content.left<=upper.left && g.content.top<=upper.top &&
            g.content.right>=upper.right && g.content.bottom>=upper.bottom);
        Assert::IsTrue(g.content.left<=lower.left && g.content.top<=lower.top &&
            g.content.right>=lower.right && g.content.bottom>=lower.bottom);
        Assert::IsTrue(g.generatedCleanup.left<=upper.left &&
            g.generatedCleanup.top<=upper.top && g.generatedCleanup.right>=upper.right &&
            g.generatedCleanup.bottom==315,
            L"generated cleanup must erase the original picture-side line but stop at the bar seam");
        const int verticalShift=g.destination.top-g.source.top;
        const auto movedUpper=MapSubtitleCutPastePixel(g,upper.left+20,
            upper.top+verticalShift);
        const auto movedLower=MapSubtitleCutPastePixel(g,lower.left+20,
            lower.top+verticalShift);
        Assert::AreEqual(upper.left+20,movedUpper.x);
        Assert::AreEqual(upper.top,movedUpper.y);
        Assert::AreEqual(lower.left+20,movedLower.x);
        Assert::AreEqual(lower.top,movedLower.y);
    }
    TEST_METHOD(DestinationWinsOverlapAndUncoveredSourceClears) {
        const auto g=ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315);
        const auto overlap=MapSubtitleCutPastePixel(g,210,290);
        Assert::IsFalse(overlap.clear);Assert::AreEqual(210,overlap.x);Assert::AreEqual(326,overlap.y);
        const auto deeperOverlap=MapSubtitleCutPastePixel(g,210,300);
        Assert::IsFalse(deeperOverlap.clear);Assert::AreEqual(336,deeperOverlap.y);
        Assert::IsTrue(MapSubtitleCutPastePixel(g,210,330).clear);
        Assert::IsTrue(MapSubtitleCutPastePixel(g,210,350).clear);
        const auto outside=MapSubtitleCutPastePixel(g,210,351);
        Assert::IsFalse(outside.clear);Assert::AreEqual(351,outside.y);
        Assert::IsFalse(MapSubtitleCutPastePixel(g,g.source.right,300).clear);
    }
    TEST_METHOD(RejectsMissingBarInvalidPictureAndOppositeBarCue) {
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},640,360,0,360).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,200,400,230},640,360,45,315).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,20,400,340},640,360,45,315).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},640,360,315,45).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},640,360,-1,315).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},640,360,45,361).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({},640,360,45,315).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},0,360,45,315).valid);
    }
    TEST_METHOD(TopBarMovesEntirePaddedCueDownIntoPicture) {
        for(const auto bounds:{SubtitleBoxRect{200,15,400,35},SubtitleBoxRect{200,25,400,65}}) {
            const SubtitleBoxRect card{180,20,420,75};
            const auto g=ComputeSubtitleCutPaste(bounds,640,360,45,315,
                SubtitleBoxPadding(20,10,15),8,&card);
            Assert::IsTrue(g.valid);
            Assert::AreEqual(53,g.destination.top);
            Assert::AreEqual(g.source.bottom-g.source.top,g.destination.bottom-g.destination.top);
            Assert::IsTrue(g.destination.bottom<=315);
            Assert::AreEqual(45,g.generatedCleanup.top);
            Assert::AreEqual(75,g.generatedCleanup.bottom);
            const int shiftedY=bounds.top+g.destination.top-g.source.top;
            const auto mapped=MapSubtitleCutPastePixel(g,250,shiftedY);
            Assert::AreEqual(bounds.top,mapped.y);Assert::IsFalse(mapped.clear);
            Assert::IsTrue(MapSubtitleCutPastePixel(g,250,bounds.top).clear);
        }
        Assert::IsFalse(ComputeSubtitleCutPaste({200,0,400,100},640,360,45,105,40,5).valid);
    }
    TEST_METHOD(NearBarConfigurationValidatesSourcePixelRange) {
        for(const char* value:{"0","20","100","200","-1","201","2.5","many"}) {
            char directory[MAX_PATH]{},path[MAX_PATH]{};
            Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
            Assert::IsTrue(GetTempFileNameA(directory,"vpp",0,path)!=0);
            {std::ofstream file(path);file<<"[vprenderer.subtitles]\nsubtitle_near_bar_px: "<<value<<"\n";}
            ConfigFile config;Assert::IsTrue(config.Load(path));DeleteFileA(path);
            RendererProfileConfig::Model model;std::string error,actual;
            const bool expected=std::string(value)=="0" || std::string(value)=="20" ||
                std::string(value)=="100" || std::string(value)=="200";
            Assert::AreEqual(expected,RendererProfileConfig::Read(config,model,error));
            if(expected) {
                Assert::IsTrue(config.TryGetString("vprenderer.subtitles","subtitle_near_bar_px",actual));
                Assert::AreEqual(std::string(value),actual);
            }
        }
    }
    TEST_METHOD(HoldConfigurationValidatesMillisecondsInSubtitleProfiles) {
        for(const char* value:{"0","250","500","1000","-1","1001","2.5","many"}) {
            char directory[MAX_PATH]{},path[MAX_PATH]{};
            Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
            Assert::IsTrue(GetTempFileNameA(directory,"vpp",0,path)!=0);
            {std::ofstream file(path);file<<"[vprenderer.subtitles]\nsubtitle_hold_ms: "<<value<<"\n";}
            ConfigFile config;Assert::IsTrue(config.Load(path));DeleteFileA(path);
            RendererProfileConfig::Model model;std::string error,actual;
            const bool expected=std::string(value)=="0" || std::string(value)=="250" ||
                std::string(value)=="500" || std::string(value)=="1000";
            Assert::AreEqual(expected,RendererProfileConfig::Read(config,model,error));
            if(expected) {
                Assert::IsTrue(config.TryGetString("vprenderer.subtitles","subtitle_hold_ms",actual));
                Assert::AreEqual(std::string(value),actual);
            }
        }
    }
    TEST_METHOD(RejectsTooTallCueAndInvalidInsetsRatherThanClippingText) {
        Assert::IsFalse(ComputeSubtitleCutPaste({200,250,400,350},640,360,230,315).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315,-1).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315,10,-1).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315,10,INT_MAX).valid);
        const auto original=MapSubtitleCutPastePixel({},210,320);
        Assert::IsFalse(original.clear);Assert::AreEqual(210,original.x);Assert::AreEqual(320,original.y);
    }
    TEST_METHOD(LayerBoundsFollowSelectedDisplayPlaneWithPhysicalFallback) {
        SubtitleCutPasteGeometry bottom;
        bottom.valid=true;bottom.destination={100,250,300,300};
        bottom.pictureTop=40;bottom.pictureBottom=280;bottom.extendToBar=true;bottom.fromTopBar=false;
        bottom.displayPictureLeft=0;bottom.displayPictureRight=640;
        bottom.displayPictureTop=40;bottom.displayPictureBottom=320;
        const auto selectedBottom=ComputeSubtitleLayerBounds(bottom,640,360);
        Assert::AreEqual(92,selectedBottom.x0);Assert::AreEqual(308,selectedBottom.x1);
        Assert::AreEqual(248,selectedBottom.y0);
        Assert::AreEqual(322,selectedBottom.y1,
            L"bottom layer includes the selected display plane even when it is over two pixels beyond source bounds");

        SubtitleCutPasteGeometry top=bottom;
        top.destination={100,60,300,110};top.pictureTop=80;top.pictureBottom=320;
        top.displayPictureTop=40;top.displayPictureBottom=320;top.fromTopBar=true;
        const auto selectedTop=ComputeSubtitleLayerBounds(top,640,360);
        Assert::AreEqual(92,selectedTop.x0);Assert::AreEqual(308,selectedTop.x1);
        Assert::AreEqual(38,selectedTop.y0,
            L"top layer mirrors selected-plane expansion beyond the measured source edge");
        Assert::AreEqual(112,selectedTop.y1);

        bottom.displayPictureLeft=bottom.displayPictureTop=0;
        bottom.displayPictureRight=bottom.displayPictureBottom=0;
        const auto physicalBottom=ComputeSubtitleLayerBounds(bottom,640,360);
        Assert::AreEqual(302,physicalBottom.y1,
            L"an absent selected display rectangle falls back to the physical source edge");
        top.displayPictureLeft=top.displayPictureTop=0;
        top.displayPictureRight=top.displayPictureBottom=0;
        const auto physicalTop=ComputeSubtitleLayerBounds(top,640,360);
        Assert::AreEqual(58,physicalTop.y0,
            L"top fallback uses the physical source edge when no display rectangle is available");
    }
    TEST_METHOD(GlyphLocalCleanupAndCaptureStayWithinFreshSupportOnBothBars) {
        const auto contains=[](const SubtitleBoxRect& outer,const SubtitleBoxRect& inner) {
            return outer.Valid() && inner.Valid() && inner.left>=outer.left && inner.top>=outer.top &&
                inner.right<=outer.right && inner.bottom<=outer.bottom;
        };
        const auto mirror=[](SubtitleBoxRect r) {
            return SubtitleBoxRect{r.left,360-r.bottom,r.right,360-r.top};
        };
        for(bool top:{false,true}) {
            auto p=Preview();p.current.nearBarDistance=20;
            auto text=Text();text.sourcePanel={150,265,450,340};
            if(top) {text.bounds=mirror(text.bounds);text.sourcePanel=mirror(text.sourcePanel);}
            SubtitleCutPastePresentation placement;
            const auto earlier=placement.Consume(text,p,SubtitleBoxPadding(5));
            Assert::IsTrue(earlier.valid && earlier.generatedCleanup.Valid(),
                L"establish a broad prior card before fresh local proof");

            const SubtitleBoxRect local=top?SubtitleBoxRect{180,50,420,70}:
                SubtitleBoxRect{180,290,420,310};
            text.bounds=top?SubtitleBoxRect{210,55,390,65}:
                SubtitleBoxRect{210,295,390,305};
            text.lineCount=1;text.lineBounds[0]=text.bounds;
            text.sourcePanel=top?SubtitleBoxRect{150,20,450,95}:
                SubtitleBoxRect{150,265,450,340};
            text.capturePanel=text.sourcePanel;text.capturePanelMeasured=true;
            text.nearBarEligibilityMeasured=true;text.nearBarEligible=true;
            p.current.text.detected=true;p.current.glyphLocalVerifiedBounds=local;
            p.current.glyphLocalTop=top;p.current.glyphLocalBottom=!top;
            const auto fresh=placement.Consume(text,p,SubtitleBoxPadding(5));
            Assert::IsTrue(fresh.valid,L"fresh local glyphs should remain presentable");
            Assert::IsTrue(contains(local,fresh.content),L"glyph capture must fit current support");
            Assert::IsTrue(contains(local,fresh.pictureCapture),L"picture capture must fit current support");
            Assert::IsTrue(contains(local,fresh.generatedCleanup),L"cleanup cannot extend to the retained bar edge or old card");
            Assert::IsTrue(contains(local,fresh.layoutCoverage),L"reduced layout cannot reuse pre-clamp card coverage");
            Assert::IsTrue(fresh.generatedCleanup.left>earlier.generatedCleanup.left &&
                fresh.generatedCleanup.right<earlier.generatedCleanup.right,
                L"fresh local support must not borrow the previous broad card");
        }
    }
    TEST_METHOD(GlyphLocalCleanupCapSurvivesPhysicalEdgeReconciliation) {
        for(bool top:{false,true}) {
            const SubtitleBoxRect glyphs=top?SubtitleBoxRect{200,55,400,65}:
                SubtitleBoxRect{200,295,400,305};
            const SubtitleBoxRect local=top?SubtitleBoxRect{180,45,420,70}:
                SubtitleBoxRect{180,290,420,315};
            auto geometry=ComputeSubtitleCutPaste(glyphs,640,360,45,315,
                SubtitleBoxPadding(5),0,&local,20,&local,true);
            Assert::IsTrue(geometry.valid && geometry.generatedCleanup.Valid());
            geometry.cleanupLimit=local;
            LimitSubtitleCleanupToLocalProof(geometry);
            const SubtitleBoxRect visible=top?SubtitleBoxRect{0,43,640,315}:
                SubtitleBoxRect{0,45,640,317};
            const auto fitted=FitSubtitleToVisiblePicture(geometry,visible);
            Assert::IsTrue(fitted.valid);
            Assert::AreEqual(local.top,fitted.generatedCleanup.top);
            Assert::AreEqual(local.bottom,fitted.generatedCleanup.bottom);
            Assert::AreEqual(local.top,fitted.layoutCoverage.top);
            Assert::AreEqual(local.bottom,fitted.layoutCoverage.bottom);
            Assert::AreEqual(local.top,fitted.pictureCapture.top);
            Assert::AreEqual(local.bottom,fitted.pictureCapture.bottom);
            Assert::IsTrue(fitted.generatedCleanup.top>=local.top &&
                fitted.generatedCleanup.bottom<=local.bottom,
                L"physical edge reconciliation cannot extend erasure beyond local proof");
        }
    }
    TEST_METHOD(ProofModeTransitionsKeepSameGlyphStableAndRefreshEachCueCapture) {
        const auto sameRect=[](const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
            return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
        };
        for(bool top:{false,true}) {
            auto p=Preview();auto text=Text();
            if(top)text.bounds=MirrorSubtitleRect(text.bounds);
            text.lineCount=1;text.lineBounds[0]=text.bounds;text.lineSignatures[0].fill(0x41);
            text.signature.fill(0x41);text.anchorSignature.fill(0x41);
            text.nearBarEligibilityMeasured=true;text.nearBarEligible=true;
            const SubtitleBoxRect panel=top?SubtitleBoxRect{170,45,430,85}:SubtitleBoxRect{170,275,430,315};
            const SubtitleBoxRect capture=top?SubtitleBoxRect{180,45,420,78}:SubtitleBoxRect{180,282,420,315};
            const SubtitleBoxRect local=top?SubtitleBoxRect{185,20,415,80}:SubtitleBoxRect{185,280,415,340};
            text.sourcePanel=panel;text.capturePanel=capture;text.capturePanelMeasured=true;
            SubtitleCutPastePresentation presentation;SubtitleLayoutAnchor anchor;
            const SubtitleBoxPadding padding{10,12,8};
            auto compose=[&]() {
                auto g=presentation.Consume(text,p,padding,0);
                Assert::IsTrue(g.valid,(L"current proof mode invalid top="+std::to_wstring(top)+
                    L" sequence="+std::to_wstring(p.current.identity.acceptedSequence)).c_str());
                const SubtitleBoxRect visible=top?SubtitleBoxRect{0,p.current.sharedPicture.bounds.top,640,315}:
                    SubtitleBoxRect{0,45,640,p.current.sharedPicture.bounds.bottom};
                g=FitSubtitleToVisiblePicture(g,visible,0);
                g=ApplySubtitleTextReduction(g,25,padding,true,false);
                return anchor.Consume(g,text,p,padding,0,false);
            };
            ConfigureProofFrame(p,text,top,false,1,{},top?45:315,top?43:317);
            const auto physical=compose();Assert::IsTrue(physical.generatedCleanup.Valid());
            const auto selectedPlane=top?physical.pictureTop:physical.pictureBottom;
            const float transformX=physical.glyphTranslateX,transformY=physical.glyphTranslateY;

            text.sourcePanel=panel;text.capturePanel=capture;text.capturePanelMeasured=false;
            ConfigureProofFrame(p,text,top,true,2,local,top?43:317,top?43:317);
            const auto localFrame=compose();
            Assert::AreEqual(selectedPlane,top?localFrame.pictureTop:localFrame.pictureBottom);
            Assert::AreEqual(top?0:45,p.current.barEvidence.top);
            Assert::AreEqual(top?315:360,p.current.barEvidence.bottom);
            Assert::IsFalse(p.current.independentBarAnalysis);
            Assert::IsFalse(p.current.subtitleArProof.accepted);
            Assert::IsTrue(sameRect(physical.destination,localFrame.destination),
                L"full/local proof changes must not move the unchanged glyphs");
            Assert::AreEqual(transformX,localFrame.glyphTranslateX);
            Assert::AreEqual(transformY,localFrame.glyphTranslateY);
            Assert::IsFalse(sameRect(physical.generatedCleanup,localFrame.generatedCleanup),
                L"cleanup follows current local support rather than the old complete card");
            Assert::IsTrue(localFrame.generatedCleanup.left>=local.left &&
                localFrame.generatedCleanup.right<=local.right &&
                localFrame.generatedCleanup.top>=local.top && localFrame.generatedCleanup.bottom<=local.bottom);
            Assert::IsTrue(localFrame.pictureCapture.left>=local.left &&
                localFrame.pictureCapture.right<=local.right &&
                localFrame.pictureCapture.top>=local.top && localFrame.pictureCapture.bottom<=local.bottom);

            const SubtitleBoxRect recovered=top?SubtitleBoxRect{50,43,590,85}:SubtitleBoxRect{50,275,590,317};
            text.sourcePanel=recovered;text.capturePanel=recovered;text.capturePanelMeasured=true;
            ConfigureProofFrame(p,text,top,false,3,{},top?45:315,top?43:317);
            const auto physicalAgain=compose();
            Assert::IsTrue(physicalAgain.generatedCleanup.left>=170 && physicalAgain.generatedCleanup.right<=430,
                L"same-glyph full recovery cleanup is limited to current support intersected with prior full card");
            Assert::IsTrue(physicalAgain.pictureCapture.left>=recovered.left && physicalAgain.pictureCapture.right<=recovered.right,
                L"recovered picture capture remains current even when cleanup is capped");
            Assert::IsTrue(physicalAgain.pictureCapture.left>=170 && physicalAgain.pictureCapture.right<=430,
                L"current capture is restricted to current support intersected with the retained safe footprint");

            ++text.cue;text.capturePanelMeasured=true;text.capturePanel=capture;
            text.sourcePanel=panel;
            ConfigureProofFrame(p,text,top,false,4,{},top?45:315,top?43:317);
            const auto cueIdChurn=compose();
            Assert::IsTrue(sameRect(localFrame.destination,cueIdChurn.destination),
                L"cue-id churn with identical glyph signature retains placement");
            Assert::AreEqual(localFrame.glyphTranslateX,cueIdChurn.glyphTranslateX);
            Assert::AreEqual(localFrame.glyphTranslateY,cueIdChurn.glyphTranslateY);

            ++text.cue;text.bounds=top?MirrorSubtitleRect({250,295,300,330}):SubtitleBoxRect{250,295,300,330};
            text.lineBounds[0]=text.bounds;text.lineSignatures[0].fill(0x59);
            text.signature.fill(0x59);text.anchorSignature.fill(0x59);
            text.capturePanelMeasured=false;text.capturePanel=capture;
            const SubtitleBoxRect youLocal=top?SubtitleBoxRect{232,20,318,75}:SubtitleBoxRect{232,285,318,340};
            ConfigureProofFrame(p,text,top,true,5,youLocal,top?43:317,top?43:317);
            const auto newCue=compose();
            Assert::IsTrue(newCue.destination.right-newCue.destination.left<
                cueIdChurn.destination.right-cueIdChurn.destination.left,
                L"a true glyph change gets its own output box");
            Assert::IsTrue(newCue.glyphTranslateX!=cueIdChurn.glyphTranslateX ||
                newCue.destination.left!=cueIdChurn.destination.left,
                L"a changed cue must not inherit the prior glyph pivot");
            Assert::IsTrue(newCue.pictureCapture.left>=youLocal.left &&
                newCue.pictureCapture.right<=youLocal.right &&
                newCue.generatedCleanup.left>=youLocal.left &&
                newCue.generatedCleanup.right<=youLocal.right &&
                newCue.generatedCleanup.top>=youLocal.top && newCue.generatedCleanup.bottom<=youLocal.bottom);

            const SubtitleBoxRect freshRecovery=top?SubtitleBoxRect{238,45,312,72}:SubtitleBoxRect{238,293,312,315};
            text.sourcePanel=freshRecovery;text.capturePanel=freshRecovery;text.capturePanelMeasured=true;
            ConfigureProofFrame(p,text,top,false,6,{},top?43:317,top?43:317);
            const auto newCueFull=compose();
            Assert::AreEqual(selectedPlane,top?newCueFull.pictureTop:newCueFull.pictureBottom);
            Assert::AreEqual(freshRecovery.left,newCueFull.generatedCleanup.left,
                L"a genuinely new glyph signature does not inherit the previous cue cleanup cap");
            ConfigureProofFrame(p,text,top,false,7,{},top?45:315,top?43:317);
            const auto jittered=compose();
            Assert::IsTrue(sameRect(newCueFull.destination,jittered.destination),
                L"physical measurement jitter does not alter stable selected AR geometry");
            text.sourcePanel=text.capturePanel=top?SubtitleBoxRect{238,39,312,72}:
                SubtitleBoxRect{238,293,312,321};
            text.capturePanelMeasured=true;
            ConfigureProofFrame(p,text,top,false,8,{},top?39:321,top?39:321);
            const auto moved=compose();
            Assert::AreEqual(top?39:321,top?moved.pictureTop:moved.pictureBottom);
            Assert::IsTrue(moved.generatedCleanup.Valid() &&
                moved.generatedCleanup.left<=text.sourcePanel.left && moved.generatedCleanup.right>=text.sourcePanel.right &&
                moved.generatedCleanup.top<=text.sourcePanel.top && moved.generatedCleanup.bottom>=text.sourcePanel.bottom,
                L"fresh physical proof reacquires its current cleanup panel after a real plane change");
            Assert::IsTrue(moved.destination.top!=jittered.destination.top || moved.destination.bottom!=jittered.destination.bottom,
                L"a genuine four-row selected-plane move redocks without a context reset");
            text.sourcePanel=text.capturePanel=freshRecovery;
            text.capturePanelMeasured=true;
            ConfigureProofFrame(p,text,top,false,9,{},top?45:315,top?37:323);
            const auto farPhysical=compose();
            ConfigureProofFrame(p,text,top,true,10,top?SubtitleBoxRect{185,20,415,80}:
                SubtitleBoxRect{185,280,415,340},top?37:323,top?37:323);
            const auto farLocal=compose();
            Assert::AreEqual(top?37:323,top?farLocal.pictureTop:farLocal.pictureBottom);
            Assert::IsTrue(sameRect(farPhysical.destination,farLocal.destination),
                L"full-to-local proof preserves the selected plane even when physical measurement differs by eight rows");
        }
    }
    TEST_METHOD(DisplayPaddingAndContextResetStaySeparateFromCurrentCleanup) {
        const auto sameRect=[](const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
            return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
        };
        for(bool top:{false,true}) {
            auto p=Preview();auto text=Text();
            if(top)text.bounds=MirrorSubtitleRect(text.bounds);
            text.lineCount=1;text.lineBounds[0]=text.bounds;text.lineSignatures[0].fill(0x31);
            text.signature.fill(0x31);text.anchorSignature.fill(0x31);
            text.nearBarEligibilityMeasured=true;text.nearBarEligible=true;text.capturePanelMeasured=false;
            const SubtitleBoxRect compact=top?SubtitleBoxRect{185,20,415,80}:SubtitleBoxRect{185,280,415,340};
            SubtitleCutPastePresentation presentation;SubtitleLayoutAnchor anchor;
            auto compose=[&](const SubtitleBoxPadding& padding) {
                auto g=presentation.Consume(text,p,padding,0);Assert::IsTrue(g.valid);
                const SubtitleBoxRect visible=top?SubtitleBoxRect{0,p.current.sharedPicture.bounds.top,640,315}:
                    SubtitleBoxRect{0,45,640,p.current.sharedPicture.bounds.bottom};
                g=FitSubtitleToVisiblePicture(g,visible,0);
                g=ApplySubtitleTextReduction(g,20,padding,true,false);
                return anchor.Consume(g,text,p,padding,0,false);
            };
            ConfigureProofFrame(p,text,top,true,1,compact);
            const SubtitleBoxPadding base{5,6,4};
            const auto first=compose(base);
            const SubtitleBoxRect expanded=top?SubtitleBoxRect{170,15,430,85}:SubtitleBoxRect{170,275,430,345};
            ConfigureProofFrame(p,text,top,true,2,expanded);
            const auto backingGrowth=compose(base);
            Assert::IsTrue(sameRect(first.destination,backingGrowth.destination),
                L"cleanup growth cannot resize or move the output box");
            Assert::AreEqual(first.glyphTranslateX,backingGrowth.glyphTranslateX);
            Assert::AreEqual(first.glyphTranslateY,backingGrowth.glyphTranslateY);
            Assert::IsFalse(sameRect(first.generatedCleanup,backingGrowth.generatedCleanup));
            const SubtitleBoxPadding larger{18,16,12};
            const auto padded=compose(larger);
            Assert::IsTrue(padded.destination.right-padded.destination.left>
                backingGrowth.destination.right-backingGrowth.destination.left ||
                padded.destination.bottom-padded.destination.top>
                backingGrowth.destination.bottom-backingGrowth.destination.top,
                L"display padding changes the output envelope independently");
            Assert::IsTrue(sameRect(backingGrowth.generatedCleanup,padded.generatedCleanup));
            Assert::IsTrue(sameRect(backingGrowth.pictureCapture,padded.pictureCapture));

            ++p.policyGeneration;++p.continuityGeneration;
            p.current.policyGeneration=p.policyGeneration;p.current.continuityGeneration=p.continuityGeneration;
            p.current.pictureTop=top?60:45;p.current.pictureBottom=top?315:300;
            p.current.sharedPicture.bounds={0,p.current.pictureTop,640,p.current.pictureBottom,640,360};
            ++p.current.identity.acceptedSequence;
            p.current.sharedPicture.identity=p.current.identity;
            const auto reset=compose(larger);
            Assert::IsTrue(reset.valid);
            Assert::AreEqual(top?60:300,top?reset.pictureTop:reset.pictureBottom);
            Assert::IsTrue(reset.glyphTranslateY!=padded.glyphTranslateY ||
                !sameRect(reset.destination,padded.destination),
                L"context and selected-plane changes discard the old anchor");
        }
    }
    TEST_METHOD(GlyphLocalRejectsAcceptedOldGlyphsOutsideCurrentSupport) {
        for(bool top:{false,true}) {
            auto p=Preview();p.current.nearBarDistance=20;
            p.current.glyphLocalVerifiedBounds=top?SubtitleBoxRect{180,50,420,70}:
                SubtitleBoxRect{180,290,420,310};
            p.current.glyphLocalTop=top;p.current.glyphLocalBottom=!top;
            p.current.text.detected=true;
            auto text=Text();text.bounds=top?SubtitleBoxRect{200,20,400,40}:
                SubtitleBoxRect{200,320,400,340};
            text.lineCount=1;text.lineBounds[0]=text.bounds;
            text.nearBarEligibilityMeasured=true;text.nearBarEligible=true;
            SubtitleCutPastePresentation placement;
            const auto stale=placement.Consume(text,p,SubtitleBoxPadding(5));
            Assert::IsFalse(stale.valid,L"a prior accepted cue cannot project glyphs outside current local proof");
        }
    }
};
namespace {
struct SubtitleShaderGpuFixture {
    SubtitlePreparationRenderer preparation;
    pl_d3d11 device=nullptr;
    pl_renderer renderer=nullptr;
    const pl_hook* hook=nullptr;
    pl_tex inputTexture=nullptr,chromaTexture=nullptr,allTexture=nullptr,cleanTexture=nullptr,layerTexture=nullptr;
    pl_tex allOutput=nullptr,splitOutput=nullptr;
    pl_frame input{};
    SubtitleShaderGpuFixture(pl_color_transfer transfer=PL_COLOR_TRC_LINEAR,int yuvFormat=0) {
        pl_d3d11_params d{};d.allow_software=true;
        device=pl_d3d11_create(nullptr,&d);
        Assert::IsTrue(device!=nullptr,L"Subtitle shader D3D11 fixture unavailable");
        renderer=pl_renderer_create(nullptr,device->gpu);
        Assert::IsTrue(renderer!=nullptr);
        hook=CreateSubtitleCutPasteHook(device->gpu);
        Assert::IsTrue(hook!=nullptr,L"Subtitle shader could not be parsed");
        pl_tex_params p{};p.w=p.h=64;p.format=pl_find_named_fmt(device->gpu,"rgba8");
        Assert::IsTrue(p.format!=nullptr);
        p.sampleable=true;p.renderable=true;p.blit_src=true;p.blit_dst=true;
        p.host_readable=true;p.host_writable=true;
        inputTexture=pl_tex_create(device->gpu,&p);
        Assert::IsTrue(inputTexture!=nullptr);
        p.format=pl_find_fmt(device->gpu,PL_FMT_FLOAT,4,16,0,
            static_cast<pl_fmt_caps>(PL_FMT_CAP_RENDERABLE|PL_FMT_CAP_BLENDABLE));
        Assert::IsTrue(p.format!=nullptr,L"Linear float target format unavailable");
        p.sampleable=false;p.blit_src=false;p.blit_dst=false;p.host_writable=false;
        allOutput=pl_tex_create(device->gpu,&p);
        splitOutput=pl_tex_create(device->gpu,&p);
        Assert::IsTrue(allOutput&&splitOutput);
        input.num_planes=1;input.planes[0].texture=inputTexture;input.planes[0].components=4;
        for(int c=0;c<4;++c)input.planes[0].component_mapping[c]=c;
        input.repr=pl_color_repr_rgb;input.repr.alpha=PL_ALPHA_NONE;
        input.color=pl_color_space_srgb;input.color.transfer=PL_COLOR_TRC_LINEAR;
        input.crop={0,0,64,64};
        std::vector<uint8_t> pixels(64*64*4,255);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
            const size_t i=(y*64+x)*4;
            const bool card=x>=20&&x<44&&y>=48&&y<60;
            const bool glyph=x>=26&&x<38&&y>=51&&y<57;
            const uint8_t value=glyph?250:card?100:40;
            pixels[i]=pixels[i+1]=pixels[i+2]=value;
        }
        pl_tex_transfer_params upload{};upload.tex=inputTexture;upload.ptr=pixels.data();
        Assert::IsTrue(pl_tex_upload(device->gpu,&upload));
        input.color.transfer=transfer;
        if(transfer==PL_COLOR_TRC_PQ || transfer==PL_COLOR_TRC_HLG) {
            input.color.primaries=PL_COLOR_PRIM_BT_2020;
            input.color.hdr.min_luma=0.005f;
            input.color.hdr.max_luma=1000.0f;
        }
        if(yuvFormat)UseYuv(yuvFormat==2);
    }
    ~SubtitleShaderGpuFixture() {
        if(!device)return;
        pl_tex_destroy(device->gpu,&inputTexture);
        pl_tex_destroy(device->gpu,&chromaTexture);
        pl_tex_destroy(device->gpu,&allTexture);
        pl_tex_destroy(device->gpu,&cleanTexture);
        pl_tex_destroy(device->gpu,&layerTexture);
        pl_tex_destroy(device->gpu,&allOutput);
        pl_tex_destroy(device->gpu,&splitOutput);
        pl_mpv_user_shader_destroy(&hook);
        preparation.Reset();
        pl_renderer_destroy(&renderer);pl_d3d11_destroy(&device);
    }
    void UseYuv(bool tenBit) {
        const int lumaStride=tenBit?2:1;
        const int chromaStride=tenBit?4:2;
        std::vector<uint8_t> y(64*64*lumaStride),uv(32*32*chromaStride);
        if(tenBit) {
            auto* luma=reinterpret_cast<uint16_t*>(y.data());
            auto* chroma=reinterpret_cast<uint16_t*>(uv.data());
            for(int row=0;row<64;++row)for(int col=0;col<64;++col) {
                const bool card=col>=20&&col<44&&row>=48&&row<60;
                const bool glyph=col>=26&&col<38&&row>=51&&row<57;
                const int value=glyph?250:card?100:40;
                luma[row*64+col]=uint16_t((64+value*876/255)<<6);
            }
            for(int i=0;i<32*32*2;++i)chroma[i]=uint16_t(512<<6);
        } else {
            for(int row=0;row<64;++row)for(int col=0;col<64;++col) {
                const bool card=col>=20&&col<44&&row>=48&&row<60;
                const bool glyph=col>=26&&col<38&&row>=51&&row<57;
                const int value=glyph?250:card?100:40;
                y[row*64+col]=uint8_t(16+value*219/255);
            }
            std::fill(uv.begin(),uv.end(),uint8_t(128));
        }
        pl_tex_params p{};p.w=p.h=64;p.sampleable=true;
        p.format=pl_find_fmt(device->gpu,PL_FMT_UNORM,1,tenBit?16:8,tenBit?16:8,PL_FMT_CAP_SAMPLEABLE);
        Assert::IsTrue(p.format!=nullptr,L"YUV luma format unavailable");
        p.initial_data=y.data();
        pl_tex luma=pl_tex_create(device->gpu,&p);
        Assert::IsTrue(luma!=nullptr);
        p.w=p.h=32;p.format=pl_find_fmt(device->gpu,PL_FMT_UNORM,2,tenBit?16:8,tenBit?16:8,PL_FMT_CAP_SAMPLEABLE);
        Assert::IsTrue(p.format!=nullptr,L"YUV chroma format unavailable");
        p.initial_data=uv.data();
        chromaTexture=pl_tex_create(device->gpu,&p);
        Assert::IsTrue(chromaTexture!=nullptr);
        pl_tex_destroy(device->gpu,&inputTexture);inputTexture=luma;
        input.num_planes=2;input.planes[0].texture=inputTexture;input.planes[0].components=1;
        input.planes[0].component_mapping[0]=0;
        input.planes[1].texture=chromaTexture;input.planes[1].components=2;
        input.planes[1].component_mapping[0]=1;input.planes[1].component_mapping[1]=2;
        input.repr.sys=input.color.primaries==PL_COLOR_PRIM_BT_2020?
            PL_COLOR_SYSTEM_BT_2020_NC:PL_COLOR_SYSTEM_BT_709;
        input.repr.levels=PL_COLOR_LEVELS_LIMITED;
        input.repr.alpha=PL_ALPHA_NONE;
        if(tenBit) {
            input.repr.bits.sample_depth=16;input.repr.bits.color_depth=10;
            input.repr.bits.bit_shift=6;
        }
    }
    static SubtitleCutPasteGeometry Geometry(bool overlap=false,bool narrow=false) {
        SubtitleCutPasteGeometry g;
        g.valid=true;g.source={20,48,44,60};g.content={24,50,40,58};
        g.destination=narrow?SubtitleBoxRect{24,32,40,44}:
            overlap?SubtitleBoxRect{20,43,44,55}:SubtitleBoxRect{20,32,44,44};
        if(narrow) {
            g.centeredGlyphMapping=true;g.glyphScale=0.5f;
            g.glyphTranslateX=16.0f;g.glyphTranslateY=11.0f;
        }
        g.generatedCleanup=g.source;
        g.pictureCapture=g.source;g.glyphLines[0]=g.content;
        g.pictureTop=8;g.pictureBottom=62;
        g.roundedCorners=true;g.extendToBar=false;
        return g;
    }
    static std::array<float,4> Pixel(const std::vector<float>& pixels,int x,int y) {
        const size_t i=(y*64+x)*4;
        return {pixels[i],pixels[i+1],pixels[i+2],pixels[i+3]};
    }
    static float FloatChannel(const uint8_t* bytes,int hostBits) {
        if(hostBits==32) {
            float value=0;std::memcpy(&value,bytes,sizeof(value));return value;
        }
        uint16_t half=0;std::memcpy(&half,bytes,sizeof(half));
        const int exponent=(half>>10)&31,significand=half&1023;
        return exponent?std::ldexp(1.0f+significand/1024.0f,exponent-15):
            std::ldexp(float(significand),-24);
    }
    std::vector<float> ReadLinear(pl_tex texture) {
        const pl_fmt format=texture->params.format;
        Assert::IsTrue(format->num_components==4 &&
            (format->host_bits[0]==16 || format->host_bits[0]==32),
            L"Unexpected float target host format");
        std::vector<uint8_t> bytes(64*64*format->texel_size);
        pl_tex_transfer_params transfer{};transfer.tex=texture;transfer.ptr=bytes.data();
        Assert::IsTrue(pl_tex_download(device->gpu,&transfer));
        std::vector<float> pixels(64*64*4);
        const size_t channelBytes=format->host_bits[0]/8;
        for(size_t pixel=0;pixel<64*64;++pixel)
            for(int channel=0;channel<4;++channel)
                pixels[pixel*4+channel]=FloatChannel(
                    bytes.data()+pixel*format->texel_size+channel*channelBytes,
                    format->host_bits[channel]);
        return pixels;
    }
    void VerifyPreparationPreservesHdrHistory() {
        auto target=input;target.planes[0].texture=allOutput;
        target.color=pl_color_space_srgb;target.color.transfer=PL_COLOR_TRC_LINEAR;
        auto peak=pl_peak_detect_high_quality_params;peak.allow_delayed=false;
        auto params=pl_render_fast_params;params.dither_params=nullptr;
        params.peak_detect_params=&peak;
        for(int i=0;i<8;++i)Assert::IsTrue(pl_render_image(renderer,&input,&target,&params));
        pl_gpu_finish(device->gpu);
        pl_hdr_metadata before{};
        Assert::IsTrue(pl_renderer_get_hdr_metadata(renderer,&before),L"HDR fixture must establish peak history");
        for(int mode:{1,3,5}) {
            auto geometry=Geometry();
            Assert::IsTrue(BindSubtitleCutPasteHook(hook,geometry,mode,input.color.transfer));
            pl_frame composed{};
            Assert::IsTrue(ComposeSubtitleBeforeCrop(preparation,device->gpu,input,hook,&allTexture,composed));
            pl_hdr_metadata after{};
            Assert::IsTrue(pl_renderer_get_hdr_metadata(renderer,&after),
                L"preparing a new subtitle packet must not reset output peak history");
            Assert::IsTrue(std::abs(before.max_pq_y-after.max_pq_y)<0.000001f);
            // A 24 Hz packet can supply three 60 Hz display ticks. Each repeat
            // must begin with the same persistent detector, without preparation.
            for(int repeat=0;repeat<3;++repeat)
                Assert::IsTrue(pl_render_image(renderer,&composed,&target,&params));
            pl_gpu_finish(device->gpu);
            Assert::IsTrue(pl_renderer_get_hdr_metadata(renderer,&before));
        }
        auto noPeak=params;noPeak.peak_detect_params=nullptr;
        Assert::IsTrue(pl_render_image(renderer,&input,&target,&noPeak));
        pl_hdr_metadata reset{};
        Assert::IsFalse(pl_renderer_get_hdr_metadata(renderer,&reset),
            L"control: a no-peak pass on the shared renderer reproduces the old reset");
    }
    void VerifyTransparentLayerDoesNotCopyPicture(bool reduced,int mode) {
        auto g=Geometry();g.pictureTop=8;g.pictureBottom=48;
        g.source={20,48,44,60};g.content={24,50,40,58};
        g.glyphLines[0]=g.content;g.pictureCapture={};g.generatedCleanup={};
        g.destination={12,24,52,38};g.extendToBar=false;
        g.centeredGlyphMapping=reduced;g.glyphScale=reduced?0.7f:1.0f;
        g.glyphTranslateX=11.3f;g.glyphTranslateY=-9.6f;
        std::vector<uint8_t> pixels(64*64*4,255);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
            const size_t i=(y*64+x)*4;
            pixels[i]=uint8_t(70+x);pixels[i+1]=uint8_t(60+y);pixels[i+2]=120;
            if(y<8 || y>=48)pixels[i]=pixels[i+1]=pixels[i+2]=0;
            if(x>=26 && x<38 && y>=52 && y<56)pixels[i]=pixels[i+1]=pixels[i+2]=230;
        }
        pl_tex_transfer_params upload{};upload.tex=inputTexture;upload.ptr=pixels.data();
        Assert::IsTrue(pl_tex_upload(device->gpu,&upload));
        SubtitleGeneratedGrayStyle style;
        style.opacity=0.0f;style.blurPixels=0.0f;style.borderOpacity=0.0f;
        style.maxLuminance=1.0f;
        Assert::IsTrue(BindSubtitleCutPasteHook(hook,g,mode,input.color.transfer,
            style,0,SubtitleCompositionPhase::LayerOnly));
        pl_frame layer{};
        Assert::IsTrue(ComposeSubtitleLayerBeforeCrop(preparation,device->gpu,input,hook,&layerTexture,layer));
        pl_tex_params storage{};storage.w=storage.h=64;storage.format=layerTexture->params.format;
        storage.blit_dst=true;storage.host_readable=true;
        pl_tex staging=pl_tex_create(device->gpu,&storage);Assert::IsNotNull(staging);
        pl_tex_blit_params copy{};copy.src=layerTexture;copy.dst=staging;
        pl_tex_blit(device->gpu,&copy);
        const auto extracted=ReadLinear(staging);pl_tex_destroy(device->gpu,&staging);
        int visible=0;
        for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
            const float alpha=Pixel(extracted,x,y)[3];
            if(alpha>0.01f)++visible;
            // No native glyph can map into this gutter on any of the four
            // destination edges. A transparent layer must contain no picture.
            if(x<=14 || x>=50 || y<=24 || y>=36)
                Assert::IsTrue(alpha<0.001f,L"transparent panel gutters must not paste an opaque picture tile");
        }
        Assert::IsTrue(visible>0,L"transparent extraction must retain glyphs");
        auto target=input;target.planes[0].texture=allOutput;
        target.color.transfer=PL_COLOR_TRC_LINEAR;
        auto params=pl_render_fast_params;params.dither_params=nullptr;
        Assert::IsTrue(pl_render_image(renderer,&input,&target,&params));
        const auto original=ReadLinear(allOutput);
        pl_overlay overlay{};pl_overlay_part part{};
        overlay.tex=layerTexture;overlay.mode=PL_OVERLAY_NORMAL;
        overlay.coords=PL_OVERLAY_COORDS_DST_FRAME;overlay.repr=layer.repr;overlay.color=layer.color;
        part.src={0,0,64,64};part.dst={0,0,64,64};overlay.parts=&part;overlay.num_parts=1;
        target.planes[0].texture=splitOutput;target.overlays=&overlay;target.num_overlays=1;
        Assert::IsTrue(pl_render_image(renderer,&input,&target,&params));
        const auto composed=ReadLinear(splitOutput);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)
            if(y==8 || y==47 || x==0 || x==63 ||
                (x>=11 && x<=52 && y>=23 && y<=38 &&
                 (x<=14 || x>=50 || y<=24 || y>=36)))
                for(int c=0;c<3;++c)
                    Assert::IsTrue(std::abs(Pixel(original,x,y)[c]-Pixel(composed,x,y)[c])<0.0001f,
                        L"transparent subtitle edges must preserve the actual picture pixels");
    }
    void VerifyViewportClippedCoverage(int mode,bool top,bool rounded) {
        auto geometry=Geometry(false,true);
        geometry.fromTopBar=top;geometry.roundedCorners=rounded;
        geometry.pictureTop=8;geometry.pictureBottom=56;
        geometry.source=geometry.content=geometry.generatedCleanup=geometry.pictureCapture=
            top?SubtitleBoxRect{20,4,44,16}:SubtitleBoxRect{20,48,44,60};
        geometry.glyphLines[0]=geometry.content;
        geometry.placementLimits=top?SubtitleBoxRect{0,2,64,16}:SubtitleBoxRect{0,48,64,62};
        geometry.destination=top?SubtitleBoxRect{24,4,40,16}:SubtitleBoxRect{24,48,40,60};
        geometry.displayPictureLeft=0;geometry.displayPictureRight=64;
        geometry.displayPictureTop=geometry.placementLimits.top;
        geometry.displayPictureBottom=geometry.placementLimits.bottom;
        geometry.layoutCoverage=geometry.generatedCleanup;
        geometry=CoverMeasuredSubtitleBacking(geometry);
        Assert::AreEqual(top?geometry.placementLimits.bottom:geometry.placementLimits.top,
            top?geometry.destination.bottom:geometry.destination.top,
            L"fixture must clip the picture-facing rounded edge");
        pl_frame layer{};
        Assert::IsTrue(BindSubtitleCutPasteHook(hook,geometry,mode,input.color.transfer,
            {},false,SubtitleCompositionPhase::LayerOnly));
        Assert::IsTrue(ComposeSubtitleLayerBeforeCrop(preparation,device->gpu,input,hook,&layerTexture,layer));
        pl_tex_params params{};params.w=params.h=64;params.format=layerTexture->params.format;
        params.blit_dst=true;params.host_readable=true;
        pl_tex staging=pl_tex_create(device->gpu,&params);Assert::IsNotNull(staging);
        pl_tex_blit_params copy{};copy.src=layerTexture;copy.dst=staging;
        pl_tex_blit(device->gpu,&copy);
        const auto pixels=ReadLinear(staging);
        pl_tex_destroy(device->gpu,&staging);
        for(int y=geometry.generatedCleanup.top;y<geometry.generatedCleanup.bottom;++y)
            for(int x=geometry.generatedCleanup.left;x<geometry.generatedCleanup.right;++x)
                Assert::IsTrue(Pixel(pixels,x,y)[3]>0.999f,
                    L"viewport-clipped panel edge must cover all repaired pixels including antialiasing");
        Assert::IsTrue(Pixel(pixels,3,32)[3]<0.001f,L"squaring cannot paint outside the bounded panel");
    }
    void VerifyPreparedBarContentErasure(int mode,bool top) {
        auto geometry=Geometry();
        geometry.fromTopBar=top;geometry.pictureTop=16;geometry.pictureBottom=48;
        geometry.source=top?SubtitleBoxRect{20,4,44,18}:SubtitleBoxRect{20,46,44,60};
        geometry.content=top?SubtitleBoxRect{24,6,40,16}:SubtitleBoxRect{24,48,40,58};
        geometry.glyphLines[0]=geometry.content;
        geometry.generatedCleanup=geometry.pictureCapture=top?
            SubtitleBoxRect{20,16,44,18}:SubtitleBoxRect{20,46,44,48};
        geometry.destination={20,24,44,38};
        geometry.extendToBar=false;
        std::vector<uint8_t> pixels(64*64*4,255);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
            const size_t i=(y*64+x)*4;
            pixels[i]=pixels[i+1]=pixels[i+2]=40;
            if(x>=geometry.content.left && x<geometry.content.right &&
               y>=geometry.content.top && y<geometry.content.bottom) {
                // Thin, antialiased, bright and colored native detail all lie
                // in the exact prepared erasure, independently of worker seeds.
                pixels[i]=uint8_t((x+y)%3==0?250:100);
                pixels[i+1]=uint8_t((x+y)%3==1?180:100);
                pixels[i+2]=uint8_t((x+y)%3==2?220:100);
            }
        }
        const int outsideY=top?10:53;
        const size_t outside=(outsideY*64+22)*4;
        pixels[outside]=240;pixels[outside+1]=120;pixels[outside+2]=80;
        pl_tex_transfer_params upload{};upload.tex=inputTexture;upload.ptr=pixels.data();
        Assert::IsTrue(pl_tex_upload(device->gpu,&upload));
        pl_frame clean{};
        Assert::IsTrue(BindSubtitleCutPasteHook(hook,geometry,mode,input.color.transfer,
            {},false,SubtitleCompositionPhase::CleanupOnly));
        Assert::IsTrue(ComposeSubtitleBeforeCrop(preparation,device->gpu,input,hook,&cleanTexture,clean));
        pl_frame beforeTarget{};beforeTarget.num_planes=1;
        beforeTarget.planes[0].texture=allOutput;beforeTarget.planes[0].components=4;
        for(int c=0;c<4;++c)beforeTarget.planes[0].component_mapping[c]=c;
        beforeTarget.repr=pl_color_repr_rgb;beforeTarget.repr.alpha=PL_ALPHA_NONE;
        beforeTarget.color=input.color;beforeTarget.color.transfer=PL_COLOR_TRC_LINEAR;
        beforeTarget.crop={0,0,64,64};
        auto afterTarget=beforeTarget;afterTarget.planes[0].texture=splitOutput;
        auto params=pl_render_fast_params;params.dither_params=nullptr;
        Assert::IsTrue(pl_render_image(renderer,&input,&beforeTarget,&params));
        Assert::IsTrue(pl_render_image(renderer,&clean,&afterTarget,&params));
        const auto before=ReadLinear(allOutput),after=ReadLinear(splitOutput);
        for(int y=geometry.content.top;y<geometry.content.bottom;++y)
            for(int x=geometry.content.left;x<geometry.content.right;++x)
                for(int c=0;c<3;++c)
                    Assert::IsTrue(Pixel(after,x,y)[c]<0.001f,
                        L"every native pixel in certified bar content must actually be erased by CleanupOnly");
        for(int c=0;c<3;++c) {
            Assert::IsTrue(std::abs(Pixel(before,22,outsideY)[c]-Pixel(after,22,outsideY)[c])<0.005f,
                L"source padding outside exact content cannot be treated as erased bar pixels");
            Assert::IsTrue(std::abs(Pixel(before,3,32)[c]-Pixel(after,3,32)[c])<0.005f,
                L"prepared cleanup must preserve unrelated picture");
        }
    }
    void VerifySurroundDonors(bool barAttached) {
        auto g=Geometry();g.source=g.content=g.generatedCleanup={16,24,48,56};
        g.glyphLines[0]={};g.pictureCapture={};g.destination={20,12,44,20};
        g.pictureTop=4;g.pictureBottom=barAttached?56:64;
        std::vector<uint8_t> pixels(64*64*4,255);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
            const size_t i=(y*64+x)*4;
            const bool left=x<16,right=x>=48,above=y<24,below=y>=56;
            pixels[i]=right?200:below?180:0;
            pixels[i+1]=above?180:below?180:0;
            pixels[i+2]=left?200:0;
            if(y<g.pictureTop || y>=g.pictureBottom) {
                // Deliberately bright poison in excluded bars: must not become a donor.
                pixels[i]=pixels[i+1]=pixels[i+2]=255;
            }
        }
        pl_tex_transfer_params up{};up.tex=inputTexture;up.ptr=pixels.data();
        Assert::IsTrue(pl_tex_upload(device->gpu,&up));
        const auto signature=hook->signature;
        auto render=[&](int fill) {
            const auto started=std::chrono::steady_clock::now();
            Assert::IsTrue(BindSubtitleCutPasteHook(hook,g,3,input.color.transfer,{},fill,SubtitleCompositionPhase::CleanupOnly));
            Assert::AreEqual(signature,hook->signature,L"Dynamic fill selection must retain parsed shader identity");
            pl_frame clean{};
            Assert::IsTrue(ComposeSubtitleBeforeCrop(preparation,device->gpu,input,hook,&cleanTexture,clean));
            pl_frame target{};target.num_planes=1;target.planes[0].texture=allOutput;target.planes[0].components=4;
            for(int c=0;c<4;++c)target.planes[0].component_mapping[c]=c;
            target.repr=pl_color_repr_rgb;target.repr.alpha=PL_ALPHA_NONE;target.color=input.color;target.crop={0,0,64,64};
            auto params=pl_render_fast_params;params.dither_params=nullptr;
            Assert::IsTrue(pl_render_image(renderer,&clean,&target,&params));
            auto pixels=ReadLinear(allOutput);
            const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            Logger::WriteMessage((std::string("spatial render mode=")+std::to_string(fill)+" wall_ms="+std::to_string(ms)).c_str());
            return pixels;
        };
        const auto mirrored=render(0),blended=render(2),again=render(2);
        const auto left=Pixel(blended,16,40),right=Pixel(blended,47,40),top=Pixel(blended,32,24);
        Assert::IsTrue(left[2]>left[0]+0.1f && left[2]>left[1]+0.1f,L"left clean edge must contribute blue");
        Assert::IsTrue(right[0]>right[1]+0.1f && right[0]>right[2]+0.1f,L"right clean edge must contribute red");
        Assert::IsTrue(top[1]>top[0]+0.1f && top[1]>top[2]+0.1f,L"upper clean edge must contribute green");
        const auto bottom=Pixel(blended,32,55);
        if(!barAttached)Assert::IsTrue(bottom[0]>0.4f && bottom[1]>0.4f && bottom[2]<0.1f,L"fourth edge must contribute when real picture is available");
        else Assert::IsTrue(bottom[0]<0.6f && bottom[1]<0.6f && bottom[2]<0.6f,L"bar poison cannot enter reconstruction");
        for(size_t n=0;n<blended.size();++n)Assert::IsTrue(std::isfinite(blended[n]) && std::abs(blended[n]-again[n])<0.001f);
        for(int y=4;y<g.pictureBottom;++y)for(int x=0;x<64;++x)
            if(x<16 || x>=48 || y<24 || y>=56)for(int c=0;c<3;++c)
                Assert::IsTrue(std::abs(Pixel(blended,x,y)[c]-float(pixels[(y*64+x)*4+c])/255)<0.005f,L"spatial fill must not change unrelated picture");
    }
    void VerifyFreshBackingLipCleanupAndOriginalWidth(int mode,bool top,int reduction,int spatialFill=0,bool transparent=false) {
        const auto mirror=[&](SubtitleBoxRect r) {
            return top?SubtitleBoxRect{r.left,64-r.bottom,r.right,64-r.top}:r;
        };
        SubtitleCutPasteGeometry base;
        base.valid=true;base.source=mirror({22,50,42,58});base.content=mirror({26,51,38,57});
        base.glyphLines[0]=base.content;base.layoutContent=base.content;
        base.destination=mirror({20,32,44,44});base.pictureTop=16;base.pictureBottom=48;
        base.displayPictureLeft=0;base.displayPictureRight=64;base.displayPictureTop=16;base.displayPictureBottom=48;
        base.placementLimits={0,16,64,48};base.fromTopBar=top;
        base.roundedCorners=false;base.extendToBar=true;
        const SubtitleBoxPadding padding{6,2,2};
        SubtitleGeneratedGrayStyle style;
        if(transparent) {
            style.opacity=0;style.blurPixels=0;style.borderOpacity=0;style.maxLuminance=1;
            style.textColor={0.7f,0.8f,0.0f};
        }
        base=ApplySubtitleTextReduction(base,reduction,padding,false,false);
        SubtitleBoxPreview preview;preview.available=true;preview.policyGeneration=7;preview.continuityGeneration=8;
        preview.current.identity={2,2,2,83416,2,3,4};preview.current.width=preview.current.height=64;
        preview.current.pictureTop=16;preview.current.pictureBottom=48;preview.current.analyzed=preview.current.barAuthority=true;
        auto& text=preview.current.text;text.detected=true;text.cue=15;text.observations=20;
        text.lineCount=1;text.bounds=text.anchor=text.lineBounds[0]=base.content;
        text.signature.fill(0x55);text.anchorSignature.fill(0x55);text.lineSignatures[0].fill(0x55);
        SubtitleLayoutAnchor finalAnchor;
        base=finalAnchor.Consume(base,text,preview,padding,4,false);
        Assert::IsTrue(base.valid && !base.pictureCapture.Valid() && !base.pictureIntrusion && !base.generatedCleanup.Valid());
        const auto original=base;
        base=PreserveExtendedSubtitleBackingWidth(base,padding,true);
        Assert::AreEqual(24,base.destination.right-base.destination.left,
            L"Explicit extension keeps the original twelve-pixel glyph envelope plus both native paddings, at every reduction.");
        Assert::AreEqual(original.glyphScale,base.glyphScale);Assert::AreEqual(original.glyphTranslateX,base.glyphTranslateX);
        Assert::AreEqual(original.glyphTranslateY,base.glyphTranslateY);
        // Geometry-to-shader integration isolates a trusted finite proof here;
        // the separate native proof suite validates the detector, pixels and budgets.
        CurrentSubtitleBackingLipProof proof;proof.valid=true;proof.identity=preview.current.identity;
        proof.policyGeneration=7;proof.continuityGeneration=8;proof.width=proof.height=64;
        proof.pictureTop=16;proof.pictureBottom=48;proof.fromTopBar=top;
        proof.measured=proof.cleanup=proof.blackInterior=mirror({20,45,44,48});
        const auto geometry=ApplyCurrentSubtitleBackingLip(base,proof,preview.current.identity,7,8);
        Assert::IsTrue(geometry.generatedCleanup.Valid() && !geometry.pictureCapture.Valid() && !geometry.pictureIntrusion);
        Assert::AreEqual(base.source.left,geometry.source.left);Assert::AreEqual(base.source.right,geometry.source.right);
        Assert::AreEqual(base.content.left,geometry.content.left);Assert::AreEqual(base.content.right,geometry.content.right);
        for(int i=0;i<3;++i) {
            Assert::AreEqual(base.glyphLines[i].top,geometry.glyphLines[i].top);
            Assert::AreEqual(base.glyphLines[i].bottom,geometry.glyphLines[i].bottom);
        }
        std::vector<uint8_t> pixels(64*64*4,255);
        auto upload=[&](bool lip) {
            for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
                const auto inside=[&](SubtitleBoxRect r) {return x>=r.left && x<r.right && y>=r.top && y<r.bottom;};
                const bool glyph=inside(geometry.content);
                const uint8_t value=glyph?250:lip&&inside(proof.measured)?0:y>=16&&y<48?80:0;
                const size_t i=(y*64+x)*4;pixels[i]=pixels[i+1]=pixels[i+2]=value;
            }
            pl_tex_transfer_params transfer{};transfer.tex=inputTexture;transfer.ptr=pixels.data();
            Assert::IsTrue(pl_tex_upload(device->gpu,&transfer));
        };
        const auto target=[&](pl_tex texture) {
            pl_frame output{};output.num_planes=1;output.planes[0].texture=texture;output.planes[0].components=4;
            for(int c=0;c<4;++c)output.planes[0].component_mapping[c]=c;
            output.repr=pl_color_repr_rgb;output.repr.alpha=PL_ALPHA_NONE;
            output.color=input.color;output.color.transfer=PL_COLOR_TRC_LINEAR;output.crop={0,0,64,64};return output;
        };
        auto params=pl_render_fast_params;params.dither_params=nullptr;
        auto beforeTarget=target(allOutput),afterTarget=target(splitOutput);
        upload(true);pl_frame clean{};
        Assert::IsTrue(BindSubtitleCutPasteHook(hook,geometry,mode,input.color.transfer,style,spatialFill,SubtitleCompositionPhase::CleanupOnly));
        Assert::IsTrue(ComposeSubtitleBeforeCrop(preparation,device->gpu,input,hook,&cleanTexture,clean));
        Assert::IsTrue(pl_render_image(renderer,&input,&beforeTarget,&params));
        Assert::IsTrue(pl_render_image(renderer,&clean,&afterTarget,&params));
        const auto before=ReadLinear(allOutput),after=ReadLinear(splitOutput);
        for(int y=16;y<48;++y)for(int x=0;x<64;++x)for(int c=0;c<3;++c) {
            const auto& lip=proof.measured;const bool inside=x>=lip.left&&x<lip.right&&y>=lip.top&&y<lip.bottom;
            if(inside) {
                const float value=Pixel(after,x,y)[c];
                // Both modes reconstruct the vacated native picture. Black mode owns only the relocated backdrop color.
                const bool repaired=value>0.02f;
                if(!repaired) {
                    const auto message=std::wstring(L"lip cleanup mode=")+std::to_wstring(mode)+L" top="+std::to_wstring(top)+
                        L" reduction="+std::to_wstring(reduction)+L" x="+std::to_wstring(x)+L" y="+std::to_wstring(y)+
                        L" channel="+std::to_wstring(c)+L" value="+std::to_wstring(value)+
                        L" before="+std::to_wstring(Pixel(before,x,y)[c]);
                    Logger::WriteMessage(message.c_str());Assert::IsTrue(repaired,message.c_str());
                }
            }
            else Assert::IsTrue(std::abs(Pixel(before,x,y)[c]-Pixel(after,x,y)[c])<0.005f,
                L"Fresh lip proof cannot modify unrelated native picture pixels.");
        }
        for(int y=geometry.content.top;y<geometry.content.bottom;++y)
            for(int x=geometry.content.left;x<geometry.content.right;++x)
                Assert::IsTrue(Pixel(after,x,y)[0]<0.001f,L"Original bar glyph erasure remains the unchanged exact content path.");
        pl_frame layer{};
        Assert::IsTrue(BindSubtitleCutPasteHook(hook,geometry,mode,input.color.transfer,style,spatialFill,SubtitleCompositionPhase::LayerOnly));
        Assert::IsTrue(ComposeSubtitleLayerBeforeCrop(preparation,device->gpu,input,hook,&layerTexture,layer));
        pl_tex_params stagingParams{};stagingParams.w=stagingParams.h=64;stagingParams.format=layerTexture->params.format;
        stagingParams.blit_dst=true;stagingParams.host_readable=true;
        pl_tex staging=pl_tex_create(device->gpu,&stagingParams);Assert::IsNotNull(staging);
        pl_tex_blit_params copy{};copy.src=layerTexture;copy.dst=staging;pl_tex_blit(device->gpu,&copy);
        const auto layerPixels=ReadLinear(staging);pl_tex_destroy(device->gpu,&staging);
        const int centerY=(geometry.destination.top+geometry.destination.bottom)/2;
        if(transparent) {
            for(int y=geometry.destination.top;y<geometry.destination.bottom;++y) {
                Assert::IsTrue(Pixel(layerPixels,geometry.destination.left+1,y)[3]<0.001f);
                Assert::IsTrue(Pixel(layerPixels,geometry.destination.right-1,y)[3]<0.001f);
            }
            // Direct composition must repair the old cap just as CleanupOnly
            // does, while the new yellow glyph layer leaves its gutters clear.
            Assert::IsTrue(BindSubtitleCutPasteHook(hook,geometry,mode,input.color.transfer,style,spatialFill,SubtitleCompositionPhase::All));
            pl_frame all{};
            Assert::IsTrue(ComposeSubtitleBeforeCrop(preparation,device->gpu,input,hook,&allTexture,all));
            Assert::IsTrue(pl_render_image(renderer,&all,&beforeTarget,&params));
            const auto full=ReadLinear(allOutput);
            for(int y=proof.measured.top;y<proof.measured.bottom;++y)
                for(int x=proof.measured.left;x<proof.measured.right;++x)for(int c=0;c<3;++c)
                    Assert::IsTrue(std::abs(Pixel(full,x,y)[c]-Pixel(after,x,y)[c])<0.005f,
                        L"Transparent direct composition must preserve the independently repaired source backing");
        } else for(int x=geometry.destination.left+1;x<geometry.destination.right-1;++x)
            Assert::IsTrue(Pixel(layerPixels,x,centerY)[3]>0.999f,L"Reduced text must retain the original-size opaque backing span.");
        Assert::IsTrue(Pixel(layerPixels,3,32)[3]<0.001f);
        if(mode==3)for(int c=0;c<3;++c) {
            // The layer stores source-transfer RGB, while style colors are linear.
            const float linear=style.color[c];
            const float expected=linear<=0.0031308f?12.92f*linear:1.055f*std::pow(linear,1.0f/2.4f)-0.055f;
            Assert::IsTrue(std::abs(Pixel(layerPixels,geometry.destination.left+1,centerY)[c]-expected)<0.001f,
                L"Black mode reconstructs the old lip while the relocated backdrop keeps its configured color.");
        }
        auto nextIdentity=preview.current.identity;++nextIdentity.acceptedSequence;++nextIdentity.sourceFrameNumber;
        nextIdentity.captureTimestamp+=41708;upload(false);
        const auto noLip=ApplyCurrentSubtitleBackingLip(base,proof,nextIdentity,7,8);
        Assert::IsFalse(noLip.generatedCleanup.Valid());
        Assert::IsTrue(BindSubtitleCutPasteHook(hook,noLip,mode,input.color.transfer,style,spatialFill,SubtitleCompositionPhase::CleanupOnly));
        Assert::IsTrue(ComposeSubtitleBeforeCrop(preparation,device->gpu,input,hook,&cleanTexture,clean));
        Assert::IsTrue(pl_render_image(renderer,&input,&beforeTarget,&params));
        Assert::IsTrue(pl_render_image(renderer,&clean,&afterTarget,&params));
        const auto freshBefore=ReadLinear(allOutput),freshAfter=ReadLinear(splitOutput);
        for(int y=proof.measured.top;y<proof.measured.bottom;++y)for(int x=proof.measured.left;x<proof.measured.right;++x)
            for(int c=0;c<3;++c)Assert::IsTrue(std::abs(Pixel(freshBefore,x,y)[c]-Pixel(freshAfter,x,y)[c])<0.005f,
                L"A next source with no lip cannot reuse the old source cleanup or erase its current picture.");
    }

    std::vector<float> ReadPreparedRaster(pl_frame composed,bool encoded=false) {
        if(encoded)composed.color.transfer=PL_COLOR_TRC_LINEAR;
        composed.crop={0,0,64,64};composed.rotation=PL_ROTATION_0;
        pl_frame target{};target.num_planes=1;target.planes[0].texture=allOutput;
        target.planes[0].components=4;
        for(int c=0;c<4;++c)target.planes[0].component_mapping[c]=c;
        target.repr=pl_color_repr_rgb;target.repr.alpha=PL_ALPHA_INDEPENDENT;
        target.color=input.color;target.color.transfer=PL_COLOR_TRC_LINEAR;
        target.crop={0,0,64,64};
        auto params=pl_render_fast_params;params.dither_params=nullptr;
        params.background=PL_CLEAR_SKIP;params.background_transparency=1.0f;
        Assert::IsTrue(pl_render_image(renderer,&composed,&target,&params),L"Prepared raster readback failed");
        return ReadLinear(allOutput);
    }
    void VerifyOpaquePreparationCoverage() {
        const auto original=input;
        input.repr.alpha=PL_ALPHA_NONE;
        input.crop={4,8,60,56};input.rotation=PL_ROTATION_90;
        auto geometry=Geometry();geometry.valid=false;
        for(int phase=0;phase<3;++phase) {
            const auto* selectedHook=phase==2?nullptr:hook;
            Assert::IsTrue(BindSubtitleCutPasteHook(hook,geometry,3,input.color.transfer,{},
                0,phase==1?SubtitleCompositionPhase::CleanupOnly:SubtitleCompositionPhase::All),
                L"Opaque coverage hook binding failed");
            std::vector<float> opaque;
            for(int alpha:{255,0,127}) {
                std::vector<uint8_t> pixels(64*64*4);
                for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
                    const size_t i=(y*64+x)*4;
                    pixels[i]=uint8_t(40+x);pixels[i+1]=uint8_t(50+y);
                    pixels[i+2]=uint8_t(60+(x+y)/2);pixels[i+3]=uint8_t(alpha);
                }
                pl_tex_transfer_params upload{};upload.tex=inputTexture;upload.ptr=pixels.data();
                Assert::IsTrue(pl_tex_upload(device->gpu,&upload),L"Opaque coverage upload failed");
                if(alpha!=255 || phase!=0) {
                    const float poison[]={17.0f,-11.0f,31.0f,0.25f};
                    Assert::IsTrue(allTexture->params.blit_dst,L"Destination poison requires the production temporal-copy surface option");
                    pl_tex_clear(device->gpu,allTexture,poison);
                }
                pl_frame composed{};
                Assert::IsTrue(RenderSubtitleFullRaster(preparation,device->gpu,input,selectedHook,
                    &allTexture,composed,false,true),L"Opaque HLG preparation failed");
                Assert::IsTrue(composed.repr.alpha==PL_ALPHA_NONE && composed.rotation==input.rotation &&
                    composed.crop.x0==input.crop.x0 && composed.crop.y0==input.crop.y0 &&
                    composed.crop.x1==input.crop.x1 && composed.crop.y1==input.crop.y1,
                    L"Full-raster preparation must restore opaque source crop/rotation");
                Assert::IsTrue(composed.color.transfer==input.color.transfer &&
                    composed.color.primaries==input.color.primaries &&
                    composed.color.hdr.min_luma==input.color.hdr.min_luma &&
                    composed.color.hdr.max_luma==input.color.hdr.max_luma,
                    L"Preparation must retain source transfer and HDR metadata");
                const auto output=ReadPreparedRaster(composed);
                for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
                    const auto p=Pixel(output,x,y);
                    for(float value:p)Assert::IsTrue(std::isfinite(value),L"Preparation produced a non-finite pixel");
                    Assert::IsTrue(p[0]>0.0001f && p[1]>0.0001f && p[2]>0.0001f &&
                        std::abs(p[3]-1.0f)<0.001f,L"All native pixels, including outside the selected crop, must be written opaque");
                }
                if(alpha==255)opaque=output;
                else for(size_t i=0;i<output.size();++i)
                    Assert::IsTrue(std::abs(output[i]-opaque[i])<0.001f,
                        L"Opaque source metadata must ignore stored alpha and overwrite poisoned destination pixels");
            }
        }
        input=original;
    }
    void VerifyRealSourceAlphaRemainsComposited() {
        const auto original=input;auto geometry=Geometry();geometry.valid=false;
        Assert::IsTrue(BindSubtitleCutPasteHook(hook,geometry,3,input.color.transfer),
            L"Real-alpha control hook binding failed");
        for(auto alphaMode:{PL_ALPHA_INDEPENDENT,PL_ALPHA_PREMULTIPLIED}) {
            input.repr.alpha=alphaMode;
            std::vector<float> opaque;
            for(int alpha:{255,127,0}) {
                const uint8_t color=uint8_t(alphaMode==PL_ALPHA_PREMULTIPLIED?160*alpha/255:160);
                std::vector<uint8_t> pixels(64*64*4);
                for(size_t i=0;i<pixels.size();i+=4) {
                    pixels[i]=pixels[i+1]=pixels[i+2]=color;pixels[i+3]=uint8_t(alpha);
                }
                pl_tex_transfer_params upload{};upload.tex=inputTexture;upload.ptr=pixels.data();
                Assert::IsTrue(pl_tex_upload(device->gpu,&upload),L"Real-alpha control upload failed");
                pl_frame composed{};
                Assert::IsTrue(ComposeSubtitleBeforeCrop(preparation,device->gpu,input,hook,&allTexture,composed),
                    L"Real source alpha must retain the original opaque-background composition");
                Assert::IsTrue(composed.repr.alpha==PL_ALPHA_NONE,L"Opaque preparation target metadata changed");
                // Compare native encoded pixels against a separate no-hook render
                // with the original opaque-background policy and same metadata.
                const auto output=ReadPreparedRaster(composed,true);
                pl_frame reference{};reference.num_planes=1;
                reference.planes[0].texture=allOutput;reference.planes[0].components=4;
                for(int c=0;c<4;++c)reference.planes[0].component_mapping[c]=c;
                reference.repr=pl_color_repr_rgb;reference.repr.alpha=PL_ALPHA_NONE;
                reference.color=input.color;reference.crop={0,0,64,64};
                auto params=pl_render_fast_params;params.dither_params=nullptr;
                Assert::IsTrue(pl_render_image(renderer,&input,&reference,&params),
                    L"Independent original alpha-background reference failed");
                const auto expected=ReadLinear(allOutput);
                if(alpha==255)opaque=output;
                for(size_t i=0;i<output.size();i+=4) {
                    for(int c=0;c<4;++c)Assert::IsTrue(std::isfinite(output[i+c]));
                    Assert::IsTrue(std::abs(output[i+3]-1.0f)<0.001f,L"Real-alpha input must still flatten to an opaque target");
                    for(int c=0;c<3;++c) {
                        Assert::IsTrue(std::abs(output[i+c]-expected[i+c])<0.001f,
                            L"Real alpha must match the original background policy, including HLG encoded black");
                        if(alpha==127)Assert::IsTrue(output[i+c]>0.001f && output[i+c]<opaque[i+c]-0.001f,
                            L"Partial source alpha cannot be ignored by the opaque-HLG workaround");
                    }
                }
            }
        }
        input=original;
    }

    void Run(int backgroundMode,bool overlap=false,bool narrow=false,bool cover=false,int spatialFill=0) {
        auto geometry=Geometry(overlap,narrow);
        if(cover) {
            geometry.placementLimits={0,8,64,62};
            geometry.layoutCoverage=geometry.generatedCleanup;
            geometry=CoverMeasuredSubtitleBacking(geometry);
        }
        SubtitleGeneratedGrayStyle style;
        style.blurPixels=backgroundMode==5?4.0f:0.0f;
        style.borderWidth=2.0f;style.borderOpacity=0.65f;
        pl_frame all{},clean{},layer{};
        Assert::IsTrue(BindSubtitleCutPasteHook(hook,geometry,backgroundMode,input.color.transfer,
            style,spatialFill,SubtitleCompositionPhase::All),L"All-phase hook binding failed");
        Assert::IsTrue(ComposeSubtitleBeforeCrop(preparation,device->gpu,input,hook,&allTexture,all),L"All-phase composition failed");
        Assert::IsTrue(BindSubtitleCutPasteHook(hook,geometry,backgroundMode,input.color.transfer,
            style,spatialFill,SubtitleCompositionPhase::CleanupOnly),L"Cleanup-phase hook binding failed");
        Assert::IsTrue(ComposeSubtitleBeforeCrop(preparation,device->gpu,input,hook,&cleanTexture,clean),L"Cleanup-phase composition failed");
        Assert::IsTrue(BindSubtitleCutPasteHook(hook,geometry,backgroundMode,input.color.transfer,
            style,spatialFill,SubtitleCompositionPhase::LayerOnly),L"Layer-phase hook binding failed");
        Assert::IsTrue(ComposeSubtitleLayerBeforeCrop(preparation,device->gpu,input,hook,&layerTexture,layer),L"Layer-phase composition failed");
        Assert::IsTrue(layer.repr.alpha==PL_ALPHA_INDEPENDENT,L"Layer alpha metadata was lost");
        pl_tex_params stagingParams{};stagingParams.w=stagingParams.h=64;
        stagingParams.format=layerTexture->params.format;
        stagingParams.blit_dst=true;stagingParams.host_readable=true;
        pl_tex staging=pl_tex_create(device->gpu,&stagingParams);
        Assert::IsTrue(staging!=nullptr,L"Layer readback staging allocation failed");
        pl_tex_blit_params copy{};copy.src=layerTexture;copy.dst=staging;
        pl_tex_blit(device->gpu,&copy);
        const pl_fmt layerFormat=staging->params.format;
        Assert::IsTrue(layerFormat->num_components==4 &&
            layerFormat->sample_order[3]==3 &&
            (layerFormat->host_bits[3]==16 || layerFormat->host_bits[3]==32),
            L"Unexpected RGBA float host format");
        std::vector<uint8_t> layerBytes(64*64*layerFormat->texel_size);
        pl_tex_transfer_params readLayer{};readLayer.tex=staging;readLayer.ptr=layerBytes.data();
        const bool downloaded=pl_tex_download(device->gpu,&readLayer);
        pl_tex_destroy(device->gpu,&staging);
        Assert::IsTrue(downloaded,L"Layer readback failed");
        const auto alphaAt=[&](size_t pixel) {
            const uint8_t* const channel=layerBytes.data()+pixel*layerFormat->texel_size+
                3*(layerFormat->host_bits[3]/8);
            if(layerFormat->host_bits[3]==32) {
                float value=0;std::memcpy(&value,channel,sizeof(value));return value;
            }
            uint16_t half=0;std::memcpy(&half,channel,sizeof(half));
            const int exponent=(half>>10)&31,significand=half&1023;
            return exponent?std::ldexp(1.0f+significand/1024.0f,exponent-15):
                std::ldexp(float(significand),-24);
        };
        if(cover) {
            for(int y=geometry.generatedCleanup.top;y<geometry.generatedCleanup.bottom;++y)
                for(int x=geometry.generatedCleanup.left;x<geometry.generatedCleanup.right;++x)
                    Assert::IsTrue(alphaAt(y*64+x)>0.999f,
                        L"every repaired card pixel, including rounded corners and bar edge, must have overlay coverage");
        }
        const float outsideAlpha=alphaAt(3*64+3);
        const int interiorX=(geometry.destination.left+geometry.destination.right)/2;
        const int interiorY=(geometry.destination.top+geometry.destination.bottom)/2;
        const float insideAlpha=alphaAt(interiorY*64+interiorX);
        int alphaCount=0,minAlphaX=64,minAlphaY=64,maxAlphaX=-1,maxAlphaY=-1;
        int partialAlphaX=-1,partialAlphaY=-1;
        float maxAlpha=0.0f;
        for(int row=0;row<64;++row)for(int col=0;col<64;++col) {
            const float a=alphaAt(row*64+col);
            if(a>0.001f) {
                ++alphaCount;minAlphaX=std::min(minAlphaX,col);minAlphaY=std::min(minAlphaY,row);
                maxAlphaX=std::max(maxAlphaX,col);maxAlphaY=std::max(maxAlphaY,row);
                maxAlpha=std::max(maxAlpha,a);
            }
        }
        Assert::IsTrue(outsideAlpha<0.001f,L"Layer is opaque outside its destination");
        Assert::IsTrue(insideAlpha>0.5f,(std::wstring(L"Layer lost its interior alpha; inside=")+std::to_wstring(insideAlpha)+L" count="+std::to_wstring(alphaCount)+L" max="+std::to_wstring(maxAlpha)+L" bbox="+std::to_wstring(minAlphaX)+L","+std::to_wstring(minAlphaY)+L".."+std::to_wstring(maxAlphaX)+L","+std::to_wstring(maxAlphaY)+L" mode="+std::to_wstring(backgroundMode)+L" transfer="+std::to_wstring(int(input.color.transfer))+L" overlap="+std::to_wstring(overlap)+L" texel="+std::to_wstring(layerFormat->texel_size)+L" host="+std::to_wstring(layerFormat->host_bits[3])).c_str());
        bool partialAlpha=false;
        for(size_t pixel=0;pixel<64*64;++pixel) {
            const float alpha=alphaAt(pixel);
            if(alpha>0.001f && alpha<0.999f && partialAlphaX<0) {
                partialAlphaX=int(pixel%64);partialAlphaY=int(pixel/64);
            }
            partialAlpha|=alpha>0.001f && alpha<0.999f;
        }
        Assert::IsTrue(partialAlpha,L"Rounded panel/border has no partially transparent edge");
        pl_frame allTarget{};allTarget.num_planes=1;
        allTarget.planes[0].texture=allOutput;allTarget.planes[0].components=4;
        for(int c=0;c<4;++c)allTarget.planes[0].component_mapping[c]=c;
        allTarget.repr=pl_color_repr_rgb;allTarget.repr.alpha=PL_ALPHA_NONE;
        allTarget.color=input.color;allTarget.color.transfer=PL_COLOR_TRC_LINEAR;
        allTarget.crop={0,0,64,64};
        pl_frame splitTarget=allTarget;splitTarget.planes[0].texture=splitOutput;
        pl_overlay overlay{};pl_overlay_part part{};
        overlay.tex=layerTexture;overlay.mode=PL_OVERLAY_NORMAL;
        overlay.coords=PL_OVERLAY_COORDS_DST_FRAME;
        overlay.repr=layer.repr;overlay.color=layer.color;
        part.src={0,0,64,64};part.dst={0,0,64,64};
        overlay.parts=&part;overlay.num_parts=1;
        splitTarget.overlays=&overlay;splitTarget.num_overlays=1;
        auto params=pl_render_fast_params;params.dither_params=nullptr;
        pl_frame cleanTarget=splitTarget;cleanTarget.overlays=nullptr;cleanTarget.num_overlays=0;
        Assert::IsTrue(pl_render_image(renderer,&input,&allTarget,&params));
        Assert::IsTrue(pl_render_image(renderer,&clean,&cleanTarget,&params));
        const auto originalSource=ReadLinear(allOutput),cleanedSource=ReadLinear(splitOutput);
        const auto outsideBefore=Pixel(originalSource,3,3),outsideAfter=Pixel(cleanedSource,3,3);
        for(int c=0;c<3;++c)
            Assert::IsTrue(std::abs(outsideBefore[c]-outsideAfter[c])<=0.005f,
                L"Cleanup changed pixels outside the subtitle footprint");
        const auto glyphBefore=Pixel(originalSource,30,53),glyphAfter=Pixel(cleanedSource,30,53);
        Assert::IsTrue(glyphBefore[0]-glyphAfter[0]>0.03f,
            L"Cleanup failed to remove the original glyph");
        Assert::IsTrue(pl_render_image(renderer,&all,&allTarget,&params));
        Assert::IsTrue(pl_render_image(renderer,&clean,&splitTarget,&params));
        const auto original=ReadLinear(allOutput),split=ReadLinear(splitOutput);
        float worst=0.0f,worstAllowed=0.0f;
        int worstX=0,worstY=0,worstChannel=0;
        for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
            const auto a=Pixel(original,x,y),b=Pixel(split,x,y);
            for(int c=0;c<3;++c) {
                const float difference=std::abs(a[c]-b[c]);
                const float allowed=(std::max)(0.008f,
                    0.005f*(std::max)(std::abs(a[c]),std::abs(b[c])));
                if(difference/allowed>worst/(std::max)(worstAllowed,0.000001f)) {
                    worst=difference;worstAllowed=allowed;
                    worstX=x;worstY=y;worstChannel=c;
                }
            }
        }
        const std::wstring mismatch=L"Split composition mismatch at ("+
            std::to_wstring(worstX)+L","+std::to_wstring(worstY)+L") channel "+
            std::to_wstring(worstChannel)+L" difference "+std::to_wstring(worst)+
            L" allowed "+std::to_wstring(worstAllowed);
        Assert::IsTrue(worst<=worstAllowed,mismatch.c_str());
        if(partialAlphaX>=0) {
            const auto cornerAll=Pixel(original,partialAlphaX,partialAlphaY);
            const auto cornerSplit=Pixel(split,partialAlphaX,partialAlphaY);
            for(int c=0;c<3;++c)
                Assert::IsTrue(std::abs(cornerAll[c]-cornerSplit[c])<=0.008f,
                    L"rounded partial-alpha pixels compose as CleanupOnly plus LayerOnly");
        }
        if(backgroundMode==3 && narrow) {
            const auto repaired=Pixel(cleanedSource,21,53);
            const auto untouched=Pixel(cleanedSource,10,53);
            Assert::IsTrue(repaired[0]>0.005f && std::abs(repaired[0]-untouched[0])<0.02f,
                L"vacated black-panel cleanup uses the current nonblack donor instead of zero");
            const auto blackPanel=Pixel(original,25,40);
            const auto preservedGlyph=Pixel(original,30,38);
            Assert::IsTrue(blackPanel[0]<0.01f && blackPanel[1]<0.01f && blackPanel[2]<0.01f,
                L"Black mode keeps the displayed panel black");
            Assert::IsTrue(preservedGlyph[0]>0.5f,
                L"Black mode preserves the relocated glyph over the repaired source");
        }
        if(backgroundMode==3 && !overlap && !narrow) {
            auto disabled=geometry;disabled.valid=false;
            Assert::IsTrue(BindSubtitleCutPasteHook(hook,disabled,backgroundMode,
                input.color.transfer,style,spatialFill,SubtitleCompositionPhase::LayerOnly));
            pl_frame emptyLayer{};
            Assert::IsTrue(ComposeSubtitleLayerBeforeCrop(preparation,device->gpu,input,
                hook,&layerTexture,emptyLayer));
            pl_tex_params stagingParams{};stagingParams.w=stagingParams.h=64;
            stagingParams.format=layerTexture->params.format;
            stagingParams.blit_dst=true;stagingParams.host_readable=true;
            pl_tex staging=pl_tex_create(device->gpu,&stagingParams);
            Assert::IsTrue(staging!=nullptr);
            pl_tex_blit_params copy{};copy.src=layerTexture;copy.dst=staging;
            pl_tex_blit(device->gpu,&copy);
            const pl_fmt format=staging->params.format;
            std::vector<uint8_t> bytes(64*64*format->texel_size);
            pl_tex_transfer_params read{};read.tex=staging;read.ptr=bytes.data();
            const bool downloaded=pl_tex_download(device->gpu,&read);
            pl_tex_destroy(device->gpu,&staging);
            Assert::IsTrue(downloaded);
            for(size_t pixel=0;pixel<64*64;++pixel) {
                const float alpha=FloatChannel(bytes.data()+pixel*format->texel_size+
                    3*(format->host_bits[3]/8),format->host_bits[3]);
                Assert::IsTrue(alpha<0.001f,L"Disabled layer left opaque pixels");
            }
        }
    }
};
}
TEST_CLASS(SubtitleCutPasteShaderTests) {
public:
    TEST_METHOD(OpaqueHlgPreparationWritesEveryPixelAndPreservesSourceMetadata) {
        SubtitleShaderGpuFixture gpu(PL_COLOR_TRC_HLG);
        gpu.VerifyOpaquePreparationCoverage();
    }
    TEST_METHOD(RealSourceAlphaRetainsOpaqueBackgroundComposition) {
        for(auto transfer:{PL_COLOR_TRC_SRGB,PL_COLOR_TRC_HLG}) {
            SubtitleShaderGpuFixture gpu(transfer);
            // A supported low-peak HLG target has finite encoded black even on
            // API377, so real-alpha behavior can be tested independently of its
            // separate high-peak black-background encoding defect.
            if(transfer==PL_COLOR_TRC_HLG)gpu.input.color.hdr.max_luma=203.0f;
            gpu.VerifyRealSourceAlphaRemainsComposited();
        }
    }
    TEST_METHOD(TransparentYellowRepairsSourceBackingWithoutRestoringAnOpaqueDestination) {
        SubtitleShaderGpuFixture gpu(PL_COLOR_TRC_SRGB);
        for(bool top:{false,true})for(int reduction:{0,50,75})for(int fill:{0,1,2})
            gpu.VerifyFreshBackingLipCleanupAndOriginalWidth(5,top,reduction,fill,true);
    }
    TEST_METHOD(SubtitlePreparationCannotResetHdrHistoryBetweenRepeatedOutputs) {
        SubtitleShaderGpuFixture gpu(PL_COLOR_TRC_PQ);
        gpu.VerifyPreparationPreservesHdrHistory();
    }
    TEST_METHOD(TransparentLayersPreservePictureAtEveryPanelEdge) {
        for(auto transfer:{PL_COLOR_TRC_SRGB,PL_COLOR_TRC_PQ,PL_COLOR_TRC_HLG}) {
            SubtitleShaderGpuFixture gpu(transfer);
            for(int mode:{1,5})for(bool reduced:{false,true})
                gpu.VerifyTransparentLayerDoesNotCopyPicture(reduced,mode);
        }
    }
    TEST_METHOD(SourceProcessingSnapshotOwnsDebandValues) {
        pl_render_params settings=pl_render_fast_params;
        pl_deband_params deband=pl_deband_default_params;
        deband.iterations=3;deband.threshold=7.0f;
        settings.plane_upscaler=&pl_filter_bicubic;
        settings.plane_downscaler=&pl_filter_bilinear;
        settings.antiringing_strength=0.25f;
        settings.deband_params=&deband;
        const auto snapshot=CaptureSubtitleSourceProcessingOptions(settings);
        deband.iterations=1;deband.threshold=2.0f;
        settings.plane_upscaler=nullptr;settings.deband_params=nullptr;
        Assert::IsTrue(snapshot.planeUpscaler==&pl_filter_bicubic);
        Assert::IsTrue(snapshot.planeDownscaler==&pl_filter_bilinear);
        Assert::IsTrue(snapshot.antiringingStrength==0.25f);
        Assert::IsTrue(snapshot.debandEnabled);
        Assert::AreEqual(3,snapshot.debandParams.iterations);
        Assert::IsTrue(snapshot.debandParams.threshold==7.0f);
    }
    TEST_METHOD(SurroundFillUsesEveryAvailableEdgeAndRejectsBarDonors) {
        SubtitleShaderGpuFixture gpu;
        gpu.VerifySurroundDonors(false);gpu.VerifySurroundDonors(true);
    }
    TEST_METHOD(SurroundFillPreservesNativeCleanupGlyphScaleAndLayerAtBothBars) {
        SubtitleShaderGpuFixture rgb(PL_COLOR_TRC_SRGB);
        for(int mode:{3,5})for(bool top:{false,true})for(int reduction:{0,75})
            rgb.VerifyFreshBackingLipCleanupAndOriginalWidth(mode,top,reduction,2);
    }
    TEST_METHOD(SurroundFillPreservesSplitCompositionAndTransferFunctions) {
        for(auto transfer:{PL_COLOR_TRC_LINEAR,PL_COLOR_TRC_SRGB,PL_COLOR_TRC_PQ,PL_COLOR_TRC_HLG}) {
            SubtitleShaderGpuFixture rgb(transfer);
            for(int mode:{3,5})rgb.Run(mode,false,false,false,2);
        }
    }
    TEST_METHOD(FreshBarOnlyBackingLipAndOriginalWidthUseActualGpuCleanupAndLayer) {
        SubtitleShaderGpuFixture rgb(PL_COLOR_TRC_SRGB);
        for(int mode:{3,5})for(bool top:{false,true})for(int reduction:{0,50,75})
            rgb.VerifyFreshBackingLipCleanupAndOriginalWidth(mode,top,reduction);
    }
    TEST_METHOD(PreparedBarErasureMatchesExactBlackAndReconstructionGpuFootprint) {
        SubtitleShaderGpuFixture rgb(PL_COLOR_TRC_SRGB);
        for(int mode:{3,5})for(bool top:{false,true})
            rgb.VerifyPreparedBarContentErasure(mode,top);
    }
    TEST_METHOD(ViewportClippedBackingHasOpaqueGpuEdgesAtBothBars) {
        SubtitleShaderGpuFixture rgb(PL_COLOR_TRC_SRGB);
        for(int mode:{3,5})for(bool top:{false,true})for(bool rounded:{false,true})
            rgb.VerifyViewportClippedCoverage(mode,top,rounded);
    }
    TEST_METHOD(MeasuredBackingIsCoveredByActualBlackAndReconstructionGpuLayers) {
        SubtitleShaderGpuFixture rgb(PL_COLOR_TRC_SRGB);
        rgb.Run(3,false,true,true);rgb.Run(5,false,true,true);
    }
    TEST_METHOD(BlackAndReconstructionLayersPreserveAlphaAndComposition) {
        for(const auto transfer:{PL_COLOR_TRC_LINEAR,PL_COLOR_TRC_SRGB,PL_COLOR_TRC_PQ,PL_COLOR_TRC_HLG}) {
            SubtitleShaderGpuFixture rgb(transfer);
            rgb.Run(3);rgb.Run(5);
            if(transfer==PL_COLOR_TRC_LINEAR) {
                rgb.Run(3,true);rgb.Run(5,true);
                rgb.Run(3,false,true);rgb.Run(5,false,true);
            }
        }
        SubtitleShaderGpuFixture nv12(PL_COLOR_TRC_SRGB,1);
        nv12.Run(3);nv12.Run(5);
        SubtitleShaderGpuFixture p010(PL_COLOR_TRC_PQ,2);
        p010.Run(3);p010.Run(5);
        p010.Run(3,true);p010.Run(5,true);
    }
};
}
