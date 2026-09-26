#include "pch.h"
#include "CppUnitTest.h"
#include <RememberedEdgeReturn.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <algorithm>
#include <vector>
#include <chrono>
#include <sstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
namespace
{
struct RecallPixels
{
    int width, height;
    bool p210;
    size_t pitch;
    std::vector<uint8_t> bytes;
    RecallPixels(int w=960, int h=540, bool fullHeightChroma=true)
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

struct PackedRecallPixels
{
    int width,height;
    size_t pitch;
    std::vector<uint8_t> bytes;
    explicit PackedRecallPixels(const AnalysisLumaSource& source)
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

int RecallTop(const RecallPixels& frame) { return frame.height==2160?276:68; }
int RecallBrightBottom(const RecallPixels& frame) { return frame.height==2160?1612:402; }
void RecallScope(RecallPixels& frame,int top=-1,int bottom=-1)
{
    if(top<0)top=RecallTop(frame); if(bottom<0)bottom=frame.height-top;
    frame.Rectangle(0,0,frame.width,frame.height,64);
    frame.Rectangle(0,top,frame.width,bottom,300);
}
void RecallWide(RecallPixels& frame)
{
    const int top=frame.height==2160?68:18;
    RecallScope(frame,top,frame.height-top);
}
void RecallDarkLowerPicture(RecallPixels& frame)
{
    frame.Rectangle(0,0,frame.width,frame.height,64);
    frame.Rectangle(0,RecallTop(frame),frame.width,RecallBrightBottom(frame),300);
}
void EqualRecallBounds(const ActivePictureBounds& a,const ActivePictureBounds& b)
{
    Assert::AreEqual(a.left,b.left); Assert::AreEqual(a.top,b.top);
    Assert::AreEqual(a.right,b.right); Assert::AreEqual(a.bottom,b.bottom);
    Assert::AreEqual(a.rasterWidth,b.rasterWidth); Assert::AreEqual(a.rasterHeight,b.rasterHeight);
    Assert::AreEqual(int(a.trustedBarAxes),int(b.trustedBarAxes));
}
void AssertRecallRaw(const ActivePictureEvidence& raw)
{
    Assert::IsTrue(raw.available && raw.classification==ActivePictureClassification::PROVISIONAL);
    Assert::IsTrue(raw.authorityOrigin==ActivePictureAuthorityOrigin::NATIVE);
    Assert::IsTrue(raw.axisEvidence.vertical.reason==ActivePictureAxisReason::BAR_ASYMMETRY);
    Assert::IsTrue(raw.axisEvidence.vertical.scanComplete && raw.axisEvidence.horizontal.scanComplete);
    Assert::IsFalse(raw.axisEvidence.horizontal.barCandidate || raw.axisEvidence.horizontal.FailedBar());
}
void AssertUnchangedRecallEvidence(const ActivePictureEvidence& raw,const ActivePictureEvidence& other)
{
    EqualRecallBounds(raw.proposedBounds,other.proposedBounds);
    EqualRecallBounds(raw.trustedBounds,other.trustedBounds);
    Assert::AreEqual(raw.available,other.available);
    Assert::AreEqual(int(raw.classification),int(other.classification));
    Assert::AreEqual(int(raw.authorityOrigin),int(other.authorityOrigin));
    Assert::IsTrue(raw.axisEvidence==other.axisEvidence);
    Assert::IsTrue(raw.reason==other.reason);
    Assert::AreEqual(raw.lumaSamples,other.lumaSamples);
    Assert::AreEqual(raw.chromaSamples,other.chromaSamples);
    Assert::AreEqual(raw.rememberedEdgeReturnProof.available,other.rememberedEdgeReturnProof.available);
}
struct RecallRig
{
    RecallPixels scope,wide;
    ActivePictureEvidence scopeEvidence,wideEvidence;
    ActivePictureTransitionModel model;
    RememberedEdgeReturnContext context;
    explicit RecallRig(int w=960,int h=540):scope(w,h),wide(w,h)
    {
        RecallScope(scope); RecallWide(wide);
        scopeEvidence=ExtractActivePictureEvidence(scope.Source());
        wideEvidence=ExtractActivePictureEvidence(wide.Source());
        context.enabled=true; context.sourceGeneration=7;
        context.rendererGeneration=11; context.viewportGeneration=13;
        context.sourceFormatGeneration=17; context.policyGeneration=19;
    }
    void Advance(uint64_t scene=4)
    {
        ++context.sourceSequence; context.timestampMs+=40;
        context.sceneId=scene; context.cadenceRepeat=false; context.discontinuity=false;
        model.SetRememberedEdgeReturnContext(context);
    }
    ActivePictureTransitionDecision FeedNative(const RecallPixels& pixels,uint64_t scene,bool learn=true)
    {
        Advance(scene);
        const auto raw=ExtractActivePictureEvidence(pixels.Source());
        Assert::IsTrue(raw.available && raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
        Assert::IsTrue(raw.authorityOrigin==ActivePictureAuthorityOrigin::NATIVE);
        const auto observation=MakeActivePictureObservation(raw,context.sourceSequence,24);
        const auto decision=model.Observe(observation);
        if(learn)model.RecordIndependentNativeGeometry(observation);
        return decision;
    }
    void Establish(const RecallPixels& pixels,uint64_t scene,bool learn=true)
    {
        bool published=false;
        for(int i=0;i<ActivePictureTransitionModel::INITIAL_CONFIRMATIONS+2;++i)
            published=FeedNative(pixels,scene,learn).publish||published;
        Assert::IsTrue(published,L"Fixture must establish its geometry through ordinary native authority.");
    }
    void Learn(unsigned scenes=2)
    {
        Establish(scope,1);
        if(scenes>=2)for(int i=0;i<3;++i)FeedNative(scope,2);
    }
    void QualifiedWide()
    {
        Learn(); Establish(wide,3);
    }
    RememberedEdgeReturnNomination Nominate(const ActivePictureEvidence& raw) const
    {
        return model.NominateRememberedEdgeReturn(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
    }
    RememberedEdgeReturnResult Inspect(const RecallPixels& pixels,uint64_t scene=4)
    {
        Advance(scene);
        const auto raw=ExtractActivePictureEvidence(pixels.Source()); AssertRecallRaw(raw);
        const auto nomination=Nominate(raw);
        Assert::IsTrue(nomination.available,L"Two independently native scenes must nominate this exact prior geometry.");
        return InspectRememberedEdgeReturn(pixels.Source(),raw,nomination,context.sourceSequence,context.timestampMs);
    }
    AlphaSourceCrop::TransitionAdmissionDecision Admit(const AnalysisLumaSource& source,
        const ActivePictureEvidence& evidence,const ActivePictureBounds& base,
        const AlphaSourceCrop::OutwardPictureConfirmationState& previous={}) const
    {
        AlphaSourceCrop::TransitionAdmissionInput input;
        input.evidence=evidence; input.retention=EvaluateActivePicturePresentationRetention(source,base);
        input.trustedGeometry=input.presentationBeforeObservation=base;
        input.trustedGeometryAvailable=input.compatiblePresentation=true;
        input.trustedGeneration=input.sourceGeneration=source.generation;
        input.sourceSequence=context.sourceSequence; input.framesPerSecond=24;
        input.outwardCandidate=evidence.trustedBounds; input.previousOutward=previous;
        return AlphaSourceCrop::EvaluateTransitionAdmission(input);
    }
    ActivePictureTransitionDecision Return(const RecallPixels& pixels)
    {
        ActivePictureTransitionDecision finalDecision;
        for(int vote=0;vote<ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS;++vote)
        {
            const auto result=Inspect(pixels);
            Assert::IsTrue(result.candidateAvailable,L"Current bar-profile proof must support the qualified dark return.");
            const auto admission=Admit(pixels.Source(),result.evidence,wideEvidence.trustedBounds);
            Assert::IsFalse(admission.observation.transitionDeferred);
            finalDecision=model.Observe(admission.observation);
            Assert::AreEqual(vote+1==ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS,finalDecision.publish);
            if(!finalDecision.publish)EqualRecallBounds(wideEvidence.trustedBounds,finalDecision.stableBounds);
        }
        EqualRecallBounds(scopeEvidence.trustedBounds,finalDecision.bounds);
        Assert::IsTrue(finalDecision.authorityOrigin==ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN);
        return finalDecision;
    }
};
}

TEST_CLASS(RememberedEdgeProfileTests)
{
public:
    TEST_METHOD(QualifiedScopeReturnsWithZeroOppositeBoundaryContrast)
    {
        RecallRig rig(3840,2160); rig.QualifiedWide();
        RecallPixels frame(3840,2160); RecallDarkLowerPicture(frame);
        const auto raw=ExtractActivePictureEvidence(frame.Source()); AssertRecallRaw(raw);
        const auto diagnostic=MeasureActivePictureOpposingBoundaryDiagnostics(frame.Source(),raw);
        Assert::IsTrue(diagnostic.evaluated); Assert::AreEqual(0.0,diagnostic.bottomAdjacent.contrast);
        Assert::IsFalse(diagnostic.bottom.trusted,L"This positive must not accidentally require fresh bottom acquisition contrast.");
        const auto result=rig.Inspect(frame);
        Assert::IsTrue(result.candidateAvailable);
        Assert::AreEqual(64.0,result.referenceY); Assert::AreEqual(512.0,result.referenceU);
        Assert::AreEqual(512.0,result.referenceV); Assert::AreEqual(0.0,result.referenceDispersion);
        Assert::AreEqual(0,result.mismatches); Assert::AreEqual(96,result.matchedEdgeSupport);
        const int step=(std::max)(1,frame.height/540);
        const int top=rig.scopeEvidence.trustedBounds.top;
        const int bottomDepth=frame.height-rig.scopeEvidence.trustedBounds.bottom;
        const size_t exteriorBudget=size_t(96)*((top+step-1)/step+(bottomDepth+step-1)/step+32);
        Assert::IsTrue(result.exteriorSamples>0 && result.exteriorSamples<=exteriorBudget);
        Assert::IsTrue(result.samples>result.exteriorSamples && result.samples<size_t(150000));
        EqualRecallBounds(rig.scopeEvidence.trustedBounds,result.evidence.trustedBounds);
        EqualRecallBounds(raw.proposedBounds,result.evidence.proposedBounds);
        Assert::IsTrue(raw.axisEvidence==result.evidence.axisEvidence);
        Assert::IsTrue(result.evidence.rememberedEdgeReturnProof.available);
        Assert::IsTrue(result.evidence.authorityOrigin==ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN);
    }

    TEST_METHOD(RememberedMatchedEdgeAllowsOnlySmallChromaFringeAcrossFormats)
    {
        for(bool bottom:{false,true}) for(bool p210:{false,true})
        {
            RecallRig rig; rig.QualifiedWide(); RecallPixels frame(960,540,p210);
            RecallDarkLowerPicture(frame);
            if(bottom)for(int y=0;y<frame.height/2;++y)
                std::swap_ranges(frame.bytes.begin()+size_t(y)*frame.pitch,frame.bytes.begin()+size_t(y+1)*frame.pitch,
                    frame.bytes.begin()+size_t(frame.height-1-y)*frame.pitch);
            const auto target=rig.scopeEvidence.trustedBounds;
            const int row=bottom?target.bottom:target.top-1;
            frame.Rectangle(0,row,frame.width,row+1,68,523,502);
            rig.Advance(); const auto raw=ExtractActivePictureEvidence(frame.Source()); AssertRecallRaw(raw);
            const auto nomination=rig.Nominate(raw); Assert::IsTrue(nomination.available);
            const auto check=[&](const AnalysisLumaSource& source) {
                const auto observed=ExtractActivePictureEvidence(source); AssertRecallRaw(observed);
                const auto result=InspectRememberedEdgeReturn(source,observed,nomination,rig.context.sourceSequence,rig.context.timestampMs);
                Assert::IsTrue(result.candidateAvailable,L"Small chroma fringe at the independently matched edge must not defeat a qualified return.");
                EqualRecallBounds(target,result.evidence.trustedBounds);
                Assert::AreEqual(0,result.mismatches);
                Assert::AreEqual(source.format==AnalysisLumaFormat::P010?192:96,result.edgeFringeSamples);
                Assert::AreEqual(bottom?target.bottom:target.top-(source.format==AnalysisLumaFormat::P010?2:1),result.firstFringeY);
                Assert::AreEqual(bottom?target.bottom+(source.format==AnalysisLumaFormat::P010?1:0):target.top-1,result.lastFringeY);
                Assert::IsTrue(observed.axisEvidence==result.evidence.axisEvidence);
            };
            check(frame.Source());
            if(p210){PackedRecallPixels packed(frame.Source());check(packed.Source());}
        }
    }

    TEST_METHOD(RememberedMatchedEdgeFringeDoesNotRelaxLumaColorOrSpatialLimits)
    {
        for(bool bottom:{false,true})for(bool p210:{false,true})for(int fault=0;fault<5;++fault)
        {
            RecallRig rig; rig.QualifiedWide(); RecallPixels frame(960,540,p210); RecallDarkLowerPicture(frame);
            if(bottom)for(int y=0;y<frame.height/2;++y)
                std::swap_ranges(frame.bytes.begin()+size_t(y)*frame.pitch,frame.bytes.begin()+size_t(y+1)*frame.pitch,
                    frame.bytes.begin()+size_t(frame.height-1-y)*frame.pitch);
            const auto target=rig.scopeEvidence.trustedBounds;
            int row=bottom?target.bottom:target.top-1,y=64,u=523;
            if(fault==0)y=69; // Actual luma, even immediately next to the matched edge.
            if(fault==1)u=525; // Chroma beyond the narrow fringe budget.
            if(fault==2)row=bottom?target.bottom+(p210?1:2):target.top-(p210?2:3);
            if(fault==3)row=bottom?target.top-1:target.bottom; // Opposite edge stays strict.
            if(fault==4){row=bottom?target.bottom+20:target.top-20;u=520;} // Overlay deeper in bar.
            frame.Rectangle(0,row,frame.width,row+1,y,u,512);
            const auto result=rig.Inspect(frame);
            Assert::IsFalse(result.candidateAvailable,L"Qualified history must not excuse current picture or overlay evidence.");
            // A deeper colored overlay can also contaminate the independently sampled reference.
            Assert::IsTrue(result.mismatches>0 || std::string(result.reason)=="reference-not-clean");
        }
    }

    TEST_METHOD(RememberedP010FringeFollowsChromaPairAtOddBoundary)
    {
        // The native scanner steps by three at this resolution, so odd boundaries
        // qualify through the real extractor and history, without invented metadata.
        for(bool bottom:{false,true})for(bool deeper:{false,true})
        {
            RecallRig rig(2880,1620);
            RecallScope(rig.scope,207,1413);RecallScope(rig.wide,51,1569);
            rig.scopeEvidence=ExtractActivePictureEvidence(rig.scope.Source());
            rig.wideEvidence=ExtractActivePictureEvidence(rig.wide.Source());
            rig.QualifiedWide();
            Assert::AreEqual(207,rig.scopeEvidence.trustedBounds.top);
            Assert::AreEqual(1413,rig.scopeEvidence.trustedBounds.bottom);
            RecallPixels frame(2880,1620,false);
            frame.Rectangle(0,bottom?411:207,2880,bottom?1413:1209,300);
            const int row=bottom?(deeper?1414:1413):(deeper?205:206);
            frame.Rectangle(0,row,2880,row+1,64,524,500);
            const auto result=rig.Inspect(frame);
            Assert::AreEqual(!deeper,result.candidateAvailable);
            if(!deeper) {
                Assert::AreEqual(96,result.edgeFringeSamples);
                Assert::AreEqual(row,result.firstFringeY);Assert::AreEqual(row,result.lastFringeY);
                EqualRecallBounds(rig.scopeEvidence.trustedBounds,result.evidence.trustedBounds);
            }
        }
    }

    TEST_METHOD(RememberedMatchedEdgeChecksEachSignedChromaLimit)
    {
        for(bool bottom:{false,true})for(bool p210:{false,true})
        for(bool vChannel:{false,true})for(int delta:{-13,-12,12,13})
        {
            RecallRig rig;rig.QualifiedWide();RecallPixels frame(960,540,p210);RecallDarkLowerPicture(frame);
            if(bottom)for(int y=0;y<frame.height/2;++y)
                std::swap_ranges(frame.bytes.begin()+size_t(y)*frame.pitch,frame.bytes.begin()+size_t(y+1)*frame.pitch,
                    frame.bytes.begin()+size_t(frame.height-1-y)*frame.pitch);
            const auto target=rig.scopeEvidence.trustedBounds;
            const int row=bottom?target.bottom:target.top-1;
            frame.Rectangle(0,row,960,row+1,64,vChannel?512:512+delta,vChannel?512+delta:512);
            const auto result=rig.Inspect(frame);
            Assert::AreEqual(std::abs(delta)==12,result.candidateAvailable);
            if(std::abs(delta)==12)Assert::AreEqual(p210?96:192,result.edgeFringeSamples);
            else Assert::IsTrue(result.mismatches>0);
        }
    }

    TEST_METHOD(RememberedMatchedEdgeFringeCannotMaskSeparateCaption)
    {
        RecallRig rig(3840,2160); rig.QualifiedWide(); RecallPixels frame(3840,2160); RecallDarkLowerPicture(frame);
        frame.Rectangle(0,275,3840,276,68,523,512);
        frame.Rectangle(1750,1925,1950,1933,700);
        const auto result=rig.Inspect(frame);
        Assert::IsFalse(result.candidateAvailable);
        Assert::IsTrue(result.mismatches>0);
    }

    TEST_METHOD(RememberedMatchedEdgeFringePublishesOnlyAfterExistingConfirmation)
    {
        RecallRig rig(3840,2160); rig.QualifiedWide(); RecallPixels frame(3840,2160); RecallDarkLowerPicture(frame);
        frame.Rectangle(0,275,3840,276,68,523,512);
        rig.Return(frame);
    }

    TEST_METHOD(CurrentBottomEdgeCanReturnWithAnEntirelyBlackUpperPicture)
    {
        RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
        for(int y=0;y<frame.height/2;++y)
            std::swap_ranges(frame.bytes.begin()+size_t(y)*frame.pitch,frame.bytes.begin()+size_t(y+1)*frame.pitch,
                frame.bytes.begin()+size_t(frame.height-1-y)*frame.pitch);
        const auto result=rig.Inspect(frame);
        Assert::IsTrue(result.candidateAvailable);
        Assert::IsTrue(result.evidence.rememberedEdgeReturnProof.nomination.matchedEdge==RememberedEdge::BOTTOM);
        EqualRecallBounds(rig.scopeEvidence.trustedBounds,result.evidence.trustedBounds);
    }

    TEST_METHOD(DisplacedDimGroundAndSmoothRampFailDespiteQualifiedHistoryAndGenericSafety)
    {
        for(int pattern=0;pattern<2;++pattern)
        {
            RecallRig rig(3840,2160); rig.QualifiedWide(); RecallPixels frame(3840,2160); RecallDarkLowerPicture(frame);
            if(pattern==0)frame.Rectangle(0,1612,3840,1908,80);
            else
            {
                frame.Rectangle(0,1612,3840,1848,84);
                for(int y=1848;y<2048;++y)frame.Rectangle(0,y,3840,y+1,84-(y-1848)*20/200);
            }
            const auto raw=ExtractActivePictureEvidence(frame.Source()); AssertRecallRaw(raw);
            Assert::IsTrue(EvaluateActivePicturePresentationRetention(frame.Source(),rig.scopeEvidence.trustedBounds).CanRetainPresentation());
            const auto result=rig.Inspect(frame);
            Assert::IsFalse(result.candidateAvailable);
            Assert::IsTrue(result.mismatches>0,L"Reject the actual prospective bar profile, not absence of bright bottom picture.");
            AssertUnchangedRecallEvidence(raw,result.evidence);
        }
    }

    TEST_METHOD(EveryImmediateExcludedRowProtectsOneAndTwoRowPictureOrCaptions)
    {
        for(int rows:{1,2})for(int luma:{80,700})
        {
            RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
            const int bottom=rig.scopeEvidence.trustedBounds.bottom;
            frame.Rectangle(0,bottom,luma==700?40:960,bottom+rows,luma);
            const auto result=rig.Inspect(frame);
            Assert::IsFalse(result.candidateAvailable);
            Assert::IsTrue(result.mismatches>=rows*(luma==700?3:96));
            Assert::IsTrue(result.evidence.classification==ActivePictureClassification::PROVISIONAL);
        }
    }

    TEST_METHOD(CurrentChromaAndNoiseMismatchesCannotHideBelowTheBlackCutoff)
    {
        for(int pattern=0;pattern<2;++pattern)
        {
            RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
            const int bottom=rig.scopeEvidence.trustedBounds.bottom;
            if(pattern==0)frame.Rectangle(0,bottom,960,bottom+2,64,560,512);
            else for(int x=0;x<960;x+=20)frame.Rectangle(x,bottom,(std::min)(960,x+20),bottom+2,(x/20)%2?54:74);
            const auto result=rig.Inspect(frame);
            Assert::IsFalse(result.candidateAvailable); Assert::IsTrue(result.mismatches>0);
        }
    }

    TEST_METHOD(NoisyReferenceAndLocalizedMatchedEdgeCannotCreateAProfileCertificate)
    {
        for(int pattern=0;pattern<2;++pattern)
        {
            RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
            const int top=rig.scopeEvidence.trustedBounds.top;
            if(pattern==0)
                for(int x=0;x<960;x+=20)frame.Rectangle(x,0,(std::min)(960,x+20),top,(x/20)%2?60:68);
            else
            {
                frame.Rectangle(0,top,960,top+8,64);
                frame.Rectangle(0,top,240,top+8,300);
            }
            const auto raw=ExtractActivePictureEvidence(frame.Source()); AssertRecallRaw(raw);
            Assert::IsTrue(raw.top.trusted,L"Fixture must reach stricter return-profile validation after native top trust.");
            const auto result=rig.Inspect(frame); Assert::IsFalse(result.candidateAvailable);
            if(pattern==0)Assert::IsTrue(result.referenceDispersion>4);
            else Assert::IsTrue(result.matchedEdgeSupport<48);
        }
    }

    TEST_METHOD(P010P210AndNativeV210ReturnTheSameFixedRememberedBounds)
    {
        RecallRig rig; rig.QualifiedWide(); RecallPixels p010(960,540,false),p210;
        RecallDarkLowerPicture(p010); RecallDarkLowerPicture(p210); PackedRecallPixels packed(p210.Source());
        rig.Advance(); const auto raw=ExtractActivePictureEvidence(p210.Source()); const auto nomination=rig.Nominate(raw);
        Assert::IsTrue(nomination.available);
        for(const auto& source:{p010.Source(),p210.Source(),packed.Source()})
        {
            const auto observed=ExtractActivePictureEvidence(source); AssertRecallRaw(observed);
            const auto result=InspectRememberedEdgeReturn(source,observed,nomination,rig.context.sourceSequence,rig.context.timestampMs);
            Assert::IsTrue(result.candidateAvailable);
            EqualRecallBounds(rig.scopeEvidence.trustedBounds,result.evidence.trustedBounds);
            Assert::AreEqual(64.0,result.referenceY); Assert::AreEqual(96,result.matchedEdgeSupport);
            Assert::AreEqual(0,result.mismatches);
        }
    }

    TEST_METHOD(InvalidIdentityUnqualifiedNominationAndSideConflictCannotReadmit)
    {
        RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
        rig.Advance(); const auto raw=ExtractActivePictureEvidence(frame.Source()); const auto nomination=rig.Nominate(raw);
        Assert::IsTrue(nomination.available);
        for(int fault=0;fault<10;++fault)
        {
            auto source=frame.Source(); auto changed=nomination; auto observed=raw;
            if(fault==0)changed.available=false;
            if(fault==1)changed.confirmedSceneCount=1;
            if(fault==2)changed.historyId=0;
            if(fault==3)++changed.sourceGeneration;
            if(fault==4)++changed.sourceSequence;
            if(fault==5)++changed.timestampMs;
            if(fault==6)source.dataBytes=1;
            if(fault==7)observed.axisEvidence.horizontal.barCandidate=true;
            if(fault==8)observed.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
            if(fault==9)changed.observedEdgeCoordinate+=2;
            const auto result=InspectRememberedEdgeReturn(source,observed,changed,rig.context.sourceSequence,rig.context.timestampMs);
            Assert::IsFalse(result.candidateAvailable); Assert::AreEqual(size_t(0),result.samples);
            AssertUnchangedRecallEvidence(observed,result.evidence);
        }
    }

    TEST_METHOD(ProfileInspectionCannotMutateSourcePixelsOrRawAuthority)
    {
        RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
        rig.Advance(); auto raw=ExtractActivePictureEvidence(frame.Source()); const auto before=raw;
        const auto pixels=frame.bytes; const auto nomination=rig.Nominate(raw);
        const auto result=InspectRememberedEdgeReturn(frame.Source(),raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
        Assert::IsTrue(result.candidateAvailable);
        Assert::IsTrue(frame.bytes==pixels); AssertUnchangedRecallEvidence(before,raw);
        AssertUnchangedRecallEvidence(before,ExtractActivePictureEvidence(frame.Source()));
        Assert::IsTrue(raw.axisEvidence.vertical.FailedBar());
        Assert::IsTrue(result.evidence.axisEvidence.vertical.FailedBar(),L"Typed proof must not erase native failed-axis metadata.");
    }

    TEST_METHOD(CurrentCompleteNativeExpansionAlwaysWinsOverRememberedHypothesis)
    {
        RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
        rig.Advance(); const auto raw=ExtractActivePictureEvidence(frame.Source()); const auto nomination=rig.Nominate(raw);
        Assert::IsTrue(nomination.available);
        const auto native=ExtractActivePictureEvidence(rig.wide.Source());
        const auto result=InspectRememberedEdgeReturn(rig.wide.Source(),native,nomination,rig.context.sourceSequence,rig.context.timestampMs);
        Assert::IsFalse(result.candidateAvailable); Assert::AreEqual(size_t(0),result.samples);
        AssertUnchangedRecallEvidence(native,result.evidence);
    }
};

TEST_CLASS(RememberedEdgeQualificationTests)
{
public:
    TEST_METHOD(TwoConfirmedScenesEachRequireThreeIndependentNativeFrames)
    {
        for(int secondSceneFrames=0;secondSceneFrames<=3;++secondSceneFrames)
        {
            RecallRig rig; rig.Establish(rig.scope,1);
            for(int i=0;i<secondSceneFrames;++i)rig.FeedNative(rig.scope,2);
            rig.Establish(rig.wide,3); RecallPixels frame; RecallDarkLowerPicture(frame);
            rig.Advance(); const auto raw=ExtractActivePictureEvidence(frame.Source()); const auto nomination=rig.Nominate(raw);
            Assert::AreEqual(secondSceneFrames==3,nomination.available);
            if(nomination.available)
            {
                Assert::AreEqual(uint32_t(2),nomination.confirmedSceneCount);
                EqualRecallBounds(rig.scopeEvidence.trustedBounds,nomination.rememberedBounds);
                Assert::IsTrue(nomination.historyId!=0 && nomination.historyRevision!=0);
            }
        }
    }

    TEST_METHOD(QualificationCanBeginAfterTheSixtyFourSceneAgeWindow)
    {
        RecallRig rig;
        rig.Establish(rig.scope,100);
        for(int i=0;i<3;++i)rig.FeedNative(rig.scope,101);
        rig.Establish(rig.wide,102);
        RecallPixels frame; RecallDarkLowerPicture(frame);
        for(int vote=0;vote<ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS;++vote)
        {
            const auto candidate=rig.Inspect(frame,103);
            Assert::IsTrue(candidate.candidateAvailable);
            Assert::AreEqual(uint32_t(2),candidate.evidence.rememberedEdgeReturnProof.nomination.confirmedSceneCount);
            const auto decision=rig.model.Observe(rig.Admit(frame.Source(),candidate.evidence,rig.wideEvidence.trustedBounds).observation);
            Assert::AreEqual(vote+1==ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS,decision.publish);
            if(decision.publish)EqualRecallBounds(rig.scopeEvidence.trustedBounds,decision.bounds);
            else EqualRecallBounds(rig.wideEvidence.trustedBounds,decision.stableBounds);
        }
    }
    TEST_METHOD(DefaultOffAndOrdinaryHistoryAloneCannotQualifyTheReturn)
    {
        for(bool disabled:{true,false})
        {
            RecallRig rig; rig.context.enabled=!disabled;
            if(disabled)rig.QualifiedWide(); else rig.Establish(rig.wide,1);
            RecallPixels frame; RecallDarkLowerPicture(frame); rig.Advance();
            const auto raw=ExtractActivePictureEvidence(frame.Source());
            Assert::IsFalse(rig.Nominate(raw).available);
            const auto result=InspectRememberedEdgeReturn(frame.Source(),raw,rig.Nominate(raw),rig.context.sourceSequence,rig.context.timestampMs);
            Assert::IsFalse(result.candidateAvailable); Assert::AreEqual(size_t(0),result.samples);
            AssertUnchangedRecallEvidence(raw,result.evidence);
        }
    }

    TEST_METHOD(PausedRepeatsAndChangingPixelsCannotInventAnIndependentScene)
    {
        for(bool newSequence:{false,true})
        {
            RecallRig rig; rig.Establish(rig.scope,1);
            for(int repeat=0;repeat<8;++repeat)
            {
                if(newSequence)++rig.context.sourceSequence;
                rig.context.timestampMs+=40; rig.context.sceneId=2;
                rig.context.cadenceRepeat=newSequence;
                rig.model.SetRememberedEdgeReturnContext(rig.context);
                // Interior noise changes real pixels but supplies no independent
                // confirmed scene or fresh source frame when cadence repeats.
                rig.scope.Rectangle(400,200,420,220,300+repeat);
                const auto raw=ExtractActivePictureEvidence(rig.scope.Source());
                const auto observed=MakeActivePictureObservation(raw,rig.context.sourceSequence,24);
                rig.model.Observe(observed);
                Assert::IsFalse(rig.model.RecordIndependentNativeGeometry(observed));
            }
            rig.context.cadenceRepeat=false; rig.Establish(rig.wide,3);
            RecallPixels frame; RecallDarkLowerPicture(frame); rig.Advance();
            Assert::IsFalse(rig.Nominate(ExtractActivePictureEvidence(frame.Source())).available);
        }
    }

    TEST_METHOD(CutCandidateAndConfirmationCreditOnlyOneNewVerifiedScene)
    {
        RecallRig rig; rig.Establish(rig.scope,1);
        for(int cutEvent=0;cutEvent<2;++cutEvent)
        {
            rig.Advance(cutEvent==0?1:2);
            rig.context.discontinuity=true; rig.model.SetRememberedEdgeReturnContext(rig.context);
            const auto observed=MakeActivePictureObservation(rig.scopeEvidence,rig.context.sourceSequence,24);
            rig.model.ResetCandidateEvidence(); rig.model.Observe(observed);
            Assert::IsFalse(rig.model.RecordIndependentNativeGeometry(observed));
        }
        for(int i=0;i<6;++i)rig.FeedNative(rig.scope,2);
        rig.Establish(rig.wide,3); RecallPixels frame; RecallDarkLowerPicture(frame); rig.Advance();
        const auto nomination=rig.Nominate(ExtractActivePictureEvidence(frame.Source()));
        Assert::IsTrue(nomination.available); Assert::AreEqual(uint32_t(2),nomination.confirmedSceneCount);
    }

    TEST_METHOD(ExperimentalOrHistoryDerivedNativeRelabelsCannotTrainQualification)
    {
        for(int fault=0;fault<5;++fault)
        {
            RecallRig rig; rig.Establish(rig.scope,1);
            for(int i=0;i<3;++i)
            {
                rig.Advance(2);
                auto observed=MakeActivePictureObservation(rig.scopeEvidence,rig.context.sourceSequence,24);
                if(fault==0)observed.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
                if(fault==1)observed.authorityOrigin=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN;
                if(fault==2)observed.rememberedEdgeReturnProof.available=true;
                if(fault==3)observed.sparseTransitionProof.available=true;
                if(fault==4)observed.axisEvidence.vertical.state=ActivePictureAxisState::UNKNOWN;
                Assert::IsFalse(rig.model.RecordIndependentNativeGeometry(observed));
            }
            rig.Establish(rig.wide,3); RecallPixels frame; RecallDarkLowerPicture(frame); rig.Advance();
            Assert::IsFalse(rig.Nominate(ExtractActivePictureEvidence(frame.Source())).available);
        }
    }

    TEST_METHOD(SourceContextRollbackAndQualifiedHistoryAgeRemainBounded)
    {
        for(int fault=0;fault<9;++fault)
        {
            RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
            const auto raw=ExtractActivePictureEvidence(frame.Source()); rig.Advance();
            Assert::IsTrue(rig.Nominate(raw).available);
            if(fault==0)++rig.context.sourceGeneration;
            if(fault==1)++rig.context.rendererGeneration;
            if(fault==2)++rig.context.viewportGeneration;
            if(fault==3)++rig.context.sourceFormatGeneration;
            if(fault==4)++rig.context.policyGeneration;
            if(fault==5)--rig.context.sourceSequence;
            if(fault==6)--rig.context.timestampMs;
            if(fault==7)--rig.context.sceneId;
            if(fault==8)rig.context.enabled=false;
            rig.model.SetRememberedEdgeReturnContext(rig.context);
            Assert::IsFalse(rig.Nominate(raw).available);
        }
        for(bool oldScene:{false,true})
        {
            RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame); rig.Advance();
            const auto raw=ExtractActivePictureEvidence(frame.Source()); const auto nomination=rig.Nominate(raw);
            Assert::IsTrue(nomination.available);
            ++rig.context.sourceSequence;
            if(oldScene)rig.context.sceneId=2+ActivePictureTransitionModel::REMEMBERED_RETURN_MAX_SCENE_DISTANCE+1;
            else rig.context.timestampMs=nomination.lastIndependentNativeTickMs+ActivePictureTransitionModel::REMEMBERED_RETURN_MAX_AGE_MS+1;
            rig.model.SetRememberedEdgeReturnContext(rig.context);
            Assert::IsFalse(rig.Nominate(raw).available);
        }
    }

    TEST_METHOD(TwoQualifiedNativeTargetsSharingTheExactEdgeRemainAmbiguous)
    {
        RecallRig rig(3840,2160); RecallPixels first(3840,2160),second(3840,2160),current(3840,2160);
        // Both pairs satisfy the existing native8px symmetry tolerance, but
        // differ16px so the legacy8px geometry match cannot merge them.
        RecallScope(first,276,1892); RecallScope(second,276,1876);
        rig.Establish(first,1); for(int i=0;i<3;++i)rig.FeedNative(first,2);
        rig.Establish(rig.wide,3);
        rig.Establish(second,4); for(int i=0;i<3;++i)rig.FeedNative(second,5);
        rig.Establish(rig.wide,6); RecallDarkLowerPicture(current); rig.Advance(7);
        const auto raw=ExtractActivePictureEvidence(current.Source()); AssertRecallRaw(raw);
        Assert::IsFalse(rig.Nominate(raw).available,L"Recency must not select among two independently qualified exact-edge matches.");
    }

    TEST_METHOD(TwoFreshProfileProofsPassOrdinaryAdmissionWithoutAFullRasterBounce)
    {
        RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
        const auto first=rig.Inspect(frame); Assert::IsTrue(first.candidateAvailable);
        const auto admission=rig.Admit(frame.Source(),first.evidence,rig.wideEvidence.trustedBounds);
        Assert::IsFalse(admission.observation.transitionDeferred);
        Assert::IsTrue(admission.observation.rememberedEdgeReturnProof.available);
        const auto pending=rig.model.Observe(admission.observation);
        Assert::IsFalse(pending.publish || pending.clearTransition);
        EqualRecallBounds(rig.wideEvidence.trustedBounds,pending.stableBounds);
        const auto repeated=rig.model.Observe(admission.observation);
        Assert::IsFalse(repeated.publish || repeated.clearTransition);
        Assert::AreEqual(pending.matchingCandidates,repeated.matchingCandidates);
        const auto second=rig.Inspect(frame); Assert::IsTrue(second.candidateAvailable);
        const auto confirmed=rig.model.Observe(rig.Admit(frame.Source(),second.evidence,rig.wideEvidence.trustedBounds).observation);
        Assert::IsTrue(confirmed.publish && confirmed.stable && !confirmed.clearTransition);
        EqualRecallBounds(rig.scopeEvidence.trustedBounds,confirmed.bounds);
        Assert::IsTrue(confirmed.authorityOrigin==ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN);
    }

    TEST_METHOD(CurrentProofCannotBeRedirectedToAnotherHistoryBaseTargetOrFrame)
    {
        for(int fault=0;fault<7;++fault)
        {
            RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
            for(int vote=0;vote<3;++vote)
            {
                auto result=rig.Inspect(frame); Assert::IsTrue(result.candidateAvailable);
                auto& proof=result.evidence.rememberedEdgeReturnProof;
                if(fault==0)++proof.nomination.historyId;
                if(fault==1)++proof.nomination.historyRevision;
                if(fault==2)++proof.nomination.sourceGeneration;
                if(fault==3)++proof.sourceSequence;
                if(fault==4)++proof.timestampMs;
                if(fault==5)proof.nomination.establishedBase.top+=2;
                if(fault==6)result.evidence.trustedBounds.bottom-=2;
                const auto decision=rig.model.Observe(rig.Admit(frame.Source(),result.evidence,rig.wideEvidence.trustedBounds).observation);
                Assert::IsFalse(decision.publish);
                EqualRecallBounds(rig.wideEvidence.trustedBounds,decision.stableBounds);
            }
        }
    }

    TEST_METHOD(DiscontinuityInvalidatesPendingVotesWithoutDestroyingQualifiedPrior)
    {
        RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
        const auto first=rig.Inspect(frame); Assert::IsTrue(first.candidateAvailable);
        Assert::IsFalse(rig.model.Observe(rig.Admit(frame.Source(),first.evidence,rig.wideEvidence.trustedBounds).observation).publish);
        rig.Advance(); rig.context.discontinuity=true; rig.model.SetRememberedEdgeReturnContext(rig.context);
        Assert::IsFalse(rig.Nominate(ExtractActivePictureEvidence(frame.Source())).available);
        rig.model.ResetCandidateEvidence();
        const auto after=rig.Inspect(frame); Assert::IsTrue(after.candidateAvailable);
        Assert::AreEqual(first.evidence.rememberedEdgeReturnProof.nomination.historyId,
            after.evidence.rememberedEdgeReturnProof.nomination.historyId);
        Assert::IsFalse(rig.model.Observe(rig.Admit(frame.Source(),after.evidence,rig.wideEvidence.trustedBounds).observation).publish);
        const auto next=rig.Inspect(frame); Assert::IsTrue(next.candidateAvailable);
        Assert::IsTrue(rig.model.Observe(rig.Admit(frame.Source(),next.evidence,rig.wideEvidence.trustedBounds).observation).publish);
    }

    TEST_METHOD(AQualifiedReturnCannotTrainItselfOrTransferToAQueuedDecision)
    {
        RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
        rig.Advance(); const auto original=rig.Nominate(ExtractActivePictureEvidence(frame.Source()));
        Assert::IsTrue(original.available);
        const auto published=rig.Return(frame);
        const auto raw=ExtractActivePictureEvidence(frame.Source());
        auto derived=MakeActivePictureObservation(raw,rig.context.sourceSequence,24);
        derived.bounds=published.bounds; derived.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
        derived.authorityOrigin=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN;
        Assert::IsFalse(rig.model.RecordIndependentNativeGeometry(derived));
        RecallRig queue; queue.QualifiedWide();
        Assert::IsFalse(queue.model.AdoptPublishedDecision(published,published.authoritativeClassification));
        rig.Establish(rig.wide,5); rig.Advance(6);
        const auto repeated=rig.Nominate(raw); Assert::IsTrue(repeated.available);
        Assert::AreEqual(original.confirmedSceneCount,repeated.confirmedSceneCount);
        Assert::AreEqual(original.lastIndependentNativeSequence,repeated.lastIndependentNativeSequence);
        Assert::AreEqual(original.historyRevision,repeated.historyRevision);
    }

    TEST_METHOD(PresentationResetPreservesSourceFactsButNeverPendingReturnVotes)
    {
        for(bool policyChange:{false,true})
        {
            RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
            const auto first=rig.Inspect(frame); Assert::IsTrue(first.candidateAvailable);
            Assert::IsFalse(rig.model.Observe(rig.Admit(frame.Source(),first.evidence,rig.wideEvidence.trustedBounds).observation).publish);
            const auto original=first.evidence.rememberedEdgeReturnProof.nomination;
            if(policyChange)++rig.context.policyGeneration; else ++rig.context.viewportGeneration;
            rig.model.SetRememberedEdgeReturnContext(rig.context);
            rig.model.ResetPresentationState();
            Assert::IsFalse(rig.Nominate(ExtractActivePictureEvidence(frame.Source())).available);
            rig.Establish(rig.wide,5,false);
            const auto next=rig.Inspect(frame,6); Assert::IsTrue(next.candidateAvailable);
            const auto& retained=next.evidence.rememberedEdgeReturnProof.nomination;
            Assert::AreEqual(original.historyId,retained.historyId);
            Assert::AreEqual(original.confirmedSceneCount,retained.confirmedSceneCount);
            Assert::AreEqual(original.lastIndependentNativeSequence,retained.lastIndependentNativeSequence);
            Assert::AreEqual(rig.context.viewportGeneration,retained.viewportGeneration);
            Assert::AreEqual(rig.context.policyGeneration,retained.policyGeneration);
            Assert::IsFalse(rig.model.Observe(rig.Admit(frame.Source(),next.evidence,rig.wideEvidence.trustedBounds).observation).publish);
            const auto confirmed=rig.Inspect(frame,6); Assert::IsTrue(confirmed.candidateAvailable);
            Assert::IsTrue(rig.model.Observe(rig.Admit(frame.Source(),confirmed.evidence,rig.wideEvidence.trustedBounds).observation).publish);
        }
    }

    TEST_METHOD(CaptionOnSecondVoteCancelsTheReturnIncludingBetweenSparseProbeRows)
    {
        for(bool betweenProbeRows:{false,true})
        {
            RecallRig rig(3840,2160); rig.QualifiedWide(); RecallPixels frame(3840,2160);
            RecallDarkLowerPicture(frame);
            const auto first=rig.Inspect(frame); Assert::IsTrue(first.candidateAvailable);
            Assert::IsFalse(rig.model.Observe(rig.Admit(frame.Source(),first.evidence,rig.wideEvidence.trustedBounds).observation).publish);
            const int y=betweenProbeRows?1925:1884;
            // 200px occupies only two/three native 48-column samples, so
            // rough black-line discovery can remain asymmetric. At1925..1933
            // the caption is between profile/six-depth rows and hits only one
            // row in64-depth visible-extent probing. Its actual pixels must
            // veto publication through the candidate-specific caption owner.
            frame.Rectangle(1750,y,1950,y+8,700);
            const auto second=rig.Inspect(frame);
            Assert::IsFalse(second.candidateAvailable,L"A current caption must veto the second remembered-return vote.");
            const auto raw=ExtractActivePictureEvidence(frame.Source()); AssertRecallRaw(raw);
            const auto decision=rig.model.Observe(rig.Admit(frame.Source(),raw,rig.wideEvidence.trustedBounds).observation);
            Assert::IsFalse(decision.publish || decision.clearTransition);
            EqualRecallBounds(rig.wideEvidence.trustedBounds,decision.stableBounds);
        }
    }
    TEST_METHOD(NativeOutwardPictureKeepsItsExistingConfirmationTimingAfterReturn)
    {
        RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
        const auto scope=rig.Return(frame);
        AlphaSourceCrop::OutwardPictureConfirmationState outward;
        const int required=AlphaSourceCrop::OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED+
            ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS-1;
        for(int i=0;i<required;++i)
        {
            rig.Advance(5);
            const auto native=ExtractActivePictureEvidence(rig.wide.Source());
            const auto admission=rig.Admit(rig.wide.Source(),native,scope.bounds,outward);
            outward=admission.outward.state;
            Assert::IsTrue(admission.outward.broadOpposingPicture);
            const auto decision=rig.model.Observe(admission.observation);
            Assert::AreEqual(i+1==required,decision.publish);
            if(decision.publish)
            {
                EqualRecallBounds(rig.wideEvidence.trustedBounds,decision.bounds);
                Assert::IsTrue(decision.authorityOrigin==ActivePictureAuthorityOrigin::NATIVE);
            }
            else EqualRecallBounds(scope.bounds,decision.stableBounds);
        }
    }
};

TEST_CLASS(RememberedEdgeRuntimeTests)
{
public:
    TEST_METHOD(FourKQualifiedAndRejectedProfilesReportBoundedInProcessCost)
    {
        RecallRig rig(3840,2160); rig.QualifiedWide();
        RecallPixels good(3840,2160),rejected(3840,2160);
        RecallDarkLowerPicture(good); RecallDarkLowerPicture(rejected);
        rejected.Rectangle(0,1612,3840,1908,80);
        rig.Advance();
        const auto raw=ExtractActivePictureEvidence(good.Source());
        const auto rejectedRaw=ExtractActivePictureEvidence(rejected.Source());
        AssertRecallRaw(raw); AssertRecallRaw(rejectedRaw);
        const auto nomination=rig.Nominate(raw); Assert::IsTrue(nomination.available);
        for(int path=0;path<3;++path)
        {
            std::vector<double> milliseconds;
            size_t samples=0;
            for(int iteration=-2;iteration<16;++iteration)
            {
                const auto started=std::chrono::steady_clock::now();
                if(path==2)
                {
                    const auto extracted=ExtractActivePictureEvidence(good.Source());
                    AssertRecallRaw(extracted);
                    samples=extracted.lumaSamples+extracted.chromaSamples;
                }
                else
                {
                    const auto result=InspectRememberedEdgeReturn(path==0?good.Source():rejected.Source(),
                        path==0?raw:rejectedRaw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
                    Assert::AreEqual(path==0,result.candidateAvailable);
                    samples=result.samples;
                }
                const double elapsed=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-started).count();
                if(iteration>=0)milliseconds.push_back(elapsed);
            }
            std::sort(milliseconds.begin(),milliseconds.end());
            std::ostringstream message;
            message<<"remembered-return 4K "<<(path==0?"qualified-helper":path==1?"rejected-helper":"forced-native-extractor")
                <<" n=16 median_ms="<<milliseconds[8]<<" p95_ms="<<milliseconds[15]
                <<" max_ms="<<milliseconds.back()<<" samples="<<samples;
            Logger::WriteMessage(message.str().c_str());
        }
    }
};
TEST_CLASS(RememberedEdgePresentationPolicyTests)
{
public:
    TEST_METHOD(PendingRememberedReturnCannotRevokePriorFullRasterAuthority)
    {
        RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
        const auto candidate=rig.Inspect(frame); Assert::IsTrue(candidate.candidateAvailable);
        const auto pending=rig.model.Observe(rig.Admit(frame.Source(),candidate.evidence,rig.wideEvidence.trustedBounds).observation);
        Assert::IsFalse(pending.publish);
        const bool retained=AlphaSourceCrop::UpdateFullRasterPresentationAuthority(true,
            candidate.evidence.classification,false,candidate.evidence.authorityOrigin);
        Assert::IsTrue(retained);
        Assert::IsTrue(AlphaSourceCrop::CommitSparseTransitionPresentationAuthority(retained,pending));
        Assert::IsFalse(AlphaSourceCrop::UpdateFullRasterPresentationAuthority(true,
            candidate.evidence.classification,false,ActivePictureAuthorityOrigin::NATIVE));
    }

    TEST_METHOD(OnlyAnActualStableRememberedPublicationCanCommitAuthorityHandoff)
    {
        RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
        const auto published=rig.Return(frame);
        Assert::IsFalse(AlphaSourceCrop::CommitSparseTransitionPresentationAuthority(true,published));
        for(int fault=0;fault<6;++fault)
        {
            auto changed=published;
            if(fault==0)changed.publish=false;
            if(fault==1)changed.stable=false;
            if(fault==2)changed.clearTransition=true;
            if(fault==3)changed.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
            if(fault==4)changed.authoritativeClassification=ActivePictureClassification::PROVISIONAL;
            if(fault==5)changed.bounds.left=2;
            Assert::IsTrue(AlphaSourceCrop::CommitSparseTransitionPresentationAuthority(true,changed));
        }
    }

    TEST_METHOD(RememberedEvidenceCanReaffirmOnlyItsOwnExactPublishedCropAndOrigin)
    {
        RecallRig rig; rig.QualifiedWide(); RecallPixels frame; RecallDarkLowerPicture(frame);
        const auto candidate=rig.Inspect(frame); Assert::IsTrue(candidate.candidateAvailable);
        Assert::IsFalse(AlphaSourceCrop::SparseTransitionMayReaffirmOwnedCrop(rig.wideEvidence.trustedBounds,
            ActivePictureAuthorityOrigin::NATIVE,candidate.evidence));
        Assert::IsFalse(AlphaSourceCrop::SparseTransitionMayReaffirmOwnedCrop(candidate.evidence.trustedBounds,
            ActivePictureAuthorityOrigin::NATIVE,candidate.evidence));
        Assert::IsTrue(AlphaSourceCrop::SparseTransitionMayReaffirmOwnedCrop(candidate.evidence.trustedBounds,
            ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN,candidate.evidence));
        for(int fault=0;fault<4;++fault)
        {
            auto owned=candidate.evidence.trustedBounds;
            if(fault==0)owned.top+=2;
            if(fault==1)owned.bottom-=2;
            if(fault==2)owned.rasterWidth+=2;
            if(fault==3)owned.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH;
            Assert::IsFalse(AlphaSourceCrop::SparseTransitionMayReaffirmOwnedCrop(owned,
                ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN,candidate.evidence));
        }
    }
};
}
