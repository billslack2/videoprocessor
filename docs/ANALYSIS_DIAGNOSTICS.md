# Live analysis diagnostics

## Capture the current failure without restarting

Open the main VideoProcessor window's system menu and choose:

- **Capture analysis diagnostics...** for structured metadata.
- **Capture analysis diagnostics with input frames...** to also retain native analysis input bytes after the trigger.

The command starts a capture in the running process. It does not restart the
renderer, reset detector history, change configuration, or request new detector
measurements. The application log records the output folder, and the window caption briefly
confirms that capture started. A compatible active VP Renderer supplies frame evidence;
unsupported producers are reported by the UI. A second request while recording
does not start an overlapping capture.

The recorder keeps up to 10 seconds of metadata before the trigger and records
up to 20 seconds afterward. These are time windows subject to bounded storage:
the rolling ring holds 8,192 records and a capture admits at most 65,536 records,
including its copied prehistory. A busy writer or a high event rate can therefore
leave gaps. Submission uses a try-lock and drops evidence instead of waiting for
the writer. Record construction reuses existing decisions and samples.

Input pixels are opt-in and **post-trigger only**. At most two retained packets
are outstanding, each at most 128 MiB; the session admits at most 2 GiB of frame
bytes. Queue contention and these limits can skip frames. This is a bounded
sample of the original ordered inputs, not a guaranteed complete recording.
Metadata collection continues when the raw-frame budget is exhausted.

The host owns the recorder and writer across renderer recreation. The renderer
DLL forwards fixed metadata through an additive export; it does not create a
second recorder. A raw packet retains an independent immutable storage owner
until the writer releases it. The producer does not copy or write pixels on the
render thread. The plugin stays loaded for the process lifetime so retained
owner release callbacks remain valid.

## Files

The default folder is beside the executable under
`logs/analysis-<date-time>-<process-id>-<sequence>/`:

| File | Contents |
| --- | --- |
| `analysis.jsonl` | One schema-1 structured event per line, including the available prehistory. |
| `frames.jsonl` | Written native frame index, filename, byte count, dimensions and selected identity fields. |
| `frame-000001.bin` | Original analysis storage bytes, including its stride/layout. |
| `frame-000001.txt` | Numeric format and exact identity sidecar for that binary. The suffix replaces `.bin`; it is not `.bin.txt`. |
| `capture-summary.json` | Session write counts, configured bounds, and recorder lifetime drop/overwrite/write-failure counters. |

Frame files require the input-frame option. The summary is written when the
capture drains. Its `session` counters describe this capture; its `lifetime`
counters include events since recorder construction, including outside this
capture. An ordinary rolling-history overwrite is not itself a lost capture
record. Sequence gaps and frame identity gaps must be considered alongside these
counters. Disk failure can leave incomplete files; a folder's existence does not
prove successful completion. Captured video pixels belong in diagnostic scratch
storage, outside the source repository.

## Structured record schema

`schema` is 1. `kind` is 1 for frame decisions, 2 for events, and 3 for subtitle
worker events. `sequence` is assigned by the host recorder; it orders accepted
records. `timestampNs` uses a monotonic clock and is not a UTC timestamp.

Join records using the complete frame context, not one counter:
`transportGeneration`, `acceptedSequence`, `sourceFrameNumber`,
`captureTimestamp`, `sourceFormatGeneration`, `viewportGeneration`,
`rendererGeneration`, and `analysisDomainKey`. Policy and continuity are recorded
separately as `policyGeneration` and `continuityGeneration` where applicable.
`frameIdentity` mirrors `acceptedSequence` for frame/worker records.
`rendererInstanceKey` is a process-local, address-derived renderer or worker
instance key; it is not source identity and is not stable across process runs.

`sourceFormat` on `analysis-input` is the numeric `VideoFrameEncoding`, while
`sourceFormatGeneration` is a generation. `analysisFormat` is the
`AnalysisLumaFormat` enum. `sourceWidth/Height` and `analysisWidth/Height` describe
their corresponding raster sizes. A field not supplied by a stage remains zero;
do not interpret every zero as a measured value. `reason` and `detail` contain
bounded diagnostic text. `resultCode`, `flags`, `counters`, and `metrics` are
stage-specific. Rectangles use `[left, top, right, bottom]` in source coordinates;
the `viewport` field on `presentation` is the target crop.

### Stage field mapping

Array indices below start at zero. Unlisted elements are unused by that stage.
Enum values correspond to the headers from the captured build.

| Stage | Fields |
| --- | --- |
| `analysis-input` | `counters[0..5]`: encoding, color space, transfer key, sample range, color depth, bit shift. Duration is in `frameDuration100ns`; script/optimization/near-bar policy and discontinuity/repeat flags are explicit fields. |
| `ar-decision` | `previous`: prior presentation; `measured`: measured bounds; `candidate`: current evidence; `published`: transition bounds. `resultCode`: evidence classification. `counters[0..7]`: matching candidates, contradictions, reversals, latency frames, outward confirmations, outward confirmations required, horizontal reason, vertical reason. `metrics[0..3]`: global luma P90, subtitle hold ms, engage drift ms, release drift ms. |
| `ar-edges` | `previous/candidate`: expansion base/candidate. Four groups of six metrics describe left, top, right, bottom: bar pixels, black fraction, luma P90, texture, continuity, neutral-chroma fraction. `reason` says whether these are expansion strips or excluded bands. |
| `lookahead` | `previous/candidate`: proof base/first observation. `counters[0..6]`: configured depth, queued future frames, observations, proof samples, owner, horizontal reason, vertical reason. `metrics[0]`: proof inspection ms. |
| `worker-submit`, `worker-start`, `worker-complete`, `worker-consumed`, `worker-miss`, `worker-dedup`, `worker-drop` | `counters[0..1]`: mismatch mask, shared-picture accepted sequence. `metrics[0..2]`: analysis ms, picture top, picture bottom. `resultCode`: text line count; `subtitle`: text bounds. Reasons distinguish queue state and acceptance. |
| `subtitle-output` | `subtitle`: presented text bounds; `resultCode`: presented line count. `counters[0..5]`: preview accepted sequence, preview domain, cue, matching frames, raw line count, component count. `metrics[0..3]`: near-bar gap, analysis ms, picture top, picture bottom. `detail`: presentation decision reason. |
| `presentation` | `previous`: prior crop; `measured`: latest evidence; `published`: effective geometry; `displayed`: actual presentation crop. `resultCode`: crop owner. `counters[0..5]`: recovery samples/required, episode revalidation samples/required, revalidation gates, scene boundary. `metrics[0..1]`: global luma P90, configured screen aspect. |
| `renderer-lifecycle` | Create/destroy and selected existing reset/configuration messages in `reason`. This stage can have no frame identity. |

