#pragma once
#include <cstdint>

// Run owns no pixels or timeline state. The caller revalidates exact source
// ownership on every pass. Zero foreground allowance still permits the
// original bounded asynchronous dispatch; an expired positive ticket does not.
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
        result.completed=analyze(optionalDeadline,true);
    }
    // Expiry forbids new work, not consumption of completed immutable facts.
    // Preserve the original zero-ticket background dispatch opportunity after
    // a missed Prime pass. The NEW expiry fallback may only consume ready work.
    if(!result.completed)result.completed=analyze(int64_t(0),optionalDeadline<=0);
    return result;
}
