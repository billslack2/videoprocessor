#pragma once
#include <cstdint>

// Run owns no pixels or timeline state. The caller revalidates exact source
// ownership on every pass. Zero foreground allowance still permits the
// original bounded asynchronous dispatch; an expired positive ticket does not.
struct PreparedPictureWindowSource {
    bool available=false,repeat=false,discontinuity=false,backlog=false;
    bool resizePending=false,renegotiationPending=false;
};
struct PreparedPictureWindowAdmission {
    bool consumeReady=false,allowForeground=false,allowZeroTicketBackground=false;
};
inline PreparedPictureWindowAdmission MakePreparedPictureWindowAdmission(
    const PreparedPictureWindowSource& source,int64_t optionalDeadline)
{
    PreparedPictureWindowAdmission admission;
    const bool fresh=source.available && !source.repeat;
    const bool compatible=fresh && !source.discontinuity &&
        !source.resizePending && !source.renegotiationPending;
    // Queue pressure blocks speculative work, not exact facts already ready.
    admission.consumeReady=compatible;
    admission.allowZeroTicketBackground=compatible && !source.backlog;
    // Preserve the earlier positive-ticket path. Its analyzer still performs
    // full source/queue/epoch checks; this is not a new crop permission.
    admission.allowForeground=fresh && optionalDeadline>0;
    return admission;
}
struct PreparedPictureWindowResult {
    bool completed=false;
    bool optionalAttempted=false;
};

template<class Clock,class Analyze>
PreparedPictureWindowResult RunPreparedPictureWindowPhases(
    const PreparedPictureWindowAdmission& admission,int64_t optionalDeadline,Clock now,Analyze analyze)
{
    PreparedPictureWindowResult result;
    // Preserve the existing optional pass first: an early cache-only pass can
    // otherwise finalize a provisional interpretation before its refinement.
    if(admission.allowForeground && optionalDeadline>0 && now()<optionalDeadline) {
        result.optionalAttempted=true;
        result.completed=analyze(optionalDeadline,true);
    }
    // Expiry forbids new work, not consumption of completed immutable facts.
    // Only the original zero-ticket background permission admits async jobs.
    if(!result.completed && admission.consumeReady)
        result.completed=analyze(int64_t(0),optionalDeadline<=0 && admission.allowZeroTicketBackground);
    return result;
}
