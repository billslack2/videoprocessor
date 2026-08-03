# VP-0079: Canonical queue profiles and gaming hotkeys

## Status

Backlog. Created 2026-08-02. This story introduces a generic, typed queue
profile group; it is not an Alpha-only latency special case. Its immediate
deployment target is the user's Alpha gaming workflow.

## User story

As a VP user, I want to switch deliberately between normal and minimum-queue
latency with `l` and Shift+`L`, using the same composable profile mechanism as
other configuration rules, so I can use Alpha for gaming without editing the
configuration or UI while capture is active.

## Decisions

1. `[queue] queue_size` becomes the canonical base hard-capacity setting.
   `[command_line] queue_size` remains a supported compatibility fallback.
   If both file settings are defined, `[queue] queue_size` wins.
2. A literal process command-line `/queue_size N` remains the highest-priority
   deliberate launch override. A selected queue profile may override the file
   base, but never that literal command-line value.
3. Add a normal generic `queue` profile group. Queue settings remain typed:
   they may appear in `[queue]` or `[profiles.queue.*]`, not in arbitrary
   input, scaling, display, viewport, shader, or renderer sections.
4. Profile conditions compose. The same physical shortcut may select matching
   rules in several groups; this story adds only the queue group. It must not
   create an `alpha_latency` or other renderer-specific profile mechanism.
5. The deployed configuration uses explicit selection keys:
   - `l`: select the `normal` queue profile.
   - Shift+`L`: select the `low_latency` queue profile.

   The implementation must use the existing normalized chord spelling used by
   the expression/accelerator parser, validate both chords, reject collisions,
   and document the exact syntax. Uppercase is not a separate unmodified key;
   it represents Shift as elsewhere in VP configuration.
6. A new process defaults to `normal` latency. Configure this group not to
   restore a previous low-latency manual selection after restart; a user must
   explicitly select Shift+`L` again. This is a safety choice for the gaming
   setting, not a change to global profile persistence.

## Problem

The deployed configuration currently supplies the shared renderer hard
capacity only through `[command_line] queue_size: 32`. Logs confirm that this
single value currently governs both paths:

```text
Renderer queue control selected: hard capacity=32 source=queue_size
Alpha queue policy: target_frames=2 target=2 hard_capacity=32
```

Alpha gaming needs a deliberate one-frame queue configuration without manual
editing or a renderer-specific one-off feature. Current profiles are strict
and do not allow queue fields, so placing queue values in an arbitrary profile
will not work. Current deployed configuration and help also need an exact,
single public vocabulary for the queue target field; the implementation audit
must resolve the observed `target_frames` versus older/currently documented
aliases before adding new public examples.

## Required behavior

1. Parse and validate canonical base `[queue] queue_size` as a positive
   integer. Resolve the base capacity with this file precedence:

   ```text
   /queue_size command-line argument
     > selected [profiles.queue.<name>] queue_size
     > [queue] queue_size
     > [command_line] queue_size compatibility fallback
     > existing default
   ```

   Preserve an existing explicit Alpha-only queue override only where it is
   currently supported, but `[queue]` and a selected queue profile take
   precedence when they explicitly supply `queue_size`. Log every effective
   source and any ignored lower-priority value.
2. Add `[profile_groups.queue]` and `[profiles.queue.<name>]` to the strict
   schema. The initial allowed fields are only:

   - `queue_size`: active renderer hard capacity; and
   - the canonical Alpha/VP-owned queue target field (currently observed as
     `target_frames`; choose and document one canonical public name).

   Do not silently add DirectShow presentation lead, frame offset, recovery
   thresholds, refresh switching, or picture-quality controls to this group.
3. Ship these values in the user's deployed configuration:

   ```ini
   [queue]
   queue_size: 32
   target_frames: 2

   [profile_groups.queue]
   profiles: normal,low_latency
   default: normal
   persist_profile_selection: false

   [profiles.queue.normal]
   when: <normalized l chord>
   queue_size: 32
   target_frames: 2

   [profiles.queue.low_latency]
   when: <normalized Shift+L chord>
   queue_size: 1
   target_frames: 1
   ```

   Replace placeholders with exact parser-valid condition strings. The base
   and normal profile intentionally agree, ensuring first launch and `l`
   always return to normal latency. `queue_size: 1` and `target_frames: 1` are
   the requested literal lowest Alpha queue configuration; brief frame drops
   during genuine capture/GPU stalls are an accepted gaming trade-off, but a
   profile switch itself must not manufacture drops.
