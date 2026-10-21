#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleBoxLookahead.h>
#include <ActivePictureDecisionTimeline.h>
#include <SubtitleCutPaste.h>
#include <SubtitleMeasurementWorker.h>
#include <SubtitleCueContinuation.h>
#include <vprenderer/SubtitleAspectRetention.h>
#include <future>
#include <atomic>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>
#include <cstring>
#include <vector>
#include <string>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests {
TEST_CLASS(SubtitleBoxLookaheadTests) {
    static ActivePictureFrameIdentity Identity(uint64_t sequence) {
        return {1,sequence,sequence,sequence*1000,2,3,4};
    }
    static SubtitleBoxObservation Cue(uint64_t sequence, bool twoLines=false) {
        SubtitleBoxObservation o;
        o.identity=Identity(sequence); o.analyzed=o.barAuthority=true;
        o.width=640; o.height=360; o.pictureTop=45; o.pictureBottom=315;
        o.text.detected=true; o.text.lineCount=twoLines?2:1;
        o.text.bounds={196,314,444,twoLines?352:336};
        o.text.lineBounds[0]={200,318,440,332};
        o.text.lineSignatures[0].fill(0x55);
        if(twoLines) {
            o.text.lineBounds[1]={240,338,400,348};
            o.text.lineSignatures[1].fill(0x33);
        }
        // Handcrafted grouping fixtures represent two physically present rows,
        // with the optional companion sometimes missed by segmentation.
        auto ink=std::make_shared<SubtitleInkSnapshot>();
        ink->width=640;ink->height=360;ink->step=1;ink->sourceRows.resize(360);
        ink->rawInk.resize((640*360+63)/64);ink->ownedInk.resize(ink->rawInk.size());
        for(int y=0;y<360;++y)ink->sourceRows[y]=y;
        const SubtitleBoxRect rows[2]={{200,318,440,332},{240,338,400,348}};
        for(int line=0;line<2;++line)for(int y=rows[line].top;y<rows[line].bottom;++y)
            for(int x=rows[line].left;x<rows[line].right;++x)if((x-rows[line].left)%9<5) {
                const size_t pixel=size_t(y)*640+x;
                ink->rawInk[pixel/64]|=uint64_t{1}<<(pixel%64);
                if(line==0 || twoLines)ink->ownedInk[pixel/64]|=uint64_t{1}<<(pixel%64);
            }
        o.ink=ink;
        return o;
    }
    static SubtitleBoxObservation CompleteBackedCue(uint64_t sequence) {
        auto cue=Cue(sequence,true);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*cue.ink);
        ink->rawEvidenceComplete=true;
        ink->blackBacking.resize(ink->rawInk.size());
        for(size_t i=0;i<ink->rawInk.size();++i)ink->blackBacking[i]=~ink->rawInk[i];
        cue.ink=ink;return cue;
    }
    static SubtitleBoxObservation NearCardThreeRows(uint64_t sequence,bool top) {
        auto cue=Cue(sequence);cue.nearBarDistance=20;cue.text.lineCount=3;
        cue.text.nearBarEligibilityMeasured=cue.text.nearBarEligible=true;
        cue.text.capturePanelMeasured=true;
        auto ink=std::make_shared<SubtitleInkSnapshot>(*cue.ink);
        ink->rawEvidenceComplete=true;
        std::fill(ink->rawInk.begin(),ink->rawInk.end(),0);
        std::fill(ink->ownedInk.begin(),ink->ownedInk.end(),0);
        ink->blackBacking.assign(ink->rawInk.size(),~uint64_t(0));
        for(int line=0;line<3;++line) {
            const int y=top?50+line*16:264+line*16;
            const SubtitleBoxRect row{200,y,440,y+14};
            cue.text.lineBounds[line]=row;cue.text.lineSignatures[line].fill(0x55);
            for(int yy=row.top;yy<row.bottom;++yy)for(int x=row.left;x<row.right;++x)
                if((x-row.left)%9<5) {
                    const size_t b=size_t(yy)*640+x;const uint64_t bit=uint64_t(1)<<(b%64);
                    ink->rawInk[b/64]|=bit;ink->ownedInk[b/64]|=bit;ink->blackBacking[b/64]&=~bit;
                }
        }
        cue.text.bounds={196,cue.text.lineBounds[0].top-4,444,cue.text.lineBounds[2].bottom+4};
        cue.text.sourcePanel=cue.text.capturePanel=cue.text.bounds;
        for(int line=0;line<3;++line)
            cue.text.linePanels[line]=cue.text.lineCapturePanels[line]=cue.text.capturePanel;
        cue.ink=ink;return cue;
    }
    static SubtitleBoxObservation LowerOnlyCue(uint64_t sequence) {
        auto o=Cue(sequence,false);
        o.text.bounds={240,338,400,348};
        o.text.lineBounds[0]={240,338,400,348};
        o.text.lineSignatures[0].fill(0x33);
        return o;
    }
    static void AddNewGlyph(SubtitleInkSnapshot& ink,int left,int top,bool hShape=false) {
        for(int y=0;y<14;++y)for(int x=0;x<8;++x) {
            const bool pixel=hShape ? (x<2 || x>=6 || (y>=6 && y<=7)) :
                (x<2 || x>=6 || y<2 || y>=12);
            if(!pixel)continue;
            const size_t p=size_t(top+y)*ink.width+left+x;
            ink.rawInk[p/64]|=uint64_t{1}<<(p%64);
        }
    }
    static SubtitleBoxPreview Resolve(std::initializer_list<SubtitleBoxObservation> frames) {
        return SubtitleBoxLookahead::Resolve(frames.begin(),frames.size(),7,8);
    }
    static SubtitleBoxPreview ConfirmedPreview(const SubtitleBoxObservation& current) {
        auto b=current,c=current,d=current;
        b.identity=Identity(current.identity.acceptedSequence+1);
        c.identity=Identity(current.identity.acceptedSequence+2);
        d.identity=Identity(current.identity.acceptedSequence+3);
        return Resolve({current,b,c,d});
    }
    static void Fill(std::vector<uint16_t>& f,int left,int top,int right,int bottom,int code) {
        for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) f[y*640+x]=uint16_t(code<<6);
    }
    static void Text(std::vector<uint16_t>& f,int left,int top,int count,bool changed=false) {
        // Supported subtitles have opaque cards, including between words.
        Fill(f,(std::max)(0,left-4),(std::max)(0,top-4),
            (std::min)(640,left+13*(count-1)+12),(std::min)(360,top+18),64);
        for(int n=0;n<count;++n) {
            const int x=left+13*n;
            Fill(f,x-1,top-1,x+9,top+15,64);
            if(!changed) {
                Fill(f,x,top,x+8,top+14,700); Fill(f,x+2,top+2,x+6,top+12,64);
            } else {
                Fill(f,x,top,x+2,top+14,700); Fill(f,x+6,top,x+8,top+14,700);
                Fill(f,x+2,top+6,x+6,top+8,700);
            }
        }
    }
    static void ArabicTextLine(std::vector<uint16_t>& f,int left,int top,int width,int height) {
        Fill(f,left-8,top-4,left+width+8,top+height+4,64);
        const int joinY=top+height*2/3;
        Fill(f,left,joinY,left+width,joinY+3,700);
        for(int x=left+7;x<left+width-5;x+=20) {
            Fill(f,x,top+2,x+3,top+height-3,700);
            Fill(f,x+8,top+4,x+11,top+height-1,700);
        }
        Fill(f,left+2,top+height/2,left+5,top+height-2,700);
        Fill(f,left+width-5,top+3,left+width-2,top+height/2,700);
        for(int x=left+12;x<left+width-5;x+=31)Fill(f,x,top,x+2,top+2,700);
    }
    static void MirrorLumaRows(std::vector<uint16_t>& f) {
        const std::vector<uint16_t> original(f.begin(),f.begin()+640*360);
        for(int y=0;y<360;++y)std::copy_n(original.begin()+size_t(359-y)*640,640,f.begin()+size_t(y)*640);
    }
    static void LocalBacking(std::vector<uint16_t>& f,int plane) {
        Fill(f,140,(std::max)(0,plane-32),500,(std::min)(360,plane+32),64);
    }
    static std::vector<uint16_t> Pixels(bool full=false) {
        std::vector<uint16_t> f(640*360*3/2,uint16_t(512<<6));
        Fill(f,0,0,640,360,64); Fill(f,0,full?0:45,640,full?360:315,300); return f;
    }
    static std::vector<uint16_t> BarredPixels(int top,int bottom) {
        auto f=Pixels(true);
        Fill(f,0,0,640,top,64);Fill(f,0,bottom,640,360,64);
        return f;
    }
    static AnalysisLumaSource Source(std::vector<uint16_t>& pixels) {
        AnalysisLumaSource source;
        source.data=reinterpret_cast<const uint8_t*>(pixels.data());source.dataBytes=pixels.size()*2;
        source.width=640;source.height=360;source.rowBytes=source.chromaRowBytes=1280;
        source.format=AnalysisLumaFormat::P010;source.generation=2;
        return source;
    }
    static SubtitleBoxObservation Measure(std::vector<uint16_t>& pixels,uint64_t sequence) {
        const auto source=Source(pixels);
        SubtitleBoxDetector scanner;
        return SubtitleBoxLookahead::Measure(scanner,source,ExtractActivePictureEvidence(source),Identity(sequence),false);
    }
    static std::vector<uint16_t> WeakBottomPixels(bool glyphs=true) {
        auto pixels=Pixels();Fill(pixels,0,0,640,360,64);
        // Keep independently strong top geometry, but only sixteen scattered
        // bright columns reveal the established bottom edge in the dark scene.
        Fill(pixels,0,45,640,100,300);
        for(int i=4;i<128;i+=8) {
            const int x=(640*(4*128+96*(2*i+1)))/(200*128);
            Fill(pixels,x,300,x+1,315,90);
        }
        if(glyphs)Text(pixels,240,310,12);
        return pixels;
    }
    static SubtitleBoxObservation MeasureWithBar(std::vector<uint16_t>& pixels,uint64_t sequence,
        const SubtitleBarTrackingReference& reference,uint64_t policy=0,uint64_t continuity=0) {
        const auto source=Source(pixels);SubtitleBoxDetector scanner;
        return SubtitleBoxLookahead::Measure(scanner,source,ExtractActivePictureEvidence(source),
            Identity(sequence),false,reference,policy,continuity);
    }
    static AnalysisLumaSource RgbSource(std::vector<uint8_t>& pixels,int width,int height) {
        AnalysisLumaSource source;source.data=pixels.data();source.dataBytes=pixels.size();
        source.width=width;source.height=height;source.rowBytes=width*4;
        source.format=AnalysisLumaFormat::NativeRgb;source.encoding=VideoFrameEncoding::BGRA_8BIT;
        source.colorspace=ColorSpace::REC_709;source.generation=2;return source;
    }

    // Overload fallback keeps these regressions compilable and red before the
    // production queue bridge is present. It is test-only compatibility.
    template<class Guard>
    static auto ResolvePendingQueue(Guard& guard,SubtitleBoxPreview& preview,
        const AnalysisLumaSource& source,double frameMs,int holdMs,
        const ActivePictureFrameIdentity* identity,bool queueOnly,int)
        -> decltype(guard.Resolve(preview,source,frameMs,holdMs,identity,queueOnly)) {
        return guard.Resolve(preview,source,frameMs,holdMs,identity,queueOnly);
    }
    template<class Guard>
    static bool ResolvePendingQueue(Guard& guard,SubtitleBoxPreview& preview,
        const AnalysisLumaSource& source,double frameMs,int holdMs,
        const ActivePictureFrameIdentity*,bool,long) {
        return guard.Resolve(preview,source,frameMs,holdMs);
    }

    template<class Presentation>
    static auto ConsumePendingQueue(Presentation& presentation,const SubtitleBoxPreview& preview,
        int holdMs,double frameMs,const ActivePictureFrameIdentity* identity,
        bool queueOnly,bool currentPixelsVerified,int)
        -> decltype(presentation.Consume(preview,holdMs,frameMs,identity,queueOnly,currentPixelsVerified)) {
        return presentation.Consume(preview,holdMs,frameMs,identity,queueOnly,currentPixelsVerified);
    }
    template<class Presentation>
    static SubtitleBoxResult ConsumePendingQueue(Presentation& presentation,const SubtitleBoxPreview& preview,
        int holdMs,double frameMs,const ActivePictureFrameIdentity* identity,bool queueOnly,bool,long) {
        return presentation.Consume(preview,holdMs,frameMs,identity,queueOnly);
    }
    template<class Reason>
    static auto QueueCapacityReason(int) -> decltype(Reason::QueueCapacityOverflow) {
        return Reason::QueueCapacityOverflow;
    }
    template<class Reason>
    static Reason QueueCapacityReason(long) { return Reason::Unknown; }
    struct ContinuationFixture {
        std::vector<uint16_t> pixels;
        SubtitleBoxPreview acquired;
        ActivePictureBounds base{0,45,640,315,640,360};
        bool p210;
        explicit ContinuationFixture(bool planar422=false):pixels(Pixels()),p210(planar422) {
            if(p210)pixels.resize(640*360*2,uint16_t(512<<6));
            Text(pixels,240,310,12);
            auto& o=acquired.current;
            o.identity=Identity(1);o.identity.transportGeneration=2;
            o.width=640;o.height=360;o.pictureTop=45;o.pictureBottom=315;
            o.analyzed=o.barAuthority=true;
            o.policyGeneration=acquired.policyGeneration=7;
            o.continuityGeneration=acquired.continuityGeneration=9;
            o.text.detected=true;o.text.lineCount=1;o.text.cue=42;
            o.text.nearBarEligibilityMeasured=o.text.nearBarEligible=true;
            o.text.bounds={236,306,395,328};o.text.lineBounds[0]={240,310,391,324};
            o.text.lineSignatures[0].fill(0x55);
            o.text.sourcePanel=o.text.capturePanel=o.text.bounds;
            o.sharedPicture.required=true;o.sharedPicture.identity=o.identity;o.sharedPicture.bounds=base;
            auto ink=std::make_shared<SubtitleInkSnapshot>();
            ink->width=640;ink->height=360;ink->step=1;ink->sourceRows.resize(360);
            ink->rawInk.resize((640*360+63)/64);ink->ownedInk.resize(ink->rawInk.size());
            ink->blackBacking.resize(ink->rawInk.size());
            ink->rawEvidenceComplete=ink->paletteValid=true;ink->inkFloor=500;ink->blackLimit=96;
            ink->inkU=ink->inkV=512;
            for(int y=0;y<360;++y) {ink->sourceRows[y]=y;for(int x=0;x<640;++x) {
                const size_t i=size_t(y)*640+x;const uint64_t bit=uint64_t(1)<<(i%64);
                const int value=pixels[i]>>6;
                if(value>=500) {ink->rawInk[i/64]|=bit;ink->ownedInk[i/64]|=bit;}
                if(value<=96)ink->blackBacking[i/64]|=bit;
            }}
            o.ink=ink;acquired.text=o.text;acquired.available=acquired.currentLinesConfirmed=true;
            acquired.matchingFrames=3;
        }
        AnalysisLumaSource Native() {
            auto source=Source(pixels);if(p210)source.format=AnalysisLumaFormat::P210;return source;
        }
        SubtitleBoxPreview Missing(uint64_t sequence) const {
            auto p=acquired;p.current.identity=Identity(sequence);p.current.identity.transportGeneration=2;
            p.current.sharedPicture.identity=p.current.identity;
            p.current.pictureBottom=360;p.current.ink.reset();p.current.text={};p.text={};
            p.current.text.diagnosticReason="no-bright-ink";
            p.currentLinesConfirmed=false;p.matchingFrames=0;return p;
        }
    };
