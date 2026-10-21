#pragma once
#include <cmath>
#include <cstdint>

// Source sequence is shared by lookahead and presentation. Output repeats must
// never advance the phase; missing observations remain unknown, not "no text".
inline bool InputFrameAnalysisDue(double inputHz, uint64_t sourceSequence)
{
    return !std::isfinite(inputHz) || inputHz <= 30.0 ||
        sourceSequence == 0 || (sourceSequence & 1ULL) != 0;
}
