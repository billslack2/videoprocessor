#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleBoxLookahead.h>
#include <vector>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests {
TEST_CLASS(SubtitleBoxLookaheadTests) {
    static ActivePictureFrameIdentity Identity(uint64_t sequence) {
        return {1,sequence,sequence,sequence*1000,2,3,4};
    }
    static SubtitleBoxObservation Cue(uint64_t sequence, bool twoLines=false) {
        SubtitleBoxObservation o;
        o.identity=Identity(sequence); o.analyzed=o.barAuthority=true;
        o.width=640; o.height=360; o.pictureTop=45; o.pictureBottom=315;
        o.text.detected=true; o.text.lineCount=twoLines?2:1;
        o.text.bounds={196,314,444,twoLines?352:336};
        o.text.lineBounds[0]={200,318,440,332};
        o.text.lineSignatures[0].fill(0x55);
        if(twoLines) {
            o.text.lineBounds[1]={240,338,400,348};
            o.text.lineSignatures[1].fill(0x33);
        }
        return o;
    }
    static SubtitleBoxPreview Resolve(std::initializer_list<SubtitleBoxObservation> frames) {
        return SubtitleBoxLookahead::Resolve(frames.begin(),frames.size(),7,8);
    }
    static void Fill(std::vector<uint16_t>& f,int left,int top,int right,int bottom,int code) {
        for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) f[y*640+x]=uint16_t(code<<6);
    }
    static void Text(std::vector<uint16_t>& f,int left,int top,int count,bool changed=false) {
        for(int n=0;n<count;++n) {
            const int x=left+13*n;
            Fill(f,x-1,top-1,x+9,top+15,64);
            if(!changed) {
                Fill(f,x,top,x+8,top+14,700); Fill(f,x+2,top+2,x+6,top+12,64);
            } else {
                Fill(f,x,top,x+2,top+14,700); Fill(f,x+6,top,x+8,top+14,700);
                Fill(f,x+2,top+6,x+6,top+8,700);
            }
        }
    }
    static std::vector<uint16_t> Pixels(bool full=false) {
        std::vector<uint16_t> f(640*360*3/2,uint16_t(512<<6));
        Fill(f,0,0,640,360,64); Fill(f,0,full?0:45,640,full?360:315,300); return f;
    }
    static AnalysisLumaSource Source(std::vector<uint16_t>& pixels) {
        AnalysisLumaSource source;
        source.data=reinterpret_cast<const uint8_t*>(pixels.data());source.dataBytes=pixels.size()*2;
        source.width=640;source.height=360;source.rowBytes=source.chromaRowBytes=1280;
        source.format=AnalysisLumaFormat::P010;source.generation=2;
        return source;
    }
    static SubtitleBoxObservation Measure(std::vector<uint16_t>& pixels,uint64_t sequence) {
        const auto source=Source(pixels);
        SubtitleBoxDetector scanner;
        return SubtitleBoxLookahead::Measure(scanner,source,ExtractActivePictureEvidence(source),Identity(sequence),false);
    }
