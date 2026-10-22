#pragma once

#include <atlstr.h>

namespace OsdTelemetryText
{
// A supported getter can intentionally clear its value after display change,
// device loss, or LUT reload. Empty is not a request to resurrect old telemetry.
inline void ApplyFallback(CString& queried, bool queryHandled,
    bool sameGeneration, const CString* previous)
{
    if (!queryHandled && sameGeneration && previous) queried = *previous;
}
}
