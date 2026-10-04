#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleCutPaste.h>
#include <climits>
#include <ConfigFile.h>
#include <RendererProfileConfig.h>
#include <RendererConfigView.h>
#include <fstream>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests {
TEST_CLASS(SubtitleCutPasteTests) {
    static SubtitleBoxPreview Preview() {
        SubtitleBoxPreview p;p.available=true;p.policyGeneration=7;p.continuityGeneration=8;
        p.current.analyzed=p.current.barAuthority=true;
        p.current.width=640;p.current.height=360;p.current.pictureTop=45;p.current.pictureBottom=315;
        p.current.identity={1,1,1,1000,2,3,4};return p;
    }
    static SubtitleBoxResult Text() {
        SubtitleBoxResult r;r.detected=true;r.cue=1;r.bounds={200,295,400,330};return r;
    }
public:
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
        const auto wider=state.Consume(r,p,SubtitleBoxPadding(60,30,10),15);
        Assert::AreEqual(140,wider.source.left);Assert::AreEqual(460,wider.source.right);
        Assert::AreEqual(initial.destination.top,wider.destination.top);
        const auto taller=state.Consume(r,p,SubtitleBoxPadding(60,50,10),15);
        Assert::AreEqual(wider.destination.top-20,taller.destination.top);
        const auto bottom=state.Consume(r,p,SubtitleBoxPadding(60,50,20),15);
        Assert::AreEqual(taller.source.bottom+10,bottom.source.bottom);
        const auto inset=state.Consume(r,p,SubtitleBoxPadding(60,50,20),25);
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
    TEST_METHOD(GeometryConfigLoadsAtRendererRootAndRejectsInvalidValues) {
        for(const char* key:{"subtitle_box_padding_sides","subtitle_box_padding_top","subtitle_box_padding_bottom","subtitle_move_inset"}) {
            for(const char* value:{"0","30","500","-1","501","2.5","many"}) {
                char directory[MAX_PATH]{},path[MAX_PATH]{};
                Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
                Assert::IsTrue(GetTempFileNameA(directory,"vpp",0,path)!=0);
                {std::ofstream file(path);file<<"[vprenderer]\n"<<key<<": "<<value<<"\n";}
                ConfigFile config;Assert::IsTrue(config.Load(path));DeleteFileA(path);
                RendererProfileConfig::Model model;std::string error,actual;
                const bool expected=std::string(value)=="0" || std::string(value)=="30" || std::string(value)=="500";
                Assert::AreEqual(expected,RendererProfileConfig::Read(config,model,error));
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
        Assert::AreEqual(301,next.destination.bottom);Assert::AreEqual(316,next.pictureBottom);
        p.current.pictureBottom=315;++p.current.identity.viewportGeneration;
        Assert::AreEqual(300,state.Consume(r,p).destination.bottom);
        p.current.pictureBottom=316;++p.policyGeneration;
        Assert::AreEqual(301,state.Consume(r,p).destination.bottom);
        p.current.pictureBottom=315;++p.continuityGeneration;
        Assert::AreEqual(300,state.Consume(r,p).destination.bottom);
    }
    TEST_METHOD(PlacementNeverSurvivesLostCueOrFreshBarAuthority) {
        auto p=Preview();auto r=Text();SubtitleCutPastePresentation state;
        Assert::IsTrue(state.Consume(r,p).valid);
        p.current.barAuthority=false;Assert::IsFalse(state.Consume(r,p).valid);
        p.current.barAuthority=true;p.current.pictureBottom=316;
        Assert::AreEqual(301,state.Consume(r,p).destination.bottom);
        r.detected=false;Assert::IsFalse(state.Consume(r,p).valid);
        r.detected=true;p.current.pictureBottom=315;
        Assert::AreEqual(300,state.Consume(r,p).destination.bottom);
        p.current.discontinuity=true;p.current.pictureBottom=316;
        Assert::AreEqual(301,state.Consume(r,p).destination.bottom);
    }
    TEST_METHOD(DefaultPaddingAddsThirtyTopAndSidesAndTenBelow) {
        const auto r=ExpandSubtitleBox({200,295,400,330},640,360);
        Assert::AreEqual(170,r.left);Assert::AreEqual(265,r.top);
        Assert::AreEqual(430,r.right);Assert::AreEqual(340,r.bottom);
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
    TEST_METHOD(MovePreservesSizeAndHorizontalPositionWithFifteenPixelGap) {
        const auto g=ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315);
        Assert::IsTrue(g.valid);
        Assert::AreEqual(300,g.destination.bottom);Assert::AreEqual(225,g.destination.top);
        Assert::AreEqual(g.source.left,g.destination.left);
        Assert::AreEqual(g.source.right,g.destination.right);
        Assert::AreEqual(g.source.bottom-g.source.top,g.destination.bottom-g.destination.top);
    }
    TEST_METHOD(DestinationWinsOverlapAndUncoveredSourceClears) {
        const auto g=ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315);
        const auto overlap=MapSubtitleCutPastePixel(g,210,290);
        Assert::IsFalse(overlap.clear);Assert::AreEqual(210,overlap.x);Assert::AreEqual(330,overlap.y);
        Assert::IsTrue(MapSubtitleCutPastePixel(g,210,300).clear);
        Assert::IsTrue(MapSubtitleCutPastePixel(g,210,339).clear);
        const auto outside=MapSubtitleCutPastePixel(g,210,340);
        Assert::IsFalse(outside.clear);Assert::AreEqual(340,outside.y);
        Assert::IsFalse(MapSubtitleCutPastePixel(g,g.source.right,300).clear);
    }
    TEST_METHOD(RejectsMissingBarInvalidPictureAndDownwardMove) {
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},640,360,0,360).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,200,400,230},640,360,45,315).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,20,400,40},640,360,45,315).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},640,360,315,45).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},640,360,-1,315).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},640,360,45,361).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({},640,360,45,315).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},0,360,45,315).valid);
    }
    TEST_METHOD(RejectsTooTallCueAndInvalidInsetsRatherThanClippingText) {
        Assert::IsFalse(ComputeSubtitleCutPaste({200,250,400,350},640,360,230,315).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315,-1).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315,10,-1).valid);
        Assert::IsFalse(ComputeSubtitleCutPaste({200,295,400,330},640,360,45,315,10,INT_MAX).valid);
        const auto original=MapSubtitleCutPastePixel({},210,320);
        Assert::IsFalse(original.clear);Assert::AreEqual(210,original.x);Assert::AreEqual(320,original.y);
    }
}; }
