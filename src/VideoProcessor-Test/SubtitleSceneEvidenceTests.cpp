#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleSceneEvidence.h>
#include <vector>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests {
TEST_CLASS(SubtitleSceneEvidenceTests) {
    static constexpr int Width=320, Height=180;
    static ActivePictureFrameIdentity Identity(uint64_t sequence) {
        return {1,sequence,sequence,sequence*1000,2,3,4};
    }
    struct Frame {
        std::vector<uint16_t> pixels;
        Frame(unsigned level=64):pixels(Width*Height*3/2,uint16_t(512<<6)) { SetLevel(level); }
        void SetLevel(unsigned level) { std::fill(pixels.begin(),pixels.begin()+Width*Height,uint16_t(level<<6)); }
        AnalysisLumaSource Source() const {
            AnalysisLumaSource source;
            source.data=reinterpret_cast<const uint8_t*>(pixels.data());source.dataBytes=pixels.size()*2;
            source.width=Width;source.height=Height;source.rowBytes=Width*2;source.chromaRowBytes=Width*2;source.generation=1;
            return source;
        }
    };
    static SubtitleBoxObservation Observation(uint64_t sequence) {
        SubtitleBoxObservation o;o.identity=Identity(sequence);o.analyzed=true;
        o.width=Width;o.height=Height;o.policyGeneration=10;o.continuityGeneration=20;
        return o;
    }
    static SubtitleBoxObservation Caption(Frame& frame,uint64_t sequence,int firstRow=16,int lastRow=18) {
        auto o=Observation(sequence);o.text.detected=true;o.text.lineCount=1;
        o.text.bounds={0,firstRow*10,Width,lastRow*10};
        auto ink=std::make_shared<SubtitleInkSnapshot>();
        ink->rawEvidenceComplete=true;ink->width=Width;ink->height=Height;ink->step=1;
        ink->sourceRows.resize(Height);for(int y=0;y<Height;++y)ink->sourceRows[y]=y;
        ink->rawInk.resize((Width*Height+63)/64);ink->ownedInk.resize(ink->rawInk.size());
        for(int row=firstRow;row<lastRow;++row)for(int col=0;col<32;++col) {
            const int x=col*10+5,y=row*10+5;
            frame.pixels[y*Width+x]=uint16_t(900<<6);
            const size_t index=size_t(y)*Width+x;
            ink->rawInk[index/64]|=uint64_t{1}<<(index%64);
            ink->ownedInk[index/64]|=uint64_t{1}<<(index%64);
        }
        o.ink=ink;return o;
    }
    static SubtitleSceneEvidenceResult Analyze(SubtitleSceneEvidenceDiagnostics& helper,
        const Frame& frame,const SubtitleBoxObservation& observation,bool accepted=true) {
        return helper.Analyze(frame.Source(),observation.identity,&observation,accepted);
    }
public:
    TEST_METHOD(AssistedDetectionRegionCannotSupplySceneMask) {
        Frame frame;auto cue=Caption(frame,1);cue.assistedSourceCandidate=true;
        SubtitleSceneEvidenceDiagnostics helper;
        const auto first=Analyze(helper,frame,cue);
        Assert::IsFalse(first.currentMaskAccepted);
        cue.identity=Identity(2);
        const auto second=Analyze(helper,frame,cue);
        Assert::IsFalse(second.excludedComparisonAvailable);
    }
    TEST_METHOD(SubtitleAppearanceAndDisappearanceUseSameUnionPixels) {
        SubtitleSceneEvidenceDiagnostics helper;Frame blank,caption;
        auto first=Observation(1);Assert::IsFalse(Analyze(helper,blank,first,false).evaluated);
        auto text=Caption(caption,2);const auto appeared=Analyze(helper,caption,text);
        Assert::IsTrue(appeared.evaluated);Assert::IsTrue(appeared.excludedComparisonAvailable);
        Assert::AreEqual(uint32_t(64),appeared.rawChangedSamples);
        Assert::AreEqual(uint32_t(64),appeared.excludedSamples);
        Assert::AreEqual(uint32_t(512),appeared.retainedSamples);
        Assert::AreEqual(uint32_t(0),appeared.withoutSubtitleAverageDifference);
        Assert::AreEqual(uint32_t(0),appeared.withoutSubtitleChangedSamples);
        auto gone=Observation(3);const auto disappeared=Analyze(helper,blank,gone,false);
        Assert::IsTrue(disappeared.excludedComparisonAvailable);
        Assert::AreEqual(uint32_t(64),disappeared.rawChangedSamples);
        Assert::AreEqual(uint32_t(64),disappeared.excludedSamples);
        Assert::AreEqual(uint32_t(0),disappeared.withoutSubtitleChangedSamples);
    }
    TEST_METHOD(RealPictureCutSurvivesSubtitleExclusion) {
        SubtitleSceneEvidenceDiagnostics helper;Frame before(64),after(512);
        auto a=Caption(before,1),b=Caption(after,2);Analyze(helper,before,a);
        const auto cut=Analyze(helper,after,b);
        Assert::IsTrue(cut.excludedComparisonAvailable);
        Assert::AreEqual(uint32_t(448),cut.withoutSubtitleAverageDifference);
        Assert::AreEqual(uint32_t(512),cut.withoutSubtitleChangedSamples);
    }
    TEST_METHOD(RejectsHeldPendingStaleFailedAndUnacceptedMeasurements) {
        for(int invalid=0;invalid<10;++invalid) {
            SubtitleSceneEvidenceDiagnostics helper;Frame blank,caption;
            auto first=Observation(1);Analyze(helper,blank,first,false);
            auto text=Caption(caption,2);bool accepted=true;
            if(invalid==0)text.text.held=true;
            if(invalid==1)text.pendingRefresh=true;
            if(invalid==2)text.analysisRefresh=true;
            if(invalid==3)text.text.workLimit=true;
            if(invalid==4)text.analyzed=false;
            if(invalid==5)text.discontinuity=true;
            if(invalid==6)accepted=false;
            if(invalid==7)text.ink.reset();
            if(invalid==8)text.identity.acceptedSequence=1;
            if(invalid==9)text.width=Width-1;
            const auto result=helper.Analyze(caption.Source(),Identity(2),&text,accepted);
            Assert::IsTrue(result.evaluated);Assert::IsFalse(result.currentMaskAccepted);
            Assert::IsFalse(result.excludedComparisonAvailable);
            Assert::AreEqual(uint32_t(64),result.rawChangedSamples);
            auto next=Observation(3);
            Assert::IsFalse(Analyze(helper,blank,next,false).excludedComparisonAvailable);
            auto fourth=Observation(4);
            Assert::IsTrue(Analyze(helper,blank,fourth,false).excludedComparisonAvailable);
        }
    }
    TEST_METHOD(MissingObservationNeverCarriesPreviousOwnedMask) {
        SubtitleSceneEvidenceDiagnostics helper;Frame caption,blank;
        auto text=Caption(caption,1);Analyze(helper,caption,text);
        const auto missing=helper.Analyze(blank.Source(),Identity(2),nullptr,false);
        Assert::IsTrue(missing.evaluated);Assert::IsFalse(missing.excludedComparisonAvailable);
        Assert::AreEqual(uint32_t(0),missing.excludedSamples);
        auto next=Observation(3);Assert::IsFalse(Analyze(helper,blank,next,false).excludedComparisonAvailable);
    }
    TEST_METHOD(OwnershipRequiresRawInkAndExactSampleCoordinates) {
        SubtitleSceneEvidenceDiagnostics helper;Frame blank,caption;
        auto first=Observation(1);Analyze(helper,blank,first,false);
        auto text=Caption(caption,2);auto changed=std::make_shared<SubtitleInkSnapshot>(*text.ink);
        std::fill(changed->rawInk.begin(),changed->rawInk.end(),0);text.ink=changed;
        const auto result=Analyze(helper,caption,text);
        Assert::IsTrue(result.excludedComparisonAvailable);
        Assert::AreEqual(uint32_t(0),result.excludedSamples);
        Assert::AreEqual(uint32_t(64),result.withoutSubtitleChangedSamples);
    }
    TEST_METHOD(ExcessiveSingleAndUnionMasksAbstain) {
        SubtitleSceneEvidenceDiagnostics helper;Frame blank,broad;
        auto first=Observation(1);Analyze(helper,blank,first,false);
        auto tooWide=Caption(broad,2,0,9);const auto rejected=Analyze(helper,broad,tooWide);
        Assert::IsFalse(rejected.currentMaskAccepted);Assert::IsFalse(rejected.excludedComparisonAvailable);
        auto next=Observation(3);Assert::IsFalse(Analyze(helper,blank,next,false).excludedComparisonAvailable);
        helper.Reset();Frame lower,upper;
        auto a=Caption(lower,1,14,18),b=Caption(upper,2,0,4);Analyze(helper,lower,a);
        const auto unionRejected=Analyze(helper,upper,b);
        Assert::IsTrue(unionRejected.currentMaskAccepted);
        Assert::AreEqual(uint32_t(256),unionRejected.excludedSamples);
        Assert::IsFalse(unionRejected.excludedComparisonAvailable);
    }
    TEST_METHOD(PolicyAndContinuityChangesInvalidateMaskPair) {
        for(int change=0;change<2;++change) {
            SubtitleSceneEvidenceDiagnostics helper;Frame frame;
            auto first=Observation(1);Analyze(helper,frame,first,false);
            auto next=Observation(2);if(change==0)++next.policyGeneration;else ++next.continuityGeneration;
            const auto result=Analyze(helper,frame,next,false);
            Assert::IsTrue(result.evaluated);Assert::IsFalse(result.excludedComparisonAvailable);
        }
    }
    TEST_METHOD(GenerationSequenceTimestampAndExplicitResetsWarmAgain) {
        for(int change=0;change<8;++change) {
            SubtitleSceneEvidenceDiagnostics helper;Frame frame;
            auto first=Observation(1);Analyze(helper,frame,first,false);
            auto next=Observation(2);
            if(change==0)++next.identity.transportGeneration;
            if(change==1)++next.identity.sourceFormatGeneration;
            if(change==2)++next.identity.viewportGeneration;
            if(change==3)++next.identity.rendererGeneration;
            if(change==4)next.identity.acceptedSequence=5;
            if(change==5)next.identity.captureTimestamp=500;
            if(change==6)next.identity.sourceFrameNumber=1;
            if(change==7)helper.Reset();
            Assert::IsFalse(Analyze(helper,frame,next,false).evaluated);
        }
        SubtitleSceneEvidenceDiagnostics helper;Frame frame;
        auto first=Observation(1),next=Observation(2);Analyze(helper,frame,first,false);
        Assert::IsFalse(helper.Analyze(frame.Source(),next.identity,&next,false,true).evaluated);
    }
    TEST_METHOD(RepeatedSequenceCannotReplaceBaselineOrUpgradeMask) {
        SubtitleSceneEvidenceDiagnostics helper;Frame blank,caption;
        auto first=Observation(1);Analyze(helper,blank,first,false);
        auto repeat=Caption(caption,1);
        Assert::IsFalse(Analyze(helper,caption,repeat).evaluated);
        auto next=Caption(caption,2);const auto result=Analyze(helper,caption,next);
        Assert::IsTrue(result.evaluated);Assert::AreEqual(uint32_t(64),result.rawChangedSamples);
        Assert::IsTrue(result.excludedComparisonAvailable);
    }
    TEST_METHOD(InvalidSourceBreaksPreviousComparison) {
        SubtitleSceneEvidenceDiagnostics helper;Frame frame;auto first=Observation(1);
        Analyze(helper,frame,first,false);
        Assert::IsFalse(helper.Analyze({},Identity(2),nullptr,false).evaluated);
        auto third=Observation(3);Assert::IsFalse(Analyze(helper,frame,third,false).evaluated);
    }
    TEST_METHOD(RawScoresMatchAuthoritativeImmediateDifferenceWithoutMutatingIt) {
        SceneDetector actual;SubtitleSceneEvidenceDiagnostics helper;Frame before(64),after(512);
        auto a=Observation(1),b=Observation(2);const auto sourceA=before.Source(),sourceB=after.Source();
        SceneDetectorInput input;input.enabled=true;input.width=Width;input.height=Height;
        input.generation=1;input.sourceSequence=1;input.analysisSource=&sourceA;
        actual.Analyze(input);Analyze(helper,before,a,false);
        input.sourceSequence=2;input.analysisSource=&sourceB;const auto authority=actual.Analyze(input);
        const auto diagnostics=Analyze(helper,after,b,false);
        Assert::AreEqual(authority.immediateAverageLumaDifference,diagnostics.rawAverageDifference);
        Assert::AreEqual(authority.changedSampleCount,diagnostics.rawChangedSamples);
        Assert::AreEqual(authority.sampleCount,diagnostics.sampleCount);
    }
};
}

