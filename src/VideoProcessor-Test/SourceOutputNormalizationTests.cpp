#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/SourcePreparationReceipt.h>
#include <vprenderer/ActivePictureMeasurementCache.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <vprenderer/SubtitleAspectRetention.h>
#include <vprenderer/TemporalRenderer.h>
#include <SubtitleCurrentCue.h>
#include <SubtitleMeasurementWorker.h>
#include <SubtitleCutPaste.h>
#include <SubtitleBackingLip.h>
#include <array>
#include <memory>
#include <vector>
#include <limits>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace {
struct NormalizationFrame {
    std::shared_ptr<const std::vector<uint16_t>> pixels;
    static constexpr int Width=640,Height=360,Top=64,Bottom=296;
    explicit NormalizationFrame(bool caption=true,bool navigation=false) {
        auto data=std::make_shared<std::vector<uint16_t>>(Width*Height*2,uint16_t(512<<6));
        auto fill=[&](int l,int t,int r,int b,int value) {
            for(int y=t;y<b;++y)for(int x=l;x<r;++x)(*data)[size_t(y)*Width+x]=uint16_t(value<<6);
        };
        fill(0,0,Width,Height,64);fill(0,Top,Width,Bottom,300);
        if(caption) {
            fill(224,Bottom-4,416,Bottom,64);
            for(int line=0;line<2;++line)for(int n=0;n<12;++n) {
                const int x=240+13*n,y=Bottom+2+line*28;
                fill(x,y,x+8,y+14,700);fill(x+2,y+2,x+6,y+12,64);
            }
        }
        if(navigation){fill(24,318,616,322,600);fill(474,338,528,348,700);fill(572,338,610,348,700);}
        pixels=std::move(data);
    }
    AnalysisLumaSource Source() const {
        AnalysisLumaSource s;s.data=reinterpret_cast<const uint8_t*>(pixels->data());
        s.dataBytes=pixels->size()*2;s.width=Width;s.height=Height;s.rowBytes=s.chromaRowBytes=Width*2;
        s.format=AnalysisLumaFormat::P210;s.encoding=VideoFrameEncoding::V210;
        s.colorspace=ColorSpace::REC_709;s.generation=2;s.backingOwner=pixels;
        s.range=AnalysisLumaRange::Limited;return s;
    }
};
ActivePictureFrameIdentity NormalizationIdentity(uint64_t sequence) {
    auto id=ActivePictureFrameIdentity{2,sequence,sequence,sequence*41708,2,3,4};id.analysisDomainKey=9;return id;
}
ActivePictureBounds NormalizationBase() {
    return {0,64,640,296,640,360,640.0/232.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
}
SourcePreparationContext NormalizationContext(const NormalizationFrame& frame,uint64_t sequence) {
    SourcePreparationContext c;c.identity=NormalizationIdentity(sequence);c.source=c.queuedSource=frame.Source();
    c.gpu=reinterpret_cast<const void*>(uintptr_t(1));c.processingEpoch=11;c.policyGeneration=7;
    c.continuityGeneration=8;c.resetEpoch=9;c.viewportEpoch=3;c.sceneGeneration=5;
    c.outputContract=17;c.subtitleMode=5;c.configuredScreen=true;return c;
}
bool SameNormalizationRect(const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
    return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;
}
struct NormalizationSnapshot {
    ActivePictureTransitionDecision ar;
    SubtitleBoxResult accepted;
    SubtitleCutPasteGeometry geometry;
    ActivePictureFrameIdentity verified;
    double leaseMs=0;
    uint64_t capSequence=0;
    bool noCaption=false;
};
// Heap-owned by tests: exercise the real detector, tracker, current proof,
// placement and finite cleanup. The receipt is the same production gate.
struct NativeNormalizationPipeline {
    SourcePreparationReceipt receipt;
    ActivePictureMeasurementCache measurements;
    ActivePictureTransitionModel model;
    SubtitleCueContinuationGuard guard;
    SubtitleBoxPresentation tracker;
    SubtitlePendingMeasurementGuard pending;
    SubtitlePaddingGuard padding;
    SubtitleCutPastePresentation compositor;
    SubtitleLayoutAnchor anchor;
    SubtitleBackingLipContinuity backing;
    PreparedCurrentSubtitleCue prepared;
    CurrentSubtitleCueDecision decision;
    SubtitleBoxPreview acquired,working;
    NormalizationSnapshot saved;
    size_t analysisReads=0,subtitleReads=0,analysisPasses=0,subtitlePasses=0;
    std::vector<uint64_t> analyzedSequences;
    SubtitleBoxObservation Measure(const AnalysisLumaSource& source,const ActivePictureFrameIdentity& identity) {
        SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
        return SubtitleBoxLookahead::Measure(detector,source,ExtractActivePictureEvidence(source),identity,false,{},7,8);
    }
    void Acquire(const NormalizationFrame& frame) {
        auto source=frame.Source();auto first=Measure(source,NormalizationIdentity(1));
        auto second=Measure(source,NormalizationIdentity(2));
        Assert::IsTrue(first.text.detected&&second.text.detected);
        const std::array<SubtitleBoxObservation,2> observations{first,second};
        auto initial=SubtitleBoxLookahead::Resolve(observations.data(),observations.size(),7,8);
        Assert::IsTrue(tracker.Consume(initial).detected);
        acquired=SubtitleBoxLookahead::Resolve(&second,1,7,8);decision.accepted=tracker.Consume(acquired);
        Assert::IsTrue(decision.accepted.detected&&guard.Remember(acquired,decision.accepted,NormalizationBase(),source));
        Assert::IsTrue(pending.RememberAccepted(acquired,decision.accepted,source,1000.0/24,250,
            &tracker.AcceptedCueSeedCertificate()));
        Assert::IsTrue(PrepareSubtitle(NormalizationContext(frame,2),true,SubtitleCurrentCueFrameSampleLimit));
        Assert::IsTrue(saved.geometry.valid&&saved.capSequence==2,L"Acquisition must seed actual placement and cleanup before UNKNOWN");
        subtitleReads=subtitlePasses=0;
    }
    void Analyze(const SourcePreparationContext& c) {
        AnalysisLumaReadBudget budget{(std::numeric_limits<size_t>::max)()};auto source=c.source;source.readBudget=&budget;
        measurements.Begin(source,c.identity,c.policyGeneration,c.continuityGeneration);
        const auto evidence=measurements.Picture();
        saved.ar=model.Observe(MakeActivePictureObservation(evidence,c.identity.acceptedSequence,24));
        analysisReads+=budget.used;++analysisPasses;analyzedSequences.push_back(c.identity.acceptedSequence);
    }
    bool PrepareSubtitle(const SourcePreparationContext& c,bool workerAvailable,size_t limit) {
        AnalysisLumaReadBudget budget{limit};auto source=c.source;source.readBudget=&budget;
        if(workerAvailable) {
            auto observed=Measure(source,c.identity);
            working=SubtitleBoxLookahead::Resolve(&observed,1,c.policyGeneration,c.continuityGeneration);
        } else {
            working=acquired;working.available=false;working.text={};working.current.identity=c.identity;
            working.current.sharedPicture={};working.current.sharedPicture.required=true;
            working.current.analyzed=false;working.current.barAuthority=false;working.current.independentBarAnalysis=false;
            working.current.barTrackingAuthority=false;working.current.currentAnchorUsesTrackedEdge=false;
            working.current.barEvidence={};working.current.pictureTop=0;working.current.pictureBottom=360;
            working.current.text={};working.current.ink.reset();
        }
        PrepareCurrentSubtitleCueInto(prepared,guard,working,source,NormalizationBase(),true,250,1000.0/24);
        bool currentPending=false;
        if(!working.available&&prepared.AllowsPendingRecovery(working))currentPending=pending.Resolve(working,source,1000.0/24,250);
        ConsumePreparedCurrentSubtitleCueInto(decision,guard,tracker,working,source,NormalizationBase(),true,
            250,1000.0/24,prepared,currentPending);
        if(!decision.accepted.detected||decision.accepted.confirmationPending)pending.Reset();
        else if(working.available&&!working.current.pendingRefresh)
            pending.RememberAccepted(working,decision.accepted,source,1000.0/24,250,&tracker.AcceptedCueSeedCertificate());
        const auto* proof=decision.proof.valid||decision.proof.candidateRetained?&decision.proof:nullptr;
        const auto safe=padding.Consume(decision.accepted,working,{12,8,6},{},proof);
        auto geometry=compositor.Consume(decision.accepted,working,safe,4,nullptr,proof);
        geometry=ApplySubtitleTextReduction(FitSubtitleToVisiblePicture(geometry,{0,64,640,296},4),100,safe,true,false);
        geometry=anchor.Consume(geometry,decision.accepted,working,safe,4,false,proof);
        if(geometry.valid&&!geometry.generatedCleanup.Valid()) {
            const auto cap=backing.Consume(source,working,decision.accepted,c.identity,c.policyGeneration,
                c.continuityGeneration,SubtitleBackingLipSampleLimit,&geometry,decision.proof.valid?&decision.proof:nullptr);
            geometry=ApplyCurrentSubtitleBackingLip(geometry,cap,c.identity,c.policyGeneration,c.continuityGeneration);
            saved.capSequence=cap.valid?cap.identity.acceptedSequence:0;
        } else saved.capSequence=0;
        saved.accepted=decision.accepted;saved.geometry=geometry;saved.verified=guard.LastVerifiedIdentity();
        saved.leaseMs=decision.proof.elapsedMs;
        saved.noCaption=ConfirmedSubtitleAbsentForPresentation(working.current,decision.accepted,c.identity);
        subtitleReads+=budget.used;++subtitlePasses;
        return !budget.exhausted&&((geometry.valid&&decision.accepted.detected&&!decision.accepted.held&&
            !decision.accepted.confirmationPending)||saved.noCaption);
    }
    void Draw(const SourcePreparationContext& c,SourceRepeatKind repeat,bool workerAvailable=true,
        size_t limit=SubtitleCurrentCueFrameSampleLimit,bool successfulCapture=true) {
        const bool priorSubtitleComplete=receipt.ReuseSubtitle(c,SourceRepeatKind::CadenceRepeat);
        const auto reuse=receipt.Begin(c,repeat);
        if(!reuse.analysis)Analyze(c);
        bool subtitleComplete=priorSubtitleComplete;
        if(!reuse.subtitle)subtitleComplete=PrepareSubtitle(c,workerAvailable,limit);
        if(successfulCapture)receipt.Commit(c,true,subtitleComplete);
    }
};
void AssertSameNormalization(const NormalizationSnapshot& a,const NormalizationSnapshot& b) {
    Assert::IsTrue(a.ar.state==b.ar.state&&a.ar.publish==b.ar.publish&&a.ar.stable==b.ar.stable);
    Assert::AreEqual(a.ar.matchingCandidates,b.ar.matchingCandidates);
    Assert::AreEqual(a.ar.contradictoryCandidates,b.ar.contradictoryCandidates);
    Assert::AreEqual(a.ar.bounds.top,b.ar.bounds.top);Assert::AreEqual(a.ar.bounds.bottom,b.ar.bounds.bottom);
    Assert::AreEqual(a.accepted.cue,b.accepted.cue);Assert::AreEqual(a.accepted.observations,b.accepted.observations);
    Assert::IsTrue(a.accepted.detected==b.accepted.detected&&a.accepted.held==b.accepted.held&&
        a.accepted.confirmationPending==b.accepted.confirmationPending);
    Assert::IsTrue(SameNormalizationRect(a.geometry.source,b.geometry.source)&&
        SameNormalizationRect(a.geometry.destination,b.geometry.destination)&&
        SameNormalizationRect(a.geometry.generatedCleanup,b.geometry.generatedCleanup));
    Assert::IsTrue(SameAnalysisFrameIdentity(a.verified,b.verified));
    Assert::AreEqual(a.leaseMs,b.leaseMs);Assert::AreEqual(a.capSequence,b.capSequence);
}
}
namespace Tests {
TEST_CLASS(SourceOutputNormalizationTests) { public:
    TEST_METHOD(RealBarInspectionScansTheSameSourceIdentitiesAtEveryOutputRate) {
        using namespace AlphaSourceCrop;
        const NormalizationFrame frame(false,true);
        std::vector<uint64_t> reference;VerticalBarPresentationState referenceState{};
        for(double rate:{24.,48.,60.,120.}) {
            SourcePreparationReceipt receipt;SourceBarInspectionCursor cursor;uint64_t ordinal=0;
            ActivePictureMeasurementCache pixels;VerticalFitConfirmationState fit;
            VerticalBarPresentationState state;std::vector<uint64_t> scanned;
            auto cadence=std::make_unique<TemporalRenderer>();MotionConfig::Settings settings;
            settings.smoothMotion="always";settings.deinterlace="off";
            for(uint64_t sequence=1;sequence<=12;++sequence) {
                const auto c=NormalizationContext(frame,sequence);
                cadence->Begin(settings,24,rate,false,0,2,sequence,double(sequence)/24,false);
                const size_t count=(std::max)(size_t(1),cadence->outputs.size());
                for(size_t draw=0;draw<count+2;++draw) {
                    const auto kind=draw==0?SourceRepeatKind::NewSource:
                        draw<count?SourceRepeatKind::InsertedOutput:SourceRepeatKind::CadenceRepeat;
                    const auto reuse=receipt.Begin(c,kind);
                    if(!reuse.analysis) {
                        const auto schedule=ScheduleSourceBarInspection(ordinal,cursor,2,sequence,true,false);
                        if(schedule.evaluated) {
                            scanned.push_back(sequence);pixels.Begin(c.source,c.identity,7,8);
                            const auto content=pixels.SubtitleContent(NormalizationBase(),64);
                            Assert::IsTrue(content.lower&&content.lowerBottom>=348);
                            VerticalBarContentInput input;input.lowerContent=true;input.lowerBarPixels=64;
                            input.lowerOccupiedDepth=content.lowerBottom-content.lowerTop;
                            input.lowerOccupiedRows=content.lowerOccupiedRows;input.lowerPeakSamples=content.lowerPeak;
                            input.sampledColumns=600;input.lowerRequiredShift=float(content.lowerBottom-296+8);
                            const auto decision=SelectVerticalBarContentPresentation(EvaluateVerticalBarContent(input),false,&input);
                            const auto confirmed=ConfirmVerticalFit(fit,decision,sequence,false);fit=confirmed.state;
                            VerticalBarPresentationUpdateInput update;update.previous=state;update.current=confirmed.effective;
                            update.lowerContent=content.lower;update.lowerContentBottom=content.lowerBottom;
                            update.currentTick=1000+sequence*42+draw;update.currentSourceSequence=sequence;update.holdMs=2000;
                            state=UpdateVerticalBarPresentation(update);
                            Assert::IsTrue(state.action==VerticalBarPresentationAction::FIT);
                        }
                    }
                    receipt.Commit(c,true,true);
                }
            }
            Assert::AreEqual(uint64_t(12),ordinal);Assert::AreEqual(size_t(4),scanned.size());
            for(size_t i=0;i<scanned.size();++i)Assert::AreEqual(uint64_t((i+1)*3),scanned[i]);
            if(reference.empty()){reference=scanned;referenceState=state;}
            else {
                Assert::IsTrue(reference==scanned);Assert::AreEqual(referenceState.lastDetectionTick,state.lastDetectionTick);
                Assert::AreEqual(referenceState.sourceSequence,state.sourceSequence);
                Assert::AreEqual(referenceState.detectedBottom,state.detectedBottom);
            }
        }
    }
    TEST_METHOD(NewNavigationPixelsForceCurrentFitWithoutAnExtraSourceConfirmation) {
        using namespace AlphaSourceCrop;
        const NormalizationFrame blank(false),navigation(false,true);
        SourcePreparationReceipt receipt;SourceBarInspectionCursor cursor;uint64_t ordinal=0;
        const auto first=NormalizationContext(blank,1);receipt.Commit(first,true,true);
        const auto next=NormalizationContext(navigation,2);const auto reuse=receipt.Begin(next,SourceRepeatKind::NewSource);
        Assert::IsFalse(reuse.analysis);
        ActivePictureMeasurementCache pixels;pixels.Begin(next.source,next.identity,7,8);
        const auto retention=pixels.Retention(NormalizationBase());
        Assert::IsFalse(retention.excludedBandsPixelSafe);
        const auto schedule=ScheduleSourceBarInspection(ordinal,cursor,2,2,true,!retention.excludedBandsPixelSafe);
        Assert::IsFalse(schedule.scheduled);Assert::IsTrue(schedule.evaluated);
        const auto content=pixels.SubtitleContent(NormalizationBase(),64);Assert::IsTrue(content.lower);
        PresentationEnvelopeGeometryInput input;input.trustedPicture=input.observedContent=NormalizationBase();
        input.observedContentAvailable=true;input.observedContent.bottom=content.lowerBottom;input.expandBottom=true;input.verticalPadding=8;
        const auto envelope=BuildPresentationEnvelope(input);
        Assert::IsTrue(envelope.valid&&envelope.expanded&&envelope.bounds.bottom>=348);
        Assert::IsTrue(envelope.bounds.bottom<=360&&envelope.bounds.top==64);
        const auto retry=ScheduleSourceBarInspection(ordinal,cursor,2,2,true,true);
        Assert::IsTrue(retry.evaluated);Assert::AreEqual(uint64_t(1),ordinal,
            L"A stronger same-source retry cannot advance the scheduled inspection ordinal");
    }
    TEST_METHOD(PriorSourceTimedFitAndTranslationExpireOnAnUnscheduledCadenceRepeat) {
        using namespace AlphaSourceCrop;
        const NormalizationFrame blank(false);const auto c=NormalizationContext(blank,3);
        for(auto action:{VerticalBarPresentationAction::FIT,VerticalBarPresentationAction::TRANSLATE}) {
            VerticalBarPresentationState state;state.action=action;state.sourceSequence=2;
            state.lastDetectionTick=1000;state.detectedBottom=348;state.translationPixels=52;
            SourcePreparationReceipt receipt;SourceBarInspectionCursor cursor;uint64_t ordinal=0;
            Assert::IsTrue(SourceAnalysisHasTimedDependency(state,3,false,false));
            receipt.Commit(c,!SourceAnalysisHasTimedDependency(state,3,false,false),true);
            Assert::IsFalse(receipt.ReuseAnalysis(c,SourceRepeatKind::CadenceRepeat));
            auto schedule=ScheduleSourceBarInspection(ordinal,cursor,2,3,true,
                SourceBarInspectionNeedsExpiryRefresh(state,1100,2000,3));
            Assert::IsFalse(schedule.evaluated);Assert::AreEqual(uint64_t(1),ordinal);
            Assert::IsTrue(IsVerticalBarPresentationActiveForFrame(state,1100,2000,3,false,true,2,2));
            // Inserted draws can reuse the completed attempt without renewing
            // the earlier source's timer or promoting either completion flag.
            for(int draw=0;draw<8;++draw) {
                const auto reuse=receipt.Begin(c,SourceRepeatKind::InsertedOutput);
                Assert::IsTrue(reuse.analysis&&reuse.subtitle);
                receipt.Commit(c,!SourceAnalysisHasTimedDependency(state,3,false,false),true);
            }
            Assert::AreEqual(uint64_t(1000),state.lastDetectionTick);
            const auto reuse=receipt.Begin(c,SourceRepeatKind::CadenceRepeat);
            Assert::IsFalse(reuse.analysis||reuse.subtitle);
            // Existing skipped-analysis policy deliberately keeps this state;
            // the new source-phase gate must force an actual negative sample.
            Assert::IsTrue(IsVerticalBarPresentationActiveForFrame(state,3101,2000,3,false,true,2,2));
            schedule=ScheduleSourceBarInspection(ordinal,cursor,2,3,true,
                SourceBarInspectionNeedsExpiryRefresh(state,3101,2000,3));
            Assert::IsTrue(schedule.evaluated);Assert::IsFalse(schedule.scheduled);
            Assert::AreEqual(uint64_t(1),ordinal);
            ActivePictureMeasurementCache pixels;pixels.Begin(c.source,c.identity,7,8);
            const auto content=pixels.SubtitleContent(NormalizationBase(),64);
            Assert::IsFalse(content.upper||content.lower);
            VerticalBarContentInput input;input.lowerBarPixels=64;
            const auto negative=SelectVerticalBarContentPresentation(EvaluateVerticalBarContent(input),
                action==VerticalBarPresentationAction::TRANSLATE,&input);
            VerticalBarPresentationUpdateInput update;update.previous=state;update.current=negative;
            update.currentTick=3101;update.currentSourceSequence=3;update.holdMs=2000;update.translationEnabled=true;
            state=UpdateVerticalBarPresentation(update);
            Assert::IsTrue(state.action==VerticalBarPresentationAction::NONE);
            Assert::IsFalse(IsVerticalBarPresentationActiveForFrame(state,3101,2000,3,true,true,2,2));
            receipt.Commit(c,!SourceAnalysisHasTimedDependency(state,3,false,false),true);
            Assert::IsTrue(receipt.ReuseAnalysis(c,SourceRepeatKind::CadenceRepeat));
            state.action=action;state.sourceSequence=3;state.lastDetectionTick=1000;
            Assert::IsFalse(SourceAnalysisHasTimedDependency(state,3,false,false));
            Assert::IsFalse(SourceBarInspectionNeedsExpiryRefresh(state,999999,0,3));
            Assert::IsTrue(IsVerticalBarPresentationActive(state,999999,0,3));
            Assert::IsTrue(SourceAnalysisHasTimedDependency(state,3,true,false));
            Assert::IsTrue(SourceAnalysisHasTimedDependency(state,3,false,true));
            state.sourceSequence=2;
            Assert::IsTrue(SourceBarInspectionNeedsExpiryRefresh(state,1100,0,3));
        }
    }
    TEST_METHOD(ActualNativeCueAndCleanupAreInvariantAcrossOutputRatesAndQueuedRepeats) {
        const NormalizationFrame frame;std::vector<NormalizationSnapshot> reference;
        size_t referenceAnalysisReads=0,referenceSubtitleReads=0;
        for(double displayRate:{24.,48.,60.,120.}) {
            auto p=std::make_unique<NativeNormalizationPipeline>();p->Acquire(frame);
            auto cadence=std::make_unique<TemporalRenderer>();MotionConfig::Settings settings;
            settings.smoothMotion="always";settings.deinterlace="off";
            std::vector<NormalizationSnapshot> observed;
            for(uint64_t sequence=3;sequence<=14;++sequence) {
                auto context=NormalizationContext(frame,sequence);
                cadence->Begin(settings,24,displayRate,false,0,2,sequence,double(sequence)/24,false,true);
                const size_t draws=(std::max)(size_t(1),cadence->outputs.size());
                for(size_t draw=0;draw<draws;++draw) {
                    p->Draw(context,draw?SourceRepeatKind::InsertedOutput:SourceRepeatKind::NewSource,sequence<5);
                    if(draw==0)Assert::IsTrue(p->saved.accepted.detected&&p->saved.geometry.valid&&p->saved.capSequence==sequence);
                }
                const size_t beforeAnalysis=p->analysisReads,beforeSubtitle=p->subtitleReads;
                for(int repeat=0;repeat<3;++repeat)p->Draw(context,SourceRepeatKind::CadenceRepeat,false);
                Assert::AreEqual(beforeAnalysis,p->analysisReads);Assert::AreEqual(beforeSubtitle,p->subtitleReads,
                    L"A completed queued repeat re-entered current native proof/cleanup");
                observed.push_back(p->saved);
            }
            Assert::AreEqual(size_t(12),p->analysisPasses);Assert::AreEqual(size_t(12),p->subtitlePasses);
            if(reference.empty()){reference=observed;referenceAnalysisReads=p->analysisReads;referenceSubtitleReads=p->subtitleReads;}
            else {
                for(size_t i=0;i<reference.size();++i)AssertSameNormalization(reference[i],observed[i]);
                Assert::AreEqual(referenceAnalysisReads,p->analysisReads);Assert::AreEqual(referenceSubtitleReads,p->subtitleReads);
            }
        }
    }
    TEST_METHOD(DiscontinuitySourcePreparationIsReusedAcrossInsertedOutputRates) {
        const NormalizationFrame frame;NormalizationSnapshot reference{};
        size_t referenceAnalysisReads=0,referenceSubtitleReads=0;
        for(double displayRate:{24.,48.,60.,120.}) {
            // Acquire a usable native cue after the host's boundary reset. The
            // event bit remains attached to this exact captured source on every draw.
            auto p=std::make_unique<NativeNormalizationPipeline>();p->Acquire(frame);
            auto c=NormalizationContext(frame,3);c.discontinuity=true;
            auto cadence=std::make_unique<TemporalRenderer>();MotionConfig::Settings settings;
            settings.smoothMotion="always";settings.deinterlace="off";
            cadence->Begin(settings,24,displayRate,false,0,2,3,3.0/24,true,true);
            const size_t draws=(std::max)(size_t(1),cadence->outputs.size());
            p->Draw(c,SourceRepeatKind::NewSource);
            Assert::IsTrue(p->saved.accepted.detected&&p->saved.geometry.valid&&p->saved.capSequence==3);
            const auto first=p->saved;
            const auto analysisReads=p->analysisReads,subtitleReads=p->subtitleReads;
            Assert::IsTrue(p->receipt.ReuseSubtitle(c,SourceRepeatKind::InsertedOutput),
                L"A successfully prepared discontinuity source must seal its exact event receipt");
            for(size_t draw=1;draw<draws+6;++draw) {
                p->Draw(c,draw<draws?SourceRepeatKind::InsertedOutput:SourceRepeatKind::CadenceRepeat,false);
                AssertSameNormalization(first,p->saved);
            }
            Assert::AreEqual(size_t(1),p->analysisPasses);Assert::AreEqual(size_t(1),p->subtitlePasses);
            Assert::AreEqual(analysisReads,p->analysisReads);Assert::AreEqual(subtitleReads,p->subtitleReads);
            if(displayRate==24.) {
                reference=first;referenceAnalysisReads=analysisReads;referenceSubtitleReads=subtitleReads;
            } else {
                AssertSameNormalization(reference,p->saved);
                Assert::AreEqual(referenceAnalysisReads,analysisReads);Assert::AreEqual(referenceSubtitleReads,subtitleReads);
            }
        }
    }
    TEST_METHOD(DiscontinuityReceiptRequiresExactEventAndSuccessfulPreparation) {
        const NormalizationFrame frame,replacement;SourcePreparationReceipt receipt;
        const auto before=NormalizationContext(frame,3);auto event=before;event.discontinuity=true;
        receipt.Commit(before,true,true);
        Assert::IsFalse(receipt.ReuseUpload(event,SourceRepeatKind::InsertedOutput),
            L"A first discontinuity event cannot borrow the prior ordinary source receipt");
        auto begun=receipt.Begin(event,SourceRepeatKind::NewSource);
        Assert::IsFalse(begun.upload||begun.analysis||begun.subtitle);
        Assert::IsFalse(receipt.ReuseUpload(before,SourceRepeatKind::InsertedOutput));
        // A reset/completed-source receipt is published only by a successful
        // invocation. Exact event equality must not admit any altered context.
        receipt.Commit(event,true,true);
        Assert::IsTrue(receipt.ReuseSubtitle(event,SourceRepeatKind::InsertedOutput));
        Assert::IsTrue(receipt.ReuseSubtitle(event,SourceRepeatKind::CadenceRepeat));
        Assert::IsFalse(receipt.ReuseUpload(event,SourceRepeatKind::NewSource));
        for(int change=0;change<25;++change) {
            auto c=event;
            switch(change) {
            case 0:c.discontinuity=false;break;case 1:++c.identity.acceptedSequence;break;
            case 2:++c.identity.sourceFrameNumber;break;case 3:++c.identity.captureTimestamp;break;
            case 4:++c.identity.transportGeneration;break;case 5:++c.identity.analysisDomainKey;break;
            case 6:++c.identity.sourceFormatGeneration;break;case 7:++c.identity.viewportGeneration;break;
            case 8:++c.identity.rendererGeneration;break;case 9:++c.resetEpoch;break;
            case 10:++c.processingEpoch;break;case 11:++c.policyGeneration;break;
            case 12:++c.continuityGeneration;break;case 13:++c.viewportEpoch;break;
            case 14:++c.sceneGeneration;break;case 15:++c.outputContract;break;
            case 16:++c.subtitleMode;break;case 17:c.configuredScreen=false;break;
            case 18:c.statisticsAnalysis=true;break;case 19:c.gpu=reinterpret_cast<const void*>(uintptr_t(2));break;
            case 20:c.source=replacement.Source();break;case 21:c.queuedSource=replacement.Source();break;
            case 22:c.source.backingOwner.reset();break;case 23:c.source.range=AnalysisLumaRange::Full;break;
            case 24:c.source.dataBytes-=2;break;
            }
            Assert::IsFalse(receipt.ReuseUpload(c,SourceRepeatKind::InsertedOutput));
            Assert::IsFalse(receipt.ReuseAnalysis(c,SourceRepeatKind::CadenceRepeat));
            Assert::IsFalse(receipt.ReuseSubtitle(c,SourceRepeatKind::InsertedOutput));
        }
        // Even a matching invocation invalidates the old publication before
        // fallible preparation; failure cannot restore it without Commit.
        begun=receipt.Begin(event,SourceRepeatKind::InsertedOutput);
        Assert::IsTrue(begun.upload&&begun.analysis&&begun.subtitle);
        Assert::IsFalse(receipt.ReuseUpload(event,SourceRepeatKind::InsertedOutput));
        for(bool analysisComplete:{false,true}) {
            receipt.Commit(event,analysisComplete,!analysisComplete);
            Assert::IsTrue(receipt.ReuseSubtitle(event,SourceRepeatKind::InsertedOutput));
            Assert::IsFalse(receipt.ReuseAnalysis(event,SourceRepeatKind::CadenceRepeat));
            Assert::IsFalse(receipt.ReuseSubtitle(event,SourceRepeatKind::CadenceRepeat));
            begun=receipt.Begin(event,SourceRepeatKind::InsertedOutput);
            Assert::IsTrue(begun.analysis&&begun.subtitle);
            receipt.Commit(event,analysisComplete,!analysisComplete);
            Assert::IsFalse(receipt.ReuseSubtitle(event,SourceRepeatKind::CadenceRepeat),
                L"Inserted event reuse cannot promote an incomplete source attempt");
        }
        receipt.Reset();Assert::IsFalse(receipt.ReuseSubtitle(event,SourceRepeatKind::InsertedOutput));
        auto p=std::make_unique<NativeNormalizationPipeline>();p->Acquire(frame);
        p->Draw(event,SourceRepeatKind::NewSource,true,SubtitleCurrentCueFrameSampleLimit,false);
        Assert::IsFalse(p->receipt.ReuseUpload(event,SourceRepeatKind::InsertedOutput));
        const auto passes=p->subtitlePasses;p->Draw(event,SourceRepeatKind::CadenceRepeat,true);
        Assert::AreEqual(passes+1,p->subtitlePasses);
        Assert::IsTrue(p->saved.geometry.valid&&p->saved.capSequence==3);
        Assert::IsTrue(p->receipt.ReuseSubtitle(event,SourceRepeatKind::InsertedOutput));
    }
    TEST_METHOD(IncompleteSameSourceCanRecoverFromLateCurrentEvidenceWithoutRenewingSourceTime) {
        const NormalizationFrame frame;auto p=std::make_unique<NativeNormalizationPipeline>();p->Acquire(frame);
        const auto c=NormalizationContext(frame,3);
        p->Draw(c,SourceRepeatKind::NewSource,false,1);
        Assert::IsFalse(p->saved.accepted.detected);Assert::IsFalse(p->saved.geometry.valid);
        Assert::IsFalse(p->receipt.ReuseSubtitle(c,SourceRepeatKind::CadenceRepeat));
        const auto analysisReads=p->analysisReads,subtitleReads=p->subtitleReads;
        const auto analysisPasses=p->analysisPasses;
        for(int inserted=0;inserted<6;++inserted)p->Draw(c,SourceRepeatKind::InsertedOutput,false,1);
        Assert::AreEqual(analysisReads,p->analysisReads);Assert::AreEqual(subtitleReads,p->subtitleReads);
        p->Draw(c,SourceRepeatKind::CadenceRepeat,true);
        Assert::IsTrue(p->saved.accepted.detected&&p->saved.geometry.valid,L"Late exact current worker did not recover the same source");
        Assert::AreEqual(uint64_t(3),p->saved.verified.acceptedSequence);
        Assert::AreEqual(analysisPasses+1,p->analysisPasses,
            L"Late current evidence must retry the coupled analysis decision stage");
        Assert::AreEqual(analysisReads,p->analysisReads,
            L"The current native pixel measurement cache can still avoid identical reads");
        Assert::IsTrue(p->subtitleReads>subtitleReads);
        const auto complete=p->saved;const auto reads=p->subtitleReads;
        for(int repeat=0;repeat<16;++repeat)p->Draw(c,SourceRepeatKind::CadenceRepeat,false);
        AssertSameNormalization(complete,p->saved);Assert::AreEqual(reads,p->subtitleReads);
    }
    TEST_METHOD(FailedSourcePreparationCannotSealReusableCleanup) {
        const NormalizationFrame frame;auto p=std::make_unique<NativeNormalizationPipeline>();p->Acquire(frame);
        const auto c=NormalizationContext(frame,3);p->Draw(c,SourceRepeatKind::NewSource,true,SubtitleCurrentCueFrameSampleLimit,false);
        Assert::IsFalse(p->receipt.ReuseUpload(c,SourceRepeatKind::CadenceRepeat));
        const auto prior=p->subtitleReads;p->Draw(c,SourceRepeatKind::CadenceRepeat,true);
        Assert::IsTrue(p->subtitleReads>prior&&p->saved.geometry.valid);
        p->receipt.Reset();Assert::IsFalse(p->receipt.ReuseSubtitle(c,SourceRepeatKind::InsertedOutput));
    }
    TEST_METHOD(NewSourceFailureCannotRestoreAbandonedOlderPreparation) {
        const NormalizationFrame frame;auto p=std::make_unique<NativeNormalizationPipeline>();p->Acquire(frame);
        const auto old=NormalizationContext(frame,3);p->Draw(old,SourceRepeatKind::NewSource);
        Assert::IsTrue(p->receipt.ReuseSubtitle(old,SourceRepeatKind::CadenceRepeat));
        for(bool sameSource:{false,true}) {
            p->Draw(old,SourceRepeatKind::NewSource);
            const auto next=NormalizationContext(frame,sameSource?3:4);
            const auto begun=p->receipt.Begin(next,sameSource?SourceRepeatKind::CadenceRepeat:SourceRepeatKind::NewSource);
            Assert::IsTrue(begun.upload==sameSource&&begun.analysis==sameSource&&begun.subtitle==sameSource);
            // A failed invocation cannot republish completion, even if it began
            // with a reusable same-source packet before a later operation failed.
            Assert::IsFalse(p->receipt.ReuseUpload(old,SourceRepeatKind::CadenceRepeat));
            Assert::IsFalse(p->receipt.ReuseSubtitle(old,SourceRepeatKind::InsertedOutput));
            Assert::IsFalse(p->receipt.ReuseSubtitle(next,SourceRepeatKind::CadenceRepeat));
        }
    }
    TEST_METHOD(IncompleteEitherStageRetriesBothCurrentNativeStagesOnCadenceRepeat) {
        const NormalizationFrame frame;
        for(bool analysisComplete:{false,true}) {
            auto p=std::make_unique<NativeNormalizationPipeline>();p->Acquire(frame);
            const auto c=NormalizationContext(frame,3);p->Draw(c,SourceRepeatKind::NewSource);
            // Controlled incomplete phase receipt after actual native preparation:
            // neither asymmetric state may freeze the other phase's old decisions.
            p->receipt.Commit(c,analysisComplete,!analysisComplete);
            Assert::IsTrue(p->receipt.ReuseUpload(c,SourceRepeatKind::CadenceRepeat));
            Assert::IsFalse(p->receipt.ReuseAnalysis(c,SourceRepeatKind::CadenceRepeat));
            Assert::IsFalse(p->receipt.ReuseSubtitle(c,SourceRepeatKind::CadenceRepeat));
            const auto analysisPasses=p->analysisPasses,subtitlePasses=p->subtitlePasses;
            const auto reuse=p->receipt.Begin(c,SourceRepeatKind::InsertedOutput);
            Assert::IsTrue(reuse.upload&&reuse.analysis&&reuse.subtitle);
            p->receipt.Commit(c,analysisComplete,!analysisComplete);
            p->Draw(c,SourceRepeatKind::CadenceRepeat);
            Assert::AreEqual(analysisPasses+1,p->analysisPasses);
            Assert::AreEqual(subtitlePasses+1,p->subtitlePasses);
            Assert::IsTrue(p->saved.geometry.valid&&p->saved.capSequence==3);
        }
    }
    TEST_METHOD(ChangedCurrentContextOrImmutableBackingCannotReuseNativeCleanup) {
        const NormalizationFrame frame,replacement;auto p=std::make_unique<NativeNormalizationPipeline>();p->Acquire(frame);
        const auto original=NormalizationContext(frame,3);p->Draw(original,SourceRepeatKind::NewSource);
        Assert::IsTrue(p->saved.geometry.valid);
        for(int change=0;change<21;++change) {
            auto c=original;
            switch(change) {
            case 0:++c.identity.acceptedSequence;break;case 1:++c.identity.captureTimestamp;break;
            case 2:++c.identity.analysisDomainKey;break;case 3:++c.identity.sourceFormatGeneration;break;
            case 4:++c.processingEpoch;break;case 5:++c.policyGeneration;break;case 6:++c.continuityGeneration;break;
            case 7:++c.resetEpoch;break;case 8:++c.viewportEpoch;break;case 9:++c.sceneGeneration;break;
            case 10:++c.outputContract;break;case 11:++c.subtitleMode;break;case 12:c.configuredScreen=false;break;
            case 13:c.gpu=reinterpret_cast<const void*>(uintptr_t(2));break;
            case 14:c.source=replacement.Source();break;case 15:c.queuedSource=replacement.Source();break;
            case 16:c.source.backingOwner.reset();break;case 17:c.source.range=AnalysisLumaRange::Full;break;
            case 18:c.source.dataBytes-=2;break;case 19:c.discontinuity=true;break;
            case 20:c.statisticsAnalysis=true;break;
            }
            Assert::IsFalse(p->receipt.ReuseUpload(c,SourceRepeatKind::CadenceRepeat));
            Assert::IsFalse(p->receipt.ReuseAnalysis(c,SourceRepeatKind::InsertedOutput));
            Assert::IsFalse(p->receipt.ReuseSubtitle(c,SourceRepeatKind::CadenceRepeat));
        }
        const NormalizationFrame blank(false);const auto next=NormalizationContext(blank,4);
        const auto before=p->subtitleReads;p->Draw(next,SourceRepeatKind::NewSource);
        Assert::IsTrue(p->subtitleReads>before);Assert::IsFalse(p->saved.accepted.detected);
        Assert::IsFalse(p->saved.geometry.valid);Assert::AreEqual(uint64_t(0),p->saved.capSequence);
    }
};
}
