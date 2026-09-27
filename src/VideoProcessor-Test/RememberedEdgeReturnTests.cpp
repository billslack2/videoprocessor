#include "pch.h"
#include "CppUnitTest.h"
#include <RememberedEdgeReturn.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <vprenderer/BufferedPictureExpansion.h>
#include <algorithm>
#include <vector>
#include <chrono>
#include <cmath>
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
// Same synthetic source pixels in separately identified queue entries. Identity
// checks, rather than elapsed logging ticks, decide whether this is a window.
struct RecallShadowWindowRig
{
    RecallRig rig;
    RecallPixels pixels;
    RememberedEdgeReturnNomination nomination;
    std::vector<RememberedEdgeReturnShadowSample> samples;
    explicit RecallShadowWindowRig(size_t count=3)
    {
        rig.context.enabled=false; rig.context.shadowOnly=true; rig.QualifiedWide(); rig.Advance();
        pixels.Rectangle(80,148,960,472,300);
        const auto raw=ExtractActivePictureEvidence(pixels.Source());
        Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL && raw.bottom.trusted);
        nomination=rig.model.NominateRememberedEdgeReturnShadow(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
        Assert::IsTrue(nomination.available);
        samples.resize(count);
        for(size_t i=0;i<count;++i)
        {
            auto& sample=samples[i]; sample.available=true;
            sample.source=pixels.Source();
            sample.sourceGenerationDomain=i?RememberedShadowSourceGeneration::FORMAT:RememberedShadowSourceGeneration::TRANSPORT;
            if(i)sample.source.generation=rig.context.sourceFormatGeneration;
            sample.raw=ExtractActivePictureEvidence(sample.source);
            sample.identity.transportGeneration=rig.context.sourceGeneration;
            sample.identity.acceptedSequence=rig.context.sourceSequence+i;
            sample.identity.sourceFrameNumber=1000+i;
            sample.identity.captureTimestamp=100000+i*1000;
            sample.identity.sourceFormatGeneration=rig.context.sourceFormatGeneration;
            sample.identity.rendererGeneration=rig.context.rendererGeneration;
            sample.identity.viewportGeneration=rig.context.viewportGeneration;
            sample.policyGeneration=rig.context.policyGeneration;
            sample.timestampMs=rig.context.timestampMs; // Queued frames can share the render clock tick.
        }
    }
};
struct GuardedActiveRecallRig
{
    RecallRig rig;
    RecallPixels pixels;
    RememberedEdgeReturnNomination nomination;
    std::vector<RememberedEdgeReturnShadowSample> samples;
    ActivePictureDecisionTimeline timeline;
    explicit GuardedActiveRecallRig(size_t count=5,int width=960,int height=540):rig(width,height),pixels(width,height)
    {
        rig.context.guardedEnabled=true; rig.QualifiedWide(); rig.Advance();
        const auto target=rig.scopeEvidence.trustedBounds;
        pixels.Rectangle(width/12,target.top+(height==2160?40:80),width,target.bottom,300);
        const int x=50*(width-1)/95;
        pixels.Rectangle(x,target.bottom,x+1,target.bottom+1,70);
        const auto raw=ExtractActivePictureEvidence(pixels.Source());
        Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL && raw.bottom.trusted);
        nomination=rig.model.NominateGuardedRememberedEdgeReturn(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
        samples.resize(count); timeline.Reset(rig.context.sourceGeneration);
        for(size_t i=0;i<count;++i)
        {
            auto& sample=samples[i]; sample.available=true; sample.source=pixels.Source();
            sample.sourceGenerationDomain=i?RememberedShadowSourceGeneration::FORMAT:RememberedShadowSourceGeneration::TRANSPORT;
            if(i)sample.source.generation=rig.context.sourceFormatGeneration;
            sample.raw=ExtractActivePictureEvidence(sample.source);
            sample.identity.transportGeneration=rig.context.sourceGeneration;
            sample.identity.acceptedSequence=rig.context.sourceSequence+i;
            sample.identity.sourceFrameNumber=1000+i; sample.identity.captureTimestamp=100000+i*1000;
            sample.identity.sourceFormatGeneration=rig.context.sourceFormatGeneration;
            sample.identity.rendererGeneration=rig.context.rendererGeneration;
            sample.identity.viewportGeneration=rig.context.viewportGeneration;
            sample.policyGeneration=rig.context.policyGeneration; sample.timestampMs=rig.context.timestampMs;
            Assert::IsTrue(timeline.TrackAcceptedFrame(sample.identity));
        }
    }
    GuardedRememberedEdgeReturnCertificate Build(uint32_t configured=5,uint32_t actual=4) const
    {
        return BuildGuardedRememberedEdgeReturnCertificate(rig.model,nomination,samples.data(),samples.size(),configured,actual,timeline.ContinuityGeneration());
    }
    bool Adopt(const GuardedRememberedEdgeReturnCertificate& certificate,ActivePictureTransitionDecision* decision=nullptr)
    {
        return ValidateAndAdoptGuardedRememberedEdgeReturn(rig.model,certificate,pixels.Source(),samples[0].raw,
            samples[0].identity,rig.context,timeline.ContinuityGeneration(),decision);
    }
};
ActivePictureTransitionDecision GuardedRecallSnapshot(const ActivePictureTransitionModel& model)
{
    auto copy=model; return copy.Observe(ActivePictureObservation{});
}
AlphaSourceCrop::GuardedSceneHoldInput GuardedRecallSceneHold(const GuardedActiveRecallRig& fixture,
    const GuardedRememberedEdgeReturnCertificate& certificate)
{
    AlphaSourceCrop::SceneHoldInput held;
    held.snapshotAvailable=true;
    held.snapshotSourceGeneration=held.frameSourceGeneration=fixture.rig.context.sourceGeneration;
    held.currentTick=fixture.rig.context.timestampMs; held.deadlineTick=held.currentTick+2000;
    AlphaSourceCrop::GuardedSceneHoldInput input;
    input.hold=AlphaSourceCrop::EvaluateSceneHold(held);
    input.snapshotAvailable=true;
    input.snapshotBounds=input.currentNativeBase=fixture.rig.wideEvidence.trustedBounds;
    input.nominationBase=certificate.nomination.establishedBase;
    input.snapshotGeneration=input.currentGeneration=fixture.rig.context.sourceGeneration;
    input.currentNativeAvailable=true; input.certificateAvailable=certificate.available;
    Assert::IsTrue(input.hold.cropActive && !input.hold.nlsActive);
    return input;
}
}
TEST_CLASS(RememberedEdgeProfileTests)
{
public:
        TEST_METHOD(GuardedReferenceKeepsShaderAndQueueGenerationDomainsIndependent)
    {
        ActivePictureFrameIdentity frame;
        frame.acceptedSequence=506; frame.transportGeneration=1;
        frame.rendererGeneration=1; frame.sourceFormatGeneration=17;
        frame.viewportGeneration=13;
        Assert::IsTrue(AlphaSourceCrop::BufferedPictureReferenceMatches(frame,1,17,13));
        Assert::IsTrue(AlphaSourceCrop::GuardedRememberedReferenceMatches(frame,1,17,13,1,1));
        Assert::IsTrue(AlphaSourceCrop::GuardedRememberedReferenceMatches(frame,1,17,13,4,4),
            L"An unchanged fourth shader lifetime must not invalidate the first queue/source lifetime.");
    }

    TEST_METHOD(GuardedReferenceRejectsQueueSourceFormatViewportOrShaderDiscontinuity)
    {
        for(int fault=0; fault<10; ++fault)
        {
            ActivePictureFrameIdentity frame;
            frame.acceptedSequence=506; frame.transportGeneration=1;
            frame.rendererGeneration=1; frame.sourceFormatGeneration=17;
            frame.viewportGeneration=13;
            uint64_t source=1, format=17, viewport=13, capturedShader=4, currentShader=4;
            switch(fault)
            {
            case 0: frame.rendererGeneration=2; break;
            case 1: frame.transportGeneration=2; break;
            case 2: format=18; break;
            case 3: viewport=14; break;
            case 4: currentShader=5; break;
            case 5: capturedShader=0; currentShader=0; break;
            case 6: currentShader=0; break;
            case 7: source=0; break;
            case 8: format=0; break;
            case 9: frame.acceptedSequence=0; break;
            }
            const auto message=std::wstring(L"Reference fault ")+std::to_wstring(fault);
            Assert::IsFalse(AlphaSourceCrop::GuardedRememberedReferenceMatches(frame,source,
                format,viewport,capturedShader,currentShader),message.c_str());
        }
    }
// Synthetic Eternals-style source-pixel sequences, not captured movie frames. Shadow diagnostics
    // may recognize a prior while the complete primary path remains unchanged.
    TEST_METHOD(RememberedShadowRecognizesSidePartialReturnWithoutChangingPrimaryOrQueuedCrop)
    {
        RecallRig baseline, shadow;
        baseline.context.enabled=false;
        shadow.context.enabled=false; shadow.context.shadowOnly=true;
        baseline.QualifiedWide(); shadow.QualifiedWide();
        const auto history=shadow.model.GetRememberedEdgeReturnHistoryStatus();
        RecallPixels partial;
        partial.Rectangle(partial.width/12,RecallTop(partial)+80,partial.width,
            partial.height-RecallTop(partial),300);
        for(int sample=0;sample<5;++sample)
        {
            baseline.Advance(); shadow.Advance();
            const auto raw=ExtractActivePictureEvidence(partial.Source());
            const auto unchanged=raw;
            Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
            Assert::IsTrue(raw.proposedBounds.left>0 && raw.bottom.trusted,
                L"This fixture must exercise inward side uncertainty and an actual matching bottom edge.");
            Assert::IsTrue(history.qualifiedEntries>=1);
            Assert::IsTrue(history.maxConfirmedScenes>=2);
            const auto nomination=shadow.model.NominateRememberedEdgeReturnShadow(
                raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
            Assert::IsTrue(nomination.available,L"Native two-scene history should nominate scope in shadow mode.");
            Assert::IsFalse(shadow.Nominate(raw).available,L"Disabled production recall cannot nominate.");
            const auto legacy=InspectRememberedEdgeReturn(partial.Source(),raw,nomination,
                shadow.context.sourceSequence,shadow.context.timestampMs);
            Assert::IsFalse(legacy.candidateAvailable,L"The existing production side-axis rejection remains intact.");
            auto untaggedSideNomination=nomination; untaggedSideNomination.shadowOnly=false;
            Assert::IsFalse(InspectRememberedEdgeReturn(partial.Source(),raw,untaggedSideNomination,
                shadow.context.sourceSequence,shadow.context.timestampMs).candidateAvailable,
                L"A diagnostic tag must not mask the unchanged production side-axis veto.");
            const auto diagnostic=InspectRememberedEdgeReturnShadow(partial.Source(),raw,nomination,
                shadow.context.sourceSequence,shadow.context.timestampMs);
            Assert::IsTrue(diagnostic.wouldVerify);
            Assert::IsTrue(diagnostic.samples>0);
            EqualRecallBounds(shadow.scopeEvidence.trustedBounds,diagnostic.target);
            AssertUnchangedRecallEvidence(unchanged,raw);
            const auto baseAdmission=baseline.Admit(partial.Source(),raw,baseline.wideEvidence.trustedBounds);
            const auto shadowAdmission=shadow.Admit(partial.Source(),raw,shadow.wideEvidence.trustedBounds);
            Assert::AreEqual(baseAdmission.observation.transitionDeferred,shadowAdmission.observation.transitionDeferred);
            Assert::IsFalse(shadowAdmission.observation.rememberedEdgeReturnProof.available);
            auto preview=shadow.model;
            const auto queued=preview.Observe(shadowAdmission.observation);
            const auto primary=shadow.model.Observe(shadowAdmission.observation);
            const auto control=baseline.model.Observe(baseAdmission.observation);
            Assert::AreEqual(control.publish,primary.publish); Assert::IsFalse(primary.publish||queued.publish);
            EqualRecallBounds(control.stableBounds,primary.stableBounds);
            EqualRecallBounds(shadow.wideEvidence.trustedBounds,queued.stableBounds);
            AlphaSourceCrop::Decision finals[2];
            for(int variant=0;variant<2;++variant)
            {
                const auto& geometry=variant?primary.stableBounds:control.stableBounds;
                const auto retention=EvaluateActivePicturePresentationRetention(partial.Source(),geometry);
                AlphaSourceCrop::Input input;
                input.automaticCropEnabled=input.sharedGeometryAvailable=true;
                input.geometry=geometry; input.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
                input.geometrySourceGeneration=input.frameSourceGeneration=7;
                input.frameSourceSequence=shadow.context.sourceSequence;
                input.rasterWidth=partial.width; input.rasterHeight=partial.height;
                input.latestObservationClassification=raw.classification;
                input.latestObservationIsProvisional=true;
                input.frameLocalPresentationRetentionEvaluated=retention.analysisValid;
                input.frameLocalPresentationRetentionSafe=retention.CanRetainPresentation();
                finals[variant]=AlphaSourceCrop::Evaluate(input);
                Assert::IsTrue(finals[variant].applyCrop);
                EqualRecallBounds(shadow.wideEvidence.trustedBounds,finals[variant].sourceBounds);
            }
            EqualRecallBounds(finals[0].sourceBounds,finals[1].sourceBounds);
        }
    }

    TEST_METHOD(RememberedShadowProfileParityKeepsP010P210AndV210PixelsUnchanged)
    {
        RecallRig rig; rig.context.enabled=false; rig.context.shadowOnly=true; rig.QualifiedWide(); rig.Advance();
        RecallPixels p010(960,540,false),p210;
        for(auto* pixels:{&p010,&p210}) pixels->Rectangle(80,148,960,472,300);
        PackedRecallPixels packed(p210.Source());
        const auto originalP010=p010.bytes, originalP210=p210.bytes, originalPacked=packed.bytes;
        for(const auto& source:{p010.Source(),p210.Source(),packed.Source()})
        {
            const auto raw=ExtractActivePictureEvidence(source);
            Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
            const auto nomination=rig.model.NominateRememberedEdgeReturnShadow(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
            Assert::IsTrue(nomination.available);
            const auto result=InspectRememberedEdgeReturnShadow(source,raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
            Assert::IsTrue(result.wouldVerify); Assert::AreEqual(0,result.mismatches);
            EqualRecallBounds(rig.scopeEvidence.trustedBounds,result.target);
            Assert::AreEqual(64.0,result.referenceY);
        }
        Assert::IsTrue(originalP010==p010.bytes && originalP210==p210.bytes && originalPacked==packed.bytes);
    }

    TEST_METHOD(RememberedShadowCannotTurnKnownGeometryOrUnsafeExteriorIntoProof)
    {
        for(int pattern=0;pattern<5;++pattern)
        {
            RecallRig rig; rig.context.enabled=false; rig.context.shadowOnly=true; rig.QualifiedWide(); rig.Advance();
            RecallPixels pixels; RecallDarkLowerPicture(pixels);
            if(pattern==0)pixels.Rectangle(360,480,600,482,700); // Caption outside the proposed remembered picture.
            if(pattern==1)pixels.Rectangle(0,472,960,488,80); // Dim real picture below the generic black cutoff.
            if(pattern==2) { pixels.Rectangle(0,68,960,76,64); pixels.Rectangle(0,68,240,76,300); }
            if(pattern==3)RecallWide(pixels); // Genuine sustained taller picture has no matching inward return.
            if(pattern==4)pixels.Rectangle(0,472,40,474,700); // Sparse caption missed by the broad scan, caught by the profile.
            const auto raw=ExtractActivePictureEvidence(pixels.Source());
            const auto nomination=rig.model.NominateRememberedEdgeReturnShadow(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
            const auto result=InspectRememberedEdgeReturnShadow(pixels.Source(),raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
            std::ostringstream diagnostic;
            diagnostic << "Shadow unsafe pattern=" << pattern << " class=" << int(raw.classification)
                << " raw=" << raw.proposedBounds.left << ',' << raw.proposedBounds.top << '-'
                << raw.proposedBounds.right << ',' << raw.proposedBounds.bottom
                << " top_trusted=" << raw.top.trusted << " bottom_trusted=" << raw.bottom.trusted
                << " nominated=" << nomination.available << " samples=" << result.samples
                << " mismatches=" << result.mismatches << " reason=" << result.reason;
            Logger::WriteMessage(diagnostic.str().c_str());
            const auto message=std::wstring(L"Synthetic unsafe/insufficient pattern ")+std::to_wstring(pattern);
            Assert::IsFalse(result.wouldVerify,message.c_str());
            if(pattern==0)
            {
                // The captured test diagnostic proved raw bottom=482, outside target472.
                // That is an earlier valid geometry veto, before any profile samples.
                Assert::IsTrue(nomination.available);
                Assert::IsTrue(raw.proposedBounds.bottom>nomination.rememberedBounds.bottom);
                Assert::AreEqual(size_t(0),result.samples);
                Assert::IsTrue(std::string(result.reason)=="ineligible-context");
            }
            if(pattern==1 || pattern==4) { Assert::IsTrue(nomination.available); Assert::IsTrue(result.mismatches>0); }
            if(pattern==2)Assert::IsTrue(result.matchedEdgeSupport<48);
            if(pattern==3)
                for(int i=0;i<5;++i)EqualRecallBounds(rig.wideEvidence.trustedBounds,rig.FeedNative(pixels,4).stableBounds);
        }
    }

    TEST_METHOD(RememberedShadowRequiresQualifiedFreshContextAndCannotLearnPausedDuplicates)
    {
        for(int fault=0;fault<7;++fault)
        {
            RecallRig rig; rig.context.enabled=false; rig.context.shadowOnly=true;
            if(fault==0) { rig.Establish(rig.scope,1); rig.Establish(rig.wide,3); }
            else rig.QualifiedWide();
            RecallPixels frame; RecallDarkLowerPicture(frame); rig.Advance();
            const auto raw=ExtractActivePictureEvidence(frame.Source());
            if(fault==1)rig.context.cadenceRepeat=true;
            if(fault==2)rig.context.timestampMs+=ActivePictureTransitionModel::REMEMBERED_RETURN_MAX_AGE_MS+1;
            if(fault==3)++rig.context.sourceGeneration;
            if(fault==4)rig.context.shadowOnly=false;
            if(fault==5)rig.context.discontinuity=true;
            // fault6 repeats the exact same context/sequence: it cannot earn another independent observation.
            if(fault>0 && fault<6)++rig.context.sourceSequence;
            if(fault!=0)rig.model.SetRememberedEdgeReturnContext(rig.context);
            const auto nomination=rig.model.NominateRememberedEdgeReturnShadow(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
            const auto message=std::wstring(L"Synthetic stale/unqualified context ")+std::to_wstring(fault);
            Assert::IsFalse(nomination.available,message.c_str());
            const auto result=InspectRememberedEdgeReturnShadow(frame.Source(),raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
            Assert::IsFalse(result.wouldVerify); Assert::AreEqual(size_t(0),result.samples);
        }
        RecallRig rig; rig.context.enabled=false; rig.context.shadowOnly=true; rig.Establish(rig.scope,1);
        for(int repeat=0;repeat<6;++repeat)
        {
            rig.Advance(2); rig.context.cadenceRepeat=true; rig.model.SetRememberedEdgeReturnContext(rig.context);
            const auto raw=ExtractActivePictureEvidence(rig.scope.Source());
            Assert::IsFalse(rig.model.RecordIndependentNativeGeometry(MakeActivePictureObservation(raw,rig.context.sourceSequence,24)));
        }
        Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().maxConfirmedScenes);
    }
    TEST_METHOD(RememberedShadowReportsLocalizedOppositeEdgeWithoutManufacturingAuthority)
    {
        for(bool localized:{false,true})
        {
            RecallRig rig; rig.context.enabled=false; rig.context.shadowOnly=true; rig.QualifiedWide(); rig.Advance();
            RecallPixels pixels; pixels.Rectangle(80,148,960,472,300);
            if(localized)pixels.Rectangle(900,68,960,76,300);
            const auto raw=ExtractActivePictureEvidence(pixels.Source());
            Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL);
            Assert::IsTrue(raw.bottom.trusted);
            const auto nomination=rig.model.NominateRememberedEdgeReturnShadow(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
            Assert::IsTrue(nomination.available);
            const auto result=InspectRememberedEdgeReturnShadow(pixels.Source(),raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
            Assert::IsTrue(result.wouldVerify);
            Assert::IsTrue(result.matchedEdge==RememberedEdge::BOTTOM);
            Assert::AreEqual(0,result.mismatches);
            if(localized)
            {
                Assert::IsTrue(result.oppositeLumaSupport>0 && result.oppositeLumaSupport<12);
                Assert::IsTrue(result.oppositeLumaZones[3]>0);
                for(int zone=0;zone<3;++zone)Assert::AreEqual(0,result.oppositeLumaZones[zone]);
                Assert::IsTrue(result.oppositeMaxLumaDelta>=200);
            }
            else Assert::AreEqual(0,result.oppositeLumaSupport);
            const auto decision=rig.model.Observe(rig.Admit(pixels.Source(),raw,rig.wideEvidence.trustedBounds).observation);
            Assert::IsFalse(decision.publish);
            EqualRecallBounds(rig.wideEvidence.trustedBounds,decision.stableBounds);
        }
    }

    TEST_METHOD(RememberedShadowTaggedNominationAndConflictingFlagsCannotPublishLegacyProof)
    {
        RecallRig rig; rig.context.enabled=false; rig.context.shadowOnly=true; rig.QualifiedWide(); rig.Advance();
        RecallPixels pixels; RecallDarkLowerPicture(pixels);
        const auto raw=ExtractActivePictureEvidence(pixels.Source()); AssertRecallRaw(raw);
        const auto nomination=rig.model.NominateRememberedEdgeReturnShadow(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
        Assert::IsTrue(nomination.available && nomination.shadowOnly);
        Assert::IsFalse(rig.Nominate(raw).available);
        const auto production=InspectRememberedEdgeReturn(pixels.Source(),raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
        Assert::IsFalse(production.candidateAvailable);
        AssertUnchangedRecallEvidence(raw,production.evidence);
        // Independently test model configuration isolation: simulate an accidental
        // legacy caller removing the diagnostic tag from otherwise valid history.
        rig.context.enabled=true; ++rig.context.sourceSequence; rig.context.timestampMs+=40;
        rig.model.SetRememberedEdgeReturnContext(rig.context);
        Assert::IsFalse(rig.model.NominateRememberedEdgeReturnShadow(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted).available);
        Assert::IsFalse(rig.Nominate(raw).available);
        auto untagged=nomination; untagged.shadowOnly=false;
        untagged.sourceSequence=rig.context.sourceSequence; untagged.timestampMs=rig.context.timestampMs;
        const auto legacy=InspectRememberedEdgeReturn(pixels.Source(),raw,untagged,rig.context.sourceSequence,rig.context.timestampMs);
        Assert::IsTrue(legacy.candidateAvailable,L"This must reach the model with a valid legacy pixel proof, not fail an unrelated inspector gate.");
        const auto observation=MakeActivePictureObservation(legacy.evidence,rig.context.sourceSequence,24);
        const auto decision=rig.model.Observe(observation);
        Assert::IsFalse(decision.publish);
        EqualRecallBounds(rig.wideEvidence.trustedBounds,decision.stableBounds);
        Assert::IsFalse(rig.model.RecordIndependentNativeGeometry(observation));
    }
    TEST_METHOD(RememberedShadowAbstainsWhenHeavySideDarknessLeavesOneMatchedQuarterUnsupported)
    {
        RecallRig rig; rig.context.enabled=false; rig.context.shadowOnly=true; rig.QualifiedWide(); rig.Advance();
        RecallPixels pixels; pixels.Rectangle(200,148,960,472,300);
        const auto raw=ExtractActivePictureEvidence(pixels.Source());
        Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL && raw.bottom.trusted);
        const auto nomination=rig.model.NominateRememberedEdgeReturnShadow(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
        Assert::IsTrue(nomination.available);
        const auto result=InspectRememberedEdgeReturnShadow(pixels.Source(),raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
        Assert::IsFalse(result.wouldVerify);
        Assert::IsTrue(result.matchedEdgeSupport>=48,L"High total support does not compensate for an unsupported quarter.");
        Assert::IsTrue(result.matchedEdgeZones[0]<6);
        Assert::IsTrue(std::string(result.reason)=="matched-edge-not-distributed");
        const auto decision=rig.model.Observe(rig.Admit(pixels.Source(),raw,rig.wideEvidence.trustedBounds).observation);
        Assert::IsFalse(decision.publish);
        EqualRecallBounds(rig.wideEvidence.trustedBounds,decision.stableBounds);
    }
    TEST_METHOD(RememberedShadowWindowVerifiesAllConsecutiveFramesWithoutChangingAuthorityOrHistory)
    {
        RecallShadowWindowRig fixture;
        const auto history=fixture.rig.model.GetRememberedEdgeReturnHistoryStatus();
        const auto originalPixels=fixture.pixels.bytes;
        const auto result=InspectRememberedEdgeReturnShadowWindow(fixture.nomination,fixture.samples.data(),fixture.samples.size(),5,2);
        Assert::IsTrue(result.allPass,L"A current plus two available consecutive verified frames must form a diagnostic window.");
        Assert::AreEqual(uint32_t(2),result.effectiveFuture);
        Assert::AreEqual(size_t(3),result.expectedFrames);
        Assert::AreEqual(size_t(3),result.inspectedFrames);
        Assert::AreEqual(size_t(3),result.passedFrames);
        Assert::IsTrue(result.totalSamples>0);
        Assert::IsTrue(SameActivePictureFrameIdentity(fixture.samples.front().identity,result.firstIdentity));
        Assert::IsTrue(SameActivePictureFrameIdentity(fixture.samples.back().identity,result.lastIdentity));
        Assert::AreEqual(-1,result.failedIndex);
        Assert::IsTrue(originalPixels==fixture.pixels.bytes);
        const auto after=fixture.rig.model.GetRememberedEdgeReturnHistoryStatus();
        Assert::AreEqual(history.entries,after.entries);
        Assert::AreEqual(history.qualifiedEntries,after.qualifiedEntries);
        Assert::AreEqual(history.maxConfirmedScenes,after.maxConfirmedScenes);
        for(size_t i=0;i<fixture.samples.size();++i)
        {
            const auto& sample=fixture.samples[i];
            Assert::AreEqual(i?uint64_t(17):uint64_t(7),sample.source.generation,L"Normalize a local view, not the caller's source identity.");
            AssertUnchangedRecallEvidence(ExtractActivePictureEvidence(sample.source),sample.raw);
            auto primary=fixture.rig.model;
            const auto decision=primary.Observe(MakeActivePictureObservation(sample.raw,sample.identity.acceptedSequence,24));
            Assert::IsFalse(decision.publish);
            EqualRecallBounds(fixture.rig.wideEvidence.trustedBounds,decision.stableBounds);
        }
        EqualRecallBounds(fixture.rig.scopeEvidence.trustedBounds,fixture.nomination.rememberedBounds);
        const auto noFuture=InspectRememberedEdgeReturnShadowWindow(fixture.nomination,fixture.samples.data(),1,5,0);
        Assert::IsFalse(noFuture.allPass,L"A previous successful window supplies no credit to a later current-only window.");
    }

    TEST_METHOD(RememberedShadowWindowCannotSkipAConflictingMiddleOrLastPicture)
    {
        for(size_t badIndex:{size_t(1),size_t(2)})
        {
            RecallShadowWindowRig fixture;
            RecallPixels conflicting; conflicting.Rectangle(80,148,960,472,300);
            conflicting.Rectangle(0,472,960,474,80); // Real dim pixels outside the fixed remembered crop.
            auto& bad=fixture.samples[badIndex]; bad.source=conflicting.Source();
            bad.source.generation=fixture.rig.context.sourceFormatGeneration;
            bad.raw=ExtractActivePictureEvidence(bad.source);
            const auto result=InspectRememberedEdgeReturnShadowWindow(fixture.nomination,fixture.samples.data(),fixture.samples.size(),2,2);
            Assert::IsFalse(result.allPass);
            Assert::AreEqual(static_cast<int>(badIndex),result.failedIndex);
            Assert::AreEqual(badIndex,result.passedFrames);
            Assert::AreEqual(badIndex+1,result.inspectedFrames);
            Assert::IsTrue(result.failureInspection.mismatches>0);
            Assert::IsTrue(SameActivePictureFrameIdentity(bad.identity,result.failureIdentity));
            EqualRecallBounds(fixture.rig.scopeEvidence.trustedBounds,result.failureInspection.target);
        }
    }

    TEST_METHOD(RememberedShadowWindowRejectsMissingRepeatedOrDifferentContextBeforeInspection)
    {
        for(int fault=0;fault<18;++fault)
        {
            RecallShadowWindowRig fixture;
            auto& bad=fixture.samples[1]; const auto first=fixture.samples[0].identity;
            if(fault==0)bad.available=false;
            if(fault==1)bad.identity.acceptedSequence=first.acceptedSequence;
            if(fault==2)bad.identity.sourceFrameNumber=first.sourceFrameNumber;
            if(fault==3)++bad.identity.sourceFormatGeneration;
            if(fault==4)++bad.identity.viewportGeneration;
            if(fault==5)++bad.identity.rendererGeneration;
            if(fault==6)++bad.policyGeneration;
            if(fault==7)++bad.identity.transportGeneration;
            if(fault==8)bad.sourceGenerationDomain=RememberedShadowSourceGeneration::TRANSPORT;
            if(fault==9)++bad.source.generation;
            if(fault==10)bad.identity.captureTimestamp=first.captureTimestamp;
            if(fault==11)bad.identity.captureTimestamp=first.captureTimestamp-1;
            if(fault==12)++bad.identity.sourceFrameNumber;
            if(fault==13)++bad.identity.acceptedSequence;
            if(fault==14)--bad.timestampMs;
            if(fault==15)bad.cadenceRepeat=true;
            if(fault==16)bad.discontinuity=true;
            if(fault==17)bad.source.dataBytes=1;
            const auto result=InspectRememberedEdgeReturnShadowWindow(fixture.nomination,fixture.samples.data(),fixture.samples.size(),2,2);
            const auto message=std::wstring(L"Shadow window identity fault ")+std::to_wstring(fault);
            Assert::IsFalse(result.allPass,message.c_str());
            Assert::AreEqual(1,result.failedIndex,message.c_str());
            Assert::AreEqual(size_t(0),result.inspectedFrames,message.c_str());
            Assert::AreEqual(size_t(0),result.totalSamples,message.c_str());
        }
    }

    TEST_METHOD(RememberedShadowWindowUsesOnlyConfiguredActuallyAvailableFramesAndCapsWork)
    {
        RecallShadowWindowRig fixture(9);
        struct LimitCase { uint32_t configured,available; size_t supplied; uint32_t effective; bool pass; };
        const LimitCase cases[]={
            {0,8,9,0,false},{5,0,9,0,false},{5,2,9,2,true},{2,5,9,2,true},
            {99,99,9,8,true},{5,5,3,5,false},{5,1,2,1,true}
        };
        for(const auto& limit:cases)
        {
            const auto result=InspectRememberedEdgeReturnShadowWindow(fixture.nomination,fixture.samples.data(),limit.supplied,limit.configured,limit.available);
            Assert::AreEqual(limit.pass,result.allPass);
            Assert::AreEqual(limit.effective,result.effectiveFuture);
            Assert::AreEqual(size_t(limit.effective+1),result.expectedFrames);
            if(limit.pass)Assert::AreEqual(size_t(limit.effective+1),result.inspectedFrames);
        }
        const auto missing=InspectRememberedEdgeReturnShadowWindow(fixture.nomination,nullptr,0,3,3);
        Assert::IsFalse(missing.allPass);
        Assert::AreEqual(size_t(0),missing.inspectedFrames);
        // A bad frame beyond the configured window must not silently enlarge it.
        fixture.samples[2].available=false;
        Assert::IsTrue(InspectRememberedEdgeReturnShadowWindow(fixture.nomination,fixture.samples.data(),fixture.samples.size(),1,8).allPass);
        Assert::IsFalse(InspectRememberedEdgeReturnShadowWindow(fixture.nomination,fixture.samples.data(),fixture.samples.size(),2,8).allPass);
    }
    TEST_METHOD(RememberedShadowWindowRejectsInvalidNomineeAndOverflowBeforeReadingPixels)
    {
        for(int fault=0;fault<11;++fault)
        {
            RecallShadowWindowRig fixture;
            if(fault==0)fixture.nomination.available=false;
            if(fault==1)fixture.nomination.shadowOnly=false;
            if(fault==2)++fixture.nomination.sourceSequence;
            if(fault==3)++fixture.nomination.timestampMs;
            if(fault==4)++fixture.nomination.sourceGeneration;
            if(fault==5)++fixture.nomination.rendererGeneration;
            if(fault==6)++fixture.nomination.viewportGeneration;
            if(fault==7)++fixture.nomination.sourceFormatGeneration;
            if(fault==8)++fixture.nomination.policyGeneration;
            if(fault==9)
            {
                fixture.nomination.sourceSequence=UINT64_MAX;
                fixture.samples[0].identity.acceptedSequence=UINT64_MAX;
                fixture.samples[1].identity.acceptedSequence=0;
                fixture.samples[2].identity.acceptedSequence=1;
            }
            if(fault==10)
            {
                fixture.samples[0].identity.sourceFrameNumber=UINT64_MAX;
                fixture.samples[1].identity.sourceFrameNumber=0;
                fixture.samples[2].identity.sourceFrameNumber=1;
            }
            const auto result=InspectRememberedEdgeReturnShadowWindow(fixture.nomination,fixture.samples.data(),fixture.samples.size(),2,2);
            const auto message=std::wstring(L"Shadow window nominee/overflow fault ")+std::to_wstring(fault);
            Assert::IsFalse(result.allPass,message.c_str());
            Assert::AreEqual(size_t(0),result.inspectedFrames,message.c_str());
            Assert::AreEqual(size_t(0),result.totalSamples,message.c_str());
        }
    }
    TEST_METHOD(RememberedGuardedShadowRetainsBoundaryPixelsWithoutChangingKnownTargetOrNativeHandoff)
    {
        // Synthetic reproduction of the measured failure: Y70 against Y64 on
        // the first excluded bottom row, and U518 against U512 above the top.
        for(int pattern=0;pattern<3;++pattern)
        {
            RecallRig rig; rig.context.enabled=false; rig.context.shadowOnly=true; rig.QualifiedWide(); rig.Advance();
            RecallPixels pixels; pixels.Rectangle(80,148,960,472,300);
            const auto target=rig.scopeEvidence.trustedBounds;
            const int x=50*(pixels.width-1)/95;
            if(pattern!=1)pixels.Rectangle(x,target.bottom,x+1,target.bottom+1,70);
            if(pattern!=0)pixels.Rectangle(x,target.top-1,x+1,target.top,64,518,512);
            const auto raw=ExtractActivePictureEvidence(pixels.Source());
            Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL && raw.bottom.trusted);
            const auto nomination=rig.model.NominateRememberedEdgeReturnShadow(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
            Assert::IsTrue(nomination.available);
            const auto exact=InspectRememberedEdgeReturnShadow(pixels.Source(),raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
            Assert::IsFalse(exact.wouldVerify,L"The original exact profile must still reject the measured six-code difference.");
            Assert::IsTrue(exact.mismatches>0);
            const auto original=pixels.bytes;
            const auto history=rig.model.GetRememberedEdgeReturnHistoryStatus();
            const auto guarded=InspectRememberedEdgeReturnGuardedShadow(pixels.Source(),raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
            Assert::IsTrue(guarded.wouldVerify,L"Retaining the two boundary rows must allow strict verification of the remaining discarded bands.");
            EqualRecallBounds(target,guarded.target);
            EqualRecallBounds(target,nomination.rememberedBounds);
            Assert::AreEqual(target.top-2,guarded.presentation.top);
            Assert::AreEqual(target.bottom+2,guarded.presentation.bottom);
            Assert::AreEqual(2,guarded.retainedTopRows); Assert::AreEqual(2,guarded.retainedBottomRows);
            Assert::IsTrue(exact.boundaryMismatchSamples>0); Assert::AreEqual(0,exact.deepMismatchSamples);
            Assert::IsTrue(original==pixels.bytes);
            const auto after=rig.model.GetRememberedEdgeReturnHistoryStatus();
            Assert::AreEqual(history.entries,after.entries); Assert::AreEqual(history.qualifiedEntries,after.qualifiedEntries);
            auto untagged=nomination; untagged.shadowOnly=false;
            Assert::IsFalse(InspectRememberedEdgeReturnGuardedShadow(pixels.Source(),raw,untagged,rig.context.sourceSequence,rig.context.timestampMs).wouldVerify);
            Assert::IsFalse(InspectRememberedEdgeReturn(pixels.Source(),raw,untagged,rig.context.sourceSequence,rig.context.timestampMs).candidateAvailable);
            const auto held=rig.model.Observe(MakeActivePictureObservation(raw,rig.context.sourceSequence,24));
            Assert::IsFalse(held.publish); EqualRecallBounds(rig.wideEvidence.trustedBounds,held.stableBounds);
            bool nativePublished=false;
            for(int i=0;i<ActivePictureTransitionModel::INITIAL_CONFIRMATIONS+2;++i)
            {
                const auto native=rig.FeedNative(rig.scope,5);
                if(native.publish)
                {
                    nativePublished=true;
                    EqualRecallBounds(target,native.bounds);
                    Assert::IsTrue(native.authorityOrigin==ActivePictureAuthorityOrigin::NATIVE);
                }
            }
            Assert::IsTrue(nativePublished,L"Subsequent exact native acquisition must publish the original geometry, never the diagnostic padding.");
        }
    }

    TEST_METHOD(RememberedGuardedShadowUsesEvenOutwardEnvelopeAcrossP010P210AndV210)
    {
        for(bool odd:{false,true})
        {
            const int width=odd?2880:960, height=odd?1620:540;
            RecallRig rig(width,height);
            if(odd)
            {
                RecallScope(rig.scope,207,1413); RecallScope(rig.wide,51,1569);
                rig.scopeEvidence=ExtractActivePictureEvidence(rig.scope.Source());
                rig.wideEvidence=ExtractActivePictureEvidence(rig.wide.Source());
            }
            rig.context.enabled=false; rig.context.shadowOnly=true; rig.QualifiedWide(); rig.Advance();
            const auto target=rig.scopeEvidence.trustedBounds;
            RecallPixels p010(width,height,false),p210(width,height,true);
            for(auto* pixels:{&p010,&p210})
            {
                pixels->Rectangle(width/12,target.top+height/6,width,target.bottom,300);
                const int x=50*(width-1)/95;
                pixels->Rectangle(x,target.bottom,x+1,target.bottom+1,70);
                pixels->Rectangle(x,target.top-1,x+1,target.top,64,518,512);
            }
            PackedRecallPixels packed(p210.Source());
            const auto initialRaw=ExtractActivePictureEvidence(p210.Source());
            const auto nomination=rig.model.NominateRememberedEdgeReturnShadow(initialRaw.proposedBounds,initialRaw.top.trusted,initialRaw.bottom.trusted);
            Assert::IsTrue(nomination.available);
            for(const auto& source:{p010.Source(),p210.Source(),packed.Source()})
            {
                const auto raw=ExtractActivePictureEvidence(source);
                Assert::IsFalse(InspectRememberedEdgeReturnShadow(source,raw,nomination,rig.context.sourceSequence,rig.context.timestampMs).wouldVerify);
                const auto result=InspectRememberedEdgeReturnGuardedShadow(source,raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
                Assert::IsTrue(result.wouldVerify);
                EqualRecallBounds(target,result.target);
                Assert::AreEqual(odd?3:2,result.retainedTopRows);
                Assert::AreEqual(odd?3:2,result.retainedBottomRows);
                Assert::AreEqual(0,result.presentation.top%2); Assert::AreEqual(0,result.presentation.bottom%2);
                Assert::AreEqual(target.top-(odd?3:2),result.presentation.top);
                Assert::AreEqual(target.bottom+(odd?3:2),result.presentation.bottom);
                AssertUnchangedRecallEvidence(ExtractActivePictureEvidence(source),raw);
            }
            // The original matched-edge chroma fringe must not migrate to the
            // new padded edge. This P010 pair is wholly outside the envelope.
            const int firstDiscarded=target.bottom+(odd?3:2);
            const int x=50*(width-1)/95;
            p010.Rectangle(x,firstDiscarded,x+1,firstDiscarded+1,64,518,512);
            const auto outsideRaw=ExtractActivePictureEvidence(p010.Source());
            const auto outside=InspectRememberedEdgeReturnGuardedShadow(p010.Source(),outsideRaw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
            Assert::IsFalse(outside.wouldVerify);
            Assert::IsTrue(outside.deepMismatchSamples>0);
        }
    }

    TEST_METHOD(RememberedGuardedShadowStillRejectsEveryConflictOutsideItsFixedEnvelope)
    {
        for(int pattern=0;pattern<5;++pattern)
        {
            RecallRig rig; rig.context.enabled=false; rig.context.shadowOnly=true; rig.QualifiedWide(); rig.Advance();
            RecallPixels pixels; pixels.Rectangle(80,148,960,472,300);
            const auto initial=ExtractActivePictureEvidence(pixels.Source());
            const auto nomination=rig.model.NominateRememberedEdgeReturnShadow(initial.proposedBounds,initial.top.trusted,initial.bottom.trusted);
            Assert::IsTrue(nomination.available);
            const int x=50*(pixels.width-1)/95;
            if(pattern==0)pixels.Rectangle(x,474,x+1,475,70); // First row that will still be discarded.
            if(pattern==1)pixels.Rectangle(0,474,40,476,700); // Caption narrower than the broad row scanner.
            if(pattern==2)pixels.Rectangle(x,65,x+1,66,64,518,512); // Chroma just outside the padded top.
            if(pattern==3)RecallWide(pixels); // Actual taller content cannot be dismissed as boundary noise.
            if(pattern==4) { pixels.Rectangle(0,0,960,540,64); pixels.Rectangle(200,148,960,472,300); }
            const auto raw=ExtractActivePictureEvidence(pixels.Source());
            const auto result=InspectRememberedEdgeReturnGuardedShadow(pixels.Source(),raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
            const auto message=std::wstring(L"Guarded shadow exterior conflict ")+std::to_wstring(pattern);
            Assert::IsFalse(result.wouldVerify,message.c_str());
            if(pattern<3)Assert::IsTrue(result.deepMismatchSamples>0,message.c_str());
            if(pattern==4)Assert::IsTrue(result.matchedEdgeZones[0]<6);
        }
    }

    TEST_METHOD(RememberedGuardedShadowWindowKeepsOneEnvelopeAndCannotSkipAConflictingFuture)
    {
        for(int badIndex=-1;badIndex<=2;++badIndex)
        {
            RecallShadowWindowRig fixture;
            const int x=50*(fixture.pixels.width-1)/95;
            fixture.pixels.Rectangle(x,472,x+1,473,70);
            for(auto& sample:fixture.samples)sample.raw=ExtractActivePictureEvidence(sample.source);
            RecallPixels alternate; alternate.Rectangle(80,148,960,472,300);
            // In the successful window one future is exact-clean, proving that
            // the explicit guarded comparison still uses one deterministic envelope.
            const size_t replaced=badIndex<0?size_t(1):size_t(badIndex);
            if(badIndex>=0)alternate.Rectangle(x,474,x+1,475,70);
            fixture.samples[replaced].source=alternate.Source();
            fixture.samples[replaced].source.generation=replaced?uint64_t(17):uint64_t(7);
            fixture.samples[replaced].raw=ExtractActivePictureEvidence(fixture.samples[replaced].source);
            const auto strict=InspectRememberedEdgeReturnShadowWindow(fixture.nomination,fixture.samples.data(),fixture.samples.size(),2,2);
            Assert::IsFalse(strict.allPass,L"The exact diagnostic must not silently inherit the new outward envelope.");
            const auto guarded=InspectRememberedEdgeReturnGuardedShadowWindow(fixture.nomination,fixture.samples.data(),fixture.samples.size(),2,2);
            const auto message=std::wstring(L"Guarded window conflicting index ")+std::to_wstring(badIndex);
            Assert::AreEqual(badIndex<0,guarded.allPass,message.c_str());
            if(badIndex<0)
            {
                Assert::AreEqual(size_t(3),guarded.passedFrames);
                EqualRecallBounds(fixture.nomination.rememberedBounds,guarded.target);
                Assert::AreEqual(66,guarded.presentation.top); Assert::AreEqual(474,guarded.presentation.bottom);
                Assert::AreEqual(2,guarded.retainedTopRows); Assert::AreEqual(2,guarded.retainedBottomRows);
            }
            else
            {
                Assert::AreEqual(badIndex,guarded.failedIndex,message.c_str());
                Assert::AreEqual(size_t(badIndex),guarded.passedFrames,message.c_str());
                Assert::IsTrue(guarded.failureInspection.deepMismatchSamples>0,message.c_str());
            }
        }
    }
    TEST_METHOD(RememberedGuardedShadowReproducesFourKMeasuredPointAt2020And1884)
    {
        RecallRig rig(3840,2160); rig.context.enabled=false; rig.context.shadowOnly=true; rig.QualifiedWide(); rig.Advance();
        RecallPixels pixels(3840,2160); pixels.Rectangle(320,316,3840,1884,300);
        pixels.Rectangle(2020,1884,2021,1885,70);
        const auto raw=ExtractActivePictureEvidence(pixels.Source());
        Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL && raw.bottom.trusted);
        const auto nomination=rig.model.NominateRememberedEdgeReturnShadow(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
        Assert::IsTrue(nomination.available);
        Assert::AreEqual(276,nomination.rememberedBounds.top); Assert::AreEqual(1884,nomination.rememberedBounds.bottom);
        const auto exact=InspectRememberedEdgeReturnShadow(pixels.Source(),raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
        Assert::IsFalse(exact.wouldVerify);
        Assert::AreEqual(2020,exact.firstMismatchX); Assert::AreEqual(1884,exact.firstMismatchY);
        Assert::AreEqual(6,exact.maxLumaDelta);
        const auto guarded=InspectRememberedEdgeReturnGuardedShadow(pixels.Source(),raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
        Assert::IsTrue(guarded.wouldVerify);
        EqualRecallBounds(nomination.rememberedBounds,guarded.target);
        Assert::AreEqual(274,guarded.presentation.top); Assert::AreEqual(1886,guarded.presentation.bottom);
        Assert::AreEqual(2,guarded.retainedTopRows); Assert::AreEqual(2,guarded.retainedBottomRows);
    }
    TEST_METHOD(RememberedGuardedShadowCannotCrossOrTouchEstablishedBase)
    {
        // Prospective safety coverage: the exact target is still inward and
        // materially narrower, but padding must never reach beyond that base.
        for(bool upper:{false,true}) for(int gap:{1,2})
        {
            RecallRig rig; rig.context.enabled=false; rig.context.shadowOnly=true; rig.QualifiedWide(); rig.Advance();
            RecallPixels pixels; pixels.Rectangle(80,148,960,472,300);
            const auto raw=ExtractActivePictureEvidence(pixels.Source());
            auto nomination=rig.model.NominateRememberedEdgeReturnShadow(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
            Assert::IsTrue(nomination.available);
            const auto target=nomination.rememberedBounds;
            if(upper)nomination.establishedBase.top=target.top-gap;
            else nomination.establishedBase.bottom=target.bottom+gap;
            nomination.establishedBase.aspectRatio=static_cast<double>(nomination.establishedBase.right-nomination.establishedBase.left)/
                (nomination.establishedBase.bottom-nomination.establishedBase.top);
            const auto exact=InspectRememberedEdgeReturnShadow(pixels.Source(),raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
            Assert::IsTrue(exact.wouldVerify,L"The nominal target remains eligible; this case must isolate padding containment.");
            const auto guarded=InspectRememberedEdgeReturnGuardedShadow(pixels.Source(),raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
            Assert::IsFalse(guarded.wouldVerify);
            Assert::IsTrue(std::string(guarded.reason)=="retained-envelope-outside-established-base");
            Assert::AreEqual(size_t(0),guarded.samples);
            EqualRecallBounds(target,guarded.target);
        }
    }
    TEST_METHOD(RememberedGuardedShadowPreservesNominalDeepSamplingPhaseAtFourK)
    {
        RecallRig rig(3840,2160); rig.context.enabled=false; rig.context.shadowOnly=true; rig.QualifiedWide(); rig.Advance();
        RecallPixels pixels(3840,2160); pixels.Rectangle(320,316,3840,1884,300);
        // This is an original four-row-cadence sample, well beyond the retained
        // two rows and the eight adjacent rows. Padding must not move that grid.
        pixels.Rectangle(2020,1900,2021,1901,70);
        const auto raw=ExtractActivePictureEvidence(pixels.Source());
        Assert::IsTrue(raw.classification==ActivePictureClassification::PROVISIONAL && raw.bottom.trusted);
        const auto nomination=rig.model.NominateRememberedEdgeReturnShadow(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
        Assert::IsTrue(nomination.available);
        const auto exact=InspectRememberedEdgeReturnShadow(pixels.Source(),raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
        Assert::IsFalse(exact.wouldVerify);
        Assert::AreEqual(2020,exact.firstMismatchX); Assert::AreEqual(1900,exact.firstMismatchY);
        Assert::AreEqual(6,exact.maxLumaDelta); Assert::IsTrue(exact.deepMismatchSamples>0);
        const auto guarded=InspectRememberedEdgeReturnGuardedShadow(pixels.Source(),raw,nomination,rig.context.sourceSequence,rig.context.timestampMs);
        Assert::IsFalse(guarded.wouldVerify,L"Retaining boundary rows must not drop an existing deep exterior sample.");
        Assert::AreEqual(2020,guarded.firstMismatchX); Assert::AreEqual(1900,guarded.firstMismatchY);
        Assert::IsTrue(guarded.deepMismatchSamples>0);
    }
    TEST_METHOD(GuardedRememberedActivePublishesOnlyVerifiedWindowAndEvaluatesPaddedCrop)
    {
        GuardedActiveRecallRig fixture;
        Assert::IsTrue(fixture.nomination.available && fixture.nomination.guarded && !fixture.nomination.shadowOnly);
        const auto original=fixture.pixels.bytes;
        const auto history=fixture.rig.model.GetRememberedEdgeReturnHistoryStatus();
        const auto before=GuardedRecallSnapshot(fixture.rig.model);
        const auto certificate=fixture.Build();
        Assert::IsTrue(certificate.available,L"Actual qualified pixels must build a guarded authority certificate.");
        Assert::AreEqual(size_t(5),certificate.frameCount);
        EqualRecallBounds(fixture.rig.scopeEvidence.trustedBounds,certificate.nomination.rememberedBounds);
        Assert::IsTrue(certificate.currentEvidence.rememberedEdgeReturnProof.guarded);
        Assert::AreEqual(66,certificate.currentEvidence.trustedBounds.top);
        Assert::AreEqual(474,certificate.currentEvidence.trustedBounds.bottom);
        EqualRecallBounds(before.stableBounds,GuardedRecallSnapshot(fixture.rig.model).stableBounds);
        Assert::IsTrue(fixture.timeline.CanProveBufferedFrames(certificate.identities.data(),certificate.frameCount));
        ActivePictureTransitionDecision decision;
        Assert::IsTrue(fixture.Adopt(certificate,&decision));
        Assert::IsTrue(decision.publish && decision.stable);
        Assert::IsTrue(decision.authorityOrigin==ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN);
        Assert::AreEqual(66,decision.bounds.top); Assert::AreEqual(474,decision.bounds.bottom);
        const auto retention=EvaluateActivePicturePresentationRetention(fixture.pixels.Source(),decision.bounds);
        Assert::IsTrue(retention.currentlyPixelSafe);
        AlphaSourceCrop::Input input;
        input.automaticCropEnabled=input.sharedGeometryAvailable=true;
        input.geometry=decision.bounds; input.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
        input.geometrySourceGeneration=input.frameSourceGeneration=7;
        input.frameSourceSequence=fixture.rig.context.sourceSequence;
        input.rasterWidth=960; input.rasterHeight=540;
        input.latestObservationClassification=fixture.samples[0].raw.classification;
        input.latestObservationIsProvisional=true;
        input.frameLocalPresentationRetentionEvaluated=true;
        input.frameLocalPresentationRetentionSafe=retention.CanRetainPresentation();
        const auto crop=AlphaSourceCrop::Evaluate(input);
        Assert::IsTrue(crop.applyCrop); EqualRecallBounds(decision.bounds,crop.sourceBounds);
        const auto after=fixture.rig.model.GetRememberedEdgeReturnHistoryStatus();
        Assert::AreEqual(history.entries,after.entries); Assert::AreEqual(history.maxConfirmedScenes,after.maxConfirmedScenes);
        Assert::IsTrue(original==fixture.pixels.bytes);
    }

    TEST_METHOD(GuardedRememberedActiveNativeHandoffKeepsEnvelopeWithoutLearningOrSkippingFrames)
    {
        GuardedActiveRecallRig fixture; const auto certificate=fixture.Build();
        Assert::IsTrue(certificate.available); ActivePictureTransitionDecision guarded;
        Assert::IsTrue(fixture.Adopt(certificate,&guarded));
        auto queuedModel=fixture.rig.model;
        ActivePictureTransitionDecision scheduled; scheduled.publish=scheduled.stable=true;
        scheduled.bounds=fixture.rig.scopeEvidence.trustedBounds; scheduled.stableBounds=guarded.bounds;
        scheduled.authorityOrigin=scheduled.stableAuthorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
        scheduled.authoritativeClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
        Assert::IsFalse(queuedModel.AdoptPublishedDecision(scheduled,ActivePictureClassification::BAR_CROP_TRUSTED),
            L"A queued nominal native decision cannot bypass the live handoff and shrink the presentation by two rows.");
        EqualRecallBounds(guarded.bounds,GuardedRecallSnapshot(queuedModel).stableBounds);
        auto immediatelyWider=fixture.rig.model; auto nextContext=fixture.rig.context;
        ++nextContext.sourceSequence; nextContext.timestampMs+=40; nextContext.sceneId=5;
        immediatelyWider.SetRememberedEdgeReturnContext(nextContext);
        const auto firstWide=immediatelyWider.Observe(MakeActivePictureObservation(fixture.rig.wideEvidence,nextContext.sourceSequence,24));
        Assert::IsTrue(firstWide.publish || firstWide.matchingCandidates>0,L"The first source frame after adoption must be evaluated, not skipped as already observed future proof.");
        const auto history=fixture.rig.model.GetRememberedEdgeReturnHistoryStatus();
        for(int i=0;i<5;++i)
        {
            fixture.rig.Advance(5);
            const auto native=MakeActivePictureObservation(fixture.rig.scopeEvidence,fixture.rig.context.sourceSequence,24);
            const auto handoff=fixture.rig.model.Observe(native);
            EqualRecallBounds(guarded.bounds,handoff.stableBounds);
            Assert::IsTrue(handoff.stableAuthorityOrigin==ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN);
            Assert::IsFalse(fixture.rig.model.RecordIndependentNativeGeometry(native),L"Nominal handoff must not train padded inferred geometry as native history.");
        }
        const auto after=fixture.rig.model.GetRememberedEdgeReturnHistoryStatus();
        Assert::AreEqual(history.entries,after.entries); Assert::AreEqual(history.maxConfirmedScenes,after.maxConfirmedScenes);
        bool published=false;
        for(int i=0;i<ActivePictureTransitionModel::INITIAL_CONFIRMATIONS+2;++i)
        {
            const auto wider=fixture.rig.FeedNative(fixture.rig.wide,6);
            published=published||wider.publish;
            if(wider.publish)EqualRecallBounds(fixture.rig.wideEvidence.trustedBounds,wider.bounds);
        }
        Assert::IsTrue(published,L"Keeping a two-row envelope must not block a genuine later wider format.");
        fixture.rig.model.Reset();
        Assert::IsFalse(GuardedRecallSnapshot(fixture.rig.model).stable);
        fixture.rig.Establish(fixture.rig.scope,7);
        const auto resetNative=GuardedRecallSnapshot(fixture.rig.model);
        EqualRecallBounds(fixture.rig.scopeEvidence.trustedBounds,resetNative.stableBounds);
        Assert::IsTrue(resetNative.stableAuthorityOrigin==ActivePictureAuthorityOrigin::NATIVE);
    }

    TEST_METHOD(GuardedRememberedActiveRejectsStaleCommitAndLeavesAuthorityUnchanged)
    {
        for(int fault=0;fault<15;++fault)
        {
            GuardedActiveRecallRig fixture; auto certificate=fixture.Build(); Assert::IsTrue(certificate.available);
            auto current=fixture.samples[0].identity; auto context=fixture.rig.context;
            auto source=fixture.pixels.Source(); auto raw=fixture.samples[0].raw;
            uint64_t continuity=fixture.timeline.ContinuityGeneration();
            if(fault==0)++continuity;
            if(fault==1){++current.acceptedSequence;++context.sourceSequence;}
            if(fault==2){++current.transportGeneration;++context.sourceGeneration;++source.generation;}
            if(fault==3){++current.sourceFormatGeneration;++context.sourceFormatGeneration;}
            if(fault==4){++current.rendererGeneration;++context.rendererGeneration;}
            if(fault==5){++current.viewportGeneration;++context.viewportGeneration;}
            if(fault==6)++context.policyGeneration;
            if(fault==7)context.timestampMs+=501;
            if(fault==8)++certificate.nomination.historyRevision;
            if(fault==9)
            {
                ActivePictureTransitionDecision changed; changed.publish=changed.stable=true;
                changed.bounds=fixture.rig.scopeEvidence.trustedBounds;
                changed.stableBounds=GuardedRecallSnapshot(fixture.rig.model).stableBounds;
                changed.authorityOrigin=changed.stableAuthorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
                changed.authoritativeClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
                Assert::IsTrue(fixture.rig.model.AdoptPublishedDecision(changed,ActivePictureClassification::BAR_CROP_TRUSTED));
            }
            if(fault==10)context.guardedEnabled=false;
            if(fault==11)context.shadowOnly=true;
            if(fault==12){fixture.pixels.Rectangle(0,480,40,483,700);raw=ExtractActivePictureEvidence(source);}
            if(fault==13)source.width-=2;
            if(fault==14)
            {
                certificate.observations[1].bounds.top-=2;
                certificate.observations[1].rememberedEdgeReturnProof.presentationBounds.top-=2;
            }
            const auto before=GuardedRecallSnapshot(fixture.rig.model);
            const auto message=std::wstring(L"Guarded active stale commit fault ")+std::to_wstring(fault);
            auto outEvidence=raw; auto outDecision=before;
            Assert::IsFalse(ValidateAndAdoptGuardedRememberedEdgeReturn(fixture.rig.model,certificate,source,raw,current,context,continuity,&outDecision,&outEvidence),message.c_str());
            AssertUnchangedRecallEvidence(raw,outEvidence);
            EqualRecallBounds(before.stableBounds,outDecision.stableBounds);
            Assert::AreEqual(before.publish,outDecision.publish,message.c_str());
            const auto after=GuardedRecallSnapshot(fixture.rig.model);
            EqualRecallBounds(before.stableBounds,after.stableBounds);
            Assert::AreEqual(int(before.stableAuthorityOrigin),int(after.stableAuthorityOrigin),message.c_str());
        }
    }

    TEST_METHOD(GuardedRememberedActiveCannotSkipConflictOrReuseConsumedQueueWindow)
    {
        for(int fault=0;fault<9;++fault)
        {
            GuardedActiveRecallRig fixture; Assert::IsTrue(fixture.Build().available);
            RecallPixels conflict; conflict.Rectangle(80,148,960,472,300);
            if(fault<3)
            {
                const size_t index=fault==0?size_t(0):(fault==1?size_t(2):size_t(4));
                conflict.Rectangle(0,480,40,483,700);
                fixture.samples[index].source=conflict.Source();
                fixture.samples[index].source.generation=index?17:7;
                fixture.samples[index].raw=ExtractActivePictureEvidence(fixture.samples[index].source);
            }
            if(fault==3)fixture.samples[2].available=false;
            if(fault==4)fixture.samples[2].cadenceRepeat=true;
            if(fault==5)fixture.samples[4].identity.acceptedSequence=fixture.samples[3].identity.acceptedSequence;
            if(fault==6)++fixture.samples[4].identity.viewportGeneration;
            if(fault==7)++fixture.samples[4].policyGeneration;
            if(fault==8)++fixture.samples[4].identity.sourceFormatGeneration;
            const auto message=std::wstring(L"Guarded active future fault ")+std::to_wstring(fault);
            Assert::IsFalse(fixture.Build().available,message.c_str());
            EqualRecallBounds(fixture.rig.wideEvidence.trustedBounds,GuardedRecallSnapshot(fixture.rig.model).stableBounds);
        }
        GuardedActiveRecallRig fixture; const auto certificate=fixture.Build(); Assert::IsTrue(certificate.available);
        fixture.timeline.MarkConsumed(certificate.identities[0]);
        Assert::IsFalse(fixture.timeline.CanProveBufferedFrames(certificate.identities.data(),certificate.frameCount));
        // Renderer must check this existing gate immediately before adoption;
        // the pure certificate owns identities, not the live queue or its locks.
        EqualRecallBounds(fixture.rig.wideEvidence.trustedBounds,GuardedRecallSnapshot(fixture.rig.model).stableBounds);
    }

    TEST_METHOD(GuardedRememberedActiveRequiresExplicitOptInAndAvailableConfirmationFrames)
    {
        for(int fault=0;fault<4;++fault)
        {
            RecallRig rig; rig.context.guardedEnabled=true;
            if(fault==0)rig.context.guardedEnabled=false;
            if(fault==1)rig.context.enabled=false;
            if(fault==2)rig.context.shadowOnly=true;
            rig.QualifiedWide(); rig.Advance();
            RecallPixels partial;partial.Rectangle(80,148,960,472,300);
            const auto raw=ExtractActivePictureEvidence(partial.Source());
            const auto nominee=rig.model.NominateGuardedRememberedEdgeReturn(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
            Assert::AreEqual(fault==3,nominee.available);
        }
        GuardedActiveRecallRig fixture; Assert::IsTrue(fixture.Build().available);
        Assert::IsFalse(fixture.Build(0,4).available); Assert::IsFalse(fixture.Build(5,0).available);
        const auto two=fixture.Build(5,1); Assert::IsTrue(two.available);
        Assert::AreEqual(size_t(2),two.frameCount);
        auto freshModel=fixture.rig.model; auto freshContext=fixture.rig.context; ++freshContext.timestampMs;
        const auto freshCertificate=fixture.Build();
        Assert::IsTrue(ValidateAndAdoptGuardedRememberedEdgeReturn(freshModel,freshCertificate,fixture.pixels.Source(),fixture.samples[0].raw,
            fixture.samples[0].identity,freshContext,fixture.timeline.ContinuityGeneration()),L"The same captured frame remains valid one millisecond later within the bounded consumption age.");
        const auto three=fixture.Build(2,4); Assert::IsTrue(three.available);
        Assert::AreEqual(size_t(3),three.frameCount);
        auto unguarded=fixture.nomination;unguarded.guarded=false;
        Assert::IsFalse(BuildGuardedRememberedEdgeReturnCertificate(fixture.rig.model,unguarded,fixture.samples.data(),fixture.samples.size(),5,4,fixture.timeline.ContinuityGeneration()).available);
        auto shadow=fixture.nomination;shadow.shadowOnly=true;
        Assert::IsFalse(BuildGuardedRememberedEdgeReturnCertificate(fixture.rig.model,shadow,fixture.samples.data(),fixture.samples.size(),5,4,fixture.timeline.ContinuityGeneration()).available);
    }
    TEST_METHOD(GuardedRememberedActiveCannotPublishFromOrdinaryLiveInspection)
    {
        GuardedActiveRecallRig fixture;
        auto live=fixture.rig.model; auto context=fixture.rig.context;
        const auto history=live.GetRememberedEdgeReturnHistoryStatus();
        for(int i=0;i<ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS+2;++i)
        {
            if(i){++context.sourceSequence;context.timestampMs+=40;live.SetRememberedEdgeReturnContext(context);}
            const auto& raw=fixture.samples[0].raw;
            const auto nominee=live.NominateGuardedRememberedEdgeReturn(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
            Assert::IsTrue(nominee.available);
            const auto inspected=InspectGuardedRememberedEdgeReturn(fixture.pixels.Source(),raw,nominee,context.sourceSequence,context.timestampMs);
            Assert::IsTrue(inspected.candidateAvailable && inspected.evidence.rememberedEdgeReturnProof.guarded);
            const auto observation=MakeActivePictureObservation(inspected.evidence,context.sourceSequence,24);
            const auto ordinary=live.Observe(observation);
            Assert::IsFalse(ordinary.publish,L"Repeated individual guarded frames cannot replace the dedicated complete-window admission.");
            EqualRecallBounds(fixture.rig.wideEvidence.trustedBounds,ordinary.stableBounds);
            Assert::IsFalse(live.RecordIndependentNativeGeometry(observation));
        }
        const auto after=live.GetRememberedEdgeReturnHistoryStatus();
        Assert::AreEqual(history.entries,after.entries); Assert::AreEqual(history.maxConfirmedScenes,after.maxConfirmedScenes);
    }
    TEST_METHOD(GuardedRememberedActiveFourKMeasuredBoundaryPublishesPaddedFinalCrop)
    {
        GuardedActiveRecallRig fixture(5,3840,2160);
        const auto buildStarted=std::chrono::steady_clock::now();
        const auto certificate=fixture.Build();
        const double buildMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-buildStarted).count();
        Assert::IsTrue(certificate.available);
        Assert::AreEqual(276,certificate.nomination.rememberedBounds.top);
        Assert::AreEqual(1884,certificate.nomination.rememberedBounds.bottom);
        ActivePictureTransitionDecision decision;
        const auto adoptStarted=std::chrono::steady_clock::now();
        Assert::IsTrue(fixture.Adopt(certificate,&decision));
        const double adoptMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-adoptStarted).count();
        std::ostringstream timing;
        timing << "Synthetic4K guarded active: frames=" << certificate.frameCount << " whole_window_build_ms=" << buildMs << " current_frame_adopt_ms=" << adoptMs;
        Logger::WriteMessage(timing.str().c_str());
        Assert::AreEqual(274,decision.bounds.top); Assert::AreEqual(1886,decision.bounds.bottom);
        const auto retention=EvaluateActivePicturePresentationRetention(fixture.pixels.Source(),decision.bounds);
        Assert::IsTrue(retention.currentlyPixelSafe);
        AlphaSourceCrop::Input input;
        input.automaticCropEnabled=input.sharedGeometryAvailable=true;
        input.geometry=decision.bounds; input.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
        input.geometrySourceGeneration=input.frameSourceGeneration=7;
        input.frameSourceSequence=fixture.rig.context.sourceSequence;
        input.rasterWidth=3840;input.rasterHeight=2160;
        input.latestObservationClassification=fixture.samples[0].raw.classification;
        input.latestObservationIsProvisional=true;
        input.frameLocalPresentationRetentionEvaluated=input.frameLocalPresentationRetentionSafe=true;
        const auto finalCrop=AlphaSourceCrop::Evaluate(input);
        Assert::IsTrue(finalCrop.applyCrop); EqualRecallBounds(decision.bounds,finalCrop.sourceBounds);
        Assert::IsTrue(finalCrop.sourceBounds.bottom>1884,L"The measured Y70 pixel at2020,1884 is retained, never reclassified as black.");
    }
    TEST_METHOD(GuardedSceneHoldValidatedWindowReplacesHeldWideSnapshotBeforeDeadline)
    {
        GuardedActiveRecallRig fixture(5,3840,2160);
        const auto certificate=fixture.Build(); Assert::IsTrue(certificate.available);
        const auto held=GuardedRecallSceneHold(fixture,certificate);
        const auto ready=AlphaSourceCrop::EvaluateGuardedSceneHold(held);
        Assert::IsTrue(ready.mayVerify,L"A same-generation exact-base crop-only hold must not block current guarded certificate verification.");
        Assert::IsFalse(ready.clearSnapshot);
        Assert::IsTrue(ready.hold.cropActive,L"Permission to verify cannot clear a hold before commit.");
        EqualRecallBounds(fixture.rig.wideEvidence.trustedBounds,GuardedRecallSnapshot(fixture.rig.model).stableBounds);
        ActivePictureTransitionDecision transition;
        const bool committed=fixture.Adopt(certificate,&transition);
        Assert::IsTrue(committed);
        const auto released=AlphaSourceCrop::EvaluateGuardedSceneHold(held,committed);
        Assert::IsTrue(released.clearSnapshot);
        Assert::IsFalse(released.hold.cropActive || released.hold.nlsActive);
        Assert::IsTrue(held.hold.cropActive,L"The pure decision must not mutate the caller's snapshot.");
        AlphaSourceCrop::Input finalInput;
        finalInput.automaticCropEnabled=finalInput.sharedGeometryAvailable=true;
        finalInput.geometry=released.hold.cropActive?held.snapshotBounds:transition.bounds;
        finalInput.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
        finalInput.geometrySourceGeneration=finalInput.frameSourceGeneration=7;
        finalInput.frameSourceSequence=fixture.rig.context.sourceSequence;
        finalInput.rasterWidth=3840;finalInput.rasterHeight=2160;
        finalInput.sceneVerificationHoldActive=released.hold.cropActive;
        finalInput.latestObservationClassification=certificate.currentEvidence.classification;
        finalInput.latestObservationSupportsCrop=true;
        const auto finalCrop=AlphaSourceCrop::Evaluate(finalInput);
        Assert::IsTrue(finalCrop.applyCrop);
        EqualRecallBounds(transition.bounds,finalCrop.sourceBounds);
        Assert::AreEqual(274,finalCrop.sourceBounds.top);Assert::AreEqual(1886,finalCrop.sourceBounds.bottom);
    }

    TEST_METHOD(GuardedSceneHoldPreservesCutNlsAndMismatchedSnapshotGuards)
    {
        GuardedActiveRecallRig fixture; const auto certificate=fixture.Build(); Assert::IsTrue(certificate.available);
        for(int fault=0;fault<11;++fault)
        {
            auto input=GuardedRecallSceneHold(fixture,certificate);
            if(fault==0)input.certificateAvailable=false;
            if(fault==1)input.cutOrDiscontinuity=true;
            if(fault==2)input.hold.nlsActive=true;
            if(fault==3)input.snapshotAvailable=false;
            if(fault==4)++input.snapshotGeneration;
            if(fault==5)input.currentGeneration=0;
            if(fault==6)input.currentNativeAvailable=false;
            if(fault==7)input.snapshotBounds.top+=2;
            if(fault==8)input.currentNativeBase.bottom-=2;
            if(fault==9)input.nominationBase.rasterWidth-=2;
            if(fault==10)input.snapshotBounds.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
            const auto message=std::wstring(L"Guarded scene-hold fault ")+std::to_wstring(fault);
            for(bool committed:{false,true})
            {
                const auto result=AlphaSourceCrop::EvaluateGuardedSceneHold(input,committed);
                Assert::IsFalse(result.mayVerify,message.c_str());
                Assert::IsFalse(result.clearSnapshot,message.c_str());
                Assert::AreEqual(input.hold.cropActive,result.hold.cropActive,message.c_str());
                Assert::AreEqual(input.hold.nlsActive,result.hold.nlsActive,message.c_str());
            }
        }
    }

    TEST_METHOD(GuardedSceneHoldInvalidCertificateCannotClearOrReplaceHeldGeometry)
    {
        for(int fault=0;fault<3;++fault)
        {
            GuardedActiveRecallRig fixture; auto certificate=fixture.Build(); Assert::IsTrue(certificate.available);
            const auto held=GuardedRecallSceneHold(fixture,certificate);
            Assert::IsTrue(AlphaSourceCrop::EvaluateGuardedSceneHold(held).mayVerify);
            if(fault==0)++certificate.continuityGeneration;
            if(fault==1)certificate.observations[certificate.frameCount-1].rememberedEdgeReturnProof.guarded=false;
            // A current caption can invalidate a previously safe queued proof.
            if(fault==2)fixture.pixels.Rectangle(0,480,40,483,700);
            const auto raw=ExtractActivePictureEvidence(fixture.pixels.Source());
            ActivePictureTransitionDecision transition;
            const bool committed=ValidateAndAdoptGuardedRememberedEdgeReturn(fixture.rig.model,certificate,fixture.pixels.Source(),raw,
                fixture.samples[0].identity,fixture.rig.context,fixture.timeline.ContinuityGeneration(),&transition);
            Assert::IsFalse(committed);
            const auto retained=AlphaSourceCrop::EvaluateGuardedSceneHold(held,committed);
            Assert::IsTrue(retained.hold.cropActive);
            Assert::IsFalse(retained.clearSnapshot || transition.publish);
            EqualRecallBounds(held.snapshotBounds,GuardedRecallSnapshot(fixture.rig.model).stableBounds);
        }
    }
    TEST_METHOD(GuardedSceneHoldPreservesReturnFromNativeFullRasterWithOrWithoutSnapshot)
    {
        GuardedActiveRecallRig fixture;
        RecallPixels full; full.Rectangle(0,0,960,540,300);
        const auto fullRaw=ExtractActivePictureEvidence(full.Source());
        Assert::IsTrue(fullRaw.classification==ActivePictureClassification::FULL_RASTER_TRUSTED);
        bool fullPublished=false;
        for(int i=0;i<ActivePictureTransitionModel::INITIAL_CONFIRMATIONS+2;++i)
        {
            fixture.rig.Advance(5);
            const auto decision=fixture.rig.model.Observe(MakeActivePictureObservation(fullRaw,fixture.rig.context.sourceSequence,24));
            fullPublished=fullPublished||decision.publish;
        }
        Assert::IsTrue(fullPublished);
        const auto stable=GuardedRecallSnapshot(fixture.rig.model);
        EqualRecallBounds(fullRaw.trustedBounds,stable.stableBounds);
        Assert::IsTrue(stable.stableAuthorityOrigin==ActivePictureAuthorityOrigin::NATIVE);
        Assert::IsTrue(stable.stableBounds.trustedBarAxes==ActivePictureBounds::BarAxes::NONE);
        fixture.rig.wideEvidence=fullRaw;
        fixture.rig.Advance(6);
        const auto& raw=fixture.samples[0].raw;
        fixture.nomination=fixture.rig.model.NominateGuardedRememberedEdgeReturn(raw.proposedBounds,raw.top.trusted,raw.bottom.trusted);
        Assert::IsTrue(fixture.nomination.available);
        for(size_t i=0;i<fixture.samples.size();++i)
        {
            auto& sample=fixture.samples[i];
            sample.identity.acceptedSequence=fixture.rig.context.sourceSequence+i;
            sample.identity.sourceFrameNumber=2000+i;sample.identity.captureTimestamp=200000+i*1000;
            sample.timestampMs=fixture.rig.context.timestampMs;
            Assert::IsTrue(fixture.timeline.TrackAcceptedFrame(sample.identity));
        }
        const auto certificate=fixture.Build();
        Assert::IsTrue(certificate.available,L"The existing guarded authority path supports return from a genuine native full raster.");
        for(bool withSnapshot:{false,true})
        {
            auto input=GuardedRecallSceneHold(fixture,certificate);
            if(!withSnapshot){input.hold={};input.snapshotAvailable=false;}
            const auto pending=AlphaSourceCrop::EvaluateGuardedSceneHold(input);
            Assert::IsTrue(pending.mayVerify,L"Scene-hold compatibility must not revoke the existing full-raster base route merely because its trusted axes are NONE.");
            Assert::IsFalse(pending.clearSnapshot);
            Assert::AreEqual(input.hold.cropActive,pending.hold.cropActive);
            auto trial=fixture.rig.model;
            ActivePictureTransitionDecision transition;
            const bool committed=ValidateAndAdoptGuardedRememberedEdgeReturn(trial,certificate,fixture.pixels.Source(),raw,
                fixture.samples[0].identity,fixture.rig.context,fixture.timeline.ContinuityGeneration(),&transition);
            Assert::IsTrue(committed);
            const auto released=AlphaSourceCrop::EvaluateGuardedSceneHold(input,committed);
            Assert::IsTrue(released.clearSnapshot);
            Assert::IsFalse(released.hold.cropActive || released.hold.nlsActive);
            Assert::AreEqual(66,transition.bounds.top);Assert::AreEqual(474,transition.bounds.bottom);
        }
    }
    TEST_METHOD(RememberedFamilyReplacementNeedsIndependentScenesAndOnlyChangesFutureRecall)
    {
        // Real native pixel extraction; Record is intentionally tested against a
        // retained native stable rectangle, without authorizing a live crop change.
        for(bool flatFamily:{false,true}) {
            RecallRig rig(3840,2160); RecallPixels oldPixels(3840,2160),newPixels(3840,2160),full(3840,2160);
            const int oldTop=flatFamily?40:264,newTop=flatFamily?68:276;
            RecallScope(oldPixels,oldTop,2160-oldTop); RecallScope(newPixels,newTop,2160-newTop);
            full.Rectangle(0,0,3840,2160,300);
            const auto oldRaw=ExtractActivePictureEvidence(oldPixels.Source()),raw=ExtractActivePictureEvidence(newPixels.Source());
            const auto fullRaw=ExtractActivePictureEvidence(full.Source());
            Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
            rig.Establish(oldPixels,1);for(int i=0;i<3;++i)rig.FeedNative(oldPixels,2);
            const auto stable=GuardedRecallSnapshot(rig.model).stableBounds;
            auto nominateFromFull=[&](const ActivePictureBounds& target,uint64_t scene) {
                auto trial=rig;
                for(int i=0;i<ActivePictureTransitionModel::INITIAL_CONFIRMATIONS+2;++i) {
                    trial.Advance(scene);trial.model.Observe(MakeActivePictureObservation(fullRaw,trial.context.sourceSequence,24));
                }
                EqualRecallBounds(fullRaw.trustedBounds,GuardedRecallSnapshot(trial.model).stableBounds);
                trial.Advance(scene);auto observed=target;observed.bottom-=80;
                return trial.model.NominateRememberedEdgeReturn(observed,true,false);
            };
            for(uint64_t scene:{uint64_t(3),uint64_t(4)})for(int i=0;i<3;++i) {
                rig.Advance(scene);
                Assert::IsTrue(rig.model.RecordIndependentNativeGeometry(MakeActivePictureObservation(raw,rig.context.sourceSequence,24)),
                    L"An independently native same-family measurement may correct future memory without moving the retained presentation.");
                EqualRecallBounds(stable,GuardedRecallSnapshot(rig.model).stableBounds);
                Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
                Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().entries);
                if(scene==3 || i<2) {
                    // For 1.85 the full-raster inward transition is below the existing
                    // five-percent gate, so status and scope-family recall cover this phase.
                    if(!flatFamily)Assert::IsTrue(nominateFromFull(oldRaw.trustedBounds,5).available);
                    Assert::IsFalse(nominateFromFull(raw.trustedBounds,5).available);
                }
            }
            Assert::IsFalse(nominateFromFull(raw.trustedBounds,4).available,
                L"The replacement cannot infer a correction in its own qualification scene.");
            const auto replacement=nominateFromFull(raw.trustedBounds,5);
            Assert::IsTrue(replacement.available);
            EqualRecallBounds(raw.trustedBounds,replacement.rememberedBounds);
            Assert::IsFalse(nominateFromFull(oldRaw.trustedBounds,5).available);
            EqualRecallBounds(stable,GuardedRecallSnapshot(rig.model).stableBounds);
        }
    }

    TEST_METHOD(RememberedFamilyAlternatingExactCandidatesCannotPoolOrReplace)
    {
        RecallRig rig(3840,2160);RecallPixels oldPixels(3840,2160),a(3840,2160),b(3840,2160);
        RecallScope(oldPixels,264,1896);RecallScope(a,276,1884);RecallScope(b,280,1880);
        rig.Establish(oldPixels,1);for(int i=0;i<3;++i)rig.FeedNative(oldPixels,2);
        const auto oldRaw=ExtractActivePictureEvidence(oldPixels.Source()),first=ExtractActivePictureEvidence(a.Source()),second=ExtractActivePictureEvidence(b.Source());
        const auto stable=GuardedRecallSnapshot(rig.model).stableBounds;
        for(int i=0;i<24;++i) {
            rig.Advance(3+i/6);
            Assert::IsTrue(rig.model.RecordIndependentNativeGeometry(MakeActivePictureObservation(i%2?first:second,rig.context.sourceSequence,24)));
            Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().entries);
            Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
            EqualRecallBounds(stable,GuardedRecallSnapshot(rig.model).stableBounds);
        }
        rig.Establish(rig.wide,7,false);
        for(const auto& raw:{oldRaw,first,second}) {
            rig.Advance(8);auto observed=raw.trustedBounds;observed.bottom-=80;
            const auto nomination=rig.model.NominateRememberedEdgeReturn(observed,true,false);
            Assert::AreEqual(raw.trustedBounds.top==oldRaw.trustedBounds.top,nomination.available);
            if(nomination.available)EqualRecallBounds(oldRaw.trustedBounds,nomination.rememberedBounds);
        }
    }

    TEST_METHOD(RememberedFamilyRetiredSameEdgeAlternativesRemainAmbiguousAcrossCorrections)
    {
        RecallRig rig(3840,2160);uint64_t scene=1;ActivePictureBounds newest;
        for(int bottom:{1892,1876,1880}) {
            RecallPixels pixels(3840,2160);RecallScope(pixels,276,bottom);
            const auto raw=ExtractActivePictureEvidence(pixels.Source());newest=raw.trustedBounds;
            // Establish an unrelated base first: near-identical native crops may
            // intentionally retain their prior geometry and need not republish.
            if(scene>1)rig.Establish(rig.wide,scene++,false);
            rig.Establish(pixels,scene++);
            for(int i=0;i<3;++i)rig.FeedNative(pixels,scene);
            ++scene;
            Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries,
                L"Same-family representatives use one eligible cache slot; retired alternatives only veto ambiguity.");
        }
        rig.Establish(rig.wide,scene++,false);rig.Advance(scene);
        auto observed=newest;observed.bottom-=80;
        Assert::IsFalse(rig.model.NominateRememberedEdgeReturn(observed,true,false).available,
            L"A to B to C replacement must retain A's and B's conflicting missing-edge knowledge.");
        // A distinct current edge may identify C exactly despite top-edge ambiguity.
        observed=newest;observed.top+=80;rig.Advance(scene);
        const auto identified=rig.model.NominateRememberedEdgeReturn(observed,false,true);
        Assert::IsTrue(identified.available);EqualRecallBounds(newest,identified.rememberedBounds);
    }

    TEST_METHOD(RememberedFamilyRetiredAmbiguityExpiresAndResetsWithSource)
    {
        for(bool sourceReset:{false,true}) {
            RecallRig rig(3840,2160);RecallPixels a(3840,2160),b(3840,2160);
            RecallScope(a,276,1892);RecallScope(b,276,1876);
            rig.Establish(a,1);for(int i=0;i<3;++i)rig.FeedNative(a,2);
            rig.Establish(rig.wide,3,false);rig.Establish(b,4);for(int i=0;i<3;++i)rig.FeedNative(b,5);
            Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
            ++rig.context.sourceSequence;
            if(sourceReset)++rig.context.sourceGeneration;
            else rig.context.timestampMs+=ActivePictureTransitionModel::REMEMBERED_RETURN_MAX_AGE_MS+1;
            rig.model.SetRememberedEdgeReturnContext(rig.context);
            for(int i=0;i<3;++i)rig.FeedNative(b,6);
            Assert::AreEqual(uint32_t(0),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
            for(int i=0;i<3;++i)rig.FeedNative(b,7);
            Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
            rig.Establish(rig.wide,8,false);rig.Advance(9);
            auto observed=ExtractActivePictureEvidence(b.Source()).trustedBounds;observed.bottom-=80;
            Assert::IsTrue(rig.model.NominateRememberedEdgeReturn(observed,true,false).available,
                L"Expired or previous-source retired witnesses cannot poison newly qualified current-source memory.");
        }
    }

    TEST_METHOD(RememberedFamilyRetiredEdgeKnowledgeSurvivesAnUnrelatedIntermediateRepresentative)
    {
        RecallRig rig(3840,2160);uint64_t scene=1;ActivePictureBounds newest;
        const int tops[]={276,280,276},bottoms[]={1892,1880,1876};
        for(int candidate=0;candidate<3;++candidate) {
            RecallPixels pixels(3840,2160);RecallScope(pixels,tops[candidate],bottoms[candidate]);
            newest=ExtractActivePictureEvidence(pixels.Source()).trustedBounds;
            if(candidate)rig.Establish(rig.wide,scene++,false);
            rig.Establish(pixels,scene++);for(int i=0;i<3;++i)rig.FeedNative(pixels,scene);
            ++scene;
            Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
        }
        rig.Establish(rig.wide,scene++,false);rig.Advance(scene);
        auto observed=newest;observed.bottom-=80;
        Assert::IsFalse(rig.model.NominateRememberedEdgeReturn(observed,true,false).available,
            L"C shares A's top edge even though B shared neither edge; comparing only adjacent replacements loses ambiguity.");
        observed=newest;observed.top+=80;rig.Advance(scene);
        Assert::IsTrue(rig.model.NominateRememberedEdgeReturn(observed,false,true).available);
    }

    TEST_METHOD(RememberedFamilyWitnessOverflowAbstainsUntilLostHistoryExpires)
    {
        for(bool sceneExpiry:{false,true}) {
            RecallRig rig(3840,2160);uint64_t scene=1,oldestTick=0,oldestScene=0;ActivePictureBounds newest;
            // Five independent exact representatives exceed three retired witnesses.
            // None share edges, so only lost-history uncertainty can veto these returns.
            for(int top:{260,264,268,272,276}) {
                RecallPixels pixels(3840,2160);RecallScope(pixels,top,2160-top);
                newest=ExtractActivePictureEvidence(pixels.Source()).trustedBounds;
                if(top!=260)rig.Establish(rig.wide,scene++,false);
                rig.Establish(pixels,scene++);for(int i=0;i<3;++i)rig.FeedNative(pixels,scene);
                if(top==260){oldestTick=rig.context.timestampMs;oldestScene=scene;}
                ++scene;
                Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
            }
            rig.Establish(rig.wide,scene++,false);
            for(bool topEdge:{false,true}) {
                rig.Advance(scene);auto observed=newest;
                if(topEdge)observed.bottom-=80;else observed.top+=80;
                Assert::IsFalse(rig.model.NominateRememberedEdgeReturn(observed,topEdge,!topEdge).available,
                    L"A bounded cache must abstain on either edge while a discarded ambiguity witness is still fresh.");
            }
            if(sceneExpiry)scene=oldestScene+ActivePictureTransitionModel::REMEMBERED_RETURN_MAX_SCENE_DISTANCE+1;
            else rig.context.timestampMs=oldestTick+ActivePictureTransitionModel::REMEMBERED_RETURN_MAX_AGE_MS+1;
            for(bool topEdge:{false,true}) {
                rig.Advance(scene);auto observed=newest;
                if(topEdge)observed.bottom-=80;else observed.top+=80;
                const auto nomination=rig.model.NominateRememberedEdgeReturn(observed,topEdge,!topEdge);
                Assert::IsTrue(nomination.available,L"Lost historical uncertainty expires independently of the fresher exact representative.");
                EqualRecallBounds(newest,nomination.rememberedBounds);
            }
        }
    }

    TEST_METHOD(RememberedFamilyCorrectionCannotTrainFromIncompleteOrExperimentalEvidence)
    {
        for(int fault=0;fault<7;++fault) {
            RecallRig rig(3840,2160);RecallPixels oldPixels(3840,2160),newPixels(3840,2160);
            RecallScope(oldPixels,264,1896);RecallScope(newPixels,276,1884);
            rig.Establish(oldPixels,1);for(int i=0;i<3;++i)rig.FeedNative(oldPixels,2);
            const auto stable=GuardedRecallSnapshot(rig.model).stableBounds;
            const auto newRaw=ExtractActivePictureEvidence(newPixels.Source());
            for(uint64_t scene:{uint64_t(3),uint64_t(4)})for(int i=0;i<3;++i) {
                rig.Advance(scene);auto observation=MakeActivePictureObservation(newRaw,rig.context.sourceSequence,24);
                if(fault==0)observation.transitionDeferred=true;
                if(fault==1)observation.authorityOrigin=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN;
                if(fault==2)observation.rememberedEdgeReturnProof.available=true;
                if(fault==3)observation.sparseTransitionProof.available=true;
                if(fault==4)observation.axisEvidence.verticalCropGuardTop=2;
                if(fault==5)observation.axisEvidence.vertical.scanComplete=false;
                if(fault==6)observation.axisEvidence.horizontal.scanComplete=false;
                const auto message=std::wstring(L"Same-family unsafe evidence fault ")+std::to_wstring(fault);
                Assert::IsFalse(rig.model.RecordIndependentNativeGeometry(observation),message.c_str());
                EqualRecallBounds(stable,GuardedRecallSnapshot(rig.model).stableBounds);
            }
            Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
            rig.Establish(rig.wide,5,false);rig.Advance(6);
            auto observed=stable;observed.bottom-=80;
            const auto old=rig.model.NominateRememberedEdgeReturn(observed,true,false);
            Assert::IsTrue(old.available);EqualRecallBounds(stable,old.rememberedBounds);
            rig.Advance(6);observed=newRaw.trustedBounds;observed.bottom-=80;
            Assert::IsFalse(rig.model.NominateRememberedEdgeReturn(observed,true,false).available);
        }
    }

    TEST_METHOD(RememberedFamilyRequalifyingTheSameTwoRectanglesDoesNotExhaustWitnessCapacity)
    {
        RecallRig rig(3840,2160);uint64_t scene=1;ActivePictureBounds newest;
        // Repeated independent A/B qualification represents only two distinct
        // geometries, not an unlimited number of ambiguity witnesses.
        for(int replacement=0;replacement<8;++replacement) {
            const int top=replacement%2?276:264;
            RecallPixels pixels(3840,2160);RecallScope(pixels,top,2160-top);
            newest=ExtractActivePictureEvidence(pixels.Source()).trustedBounds;
            if(replacement)rig.Establish(rig.wide,scene++,false);
            rig.Establish(pixels,scene++);for(int i=0;i<3;++i)rig.FeedNative(pixels,scene);
            ++scene;
            Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
        }
        rig.Establish(rig.wide,scene++,false);
        for(bool topEdge:{false,true}) {
            rig.Advance(scene);auto observed=newest;
            if(topEdge)observed.bottom-=80;else observed.top+=80;
            const auto nomination=rig.model.NominateRememberedEdgeReturn(observed,topEdge,!topEdge);
            Assert::IsTrue(nomination.available,L"Repeated A/B corrections must deduplicate retired exact bounds rather than create false overflow uncertainty.");
            EqualRecallBounds(newest,nomination.rememberedBounds);
        }
    }

    TEST_METHOD(RememberedFamilyReplacementInvalidatesRetiredHistoryDespiteFreshPixelProof)
    {
        RecallRig rig(3840,2160);rig.QualifiedWide();
        RecallPixels partial(3840,2160),replacement(3840,2160);
        RecallDarkLowerPicture(partial);RecallScope(replacement,280,1880);
        const auto raw=ExtractActivePictureEvidence(partial.Source());AssertRecallRaw(raw);
        rig.Advance(4);auto retired=rig.Nominate(raw);Assert::IsTrue(retired.available);
        rig.Establish(replacement,5,false);
        for(uint64_t scene:{uint64_t(6),uint64_t(7)})for(int i=0;i<3;++i)rig.FeedNative(replacement,scene);
        rig.Establish(rig.wide,8,false);
        for(int vote=0;vote<3;++vote) {
            rig.Advance(9);
            // All frame/scene/pixel proof is fresh: only the retired learned
            // identity is stale. It must not authorize an old queued target.
            retired.sourceSequence=rig.context.sourceSequence;
            retired.timestampMs=rig.context.timestampMs;retired.sceneId=rig.context.sceneId;
            const auto inspected=InspectRememberedEdgeReturn(partial.Source(),raw,retired,
                rig.context.sourceSequence,rig.context.timestampMs);
            Assert::IsTrue(inspected.candidateAvailable);
            const auto admitted=rig.Admit(partial.Source(),inspected.evidence,rig.wideEvidence.trustedBounds);
            Assert::IsTrue(admitted.observation.rememberedEdgeReturnProof.available);
            const auto decision=rig.model.Observe(admitted.observation);
            Assert::IsFalse(decision.publish);
            EqualRecallBounds(rig.wideEvidence.trustedBounds,GuardedRecallSnapshot(rig.model).stableBounds);
        }
        rig.Advance(10);auto observed=ExtractActivePictureEvidence(replacement.Source()).trustedBounds;observed.bottom-=80;
        const auto current=rig.model.NominateRememberedEdgeReturn(observed,true,false);
        Assert::IsTrue(current.available);Assert::IsTrue(current.historyId!=retired.historyId);
    }

    TEST_METHOD(RememberedFamilyRevisitingRepresentativeAtWitnessCapacityKeepsRecall)
    {
        RecallRig rig(3840,2160);uint64_t scene=1;ActivePictureBounds newest;
        // A -> B -> C -> D -> A contains four distinct geometries, which fit
        // one active representative plus three retired ambiguity witnesses.
        for(int top:{264,268,272,276,264}) {
            RecallPixels pixels(3840,2160);RecallScope(pixels,top,2160-top);
            newest=ExtractActivePictureEvidence(pixels.Source()).trustedBounds;
            if(scene>1)rig.Establish(rig.wide,scene++,false);
            rig.Establish(pixels,scene++);for(int i=0;i<3;++i)rig.FeedNative(pixels,scene);
            ++scene;
            Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
        }
        rig.Establish(rig.wide,scene++,false);
        for(bool topEdge:{false,true}) {
            rig.Advance(scene);auto observed=newest;
            if(topEdge)observed.bottom-=80;else observed.top+=80;
            const auto nomination=rig.model.NominateRememberedEdgeReturn(observed,topEdge,!topEdge);
            Assert::IsTrue(nomination.available,
                L"Returning A to active must remove redundant retired A before inserting D, without manufacturing overflow uncertainty.");
            EqualRecallBounds(newest,nomination.rememberedBounds);
        }
    }

    TEST_METHOD(RememberedEligibilityRejectsNonCommonLearningWithoutChangingNativeAcquisition)
    {
        for(bool spoofCachedRatio:{false,true})
        {
            RecallRig rig(3840,2160); RecallPixels pixels(3840,2160);
            RecallScope(pixels,166,1994); // Computed 2.10066, outside every approved family.
            const auto raw=ExtractActivePictureEvidence(pixels.Source());
            Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
            const double computed=double(raw.trustedBounds.right-raw.trustedBounds.left)/
                (raw.trustedBounds.bottom-raw.trustedBounds.top);
            Assert::IsTrue(computed>2.08 && computed<2.12);
            bool published=false;
            for(int i=0;i<ActivePictureTransitionModel::INITIAL_CONFIRMATIONS+3;++i) {
                rig.Advance(1); auto observation=MakeActivePictureObservation(raw,rig.context.sourceSequence,24);
                if(spoofCachedRatio) observation.bounds.aspectRatio=2.39;
                published=rig.model.Observe(observation).publish || published;
            }
            Assert::IsTrue(published,L"The primary native detector must still acquire non-common aspect ratios.");
            const auto stable=GuardedRecallSnapshot(rig.model).stableBounds;
            for(uint64_t scene:{uint64_t(2),uint64_t(3)}) for(int i=0;i<3;++i) {
                rig.Advance(scene); auto observation=MakeActivePictureObservation(raw,rig.context.sourceSequence,24);
                if(spoofCachedRatio) observation.bounds.aspectRatio=2.39;
                rig.model.Observe(observation);
                Assert::IsFalse(rig.model.RecordIndependentNativeGeometry(observation),
                    L"Known-format eligibility is only a secondary-learning gate and must use rectangle dimensions.");
                EqualRecallBounds(stable,GuardedRecallSnapshot(rig.model).stableBounds);
            }
            Assert::AreEqual(uint32_t(0),rig.model.GetRememberedEdgeReturnHistoryStatus().entries);
        }
    }

    TEST_METHOD(RememberedEligibilityAcceptsCommonFamiliesWithinOnePercentWithoutSnapping)
    {
        for(double family:{1.85,1.90,2.00,2.20,2.35,2.39,2.40,2.55,2.76})
        for(double offset:{-0.007,0.0,0.007})
        {
            RecallRig rig(3840,2160); RecallPixels pixels(3840,2160);
            const int top=int(std::lround((2160.0-3840.0/(family*(1.0+offset)))/8.0))*4;
            RecallScope(pixels,top,2160-top);
            const auto raw=ExtractActivePictureEvidence(pixels.Source());
            Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
            const double computed=double(raw.trustedBounds.right-raw.trustedBounds.left)/
                (raw.trustedBounds.bottom-raw.trustedBounds.top);
            Assert::IsTrue(std::abs(computed/family-1.0)<=0.01);
            rig.Establish(pixels,1,false);
            for(uint64_t scene:{uint64_t(2),uint64_t(3)}) for(int i=0;i<3;++i) {
                rig.Advance(scene); const auto observation=MakeActivePictureObservation(raw,rig.context.sourceSequence,24);
                rig.model.Observe(observation);
                Assert::IsTrue(rig.model.RecordIndependentNativeGeometry(observation));
            }
            EqualRecallBounds(raw.trustedBounds,GuardedRecallSnapshot(rig.model).stableBounds);
            const auto history=rig.model.GetRememberedEdgeReturnHistoryStatus();
            Assert::AreEqual(uint32_t(1),history.qualifiedEntries);
            Assert::AreEqual(uint32_t(2),history.maxConfirmedScenes);
        }
        // Deliberately malformed cached AR is not the source of eligibility.
        RecallRig rig(3840,2160); RecallPixels pixels(3840,2160); RecallScope(pixels,328,1832);
        const auto raw=ExtractActivePictureEvidence(pixels.Source());
        bool published=false;
        for(int i=0;i<ActivePictureTransitionModel::INITIAL_CONFIRMATIONS+3;++i) {
            rig.Advance(1); auto observation=MakeActivePictureObservation(raw,rig.context.sourceSequence,24);
            observation.bounds.aspectRatio=2.10;
            published=rig.model.Observe(observation).publish || published;
        }
        Assert::IsTrue(published);
        for(uint64_t scene:{uint64_t(2),uint64_t(3)}) for(int i=0;i<3;++i) {
            rig.Advance(scene); auto observation=MakeActivePictureObservation(raw,rig.context.sourceSequence,24);
            observation.bounds.aspectRatio=2.10; rig.model.Observe(observation);
            Assert::IsTrue(rig.model.RecordIndependentNativeGeometry(observation));
        }
        Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
    }

    TEST_METHOD(RememberedEligibilityUsesOnePercentComputedBoundaryWithoutDetectorRounding)
    {
        // Synthetic trusted observations isolate the admission threshold from
        // the native scanner's four-pixel sampling step. All geometry is centered.
        for(int top:{390,392,378,376}) {
            RecallRig rig(3840,2160);
            const bool eligible=top==390 || top==378;
            auto observation=MakeActivePictureObservation(rig.scopeEvidence,1,24);
            observation.bounds.top=top; observation.bounds.bottom=2160-top;
            observation.bounds.aspectRatio=3840.0/(2160-2*top);
            const double relativeDifference=std::abs(observation.bounds.aspectRatio/2.76-1.0);
            Assert::AreEqual(eligible,relativeDifference<=0.01);
            bool published=false;
            for(int i=0;i<ActivePictureTransitionModel::INITIAL_CONFIRMATIONS+3;++i) {
                rig.Advance(1); observation.frameNumber=rig.context.sourceSequence;
                published=rig.model.Observe(observation).publish || published;
            }
            Assert::IsTrue(published);
            for(uint64_t scene:{uint64_t(2),uint64_t(3)}) for(int i=0;i<3;++i) {
                rig.Advance(scene); observation.frameNumber=rig.context.sourceSequence;
                rig.model.Observe(observation);
                Assert::AreEqual(eligible,rig.model.RecordIndependentNativeGeometry(observation));
                EqualRecallBounds(observation.bounds,GuardedRecallSnapshot(rig.model).stableBounds);
            }
            Assert::AreEqual(eligible?uint32_t(1):uint32_t(0),
                rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
        }
    }
    TEST_METHOD(RememberedEligibilityFourthCandidateCannotEvictQualifiedHistoryUntilQualified)
    {
        RecallRig rig(3840,2160); std::vector<ActivePictureBounds> profiles;
        uint64_t scene=1;
        for(int top:{208,276,328}) {
            RecallPixels pixels(3840,2160); RecallScope(pixels,top,2160-top);
            const auto raw=ExtractActivePictureEvidence(pixels.Source());
            profiles.push_back(raw.trustedBounds);
            rig.Establish(pixels,scene++);
            for(int i=0;i<3;++i) rig.FeedNative(pixels,scene);
            ++scene;
        }
        Assert::AreEqual(uint32_t(3),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
        RecallPixels fourth(3840,2160); RecallScope(fourth,384,1776);
        const auto fourthRaw=ExtractActivePictureEvidence(fourth.Source());
        rig.Establish(fourth,scene++);
        Assert::AreEqual(uint32_t(3),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries,
            L"A newly observed but unqualified fourth geometry must not evict a qualified entry.");
        rig.Establish(rig.wide,scene++,false);
        for(const auto& target:profiles) {
            rig.Advance(scene); auto observed=target; observed.bottom-=80;
            const auto nomination=rig.model.NominateRememberedEdgeReturn(observed,true,false);
            Assert::IsTrue(nomination.available);
            EqualRecallBounds(target,nomination.rememberedBounds);
        }
        ++scene;
        rig.Establish(fourth,scene++,false);
        for(int i=0;i<3;++i)rig.FeedNative(fourth,scene);
        ++scene;
        Assert::AreEqual(uint32_t(3),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
        rig.Establish(rig.wide,scene++,false);
        for(size_t i=0;i<profiles.size();++i) {
            rig.Advance(scene); auto observed=profiles[i]; observed.bottom-=80;
            const auto nomination=rig.model.NominateRememberedEdgeReturn(observed,true,false);
            Assert::AreEqual(i!=0,nomination.available,
                L"Only a fully qualified newcomer may replace the oldest qualified exact geometry.");
        }
        rig.Advance(scene); auto observed=fourthRaw.trustedBounds; observed.bottom-=80;
        const auto nomination=rig.model.NominateRememberedEdgeReturn(observed,true,false);
        Assert::IsTrue(nomination.available); EqualRecallBounds(fourthRaw.trustedBounds,nomination.rememberedBounds);
    }

    TEST_METHOD(RememberedEligibilityAlternatingOverflowCandidatesCannotPoolOrEvict)
    {
        RecallRig rig(3840,2160); uint64_t scene=1; std::vector<ActivePictureBounds> qualified;
        for(int top:{208,276,328}) {
            RecallPixels pixels(3840,2160); RecallScope(pixels,top,2160-top);
            qualified.push_back(ExtractActivePictureEvidence(pixels.Source()).trustedBounds);
            rig.Establish(pixels,scene++);
            for(int i=0;i<3;++i)rig.FeedNative(pixels,scene);
            ++scene;
        }
        RecallPixels a(3840,2160),b(3840,2160); RecallScope(a,384,1776); RecallScope(b,388,1772);
        const auto first=ExtractActivePictureEvidence(a.Source()),second=ExtractActivePictureEvidence(b.Source());
        rig.Establish(a,scene,false);
        for(int i=0;i<24;++i) {
            rig.Advance(scene+(i>=12?1:0));
            const auto observation=MakeActivePictureObservation(i%2?second:first,rig.context.sourceSequence,24);
            rig.model.Observe(observation);
            Assert::IsTrue(rig.model.RecordIndependentNativeGeometry(observation));
            const auto history=rig.model.GetRememberedEdgeReturnHistoryStatus();
            Assert::AreEqual(uint32_t(3),history.qualifiedEntries);
            Assert::AreEqual(uint32_t(3),history.entries,
                L"Overflow probation does not consume or expose a fourth qualified-cache slot.");
        }
        scene+=2; rig.Establish(rig.wide,scene++,false);
        for(const auto& target:qualified) {
            rig.Advance(scene); auto observed=target; observed.bottom-=80;
            Assert::IsTrue(rig.model.NominateRememberedEdgeReturn(observed,true,false).available);
        }
        for(const auto& raw:{first,second}) {
            rig.Advance(scene); auto observed=raw.trustedBounds; observed.bottom-=80;
            Assert::IsFalse(rig.model.NominateRememberedEdgeReturn(observed,true,false).available);
        }
    }

    TEST_METHOD(RememberedEligibilityDoesNotChangeFourByThreeNativePresentation)
    {
        for(bool pillarboxed:{false,true}) {
            const int width=pillarboxed?960:720;
            RecallRig rig(width,540); RecallPixels pixels(width,540);
            pixels.Rectangle(pillarboxed?120:0,0,pillarboxed?840:720,540,300);
            const auto raw=ExtractActivePictureEvidence(pixels.Source());
            Assert::IsTrue(raw.available);
            Assert::IsTrue(raw.classification==(pillarboxed?ActivePictureClassification::BAR_CROP_TRUSTED:
                ActivePictureClassification::FULL_RASTER_TRUSTED));
            bool published=false;
            for(uint64_t scene:{uint64_t(1),uint64_t(2)})
            for(int i=0;i<ActivePictureTransitionModel::INITIAL_CONFIRMATIONS+3;++i) {
                rig.Advance(scene); const auto observation=MakeActivePictureObservation(raw,rig.context.sourceSequence,24);
                published=rig.model.Observe(observation).publish || published;
                Assert::IsFalse(rig.model.RecordIndependentNativeGeometry(observation));
            }
            Assert::IsTrue(published);
            EqualRecallBounds(raw.trustedBounds,GuardedRecallSnapshot(rig.model).stableBounds);
            Assert::AreEqual(pillarboxed?120:0,GuardedRecallSnapshot(rig.model).stableBounds.left);
            Assert::AreEqual(pillarboxed?840:720,GuardedRecallSnapshot(rig.model).stableBounds.right);
            Assert::AreEqual(uint32_t(0),rig.model.GetRememberedEdgeReturnHistoryStatus().entries);
        }
    }
    TEST_METHOD(RememberedEligibilityOverflowCandidateMustRequalifyAfterExpiryOrSourceReset)
    {
        for(bool sourceReset:{false,true}) {
            RecallRig rig(3840,2160); uint64_t scene=1;
            for(int top:{208,276,328}) {
                RecallPixels pixels(3840,2160); RecallScope(pixels,top,2160-top);
                rig.Establish(pixels,scene++);
                for(int i=0;i<3;++i)rig.FeedNative(pixels,scene);
                ++scene;
            }
            RecallPixels fourth(3840,2160); RecallScope(fourth,384,1776);
            rig.Establish(fourth,scene++); // Overflow candidate has only one credited scene.
            ++rig.context.sourceSequence;
            if(sourceReset)++rig.context.sourceGeneration;
            else rig.context.timestampMs+=ActivePictureTransitionModel::REMEMBERED_RETURN_MAX_AGE_MS+1;
            rig.model.SetRememberedEdgeReturnContext(rig.context);
            for(int i=0;i<3;++i)rig.FeedNative(fourth,scene);
            Assert::AreEqual(uint32_t(0),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries,
                L"A stale overflow candidate cannot carry its prior-scene credit into a new lifetime.");
            ++scene;
            for(int i=0;i<3;++i)rig.FeedNative(fourth,scene);
            Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
        }
    }
    TEST_METHOD(RememberedEligibilityProtectionCannotReviveExpiredOrPreviousSourceHistory)
    {
        for(bool sourceReset:{false,true}) {
            RecallRig rig; rig.Learn();
            Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
            ++rig.context.sourceSequence;
            if(sourceReset)++rig.context.sourceGeneration;
            else rig.context.timestampMs+=ActivePictureTransitionModel::REMEMBERED_RETURN_MAX_AGE_MS+1;
            rig.model.SetRememberedEdgeReturnContext(rig.context);
            Assert::AreEqual(uint32_t(0),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
            for(int i=0;i<3;++i)rig.FeedNative(rig.scope,3);
            Assert::AreEqual(uint32_t(0),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries,
                L"An expired or previous-source profile needs two newly verified scenes.");
            for(int i=0;i<3;++i)rig.FeedNative(rig.scope,4);
            Assert::AreEqual(uint32_t(1),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
        }
    }
    TEST_METHOD(RememberedNativeLearningRetainsPresentationButLearnsExactFourKMeasurement)
    {
        RecallRig rig(3840,2160);
        RecallPixels retained(3840,2160); RecallScope(retained,272,1880);
        rig.Establish(retained,1,false);
        const auto stable=GuardedRecallSnapshot(rig.model).stableBounds;
        Assert::AreEqual(272,stable.top); Assert::AreEqual(1880,stable.bottom);
        Assert::AreEqual(276,rig.scopeEvidence.trustedBounds.top);
        Assert::AreEqual(1884,rig.scopeEvidence.trustedBounds.bottom);
        for(uint64_t scene:{uint64_t(2),uint64_t(3)})
            for(int i=0;i<3;++i)
            {
                rig.Advance(scene);
                const auto raw=ExtractActivePictureEvidence(rig.scope.Source());
                Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED &&
                    raw.authorityOrigin==ActivePictureAuthorityOrigin::NATIVE);
                Assert::IsFalse(raw.axisEvidence.HasVerticalCropBoundaryGuard());
                const auto observation=MakeActivePictureObservation(raw,rig.context.sourceSequence,24);
                rig.model.Observe(observation);
                EqualRecallBounds(stable,GuardedRecallSnapshot(rig.model).stableBounds);
                Assert::IsTrue(rig.model.RecordIndependentNativeGeometry(observation),
                    L"A retained four-pixel presentation offset must not discard independently trusted exact native measurements.");
            }
        const auto history=rig.model.GetRememberedEdgeReturnHistoryStatus();
        Assert::AreEqual(uint32_t(1),history.entries); Assert::AreEqual(uint32_t(1),history.qualifiedEntries);
        Assert::AreEqual(uint32_t(2),history.maxConfirmedScenes);
        rig.Establish(rig.wide,4,false);
        RecallPixels partial(3840,2160); RecallDarkLowerPicture(partial);
        rig.Advance(5); const auto raw=ExtractActivePictureEvidence(partial.Source()); AssertRecallRaw(raw);
        const auto nominee=rig.Nominate(raw); Assert::IsTrue(nominee.available);
        EqualRecallBounds(rig.scopeEvidence.trustedBounds,nominee.rememberedBounds);
        Assert::AreEqual(276,nominee.rememberedBounds.top); Assert::AreEqual(1884,nominee.rememberedBounds.bottom);
    }

    TEST_METHOD(RememberedNativeLearningEquivalenceDoesNotAdmitMaterialOrInferredGeometry)
    {
        for(int fault=0;fault<9;++fault)
        {
            RecallRig rig(3840,2160); rig.Establish(rig.scope,1,false); rig.Advance(2);
            auto raw=MakeActivePictureObservation(rig.scopeEvidence,rig.context.sourceSequence,24);
            raw.bounds.top+=8; raw.bounds.bottom+=8;
            if(fault==1){++raw.bounds.top;++raw.bounds.bottom;}
            if(fault==2)raw.bounds.aspectRatio+=0.026;
            if(fault==3)raw.bounds.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH;
            if(fault==4)raw.axisEvidence.verticalCropGuardTop=2;
            if(fault==5)raw.authorityOrigin=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN;
            if(fault==6)raw.rememberedEdgeReturnProof.available=true;
            if(fault==7)raw.axisEvidence.vertical.scanComplete=false;
            if(fault==8)raw.axisEvidence.horizontal.scanComplete=false;
            const auto message=std::wstring(L"Independent native learning equivalence case ")+std::to_wstring(fault);
            if(fault==0)Assert::IsTrue(rig.model.RecordIndependentNativeGeometry(raw),message.c_str());
            else Assert::IsFalse(rig.model.RecordIndependentNativeGeometry(raw),message.c_str());
            Assert::AreEqual(uint32_t(0),rig.model.GetRememberedEdgeReturnHistoryStatus().qualifiedEntries);
            EqualRecallBounds(rig.scopeEvidence.trustedBounds,GuardedRecallSnapshot(rig.model).stableBounds);
        }
    }

    TEST_METHOD(RememberedNativeLearningNeverPoolsNearbyExactMeasurementVariants)
    {
        RecallRig rig(3840,2160);
        RecallPixels retained(3840,2160); RecallScope(retained,272,1880);
        rig.Establish(retained,1,false);
        const auto stable=GuardedRecallSnapshot(rig.model).stableBounds;
        for(uint64_t scene:{uint64_t(2),uint64_t(3)})
            for(int i=0;i<6;++i)
            {
                rig.Advance(scene);
                auto raw=MakeActivePictureObservation(rig.scopeEvidence,rig.context.sourceSequence,24);
                if(i%2){raw.bounds.top+=4;raw.bounds.bottom+=4;}
                rig.model.Observe(raw);
                Assert::IsTrue(rig.model.RecordIndependentNativeGeometry(raw));
                EqualRecallBounds(stable,GuardedRecallSnapshot(rig.model).stableBounds);
            }
        const auto history=rig.model.GetRememberedEdgeReturnHistoryStatus();
        Assert::AreEqual(uint32_t(1),history.entries,
            L"One provisional family slot must still keep exact candidates independent, without pooling evidence.");
        Assert::AreEqual(uint32_t(0),history.qualifiedEntries);
        Assert::AreEqual(uint32_t(0),history.maxConfirmedScenes,
            L"Alternating nearby edges cannot pool into three consecutive samples of either exact geometry.");
        rig.Establish(rig.wide,4,false); rig.Advance(5);
        RecallPixels partial(3840,2160); RecallDarkLowerPicture(partial);
        Assert::IsFalse(rig.Nominate(ExtractActivePictureEvidence(partial.Source())).available);
    }

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
TEST_CLASS(RememberedRuntimeModeTests)
{
public:
    TEST_METHOD(RememberedRuntimeModeNormalLaunchUsesGuardedFallback)
    {
        Assert::IsTrue(ResolveRememberedEdgeReturnMode(nullptr) == RememberedEdgeReturnMode::GUARDED);
        Assert::IsTrue(ResolveRememberedEdgeReturnMode("") == RememberedEdgeReturnMode::GUARDED);
    }
    TEST_METHOD(RememberedRuntimeModeExplicitDisableAndInvalidStayOff)
    {
        for (const char* option : {"off", "0", "false", "invalid", "guarded-typo"})
            Assert::IsTrue(ResolveRememberedEdgeReturnMode(option) == RememberedEdgeReturnMode::OFF);
    }
    TEST_METHOD(RememberedRuntimeModePreservesExplicitDiagnosticModes)
    {
        Assert::IsTrue(ResolveRememberedEdgeReturnMode("guarded") == RememberedEdgeReturnMode::GUARDED);
        Assert::IsTrue(ResolveRememberedEdgeReturnMode("shadow") == RememberedEdgeReturnMode::SHADOW);
        Assert::IsTrue(ResolveRememberedEdgeReturnMode("experimental") == RememberedEdgeReturnMode::EXPERIMENTAL);
    }
};

}
