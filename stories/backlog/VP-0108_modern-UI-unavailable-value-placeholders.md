# VP-0108: Make Modern UI unavailable values use the canonical `---` placeholder

## Status

Backlog (2026-08-09). This follow-up to VP-0102 aligns the Modern operator
UI's display of unavailable telemetry with the established dashboard pattern.
The supplied live Modern UI screenshot shows a mixture of `---`, blank-looking
values, and synthetic angle-bracket strings such as `<pixel format>` and
`<color space>` when real data is absent. No implementation branch or worktree
has been selected.

Before source work starts, query the current default branch of
`billslack2/videoprocessor`, report it to the developer, and obtain explicit
confirmation of the implementation base under the tracker workflow.

## User story

As a VideoProcessor operator, I want every unavailable Modern UI telemetry
value to display `---` so I can distinguish missing live data from a real value
or a UI placeholder at a glance.

## Scope

1. Audit every read-only value rendered by the Modern operator UI, including
   capture/input facts, captured-video metadata, HDR luminance, renderer,
   queue, latency/timing, and any compact summary/status rows.
2. Use the literal `---` for each value whose live data is absent, invalid,
   unsupported, not yet sampled, or intentionally not applicable to the
   active capture/renderer mode.
3. Replace synthetic design-time/runtime fallback text used in value positions
   (for example `<pixel format>`, `<color space>`, `<eotf>`, empty strings,
   or whitespace) with `---`. Labels, action captions, explanatory error
   messages, and meaningful lifecycle states such as `Capturing` are not value
   placeholders and must retain their existing semantics.
4. Apply the rule at the shared Modern display-formatting boundary where
   practical, so all cards use the same behavior and new cards cannot regress
   to a different unavailable presentation.
5. Do not fabricate numeric zeroes or stale prior values merely to avoid the
   placeholder. Existing real zero measurements remain displayed as `0` (with
   their current unit/format) when they are valid measurements.

## Acceptance criteria

1. In the Modern UI, every telemetry value lacking a real current value renders
   exactly `---`; no angle-bracket mock value, blank value area, `N/A`, or
   misleading zero remains in a value position.
2. The rule covers the screenshot's Captured video, HDR luminance, Latency,
   and Queue sections as well as all other Modern cards and responsive layouts.
3. Valid values preserve their current text, units, precision, color/status
   treatment, and update cadence. Valid numeric zeroes remain distinguishable
   from unavailable data.
4. Capture/renderer lifecycle labels and actionable failure messages remain
   meaningful; the change does not reduce them to `---`.
5. Focused formatting/unit tests cover missing, empty, invalid, unsupported,
   warming, and valid-zero inputs. A live or controlled no-data screenshot
   verifies the visual result alongside normal capture/renderer data.
6. Classic UI behavior, capture/rendering behavior, configuration, and layout
   geometry are unchanged. An x64 Release build and relevant UI tests pass.

## Dependencies and references

- VP-0102 is complete and established the Modern operator UI.
- User-provided Modern UI screenshot:
  `C:\Users\bslac\AppData\Local\Temp\codex-clipboard-d93cf650-2d12-4e6d-a126-e0e768b989b1.png`.
- The implementation must preserve VP-0102's read-only dashboard and shared
  live-status semantics; this is a presentation-consistency fix only.
