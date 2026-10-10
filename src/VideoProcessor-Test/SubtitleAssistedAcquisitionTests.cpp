#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/SubtitleAssistedAcquisition.h>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;
namespace Tests {
TEST_CLASS(SubtitleAssistedAcquisitionTests) {
    static SubtitleAssistedAcquisitionInput Input(uint64_t n) {
        SubtitleAssistedAcquisitionInput i;i.identity={1,n,n,n*1000,2,3,4};i.proofIdentity=i.identity;
        i.sourceGeneration=1;i.policyGeneration=5;i.continuityGeneration=6;
        i.candidate={0,28,640,332,640,360};i.protectedBounds={0,28,640,352,640,360};i.sourceProofAccepted=true;return i;
    }
    static void Confirm(SubtitleAssistedAcquisitionGate& g) {
        for(uint64_t n=1;n<=4;++n)Assert::AreEqual(n==4,g.Evaluate(Input(n)).confirmed);
    }
public:
    TEST_METHOD(FourFreshFramesAndCurrentProofRequired) {
        SubtitleAssistedAcquisitionGate g;Confirm(g);
        const auto r=g.Evaluate(Input(5));Assert::IsTrue(r.confirmed);Assert::AreEqual(352,r.protectedBounds.bottom);
        Assert::IsTrue(r.candidate.trustedBarAxes==ActivePictureBounds::BarAxes::NONE);
    }
    TEST_METHOD(RepeatNeverAddsVoteOrRestoresFailure) {
        SubtitleAssistedAcquisitionGate g;auto i=Input(1);
        for(int n=0;n<10;++n)Assert::AreEqual(1u,g.Evaluate(i).matchingFrames);
        i.sourceProofAccepted=false;Assert::IsFalse(g.Evaluate(i).confirmed);
        i.sourceProofAccepted=true;Assert::AreEqual(0u,g.Evaluate(i).matchingFrames);
    }
    TEST_METHOD(ConfirmedLossWidensAndLatchesThroughNewCueFrames) {
        SubtitleAssistedAcquisitionGate g;Confirm(g);auto i=Input(5);i.sourceProofAccepted=false;
        Assert::IsTrue(g.Evaluate(i).inhibited);
        for(uint64_t n=6;n<15;++n)Assert::IsFalse(g.Evaluate(Input(n)).confirmed);
        i=Input(15);i.independentRawBandsClear=true;i.sourceProofAccepted=false;Assert::IsFalse(g.Evaluate(i).inhibited);
        for(uint64_t n=16;n<20;++n)Assert::AreEqual(n==19,g.Evaluate(Input(n)).confirmed);
    }
    TEST_METHOD(SameSequenceFailureImmediatelyWithdraws) {
        SubtitleAssistedAcquisitionGate g;Confirm(g);auto i=Input(4);i.sourceProofAccepted=false;
        Assert::IsTrue(g.Evaluate(i).inhibited);i.sourceProofAccepted=true;i.independentRawBandsClear=true;
        Assert::IsFalse(g.Evaluate(i).confirmed);
    }
    TEST_METHOD(ForeignStaleOrMalformedProofCannotVote) {
        for(int defect=0;defect<10;++defect) {
            SubtitleAssistedAcquisitionGate g;auto i=Input(1);
            if(defect==0)++i.proofIdentity.acceptedSequence;
            if(defect==1)++i.proofIdentity.transportGeneration;
            if(defect==2)++i.proofIdentity.sourceFormatGeneration;
            if(defect==3)++i.proofIdentity.viewportGeneration;
            if(defect==4)++i.proofIdentity.rendererGeneration;
            if(defect==5)++i.proofIdentity.captureTimestamp;
            if(defect==6)++i.sourceGeneration;
            if(defect==7)i.candidate.left=2;
            if(defect==8)i.protectedBounds.bottom=i.candidate.bottom-1;
            if(defect==9)i.proofIdentity.sourceFrameNumber++;
            Assert::AreEqual(0u,g.Evaluate(i).matchingFrames);
            Assert::AreEqual(0u,g.Evaluate(Input(1)).matchingFrames);
        }
    }
    TEST_METHOD(EveryContextChangeRequiresNewConfirmation) {
        for(int change=0;change<7;++change) {
            SubtitleAssistedAcquisitionGate g;Confirm(g);auto i=Input(5);
            if(change==0)++i.identity.transportGeneration,++i.sourceGeneration;
            if(change==1)++i.identity.sourceFormatGeneration;
            if(change==2)++i.identity.viewportGeneration;
            if(change==3)++i.identity.rendererGeneration;
            if(change==4)++i.policyGeneration;
            if(change==5)++i.continuityGeneration;
            if(change==6)i=Input(1);

            i.proofIdentity=i.identity;const auto r=g.Evaluate(i);
            Assert::IsFalse(r.confirmed);Assert::AreEqual(1u,r.matchingFrames);
        }
    }
    TEST_METHOD(GapOrRepeatedSourceFrameCannotMaintainProof) {
        for(bool gap:{false,true}) {
            SubtitleAssistedAcquisitionGate g;Confirm(g);auto i=Input(gap?6:5);
            if(!gap)i.identity.sourceFrameNumber=4,i.proofIdentity=i.identity;
            const auto r=g.Evaluate(i);Assert::IsFalse(r.confirmed);Assert::IsTrue(r.inhibited);
        }
    }
    TEST_METHOD(DiscontinuityAndResetDoNotCarryAuthority) {
        SubtitleAssistedAcquisitionGate g;Confirm(g);auto i=Input(5);i.discontinuity=true;
        Assert::AreEqual(0u,g.Evaluate(i).matchingFrames);Assert::IsFalse(g.Evaluate(Input(5)).confirmed);
        g.Reset();Assert::AreEqual(1u,g.Evaluate(Input(5)).matchingFrames);
    }
};
}


