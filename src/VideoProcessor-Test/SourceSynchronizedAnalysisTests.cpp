#include "pch.h"
#include "CppUnitTest.h"
#include <SourcePictureDetector.h>
#include <SubtitleMeasurementWorker.h>
#include <ActivePictureTransitionModel.h>
#include <SourceSynchronizedAnalysis.h>
#include <algorithm>
#include <memory>
#include <vector>
#include <sstream>
#include <thread>
#include <chrono>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests {
namespace {
// Generated native-format PIXELS, not supplied detector classifications. The
// timelines below are explicit test sources, not relabelled captured footage.
struct MovingSourcePixels {
    static constexpr int Width=640, Height=360;
    std::shared_ptr<std::vector<uint16_t>> pixels;
    AnalysisLumaSource source;
    ActivePictureFrameIdentity identity;
    MovingSourcePixels(uint64_t sequence,int left,int top,int right,int bottom)
        :pixels(std::make_shared<std::vector<uint16_t>>(Width*Height*3/2,uint16_t(512<<6))) {
        identity={11,sequence,7000+sequence,900000+sequence*417083,11,17,23,29};
        source.data=reinterpret_cast<const uint8_t*>(pixels->data());source.dataBytes=pixels->size()*2;
        source.width=Width;source.height=Height;source.rowBytes=source.chromaRowBytes=Width*2;
        source.format=AnalysisLumaFormat::P010;source.encoding=VideoFrameEncoding::V210;
        source.colorspace=ColorSpace::REC_709;source.range=AnalysisLumaRange::Limited;
        source.generation=identity.transportGeneration;source.backingOwner=pixels;
        Fill(0,0,Width,Height,64);
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)
            (*pixels)[size_t(y)*Width+x]=uint16_t((220+((x/7+y/5+int(sequence)*3)%70))<<6);
    }
    void Fill(int left,int top,int right,int bottom,int code) {
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)
            (*pixels)[size_t(y)*Width+x]=uint16_t(code<<6);
    }
    void Caption(int count,int phase=0) {
        // Same outlined hollow raster used by existing detector fixtures.
        for(int n=0;n<count;++n) {
            const int x=160+n*13,y=320;
            Fill(x-1,y-1,x+9,y+15,64);
            Fill(x,y,x+8,y+14,800+4*phase);Fill(x+2,y+2,x+6,y+12,64);
        }
    }
};
struct PixelRectangle {int left,top,right,bottom;};
bool NearRectangle(const ActivePictureBounds& measured,const PixelRectangle& expected,int tolerance=3) {
    return std::abs(measured.left-expected.left)<=tolerance && std::abs(measured.top-expected.top)<=tolerance &&
        std::abs(measured.right-expected.right)<=tolerance && std::abs(measured.bottom-expected.bottom)<=tolerance;
}
SourcePictureDetectionResult Measure(const MovingSourcePixels& pixels) {
    SourcePictureSupportRequest request;
    return DetectPreparedSourcePictureBounded(pixels.source,pixels.identity,&request);
}
SubtitleBoxObservation Request(const MovingSourcePixels& pixels) {
    SubtitleBoxObservation key;key.identity=pixels.identity;key.width=pixels.source.width;key.height=pixels.source.height;
    key.policyGeneration=31;key.continuityGeneration=37;key.analysisStride=1;
    key.sharedPicture.required=true;key.sharedPicture.identity=pixels.identity;
    return key;
}
SubtitleBoxObservation RawCue(const MovingSourcePixels& pixels,const SourcePictureDetectionResult& native,
    SubtitleMeasurementSampler& sampler,const SubtitleBoxObservation* suppliedRequest=nullptr) {
    auto key=suppliedRequest?*suppliedRequest:Request(pixels);
    std::shared_ptr<const CurrentSourceSupportEvidence> support;
    if(native.support)support=std::shared_ptr<const CurrentSourceSupportEvidence>(native.support,&native.support->currentSupport);
    return sampler.Measure(pixels.source,key,{},24.0,native.evidence,support);
}
}
TEST_CLASS(SourceSynchronizedAnalysisTests) {
public:
    TEST_METHOD(PixelMeasuredVariableAspectTransitionsRespondInBothDirections) {
        // Scope/1.90 and 2.20/pillarboxed1.43 are source pixel rectangles. The
        // detector receives neither these rectangles nor a preferred aspect.
        const PixelRectangle scenes[]={{0,45,640,315},{0,12,640,348},{0,45,640,315},
            {0,35,640,325},{63,0,577,360},{0,35,640,325}};
        ActivePictureTransitionModel model;
        uint64_t sequence=1;
        for(const auto& scene:scenes) {
            // Cold acquisition retains its existing four-observation contract;
            // subsequent genuine transitions still have the independent limit.
            const unsigned limit=sequence==1?ActivePictureTransitionModel::INITIAL_CONFIRMATIONS:3;
            unsigned firstCorrect=0,publications=0;
            for(unsigned frame=0;frame<8;++frame,++sequence) {
                MovingSourcePixels pixels(sequence,scene.left,scene.top,scene.right,scene.bottom);
                const auto measured=Measure(pixels);
                Assert::IsTrue(measured.evidence.currentSourceIdentityAvailable &&
                    SameAnalysisFrameIdentity(measured.evidence.currentSourceIdentity,pixels.identity));
                Assert::IsTrue(measured.evidence.physicalEdges.estimateAvailable,
                    L"A clear native-pixel rectangle must yield a physical measurement.");
                Assert::IsTrue(NearRectangle(measured.evidence.physicalEdges.estimate,scene),
                    L"Physical geometry must follow source pixels, independently of screen/fill.");
                const auto observation=MakeActivePictureObservation(measured.evidence,sequence,24.0,&pixels.identity);
                const auto decision=model.Observe(observation);
                std::ostringstream detail;detail<<"PIXEL_TRANSITION source="<<sequence<<" expected="<<scene.left<<","<<scene.top<<","<<scene.right<<","<<scene.bottom<<" physical="<<measured.evidence.physicalEdges.estimate.left<<","<<measured.evidence.physicalEdges.estimate.top<<","<<measured.evidence.physicalEdges.estimate.right<<","<<measured.evidence.physicalEdges.estimate.bottom<<" class="<<int(measured.evidence.classification)<<" stable="<<decision.stable<<" publish="<<decision.publish<<" decision="<<decision.bounds.left<<","<<decision.bounds.top<<","<<decision.bounds.right<<","<<decision.bounds.bottom<<" reason="<<decision.reason;Logger::WriteMessage(detail.str().c_str());
                if(decision.publish)++publications;
                const auto& effective=decision.publish?decision.bounds:decision.stableBounds;
                if(decision.stable && NearRectangle(effective,scene)) {
                    if(!firstCorrect)firstCorrect=frame+1;
                } else Assert::IsTrue(frame<limit,L"A real aspect transition must not remain stuck on its previous scene.");
            }
            Assert::AreEqual(1u,publications,L"Each real scene transition must publish exactly once, not oscillate or remain a candidate.");
            Assert::IsTrue(firstCorrect>0 && firstCorrect<=limit,
                L"Cold acquisition follows its existing bound; real transitions settle within three observations.");
        }
    }
    TEST_METHOD(PixelMeasuredMenuAndInteriorDarkShapesDoNotBecomeEncodedBars) {
        ActivePictureTransitionModel model;
        for(unsigned variant=0;variant<4;++variant) {
            MovingSourcePixels pixels(100+variant,0,0,640,360);
            if(variant==0)pixels.Fill(100,70,540,290,64); // Interior black artwork, colored perimeter.
            if(variant==1) {pixels.Fill(70,40,570,320,70);pixels.Fill(110,65,530,85,800);} // Menu panel.
            if(variant==2) {pixels.Fill(0,285,640,340,64);pixels.Fill(30,308,610,313,700);} // UI over full picture.
            if(variant==3) {pixels.Fill(0,0,640,360,80);pixels.Fill(20,160,620,200,130);} // Dark full raster.
            const auto measured=Measure(pixels);
            std::ostringstream detail;detail<<"PIXEL_MENU variant="<<variant<<" physical="<<measured.evidence.physicalEdges.estimateAvailable<<" class="<<int(measured.evidence.classification)<<" reason="<<measured.evidence.reason;Logger::WriteMessage(detail.str().c_str());
            if(variant<3) {
                Assert::IsTrue(measured.evidence.physicalEdges.estimateAvailable,
                    L"Visible full-raster perimeter must not be classified from interior black artwork.");
                Assert::IsTrue(NearRectangle(measured.evidence.physicalEdges.estimate,{0,0,640,360}),
                    L"Menu/interior black content is not an encoded bar rectangle.");
            } else if(measured.evidence.physicalEdges.estimateAvailable) {
                Assert::IsTrue(NearRectangle(measured.evidence.physicalEdges.estimate,{0,0,640,360}));
            }
            Assert::IsFalse(measured.evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
            const auto decision=model.Observe(MakeActivePictureObservation(measured.evidence,
                pixels.identity.acceptedSequence,24.0,&pixels.identity));
            Assert::IsTrue(decision.stable && NearRectangle(decision.publish?decision.bounds:decision.stableBounds,{0,0,640,360}),
                L"An ambiguous dark frame may report unavailable current measurement, but must retain established full-raster geometry.");
        }
    }
    TEST_METHOD(PixelMeasuredCaptionChangesRemainBoundToTheirOriginalSource) {
        SubtitleMeasurementSampler sampler;
        for(uint64_t sequence=200;sequence<208;++sequence) {
            MovingSourcePixels pixels(sequence,0,45,640,315);
            const int count=sequence<204?20:24;pixels.Caption(count,int(sequence%4));
            const auto native=Measure(pixels);const auto cue=RawCue(pixels,native,sampler);
            Assert::IsTrue(SameAnalysisFrameIdentity(cue.identity,pixels.identity));
            Assert::IsTrue(cue.analyzed && cue.text.detected && !cue.text.workLimit && cue.ink &&
                cue.ink->rawEvidenceComplete,L"Use actual current glyph sampling, never synthetic authority or copied raw masks.");
            const int finalGlyphRight=160+(count-1)*13+8;
            Assert::IsTrue(cue.text.bounds.left<=160 && cue.text.bounds.right>=finalGlyphRight &&
                cue.text.bounds.top<=320 && cue.text.bounds.bottom>=334,
                L"The new word's current glyph pixels must not disappear behind retained cue geometry.");
        }
    }
    TEST_METHOD(SourceTicketPairsMeasuredArAndCueWithoutAcceptingLateForeignPixels) {
        MovingSourcePixels first(400,0,45,640,315),next(401,0,45,640,315);
        first.Caption(20);next.Caption(24);
        const auto firstNative=Measure(first),nextNative=Measure(next);
        auto firstRequest=Request(first),nextRequest=Request(next);
        std::shared_ptr<const SourceAnalysisTicket> firstSlot,nextSlot;
        const auto firstTicket=SourceAnalysisTicket::Acquire(&firstSlot,firstRequest);
        const auto nextTicket=SourceAnalysisTicket::Acquire(&nextSlot,nextRequest);
        Assert::IsTrue(firstTicket && nextTicket && firstTicket!=nextTicket);
        SubtitleMeasurementSampler sampler;
        // Intentionally complete the newer actual source first. No source IDs
        // or measured masks are rewritten to hide this completion order.
        const auto nextCue=RawCue(next,nextNative,sampler,&nextRequest);
        const auto lateCue=RawCue(first,firstNative,sampler,&firstRequest);
        Assert::IsTrue(nextCue.text.detected && lateCue.text.detected);
        Assert::IsTrue(firstTicket->Accepts(lateCue) && nextTicket->Accepts(nextCue));
        Assert::IsFalse(firstTicket->Accepts(nextCue));Assert::IsFalse(nextTicket->Accepts(lateCue));
        Assert::IsTrue(SameAnalysisFrameIdentity(firstNative.evidence.currentSourceIdentity,lateCue.identity));
        Assert::IsTrue(SameAnalysisFrameIdentity(nextNative.evidence.currentSourceIdentity,nextCue.identity));
        Assert::IsTrue(nextCue.text.bounds.right>lateCue.text.bounds.right,
            L"The later source contains real added glyphs, so exchanging these completions would lose text.");
        auto completed=lateCue;Assert::IsFalse(firstTicket->BindRequest(completed),
            L"A frozen request must never rewrite already measured pixels.");
        auto revisedRequest=firstRequest;++revisedRequest.policyGeneration;
        const auto revisedTicket=SourceAnalysisTicket::Acquire(&firstSlot,revisedRequest);
        Assert::IsTrue(revisedTicket && revisedTicket!=firstTicket);
        Assert::IsFalse(revisedTicket->Accepts(lateCue));
        Assert::IsFalse(bool(SourceAnalysisTicket::Acquire(&firstSlot,firstRequest)),
            L"A late request cannot roll an exact-source policy revision backward.");
        const auto revisedCue=RawCue(first,firstNative,sampler,&revisedRequest);
        Assert::IsTrue(revisedTicket->Accepts(revisedCue));
    }
    TEST_METHOD(SourceTicketMissingWorkDoesNotRenameRequestsOrGrantCurrentEvidence) {
        MovingSourcePixels pixels(450,0,45,640,315);pixels.Caption(20);
        auto request=Request(pixels);std::shared_ptr<const SourceAnalysisTicket> slot;
        const auto ticket=SourceAnalysisTicket::Acquire(&slot,request);Assert::IsTrue(ticket && ticket->Valid());
        // Omitted completions do not require waits, fake negatives or a new task.
        for(unsigned retry=0;retry<32;++retry) {
            auto later=request;later.sharedPicture.required=false;
            const auto same=SourceAnalysisTicket::Acquire(&slot,later);
            Assert::IsTrue(same==ticket && same->BindRequest(later));
            Assert::IsTrue(later.sharedPicture.Matches(request.sharedPicture));
            Assert::IsFalse(later.analyzed || later.ink || later.barAuthority || later.text.detected);
        }
        for(unsigned fault=0;fault<14;++fault) {
            auto foreign=request;
            switch(fault) {
            case 0:++foreign.identity.acceptedSequence;break;
            case 1:++foreign.identity.sourceFrameNumber;break;
            case 2:++foreign.identity.captureTimestamp;break;
            case 3:++foreign.identity.transportGeneration;break;
            case 4:++foreign.identity.sourceFormatGeneration;break;
            case 5:++foreign.identity.analysisDomainKey;break;
            case 6:++foreign.identity.viewportGeneration;break;
            case 7:++foreign.identity.rendererGeneration;break;
            case 8:++foreign.width;break;
            case 9:++foreign.nearBarDistance;break;
            case 10:++foreign.optimizationMode;break;
            case 11:foreign.analysisStride=2;break;
            case 12:foreign.discontinuity=true;break;
            case 13:--foreign.continuityGeneration;break;
            }
            Assert::IsFalse(ticket->MatchesRequestContext(foreign));
            Assert::IsFalse(ticket->BindRequest(foreign));
            Assert::IsFalse(bool(SourceAnalysisTicket::Acquire(&slot,foreign)),
                L"A source/context mismatch must not replace a queued source's immutable request.");
        }
        const auto native=Measure(pixels);SubtitleMeasurementSampler sampler;
        const auto completed=RawCue(pixels,native,sampler,&request);
        Assert::IsTrue(completed.text.detected && ticket->Accepts(completed));
        Assert::IsTrue(SameAnalysisFrameIdentity(native.evidence.currentSourceIdentity,completed.identity));
    }

    TEST_METHOD(SourceTicketAdoptsOnlyWitnessedExactOwnedScanWithoutRewritingNativeProof) {
        ActivePictureDecisionTimeline timeline;timeline.Reset(11);
        for(uint64_t sequence=1;sequence<=4;++sequence) {
            MovingSourcePixels frame(sequence,0,45,640,315);
            Assert::IsTrue(timeline.TrackAcceptedFrame(frame.identity));
        }
        MovingSourcePixels pixels(3,0,45,640,315);pixels.Caption(20);
        const auto native=Measure(pixels);auto key=Request(pixels);
        key.policyGeneration=timeline.LookaheadPolicyGeneration();
        key.continuityGeneration=timeline.ContinuityGeneration();
        std::shared_ptr<const SourceAnalysisTicket> slot;
        const auto originalTicket=SourceAnalysisTicket::Acquire(&slot,key);
        SubtitleMeasurementWorker worker;
        worker.Submit(key,[pixels,native,key](SubtitleMeasurementSampler& sampler,const SubtitleBarTrackingReference&) {
            return RawCue(pixels,native,sampler,&key);
        });
        SubtitleBoxObservation original;bool ready=false;
        for(unsigned wait=0;wait<500 && !ready;++wait) {
            ready=worker.TryRead(key,original);
            if(!ready)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Assert::IsTrue(ready && original.text.detected && originalTicket->Accepts(original));
        Assert::IsTrue(SubtitleMeasurementWorker::CompleteIndependentScanForKey(original,key),
            L"The fixture must produce the real complete independent scan required by the existing witness path.");
        const auto originalProof=original.subtitleArProof.accepted;
        MovingSourcePixels retired1(1,0,45,640,315),retired2(2,0,45,640,315);
        timeline.MarkDiscarded(retired1.identity,1,ActivePictureDiscardReason::BacklogRecovery);
        timeline.MarkDiscarded(retired2.identity,2,ActivePictureDiscardReason::QueueCapacityOverflow);
        auto current=key;current.continuityGeneration=timeline.ContinuityGeneration();
        const auto currentTicket=SourceAnalysisTicket::Acquire(&slot,current);
        Assert::IsTrue(currentTicket && currentTicket!=originalTicket);
        Assert::IsFalse(currentTicket->Accepts(original));
        const auto witness=timeline.QualifyOwnedScanBacklogWitness(current.identity,
            key.continuityGeneration,current.continuityGeneration);
        SubtitleBoxObservation rebound;
        Assert::IsFalse(worker.TryRead(current,rebound));
        Assert::IsFalse(worker.TryReadOwnedScan(current,rebound,OwnedScanBacklogWitness{},key.policyGeneration));
        Assert::IsTrue(worker.TryReadOwnedScan(current,rebound,witness,key.policyGeneration));
        Assert::IsTrue(currentTicket->Accepts(rebound));
        Assert::IsTrue(SameAnalysisFrameIdentity(rebound.identity,original.identity));
        Assert::IsTrue(rebound.ink==original.ink);
        Assert::IsFalse(rebound.subtitleArProof.accepted,
            L"Transport adoption of already measured current glyphs must not restore prior native AR permission.");
        SubtitleBoxObservation unchanged;Assert::IsTrue(worker.TryRead(key,unchanged));
        Assert::AreEqual(key.continuityGeneration,unchanged.continuityGeneration);
        Assert::AreEqual(originalProof,unchanged.subtitleArProof.accepted);
        Assert::IsTrue(originalTicket->Accepts(unchanged));
        auto foreign=current;++foreign.identity.analysisDomainKey;
        Assert::IsFalse(worker.TryReadOwnedScan(foreign,rebound,witness,key.policyGeneration));
        timeline.BreakContinuity(3);
        auto reset=current;reset.continuityGeneration=timeline.ContinuityGeneration();
        const auto resetWitness=timeline.QualifyOwnedScanBacklogWitness(reset.identity,key.continuityGeneration,reset.continuityGeneration);
        Assert::IsFalse(worker.TryReadOwnedScan(reset,rebound,resetWitness,key.policyGeneration),
            L"A genuine continuity break cannot be reclassified as queue retirement.");
        worker.Stop();
    }

};
}
