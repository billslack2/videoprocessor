# PresentMon dependency for the VP display observer

This folder contains the subset of Intel PresentMon PresentData used by
VideoProcessor-DisplayObserver. The source is pinned to upstream tag
v2.1.0, commit de71bba2fb65482f9cf6486284c9eed6aa87698f:

- Source tag: <https://github.com/GameTechDev/PresentMon/tree/v2.1.0>
- Exact source commit: <https://github.com/GameTechDev/PresentMon/commit/de71bba2fb65482f9cf6486284c9eed6aa87698f>
- License: [MIT](https://github.com/GameTechDev/PresentMon/blob/v2.1.0/LICENSE.txt)

The copied subset includes the five translation units and headers they need:
Debug, GpuTrace, PresentMonTraceConsumer, PresentMonTraceSession,
TraceConsumer, and the ETW provider event headers. The project compiles these
files directly with the observer, using the existing VP-compatible VS2019/v142,
C++14 toolchain. GPU tracking, input tracking, and frame-type tracking are off.

## Local instrumentation

Two small changes keep coverage status truthful:

1. PresentMonTraceConsumer exposes an atomic
   mCompletedPresentRingOverflowCount. It increments when a completed
   present is discarded or evicted because the upstream completed-event ring
   was full. The observer reports that cumulative count in each STATUS line.
2. PMTraceSession retains the ControlTraceW(QUERY) and ControlTraceW(STOP)
   result codes. If querying ETW counters fails, the observer reports -1 for
   unavailable loss counts rather than presenting zero as measured evidence.

The observer never calls StopNamedTraceSession; it stops only the session
handle it successfully created. A unique parent-provided session name lets the
parent clean up that exact session if the helper terminates unexpectedly.
