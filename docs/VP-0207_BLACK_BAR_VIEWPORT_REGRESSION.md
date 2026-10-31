# VP-0207: black-bar content and viewport regression

This note records the rejected live build, the reproduced policy failures, and
the qualification required for the replacement. Matching runtime and motion
receipts establish build qualification; native passes alone do not qualify
deployment or establish physical playback acceptance. Native and actual renderer
tests establish different contracts; neither replaces the user's playback acceptance.

## Reproduction and source identity

The live rejection was against `b52ae03a`, deployed at 13:17 on 31 October 2026.
The preserved evidence is
`E:\codex\diagnostics\blackbar-content-regression-20261031-1325`.
Its `vp.log`, configuration/state copies, installed hashes, and runtime sidecars
identify the rejected setup. The observed UHD input was 3840x2160 v210,
59.940060 Hz, stride-two analysis, SDR Rec.709, range 1 with black code 64.
This is evidence about the captured input, not the streaming service's codec.

During the Classic interval at approximately 13:24:29, the accepted native
picture remained 250..1906, accepted source 694, presentation epoch 5. Its
2.31884 aspect was below the configured 2.32 narrower-fill limit, so this was an
important **off-fill** case. Source 2691 submitted the translated viewport
318..1974. Source 2692, with skipped dense analysis and current raw visible
extent 127..1939, submitted outward visibility fallback 126..1974. Scheduled
source 2694 returned to 318..1974 and skipped source 2695 returned to 126..1974.
Both-bar occupancy existed; the controller's bottom-first translation choice
did not establish permission to discard the upper bar's current content.
These are presentation changes over a stable independently measured native
contract, not evidence of genuine repeated aspect-ratio cuts.

## Causal boundaries

1. **Confirmed translation bypassed current opposite-bar coverage.**
   `HasConfirmedPresentationCandidate` accepted a resolved, matching translation
   without requiring its actual output rectangle to contain current visible
   content outside the accepted native picture. The helper is present at
   `7d9b26df`; its earlier history predates this live rejection. A scheduled
   confirmation could therefore suppress the visibility fallback while the
   same pixels on a skipped source could not. The UHD scheduled/skipped native
   regressions change this ownership condition and preserve the native AR.
   The repair checks actual outside-native coverage before every translated
   confirmed-candidate shortcut. Cropping the opposite edge *inside* the native
   picture remains part of an authorized Classic translation.

2. **Affirmative raw contradiction made the inconsistent route visible.**
   Commit `6181deb9` introduced the `currentContradiction` outward-fallback branch
   in `ResolveUncertainPicturePresentation`. Its intent was to preserve newly
   visible source pixels through uncertain analysis. Combined with the older
   confirmed-translation bypass, it allowed different scheduled/skipped routes
   to select different viewports. Additional RED tests showed that exact current
   visibility could be lost when the coarse nomination was unavailable, and
   that an unresolved full-raster placeholder could inflate a bounded fallback.
   The repair uses exact current visibility even without a valid coarse
   nomination, ignores untrusted placeholder extents, and does not union an
   unqualified fail-open proposal as though it were measured pixels.

3. **Advanced mode geometry was finalized after certification.**
   `SubtitlePresentationBounds` unconditionally undoes Classic translation in
   Black/Reconstruction modes; its preparation arguments did not gate that mode
   rule. At Reconstruction source 3909, composed output was native 250..1906,
   while its certificate still approved translated 342..1998. Actual submission
   rejected that mismatch. At 3910 and 3912, cleanup/glyph cadence rejection
   produced uncomposed raw-content fallback to 2000. Later prepared sources
   could retain the old fallback or return to native via a different route.
   Commit `61d31ede` moves mode canonicalization before policy admission and
   qualification, while preserving raw/FIT/native context and first-current
   picture preservation. `composed=1` alone is never erasure or AR authority.

