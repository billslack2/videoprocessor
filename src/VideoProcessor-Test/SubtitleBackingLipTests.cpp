#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleBackingLip.h>
#include <SubtitleBackingPresentation.h>
#include <SubtitleBoxLookahead.h>
#include <SubtitleCutPaste.h>
#include <SubtitleMeasurementWorker.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests {
TEST_CLASS(SubtitleBackingLipTests) {
    enum class Format { P010, P210, DecodedDv, Rgb };
    static ActivePictureFrameIdentity Identity(uint64_t sequence) {
        auto id=ActivePictureFrameIdentity{2,sequence,sequence,sequence*41708,2,3,4};
        id.analysisDomainKey=9;return id;
    }
    static bool Same(const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
    }
    static bool Contains(const SubtitleBoxRect& outer,const SubtitleBoxRect& inner) {
        return outer.Valid() && inner.Valid() && outer.left<=inner.left && outer.top<=inner.top &&
            outer.right>=inner.right && outer.bottom>=inner.bottom;
    }
    struct Frame {
        int scale,width,height,pictureTop,pictureBottom,floor;
        Format format;bool top;
        std::vector<uint16_t> planes;
        std::vector<uint8_t> rgba;
        Frame(int s=1,Format f=Format::P010,bool upper=false,int black=64)
            :scale(s),width(640*s),height(360*s),pictureTop(64*s),pictureBottom(296*s),
             floor(black),format(f),top(upper) {
            if(f==Format::Rgb)rgba.resize(size_t(width)*height*4);
            else planes.assign(size_t(width)*height*(f==Format::P010?3:4)/2,uint16_t(512<<6));
            Fill({0,0,width,height},floor);
            Fill({0,pictureTop,width,pictureBottom},300);
            for(int row=0;row<2;++row)for(int n=0;n<12;++n) {
                const int x=(240+13*n)*scale,y=pictureBottom+2+row*28*scale;
                // Hollow native glyphs, not a solid bright text rectangle.
                Fill({x,y,x+8*scale,y+14*scale},700);
                Fill({x+2*scale,y+2*scale,x+6*scale,y+12*scale},floor);
            }
        }
        int Y(int lowerY) const {return top?height-1-lowerY:lowerY;}
        SubtitleBoxRect Rect(SubtitleBoxRect lower) const {
            return top?SubtitleBoxRect{lower.left,height-lower.bottom,lower.right,height-lower.top}:lower;
        }
        void Pixel(int x,int lowerY,int code) {
            if(x<0 || x>=width || lowerY<0 || lowerY>=height)return;
            const auto index=size_t(Y(lowerY))*width+x;
            if(format==Format::Rgb) {
                const auto value=uint8_t((code*255+511)/1023);
                rgba[index*4]=rgba[index*4+1]=rgba[index*4+2]=value;rgba[index*4+3]=255;
            } else {
                // DV keeps low 12-bit residuals that Sample intentionally discards.
                planes[index]=uint16_t((code<<6)|(format==Format::DecodedDv?0x30:0));
            }
        }
        void Fill(SubtitleBoxRect lower,int code) {
            for(int y=lower.top;y<lower.bottom;++y)for(int x=lower.left;x<lower.right;++x)Pixel(x,y,code);
        }
        SubtitleBoxRect Lip(int depth,bool rounded=false) {
            const SubtitleBoxRect lower{224*scale,pictureBottom-depth,416*scale,pictureBottom};
            for(int y=lower.top;y<lower.bottom;++y) {
                // Independently finite, monotonic rounded corner; no hidden holes.
                const int inset=rounded?(std::max)(0,2*scale-(y-lower.top)):0;
                Fill({lower.left+inset,y,lower.right-inset,y+1},floor);
            }
            return Rect(lower);
        }
        void Chroma(SubtitleBoxRect lower,int u,int v) {
            Assert::IsTrue(format!=Format::Rgb);
            const int vertical=format==Format::P010?2:1;
            const auto r=Rect(lower);
            for(int y=r.top/vertical;y<(r.bottom+vertical-1)/vertical;++y)
                for(int x=(r.left/2)*2;x<r.right;x+=2) {
                    const size_t index=size_t(width)*height+size_t(y)*width+x;
                    planes[index]=uint16_t(u<<6);planes[index+1]=uint16_t(v<<6);
                }
        }
        AnalysisLumaSource Source() const {
            AnalysisLumaSource source;source.width=width;source.height=height;source.generation=2;
            if(format==Format::Rgb) {
                source.data=rgba.data();source.dataBytes=rgba.size();source.rowBytes=width*4;
                source.format=AnalysisLumaFormat::NativeRgb;source.encoding=VideoFrameEncoding::BGRA_8BIT;
                source.colorspace=ColorSpace::REC_709;
            } else {
                source.data=reinterpret_cast<const uint8_t*>(planes.data());source.dataBytes=planes.size()*2;
                source.rowBytes=source.chromaRowBytes=width*2;
                source.format=format==Format::P010?AnalysisLumaFormat::P010:
                    format==Format::P210?AnalysisLumaFormat::P210:AnalysisLumaFormat::DecodedDolbyVision422;
            }
            return source;
        }
        SubtitleBoxObservation Measure(uint64_t sequence) const {
            const auto source=Source();SubtitleBoxDetector scanner;
            return SubtitleBoxLookahead::Measure(scanner,source,ExtractActivePictureEvidence(source),
                Identity(sequence),false,{},7,8);
        }
    };
    static SubtitleBoxPreview Resolve(std::initializer_list<SubtitleBoxObservation> list) {
        return SubtitleBoxLookahead::Resolve(list.begin(),list.size(),7,8);
    }
    struct Acquired {
        SubtitleBoxPreview preview;
        SubtitleBoxResult accepted;
        SubtitleBoxPresentation tracker;
    };
    static Acquired Acquire(const Frame& frame) {
        const auto first=frame.Measure(1),second=frame.Measure(2);
        std::ostringstream diagnostic;diagnostic<<"scale="<<frame.scale<<" format="<<int(frame.format)
            <<" top="<<frame.top<<" first="<<first.text.diagnosticReason<<" current="<<second.text.diagnosticReason
            <<" physical="<<second.pictureTop<<":"<<second.pictureBottom<<" lines="<<second.text.lineCount;
        Logger::WriteMessage(diagnostic.str().c_str());
        Assert::IsTrue(second.text.detected && first.text.detected);
        Assert::IsTrue(second.independentBarAnalysis && second.barAuthority && second.barEvidence.available);
        Assert::AreEqual(frame.pictureTop,second.pictureTop);Assert::AreEqual(frame.pictureBottom,second.pictureBottom);
        Assert::IsTrue(second.ink && second.ink->rawEvidenceComplete);
        for(int i=0;i<second.text.lineCount;++i) {
            const auto& line=second.text.lineBounds[i];
            Assert::IsTrue(frame.top?line.bottom<=frame.pictureTop:line.top>=frame.pictureBottom,
                L"Real measured glyphs are wholly in the physical bar; detector display fringe is not ownership.");
        }
        Assert::IsFalse(second.text.sourcePanel.Valid() || second.text.provenSourcePanel.Valid());
        Assert::IsTrue(second.text.capturePanelMeasured && !second.text.capturePanel.Valid());
        SubtitleBoxPresentation tracker;
        Assert::IsTrue(tracker.Consume(Resolve({first,second})).detected);
        Acquired result;result.preview=Resolve({second});result.accepted=tracker.Consume(result.preview);
        Assert::IsTrue(result.accepted.detected && !result.accepted.held && result.accepted.observations>=2);
        result.tracker=tracker;return result;
    }
    static void LogProof(const CurrentSubtitleBackingLipProof& proof,const Frame& frame,const char* group) {
        std::ostringstream out;out<<group<<" scale="<<frame.scale<<" format="<<int(frame.format)<<" top="<<frame.top
            <<" valid="<<proof.valid<<" reason="<<proof.reason<<" samples="<<proof.samples
            <<" measured="<<proof.measured.left<<","<<proof.measured.top<<"-"<<proof.measured.right<<","<<proof.measured.bottom;
        Logger::WriteMessage(out.str().c_str());
    }
    static SubtitleCutPasteGeometry Geometry(const Acquired& cue,SubtitleBoxPadding padding={12,8,6}) {
        SubtitleCutPastePresentation presentation;
        auto g=presentation.Consume(cue.accepted,cue.preview,padding,4);
        Assert::IsTrue(g.valid && !g.generatedCleanup.Valid());return g;
    }
    static void AssertGeometryExceptCleanup(const SubtitleCutPasteGeometry& a,const SubtitleCutPasteGeometry& b) {
        Assert::IsTrue(Same(a.source,b.source) && Same(a.destination,b.destination) && Same(a.content,b.content));
        Assert::IsTrue(Same(a.pictureCapture,b.pictureCapture) && Same(a.cleanupLimit,b.cleanupLimit));
        Assert::IsTrue(Same(a.placementLimits,b.placementLimits) && Same(a.layoutContent,b.layoutContent) && Same(a.layoutCoverage,b.layoutCoverage));
        for(size_t i=0;i<a.glyphLines.size();++i)Assert::IsTrue(Same(a.glyphLines[i],b.glyphLines[i]));
        Assert::AreEqual(a.glyphScale,b.glyphScale);Assert::AreEqual(a.glyphTranslateX,b.glyphTranslateX);Assert::AreEqual(a.glyphTranslateY,b.glyphTranslateY);
        Assert::IsTrue(a.centeredGlyphMapping==b.centeredGlyphMapping && a.roundedCorners==b.roundedCorners &&
            a.extendToBar==b.extendToBar && a.pictureIntrusion==b.pictureIntrusion && a.valid==b.valid && a.fromTopBar==b.fromTopBar);
        Assert::AreEqual(a.pictureTop,b.pictureTop);Assert::AreEqual(a.pictureBottom,b.pictureBottom);
        Assert::AreEqual(a.displayPictureLeft,b.displayPictureLeft);Assert::AreEqual(a.displayPictureTop,b.displayPictureTop);
        Assert::AreEqual(a.displayPictureRight,b.displayPictureRight);Assert::AreEqual(a.displayPictureBottom,b.displayPictureBottom);
    }
    static void AssertOnlyHorizontalDestination(const SubtitleCutPasteGeometry& before,const SubtitleCutPasteGeometry& after) {
        auto normalized=after;normalized.destination.left=before.destination.left;normalized.destination.right=before.destination.right;
        AssertGeometryExceptCleanup(before,normalized);Assert::IsTrue(Same(before.generatedCleanup,normalized.generatedCleanup));
    }
    static SubtitleBoxPreview LayoutPreview(bool top,uint64_t sequence=1) {
        SubtitleBoxPreview p;p.available=true;p.policyGeneration=7;p.continuityGeneration=8;
        p.current.identity=Identity(sequence);p.current.analyzed=p.current.barAuthority=true;
        p.current.width=3840;p.current.height=2160;p.current.pictureTop=384;p.current.pictureBottom=1776;
        p.current.text.detected=true;p.current.text.cue=15;p.current.text.observations=20;p.current.text.lineCount=2;
        p.current.text.lineBounds[0]={1362,1778,2474,1874};p.current.text.lineBounds[1]={1540,1926,2300,2022};
        p.current.text.bounds={1362,1778,2474,2022};p.current.text.anchor=p.current.text.lineBounds[0];
        if(top) {
            auto mirror=[](SubtitleBoxRect r){return SubtitleBoxRect{r.left,2160-r.bottom,r.right,2160-r.top};};
            p.current.text.bounds=mirror(p.current.text.bounds);p.current.text.anchor=mirror(p.current.text.anchor);
            for(auto& line:p.current.text.lineBounds)if(line.Valid())line=mirror(line);
        }
        p.current.text.capturePanelMeasured=true;p.current.text.signature.fill(0x55);p.current.text.anchorSignature.fill(0x55);
        for(auto& signature:p.current.text.lineSignatures)signature.fill(0x55);
        p.text=p.current.text;return p;
    }
    static SubtitleCutPasteGeometry Layout(const SubtitleBoxPreview& p,int reduction,SubtitleBoxPadding padding,bool floating) {
        SubtitleCutPastePresentation presentation;
        auto g=presentation.Consume(p.text,p,padding,15);
        g=FitSubtitleToVisiblePicture(g,{0,384,3840,1776},15);
        return ApplySubtitleTextReduction(g,reduction,padding,true,floating);
    }
public:
    TEST_METHOD(ExplicitBarBackingRetainsOriginalGlyphWidthAfterTextReduction) {
        const SubtitleBoxPadding padding{73,60,25};
        for(bool top:{false,true})for(int reduction:{0,50,75}) {
            auto p=LayoutPreview(top);SubtitleLayoutAnchor anchor;
            const auto base=anchor.Consume(Layout(p,reduction,padding,false),p.text,p,padding,15,false);
            Assert::IsTrue(base.valid && base.centeredGlyphMapping && base.extendToBar);
            const auto result=PreserveExtendedSubtitleBackingWidth(base,padding,true);
            AssertOnlyHorizontalDestination(base,result);
            Assert::IsTrue(result.destination.right-result.destination.left>=1112+2*padding.sides);
            Assert::IsTrue(result.destination.left<=base.destination.left && result.destination.right>=base.destination.right);
            const float left=result.glyphScale*result.content.left+result.glyphTranslateX;
            const float right=result.glyphScale*result.content.right+result.glyphTranslateX;
            Assert::IsTrue(result.destination.left<=left && result.destination.right>=right);
            Assert::AreEqual(1362,p.current.text.bounds.left);Assert::AreEqual(2474,p.current.text.bounds.right);
        }
    }
    TEST_METHOD(FloatSettingDoesNotBorrowForcedNativeCardExtension) {
        const SubtitleBoxPadding padding{73,60,25};
        for(bool top:{false,true})for(int reduction:{0,50,75}) {
            auto p=LayoutPreview(top);auto base=Layout(p,reduction,padding,true);
            Assert::IsTrue(base.valid);
            // Native backing may force extension even with float configured.
            base.extendToBar=true;base.pictureIntrusion=true;
            const auto result=PreserveExtendedSubtitleBackingWidth(base,padding,false);
            AssertGeometryExceptCleanup(base,result);Assert::IsTrue(Same(base.generatedCleanup,result.generatedCleanup));
            Assert::IsTrue(result.destination.right-result.destination.left==base.destination.right-base.destination.left);
        }
    }
    TEST_METHOD(BackingWidthDecorationRunsAfterStableAnchorWithoutEnteringItsCache) {
        const SubtitleBoxPadding padding{73,60,25};
        for(bool top:{false,true}) {
            SubtitleLayoutAnchor anchor;auto p=LayoutPreview(top);
            const auto first=anchor.Consume(Layout(p,75,padding,false),p.text,p,padding,15,false);
            const auto decorated=PreserveExtendedSubtitleBackingWidth(first,padding,true);
            Assert::IsTrue(decorated.destination.right-decorated.destination.left>first.destination.right-first.destination.left);
            p=LayoutPreview(top,2);
            const auto next=anchor.Consume(Layout(p,75,padding,false),p.text,p,padding,15,false);
            Assert::IsTrue(Same(first.destination,next.destination),L"Display-only widening must not mutate the stored glyph anchor.");
            const auto nextDecorated=PreserveExtendedSubtitleBackingWidth(next,padding,true);
            Assert::IsTrue(Same(decorated.destination,nextDecorated.destination));
            AssertOnlyHorizontalDestination(next,nextDecorated);
        }
    }
    TEST_METHOD(BackingWidthClampsToViewportAndNeverShrinksLargerCoverage) {
        auto p=LayoutPreview(false);const SubtitleBoxPadding padding{73,60,25};
        auto base=Layout(p,75,padding,false);Assert::IsTrue(base.valid);
        base.placementLimits.left=1600;base.placementLimits.right=2200;
        base.destination.left=1700;base.destination.right=2100;
        const auto clipped=PreserveExtendedSubtitleBackingWidth(base,padding,true);
        AssertOnlyHorizontalDestination(base,clipped);Assert::AreEqual(1600,clipped.destination.left);Assert::AreEqual(2200,clipped.destination.right);
        base=Layout(p,75,padding,false);base.destination.left=900;base.destination.right=2900;
        const auto larger=PreserveExtendedSubtitleBackingWidth(base,padding,true);
        Assert::IsTrue(Same(base.destination,larger.destination));AssertOnlyHorizontalDestination(base,larger);
    }
    TEST_METHOD(BackingWidthFallbackAndInvalidGeometryStayDisplayOnly) {
        auto p=LayoutPreview(false);const SubtitleBoxPadding padding{73,60,25};
        const auto valid=Layout(p,75,padding,false);
        auto fallback=valid;fallback.layoutContent={};
        const auto widened=PreserveExtendedSubtitleBackingWidth(fallback,padding,true);
        Assert::IsTrue(widened.destination.right-widened.destination.left>=fallback.content.right-fallback.content.left+146);
        AssertOnlyHorizontalDestination(fallback,widened);
        for(int fault=0;fault<5;++fault) {
            auto base=valid;
            if(fault==0)base.valid=false;if(fault==1)base.extendToBar=false;
            if(fault==2)base.centeredGlyphMapping=false;if(fault==3)base.placementLimits={};
            if(fault==4)base.layoutContent=base.content={};
            const auto result=PreserveExtendedSubtitleBackingWidth(base,padding,true);
            Assert::IsTrue(Same(base.destination,result.destination));AssertOnlyHorizontalDestination(base,result);
        }
    }
    TEST_METHOD(CurrentBarOnlyGlyphsProveFiniteNativeLipAcrossFormatsAndBothBars) {
        for(int scale:{1,6})for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})
            for(bool top:{false,true})for(int depth:{2,3,4}) {
                Frame frame(scale,format,top);const auto lip=frame.Lip(depth);
                const auto cue=Acquire(frame);const auto original=cue.preview.current.text;
                const auto originalInk=cue.preview.current.ink;
                const auto rawBefore=ExtractActivePictureEvidence(frame.Source());
                const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
                LogProof(proof,frame,"finite");
                Assert::IsTrue(proof.valid);Assert::IsTrue(Contains(proof.cleanup,lip));
                Assert::IsTrue(proof.samples>=size_t(lip.right-lip.left)*(lip.bottom-lip.top) && proof.samples<=65536);
                Assert::IsTrue(SameAnalysisFrameIdentity(proof.identity,Identity(2)));
                Assert::IsTrue(cue.preview.current.ink==originalInk);
                Assert::IsTrue(Same(original.bounds,cue.preview.current.text.bounds) &&
                    Same(original.sourcePanel,cue.preview.current.text.sourcePanel) &&
                    Same(original.provenSourcePanel,cue.preview.current.text.provenSourcePanel) &&
                    Same(original.capturePanel,cue.preview.current.text.capturePanel));
                Assert::IsTrue(original.signature==cue.preview.current.text.signature &&
                    original.anchorSignature==cue.preview.current.text.anchorSignature);
                for(int i=0;i<original.lineCount;++i) {
                    Assert::IsTrue(Same(original.lineBounds[i],cue.preview.current.text.lineBounds[i]));
                    Assert::IsTrue(original.lineSignatures[i]==cue.preview.current.text.lineSignatures[i]);
                }
                const auto rawAfter=ExtractActivePictureEvidence(frame.Source());
                Assert::IsTrue(rawBefore.classification==rawAfter.classification &&
                    rawBefore.proposedBounds.left==rawAfter.proposedBounds.left && rawBefore.proposedBounds.top==rawAfter.proposedBounds.top &&
                    rawBefore.proposedBounds.right==rawAfter.proposedBounds.right && rawBefore.proposedBounds.bottom==rawAfter.proposedBounds.bottom &&
                    rawBefore.trustedBounds.left==rawAfter.trustedBounds.left && rawBefore.trustedBounds.top==rawAfter.trustedBounds.top &&
                    rawBefore.trustedBounds.right==rawAfter.trustedBounds.right && rawBefore.trustedBounds.bottom==rawAfter.trustedBounds.bottom,
                    L"Cleanup proof grants no new glyph, capture, detector-card or AR authority.");
            }
    }
    TEST_METHOD(UhdThirtyPixelLipUsesDefaultBoundedBudgetAndExactDvUpperTenBits) {
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})for(bool top:{false,true}) {
            Frame frame(6,format,top);const auto lip=frame.Lip(30);const auto cue=Acquire(frame);
            AnalysisLumaSample sample;Assert::IsTrue(frame.Source().Sample((lip.left+lip.right)/2,top?lip.top:lip.bottom-1,sample));
            Assert::IsTrue(sample.luma==64,L"Decoded DV low twelve-bit residuals must not round up the analysis code.");
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            LogProof(proof,frame,"UHD30");Assert::IsTrue(proof.valid && Contains(proof.cleanup,lip));
            Assert::IsTrue(proof.samples>32768 && proof.samples<=65536,L"The current native card needs the 65,536-read default, including every interior pixel.");
        }
    }
    TEST_METHOD(BlackFloorParityAndSingleNativeRowRemainConservative) {
        for(int floor:{0,1,64})for(auto format:{Format::P010,Format::P210,Format::DecodedDv})for(bool top:{false,true}) {
            Frame frame(1,format,top,floor);const auto lip=frame.Lip(3);const auto cue=Acquire(frame);
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            LogProof(proof,frame,"black-floor");Assert::IsTrue(proof.valid && Contains(proof.cleanup,lip));
            Frame one(1,format,top,floor);one.Lip(1);const auto oneCue=Acquire(one);
            const auto unsupported=ProveCurrentSubtitleBackingLip(one.Source(),oneCue.preview,oneCue.accepted,Identity(2),7,8);
            Assert::IsFalse(unsupported.valid,L"A one-native-row object lacks independent side corroboration; leave it rather than erase uncertain picture.");
        }
    }
    TEST_METHOD(RoundedFiniteLipKeepsBoundedCornersAndNativeAntialiasing) {
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})for(bool top:{false,true}) {
            Frame frame(6,format,top);const auto lip=frame.Lip(30,true);
            // Neutral native edge antialiasing, two levels above the bar floor,
            // remains within the same exhaustively checked black component.
            for(int y=frame.pictureBottom-30;y<frame.pictureBottom;++y) {
                const int inset=(std::max)(0,12-(y-(frame.pictureBottom-30)));
                frame.Pixel(1344+inset,y,66);frame.Pixel(2495-inset,y,66);
            }
            const auto cue=Acquire(frame);
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            LogProof(proof,frame,"rounded");Assert::IsTrue(proof.valid);
            Assert::IsTrue(Contains(proof.cleanup,lip),L"A proved rounded original backing must not retain its wider bar-facing corners.");
            Assert::IsTrue(proof.cleanup.left>=lip.left-6 && proof.cleanup.right<=lip.right+6);
        }
    }
    TEST_METHOD(SingleUnsampledNativeHoleAndColoredInteriorCannotBecomeCleanup) {
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv})for(bool top:{false,true})for(int fault=0;fault<3;++fault) {
            Frame frame(6,format,top);frame.Lip(30);
            // None of these native positions lies on the ordinary four-pixel
            // detector grid. A cleanup certificate must still inspect them.
            if(fault==0)frame.Pixel(1873,frame.pictureBottom-13,300);
            if(fault==1)frame.Pixel(1381,frame.pictureBottom-17,300);
            if(fault==2)frame.Chroma({1860,frame.pictureBottom-20,1880,frame.pictureBottom-10},420,610);
            const auto cue=Acquire(frame);
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            LogProof(proof,frame,"native-hole-or-color");Assert::IsFalse(proof.valid);
        }
    }
    TEST_METHOD(UnboundedOneSidedAndIrregularDarkSceneryCannotBorrowGlyphOwnership) {
        for(bool top:{false,true})for(int fault=0;fault<6;++fault) {
            Frame frame(1,Format::P010,top);frame.Lip(4);
            if(fault==0)frame.Fill({200,frame.pictureBottom-4,416,frame.pictureBottom},64);
            if(fault==1)frame.Fill({224,frame.pictureBottom-4,440,frame.pictureBottom},64);
            if(fault==2)frame.Fill({224,frame.pictureBottom-30,416,frame.pictureBottom},64);
            if(fault==3)frame.Fill({270,frame.pictureBottom-5,300,frame.pictureBottom-4},64);
            if(fault==4) {
                // An expanding coat/rock shoulder, not a bounded rounded card.
                frame.Fill({210,frame.pictureBottom-3,430,frame.pictureBottom-2},64);
            }
            if(fault==5) {
                // One flank has a black texture interruption beyond the run.
                frame.Pixel(222,frame.pictureBottom-2,64);
            }
            const auto cue=Acquire(frame);
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            LogProof(proof,frame,"non-card-dark-scenery");Assert::IsFalse(proof.valid);
        }
        // An identically shaped, identically colored scene object cannot be
        // semantically distinguished by pixels alone; no such claim is made.
    }
    TEST_METHOD(MissingLipAndAllBlackPictureNeverManufactureAPlate) {
        for(bool top:{false,true}) {
            Frame frame(1,Format::P010,top);const auto cue=Acquire(frame);
            Assert::IsFalse(ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8).valid);
            frame.Fill({0,frame.pictureTop,640,frame.pictureBottom},64);
            const auto black=frame.Measure(3);const auto current=Resolve({black});
            Assert::IsFalse(ProveCurrentSubtitleBackingLip(frame.Source(),current,cue.accepted,Identity(3),7,8).valid);
        }
    }
    TEST_METHOD(CurrentStrongOwnedGlyphsAreRequiredWithoutFallbackToDisplayBounds) {
        Frame frame;frame.Lip(3);const auto good=Acquire(frame);
        for(int fault=0;fault<15;++fault) {
            auto cue=good;
            if(fault==0)cue.accepted.detected=false;if(fault==1)cue.accepted.held=true;
            if(fault==2)cue.accepted.workLimit=true;if(fault==3)cue.accepted.revised=true;
            if(fault==4)cue.accepted.observations=0;if(fault==5)cue.accepted.nearBarEligible=false;
            if(fault==6)cue.preview.current.text.detected=false;if(fault==7)cue.preview.current.text.workLimit=true;
            if(fault==8)cue.preview.current.text.nearBarEligibilityMeasured=false;
            if(fault==9)cue.preview.current.ink.reset();
            if(fault>=10) {
                auto ink=std::make_shared<SubtitleInkSnapshot>(*cue.preview.current.ink);
                if(fault==10)std::fill(ink->ownedInk.begin(),ink->ownedInk.end(),0);
                if(fault==11)ink->paletteValid=false;if(fault==12)ink->rawEvidenceComplete=false;
                if(fault==13)std::fill(ink->rawInk.begin(),ink->rawInk.end(),0);
                cue.preview.current.ink=ink;
            }
            if(fault==14)cue.accepted.cue=0;
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            std::ostringstream message;message<<"owned fault="<<fault<<" reason="<<proof.reason;Logger::WriteMessage(message.str().c_str());
            Assert::IsFalse(proof.valid);
        }
    }
    TEST_METHOD(EveryIdentityAndPhysicalMeasurementBindingIsExactCurrent) {
        Frame frame;frame.Lip(3);const auto good=Acquire(frame);
        for(int fault=0;fault<30;++fault) {
            auto cue=good;auto expected=Identity(2);auto source=frame.Source();
            if(fault==0)++expected.acceptedSequence;if(fault==1)++expected.sourceFrameNumber;
            if(fault==2)++expected.captureTimestamp;if(fault==3)++expected.transportGeneration;
            if(fault==4)++expected.sourceFormatGeneration;if(fault==5)++expected.viewportGeneration;
            if(fault==6)++expected.rendererGeneration;if(fault==7)++expected.analysisDomainKey;
            if(fault==8)cue.preview.available=false;if(fault==9)cue.preview.current.analyzed=false;
            if(fault==10)cue.preview.current.discontinuity=true;if(fault==11)cue.preview.current.barEvidence.topRevalidatedNearby=true;
            if(fault==12)cue.preview.current.analysisRefresh=true;if(fault==13)cue.preview.current.cadenceSkipped=true;
            if(fault==14)cue.preview.current.glyphLocalTop=true;if(fault==15)cue.preview.current.glyphLocalBottom=true;
            if(fault==16)cue.preview.current.independentBarAnalysis=false;if(fault==17)cue.preview.current.barAuthority=false;
            if(fault==18)cue.preview.current.barTrackingAuthority=true;if(fault==19)cue.preview.current.currentAnchorUsesTrackedEdge=true;
            if(fault==20)cue.preview.current.barEvidence.available=false;if(fault==21)cue.preview.current.barEvidence.captionBackedBottom=true;
            if(fault==22)cue.preview.current.barEvidence.bottomRevalidatedAtReference=true;
            if(fault==23)cue.preview.current.barEvidence.bottomRevalidatedNearby=true;
            if(fault==24)++cue.preview.current.pictureBottom;if(fault==25)++cue.preview.current.width;
            if(fault==26)++source.generation;if(fault==27)source.dataBytes=1;if(fault==28)source.rowBytes=1;
            if(fault==29)cue.preview.current.identity.acceptedSequence=expected.acceptedSequence=0;
            const auto proof=ProveCurrentSubtitleBackingLip(source,cue.preview,cue.accepted,expected,7,8);
            std::ostringstream message;message<<"context fault="<<fault<<" reason="<<proof.reason;Logger::WriteMessage(message.str().c_str());
            Assert::IsFalse(proof.valid);
        }
        for(int fault=0;fault<4;++fault) {
            auto cue=good;
            if(fault==0)++cue.preview.policyGeneration;if(fault==1)++cue.preview.current.policyGeneration;
            if(fault==2)++cue.preview.continuityGeneration;if(fault==3)++cue.preview.current.continuityGeneration;
            Assert::IsFalse(ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8).valid);
        }
    }
    TEST_METHOD(MalformedOwnershipGridAndSourceRowsFailClosedWithinBudget) {
        Frame frame;frame.Lip(3);const auto good=Acquire(frame);
        for(int fault=0;fault<10;++fault) {
            auto cue=good;auto ink=std::make_shared<SubtitleInkSnapshot>(*cue.preview.current.ink);
            if(fault==0)ink->step=0;if(fault==1)ink->step=17;if(fault==2)++ink->width;
            if(fault==3)ink->sourceRows.clear();if(fault==4)ink->sourceRows[4]=ink->sourceRows[3];
            if(fault==5)std::swap(ink->sourceRows[3],ink->sourceRows[4]);
            if(fault==6)ink->sourceRows.back()=360;if(fault==7)ink->sourceRows.front()=-1;
            if(fault==8)ink->ownedInk.pop_back();if(fault==9)ink->rawInk.pop_back();cue.preview.current.ink=ink;
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            Assert::IsFalse(proof.valid);Assert::IsTrue(proof.samples==0,
                L"Malformed grids must be rejected before any sampling; normal budget exhaustion cannot mask a missing guard.");
        }
    }
    TEST_METHOD(AllNativeProbeReadsRespectTheCallerWorkLimit) {
        Frame frame(6,Format::DecodedDv);frame.Lip(30);const auto cue=Acquire(frame);
        const auto full=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
        LogProof(full,frame,"budget-baseline");Assert::IsTrue(full.valid && full.samples>32768);
        for(size_t cap:{size_t(0),size_t(1),size_t(32768),full.samples-1}) {
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8,cap);
            Assert::IsFalse(proof.valid);Assert::IsTrue(proof.samples<=cap);
        }
        const auto exact=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8,full.samples);
        Assert::IsTrue(exact.valid);Assert::IsTrue(exact.samples==full.samples);
    }
    TEST_METHOD(LipApplyDecoratesOnlyFreshCleanupWithoutMutatingGeometryOrCaches) {
        for(bool top:{false,true}) {
            Frame frame(1,Format::P210,top);const auto lip=frame.Lip(3);const auto cue=Acquire(frame);
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            Assert::IsTrue(proof.valid);
            SubtitleCutPastePresentation presentation;
            const auto base=presentation.Consume(cue.accepted,cue.preview,SubtitleBoxPadding{12,8,6},4);
            Assert::IsTrue(base.valid && !base.generatedCleanup.Valid());
            const auto result=ApplyCurrentSubtitleBackingLip(base,proof,Identity(2),7,8);
            AssertGeometryExceptCleanup(base,result);Assert::IsTrue(Contains(result.generatedCleanup,lip));
            Assert::IsFalse(base.generatedCleanup.Valid(),L"The cached normal-card descriptor must remain undecorated.");
            const auto cached=presentation.Consume(cue.accepted,cue.preview,SubtitleBoxPadding{12,8,6},4);
            Assert::IsFalse(cached.generatedCleanup.Valid());AssertGeometryExceptCleanup(base,cached);
        }
    }
    TEST_METHOD(LipApplyCannotReplaceExistingCleanupOrCrossItsPhysicalBinding) {
        Frame frame;frame.Lip(3);const auto cue=Acquire(frame);
        const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
        const auto base=Geometry(cue);Assert::IsTrue(proof.valid);
        for(int fault=0;fault<11;++fault) {
            auto g=base;auto p=proof;auto expected=Identity(2);
            if(fault==0)g.generatedCleanup={10,280,20,296};if(fault==1)g.cleanupLimit={0,280,640,296};
            if(fault==2)g.valid=false;if(fault==3)++g.pictureBottom;if(fault==4)g.fromTopBar=!g.fromTopBar;
            if(fault==5)++expected.acceptedSequence;if(fault==6)++expected.analysisDomainKey;
            if(fault==7)p.valid=false;if(fault==8)p.cleanup.bottom=297;
            if(fault==9)++p.policyGeneration;if(fault==10)++p.continuityGeneration;
            const auto result=ApplyCurrentSubtitleBackingLip(g,p,expected,7,8);
            AssertGeometryExceptCleanup(g,result);Assert::IsTrue(Same(g.generatedCleanup,result.generatedCleanup));
        }
    }
    TEST_METHOD(LipDisappearanceHasNoTimerCacheOrOldIdentityCleanup) {
        for(bool top:{false,true}) {
            Frame withLip(1,Format::P010,top);withLip.Lip(3);const auto cue=Acquire(withLip);
            const auto proof=ProveCurrentSubtitleBackingLip(withLip.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            Assert::IsTrue(proof.valid);const auto base=Geometry(cue);
            Frame withoutLip(1,Format::P010,top);const auto observation=withoutLip.Measure(3);
            const auto current=Resolve({observation});
            const auto absent=ProveCurrentSubtitleBackingLip(withoutLip.Source(),current,cue.accepted,Identity(3),7,8);
            Assert::IsFalse(absent.valid);
            Assert::IsFalse(ApplyCurrentSubtitleBackingLip(base,absent,Identity(3),7,8).generatedCleanup.Valid());
            Assert::IsFalse(ApplyCurrentSubtitleBackingLip(base,proof,Identity(3),7,8).generatedCleanup.Valid(),
                L"Even an unchanged cue cannot reuse a prior source-frame cleanup certificate.");
        }
    }
    TEST_METHOD(PendingGuardRevalidatesCurrentGlyphClassesAndNeverCachesTheLip) {
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})for(bool top:{false,true}) {
            Frame frame(1,format,top);frame.Lip(3);const auto cue=Acquire(frame);
            SubtitlePendingMeasurementGuard guard;
            Assert::IsTrue(guard.RememberAccepted(cue.preview,cue.accepted,frame.Source(),41.708,250));
            auto pendingFor=[&](uint64_t sequence) {
                auto key=cue.preview.current;key.identity=Identity(sequence);key.analyzed=false;key.text={};key.ink.reset();
                return Resolve({key});
            };
            auto pending=pendingFor(3);
            Assert::IsTrue(guard.Resolve(pending,frame.Source(),41.708,250));
            Assert::IsTrue(pending.current.pendingRefresh && !pending.current.analysisRefresh);
            auto tracker=cue.tracker;tracker.RefreshReferencePixels(pending,frame.Source());
            const auto accepted=tracker.Consume(pending,250,41.708);
            Assert::IsTrue(accepted.detected && !accepted.held && accepted.observations>=3);
            Assert::IsTrue(SameFreshSubtitleGlyphs(accepted,pending.current.text,0));
            const auto repeated=tracker.Consume(pending,250,41.708);
            Assert::AreEqual(accepted.observations,repeated.observations);
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),pending,accepted,Identity(3),7,8);
            LogProof(proof,frame,"native-pending-guard");Assert::IsTrue(proof.valid);
            Assert::IsFalse(pending.current.subtitleArProof.accepted,L"Fresh cleanup revalidation is not an AR lease.");
            for(bool removeInk:{false,true}) {
                Frame changed=frame;
                // Unit-level malformed producer: the newly identified source
                // carries old pending mask metadata. Lip proof independently
                // checks both positive and negative current classifier cells.
                changed.Pixel(removeInk?240:243,frame.pictureBottom+6,removeInk?64:700);
                auto staleMask=pending;staleMask.current.identity=Identity(4);
                const auto rejected=ProveCurrentSubtitleBackingLip(changed.Source(),staleMask,cue.accepted,Identity(4),7,8);
                LogProof(rejected,changed,"changed-pending-glyph-class");Assert::IsFalse(rejected.valid);
                auto actual=pendingFor(4);auto independentGuard=guard;
                Assert::IsFalse(independentGuard.Resolve(actual,changed.Source(),41.708,250),
                    L"The production pending guard also rejects the changed current glyph pixels.");
            }
            Frame noLip(1,format,top);auto next=pendingFor(4);
            Assert::IsTrue(guard.Resolve(next,noLip.Source(),41.708,250),L"An unchanged bar-only glyph cue may remain pending after its picture-side lip disappears.");
            tracker.RefreshReferencePixels(next,noLip.Source());
            const auto nextAccepted=tracker.Consume(next,250,41.708);
            Assert::IsTrue(nextAccepted.detected && !nextAccepted.held);
            const auto absent=ProveCurrentSubtitleBackingLip(noLip.Source(),next,nextAccepted,Identity(4),7,8);
            Assert::IsFalse(absent.valid);
            const auto base=Geometry(cue);
            Assert::IsFalse(ApplyCurrentSubtitleBackingLip(base,absent,Identity(4),7,8).generatedCleanup.Valid());
            Assert::IsFalse(ApplyCurrentSubtitleBackingLip(base,proof,Identity(4),7,8).generatedCleanup.Valid());
        }
    }
    TEST_METHOD(FirstAcceptedConfirmedLookaheadCueCanRestoreItsCurrentBackingLip) {
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})for(bool top:{false,true}) {
            Frame frame(1,format,top);const auto lip=frame.Lip(3);
            const auto first=frame.Measure(1),second=frame.Measure(2);
            const auto preview=Resolve({first,second});
            Assert::IsTrue(preview.futureAvailable && preview.matchingFrames>=2 && preview.currentLinesConfirmed);
            SubtitleBoxPresentation tracker;
            const auto accepted=tracker.Consume(preview);
            Assert::IsTrue(accepted.detected && !accepted.held && accepted.cue!=0);
            Assert::AreEqual(uint32_t(1),accepted.observations,
                L"One accepted presentation can already contain independent current-plus-future confirmation.");
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),preview,accepted,Identity(1),7,8);
            LogProof(proof,frame,"first-confirmed-lookahead");
            Assert::IsTrue(proof.valid && Contains(proof.cleanup,lip));
            Acquired cue;cue.preview=preview;cue.accepted=accepted;
            const auto base=Geometry(cue);
            const auto repaired=ApplyCurrentSubtitleBackingLip(base,proof,Identity(1),7,8);
            AssertGeometryExceptCleanup(base,repaired);
            Assert::IsTrue(Contains(repaired.generatedCleanup,lip));
            Assert::IsFalse(first.text.sourcePanel.Valid() || first.text.provenSourcePanel.Valid() ||
                first.text.capturePanel.Valid(),
                L"First-frame repair must not manufacture detector card, capture, or retained AR ownership.");
        }
    }
    TEST_METHOD(FirstAcceptedNoFutureCueUsesExistingImmediateAdmissionWithoutAnotherVote) {
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})for(bool top:{false,true}) {
            Frame frame(1,format,top);const auto lip=frame.Lip(3);
            const auto current=frame.Measure(1);const auto preview=Resolve({current});
            Assert::IsTrue(preview.available && !preview.futureAvailable);
            SubtitleBoxPresentation tracker;
            const auto accepted=tracker.Consume(preview);
            Assert::IsTrue(accepted.detected && !accepted.held && accepted.cue!=0,
                L"The existing no-lookahead current-cue admission remains the subtitle owner.");
            Assert::AreEqual(uint32_t(1),accepted.observations);
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),preview,accepted,Identity(1),7,8);
            LogProof(proof,frame,"first-no-future");
            Assert::IsTrue(proof.valid && Contains(proof.cleanup,lip));
            const auto repeated=tracker.Consume(preview);
            Assert::AreEqual(accepted.observations,repeated.observations,
                L"Repeating one source must not supply a second observation to enable repair.");
            const auto repeatedProof=ProveCurrentSubtitleBackingLip(frame.Source(),preview,repeated,Identity(1),7,8);
            Assert::IsTrue(repeatedProof.valid && Same(proof.cleanup,repeatedProof.cleanup));
        }
    }
    TEST_METHOD(RejectedCurrentCueAndZeroObservationHandoffCannotAcquireCleanup) {
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})for(bool top:{false,true}) {
            Frame frame(1,format,top);frame.Lip(3);
            const auto current=frame.Measure(1);
            Frame unrelated=frame;
            unrelated.Fill({0,unrelated.pictureBottom,unrelated.width,unrelated.height},unrelated.floor);
            const auto next=unrelated.Measure(2);
            Assert::IsTrue(current.text.detected);Assert::IsFalse(next.text.detected);
            const auto preview=Resolve({current,next});
            Assert::IsTrue(preview.futureAvailable && preview.matchingFrames==1);
            SubtitleBoxPresentation tracker;
            const auto rejected=tracker.Consume(preview);
            Assert::IsFalse(rejected.detected,
                L"The lip's shape cannot overrule the tracker when an available unrelated next source rejects the cue.");
            Assert::IsFalse(ProveCurrentSubtitleBackingLip(frame.Source(),preview,rejected,Identity(1),7,8).valid);

            const auto matching=frame.Measure(2);const auto confirmed=Resolve({current,matching});
            SubtitleBoxPresentation acceptedTracker;
            auto provisional=acceptedTracker.Consume(confirmed);
            Assert::IsTrue(provisional.detected && provisional.observations==1);
            Assert::IsTrue(ProveCurrentSubtitleBackingLip(frame.Source(),confirmed,provisional,Identity(1),7,8).valid);
            provisional.observations=0;
            Assert::IsFalse(ProveCurrentSubtitleBackingLip(frame.Source(),confirmed,provisional,Identity(1),7,8).valid,
                L"An emitted observations-zero handoff remains presentation-only and cannot seed backing cleanup.");
        }
    }

};
}
