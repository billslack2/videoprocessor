# VP-0157: Expose direct GPU sampling for VP Renderer downscaling

## Status

Backlog (2026-08-27). Created after fetching canonical `origin/main` and
auditing 175 story files against 175 index rows. No duplicate or missing IDs
were found. The audit found one unrelated pre-existing state mismatch:
`VP-0154` is stored under `review/` while its status and index row say Done;
this story does not alter it.

GitHub reports `v1.3.001-beta` as the current default integration branch.
Implementation is waiting only for the required developer confirmation of
that base.

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

