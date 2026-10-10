#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleMeasurementWorker.h>
#include <vector>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests {
TEST_CLASS(SubtitleMeasurementWorkerTests) {
    static ActivePictureFrameIdentity Identity(uint64_t sequence) {
        return {1,sequence,sequence,sequence*1000,2,3,4};
    }
    static SubtitleBoxPreview Ready(uint64_t sequence) {
        SubtitleBoxPreview preview;preview.available=true;
        preview.policyGeneration=11;preview.continuityGeneration=21;
        auto& current=preview.current;
        current.identity=Identity(sequence);current.analyzed=current.barAuthority=true;
        current.width=160;current.height=90;current.pictureTop=12;current.pictureBottom=78;
        current.policyGeneration=11;current.continuityGeneration=21;
        current.sharedPicture.required=true;current.sharedPicture.identity=current.identity;
        current.sharedPicture.bounds={0,12,160,78,160,90};
        auto& text=current.text;text.detected=true;text.lineCount=3;text.cue=1;
        text.bounds={50,50,110,80};text.capturePanelMeasured=true;
        text.capturePanel=text.sourcePanel={40,47,120,82};
        for(int line=0;line<3;++line) {
            text.lineBounds[line]={50,50+12*line,110,56+12*line};
            text.lineCapturePanels[line]=text.capturePanel;text.linePanels[line]=text.sourcePanel;
        }
        preview.text=text;return preview;
    }
    static std::vector<uint16_t> Pixels() {
        std::vector<uint16_t> pixels(160*90*3/2,uint16_t(512<<6));
        std::fill(pixels.begin(),pixels.begin()+160*90,uint16_t(64<<6));
        for(int line=0;line<3;++line)for(int y=50+12*line;y<56+12*line;++y)
            for(int x=50;x<110;++x)if(x%7<3)pixels[y*160+x]=uint16_t(700<<6);
        return pixels;
    }
    static AnalysisLumaSource Source(std::vector<uint16_t>& pixels) {
        AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels.data());
        source.dataBytes=pixels.size()*sizeof(uint16_t);source.width=160;source.height=90;
        source.rowBytes=source.chromaRowBytes=320;source.format=AnalysisLumaFormat::P010;
        source.generation=2;return source;
    }
    static SubtitleBoxPreview Pending(uint64_t sequence) {
        auto preview=Ready(sequence);preview.available=false;preview.text={};
        preview.current.analyzed=false;preview.current.text={};return preview;
    }
    static std::shared_ptr<std::vector<uint8_t>> WorkerContractPixels() {
        auto pixels=std::make_shared<std::vector<uint8_t>>(640*360*2,128);
        const auto fill=[&](int l,int t,int r,int b,int yCode) {
            for(int y=t;y<b;++y)for(int x=l;x<r;++x)(*pixels)[size_t(y)*1280+x*2+1]=uint8_t(yCode);
        };
        fill(0,0,640,360,16);fill(0,45,640,315,50);
        for(int n=0;n<18;++n){const int x=195+n*13;fill(x,326,x+8,340,128);fill(x+2,328,x+6,338,16);}
        return pixels;
    }
    static SubtitleBoxObservation WorkerContractKey() {
        SubtitleBoxObservation key;key.identity={2,100,500,12345,3332769639299398632ull,3,4};
        key.width=640;key.height=360;key.policyGeneration=11;key.continuityGeneration=21;
        key.sharedPicture.required=true;key.sharedPicture.identity=key.identity;
        key.sharedPicture.bounds={0,10,640,350,640,360};key.sharedPicture.allowAssistedReacquisition=true;
        return key;
    }
    static AnalysisLumaSource WorkerContractSource(const std::vector<uint8_t>& pixels,const SubtitleBoxObservation& key) {
        return {pixels.data(),pixels.size(),640,360,1280,0,AnalysisLumaFormat::NativeYuv422,
            VideoFrameEncoding::UYVY,ColorSpace::REC_709,SubtitleWorkerSourceGeneration(key.identity)};
    }
