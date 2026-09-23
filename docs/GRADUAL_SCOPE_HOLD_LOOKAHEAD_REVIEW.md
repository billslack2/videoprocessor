# Gradual scope hold and lookahead review

## Requested behavior

Keep the established scope crop during a positively identified gradual outward format transition, then change framing when the new format settles. This deliberately crops newly revealed edge pixels temporarily and avoids prematurely shrinking the picture into four-sided padding. Abrupt cuts should retain their existing fast handling. The separate caption-return fix must remain intact.

This document records the source audit and the implemented presentation constraints. Independent policy review found no remaining blocker. Exact-scene visual validation remains necessary; synthetic pixel tests cannot substitute for the reported HDMI playback.

## Actual frame budget

`vprenderer/AlphaQueuePolicy.h` selects the current frame plus at most `min(configured lookahead, available future source frames, 8)` future frames. Cadence repeats do not provide additional evidence. Selection does not change queue depth or wait for more frames. A queue containing five distinct source frames has four future frames, because one frame is current.

The configured setting is wired through configuration/profile application and the renderer setter. Changing its value invalidates previously scheduled decision policy; crossing zero also resets partial preview evidence. Increasing lookahead cannot replace missing physical frames or override insufficient current-frame evidence.

## Audited paths

| Path | Existing use of lookahead | Assessment |
| --- | --- | --- |
| Abrupt outward picture change | Current plus two future frames prove broad opposing picture expansion. | Appropriate; deeper analysis is not required for this certificate. |
| Inward picture change | Simulates available adjacent evidence against a copy of the live transition model, bounded by configured/available depth. | Appropriate; live state is not advanced to a future sequence. |
| Caption return | Shared caption inspection runs for preview and live frames; current pixels and protected bounds are revalidated. | Preserve this separate fix; cache only original extraction evidence. |
| Translation-to-picture handoff | Dedicated queued outward certificate with current ownership and geometry checks. | Appropriate narrow exception. |
| Subtitle FIT ownership | Buffered expansion is inspected diagnostically but does not transfer ownership. | Deliberate safety exclusion. |
| Active gradual edge movement | Queued publication cannot bypass active motion. | Required for stable framing during a ramp. |
| Gradual endpoint | Approximately 250 ms of adjacent quiet evidence is currently evaluated on live frames. | Deliberate limitation described below. |
| Darkness or ambiguous evidence | Future evidence cannot make an unproven current frame safe to crop. | Preserve current safeguards. |
| Startup acquisition | Initial crop cannot be backdated from implicitly assumed full raster. | Appropriate conservative acquisition. |
| Queue drops and context changes | Continuity, source, renderer, viewport, policy, and exact effective-frame identity are checked. | No blocking defect identified in this audit. |
| Native versus converted input | Preview reads native capture samples before renderer conversion; final consumption validates the live analysis evidence. | Lookahead is available with both P010 conversion and the native/P210 rendering path. |

## Settlement optimization boundary

The current moving-state quiet-settlement test is live-only. Lookahead therefore does not remove all endpoint dwell. A later optimization could simulate the moving state and admission rules across queued physical frames and issue an exact-current-frame settlement certificate. It would need independent continuity, current-pixel, owner, and publication validation.

Do not substitute the ordinary three-frame outward certificate for this settlement proof or allow it to bypass active movement. A short quiet portion of a ramp is not necessarily the completed transition. Keep this possible optimization separate from restoring the requested scope hold.

## Source areas reviewed

Paths below are relative to `src/VideoProcessor-Lib`:

- `vprenderer/AlphaQueuePolicy.h`: physical availability and preview selection.
- `vprenderer/LibplaceboVideoRenderer.cpp`: configuration invalidation, native preview inspection, caption inspection, queued proof construction, live ownership/admission, and final runtime identity validation.
- `vprenderer/BufferedPictureExpansion.cpp`: moving-state continuity and settlement; outward and inward certificate construction/validation.
- `ActivePictureDecisionTimeline.cpp`: pending-frame association, consumed/discarded frames, continuity boundaries, and policy generations.
- `ActivePictureTransitionModel.cpp`: live model authority and confirmation cadence.

## Validation to retain

Existing coverage includes configured depths 0/1/2/3/5/8, short queues, repeat exclusion, source/timestamp gaps, policy changes, stale identities, preventing publication before the changed source frame, inward/outward round trips, hard cuts during movement, and native-v210/P010 caption parity.

Run the relevant `AlphaQueuePolicyTests`, `ActivePictureDecisionTimelineTests`, `LookaheadOutwardProofTests`, `MovingPictureTransitionTests`, and `InwardCaptionEvidenceTests` with the new gradual-hold regressions. New tests should explicitly require retaining established scope during proved movement, settling once, preserving hard-cut handling, and not bypassing current caption protection or context-reset safeguards.

## Review outcome

The correction retains presentation only after the existing motion detector has confirmed the episode and the crop was previously admitted. It preserves exact geometry, configured fill and NLS mapping, while recovery and explicit safety owners retain precedence. The ordinary lookahead/caption certificates and adoption rules were not broadened. Review checked the renderer path as well as the pure crop-policy boundary.

The regression baseline produced seven expected assertion failures before implementation. The new endpoint fixture initially demanded literal zero-height bars; source measurements showed the unchanged detector already reports `FULL_RASTER_TRUSTED` with eight pixels remaining. Its corrected invariant is to retain scope until affirmative full-raster authority, then remain full frame. The fill assertion compares the original displayed bounds, including configured horizontal trimming. Neither correction changes production thresholds or disables fill.

Focused validation: clean x64 Release test-target rebuild succeeded; all 69 tests passed across the movement, handoff, buffered proof and inward-caption suites. Full-solution build and full-suite results are recorded separately in artifacts/gradual-hold-validation.json after completion. No deployment or configuration changes are included.
