#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/QueueNativeContinuity.h>
#include <vector>
#include <thread>
#include <cstring>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{
TEST_CLASS(QueueNativeContinuityTests)
{
    static ActivePictureBounds Scope()
    {
        return {0,276,3840,1884,3840,2160,3840.0/1608.0,
            ActivePictureBounds::BarAxes::TOP_BOTTOM};
    }
    static ActivePictureBounds Tall()
    {
        return {0,68,3840,2092,3840,2160,3840.0/2024.0,
            ActivePictureBounds::BarAxes::TOP_BOTTOM};
    }
    static ActivePictureObservation Observation(const ActivePictureBounds& bounds,uint64_t frame)
    {
        ActivePictureObservation result;result.bounds=bounds;result.frameNumber=frame;
        result.available=true;result.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
        result.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;result.framesPerSecond=23.976;
        return result;
    }
    static void Establish(ActivePictureTransitionModel& model)
    {
        for(uint64_t frame=10;frame<=13;++frame)
        {
            const auto result=model.Observe(Observation(Scope(),frame));
            Assert::IsTrue(frame==13?result.publish:!result.publish);
        }
    }
    struct Fixture
    {
        std::vector<uint16_t> pixels;
        ActivePictureTransitionModel model;
        QueueNativeContinuityInput input;
        explicit Fixture(bool p210=false)
        {
            constexpr int width=3840,height=2160;
            pixels.assign(size_t(width)*(height+(p210?height:height/2)),uint16_t(512<<6));
            for(int y=0;y<height;++y)for(int x=0;x<width;++x)
                pixels[size_t(y)*width+x]=uint16_t((y>=276 && y<1884?300:64)<<6);
            AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels.data());
            source.dataBytes=pixels.size()*sizeof(uint16_t);source.width=width;source.height=height;
            source.rowBytes=source.chromaRowBytes=width*2;source.generation=8;
            source.format=p210?AnalysisLumaFormat::P210:AnalysisLumaFormat::P010;
            Establish(model);
            input.resetKind=QueueNativeResetKind::QUEUE_ONLY;
            input.previousModel=&model;input.previousNativeBounds=Scope();
            input.previousIdentity={7,99,100,41600000,11,0,7};
            input.previousIdentity.analysisDomainKey=12;
            input.currentIdentity={8,1,101,42017083,11,0,8};
            input.currentIdentity.analysisDomainKey=12;
            input.nativeEvidenceIdentity=input.retentionIdentity=input.currentIdentity;
            input.currentSourceValid=source.IsValid();input.width=width;input.height=height;
            input.nativeEvidence=ExtractActivePictureEvidence(source);
            input.retention=EvaluateActivePicturePresentationRetention(source,Scope());
            input.darknessEvaluated=EvaluateActivePictureArDarkness(source,input.nativeEvidence).global.evaluated;
        }
    };