#include <vprenderer/SubtitleAssistedPresentation.h>
namespace Tests {
TEST_CLASS(SubtitleAssistedPresentationTests) {
    struct Fixture {
        SubtitleBoxObservation observation;
        SubtitleBoxResult accepted;
        SubtitleAssistedPresentationInput input;
        Fixture() {
            input.identity={1,4,4,4000,2,3,4};input.policyGeneration=5;input.continuityGeneration=6;
            input.proof.confirmed=true;input.proof.identity=input.identity;
            input.proof.policyGeneration=5;input.proof.continuityGeneration=6;
            input.proof.candidate={0,28,640,332,640,360};input.proof.protectedBounds={0,28,640,354,640,360};
            observation.identity=input.identity;observation.policyGeneration=5;observation.continuityGeneration=6;
            observation.width=640;observation.height=360;observation.pictureTop=28;observation.pictureBottom=332;
            observation.analyzed=observation.assistedSourceCandidate=true;observation.assistedBounds=input.proof.candidate;
            observation.text.detected=true;observation.text.lineCount=1;observation.text.bounds={200,338,400,350};
            observation.text.lineBounds[0]=observation.text.bounds;accepted=observation.text;
            auto ink=std::make_shared<SubtitleInkSnapshot>();ink->rawEvidenceComplete=true;
            ink->step=SubtitleBoxDetector::SamplingStep(640,360);ink->width=(640+ink->step-1)/ink->step;
            ink->height=(360+ink->step-1)/ink->step;
            for(int y=0;y<360;y+=ink->step)ink->sourceRows.push_back(y);
            const size_t words=(size_t(ink->width)*ink->height+63)/64;
            ink->rawInk.resize(words);ink->ownedInk.resize(words);
            const size_t bit=size_t(340/ink->step)*ink->width+220/ink->step;
            ink->rawInk[bit/64]=ink->ownedInk[bit/64]=uint64_t(1)<<(bit%64);observation.ink=ink;
            input.observation=&observation;input.acceptedCue=&accepted;input.compositionSucceeded=true;
            input.move.valid=true;input.move.content=SubtitleGlyphCaptureBounds(accepted,640,360);
            input.move.glyphLines=SubtitleGlyphCaptureLines(accepted,640,360);
            input.move.source={190,334,410,356};input.move.destination={190,280,410,302};
            input.move.pictureTop=28;input.move.pictureBottom=332;
        }
        auto Select() const {return SelectSubtitleAssistedPresentation(input);}
    };
public:
    TEST_METHOD(ExactCompositionSelectsCandidateWithNoNativeAxes) {
        Fixture f;const auto r=f.Select();Assert::IsTrue(r.available);Assert::IsTrue(r.composed);
        Assert::AreEqual(332,r.bounds.bottom);Assert::IsTrue(r.bounds.trustedBarAxes==ActivePictureBounds::BarAxes::NONE);
    }
    TEST_METHOD(UncomposedProtectsActualCurrentGlyphsEvenAcceptedCueDiffers) {
        Fixture f;f.input.compositionSucceeded=false;f.accepted.bounds.bottom=359;f.accepted.held=true;
        const auto r=f.Select();Assert::IsTrue(r.available);Assert::IsFalse(r.composed);Assert::IsTrue(r.bounds.bottom>=354);
    }
    TEST_METHOD(UncertifiedComposedFootprintFailsOpen) {
        for(int defect=0;defect<10;++defect) {
            Fixture f;
            if(defect==0)f.accepted.held=true;
            if(defect==1)f.accepted.lineBounds[0].left++;
            if(defect==2)f.input.move.content.right++;
            if(defect==3)f.input.move.glyphLines[0].left++;
            if(defect==4)f.input.move.destination.bottom=355;
            if(defect==5)f.input.move.destination.left=3;
            if(defect==6)f.input.backgroundMode=2;
            if(defect==7)f.input.move.source.bottom=345;
            if(defect==8)f.input.move.pictureTop++;
            if(defect==9)f.input.acceptedCue=nullptr;
            const auto r=f.Select();Assert::IsFalse(r.available);Assert::IsFalse(r.composed);
        }
    }
    TEST_METHOD(StaleOrUnprovedObservationCannotCropEvenProtected) {
        for(int defect=0;defect<9;++defect) {
            Fixture f;f.input.compositionSucceeded=false;
            if(defect==0)f.observation.identity.acceptedSequence++;
            if(defect==1)f.observation.assistedSourceCandidate=false;
            if(defect==2)f.observation.assistedBounds.top++;
            if(defect==3)f.observation.text.held=true;
            if(defect==4)f.observation.analysisRefresh=true;
            if(defect==5)f.observation.pendingRefresh=true;
            if(defect==6)f.input.proof.inhibited=true;
            if(defect==7)f.input.proof.policyGeneration++;
            if(defect==8)f.observation.ink.reset();
            Assert::IsFalse(f.Select().available);
        }
    }
    TEST_METHOD(GeometryJitterDoesNotClearAcquisitionInhibition) {
        SubtitleAssistedAcquisitionGate gate;SubtitleAssistedAcquisitionInput i;
        i.sourceGeneration=1;i.candidate={0,28,640,332,640,360};i.protectedBounds={0,24,640,354,640,360};i.sourceProofAccepted=true;
        for(uint64_t n=1;n<=4;++n){i.identity={1,n,n,n*1000,2,3,4};i.proofIdentity=i.identity;gate.Evaluate(i);}
        i.sourceProofAccepted=false;i.identity.acceptedSequence=i.identity.sourceFrameNumber=5;i.proofIdentity=i.identity;
        Assert::IsTrue(gate.Evaluate(i).inhibited);i.sourceProofAccepted=true;
        for(uint64_t n=6;n<=12;++n){i.identity.acceptedSequence=i.identity.sourceFrameNumber=n;i.proofIdentity=i.identity;i.candidate.top=26+int(n%2);Assert::IsTrue(gate.Evaluate(i).inhibited);}
    }
};
}
namespace Tests {
TEST_CLASS(SubtitleAssistedRevalidationTests) {
public:
    TEST_METHOD(ReadOnlyCandidateRequiresSameContextNewFrameAndDoesNotRearm) {
        SubtitleAssistedAcquisitionGate gate;SubtitleAssistedAcquisitionInput i;
        i.sourceGeneration=1;i.policyGeneration=5;i.continuityGeneration=6;
        i.candidate={0,28,640,332,640,360};i.protectedBounds={0,28,640,354,640,360};i.sourceProofAccepted=true;
        ActivePictureBounds out;
        for(uint64_t n=1;n<=4;++n){i.identity={1,n,n,n*1000,2,3,4};i.proofIdentity=i.identity;gate.Evaluate(i);}
        Assert::IsFalse(gate.CandidateForRevalidation(i.identity,5,6,out));
        i.identity={1,5,5,5000,2,3,4};i.proofIdentity=i.identity;i.sourceProofAccepted=false;
        Assert::IsTrue(gate.Evaluate(i).inhibited);
        auto next=i.identity;++next.acceptedSequence;++next.sourceFrameNumber;next.captureTimestamp+=1000;
        Assert::IsTrue(gate.CandidateForRevalidation(next,5,6,out));Assert::AreEqual(28,out.top);
        Assert::IsTrue(gate.CandidateForRevalidation(next,5,6,out));
        Assert::IsFalse(gate.CandidateForRevalidation(next,7,6,out));
        auto foreign=next;++foreign.transportGeneration;Assert::IsFalse(gate.CandidateForRevalidation(foreign,5,6,out));
        i.identity=next;i.proofIdentity=next;i.sourceProofAccepted=true;
        Assert::IsTrue(gate.Evaluate(i).inhibited);
        gate.Reset();Assert::IsFalse(gate.CandidateForRevalidation(next,5,6,out));
    }
};
}
#include <vprenderer/AlphaSourceCropPolicy.h>
namespace Tests {
TEST_CLASS(SubtitleAssistedStartupEligibilityTests) {
    static NearBlackPresentationEpisodeInput Blank(bool trusted=false) {
        NearBlackPresentationEpisodeInput e;e.sourceGeneration=1;e.sourceSequence=1;e.presentationEpoch=3;
        e.measurementCurrent=e.nearBlackEvaluated=e.globalNearBlack=true;
        e.trustedCropAvailable=trusted;e.boundedVisibleContentOutsideCrop=trusted;
        if(trusted)e.trustedCrop={0,28,640,332,640,360,640.0/304,ActivePictureBounds::BarAxes::TOP_BOTTOM};
        return e;
    }
    static SubtitleAssistedStartupPresentationInput Bright(uint64_t n=2) {
        SubtitleAssistedStartupPresentationInput i;i.identity={1,n,n,n*1000,2,3,4};i.measurementIdentity=i.identity;
        i.policyGeneration=5;i.continuityGeneration=6;i.measurementAvailable=i.nearBlackEvaluated=true;return i;
    }
    static SubtitleAssistedAcquisitionDecision Proof(const SubtitleAssistedStartupPresentationInput& i) {
        SubtitleAssistedAcquisitionDecision p;p.confirmed=true;p.matchingFrames=4;p.identity=i.identity;
        p.policyGeneration=i.policyGeneration;p.continuityGeneration=i.continuityGeneration;return p;
    }
public:
    TEST_METHOD(RealBlankEpisodeAllowsCollectionButNoPresentationUntilFourProofs) {
        auto episodeInput=Blank();auto episode=EvaluateNearBlackPresentationEpisode(episodeInput).state;
        Assert::IsTrue(episode.startedWithoutTrustedCrop);SubtitleAssistedAcquisitionGate gate;
        for(uint64_t n=2;n<=5;++n) {
            auto input=Bright(n);Assert::IsTrue(CanCollectSubtitleAssistedStartupEvidence(episode,input));
            SubtitleAssistedAcquisitionInput frame;frame.identity=frame.proofIdentity=input.identity;
            frame.sourceGeneration=1;frame.policyGeneration=5;frame.continuityGeneration=6;
            frame.candidate={0,28,640,332,640,360};frame.protectedBounds={0,28,640,354,640,360};frame.sourceProofAccepted=true;
            const auto proof=gate.Evaluate(frame);
            Assert::AreEqual(n==5,CanPresentSubtitleAssistedStartupEvidence(episode,input,proof));
            episodeInput.previous=episode;episodeInput.sourceSequence=n;episodeInput.globalNearBlack=false;
            episode=EvaluateNearBlackPresentationEpisode(episodeInput).state;
            Assert::IsTrue(episode.mode==NearBlackPresentationMode::FULL_RASTER);
            Assert::IsFalse(episode.entryTrustedCropAvailable);
        }
        auto input=Bright(6);SubtitleAssistedAcquisitionInput fail;fail.identity=fail.proofIdentity=input.identity;
        fail.sourceGeneration=1;fail.policyGeneration=5;fail.continuityGeneration=6;
        fail.candidate={0,28,640,332,640,360};fail.protectedBounds={0,28,640,354,640,360};
        const auto withdrawn=gate.Evaluate(fail);Assert::IsTrue(withdrawn.inhibited);
        Assert::IsFalse(CanPresentSubtitleAssistedStartupEvidence(episode,input,withdrawn));
        Assert::IsTrue(episode.mode==NearBlackPresentationMode::FULL_RASTER);
    }
    TEST_METHOD(EpochExpirationCannotManufactureColdOrigin) {
        for(bool trusted:{false,true}) {
            auto entry=Blank(trusted);auto episode=EvaluateNearBlackPresentationEpisode(entry).state;
            Assert::AreEqual(!trusted,episode.startedWithoutTrustedCrop);
            entry.previous=episode;entry.sourceSequence=2;entry.presentationEpoch=7;
            entry.trustedCropAvailable=false;entry.globalNearBlack=false;entry.boundedVisibleContentOutsideCrop=false;
            episode=EvaluateNearBlackPresentationEpisode(entry).state;
            Assert::IsFalse(episode.entryTrustedCropAvailable);Assert::AreEqual(!trusted,episode.startedWithoutTrustedCrop);
            auto input=Bright(3);input.identity.viewportGeneration=7;input.measurementIdentity=input.identity;
            Assert::AreEqual(!trusted,CanCollectSubtitleAssistedStartupEvidence(episode,input));
        }
    }
    TEST_METHOD(ActiveAuthorityDarknessRecoveryAndStaleContextAllBlock) {
        const auto original=EvaluateNearBlackPresentationEpisode(Blank()).state;
        for(int defect=0;defect<21;++defect) {
            auto episode=original;auto input=Bright();
            if(defect==0)input.globalNearBlack=true;
            if(defect==1)input.nearBlackEvaluated=false;
            if(defect==2)input.measurementAvailable=false;
            if(defect==3)input.measurementIdentity.acceptedSequence++;
            if(defect==4)input.identity.viewportGeneration++,input.measurementIdentity=input.identity;
            if(defect==5)input.identity.transportGeneration++,input.measurementIdentity=input.identity;
            if(defect==6)input.trustedCropAvailable=true;
            if(defect==7)input.fullRasterAuthorityAvailable=true;
            if(defect==8)input.knownFullRasterRetained=true;
            if(defect==9)input.cropAdmissionAvailable=true;
            if(defect==10)input.sourceDiscontinuity=true;
            if(defect==11)input.sceneTransition=true;
            if(defect==12)input.recoveryActive=true;
            if(defect==13)input.conflictingPresentationOwnership=true;
            if(defect==14)episode.boundedPresentationAvailable=true;
            if(defect==15)episode.boundedPresentationFailed=true;
            if(defect==16)episode.confirmedNonNearBlackContent=true;
            if(defect==17)episode.outwardConfirmationSamples=1;
            if(defect==18)episode.entryTrustedCropAvailable=true;
            if(defect==19)episode.startedWithoutTrustedCrop=false;
            if(defect==20)episode.mode=NearBlackPresentationMode::RETAIN_CROP;
            Assert::IsFalse(CanCollectSubtitleAssistedStartupEvidence(episode,input));
            Assert::IsFalse(CanPresentSubtitleAssistedStartupEvidence(episode,input,Proof(input)));
        }
    }
    TEST_METHOD(FinalOverlayRequiresExactCurrentConfirmedProof) {
        const auto episode=EvaluateNearBlackPresentationEpisode(Blank()).state;const auto input=Bright();
        for(int defect=0;defect<10;++defect) {
            auto proof=Proof(input);
            if(defect==0)proof.confirmed=false;
            if(defect==1)proof.inhibited=true;
            if(defect==2)proof.matchingFrames=3;
            if(defect==3)proof.identity.acceptedSequence++;
            if(defect==4)proof.identity.captureTimestamp++;
            if(defect==5)proof.identity.sourceFormatGeneration++;
            if(defect==6)proof.identity.rendererGeneration++;
            if(defect==7)proof.identity.viewportGeneration++;
            if(defect==8)proof.policyGeneration++;
            if(defect==9)proof.continuityGeneration++;
            Assert::IsFalse(CanPresentSubtitleAssistedStartupEvidence(episode,input,proof));
        }
    }
    TEST_METHOD(NativeFullFrameAtEpisodeEntryCannotBecomeCold) {
        auto input=Blank();input.fullRasterAuthorityAvailable=true;
        auto episode=EvaluateNearBlackPresentationEpisode(input).state;
        Assert::IsFalse(episode.startedWithoutTrustedCrop);
        input.fullRasterAuthorityAvailable=false;input.knownFullRasterRetained=true;
        episode=EvaluateNearBlackPresentationEpisode(input).state;
        Assert::IsFalse(episode.startedWithoutTrustedCrop);
    }
};
}


