# VP-0121: Make configuration help UI-first with current screenshots

## Status

In progress (2026-08-17). `CONFIGURATION.html` is a comprehensive public-field
reference, but it still teaches configuration primarily as hand-edited text.
The standalone configuration editor now exposes the supported workflow in a
safer, discoverable UI and should become the primary path presented to users.

The expanded deliverable requested at implementation start uses one editable
Markdown source and publishes both offline HTML and PDF editions. Content and
screenshots are being produced by separate senior documentation roles and will
receive an independent senior documentation review before acceptance.

## User story

As a VideoProcessor operator, I want the shipped configuration help to show me
where settings live and how to complete common tasks in the current Config UI,
so I can configure VP confidently without first learning the configuration-file
schema, while still having an exact field reference when I need it.

## Documentation strategy

Keep one canonical Markdown source, published as `CONFIGURATION.html` and a
matching PDF, with two complementary layers:

1. **UI guide:** task-oriented explanations and current screenshots for normal
   operators.
2. **Field reference:** the existing stable anchors, syntax, defaults, ranges,
   inheritance, and manual-editing details for advanced use and compatibility.

Screenshots provide orientation, not the authoritative value contract. Text and
field anchors must remain complete and searchable so a later UI label or layout
change cannot silently erase configuration semantics.

## Scope

1. Add a short **Open Config and understand changes** introduction covering the
   VP settings button/shortcut, tray-resident behavior, active-profile markers,
   pending changes, validation messages, **Apply**, **OK**, **Cancel**, renderer
   restart indications, and next-start-only settings.
2. Add an annotated UI map for the current navigation hierarchy:
   - General hardware, display, startup behavior, and input processing;
   - Queue profiles and advanced queue values;
   - LLDV profiles;
   - Shaders/NLS modes and active shader indication;
   - Actions, events, conditions, variables, and command lines;
   - Shortcuts;
   - Logs;
   - VP Renderer rendering profiles and Screen Config/viewport profiles;
   - DirectShow settings.
3. Write concise, task-based walkthroughs for the workflows users most often
   need:
   - select a capture device/input, renderer, and monitor;
   - choose 16:9 versus scope/CIH presentation;
   - configure automatic crop and subtitle placement;
   - configure an anamorphic lens ratio;
   - choose a queue profile;
   - enable refresh-rate switching and verify that it actually applied;
   - configure SDR/HDR/LUT behavior without mixing source and output concepts;
   - select NLS/shaders and understand which entries are active;
   - add a shortcut, rule, or event action;
   - find and collect logs for troubleshooting.
4. For every task, distinguish:
   - what the setting changes;
   - whether it applies live, restarts a renderer, or waits for next launch;
   - how the UI reports pending/effective state;
   - how to verify the result in the operator UI, OSD, or logs;
   - the related stable field-reference anchor.
5. Capture current screenshots from a clean x64 Release editor build using a
   deterministic documentation configuration. Cover the key pages and expanded
   sections needed for the walkthroughs without duplicating nearly identical
   images for every individual field.
6. Use consistent capture presentation: a readable application window size,
   100% Windows scaling unless a screenshot specifically explains DPI behavior,
   no overlapping windows or transient combo boxes, and a consistent crop and
   image scale in the HTML.
7. Sanitize captured content. Do not publish usernames, local paths, customer
   names, serial numbers, network locations, private commands, real LUT names,
   or hardware identifiers beyond generic/example devices required to explain
   the control.
8. Store documentation images in a clearly owned source asset directory with
   descriptive stable filenames. Ensure Release packaging copies them beside
   the HTML using relative paths that work offline from an extracted package.
9. Add useful alternative text and nearby prose for every image. The guide must
   remain understandable with images disabled and usable with keyboard and
   screen-reader navigation.
10. Refresh the top-level navigation, quick-start content, troubleshooting
    section, and cross-links so users encounter the UI workflow first and can
    jump directly to the exact underlying field when needed.
11. Remove or rewrite instructions that imply manual editing is the normal way
    to change settings now supported by Config. Preserve manual-edit guidance
    for recovery, unsupported legacy fields, automation, and expert workflows.

## Screenshot inventory to produce

- General page with hardware/display and startup/input-processing cards.
- Queue profile list plus expanded Advanced values and unit presentation.
- VP Renderer Rendering page showing profile selection, active marker, basic
  output controls, and refresh switching.
- Screen Config page showing 16:9 and Scope profiles, active marker, screen
  geometry, crop, anamorphic lens compensation, and subtitle controls.
- Shaders page showing Off plus available NLS modes and active selection.
- Actions page showing renderer target, event selection, optional condition,
  command line, and the new-action empty state where useful.
- Shortcuts and Logs pages.
- DirectShow and LLDV pages where their workflows cannot be explained clearly
  by prose alone.
- Footer/state examples for **No pending changes**, validation failure, live
  apply, renderer restart, and next-start behavior. These may be focused crops
  rather than full-window screenshots.

## Acceptance criteria

- A new user can use `CONFIGURATION.html` to select hardware and renderer,
  create 16:9 and scope profiles, configure subtitle/anamorphic behavior, apply
  the changes, and find the verification logs without opening the CFG file.
- Every current top-level Config page is introduced and every common workflow
  above has an explicit click path, effect/apply classification, verification
  step, and field-reference link.
- Screenshots come from the same clean Release build and documentation fixture,
  are legible at the HTML's normal width, contain no private data, have useful
  alt text, and load from the packaged HTML without internet access.
- UI labels, screenshot labels, sample configuration, field inventory, schema,
  and `CONFIGURATION.html` do not contradict one another.
- The complete public-field inventory and stable anchor coverage introduced by
  VP-0049/VP-0086 remain intact; UI-first restructuring does not weaken parser
  validation or advanced manual-edit documentation.
- Documentation tests check every referenced image exists in both source and
  staged Release output, every image has nonempty alt text, internal anchors are
  valid, and the public-field inventory still matches the reference.
- The Markdown source is the editable authority, and generated HTML and PDF
  preserve its content, screenshots, navigation, links, and field-reference
  coverage without requiring hand edits to either published format.
- The HTML is reviewed offline at normal and narrow widths, and screenshots are
  checked at 100% and 150% display scaling for readability rather than clipping.
- A clean x64 Release build packages the updated HTML and image assets, and the
  staged copy is verified against the source documentation.

## Non-goals

- Replacing the configuration editor or redesigning its controls as part of the
  documentation work.
- Removing the raw field reference or making screenshots authoritative for
  defaults and accepted values.
- Embedding videos or requiring an online documentation service.
- Documenting experimental or unimplemented fields as supported UI features.

## Dependencies and references

- VP-0049: complete canonical `CONFIGURATION.html` field reference.
- VP-0086: comprehensive configuration usage reference and validation model.
- VP-0097: safe standalone configuration editor and its structured pages.
- VP-0112/VP-0116: active profile and active shader presentation.
- VP-0113: current Screen Config and unit-field layout.
