# Sparse boundary crop experiment

This is a default-off experiment. It tests whether repeated local picture-edge
evidence can recover scope acquisition when the ordinary broad edge detector
remains provisional. Startup and mid-movie modes are enabled separately.

## Enabling and scope

Set `VP_SPARSE_BOUNDARY_CROP=experimental` in the environment of the VP process
before launch. Unset and unrecognized values leave it disabled. The option is
read once per renderer instance; no user configuration file is rewritten.

The first implementation requires automatic crop on a configured screen, with
NLS and fixed-aspect crop off. It cannot replace an established format, a scene
snapshot, an active presentation recovery/owner, or a ready native lookahead
publication. A one-shot source-generation gate closes at the first native or
experimental format publication. Profile changes, invalid analysis, viewport
changes, cuts, and diagnostic expiry cannot reopen it in that same source
session. This scope is deliberately narrower than all the potential uses of
local boundary evidence.

`VP_LOCAL_BOUNDARY_TRACE=shadow` remains an independent diagnostic option. Its
600-snapshot limit does not limit the experiment's acquisition checks or an
admitted crop's lifetime.

## Evidence and publication

1. Sample a bounded 128 by 270 source Y/U/V grid at most every 200 ms. Require
   consistent opposing boundaries, current clean sampled exterior, inward
   support, spatial coverage, and changing edge support across at least four
   fresh observations spanning at least 600 ms. One repeated source frame
   cannot supply extra evidence.
2. Freeze the original exterior reference and its black cutoff. New inward
   motion or a missing fresh proposal does not redefine that reference.
3. Round the candidate outward to retain the unsampled strips adjacent to the
   boundary witnesses, and align it for chroma. The Alien coarse interval
   280..1879 therefore becomes a conservative full-width crop 272..1888 at 4K,
   rather than silently claiming the intervening pixels were black.
4. On every fresh live source frame while acquisition is pending, recheck the
   fixed sampled exterior and run the ordinary retention inspection against
   the exact proposed crop. Current native authority, known orthogonal bars,
   outward pixels, unsupported precision, identity/gap/cut changes, and global
   near-black acquisition vetoes still prevent promotion.
5. A separate validated experimental contract may satisfy the existing startup
   near-black bootstrap dwell. That only reopens acquisition. The normal live
   transition model must then confirm and publish the candidate on subsequent
   frames. Raw measurements and failed-axis explanations remain unchanged.
6. Once published, existing current-frame retention, caption protection,
   expansion, near-black, and presentation admission govern the crop. There is
   no new 15-second crop expiry. The diagnostic/pending-reference lifetime is
   not an instruction to release an admitted crop.

Queued analysis stays native. The experiment cannot provide an `EXACT_INWARD`
lookahead certificate or backdate its startup crop. Existing lookahead continues
for independently measured native transitions.

Explicit experimental provenance follows evidence, model candidates, stable
geometry, and publication. Experimental geometry never enters recent native
trusted-format history. Only exact current native verification or a genuinely
new native publication can supersede that provenance; deadband/contained
matches are insufficient.

## Limits

These are bounded samples, not inspection of every excluded pixel. A lone
pixel between all sample locations can be missed. More fundamentally, moving
artwork on a full-raster black background can have exactly the same pixels as a
letterboxed shot. The tests retain that counterexample instead of claiming a
semantic distinction the detector cannot establish. This is why the first
active test is opt-in and why passing synthetic tests is not proof that Alien
or other real content is now solved.

Known 10/12-bit native input precision is required. P010 conversion, P210, and
native v210 use the same source-coordinate logic; the new tests compare their
results. An 8-bit capture expanded into a 10-bit plane does not acquire source
precision and remains ineligible.

## Logging and validation

Regular log events identify enablement, startup eligibility, source sequence,
reference ID, exact crop, native classification, near-black episode, current
exterior violations, ordinary retention/outward results, sample counts, cost,
and experimental publication. Instance IDs distinguish renderer restarts.

The new tests exercise actual source pixels through extraction, temporal
acquisition, near-black constraints, normal confirmation, and crop admission;
plus repeats, context resets, exact crop rounding, current intrusions, captions,
true expansion, provenance/history and the known moving-art ambiguity.
Validation results and build hashes are recorded under
`artifacts/sparse-boundary-experiment` in the checkout. Deployment requires a
separate request and a successfully completed x64 Release runtime pair.

