# VideoProcessor plan index

This folder contains self-contained implementation stories and plans. Root
story IDs are assigned monotonically and are never reused; an approved root
story may have ordered child-task IDs as defined below.

## Registry state

- Last assigned root story: `VP-0140`
- Next root story number: `VP-0141`
- Total indexed items: 155

## Story locations

The directory is the canonical state. Every story is in exactly one of these
folders, and its `## Status` heading must use the matching state name.

`INDEX.md` is the sole authoritative workflow instruction for this tracker.
The state-folder `README.md` files are intentionally brief navigation labels;
they must not contain requirements that are absent from this file.

| Folder / state | Meaning |
| --- | --- |
| `backlog` / Backlog | Known work that is not actively being implemented, including discovery or work with a known dependency. Record dependencies, blockers, and next action in the story; do not create additional state names. |
| `in-progress` / In Progress | Codex is actively implementing the story on a recorded branch/worktree. Include current progress, commit(s), and any blocker under `## Status`. |
| `blocked` / Blocked | Work cannot currently make meaningful progress because of an unresolved technical, validation, external, or developer-decision blocker. Record evidence, the exact blocker, and the condition that resumes work. |
| `review` / Review | Implementation is ready for code review, build/test review, or requested user validation. Record evidence, remaining validation, and the proposed merge/release decision. |
| `done` / Done | Accepted and complete. Record the final commit, merge/release reference, and validation result. |
| `will-not-do` / Will Not Do | Intentionally retained but not implemented as separate work. Record the decision, reason, date, and any superseding story/commit. Do not delete or reuse its ID. |

## Items

