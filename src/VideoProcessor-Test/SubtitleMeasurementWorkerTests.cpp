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
    static SubtitleBoxObservation CompleteThreeLineScan(uint64_t sequence,
        const std::vector<uint16_t>& pixels,uint64_t continuity=21) {
        auto scan=Ready(sequence).current;scan.continuityGeneration=continuity;
        scan.independentBarAnalysis=true;scan.barEvidence.available=true;
        scan.barEvidence.top=scan.pictureTop;scan.barEvidence.bottom=scan.pictureBottom;
        scan.text.signature.fill(0x55);
        for(auto& signature:scan.text.lineSignatures)signature.fill(0x55);
        auto ink=std::make_shared<SubtitleInkSnapshot>();
        ink->width=160;ink->height=90;ink->step=1;ink->sourceRows.resize(90);
        ink->rawEvidenceComplete=ink->paletteValid=true;ink->inkFloor=500;ink->blackLimit=96;
        ink->rawInk.resize((160*90+63)/64);ink->ownedInk.resize(ink->rawInk.size());
        ink->blackBacking.resize(ink->rawInk.size());
        for(int y=0;y<90;++y) {ink->sourceRows[y]=y;for(int x=0;x<160;++x) {
            const size_t i=size_t(y)*160+x;const uint64_t bit=uint64_t(1)<<(i%64);
            const int value=pixels[i]>>6;
            if(value>=500) {ink->rawInk[i/64]|=bit;ink->ownedInk[i/64]|=bit;}
            if(value<=96)ink->blackBacking[i/64]|=bit;
        }}
        scan.ink=ink;return scan;
    }
    static SubtitleBoxPreview ReassociateThreeLineCue(SubtitleBoxPresentation& tracker,
        const std::vector<uint16_t>& pixels,SubtitleBoxResult& accepted) {
        std::array<SubtitleBoxObservation,4> initial;
        for(size_t i=0;i<initial.size();++i)initial[i]=CompleteThreeLineScan(i+1,pixels);
        const auto acquired=tracker.Consume(SubtitleBoxLookahead::Resolve(initial.data(),initial.size(),11,21));
        Assert::IsTrue(acquired.detected);Assert::AreEqual(3,acquired.lineCount);
        auto current=CompleteThreeLineScan(5,pixels,22);
        auto preview=SubtitleBoxLookahead::Resolve(&current,1,11,22);
        Assert::IsFalse(preview.text.detected,L"One ready frame cannot freshly acquire a crowded cue.");
        accepted=tracker.Consume(preview,250,40,&initial[0].identity,true);
        Assert::IsTrue(accepted.detected);Assert::AreEqual(3,accepted.lineCount);
        Assert::IsFalse(accepted.held);Assert::AreEqual(acquired.cue,accepted.cue);
        Assert::AreEqual("backlog-current-cue-reassociated",tracker.DecisionReason());
        Assert::IsTrue(tracker.AcceptedCueSeedCertificate().Matches(preview,accepted));
        return preview;
    }
    static SubtitleBoxPreview PendingAfterReassociation(uint64_t sequence) {
        auto pending=Pending(sequence);pending.continuityGeneration=pending.current.continuityGeneration=22;
        return pending;
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
public:
    TEST_METHOD(NegativeClearanceDemandIsPartOfWorkerCacheIdentity) {
        auto a=Ready(42).current;auto b=a;
        Assert::IsTrue(SubtitleMeasurementWorker::SameKey(a,b));
        b.requestNegativeClearance=true;
        Assert::IsFalse(SubtitleMeasurementWorker::SameKey(a,b));
        a.requestNegativeClearance=true;
        Assert::IsTrue(SubtitleMeasurementWorker::SameKey(a,b));
    }
    TEST_METHOD(FaultCapturePredicateRequiresCurrentSharedFailureAndBoundedSource) {
        auto pixels=Pixels();
        auto source=Source(pixels);
        auto key=Ready(17).current;
        auto result=key;
        result.text={};
        result.pictureBottom=source.height;
        result.sharedProbeAttempted=result.sharedProbeDetected=true;
        Assert::IsTrue(IsSubtitleProofFailureCaptureCandidate(source,key,result));
        auto changed=result;changed.sharedProbeDetected=false;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        changed=result;changed.sharedProbeAttempted=false;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        changed=result;changed.sharedProbeWorkLimit=true;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        changed=result;changed.text.workLimit=true;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        changed=result;changed.text.detected=true;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        changed=result;changed.pictureBottom=key.sharedPicture.bounds.bottom;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        changed=result;changed.identity.acceptedSequence++;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        changed=result;changed.pendingRefresh=true;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        auto unavailable=key;unavailable.sharedPicture.required=false;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,unavailable,result));
        auto invalid=source;invalid.data=nullptr;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(invalid,key,result));
        auto oversized=source;oversized.dataBytes=64u*1024u*1024u+1u;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(oversized,key,result));
    }
    TEST_METHOD(FaultCaptureModeWaitsForFailureAndPreservesLegacyStartupRules) {
        Assert::IsFalse(ShouldCaptureSubtitleDiagnosticFrame(false,true,true,false,17,false,true));
        Assert::IsFalse(ShouldCaptureSubtitleDiagnosticFrame(true,false,true,false,17,false,true));
        Assert::IsFalse(ShouldCaptureSubtitleDiagnosticFrame(true,true,true,false,17,false,false));
        Assert::IsTrue(ShouldCaptureSubtitleDiagnosticFrame(true,true,true,false,17,true,true));
        Assert::IsTrue(ShouldCaptureSubtitleDiagnosticFrame(true,true,false,false,17,false,false));
        Assert::IsFalse(ShouldCaptureSubtitleDiagnosticFrame(true,true,false,false,17,true,true));
        Assert::IsFalse(ShouldCaptureSubtitleDiagnosticFrame(true,true,false,true,239,true,true));
        Assert::IsTrue(ShouldCaptureSubtitleDiagnosticFrame(true,true,false,true,240,true,false));
    }

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
    TEST_METHOD(CompletedThreeLineBacklogScanSeedsNextPendingOnlyWithTrackerCertificate) {
        auto pixels=PixelsWithCurrentBars();SubtitleBoxPresentation tracker;SubtitleBoxResult accepted;
        const auto preview=ReassociateThreeLineCue(tracker,pixels,accepted);
        SubtitlePendingMeasurementGuard guard;
        Assert::IsFalse(guard.RememberAccepted(preview,accepted,Source(pixels),40,250));
        auto missing=PendingAfterReassociation(6);
        Assert::IsFalse(guard.Resolve(missing,Source(pixels),40,250),L"Reproduces the old no-accepted-seed loss.");
        Assert::IsTrue(guard.RememberAccepted(preview,accepted,Source(pixels),40,250,
            &tracker.AcceptedCueSeedCertificate()));
        Assert::AreEqual("accepted-existing-cue-certified",guard.DecisionReason());
        auto pending=PendingAfterReassociation(6);
        Assert::IsTrue(guard.Resolve(pending,Source(pixels),40,250));
        const auto retained=tracker.Consume(pending,250,40);
        Assert::IsTrue(retained.detected);Assert::AreEqual(3,retained.lineCount);
        Assert::AreEqual(accepted.cue,retained.cue);
        for(int line=0;line<3;++line)
            Assert::AreEqual(accepted.lineBounds[line].top,retained.lineBounds[line].top);
    }
    TEST_METHOD(UnresolvedFreshThreeLineCueCannotMintPendingSeedCertificate) {
        auto pixels=PixelsWithCurrentBars();auto current=CompleteThreeLineScan(1,pixels);
        const auto preview=SubtitleBoxLookahead::Resolve(&current,1,11,21);
        SubtitleBoxPresentation tracker;const auto accepted=tracker.Consume(preview);
        Assert::IsFalse(accepted.detected);
        SubtitlePendingMeasurementGuard guard;
        Assert::IsFalse(guard.RememberAccepted(preview,current.text,Source(pixels),40,250,
            &tracker.AcceptedCueSeedCertificate()));
        const SubtitleAcceptedCueSeedCertificate empty;
        Assert::IsFalse(guard.RememberAccepted(preview,current.text,Source(pixels),40,250,&empty));
    }
    TEST_METHOD(CertifiedSeedRejectsPartialRawChangedInkContextAndHeldRenewal) {
        for(int defect=0;defect<23;++defect) {
            auto pixels=PixelsWithCurrentBars();SubtitleBoxPresentation tracker;SubtitleBoxResult accepted;
            auto preview=ReassociateThreeLineCue(tracker,pixels,accepted);
            const auto certificate=tracker.AcceptedCueSeedCertificate();
            if(defect==0)preview.current.text.lineCount=2;
            if(defect==1)preview.current.text.capturePanel.top=60;
            if(defect==2)preview.current.identity.acceptedSequence++;
            if(defect==3)preview.current.identity.transportGeneration++;
            if(defect==4)preview.current.identity.sourceFormatGeneration++;
            if(defect==5)preview.current.identity.analysisDomainKey++;
            if(defect==6)preview.current.identity.viewportGeneration++;
            if(defect==7)preview.current.identity.rendererGeneration++;
            if(defect==8)preview.policyGeneration++;
            if(defect==9)preview.continuityGeneration++;
            if(defect==10)preview.current.pendingRefresh=true;
            if(defect==11)preview.current.analysisRefresh=true;
            if(defect==12)preview.current.discontinuity=true;
            if(defect==13)preview.current.independentBarAnalysis=false;
            if(defect==14)preview.current.text.workLimit=true;
            if(defect==15)accepted.held=true;
            if(defect==16)accepted.revised=true;
            if(defect==17)accepted.lineBounds[0].top++;
            if(defect==18)preview.current.pictureBottom++;
            if(defect==19)preview.current.barEvidence.bottomRevalidatedAtReference=true;
            if(defect==20)preview.current.barEvidence.captionBackedBottom=true;
            if(defect==21)preview.current.scriptHint=SubtitleScriptHint::Arabic;
            if(defect==22) {
                auto changed=std::make_shared<SubtitleInkSnapshot>(*preview.current.ink);
                changed->rawInk.assign(changed->rawInk.size(),0);preview.current.ink=changed;
            }
            SubtitlePendingMeasurementGuard guard;
            Assert::IsFalse(guard.RememberAccepted(preview,accepted,Source(pixels),40,250,&certificate));
            auto pending=PendingAfterReassociation(6);Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        }
    }
    TEST_METHOD(CertifiedSeedCannotHoldRemovedLineCueEndOrLostPhysicalBar) {
        for(int change=0;change<5;++change) {
            auto pixels=PixelsWithCurrentBars();SubtitleBoxPresentation tracker;SubtitleBoxResult accepted;
            const auto preview=ReassociateThreeLineCue(tracker,pixels,accepted);
            SubtitlePendingMeasurementGuard guard;
            Assert::IsTrue(guard.RememberAccepted(preview,accepted,Source(pixels),40,250,
                &tracker.AcceptedCueSeedCertificate()));
            if(change<3)for(int y=50+12*change;y<56+12*change;++y)for(int x=50;x<110;++x)
                pixels[y*160+x]=uint16_t((y<78?300:64)<<6);
            if(change==3)std::fill(pixels.begin(),pixels.begin()+160*90,uint16_t(64<<6));
            if(change==4)for(int y=0;y<90;++y)for(int x=0;x<160;++x)
                if(y<12 || y>=78)pixels[y*160+x]=uint16_t(300<<6);
            auto pending=PendingAfterReassociation(6);
            Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
            Assert::IsFalse(pending.available);
        }
    }
    TEST_METHOD(ExportedCertificateCannotRenewPendingBudgetOrSurviveRepeatedConsume) {
        auto pixels=PixelsWithCurrentBars();SubtitleBoxPresentation tracker;SubtitleBoxResult accepted;
        const auto preview=ReassociateThreeLineCue(tracker,pixels,accepted);
        const auto certificate=tracker.AcceptedCueSeedCertificate();SubtitlePendingMeasurementGuard guard;
        Assert::IsTrue(guard.RememberAccepted(preview,accepted,Source(pixels),40,250,&certificate));
        tracker.Consume(preview,250,40);
        Assert::IsFalse(tracker.AcceptedCueSeedCertificate().Matches(preview,accepted));
        Assert::IsFalse(guard.RememberAccepted(preview,accepted,Source(pixels),40,250,
            &tracker.AcceptedCueSeedCertificate()),L"A repeated presentation must neither clear nor renew its seed.");
        for(uint64_t sequence=6;sequence<=11;++sequence) {
            auto pending=PendingAfterReassociation(sequence);
            Assert::IsTrue(guard.Resolve(pending,Source(pixels),40,250));
            Assert::IsFalse(guard.RememberAccepted(preview,accepted,Source(pixels),40,250,&certificate));
        }
        auto expired=PendingAfterReassociation(12);Assert::IsFalse(guard.Resolve(expired,Source(pixels),40,250));
        Assert::IsFalse(guard.RememberAccepted(preview,accepted,Source(pixels),40,250,&certificate));
        tracker.Consume(preview,250,40);
        Assert::IsFalse(tracker.AcceptedCueSeedCertificate().Matches(preview,accepted));
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

    TEST_METHOD(PendingQueueBridgeRequiresCurrentNativePixelsAndClearsNonSubtitleAuthority) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        ready.current.subtitleArProof.accepted=true;
        ready.current.subtitleArDarkness.global.nearBlack=true;
        ready.futureAvailable=true;ready.followingCount=1;
        ready.following[0]=ready.current;ready.matchingFrames=3;
        ready.currentLinesConfirmed=true;ready.newScanFrames=2;ready.newScanMs=17;
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        auto pending=Pending(3);pending.current.continuityGeneration=22;pending.continuityGeneration=22;
        const auto token=ready.current.identity;
        Assert::IsTrue(ResolvePendingQueue(guard,pending,Source(pixels),40,250,&token,true,0));
        Assert::IsTrue(pending.available && pending.current.pendingRefresh);
        Assert::IsTrue(SameAnalysisFrameIdentity(Identity(3),pending.current.identity));
        Assert::AreEqual(uint64_t(22),pending.continuityGeneration);
        Assert::AreEqual(uint64_t(22),pending.current.continuityGeneration);
        Assert::AreEqual(ready.text.cue,pending.text.cue);
        Assert::AreEqual(3,pending.text.lineCount);
        Assert::IsTrue(guard.DecisionDiagnostics().glyphSamplesChecked>0);
        Assert::IsTrue(guard.DecisionDiagnostics().barsAvailable);
        Assert::IsFalse(pending.current.subtitleArProof.accepted);
        Assert::IsFalse(pending.current.subtitleArDarkness.global.nearBlack);
        Assert::IsFalse(pending.current.barTrackingAuthority);
        Assert::IsFalse(pending.current.currentAnchorUsesTrackedEdge);
        Assert::IsFalse(pending.futureAvailable || pending.currentLinesConfirmed);
        Assert::AreEqual(0u,pending.followingCount);
        Assert::AreEqual(uint64_t(0),pending.following[0].identity.acceptedSequence);
        Assert::AreEqual(1u,pending.matchingFrames);
        Assert::AreEqual(0u,pending.newScanFrames);
        Assert::AreEqual(0.0,pending.newScanMs);
    }
    TEST_METHOD(PendingQueueBridgeNeverRelaxesOtherContextOrTokenRequirements) {
        for(int scenario=0;scenario<22;++scenario) {
            auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
            auto pending=Pending(3);pending.current.continuityGeneration=22;pending.continuityGeneration=22;
            auto token=ready.current.identity;const ActivePictureFrameIdentity* tokenPointer=&token;
            bool queueOnly=true;auto source=Source(pixels);
            switch(scenario) {
            case 0: pending.current.discontinuity=true;break;
            case 1: ++pending.current.identity.transportGeneration;break;
            case 2: ++pending.current.identity.sourceFormatGeneration;break;
            case 3: ++pending.current.identity.analysisDomainKey;break;
            case 4: ++pending.current.identity.viewportGeneration;break;
            case 5: ++pending.current.identity.rendererGeneration;break;
            case 6: ++pending.current.policyGeneration;++pending.policyGeneration;break;
            case 7: ++pending.current.nearBarDistance;break;
            case 8: pending.current.scriptHint=SubtitleScriptHint::Arabic;break;
            case 9: --pending.current.width;break;
            case 10: source.data=nullptr;break;
            case 11: queueOnly=false;break;
            case 12: tokenPointer=nullptr;break;
            case 13: --token.acceptedSequence;break;
            case 14: ++token.analysisDomainKey;break;
            case 15: ++pending.policyGeneration;break;
            case 16: ++pending.continuityGeneration;break;
            case 17: pending.current.continuityGeneration=pending.continuityGeneration=20;break;
            case 18: pending.current.continuityGeneration=pending.continuityGeneration=0;break;
            case 19: ++pending.current.optimizationMode;break;
            case 20: pending.current.identity.captureTimestamp=token.captureTimestamp;break;
            case 21: ++pending.current.identity.sourceFrameNumber;break;
            }
            Assert::IsFalse(ResolvePendingQueue(guard,pending,source,40,250,tokenPointer,queueOnly,0));
            Assert::IsFalse(pending.available);
            Assert::IsTrue(guard.DecisionDiagnostics().glyphSamplesChecked==0);
        }
    }
    TEST_METHOD(PendingQueueBridgeStillRejectsChangedGlyphChromaAndPhysicalBars) {
        for(int scenario=0;scenario<3;++scenario) {
            auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
            ready.current.sharedPicture.required=false;
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
            if(scenario==0)pixels[50*160+50]=uint16_t(1000<<6);
            if(scenario==1)pixels[160*90+25*160+50]=uint16_t(700<<6);
            if(scenario==2)for(int y=0;y<12;++y)for(int x=0;x<160;++x)
                pixels[y*160+x]=uint16_t(300<<6);
            auto pending=Pending(3);pending.current.sharedPicture.required=false;
            pending.current.continuityGeneration=pending.continuityGeneration=22;
            const auto token=ready.current.identity;
            Assert::IsFalse(ResolvePendingQueue(guard,pending,Source(pixels),40,250,&token,true,0));
            Assert::IsFalse(pending.available);
            Assert::IsTrue(guard.DecisionDiagnostics().glyphSamplesChecked>0,
                L"Queue reassociation must reach current native proof, not bypass it.");
            Assert::AreEqual(scenario==2?"current-bars-changed":"current-pixels-changed",guard.DecisionReason());
        }
    }
    TEST_METHOD(PendingQueueBridgeRequiresSeedAndStrictlyForwardSource) {
        for(int scenario=0;scenario<3;++scenario) {
            auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
            if(scenario!=0)Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
            auto pending=Pending(scenario==0?3:scenario==1?1:0);
            pending.current.continuityGeneration=pending.continuityGeneration=22;
            const auto token=ready.current.identity;
            Assert::IsFalse(ResolvePendingQueue(guard,pending,Source(pixels),40,250,&token,true,0));
            Assert::IsFalse(pending.available);
            Assert::AreEqual(scenario==0?"no-accepted-seed":"non-forward-source",guard.DecisionReason());
        }
    }
    TEST_METHOD(PendingQueueBridgeTokenMustNameLastValidatedPendingFrameNotOriginalSeed) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        const auto original=ready.current.identity;
        auto first=Pending(3);first.current.continuityGeneration=first.continuityGeneration=22;
        Assert::IsTrue(ResolvePendingQueue(guard,first,Source(pixels),40,250,&original,true,0));
        auto next=Pending(5);next.current.continuityGeneration=next.continuityGeneration=23;
        Assert::IsFalse(ResolvePendingQueue(guard,next,Source(pixels),40,250,&original,true,0));
        Assert::IsFalse(next.available);
        Assert::IsTrue(guard.DecisionDiagnostics().glyphSamplesChecked==0);
    }
    TEST_METHOD(PendingQueueBridgePreservesEntireLeaseAcrossSuccessiveRetirements) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),50,250));
        auto token=ready.current.identity;
        auto first=Pending(3);first.current.continuityGeneration=first.continuityGeneration=22;
        Assert::IsTrue(ResolvePendingQueue(guard,first,Source(pixels),50,250,&token,true,0)); // 100ms
        // Re-presenting recovered output cannot reseed or refresh its lease.
        Assert::IsFalse(ResolvePendingQueue(guard,first,Source(pixels),50,250,&token,true,0));
        auto middle=Pending(4);middle.current.continuityGeneration=middle.continuityGeneration=22;
        Assert::IsTrue(ResolvePendingQueue(guard,middle,Source(pixels),50,250,nullptr,false,0)); // 150ms
        token=middle.current.identity;
        auto last=Pending(6);last.current.continuityGeneration=last.continuityGeneration=23;
        Assert::IsTrue(ResolvePendingQueue(guard,last,Source(pixels),50,250,&token,true,0)); // 250ms
        Assert::AreEqual(uint64_t(23),last.current.continuityGeneration);
        auto expired=Pending(7);expired.current.continuityGeneration=expired.continuityGeneration=23;
        Assert::IsFalse(ResolvePendingQueue(guard,expired,Source(pixels),50,250,nullptr,false,0));
        Assert::AreEqual("pending-budget-expired",guard.DecisionReason());
        token=last.current.identity;
        auto after=Pending(9);after.current.continuityGeneration=after.continuityGeneration=24;
        Assert::IsFalse(ResolvePendingQueue(guard,after,Source(pixels),50,250,&token,true,0));
        Assert::AreEqual("no-accepted-seed",guard.DecisionReason());
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
