# Normal-log picture decision diagnostics

These records are enabled with ordinary VP logging. No verbose mode, debug
switch, or pixel capture is required. They only format evidence already computed
by the detector; they do not resample pixels, advance confirmation, wait for queue
frames, or change geometry, subtitle timing, thresholds, or policy.

## Correlation and interpretation

Join by renderer `instance`, source `generation`, `sequence`, and viewport `epoch`.
Preview records also identify format and lookahead policy generations. Sequence
numbers are source-frame identities, not elapsed time; repeated frames do not
represent additional proof. Use `Alpha source crop` / `Alpha final layout` for the
actual applied rectangle and subtitle shift. The new decision event is upstream
of final presentation: `published=1` does not by itself mean that geometry was
shown. `requested_shift` is the stored subtitle presentation request at this stage,
not the final applied shift.

- `Alpha picture decision`: pre-constraint measured class/bounds, constrained
  candidate, measured presentation base, axis rejection reasons, near-black
  acquisition gate, first blocking stage, live proof and prior proof identities,
  queued proof present/valid/accepted, scheduled and model admission outcomes,
  subtitle owner/request/drift and configured hold/engage/release durations.
- `Alpha picture decision edges`: current expansion strips when available,
  otherwise whole excluded bands. The region, strip base and candidate are
  explicit: a mismatched strip certificate must not be interpreted as proof for
  another rectangle. Zero-valued evidence with `valid=0` is unavailable, not black.
  Values include coverage, brightness, texture, continuity and neutral chroma.
- `Alpha picture preview status`: configured ceiling versus actual available
  future frames, selected/analyzed frames, proof sample count, owner eligibility,
  candidate geometry/axis reasons, queue membership/continuity outcome and whether
  a proof was built. Future counts exclude the current frame; selected/proof
  sample counts include it. Outward proof requires three total source frames.
- `Alpha picture preview sample`: up to three contributing samples when an
  inspected proof fails, including physical source/timestamp identities, trust,
  darkness, axis failures, retention validity and top/bottom strip statistics.

Typical distinctions are `candidate-untrusted` versus `outward-proof`,
`presentation-owner`, `scheduled-admission`, and `near-black-acquisition`.
Preview further separates configured depth, physical shortage, unselected/missing
samples, no two-edge candidate, and rejected complete proof. `proof-rejected`
still requires reading its sample fields; it is not a claim that a particular
sample was the sole cause. Stage is a first-blocker summary; individual flags
retain simultaneous causes.

Live `outward` reasons come from existing confirmation branches, including
measurement-step-only, strip mismatch, invalid measurement, weak horizontal or
vertical picture evidence, repeated source, restarted proof and confirmed proof.
They are diagnostics only. Existing broad-picture tests require each expanding
edge to have black fraction <=0.80, continuity <=0.85, and either p90 >=112 or
texture >=12, using 10-bit analysis units. They are not new limits. P010 and native
analysis feed the same live records; queued analysis retains its existing native
format support and reports missing measurements rather than inventing values.

## Volume and limits

Each stream coalesces changes to at most one bundle every 500 ms within a source
context, with a two-second heartbeat during unresolved activity. Inactive steady
state is silent. Context resets can emit immediately. `coalesced` counts changed
or event observations suppressed since the previous bundle, not lost frames.
A brief end during the rate limit is flushed on a subsequent observation. Existing
publication/recovery/final-layout events remain the authoritative event timeline.
The added summaries are sampled diagnostics, not an exhaustive video trace.

No deployment or configuration edit is part of adding these diagnostics.

## Validation

Full x64 Release solution build passed (`artifacts/lookahead/picture-diagnostics-release-build.log`).
All 1,426 native tests passed (`artifacts/lookahead/picture-diagnostics-green.trx`).
New tests cover quiet steady state, hard rate caps during oscillation, repeated
source observations, pending-end flush, heartbeat and source/viewport/clock resets.
Decision-reason tests retain assertions on actual confirmation and authority.
Independent review checked policy isolation, synchronization, formatting and
label semantics. The initial incremental test link hit existing MSVC LNK1103
corrupt debug information; a clean test rebuild and full build succeeded.
Actual playback log volume/interpretation remains to be verified after deployment.
