#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/PreparedOutwardWindow.h>
#include <vprenderer/PresentationAnalysisBudget.h>
#include <vector>

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
                raw->evidence=ExtractActivePictureEvidence(raw->source);
                raw->darkness=EvaluateActivePictureArDarkness(raw->source,raw->evidence);
                std::shared_ptr<const WindowRawMeasurement> complete=raw;
                std::atomic_store(&request.rawSlots[i]->ready,complete);
                return complete;
            }
            void PublishRetention(size_t i)
            {
                const auto raw=WindowRequest::RawWorker::Ready(request.rawSlots[i]);
                std::shared_ptr<const PreparedPictureEvidence> complete=
                    std::make_shared<PreparedPictureEvidence>(EvaluatePreparedPictureEvidence(
                        request.retentionKeys[i],raw->evidence,raw->darkness));
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
                Assert::IsNotNull(window.caches[i].Lookup(window.request.retentionKeys[i]).get());
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
            current.retention=window.caches[0].Lookup(window.request.retentionKeys[0])->retention;
            Assert::IsTrue(AlphaSourceCrop::ValidateBufferedPictureExpansion(result->proof,
                window.request.identities[0],current,true));
            for (size_t i=0;i<3;++i)
                Assert::IsNotNull(window.caches[i].Lookup(window.request.retentionKeys[i]).get());
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
            Assert::IsTrue(result.retentionComputed==1 && result.retentionReused==2);
        }

        TEST_METHOD(IdenticalLateRawCompletionDoesNotInvalidateStableRequestSlotKey)
        {
            OwnedWindow window;
            Assert::IsTrue(EvaluatePreparedOutwardWindow(window.request).proof.valid);
            const auto earlier=WindowRequest::RawWorker::Ready(window.request.rawSlots[0]);
            const auto replacement=window.PublishRaw(0);
            Assert::IsTrue(earlier!=replacement);
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
            auto negative=std::make_shared<PreparedPictureEvidence>();negative->key=rejected.request.retentionKeys[1];
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