4. **Correct certificates still allowed processing-state viewport changes.**
   After the preceding repair, a native sequence against compiled `61d31ede`
   established prepared-native output at 3909, committed raw-caption fallback
   250..2000 at 3910, and alternated current prepared/unprepared sources with the
   same persistent raw bottom-band occupancy. The first prepared source 3911
   shrank to 1906 instead of retaining 2000. This is a separate stability gap:
   current successful erasure is not evidence that the raw cue ended. A cold
   startup control also proved that the *first* prepared native output must be
   allowed to initialize even after a long fallback warmup. The pending repair
   records typed, successfully submitted prepared-native initialization in the
   same mode/native context, then permits only an already submitted bounded
   visibility envelope to persist while exact current raw occupancy remains.

These statements distinguish demonstrated paths and commit contents from a
first-bad-commit claim. There has been no complete historical playback bisect.
In particular, `6181deb9` is the identified introduction of the contradiction
branch, not proof that it alone introduced every symptom; the translation
shortcut and late mode rule existed earlier.

## Evidence and test-first results

The evidence directory contains `b52-red.trx`, `b52-red-02.trx`,
`b52-red-03.trx`, and `b52-v210-red.trx`, with corresponding logs and saved
patches. These separate intended failing assertions from valid single-bar and
foreign-context controls. The third RED run reported 13 tests: five controls
passed and eight regressions failed against the unchanged rejected library.
Renderer before/after hash records preserve that baseline distinction.

`61d-sequence-red.trx` and `61d-startup-control-red.trx` record the later
processing-state sequence. The corrected sequence fails at prepared source
3911 (expected bottom 2000, actual 1906), while the cold-start initialization
control passes. `61d-corrected-warm-controls.trx` retains the accompanying warm
controls. These RED results are evidence of missing behavior, not a green
qualification receipt for the current changes.

The full-chain native tests execute Evaluate, recovery, admission, uncertain
presentation resolution, final mode bounds, and successful actual submission.
They check exact current certificate bounds and retain accepted native source
694. New controls cover failed/repeated/unsubmitted initialization, typed mode,
foreign generation/epoch/format/domain/native contract, both authorized native
fill axes, absent/stale raw witnesses, per-edge retirement, and immediate
first-current genuine picture expansion.

## Why earlier green coverage missed this

Earlier tests covered valid single-bar translation, natural skipped scans,
configured wider/narrower fill, controller engage/release drift, cadence proof,
and individual admission/certificate rules. Those cases did not jointly cover
off-fill UHD geometry, current content in both bars, a scheduled confirmation
versus a skipped current-pixel path, and advanced composition success alternating
with safe proof rejection. Simplified native fixtures could manually preserve
an admission certificate without exercising final mode geometry or the actual
submission commit. A short sequence of fully prepared frames also could not
expose a viewport dependent on composition availability.

The actual UHD fixtures now include both-bar UI, V210, residual UI after
Reconstruction, and captions crossing the native edge over moving picture.
Their cleanup coverage assertions distinguish actual row footprints from a
padded bounding rectangle. Changes inside unowned erase pixels and loss of
owned glyph core must still reject reuse; motion outside the actual footprint
and bounded allowed antialias fringe have separate controls. These tests do not
relax cadence pixel thresholds or authorize erasing unowned moving picture.

## Presentation stability and safety contract

The new lineage is presentation-only. A current typed prepared-native marker is
minted only from exact current prepared evidence. Initialization requires its
matching actual native or qualified configured-fill submission. A failed,
repeated, translated, FIT, or visibility-fallback submission cannot initialize
it. The original initialized source is carried without renewal only through the
same native contract, generation, epoch, format/domain and frozen packet mode.
Scope/style resets clear this lineage without clearing native detector authority.

Retention requires a real previously submitted visibility-fallback or confirmed
UI/bar-content FIT rectangle,
exact current pre-erasure band safety and current post-erasure-safe native
output. It can preserve that viewport, not enlarge it through unknown pixels.
Current raw-safe sides trim obsolete outward margins independently. All raw
bands safe disables this hold. Newly measured larger raw visibility and the
first genuine current picture proof take priority immediately. No new caption
ownership, erasure permission, native vote, or caption lease follows from keeping
the viewport stable. Startup without prepared-native initialization remains free
to admit its first current prepared native output.

## Required qualification and remaining acceptance

