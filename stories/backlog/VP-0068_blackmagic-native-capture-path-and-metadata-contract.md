# VP-0068: Evaluate a native Blackmagic SDK capture path and complete frame metadata contract

## Status

Backlog. This story begins as an architecture and feasibility investigation.
It must first establish what VP already receives from the native Blackmagic
capture API, what currently depends on DirectShow, and whether a second
non-DirectShow path would provide a meaningful latency or metadata benefit.

## User story

As a VP developer evaluating a modern Blackmagic capture integration, I want to
know whether the Blackmagic Desktop Video SDK can feed VP directly without a
DirectShow source/output path, and I want the complete video/audio metadata
contract defined before attempting that integration.

## Question to resolve

Can VP use a current native Blackmagic SDK capture path to deliver frames and
metadata directly to the renderer pipeline, or does the SDK require a
DirectShow path for the information and timing behavior VP needs?

The answer must distinguish the capture API from the downstream renderer. A
native capture adapter may bypass DirectShow while still publishing the same
VP-neutral `VideoFrame`, `VideoState`, timing, audio, and transition contracts.
It must not assume that a new SDK automatically provides complete metadata or
lower latency.

## Metadata and event contract to inventory

The investigation must determine how each item is obtained, represented,
validated, and propagated through VP:

### Video format and timing

- progressive/interlaced mode and field order;
- repeat/telecine flags and field or frame repeat information;
- source and delivered frame resolution;
- frame rate, nominal rate, and individual hardware frame timestamps;
- timestamp clock domain, discontinuities, dropped/missed frames, and
  generation changes;
- chroma subsampling and pixel format, including 8/10/12-bit RGB/YUV cases;
- chroma location;
- range/nominal level interpretation;
- color primaries, matrix, and transfer function; and
- media-type, input-format, signal-lock, and resolution-change events.

### Active picture and geometry

- whether the SDK reports active-picture bounds or only the full raster;
- detection of letterboxed or pillarboxed source images;
- resolution checks required when a source places content inside a larger
  raster;
- relationship between encoded bars, visible picture, subtitle bars, and
  configured viewport/screen profiles; and
- whether geometry evidence can be obtained without converting the full frame.

### HDR and metadata

- HDR10 static metadata, mastering-display data, MaxCLL, and MaxFALL;
- PQ/HLG/SDR transfer identification;
- HDR color-space/container information such as BT.2020 and P3;
- Dolby Vision/LLDV-related signaling or metadata, where exposed;
- AVI/info-frame or signal metadata versus actual captured pixel metadata;
- metadata lifetime, ordering, and frame association; and
- behavior when metadata is missing, delayed, contradictory, or changes
  mid-stream.

### Audio and device events

- audio sample format, channels, channel layout, sample rate, timestamps, and
  clock relationship to video;
- audio discontinuities, buffering, device changes, and resynchronization;
- input lock/unlock, HDMI/SDI mode changes, loss of signal, and recovery;
- device/connector identity and multi-input selection; and
- shutdown, flush, reset, and thread-ownership requirements.

## Required investigation

1. Identify the exact Blackmagic SDK/API currently used by
   `BlackMagicDeckLinkCaptureDevice` and document which portions of VP already
   avoid DirectShow.
2. Compare the SDK’s native frame interfaces, pixel formats, metadata APIs,
   timestamp behavior, audio interfaces, and event callbacks with VP’s
   existing `ACaptureDevice`, `VideoFrame`, `VideoState`, and timing contracts.
3. Determine whether frame buffers can be shared or uploaded efficiently to
   D3D11/libplacebo without an extra copy, and record the limitations of any
   SDK-provided DirectX preview helper versus a true capture-to-texture path.
4. Build a metadata availability matrix by device model, connection type,
   resolution, frame rate, HDR mode, interlace mode, and pixel format.
5. Trace how the current DirectShow path obtains or synthesizes missing
   metadata and identify behavior that must not be lost in a native adapter.
6. Measure capture-to-renderer latency, queueing, timestamp accuracy, format
   changes, and reset behavior before deciding whether a native path is worth
   implementing.

## Proposed architectural boundary

If feasible, add a native Blackmagic capture adapter that publishes the same
neutral VP contracts as other capture devices:

```text
Blackmagic SDK callback
    -> native frame/metadata adapter
    -> VideoFrame + VideoState + audio/timing events
    -> shared VP renderer ingress
```

The adapter must preserve source buffer ownership, avoid blocking the SDK
callback, attach frame timestamps and metadata to the correct generation, and
surface unsupported or unavailable fields explicitly. It must not put
Blackmagic-specific structures into renderer code or silently infer metadata
that the SDK did not provide.

## Acceptance criteria

- A documented decision states whether the current SDK supports a non-
  DirectShow capture path suitable for VP and what benefits it would provide.
- A complete metadata matrix identifies supported, unavailable, synthesized,
  and lossy fields for video, HDR, geometry, audio, timestamps, and events.
- The investigation identifies whether VP already uses a native Blackmagic
  capture path and clearly separates that from the DirectShow renderer path.
- Any proposed adapter preserves interlace/field order, repeat flags, range,
  primaries, matrix, transfer, chroma location, resolution, rate, per-frame
  timestamps, subsampling, active-picture evidence, HDR metadata, audio
  timing, and signal/reset events—or documents an explicit safe fallback.
- A latency comparison proves whether bypassing DirectShow changes capture,
  processing, queue, or presentation latency materially.
- A bounded prototype or spike can ingest representative SDR, HDR, 23.976,
  59.94/60, interlaced, letterboxed, and format-change traces without
  changing the production renderer path.
- Missing or contradictory metadata is logged and cannot silently produce
  incorrect color, timing, geometry, or audio behavior.

## Out of scope

Replacing the current capture path before the feasibility decision; replacing
DirectShow renderers; changing VP’s renderer selection model; adding a new
audio engine; assuming GPU zero-copy without SDK/device evidence; or changing
user-facing defaults.

## Definition of done

The SDK/API decision, metadata matrix, latency findings, proposed ownership
and event model, representative trace results, and any follow-up implementation
or spike stories are recorded. No production capture-path change is required
unless a separate implementation story is approved.