#include <vprenderer/SubtitlePresentationContinuity.h>
namespace Tests {
TEST_CLASS(SubtitlePresentationContinuityTests) {
    static AlphaSourceCrop::SubtitlePresentationContinuityInput Input(uint64_t sequence,bool accepted=true) {
        AlphaSourceCrop::SubtitlePresentationContinuityInput input;
        input.identity={1,sequence,sequence,sequence*1000,2,3,4};
        input.policyGeneration=10;input.continuityGeneration=20;input.cue=30;
        input.logical={0,40,320,140,320,180,3.2,ActivePictureBounds::BarAxes::TOP_BOTTOM};
        input.certificateAccepted=accepted;return input;
    }
public:
    TEST_METHOD(CertificateLossWidensOnceAndRequiresIndependentRawClear) {
        AlphaSourceCrop::SubtitlePresentationContinuityGate gate;
        Assert::IsTrue(gate.Evaluate(Input(1)).allow);
        const auto loss=gate.Evaluate(Input(2,false));
        Assert::IsFalse(loss.allow);Assert::IsTrue(loss.inhibited);
        auto next=Input(3);++next.cue;
        Assert::IsFalse(gate.Evaluate(next).allow);
        next=Input(4,false);next.cue=0;
        Assert::IsTrue(gate.Evaluate(next).inhibited);
        Assert::IsFalse(gate.Evaluate(Input(5)).allow);
        auto clear=Input(6,false);clear.independentRawBandsClear=true;
        const auto cleared=gate.Evaluate(clear);
        Assert::IsFalse(cleared.allow);Assert::IsFalse(cleared.inhibited);
        Assert::IsTrue(gate.Evaluate(Input(7)).allow);
    }
    TEST_METHOD(RepeatedCompositionFailureImmediatelyWidensAndCannotRearm) {
        AlphaSourceCrop::SubtitlePresentationContinuityGate gate;
        Assert::IsTrue(gate.Evaluate(Input(1)).allow);
        Assert::IsTrue(gate.Evaluate(Input(1)).allow);
        Assert::IsTrue(gate.Evaluate(Input(1,false)).inhibited);
        auto repeat=Input(1);repeat.independentRawBandsClear=true;++repeat.cue;
        Assert::IsFalse(gate.Evaluate(repeat).allow);
        Assert::IsFalse(gate.Evaluate(Input(2)).allow);
        auto clear=Input(3,false);clear.independentRawBandsClear=true;gate.Evaluate(clear);
        Assert::IsTrue(gate.Evaluate(Input(4)).allow);
    }
    TEST_METHOD(InitialFailureDoesNotInhibitButSameSequenceCannotEngage) {
        AlphaSourceCrop::SubtitlePresentationContinuityGate gate;
        Assert::IsFalse(gate.Evaluate(Input(1,false)).inhibited);
        Assert::IsFalse(gate.Evaluate(Input(1)).allow);
        Assert::IsTrue(gate.Evaluate(Input(2)).allow);
    }
    TEST_METHOD(SourceGeometryPolicyAndExplicitDiscontinuityResetContext) {
        for(int change=0;change<11;++change) {
            AlphaSourceCrop::SubtitlePresentationContinuityGate gate;
            gate.Evaluate(Input(1));gate.Evaluate(Input(2,false));
            auto next=Input(3);
            if(change==0)++next.identity.transportGeneration;
            if(change==1)++next.identity.sourceFormatGeneration;
            if(change==2)++next.identity.viewportGeneration;
            if(change==3)++next.identity.rendererGeneration;
            if(change==4)++next.policyGeneration;
            if(change==5)++next.continuityGeneration;
            if(change==6)next.logical.top+=2;
            if(change==7)next.logical.bottom-=2;
            if(change==8)next.logical.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
            if(change==9)next.discontinuity=true;
            if(change==10)gate.Reset();
            const auto fresh=gate.Evaluate(next);
            Assert::IsTrue(fresh.allow);Assert::IsFalse(fresh.inhibited);
        }
    }
    TEST_METHOD(BackwardSeekResetsButForwardGapsDoNotRearm) {
        for(int seek=0;seek<3;++seek) {
            AlphaSourceCrop::SubtitlePresentationContinuityGate gate;
            gate.Evaluate(Input(10));gate.Evaluate(Input(11,false));
            Assert::IsFalse(gate.Evaluate(Input(20)).allow);
            auto next=Input(21);
            if(seek==0)next.identity.acceptedSequence=1;
            if(seek==1)next.identity.captureTimestamp=1000;
            if(seek==2)next.identity.sourceFrameNumber=1;
            Assert::IsTrue(gate.Evaluate(next).allow);
        }
    }
    TEST_METHOD(InvalidInputWidensWithoutErasingInhibition) {
        AlphaSourceCrop::SubtitlePresentationContinuityGate gate;
        gate.Evaluate(Input(1));auto invalid=Input(2);invalid.logical={};
        Assert::IsTrue(gate.Evaluate(invalid).inhibited);
        Assert::IsFalse(gate.Evaluate(Input(3)).allow);
    }
    TEST_METHOD(RawClearWithoutCertificateNeverAuthorizesPresentation) {
        AlphaSourceCrop::SubtitlePresentationContinuityGate gate;
        gate.Evaluate(Input(1));gate.Evaluate(Input(2,false));
        auto clear=Input(3,false);clear.independentRawBandsClear=true;
        Assert::IsFalse(gate.Evaluate(clear).allow);
        Assert::IsFalse(gate.Evaluate(Input(4,false)).allow);
        Assert::IsTrue(gate.Evaluate(Input(5)).allow);
    }
    TEST_METHOD(CleanRawSameSequenceCannotClearExistingInhibition) {
        AlphaSourceCrop::SubtitlePresentationContinuityGate gate;
        gate.Evaluate(Input(1));gate.Evaluate(Input(2,false));
        auto same=Input(2,false);same.independentRawBandsClear=true;
        Assert::IsTrue(gate.Evaluate(same).inhibited);
        Assert::IsFalse(gate.Evaluate(Input(3)).allow);
    }
};
TEST_CLASS(SubtitleSceneGenerationTests) {
public:
    TEST_METHOD(SourceTransportGenerationMustMatchEvenWithExactObservationIdentity) {
        const int width=320,height=180;
        std::vector<uint16_t> pixels(width*height*3/2,uint16_t(512<<6));
        AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels.data());
        source.dataBytes=pixels.size()*2;source.width=width;source.height=height;
        source.rowBytes=width*2;source.chromaRowBytes=width*2;source.generation=7;
        SubtitleBoxObservation observed;observed.identity={7,1,1,1000,99,3,4};
        observed.width=width;observed.height=height;observed.analyzed=true;
        SubtitleSceneEvidenceDiagnostics helper;
        Assert::IsFalse(helper.Analyze(source,observed.identity,&observed,false).evaluated);
        observed.identity.acceptedSequence=2;observed.identity.sourceFrameNumber=2;observed.identity.captureTimestamp=2000;
        Assert::IsTrue(helper.Analyze(source,observed.identity,&observed,false).evaluated);
        source.generation=99;observed.identity.acceptedSequence=3;
        observed.identity.sourceFrameNumber=3;observed.identity.captureTimestamp=3000;
        Assert::IsFalse(helper.Analyze(source,observed.identity,&observed,false).evaluated);
        source.generation=7;observed.identity.acceptedSequence=4;
        observed.identity.sourceFrameNumber=4;observed.identity.captureTimestamp=4000;
        Assert::IsFalse(helper.Analyze(source,observed.identity,&observed,false).evaluated);
    }
};
}