| ID | State | Title |
| --- | --- | --- |
| VP-0001 | Done | NLS one-shot selection and queue safety |
| VP-0002 | Done | Preserve armed NLS state through conditional aspect fallback |
| VP-0003 | Done | Generic active rectangle support for NLS |
| VP-0004 | Done | Reliable limited-range output investigation and implementation plan |
| VP-0005 | Will Not Do | Alpha renderer timing observability and queue health |
| VP-0006 | Will Not Do | Alpha renderer optional scene-safe queue-drop correction |
| VP-0007 | Will Not Do | Alpha renderer controlled repeat correction |
| VP-0008 | Will Not Do | Alpha renderer presentation-pacing assessment and refinement |
| VP-0009 | Done | Alpha renderer DeckLink R210/R12B format parity |
| VP-0010 | Blocked | Strict opaque-panel OCR subtitle replacement |
| VP-0011 | Done | Alpha renderer color-managed 3D LUT support |
| VP-0012 | Done | Alpha renderer LUT pipeline contract spike |
| VP-0013 | Done | DirectShow queue/reset alignment and no-drop review |
| VP-0014 | Will Not Do | Alpha renderer SDR BT.2020 source and target support (duplicate of VP-0019; will not do separately) |
| VP-0015 | Done | Alpha renderer support for paired shader rules |
| VP-0016 | Will Not Do | Make scene detection available for the alpha renderer |
| VP-0017 | Will Not Do | Explain variable alpha-renderer queue depth |
| VP-0018 | Done | Re-select the content refresh rate when switching to alpha |
| VP-0019 | Done | SDR BT.2020 display profiles, F5/F6 hotkeys, and output signaling |
| VP-0020 | Done | v210 arbitrary-width and DCI P010/P210 support |
| VP-0021 | Backlog | Renderer format negotiation parity and truthful capability reporting |
| VP-0022 | Will Not Do | DeckLink encoded-format boundary and diagnostics |
| VP-0023 | Done | Alpha P010 sample-range contract and conversion regression tests |
| VP-0030 | Done | Rotate debug logs with a fixed default retention of ten files |
| VP-0031 | Done | Make debug-log retention configurable |
| VP-0032 | Backlog | Alpha renderer field-aware GPU deinterlacing |
| VP-0033 | Backlog | Alpha renderer libplacebo frame-mixing smooth motion |
| VP-0034 | Done | Restart-free mixed-aspect NLS |
| VP-0035 | Done | Robust low-latency active-aspect transitions |
| VP-0036 | Done | Consolidate application and renderer settings into one configuration file |
| VP-0037 | Done | Fix Alpha windowed-preview composition regression |
| VP-0038 | Done | Generic viewport state and screen-aware NLS configuration |
| VP-0039 | Done | Alpha cadence due-forecast liveness and diagnostics |
| VP-0040 | Done | Trusted active-picture detection and stable NLS engagement |
| VP-0041 | Done | Eliminate stale-frame flashes across renderer rebuilds |
| VP-0042 | Done | Indexed debug-log rotation filenames |
| VP-0043 | Done | madVR graph re-prime after lifecycle and queue pressure |
| VP-0044 | Done | Alpha native OSD visible-picture anchoring and scaling |
| VP-0045 | Done | Namespace built-in renderer configuration as vpvr |
| VP-0046 | Backlog | DirectShow event plumbing and passive health diagnostics |
| VP-0047 | Will Not Do | P3-D65 LUT-input and calibrated SDR output contract |
| VP-0048 | Will Not Do | Explicit SDR LUT transfer and range contracts (superseded by VP-0100/VP-0101) |
| VP-0049 | Done | Complete canonical CONFIGURATION.html reference |
| VP-0050 | Done | Put Alpha first and reverse renderer order |
| VP-0051 | Backlog | Generic Alpha shader-chain support |
| VP-0052 | Done | Lowercase runtime layout and vprenderer directory |
| VP-0053 | Backlog | Alpha LLDV parity and transition validation |
| VP-0054 | Done | DirectShow handoff queue saturation and UI-liveness recovery |
| VP-0055 | Done | Display-rate outlier quarantine and transition warm-up |
| VP-0057 | Done | Re-prime Alpha when it exceeds the configured queue limit |
| VP-0058 | Will Not Do | ReShade compatibility prototype with madVR |
| VP-0059 | Will Not Do | Stable per-mode frame-offset policy and Alpha semantics (superseded by VP-0069) |
| VP-0060 | Done | Reduce madVR fullscreen transition latency with stable target ownership |
| VP-0061 | Blocked | DirectShow in-place reset re-prime with asymmetric madVR queues |
| VP-0062 | Done | NLS safe full-raster fallback for high-black UI and artwork |
| VP-0063 | Backlog | Automatic Alpha-to-madVR handoff re-prime |
| VP-0064 | Done | Persisted Alpha SDR BT.2020 output and clear OSD reporting |
| VP-0065 | Done | Invalidate stale frames during channel and stream transitions |
| VP-0066 | Done | Re-architect the live video output pipeline into testable components |
| VP-0067 | Backlog | Upgrade VideoProcessor to C++17 |
| VP-0068 | Backlog | Evaluate a native Blackmagic SDK capture path and complete frame metadata contract |
| VP-0069 | Done | Achieve and verify a 50 ms low-latency Alpha renderer path |
| VP-0069-1 | Done | Native-format Alpha ingress and conditional P010 analysis |
| VP-0069-2 | Will Not Do | Alpha end-to-end latency reduction investigation (superseded by VP-0074) |
| VP-0070 | Blocked | CIH bar/boundary subtitle capture and relocation |
| VP-0070-1 | Blocked | Bar/boundary CueSet architecture and detector benchmark |
| VP-0070-2 | Blocked | Stable boundary-crossing diagnostic overlay |
| VP-0070-3 | Blocked | Same-frame panel restoration and glyph relocation |
| VP-0070-4 | Blocked | Panel subtitle live validation and performance |
| VP-0070-5 | Blocked | Extract subtitle analysis and relocation pipeline |
| VP-0071 | Done | Compose the VP diagnostics OSD through madVR |
| VP-0072 | Done | Repair or explicitly constrain DirectShow no-stop timestamp modes |
| VP-0073 | Done | Diagnose and minimally repair keyboard-command responsiveness |
| VP-0074 | Done | Alpha latency resilience and NLS shader cold-start recovery |
| VP-0075 | Done | Restore Alpha analysis parity on native RGB ingress |
| VP-0076 | Backlog | Decompose the DirectShow live-output pin without behavioral change |
| VP-0077 | Done | VP-0066 merged-beta acceptance validation |
| VP-0078 | Done | Re-prime Alpha after a real output refresh transition |
| VP-0079 | Done | Canonical queue profiles and gaming hotkeys |
| VP-0080 | Done | Make Alpha active-picture cropping fail safe on live full-raster video |
| VP-0081 | Backlog | Preserve madVR NLS geometry through output-readiness re-primes |
| VP-0082 | Done | Buffered active-picture look-ahead for Alpha |
| VP-0083 | Done | Alpha anamorphic presentation profiles |
| VP-0084 | Blocked | Bound DirectShow total steady queue after reset |
| VP-0085 | In Progress | Frame-correlated madVR NLS look-ahead |
| VP-0086 | Done | Comprehensive configuration usage reference |
| VP-0087 | Blocked | VP-managed subtitle fit with madVR presentation |
| VP-0088 | In Progress | Expose fast Alpha-native display refresh measurement |
| VP-0089 | Review | VP Renderer NLS+ balanced stretch |
| VP-0091 | Done | Hide System32 DirectShow renderers by default |
| VP-0092 | Backlog | Discover madVR shortcuts and control its native statistics OSD |
| VP-0093 | Done | Prevent Alpha SDR BT.2020 output-contract regressions |
| VP-0094 | Done | Select the configured fullscreen monitor by friendly name |
| VP-0095 | Done | Target configured fullscreen display only (MultiMonitor controls display power/topology) |
| VP-0096 | Done | Establish range-correct video-frame conversion contracts |
| VP-0097 | Done | Safe standalone configuration editor and VP integration |
| VP-0098 | Done | Fit trusted active-picture envelopes correctly on arbitrary CIH screens |
| VP-0099 | Review | Dynamic, renderer-neutral NLS geometry and safety policy |
| VP-0100 | Backlog | Prove pixel-owned SDR presentation for Alpha |
| VP-0101 | Backlog | Implement production pixel-owned calibrated output and 3D LUTs |
| VP-0102 | Done | Dual-mode classic and modern operator UI |
| VP-0103 | Review | Apply saved configuration safely to a running VideoProcessor |
| VP-0104 | Done | Allow NLS without trusted crop on known scope viewports |
| VP-0105 | Done | Toggle the runtime UI with a configurable shortcut |
| VP-0106 | Done | Reduce madVR NLS transition latency |
| VP-0107 | Done | Normalize runtime dependency layout and plugin-private libraries |
| VP-0108 | Backlog | Make Modern UI unavailable values use the canonical `---` placeholder |
| VP-0109 | Backlog | Validate and support Alpha pure-2.2 Studio limited output |
| VP-0109-1 | In Progress | Prove the pure-2.2 renderer/Studio-G22 transport pairing |
| VP-0109-2 | Backlog | Implement rejection-safe pure-2.2 Studio limited output |
| VP-0110 | Done | Smooth viewport subtitle placement in and out with millisecond timing |
| VP-0111 | Backlog | Configurable foreground shortcuts and focus restoration |
| VP-0112 | Done | Show the active profile in relevant configuration pages |
| VP-0113 | Done | Screen Config layout and unit-field consistency |
| VP-0114 | Backlog | Alpha conservative scaling and small-bar zoom controls |
| VP-0115 | Backlog | Diagnose DeckLink delivery failures and renderer-transition drops |
| VP-0116 | Done | Show all active shaders in the configuration UI |
| VP-0117 | Done | Restore VP Renderer presentation timing and mode truth |
| VP-0118 | Done | Make Alt+Enter enter fullscreen from an inactive startup request |
| VP-0119 | Done | Apply anamorphic lens scale in the correct direction |
| VP-0120 | Backlog | Retire owner-bound configuration tray processes |
| VP-0121 | Review | Make configuration help UI-first with current screenshots |
| VP-0122 | Done | Retain scope geometry through subtitle and volume overlays |
| VP-0123 | Review | Split video conversion policy by renderer |
| VP-0124 | In Progress | Safely accelerate outward active-picture transitions with bounded lookahead |
| VP-0125 | Done | Diagnose Alpha fullscreen target-nits colour crushing and provide Output Experiments |
| VP-0126 | In Progress | Standalone Alpha test-pattern generator |
| VP-0127 | Done | Enforce output format and prove composed display delivery |
| VP-0128 | Backlog | Audit VP Renderer option parity and resolved defaults |
| VP-0129 | Review | Retain scope through vertical overlay arbitration |
| VP-0130 | Done | Renderer telemetry and live input-configuration clarity |
| VP-0131 | Done | VP Renderer NLS-V and bounded presentation crop |
| VP-0132 | Done | Eliminate subtitle-fit presentation chatter |
| VP-0133 | In Progress | Capture authoritative renderer output and near-black diagnostics |
| VP-0134 | In Progress | Verified symmetric renderer handoff and display-state restoration |
| VP-0135 | Backlog | Execute refresh-rate commands directly without a ping delay |
| VP-0136 | In Progress | Prevent transient same-axis inward aspect switches |
| VP-0137 | Done | Restore bounded madVR queue, NLS, and renderer-switch behavior |
| VP-0139 | Done | Configuration inheritance and VP Renderer quality clarity |
| VP-0140 | Done | Compile VP Renderer shaders without blocking UI or presentation |
| VP-0066-1 | Done | Characterize the live output pipeline with replayable golden traces |
| VP-0066-2 | Done | Extract a graph-independent video timing controller |
| VP-0066-3 | Done | Extract epoch-aware frame transport and processing components |
| VP-0066-4 | Done | Integrate DirectShow delivery and lifecycle coordination |
| VP-0066-5 | Will Not Do | Extract subtitle analysis and relocation (superseded by VP-0070-5) |
| VP-0066-6 | Done | Output readiness and deterministic post-ready prefill |
| VP-0066-7 | Will Not Do | Invalidate and reacquire refresh-rate measurements at real transition boundaries |
| VP-0066-8 | Will Not Do | Extend the DXGI refresh-rate evidence window |
| VP-0066-9 | Done | Converge the VP-owned queue once per fresh live-output epoch |
| VP-0024 | Done | Alpha source-to-display timing and queue telemetry |
| VP-0025 | Done | Renderer-neutral scene detection and Alpha integration |
| VP-0026 | Done | Alpha low-latency elastic queue |
| VP-0027 | Done | Alpha display-verified scene-safe cadence correction |
| VP-0028 | Done | Renderer configuration profile, hotkey, and event-action unification |
| VP-0029 | Will Not Do | Alpha two-pass display LUT and final-dither pipeline spike |

