#include "pch.h"
#include "CppUnitTest.h"
#include <ActivePictureEvidence.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <vprenderer/BufferedPictureExpansion.h>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
namespace
{
struct DimCompositionPixels
{
    int width, height;
    bool p210;
    size_t pitch;
    std::vector<uint8_t> bytes;
    DimCompositionPixels(int w=960, int h=540, bool fullHeightChroma=true)
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

struct PackedDimCompositionPixels
{
    int width,height;
    size_t pitch;
    std::vector<uint8_t> bytes;
    explicit PackedDimCompositionPixels(const AnalysisLumaSource& source)
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

void DimScope(DimCompositionPixels& frame,int top)
{
    frame.Rectangle(0,0,960,540,64);
    frame.Rectangle(0,top,960,540-top,300);
}
void DimTintedInset(DimCompositionPixels& frame)
{
    DimScope(frame,64);
    frame.Rectangle(0,64,48,476,77,552);
    frame.Rectangle(912,64,960,476,81,552);
}
void DimNeutralInset(DimCompositionPixels& frame)
{
    DimScope(frame,64);
    frame.Rectangle(0,64,48,476,80);
    frame.Rectangle(912,64,960,476,80);
    // The rough scan stops at89. These neutral, raised margins are coherent
    // padding but lack inner contrast, so only TOP_BOTTOM becomes trusted.
    frame.Rectangle(48,64,80,476,89);
    frame.Rectangle(880,64,912,476,89);
}
// Three bright side zones plus one real but dim zone reproduce the paused
// scene without requiring any remembered-format qualification.
void ShadowedNativeSide(DimCompositionPixels& frame,bool leftPicture,int shadowZone,int shadowLuma=100)
{
    DimScope(frame,68);
    frame.Rectangle(leftPicture?912:0,68,leftPicture?960:48,472,80,552);
    frame.Rectangle(leftPicture?0:928,68+shadowZone*101,leftPicture?32:960,
        68+(shadowZone+1)*101,shadowLuma);
}
void FourKShadowedSide(DimCompositionPixels& frame)
{
    frame.Rectangle(0,0,3840,2160,64);
    frame.Rectangle(0,276,3840,1884,300);
    frame.Rectangle(3468,276,3840,1884,80,552);
    frame.Rectangle(0,1482,128,1884,100);
}
void AssertFourKShadowedRaw(const ActivePictureEvidence& observed)
{
    Assert::IsTrue(observed.available && observed.top.trusted && observed.bottom.trusted);
    Assert::IsTrue(observed.axisEvidence.horizontal.FailedBar());
    Assert::AreEqual(0,observed.proposedBounds.left); Assert::AreEqual(3468,observed.proposedBounds.right);
    Assert::AreEqual(276,observed.proposedBounds.top); Assert::AreEqual(1884,observed.proposedBounds.bottom);
    Assert::AreEqual(int(ActivePictureAuthorityOrigin::NATIVE),int(observed.authorityOrigin));
}
void AssertShadowedNativeSide(const ActivePictureEvidence& observed,bool leftPicture)
{
    Assert::IsTrue(observed.available && observed.top.trusted && observed.bottom.trusted);
    Assert::AreEqual(int(ActivePictureAuthorityOrigin::NATIVE),int(observed.authorityOrigin));
    Assert::AreEqual(int(ActivePictureClassification::BAR_CROP_TRUSTED),int(observed.classification));
    Assert::AreEqual(0,observed.trustedBounds.left); Assert::AreEqual(960,observed.trustedBounds.right);
    Assert::AreEqual(68,observed.proposedBounds.top); Assert::AreEqual(472,observed.proposedBounds.bottom);
    Assert::AreEqual(leftPicture?0:48,observed.proposedBounds.left);
    Assert::AreEqual(leftPicture?912:960,observed.proposedBounds.right);
    Assert::IsTrue(observed.axisEvidence.horizontal.FailedBar());
    Assert::IsTrue(observed.axisEvidence.vertical.state==ActivePictureAxisState::TRUSTED_BARS);
    Assert::AreEqual(0,leftPicture?observed.axisEvidence.leftPictureMinimum:observed.axisEvidence.rightPictureMinimum);
}
void EqualDimBounds(const ActivePictureBounds& a,const ActivePictureBounds& b)
{
    Assert::AreEqual(a.left,b.left); Assert::AreEqual(a.top,b.top);
    Assert::AreEqual(a.right,b.right); Assert::AreEqual(a.bottom,b.bottom);
    Assert::AreEqual(a.rasterWidth,b.rasterWidth); Assert::AreEqual(a.rasterHeight,b.rasterHeight);
    Assert::AreEqual(int(a.trustedBarAxes),int(b.trustedBarAxes));
}
void EstablishDimNative(ActivePictureTransitionModel& model,
    const ActivePictureEvidence& evidence,uint64_t& sequence)
{
    Assert::IsTrue(evidence.available);
    Assert::AreEqual(int(ActivePictureAuthorityOrigin::NATIVE),int(evidence.authorityOrigin));
    bool published=false;
    for (int i=0;i<ActivePictureTransitionModel::INITIAL_CONFIRMATIONS;++i)
    {
        const auto decision=model.Observe(MakeActivePictureObservation(evidence,sequence++,24));
        if (decision.publish) { EqualDimBounds(evidence.trustedBounds,decision.bounds); published=true; }
    }
    Assert::IsTrue(published);
}
AlphaSourceCrop::TransitionAdmissionInput DimAdmission(const AnalysisLumaSource& source,
    const ActivePictureEvidence& observed,const ActivePictureBounds& base,uint64_t sequence)
{
    AlphaSourceCrop::TransitionAdmissionInput input;
    input.evidence=observed;
    input.retention=EvaluateActivePicturePresentationRetention(source,base);
    input.trustedGeometry=input.presentationBeforeObservation=base;
    input.trustedGeometryAvailable=input.compatiblePresentation=true;
    input.trustedGeneration=input.sourceGeneration=source.generation;
    input.sourceSequence=sequence; input.framesPerSecond=24;
    input.outwardCandidate=observed.trustedBounds;
    return input;
}
void AssertDimVerifiedVerticalPreconditions(const AnalysisLumaSource& source,
    const ActivePictureEvidence& observed,const ActivePictureBounds& base)
{
    Assert::IsTrue(observed.available);
    Assert::AreEqual(int(ActivePictureClassification::BAR_CROP_TRUSTED),int(observed.classification));
    Assert::AreEqual(int(ActivePictureAuthorityOrigin::NATIVE),int(observed.authorityOrigin));
    Assert::AreEqual(48,observed.proposedBounds.left); Assert::AreEqual(912,observed.proposedBounds.right);
    Assert::AreEqual(0,observed.trustedBounds.left); Assert::AreEqual(960,observed.trustedBounds.right);
    Assert::AreEqual(62,observed.trustedBounds.top); Assert::AreEqual(478,observed.trustedBounds.bottom);
    Assert::AreEqual(int(ActivePictureBounds::BarAxes::TOP_BOTTOM),int(observed.trustedBounds.trustedBarAxes));
    Assert::IsTrue(observed.top.trusted && observed.bottom.trusted);
    Assert::IsTrue(observed.axisEvidence.vertical.state==ActivePictureAxisState::TRUSTED_BARS);
    Assert::IsTrue(observed.axisEvidence.vertical.scanComplete && observed.axisEvidence.horizontal.scanComplete);
    Assert::IsTrue(observed.axisEvidence.horizontal.FailedBar());
    Assert::IsTrue(observed.axisEvidence.horizontal.reason==ActivePictureAxisReason::BAR_EDGE_REJECTED);
    Assert::IsFalse(observed.axisEvidence.HasBlockingFailedBar(observed.trustedBounds));
    Assert::AreEqual(-1,observed.axisEvidence.leftPictureMinimum);
    Assert::AreEqual(-1,observed.axisEvidence.rightPictureMinimum);
    Assert::IsTrue(ActivePictureTransitionModel::IsSparseBoundaryInwardTransitionGeometry(base,observed.trustedBounds,2.0));
    const auto targetSafety=EvaluateActivePicturePresentationRetention(source,observed.trustedBounds);
    Assert::IsTrue(targetSafety.CanRetainPresentation() && targetSafety.excludedBandsPixelSafe);
    Assert::IsFalse(targetSafety.globalNearBlack);
    Assert::AreEqual(300.0,targetSafety.globalLumaP90);
    const auto sides=MeasureActivePictureSideDiagnostics(source,observed);
    Assert::IsTrue(sides.evaluated);
    Assert::AreEqual(112,sides.threshold);
    Assert::AreEqual(0,sides.leftMinimum); Assert::AreEqual(0,sides.rightMinimum);
}
void ConfirmDimVerticalWhilePreservingColumns(const AnalysisLumaSource& source,const ActivePictureEvidence& observed,
    const ActivePictureBounds& base,ActivePictureTransitionModel& model,uint64_t& sequence)
{
    for(int i=0;i<12;++i)
    {
        const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(DimAdmission(source,observed,base,sequence++));
        Assert::IsFalse(admission.deferPartialComposition || admission.deferPresentation || admission.deferOutward);
        const auto decision=model.Observe(admission.observation);
        const bool commit=i+1==ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS;
        Assert::AreEqual(commit,decision.publish);
        Assert::IsTrue(decision.stable);
        if(i+1<ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS)EqualDimBounds(base,decision.stableBounds);
        else EqualDimBounds(observed.trustedBounds,decision.bounds);
        Assert::AreEqual(0,decision.bounds.left);Assert::AreEqual(source.width,decision.bounds.right);
        Assert::IsFalse(decision.clearTransition);
    }
}
}
TEST_CLASS(DimSideCompositionGuardTests)
{
public:
    TEST_METHOD(VerifiedVerticalCropAcceptsCapturedTwoBrightZoneSideWithoutBrightnessQuota)
    {
        DimCompositionPixels initial(3840,2160),frame(3840,2160);
        initial.Rectangle(0,68,3840,2092,300);
        frame.Rectangle(0,276,3840,1884,300);
        frame.Rectangle(0,276,128,1884,100);
        frame.Rectangle(3388,276,3840,1884,77,510,510);
        // Actual48YUVsamples at each of x0/30/60 from paused Eternals,
        // 2026-09-25 09:10:00, renderer B009438B, source sequence505.
        // Unsampled pixels are explicit fixture blocks, not a full-frame replay.
        const int captured[3][48][3]={
            {{118,504,504},{131,506,509},{109,505,514},{108,504,506},{123,506,503},{102,503,508},{144,507,507},{125,507,506},{146,502,506},{119,501,505},{116,501,508},{93,506,508},{86,510,511},{149,493,512},{116,501,507},{127,504,507},{96,509,507},{123,501,503},{111,510,505},{115,502,505},{119,502,503},{122,505,506},{121,500,507},{125,495,510},{120,504,506},{108,505,506},{145,501,505},{106,503,506},{111,502,505},{110,499,506},{111,502,507},{117,501,510},{125,498,509},{115,503,508},{98,504,506},{101,504,506},{107,504,507},{96,507,507},{89,506,508},{101,506,507},{105,503,508},{92,504,508},{92,505,508},{125,502,505},{112,501,507},{93,507,504},{90,507,509},{87,508,508}},
            {{127,502,508},{135,504,507},{130,500,509},{112,510,507},{111,503,507},{124,504,510},{111,505,505},{131,506,506},{143,502,509},{118,502,506},{119,502,507},{100,502,508},{99,505,507},{121,499,508},{116,504,507},{131,501,508},{111,508,506},{97,505,506},{106,506,503},{119,504,507},{113,502,507},{118,502,509},{113,503,506},{107,503,507},{107,504,507},{118,507,506},{119,505,506},{103,503,507},{119,503,506},{106,504,507},{106,502,506},{119,501,507},{129,502,509},{117,503,506},{103,503,506},{105,504,508},{101,504,504},{97,505,508},{104,506,507},{102,503,507},{93,503,508},{108,504,507},{97,505,508},{93,503,508},{98,504,505},{130,502,505},{92,506,508},{87,508,509}},
            {{127,505,507},{132,502,510},{134,505,508},{140,502,508},{151,504,508},{135,504,507},{114,505,508},{138,504,506},{132,502,511},{120,507,508},{117,503,508},{121,504,506},{116,500,508},{134,504,507},{111,505,506},{162,501,508},{107,507,504},{115,522,504},{108,505,510},{118,507,505},{133,501,507},{114,502,507},{162,502,509},{94,505,505},{107,501,508},{116,509,507},{99,505,506},{93,505,506},{121,504,507},{122,501,507},{113,503,507},{120,502,510},{120,500,507},{107,503,506},{107,503,507},{96,505,507},{108,504,507},{102,501,510},{107,504,507},{95,504,507},{97,506,508},{109,508,507},{113,503,506},{95,508,506},{110,500,506},{124,501,507},{86,510,508},{88,507,507}}
        };
        for(int depth=0;depth<3;++depth)for(int i=0;i<48;++i)
        {
            const int x=depth*30,y=276+((2*i+1)*(1884-276))/96;
            frame.Code(size_t(y)*frame.pitch+size_t(x)*2,captured[depth][i][0]);
            frame.Code(frame.pitch*frame.height+size_t(y)*frame.pitch+size_t(x)*2,captured[depth][i][1]);
            frame.Code(frame.pitch*frame.height+size_t(y)*frame.pitch+size_t(x)*2+2,captured[depth][i][2]);
        }
        const auto source=frame.Source();
        const auto base=ExtractActivePictureEvidence(initial.Source());
        const auto observed=ExtractActivePictureEvidence(source);
        Assert::IsTrue(observed.top.trusted && observed.bottom.trusted);
        Assert::IsTrue(observed.axisEvidence.horizontal.FailedBar());
        Assert::AreEqual(0,observed.proposedBounds.left); Assert::AreEqual(3388,observed.proposedBounds.right);
        Assert::AreEqual(3,int(observed.axisEvidence.leftPictureStrongZoneMask));
        Assert::AreEqual(10,observed.axisEvidence.leftPictureNonBlackMinimum);
        Assert::IsTrue(observed.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds),
            L"Current verified blank vertical bands must not depend on a side brightness quota.");
        Assert::AreEqual(0,observed.trustedBounds.left); Assert::AreEqual(3840,observed.trustedBounds.right);
        Assert::AreEqual(272,observed.trustedBounds.top); Assert::AreEqual(1888,observed.trustedBounds.bottom);
        ActivePictureTransitionModel model; uint64_t sequence=1;
        EstablishDimNative(model,base,sequence);
        for(int vote=0;vote<ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS;++vote)
        {
            const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(DimAdmission(source,observed,base.trustedBounds,sequence++));
            Assert::IsFalse(admission.observation.transitionDeferred || admission.deferPartialComposition);
            const auto decision=model.Observe(admission.observation);
            Assert::AreEqual(vote+1==ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS,decision.publish);
            if(decision.publish)EqualDimBounds(observed.trustedBounds,decision.bounds);
            else EqualDimBounds(base.trustedBounds,decision.stableBounds);
        }
    }

