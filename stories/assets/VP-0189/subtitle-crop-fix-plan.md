# VP-0189: subtitle/crop arbitration review and fix plan

Reviewed 2026-09-17 against deployed source a7dc020a91c9c5e32502e58db4ac47af5e7fe6fb. Scope is a plan, not an implementation or deployment. Review consists of the recording, source control flow, existing tests, and a compiled probe of the production policy and transition model. It is not an independent external review.

## Outcome we need

A subtitle appearing outside the established picture must be handled as visible overlay content without becoming evidence for a different movie aspect ratio. A genuine change in picture format must still expose the new picture promptly. Sampling variation must not move the displayed crop. A black divider inside the composition must not become an outer picture boundary.

## Evidence and confidence

Recording: `C:\Users\bslac\Videos\2026-09-17 09-35-45.mp4`, SHA256 E717AB88F0DCFD0DB3188A69BA96D546079ED0C84D9C08259CD07B66581EED57, 59.9 seconds. Sampled the full recording and the 34–42 second interval. The desktop recording clips part of the VP window; it is not a raw captured source stream.

At approximately 35–36 seconds, the same single-person office shot shrinks when a two-line subtitle appears; the earlier central divider is absent. The corresponding log at 09:36:20 shows:

1. Sequence 90015: bottom overlay extent 1798..1846; subtitle translation begins confirmation at 1/3, target 84 source pixels.
2. The logical picture candidate changes from 188,364–3652,1796 (bars on both axes) to approximately 188,0–3648,2160 (full-height pillarbox).
3. Sequence 90016: recent geometry is reacquired, the previous crop is withdrawn through the horizontal-conflict path, and recovery starts.
4. The final decision remains full raster even when the proposed owner is a bounded fit. Event 11 ends after 11437 ms, at sequence 90290.

This establishes an interaction between subtitle evidence, logical crop publication and recovery. It does not establish that the central divider causes the raw detector error or that all events have the same trigger. Nor does the recording alone prove what each raw edge scan saw.

## Confirmed code gaps

### 1. Mixed edge movement escapes the outward guard

`AlphaSourceCropPolicy.cpp::ConfirmOutwardPictureTransition` first requires complete containment. In this event the candidate expands vertically but its right edge is four pixels inward. The function therefore says there is no outward transition, and the caller does not defer it even without broad picture evidence above and below.

Compiled probe: the mixed candidate returns outwardTransition=0; changing only its right edge to contain the base returns outwardTransition=1, authoritative=0. This is a guard-selection error, not a reason to increase the aspect-ratio sensitivity.

### 2. Subtitle protection starts after its vulnerable confirmation interval

`ShouldDeferVerticalGeometryTransition` recognizes active translation/drift. It does not cover translation pending confirmation or all relevant fit/inspection states. Logical transition evaluation also occurs before the current frame's later dense subtitle decision. The observed two-frame history reacquisition can win before the three-sample subtitle translation is established.

Compiled probe: same-generation vertical expansion is not deferred with inactive translation and is deferred once translation is active. Pending state cannot be represented by this API.

### 3. Provisional classification is not a publication veto

The renderer uses PROVISIONAL for intentionally deferred geometry, but `ActivePictureTransitionModel::FindRecentTrustedGeometry` can restore remembered authority from a provisional observation. The history path precedes the general crop-authority check. Existing tests deliberately support provisional recurrence, so disabling all such recurrence would risk multi-AR/dark-scene recovery.

Compiled probe: after seeding pillarbox and then windowbox, two provisional pillarbox observations publish the remembered pillarbox geometry. Consequently, fixing gap 1 or 2 solely by changing classification would be incomplete.

### 4. Recovery evaluates the old logical crop even when presentation can expand safely

`EvaluatePresentationRecovery` requires safe excluded bands and contained observation for the saved logical crop. A subtitle outside that crop keeps this false. The log confirms a bounded-fit candidate exists during the full-raster hold. It would be unsafe to accept a fit label alone, but it is also wrong to assume proof for the old crop is the only possible way to restore a useful presentation.

The existing bounded-fit tests intentionally retain full raster for an armed recovery episode. That expectation must change only when the new test supplies a complete current certificate for the actual proposed presentation.

## Recommended implementation

### A. Reproduce the whole decision sequence first

Create a regression that exercises the actual production sequence: frame evidence, transition admission, history reacquisition, subtitle confirmation, candidate presentation, and final recovery decision. Seed the remembered geometry as in this session. Reproduce frames 90015/90016 plus continued subtitle presence and disappearance. Do not replace this with a test that manually sets the final conflict flag to the desired value.

Extract only the necessary decision coordination into a testable function if required. Keep scans and renderer resources outside it. This makes ordering and precedence testable without rewriting the detector.

### B. Make transition permission explicit

Represent intentional deferral separately from an ordinary uncertain/provisional observation. Carry its reason through the local transition model and scheduled/look-ahead adoption. An intentionally deferred observation cannot publish or reacquire a different geometry through history, seed trusted history, or accumulate confirmations that cause an immediate stale commit when the deferral ends.

Keep legitimate provisional reacquisition available when there is no explicit veto and current evidence permits it. Remembered geometry helps identify a candidate; it cannot override a current overlay conflict or reset boundary. Test local and queued paths with the same evidence and expect the same result.

### C. Evaluate expansion per axis and arbitrate subtitle onset before publication

