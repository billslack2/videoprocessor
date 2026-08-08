# VP-0097 configuration editor UI design contract

## Purpose and scope

This document defines the visual system and interaction rules for the
standalone VideoProcessor configuration editor. It is deliberately a gate for
feature growth: do not add further configuration pages to the current raw
coordinate-based form. First rebuild the shell around reusable layout and
theme primitives, then obtain review of the General & output and Viewports
pages.

The existing C++ configuration parser, schema validation, safe save/backup,
tray behavior, and VideoProcessor launch integration remain the application
logic. This design changes presentation and interaction, not configuration
semantics.

## Why the current prototype is not a foundation

The current prototype demonstrates safe editing, but the screenshots expose
presentation defects that cannot be solved reliably with another round of
pixel changes:

- Controls and their labels are independently positioned. Resizing moves
  fields and group boxes without measuring and moving the associated copy,
  leading to clipping and overlap.
- Stock group boxes, list boxes, buttons, edits, and combo boxes use unrelated
  visual rules beside the branded header. The result looks like a debugger
  form, rather than a VP application.
- The selector controls are currently handed an oversized control height; the
  resulting dropdown client area intrudes into the General & output form.
- The editor is only legacy system-DPI aware. It has neither a Per-Monitor V2
  declaration nor a DPI-change/reflow path.
- Navigation, action hierarchy, validation feedback, and customer-facing
  wording are not yet product-level components.

## Implementation decision

Keep the current native C++ host and configuration logic, but replace direct
form assembly with a small native presentation layer before expanding the
editor. That layer must provide a measured layout tree (stack, grid, flow,
and scrollable content), a central theme, and reusable controls. It must not
be a second set of hard-coded child-control coordinates.

WinUI 3/C++/WinRT is an acceptable alternative if its deployment/runtime story
is approved before implementation. It supplies the required layout,
accessibility, and DPI behavior, but would add a framework and deployment
decision to a small utility. Until that decision is made, the native layer is
the default implementation path.

The minimum shell architecture is:

1. Per-Monitor V2 DPI awareness, DIP-to-pixel conversion, `WM_DPICHANGED`,
   and a remeasure/reflow path.
2. A window shell: branded header, sidebar, page header, scrollable content,
   and a sticky action footer.
3. Primitives for sidebar navigation, card/surface, form field, checkbox,
   selector, text/number input, button, status, inline help, and field error.
4. A page descriptor for every structured setting: display name, help,
   storage key, value type, control kind, validation, default, units, and
   advanced/manual status.

## Visual language

Use VP yellow sparingly as a brand accent; blue is the interaction color.

| Token | Value | Intended use |
| --- | --- | --- |
| Canvas | `#0B121B` | Application background |
| Surface | `#101B27` | Cards, lists, inputs |
| Border | `#273D53` | Card and inactive-control borders |
| Ink | `#F5F9FD` | Titles and primary text |
| Muted text | `#9EB2C5` | Help, paths, metadata |
| Header navy | `#0A1119` | Header background |
| Primary blue | `#1677D2` | Active navigation, focus, primary action |
| VP yellow | `#F4C400` | Logo/key accent only |
| Error | `#C83B3B` | Invalid fields and error messages |
| Success | `#248A4A` | Validate/save status |

Use Segoe UI Variable when present and Segoe UI otherwise:

- Page title: 24/30 semibold
- Card title: 16/22 semibold
- Field label and body: 13/20 regular
- Helper and status text: 12/18 regular
- Button label: 13/20 semibold

## Layout contract

All measurements are DIPs and use a four-DIP rhythm.

- Initial supported logical window: 1040 x 700. Content scrolls before the
  footer or action buttons are allowed to clip.
- Header: 56 high. Show the VP icon and `VideoProcessor`; the configuration
  window does not repeat a `Configuration` chip in its own header.
- Outer padding: 16. Sidebar: 156 wide. Sidebar-to-content gap: 16.
- Cards: 8 radius, 1-DIP border, 14 padding. Do not use group-box caption
  cutouts as cards.
- Fields: label above or clearly paired with its input; 6-DIP label gap,
  4-DIP helper gap, and 16-DIP field gap. Standard input height is 28;
  action buttons are 36.
- Footer: 54 high and sticky, with a top border. Status is left aligned;
  secondary actions precede a visually primary `Save changes` action.
- Verify 100%, 125%, and 150% DPI and keyboard focus at each size.