## Mid-movie trial

Set `VP_SPARSE_BOUNDARY_CROP=experimental-transitions` before launch to enable
startup acquisition plus the narrow mid-movie trial. The original `experimental`
value remains startup-only. No environment change is persisted system-wide.

The added case is a material inward change from an already native, full-width
full raster or taller letterboxed picture (for example 1.90:1) to a narrower
full-width picture (for example 2.40:1). Existing geometry and 5% inward aspect
deadbands remain in force. Both top and bottom must move inward. Current native
side scans must complete without a side-bar proposal, so ambiguous pillarbox or
all-sided inset content cannot use this path.

Each candidate carries a certificate bound to the exact native established
base, guarded target, current source generation and sequence, and original
exterior reference. Base changes, cuts, gaps, and context changes invalidate the
proof. Pending inward candidates retain the old presentation, including known
full raster. Only normal live confirmation/publication transfers authority.
The certificate can cover the failed vertical edge proof; it does not waive
orthogonal conflicts, moving-picture ownership, caption envelopes, near-black
acquisition, current excluded-pixel safety, or the normal publication gates.
Movement is evaluated from current native evidence, never the frozen target.

A previously native scope remembered in history cannot grant native provenance
to an experimental return to scope. Experimental targets stay live-only and out
of native format history. Native outward expansion to IMAX/full raster remains
unchanged, including its existing lookahead. This allows native 1.9 -> experimental
scope -> native 1.9 cycles. A second experimental transition directly from an
experimental base is intentionally unsupported until native evidence establishes
that base.

This trial does not add sparse pillarbox acquisition, sparse outward expansion,
or permission to zoom into an all-sided composition. Scene cuts reset proof;
they are not crop authority. Bars in VP's output layout are not source evidence.
It may still abstain on dark scenes. Matching a synthetic mixed-aspect sequence
does not prove a particular Eternals shot is solved; deployment and replay logs
must establish that separately. The mode performs live analysis on each accepted
frame while enabled, so candidate logs include sampling costs for the trial.

## Startup-only validation (before transition extension)

- RED adapter-disabled check: 26 tests executed, 17 failed at the missing
  source-derived candidate as expected, 9 passed. Source restored afterward.
- Final focused run: 27/27 passed, including real startup near-black bootstrap
  through ordinary publication and a caption appearing on the fourth proof
  frame.
- Full regression suite: 1534/1534 passed. Existing test files/expectations
  were unchanged. The one new caption fixture was corrected to call the same
  chroma-aligned presentation-envelope composition used by the renderer;
  caption visibility and logical-geometry assertions were retained.
- Final clean x64 Release solution build: successful, 0 errors. An earlier
  incremental MSVC debug-information failure was resolved by clean rebuild.
- Independent review found and verified fixes for cooldown-suppressed cut
  resets and a startup gate that could otherwise reopen after analysis reset.
- Synthetic 4K helper medians: 1.42-1.58 ms discovery, 0.52-0.67 ms intervening
  validation across P010/P210/native v210. This includes helper retention work,
  but excludes pre-existing caller extraction/rendering. Disabled calls read
  no extra pixels. The identical source files were hashed with the benchmark.
- No deployment, configuration change, merge, or actual playback validation
  was performed for this implementation.

Build/test logs, runtime pair hashes, source hashes, benchmark report and
identity records are in `artifacts/sparse-boundary-experiment`.

## Transition extension validation

- Transition-only RED check: 19 tests ran with just the added transition adapter
  disabled; 14 failed at missing source-derived transition candidates and 5
  negative/default-mode tests passed. The complete helper was restored and its
  pre-test hash verified before the clean GREEN build.
- All 19 new tests pass, and the full suite passes 1553/1553. The preceding 1534
  tests and their expectations are unchanged.
- A clean x64 Release solution build succeeds. Native full-frame/IMAX -> sparse
  scope -> native IMAX/full cycles retain normal admission and outward timing.
