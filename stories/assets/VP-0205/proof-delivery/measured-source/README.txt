Prepared harness. DO NOT link/run until root confirms the shared x64 Release build passed.

After shared Release PASS, link without running measurements:
  .\run-after-release.ps1 -Stage Build -SharedReleaseBuildConfirmed
After relevant native tests have finished, run without rebuilding:
  .\run-after-release.ps1 -Stage Run -SharedReleaseBuildConfirmed -NativeTestsFinished
Or use -Stage All only after both prerequisites are confirmed.
Pass -SourceRoot if the harness sits outside the VP tree, -OutputDirectory for
an alternate results folder, -VisualStudioInstallation for a specific VS copy,
-MagewellSdkRoot for a machine-local SDK, and -PythonExecutable when desired.
The driver otherwise discovers the ancestor VP root, VS(vswhere/developer-shell),
and python.exe. No E: worktree path is embedded in source or MSBuild targets.
The script disables project-reference builds and uses separate DLL/object outputs.
It records current source HEAD/status and source/library hashes before linking.

15 synthetic 4K cases: P010/P210/BGRA8 x bright expansion, textured expansion,
unchanged scope, dark texture, sparse caption. Each window uses three separate
immutable owned allocations, distinct accepted/source-frame/timestamp identities,
correct queued source format generation and domain metadata, and a retained
raw REQUEST SLOT as key.rawOwner. Raw payload exposes the exact source, identity,
domainKey, evidence and darkness required by EvaluatePreparedOutwardWindow. BGRA uses PrepareAnalysisFrame;
P010/P210 explicitly model valid synthetic prepared planar contracts (ordinary
capture factory currently selects native YUV422).

Four rotated delivery paths, 101 trials per case: actual direct cooperative
retention at2/3/4ms, versus three actual PreparedPictureEvidenceCache lookups and
production BuildBufferedPictureExpansion at0 new pixel work admission. Actual
OptionalAnalysisWorker precomputes EvaluatePreparedPictureEvidence separately.
Three caches represent the production per-frame ownership; they are not one
2-entry cache pretending to hold a3-frame window. Every completed path checks
full retention outputs and certificate decision fingerprint against the same
mandatory reference, including effective first-frame identity.

Worker service/threadcycles, worker snapshot wall, and dispatch-to-ready observed
latency are separate. The last includes up to1ms harness wait quantization.
Raw extraction/darkness, pixel fixture generation and reference evaluation are
outside both delivery paths; fixture CSV labels their combined setup duration.
Proof completion, valid certificate, no change and invalid evidence stay distinct.
Cached negative results are still complete entries, not useful geometry authority.
This measures component/certificate delivery, not full renderer or live adoption.

Files: proof_delivery_measurements.cpp, proof-delivery-hook.targets,
run-after-release.ps1, summarize-proof-delivery.py. No production edits.

Whole-worker addition: actual EvaluatePreparedOutwardWindow<Raw> is submitted as
ONE OptionalAnalysisWorker job, with3 distinct prepared frames,3 raw request slots,
3 per-frame retention cache request slots and exact current base/context. A
component-miss request asserts rawComputed3/retentionComputed3; the following
warm request asserts rawReused3/retentionReused3 and no new scans. Both compare
all retention fields and the full certificate (configured2/available2) against
the same direct reference; noChange must agree with complete exact native bounds.
whole-window-worker-{summary,raw}.csv separate service, worker snapshot and
observed-ready time/cycles. Component cold means cache miss, not cold CPU memory.
The cold/warm pair is ordered because warm requires the cold result; caller
2/3/4ms versus0newpixel delivery still rotates101trials as documented above.

Relocatable outputs: the targets use MSBuildThisFileDirectory for bin/obj/source,
SolutionDir for test includes. C++ uses VP_LOOKAHEAD_MEASUREMENT_OUTPUT_DIR
(set only for the child test host by the driver) or __FILE__ parent. Summarizer
accepts the results directory as its first argument. Run-only validates source,
current Release library and isolated DLL hashes against the build identity.