    TEST_METHOD(VerifiedVerticalCropRetainsBothDimSideCompositionsAcrossFormats)
    {
        for(bool neutral:{false,true})
        {
            DimCompositionPixels initial,p010(960,540,false),p210;
            DimScope(initial,18);
            if(neutral){DimNeutralInset(p010);DimNeutralInset(p210);}
            else{DimTintedInset(p010);DimTintedInset(p210);}
            PackedDimCompositionPixels packed(p210.Source());
            const auto base=ExtractActivePictureEvidence(initial.Source());
            for(const auto& source:{p010.Source(),p210.Source(),packed.Source()})
            {
                const auto observed=ExtractActivePictureEvidence(source);
                Assert::IsTrue(observed.top.trusted && observed.bottom.trusted);
                Assert::IsTrue(observed.axisEvidence.horizontal.FailedBar());
                Assert::AreEqual(48,observed.proposedBounds.left);Assert::AreEqual(912,observed.proposedBounds.right);
                Assert::AreEqual(-1,observed.axisEvidence.leftPictureMinimum);
                Assert::AreEqual(-1,observed.axisEvidence.rightPictureMinimum);
                Assert::IsTrue(observed.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds),
                    L"Permit only verified vertical removal while retaining every dim side-composition pixel.");
                Assert::AreEqual(0,observed.trustedBounds.left);Assert::AreEqual(960,observed.trustedBounds.right);
                Assert::AreEqual(62,observed.trustedBounds.top);Assert::AreEqual(478,observed.trustedBounds.bottom);
                Assert::IsTrue(observed.trustedBounds.trustedBarAxes==ActivePictureBounds::BarAxes::TOP_BOTTOM);
                ActivePictureTransitionModel model;uint64_t sequence=1;
                EstablishDimNative(model,base,sequence);
                for(int vote=0;vote<ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS;++vote)
                {
                    const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(DimAdmission(source,observed,base.trustedBounds,sequence++));
                    Assert::IsFalse(admission.deferPartialComposition || admission.observation.transitionDeferred);
                    const auto decision=model.Observe(admission.observation);
                    Assert::AreEqual(vote+1==ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS,decision.publish);
                    if(decision.publish)EqualDimBounds(observed.trustedBounds,decision.bounds);
                    else EqualDimBounds(base.trustedBounds,decision.stableBounds);
                }
            }
        }
    }
    TEST_METHOD(DimTintedMarginsPreserveColumnsDuringVerifiedVerticalCrop)
    {
        DimCompositionPixels initial,frame; DimScope(initial,18); DimTintedInset(frame);
        const auto base=ExtractActivePictureEvidence(initial.Source());
        const auto observed=ExtractActivePictureEvidence(frame.Source());
        AssertDimVerifiedVerticalPreconditions(frame.Source(),observed,base.trustedBounds);
        Assert::AreEqual(77.0,observed.left.lumaP90);
        Assert::AreEqual(81.0,observed.right.lumaP90);
        Assert::IsTrue(observed.left.neutralChromaFraction<0.90 && observed.right.neutralChromaFraction<0.90);
        ActivePictureTransitionModel model; uint64_t sequence=1;
        EstablishDimNative(model,base,sequence);
        ConfirmDimVerticalWhilePreservingColumns(frame.Source(),observed,base.trustedBounds,model,sequence);
    }