- Independent review caught and fixed two integration seams: expired cached
  scene snapshots must not veto forever, and pending contained candidates must
  not relabel or rearm the old full-raster contract. The latter has a regression
  covering a first pending vote followed by real black pixels and abort.
- Deployment uses the explicit `experimental-transitions` process environment,
  matched host/renderer Release artifacts, and a backup. Configuration files are
  preserved. Runtime identity, checks, and deployment records are under
  `artifacts/sparse-boundary-transitions`; real Eternals replay remains separate
  from these synthetic checks.

## Dense verification of thin startup boundaries (September 25)

The startup experiment can now perform bounded source-pixel verification when a
coarse boundary proposal misses narrow detail outside it. At least one original
coarse edge must have connected support, inward continuation and a clean sampled
exterior. Its opposing inset only nominates a search strip; an actual sampled
row with picture evidence must supply the opposite edge. Two weak edges cannot
bootstrap each other, and no AR constants or remembered formats are used.

For each candidate coarse column, refinement examines its midpoint-bounded
source-x cell at the candidate row, the adjacent exterior row and three inward
sampled rows. A connected run through the nominated coarse point must have at
least max(4, coarse-column-spacing / 4) physical source pixels (7 at UHD), measured
inside/outside contrast, and at least 75% inward continuation. These are local
experimental verification requirements, not changes to normal detector cutoffs.
The helper aborts without replacing the coarse observation if sampling fails or
would exceed 8,192 additional source reads. It runs at the existing 200 ms
startup discovery cadence; the native and mid-movie transition paths do not use it.

A verified patch retains ONE original coarse-column identity. Its additional
fine pixels do not create extra coverage or time votes. The existing four fresh
observations / 600 ms minimum, three-second bounded history, bilateral coverage,
changing masks, context resets, frozen exterior checks, near-black guard and
ordinary publication rules remain unchanged. Evidence accumulates across motion
along a stable edge, not across drifting edge coordinates. A static source with
changing dither or unrelated interior motion cannot satisfy the spatial gate.

The new thin-edge synthetic case must traverse at least 13 original columns
before its weak edge reaches 10% coverage. The test runs for about 2.6 seconds of
simulated source time. Actual slow motion may still abstain within the existing
three-second window; this implementation does not widen that window to make the
Alien sample pass. Moving artwork against black remains semantically ambiguous.

`Alpha sparse-boundary refinement` records coarse and measured bounds, actual
read count, verified original-column counts, physical run length, history age,
coverage/new coverage, distinct masks and separate refinement/hypothesis/crop
reasons. The ordinary diagnostic session remains coarse-only unless the startup
experiment explicitly supplies its live source. Raw native measurements and
rejection reasons are preserved. Logs can therefore distinguish a newly measured
edge from sufficient temporal evidence and from actual crop publication.

The positive regression was first run against the unchanged implementation and
failed at the expected missing startup candidate. The same test now passes.
Additional tests cover source-format parity, thin stars/stripes, symmetry-only
inference, occupied exteriors, noise/repeats, drift/cuts, default-off behavior,
near-black vetoes, current caption arrival and read-budget exhaustion. Full
validation receipts are under `artifacts/sparse-refinement-20260925/`. These tests
are not a substitute for an actual moving-source replay.


### Saved sparse crop and near-black episode recovery (September 25)

A published sparse startup crop keeps its experimental origin. Previously this could strand near-black episode recovery: that recovery required a native stable association, while the episode deliberately suppressed new native acquisition. A renderer restart concealed the conflict.

Current raw native bars can now corroborate only the exact saved sparse entry. They must be contained by it, match raster and trusted axes, have no failed axis, and fit the existing stable geometry tolerance. The current excluded-band certificate, seven fresh-frame dwell, source/sequence/epoch checks, brightness and outward-content vetoes remain required. This applies to retained-crop and full-raster episodes, without teaching history, changing origin, acquiring a new crop or relaxing thresholds. Raw retention evidence is used before hypothesis/constrained classifications. Regular episode logs include entry_origin and sparse_native_reaffirmed.

Validation: 10 added regression tests; 1,671/1,671 x64 Release tests passed. See artifacts/near-black-reacquisition-20260925/REVIEW.md for the captured failure, red/green evidence, deployment and remaining playback validation.