## Navigation and actions

The sidebar is a navigation component, not a list box. Initial pages are:

- General & output
- Viewports

Future pages can be Rendering, Input & shortcuts, and Advanced. An active
entry has `#EAF3FF` fill, blue text, and a 3-DIP left accent. Hover and focus
states must be distinct and visible.

Footer hierarchy:

`[Saved safely / 3 unsaved changes]                 [Reload] [Validate] [Save changes]`

`Save changes` is the only filled primary action and remains disabled while
the document is clean. Reload and Validate are secondary actions. Errors
identify the field and correction inline; they must not discard user input.

## Form-control policy

Choose the control from the value's domain, not from parser convenience.

| Value kind | UI control | VP-0097 examples |
| --- | --- | --- |
| Finite schema enum | Selector | VP Renderer quality, presentation mode, output range |
| Independent boolean | Checkbox | Fullscreen, crop black bars, subtitle fit |
| Constrained literal | Validated text input | Screen aspect, anamorphic scale, viewport label |
| Numeric literal | Number input plus non-editable unit | Hold/release seconds, padding pixels, queue frames |
| Expression | Text input with syntax-specific help | `when` activation condition |
| Advanced/manual content | Preserve plus clearly labelled manual access | Shader definitions, custom actions, unknown settings |

Aspect ratios, expressions, and numeric values are not fake dropdowns.
Structured text fields have neutral, focused, and invalid states; green chrome
is reserved for validated/saved status, not individual values.

## Viewports page

The page has a selected-viewport list and a detail surface. The first named
viewport in file order is visibly marked as the default/fallback and cannot be
removed. A legacy `[vprenderer.viewport]` root remains the fallback unchanged
until the user explicitly names and migrates it. Named viewports show their
operator-facing label and an activation summary in the list.

`Add viewport` opens a compact dialog for an operator-facing display label
(spaces allowed; a distinct safe identifier is generated), rather than
permanently consuming list space. Space maps to `_`; a literal underscore maps
to `__`; the label itself is retained exactly. A legacy root exposes **Name
legacy** as the only migration action. `Remove
viewport` appears in the selected named viewport's detail header and requires
confirmation.

The condition field is worded for an operator:

> When should VP use this viewport?

Its helper explains that the first named viewport remains the fallback when no
condition matches, gives
`${key} == "F2"` as an example, and places syntax errors directly below the
input. Geometry and subtitle fields use the field policy above.

## Profile-page consistency

Queue, VP Renderer, Viewports, and LLDV are all ordered profile editors. At
the normal supported window size they must use the same side-by-side pattern:
the ordered profile list remains visible on the left while the selected
profile's name, shortcut, optional rule, and settings remain visible on the
right. Do not replace this with a vertically stacked list/detail page for only
one profile domain. At genuinely constrained widths, any fallback layout must
keep both surfaces reachable and must never clip fields or hide a card.

The first profile in file order is always visibly identified as the default.
Every profile page supports the same add, remove, move-up, move-down, inline
name, Shortcut-first, and optional Rule interaction unless the underlying
configuration domain explicitly lacks that operation.

## LLDV detection mode

The current `newlldv` switch is a global input-detection policy and belongs on
Startup's Source analysis and HDR card. It must not be presented as an LLDV
metadata-profile value until runtime profile selection can evaluate LLDV rules
from the raw capture state. Evaluating those rules from synthesized/effective
PQ state would allow a BT.2020/SDR rule to select the new heuristic and then
immediately deselect itself. Future per-profile support therefore requires a
separate raw-state lookup and precedence of explicit process command line,
selected LLDV profile, legacy global setting, then false.

## Review and quality gate

Before adding more setting groups, the designer and UI engineer must approve
static, working General & output and Viewports pages against these criteria:

1. No clipped, overlapped, detached, or raw-config-name text at the supported
   initial window size or 100/125/150% DPI.
2. Both pages use the same sidebar, page header, card, form-field, help,
   footer, and focus-state primitives.
3. The viewport default/base rule and named-viewport lifecycle are obvious.
4. Save is visibly primary; keyboard tab order, focus indicators,
   high-contrast behavior, and accessible names are verified.
5. Existing safe-save, backup, validation, tray, and VP-launch behavior are
   retained.
6. Every profile page is reviewed together at its normal and minimum widths;
   Queue, VP Renderer, Viewports, and LLDV retain the same side-by-side
   list/detail structure and ordering affordances.
