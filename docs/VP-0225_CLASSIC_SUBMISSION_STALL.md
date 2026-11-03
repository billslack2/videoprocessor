# VP-0225 Classic submission stalls

## Evidence and scope

The 2026-11-02 18-55-54 desktop recording correlates to the retained VP log:
source 5668 submits [16,410,3824,2030]; 5669-5672 are withheld, then 5673
submits [16,438,3824,2058]. Several subsequent updates are about 167 ms apart.
Capture remains approximately 23.976 fps without input misses and measured GPU
work is approximately 4 ms. The missing submissions are distinct from normal
23.976-to-59.95 Hz display cadence. The recording is not a native pixel fixture.

Three Luna xhigh research lanes independently reviewed historical behavior,
current admission and the incident/reproduction. They discussed the current
candidate and configured-fill ownership before selecting a correction.

## Historical comparison

Direct GitHub verification identifies remote tag 1.4-beta-rc4 as
6d5f8ad4241c24b750b96d2e3f54abb458bc7986 and remote 1.4-beta-rc6 as
7af23836ae937353edb50a5b1b3f8e9176634480. Both have no
SubtitleCanvasMode/waitForSubtitleProcessing/selectedCanvasWait whole-output
gate. The initial local RC6 tag was stale at release ancestor
e3a34b6c9358485e2d975d7b76c7be1ad05baec2; the remote RC6 merge has identical
crop-policy code and only seven renderer lines differ, with no canvas gate.
The wait machinery first appears in Nov 2 commit 01c1e13f, whose
purpose was to prevent subtitles from resizing the movie. The fixed movie
canvas requirement remains valid; withholding prolonged source progression is
not an acceptable way to satisfy it. This identifies a post-RC6 regression
mechanism, not evidence that every older release was smooth under all conditions.

The existing 788554a100afe0af2352b904fe1870d80fb75bb7 repair permits current raw
continuation for UNKNOWN Black/Reconstruction inspection without new detection
or caption proof. It deliberately leaves Classic unchanged. It and the canvas
machinery are on the unmerged AR work branch, not on current beta a591ad3b.
This worktree was created from freshly queried/fetched beta, then explicitly
merged that dependency as 24ae1e2, preserving the AR agent's worktree.

## Classic failure mechanisms

CommitSubmittedNativeScopeAnchor records the actual final renderImage.crop,
including configured fill. The prior-pan checks compare that rectangle's X
bounds and height to the detector's native bounds instead of the configured
viewport. At source5668 the native rectangle is [0,270,3840,1890], while fill
has trimmed X to [16,3824]. The comparison therefore rejects an established
valid pan. Its vertical offset must likewise be measured from the configured
viewport top, especially when configured fill also trims vertically.

Changing that comparison alone is insufficient: source5669 has exact-current
text candidate [810,1926,3034,2042] while semantic state is UNKNOWN. Its bottom
extends below the last pose's 2030, so blindly retaining that pose clips it.
The source has raw picture measurement/retention and current coarse visible
extent as well, but coarse extent ends around2028 and does not cover the full
candidate. The candidate is copied only from a same-identity analyzed text
observation. Its pixels can constrain a same-size pan without making it a
confirmed cue, caption absence, cleanup authority, or native picture evidence.

Sources5671/5672 lack explicit candidate proof in the retained log. Never carry
the 5669 candidate into those sources as if newly measured. Continuation there
must rely on their own exact raw visibility and compatible viewport; otherwise
an explicit skip remains a limitation to investigate rather than invented proof.

## Correction and validation boundary

Preserve the measured native crop, configured movie size, actual source identity,
controller timing, bottom-content priority and genuine picture expansion guards.
Compare prior poses to the current configured viewport, and constrain pending
Classic placement by exact-current raw occupancy plus the complete current text
candidate when present. Keep UNKNOWN semantic state and withhold native/fit
certificates for this unprocessed continuation. No extra full detector or text
scan belongs in the presentation path. Stale/missing/infeasible evidence must
not acquire new authority. Test source progression, candidate coverage and
context mismatches; passing policy tests alone is not live playback acceptance.

The confirmed-caption path preserves the same exact-current raw occupied-side
margin: changing UNKNOWN to PRESENT must not drop that bound and reverse a safe
pan. The original source frame/capture identity is retained from the selected
snapshot in diagnostics; delayed output never borrows the current input's identity.
Runtime tests join unprocessed output to exact-source admissions and previously
measured native geometry without inventing a presentation certificate. Existing
per-output caption visibility, shader readiness, native dimensions and repeat
identity assertions remain in force for Classic, Black and Reconstruction.

No deployment or active configuration changes are part of this work.
