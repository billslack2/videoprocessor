#include "pch.h"
#include "CppUnitTest.h"
#include <ActivePictureEvidence.h>
#include <algorithm>
#include <type_traits>
#include <limits>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
namespace
{
struct OpposingPixels
{
    int width, height;
    bool p210;
    size_t pitch;
    std::vector<uint8_t> bytes;
    OpposingPixels(int w=960, int h=540, bool fullHeightChroma=true)
        : width(w), height(h), p210(fullHeightChroma), pitch(size_t(w)*2+32),
          bytes(pitch*(h+(fullHeightChroma?h:h/2)),0)
    {
        Rectangle(0,0,w,h,64);
    }
    void Code(size_t offset, int value)
    {
        const uint16_t packed=static_cast<uint16_t>(value<<6);
        bytes[offset]=static_cast<uint8_t>(packed);
        bytes[offset+1]=static_cast<uint8_t>(packed>>8);
    }
    void Rectangle(int left,int top,int right,int bottom,int y,int u=512,int v=512)
    {
        for (int row=top;row<bottom;++row)
            for (int x=left;x<right;++x) Code(size_t(row)*pitch+size_t(x)*2,y);
        for (int row=p210?top:top/2;row<(p210?bottom:(bottom+1)/2);++row)
            for (int x=left&~1;x<right;x+=2)
            {
                Code(pitch*height+size_t(row)*pitch+size_t(x)*2,u);
                Code(pitch*height+size_t(row)*pitch+size_t(x)*2+2,v);
            }
    }
    AnalysisLumaSource Source() const
    {
        return {bytes.data(),bytes.size(),width,height,pitch,pitch,
            p210?AnalysisLumaFormat::P210:AnalysisLumaFormat::P010,
            VideoFrameEncoding::V210,ColorSpace::REC_709,7};
    }
};

struct PackedOpposingPixels
{
    int width,height;
    size_t pitch;
    std::vector<uint8_t> bytes;
    explicit PackedOpposingPixels(const AnalysisLumaSource& source)
        : width(source.width),height(source.height),pitch(size_t(width/6)*16+32),
          bytes(pitch*height,0)
    {
        Assert::AreEqual(0,width%6);
        for (int y=0;y<height;++y)
            for (int x=0;x<width;x+=6)
            {
                AnalysisLumaSample p[6];
                for (int i=0;i<6;++i) Assert::IsTrue(source.Sample(x+i,y,p[i]));
                const uint32_t words[]={
                    uint32_t(p[0].chromaU)|uint32_t(p[0].luma)<<10|uint32_t(p[0].chromaV)<<20,
                    uint32_t(p[1].luma)|uint32_t(p[2].chromaU)<<10|uint32_t(p[2].luma)<<20,
                    uint32_t(p[2].chromaV)|uint32_t(p[3].luma)<<10|uint32_t(p[4].chromaU)<<20,
                    uint32_t(p[4].luma)|uint32_t(p[4].chromaV)<<10|uint32_t(p[5].luma)<<20};
                auto* target=bytes.data()+size_t(y)*pitch+size_t(x/6)*16;
                for (int word=0;word<4;++word)
                    for (int b=0;b<4;++b) target[word*4+b]=static_cast<uint8_t>(words[word]>>(b*8));
            }
    }
    AnalysisLumaSource Source() const
    {
        return {bytes.data(),bytes.size(),width,height,pitch,0,
            AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,ColorSpace::REC_709,7};
    }
};

void BuildOpposingFixture(OpposingPixels& frame,int pattern=0)
{
    const bool fullSize=frame.height==2160;
    const int top=fullSize?276:68;
    const int brightBottom=fullSize?1612:402;
    const int mirroredBottom=frame.height-top;
    frame.Rectangle(0,0,frame.width,frame.height,64);
    frame.Rectangle(0,top,frame.width,brightBottom,300);
    if (pattern==0) frame.Rectangle(0,brightBottom,frame.width,mirroredBottom,80);
    if (pattern==1) frame.Rectangle(0,brightBottom,frame.width,mirroredBottom+(fullSize?24:6),80);
    if (pattern==2)
    {
        Assert::IsTrue(fullSize);
        frame.Rectangle(0,1612,frame.width,1848,84);
        for (int y=1848;y<2048;++y)
            frame.Rectangle(0,y,frame.width,y+1,84-(y-1848)*20/200);
    }
}
void EqualOpposingBounds(const ActivePictureBounds& a,const ActivePictureBounds& b)
{
    Assert::AreEqual(a.left,b.left); Assert::AreEqual(a.top,b.top);
    Assert::AreEqual(a.right,b.right); Assert::AreEqual(a.bottom,b.bottom);
    Assert::AreEqual(a.rasterWidth,b.rasterWidth); Assert::AreEqual(a.rasterHeight,b.rasterHeight);
    Assert::AreEqual(a.aspectRatio,b.aspectRatio);
    Assert::AreEqual(int(a.trustedBarAxes),int(b.trustedBarAxes));
}
void AssertOpposingRaw(const ActivePictureEvidence& raw)
{
    Assert::IsTrue(raw.available && raw.classification==ActivePictureClassification::PROVISIONAL);
    Assert::IsTrue(raw.top.trusted && raw.bottom.trusted);
    Assert::IsTrue(raw.axisEvidence.vertical.reason==ActivePictureAxisReason::BAR_ASYMMETRY);
    Assert::IsTrue(raw.axisEvidence.vertical.scanComplete && raw.axisEvidence.horizontal.scanComplete);
    Assert::IsFalse(raw.axisEvidence.horizontal.barCandidate || raw.axisEvidence.horizontal.FailedBar());
    Assert::AreEqual(0,raw.proposedBounds.left);
    Assert::AreEqual(raw.proposedBounds.rasterWidth,raw.proposedBounds.right);
}
void AssertOpposingMeasured(const ActivePictureOpposingBoundaryDiagnostics& result,int width=3840,int height=2160,int inset=276)
{
    Assert::IsTrue(result.evaluated);
    Assert::AreEqual(0,result.candidate.left); Assert::AreEqual(width,result.candidate.right);
    Assert::AreEqual(inset,result.candidate.top); Assert::AreEqual(height-inset,result.candidate.bottom);
    Assert::AreEqual(width,result.candidate.rasterWidth); Assert::AreEqual(height,result.candidate.rasterHeight);
    Assert::AreEqual(size_t(864),result.lumaSamples);
    Assert::AreEqual(size_t(576),result.chromaSamples);
}
void AssertOpposingUnmeasured(const ActivePictureOpposingBoundaryDiagnostics& result)
{
    Assert::IsFalse(result.evaluated);
    Assert::AreEqual(size_t(0),result.lumaSamples);
    Assert::AreEqual(size_t(0),result.chromaSamples);
    Assert::IsFalse(result.top.trusted || result.bottom.trusted);
    Assert::AreEqual(0,result.topAdjacent.supported);
    Assert::AreEqual(0,result.bottomAdjacent.supported);
}
void SameOpposingAdjacent(const ActivePictureAdjacentBoundaryProbe& a,const ActivePictureAdjacentBoundaryProbe& b)
{
    Assert::AreEqual(a.insideMean,b.insideMean); Assert::AreEqual(a.outsideMean,b.outsideMean);
    Assert::AreEqual(a.outsideP90,b.outsideP90); Assert::AreEqual(a.outsideDispersion,b.outsideDispersion);
    Assert::AreEqual(a.contrast,b.contrast); Assert::AreEqual(a.supported,b.supported);
}
}

