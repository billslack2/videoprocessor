# VP-0159 profile-list inventory and decisions

## Review record

The code inventory and component boundary were reviewed before the production
refactor on 2026-09-06 by an architecture reviewer and a senior UI reviewer.
The first implementation was rejected because NLS and Standard retained copied
list handlers and a protected, synthetic `Default` row. The approved corrective
boundary is `ProfileListController`: it owns list lifecycle and ordering, while
each page supplies a persistence/naming adapter and its domain-specific detail
editor.

The configuration namespaces remain unchanged. `[shader.nls]`,
`[shader.nls.<name>]`, `[shader.standard]`, and
`[shader.standard.<name>]` adapt into the shared runtime model; they are not
migrated to `[profiles.*]`. This preserves file compatibility and keeps shader
payloads non-inheriting. Actions remain a separate command domain and do not use
profile active-state concepts.

## Implementation inventory

| Surface | UI construction | Backing model and persistence | Runtime/live path | Primary regression coverage | Classification |
|---|---|---|---|---|---|
| Queue | `createProfilePage("Queue", "queue")` | ordered `ConfigDocument` sections; `RendererProfileConfig` queue group; `.state` manual selection | `UnifiedProfileRuntime` then application queue contract | Config editor lifecycle/round-trip; runtime queue selection/restart tests | true profile; shared code |
| Rendering | `createProfilePage("Rendering", "vprenderer")` | ordered root/child sections; renderer-setting inheritance | runtime snapshot, then renderer application state | editor lifecycle, inheritance, active marker, live apply | true profile; shared code |
| Scaling | `createProfilePage("Scaling", "vprenderer.scaling")` | ordered root/child sections with inherited renderer settings | runtime snapshot then renderer application state | full editor round-trip and runtime profile tests | true profile; shared code |
| Color Config | `createProfilePage("Color Config", "vprenderer.color")` | ordered root/child sections with color settings | runtime snapshot, renderer application, actions | migration, inheritance, active marker, action tests | true profile; shared code |
| Output | `createProfilePage("Output", "vprenderer.output")` | ordered root/child sections with output settings | runtime snapshot then renderer application state | output preset, inheritance and live-effect tests | true profile; shared code |
| Screen Config | `createProfilePage("Screen", "vprenderer.viewport")` | ordered root/child sections; label plus geometry; inherited settings | runtime viewport resolution and OSD/status | screen/zoom migration, active marker, OSD tests | true profile; shared code |
| Zoom | `createProfilePage("Zoom", "vprenderer.zoom")` | ordered root/child sections; label plus crop/subtitle settings | runtime viewport/zoom resolution and OSD/status | screen/zoom migration and runtime tests | true profile; shared code |
| NLS | `createNlsShadersPage` plus `ProfileListController` | ordered `shader.nls` sections adapted to runtime group `nls`; no inheritance | exclusive runtime selection; typed shader-profile renderer boundary | shared controller; NLS add/remove/drag/round-trip; VP-0159 runtime and Off tests | duplicated behavior normalized through adapter |
| Standard shaders | `createStandardShadersPage` plus `ProfileListController` | ordered `shader.standard` sections adapted to `standard_shaders`; no inheritance | composable runtime selection in file order; typed renderer boundary | shared controller; Standard add/remove/reorder; VP-0159 multi-selection tests | duplicated behavior normalized through adapter |
| Actions | `createActionsPage` | `actions.*` command definitions, enabled drafts, event selection | `EventActionLauncher`; never a runtime profile | action draft, round-trip, validation and scheduling tests | visually similar, deliberately separate |
| Input Processing | `createInputProcessingPage` | singleton backend policy sections | renderer input-policy application | input migration and selector refresh tests | not a list; retain |
| LLDV metadata | `createLldvPage` | singleton metadata section | resolved LLDV snapshot | LLDV migration/runtime tests | not a list; retain |
| Shader Setup, Shortcuts, Logs | dedicated non-profile pages | singleton settings/commands | dedicated paths | page-level editor tests | not profile lists; retain |

## Deviation and retention matrix

