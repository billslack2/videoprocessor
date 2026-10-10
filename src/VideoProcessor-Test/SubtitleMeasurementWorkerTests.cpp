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
};
}
