# Alien dark-scene crop recovery

## Observed failure

The deployed e0d226ad session on 2026-09-20 accepted a 3840x1600
picture (0,280-3840,1880) at source sequence 247. At sequence 255,
global near-black and bounded content outside that picture arrived together.
The near-black episode began at full raster and discarded its entry crop.
Later frames had safe excluded bands and a contained provisional observation
(0,416-2668,1880), but no fresh native crop acquisition. The episode remained
full raster with zero recovery samples.

The available evidence is the runtime log, not a captured Alien source frame.

## Change

Preserve the existing trusted crop as the episode's recovery reference even
when outward content requires full-raster presentation at episode entry.
Use the existing exact-entry recovery path without changing its pixel,
provenance, or dwell requirements. At 23.976 Hz this requires seven qualifying
source measurements. An HDMI source paused upstream still supplies new capture
frames; cached cadence repeats do not count as additional proof.

The old native-bootstrap route remains available for episodes that began at
full raster. A partial exact-entry proof does not delay independent native acquisition.
Episodes that began retaining a crop do not gain new bootstrap eligibility.

No change to detection thresholds, bar symmetry, accepted geometry, subtitle
pixel vetoes, or source/scene/profile invalidation. The provisional inner
rectangle is never promoted to picture authority or used as a new crop.

## Regression coverage

- The logged rectangle and simultaneous outward/dark entry reproduce a failed
  recovery on the unmodified beta; the negative cases already pass there.
- Seventeen rejection cases cover unavailable or stale measurements, unsafe
  bands, missing/uncontained observations, old or unrelated crop authority,
  generation/profile mismatch, outward content, darkness, and cadence repeats.
- Scene, source, profile and full-raster-authority boundaries clear the saved
  recovery context instead of restoring it.
- A synthetic P010 replay acquires a scope picture through the real transition
  model, enters full raster on a near-black frame with outside-band content,
  then exercises asymmetric illumination, bright and colored outside-band
  content, real full-frame content, and complete black. It checks both episode
  recovery and the final source crop.
- Existing native-bootstrap, paused-frame, title, crop, transition, and scene
  tests remain part of the full native suite.

## Validation and rollout

The candidate starts from remote v1.3.005-beta at 525767f6. The running e0d226ad
build additionally contains the unmerged crop-admission and look-ahead work;
those changes have not been imported into this branch.

Automated results are recorded in TestResults. Live replay and visual acceptance
on a candidate containing the intended complete release stack remain required.
This investigation does not deploy binaries or change the active configuration.

Validation completed:
- Original-code reproduction: alien-before.trx, 1 expected recovery failure;
  both rejection/context tests passed.
- Focused candidate replay: alien-focused.trx, 5/5 passed.
- Final complete x64 Release solution build: succeeded, zero errors
  (alien-solution-build.log). Repository toolsets, including v142 for MFC
  targets, were retained.
- Final native regression suite with the built renderer:
  alien-final-full.trx, 1,307 passed, zero failed or skipped. This includes the
  added test proving native bootstrap is not delayed by partial entry recovery.
- The near-black episode function is identical in the original beta and
  deployed e0d226ad sources; integration with the other deployed changes and
  live Alien replay are still separate acceptance steps.
- No binaries were deployed and no active configuration was modified.

Reproduction commands from this worktree, using the installed Visual Studio
18 Professional MSBuild and VSTest:

    MSBuild.exe VideoProcessor.sln /m /p:Configuration=Release /p:Platform=x64
    vstest.console.exe x64\Release\VideoProcessor-Test.dll /Platform:x64 /Logger:trx
