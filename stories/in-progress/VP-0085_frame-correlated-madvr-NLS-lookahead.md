# VP-0085: Frame-correlated madVR NLS look-ahead

## Status

In Progress. Split from VP-0082 on 2026-08-04 when Alpha runtime look-ahead was
accepted and closed independently. Implementation began on 2026-08-04 on
`codex/vp-0085-madvr-lookahead`, based on the repository's current default
branch `v1.1.015-beta`. The first action is a diagnostic replay
that correlates VP source identity, DirectShow delivery acceptance, NLS
graph-owner application, and the nearest observable madVR presentation
boundary. No runtime control should be added until that correlation is proven.

The creation audit found 99 canonical story files and 99 index rows through
`VP-0084`, with no duplicate or missing IDs. The pre-existing index-state
mismatches for unrelated stories were reported previously and are not silently
repaired here.

## Configuration

Reuse the existing optional `[queue] active_picture_lookahead_frames` setting;
do not add a madVR-specific setting. Omit it or set it to `0` to retain current
madVR behavior. Positive values (`2`, `4`, or `8` during validation) enable
only the VP-0085 look-ahead path while leaving queue capacity and madVR native
crop/fit settings unchanged. The shipped configuration currently documents it
as an Alpha diagnostic preview, so that wording must be updated before this
madVR implementation is enabled for live use.

## User story

As a madVR CIH user watching mixed-aspect content, I want VP to use its
existing buffered active-picture evidence to prepare NLS mode and geometry
before the corresponding scene becomes visible, so scene changes do not flash
through `Waiting`, stale stretch, or the wrong safe-fit mode.

## Current behavior and boundary

VP-0082 merged the shared scheduled-decision model and full Alpha runtime
application. The DirectShow path currently:

- accepts and retains `active_picture_lookahead_frames`;
- propagates it into the live source output pin;
- logs `runtime-active=0`;
- never reads the retained value to analyze or consume a scheduled decision;
- continues to publish the current active-picture rectangle, which the madVR
  graph-owner refresh timer uses to select NLS mapping.

madVR NLS presently uses VP geometry to choose `Active`, `Scope passthrough`,
`Safe fit`, or `Waiting`, install the required shader parameters, and prepare
the output-aspect contract. For passthrough and safe-fit cases, VP deliberately
delegates hard-bar crop/fit to madVR's native implementation to prevent a
double crop.

The useful opportunity is therefore frame-correlated NLS preparation. VP's
future black-bar analysis can inform the upcoming NLS mode, but it does not
currently advance or replace madVR's own native black-bar detector.

## Scope

1. Reuse the VP-0082 bounded scheduled active-picture decision and existing
   DirectShow raw/converted queues. Do not add buffering to obtain look-ahead.
2. Associate each accepted decision with source sequence, queue epoch, raster,
   renderer generation, and the DirectShow sample that carries that source
   frame downstream.
3. Translate the semantic decision into madVR NLS mode, source geometry,
   shader parameters, and output-aspect intent before the corresponding scene
   reaches the nearest safe visible boundary.
4. Keep madVR's graph-owner thread as the sole owner of shader and output
   contract mutation. Delivery/capture threads may publish an immutable intent
   but may not call renderer COM interfaces directly.
5. Coalesce repeated equivalent intents and apply only state changes. Never
   reinstall or mutate madVR settings once per frame.
6. Validate the decision against current epoch, source/raster, screen profile,
   renderer generation, and latest trusted outward-safety evidence immediately
   before application.
7. On insufficient lead or late graph-owner execution, apply at the earliest
   safe boundary and log the measured lateness. Never delay sample delivery or
   presentation to satisfy the configured count.
8. Preserve the last safe NLS mapping through ordinary dark/ambiguous evidence;
   use `Waiting` only for a real invalidation or a transition that cannot safely
   retain prior geometry.
9. Keep crop and NLS responsibilities truthful: VP owns NLS intent and custom
   shader geometry; madVR continues to own native hard-bar crop/fit unless a
   separate story explicitly changes that contract.

## Native black-bar non-goal

This story does not attempt to drive madVR's native black-bar removal per
frame. `IMadVRSettings` exposes profile/settings mutation, not a proven
frame-latched crop callback. Calling it for every frame would race madVR's
private downstream queue and could apply a crop to the wrong visible sample.

Do not disable madVR bar detection, crop the DirectShow media raster, or
install a second VP crop merely to claim look-ahead parity. If native madVR
bar transitions remain visibly inadequate after NLS look-ahead, create a
separate capability spike with an explicit ownership and rollback contract.

## Timing and synchronization questions to prove

- Which source identity survives conversion into each successfully accepted
  DirectShow sample?