public:
    TEST_METHOD(AcceptedCompleteExtractionSeedsCurrentPixelPendingFallback) {
        auto pixels=Pixels();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        auto pending=Pending(2);
        Assert::IsTrue(guard.Resolve(pending,Source(pixels),40,250));
        Assert::IsTrue(pending.current.pendingRefresh);
        Assert::AreEqual(3,pending.current.text.lineCount);
        for(int line=0;line<3;++line)
            Assert::AreEqual(ready.text.lineBounds[line].top,pending.current.text.lineBounds[line].top);
    }
    TEST_METHOD(PartialDetectionPruningAndNarrowCaptureCannotSeedSmallerFallback) {
        for(int scenario=0;scenario<4;++scenario) {
            auto pixels=Pixels();SubtitlePendingMeasurementGuard guard;auto initial=Ready(1);
            Assert::IsTrue(guard.RememberAccepted(initial,initial.text,Source(pixels),40,250));
            auto partial=Ready(2);const auto accepted=partial.text;
            if(scenario==0)partial.current.text.lineCount=2;
            if(scenario==1)partial.text.lineCount=2;
            if(scenario==2)partial.current.text.capturePanel.top=60;
            if(scenario==3)partial.text.capturePanel.top=60;
            Assert::IsFalse(guard.RememberAccepted(partial,accepted,Source(pixels),40,250));
            auto pending=Pending(3);
            Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
            Assert::IsFalse(pending.available);
        }
    }
    TEST_METHOD(PendingSeedNeverPromotesUnacceptedDetectorRows) {
        auto pixels=Pixels();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        auto accepted=ready.text;accepted.lineCount=2;
        accepted.lineBounds[2]={};accepted.lineCapturePanels[2]={};accepted.linePanels[2]={};
        Assert::IsTrue(guard.RememberAccepted(ready,accepted,Source(pixels),40,250));
        auto pending=Pending(2);
        Assert::IsTrue(guard.Resolve(pending,Source(pixels),40,250));
        Assert::AreEqual(2,pending.current.text.lineCount);
        Assert::IsFalse(pending.current.text.lineBounds[2].Valid());
    }
    TEST_METHOD(AcceptedSeedStillRejectsChangedCurrentPixels) {
        auto pixels=Pixels();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        pixels[50*160+50]=uint16_t(1000<<6);
        auto pending=Pending(2);
        Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        Assert::IsFalse(pending.available);
    }
    TEST_METHOD(AssistedCandidateNeverSeedsNativeBarTrackingOrPendingFallback) {
        auto pixels=Pixels();auto ready=Ready(1);
        Assert::IsTrue(SubtitleBoxLookahead::AdvanceBarTrackingReference(ready.current).valid);
        ready.current.assistedSourceCandidate=true;
        ready.current.assistedBounds={0,12,160,78,160,90};
        Assert::IsFalse(SubtitleBoxLookahead::AdvanceBarTrackingReference(ready.current).valid);
        SubtitlePendingMeasurementGuard guard;
        Assert::IsFalse(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        auto pending=Pending(2);
        Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        // The lower-level available path must not bypass RememberAccepted.
        Assert::IsFalse(guard.Resolve(ready,Source(pixels),40,250));
        pending=Pending(2);
        Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
    }
    TEST_METHOD(AssistedAndNativeMeasurementsCannotConfirmEachOther) {
        auto native=Ready(1).current;auto assisted=Ready(2).current;
        Assert::IsTrue(SubtitleBoxLookahead::SameContext(native,assisted));
        assisted.assistedSourceCandidate=true;assisted.assistedBounds={0,12,160,78,160,90};
        Assert::IsFalse(SubtitleBoxLookahead::SameContext(native,assisted));
        SubtitleBoxObservation frames[]={native,assisted};
        const auto preview=SubtitleBoxLookahead::Resolve(frames,2,11,21);
        Assert::IsFalse(preview.futureAvailable);
        Assert::AreEqual(0u,preview.followingCount);
        native.assistedSourceCandidate=true;native.assistedBounds=assisted.assistedBounds;
        Assert::IsTrue(SubtitleBoxLookahead::SameContext(native,assisted));
        ++assisted.assistedBounds.top;
        Assert::IsFalse(SubtitleBoxLookahead::SameContext(native,assisted));
    }
    TEST_METHOD(AssistedMeasurementReuseRequiresExactFreshSourceAndAbsentNativeAuthority) {
        auto observed=Ready(1).current;observed.assistedSourceCandidate=true;
        observed.assistedBounds={0,12,160,78,160,90};
        observed.sharedPicture.bounds={0,0,160,90,160,90};
        observed.sharedPicture.allowAssistedNomination=true;
        const auto reuse=[](const SubtitleBoxObservation& value) {
            return SubtitleBoxLookahead::CanReuseMeasurement(value,Identity(1),160,90,11,21,false,{});
        };
        Assert::IsTrue(reuse(observed));
        for(int scenario=0;scenario<8;++scenario) {
            auto unsafe=observed;
            if(scenario==0)unsafe.analysisRefresh=true;
            if(scenario==1)unsafe.pendingRefresh=true;
            if(scenario==2)unsafe.text.held=true;
            if(scenario==3)unsafe.text.workLimit=true;
            if(scenario==4)unsafe.identity=Identity(2);
            if(scenario==5)unsafe.sharedPicture.bounds={0,12,160,78,160,90};
            if(scenario==6)unsafe.sharedPicture.allowAssistedNomination=false;
            if(scenario==7)++unsafe.sharedPicture.identity.sourceFormatGeneration;
            Assert::IsFalse(reuse(unsafe));
        }
    }
    TEST_METHOD(WorkerLookupKeepsOriginalAuthorityKeyForAssistedResults) {
        const auto request=Ready(1).current;auto result=request;
        result.assistedSourceCandidate=true;result.assistedBounds={0,20,160,70,160,90};
        // Provenance describes the output, not a replacement for the queued key.
        Assert::IsTrue(SubtitleMeasurementWorker::SameKey(request,result));
        result.sharedPicture.bounds=result.assistedBounds;
        Assert::IsFalse(SubtitleMeasurementWorker::SameKey(request,result));
    }
    TEST_METHOD(AssistedCurrentProofLossCannotReuseCueOnRepeatedOrLaterFrame) {
        for(int later=0;later<2;++later)for(int scenario=0;scenario<6;++scenario) {
            SubtitleBoxPresentation presentation;auto ready=Ready(1);
            ready.current.assistedSourceCandidate=true;ready.current.assistedBounds={0,12,160,78,160,90};
            Assert::IsTrue(presentation.Consume(ready).detected);
            auto failed=ready;
            if(later)failed.current.identity=Identity(2);
            if(scenario==0)failed.current.barAuthority=false;
            if(scenario==1)failed.current.text={};
            if(scenario==2)failed.current.text.held=true;
            if(scenario==3)failed.current.text.workLimit=true;
            if(scenario==4)failed.current.pendingRefresh=true;
            if(scenario==5)failed.current.analysisRefresh=true;
            Assert::IsFalse(presentation.Consume(failed).detected);
        }
    }
    TEST_METHOD(ReacquisitionPermissionWithdrawalInvalidatesRepeatedCueAndFallback) {
        for(bool later:{false,true}) {
            auto ready=Ready(1);ready.current.assistedSourceCandidate=ready.current.assistedReacquisitionCandidate=true;
            ready.current.assistedBounds={0,20,160,70,160,90};ready.current.sharedPicture.allowAssistedReacquisition=true;
            SubtitleBoxPresentation presentation;Assert::IsTrue(presentation.Consume(ready).detected);
            auto failed=ready;failed.current.sharedPicture.allowAssistedReacquisition=false;
            if(later)failed.current.identity=Identity(2);
            Assert::IsTrue(failed.current.sharedPicture.AvailableFor(failed.current.identity,160,90));
            Assert::IsFalse(presentation.Consume(failed).detected);
            Assert::IsFalse(SubtitleMeasurementWorker::SameKey(ready.current,failed.current));
            auto pixels=Pixels();SubtitlePendingMeasurementGuard guard;
            Assert::IsFalse(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
            Assert::IsFalse(SubtitleBoxLookahead::AdvanceBarTrackingReference(ready.current).valid);
        }
    }
    TEST_METHOD(WorkerFormatFingerprintReachesSearchWhileTransportTagCannotMasqueradeAsWorkerSource) {
        const auto pixels=WorkerContractPixels();const auto key=WorkerContractKey();
        const auto source=WorkerContractSource(*pixels,key);Assert::IsTrue(source.generation!=key.identity.transportGeneration);
        SubtitleMeasurementSampler sampler;const auto observed=sampler.Measure(source,key,{},24.0);
        Assert::IsTrue(observed.assistedReacquisitionAttempted);
        Assert::AreEqual("reacquisition-initial-no-bright-ink",observed.assistedReacquisitionReason);
        Assert::IsFalse(observed.assistedReacquisitionCandidate,L"a source-contract match must not waive morphology proof");
        Assert::IsTrue(SubtitleMeasurementWorker::SameKey(key,observed));
        for(int scenario=0;scenario<6;++scenario) {
            auto invalid=source;auto changed=key;
            if(scenario==0)invalid.generation=key.identity.transportGeneration;
            if(scenario==1)++invalid.generation;
            if(scenario==2)invalid.generation=0;
            if(scenario==3)++changed.width;
            if(scenario==4)changed.discontinuity=true;
            if(scenario==5)++changed.sharedPicture.identity.viewportGeneration;
            SubtitleMeasurementSampler fresh;const auto rejected=fresh.Measure(invalid,changed,{},24.0);
            Assert::IsFalse(rejected.assistedReacquisitionAttempted);Assert::IsFalse(rejected.assistedReacquisitionCandidate);
            Assert::AreEqual(scenario<4?"worker-source-context-mismatch":scenario==4?"source-discontinuity":"reacquisition-permission-unavailable",rejected.assistedReacquisitionReason);
        }
        auto disabled=key;disabled.sharedPicture.allowAssistedReacquisition=false;
        const auto off=sampler.Measure(source,disabled,{},24.0);
        Assert::IsFalse(off.assistedReacquisitionAttempted);Assert::AreEqual("not-requested",off.assistedReacquisitionReason);
    }
    TEST_METHOD(AsynchronousWorkerPreservesDistinctFormatAndTransportIdentityAndSearchDiagnostics) {
        const auto pixels=WorkerContractPixels();const auto key=WorkerContractKey();const auto source=WorkerContractSource(*pixels,key);
        SubtitleMeasurementWorker worker;
        worker.Submit(key,[pixels,source,key](SubtitleMeasurementSampler& sampler,const SubtitleBarTrackingReference& prior) {
            return sampler.Measure(source,key,prior,24.0);
        });
        auto wrong=key;++wrong.identity.sourceFormatGeneration;wrong.sharedPicture.identity=wrong.identity;
        SubtitleBoxObservation observed;bool ready=false;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(std::chrono::steady_clock::now()<deadline) {
            Assert::IsFalse(worker.TryTake(wrong,observed));
            if(worker.TryTake(key,observed)){ready=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Assert::IsTrue(ready);worker.Stop();
        Assert::IsTrue(SameActivePictureFrameIdentity(key.identity,observed.identity));
        Assert::IsTrue(SubtitleMeasurementWorker::SameKey(key,observed));
        Assert::IsTrue(observed.assistedReacquisitionAttempted);Assert::IsFalse(observed.assistedReacquisitionCandidate);
        Assert::AreEqual("reacquisition-initial-no-bright-ink",observed.assistedReacquisitionReason);
        const auto diagnostic=worker.TakeDiagnostics();
        Assert::AreEqual(uint64_t(1),diagnostic.reacquisitionRequested);Assert::AreEqual(uint64_t(1),diagnostic.reacquisitionAttempted);
        Assert::AreEqual(uint64_t(0),diagnostic.reacquisitionAccepted);Assert::AreEqual(uint64_t(0),diagnostic.reacquisitionSourceMismatch);
        Assert::AreEqual("reacquisition-initial-no-bright-ink",diagnostic.reacquisitionLastReason);
        Assert::AreEqual(uint64_t(0),worker.TakeDiagnostics().reacquisitionRequested);
    }
};
}
