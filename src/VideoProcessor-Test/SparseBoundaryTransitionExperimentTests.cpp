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
struct TransitionPixels
{
    int width, height;
    bool p210;
    size_t pitch;
    std::vector<uint16_t> pixels;
    TransitionPixels(int w=384,int h=216,bool nativePlanar=true)
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

void EqualTransitionBounds(const ActivePictureBounds& expected,const ActivePictureBounds& actual)
{
    Assert::AreEqual(expected.left,actual.left);Assert::AreEqual(expected.top,actual.top);
    Assert::AreEqual(expected.right,actual.right);Assert::AreEqual(expected.bottom,actual.bottom);
    Assert::AreEqual(expected.rasterWidth,actual.rasterWidth);Assert::AreEqual(expected.rasterHeight,actual.rasterHeight);
    Assert::AreEqual(int(expected.trustedBarAxes),int(actual.trustedBarAxes));
}
struct NativeTransitionBase
{
    ActivePictureTransitionModel model;ActivePictureBounds bounds;
    ActivePictureClassification classification=ActivePictureClassification::UNAVAILABLE;
    explicit NativeTransitionBase(bool full=false,int inset=8)
    {
        TransitionPixels pixels;pixels.Rectangle(0,full?0:inset,384,full?216:216-inset,300);
        const auto evidence=ExtractActivePictureEvidence(pixels.Source());
        classification=evidence.classification;bounds=evidence.trustedBounds;
        Assert::IsTrue(classification==(full?ActivePictureClassification::FULL_RASTER_TRUSTED:ActivePictureClassification::BAR_CROP_TRUSTED));
        bool published=false;
        for(uint64_t sequence=1;sequence<=4;++sequence)
            published|=model.Observe(MakeActivePictureObservation(evidence,sequence,25)).publish;
        Assert::IsTrue(published);
    }
};
SparseBoundaryCropExperimentInput TransitionInput(const NativeTransitionBase& base)
{
    SparseBoundaryCropExperimentInput input;input.enabled=input.transitionEligible=true;
    input.establishedBase=base.bounds;input.establishedBaseSourceGeneration=7;
    input.establishedBaseOrigin=ActivePictureAuthorityOrigin::NATIVE;
    input.context.sourceGeneration=7;input.context.rendererGeneration=11;input.context.viewportGeneration=13;
    input.context.sourceFormatGeneration=17;input.context.policyGeneration=19;input.context.continuityGeneration=23;
    input.context.sceneGeneration=29;input.context.acceptedSequence=10;input.context.timestampMs=100;return input;
}
void NextTransition(SparseBoundaryCropExperimentInput& input,uint64_t milliseconds=200)
{++input.context.acceptedSequence;input.context.timestampMs+=milliseconds;}
SparseBoundaryCropExperimentResult FeedTransition(SparseBoundaryCropExperiment& helper,TransitionPixels& pixels,
    SparseBoundaryCropExperimentInput& input,int phase)
{
    pixels.Picture(phase);const auto source=pixels.Source();
    return helper.Observe(source,ExtractActivePictureEvidence(source),input);
}
SparseBoundaryCropExperimentResult MatureTransition(SparseBoundaryCropExperiment& helper,TransitionPixels& pixels,
    SparseBoundaryCropExperimentInput& input)
{
    SparseBoundaryCropExperimentResult result;
    for(int phase=0;phase<4;++phase)
    {if(phase)NextTransition(input);result=FeedTransition(helper,pixels,input,phase);if(phase<3)Assert::IsFalse(result.candidateAvailable);}
    Logger::WriteMessage(result.reason);
    Assert::IsTrue(result.candidateAvailable&&result.current,L"Actual sparse source must qualify the distinct midmovie path.");
    Assert::AreEqual(int(ActivePictureAuthorityOrigin::SPARSE_TRANSITION_EXPERIMENT),int(result.evidence.authorityOrigin));
    Assert::IsTrue(result.evidence.sparseTransitionProof.available);return result;
}
AlphaSourceCrop::TransitionAdmissionInput RealTransitionAdmission(const AnalysisLumaSource& source,
    const ActivePictureEvidence& evidence,const ActivePictureBounds& base,uint64_t sequence)
{
    AlphaSourceCrop::TransitionAdmissionInput input;input.evidence=evidence;input.trustedGeometry=base;
    input.presentationBeforeObservation=base;input.trustedGeometryAvailable=input.compatiblePresentation=true;
    input.trustedGeneration=input.sourceGeneration=source.generation;input.sourceSequence=sequence;input.framesPerSecond=25;
    input.outwardCandidate=evidence.classification==ActivePictureClassification::PROVISIONAL?evidence.proposedBounds:evidence.trustedBounds;
    input.retention=EvaluateActivePicturePresentationRetention(source,base);return input;
}
ActivePictureTransitionDecision ConfirmTransition(NativeTransitionBase& base,SparseBoundaryCropExperiment& helper,
    TransitionPixels& pixels,SparseBoundaryCropExperimentInput& input,SparseBoundaryCropExperimentResult first)
{
    ActivePictureTransitionDecision published;
    for(int vote=0;vote<ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS;++vote)
    {
        auto candidate=first;if(vote){NextTransition(input,40);candidate=FeedTransition(helper,pixels,input,3);}
        Assert::IsTrue(candidate.candidateAvailable);
        const auto dark=EvaluateActivePictureGlobalNearBlack(pixels.Source());Assert::IsTrue(dark.evaluated&&!dark.nearBlack);
        const auto guarded=ConstrainNearBlackCropAcquisition(candidate.evidence,dark.nearBlack);
        const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(RealTransitionAdmission(pixels.Source(),guarded,base.bounds,input.context.acceptedSequence));
        Assert::IsFalse(admission.observation.transitionDeferred);
        const auto decision=base.model.Observe(admission.observation);
        if(vote+1<ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS)
        {Assert::IsFalse(decision.publish);EqualTransitionBounds(base.bounds,decision.stableBounds);}
        else{Assert::IsTrue(decision.publish);published=decision;}
    }
    return published;
}
AlphaSourceCrop::Decision PresentTransitionContract(const AnalysisLumaSource& source,const ActivePictureBounds& committed,
    ActivePictureClassification classification,const ActivePictureEvidence& current,uint64_t sequence,bool pending)
{
    AlphaSourceCrop::Input crop;crop.automaticCropEnabled=crop.sharedGeometryAvailable=true;
    crop.geometry=committed;crop.classification=classification;crop.geometrySourceGeneration=crop.frameSourceGeneration=source.generation;
    crop.frameSourceSequence=sequence;crop.rasterWidth=source.width;crop.rasterHeight=source.height;
    // The full-raster owner is derived from the still-committed contract. A
    // candidate is not a publication and must not clear this owner early.
    crop.fullRasterPresentationAuthoritative=AlphaSourceCrop::UpdateFullRasterPresentationAuthority(false,
        classification,committed.top==0&&committed.bottom==source.height&&committed.left==0&&committed.right==source.width);
    const auto retention=EvaluateActivePicturePresentationRetention(source,committed);
    crop.frameLocalPresentationRetentionEvaluated=retention.analysisValid&&retention.presentationValid;
    crop.frameLocalPresentationRetentionSafe=retention.CanRetainPresentation();
    crop.latestObservationClassification=current.classification;
    crop.latestObservationIsProvisional=current.classification==ActivePictureClassification::PROVISIONAL;
    crop.latestObservationIsUnavailable=current.classification==ActivePictureClassification::UNAVAILABLE;
    crop.latestObservationSupportsCrop=current.classification==ActivePictureClassification::BAR_CROP_TRUSTED&&
        AlphaSourceCrop::IsPixelSafeCropReaffirmation(committed,current.trustedBounds,retention.excludedBandsPixelSafe);
    crop.barCropRefinementPending=pending&&current.classification==ActivePictureClassification::BAR_CROP_TRUSTED;
    return AlphaSourceCrop::Evaluate(crop);
}
} // namespace