public:
    TEST_METHOD(FirstDisplayedFrameIncludesFutureCompanionWithoutChangingCurrentIdentity) {
        SubtitleBoxPresentation tracker;
        const auto preview=Resolve({Cue(1),Cue(2,true),Cue(3,true)});
        const auto result=tracker.Consume(preview);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::AreEqual(352,result.bounds.bottom);Assert::AreEqual(uint64_t(1),preview.current.identity.acceptedSequence);
        Assert::AreEqual(3u,preview.matchingFrames);
    }
    TEST_METHOD(BlankCurrentFrameCannotBorrowFutureCue) {
        auto blank=Cue(1);blank.text={};SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(Resolve({blank,Cue(2),Cue(3)})).detected);
    }
    TEST_METHOD(OneFrameFlashIsRejectedButLastFrameOfEstablishedCueRemains) {
        auto blank=Cue(2);blank.text={};SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(Resolve({Cue(1),blank})).detected);
        tracker.Reset();Assert::IsTrue(tracker.Consume(Resolve({Cue(1),Cue(2)})).detected);
        blank.identity=Identity(3);
        Assert::IsTrue(tracker.Consume(Resolve({Cue(2),blank})).detected);
        Assert::IsFalse(tracker.Consume(Resolve({blank,Cue(4)})).detected);
    }
    TEST_METHOD(ChangedUpperLineCannotBorrowUnchangedLowerLine) {
        auto first=Cue(1,true),next=Cue(2,true);
        next.text.lineSignatures[0].fill(0xAA);
        Assert::AreEqual(1u,Resolve({first,next}).matchingFrames);
        SubtitleBoxPresentation tracker;Assert::IsFalse(tracker.Consume(Resolve({first,next})).detected);
    }
    TEST_METHOD(AnchorOrderCanChangeWhenSecondLineAppears) {
        auto second=Cue(2,true);
        std::swap(second.text.lineBounds[0],second.text.lineBounds[1]);
        std::swap(second.text.lineSignatures[0],second.text.lineSignatures[1]);
        Assert::AreEqual(2u,Resolve({Cue(1),second}).matchingFrames);
    }
    TEST_METHOD(OneFutureCompanionCannotEnlargePersistentBarCue) {
        const auto transient=Resolve({Cue(1),Cue(2,true),Cue(3),Cue(4)});
        Assert::AreEqual(1,transient.text.lineCount);Assert::AreEqual(336,transient.text.bounds.bottom);
        const auto confirmed=Resolve({Cue(1),Cue(2,true),Cue(3,true),Cue(4)});
        Assert::AreEqual(2,confirmed.text.lineCount);Assert::AreEqual(352,confirmed.text.bounds.bottom);
    }
    TEST_METHOD(UnconfirmedCurrentCompanionCannotAcquireOrEnlargeCue) {
        const auto transient=Resolve({Cue(1,true),Cue(2),Cue(3)});
        Assert::IsFalse(transient.currentLinesConfirmed);
        SubtitleBoxPresentation tracker;
        Assert::IsFalse(tracker.Consume(transient).detected);
        Assert::IsTrue(tracker.Consume(Resolve({Cue(2),Cue(3)})).detected);
        const auto result=tracker.Consume(Resolve({Cue(3,true),Cue(4),Cue(5)}));
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);Assert::AreEqual(336,result.bounds.bottom);
        auto otherTransient=Cue(4,true);otherTransient.text.lineSignatures[1].fill(0xAA);
        const auto another=tracker.Consume(Resolve({otherTransient,Cue(5),Cue(6)}));
        Assert::IsTrue(another.detected);Assert::AreEqual(1,another.lineCount);Assert::AreEqual(result.cue,another.cue);
        const auto clear=tracker.Consume(Resolve({Cue(5),Cue(6)}));
        Assert::IsTrue(clear.detected);Assert::AreEqual(result.cue,clear.cue);
    }
    TEST_METHOD(GeometryFullRasterSequenceAndGenerationChangesBreakProof) {
        for(int kind=0;kind<8;++kind) {
            auto next=Cue(2);
            if(kind==0)next.pictureTop=44;
            if(kind==1)next.barAuthority=false;
            if(kind==2)next.identity.acceptedSequence=3;
            if(kind==3)next.identity.sourceFormatGeneration++;
            if(kind==4)next.identity.viewportGeneration++;
            if(kind==5)next.discontinuity=true;
            if(kind==6)next.identity.transportGeneration++;
            if(kind==7)next.identity.rendererGeneration++;
            Assert::AreEqual(1u,Resolve({Cue(1),next,Cue(3)}).matchingFrames);
        }
    }
    TEST_METHOD(ProofCannotCrossBlankThenResumeSameCue) {
        auto blank=Cue(2);blank.text={};
        Assert::AreEqual(1u,Resolve({Cue(1),blank,Cue(3)}).matchingFrames);
    }
    TEST_METHOD(ExactIdentityPolicyAndContinuityAreRequiredAtConsumption) {
        const auto preview=Resolve({Cue(1),Cue(2)});
        Assert::IsTrue(SubtitleBoxLookahead::IsCurrent(preview,Identity(1),7,8));
        for(int kind=0;kind<7;++kind) {
            auto identity=Identity(1);
            if(kind==0)identity.transportGeneration++;if(kind==1)identity.acceptedSequence++;
            if(kind==2)identity.sourceFrameNumber++;if(kind==3)identity.captureTimestamp++;
            if(kind==4)identity.sourceFormatGeneration++;if(kind==5)identity.viewportGeneration++;
            if(kind==6)identity.rendererGeneration++;
            Assert::IsFalse(SubtitleBoxLookahead::IsCurrent(preview,identity,7,8));
        }
        Assert::IsFalse(SubtitleBoxLookahead::IsCurrent(preview,Identity(1),9,8));
        Assert::IsFalse(SubtitleBoxLookahead::IsCurrent(preview,Identity(1),7,9));
    }
    TEST_METHOD(RepeatDoesNotAddConfirmationOrChangeAlreadyPresentedBounds) {
        SubtitleBoxPresentation tracker;
        const auto first=tracker.Consume(Resolve({Cue(1)}));
        const auto repeated=tracker.Consume(Resolve({Cue(1),Cue(2,true)}));
        Assert::AreEqual(first.bounds.bottom,repeated.bounds.bottom);
        Assert::AreEqual(first.observations,repeated.observations);
        Assert::AreEqual(first.cue,repeated.cue);
    }
    TEST_METHOD(ZeroFutureFramesUsesImmediateCurrentDetection) {
        SubtitleBoxPresentation tracker;const auto result=tracker.Consume(Resolve({Cue(1)}));
        Assert::IsTrue(result.detected);Assert::AreEqual(1u,result.observations);
    }
    TEST_METHOD(SlowDriftCannotAccumulateAnUnboundedCueBox) {
        SubtitleBoxPresentation tracker;uint64_t firstCue=0,lastCue=0;
        for(int shift=0;shift<20;++shift) {
            auto cue=Cue(shift+1);cue.text.bounds.left+=shift;cue.text.bounds.right+=shift;
            cue.text.lineBounds[0].left+=shift;cue.text.lineBounds[0].right+=shift;
            const auto result=tracker.Consume(Resolve({cue}));
            if(!shift)firstCue=result.cue;lastCue=result.cue;
            Assert::AreEqual(248,result.bounds.right-result.bounds.left);
        }
        Assert::IsTrue(firstCue!=lastCue);
    }
    TEST_METHOD(FrozenBoundsMustContainCurrentGlyphsEvenWithinIdentityTolerance) {
        SubtitleBoxPresentation tracker;auto cue=Cue(1);cue.height=2160;
        cue.text.bounds=cue.text.lineBounds[0];
        const auto first=tracker.Consume(Resolve({cue}));
        cue.identity=Identity(2);cue.text.lineBounds[0].left+=4;cue.text.lineBounds[0].right+=4;
        cue.text.bounds=cue.text.lineBounds[0];
        const auto shifted=tracker.Consume(Resolve({cue}));
        Assert::IsTrue(shifted.cue!=first.cue);Assert::IsTrue(shifted.bounds.right>=cue.text.lineBounds[0].right);
    }
    TEST_METHOD(UnconfirmedCompanionCannotHideEscapeOfAnEstablishedLine) {
        SubtitleBoxPresentation tracker;auto first=Cue(1);first.height=2160;
        first.text.bounds=first.text.lineBounds[0];
        Assert::IsTrue(tracker.Consume(Resolve({first})).detected);
        auto shifted=Cue(2,true);shifted.height=2160;
        shifted.text.lineBounds[0].left+=4;shifted.text.lineBounds[0].right+=4;
        auto future=Cue(3);future.height=2160;future.text.lineBounds[0]=shifted.text.lineBounds[0];
        const auto result=tracker.Consume(Resolve({shifted,future}));
        Assert::IsFalse(result.detected,L"cannot retain old box after known glyphs escape it");
    }
    TEST_METHOD(RealPixelsRecoverContaminatedBarsAndBothBoundaryCrossingLines) {
        auto pixels=Pixels();Text(pixels,175,307,22);Text(pixels,240,336,12);
        const auto first=Measure(pixels,1),second=Measure(pixels,2);
        Assert::IsTrue(first.barAuthority);Assert::IsTrue(first.text.detected);Assert::AreEqual(2,first.text.lineCount);
        SubtitleBoxPresentation tracker;const auto result=tracker.Consume(Resolve({first,second}));
        Assert::IsTrue(result.detected);Assert::IsTrue(result.bounds.top<=307);Assert::IsTrue(result.bounds.bottom>=350);
        auto expanded=Pixels(true);Text(expanded,175,307,22);Text(expanded,240,336,12);
        const auto full=Measure(expanded,3);
        Assert::IsFalse(full.barAuthority);Assert::IsFalse(tracker.Consume(Resolve({full})).detected);
    }
    TEST_METHOD(RealPixelsDetectEqualBoundsChangedTextDespiteUnchangedCompanion) {
        auto first=Pixels(),second=Pixels();
        Text(first,175,307,22);Text(first,240,336,12);
        Text(second,175,307,22,true);Text(second,240,336,12);
        const auto a=Measure(first,1),b=Measure(second,2);
        Assert::IsTrue(a.barAuthority,L"original cue has fresh bars");
        Assert::IsTrue(b.barAuthority,L"changed cue has fresh bars");
        Assert::IsTrue(a.text.detected,L"original cue detected");
        Assert::IsTrue(b.text.detected,L"changed cue detected");
        Assert::AreEqual(2,a.text.lineCount,L"original cue lines");
        Assert::AreEqual(2,b.text.lineCount,L"changed cue lines");
        Assert::IsFalse(SubtitleBoxLookahead::SameCue(a,b));
        Assert::AreEqual(1u,Resolve({a,b}).matchingFrames);
    }
    TEST_METHOD(RealPixelsAspectChangesUseTheCurrentBoundaryInBothDirections) {
        auto narrow=Pixels();Fill(narrow,0,0,640,360,64);Fill(narrow,0,60,640,300,300);
        Text(narrow,200,300,18);
        auto wide=Pixels();Text(wide,200,300,18);
        const auto n1=Measure(narrow,1),n2=Measure(narrow,2);
        Assert::IsTrue(n1.barAuthority&&n1.text.detected);
        SubtitleBoxPresentation tracker;const auto first=tracker.Consume(Resolve({n1,n2}));
        Assert::IsTrue(first.detected);
        const auto w3=Measure(wide,3),w4=Measure(wide,4);
        Assert::IsTrue(w3.barAuthority);Assert::IsFalse(w3.text.detected);
        Assert::IsFalse(tracker.Consume(Resolve({w3,w4})).detected);
        const auto n5=Measure(narrow,5),n6=Measure(narrow,6);
        const auto reacquired=tracker.Consume(Resolve({n5,n6}));
        Assert::IsTrue(reacquired.detected);Assert::IsTrue(first.cue!=reacquired.cue);
        Assert::AreEqual(0u,Resolve({w4,n5,n6}).matchingFrames);
        auto stableNarrow=Pixels();Fill(stableNarrow,0,0,640,360,64);Fill(stableNarrow,0,60,640,300,300);
        auto stableWide=Pixels();Text(stableNarrow,200,336,18);Text(stableWide,200,336,18);
        const auto sw=Measure(stableWide,10),sn=Measure(stableNarrow,11);
        const std::wstring geometry = L"wide="+std::to_wstring(sw.pictureTop)+L","+
            std::to_wstring(sw.pictureBottom)+L" detected="+std::to_wstring(sw.text.detected)+
            L" narrow="+std::to_wstring(sn.pictureTop)+L","+std::to_wstring(sn.pictureBottom)+
            L" detected="+std::to_wstring(sn.text.detected);
        Logger::WriteMessage(geometry.c_str());
        Assert::IsTrue(sw.barAuthority,L"wide lower-bar cue authority");
        Assert::IsTrue(sn.barAuthority,L"narrow lower-bar cue authority");
        Assert::IsTrue(sw.text.detected,L"wide lower-bar cue detected");
        Assert::IsTrue(sn.text.detected,L"narrow lower-bar cue detected");
        Assert::AreEqual(1u,Resolve({sw,sn}).matchingFrames);
    }
    TEST_METHOD(PolicyContinuityAndSourceDiscontinuityResetEstablishedCue) {
        for(int kind=0;kind<3;++kind) {
            SubtitleBoxPresentation tracker;const auto first=tracker.Consume(Resolve({Cue(1),Cue(2)}));
            auto preview=Resolve({Cue(2),Cue(3)});
            if(kind==0)preview.policyGeneration++;if(kind==1)preview.continuityGeneration++;
            if(kind==2)preview.current.discontinuity=true;
            const auto next=tracker.Consume(preview);
            Assert::IsTrue(next.detected);Assert::IsTrue(next.cue!=first.cue);Assert::AreEqual(1u,next.observations);
        }
    }
    TEST_METHOD(AlignedRejectedCaptionEdgeIsOnlyRelaxedForDiagnosticWithFreshPixelProof) {
        auto pixels=Pixels();Text(pixels,200,336,18);
        const auto source=Source(pixels);const auto raw=ExtractActivePictureEvidence(source);
        Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
        Assert::IsTrue(EvaluateSymmetricVerticalBarHypothesis(source,raw).classification==
            ActivePictureClassification::PROVISIONAL,L"default crop acquisition unchanged");
        const auto diagnostic=EvaluateSymmetricVerticalBarHypothesis(source,raw,true);
        Assert::IsTrue(diagnostic.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
        Assert::IsTrue(diagnostic.trustedBounds.top>=42&&diagnostic.trustedBounds.bottom<=318);
        auto expanded=Pixels();Fill(expanded,0,315,640,330,300);Text(expanded,200,336,18);
        const auto expandedSource=Source(expanded);const auto expandedRaw=ExtractActivePictureEvidence(expandedSource);
        const auto rejected=EvaluateSymmetricVerticalBarHypothesis(expandedSource,expandedRaw,true);
        Assert::IsTrue(rejected.classification!=ActivePictureClassification::BAR_CROP_TRUSTED ||
            rejected.trustedBounds.bottom>=330,L"connected picture must not become an inferred subtitle bar");
    }
};
}
