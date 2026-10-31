#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleMeasurementWorker.h>
#include <SubtitleCadenceGuard.h>
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
    static AnalysisLumaSource CadenceSource(std::vector<uint16_t>& pixels) {
        auto source=Source(pixels);source.generation=1;return source;
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
    TEST_METHOD(VerifiedCadenceAdvancesPendingAnchorAcrossDeterministicQueueRetirement) {
        const auto identity=[](uint64_t sequence) {
            auto value=Identity(sequence);value.analysisDomainKey=7;return value;
        };
        const auto pendingKey=[&](uint64_t sequence) {
            auto value=Pending(sequence);value.current.identity=identity(sequence);
            value.current.sharedPicture.identity=value.current.identity;return value;
        };
        auto pixels=PixelsWithCurrentBars();
        // One independently measured, entirely bar-owned caption.
        for(int y=0;y<90;++y)for(int x=0;x<160;++x)
            pixels[y*160+x]=uint16_t((y>=12 && y<78?300:64)<<6);
        for(int y=80;y<82;++y)for(int x=50;x<110;++x)
            if(x%7<3)pixels[y*160+x]=uint16_t(700<<6);
        ActivePictureDecisionTimeline timeline;timeline.Reset(1);
        for(uint64_t seq=3159;seq<=3164;++seq)Assert::IsTrue(timeline.TrackAcceptedFrame(identity(seq)));
        const auto before=timeline.ContinuityGeneration();
        auto ready=Ready(3159);ready.current=CompleteThreeLineScan(3159,pixels,before);
        ready.current.identity=identity(3159);ready.current.sharedPicture.identity=ready.current.identity;
        ready.continuityGeneration=before;ready.current.analysisStride=2;
        auto& text=ready.current.text;text.lineCount=1;text.bounds={50,80,110,82};
        text.sourcePanel=text.capturePanel={48,78,112,84};
        text.lineBounds[0]=text.bounds;text.lineBounds[1]=text.lineBounds[2]={};
        text.lineCapturePanels[0]=text.capturePanel;text.linePanels[0]=text.sourcePanel;
        text.lineCapturePanels[1]=text.lineCapturePanels[2]={};text.linePanels[1]=text.linePanels[2]={};
        ready.text=text;
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(ready,250,50);
        Assert::IsTrue(acquired.detected && !acquired.held);
        Assert::IsTrue(tracker.HasAcceptedSourceTemplate());
        auto stale=std::make_unique<SubtitlePendingMeasurementGuard>();
        auto advanced=std::make_unique<SubtitlePendingMeasurementGuard>();
        Assert::IsTrue(stale->RememberAccepted(ready,ready.text,CadenceSource(pixels),50,250));
        Assert::IsTrue(advanced->RememberAccepted(ready,ready.text,CadenceSource(pixels),50,250));
        SubtitleCutPasteGeometry g;g.valid=true;g.content=text.bounds;g.glyphLines=text.lineBounds;
        g.source=g.destination=g.generatedCleanup=g.pictureCapture=text.sourcePanel;
        g.pictureTop=12;g.pictureBottom=78;
        SubtitleCadenceGuard cadence;
        Assert::IsTrue(cadence.RememberAccepted(ready,ready.text,CadenceSource(pixels),ready.current.sharedPicture.bounds,g,50,250));
        auto skipped=pendingKey(3160);skipped.continuityGeneration=skipped.current.continuityGeneration=before;
        skipped.current.cadenceSkipped=true;skipped.current.analysisStride=2;
        const auto& proof=cadence.PrepareReference(skipped,CadenceSource(pixels),ready.current.sharedPicture.bounds,50,250);
        Assert::IsTrue(proof.valid);
        const auto tracked=tracker.Consume(proof.preview,250,50,nullptr,false,false,&proof.continuation);
        Assert::IsTrue(tracked.detected && tracked.held && proof.continuation.MatchesCue(tracked));
        Assert::AreEqual(0x1000u,tracker.InkDiagnostics().contextMismatch,
            L"Cadence deliberately strips bar metadata while retaining separately verified current glyphs.");
        Assert::IsFalse(tracker.HasAcceptedSourceTemplate(),L"Cadence cannot restore measured bar/source-template authority.");
        Assert::IsTrue(tracker.HasAcceptedQueueTemplate(),
            L"An explicitly accepted current continuation must expose its source identity for queue-only reassociation without restoring bar authority.");
        for(int fault=0;fault<20;++fault) {
            auto rejected=std::make_unique<SubtitleBoxPresentation>();
            Assert::IsTrue(rejected->Consume(ready,250,50).detected);
            auto copied=std::make_unique<SubtitleBoxPreview>(proof.preview);
            auto changed=std::make_unique<SubtitleCueContinuation>(proof.continuation);
            const SubtitleCueContinuation* supplied=changed.get();
            if(fault==0)supplied=nullptr;
            if(fault==1)changed->localGlyphsOnly=true;
            if(fault==2)++copied->current.identity.analysisDomainKey;
            if(fault==3)++changed->accepted.cue;
            if(fault==4)changed->physicalBase.bottom-=2;
            if(fault==5)++changed->identity.acceptedSequence;
            if(fault==6){++copied->policyGeneration;++copied->current.policyGeneration;}
            if(fault==7){++copied->continuityGeneration;++copied->current.continuityGeneration;}
            if(fault==8)++copied->current.nearBarDistance;
            if(fault==9)copied->current.text.workLimit=true;
            if(fault==10)changed->accepted.confirmationPending=true;
            if(fault==11)changed->accepted.workLimit=true;
            if(fault==12)copied->current.sharedPicture.bounds.bottom-=2;
            if(fault==13)copied->current.discontinuity=true;
            if(fault==14)copied->current.analysisRefresh=true;
            if(fault==15)copied->current.analysisStride=1;
            if(fault==16)copied->current.cadenceSkipped=false;
            if(fault==17)copied->current.independentBarAnalysis=true;
            if(fault==18)copied->current.scriptHint=SubtitleScriptHint::Arabic;
            if(fault==19)changed->samples=0;
            rejected->Consume(*copied,250,50,nullptr,false,false,supplied);
            Assert::IsFalse(rejected->HasAcceptedQueueTemplate(),
                L"Only a private exact accepted shared cadence certificate may replace missing bar metadata for queue identity.");
        }
        tracker.Consume(proof.preview,250,50,nullptr,false,false,&proof.continuation);
        Assert::IsTrue(SameAnalysisFrameIdentity(identity(3160),tracker.AcceptedQueueTemplateIdentity()),
            L"Repeated accepted output cannot advance or replace its original queue certificate.");
        {
            auto missingReference=std::make_unique<SubtitleBoxPresentation>();
            auto stripped=std::make_unique<SubtitleBoxPreview>(ready);
            stripped->current.barAuthority=false;stripped->current.barEvidence={};
            missingReference->Consume(*stripped,250,50);
            missingReference->Consume(proof.preview,250,50,nullptr,false,false,&proof.continuation);
            Assert::IsFalse(missingReference->HasAcceptedQueueTemplate(),
                L"A cadence certificate cannot manufacture a measured-bar acquisition reference.");
        }
        for(int fault=0;fault<18;++fault) {
            auto guarded=std::make_unique<SubtitlePendingMeasurementGuard>();
            Assert::IsTrue(guarded->RememberAccepted(ready,ready.text,CadenceSource(pixels),50,250));
            auto bad=std::make_unique<SubtitleCadenceProof>(proof);auto changedAccepted=proof.continuation.accepted;
            if(fault==0)bad->valid=false;
            if(fault==1)bad->continuation.valid=false;
            if(fault==2)++changedAccepted.cue;
            if(fault==3)++bad->continuation.previousIdentity.acceptedSequence;
            if(fault==4)++bad->continuation.continuityGeneration;
            if(fault==5)++bad->preview.current.continuityGeneration;
            if(fault==6)++bad->preview.current.identity.analysisDomainKey;
            if(fault==7)++bad->preview.current.identity.viewportGeneration;
            if(fault==8)++bad->preview.current.identity.sourceFormatGeneration;
            if(fault==9)bad->preview.current.discontinuity=true;
            if(fault==10)++bad->preview.current.identity.sourceFrameNumber;
            if(fault==11)bad->preview.current.identity.captureTimestamp=ready.current.identity.captureTimestamp;
            if(fault==12)bad->preview.current.pendingRefresh=false;
            if(fault==13)bad->samples=0;
            if(fault==14)bad->continuation.localGlyphsOnly=true;
            if(fault==15)++bad->continuation.policyGeneration;
            if(fault==16)bad->continuation.physicalBase.bottom-=2;
            if(fault==17)changedAccepted.workLimit=true;
            Assert::IsFalse(guarded->AdvanceVerifiedCadence(*bad,changedAccepted,50,250));
        }
        auto accepted=proof.continuation.accepted;
        Assert::IsTrue(advanced->AdvanceVerifiedCadence(proof,accepted,50,250));
        Assert::IsTrue(advanced->AdvanceVerifiedCadence(proof,accepted,50,250),L"Retry cannot spend or reset the lease twice.");
        {
            auto tightened=std::make_unique<SubtitlePendingMeasurementGuard>();
            Assert::IsTrue(tightened->RememberAccepted(ready,ready.text,CadenceSource(pixels),50,250));
            Assert::IsTrue(tightened->AdvanceVerifiedCadence(proof,accepted,50,250));
            Assert::IsFalse(tightened->AdvanceVerifiedCadence(proof,accepted,50,49),
                L"A same-source retry cannot survive a newly shortened hold budget.");
            Assert::AreEqual("pending-budget-expired",tightened->DecisionReason());
        }
        timeline.MarkConsumed(identity(3159));timeline.MarkConsumed(identity(3160));
        for(uint64_t seq=3161;seq<=3163;++seq)
            timeline.MarkDiscarded(identity(seq),seq,ActivePictureDiscardReason::BacklogRecovery);
        const auto after=timeline.ContinuityGeneration();auto token=tracker.AcceptedQueueTemplateIdentity();
        Assert::IsTrue(SameAnalysisFrameIdentity(identity(3160),token));
        Assert::IsTrue(timeline.OnlyBacklogRetirementBetween(token,identity(3164),before,after));
        for(int fault=0;fault<3;++fault) {
            auto changed=pixels;auto guarded=std::make_unique<SubtitlePendingMeasurementGuard>();
            Assert::IsTrue(guarded->RememberAccepted(ready,ready.text,CadenceSource(changed),50,250));
            Assert::IsTrue(guarded->AdvanceVerifiedCadence(proof,accepted,50,250));
            if(fault==0)changed[80*160+50]=uint16_t(64<<6);
            if(fault==1)changed[160*90+(80/2)*160+50]=0;
            if(fault==2)for(int y=0;y<12;++y)for(int x=0;x<160;++x)changed[y*160+x]=uint16_t(300<<6);
            auto pending=pendingKey(3164);pending.current.continuityGeneration=pending.continuityGeneration=after;
            Assert::IsFalse(guarded->Resolve(pending,CadenceSource(changed),50,250,&token,true));
            Assert::IsTrue(guarded->DecisionDiagnostics().glyphSamplesChecked>0);
        }
        auto current=pendingKey(3164);current.current.continuityGeneration=current.continuityGeneration=after;
        auto red=current;
        Assert::IsFalse(stale->Resolve(red,CadenceSource(pixels),50,250,&token,true));
        Assert::AreEqual(static_cast<uint32_t>(SubtitlePendingMeasurementGuard::ContextContinuity),stale->DecisionDiagnostics().contextMismatch);
        Assert::AreEqual(size_t(0),stale->DecisionDiagnostics().glyphSamplesChecked);
        Assert::IsTrue(advanced->Resolve(current,CadenceSource(pixels),50,250,&token,true)); // Original 250ms, not a new 200ms lease.
        Assert::IsTrue(advanced->DecisionDiagnostics().glyphSamplesChecked>0);
        Assert::IsTrue(advanced->DecisionDiagnostics().barsAvailable && advanced->DecisionDiagnostics().backlogReassociated);
        auto expired=pendingKey(3165);expired.current.continuityGeneration=expired.continuityGeneration=after;
        Assert::IsFalse(advanced->Resolve(expired,CadenceSource(pixels),50,250));
        Assert::AreEqual("pending-budget-expired",advanced->DecisionReason());
        auto unknown=pendingKey(3164);unknown.continuityGeneration=unknown.current.continuityGeneration=after;
        Assert::IsFalse(tracker.Consume(unknown,250,50,&token,true).detected);
        Assert::IsTrue(tracker.HasAcceptedQueueTemplate());
        Assert::IsTrue(SameAnalysisFrameIdentity(token,tracker.AcceptedQueueTemplateIdentity()),
            L"Intentional backlog wait preserves the same old anchor, never unknown source identity.");
        tracker.Reset();Assert::IsFalse(tracker.HasAcceptedQueueTemplate());
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
    TEST_METHOD(CadenceGuardUhdOppositeEdgeLossUsesActualTwoRowFootprintInNativeFormats) {
        for(auto format:{AnalysisLumaFormat::P010,AnalysisLumaFormat::P210,AnalysisLumaFormat::DecodedDolbyVision422}) {
            std::vector<uint16_t> pixels(size_t(3840)*2160*(format==AnalysisLumaFormat::P010?3:4)/2,uint16_t(512<<6));
            std::fill(pixels.begin(),pixels.begin()+3840*2160,uint16_t(64<<6));
            for(int y=256;y<1904;++y)for(int x=0;x<3840;++x)pixels[size_t(y)*3840+x]=uint16_t(300<<6);
            for(int y=1730;y<1904;++y)for(int x=1520;x<2340;++x)pixels[size_t(y)*3840+x]=uint16_t(64<<6);
            auto ready=Ready(1);auto& c=ready.current;c.width=3840;c.height=2160;
            c.pictureTop=0;c.pictureBottom=1904;c.analysisStride=2;
            c.sharedPicture.bounds={0,256,3840,1904,3840,2160};
            c.text.lineCount=2;c.text.bounds={1580,1750,2260,1992};
            c.text.lineBounds[0]={1580,1890,2260,1992};
            c.text.lineBounds[1]={1640,1750,2200,1848};c.text.lineBounds[2]={};
            c.text.sourcePanel={1520,1730,2340,1904};
            c.text.nearBarEligibilityMeasured=c.text.nearBarEligible=true;c.nearBarDistance=20;
            for(int line=0;line<2;++line)for(int y=c.text.lineBounds[line].top;y<c.text.lineBounds[line].bottom;++y)
                for(int x=c.text.lineBounds[line].left;x<c.text.lineBounds[line].right;++x)
                    if((x-c.text.lineBounds[line].left)%28<12)pixels[size_t(y)*3840+x]=uint16_t(700<<6);
            auto ink=std::make_shared<SubtitleInkSnapshot>();ink->width=960;ink->height=540;ink->step=4;
            ink->sourceRows.resize(540);ink->rawEvidenceComplete=ink->paletteValid=true;
            ink->inkFloor=500;ink->blackLimit=96;
            ink->rawInk.resize(8100);ink->ownedInk.resize(8100);ink->blackBacking.resize(8100);
            for(int y=0;y<540;++y) {ink->sourceRows[y]=4*y;for(int x=0;x<960;++x) {
                const size_t i=size_t(y)*960+x;const uint64_t bit=uint64_t(1)<<(i%64);
                const int value=pixels[size_t(y*4)*3840+x*4]>>6;
                if(value>=500){ink->rawInk[i/64]|=bit;ink->ownedInk[i/64]|=bit;}
                if(value<=96)ink->blackBacking[i/64]|=bit;
            }}
            c.ink=ink;ready.text=c.text;
            AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels.data());
            source.dataBytes=pixels.size()*2;source.width=3840;source.height=2160;
            source.rowBytes=source.chromaRowBytes=7680;source.format=format;source.generation=1;
            Assert::IsTrue(source.IsValid());
            SubtitleCutPasteGeometry g;g.valid=true;g.content=c.text.bounds;g.glyphLines=c.text.lineBounds;
            g.source={1480,1700,2380,2024};g.destination=g.source;g.generatedCleanup=c.text.sourcePanel;
            g.pictureCapture=c.text.sourcePanel;g.pictureTop=0;g.pictureBottom=1904;
            g=ApplySubtitleTextReduction(FitSubtitleToVisiblePicture(g,{0,256,3840,1904},20),
                30,SubtitleBoxPadding(40),true,false);
            Assert::IsTrue(g.valid);Assert::AreEqual(0.7f,g.glyphScale);
            SubtitleCadenceGuard guard;const auto started=std::chrono::steady_clock::now();
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,source,c.sharedPicture.bounds,g));
            const double seedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            auto pending=ready;pending.available=false;pending.current.identity=Identity(2);
            pending.current.sharedPicture.identity=pending.current.identity;
            pending.current.analyzed=false;pending.current.cadenceSkipped=true;pending.current.text={};pending.text={};
            // Motion outside the exact card/content footprint cannot veto.
            for(int y=256;y<1600;++y)for(int x=0;x<3840;++x)pixels[size_t(y)*3840+x]=uint16_t(450<<6);
            const auto proofStart=std::chrono::steady_clock::now();
            const auto result=guard.Prepare(pending,source,c.sharedPicture.bounds,1000.0/60,250);
            const double proofMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-proofStart).count();
            Logger::WriteMessage((std::string("UHD cadence format=")+std::to_string(int(format))+
                " seed_ms="+std::to_string(seedMs)+" proof_ms="+std::to_string(proofMs)+
                " samples="+std::to_string(result.samples)+" reason="+result.reason+"\n").c_str());
            Assert::IsTrue(result.valid);Assert::IsFalse(result.preview.current.barAuthority);
            Assert::IsTrue(result.continuation.Matches(pending.current.identity,11,21,c.sharedPicture.bounds));
            Assert::AreEqual(202520u,result.samples,L"Every native union pixel is counted once, not step-4 samples.");
            Assert::IsFalse(result.continuation.backingVerifiedBounds.Valid());
            // A source-padding pixel is not extracted or erased.
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,source,c.sharedPicture.bounds,g));
            pixels[size_t(1711)*3840+1491]=uint16_t(700<<6);
            Assert::IsTrue(guard.Prepare(pending,source,c.sharedPicture.bounds,1000.0/60,250).valid);
            for(int mutation=0;mutation<3;++mutation) {
                Assert::IsTrue(guard.RememberAccepted(ready,ready.text,source,c.sharedPicture.bounds,g));
                const int x=mutation==0?1581:mutation==1?1597:1521;
                const int y=mutation==2?1731:1893;
                const auto before=pixels[size_t(y)*3840+x];
                pixels[size_t(y)*3840+x]=uint16_t((mutation==0?64:700)<<6);
                Assert::IsFalse(guard.Prepare(pending,source,c.sharedPicture.bounds,1000.0/60,250).valid,
                    L"One native pixel between the detector grid points must revoke the certificate.");
                pixels[size_t(y)*3840+x]=before;
            }
        }
    }
    TEST_METHOD(CadenceGuardKeepsCurrentGlyphsAcrossAlternatingInputsAndMovingBackground) {
        auto pixels=Pixels();SubtitleCadenceGuard guard;SubtitleBoxPresentation tracker;unsigned rearms=0;
        for(uint64_t sequence=1;sequence<601;sequence+=2) {
            auto ready=Ready(sequence);
            ready.current=CompleteThreeLineScan(sequence,pixels);
            ready.current.analysisStride=2;ready.current.pictureTop=0;
            ready.current.independentBarAnalysis=false;ready.current.barEvidence.top=0;
            ready.text=ready.current.text;ready.futureAvailable=true;
            ready.matchingFrames=4;ready.currentLinesConfirmed=true;
            const auto accepted=tracker.Consume(ready,250,1000.0/60);
            Assert::IsTrue(accepted.detected && !accepted.held);
            Assert::AreEqual(ready.text.cue,accepted.cue);
            SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
            g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;g.generatedCleanup=ready.text.sourcePanel;
            g.pictureTop=0;g.pictureBottom=78;g.source=g.content;g.destination=g.content;
            Assert::IsTrue(guard.RememberAccepted(ready,accepted,CadenceSource(pixels),
                ready.current.sharedPicture.bounds,g,1000.0/60,250));
            if(sequence>1) {
                Assert::AreEqual("cadence-seed-rearmed",guard.DecisionReason());
                Assert::AreEqual(0u,guard.SampleCount());++rearms;
            }
            // More than the legacy eight-code raw-pixel tolerance, while the
            // unchanged neutral backing and caption-color mask remain proved.
            for(int y=47;y<82;++y)for(int x=40;x<120;++x)
                if((pixels[y*160+x]>>6)<500)pixels[y*160+x]=uint16_t((sequence%4==1?90:64)<<6);
            // Unrelated picture can change arbitrarily outside source ownership.
            for(int y=12;y<40;++y)for(int x=0;x<160;++x)
                pixels[y*160+x]=uint16_t((200+sequence%500)<<6);
            auto pending=Pending(sequence+1);pending.current.analysisStride=2;
            pending.current.cadenceSkipped=true;
            const auto result=guard.Prepare(pending,CadenceSource(pixels),ready.current.sharedPicture.bounds,1000.0/60,250);
            Assert::IsTrue(result.valid,L"an explicit skipped input must retain only freshly verified caption pixels");
            Assert::IsTrue(result.preview.available && result.preview.current.analyzed && result.preview.current.pendingRefresh);
            Assert::IsTrue(result.preview.text.held);Assert::AreEqual(ready.text.cue,result.preview.text.cue);
            Assert::IsFalse(result.preview.current.barAuthority || result.preview.current.independentBarAnalysis);
            Assert::IsFalse(result.preview.current.subtitleArProof.accepted);
            Assert::AreEqual(0u,result.preview.followingCount);Assert::IsFalse(result.preview.currentLinesConfirmed);
            const auto held=tracker.Consume(result.preview,250,1000.0/60,nullptr,false,false,&result.continuation);
            Assert::IsTrue(held.detected && held.held);Assert::AreEqual(accepted.cue,held.cue);
            Assert::IsTrue(result.samples>0 && result.samples<=SubtitleCadenceGuard::SampleBudget);
            const auto repeat=guard.Prepare(pending,CadenceSource(pixels),ready.current.sharedPicture.bounds,1000.0/60,250);
            Assert::IsTrue(repeat.valid);Assert::AreEqual(result.continuation.elapsedMs,repeat.continuation.elapsedMs);
        }
        Assert::AreEqual(299u,rearms,L"A repeated stable cue reuses its immutable native baseline after each fresh positive scan.");
    }
    TEST_METHOD(CadenceGuardRejectsGlyphChangesCleanupIntrusionAndUnknownContexts) {
        for(int fault=0;fault<16;++fault) {
            auto pixels=Pixels();auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
            ready.current.analysisStride=2;ready.current.pictureTop=0;ready.current.independentBarAnalysis=false;
            ready.text=ready.current.text;
            SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
            g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;g.generatedCleanup=ready.text.sourcePanel;
            g.pictureTop=0;g.pictureBottom=78;g.source=g.content;g.destination=g.content;
            if(fault==14)pixels[48*160+45]=uint16_t(300<<6); // baseline nonblack cleanup, outside capture
            if(fault==15){g.pictureBottom=60;g.generatedCleanup={};ready.text.sourcePanel={};ready.current.text.sourcePanel={};}
            SubtitleCadenceGuard guard;
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,CadenceSource(pixels),ready.current.sharedPicture.bounds,g));
            auto pending=Pending(2);pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
            auto base=ready.current.sharedPicture.bounds;auto source=CadenceSource(pixels);
            if(fault==0)for(int y=50;y<56;++y)for(int x=50;x<110;++x)pixels[y*160+x]=uint16_t(64<<6);
            if(fault==1)pixels[51*160+50]=uint16_t(64<<6);
            if(fault==2)pixels[51*160+53]=uint16_t(700<<6); // new glyph stroke
            if(fault==3)pixels[48*160+45]=uint16_t(300<<6); // exposed picture in cleanup
            if(fault==4)++pending.current.identity.analysisDomainKey;
            if(fault==5)++pending.current.identity.viewportGeneration;
            if(fault==6)pending.current.discontinuity=true;
            if(fault==7){pending.current.identity=Identity(4);pending.current.sharedPicture.identity=pending.current.identity;}
            if(fault==8)pending.current.cadenceSkipped=false;
            if(fault==9)pending.current.analysisStride=1;
            if(fault==10)++base.bottom;
            if(fault==11)source.range=AnalysisLumaRange::Full;
            if(fault==12)pending.current.identity.captureTimestamp=ready.current.identity.captureTimestamp;
            if(fault==13)source.dataBytes=1;
            if(fault==14)pixels[48*160+45]=uint16_t(700<<6);
            if(fault==15)pixels[70*160+70]=uint16_t(700<<6); // erased bar gap between rows
            Assert::IsFalse(guard.Prepare(pending,source,base,1000.0/60,250).valid);
        }
    }
    TEST_METHOD(CadenceGuardAllowsOnlyOwnedOuterFringeJitterAndKeepsSnapshotCurrent) {
        for(int fault=0;fault<6;++fault) {
            auto pixels=Pixels();
            // A weak outer edge of an 18-pixel stroke, plus a weak non-ink
            // neighbour of another unchanged strong edge of that same stroke.
            pixels[52*160+56]=uint16_t(505<<6);
            pixels[53*160+55]=uint16_t(495<<6);
            if(fault==3)pixels[52*160+61]=uint16_t(700<<6); // isolated punctuation
            auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
            ready.current.analysisStride=2;ready.text=ready.current.text;
            SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
            g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;g.generatedCleanup=ready.text.sourcePanel;
            g.source=g.destination=g.content;g.pictureTop=0;g.pictureBottom=78;
            SubtitleCadenceGuard guard;
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,CadenceSource(pixels),ready.current.sharedPicture.bounds,g));
            pixels[52*160+56]=uint16_t(495<<6);
            pixels[53*160+55]=uint16_t(505<<6);
            if(fault==1)pixels[52*160+61]=uint16_t(700<<6); // disconnected new glyph
            if(fault==2)pixels[52*160+57]=uint16_t(64<<6); // lost strong interior
            if(fault==3)pixels[52*160+61]=uint16_t(64<<6); // punctuation disappeared
            if(fault==4)pixels[52*160+56]=uint16_t(450<<6); // beyond encoded fringe
            if(fault==5)pixels[48*160+45]=uint16_t(110<<6); // noncaption cleanup intrusion
            auto pending=Pending(2);pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
            const auto proof=guard.Prepare(pending,CadenceSource(pixels),ready.current.sharedPicture.bounds,1000.0/60,250);
            if(fault)Assert::IsFalse(proof.valid);
            else {
                Assert::IsTrue(proof.valid);
                Assert::IsFalse(proof.preview.current.ink->Get(56,52));
                Assert::IsTrue(proof.preview.current.ink->Get(55,53));
                Assert::IsFalse(proof.preview.current.ink->Get(55,53,true),L"New fringe is current ink, not invented old ownership.");
                Assert::IsFalse(proof.preview.current.ink->rawEvidenceComplete);
            }
        }
    }
    TEST_METHOD(CadenceGuardDoesNotTolerateFringeChangesInsideGlyphHoles) {
        for(int mutation=0;mutation<2;++mutation) {
            auto pixels=Pixels();
            std::fill(pixels.begin(),pixels.begin()+160*90,uint16_t(64<<6));
            for(int y=50;y<61;++y)for(int x=50;x<61;++x)
                if(x<52 || x>=59 || y<52 || y>=59)pixels[y*160+x]=uint16_t(700<<6);
            const int x=mutation?52:51;
            pixels[55*160+x]=uint16_t((mutation?495:505)<<6);
            auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
            auto& c=ready.current;c.analysisStride=2;c.text.lineCount=1;
            c.text.bounds=c.text.lineBounds[0]={50,50,61,61};c.text.lineBounds[1]=c.text.lineBounds[2]={};
            ready.text=c.text;
            SubtitleCutPasteGeometry g;g.valid=true;g.content=c.text.bounds;g.glyphLines=c.text.lineBounds;
            g.source=g.destination=g.content;g.pictureCapture=c.text.capturePanel;
            g.generatedCleanup=c.text.sourcePanel;g.pictureTop=0;g.pictureBottom=78;
            SubtitleCadenceGuard guard;
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,CadenceSource(pixels),c.sharedPicture.bounds,g));
            pixels[55*160+x]=uint16_t((mutation?505:495)<<6);
            auto pending=Pending(2);pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
            Assert::IsFalse(guard.Prepare(pending,CadenceSource(pixels),c.sharedPicture.bounds,1000.0/60,250).valid,
                L"Inner hole pixels and the strokes separating holes from outside stay strict.");
        }
    }
    TEST_METHOD(CadenceGuardRoundedCleanupPreservesMovingCornersAndRejectsActualEraseIntrusion) {
        for(int fault=0;fault<2;++fault) {
            auto pixels=Pixels();auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
            ready.current.analysisStride=2;ready.text=ready.current.text;
            SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
            g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;g.source=g.destination=g.content;
            g.pictureTop=0;g.pictureBottom=78;g.generatedCleanup={60,47,100,78};
            for(int y=47;y<78;++y)g.generatedCleanupRows[g.generatedCleanupRowCount++]={
                y<50?60:40,y,y<50?100:120,y+1};
            SubtitleCadenceGuard guard;
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,CadenceSource(pixels),ready.current.sharedPicture.bounds,g));
            // Both points sit inside the sourcePanel/hull, but only the second
            // belongs to a shader erase span. The first corner stays live.
            pixels[48*160+(fault?65:45)]=uint16_t(300<<6);
            auto pending=Pending(2);pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
            const auto proof=guard.Prepare(pending,CadenceSource(pixels),ready.current.sharedPicture.bounds,1000.0/60,250);
            Assert::AreEqual(fault==0,proof.valid);
        }
    }
    TEST_METHOD(CadenceGuardUhdCrossingCaptionKeepsOnlyProvedRowFootprintAndGlyphFringe) {
        for(auto format:{AnalysisLumaFormat::P010,AnalysisLumaFormat::P210,AnalysisLumaFormat::DecodedDolbyVision422})
        for(int mutation=0;mutation<4;++mutation) {
            constexpr int width=3840,height=2160;
            std::vector<uint16_t> pixels(size_t(width)*height*(format==AnalysisLumaFormat::P010?3:4)/2,uint16_t(512<<6));
            std::fill(pixels.begin(),pixels.begin()+size_t(width)*height,uint16_t(64<<6));
            for(int y=250;y<1906;++y)for(int x=0;x<width;++x)pixels[size_t(y)*width+x]=uint16_t(300<<6);
            auto ready=Ready(1);auto& c=ready.current;
            c.width=width;c.height=height;c.pictureTop=250;c.pictureBottom=1906;c.analysisStride=2;
            c.sharedPicture.bounds={0,250,width,1906,width,height};
            c.text.lineCount=2;c.text.bounds={1354,1746,2470,1992};
            c.text.lineBounds[0]={1354,1892,2470,1992};
            c.text.lineBounds[1]={1640,1746,2194,1828};c.text.lineBounds[2]={};
            c.text.sourcePanel=c.text.capturePanel={1302,1740,2528,1906};
            c.text.nearBarEligibilityMeasured=c.text.nearBarEligible=true;
            for(int line=0;line<2;++line) {
                const auto& r=c.text.lineBounds[line];
                for(int y=r.top;y<r.bottom;++y)for(int x=r.left;x<r.right;++x)
                    if((x-r.left)%28<12)pixels[size_t(y)*width+x]=uint16_t(700<<6);
            }
            // One external AA fringe beside a large, surviving owned core.
            pixels[size_t(1950)*width+1366]=uint16_t(495<<6);
            auto ink=std::make_shared<SubtitleInkSnapshot>();ink->width=960;ink->height=540;ink->step=4;
            ink->sourceRows.resize(540);ink->rawEvidenceComplete=ink->paletteValid=true;
            ink->inkFloor=500;ink->blackLimit=96;
            ink->rawInk.resize(8100);ink->ownedInk.resize(8100);ink->blackBacking.resize(8100);
            for(int y=0;y<540;++y) {ink->sourceRows[y]=4*y;for(int x=0;x<960;++x) {
                const size_t at=size_t(y)*960+x;const uint64_t bit=uint64_t(1)<<(at%64);
                const int value=pixels[size_t(y*4)*width+x*4]>>6;
                if(value>=500){ink->rawInk[at/64]|=bit;ink->ownedInk[at/64]|=bit;}
                if(value<=96)ink->blackBacking[at/64]|=bit;
            }}
            c.ink=ink;ready.text=c.text;
            AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels.data());
            source.dataBytes=pixels.size()*sizeof(uint16_t);source.width=width;source.height=height;
            source.rowBytes=source.chromaRowBytes=width*2;source.generation=1;source.format=format;
            Assert::IsTrue(source.IsValid());
            SubtitleCutPasteGeometry g;g.valid=true;g.content=c.text.bounds;g.glyphLines=c.text.lineBounds;
            g.source=g.destination={1280,1712,2544,2018};g.pictureCapture=c.text.capturePanel;
            g.pictureTop=250;g.pictureBottom=1906;g.generatedCleanup=c.text.sourcePanel;
            for(int y=1740;y<1852;++y) {
                const bool rounded=y<1748 || y>=1844;
                g.generatedCleanupRows[g.generatedCleanupRowCount++]={rounded?1340:1302,y,rounded?2490:2528,y+1};
            }
            for(int y=1892;y<1906;++y)
                g.generatedCleanupRows[g.generatedCleanupRowCount++]={1354,y,2470,y+1};
            auto guard=std::make_unique<SubtitleCadenceGuard>();
            const bool seeded=guard->RememberAccepted(ready,ready.text,source,c.sharedPicture.bounds,g);
            if(!seeded)Logger::WriteMessage(guard->DecisionReason());
            Assert::IsTrue(seeded);
            if(mutation==0) {
                // Moving native picture includes the corners INSIDE the cleanup
                // hull but OUTSIDE its actual row spans. Neither is erased.
                for(int y=250;y<1906;++y)for(int x=0;x<width;++x) {
                    bool captured=false;for(int line=0;line<2;++line) {
                        const auto& r=c.text.lineBounds[line];
                        captured|=x>=r.left && x<r.right && y>=r.top && y<r.bottom;
                    }
                    bool erased=false;
                    if(y>=1740 && y<1852) {
                        const auto& r=g.generatedCleanupRows[y-1740];erased=x>=r.left && x<r.right;
                    }
                    if(y>=1892 && y<1906)erased=x>=1354 && x<2470;
                    if(!captured && !erased)pixels[size_t(y)*width+x]=uint16_t((340+((x+y)&1)*20)<<6);
                }
                Assert::AreEqual(340,int(pixels[size_t(1742)*width+1304]>>6));
            }
            if(mutation==1)pixels[size_t(1805)*width+1310]=uint16_t(340<<6); // unowned picture inside actual erasure
            if(mutation==2)pixels[size_t(1950)*width+1360]=uint16_t(64<<6); // loss of a strong owned core
            if(mutation==3)pixels[size_t(1950)*width+1366]=uint16_t(505<<6); // bounded external AA only
            auto pending=Pending(2);pending.current.width=width;pending.current.height=height;
            pending.current.pictureTop=250;pending.current.pictureBottom=1906;
            pending.current.sharedPicture.bounds=c.sharedPicture.bounds;
            pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
            const auto& proof=guard->PrepareReference(pending,source,c.sharedPicture.bounds,1000.0/60,250);
            Assert::AreEqual(mutation==0 || mutation==3,proof.valid,
                L"Only outside-footprint native motion and bounded owned external AA may continue; unowned erasure and core changes stay strict.");
            if(mutation==1)Assert::AreEqual("cadence-current-cleanup-changed",proof.reason);
            if(mutation==2)Assert::AreEqual("cadence-current-glyph-changed",proof.reason);
        }
    }
    TEST_METHOD(CadenceGuardRejectsNativeWorkOverBudgetAndUnsupportedBackground) {
        auto pixels=Pixels();auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
        ready.current.analysisStride=2;ready.text=ready.current.text;
        SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
        g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;g.source=g.destination=g.content;g.pictureTop=0;g.pictureBottom=78;
        SubtitleCadenceGuard guard;
        Assert::IsFalse(guard.RememberAccepted(ready,ready.text,CadenceSource(pixels),ready.current.sharedPicture.bounds,g,1000.0/60,250,2));
        Assert::AreEqual("cadence-unsupported-background",guard.DecisionReason());
        // A valid 1920x600 footprint exceeds the one-million native work cap.
        pixels.assign(size_t(1920)*600*3/2,uint16_t(512<<6));
        std::fill(pixels.begin(),pixels.begin()+1920*600,uint16_t(64<<6));
        auto source=CadenceSource(pixels);source.width=1920;source.height=600;source.rowBytes=source.chromaRowBytes=3840;
        auto& c=ready.current;c.width=1920;c.height=600;c.sharedPicture.bounds={0,12,1920,588,1920,600};
        auto ink=std::make_shared<SubtitleInkSnapshot>();ink->step=2;ink->width=960;ink->height=300;
        ink->sourceRows.resize(300);for(int y=0;y<300;++y)ink->sourceRows[y]=2*y;
        ink->paletteValid=ink->rawEvidenceComplete=true;ink->inkFloor=500;ink->blackLimit=96;
        ink->rawInk.resize(4500);ink->ownedInk.resize(4500);ink->blackBacking.resize(4500);c.ink=ink;
        g.generatedCleanup={0,0,1920,600};g.pictureBottom=600;
        Assert::IsFalse(guard.RememberAccepted(ready,ready.text,source,c.sharedPicture.bounds,g));
        Assert::AreEqual("cadence-sample-budget-or-source",guard.DecisionReason());
        Assert::AreEqual(SubtitleCadenceGuard::SampleBudget,guard.SampleCount());
    }
    TEST_METHOD(CadenceGuardValidatedPlanarReaderMatchesCanonicalSamplesWithUnalignedPaddedPlanes) {
        for(auto format:{AnalysisLumaFormat::P010,AnalysisLumaFormat::P210,AnalysisLumaFormat::DecodedDolbyVision422})
        for(int fault=0;fault<7;++fault) {
            auto pixels=Pixels();auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
            ready.current.analysisStride=2;ready.text=ready.current.text;
            // Deliberately odd and different pitches, plus an unaligned base.
            // Dirty padding makes accidental tight/16-bit row access observable.
            const size_t lumaPitch=327,chromaPitch=333;
            const int chromaHeight=format==AnalysisLumaFormat::P010?45:90;
            const size_t lumaBytes=lumaPitch*90;
            std::vector<uint8_t> bytes(1+lumaBytes+chromaPitch*chromaHeight,uint8_t(0xA5));
            auto* data=bytes.data()+1;
            const auto write=[&](size_t offset,unsigned code,unsigned low) {
                const unsigned word=(code<<6)|(low&63);
                data[offset]=uint8_t(word);data[offset+1]=uint8_t(word>>8);
            };
            for(int y=0;y<90;++y)for(int x=0;x<160;++x)
                write(size_t(y)*lumaPitch+size_t(x)*2,pixels[y*160+x]>>6,unsigned(x+3*y));
            for(int y=0;y<chromaHeight;++y)for(int x=0;x<80;++x) {
                const size_t offset=lumaBytes+size_t(y)*chromaPitch+size_t(x)*4;
                write(offset,512,unsigned(x+y));write(offset+2,512,unsigned(2*x+y));
            }
            auto source=CadenceSource(pixels);source.data=data;source.dataBytes=bytes.size()-1;
            source.rowBytes=lumaPitch;source.chromaRowBytes=chromaPitch;source.format=format;
            Assert::IsTrue(source.IsValid());
            SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
            g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;
            g.generatedCleanup=ready.text.sourcePanel;g.source=g.destination=g.content;
            g.pictureTop=0;g.pictureBottom=78;
            SubtitleCadenceGuard guard;
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,source,ready.current.sharedPicture.bounds,g));
            // All three formats expose upper ten bits. Altering low storage bits
            // must remain invisible even for decoded 12-bit DV storage.
            data[size_t(52)*lumaPitch+56*2]^=uint8_t(63);
            const auto chromaOffset=[&](int x,int y) {
                return lumaBytes+size_t(format==AnalysisLumaFormat::P010?y/2:y)*chromaPitch+size_t(x/2)*4;
            };
            if(fault==1)write(chromaOffset(56,52),700,31); // owned core loses caption chroma
            if(fault==2)write(chromaOffset(44,48)+2,700,31); // cleanup ceases to be neutral black
            if(fault==3)--source.dataBytes; // last plane row is truncated
            if(fault==4)source.rowBytes=319;
            if(fault==5)source.chromaRowBytes=319;
            if(fault==6)write(size_t(53)*lumaPitch+57*2,64,31); // odd native pixel/row
            auto pending=Pending(2);pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
            const auto proof=guard.Prepare(pending,source,ready.current.sharedPicture.bounds,1000.0/60,250);
            if(fault) {
                Assert::IsFalse(proof.valid);
                if(fault>=3 && fault<=5) {
                    Assert::AreEqual(0u,proof.samples,L"Invalid complete allocation is rejected before the fast reader.");
                    Assert::IsFalse(guard.RememberAccepted(ready,ready.text,source,ready.current.sharedPicture.bounds,g));
                }
            } else {
                Assert::IsTrue(proof.valid);Assert::AreEqual(2800u,proof.samples);
                for(int y=47;y<82;++y)for(int x=40;x<120;++x) {
                    AnalysisLumaSample expected;Assert::IsTrue(source.Sample(x,y,expected));
                    Assert::AreEqual(int(pixels[y*160+x]>>6),int(expected.luma));
                    Assert::AreEqual(512,int(expected.chromaU));Assert::AreEqual(512,int(expected.chromaV));
                    Assert::AreEqual(expected.luma>=500,proof.preview.current.ink->Get(x,y));
                    Assert::AreEqual(expected.luma<=96,proof.preview.current.ink->Black(x,y));
                }
            }
        }
    }
    TEST_METHOD(CadenceGuardRearmRequiresFreshIndependentScanAndExactFinalGeometry) {
        for(int fault=0;fault<8;++fault) {
            auto pixels=Pixels();auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
            ready.current.analysisStride=2;ready.text=ready.current.text;
            SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
            g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;
            g.generatedCleanup=ready.text.sourcePanel;g.source=g.destination=g.content;
            g.pictureTop=0;g.pictureBottom=78;
            SubtitleCadenceGuard guard;
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,CadenceSource(pixels),ready.current.sharedPicture.bounds,g));
            auto pending=Pending(2);pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
            Assert::IsTrue(guard.Prepare(pending,CadenceSource(pixels),ready.current.sharedPicture.bounds,1000.0/60,250).valid);
            auto fresh=ready;fresh.current.identity=Identity(fault==7?5:3);
            if(fault==1)fresh.current.text.held=true;
            if(fault==2)fresh.current.pendingRefresh=true;
            if(fault==3)g.glyphScale=0.7f;
            if(fault==4)++g.pictureCapture.left;
            if(fault==5)++fresh.current.identity.viewportGeneration;
            if(fault==6)++fresh.current.text.cue;
            fresh.current.sharedPicture.identity=fresh.current.identity;fresh.text=fresh.current.text;
            const bool remembered=guard.RememberAccepted(fresh,fresh.text,CadenceSource(pixels),fresh.current.sharedPicture.bounds,g);
            if(fault==1 || fault==2)Assert::IsFalse(remembered,L"Held/pending output cannot rearm a native baseline.");
            else {
                Assert::IsTrue(remembered);
                if(!fault) {
                    Assert::AreEqual("cadence-seed-rearmed",guard.DecisionReason());Assert::AreEqual(0u,guard.SampleCount());
                    auto next=Pending(4);next.current.analysisStride=2;next.current.cadenceSkipped=true;
                    const auto proof=guard.Prepare(next,CadenceSource(pixels),fresh.current.sharedPicture.bounds,1000.0/60,250);
                    Assert::IsTrue(proof.valid);Assert::AreEqual(uint64_t(3),proof.continuation.previousIdentity.acceptedSequence);
                    Assert::IsTrue(proof.samples>0,L"A rearmed seed still verifies every native pixel on the next skipped source.");
                } else {
                    Assert::AreEqual("cadence-seeded",guard.DecisionReason());Assert::IsTrue(guard.SampleCount()>0);
                }
            }
        }
    }
    TEST_METHOD(CadenceGuardRequiresNewStrongScanAndRejectsUnownedExtraCapture) {
        auto pixels=Pixels();auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
        ready.current.analysisStride=2;ready.current.pictureTop=0;ready.current.independentBarAnalysis=false;
        ready.text=ready.current.text;
        SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
        g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;g.generatedCleanup=ready.text.sourcePanel;
        g.pictureTop=0;g.pictureBottom=78;
        SubtitleCadenceGuard guard;
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,CadenceSource(pixels),ready.current.sharedPicture.bounds,g));
        auto pending=Pending(2);pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
        Assert::IsTrue(guard.Prepare(pending,CadenceSource(pixels),ready.current.sharedPicture.bounds,1000.0/60,250).valid);
        pending.current.identity=Identity(3);pending.current.sharedPicture.identity=pending.current.identity;
        Assert::IsFalse(guard.Prepare(pending,CadenceSource(pixels),ready.current.sharedPicture.bounds,1000.0/60,250).valid);
        Assert::AreEqual("cadence-needs-new-independent-scan",guard.DecisionReason());
        auto one=ready;one.text.lineCount=one.current.text.lineCount=1;
        one.text.bounds=one.current.text.bounds=one.text.lineBounds[0];
        g.content=one.text.bounds;g.glyphLines[1]={120,10,140,20};g.glyphLines[2]={};
        Assert::IsFalse(guard.RememberAccepted(one,one.text,CadenceSource(pixels),one.current.sharedPicture.bounds,g));
        g.glyphLines[1]={};g.glyphLines[0].left+=5;
        Assert::IsFalse(guard.RememberAccepted(one,one.text,CadenceSource(pixels),one.current.sharedPicture.bounds,g),
            L"actual owned glyphs cannot fall outside the shader capture");
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
