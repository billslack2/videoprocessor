# Retiring subtitle translation to current fit

## Report and baseline evidence

The 2026-09-22 replay at 15:46:59, renderer instance
`{38CE2C33-1D06-486D-B968-F5F212ADE700}`, source generation 1, retained
logical scope bounds `0,276-3840,1884` with a 192-pixel subtitle translation.
Dense FIT was confirmed at source sequence 4218 but the translation still owned
presentation. At 4225 the hold expired. The renderer cleared its subtitle
state and showed centered scope for three frames before bounded FIT
`0,54-3840,2106` at 4228. This is approximately 125 ms at 24 fps.
The full-raster recovery fix was working: the intermediate state was centered
scope, not an uncropped full raster.

The initial outward crop proof was independently uncertain. Sequence 4213 had
top-strip p90 106 and texture 10.6, below the existing alternative requirements
112 or 12. The following queued samples also lacked trusted bar evidence.
This correction must not lower those requirements or publish provisional
aspect geometry.

## Narrow correction

After a positive subtitle hold expires, a recent confirmed FIT can request a
fresh dense inspection against the same trusted base. Its last sample must be
one to three source frames old, matching the existing dense scan cadence.
The source, stored base, and current provisional envelope must match; the
envelope must expand both vertical edges and preserve horizontal extent. The
trusted base must span the raster width and have actual top and bottom bars.
Near-black episodes and current moving transitions exclude this path.

Historical FIT only authorizes inspection. The current dense scan must again
produce confirmed FIT and detect content on both edges before it can retire
translation ownership. A one-edge result cannot borrow the old subtitle's
opposite edge. Successful handoff clears only translation drift, preserving
the new FIT evidence. Current envelope containment and existing final crop
admission remain in charge of displayed bounds. Logical aspect authority,
queue lookahead, black thresholds, subtitle hold duration and normal release
behavior are unchanged.

## Validation

- RED: two expected regression failures in
  `artifacts/lookahead/retiring-fit-red.trx`: denied current inspection and
  confirmed FIT discarded by expired translation ownership. Fourteen other
  tests selected by the filter passed.
- GREEN: both reproductions pass; the complete native suite passes **1,430/1,430**
  in `artifacts/lookahead/retiring-fit-green-full.trx`.
- Final x64 Release solution build succeeded:
  `artifacts/lookahead/retiring-fit-final-release-build.log`.
- Tests cover recent sample ages 1..3, 32 incompatible eligibility conditions,
  10 ownership vetoes, fresh negative/one-edge scan results, and composed final
  presentation under the actual provisional-authority gap. Current confirmed
  FIT resolves the inspection bridge, displays bounded `54..2106`, and preserves
  logical scope `276..1884`.
- Independent design, implementation and QA reviews found no blocking issues.
  Review checked the renderer-owned drift reset and preservation of fresh dense
  evidence; the native composed test does not instantiate the GPU renderer.
- Initial incremental test linking hit existing MSVC LNK1103 corrupt debug
  information. Clean test rebuild succeeded. Existing conversion and Qt runtime
  discovery warnings remain; there were no build errors in the final build.

No deployment or user configuration change was made. A fresh playback of the
reported transition is still needed to confirm visual improvement. The log
snapshot does not contain every source pixel and cannot prove the precise live
handoff frame in advance. This targets the extra intermediate recentering;
weak-evidence delays before actual aspect acceptance retain their existing rules.