namespace Tests {
TEST_CLASS(SubtitleAssistedReacquisitionEligibilityTests) {
    struct Fixture {
        SubtitleAssistedReacquisitionInput input;
        AlphaSourceCrop::Input native;
        Decision candidate;
        Fixture(uint64_t sequence=4) {
            input.featureEnabled=true;input.identity={1,sequence,sequence,sequence*1000,2,3,4};
            input.measurementIdentity=input.identity;input.sourceGeneration=1;
            input.policyGeneration=5;input.continuityGeneration=6;
            input.measurementAvailable=input.nearBlackEvaluated=true;
            input.nativePresentationFullRaster=input.automaticCropEnabled=true;
            native.automaticCropEnabled=true;native.frameSourceGeneration=1;native.frameSourceSequence=sequence;
            native.rasterWidth=640;native.rasterHeight=360;
            native.geometry={0,12,640,348,640,360,640.0/336,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            candidate.applyCrop=true;candidate.owner=DecisionOwner::PIXEL_SAFE_RETENTION;
            candidate.sourceBounds=native.geometry;
        }
        void SetUnadmittedVisibleConflict() {
            native.geometry={0,28,640,332,640,360,640.0/304,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            candidate.sourceBounds=native.geometry;input.nativeGeometry=native.geometry;
            native.currentVisibleBoundsAvailable=true;
            native.currentVisibleSourceGeneration=input.identity.transportGeneration;
            native.currentVisibleSourceSequence=input.identity.acceptedSequence;
            native.currentVisibleBase=native.geometry;
            native.currentVisibleBounds={0,28,640,354,640,360};
            input.currentVisibleConflict=true;input.currentClassification=ActivePictureClassification::PROVISIONAL;
        }
        void SetClearance(const ActivePictureBounds& bounds) {
            auto& c=input.candidateClearance;c.wholeNativeBandsVerified=true;c.identity=input.identity;
            c.sourceGeneration=input.sourceGeneration;c.policyGeneration=input.policyGeneration;
            c.continuityGeneration=input.continuityGeneration;c.candidate=bounds;
        }
        CropPresentationAdmissionDecision Admission() const {
            return AdmitCropPresentation(input.priorAdmission,native,candidate,input.identity.viewportGeneration);
        }
        SubtitleAssistedAcquisitionInput Frame() const {
            SubtitleAssistedAcquisitionInput f;f.identity=f.proofIdentity=input.identity;
            f.sourceGeneration=input.sourceGeneration;f.policyGeneration=input.policyGeneration;
            f.continuityGeneration=input.continuityGeneration;f.sourceProofAccepted=true;
            f.candidate={0,28,640,332,640,360};f.protectedBounds={0,28,640,354,640,360};return f;
        }
        SubtitleAssistedAcquisitionDecision Proof() const {
            SubtitleAssistedAcquisitionGate gate;SubtitleAssistedAcquisitionDecision proof;
            for(uint64_t n=1;n<=4;++n){auto f=Frame();f.identity.acceptedSequence=f.identity.sourceFrameNumber=n;
                f.identity.captureTimestamp=n*1000;f.proofIdentity=f.identity;proof=gate.Evaluate(f);}
            return proof;
        }
    };
public:
    TEST_METHOD(DefaultOffAndRetainedLogicalAspectCannotGrantPermission) {
        Fixture f;SubtitleAssistedReacquisitionInput defaults;
        Assert::IsFalse(defaults.featureEnabled);
        f.input.featureEnabled=false;
        Assert::IsFalse(CanCollectSubtitleAssistedReacquisitionEvidence(f.input,f.Admission()));
        f.input.featureEnabled=true;
        for(int top:{12,28,40}) {
            f.native.geometry.top=top;f.native.geometry.bottom=360-top;f.candidate.sourceBounds=f.native.geometry;
            const auto admission=f.Admission();
            Assert::IsTrue(admission.blocked);
            Assert::IsTrue(admission.blockReason==CropPresentationAdmissionBlockReason::MISSING_ACQUISITION_AUTHORITY);
            Assert::IsFalse(admission.state.available);
            Assert::IsTrue(CanCollectSubtitleAssistedReacquisitionEvidence(f.input,admission));
            Assert::IsTrue(CanPresentSubtitleAssistedReacquisitionEvidence(f.input,admission,f.Proof(),true));
            Assert::IsFalse(f.input.priorAdmission.available);
            Assert::AreEqual(top,f.native.geometry.top);
        }
    }
    TEST_METHOD(PreviouslyAdmittedScopeCannotUseReacquisition) {
        Fixture f;f.input.priorAdmission.available=true;f.input.priorAdmission.sourceGeneration=1;
        f.input.priorAdmission.presentationEpoch=3;f.input.priorAdmission.trustedCrop=f.native.geometry;
        Assert::IsFalse(CanCollectSubtitleAssistedReacquisitionEvidence(f.input,f.Admission()));
        // A different saved logical crop is still admitted ownership in this context.
        f.input.priorAdmission.trustedCrop.top=28;
        Assert::IsFalse(CanCollectSubtitleAssistedReacquisitionEvidence(f.input,f.Admission()));
        // Old-context records are not current authority; they still supply no proof.
        f.input.priorAdmission.sourceGeneration=9;
        Assert::IsTrue(CanCollectSubtitleAssistedReacquisitionEvidence(f.input,f.Admission()));
        Assert::IsFalse(CanPresentSubtitleAssistedReacquisitionEvidence(f.input,f.Admission(),{},true));
    }
    TEST_METHOD(EveryCurrentContextAndPresentationVetoBlocksCollection) {
        for(int defect=0;defect<32;++defect) {
            Fixture f;auto admission=f.Admission();
            if(defect==0)f.input.featureEnabled=false;
            if(defect==1)f.input.automaticCropEnabled=false;
            if(defect==2)f.input.fixedCrop=true;
            if(defect==3)f.input.nls=true;
            if(defect==4)f.input.sourceGeneration++;
            if(defect==5)f.input.measurementAvailable=false;
            if(defect==6)f.input.nearBlackEvaluated=false;
            if(defect==7)f.input.globalNearBlack=true;
            if(defect==8)f.input.nearBlackEpisodeActive=true;
            if(defect==9)f.input.nativePresentationFullRaster=false;
            if(defect==10)f.input.fullRasterAuthorityAvailable=true;
            if(defect==11)f.input.sourceDiscontinuity=true;
            if(defect==12)f.input.sceneTransition=true;
            if(defect==13)f.input.recoveryActive=true;
            if(defect==14)f.input.presentationFailOpen=true;
            if(defect==15)f.input.conflictingPresentationOwnership=true;
            if(defect==16)f.input.measurementIdentity.acceptedSequence++;
            if(defect==17)f.input.measurementIdentity.transportGeneration++;
            if(defect==18)f.input.measurementIdentity.sourceFormatGeneration++;
            if(defect==19)f.input.measurementIdentity.viewportGeneration++;
            if(defect==20)f.input.measurementIdentity.rendererGeneration++;
            if(defect==21)f.input.measurementIdentity.captureTimestamp++;
            if(defect==22)admission.blocked=false;
            if(defect==23)admission.blockReason=CropPresentationAdmissionBlockReason::CURRENT_VISIBLE_PIXELS;
            if(defect==24)admission.blockReason=CropPresentationAdmissionBlockReason::NONE;
            if(defect==25)admission.currentVisibleConflict=true;
            if(defect==26)admission.presentation.applyCrop=true;
            if(defect==27)admission.presentation.sourceBounds.top=1;
            if(defect==28)admission.state.available=true,admission.state.sourceGeneration=1,admission.state.presentationEpoch=3;
            if(defect==29)admission.sourceGeneration++;
            if(defect==30)admission.sourceSequence++;
            if(defect==31)admission.presentationEpoch++;
            Assert::IsFalse(CanCollectSubtitleAssistedReacquisitionEvidence(f.input,admission));
            Assert::IsFalse(CanPresentSubtitleAssistedReacquisitionEvidence(f.input,admission,f.Proof(),true));
        }
    }
    TEST_METHOD(MissingAcquisitionCannotHideSimultaneousVisiblePixelVeto) {
        Fixture f;f.native.currentVisibleBoundsAvailable=true;
        f.native.currentVisibleSourceGeneration=1;f.native.currentVisibleSourceSequence=4;
        f.native.currentVisibleBase=f.native.geometry;f.native.currentVisibleBounds=f.native.geometry;
        f.candidate.sourceBounds.top=28;f.candidate.sourceBounds.bottom=332;
        auto admission=f.Admission();
        Assert::IsTrue(admission.currentVisibleConflict);
        Assert::IsTrue(admission.blockReason==CropPresentationAdmissionBlockReason::MISSING_ACQUISITION_AUTHORITY);
        Assert::IsFalse(CanCollectSubtitleAssistedReacquisitionEvidence(f.input,admission));
        f.native.latestObservationSupportsCrop=true;f.candidate.owner=DecisionOwner::TRUSTED_CROP;
        admission=f.Admission();
        Assert::IsTrue(admission.blockReason==CropPresentationAdmissionBlockReason::CURRENT_VISIBLE_PIXELS);
        Assert::IsFalse(CanCollectSubtitleAssistedReacquisitionEvidence(f.input,admission));
    }
    TEST_METHOD(FinalPresentationRequiresActualDenialCompositionAndExactProof) {
        for(int defect=0;defect<12;++defect) {
            Fixture f;auto proof=f.Proof();bool composed=true;
            if(defect==0)composed=false;
            if(defect==1)proof.confirmed=false;
            if(defect==2)proof.inhibited=true;
            if(defect==3)proof.matchingFrames=3;
            if(defect==4)proof.identity.acceptedSequence++;
            if(defect==5)proof.identity.captureTimestamp++;
            if(defect==6)proof.policyGeneration++;
            if(defect==7)proof.continuityGeneration++;
            if(defect==8)proof.candidate.rasterWidth++;
            if(defect==9)proof.candidate.trustedBarAxes=ActivePictureBounds::BarAxes::TOP_BOTTOM;
            if(defect==10)proof.candidate.top=0;
            if(defect==11)proof.identity.sourceFormatGeneration++;
            Assert::IsFalse(CanPresentSubtitleAssistedReacquisitionEvidence(f.input,f.Admission(),proof,composed));
        }
        Fixture f;const auto prediction=f.Admission();Assert::IsTrue(CanCollectSubtitleAssistedReacquisitionEvidence(f.input,prediction));
        f.native.latestObservationSupportsCrop=true;f.candidate.owner=DecisionOwner::TRUSTED_CROP;
        const auto actual=f.Admission();Assert::IsFalse(actual.blocked);
        Assert::IsFalse(CanPresentSubtitleAssistedReacquisitionEvidence(f.input,actual,f.Proof(),true));
    }
    TEST_METHOD(FourFreshFramesDropoutLatchAndProfileContextResetRemainRequired) {
        SubtitleAssistedAcquisitionGate gate;
        for(uint64_t n=1;n<=4;++n) {
            Fixture f(n);const auto proof=gate.Evaluate(f.Frame());
            Assert::AreEqual(n==4,CanPresentSubtitleAssistedReacquisitionEvidence(f.input,f.Admission(),proof,true));
        }
        Fixture failed(5);auto frame=failed.Frame();frame.sourceProofAccepted=false;
        Assert::IsTrue(gate.Evaluate(frame).inhibited);
        Fixture after(6);Assert::IsFalse(CanPresentSubtitleAssistedReacquisitionEvidence(after.input,after.Admission(),gate.Evaluate(after.Frame()),true));
        // Runtime changes must advance policyGeneration; no old proof crosses it.
        Fixture changed;auto old=changed.Proof();changed.input.policyGeneration++;
        Assert::IsFalse(CanPresentSubtitleAssistedReacquisitionEvidence(changed.input,changed.Admission(),old,true));
        for(uint64_t n=7;n<=10;++n) {
            Fixture fresh(n);fresh.input.policyGeneration++;
            const auto proof=gate.Evaluate(fresh.Frame());
            Assert::AreEqual(n==10,CanPresentSubtitleAssistedReacquisitionEvidence(fresh.input,fresh.Admission(),proof,true));
        }
    }

    TEST_METHOD(AdmittedAmbiguousRetentionAllowsOnlyFreshContainedComposedReplacement) {
        SubtitleAssistedAcquisitionGate gate;
        for(uint64_t n=1;n<=4;++n) {
            Fixture f(n);
            f.input.priorAdmission.available=true;f.input.priorAdmission.sourceGeneration=1;f.input.priorAdmission.presentationEpoch=3;
            f.input.priorAdmission.trustedCrop=f.input.nativeGeometry=f.input.retentionBounds=f.native.geometry;
            f.input.retentionIdentity=f.input.identity;f.input.retentionEvaluated=f.input.retentionPixelSafe=true;
            f.input.currentClassification=ActivePictureClassification::PROVISIONAL;f.input.nativePresentationFullRaster=false;
            const auto admission=f.Admission();const auto proof=gate.Evaluate(f.Frame());
            const auto eligibility=EvaluateSubtitleAssistedReacquisitionEligibility(f.input,admission);
            Assert::IsTrue(eligibility.allowed);Assert::IsTrue(eligibility.retainedCrop);
            Assert::IsFalse(admission.blocked);Assert::IsTrue(admission.presentation.applyCrop);
            Assert::AreEqual(n==4,CanPresentSubtitleAssistedReacquisitionEvidence(f.input,admission,proof,true));
            Assert::IsFalse(CanPresentSubtitleAssistedReacquisitionEvidence(f.input,admission,proof,false));
            Assert::AreEqual(12,admission.state.trustedCrop.top);Assert::AreEqual(348,admission.state.trustedCrop.bottom);
            Assert::AreEqual(12,f.input.priorAdmission.trustedCrop.top);
            Assert::AreEqual(12,admission.presentation.sourceBounds.top);
            Assert::IsTrue(admission.presentation.owner==DecisionOwner::PIXEL_SAFE_RETENTION);
        }
    }
    TEST_METHOD(AdmittedReplacementCannotOverrideAffirmativeNativeOrForeignRetention) {
        for(int defect=0;defect<20;++defect) {
            Fixture f;f.input.priorAdmission.available=true;f.input.priorAdmission.sourceGeneration=1;f.input.priorAdmission.presentationEpoch=3;
            f.input.priorAdmission.trustedCrop=f.input.nativeGeometry=f.input.retentionBounds=f.native.geometry;
            f.input.retentionIdentity=f.input.identity;f.input.retentionEvaluated=f.input.retentionPixelSafe=true;
            f.input.currentClassification=ActivePictureClassification::PROVISIONAL;f.input.nativePresentationFullRaster=false;
            auto admission=f.Admission();
            if(defect==0)f.input.latestObservationSupportsCrop=true;
            if(defect==1)f.input.currentClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
            if(defect==2)f.input.currentClassification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            if(defect==3)f.input.retentionEvaluated=false;
            if(defect==4)f.input.retentionPixelSafe=false;
            if(defect==5)f.input.retentionIdentity.acceptedSequence++;
            if(defect==6)f.input.retentionIdentity.sourceFormatGeneration++;
            if(defect==7)f.input.retentionBounds.top++;
            if(defect==8)f.input.nativeGeometry.top++;
            if(defect==9)f.input.priorAdmission.presentationEpoch++;
            if(defect==10)admission.state.sourceGeneration++;
            if(defect==11)admission.presentation.owner=DecisionOwner::TRUSTED_CROP;
            if(defect==12)admission.presentation.owner=DecisionOwner::AMBIGUITY_HOLD;
            if(defect==13)admission.presentation.owner=DecisionOwner::OUTWARD_FIT;
            if(defect==14)admission.presentation.sourceBounds.top++;
            if(defect==15)admission.presentation.outwardExpanded=true;
            if(defect==16)admission.presentation.verticallyTranslated=true;
            if(defect==17)f.input.nativeGeometry.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
            if(defect==18)f.input.currentVisibleConflict=true;
            if(defect==19)f.input.retentionIdentity.viewportGeneration++;
            Assert::IsFalse(CanCollectSubtitleAssistedReacquisitionEvidence(f.input,admission));
            Assert::IsFalse(CanPresentSubtitleAssistedReacquisitionEvidence(f.input,admission,f.Proof(),true));
        }
    }
    TEST_METHOD(AdmittedReplacementRejectsUnchangedAndExpandedCandidate) {
        Fixture f;f.input.priorAdmission.available=true;f.input.priorAdmission.sourceGeneration=1;f.input.priorAdmission.presentationEpoch=3;
        f.input.priorAdmission.trustedCrop=f.input.nativeGeometry=f.input.retentionBounds=f.native.geometry;
        f.input.retentionIdentity=f.input.identity;f.input.retentionEvaluated=f.input.retentionPixelSafe=true;
        f.input.currentClassification=ActivePictureClassification::PROVISIONAL;f.input.nativePresentationFullRaster=false;
        const auto admission=f.Admission();
        for(int defect=0;defect<4;++defect) {
            auto proof=f.Proof();
            if(defect==0)proof.candidate.top=12,proof.candidate.bottom=348;
            if(defect==1)proof.candidate.top=10;
            if(defect==2)proof.candidate.bottom=350;
            if(defect==3)proof.candidate.left=2;
            Assert::IsFalse(CanPresentSubtitleAssistedReacquisitionEvidence(f.input,admission,proof,true));
        }
    }
    TEST_METHOD(UnadmittedVisibleConflictCollectsOnlyCandidateSpecificEvidence) {
        Fixture f;f.SetUnadmittedVisibleConflict();const auto admission=f.Admission();
        Assert::IsTrue(admission.blocked);Assert::IsTrue(admission.currentVisibleConflict);
        Assert::IsTrue(admission.blockReason==CropPresentationAdmissionBlockReason::MISSING_ACQUISITION_AUTHORITY);
        const auto eligibility=EvaluateSubtitleAssistedReacquisitionEligibility(f.input,admission);
        Assert::IsTrue(eligibility.allowed);Assert::IsTrue(eligibility.candidateRelativeConflict);
        Assert::IsFalse(eligibility.retainedCrop);
        // Scheduling cannot clear a visible witness or supply display proof.
        Assert::IsFalse(CanPresentSubtitleAssistedReacquisitionEvidence(f.input,admission,f.Proof(),true));
        Assert::IsFalse(HasCurrentSubtitleAssistedCandidateClearance(f.input,f.Proof().candidate));
        Assert::IsFalse(admission.state.available);Assert::IsTrue(admission.currentVisibleConflict);
    }
    TEST_METHOD(UnadmittedOutwardCandidatePreservesNativePictureAndRequiresFourComposedProofs) {
        SubtitleAssistedAcquisitionGate gate;
        for(uint64_t n=1;n<=4;++n) {
            Fixture f(n);f.SetUnadmittedVisibleConflict();auto frame=f.Frame();
            frame.candidate.top=26;frame.candidate.bottom=334;frame.protectedBounds.top=26;
            f.SetClearance(frame.candidate);const auto admission=f.Admission();const auto proof=gate.Evaluate(frame);
            Assert::AreEqual(n==4,CanPresentSubtitleAssistedReacquisitionEvidence(f.input,admission,proof,true));
            Assert::IsFalse(CanPresentSubtitleAssistedReacquisitionEvidence(f.input,admission,proof,false));
            Assert::IsFalse(admission.state.available);Assert::IsTrue(admission.blocked);
            Assert::IsTrue(admission.currentVisibleConflict);Assert::IsFalse(admission.presentation.applyCrop);
            Assert::AreEqual(28,f.input.nativeGeometry.top);Assert::AreEqual(332,f.input.nativeGeometry.bottom);
        }
        Fixture missing(5);missing.SetUnadmittedVisibleConflict();auto lost=missing.Frame();
        lost.candidate.top=26;lost.candidate.bottom=334;lost.protectedBounds.top=26;lost.sourceProofAccepted=false;
        Assert::IsTrue(gate.Evaluate(lost).inhibited);
        Fixture after(6);after.SetUnadmittedVisibleConflict();auto frame=after.Frame();
        frame.candidate.top=26;frame.candidate.bottom=334;frame.protectedBounds.top=26;after.SetClearance(frame.candidate);
        Assert::IsFalse(CanPresentSubtitleAssistedReacquisitionEvidence(after.input,after.Admission(),gate.Evaluate(frame),true));
    }
    TEST_METHOD(CandidateClearanceCannotCrossIdentityContextGeometryOrMissingAudit) {
        for(int defect=0;defect<16;++defect) {
            Fixture f;f.SetUnadmittedVisibleConflict();auto proof=f.Proof();f.SetClearance(proof.candidate);
            auto& c=f.input.candidateClearance;
            if(defect==0)c.wholeNativeBandsVerified=false;
            if(defect==1)c.sourceGeneration++;
            if(defect==2)c.identity.acceptedSequence++;
            if(defect==3)c.identity.transportGeneration++;
            if(defect==4)c.identity.sourceFormatGeneration++;
            if(defect==5)c.identity.viewportGeneration++;
            if(defect==6)c.identity.rendererGeneration++;
            if(defect==7)c.identity.captureTimestamp++;
            if(defect==8)c.identity.sourceFrameNumber++;
            if(defect==9)c.policyGeneration++;
            if(defect==10)c.continuityGeneration++;
            if(defect==11)c.candidate.top--;
            if(defect==12)c.candidate.bottom++;
            if(defect==13)c.candidate.rasterWidth++;
            if(defect==14)c.candidate.trustedBarAxes=ActivePictureBounds::BarAxes::TOP_BOTTOM;
            if(defect==15)proof.candidate.top--;
            Assert::IsFalse(HasCurrentSubtitleAssistedCandidateClearance(f.input,proof.candidate));
            Assert::IsFalse(CanPresentSubtitleAssistedReacquisitionEvidence(f.input,f.Admission(),proof,true));
        }
    }
    TEST_METHOD(VisibleConflictAlternativeCannotContractKnownNativePicture) {
        for(int defect=0;defect<5;++defect) {
            Fixture f;f.SetUnadmittedVisibleConflict();auto proof=f.Proof();
            if(defect==0)proof.candidate.top++;
            if(defect==1)proof.candidate.bottom--;
            if(defect==2)proof.candidate.left++;
            if(defect==3)proof.candidate.right--;
            if(defect==4)proof.candidate.rasterHeight++;
            f.SetClearance(proof.candidate);
            Assert::IsFalse(CanPresentSubtitleAssistedReacquisitionEvidence(f.input,f.Admission(),proof,true));
        }
        Fixture f;f.SetUnadmittedVisibleConflict();const auto proof=f.Proof();f.SetClearance(proof.candidate);
        Assert::IsTrue(CanPresentSubtitleAssistedReacquisitionEvidence(f.input,f.Admission(),proof,true));
    }
    TEST_METHOD(CandidateRelativeCollectionKeepsAllIndependentOwnershipVetoes) {
        for(int defect=0;defect<20;++defect) {
            Fixture f;f.SetUnadmittedVisibleConflict();auto admission=f.Admission();auto proof=f.Proof();f.SetClearance(proof.candidate);
            if(defect==0)f.input.featureEnabled=false;
            if(defect==1)f.input.fixedCrop=true;
            if(defect==2)f.input.nls=true;
            if(defect==3)f.input.sceneTransition=true;
            if(defect==4)f.input.recoveryActive=true;
            if(defect==5)f.input.presentationFailOpen=true;
            if(defect==6)f.input.conflictingPresentationOwnership=true;
            if(defect==7)f.input.globalNearBlack=true;
            if(defect==8)f.input.nearBlackEpisodeActive=true;
            if(defect==9)f.input.sourceDiscontinuity=true;
            if(defect==10)f.input.fullRasterAuthorityAvailable=true;
            if(defect==11)f.input.latestObservationSupportsCrop=true;
            if(defect==12)f.input.currentClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
            if(defect==13)f.input.currentClassification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            if(defect==14)f.input.nativeGeometry.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
            if(defect==15)admission.blockReason=CropPresentationAdmissionBlockReason::CURRENT_VISIBLE_PIXELS;
            if(defect==16)admission.blocked=false;
            if(defect==17)f.input.measurementIdentity.acceptedSequence++;
            if(defect==18)f.input.priorAdmission.available=true,f.input.priorAdmission.sourceGeneration=1,f.input.priorAdmission.presentationEpoch=3;
            if(defect==19)admission.state.available=true,admission.state.sourceGeneration=1,admission.state.presentationEpoch=3;
            Assert::IsFalse(CanCollectSubtitleAssistedReacquisitionEvidence(f.input,admission));
            Assert::IsFalse(CanPresentSubtitleAssistedReacquisitionEvidence(f.input,admission,proof,true));
        }
    }
};
}
