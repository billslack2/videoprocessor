#pragma once
#include "PresentationDecisionTelemetry.h"
#include <ctime>
#include <cstdio>
#include <algorithm>
namespace PresentationIncident {
using namespace PresentationDecisionTelemetry;
inline std::string Timecode(const Output& o) {
    const time_t seconds=static_cast<time_t>(o.utcMs/1000);tm utc{};gmtime_s(&utc,&seconds);
    auto tag=o.instance;tag.erase(std::remove(tag.begin(),tag.end(),'{'),tag.end());tag=tag.substr(0,8);
    char text[192]{};sprintf_s(text,"%02d:%02d:%02d.%03uZ %s G%llu O%llu S%llu",utc.tm_hour,utc.tm_min,utc.tm_sec,
        unsigned(o.utcMs%1000),tag.c_str(),o.generation,o.outputId,o.selectedSequence);return text;
}
inline bool Current(const Output& o){return o.facts.available && o.facts.generation==o.generation && o.facts.sequence==o.selectedSequence;}
inline double Area(const Rect& r){return (std::max)(0.0,r.right-r.left)*(std::max)(0.0,r.bottom-r.top);}
inline bool UsableCaptureExtent(unsigned width,unsigned height){return width>1 && height>1;}
struct Request {bool capture=false;uint64_t incident=0;const char* phase="";const char* reason="";};
class Policy {
    Output previous_;bool have_=false;uint64_t lastIncident_=0,followupAt_=0,incident_=0;unsigned captures_=0;
    const char* reason_="";bool mismatchCaptured_=false,limitReported_=false;
public:
    static constexpr unsigned MaxCaptures=12;
    Request Plan(const Output& o,bool enabled) {
        Request r;if(!enabled || !o.rendered || !Current(o))return r;
        if(captures_>=MaxCaptures){if(!limitReported_){limitReported_=true;return {false,incident_,"limit","session-capture-limit"};}return r;}
        const bool contextChanged=have_ && (o.generation!=previous_.generation ||
            o.facts.viewport!=previous_.facts.viewport || o.facts.format!=previous_.facts.format ||
            o.facts.domain!=previous_.facts.domain || o.facts.requestedMode!=previous_.facts.requestedMode || o.facts.processingProfile!=previous_.facts.processingProfile);
        if(contextChanged) {
            // Start comparison in the new profile without carrying a screenshot
            // or cooldown from the previous profile. The session cap still applies.
            const bool canceled=followupAt_!=0;followupAt_=lastIncident_=0;mismatchCaptured_=false;
            if(canceled)return {false,incident_,"canceled","context-changed"};
            return r;
        }
        if(followupAt_) {
            if(o.generation!=previous_.generation || o.facts.viewport!=previous_.facts.viewport || o.facts.format!=previous_.facts.format || o.facts.domain!=previous_.facts.domain){followupAt_=0;return {false,incident_,"canceled","context-changed"};}
            if(o.tickMs>=followupAt_){followupAt_=0;++captures_;return {true,incident_,"after",reason_};}
            return r;
        }
        const bool same=have_ && Current(previous_) && previous_.generation==o.generation &&
            previous_.facts.viewport==o.facts.viewport && previous_.facts.format==o.facts.format &&
            previous_.facts.domain==o.facts.domain && previous_.facts.requestedMode==o.facts.requestedMode && previous_.facts.processingProfile==o.facts.processingProfile;
        if(!same){mismatchCaptured_=false;return r;}
        if(lastIncident_ && o.tickMs-lastIncident_<20000)return r;
        const char* reason=nullptr;
        if(previous_.facts.nativeAvailable && o.facts.nativeAvailable && previous_.facts.native==o.facts.native &&
            Area(o.source)>Area(previous_.source)*1.03)reason="presentation-widened-with-unchanged-native";
        else if((o.facts.requestedMode>=2 || previous_.facts.mode>=2) && previous_.facts.composed && !o.facts.composed && !o.facts.absence)
            reason="advanced-composition-lost";
        else if(!mismatchCaptured_ && o.facts.nativeAvailable && o.facts.measuredAvailable && Area(o.facts.measured)>0 &&
            std::abs(Area(o.facts.native)-Area(o.facts.measured))>Area(o.facts.native)*0.03)
            reason="measured-and-accepted-picture-disagree";
        if(!reason)return r;
        if(std::string(reason)=="measured-and-accepted-picture-disagree")mismatchCaptured_=true;
        reason_=reason;++incident_;lastIncident_=o.tickMs;followupAt_=o.tickMs+2000;++captures_;
        return {true,incident_,"failure",reason};
    }
    void Submitted(const Output& o){if(o.rendered && o.submitted){
        if(!o.facts.nativeAvailable || !o.facts.measuredAvailable || Area(o.facts.native)<=0 ||
            std::abs(Area(o.facts.native)-Area(o.facts.measured))<=Area(o.facts.native)*0.03)mismatchCaptured_=false;
        previous_=o;have_=true;
    }}
    // Planning happens before Present. A failed/internal output must not consume
    // a reservation or the pending after-image; commit only the exact submitted output.
    Request Preview(const Output& o,bool enabled) const {
        if(!enabled || !o.rendered || !Current(o))return {};
        auto next=*this;return next.Plan(o,enabled);
    }
    void CompleteSubmitted(const Output& o,bool enabled,uint64_t queuedOutputId) {
        if(!o.rendered || !o.submitted || !Current(o))return;
        if(!enabled){Submitted(o);return;}
        auto next=*this;const auto request=next.Plan(o,enabled);
        if(!request.capture || (queuedOutputId!=0 && queuedOutputId==o.outputId))*this=std::move(next);
        Submitted(o);
    }
    unsigned CapturesReserved()const{return captures_;}
};
}