public:
    TEST_METHOD(PictureGlyphConfirmationFollowsIndependentInputAnalysisCadence) {
        auto first=CompleteBackedCue(1),next=CompleteBackedCue(3);
        first.text.diagnosticPictureGlyphProofRequired=true;
        next.text.diagnosticPictureGlyphProofRequired=true;
        first.analysisStride=next.analysisStride=2;
        Assert::IsTrue(SubtitleBoxLookahead::ConfirmPictureGlyphAcquisitionPair(first,next,true).confirmed);
        Assert::IsTrue(SubtitleBoxLookahead::ConfirmPictureGlyphAcquisitionPair(next,first,false).confirmed);
        next.analysisStride=1;
        Assert::IsFalse(SubtitleBoxLookahead::ConfirmPictureGlyphAcquisitionPair(first,next,true).confirmed);
        next.analysisStride=2;next.identity=Identity(5);
        Assert::IsFalse(SubtitleBoxLookahead::ConfirmPictureGlyphAcquisitionPair(first,next,true).confirmed);
        next.identity=Identity(3);next.analyzed=false;next.cadenceSkipped=true;
        Assert::IsFalse(SubtitleBoxLookahead::ConfirmPictureGlyphAcquisitionPair(first,next,true).confirmed);
        first.analysisStride=next.analysisStride=1;next=CompleteBackedCue(2);
        Assert::IsTrue(SubtitleBoxLookahead::ConfirmPictureGlyphAcquisitionPair(first,next,true).confirmed);
    }
    TEST_METHOD(AlternateInputCadenceCountsOnlyIndependentMeasuredFrames) {
        std::array<SubtitleBoxObservation,5> frames;
        std::array<ActivePictureFrameIdentity,5> owned;
        for(size_t i=0;i<frames.size();++i) {
            frames[i]=Cue(i+1);frames[i].analysisStride=2;
            frames[i].policyGeneration=7;frames[i].continuityGeneration=8;
            owned[i]=frames[i].identity;
            if(i%2) {frames[i].analyzed=false;frames[i].cadenceSkipped=true;}
        }
        const auto preview=SubtitleBoxLookahead::ResolveOwnedWindow(frames.data(),owned.data(),frames.size(),7,8);
        Assert::IsTrue(preview.available && preview.futureAvailable);
        Assert::AreEqual(3u,preview.matchingFrames);
        Assert::AreEqual(2u,preview.followingCount);
        Assert::AreEqual(uint64_t(3),preview.following[0].identity.acceptedSequence);
        Assert::AreEqual(uint64_t(5),preview.following[1].identity.acceptedSequence);
        frames[1].cadenceSkipped=false;
        Assert::AreEqual(1u,SubtitleBoxLookahead::ResolveOwnedWindow(frames.data(),owned.data(),frames.size(),7,8).matchingFrames);
        frames[1].cadenceSkipped=true;frames[1].discontinuity=true;
        Assert::AreEqual(1u,SubtitleBoxLookahead::ResolveOwnedWindow(frames.data(),owned.data(),frames.size(),7,8).matchingFrames);
        frames[1].discontinuity=false;frames[2].analyzed=false;
        Assert::AreEqual(1u,SubtitleBoxLookahead::ResolveOwnedWindow(frames.data(),owned.data(),frames.size(),7,8).matchingFrames);
    }

    TEST_METHOD(ContinuationRevalidatesNativeCueWithoutChangingMissingEdgeObservation) {
        for(bool p210:{false,true}) {
            ContinuationFixture f(p210);SubtitleCueContinuationGuard guard;
            Assert::IsTrue(guard.Remember(f.acquired,f.acquired.text,f.base,f.Native()));
            auto current=f.Missing(2);const auto raw=current;
            const auto proof=guard.Prepare(current,f.Native(),f.base,250,40);
            Assert::IsTrue(proof.valid);Assert::IsTrue(proof.accepted.detected&&proof.accepted.held);
            Assert::AreEqual(f.acquired.text.cue,proof.accepted.cue);
            Assert::AreEqual(315,proof.pictureBottom);Assert::AreEqual(45,proof.pictureTop);
            Assert::IsTrue(SameAnalysisFrameIdentity(proof.identity,current.current.identity));
            Assert::IsTrue(proof.ink!=nullptr);Assert::IsTrue(proof.samples>0&&proof.samples<=65536);
            Assert::AreEqual(360,current.current.pictureBottom);Assert::IsTrue(current.current.ink==nullptr);
            Assert::IsFalse(current.current.text.detected);Assert::IsFalse(current.text.detected);
            Assert::AreEqual(raw.current.barAuthority,current.current.barAuthority);
            Assert::AreEqual(raw.current.sharedPicture.bounds.bottom,current.current.sharedPicture.bounds.bottom);
            Assert::AreEqual(f.base.bottom,proof.physicalBase.bottom);
        }
    }
    TEST_METHOD(ContinuationRevalidatesMirroredMissingTopEdge) {
        ContinuationFixture f;
        // Mirror the source cue into the top letterbox and update the accepted
        // current-frame measurements while preserving the same physical base.
        const auto original=f.pixels;
        for(int y=0;y<360;++y)for(int x=0;x<640;++x)
            f.pixels[size_t(y)*640+x]=original[size_t(359-y)*640+x];
        auto& observation=f.acquired.current;
        observation.text.bounds={236,32,395,54};
        observation.text.lineBounds[0]={240,36,391,50};
        observation.text.sourcePanel=observation.text.capturePanel=observation.text.bounds;
        f.acquired.text=observation.text;
        auto ink=std::make_shared<SubtitleInkSnapshot>(*observation.ink);
        std::fill(ink->rawInk.begin(),ink->rawInk.end(),0);
        std::fill(ink->ownedInk.begin(),ink->ownedInk.end(),0);
        std::fill(ink->blackBacking.begin(),ink->blackBacking.end(),0);
        for(int y=0;y<360;++y)for(int x=0;x<640;++x) {
            const size_t i=size_t(y)*640+x;const uint64_t bit=uint64_t(1)<<(i%64);
            const int value=f.pixels[i]>>6;
            if(value>=ink->inkFloor) {ink->rawInk[i/64]|=bit;ink->ownedInk[i/64]|=bit;}
            if(value<=ink->blackLimit)ink->blackBacking[i/64]|=bit;
        }
        observation.ink=ink;
        SubtitleCueContinuationGuard guard;
        Assert::IsTrue(guard.Remember(f.acquired,f.acquired.text,f.base,f.Native()));
        auto missing=f.acquired;
        missing.current.identity=Identity(2);missing.current.identity.transportGeneration=2;
        missing.current.sharedPicture.identity=missing.current.identity;
        missing.current.pictureTop=0;missing.current.text={};missing.current.text.diagnosticReason="no-bright-ink";
        missing.current.ink.reset();missing.text={};missing.currentLinesConfirmed=false;missing.matchingFrames=0;
        const auto proof=guard.Prepare(missing,f.Native(),f.base,250,40);
        Assert::IsTrue(proof.valid);Assert::AreEqual(45,proof.pictureTop);Assert::AreEqual(315,proof.pictureBottom);
        Assert::AreEqual(f.acquired.text.cue,proof.accepted.cue);
        Assert::AreEqual(0,missing.current.pictureTop);Assert::IsFalse(missing.current.text.detected);
        Assert::IsTrue(missing.current.ink==nullptr);
    }
    TEST_METHOD(PresentationConsumeKeepsCueAndGeometryAcrossMissingEdgeAndRecovery) {
        ContinuationFixture f;SubtitleCueContinuationGuard guard;
        SubtitleBoxPresentation presentation;SubtitleCutPastePresentation compositor;
        const auto accepted=presentation.Consume(f.acquired);
        Assert::IsTrue(accepted.detected);Assert::IsFalse(accepted.held);
        Assert::IsTrue(guard.Remember(f.acquired,accepted,f.base,f.Native()));
        const auto initialGeometry=compositor.Consume(accepted,f.acquired);
        Assert::IsTrue(initialGeometry.valid);

        auto missing=f.Missing(2);const auto rawMissing=missing;
        const auto proof=guard.Prepare(missing,f.Native(),f.base,250,40);
        Assert::IsTrue(proof.valid);
        const auto continued=presentation.Consume(missing,250,40,nullptr,false,false,&proof);
        Assert::IsTrue(continued.detected);Assert::IsTrue(continued.held);
        Assert::AreEqual(accepted.cue,continued.cue);
        Assert::AreEqual(std::string("current-cue-missing-edge"),std::string(presentation.DecisionReason()));
        const auto continuedGeometry=compositor.Consume(continued,missing,{},0,nullptr,&proof);
        Assert::IsTrue(continuedGeometry.valid);
        Assert::IsTrue(SubtitleCueContinuation::SameRect(initialGeometry.source,continuedGeometry.source));
        Assert::IsTrue(SubtitleCueContinuation::SameRect(initialGeometry.destination,continuedGeometry.destination));
        Assert::IsTrue(SubtitleCueContinuation::SameRect(initialGeometry.generatedCleanup,continuedGeometry.generatedCleanup));
        Assert::AreEqual(360,missing.current.pictureBottom);Assert::IsTrue(missing.current.ink==nullptr);
        Assert::IsFalse(missing.current.text.detected);Assert::IsFalse(missing.text.detected);
        Assert::AreEqual(rawMissing.current.pictureBottom,missing.current.pictureBottom);
        Assert::IsTrue(rawMissing.current.ink==missing.current.ink);

        auto recovered=f.acquired;recovered.current.identity=Identity(3);
        recovered.current.identity.transportGeneration=2;recovered.current.sharedPicture.identity=recovered.current.identity;
        const auto normal=presentation.Consume(recovered);
        Assert::IsTrue(normal.detected);Assert::IsFalse(normal.held);
        Assert::AreEqual(accepted.cue,normal.cue);
        Assert::AreEqual(std::string("detected"),std::string(presentation.DecisionReason()));
        const auto recoveredGeometry=compositor.Consume(normal,recovered);
        Assert::IsTrue(recoveredGeometry.valid);
        Assert::IsTrue(SubtitleCueContinuation::SameRect(initialGeometry.source,recoveredGeometry.source));
        Assert::IsTrue(SubtitleCueContinuation::SameRect(initialGeometry.destination,recoveredGeometry.destination));
        Assert::IsTrue(SubtitleCueContinuation::SameRect(initialGeometry.generatedCleanup,recoveredGeometry.generatedCleanup));
    }
    TEST_METHOD(PresentationConsumeRejectsStaleOrMismatchedMissingEdgeProof) {
        for(int mismatch=0;mismatch<2;++mismatch) {
            ContinuationFixture f;SubtitleCueContinuationGuard guard;SubtitleBoxPresentation presentation;
            const auto accepted=presentation.Consume(f.acquired);
            Assert::IsTrue(accepted.detected);Assert::IsTrue(guard.Remember(f.acquired,accepted,f.base,f.Native()));
            auto missing=f.Missing(2);
            auto stale=guard.Prepare(missing,f.Native(),f.base,250,40);
            Assert::IsTrue(stale.valid);
            if(mismatch==0)--stale.identity.acceptedSequence;
            else ++stale.policyGeneration;
            const auto result=presentation.Consume(missing,250,40,nullptr,false,false,&stale);
            Assert::IsFalse(result.detected,
                L"A proof with another source identity or policy cannot preserve a missing-edge cue");
            Assert::IsFalse(missing.current.text.detected);Assert::IsTrue(missing.current.ink==nullptr);
            Assert::IsFalse(std::strcmp(presentation.DecisionReason(),"current-cue-missing-edge")==0);
        }
    }
    TEST_METHOD(ContinuationCompositorKeepsExactPreparedPlacementAndRejectsDifferentContract) {
        for(bool p210:{false,true})for(int invalid=0;invalid<7;++invalid) {
            ContinuationFixture f(p210);SubtitleCueContinuationGuard guard;
            Assert::IsTrue(guard.Remember(f.acquired,f.acquired.text,f.base,f.Native()));
            SubtitleCutPastePresentation compositor;
            const auto first=compositor.Consume(f.acquired.text,f.acquired);
            Assert::IsTrue(first.valid);
            auto current=f.Missing(2);
            auto proof=guard.Prepare(current,f.Native(),f.base,250,40);
            Assert::IsTrue(proof.valid);
            auto accepted=proof.accepted;SubtitleBoxPadding padding;
            if(invalid==1)--proof.identity.acceptedSequence;
            if(invalid==2)proof.physicalBase.bottom+=1;
            if(invalid==3)++proof.policyGeneration;
            if(invalid==4)proof.sampledBounds.bottom=310;
            if(invalid==5)accepted.cue+=1;
            if(invalid==6)proof.backingVerifiedBounds.bottom=310;
            const auto next=compositor.Consume(accepted,current,padding,0,nullptr,&proof);
            if(invalid)Assert::IsFalse(next.valid);
            else {
                Assert::IsTrue(next.valid);
                Assert::IsTrue(SubtitleCueContinuation::SameRect(first.source,next.source));
                Assert::IsTrue(SubtitleCueContinuation::SameRect(first.destination,next.destination));
                Assert::IsTrue(SubtitleCueContinuation::SameRect(first.generatedCleanup,next.generatedCleanup));
                Assert::AreEqual(315,next.pictureBottom);Assert::AreEqual(45,next.pictureTop);
                Assert::IsTrue(proof.ink!=f.acquired.current.ink,L"Composition uses fresh proof, never remembered pixels.");
            }
            Assert::AreEqual(360,current.current.pictureBottom);
            Assert::IsFalse(current.current.text.detected);Assert::IsTrue(current.current.ink==nullptr);
        }
    }
    TEST_METHOD(ContinuationVerifiedRefreshAdvancesCursorWithoutRenewingStartedLease) {
        for(bool p210:{false,true}) {
            ContinuationFixture f(p210);SubtitleCueContinuationGuard guard;
            Assert::IsTrue(guard.Remember(f.acquired,f.acquired.text,f.base,f.Native()));
            auto refresh=f.acquired;refresh.current.analysisRefresh=true;
            for(uint64_t sequence=2;sequence<=10;++sequence) {
                refresh.current.identity=Identity(sequence);refresh.current.identity.transportGeneration=2;
                refresh.current.sharedPicture.identity=refresh.current.identity;
                Assert::IsTrue(guard.AdvanceVerified(refresh,f.acquired.text,f.Native(),f.base,1000,50));
            }
            const auto first=guard.Prepare(f.Missing(11),f.Native(),f.base,1000,50);
            Assert::IsTrue(first.valid);Assert::AreEqual(50.0,first.elapsedMs);
            refresh.current.identity=Identity(12);refresh.current.identity.transportGeneration=2;
            refresh.current.sharedPicture.identity=refresh.current.identity;
            Assert::IsTrue(guard.AdvanceVerified(refresh,f.acquired.text,f.Native(),f.base,1000,50));
            for(uint64_t sequence=13;sequence<=15;++sequence) {
                const auto proof=guard.Prepare(f.Missing(sequence),f.Native(),f.base,1000,50);
                Assert::IsTrue(proof.valid);
                Assert::AreEqual(double((sequence-10)*50),proof.elapsedMs);
                Assert::AreEqual(sequence-1,proof.previousIdentity.acceptedSequence);
                Assert::AreEqual(uint64_t(1),proof.templateIdentity.acceptedSequence);
            }
            Assert::IsFalse(guard.Prepare(f.Missing(16),f.Native(),f.base,1000,50).valid,
                L"A verified refresh after the first missing edge cannot renew the250ms lease.");
        }
    }
    TEST_METHOD(ContinuationPaddingPreservesPreparedSafeLayoutOnMissingEdge) {
        ContinuationFixture f;SubtitleCueContinuationGuard guard;
        Assert::IsTrue(guard.Remember(f.acquired,f.acquired.text,f.base,f.Native()));
        SubtitlePaddingGuard padding;SubtitlePaddingPolicy policy;policy.glyphRelative=true;
        const SubtitleBoxPadding requested{75,35,25};
        const auto original=padding.Consume(f.acquired.text,f.acquired,requested,policy);
        const auto bounds=padding.DisplayBounds();const int height=padding.GlyphHeight();
        auto current=f.Missing(2);const auto proof=guard.Prepare(current,f.Native(),f.base,250,40);
        Assert::IsTrue(proof.valid);
        const auto retained=padding.Consume(proof.accepted,current,requested,policy,&proof);
        Assert::IsTrue(retained==original);
        Assert::IsTrue(SubtitleCueContinuation::SameRect(bounds,padding.DisplayBounds()));
        Assert::AreEqual(height,padding.GlyphHeight());Assert::AreEqual(360,current.current.pictureBottom);
    }
    TEST_METHOD(ContinuationCannotBorrowDisappearedChangedOrAdditionalGlyphs) {
        for(bool p210:{false,true})for(int change=0;change<4;++change) {
            ContinuationFixture f(p210);SubtitleCueContinuationGuard guard;
            Assert::IsTrue(guard.Remember(f.acquired,f.acquired.text,f.base,f.Native()));
            if(change==0)Fill(f.pixels,236,306,395,328,64);
            if(change==1)Text(f.pixels,240,310,1,true);
            if(change==2)Text(f.pixels,240,338,8);
            if(change==3)Fill(f.pixels,0,315,640,335,300);
            const auto proof=guard.Prepare(f.Missing(2),f.Native(),f.base,250,40);
            Assert::IsFalse(proof.valid,L"Current native disagreement must never emit old subtitle pixels.");
        }
    }
    TEST_METHOD(ContinuationRejectsChangedContextSourceAndPhysicalBase) {
        for(int change=0;change<14;++change) {
            ContinuationFixture f;SubtitleCueContinuationGuard guard;
            Assert::IsTrue(guard.Remember(f.acquired,f.acquired.text,f.base,f.Native()));
            auto current=f.Missing(2);auto source=f.Native();auto base=f.base;
            if(change==0)++current.current.identity.transportGeneration;
            if(change==1)++current.current.identity.sourceFormatGeneration;
            if(change==2)++current.current.identity.analysisDomainKey;
            if(change==3)++current.current.identity.viewportGeneration;
            if(change==4)++current.current.identity.rendererGeneration;
            if(change==5)++current.policyGeneration;
            if(change==6)++current.continuityGeneration;
            if(change==7)current.current.discontinuity=true;
            if(change==8)++source.generation;
            if(change==9)base.bottom+=10;
            if(change==10)current.current.pictureTop=0;
            if(change==11)++current.current.optimizationMode;
            if(change==12)current.current.scriptHint=SubtitleScriptHint::Arabic;
            if(change==13)current.current.identity=Identity(0);
            Assert::IsFalse(guard.Prepare(current,source,base,250,40).valid);
        }
    }
    TEST_METHOD(ContinuationProofDoesNotRenewLeaseAndRepeatedOutputDoesNotConsumeIt) {
        ContinuationFixture f;SubtitleCueContinuationGuard guard;
        Assert::IsTrue(guard.Remember(f.acquired,f.acquired.text,f.base,f.Native()));
        for(uint64_t sequence=2;sequence<=6;++sequence) {
            const auto current=f.Missing(sequence);
            const auto proof=guard.Prepare(current,f.Native(),f.base,250,50);
            Assert::IsTrue(proof.valid);
            for(int repeat=0;repeat<3;++repeat) {
                const auto again=guard.Prepare(current,f.Native(),f.base,250,50);
                Assert::IsTrue(again.valid);Assert::AreEqual(proof.elapsedMs,again.elapsedMs);
            }
        }
        Assert::IsFalse(guard.Prepare(f.Missing(7),f.Native(),f.base,250,50).valid);
    }
    TEST_METHOD(ContinuationRejectsReadFailureInsufficientBudgetAndAbsentSeed) {
        for(int failure=0;failure<4;++failure) {
            ContinuationFixture f;SubtitleCueContinuationGuard guard;
            if(failure!=0)Assert::IsTrue(guard.Remember(f.acquired,f.acquired.text,f.base,f.Native()));
            auto source=f.Native();size_t budget=65536;
            if(failure==1)source.dataBytes=1;
            if(failure==2)budget=1;
            if(failure==3)guard.Reset();
            const auto proof=guard.Prepare(f.Missing(2),source,f.base,250,40,budget);
            Assert::IsFalse(proof.valid);Assert::IsTrue(proof.samples<=budget);
        }
    }
    TEST_METHOD(CurrentReferencePixelsPreserveThreeLinesThroughPartialGroupingAndCapture) {
        for(bool top : {false,true}) {
            auto acquired=NearCardThreeRows(1,top);
            auto palette=std::make_shared<SubtitleInkSnapshot>(*acquired.ink);
            palette->paletteValid=true;palette->inkFloor=375;palette->blackLimit=80;
            acquired.ink=palette;
            auto pixels=Pixels();Fill(pixels,0,0,640,360,64);
            for(int y=0;y<360;++y)for(int x=0;x<640;++x)
                if(palette->Get(x,y))pixels[y*640+x]=uint16_t(600<<6);
            SubtitleBoxPresentation tracker;SubtitleCutPastePresentation compositor;
            auto initial=ConfirmedPreview(acquired);auto accepted=tracker.Consume(initial);
            Assert::IsTrue(accepted.detected);const auto cue=accepted.cue;
            const auto geometry=compositor.Consume(accepted,initial);
            Assert::IsTrue(geometry.valid);
            for(uint64_t sequence=2;sequence<=16;++sequence) {
                auto current=acquired;current.identity=Identity(sequence);
                auto ink=std::make_shared<SubtitleInkSnapshot>(*palette);
                const int missing=top?2:0;
                const auto lost=acquired.text.lineBounds[missing];
                // This is unknown/unsampled analysis coverage, not absent pixels.
                for(int y=lost.top-1;y<=lost.bottom;++y)for(int x=lost.left-1;x<=lost.right;++x) {
                    const size_t i=size_t(y)*640+x;const uint64_t bit=uint64_t(1)<<(i%64);
                    ink->rawInk[i/64]&=~bit;ink->ownedInk[i/64]&=~bit;ink->blackBacking[i/64]&=~bit;
                }
                current.ink=ink;
                if(sequence%2==0) {
                    current.text.lineCount=2;
                    if(!top)for(int i=0;i<2;++i)current.text.lineBounds[i]=acquired.text.lineBounds[i+1];
                    current.text.bounds.top=current.text.lineBounds[0].top;
                    current.text.bounds.bottom=current.text.lineBounds[1].bottom;
                }
                // Alternate lost grouping and same-count but narrowed copy mask.
                if(top)current.text.capturePanel.bottom=lost.top;
                else current.text.capturePanel.top=lost.bottom;
                auto preview=Resolve({current});
                Assert::IsTrue(tracker.RefreshReferencePixels(preview,Source(pixels)));
                Assert::IsTrue(tracker.ReferenceSampleCount()>0 && tracker.ReferenceSampleCount()<=65536);
                const auto result=tracker.Consume(preview);
                Assert::IsTrue(result.detected);Assert::AreEqual(cue,result.cue);
                Assert::AreEqual(3,result.lineCount);
                const auto move=compositor.Consume(result,preview);
                Assert::IsTrue(move.valid);
                Assert::AreEqual(geometry.pictureCapture.top,move.pictureCapture.top);
                Assert::AreEqual(geometry.pictureCapture.bottom,move.pictureCapture.bottom);
                for(int i=0;i<3;++i) {
                    Assert::AreEqual(acquired.text.lineBounds[i].top,result.lineBounds[i].top);
                    Assert::AreEqual(acquired.text.lineBounds[i].bottom,result.lineBounds[i].bottom);
                }
            }
            auto changed=acquired;changed.identity=Identity(17);
            const auto lost=acquired.text.lineBounds[top?2:0];
            Fill(pixels,lost.left,lost.top,lost.right,lost.bottom,64);
            changed.text.detected=false;changed.text.lineCount=0;changed.text.bounds={};
            auto preview=Resolve({changed});
            Assert::IsTrue(tracker.RefreshReferencePixels(preview,Source(pixels)));
            const auto gone=tracker.Consume(preview);
            Assert::IsFalse(gone.detected,L"A genuinely removed line cannot borrow remembered pixels");
        }
    }
    TEST_METHOD(CurrentReferenceSamplingRejectsDiscontinuityAndChangedContext) {
        for(int change=0;change<4;++change) {
            auto acquired=NearCardThreeRows(1,false);
            auto ink=std::make_shared<SubtitleInkSnapshot>(*acquired.ink);
            ink->paletteValid=true;ink->inkFloor=375;ink->blackLimit=80;acquired.ink=ink;
            SubtitleBoxPresentation tracker;Assert::IsTrue(tracker.Consume(ConfirmedPreview(acquired)).detected);
            auto next=acquired;next.identity=Identity(2);
            if(change==0)next.discontinuity=true;
            if(change==1)next.identity.sourceFormatGeneration++;
            if(change==2)next.barAuthority=false;
            if(change==3)next.identity.acceptedSequence++;
            auto preview=Resolve({next});auto pixels=Pixels();
            const auto before=preview.current.ink;
            Assert::IsFalse(tracker.RefreshReferencePixels(preview,Source(pixels)));
            Assert::IsTrue(preview.current.ink==before);
        }
    }
    TEST_METHOD(CurrentReferenceRefreshCannotPoisonReplacementPalette) {
        auto acquired=NearCardThreeRows(1,false);
        auto oldInk=std::make_shared<SubtitleInkSnapshot>(*acquired.ink);
        oldInk->paletteValid=true;oldInk->inkFloor=700;oldInk->blackLimit=80;
        acquired.ink=oldInk;
        SubtitleBoxPresentation tracker;
        const auto old=tracker.Consume(ConfirmedPreview(acquired));
        auto replacement=NearCardThreeRows(2,false);
        auto newInk=std::make_shared<SubtitleInkSnapshot>(*replacement.ink);
        newInk->paletteValid=true;newInk->inkFloor=375;newInk->blackLimit=80;
        replacement.ink=newInk;
        auto pixels=Pixels();Fill(pixels,0,0,640,360,64);
        for(int y=0;y<360;++y)for(int x=0;x<640;++x)
            if(newInk->Get(x,y))pixels[y*640+x]=uint16_t(600<<6);
        auto preview=ConfirmedPreview(replacement);
        Assert::IsTrue(tracker.RefreshReferencePixels(preview,Source(pixels)));
        Assert::IsTrue(preview.current.ink==newInk,L"Continuity palette must not overwrite acquisition evidence");
        const auto accepted=tracker.Consume(preview);
        Assert::IsTrue(accepted.detected);Assert::IsTrue(accepted.cue!=old.cue);
        auto partial=replacement;partial.identity=Identity(3);
        auto unknown=std::make_shared<SubtitleInkSnapshot>(*newInk);
        std::fill(unknown->rawInk.begin(),unknown->rawInk.end(),0);
        std::fill(unknown->blackBacking.begin(),unknown->blackBacking.end(),0);
        partial.ink=unknown;partial.text.detected=false;partial.text.lineCount=0;
        preview=Resolve({partial});
        Assert::IsTrue(tracker.RefreshReferencePixels(preview,Source(pixels)));
        const auto held=tracker.Consume(preview);
        Assert::IsTrue(held.detected);Assert::AreEqual(accepted.cue,held.cue);
        Assert::AreEqual(3,held.lineCount);
    }
    TEST_METHOD(CurrentReferenceFailedSamplingLeavesDetectorEvidenceUntouched) {
        auto acquired=NearCardThreeRows(1,false);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*acquired.ink);
        ink->paletteValid=true;ink->inkFloor=375;ink->blackLimit=80;acquired.ink=ink;
        SubtitleBoxPresentation tracker;tracker.Consume(ConfirmedPreview(acquired));
        acquired.identity=Identity(2);auto preview=ConfirmedPreview(acquired);
        auto pixels=Pixels();auto source=Source(pixels);source.dataBytes=0;
        Assert::IsFalse(tracker.RefreshReferencePixels(preview,source));
        Assert::IsTrue(preview.current.ink==ink);
    }
    TEST_METHOD(ResolvedSubsetCannotDropVerifiedCaptureRows) {
        for(int omitted : {0,1}) {
            auto acquired=NearCardThreeRows(1,false);
            SubtitleBoxPresentation tracker;SubtitleCutPastePresentation compositor;
            auto first=ConfirmedPreview(acquired);const auto accepted=tracker.Consume(first);
            compositor.Consume(accepted,first);
            auto current=acquired;current.identity=Identity(2);
            auto preview=ConfirmedPreview(current);preview.futureAvailable=true;
            preview.currentLinesConfirmed=true;preview.matchingFrames=4;
            preview.text.lineCount=0;preview.text.lineBounds={};
            preview.text.lineCapturePanels={};preview.text.linePanels={};
            for(int i=0;i<3;++i)if(i!=omitted) {
                const int dst=preview.text.lineCount++;
                preview.text.lineBounds[dst]=current.text.lineBounds[i];
                preview.text.lineCapturePanels[dst]=current.text.lineCapturePanels[i];
                preview.text.linePanels[dst]=current.text.linePanels[i];
            }
            const auto held=tracker.Consume(preview);
            Assert::AreEqual(accepted.cue,held.cue);Assert::AreEqual(3,held.lineCount);
            const auto geometry=compositor.Consume(held,preview);Assert::IsTrue(geometry.valid);
            for(int i=0;i<3;++i) {
                const auto& row=acquired.text.lineBounds[i];
                Assert::AreEqual(row.top,held.lineBounds[i].top);
                const int center=(row.top+row.bottom)/2;
                bool covered=false;
                for(const auto& mask:geometry.glyphLines)
                    covered |= mask.Valid() && mask.top<=center && mask.bottom>center;
                Assert::IsTrue(covered,L"Every original row must remain in the extraction mask");
            }
        }
    }
    TEST_METHOD(ConfirmedSubsetCannotVetoCurrentPixelGraceAsFullReplacement) {
        for(bool fullReplacement : {false,true}) {
            auto acquired=NearCardThreeRows(1,false);
            SubtitleBoxPresentation tracker;const auto initial=tracker.Consume(ConfirmedPreview(acquired));
            auto current=acquired;current.identity=Identity(2);
            // Segmentation signatures vary while all but a fringe of actual ink remains.
            for(auto& signature:current.text.lineSignatures)signature.fill(0xaa);
            auto ink=std::make_shared<SubtitleInkSnapshot>(*acquired.ink);
            for(int y=264;y<267;++y)for(int x=200;x<205;++x) {
                const size_t p=size_t(y)*640+x;ink->rawInk[p/64]&=~(uint64_t(1)<<(p%64));
            }
            current.ink=ink;
            auto preview=ConfirmedPreview(current);preview.futureAvailable=true;
            preview.matchingFrames=2;preview.currentLinesConfirmed=true;
            if(!fullReplacement) {
                preview.text.lineCount=1;preview.text.lineBounds[0]=current.text.lineBounds[2];
                preview.text.lineBounds[1]={};preview.text.lineBounds[2]={};
            }
            const auto result=tracker.Consume(preview,250,40);
            Assert::IsTrue(result.detected);Assert::AreEqual(3,result.lineCount);
            if(fullReplacement)Assert::IsTrue(result.cue!=initial.cue);
            else {
                Assert::AreEqual(initial.cue,result.cue);
                Assert::IsTrue(result.held);
                Assert::AreEqual(acquired.text.lineBounds[0].top,result.lineBounds[0].top);
            }
        }
    }
    TEST_METHOD(RejectedCandidateRowIsNotRequiredForAcceptedCueContinuity) {
        auto current=NearCardThreeRows(1,false);
        auto preview=ConfirmedPreview(current);
        preview.text.lineCount=2;
        for(int i=0;i<2;++i)preview.text.lineBounds[i]=current.text.lineBounds[i+1];
        preview.text.lineBounds[2]={};
        preview.text.bounds.top=preview.text.lineBounds[0].top;
        SubtitleBoxPresentation tracker;const auto accepted=tracker.Consume(preview);
        Assert::IsTrue(accepted.detected);Assert::AreEqual(2,accepted.lineCount);
        auto next=current;next.identity=Identity(2);next.text=preview.text;
        auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        const auto rejected=current.text.lineBounds[0];
        for(int y=rejected.top;y<rejected.bottom;++y)for(int x=rejected.left;x<rejected.right;++x) {
            const size_t p=size_t(y)*640+x;const uint64_t bit=uint64_t(1)<<(p%64);
            ink->rawInk[p/64]&=~bit;ink->ownedInk[p/64]&=~bit;ink->blackBacking[p/64]|=bit;
        }
        next.ink=ink;
        const auto held=tracker.Consume(ConfirmedPreview(next));
        Assert::AreEqual(accepted.cue,held.cue);
        Assert::AreEqual(2,held.lineCount);
        // A later fully confirmed row can still join this caption.
        current.identity=Identity(3);
        const auto expanded=tracker.Consume(ConfirmedPreview(current));
        Assert::AreEqual(accepted.cue,expanded.cue);Assert::AreEqual(3,expanded.lineCount);
        // Removing a genuinely accepted row must invalidate the template.
        next.identity=Identity(4);next.text.detected=false;next.text.lineCount=0;
        const auto gone=tracker.Consume(Resolve({next}));
        Assert::IsFalse(gone.detected);
    }
    TEST_METHOD(BackedTinyComponentRetentionAllowsOnlyOneFringeSample) {
        for(int count : {4,5,6})for(int lost : {1,2}) {
            auto old=CompleteBackedCue(1);
            auto ink=std::make_shared<SubtitleInkSnapshot>(*old.ink);
            // Isolate a tiny component in existing empty space on the first row.
            for(int y=318;y<332;++y)for(int x=205;x<209;++x) {
                const size_t p=size_t(y)*640+x;const uint64_t bit=uint64_t(1)<<(p%64);
                ink->rawInk[p/64]&=~bit;ink->ownedInk[p/64]&=~bit;ink->blackBacking[p/64]|=bit;
            }
            for(int i=0;i<count;++i) {
                const size_t p=size_t(321+i/2)*640+206+i%2;const uint64_t bit=uint64_t(1)<<(p%64);
                ink->rawInk[p/64]|=bit;ink->ownedInk[p/64]|=bit;ink->blackBacking[p/64]&=~bit;
            }
            old.ink=ink;auto now=old;now.identity=Identity(2);
            auto partial=std::make_shared<SubtitleInkSnapshot>(*ink);
            for(int i=count-lost;i<count;++i) {
                const size_t p=size_t(321+i/2)*640+206+i%2;
                partial->rawInk[p/64]&=~(uint64_t(1)<<(p%64));
            }
            now.ink=partial;
            const bool allowed=count>=5 && lost==1;
            Assert::AreEqual(allowed,SubtitleBoxLookahead::ConfidentSameInk(old,now));
            Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(old,now),
                L"Acquisition and extension proof must retain the strict component rule");
            std::fill(partial->blackBacking.begin(),partial->blackBacking.end(),0);
            Assert::IsFalse(SubtitleBoxLookahead::ConfidentSameInk(old,now),
                L"Tiny loss exception requires freshly verified backing");
        }
    }
    TEST_METHOD(SharedPictureAuthorityKeepsCaptionAcrossAmbiguousBarPixels) {
        auto pixels=Pixels();
        // Keep an actual bright picture with a centered dark intrusion, plus real current
        // black-band boundaries. The text card crosses the actual lower edge.
        Fill(pixels,260,100,380,200,64);Text(pixels,200,310,16);
        const auto source=Source(pixels);
        const auto independent=ExtractSubtitleBarEvidence(source);
        Assert::IsTrue(independent.available,L"dark picture texture must not erase current edge proof");
        Assert::AreEqual(45,independent.top);Assert::AreEqual(315,independent.bottom);
        SubtitlePictureAuthority authority;authority.required=true;authority.identity=Identity(1);
        authority.bounds={0,45,640,315,640,360,640.0/270,ActivePictureBounds::BarAxes::TOP_BOTTOM};
        SubtitleMeasurementSampler sampler;SubtitleBoxPresentation tracker;uint64_t cue=0;
        for(uint64_t frame=1;frame<=20;++frame) {
            auto key=Cue(frame);key.sharedPicture=authority;
            auto observation=sampler.Measure(source,key,{},60);
            Assert::IsTrue(observation.barAuthority);Assert::AreEqual(315,observation.pictureBottom);
            Assert::IsTrue(observation.text.detected);
            auto preview=SubtitleBoxLookahead::Resolve(&observation,1,0,0);
            const auto result=tracker.Consume(preview,250,1000.0/60);
            Assert::IsTrue(result.detected);
            if(frame==1)cue=result.cue;
            Assert::AreEqual(cue,result.cue);
        }
        // Authority can persist; missing glyphs still cannot become retained text.
        Fill(pixels,190,300,430,340,64);
        auto key=Cue(21);key.sharedPicture=authority;
        auto blank=sampler.Measure(Source(pixels),key,{},60);
        Assert::IsFalse(blank.text.detected);
        Assert::IsFalse(tracker.Consume(SubtitleBoxLookahead::Resolve(&blank,1,0,0),250,1000.0/60).detected);
    }
    TEST_METHOD(IndependentCurrentBarsAllowAnalysisWithoutSharedPresentationAuthority) {
        auto pixels=Pixels();Text(pixels,200,318,16);SubtitleMeasurementSampler sampler;
        for(int change=0;change<6;++change) {
            auto key=Cue(1);key.sharedPicture.required=true;key.sharedPicture.identity=Identity(1);
            key.sharedPicture.bounds={0,45,640,315,640,360};
            if(change==0)key.sharedPicture.bounds={};
            if(change==1)key.sharedPicture.bounds={0,0,640,360,640,360};
            if(change==2)++key.sharedPicture.identity.transportGeneration;
            if(change==3)++key.sharedPicture.identity.viewportGeneration;
            if(change==4)++key.sharedPicture.identity.sourceFormatGeneration;
            if(change==5)++key.sharedPicture.identity.rendererGeneration;
            auto observation=sampler.Measure(Source(pixels),key,{},60);
            Assert::IsTrue(observation.barAuthority);Assert::IsTrue(observation.text.detected);
            Assert::IsTrue(observation.independentBarAnalysis);
            Assert::AreEqual(45,observation.pictureTop);Assert::AreEqual(315,observation.pictureBottom);
            Assert::IsFalse(observation.sharedPicture.AvailableFor(key.identity,640,360),
                L"Independent scanning must not manufacture shared crop authority");
        }
        // Removing physical bars must still reject picture lights, regardless
        // of an old presentation snapshot or a previously detected cue.
        Fill(pixels,0,0,640,360,256);Text(pixels,200,318,16);
        auto key=Cue(2);key.sharedPicture.required=true;
        auto withoutBars=sampler.Measure(Source(pixels),key,{},60);
        Assert::IsFalse(withoutBars.barAuthority);
        Assert::IsFalse(withoutBars.text.detected);
        Assert::IsFalse(withoutBars.subtitleArProof.accepted);
    }
    TEST_METHOD(IndependentPixelMeasurementSurvivesPresentationSnapshotChangeOnly) {
        auto observation=Cue(7);observation.independentBarAnalysis=true;
        observation.sharedPicture.required=true;
        observation.sharedPicture.identity=observation.identity;
        SubtitlePictureAuthority published=observation.sharedPicture;
        published.bounds={0,45,640,315,640,360};
        Assert::IsTrue(SubtitleBoxLookahead::RebindIndependentPicture(observation,published));
        Assert::IsTrue(observation.sharedPicture.Matches(published));
        Assert::IsTrue(SameAnalysisFrameIdentity(observation.identity,Identity(7)));
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(observation,Identity(8),640,360,0,0,false,{}));
        auto otherDomain=Identity(7);++otherDomain.analysisDomainKey;
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(observation,otherDomain,640,360,0,0,false,{}));
        auto nextContinuity=observation;
        ++nextContinuity.continuityGeneration;
        Assert::IsFalse(SubtitleBoxLookahead::RebindIndependentMeasurement(
            observation,nextContinuity.continuityGeneration));
        Assert::IsFalse(SubtitleMeasurementWorker::SameKey(observation,nextContinuity),
            L"A stale independent proof must not suppress the new worker scan");
        for (int invalid=0;invalid<4;++invalid) {
            auto unsafe=observation;unsafe.sharedPicture.bounds={};
            if(invalid==0)unsafe.pendingRefresh=true;
            if(invalid==1)unsafe.analysisRefresh=true;
            if(invalid==2)unsafe.barTrackingAuthority=true;
            if(invalid==3)unsafe.independentBarAnalysis=false;
            Assert::IsFalse(SubtitleBoxLookahead::RebindIndependentPicture(unsafe,published));
        }
    }
    TEST_METHOD(IndependentCurrentBlankScanIgnoresUnconsumedWorkerPredecessor) {
        auto pixels=Pixels();
        auto key=Cue(151);
        key.policyGeneration=2;key.continuityGeneration=4;
        key.sharedPicture.required=true;
        key.sharedPicture.identity=key.identity;
        key.sharedPicture.bounds={0,45,640,315,640,360};
        SubtitleBarTrackingReference workerPrior;
        workerPrior.identity=Identity(150);
        workerPrior.width=640;workerPrior.height=360;
        workerPrior.pictureTop=45;workerPrior.pictureBottom=315;
        workerPrior.policyGeneration=2;workerPrior.continuityGeneration=4;
        workerPrior.scriptHint=key.scriptHint;workerPrior.valid=true;
        SubtitleMeasurementSampler sampler;
        const auto observation=sampler.Measure(Source(pixels),key,workerPrior,60);
        Assert::IsTrue(observation.analyzed);
        Assert::IsTrue(observation.barAuthority);
        Assert::IsTrue(observation.independentBarAnalysis);
        Assert::IsFalse(observation.text.detected);
        Assert::IsFalse(observation.text.workLimit);
        Assert::IsTrue(observation.text.diagnosticReason &&
            std::strcmp(observation.text.diagnosticReason,"no-bright-ink")==0);
        Assert::IsTrue(observation.incomingBarTrackingReference.valid);
        Assert::IsFalse(observation.hasBarTrackingReference);
        Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(observation,
            key.identity,640,360,2,4,false,{},key.scriptHint),
            L"The complete current blank scan used current bars, not the worker predecessor.");
        auto differentPrior=workerPrior;
        differentPrior.identity=Identity(149);
        Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(observation,
            key.identity,640,360,2,4,false,differentPrior,key.scriptHint),
            L"This exact shared-picture scan returned before consulting either predecessor.");
        // Preserve the narrower generic independent-scan contract separately:
        // when this observation is not a shared-picture result, a different
        // newly supplied predecessor still requires a rescan.
        auto independentOnly=observation;independentOnly.sharedPicture={};
        Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(independentOnly,
            key.identity,640,360,2,4,false,{},key.scriptHint));
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(independentOnly,
            key.identity,640,360,2,4,false,differentPrior,key.scriptHint),
            L"A generic independent negative scan remains tied to its measured predecessor.");

        auto otherFrame=key.identity;++otherFrame.acceptedSequence;
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(observation,
            otherFrame,640,360,2,4,false,{},key.scriptHint));
        otherFrame=key.identity;++otherFrame.analysisDomainKey;
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(observation,
            otherFrame,640,360,2,4,false,{},key.scriptHint));
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(observation,
            key.identity,640,360,3,4,false,{},key.scriptHint));
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(observation,
            key.identity,640,360,2,5,false,{},key.scriptHint));
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(observation,
            key.identity,640,360,2,4,true,{},key.scriptHint));
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(observation,
            key.identity,640,360,2,4,false,{},SubtitleScriptHint::Arabic));

        for(int unsafe=0;unsafe<14;++unsafe) {
            auto changed=independentOnly;
            if(unsafe==0)changed.independentBarAnalysis=false;
            if(unsafe==1)changed.barAuthority=false;
            if(unsafe==2)changed.barTrackingAuthority=true;
            if(unsafe==3)changed.hasBarTrackingReference=true;
            if(unsafe==4)changed.currentAnchorUsesTrackedEdge=true;
            if(unsafe==5)changed.analysisRefresh=true;
            if(unsafe==6)changed.pendingRefresh=true;
            if(unsafe==7)changed.barEvidence.topRevalidatedAtReference=true;
            if(unsafe==8)changed.barEvidence.captionBackedBottom=true;
            if(unsafe==9)changed.text.workLimit=true;
            if(unsafe==10)changed.text.diagnosticReason="no-centered-text-line";
            if(unsafe==11)++changed.pictureBottom;
            if(unsafe==12)changed.text.held=true;
            if(unsafe==13)changed.discontinuity=true;
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(changed,
                key.identity,640,360,2,4,changed.discontinuity,{},key.scriptHint),
                L"Only a complete independent current-bar no-ink scan may ignore the predecessor.");
        }
    }
    TEST_METHOD(SharedCurrentNegativeAndPartialMeasurementsIgnoreUnusedWorkerPrior) {
        constexpr uint64_t policy=2,continuity=4;
        for(bool partialBottom:{false,true}) {
            const uint64_t sequence=partialBottom?153:152;
            auto pixels=Pixels(partialBottom);
            if(partialBottom)Fill(pixels,0,0,640,45,64); // top bar only; bottom edge remains unavailable
            auto key=Cue(sequence);key.identity=Identity(sequence);
            key.policyGeneration=policy;key.continuityGeneration=continuity;
            key.nearBarDistance=20;key.optimizationMode=0;
            key.sharedPicture.required=true;key.sharedPicture.identity=key.identity;
            key.sharedPicture.bounds={0,45,640,315,640,360};

            SubtitleBarTrackingReference workerPrior;
            workerPrior.identity=Identity(sequence-1);workerPrior.width=640;workerPrior.height=360;
            workerPrior.pictureTop=45;workerPrior.pictureBottom=315;
            workerPrior.policyGeneration=policy;workerPrior.continuityGeneration=continuity;
            workerPrior.scriptHint=key.scriptHint;workerPrior.valid=true;

            SubtitleMeasurementSampler sampler;
            const auto measured=sampler.Measure(Source(pixels),key,workerPrior,60);
            Assert::IsTrue(measured.analyzed);
            Assert::IsTrue(measured.barAuthority);
            Assert::IsTrue(measured.barEvidence.available);
            Assert::IsFalse(measured.text.detected);
            Assert::IsFalse(measured.text.workLimit);
            Assert::IsTrue(measured.text.diagnosticReason &&
                std::strcmp(measured.text.diagnosticReason,"no-bright-ink")==0);
            Assert::IsTrue(measured.incomingBarTrackingReference.valid);
            Assert::IsFalse(measured.hasBarTrackingReference,
                L"The valid shared picture path performs its exact current scan without consuming the worker predecessor.");
            Assert::IsFalse(measured.barTrackingAuthority);
            Assert::IsFalse(measured.currentAnchorUsesTrackedEdge);
            if(partialBottom) {
                Assert::AreEqual(45,measured.pictureTop);
                Assert::AreEqual(360,measured.pictureBottom,
                    L"A partial top-only current scan remains incomplete when reused.");
            } else {
                Assert::AreEqual(45,measured.pictureTop);
                Assert::AreEqual(315,measured.pictureBottom);
            }

            SubtitleBarTrackingReference emptyPrior;
            auto differentPrior=workerPrior;
            differentPrior.identity=Identity(sequence-2);
            differentPrior.pictureTop=60;differentPrior.pictureBottom=300;
            Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(measured,key.identity,
                640,360,policy,continuity,false,emptyPrior,key.scriptHint),
                L"Renderer history may be empty even though the worker supplied an unused valid predecessor.");
            Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(measured,key.identity,
                640,360,policy,continuity,false,differentPrior,key.scriptHint),
                L"A different tracking predecessor cannot change an exact shared-picture scan.");
            Assert::IsFalse(measured.text.detected,
                L"Reusing a negative/partial result must preserve its no-ink outcome.");
            Assert::AreEqual(partialBottom?360:315,measured.pictureBottom);

            auto other=key.identity;++other.acceptedSequence;
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(measured,other,
                640,360,policy,continuity,false,emptyPrior,key.scriptHint));
            other=key.identity;++other.analysisDomainKey;
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(measured,other,
                640,360,policy,continuity,false,emptyPrior,key.scriptHint));
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(measured,key.identity,
                640,360,policy+1,continuity,false,emptyPrior,key.scriptHint));
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(measured,key.identity,
                640,360,policy,continuity+1,false,emptyPrior,key.scriptHint));
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(measured,key.identity,
                640,360,policy,continuity,true,emptyPrior,key.scriptHint));
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(measured,key.identity,
                640,360,policy,continuity,false,emptyPrior,SubtitleScriptHint::Arabic));

            auto changed=measured;changed.barTrackingAuthority=true;
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(changed,key.identity,
                640,360,policy,continuity,false,emptyPrior,key.scriptHint));
            changed=measured;changed.hasBarTrackingReference=true;
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(changed,key.identity,
                640,360,policy,continuity,false,emptyPrior,key.scriptHint));
            changed=measured;changed.currentAnchorUsesTrackedEdge=true;
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(changed,key.identity,
                640,360,policy,continuity,false,emptyPrior,key.scriptHint));
            changed=measured;changed.analysisRefresh=true;
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(changed,key.identity,
                640,360,policy,continuity,false,emptyPrior,key.scriptHint));
            changed=measured;changed.pendingRefresh=true;
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(changed,key.identity,
                640,360,policy,continuity,false,emptyPrior,key.scriptHint));

            Assert::IsTrue(SubtitleMeasurementWorker::SameKey(measured,key));
            auto changedKey=key;changedKey.nearBarDistance++;
            Assert::IsFalse(SubtitleMeasurementWorker::SameKey(measured,changedKey));
            changedKey=key;++changedKey.optimizationMode;
            Assert::IsFalse(SubtitleMeasurementWorker::SameKey(measured,changedKey));
            changedKey=key;++changedKey.policyGeneration;
            Assert::IsFalse(SubtitleMeasurementWorker::SameKey(measured,changedKey));
            changedKey=key;changedKey.sharedPicture.bounds.bottom=314;
            Assert::IsFalse(SubtitleMeasurementWorker::SameKey(measured,changedKey));
            changedKey=key;changedKey.scriptHint=SubtitleScriptHint::Arabic;
            Assert::IsFalse(SubtitleMeasurementWorker::SameKey(measured,changedKey));
        }
    }
    TEST_METHOD(WorkerSharedPartialScanWithHistoryIsReusableButNotAbsenceEvidence) {
        constexpr uint64_t policy=7,continuity=9;
        auto pixels=Pixels(true);Fill(pixels,0,0,640,45,64); // current top bar only
        SubtitleMeasurementWorker worker;

        auto firstKey=Cue(160);firstKey.identity=Identity(160);
        firstKey.policyGeneration=policy;firstKey.continuityGeneration=continuity;
        firstKey.nearBarDistance=20;firstKey.optimizationMode=0;
        worker.Submit(firstKey,[pixels,firstKey](SubtitleMeasurementSampler& sampler,
            const SubtitleBarTrackingReference& prior) mutable {
            auto source=Source(pixels);
            return sampler.Measure(source,firstKey,prior,60);
        });
        SubtitleBoxObservation firstResult;bool firstReady=false;
        const auto firstUntil=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(std::chrono::steady_clock::now()<firstUntil && !(firstReady=worker.TryTake(firstKey,firstResult)))
            std::this_thread::yield();
        Assert::IsTrue(firstReady,L"first measurement establishes worker tracking history");
        Assert::IsTrue(SubtitleBoxLookahead::AdvanceBarTrackingReference(firstResult).valid);

        auto currentKey=Cue(161);currentKey.identity=Identity(161);
        currentKey.policyGeneration=policy;currentKey.continuityGeneration=continuity;
        currentKey.nearBarDistance=20;currentKey.optimizationMode=0;
        currentKey.sharedPicture.required=true;currentKey.sharedPicture.identity=currentKey.identity;
        currentKey.sharedPicture.bounds={0,45,640,315,640,360};
        worker.Submit(currentKey,[pixels,currentKey](SubtitleMeasurementSampler& sampler,
            const SubtitleBarTrackingReference& prior) mutable {
            auto source=Source(pixels);
            return sampler.Measure(source,currentKey,prior,60);
        });
        SubtitleBoxObservation measured;bool ready=false;
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(std::chrono::steady_clock::now()<until && !(ready=worker.TryRead(currentKey,measured)))
            std::this_thread::yield();
        worker.Stop();
        Assert::IsTrue(ready,L"exact shared-current measurement remains readable");
        Assert::IsTrue(measured.incomingBarTrackingReference.valid,
            L"worker history supplied a prior to the measurement");
        Assert::IsFalse(measured.hasBarTrackingReference,
            L"shared-current measurement did not consume the tracking prior");
        Assert::IsTrue(measured.barAuthority);
        Assert::AreEqual(45,measured.pictureTop);
        Assert::AreEqual(360,measured.pictureBottom,
            L"the missing bottom edge remains incomplete after worker read");
        Assert::IsFalse(measured.text.detected);
        Assert::IsTrue(SubtitleAbsenceIsUnknown(measured),
            L"a scan with no bottom-edge search coverage cannot certify cue absence");
        SubtitleBoxResult accepted;
        Assert::IsFalse(ConfirmedSubtitleAbsentForPresentation(measured,accepted,currentKey.identity));

        SubtitleBarTrackingReference otherPrior;
        otherPrior.identity=Identity(158);otherPrior.width=640;otherPrior.height=360;
        otherPrior.pictureTop=60;otherPrior.pictureBottom=300;
        otherPrior.policyGeneration=policy;otherPrior.continuityGeneration=continuity;
        otherPrior.scriptHint=currentKey.scriptHint;otherPrior.valid=true;
        Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(measured,currentKey.identity,
            640,360,policy,continuity,false,{},currentKey.scriptHint));
        Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(measured,currentKey.identity,
            640,360,policy,continuity,false,otherPrior,currentKey.scriptHint));
        Assert::IsTrue(SubtitleAbsenceIsUnknown(measured),
            L"reusing the same immutable result leaves incomplete coverage unknown");
    }
    TEST_METHOD(CurrentNegativeScanCanRebindBetweenUnavailablePictureSnapshots) {
        const auto identity=Identity(37);
        for(int scenario=0;scenario<2;++scenario) {
            auto pixels=Pixels(true);
            if(scenario==1)Fill(pixels,0,0,640,45,64); // one strong edge, no bottom bar
            auto key=Cue(identity.acceptedSequence);
            key.identity=identity;key.policyGeneration=11;key.continuityGeneration=13;
            key.sharedPicture.required=true;
            key.sharedPicture.identity=identity;
            key.sharedPicture.identity.viewportGeneration++;
            key.sharedPicture.bounds={0,0,640,360,640,360}; // invalid full-raster authority
            SubtitleBarTrackingReference prior;
            prior.identity=Identity(identity.acceptedSequence-1);
            prior.width=640;prior.height=360;prior.pictureTop=45;prior.pictureBottom=315;
            prior.policyGeneration=key.policyGeneration;
            prior.continuityGeneration=key.continuityGeneration;
            prior.scriptHint=key.scriptHint;prior.valid=true;
            SubtitleMeasurementSampler sampler;
            auto observation=sampler.Measure(Source(pixels),key,prior,60);
            Assert::IsTrue(observation.analyzed);
            Assert::IsFalse(observation.text.detected);
            Assert::IsFalse(observation.independentBarAnalysis,
                L"A negative scan with no bars or only one edge is not an independent bar certificate.");
            Assert::IsTrue(observation.pictureAuthorityUnavailableForMeasurement);
            Assert::IsFalse(observation.sharedPicture.AvailableFor(identity,640,360));

            auto nextSnapshot=key.sharedPicture;
            nextSnapshot.identity.rendererGeneration++;
            nextSnapshot.bounds={};
            Assert::IsFalse(nextSnapshot.AvailableFor(identity,640,360));
            Assert::IsFalse(observation.sharedPicture.Matches(nextSnapshot));
            Assert::IsTrue(SubtitleBoxLookahead::RebindIndependentPicture(
                observation,nextSnapshot),
                L"This completed exact-frame scan used raw current bars; neither unavailable snapshot supplied geometry.");
            Assert::IsTrue(observation.sharedPicture.Matches(nextSnapshot));
            Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(observation,
                identity,640,360,11,13,false,{}),
                L"The required-but-unavailable branch never consumed its supplied valid predecessor.");
            auto changedReference=prior;
            changedReference.identity=Identity(2);
            Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(observation,
                identity,640,360,11,13,false,changedReference),
                L"Changing the irrelevant current predecessor cannot invalidate this exact-frame scan.");

            auto changedFrame=identity;
            ++changedFrame.acceptedSequence;++changedFrame.sourceFrameNumber;
            ++changedFrame.captureTimestamp;
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(observation,
                changedFrame,640,360,11,13,false,{}),
                L"Snapshot rebinding does not relax exact source-frame identity.");
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(observation,
                identity,640,360,11,14,false,{}),
                L"The cold-scan reuse path still requires the exact continuity generation.");
            Assert::IsFalse(SubtitleBoxLookahead::RebindIndependentMeasurement(
                observation,observation.continuityGeneration+1),
                L"Snapshot rebinding does not relax continuity-bound certificate rules.");

            auto authorityNowAvailable=observation;
            auto available=nextSnapshot;
            available.identity=identity;
            available.bounds={0,45,640,315,640,360};
            Assert::IsTrue(available.AvailableFor(identity,640,360));
            Assert::IsFalse(SubtitleBoxLookahead::RebindIndependentPicture(
                authorityNowAvailable,available),
                L"A newly available shared snapshot can change caption analysis and must trigger a rescan.");
            authorityNowAvailable.sharedPicture=available;
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(authorityNowAvailable,
                identity,640,360,11,13,false,{}),
                L"A newly available shared authority disables the unavailable-snapshot shortcut.");

            for(int unsafe=0;unsafe<5;++unsafe) {
                auto guarded=observation;
                if(unsafe==0)guarded.barTrackingAuthority=true;
                if(unsafe==1)guarded.hasBarTrackingReference=true;
                if(unsafe==2)guarded.currentAnchorUsesTrackedEdge=true;
                if(unsafe==3)guarded.analysisRefresh=true;
                if(unsafe==4)guarded.pendingRefresh=true;
                Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(guarded,
                    identity,640,360,11,13,false,{}),
                    L"Tracked, refreshed, or pending measurements cannot use the cold-scan shortcut.");
            }
        }
    }
    TEST_METHOD(WorkerKeyIncludesSharedAuthorityEvenForSameSourceFrame) {
        auto a=Cue(1);a.sharedPicture.required=true;a.sharedPicture.identity=Identity(1);
        a.sharedPicture.bounds={0,45,640,315,640,360};auto b=a;
        Assert::IsTrue(SubtitleMeasurementWorker::SameKey(a,b));
        b.sharedPicture.bounds.bottom=310;Assert::IsFalse(SubtitleMeasurementWorker::SameKey(a,b));
        b=a;b.sharedPicture.bounds={};Assert::IsFalse(SubtitleMeasurementWorker::SameKey(a,b));
        b=a;b.sharedPicture.required=false;Assert::IsFalse(SubtitleMeasurementWorker::SameKey(a,b));
    }
    TEST_METHOD(PendingMeasurementUsesSharedAuthorityWhenCurrentBandsRemainValid) {
        auto pixels=Pixels();Fill(pixels,260,100,380,200,64);Text(pixels,200,310,16);
        auto key=Cue(1);key.sharedPicture.required=true;key.sharedPicture.identity=Identity(1);
        key.sharedPicture.bounds={0,45,640,315,640,360};SubtitleMeasurementSampler sampler;
        auto observed=sampler.Measure(Source(pixels),key,{},60);
        Assert::IsTrue(observed.text.detected);
        auto ready=SubtitleBoxLookahead::Resolve(&observed,1,7,8);SubtitlePendingMeasurementGuard guard;
        guard.Resolve(ready,Source(pixels),1000.0/60,250);
        SubtitleBoxPresentation presentation;
        const auto acquired=presentation.Consume(ConfirmedPreview(observed),250,1000.0/60);
        Assert::IsTrue(acquired.detected);
        for(uint64_t sequence=2;sequence<=4;++sequence) {
            SubtitleBoxPreview pending;pending.current=key;pending.current.identity=Identity(sequence);
            Assert::IsTrue(guard.Resolve(pending,Source(pixels),1000.0/60,250));
            Assert::IsTrue(pending.current.pendingRefresh);
            Assert::AreEqual(ready.policyGeneration,pending.policyGeneration,
                L"Ordinary pending pixels preserve the accepted preview policy generation.");
            Assert::AreEqual(ready.continuityGeneration,pending.continuityGeneration,
                L"Ordinary pending pixels preserve wrapper continuity; default observation metadata cannot replace it.");
            const auto kept=presentation.Consume(pending,250,1000.0/60);
            Assert::IsTrue(kept.detected);Assert::AreEqual(acquired.cue,kept.cue,
                L"fresh current bar proof and unchanged glyph pixels retain the old cue");
        }
        SubtitleBoxPreview stale;stale.current=key;stale.current.identity=Identity(5);
        stale.current.sharedPicture.bounds={};
        Assert::IsFalse(guard.Resolve(stale,Source(pixels),1000.0/60,250));
    }
    TEST_METHOD(SharedRetainedBoundsUseCurrentBandsAndRejectInteriorCityLights) {
        auto pixels=BarredPixels(11,348);
        // A bright, card-backed pattern crosses retained top=45, but lies
        // wholly inside this frame's active picture, whose top edge is 11.
        Text(pixels,200,34,16);
        const auto source=Source(pixels);
        SubtitlePictureAuthority authority;authority.required=true;authority.identity=Identity(1);
        authority.bounds={0,45,640,315,640,360};
        auto key=Cue(1);key.nearBarDistance=0;key.sharedPicture=authority;
        SubtitleMeasurementSampler sampler;
        const auto observation=sampler.Measure(source,key,{},60);
        Assert::IsTrue(observation.sharedPicture.Matches(authority),L"Classic presentation snapshot stays intact");
        Assert::IsTrue(observation.barAuthority);
        Assert::AreEqual(11,observation.pictureTop,L"analysis uses the current top edge");
        Assert::AreEqual(348,observation.pictureBottom,L"analysis uses the current bottom edge");
        Assert::IsFalse(observation.text.detected,
            L"a pattern crossing only the obsolete retained plane is interior picture content");
    }
    TEST_METHOD(UniformDarkRasterCannotSubstituteForCurrentSharedBandEvidence) {
        auto pixels=Pixels(true);Fill(pixels,0,0,640,360,64);Text(pixels,200,310,16);
        SubtitlePictureAuthority authority;authority.required=true;authority.identity=Identity(1);
        authority.bounds={0,45,640,315,640,360};
        auto key=Cue(1);key.sharedPicture=authority;
        SubtitleMeasurementSampler sampler;
        const auto observation=sampler.Measure(Source(pixels),key,{},60);
        Assert::IsTrue(observation.sharedPicture.Matches(authority));
        Assert::IsFalse(observation.barAuthority,
            L"uniform darkness and a caption-shaped card do not establish a physical picture edge");
        Assert::IsFalse(observation.text.detected);
    }
    TEST_METHOD(SharedCurrentSingleSideBarsPreserveOnlyTheProvedOppositeSide) {
        for(bool topOnly : {true,false}) {
            auto pixels=Pixels(true);
            if(topOnly) {
                Fill(pixels,0,0,640,11,64);
                Text(pixels,200,3,7);
            } else {
                Fill(pixels,0,348,640,360,64);
                Text(pixels,200,336,7);
            }
            const auto source=Source(pixels);
            SubtitlePictureAuthority authority;authority.required=true;authority.identity=Identity(1);
            authority.bounds={0,45,640,315,640,360};
            auto key=Cue(1);key.nearBarDistance=0;key.sharedPicture=authority;
            SubtitleMeasurementSampler sampler;
            const auto observation=sampler.Measure(source,key,{},60);
            Assert::IsTrue(observation.barAuthority);
            Assert::AreEqual(topOnly?11:0,observation.pictureTop);
            Assert::AreEqual(topOnly?360:348,observation.pictureBottom);
            Assert::IsTrue(observation.text.detected,
                L"a real caption crossing the one currently proved band remains eligible");
        }
    }
    TEST_METHOD(SharedCurrentBottomBoundaryRetainsArabicHintMultilineCaption) {
        auto pixels=BarredPixels(11,348);
        ArabicTextLine(pixels,220,307,180,14);ArabicTextLine(pixels,260,336,120,14);
        const auto source=Source(pixels);
        SubtitlePictureAuthority authority;authority.required=true;authority.identity=Identity(1);
        authority.bounds={0,45,640,315,640,360};
        auto key=Cue(1);key.nearBarDistance=0;key.scriptHint=SubtitleScriptHint::Arabic;
        key.sharedPicture=authority;
        SubtitleMeasurementSampler sampler;
        const auto observation=sampler.Measure(source,key,{},60);
        Assert::AreEqual(11,observation.pictureTop);
        Assert::AreEqual(348,observation.pictureBottom);
        Assert::IsTrue(observation.text.detected,L"caption crossing the current lower boundary is retained");
        Assert::AreEqual(2,observation.text.lineCount,L"both Arabic-hint lines remain grouped");
    }
    TEST_METHOD(SharedCurrentEdgeThreeLineCaptionsSurviveSupportedScriptHints) {
        for(SubtitleScriptHint script : {SubtitleScriptHint::Latin,SubtitleScriptHint::Cjk,
            SubtitleScriptHint::Thai}) {
            auto pixels=BarredPixels(11,348);
            Text(pixels,200,298,15);Text(pixels,215,318,12);Text(pixels,240,338,8);
            const auto source=Source(pixels);
            SubtitlePictureAuthority authority;authority.required=true;authority.identity=Identity(1);
            authority.bounds={0,45,640,315,640,360};
            auto key=Cue(1);key.nearBarDistance=0;key.scriptHint=script;
            key.sharedPicture=authority;
            SubtitleMeasurementSampler sampler;
            const auto observation=sampler.Measure(source,key,{},60);
            Assert::AreEqual(11,observation.pictureTop);
            Assert::AreEqual(348,observation.pictureBottom);
            Assert::IsTrue(observation.text.detected,
                L"the current bottom-edge anchor remains admissible for each script hint");
            Assert::AreEqual(3,observation.text.lineCount,
                L"three physically present rows remain grouped around the current edge anchor");
        }
        auto arabicPixels=BarredPixels(11,348);
        ArabicTextLine(arabicPixels,220,296,180,14);
        ArabicTextLine(arabicPixels,235,316,150,14);
        ArabicTextLine(arabicPixels,260,336,120,14);
        const auto arabicSource=Source(arabicPixels);
        SubtitlePictureAuthority arabicAuthority;arabicAuthority.required=true;
        arabicAuthority.identity=Identity(1);arabicAuthority.bounds={0,45,640,315,640,360};
        auto arabicKey=Cue(1);arabicKey.nearBarDistance=0;
        arabicKey.scriptHint=SubtitleScriptHint::Arabic;arabicKey.sharedPicture=arabicAuthority;
        SubtitleMeasurementSampler arabicSampler;
        const auto arabic=arabicSampler.Measure(arabicSource,arabicKey,{},60);
        Assert::AreEqual(11,arabic.pictureTop);Assert::AreEqual(348,arabic.pictureBottom);
        Assert::IsTrue(arabic.text.detected);Assert::AreEqual(3,arabic.text.lineCount,
            L"three joined Arabic rows remain grouped around the current edge anchor");
    }
    TEST_METHOD(PendingSharedCueRejectsLostPhysicalBarsWhenAcceptedGlyphPixelsAreUnchanged) {
        auto pixels=Pixels();Fill(pixels,260,100,380,200,64);Text(pixels,200,310,16);
        SubtitlePictureAuthority authority;authority.required=true;authority.identity=Identity(1);
        authority.bounds={0,45,640,315,640,360};
        auto key=Cue(1);key.sharedPicture=authority;SubtitleMeasurementSampler sampler;
        const auto observed=sampler.Measure(Source(pixels),key,{},60);
        Assert::IsTrue(observed.text.detected);
        SubtitlePendingMeasurementGuard guard;
        auto accepted=SubtitleBoxLookahead::Resolve(&observed,1,0,0);
        guard.Resolve(accepted,Source(pixels),1000.0/60,250);

        // Keep every pixel in the guard's expanded line sampling rectangles
        // identical, but remove the full-width black bands and their edges.
        // The retained Classic authority and accepted glyph samples therefore
        // remain unchanged while current physical bar evidence is absent.
        auto noBars=Pixels(true);Fill(noBars,0,0,640,360,300);
        const int step=SubtitleBoxDetector::SamplingStep(640,360);
        for(int line=0;line<observed.text.lineCount && line<3;++line) {
            const auto r=observed.text.lineBounds[line];
            const int top=(std::max)(0,r.top-step),bottom=(std::min)(360,r.bottom+step);
            const int left=(std::max)(0,r.left-step),right=(std::min)(640,r.right+step);
            for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)
                noBars[size_t(y)*640+x]=pixels[size_t(y)*640+x];
        }
        SubtitleBoxPreview pending;pending.current=key;pending.current.identity=Identity(2);
        Assert::IsFalse(guard.Resolve(pending,Source(noBars),1000.0/60,250));
        Assert::AreEqual(std::string("current-bars-changed"),std::string(guard.DecisionReason()),
            L"old shared bounds and unchanged accepted glyph pixels cannot stand in for disappeared physical bars");
    }
    TEST_METHOD(PendingSharedCueRejectsCurrentBandGeometryChange) {
        auto pixels=Pixels();Fill(pixels,260,100,380,200,64);Text(pixels,200,310,16);
        SubtitlePictureAuthority authority;authority.required=true;authority.identity=Identity(1);
        authority.bounds={0,45,640,315,640,360};
        auto key=Cue(1);key.sharedPicture=authority;SubtitleMeasurementSampler sampler;
        const auto observed=sampler.Measure(Source(pixels),key,{},60);
        Assert::IsTrue(observed.text.detected);
        SubtitlePendingMeasurementGuard guard;
        guard.Resolve(SubtitleBoxLookahead::Resolve(&observed,1,0,0),Source(pixels),1000.0/60,250);
        auto shifted=BarredPixels(60,300);Text(shifted,200,310,16);
        auto pending=SubtitleBoxPreview{};pending.current=key;pending.current.identity=Identity(2);
        Assert::IsFalse(guard.Resolve(pending,Source(shifted),1000.0/60,250),
            L"matching caption pixels and retained Classic identity cannot reuse a cue after both current bars move");
    }

    TEST_METHOD(ActualBacklogDropPendingThenCurrentScanRetainsAcquiredCue) {
        ActivePictureDecisionTimeline timeline;
        timeline.Reset(1);
        auto first=CompleteBackedCue(4497);
        first.identity.analysisDomainKey=7;
        first.independentBarAnalysis=true;
        first.barEvidence.available=true;first.barEvidence.top=45;first.barEvidence.bottom=315;
        auto dropped=first.identity;dropped.acceptedSequence=4498;
        dropped.sourceFrameNumber=4498;dropped.captureTimestamp=4498000;
        auto pendingIdentity=first.identity;pendingIdentity.acceptedSequence=4499;
        pendingIdentity.sourceFrameNumber=4499;pendingIdentity.captureTimestamp=4499000;
        auto current=first;current.identity.acceptedSequence=4500;
        current.identity.sourceFrameNumber=4500;current.identity.captureTimestamp=4500000;
        for(const auto& id : {first.identity,dropped,pendingIdentity,current.identity})
            Assert::IsTrue(timeline.TrackAcceptedFrame(id));
        const auto before=timeline.ContinuityGeneration();
        SubtitleBoxPresentation tracker;
        auto initial=Resolve({first});initial.continuityGeneration=before;
        const auto acquired=tracker.Consume(initial);
        Assert::IsTrue(acquired.detected);
        Assert::IsTrue(tracker.HasAcceptedSourceTemplate());
        Assert::IsTrue(tracker.AcceptedTemplateIdentity().acceptedSequence==4497);
        // The submitted output may still select source 4496 here; the current
        // source tracker and future immutable output packet have distinct clocks.
        timeline.MarkConsumed(first.identity);
        timeline.MarkDiscarded(dropped,4498,ActivePictureDiscardReason::BacklogRecovery);
        const auto after=timeline.ContinuityGeneration();
        Assert::IsTrue(timeline.OnlyBacklogRetirementBetween(
            tracker.AcceptedTemplateIdentity(),pendingIdentity,
            tracker.AcceptedTemplateContinuity(),after));
        auto pending=SubtitleBoxPreview{};pending.current=current;
        pending.current.identity=pendingIdentity;
        pending.policyGeneration=initial.policyGeneration;
        pending.continuityGeneration=after;
        Assert::IsFalse(tracker.Consume(pending,250,1000.0/24.0,
            &first.identity,true).detected);
        timeline.MarkConsumed(pendingIdentity);
        Assert::IsTrue(timeline.OnlyBacklogRetirementBetween(
            tracker.AcceptedTemplateIdentity(),current.identity,
            tracker.AcceptedTemplateContinuity(),after),
            L"A processed but unproven intervening source is known, not a cue vote.");
        current.continuityGeneration=after;
        auto completed=Resolve({current});completed.continuityGeneration=after;
        const auto resumed=tracker.Consume(completed,250,1000.0/24.0,
            &first.identity,true);
        Assert::IsTrue(resumed.detected);
        Assert::IsFalse(resumed.held);
        Assert::AreEqual(acquired.cue,resumed.cue);
        Assert::IsTrue(tracker.AcceptedTemplateIdentity().acceptedSequence==4500);
    }
    TEST_METHOD(BacklogOnlyCurrentScanReassociatesSubmittedCueWithoutFutureVote) {
        auto first=CompleteBackedCue(1);
        first.independentBarAnalysis=true;
        first.barEvidence.available=true;first.barEvidence.top=45;first.barEvidence.bottom=315;
        auto initial=Resolve({first});
        SubtitleBoxPresentation tracker;
        const auto accepted=tracker.Consume(initial);
        Assert::IsTrue(accepted.detected);
        auto pending=SubtitleBoxPreview{};
        pending.current=first;pending.current.identity=Identity(4);
        pending.continuityGeneration=9;pending.policyGeneration=initial.policyGeneration;
        Assert::IsFalse(tracker.Consume(pending,250,1000.0/24.0,
            &first.identity,true).detected,
            L"A missing worker result must not emit remembered cue pixels.");
        auto current=first;current.identity=Identity(5);
        current.continuityGeneration=9;
        auto live=Resolve({current});live.continuityGeneration=9;
        const auto resumed=tracker.Consume(live,250,1000.0/24.0,
            &first.identity,true);
        Assert::IsTrue(resumed.detected);
        Assert::IsFalse(resumed.held);
        Assert::AreEqual(accepted.cue,resumed.cue);
        Assert::IsTrue(std::string(tracker.DecisionReason())=="backlog-current-cue-reassociated");
    }
    TEST_METHOD(BacklogReassociationAllowsOfferedButUnusedBarReference) {
        auto first=CompleteBackedCue(1);
        first.independentBarAnalysis=true;
        first.barEvidence.available=true;
        first.barEvidence.top=45;first.barEvidence.bottom=315;
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first}));
        Assert::IsTrue(acquired.detected);
        auto current=first;
        current.identity=Identity(5);
        current.continuityGeneration=9;
        current.hasBarTrackingReference=true; // Offered; neither edge consumed it.
        current.barTrackingAuthority=false;
        current.currentAnchorUsesTrackedEdge=false;
        current.barEvidence.topRevalidatedAtReference=false;
        current.barEvidence.bottomRevalidatedAtReference=false;
        auto live=Resolve({current});
        live.continuityGeneration=9;
        const auto resumed=tracker.Consume(live,250,1000.0/24.0,
            &first.identity,true);
        Assert::IsTrue(resumed.detected);
        Assert::IsFalse(resumed.held);
        Assert::AreEqual(acquired.cue,resumed.cue);
        Assert::IsTrue(std::string(tracker.DecisionReason())==
            "backlog-current-cue-reassociated");
    }
    TEST_METHOD(BacklogOnlyReassociationRejectsNewOwnedInkAndChangedGlyphs) {
        auto first=CompleteBackedCue(1);
        first.independentBarAnalysis=true;
        first.barEvidence.available=true;first.barEvidence.top=45;first.barEvidence.bottom=315;
        for(int scenario=0;scenario<3;++scenario) {
            SubtitleBoxPresentation tracker;
            const auto acquired=tracker.Consume(Resolve({first}));
            Assert::IsTrue(acquired.detected);
            auto current=first;current.identity=Identity(5);
            current.continuityGeneration=9;
            if(scenario==0) {
                auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
                const size_t pixel=size_t(340)*ink->width+480;
                ink->rawInk[pixel/64]|=uint64_t(1)<<(pixel%64);
                ink->ownedInk[pixel/64]|=uint64_t(1)<<(pixel%64);
                current.ink=ink;
            }
            if(scenario==1) {
                auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
                for(int y=318;y<332;++y)for(int x=200;x<440;++x) {
                    const size_t pixel=size_t(y)*ink->width+x;
                    ink->rawInk[pixel/64]&=~(uint64_t(1)<<(pixel%64));
                }
                current.ink=ink;
            }
            if(scenario==2)current.identity.analysisDomainKey++;
            auto preview=Resolve({current});preview.continuityGeneration=9;
            const auto result=tracker.Consume(preview,250,1000.0/24.0,
                &first.identity,true);
            Assert::IsFalse(result.detected && result.cue==acquired.cue,
                L"New ink, changed glyphs, or a changed domain cannot borrow the submitted cue.");
        }
    }
    TEST_METHOD(BackedMinorCoverageVariationRetainsCueBeyondGrace) {
        const auto initial=CompleteBackedCue(1);
        SubtitleBoxPresentation tracker;const auto acquired=tracker.Consume(Resolve({initial}));
        Assert::IsTrue(acquired.detected);
        for(int frame=2;frame<35;++frame) {
            auto current=CompleteBackedCue(frame);
            auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
            // Lose one of fourteen raster rows (7.1%) from every first-line
            // glyph. Local shape checks still pass; no new strokes appear.
            for(int x=200;x<440;++x) {
                const size_t bit=size_t(318)*640+x;
                ink->rawInk[bit/64]&=~(uint64_t(1)<<(bit%64));
            }
            current.ink=ink;
            SubtitleBoxLookahead::InkMatchDiagnostics d;
            Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(initial,current));
            Assert::IsTrue(SubtitleBoxLookahead::ConfidentSameInk(initial,current,&d));
            Assert::IsTrue(d.backingConfirmed);Assert::IsTrue(d.minimumCoverage>=90);
            const auto result=tracker.Consume(Resolve({current}),250,1000.0/60);
            Assert::IsTrue(result.detected);Assert::AreEqual(acquired.cue,result.cue);
            Assert::AreEqual(acquired.bounds.top,result.bounds.top);
            Assert::AreEqual(acquired.bounds.right,result.bounds.right);
        }
    }
    TEST_METHOD(BackedConfidenceCannotHideRemovedGlyphOrInventBacking) {
        const auto initial=CompleteBackedCue(1);auto current=CompleteBackedCue(2);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        for(int y=318;y<332;++y)for(int x=200;x<205;++x) {
            const size_t bit=size_t(y)*640+x;ink->rawInk[bit/64]&=~(uint64_t(1)<<(bit%64));
        }
        current.ink=ink;
        Assert::IsFalse(SubtitleBoxLookahead::ConfidentSameInk(initial,current));
        current=CompleteBackedCue(3);ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        ink->blackBacking.clear();
        for(int x=200;x<440;++x) {
            const size_t bit=size_t(318)*640+x;ink->rawInk[bit/64]&=~(uint64_t(1)<<(bit%64));
        }
        current.ink=ink;
        Assert::IsFalse(SubtitleBoxLookahead::ConfidentSameInk(initial,current));
    }

    TEST_METHOD(ConfirmedHeldExtensionUpdatesOnlyAnOverlappingCurrentCardCleanup) {
        for(bool overlaps:{false,true}) {
            auto first=Cue(1),lock=Cue(2);
            first.pictureBottom=lock.pictureBottom=329;
            first.text.sourcePanel=lock.text.sourcePanel={186,306,446,329};
            first.text.capturePanel=lock.text.capturePanel={190,310,442,329};
            first.text.capturePanelMeasured=lock.text.capturePanelMeasured=true;
            SubtitleBoxPresentation tracker;const auto acquired=tracker.Consume(Resolve({first,lock}));
            Assert::IsTrue(acquired.detected);
            auto current=Cue(2);current.pictureBottom=329;current.text.detected=false;
            current.text.capturePanelMeasured=true;
            current.text.capturePanel={overlaps?438:446,314,466,329};
            current.text.sourcePanel={overlaps?434:442,310,470,329};
            auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);AddNewGlyph(*ink,448,318);
            current.ink=ink;auto next=current;next.identity=Identity(3);
            const auto extended=tracker.Consume(Resolve({current,next}));
            Assert::IsTrue(extended.detected);Assert::AreEqual(acquired.cue,extended.cue);
            Assert::IsTrue(extended.bounds.right>acquired.bounds.right);
            Assert::AreEqual(overlaps?470:446,extended.sourcePanel.right,
                L"only current card proof overlapping the acquired card may enlarge held cleanup");
            Assert::IsTrue(extended.capturePanel.right>=456 && extended.capturePanel.right<470,
                L"the full cleanup fringe must not become glyph capture authority");
        }
    }
    TEST_METHOD(CurrentVerifiedTerminalGlyphIsNotClippedByFrozenCueBounds) {
        auto first=Cue(1),locked=Cue(2);
        first.text.bounds=locked.text.bounds={196,314,440,336};
        SubtitleBoxPresentation tracker;SubtitlePaddingGuard padding;SubtitleCutPastePresentation placement;
        auto preview=Resolve({first,locked});const auto acquired=tracker.Consume(preview);
        const auto margin=padding.Consume(acquired,preview,SubtitleBoxPadding(30),SubtitlePaddingPolicy{true});
        auto display=padding.DisplayBounds();
        const auto before=placement.Consume(acquired,preview,margin,0,&display);
        Assert::IsTrue(before.valid);
        auto current=Cue(2);current.text.lineBounds[0].right=444;current.text.bounds.right=448;
        auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        // A small currently measured terminal mark can stay below the separate
        // large-extension tracker's component threshold. It still belongs to
        // the detector's accepted line and must survive the frozen envelope.
        for(int y=325;y<328;++y)for(int x=441;x<444;++x) {
            const size_t pixel=size_t(y)*ink->width+x;
            ink->rawInk[pixel/64]|=uint64_t{1}<<(pixel%64);
            ink->ownedInk[pixel/64]|=uint64_t{1}<<(pixel%64);
        }
        current.ink=ink;auto next=current;next.identity=Identity(3);
        preview=Resolve({current,next});const auto tracked=tracker.Consume(preview);
        Assert::IsTrue(tracked.detected);Assert::AreEqual(acquired.cue,tracked.cue);
        Assert::AreEqual(acquired.bounds.right,tracked.bounds.right);
        const auto stableMargin=padding.Consume(tracked,preview,SubtitleBoxPadding(30),SubtitlePaddingPolicy{true});
        display=padding.DisplayBounds();const auto moved=placement.Consume(tracked,preview,stableMargin,0,&display);
        Assert::IsTrue(moved.valid);Assert::IsTrue(moved.glyphLines[0].right>=444);
        Assert::IsTrue(moved.content.right>=444,L"verified current punctuation must reach the composition mask");
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(before.destination,moved.destination,0),
            L"correcting extraction inside existing headroom must not breathe the displayed panel");
    }
    TEST_METHOD(MovingInterGlyphPicturePreservesCompleteCueDuringRegrouping) {
        auto first=Cue(1,true),second=Cue(2,true);
        first.pictureBottom=second.pictureBottom=329;
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,second}));
        Assert::IsTrue(acquired.detected);
        for(uint64_t sequence=2;sequence<62;++sequence) {
            auto current=LowerOnlyCue(sequence);
            current.pictureBottom=329;
            auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
            // Picture texture changes in every inter-glyph gap. None of these
            // short fragments is a glyph, and all acquired glyphs stay exact.
            for(int gap=0;gap<26;++gap)for(int dy=0;dy<6;++dy)for(int dx=0;dx<4;++dx) {
                const int y=318+int(sequence%2)*5+dy,x=205+gap*9+dx;
                const size_t pixel=size_t(y)*ink->width+x;
                ink->rawInk[pixel/64]|=uint64_t{1}<<(pixel%64);
            }
            current.ink=ink;
            Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(first,current));
            auto next=current;next.identity=Identity(sequence+1);
            const auto result=tracker.Consume(Resolve({current,next}));
            Assert::IsTrue(result.detected);Assert::AreEqual(acquired.cue,result.cue);
            Assert::AreEqual(2,result.lineCount);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,result.bounds,0));
        }
    }
    TEST_METHOD(RealNewSecondLineExpandsCurrentCueAndBlankFrameStopsIt) {
        auto single=Pixels();Text(single,175,307,22);
        auto doubleLine=single;Text(doubleLine,240,336,12);
        auto blank=Pixels();
        const auto first=Measure(single,1),lock=Measure(single,2);
        const auto expanded=Measure(doubleLine,2),confirmed=Measure(doubleLine,3);
        const auto gone=Measure(blank,3);
        Assert::AreEqual(1,first.text.lineCount);
        Assert::AreEqual(2,expanded.text.lineCount);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        const auto complete=tracker.Consume(Resolve({expanded,confirmed}));
        Assert::IsTrue(complete.detected);Assert::AreEqual(2,complete.lineCount);
        Assert::AreEqual(acquired.cue,complete.cue);
        Assert::IsTrue(complete.bounds.bottom>acquired.bounds.bottom);
        auto returning=Measure(doubleLine,4);
        Assert::IsFalse(tracker.Consume(Resolve({gone,returning})).detected,
            L"future return cannot justify moving text absent from current pixels");
    }
    TEST_METHOD(CurrentGlyphsRemainMovedThroughLastFiveFramesBeforeDifferentCue) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        auto changed=Pixels();Text(changed,175,307,22,true);Text(changed,240,336,12,true);
        SubtitleBoxPresentation tracker;SubtitleCutPastePresentation placement;
        const auto first=Measure(pixels,1),lock=Measure(pixels,2);
        const auto acquiredPreview=Resolve({first,lock});
        const auto acquired=tracker.Consume(acquiredPreview);
        const auto originalPlacement=placement.Consume(acquired,acquiredPreview);
        Assert::IsTrue(originalPlacement.valid);
        for(uint64_t sequence=2;sequence<=6;++sequence) {
            auto current=Measure(pixels,sequence);current.text={};
            // The queue has already crossed the subtitle boundary. There is
            // no future old grouping available to authorize a timed bridge.
            auto next=Measure(changed,sequence+1);
            const auto preview=Resolve({current,next});
            const auto result=tracker.Consume(preview);
            const auto moved=placement.Consume(result,preview);
            Assert::IsTrue(result.detected && result.held && moved.valid);
            Assert::AreEqual(acquired.cue,result.cue);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,result.bounds,0));
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(originalPlacement.destination,moved.destination,0));
        }
        auto next=Measure(changed,7),confirmed=Measure(changed,8);
        const auto replacement=tracker.Consume(Resolve({next,confirmed}));
        Assert::IsTrue(replacement.detected);Assert::IsTrue(replacement.cue!=acquired.cue);
        auto blank=Pixels();
        Assert::IsFalse(tracker.Consume(Resolve({Measure(blank,8),Measure(changed,9)})).detected);
    }
    TEST_METHOD(SegmentationMissWithNewSecondLineCannotReuseIncompleteOldCue) {
        auto single=Pixels();Text(single,175,307,22);
        auto both=single;Text(both,240,336,12);
        const auto first=Measure(single,1),lock=Measure(single,2);
        auto current=Measure(both,2);current.text={};
        SubtitleBoxRect novelLine;
        Assert::IsTrue(SubtitleBoxLookahead::FindNewInkExtension(*first.ink,*current.ink,
            first.text.lineBounds[0],first.width,first.height,novelLine),
            L"the current second line must be recognized as an unconfirmed glyph extension");
        SubtitleBoxPresentation tracker;
        Assert::IsTrue(tracker.Consume(Resolve({first,lock})).detected);
        auto oldReturns=Measure(single,3);
        Assert::IsFalse(tracker.Consume(Resolve({current,oldReturns})).detected,
            L"new current glyphs outside the reference require complete reacquisition");
    }
    TEST_METHOD(TotalSegmentationMissCanExpandEstablishedCueOnlyWithConfirmedCurrentInk) {
        auto single=Pixels();Text(single,175,307,22);
        auto both=single;Text(both,240,336,12);
        const auto first=Measure(single,1),lock=Measure(single,2);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        Assert::IsTrue(acquired.detected);
        auto missed=Measure(both,2);missed.text={};
        auto next1=Measure(both,3),next2=Measure(both,4),next3=Measure(both,5);
        next1.text={};next2.text={};next3.text={};
        const auto expanded=tracker.Consume(Resolve({missed,next1,next2,next3}));
        Assert::IsTrue(expanded.detected,
            L"current and queued glyph pixels can complete an already acquired cue through a grouping miss");
        Assert::AreEqual(acquired.cue,expanded.cue);
        Assert::IsTrue(expanded.bounds.bottom>acquired.bounds.bottom);
        const auto geometry=SubtitleCutPastePresentation{}.Consume(expanded,Resolve({missed,next1,next2,next3}));
        Assert::IsTrue(geometry.valid);Assert::IsTrue(geometry.content.bottom>=350,
            L"a confirmed second row must reach extraction, not only the displayed bounds");
        Assert::IsTrue(geometry.glyphLines[1].Valid());
        const auto retained=tracker.Consume(Resolve({next1,next2,next3}));
        Assert::IsTrue(retained.detected);
        Assert::AreEqual(expanded.cue,retained.cue);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(expanded.bounds,retained.bounds,0));
        auto blankPixels=Pixels();
        auto blank=Measure(blankPixels,4);blank.text={};
        Assert::IsFalse(tracker.Consume(Resolve({blank,next3})).detected,
            L"an accepted extension cannot carry the cue across a blank frame");
    }
    TEST_METHOD(TotalMissExtensionNeedsFullProofAndCannotCrossContextOrChangedText) {
        auto single=Pixels();Text(single,175,307,22);
        auto both=single;Text(both,240,336,12);
        const auto first=Measure(single,1),lock=Measure(single,2);
        auto missed=Measure(both,2);missed.text={};
        auto next1=Measure(both,3),next2=Measure(both,4),next3=Measure(both,5);
        next1.text={};next2.text={};next3.text={};
        auto checkRejected=[&](SubtitleBoxObservation current,
            std::initializer_list<SubtitleBoxObservation> following) {
            SubtitleBoxPresentation tracker;
            Assert::IsTrue(tracker.Consume(Resolve({first,lock})).detected);
            std::vector<SubtitleBoxObservation> frames{current};
            frames.insert(frames.end(),following.begin(),following.end());
            const auto preview=SubtitleBoxLookahead::Resolve(frames.data(),frames.size(),7,8);
            Assert::IsFalse(tracker.Consume(preview).detected);
        };
        checkRejected(missed,{next1,next2});
        auto vanished=Measure(single,3);
        checkRejected(missed,{vanished,next2,next3});
        auto workLimited=missed;workLimited.text.workLimit=true;
        checkRejected(workLimited,{next1,next2,next3});
        auto noBar=missed;noBar.barAuthority=false;noBar.barTrackingAuthority=false;
        checkRejected(noBar,{next1,next2,next3});
        auto gap=missed;gap.identity=Identity(3);
        checkRejected(gap,{next1,next2,next3});
        auto changed=Pixels();Text(changed,175,307,22,true);Text(changed,240,336,12);
        auto changedMiss=Measure(changed,2);changedMiss.text={};
        checkRejected(changedMiss,{next1,next2,next3});
        auto blankPixels=Pixels();auto blank=Measure(blankPixels,2);blank.text={};
        checkRejected(blank,{next1,next2,next3});
    }
    TEST_METHOD(WeakCurrentGlyphCoverageKeepsEstablishedBoxAcrossTenFrames) {
        const auto first=Cue(1,true),lock=Cue(2,true);
        auto weakened=[](uint64_t sequence,bool partial) {
            auto current=Cue(sequence,true);
            if(partial) {
                current.text.lineCount=1;
                current.text.bounds=current.text.lineBounds[0];
            } else current.text={};
            auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
            for(int y=0;y<ink->height;++y)for(int x=0;x<ink->width;++x) {
                if(!ink->Get(x,y,true) || (x+y+int(sequence)%3)%16!=0)continue;
                const size_t bit=size_t(y)*ink->width+x;
                ink->rawInk[bit/64]&=~(uint64_t{1}<<(bit%64));
            }
            current.ink=ink;
            return current;
        };
        auto sample=weakened(2,false);
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(first,sample));
        Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(first,sample,90));
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        Assert::IsTrue(acquired.detected);
        for(uint64_t sequence=2;sequence<12;++sequence) {
            auto current=weakened(sequence,sequence%2==0);
            auto following=weakened(sequence+1,(sequence+1)%2==0);
            const auto held=tracker.Consume(Resolve({current,following}),250,20.0);
            Assert::IsTrue(held.detected && held.held,
                L"each distinct source frame must still supply positive per-line glyph and bar pixels");
            Assert::AreEqual(acquired.cue,held.cue);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,held.bounds,0));
        }
        for(uint64_t sequence=12;sequence<=13;++sequence) {
            auto current=weakened(sequence,false);
            auto following=weakened(sequence+1,false);
            const auto held=tracker.Consume(Resolve({current,following}),250,20.0);
            Assert::IsTrue(held.detected && held.held,
                L"the hysteresis margin must also cover 240 ms of the 250 ms grace period");
            Assert::AreEqual(acquired.cue,held.cue);
        }
        auto exhausted=weakened(14,false);
        const auto released=tracker.Consume(Resolve({exhausted}),250,20.0);
        Assert::IsFalse(released.detected,
            L"weak evidence alone must not preserve a stale subtitle indefinitely");
        auto missingUpper=Cue(12,true);missingUpper.text={};
        auto ink=std::make_shared<SubtitleInkSnapshot>(*missingUpper.ink);
        const auto& upper=first.text.lineBounds[0];
        for(int y=upper.top;y<upper.bottom;++y)for(int x=upper.left;x<upper.right;++x) {
            const size_t bit=size_t(y)*ink->width+x;
            ink->rawInk[bit/64]&=~(uint64_t{1}<<(bit%64));
        }
        missingUpper.ink=ink;
        Assert::IsFalse(tracker.Consume(Resolve({missingUpper,Cue(13,true)})).detected,
            L"an absent learned row cannot use the unchanged companion to extend the hold");
    }
    TEST_METHOD(BlackBackingAllowsPartialInkButNotBlankRowsOrNewStrokes) {
        auto reference=Cue(1,true);
        auto original=std::make_shared<SubtitleInkSnapshot>(*reference.ink);
        original->blackBacking.resize(original->rawInk.size());
        for(size_t i=0;i<original->rawInk.size();++i)original->blackBacking[i]=~original->rawInk[i];
        reference.ink=original;
        auto weaken=[&](uint64_t sequence,int divisor) {
            auto current=reference;current.identity=Identity(sequence);current.text={};
            auto ink=std::make_shared<SubtitleInkSnapshot>(*original);
            for(int y=0;y<ink->height;++y)for(int x=0;x<ink->width;++x)
                if(ink->Get(x,y,true) && (x+y)%divisor==0) {
                    const size_t bit=size_t(y)*ink->width+x;
                    ink->rawInk[bit/64]&=~(uint64_t{1}<<(bit%64));
                }
            current.ink=ink;return current;
        };
        auto weak=weaken(2,4);
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(reference,weak,90));
        Assert::IsTrue(SubtitleBoxLookahead::BackedPartialInkAtReference(reference,weak));
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({reference}));Assert::IsTrue(acquired.detected);
        for(int frame=2;frame<=16;++frame) {
            const auto held=tracker.Consume(Resolve({weaken(frame,4)}));
            Assert::IsTrue(held.detected && held.held);
            Assert::AreEqual(acquired.cue,held.cue);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,held.bounds,0));
            Assert::AreEqual(std::string("grace-black-panel-partial-ink"),std::string(tracker.DecisionReason()));
        }
        Assert::IsFalse(tracker.Consume(Resolve({weaken(17,4)})).detected);
        auto missing=weak;auto missingInk=std::make_shared<SubtitleInkSnapshot>(*weak.ink);
        for(int y=318;y<332;++y)for(int x=200;x<440;++x) {
            const size_t bit=size_t(y)*640+x;missingInk->rawInk[bit/64]&=~(uint64_t{1}<<(bit%64));
        }
        missing.ink=missingInk;
        Assert::IsFalse(SubtitleBoxLookahead::BackedPartialInkAtReference(reference,missing),
            L"unchanged backing and the remaining line cannot preserve a missing line");
        auto bright=weak;auto brightInk=std::make_shared<SubtitleInkSnapshot>(*weak.ink);
        std::fill(brightInk->blackBacking.begin(),brightInk->blackBacking.end(),0);
        bright.ink=brightInk;
        Assert::IsFalse(SubtitleBoxLookahead::BackedPartialInkAtReference(reference,bright));
        auto replacement=weak;auto changedInk=std::make_shared<SubtitleInkSnapshot>(*weak.ink);
        // Fill the gaps inside a learned glyph envelope: no new outer box is needed.
        for(int y=318;y<332;++y)for(int x=204;x<209;++x) {
            const size_t bit=size_t(y)*640+x;changedInk->rawInk[bit/64]|=uint64_t{1}<<(bit%64);
        }
        replacement.ink=changedInk;
        Assert::IsFalse(SubtitleBoxLookahead::BackedPartialInkAtReference(reference,replacement));
        auto third=weaken(2,3);
        Assert::IsFalse(SubtitleBoxLookahead::BackedPartialInkAtReference(reference,third));
        reference.text.sourcePanel={190,300,450,315};third.text=reference.text;
        Assert::IsTrue(SubtitleBoxLookahead::BackedPartialInkAtReference(reference,third),
            L"a matching freshly measured panel permits 60 percent coverage per line");
    }
    TEST_METHOD(BackedTrackingToleratesOneMissingGlyphAndConfirmedNewCueOverrides) {
        auto reference=Cue(1,true);
        auto old=std::make_shared<SubtitleInkSnapshot>(*reference.ink);
        old->blackBacking.resize(old->rawInk.size());
        for(size_t i=0;i<old->rawInk.size();++i)old->blackBacking[i]=~old->rawInk[i];
        reference.ink=old;
        auto partial=reference;partial.identity=Identity(2);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*old);
        for(int y=318;y<332;++y)for(int x=200;x<205;++x) {
            const size_t bit=size_t(y)*640+x;ink->rawInk[bit/64]&=~(uint64_t{1}<<(bit%64));
        }
        partial.ink=ink;
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(reference,partial,90));
        Assert::IsTrue(SubtitleBoxLookahead::BackedPartialInkAtReference(reference,partial));
        SubtitleBoxPresentation tracker;
        const auto first=tracker.Consume(Resolve({reference}));
        auto changed=partial;changed.text.lineSignatures[0].fill(0xff);
        auto following=changed;following.identity=Identity(3);
        const auto next=tracker.Consume(Resolve({changed,following}));
        Assert::IsTrue(next.detected && !next.held);Assert::IsTrue(next.cue!=first.cue);
    }
    TEST_METHOD(DetectorPublishesIndependentBlackBackingOnGroupingMiss) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        const auto first=Measure(pixels,1);
        auto current=Measure(pixels,2);current.text={};
        Assert::IsTrue(first.text.detected);
        Assert::IsTrue(SubtitleBoxLookahead::StableBlackBacking(first,current));
        Assert::IsTrue(SubtitleBoxLookahead::BackedPartialInkAtReference(first,current));
    }
    TEST_METHOD(StabilityTelemetryIdentifiesBriefSameCueRecoveryWithoutRepeatNoise) {
        SubtitleStabilityTelemetry telemetry;
        auto a=Cue(1,true);auto result=a.text;result.cue=1;
        Assert::IsTrue(telemetry.Observe(a,result,true,20).changed);
        Assert::IsFalse(telemetry.Observe(a,result,true,20).changed);
        auto miss=Cue(2,true);miss.text={};
        auto held=result;held.held=true;
        const auto detectorDrop=telemetry.Observe(miss,held,true,20);
        Assert::IsTrue(detectorDrop.detectorLost && detectorDrop.holdStarted);
        Assert::IsFalse(detectorDrop.outputLost);
        miss.identity=Identity(3);
        Assert::IsTrue(telemetry.Observe(miss,{},false,20).outputLost);
        const auto recovered=telemetry.Observe(Cue(4,true),result,true,20);
        Assert::IsTrue(recovered.outputRegained && recovered.detectorRegained && recovered.suspectedFlash);
        Assert::AreEqual(20.0,recovered.gapMs);
        Assert::AreEqual(uint64_t{1},recovered.flashes);
        miss.identity=Identity(5);telemetry.Observe(miss,{},false,20);
        auto reset=Cue(6,true);reset.discontinuity=true;
        Assert::IsFalse(telemetry.Observe(reset,result,true,20).suspectedFlash);
    }
    TEST_METHOD(ResourceLimitHoldsOnlyCompleteCurrentBackedInkForConfiguredSourceTime) {
        for(const double rate:{24.0,60.0})for(const int hold:{0,250,500}) {
            SubtitleBoxPresentation tracker;
            const auto acquired=tracker.Consume(Resolve({CompleteBackedCue(1),CompleteBackedCue(2)}),hold,1000.0/rate);
            Assert::IsTrue(acquired.detected);
            const int allowed=int(hold*rate/1000.0+0.000001);
            for(int frame=1;frame<=allowed+1;++frame) {
                auto limited=CompleteBackedCue(frame+1);limited.text={};limited.text.workLimit=true;
                const auto preview=Resolve({limited});
                const auto result=tracker.Consume(preview,hold,1000.0/rate);
                Assert::AreEqual(frame<=allowed,result.detected);
                if(result.detected) {
                    Assert::IsTrue(result.held);Assert::AreEqual(acquired.cue,result.cue);
                    Assert::AreEqual(std::string("grace-resource-limit-current-ink"),std::string(tracker.DecisionReason()));
                    for(int repeat=0;repeat<5;++repeat)
                        Assert::AreEqual(result.cue,tracker.Consume(preview,hold,1000.0/rate).cue);
                }
            }
        }
    }
    TEST_METHOD(ResourceLimitCannotAcquireOrUseIncompleteStaleChangedOrUnbackedInk) {
        for(int damage=0;damage<10;++damage) {
            auto first=CompleteBackedCue(1),limited=CompleteBackedCue(2);
            SubtitleBoxPresentation tracker;
            Assert::IsTrue(tracker.Consume(Resolve({first,CompleteBackedCue(2)})).detected);
            auto ink=std::make_shared<SubtitleInkSnapshot>(*limited.ink);
            limited.text.workLimit=true; // even a partial positive grouping cannot acquire
            if(damage==0)ink->rawEvidenceComplete=false;
            if(damage==1)std::fill(ink->rawInk.begin(),ink->rawInk.end(),0);
            if(damage==2)std::fill(ink->blackBacking.begin(),ink->blackBacking.end(),0);
            if(damage==3)limited.analysisRefresh=true;
            if(damage==4)limited.pendingRefresh=true;
            if(damage==5)AddNewGlyph(*ink,445,318);
            if(damage==6) {AddNewGlyph(*ink,290,297);AddNewGlyph(*ink,303,297);}
            if(damage==7)limited.identity.transportGeneration++;
            if(damage==8)ink->rawInk.pop_back();
            limited.ink=damage==9?first.ink:ink;
            Assert::IsFalse(tracker.Consume(Resolve({limited})).detected);
            SubtitleBoxPresentation onset;
            Assert::IsFalse(onset.Consume(Resolve({limited})).detected);
        }
    }
    TEST_METHOD(ResourceLimitLocalWordOrCompanionLossCannotBorrowQueuedRestoration) {
        for(bool entireLine:{false,true}) {
            auto first=CompleteBackedCue(1),limited=CompleteBackedCue(2);
            SubtitleBoxPresentation tracker;
            Assert::IsTrue(tracker.Consume(Resolve({first,CompleteBackedCue(2)})).detected);
            auto ink=std::make_shared<SubtitleInkSnapshot>(*limited.ink);
            for(int y=318;y<332;++y)for(int x=200;x<(entireLine?440:203);++x) {
                const size_t p=size_t(y)*ink->width+x;
                ink->rawInk[p/64]&=~(uint64_t(1)<<(p%64));
            }
            limited.ink=ink;limited.text={};limited.text.workLimit=true;
            Assert::IsFalse(tracker.Consume(Resolve({limited,CompleteBackedCue(3)})).detected,
                L"current local text change or missing companion overrides a queued identical return");
        }
    }
    TEST_METHOD(ResourceLimitBriefRecoveryRetainsCueAndFreshDetectionRenewsGrace) {
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({CompleteBackedCue(1),CompleteBackedCue(2)}));
        for(uint64_t sequence=2;sequence<20;++sequence) {
            auto current=CompleteBackedCue(sequence);
            if(sequence%3) {current.text={};current.text.workLimit=true;}
            const auto result=tracker.Consume(Resolve({current}),250,1000.0/24);
            Assert::IsTrue(result.detected);Assert::AreEqual(acquired.cue,result.cue);
            Assert::AreEqual(sequence%3!=0,result.held);
        }
    }
    TEST_METHOD(GraceDurationUsesSourceCadenceAndRepeatedFramesDoNotRenewIt) {
        for(const double rate:{24.0,30.0,60.0,120.0}) for(const int hold:{0,250,500}) {
            SubtitleBoxPresentation tracker;
            const auto reference=Cue(1,true);
            const auto acquired=tracker.Consume(Resolve({reference,Cue(2,true)}),hold,1000.0/rate);
            Assert::IsTrue(acquired.detected);
            const int allowed=int(hold*rate/1000.0+0.000001);
            for(int frame=1;frame<=allowed+1;++frame) {
                auto weak=Cue(frame+1,true);weak.text={};
                auto ink=std::make_shared<SubtitleInkSnapshot>(*weak.ink);
                for(int y=0;y<ink->height;++y)for(int x=0;x<ink->width;++x)
                    if(ink->Get(x,y,true) && (x+y)%16==0) {
                        const size_t bit=size_t(y)*ink->width+x;
                        ink->rawInk[bit/64]&=~(uint64_t{1}<<(bit%64));
                    }
                weak.ink=ink;
                const auto preview=Resolve({weak});
                const auto result=tracker.Consume(preview,hold,1000.0/rate);
                Assert::AreEqual(frame<=allowed,result.detected);
                if(result.detected) {
                    Assert::AreEqual(acquired.cue,result.cue);
                    for(int repeat=0;repeat<5;++repeat)
                        Assert::AreEqual(result.cue,tracker.Consume(preview,hold,1000.0/rate).cue);
                }
            }
        }
    }
    TEST_METHOD(ConfirmedLargeNewRowCanExpandBeyondNinetyPercentOfRaster) {
        auto reference=Cue(1,false);
        auto current=std::make_shared<SubtitleInkSnapshot>(*reference.ink);
        // A large-font upper row spans 94% of the raster. Its individual
        // glyph components are modest; only the full row exceeds the former
        // 90% extension cap.
        for(int glyph=0;glyph<38;++glyph)
            AddNewGlyph(*current,10+glyph*16,292);
        SubtitleBoxRect extension;
        Assert::IsTrue(SubtitleBoxLookahead::FindNewInkExtension(*reference.ink,*current,
            reference.text.lineBounds[0],reference.width,reference.height,extension),
            L"a plausible centered large-font row below the detector's 98% cap must be discoverable");
        const std::wstring evidence=L"extension="+std::to_wstring(extension.left)+L","+
            std::to_wstring(extension.top)+L".."+std::to_wstring(extension.right)+L","+
            std::to_wstring(extension.bottom);
        Assert::IsTrue(extension.left<=10 && extension.right>=610 && extension.top<=292 && extension.bottom>=306,
            evidence.c_str());
    }
    TEST_METHOD(WeakTemplateCannotHideNewInkOrChangedDetectedRow) {
        const auto first=Cue(1,true),lock=Cue(2,true);
        auto weak=Cue(2,true);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*weak.ink);
        for(int y=0;y<ink->height;++y)for(int x=0;x<ink->width;++x)
            if(ink->Get(x,y,true) && (x+y)%16==0) {
                const size_t bit=size_t(y)*ink->width+x;
                ink->rawInk[bit/64]&=~(uint64_t{1}<<(bit%64));
            }
        weak.ink=ink;
        Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(first,weak,90));
        auto checkRejected=[&](SubtitleBoxObservation current) {
            SubtitleBoxPresentation tracker;
            Assert::IsTrue(tracker.Consume(Resolve({first,lock})).detected);
            Assert::IsFalse(tracker.Consume(Resolve({current})).detected);
        };
        auto changedLine=weak;
        changedLine.text.lineSignatures[0].fill(0xaa);
        {
            SubtitleBoxPresentation tracker;
            const auto acquired=tracker.Consume(Resolve({first,lock}));
            const auto changed=tracker.Consume(Resolve({changedLine}));
            Assert::IsTrue(!changed.detected || changed.cue!=acquired.cue,
                L"a detected changed row cannot borrow the old cue identity");
        }
        auto newInk=weak;newInk.text={};
        auto extendedInk=std::make_shared<SubtitleInkSnapshot>(*ink);
        for(int glyph=0;glyph<3;++glyph)AddNewGlyph(*extendedInk,455+glyph*12,318);
        newInk.ink=extendedInk;
        checkRejected(newInk);
        auto noBar=weak;noBar.text={};noBar.barAuthority=false;
        checkRejected(noBar);
        auto gap=weak;gap.text={};gap.identity=Identity(3);
        checkRejected(gap);
        auto workLimit=weak;workLimit.text={};workLimit.text.workLimit=true;
        checkRejected(workLimit);
    }
    TEST_METHOD(UnchangedGroupingStillDiscoversAndTracksAnActualNewSecondLine) {
        auto single=Pixels();Text(single,175,307,22);
        auto both=single;Text(both,240,336,12);
        const auto first=Measure(single,1),lock=Measure(single,2);
        auto current=Measure(both,2),next=Measure(both,3);
        const auto completeBounds=current.text.bounds;
        // Simulate repeated detector under-grouping while raw current pixels
        // contain a newly appeared second line on every frame.
        current.text=first.text;next.text=first.text;
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        auto next2=next;next2.identity=Identity(4);next2.text=first.text;
        auto next3=next;next3.identity=Identity(5);next3.text=first.text;
        const auto expanded=tracker.Consume(Resolve({current,next,next2,next3}));
        Assert::IsTrue(expanded.detected);Assert::AreEqual(acquired.cue,expanded.cue);
        Assert::IsTrue(expanded.bounds.bottom>=completeBounds.bottom-4);
        Assert::IsTrue(expanded.bounds.bottom>acquired.bounds.bottom);
        const auto retained=tracker.Consume(Resolve({next}));
        Assert::IsTrue(retained.detected);Assert::AreEqual(expanded.cue,retained.cue);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(expanded.bounds,retained.bounds,0));
        auto removed=Measure(single,4),stillRemoved=Measure(single,5);
        const auto shortened=tracker.Consume(Resolve({removed,stillRemoved}));
        Assert::IsTrue(shortened.detected);Assert::IsTrue(shortened.cue!=expanded.cue);
        Assert::IsTrue(shortened.bounds.bottom<expanded.bounds.bottom);
        SubtitleBoxPresentation unconfirmed;
        unconfirmed.Consume(Resolve({first,lock}));
        const auto awaiting=unconfirmed.Consume(Resolve({current,next}));
        Assert::IsTrue(awaiting.detected);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,awaiting.bounds,0),
            L"unconfirmed scene candidates cannot resize or revoke the validated current cue");
    }
    TEST_METHOD(NormalDetectedLocalGlyphChangeCannotReuseCoarseCueIdentity) {
        auto before=Pixels();Text(before,175,307,22);Text(before,240,336,12);
        auto changed=before;Text(changed,175+13*10,307,1,true);
        const auto first=Measure(before,1),lock=Measure(before,2);
        const auto current=Measure(changed,2),next=Measure(changed,3);
        Assert::IsTrue(current.text.detected);
        Assert::IsTrue(SubtitleBoxLookahead::SameCue(first,current),
            L"one changed letter can fit the coarse whole-line signature tolerance");
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(first,current));
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        const auto replacement=tracker.Consume(Resolve({current,next}));
        Assert::IsTrue(replacement.detected);Assert::IsTrue(replacement.cue!=acquired.cue);
    }
    TEST_METHOD(OneSampleStemThickeningIsNotANewDetachedCharacter) {
        auto first=Cue(1,true),current=LowerOnlyCue(2);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        // Captured antialiasing can add a one-sample column alongside an
        // unchanged stem. The original stem remains present at every row.
        for(int y=320;y<329;++y) {
            const size_t pixel=size_t(y)*ink->width+205;
            ink->rawInk[pixel/64]|=uint64_t{1}<<(pixel%64);
        }
        current.ink=ink;
        Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(first,current));
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,Cue(2,true)}));
        const auto held=tracker.Consume(Resolve({current}));
        Assert::IsTrue(held.detected);Assert::AreEqual(acquired.cue,held.cue);
    }
    TEST_METHOD(IsolatedStableSceneComponentsCannotInventALineOrDistantWord) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        auto current=Cue(2,true),next=Cue(3,true);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        AddNewGlyph(*ink,310,292); // One centered bright shape above the row.
        AddNewGlyph(*ink,100,318); // Baseline aligned, implausibly distant.
        current.ink=ink;next.ink=ink;
        const auto result=tracker.Consume(Resolve({current,next}));
        Assert::IsTrue(result.detected);Assert::AreEqual(acquired.cue,result.cue);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,result.bounds,0));
        auto extendedInk=std::make_shared<SubtitleInkSnapshot>(*ink);
        AddNewGlyph(*extendedInk,448,318); // A legitimate nearby extension.
        current.ink=extendedInk;next.ink=extendedInk;
        current.identity=Identity(3);next.identity=Identity(4);
        const auto extended=tracker.Consume(Resolve({current,next}));
        Assert::IsTrue(extended.detected);Assert::IsTrue(extended.bounds.right>acquired.bounds.right);
        Assert::AreEqual(acquired.bounds.left,extended.bounds.left,
            L"a valid nearby extension cannot lend support to a disconnected distant scene component");
    }
    TEST_METHOD(NewGlyphInsideOldLineWhitespaceRejectsCachedIdentity) {
        auto first=Cue(1,true),current=LowerOnlyCue(2);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        // A new full-height narrow character occupies an old gap. It lies
        // outside all acquired component envelopes but inside the old line.
        for(int y=318;y<332;++y)for(int x=206;x<208;++x) {
            const size_t pixel=size_t(y)*ink->width+x;
            ink->rawInk[pixel/64]|=uint64_t{1}<<(pixel%64);
        }
        current.ink=ink;
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(first,current));
        SubtitleBoxPresentation tracker;
        Assert::IsTrue(tracker.Consume(Resolve({first,Cue(2,true)})).detected);
        current.text={};
        Assert::IsFalse(tracker.Consume(Resolve({current,Cue(3,true)})).detected);
    }
    TEST_METHOD(NewStrokeInsideAcquiredGlyphHoleRejectsCachedIdentity) {
        auto before=Pixels();Text(before,175,307,22);Text(before,240,336,12);
        auto after=before;
        // Fill one letter's previously empty interior without removing any
        // old stroke. Coverage alone would incorrectly accept this edit.
        Fill(after,175+13*10+2,309,175+13*10+6,319,700);
        const auto reference=Measure(before,1),current=Measure(after,2);
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(reference,current));
    }
    TEST_METHOD(FreshAcceptedCueAfterBridgeDoesNotRetainHeldDiagnosticFlag) {
        SubtitleBoxPresentation tracker;
        const auto first=tracker.Consume(Resolve({Cue(1),Cue(2)}));
        auto miss=Cue(2);miss.text={};
        const auto held=tracker.Consume(Resolve({miss,Cue(3)}));
        Assert::IsTrue(held.detected && held.held);
        const auto fresh=tracker.Consume(Resolve({Cue(3,true),Cue(4)}));
        Assert::IsTrue(fresh.detected);Assert::IsFalse(fresh.held);
        Assert::AreEqual(first.cue,fresh.cue);Assert::AreEqual(1,fresh.lineCount);
    }
    TEST_METHOD(OneSegmentationMissBridgesOnlyWithCurrentInkAndQueuedReturn) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        const auto first=Measure(pixels,1),second=Measure(pixels,2),third=Measure(pixels,3);
        Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(first,second));
        auto miss=second;miss.text={};
        SubtitleBoxPresentation tracker;const auto acquired=tracker.Consume(Resolve({first,second}));
        const auto held=tracker.Consume(Resolve({miss,third}));
        Assert::IsTrue(held.detected && held.held);Assert::AreEqual(acquired.cue,held.cue);
        Assert::AreEqual(acquired.observations,held.observations,L"rescue is not segmentation confirmation");
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,held.bounds,0));
        const auto returned=tracker.Consume(Resolve({third}));
        Assert::IsTrue(returned.detected && !returned.held);Assert::AreEqual(acquired.cue,returned.cue);
    }
    TEST_METHOD(ExactCurrentInkSurvivesLookaheadCueBoundaryButConfirmedChangedTextDoesNot) {
        auto oldPixels=Pixels();Text(oldPixels,175,307,22);Text(oldPixels,240,336,12);
        const auto first=Measure(oldPixels,1),lockedFrame=Measure(oldPixels,2),
            current=Measure(oldPixels,3);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lockedFrame}));
        Assert::IsTrue(acquired.detected);
        Assert::AreEqual(acquired.cue,tracker.Consume(Resolve({lockedFrame})).cue);

        auto changedPixels=Pixels();Text(changedPixels,175,307,22,true);Text(changedPixels,240,336,12);
        const auto changed=Measure(changedPixels,4),changedNext=Measure(changedPixels,5);
        const auto boundary=Resolve({current,changed});
        Assert::AreEqual(1u,boundary.matchingFrames,L"the next frame starts a different cue");
        Assert::IsFalse(boundary.currentLinesConfirmed);
        const auto retained=tracker.Consume(boundary);
        Assert::IsTrue(retained.detected,L"exact current pixels still prove the established cue");
        Assert::AreEqual(acquired.cue,retained.cue);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,retained.bounds,0));

        const auto newCue=tracker.Consume(Resolve({changed,changedNext}));
        Assert::IsTrue(newCue.detected,L"two frames confirm the changed text");
        Assert::IsTrue(newCue.cue!=acquired.cue,L"changed fixed-coordinate glyphs break the old cue");
    }
    TEST_METHOD(ExactCurrentGlyphValidationOutlivesSegmentationBudgetWithoutRepeatConfirmation) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        auto a=Measure(pixels,1),b=Measure(pixels,2),c=Measure(pixels,3),d=Measure(pixels,4),e=Measure(pixels,5);
        SubtitleBoxPresentation tracker;const auto acquired=tracker.Consume(Resolve({a,b}));
        b.text={};c.text={};
        const auto one=tracker.Consume(Resolve({b,c,d}));
        Assert::IsTrue(one.detected && one.held);
        const auto repeat=tracker.Consume(Resolve({b,c,d}));
        Assert::AreEqual(one.cue,repeat.cue);Assert::AreEqual(one.observations,repeat.observations);
        const auto two=tracker.Consume(Resolve({c,d}));Assert::IsTrue(two.detected && two.held);
        d.text={};
        const auto third=tracker.Consume(Resolve({d,e}));
        Assert::IsTrue(third.detected && third.held,
            L"fresh exact current glyph proof does not expire with the segmentation bridge budget");
        Assert::AreEqual(acquired.cue,third.cue);
        Assert::AreEqual(acquired.observations,third.observations);
        Assert::IsTrue(acquired.detected);
    }
    TEST_METHOD(UnconfirmedExpandedGroupingMayBridgeButConfirmedNewTextWins) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        const auto a=Measure(pixels,1),b=Measure(pixels,2),c=Measure(pixels,3);
        SubtitleBoxPresentation tracker;const auto old=tracker.Consume(Resolve({a,b}));
        auto noisy=b;noisy.text.bounds.left-=20;noisy.text.lineBounds[0].left-=20;
        const auto held=tracker.Consume(Resolve({noisy,c}));
        Assert::IsTrue(held.detected && held.held);Assert::AreEqual(old.cue,held.cue);
        auto changed=Pixels();Text(changed,175,307,22,true);Text(changed,240,336,12);
        const auto n3=Measure(changed,3),n4=Measure(changed,4);
        const auto next=tracker.Consume(Resolve({n3,n4}));
        Assert::IsTrue(next.detected && !next.held);Assert::IsTrue(next.cue!=old.cue);
    }
    TEST_METHOD(BlankCueEndCannotBridgeEvenWhenIdenticalTextReturnsNextFrame) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        auto blank=Pixels();SubtitleBoxPresentation tracker;
        const auto a=Measure(pixels,1),b=Measure(pixels,2),returned=Measure(pixels,3);
        Assert::IsTrue(tracker.Consume(Resolve({a,b})).detected);
        Assert::IsFalse(tracker.Consume(Resolve({Measure(blank,2),returned})).detected);
        SubtitleBoxPresentation fresh;auto miss=b;miss.text={};
        Assert::IsFalse(fresh.Consume(Resolve({miss,returned})).detected,L"rescue cannot start a cue");
    }
    TEST_METHOD(EstablishedTwoLineGlyphTemplateSurvivesScenePixelsAndOneLineRegrouping) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto accepted=tracker.Consume(Resolve({first,lock}));
        Assert::IsTrue(accepted.detected);Assert::AreEqual(2,accepted.lineCount);
        auto noisy=LowerOnlyCue(3);
        auto changed=std::make_shared<SubtitleInkSnapshot>(*noisy.ink);
        // Current picture texture changes inside the upper line envelope, while
        // both acquired rows remain present in the raw glyph mask.
        const auto& upper=first.text.lineBounds[0];
        for(int y=upper.top;y<upper.bottom;++y)for(int x=upper.left;x<upper.right;++x)
            if((x+y)%40==0 && !changed->Get(x,y)) {
                const size_t p=size_t(y)*changed->width+x;
                changed->rawInk[p/64]|=uint64_t{1}<<(p%64);
            }
        noisy.ink=changed;
        const auto established=tracker.Consume(Resolve({lock,noisy}));
        Assert::IsTrue(established.detected);Assert::AreEqual(2,established.lineCount);
        const auto held=tracker.Consume(Resolve({noisy}));
        Assert::IsTrue(held.detected && held.held);
        Assert::AreEqual(2,held.lineCount);
        Assert::AreEqual(established.cue,held.cue);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(established.bounds,held.bounds,0));
    }
    TEST_METHOD(AlternatingDetectorBoundsCannotFlashAnUnchangedSubtitleByFiftyPixels) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;SubtitleCutPastePresentation placement;
        const auto firstPreview=Resolve({first,lock});
        const auto acquired=tracker.Consume(firstPreview);
        const auto originalMove=placement.Consume(acquired,firstPreview);
        Assert::IsTrue(acquired.detected && originalMove.valid);
        const auto lockPreview=Resolve({lock});
        const auto locked=tracker.Consume(lockPreview);
        const auto lockedMove=placement.Consume(locked,lockPreview);
        Assert::AreEqual(acquired.cue,locked.cue);
        Assert::IsTrue(lockedMove.valid);

        // The OCR-free grouping estimate alternates by 10px between frames,
        // which prevents its short lookahead from confirming any position.
        // The complete current source-pixel glyph mask remains unchanged.
        const int offsets[]={-50,-40,-30,-40};
        for(int i=0;i<3;++i) {
            auto current=Cue(uint64_t(3+i),true);
            auto following=Cue(uint64_t(4+i),true);
            auto shiftGrouping=[](SubtitleBoxObservation& observation,int offset) {
                observation.text.bounds.top+=offset;
                observation.text.bounds.bottom+=offset;
                for(int line=0;line<observation.text.lineCount;++line) {
                    observation.text.lineBounds[line].top+=offset;
                    observation.text.lineBounds[line].bottom+=offset;
                }
            };
            shiftGrouping(current,offsets[i]);
            shiftGrouping(following,offsets[i+1]);
            Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(first,current),
                L"fixed-coordinate glyphs in the current frame prove the same subtitle");
            Assert::IsFalse(SubtitleBoxLookahead::SameCue(first,current),
                L"the detector's shifting group estimate cannot confirm a stable position");
            const auto preview=Resolve({current,following});
            Assert::AreEqual(1u,preview.matchingFrames);
            const auto result=tracker.Consume(preview);
            const auto moved=placement.Consume(result,preview);
            Assert::IsTrue(result.detected && result.held && moved.valid);
            Assert::AreEqual(acquired.cue,result.cue);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,result.bounds,0));
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(originalMove.destination,moved.destination,0),
                L"the moved subtitle stays at one destination through alternating 50px grouping errors");
        }
    }
    static void AddWideJoinedWord(SubtitleInkSnapshot& ink,int left,int top,
        int width,int height) {
        for(int y=top;y<top+height;++y) for(int x=left;x<left+width;++x) {
            const bool pixel=(y>=top+height*2/3 && y<top+height*2/3+2) ||
                ((x-left)%18<2 && y>=top+2 && y<top+height-1);
            if(!pixel)continue;
            const size_t p=size_t(y)*ink.width+x;
            ink.rawInk[p/64]|=uint64_t{1}<<(p%64);
        }
    }
    TEST_METHOD(SolidScenePatchCannotImpersonateAnAcquiredGlyphTemplate) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto accepted=tracker.Consume(Resolve({first,lock}));
        auto scene=LowerOnlyCue(3);
        auto changed=std::make_shared<SubtitleInkSnapshot>(*scene.ink);
        const auto& upper=first.text.lineBounds[0];
        for(int y=upper.top;y<upper.bottom;++y)for(int x=upper.left;x<upper.right;++x) {
            const size_t p=size_t(y)*changed->width+x;
            changed->rawInk[p/64]|=uint64_t{1}<<(p%64);
        }
        scene.ink=changed;
        tracker.Consume(Resolve({lock,scene}));
        const auto next=tracker.Consume(Resolve({scene}));
        Assert::IsTrue(next.detected);
        Assert::IsFalse(next.held);
        Assert::IsTrue(next.cue!=accepted.cue,L"a bright block cannot keep the old cue identity");
        Assert::AreEqual(1,next.lineCount);
    }
    TEST_METHOD(ExistingAndSlightlyMovingSceneStrokeOutsideCueDoesNotInvalidateItsMask) {
        auto reference=Cue(1,true),current=Cue(2,true);
        auto oldInk=std::make_shared<SubtitleInkSnapshot>(*reference.ink);
        auto nowInk=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        for(int y=318;y<332;++y) {
            const size_t oldPixel=size_t(y)*oldInk->width+190;
            oldInk->rawInk[oldPixel/64]|=uint64_t{1}<<(oldPixel%64);
            const size_t newPixel=size_t(y+1)*nowInk->width+190;
            nowInk->rawInk[newPixel/64]|=uint64_t{1}<<(newPixel%64);
        }
        reference.ink=oldInk;current.ink=nowInk;
        Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(reference,current),
            L"a pre-existing railing stroke or one-row scene shift is not new subtitle extent");
    }
    TEST_METHOD(ConfirmedDifferentCueWinsWhenCurrentGlyphMaskChanges) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto accepted=tracker.Consume(Resolve({first,lock}));
        auto changed=Cue(3,true),confirmed=Cue(4,true);
        for(auto* cue:{&changed,&confirmed}) {
            cue->text.lineSignatures[0].fill(0xaa);
            cue->text.lineSignatures[1].fill(0x77);
            auto ink=std::make_shared<SubtitleInkSnapshot>(*cue->ink);
            for(int line=0;line<cue->text.lineCount;++line) {
                const auto& bounds=cue->text.lineBounds[line];
                for(int y=bounds.top;y<bounds.bottom;++y)
                    for(int x=bounds.left;x<bounds.right;++x) {
                        const size_t pixel=size_t(y)*ink->width+x;
                        ink->rawInk[pixel/64]&=~(uint64_t{1}<<(pixel%64));
                    }
                for(int glyph=0;glyph<(bounds.right-bounds.left)/13;++glyph)
                    AddNewGlyph(*ink,bounds.left+glyph*13,bounds.top,true);
            }
            cue->ink=ink;
        }
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(first,changed));
        tracker.Consume(Resolve({lock,changed}));
        const auto next=tracker.Consume(Resolve({changed,confirmed}));
        Assert::IsTrue(next.detected);
        Assert::IsFalse(next.held);
        Assert::IsTrue(next.cue!=accepted.cue,L"confirmed text change takes precedence over the old template");
    }
    TEST_METHOD(ChangedMissingUpperLineCannotHoldTheOldTwoLineCue) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        Assert::IsTrue(acquired.detected && acquired.lineCount==2);
        const auto locked=tracker.Consume(Resolve({lock}));
        Assert::AreEqual(acquired.cue,locked.cue);
        for(uint64_t sequence=3;sequence<=5;++sequence) {
            auto partial=LowerOnlyCue(sequence);
            auto ink=std::make_shared<SubtitleInkSnapshot>(*partial.ink);
            const auto& upper=first.text.lineBounds[0];
            for(int y=upper.top;y<upper.bottom;++y)for(int x=upper.left;x<upper.right;++x) {
                const int glyphX=(x-upper.left)%9,glyphY=y-upper.top;
                const bool hShape=glyphX<2 || glyphX>=7 || (glyphY>=6 && glyphY<=7);
                const size_t p=size_t(y)*ink->width+x;
                const uint64_t bit=uint64_t{1}<<(p%64);
                if(hShape)ink->rawInk[p/64]|=bit;else ink->rawInk[p/64]&=~bit;
            }
            partial.ink=ink;
            const auto current=tracker.Consume(Resolve({partial}));
            Assert::IsTrue(current.detected);
            Assert::IsFalse(current.held,L"changed upper strokes cannot be carried by the unchanged lower line");
            Assert::AreEqual(1,current.lineCount);
            Assert::IsTrue(current.cue!=acquired.cue);
        }
    }
    TEST_METHOD(NewGlyphsJustOutsideMissingLineExpandInsteadOfKeepingTheOldNarrowBox) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        const auto locked=tracker.Consume(Resolve({lock}));
        Assert::AreEqual(acquired.cue,locked.cue);
        auto partial=LowerOnlyCue(3);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*partial.ink);
        for(int glyph=0;glyph<3;++glyph) {
            AddNewGlyph(*ink,130+glyph*13,318);
            AddNewGlyph(*ink,470+glyph*13,318);
        }
        partial.ink=ink;
        SubtitleBoxRect extension;
        Assert::IsTrue(SubtitleBoxLookahead::FindNewInkExtension(
            *first.ink,*ink,first.text.lineBounds[0],first.width,first.height,extension));
        Assert::IsTrue(extension.Valid());
        auto following=partial;
        following.identity=Identity(4);
        auto followingInk=std::make_shared<SubtitleInkSnapshot>(*ink);
        following.ink=followingInk;
        const auto current=tracker.Consume(Resolve({partial,following}));
        Assert::IsTrue(current.detected);
        Assert::AreEqual(acquired.cue,current.cue,L"confirmed new glyphs extend the established cue instead of reacquiring it");
        Assert::AreEqual(2,current.lineCount);
        Assert::IsTrue(current.bounds.left<acquired.bounds.left,
            L"the established box expands to include the persistent glyphs outside its prior bounds");
        Assert::IsTrue(current.bounds.right>acquired.bounds.right,
            L"pixels between both existing line extents do not suppress a right-side extension");
        const auto geometry=SubtitleCutPastePresentation{}.Consume(current,Resolve({partial,following}));
        Assert::IsTrue(geometry.valid);
        Assert::IsTrue(geometry.glyphLines[0].left<=130 && geometry.glyphLines[0].right>=504,
            L"confirmed side glyphs must extend their own row's extraction mask");
        Assert::IsTrue(geometry.glyphLines[1].left>geometry.glyphLines[0].left,
            L"the shorter second row must not gain the first row's picture corridor");
    }
    TEST_METHOD(SingleNarrowSubtitleGlyphCanExtendAfterContiguousConfirmation) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        tracker.Consume(Resolve({lock}));
        auto partial=LowerOnlyCue(3);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*partial.ink);
        for(int y=318;y<332;++y) {
            const size_t p=size_t(y)*ink->width+448;
            ink->rawInk[p/64]|=uint64_t{1}<<(p%64);
        }
        partial.ink=ink;
        auto following=partial;following.identity=Identity(4);
        const auto current=tracker.Consume(Resolve({partial,following}));
        Assert::AreEqual(acquired.cue,current.cue);
        Assert::IsTrue(current.bounds.right>acquired.bounds.right,
            L"a persistent one-pixel-wide glyph is retained in the cue bounds");
    }
    TEST_METHOD(WideJoinedArabicWordCanExtendBeyondTheAcquiredLine) {
        auto reference=LowerOnlyCue(1);
        auto current=LowerOnlyCue(2);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        AddWideJoinedWord(*ink,420,338,60,10);
        current.ink=ink;
        SubtitleBoxRect extension;
        Assert::IsTrue(SubtitleBoxLookahead::FindNewInkExtension(
            *reference.ink,*current.ink,reference.text.lineBounds[0],640,360,extension));
        Assert::IsTrue(extension.left<=420 && extension.right>=480,
            L"a joined word wider than twice the line height remains eligible on the established baseline");
    }
    TEST_METHOD(ExpandedBoxRequiresTheSameAddedGlyphsToRemainVisible) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        tracker.Consume(Resolve({lock}));
        auto partial=LowerOnlyCue(3);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*partial.ink);
        AddNewGlyph(*ink,160,318);
        partial.ink=ink;
        auto following=partial;following.identity=Identity(4);
        const auto expanded=tracker.Consume(Resolve({partial,following}));
        Assert::AreEqual(acquired.cue,expanded.cue);
        Assert::IsTrue(expanded.bounds.left<acquired.bounds.left);
        tracker.Consume(Resolve({following}));
        auto vanished=LowerOnlyCue(5),stillGone=LowerOnlyCue(6);
        const auto reacquired=tracker.Consume(Resolve({vanished,stillGone}));
        Assert::IsTrue(reacquired.detected);
        Assert::IsTrue(reacquired.cue!=acquired.cue,
            L"removing the newly acquired glyphs cannot preserve a stale expanded cue box");
        Assert::AreEqual(1,reacquired.lineCount);
    }
    TEST_METHOD(ExpandedCueSurvivesDetectorMissWithEmptyOwnedInk) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        tracker.Consume(Resolve({lock}));
        auto partial=LowerOnlyCue(3);
        auto ink=std::make_shared<SubtitleInkSnapshot>(*partial.ink);
        AddNewGlyph(*ink,160,318);
        partial.ink=ink;
        auto following=partial;following.identity=Identity(4);
        const auto expanded=tracker.Consume(Resolve({partial,following}));
        Assert::AreEqual(acquired.cue,expanded.cue);
        Assert::IsTrue(expanded.bounds.left<acquired.bounds.left);
        tracker.Consume(Resolve({following}));

        auto miss=partial;miss.identity=Identity(5);miss.text={};
        auto missedInk=std::make_shared<SubtitleInkSnapshot>(*partial.ink);
        missedInk->ownedInk.clear();
        miss.ink=missedInk;
        auto recovered=partial;recovered.identity=Identity(6);
        const auto result=tracker.Consume(Resolve({miss,recovered}));
        Assert::IsTrue(result.detected && result.held,
            L"a raw-ink grouping miss must not crash or lose a proven cue");
        Assert::AreEqual(acquired.cue,result.cue);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(expanded.bounds,result.bounds,0));
    }
    TEST_METHOD(DifferentExtensionGlyphStrokesCannotConfirmTheSameBounds) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        tracker.Consume(Resolve({lock}));
        auto outlined=LowerOnlyCue(3),confirmed=LowerOnlyCue(4);
        auto o=std::make_shared<SubtitleInkSnapshot>(*outlined.ink);
        AddNewGlyph(*o,160,318,false);
        outlined.ink=o;confirmed.ink=std::make_shared<SubtitleInkSnapshot>(*o);
        const auto expanded=tracker.Consume(Resolve({outlined,confirmed}));
        Assert::AreEqual(acquired.cue,expanded.cue);
        Assert::IsTrue(expanded.bounds.left<acquired.bounds.left);
        tracker.Consume(Resolve({confirmed}));
        auto changed=LowerOnlyCue(5),returning=LowerOnlyCue(6);
        auto h=std::make_shared<SubtitleInkSnapshot>(*changed.ink);
        AddNewGlyph(*h,160,318,true);
        changed.ink=h;
        returning.ink=std::make_shared<SubtitleInkSnapshot>(*o);
        const auto result=tracker.Consume(Resolve({changed,returning}));
        Assert::IsTrue(result.detected);
        Assert::IsTrue(result.cue!=acquired.cue,
            L"a changed glyph takes precedence over the old box even if old-shaped pixels return next");
        Assert::AreEqual(1,result.lineCount);
    }
    TEST_METHOD(LaterExpansionInkIsTrackedAndCannotDisappearBehindTheFirstGlyph) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        tracker.Consume(Resolve({lock}));

        auto initial=LowerOnlyCue(3);
        auto initialInk=std::make_shared<SubtitleInkSnapshot>(*initial.ink);
        AddNewGlyph(*initialInk,448,318);
        initial.ink=initialInk;
        auto initialFollowing=initial;initialFollowing.identity=Identity(4);
        const auto firstExpansion=tracker.Consume(Resolve({initial,initialFollowing}));
        Assert::AreEqual(acquired.cue,firstExpansion.cue);
        Assert::IsTrue(firstExpansion.bounds.right>acquired.bounds.right);
        tracker.Consume(Resolve({initialFollowing}));

        auto later=LowerOnlyCue(5);
        auto laterInk=std::make_shared<SubtitleInkSnapshot>(*later.ink);
        AddNewGlyph(*laterInk,448,318);
        AddNewGlyph(*laterInk,470,318);
        later.ink=laterInk;
        auto laterFollowing=later;laterFollowing.identity=Identity(6);
        const auto secondExpansion=tracker.Consume(Resolve({later,laterFollowing}));
        Assert::AreEqual(acquired.cue,secondExpansion.cue);
        Assert::IsTrue(secondExpansion.bounds.right>firstExpansion.bounds.right,
            L"a second confirmed glyph expands the existing cue box");
        tracker.Consume(Resolve({laterFollowing}));

        auto oneFrameDropout=LowerOnlyCue(7);
        auto oneFrameInk=std::make_shared<SubtitleInkSnapshot>(*oneFrameDropout.ink);
        AddNewGlyph(*oneFrameInk,448,318);
        oneFrameDropout.ink=oneFrameInk;
        auto returned=LowerOnlyCue(8);
        auto returnedInk=std::make_shared<SubtitleInkSnapshot>(*returned.ink);
        AddNewGlyph(*returnedInk,448,318);
        AddNewGlyph(*returnedInk,470,318);
        returned.ink=returnedInk;
        const auto bridged=tracker.Consume(Resolve({oneFrameDropout,returned}));
        Assert::IsTrue(bridged.detected && bridged.held,
            L"a single missing extension glyph bridges only when the exact learned mask returns next");
        Assert::AreEqual(acquired.cue,bridged.cue);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(bridged.bounds,secondExpansion.bounds,0));
        tracker.Consume(Resolve({returned}));

        auto lostSecond=LowerOnlyCue(9);
        auto lostSecondInk=std::make_shared<SubtitleInkSnapshot>(*lostSecond.ink);
        AddNewGlyph(*lostSecondInk,448,318);
        lostSecond.ink=lostSecondInk;
        auto stillLost=lostSecond;stillLost.identity=Identity(10);
        const auto reacquired=tracker.Consume(Resolve({lostSecond,stillLost}));
        Assert::IsTrue(reacquired.detected);
        Assert::AreEqual(1,reacquired.lineCount);
        Assert::IsTrue(reacquired.cue!=acquired.cue,
            L"losing a later extension cannot preserve the stale expanded cue");
        Assert::IsTrue(reacquired.bounds.right<secondExpansion.bounds.right);
    }
    TEST_METHOD(ChangingPictureTextureInsideTheLineDoesNotBecomeExpansionEvidence) {
        auto first=Cue(1,true),lock=Cue(2,true);
        first.pictureBottom=lock.pictureBottom=320;
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        tracker.Consume(Resolve({lock}));

        auto partial=LowerOnlyCue(3);
        partial.pictureBottom=320;
        auto noisy=std::make_shared<SubtitleInkSnapshot>(*partial.ink);
        AddNewGlyph(*noisy,448,318);
        // Two new picture-side specks in each original inter-glyph gap. They
        // are tolerated by the acquired-line mask and lie inside its envelope.
        for(int gap=0;gap<26;++gap)for(int y=320;y<322;++y) {
            const int x=205+gap*9;
            const size_t p=size_t(y)*noisy->width+x;
            noisy->rawInk[p/64]|=uint64_t{1}<<(p%64);
        }
        partial.ink=noisy;
        auto following=partial;following.identity=Identity(4);
        Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(first,partial));
        const auto expanded=tracker.Consume(Resolve({partial,following}));
        Assert::AreEqual(acquired.cue,expanded.cue);
        Assert::AreEqual(2,expanded.lineCount);
        tracker.Consume(Resolve({following}));

        auto clean=LowerOnlyCue(5);
        clean.pictureBottom=320;
        auto cleanInk=std::make_shared<SubtitleInkSnapshot>(*clean.ink);
        AddNewGlyph(*cleanInk,448,318);
        clean.ink=cleanInk;
        auto cleanFollowing=clean;cleanFollowing.identity=Identity(6);
        const auto stillSame=tracker.Consume(Resolve({clean,cleanFollowing}));
        Assert::IsTrue(stillSame.detected);
        Assert::AreEqual(acquired.cue,stillSame.cue,
            L"tolerated picture texture vanishing cannot discard the subtitle extension");
        Assert::AreEqual(2,stillSame.lineCount);
    }
    TEST_METHOD(PermanentlyMissingExtensionGlyphCannotHideInsideGlobalCoverage) {
        auto first=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(Resolve({first,lock}));
        tracker.Consume(Resolve({lock}));

        auto expanded=LowerOnlyCue(3);
        auto allGlyphs=std::make_shared<SubtitleInkSnapshot>(*expanded.ink);
        for(int glyph=0;glyph<8;++glyph)AddNewGlyph(*allGlyphs,448+glyph*10,318);
        expanded.ink=allGlyphs;
        auto confirmed=expanded;confirmed.identity=Identity(4);
        const auto wider=tracker.Consume(Resolve({expanded,confirmed}));
        Assert::AreEqual(acquired.cue,wider.cue);
        Assert::IsTrue(wider.bounds.right>acquired.bounds.right);
        tracker.Consume(Resolve({confirmed}));

        auto missing=LowerOnlyCue(5);
        auto sevenGlyphs=std::make_shared<SubtitleInkSnapshot>(*missing.ink);
        for(int glyph=0;glyph<7;++glyph)AddNewGlyph(*sevenGlyphs,448+glyph*10,318);
        missing.ink=sevenGlyphs;
        auto stillMissing=missing;stillMissing.identity=Identity(6);
        const auto reacquired=tracker.Consume(Resolve({missing,stillMissing}));
        Assert::IsTrue(reacquired.detected);
        Assert::AreEqual(1,reacquired.lineCount);
        Assert::IsTrue(reacquired.cue!=acquired.cue,
            L"global coverage by the other seven extensions cannot keep a missing glyph");
        Assert::IsTrue(reacquired.bounds.right<wider.bounds.right);
    }
    TEST_METHOD(VerticalSceneExtensionNeedsFullFixedPixelHorizon) {
        auto acquire=Cue(1,true),lock=Cue(2,true);
        SubtitleBoxPresentation transient;
        const auto original=transient.Consume(Resolve({acquire,lock}));
        transient.Consume(Resolve({lock}));
        auto rail=Cue(3,true);
        auto railInk=std::make_shared<SubtitleInkSnapshot>(*rail.ink);
        AddNewGlyph(*railInk,310,292);
        rail.ink=railInk;
        auto near1=rail;near1.identity=Identity(4);
        auto near2=rail;near2.identity=Identity(5);
        auto drift=Cue(6,true);
        const auto rejected=transient.Consume(Resolve({rail,near1,near2,drift}));
        Assert::AreEqual(original.bounds.top,rejected.bounds.top,
            L"a transient vertical scene row cannot enlarge the measured subtitle");

        SubtitleBoxPresentation shortHorizon;
        const auto shortOriginal=shortHorizon.Consume(Resolve({acquire,lock}));
        shortHorizon.Consume(Resolve({lock}));
        const auto held=shortHorizon.Consume(Resolve({rail,near1}));
        Assert::AreEqual(shortOriginal.bounds.top,held.bounds.top,
            L"a short queue retains the proven subtitle instead of accepting a vertical candidate");
    }
    TEST_METHOD(ShiftedGlyphWhollyInsidePictureCannotCountAsCurrentBarText) {
        auto reference=Cue(1,false),current=Cue(2,false);
        reference.pictureBottom=current.pictureBottom=331;
        auto shifted=std::make_shared<SubtitleInkSnapshot>(*current.ink);
        std::fill(shifted->rawInk.begin(),shifted->rawInk.end(),0);
        const auto& line=current.text.lineBounds[0];
        for(int y=line.top;y<line.bottom;++y)for(int x=line.left;x<line.right;++x)
            if(current.ink->Get(x,y)) {
                const int movedY=y-1;
                const size_t p=size_t(movedY)*shifted->width+x;
                shifted->rawInk[p/64]|=uint64_t{1}<<(p%64);
            }
        current.ink=shifted;
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(reference,current),
            L"the actual matched current rows are all above the black-bar boundary");
    }
    TEST_METHOD(SameCueRemainsAnchoredByOneExactCurrentBlackBarRow) {
        auto reference=Cue(1,false),current=Cue(2,false);
        reference.pictureBottom=current.pictureBottom=331;
        Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(reference,current),
            L"the last exact glyph row at y=331 is still current bar evidence");
    }
    TEST_METHOD(LocalChangedGlyphCannotHideInsideAnOtherwiseUnchangedLongCue) {
        auto original=Pixels();Text(original,175,307,22);Text(original,240,336,12);
        auto changed=original;Text(changed,175+13*10,307,1,true);
        const auto a=Measure(original,1),b=Measure(original,2),later=Measure(original,3);
        auto miss=Measure(changed,2);miss.text={};
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(a,miss));
        SubtitleBoxPresentation tracker;Assert::IsTrue(tracker.Consume(Resolve({a,b})).detected);
        Assert::IsFalse(tracker.Consume(Resolve({miss,later})).detected);
    }
    TEST_METHOD(RemovedUpperLineCannotBorrowFutureInkOrPreserveTheOldFullBox) {
        auto both=Pixels();Text(both,175,307,22);Text(both,240,336,12);
        auto lower=Pixels();Text(lower,240,336,12);
        const auto a=Measure(both,1),b=Measure(both,2),c=Measure(both,3),d=Measure(both,4);
        const auto current=Measure(lower,2);
        const auto preview=Resolve({current,c,d});
        Assert::AreEqual(1,preview.text.lineCount,L"future pixels cannot supply the removed line");
        SubtitleBoxPresentation tracker;const auto old=tracker.Consume(Resolve({a,b}));
        auto miss=current;miss.text={};
        Assert::IsFalse(tracker.Consume(Resolve({miss,c,d})).detected);
        Assert::IsTrue(old.detected);
    }
    TEST_METHOD(CurrentGlyphReuseCannotCrossUnknownBarsOrCurrentSequenceGap) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        const auto a=Measure(pixels,1),b=Measure(pixels,2),c=Measure(pixels,3),d=Measure(pixels,4);
        for(int kind=0;kind<7;++kind) {
            SubtitleBoxPresentation tracker;Assert::IsTrue(tracker.Consume(Resolve({a,b})).detected);
            auto miss=b,future=c;miss.text={};
            if(kind==0)miss.barAuthority=false;
            if(kind==1)miss.pictureBottom-=2;
            if(kind==2)miss.discontinuity=true;
            if(kind==3)miss.text.workLimit=true;
            if(kind==4)miss.identity.acceptedSequence++;
            if(kind==5){future.text={};future.ink.reset();}
            if(kind==6)future.identity.acceptedSequence++;
            const auto result=tracker.Consume(Resolve({miss,future,d}));
            if(kind==3) {
                Assert::IsTrue(miss.ink && miss.ink->rawEvidenceComplete,
                    L"the real detector completed independent current pixel evidence before grouping failed");
                Assert::IsTrue(result.detected && result.held,
                    L"component budget exhaustion alone must use the configured current-pixel grace");
                Assert::AreEqual(std::string("grace-resource-limit-current-ink"),std::string(tracker.DecisionReason()));
                auto incomplete=miss;
                auto partial=std::make_shared<SubtitleInkSnapshot>(*miss.ink);
                partial->rawEvidenceComplete=false;incomplete.ink=partial;
                SubtitleBoxPresentation partialTracker;
                Assert::IsTrue(partialTracker.Consume(Resolve({a,b})).detected);
                Assert::IsFalse(partialTracker.Consume(Resolve({incomplete,future,d})).detected,
                    L"identical pixels in an incomplete snapshot cannot rescue a failed grouping");
            } else if(kind<5)Assert::IsFalse(result.detected);
            else Assert::IsTrue(result.detected && result.held,
                L"future gaps or missing future analysis cannot revoke valid current glyphs");
        }
    }
    TEST_METHOD(CurrentGlyphValidationDoesNotRequireFutureGroupingRecovery) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        const auto a=Measure(pixels,1),b=Measure(pixels,2),c=Measure(pixels,3),d=Measure(pixels,4),e=Measure(pixels,5);
        for(bool noFuture:{false,true}) {
            SubtitleBoxPresentation tracker;Assert::IsTrue(tracker.Consume(Resolve({a,b})).detected);
            auto m2=b,m3=c,m4=d;m2.text={};m3.text={};m4.text={};
            const auto current=tracker.Consume(noFuture?Resolve({m2}):Resolve({m2,m3,m4,e}));
            Assert::IsTrue(current.detected && current.held);
        }
    }
    TEST_METHOD(MovingCurrentInkCannotDriftTheFixedAcquisitionReference) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        auto shifted=Pixels();Text(shifted,176,307,22);Text(shifted,241,336,12);
        const auto a=Measure(pixels,1),b=Measure(pixels,2),c=Measure(pixels,3);
        auto miss=Measure(shifted,2);miss.text={};
        Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(a,miss));
        SubtitleBoxPresentation tracker;Assert::IsTrue(tracker.Consume(Resolve({a,b})).detected);
        Assert::IsFalse(tracker.Consume(Resolve({miss,c})).detected);
    }
    TEST_METHOD(SnapshotRowsAndOwnershipMustSupportEveryReferenceLine) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        const auto a=Measure(pixels,1);auto b=Measure(pixels,2);
        Assert::IsTrue(a.ink && b.ink);Assert::IsTrue(SubtitleBoxLookahead::SameInkAtReference(a,b));
        auto moved=std::make_shared<SubtitleInkSnapshot>(*b.ink);moved->sourceRows[307]++;
        b.ink=moved;Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(a,b));
        b=a;auto noOwner=std::make_shared<SubtitleInkSnapshot>(*a.ink);noOwner->ownedInk.clear();
        b.ink=noOwner;Assert::IsFalse(SubtitleBoxLookahead::SameInkAtReference(b,a));
    }
    TEST_METHOD(MissingSnapshotFailsClosedForFutureCompanionsAndBridging) {
        auto first=Cue(1),future=Cue(2,true),later=Cue(3,true);
        first.ink.reset();Assert::AreEqual(1,Resolve({first,future,later}).text.lineCount);
        SubtitleBoxPresentation tracker;const auto initial=tracker.Consume(Resolve({Cue(1),Cue(2)}));
        auto miss=Cue(2);miss.text={};miss.ink.reset();
        Assert::IsTrue(initial.detected);Assert::IsFalse(tracker.Consume(Resolve({miss,Cue(3)})).detected);
    }
    TEST_METHOD(OnePixelMeasuredBarFringeDoesNotInterruptTheSameProvenCue) {
        auto first=Cue(1),second=Cue(2),third=Cue(3),fourth=Cue(4);
        second.pictureBottom--;fourth.pictureTop++;
        SubtitleBoxPresentation tracker;
        const auto a=tracker.Consume(Resolve({first,second,third,fourth}));
        const auto b=tracker.Consume(Resolve({second,third,fourth}));
        const auto c=tracker.Consume(Resolve({third,fourth}));
        const auto d=tracker.Consume(Resolve({fourth}));
        Assert::IsTrue(a.detected && b.detected && c.detected && d.detected);
        Assert::AreEqual(a.cue,d.cue);
        Assert::IsTrue(SubtitleBoxLookahead::SameLine(a.bounds,d.bounds,0));
    }
    TEST_METHOD(BarFringeToleranceCannotBridgeMissingBarsOrAnActualBoundaryMove) {
        auto first=Cue(1),next=Cue(2);
        next.pictureBottom-=2;
        Assert::IsFalse(SubtitleBoxLookahead::SameContext(first,next));
        first.pictureTop=1;next=first;next.pictureTop=0;
        Assert::IsFalse(SubtitleBoxLookahead::SameContext(first,next));
        next=first;next.barAuthority=false;
        Assert::IsFalse(SubtitleBoxLookahead::SameContext(first,next));
    }
    TEST_METHOD(ScaleRelativeBoundaryWobbleKeepsCueContextButAspectChangeBreaksIt) {
        auto first=Cue(1),wobble=Cue(2);
        first.width=wobble.width=2560;first.height=wobble.height=1440;
        first.pictureTop=180;first.pictureBottom=1260;
        wobble.pictureTop=187;wobble.pictureBottom=1253;
        Assert::IsTrue(SubtitleBoxLookahead::SameContext(first,wobble),
            L"a seven-pixel boundary wobble at 1440p is within the scale-relative allowance");
        wobble.pictureBottom=1252;
        Assert::IsFalse(SubtitleBoxLookahead::SameContext(first,wobble),
            L"a larger boundary shift breaks continuity and requires reacquisition");
    }
    TEST_METHOD(BlackSceneCannotHideClearlyBackedBarCaptionFromAutomaticPipeline) {
        for(bool top:{false,true}) {
            auto pixels=Pixels();
            // A dark sloping horizon hides the lower encoded bar boundary.
            for(int x=0;x<640;++x)Fill(pixels,x,245+x/20,x+1,315,64);
            Text(pixels,200,324,22);
            if(top)for(int y=0;y<180;++y)for(int x=0;x<640;++x)std::swap(pixels[y*640+x],pixels[(359-y)*640+x]);
            const auto source=Source(pixels);const auto raw=ExtractActivePictureEvidence(source);
            const auto unchanged=raw.proposedBounds;
            SubtitleBoxDetector detector;
            const auto o=SubtitleBoxLookahead::Measure(detector,source,raw,Identity(1),false);
            Assert::IsTrue(o.text.detected && (top?o.barEvidence.captionBackedTop:o.barEvidence.captionBackedBottom));
            Assert::AreEqual(unchanged.top,raw.proposedBounds.top);Assert::AreEqual(unchanged.bottom,raw.proposedBounds.bottom);
            const auto ref=SubtitleBoxLookahead::AdvanceBarTrackingReference(o);
            Assert::AreEqual(top?0:360,top?ref.pictureTop:ref.pictureBottom,L"caption strip is not a physical bar reference");
            auto next=SubtitleBoxLookahead::Measure(detector,source,raw,Identity(2),false,ref);
            Assert::IsTrue(next.text.detected);
        }
    }
    TEST_METHOD(CaptionStripProofRejectsUnownedPictureAndMissingOppositeBar) {
        auto pixels=Pixels();
        for(int x=0;x<640;++x)Fill(pixels,x,245+x/20,x+1,315,64);
        Text(pixels,200,324,22);
        SubtitleBoxDetector detector;
        auto source=Source(pixels);auto text=detector.Analyze(source,45,240,1,1);
        Assert::IsTrue(text.detected);
        int plane=0;const auto ink=detector.InkSnapshot();
        Assert::IsTrue(ProveSubtitleCaptionStrip(source,text,ink.get(),true,64,plane));
        Fill(pixels,30,337,170,348,300);
        Assert::IsFalse(ProveSubtitleCaptionStrip(Source(pixels),text,ink.get(),true,64,plane),L"picture stripe cannot be masked by a caption rectangle");
        Fill(pixels,0,0,640,360,64);
        Text(pixels,200,324,22);
        source=Source(pixels);const auto o=SubtitleBoxLookahead::Measure(detector,source,ExtractActivePictureEvidence(source),Identity(1),false);
        Assert::IsFalse(o.barEvidence.captionBackedBottom,L"no unconditional full-black-frame acquisition");
    }
    TEST_METHOD(DistributedBarsRecoverDarkFlanksWithoutMirroring) {
        for(bool darkLeft:{false,true}) {
            auto pixels=Pixels();
            const int left=darkLeft?0:440,right=darkLeft?200:640;
            Fill(pixels,left,45,right,75,70);
            Fill(pixels,left,285,right,315,70);
            const auto bars=ExtractSubtitleBarEvidence(Source(pixels));
            Assert::AreEqual(45,bars.top);Assert::AreEqual(315,bars.bottom);
            Assert::IsTrue(bars.topDistributedBoundary && bars.bottomDistributedBoundary);
            Assert::IsTrue(bars.topBoundarySupport>=16 && bars.bottomBoundarySupport>=16);
            Assert::IsTrue(bars.topBlackFraction>=0.97 && bars.bottomBlackFraction>=0.97);
            Assert::IsTrue(bars.samples<70000,L"The failed-flank fallback has a fixed sampling budget.");
        }
    }
    TEST_METHOD(DistributedBarsRequireSeparateOpposingEvidenceAndRejectIntrusion) {
        for(int mode=0;mode<4;++mode) {
            auto pixels=Pixels();
            Fill(pixels,0,45,200,75,70);
            if(mode==0)Fill(pixels,0,315,640,360,300); // no opposite bar
            if(mode==1)Fill(pixels,0,315,640,325,300); // asymmetric opposite bar
            if(mode==2)Fill(pixels,180,25,440,45,300); // connected picture
            if(mode==3) { // compact interior light, no broad edge evidence
                Fill(pixels,0,45,640,75,70);
                Fill(pixels,250,45,340,65,300);
            }
            const auto bars=ExtractSubtitleBarEvidence(Source(pixels));
            Assert::IsFalse(bars.topDistributedBoundary);
            if(mode!=3)Assert::AreEqual(0,bars.top,L"Failure must preserve the unproved top edge, not mirror the bottom.");
            else Assert::IsTrue(bars.top!=45,L"A compact interior light must not establish its own bar plane.");
        }
    }
    TEST_METHOD(DistributedBarsDoNotAlterOrdinaryWideCardGeometry) {
        auto pixels=Pixels();Fill(pixels,80,280,560,340,64);Text(pixels,255,301,10);
        const auto bars=ExtractSubtitleBarEvidence(Source(pixels));
        Assert::AreEqual(45,bars.top);Assert::AreEqual(315,bars.bottom);
        Assert::IsFalse(bars.topDistributedBoundary || bars.bottomDistributedBoundary);
    }
    TEST_METHOD(SubtitleBarsProveAsymmetricAndSingleEdgesIndependently) {
        for(int mode=0;mode<3;++mode) {
            auto pixels=Pixels();Fill(pixels,0,0,640,360,64);
            const int top=mode==1?0:35,bottom=mode==2?360:305;
            Fill(pixels,0,top,640,bottom,300);
            const auto evidence=ExtractSubtitleBarEvidence(Source(pixels));
            Assert::IsTrue(evidence.available);Assert::AreEqual(top,evidence.top);
            Assert::AreEqual(bottom,evidence.bottom);
        }
    }
    TEST_METHOD(SubtitleBarsKeepActualBoundaryUnderWideCenteredOpaquePanels) {
        for(int width:{192,352,480}) {
            auto pixels=Pixels();
            const int left=(640-width)/2;
            // The black panel crosses the picture/bar edge. At 55% and 75%
            // width its upper edge would win an all-column run median.
            Fill(pixels,left,280,left+width,340,64);
            Text(pixels,255,301,10);
            const auto evidence=ExtractSubtitleBarEvidence(Source(pixels));
            Assert::IsTrue(evidence.available);
            Assert::AreEqual(45,evidence.top);
            Assert::AreEqual(315,evidence.bottom,
                L"a centered subtitle panel must not become the bar plane");
            Assert::IsTrue(evidence.bottomBoundarySupport>=8);
        }
    }
    TEST_METHOD(SubtitleBarsDoNotInventBoundaryFromCenteredPanelWithoutSideBars) {
        for(int width:{192,352,480}) {
            auto pixels=Pixels(true);
            const int left=(640-width)/2;
            Fill(pixels,left,280,left+width,360,64);
            Assert::IsFalse(ExtractSubtitleBarEvidence(Source(pixels)).available,
                L"an interior panel edge cannot establish a full-width bar");
        }
    }
    TEST_METHOD(SubtitleBarsRejectConnectedPictureOnOnlyTheAffectedEdge) {
        for(bool top:{false,true}) {
            auto pixels=Pixels();
            Fill(pixels,190,top?35:315,450,top?45:325,300);
            const auto evidence=ExtractSubtitleBarEvidence(Source(pixels));
            Assert::IsTrue(evidence.available,L"unaffected bar remains usable");
            Assert::AreEqual(top?0:45,evidence.top);
            Assert::AreEqual(top?315:360,evidence.bottom);
        }
    }
    TEST_METHOD(SubtitleBarsRejectSmoothFullRasterDarkGradients) {
        const int w=1040,h=585;
        for(int mode=0;mode<5;++mode) {
            std::vector<uint8_t> pixels(w*h*4,255);
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                const int distance=(std::min)(y,h-1-y);
                const int ramp=mode==0?distance/2:mode==1?distance/10:
                    mode==2?distance*2:mode==3?distance*3:(std::max)(0,distance-30)*2;
                const auto level=uint8_t((std::min)(100,ramp));
                auto* pixel=&pixels[(y*w+x)*4];pixel[0]=pixel[1]=pixel[2]=level;
            }
            Assert::IsFalse(ExtractSubtitleBarEvidence(RgbSource(pixels,w,h)).available);
        }
    }
    TEST_METHOD(SubtitleBarsRejectUniformBlackDarkAndFullRasterPicture) {
        for(int level:{0,64,90,300}) {
            auto pixels=Pixels();Fill(pixels,0,0,640,360,level);
            Assert::IsFalse(ExtractSubtitleBarEvidence(Source(pixels)).available);
        }
    }
    TEST_METHOD(SubtitleBarsAcceptSparseCrossingGlyphsAndPreserveExactBoundary) {
        auto pixels=Pixels();Text(pixels,180,310,22);
        const auto evidence=ExtractSubtitleBarEvidence(Source(pixels));
        Assert::IsTrue(evidence.available);Assert::AreEqual(45,evidence.top);
        Assert::AreEqual(315,evidence.bottom);
        Assert::IsTrue(Measure(pixels,1).text.detected);
        auto above=Pixels();Text(above,180,299,22);
        Assert::IsFalse(Measure(above,2).text.detected,L"glyphs entirely inside picture have no bar anchor");
    }
    TEST_METHOD(SubtitleBarsUseBothSidesOfLearnedBlackFloor) {
        auto pixels=Pixels();
        // Retain the learned64 edge floor but give the alleged bars alternating
        // dark texture below it; one-sided luma<=floor+12 would accept this.
        for(int y=3;y<45;++y)for(int x=0;x<640;++x) {
            const int code=((x/8+y/3)%2)?0:64;
            pixels[y*640+x]=uint16_t(code<<6);
            pixels[(359-y)*640+x]=uint16_t(code<<6);
        }
        Assert::IsFalse(ExtractSubtitleBarEvidence(Source(pixels)).available);
    }
    TEST_METHOD(SubtitleBottomBarIgnoresOneDarkSceneRowWithoutRelaxingIntrusionGate) {
        auto pixels=Pixels();
        // One row of dark picture is accidentally included by the median run.
        // The adjacent shallower edge is independently visible in this frame.
        Fill(pixels,0,314,640,315,64);
        Fill(pixels,380,314,530,315,700);
        const auto evidence=ExtractSubtitleBarEvidence(Source(pixels));
        Assert::IsTrue(evidence.available);
        Assert::AreEqual(315,evidence.bottom);
        Assert::IsTrue(evidence.bottomRecoveredAtShallowerEdge);

        auto unsafe=Pixels();
        // A long moving-image intrusion deeper than the two-row recovery window
        // remains vetoed; a merely dark median cannot certify a bar.
        Fill(unsafe,0,310,640,315,64);
        Fill(unsafe,380,310,530,315,700);
        const auto rejected=ExtractSubtitleBarEvidence(Source(unsafe));
        Assert::AreEqual(360,rejected.bottom,
            L"persistent content must leave the bottom edge uncertified");
        Assert::IsFalse(rejected.bottomRecoveredAtShallowerEdge,
            L"shallower recovery must not leap over persistent picture content");
    }
    TEST_METHOD(SubtitleBarsRgbOddHeightAndLimitedRangeLumaHaveExactGeometry) {
        const int w=1040,h=585,top=74,bottom=511;
        std::vector<uint8_t> pixels(w*h*4,255);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            const auto value=uint8_t(y>=top&&y<bottom?100:0);
            auto* pixel=&pixels[(y*w+x)*4];pixel[0]=pixel[1]=pixel[2]=value;
        }
        const auto evidence=ExtractSubtitleBarEvidence(RgbSource(pixels,w,h));
        Assert::IsTrue(evidence.available);Assert::AreEqual(top,evidence.top);
        Assert::AreEqual(bottom,evidence.bottom);
        auto limited=Pixels();Fill(limited,0,45,640,315,80);
        const auto dim=ExtractSubtitleBarEvidence(Source(limited));
        Assert::IsTrue(dim.available);Assert::AreEqual(45,dim.top);Assert::AreEqual(315,dim.bottom);
    }
    TEST_METHOD(SubtitleBarAnalysisReprovesNearbyReferencePlaneFromCurrentPixels) {
        constexpr int width=3840,height=2160;
        for(bool p210:{false,true}) for(bool top:{false,true}) {
            std::vector<uint16_t> pixels(p210?size_t(width)*height*2:size_t(width)*height*3/2,uint16_t(512<<6));
            std::fill_n(pixels.begin(),size_t(width)*height,uint16_t(64<<6));
            for(int i=4;i<128;i+=8) {
                const int x=(width*(4*128+96*(2*i+1)))/(200*128);
                const int first=top?276:1864,last=top?296:1884;
                for(int y=first;y<last;++y)pixels[size_t(y)*width+x]=uint16_t(90<<6);
            }
            AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels.data());
            source.dataBytes=pixels.size()*sizeof(uint16_t);source.width=width;source.height=height;
            source.rowBytes=source.chromaRowBytes=size_t(width)*2;
            source.format=p210?AnalysisLumaFormat::P210:AnalysisLumaFormat::P010;
            const auto current=ExtractSubtitleBarEvidence(source);
            Assert::IsFalse(current.available,L"weak edge fragments cannot acquire bar authority");
            const int hintTop=top?268:0,hintBottom=top?height:1892;
            const auto exact=RevalidateSubtitleBarEvidence(source,current,hintTop,hintBottom);
            Assert::IsFalse(top?exact.topRevalidatedAtReference:exact.bottomRevalidatedAtReference,
                L"the exact retained plane is eight pixels away from the native boundary");
            const auto exactOnly=ResolveCurrentSubtitleAnalysisBars(source,current,hintTop,hintBottom,nullptr,nullptr,false);
            Assert::IsFalse(exactOnly.available,L"the disabled fallback path keeps the legacy render-thread answer");
            Assert::AreEqual(0u,exactOnly.nearbyProbes,L"render-thread resolver does not sample nearby fallback planes");
            const auto resolved=ResolveCurrentSubtitleAnalysisBars(source,current,hintTop,hintBottom);
            Assert::IsTrue(top?resolved.topRevalidatedNearby:resolved.bottomRevalidatedNearby,
                L"a nearby edge within two sampling steps should be re-proved");
            Assert::IsFalse(resolved.topRevalidatedAtReference||resolved.bottomRevalidatedAtReference,
                L"a shifted proof must not claim an exact reference-plane proof");
            Assert::IsTrue(top?(resolved.top!=hintTop&&std::abs(resolved.top-276)<=8):resolved.top==0,
                L"the resolved top plane must come from current pixels near the shifted edge");
            Assert::IsTrue(top?resolved.bottom==height:(resolved.bottom!=hintBottom&&std::abs(resolved.bottom-1884)<=8),
                L"analysis must use a nearby current-pixel plane, not the retained hint");
            const auto strictlyReproved=RevalidateSubtitleBarEvidence(source,resolved,resolved.top,resolved.bottom);
            Assert::IsTrue(top?strictlyReproved.topRevalidatedAtReference:strictlyReproved.bottomRevalidatedAtReference,
                L"the returned nearby plane must itself pass the unchanged exact current-pixel proof");
            Assert::IsTrue(resolved.available);
            Assert::IsTrue(resolved.nearbyProbes>0);
            const int delta=top?resolved.topReferenceDelta:resolved.bottomReferenceDelta;
            Assert::IsTrue(delta!=0&&std::abs(delta)<=8,
                L"the reported plane offset must reflect the nearby current-pixel proof");
        }
    }
    TEST_METHOD(SubtitleBarAnalysisNearbyProofRejectsGradientIntrusionAndFreshLargerPicture) {
        auto gradient=Pixels();Fill(gradient,0,0,640,360,64);
        for(int y=0;y<360;++y)Fill(gradient,0,y,640,y+1,64+(359-y)/2);
        const auto gradientSource=Source(gradient);
        const auto gradientCurrent=ExtractSubtitleBarEvidence(gradientSource);
        const auto gradientResult=ResolveCurrentSubtitleAnalysisBars(gradientSource,gradientCurrent,0,317);
        Assert::IsFalse(gradientResult.bottomRevalidatedNearby,
            L"smooth dark gradients do not form a sharply bounded native edge");

        auto intrusion=Pixels();Fill(intrusion,0,0,640,360,64);
        for(int i=4;i<128;i+=8) {
            const int x=(640*(4*128+96*(2*i+1)))/(200*128);
            Fill(intrusion,x,300,x+1,315,90);
        }
        Fill(intrusion,320,315,520,321,700);
        const auto intrusionSource=Source(intrusion);
        const auto intrusionCurrent=ExtractSubtitleBarEvidence(intrusionSource);
        const auto intrusionResult=ResolveCurrentSubtitleAnalysisBars(intrusionSource,intrusionCurrent,0,317);
        Assert::IsFalse(intrusionResult.available,
            L"an intrusion in the actual bottom bar must leave the analysis edge unresolved");
        Assert::AreEqual(360,intrusionResult.bottom);

        auto largerPicture=Pixels();Fill(largerPicture,0,0,640,360,64);
        Fill(largerPicture,0,45,640,300,300);
        const auto largerSource=Source(largerPicture);
        const auto largerCurrent=ExtractSubtitleBarEvidence(largerSource);
        const auto largerResult=ResolveCurrentSubtitleAnalysisBars(largerSource,largerCurrent,45,317);
        Assert::AreEqual(300,largerResult.bottom,
            L"a fresh current edge takes precedence over the stale retained hint");
        Assert::IsFalse(largerResult.bottomRevalidatedNearby,
            L"the old nearby hint cannot override a freshly measured larger picture");
    }
    TEST_METHOD(SubtitleReferencePlaneUsesFreshDistributedEdgeWithoutAcquisitionAuthority) {
        auto strong=Pixels();
        const auto reference=ExtractSubtitleBarEvidence(Source(strong));
        Assert::AreEqual(315,reference.bottom);
        auto dark=Pixels();Fill(dark,0,0,640,360,64);
        // Most picture columns blend into black. Sixteen isolated portions of
        // the old encoded edge remain visible across all four horizontal zones.
        for(int i=4;i<128;i+=8) {
            const int x=(640*(4*128+96*(2*i+1)))/(200*128);
            Fill(dark,x,300,x+1,315,90);
        }
        const auto acquired=ExtractSubtitleBarEvidence(Source(dark));
        Assert::IsFalse(acquired.available);
        const auto revisited=RevalidateSubtitleBarEvidence(Source(dark),acquired,reference.top,reference.bottom);
        Assert::IsTrue(revisited.bottomRevalidatedAtReference);
        Assert::IsFalse(revisited.available,L"reference proof must never masquerade as strong acquisition");
        Assert::AreEqual(acquired.bottom,revisited.bottom);
        Assert::AreEqual(acquired.bottomDepth,revisited.bottomDepth);
        Assert::IsTrue(revisited.bottomReferenceBoundarySupport>=8);
        const auto absent=RevalidateSubtitleBarEvidence(Source(dark),acquired,0,360);
        Assert::IsFalse(absent.topRevalidatedAtReference || absent.bottomRevalidatedAtReference);
    }
    TEST_METHOD(SubtitleReferencePlaneRejectsStrongAspectChangesAndUniformFrames) {
        for(int bottom:{300,330,360}) {
            auto pixels=Pixels();Fill(pixels,0,0,640,360,64);Fill(pixels,0,45,640,bottom,300);
            const auto raw=ExtractSubtitleBarEvidence(Source(pixels));
            const auto revisited=RevalidateSubtitleBarEvidence(Source(pixels),raw,45,315);
            Assert::IsFalse(revisited.bottomRevalidatedAtReference);
        }
        for(int level:{0,64,90,300}) {
            auto pixels=Pixels();Fill(pixels,0,0,640,360,level);
            const auto raw=ExtractSubtitleBarEvidence(Source(pixels));
            const auto revisited=RevalidateSubtitleBarEvidence(Source(pixels),raw,45,315);
            Assert::IsFalse(revisited.topRevalidatedAtReference || revisited.bottomRevalidatedAtReference);
        }
        auto fringe=Pixels();Fill(fringe,0,314,640,315,64);
        const auto raw=ExtractSubtitleBarEvidence(Source(fringe));
        Assert::AreEqual(314,raw.bottom);
        Assert::IsTrue(RevalidateSubtitleBarEvidence(Source(fringe),raw,45,315).bottomRevalidatedAtReference,
            L"a nearby edge can normalize only with fresh proof at the reference plane");
    }
    TEST_METHOD(SubtitleReferencePlaneRejectsSmoothSlopeAndClusteredBoundaryPatch) {
        auto gradient=Pixels();Fill(gradient,0,0,640,360,64);
        for(int y=0;y<360;++y)Fill(gradient,0,y,640,y+1,64+(359-y)/2);
        const auto raw=ExtractSubtitleBarEvidence(Source(gradient));
        Assert::IsFalse(RevalidateSubtitleBarEvidence(Source(gradient),raw,0,315).bottomRevalidatedAtReference);
        auto clustered=Pixels();Fill(clustered,0,0,640,360,64);Fill(clustered,30,300,100,315,90);
        const auto patch=ExtractSubtitleBarEvidence(Source(clustered));
        const auto revisited=RevalidateSubtitleBarEvidence(Source(clustered),patch,0,315);
        Assert::IsTrue(revisited.bottomReferenceBoundarySupport>=8);
        Assert::IsFalse(revisited.bottomRevalidatedAtReference,L"one horizontal zone cannot prove an old plane");
    }
    TEST_METHOD(SubtitleReferencePlaneRejectsConnectedIntrusionEvenWhenAlmostAllBarIsBlack) {
        auto pixels=Pixels();Fill(pixels,190,326,450,330,300);
        const auto raw=ExtractSubtitleBarEvidence(Source(pixels));
        const auto revisited=RevalidateSubtitleBarEvidence(Source(pixels),raw,45,315);
        Assert::IsTrue(revisited.bottomReferenceBlackFraction>0.85);
        Assert::IsTrue(revisited.bottomReferenceMaxIntrusion>=25);
        Assert::IsFalse(revisited.bottomRevalidatedAtReference);
    }
    TEST_METHOD(SubtitleOwnedCrossingStrokesCanExplainOnlyTheirBarSamples) {
        auto pixels=Pixels();
        auto sampleX=[](int i) {return (640*(4*128+96*(2*i+1)))/(200*128);};
        auto ink=std::make_shared<SubtitleInkSnapshot>();
        ink->width=640;ink->height=360;ink->step=1;ink->sourceRows.resize(360);
        ink->rawInk.resize((640*360+63)/64);ink->ownedInk.resize(ink->rawInk.size());
        for(int y=0;y<360;++y)ink->sourceRows[size_t(y)]=y;
        // Thirty bright samples in one bar row create a false long intrusion
        // unless they are explained by the provisional detector's owned text.
        for(int i=48;i<78;++i) {
            const int x=sampleX(i);Fill(pixels,x,330,x+1,331,700);
            if((i-48)%4==0) {
                const size_t p=size_t(330)*640+x;
                ink->ownedInk[p/64]|=uint64_t{1}<<(p%64);
            }
        }
        const auto source=Source(pixels);
        const auto current=ExtractSubtitleBarEvidence(source);
        const auto ordinary=RevalidateSubtitleBarEvidence(source,current,45,315);
        Assert::IsTrue(ordinary.bottomReferenceMaxIntrusion>=25);
        Assert::IsFalse(ordinary.bottomRevalidatedAtReference,
            L"unexplained bright pixels still invalidate a tracked bar plane");
        const SubtitleBoxRect crossingLine{sampleX(48),300,sampleX(78),340};
        const auto masked=RevalidateSubtitleBarEvidence(source,current,45,315,ink.get(),&crossingLine);
        Assert::IsTrue(masked.bottomRevalidatedAtReference,
            L"only the bounded, detector-owned text samples may be excluded from the bar test");
        Assert::AreEqual(current.available,masked.available,
            L"candidate-masked proof must not create acquisition authority");
        Assert::AreEqual(current.bottom,masked.bottom,
            L"candidate-masked proof must not change the acquired lower edge");
        for(int i=48;i<78;++i) {
            const int x=sampleX(i);const size_t p=size_t(330)*640+x;
            ink->ownedInk[p/64]|=uint64_t{1}<<(p%64);
        }
        const auto broadPatch=RevalidateSubtitleBarEvidence(source,current,45,315,ink.get(),&crossingLine);
        Assert::IsFalse(broadPatch.bottomRevalidatedAtReference,
            L"a broad contiguous patch cannot be excused as subtitle-owned ink");
    }
    TEST_METHOD(SubtitleBarMaskMapsAgainstDetectorSamplingAt1440p) {
        constexpr int width=2560,height=1440,top=184,bottom=1256;
        std::vector<uint8_t> pixels(size_t(width)*height*4,0);
        for(int y=top;y<bottom;++y)for(int x=0;x<width;++x) {
            auto* p=&pixels[(size_t(y)*width+x)*4];p[0]=p[1]=p[2]=100;p[3]=255;
        }
        auto sampleX=[](int i) {return (2560*(4*128+96*(2*i+1)))/(200*128);};
        auto ink=std::make_shared<SubtitleInkSnapshot>();
        ink->width=(width+2)/3;ink->height=(height+2)/3;ink->step=3;
        ink->sourceRows.resize(size_t(ink->height));
        ink->rawInk.resize((size_t(ink->width)*ink->height+63)/64);
        ink->ownedInk.resize(ink->rawInk.size());
        for(int y=0;y<ink->height;++y)ink->sourceRows[size_t(y)]=y*3;
        for(int i=48;i<78;++i) {
            const int x=sampleX(i);auto* p=&pixels[(size_t(1301)*width+x)*4];
            p[0]=p[1]=p[2]=255;p[3]=255;
            if((i-48)%4==0) {
                const size_t pixel=size_t(1302/3)*ink->width+x/3;
                ink->ownedInk[pixel/64]|=uint64_t{1}<<(pixel%64);
            }
        }
        const auto source=RgbSource(pixels,width,height);
        const auto current=ExtractSubtitleBarEvidence(source);
        const auto ordinary=RevalidateSubtitleBarEvidence(source,current,top,bottom);
        Assert::IsTrue(ordinary.bottomReferenceMaxIntrusion>=25);
        Assert::IsFalse(ordinary.bottomRevalidatedAtReference);
        const SubtitleBoxRect crossingLine{sampleX(48),1200,sampleX(78),1330};
        const auto masked=RevalidateSubtitleBarEvidence(source,current,top,bottom,ink.get(),&crossingLine);
        Assert::IsTrue(masked.bottomRevalidatedAtReference,
            L"native bar stride and detector mask stride differ at 1440p");
    }
    TEST_METHOD(SubtitleReferencePlaneChecksLastNativeBarRowBetweenAnalysisSamples) {
        const int w=1040,h=1080,top=135,bottom=945;
        std::vector<uint8_t> pixels(size_t(w)*h*4,255);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            auto* p=&pixels[(size_t(y)*w+x)*4];
            p[0]=p[1]=p[2]=uint8_t(y>=top&&y<bottom?100:0);
        }
        // Bottom depth135 has an even final sample, so use depth134 where the
        // last native row is odd and would otherwise escape the step2 scan.
        const int referenceBottom=946;
        for(int x=300;x<740;++x) {
            auto* p=&pixels[(size_t(referenceBottom)*w+x)*4];p[0]=p[1]=p[2]=100;
        }
        const auto source=RgbSource(pixels,w,h);
        const auto raw=ExtractSubtitleBarEvidence(source);
        const auto revisited=RevalidateSubtitleBarEvidence(source,raw,top,referenceBottom);
        Assert::IsFalse(revisited.bottomRevalidatedAtReference);
        Assert::IsTrue(revisited.bottomReferenceMaxIntrusion>=25);
    }
    TEST_METHOD(TrackedBarPlaneKeepsOneCueAndBoxThroughMoreThanOneHundredDarkFrames) {
        auto strong=Pixels(),weak=WeakBottomPixels();SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(Resolve({Measure(strong,1)})).detected);
        SubtitleBoxResult first;
        for(uint64_t sequence=2;sequence<123;++sequence) {
            const auto prior=tracker.BarTrackingReferenceFor(Identity(sequence),640,360);
            Assert::IsTrue(prior.valid);Assert::AreEqual(sequence-1,prior.identity.acceptedSequence);
            Assert::AreEqual(315,prior.pictureBottom);
            const auto current=MeasureWithBar(weak,sequence,prior);
            Assert::AreEqual(360,current.barEvidence.bottom,L"ordinary bottom acquisition remains unavailable");
            Assert::IsTrue(current.barEvidence.bottomRevalidatedAtReference);
            Assert::IsTrue(current.barTrackingAuthority && current.text.detected);
            const auto next=MeasureWithBar(weak,sequence+1,SubtitleBoxLookahead::AdvanceBarTrackingReference(current));
            const auto shown=tracker.Consume(Resolve({current,next}));
            Assert::IsTrue(shown.detected);
            if(sequence==2)first=shown;
            else {
                Assert::AreEqual(first.cue,shown.cue);
                Assert::IsTrue(SubtitleBoxLookahead::SameLine(first.bounds,shown.bounds,0));
            }
        }
    }
    TEST_METHOD(WeakBottomStartupCannotAcquireWithoutStrongPrecedingPlane) {
        auto weak=WeakBottomPixels();
        const auto first=Measure(weak,1),second=Measure(weak,2);
        Assert::AreEqual(360,first.pictureBottom);
        Assert::IsFalse(first.barTrackingAuthority || first.text.detected);
        SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(Resolve({first,second})).detected);
    }
    TEST_METHOD(NewCueAtTrackedBottomRequiresDistinctLookaheadEvenWithStrongTopBar) {
        auto strong=Pixels(),weak=WeakBottomPixels();SubtitleBoxPresentation tracker;
        tracker.Consume(Resolve({Measure(strong,1)}));
        const auto current=MeasureWithBar(weak,2,tracker.BarTrackingReferenceFor(Identity(2),640,360));
        Assert::IsTrue(current.barAuthority,L"the unrelated top bar is still independently strong");
        Assert::IsTrue(current.barEvidence.bottomRevalidatedAtReference && current.text.detected);
        SubtitleBoxPresentation single;
        Assert::IsFalse(single.Consume(Resolve({current})).detected,
            L"a strong unrelated edge cannot grant acquisition to a weak tracked caption edge");
        SubtitleBoxPresentation repeated;
        Assert::IsFalse(repeated.Consume(Resolve({current,current})).detected);
        const auto next=MeasureWithBar(weak,3,SubtitleBoxLookahead::AdvanceBarTrackingReference(current));
        const auto result=tracker.Consume(Resolve({current,next}));
        Assert::IsTrue(result.detected);
        Assert::AreEqual(315,tracker.BarTrackingReferenceFor(Identity(3),640,360).pictureBottom);
    }
    TEST_METHOD(SubtitleEntirelyInsideTrackedBarRequiresLookaheadDespiteStrongOppositeBar) {
        auto strong=Pixels(false);SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(Resolve({Measure(strong,1)})).detected);
        auto weak=WeakBottomPixels(false);Text(weak,240,334,12);
        const auto prior=tracker.BarTrackingReferenceFor(Identity(2),640,360);
        const auto current=MeasureWithBar(weak,2,prior);
        Assert::IsTrue(current.barAuthority && current.barTrackingAuthority);
        Assert::IsTrue(current.text.detected);
        Assert::IsTrue(current.currentAnchorUsesTrackedEdge,
            L"all anchor ink in the tracked bar depends on that boundary, even away from its edge");
        Assert::IsFalse(tracker.Consume(Resolve({current})).detected,
            L"a strong opposite bar cannot authorize a singleton entirely inside the tracked bar");
        const auto next=MeasureWithBar(weak,3,SubtitleBoxLookahead::AdvanceBarTrackingReference(current));
        Assert::IsFalse(tracker.Consume(Resolve({current,next})).detected,
            L"replaying an already presented identity cannot add lookahead confirmation");
        SubtitleBoxPresentation confirmed;
        confirmed.Consume(Resolve({Measure(strong,1)}));
        const auto confirmedCurrent=MeasureWithBar(weak,2,
            confirmed.BarTrackingReferenceFor(Identity(2),640,360));
        const auto confirmedNext=MeasureWithBar(weak,3,
            SubtitleBoxLookahead::AdvanceBarTrackingReference(confirmedCurrent));
        Assert::IsTrue(confirmed.Consume(Resolve({confirmedCurrent,confirmedNext})).detected,
            L"two distinct source frames confirm the stable tracked-bar cue");
    }
    TEST_METHOD(CadenceRepeatReusesExactPreviewAndKeepsCutPastePlacement) {
        auto strong=Pixels(false);SubtitleBoxPresentation tracker;
        tracker.Consume(Resolve({Measure(strong,1)}));
        auto weak=WeakBottomPixels();
        const auto current=MeasureWithBar(weak,2,tracker.BarTrackingReferenceFor(Identity(2),640,360));
        const auto next=MeasureWithBar(weak,3,SubtitleBoxLookahead::AdvanceBarTrackingReference(current));
        auto firstPreview=Resolve({current,next});
        firstPreview.newScanMs=4.0;firstPreview.newScanFrames=2;
        const auto first=tracker.Consume(firstPreview);
        SubtitleCutPastePresentation cutPaste;
        const auto placed=cutPaste.Consume(first,firstPreview);
        Assert::IsTrue(first.detected && placed.valid);
        Assert::AreEqual(315,placed.pictureBottom);

        const auto rawRepeat=Measure(weak,2);
        auto repeatedPreview=Resolve({rawRepeat});
        Assert::IsTrue(tracker.ReusePreviewForRepeatedSourceFrame(Identity(2),640,360,false,
            repeatedPreview.policyGeneration,repeatedPreview.continuityGeneration,repeatedPreview));
        Assert::IsTrue(repeatedPreview.current.barTrackingAuthority);
        Assert::AreEqual(0.0,repeatedPreview.newScanMs);
        Assert::AreEqual(0u,repeatedPreview.newScanFrames);
        const auto repeated=tracker.Consume(repeatedPreview);
        const auto repeatedPlacement=cutPaste.Consume(repeated,repeatedPreview);
        Assert::AreEqual(first.cue,repeated.cue);
        Assert::IsTrue(repeatedPlacement.valid,
            L"repeating the same pixels reuses their proof instead of losing the tracked plane");
        Assert::AreEqual(315,repeatedPlacement.pictureBottom);
    }
    TEST_METHOD(IndependentDetectedMeasurementSurvivesDiscardedPredecessor) {
        auto strong=Pixels(false);Text(strong,230,305,12);const auto measured=MeasureWithBar(strong,2,{},11,21);
        Assert::IsTrue(measured.text.detected);Assert::IsTrue(measured.barAuthority);
        const auto prior=SubtitleBoxLookahead::AdvanceBarTrackingReference(MeasureWithBar(strong,1,{},11,21));
        Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(measured,Identity(2),640,360,11,21,false,prior));
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(measured,Identity(2),640,360,12,21,false,prior));
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(measured,Identity(3),640,360,11,21,false,prior));
    }
    TEST_METHOD(FreshMeasurementsValidateEveryCurrentFrameAndChangedGlyphs) {
        auto pixels=Pixels(false);Text(pixels,230,305,12);
        SubtitleMeasurementSampler sampler;
        SubtitleBoxObservation key;key.identity=Identity(1);key.width=640;key.height=360;
        key.policyGeneration=11;key.continuityGeneration=21;
        const auto first=sampler.Measure(Source(pixels),key,{},60);
        Assert::IsTrue(first.text.detected);
        auto previous=SubtitleBoxLookahead::AdvanceBarTrackingReference(first);
        key.identity=Identity(2);
        const auto same=sampler.Measure(Source(pixels),key,previous,60);
        Assert::IsTrue(same.text.detected);Assert::IsTrue(SubtitleBoxLookahead::SameCue(first,same));
        Text(pixels,230,305,12,true);key.identity=Identity(3);
        const auto changed=sampler.Measure(Source(pixels),key,SubtitleBoxLookahead::AdvanceBarTrackingReference(same),60);
        Assert::IsTrue(changed.text.detected);Assert::IsFalse(SubtitleBoxLookahead::SameCue(first,changed));
        Fill(pixels,0,295,640,360,64);key.identity=Identity(4);
        const auto blank=sampler.Measure(Source(pixels),key,SubtitleBoxLookahead::AdvanceBarTrackingReference(changed),60);
        Assert::IsFalse(blank.text.detected);
    }


    TEST_METHOD(SameFrameReuseModesMatchFreshBaselineThroughCueChangesAndBlankFrames) {
        for(int mode:{1,2}){
            SubtitleMeasurementSampler a,b;SubtitleBarTrackingReference ap,bp;
            for(int frame=1;frame<=30;++frame){
                auto pixels=Pixels(false);if(frame<20)Text(pixels,230,305,12,frame>=10);
                SubtitleBoxObservation key;key.identity=Identity(frame);key.width=640;key.height=360;
                key.policyGeneration=11;key.continuityGeneration=21;key.nearBarDistance=20;
                const auto x=a.Measure(Source(pixels),key,ap,60);key.optimizationMode=mode;
                const auto y=b.Measure(Source(pixels),key,bp,60);
                Assert::AreEqual(x.text.detected,y.text.detected);Assert::IsFalse(x.analysisRefresh);Assert::IsFalse(y.analysisRefresh);
                Assert::IsTrue(x.text.lineSignatures==y.text.lineSignatures);
                Assert::AreEqual(x.text.bounds.left,y.text.bounds.left);Assert::AreEqual(x.text.bounds.top,y.text.bounds.top);
                Assert::AreEqual(x.text.bounds.right,y.text.bounds.right);Assert::AreEqual(x.text.bounds.bottom,y.text.bounds.bottom);
                ap=SubtitleBoxLookahead::AdvanceBarTrackingReference(x);bp=SubtitleBoxLookahead::AdvanceBarTrackingReference(y);
            }
        }
    }
    TEST_METHOD(EverySamplingModeDiscoversAndRemovesCompanionOutsideOldGlyphRectangleImmediately) {
        for(int mode:{0,1,2})for(double rate:{24.0,60.0}) {
            auto single=Pixels(false);Text(single,240,336,12);
            SubtitleMeasurementSampler sampler;
            SubtitleBoxObservation key;key.identity=Identity(1);key.width=640;key.height=360;
            key.optimizationMode=mode;key.nearBarDistance=20;
            auto first=sampler.Measure(Source(single),key,{},rate);
            Assert::IsTrue(first.text.detected);Assert::AreEqual(1,first.text.lineCount);
            auto two=single;Text(two,175,307,22);key.identity=Identity(2);
            const auto added=sampler.Measure(Source(two),key,SubtitleBoxLookahead::AdvanceBarTrackingReference(first),rate);
            Assert::IsFalse(added.analysisRefresh);Assert::AreEqual(2,added.text.lineCount);
            Assert::IsTrue(added.ink && added.ink!=first.ink && added.ink->rawEvidenceComplete);
            Assert::IsTrue(added.ink->Get(175,307));Assert::IsFalse(first.ink->Get(175,307));
            key.identity=Identity(3);
            const auto removed=sampler.Measure(Source(single),key,SubtitleBoxLookahead::AdvanceBarTrackingReference(added),rate);
            Assert::IsFalse(removed.analysisRefresh);Assert::AreEqual(1,removed.text.lineCount);
            Assert::IsFalse(removed.ink->Get(175,307));
        }
    }

    TEST_METHOD(ActualQueueRetirementPendingPixelsPreserveCueThenExpireWithoutWorkerRenewal) {
        for(const auto reason:{ActivePictureDiscardReason::BacklogRecovery,
            QueueCapacityReason<ActivePictureDiscardReason>(0)}) {
            ActivePictureDecisionTimeline timeline;timeline.Reset(1);
            auto identity=[](uint64_t sequence) {auto id=Identity(sequence);id.analysisDomainKey=7;return id;};
            for(uint64_t sequence=1;sequence<=7;++sequence)
                Assert::IsTrue(timeline.TrackAcceptedFrame(identity(sequence)));
            const auto before=timeline.ContinuityGeneration();
            auto pixels=Pixels(false);Text(pixels,230,305,12);
            auto first=MeasureWithBar(pixels,1,{},11,before);first.identity=identity(1);
            auto initial=SubtitleBoxLookahead::Resolve(&first,1,11,before);
            SubtitleBoxPresentation tracker;SubtitlePendingMeasurementGuard guard;
            const auto acquired=tracker.Consume(initial,250,50);
            Assert::IsTrue(acquired.detected && tracker.HasAcceptedSourceTemplate());
            Assert::IsTrue(guard.RememberAccepted(initial,acquired,Source(pixels),50,250));
            timeline.MarkConsumed(identity(1));
            timeline.MarkDiscarded(identity(2),2,reason);
            for(uint64_t sequence:{uint64_t(3),uint64_t(4),uint64_t(6)}) {
                if(sequence==6)timeline.MarkDiscarded(identity(5),5,reason);
                const auto currentContinuity=timeline.ContinuityGeneration();
                auto key=first;key.identity=identity(sequence);key.analyzed=false;key.text={};key.ink.reset();
                key.continuityGeneration=currentContinuity;
                auto pending=SubtitleBoxLookahead::Resolve(&key,1,11,currentContinuity);
                const auto token=tracker.AcceptedTemplateIdentity();
                const bool queueOnly=timeline.OnlyBacklogRetirementBetween(token,key.identity,
                    tracker.AcceptedTemplateContinuity(),currentContinuity);
                Assert::AreEqual(sequence!=4,queueOnly);
                const bool verified=ResolvePendingQueue(guard,pending,Source(pixels),50,250,
                    queueOnly?&token:nullptr,queueOnly,0);
                Assert::IsTrue(verified);
                const auto kept=ConsumePendingQueue(tracker,pending,250,50,
                    queueOnly?&token:nullptr,queueOnly,verified,0);
                Assert::IsTrue(kept.detected);
                Assert::AreEqual(acquired.cue,kept.cue);
                Assert::AreEqual(acquired.lineCount,kept.lineCount);
                Assert::AreEqual(acquired.bounds.top,kept.bounds.top);
                Assert::AreEqual(acquired.bounds.bottom,kept.bounds.bottom);
                Assert::IsTrue(SameAnalysisFrameIdentity(key.identity,tracker.AcceptedTemplateIdentity()));
                Assert::AreEqual(currentContinuity,tracker.AcceptedTemplateContinuity());
                Assert::IsFalse(pending.futureAvailable || pending.currentLinesConfirmed);
                Assert::IsFalse(pending.current.subtitleArProof.accepted);
                auto reseedAttempt=guard;
                Assert::IsFalse(reseedAttempt.RememberAccepted(pending,kept,Source(pixels),50,250),
                    L"Recovered pending output is never a new accepted measurement seed.");
                timeline.MarkConsumed(key.identity);
            }
            auto key=first;key.identity=identity(7);key.analyzed=false;key.text={};key.ink.reset();
            key.continuityGeneration=timeline.ContinuityGeneration();
            auto expired=SubtitleBoxLookahead::Resolve(&key,1,11,key.continuityGeneration);
            const bool verified=ResolvePendingQueue(guard,expired,Source(pixels),50,250,nullptr,false,0);
            Assert::IsFalse(verified);
            Assert::AreEqual("pending-budget-expired",guard.DecisionReason());
            Assert::IsFalse(ConsumePendingQueue(tracker,expired,250,50,nullptr,false,verified,0).detected);
        }
    }
    TEST_METHOD(PendingPresentationReassociationRequiresExactTokenAndSuccessfulCurrentProof) {
        for(int scenario=0;scenario<8;++scenario) {
            auto pixels=Pixels(false);Text(pixels,230,305,12);
            auto first=MeasureWithBar(pixels,1,{},11,21);
            auto initial=SubtitleBoxLookahead::Resolve(&first,1,11,21);
            SubtitleBoxPresentation tracker;SubtitlePendingMeasurementGuard guard;
            const auto acquired=tracker.Consume(initial);
            Assert::IsTrue(acquired.detected);
            Assert::IsTrue(guard.RememberAccepted(initial,acquired,Source(pixels),40,250));
            auto key=first;key.identity=Identity(3);key.analyzed=false;key.text={};key.ink.reset();
            key.continuityGeneration=22;
            auto pending=SubtitleBoxLookahead::Resolve(&key,1,11,22);
            auto token=tracker.AcceptedTemplateIdentity();
            Assert::IsTrue(ResolvePendingQueue(guard,pending,Source(pixels),40,250,&token,true,0));
            bool verified=true,queueOnly=true;const ActivePictureFrameIdentity* tokenPointer=&token;
            switch(scenario) {
            case 0: verified=false;break;
            case 1: queueOnly=false;break;
            case 2: tokenPointer=nullptr;break;
            case 3: ++token.acceptedSequence;break;
            case 4: ++token.analysisDomainKey;break;
            case 5: ++pending.policyGeneration;break;
            case 6: ++pending.continuityGeneration;break;
            case 7: pending.current.discontinuity=true;break;
            }
            const auto result=ConsumePendingQueue(tracker,pending,250,40,tokenPointer,queueOnly,verified,0);
            Assert::IsTrue(!result.detected || result.cue!=acquired.cue,
                L"A pending queue token or copied pixels cannot independently reconnect an acquired cue.");
        }
    }
    TEST_METHOD(QueueCapacityWitnessAllowsOnlyKnownRetirementsAndNeverRestoresArContinuity) {
        for(int scenario=0;scenario<10;++scenario) {
            ActivePictureDecisionTimeline timeline;timeline.Reset(1);
            auto previous=Identity(1),dropped=Identity(2),current=Identity(3);
            for(auto* item:{&previous,&dropped,&current})item->analysisDomainKey=7;
            const ActivePictureFrameIdentity buffered[]={previous,dropped,current};
            for(const auto& item:buffered)Assert::IsTrue(timeline.TrackAcceptedFrame(item));
            Assert::IsTrue(timeline.CanProveBufferedFrames(buffered,3));
            const auto before=timeline.ContinuityGeneration();timeline.MarkConsumed(previous);
            if(scenario!=3)timeline.MarkDiscarded(dropped,2,scenario==1?
                ActivePictureDiscardReason::Unknown:QueueCapacityReason<ActivePictureDiscardReason>(0));
            if(scenario==2)timeline.BreakContinuity(3);
            const auto after=timeline.ContinuityGeneration();
            if(scenario==4)++current.transportGeneration;
            if(scenario==5)++current.sourceFormatGeneration;
            if(scenario==6)++current.analysisDomainKey;
            if(scenario==7)++current.viewportGeneration;
            if(scenario==8)++current.rendererGeneration;
            if(scenario==9)++current.sourceFrameNumber;
            const bool witness=timeline.OnlyBacklogRetirementBetween(previous,current,before,after);
            Assert::AreEqual(scenario==0,witness);
            Assert::IsFalse(timeline.CanProveBufferedFrames(buffered,3));
            if(scenario!=3) { Assert::IsTrue(after>before);Assert::IsFalse(timeline.CanProveBufferedFrames(&buffered[2],1)); }
        }
    }
    TEST_METHOD(PendingWorkerKeepsCueOnlyWithCurrentPixelsAndBoundedBudget) {
        auto pixels=Pixels(false);Text(pixels,230,305,12);constexpr uint64_t policy=11,continuity=21;
        auto first=MeasureWithBar(pixels,1,{},policy,continuity);
        Assert::IsTrue(first.text.detected);
        SubtitlePendingMeasurementGuard guard;SubtitleBoxPresentation presentation;
        auto good=SubtitleBoxLookahead::Resolve(&first,1,policy,continuity);
        guard.Resolve(good,Source(pixels),50,250);
        const auto acquired=presentation.Consume(good);
        Assert::IsTrue(acquired.detected);
        for(int i=2;i<=6;++i) {
            auto key=first;key.identity=Identity(i);key.analyzed=false;key.text={};
            auto pending=SubtitleBoxLookahead::Resolve(&key,1,policy,continuity);
            Assert::IsTrue(guard.Resolve(pending,Source(pixels),50,250));
            Assert::IsTrue(pending.current.pendingRefresh);
            const auto kept=presentation.Consume(pending);
            Assert::IsTrue(kept.detected);Assert::AreEqual(acquired.cue,kept.cue);
            // Re-presenting the frame must neither spend nor replenish grace.
            guard.Resolve(pending,Source(pixels),50,250);
        }
        auto key=first;key.identity=Identity(7);key.analyzed=false;
        auto expired=SubtitleBoxLookahead::Resolve(&key,1,policy,continuity);
        Assert::IsFalse(guard.Resolve(expired,Source(pixels),50,250));
        Assert::IsFalse(expired.available);
    }
    TEST_METHOD(PendingWorkerRejectsChangedGlyphsBlankFramesAndNewEpoch) {
        for(int scenario=0;scenario<4;++scenario) {
            auto pixels=Pixels(false);Text(pixels,230,305,12);auto first=MeasureWithBar(pixels,1,{},11,21);
            Assert::IsTrue(first.text.detected);
            SubtitlePendingMeasurementGuard guard;
            auto good=SubtitleBoxLookahead::Resolve(&first,1,11,21);
            guard.Resolve(good,Source(pixels),40,250);
            auto key=first;key.identity=Identity(2);key.analyzed=false;
            if(scenario==0)Fill(pixels,0,295,640,360,64);
            if(scenario==1)Text(pixels,230,305,12,true);
            if(scenario==2)++key.identity.viewportGeneration;
            if(scenario==3)key.discontinuity=true;
            auto pending=SubtitleBoxLookahead::Resolve(&key,1,11,21);
            Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        }
    }
    TEST_METHOD(SubtitleWorkerIsNonblockingDeduplicatesAndBoundsPendingWork) {
        SubtitleMeasurementWorker worker;
        std::promise<void> entered,release;auto released=release.get_future().share();
        std::atomic<int> runs{0};std::atomic<bool> wrongThread{false};
        const auto caller=std::this_thread::get_id();
        SubtitleBoxObservation first;first.identity=Identity(1);
        worker.Submit(first,[&](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&){
            wrongThread=std::this_thread::get_id()==caller;++runs;entered.set_value();
            released.wait_for(std::chrono::seconds(2));auto result=first;result.analyzed=true;return result;
        });
        entered.get_future().wait();
        SubtitleBoxObservation out;
        const bool wasReady=worker.TryTake(first,out);
        for(int i=0;i<20;++i)worker.Submit(first,[&](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&){++runs;return first;});
        for(int i=2;i<=20;++i) {
            auto key=first;key.identity=Identity(i);
            worker.Submit(key,[&,key](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&){++runs;auto result=key;result.analyzed=true;return result;});
        }
        release.set_value();
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        // Keep the next eight accepted sources, not the farthest speculative
        // frame. Dropping near frames here made their exact display scans late.
        auto nearestTail=first;nearestTail.identity=Identity(9);bool gotNearestTail=false;
        while(std::chrono::steady_clock::now()<until && !(gotNearestTail=worker.TryTake(nearestTail,out)))std::this_thread::yield();
        worker.Stop();
        auto latest=first;latest.identity=Identity(20);
        Assert::IsFalse(worker.TryTake(latest,out),L"Far future work must not displace the imminent sources.");
        Assert::IsFalse(wasReady);Assert::IsFalse(wrongThread.load());
        Assert::IsTrue(gotNearestTail);Assert::AreEqual(9,runs.load());
        const auto diagnostics=worker.TakeDiagnostics();
        Assert::AreEqual(uint64_t(9),diagnostics.fullScans);
        Assert::AreEqual(uint64_t(11),diagnostics.dropped);
        Assert::AreEqual(uint64_t(0),diagnostics.failed);
        Assert::IsTrue(diagnostics.requestMisses>0);
        Assert::AreEqual(uint64_t(0),worker.TakeDiagnostics().fullScans);
        auto wrong=latest;++wrong.policyGeneration;Assert::IsFalse(worker.TryTake(wrong,out));
        wrong=latest;wrong.optimizationMode=2;Assert::IsFalse(SubtitleMeasurementWorker::SameKey(latest,wrong));
    }
    TEST_METHOD(CachedTrackedMeasurementMustMatchItsIncomingReferenceAndGenerations) {
        constexpr uint64_t policy=11,continuity=21;
        auto strong=Pixels(false);auto first=MeasureWithBar(strong,1,{},policy,continuity);
        const auto firstReference=SubtitleBoxLookahead::AdvanceBarTrackingReference(first);
        auto weak=WeakBottomPixels();
        const auto second=MeasureWithBar(weak,2,firstReference,policy,continuity);
        Assert::IsTrue(second.barTrackingAuthority);
        Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(second,Identity(2),640,360,
            policy,continuity,false,firstReference));
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(second,Identity(2),640,360,
            policy+1,continuity,false,firstReference));
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(second,Identity(2),640,360,
            policy,continuity,false,{}));
        const auto secondReference=SubtitleBoxLookahead::AdvanceBarTrackingReference(second);
        const auto third=MeasureWithBar(weak,3,secondReference,policy,continuity);
        Assert::IsTrue(third.barTrackingAuthority);
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(third,Identity(3),640,360,
            policy,continuity,false,firstReference),
            L"a cached frame cannot restore proof whose immediate predecessor was discarded");
    }
    TEST_METHOD(CachedMeasurementMissMustNotSurviveNewIncomingTrackingReference) {
        constexpr uint64_t policy=11,continuity=21;
        auto strong=Pixels(false);auto first=MeasureWithBar(strong,1,{},policy,continuity);
        const auto newlyAvailableReference=SubtitleBoxLookahead::AdvanceBarTrackingReference(first);
        auto weak=WeakBottomPixels();
        const auto oldAttempt=MeasureWithBar(weak,2,{},policy,continuity);
        const auto freshAttempt=MeasureWithBar(weak,2,newlyAvailableReference,policy,continuity);
        Assert::IsTrue(freshAttempt.barTrackingAuthority,
            L"the pixels can be validated when the newly available predecessor is supplied");
        Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(oldAttempt,Identity(2),640,360,
            policy,continuity,false,newlyAvailableReference),
            L"a cached miss measured without that predecessor must be rescanned");
        const auto wrongPolicy=MeasureWithBar(weak,2,newlyAvailableReference,policy+1,continuity);
        const auto wrongContinuity=MeasureWithBar(weak,2,newlyAvailableReference,policy,continuity+1);
        Assert::IsFalse(wrongPolicy.barTrackingAuthority);
        Assert::IsFalse(wrongContinuity.barTrackingAuthority);
    }
    TEST_METHOD(RepeatedPreviewReuseRejectsChangedSourceAndContext) {
        auto strong=Pixels(false);SubtitleBoxPresentation tracker;
        auto original=Resolve({MeasureWithBar(strong,1,{},7,8)});
        original.newScanMs=3.0;original.newScanFrames=1;
        tracker.Consume(original);
        SubtitleBoxPreview repeated;
        Assert::IsTrue(tracker.ReusePreviewForRepeatedSourceFrame(Identity(1),640,360,false,
            7,8,repeated));
        Assert::AreEqual(0.0,repeated.newScanMs);Assert::AreEqual(0u,repeated.newScanFrames);
        Assert::IsFalse(tracker.ReusePreviewForRepeatedSourceFrame(Identity(2),640,360,false,
            7,8,repeated),L"a different accepted source frame is never treated as cadence reuse");
        Assert::IsFalse(tracker.ReusePreviewForRepeatedSourceFrame(Identity(1),1280,720,false,
            7,8,repeated));
        Assert::IsFalse(tracker.ReusePreviewForRepeatedSourceFrame(Identity(1),640,360,true,
            7,8,repeated));
        Assert::IsFalse(tracker.ReusePreviewForRepeatedSourceFrame(Identity(1),640,360,false,
            9,8,repeated));
        Assert::IsFalse(tracker.ReusePreviewForRepeatedSourceFrame(Identity(1),640,360,false,
            7,9,repeated));
    }
    TEST_METHOD(StrongAspectChangeRejectsOldTrackedPlaneAndAcquiresNewCueGeometry) {
        auto firstPixels=Pixels();Text(firstPixels,240,310,12);
        const auto first=Measure(firstPixels,1);SubtitleBoxPresentation tracker;
        const auto old=tracker.Consume(Resolve({first}));Assert::IsTrue(old.detected);
        auto changed=Pixels();Fill(changed,0,0,640,360,64);Fill(changed,0,60,640,300,300);
        Text(changed,240,294,12);
        const auto current=MeasureWithBar(changed,2,tracker.BarTrackingReferenceFor(Identity(2),640,360));
        Assert::IsTrue(current.barAuthority);
        Assert::IsFalse(current.barTrackingAuthority);
        Assert::AreEqual(60,current.pictureTop);Assert::AreEqual(300,current.pictureBottom);
        const auto next=MeasureWithBar(changed,3,SubtitleBoxLookahead::AdvanceBarTrackingReference(current));
        const auto result=tracker.Consume(Resolve({current,next}));
        Assert::IsTrue(result.detected);Assert::AreNotEqual(old.cue,result.cue);
        Assert::AreEqual(300,tracker.BarTrackingReferenceFor(Identity(3),640,360).pictureBottom);
    }
    TEST_METHOD(OneFailedPlaneValidationBreaksTheLowerReferenceChain) {
        auto strong=Pixels(),weak=WeakBottomPixels();SubtitleBoxPresentation tracker;
        tracker.Consume(Resolve({Measure(strong,1)}));
        const auto current=MeasureWithBar(weak,2,tracker.BarTrackingReferenceFor(Identity(2),640,360));
        const auto next=MeasureWithBar(weak,3,SubtitleBoxLookahead::AdvanceBarTrackingReference(current));
        Assert::IsTrue(tracker.Consume(Resolve({current,next})).detected);
        auto absent=WeakBottomPixels(false);Fill(absent,0,300,640,360,64);
        const auto failed=MeasureWithBar(absent,3,tracker.BarTrackingReferenceFor(Identity(3),640,360));
        Assert::IsFalse(failed.barEvidence.bottomRevalidatedAtReference);
        Assert::IsFalse(tracker.Consume(Resolve({failed})).detected);
        const auto prior=tracker.BarTrackingReferenceFor(Identity(4),640,360);
        Assert::IsTrue(prior.valid,L"unaffected strong top edge remains usable");
        Assert::AreEqual(360,prior.pictureBottom,L"lost bottom history must not survive through the top edge");
        const auto returned=MeasureWithBar(weak,4,prior);
        Assert::IsFalse(returned.barEvidence.bottomRevalidatedAtReference || returned.text.detected);
        const auto reacquired=MeasureWithBar(strong,5,SubtitleBoxLookahead::AdvanceBarTrackingReference(returned));
        Assert::IsTrue(reacquired.barAuthority);Assert::AreEqual(315,reacquired.pictureBottom);
    }
    TEST_METHOD(SubtitleBarsUhdSamplingWorkIsBounded) {
        const int w=3840,h=2160;std::vector<uint8_t> pixels(size_t(w)*h*4,255);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            auto* pixel=&pixels[(size_t(y)*w+x)*4];
            pixel[0]=pixel[1]=pixel[2]=uint8_t(y>=276&&y<1884?100:0);
        }
        const auto evidence=ExtractSubtitleBarEvidence(RgbSource(pixels,w,h));
        Assert::IsTrue(evidence.available);Assert::AreEqual(276,evidence.top);
        Assert::AreEqual(1884,evidence.bottom);Assert::IsTrue(evidence.samples<50000);
    }
    TEST_METHOD(FuturePictureCompanionRequiresCurrentOpaqueBacking) {
        auto current=LowerOnlyCue(1),next=Cue(2,true),last=Cue(3,true);
        current.pictureBottom=next.pictureBottom=last.pictureBottom=336;
        current.text.anchor=current.text.lineBounds[0];
        next.text.anchor=last.text.anchor=current.text.anchor;
        next.text.sourcePanel=last.text.sourcePanel={190,310,450,336};
        const auto rejected=Resolve({current,next,last});
        Assert::AreEqual(1,rejected.text.lineCount,
            L"future black-card evidence cannot create backing on the current frame");
        Assert::IsFalse(rejected.text.sourcePanel.Valid());
        current.text.sourcePanel={190,310,450,336};
        const auto accepted=Resolve({current,next,last});
        Assert::AreEqual(2,accepted.text.lineCount);
        Assert::AreEqual(190,accepted.text.sourcePanel.left);
        Assert::AreEqual(310,accepted.text.sourcePanel.top);
    }
    TEST_METHOD(FirstDisplayedFrameIncludesFutureCompanionWithoutChangingCurrentIdentity) {
        SubtitleBoxPresentation tracker;
        const auto preview=Resolve({Cue(1),Cue(2,true),Cue(3,true)});
        const auto result=tracker.Consume(preview);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::AreEqual(352,result.bounds.bottom);Assert::AreEqual(uint64_t(1),preview.current.identity.acceptedSequence);
        Assert::AreEqual(3u,preview.matchingFrames);
    }
    TEST_METHOD(ThreeVerifiedOpaqueNearBarRowsUseDetectorAdmissionAtEitherPictureEdge) {
        for(bool top:{false,true}) {
            const auto preview=Resolve({NearCardThreeRows(1,top),NearCardThreeRows(2,top),
                NearCardThreeRows(3,top),NearCardThreeRows(4,top)});
            Assert::AreEqual(top?0:2,SubtitleBoxLookahead::BarAnchorLine(preview.current));
            Assert::IsTrue(preview.currentLinesConfirmed);Assert::AreEqual(3,preview.text.lineCount);
            SubtitleBoxPresentation tracker;
            Assert::IsTrue(tracker.Consume(preview).detected);
            // Eligibility remains detector-owned even when scenery merges with
            // the card and its finite cleanup/capture rectangle is unavailable.
            auto ambiguous=NearCardThreeRows(10,top);ambiguous.text.sourcePanel={};ambiguous.text.capturePanel={};
            Assert::AreEqual(top?0:2,SubtitleBoxLookahead::BarAnchorLine(ambiguous));
            for(int rejected=0;rejected<3;++rejected) {
                auto a=NearCardThreeRows(1,top),b=NearCardThreeRows(2,top),
                    c=NearCardThreeRows(3,top),d=NearCardThreeRows(4,top);
                for(auto* o:{&a,&b,&c,&d}) {
                    if(rejected==0)o->text.nearBarEligible=false;
                    if(rejected==1)o->text.nearBarEligibilityMeasured=false;
                    if(rejected==2)o->nearBarDistance=0;
                }
                Assert::IsFalse(Resolve({a,b,c,d}).text.detected,
                    L"cleanup rectangles alone cannot authorize a near-bar crowded cue");
            }
        }
    }
    TEST_METHOD(CrowdedThirdRowCannotPoisonTwoStableBarAnchoredRows) {
        auto crowded=[](uint64_t sequence,int sceneShift) {
            SubtitleBoxObservation o=Cue(sequence,true);
            o.pictureBottom=340;o.text.lineCount=3;
            o.text.lineBounds[2]={280+sceneShift,280,360+sceneShift,296};
            o.text.linePanels[0]={190,310,450,340};
            o.text.linePanels[2]={270+sceneShift,270,370+sceneShift,340};
            o.text.sourcePanel={190,270,450,340};
            o.text.lineSignatures[2].fill(0x77);
            auto ink=std::make_shared<SubtitleInkSnapshot>(*o.ink);
            for(int y=280;y<296;++y)for(int x=280+sceneShift;x<360+sceneShift;++x)
                if((x-(280+sceneShift))%8<4) {
                    const size_t p=size_t(y)*640+x;
                    const uint64_t bit=uint64_t{1}<<(p%64);
                    ink->rawInk[p/64]|=bit;ink->ownedInk[p/64]|=bit;
                }
            o.ink=ink;
            return o;
        };
        const auto steady=Resolve({crowded(10,0),crowded(11,0),crowded(12,0),crowded(13,0)});
        Assert::IsTrue(steady.currentLinesConfirmed);
        Assert::AreEqual(3,steady.text.lineCount,L"a stable genuine third row remains eligible");
        const auto preview=Resolve({crowded(1,0),crowded(2,0),crowded(3,0),crowded(4,3)});
        Assert::AreEqual(4u,preview.matchingFrames);
        Assert::IsTrue(preview.currentLinesConfirmed);
        Assert::AreEqual(2,preview.text.lineCount);
        Assert::AreEqual(310,preview.text.sourcePanel.top,
            L"rejected scene row must not survive in source-card cleanup");
        Assert::AreEqual(318,preview.text.bounds.top);
        Assert::AreEqual(348,preview.text.bounds.bottom);
    }
    TEST_METHOD(TranslucentBackingTopEdgeIsAuxiliaryEvidenceOnly) {
        auto blank=Pixels();
        SubtitleBoxDetector scanner;
        Assert::IsFalse(scanner.Analyze(Source(blank),45,315,1,1).detected);
        auto backed=blank;
        Fill(backed,150,280,500,315,190);
        Text(backed,175,307,22);Text(backed,240,336,12);
        scanner.Reset();
        const auto result=scanner.Analyze(Source(backed),45,315,2,1);
        Assert::IsTrue(result.detected);
        bool found=false;
        for(int line=0;line<result.lineCount;++line)
            if(result.panelTopEdges[line].Valid() &&
                std::abs(result.panelTopEdges[line].y-280)<=2 &&
                result.panelTopEdges[line].supportBasisPoints>=9000)found=true;
        Assert::IsTrue(found,L"detector-owned line has broad backing edge on sampled grid");
        auto narrow=blank;
        Fill(narrow,220,280,250,315,190);
        Text(narrow,175,307,22);Text(narrow,240,336,12);
        scanner.Reset();
        const auto narrowResult=scanner.Analyze(Source(narrow),45,315,3,1);
        Assert::IsTrue(narrowResult.detected);
        for(int line=0;line<narrowResult.lineCount;++line)
            Assert::IsFalse(narrowResult.panelTopEdges[line].Valid(),
                L"a narrow picture edge cannot masquerade as broad subtitle backing");
    }
    TEST_METHOD(MeasuredOpaqueCardSupportsBorderlineCrowdedRowWithoutLegacyContrastEdge) {
        auto crowded=[](uint64_t sequence,int lostEvery,bool measuredCard=true) {
            auto o=NearCardThreeRows(sequence,false);
            // The top row is farthest from the bottom-card anchor.
            auto ink=std::make_shared<SubtitleInkSnapshot>(*o.ink);
            unsigned ordinal=0;const auto& row=o.text.lineBounds[0];
            for(int y=row.top;y<row.bottom;++y)for(int x=row.left;x<row.right;++x)
                if(ink->Get(x,y,true) && lostEvery && (++ordinal%lostEvery)==0) {
                    const size_t p=size_t(y)*ink->width+x;
                    ink->rawInk[p/64]&=~(uint64_t(1)<<(p%64));
                }
            o.ink=ink;
            if(!measuredCard) {
                o.text.capturePanel={};o.text.lineCapturePanels={};
                o.text.panelTopEdges[0]={260,200,440,10000};
            }
            return o;
        };
        const auto backed=Resolve({crowded(1,0),crowded(2,0),crowded(3,0),crowded(4,13)});
        Assert::AreEqual(3,backed.text.lineCount,
            L"a stable measured opaque card supports 90% fixed current/future glyph coverage");
        const auto cardGone=Resolve({crowded(1,0,false),crowded(2,0,false),
            crowded(3,0,false),crowded(4,13,false)});
        Assert::AreEqual(2,cardGone.text.lineCount,
            L"legacy contrast edges cannot replace an empty measured opaque interior");
        const auto glyphsGone=Resolve({crowded(1,0),crowded(2,0),crowded(3,0),crowded(4,4)});
        Assert::IsTrue(glyphsGone.text.lineCount<3,L"black card alone cannot preserve missing glyphs");
    }
    TEST_METHOD(StablePanelCanResolveBorderlineCrowdedRowButNotMissingGlyphs) {
        auto crowded=[](uint64_t sequence,bool panel,int lostEvery) {
            auto o=Cue(sequence,true);o.pictureBottom=340;o.text.lineCount=3;
            const SubtitleBoxRect pictureRow={280,280,360,296};
            o.text.lineBounds[2]=pictureRow;o.text.lineSignatures[2].fill(0x77);
            auto ink=std::make_shared<SubtitleInkSnapshot>(*o.ink);
            unsigned ordinal=0;
            for(int y=pictureRow.top;y<pictureRow.bottom;++y)
                for(int x=pictureRow.left;x<pictureRow.right;++x)
                    if((x-pictureRow.left)%8<4) {
                        const size_t p=size_t(y)*640+x;
                        const uint64_t bit=uint64_t{1}<<(p%64);
                        if(!lostEvery || (++ordinal%lostEvery)!=0)
                            ink->rawInk[p/64]|=bit;
                        ink->ownedInk[p/64]|=bit;
                    }
            o.ink=ink;
            if(panel)o.text.panelTopEdges[2]={270,280,360,8000};
            return o;
        };
        const auto noPanel=Resolve({crowded(1,false,0),crowded(2,false,0),
            crowded(3,false,0),crowded(4,false,13)});
        Assert::AreEqual(2,noPanel.text.lineCount,
            L"borderline picture ink alone is insufficient for the farthest row");
        const auto backed=Resolve({crowded(1,true,0),crowded(2,true,0),
            crowded(3,true,0),crowded(4,true,13)});
        Assert::AreEqual(3,backed.text.lineCount,
            L"stable backing supports a row with independently persistent glyphs");
        const auto panelGone=Resolve({crowded(1,true,0),crowded(2,true,0),
            crowded(3,true,0),crowded(4,false,13)});
        Assert::AreEqual(2,panelGone.text.lineCount);
        const auto glyphsGone=Resolve({crowded(1,true,0),crowded(2,true,0),
            crowded(3,true,0),crowded(4,true,4)});
        Assert::AreEqual(2,glyphsGone.text.lineCount,
            L"stable backing cannot replace current/future glyph proof");
    }
    TEST_METHOD(FutureOnlyThirdRowNeedsCurrentAndFarthestFixedPixelProof) {
        auto withPictureRow=[](uint64_t sequence,int shift,bool grouped) {
            SubtitleBoxObservation o=Cue(sequence,true);o.pictureBottom=340;
            auto ink=std::make_shared<SubtitleInkSnapshot>(*o.ink);
            const SubtitleBoxRect row={280+shift,280,360+shift,296};
            o.text.sourcePanel={190,270,450,340}; // Current opaque backing is independently verified.
            for(int y=row.top;y<row.bottom;++y)for(int x=row.left;x<row.right;++x)
                if((x-row.left)%8<4) {
                    const size_t p=size_t(y)*640+x;const uint64_t bit=uint64_t{1}<<(p%64);
                    ink->rawInk[p/64]|=bit;
                    if(grouped)ink->ownedInk[p/64]|=bit;
                }
            o.ink=ink;
            if(grouped) {
                o.text.lineCount=3;o.text.lineBounds[2]=row;
                o.text.lineSignatures[2].fill(0x77);
                o.text.bounds.top=(std::min)(o.text.bounds.top,row.top);
            }
            return o;
        };
        const auto transient=Resolve({withPictureRow(1,0,false),withPictureRow(2,0,true),
            withPictureRow(3,0,true),withPictureRow(4,3,true)});
        Assert::AreEqual(2,transient.text.lineCount);
        Assert::AreEqual(314,transient.text.bounds.top,
            L"future duplicate grouping cannot bloat the current two-line bounds");
        const auto currentInk=Resolve({withPictureRow(10,0,false),withPictureRow(11,0,true),
            withPictureRow(12,0,true),withPictureRow(13,0,true)});
        Assert::AreEqual(3,currentInk.text.lineCount,
            L"a genuine third row already present in current pixels may be added with full-window proof");
        Assert::AreEqual(280,currentInk.text.bounds.top);
    }
    TEST_METHOD(BlankCurrentFrameCannotBorrowFutureCue) {
        auto blank=Cue(1);blank.text={};blank.ink.reset();SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(Resolve({blank,Cue(2),Cue(3)})).detected);
    }
    TEST_METHOD(OneFrameFlashIsRejectedButLastFrameOfEstablishedCueRemains) {
        auto blank=Cue(2);blank.text={};blank.ink.reset();SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(Resolve({Cue(1),blank})).detected);
        tracker.Reset();Assert::IsTrue(tracker.Consume(Resolve({Cue(1),Cue(2)})).detected);
        blank.identity=Identity(3);
        Assert::IsTrue(tracker.Consume(Resolve({Cue(2),blank})).detected);
        Assert::IsFalse(tracker.Consume(Resolve({blank,Cue(4)})).detected);
    }
    TEST_METHOD(ChangedUpperLineCannotBorrowUnchangedLowerLine) {
        auto first=Cue(1,true),next=Cue(2,true);
        next.text.lineSignatures[0].fill(0xAA);
        Assert::AreEqual(1u,Resolve({first,next}).matchingFrames);
        SubtitleBoxPresentation tracker;Assert::IsFalse(tracker.Consume(Resolve({first,next})).detected);
    }
    TEST_METHOD(AnchorOrderCanChangeWhenSecondLineAppears) {
        auto second=Cue(2,true);
        std::swap(second.text.lineBounds[0],second.text.lineBounds[1]);
        std::swap(second.text.lineSignatures[0],second.text.lineSignatures[1]);
        Assert::AreEqual(2u,Resolve({Cue(1),second}).matchingFrames);
    }
    TEST_METHOD(OneFutureCompanionCannotEnlargePersistentBarCue) {
        const auto transient=Resolve({Cue(1),Cue(2,true),Cue(3),Cue(4)});
        Assert::AreEqual(1,transient.text.lineCount);Assert::AreEqual(336,transient.text.bounds.bottom);
        const auto confirmed=Resolve({Cue(1),Cue(2,true),Cue(3,true),Cue(4)});
        Assert::AreEqual(2,confirmed.text.lineCount);Assert::AreEqual(352,confirmed.text.bounds.bottom);
    }
    TEST_METHOD(UnconfirmedCurrentCompanionCannotAcquireOrEnlargeCue) {
        const auto transient=Resolve({Cue(1,true),Cue(2),Cue(3)});
        Assert::IsFalse(transient.currentLinesConfirmed);
        SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(transient).detected);
        Assert::IsTrue(tracker.Consume(Resolve({Cue(2),Cue(3)})).detected);
        const auto result=tracker.Consume(Resolve({Cue(3,true),Cue(4),Cue(5)}));
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);Assert::AreEqual(336,result.bounds.bottom);
        auto otherTransient=Cue(4,true);otherTransient.text.lineSignatures[1].fill(0xAA);
        const auto another=tracker.Consume(Resolve({otherTransient,Cue(5),Cue(6)}));
        Assert::IsTrue(another.detected);Assert::AreEqual(1,another.lineCount);Assert::AreEqual(result.cue,another.cue);
        const auto clear=tracker.Consume(Resolve({Cue(5),Cue(6)}));
        Assert::IsTrue(clear.detected);Assert::AreEqual(result.cue,clear.cue);
    }
    TEST_METHOD(GeometryFullRasterSequenceAndGenerationChangesBreakProof) {
        for(int kind=0;kind<8;++kind) {
            auto next=Cue(2);
            if(kind==0)next.pictureTop=43;
            if(kind==1)next.barAuthority=false;
            if(kind==2)next.identity.acceptedSequence=3;
            if(kind==3)next.identity.sourceFormatGeneration++;
            if(kind==4)next.identity.viewportGeneration++;
            if(kind==5)next.discontinuity=true;
            if(kind==6)next.identity.transportGeneration++;
            if(kind==7)next.identity.rendererGeneration++;
            Assert::AreEqual(1u,Resolve({Cue(1),next,Cue(3)}).matchingFrames);
        }
    }
    TEST_METHOD(ProofCannotCrossBlankThenResumeSameCue) {
        auto blank=Cue(2);blank.text={};blank.ink.reset();
        Assert::AreEqual(1u,Resolve({Cue(1),blank,Cue(3)}).matchingFrames);
    }
    TEST_METHOD(ExactIdentityPolicyAndContinuityAreRequiredAtConsumption) {
        const auto preview=Resolve({Cue(1),Cue(2)});
        Assert::IsTrue(SubtitleBoxLookahead::IsCurrent(preview,Identity(1),7,8));
        for(int kind=0;kind<7;++kind) {
            auto identity=Identity(1);
            if(kind==0)identity.transportGeneration++;if(kind==1)identity.acceptedSequence++;
            if(kind==2)identity.sourceFrameNumber++;if(kind==3)identity.captureTimestamp++;
            if(kind==4)identity.sourceFormatGeneration++;if(kind==5)identity.viewportGeneration++;
            if(kind==6)identity.rendererGeneration++;
            Assert::IsFalse(SubtitleBoxLookahead::IsCurrent(preview,identity,7,8));
        }
        Assert::IsFalse(SubtitleBoxLookahead::IsCurrent(preview,Identity(1),9,8));
        Assert::IsFalse(SubtitleBoxLookahead::IsCurrent(preview,Identity(1),7,9));
    }
    TEST_METHOD(RepeatDoesNotAddConfirmationOrChangeAlreadyPresentedBounds) {
        SubtitleBoxPresentation tracker;
        const auto first=tracker.Consume(Resolve({Cue(1)}));
        const auto repeated=tracker.Consume(Resolve({Cue(1),Cue(2,true)}));
        Assert::AreEqual(first.bounds.bottom,repeated.bounds.bottom);
        Assert::AreEqual(first.observations,repeated.observations);
        Assert::AreEqual(first.cue,repeated.cue);
    }
    TEST_METHOD(PendingUnknownFutureDoesNotVetoOnsetOrBorrowProofThroughAGap) {
        for(int unknown=0;unknown<6;++unknown) {
            auto pending=Cue(2);pending.text={};
            if(unknown==0)pending.analyzed=false;
            if(unknown==1)pending.analysisRefresh=true;
            if(unknown==2)pending.pendingRefresh=true;
            if(unknown==3)pending.text.workLimit=true;
            if(unknown==4)pending.discontinuity=true;
            if(unknown==5)pending.identity.transportGeneration++;
            const auto preview=Resolve({Cue(1),pending,Cue(3)});
            Assert::IsTrue(preview.available);Assert::IsFalse(preview.futureAvailable);
            Assert::AreEqual(0u,preview.followingCount);Assert::AreEqual(1u,preview.matchingFrames);
            SubtitleBoxPresentation tracker;
            Assert::IsTrue(tracker.Consume(preview).detected);
        }
        auto blank=Cue(2);blank.text={};
        const auto knownAbsent=Resolve({Cue(1),blank});
        Assert::IsTrue(knownAbsent.futureAvailable);
        SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(knownAbsent).detected,
            L"an independently measured blank frame retains onset rejection");
    }
    TEST_METHOD(ZeroFutureFramesUsesImmediateCurrentDetection) {
        SubtitleBoxPresentation tracker;const auto result=tracker.Consume(Resolve({Cue(1)}));
        Assert::IsTrue(result.detected);Assert::AreEqual(1u,result.observations);
    }
    TEST_METHOD(DriftingSegmentationBoundsCannotMoveUnchangedGlyphPixels) {
        SubtitleBoxPresentation tracker;SubtitleBoxResult acquired;
        for(int shift=0;shift<20;++shift) {
            auto cue=Cue(shift+1);cue.text.bounds.left+=shift;cue.text.bounds.right+=shift;
            cue.text.lineBounds[0].left+=shift;cue.text.lineBounds[0].right+=shift;
            const auto result=tracker.Consume(Resolve({cue}));
            if(!shift)acquired=result;
            Assert::IsTrue(result.detected);Assert::AreEqual(acquired.cue,result.cue);
            Assert::IsTrue(SubtitleBoxLookahead::SameLine(acquired.bounds,result.bounds,0));
        }
    }
    TEST_METHOD(SlowDriftCannotAccumulateAnUnboundedCueBox) {
        SubtitleBoxPresentation tracker;uint64_t firstCue=0,lastCue=0;
        for(int shift=0;shift<20;++shift) {
            auto cue=Cue(shift+1);cue.text.bounds.left+=shift;cue.text.bounds.right+=shift;
            cue.text.lineBounds[0].left+=shift;cue.text.lineBounds[0].right+=shift;
            auto shifted=std::make_shared<SubtitleInkSnapshot>(*cue.ink);
            std::fill(shifted->rawInk.begin(),shifted->rawInk.end(),0);
            std::fill(shifted->ownedInk.begin(),shifted->ownedInk.end(),0);
            for(int y=0;y<shifted->height;++y)for(int x=shift;x<shifted->width;++x) {
                const size_t pixel=size_t(y)*shifted->width+x;
                if(cue.ink->Get(x-shift,y))shifted->rawInk[pixel/64]|=uint64_t{1}<<(pixel%64);
                if(cue.ink->Get(x-shift,y,true))shifted->ownedInk[pixel/64]|=uint64_t{1}<<(pixel%64);
            }
            cue.ink=shifted;
            const auto result=tracker.Consume(Resolve({cue}));
            if(!shift)firstCue=result.cue;lastCue=result.cue;
            Assert::AreEqual(248,result.bounds.right-result.bounds.left);
        }
        Assert::IsTrue(firstCue!=lastCue);
    }
    TEST_METHOD(FrozenBoundsMustContainCurrentGlyphsEvenWithinIdentityTolerance) {
        SubtitleBoxPresentation tracker;auto cue=Cue(1);cue.height=2160;
        cue.text.bounds=cue.text.lineBounds[0];
        const auto first=tracker.Consume(Resolve({cue}));
        cue.identity=Identity(2);cue.text.lineBounds[0].left+=4;cue.text.lineBounds[0].right+=4;
        cue.text.bounds=cue.text.lineBounds[0];
        const auto shifted=tracker.Consume(Resolve({cue}));
        Assert::IsTrue(shifted.cue!=first.cue);Assert::IsTrue(shifted.bounds.right>=cue.text.lineBounds[0].right);
    }
    TEST_METHOD(UnconfirmedCompanionCannotHideEscapeOfAnEstablishedLine) {
        SubtitleBoxPresentation tracker;auto first=Cue(1);first.height=2160;
        first.text.bounds=first.text.lineBounds[0];
        Assert::IsTrue(tracker.Consume(Resolve({first})).detected);
        auto shifted=Cue(2,true);shifted.height=2160;
        shifted.text.lineBounds[0].left+=4;shifted.text.lineBounds[0].right+=4;
        auto future=Cue(3);future.height=2160;future.text.lineBounds[0]=shifted.text.lineBounds[0];
        const auto result=tracker.Consume(Resolve({shifted,future}));
        Assert::IsFalse(result.detected,L"cannot retain old box after known glyphs escape it");
    }
    TEST_METHOD(RealPixelsRecoverContaminatedBarsAndBothBoundaryCrossingLines) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        const auto first=Measure(pixels,1),second=Measure(pixels,2);
        Assert::IsTrue(first.barAuthority);Assert::IsTrue(first.text.detected);Assert::AreEqual(2,first.text.lineCount);
        SubtitleBoxPresentation tracker;const auto result=tracker.Consume(Resolve({first,second}));
        Assert::IsTrue(result.detected);Assert::IsTrue(result.bounds.top<=307);Assert::IsTrue(result.bounds.bottom>=350);
        auto expanded=Pixels(true);Text(expanded,175,307,22);Text(expanded,240,336,12);
        const auto full=Measure(expanded,3);
        Assert::IsFalse(full.barAuthority);Assert::IsFalse(tracker.Consume(Resolve({full})).detected);
    }
    TEST_METHOD(RealPixelsDetectEqualBoundsChangedTextDespiteUnchangedCompanion) {
        auto first=Pixels(),second=Pixels();
        Text(first,175,307,22);Text(first,240,336,12);
        Text(second,175,307,22,true);Text(second,240,336,12);
        const auto a=Measure(first,1),b=Measure(second,2);
        Assert::IsTrue(a.barAuthority,L"original cue has fresh bars");
        Assert::IsTrue(b.barAuthority,L"changed cue has fresh bars");
        Assert::IsTrue(a.text.detected,L"original cue detected");
        Assert::IsTrue(b.text.detected,L"changed cue detected");
        Assert::AreEqual(2,a.text.lineCount,L"original cue lines");
        Assert::AreEqual(2,b.text.lineCount,L"changed cue lines");
        Assert::IsFalse(SubtitleBoxLookahead::SameCue(a,b));
        Assert::AreEqual(1u,Resolve({a,b}).matchingFrames);
    }
    TEST_METHOD(RealPixelsAspectChangesUseTheCurrentBoundaryInBothDirections) {
        auto narrow=Pixels();Fill(narrow,0,0,640,360,64);Fill(narrow,0,60,640,300,300);
        Text(narrow,200,300,18);
        auto wide=Pixels();Text(wide,200,300,18);
        const auto n1=Measure(narrow,1),n2=Measure(narrow,2);
        Assert::IsTrue(n1.barAuthority&&n1.text.detected);
        SubtitleBoxPresentation tracker;const auto first=tracker.Consume(Resolve({n1,n2}));
        Assert::IsTrue(first.detected);
        const auto w3=Measure(wide,3),w4=Measure(wide,4);
        Assert::IsTrue(w3.barAuthority);Assert::IsFalse(w3.text.detected);
        Assert::IsFalse(tracker.Consume(Resolve({w3,w4})).detected);
        const auto n5=Measure(narrow,5),n6=Measure(narrow,6);
        const auto reacquired=tracker.Consume(Resolve({n5,n6}));
        Assert::IsTrue(reacquired.detected);Assert::IsTrue(first.cue!=reacquired.cue);
        Assert::AreEqual(0u,Resolve({w4,n5,n6}).matchingFrames);
        auto stableNarrow=Pixels();Fill(stableNarrow,0,0,640,360,64);Fill(stableNarrow,0,60,640,300,300);
        auto stableWide=Pixels();Text(stableNarrow,200,336,18);Text(stableWide,200,336,18);
        const auto sw=Measure(stableWide,10),sn=Measure(stableNarrow,11);
        const std::wstring geometry = L"wide="+std::to_wstring(sw.pictureTop)+L","+
            std::to_wstring(sw.pictureBottom)+L" detected="+std::to_wstring(sw.text.detected)+
            L" narrow="+std::to_wstring(sn.pictureTop)+L","+std::to_wstring(sn.pictureBottom)+
            L" detected="+std::to_wstring(sn.text.detected);
        Logger::WriteMessage(geometry.c_str());
        Assert::IsTrue(sw.barAuthority,L"wide lower-bar cue authority");
        Assert::IsTrue(sn.barAuthority,L"narrow lower-bar cue authority");
        Assert::IsTrue(sw.text.detected,L"wide lower-bar cue detected");
        Assert::IsTrue(sn.text.detected,L"narrow lower-bar cue detected");
        Assert::AreEqual(1u,Resolve({sw,sn}).matchingFrames);
    }
    TEST_METHOD(NativePausedReplayReportsSubtitleBarReferenceSensitivity) {
        char* pathValue=nullptr;size_t pathSize=0;
        _dupenv_s(&pathValue,&pathSize,"VP_SUBTITLE_REPLAY_PATH");
        const std::string path=pathValue?pathValue:"";std::free(pathValue);
        if(path.empty()) { Logger::WriteMessage(L"Native replay diagnostic skipped: VP_SUBTITLE_REPLAY_PATH is not set."); return; }
        std::ifstream input(path,std::ios::binary);
        if(!input) { Logger::WriteMessage(L"Native replay diagnostic skipped: capture file is unavailable."); return; }
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        Assert::AreEqual(size_t(3840)*2160*4,bytes.size(),L"capture must match 3840x2160 4:2:2");
        AnalysisLumaSource source;source.data=bytes.data();source.dataBytes=bytes.size();
        source.width=3840;source.height=2160;source.rowBytes=source.chromaRowBytes=7680;
        source.format=AnalysisLumaFormat::DecodedDolbyVision422;
        source.encoding=VideoFrameEncoding::R210;source.colorspace=ColorSpace::BT_2020;
        source.range=AnalysisLumaRange::Full;source.generation=2;
        Assert::IsTrue(source.IsValid(),L"captured native source must be valid");
        const auto current=ExtractSubtitleBarEvidence(source);
        std::wstring report=L"current available="+std::to_wstring(current.available)+L" top/bottom="+
            std::to_wstring(current.top)+L"/"+std::to_wstring(current.bottom)+L" floor="+
            std::to_wstring(current.topFloor)+L"/"+std::to_wstring(current.bottomFloor)+L" samples="+
            std::to_wstring(current.samples)+L" reason="+std::wstring(current.reason,current.reason+strlen(current.reason));
        Logger::WriteMessage(report.c_str());

        for(const auto pair:{std::pair<int,int>{268,1892},{276,1884}}) {
            const auto exact=RevalidateSubtitleBarEvidence(source,current,pair.first,pair.second);
            if(pair.first==268) Assert::IsFalse(exact.topRevalidatedAtReference||exact.bottomRevalidatedAtReference,
                L"the quantized retained scope must fail exact proof for both native edges");
            else Assert::IsTrue(exact.bottomRevalidatedAtReference,
                L"the captured physical bottom edge must pass exact proof");
            report=L"exact ref="+std::to_wstring(pair.first)+L"/"+std::to_wstring(pair.second)+
                L" top/bottom="+std::to_wstring(exact.topRevalidatedAtReference)+L"/"+
                std::to_wstring(exact.bottomRevalidatedAtReference)+L" support="+
                std::to_wstring(exact.topReferenceBoundarySupport)+L"/"+
                std::to_wstring(exact.bottomReferenceBoundarySupport)+L" black="+
                std::to_wstring(exact.topReferenceBlackFraction)+L"/"+
                std::to_wstring(exact.bottomReferenceBlackFraction)+L" intrusion="+
                std::to_wstring(exact.topReferenceMaxIntrusion)+L"/"+
                std::to_wstring(exact.bottomReferenceMaxIntrusion);
            Logger::WriteMessage(report.c_str());
        }
        SubtitleBoxDetector scanner;
        const auto provisional=scanner.Analyze(source,268,1892,2168,3);auto ink=scanner.InkSnapshot();int plane=0;
        for(bool withCandidateInk:{false,true}) {
            const auto started=std::chrono::steady_clock::now();
            SubtitleBarEvidence last;
            for(int i=0;i<20;++i) last=ResolveCurrentSubtitleAnalysisBars(source,current,268,1892,
                withCandidateInk?ink.get():nullptr,withCandidateInk?&provisional.anchor:nullptr);
            const double elapsed=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-started).count();
            report=L"resolver benchmark candidateMask="+std::to_wstring(withCandidateInk)+
                L" iterations=20 avgMs="+std::to_wstring(elapsed/20.0)+
                L" samplesEach="+std::to_wstring(last.samples)+L" probesEach="+
                std::to_wstring(last.nearbyProbes)+L" available="+
                std::to_wstring(last.available)+L" planes="+
                std::to_wstring(last.top)+L"/"+std::to_wstring(last.bottom);
            Logger::WriteMessage(report.c_str());
            Assert::IsTrue(last.nearbyProbes<=8,L"nearby fallback is capped at four probes per missing side");
        }
        const bool strip=ProveSubtitleCaptionStrip(source,provisional,ink.get(),true,current.bottomFloor,plane);
        report=L"provisional detected="+std::to_wstring(provisional.detected)+L" reason="+
            std::wstring(provisional.diagnosticReason,provisional.diagnosticReason+strlen(provisional.diagnosticReason))+
            L" workLimit="+std::to_wstring(provisional.workLimit)+L" anchor="+
            std::to_wstring(provisional.anchor.left)+L","+std::to_wstring(provisional.anchor.top)+L"-"+
            std::to_wstring(provisional.anchor.right)+L","+std::to_wstring(provisional.anchor.bottom)+
            L" lines="+std::to_wstring(provisional.lineCount)+L" snapshot="+std::to_wstring(bool(ink))+
            L" complete="+std::to_wstring(ink&&ink->rawEvidenceComplete)+L" captionStrip="+
            std::to_wstring(strip)+L" plane="+std::to_wstring(plane);
        Logger::WriteMessage(report.c_str());
        for(const auto pair:{std::pair<int,int>{268,1892},{272,1884},{276,1884},
            {276,1880},{276,1888},{272,1888},{276,1892}}) {
            const auto bars=ResolveCurrentSubtitleAnalysisBars(source,current,pair.first,pair.second);
            report=L"reference="+std::to_wstring(pair.first)+L"/"+std::to_wstring(pair.second)+L" resolved="+
                std::to_wstring(bars.top)+L"/"+std::to_wstring(bars.bottom)+L" available="+
                std::to_wstring(bars.available)+L" reason="+std::wstring(bars.reason,bars.reason+strlen(bars.reason))+
                L" refSupport="+std::to_wstring(bars.topReferenceBoundarySupport)+L"/"+
                std::to_wstring(bars.bottomReferenceBoundarySupport)+L" black="+
                std::to_wstring(bars.topReferenceBlackFraction)+L"/"+
                std::to_wstring(bars.bottomReferenceBlackFraction)+L" intrusion="+
                std::to_wstring(bars.topReferenceMaxIntrusion)+L"/"+
                std::to_wstring(bars.bottomReferenceMaxIntrusion);
            Logger::WriteMessage(report.c_str());
        }
        for(const auto pair:{std::pair<int,int>{268,1892},{272,1884},{276,1884}}) {
            ActivePictureFrameIdentity identity{2,2168,2168,2168000,2,3,4,55};
            auto previous=identity;previous.acceptedSequence=2167;previous.sourceFrameNumber=2167;previous.captureTimestamp=2167000;
            SubtitleBoxObservation key;key.identity=identity;key.width=3840;key.height=2160;
            key.sharedPicture.required=true;key.sharedPicture.identity=previous;
            key.sharedPicture.bounds={0,pair.first,3840,pair.second,3840,2160};
            SubtitleBarTrackingReference prior;prior.valid=true;prior.identity=previous;prior.width=3840;prior.height=2160;
            prior.pictureTop=276;prior.pictureBottom=1884;prior.policyGeneration=key.policyGeneration;
            prior.continuityGeneration=key.continuityGeneration;
            SubtitleMeasurementSampler sampler;const auto measured=sampler.Measure(source,key,prior,60);
            report=L"worker reference="+std::to_wstring(pair.first)+L"/"+std::to_wstring(pair.second)+
                L" analyzed="+std::to_wstring(measured.analyzed)+L" fullMeasureMs="+std::to_wstring(measured.analysisMs)+L" authority="+
                std::to_wstring(measured.barAuthority)+L" geometry="+
                std::to_wstring(measured.pictureTop)+L"/"+std::to_wstring(measured.pictureBottom)+
                L" text="+std::to_wstring(measured.text.detected)+L" reason="+
                std::wstring(measured.text.diagnosticReason,measured.text.diagnosticReason+strlen(measured.text.diagnosticReason))+
                L" barReason="+std::wstring(measured.barEvidence.reason,measured.barEvidence.reason+strlen(measured.barEvidence.reason));
            Logger::WriteMessage(report.c_str());
            if(pair.first==268 && pair.second==1892) {
                Assert::IsTrue(measured.barAuthority,L"nearby current-pixel proof supplies subtitle analysis authority");
                Assert::IsTrue(measured.text.detected,L"the captured cue is recognized under the original retained scope");
                Assert::AreEqual(268,measured.sharedPicture.bounds.top,L"subtitle analysis must not rewrite shared AR geometry");
                Assert::AreEqual(1892,measured.sharedPicture.bounds.bottom,L"subtitle analysis must not rewrite shared AR geometry");
                Assert::IsFalse(measured.subtitleArProof.accepted,L"nearby subtitle edge recovery cannot promote subtitle evidence into AR");
                auto first=measured,second=measured,third=measured,fourth=measured;
                SubtitleBoxObservation* frames[]={&first,&second,&third,&fourth};
                for(unsigned i=0;i<4;++i) {
                    frames[i]->identity.acceptedSequence=2168+i;
                    frames[i]->identity.sourceFrameNumber=2168+i;
                    frames[i]->identity.captureTimestamp=2168000+i*16667;
                    frames[i]->sharedPicture.identity=frames[i]->identity;
                }
                const auto nativePreview=Resolve({first,second,third,fourth});
                SubtitleBoxPresentation presentation;
                const auto accepted=presentation.Consume(nativePreview);
                Assert::IsTrue(accepted.detected,L"repeated exact paused-source pixels confirm the native cue at presentation");
                const auto geometry=ComputeSubtitleCutPaste(accepted.bounds,3840,2160,
                    measured.pictureTop,measured.pictureBottom,{},0,&accepted.sourcePanel,
                    measured.nearBarDistance,&accepted.capturePanel,accepted.nearBarEligible);
                Assert::IsTrue(geometry.valid,L"accepted native subtitle produces cut-paste geometry");
                Assert::AreEqual(measured.pictureTop,geometry.pictureTop);
                Assert::AreEqual(measured.pictureBottom,geometry.pictureBottom);
                Assert::AreEqual(268,measured.sharedPicture.bounds.top);
                Assert::AreEqual(1892,measured.sharedPicture.bounds.bottom);
            }
        }
    }
    TEST_METHOD(NativePausedReplayRuntimeNearBar20ReuseDiagnostic) {
        char* pathValue=nullptr;size_t pathSize=0;
        _dupenv_s(&pathValue,&pathSize,"VP_SUBTITLE_REPLAY_PATH");
        const std::string path=pathValue?pathValue:"";std::free(pathValue);
        if(path.empty()) { Logger::WriteMessage(L"Runtime near-bar diagnostic skipped: VP_SUBTITLE_REPLAY_PATH is not set."); return; }
        std::ifstream input(path,std::ios::binary);
        if(!input) { Logger::WriteMessage(L"Runtime near-bar diagnostic skipped: capture file is unavailable."); return; }
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        Assert::AreEqual(size_t(3840)*2160*4,bytes.size(),L"capture must match 3840x2160 4:2:2");
        AnalysisLumaSource source;source.data=bytes.data();source.dataBytes=bytes.size();
        source.width=3840;source.height=2160;source.rowBytes=source.chromaRowBytes=7680;
        source.format=AnalysisLumaFormat::DecodedDolbyVision422;
        source.encoding=VideoFrameEncoding::R210;source.colorspace=ColorSpace::BT_2020;
        source.range=AnalysisLumaRange::Full;source.generation=2;
        Assert::IsTrue(source.IsValid(),L"captured native source must be valid");
        const auto current=ExtractSubtitleBarEvidence(source);
        SubtitleBoxDetector detector;
        detector.SetNearBarDistance(20);
        detector.SetOptimizationMode(0);
        detector.SetScriptHint(SubtitleScriptHint::Latin);
        auto provisional=detector.Analyze(source,268,1892,2168,3);
        auto ink=detector.InkSnapshot();
        auto countBits=[](const std::vector<uint64_t>& words) {
            size_t count=0;
            for(uint64_t word:words) while(word) { word&=word-1; ++count; }
            return count;
        };
        const size_t rawInk=ink?countBits(ink->rawInk):0;
        const size_t ownedInk=ink?countBits(ink->ownedInk):0;
        std::wstring report=L"candidate mode=0 nearBar=20 script=Latin detected="+
            std::to_wstring(provisional.detected)+L" reason="+
            std::wstring(provisional.diagnosticReason,provisional.diagnosticReason+strlen(provisional.diagnosticReason))+
            L" anchor="+std::to_wstring(provisional.anchor.left)+L","+
            std::to_wstring(provisional.anchor.top)+L"-"+std::to_wstring(provisional.anchor.right)+L","+
            std::to_wstring(provisional.anchor.bottom)+L" ink_raw="+std::to_wstring(rawInk)+
            L" ink_owned="+std::to_wstring(ownedInk)+L" complete="+
            std::to_wstring(ink&&ink->rawEvidenceComplete);
        Logger::WriteMessage(report.c_str());
        const ActivePictureFrameIdentity identity{2,2168,2168,2168000,2,3,4,55};
        auto previous=identity;previous.acceptedSequence=2167;previous.sourceFrameNumber=2167;
        previous.captureTimestamp=2167000;
        SubtitleBarTrackingReference prior;prior.valid=true;prior.identity=previous;
        prior.width=3840;prior.height=2160;prior.pictureTop=276;prior.pictureBottom=1884;
        prior.policyGeneration=0;prior.continuityGeneration=0;
        for(const auto pair:{std::pair<int,int>{268,1892},{272,1884},{276,1884}}) {
            const auto exact=RevalidateSubtitleBarEvidence(source,current,pair.first,pair.second);
            const auto unmasked=ResolveCurrentSubtitleAnalysisBars(source,current,pair.first,pair.second,
                nullptr,nullptr,false);
            const auto masked=ResolveCurrentSubtitleAnalysisBars(source,current,pair.first,pair.second,
                ink.get(),&provisional.anchor,false);
            auto key=SubtitleBoxObservation{};key.identity=identity;key.width=3840;key.height=2160;
            key.nearBarDistance=20;key.optimizationMode=0;key.scriptHint=SubtitleScriptHint::Latin;
            key.sharedPicture.required=true;key.sharedPicture.identity=previous;
            key.sharedPicture.bounds={0,pair.first,3840,pair.second,3840,2160};
            SubtitleMeasurementSampler sampler;
            const auto withPrior=sampler.Measure(source,key,prior,24);
            const auto withoutPrior=sampler.Measure(source,key,{},24);
            const bool reuseWithPrior=SubtitleBoxLookahead::CanReuseMeasurement(withPrior,identity,
                3840,2160,0,0,false,prior,SubtitleScriptHint::Latin);
            const bool reuseWithoutPrior=SubtitleBoxLookahead::CanReuseMeasurement(withoutPrior,identity,
                3840,2160,0,0,false,{},SubtitleScriptHint::Latin);
            report=L"plane="+std::to_wstring(pair.first)+L"/"+std::to_wstring(pair.second)+
                L" exact="+std::to_wstring(exact.topRevalidatedAtReference)+L"/"+
                std::to_wstring(exact.bottomRevalidatedAtReference)+L" exactSupport="+
                std::to_wstring(exact.topReferenceBoundarySupport)+L"/"+
                std::to_wstring(exact.bottomReferenceBoundarySupport)+
                L" unmasked="+std::to_wstring(unmasked.available)+L":"+
                std::to_wstring(unmasked.top)+L"/"+std::to_wstring(unmasked.bottom)+
                L" masked="+std::to_wstring(masked.available)+L":"+
                std::to_wstring(masked.top)+L"/"+std::to_wstring(masked.bottom)+
                L" candidateProbes="+std::to_wstring(masked.nearbyProbes)+
                L" prior analyzed/authority/geometry/text="+std::to_wstring(withPrior.analyzed)+L"/"+
                std::to_wstring(withPrior.barAuthority)+L":"+std::to_wstring(withPrior.pictureTop)+L"/"+
                std::to_wstring(withPrior.pictureBottom)+L"/"+std::to_wstring(withPrior.text.detected)+
                L" reason="+std::wstring(withPrior.text.diagnosticReason,
                    withPrior.text.diagnosticReason+strlen(withPrior.text.diagnosticReason))+
                L" barReason="+std::wstring(withPrior.barEvidence.reason,
                    withPrior.barEvidence.reason+strlen(withPrior.barEvidence.reason))+
                L" reuse="+std::to_wstring(reuseWithPrior)+L"/"+
                std::to_wstring(reuseWithoutPrior)+L" empty analyzed/authority/geometry/text="+
                std::to_wstring(withoutPrior.analyzed)+L"/"+std::to_wstring(withoutPrior.barAuthority)+L":"+
                std::to_wstring(withoutPrior.pictureTop)+L"/"+std::to_wstring(withoutPrior.pictureBottom)+
                L"/"+std::to_wstring(withoutPrior.text.detected)+L" emptyReason="+
                std::wstring(withoutPrior.text.diagnosticReason,
                    withoutPrior.text.diagnosticReason+strlen(withoutPrior.text.diagnosticReason));
            Logger::WriteMessage(report.c_str());
        }
    }
    TEST_METHOD(NativeReplayLocalGlyphProofSupportsSubtitleOnlyPlacement) {
        char* pathValue=nullptr;size_t pathSize=0;
        _dupenv_s(&pathValue,&pathSize,"VP_SUBTITLE_REPLAY_PATH");
        const std::string path=pathValue?pathValue:"";std::free(pathValue);
        if(path.empty()) { Logger::WriteMessage(L"Native local-glyph regression skipped: VP_SUBTITLE_REPLAY_PATH is not set."); return; }
        std::ifstream input(path,std::ios::binary);
        if(!input) { Logger::WriteMessage(L"Native local-glyph regression skipped: capture file is unavailable."); return; }
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        Assert::AreEqual(size_t(3840)*2160*4,bytes.size());
        AnalysisLumaSource source;source.data=bytes.data();source.dataBytes=bytes.size();
        source.width=3840;source.height=2160;source.rowBytes=source.chromaRowBytes=7680;
        source.format=AnalysisLumaFormat::DecodedDolbyVision422;
        source.encoding=VideoFrameEncoding::R210;source.colorspace=ColorSpace::BT_2020;
        source.range=AnalysisLumaRange::Full;source.generation=2;
        Assert::IsTrue(source.IsValid());

        ActivePictureFrameIdentity identity{1,437,442,859326510460ULL,
            16737396327766010546ULL,3,1,3440028774926749422ULL};
        SubtitleBoxObservation key;key.identity=identity;key.width=3840;key.height=2160;
        key.policyGeneration=2;key.continuityGeneration=5;key.nearBarDistance=20;
        key.optimizationMode=0;key.scriptHint=SubtitleScriptHint::Latin;
        key.sharedPicture.required=true;key.sharedPicture.identity=identity;
        key.sharedPicture.bounds={0,276,3840,1884,3840,2160};
        SubtitleBarTrackingReference prior;prior.valid=true;prior.identity=identity;
        --prior.identity.acceptedSequence;--prior.identity.sourceFrameNumber;
        --prior.identity.captureTimestamp;
        prior.width=3840;prior.height=2160;prior.pictureTop=276;prior.pictureBottom=1884;
        prior.policyGeneration=key.policyGeneration;prior.continuityGeneration=key.continuityGeneration;
        prior.scriptHint=key.scriptHint;
        SubtitleMeasurementSampler sampler;
        const auto measured=sampler.Measure(source,key,prior,24);
        std::wstring report=L"local proof="+
            std::wstring(measured.glyphLocalProofReason,
                measured.glyphLocalProofReason+std::strlen(measured.glyphLocalProofReason))+
            L" detected="+std::to_wstring(measured.text.detected)+L" anchor="+
            std::to_wstring(measured.text.anchor.left)+L","+std::to_wstring(measured.text.anchor.top)+L"-"+
            std::to_wstring(measured.text.anchor.right)+L","+std::to_wstring(measured.text.anchor.bottom)+
            L" verified="+std::to_wstring(measured.glyphLocalVerifiedBounds.left)+L","+
            std::to_wstring(measured.glyphLocalVerifiedBounds.top)+L"-"+
            std::to_wstring(measured.glyphLocalVerifiedBounds.right)+L","+
            std::to_wstring(measured.glyphLocalVerifiedBounds.bottom);
        Logger::WriteMessage(report.c_str());
        Assert::IsTrue(measured.analyzed && measured.barAuthority);
        Assert::IsTrue(measured.text.detected && !measured.text.workLimit);
        Assert::IsTrue(measured.sharedProbeAttempted && measured.sharedProbeDetected);
        Assert::IsTrue(measured.glyphLocalBottom && !measured.glyphLocalTop,
            L"the live [Sighs] cue should get a fresh local bottom-backing proof");
        Assert::IsTrue(measured.barEvidence.captionBackedBottom);
        Assert::AreEqual(276,measured.barEvidence.top);
        Assert::AreEqual(2160,measured.barEvidence.bottom,
            L"local glyph backing must not rewrite the unproved physical bottom edge");
        Assert::AreEqual(1884,measured.pictureBottom,
            L"subtitle placement may use the retained hint without changing the shared AR snapshot");
        Assert::AreEqual(1884,measured.sharedPicture.bounds.bottom);
        Assert::IsFalse(measured.independentBarAnalysis);
        Assert::IsFalse(measured.subtitleArProof.accepted,
            L"a subtitle-only local proof cannot authorize crop/AR geometry");
        Assert::IsTrue(measured.glyphLocalVerifiedBounds.Valid());
        const auto nativeCapture=SubtitleGlyphCaptureBounds(measured.text,source.width,source.height);
        Assert::IsTrue(nativeCapture.left>=measured.glyphLocalVerifiedBounds.left &&
            nativeCapture.top>=measured.glyphLocalVerifiedBounds.top &&
            nativeCapture.right<=measured.glyphLocalVerifiedBounds.right &&
            nativeCapture.bottom<=measured.glyphLocalVerifiedBounds.bottom,
            L"fresh local pixels cover the compositor capture mask");
        Assert::IsFalse(ConfirmedSubtitleAbsentForPresentation(measured,{},identity));

        auto preview=SubtitleBoxLookahead::Resolve(&measured,1,key.policyGeneration,key.continuityGeneration);
        SubtitleCutPastePresentation compositor;
        const auto geometry=compositor.Consume(preview.text,preview);
        Assert::IsTrue(geometry.valid,L"the local proof supports bounded subtitle placement");
        Assert::AreEqual(276,geometry.pictureTop);Assert::AreEqual(1884,geometry.pictureBottom);
        Assert::IsTrue(geometry.generatedCleanup.Valid());
        Assert::IsTrue(geometry.generatedCleanup.left>=0 && geometry.generatedCleanup.right<=source.width);
        Assert::IsTrue(geometry.generatedCleanup.top>=0 && geometry.generatedCleanup.bottom<=source.height);
        Assert::IsTrue(geometry.generatedCleanup.right-geometry.generatedCleanup.left<source.width*4/5,
            L"cleanup remains limited to this cue instead of expanding across the frame");
    }

    TEST_METHOD(LocalGlyphBackingProofIsFreshBoundedAndSymmetricAcrossScripts) {
        for(bool top:{false,true})for(SubtitleScriptHint script:{SubtitleScriptHint::Latin,SubtitleScriptHint::Thai}) {
            auto pixels=Pixels();
            const int plane=top?45:315;
            LocalBacking(pixels,plane);
            Text(pixels,220,top?36:307,script==SubtitleScriptHint::Thai?4:12);
            const auto source=Source(pixels);
            SubtitleBoxDetector detector;detector.SetNearBarDistance(20);detector.SetScriptHint(script);
            const auto candidate=detector.Analyze(source,45,315,1,1);
            const auto ink=detector.InkSnapshot();
            Assert::IsTrue(candidate.detected && !candidate.held && !candidate.workLimit,
                L"fixture must contain a fresh detector-owned caption candidate");
            Assert::IsTrue(candidate.nearBarEligibilityMeasured && candidate.nearBarEligible);
            const auto proof=ProveLocalSubtitleGlyphBacking(source,candidate,ink.get(),plane,!top);
            if(!proof.accepted) {
                const wchar_t* scriptName=script==SubtitleScriptHint::Latin?L"Latin":
                    script==SubtitleScriptHint::Arabic?L"Arabic":L"Thai";
                const std::wstring diagnostic=L"local proof rejected script="+std::wstring(scriptName)+
                    L" top="+(top?L"true":L"false")+L" reason="+std::wstring(proof.reason,proof.reason+std::strlen(proof.reason))+
                    L" anchor="+std::to_wstring(candidate.anchor.left)+L","+
                    std::to_wstring(candidate.anchor.top)+L"-"+std::to_wstring(candidate.anchor.right)+L","+
                    std::to_wstring(candidate.anchor.bottom);
                Logger::WriteMessage(diagnostic.c_str());
            }
            Assert::IsTrue(proof.accepted,L"local backing proof supports mirrored edges and supported scripts");
            const auto capture=SubtitleGlyphCaptureBounds(candidate,source.width,source.height);
            Assert::IsTrue(capture.left>=proof.verifiedBounds.left && capture.top>=proof.verifiedBounds.top &&
                capture.right<=proof.verifiedBounds.right && capture.bottom<=proof.verifiedBounds.bottom,
                L"the local proof covers the actual glyph capture mask");

            auto nonCrossing=candidate;nonCrossing.anchor.bottom=plane;
            Assert::IsFalse(ProveLocalSubtitleGlyphBacking(source,nonCrossing,ink.get(),plane,!top).accepted,
                L"a line that does not cross the proposed edge cannot claim local backing");
            auto held=candidate;held.held=true;
            Assert::IsFalse(ProveLocalSubtitleGlyphBacking(source,held,ink.get(),plane,!top).accepted,
                L"held detector output is not fresh glyph evidence");
            auto unavailable=std::make_shared<SubtitleInkSnapshot>(*ink);
            unavailable->rawEvidenceComplete=false;
            Assert::IsFalse(ProveLocalSubtitleGlyphBacking(source,candidate,unavailable.get(),plane,!top).accepted,
                L"incomplete current-pixel ownership cannot pass the local proof");
        }

        auto pixels=Pixels();LocalBacking(pixels,315);Text(pixels,260,307,4); // short, centered [You]-sized line
        const auto source=Source(pixels);SubtitleBoxDetector detector;
        detector.SetNearBarDistance(20);
        const auto shortCue=detector.Analyze(source,45,315,1,1);
        Assert::IsTrue(shortCue.detected && shortCue.nearBarEligible);
        Assert::IsTrue(ProveLocalSubtitleGlyphBacking(source,shortCue,detector.InkSnapshot().get(),315,true).accepted,
            L"a short cue can pass when its owned glyphs and backing meet the sampling contract");
    }

    TEST_METHOD(ArabicLocalGlyphProofNeedsSparseBarSideBacking) {
        // This dense connected baseline crosses well into the missing-bar side;
        // its owned halo leaves too little distributed backing to certify.
        auto densePixels=Pixels();LocalBacking(densePixels,315);
        ArabicTextLine(densePixels,220,307,180,14);
        auto denseSource=Source(densePixels);SubtitleBoxDetector denseDetector;
        denseDetector.SetNearBarDistance(20);denseDetector.SetScriptHint(SubtitleScriptHint::Arabic);
        const auto dense=denseDetector.Analyze(denseSource,45,315,1,1);
        Assert::IsTrue(dense.detected && dense.nearBarEligible);
        const auto denseProof=ProveLocalSubtitleGlyphBacking(denseSource,dense,
            denseDetector.InkSnapshot().get(),315,true);
        Assert::IsFalse(denseProof.accepted);
        Assert::AreEqual(0,std::strcmp(denseProof.reason,"insufficient-unowned-backing"),
            L"dense owned Arabic strokes do not leave enough current dark backing");

        for(bool top:{false,true}) {
            auto pixels=Pixels();LocalBacking(pixels,315);
            ArabicTextLine(pixels,220,297,180,22);
            if(top)MirrorLumaRows(pixels);
            const auto source=Source(pixels);SubtitleBoxDetector detector;
            detector.SetNearBarDistance(20);detector.SetScriptHint(SubtitleScriptHint::Arabic);
            const auto candidate=detector.Analyze(source,45,315,1,1);
            Assert::IsTrue(candidate.detected && !candidate.held && !candidate.workLimit &&
                candidate.nearBarEligibilityMeasured && candidate.nearBarEligible,
                L"sparse Arabic descenders produce a fresh near-edge candidate");
            const int plane=top?45:315;
            const auto proof=ProveLocalSubtitleGlyphBacking(source,candidate,
                detector.InkSnapshot().get(),plane,!top);
            Assert::IsTrue(proof.accepted,
                L"sparse Arabic descenders crossing the edge retain distributed neutral backing");
            const auto capture=SubtitleGlyphCaptureBounds(candidate,source.width,source.height);
            Assert::IsTrue(capture.left>=proof.verifiedBounds.left && capture.top>=proof.verifiedBounds.top &&
                capture.right<=proof.verifiedBounds.right && capture.bottom<=proof.verifiedBounds.bottom);
        }
    }
    TEST_METHOD(LocalGlyphBackingCannotReuseDisappearedOrChangedCurrentInk) {
        auto pixels=Pixels();LocalBacking(pixels,315);Text(pixels,220,307,12);
        const auto source=Source(pixels);SubtitleBoxDetector detector;
        detector.SetNearBarDistance(20);
        const auto first=detector.Analyze(source,45,315,1,1);const auto firstInk=detector.InkSnapshot();
        Assert::IsTrue(first.detected);
        Assert::IsTrue(ProveLocalSubtitleGlyphBacking(source,first,firstInk.get(),315,true).accepted);
        const auto unchanged=detector.Analyze(source,45,315,2,1);const auto unchangedInk=detector.InkSnapshot();
        Assert::IsTrue(unchanged.detected && unchanged.cue==first.cue && unchangedInk!=firstInk,
            L"unchanged pixels are freshly sampled while retaining the detector cue");
        Assert::IsTrue(ProveLocalSubtitleGlyphBacking(source,unchanged,unchangedInk.get(),315,true).accepted);

        Text(pixels,220,307,12,true);
        const auto changedSource=Source(pixels);
        const auto changed=detector.Analyze(changedSource,45,315,3,1);const auto changedInk=detector.InkSnapshot();
        Assert::IsTrue(changed.detected && changedInk!=firstInk);
        SubtitleBoxObservation firstObservation,changedObservation;
        firstObservation.identity=Identity(1);firstObservation.width=640;firstObservation.height=360;
        firstObservation.pictureTop=45;firstObservation.pictureBottom=315;
        firstObservation.text=first;firstObservation.barAuthority=true;
        changedObservation.identity=Identity(2);changedObservation.width=640;changedObservation.height=360;
        changedObservation.pictureTop=45;changedObservation.pictureBottom=315;
        changedObservation.text=changed;changedObservation.barAuthority=true;
        Assert::IsFalse(SubtitleBoxLookahead::SameCue(firstObservation,changedObservation),
            L"changed fresh glyph evidence does not keep the earlier cue identity");
        Assert::IsTrue(ProveLocalSubtitleGlyphBacking(changedSource,changed,changedInk.get(),315,true).accepted,
            L"a changed cue can prove only its own current glyph/backing pixels");

        Fill(pixels,216,303,380,325,64);
        const auto blankSource=Source(pixels);
        const auto blank=detector.Analyze(blankSource,45,315,4,1);const auto blankInk=detector.InkSnapshot();
        Assert::IsFalse(blank.detected);
        Assert::IsFalse(ProveLocalSubtitleGlyphBacking(blankSource,first,firstInk.get(),315,true).accepted,
            L"stale owned masks cannot pass after source glyph pixels disappear");
        Assert::IsFalse(ProveLocalSubtitleGlyphBacking(blankSource,blank,blankInk.get(),315,true).accepted,
            L"disappearance cannot reuse a prior cue or its ink snapshot");
    }
    TEST_METHOD(LocalGlyphProofRejectsNarrowLightsAndARExpansionWithoutOppositeBar) {
        auto light=Pixels();Fill(light,300,307,308,322,700);
        const auto lightSource=Source(light);SubtitleBoxDetector lightDetector;
        lightDetector.SetNearBarDistance(20);
        const auto lightCue=lightDetector.Analyze(lightSource,45,315,1,1);
        Assert::IsFalse(ProveLocalSubtitleGlyphBacking(lightSource,lightCue,
            lightDetector.InkSnapshot().get(),315,true).accepted,
            L"a narrow interior light cannot masquerade as a crossing caption");

        auto expanded=Pixels(true);Fill(expanded,0,0,640,360,300);Text(expanded,220,307,12);
        const auto expandedSource=Source(expanded);
        auto key=Cue(1);key.nearBarDistance=20;key.policyGeneration=2;key.continuityGeneration=5;
        key.sharedPicture.required=true;key.sharedPicture.identity=key.identity;
        key.sharedPicture.bounds={0,45,640,315,640,360};
        SubtitleMeasurementSampler sampler;
        const auto result=sampler.Measure(expandedSource,key,{},24);
        Assert::IsFalse(result.glyphLocalTop || result.glyphLocalBottom,
            L"real picture expansion without the opposite physical bar cannot borrow a stale shared edge");
        Assert::IsFalse(result.independentBarAnalysis);
        Assert::IsFalse(result.subtitleArProof.accepted);
        Assert::AreEqual(315,result.sharedPicture.bounds.bottom);
    }
    TEST_METHOD(PolicyContinuityAndSourceDiscontinuityResetEstablishedCue) {
        for(int kind=0;kind<3;++kind) {
            SubtitleBoxPresentation tracker;const auto first=tracker.Consume(Resolve({Cue(1),Cue(2)}));
            auto preview=Resolve({Cue(2),Cue(3)});
            if(kind==0)preview.policyGeneration++;if(kind==1)preview.continuityGeneration++;
            if(kind==2)preview.current.discontinuity=true;
            const auto next=tracker.Consume(preview);
            Assert::IsTrue(next.detected);Assert::IsTrue(next.cue!=first.cue);Assert::AreEqual(1u,next.observations);
        }
    }
    TEST_METHOD(AlignedRejectedCaptionEdgeIsOnlyRelaxedForDiagnosticWithFreshPixelProof) {
        auto pixels=Pixels();Text(pixels,200,336,18);
        const auto source=Source(pixels);const auto raw=ExtractActivePictureEvidence(source);
        Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
        Assert::IsTrue(EvaluateSymmetricVerticalBarHypothesis(source,raw).classification==
            ActivePictureClassification::PROVISIONAL,L"default crop acquisition unchanged");
        const auto diagnostic=EvaluateSymmetricVerticalBarHypothesis(source,raw,true);
        Assert::IsTrue(diagnostic.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
        Assert::IsTrue(diagnostic.trustedBounds.top>=42&&diagnostic.trustedBounds.bottom<=318);
        auto expanded=Pixels();Fill(expanded,0,315,640,330,300);Text(expanded,200,336,18);
        const auto expandedSource=Source(expanded);const auto expandedRaw=ExtractActivePictureEvidence(expandedSource);
        const auto rejected=EvaluateSymmetricVerticalBarHypothesis(expandedSource,expandedRaw,true);
        Assert::IsTrue(rejected.classification!=ActivePictureClassification::BAR_CROP_TRUSTED ||
            rejected.trustedBounds.bottom>=330,L"connected picture must not become an inferred subtitle bar");
    }
    TEST_METHOD(OwnedSubtitleWindowAcquiresAllThreeRowsFromItsOwnReadyEvidence) {
        for(bool top:{false,true}) {
            std::array<SubtitleBoxObservation,4> frames;
            std::array<ActivePictureFrameIdentity,4> owned;
            for(size_t i=0;i<frames.size();++i) {
                frames[i]=NearCardThreeRows(i+1,top);
                frames[i].policyGeneration=11;frames[i].continuityGeneration=21;
                owned[i]=frames[i].identity;
            }
            const auto preview=SubtitleBoxLookahead::ResolveOwnedWindow(
                frames.data(),owned.data(),frames.size(),11,21);
            Assert::IsTrue(preview.available && preview.futureAvailable);
            Assert::AreEqual(3u,preview.followingCount);
            Assert::AreEqual(3,preview.text.lineCount);
            SubtitleBoxPresentation tracker;
            const auto accepted=tracker.Consume(preview);
            Assert::IsTrue(accepted.detected);
            Assert::AreEqual(3,accepted.lineCount);
        }
    }
    TEST_METHOD(OwnedSubtitleWindowNeverBridgesAMissingOrForeignFutureSample) {
        for(int scenario=0;scenario<13;++scenario) {
            std::array<SubtitleBoxObservation,4> frames;
            std::array<ActivePictureFrameIdentity,4> owned;
            for(size_t i=0;i<frames.size();++i) {
                frames[i]=NearCardThreeRows(i+1,false);
                frames[i].policyGeneration=11;frames[i].continuityGeneration=21;
                owned[i]=frames[i].identity;
            }
            auto& future=frames[1];
            if(scenario==0)future.analyzed=false;
            if(scenario==1)future.pendingRefresh=true;
            if(scenario==2)future.analysisRefresh=true;
            if(scenario==3)future.text.workLimit=true;
            if(scenario==4)future.discontinuity=true;
            if(scenario==5)future.identity.acceptedSequence++;
            if(scenario==6)future.identity.analysisDomainKey++;
            if(scenario==7)future.identity.sourceFormatGeneration++;
            if(scenario==8)future.identity.viewportGeneration++;
            if(scenario==9)future.identity.rendererGeneration++;
            if(scenario==10)future.policyGeneration++;
            if(scenario==11)future.continuityGeneration++;
            if(scenario==12)owned[1].captureTimestamp++;
            const auto preview=SubtitleBoxLookahead::ResolveOwnedWindow(
                frames.data(),owned.data(),frames.size(),11,21);
            Assert::IsTrue(preview.available);
            Assert::IsFalse(preview.futureAvailable);
            Assert::AreEqual(0u,preview.followingCount);
            Assert::AreEqual(0,preview.text.lineCount);
        }
    }
    TEST_METHOD(OwnedSubtitleWindowRequiresExactCurrentQueueOwnershipAndContext) {
        for(int scenario=0;scenario<5;++scenario) {
            auto frame=NearCardThreeRows(1,false);
            frame.policyGeneration=11;frame.continuityGeneration=21;
            auto owned=frame.identity;
            if(scenario==0)owned.acceptedSequence++;
            if(scenario==1)owned.analysisDomainKey++;
            if(scenario==2)frame.policyGeneration++;
            if(scenario==3)frame.continuityGeneration++;
            if(scenario==4)frame.analyzed=false;
            const auto preview=SubtitleBoxLookahead::ResolveOwnedWindow(&frame,&owned,1,11,21);
            Assert::IsFalse(preview.available);
        }
        Assert::IsFalse(SubtitleBoxLookahead::ResolveOwnedWindow(nullptr,nullptr,0,11,21).available);
    }
    TEST_METHOD(OwnedSubtitleWindowCannotUseFutureCaptionToFillACurrentCueEnd) {
        std::array<SubtitleBoxObservation,4> frames;
        std::array<ActivePictureFrameIdentity,4> owned;
        for(size_t i=0;i<frames.size();++i) {
            frames[i]=NearCardThreeRows(i+1,false);
            frames[i].policyGeneration=11;frames[i].continuityGeneration=21;
            owned[i]=frames[i].identity;
        }
        frames[0].text={};
        auto empty=std::make_shared<SubtitleInkSnapshot>(*frames[0].ink);
        std::fill(empty->rawInk.begin(),empty->rawInk.end(),0);
        std::fill(empty->ownedInk.begin(),empty->ownedInk.end(),0);
        frames[0].ink=empty;
        const auto preview=SubtitleBoxLookahead::ResolveOwnedWindow(
            frames.data(),owned.data(),frames.size(),11,21);
        Assert::IsTrue(preview.available);
        Assert::IsFalse(preview.text.detected);
        SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(preview).detected);
    }
};
}
