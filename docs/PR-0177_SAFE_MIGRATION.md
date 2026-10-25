# PR #177: lossless defaults and profile migration

This test integration starts at `v1.4.02-beta` commit `8c4537462bbabd6ce07ba3e9ffd9be9bc8745492`, which contains #192 through #202. It replays #177 head `2a0f4763ca60de87cef7d1d8f5e4c9e29060580c` and adapts chart availability to the promoted VP Renderer. It does not merge #177 into beta.

## Ownership

Processing overrides remain on their original Rendering profile. Migration never creates independently selected Scaling copies, copies selectors, or splits a cycle group. Existing independent Scaling profiles retain their order, values and selection. Rendering overrides remain editable, can be cleared back to inherited settings, and survive save/reopen, rename, duplicate, export and import. Export/duplicate also carries inherited Rendering processing overrides and the effective legacy preset fallback; it omits live shortcut assignments.

## Defaults and compatibility

New configurations receive explicit High defaults:

| Setting | Value |
| --- | --- |
| Upscaler | ewa_lanczossharp |
| Downscaler | hermite |
| Sigmoid | on |
| Debanding | default |
| Dithering | blue_noise |
| Dither depth | output (Match output format) |
| Peak detection | high_quality |
| Contrast recovery | 0.3 |

An explicit Auto reset in a High configuration becomes its concrete default on the same owner. Missing named Scaling settings keep ordinary inheritance. Output matching follows the actual output representation; fixed 8/10 caps remain supported.

Legacy Fast/Balanced presets are fallback policies below explicit settings, not unconditional Rendering overrides. Their effect can change with an independently selected Scaling profile. The original PR's unconditional removal cannot express that behavior losslessly. The corrected migration therefore preserves legacy preset inputs, Auto resets and legacy debanding dependencies. The Quality selector is removed from Config, while compatibility parsing and the original native preset projection remain. Invalid legacy presets still fail validation. No setting silently falls back because a copied shortcut was unavailable.

The runtime and editor share one migration plan. Loading never writes the source file. Explicit Save/Apply creates a migration backup and preserves comments and unrelated content. Tone controls retain the independent-profile behavior introduced by #177, while processing overrides and compatibility fallbacks retain their existing inheritance.

The charts use the same legacy colour-map preset fallback as the renderer. A High comparison remains independent of the edited preset. A Rendering-only preview reports unavailable when a legacy Scaling preset makes its defaults depend on an independent selection.

## GUI corrections

Default Rendering processing controls retain an empty/inherit option. Resetting a fallback knee chooses an interior value when the automatic value lands on a forbidden bound. Section resets are labelled Restore visible defaults and preserve hidden settings.

## Left docking Config workspace

Settings navigation and profile selection remain on the left. Each panel has its own panel-left collapse/expand icon and a narrow rail. The settings rail retains page actions; the profile rail opens the original ordered list and management controls in a temporary drawer. Selection closes that drawer, Escape dismisses it, and Pin docks it. The selected profile remains visible above the editor. All profile families, Standard/NLS shaders and Actions share this docking behavior.

Opening Advanced tone mapping reveals the chart pane, expands and scrolls to the tone controls, and collapses profiles and Settings navigation once. Closing it returns to the prior editor scroll position. Restore panels reopens only panels collapsed automatically; manual panel choices take priority. Closing advanced mode restores those automatic changes. Settings and charts appear side by side where they fit and stack at narrow widths. Narrow width handling may temporarily turn a requested dock into a drawer to protect the editor, while retaining the requested expansion for when space returns. Neither navigation nor profiles are replaced by dropdowns, and the editor/charts are not replaced by compact view tabs. Reflow uses the original widgets and preserves edits and selection. Chart comparison, scene, display and zone controls wrap; long names retain tooltips and accessible labels.
The migration notice appears for pending tone-profile migration, summarizes the change in one sentence, and puts affected profile names in its tooltip. A successful Save/Apply hides it; loading the already migrated file does not repeat it. Failed saves retain the notice. Opening the editor still does not write the configuration.

## Validation

The original defaults/migration integration was validated as follows:

- Relevant native configuration, migration, selection, runtime, documentation and render-parameter suite: 246 passed.
- Focused Config checks: 13 passed, including real menu duplicate/export/import, save/reopen/rename, clearing overrides, legacy presets, promotion/cache selection, Display controls, Apply/OK/Cancel, profile lifecycle and tone resets.
- Real shipped libplacebo provider: 216 tone and 648 colour requests, plus five PQ round trips.
- Full x64 Release solution build and exact binary hash evidence are retained with the test deployment receipt.

The left-docking follow-up passes 23 focused Config checks, including drawer management, manual panel choices and automatic focus on tone controls. The resize/profile-management test visits all 23 pages from 760x540 through 1920x1080 at 100%, 150% and 200% Qt scaling with the real chart provider. It checks retained unsaved edits and selection, long comparison names, reachable chart bottoms, zero outer Rendering-page vertical scrolling, profile duplication and unchanged source configuration bytes. Migration acknowledgement, keyboard/accessibility, no-target startup and expanded shader scrolling also pass. Screenshots were inspected at compact and wide sizes. A pre-existing no-target test's mojibake ellipsis expectation was corrected to match the production Unicode string; the initial failing result is retained in local evidence. The native and chart-math baseline above was not rerun for this UI-only follow-up.

These automated checks do not establish physical HDMI playback or visual acceptance. PR #177 remains unmerged; this is a local test integration.
