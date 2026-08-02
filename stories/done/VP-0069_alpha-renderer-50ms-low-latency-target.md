# VP-0069: Achieve and verify a 50 ms low-latency Alpha renderer path

## Status

Done. Accepted 2026-08-02 as the completed VP-0069 native Alpha ingress and
conditional-P010 delivery tranche. The remaining end-to-end 50 ms qualification
and runtime shader-stall work have been deliberately split to VP-0074, a
separate root story; this record must not be read as physical 50 ms
capture-to-photon qualification.

## User story

As a VP user playing latency-sensitive live content, I want the Alpha/libplacebo
renderer path to minimize capture-to-visible-picture latency and provide a
verified approximately 50 ms end-to-end mode on capable hardware, without
depending on DirectShow or madVR.

## Goal

The primary goal is a repeatable low-latency Alpha configuration whose
capture-to-visible-picture result is approximately 50 ms or less on a defined
qualification system with a low-latency display path. This requires a complete
latency budget and physical validation; it cannot be inferred from renderer
queue depth or a single VP timestamp alone.

The story must also identify the best attainable VP contribution when the
capture card, AVR, projector, source, or display makes a 50 ms total impossible.

## Dependencies

This story consumes the VP-0066 pipeline work rather than creating a parallel
timing system:

- VP-0066-3 and VP-0066-4 provide epoch-aware transport, delivery, and
  lifecycle evidence;
- VP-0066-6 provides output-readiness and deterministic prefill boundaries;
- VP-0066-7 defines refresh-measurement invalidation/reacquisition; and
- VP-0066-8 defines the longer DXGI evidence policy and target-path guardrail.

Do not start a production low-latency mode until those dependencies supply
their required timing and transition evidence, or an explicitly bounded spike
documents why an isolated Alpha measurement can proceed safely.

## Latency definition and required measurement

Report latency as separate stages, not one ambiguous number:

```text
source/capture arrival
  -> VP callback and queue admission
  -> conversion/upload completion
  -> GPU render submission
  -> Present / scanout evidence
  -> physically visible display result
```

At minimum record:

- DeckLink hardware timestamp and host callback arrival;
- VP queue wait, conversion, upload, and renderer dequeue times;
- GPU render and Present submission/completion evidence;
- D3D11/swapchain frame-latency behavior;
- the selected display refresh and its readiness generation; and
- physical capture-to-photon latency using a reproducible external method
  (for example a synchronized high-speed camera, photodiode, or equivalent
  validated measurement rig).

Clearly label measurements that stop at VP/Present versus measurements that
include the capture device, AVR, display scanout, and physical display
processing.

## Frame-offset and timing policy

Frame offset must be evaluated as part of the VP-0066 timing model, not treated
as an unexplained global delay. The current offset shifts capture timestamps
forward and may represent a material portion of measured latency.

The low-latency path must:

- determine whether Alpha can safely use a zero offset on each refresh family;
- prohibit negative timestamps unless a separately validated timing design
  proves them safe;
- avoid automatic or per-refresh offset changes silently reintroducing latency;
- log the configured, effective, and automatically selected offset together
  with its stated purpose; and
- preserve existing offset behavior for other renderers unless deliberately
  changed by a separate approved story.

Any replacement for the offset must be derived from current capture/display
evidence and must not become a blind fixed wait.

## Required Alpha-path investigation

1. Baseline the current Alpha path at 23.976 and 59.94/60 Hz with HDR and SDR,
   including the configured frame offset, queue depth, render settings, and
   physical display latency.
2. Verify the actual Alpha FIFO desired depth and hard capacity. Test zero or
   one buffered frame only where the implementation can remain stable; queue
   depth reported in the OSD is not itself a latency measurement.
3. Evaluate D3D11/libplacebo swapchain frame latency, beginning with the
   current setting and testing whether a one-frame maximum is safe. Do not
   assume a lower driver queue always lowers capture-to-photon latency.
4. Establish a low-latency rendering preset: no unnecessary GPU readbacks,
   no frame mixing, no costly optional filters, no shader compilation on the
   live path, and only required tone mapping/color conversion.
