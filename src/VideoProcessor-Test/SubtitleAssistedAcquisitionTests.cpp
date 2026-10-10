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