    TEST_METHOD(P010P210AndNativeV210KeepDimSideCompositionPixels)
    {
        DimCompositionPixels initial,p010(960,540,false),p210;
        DimScope(initial,18); DimTintedInset(p010); DimTintedInset(p210);
        PackedDimCompositionPixels packed(p210.Source());
        const auto base=ExtractActivePictureEvidence(initial.Source());
        const auto reference=ExtractActivePictureEvidence(p210.Source());
        for (const auto& source:{p010.Source(),p210.Source(),packed.Source()})
        {
            const auto observed=ExtractActivePictureEvidence(source);
            AssertDimVerifiedVerticalPreconditions(source,observed,base.trustedBounds);
            Assert::IsTrue(reference.axisEvidence==observed.axisEvidence);
            EqualDimBounds(reference.proposedBounds,observed.proposedBounds);
            EqualDimBounds(reference.trustedBounds,observed.trustedBounds);
            ActivePictureTransitionModel model; uint64_t sequence=1;
            EstablishDimNative(model,base,sequence);
            ConfirmDimVerticalWhilePreservingColumns(source,observed,base.trustedBounds,model,sequence);
        }
    }

    TEST_METHOD(NeutralRaisedMarginsPermitOnlyVerifiedVerticalRemoval)
    {
        DimCompositionPixels initial,frame; DimScope(initial,18); DimNeutralInset(frame);
        const auto base=ExtractActivePictureEvidence(initial.Source());
        const auto observed=ExtractActivePictureEvidence(frame.Source());
        AssertDimVerifiedVerticalPreconditions(frame.Source(),observed,base.trustedBounds);
        Assert::AreEqual(80.0,observed.left.lumaP90); Assert::AreEqual(80.0,observed.right.lumaP90);
        Assert::AreEqual(1.0,observed.left.neutralChromaFraction); Assert::AreEqual(1.0,observed.right.neutralChromaFraction);
        Assert::IsTrue(observed.left.innerBoundaryContrast<10 && observed.right.innerBoundaryContrast<10);
        ActivePictureTransitionModel model; uint64_t sequence=1;
        EstablishDimNative(model,base,sequence);
        ConfirmDimVerticalWhilePreservingColumns(frame.Source(),observed,base.trustedBounds,model,sequence);
    }

    TEST_METHOD(RememberedScopeCannotTightenCurrentVerifiedVerticalGuard)
    {
        for (bool neutral:{false,true})
        {
            DimCompositionPixels initial,clean,frame; DimScope(initial,18); DimScope(clean,64);
            if (neutral) DimNeutralInset(frame); else DimTintedInset(frame);
            const auto base=ExtractActivePictureEvidence(initial.Source());
            const auto rememberedTarget=ExtractActivePictureEvidence(clean.Source());
            const auto observed=ExtractActivePictureEvidence(frame.Source());
            Assert::AreEqual(rememberedTarget.trustedBounds.top,observed.proposedBounds.top);
            Assert::AreEqual(rememberedTarget.trustedBounds.bottom,observed.proposedBounds.bottom);
            AssertDimVerifiedVerticalPreconditions(frame.Source(),observed,base.trustedBounds);
            ActivePictureTransitionModel model; uint64_t sequence=1;
            EstablishDimNative(model,rememberedTarget,sequence);
            EstablishDimNative(model,base,sequence);
            ActivePictureBounds remembered;
            Assert::IsTrue(model.FindRecentTrustedBarGeometry(observed.trustedBounds,remembered));
            EqualDimBounds(rememberedTarget.trustedBounds,remembered);
            ConfirmDimVerticalWhilePreservingColumns(frame.Source(),observed,base.trustedBounds,model,sequence);
        }
    }