TEST_CLASS(OpposingBoundaryDiagnosticsTests)
{
public:
    TEST_METHOD(TrueDimPictureBoundaryHasNativeBarProofAndAdjacentContrast)
    {
        OpposingPixels frame(3840,2160); BuildOpposingFixture(frame);
        const auto raw=ExtractActivePictureEvidence(frame.Source()); AssertOpposingRaw(raw);
        Assert::AreEqual(276,raw.proposedBounds.top); Assert::AreEqual(1612,raw.proposedBounds.bottom);
        const auto result=MeasureActivePictureOpposingBoundaryDiagnostics(frame.Source(),raw);
        AssertOpposingMeasured(result);
        Assert::IsTrue(result.top.trusted && result.bottom.trusted);
        Assert::AreEqual(16.0,result.bottom.innerBoundaryContrast);
        Assert::AreEqual(80.0,result.bottomAdjacent.insideMean);
        Assert::AreEqual(64.0,result.bottomAdjacent.outsideMean);
        Assert::AreEqual(64.0,result.bottomAdjacent.outsideP90);
        Assert::AreEqual(0.0,result.bottomAdjacent.outsideDispersion);
        Assert::AreEqual(16.0,result.bottomAdjacent.contrast);
        Assert::AreEqual(48,result.bottomAdjacent.supported);
        Assert::AreEqual(236.0,result.topAdjacent.contrast);
        Assert::AreEqual(48,result.topAdjacent.supported);
        static_assert(!std::is_convertible<ActivePictureOpposingBoundaryDiagnostics,ActivePictureEvidence>::value,
            "A mirrored diagnostic is not acquisition evidence.");
        static_assert(!std::is_convertible<ActivePictureOpposingBoundaryDiagnostics,ActivePictureObservation>::value,
            "A mirrored diagnostic is not a transition observation.");
    }

    TEST_METHOD(DisplacedDimGroundCanPassNativeBarProofWithoutAnEdgeAtTheNominee)
    {
        OpposingPixels frame(3840,2160); BuildOpposingFixture(frame,1);
        const auto raw=ExtractActivePictureEvidence(frame.Source()); AssertOpposingRaw(raw);
        const auto result=MeasureActivePictureOpposingBoundaryDiagnostics(frame.Source(),raw);
        AssertOpposingMeasured(result);
        // The actual80->64 seam is1908. A crop at1884 would discard24 rows of
        // dim picture even though five of the six exterior depth lines are64.
        Assert::IsTrue(result.top.trusted && result.bottom.trusted);
        Assert::IsTrue(result.bottom.innerBoundaryContrast>13.0 && result.bottom.innerBoundaryContrast<14.0);
        Assert::AreEqual(80.0,result.bottomAdjacent.insideMean);
        Assert::AreEqual(80.0,result.bottomAdjacent.outsideMean);
        Assert::AreEqual(0.0,result.bottomAdjacent.contrast);
        Assert::AreEqual(0,result.bottomAdjacent.supported);
        const auto existingSafety=EvaluateActivePicturePresentationRetention(frame.Source(),result.candidate);
        Assert::IsTrue(existingSafety.CanRetainPresentation(),L"Retain this safety limitation as a counterexample.");
        Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
    }

    TEST_METHOD(SmoothGroundRampCanPassNativeBarProofWithZeroAdjacentContrast)
    {
        OpposingPixels frame(3840,2160); BuildOpposingFixture(frame,2);
        const auto raw=ExtractActivePictureEvidence(frame.Source()); AssertOpposingRaw(raw);
        const auto result=MeasureActivePictureOpposingBoundaryDiagnostics(frame.Source(),raw);
        AssertOpposingMeasured(result);
        Assert::IsTrue(result.top.trusted && result.bottom.trusted);
        Assert::IsTrue(result.bottom.innerBoundaryContrast>11.0 && result.bottom.innerBoundaryContrast<12.0);
        Assert::AreEqual(81.0,result.bottomAdjacent.insideMean);
        Assert::AreEqual(81.0,result.bottomAdjacent.outsideMean);
        Assert::AreEqual(0.0,result.bottomAdjacent.contrast);
        Assert::AreEqual(0,result.bottomAdjacent.supported);
        Assert::IsTrue(EvaluateActivePicturePresentationRetention(frame.Source(),result.candidate).CanRetainPresentation());
        Assert::IsTrue(raw.axisEvidence.vertical.FailedBar());
    }

    TEST_METHOD(OrdinaryAsymmetricBlackBarsDoNotInventOpposingBoundaryContrast)
    {
        OpposingPixels frame(3840,2160); BuildOpposingFixture(frame,3);
        const auto raw=ExtractActivePictureEvidence(frame.Source()); AssertOpposingRaw(raw);
        const auto result=MeasureActivePictureOpposingBoundaryDiagnostics(frame.Source(),raw);
        AssertOpposingMeasured(result);
        Assert::IsTrue(result.top.trusted); Assert::IsFalse(result.bottom.trusted);
        Assert::AreEqual(0.0,result.bottom.innerBoundaryContrast);
        Assert::AreEqual(0.0,result.bottomAdjacent.contrast);
        Assert::AreEqual(0,result.bottomAdjacent.supported);
    }

    TEST_METHOD(TheShallowerBottomMarginCanNominateTheMirroredTopBoundary)
    {
        OpposingPixels frame(3840,2160); BuildOpposingFixture(frame);
        for (int row=0;row<frame.height/2;++row)
            std::swap_ranges(frame.bytes.begin()+size_t(row)*frame.pitch,
                frame.bytes.begin()+size_t(row+1)*frame.pitch,
                frame.bytes.begin()+size_t(frame.height-1-row)*frame.pitch);
        const auto raw=ExtractActivePictureEvidence(frame.Source()); AssertOpposingRaw(raw);
        Assert::AreEqual(548,raw.proposedBounds.top); Assert::AreEqual(1884,raw.proposedBounds.bottom);
        const auto result=MeasureActivePictureOpposingBoundaryDiagnostics(frame.Source(),raw);
        AssertOpposingMeasured(result);
        Assert::IsTrue(result.top.trusted && result.bottom.trusted);
        Assert::AreEqual(16.0,result.topAdjacent.contrast); Assert::AreEqual(48,result.topAdjacent.supported);
        Assert::AreEqual(236.0,result.bottomAdjacent.contrast); Assert::AreEqual(48,result.bottomAdjacent.supported);
    }

    TEST_METHOD(SmallBarsKeepTheExistingEighteenCodeContrastRequirement)
    {
        OpposingPixels frame;
        frame.Rectangle(0,0,960,540,64);
        frame.Rectangle(0,10,960,490,300);
        frame.Rectangle(0,490,960,530,80);
        const auto raw=ExtractActivePictureEvidence(frame.Source()); AssertOpposingRaw(raw);
        Assert::AreEqual(10,raw.proposedBounds.top); Assert::AreEqual(490,raw.proposedBounds.bottom);
        const auto result=MeasureActivePictureOpposingBoundaryDiagnostics(frame.Source(),raw);
        AssertOpposingMeasured(result,960,540,10);
        Assert::IsTrue(result.top.trusted); Assert::IsFalse(result.bottom.trusted);
        Assert::AreEqual(16.0,result.bottomAdjacent.contrast);
        Assert::AreEqual(0,result.bottomAdjacent.supported,L"Small-bar support must not silently use the large-bar10 threshold.");
    }

    TEST_METHOD(P010P210AndNativeV210AgreeOnTheExactMirroredMeasurement)
    {
        OpposingPixels p010(960,540,false),p210; BuildOpposingFixture(p010); BuildOpposingFixture(p210);
        PackedOpposingPixels packed(p210.Source());
        const auto reference=MeasureActivePictureOpposingBoundaryDiagnostics(p210.Source(),ExtractActivePictureEvidence(p210.Source()));
        AssertOpposingMeasured(reference,960,540,68);
        for (const auto& source:{p010.Source(),packed.Source()})
        {
            const auto raw=ExtractActivePictureEvidence(source); AssertOpposingRaw(raw);
            const auto result=MeasureActivePictureOpposingBoundaryDiagnostics(source,raw);
            AssertOpposingMeasured(result,960,540,68);
            EqualOpposingBounds(reference.candidate,result.candidate);
            Assert::AreEqual(reference.top.trusted,result.top.trusted);
            Assert::AreEqual(reference.bottom.trusted,result.bottom.trusted);
            Assert::AreEqual(reference.bottom.innerBoundaryContrast,result.bottom.innerBoundaryContrast);
            SameOpposingAdjacent(reference.topAdjacent,result.topAdjacent);
            SameOpposingAdjacent(reference.bottomAdjacent,result.bottomAdjacent);
        }
    }

    TEST_METHOD(ValidLayoutWithUnsupportedRgbDecoderCannotReportEvaluatedZeros)
    {
        OpposingPixels frame; BuildOpposingFixture(frame);
        const auto raw=ExtractActivePictureEvidence(frame.Source()); AssertOpposingRaw(raw);
        std::vector<uint8_t> bytes(size_t(960)*540*4,0);
        AnalysisLumaSource source{bytes.data(),bytes.size(),960,540,size_t(960)*4,0,
            AnalysisLumaFormat::NativeRgb,VideoFrameEncoding::UNKNOWN,ColorSpace::REC_709,7};
        Assert::IsTrue(source.IsValid(),L"The layout is valid; decoding is independently unsupported.");
        AnalysisLumaSample sample;
        Assert::IsFalse(source.Sample(480,270,sample));
        AssertOpposingUnmeasured(MeasureActivePictureOpposingBoundaryDiagnostics(source,raw));
    }
    TEST_METHOD(InvalidSourceOrMismatchedProposedRasterUsesNoSamples)
    {
        OpposingPixels frame; BuildOpposingFixture(frame);
        const auto raw=ExtractActivePictureEvidence(frame.Source()); AssertOpposingRaw(raw);
        AssertOpposingUnmeasured(MeasureActivePictureOpposingBoundaryDiagnostics({},raw));
        for (int fault=0;fault<6;++fault)
        {
            auto source=frame.Source();
            if (fault==0) source.dataBytes=1;
            if (fault==1) source.rowBytes=1;
            if (fault==2) source.width-=2;
            if (fault==3) source.height-=2;
            if (fault==4) source.data=nullptr;
            if (fault==5) { source.format=AnalysisLumaFormat::NativeYuv422; source.encoding=VideoFrameEncoding::UNKNOWN; }
            AssertOpposingUnmeasured(MeasureActivePictureOpposingBoundaryDiagnostics(source,raw));
        }
        for (int fault=0;fault<6;++fault)
        {
            auto changed=raw;
            if (fault==0) changed.proposedBounds.left=2;
            if (fault==1) changed.proposedBounds.right-=2;
            if (fault==2) changed.proposedBounds.rasterWidth+=2;
            if (fault==3) changed.proposedBounds.rasterHeight+=2;
            if (fault==4) changed.proposedBounds.top=0;
            if (fault==5) changed.proposedBounds.bottom=changed.proposedBounds.top;
            AssertOpposingUnmeasured(MeasureActivePictureOpposingBoundaryDiagnostics(frame.Source(),changed));
        }
    }

    TEST_METHOD(UntrustedAsymmetryHorizontalConflictOrInconsistentFloorUsesNoSamples)
    {
        OpposingPixels frame; BuildOpposingFixture(frame);
        const auto raw=ExtractActivePictureEvidence(frame.Source()); AssertOpposingRaw(raw);
        for (int fault=0;fault<17;++fault)
        {
            auto changed=raw;
            if (fault==0) changed.available=false;
            if (fault==1) changed.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            if (fault==2) changed.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
            if (fault==3) changed.axisEvidence.vertical.reason=ActivePictureAxisReason::BAR_EDGE_REJECTED;
            if (fault==4) changed.axisEvidence.vertical.scanComplete=false;
            if (fault==5) changed.top.trusted=false;
            if (fault==6) changed.bottom.trusted=false;
            if (fault==7) changed.axisEvidence.horizontal.scanComplete=false;
            if (fault==8) changed.axisEvidence.horizontal.barCandidate=true;
            if (fault==9) changed.top.lumaFloor=-1;
            if (fault==10) changed.top.lumaFloor=changed.bottom.lumaFloor=81;
            if (fault==11) changed.bottom.lumaFloor-=1;
            if (fault==12) changed.axisEvidence.horizontal.state=ActivePictureAxisState::TRUSTED_BARS;
            if (fault==13) changed.axisEvidence.vertical.barCandidate=false;
            if (fault==14) changed.top.barPixels+=1;
            if (fault==15) changed.bottom.barPixels+=1;
            if (fault==16) changed.top.lumaFloor=changed.bottom.lumaFloor=std::numeric_limits<double>::quiet_NaN();
            AssertOpposingUnmeasured(MeasureActivePictureOpposingBoundaryDiagnostics(frame.Source(),changed));
        }
    }

    TEST_METHOD(DiagnosticDoesNotChangePixelsEvidenceObservationOrOrdinaryModelAuthority)
    {
        OpposingPixels frame; BuildOpposingFixture(frame,1);
        auto raw=ExtractActivePictureEvidence(frame.Source()); AssertOpposingRaw(raw);
        const auto before=MakeActivePictureObservation(raw,5,24);
        const auto pixels=frame.bytes;
        const auto* representation=reinterpret_cast<const uint8_t*>(&raw);
        const std::vector<uint8_t> objectBytes(representation,representation+sizeof(raw));
        const auto result=MeasureActivePictureOpposingBoundaryDiagnostics(frame.Source(),raw);
        AssertOpposingMeasured(result,960,540,68);
        Assert::IsTrue(frame.bytes==pixels);
        Assert::IsTrue(std::equal(objectBytes.begin(),objectBytes.end(),representation));
        const auto after=MakeActivePictureObservation(raw,5,24);
        EqualOpposingBounds(before.bounds,after.bounds);
        Assert::IsTrue(before.axisEvidence==after.axisEvidence);
        Assert::AreEqual(int(before.classification),int(after.classification));
        Assert::AreEqual(int(before.authorityOrigin),int(after.authorityOrigin));
        Assert::AreEqual(before.available,after.available);
        Assert::AreEqual(before.frameNumber,after.frameNumber);
        Assert::AreEqual(before.framesPerSecond,after.framesPerSecond);
        Assert::AreEqual(before.transitionDeferred,after.transitionDeferred);
        Assert::IsFalse(after.sparseTransitionProof.available);
        OpposingPixels initial; initial.Rectangle(0,18,960,522,300);
        const auto base=ExtractActivePictureEvidence(initial.Source());
        ActivePictureTransitionModel baseline,measured;
        for (uint64_t seq=1;seq<=4;++seq)
        {
            const auto observation=MakeActivePictureObservation(base,seq,24);
            baseline.Observe(observation); measured.Observe(observation);
        }
        for (uint64_t seq=5;seq<=16;++seq)
        {
            auto oldObservation=before; oldObservation.frameNumber=seq;
            const auto a=baseline.Observe(oldObservation);
            MeasureActivePictureOpposingBoundaryDiagnostics(frame.Source(),raw);
            const auto b=measured.Observe(MakeActivePictureObservation(raw,seq,24));
            Assert::IsFalse(a.publish || b.publish,L"Mirrored diagnostics must not repair asymmetric crop authority.");
            Assert::AreEqual(int(a.state),int(b.state)); Assert::IsTrue(a.reason==b.reason);
            EqualOpposingBounds(a.stableBounds,b.stableBounds);
            EqualOpposingBounds(base.trustedBounds,b.stableBounds);
        }
    }
};
}