## Codex story workflow

`origin/main` is the canonical source of truth for story state. Story status,
folder placement, the registry, and workflow instructions must always be
tracked from the current story-tracker `main` branch so a later session can
discover and update them reliably. Secondary story checkouts are disposable
working copies, not independent authorities.

Before reading, creating, moving, or committing a story, Codex must fetch
`origin/main` and verify the working copy is based on the current remote main.
Use a clean main-based checkout for the change. If an existing checkout is
dirty or based on an older main, do not merge, rebase, reset, or overwrite its
uncommitted work; create a fresh checkout from `origin/main` instead. A story
change is not complete until it is committed and pushed to the canonical
`main` branch, unless the user explicitly requests another review branch.

## Story ID conflict audit

Before creating a story or child task, and after every fetch/retry caused by
remote tracker changes, Codex must audit all canonical state folders
(`backlog`, `in-progress`, `blocked`, `review`, `done`, and `will-not-do`).
Parse root `VP-####` and child `VP-####-N` IDs from story filenames, then
compare them with the Registry state and every `## Items` table row.

The audit must detect and report:

- duplicate IDs in two canonical story files;
- a canonical story file absent from the table;
- a table entry without exactly one canonical story file;
- Registry `Last assigned`, `Next story number`, or total-count values that do
  not match the discovered state; and
