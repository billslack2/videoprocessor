#include "pch.h"
#include "CppUnitTest.h"
#include <ActivePictureEvidence.h>
#include <SparseBoundaryCropExperiment.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <vprenderer/BufferedPictureExpansion.h>
#include <algorithm>
#include <array>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
namespace
{
struct SparsePixels
{
    int width, height;
    bool p210;
    size_t pitch;
    std::vector<uint16_t> pixels;
    SparsePixels(int w=384,int h=216,bool nativePlanar=true)
        : width(w),height(h),p210(nativePlanar),pitch(size_t(w)+8),
          pixels(pitch*(h+(nativePlanar?h:h/2)),uint16_t(512<<6))
    { Rectangle(0,0,width,height,64); }
    void Rectangle(int left,int top,int right,int bottom,int y,int u=512,int v=512)
    {
        for(int row=top;row<bottom;++row)
            std::fill(pixels.begin()+row*pitch+left,pixels.begin()+row*pitch+right,uint16_t(y<<6));
        for(int row=p210?top:top/2;row<(p210?bottom:(bottom+1)/2);++row)
            for(int x=left&~1;x<right;x+=2)
            {pixels[pitch*height+row*pitch+x]=uint16_t(u<<6);pixels[pitch*height+row*pitch+x+1]=uint16_t(v<<6);}
    }
    void Picture(int phase=0,bool brightInterior=true,int top=28,int bottom=188)
    {
        Rectangle(0,0,width,height,64);
        const int scaleX=width/384,scaleY=height/216;
        const int x=(18+(phase%4)*36)*scaleX;
        Rectangle(x,top*scaleY,x+24*scaleX,bottom*scaleY,144,485,528);
        Rectangle(216*scaleX,top*scaleY,360*scaleX,(top+8)*scaleY,144,485,528);
        if(brightInterior)Rectangle(0,72*scaleY,width,144*scaleY,300);
    }
    AnalysisLumaSource Source() const
    {return {reinterpret_cast<const uint8_t*>(pixels.data()),pixels.size()*2,width,height,pitch*2,pitch*2,
        p210?AnalysisLumaFormat::P210:AnalysisLumaFormat::P010,VideoFrameEncoding::V210,ColorSpace::REC_709,7};}
};
struct PackedSparsePixels
{
    int width,height;size_t pitch;std::vector<uint8_t> pixels;
    explicit PackedSparsePixels(const AnalysisLumaSource& source):width(source.width),height(source.height),
        pitch(size_t(width/6)*16+16),pixels(pitch*height)
    {
        for(int y=0;y<height;++y)for(int x=0;x<width;x+=6)
        {
            AnalysisLumaSample p[6];for(int i=0;i<6;++i)Assert::IsTrue(source.Sample(x+i,y,p[i]));
            const uint32_t words[]={uint32_t(p[0].chromaU)|uint32_t(p[0].luma)<<10|uint32_t(p[0].chromaV)<<20,
                uint32_t(p[1].luma)|uint32_t(p[2].chromaU)<<10|uint32_t(p[2].luma)<<20,
                uint32_t(p[2].chromaV)|uint32_t(p[3].luma)<<10|uint32_t(p[4].chromaU)<<20,
                uint32_t(p[4].luma)|uint32_t(p[4].chromaV)<<10|uint32_t(p[5].luma)<<20};
            for(int word=0;word<4;++word)for(int b=0;b<4;++b)
                pixels[y*pitch+(x/6)*16+word*4+b]=uint8_t(words[word]>>(8*b));
        }
    }
    AnalysisLumaSource Source()const
    {return {pixels.data(),pixels.size(),width,height,pitch,0,AnalysisLumaFormat::NativeYuv422,
        VideoFrameEncoding::V210,ColorSpace::REC_709,7};}
};
SparseBoundaryCropExperimentInput EligibleInput()
{
    SparseBoundaryCropExperimentInput input;input.enabled=input.startupEligible=true;
    input.context.sourceGeneration=7;input.context.rendererGeneration=11;
    input.context.viewportGeneration=13;input.context.sourceFormatGeneration=17;
    input.context.policyGeneration=19;input.context.continuityGeneration=23;input.context.sceneGeneration=29;
    input.context.acceptedSequence=1;input.context.timestampMs=100;
    return input;
}
void NextSparse(SparseBoundaryCropExperimentInput& input,uint64_t milliseconds=200)
{++input.context.acceptedSequence;input.context.timestampMs+=milliseconds;}
void SameBounds(const ActivePictureBounds& expected,const ActivePictureBounds& actual)
{
    Assert::AreEqual(expected.left,actual.left);Assert::AreEqual(expected.top,actual.top);
    Assert::AreEqual(expected.right,actual.right);Assert::AreEqual(expected.bottom,actual.bottom);
    Assert::AreEqual(expected.rasterWidth,actual.rasterWidth);Assert::AreEqual(expected.rasterHeight,actual.rasterHeight);
    Assert::AreEqual(int(expected.trustedBarAxes),int(actual.trustedBarAxes));
}
void SameRaw(const ActivePictureEvidence& expected,const ActivePictureEvidence& actual)
{
    Assert::AreEqual(expected.available,actual.available);Assert::AreEqual(int(expected.classification),int(actual.classification));
    SameBounds(expected.proposedBounds,actual.proposedBounds);SameBounds(expected.trustedBounds,actual.trustedBounds);
    Assert::IsTrue(expected.axisEvidence==actual.axisEvidence);Assert::AreEqual(expected.reason,actual.reason);
    Assert::AreEqual(expected.lumaSamples,actual.lumaSamples);Assert::AreEqual(expected.chromaSamples,actual.chromaSamples);
    Assert::AreEqual(int(expected.authorityOrigin),int(actual.authorityOrigin));
}
SparseBoundaryCropExperimentResult FeedSparse(SparseBoundaryCropExperiment& experiment,SparsePixels& pixels,
    SparseBoundaryCropExperimentInput& input,int phase)
{
    pixels.Picture(phase);const auto source=pixels.Source();
    return experiment.Observe(source,ExtractActivePictureEvidence(source),input);
}
SparseBoundaryCropExperimentResult MatureSparse(SparseBoundaryCropExperiment& experiment,SparsePixels& pixels,
    SparseBoundaryCropExperimentInput& input)
{
    SparseBoundaryCropExperimentResult result;
    for(int phase=0;phase<4;++phase)
    {if(phase)NextSparse(input);result=FeedSparse(experiment,pixels,input,phase);}
    Logger::WriteMessage(result.reason);
    Assert::IsTrue(result.candidateAvailable,L"Actual sparse source must mature an opt-in candidate, not just metadata.");
    Assert::IsTrue(result.current&&result.referenceAvailable);
    return result;
}
AlphaSourceCrop::Input CropInput(const ActivePictureBounds& bounds,uint64_t sequence=10)
{
    AlphaSourceCrop::Input input;input.automaticCropEnabled=input.sharedGeometryAvailable=true;
    input.latestObservationSupportsCrop=true;input.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
    input.classification=ActivePictureClassification::BAR_CROP_TRUSTED;input.geometry=bounds;
    input.geometrySourceGeneration=input.frameSourceGeneration=7;input.frameSourceSequence=sequence;
    input.rasterWidth=bounds.rasterWidth;input.rasterHeight=bounds.rasterHeight;return input;
}
ActivePictureTransitionDecision PublishSparse(ActivePictureTransitionModel& model,
    SparseBoundaryCropExperiment& experiment,SparsePixels& pixels,SparseBoundaryCropExperimentInput& input)
{
    const auto first=MatureSparse(experiment,pixels,input);
    ActivePictureTransitionDecision decision;
    for(int confirmation=0;confirmation<ActivePictureTransitionModel::INITIAL_CONFIRMATIONS;++confirmation)
    {
        SparseBoundaryCropExperimentResult result=first;
        if(confirmation){NextSparse(input,40);result=FeedSparse(experiment,pixels,input,3);}
        Assert::IsTrue(result.candidateAvailable);
        const auto darkness=EvaluateActivePictureGlobalNearBlack(pixels.Source());
        Assert::IsTrue(darkness.evaluated&&!darkness.nearBlack);
        const auto constrained=ConstrainNearBlackCropAcquisition(result.evidence,darkness.nearBlack);
        decision=model.Observe(MakeActivePictureObservation(constrained,input.context.acceptedSequence,25));
        if(confirmation+1<ActivePictureTransitionModel::INITIAL_CONFIRMATIONS)Assert::IsFalse(decision.publish);
    }
    Assert::IsTrue(decision.publish&&decision.stable);
    Assert::AreEqual(int(ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT),int(decision.authorityOrigin));
    return decision;
}
ActivePictureObservation TrustedObservation(const ActivePictureBounds& bounds,uint64_t sequence,
    ActivePictureAuthorityOrigin origin=ActivePictureAuthorityOrigin::NATIVE)
{
    ActivePictureObservation observation;observation.bounds=bounds;observation.frameNumber=sequence;
    observation.available=true;observation.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
    observation.framesPerSecond=25;observation.authorityOrigin=origin;return observation;
}
} // namespace

