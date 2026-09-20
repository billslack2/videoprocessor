# VP-0189: picture transition / subtitle FIT handoff

## Observed problem

The preserved September 19 log (`vp-transition-stutter-20260919-2209.log`)
shows source sequences 3729-3732 at approximately 22:08:15 on release 29ea0339:

| Sequence | Picture proof | Logical picture | Presentation |
| --- | --- | --- | --- |
| 3729 | Broad expansion 1/3 | 0,276-3840,1884 | Established scope |
| 3731 | Broad expansion 3/3; local model starts confirmation | Same scope | Dense FIT plus padding: 0,20-3840,2140 |
| 3732 | Local model publishes | 0,68-3840,2092 | New picture |

The intermediate aspect is 1.8113 rather than the final 1.8972. At 24 fps the
logged intermediate lasts one source frame. The queued decision at 3731 was
rejected because its old reference begins at 280 instead of 276, a four-pixel
scan difference. Other scheduling phases can expose FIT before the outward
proof completes. The newer final crop-acquisition guard did not block these
frames; this is a competing presentation handoff, not that guard rejecting crop.

This reproduces a specific logged transition; it is not proof that every visual
stutter has the same cause. The tests use measured policy inputs rather than a
raw decoded replay of the HDMI recording.

## Correction boundaries

- A current full-width trusted bar observation, with broad picture evidence on
  both expanding vertical strips, may reserve presentation for the picture
  model's existing confirmation. Subtitle FIT cannot insert its padded geometry
  during that handoff. Normal model publication applies the final picture.
- This permission is tied to the exact old crop, current observation, source
  generation/sequence and presentation epoch. Final acquisition admission and
  presentation recovery still run afterward. No new picture authority is minted.
- The model's own geometry eligibility rules exclude small retained changes.
  They remain eligible for ordinary bounded FIT; there is no separate AR threshold.
- Subtitle translation/drift, or FIT already active when the proof begins, stays
  on its existing path. Removing an already visible displacement/FIT would itself
  cause an extra step. FIT generated during this proof is the conflict being fixed.
- Broad outward proof now tolerates one actual detector scan step per edge:
  4 pixels at 4K or 2 at 1080p. Every frame still needs fresh exact strip
  evidence. Proof compares against its first candidate, not the previous sample,
  so 68 -> 72 -> 76 cannot accumulate tolerance. Exact base, axes, raster and
  generation changes reset proof.
- If larger candidate jitter prevents completion, the handoff must relinquish control
  after the existing combined confirmation budget. Ordinary pixel-bounded FIT
  then exposes the picture. It must not restart suppression on each noisy target.
- A real model commitment uses the current trusted coordinates, with matching
  classification/axes and all existing admission checks. Its candidate remains
  anchored before commitment and the committed crop remains fixed afterward.
  Provisional history recovery retains its canonical trusted geometry.
- Lookahead may tolerate one detector scan step in an old reference only when
  current outward picture proof independently confirms the expansion. Exact
  current target/frame identity, raster and bar axes, containment of both old
  references, and all normal admission/deadband vetoes remain required. Inward
  reference checks remain exact.
- A pending recurrence of remembered geometry may differ from current trusted
  bounds by one detector step. That tolerance only preserves the old presented
  crop while confirmation finishes; it cannot publish remembered coordinates
  early or relax the queued decision's exact current-target validation.
- No additional pixel sampling, brightness/color thresholds, or wall-clock hold
  is introduced. Existing source-frame confirmation requirements remain intact.

The narrow handoff does not claim new coverage for mixed-axis/asymmetric format
changes, near-black scenes, or transitions that begin with subtitle motion.
Those keep their existing policies and require regular viewing validation.

## Review and validation

Two independent colleagues reviewed the design and QA integration. Their review
identified pre-existing FIT and continually resetting candidate proof as risks;
both received dedicated regressions and were corrected before qualification.

Durable evidence: `C:\Users\bslac\Documents\ChatGPT\Done\crop-transition-handoff-20260919`.
The qualification runner and original working results are also preserved under
`C:\Users\bslac\AppData\Local\Temp\vp-transition-handoff`.

- RED: `red-handoff-2.trx`: three expected behavior failures, two safety controls
  passing, on unchanged production code. The earlier fixture type compile error
  is preserved separately and is not counted as behavioral RED evidence.
- An incremental green build hit MSVC LNK1103 corrupt debug information. That
  failed build is preserved; qualification requires a successful clean Release
  rebuild and tests, not a retry against uncertain output.
- `red-review-findings-2.trx`: 104/106 passed. Both added review controls failed
  as predicted: pre-existing FIT was overridden and alternating 4-pixel targets
  retained the handoff indefinitely. Both defects were corrected and the
  expanded 315-test focused run passed those controls.
- A hybrid P010/policy replay then proved another defect. At sequence 3732,
  current measured top 68 was published as the earlier top 72 candidate; actual
  same-frame pixel reinspection rejected it and final presentation became full
  raster. `red-current-commit-final.trx` records the exact values. This replay
  uses stable source bytes plus injected detector noise, not the original film.
  It motivated the current-trusted-coordinate commitment correction.
- Repeating the same scope-to-taller-to-scope cycle exposed a history-only
  mismatch: the pending model named a remembered rectangle four pixels away
  from current trusted bounds. `red-repeated-transition-noise.trx` passed
  319/321; both noisy repeated-history regressions failed. Pending handoff now
  uses the same anchored detector-step tolerance, with exact current evidence,
  containment of the old crop, source identity and budget still required.
- `green-final-full.trx`: clean x64 Release solution rebuild succeeded and all
  **1,336/1,336 tests passed**. This adds 29 tests over the 1,307-test baseline.
  Every changed source/test/project file matches the pre-build source snapshot.
  Source commit identity and hashes are recorded in the durable receipt.
- Coverage includes one/both-edge four-pixel variations in both orders, dense
  analysis cadences of one/two/three frames, lookahead and live decisions,
  rejected stale queued targets, and two complete aspect-change round trips.
  Outward replay uses actual P010 pixel-retention reinspection; inward legs use
  policy evidence. It is not a decoded-content or GPU presentation replay.
- QA and the independent model reviewer found no remaining blocking issue in
  the final diff. Safety controls cover excessive/cumulative drift, stale strip
  evidence, source/base/raster/axis changes, existing subtitle ownership,
  admission/recovery, and the finite confirmation budget. Existing genuine
  multi-AR transitions in both directions remain in the passing full suite.

Build/test validation preceded the local source commit. No candidate deployment,
merge, ZIP or installer is part of this correction pass. A deployment must rebuild
the clean committed identity and verify the paired host/renderer hashes.

## Live validation

After an explicitly requested deployment, replay the same transition several
ways (ordinary playback and rewinds), verify matching host/renderer identities,
and inspect final source-crop events for a single old-picture to new-picture
change with no padded FIT excursion. Also watch both aspect-change directions,
burned-in subtitles, menus and dark introductions. Keep VP-0189 in Review.
