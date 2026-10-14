#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleMeasurementWorker.h>
#include <atomic>
#include <chrono>
#include <thread>
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
    static std::vector<uint16_t> PixelsWithCurrentBars() {
        auto pixels=Pixels();
        for(int y=12;y<78;++y)for(int x=0;x<160;++x)pixels[y*160+x]=uint16_t(300<<6);
        // Preserve the same accepted glyph rows, including the two bottom-row
        // pixels that cross the current lower bar plane.
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
    static SubtitleBoxObservation WorkerKey(uint64_t sequence,uint64_t analysisDomain) {
        SubtitleBoxObservation key;key.identity=Identity(sequence);
        key.identity.analysisDomainKey=analysisDomain;
        key.width=640;key.height=360;key.policyGeneration=11;key.continuityGeneration=21;
        return key;
    }
    static bool WaitForTake(SubtitleMeasurementWorker& worker,
        const SubtitleBoxObservation& key,SubtitleBoxObservation& result) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(std::chrono::steady_clock::now()<deadline) {
            if(worker.TryTake(key,result))return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
public:
    TEST_METHOD(CompletedWorkerResultIsAvailableToTheExactQueuedFrameRequest) {
        SubtitleMeasurementWorker worker;
        const auto key=WorkerKey(17,0xA11CE);
        std::atomic_uint calls{0};
        worker.Submit(key,[key,&calls](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            ++calls;auto result=key;result.analyzed=true;return result;
        });

        SubtitleBoxObservation ready;
        Assert::IsTrue(WaitForTake(worker,key,ready),
            L"A completed lookahead scan must be consumable when the same frame and analysis domain become current.");
        Assert::IsTrue(SubtitleMeasurementWorker::SameKey(key,ready));
        Assert::IsTrue(ready.analyzed);
        Assert::AreEqual(1u,calls.load());
    }

    TEST_METHOD(DifferentAnalysisDomainsCannotConsumeOrCoalesceEachOthersJobs) {
        SubtitleMeasurementWorker worker;
        const auto queuedKey=WorkerKey(23,0x4E4154495645ull);
        auto currentKey=queuedKey;currentKey.identity.analysisDomainKey=0x50303130ull;
        Assert::IsFalse(SubtitleMeasurementWorker::SameKey(queuedKey,currentKey),
            L"The analysis domain is part of an exact-frame measurement identity.");

        std::atomic_uint calls{0};
        const auto work=[&calls](const SubtitleBoxObservation& key) {
            return [key,&calls](SubtitleMeasurementSampler&,
                const SubtitleBarTrackingReference&) {
                ++calls;auto result=key;result.analyzed=true;return result;
            };
        };
        worker.Submit(queuedKey,work(queuedKey));
        SubtitleBoxObservation ready;
        Assert::IsFalse(worker.TryTake(currentKey,ready),
            L"A request using another analysis interpretation must not accept queued pixels.");
        worker.Submit(currentKey,work(currentKey));

        Assert::IsTrue(WaitForTake(worker,queuedKey,ready));
        Assert::IsTrue(WaitForTake(worker,currentKey,ready));
        Assert::AreEqual(2u,calls.load(),
            L"A persistent interpretation mismatch submits two independent scans for one transport frame.");
        const auto diagnostics=worker.TakeDiagnostics();
        Assert::IsTrue(diagnostics.requestMisses>=1,
            L"The mismatched lookup must be recorded as a miss.");
    }

    TEST_METHOD(StaleSharedPictureAuthorityCannotSatisfyTheCurrentFrameRequest) {
        SubtitleMeasurementWorker worker;
        auto staleKey=WorkerKey(31,0xD0A1A);
        staleKey.sharedPicture.required=true;staleKey.sharedPicture.identity=staleKey.identity;
        staleKey.sharedPicture.bounds={0,0,640,360,640,360};
        auto currentKey=staleKey;
        currentKey.sharedPicture.bounds={0,45,640,315,640,360};
        Assert::IsTrue(SameAnalysisFrameIdentity(staleKey.identity,currentKey.identity),
            L"Only the retained picture authority changed across this simulated transition.");
        Assert::IsFalse(SubtitleMeasurementWorker::SameKey(staleKey,currentKey));

        std::atomic_uint calls{0};
        const auto work=[&calls](const SubtitleBoxObservation& key) {
            return [key,&calls](SubtitleMeasurementSampler&,
                const SubtitleBarTrackingReference&) {
                ++calls;auto result=key;result.analyzed=true;return result;
            };
        };
        worker.Submit(staleKey,work(staleKey));
        SubtitleBoxObservation ready;
        Assert::IsFalse(worker.TryTake(currentKey,ready),
            L"A full-raster scan cannot be rebound to a new authority when it supplied no independent bar proof.");
        worker.Submit(currentKey,work(currentKey));

        Assert::IsTrue(WaitForTake(worker,staleKey,ready));
        Assert::IsTrue(WaitForTake(worker,currentKey,ready));
        Assert::AreEqual(2u,calls.load(),
            L"Changing only the shared-picture snapshot makes the old frame scan unavailable and queues another.");
        const auto diagnostics=worker.TakeDiagnostics();
        Assert::IsTrue(diagnostics.requestMisses>=1,
            L"The stale-authority lookup must be recorded as a miss.");
    }

    TEST_METHOD(UnavailablePictureSnapshotsCoalesceOneActiveExactFrameScan) {
        SubtitleMeasurementWorker worker;
        auto oldKey=WorkerKey(41,0xD0A1A);
        oldKey.width=160;oldKey.height=90;
        oldKey.sharedPicture.required=true;
        oldKey.sharedPicture.identity=oldKey.identity;
        ++oldKey.sharedPicture.identity.viewportGeneration;
        oldKey.sharedPicture.bounds={0,0,160,90,160,90};
        auto currentKey=oldKey;
        ++currentKey.sharedPicture.identity.rendererGeneration;
        currentKey.sharedPicture.bounds={};
        Assert::IsFalse(oldKey.sharedPicture.AvailableFor(oldKey.identity,160,90));
        Assert::IsFalse(currentKey.sharedPicture.AvailableFor(currentKey.identity,160,90));
        Assert::IsFalse(oldKey.sharedPicture.Matches(currentKey.sharedPicture));

        std::vector<uint16_t> pixels(160*90*3/2,uint16_t(512<<6));
        std::fill(pixels.begin(),pixels.begin()+160*90,uint16_t(300<<6));
        std::atomic_bool entered{false},release{false};
        std::atomic_uint calls{0};
        const auto work=[&](const SubtitleBoxObservation& key) {
            return [&,key](SubtitleMeasurementSampler& sampler,
                const SubtitleBarTrackingReference& prior) {
                ++calls;entered.store(true,std::memory_order_release);
                while(!release.load(std::memory_order_acquire))
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return sampler.Measure(Source(pixels),key,prior,24.0);
            };
        };
        worker.Submit(oldKey,work(oldKey));
        const auto enteredDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!entered.load(std::memory_order_acquire) &&
            std::chrono::steady_clock::now()<enteredDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool enteredOnTime=entered.load(std::memory_order_acquire);
        if(enteredOnTime)worker.Submit(currentKey,work(currentKey));
        release.store(true,std::memory_order_release);
        Assert::IsTrue(enteredOnTime,L"The first exact-frame scan must be active before the authority changes.");

        SubtitleBoxObservation ready;
        Assert::IsTrue(WaitForTake(worker,currentKey,ready),
            L"The completed raw-current-frame result remains consumable while both presentation snapshots are unavailable.");
        Assert::IsTrue(ready.pictureAuthorityUnavailableForMeasurement);
        Assert::IsFalse(ready.text.detected);
        Assert::AreEqual(1u,calls.load(),
            L"Changing only between unavailable authorities must not enqueue a duplicate active scan.");
        const auto diagnostics=worker.TakeDiagnostics();
        Assert::AreEqual<uint64_t>(1,diagnostics.fullScans);
    }

    TEST_METHOD(AcceptedCompleteExtractionSeedsCurrentPixelPendingFallback) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        const auto bars=ExtractSubtitleBarEvidence(Source(pixels));
        Assert::IsTrue(bars.available);Assert::AreEqual(12,bars.top);Assert::AreEqual(78,bars.bottom);
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
            auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto initial=Ready(1);
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
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        auto accepted=ready.text;accepted.lineCount=2;
        accepted.lineBounds[2]={};accepted.lineCapturePanels[2]={};accepted.linePanels[2]={};
        Assert::IsTrue(guard.RememberAccepted(ready,accepted,Source(pixels),40,250));
        auto pending=Pending(2);
        Assert::IsTrue(guard.Resolve(pending,Source(pixels),40,250));
        Assert::AreEqual(2,pending.current.text.lineCount);
        Assert::IsFalse(pending.current.text.lineBounds[2].Valid());
    }
    TEST_METHOD(PendingFallbackRejectsChangedAnalysisDomainWithIdenticalPixels) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        ready.current.identity.analysisDomainKey=11;
        ready.current.sharedPicture.required=false;
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        auto pending=Pending(2);
        pending.current.identity.analysisDomainKey=12;
        pending.current.sharedPicture.required=false;
        Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        Assert::AreEqual("pending-context-changed",guard.DecisionReason());
        Assert::IsFalse(pending.available);
    }
    TEST_METHOD(AcceptedSeedStillRejectsChangedCurrentPixels) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        pixels[50*160+50]=uint16_t(1000<<6);
        auto pending=Pending(2);
        Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        Assert::IsFalse(pending.available);
    }
};
}
