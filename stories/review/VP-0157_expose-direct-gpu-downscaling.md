# VP-0157: Expose direct GPU sampling for VP Renderer downscaling

## Status

Review (2026-08-27). Implemented from the developer-confirmed current default
integration branch `v1.3.001-beta` tip `bdcb60cf` on
`codex/vp-0157-gpu-downscaler` at source commit `b66c8632`. Draft
[PR #72](https://github.com/billslack2/videoprocessor/pull/72) targets
`v1.3.001-beta`.

The Downscaler selector now includes **Use GPU** and persists the new canonical
`downscaler: gpu` value. The renderer accepts that value through root and named
live scaling profiles and projects it to a null libplacebo downscaler, selecting
the built-in direct sampling path. Legacy `none` and `ewa_lanczos` downscaler
values continue migrating to Auto. Configuration help records the lack of
anti-aliasing and possible aliasing/shimmer trade-off.

Validation evidence:

- Clean x64 Release rebuild of the complete solution passed from committed
  source `b66c8632`, including GUI, Config, VP Renderer, native tests, and
  configuration tests.
- Focused native scaler/profile tests passed 2/2 from the clean build outputs.
- The focused Config renderer-profile UI/persistence test passed from the clean
  build output and proves the **Use GPU** label plus `downscaler: gpu` save.
- The full native suite remained at the beta baseline 940/941. The only failure
  was the pre-existing `ConfigurationReferenceMatchesPublicFieldInventory`
  mismatch already recorded on this beta line.
- A complete Config test run passed before the clean rebuild. The clean-output
  rerun had one unrelated native popup-association timing failure; that same
  test passed immediately when retried alone.

Remaining review: confirm the new Downscaler choice is clear in Config and,
optionally, compare direct GPU downscaling against a filtered choice on live
content. No deployment was performed.

## User story

As a VP Renderer operator, I want the Downscaler selector to offer **Use GPU**
just as the Upscaler selector does, so I can explicitly choose the video
card's built-in sampling path instead of a libplacebo reconstruction filter.

## Technical contract

Libplacebo documents that a null `pl_render_params.downscaler` selects its
inexpensive built-in bilinear or nearest-neighbour sampling path. Unlike a
null upscaler, a null downscaler also disables anti-aliasing because the
built-in GPU sampling path cannot anti-alias.

Use a new unambiguous persisted value such as `gpu` for this selection.
Do not restore the removed legacy downscaler value `none`: it formerly meant
"same as upscaler" and must continue migrating to Auto rather than silently
changing old configuration behavior.

## Acceptance criteria

1. Every VP Renderer scaling-profile Downscaler selector includes **Use GPU**
   in the same explicit-choice position used by the Upscaler selector.
2. Selecting **Use GPU** persists the canonical downscaler GPU-bypass value
   and resolves `pl_render_params.downscaler` to null.
3. The setting applies through the existing live renderer-profile update path
   without restarting the renderer or disturbing unrelated profile fields.
4. **Use default** and **Auto** retain their existing inheritance and quality-
   preset behavior; they are not aliases for the new explicit choice.
5. Legacy downscaler `none` and removed `ewa_lanczos` values retain their
   existing migration to Auto.
6. Configuration help explains that direct GPU downscaling uses built-in
   bilinear/nearest sampling and disables anti-aliasing, so it may alias or
   shimmer more than a reconstruction filter.
7. Parser, projection, UI ordering/label, persistence/migration, live apply,
   and configuration-reference tests cover the new value.
8. Focused tests and a clean x64 Release build pass.

## Boundaries

- Do not change libplacebo's Auto quality presets or filter definitions.
- Do not relabel filtered GPU shader scalers as CPU operations; **Use GPU**
  specifically denotes libplacebo's built-in direct sampling path.
- Do not reinterpret existing persisted `downscaler: none` configurations.

## Evidence

- Libplacebo `renderer.h` documents that null upscaler/downscaler pointers use
  inexpensive built-in sampling and that a null downscaler implies
  `skip_anti_aliasing`.
- Source commit `b66c8632` and draft PR #72 contain the implementation and
  automated coverage.
