# Subtitle causal diagnostics

Use a recording and source-bound decision trace together. A subtitle may remain
`detected` while its composition is refused, or it may be composed successfully
and then rejected by the final crop/erasure check. A successful short steady
window does not establish that subsequent cue transitions work.

## Collect an incident

1. Preserve the screen recording, including the VP timecode, and the active
   configuration/state and deployed host/renderer identities.
2. While the failing section is playing, use the VP system menu's **Capture
   analysis diagnostics with input frames**. It does not restart capture or
   change subtitle profiles. Preserve the completed directory under `logs`.
3. Keep `analysis.jsonl`, `capture-summary.json`, `frames.jsonl`, and each native
   `.bin`/`.txt` pair together. Incident screenshots under `screenshots` provide
   rendered output and presentation decisions; they are not native input.
4. Run `tools/subtitle_causal_report.py --help` for the offline report. Use a new
   output directory so previous evidence is not overwritten. Preserve original
   frame files locally; the report references them rather than copying movie
   pixels into the source repository.

Current recorder defaults are a 10-second record pre-roll and a 20-second capture,
with at most 65,536 records, 2 GiB of native frame data, and two outstanding frame
writes. Native samples normally select every fifth input. Native frames are not
retrospectively available for the record pre-roll. These are bounded diagnostic
samples, not a consecutive-frame recording. The capture summary reports actual
sampling, selected misses, and bytes. Lifetime drops and history overwrites must
not be reported as losses in the current capture session.

## Join identities before interpreting events

Retain transport generation, accepted sequence, original source-frame number,
capture timestamp, source-format generation, viewport/renderer generation,
analysis domain, policy, and continuity. The overlay's selected source can be the
accepted sequence; it is not always the original capture source-frame number.
Renderer recreation can reuse short sequence values. Never join by sequence alone.

A worker template revision can be rearmed without rebuilding its immutable pixel
arrays. The template's latest source is therefore not necessarily the source of
its stored comparison pixels. The pixel witness's separate immutable-baseline
identity is the authority for that comparison. If either native endpoint was not
sampled, report it as unavailable; do not substitute a nearby frame.

## New causal records

All events use the existing fixed-layout analysis-record ABI and the existing
bounded diagnostic queue. Their identity is the current source unless noted.
They do not change subtitle admission, geometry, thresholds, or rendering.

| Stage | Evidence |
| --- | --- |
| `subtitle-held-certificate` | Evaluated and failed predicate masks, cue/reference identity, independent search and edge-proof facts, raw versus held bounds and current shared picture. A predicate not evaluated after short-circuit rejection is **unknown**, not passing. |
| `subtitle-held-reference` | Full original reference identity and interpretation context, linked to the current accepted/original source numbers. |
| `subtitle-held-line` | Current and held per-line bounds and their measured capture panels. These are measurement rectangles; they are distinct from the final operation's actual extraction rectangles. |
| `worker-cue-pixel-witness` | Template revision, typed failure stage, failure coordinate and optional neighbor, old/current YUV, classifications, thresholds, and operation-zone flags. The values come from samples already used by verification, with no additional source reads. |
| `worker-cue-pixel-seed` | Full identity and policy/continuity of the immutable sampled baseline, linked to the current rejection and template revision. |
| `subtitle-proof-rejection` | Final glyph-containment, physical-base or observation-edge rejection facts: required capture at the qualifier input (raw or explicitly marked continuation), prepared content/source/glyph rectangles, raw/prepared/native/display picture planes and failed clauses. |

Certificate detail is coalesced when its decision and geometry are unchanged,
with a periodic heartbeat. Final proof detail is limited to four records per
second per renderer and includes diagnosed-rejection/suppression counts. These
sampled details must not be counted as if they enumerate every output. The
existing per-source composition/canvas/submitted-presentation records provide
that temporal sequence. Pixel witnesses occur only for a verification that
actually ran and failed; context/window expiration cannot inherit an old witness.

The pixel witness includes both endpoints of a failing destination contrast
comparison. An unchanged point may fail because its neighbor changed. Category
flags distinguish immutable ownership/core topology from current black/ink
classification. Zone flags distinguish glyph capture, content, cleanup, measured
backing, destination probe, and encoded-bar coordinates. Changes above a subtitle
in moving movie padding are not automatically changes to the subtitle itself.

