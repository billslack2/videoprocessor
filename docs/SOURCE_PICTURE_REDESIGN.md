# Source-picture redesign: first implementation stage

This stage implements the reviewed CPU measurement prototype, native capture replay,
and ready-lookahead budget repair. It does **not** replace live geometry/crop authority.

## Measurement order

1. Validate the canonical native source and retain its source-coordinate domain.
2. Build bounded sampled topology with explicit sampling coverage. Unsampled pixels
   are not certified empty and this map grants no crop permission.
3. Retain competing local matte-to-picture departures on each edge. A gradual
   brightness drift is not an abrupt boundary; paired chroma cannot move a luma edge.
4. Identify repeated glyph rows separately from picture-edge support.
5. Reconcile candidate rectangles against clipped exterior occupancy. A connector
   into a panel cannot make its exterior header disappear. Contradiction alone does
   not establish another rectangle: canvas inference requires independent layout support.
6. Report measured coordinates, supported canvas inference with unresolved margins,
   partial evidence, or missing work separately. Ratios come from coordinates; no
   standard aspect or screen geometry is an input.

The topology overview is at most 480 by 270 sampled cells with at most 512 components.
Exhausting the component limit produces an incomplete result, never a trusted rectangle.
A sparse overview is not a guarantee that one-pixel objects are observed at every phase.
Exact discard verification remains a separate responsibility.

## Runtime integration and bounds

`VP_SOURCE_PICTURE_SHADOW=1` enables diagnostic evaluation on the existing native
analysis worker. It defaults off. A shared admission policy allows at most one new
measurement per 500 ms, with a 12 ms cooperative deadline. No additional work queue,
full-raster copy, render-thread scan, or crop/AR publication is introduced. Facts live
inside the existing exact-source worker result and emit `source-picture-shadow` records.
The limits bound optional work; expiration is missing evidence, not a negative result.
The observation's retained topology is at most 259,200 label bytes, plus components
and edge candidates. Temporary CPU scratch is released with the evaluation.

The separate production timing repair consumes already-ready exact source evidence
when the optional fresh-scan deadline is zero or expires. The cache-only pass cannot
submit native, subtitle, or component scans. Repeated displayed inputs do not gain
new votes. Existing source identity, pending-prefix, epoch and effective-source checks
still govern publication. No new confirmation dwell is added.

## Validation and migration gate

All new functional and conversion contracts are registered in the mandatory native
gate. `SourcePictureOptionalNativeCaptureReplay` additionally accepts private captures
through `VP_SOURCE_PICTURE_CAPTURE_DIRECTORY`, `VP_SOURCE_PICTURE_CAPTURE_EXPECTED_EDGES`
(left,top,right,bottom), and `VP_SOURCE_PICTURE_CAPTURE_EXPECTED_OUTCOME`. An absent
private directory explicitly records zero capture coverage; it is not a corpus pass.
Do not commit captured movie pixels.

The next authority migration must carry measured geometry and uncertainty separately
from the conservative retained aperture and permission to discard pixels. Overwriting
`MakeActivePictureObservation.bounds` while retaining its trusted classification would
silently grant crop authority and is prohibited. Preserve current subtitle proof bases.
Consolidate source events and commit ownership before removing legacy inference routes.
Final acceptance still needs full-rate scope/IMAX transition capture and live playback;
passing paused native frames is not proof of those transitions.

The first optimized private replay exercised 479 saved frame instances in seven capture
sets with zero mismatches (including 15 byte-identical paused scope instances). Average
CPU measurement time was 5.5–9.4 ms by set; observed peaks reached 14.1 ms. These are
unbudgeted CPU replay timings, not live latency guarantees. The 12 ms opt-in deadline
may intentionally leave slow packets incomplete; its admission rate stays two per second.
Further optimization and full-rate temporal evidence remain prerequisites to replacing
the live detector. No private pixels are included in this repository.
