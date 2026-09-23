# Moving picture edges: implementation and regression review

The September 20 session showed a scope-to-16:9 expansion publishing intermediate crop rectangles roughly every 10-13 frames. The current three-sample outward proof establishes real picture, but does not establish that its edges have stopped moving. Each accepted rectangle resets the stable-geometry deadband, producing a visible staircase. The second occurrence also included a profile epoch change; that separate reset behavior is unchanged.

## Policy

The new tracker recognizes sustained, full-width opposing vertical expansion using the existing exact current-frame strip evidence. After the first qualified sample, at least three monotonic changes and cumulative movement on both edges greater than two detector scan steps are required. Quantized top/bottom edges may advance on alternating samples. A single hard jump followed by stationary or bounded jitter samples cannot establish movement.

Once motion is established, local and queued publication are deferred. Presentation now retains the exact previously admitted scope crop through motion and the awaiting-publication phase. This deliberately clips newly revealed edge pixels temporarily, matching the requested steady-framing behavior instead of shrinking to full raster. The current source, sequence, viewport, exact base and vertical-only authority must match. The hold cannot acquire an unpresented crop or override an existing recovery obligation, near-black owner, explicit full-raster authority, or competing dense presentation. Subtitle inspection stays suppressed so newly revealed picture cannot become a competing FIT owner. Existing aspect-limit fill and NLS continue using the finally admitted held geometry.

A trusted intermediate endpoint needs a quarter-second of adjacent, qualified observations inside an anchored scan-step band before the ordinary acquisition path resumes. The tracker does not assume the endpoint is 16:9. Large hard changes and affirmative full-raster observations leave motion immediately and use existing proof timing. A separate presentation hold remains until that ordinary publication completes, so the local confirmation frames retain the same established framing. Existing near-black, subtitle, crop admission, and recovery safeguards remain in force. Motion does not create another general recovery dwell, and cannot erase a pre-existing recovery obligation.

Distinct source/capture identity is required. Repeats and stale replays cannot advance quiet proof. A gap clears temporal proof but preserves the established presentation hold in the same context. Source, format, raster, viewport, renderer, and scene resets discard the episode through the existing reset paths. Ambiguous observations cannot declare a settled endpoint or acquire a crop that was never presented.

## Regression coverage

- Actual 4K P010 picture ramps with 2px and 4px edge increments exercise current and buffered proof, retention remeasurement, recovery, final crop admission, and aspect-limit fill. They stop at an intermediate format (~2.0:1), and include feedback from dense bar FIT detection.
- A disabled-motion control runs the same pixels through the prior path and requires multiple intermediate publications and actual final rectangle changes.
- Buffered hard-cut controls compare final presentation before, during, and after motion; existing local hard-change fixtures run with and without a contiguous movement prelude and require their original source-frame publication timing.
- Movement is injected into existing clean-bar measurement-jitter, subtitle ownership, quarter-second recovery at 23.976/24/59.94/60 Hz, and Alien near-black recovery scenarios. Their original exact recovery/publication assertions remain.
- Dedicated tests cover 1080p/2160p, fractional/integer frame rates, alternating quantized edges, reversals, pauses, intermediate endpoints, provisional endpoints, stale/repeated frames, capture gaps, context changes, and missing/contradictory evidence.

Two independent expert reviewers examined policy and regression coverage. Review findings about competing dense FIT, alternating edge updates, capture gaps, and stale replay were addressed with code and test changes.

The first complete regression run caught a real handoff issue in the newly injected existing test: ending motion before local confirmation could briefly restore the old scope crop. This led to the separate awaiting-publication presentation hold and pixel tests for local hard cuts and local settlement. Hard-cut model timing remains unchanged. The no-lookahead settlement test then caught reuse of the completed ramp's movement count, which could re-enter motion on stationary frames. Quiet exit now resets that evidence; the test explicitly forbids reactivation while ordinary local confirmation finishes.

## Scope and limitations

This is deliberately limited to established, strip-certified opposing vertical expansion. It is not a general smoothing filter for arbitrary crop motion. Motion recognition has a short evidence-gathering period; it does not retroactively change already displayed frames. Pauses longer than the settling interval are treated as settled endpoints. The hard-step discriminator is 1% of raster height (minimum 4px) per observed sample.

The saved log establishes the observed staircase, but no source video was captured. Synthetic tests and independent review support the change; they cannot guarantee behavior for all content. Live replay of the reported scene remains the final visual check. No deployment or configuration change is part of this work.

Baseline: fresh origin/v1.3.005-beta at 525767f6, then fast-forward integration of the existing deployed fixes through 75651be5. Worktree: E:\codex\videoprocessor\moving-picture-transition.


## Historical validation of the original motion tracker

- x64 Release solution clean rebuild: succeeded, zero errors. Build log: `moving-final-local-settlement-rebuild.log`.
- Focused injected fixtures: 34 passed, zero failed (`TestResults/moving-injected-fixtures-verified.trx`).
- Full native suite: 1,374 passed, zero failed, zero skipped, 67 seconds (`TestResults/moving-picture-final-full.trx`). This includes all 1,355 original test methods and 19 new methods; selected original methods also run with injected movement.
- Both independent reviewers report no remaining blocking findings after the final corrections.
- `git diff --check` passed. No deployment, active configuration, or story-state changes.
- The test artifacts were built from the final source changes in this worktree. A later deployment must rebuild from the committed source with clean version identity; these development binaries are not release packages.

## September 22 presentation correction

The earlier full-raster hold was intentional but produced the unwanted early shrink reported during the Mandalorian transition. The detector and quiet-settlement rules are unchanged; the correction is at final presentation arbitration. `MOVING_PICTURE_HOLD` makes this choice visible in existing crop diagnostics. The separate inward-caption-return helper and its live/queued eligibility are unchanged.

Baseline: current remote beta `6785d0b1`, fast-forwarded through the tested caption build `c9755e84`. Source worktree: `E:\codex\videoprocessor\gradual-scope-hold`. See `GRADUAL_SCOPE_HOLD_LOOKAHEAD_REVIEW.md` for the frame-budget audit and deliberate exclusions.

Seven selected tests were first run against the previous production policy and failed on assertions, including actual P010 motion through detection, recovery, final admission and aspect-limit fill. They establish that the previous policy withdraws the displayed crop. Additional tests cover a full-raster endpoint, reversal during an established motion episode, stale hold identity, conflicting owners and missing prior admission. These are synthetic source-pixel tests, not recordings of the exact Mandalorian scene.

Recognition still requires sustained outward motion. This change does not promise to eliminate steps before motion is recognized or add an independent gradual inward-transition detector. A positive full-raster observation still uses the existing immediate exit. Live-only endpoint quiet proof remains approximately 250 ms; replacing it with queued settlement certification is separate work.