These are failure-triggered **pixel samples**, not an automatic full native-frame
pair recorder. The offline report distinguishes identity/size matching from pixel-hash verification. Full native-frame availability remains bounded by the manual
capture and is stated explicitly in the offline report. No additional full-frame
owners, video queue slots, source scans, or synchronous disk writes are added to
the playback path by these diagnostic records.

## Use the evidence for a focused regression

First identify a same-cue on/off/on sequence, its exact current/held extraction
bounds, final rejection gate, and worker baseline. Preserve valid fallback cases
separately, especially player UI outside the erasure region. Measure native
picture boundaries independently of fitting and the physical screen.

Replay actual native samples through the real producer/tracker/compositor/proof
chain. Label sparse-source replay identities and modeled GPU results honestly.
Build the matching native library and headers together; class-layout or inline
body mixtures from different builds are not valid evidence. A useful regression
must fail on the old contract and preserve real current glyph pixels, including
antialias rows, without accepting new unowned text or picture. Do not make a
containment test pass by declaring nonempty rows to be black padding.

Keep failure records, coverage gaps, and cold-start behavior alongside passing
steady intervals. Tests of diagnostics must show that rejection decisions stay
unchanged, masks distinguish unevaluated gates, and missing/expired work cannot
produce a fabricated pixel witness. Optional diagnostic overload must drop
samples under existing bounds rather than delay current video.


## Consecutive input-frame bursts

Use **Capture consecutive input-frame burst** when the question depends on
acquisition, holding and rejection on adjacent input frames. The existing sampled
capture remains available and unchanged. Both retain the twenty-second decision
trace and metadata pre-roll. Neither has a native-pixel pre-roll: arm the burst
before the cue appears.

The burst uses the same two-owner nonblocking writer queue and 2 GiB byte limit.
It selects every source in a fixed initial source window, rather than spreading
samples over twenty seconds. At the observed 3840x2160 V210 stride this permits
97 frames, about four seconds at 23.976 Hz. Disk/queue contention can still miss
frames; a miss never stalls playback or extends the source window to conceal the
hole. Context changes terminate the burst rather than join unrelated sources.
Inspect the capture summary and full identity sequence before calling it a
consecutive reproduction. A partial burst is evidence with gaps, not a passing
replay fixture.

These files contain the exact prepared analysis-source bytes and their original
format/range/domain metadata. That is native V210 in this incident; other capture
formats may use a converted analysis domain. Do not infer original HDMI values
from a desktop MP4: output may already be toned, resized, composed and compressed.
The recording supplies visible transitions/timecodes; these input files supply
pixel and temporal evidence.

### Background control

`tools/request_analysis_capture.ps1` addresses one explicitly selected VP process
and verifies its executable path. `-Action Status` is read-only; `-Action Records`,
`SampledFrames` or `ConsecutiveBurst` starts the corresponding bounded capture.
For example, use `-TargetProcessId 12345 -Action ConsecutiveBurst`, replacing the
example PID with the running VP process. The default expected executable is
`C:\Videoprocessor\vp\VideoProcessor.exe`; an alternate build requires an explicit
`-ExpectedExecutable` path.

The versioned local `VideoProcessor.AnalysisCapture.v1` request does not send keys,
move the mouse, activate a window, restart playback or change profiles. Unsupported
builds and failed requests report an error without showing a dialog. `AlreadyRecording`
does not change the active capture mode or start another capture. Read `Status`
until both `recording` and `captureWriting` are false before requesting a new one.
The runtime log records the acknowledged request and exact output directory.

Protocol: LPARAM is version 1; WPARAM is records=0, sampled=1, burst=2, status=3.
Replies have tag `0x56500000`, low-byte capture-result code (0..5 from the recorder,
6 for status), and flags in bits 8..13 for recording, writing, producer connected,
frame support, writer ready and include-frames. Unknown versions/commands fail
closed. The command script probes the endpoint on windows owned by the selected
process; no window title or currently focused application is used as authority.