TEST_CLASS(SparseBoundaryCropExperimentTests)
{
public:
    TEST_METHOD(DefaultOffPreservesRawPixelsEvidenceAndNativeTransitionDecisions)
    {
        SparseBoundaryCropExperiment experiment;ActivePictureTransitionModel baseline,disabled;
        SparsePixels pixels;auto input=EligibleInput();input.enabled=false;
        for(int frame=0;frame<12;++frame)
        {
            pixels.Picture(frame);const auto before=pixels.pixels;const auto source=pixels.Source();
            const auto raw=ExtractActivePictureEvidence(source);const auto result=experiment.Observe(source,raw,input);
            Assert::IsFalse(result.candidateAvailable||result.referenceAvailable||result.discoverySampled);
            Assert::AreEqual(size_t(0),result.discoverySamples);Assert::AreEqual(size_t(0),result.fixedExteriorSamples);
            SameRaw(raw,result.evidence);Assert::IsTrue(before==pixels.pixels);
            const auto a=baseline.Observe(MakeActivePictureObservation(raw,input.context.acceptedSequence,25));
            const auto b=disabled.Observe(MakeActivePictureObservation(result.evidence,input.context.acceptedSequence,25));
            Assert::AreEqual(a.publish,b.publish);Assert::AreEqual(a.stable,b.stable);SameBounds(a.bounds,b.bounds);
            SameBounds(a.stableBounds,b.stableBounds);Assert::AreEqual(a.reason,b.reason);NextSparse(input);
        }
        input.enabled=true;const auto first=FeedSparse(experiment,pixels,input,0);
        Assert::IsFalse(first.candidateAvailable,L"Disabled observations must not secretly mature acquisition.");
    }

    TEST_METHOD(ActualSparseSourceNeedsTemporalEvidenceAndOrdinaryPublicationConfirmation)
    {
        SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();
        ActivePictureTransitionModel model;const auto decision=PublishSparse(model,experiment,pixels,input);
        const auto crop=CropInput(decision.bounds,input.context.acceptedSequence);
        const auto shown=AlphaSourceCrop::AdmitCropPresentation({},crop,AlphaSourceCrop::Evaluate(crop),13);
        Assert::IsFalse(shown.blocked);Assert::IsTrue(shown.presentation.applyCrop);
        SameBounds(decision.bounds,shown.presentation.sourceBounds);
        Assert::AreEqual(26,decision.bounds.top);Assert::AreEqual(190,decision.bounds.bottom);
        ActivePictureBounds remembered;Assert::IsFalse(model.FindRecentTrustedBarGeometry(decision.bounds,remembered));
    }

    TEST_METHOD(CandidatePreservesRawAxisMeasurementsAndReportsItsExperimentalOrigin)
    {
        SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();
        const auto result=MatureSparse(experiment,pixels,input);
        const auto raw=ExtractActivePictureEvidence(pixels.Source());
        Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
        Assert::IsTrue(result.evidence.axisEvidence==raw.axisEvidence);
        Assert::AreEqual(raw.top.confidence,result.evidence.top.confidence);
        Assert::AreEqual(raw.bottom.confidence,result.evidence.bottom.confidence);
        Assert::AreEqual(int(ActivePictureAuthorityOrigin::NATIVE),int(raw.authorityOrigin));
        Assert::AreEqual(int(ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT),int(result.evidence.authorityOrigin));
        const auto snapshot=pixels.pixels;experiment.Observe(pixels.Source(),raw,input);Assert::IsTrue(snapshot==pixels.pixels);
    }

    TEST_METHOD(RepeatedSequenceAndCadenceRepeatCannotSupplyTemporalVotes)
    {
        for(int kind=0;kind<2;++kind)
        {
            SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();
            for(int phase=0;phase<16;++phase)
            {
                if(phase){input.context.timestampMs+=200;if(kind)++input.context.acceptedSequence;}
                input.context.cadenceRepeat=kind!=0;
                const auto result=FeedSparse(experiment,pixels,input,phase);
                Assert::IsFalse(result.candidateAvailable||result.referenceAvailable);
            }
        }
    }

    TEST_METHOD(MatureCandidateRequiresFreshValidationBetweenDiscoveryTicks)
    {
        SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();
        const auto acquired=MatureSparse(experiment,pixels,input);NextSparse(input,40);
        const auto fresh=FeedSparse(experiment,pixels,input,3);
        Assert::IsTrue(fresh.candidateAvailable&&fresh.current);Assert::IsFalse(fresh.discoverySampled);
        Assert::AreEqual(acquired.referenceId,fresh.referenceId);Assert::IsTrue(fresh.fixedExteriorSamples>0);
        const auto duplicate=FeedSparse(experiment,pixels,input,0);
        Assert::IsFalse(duplicate.candidateAvailable,L"One source sequence cannot supply another transition confirmation.");
    }

    TEST_METHOD(EveryIdentityChangeCutAndSequenceDiscontinuityDiscardsPendingProof)
    {
        for(int fault=0;fault<12;++fault)
        {
            SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();MatureSparse(experiment,pixels,input);
            NextSparse(input,40);
            switch(fault)
            {
            case 0:++input.context.sourceGeneration;break;case 1:++input.context.rendererGeneration;break;
            case 2:++input.context.viewportGeneration;break;case 3:++input.context.sourceFormatGeneration;break;
            case 4:++input.context.policyGeneration;break;case 5:++input.context.continuityGeneration;break;
            case 6:++input.context.sceneGeneration;break;case 7:input.sceneCut=true;break;
            case 8:input.context.acceptedSequence+=2;break;case 9:input.context.acceptedSequence=1;break;
            case 10:input.context.timestampMs+=600;break;case 11:input.context.timestampMs=1;break;
            }
            const auto result=FeedSparse(experiment,pixels,input,0);
            Assert::IsFalse(result.candidateAvailable||result.referenceAvailable);
            Assert::IsTrue(result.reset);
        }
    }

    TEST_METHOD(DisableOrLossOfStartupEligibilityCannotRetainOrResurrectPendingCrop)
    {
        for(int fault=0;fault<2;++fault)
        {
            SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();MatureSparse(experiment,pixels,input);
            NextSparse(input,40);if(fault)input.startupEligible=false;else input.enabled=false;
            const auto stopped=FeedSparse(experiment,pixels,input,0);Assert::IsFalse(stopped.candidateAvailable||stopped.referenceAvailable);
            input.enabled=input.startupEligible=true;NextSparse(input,40);
            Assert::IsFalse(FeedSparse(experiment,pixels,input,1).candidateAvailable);
        }
    }

    TEST_METHOD(InvalidPrecisionAndMismatchedSourceIdentityCannotAcquire)
    {
        for(int fault=0;fault<5;++fault)
        {
            SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();
            for(int phase=0;phase<5;++phase)
            {
                pixels.Picture(phase);auto source=pixels.Source();
                if(fault==0)source.encoding=VideoFrameEncoding::UNKNOWN;
                if(fault==1)source.encoding=VideoFrameEncoding::UYVY;
                if(fault==2)source.generation=8;
                if(fault==3)source.dataBytes=4;
                if(fault==4)input.context.acceptedSequence=0;
                const auto result=experiment.Observe(source,ExtractActivePictureEvidence(source),input);
                Assert::IsFalse(result.candidateAvailable||result.referenceAvailable);NextSparse(input);
            }
        }
    }

    TEST_METHOD(CurrentExteriorLumaAndChromaIntrusionsRejectMaturePendingReference)
    {
        for(int channel=0;channel<2;++channel)
        {
            SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();MatureSparse(experiment,pixels,input);
            NextSparse(input,40);pixels.Picture(3);
            pixels.Rectangle(0,0,384,20,channel?64:300,channel?550:512,512);
            const auto source=pixels.Source();const auto result=experiment.Observe(source,ExtractActivePictureEvidence(source),input);
            Assert::IsFalse(result.candidateAvailable||result.referenceAvailable);
            Assert::IsTrue(result.reset);
        }
    }

    TEST_METHOD(FrozenExteriorCutoffCannotRiseWithCurrentBackgroundFloor)
    {
        SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();
        const auto acquired=MatureSparse(experiment,pixels,input);Assert::AreEqual(88,acquired.fixedExterior.blackThreshold);
        NextSparse(input,40);
        for(size_t row=0;row<size_t(pixels.height);++row)for(int x=0;x<pixels.width;++x)
            if(pixels.pixels[row*pixels.pitch+x]==uint16_t(64<<6))pixels.pixels[row*pixels.pitch+x]=uint16_t(100<<6);
        const auto source=pixels.Source();const auto result=experiment.Observe(source,ExtractActivePictureEvidence(source),input);
        Assert::IsFalse(result.candidateAvailable||result.referenceAvailable);
        Assert::AreEqual(88,result.fixedExterior.blackThreshold);Assert::IsTrue(result.fixedExterior.topViolations>0);
    }
    TEST_METHOD(OutwardRoundingIncludesUnmeasuredBoundaryStripsAtFourK)
    {
        SparseBoundaryCropExperiment experiment;SparsePixels pixels(3840,2160);auto input=EligibleInput();
        const auto result=MatureSparse(experiment,pixels,input);
        Assert::AreEqual(272,result.referenceBounds.top);Assert::AreEqual(1888,result.referenceBounds.bottom);
        Assert::AreEqual(0,result.referenceBounds.top%2);Assert::AreEqual(0,result.referenceBounds.bottom%2);
        Assert::IsTrue(result.referenceBounds.top<=result.fixedExterior.topUnknownFirstY);
        Assert::IsTrue(result.referenceBounds.bottom>result.fixedExterior.bottomUnknownLastY);
        Assert::IsTrue(result.discoverySamples<=LOCAL_BOUNDARY_MAX_SAMPLES);
        NextSparse(input,40);
        // y273..279 and1879..1885 were not grid witnesses. They remain INSIDE
        // the rounded crop, so newly visible pixels there are not silently lost.
        pixels.Rectangle(2,274,26,280,400);pixels.Rectangle(2,1880,26,1886,400);
        const auto source=pixels.Source();const auto next=experiment.Observe(source,ExtractActivePictureEvidence(source),input);
        Assert::IsTrue(next.candidateAvailable);SameBounds(result.referenceBounds,next.referenceBounds);
    }

    TEST_METHOD(ShrinkingOrMissingFreshProposalKeepsPendingReferenceWhileOldExteriorIsSafe)
    {
        SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();const auto acquired=MatureSparse(experiment,pixels,input);
        NextSparse(input);pixels.Picture(3);pixels.Rectangle(0,170,384,188,64);
        const auto source=pixels.Source();const auto raw=ExtractActivePictureEvidence(source);
        Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
        const auto shrunk=experiment.Observe(source,raw,input);
        Assert::IsTrue(shrunk.candidateAvailable);SameBounds(acquired.referenceBounds,shrunk.referenceBounds);
        Assert::IsTrue(shrunk.diagnostic.observation.bottom<acquired.fixedExterior.bottom);
        NextSparse(input);pixels.Rectangle(0,0,384,216,64);
        const auto black=experiment.Observe(pixels.Source(),ExtractActivePictureEvidence(pixels.Source()),input);
        Assert::IsTrue(black.referenceAvailable&&black.fixedExterior.current);
        Assert::IsFalse(black.candidateAvailable,L"A retained reference never bypasses the global near-black acquisition guard.");
    }

    TEST_METHOD(NearBlackTitleArtworkCannotPublishThroughOrdinaryAcquisitionConstraint)
    {
        SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();ActivePictureTransitionModel model;
        for(int phase=0;phase<12;++phase)
        {
            pixels.Picture(phase,false);const auto source=pixels.Source();const auto dark=EvaluateActivePictureGlobalNearBlack(source);
            Assert::IsTrue(dark.evaluated&&dark.nearBlack);
            const auto result=experiment.Observe(source,ExtractActivePictureEvidence(source),input);
            const auto guarded=ConstrainNearBlackCropAcquisition(result.evidence,true);
            const auto decision=model.Observe(MakeActivePictureObservation(guarded,input.context.acceptedSequence,25));
            Assert::IsFalse(decision.publish);NextSparse(input);
        }
    }

    TEST_METHOD(PausedInteriorNoiseAndDriftingAsymmetricTitlesNeverAcquire)
    {
        for(int kind=0;kind<2;++kind)
        {
            SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();
            for(int phase=0;phase<12;++phase)
            {
                pixels.Picture(kind?phase:0,true,kind?8:28,kind?204:188);
                if(!kind)pixels.Rectangle(100,100,110,106,300+phase);
                const auto source=pixels.Source();const auto result=experiment.Observe(source,ExtractActivePictureEvidence(source),input);
                Assert::IsFalse(result.candidateAvailable);NextSparse(input);
            }
        }
    }

    TEST_METHOD(NativeAuthorityAlwaysWinsAndCannotBeRelabeledExperimental)
    {
        for(int shape=0;shape<2;++shape)
        {
            SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();MatureSparse(experiment,pixels,input);
            NextSparse(input,40);pixels.Rectangle(0,0,384,216,64);pixels.Rectangle(0,shape?0:28,384,shape?216:188,300);
            const auto source=pixels.Source();const auto raw=ExtractActivePictureEvidence(source);
            Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED||raw.classification==ActivePictureClassification::FULL_RASTER_TRUSTED);
            const auto result=experiment.Observe(source,raw,input);SameRaw(raw,result.evidence);
            Assert::IsFalse(result.candidateAvailable||result.referenceAvailable);
        }
    }

    TEST_METHOD(AdmittedCropUsesCurrentNativeRetentionWithoutDiagnosticExpiryBounce)
    {
        SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();ActivePictureTransitionModel model;
        const auto committed=PublishSparse(model,experiment,pixels,input);
        input.startupEligible=false;
        for(int frame=0;frame<170;++frame)
        {
            NextSparse(input);pixels.Rectangle(0,0,384,216,64); // Genuine fade, including no fresh candidate.
            const auto source=pixels.Source();const auto raw=ExtractActivePictureEvidence(source);
            Assert::IsFalse(experiment.Observe(source,raw,input).candidateAvailable);
            const auto retention=EvaluateActivePicturePresentationRetention(source,committed.bounds);
            Assert::IsTrue(retention.CanRetainPresentation());
            auto crop=CropInput(committed.bounds,input.context.acceptedSequence);
            crop.latestObservationSupportsCrop=false;crop.latestObservationClassification=raw.classification;
            crop.latestObservationIsUnavailable=raw.classification==ActivePictureClassification::UNAVAILABLE;
            crop.latestObservationIsProvisional=raw.classification==ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated=true;crop.frameLocalPresentationRetentionSafe=retention.CanRetainPresentation();
            const auto shown=AlphaSourceCrop::Evaluate(crop);Assert::IsTrue(shown.applyCrop);SameBounds(committed.bounds,shown.sourceBounds);
        }
        Assert::IsTrue(input.context.timestampMs>2*LOCAL_BOUNDARY_ANCHOR_MAX_AGE_MS);
    }

    TEST_METHOD(FullRasterExpansionWithdrawsExperimentalCropUsingExistingPolicy)
    {
        SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();ActivePictureTransitionModel model;
        const auto committed=PublishSparse(model,experiment,pixels,input);pixels.Rectangle(0,0,384,216,300);
        const auto source=pixels.Source();const auto raw=ExtractActivePictureEvidence(source);
        const auto retention=EvaluateActivePicturePresentationRetention(source,committed.bounds);
        Assert::IsFalse(retention.excludedBandsPixelSafe);Assert::IsTrue(raw.classification==ActivePictureClassification::FULL_RASTER_TRUSTED);
        auto crop=CropInput(committed.bounds);crop.latestObservationSupportsCrop=false;crop.latestObservationClassification=raw.classification;
        crop.fullRasterPresentationAuthoritative=true;crop.frameLocalPresentationRetentionEvaluated=true;
        crop.frameLocalPresentationRetentionSafe=retention.CanRetainPresentation();
        const auto shown=AlphaSourceCrop::Evaluate(crop);Assert::IsFalse(shown.applyCrop);
        Assert::AreEqual(0,shown.sourceBounds.top);Assert::AreEqual(216,shown.sourceBounds.bottom);
    }

    TEST_METHOD(CaptionAfterAcquisitionUsesOutwardPresentationWithoutChangingLogicalAspect)
    {
        SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();ActivePictureTransitionModel model;
        const auto committed=PublishSparse(model,experiment,pixels,input);pixels.Picture(3);
        pixels.Rectangle(96,196,288,204,700);const auto source=pixels.Source();
        const auto retention=EvaluateActivePicturePresentationRetention(source,committed.bounds);
        Assert::IsFalse(retention.excludedBandsPixelSafe);Assert::IsTrue(retention.outwardVisibleBoundsAvailable);
        auto crop=CropInput(committed.bounds);crop.latestObservationSupportsCrop=false;crop.latestObservationIsProvisional=true;
        crop.latestObservationClassification=ActivePictureClassification::PROVISIONAL;
        // The measured extent can be odd (207 here). The renderer composes and
        // rounds it outward before final crop arbitration; raw extent is not a
        // chroma-aligned presentation rectangle by itself.
        AlphaSourceCrop::PresentationEnvelopeCompositionInput envelopeInput;
        envelopeInput.trustedPicture=committed.bounds;
        envelopeInput.detectorContent.bounds=retention.outwardVisibleBounds;
        envelopeInput.detectorContent.expandBottom=retention.outwardVisibleBounds.bottom>committed.bounds.bottom;
        envelopeInput.verticalPadding=8;
        const auto envelope=AlphaSourceCrop::BuildComposedPresentationEnvelope(envelopeInput);
        Assert::IsTrue(envelope.valid&&envelope.expanded);
        Assert::IsTrue(envelope.bounds.bottom>=retention.outwardVisibleBounds.bottom);
        crop.outwardPresentationActive=crop.outwardExpansionAvailable=true;crop.outwardExpansion=envelope.bounds;
        crop.outwardExpansionSourceGeneration=7;const auto shown=AlphaSourceCrop::Evaluate(crop);
        Assert::IsTrue(shown.applyCrop&&shown.outwardExpanded);Assert::IsTrue(shown.sourceBounds.bottom>=204);
        SameBounds(committed.bounds,crop.geometry);
        ActivePictureBounds remembered;Assert::IsFalse(model.FindRecentTrustedBarGeometry(retention.outwardVisibleBounds,remembered));
    }

    TEST_METHOD(EquivalentPlanarAndPackedSourcesProduceTheSameGuardedCandidate)
    {
        std::array<SparseBoundaryCropExperiment,3> experiments;std::array<SparseBoundaryCropExperimentResult,3> results;
        auto input=EligibleInput();
        for(int phase=0;phase<4;++phase)
        {
            SparsePixels p010(384,216,false),p210;p010.Picture(phase);p210.Picture(phase);PackedSparsePixels packed(p210.Source());
            const AnalysisLumaSource sources[]={p010.Source(),p210.Source(),packed.Source()};
            for(int i=0;i<3;++i)results[i]=experiments[i].Observe(sources[i],ExtractActivePictureEvidence(sources[i]),input);
            NextSparse(input);
        }
        for(int i=0;i<3;++i)
        {
            Assert::IsTrue(results[i].candidateAvailable);SameBounds(results[0].referenceBounds,results[i].referenceBounds);
            Assert::AreEqual(results[0].fixedExterior.topPeakLuma,results[i].fixedExterior.topPeakLuma);
            Assert::AreEqual(results[0].fixedExterior.bottomPeakChromaDelta,results[i].fixedExterior.bottomPeakChromaDelta);
            Assert::IsTrue(results[i].discoverySamples<=LOCAL_BOUNDARY_MAX_SAMPLES);
        }
    }

    TEST_METHOD(ExperimentalGeometryCannotEnterRecentTrustedHistoryOrReacquireAsNative)
    {
        SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();ActivePictureTransitionModel model;
        const auto committed=PublishSparse(model,experiment,pixels,input);ActivePictureBounds remembered;
        Assert::IsFalse(model.FindRecentTrustedBarGeometry(committed.bounds,remembered));
        auto full=committed.bounds;full.top=0;full.bottom=216;full.aspectRatio=384.0/216;full.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
        for(int i=0;i<4;++i){auto observed=TrustedObservation(full,100+i);observed.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;model.Observe(observed);}
        for(int i=0;i<6;++i)
        {
            auto provisional=TrustedObservation(committed.bounds,110+i);provisional.classification=ActivePictureClassification::PROVISIONAL;
            const auto next=model.Observe(provisional);Assert::IsFalse(next.publish||next.knownTrustedGeometryReacquired);
        }
        Assert::IsFalse(model.FindRecentTrustedBarGeometry(committed.bounds,remembered));
    }

    TEST_METHOD(DefaultNativeOriginMatchesExplicitNativeAndExactNativeProofCanUpgradeExperiment)
    {
        const ActivePictureBounds bounds{0,26,384,190,384,216,384.0/164,ActivePictureBounds::BarAxes::TOP_BOTTOM};
        ActivePictureTransitionModel ordinary,explicitNative,experimental;
        for(uint64_t frame=1;frame<=4;++frame)
        {
            auto observed=TrustedObservation(bounds,frame);const auto a=ordinary.Observe(observed);
            observed.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;const auto b=explicitNative.Observe(observed);
            Assert::AreEqual(a.publish,b.publish);SameBounds(a.bounds,b.bounds);Assert::AreEqual(a.reason,b.reason);
            observed.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;experimental.Observe(observed);
        }
        // Recent history records replaced geometry, not the current stable entry.
        ActivePictureBounds remembered;Assert::IsFalse(ordinary.FindRecentTrustedBarGeometry(bounds,remembered));
        Assert::IsFalse(experimental.FindRecentTrustedBarGeometry(bounds,remembered));
        auto full=bounds;full.top=0;full.bottom=216;full.aspectRatio=384.0/216;full.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
        for(uint64_t frame=5;frame<=8;++frame)
        {
            auto observed=TrustedObservation(full,frame);observed.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            const auto a=ordinary.Observe(observed),b=explicitNative.Observe(observed);
            Assert::AreEqual(a.publish,b.publish);SameBounds(a.bounds,b.bounds);
        }
        Assert::IsTrue(ordinary.FindRecentTrustedBarGeometry(bounds,remembered));
        Assert::IsTrue(explicitNative.FindRecentTrustedBarGeometry(bounds,remembered));
        auto shifted=bounds;shifted.top-=2;shifted.bottom+=2;shifted.aspectRatio=384.0/(shifted.bottom-shifted.top);
        const auto nearbyDecision=experimental.Observe(TrustedObservation(shifted,5));
        Assert::AreEqual(int(ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT),int(nearbyDecision.stableAuthorityOrigin));
        Assert::IsFalse(experimental.FindRecentTrustedBarGeometry(bounds,remembered));
        const auto exact=experimental.Observe(TrustedObservation(bounds,6));
        Assert::AreEqual(int(ActivePictureAuthorityOrigin::NATIVE),int(exact.stableAuthorityOrigin));
        Assert::IsFalse(experimental.FindRecentTrustedBarGeometry(bounds,remembered));
        for(uint64_t frame=7;frame<=10;++frame)
        {
            auto observed=TrustedObservation(full,frame);observed.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            experimental.Observe(observed);
        }
        Assert::IsTrue(experimental.FindRecentTrustedBarGeometry(bounds,remembered));
    }

    TEST_METHOD(QueuedAdoptionRejectsExperimentalTargetButAcceptsNativeOutwardAuthority)
    {
        const ActivePictureBounds scope{0,26,384,190,384,216,384.0/164,ActivePictureBounds::BarAxes::TOP_BOTTOM};
        ActivePictureTransitionModel preview,consumer;ActivePictureTransitionDecision target;
        for(uint64_t sequence=1;sequence<=4;++sequence)
            target=preview.Observe(TrustedObservation(scope,sequence,ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT));
        Assert::IsTrue(target.publish);Assert::IsFalse(consumer.AdoptPublishedDecision(target,target.authoritativeClassification));
        for(uint64_t sequence=1;sequence<=4;++sequence)
            consumer.Observe(TrustedObservation(scope,sequence,ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT));
        auto imax=scope;imax.top=8;imax.bottom=208;imax.aspectRatio=384.0/200;
        ActivePictureTransitionModel nativePreview=consumer;target={};
        for(uint64_t sequence=5;sequence<=8;++sequence)
        {
            const auto observed=nativePreview.Observe(TrustedObservation(imax,sequence));
            if(observed.publish)target=observed;
        }
        Assert::IsTrue(target.publish);Assert::AreEqual(int(ActivePictureAuthorityOrigin::NATIVE),int(target.authorityOrigin));
        Assert::IsTrue(consumer.AdoptPublishedDecision(target,target.authoritativeClassification));
    }
    TEST_METHOD(NearBlackStartupEpisodeReopensBeforeFreshOrdinaryCropConfirmation)
    {
        using namespace AlphaSourceCrop;
        SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();
        ActivePictureTransitionModel model;NearBlackPresentationEpisodeState episode;
        uint64_t firstCandidate=0,firstCandidateTick=0,reopenedAt=0,publishedAt=0;
        uint32_t candidatesAfterReopen=0;ActivePictureTransitionDecision published;
        for(int frame=0;frame<60&&!publishedAt;++frame)
        {
            if(frame){NextSparse(input,40);pixels.Picture((frame-1)/5);}
            // Frame zero is actual all-black startup, with no trusted crop.
            const auto source=pixels.Source();const auto raw=ExtractActivePictureEvidence(source);
            const auto darkness=EvaluateActivePictureGlobalNearBlack(source);
            const auto result=experiment.Observe(source,raw,input);
            if(result.candidateAvailable&&!firstCandidate)
            {firstCandidate=input.context.acceptedSequence;firstCandidateTick=input.context.timestampMs;}
            const bool blocked=darkness.nearBlack||episode.mode!=NearBlackPresentationMode::INACTIVE;
            const auto observation=ConstrainNearBlackCropAcquisition(result.evidence,blocked);
            const auto transition=model.Observe(MakeActivePictureObservation(observation,input.context.acceptedSequence,25));
            NearBlackPresentationEpisodeInput proof;proof.previous=episode;
            proof.measurementCurrent=source.generation==input.context.sourceGeneration;
            proof.nearBlackEvaluated=darkness.evaluated;proof.globalNearBlack=darkness.nearBlack;
            proof.sourceGeneration=source.generation;proof.sourceSequence=input.context.acceptedSequence;
            proof.presentationEpoch=input.context.viewportGeneration;proof.currentTick=input.context.timestampMs;
            proof.framesPerSecond=25;proof.cadenceRepeat=input.context.cadenceRepeat;
            proof.currentObservationAvailable=observation.available;proof.currentObservationClassification=observation.classification;
            proof.currentObservation=observation.classification==ActivePictureClassification::PROVISIONAL
                ?observation.proposedBounds:observation.trustedBounds;
            proof.nativeBootstrapContractAvailable=result.candidateAvailable;
            if(result.candidateAvailable)
            {
                // Recompute current native retention of the exact experimental
                // rectangle, matching the renderer bootstrap input construction.
                const auto retention=EvaluateActivePicturePresentationRetention(source,result.evidence.trustedBounds);
                proof.nativeBootstrapContract=result.evidence.trustedBounds;
                proof.nativeBootstrapRetentionEvaluated=retention.analysisValid&&retention.presentationValid;
                proof.nativeBootstrapRetentionSafe=proof.nativeBootstrapRetentionEvaluated&&retention.currentlyPixelSafe;
                proof.nativeBootstrapOutwardVisible=retention.outwardVisibleBoundsAvailable;
                proof.nativeBootstrapSourceGeneration=source.generation;
                proof.nativeBootstrapSourceSequence=input.context.acceptedSequence;
                proof.nativeBootstrapPresentationEpoch=input.context.viewportGeneration;
            }
            const auto recovery=EvaluateNearBlackPresentationEpisode(proof);episode=recovery.state;
            if(frame==0)
            {
                Assert::IsTrue(recovery.started);Assert::IsTrue(episode.mode==NearBlackPresentationMode::FULL_RASTER);
                Assert::IsFalse(episode.entryTrustedCropAvailable);
            }
            if(!reopenedAt)
            {
                Assert::IsFalse(transition.publish,L"Bootstrap evidence cannot force a crop on the same source frame.");
                if(recovery.bootstrapReleased)
                {
                    reopenedAt=input.context.acceptedSequence;
                    Assert::IsTrue(firstCandidate>0&&reopenedAt>firstCandidate);
                    Assert::IsTrue(input.context.timestampMs-firstCandidateTick>=375);
                    Assert::IsTrue(recovery.resetTransitionEvidence&&recovery.ended);
                    Assert::IsFalse(recovery.releasedToTrustedCrop);
                    Assert::IsTrue(episode.mode==NearBlackPresentationMode::INACTIVE);
                }
            }
            else if(result.candidateAvailable)
            {
                ++candidatesAfterReopen;
                if(candidatesAfterReopen<ActivePictureTransitionModel::INITIAL_CONFIRMATIONS)Assert::IsFalse(transition.publish);
                if(transition.publish)
                {
                    published=transition;publishedAt=input.context.acceptedSequence;
                    Assert::AreEqual(uint32_t(ActivePictureTransitionModel::INITIAL_CONFIRMATIONS),candidatesAfterReopen);
                }
            }
            if(recovery.resetTransitionEvidence)model.ResetCandidateEvidence();
        }
        Assert::IsTrue(firstCandidate>0&&reopenedAt>firstCandidate&&publishedAt>reopenedAt);
        Assert::AreEqual(int(ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT),int(published.authorityOrigin));
        const auto crop=CropInput(published.bounds,publishedAt);
        const auto shown=AdmitCropPresentation({},crop,Evaluate(crop),input.context.viewportGeneration);
        Assert::IsFalse(shown.blocked);Assert::IsTrue(shown.presentation.applyCrop);SameBounds(published.bounds,shown.presentation.sourceBounds);
    }

    TEST_METHOD(MixedNativeAndExperimentalFramesCannotShareInitialConfirmationVotes)
    {
        const ActivePictureBounds bounds{0,26,384,190,384,216,384.0/164,ActivePictureBounds::BarAxes::TOP_BOTTOM};
        for(int direction=0;direction<2;++direction)
        {
            ActivePictureTransitionModel model;
            const auto first=direction?ActivePictureAuthorityOrigin::NATIVE:ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
            const auto next=direction?ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT:ActivePictureAuthorityOrigin::NATIVE;
            for(uint64_t frame=1;frame<=3;++frame)Assert::IsFalse(model.Observe(TrustedObservation(bounds,frame,first)).publish);
            for(uint64_t frame=4;frame<=7;++frame)
            {
                const auto decision=model.Observe(TrustedObservation(bounds,frame,next));
                Assert::AreEqual(frame==7,decision.publish);
                if(decision.publish)Assert::AreEqual(int(next),int(decision.authorityOrigin));
            }
        }
    }

    TEST_METHOD(StartupAdmissionClosesOncePerRealSourceGenerationAcrossUnrelatedResets)
    {
        SparseBoundaryStartupGate gate;
        Assert::IsFalse(gate.IsOpen(0));gate.ObserveSource(7);Assert::IsTrue(gate.IsOpen(7));
        gate.OnPublication(7);Assert::IsFalse(gate.IsOpen(7));
        // Analysis/profile/cut resets have no authority to invent a new source.
        gate.ObserveSource(0);gate.ObserveSource(7);Assert::IsFalse(gate.IsOpen(7));
        SparseBoundaryCropExperiment helper;helper.Reset();Assert::IsFalse(gate.IsOpen(7));
        gate.ObserveSource(8);Assert::IsTrue(gate.IsOpen(8));Assert::IsFalse(gate.IsOpen(7));
        gate.OnPublication(7);Assert::IsTrue(gate.IsOpen(8));
        gate.OnPublication(0);Assert::IsTrue(gate.IsOpen(8));
        gate.OnPublication(8);Assert::IsFalse(gate.IsOpen(8));
        gate.ObserveSource(0);gate.ObserveSource(8);Assert::IsFalse(gate.IsOpen(8));
    }

    TEST_METHOD(CaptionOnWouldBePublicationFrameCannotCompleteStaleExperimentalProof)
    {
        SparseBoundaryCropExperiment experiment;SparsePixels pixels;auto input=EligibleInput();
        ActivePictureTransitionModel model;const auto first=MatureSparse(experiment,pixels,input);
        for(int confirmation=0;confirmation<3;++confirmation)
        {
            SparseBoundaryCropExperimentResult result=first;
            if(confirmation){NextSparse(input,40);result=FeedSparse(experiment,pixels,input,3);}
            Assert::IsTrue(result.candidateAvailable);
            const auto dark=EvaluateActivePictureGlobalNearBlack(pixels.Source());Assert::IsFalse(dark.nearBlack);
            const auto guarded=ConstrainNearBlackCropAcquisition(result.evidence,dark.nearBlack);
            Assert::IsFalse(model.Observe(MakeActivePictureObservation(guarded,input.context.acceptedSequence,25)).publish);
        }
        NextSparse(input,40);pixels.Picture(3);pixels.Rectangle(96,196,288,204,700);
        const auto source=pixels.Source();const auto raw=ExtractActivePictureEvidence(source);
        const auto result=experiment.Observe(source,raw,input);
        Assert::IsFalse(result.candidateAvailable||result.referenceAvailable);Assert::IsTrue(result.reset);
        Assert::IsTrue(result.fixedExterior.bottomViolations>0);
        SameRaw(raw,result.evidence);
        const auto dark=EvaluateActivePictureGlobalNearBlack(source);
        const auto guarded=ConstrainNearBlackCropAcquisition(result.evidence,dark.nearBlack);
        const auto decision=model.Observe(MakeActivePictureObservation(guarded,input.context.acceptedSequence,25));
        Assert::IsFalse(decision.publish,L"Three earlier clean frames cannot hide a caption that appears at publication.");
        AlphaSourceCrop::Input crop;crop.automaticCropEnabled=true;
        crop.frameSourceGeneration=source.generation;crop.frameSourceSequence=input.context.acceptedSequence;
        crop.rasterWidth=source.width;crop.rasterHeight=source.height;
        crop.latestObservationClassification=raw.classification;
        const auto shown=AlphaSourceCrop::Evaluate(crop);
        Assert::IsFalse(shown.applyCrop);Assert::IsTrue(shown.sourceBounds.bottom>=204);
    }

    TEST_METHOD(TemporalAcquisitionWithoutLogicalGeometryCannotPromoteMovingArtwork)
    {
        SparseBoundaryCropExperiment experiment;
        SparsePixels artwork(3840, 2160);
        auto input = EligibleInput();
        const auto temporal = MatureSparse(experiment, artwork, input);
        const auto source = artwork.Source();
        const auto raw = ExtractActivePictureEvidence(source);
        const auto retained = EvaluateActivePicturePresentationRetention(source, temporal.referenceBounds);
        Assert::IsTrue(temporal.diagnostic.hypothesis.qualifies);
        Assert::IsTrue(temporal.diagnostic.hypothesis.ambiguous);
        Assert::IsTrue(retained.currentlyPixelSafe && retained.excludedBandsPixelSafe);
        Assert::IsFalse(retained.globalNearBlack || retained.outwardVisibleBoundsAvailable);
        Assert::IsTrue(raw.classification == ActivePictureClassification::PROVISIONAL);
        ActivePictureTransitionModel ordinary;
        for (int frame = 0; frame < 8; ++frame)
        {
            const auto decision = ordinary.Observe(MakeActivePictureObservation(raw, frame + 1, 25));
            Assert::IsFalse(decision.publish || decision.stable);
        }
        auto crop = CropInput(temporal.referenceBounds, input.context.acceptedSequence);
        crop.sharedGeometryAvailable = false;
        crop.latestObservationSupportsCrop = false;
        crop.latestObservationClassification = raw.classification;
        crop.latestObservationIsProvisional = raw.classification == ActivePictureClassification::PROVISIONAL;
        crop.frameLocalPresentationRetentionEvaluated = true;
        crop.frameLocalPresentationRetentionSafe = retained.currentlyPixelSafe;
        const auto shown = AlphaSourceCrop::AdmitCropPresentation({}, crop, AlphaSourceCrop::Evaluate(crop), 13);
        Assert::IsFalse(shown.presentation.applyCrop || shown.state.available);
    }

    TEST_METHOD(TemporalAcquisitionLogicalButNeverPresentedStillNeedsFreshAuthority)
    {
        for (bool p210 : { false, true })
        {
            SparseBoundaryCropExperiment experiment;
            SparsePixels artwork(3840, 2160, p210);
            auto input = EligibleInput();
            const auto temporal = MatureSparse(experiment, artwork, input);
            const auto retained = EvaluateActivePicturePresentationRetention(artwork.Source(), temporal.referenceBounds);
            Assert::IsTrue(temporal.diagnostic.hypothesis.qualifies && temporal.diagnostic.hypothesis.ambiguous);
            Assert::IsTrue(retained.currentlyPixelSafe && retained.excludedBandsPixelSafe);
            Assert::IsFalse(retained.globalNearBlack || retained.outwardVisibleBoundsAvailable);
            auto crop = CropInput(temporal.referenceBounds, input.context.acceptedSequence);
            crop.latestObservationSupportsCrop = false;
            crop.latestObservationClassification = retained.activePicture.classification;
        crop.latestObservationIsProvisional = retained.activePicture.classification == ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated = true;
            crop.frameLocalPresentationRetentionSafe = retained.currentlyPixelSafe;
            const auto candidate = AlphaSourceCrop::Evaluate(crop);
            Assert::IsTrue(candidate.applyCrop);
            Assert::AreEqual(int(AlphaSourceCrop::DecisionOwner::PIXEL_SAFE_RETENTION), int(candidate.owner));
            const auto shown = AlphaSourceCrop::AdmitCropPresentation({}, crop, candidate, 13);
            Assert::IsTrue(shown.blocked,
                L"A temporal rectangle plus clean current exterior is also satisfied by internal artwork; retention cannot create admission.");
            Assert::IsFalse(shown.presentation.applyCrop || shown.state.available);
            Assert::AreEqual(0, shown.presentation.sourceBounds.top);
            Assert::AreEqual(2160, shown.presentation.sourceBounds.bottom);
        }
    }

    TEST_METHOD(TemporalAcquisitionNativeRecoveryThenAmbiguousRetentionNeedsNoRestart)
    {
        SparseBoundaryCropExperiment experiment;
        SparsePixels pixels(3840, 2160);
        auto input = EligibleInput();
        const auto temporal = MatureSparse(experiment, pixels, input);
        const auto bounds = temporal.referenceBounds;
        const auto retained = EvaluateActivePicturePresentationRetention(pixels.Source(), bounds);
        Assert::IsTrue(temporal.diagnostic.hypothesis.qualifies && retained.currentlyPixelSafe);
        auto crop = CropInput(bounds, input.context.acceptedSequence);
        crop.latestObservationSupportsCrop = false;
        crop.latestObservationClassification = retained.activePicture.classification;
        crop.latestObservationIsProvisional = retained.activePicture.classification == ActivePictureClassification::PROVISIONAL;
        crop.frameLocalPresentationRetentionEvaluated = true;
        crop.frameLocalPresentationRetentionSafe = retained.currentlyPixelSafe;
        const auto refused = AlphaSourceCrop::AdmitCropPresentation({}, crop, AlphaSourceCrop::Evaluate(crop), 13);
        Assert::IsTrue(refused.blocked && !refused.state.available, L"Never-presented temporal/retention contract must remain unadmitted.");

        // Actual new source pixels, not a relabelled temporal hypothesis, supply
        // broad current native bars. Source generation and presentation epoch stay fixed.
        pixels.Rectangle(0, 0, pixels.width, pixels.height, 64);
        pixels.Rectangle(0, bounds.top, pixels.width, bounds.bottom, 300);
        const auto native = ExtractActivePictureEvidence(pixels.Source());
        Assert::IsTrue(native.classification == ActivePictureClassification::BAR_CROP_TRUSTED);
        Assert::IsTrue(native.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE);
        ActivePictureTransitionModel ordinary;
        ActivePictureTransitionDecision published;
        for (int frame = 0; frame < ActivePictureTransitionModel::INITIAL_CONFIRMATIONS; ++frame)
        {
            NextSparse(input, 40);
            published = ordinary.Observe(MakeActivePictureObservation(native, input.context.acceptedSequence, 25));
        }
        Assert::IsTrue(published.publish && published.stable);
        crop = CropInput(published.bounds, input.context.acceptedSequence);
        const auto acquired = AlphaSourceCrop::AdmitCropPresentation(refused.state, crop, AlphaSourceCrop::Evaluate(crop), 13);
        Assert::IsFalse(acquired.blocked);
        Assert::IsTrue(acquired.presentation.applyCrop && acquired.state.available);

        // Once genuinely admitted, identical ambiguous moving content can retain
        // this exact contract without manufacturing another acquisition event.
        pixels.Picture(3);
        const auto retention = EvaluateActivePicturePresentationRetention(pixels.Source(), published.bounds);
        Assert::IsTrue(retention.currentlyPixelSafe && retention.excludedBandsPixelSafe);
        Assert::IsTrue(retention.activePicture.classification == ActivePictureClassification::PROVISIONAL);
        ++crop.frameSourceSequence;
        crop.latestObservationSupportsCrop = false;
        crop.latestObservationClassification = retention.activePicture.classification;
        crop.latestObservationIsProvisional = retention.activePicture.classification == ActivePictureClassification::PROVISIONAL;
        crop.frameLocalPresentationRetentionEvaluated = true;
        crop.frameLocalPresentationRetentionSafe = retention.currentlyPixelSafe;
        const auto kept = AlphaSourceCrop::AdmitCropPresentation(acquired.state, crop, AlphaSourceCrop::Evaluate(crop), 13);
        Assert::IsFalse(kept.blocked);
        Assert::IsTrue(kept.presentation.applyCrop && kept.state.available);
        Assert::AreEqual(int(AlphaSourceCrop::DecisionOwner::PIXEL_SAFE_RETENTION), int(kept.presentation.owner));
        SameBounds(published.bounds, kept.presentation.sourceBounds);
    }

    TEST_METHOD(MovingArtworkCounterexampleRemainsAnExplicitAcquisitionLimitation)
    {
        SparseBoundaryCropExperiment experiment;SparsePixels artwork;auto input=EligibleInput();
        // These actual pixels can be moving yellow artwork on an intentional
        // full-raster black canvas. No pixel-only semantic oracle separates
        // that interpretation from sparse content inside encoded black bars.
        const auto result=MatureSparse(experiment,artwork,input);
        Assert::IsTrue(result.diagnostic.hypothesis.ambiguous);
        Assert::IsTrue(result.candidateAvailable);
        Assert::AreEqual(int(ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT),int(result.evidence.authorityOrigin));
    }
};
}