namespace Tests {
TEST_CLASS(SubtitlePresentationContinuityWorkGateTests) {
    static AlphaSourceCrop::SubtitlePresentationContinuityInput Input(uint64_t sequence,bool accepted=true) {
        AlphaSourceCrop::SubtitlePresentationContinuityInput input;
        input.identity={1,sequence,sequence,sequence*1000,2,3,4};
        input.policyGeneration=10;input.continuityGeneration=20;
        input.logical={0,40,320,140,320,180,3.2,ActivePictureBounds::BarAxes::TOP_BOTTOM};
        input.certificateAccepted=accepted;return input;
    }
public:
    TEST_METHOD(InhibitedScanCanOnlyRearmWithFreshIndependentRawClear) {
        AlphaSourceCrop::SubtitlePresentationContinuityGate gate;
        Assert::IsTrue(gate.CanAttempt(Input(1)));gate.Evaluate(Input(1));
        Assert::IsTrue(gate.CanAttempt(Input(2,false)));gate.Evaluate(Input(2,false));
        Assert::IsFalse(gate.CanAttempt(Input(3)));
        auto repeat=Input(2);repeat.independentRawBandsClear=true;
        Assert::IsFalse(gate.CanAttempt(repeat));
        auto clean=Input(3);clean.independentRawBandsClear=true;
        Assert::IsTrue(gate.CanAttempt(clean));Assert::IsTrue(gate.Evaluate(clean).allow);
    }
    TEST_METHOD(ReadOnlyQueriesCannotRearmOrConsumeSequence) {
        AlphaSourceCrop::SubtitlePresentationContinuityGate gate;
        gate.Evaluate(Input(1));gate.Evaluate(Input(2,false));
        const auto& readOnly=gate;
        auto prospectiveClear=Input(3);prospectiveClear.independentRawBandsClear=true;
        Assert::IsTrue(readOnly.CanAttempt(prospectiveClear));
        Assert::IsTrue(readOnly.CanAttempt(prospectiveClear));
        // The query neither cleared the latch nor consumed sequence three.
        Assert::IsFalse(readOnly.CanAttempt(Input(3)));
        Assert::IsFalse(gate.Evaluate(Input(3)).allow);
        auto nextClear=Input(4);nextClear.independentRawBandsClear=true;
        Assert::IsTrue(readOnly.CanAttempt(nextClear));Assert::IsTrue(gate.Evaluate(nextClear).allow);
    }
    TEST_METHOD(RepeatedUnengagedFramesCannotBeginAuditButFreshSuccessMayContinue) {
        AlphaSourceCrop::SubtitlePresentationContinuityGate gate;
        gate.Evaluate(Input(1,false));
        Assert::IsFalse(gate.CanAttempt(Input(1)));
        Assert::IsTrue(gate.CanAttempt(Input(2)));gate.Evaluate(Input(2));
        Assert::IsTrue(gate.CanAttempt(Input(2)));
        Assert::IsTrue(gate.CanAttempt(Input(2,false)));
        gate.Evaluate(Input(2,false));
        Assert::IsFalse(gate.CanAttempt(Input(2)));
    }
    TEST_METHOD(ContextResetSeekAndValidityMatchEvaluationRules) {
        for(int change=0;change<9;++change) {
            AlphaSourceCrop::SubtitlePresentationContinuityGate gate;
            gate.Evaluate(Input(10));gate.Evaluate(Input(11,false));
            auto next=Input(12);
            if(change==0)++next.identity.transportGeneration;
            if(change==1)++next.identity.sourceFormatGeneration;
            if(change==2)++next.policyGeneration;
            if(change==3)++next.continuityGeneration;
            if(change==4)next.logical.bottom-=2;
            if(change==5)next.identity.acceptedSequence=1;
            if(change==6)next.identity.captureTimestamp=1000;
            if(change==7)next.identity.sourceFrameNumber=1;
            if(change==8)gate.Reset();
            Assert::IsTrue(gate.CanAttempt(next));Assert::IsTrue(gate.Evaluate(next).allow);
        }
        AlphaSourceCrop::SubtitlePresentationContinuityGate gate;
        auto invalid=Input(1);invalid.logical={};Assert::IsFalse(gate.CanAttempt(invalid));
        auto discontinuity=Input(1);discontinuity.discontinuity=true;
        Assert::IsFalse(gate.CanAttempt(discontinuity));
    }
};
}
