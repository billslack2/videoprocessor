# VP-0207: Overlay fit stability and subtitle backing boundaries

## Status
Review

Implementation on `codex/subtitle-overlay-stability-20261023`, starting at current beta `dfd34ea6f6365eddb17d986914bb7792836d694b`, in `E:\codex\videoprocessor\subtitle-overlay-stability-20261023`.

## Report
Paused subtitle toggle causes repeated scope/full-raster flashing; playback scrub controls should remain visible in a stable zoomed-out presentation. Subtitle backing lips remain intermittently visible. Screenshot supplied 2026-10-23.

## Evidence and work
Deployed log identifies host 4d1d62d0. Current logs repeatedly reject finite backing with `lip-top-closure-occluded`, `lip-outline-boundary-occluded`, and `lip-black-span-does-not-cover-glyphs`. Shared vertical policy explicitly lets a one-edge translate replace held FIT.

## Acceptance
- Hold a zoomed-out FIT across changing overlay classification for the existing bounded interval; allow immediate outward growth and expire normally.
- Verify finite subtitle backing using glyph-bounded width and a bounded collar above its edge, without extending cleanup into scene shadows.
- Preserve frame/cue ownership, sample budget, native picture transitions and existing cleanup exclusions.
- Pass x64 Release regression tests. Live validation remains required before claiming the reported playback incident resolved.

## Implementation and validation — 2026-10-23

- Draft PR: https://github.com/billslack2/videoprocessor/pull/195
- Source: `5ab30a23f7e463ab520d65b1e6a5e71d87437ebb` (preceded by implementation commit `bcb631297a7b75d7e4f51f4e04757d0fd43cbc80`).
- Clean x64 Release solution Rebuild succeeded with the official installed Magewell SDK. Both generated host/renderer identities report the final source commit and `dirty=false`.
- Full native suite: **3,153 passed, zero failures**, 13.4128 minutes. Report: `E:\codex\videoprocessor\subtitle-overlay-stability-20261023\TestResults\overlay-backing-clean-full.trx`; build/test logs in that worktree are `build-clean-release.log` and `test-clean-full.log`.
- Displayed pending translation/FIT envelopes are remembered only after successful submission with matching existing native admission/context; no new native aspect authority is granted.
- Held zoomed-out FIT survives fresh one-edge classification, grows immediately for new outward occupancy, and uses the existing bounded release and extent expiry.
- Backing closure can be corroborated in a bounded picture collar (up to half a glyph height, capped at 32 native pixels). The glyph-bounded side search, exhaustive black body, ownership and sample budget remain enforced. Rounded corners above glyphs may narrow inside the lower text's projection. Scene collar pixels never enter cleanup.
- Native regression coverage includes P010/P210/decoded DV/RGB, both bars, attached shadows, rounded corners, invalid ownership/context, budget exhaustion, failed/late submissions and bounded overlay release. Existing actual GPU cleanup/layer tests passed.

## Remaining review

Not merged or deployed. Active runtime and configuration were not modified by this task. Replay the user's paused subtitle toggle and playback time-bar interaction after an authorized deployment; live incident resolution is not yet claimed.
