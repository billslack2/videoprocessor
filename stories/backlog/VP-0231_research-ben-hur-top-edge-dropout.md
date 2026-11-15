# VP-0231: Research and prevent disappearing top-edge measurements in stable Ben-Hur footage

## Status

Backlog

Requested 2026-11-15. The user observes that the Ben-Hur scene is visually consistent and its top picture edge should not disappear to zero. First determine exactly where and why the zero originates using current native pixels; then make the smallest evidence-supported correction and validate it. No implementation, deployment, or live acceptance is claimed by this story. Coordinate with VP-0214 and VP-0230 before touching shared code.

## User story

As a viewer, I want VP to recognize and maintain the real upper boundary of a stable letterboxed picture, so subtitle analysis and aspect detection have accurate current-source geometry without flicker or picture loss. The boundary must follow the source pixels, including genuinely dark scenes and real format changes.

## Incident evidence

- The native diagnostic `C:/Videoprocessor/vp/logs/analysis-20261115-154925-19564-1` contains 64 original decoded Dolby Vision 4:2:2 analysis frames at 3840×2160, structured events and exact source sidecars. It is a bounded post-trigger sample, not a complete recording; preserve identities and note gaps. The file `frames.jsonl` maps frames 000003–000006 to consecutive source frames 2147–2150 (accepted sequences 2000–2003).
- In that window the subtitle worker's measured top/bottom fields are `0/1778`, `0/1778`, `384/1778`, `0/1778`. It detects the same subtitle on all four. The separate AR-decision record measures top around 384, while presentation retains `0,384–3840,1778`; these are distinct paths, not proof that the main AR detector itself reported zero. Establish whether zero means no bar, unknown/unavailable authority, a failed extraction, or a later binding/conversion problem in the affected path. Do not treat every zero as the same semantic state.
- Visual inspection of source 2149 shows a stable upper matte and picture boundary, with the original subtitle in the lower encoded bar; VP relocates it near the picture. The user confirms the scene itself is consistent. A quick descriptive luma sample of every eighth pixel across rows of native frames 2147–2150, using stored 16-bit samples shifted right six bits, gives row-380 P90=0, row-384 P90=14–16, and row-388 P90=21. This is a low-contrast transition near black, not a prescribed threshold or a complete boundary ground truth. Measure and visually label the full source frames and relevant neighboring rows before setting acceptance tolerances.
- Source 2149 (accepted 2002) reports subtitle output loss with `context_mask=0x500` and `pixels_compared=0`; 2150 reacquires. Subtitle-area luma samples are identical across the four source frames. VP-0214's deployed `fd3fc2db82655e2b9036f9c86fe457fd48e58f53` candidate addresses two observed subtitle continuity/placement failures but does not establish why the top-edge observation became zero. A separate recording `C:/Users/bslac/Videos/2026-11-15 16-05-41.mp4` shows the same cue flashing; it is not synchronized to the native diagnostic.

## Research and implementation

1. Join the diagnostic records and native sidecars by complete frame identity and context. Trace native conversion, top/bottom bar measurements, black reference and thresholds, source sampling, shared-picture and worker selection, and all per-frame mapping/availability gates to the subtitle worker's reported top field. Identify the first value or authority transition that produces zero and its reason, including whether analysis was ready and exact-source matched. Compare the physical AR evidence, subtitle-facing measurement and presentation independently; verify each stage on the deployed incident source and current beta rather than assuming they share one detector result.
2. Reproduce the consecutive 2147–2150 sequence and its current-source behavior with the captured native pixels and the recorded context. Compare the unchanged current beta with the candidate on the same sources. If necessary obtain a bounded new native recording around the same scene with enough consecutive frames before and after the failure, recording the runtime source pin and exact context. Do not synthesize missing source frames or use a copied subtitle/background frame as current evidence.
3. Test the simplest justified remedy at the actual failure point. For example, correct a measurement, mapping, availability or consistency check if the evidence identifies it. Use current source-pixel evidence and truthful unavailable/uncertain semantics. Do not replace an unmeasured edge with a remembered or standard-ratio value as detection authority, invent absence, or force a 2.76:1, screen, CIH or fitting result. Preserve the independently verified subtitle correction in VP-0214 and avoid re-architecture or broad policy changes.
4. Record the cause, exact raw versus published values, failure and corrected behavior, source identities, measured costs and limitations. Cross-reference VP-0230's adaptive matte work and VP-0228's transition evidence; reconcile any shared implementation so this incident is not silently absorbed or fixed twice. Follow the current-remote-beta worktree rules when source work starts.

## Acceptance and validation

- On labeled consecutive native Ben-Hur sources, report top/bottom boundary error and the number and duration of zero/unknown/missed-edge observations before and after. Show that supported stable upper matte and picture boundary do not spuriously collapse to zero, while preserving full picture and captions. Verify actual detector and subtitle-consumer inputs separately; retained presentation alone is not evidence that detection succeeded.
- Include dark or low-contrast real picture, raised/variable black and noise, fades/full black, subtitles crossing or near either edge, dense/changing captions, volume and player UI, and persistent overlays. Include genuine full-frame or expanding-picture transitions, asymmetric bars and arbitrary aspects. Reject false bars/crops and stale or mismatched evidence; correct response to genuinely absent or ambiguous support may be unknown, not a fabricated bar.
- Exercise missing, late and out-of-order optional analysis, source/profile/format resets and Off-mode framing. Continue submitting distinct eligible current-source video with original timestamps; never wait for, replay, or borrow older frame pixels to resolve analysis. Measure actual source and presentation gaps, bounded latency and total critical-path worst-case against the applicable frame budget, including dense subtitles and slow analysis; renderer drop counters or still images alone are insufficient.
- Compare caption/picture preservation and visible stability in live playback, including the Ben-Hur incident and relevant alignments. Keep detected source aspect independent of physical screen, zoom, fit, fill limits, CIH and NLS. Use a successfully completed x64 Release build for any authorized deployment, preserving active configuration.

## Related work

[VP-0214](../in-progress/VP-0214_shared-subtitle-fit-ownership-and-temporal-recovery.md) owns the existing Ben-Hur subtitle-flash incident and candidate. [VP-0230](VP-0230_adaptive-matte-geometry-and-history.md) covers the broader matte/contrast/geometry research; [VP-0228](../in-progress/VP-0228-diagnose-outward-picture-lookahead.md) covers the Eternals transition investigation. This story owns the measured stable-scene top-edge dropout and its root cause.

## Tracker audit

At creation, ID audit found 248 indexed items, no duplicate IDs, missing index entries, orphan children, or child gaps; this story uses the next unused root ID. An existing VP-0211 index row says Review while its canonical file and status say In Progress, and 16 existing in-progress records use nonexact status-line formatting. These unrelated discrepancies were not altered; defer their repair to a deliberate tracker-maintenance decision.