public:
    TEST_METHOD(ExactFreshP010AndP210NativeBarsSurviveOnlyTheQueueGenerationChange)
    {
        for(bool p210:{false,true})
        {
            Fixture fixture(p210);const auto result=EvaluateQueueNativeContinuity(fixture.input);
            const auto message=std::wstring(L"P210=")+std::to_wstring(p210)+L" reason="+
                std::wstring(result.reason,result.reason+std::strlen(result.reason));
            Assert::IsTrue(result.retained,message.c_str());
            Assert::AreEqual(uint64_t(8),result.sourceGeneration);
            Assert::AreEqual(276,result.nativeBounds.top);Assert::AreEqual(1884,result.nativeBounds.bottom);
            auto rebound=result.model;
            const auto current=rebound.Observe(Observation(Scope(),1));
            Assert::IsTrue(current.stable);Assert::IsFalse(current.publish);
            Assert::AreEqual(0u,unsigned(current.matchingCandidates));
            Assert::AreEqual(int(ActivePictureAuthorityOrigin::NATIVE),int(current.stableAuthorityOrigin));
        }
    }
    TEST_METHOD(ChangedContextOrNonCurrentEvidenceCannotRebindNativeGeometry)
    {
        Fixture fixture;
        for(int variant=0;variant<25;++variant)
        {
            auto input=fixture.input;
            if(variant==0)input.resetKind=QueueNativeResetKind::FULL;
            if(variant==1)input.resetKind=QueueNativeResetKind::NONE;
            if(variant==2)input.currentSourceValid=false;
            if(variant==3)input.sourceDiscontinuity=true;
            if(variant==4)input.previousIdentity.transportGeneration=0;
            if(variant==5)input.currentIdentity.transportGeneration=7;
            if(variant==6)input.currentIdentity.sourceFormatGeneration=13;
            if(variant==7)input.previousIdentity.sourceFormatGeneration=0;
            if(variant==8)input.currentIdentity.analysisDomainKey=13;
            if(variant==9)input.previousIdentity.analysisDomainKey=0;
            if(variant==10)++input.currentIdentity.viewportGeneration;
            if(variant==11)input.currentIdentity.rendererGeneration=7;
            if(variant==12)input.previousIdentity.rendererGeneration=6;
            if(variant==13)input.currentIdentity.sourceFrameNumber=100;
            if(variant==14)input.currentIdentity.captureTimestamp=41600000;
            if(variant==15)input.currentIdentity.acceptedSequence=0;
            if(variant==16)input.width=1920;
            if(variant==17)input.height=1080;
            if(variant==18)++input.nativeEvidenceIdentity.acceptedSequence;
            if(variant==19)++input.retentionIdentity.captureTimestamp;
            if(variant==20)input.previousModel=nullptr;
            if(variant==21)input.currentIdentity.transportGeneration=0;
            if(variant==22)input.previousIdentity.sourceFrameNumber=0;
            if(variant==23)input.previousIdentity.captureTimestamp=0;
            if(variant==24)input.previousIdentity.acceptedSequence=0;
            const auto result=EvaluateQueueNativeContinuity(input);
            Assert::IsFalse(result.retained,(L"context variant="+std::to_wstring(variant)).c_str());
        }
    }
    TEST_METHOD(UnknownDarkMaskedExpandedOrChangedNativePixelsCannotBecomeQueueContinuity)
    {
        Fixture fixture;
        for(int variant=0;variant<21;++variant)
        {
            auto input=fixture.input;
            if(variant==0)input.nativeEvidence.available=false;
            if(variant==1)input.nativeEvidence.classification=ActivePictureClassification::PROVISIONAL;
            if(variant==2)input.nativeEvidence.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            if(variant==3)input.nativeEvidence.authorityOrigin=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN;
            if(variant==4)input.nativeEvidence.sparseTransitionProof.available=true;
            if(variant==5)input.nativeEvidence.rememberedEdgeReturnProof.available=true;
            if(variant==6)input.nativeEvidence.axisEvidence.verticalCropGlyphExclusions=true;
            if(variant==7)input.nativeEvidence.trustedBounds.top+=2;
            if(variant==8)input.nativeEvidence.proposedBounds.bottom+=2;
            if(variant==9)input.darknessEvaluated=false;
            if(variant==10){input.retention.globalNearBlack=true;input.retention.distributedDarkPicture=false;}
            if(variant==11)input.retention.analysisValid=false;
            if(variant==12)input.retention.presentationValid=false;
            if(variant==13){input.retention.currentlyPixelSafe=false;input.retention.samplingEquivalent=true;}
            if(variant==14)input.retention.excludedBandsPixelSafe=false;
            if(variant==15)input.retention.outwardVisibleBoundsAvailable=true;
            if(variant==16)input.retention.activePicture.trustedBounds.bottom-=2;
            if(variant==17)input.retention.activePicture.axisEvidence.verticalCropGlyphExclusions=true;
            if(variant==18)input.retention.activePicture.classification=ActivePictureClassification::UNAVAILABLE;
            if(variant==19)input.nativeEvidence.axisEvidence.horizontal.scanComplete=false;
            if(variant==20)input.nativeEvidence.axisEvidence.vertical.scanComplete=false;
            const auto result=EvaluateQueueNativeContinuity(input);
            Assert::IsFalse(result.retained,(L"pixel variant="+std::to_wstring(variant)).c_str());
        }
    }
    TEST_METHOD(NativeFormatFactsSurviveButPendingVotesAndSourceBoundCertificatesDoNot)
    {
        Fixture fixture;
        Assert::IsFalse(fixture.model.Observe(Observation(Tall(),14)).publish);
        Assert::IsTrue(fixture.model.Observe(Observation(Tall(),15)).publish);
        Assert::IsFalse(fixture.model.Observe(Observation(Scope(),16)).publish);
        Assert::IsTrue(fixture.model.Observe(Observation(Scope(),17)).publish);
        Assert::IsFalse(fixture.model.Observe(Observation(Tall(),18)).publish);
        uint64_t sequence=19;
        for(uint64_t scene=1;scene<=2;++scene)for(int sample=0;sample<3;++sample,++sequence)
        {
            RememberedEdgeReturnContext context;context.enabled=true;context.sourceGeneration=7;
            context.rendererGeneration=7;context.sourceFormatGeneration=11;context.policyGeneration=1;
            context.sceneId=scene;context.sourceSequence=sequence;context.timestampMs=1000+sequence*42;
            fixture.model.SetRememberedEdgeReturnContext(context);
            const auto independentlyMeasured=MakeActivePictureObservation(fixture.input.nativeEvidence,sequence,23.976);
            Assert::IsTrue(fixture.model.RecordIndependentNativeGeometry(independentlyMeasured));
        }
        Assert::IsTrue(fixture.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries>0);
        const auto result=EvaluateQueueNativeContinuity(fixture.input);Assert::IsTrue(result.retained);
        auto rebound=result.model;
        Assert::AreEqual(0u,rebound.GetRememberedEdgeReturnHistoryStatus().entries);
        Assert::AreEqual(0u,rebound.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
        ActivePictureBounds remembered;
        Assert::IsTrue(rebound.FindRecentTrustedBarGeometry(Tall(),remembered));
        Assert::AreEqual(Tall().top,remembered.top);
        Assert::IsFalse(rebound.Observe(Observation(Tall(),1)).publish,
            L"The pre-reset one-sample candidate cannot complete a post-reset transition.");
        Assert::IsTrue(rebound.Observe(Observation(Tall(),2)).publish);
    }
    TEST_METHOD(ModelResetCannotPreserveFullRasterInferredOrWrongNativeContracts)
    {
        ActivePictureTransitionModel full;
        auto raw=Observation({0,0,3840,2160,3840,2160,16.0/9.0},1);
        raw.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
        Assert::IsTrue(full.Observe(raw).publish);
        Assert::IsFalse(full.ResetForNativeQueueContinuity(raw.bounds));
        ActivePictureTransitionModel inferred;
        for(uint64_t sequence=1;sequence<=4;++sequence)
        {
            auto observation=Observation(Scope(),sequence);
            observation.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
            const auto result=inferred.Observe(observation);
            if(sequence==4)
            {
                Assert::IsTrue(result.publish && result.stable);
                Assert::AreEqual(int(ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT),int(result.authorityOrigin));
            }
        }
        Assert::IsFalse(inferred.ResetForNativeQueueContinuity(Scope()));
        ActivePictureTransitionModel native;Establish(native);
        Assert::IsFalse(native.Observe(Observation(Tall(),14)).publish);
        auto wrong=Scope();wrong.top+=2;
        Assert::IsFalse(native.ResetForNativeQueueContinuity(wrong));
        Assert::IsTrue(native.Observe(Observation(Tall(),15)).publish,
            L"A rejected reset guard must not mutate the existing model or its ordinary pending evidence.");
    }
    TEST_METHOD(OnlyAnAlreadyDequeuedOldPacketDefersQueueOnlyInvalidation)
    {
        Assert::IsTrue(DeferQueueNativeResetUntilNewPacket(QueueNativeResetKind::QUEUE_ONLY,7,7,false));
        Assert::IsTrue(DeferQueueNativeResetUntilNewPacket(QueueNativeResetKind::QUEUE_ONLY,7,6,false));
        Assert::IsFalse(DeferQueueNativeResetUntilNewPacket(QueueNativeResetKind::QUEUE_ONLY,7,8,false));
        Assert::IsFalse(DeferQueueNativeResetUntilNewPacket(QueueNativeResetKind::QUEUE_ONLY,7,7,true));
        Assert::IsFalse(DeferQueueNativeResetUntilNewPacket(QueueNativeResetKind::FULL,7,7,false));
        Assert::IsFalse(DeferQueueNativeResetUntilNewPacket(QueueNativeResetKind::NONE,7,7,false));
        Assert::IsFalse(DeferQueueNativeResetUntilNewPacket(QueueNativeResetKind::QUEUE_ONLY,0,0,false));
    }
    TEST_METHOD(FullResetDominatesPendingQueueOnlyRequestsInEitherOrder)
    {
        for(bool fullFirst:{false,true})
        {
            QueueNativeResetEpoch reset;uint64_t consumed=0;
            reset.Request(fullFirst?QueueNativeResetKind::FULL:QueueNativeResetKind::QUEUE_ONLY);
            reset.Request(fullFirst?QueueNativeResetKind::QUEUE_ONLY:QueueNativeResetKind::FULL);
            reset.Request(QueueNativeResetKind::QUEUE_ONLY);
            QueueNativeResetKind kind=QueueNativeResetKind::NONE;
            Assert::IsTrue(reset.Consume(consumed,&kind));
            Assert::AreEqual(int(QueueNativeResetKind::FULL),int(kind));
            Assert::AreEqual(uint64_t(3),consumed);Assert::IsFalse(reset.Consume(consumed,&kind));
            reset.Request(QueueNativeResetKind::QUEUE_ONLY);
            Assert::IsTrue(reset.Consume(consumed,&kind));
            Assert::AreEqual(int(QueueNativeResetKind::QUEUE_ONLY),int(kind));
        }
    }
    TEST_METHOD(ConcurrentQueueRequestsCannotDowngradeAnUnconsumedFullReset)
    {
        QueueNativeResetEpoch reset;uint64_t consumed=0;
        std::thread queueA([&]{for(int i=0;i<250;++i)reset.Request(QueueNativeResetKind::QUEUE_ONLY);});
        std::thread full([&]{reset.Request(QueueNativeResetKind::FULL);});
        std::thread queueB([&]{for(int i=0;i<250;++i)reset.Request(QueueNativeResetKind::QUEUE_ONLY);});
        queueA.join();full.join();queueB.join();
        QueueNativeResetKind kind=QueueNativeResetKind::NONE;
        Assert::IsTrue(reset.Consume(consumed,&kind));
        Assert::AreEqual(int(QueueNativeResetKind::FULL),int(kind));
        Assert::AreEqual(uint64_t(501),consumed);
    }
    TEST_METHOD(EmptyResetRequestsDoNotCreateAnEpochAndDefaultRequestsRemainFull)
    {
        QueueNativeResetEpoch reset;uint64_t consumed=0;
        reset.Request(QueueNativeResetKind::NONE);Assert::IsFalse(reset.Consume(consumed));
        reset.Request();QueueNativeResetKind kind=QueueNativeResetKind::NONE;
        Assert::IsTrue(reset.Consume(consumed,&kind));
        Assert::AreEqual(int(QueueNativeResetKind::FULL),int(kind));
        Assert::AreEqual(uint64_t(1),consumed);
    }
};
}