Flag bits, listed from bit 0 upward:

- `ar-decision`: publish, apply scheduled decision, defer presentation, defer outward, defer partial composition, block near-black acquisition, buffered expansion ready, global near-black.
- `ar-edges`: analysis valid, presentation valid.
- `lookahead`: built, proof valid, owner eligible, pending.
- Worker stages: analyzed, detected text, shared picture required, shared picture available for this exact frame, explicit unavailable-authority measurement.
- `subtitle-output`: detected, held, composition succeeded, bar authority, preview analyzed, work limit reached.
- `presentation`: apply crop, aspect fill applied, recovery active, near-black episode full raster, admission blocked.

`fullScans`, `requestMisses`, `pendingFrames`, and `acceptedSeeds` are available
record fields, not a universal cumulative-rate contract. In particular, current
`ar-decision` pending/seed values describe the observed state. Derive worker
activity from the worker stages and their identities; do not equate render
callbacks with newly accepted source frames.

## Native sidecar format

The first line contains ten whitespace-separated values:

```text
width height stride0 stride1 format encoding colorspace top bottom scriptHint
```

The second line starts with `identity_v1` and has this exact order:

```text
identity_v1 transportGeneration acceptedSequence sourceFrameNumber captureTimestamp sourceFormatGeneration viewportGeneration rendererGeneration analysisDomainKey policyGeneration continuityGeneration nearBarDistance optimizationMode frameDuration100ns discontinuity cadenceRepeat rgbBitAlignment
```

`format`: 0=P010, 1=P210, 2=NativeRgb, 3=NativeYuv422,
4=DecodedDolbyVision422, 5=SdrTunnelRgb. Encoding and color space are the numeric
values from `VideoFrameEncoding.h` and `ColorSpace.h`. Strides are bytes.
`rgbBitAlignment` preserves the SDR tunnel convention (1=high eight bits,
2=low eight bits). Top/bottom are captured picture-reference metadata; they do
not authorize a crop during fresh replay.

These are the prepared original analysis bytes, before tone mapping, scaling,
cropping or subtitle composition. Valid Dolby Vision uses decoded 12-bit 4:2:2
storage with the existing ten-bit evidence sampler; packed DV transport must not
be interpreted as RGB picture evidence. All six analysis layouts are supported
by the replay reader without changing their sampling rules.

## Fresh acquisition replay and comparison

Set `VP_ANALYSIS_REPLAY_DIRECTORY` in the test process environment to a completed
capture directory, then run
`AnalysisDiagnosticReplayTests::OptionalOrderedNativeAnalysisCaptureReportsFreshAcquisition`.
Clear the environment variable afterward. With no directory set, that optional
test reports that replay was not requested. Results are written to test output;
the test does not produce a separate replay report file.

The reader loads sorted `frame-*.bin` files and their matching `.txt` sidecars,
one frame at a time (at most 4,096 files, 128 MiB each). It requires the actual
`identity_v1` data; old single-frame fixtures do not receive invented identities.
It uses production `AnalysisLumaSource`, active-picture extraction, subtitle
measurement, darkness/crop-acquisition constraints and transition components.
Identity/context gaps restart fresh confirmation; repeated identities do not
add independent votes.

Replay starts with empty history. It does not restore the live renderer's
remembered geometry, near-black episode, worker scheduling, queue state or
presentation history. Consequently it cannot reproduce the entire live session
or establish that a different live crop was wrong. Compare fresh results with
`analysis-input`, `ar-decision`, worker, and `presentation` records at the same
complete identities. Use the prehistory to distinguish an existing-history
retention decision from what these pixels can acquire from scratch. Missing raw
frames limit temporal conclusions even when metadata exists for those frames.

The deterministic tests cover a full-raster-to-scope sequence, duplicate/gap
handling, rolling-trace result equivalence, and an actual recorder write followed
by sidecar loading and production acquisition. These establish component and
file-contract behavior; they do not substitute for a live capture of the
specific failure or measure end-to-end render performance.

## Implementation validation (2026-10-14)

- Clean x64 Release solution rebuild succeeded using the installed Magewell SDK.
- 760 selected diagnostic, worker, analysis recovery, and AR regression tests passed; this was not the full suite.
- End-to-end native capture, sidecar read, and production component replay passed.
- Rolling metadata submission microbenchmark: 20,000 calls in 1,040.7 microseconds (0.052 microseconds/call), with 11 allowed contention drops. This is not a disk-throughput or full-render-pipeline benchmark.
- No deployment, renderer restart, or live capture was performed for this change. The delayed live AR transition still needs a capture from the instrumented build.
- Build log: `rebuild-analysis-trace.log`; test result: `TestResults/analysis-trace-final.trx`.
