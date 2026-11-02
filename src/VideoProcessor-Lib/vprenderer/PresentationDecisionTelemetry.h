#pragma once
#include <cstdint>
#include <string>
#include <sstream>
#include <locale>
#include <iomanip>
#include <cmath>

// Observations only: these records never authorize analysis, crops or composition.
namespace PresentationDecisionTelemetry {
struct Rect {
    double left=0,top=0,right=0,bottom=0;
    bool operator==(const Rect& b) const {return left==b.left && top==b.top && right==b.right && bottom==b.bottom;}
    bool operator!=(const Rect& b) const {return !(*this==b);}
};
template<class Bounds> Rect BoundsRect(const Bounds& b) {return {double(b.left),double(b.top),double(b.right),double(b.bottom)};}
struct Facts {
    bool available=false;
    uint64_t generation=0,sequence=0,viewport=0,format=0,domain=0,policy=0,continuity=0;
    uint64_t measurementSequence=0,nativeAcceptedSequence=0,captionSequence=0,cue=0;
    int width=0,height=0,classification=0,mode=0,requestedMode=0,processingProfile=0,preparationState=0;
    uint64_t preparationKey=0;bool shaderFailed=false;
    Rect measured,native,visible,caption,captionDestination;
    bool measuredAvailable=false,nativeAvailable=false,observationSupportsCrop=false;
    bool retentionEvaluated=false,retentionSafe=false,visibleAvailable=false;
    bool captionCurrent=false,captionDetected=false,captionHeld=false,captionIncomplete=false;
    bool composed=false,sourcePrepared=false,currentComposition=false,strongPrepared=false,absence=false;
    uint32_t preparationFailures=0;
    std::string measurementReason,scanReason,ownershipReason,erasureReason,continuationReason,decisionReason,owner;
};
struct Output {
    Facts facts;
    std::string instance;uint64_t process=0;
    uint64_t generation=0,inputSequence=0,selectedSequence=0,tickMs=0,qpc=0,qpcFrequency=0,outputId=0,utcMs=0;
    std::string timecode;bool timecodeDrawn=false;
    double sourcePts=0,outputPts=0,screenAspect=0,diagnosticCpuMs=0;
    Rect source,target;
    bool temporal=false,repeated=false,rendered=false,submitted=false;
};
inline std::string Quote(const std::string& value) {
    std::string s="\"";
    for(unsigned char c:value.substr(0,192)) {
        if(s.size()>128){s+="...";break;}
        if(c=='"' || c=='\\'){s+='\\';s+=char(c);}
        else if(c=='\n')s+="\\n";else if(c=='\r')s+="\\r";else if(c=='\t')s+="\\t";
        else if(c<32){const char* h="0123456789abcdef";s+="\\u00";s+=h[c>>4];s+=h[c&15];}
        else s+=char(c);
    }
    return s+'"';
}
inline std::string Number(double v) {if(!std::isfinite(v))return "null";std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(10)<<v;return s.str();}
inline void WriteRect(std::ostream& s,const Rect& b) {s<<'['<<Number(b.left)<<','<<Number(b.top)<<','<<Number(b.right)<<','<<Number(b.bottom)<<']';}
inline std::string Json(const Output& o) {
    const auto& f=o.facts;
    const bool joined=f.available && f.generation==o.generation && f.sequence==o.selectedSequence;
    std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(10)<<std::boolalpha;
    s<<"{\"schema\":1,\"instance\":"<<Quote(o.instance)<<",\"process\":"<<o.process<<",\"tick_ms\":"<<o.tickMs<<",\"qpc\":"<<o.qpc<<",\"qpc_hz\":"<<o.qpcFrequency
     <<",\"diagnostic_cpu_ms\":"<<Number(o.diagnosticCpuMs)<<",\"utc_ms\":"<<o.utcMs<<",\"timecode\":"<<Quote(o.timecode)<<",\"timecode_drawn\":"<<o.timecodeDrawn<<",\"output\":"<<o.outputId<<",\"generation\":"<<o.generation<<",\"input\":"<<o.inputSequence
     <<",\"selected\":"<<o.selectedSequence<<",\"facts_source\":"<<f.sequence<<",\"facts_current\":"<<joined
     <<",\"source_pts\":"<<Number(o.sourcePts)<<",\"output_pts\":"<<Number(o.outputPts)
     <<",\"temporal\":"<<o.temporal<<",\"repeated\":"<<o.repeated<<",\"rendered\":"<<o.rendered<<",\"submitted\":"<<o.submitted
     <<",\"screen_aspect\":"<<Number(o.screenAspect)<<",\"actual_source\":";WriteRect(s,o.source);s<<",\"actual_target\":";WriteRect(s,o.target);
    // A missing selected packet must never borrow the latest input's facts.
    if(!joined){s<<",\"reason\":\"selected-source-facts-unavailable\"}";return s.str();}
    s<<",\"viewport\":"<<f.viewport<<",\"format\":"<<f.format<<",\"domain\":"<<f.domain<<",\"policy\":"<<f.policy<<",\"continuity\":"<<f.continuity
     <<",\"raster\":["<<f.width<<','<<f.height<<"],\"measurement_source\":"<<f.measurementSequence<<",\"measurement_available\":"<<f.measuredAvailable
     <<",\"measurement_reason\":"<<Quote(f.measurementReason)<<",\"classification\":"<<f.classification<<",\"measured\":";WriteRect(s,f.measured);
    s<<",\"native_available\":"<<f.nativeAvailable<<",\"native_accepted_source\":"<<f.nativeAcceptedSequence<<",\"native\":";WriteRect(s,f.native);
    s<<",\"observation_supports_crop\":"<<f.observationSupportsCrop<<",\"retention_evaluated\":"<<f.retentionEvaluated<<",\"retention_safe\":"<<f.retentionSafe
     <<",\"visible_available\":"<<f.visibleAvailable<<",\"raw_visible\":";WriteRect(s,f.visible);
    s<<",\"subtitle_processing_profile\":"<<f.processingProfile<<",\"requested_mode\":"<<f.requestedMode<<",\"preparation_state\":"<<f.preparationState<<",\"preparation_key\":"<<f.preparationKey<<",\"shader_failed\":"<<f.shaderFailed<<",\"mode\":"<<f.mode<<",\"caption_source\":"<<f.captionSequence<<",\"caption_current\":"<<f.captionCurrent<<",\"cue\":"<<f.cue
     <<",\"detected\":"<<f.captionDetected<<",\"held\":"<<f.captionHeld<<",\"incomplete\":"<<f.captionIncomplete<<",\"caption\":";WriteRect(s,f.caption);
    s<<",\"destination\":";WriteRect(s,f.captionDestination);
    s<<",\"composed\":"<<f.composed<<",\"source_prepared\":"<<f.sourcePrepared<<",\"prepare_failures\":"<<f.preparationFailures
     <<",\"current_composition\":"<<f.currentComposition<<",\"strong_prepared\":"<<f.strongPrepared<<",\"absence\":"<<f.absence
     <<",\"scan_reason\":"<<Quote(f.scanReason)<<",\"ownership_reason\":"<<Quote(f.ownershipReason)<<",\"erasure_reason\":"<<Quote(f.erasureReason)
     <<",\"continuation_reason\":"<<Quote(f.continuationReason)<<",\"owner\":"<<Quote(f.owner)<<",\"reason\":"<<Quote(f.decisionReason)<<'}';return s.str();
}
inline std::string StateKey(Output o) {
    // Source counters advance during a stable scene. Preserve semantic freshness
    // explicitly, then remove those counters from change detection only.
    const bool current=o.facts.available && o.facts.generation==o.generation && o.facts.sequence==o.selectedSequence;
    o.tickMs=o.qpc=o.qpcFrequency=o.outputId=o.inputSequence=o.utcMs=0;o.sourcePts=o.outputPts=o.diagnosticCpuMs=0;o.timecode.clear();
    o.selectedSequence=1;o.facts.sequence=current?1:0;
    o.facts.measurementSequence=o.facts.nativeAcceptedSequence=o.facts.captionSequence=o.facts.cue=0;
    o.repeated=false;
    return Json(o);
}
struct Emission {bool emit=false,changed=false;uint64_t span=0;Rect previousSource;const char* event="heartbeat";};
class Recorder {
    std::string state_;uint64_t lastTick_=0,span_=0;bool have_=false;Rect previous_;
public:
    Emission Observe(const Output& o,bool force=false) {
        ++span_;const auto state=StateKey(o);const bool changed=!have_ || state!=state_;
        Emission e;e.changed=changed;e.previousSource=previous_;e.span=span_;
        e.emit=force || changed || o.tickMs<lastTick_ || o.tickMs-lastTick_>=1000;
        if(e.emit) {
            e.event=!have_?"initial":(o.source!=previous_?"presentation-change":(changed?"state-change":(force?"correlation":"heartbeat")));
            state_=state;previous_=o.source;lastTick_=o.tickMs;span_=0;have_=true;
        }
        return e;
    }
};
}
