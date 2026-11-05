#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/PreparedOutwardWindow.h>
#include <vprenderer/ActivePictureMeasurementCache.h>
#include <vprenderer/PresentationAnalysisBudget.h>
#include <vector>
#include <OwnedSourcePictureObservationCache.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
    namespace
    {
        struct WindowRawMeasurement
        {
            AnalysisLumaSource source;
            ActivePictureFrameIdentity identity;
            uint64_t domainKey=0;
            ActivePictureEvidence evidence;
            ActivePictureArDarknessEvidence darkness;
            std::shared_ptr<const SourcePictureSupportResult> sourcePictureSupport;
        };
        using WindowRequest = PreparedOutwardWindowRequest<WindowRawMeasurement>;
        using WindowWorker = OptionalAnalysisWorker<PreparedOutwardWindowResult>;

        struct OwnedWindow
        {
            WindowRequest request;
            std::array<PreparedPictureEvidenceCache,3> caches;
            explicit OwnedWindow(bool unchanged=false)
            {
                const ActivePictureBounds base{0,276,3840,1884,3840,2160,3840.0/1608.0,
                    ActivePictureBounds::BarAxes::TOP_BOTTOM};
                request.context.base=base;
                request.context.policyGeneration=23; request.context.continuityGeneration=19;
                for (size_t i=0;i<3;++i)
                {
                    const int top=unchanged?276:68, bottom=unchanged?1884:2092;
                    std::vector<uint16_t> words(3840*2160*3/2,static_cast<uint16_t>(512<<6));
                    for (int y=0;y<2160;++y)
                        if (y<top || y>=bottom)
                            std::fill(words.begin()+size_t(y)*3840,words.begin()+size_t(y+1)*3840,
                                static_cast<uint16_t>(64<<6));
                    const auto pixels=std::make_shared<const std::vector<uint16_t>>(std::move(words));
                    auto& frame=request.frames[i];
                    frame.source.data=reinterpret_cast<const uint8_t*>(pixels->data());
                    frame.source.dataBytes=pixels->size()*sizeof(uint16_t);
                    frame.source.width=3840; frame.source.height=2160;
                    frame.source.rowBytes=frame.source.chromaRowBytes=3840*sizeof(uint16_t);
                    frame.source.format=AnalysisLumaFormat::P010; frame.source.generation=11;
                    frame.source.backingOwner=pixels; frame.sourceOwner=pixels;
                    frame.domain=AnalysisFrameDomain::ConvertedP010;
                    frame.profile=AnalysisSourceProfile::QueuedCapture; frame.domainKey=101;
                    frame.colorDepth=10; frame.bitShift=6;
                    request.identities[i]={7,100+i,100+i,(100+i)*417083,11,13,17,101};
                    request.context.contributors[i]=request.identities[i];
                    request.sourceRates[i]=24;
                    request.rawSlots[i]=std::make_shared<WindowRequest::RawWorker::Slot>();
                    request.retentionKeys[i]=MakePreparedPictureEvidenceKey(
                        PreparedPictureEvidenceKind::Retention,frame,request.identities[i],base,23,19,
                        request.rawSlots[i]);
                    request.retentionSlots[i]=caches[i].Request(request.retentionKeys[i]);
                }
            }
            std::shared_ptr<const WindowRawMeasurement> PublishRaw(size_t i)
            {
                auto raw=std::make_shared<WindowRawMeasurement>();
                raw->source=request.frames[i].source; raw->identity=request.identities[i];
                raw->domainKey=request.frames[i].domainKey;
                raw->evidence=DetectSourcePictureForWorker(raw->source).evidence;
                raw->darkness=EvaluateActivePictureArDarkness(raw->source,raw->evidence);
                std::shared_ptr<const WindowRawMeasurement> complete=raw;
                std::atomic_store(&request.rawSlots[i]->ready,complete);
                return complete;
            }
            PreparedPictureEvidenceKey Key(size_t i) const {
                auto key=request.retentionKeys[i];key.rawRevision=WindowRequest::RawWorker::Ready(request.rawSlots[i]);return key;
            }
            void PublishRetention(size_t i)
            {
                const auto raw=WindowRequest::RawWorker::Ready(request.rawSlots[i]);
                std::shared_ptr<const PreparedPictureEvidence> complete=
                    std::make_shared<PreparedPictureEvidence>(EvaluatePreparedPictureEvidence(
                        Key(i),raw->evidence,raw->darkness));
                std::atomic_store(&request.retentionSlots[i]->ready,complete);
            }
        };
        struct Event
        {
            HANDLE handle=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            ~Event() { if(handle)CloseHandle(handle); }
        };
        struct ReleaseOnExit
        {
            HANDLE handle;
            ~ReleaseOnExit() { SetEvent(handle); }
        };
    }

    TEST_CLASS(PreparedOutwardWindowTests)
    {
    public:
        TEST_METHOD(CompleteNativePublicationRetainsLateCurrentSupportInEitherProducerOrder)
        {
            OwnedWindow window(true);
            const auto plain=window.PublishRaw(0);
            const auto& frame=window.request.frames[0];const auto& identity=window.request.identities[0];
            SourcePictureSupportRequest request;
            const auto detected=DetectPreparedSourcePictureBounded(frame.source,identity,&request);
            Assert::IsTrue(detected.support && detected.support->currentSupport.verified);
            auto enriched=std::make_shared<WindowRawMeasurement>(*plain);
            enriched->sourcePictureSupport=detected.support;
            std::shared_ptr<const WindowRawMeasurement> rich=enriched;
            const auto slot=window.request.rawSlots[0];
            slot->completion=std::make_shared<AnalysisCompletionSignal>();
            const auto before=slot->completion->Snapshot();
            const auto selected=PublishPreparedOutwardRaw<WindowRawMeasurement>(frame,identity,slot,plain,rich);
            Assert::IsTrue(selected->sourcePictureSupport==detected.support,
                L"A complete outward/native result must not suppress later exact-current sampling support.");
            Assert::IsTrue(WindowRequest::RawWorker::Ready(slot)==selected);
            Assert::IsTrue(slot->completion->Snapshot()>before);
            Assert::AreEqual(plain->evidence.physicalEdges.estimate.top,selected->evidence.physicalEdges.estimate.top);
            Assert::AreEqual(plain->evidence.trustedBounds.top,selected->evidence.trustedBounds.top);
            const auto after=slot->completion->Snapshot();
            Assert::IsTrue(PublishPreparedOutwardRaw<WindowRawMeasurement>(frame,identity,slot,plain,plain)==selected);
            Assert::IsTrue(slot->completion->Snapshot()==after);
            // Reverse producer order is equally monotonic.
            std::atomic_store(&slot->ready,rich);
            Assert::IsTrue(PublishPreparedOutwardRaw<WindowRawMeasurement>(frame,identity,slot,rich,plain)==rich);
            // A valid independent support capability cannot rewrite established native geometry.
            std::atomic_store(&slot->ready,plain);
            auto different=std::make_shared<WindowRawMeasurement>(*enriched);
            different->evidence.physicalEdges.estimate.top+=7;
            different->evidence.trustedBounds.top+=7;
            std::shared_ptr<const WindowRawMeasurement> other=different;
            const auto preserved=PublishPreparedOutwardRaw<WindowRawMeasurement>(frame,identity,slot,plain,other);
            Assert::IsTrue(preserved->sourcePictureSupport==detected.support);
            Assert::AreEqual(plain->evidence.physicalEdges.estimate.top,preserved->evidence.physicalEdges.estimate.top);
            Assert::AreEqual(plain->evidence.trustedBounds.top,preserved->evidence.trustedBounds.top);
            for(int invalid=0;invalid<7;++invalid) {
                std::atomic_store(&slot->ready,plain);
                auto bad=std::make_shared<WindowRawMeasurement>(*enriched);
                auto proof=std::make_shared<SourcePictureSupportResult>(*detected.support);
                if(invalid==0)++proof->currentSupport.identity.acceptedSequence;
                if(invalid==1)++proof->currentSupport.identity.viewportGeneration;
                if(invalid==2)proof->currentSupport.verified=false;
                if(invalid==3)proof->currentSupport.verification.vertical=ActivePictureCandidateVerificationState::CONTRADICTED;
                if(invalid==4)++bad->domainKey;
                if(invalid==5)bad->source.backingOwner=std::make_shared<int>(9);
                if(invalid==6)proof->currentSupport.samplingBounds.rasterWidth-=2;
                bad->sourcePictureSupport=proof;
                std::shared_ptr<const WindowRawMeasurement> rejected=bad;
                PublishPreparedOutwardRaw<WindowRawMeasurement>(frame,identity,slot,plain,rejected);
                const auto final=WindowRequest::RawWorker::Ready(slot);
                if(invalid==2) {
                    Assert::IsTrue(final->sourcePictureSupport && !final->sourcePictureSupport->currentSupport.verified);
                    Assert::IsTrue(PreparedNativeSupportWorkComplete(frame,identity,final));
                } else Assert::IsTrue(final==plain,
                    L"Foreign or malformed claimed-positive support cannot enrich the current slot.");
            }
        }

        TEST_METHOD(CurrentNativeSupportAndEquivalenceCapabilitiesMergeInEveryOrder)
        {
            OwnedWindow window(true);const auto plain=window.PublishRaw(0);
            const auto& frame=window.request.frames[0];const auto& id=window.request.identities[0];
            const auto slot=window.request.rawSlots[0];
            SourcePictureSupportRequest request;
            const auto detected=DetectPreparedSourcePictureBounded(frame.source,id,&request);
            Assert::IsTrue(detected.support && detected.support->currentSupport.verified);
            auto facts=std::make_shared<const SourcePictureObservation>(MeasureSourcePicture(frame.source));
            OwnedSourcePictureObservationCache cache;SourcePictureEquivalenceToken token;
            Assert::IsTrue(cache.Store(frame.source,id,facts,0,false,&token));
            Assert::IsTrue(token.MatchesSource(id));
            auto support=std::make_shared<SourcePictureSupportResult>(*detected.support);support->sourceEquivalence={};
            auto supportRaw=std::make_shared<WindowRawMeasurement>(*plain);
            supportRaw->evidence=MissingSourcePictureEvidence(frame.source);supportRaw->sourcePictureSupport=support;
            auto tokenSupport=std::make_shared<SourcePictureSupportResult>();
            tokenSupport->currentSupport.identity=id;tokenSupport->currentSupport.reason="support-not-yet-verified";
            tokenSupport->sourceEquivalence=token;
            auto tokenRaw=std::make_shared<WindowRawMeasurement>(*supportRaw);tokenRaw->sourcePictureSupport=tokenSupport;
            std::array<std::shared_ptr<const WindowRawMeasurement>,3> producers{plain,supportRaw,tokenRaw};
            std::array<unsigned,3> order{0,1,2};unsigned permutations=0;
            do {
                std::shared_ptr<const WindowRawMeasurement> empty;
                std::atomic_store(&slot->ready,empty);
                for(auto i:order)PublishPreparedOutwardRaw<WindowRawMeasurement>(frame,id,slot,empty,producers[i]);
                const auto ready=WindowRequest::RawWorker::Ready(slot);
                Assert::IsTrue(SourcePictureMeasurementWorkComplete(ready->evidence));
                Assert::AreEqual(plain->evidence.trustedBounds.top,ready->evidence.trustedBounds.top);
                Assert::AreEqual(plain->evidence.trustedBounds.bottom,ready->evidence.trustedBounds.bottom);
                Assert::IsTrue(ready->sourcePictureSupport && ready->sourcePictureSupport->currentSupport.verified);
                Assert::IsTrue(ready->sourcePictureSupport->sourceEquivalence.SameSnapshot(token));
                Assert::AreEqual(support->currentSupport.samplingBounds.top,ready->sourcePictureSupport->currentSupport.samplingBounds.top);
                for(const auto& weaker:producers)
                    Assert::IsTrue(PublishPreparedOutwardRaw<WindowRawMeasurement>(frame,id,slot,empty,weaker)==ready);
                ++permutations;
            }while(std::next_permutation(order.begin(),order.end()));
            Assert::AreEqual(6u,permutations);
            // A foreign capability cannot enrich the current immutable slot.
            for(unsigned fault=0;fault<4;++fault) {
                std::atomic_store(&slot->ready,plain);
                auto foreign=std::make_shared<WindowRawMeasurement>(*tokenRaw);
                auto foreignSupport=std::make_shared<SourcePictureSupportResult>(*tokenSupport);
                if(fault==0)++foreign->identity.acceptedSequence;
                if(fault==1)++foreign->domainKey;
                if(fault==2)++foreignSupport->currentSupport.identity.viewportGeneration;
                if(fault==3) {
                    auto other=id;++other.rendererGeneration;SourcePictureEquivalenceToken otherToken;
                    Assert::IsTrue(bool(cache.Find(frame.source,other,0,&otherToken)));
                    foreignSupport->sourceEquivalence=otherToken;
                }
                foreign->sourcePictureSupport=foreignSupport;
                PublishPreparedOutwardRaw<WindowRawMeasurement>(frame,id,slot,plain,foreign);
                const auto result=WindowRequest::RawWorker::Ready(slot);
                Assert::IsFalse(result->sourcePictureSupport && result->sourcePictureSupport->sourceEquivalence.MatchesSource(id));
                Assert::AreEqual(plain->evidence.trustedBounds.top,result->evidence.trustedBounds.top);
            }
            // Expiration is never negative native/support evidence. A fresh
            // exact-current token can enrich the same retained proof metadata.
            std::atomic_store(&slot->ready,std::shared_ptr<const WindowRawMeasurement>(supportRaw));
            auto rich=PublishPreparedOutwardRaw<WindowRawMeasurement>(frame,id,slot,supportRaw,tokenRaw);
            cache.Clear();Assert::IsFalse(rich->sourcePictureSupport->sourceEquivalence.Valid());
            Assert::IsTrue(cache.Store(frame.source,id,facts,0,false,&token));
            auto freshSupport=std::make_shared<SourcePictureSupportResult>(*tokenSupport);freshSupport->sourceEquivalence=token;
            auto fresh=std::make_shared<WindowRawMeasurement>(*tokenRaw);fresh->sourcePictureSupport=freshSupport;
            const auto renewed=PublishPreparedOutwardRaw<WindowRawMeasurement>(frame,id,slot,rich,fresh);
            Assert::IsTrue(renewed->sourcePictureSupport->currentSupport.verified);
            Assert::IsTrue(renewed->sourcePictureSupport->sourceEquivalence.MatchesSource(id));
            Assert::IsTrue(PublishPreparedOutwardRaw<WindowRawMeasurement>(frame,id,slot,rich,tokenRaw)==renewed);
        }

        TEST_METHOD(NativeSupportAttemptCompletesOnceAfterOutwardPublicationWithoutRetryingNegatives)
        {
            OwnedWindow window(true);const auto plain=window.PublishRaw(0);
            const auto& frame=window.request.frames[0];const auto& identity=window.request.identities[0];
            const auto slot=window.request.rawSlots[0];
            Assert::IsTrue(SourcePictureMeasurementWorkComplete(plain->evidence));
            Assert::IsFalse(PreparedNativeSupportWorkComplete(frame,identity,plain));
            unsigned attempts=0;
            for(int admission=0;admission<3;++admission) {
                const auto ready=WindowRequest::RawWorker::Ready(slot);
                if(PreparedNativeSupportWorkComplete(frame,identity,ready))continue;
                ++attempts;
                auto completed=std::make_shared<WindowRawMeasurement>(*ready);
                auto terminal=std::make_shared<SourcePictureSupportResult>();
                terminal->currentSupport.identity=identity;
                terminal->currentSupport.reason="current-support-budget-exhausted";
                completed->sourcePictureSupport=terminal;
                std::shared_ptr<const WindowRawMeasurement> result=completed;
                PublishPreparedOutwardRaw<WindowRawMeasurement>(frame,identity,slot,ready,result);
            }
            Assert::AreEqual(1u,attempts);
            const auto negative=WindowRequest::RawWorker::Ready(slot);
            Assert::IsTrue(PreparedNativeSupportWorkComplete(frame,identity,negative));
            Assert::IsFalse(negative->sourcePictureSupport->currentSupport.verified);
            // Support completion neither hides native facts nor grants crop proof.
            Assert::AreEqual(plain->evidence.trustedBounds.top,negative->evidence.trustedBounds.top);
            SourcePictureSupportRequest request;
            const auto detected=DetectPreparedSourcePictureBounded(frame.source,identity,&request);
            Assert::IsTrue(detected.support && detected.support->currentSupport.verified);
            auto complete=std::make_shared<WindowRawMeasurement>(*negative);complete->sourcePictureSupport=detected.support;
            std::shared_ptr<const WindowRawMeasurement> rich=complete;
            const auto upgraded=PublishPreparedOutwardRaw<WindowRawMeasurement>(frame,identity,slot,negative,rich);
            Assert::IsTrue(upgraded->sourcePictureSupport->currentSupport.verified);
            Assert::IsTrue(PublishPreparedOutwardRaw<WindowRawMeasurement>(frame,identity,slot,negative,negative)==upgraded);
            // A native completion racing later must retain already verified support.
            auto incomplete=std::make_shared<WindowRawMeasurement>(*upgraded);
            incomplete->evidence=MissingSourcePictureEvidence(frame.source);
            std::shared_ptr<const WindowRawMeasurement> supportOnly=incomplete;
            std::atomic_store(&slot->ready,supportOnly);
            const auto combined=PublishPreparedOutwardRaw<WindowRawMeasurement>(frame,identity,slot,supportOnly,plain);
            Assert::IsTrue(SourcePictureMeasurementWorkComplete(combined->evidence));
            Assert::IsTrue(combined->sourcePictureSupport==detected.support);
        }

        TEST_METHOD(ColdOutwardPublicationCannotEraseARacingCurrentPhysicalObservation)
        {
            OwnedWindow window;
            const std::shared_ptr<const WindowRawMeasurement> originallyMissing;
            const auto raw=window.PublishRaw(0);
            auto upgraded=std::make_shared<WindowRawMeasurement>(*raw);
            Assert::IsTrue(upgraded->evidence.physicalEdges.evaluated);
            Assert::IsFalse(ActivePictureMeasurementCache::CompletePhysicalObservationIfMissing(
                upgraded->source,upgraded->evidence));
            std::shared_ptr<const WindowRawMeasurement> current=upgraded;
            std::atomic_store(&window.request.rawSlots[0]->ready,current);
            auto unfinished=std::make_shared<WindowRawMeasurement>(*raw);
            unfinished->evidence=MissingSourcePictureEvidence(unfinished->source);
            std::shared_ptr<const WindowRawMeasurement> late=unfinished;
            const auto selected=PublishPreparedOutwardRaw<WindowRawMeasurement>(window.request.frames[0],
                window.request.identities[0],window.request.rawSlots[0],originallyMissing,late);
            Assert::IsTrue(selected.get()==current.get(),
                L"A late cold outward scan must use the already completed current source, preserving its physical observation.");
            Assert::IsTrue(WindowRequest::RawWorker::Ready(window.request.rawSlots[0]).get()==current.get());
            auto incompatible=std::make_shared<WindowRawMeasurement>(*upgraded);
            ++incompatible->domainKey;++incompatible->identity.analysisDomainKey;
            std::shared_ptr<const WindowRawMeasurement> newer=incompatible;
            std::atomic_store(&window.request.rawSlots[0]->ready,newer);
            const auto privateResult=PublishPreparedOutwardRaw<WindowRawMeasurement>(window.request.frames[0],
                window.request.identities[0],window.request.rawSlots[0],originallyMissing,late);
            Assert::IsTrue(privateResult.get()==late.get());
            Assert::IsTrue(WindowRequest::RawWorker::Ready(window.request.rawSlots[0]).get()==newer.get(),
                L"An incompatible newer context must not be overwritten or adopted by an older request.");
        }

        TEST_METHOD(BoundedWorkerBindsOnlyValidCurrentSourceIdentity)
        {
            OwnedWindow window;auto source=window.request.frames[0].source;const auto identity=window.request.identities[0];
            const auto queued=DetectPreparedSourcePictureBounded(source,identity);
            Assert::IsTrue(queued.evidence.currentSourceIdentityAvailable);
            Assert::IsTrue(SameAnalysisFrameIdentity(identity,queued.evidence.currentSourceIdentity));
            source.generation=identity.transportGeneration;
            const auto live=DetectPreparedSourcePictureBounded(source,identity);
            Assert::IsTrue(live.evidence.currentSourceIdentityAvailable);
            Assert::IsTrue(SameAnalysisFrameIdentity(identity,live.evidence.currentSourceIdentity));
            source.generation=identity.sourceFormatGeneration+99;
            const auto foreign=DetectPreparedSourcePictureBounded(source,identity);
            Assert::IsFalse(foreign.evidence.currentSourceIdentityAvailable);
        }
        TEST_METHOD(OutwardRawCompletionWakesPreparationBeforeNormalWorkerFinishes)
        {
            OwnedWindow window;const auto complete=window.PublishRaw(0);
            auto partial=std::make_shared<WindowRawMeasurement>(*complete);
            partial->evidence=MissingSourcePictureEvidence(partial->source);
            std::shared_ptr<const WindowRawMeasurement> missing=partial;
            const auto slot=window.request.rawSlots[0];std::atomic_store(&slot->ready,missing);
            slot->completion=std::make_shared<AnalysisCompletionSignal>();
            const auto revision=slot->completion->Snapshot();
            Event started,release;WindowRequest::RawWorker worker;ReleaseOnExit unblock{release.handle};
            Assert::IsTrue(worker.Submit(slot,7,100,100,[&] {
                SetEvent(started.handle);WaitForSingleObject(release.handle,5000);return *missing;
            },[&](std::shared_ptr<const WindowRawMeasurement> result) {
                PublishPreparedOutwardRaw<WindowRawMeasurement>(window.request.frames[0],
                    window.request.identities[0],slot,missing,std::move(result));
            }));
            Assert::IsTrue(WaitForSingleObject(started.handle,2000)==WAIT_OBJECT_0);
            PublishPreparedOutwardRaw<WindowRawMeasurement>(window.request.frames[0],
                window.request.identities[0],slot,missing,complete);
            Assert::IsTrue(slot->completion->WaitUntil(revision,std::chrono::steady_clock::now()));
            Assert::IsTrue(slot->pending.load()); // Normal worker has not completed.
            Assert::IsTrue(WindowRequest::RawWorker::Ready(slot)==complete);
            const auto publishedRevision=slot->completion->Snapshot();
            PublishPreparedOutwardRaw<WindowRawMeasurement>(window.request.frames[0],
                window.request.identities[0],slot,missing,missing);
            Assert::IsTrue(slot->completion->Snapshot()==publishedRevision); // Rejected downgrade is not publication.
            SetEvent(release.handle);worker.Stop();
            Assert::IsTrue(WindowRequest::RawWorker::Ready(slot)==complete);
            Assert::IsTrue(slot->completion->Snapshot()>publishedRevision); // Normal completion also wakes.
        }
        TEST_METHOD(NormalAndOutwardRawPublishersNeverDowngradeEitherCompletionOrder)
        {
            for(bool normalCompletes:{false,true})for(bool foreignWinner:{false,true}) {
                OwnedWindow window;const auto complete=window.PublishRaw(0);
                auto partial=std::make_shared<WindowRawMeasurement>(*complete);
                partial->evidence=MissingSourcePictureEvidence(partial->source);
                std::shared_ptr<const WindowRawMeasurement> missing=partial;
                const auto slot=window.request.rawSlots[0];std::atomic_store(&slot->ready,missing);
                Event started,release;WindowRequest::RawWorker worker;ReleaseOnExit unblock{release.handle};
                Assert::IsTrue(worker.Submit(slot,7,100,100,[&] {
                    SetEvent(started.handle);WaitForSingleObject(release.handle,5000);
                    return normalCompletes?*complete:*missing;
                },[&](std::shared_ptr<const WindowRawMeasurement> result) {
                    PublishPreparedOutwardRaw<WindowRawMeasurement>(window.request.frames[0],
                        window.request.identities[0],slot,missing,std::move(result));
                }));
                Assert::IsTrue(WaitForSingleObject(started.handle,2000)==WAIT_OBJECT_0);
                const auto outward=normalCompletes?missing:complete;
                PublishPreparedOutwardRaw<WindowRawMeasurement>(window.request.frames[0],
                    window.request.identities[0],slot,missing,outward);
                std::shared_ptr<const WindowRawMeasurement> newer;
                if(foreignWinner) {
                    auto changed=std::make_shared<WindowRawMeasurement>(*complete);++changed->identity.analysisDomainKey;
                    newer=changed;std::atomic_store(&slot->ready,newer);
                }
                SetEvent(release.handle);worker.Stop();
                const auto final=WindowRequest::RawWorker::Ready(slot);
                if(foreignWinner)Assert::IsTrue(final==newer);
                else {
                    Assert::IsTrue(SourcePictureMeasurementWorkComplete(final->evidence));
                    Assert::AreEqual(68,final->evidence.physicalEdges.estimate.top);
                }
            }
        }
        TEST_METHOD(UnfinishedRawRetriesButCompletedNegativeIsReusable)
        {
            OwnedWindow window;for(size_t i=0;i<3;++i)window.PublishRaw(i);
            const auto complete=WindowRequest::RawWorker::Ready(window.request.rawSlots[0]);
            auto unfinished=std::make_shared<WindowRawMeasurement>(*complete);
            unfinished->evidence=MissingSourcePictureEvidence(unfinished->source);
            std::shared_ptr<const WindowRawMeasurement> missing=unfinished;
            std::atomic_store(&window.request.rawSlots[0]->ready,missing);
            const auto retried=EvaluatePreparedOutwardWindow(window.request);
            Assert::IsTrue(retried.proof.valid && retried.rawComputed==1 && retried.rawReused==2);
            auto rejected=std::make_shared<WindowRawMeasurement>(*complete);
            rejected->evidence=MissingSourcePictureEvidence(rejected->source,"complete-no-rectangle");
            rejected->evidence.physicalEdges.evaluated=true;
            std::shared_ptr<const WindowRawMeasurement> negative=rejected;
            std::atomic_store(&window.request.rawSlots[0]->ready,negative);
            const auto final=EvaluatePreparedOutwardWindow(window.request);
            Assert::IsFalse(final.proof.valid);
            Assert::IsTrue(final.rawComputed==0 && final.rawReused==3);
            Assert::IsTrue(WindowRequest::RawWorker::Ready(window.request.rawSlots[0])==negative);
        }
        TEST_METHOD(SourceCompletionRevisionInvalidatesOnlyDerivedPendingFacts)
        {
            OwnedWindow window;for(size_t i=0;i<3;++i)window.PublishRaw(i);
            const auto complete=WindowRequest::RawWorker::Ready(window.request.rawSlots[0]);
            auto pending=std::make_shared<WindowRawMeasurement>(*complete);
            pending->evidence.sourceCandidateVerificationComplete=false;pending->evidence.available=false;
            std::shared_ptr<const WindowRawMeasurement> earlier=pending;
            std::atomic_store(&window.request.rawSlots[0]->ready,earlier);
            for(size_t i=0;i<3;++i)window.PublishRetention(i);
            const auto oldKey=window.Key(0);
            Assert::IsFalse(SourcePictureMeasurementWorkComplete(earlier->evidence));
            auto changed=std::make_shared<WindowRawMeasurement>(*complete);++changed->evidence.physicalEdges.estimate.top;
            std::shared_ptr<const WindowRawMeasurement> wrong=changed;
            Assert::IsTrue(PublishPreparedOutwardRaw<WindowRawMeasurement>(window.request.frames[0],
                window.request.identities[0],window.request.rawSlots[0],earlier,wrong)==earlier);
            Assert::IsTrue(PublishPreparedOutwardRaw<WindowRawMeasurement>(window.request.frames[0],
                window.request.identities[0],window.request.rawSlots[0],earlier,complete)==complete);
            Assert::IsFalse(SamePreparedPictureEvidenceKey(oldKey,window.Key(0)));
            Assert::IsNull(window.caches[0].Lookup(window.Key(0)).get());
            const auto result=EvaluatePreparedOutwardWindow(window.request);
            Assert::IsTrue(result.rawReused==3 && result.rawComputed==0);
            Assert::IsTrue(result.retentionComputed==1 && result.retentionReused==2);
            Assert::IsTrue(result.proof.valid);
            auto negative=MissingSourcePictureEvidence(complete->source);negative.physicalEdges.evaluated=true;
            Assert::IsTrue(SourcePictureMeasurementWorkComplete(negative));
            auto provisional=complete->evidence;provisional.classification=ActivePictureClassification::PROVISIONAL;
            Assert::IsTrue(SourcePictureMeasurementWorkComplete(provisional));
        }
        TEST_METHOD(CompletedOutwardRawIncludesPhysicalObservationWithoutCompetingRescan)
        {
            OwnedWindow window;
            const auto proof=EvaluatePreparedOutwardWindow(window.request);
            Assert::IsTrue(proof.proof.valid);
            Assert::IsTrue(proof.rawComputed==3 && proof.retentionComputed==3);
            for(size_t i=0;i<3;++i) {
                const auto original=WindowRequest::RawWorker::Ready(window.request.rawSlots[i]);
                Assert::IsTrue(PreparedOutwardRawMeasurementCurrent(window.request.frames[i],
                    window.request.identities[i],original));
                Assert::IsTrue(original->evidence.physicalEdges.evaluated);
                Assert::IsTrue(original->evidence.physicalEdges.estimateAvailable);
                Assert::AreEqual(original->source.generation,original->evidence.physicalEdges.sourceGeneration);
                auto unchanged=*original;
                Assert::IsFalse(ActivePictureMeasurementCache::CompletePhysicalObservationIfMissing(
                    unchanged.source,unchanged.evidence),
                    L"The common worker already supplies geometry; a foreground observer must not reinterpret it.");
                Assert::AreEqual(int(original->evidence.classification),int(unchanged.evidence.classification));
                Assert::AreEqual(original->evidence.trustedBounds.top,unchanged.evidence.trustedBounds.top);
                Assert::AreEqual(original->evidence.trustedBounds.bottom,unchanged.evidence.trustedBounds.bottom);
            }
            const auto reused=EvaluatePreparedOutwardWindow(window.request);
            Assert::IsTrue(reused.proof.valid && reused.rawComputed==0 && reused.rawReused==3);
            Assert::IsTrue(reused.retentionComputed==0 && reused.retentionReused==3,
                L"Reading completed geometry must preserve the existing outward certificates.");
            const auto raw=WindowRequest::RawWorker::Ready(window.request.rawSlots[0]);
            auto missing=*raw;missing.evidence.physicalEdges={};
            AnalysisEvaluationBudget expired([](void*){return true;},nullptr);
            Assert::IsFalse(ActivePictureMeasurementCache::CompletePhysicalObservationIfMissing(
                missing.source,missing.evidence,&expired));
            Assert::IsFalse(missing.evidence.physicalEdges.evaluated,
                L"Missing worker facts must remain pending, never become a competing observer's Unknown.");
            Assert::IsTrue(WindowRequest::RawWorker::Ready(window.request.rawSlots[0])->evidence.physicalEdges.estimateAvailable);
            Assert::IsTrue(reused.proof.valid);
        }

        TEST_METHOD(MissingRawAndRetentionFinishCompleteWindowWithoutAnotherRenderPass)
        {
            OwnedWindow window;
            const auto result=EvaluatePreparedOutwardWindow(window.request);
            Assert::IsTrue(result.proof.valid);
            Assert::IsFalse(result.noChange);
            Assert::IsTrue(result.rawComputed==3 && result.rawReused==0);
            Assert::IsTrue(result.retentionComputed==3 && result.retentionReused==0);
            Assert::IsTrue(result.proof.decision.effectiveIdentity.acceptedSequence==100);
            for (size_t i=0;i<3;++i)
            {
                Assert::IsTrue(PreparedOutwardRawMeasurementCurrent(window.request.frames[i],
                    window.request.identities[i],WindowRequest::RawWorker::Ready(window.request.rawSlots[i])));
                Assert::IsNotNull(window.caches[i].Lookup(window.Key(i)).get());
            }
            const auto reused=EvaluatePreparedOutwardWindow(window.request);
            Assert::IsTrue(reused.proof.valid);
            Assert::IsTrue(reused.rawComputed==0 && reused.rawReused==3);
            Assert::IsTrue(reused.retentionComputed==0 && reused.retentionReused==3);
        }

        TEST_METHOD(RawCompletionAfterPrimeCanReachNextSourceAtZeroForegroundAllowance)
        {
            OwnedWindow window;
            Event started,release; WindowWorker worker; ReleaseOnExit unblock{release.handle};
            auto output=std::make_shared<WindowWorker::Slot>();
            // Snapshot/prime occurred with no raw results. The full outward job
            // is already queued; completing raw needs no second render pass.
            Assert::IsTrue(worker.Submit(output,7,100,100,[&] {
                SetEvent(started.handle); WaitForSingleObject(release.handle,5000);
                return EvaluatePreparedOutwardWindow(window.request);
            }));
            Assert::IsTrue(WaitForSingleObject(started.handle,2000)==WAIT_OBJECT_0);
            Assert::IsNull(WindowWorker::Ready(output).get());
            for (size_t i=0;i<3;++i)window.PublishRaw(i);
            SetEvent(release.handle);worker.Stop();
            const auto result=WindowWorker::Ready(output);
            Assert::IsNotNull(result.get()); Assert::IsTrue(result->proof.valid);
            Assert::IsTrue(result->rawReused==3 && result->rawComputed==0);
            Assert::IsTrue(result->retentionComputed==3);
            PresentationAnalysisBudget budget;
            budget.Observe(7,1000000,1001000,1000000,60,true,20);
            Assert::IsTrue(budget.Begin(7,1001000,1000000,false,false).deadlineQpc==0);
            const auto raw=WindowRequest::RawWorker::Ready(window.request.rawSlots[0]);
            AlphaSourceCrop::TransitionAdmissionInput current;
            current.evidence=raw->evidence;current.outwardCandidate=raw->evidence.trustedBounds;
            current.trustedGeometry=current.presentationBeforeObservation=window.request.context.base;
            current.trustedGeometryAvailable=current.compatiblePresentation=true;
            current.trustedGeneration=current.sourceGeneration=current.presentationEvidenceGeneration=7;
            current.sourceSequence=100;current.framesPerSecond=24;
            current.retention=window.caches[0].Lookup(window.Key(0))->retention;
            Assert::IsTrue(AlphaSourceCrop::ValidateBufferedPictureExpansion(result->proof,
                window.request.identities[0],current,true));
            for (size_t i=0;i<3;++i)
                Assert::IsNotNull(window.caches[i].Lookup(window.Key(i)).get());
        }

        TEST_METHOD(ReadyRawWithMissingRetentionComputesOnlyMissingCompleteComponents)
        {
            OwnedWindow window;
            for(size_t i=0;i<3;++i)window.PublishRaw(i);
            window.PublishRetention(0);window.PublishRetention(2);
            const auto result=EvaluatePreparedOutwardWindow(window.request);
            Assert::IsTrue(result.proof.valid);
            Assert::IsTrue(result.rawReused==3 && result.rawComputed==0);
            Assert::IsTrue(result.retentionReused==2 && result.retentionComputed==1);
        }

        TEST_METHOD(StaleRawIdentityAndWrongRetentionKeyAreRecomputedWithoutAdoption)
        {
            OwnedWindow window;
            for(size_t i=0;i<3;++i) {window.PublishRaw(i);window.PublishRetention(i);}
            auto wrong=std::make_shared<WindowRawMeasurement>(
                *WindowRequest::RawWorker::Ready(window.request.rawSlots[1]));
            ++wrong->identity.analysisDomainKey;
            std::shared_ptr<const WindowRawMeasurement> wrongRaw=wrong;
            std::atomic_store(&window.request.rawSlots[1]->ready,wrongRaw);
            auto stale=std::make_shared<PreparedPictureEvidence>(
                *PreparedPictureEvidenceWorker::Ready(window.request.retentionSlots[2]));
            ++stale->key.policyGeneration;
            std::shared_ptr<const PreparedPictureEvidence> wrongRetention=stale;
            std::atomic_store(&window.request.retentionSlots[2]->ready,wrongRetention);
            const auto result=EvaluatePreparedOutwardWindow(window.request);
            Assert::IsTrue(result.proof.valid);
            Assert::IsTrue(result.rawComputed==1 && result.rawReused==2);
            Assert::IsTrue(result.retentionComputed==2 && result.retentionReused==1);
        }

        TEST_METHOD(IdenticalLateRawCompletionDoesNotInvalidateStableRequestSlotKey)
        {
            OwnedWindow window;
            Assert::IsTrue(EvaluatePreparedOutwardWindow(window.request).proof.valid);
            const auto earlier=WindowRequest::RawWorker::Ready(window.request.rawSlots[0]);
            std::shared_ptr<const WindowRawMeasurement> replacement=std::make_shared<WindowRawMeasurement>(*earlier);
            const auto selected=PublishPreparedOutwardRaw<WindowRawMeasurement>(window.request.frames[0],
                window.request.identities[0],window.request.rawSlots[0],earlier,replacement);
            Assert::IsTrue(selected==earlier);
            Assert::IsTrue(WindowRequest::RawWorker::Ready(window.request.rawSlots[0])==earlier);
            const auto result=EvaluatePreparedOutwardWindow(window.request);
            Assert::IsTrue(result.proof.valid);
            Assert::IsTrue(result.rawReused==3 && result.retentionReused==3);
            Assert::IsTrue(result.retentionComputed==0);
        }

        TEST_METHOD(OldContextCompletionCannotPublishIntoReplacementRetentionSlots)
        {
            OwnedWindow window;
            auto newer=window.request;
            newer.context.base.top+=4;newer.context.base.bottom-=4;
            newer.context.base.aspectRatio=3840.0/(newer.context.base.bottom-newer.context.base.top);
            for(size_t i=0;i<3;++i)
            {
                newer.retentionKeys[i].base=newer.context.base;
                newer.retentionSlots[i]=window.caches[i].Request(newer.retentionKeys[i]);
                Assert::IsTrue(newer.retentionSlots[i]!=window.request.retentionSlots[i]);
            }
            const auto old=EvaluatePreparedOutwardWindow(window.request);
            Assert::IsTrue(old.proof.valid);
            for(size_t i=0;i<3;++i)
                Assert::IsNull(window.caches[i].Lookup(newer.retentionKeys[i]).get());
            Assert::IsFalse(AlphaSourceCrop::AsyncOutwardContextCurrent(old.context,
                newer.identities[0],newer.context.base,{},AlphaSourceCrop::VerticalBarPresentationAction::NONE,
                0,newer.context.policyGeneration,newer.context.continuityGeneration));
        }

        TEST_METHOD(CompletedNoChangeAndRejectedEvidenceRemainDistinctFromPending)
        {
            OwnedWindow unchanged(true);
            auto output=std::make_shared<WindowWorker::Slot>();output->pending.store(true);
            Assert::IsNull(WindowWorker::Ready(output).get());
            const auto result=EvaluatePreparedOutwardWindow(unchanged.request);
            Assert::IsFalse(result.proof.valid);Assert::IsTrue(result.noChange);
            std::shared_ptr<const PreparedOutwardWindowResult> completed=
                std::make_shared<PreparedOutwardWindowResult>(result);
            std::atomic_store(&output->ready,completed);
            Assert::IsNotNull(WindowWorker::Ready(output).get());
            OwnedWindow rejected;
            for(size_t i=0;i<3;++i) {rejected.PublishRaw(i);rejected.PublishRetention(i);}
            auto negative=std::make_shared<PreparedPictureEvidence>();negative->key=rejected.Key(1);
            std::shared_ptr<const PreparedPictureEvidence> completeNegative=negative;
            std::atomic_store(&rejected.request.retentionSlots[1]->ready,completeNegative);
            const auto invalid=EvaluatePreparedOutwardWindow(rejected.request);
            Assert::IsFalse(invalid.proof.valid);Assert::IsFalse(invalid.noChange);
            Assert::IsTrue(invalid.retentionReused==3 && invalid.retentionComputed==0);
        }

        TEST_METHOD(InconsistentContextFailsBeforePublishingAnyComponents)
        {
            OwnedWindow window;
            ++window.request.context.contributors[2].sourceFormatGeneration;
            Assert::ExpectException<std::invalid_argument>([&] {
                (void)EvaluatePreparedOutwardWindow(window.request);
            });
            for(size_t i=0;i<3;++i)
            {
                Assert::IsNull(WindowRequest::RawWorker::Ready(window.request.rawSlots[i]).get());
                Assert::IsNull(PreparedPictureEvidenceWorker::Ready(window.request.retentionSlots[i]).get());
            }
        }
    };
}