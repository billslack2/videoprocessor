# VP-0123: Split video conversion policy by renderer

## Status

Done (2026-08-23). Accepted for completion. The implementation from
[`#60`](https://github.com/billslack2/videoprocessor/pull/60) is integrated
into `v1.2.001-beta`; its Release build, deployment, and recorded manual
acceptance complete this story.

During implementation, operator review expanded the original split: General
now remains the shared input-processing default, while DirectShow and VP
Renderer each expose an optional override for video conversion, container
color space, HDR color space, and HDR luminance. Every renderer selector can
inherit and display the effective General value.

## Implementation checkpoint (2026-08-15)

- Implemented and pushed `codex/vp-0123-conversion-policy`, synchronized with
  `origin/v1.2.001-beta` at merge commit `067952a`.
- Added dedicated **Input Processing** pages beneath VP Renderer and DirectShow,
  retaining General as the shared default and storing renderer overrides in
  canonical renderer-owned sections.
- Preserved old and new configuration loading, migrated legacy VP profile-owned
  input values, and kept save operations comment/value preserving.
- Applied the effective policy at renderer startup and restart, including the
  active renderer's V210-to-P010 conversion behavior.
- Completed the requested Config UI polish: inherited effective-value labels,
  aligned two-column cards and shortcut fields, renderer/display organization,
  selector caching, compact navigation focus styling, and sufficient sidebar
  width for renderer child pages.
- Clean x64 Release solution build passed after synchronization. Both focused
  native VP-0123 tests passed. The configuration UI suite passed before the
  final default-branch sync; all VP-0123 checks also passed afterward. One
  popup-focus test remains sensitive to a separately running deployed Config
  process and passed when that process was closed.
- Packaged x64 Release was deployed with the active configuration hash
  preserved and manually accepted before review.

## User story

As a VideoProcessor operator, I want independent video-conversion settings for
DirectShow and VP Renderer, so I can use the best native path for VP Renderer
without changing the compatibility conversion required by madVR or another
DirectShow renderer.

## Scope

1. Replace the shared **General > Input processing > Video conversion** editor
   control with the same conversion-choice control under both renderer owners:
   - **DirectShow**;
   - **VP Renderer**.
2. Keep the menu vocabulary and disabled/default presentation consistent unless
   a backend genuinely supports a different set of choices. Capability-specific
   options must be disabled or rejected for the wrong backend rather than
   silently coerced.
3. Add canonical backend-owned configuration fields. Prefer the established
   owner namespaces (`[directshow]` and `[vprenderer]`) and keep parsing,
   validation, runtime publication, logging, UI binding, examples, and
   `CONFIGURATION.html` aligned with the final field names.
4. Preserve legacy configurations:
   - an existing shared `[general] video_conversion` value initializes both
     backend controls when their independent values are absent;
   - the historical `[directshow] video_conversion` compatibility location
     continues to load safely and, when no independent VP Renderer value exists,
     seeds both backends as the old shared behavior did;
   - an explicit backend value always wins for that backend;
   - conflicting legacy and canonical input follows one documented,
     deterministic precedence rule and emits a useful diagnostic.
5. On the first successful **Apply** or **OK**, save canonical independent values
   for both backends and remove the shared legacy representation. The migration
   must not change the effective settings merely because the file was opened and
   saved.
6. Runtime selection must consult only the active renderer's resolved conversion
   value. Renderer switching/rebuild must publish the correct backend value and
   must not mutate the inactive backend's saved choice.
7. Update the Config UI descriptions so neither page claims conversion is
   shared by every renderer. Show pending/restart behavior consistently with
   other renderer-owned fields.
8. Update the checked-in example configuration, schema/public-field inventory,
   configuration reference, apply/restart classification, discovery/editor
   model, and diagnostics.
9. Preserve shortcut/action behavior that explicitly changes video conversion.
   Define whether such commands target the active backend or both backends; the
   UI and logs must make that ownership unambiguous.

## Acceptance criteria

- DirectShow and VP Renderer expose independent video-conversion controls in
  their respective Config pages, with consistent sizing, labels, and choices.
- A legacy file containing only the current shared value loads with both controls
  showing that value; saving produces two canonical backend values with no
  effective behavior change.
- A legacy file using the historical DirectShow location also migrates without
  surprising VP Renderer behavior.
- A mixed file with one explicit backend value and a legacy shared value applies
  the explicit value only to its backend and uses the shared value only as the
  missing backend's fallback.
- Choosing different values, saving, closing, and reopening preserves both
  independently.
- Starting and switching each renderer proves through logs and effective frame
  format that only its resolved conversion policy is active.
- Empty configuration, omitted values, invalid values, and renderer-unavailable
  startup remain safe and editable.
- Parser, editor round-trip, migration, runtime publication, renderer-switch,
  empty-config, example-config, and public-field-reference tests cover the new
  contract.
- A clean x64 Release build and complete test suite pass, aside from separately
  documented pre-existing failures.

## Non-goals

- Redesigning conversion algorithms or adding new pixel formats as part of the
  settings split.
- Splitting the other General input metadata controls unless their runtime
  ownership is independently shown to require it.
- Removing legacy read compatibility in the same release that introduces the
  independent fields.

## Dependencies and references

- VP-0036: consolidated application and renderer configuration.
- VP-0045: built-in renderer configuration namespace.
- VP-0097: standalone configuration editor and ownership model.
- VP-0107: canonical packaged configuration and release layout.