    TEST_METHOD(ShadowedSidePublishesCurrentScopeWithoutRememberedHistoryAcrossFormats)
    {
        for(bool leftPicture:{true,false}) for(int shadowZone=0;shadowZone<4;++shadowZone)
        {
            DimCompositionPixels initial,p010(960,540,false),p210;
            DimScope(initial,18); ShadowedNativeSide(p010,leftPicture,shadowZone); ShadowedNativeSide(p210,leftPicture,shadowZone);
            PackedDimCompositionPixels packed(p210.Source());
            const auto base=ExtractActivePictureEvidence(initial.Source());
            for(const auto& source:{p010.Source(),p210.Source(),packed.Source()})
            {
                const auto observed=ExtractActivePictureEvidence(source);
                AssertShadowedNativeSide(observed,leftPicture);
                const auto& axes=observed.axisEvidence;
                Assert::AreEqual(15^(1<<shadowZone),int(leftPicture?axes.leftPictureStrongZoneMask:axes.rightPictureStrongZoneMask));
                Assert::AreEqual(12,leftPicture?axes.leftPictureNonBlackMinimum:axes.rightPictureNonBlackMinimum);
                Assert::IsTrue(axes.verticalCropProfileEvaluated && axes.verticalCropProfileClean);
                Assert::IsTrue(axes.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds));
                Assert::AreEqual(66,observed.trustedBounds.top); Assert::AreEqual(474,observed.trustedBounds.bottom);
                ActivePictureTransitionModel model; uint64_t sequence=1;
                EstablishDimNative(model,base,sequence);
                for(int vote=0;vote<ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS;++vote)
                {
                    const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(DimAdmission(source,observed,base.trustedBounds,sequence++));
                    Assert::IsFalse(admission.observation.transitionDeferred);
                    const auto decision=model.Observe(admission.observation);
                    Assert::AreEqual(vote+1==ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS,decision.publish);
                    if(decision.publish)EqualDimBounds(observed.trustedBounds,decision.bounds);
                    else EqualDimBounds(base.trustedBounds,decision.stableBounds);
                    Assert::AreEqual(int(ActivePictureAuthorityOrigin::NATIVE),int(decision.authorityOrigin));
                }
            }
        }
    }

    TEST_METHOD(SideDistributionCannotRejectVerifiedVerticalRemoval)
    {
        for(bool leftPicture:{true,false}) for(int pattern=0;pattern<5;++pattern)
        {
            DimCompositionPixels frame; ShadowedNativeSide(frame,leftPicture,3);
            const int first=leftPicture?0:928, last=leftPicture?32:960;
            if(pattern==0)frame.Rectangle(first,371,last,472,64); // No picture support in one whole zone.
            if(pattern==1)frame.Rectangle(first,68,last,169,100); // Only two strong zones remain.
            if(pattern==2)frame.Rectangle(first,68,last,472,100); // Uniform raised margin is not picture proof.
            if(pattern==3) // Every depth has three strong zones, but they are different zones.
            {
                frame.Rectangle(first,68,last,472,300);
                for(int depth=0;depth<3;++depth)
                {
                    const int x=leftPicture?depth*7:959-depth*7;
                    frame.Rectangle(x,68+depth*101,x+1,68+(depth+1)*101,100);
                }
            }
            if(pattern==4) // A black raster-edge pixel remains in the full-width result.
            {
                const int x=leftPicture?0:959;
                frame.Rectangle(x,68,x+1,472,64);
            }
            const auto observed=ExtractActivePictureEvidence(frame.Source());
            Assert::IsTrue(observed.available && observed.top.trusted && observed.bottom.trusted);
            Assert::IsTrue(observed.axisEvidence.horizontal.FailedBar());
            Assert::AreEqual(66,observed.trustedBounds.top); Assert::AreEqual(474,observed.trustedBounds.bottom);
            Assert::AreEqual(0,observed.trustedBounds.left);Assert::AreEqual(960,observed.trustedBounds.right);
            Assert::IsTrue(observed.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds));
            Assert::IsFalse(observed.axisEvidence.HasBlockingFailedBar(observed.trustedBounds));
        }
    }

    TEST_METHOD(SideBrightnessCountsRemainDiagnosticAtTheirExactThresholds)
    {
        for(int strongCount:{5,6}) for(int nonBlackCount:{5,6})
        {
            DimCompositionPixels frame; ShadowedNativeSide(frame,true,3);
            frame.Rectangle(0,68,32,371,100);
            frame.Rectangle(0,371,32,472,64);
            for(int zone=0;zone<4;++zone)
                for(int i=0;i<(zone<3?strongCount:nonBlackCount);++i)
                {
                    // Paint both nearby grids to keep diagnostic counts deterministic.
                    // Vertical permission depends on removed pixels, not this count.
                    for(int apertureTop:{68,66})
                    {
                        const int height=540-2*apertureTop;
                        const int y=apertureTop+((2*(zone*12+i)+1)*height)/96;
                        frame.Rectangle(0,y,32,y+1,zone<3?113:89);
                    }
                }
            const auto observed=ExtractActivePictureEvidence(frame.Source());
            AssertShadowedNativeSide(observed,true);
            for(int depth=0;depth<3;++depth)
            {
                for(int zone=0;zone<3;++zone)
                    Assert::AreEqual(strongCount,observed.leftSideProbe.cells[depth*4+zone].strong);
                Assert::AreEqual(nonBlackCount,observed.leftSideProbe.cells[depth*4+3].nonBlack);
            }
            Assert::IsTrue(observed.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds));
        }
    }

    TEST_METHOD(QuantizedShadowedSideKeepsBoundaryPixelsAcrossFormats)
    {
        DimCompositionPixels initial(3840,2160);
        initial.Rectangle(0,68,3840,2092,300);
        const auto base=ExtractActivePictureEvidence(initial.Source());
        for(int edges=1;edges<=3;++edges) for(int firstTop:{273,275})
        {
            DimCompositionPixels p010(3840,2160,false),p210(3840,2160);
            for(auto* frame:{&p010,&p210})
            {
                FourKShadowedSide(*frame);
                // These rows fall between the acquisition grid. They can be
                // genuine bright picture or dim picture, never pixels to discard.
                const int luma=firstTop==273?72:300;
                if(edges&1)frame->Rectangle(0,firstTop,3840,276,luma);
                if(edges&2)frame->Rectangle(0,1884,3840,2160-firstTop,luma);
            }
            PackedDimCompositionPixels packed(p210.Source());
            for(const auto& source:{p010.Source(),p210.Source(),packed.Source()})
            {
                const auto observed=ExtractActivePictureEvidence(source);
                AssertFourKShadowedRaw(observed);
                Assert::IsTrue(observed.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds),
                    L"Current verified bars should admit only a conservatively expanded crop that retains quantized boundary pixels.");
                const auto& target=observed.trustedBounds;
                Assert::AreEqual(0,target.left); Assert::AreEqual(3840,target.right);
                Assert::AreEqual(272,target.top); Assert::AreEqual(1888,target.bottom);
                if(edges&1)Assert::IsTrue(target.top<=firstTop);
                if(edges&2)Assert::IsTrue(target.bottom>=2160-firstTop);
                Assert::AreEqual(276,observed.axisEvidence.sidePictureTop);
                Assert::AreEqual(1884,observed.axisEvidence.sidePictureBottom);
                Assert::AreEqual(observed.axisEvidence.sidePictureTop,target.top+observed.axisEvidence.verticalCropGuardTop);
                Assert::AreEqual(observed.axisEvidence.sidePictureBottom,target.bottom-observed.axisEvidence.verticalCropGuardBottom);
                Assert::IsTrue(observed.axisEvidence.verticalCropProfileClean);
                ActivePictureTransitionModel model; uint64_t sequence=1;
                EstablishDimNative(model,base,sequence);
                for(int vote=0;vote<ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS;++vote)
                {
                    const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(DimAdmission(source,observed,base.trustedBounds,sequence++));
                    Assert::IsFalse(admission.observation.transitionDeferred);
                    const auto decision=model.Observe(admission.observation);
                    Assert::AreEqual(vote+1==ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS,decision.publish);
                    if(decision.publish)EqualDimBounds(target,decision.bounds);
                    else EqualDimBounds(base.trustedBounds,decision.stableBounds);
                }
            }
        }
    }

    TEST_METHOD(ShadowedSideRequiresStrictCurrentExcludedBandProfile)
    {
        for(bool upper:{true,false}) for(int delta:{4,5})
        {
            DimCompositionPixels frame; ShadowedNativeSide(frame,true,3);
            const int y=upper?67:472;
            frame.Rectangle(0,y,960,y+1,64+delta);
            const auto observed=ExtractActivePictureEvidence(frame.Source());
            AssertShadowedNativeSide(observed,true);
            Assert::AreEqual(7,int(observed.axisEvidence.leftPictureStrongZoneMask));
            Assert::AreEqual(12,observed.axisEvidence.leftPictureNonBlackMinimum);
            Assert::IsTrue(observed.axisEvidence.verticalCropProfileEvaluated);
            Assert::AreEqual(delta==4,observed.verticalBarProfile.clean);
            Assert::IsTrue(observed.axisEvidence.verticalCropProfileClean);
            Assert::IsTrue(observed.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds));
            Assert::AreEqual(66,observed.trustedBounds.top); Assert::AreEqual(474,observed.trustedBounds.bottom);
            Assert::IsTrue(y>=observed.trustedBounds.top && y<observed.trustedBounds.bottom,
                L"A boundary mismatch is safe only because its row remains in the displayed picture.");
        }
        DimCompositionPixels caption; ShadowedNativeSide(caption,true,3);
        // A caption deeper than the two-row envelope must still reject the
        // crop. This protects pixels that the permitted expansion cannot retain.
        caption.Rectangle(240,65,720,66,300);
        const auto observed=ExtractActivePictureEvidence(caption.Source());
        Assert::IsTrue(observed.axisEvidence.horizontal.FailedBar());
        Assert::IsFalse(observed.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds));
    }

    TEST_METHOD(ShadowedSideCertificateIsBoundToItsApertureAndCurrentProfile)
    {
        DimCompositionPixels frame; ShadowedNativeSide(frame,true,3);
        const auto observed=ExtractActivePictureEvidence(frame.Source());
        AssertShadowedNativeSide(observed,true);
        Assert::IsTrue(observed.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds));
        for(int fault=0;fault<18;++fault)
        {
            auto axes=observed.axisEvidence; auto bounds=observed.trustedBounds;
            if(fault==0)axes.verticalCropProfileEvaluated=false;
            if(fault==1)axes.verticalCropProfileClean=false;
            if(fault==2)axes.vertical.scanComplete=false;
            if(fault==3)axes.vertical.state=ActivePictureAxisState::UNKNOWN;
            if(fault==4)++bounds.top;
            if(fault==5)--bounds.bottom;
            if(fault==6)bounds.left=2;
            if(fault==7)bounds.rasterWidth*=2;
            if(fault==8)axes.horizontal.scanComplete=false;
            if(fault==9)axes.sidePictureTop+=2;
            if(fault==10)axes.sidePictureBottom-=2;
            // Even an exactly bound certificate cannot authorize an inward
            // move, a larger envelope, or a different sampling aperture.
            if(fault==11){axes.verticalCropGuardTop=-2; bounds.top=axes.sidePictureTop+2;}
            if(fault==12){axes.verticalCropGuardBottom=-2; bounds.bottom=axes.sidePictureBottom-2;}
            if(fault==13){axes.verticalCropGuardTop=4; bounds.top=axes.sidePictureTop-4;}
            if(fault==14){axes.verticalCropGuardBottom=4; bounds.bottom=axes.sidePictureBottom+4;}
            if(fault==15){axes.verticalCropGuardTop=0; axes.verticalCropGuardBottom=0;}
            if(fault==16){axes.sidePictureTop-=2; axes.sidePictureBottom-=2;}
            if(fault==17){++bounds.top; ++bounds.bottom;}
            Assert::IsFalse(axes.SupportsVerticalCropDespiteSideAmbiguity(bounds));
        }
        auto exactThreshold=observed.axisEvidence; exactThreshold.leftPictureNonBlackMinimum=-1;
        exactThreshold.leftPictureStrongZoneMask=0;exactThreshold.leftPictureMinimum=-1;
        Assert::IsTrue(exactThreshold.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds));
        auto borrowedOldRoute=observed.axisEvidence;
        borrowedOldRoute.leftPictureMinimum=12;
        borrowedOldRoute.verticalCropProfileClean=false;
        Assert::IsFalse(borrowedOldRoute.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds),
            L"An expanded aperture cannot borrow the old four-zone exemption to skip its own profile proof.");
    }

    TEST_METHOD(ShadowedSideQueuedInwardDecisionRequiresCurrentProofInEveryFrame)
    {
        DimCompositionPixels initial,frame; DimScope(initial,18); ShadowedNativeSide(frame,true,3);
        const auto base=ExtractActivePictureEvidence(initial.Source());
        const auto observed=ExtractActivePictureEvidence(frame.Source());
        AssertShadowedNativeSide(observed,true);
        ActivePictureTransitionModel model; uint64_t sequence=1;
        EstablishDimNative(model,base,sequence);
        AlphaSourceCrop::BufferedPictureExpansionSample samples[5];
        for(int i=0;i<5;++i)
        {
            const uint64_t seq=sequence+i;
            samples[i].identity={7,seq,seq,seq*1000,1,1,1};
            samples[i].observation=MakeActivePictureObservation(observed,seq,24);
            samples[i].retention=EvaluateActivePicturePresentationRetention(frame.Source(),base.trustedBounds);
            samples[i].nearBlackEvaluated=true;
        }
        const auto build=[&]() { return AlphaSourceCrop::BuildBufferedInwardDecision(samples,5,model,base.trustedBounds,5,4,1,1); };
        const auto proved=build();
        Assert::IsTrue(proved.transition.publish);
        EqualDimBounds(observed.trustedBounds,proved.transition.bounds);
        Assert::IsTrue(proved.association==ActivePictureDecisionAssociation::EXACT_INWARD);
        Assert::AreEqual(sequence,proved.effectiveIdentity.acceptedSequence);
        Assert::AreEqual(sequence+1,proved.observationIdentity.acceptedSequence);
        Assert::IsFalse(AlphaSourceCrop::BuildBufferedInwardDecision(samples,5,model,base.trustedBounds,0,4,1,1).transition.publish);
        Assert::IsFalse(AlphaSourceCrop::BuildBufferedInwardDecision(samples,5,model,base.trustedBounds,5,0,1,1).transition.publish);
        Assert::IsFalse(AlphaSourceCrop::BuildBufferedInwardDecision(samples,1,model,base.trustedBounds,5,4,1,1).transition.publish);
        for(int fault=0;fault<4;++fault)
        {
            samples[1].observation=MakeActivePictureObservation(observed,sequence+1,24);
            samples[1].retention.globalNearBlack=false;
            if(fault==0)samples[1].observation.axisEvidence.verticalCropProfileClean=false;
            if(fault==1)samples[1].observation.axisEvidence.vertical.scanComplete=false;
            if(fault==2)samples[1].observation.axisEvidence.sidePictureTop+=2;
            if(fault==3)samples[1].retention.globalNearBlack=true;
            Assert::IsFalse(build().transition.publish,L"Lookahead must not reuse another frame's verified vertical proof.");
        }
    }

    TEST_METHOD(QuantizedShadowedSideRejectsDeeperPictureCaptionsAndRamps)
    {
        for(int pattern=0;pattern<4;++pattern)
        {
            DimCompositionPixels frame(3840,2160); FourKShadowedSide(frame);
            frame.Rectangle(0,273,3840,276,72);
            frame.Rectangle(0,1884,3840,1887,72);
            if(pattern==0)frame.Rectangle(0,271,3840,272,69);
            if(pattern==1)frame.Rectangle(0,1888,3840,1889,69);
            if(pattern==2)frame.Rectangle(480,267,600,276,300);
            if(pattern==3)for(int y=255;y<276;++y)frame.Rectangle(0,y,3840,y+1,64+(y-255)/2);
            const auto observed=ExtractActivePictureEvidence(frame.Source());
            AssertFourKShadowedRaw(observed);
            Assert::IsTrue(observed.verticalBarProfile.evaluated && !observed.verticalBarProfile.clean);
            Assert::IsFalse(observed.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds));
            Assert::IsTrue(observed.axisEvidence.HasBlockingFailedBar(observed.trustedBounds));
        }
    }

    TEST_METHOD(GuardedNativeReturnDoesNotBorrowHistoricalOrQueuedUnsafeBounds)
    {
        DimCompositionPixels scope(3840,2160),imax(3840,2160),guarded(3840,2160);
        scope.Rectangle(0,276,3840,1884,300); imax.Rectangle(0,68,3840,2092,300);
        FourKShadowedSide(guarded); guarded.Rectangle(0,273,3840,276,72); guarded.Rectangle(0,1884,3840,1887,72);
        const auto clean=ExtractActivePictureEvidence(scope.Source());
        const auto base=ExtractActivePictureEvidence(imax.Source());
        const auto current=ExtractActivePictureEvidence(guarded.Source());
        AssertFourKShadowedRaw(current);
        Assert::AreEqual(272,current.trustedBounds.top); Assert::AreEqual(1888,current.trustedBounds.bottom);
        Assert::IsTrue(current.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(current.trustedBounds));
        ActivePictureTransitionModel model; uint64_t sequence=1;
        EstablishDimNative(model,clean,sequence); EstablishDimNative(model,base,sequence);
        ActivePictureBounds remembered;
        Assert::IsTrue(model.FindRecentTrustedBarGeometry(current.trustedBounds,remembered));
        EqualDimBounds(clean.trustedBounds,remembered);
        AlphaSourceCrop::BufferedPictureExpansionSample samples[2];
        for(int i=0;i<2;++i)
        {
            const uint64_t seq=sequence+i;
            samples[i].identity={7,seq,seq,seq*1000,1,1,1};
            samples[i].observation=MakeActivePictureObservation(current,seq,24);
            samples[i].retention=EvaluateActivePicturePresentationRetention(guarded.Source(),base.trustedBounds);
            samples[i].nearBlackEvaluated=true;
        }
        const auto queued=AlphaSourceCrop::BuildBufferedInwardDecision(samples,2,model,base.trustedBounds,1,1,1,1);
        Assert::IsTrue(queued.transition.publish);
        EqualDimBounds(current.trustedBounds,queued.transition.bounds);
        Assert::AreEqual(sequence,queued.effectiveIdentity.acceptedSequence);
        for(int vote=0;vote<ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS;++vote)
        {
            const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(DimAdmission(guarded.Source(),current,base.trustedBounds,sequence++));
            const auto decision=model.Observe(admission.observation);
            Assert::AreEqual(vote+1==ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS,decision.publish);
            if(decision.publish)EqualDimBounds(current.trustedBounds,decision.bounds);
        }
        // The old native coordinate and the conservative one are the same
        // established program aspect, not a reason to reframe four pixels.
        for(int i=0;i<8;++i)
        {
            const auto& evidence=i%2?current:clean;
            const auto decision=model.Observe(MakeActivePictureObservation(evidence,sequence++,24));
            Assert::IsFalse(decision.publish || decision.clearTransition);
            EqualDimBounds(current.trustedBounds,decision.stableBounds);
        }
    }

    TEST_METHOD(MixedNativeAndGuardedCandidatesCommitOnlyTheCurrentSafeAperture)
    {
        DimCompositionPixels scope(3840,2160),imax(3840,2160),guarded(3840,2160);
        scope.Rectangle(0,276,3840,1884,300); imax.Rectangle(0,68,3840,2092,300);
        FourKShadowedSide(guarded); guarded.Rectangle(0,273,3840,276,72);
        const auto clean=ExtractActivePictureEvidence(scope.Source());
        const auto base=ExtractActivePictureEvidence(imax.Source());
        const auto current=ExtractActivePictureEvidence(guarded.Source());
        Assert::AreEqual(272,current.trustedBounds.top); Assert::AreEqual(1888,current.trustedBounds.bottom);
        ActivePictureTransitionModel model; uint64_t sequence=1;
        EstablishDimNative(model,base,sequence);
        constexpr int confirmations=ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS;
        AlphaSourceCrop::BufferedPictureExpansionSample samples[confirmations];
        for(int i=0;i<confirmations;++i)
        {
            const uint64_t seq=sequence+i;
            const auto& evidence=i==0?clean:current;
            samples[i].identity={7,seq,seq,seq*1000,1,1,1};
            samples[i].observation=MakeActivePictureObservation(evidence,seq,24);
            samples[i].retention=EvaluateActivePicturePresentationRetention(i==0?scope.Source():guarded.Source(),base.trustedBounds);
            samples[i].nearBlackEvaluated=true;
        }
        Assert::IsFalse(AlphaSourceCrop::BuildBufferedInwardDecision(samples,confirmations,model,base.trustedBounds,
            confirmations-1,confirmations-1,1,1).transition.publish,L"Mixed exact apertures must not share a queued inward proof.");
        for(int vote=0;vote<confirmations;++vote)
        {
            const bool finalVote=vote+1==confirmations;
            const auto& evidence=finalVote?current:clean;
            const auto source=finalVote?guarded.Source():scope.Source();
            const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(DimAdmission(source,evidence,base.trustedBounds,sequence++));
            const auto decision=model.Observe(admission.observation);
            Assert::AreEqual(finalVote,decision.publish);
            if(decision.publish)EqualDimBounds(current.trustedBounds,decision.bounds);
        }
        RememberedEdgeReturnContext context;
        context.enabled=true; context.sourceGeneration=7;
        context.rendererGeneration=context.viewportGeneration=context.sourceFormatGeneration=context.policyGeneration=1;
        for(uint64_t scene:{uint64_t(100),uint64_t(101)}) for(int sample=0;sample<3;++sample)
        {
            context.sceneId=scene; context.sourceSequence=sequence++; context.timestampMs=context.sourceSequence*40;
            model.SetRememberedEdgeReturnContext(context);
            const auto observation=MakeActivePictureObservation(current,context.sourceSequence,24);
            const auto decision=model.Observe(observation);
            Assert::IsFalse(decision.publish || decision.clearTransition);
            EqualDimBounds(current.trustedBounds,decision.stableBounds);
            Assert::IsFalse(model.RecordIndependentNativeGeometry(observation),
                L"A safe outward envelope is not an independently measured exact boundary for training learned formats.");
        }
        // Confirm that the learning context itself is valid: the same context
        // accepts a separately established, ordinary exact native geometry.
        ActivePictureTransitionModel nativeControl; uint64_t controlSequence=1;
        EstablishDimNative(nativeControl,clean,controlSequence);
        context.sceneId=100; context.sourceSequence=controlSequence; context.timestampMs=controlSequence*40;
        nativeControl.SetRememberedEdgeReturnContext(context);
        const auto nativeObservation=MakeActivePictureObservation(clean,controlSequence,24);
        nativeControl.Observe(nativeObservation);
        Assert::IsTrue(nativeControl.RecordIndependentNativeGeometry(nativeObservation));
    }

    TEST_METHOD(RawSideWitnessSurvivesOutwardGuardSamplingPhaseChange)
    {
        DimCompositionPixels p010(3840,2160,false),p210(3840,2160);
        for(auto* frame:{&p010,&p210})
        {
            FourKShadowedSide(*frame);
            // A contiguous bright area supplies six raw-grid samples in zone0.
            // Shifting the witness grid out four rows moves its first sample
            // from292 to288 and loses one vote, without changing any pixels.
            frame->Rectangle(0,276,128,678,100);
            frame->Rectangle(0,292,128,461,300);
        }
        PackedDimCompositionPixels packed(p210.Source());
        for(const auto& source:{p010.Source(),p210.Source(),packed.Source()})
        {
            const auto observed=ExtractActivePictureEvidence(source);
            AssertFourKShadowedRaw(observed);
            for(int depth=0;depth<3;++depth)
                Assert::AreEqual(6,observed.leftSideProbe.cells[depth*4].strong);
            Assert::AreEqual(7,int(observed.axisEvidence.leftPictureStrongZoneMask));
            Assert::AreEqual(12,observed.axisEvidence.leftPictureNonBlackMinimum);
            Assert::IsTrue(observed.verticalBarGuardProfile.completed && observed.verticalBarGuardProfile.clean);
            Assert::IsTrue(observed.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds),
                L"Retaining extra boundary rows cannot invalidate current picture evidence inside the unchanged original aperture.");
            Assert::AreEqual(272,observed.trustedBounds.top); Assert::AreEqual(1888,observed.trustedBounds.bottom);
            Assert::AreEqual(276,observed.axisEvidence.sidePictureTop); Assert::AreEqual(1884,observed.axisEvidence.sidePictureBottom);
            Assert::AreEqual(observed.axisEvidence.sidePictureTop,observed.trustedBounds.top+observed.axisEvidence.verticalCropGuardTop);
            Assert::AreEqual(observed.axisEvidence.sidePictureBottom,observed.trustedBounds.bottom-observed.axisEvidence.verticalCropGuardBottom);
            const auto diagnostics=MeasureActivePictureSideDiagnostics(source,observed);
            Assert::IsTrue(diagnostics.evaluated);
            Assert::AreEqual(size_t(288),diagnostics.lumaSamples);
            Assert::AreEqual(276,diagnostics.aperture.top); Assert::AreEqual(1884,diagnostics.aperture.bottom);
            for(int depth=0;depth<3;++depth)
                Assert::AreEqual(6,diagnostics.left.cells[depth*4].strong);
        }
    }

    TEST_METHOD(RawSideWitnessKeepsLookaheadProofAcrossSamplingPhaseChanges)
    {
        DimCompositionPixels initial(3840,2160),regular(3840,2160),alias(3840,2160);
        initial.Rectangle(0,68,3840,2092,300);
        FourKShadowedSide(regular); FourKShadowedSide(alias);
        alias.Rectangle(0,276,128,678,100); alias.Rectangle(0,292,128,461,300);
        const auto base=ExtractActivePictureEvidence(initial.Source());
        const auto normal=ExtractActivePictureEvidence(regular.Source());
        const auto phaseChanged=ExtractActivePictureEvidence(alias.Source());
        EqualDimBounds(normal.trustedBounds,phaseChanged.trustedBounds);
        Assert::IsTrue(phaseChanged.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(phaseChanged.trustedBounds));
        ActivePictureTransitionModel model; uint64_t sequence=1;
        EstablishDimNative(model,base,sequence);
        AlphaSourceCrop::BufferedPictureExpansionSample samples[3];
        for(int i=0;i<3;++i)
        {
            const uint64_t seq=sequence+i;
            samples[i].identity={7,seq,seq,seq*1000,1,1,1};
            samples[i].observation=MakeActivePictureObservation(i==1?phaseChanged:normal,seq,24);
            samples[i].retention=EvaluateActivePicturePresentationRetention(i==1?alias.Source():regular.Source(),base.trustedBounds);
            samples[i].nearBlackEvaluated=true;
        }
        const auto build=[&]() { return AlphaSourceCrop::BuildBufferedInwardDecision(samples,3,model,base.trustedBounds,2,2,1,1); };
        const auto proved=build();
        Assert::IsTrue(proved.transition.publish);
        EqualDimBounds(normal.trustedBounds,proved.transition.bounds);
        Assert::IsTrue(proved.association==ActivePictureDecisionAssociation::EXACT_INWARD);
        Assert::AreEqual(sequence,proved.effectiveIdentity.acceptedSequence);
        Assert::AreEqual(sequence+1,proved.observationIdentity.acceptedSequence);
        for(int fault=0;fault<3;++fault)
        {
            samples[1].observation=MakeActivePictureObservation(phaseChanged,sequence+1,24);
            if(fault==0)samples[1].observation.axisEvidence.verticalCropProfileClean=false;
            if(fault==1)samples[1].observation.axisEvidence.sidePictureTop+=2;
            if(fault==2)samples[1].observation.axisEvidence.verticalCropGuardTop=-4;
            Assert::IsFalse(build().transition.publish,
                L"A valid later frame cannot replace missing or misbound proof on the intervening frame.");
        }
    }

    TEST_METHOD(OriginalFourZoneSideRouteKeepsItsNativeUnpaddedContract)
    {
        DimCompositionPixels frame(3840,2160); FourKShadowedSide(frame);
        frame.Rectangle(0,1482,128,1884,300);
        const auto observed=ExtractActivePictureEvidence(frame.Source());
        AssertFourKShadowedRaw(observed);
        Assert::AreEqual(12,observed.axisEvidence.leftPictureMinimum);
        Assert::AreEqual(276,observed.trustedBounds.top); Assert::AreEqual(1884,observed.trustedBounds.bottom);
        Assert::IsFalse(observed.axisEvidence.verticalCropProfileEvaluated);
        Assert::IsTrue(observed.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds));
    }

    TEST_METHOD(VerifiedVerticalCropRejectsCurrentRemovedLumaChromaAndSmallOverlays)
    {
        for(int pattern=0;pattern<4;++pattern)
        {
            DimCompositionPixels initial,frame;DimScope(initial,18);DimTintedInset(frame);
            // Row20 misses ordinary six-depth edge acquisition but is inside
            // the current removed-band profile. The32px overlay stays below
            // the rough black-line scan's five-sample picture requirement.
            if(pattern==0)frame.Rectangle(0,20,960,21,69);
            if(pattern==1)frame.Rectangle(0,20,960,21,64,518,512);
            if(pattern==2)frame.Rectangle(0,20,960,21,64,512,506);
            if(pattern==3)frame.Rectangle(400,20,432,22,700);
            const auto base=ExtractActivePictureEvidence(initial.Source());
            const auto observed=ExtractActivePictureEvidence(frame.Source());
            Assert::IsTrue(observed.top.trusted && observed.bottom.trusted);
            Assert::IsTrue(observed.axisEvidence.horizontal.FailedBar());
            Assert::AreEqual(64,observed.proposedBounds.top);
            Assert::AreEqual(476,observed.proposedBounds.bottom);
            Assert::IsTrue(observed.verticalBarProfile.evaluated && !observed.verticalBarProfile.clean);
            Assert::IsFalse(observed.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds));
            ActivePictureTransitionModel model;uint64_t sequence=1;
            EstablishDimNative(model,base,sequence);
            for(int i=0;i<6;++i)
            {
                const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(
                    DimAdmission(frame.Source(),observed,base.trustedBounds,sequence++));
                const auto decision=model.Observe(admission.observation);
                Assert::IsFalse(decision.publish || decision.clearTransition);
                EqualDimBounds(base.trustedBounds,decision.stableBounds);
            }
        }
    }

    TEST_METHOD(VerifiedVerticalCropDoesNotBypassGenuineBothAxisWindowbox)
    {
        DimCompositionPixels initial,frame;DimScope(initial,18);
        frame.Rectangle(48,64,912,476,300);
        const auto base=ExtractActivePictureEvidence(initial.Source());
        const auto observed=ExtractActivePictureEvidence(frame.Source());
        Assert::IsTrue(observed.top.trusted && observed.bottom.trusted && observed.left.trusted && observed.right.trusted);
        Assert::IsTrue(observed.trustedBounds.trustedBarAxes==ActivePictureBounds::BarAxes::BOTH);
        Assert::IsFalse(observed.verticalBarProfile.evaluated);
        Assert::IsFalse(observed.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds));
        ActivePictureTransitionModel model;uint64_t sequence=1;EstablishDimNative(model,base,sequence);
        for(int i=0;i<12;++i)
        {
            const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(
                DimAdmission(frame.Source(),observed,base.trustedBounds,sequence++));
            const auto decision=model.Observe(admission.observation);
            Assert::IsFalse(decision.publish || decision.clearTransition);
            EqualDimBounds(base.trustedBounds,decision.stableBounds);
        }
    }

    TEST_METHOD(VerifiedVerticalCropStillObeysActualNearBlackAcquisitionVeto)
    {
        DimCompositionPixels initial,frame;DimScope(initial,18);
        frame.Rectangle(0,64,960,476,90);
        frame.Rectangle(0,64,48,476,77,552,512);
        frame.Rectangle(912,64,960,476,81,552,512);
        const auto base=ExtractActivePictureEvidence(initial.Source());
        const auto observed=ExtractActivePictureEvidence(frame.Source());
        const auto darkness=EvaluateActivePictureGlobalNearBlack(frame.Source());
        Assert::IsTrue(darkness.evaluated && darkness.nearBlack);
        Assert::IsTrue(observed.top.trusted && observed.bottom.trusted);
        Assert::IsTrue(observed.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(observed.trustedBounds));
        const auto constrained=ConstrainNearBlackCropAcquisition(observed,darkness.nearBlack);
        Assert::IsFalse(constrained.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
        ActivePictureTransitionModel model;uint64_t sequence=1;EstablishDimNative(model,base,sequence);
        for(int i=0;i<6;++i)
        {
            const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(
                DimAdmission(frame.Source(),constrained,base.trustedBounds,sequence++));
            const auto decision=model.Observe(admission.observation);
            Assert::IsFalse(decision.publish || decision.clearTransition);
            EqualDimBounds(base.trustedBounds,decision.stableBounds);
        }
    }
    TEST_METHOD(CompleteNativePictureStillConfirmsInwardAndReturnsToImax)
    {
        DimCompositionPixels initial,scope; DimScope(initial,18); DimScope(scope,64);
        const auto base=ExtractActivePictureEvidence(initial.Source());
        const auto narrow=ExtractActivePictureEvidence(scope.Source());
        Assert::IsFalse(base.axisEvidence.HasFailedBar() || narrow.axisEvidence.HasFailedBar());
        Assert::IsTrue(narrow.axisEvidence.horizontal.state==ActivePictureAxisState::FULL_EXTENT_SUPPORTED);
        ActivePictureTransitionModel model; uint64_t sequence=1;
        EstablishDimNative(model,base,sequence);
        for (int i=0;i<ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS;++i)
        {
            const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(
                DimAdmission(scope.Source(),narrow,base.trustedBounds,sequence++));
            Assert::IsFalse(admission.observation.transitionDeferred);
            const auto decision=model.Observe(admission.observation);
            Assert::AreEqual(i+1==ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS,decision.publish);
            if (decision.publish) EqualDimBounds(narrow.trustedBounds,decision.bounds);
            else EqualDimBounds(base.trustedBounds,decision.stableBounds);
        }
        AlphaSourceCrop::OutwardPictureConfirmationState outward;
        const int required=AlphaSourceCrop::OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED+
            ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS-1;
        for (int i=0;i<required;++i)
        {
            auto input=DimAdmission(initial.Source(),base,narrow.trustedBounds,sequence++);
            input.previousOutward=outward;
            const auto admission=AlphaSourceCrop::EvaluateTransitionAdmission(input);
            outward=admission.outward.state;
            Assert::IsTrue(admission.outward.broadOpposingPicture);
            const auto decision=model.Observe(admission.observation);
            Assert::AreEqual(i+1==required,decision.publish);
            if (decision.publish)
            {
                EqualDimBounds(base.trustedBounds,decision.bounds);
                Assert::AreEqual(int(ActivePictureAuthorityOrigin::NATIVE),int(decision.authorityOrigin));
            }
            else EqualDimBounds(narrow.trustedBounds,decision.stableBounds);
        }
    }
};
}
