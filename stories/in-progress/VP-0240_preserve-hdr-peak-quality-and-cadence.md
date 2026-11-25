# VP-0240 Preserve HDR tone mapping while reducing peak analysis cost

## Status

In Progress

Source branch `trial/vp0240-libplacebo-histogram` in `E:\codex\videoprocessor\vp0240-hdr-histogram-20261125` starts at verified beta `d5f69645f4c818559d627d68eced177489c159d5`. Building the opt-in original histogram reference and full-rate delayed control first. Astra verified that delayed libplacebo on D3D11 can still force readback. True half cadence requires retaining native state and polling readback without waiting. No live evidence yet.

## Context

- The 2.0 beta compact HDR path replaces libplacebo peak detection for eligible inputs. Its sparse grid and cadence change the peak estimate, and disabling the detector can bypass profile tuning, mastering-peak soft clipping, and measured Dolby Vision metadata fill-in.
- The user accepts one frame of analysis lag. Trial a full-pixel histogram at half source cadence above 30 fps, then test a quality GPU downscale to half width and height with advanced subtitles if more headroom is needed. A two-frame pipeline can prepare the analysis texture on one frame and measure it on the next.

## Work

1. Get an Astra principal-engineer review of scheduling, alternatives, state reuse, HDR reduction quality, and the live frame budget. Record the decision and alternatives.
2. Build an opt-in x64 Release comparison trial from the current official beta. Keep the configured libplacebo histogram, tuning, processing region, soft clipping and Dolby Vision fill-in where applicable.
3. Compare full-rate, half-rate, and staged half-resolution analysis on matched HDMI inputs with moving highlights, cuts, fades, HDR10, HLG, Dolby Vision and dense advanced subtitles.

## Acceptance

- No optional HDR work blocks current-source video. Measure the worst scale-frame and histogram-frame critical paths, source presentation gaps and result age.
- On a verified cut discard incompatible pending work and prioritize the new scene. Keep analysis bound to the source, format and target context.
- Provide an isolated test build, exact source identity and rollback guidance. Record visual and timing gaps before recommending a release default.