- any root ID in the registry/table greater than the highest canonical root
  filename ID; and
- any child ID without its root story or with a non-contiguous child sequence.

Never reuse an ID because the registry is stale. For a new root story, assign
an ID strictly greater than the maximum root ID discovered in both the
canonical files and the index/registry, then update the Registry state and
table in the same commit. A duplicate or inconsistent historical entry must
not be silently renumbered, overwritten, deleted, or moved: report it and
obtain or record a deliberate tracker-repair decision. If new work still needs
a root story, use the next higher unused root ID so the conflict cannot
spread.

Codex owns moving stories between folders in this session. Move the file,
update its `## Status` section, and update the table above in the **same
commit**. Never copy a story into a second state folder; `git mv` is preferred
so history follows the file.

## Story decomposition and child tasks

A broad root story may be decomposed only when the child boundaries are
meaningful implementation increments with independently runnable acceptance
tests. Use this to preserve testability and reviewability, not to turn every
class extraction, file move, or investigation note into a separate task.

- A root story uses the permanent `VP-####` ID. Its ordered child tasks use
  `VP-####-N`, starting at 1 with no gaps (for example `VP-0066-1` through
  `VP-0066-4`). Child-task IDs do not consume a root story number or change
  `Last assigned item` / `Next story number`.
- Treat `VP-####-N` as a complete story ID in filenames, headings, links, and
  the `## Items` table. The ID audit must parse both `VP-####` and
  `VP-####-N`, require exactly one canonical file and one table row for each,
  detect duplicate child IDs, and verify every child has an existing root.
  `Total indexed items` counts both roots and child tasks.
