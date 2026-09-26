#include "pch.h"
#include "CppUnitTest.h"
#include <ActivePictureEvidence.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <algorithm>
#include <array>
#include <type_traits>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
namespace
{
struct SidePixels
{
    int width, height;
    bool p210;
    size_t pitch;
    std::vector<uint8_t> bytes;
    SidePixels(int w=960, int h=540, bool fullHeightChroma=true)
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
    void BothRoughSideInsets()
    {
        Rectangle(0,68,width,472,300);
        Rectangle(0,68,40,472,80,640);
        Rectangle(width-80,68,width,472,80,640);
    }
    void AliasedBrightEdges()
    {
        BothRoughSideInsets();
        Rectangle(0,68,40,472,160);
        Rectangle(width-80,68,width,472,160);
        // Deliberate counterexample: the whole-height rough scan and the
        // aperture-relative diagnostic sample different rows. Strong values
        // from the latter must not silently become a crop certificate.
        for (int i=0;i<48;++i)
        {
            const int y=((i*2+1)*height)/96;
            Rectangle(0,y,40,y+1,80,640);
            Rectangle(width-80,y,width,y+1,80,640);
        }
    }
    AnalysisLumaSource Source() const
    {
        return {bytes.data(),bytes.size(),width,height,pitch,pitch,
            p210?AnalysisLumaFormat::P210:AnalysisLumaFormat::P010,
            VideoFrameEncoding::V210,ColorSpace::REC_709,7};
    }
};

struct PackedSidePixels
{
    int width,height;
    size_t pitch;
    std::vector<uint8_t> bytes;
    explicit PackedSidePixels(const AnalysisLumaSource& source)
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

void SameSideBounds(const ActivePictureBounds& a,const ActivePictureBounds& b)
{
    Assert::AreEqual(a.left,b.left); Assert::AreEqual(a.top,b.top);
    Assert::AreEqual(a.right,b.right); Assert::AreEqual(a.bottom,b.bottom);
    Assert::AreEqual(a.rasterWidth,b.rasterWidth); Assert::AreEqual(a.rasterHeight,b.rasterHeight);
    Assert::AreEqual(a.aspectRatio,b.aspectRatio);
    Assert::AreEqual(int(a.trustedBarAxes),int(b.trustedBarAxes));
}
void SameSideProbe(const ActivePictureSideProbe& a,const ActivePictureSideProbe& b)
{
    Assert::AreEqual(a.evaluated,b.evaluated);
    for (size_t i=0;i<a.cells.size();++i)
    {
        Assert::AreEqual(a.cells[i].strong,b.cells[i].strong);
        Assert::AreEqual(a.cells[i].nonBlack,b.cells[i].nonBlack);
        Assert::AreEqual(a.cells[i].meanLuma,b.cells[i].meanLuma);
        Assert::AreEqual(a.cells[i].peakLuma,b.cells[i].peakLuma);
    }
}
void SameSideObservation(const ActivePictureObservation& a,const ActivePictureObservation& b)
{
    SameSideBounds(a.bounds,b.bounds);
    Assert::AreEqual(a.frameNumber,b.frameNumber);
    Assert::AreEqual(a.available,b.available);
    Assert::AreEqual(int(a.classification),int(b.classification));
    Assert::AreEqual(a.framesPerSecond,b.framesPerSecond);
    Assert::AreEqual(a.transitionDeferred,b.transitionDeferred);
    Assert::IsTrue(a.axisEvidence==b.axisEvidence);
    Assert::AreEqual(int(a.authorityOrigin),int(b.authorityOrigin));
    const auto& x=a.sparseTransitionProof; const auto& y=b.sparseTransitionProof;
    Assert::AreEqual(x.available,y.available);
    SameSideBounds(x.establishedBase,y.establishedBase);
    SameSideBounds(x.guardedBounds,y.guardedBounds);
    Assert::AreEqual(x.referenceId,y.referenceId);
    Assert::AreEqual(x.sourceGeneration,y.sourceGeneration);
    Assert::AreEqual(x.sourceSequence,y.sourceSequence);
}
void AssertNoSideMeasurement(const ActivePictureSideDiagnostics& result)
{
    Assert::IsFalse(result.evaluated);
    Assert::IsFalse(result.left.evaluated);
    Assert::IsFalse(result.right.evaluated);
    Assert::AreEqual(-1,result.leftMinimum);
    Assert::AreEqual(-1,result.rightMinimum);
    Assert::AreEqual(size_t(0),result.lumaSamples);
}
void AssertSideMeasurement(const ActivePictureSideDiagnostics& result,
    const ActivePictureEvidence& evidence)
{
    Assert::IsTrue(result.evaluated && result.left.evaluated && result.right.evaluated);
    Assert::AreEqual(size_t(288),result.lumaSamples);
    // The outward guard retains uncertainty rows without shifting the side
    // measurement grid. Diagnostics describe the native witness aperture.
    auto witness=evidence.trustedBounds;
    witness.top=evidence.axisEvidence.sidePictureTop;
    witness.bottom=evidence.axisEvidence.sidePictureBottom;
    witness.aspectRatio=static_cast<double>(witness.right-witness.left)/(witness.bottom-witness.top);
    Assert::AreEqual(witness.top-evidence.trustedBounds.top,evidence.axisEvidence.verticalCropGuardTop);
    Assert::AreEqual(evidence.trustedBounds.bottom-witness.bottom,evidence.axisEvidence.verticalCropGuardBottom);
    SameSideBounds(witness,result.aperture);
    Assert::AreEqual(evidence.axisEvidence.sidePictureThreshold,result.threshold);
}
}

TEST_CLASS(SidePictureDiagnosticsTests)
{
public:
    TEST_METHOD(BothRoughSideInsetsAreMeasuredWithoutFillingAuthoritativeMinima)
    {
        SidePixels frame; frame.BothRoughSideInsets();
        const auto evidence=ExtractActivePictureEvidence(frame.Source());
            const auto before=MakeActivePictureObservation(evidence,5,24);
            Assert::IsTrue(evidence.axisEvidence.HasVerifiedVerticalCropProfile(evidence.trustedBounds));
        Assert::AreEqual(40,evidence.proposedBounds.left);
        Assert::AreEqual(880,evidence.proposedBounds.right);
        Assert::AreEqual(-1,evidence.axisEvidence.leftPictureMinimum);
        Assert::AreEqual(-1,evidence.axisEvidence.rightPictureMinimum);
        Assert::IsFalse(evidence.leftSideProbe.evaluated || evidence.rightSideProbe.evaluated);
        const auto measured=MeasureActivePictureSideDiagnostics(frame.Source(),evidence);
        AssertSideMeasurement(measured,evidence);
        Assert::AreEqual(0,measured.leftMinimum);
        Assert::AreEqual(0,measured.rightMinimum);
        for (const auto* side:{&measured.left,&measured.right})
            for (const auto& cell:side->cells)
            {
                Assert::AreEqual(0,cell.strong); Assert::AreEqual(0,cell.nonBlack);
                Assert::AreEqual(80,cell.meanLuma); Assert::AreEqual(80,cell.peakLuma);
            }
        Assert::AreEqual(-1,evidence.axisEvidence.leftPictureMinimum);
        Assert::AreEqual(-1,evidence.axisEvidence.rightPictureMinimum);
        Assert::IsFalse(evidence.axisEvidence.HasBlockingFailedBar(evidence.trustedBounds));
            Assert::IsTrue(evidence.axisEvidence.horizontal.FailedBar());
            Assert::IsTrue(evidence.axisEvidence.HasVerifiedVerticalCropProfile(evidence.trustedBounds));
            SameSideObservation(before,MakeActivePictureObservation(evidence,5,24));
        static_assert(!std::is_convertible<ActivePictureSideDiagnostics,ActivePictureEvidence>::value,
            "Side diagnostics must not implicitly grant crop evidence.");
        static_assert(!std::is_convertible<ActivePictureSideDiagnostics,ActivePictureObservation>::value,
            "Side diagnostics must not implicitly grant transition observations.");
    }

    TEST_METHOD(DistributedDiagnosticSupportCannotLaunderSkippedSideAuthority)
    {
        SidePixels frame; frame.AliasedBrightEdges();
        const auto evidence=ExtractActivePictureEvidence(frame.Source());
        Assert::AreEqual(40,evidence.proposedBounds.left);
        Assert::AreEqual(880,evidence.proposedBounds.right);
        Assert::AreEqual(-1,evidence.axisEvidence.leftPictureMinimum);
        Assert::AreEqual(-1,evidence.axisEvidence.rightPictureMinimum);
        const auto result=MeasureActivePictureSideDiagnostics(frame.Source(),evidence);
        AssertSideMeasurement(result,evidence);
        Assert::AreEqual(12,result.leftMinimum);
        Assert::AreEqual(12,result.rightMinimum);
        Assert::IsFalse(evidence.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(evidence.trustedBounds));
        Assert::IsTrue(evidence.axisEvidence.HasBlockingFailedBar(evidence.trustedBounds));
        const auto observation=MakeActivePictureObservation(evidence,5,24);
        Assert::AreEqual(-1,observation.axisEvidence.leftPictureMinimum);
        Assert::AreEqual(-1,observation.axisEvidence.rightPictureMinimum);
    }

    TEST_METHOD(DiagnosticMatchesExistingProbeAtEitherDistributedPictureEdge)
    {
        for (bool brightLeft:{true,false})
        {
            SidePixels frame; frame.BothRoughSideInsets();
            frame.Rectangle(brightLeft?0:880,68,brightLeft?40:960,472,160);
            const auto evidence=ExtractActivePictureEvidence(frame.Source());
            Assert::IsTrue(evidence.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(evidence.trustedBounds));
            const auto result=MeasureActivePictureSideDiagnostics(frame.Source(),evidence);
            AssertSideMeasurement(result,evidence);
            Assert::AreEqual(12,brightLeft?result.leftMinimum:result.rightMinimum);
            Assert::AreEqual(0,brightLeft?result.rightMinimum:result.leftMinimum);
            SameSideProbe(brightLeft?evidence.leftSideProbe:evidence.rightSideProbe,
                brightLeft?result.left:result.right);
        }
    }

    TEST_METHOD(WindowboxLogoThinBorderDimMarginAndSparseCreditsLackDistributedSupport)
    {
        for (int pattern=0;pattern<5;++pattern)
        {
            SidePixels frame; frame.BothRoughSideInsets();
            if (pattern==1) frame.Rectangle(0,180,40,240,700);
            if (pattern==2) frame.Rectangle(0,68,2,472,700);
            if (pattern==3) frame.Rectangle(0,68,40,472,100);
            if (pattern==4)
                for (int y=80;y<472;y+=100) frame.Rectangle(0,y,40,y+16,700);
            const auto evidence=ExtractActivePictureEvidence(frame.Source());
            const auto before=MakeActivePictureObservation(evidence,5,24);
            Assert::IsTrue(evidence.axisEvidence.HasVerifiedVerticalCropProfile(evidence.trustedBounds));
            const auto result=MeasureActivePictureSideDiagnostics(frame.Source(),evidence);
            AssertSideMeasurement(result,evidence);
            Assert::IsTrue(result.leftMinimum<6 && result.rightMinimum<6);
            Assert::IsFalse(evidence.axisEvidence.HasBlockingFailedBar(evidence.trustedBounds));
            Assert::IsTrue(evidence.axisEvidence.horizontal.FailedBar());
            Assert::IsTrue(evidence.axisEvidence.HasVerifiedVerticalCropProfile(evidence.trustedBounds));
            SameSideObservation(before,MakeActivePictureObservation(evidence,5,24));
        }
    }

    TEST_METHOD(ZoneTelemetryDistinguishesDarkCornerFromUniformDimSide)
    {
        for (bool darkCorner:{true,false})
        {
            SidePixels frame; frame.BothRoughSideInsets();
            frame.Rectangle(0,68,40,472,darkCorner?160:100);
            if (darkCorner) frame.Rectangle(0,68,40,169,64);
            const auto evidence=ExtractActivePictureEvidence(frame.Source());
            const auto result=MeasureActivePictureSideDiagnostics(frame.Source(),evidence);
            AssertSideMeasurement(result,evidence);
            Assert::AreEqual(0,result.leftMinimum);
            for (int depth=0;depth<3;++depth)
                for (int zone=0;zone<4;++zone)
                {
                    const int expected=darkCorner?(zone==0?64:160):100;
                    const auto& cell=result.left.cells[depth*4+zone];
                    Assert::AreEqual(expected,cell.meanLuma);
                    Assert::AreEqual(expected,cell.peakLuma);
                    Assert::AreEqual(expected>112?12:0,cell.strong);
                    Assert::AreEqual(expected>88?12:0,cell.nonBlack);
                }
        }
    }

    TEST_METHOD(ExistingStrictThresholdsAreReportedWithoutReinterpretation)
    {
        for (const int value:{88,89,112,113})
        {
            SidePixels frame; frame.BothRoughSideInsets();
            frame.Rectangle(0,68,40,472,value);
            const auto evidence=ExtractActivePictureEvidence(frame.Source());
            const auto result=MeasureActivePictureSideDiagnostics(frame.Source(),evidence);
            AssertSideMeasurement(result,evidence);
            Assert::AreEqual(112,result.threshold);
            Assert::AreEqual(value>112?12:0,result.leftMinimum);
            for (const auto& cell:result.left.cells)
            {
                Assert::AreEqual(value>112?12:0,cell.strong);
                Assert::AreEqual(value>88?12:0,cell.nonBlack);
                Assert::AreEqual(value,cell.meanLuma);
                Assert::AreEqual(value,cell.peakLuma);
            }
        }
    }

    TEST_METHOD(PaddedP010P210AndNativeV210HaveIdenticalSideMeasurements)
    {
        SidePixels p010(960,540,false),p210;
        p010.BothRoughSideInsets(); p210.BothRoughSideInsets();
        p010.Rectangle(0,68,40,472,160); p210.Rectangle(0,68,40,472,160);
        PackedSidePixels packed(p210.Source());
        const auto referenceEvidence=ExtractActivePictureEvidence(p210.Source());
        const auto reference=MeasureActivePictureSideDiagnostics(p210.Source(),referenceEvidence);
        AssertSideMeasurement(reference,referenceEvidence);
        for (const auto& source:{p010.Source(),packed.Source()})
        {
            const auto evidence=ExtractActivePictureEvidence(source);
            const auto result=MeasureActivePictureSideDiagnostics(source,evidence);
            AssertSideMeasurement(result,evidence);
            SameSideBounds(reference.aperture,result.aperture);
            Assert::AreEqual(reference.threshold,result.threshold);
            Assert::AreEqual(reference.leftMinimum,result.leftMinimum);
            Assert::AreEqual(reference.rightMinimum,result.rightMinimum);
            SameSideProbe(reference.left,result.left); SameSideProbe(reference.right,result.right);
            Assert::IsTrue(referenceEvidence.axisEvidence==evidence.axisEvidence);
        }
    }

    TEST_METHOD(FourKMeasurementHasTheSameFixed288ReadBudget)
    {
        SidePixels frame(3840,2160);
        frame.Rectangle(0,280,3840,1880,300);
        frame.Rectangle(0,280,160,1880,80,640);
        frame.Rectangle(3520,280,3840,1880,80,640);
        const auto evidence=ExtractActivePictureEvidence(frame.Source());
        const auto result=MeasureActivePictureSideDiagnostics(frame.Source(),evidence);
        AssertSideMeasurement(result,evidence);
        Assert::AreEqual(0,result.leftMinimum); Assert::AreEqual(0,result.rightMinimum);
    }

    TEST_METHOD(InvalidOrUnsupportedSourceAndMismatchedApertureFailBeforeAnyRead)
    {
        SidePixels frame; frame.BothRoughSideInsets();
        const auto evidence=ExtractActivePictureEvidence(frame.Source());
        AssertNoSideMeasurement(MeasureActivePictureSideDiagnostics({},evidence));
        for (int kind=0;kind<5;++kind)
        {
            auto source=frame.Source();
            if (kind==0) source.dataBytes=1;
            if (kind==1) source.rowBytes=1;
            if (kind==2) source.width=958;
            if (kind==3) source.height=538;
            if (kind==4) { source.format=AnalysisLumaFormat::NativeYuv422; source.encoding=VideoFrameEncoding::UNKNOWN; }
            AssertNoSideMeasurement(MeasureActivePictureSideDiagnostics(source,evidence));
        }
        for (int kind=0;kind<11;++kind)
        {
            auto changed=evidence;
            if (kind==0) changed.trustedBounds.left=2;
            if (kind==1) changed.trustedBounds.right-=2;
            if (kind==2) changed.trustedBounds.top+=2;
            if (kind==3) changed.trustedBounds.bottom-=2;
            if (kind==4) changed.trustedBounds.rasterWidth+=2;
            if (kind==5) changed.trustedBounds.rasterHeight+=2;
            if (kind==6) changed.axisEvidence.sidePictureWidth+=2;
            if (kind==7) changed.axisEvidence.sidePictureHeight+=2;
            if (kind==8) changed.axisEvidence.sidePictureTop+=2;
            if (kind==9) changed.axisEvidence.sidePictureBottom-=2;
            if (kind==10) changed.trustedBounds.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH;
            AssertNoSideMeasurement(MeasureActivePictureSideDiagnostics(frame.Source(),changed));
        }
    }

    TEST_METHOD(IneligibleClassificationAxesOriginAndThresholdFailBeforeAnyRead)
    {
        SidePixels frame; frame.BothRoughSideInsets();
        const auto evidence=ExtractActivePictureEvidence(frame.Source());
        for (int kind=0;kind<12;++kind)
        {
            auto changed=evidence;
            if (kind==0) changed.available=false;
            if (kind==1) changed.classification=ActivePictureClassification::PROVISIONAL;
            if (kind==2) changed.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            if (kind==3) changed.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
            if (kind==4) changed.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_TRANSITION_EXPERIMENT;
            if (kind==5) changed.axisEvidence.horizontal.scanComplete=false;
            if (kind==6) changed.axisEvidence.horizontal.barCandidate=false;
            if (kind==7) changed.axisEvidence.horizontal.state=ActivePictureAxisState::TRUSTED_BARS;
            if (kind==8) changed.axisEvidence.vertical.scanComplete=false;
            if (kind==9) changed.axisEvidence.vertical.state=ActivePictureAxisState::UNKNOWN;
            if (kind==10) changed.axisEvidence.sidePictureThreshold=23;
            if (kind==11) changed.axisEvidence.sidePictureThreshold=129;
            AssertNoSideMeasurement(MeasureActivePictureSideDiagnostics(frame.Source(),changed));
        }
        SidePixels smallFrame(318,178);
        AssertNoSideMeasurement(MeasureActivePictureSideDiagnostics(smallFrame.Source(),evidence));
    }

    TEST_METHOD(ReadOnlySideMeasurementLeavesSourceEvidenceObservationsAndPolicyUnchanged)
    {
        SidePixels frame; frame.AliasedBrightEdges();
        auto evidence=ExtractActivePictureEvidence(frame.Source());
        const auto before=evidence;
        const auto pixelBytes=frame.bytes;
        const auto* representation=reinterpret_cast<const uint8_t*>(&evidence);
        const std::vector<uint8_t> objectBytes(representation,representation+sizeof(evidence));
        const auto observedBefore=MakeActivePictureObservation(evidence,5,24);
        const auto diagnostic=MeasureActivePictureSideDiagnostics(frame.Source(),evidence);
        AssertSideMeasurement(diagnostic,evidence);
        Assert::IsTrue(frame.bytes==pixelBytes);
        Assert::IsTrue(std::equal(objectBytes.begin(),objectBytes.end(),representation));
        Assert::IsTrue(before.reason==evidence.reason);
        SameSideObservation(observedBefore,MakeActivePictureObservation(evidence,5,24));
        SameSideObservation(observedBefore,MakeActivePictureObservation(
            ExtractActivePictureEvidence(frame.Source()),5,24));

        SidePixels basePixels; basePixels.Rectangle(0,18,960,522,300);
        const auto base=ExtractActivePictureEvidence(basePixels.Source());
        ActivePictureTransitionModel baseline,measured;
        for (uint64_t seq=1;seq<=4;++seq)
        {
            const auto observation=MakeActivePictureObservation(base,seq,24);
            baseline.Observe(observation); measured.Observe(observation);
        }
        const auto retention=EvaluateActivePicturePresentationRetention(frame.Source(),base.trustedBounds);
        for (uint64_t seq=5;seq<=20;++seq)
        {
            AlphaSourceCrop::TransitionAdmissionInput input;
            input.evidence=before; input.retention=retention;
            input.trustedGeometry=input.presentationBeforeObservation=base.trustedBounds;
            input.trustedGeometryAvailable=input.compatiblePresentation=true;
            input.trustedGeneration=input.sourceGeneration=7;
            input.sourceSequence=seq; input.framesPerSecond=24;
            input.outwardCandidate=before.trustedBounds;
            const auto original=AlphaSourceCrop::EvaluateTransitionAdmission(input);
            MeasureActivePictureSideDiagnostics(frame.Source(),evidence);
            input.evidence=evidence;
            const auto after=AlphaSourceCrop::EvaluateTransitionAdmission(input);
            SameSideObservation(original.observation,after.observation);
            const auto a=baseline.Observe(original.observation);
            const auto b=measured.Observe(after.observation);
            Assert::IsFalse(a.publish || b.publish,L"Diagnostic side values authorized a previously blocked crop.");
            Assert::AreEqual(int(a.state),int(b.state));
            Assert::AreEqual(a.matchingCandidates,b.matchingCandidates);
            Assert::AreEqual(a.clearTransition,b.clearTransition);
            Assert::AreEqual(int(a.authoritativeClassification),int(b.authoritativeClassification));
            Assert::AreEqual(int(a.authorityOrigin),int(b.authorityOrigin));
            Assert::IsTrue(a.reason==b.reason);
            SameSideBounds(a.bounds,b.bounds); SameSideBounds(a.stableBounds,b.stableBounds);
            SameSideBounds(base.trustedBounds,b.stableBounds);
        }
    }
};
}
