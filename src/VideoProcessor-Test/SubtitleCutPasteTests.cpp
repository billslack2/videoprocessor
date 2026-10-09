#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleCutPaste.h>
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
    static SubtitleBoxResult Text() {
        SubtitleBoxResult r;r.detected=true;r.cue=1;r.bounds={200,295,400,330};return r;
    }
public:
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
                    Assert::IsTrue(scaled.destination.left<=cleanup.left && scaled.destination.right>=cleanup.right &&
                        (top?scaled.destination.bottom>=cleanup.bottom:scaled.destination.top<=cleanup.top) && scaled.extendToBar,
                        L"smaller glyphs cannot expose any original picture intrusion");
                }
                Assert::AreEqual(top?base.destination.top:base.destination.bottom,
                    top?scaled.destination.top:scaled.destination.bottom,
                    L"offset places the whole box, before extension");
                Assert::IsTrue(std::abs((left+right)-(scaled.destination.left+scaled.destination.right))<0.01f &&
                    std::abs((above+below)-(scaled.destination.top+scaled.destination.bottom))<0.01f,
                    L"glyph envelope is centered inside the padded box");
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
        Assert::AreEqual(290,second.generatedCleanup.top,L"fresh cleanup is not replaced by the frozen display envelope");
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
    TEST_METHOD(SameCueDisplayCannotShrinkButCleanupUsesCurrentPanel) {
        auto p=Preview();auto r=Text();SubtitleCutPastePresentation state;
        r.sourcePanel={160,280,440,315};
        const auto initial=state.Consume(r,p,{5,5,5});
        r.sourcePanel={180,282,420,315};++p.current.identity.acceptedSequence;
        const auto narrower=state.Consume(r,p,{5,5,5});
        Assert::AreEqual(initial.destination.left,narrower.destination.left);
        Assert::AreEqual(initial.destination.right,narrower.destination.right);
        Assert::AreEqual(180,narrower.generatedCleanup.left);
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
    TEST_METHOD(MeasuredCardChangeInvalidatesPresentationGeometry) {
        auto p=Preview();auto r=Text();SubtitleCutPastePresentation state;
        r.sourcePanel={180,275,420,315};
        const auto first=state.Consume(r,p,SubtitleBoxPadding(0));
        Assert::IsTrue(first.valid);
        r.sourcePanel={170,270,430,315};
        const auto changed=state.Consume(r,p,SubtitleBoxPadding(0));
        Assert::AreEqual(170,changed.generatedCleanup.left);
        Assert::AreEqual(270,changed.generatedCleanup.top);
        Assert::AreEqual(430,changed.generatedCleanup.right);
        Assert::AreEqual(first.destination.top,changed.destination.top,
            L"refining the source card must not move the displayed glyphs");
        r.sourcePanel={};
        const auto absent=state.Consume(r,p,SubtitleBoxPadding(0));
        Assert::IsFalse(absent.generatedCleanup.Valid(),
            L"missing measured card must invalidate cleanup rather than borrow the glyph fringe");
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
}; }
