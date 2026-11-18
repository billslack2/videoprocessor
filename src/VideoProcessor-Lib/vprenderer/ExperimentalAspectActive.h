#pragma once
#include "../ExperimentalAspectDetector.h"
#include <ActivePictureEvidence.h>
#include <DebugLog.h>
#include <Windows.h>
#include <chrono>
#include <cstring>

namespace ExperimentalAspect {
// Worker-local, bounded nomination history; never a pixel/caption/crop certificate.
class MeasuredHistory {
    Prior prior;
    ActivePictureFrameIdentity identity{};
    uint64_t measuredSequence=0,measuredMs=0;
public:
    Prior Hint(const ActivePictureFrameIdentity& current,int width,int height,uint64_t nowMs){
        const bool same=identity.transportGeneration==current.transportGeneration &&
            identity.sourceFormatGeneration==current.sourceFormatGeneration &&
            identity.analysisDomainKey==current.analysisDomainKey &&
            identity.viewportGeneration==current.viewportGeneration &&
            identity.rendererGeneration==current.rendererGeneration;
        if(!same||current.acceptedSequence<=identity.acceptedSequence||
            current.acceptedSequence-measuredSequence>120||nowMs<measuredMs||nowMs-measuredMs>2000||
            width!=prior.width||height!=prior.height)prior={};
        identity=current;return prior;
    }
    void Observe(const Result& result,int width,int height,uint64_t nowMs){
        // Partial/obscured observations may use a hint but cannot refresh it.
        if(!result.complete||result.kind!=Kind::Bars)return;
        const bool horizontal=result.left>0||result.right<width;
        const bool vertical=result.top>0||result.bottom<height;
        if((horizontal&&(result.edges[0].support<7.0/9||result.edges[2].support<7.0/9))||
           (vertical&&(result.edges[1].support<7.0/9||result.edges[3].support<7.0/9)))return;
        prior={width,height,result.left,result.top,result.right,result.bottom,true};
        measuredSequence=identity.acceptedSequence;measuredMs=nowMs;
    }
};
inline bool ActiveTrialEnabled(){
    static const bool enabled=[] {char mode[16]{};return GetEnvironmentVariableA("VP_AR_EXPERIMENT",mode,sizeof(mode))==6&&std::strcmp(mode,"active")==0;}();
    return enabled;
}
// No legacy certificates are copied to new geometry. Uncropped axes remain
// explicitly unknown unless current perimeter samples independently support them.
inline ActivePictureEvidence EvidenceFromMeasurement(const AnalysisLumaSource& source,const Result& measured){
    ActivePictureEvidence out;
    if(!measured.complete||measured.kind!=Kind::Bars)return out;
    const int w=source.width,h=source.height;
    if(measured.left<0||measured.top<0||measured.right>w||measured.bottom>h||
       measured.right-measured.left<w/3||measured.bottom-measured.top<h/3)return out;
    const bool horizontal=measured.left>0||measured.right<w,vertical=measured.top>0||measured.bottom<h;
    const auto axes=static_cast<ActivePictureBounds::BarAxes>((vertical?1:0)|(horizontal?2:0));
    out.available=true;out.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
    out.proposedBounds={measured.left,measured.top,measured.right,measured.bottom,w,h,
        double(measured.right-measured.left)/(measured.bottom-measured.top),axes};
    out.trustedBounds=out.proposedBounds;
    // Retain a whole chroma pair at odd native boundaries; never round inward.
    auto& b=out.trustedBounds;b.left&=~1;b.top&=~1;b.right=(std::min)(w,(b.right+1)&~1);b.bottom=(std::min)(h,(b.bottom+1)&~1);
    b.aspectRatio=double(b.right-b.left)/(b.bottom-b.top);
    out.lumaSamples=measured.samples;out.reason="experimental joint current-pixel boundary measurement";
    ActivePictureEdgeEvidence* edges[]={&out.left,&out.top,&out.right,&out.bottom};
    for(int side=0;side<4;++side){auto& e=*edges[side];const auto& m=measured.edges[side];
        const bool cropped=(side==0||side==2)?horizontal:vertical;
        if(cropped){e.barPixels=m.depth;e.continuity=m.support;e.innerBoundaryContrast=m.contrast;
            e.lumaDispersion=m.noise;e.confidence=m.score;e.trusted=true;}
    }
    auto axis=[&](bool cropped,bool alongX){
        ActivePictureAxisEvidence e;e.scanComplete=true;e.barCandidate=cropped;
        if(cropped){e.state=ActivePictureAxisState::TRUSTED_BARS;e.reason=ActivePictureAxisReason::BAR_CONFIRMED;return e;}
        e.reason=ActivePictureAxisReason::NO_FULL_EXTENT_SUPPORT;
        int supported[2]{};
        for(int side=0;side<2;++side)for(int lane=0;lane<9;++lane){
            const int across=alongX?b.top+(lane+1)*(b.bottom-b.top)/10:b.left+(lane+1)*(b.right-b.left)/10;
            AnalysisLumaSample outer,inner;
            const int extent=alongX?w:h,d=side?extent-1:0,inside=side?extent-4:3;
            if(source.Sample(alongX?d:across,alongX?across:d,outer)&&source.Sample(alongX?inside:across,alongX?across:inside,inner)){
                out.lumaSamples+=2;const int black=source.range==AnalysisLumaRange::Full?0:64;
                if(outer.luma>black+48&&inner.luma>black+48)++supported[side];
            }
        }
        if(supported[0]>=7&&supported[1]>=7){e.state=ActivePictureAxisState::FULL_EXTENT_SUPPORTED;e.reason=ActivePictureAxisReason::FULL_EXTENT_SUPPORTED;}
        return e;
    };
    out.axisEvidence.horizontal=axis(horizontal,true);out.axisEvidence.vertical=axis(vertical,false);
    return out;
}
// Called only by the existing bounded lookahead measurement worker. The renderer
// imports its result only when immutable source identity/domain match exactly.
inline ActivePictureEvidence MeasureActiveTrial(const AnalysisLumaSource& source,const ActivePictureFrameIdentity& identity){
    if(!ActiveTrialEnabled())return ExtractActivePictureEvidence(source);
    using Clock=std::chrono::steady_clock;
    static thread_local MeasuredHistory history;
    const auto start=Clock::now();
    const auto nowMs=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(start.time_since_epoch()).count());
    const auto prior=history.Hint(identity,source.width,source.height,nowMs);
    Result result;result.reason="unknown-code-range";
    if(source.IsValid()&&source.range!=AnalysisLumaRange::Unknown){
        result=Measure(source.width,source.height,source.range==AnalysisLumaRange::Full?0:64,
            [&](int x,int y,int& v){AnalysisLumaSample p;if(!source.Sample(x,y,p))return false;v=p.luma;return true;},
            [&]{return Clock::now()-start<std::chrono::milliseconds(2);},24000,&prior);
    }
    history.Observe(result,source.width,source.height,nowMs);
    auto evidence=EvidenceFromMeasurement(source,result);
    const bool applied=evidence.available;
    // Unknown, missing work and sparse full-extent guesses gain no new authority.
    // Use the existing CURRENT-frame detector, not old crop/picture pixels.
    if(!applied)evidence=ExtractActivePictureEvidence(source);
    static thread_local uint64_t nextLogMs=0;
    static thread_local bool previousApplied=false;
    if(nowMs>=nextLogMs||applied!=previousApplied){
        nextLogMs=nowMs+250;previousApplied=applied;
        DebugLog::Log("[AR-ACTIVE] generation=%llu source=%llu domain=%llu viewport=%llu renderer=%llu applied=%d prior=%d kind=%u complete=%d measured=%d,%d,%d,%d score=%.3f support=%.2f,%.2f,%.2f,%.2f reason=%s selected_kind=%u selected=%d,%d,%d,%d worker_ms=%.3f",
            identity.transportGeneration,identity.acceptedSequence,identity.analysisDomainKey,identity.viewportGeneration,identity.rendererGeneration,
            applied,prior.valid,unsigned(result.kind),result.complete,result.left,result.top,result.right,result.bottom,result.score,
            result.edges[0].support,result.edges[1].support,result.edges[2].support,result.edges[3].support,result.reason,
            unsigned(evidence.classification),evidence.trustedBounds.left,evidence.trustedBounds.top,evidence.trustedBounds.right,evidence.trustedBounds.bottom,
            std::chrono::duration<double,std::milli>(Clock::now()-start).count());
    }
    return evidence;
}
} // namespace ExperimentalAspect
