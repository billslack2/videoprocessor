# VP-0103: Apply saved configuration safely to a running VideoProcessor

## Status

Review (2026-08-09). The original vertical-alignment implementation is
integrated and remains subject to its outstanding live Alpha subtitle
validation. The expanded live-configuration editor implementation now stages
validated configuration changes, applies the required restart/reset/save-only
action, and keeps the configuration shortcut as a one-way reveal command.
While visible, the editor is a Qt-owned topmost operator surface so it remains
above the VP video host and other desktop windows. The complete x64 Release
solution build passed before review.

This is deliberately a conservative first live-configuration contract. It is
not a file watcher and it does not attempt arbitrary per-key hot reload. An
editor save is the only configuration-application entry point in this story.

## Implementation checkpoint (2026-08-08)

- Added per-viewport `vertical_alignment` with finite `top`, `center`, and
  `bottom` values. Omission defaults to `center`, and invalid values fail with
  a setting-specific validation error.
- Alpha places the final fitted picture at the selected resting edge by
  redistributing only unused vertical destination space. Source crop,
  horizontal centering, picture scale, active-picture authority, anamorphic
  mapping, NLS mapping, and existing HDMI subtitle source-pixel translation
  remain independent.
- The resolved value is published with viewport state and participates in
  live renderer updates, state equality, renderer fingerprints, OSD, and
  final-layout diagnostics. DirectShow/madVR explicitly reports non-center
  placement as unsupported instead of applying a translation shader.
- Both the modern Qt editor and legacy Win32 editor expose **Vertical picture
  alignment** with **Top**, **Center**, and **Bottom**, default new viewports to
  **Center**, explain temporary subtitle-fit movement, and preserve the value
  across save/reload. The canonical configuration reference, sample config,
  and public field/value inventories are updated.
- Feature source commit `a40d992` was pushed on
  `codex/vp-0103-vertical-alignment` and fast-forwarded into
  `v1.2.001-beta`. Because the formats beta has intentionally divergent
  history, that branch received only the verified VP-0103 change as commit
  `90600f1`, preserving its formats and live-editor work.
- Full x64 Release solution builds succeeded on the feature/default result and
  the formats result. The VP-0103 geometry and configuration/publication tests,
  checked-in configuration validation, and all 14 Qt editor tests pass on the
  formats result.
- Full native regression on the default feature result passed 684 of 689 tests;
  its untouched default-beta baseline passed 682 of 687 with the same five
  failures. The formats result passed 722 of 727 with those same five existing
  configuration-suite failures:
  `ConfigurationReferenceMatchesPublicFieldInventory`,
  `Vp0097NamedViewportsUseFileOrderAndIgnoreLabels`,
  `Vp0079OwnerVariantsResolveWithoutPersistedProfileState`,
  `ConfigEditorCoreRoundTripsEveryEditorOwnedKey`, and
  `ConfigEditorCoreValidatesEveryEditableOrderedProfileSurface`.

## Expanded user story

As a VideoProcessor user editing the active configuration while video is
playing, I want **Apply** to save valid changes and make their effect clear:
VP either performs the necessary renderer restart, performs a delayed
queue-only reset, or records the change for the next process start. I want a
renderer restart to reload the complete saved configuration safely, including
keyboard shortcuts, so it never resumes with a mixture of old and new state.

The original story remains in scope:

As a user displaying scope or other wide content on a taller screen, I want
each viewport profile to place the fitted picture at the top, center, or bottom
of the visible screen so I can use one-sided physical masking, while the
existing HDMI subtitle-fit behavior can still shift the whole picture in
either direction when subtitle pixels appear in or across an encoded black
bar.

## Why an explicit reload transaction is required

Today a renderer restart tears down and recreates the renderer from the
already-loaded dialog/session values. It does not reload the main configuration
or rebuild the accelerator table. Renderer-specific Alpha settings are read
during renderer construction, but that is not a validated whole-configuration
reload. Thus merely calling the existing restart path would give misleading
partial behavior and can leave shortcuts stale.

The implementation must stage a candidate configuration through the same
production parsing and aggregate validation used at process start. It must
construct a replacement shortcut/accelerator table and all selected-profile
state before it changes a live binding, capture setting, renderer, or queue.
On any load, parse, validation, discovery, or accelerator-conflict failure,
retain the complete current runtime state and show the error; never leave VP
without its previous shortcut table.