| Behavior | Ordinary profiles | NLS | Standard shaders | Actions | Decision |
|---|---|---|---|---|---|
| Create/delete | shared component; any configured row may be removed | same component | same component | action-specific handler | normalize profiles/shaders; retain Actions |
| Duplicate | not currently exposed | not exposed | not exposed | not exposed | retain; no duplicated implementation exists |
| Rename/list label | immediate list preview; ordinary profile identifier commits when editing finishes | immediate `label` persistence; stable shader section ID | same as NLS | action name/ID semantics | retain domain adapters; normalize immediate feedback |
| Ordering | drag and Move buttons; physical section order persists | same; determines default, rule priority, and cycle order | same; determines default and composition order | no default/priority ordering contract | normalize profiles/shaders; retain Actions |
| Enabled/draft state | profiles are configured by existence | no separate enabled flag | no separate enabled flag | explicit enabled draft | retain |
| Default/root | first configured row is visibly marked Default; root is not protected or synthetic | first row is the no-match fallback but has no hard-coded name/badge | same as NLS | none | retain ordinary marker; normalize shaders |
| Off | not applicable | selected profile with both shader files blank is Off | all selected profiles without files is Off | not applicable | normalize shader meaning; never reserve a name/ID |
| Inheritance | retained for ordinary renderer profile families | never | never | not applicable | retain explicit domain difference |
| Edit vs active selection | list selection edits only; active marker is independent | same | same, including multiple active markers | no active-profile marker | normalize where applicable |
| Active cardinality | one per group | exactly one | zero or more composed selections | not applicable | retain explicit runtime policy |
| Shortcut behavior | selecting a profile replaces the group | replaces NLS selection; cycle supported | same key selects all matching Standard profiles and replaces the complete set | action triggers are unrelated | retain/normalize as approved |
| Rule precedence | source rules select over configured/persisted defaults; live manual shortcut remains a session override | same | same | event condition controls command launch | normalize runtime profile policy |
| Validation/errors | page/domain fields validate through existing schemas | shader and rule validation; one NLS active | shader/rule validation; multi composition | command/event validation | retain adapters |
| Empty state | no synthetic profile | no synthetic `Default` or `Off` row | no synthetic `Default` or `Off` row | action empty-state copy | normalize |
| Keyboard/focus | Qt list/button conventions and shared button state | same | same | retained action behavior | normalize profiles/shaders |
| Unsaved changes | shared mutations call the window dirty contract | same | same | existing action dirty contract | normalize |
| Persistence | physical order plus optional durable manual selection | same global persistence; non-inheriting payload | same; multi-selection serialized only at state boundary | config file only | normalize profile runtime; retain Actions |
| Live apply/status | unified snapshot and generation-safe active marker | typed renderer selection; file-derived Off OSD | typed composed selection; multi-row status; file-derived Off OSD | scheduled action status only | retain explicit domain contracts |

## Component contract

`ProfileListController` receives capabilities as callbacks rather than checking
page names. It owns refresh/selection, Add, Remove confirmation, Move Up/Down,
drag persistence, optional first-row role decoration, empty state, action
enablement, immediate visible-name updates, dirty notification, and
active-indicator refresh. Ordinary profiles retain their `Default` role marker;
shader adapters disable it so a profile named `Off` is displayed simply as
`Off`.

Ordinary profile pages, NLS, and Standard instantiate this same component.
Their adapters alone decide how sections are enumerated, created, removed,
normalized when a root changes position, displayed, and physically reordered.
Shader detail fields and ordinary renderer detail fields remain separate because
their settings and inheritance contracts are intentionally different.

## Accepted runtime decisions

- Shader profiles never inherit shader files or parameters.
- A profile name or identifier `Off` has no special meaning.
- NLS has one effective member; Standard may have multiple effective members.
- The no-match Standard fallback is the first single profile. Multiple Standard
  profiles compose when they share a matching rule or shortcut; configuration
  does not imply that every Standard profile is active by default.
- A Standard shortcut shared by multiple profiles replaces the complete current
  Standard selection with those profiles in configured order.
- Manual shader selection follows the existing durable profile-selection
  setting. Matching rules override configured or restored defaults; a live
  shortcut remains the explicit session override used by other profile groups.
- The OSD displays Off only when the effective shader selection contains no
  HLSL or GLSL file, including the transition caused by deleting the last
  configured shader profile.
- The application-to-renderer boundary carries a typed vector. Delimited
  selector text is confined to saved-state and legacy renderer compatibility.
