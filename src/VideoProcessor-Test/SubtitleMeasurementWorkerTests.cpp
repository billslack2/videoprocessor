#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleMeasurementWorker.h>
#include <atomic>
#include <chrono>
#include <mutex>
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
    static bool WaitForRead(SubtitleMeasurementWorker& worker,
        const SubtitleBoxObservation& key,SubtitleBoxObservation& result) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(std::chrono::steady_clock::now()<deadline) {
            if(worker.TryRead(key,result))return true;
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

    TEST_METHOD(RepeatedExactReadsKeepOneImmutableReadyResultAndDeduplicateSubmission) {
        SubtitleMeasurementWorker worker;
        const auto key=WorkerKey(19,0xA11CE);
        std::atomic_uint calls{0};
        const auto work=[&calls,key](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            ++calls;auto result=key;result.analyzed=true;
            result.text.cue=37;return result;
        };
        worker.Submit(key,work);
        SubtitleBoxObservation first;
        Assert::IsTrue(WaitForRead(worker,key,first));
        Assert::AreEqual(static_cast<uint64_t>(37),first.text.cue);
        first.text.cue=999;
        first.identity.acceptedSequence=999;
        worker.Submit(key,work); // Priming must not rescan a retained ready key.
        SubtitleBoxObservation second;
        Assert::IsTrue(worker.TryRead(key,second));
        Assert::AreEqual(static_cast<uint64_t>(37),second.text.cue);
        Assert::AreEqual(static_cast<uint64_t>(19),second.identity.acceptedSequence);
        Assert::AreEqual(1u,calls.load());
        const auto diagnostics=worker.TakeDiagnostics();
        Assert::AreEqual(static_cast<uint64_t>(1),diagnostics.submitted);
        Assert::AreEqual(static_cast<uint64_t>(1),diagnostics.fullScans);
        Assert::AreEqual(static_cast<uint64_t>(1),diagnostics.deduplicated);
        Assert::AreEqual(static_cast<uint64_t>(2),diagnostics.exactReads);
        Assert::AreEqual(static_cast<size_t>(1),diagnostics.ready);
    }

    TEST_METHOD(NonDestructiveReadRefusesWrongFrameDomainAndPolicy) {
        SubtitleMeasurementWorker worker;
        const auto key=WorkerKey(29,0xA11CE);
        worker.Submit(key,[key](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            auto result=key;result.analyzed=true;return result;
        });
        SubtitleBoxObservation ready;
        Assert::IsTrue(WaitForRead(worker,key,ready));
        auto wrong=key;wrong.identity.acceptedSequence++;
        Assert::IsFalse(worker.TryRead(wrong,ready));
        wrong=key;wrong.identity.analysisDomainKey++;
        Assert::IsFalse(worker.TryRead(wrong,ready));
        wrong=key;wrong.policyGeneration++;
        Assert::IsFalse(worker.TryRead(wrong,ready));
        Assert::IsTrue(worker.TryRead(key,ready));
        Assert::IsTrue(worker.TryTake(key,ready));
        Assert::IsFalse(worker.TryRead(key,ready));
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

    TEST_METHOD(SourceWindowExpiresOnlyPendingOldFramesWithoutWaitingForActiveWork) {
        SubtitleMeasurementWorker worker;
        auto active=WorkerKey(1,0xA11CE);
        std::atomic_bool entered{false},release{false};
        worker.Submit(active,[&](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            entered.store(true,std::memory_order_release);
            while(!release.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto result=active;result.analyzed=true;return result;
        });
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!entered.load(std::memory_order_acquire) &&
            std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool started=entered.load(std::memory_order_acquire);
        if(started) {
            const auto work=[](SubtitleBoxObservation key) {
                return [key](SubtitleMeasurementSampler&,
                    const SubtitleBarTrackingReference&) {
                    auto result=key;result.analyzed=true;return result;
                };
            };
            const auto expired=WorkerKey(2,0xA11CE);
            auto oldTransport=WorkerKey(20,0xA11CE);
            oldTransport.identity.transportGeneration=0;
            const auto current=WorkerKey(3,0xA11CE);
            worker.Submit(expired,work(expired));
            worker.Submit(oldTransport,work(oldTransport));
            worker.Submit(current,work(current));
            worker.BeginSourceWindow(current.identity);
            worker.Submit(expired,work(expired));
            release.store(true,std::memory_order_release);
            SubtitleBoxObservation result;
            Assert::IsTrue(WaitForRead(worker,current,result));
            Assert::IsFalse(worker.TryRead(expired,result));
            Assert::IsFalse(worker.TryRead(oldTransport,result));
            const auto diagnostics=worker.TakeDiagnostics();
            Assert::AreEqual(static_cast<uint64_t>(3),diagnostics.expired);
        } else release.store(true,std::memory_order_release);
        Assert::IsTrue(started,L"The blocked active scan must start before pending pruning.");
    }

    TEST_METHOD(FullPendingQueueKeepsCurrentAndNearestSources) {
        SubtitleMeasurementWorker worker;
        const auto active=WorkerKey(50,0xA11CE);
        std::atomic_bool entered{false},release{false};
        std::mutex executionMutex;
        std::vector<uint64_t> execution;
        worker.Submit(active,[&](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            entered.store(true,std::memory_order_release);
            while(!release.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            {std::lock_guard<std::mutex> guard(executionMutex);execution.push_back(active.identity.acceptedSequence);}
            auto result=active;result.analyzed=true;return result;
        });
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!entered.load(std::memory_order_acquire) &&
            std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool started=entered.load(std::memory_order_acquire);
        if(started) {
            const auto current=WorkerKey(51,0xA11CE);
            worker.BeginSourceWindow(current.identity);
            const auto work=[&](SubtitleBoxObservation key) {
                return [&,key](SubtitleMeasurementSampler&,
                    const SubtitleBarTrackingReference&) {
                    {std::lock_guard<std::mutex> guard(executionMutex);
                        execution.push_back(key.identity.acceptedSequence);}
                    auto result=key;result.analyzed=true;return result;
                };
            };
            for(uint64_t sequence=52;sequence<=59;++sequence) {
                const auto future=WorkerKey(sequence,0xA11CE);
                worker.Submit(future,work(future));
            }
            const auto farther=WorkerKey(60,0xA11CE);
            worker.Submit(farther,work(farther));
            worker.Submit(current,work(current));
            release.store(true,std::memory_order_release);
            SubtitleBoxObservation result;
            Assert::IsTrue(WaitForRead(worker,current,result));
            {std::lock_guard<std::mutex> guard(executionMutex);
                Assert::IsTrue(execution.size()>=2);
                Assert::AreEqual(static_cast<uint64_t>(50),execution[0]);
                Assert::AreEqual(static_cast<uint64_t>(51),execution[1],
                    L"The current source must execute immediately after the active scan.");}
            Assert::IsFalse(worker.TryRead(WorkerKey(59,0xA11CE),result));
            Assert::IsFalse(worker.TryRead(farther,result));
            const auto diagnostics=worker.TakeDiagnostics();
            Assert::AreEqual(static_cast<uint64_t>(2),diagnostics.dropped);
            Assert::AreEqual(static_cast<uint64_t>(10),diagnostics.submitted);
        } else release.store(true,std::memory_order_release);
        Assert::IsTrue(started,L"The blocked active scan must start before filling pending capacity.");
    }
    TEST_METHOD(NewTransportCurrentRequestSupersedesOldFullPendingQueueWithoutPriming) {
        SubtitleMeasurementWorker worker;
        const auto active=WorkerKey(1,0xA11CE);
        std::atomic_bool entered{false},release{false};
        worker.Submit(active,[&](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            entered.store(true,std::memory_order_release);
            while(!release.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto result=active;result.analyzed=true;return result;
        });
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!entered.load(std::memory_order_acquire) &&
            std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool started=entered.load(std::memory_order_acquire);
        if(started) {
            const auto work=[](SubtitleBoxObservation key) {
                return [key](SubtitleMeasurementSampler&,
                    const SubtitleBarTrackingReference&) {
                    auto result=key;result.analyzed=true;return result;
                };
            };
            for(uint64_t sequence=2;sequence<=9;++sequence) {
                const auto old=WorkerKey(sequence,0xA11CE);
                worker.Submit(old,work(old));
            }
            auto fresh=WorkerKey(1,0xA11CE);
            fresh.identity.transportGeneration=2;
            worker.Submit(fresh,work(fresh));
            release.store(true,std::memory_order_release);
            SubtitleBoxObservation result;
            Assert::IsTrue(WaitForRead(worker,fresh,result));
            Assert::IsFalse(worker.TryRead(WorkerKey(9,0xA11CE),result));
            const auto diagnostics=worker.TakeDiagnostics();
            Assert::AreEqual(static_cast<uint64_t>(1),diagnostics.dropped);
            Assert::AreEqual(static_cast<uint64_t>(10),diagnostics.submitted);
        } else release.store(true,std::memory_order_release);
        Assert::IsTrue(started);
    }
    TEST_METHOD(BacklogTokenReusesOnlyCompletedExactCurrentGlyphAndNativeBars) {
        SubtitleMeasurementWorker worker;
        auto oldKey=WorkerKey(51,0xA11CE);
        oldKey.continuityGeneration=21;
        std::atomic_uint scans{0};
        worker.Submit(oldKey,[oldKey,&scans](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            ++scans;
            auto scan=oldKey;
            scan.analyzed=scan.independentBarAnalysis=scan.barAuthority=true;
            scan.pictureTop=30;scan.pictureBottom=330;
            scan.barEvidence.available=true;
            scan.barEvidence.top=30;scan.barEvidence.bottom=330;
            scan.text.detected=true;scan.text.lineCount=1;
            scan.text.bounds=scan.text.lineBounds[0]={200,300,400,325};
            auto ink=std::make_shared<SubtitleInkSnapshot>();
            ink->width=2;ink->height=2;ink->step=4;
            ink->sourceRows={300,304};
            ink->rawInk={1};ink->ownedInk={1};
            scan.ink=ink;
            scan.subtitleArProof.accepted=true; // Old AR continuity must be discarded.
            return scan;
        });
        SubtitleBoxObservation first;
        Assert::IsTrue(WaitForRead(worker,oldKey,first));
        auto current=oldKey;current.continuityGeneration=22;
        SubtitleBoxObservation ready;
        Assert::IsFalse(worker.TryRead(current,ready),
            L"A continuity mismatch alone does not authorize re-association.");
        Assert::IsTrue(worker.TryRead(current,ready,true),
            L"An explicit backlog-only token may reuse the immutable exact-frame scan.");
        Assert::IsTrue(ready.continuityGeneration==22);
        Assert::IsTrue(ready.text.detected);
        Assert::IsFalse(ready.subtitleArProof.accepted);
        Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(ready,current.identity,
            current.width,current.height,current.policyGeneration,current.continuityGeneration,
            false,{},current.scriptHint));
        Assert::AreEqual(1u,scans.load());
        auto wrong=current;wrong.identity.analysisDomainKey++;
        Assert::IsFalse(worker.TryRead(wrong,ready,true));
        wrong=current;wrong.identity.acceptedSequence++;
        Assert::IsFalse(worker.TryRead(wrong,ready,true));
        wrong=current;wrong.policyGeneration++;
        Assert::IsFalse(worker.TryRead(wrong,ready,true));
        wrong=current;wrong.discontinuity=true;
        Assert::IsFalse(worker.TryRead(wrong,ready,true));
    }
    TEST_METHOD(BacklogRebindRejectsTrackedChangedOrIncompleteGlyphEvidence) {
        SubtitleBoxObservation scan=WorkerKey(61,0xA11CE);
        scan.analyzed=scan.independentBarAnalysis=scan.barAuthority=true;
        scan.pictureTop=30;scan.pictureBottom=330;
        scan.barEvidence.available=true;scan.barEvidence.top=30;scan.barEvidence.bottom=330;
        scan.text.detected=true;scan.text.lineCount=1;
        scan.ink=std::make_shared<SubtitleInkSnapshot>();
        const auto accepted=scan;
        auto unusedPredecessor=accepted;
        unusedPredecessor.hasBarTrackingReference=true;
        Assert::IsTrue(SubtitleBoxLookahead::RebindCompletedIndependentScanAfterBacklog(
            unusedPredecessor,22,true),
            L"An offered predecessor is harmless when neither edge used it.");
        Assert::IsFalse(SubtitleBoxLookahead::RebindCompletedIndependentScanAfterBacklog(
            scan,22,false));
        for(int scenario=0;scenario<7;++scenario) {
            scan=accepted;
            if(scenario==0)scan.text.detected=false;
            if(scenario==1)scan.text.held=true;
            if(scenario==2)scan.text.workLimit=true;
            if(scenario==3)scan.barTrackingAuthority=true;
            if(scenario==4)scan.barEvidence.bottomRevalidatedAtReference=true;
            if(scenario==5)scan.barEvidence.captionBackedBottom=true;
            if(scenario==6)scan.ink.reset();
            Assert::IsFalse(SubtitleBoxLookahead::RebindCompletedIndependentScanAfterBacklog(
                scan,22,true));
        }
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
        Assert::AreEqual(static_cast<uint32_t>(SubtitlePendingMeasurementGuard::ContextDomain),
            guard.DecisionDiagnostics().contextMismatch);
        Assert::IsFalse(pending.available);
    }
    TEST_METHOD(PendingGuardReportsQueueContinuityWithoutRelaxingIt) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        auto pending=Pending(2);
        ++pending.current.continuityGeneration;
        Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        Assert::AreEqual("pending-context-changed",guard.DecisionReason());
        Assert::AreEqual(static_cast<uint32_t>(SubtitlePendingMeasurementGuard::ContextContinuity),
            guard.DecisionDiagnostics().contextMismatch);
        Assert::IsTrue(guard.DecisionDiagnostics().glyphSamplesChecked==0);
        Assert::IsFalse(pending.available);
    }
    TEST_METHOD(PendingGuardReportsExactChangedGlyphPixel) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        pixels[50*160+50]=uint16_t(1000<<6);
        auto pending=Pending(2);
        Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        Assert::AreEqual("current-pixels-changed",guard.DecisionReason());
        const auto& detail=guard.DecisionDiagnostics();
        Assert::IsTrue(detail.glyphSamplesChecked>0);
        Assert::AreEqual(50,detail.glyphMismatchX);
        Assert::AreEqual(50,detail.glyphMismatchY);
        Assert::IsFalse(pending.available);
    }
    TEST_METHOD(PendingGuardReportsCurrentBarFailureWithoutCaptionAuthority) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        ready.current.sharedPicture.required=false;
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        // Fill the former top bar with picture-level pixels. Glyphs are unchanged,
        // so rejection must come from the current native bar proof.
        for(int y=0;y<12;++y)for(int x=0;x<160;++x)
            pixels[y*160+x]=uint16_t(300<<6);
        auto pending=Pending(2);pending.current.sharedPicture.required=false;
        Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        Assert::AreEqual("current-bars-changed",guard.DecisionReason());
        const auto& detail=guard.DecisionDiagnostics();
        Assert::IsTrue(detail.glyphSamplesChecked>0);
        Assert::AreEqual(12,detail.expectedTop);
        Assert::AreEqual(78,detail.expectedBottom);
        Assert::IsTrue(!detail.barsAvailable || detail.measuredTop!=detail.expectedTop);
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
