# VP Renderer presentation controls

Config's Display page has two independent global VP Renderer controls:

- **Presentation queue depth**: `[general] presentation_queue_depth`, integer
  1 through 8, default 2. Bounds pending device presentations, including repeated
  pictures. Higher values can absorb short stalls but add latency. This does not
  change the source FIFO's Queue profile, target, capacity or lookahead.
- **Present on every display refresh**: `[general] present_every_vsync`, Boolean,
  default false. Repeats final processed output between new due pictures without
  enabling Smooth Motion or changing the speed of valid source timestamps.

Apply restarts VP Renderer. Both settings are shared across Display profiles.
DirectShow/madVR retain their own presentation policy. Absent keys preserve the
old device-latency request of two and ordinary source-paced presentation.

The D3D11 device limit is set and read back before swapchain creation. Normal
flip chains allocate that limit plus one buffers, matching bundled libplacebo;
diagnostic bitblt uses one. `VP presentation queue` records the requested and
applied device limit. Neither the limit nor buffer count reports queue occupancy.
A rejected limit fails initialization rather than silently claiming it applied.

Every-refresh mode uses interval-1 DXGI presentation and its bounded queue for
backpressure. There is no independent sleep timer. Several calls can fill the
queue before a call blocks: this is not a per-vblank callback. Valid, stable DXGI
statistics can discipline the next queued-slot estimate using mpv's guarded
queue-tail calculation. Missing, zero, disjoint or implausible statistics are
rejected; FIFO pacing continues without claiming physical timing confirmation.

Source PTS chooses pictures; a separate output-slot estimate controls repeats.
For nominal 24 fps at 120 Hz there are five slots per picture; 24 at 60 Hz keeps
3:2 cadence. Clock drift and late sources can retire due source frames while
preserving the configured source-queue reserve. Elapsed slots are not repaid with
a catch-up burst. Frozen/backward/implausible timestamps use a monotonic
sequence/rate estimate anchored once per source generation; raw capture ticks
remain available in telemetry. If neither timestamp nor rate can be estimated,
repeat pacing falls back to the ordinary source path rather than starving it.

Final output is copied to owned GPU storage, independent of capture/backbuffers.
Repeats preserve pixels, overlays and dithering without repeating conversion,
tone mapping or source/subtitle analysis. Smooth Motion still chooses temporal
pixels; deinterlacing keeps its nominal field/frame processing cadence. One slot
clock also repeats final temporal pictures between due fields or missing inputs.
It never enables blending or expands expensive deinterlacing to display Hz.

When Smooth Motion actually engages mixing, it already produces display-rate
outputs and owns cadence. The every-refresh preference stays saved/checked but
its separate repeat clock, cache and source-retirement policy are bypassed.
Selecting Auto alone does not disable repeats: they resume when Auto bypasses
mixing. Deinterlace-only output also remains eligible. Presentation queue depth
continues to apply independently in both cases. The log names the active cadence
owner and requested/effective every-refresh states.

Resets, resize/output changes, pending profile/shader/LUT changes and visibility
changes retire the cached repeat path. Already submitted frames may drain from
the bounded driver queue. During a source stall, an output change can stop new
repeat submissions until a new source can be processed; the displayed image can
remain held by scanout. Queued output can cover a short future-render stall only
within the available queue lead; no finite depth covers arbitrary rendering stalls.

`VP display refresh` logs repeat/source-retirement counts, feedback acceptance
and timestamp-fallback state. Successful Present and software slot cadence are
not proof of HDMI scanout or source-to-display latency. See
[the source comparison and qualification report](VP-0211_PRESENTATION_REVIEW.md).


Source eligibility includes a small timestamp precision allowance: the larger
of one capture-clock tick and 10 microseconds, capped at 100 microseconds.
The 10-microsecond minimum covers the observed DeckLink hardware PTS grid even
when its API timebase is 1 MHz. This prevents rounding at a refresh boundary
from inserting a cached repeat followed by discarding the adjacent source.
It preserves cadence phase and genuine rate drift; it is not a half-refresh
advance or a video speed adjustment. The same allowance applies to source
selection, stale-source retirement and nonmixing temporal output checks.
Repeat diagnostics include `next_pts_lead_ms`, `pts_tolerance_us` and
`queued_sources`. These CPU submission records do not prove HDMI scanout.