Classify outward movement on each axis even when another edge moves inward. Small orthogonal sampling differences must not suppress vertical expansion checks; material simultaneous changes need evidence for each affected axis. Use any enclosing rectangle only to evaluate visibility, never silently publish its coordinates as a new crop.

Use current, same-base, same-frame excluded-band evidence to identify a localized overlay candidate during the initial confirmation/inspection interval. Keep the established logical picture while the presentation path confirms translation or fit. Use existing bounded inspection and confirmation mechanisms first; do not introduce a new long timeout.

A pending subtitle label alone cannot retain a crop indefinitely. Broad new picture, incompatible base, unavailable/stale measurements, context changes, and exhausted inspection bounds must select the appropriate safe presentation. Preserve support for asymmetric real format changes: bilateral broad evidence may be useful for this symmetric case, but must not become a universal requirement for all transitions.

Do not crop off subtitle pixels while waiting. Reuse a current certified presentation when valid, use a bounded containing presentation when proved safe, and retain full raster as the fallback when neither is established. The test must assert both logical stability and visibility.

### D. Separate inward recovery from safe outward presentation

Keep the existing strict proof/dwell before returning inward to a smaller logical crop. Add a separate route out of full-raster recovery for a confirmed presentation that is safe on the current frame.

The certificate must identify the actual proposed rectangle, source sequence, generation, viewport epoch and logical base; include all detected picture and overlay extents; validate chroma alignment; and prove that anything still excluded is safe. A translation requires proof for newly excluded pixels as well as containment of the subtitle. Reuse existing scan evidence where it proves these facts; otherwise request a bounded current scan. A fit-owner string, accumulated stale envelope, or old-base bands-safe bit is insufficient.

Do not allow shrink-back to the logical crop merely because this outward presentation passed. Keep that separate confirmation, and reset both proofs correctly on seeks, cuts, resize/viewport changes and generation changes.

### E. Add diagnostics that explain the decision

At crop/owner changes and rate-limited summaries, log raw candidate and axes; admission allowed/deferred and reason; history match versus history publication allowed; subtitle pending/confirmed state; logical crop; proposed/applied presentation; safety certificate base/sequence/generation; and recovery gate. Add startup identity for both executable and renderer, including commit/build identity and loaded path, so future logs establish the tested pair directly.

The proposed file-only graphical overlay would help later, but is not required for this fix. If implemented separately, default off; distinguish raw/uncertain detection, logical crop and actual presentation; stamp source sequence; draw after analysis so it cannot affect detection. A source inset is preferable for bounds outside the displayed crop. Do not expand this corrective patch into an overlay UI project.

## Regression and acceptance matrix

| Scenario | Required result |
|---|---|
| Logged subtitle onset, remembered pillarbox, 4-pixel side mismatch | Subtitle remains visible; logical crop is not reacquired as full height; no subtitle-duration full-raster hold |
| Same candidate marked intentionally deferred, local and look-ahead paths | Neither history nor scheduled adoption can publish it |
| Ordinary uncertain return to a known multi-AR mode, no veto | Existing supported reacquisition still works |
| Real bilateral and asymmetric expansion; simultaneous subtitle | Real picture pixels are exposed; subtitle state does not lock an obsolete crop |
| Split-screen with internal black divider, captions on/off | Outer picture bounds cover both panels; internal divider is not an outer edge |
| Star field, fade, dark action, credits | No new crop acquired from sparse darkness alone; genuine new visible content is preserved |
| Subtitle persists or alternates one/two lines, then disappears | Stable logical crop; translation/fit contains text; smooth return using existing policy |
| Existing full-raster recovery plus certified bounded presentation | Can leave recovery for that safe rectangle while old crop still excludes subtitle pixels |
| Same case with stale/partial/wrong-base evidence, repeat sample, seek, epoch change | No unauthorized release; no accumulated stale confirmation |
| Continuous gradual format change | Follows the intended transition without restart starvation or chasing sampling jitter |

Exercise 23.976/24 and 59.94/60 capture rates and cadence repeats. Include synthetic image evidence through detector-to-policy tests, not just hand-authored trusted rectangles. A rendered desktop video is a playback oracle, not a substitute for raw pixel fixtures.

Build and run the full native x64 Release suite plus affected diagnostic helper tests. Check that added inspection is bounded to the conflict interval and does not create continuous extra scan work. Then deploy the matched pair with configuration preserved and log the identity. Replay Women in Blue S02E06 from before the gradual transition through the subtitle scene and recovery, comparing the same shots and captions. Unit-test counts alone are not acceptance.

## Review decisions and limits

- Do not increase four-pixel sensitivity, black thresholds, the 2% deadband, or the four-second nested guard in this change.
- Do not treat every subtitle or horizontal conflict as safe; proof must describe current pixels and the proposed presentation.
- Do not disable all history reacquisition or ignore all provisional observations.
- Do not merely allow every fit to bypass recovery. Retain the tests rejecting incomplete certificates.
- Do not claim the divider issue is independently diagnosed until a raw/synthetic fixture reproduces it.
- Implement A–D as one tested behavior change. Small commits are useful for review, but another deployment that addresses only one guard would repeat the earlier incomplete fixes.

This is ready for implementation with the integrated failing regression as the first gate. The exact overlay-versus-real-picture classifier behavior and availability of sufficient current presentation certificates remain facts to verify during that work, not assumptions to hide with a timer.
