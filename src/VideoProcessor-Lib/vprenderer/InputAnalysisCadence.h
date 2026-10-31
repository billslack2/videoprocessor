#pragma once
#include <cmath>
#include <cstdint>

// Source parity alone never authorizes omitting current-source evidence. A
// caller may skip only when it owns a valid, successfully submitted complete
// presentation to repeat instead. Lookahead cannot make that guarantee for a
// future queued source and must use the default fresh-analysis path.
inline bool InputFrameAnalysisDue(double inputHz, uint64_t sourceSequence,
    bool completePresentationRepeatReady = false)
{
    return !completePresentationRepeatReady || !std::isfinite(inputHz) ||
        inputHz <= 30.0 || sourceSequence == 0 || (sourceSequence & 1ULL) != 0;
}
