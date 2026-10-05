# VP-0196 anamorphic lens and NLS validation

## Geometry and setup
`screen_aspect` is the physical screen/aperture after the lens. `anamorphic_scale`
is its horizontal aspect expansion (4:3 means 1.333333x). Select these in the active
Screen profile. The renderer fits screen/lens into output pixels; ordinary content
fits content/lens inside that rectangle. Active NLS compares physical content with
physical screen, then fills the corrected output rectangle. Both NLS variants use
the existing normalized MAIN hook before libplacebo scaling.

With no explicit screen, physical target is output aspect times lens expansion.
This does not enable policies that require an explicitly configured screen.
Lens 1 preserves ordinary output. No new configuration key is required.
Avoid applying a second digital anamorphic correction in the projector: VP already
supplies the compressed pixel geometry for the physical lens. A regular monitor
will show compressed proportions until the optical expansion is accounted for.

## Diagnostic records
Existing debug logging emits `VP anamorphic layout: schema=1` for initial layout,
changes (maximum four per second), and every ten seconds with an active lens.
The record includes source sequence/generation and viewport serial, input/output
sizes, selected source crop, content aspect, physical and raster screen aspects,
lens factor, screen/picture rectangles, output margins and margins inside the
screen, NLS mode, actual hook attachment, failure latch/retry state, rendering
result, alignment/padding, overlay count and fallback reason. `suppressed` counts
changed frame layouts omitted by throttling. Rectangles are requested floating
pixel coordinates; libplacebo rounds destination edges and adjusts source sampling.

`VP anamorphic hook recovery: schema=1` records the disabled hook signature,
libplacebo error flags, single retry and result. The hook stays unavailable until
shader selection changes. Reset clears only that hook's libplacebo error entry.
If retry itself fails, VP-owned presentation retains its preceding frame; the
libplacebo-owned swapchain must consume its acquired frame and submits a cleared
black frame, logged as `present_policy=black_clear`. Neither presents the failed
NLS image. The older `Alpha final layout` record describes the policy before render;
use the new record plus recovery record for the final submission attempt.

## Initial run and remote reports
Retain startup/version logs plus at least ten seconds around the problem. The
active deployment log is normally `C:\Videoprocessor\vp\logs\vp.log`; verify
its timestamp and include rotated files if playback was restarted. Collect the
active Screen/Zoom/shader profile values, output resolution, lens model/ratio,
projector's digital aspect setting, and a screen photograph. Explain whether the
image is wrong-sized, wrong-shaped, clipped, or surrounded by margins.

A matching 64:27 picture/screen, 4:3 lens and 1920x1080 output should fill the raster.
A 16:9 picture on that setup with NLS off should occupy 1440x1080 with side bars;
active horizontal NLS should fill the raster. Different screen/output combinations
can legitimately leave margins outside the screen, and ordinary content fit can
leave further margins inside it. Four-sided bars alone are not proof of a defect.

## Acceptance
Use the same source pattern with NLS off/on, both shader variants and relevant
fallbacks. Verify projected circles/grid proportions, edge visibility, source
subtitle preservation, overlay proportions, alignment and profile changes.
The automated D3D11 WARP tests exercise the shipped libplacebo DLL and shaders,
coordinate-field readback, fractional source crops, both overlay coordinate modes,
and deliberately failing runtime hooks. Real projector/GPU-driver acceptance is
still required before marking the story Done.
