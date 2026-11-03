#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleCurrentCuePresentation.h>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests {
TEST_CLASS(SubtitleCurrentCuePresentationTests) {
    static SubtitleBoxObservation Inspection() {
        SubtitleBoxObservation o;
        o.identity={1,168,168,168000,2,3,4,5};
        o.width=640;o.height=360;o.policyGeneration=6;o.continuityGeneration=7;
        o.nearBarDistance=24;o.optimizationMode=1;o.analysisStride=2;
        o.analyzed=true;o.inspectionCandidateUsed=true;
        o.pictureAuthorityUnavailableForMeasurement=true;
        o.sharedPicture.required=true;o.sharedPicture.identity=o.identity;
        o.text.detected=true;o.text.lineCount=1;
        o.text.bounds=o.text.anchor=o.text.lineBounds[0]={200,310,440,330};
        o.text.lineSignatures[0].fill(100);
        return o;
    }
    static ActivePictureBounds Base() {
        ActivePictureBounds b;b.left=0;b.top=44;b.right=640;b.bottom=314;
        b.rasterWidth=640;b.rasterHeight=360;return b;
    }
public:
    TEST_METHOD(InspectionScanUsesNativeProofRequestWithoutChangingMeasurement) {
        const auto raw=Inspection();SubtitleBoxObservation key;
        Assert::IsFalse(raw.sharedPicture.AvailableFor(raw.identity,raw.width,raw.height));
        Assert::IsTrue(BuildSubtitleCurrentCueProofKey(raw,raw.identity,Base(),true,key));
        Assert::IsTrue(key.sharedPicture.AvailableFor(raw.identity,raw.width,raw.height));
        Assert::IsTrue(SameAnalysisFrameIdentity(raw.identity,key.identity));
        Assert::AreEqual(raw.policyGeneration,key.policyGeneration);
        Assert::AreEqual(raw.continuityGeneration,key.continuityGeneration);
        Assert::AreEqual(raw.analysisStride,key.analysisStride);
        Assert::AreEqual(raw.nearBarDistance,key.nearBarDistance);
        Assert::IsFalse(key.analyzed);Assert::IsFalse(key.text.detected);
        Assert::IsFalse(key.barAuthority);Assert::IsFalse(key.independentBarAnalysis);
        Assert::IsFalse(key.inspectionCandidateUsed);Assert::IsTrue(!key.ink);
        Assert::IsTrue(raw.analyzed);Assert::IsTrue(raw.inspectionCandidateUsed);
        Assert::IsTrue(raw.pictureAuthorityUnavailableForMeasurement);
        Assert::IsTrue(raw.text.detected);
        // A different Classic lease snapshot cannot change this native proof request.
        auto renewed=raw;renewed.sharedPicture=key.sharedPicture;
        SubtitleBoxObservation renewedKey;
        Assert::IsTrue(BuildSubtitleCurrentCueProofKey(renewed,raw.identity,Base(),true,renewedKey));
        Assert::IsTrue(key.sharedPicture.Matches(renewedKey.sharedPicture));
    }
    TEST_METHOD(ProofRequestRejectsStaleSourceAndUnavailableNativeContext) {
        auto raw=Inspection();SubtitleBoxObservation key;
        auto identity=raw.identity;++identity.acceptedSequence;
        Assert::IsFalse(BuildSubtitleCurrentCueProofKey(raw,identity,Base(),true,key));
        identity=raw.identity;++identity.analysisDomainKey;
        Assert::IsFalse(BuildSubtitleCurrentCueProofKey(raw,identity,Base(),true,key));
        Assert::IsFalse(BuildSubtitleCurrentCueProofKey(raw,raw.identity,Base(),false,key));
        auto base=Base();base.rasterWidth=1280;
        Assert::IsFalse(BuildSubtitleCurrentCueProofKey(raw,raw.identity,base,true,key));
        base=Base();base.top=0;base.bottom=360;
        Assert::IsFalse(BuildSubtitleCurrentCueProofKey(raw,raw.identity,base,true,key));
        raw.discontinuity=true;
        Assert::IsFalse(BuildSubtitleCurrentCueProofKey(raw,raw.identity,Base(),true,key));
        Assert::IsFalse(key.sharedPicture.required);
    }
    TEST_METHOD(CurrentCueMetadataRequiresVerifiedCaptureAndSimilarLines) {
        const auto accepted=Inspection().text;
        auto current=accepted;
        SubtitleCutPasteGeometry operation;operation.valid=true;
        operation.glyphLines[0]={196,306,444,334};
        current.lineBounds[0]={196,310,444,330};
        // Different raw card estimates are not substituted for verified cleanup.
        current.capturePanel={190,300,450,333};current.lineCapturePanels[0]=current.capturePanel;
        current.lineSignatures[0][10]=104;
        Assert::IsTrue(CurrentSubtitleCueFitsVerifiedOperation(current,accepted,operation,4));
        auto changed=current;changed.lineBounds[0].left=195;
        Assert::IsFalse(CurrentSubtitleCueFitsVerifiedOperation(changed,accepted,operation,4));
        auto clipped=operation;clipped.glyphLines[0].left=198;
        Assert::IsFalse(CurrentSubtitleCueFitsVerifiedOperation(current,accepted,clipped,4));
        changed=current;changed.lineCount=2;
        Assert::IsFalse(CurrentSubtitleCueFitsVerifiedOperation(changed,accepted,operation,4));
        changed=current;changed.lineSignatures[0].fill(0);
        Assert::IsFalse(CurrentSubtitleCueFitsVerifiedOperation(changed,accepted,operation,4));
        changed=current;changed.workLimit=true;
        Assert::IsFalse(CurrentSubtitleCueFitsVerifiedOperation(changed,accepted,operation,4));
        Assert::IsFalse(CurrentSubtitleCueFitsVerifiedOperation(current,accepted,operation,9));
        Assert::IsFalse(CurrentSubtitleCueFitsVerifiedOperation(current,accepted,{},4));
        Assert::IsTrue(CurrentSubtitleCueFitsVerifiedOperation(accepted,accepted,{},4));
        // A worker may seal already-verified black erasure pixels as harmless
        // grouping padding. This grants no larger glyph capture.
        auto tight=operation;tight.glyphLines[0].bottom=accepted.lineBounds[0].bottom;
        auto rounded=accepted;rounded.lineBounds[0].bottom+=4;
        Assert::IsFalse(CurrentSubtitleCueFitsVerifiedOperation(rounded,accepted,tight,4));
        auto semantic=tight.glyphLines;semantic[0].bottom+=4;
        Assert::IsTrue(CurrentSubtitleCueFitsVerifiedOperation(rounded,accepted,tight,4,&semantic));
        Assert::AreEqual(accepted.lineBounds[0].bottom,tight.glyphLines[0].bottom);
        auto excessive=semantic;++excessive[0].bottom;
        Assert::IsFalse(CurrentSubtitleCueFitsVerifiedOperation(rounded,accepted,tight,4,&excessive));
        auto shrunken=semantic;shrunken[0].left=accepted.lineBounds[0].left+1;
        Assert::IsFalse(CurrentSubtitleCueFitsVerifiedOperation(rounded,accepted,tight,4,&shrunken));
        // An unused capture collar need not become a semantic padding strip.
        auto narrow=semantic;narrow[0].left=accepted.lineBounds[0].left;
        Assert::IsTrue(narrow[0].left>tight.glyphLines[0].left);
        Assert::IsTrue(CurrentSubtitleCueFitsVerifiedOperation(rounded,accepted,tight,4,&narrow));
        // Two similar lines still require a one-to-one capture correspondence.
        auto pair=accepted;pair.lineCount=2;pair.lineBounds[1]={200,336,440,350};
        pair.lineSignatures[1]=pair.lineSignatures[0];
        auto duplicated=pair;duplicated.lineBounds[1]=pair.lineBounds[0];
        operation.glyphLines[1]={196,332,444,354};
        Assert::IsFalse(CurrentSubtitleCueFitsVerifiedOperation(duplicated,pair,operation,4));
    }
};
}