- Keep the root story as the durable objective and completion roll-up. Add a
  `## Decomposition` section that lists each child in order, its intended
  testable outcome, dependencies, and the rule for closing the root. The root
  normally remains `Backlog` while its child tasks progress; it is not
  implementation work in parallel with an active child unless explicitly
  stated.
- Each child is a canonical story in exactly one state folder and must contain
  its parent, scope boundary, dependencies, independently testable acceptance
  criteria, and its own `## Status` evidence. Move and update children under
  the normal state workflow. A later child must not begin until its recorded
  dependency is accepted, unless the root explicitly documents safe parallel
  work.
- Do not create grandchildren (`VP-####-N-M`). If a proposed child cannot be
  completed and validated as a coherent unit, revise the root decomposition or
  create a new root story rather than nesting task IDs further.
- Move a root story to `done` only after all its child tasks are `Done`, its
  cross-task acceptance criteria have passed, and the root records the final
  roll-up evidence. If decomposition is abandoned, preserve the existing child
  records and mark their deliberate disposition; never delete or reuse them.

## Implementation branch gate

Story documents and story-state changes always commit directly to the tracker
`main` branch. Implementation work for a story in the VP source repository
does **not** assume that `main` is the integration base. Before starting that
implementation work, Codex must manually query the current default branch of
`origin` for `billslack2/videoprocessor` (for example with `git remote show
origin`) and report it to the developer.

Codex must then ask the developer to confirm that discovered default branch as
the implementation base or to name another branch. Do not create a feature
branch, worktree, PR, or implementation commit until that confirmation is
received. Record the confirmed base branch and the implementation branch/worktree
in the story when it moves to `in-progress`. Re-run this manual discovery and
confirmation gate whenever a new story implementation starts; the origin
default branch may change.

1. After completing the Story ID conflict audit above, create new root stories
   in `stories/backlog/` using the next permanent root ID greater than every
   known root ID, then update the Registry state and table in this file.
2. Before moving a Backlog story to `in-progress`, record a readiness review in the
   story. Verify that the configuration model matches current code; required API
   behavior, pipeline order, and resource lifetime are known; dependencies and
   platform constraints have an explicit completion boundary; validation can
   prove important correctness claims; and work will use a clean, correctly
   based worktree.
3. If an unknown could invalidate the design (for example color-pipeline
   placement, security boundary, concurrency model, or OS/API behavior), create
   a bounded Backlog spike first. A spike must state its unknown, non-production
   scope, reproducible evidence, decision, and the stories it unblocks. Keep
   the dependent story in Backlog and link the spike.
4. When implementation begins, move the story to `in-progress/`, set its
   status to `In Progress`, and record the branch/worktree and first progress
   note in the same tracker change. Subsequent meaningful progress, blockers,
   branch changes, and validation results must be recorded in the story while
   work continues. Do not leave an actively implemented story in `Backlog`, and
   do not move it merely because investigation or discussion started.
5. When implementation is ready for independent assessment, move it to
   `review/`, set its status to `Review`, and record build/test evidence plus
   completed work, build/test evidence, and exactly what reviewer or user
   validation remains.
6. Move a story to `done/` only after acceptance, appropriate validation, and
   merge or deliberate release. Set its status to `Done` and record the final
   commit/PR/release reference.
7. A story may move backward—for example Review to In Progress after a failed
   test, or In Progress to Backlog when a new prerequisite is found. Record why
   in `## Status`; do not invent additional state names.

Terminal disposition: move a story to `will-not-do/` when it is intentionally
declined, duplicated, superseded, or otherwise determined not to be separate
work. Set its status to `Will Not Do` and record the decision, reason, date,
and any replacement story/commit. This is a tracked terminal state, not
deletion.

Blocked workflow: move a story to `blocked/` when work is genuinely stopped by
a specific unresolved technical, validation, external, or developer-decision
blocker. Set its status to `Blocked` and record the evidence, exact blocker,
safe interim behavior, and objective condition that resumes work. `Blocked` is
not a parking state: when a concrete next action exists, move the story to the
appropriate active state and record why.
