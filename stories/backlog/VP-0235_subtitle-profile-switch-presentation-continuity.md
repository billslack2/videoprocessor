# VP-0235: Preserve presentation geometry while switching subtitle profiles

## Status

Backlog. Requested 2026-11-18; explicitly deferred by the user and excluded from current VP-0230 AR work.

## Problem and evidence

In the c841a9bf Eternals session, Ctrl+Shift+T selects Reconstruction subtitles at 13:58:14 (profile generation 10). Source 18176 submits the full 3840x2160 raster while measured and published physical AR correctly remain scope 0,276–3840,1884. Scope presentation returns at source 18181, about five source intervals / 204 ms later. The preceding resize had already resumed correct scope. This is a presentation handoff issue, not a false full-raster detection.

Evidence: E:\codex\videoprocessor\ar-extent-test-c841a9bf\logs\vp.log and analysis-20261118-135822-62244-1. The pre-roll includes this event. Full-raster presentation has full_authority=0 and owner=full-raster; native-contract-rejected is the downstream anchor rejection, not the established initiating cause. See VP-0230 transition review recorded 2026-11-18.

## Future work

Trace the exact profile-change branch that temporarily loses eligible presentation geometry. Preserve current-source video progression and visible picture/captions while switching profiles. Do not hold/replay video, invent caption absence or crop authority, overwrite settings, or conflate physical detection with presentation fallback.

Validate switches between Off, Classic and Reconstruction with active/changing captions and without captions, including viewport/format changes. Compare actual submitted crops, current source identities and presentation gaps. Use successfully completed x64 Release builds and preserve user configuration. Discover the latest remote beta before source implementation; the incident pin is a reproduction reference, not the integration baseline.

## Scope separation

VP-0230 owns promptly applying well-supported measured picture boundaries during real AR changes. VP-0234 owns critical-path performance and 60 fps qualification. This backlog story owns subtitle-profile switching only; no implementation is authorized as part of the current AR increment.
