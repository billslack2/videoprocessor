# VP-0189: crop retention cannot acquire picture geometry

On 2026-09-19 at 21:24:59, the Eternals opening-text replay changed from full
raster to 0,476-3840,1688 (3.168:1) at source sequence 984. The log called this
"trusted crop retained while bar refinement confirms", despite previous
presentation being full raster and current retention bands being unsafe.
Subtitle translation confirmation was also pending. Sequence 994 returned to
full raster, making the excursion about 0.42 seconds at 24 fps.

The evidence is in the preserved vp-crop-incident-20260919-212606.log, lines
9951 and 9966. The user could not reproduce the visual event after rewinding;
the log demonstrates the presentation transition, not a definitive subtitle
classification for the rendered text.

## Correction

After ordinary crop evaluation and recovery, require either current picture
acquisition authority or the identical previously admitted logical crop.
Temporary retention and subtitle owners cannot introduce a crop that has never
been presented. Subtitle adjustments around an admitted picture retain their
existing behavior; genuine current aspect changes can acquire immediately.

Store this history independently of diagnostics. Preserve it through temporary
full-raster withdrawals so recovery of the established crop still works. Clear
it on source generation, raster or presentation-epoch changes, automatic-crop
disable, or authoritative full raster. The history grants no exemption from
existing pixel-safety or recovery decisions: it only filters candidates that
those decisions would otherwise apply.

Apply the check before fill/NLS and suppress the NLS geometry fallback when a
candidate is blocked. Explicit fixed-aspect configuration remains authoritative.
Log blocking-state transitions with renderer instance, generation, sequence,
epoch, previous/candidate geometry, owner and current picture support. Existing
unresolved-event summaries include the wait. No pixel scan or timer is added.

## Validation

The initial seam passed candidates through unchanged, preserving existing
presentation behavior. Four new regression tests failed; the genuine-aspect
positive control passed after correcting its synthetic raster fixture.

Coverage includes:

- The ten-frame scrolling-text excursion and subsequent normal acquisition.
- Twelve retention/subtitle paths: blocked before acquisition, allowed after it.
- 2.20 -> 1.43 -> 2.20 and 2.35 -> 1.90 -> 2.35 at a fixed test raster.
- Changed bounds, axes, source, raster and presentation epoch.
- Temporary withdrawal versus authoritative full-raster reset.
- The existing four-pixel sampling and provisional-recovery scenarios, composed
  with the new admission step, at 23.976, 24, 59.94 and 60 fps.

Build/test transcripts are preserved under
C:\Users\bslac\AppData\Local\Temp\vp-retention-admission.
The first targeted corrected-code run passed 203/204; its remaining failure was
a hardcoded 3840-width assertion in the new 1920-width reset fixture, corrected
to assert the actual raster size. A subsequent incremental full build stopped
with LNK1103 (corrupt debug information). The clean x64 Release rebuild then succeeded, and all 1307 tests passed (admission-full-clean.trx).
Live replay validation remains outstanding. No deployment or merge is implied.