TEST_CLASS(SparseBoundaryTransitionExperimentTests)
{
public:
    TEST_METHOD(RealFullAndImaxSourcesTransitionInwardOnlyAfterCurrentProofAndOrdinaryConfirmation)
    {
        for(bool full:{false,true})
        {
            NativeTransitionBase base(full);SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
            const auto candidate=MatureTransition(helper,pixels,input);const auto raw=ExtractActivePictureEvidence(pixels.Source());
            Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
            Assert::IsTrue(candidate.evidence.axisEvidence==raw.axisEvidence);
            const auto held=PresentTransitionContract(pixels.Source(),base.bounds,base.classification,candidate.evidence,input.context.acceptedSequence,true);
            Assert::AreEqual(!full,held.applyCrop);Assert::AreEqual(base.bounds.top,held.sourceBounds.top);Assert::AreEqual(base.bounds.bottom,held.sourceBounds.bottom);
            const auto published=ConfirmTransition(base,helper,pixels,input,candidate);
            Assert::AreEqual(26,published.bounds.top);Assert::AreEqual(190,published.bounds.bottom);
            const auto shown=PresentTransitionContract(pixels.Source(),published.bounds,published.authoritativeClassification,
                candidate.evidence,input.context.acceptedSequence,false);
            Assert::IsTrue(shown.applyCrop);EqualTransitionBounds(published.bounds,shown.sourceBounds);
            Assert::AreEqual(int(ActivePictureAuthorityOrigin::SPARSE_TRANSITION_EXPERIMENT),int(published.authorityOrigin));
        }
    }

    TEST_METHOD(DefaultDisabledAndStartupOnlyModesCannotReplaceExistingAuthority)
    {
        for(int mode=0;mode<3;++mode)
        {
            NativeTransitionBase base;SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
            if(mode==0)input.enabled=false;
            if(mode==1){input.transitionEligible=false;input.startupEligible=true;}
            if(mode==2)input.startupEligible=true;
            for(int phase=0;phase<8;++phase)
            {
                const auto result=FeedTransition(helper,pixels,input,phase);
                if(mode!=1)Assert::IsFalse(result.candidateAvailable);
                const auto observation=MakeActivePictureObservation(result.evidence,input.context.acceptedSequence,25);
                Assert::IsFalse(base.model.Observe(observation).publish);NextTransition(input);
            }
        }
    }

    TEST_METHOD(TypedProofBindsCurrentSourceExactOldBaseAndGuardedRectangle)
    {
        NativeTransitionBase fixture;SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(fixture);
        const auto result=MatureTransition(helper,pixels,input);
        EqualTransitionBounds(fixture.bounds,result.evidence.sparseTransitionProof.establishedBase);
        EqualTransitionBounds(result.referenceBounds,result.evidence.sparseTransitionProof.guardedBounds);
        Assert::AreEqual(input.context.sourceGeneration,result.evidence.sparseTransitionProof.sourceGeneration);
        Assert::AreEqual(input.context.acceptedSequence,result.evidence.sparseTransitionProof.sourceSequence);
        Assert::AreEqual(result.referenceId,result.evidence.sparseTransitionProof.referenceId);
        for(int fault=0;fault<8;++fault)
        {
            NativeTransitionBase base;auto observed=MakeActivePictureObservation(result.evidence,20,25);
            observed.sparseTransitionProof.sourceSequence=20;
            switch(fault)
            {
            case 0:observed.sparseTransitionProof.available=false;break;
            case 1:observed.sparseTransitionProof.establishedBase.top+=2;break;
            case 2:observed.bounds.top+=2;break;
            case 3:observed.sparseTransitionProof.referenceId=0;break;
            case 4:observed.sparseTransitionProof.sourceGeneration=0;break;
            case 5:observed.sparseTransitionProof.sourceSequence=19;break;
            case 6:observed.bounds.left=12;observed.bounds.right-=12;observed.sparseTransitionProof.guardedBounds=observed.bounds;break;
            case 7:observed.bounds.top=0;observed.sparseTransitionProof.guardedBounds=observed.bounds;break;
            }
            for(int repeat=0;repeat<5;++repeat)
            {
                observed.frameNumber=20+repeat;
                if(fault!=5)observed.sparseTransitionProof.sourceSequence=observed.frameNumber;
                Assert::IsFalse(base.model.Observe(observed).publish);
            }
        }
    }

    TEST_METHOD(ChangedReferenceRestartsVotesAndNativeVotesCannotMixWithTransitionVotes)
    {
        NativeTransitionBase base;SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
        const auto result=MatureTransition(helper,pixels,input);
        auto first=MakeActivePictureObservation(result.evidence,20,25);first.sparseTransitionProof.sourceSequence=20;
        Assert::IsFalse(base.model.Observe(first).publish);
        auto changed=first;changed.frameNumber=21;changed.sparseTransitionProof.sourceSequence=21;++changed.sparseTransitionProof.referenceId;
        Assert::IsFalse(base.model.Observe(changed).publish);
        changed.frameNumber=22;changed.sparseTransitionProof.sourceSequence=22;Assert::IsTrue(base.model.Observe(changed).publish);
        NativeTransitionBase separate;
        auto native=first;native.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;native.axisEvidence={};native.sparseTransitionProof={};
        Assert::IsFalse(separate.model.Observe(native).publish);
        first.frameNumber=21;first.sparseTransitionProof.sourceSequence=21;Assert::IsFalse(separate.model.Observe(first).publish);
        first.frameNumber=22;first.sparseTransitionProof.sourceSequence=22;Assert::IsTrue(separate.model.Observe(first).publish);
    }

    TEST_METHOD(ContextCutAndOldBaseChangesInvalidateTheEntireDiscoveryWindow)
    {
        for(int fault=0;fault<13;++fault)
        {
            NativeTransitionBase base;SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
            MatureTransition(helper,pixels,input);NextTransition(input,40);
            switch(fault)
            {
            case 0:++input.context.sourceGeneration;break;case 1:++input.context.rendererGeneration;break;
            case 2:++input.context.viewportGeneration;break;case 3:++input.context.sourceFormatGeneration;break;
            case 4:++input.context.policyGeneration;break;case 5:++input.context.continuityGeneration;break;
            case 6:++input.context.sceneGeneration;break;case 7:input.sceneCut=true;break;
            case 8:input.establishedBase.top+=2;break;case 9:++input.establishedBaseSourceGeneration;break;
            case 10:input.establishedBaseOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;break;
            case 11:input.context.acceptedSequence+=2;break;case 12:input.context.timestampMs+=600;break;
            }
            const auto result=FeedTransition(helper,pixels,input,0);
            Assert::IsFalse(result.candidateAvailable||result.referenceAvailable);Assert::IsTrue(result.reset);
        }
    }

    TEST_METHOD(RestoringAnOldBaseCannotResurrectItsPreviousReference)
    {
        NativeTransitionBase base;SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
        MatureTransition(helper,pixels,input);const auto original=input.establishedBase;
        NextTransition(input,40);input.establishedBase.top+=2;Assert::IsFalse(FeedTransition(helper,pixels,input,0).candidateAvailable);
        NextTransition(input,40);input.establishedBase=original;Assert::IsFalse(FeedTransition(helper,pixels,input,1).candidateAvailable);
        NextTransition(input);Assert::IsFalse(FeedTransition(helper,pixels,input,2).candidateAvailable);
    }

    TEST_METHOD(RepeatsAndInteriorNoiseCannotSupplyMissingTransitionEvidence)
    {
        for(int kind=0;kind<3;++kind)
        {
            NativeTransitionBase base;SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
            for(int frame=0;frame<12;++frame)
            {
                pixels.Picture(kind==2?0:frame);if(kind==2)pixels.Rectangle(100,100,110,106,300+frame);
                input.context.cadenceRepeat=kind==1;
                const auto source=pixels.Source();const auto result=helper.Observe(source,ExtractActivePictureEvidence(source),input);
                Assert::IsFalse(result.candidateAvailable);input.context.timestampMs+=200;if(kind!=0)++input.context.acceptedSequence;
            }
        }
    }

    TEST_METHOD(FourPixelAndFivePercentAspectDeadbandsStillRetainTheOldFormat)
    {
        for(int inset:{22,24})
        {
            NativeTransitionBase base(false,inset);SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
            for(int phase=0;phase<10;++phase)
            {
                const auto result=FeedTransition(helper,pixels,input,phase);
                Assert::IsFalse(result.candidateAvailable);Assert::IsFalse(base.model.Observe(MakeActivePictureObservation(result.evidence,input.context.acceptedSequence,25)).publish);
                NextTransition(input);
            }
        }
        const ActivePictureBounds base{0,16,384,200,384,216,384.0/184,ActivePictureBounds::BarAxes::TOP_BOTTOM};
        auto within=base;within.top+=4;within.bottom-=4;within.aspectRatio=384.0/176;
        Assert::IsFalse(ActivePictureTransitionModel::IsSparseBoundaryInwardTransitionGeometry(base,within,0));
        auto material=within;material.top+=2;material.bottom-=2;material.aspectRatio=384.0/172;
        Assert::IsTrue(ActivePictureTransitionModel::IsSparseBoundaryInwardTransitionGeometry(base,material,0));
    }

    TEST_METHOD(ExperimentalOrAllSidedEstablishedBasesCannotAuthorizeNewTransitions)
    {
        for(int kind=0;kind<3;++kind)
        {
            NativeTransitionBase base;SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
            if(kind==0)input.establishedBaseOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
            if(kind==1)input.establishedBaseOrigin=ActivePictureAuthorityOrigin::SPARSE_TRANSITION_EXPERIMENT;
            if(kind==2){input.establishedBase.left=20;input.establishedBase.right=364;input.establishedBase.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH;}
            for(int phase=0;phase<6;++phase){Assert::IsFalse(FeedTransition(helper,pixels,input,phase).candidateAvailable);NextTransition(input);}
        }
    }

    TEST_METHOD(CurrentHorizontalAxisConflictCannotBeLaunderedByAValidVerticalProof)
    {
        NativeTransitionBase base;SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
        const auto result=MatureTransition(helper,pixels,input);
        auto observed=MakeActivePictureObservation(result.evidence,20,25);
        observed.axisEvidence.horizontal={ActivePictureAxisState::UNKNOWN,ActivePictureAxisReason::BAR_EDGE_REJECTED,true,true};
        for(uint64_t seq=20;seq<25;++seq)
        {observed.frameNumber=seq;observed.sparseTransitionProof.sourceSequence=seq;Assert::IsFalse(base.model.Observe(observed).publish);}
        NextTransition(input,40);auto raw=ExtractActivePictureEvidence(pixels.Source());raw.axisEvidence.horizontal=observed.axisEvidence.horizontal;
        Assert::IsFalse(helper.Observe(pixels.Source(),raw,input).candidateAvailable);
    }

    TEST_METHOD(GraduallyMovingBoundaryCannotAccumulateAStationaryAcquisitionWindow)
    {
        NativeTransitionBase base;SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
        for(int frame=0;frame<8;++frame)
        {
            pixels.Picture(frame,true,20+frame*2,196-frame*2);const auto source=pixels.Source();
            const auto result=helper.Observe(source,ExtractActivePictureEvidence(source),input);
            Assert::IsFalse(result.candidateAvailable);Assert::IsFalse(base.model.Observe(MakeActivePictureObservation(result.evidence,input.context.acceptedSequence,25)).publish);
            NextTransition(input);
        }
    }

    TEST_METHOD(CaptionOnPublicationFrameVetoesCurrentCandidateAndPreservesTheOldSafeCrop)
    {
        NativeTransitionBase base;SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
        const auto candidate=MatureTransition(helper,pixels,input);
        const auto first=AlphaSourceCrop::EvaluateTransitionAdmission(RealTransitionAdmission(pixels.Source(),candidate.evidence,base.bounds,input.context.acceptedSequence));
        Assert::IsFalse(base.model.Observe(first.observation).publish);
        NextTransition(input,40);pixels.Picture(3);pixels.Rectangle(96,196,288,204,700);
        const auto source=pixels.Source();const auto result=helper.Observe(source,ExtractActivePictureEvidence(source),input);
        Assert::IsFalse(result.candidateAvailable||result.referenceAvailable);
        const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(RealTransitionAdmission(source,result.evidence,base.bounds,input.context.acceptedSequence));
        Assert::IsFalse(base.model.Observe(admission.observation).publish);
        const auto shown=PresentTransitionContract(source,base.bounds,base.classification,result.evidence,input.context.acceptedSequence,false);
        Assert::IsTrue(shown.applyCrop);EqualTransitionBounds(base.bounds,shown.sourceBounds);Assert::IsTrue(shown.sourceBounds.bottom>=204);
    }

    TEST_METHOD(ActiveCaptionAndMovingPresentationOwnersCannotBeBypassed)
    {
        NativeTransitionBase base;SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
        const auto candidate=MatureTransition(helper,pixels,input);
        const auto current=RealTransitionAdmission(pixels.Source(),candidate.evidence,base.bounds,input.context.acceptedSequence);
        auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(current);
        AlphaSourceCrop::MovingPictureTransitionState moving;moving.active=true;
        AlphaSourceCrop::ConstrainMovingPictureTransition(moving,admission);
        Assert::IsTrue(admission.observation.transitionDeferred);Assert::IsFalse(base.model.Observe(admission.observation).publish);
        // The renderer's current caption/coarse-hold eligibility gate supplies
        // false here; neither a mature reference nor prior votes override it.
        NextTransition(input,40);input.transitionEligible=false;
        const auto blocked=FeedTransition(helper,pixels,input,3);
        Assert::IsFalse(blocked.candidateAvailable||blocked.referenceAvailable);
        Assert::IsFalse(base.model.Observe(MakeActivePictureObservation(blocked.evidence,input.context.acceptedSequence,25)).publish);
    }

    TEST_METHOD(DarkFadeCannotAcquireButAdmittedScopeRetainsWithoutReferenceExpiryBounce)
    {
        NativeTransitionBase base;SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
        for(int phase=0;phase<6;++phase)
        {
            pixels.Picture(phase,false);const auto source=pixels.Source();const auto raw=ExtractActivePictureEvidence(source);
            const auto result=helper.Observe(source,raw,input);const auto dark=EvaluateActivePictureGlobalNearBlack(source);
            Assert::IsTrue(dark.nearBlack);Assert::IsFalse(result.candidateAvailable);
            Assert::IsFalse(base.model.Observe(MakeActivePictureObservation(ConstrainNearBlackCropAcquisition(result.evidence,true),input.context.acceptedSequence,25)).publish);
            NextTransition(input);
        }
        helper.Reset();const auto candidate=MatureTransition(helper,pixels,input);const auto committed=ConfirmTransition(base,helper,pixels,input,candidate);
        input.transitionEligible=false;
        for(int frame=0;frame<170;++frame)
        {
            NextTransition(input);pixels.Rectangle(0,0,384,216,64);const auto source=pixels.Source();const auto raw=ExtractActivePictureEvidence(source);
            Assert::IsFalse(helper.Observe(source,raw,input).candidateAvailable);
            const auto shown=PresentTransitionContract(source,committed.bounds,committed.authoritativeClassification,raw,input.context.acceptedSequence,false);
            Assert::IsTrue(shown.applyCrop);EqualTransitionBounds(committed.bounds,shown.sourceBounds);
        }
        Assert::IsTrue(input.context.timestampMs>2*LOCAL_BOUNDARY_ANCHOR_MAX_AGE_MS);
    }

    TEST_METHOD(NativeEvidenceWinsAndReverseTransitionsKeepExistingConfirmationTiming)
    {
        for(bool full:{false,true})
        {
            NativeTransitionBase base;SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
            const auto candidate=MatureTransition(helper,pixels,input);const auto scope=ConfirmTransition(base,helper,pixels,input,candidate);
            NativeTransitionBase nativeBaseline;TransitionPixels cleanScope;
            cleanScope.Rectangle(0,scope.bounds.top,384,scope.bounds.bottom,300);
            const auto nativeScope=ExtractActivePictureEvidence(cleanScope.Source());
            Assert::IsTrue(nativeScope.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
            EqualTransitionBounds(scope.bounds,nativeScope.trustedBounds);
            for(uint64_t seq=5;seq<=8;++seq)nativeBaseline.model.Observe(MakeActivePictureObservation(nativeScope,seq,25));
            uint64_t experimentalPublished=0,controlPublished=0;
            pixels.Rectangle(0,0,384,216,64);pixels.Rectangle(0,full?0:8,384,full?216:208,300);
            const auto source=pixels.Source();const auto native=ExtractActivePictureEvidence(source);
            Assert::IsTrue(native.classification==(full?ActivePictureClassification::FULL_RASTER_TRUSTED:ActivePictureClassification::BAR_CROP_TRUSTED));
            AlphaSourceCrop::OutwardPictureConfirmationState outward;
            for(int frame=0;frame<8;++frame)
            {
                NextTransition(input,40);const auto result=helper.Observe(source,native,input);
                Assert::IsFalse(result.candidateAvailable);Assert::AreEqual(int(ActivePictureAuthorityOrigin::NATIVE),int(result.evidence.authorityOrigin));
                auto current=RealTransitionAdmission(source,native,scope.bounds,input.context.acceptedSequence);current.previousOutward=outward;
                const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(current);outward=admission.outward.state;
                const auto a=base.model.Observe(admission.observation),b=nativeBaseline.model.Observe(admission.observation);
                Assert::AreEqual(a.publish,b.publish);if(a.publish&&!experimentalPublished)experimentalPublished=input.context.acceptedSequence;
                if(b.publish&&!controlPublished)controlPublished=input.context.acceptedSequence;
            }
            Assert::IsTrue(experimentalPublished>0);Assert::AreEqual(controlPublished,experimentalPublished);
            ActivePictureBounds remembered;Assert::IsFalse(base.model.FindRecentTrustedBarGeometry(scope.bounds,remembered));
        }
    }

    TEST_METHOD(NativeToExperimentalToNativeCycleCanRepeatWithoutRememberingExperimentalScope)
    {
        NativeTransitionBase base;SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
        for(int cycle=0;cycle<2;++cycle)
        {
            helper.Reset();const auto candidate=MatureTransition(helper,pixels,input);const auto scope=ConfirmTransition(base,helper,pixels,input,candidate);
            pixels.Rectangle(0,0,384,216,64);pixels.Rectangle(0,8,384,208,300);const auto source=pixels.Source();const auto native=ExtractActivePictureEvidence(source);
            bool restored=false;AlphaSourceCrop::OutwardPictureConfirmationState outward;
            for(int frame=0;frame<8;++frame)
            {
                NextTransition(input,40);auto current=RealTransitionAdmission(source,native,scope.bounds,input.context.acceptedSequence);current.previousOutward=outward;
                const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(current);outward=admission.outward.state;
                const auto decision=base.model.Observe(admission.observation);if(decision.publish)restored=true;
            }
            Assert::IsTrue(restored);ActivePictureBounds remembered;Assert::IsFalse(base.model.FindRecentTrustedBarGeometry(scope.bounds,remembered));
            NextTransition(input);
        }
    }

    TEST_METHOD(FullRasterAuthoritySurvivesPendingExperimentAndClearsOnlyOnActualPublication)
    {
        NativeTransitionBase base(true);SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
        const auto first=MatureTransition(helper,pixels,input);bool authority=true;ActivePictureTransitionDecision published;
        for(int vote=0;vote<ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS;++vote)
        {
            auto candidate=first;if(vote){NextTransition(input,40);candidate=FeedTransition(helper,pixels,input,3);}
            authority=AlphaSourceCrop::UpdateFullRasterPresentationAuthority(authority,candidate.evidence.classification,false,candidate.evidence.authorityOrigin);
            Assert::IsTrue(authority,L"A pending local hypothesis must not revoke the old full-raster presentation.");
            const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(RealTransitionAdmission(pixels.Source(),candidate.evidence,base.bounds,input.context.acceptedSequence));
            const auto decision=base.model.Observe(admission.observation);
            authority=AlphaSourceCrop::CommitSparseTransitionPresentationAuthority(authority,decision);
            if(!decision.publish)Assert::IsTrue(authority);
            else{Assert::IsFalse(authority);published=decision;}
        }
        Assert::IsTrue(published.publish);
        for(int fault=0;fault<6;++fault)
        {
            auto invalid=published;
            if(fault==0)invalid.publish=false;if(fault==1)invalid.stable=false;if(fault==2)invalid.clearTransition=true;
            if(fault==3)invalid.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
            if(fault==4)invalid.authoritativeClassification=ActivePictureClassification::PROVISIONAL;
            if(fault==5)invalid.bounds.left=12;
            Assert::IsTrue(AlphaSourceCrop::CommitSparseTransitionPresentationAuthority(true,invalid));
        }
        Assert::IsFalse(AlphaSourceCrop::UpdateFullRasterPresentationAuthority(true,
            ActivePictureClassification::BAR_CROP_TRUSTED,false),L"Default native behavior must remain unchanged.");
    }
    TEST_METHOD(PendingTransitionCannotReclassifyOldFullRasterButExactCommittedScopeCanReaffirm)
    {
        NativeTransitionBase base(true);SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
        const auto candidate=MatureTransition(helper,pixels,input);
        const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(RealTransitionAdmission(pixels.Source(),candidate.evidence,base.bounds,input.context.acceptedSequence));
        const auto pending=base.model.Observe(admission.observation);Assert::IsFalse(pending.publish);
        ActivePictureClassification ownedClassification=base.classification;bool fullAuthority=true;
        const bool mayReaffirm=AlphaSourceCrop::SparseTransitionMayReaffirmOwnedCrop(base.bounds,ActivePictureAuthorityOrigin::NATIVE,candidate.evidence);
        Assert::IsFalse(mayReaffirm,L"Containment in old full raster is not permission to relabel it as experimental bar authority.");
        if(mayReaffirm)ownedClassification=candidate.evidence.classification;
        fullAuthority=AlphaSourceCrop::UpdateFullRasterPresentationAuthority(fullAuthority,candidate.evidence.classification,false,candidate.evidence.authorityOrigin);
        Assert::IsTrue(ownedClassification==ActivePictureClassification::FULL_RASTER_TRUSTED&&fullAuthority);
        // Abort with actual black pixels on the next source frame. The old
        // native full-raster owner must survive without a phantom BAR contract.
        NextTransition(input,40);pixels.Rectangle(0,0,384,216,64);const auto source=pixels.Source();
        const auto dark=EvaluateActivePictureGlobalNearBlack(source);Assert::IsTrue(dark.nearBlack);
        const auto raw=ExtractActivePictureEvidence(source);const auto aborted=helper.Observe(source,raw,input);
        Assert::IsFalse(aborted.candidateAvailable);
        const auto held=base.model.Observe(MakeActivePictureObservation(ConstrainNearBlackCropAcquisition(aborted.evidence,true),input.context.acceptedSequence,25));
        Assert::IsFalse(held.publish);Assert::IsTrue(held.authoritativeClassification==ActivePictureClassification::FULL_RASTER_TRUSTED);
        EqualTransitionBounds(base.bounds,held.stableBounds);
        fullAuthority=AlphaSourceCrop::UpdateFullRasterPresentationAuthority(fullAuthority,raw.classification,false,raw.authorityOrigin);
        Assert::IsTrue(fullAuthority);Assert::IsTrue(ownedClassification==ActivePictureClassification::FULL_RASTER_TRUSTED);
        helper.Reset();base.model.ResetCandidateEvidence();NextTransition(input);
        auto current=MatureTransition(helper,pixels,input);ActivePictureTransitionDecision committed;
        for(int vote=0;vote<ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS;++vote)
        {
            if(vote){NextTransition(input,40);current=FeedTransition(helper,pixels,input,3);}
            const auto checked=AlphaSourceCrop::EvaluateTransitionAdmission(RealTransitionAdmission(pixels.Source(),current.evidence,base.bounds,input.context.acceptedSequence));
            committed=base.model.Observe(checked.observation);
        }
        Assert::IsTrue(committed.publish);
        Assert::IsTrue(AlphaSourceCrop::SparseTransitionMayReaffirmOwnedCrop(committed.bounds,committed.authorityOrigin,current.evidence));
        Assert::IsFalse(AlphaSourceCrop::SparseTransitionMayReaffirmOwnedCrop(committed.bounds,ActivePictureAuthorityOrigin::NATIVE,current.evidence));
        auto different=current.evidence;different.trustedBounds.top+=2;different.trustedBounds.bottom-=2;
        Assert::IsFalse(AlphaSourceCrop::SparseTransitionMayReaffirmOwnedCrop(committed.bounds,committed.authorityOrigin,different));
        different=current.evidence;different.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
        Assert::IsTrue(AlphaSourceCrop::SparseTransitionMayReaffirmOwnedCrop(base.bounds,ActivePictureAuthorityOrigin::NATIVE,different));
        different.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
        Assert::IsTrue(AlphaSourceCrop::SparseTransitionMayReaffirmOwnedCrop(base.bounds,ActivePictureAuthorityOrigin::NATIVE,different));
    }
    TEST_METHOD(ExperimentalTransitionCannotBeQueuedOrReacquiredFromNativeHistory)
    {
        NativeTransitionBase base;SparseBoundaryCropExperiment helper;TransitionPixels pixels;auto input=TransitionInput(base);
        const auto candidate=MatureTransition(helper,pixels,input);const auto published=ConfirmTransition(base,helper,pixels,input,candidate);
        NativeTransitionBase consumer;Assert::IsFalse(consumer.model.AdoptPublishedDecision(published,published.authoritativeClassification));
        ActivePictureBounds remembered;Assert::IsFalse(base.model.FindRecentTrustedBarGeometry(published.bounds,remembered));
        auto stale=MakeActivePictureObservation(candidate.evidence,input.context.acceptedSequence+1,25);
        stale.sparseTransitionProof.sourceSequence=stale.frameNumber;
        Assert::IsFalse(base.model.Observe(stale).publish,L"An experimental stable base cannot recursively authorize another experimental transition.");
    }
};
}