On a successful renderer restart, atomically publish the staged configuration
at the controlled UI-thread restart boundary, then construct the new renderer
from that one coherent snapshot. The new keyboard table must replace the old
one only after it has been created successfully. The first frame after restart
must therefore observe either all old state or all new state, never a mix.

## Configuration effect classes

The editor must classify the *effective difference* between the running
snapshot and the validated saved candidate. The classification is visible in
the unsaved-change summary and is recomputed at Apply time.

| Edited area | Apply result while rendering | Notes |
| --- | --- | --- |
| Startup / Hardware / Input processing / General behavior | renderer restart | Includes capture device, input connection, renderer selection, conversion, HDR/container policies, scene detection, and display/timing policies. A capture-device or input change may require the existing controlled capture stop/start as part of the restart transaction. |
| DirectShow settings | renderer restart | Includes timing, offsets, queue delivery policy, and DirectShow sample metadata overrides. These values are construction/graph settings; do not try to mutate a live graph. |
| Queue profile and queue capacity settings | delayed reset | Apply the validated queue policy only through the existing serialized reset/re-prime path. Coalesce repeated Apply requests, wait for an existing reset/transition to finish, and do not rebuild the renderer merely to alter a queue. |
| LLDV metadata/profiles and LLDV policy | renderer restart | Do not assume that a live `OnVideoState` update is sufficient for every renderer. The existing LLDV live path may still be used for capture-driven metadata changes, but an editor configuration change uses the conservative restart contract. |
| Shader definitions, shader profile/rule selection, and shader shortcuts | renderer restart | Rebuild shader resolution and renderer state from the new file; do not preserve old compiled or selected shader state. |
| Viewport profiles, including `vertical_alignment` and subtitle-fit settings | renderer restart | The existing hotkey/profile path remains available for a runtime selection, but saved-file Apply uses the simple restart contract in this story. |
| Logging settings | save only | Persist to the configuration, but do not reopen/reconfigure logging during playback. It takes effect on next process start. |
| Keyboard shortcuts | save only | Persist them for next process start. They are also loaded as part of any renderer-restart configuration reload, but **Apply** with shortcut-only changes must not restart or replace the live accelerator table. |
| Unknown, unsupported, or editor-preserved content | save only, with a clear notice | Preserve text exactly. Do not infer a runtime action from an unknown key. |

If a candidate contains more than one class, perform the strongest required
action once: renderer restart wins over delayed reset, and either wins over
save-only. A save with no effective runtime difference must not interrupt
playback.

## Apply, OK, and Cancel contract

Use the familiar madVR-style button names and order: **OK**, **Cancel**,
**Apply**. Their semantic contract for VP is:

- **OK**: validate, save, perform the classified action, and close the editor
  only after the save succeeds and the action has been accepted/scheduled.
- **Cancel**: discard only the editor's unsaved working copy and close. It
  never changes the file or running VP.
- **Apply**: validate, save, and perform the classified action while leaving
  the editor open. It becomes enabled only when the working copy differs from
  the last successfully saved version.

This mirrors the common Windows/madVR meanings: OK commits and closes, Cancel
abandons pending edits, and Apply commits without closing. It does not claim
that every Apply is live/no-interruption; VP's effect summary must say
**Restart renderer**, **Reset queues**, or **Takes effect next start** before
the user commits.

Saving retains VP-0097's backup, external-modification detection, full-file
validation, rollback, and exact unknown-text preservation guarantees.

## Restart reload contract

Every intentional renderer restart, whether requested by Apply, the renderer
Restart command, a renderer selection, or VP's existing automatic renderer
restart paths, must first attempt a complete reload of the active config file.
This includes automatic EOTF/LLDV recovery restarts. A failed reload must
abort the pending configuration replacement, retain the last-known-good
runtime snapshot and shortcuts, report the exact reason, and continue the
already requested restart using that last-known-good snapshot when safe.

The reload transaction must:

1. Read one stable file snapshot (with bounded retry if an external editor is
   replacing it), parse it, and run normal startup-equivalent validation.
