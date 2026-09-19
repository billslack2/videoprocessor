# VP-0191: Prevent incompatible saved queue profiles during refresh transitions

## Status

In Progress

Implementation began 2026-09-19 after the developer confirmed the discovered
remote default branch `v1.3.005-beta` as the integration base. The latest remote
tip was fetched as `8414b11633d3ab8cc06fa3964c273c81b76f05bc` (merge PR #95).

Branch: `codex/vp-0191-queue-profile-compat`  
Clean worktree: `E:\\codex\\videoprocessor\\vp-0191-queue-profile-compat`

## Readiness review

The deployed trace establishes the bounded failure: a source-invalid transition
publishes a saved queue selection before the successor renderer/source context
can resolve a compatible queue rule. The queue resolver, persisted selection
ownership, profile-event/OSD publication, and queue policy application must be
traced together before code changes. The implementation boundary is complete
when one production-connected regression proves rejected/deferred incompatible
state cannot reach policy, actions, OSD, or persistence while compatible
selection and confirmed madVR handoff behavior remain intact.

## User story

As a VP Renderer user, I want refresh/format transitions to retain or resolve a
queue profile compatible with the active renderer, so I never see a transient
madVR queue OSD or run an incompatible queue policy while VP Renderer is
rebuilding.

## Evidence and problem

The deployed log records a repeated sequence on refresh/format resync:

1. Capture becomes invalid long enough to be applied as `UNKNOWN / UNKNOWN`.
2. The persisted `profile.queue: madvr_queue` selection is re-applied, despite
   `[queue.MadVR_Queue]` declaring `when: ${renderer}=="DirectShow - madVR"`.
   The profile OSD reports `Queue=Madvr Queue`, and the Alpha queue is briefly
   changed to target 3.
3. The renderer/capture restart resolves the actual 23.976 source and applies
   `vp_24` (target 5), which is the intended steady VP Renderer queue.

This is not merely a stale label: the incompatible policy is briefly applied,
triggers a graph reset, and is written to the managed state file. `Ctrl+U`
therefore can report `madvr_queue` even though the recovered VP Renderer queue
is `vp_24`.

Relevant deployed evidence is `C:\Videoprocessor\vp\logs\vp.log` at
09:55:52 and repeated at 09:57:38, 09:58:02, 09:58:10, and 09:58:19:
`Profile overlay shown ... Queue=Madvr Queue`, followed by `Queue profile
applied: profile=madvr_queue`; the subsequent renderer restart applies
`profile=vp_24`.

Related work: VP-0142 (profile-selection persistence policy), VP-0146
(safe profile-rule reapplication), VP-0152 (profile actions and OSD), and
VP-0160 (profile rules after renderer swaps).

## Scope and safe design

Make queue-profile resolution renderer-compatible across invalid-capture,
refresh-transition, renderer-retirement, and renderer-start boundaries.

- Treat a persisted/manual queue selection whose `when` predicate does not
  match the effective renderer as ineligible. It must not change queue policy,
  emit `profile.queue.changed`, show the profile OSD, or overwrite managed
  state during a transient transition.
- When the renderer is known but source rate is temporarily unavailable, retain
  the last compatible effective queue policy until a matching rule can be
  resolved. Do not fall back to a saved madVR selection for VP Renderer.
- Once source rate is valid, resolve and apply the normal VP queue rule (for
  example `vp_24`, `vp_50`, or `vp_60`) exactly once. Preserve a compatible
  explicit manual selection when the user intentionally made one.
- Keep real renderer changes working: a saved `madvr_queue` selection may be
  restored only after DirectShow/madVR is the confirmed effective renderer.
- Keep profile OSD and `Ctrl+U` sourced from the same committed effective
  selection. They must never claim a profile that was rejected or only pending.
- Do not mask a source/renderer lifecycle fault by silently applying base queue
  settings; log one concise rejected/deferred-resolution reason per transition.

This story does not redesign profile persistence, alter queue capacities, or
change refresh-mode selection. It addresses compatibility and atomicity of the
existing saved/manual queue selection path.

## Acceptance and regression evidence

- Add a production-connected regression covering a VP Renderer refresh resync
  with managed state containing `profile.queue: madvr_queue`. Prove the
  incompatible selection neither changes Alpha queue policy nor emits an OSD,
  action, or state write.
- Prove a valid 23.976 recovery resolves `vp_24` once and that 50/60 Hz recoveries
  select their corresponding VP profiles without duplicate resets or OSDs.
- Prove an explicit compatible VP queue selection persists through the same
  transition, and a confirmed DirectShow/madVR handoff can legitimately resolve
  `madvr_queue`.
- Assert status/profile reporting cannot expose a rejected, stale, or pending
  queue as active.
- Cover invalid-source grace expiry, renderer restart, profile-rule reapply,
  and a queued source-rate update so the fix cannot regress either policy or
  lifecycle ordering.
- Complete targeted tests and an x64 Release build. Record a before/after live
  refresh-rate test with logs showing no `Madvr Queue` overlay on VP Renderer.

## Next action

Trace current profile-resolution ownership, add the failing lifecycle regression,
then implement and verify the smallest ownership-bound compatibility gate.
