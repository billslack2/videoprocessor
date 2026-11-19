// Native P010 integration checks for optional current-source continuation proof.
#include "../src/VideoProcessor-Lib/vprenderer/ExperimentalAspectActive.h"
#include "../src/VideoProcessor-Lib/vprenderer/PreparedPictureEvidence.h"
#include "../src/VideoProcessor-Lib/vprenderer/AlphaSourceCropPolicy.h"
#include <cstdio>
#include <vector>

namespace {
int failures = 0;
void Check(bool condition, const char* description)
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++failures;
}
struct Fixture {
    std::shared_ptr<std::vector<uint16_t>> pixels;
    PreparedAnalysisFrame frame;
    ActivePictureFrameIdentity identity;
    ActivePictureEvidence measured;
    ActivePictureArDarknessEvidence darkness;
    ActivePictureBounds base{0,276,3840,1884,3840,2160,3840.0/1608,
        ActivePictureBounds::BarAxes::TOP_BOTTOM};
    explicit Fixture(int scene)
    {
        pixels=std::make_shared<std::vector<uint16_t>>(3840*2160*3/2,512<<6);
        for(int y=0;y<2160;++y)for(int x=0;x<3840;++x){
            const int d=(std::min)(y,2159-y);
            int value=d<68?64:(d<276?84:(std::min)(300,84+d-276));
            if(scene==1){value=d<68?64:300;if(x<960&&d>=100&&d<276)value=70;}
            if(scene==2)value=d<68?64:(d<276?84:300);
            (*pixels)[y*3840+x]=static_cast<uint16_t>(value<<6);
        }
        auto& source=frame.source;
        source.data=reinterpret_cast<const uint8_t*>(pixels->data());source.dataBytes=pixels->size()*2;
        source.width=3840;source.height=2160;source.rowBytes=source.chromaRowBytes=7680;
        source.range=AnalysisLumaRange::Limited;source.backingOwner=pixels;source.generation=10;
        frame.sourceOwner=pixels;frame.domain=AnalysisFrameDomain::ConvertedP010;
        frame.profile=AnalysisSourceProfile::QueuedCapture;frame.domainKey=42;
        identity.transportGeneration=7;identity.sourceFormatGeneration=10;
        identity.acceptedSequence=123;identity.captureTimestamp=456;identity.analysisDomainKey=42;
        identity.viewportGeneration=3;identity.rendererGeneration=2;
        const auto result=ExperimentalAspect::Measure(source.width,source.height,64,
            [&](int x,int y,int& value){AnalysisLumaSample p;if(!source.Sample(x,y,p))return false;
                value=p.luma;return true;},[]{return true;});
        measured=ExperimentalAspect::EvidenceFromMeasurement(source,result);
        darkness=EvaluateActivePictureArDarkness(source,measured,EvaluateActivePictureGlobalNearBlack(source));
    }
    PreparedPictureEvidenceKey Key() const
    {
        return MakePreparedPictureEvidenceKey(PreparedPictureEvidenceKind::Retention,
            frame,identity,base,47,23,pixels);
    }
};
bool NeverExpires(void*) { return false; }
struct Countdown { int checks=0, limit=0; };
bool ExpiresAfter(void* context)
{
    auto& state=*static_cast<Countdown*>(context);
    return ++state.checks>state.limit;
}
}

