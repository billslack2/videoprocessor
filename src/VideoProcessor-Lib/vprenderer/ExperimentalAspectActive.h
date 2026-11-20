#pragma once
#include "../ExperimentalAspectDetector.h"
#include "ExperimentalAspectMode.h"
#include <ActivePictureEvidence.h>
#include <DebugLog.h>
#include <Windows.h>
#include <chrono>
#include <cstring>

namespace ExperimentalAspect {
constexpr unsigned ActiveMeasurementBudgetMilliseconds=2;
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
    static const bool enabled=[] {
        const auto mode=ReadTrialMode();
        DebugLog::Log("[AR-EXPERIMENT] consumer=active mode=%s source=%s",TrialModeName(mode.mode),mode.source);
        return mode.mode==TrialMode::Active;
    }();
    return enabled;
}
// No legacy certificates are copied to new geometry. Uncropped axes remain
// explicitly unknown unless current perimeter samples independently support them.
inline ActivePictureEvidence EvidenceFromMeasurement(const AnalysisLumaSource& source,const Result& measured){
    ActivePictureEvidence out;
    if(!measured.complete||!source.IsValid()||source.range==AnalysisLumaRange::Unknown)return out;
    // Retention consumes these code thresholds even without a calibrated matte
    // certificate. Use the declared source range, never the default full-range
    // zero for limited-range black64. This is nominal metadata, not measured
    // calibration: evaluated, qualified edges and qualified axes remain false.
    out.blackReference.floor=source.range==AnalysisLumaRange::Limited?64:0;
    out.blackReference.threshold=out.blackReference.floor+24;
    const int w=source.width,h=source.height;
    out.chromaSamples=measured.chromaSamples;
    if(measured.kind==Kind::FullRaster){
        // Qualification belongs to the completed current-pixel detector pass.
        // Mere brightness, a missing bar pair or history never grants extent.
        if(!source.IsValid()||source.range==AnalysisLumaRange::Unknown||
           !measured.fullExtentQualified||measured.left!=0||measured.top!=0||
           measured.right!=w||measured.bottom!=h)return out;
        out.available=true;out.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
        out.proposedBounds={0,0,w,h,w,h,double(w)/h,ActivePictureBounds::BarAxes::NONE};
        out.trustedBounds=out.proposedBounds;out.lumaSamples=measured.samples;
        out.reason="experimental qualified current-pixel full extent";
        auto extentAxis=[&](int first,int opposite){
            ActivePictureAxisEvidence axis;axis.scanComplete=true;
            // A dark/ambiguous edge stays unknown. Do not manufacture four-edge
            // support, matte calibration, glyph ownership or crop certificates.
            const bool both=measured.pictureEdgeSupported[first]&&measured.pictureEdgeSupported[opposite];
            axis.state=both?ActivePictureAxisState::FULL_EXTENT_SUPPORTED:ActivePictureAxisState::UNKNOWN;
            axis.reason=both?ActivePictureAxisReason::FULL_EXTENT_SUPPORTED:ActivePictureAxisReason::NO_FULL_EXTENT_SUPPORT;
            return axis;
        };
        out.axisEvidence.horizontal=extentAxis(0,2);out.axisEvidence.vertical=extentAxis(1,3);
        return out;
    }
    if(measured.kind!=Kind::Bars)return out;
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
            e.lumaFloor=m.matte;e.lumaP90=m.matteP90;e.lumaDispersion=m.noise;e.confidence=m.score;e.trusted=true;}
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
    const unsigned matteAxes=(horizontal && measured.opposingMatteQualified[0]?2u:0u) |
        (vertical && measured.opposingMatteQualified[1]?1u:0u);
    if(matteAxes){
        out.axisEvidence.measuredMatteBounds=b;
        out.axisEvidence.measuredMatteBounds.trustedBarAxes=
            static_cast<ActivePictureBounds::BarAxes>(matteAxes);
    }
    // Only completed current measurements with strong support on BOTH edges
    // may nominate immediate expansion. Admission still independently checks
    // exact new-picture strips, the established base and source context.
    out.strongCurrentBarMeasurement=source.IsValid()&&source.range!=AnalysisLumaRange::Unknown&&
        (!horizontal||(measured.acceptedPairAttempt[0]>0&&measured.edges[0].support>=7.0/9&&measured.edges[2].support>=7.0/9))&&
        (!vertical||(measured.acceptedPairAttempt[1]>0&&measured.edges[1].support>=7.0/9&&measured.edges[3].support>=7.0/9));
    return out;
}
// An interrupted nomination leaves geometry unavailable for this source. The
// legacy scan can misclassify lifted bars as full raster when it follows a
// timeout. Completed ambiguity can still use its independent current pixels.
inline ActivePictureEvidence CurrentFallback(const AnalysisLumaSource& source,const Result& result){
    if(result.budgetExhausted){
        ActivePictureEvidence unavailable;
        unavailable.reason="experimental measurement budget exhausted";
        return unavailable;
    }
    auto evidence=ExtractActivePictureEvidence(source);
    const auto& b=evidence.trustedBounds;
    if(evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED){
        const int black=source.range==AnalysisLumaRange::Limited?64:0;
        auto contradictsMatte=[&](const ActivePictureEdgeEvidence& first,
            const ActivePictureEdgeEvidence& last){
            // The legacy scan already measured both distributions. Use its
            // P10/P90 interval to tolerate isolated captions and noise.
            const double lowFirst=first.lumaP90-first.lumaDispersion;
            const double lowLast=last.lumaP90-last.lumaDispersion;
            auto nonMatte=[&](const ActivePictureEdgeEvidence& edge,double low){
                return edge.barPixels>0 && (low>black+MaxMatteElevation ||
                    edge.neutralChromaFraction<.5);
            };
            return nonMatte(first,lowFirst) || nonMatte(last,lowLast) ||
                (first.barPixels>0 && last.barPixels>0 &&
                 (lowFirst-last.lumaP90>MaxOpposingMatteDifference ||
                  lowLast-first.lumaP90>MaxOpposingMatteDifference));
        };
        const bool horizontal=b.left>0 || b.right<source.width;
        const bool vertical=b.top>0 || b.bottom<source.height;
        if((horizontal && ((result.complete && result.exteriorPictureConflict[0]) ||
                          contradictsMatte(evidence.left,evidence.right))) ||
           (vertical && ((result.complete && result.exteriorPictureConflict[1]) ||
                        contradictsMatte(evidence.top,evidence.bottom)))){
            evidence={};
            evidence.reason="current exterior evidence contradicts fallback crop";
        }
    }
    return evidence;
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
            [&](int x,int y,Pixel& v){
                AnalysisLumaSample p;
                if(!source.Sample(x,y,p))return false;
                v={p.luma,p.chromaU,p.chromaV};return true;
            },
            [&]{return Clock::now()-start<std::chrono::milliseconds(ActiveMeasurementBudgetMilliseconds);},24000,&prior);
    }
    history.Observe(result,source.width,source.height,nowMs);
    auto evidence=EvidenceFromMeasurement(source,result);
    const bool applied=evidence.available;
    // Unknown, missing work and sparse full-extent guesses gain no new authority.
    // Use the existing CURRENT-frame detector, not old crop/picture pixels.
    if(!applied)evidence=CurrentFallback(source,result);
    static thread_local uint64_t nextLogMs=0;
    static thread_local bool previousApplied=false;
    if(nowMs>=nextLogMs||applied!=previousApplied){
        nextLogMs=nowMs+250;previousApplied=applied;
        DEBUGLOG_ENHANCED("[AR-ACTIVE] generation=%llu source=%llu domain=%llu viewport=%llu renderer=%llu applied=%d prior=%d kind=%u complete=%d measured=%d,%d,%d,%d score=%.3f support=%.2f,%.2f,%.2f,%.2f pair_attempts=%u,%u accepted_pair=%u,%u full_qualified=%d full_score=%.3f picture_edges=%d,%d,%d,%d unpaired_support=%.2f,%.2f,%.2f,%.2f reason=%s selected_kind=%u selected=%d,%d,%d,%d worker_ms=%.3f",
            identity.transportGeneration,identity.acceptedSequence,identity.analysisDomainKey,identity.viewportGeneration,identity.rendererGeneration,
            applied,prior.valid,unsigned(result.kind),result.complete,result.left,result.top,result.right,result.bottom,result.score,
            result.edges[0].support,result.edges[1].support,result.edges[2].support,result.edges[3].support,result.pairAttempts[0],result.pairAttempts[1],result.acceptedPairAttempt[0],result.acceptedPairAttempt[1],result.fullExtentQualified,result.fullExtentScore,
            result.pictureEdgeSupported[0],result.pictureEdgeSupported[1],result.pictureEdgeSupported[2],result.pictureEdgeSupported[3],
            result.unpairedBoundarySupport[0],result.unpairedBoundarySupport[1],result.unpairedBoundarySupport[2],result.unpairedBoundarySupport[3],result.reason,
            unsigned(evidence.classification),evidence.trustedBounds.left,evidence.trustedBounds.top,evidence.trustedBounds.right,evidence.trustedBounds.bottom,
            std::chrono::duration<double,std::milli>(Clock::now()-start).count());
    }
    return evidence;
}
} // namespace ExperimentalAspect