2. Resolve all profile/rule selections against current runtime facts without
   changing live state.
3. Build a complete replacement accelerator table, detecting duplicate and
   invalid bindings before touching the current table.
4. Classify differences against the current accepted snapshot and publish the
   replacement only at a UI-thread lifecycle boundary.
5. Tear down and reconstruct the renderer/capture graph only after the new
   snapshot is accepted; keep DirectShow delivery/reset serialization and
   current epoch guarantees intact.
6. Emit one concise diagnostic containing config identity/hash, action,
   affected categories, reload result, and fallback reason if the last-known-
   good snapshot was retained.

This requirement is intentionally stricter than reloading only
`vprenderer.ini`: the main configuration, renderer configuration, profile
rules, and keyboard table must be a coherent generation.

## Context

VideoProcessor receives the program and subtitles together as HDMI video
pixels. It does not own a subtitle track or separately render subtitle glyphs.
Subtitles may be entirely inside the detected active picture, entirely in an
encoded top or bottom bar, or cross a detected picture boundary.

The current Alpha `subtitle_fit` behavior can move the complete presentation
to keep required subtitle pixels visible. This story adds a configurable
*resting* vertical placement. It must generalize the existing displacement
behavior rather than add a second subtitle detector or assume subtitles occur
only below the picture.

The primary use case is scope content on a 16:9 screen with one-sided masking:

- `top` places all otherwise unused vertical screen space below the picture;
- `center` preserves the current equal-bar presentation; and
- `bottom` places all otherwise unused vertical screen space above the
  picture.

On a CIH screen where the fitted presentation already consumes the available
height, the alignment selection has no resting-position effect. Existing
subtitle-fit displacement must continue to operate within the space available.

## Required geometry contract

Vertical alignment is destination placement, not source cropping. It must not
translate, contract, or invent a source crop and must not acquire active-picture
authority.

For the generation-current presentation envelope and physical screen:

1. Select the trusted source presentation envelope under the existing crop and
   bounded bar-content rules.
2. Apply the existing aspect-preserving, anamorphic, or approved NLS mapping to
   determine the final picture size.
3. Place that picture at its configured resting alignment within the physical
   screen rectangle.
4. Apply the existing subtitle-fit policy relative to that resting rectangle.
   Required top pixels may shift the whole presentation downward and required
   bottom pixels may shift it upward.
5. Use the minimum displacement that keeps the required presentation envelope
   visible, clamp the result to the physical screen, retain the existing hold
   behavior, and return to the configured resting alignment after release.

The three resting positions for a fitted picture rectangle of height `H`
inside screen rectangle `[screenTop, screenBottom)` are:

```text
top:    y = screenTop
center: y = screenTop + (screenHeight - H) / 2
bottom: y = screenBottom - H
```

Existing integer rounding and chroma-alignment policy owns the exact pixel
boundary. No mode may change picture scale merely to obtain a different
resting position. When no vertical slack exists, all three modes resolve to
the same rectangle.

## Subtitle-fit interaction

- Subtitle evidence remains encoded-video evidence from the shared/current
  analysis path; do not introduce OCR or a renderer-generated subtitle model.
- A subtitle wholly inside the active picture requires no additional shift.
- A lower-bar or lower-boundary subtitle may move a top-, center-, or
  bottom-aligned presentation upward by the minimum required amount.
- A top-bar or top-boundary subtitle may move any resting alignment downward by
  the minimum required amount.
- Alternating top and bottom subtitles must not inherit a stale displacement,
  oscillate between unrelated candidates, or manufacture an opposite source
  edge.
- Existing subtitle padding, hold, release, generation invalidation, and
  fail-safe behavior remain authoritative.
- If current required pixels cannot all fit inside the screen without an
  unsupported crop or scale change, preserve the current fail-safe behavior
  and report the constrained result; never silently discard required pixels.

## Configuration contract

Add a finite per-viewport setting:

```text
[profiles.viewport.scope_on_16x9]
screen_aspect: 16:9
vertical_alignment: top
subtitle_fit: true
subtitle_hold_seconds: 2
subtitle_padding_pixels: 30
```

- Accepted values are `top`, `center`, and `bottom`, case-insensitively if that
  matches the shared enum parser convention.