int main()
{
    for(int scene=0;scene<3;++scene){
        Fixture f(scene);
        Check(f.measured.strongCurrentBarMeasurement,"fixture has independently measured strong opposing edges");
        const auto ordinary=EvaluateActivePicturePresentationRetention(f.frame.source,f.base,
            f.measured,f.darkness.global,f.darkness);
        Check(!ordinary.relativeContinuation.complete && ordinary.relativeContinuation.extraSamples==0,
            "mandatory null-budget path does not collect relative proof");
        AnalysisEvaluationBudget allowed(NeverExpires,nullptr);
        const auto withProof=EvaluateActivePicturePresentationRetention(f.frame.source,f.base,
            f.measured,f.darkness.global,f.darkness,nullptr,&allowed);
        const auto& proof=withProof.relativeContinuation;
        Check(proof.complete && proof.extraSamples==960,"optional proof reuses strip grid and adds exactly 960 boundary reads");
        const bool preferred=proof.newScore>0.5&&proof.newScore>proof.oldScore;
        Check(preferred==(scene!=2),scene==2?"nested raised matte remains ambiguous":
            "dim or spatially dark picture outranks old boundary");
        Check(SamePreparedPictureBounds(proof.base,f.base)&&
            SamePreparedPictureBounds(proof.candidate,f.measured.trustedBounds),"relative scores retain exact base and measured target");
        Check(proof.identity.acceptedSequence==0,"raw feature cannot invent a prepared source identity");
        Check(withProof.excludedBandsPixelSafe==ordinary.excludedBandsPixelSafe &&
            withProof.outwardVisibleBoundsAvailable==ordinary.outwardVisibleBoundsAvailable &&
            withProof.excludedTop.blackFraction==ordinary.excludedTop.blackFraction &&
            withProof.excludedBottom.blackFraction==ordinary.excludedBottom.blackFraction,
            "relative features preserve ordinary excluded-band and visible-pixel evidence");
        Check(!withProof.activePicture.blackReference.evaluated &&
            !withProof.activePicture.axisEvidence.verticalCropGlyphExclusions,
            "relative preference supplies neither matte calibration nor subtitle ownership");
        AnalysisEvaluationBudget preparedBudget(NeverExpires,nullptr);
        const auto key=f.Key();
        Check(ValidPreparedPictureEvidenceKey(key),"prepared fixture is an immutable exact-source request");
        const auto prepared=EvaluatePreparedPictureEvidence(key,f.measured,f.darkness,nullptr,&preparedBudget);
        Check(SameAnalysisFrameIdentity(prepared.retention.relativeContinuation.identity,f.identity)&&
            prepared.retention.relativeContinuation.policyGeneration==47&&
            prepared.retention.relativeContinuation.continuityGeneration==23,
            "prepared wrapper binds relative proof to complete source and policy context");
        AlphaSourceCrop::TransitionAdmissionInput admission;
        admission.evidence=f.measured;admission.outwardCandidate=f.measured.trustedBounds;
        admission.presentationBeforeObservation=admission.trustedGeometry=f.base;
        admission.trustedGeometryAvailable=admission.compatiblePresentation=true;
        admission.sourceGeneration=admission.trustedGeneration=f.identity.transportGeneration;
        admission.sourceSequence=f.identity.acceptedSequence;admission.sourceIdentity=f.identity;
        admission.policyGeneration=47;admission.continuityGeneration=23;admission.framesPerSecond=24;
        admission.retention=prepared.retention;admission.allowCurrentSpatialExpansion=true;
        admission.presentation.action=AlphaSourceCrop::VerticalBarPresentationAction::TRANSLATE;
        admission.presentationEvidenceGeneration=f.identity.transportGeneration;admission.translationDriftActive=true;
        const auto accepted=AlphaSourceCrop::EvaluateTransitionAdmission(admission);
        Check(accepted.currentSpatialRelative==(scene!=2)&&accepted.currentSpatialExpansion==(scene!=2),
            "production admission uses relative preference only for unambiguous current expansion");
        auto ordinaryAdmission=admission;ordinaryAdmission.retention.relativeContinuation={};
        Check(!AlphaSourceCrop::EvaluateTransitionAdmission(ordinaryAdmission).currentSpatialExpansion,
            "fixtures exercise the relative path rather than existing strict strip acceptance");
        if(scene==0){
            for(int fault=0;fault<10;++fault){
                auto bad=admission;
                auto& relative=bad.retention.relativeContinuation;
                if(fault==0)++relative.identity.acceptedSequence;
                if(fault==1)++relative.identity.transportGeneration;
                if(fault==2)++relative.identity.viewportGeneration;
                if(fault==3)++relative.identity.analysisDomainKey;
                if(fault==4)++relative.policyGeneration;
                if(fault==5)++relative.continuityGeneration;
                if(fault==6)relative.base.top+=2;
                if(fault==7)relative.candidate.bottom-=2;
                if(fault==8)relative.complete=false;
                if(fault==9)bad.evidence.strongCurrentBarMeasurement=false;
                const auto rejected=AlphaSourceCrop::EvaluateTransitionAdmission(bad);
                Check(!rejected.currentSpatialRelative&&!rejected.currentSpatialExpansion,
                    "stale source/context, mismatched geometry or incomplete proof cannot release admission");
            }
            Countdown state;state.limit=5;
            AnalysisEvaluationBudget expired(ExpiresAfter,&state);
            const auto cancelled=EvaluateActivePicturePresentationRetention(f.frame.source,f.base,
                f.measured,f.darkness.global,f.darkness,nullptr,&expired);
            Check(expired.Cancelled()&&!cancelled.relativeContinuation.complete&&
                cancelled.relativeContinuation.extraSamples>0&&cancelled.relativeContinuation.extraSamples<960,
                "actual feature cancellation after partial reads discards relative proof");
            Check(cancelled.analysisValid==ordinary.analysisValid&&cancelled.presentationValid==ordinary.presentationValid&&
                cancelled.excludedBandsPixelSafe==ordinary.excludedBandsPixelSafe&&
                cancelled.outwardVisibleBoundsAvailable==ordinary.outwardVisibleBoundsAvailable,
                "optional feature expiry retains completed ordinary retention");
            auto atLimit=f.measured;atLimit.lumaSamples=24000;
            AnalysisEvaluationBudget limit(NeverExpires,nullptr);
            const auto capped=EvaluateActivePicturePresentationRetention(f.frame.source,f.base,
                atLimit,f.darkness.global,f.darkness,nullptr,&limit);
            Check(!capped.relativeContinuation.complete&&capped.relativeContinuation.extraSamples==0,
                "exhausted shared sample allowance cannot start old-boundary sampling");
            auto weak=f.measured;weak.strongCurrentBarMeasurement=false;
            AnalysisEvaluationBudget weakBudget(NeverExpires,nullptr);
            const auto unsupported=EvaluateActivePicturePresentationRetention(f.frame.source,f.base,
                weak,f.darkness.global,f.darkness,nullptr,&weakBudget);
            Check(!unsupported.relativeContinuation.complete&&unsupported.relativeContinuation.extraSamples==0,
                "weak opposing boundaries cannot request a relative admission proof");
        }
    }
    return failures ? 1 : 0;
}