4. On a queue profile change, atomically resolve the effective policy and
   apply it to the currently selected renderer through the existing queue-size
   update path. It must perform one serialized queue/renderer re-prime so
   entries admitted under the old capacity/target cannot persist. Coalesce a
   repeated same-profile selection and do not create a reset loop.
5. The generic queue profile applies to the currently selected renderer;
   DirectShow retains its own renderer internals and madVR private queues are
   never inferred or controlled. Alpha is the live-validation target.
6. Log startup and each profile change with selected profile, base/profile/
   command-line sources, effective capacity and target, active renderer,
   pre/post queue depth, re-prime identity, and outcome. Keep the existing OSD
   effective queue display correct; add the concise active profile name only
   if it can be done without changing unrelated OSD layout.
7. Update the sample `VideoProcessor.cfg` and `CONFIGURATION.html` with only
   the canonical public names, precedence, value ranges, hotkeys, default, and
   low-latency trade-off. Do not describe compatibility aliases, hidden
   settings, or implementation history in the user-facing help.

## Deployment requirement

After the implementation is accepted and a clean x64 Release build succeeds,
permanently update `C:\Videoprocessor\vp\VideoProcessor.cfg` with the normal
and low-latency queue profiles above. Before changing it:

1. create a timestamped backup beside the active configuration;
2. preserve every unrelated user value, comment, shader rule, display profile,
   renderer selection, audio action, and ordering as far as practical;
3. make the smallest possible edit to canonicalize the queue base and add the
   queue profile sections; and
4. launch the deployed Release binary, verify normal first-launch policy, then
   exercise `l` and Shift+`L` while Alpha is selected.

Deploy `VideoProcessor.exe` and `vprenderer\VideoProcessorVPRenderer.dll`
together from the same Release build and verify their hashes. Do not deploy a
Debug build or overwrite the user's configuration wholesale.

## Validation

1. Unit-test precedence for every combination of literal `/queue_size`,
   `[queue] queue_size`, legacy `[command_line] queue_size`, profile override,
   missing value, invalid value, and existing Alpha-specific override.
2. Unit-test strict schema acceptance for the two permitted queue fields and
   rejection when they appear in non-queue profile groups.
3. Unit-test normalized `l` and Shift+`L` profile expressions, duplicate-key
   rejection, normal default, and non-persistence of the low-latency choice
   across a fresh process.
4. Integration-test changing 32/2 -> 1/1 -> 32/2 with Alpha. Prove exactly
   one re-prime per actual change, no retained old-capacity frame, no reset on
   a repeated same-profile key, and correct logs/OSD capacity and target.
5. Live validate Alpha at 59.94/60 Hz with a game/capture source. Compare
   queue age and VP renderer latency in normal versus low latency, record
   dropped frames, and confirm the low-latency profile does not claim to
   measure or eliminate capture/display latency outside VP.
6. Smoke-test the generic profile behavior with DirectShow selected, without
   asserting control of madVR's private queues or changing its settings.

## Non-goals

- No madVR profile editing or private-queue control.
- No automatic latency tuning, frame-offset tuning, or refresh-rate-policy
  change.
- No shader, NLS, tone-mapping, scaling, HDR, or picture-quality profile in
  this initial queue-only story.
- No new broad configuration feature allowing arbitrary fields in any profile.

## Related work

- [VP-0057](../done/VP-0057_alpha-queue-limit-reprime.md) defines Alpha's
  hard-capacity behavior and protection against queue-limit reset loops.
- [VP-0074](../done/VP-0074_alpha-latency-resilience-and-NLS-shader-cold-start-recovery.md)
  remains the independent, rate-scaled stall/age recovery backstop.
- [VP-0078](../review/VP-0078_alpha-refresh-transition-reprime.md)
  owns one-shot re-prime after a real output-refresh transition. Queue-profile
  re-prime must compose with, not duplicate, that action.