5. Measure CPU conversion and GPU upload separately. Keep the existing CPU
   P010 conversion when it is within budget; do not adopt a GPU conversion
   design that adds blocking readback, a hidden queue, or GPU dependency unless
   measurements demonstrate a net end-to-end improvement.
6. Confirm that NLS, active-picture detection, scene detection, subtitle work,
   LUTs, shaders, HDR tone mapping, and OSD each have explicit low-latency
   behavior. Features may be disabled for a named low-latency preset only when
   the OSD/configuration make that tradeoff clear.
7. Validate renderer starts, renderer swaps, refresh switches, HDR/SDR
   transitions, source/channel changes, and resets do not leave stale frames,
   reintroduce a multi-frame backlog, or require a prolonged black screen.

## Qualification configurations

Define at least two configurations:

- **Reference quality:** current safe Alpha behavior, used to detect quality
  and correctness regressions.
- **Low latency:** explicitly bounded queue/presentation/processing choices,
  with its effective frame offset and feature tradeoffs visible in diagnostics.

The low-latency configuration must not alter default behavior without explicit
user selection and must fall back safely if the selected device/display cannot
hold the target without drops or unstable pacing.

## Acceptance criteria

- A documented qualification rig demonstrates capture-to-photon latency of
  approximately 50 ms or less for stable 59.94/60 Hz Alpha playback, provided
  the capture and display hardware are capable of it.
- The same rig has documented 23.976 behavior; where the 41.7 ms source-frame
  period makes the total target impractical, the measured result and limiting
  stage are reported rather than hidden.
- VP-controlled latency is broken down by stage and can be compared against
  the physical end-to-end result.
- The low-latency preset uses no unbounded queue, hidden timer delay, blocking
  GPU readback, or unsupported negative timestamp behavior.
- Frame-offset selection is measured, logged, and compatible with the VP-0066
  timing/readiness model.
- No sustained drops, repeats, queue growth, stale-frame presentation, or
  audio/video regression occurs during the qualified tests.
- SDR, HDR tone mapping, display/profile selection, and refresh switching
  remain correct under the selected low-latency options.
- If a 50 ms total is not feasible on a given chain, logs and documentation
  identify the limiting external stage and the best verified VP-side result.

## Decomposition

The native-ingress child is complete. The unstarted end-to-end latency child
was deliberately superseded by independent root VP-0074 so this root can close
without conflating format delivery with physical latency qualification.

1. **VP-0069-1 — Native-format Alpha ingress and conditional P010 analysis.**
   Establish whether the unconditional full-frame CPU P010 conversion can be
   removed for supported source formats without losing color correctness,
   active-picture/NLS, subtitle/glyph, scene, or low-latency behavior. This
   child must finish before VP-0069 chooses a production low-latency format
   path.

VP-0059 is superseded by this root: its frame-offset requirements are retained
above under **Frame-offset and timing policy**, and are evaluated as part of
the end-to-end qualification rather than as a separate policy project.

## Closing decision

VP-0069-1 is accepted and deployed at source commit `20acbdd`; visual and log
validation confirmed the lossless P210 ingress, explicit P010 fallback, and
stable steady-state Alpha queue. VP-0069-2 is retained as a Will Not Do record
because its work is moved unchanged into standalone VP-0074. The original
50 ms physical target, including NLS shader cold-start recovery, remains open
only in VP-0074.

The accepted source branch was merged into and pushed to
`origin/v1.1.015-beta` as `327ca7f` on 2026-08-02. A clean x64 Release build
and the complete suite (**461/461**) passed from that exact merge commit.

## Out of scope

Guaranteeing 50 ms on arbitrary projectors/AVRs/capture cards; DirectShow or
madVR latency reduction; a new capture driver; replacing libplacebo; frame
interpolation; unmeasured GPU-native conversion; and changing the normal
quality/default rendering profile.

## Definition of done

A stable, opt-in Alpha low-latency configuration is documented and validated
with stage-level and physical latency evidence. Its limitations, hardware
qualification conditions, frame-offset policy, quality tradeoffs, and fallback
behavior are recorded, and any remaining hardware-bound latency is explicitly
separated from VP latency.