- The default is `center`, preserving every existing configuration and current
  presentation result.
- Reject unsupported values with a section/key-specific validation error.
- Resolve the value into the same coherent viewport snapshot as
  `screen_aspect`, `subtitle_fit`, subtitle hold, padding, anamorphic scale, and
  viewport generation.
- Runtime hotkey/profile selection continues to use its existing live path.
  Editing and applying the saved file follows this story's conservative
  renderer-restart classification instead of adding a second partial reload
  route.
- Profile names remain labels and must not imply an alignment.
- Document the setting in the canonical configuration reference and generated
  field inventory.

## Configuration editor requirements

- Expose **Vertical picture alignment** on each viewport detail page as a
  three-value selector: **Top**, **Center**, and **Bottom**.
- Default newly created viewports to **Center**.
- Explain that this controls the picture's resting position within unused
  vertical screen space and that enabled subtitle fitting may temporarily move
  it away from that edge.
- Load, edit, validate, save, and reopen all three values without changing
  unrelated configuration text or values.
- Preserve the existing safe-save, external-change detection, validation, and
  rollback guarantees owned by VP-0097.
- Provide a persistent effect summary adjacent to the bottom buttons. It must
  name the strongest pending action and list the category/key groups that
  caused it. It must not label a renderer restart as a live update.
- Disable **Apply** after a successful save until another edit is made.
- If VP is absent, Apply and OK save normally and report **Takes effect when
  VideoProcessor next starts**; they must never try to launch VP.
- If VP is running but cannot accept an action, retain the saved config,
  explain that it will take effect on its next eligible restart/process start,
  and keep the editor responsive.

## Queue reset requirements

A queue-only Apply must not reconstruct capture or the renderer graph. It
must enqueue a single reset request with the validated policy snapshot and
apply it at the existing safe boundary. The queue transaction must preserve
the DirectShow epoch, flush/re-prime, delivery serialization, bounded capacity,
and liveness protections established by VP-0066 and VP-0084. The UI must show
**Reset queues** while it is pending and the result when it completes.

Do not use this lightweight path for a mixed queue-plus-restart edit: one
controlled renderer restart is the only action in that case.

## Renderer boundary

Alpha owns its final libplacebo viewport and is the required implementation
target for this story.

VP does not currently have a documented madVR control that can move madVR's
completed presentation without pixel remapping or a renderer restart. This
story must not implement alignment or subtitle displacement through a madVR
translation shader. When madVR is active, retain its independently configured
placement and make the unsupported VP alignment state clear in diagnostics or
UI. VP-0087 remains the blocked story for VP-managed subtitle-fit placement
with madVR.

## Diagnostics and OSD

Expose enough state to distinguish the resting policy from a temporary
subtitle displacement:

- selected viewport and `vertical_alignment`;
- physical screen rectangle;
- fitted resting picture rectangle;
- current subtitle-fit direction and displacement in pixels;
- final presented rectangle; and
- unsupported-renderer or constrained-placement reason when applicable.

Routine logs should report transitions rather than emit the same placement on
every frame. Existing OSD geometry must follow the final presented picture
rectangle where its current contract requires picture-relative placement.

For saved-configuration application, provide diagnostics that let support
distinguish: saved-file validation failure; external-conflict rejection;
save-only success; queued reset accepted/completed/rejected; reload staged;
shortcut-table staged/replaced/retained; renderer restart beginning/completed;
and last-known-good fallback. Do not log shortcut text or an entire
configuration file at normal verbosity.

## Verification

Add deterministic geometry and configuration tests covering:

1. Top, center, and bottom placement of 2.35 and 2.40 content on 16:9 screens.
2. Center-default compatibility for configurations with no new key.
3. Equal-height/CIH cases where all alignments produce the same resting
   rectangle.
4. Content narrower than the screen, where there is no vertical slack and the
   setting must not introduce vertical movement.
5. Bottom-bar, bottom-boundary, top-bar, and top-boundary subtitle evidence
   from every resting alignment.
6. Minimum bidirectional displacement, screen clamping, subtitle padding,
   hold/release, and return to the selected resting edge.
7. Alternating top/bottom evidence, scene changes, viewport changes, renderer
   epochs, resolution changes, and evidence withdrawal without stale offsets.
