#pragma once
#include <cstdint>

// Run owns no pixels or timeline state. The caller revalidates exact source
// ownership on every pass. A zero deadline requests cache-only consumption.
struct PreparedPictureWindowResult {
    bool completed=false;
    bool optionalAttempted=false;
};

template<class Clock,class Analyze>
PreparedPictureWindowResult RunPreparedPictureWindowPhases(
    bool freshSourceEligible,int64_t optionalDeadline,Clock now,Analyze analyze)
{
    PreparedPictureWindowResult result;
    if(!freshSourceEligible)return result;
    // Preserve the existing optional pass first: an early cache-only pass can
    // otherwise finalize a provisional interpretation before its refinement.
    if(optionalDeadline>0 && now()<optionalDeadline) {
        result.optionalAttempted=true;
        result.completed=analyze(optionalDeadline);
    }
    // Expiry forbids additional scans, not consumption of immutable facts
    // completed before or during the optional pass. No ticket is renewed.
    if(!result.completed)result.completed=analyze(int64_t(0));
    return result;
}