Mandatory runtime qualification requires **727 native tests and 20 actual renderer
tests**, with the declared dependency variants and matching source/build identity.
The actual matrix contains the six existing Classic translation/lifecycle cases
and fourteen UHD off-fill/both-bar/Black/Reconstruction/V210/moving-cleanup cases across
standard and beta renderers. Source compilation, native execution, renderer
execution, runtime hashes and gate receipts must be checked before deployment.
Independent review identified and reproduced an additional origin distinction:
an actually submitted confirmed UI/bar-content FIT can have the same bounded
250..2000 viewport and initialized native lineage. `lineage-extra-red.trx`
proved that this origin shrank to 1906 on the next prepared source (expected
2000), while five controls passed, including the actual renderer's full-raster
fallback admission bridge. The repair accepts the exactly certified,
actually submitted confirmed-FIT envelope under the same bounded current raw
evidence, then emits a new visibility-only certificate without copying old FIT
or caption ownership. No no-crop admission exception was needed. Full qualification remains mandatory; RED evidence is not a pass receipt.

The fixtures observe 24 distinct settled sources to detect instability. **There
is no new 24-frame runtime waiting gate.** Transition responsiveness and first
current picture preservation remain explicit requirements.

After automated qualification, physical playback still must confirm stable
Classic, Black and Reconstruction output, single-bar translation, both-bar visibility,
true scope/IMAX transitions, profile changes and moving edge-crossing subtitles
with the user's unchanged active configuration. Automated green results alone
must not be reported as that acceptance.

## Native repair evidence

`native-lineage-green-02.trx` passes all 725 mandatory native methods. The
matching gate metadata tests verify exact execution identity and reject absent,
failed, stale, duplicate or tampered evidence. Both exact-pixel P210/V210 native
preflights pass acquisition and measured native boundaries, establish an actual
cleanup footprint in moving native pixels without a finite card, and reject
changed unowned cleanup samples. The renderer fixtures separately retire a
raw-clear top margin while the bottom cue persists, then remove all bar content
and require current raw absence plus exact native output before a new Classic
cue onset. Runtime and dual dependency GPU/cue receipts remain separate from
these native results and from physical playback acceptance.

## Current raw output during an unknown-caption hold

The moving V210 cross-edge renderer replay caught a second unsafe hold path.
At source 2810 in `373-cross-diagnostics.trx`, the same renderer instance reported
an uncomposed, cadence-skipped current source and a native 250..1906 viewport
owned by `retain-submitted-picture-during-caption-unknown`, while the raw cue
extended to 1940. The unknown hold excluded the previous erased footprint from
current inspection, suppressed raw visibility, and overrode the safety view.
Historical erasure bounds can test independent contradictions; they cannot
establish that current raw caption pixels have been removed.

`373-unknown-hold-red.trx` reproduces that public-helper defect before repair:
the persistent unprocessed caption inside the old footprint fails the required
no-hold assertion; the genuinely raw-clear positive and original hold control
pass. The repair requires completed current immutable raw-band safety on the
exact native base, with all four sides clear, before unknown hold can retain
the prior view. Occupied or unavailable raw evidence uses ordinary visibility
recovery. Lease limits, current picture vetoes and glyph/cleanup thresholds stay
unchanged. Old positive fixtures must actually clear current raw bars while
retaining the independently submitted prior proof.

The new opt-in `submitted-presentation` diagnostic joins final composition,
selected identity, mode, actual crop and certificate after every successful
render and submission. Preparation records can describe another attempt and
must not be joined to output only by source number. Renderer tests inspect
every successful output record, including unchanged geometry, and distinguish
applied pose from the controller requested translation. Earlier requested-pose
assertion failures and interrupted runs do not qualify a build. Final runtime
and dual-dependency GPU receipts remain required.

`unknown-hold-green-native.trx` passes all 727 mandatory native methods after
the current-raw safety repair, and `unknown-hold-gate-metadata.log` verifies
exact required execution and fail-closed qualification. The unknown-hold
footprint-subtraction helper existed in `c3cff54ce` (19 October); this identifies
the unsafe permission path, not a completed first-bad playback bisect.