8. Automatic crop on and off, linear fit, anamorphic profiles, and supported
   NLS mappings without source-pixel loss or an unintended scale change.
9. Parser acceptance/rejection, resolved viewport publication, configuration
   round trips, editor defaults, and preservation of unrelated configuration.
10. Clear non-application under madVR with no generated translation shader.
11. Every editor-owned field maps to exactly one documented effect class; a
    multi-category edit selects the strongest action once.
12. Invalid configuration, transient partial-file replacement, invalid profile
    resolution, and accelerator collision preserve the complete old running
    snapshot and shortcut table.
13. Renderer Restart reloads main and renderer configuration and publishes
    every changed restart-class value to the newly built renderer.
14. A shortcut-only or logging-only Apply writes the file with no renderer
    restart, reset, accelerator replacement, graph interruption, or changed
    live shortcut behavior; the values load on the next restart/start.
15. Queue-only Apply coalesces requests and completes a reset/re-prime without
    renderer reconstruction, stale delivery, or an unbounded queue.
16. Automatic EOTF/LLDV restart, manual renderer restart, and an Apply-driven
    restart all use the same staged reload path and retain last-known-good
    state on reload failure.
17. OK/Cancel/Apply button enablement and semantics: Apply remains open and
    disables after success; OK commits then closes; Cancel cannot save or
    trigger a runtime action.

Complete the native tests, focused configuration-editor tests, and a clean x64
Release solution build. Live Alpha validation must include a scope program on
a 16:9 screen with one-sided masking behavior and real HDMI subtitles at the
bottom, across a lower boundary, at the top, and across an upper boundary.

## Acceptance criteria

- A viewport can select top, center, or bottom resting alignment in both the
  configuration file and configuration editor.
- Omitting the setting exactly preserves centered behavior.
- Top and bottom placement redistribute only unused vertical destination
  space; they do not crop, rescale, stretch, or alter source authority.
- Existing subtitle fitting can temporarily shift the complete presentation
  upward or downward from any resting alignment and returns to that alignment
  after the configured hold/release behavior.
- Required HDMI subtitle pixels wholly in a bar or crossing either picture
  boundary remain visible whenever the existing screen-fit contract can
  contain them.
- Viewport hotkey/profile changes update alignment coherently with aspect and
  subtitle settings without stale placement.
- Unsupported madVR placement is explicit and does not use a translation
  shader or silently claim the Alpha behavior.
- Apply reports and performs the documented strongest action exactly once;
  save-only changes do not interrupt playback.
- Every renderer restart rebuilds from one validated configuration generation.
  A failed reload cannot alter the active shortcut table or leave VP with
  mixed old/new state.
- Queue-only changes reset safely without rebuilding the renderer, while
  restart-class changes never attempt unsafe live mutation.
- The editor exposes **OK**, **Cancel**, **Apply** in that order with the
  defined commit/close/discard behavior and an honest effect summary.
- Automated tests, the clean x64 Release build, and live Alpha validation pass.

## Dependencies and related work

- VP-0038 owns generic viewport state, configuration, and restart-free
  selection.
- VP-0080 owns fail-safe Alpha crop authority and subtitle evidence boundaries.
- VP-0098 owns the trusted presentation envelope and arbitrary-screen fit.
- VP-0097 owns the standalone configuration editor and safe round-trip edits.
- VP-0066 and VP-0084 own DirectShow reset/re-prime and bounded live-queue
  safety; this story consumes rather than changes their protocol.
- VP-0087 remains blocked for equivalent VP-managed placement under madVR.
- VP-0070 owns future subtitle capture/relocation and is not required here.

## Non-goals

- OCR, glyph extraction, subtitle replacement, or separate subtitle rendering.
- Moving physical masks, curtains, a projector lens, or screen hardware.
- Adding horizontal alignment or arbitrary pixel offsets.
- Reconfiguring madVR or implementing placement with a translation shader.
- Changing active-picture detection thresholds or granting subtitle evidence
  crop authority.
- A general file-system watcher, automatic application of arbitrary external
  edits during playback, or live mutation of every setting.
- Reconfiguring the logger or replacing keyboard bindings for a save-only
  Apply.
