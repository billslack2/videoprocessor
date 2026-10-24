#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleBackingLip.h>
#include <SubtitleBackingPresentation.h>
#include <SubtitleBoxLookahead.h>
#include <SubtitleCutPaste.h>
#include <SubtitleMeasurementWorker.h>
#include <SubtitleCueContinuation.h>
#include <SubtitleCurrentCue.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
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
        SubtitleBoxObservation Measure(uint64_t sequence,int nearBar=0) const {
            const auto source=Source();SubtitleBoxDetector scanner;scanner.SetNearBarDistance(nearBar);
            return SubtitleBoxLookahead::Measure(scanner,source,ExtractActivePictureEvidence(source),
                Identity(sequence),false,{},7,8);
        }
    };
    static SubtitleBoxRect WideCaptionAndLip(Frame& frame,int glyphCount,int depth) {
        frame.Fill({0,frame.pictureBottom,frame.width,frame.height},frame.floor);
        const int glyphWidth=(glyphCount-1)*13*frame.scale+8*frame.scale;
        const int left=(frame.width-glyphWidth)/2;
        for(int row=0;row<2;++row)for(int glyph=0;glyph<glyphCount;++glyph) {
            const int x=left+glyph*13*frame.scale,y=frame.pictureBottom+2+row*28*frame.scale;
            frame.Fill({x,y,x+8*frame.scale,y+14*frame.scale},700);
            frame.Fill({x+2*frame.scale,y+2*frame.scale,x+6*frame.scale,y+12*frame.scale},frame.floor);
        }
        const SubtitleBoxRect cap{left-16*frame.scale,frame.pictureBottom-depth,
            left+glyphWidth+16*frame.scale,frame.pictureBottom};
        for(int y=cap.top;y<cap.bottom;++y) {
            const int inset=(std::max)(0,2*frame.scale-(y-cap.top));
            frame.Fill({cap.left+inset,y,cap.right-inset,y+1},frame.floor);
        }
        return frame.Rect(cap);
    }
    static SubtitleBoxRect WideSingleLineAndLip(Frame& frame,int glyphCount,int glyphHeight) {
        const auto cap=WideCaptionAndLip(frame,glyphCount,30);
        frame.Fill({0,frame.pictureBottom,frame.width,frame.height},frame.floor);
        const int left=(frame.width-((glyphCount-1)*78+48))/2;
        for(int glyph=0;glyph<glyphCount;++glyph) {
            const int x=left+glyph*78,y=frame.pictureBottom+2;
            frame.Fill({x,y,x+48,y+glyphHeight},700);
            frame.Fill({x+12,y+12,x+36,y+glyphHeight-12},frame.floor);
        }
        return cap;
    }
    static SubtitleBoxPreview MissingCurrentWorker(const SubtitleBoxPreview& current,uint64_t sequence) {
        auto missing=current;missing.available=false;missing.text={};auto& raw=missing.current;
        raw.identity=Identity(sequence);raw.sharedPicture.identity=raw.identity;
        raw.analyzed=false;raw.barAuthority=false;raw.independentBarAnalysis=false;
        raw.barTrackingAuthority=false;raw.currentAnchorUsesTrackedEdge=false;
        raw.barEvidence={};raw.pictureTop=0;raw.pictureBottom=raw.height;raw.text={};raw.ink.reset();
        return missing;
    }
    static SubtitleBoxPreview Resolve(std::initializer_list<SubtitleBoxObservation> list) {
        return SubtitleBoxLookahead::Resolve(list.begin(),list.size(),7,8);
    }
    struct Acquired {
        SubtitleBoxPreview preview;
        SubtitleBoxResult accepted;
        SubtitleBoxPresentation tracker;
    };
    static Acquired Acquire(const Frame& frame,int nearBar=0) {
        const auto first=frame.Measure(1,nearBar),second=frame.Measure(2,nearBar);
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
    // Heap-owned fixture drives the same Prepare -> optional pending -> Consume
    // -> compositor -> current cap application ordering as the renderer.
    struct EnvelopeCueState {
        Frame frame;
        AnalysisLumaSource source;
        ActivePictureBounds base{0,384,3840,1776,3840,2160};
        std::array<SubtitleBoxObservation,2> observed;
        SubtitleBoxPreview current,working;
        SubtitleCueContinuationGuard guard;
        SubtitleBoxPresentation tracker;
        SubtitlePendingMeasurementGuard pending;
        SubtitleCutPastePresentation compositor;
        SubtitleBackingLipContinuity backing;
        PreparedCurrentSubtitleCue prepared;
        CurrentSubtitleCueDecision decision;
        SubtitleCutPasteGeometry geometry,applied;
        CurrentSubtitleBackingLipProof cap;
        SubtitleBoxRect expected;
        const char* applicationReason="not-called";
        size_t reads=0;bool exhausted=false;
        uint64_t cue=0;
        explicit EnvelopeCueState(bool top):frame(6,Format::P210,top){}
    };
    static void DrawEnvelopeCue(EnvelopeCueState& s,int glyphs=26,int firstY=1778,int pictureDepth=30) {
        s.expected=WideCaptionAndLip(s.frame,glyphs,pictureDepth);
        s.frame.Fill({0,1776,3840,2160},64);
        const int left=(3840-((glyphs-1)*78+48))/2;
        for(int row=0;row<2;++row)for(int n=0;n<glyphs;++n) {
            const int x=left+n*78,y=row?1886:firstY;
            s.frame.Fill({x,y,x+48,y+66},700);
            s.frame.Fill({x+12,y+12,x+36,y+54},64);
        }
    }
    static void AcquireEnvelopeCue(EnvelopeCueState& s,int distance) {
        s.source=s.frame.Source();
        for(size_t i=0;i<2;++i) {
            SubtitleBoxDetector detector;detector.SetNearBarDistance(distance);
            SubtitlePictureAuthority shared;shared.required=true;shared.identity=Identity(i+1);shared.bounds=s.base;
            s.observed[i]=SubtitleBoxLookahead::Measure(detector,s.source,{},Identity(i+1),false,{},7,8,shared);
            Assert::IsTrue(s.observed[i].text.detected);
        }
        s.working=SubtitleBoxLookahead::Resolve(s.observed.data(),2,7,8);
        PrepareCurrentSubtitleCueInto(s.prepared,s.guard,s.working,s.source,s.base,true,250,41.708);
        ConsumePreparedCurrentSubtitleCueInto(s.decision,s.guard,s.tracker,s.working,s.source,s.base,true,250,41.708,s.prepared);
        s.current=SubtitleBoxLookahead::Resolve(&s.observed[1],1,7,8);s.working=s.current;
        PrepareCurrentSubtitleCueInto(s.prepared,s.guard,s.working,s.source,s.base,true,250,41.708);
        ConsumePreparedCurrentSubtitleCueInto(s.decision,s.guard,s.tracker,s.working,s.source,s.base,true,250,41.708,s.prepared);
        Assert::IsTrue(s.decision.accepted.detected && s.guard.HasReference());s.cue=s.decision.accepted.cue;
        Assert::IsTrue(s.pending.RememberAccepted(s.current,s.decision.accepted,s.source,41.708,250,&s.tracker.AcceptedCueSeedCertificate()));
        s.geometry=s.compositor.Consume(s.decision.accepted,s.current);
        s.cap=s.backing.Consume(s.source,s.current,s.decision.accepted,Identity(2),7,8,SubtitleBackingLipSampleLimit,&s.geometry);
    }
    static void RunEnvelopeCue(EnvelopeCueState& s,uint64_t sequence,
        size_t inspectionLimit=SubtitleCurrentCueInspectionSampleLimit,
        size_t frameLimit=SubtitleCurrentCueFrameSampleLimit) {
        s.working=MissingCurrentWorker(s.current,sequence);
        // No unrelated shared hint may authorize the local fallback.
        s.working.current.sharedPicture={};s.working.current.sharedPicture.required=true;
        AnalysisLumaReadBudget budget{frameLimit};auto source=s.source;source.readBudget=&budget;
        PrepareCurrentSubtitleCueInto(s.prepared,s.guard,s.working,source,s.base,true,250,41.708,inspectionLimit);
        bool pending=false;
        if(s.prepared.proof.RetainsCandidateFor(Identity(sequence),7,8))pending=s.pending.Resolve(s.working,source,41.708,250);
        ConsumePreparedCurrentSubtitleCueInto(s.decision,s.guard,s.tracker,s.working,source,s.base,true,250,41.708,s.prepared,pending);
        s.geometry=s.compositor.Consume(s.decision.accepted,s.working,{},0,nullptr,&s.decision.proof);
        s.cap=s.backing.Consume(source,s.working,s.decision.accepted,Identity(sequence),7,8,
            SubtitleBackingLipSampleLimit,&s.geometry,&s.decision.proof);
        s.applied=ApplyCurrentSubtitleBackingLip(s.geometry,s.cap,Identity(sequence),7,8,&s.applicationReason);
        s.reads=budget.used;s.exhausted=budget.exhausted;
    }
public:
    TEST_METHOD(EnvelopeOnlyPlaneFallbackKeepsCurrentTwoLineCueAndAppliedCap) {
        for(bool top:{false,true})for(int distance:{20,200}) {
            auto s=std::make_unique<EnvelopeCueState>(top);DrawEnvelopeCue(*s);AcquireEnvelopeCue(*s,distance);
            const int plane=top?384:1776;const auto& text=s->current.current.text;
            Assert::IsTrue(text.bounds.top<plane && text.bounds.bottom>plane);
            for(int i=0;i<text.lineCount;++i)Assert::IsTrue(top?text.lineBounds[i].bottom<=plane:text.lineBounds[i].top>=plane);
            SubtitleBarEvidence neutral;neutral.bottom=2160;
            const auto masked=RevalidateSubtitleBarEvidence(s->source,neutral,top?plane:0,top?2160:plane,s->current.current.ink.get(),&text.bounds);
            Assert::IsFalse(top?masked.topRevalidatedAtReference:masked.bottomRevalidatedAtReference);
            for(uint64_t sequence=3;sequence<=10;++sequence) {
                RunEnvelopeCue(*s,sequence);
                Assert::IsTrue(s->decision.proof.completeCurrentCue && s->decision.accepted.detected);
                Assert::AreEqual(s->cue,s->decision.accepted.cue);
                const auto expectedSearch=top?SubtitleBoxRect{0,0,3840,distance==20?712:892}:
                    SubtitleBoxRect{0,distance==20?1448:1268,3840,2160};
                Assert::IsTrue(Same(s->decision.proof.verifiedSearch,expectedSearch));
                Assert::IsTrue(s->cap.valid && Same(s->cap.cleanup,s->expected));
                Assert::IsTrue(std::strcmp(s->applicationReason,"lip-current-cleanup-applied")==0);
                Assert::IsTrue(Same(s->applied.generatedCleanup,s->expected));
                AssertGeometryExceptCleanup(s->geometry,s->applied);
                Assert::IsFalse(s->exhausted);Assert::IsTrue(s->reads<SubtitleCurrentCueFrameSampleLimit);
            }
        }
    }
    TEST_METHOD(EnvelopeOnlyPlaneFallbackRejectsChangedCurrentPixelsAndLostPlane) {
        for(bool top:{false,true})for(int fault=0;fault<5;++fault) {
            auto s=std::make_unique<EnvelopeCueState>(top);DrawEnvelopeCue(*s);AcquireEnvelopeCue(*s,200);
            const int left=(3840-(25*78+48))/2;
            if(fault==0)s->frame.Fill({0,1768,3840,1776},64); // no boundary contrast
            if(fault==1)s->frame.Fill({0,1774,3840,1776},64); // nearby edge is not this exact plane
            if(fault==2)s->frame.Fill({left,1778,left+48,1844},64);
            if(fault==3)s->frame.Fill({0,1788,3840,1792},700);
            if(fault==4){s->frame.Fill({0,1788,3840,1792},700);s->frame.Chroma({0,1788,3840,1792},200,800);}
            RunEnvelopeCue(*s,3);
            Assert::IsFalse(s->decision.proof.valid || s->decision.accepted.detected || s->cap.valid);
            Assert::IsTrue(std::strcmp(s->applicationReason,"lip-application-unavailable")==0);
            Assert::IsFalse(s->applied.generatedCleanup.Valid());
        }
    }
    TEST_METHOD(EnvelopeOnlyPlaneFallbackRejectsThinBarGlyphsMasqueradingAsBoundary) {
        // The upper mirror fits the existing acquisition limit. Its one/two-row
        // native stroke fools the broad nine-row max-jump sampler after the
        // actual picture boundary disappears; exact-row fallback must reject.
        for(int stroke:{1,2})for(int distance:{20,200}) {
            auto s=std::make_unique<EnvelopeCueState>(true);
            s->expected=WideCaptionAndLip(s->frame,26,30);s->frame.Fill({0,1776,3840,2160},64);
            const int left=(3840-(25*78+48))/2;
            for(int row=0;row<2;++row)for(int n=0;n<26;++n) {
                const int x=left+n*78,y=1776+row*108;
                s->frame.Fill({x,y,x+48,y+66},700);s->frame.Fill({x+12,y+stroke,x+36,y+54},64);
            }
            AcquireEnvelopeCue(*s,distance);
            for(int i=0;i<s->current.current.text.lineCount;++i)Assert::IsTrue(s->current.current.text.lineBounds[i].bottom<=384);
            s->frame.Fill({0,1752,3840,1776},64);
            SubtitleBarEvidence neutral;neutral.bottom=2160;
            const auto masked=RevalidateSubtitleBarEvidence(s->source,neutral,384,2160,
                s->current.current.ink.get(),&s->current.current.text.bounds);
            const auto unmasked=RevalidateSubtitleBarEvidence(s->source,neutral,384,2160);
            Assert::IsFalse(masked.topRevalidatedAtReference);
            Assert::IsTrue(unmasked.topRevalidatedAtReference,L"Control must distinguish the unsafe direct-unmask shortcut.");
            Assert::IsFalse(ProveCurrentSubtitleCueSidePlane(s->source,384,true,65536).valid);
            RunEnvelopeCue(*s,3);
            Assert::IsFalse(s->decision.proof.valid || s->decision.accepted.detected || s->cap.valid);
            Assert::IsFalse(s->applied.generatedCleanup.Valid());
        }
    }
    TEST_METHOD(EnvelopeOnlyPlaneFallbackKeepsGlyphAndBackingAuthoritiesSeparate) {
        for(bool top:{false,true}) {
            auto s=std::make_unique<EnvelopeCueState>(top);DrawEnvelopeCue(*s);AcquireEnvelopeCue(*s,200);
            const int left=(3840-(25*78+48))/2;
            s->frame.Fill({left+200,1750,left+500,1760},300); // a current hole in the finite cap
            RunEnvelopeCue(*s,3);
            Assert::IsTrue(s->decision.proof.completeCurrentCue && s->decision.accepted.detected);
            Assert::AreEqual(s->cue,s->decision.accepted.cue);
            Assert::IsFalse(s->cap.valid || s->applied.generatedCleanup.Valid());
        }
    }
    TEST_METHOD(EnvelopeOnlyPlaneFallbackCannotUsePictureSideTouchingGlyphsAsPlane) {
        for(bool top:{false,true}) {
            auto s=std::make_unique<EnvelopeCueState>(top);DrawEnvelopeCue(*s,18,1710,96);AcquireEnvelopeCue(*s,200);
            const int plane=top?384:1776;bool allBar=true;
            for(int i=0;i<s->current.current.text.lineCount;++i) {
                const auto& line=s->current.current.text.lineBounds[i];
                Assert::IsFalse(line.top<plane && line.bottom>plane);
                allBar&=top?line.bottom<=plane:line.top>=plane;
            }
            Assert::IsFalse(allBar);
            s->frame.Fill({0,1746,3840,1776},64);
            const int left=(3840-(17*78+48))/2;
            for(int n=0;n<18;++n){const int x=left+n*78;s->frame.Fill({x,1710,x+48,1776},700);s->frame.Fill({x+12,1722,x+36,1764},64);}
            // A glyph can provide an exact row jump; mixed placement must veto
            // the fallback even if the standalone geometric sampler accepts it.
            Assert::IsTrue(ProveCurrentSubtitleCueSidePlane(s->source,plane,top,65536).valid);
            RunEnvelopeCue(*s,3);
            Assert::IsFalse(s->decision.proof.valid || s->decision.accepted.detected || s->cap.valid);
            Assert::IsFalse(s->applied.generatedCleanup.Valid());
        }
    }
    TEST_METHOD(EnvelopeOnlyPlaneFallbackPreservesMaskedCrossingAndCompanionRejection) {
        for(bool top:{false,true}) {
            auto crossing=std::make_unique<EnvelopeCueState>(top);DrawEnvelopeCue(*crossing,18,1758);AcquireEnvelopeCue(*crossing,20);
            RunEnvelopeCue(*crossing,3);
            Assert::IsTrue(crossing->decision.proof.completeCurrentCue && crossing->decision.accepted.detected);
            const int plane=top?384:1776;bool straddles=false;
            for(int i=0;i<crossing->current.current.text.lineCount;++i){const auto& line=crossing->current.current.text.lineBounds[i];straddles|=line.top<plane && line.bottom>plane;}
            Assert::IsTrue(straddles);
            auto changed=std::make_unique<EnvelopeCueState>(top);DrawEnvelopeCue(*changed);AcquireEnvelopeCue(*changed,20);
            // This nearer companion is inside both the configured scan AND
            // the original anchor's bounded association distance.
            const int left=(3840-(25*78+48))/2;
            changed->frame.Fill({left-96,1618,left+25*78+48+96,1776},64);
            for(int n=0;n<26;++n){const int x=left+n*78;changed->frame.Fill({x,1630,x+48,1696},700);changed->frame.Fill({x+12,1642,x+36,1684},64);}
            RunEnvelopeCue(*changed,3);
            Assert::IsFalse(changed->decision.proof.valid || changed->decision.accepted.detected || changed->cap.valid);
            Assert::IsFalse(changed->applied.generatedCleanup.Valid());
        }
    }
    TEST_METHOD(EnvelopeOnlyPlaneFallbackBudgetUnknownCannotRenewOrApplyAndRecovers) {
        for(bool top:{false,true})for(bool global:{false,true}) {
            auto s=std::make_unique<EnvelopeCueState>(top);DrawEnvelopeCue(*s);AcquireEnvelopeCue(*s,200);
            for(uint64_t sequence=3;sequence<=10;++sequence) {
                RunEnvelopeCue(*s,sequence,global?SubtitleCurrentCueInspectionSampleLimit:80000,
                    global?80000:SubtitleCurrentCueFrameSampleLimit);
                Assert::IsTrue(std::strcmp(s->decision.proof.reason,"complete-sample-budget")==0);
                Assert::IsFalse(s->decision.proof.valid || s->decision.accepted.detected || s->cap.valid);
                Assert::IsTrue(s->decision.proof.candidateRetained && s->guard.HasReference());
                Assert::IsTrue(SameAnalysisFrameIdentity(Identity(2),s->guard.LastVerifiedIdentity()));
                Assert::IsFalse(s->applied.generatedCleanup.Valid());Assert::AreEqual(global,s->exhausted);
            }
            RunEnvelopeCue(*s,11);
            Assert::IsTrue(s->decision.proof.completeCurrentCue && s->decision.accepted.detected);
            Assert::AreEqual(s->cue,s->decision.accepted.cue);
            Assert::IsTrue(std::strcmp(s->applicationReason,"lip-current-cleanup-applied")==0);
        }
    }
    TEST_METHOD(PreparedCurrentCuePriorityKeepsWideBackingInsideSharedFrameBudget) {
        for(const auto shape:{std::pair<int,int>{36,54},{34,66}})for(bool prioritize:{false,true}) {
            Frame frame(6,Format::P210);const auto expected=WideSingleLineAndLip(frame,shape.first,shape.second);
            const auto source=frame.Source();const ActivePictureBounds base{0,384,3840,1776,3840,2160};
            const auto measure=[&](uint64_t seq){SubtitleBoxDetector detector;detector.SetNearBarDistance(200);
                SubtitlePictureAuthority shared;shared.required=true;shared.identity=Identity(seq);shared.bounds=base;
                return SubtitleBoxLookahead::Measure(detector,source,{},Identity(seq),false,{},7,8,shared);};
            const auto first=measure(1),second=measure(2);Assert::IsTrue(first.text.detected && second.text.detected);
            SubtitleCueContinuationGuard guard;SubtitleBoxPresentation tracker;SubtitleCutPastePresentation compositor;
            SubtitlePendingMeasurementGuard pending;SubtitleBackingLipContinuity backing;
            auto initial=Resolve({first,second});auto accepted=ConsumeCurrentSubtitleCue(guard,tracker,initial,source,base,true,250,41.708);
            auto current=Resolve({second});accepted=ConsumeCurrentSubtitleCue(guard,tracker,current,source,base,true,250,41.708);
            Assert::IsTrue(accepted.accepted.detected && guard.HasReference());
            Assert::IsTrue(pending.RememberAccepted(current,accepted.accepted,source,41.708,250,&tracker.AcceptedCueSeedCertificate()));
            const auto geometry=compositor.Consume(accepted.accepted,current);
            Assert::IsTrue(backing.Consume(source,current,accepted.accepted,Identity(2),7,8,SubtitleBackingLipSampleLimit,&geometry).valid);
            auto missing=MissingCurrentWorker(current,3);AnalysisLumaReadBudget budget{SubtitleCurrentCueFrameSampleLimit};
            auto bounded=source;bounded.readBudget=&budget;CurrentSubtitleCueDecision decision;
            if(prioritize) {
                const auto prepared=PrepareCurrentSubtitleCue(guard,missing,bounded,base,true,250,41.708);
                Assert::IsTrue(prepared.proof.completeCurrentCue && !prepared.proof.RetainsCandidateFor(Identity(3),7,8));
                const auto reads=budget.used;
                decision=ConsumePreparedCurrentSubtitleCue(guard,tracker,missing,bounded,base,true,250,41.708,prepared);
                Assert::AreEqual(reads,budget.used,L"Consumption must not inspect the same source twice.");
            } else {
                const bool verified=pending.Resolve(missing,bounded,41.708,250);
                decision=ConsumeCurrentSubtitleCue(guard,tracker,missing,bounded,base,true,250,41.708,verified);
            }
            Assert::IsTrue(decision.proof.completeCurrentCue && decision.accepted.detected);
            Assert::IsTrue(Same(decision.proof.verifiedSearch,{0,1268,3840,2160}),L"Keep full configured companion coverage.");
            const auto output=compositor.Consume(decision.accepted,missing,{},0,nullptr,&decision.proof);
            const auto cap=backing.Consume(bounded,missing,decision.accepted,Identity(3),7,8,SubtitleBackingLipSampleLimit,&output,&decision.proof);
            Assert::IsTrue(output.valid);Assert::AreEqual(prioritize,cap.valid);
            if(prioritize) {
                Assert::IsTrue(Same(expected,cap.cleanup) && !budget.exhausted && budget.used<500000);
                Assert::AreEqual(accepted.accepted.cue,decision.accepted.cue);
            } else {
                Assert::IsTrue(std::strcmp(cap.reason,"lip-work-budget")==0 && budget.exhausted);
                Assert::AreEqual(SubtitleCurrentCueFrameSampleLimit,budget.used);
            }
        }
    }
    TEST_METHOD(PreparedCurrentCueUnknownPreservesGenuinePendingFallbackAndExactRepeats) {
        Frame frame(1,Format::P210);frame.Lip(4,true);auto cue=Acquire(frame,20);auto source=frame.Source();
        const ActivePictureBounds base{0,64,640,296,640,360};SubtitleCueContinuationGuard guard;
        Assert::IsTrue(guard.Remember(cue.preview,cue.accepted,base,source));SubtitlePendingMeasurementGuard pending;
        Assert::IsTrue(pending.RememberAccepted(cue.preview,cue.accepted,source,41.708,250,&cue.tracker.AcceptedCueSeedCertificate()));
        for(int repeat=0;repeat<3;++repeat) {
            auto missing=MissingCurrentWorker(cue.preview,3);AnalysisLumaReadBudget budget{SubtitleCurrentCueFrameSampleLimit};source.readBudget=&budget;
            // Force only the inspector phase to one read inside the normal shared frame budget.
            // Genuine pending work below spends the same remaining counter; this tests state semantics.
            const auto prepared=PrepareCurrentSubtitleCue(guard,missing,source,base,true,250,41.708,1);
            Assert::IsTrue(prepared.proof.RetainsCandidateFor(Identity(3),7,8));
            Assert::IsTrue(pending.Resolve(missing,source,41.708,250));const auto reads=budget.used;
            const auto decision=ConsumePreparedCurrentSubtitleCue(guard,cue.tracker,missing,source,base,true,250,41.708,prepared,true);
            Assert::IsTrue(!decision.proof.valid && decision.accepted.detected);
            Assert::AreEqual(cue.accepted.cue,decision.accepted.cue);Assert::AreEqual(reads,budget.used);
            Assert::IsTrue(SameAnalysisFrameIdentity(Identity(2),guard.LastVerifiedIdentity()));
        }
        auto missing=MissingCurrentWorker(cue.preview,4);AnalysisLumaReadBudget budget{SubtitleCurrentCueFrameSampleLimit};source.readBudget=&budget;
        const auto prepared=PrepareCurrentSubtitleCue(guard,missing,source,base,true,250,41.708);Assert::IsTrue(prepared.proof.completeCurrentCue);
        const auto reads=budget.used;const auto decision=ConsumePreparedCurrentSubtitleCue(guard,cue.tracker,missing,source,base,true,250,41.708,prepared);
        Assert::IsTrue(decision.accepted.detected);Assert::AreEqual(cue.accepted.cue,decision.accepted.cue);Assert::AreEqual(reads,budget.used);
    }
    TEST_METHOD(PreparedCurrentCueRejectsChangedSourceOrContextAfterPreparation) {
        auto preparedStorage=std::make_unique<PreparedCurrentSubtitleCue>();
        auto decisionStorage=std::make_unique<CurrentSubtitleCueDecision>();
        auto& prepared=*preparedStorage;auto& decision=*decisionStorage;
        for(int fault=0;fault<10;++fault) {
            Frame frame(1,Format::P210);frame.Lip(4,true);auto cue=Acquire(frame,20);auto source=frame.Source();
            const ActivePictureBounds base{0,64,640,296,640,360};SubtitleCueContinuationGuard guard;
            Assert::IsTrue(guard.Remember(cue.preview,cue.accepted,base,source));auto missing=MissingCurrentWorker(cue.preview,3);
            PrepareCurrentSubtitleCueInto(prepared,guard,missing,source,base,true,250,41.708);Assert::IsTrue(prepared.proof.completeCurrentCue);
            if(fault==1)missing.current.identity=Identity(4);
            if(fault==2)missing.policyGeneration=missing.current.policyGeneration=9;
            if(fault==3)missing.continuityGeneration=missing.current.continuityGeneration=9;
            if(fault==4)missing.current.width+=4;
            auto changedBase=base;
            if(fault==6)changedBase.top+=4;
            if(fault==7)source.format=AnalysisLumaFormat::P010;
            if(fault==8)source.dataBytes=0;
            if(fault==9)source.generation+=1;
            ConsumePreparedCurrentSubtitleCueInto(decision,guard,cue.tracker,missing,source,changedBase,fault!=5,250,41.708,prepared);
            Assert::AreEqual(fault==0,decision.proof.valid);Assert::AreEqual(fault==0,decision.accepted.detected);
            if(fault)Assert::IsFalse(decision.proof.candidateRetained);
        }
    }
    TEST_METHOD(PreparedCurrentCueCompanionContradictionCannotUsePendingFallback) {
        Frame frame(6,Format::P210);WideSingleLineAndLip(frame,34,54);auto cue=Acquire(frame,200);auto source=frame.Source();
        const ActivePictureBounds base{0,384,3840,1776,3840,2160};SubtitleCueContinuationGuard guard;
        Assert::IsTrue(guard.Remember(cue.preview,cue.accepted,base,source));SubtitlePendingMeasurementGuard pending;
        Assert::IsTrue(pending.RememberAccepted(cue.preview,cue.accepted,source,41.708,250,&cue.tracker.AcceptedCueSeedCertificate()));
        const int left=(3840-(33*78+48))/2;
        frame.Fill({left-96,1618,left+33*78+48+96,1776},64);
        for(int n=0;n<34;++n){const int x=left+n*78;frame.Fill({x,1630,x+48,1714},700);frame.Fill({x+12,1642,x+36,1702},64);}
        // The narrower pending check alone still recognizes the old bar text.
        auto pendingControl=MissingCurrentWorker(cue.preview,3);
        Assert::IsTrue(pending.Resolve(pendingControl,source,41.708,250));
        auto missing=MissingCurrentWorker(cue.preview,3);AnalysisLumaReadBudget budget{SubtitleCurrentCueFrameSampleLimit};source.readBudget=&budget;
        const auto prepared=PrepareCurrentSubtitleCue(guard,missing,source,base,true,250,41.708);
        Assert::IsTrue(!prepared.proof.valid && !guard.HasReference() && std::strcmp(prepared.proof.reason,"complete-cue-changed")==0);
        Assert::IsFalse(prepared.proof.RetainsCandidateFor(Identity(3),7,8),L"Hard contradiction must skip pending fallback.");
        const auto reads=budget.used;const auto decision=ConsumePreparedCurrentSubtitleCue(guard,cue.tracker,missing,source,base,true,250,41.708,prepared);
        Assert::IsFalse(decision.accepted.detected);Assert::AreEqual(reads,budget.used);
    }
    TEST_METHOD(WideCurrentBackingUsesBoundedPhaseAllowanceAndSharedFrameLimit) {
        Assert::AreEqual(size_t(131072),SubtitleBackingLipSampleLimit);
        Assert::AreEqual(size_t(524288),SubtitleCurrentCueFrameSampleLimit);
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})
        for(bool top:{false,true})for(int glyphCount:{20,34}) {
            Frame frame(6,format,top);const auto expected=WideCaptionAndLip(frame,glyphCount,30);
            const auto cue=Acquire(frame);auto preview=cue.preview;
            preview.current.independentBarAnalysis=false;
            if(top)preview.current.barEvidence.topRevalidatedNearby=true;
            else preview.current.barEvidence.bottomRevalidatedNearby=true;
            const auto geometry=Geometry(cue);
            const auto legacy=ProveCurrentSubtitleBackingLip(frame.Source(),preview,cue.accepted,Identity(2),7,8,65536);
            Assert::IsFalse(legacy.valid);Assert::IsTrue(std::strcmp(legacy.reason,"lip-work-budget")==0);
            AnalysisLumaReadBudget budget{SubtitleCurrentCueFrameSampleLimit};auto source=frame.Source();source.readBudget=&budget;
            const auto current=ProveCurrentSubtitleBackingLip(source,preview,cue.accepted,Identity(2),7,8);
            Assert::IsTrue(current.valid && current.cuePlaneVerified && Same(current.cleanup,expected));
            Assert::IsTrue(current.samples>65536 && current.samples<=SubtitleBackingLipSampleLimit);
            const auto reads=budget.used;Assert::IsTrue(reads>65536 && reads<=SubtitleBackingLipSampleLimit && !budget.exhausted);
            // Full inspector allowance and the recorded pending+inspector cost
            // both leave enough shared capacity for this wider exact body.
            for(size_t priorReads:{SubtitleCurrentCueInspectionSampleLimit,size_t(397101),SubtitleCurrentCueFrameSampleLimit-reads}) {
                budget={SubtitleCurrentCueFrameSampleLimit,priorReads,false};
                const auto fitting=ProveCurrentSubtitleBackingLip(source,preview,cue.accepted,Identity(2),7,8);
                Assert::IsTrue(fitting.valid && Same(fitting.cleanup,expected));
                Assert::AreEqual(priorReads+reads,budget.used);Assert::IsFalse(budget.exhausted);
            }
            // One native read short never exports a partial cleanup. The new
            // local ceiling cannot bypass or replenish the shared frame cap.
            budget={SubtitleCurrentCueFrameSampleLimit,SubtitleCurrentCueFrameSampleLimit-reads+1,false};
            const auto insufficient=ProveCurrentSubtitleBackingLip(source,preview,cue.accepted,Identity(2),7,8);
            Assert::IsFalse(insufficient.valid);Assert::IsTrue(budget.exhausted);
            Assert::AreEqual(SubtitleCurrentCueFrameSampleLimit,budget.used);
            Assert::IsFalse(ApplyCurrentSubtitleBackingLip(geometry,insufficient,Identity(2),7,8).generatedCleanup.Valid());
            // More capacity must not erase a newly bright native body pixel.
            frame.Pixel(frame.width/2,frame.pictureBottom-4,700);budget={SubtitleCurrentCueFrameSampleLimit};
            Assert::IsFalse(ProveCurrentSubtitleBackingLip(source,preview,cue.accepted,Identity(2),7,8).valid);
        }
    }
    TEST_METHOD(IncompleteBackingBudgetKeepsOnlyUnrenewedOutlineCoordinates) {
        for(bool top:{false,true}) {
            Frame frame(6,Format::P210,top);const auto expected=WideCaptionAndLip(frame,12,30);
            const auto cue=Acquire(frame);const auto geometry=Geometry(cue);
            SubtitleBackingLipContinuity state;
            const auto seeded=state.Consume(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8,
                SubtitleBackingLipSampleLimit,&geometry);
            Assert::IsTrue(seeded.valid && Same(seeded.cleanup,expected));
            auto expiring=state;auto changedContext=state;
            auto previewFor=[&](uint64_t seq){return Resolve({frame.Measure(seq)});};
            const auto third=previewFor(3);
            for(int repeat=0;repeat<3;++repeat) {
                const auto unknown=state.Consume(frame.Source(),third,cue.accepted,Identity(3),7,8,1,&geometry);
                Assert::IsFalse(unknown.valid);Assert::IsTrue(std::strcmp(unknown.reason,"lip-work-budget")==0);
                Assert::IsFalse(ApplyCurrentSubtitleBackingLip(geometry,unknown,Identity(3),7,8).generatedCleanup.Valid());
            }
            // A small OUTSIDE flank shadow is allowed by the established
            // outline's distributed current boundary proof, but defeats cold
            // acquisition. It cannot enlarge the old removal rectangle.
            frame.Pixel(expected.left-1,frame.pictureBottom-10,frame.floor);
            const auto fourth=previewFor(4);SubtitleBackingLipContinuity cold;
            Assert::IsFalse(cold.Consume(frame.Source(),fourth,cue.accepted,Identity(4),7,8,
                SubtitleBackingLipSampleLimit,&geometry).valid);
            const auto recovered=state.Consume(frame.Source(),fourth,cue.accepted,Identity(4),7,8,
                SubtitleBackingLipSampleLimit,&geometry);
            Assert::IsTrue(recovered.valid && recovered.revalidatedOutline && Same(recovered.cleanup,expected));
            Assert::IsTrue(SameAnalysisFrameIdentity(recovered.identity,Identity(4)));
            for(uint64_t seq=3;seq<=10;++seq) {
                const auto current=previewFor(seq);
                const auto unknown=expiring.Consume(frame.Source(),current,cue.accepted,Identity(seq),7,8,1,&geometry);
                Assert::IsFalse(unknown.valid);Assert::IsTrue(std::strcmp(unknown.reason,"lip-work-budget")==0);
            }
            // Eight failed/repeated sources never renew the last-success cursor.
            const auto expired=previewFor(11);
            Assert::IsFalse(expiring.Consume(frame.Source(),expired,cue.accepted,Identity(11),7,8,
                SubtitleBackingLipSampleLimit,&geometry).valid);
            // A real body contradiction discards the retained outline. Restoring
            // pixels later must reacquire; it cannot revive discarded authority.
            frame.Pixel(frame.width/2,frame.pictureBottom-4,700);
            const auto fifth=previewFor(5);
            Assert::IsFalse(state.Consume(frame.Source(),fifth,cue.accepted,Identity(5),7,8,
                SubtitleBackingLipSampleLimit,&geometry).valid);
            frame.Pixel(frame.width/2,frame.pictureBottom-4,frame.floor);
            const auto sixth=previewFor(6);
            Assert::IsFalse(state.Consume(frame.Source(),sixth,cue.accepted,Identity(6),7,8,
                SubtitleBackingLipSampleLimit,&geometry).valid);
            auto changed=fourth;changed.policyGeneration=changed.current.policyGeneration=9;
            Assert::IsFalse(changedContext.Consume(frame.Source(),changed,cue.accepted,Identity(4),9,8,1,&geometry).valid);
            Assert::IsFalse(changedContext.Consume(frame.Source(),sixth,cue.accepted,Identity(6),7,8,
                SubtitleBackingLipSampleLimit,&geometry).valid);
        }
    }
    TEST_METHOD(ColdCurrentPlaneReproofAdmitsNearbySidesWithoutChangingRawAuthority) {
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})for(bool top:{false,true}) {
            Frame frame(6,format,top);const auto lip=frame.Lip(30,true);const auto cue=Acquire(frame);
            const auto geometry=Geometry(cue);
            for(int mode=0;mode<4;++mode) {
                auto preview=cue.preview;auto& raw=preview.current;raw.independentBarAnalysis=false;
                if(mode==0) {if(top)raw.barEvidence.topRevalidatedNearby=true;else raw.barEvidence.bottomRevalidatedNearby=true;}
                if(mode==1) {if(top)raw.barEvidence.topRevalidatedAtReference=true;else raw.barEvidence.bottomRevalidatedAtReference=true;}
                if(mode==2)raw.barEvidence.topRevalidatedNearby=raw.barEvidence.bottomRevalidatedNearby=true;
                if(mode==3) {if(top)raw.pictureBottom=raw.barEvidence.bottom=frame.height;else raw.pictureTop=raw.barEvidence.top=0;}
                const auto before=raw;SubtitleBackingLipContinuity cold;
                const auto proof=cold.Consume(frame.Source(),preview,cue.accepted,Identity(2),7,8,65536,&geometry);
                LogProof(proof,frame,"cold-current-plane");
                Assert::IsTrue(proof.valid && proof.cuePlaneVerified && proof.cuePlaneSamples>0);
                Assert::IsTrue(proof.samples<=65536 && Same(lip,proof.cleanup));
                Assert::IsFalse(raw.independentBarAnalysis);
                Assert::IsTrue(raw.pictureTop==before.pictureTop && raw.pictureBottom==before.pictureBottom &&
                    raw.barEvidence.topRevalidatedNearby==before.barEvidence.topRevalidatedNearby &&
                    raw.barEvidence.bottomRevalidatedNearby==before.barEvidence.bottomRevalidatedNearby);
                const auto applied=ApplyCurrentSubtitleBackingLip(geometry,proof,Identity(2),7,8);
                AssertGeometryExceptCleanup(geometry,applied);Assert::IsTrue(Same(lip,applied.generatedCleanup));
            }
        }
    }
    TEST_METHOD(ColdNearbyCueSeedsOnlyCandidateUntilCompleteCurrentInspection) {
        for(bool top:{false,true})for(int metadata=0;metadata<4;++metadata) {
            Frame frame(1,Format::P210,top);frame.Lip(4,true);auto cue=Acquire(frame,20);
            auto& raw=cue.preview.current;raw.independentBarAnalysis=false;
            if(top)raw.barEvidence.topRevalidatedNearby=true;else raw.barEvidence.bottomRevalidatedNearby=true;
            if(metadata==1) {
                if(top){raw.barEvidence.topRevalidatedNearby=false;raw.barEvidence.topRevalidatedAtReference=true;}
                else{raw.barEvidence.bottomRevalidatedNearby=false;raw.barEvidence.bottomRevalidatedAtReference=true;}
            }
            if(metadata==2)raw.barEvidence.topRevalidatedNearby=raw.barEvidence.bottomRevalidatedNearby=true;
            if(metadata==3) {
                if(top)raw.pictureBottom=raw.barEvidence.bottom=frame.height;
                else raw.pictureTop=raw.barEvidence.top=0;
            }
            Assert::IsFalse(raw.text.configuredSearchComplete);
            const ActivePictureBounds base{0,frame.pictureTop,frame.width,frame.pictureBottom,frame.width,frame.height};
            raw.sharedPicture.required=true;raw.sharedPicture.identity=raw.identity;raw.sharedPicture.bounds=base;
            AnalysisLumaReadBudget seedBudget{SubtitleCurrentCueFrameSampleLimit};auto source=frame.Source();source.readBudget=&seedBudget;
            SubtitleCueContinuationGuard guard;
            Assert::IsTrue(guard.Remember(cue.preview,cue.accepted,base,source));
            Assert::IsTrue(guard.HasReference() && seedBudget.used>0 && !seedBudget.exhausted);
            Assert::IsFalse(raw.independentBarAnalysis || raw.text.configuredSearchComplete);
            // The old detector-negative/missing-edge lease must not bypass
            // configured inspection after only nearby candidate acquisition.
            SubtitleCueContinuationGuard weakCandidate;
            Assert::IsTrue(weakCandidate.Remember(cue.preview,cue.accepted,base,frame.Source()));
            auto missing=cue.preview;missing.current.identity=Identity(3);missing.current.text={};missing.text={};
            missing.current.sharedPicture.required=true;missing.current.sharedPicture.identity=Identity(3);missing.current.sharedPicture.bounds=base;
            if(top)missing.current.pictureTop=missing.current.barEvidence.top=0;
            else missing.current.pictureBottom=missing.current.barEvidence.bottom=frame.height;
            const auto firstContinuation=weakCandidate.Prepare(missing,frame.Source(),base,250,41.708);
            Assert::IsTrue(firstContinuation.completeCurrentCue && firstContinuation.valid,
                L"A nearby cold candidate must complete current inspection, never enter the old partial lease.");
            auto pending=cue.preview;pending.current.identity=Identity(3);pending.available=false;
            pending.current.analyzed=false;pending.current.text={};pending.current.ink.reset();
            pending.current.barAuthority=false;pending.current.barEvidence={};
            const auto incomplete=guard.InspectCurrent(pending,frame.Source(),1);
            Assert::IsFalse(incomplete.valid || incomplete.completeCurrentCue,
                L"An internal candidate cannot authorize output without the full configured current scan.");
            const auto current=guard.InspectCurrent(pending,frame.Source());
            Logger::WriteMessage(current.reason);
            Assert::IsTrue(current.completeCurrentCue && current.MatchesSource(Identity(3),7,8,cue.accepted));
            Assert::IsFalse(pending.available || pending.current.analyzed || pending.current.barAuthority);
        }
    }
    TEST_METHOD(ColdNearbyCueCandidateRejectsWrongPlaneWeakOwnershipAndContext) {
        for(bool top:{false,true})for(int fault=0;fault<14;++fault) {
            Frame frame(1,Format::P210,top);frame.Lip(4,true);auto cue=Acquire(frame,20);
            auto& raw=cue.preview.current;raw.independentBarAnalysis=false;
            if(top)raw.barEvidence.topRevalidatedNearby=true;else raw.barEvidence.bottomRevalidatedNearby=true;
            auto source=frame.Source();
            if(fault==0){if(top)++raw.pictureTop,++raw.barEvidence.top;else ++raw.pictureBottom,++raw.barEvidence.bottom;}
            if(fault==1)raw.barTrackingAuthority=true;
            if(fault==2)raw.currentAnchorUsesTrackedEdge=true;
            if(fault==3){if(top)raw.barEvidence.captionBackedTop=true;else raw.barEvidence.captionBackedBottom=true;}
            if(fault==4){if(top)++raw.barEvidence.top;else ++raw.barEvidence.bottom;}
            if(fault==5)++source.generation;
            if(fault==6)++cue.preview.policyGeneration;
            if(fault==7){if(top)raw.text.topSearchComplete=false;else raw.text.bottomSearchComplete=false;}
            if(fault==8){if(top)raw.text.topSearchEnd=raw.pictureTop-1;else raw.text.bottomSearchStart=raw.pictureBottom+1;}
            if(fault==9){auto ink=std::make_shared<SubtitleInkSnapshot>(*raw.ink);ink->rawEvidenceComplete=false;raw.ink=ink;}
            if(fault==10)frame.Fill({0,frame.pictureBottom+1,frame.width,frame.height},frame.floor);
            if(fault==11)raw.text.workLimit=true;
            if(fault==12)raw.discontinuity=true;
            if(fault==13)frame.Fill({0,frame.pictureTop,frame.width,frame.pictureBottom},frame.floor);
            const ActivePictureBounds base{0,frame.pictureTop,frame.width,frame.pictureBottom,frame.width,frame.height};
            SubtitleCueContinuationGuard guard;
            const bool seeded=guard.Remember(cue.preview,cue.accepted,base,source);
            std::ostringstream out;out<<"cold candidate negative top="<<top<<" fault="<<fault<<" reason="<<guard.LastSeedReason();
            Logger::WriteMessage(out.str().c_str());
            Assert::IsFalse(seeded || guard.HasReference());
        }
    }
    TEST_METHOD(IncompleteInspectorCanDisplayOnlyOrdinarilyAdmittedExactCurrentRawCue) {
        for(int mode=0;mode<5;++mode) {
            Frame frame(1,Format::P210);frame.Lip(4,true);auto cue=Acquire(frame);
            const ActivePictureBounds base{0,frame.pictureTop,frame.width,frame.pictureBottom,frame.width,frame.height};
            SubtitleCueContinuationGuard guard;Assert::IsTrue(guard.Remember(cue.preview,cue.accepted,base,frame.Source()));
            const auto selected=guard.LastVerifiedIdentity();auto current=cue.preview;current.current.identity=Identity(3);
            bool pendingVerified=false;
            if(mode==1 || mode==2) {
                SubtitlePendingMeasurementGuard pendingGuard;
                Assert::IsTrue(pendingGuard.RememberAccepted(cue.preview,cue.accepted,frame.Source(),41.708,250,
                    &cue.tracker.AcceptedCueSeedCertificate()));
                current.available=false;current.current.analyzed=false;
                pendingVerified=pendingGuard.Resolve(current,frame.Source(),41.708,250);
                Assert::IsTrue(pendingVerified);if(mode==2)pendingVerified=false;
            } else {
                current.current.independentBarAnalysis=false;current.current.barEvidence.bottomRevalidatedNearby=true;
                if(mode==3)current.current.analysisRefresh=true;
                if(mode==4)current.current.text.workLimit=true;
            }
            AnalysisLumaReadBudget budget{1};auto bounded=frame.Source();bounded.readBudget=&budget;
            const auto result=ConsumeCurrentSubtitleCue(guard,cue.tracker,current,bounded,base,true,250,41.708,pendingVerified);
            Assert::IsFalse(result.proof.valid || result.proof.completeCurrentCue);
            Assert::IsTrue(guard.HasReference() && SameAnalysisFrameIdentity(selected,guard.LastVerifiedIdentity()));
            if(mode<2) {
                Assert::IsTrue(result.accepted.detected && !result.accepted.held && !result.proof.candidateRetained);
                Assert::AreEqual(cue.accepted.cue,result.accepted.cue);
                Assert::IsTrue(Same(cue.accepted.bounds,result.accepted.bounds));
                Assert::IsFalse(cue.tracker.AcceptedCueSeedCertificate().Matches(cue.preview,cue.accepted));
            } else Assert::IsTrue(result.proof.candidateRetained && !result.accepted.detected);
            auto next=cue.preview;next.available=false;next.current.identity=Identity(4);next.current.analyzed=false;
            next.current.barAuthority=false;next.current.barEvidence={};next.current.text={};next.current.ink.reset();
            const auto recovered=ConsumeCurrentSubtitleCue(guard,cue.tracker,next,frame.Source(),base,true,250,41.708);
            Assert::IsTrue(recovered.proof.completeCurrentCue && recovered.accepted.detected);
            Assert::AreEqual(cue.accepted.cue,recovered.accepted.cue);
            Assert::IsTrue(SameAnalysisFrameIdentity(selected,recovered.proof.previousIdentity),
                L"Display-only trial admission cannot advance the saved source cursor or lease.");
        }
    }
    TEST_METHOD(CurrentRawFallbackCursorSurvivesConsecutiveUnknownAndExactRepeatsWithoutRenewingProof) {
        Frame frame(1,Format::P210);frame.Lip(4,true);auto cue=Acquire(frame);
        const ActivePictureBounds base{0,frame.pictureTop,frame.width,frame.pictureBottom,frame.width,frame.height};
        SubtitleCueContinuationGuard guard;Assert::IsTrue(guard.Remember(cue.preview,cue.accepted,base,frame.Source()));
        const auto selected=guard.LastVerifiedIdentity();const auto weakMs=cue.tracker.WeakHoldMilliseconds();
        const auto handoffs=cue.tracker.HandoffSources();SubtitleCutPastePresentation compositor;
        const auto original=compositor.Consume(cue.accepted,cue.preview);Assert::IsTrue(original.valid);
        const auto rawAt=[&](uint64_t seq) {auto p=Resolve({frame.Measure(seq)});
            p.current.independentBarAnalysis=false;p.current.barEvidence.bottomRevalidatedNearby=true;return p;};
        for(uint64_t seq=3;seq<=10;++seq) {
            const auto raw=rawAt(seq);uint64_t observations=0;
            for(int repeat=0;repeat<(seq==4?2:1);++repeat) {
                AnalysisLumaReadBudget budget{1};auto bounded=frame.Source();bounded.readBudget=&budget;
                const auto next=ConsumeCurrentSubtitleCue(guard,cue.tracker,raw,bounded,base,true,250,41.708);
                Assert::IsTrue(next.accepted.detected && !next.accepted.held && !next.proof.valid && !next.proof.candidateRetained);
                Assert::AreEqual(cue.accepted.cue,next.accepted.cue);
                Assert::IsTrue(SameAnalysisFrameIdentity(selected,guard.LastVerifiedIdentity()));
                Assert::IsTrue(SameAnalysisFrameIdentity(selected,cue.tracker.AcceptedTemplateIdentity()));
                Assert::AreEqual(weakMs,cue.tracker.WeakHoldMilliseconds());Assert::AreEqual(handoffs,cue.tracker.HandoffSources());
                Assert::IsFalse(cue.tracker.AcceptedCueSeedCertificate().Matches(cue.preview,cue.accepted));
                if(repeat)Assert::IsTrue(observations==next.accepted.observations);
                observations=next.accepted.observations;
                const auto geometry=compositor.Consume(next.accepted,raw);Assert::IsTrue(geometry.valid);
                AssertGeometryExceptCleanup(original,geometry);
                if(seq==3) {
                    SubtitleBackingLipContinuity cap;
                    const auto exhausted=cap.Consume(bounded,raw,next.accepted,Identity(seq),7,8,65536,&geometry);
                    Assert::IsFalse(exhausted.valid);
                    Assert::IsTrue(std::strcmp(exhausted.reason,"lip-work-budget")==0);
                    const auto fresh=cap.Consume(frame.Source(),raw,next.accepted,Identity(seq),7,8,65536,&geometry);
                    Assert::IsTrue(fresh.valid,L"Raw fallback does not replace fresh finite backing proof.");
                }
            }
        }
        auto complete=rawAt(11);complete.available=false;complete.current.analyzed=false;complete.current.text={};
        const auto recovered=ConsumeCurrentSubtitleCue(guard,cue.tracker,complete,frame.Source(),base,true,250,41.708);
        Assert::IsTrue(recovered.proof.completeCurrentCue && recovered.accepted.detected);
        Assert::IsTrue(SameAnalysisFrameIdentity(selected,recovered.proof.previousIdentity));
        Assert::AreEqual(cue.accepted.cue,recovered.accepted.cue);
        const auto raw=rawAt(12);AnalysisLumaReadBudget budget{1};auto bounded=frame.Source();bounded.readBudget=&budget;
        const auto resumed=ConsumeCurrentSubtitleCue(guard,cue.tracker,raw,bounded,base,true,250,41.708);
        Assert::IsTrue(resumed.accepted.detected && !resumed.proof.valid);
        Assert::IsTrue(SameAnalysisFrameIdentity(Identity(11),guard.LastVerifiedIdentity()),
            L"A complete source resets the separate display cursor without changing its own proof semantics.");
    }
    TEST_METHOD(CurrentRawFallbackCursorCannotBridgeUnknownGapOrChangedRepeat) {
        for(int mode=0;mode<4;++mode) {
            Frame frame(1,Format::P210);frame.Lip(4,true);auto cue=Acquire(frame);
            const ActivePictureBounds base{0,frame.pictureTop,frame.width,frame.pictureBottom,frame.width,frame.height};
            SubtitleCueContinuationGuard guard;Assert::IsTrue(guard.Remember(cue.preview,cue.accepted,base,frame.Source()));
            const auto selected=guard.LastVerifiedIdentity();auto raw=cue.preview;raw.current.identity=Identity(3);
            raw.current.independentBarAnalysis=false;raw.current.barEvidence.bottomRevalidatedNearby=true;
            const auto unknown=[&](const SubtitleBoxPreview& p) {AnalysisLumaReadBudget budget{1};auto bounded=frame.Source();bounded.readBudget=&budget;
                return ConsumeCurrentSubtitleCue(guard,cue.tracker,p,bounded,base,true,250,41.708);};
            Assert::IsTrue(unknown(raw).accepted.detected);
            auto rejected=raw;
            if(mode==0) {rejected.current.identity=Identity(4);rejected.available=false;rejected.current.analyzed=false;rejected.current.text={};}
            if(mode==1) {auto changed=std::make_shared<SubtitleInkSnapshot>(*raw.current.ink);changed->rawInk[0]^=1;rejected.current.ink=changed;}
            if(mode==2) {++rejected.current.text.lineBounds[0].right;}
            if(mode==3)rejected.current.ink=std::make_shared<SubtitleInkSnapshot>(*raw.current.ink);
            const auto blank=unknown(rejected);Assert::IsTrue(blank.proof.candidateRetained && !blank.accepted.detected);
            auto following=raw;following.current.identity=Identity(mode==0?5:4);
            Assert::IsFalse(unknown(following).accepted.detected,
                L"Rejected or missing raw evidence clears the display cursor; it cannot invent a predecessor.");
            following.current.identity=Identity(6);following.available=false;following.current.analyzed=false;following.current.text={};
            const auto recovered=ConsumeCurrentSubtitleCue(guard,cue.tracker,following,frame.Source(),base,true,250,41.708);
            Assert::IsTrue(recovered.proof.completeCurrentCue && recovered.accepted.detected);
            Assert::IsTrue(SameAnalysisFrameIdentity(selected,recovered.proof.previousIdentity));
        }
    }
    TEST_METHOD(CurrentRawFallbackCursorCannotSurviveContextResetOrNewCurrentCue) {
        for(int fault=0;fault<5;++fault) {
            Frame frame(1,Format::P210);frame.Lip(4,true);auto cue=Acquire(frame);
            const ActivePictureBounds base{0,frame.pictureTop,frame.width,frame.pictureBottom,frame.width,frame.height};
            SubtitleCueContinuationGuard guard;Assert::IsTrue(guard.Remember(cue.preview,cue.accepted,base,frame.Source()));
            auto raw=cue.preview;raw.current.identity=Identity(3);raw.current.independentBarAnalysis=false;
            raw.current.barEvidence.bottomRevalidatedNearby=true;
            const auto unknown=[&](const SubtitleBoxPreview& p) {AnalysisLumaReadBudget budget{1};auto bounded=frame.Source();bounded.readBudget=&budget;
                return ConsumeCurrentSubtitleCue(guard,cue.tracker,p,bounded,base,true,250,41.708);};
            Assert::IsTrue(unknown(raw).accepted.detected);auto changed=raw;changed.current.identity=Identity(4);
            if(fault==0)++changed.current.identity.transportGeneration;
            if(fault==1)++changed.current.identity.sourceFormatGeneration;
            if(fault==2){++changed.policyGeneration;++changed.current.policyGeneration;}
            if(fault==3)changed.current.discontinuity=true;
            if(fault==4) {
                // A real changed current glyph snapshot must not borrow the old
                // raw cursor even though the local inspector lacks work budget.
                frame.Fill({240,frame.pictureBottom+2,248,frame.pictureBottom+16},frame.floor);
                changed=Resolve({frame.Measure(4)});changed.current.independentBarAnalysis=false;
                changed.current.barEvidence.bottomRevalidatedNearby=true;
            }
            const auto next=unknown(changed);
            Assert::IsFalse(next.accepted.detected && next.accepted.cue==cue.accepted.cue);
            auto missing=changed;missing.current.identity=Identity(5);missing.available=false;missing.current.analyzed=false;missing.current.text={};
            const auto after=unknown(missing);Assert::IsFalse(after.accepted.detected);
        }
    }
    TEST_METHOD(CompleteCurrentWorkerCoverageReusesNearbyPlaneOnlyAfterFreshNativeProof) {
        for(bool top:{false,true})for(int provenance=0;provenance<3;++provenance) {
            Frame frame(1,Format::P210,top);frame.Lip(4,true);auto cue=Acquire(frame,20);
            const ActivePictureBounds base{0,frame.pictureTop,frame.width,frame.pictureBottom,frame.width,frame.height};
            SubtitleCueContinuationGuard guard;Assert::IsTrue(guard.Remember(cue.preview,cue.accepted,base,frame.Source()));
            auto raw=frame.Measure(3,20);SubtitleBoxDetector complete;complete.SetNearBarDistance(20);
            raw.text=complete.Analyze(frame.Source(),raw.pictureTop,raw.pictureBottom,3,raw.identity.viewportGeneration,true);
            raw.ink=complete.InkSnapshot();Assert::IsTrue(raw.text.detected && raw.text.configuredSearchComplete && raw.ink);
            raw.independentBarAnalysis=false;
            if(top) {raw.barEvidence.topRevalidatedNearby=provenance!=1;raw.barEvidence.topRevalidatedAtReference=provenance!=0;}
            else {raw.barEvidence.bottomRevalidatedNearby=provenance!=1;raw.barEvidence.bottomRevalidatedAtReference=provenance!=0;}
            const auto preview=Resolve({raw});AnalysisLumaReadBudget budget{SubtitleCurrentCueFrameSampleLimit};
            auto source=frame.Source();source.readBudget=&budget;const auto proof=guard.InspectCurrent(preview,source);
            Assert::IsTrue(proof.completeCurrentCue && proof.valid && proof.currentInk==raw.ink,
                L"Exact complete current worker coverage can follow fresh own-plane proof regardless of nearby/reference nomination.");
            Assert::IsTrue(proof.samples>0 && proof.ink!=raw.ink && !proof.ink->rawEvidenceComplete && !budget.exhausted);
            Assert::IsTrue(proof.MatchesSource(raw.identity,7,8) && proof.MatchesCue(cue.accepted));
            Assert::IsFalse(preview.current.independentBarAnalysis);
        }
    }
    TEST_METHOD(CompleteWorkerReuseCannotPromoteIncompleteWeakOrNoncurrentCoverage) {
        for(int fault=0;fault<12;++fault) {
            Frame frame(1,Format::P210);frame.Lip(4,true);auto cue=Acquire(frame,20);
            const ActivePictureBounds base{0,frame.pictureTop,frame.width,frame.pictureBottom,frame.width,frame.height};
            SubtitleCueContinuationGuard guard;Assert::IsTrue(guard.Remember(cue.preview,cue.accepted,base,frame.Source()));
            auto raw=frame.Measure(3,20);SubtitleBoxDetector complete;complete.SetNearBarDistance(20);
            raw.text=complete.Analyze(frame.Source(),raw.pictureTop,raw.pictureBottom,3,raw.identity.viewportGeneration,true);
            raw.ink=complete.InkSnapshot();Assert::IsTrue(raw.text.detected && raw.text.configuredSearchComplete && raw.ink);
            raw.independentBarAnalysis=false;raw.barEvidence.bottomRevalidatedNearby=true;
            if(fault==0)raw.text.configuredSearchComplete=false;
            if(fault==1)raw.text.bottomSearchStart=raw.pictureBottom;
            if(fault==2)raw.pendingRefresh=true;
            if(fault==3)raw.analysisRefresh=true;
            if(fault==4)raw.barTrackingAuthority=true;
            if(fault==5)raw.currentAnchorUsesTrackedEdge=true;
            if(fault==6)raw.barEvidence.captionBackedBottom=true;
            if(fault==7)raw.glyphLocalBottom=true;
            if(fault==8){auto ink=std::make_shared<SubtitleInkSnapshot>(*raw.ink);ink->rawEvidenceComplete=false;raw.ink=ink;}
            if(fault==9)raw.cadenceSkipped=true;
            if(fault==10)++raw.barEvidence.bottom;
            if(fault==11)raw.analyzed=false;
            const auto preview=Resolve({raw});const auto proof=guard.InspectCurrent(preview,frame.Source());
            Assert::IsTrue(proof.completeCurrentCue && proof.valid && proof.currentInk!=raw.ink,
                L"Weak or incomplete worker provenance still requires a complete fresh native detector scan.");
        }
    }
    TEST_METHOD(CompleteWorkerReuseRejectsChangedPlaneEpochAndNewBarInk) {
        for(int fault=0;fault<8;++fault) {
            Frame frame(1,Format::P210);frame.Lip(4,true);auto cue=Acquire(frame,20);
            const ActivePictureBounds base{0,frame.pictureTop,frame.width,frame.pictureBottom,frame.width,frame.height};
            SubtitleCueContinuationGuard guard;Assert::IsTrue(guard.Remember(cue.preview,cue.accepted,base,frame.Source()));
            if(fault==3)frame.Fill({0,frame.pictureTop,frame.width,frame.pictureBottom},frame.floor);
            if(fault==7)frame.Fill({240,frame.pictureBottom+2,248,frame.pictureBottom+16},frame.floor);
            if(fault==4)for(int n=0;n<6;++n) {
                const int x=24+n*13,y=frame.pictureBottom+8;
                frame.Fill({x,y,x+8,y+14},700);frame.Fill({x+2,y+2,x+6,y+12},frame.floor);
            }
            auto raw=cue.preview.current;raw.identity=Identity(3);SubtitleBoxDetector complete;complete.SetNearBarDistance(20);
            raw.text=complete.Analyze(frame.Source(),raw.pictureTop,raw.pictureBottom,3,raw.identity.viewportGeneration,true);
            raw.ink=complete.InkSnapshot();raw.independentBarAnalysis=false;raw.barEvidence.bottomRevalidatedNearby=true;
            if(fault==0){++raw.pictureBottom;++raw.barEvidence.bottom;}
            if(fault==1)++raw.policyGeneration;
            if(fault==2)raw.identity=Identity(1);
            if(fault==5)++raw.identity.transportGeneration;
            if(fault==6)++raw.identity.sourceFormatGeneration;
            const auto proof=guard.InspectCurrent(Resolve({raw}),frame.Source());
            Assert::IsFalse(proof.valid || proof.completeCurrentCue);
            Assert::IsFalse(proof.currentInk!=nullptr,L"No current worker shortcut may precede native/context/residual verification.");
        }
    }
    TEST_METHOD(CurrentPlaneReproofRejectsWrongNativeRowSceneBlackAndBarIntrusion) {
        for(bool top:{false,true})for(int fault=0;fault<5;++fault) {
            Frame frame(6,Format::DecodedDv,top);frame.Lip(30);auto cue=Acquire(frame);
            auto& raw=cue.preview.current;raw.independentBarAnalysis=false;
            raw.barEvidence.topRevalidatedNearby=raw.barEvidence.bottomRevalidatedNearby=true;
            if(fault<2) {
                const int delta=fault==0?-1:1;
                if(top)raw.pictureTop=raw.barEvidence.top=frame.pictureTop+delta;
                else raw.pictureBottom=raw.barEvidence.bottom=frame.pictureBottom+delta;
            }
            if(fault==2)frame.Fill({0,frame.pictureTop,frame.width,frame.pictureBottom},frame.floor);
            if(fault==3)frame.Fill({0,frame.pictureBottom+8,frame.width,frame.pictureBottom+16},700);
            if(fault==4)frame.Fill({0,frame.pictureBottom,frame.width,frame.pictureBottom+16},300);
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            LogProof(proof,frame,"current-plane-negative");
            Assert::IsFalse(proof.valid);Assert::IsTrue(proof.samples<=65536);
            Assert::IsTrue((proof.contextRejectMask&(1u<<13))!=0);
            Assert::IsFalse(proof.cuePlaneVerified);
        }
    }
    TEST_METHOD(CurrentPlaneAndFiniteBodyShareOneExactWorkBudget) {
        Frame frame(6,Format::DecodedDv);const auto lip=frame.Lip(30,true);auto cue=Acquire(frame);
        cue.preview.current.independentBarAnalysis=false;
        cue.preview.current.barEvidence.bottomRevalidatedNearby=true;
        const auto full=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
        Assert::IsTrue(full.valid && full.cuePlaneVerified && Same(lip,full.cleanup));
        Assert::IsTrue(full.cuePlaneSamples>0 && full.samples>full.cuePlaneSamples && full.samples<=65536);
        for(size_t budget:{size_t(1),size_t(1024),full.cuePlaneSamples,full.samples-1}) {
            const auto limited=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8,budget);
            Assert::IsFalse(limited.valid);Assert::IsTrue(limited.samples<=budget);
            Assert::IsTrue(std::string(limited.reason)=="lip-work-budget");
        }
        const auto exact=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8,full.samples);
        Assert::IsTrue(exact.valid && exact.samples==full.samples && Same(lip,exact.cleanup));
        AnalysisLumaReadBudget sharedBudget{1024};auto bounded=frame.Source();bounded.readBudget=&sharedBudget;
        const auto sharedLimited=ProveCurrentSubtitleBackingLip(bounded,cue.preview,cue.accepted,Identity(2),7,8);
        Assert::IsFalse(sharedLimited.valid);Assert::IsTrue(sharedBudget.exhausted && sharedBudget.used<=sharedBudget.limit);
        Assert::IsTrue(std::string(sharedLimited.reason)=="lip-work-budget");
    }
    TEST_METHOD(CompleteLocalCueKeepsFiniteCapWhenRawWorkerObservationIsUnavailable) {
        for(bool top:{false,true}) {
            Frame frame(1,Format::P210,top);const auto lip=frame.Lip(4,true);auto cue=Acquire(frame);
            const ActivePictureBounds base{0,frame.pictureTop,frame.width,frame.pictureBottom,frame.width,frame.height};
            SubtitleCueContinuationGuard guard;
            Assert::IsTrue(guard.Remember(cue.preview,cue.accepted,base,frame.Source()));
            auto pending=cue.preview;pending.available=false;auto& raw=pending.current;
            raw.identity=Identity(3);raw.analyzed=false;raw.barAuthority=false;raw.independentBarAnalysis=false;
            raw.barEvidence={};raw.pictureTop=0;raw.pictureBottom=frame.height;raw.text={};raw.ink.reset();
            const auto complete=guard.InspectCurrent(pending,frame.Source());
            Logger::WriteMessage(complete.reason);
            Assert::IsTrue(complete.completeCurrentCue && complete.MatchesSource(Identity(3),7,8,cue.accepted));
            const auto geometry=Geometry(cue);
            Assert::IsFalse(ProveCurrentSubtitleBackingLip(frame.Source(),pending,cue.accepted,Identity(3),7,8,65536,nullptr,&geometry).valid);
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),pending,cue.accepted,Identity(3),7,8,65536,nullptr,&geometry,&complete);
            LogProof(proof,frame,"complete-local-current-cap");
            Assert::IsTrue(proof.valid && proof.cuePlaneVerified && Same(lip,proof.cleanup));
            Assert::IsFalse(pending.available || raw.analyzed || raw.barAuthority || raw.barEvidence.available);
            for(int fault=0;fault<5;++fault) {
                auto invalid=complete;
                if(fault==0)invalid.completeCurrentCue=false;if(fault==1)invalid.valid=false;
                if(fault==2)++invalid.identity.acceptedSequence;if(fault==3)invalid.currentInk.reset();
                if(fault==4)invalid.fromTopBar=!invalid.fromTopBar;
                Assert::IsFalse(ProveCurrentSubtitleBackingLip(frame.Source(),pending,cue.accepted,Identity(3),7,8,65536,nullptr,&geometry,&invalid).valid);
            }
            frame.Pixel(frame.width/2,frame.pictureBottom-2,700);
            Assert::IsFalse(ProveCurrentSubtitleBackingLip(frame.Source(),pending,cue.accepted,Identity(3),7,8,65536,nullptr,&geometry,&complete).valid,
                L"A complete cue certificate cannot authorize stale backing pixels.");
        }
    }
    TEST_METHOD(OptionalNativeRecurrenceCoordinatorKeepsCueGeometryAndCapWithinProductionBudgets) {
        char* value=nullptr;size_t size=0;_dupenv_s(&value,&size,"VP_SUBTITLE_CURRENT_CAP_REPLAY_PATH");
        const std::string path=value?value:"";std::free(value);
        if(path.empty()){Logger::WriteMessage("Native current cue/cap integration replay not requested");return;}
        std::ifstream data(path,std::ios::binary),metadata(path+".txt");
        Assert::IsTrue(bool(data) && bool(metadata));
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(data)),{});
        AnalysisLumaSource source;int format=0,encoding=0,color=0,top=0,bottom=0,script=0,range=0;
        metadata>>source.width>>source.height>>source.rowBytes>>source.chromaRowBytes>>format>>encoding>>color>>top>>bottom>>script>>range;
        source.data=bytes.data();source.dataBytes=bytes.size();source.generation=2;
        source.format=static_cast<AnalysisLumaFormat>(format);source.encoding=static_cast<VideoFrameEncoding>(encoding);
        source.colorspace=static_cast<ColorSpace>(color);source.range=static_cast<AnalysisLumaRange>(range);
        Assert::IsTrue(source.IsValid());Assert::AreEqual(3840,source.width);Assert::AreEqual(2160,source.height);
        const ActivePictureBounds base{0,376,3840,1784,3840,2160};
        const SubtitleBoxRect nativeCap{1285,1746,2555,1776};
        for(int nearBar:{20,200})for(bool sharedAvailable:{true,false}) {
            SubtitlePictureAuthority shared;shared.required=true;shared.identity=Identity(1);shared.bounds=base;
            const auto measure=[&](uint64_t seq){SubtitleBoxDetector detector;
                detector.SetScriptHint(static_cast<SubtitleScriptHint>(script));detector.SetNearBarDistance(nearBar);
                return SubtitleBoxLookahead::Measure(detector,source,ActivePictureEvidence{},Identity(seq),false,{},7,8,shared);};
            const auto first=measure(1),second=measure(2);
            Assert::IsTrue(first.text.detected && second.text.detected);
            SubtitleCueContinuationGuard guard;SubtitleBoxPresentation tracker;SubtitleCutPastePresentation compositor;
            SubtitleBackingLipContinuity backing;SubtitlePendingMeasurementGuard pendingGuard;
            auto acquired=Resolve({first,second});
            auto decision=ConsumeCurrentSubtitleCue(guard,tracker,acquired,source,base,true,250,41.708);
            auto current=Resolve({second});
            decision=ConsumeCurrentSubtitleCue(guard,tracker,current,source,base,true,250,41.708);
            Assert::IsTrue(decision.accepted.detected && guard.HasReference());
            Assert::IsTrue(pendingGuard.RememberAccepted(current,decision.accepted,source,41.708,250,
                &tracker.AcceptedCueSeedCertificate()));
            const auto cue=decision.accepted.cue;const SubtitleBoxPadding padding{60,28,20};
            const auto original=compositor.Consume(decision.accepted,current,padding,15);
            Assert::IsTrue(original.valid);
            const auto initialCap=backing.Consume(source,current,decision.accepted,Identity(2),7,8,65536,&original);
            Assert::IsTrue(initialCap.valid && Same(nativeCap,initialCap.cleanup));
            for(uint64_t sequence=3;sequence<=12;++sequence) {
                auto pending=current;pending.available=false;pending.text={};auto& raw=pending.current;
                raw.identity=Identity(sequence);raw.analyzed=false;raw.barAuthority=false;raw.independentBarAnalysis=false;
                raw.barTrackingAuthority=false;raw.currentAnchorUsesTrackedEdge=false;raw.barEvidence={};
                raw.pictureTop=0;raw.pictureBottom=source.height;raw.text={};raw.ink.reset();
                if(!sharedAvailable){raw.sharedPicture={};raw.sharedPicture.required=true;}
                AnalysisLumaReadBudget budget{SubtitleCurrentCueFrameSampleLimit};auto bounded=source;bounded.readBudget=&budget;
                // Include the renderer's real pending prepass in the same frame
                // budget, with both a matching and missing required AR hint.
                const bool pendingVerified=pendingGuard.Resolve(pending,bounded,41.708,250);
                const auto pendingSamples=budget.used;
                if(sequence==3)Assert::AreEqual(sharedAvailable,pendingVerified);
                const bool availableBefore=pending.available,analyzedBefore=raw.analyzed,
                    barAuthorityBefore=raw.barAuthority,barsAvailableBefore=raw.barEvidence.available;
                // Call the exact production coordinator with its default
                // inspector budget. No hand-built positive proof or enlarged
                // test-only budget can conceal a real 4K fallback failure.
                const auto next=ConsumeCurrentSubtitleCue(guard,tracker,pending,bounded,base,true,250,41.708,pendingVerified);
                if(!next.proof.completeCurrentCue)Logger::WriteMessage(next.proof.reason);
                Assert::IsTrue(next.proof.completeCurrentCue && next.accepted.detected && !next.accepted.held);
                Assert::AreEqual(cue,next.accepted.cue);
                const auto geometry=compositor.Consume(next.accepted,pending,padding,15,nullptr,&next.proof);
                Assert::IsTrue(geometry.valid);AssertGeometryExceptCleanup(original,geometry);
                const auto cap=backing.Consume(bounded,pending,next.accepted,Identity(sequence),7,8,65536,&geometry,&next.proof);
                std::ostringstream report;report<<"native coordinator near="<<nearBar<<" shared="<<sharedAvailable<<" sequence="<<sequence
                    <<" cue="<<cue<<" pending="<<pendingSamples<<" proof="<<next.proof.samples<<" cap="<<cap.samples
                    <<" total="<<budget.used<<" reason="<<cap.reason;
                Logger::WriteMessage(report.str().c_str());
                Assert::IsTrue(cap.valid && cap.cuePlaneVerified && Same(nativeCap,cap.cleanup));
                Assert::IsTrue(next.proof.samples<=SubtitleCurrentCueInspectionSampleLimit && cap.samples<=65536);
                Assert::IsTrue(!budget.exhausted && budget.used<=SubtitleCurrentCueFrameSampleLimit);
                const auto applied=ApplyCurrentSubtitleBackingLip(geometry,cap,Identity(sequence),7,8);
                AssertGeometryExceptCleanup(original,applied);Assert::IsTrue(Same(nativeCap,applied.generatedCleanup));
                Assert::AreEqual(availableBefore,pending.available);Assert::AreEqual(analyzedBefore,raw.analyzed);
                Assert::AreEqual(barAuthorityBefore,raw.barAuthority);Assert::AreEqual(barsAvailableBefore,raw.barEvidence.available);
                const auto reads=budget.used;
                const auto repeated=ConsumeCurrentSubtitleCue(guard,tracker,pending,bounded,base,true,250,41.708,pendingVerified);
                Assert::IsTrue(repeated.accepted.detected && repeated.proof.completeCurrentCue);
                Assert::AreEqual(cue,repeated.accepted.cue);Assert::AreEqual(reads,budget.used);
            }
        }
    }
    TEST_METHOD(OptionalNativeNearbyFirstCueSeedsCandidateAndSurvivesCurrentMetadataLoss) {
        char* value=nullptr;size_t size=0;_dupenv_s(&value,&size,"VP_SUBTITLE_CURRENT_CAP_REPLAY_PATH");
        const std::string path=value?value:"";std::free(value);
        if(path.empty()){Logger::WriteMessage("Native nearby first-cue replay not requested");return;}
        std::ifstream data(path,std::ios::binary),metadata(path+".txt");
        Assert::IsTrue(bool(data) && bool(metadata));
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(data)),{});
        AnalysisLumaSource source;int format=0,encoding=0,color=0,top=0,bottom=0,script=0,range=0;
        metadata>>source.width>>source.height>>source.rowBytes>>source.chromaRowBytes>>format>>encoding>>color>>top>>bottom>>script>>range;
        source.data=bytes.data();source.dataBytes=bytes.size();source.generation=2;
        source.format=static_cast<AnalysisLumaFormat>(format);source.encoding=static_cast<VideoFrameEncoding>(encoding);
        source.colorspace=static_cast<ColorSpace>(color);source.range=static_cast<AnalysisLumaRange>(range);
        Assert::IsTrue(source.IsValid());Assert::AreEqual(3840,source.width);Assert::AreEqual(2160,source.height);
        const ActivePictureBounds base{0,376,3840,1784,3840,2160};const SubtitleBoxRect nativeCap{1285,1746,2555,1776};
        for(int nearBar:{20,200}) {
            SubtitlePictureAuthority shared;shared.required=true;shared.identity=Identity(1);shared.bounds=base;
            const auto measure=[&](uint64_t seq){SubtitleBoxDetector detector;
                detector.SetScriptHint(static_cast<SubtitleScriptHint>(script));detector.SetNearBarDistance(nearBar);
                auto observation=SubtitleBoxLookahead::Measure(detector,source,ActivePictureEvidence{},Identity(seq),false,{},7,8,shared);
                // Inject the recorded nearby metadata at FIRST acquisition,
                // over preserved native bytes; no favorable independent seed.
                observation.independentBarAnalysis=false;
                observation.barEvidence.topRevalidatedNearby=observation.barEvidence.bottomRevalidatedNearby=true;
                return observation;};
            const auto first=measure(1),second=measure(2);
            Assert::IsTrue(first.text.detected && second.text.detected);
            Assert::IsFalse(first.text.configuredSearchComplete || second.text.configuredSearchComplete);
            SubtitleCueContinuationGuard guard;SubtitleBoxPresentation tracker;SubtitleCutPastePresentation compositor;
            SubtitleBackingLipContinuity backing;AnalysisLumaReadBudget acquisitionBudget{SubtitleCurrentCueFrameSampleLimit};
            auto bounded=source;bounded.readBudget=&acquisitionBudget;
            auto acquired=Resolve({first,second});
            auto decision=ConsumeCurrentSubtitleCue(guard,tracker,acquired,bounded,base,true,250,41.708);
            Assert::IsTrue(decision.accepted.detected && guard.HasReference());
            Assert::IsFalse(decision.proof.valid || decision.proof.completeCurrentCue,
                L"Cold acquisition stores only a candidate; it cannot manufacture a COMPLETE output proof.");
            const auto firstReads=acquisitionBudget.used;
            acquisitionBudget={SubtitleCurrentCueFrameSampleLimit};
            auto current=Resolve({second});
            decision=ConsumeCurrentSubtitleCue(guard,tracker,current,bounded,base,true,250,41.708);
            Assert::IsTrue(decision.accepted.detected && guard.HasReference());
            const auto cue=decision.accepted.cue;const SubtitleBoxPadding padding{60,28,20};
            const auto original=compositor.Consume(decision.accepted,current,padding,15);
            const auto initialCap=backing.Consume(bounded,current,decision.accepted,Identity(2),7,8,65536,&original);
            Assert::IsTrue(original.valid && initialCap.valid && Same(nativeCap,initialCap.cleanup));
            Assert::IsTrue(!acquisitionBudget.exhausted && acquisitionBudget.used<=SubtitleCurrentCueFrameSampleLimit);
            std::ostringstream seeded;seeded<<"native cold nearby near="<<nearBar<<" first_reads="<<firstReads
                <<" second_seed_and_cap="<<acquisitionBudget.used;Logger::WriteMessage(seeded.str().c_str());
            for(uint64_t seq=3;seq<=12;++seq) {
                auto pending=current;auto& raw=pending.current;raw.identity=Identity(seq);
                if(seq%2) {raw.pictureTop=raw.barEvidence.top=0;raw.text.topSearchComplete=false;}
                else {pending.available=false;pending.text={};raw.analyzed=false;raw.barAuthority=false;
                    raw.pictureTop=0;raw.pictureBottom=source.height;raw.barEvidence={};raw.text={};raw.ink.reset();raw.sharedPicture={};}
                AnalysisLumaReadBudget budget{SubtitleCurrentCueFrameSampleLimit};bounded=source;bounded.readBudget=&budget;
                const auto next=ConsumeCurrentSubtitleCue(guard,tracker,pending,bounded,base,true,250,41.708);
                Logger::WriteMessage(next.proof.reason);
                Assert::IsTrue(next.proof.completeCurrentCue && next.accepted.detected);
                Assert::AreEqual(cue,next.accepted.cue);
                const auto geometry=compositor.Consume(next.accepted,pending,padding,15,nullptr,&next.proof);
                AssertGeometryExceptCleanup(original,geometry);
                const auto cap=backing.Consume(bounded,pending,next.accepted,Identity(seq),7,8,65536,&geometry,&next.proof);
                Assert::IsTrue(cap.valid && Same(nativeCap,cap.cleanup));
                Assert::IsTrue(!budget.exhausted && budget.used<=SubtitleCurrentCueFrameSampleLimit);
                Assert::IsFalse(raw.independentBarAnalysis || raw.text.configuredSearchComplete);
                std::ostringstream out;out<<"native cold nearby near="<<nearBar<<" seq="<<seq<<" reads="<<budget.used
                    <<" inspector="<<next.proof.samples<<" cap="<<cap.samples;Logger::WriteMessage(out.str().c_str());
            }
        }
    }
    TEST_METHOD(OptionalStationaryNativeBudgetCannotConvertCandidateIntoOutputAuthority) {
        char* value=nullptr;size_t size=0;_dupenv_s(&value,&size,"VP_SUBTITLE_STATIONARY_REPLAY_PATH");
        const std::string path=value?value:"";std::free(value);
        if(path.empty()){Logger::WriteMessage("Stationary native budget replay not requested");return;}
        std::ifstream data(path,std::ios::binary),metadata(path+".txt");
        Assert::IsTrue(bool(data) && bool(metadata));
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(data)),{});
        AnalysisLumaSource source;int format=0,encoding=0,color=0,top=0,bottom=0,script=0,range=0;
        metadata>>source.width>>source.height>>source.rowBytes>>source.chromaRowBytes>>format>>encoding>>color>>top>>bottom>>script>>range;
        source.data=bytes.data();source.dataBytes=bytes.size();source.generation=2;
        source.format=static_cast<AnalysisLumaFormat>(format);source.encoding=static_cast<VideoFrameEncoding>(encoding);
        source.colorspace=static_cast<ColorSpace>(color);source.range=static_cast<AnalysisLumaRange>(range);
        Assert::IsTrue(source.IsValid());const ActivePictureBounds base{0,376,3840,1784,3840,2160};
        for(int nearBar:{20,200}) {
            const auto measure=[&](uint64_t seq){SubtitleBoxDetector detector;
                detector.SetScriptHint(static_cast<SubtitleScriptHint>(script));detector.SetNearBarDistance(nearBar);
                SubtitlePictureAuthority shared;shared.required=true;shared.identity=Identity(seq);shared.bounds=base;
                return SubtitleBoxLookahead::Measure(detector,source,ActivePictureEvidence{},Identity(seq),false,{},7,8,shared);};
            const auto first=measure(1),second=measure(2);auto acquired=Resolve({first,second});
            SubtitleCueContinuationGuard guard;SubtitleBoxPresentation tracker;SubtitleCutPastePresentation compositor;
            auto initial=ConsumeCurrentSubtitleCue(guard,tracker,acquired,source,base,true,250,41.708);
            auto current=Resolve({second});initial=ConsumeCurrentSubtitleCue(guard,tracker,current,source,base,true,250,41.708);
            Assert::IsTrue(initial.accepted.detected && guard.HasReference());
            const auto cue=initial.accepted.cue;const auto original=compositor.Consume(initial.accepted,current);
            Assert::IsTrue(original.valid);
            const SubtitleBoxRect nativeCap{1442,1746,2398,1776};SubtitleBackingLipContinuity backing;
            SubtitlePendingMeasurementGuard pendingGuard;
            Assert::IsTrue(pendingGuard.RememberAccepted(current,initial.accepted,source,41.708,250,
                &tracker.AcceptedCueSeedCertificate()));
            auto pending=current;pending.available=false;pending.current.analyzed=false;pending.current.identity=Identity(3);
            pending.current.sharedPicture.identity=Identity(3);
            AnalysisLumaReadBudget fullPipelineBudget{SubtitleCurrentCueFrameSampleLimit};auto boundedPipeline=source;boundedPipeline.readBudget=&fullPipelineBudget;
            const bool pendingVerified=pendingGuard.Resolve(pending,boundedPipeline,41.708,250);
            Assert::IsTrue(pendingVerified);const auto pendingReads=fullPipelineBudget.used;
            const auto full=ConsumeCurrentSubtitleCue(guard,tracker,pending,boundedPipeline,base,true,250,41.708,pendingVerified);
            Assert::IsTrue(full.proof.completeCurrentCue && full.accepted.detected && full.proof.samples<=SubtitleCurrentCueInspectionSampleLimit);
            const auto fullGeometry=compositor.Consume(full.accepted,pending,{},0,nullptr,&full.proof);
            Assert::IsTrue(fullGeometry.valid);AssertGeometryExceptCleanup(original,fullGeometry);
            const auto fullCap=backing.Consume(boundedPipeline,pending,full.accepted,Identity(3),7,8,65536,&fullGeometry,&full.proof);
            Assert::IsTrue(fullCap.valid && Same(nativeCap,fullCap.cleanup));
            Assert::IsTrue(!fullPipelineBudget.exhausted && fullPipelineBudget.used<=SubtitleCurrentCueFrameSampleLimit);
            std::ostringstream pipeline;pipeline<<"stationary complete near="<<nearBar<<" pending="<<pendingReads
                <<" inspector="<<full.proof.samples<<" cap="<<fullCap.samples<<" total="<<fullPipelineBudget.used;
            Logger::WriteMessage(pipeline.str().c_str());
            for(uint64_t seq=4;seq<=9;++seq) {
                auto pending=current;pending.current.identity=Identity(seq);pending.current.sharedPicture.identity=Identity(seq);
                // Even frames model an unavailable worker with deliberately
                // insufficient work. Odd frames restore the SAME raw context:
                // a retained candidate must retry its complete inspector.
                const bool forcedIncomplete=seq%2==0;
                if(forcedIncomplete){pending.available=false;pending.current.analyzed=false;pending.current.text={};}
                AnalysisLumaReadBudget budget{forcedIncomplete?size_t(1):SubtitleCurrentCueFrameSampleLimit};
                auto bounded=source;bounded.readBudget=&budget;
                const auto next=ConsumeCurrentSubtitleCue(guard,tracker,pending,bounded,base,true,250,41.708);
                Assert::IsTrue(next.proof.samples>0 && budget.used<=budget.limit);
                const auto geometry=compositor.Consume(next.accepted,pending,{},0,nullptr,&next.proof);
                if(next.proof.completeCurrentCue) {
                    Assert::IsFalse(forcedIncomplete);Assert::AreEqual(cue,next.accepted.cue);
                    Assert::IsTrue(geometry.valid);AssertGeometryExceptCleanup(original,geometry);
                    const auto cap=backing.Consume(bounded,pending,next.accepted,Identity(seq),7,8,65536,&geometry,&next.proof);
                    Assert::IsTrue(cap.valid && Same(nativeCap,cap.cleanup));
                    Assert::IsTrue(!budget.exhausted && budget.used<=SubtitleCurrentCueFrameSampleLimit);
                } else {
                    Assert::IsTrue(forcedIncomplete,L"Production budgets must complete this stationary native fixture.");
                    backing.Reset();
                    Assert::IsTrue(next.proof.candidateRetained && guard.HasReference());
                    Assert::IsFalse(next.proof.valid || next.accepted.detected || geometry.valid || geometry.generatedCleanup.Valid());
                }
                std::ostringstream out;out<<"stationary native near="<<nearBar<<" seq="<<seq<<" complete="<<next.proof.completeCurrentCue
                    <<" retained="<<next.proof.candidateRetained<<" reads="<<budget.used<<" reason="<<next.proof.reason;
                Logger::WriteMessage(out.str().c_str());
            }
            // Simulate a completed current worker packet, not a copied or
            // promoted narrow observation. Its full scan runs outside the
            // render-thread budget, as the actual worker does.
            auto completeWorker=measure(10);SubtitleBoxDetector completeDetector;
            completeDetector.SetScriptHint(static_cast<SubtitleScriptHint>(script));completeDetector.SetNearBarDistance(nearBar);
            completeWorker.text=completeDetector.Analyze(source,completeWorker.pictureTop,completeWorker.pictureBottom,
                10,completeWorker.identity.viewportGeneration,true);completeWorker.ink=completeDetector.InkSnapshot();
            Assert::IsTrue(completeWorker.text.detected && completeWorker.text.configuredSearchComplete);
            completeWorker.independentBarAnalysis=false;completeWorker.barEvidence.bottomRevalidatedNearby=true;
            const auto workerPreview=Resolve({completeWorker});AnalysisLumaReadBudget reuseBudget{SubtitleCurrentCueFrameSampleLimit};
            auto reuseSource=source;reuseSource.readBudget=&reuseBudget;
            const auto reused=ConsumeCurrentSubtitleCue(guard,tracker,workerPreview,reuseSource,base,true,250,41.708);
            Assert::IsTrue(reused.proof.completeCurrentCue && reused.accepted.detected && reused.proof.currentInk==completeWorker.ink);
            const auto reusedGeometry=compositor.Consume(reused.accepted,workerPreview,{},0,nullptr,&reused.proof);
            Assert::IsTrue(reusedGeometry.valid);AssertGeometryExceptCleanup(original,reusedGeometry);
            const auto reusedCap=backing.Consume(reuseSource,workerPreview,reused.accepted,Identity(10),7,8,65536,&reusedGeometry,&reused.proof);
            Assert::IsTrue(reusedCap.valid && Same(nativeCap,reusedCap.cleanup) && !reuseBudget.exhausted);
            std::ostringstream reuse;reuse<<"stationary complete-worker reuse near="<<nearBar<<" inspector="<<reused.proof.samples
                <<" cap="<<reusedCap.samples<<" total="<<reuseBudget.used;Logger::WriteMessage(reuse.str().c_str());
        }
    }
    TEST_METHOD(OptionalPausedRecurrenceColdCapUsesCurrentPixelsWithLoggedNearbyMetadata) {
        char* value=nullptr;size_t size=0;_dupenv_s(&value,&size,"VP_SUBTITLE_CURRENT_CAP_REPLAY_PATH");
        const std::string path=value?value:"";std::free(value);
        if(path.empty()){Logger::WriteMessage("Paused recurrence cap replay not requested");return;}
        std::ifstream data(path,std::ios::binary),metadata(path+".txt");
        Assert::IsTrue(bool(data) && bool(metadata));
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(data)),{});
        AnalysisLumaSource source;int format=0,encoding=0,color=0,top=0,bottom=0,script=0,range=0;
        metadata>>source.width>>source.height>>source.rowBytes>>source.chromaRowBytes>>format>>encoding>>color>>top>>bottom>>script>>range;
        source.data=bytes.data();source.dataBytes=bytes.size();source.generation=2;
        source.format=static_cast<AnalysisLumaFormat>(format);source.encoding=static_cast<VideoFrameEncoding>(encoding);
        source.colorspace=static_cast<ColorSpace>(color);source.range=static_cast<AnalysisLumaRange>(range);
        Assert::IsTrue(source.IsValid());Assert::AreEqual(3840,source.width);Assert::AreEqual(2160,source.height);
        SubtitlePictureAuthority shared;shared.required=true;shared.identity=Identity(1);shared.bounds={0,376,3840,1784,3840,2160};
        const auto measure=[&](uint64_t seq){SubtitleBoxDetector detector;detector.SetScriptHint(static_cast<SubtitleScriptHint>(script));
            return SubtitleBoxLookahead::Measure(detector,source,ActivePictureEvidence{},Identity(seq),false,{},7,8,shared);};
        const auto first=measure(1),second=measure(2);Acquired cue;
        Assert::IsTrue(first.text.detected && second.text.detected);
        cue.tracker.Consume(Resolve({first,second}));cue.preview=Resolve({second});cue.accepted=cue.tracker.Consume(cue.preview);
        Assert::IsTrue(cue.accepted.detected && !cue.accepted.held);
        auto geometry=Geometry(cue);geometry.pictureTop=376;geometry.pictureBottom=1784;
        const SubtitleBoxRect nativeMeasured{1285,1746,2555,1776};
        for(int mode=0;mode<2;++mode){
            auto preview=cue.preview;
            // The post-restart native capture independently proves its bars.
            // Reproduce the pre-restart logged nearby flags over those exact
            // current pixels explicitly; this is not a captured frame sequence.
            if(mode){preview.current.independentBarAnalysis=false;
                preview.current.barEvidence.topRevalidatedNearby=true;preview.current.barEvidence.bottomRevalidatedNearby=true;}
            SubtitleBackingLipContinuity cold;
            const auto proof=cold.Consume(source,preview,cue.accepted,Identity(2),7,8,65536,&geometry);
            std::ostringstream report;report<<"paused recurrence mode="<<mode<<" valid="<<proof.valid<<" reason="<<proof.reason
                <<" plane="<<proof.cuePlaneVerified<<" samples="<<proof.samples<<" plane_samples="<<proof.cuePlaneSamples;
            Logger::WriteMessage(report.str().c_str());Assert::IsTrue(proof.valid && Same(nativeMeasured,proof.cleanup));
            if(mode)Assert::IsTrue(proof.cuePlaneVerified && proof.cuePlaneSamples>0);
            const auto applied=ApplyCurrentSubtitleBackingLip(geometry,proof,Identity(2),7,8);
            AssertGeometryExceptCleanup(geometry,applied);Assert::IsTrue(Same(nativeMeasured,applied.generatedCleanup));
        }
    }
    TEST_METHOD(KnownCurrentBackingIgnoresOppositeBarMeasurementLossAndReferenceFlags) {
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})for(bool top:{false,true}) {
            Frame frame(6,format,top);const auto strip=frame.Lip(30);const auto cue=Acquire(frame);
            const auto geometry=Geometry(cue);SubtitleBackingLipContinuity continuity;
            const auto first=continuity.Consume(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8,65536,&geometry);
            Assert::IsTrue(first.valid);
            for(int mode=0;mode<4;++mode) {
                const uint64_t seq=uint64_t(mode+3);auto preview=cue.preview;auto& c=preview.current;
                c.identity=Identity(seq);c.independentBarAnalysis=false;
                if(top) {
                    if(mode==0)c.pictureBottom=c.barEvidence.bottom=frame.height;
                    if(mode==1)c.barEvidence.captionBackedBottom=true;
                    if(mode==2)c.barEvidence.bottomRevalidatedAtReference=true;
                    if(mode==3)c.barEvidence.bottomRevalidatedNearby=true;
                }else {
                    if(mode==0)c.pictureTop=c.barEvidence.top=0;
                    if(mode==1)c.barEvidence.captionBackedTop=true;
                    if(mode==2)c.barEvidence.topRevalidatedAtReference=true;
                    if(mode==3)c.barEvidence.topRevalidatedNearby=true;
                }
                const auto cold=ProveCurrentSubtitleBackingLip(frame.Source(),preview,cue.accepted,Identity(seq),7,8);
                Assert::IsTrue(cold.valid && cold.cuePlaneVerified && Same(cold.cleanup,strip),
                    L"Only a fresh cue-side native plane proof, not the opposite metadata, may acquire finite backing.");
                const auto proof=continuity.Consume(frame.Source(),preview,cue.accepted,Identity(seq),7,8,65536,&geometry);
                LogProof(proof,frame,"opposite-bar-current-card");
                Assert::IsTrue(proof.valid && proof.revalidatedOutline && proof.outlineBaseAnchored);
                Assert::IsTrue(Same(proof.cleanup,strip) && proof.samples<=65536);
                const auto result=ApplyCurrentSubtitleBackingLip(geometry,proof,Identity(seq),7,8);
                AssertGeometryExceptCleanup(geometry,result);Assert::IsTrue(Same(result.generatedCleanup,strip));
                Assert::IsFalse(c.independentBarAnalysis);
            }
            auto preview=cue.preview;preview.current.identity=Identity(7);
            frame.Pixel(300*frame.scale,frame.pictureBottom-2,700);
            Assert::IsFalse(continuity.Consume(frame.Source(),preview,cue.accepted,Identity(7),7,8,65536,&geometry).valid);
        }
    }
    TEST_METHOD(KnownBackingSurvivesCaptionPlaneJitterWithoutBorrowingCropAuthority) {
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})
            for(bool top:{false,true})for(int delta:{1,2}) {
                Frame frame(6,format,top);const auto lip=frame.Lip(30,true);auto cue=Acquire(frame);
                auto geometry=Geometry(cue);geometry.pictureTop-=2;geometry.pictureBottom+=2;
                SubtitleBackingLipContinuity state;
                const auto first=state.Consume(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8,65536,&geometry);
                Assert::IsTrue(first.valid);
                auto observation=frame.Measure(3);auto current=Resolve({observation});
                const auto text=cue.tracker.Consume(current);Assert::IsTrue(text.detected && !text.held);
                current.current.independentBarAnalysis=false;
                if(top) {
                    current.current.pictureTop-=delta;
                    current.current.barEvidence.top=current.current.pictureTop;
                    current.current.barEvidence.captionBackedTop=true;
                } else {
                    current.current.pictureBottom+=delta;
                    current.current.barEvidence.bottom=current.current.pictureBottom;
                    current.current.barEvidence.captionBackedBottom=true;
                }
                const auto ink=current.current.ink;
                const auto noCandidate=ProveCurrentSubtitleBackingLip(frame.Source(),current,text,Identity(3),7,8,65536,nullptr,&geometry);
                Assert::IsFalse(noCandidate.valid,L"A caption-backed edge cannot acquire new cleanup");
                const auto proof=state.Consume(frame.Source(),current,text,Identity(3),7,8,65536,&geometry);
                LogProof(proof,frame,"caption-plane-jitter");
                Assert::IsTrue(proof.valid && proof.revalidatedOutline && proof.outlineBaseAnchored);
                Assert::IsTrue(Same(first.cleanup,proof.cleanup) && Same(lip,proof.cleanup));
                Assert::IsTrue(proof.samples<=65536 && current.current.ink==ink);
                Assert::IsFalse(current.current.independentBarAnalysis,L"Cleanup cannot promote raw evidence to native crop authority");
                const auto applied=ApplyCurrentSubtitleBackingLip(geometry,proof,Identity(3),7,8);
                AssertGeometryExceptCleanup(geometry,applied);Assert::IsTrue(Same(applied.generatedCleanup,lip));
                // A changed current body pixel revokes the same remembered mask.
                frame.Pixel(300*6,frame.pictureBottom-10,300);
                auto next=current;next.current.identity=Identity(4);
                const auto changed=state.Consume(frame.Source(),next,text,Identity(4),7,8,65536,&geometry);
                Assert::IsFalse(changed.valid);
            }
    }
    TEST_METHOD(RememberedBackingClosureToleratesSmallCurrentShadowWithoutGrowingCleanup) {
        for(bool top:{false,true}) {
            Frame frame(6,Format::DecodedDv,top);const auto lip=frame.Lip(30,true);auto cue=Acquire(frame);
            SubtitleBackingLipContinuity state;
            const auto first=state.Consume(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            Assert::IsTrue(first.valid);
            // A 51-pixel moving shadow at every collar row matches the live
            // rejection. Its pixels stay outside the remembered finite card.
            frame.Fill({1500,frame.pictureBottom-63,1551,frame.pictureBottom-30},frame.floor);
            auto current=Resolve({frame.Measure(3)});auto text=cue.tracker.Consume(current);
            const auto fresh=ProveCurrentSubtitleBackingLip(frame.Source(),current,text,Identity(3),7,8);
            Assert::IsFalse(fresh.valid,L"First acquisition retains the strict closure requirement");
            const auto retained=state.Consume(frame.Source(),current,text,Identity(3),7,8);
            LogProof(retained,frame,"small-moving-closure-shadow");
            Assert::IsTrue(retained.valid && retained.revalidatedOutline && Same(retained.cleanup,lip));
            Assert::IsTrue(retained.samples<=65536 && retained.closureDark*10<=retained.closureSamples);
            // A broad ambiguous shadow still cannot support the current outline.
            frame.Fill({1500,frame.pictureBottom-63,1740,frame.pictureBottom-30},frame.floor);
            auto broad=Resolve({frame.Measure(4)});text=cue.tracker.Consume(broad);
            const auto rejected=state.Consume(frame.Source(),broad,text,Identity(4),7,8);
            Assert::IsFalse(rejected.valid);
        }
    }
    TEST_METHOD(CurrentOutlineSurvivesSceneFlankAndClosureNoiseWithoutMoving) {
        for(bool top:{false,true})for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb}) {
            Frame frame(1,format,top);frame.Lip(8,true);auto cue=Acquire(frame);
            SubtitleBackingLipContinuity continuity;
            auto first=continuity.Consume(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            LogProof(first,frame,"outline-acquire");Assert::IsTrue(first.valid && !first.revalidatedOutline);
            // One dark exterior point defeated strict closure; a small dark
            // object touching a side defeated monotone/flank acquisition.
            frame.Pixel(232,frame.pictureBottom-9,frame.floor);
            frame.Fill({416,frame.pictureBottom-5,421,frame.pictureBottom-3},frame.floor);
            for(uint64_t seq=3;seq<20;++seq) {
                auto current=frame.Measure(seq);auto preview=Resolve({current});
                const auto text=cue.tracker.Consume(preview);
                auto proof=continuity.Consume(frame.Source(),preview,text,Identity(seq),7,8);
                LogProof(proof,frame,"outline-current");
                Assert::IsTrue(proof.valid && proof.revalidatedOutline);
                Assert::IsTrue(Same(first.cleanup,proof.cleanup));
                Assert::IsTrue(SameAnalysisFrameIdentity(proof.identity,Identity(seq)));
                Assert::IsTrue(proof.samples<=65536);
                const auto geometry=Geometry(cue);
                AssertGeometryExceptCleanup(geometry,ApplyCurrentSubtitleBackingLip(geometry,proof,Identity(seq),7,8));
            }
        }
    }
    TEST_METHOD(CurrentOutlineAntialiasCollarIsBoundedInsideTheMeasuredMask) {
        for(bool top:{false,true})for(int depth:{1,2,3}) {
            Frame native(2,Format::P210,top);native.Lip(16,true);auto cue=Acquire(native);
            SubtitleBackingLipContinuity continuity;
            const auto first=continuity.Consume(native.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            Assert::IsTrue(first.valid);
            native.Fill({448,584,448+depth,585},300);
            auto p=Resolve({native.Measure(3)});const auto accepted=cue.tracker.Consume(p);
            const auto next=continuity.Consume(native.Source(),p,accepted,Identity(3),7,8);
            if(depth<=2) {
                Assert::IsTrue(next.valid && next.revalidatedOutline);
                Assert::IsTrue(Same(first.cleanup,next.cleanup));
                for(int y=next.blackInterior.top;y<next.blackInterior.bottom;++y)
                    for(int x=next.blackInterior.left;x<next.blackInterior.right;++x) {
                        AnalysisLumaSample v;Assert::IsTrue(native.Source().Sample(x,y,v));
                        Assert::IsTrue(v.luma<=native.floor+4,L"AA collar is not certified black interior");
                    }
            } else Assert::IsFalse(next.valid,L"Three-pixel intrusion reaches the strict current body");
        }
    }
    TEST_METHOD(CurrentOutlineRejectsBrightOrColoredBodyEvenWithUnchangedGlyphs) {
        for(bool top:{false,true})for(bool colored:{false,true}) {
            Frame frame(1,Format::P210,top);frame.Lip(8,true);auto cue=Acquire(frame);
            SubtitleBackingLipContinuity continuity;
            Assert::IsTrue(continuity.Consume(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8).valid);
            if(colored)frame.Chroma({300,290,302,291},700,512);
            else frame.Pixel(300,290,500);
            auto preview=Resolve({frame.Measure(3)});const auto text=cue.tracker.Consume(preview);
            const auto proof=continuity.Consume(frame.Source(),preview,text,Identity(3),7,8);
            Assert::IsFalse(proof.valid);Assert::AreEqual("lip-outline-body-changed",proof.reason);
        }
    }
    TEST_METHOD(CurrentOutlineRetiresOnContradictionsButRevalidatesAfterBudgetFailure) {
        for(int failure=0;failure<5;++failure) {
            Frame frame;frame.Lip(8,true);auto cue=Acquire(frame);SubtitleBackingLipContinuity continuity;
            const auto initial=continuity.Consume(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            Assert::IsTrue(initial.valid);const auto geometry=Geometry(cue);
            auto preview=cue.preview;preview.current.identity=Identity(3);auto text=cue.accepted;
            if(failure==0)text.detected=false;
            if(failure==1)text.held=true;
            if(failure==2)preview.current.barTrackingAuthority=true;
            if(failure==3)frame.Fill({215,245,425,288},frame.floor);
            const auto proof=continuity.Consume(frame.Source(),preview,text,Identity(3),7,8,failure==4?16:65536);
            Assert::IsFalse(proof.valid);
            Assert::IsFalse(ApplyCurrentSubtitleBackingLip(geometry,proof,Identity(3),7,8).generatedCleanup.Valid());
            if(failure==4)Assert::IsTrue(std::strcmp(proof.reason,"lip-work-budget")==0);
            frame.Fill({215,280,425,288},300);
            preview=cue.preview;preview.current.identity=Identity(4);
            const auto next=continuity.Consume(frame.Source(),preview,cue.accepted,Identity(4),7,8);
            Assert::IsTrue(next.valid);
            if(failure==4) {
                Assert::IsTrue(next.revalidatedOutline && Same(initial.cleanup,next.cleanup));
                Assert::IsTrue(SameAnalysisFrameIdentity(next.identity,Identity(4)));
            } else Assert::IsFalse(next.revalidatedOutline,L"a cue or boundary contradiction must retire the remembered outline");
        }
    }
    TEST_METHOD(CurrentOutlineRetiresOnCuePolicyEpochAndSequenceChanges) {
        for(int change=0;change<10;++change) {
            Frame frame;frame.Lip(8,true);auto cue=Acquire(frame);SubtitleBackingLipContinuity continuity;
            Assert::IsTrue(continuity.Consume(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8).valid);
            auto preview=cue.preview;auto text=cue.accepted;auto id=Identity(3);
            uint64_t policy=7,epoch=8;
            if(change==0)++text.cue;
            if(change==1){text.signature[0]^=1;preview.current.text.signature=text.signature;}
            if(change==2)++id.sourceFormatGeneration;
            if(change==3)++id.viewportGeneration;
            if(change==4)++id.rendererGeneration;
            if(change==5)++id.analysisDomainKey;
            if(change==6){++policy;preview.policyGeneration=preview.current.policyGeneration=policy;}
            if(change==7){++epoch;preview.continuityGeneration=preview.current.continuityGeneration=epoch;}
            if(change==8)id=Identity(11);
            if(change==9)continuity.Reset();
            preview.current.identity=id;
            const auto proof=continuity.Consume(frame.Source(),preview,text,id,policy,epoch);
            Assert::IsTrue(proof.valid && !proof.revalidatedOutline,L"fresh proof may acquire, but old authority cannot cross a reset");
        }
    }
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
            // Scenery contact persists beyond the bounded collar; a short
            // attached shadow is covered by the positive collar regression.
            if(fault==3)frame.Fill({270,frame.pictureBottom-20,300,frame.pictureBottom-4},64);
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
            const bool nominationOnly=fault==11 || fault==16 || fault==22 || fault==23;
            if(nominationOnly)Assert::IsTrue(proof.valid && proof.cuePlaneVerified,
                L"Nomination flags do not override an independently re-proved current physical side.");
            else Assert::IsFalse(proof.valid);
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
    TEST_METHOD(LipApplyCannotReplaceExistingCleanupOrLeaveEitherCurrentPicture) {
        Frame frame;frame.Lip(3);const auto cue=Acquire(frame);
        const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
        const auto base=Geometry(cue);Assert::IsTrue(proof.valid);
        for(int fault=0;fault<11;++fault) {
            auto g=base;auto p=proof;auto expected=Identity(2);
            if(fault==0)g.generatedCleanup={10,280,20,296};if(fault==1)g.cleanupLimit={0,280,640,296};
            if(fault==2)g.valid=false;if(fault==3)--g.pictureBottom;if(fault==4)g.fromTopBar=!g.fromTopBar;
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

    TEST_METHOD(CurrentVisibleLipSurvivesOnePixelMeasuredCueSideExpansion) {
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})for(bool top:{false,true}) {
            Frame frame(6,format,top);const auto strip=frame.Lip(30);auto cue=Acquire(frame);
            const auto geometry=Geometry(cue);SubtitleBackingLipContinuity continuity;
            for(uint64_t seq=2;seq<10;++seq) {
                auto preview=cue.preview;preview.current.identity=Identity(seq);
                const int drift=int(seq%2);
                if(top) {preview.current.pictureTop-=drift;preview.current.barEvidence.top-=drift;}
                else {preview.current.pictureBottom+=drift;preview.current.barEvidence.bottom+=drift;}
                if(drift) {
                    const auto raw=ProveCurrentSubtitleBackingLip(frame.Source(),preview,cue.accepted,Identity(seq),7,8);
                    Assert::IsFalse(raw.valid && ApplyCurrentSubtitleBackingLip(geometry,raw,Identity(seq),7,8).generatedCleanup.Valid(),
                        L"Unbound measured-edge probing reproduces the logged cleanup dropout");
                }
                const auto proof=continuity.Consume(frame.Source(),preview,cue.accepted,Identity(seq),7,8,65536,&geometry);
                LogProof(proof,frame,"one-pixel-visible-edge");
                Assert::IsTrue(proof.valid);Assert::AreEqual(drift!=0,proof.compositionEdgeClipped);
                Assert::IsTrue(seq==2 || proof.revalidatedOutline);
                Assert::IsTrue(proof.samples<=65536 && Same(proof.cleanup,strip));
                const auto output=ApplyCurrentSubtitleBackingLip(geometry,proof,Identity(seq),7,8);
                Assert::IsTrue(Same(output.generatedCleanup,strip));AssertGeometryExceptCleanup(geometry,output);
                Assert::IsFalse(geometry.generatedCleanup.Valid(),L"Current cleanup cannot enter the layout cache");
            }
        }
    }
    TEST_METHOD(VisibleEdgeClippingStillRequiresExactCurrentBodyPixels) {
        for(bool top:{false,true})for(bool removed:{false,true}) {
            Frame frame(6,Format::P210,top);frame.Lip(30);auto cue=Acquire(frame);
            const auto geometry=Geometry(cue);SubtitleBackingLipContinuity continuity;
            Assert::IsTrue(continuity.Consume(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8,65536,&geometry).valid);
            if(removed)frame.Fill({220*6,frame.pictureBottom-30,420*6,frame.pictureBottom},300);
            else frame.Pixel(frame.width/2,frame.pictureBottom-10,700);
            auto preview=cue.preview;preview.current.identity=Identity(3);
            if(top) {--preview.current.pictureTop;--preview.current.barEvidence.top;}
            else {++preview.current.pictureBottom;++preview.current.barEvidence.bottom;}
            const auto rejected=continuity.Consume(frame.Source(),preview,cue.accepted,Identity(3),7,8,65536,&geometry);
            Assert::IsFalse(rejected.valid,L"One-pixel rebinding never holds stale or non-black cleanup");
            Assert::IsFalse(ApplyCurrentSubtitleBackingLip(geometry,rejected,Identity(3),7,8).generatedCleanup.Valid());
        }
    }
    TEST_METHOD(CurrentBackingUsesNativePixelsInsideBothWiderAndNarrowerDisplayWindows) {
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})for(bool top:{false,true}) {
            Frame frame(6,format,top);const auto strip=frame.Lip(30,true);const auto cue=Acquire(frame);
            const auto base=Geometry(cue);
            for(int delta:{-8,-2,0,2,8}) {
                auto geometry=base;geometry.pictureTop-=delta;geometry.pictureBottom+=delta;
                const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8,65536,nullptr,&geometry);
                LogProof(proof,frame,"current-visible-intersection");Assert::IsTrue(proof.valid);
                auto expected=strip;expected.top=(std::max)(strip.top,geometry.pictureTop);
                expected.bottom=(std::min)(strip.bottom,geometry.pictureBottom);
                Assert::IsTrue(Same(expected,proof.cleanup));
                const auto output=ApplyCurrentSubtitleBackingLip(geometry,proof,Identity(2),7,8);
                Assert::IsTrue(Same(expected,output.generatedCleanup));
                AssertGeometryExceptCleanup(geometry,output);
                for(int i=0;i<cue.accepted.lineCount;++i) {
                    const auto& glyph=cue.accepted.lineBounds[i];
                    Assert::IsTrue(top?expected.top>=glyph.bottom:expected.bottom<=glyph.top,
                        L"Glyph-free backing restoration cannot erase original subtitle glyphs");
                }
            }
        }
    }
    TEST_METHOD(OptionalNativePausedBackingCaptureCleansGlyphFreePictureIntrusion) {
        char* value=nullptr;size_t size=0;_dupenv_s(&value,&size,"VP_SUBTITLE_BACKING_REPLAY_PATH");
        const std::string path=value?value:"";std::free(value);
        if(path.empty()) {Logger::WriteMessage("Paused backing replay not requested");return;}
        std::ifstream data(path,std::ios::binary),metadata(path+".txt");
        Assert::IsTrue(bool(data) && bool(metadata),L"Explicit native replay requires its capture and metadata");
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(data)),{});
        AnalysisLumaSource source;int format=0,encoding=0,color=0,top=0,bottom=0,script=0,range=0;
        metadata>>source.width>>source.height>>source.rowBytes>>source.chromaRowBytes>>format>>encoding>>color>>top>>bottom>>script>>range;
        source.data=bytes.data();source.dataBytes=bytes.size();source.generation=2;
        source.format=static_cast<AnalysisLumaFormat>(format);source.encoding=static_cast<VideoFrameEncoding>(encoding);
        source.colorspace=static_cast<ColorSpace>(color);source.range=static_cast<AnalysisLumaRange>(range);
        Assert::IsTrue(source.IsValid());Assert::AreEqual(3840,source.width);Assert::AreEqual(2160,source.height);
        const auto measure=[&](uint64_t seq) {SubtitleBoxDetector scanner;
            return SubtitleBoxLookahead::Measure(scanner,source,ExtractActivePictureEvidence(source),Identity(seq),false,{},7,8);};
        const auto first=measure(1),second=measure(2);Acquired cue;
        Assert::IsTrue(first.text.detected && second.text.detected && second.independentBarAnalysis);
        cue.tracker.Consume(Resolve({first,second}));cue.preview=Resolve({second});cue.accepted=cue.tracker.Consume(cue.preview);
        Assert::IsTrue(cue.accepted.detected && !cue.accepted.held);
        auto geometry=Geometry(cue);geometry.pictureTop=382;geometry.pictureBottom=1778;
        const auto proof=ProveCurrentSubtitleBackingLip(source,cue.preview,cue.accepted,Identity(2),7,8,65536,nullptr,&geometry);
        std::ostringstream report;report<<"paused native backing valid="<<proof.valid<<" reason="<<proof.reason
            <<" native="<<second.pictureTop<<":"<<second.pictureBottom<<" composed=382:1778 cleanup="
            <<proof.cleanup.left<<","<<proof.cleanup.top<<"-"<<proof.cleanup.right<<","<<proof.cleanup.bottom;
        Logger::WriteMessage(report.str().c_str());Assert::IsTrue(proof.valid);
        const SubtitleBoxRect independentlyMeasured{1492,1746,2348,1776};
        Assert::IsTrue(Same(independentlyMeasured,proof.cleanup),L"Remove the complete rounded glyph-free backing measured from the paused native pixels");
        const auto output=ApplyCurrentSubtitleBackingLip(geometry,proof,Identity(2),7,8);
        Assert::IsTrue(Same(independentlyMeasured,output.generatedCleanup));AssertGeometryExceptCleanup(geometry,output);
        for(int i=0;i<cue.accepted.lineCount;++i)Assert::IsTrue(proof.cleanup.bottom<=cue.accepted.lineBounds[i].top);
    }
    TEST_METHOD(VisibleLipProofRejectsInvalidGeometryAndWrongContext) {
        for(bool top:{false,true}) {
            Frame frame(6,Format::P210,top);frame.Lip(30);const auto cue=Acquire(frame);
            const auto base=Geometry(cue);
            for(int fault=0;fault<10;++fault) {
                auto geometry=base;auto id=Identity(2);uint64_t policy=7,epoch=8;
                if(fault==0)geometry.valid=false;
                if(fault==1)geometry.fromTopBar=!geometry.fromTopBar;
                if(fault==2) {geometry.pictureTop=frame.height-1;geometry.pictureBottom=frame.height;}
                if(fault==3) {geometry.pictureTop=0;geometry.pictureBottom=1;}
                if(fault==4)geometry.pictureTop=-1;
                if(fault==5)geometry.pictureBottom=frame.height+1;
                if(fault==6)geometry.pictureBottom=geometry.pictureTop;
                if(fault==7)++id.acceptedSequence;
                if(fault==8)++policy;if(fault==9)++epoch;
                const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,id,policy,epoch,65536,nullptr,&geometry);
                Assert::IsFalse(proof.valid);
            }
        }
    }
    TEST_METHOD(LipApplyToleratesOnlyTheOppositePhysicalEdgeWhenCleanupFitsBothPictures) {
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})for(bool top:{false,true}) {
            Frame frame(6,format,top);const auto lip=frame.Lip(30);const auto cue=Acquire(frame);
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            Assert::IsTrue(proof.valid);const auto base=Geometry(cue);
            for(int delta:{-1,1}) {
                auto g=base;
                if(top)g.pictureBottom+=delta;else g.pictureTop+=delta;
                const SubtitleBoxRect visible{0,g.pictureTop,frame.width,g.pictureBottom};
                const SubtitleBoxRect measured{0,proof.pictureTop,frame.width,proof.pictureBottom};
                Assert::IsTrue(Contains(visible,proof.cleanup) && Contains(measured,proof.cleanup));
                const auto result=ApplyCurrentSubtitleBackingLip(g,proof,Identity(2),7,8);
                std::ostringstream diagnostic;diagnostic<<"opposite-edge top="<<top<<" delta="<<delta
                    <<" source="<<g.pictureTop<<":"<<g.pictureBottom<<" proof="<<proof.pictureTop<<":"<<proof.pictureBottom;
                Logger::WriteMessage(diagnostic.str().c_str());
                Assert::IsTrue(Contains(result.generatedCleanup,lip),
                    L"One-pixel opposite-edge jitter cannot discard an exact-current lip attached to the unchanged cue-side physical plane.");
                AssertGeometryExceptCleanup(g,result);
                Assert::IsFalse(base.generatedCleanup.Valid());
            }
        }
    }
    TEST_METHOD(LipApplyStillRequiresCurrentCoverageIdentityAndPolicy) {
        for(bool top:{false,true}) {
            Frame frame(6,Format::P210,top);frame.Lip(30);const auto cue=Acquire(frame);
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            Assert::IsTrue(proof.valid);const auto base=Geometry(cue);
            for(int fault=0;fault<9;++fault) {
                auto g=base;auto expected=Identity(2);uint64_t policy=7,continuity=8;
                // The unrelated edge mismatch is deliberately present in
                // every fault, so it cannot bypass the cue-side or epoch gates.
                if(top)--g.pictureBottom;else --g.pictureTop;
                if(fault==0) {if(top)++g.pictureTop;else --g.pictureBottom;}
                if(fault==1)g.pictureBottom=g.pictureTop;
                if(fault==2)++expected.acceptedSequence;
                if(fault==3)++expected.sourceFrameNumber;
                if(fault==4)++expected.captureTimestamp;
                if(fault==5)++expected.analysisDomainKey;
                if(fault==6)++policy;if(fault==7)++continuity;
                if(fault==8)g.fromTopBar=!g.fromTopBar;
                const auto result=ApplyCurrentSubtitleBackingLip(g,proof,expected,policy,continuity);
                std::ostringstream diagnostic;diagnostic<<"cue-side-binding top="<<top<<" fault="<<fault;
                Logger::WriteMessage(diagnostic.str().c_str());
                Assert::IsFalse(result.generatedCleanup.Valid());AssertGeometryExceptCleanup(g,result);
            }
        }
    }
    TEST_METHOD(LipApplyRejectsInvalidOrPartialPictureIntervalsAndPreservesExistingCleanup) {
        for(bool top:{false,true}) {
            Frame frame(6,Format::P210,top);frame.Lip(30);const auto cue=Acquire(frame);
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            Assert::IsTrue(proof.valid);const auto base=Geometry(cue);
            for(int fault=0;fault<9;++fault) {
                auto g=base;auto p=proof;
                if(fault==0) {if(top)g.pictureBottom=frame.height+1;else g.pictureTop=-1;}
                if(fault==1) {if(top)g.pictureBottom=g.pictureTop;else g.pictureTop=g.pictureBottom;}
                if(fault==2) {if(top)g.pictureBottom=g.pictureTop-1;else g.pictureTop=g.pictureBottom+1;}
                if(fault==3) {
                    // Valid intervals with a positive intersection are still
                    // insufficient when they clip even one proved cleanup row.
                    if(top)g.pictureBottom=p.cleanup.bottom-1;else g.pictureTop=p.cleanup.top+1;
                    Assert::IsTrue(g.pictureTop<g.pictureBottom);
                }
                if(fault==4)g.generatedCleanup=p.cleanup;
                if(fault==5)g.generatedCleanup={10,500,20,510};
                if(fault==6)g.cleanupLimit={0,384,frame.width,1776};
                if(fault==7) {
                    // A cleanup rectangle outside its independently measured
                    // picture is not rescued by a larger composition interval.
                    if(top) {
                        p.pictureBottom=p.cleanup.bottom-1;g.pictureBottom=1776;
                    } else {
                        p.pictureTop=p.cleanup.top+1;g.pictureTop=384;
                    }
                }
                if(fault==8)p.cleanup={};
                const auto result=ApplyCurrentSubtitleBackingLip(g,p,Identity(2),7,8);
                std::ostringstream diagnostic;diagnostic<<"cleanup-interval top="<<top<<" fault="<<fault;
                Logger::WriteMessage(diagnostic.str().c_str());
                Assert::IsTrue(Same(g.generatedCleanup,result.generatedCleanup));AssertGeometryExceptCleanup(g,result);
            }
        }
    }
    static void PaintAttachmentSceneContact(Frame& frame,int contact,bool right,int rows=1) {
        const int edge=(right?416:224)*frame.scale;
        for(int row=0;row<rows;++row) {
            const int y=frame.pictureBottom-1-row;
            frame.Fill(right?SubtitleBoxRect{edge,y,edge+contact,y+1}:
                SubtitleBoxRect{edge-contact,y,edge,y+1},frame.floor);
            // A separated scene pixel breaks the three-pixel exterior flank.
            // Only depth one may corroborate and clip this physical seam.
            frame.Pixel(right?edge+contact+1:edge-contact-2,y,frame.floor);
        }
    }
    TEST_METHOD(ClosureAllowsBoundedOneToThreeNativePixelSceneShadowsWithoutErasingThem) {
        for(int scale:{1,6})for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})
            for(bool top:{false,true})for(int darkPixels:{1,2,3}) {
                Frame frame(scale,format,top);const int depth=scale==1?4:30;
                const auto lip=frame.Lip(depth);const int closureY=frame.pictureBottom-depth-1;
                const int left=250*scale+1;
                frame.Fill({left,closureY,left+darkPixels,closureY+1},frame.floor);
                const auto shadow=frame.Rect({left,closureY,left+darkPixels,closureY+1});
                const auto cue=Acquire(frame);
                const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
                LogProof(proof,frame,"tiny-outside-closure-shadow");Assert::IsTrue(proof.valid);
                Assert::IsTrue(Same(lip,proof.cleanup));Assert::IsFalse(Contains(proof.cleanup,shadow));
                Assert::IsTrue(proof.closureDark==unsigned(darkPixels) && proof.closureLongestDarkRun==unsigned(darkPixels));
                const auto repaired=ApplyCurrentSubtitleBackingLip(Geometry(cue),proof,Identity(2),7,8);
                Assert::IsTrue(Same(lip,repaired.generatedCleanup));
                Assert::IsFalse(Contains(repaired.generatedCleanup,shadow),L"Outside scene detail can corroborate closure without becoming owned cleanup.");
            }
    }
    TEST_METHOD(UhdDistributedClosureShadowsAndOneRowSceneContactKeepTheExactFiniteLip) {
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})for(bool top:{false,true}) {
            Frame frame(6,format,top);const auto lip=frame.Lip(30);PaintAttachmentSceneContact(frame,16,false);
            const int left=1344+6,width=1152-12,y=frame.pictureBottom-31;
            unsigned totalDark=0;
            for(int bin=0;bin<8;++bin) {
                const int begin=left+width*bin/8,end=left+width*(bin+1)/8;
                const int count=bin<7?9:12,x=begin+(end-begin)/3;
                frame.Fill({x,y,x+count,y+1},frame.floor);totalDark+=count;
            }
            Assert::AreEqual(unsigned(75),totalDark);
            const auto cue=Acquire(frame);
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            LogProof(proof,frame,"6.6-percent-closure-plus-seam");Assert::IsTrue(proof.valid);
            Assert::IsTrue(proof.attachmentSeamClipped && Same(proof.measured,lip) && Same(proof.cleanup,lip));
            Assert::IsTrue(proof.closureSamples==1140 && proof.closureDark==75 && proof.closureLongestDarkRun==12);
            Assert::IsTrue(proof.cornerPixels==0,L"Unowned attachment contact must not be counted as an owned rounded corner.");
            const auto outside=frame.Rect({1328,frame.pictureBottom-1,1344,frame.pictureBottom});
            Assert::IsFalse(Contains(proof.cleanup,outside));
        }
    }
    TEST_METHOD(RoundedBackingAboveTextMayNarrowInsideLowerGlyphProjection) {
        for(bool top:{false,true}) {
            Frame frame(6,Format::P010,top);auto cue=Acquire(frame);
            int glyphRight=0;
            for(int i=0;i<cue.accepted.lineCount;++i)glyphRight=(std::max)(glyphRight,cue.accepted.lineBounds[i].right);
            const int right=glyphRight+12,edge=frame.pictureBottom-30;
            frame.Fill({1344,edge,right,frame.pictureBottom},frame.floor);
            // The top three rounded rows step inward, entirely ABOVE glyphs.
            for(int row=0;row<3;++row)
                frame.Fill({right-(3-row)*8,edge+row,right,edge+row+1},300);
            cue=Acquire(frame);
            const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            LogProof(proof,frame,"rounded-glyph-projection");Assert::IsTrue(proof.valid);
            Assert::AreEqual(right,proof.measured.right);
            Assert::AreEqual(size_t(48),proof.cornerPixels);
        }
    }
    TEST_METHOD(BoundedPictureCollarProvesBackingWithoutErasingAttachedSceneShadow) {
        for(bool top:{false,true})for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb}) {
            Frame frame(6,format,top);const auto lip=frame.Lip(30);
            const int edge=frame.pictureBottom-31;
            // Wider than the old 32-pixel run limit, and directly attached.
            frame.Fill({1500,edge-3,1636,edge+1},frame.floor);
            const auto cue=Acquire(frame);SubtitleBackingLipContinuity continuity;
            const auto proof=continuity.Consume(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            LogProof(proof,frame,"bounded-shadow-collar");Assert::IsTrue(proof.valid);
            Assert::IsTrue(Same(lip,proof.cleanup));
            const auto next=Resolve({frame.Measure(3)});const auto text=cue.accepted;
            const auto rechecked=continuity.Consume(frame.Source(),next,text,Identity(3),7,8);
            Assert::IsTrue(rechecked.valid && rechecked.revalidatedOutline);
            Assert::IsTrue(Same(lip,rechecked.cleanup));
            const auto applied=ApplyCurrentSubtitleBackingLip(Geometry(cue),proof,Identity(2),7,8);
            Assert::IsTrue(Same(lip,applied.generatedCleanup));
            // The collar remains outside removal, even when it contains black.
            const auto shadow=frame.Rect({1500,edge-3,1636,edge+1});
            Assert::IsFalse(Contains(applied.generatedCleanup,shadow));
            const auto capped=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8,proof.samples-1);
            Assert::IsFalse(capped.valid);Assert::IsTrue(capped.samples<=proof.samples-1);
        }
    }
    TEST_METHOD(ClosureRejectsLocalizedUnboundedOrExcessiveShadowsAndAFalseBodyClosure) {
        for(int scale:{1,6})for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})
            for(bool top:{false,true})for(int fault=0;fault<5;++fault) {
                Frame frame(scale,format,top);const int depth=scale==1?4:30;
                frame.Lip(depth);const int left=224*scale+6,width=192*scale-12,y=frame.pictureBottom-depth-1;
                const int binOne=left+width/8;
                if(fault==0) {
                    // Less than ten percent globally, but more than one third
                    // of a single bin; each separate run remains short.
                    const int runs=scale==1?3:6,length=scale==1?3:10,gap=scale==1?2:4;
                    for(int i=0;i<runs;++i) {
                        const int x=binOne+2+i*(length+gap);frame.Fill({x,y-32,x+length,y+1},frame.floor);
                    }
                }
                if(fault==1)frame.Fill({left,y-32,left+1,y+1},frame.floor); // Not bright-bounded at the closure edge.
                if(fault==2) {
                    const int length=scale==1?6:32;
                    frame.Fill({binOne+2,y-32,binOne+2+length,y+1},frame.floor);
                }
                if(fault==3)frame.Pixel(320*scale,frame.pictureBottom-(scale==1?2:16),300);
                if(fault==4)for(int bin=0;bin<8;++bin) {
                    const int begin=left+width*bin/8,end=left+width*(bin+1)/8;
                    const int length=scale==1?3:16,x=begin+(end-begin)/3;
                    frame.Fill({x,y-32,x+length,y+1},frame.floor);
                }
                const auto cue=Acquire(frame);
                const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
                std::ostringstream diagnostic;diagnostic<<"closure-negative scale="<<scale<<" top="<<top<<" fault="<<fault
                    <<" reason="<<proof.reason<<" dark="<<proof.closureDark<<" samples="<<proof.closureSamples;
                Logger::WriteMessage(diagnostic.str().c_str());Assert::IsFalse(proof.valid);
            }
    }
    TEST_METHOD(FirstAttachmentSeamUsesDepthOneBoundsAndNeverErasesItsSceneContact) {
        for(int scale:{1,6})for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})
            for(bool top:{false,true})for(bool right:{false,true}) {
                Frame frame(scale,format,top);const int depth=scale==1?4:30,contact=scale==1?2:16;
                const auto lip=frame.Lip(depth);PaintAttachmentSceneContact(frame,contact,right);
                const auto cue=Acquire(frame);
                const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
                LogProof(proof,frame,"bounded-first-native-seam");Assert::IsTrue(proof.valid);
                Assert::IsTrue(proof.attachmentSeamClipped && Same(proof.measured,lip) && Same(proof.cleanup,lip));
                Assert::IsTrue(Same(proof.blackInterior,lip) && proof.cornerPixels==0);
                const int edge=(right?416:224)*scale;
                const auto contactBounds=frame.Rect(right?
                    SubtitleBoxRect{edge,frame.pictureBottom-1,edge+contact,frame.pictureBottom}:
                    SubtitleBoxRect{edge-contact,frame.pictureBottom-1,edge,frame.pictureBottom});
                Assert::IsFalse(Contains(proof.cleanup,contactBounds));
                const auto base=Geometry(cue),result=ApplyCurrentSubtitleBackingLip(base,proof,Identity(2),7,8);
                AssertGeometryExceptCleanup(base,result);Assert::IsTrue(Same(result.generatedCleanup,lip));
                Assert::IsFalse(Contains(result.generatedCleanup,contactBounds));
            }
    }
    TEST_METHOD(AttachmentSeamRejectsLaterContactInsufficientBodyAndUnboundedOrDamagedRetainedPixels) {
        for(auto format:{Format::P010,Format::P210,Format::DecodedDv,Format::Rgb})for(bool top:{false,true})
            for(int fault=0;fault<6;++fault) {
                if(fault==5 && format==Format::Rgb)continue; // Planar UV-only contamination is tested independently of luma.
                Frame frame(6,format,top);frame.Lip(fault==1?2:30);
                PaintAttachmentSceneContact(frame,fault==2?32:16,false,fault==0?2:1);
                if(fault==3) {
                    // Rebuild a seam with no broad first-row run; a wider
                    // depth-one span cannot be claimed from the old row.
                    frame.Fill({1328,frame.pictureBottom-1,1344,frame.pictureBottom},300);
                    frame.Fill({1336,frame.pictureBottom-2,1344,frame.pictureBottom-1},frame.floor);
                    frame.Pixel(1342,frame.pictureBottom-1,frame.floor);
                }
                if(fault==4)frame.Pixel(1918,frame.pictureBottom-1,300);
                if(fault==5)frame.Chroma({1918,frame.pictureBottom-1,1920,frame.pictureBottom},420,610);
                const auto cue=Acquire(frame);
                const auto proof=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
                std::ostringstream diagnostic;diagnostic<<"seam-negative format="<<int(format)<<" top="<<top<<" fault="<<fault
                    <<" reason="<<proof.reason<<" samples="<<proof.samples;
                Logger::WriteMessage(diagnostic.str().c_str());Assert::IsFalse(proof.valid);
            }
    }
    TEST_METHOD(ClosureAndAttachmentRechecksRemainInsideTheExactCallerBudget) {
        for(bool top:{false,true}) {
            Frame frame(6,Format::DecodedDv,top);const auto lip=frame.Lip(30);PaintAttachmentSceneContact(frame,16,false);
            const int left=1350,width=1140,y=frame.pictureBottom-31;
            for(int bin=0;bin<8;++bin) {
                const int begin=left+width*bin/8,end=left+width*(bin+1)/8;
                const int length=bin<7?9:12,x=begin+(end-begin)/3;frame.Fill({x,y,x+length,y+1},frame.floor);
            }
            const auto cue=Acquire(frame);
            const auto full=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8);
            Assert::IsTrue(full.valid && Same(full.cleanup,lip) && full.attachmentSeamClipped);
            Assert::IsTrue(full.samples>32768 && full.samples<=65536);
            for(size_t cap:{size_t(1),size_t(1500),size_t(2500),full.samples-1}) {
                const auto limited=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8,cap);
                Assert::IsFalse(limited.valid);Assert::IsTrue(limited.samples<=cap);
                Assert::IsTrue(std::string(limited.reason)=="lip-work-budget");
            }
            const auto exact=ProveCurrentSubtitleBackingLip(frame.Source(),cue.preview,cue.accepted,Identity(2),7,8,full.samples);
            Assert::IsTrue(exact.valid && exact.samples==full.samples && Same(exact.cleanup,lip));
        }
    }
};
}