- How far ahead of that delivery can the graph owner safely prepare an NLS
  shader/output contract without affecting older samples already queued in
  madVR?
- Does madVR apply an external shader or dynamic output-aspect change to all
  queued samples, only future processing, or an unobservable internal boundary?
- Can the existing refresh timer meet the required boundary, or is a keyed
  graph-owner command required?
- What is the bounded fallback when the requested frame has already entered
  madVR's private queue?

The implementation may target the nearest safe DirectShow/graph boundary; it
must not claim exact madVR presentation-frame latching without evidence.

## Required telemetry

For each NLS state change, record configured/available/effective look-ahead,
observation and effective source identity, queue epoch, DirectShow delivery
identity and timestamp, graph-owner apply time, renderer generation, mapping
before/after, late/held/rejected reason, and whether native crop/fit or a VP
custom shader owns presentation.

Telemetry must distinguish:

- VP active-picture detection lead;
- VP-to-DirectShow delivery lead;
- requested NLS application boundary;
- actual graph-owner application;
- unobservable madVR internal residence.

Do not log every normal frame.

## Required tests

1. `active_picture_lookahead_frames = 0` preserves current madVR NLS behavior.
2. Positive values clamp to safely available DirectShow lead without changing
   queue capacity or blocking delivery.
3. Scheduled decisions are accepted only for the matching source sequence,
   queue epoch, raster, renderer, and screen-profile generation.
4. An NLS state change is published once, coalesced on duplicates, and applied
   only by the graph owner.
5. Late, stale, reset, flush, drop, renderer-switch, and output-readiness
   decisions fail safely without carrying geometry across generations.
6. Dark scope cuts retain the last safe mapping; genuine scope/16:9/4:3
   transitions select the correct upcoming mode.
7. Safe-fit and scope-passthrough plans leave native madVR hard-bar crop/fit
   enabled and never install a duplicate VP crop.
8. Custom NLS geometry and output aspect change coherently, without a frame
   exposing old geometry with the new mapping.
9. Rapid NLS toggle, pause/resume, manual reset, automatic re-prime, and
   Alpha/madVR handoff remain generation-safe.
10. Complete the full native suite and a clean x64 Release build.

## Live validation

A/B the same content at look-ahead values `0`, `2`, `4`, and `8`, without
changing queue depth:

- dark scope-to-scope cuts;
- scope to 16:9, 1.85, and 4:3 transitions;
- Apple TV menus, volume overlays, recap buttons, and mixed-aspect programming;
- NLS on/off while paused and playing;
- reset/re-prime and rapid renderer switches;
- 23.976 and 59.94/60 Hz sources.

Review synchronized logs and screen recordings. A visual improvement is not
sufficient if telemetry shows the NLS command applied to a different source
identity than intended.

Bill owns practical CIH and madVR viewing acceptance. Urvish owns detector,
dark-scene, overlay, and temporal-evidence review. Both must approve runtime
activation; Bill's practical renderer experience decides between equivalent
safe approaches when an exact but expensive design offers no visible benefit.

## Acceptance criteria

- Positive look-ahead materially reduces madVR NLS `Waiting`, stale-stretch,
  and safe-fit flashes at scene transitions compared with value zero.
- The feature uses only already-buffered evidence and adds no queue depth,
  presentation wait, or per-frame COM/settings mutation.
- Every applied NLS transition is correlated to a valid current DirectShow
  source identity and the nearest honestly reported safe boundary.
- Crop ownership remains singular: madVR native crop/fit for delegated modes,
  VP geometry only where the custom NLS shader requires it.
- Ordinary uncertainty retains the last safe mapping; real invalidations fail
  safely without stale cross-generation geometry.
- Alpha look-ahead behavior is unchanged.
- Bill and Urvish approve live evidence after the native suite and clean x64
  Release build pass.

## Related stories

- VP-0034: Restart-free mixed-aspect NLS.
- VP-0040: Trusted active-picture detection and stable NLS engagement.
- VP-0061: DirectShow in-place reset re-prime with asymmetric madVR queues.
- VP-0066: Live-output pipeline, queue, identity, and epoch architecture.
- VP-0080: Fail-safe Alpha active-picture crop authority.
- VP-0081: Preserve madVR NLS geometry through output-readiness re-primes.
- VP-0082: Buffered active-picture look-ahead for Alpha.

## Evidence

- [videoprocessor PR #39](https://github.com/billslack2/videoprocessor/pull/39)
- `DirectShowVideoRenderer::SetActivePictureLookaheadFrames`
- `CBufferedLiveSourceVideoOutputPin` active-picture publication
- `DirectShowGenericHDRVideoRenderer::RefreshShaderRule`
- `MadVRShaderLoader::ResolveNlsRuleForFrame`
