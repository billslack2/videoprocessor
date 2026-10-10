#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleCutPaste.h>
#include <vprenderer/SubtitleAssistedSourceEvidence.h>
#include <vector>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace VideoProcessorTest {
namespace {
struct AssistedSourceFixture {
    int w,h,top,bottom;bool p210;size_t pitch;
    std::vector<uint16_t> data;
    ActivePictureEvidence raw;
    SubtitleAssistedSourceNomination nomination;
    SubtitleBoxResult text;
    std::shared_ptr<const SubtitleInkSnapshot> ink;
    SubtitleBoxRect capture;
    void Fill(int l,int t,int r,int b,int value) {
        for(int y=t;y<b;++y)for(int x=l;x<r;++x)data[size_t(y)*pitch+x]=uint16_t(value<<6);
    }
    void Chroma(int x,int y,int u,int v) {
        const auto at=pitch*h+size_t(p210?y:y/2)*pitch+(x&~1);
        data[at]=uint16_t(u<<6);data[at+1]=uint16_t(v<<6);
    }
    AnalysisLumaSource Source() const {
        return {reinterpret_cast<const uint8_t*>(data.data()),data.size()*2,w,h,pitch*2,pitch*2,
            p210?AnalysisLumaFormat::P210:AnalysisLumaFormat::P010,
            p210?VideoFrameEncoding::V210:VideoFrameEncoding::UNKNOWN,ColorSpace::REC_709,11};
    }
    AssistedSourceFixture(bool fullChroma=false,bool upper=false,bool asymmetric=false,int scale=1,int extraWidth=0):
        w(640*scale+extraWidth),h(360*scale),top(45*scale),bottom((asymmetric?300:315)*scale),p210(fullChroma),pitch(w+16),
        data(pitch*(h+(p210?h:h/2)),uint16_t(512<<6)) {
        Fill(0,0,w,h,64);Fill(0,top,w,bottom,200);
        const int y=(upper?20:326)*scale;
        for(int n=0;n<18;++n) {
            const int x=(195+n*13)*scale;
            Fill(x,y,x+8*scale,y+14*scale,510);
            Fill(x+2*scale,y+2*scale,x+6*scale,y+12*scale,64);
        }
        raw.available=true;raw.classification=ActivePictureClassification::PROVISIONAL;
        raw.proposedBounds={0,top,w,bottom,w,h};raw.top.barPixels=top;raw.bottom.barPixels=h-bottom;
        raw.top.trusted=!upper;raw.bottom.trusted=upper;
        raw.top.lumaFloor=raw.bottom.lumaFloor=64;
        raw.top.neutralChromaFraction=raw.bottom.neutralChromaFraction=1;
        nomination=NominateSubtitleAssistedSourceBounds(Source(),raw);
        SubtitleBoxDetector detector;
        text=detector.Analyze(Source(),nomination.candidate.top,nomination.candidate.bottom,100,7);
        ink=detector.InkSnapshot();capture=SubtitleGlyphCaptureBounds(text,w,h);
    }
    SubtitleAssistedSourceProof Proof() const {
        if(!ink)return {};
        return VerifySubtitleAssistedSourceEvidence(Source(),nomination,text,*ink,capture);
    }
};
}
TEST_CLASS(SubtitleAssistedSourceEvidenceTests) {
public:
    TEST_METHOD(CurrentMorphologyPassesBothEdgesFormatsAndAsymmetricBarsWithoutMutation) {
        for(bool p210:{false,true})for(bool top:{false,true})for(bool asymmetric:{false,true}) {
            AssistedSourceFixture f(p210,top,asymmetric);
            Assert::IsTrue(f.nomination.nominated);Assert::IsTrue(f.text.detected);
            const auto before=f.data;const auto proof=f.Proof();
            Assert::IsTrue(proof.verified);Assert::IsTrue(proof.ownedBarPixels>8);
            Assert::AreEqual(0,proof.candidate.left);Assert::AreEqual(f.w,proof.candidate.right);
            Assert::IsTrue(proof.candidate.trustedBarAxes==ActivePictureBounds::BarAxes::NONE);
            Assert::IsTrue(before==f.data);
        }
    }
    TEST_METHOD(RealFourKDetectorResultPassesNativeBudget) {
        AssistedSourceFixture f(true,false,false,6);
        Assert::IsTrue(f.nomination.nominated);Assert::IsTrue(f.text.detected);
        Assert::IsTrue(f.Proof().verified);
    }
    TEST_METHOD(NominationRequiresIndependentEvidenceOnBothEdges) {
        AssistedSourceFixture f;Assert::IsTrue(f.nomination.nominated);
        f.raw.top.trusted=f.raw.bottom.trusted=false;
        Assert::IsFalse(NominateSubtitleAssistedSourceBounds(f.Source(),f.raw).nominated);
        f.raw.top.trusted=f.raw.bottom.trusted=true;
        Assert::IsFalse(NominateSubtitleAssistedSourceBounds(f.Source(),f.raw).nominated);
        f.raw.bottom.trusted=false;f.Fill(0,f.bottom-4,f.w,f.bottom,64);
        Assert::IsFalse(NominateSubtitleAssistedSourceBounds(f.Source(),f.raw).nominated);
    }
    TEST_METHOD(NearBlackSceneCannotBootstrapFromBroadButDimEdges) {
        AssistedSourceFixture f;Assert::IsTrue(f.Proof().verified);
        f.Fill(0,f.top,f.w,f.bottom,90);
        Assert::IsTrue(SubtitleAssistedSourceDetail::Boundary(f.Source(),f.top,true,1,88));
        Assert::IsTrue(SubtitleAssistedSourceDetail::Boundary(f.Source(),f.bottom,false,1,88));
        const auto nomination=NominateSubtitleAssistedSourceBounds(f.Source(),f.raw);
        Assert::IsFalse(nomination.nominated);Assert::AreEqual("global-near-black",nomination.reason);
        const auto proof=f.Proof();Assert::IsFalse(proof.verified);
        Assert::AreEqual("global-near-black",proof.reason);
    }
    TEST_METHOD(CurrentCaptionCannotSupplyItsOwnPictureBoundaryProof) {
        AssistedSourceFixture f;
        f.Fill(0,f.bottom-32,f.w,f.bottom,64);
        for(int n=0;n<26;++n) {
            const int x=145+n*13,y=301;
            f.Fill(x,y,x+8,y+14,510);f.Fill(x+2,y+2,x+6,y+12,64);
        }
        const auto nomination=NominateSubtitleAssistedSourceBounds(f.Source(),f.raw);
        Assert::IsTrue(nomination.nominated,L"caption-only transition may nominate a search ROI");
        SubtitleBoxDetector detector;
        const auto text=detector.Analyze(f.Source(),nomination.candidate.top,nomination.candidate.bottom,100,7);
        Assert::IsTrue(text.detected);Assert::AreEqual(2,text.lineCount);
        const auto ink=detector.InkSnapshot();Assert::IsTrue(ink!=nullptr);
        const auto proof=VerifySubtitleAssistedSourceEvidence(f.Source(),nomination,text,*ink,SubtitleGlyphCaptureBounds(text,f.w,f.h));
        Assert::IsFalse(proof.verified);Assert::AreEqual("current-boundary-conflict",proof.reason);
    }
    TEST_METHOD(LocalizedPictureCannotSupplyBroadIndependentBoundary) {
        AssistedSourceFixture f;f.Fill(0,f.top,f.w,f.top+6,64);
        f.Fill(300,f.top,330,f.top+6,200);
        Assert::IsFalse(NominateSubtitleAssistedSourceBounds(f.Source(),f.raw).nominated);
    }
    TEST_METHOD(AllEightNativeLumaLanesAndScalarTailRejectUnownedContent) {
        for(bool p210:{false,true})for(int x:{16,17,18,19,20,21,22,23,638,639}) {
            AssistedSourceFixture f(p210);f.Fill(x,10,x+1,11,300);
            const auto proof=f.Proof();Assert::IsFalse(proof.verified);
            Assert::AreEqual("native-unowned-content",proof.reason);
        }
    }
    TEST_METHOD(ScalarNativeTailAndAdjacentRadiusPlusOnePixelsReject) {
        for(bool p210:{false,true})for(int x:{640,641}) {
            AssistedSourceFixture f(p210,false,false,1,2);Assert::IsTrue(f.Proof().verified);
            f.Fill(x,10,x+1,11,300);const auto proof=f.Proof();
            Assert::IsFalse(proof.verified);Assert::AreEqual("native-unowned-content",proof.reason);
        }
        AssistedSourceFixture f;f.Fill(192,330,193,331,300);
        Assert::IsFalse(f.Proof().verified);
        AssistedSourceFixture g;g.Fill(195,342,425,343,300);
        Assert::IsFalse(g.Proof().verified);
    }
    TEST_METHOD(EachChromaLaneRejectsEvenInsideOwnedGlyphArea) {
        for(bool p210:{false,true})for(int x:{196,198,200,202})for(bool u:{false,true}) {
            AssistedSourceFixture f(p210);f.Chroma(x,335,u?545:512,u?512:479);
            Assert::IsFalse(f.Proof().verified);
        }
    }
    TEST_METHOD(RealExpansionAndNearApertureContentCannotBorrowGlyphDilation) {
        AssistedSourceFixture f;Assert::IsTrue(f.Proof().verified);
        f.Fill(180,f.nomination.candidate.bottom,440,f.nomination.candidate.bottom+2,110);
        Assert::IsFalse(f.Proof().verified);
        AssistedSourceFixture g;g.Fill(10,345,40,349,400);
        Assert::IsFalse(g.Proof().verified);
    }
    TEST_METHOD(HeldWorkLimitedMissingAndStaleSourceGlyphsFailClosed) {
        AssistedSourceFixture f;f.text.held=true;Assert::IsFalse(f.Proof().verified);
        f.text.held=false;f.text.workLimit=true;Assert::IsFalse(f.Proof().verified);
        f.text.workLimit=false;f.text.detected=false;Assert::IsFalse(f.Proof().verified);
        AssistedSourceFixture g;g.Fill(195,326,203,340,64);Assert::IsFalse(g.Proof().verified);
        AssistedSourceFixture h;auto ink=*h.ink;ink.rawEvidenceComplete=false;
        Assert::IsFalse(VerifySubtitleAssistedSourceEvidence(h.Source(),h.nomination,h.text,ink,h.capture).verified);
    }
    TEST_METHOD(GeneratedPanelsNeverSupplyMissingSourceProof) {
        AssistedSourceFixture f;f.text.sourcePanel=f.text.capturePanel={0,0,f.w,f.h};
        f.Fill(10,345,40,349,400);Assert::IsFalse(f.Proof().verified);
    }
    TEST_METHOD(ActualCaptureMustContainEveryWhitelistedNativePixel) {
        AssistedSourceFixture f;Assert::IsTrue(f.Proof().verified);
        f.capture.left=200;Assert::IsFalse(f.Proof().verified);
    }
    TEST_METHOD(MalformedAndNonplanarDescriptorsFailClosed) {
        AssistedSourceFixture f;auto ink=*f.ink;ink.sourceRows[10]=ink.sourceRows[9];
        Assert::IsFalse(VerifySubtitleAssistedSourceEvidence(f.Source(),f.nomination,f.text,ink,f.capture).verified);
        auto source=f.Source();--source.dataBytes;
        Assert::IsFalse(VerifySubtitleAssistedSourceEvidence(source,f.nomination,f.text,*f.ink,f.capture).verified);
        source=f.Source();source.format=AnalysisLumaFormat::NativeYuv422;source.encoding=VideoFrameEncoding::UYVY;
        Assert::IsTrue(source.IsValid());
        Assert::IsFalse(VerifySubtitleAssistedSourceEvidence(source,f.nomination,f.text,*f.ink,f.capture).verified);
        auto nomination=f.nomination;nomination.candidate.left=1;
        Assert::IsFalse(VerifySubtitleAssistedSourceEvidence(f.Source(),nomination,f.text,*f.ink,f.capture).verified);
    }
};
}



