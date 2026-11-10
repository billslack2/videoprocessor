# VP-0212: madVR-owned source geometry for madVR NLS

## Behavior

madVR NLS now queries IMadVRInfo on the graph owner, using only originalVideoSize
and videoCropRect for source geometry. The crop must be nonempty, inside the
negotiated raster, and refer to exactly that raster size. Both values are read
again to reject feedback that changes during the query. Missing interfaces,
failed/non-S_OK queries, size mismatches, empty/outside bounds or inconsistent
reads produce Waiting, remove NLS mapping and restore native output through the
existing coherent shader/media-DAR transaction. The request remains armed.
There is no VP detector fallback and no invented full-raster rectangle. A
successfully reported full raster is valid madVR feedback and can engage NLS.

madVR owns detection and temporal stabilization. VP does not add a second
classification or dwell; changed/lost feedback withdraws the previous rectangle
on the next shader refresh. A graph-owned geometry serial advances on bounds,
aspect or availability changes, keeping the existing shader refresh deduplication.
Graph reconstruction resets the observer. Existing controlled output-contract
replacement can preserve an already prepared rectangle/DAR pair; refresh then
queries the new madVR instance rather than rebuilding coordinates from scalar AR.

Source aspect is crop width/height in original source pixels, matching VP's
current square-pixel DirectShow source contract. arAdjustedVideoSize,
videoOutputRect and croppedVideoOutputRect are deliberately excluded: NLS changes
the negotiated display aspect and would otherwise feed its own output back into
the source decision. The unchanged pre-resize shader maps within the same crop,
preserves encoded bars, and leaves madVR responsible for removing bars once.

Matching 2.35:1 source/target passes through within tolerance. Narrower/wider
content still obeys the selected direction, tolerance and stretch ceiling.
Tolerance is an aspect deadband, not black pixel sensitivity.

## Contract and limitations

Verified against the installed madVR developer header:
C:\madvr210\developers\interfaces\mvrInterfaces.h, lines 313-317.
The published header identifies videoCropRect as a crop of originalVideoSize
and separates it from AR-adjusted and final output geometry. Developer notes:
https://madshi.net/89notesForDevs.pdf

madVR black-bar detection must be configured to report the intended crop.
IMadVRInfo has no detector confidence or source-frame timestamp: valid full-raster
feedback cannot be distinguished from a detector that has not found bars yet.
Repeated reads detect an observed change, not atomic per-frame synchronization.
The policy cannot correct a wrong crop reported consistently by madVR. Disabled
bar detection can therefore leave letterboxed content reported as full raster.

No VP Renderer code, detector, shader, configuration defaults or active deployed
settings change. Non-NLS legacy conditional shader guards retain their existing
VP detector behavior.

## Validation

Native regressions exercise the actual IMadVRInfo reader using a COM stub:
matching scope, explicit full raster versus missing feedback, pillarbox/asymmetric
bounds, narrower/wider direction and safe fit, invalid and torn reads, output-DAR
independence, availability serials and native output contract withdrawal. Existing
shader/runtime tests cover coherent output plans and controlled replacement.
Build/test result and exact commit are recorded in the authoritative story.

Live acceptance is still required with madVR: 2.35/2.39 to matching target,
16:9 to scope, 4:3 safe fit, asymmetric bars, mixed-aspect transitions, disabled
bar detection, renderer/source changes, and dynamic media-DAR rejection/rebuild.
Check log geometry=madvr-videoCropRect, madVR crop telemetry, NLS mode, visible
framing and absence of output-aspect oscillation. Unit tests are not physical
playback acceptance. No deployment is part of this implementation.
