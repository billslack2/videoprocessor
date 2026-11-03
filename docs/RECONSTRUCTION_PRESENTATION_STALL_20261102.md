# Unknown subtitle inspection must not freeze video

The live Reconstruction renderer kept consuming input and completing source
analysis, but submitted source 286 indefinitely while input advanced past 1608.
Every following admission waited for subtitle processing even though the current
observation was UNKNOWN and there was no current text candidate. This was semantic
presentation starvation, not evidence of a blocked GPU or logging writer.

The fixed movie-canvas rule must constrain geometry without freezing source time.
For Black/Reconstruction, a missing inspection with no text candidate now submits
the current raw frame at an already successfully submitted accepted native aperture.
The submitted owner must match generation, presentation epoch, format and analysis
domain; startup cannot invent a continuation viewport. This route has no
caption absence, erasure, composition, native presentation certificate or fit
ownership. It does not renew accepted detection. Positive current caption
candidates still require processing; actual current picture expansion and foreign
native contexts continue through their existing guards. Classic and its bottom
priority are unchanged.

`subtitle-canvas-admission` traces wait/unprocessed/composed flags, selected mode,
caption state and the last submitted sequence, with native/displayed rectangles.
The existing text admission record adds `unprocessed`; pending processed captions
also set an explicit render-skip reason. This distinguishes missing classification
from actual in-flight composition in future recordings.

The native regression exercises 1600 consecutive UNKNOWN inspections without
minting proofs, then verifies that a positive caption or candidate still waits.
Actual standard/beta renderer regressions cover Black and Reconstruction, require
more than 20 distinct current submitted sources during a 3.2-second uncertain
interval, join those submissions to actual unprocessed admission records and reject
whole-image repeats. They also verify subsequent real full-raster expansion and
return to scope. The old proxy-only repeated-sequence counting is not used as
progress evidence for these new checks.

The previous relative dark-edge repair remains intact. No configuration edit or
permanent profile workaround is part of this source fix. Full Release qualification
and both motion suites remain deployment gates.
