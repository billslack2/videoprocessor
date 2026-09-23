# Known scope return with a carried caption

## Observed problem

The paired 2026-09-22 recordings show a hard cut from IMAX framing to scope at the red-car shot. The preceding `(GROWLS SLOWLY)` caption remains in the newly formed bottom bar for six frames. The later `Run! Go! Go!` caption is a separate event.

The corresponding log retains the previous 3840x2160 geometry (0,68)-(3840,2092) while observations 719-724 propose (0,276)-(3840,2016). Clean scope (0,276)-(3840,1884) is accepted at observation 725. This is delayed inward acquisition, not a full-raster recovery during the target interval.

## Bounded change

`InspectInwardCaptionEvidence` considers a material inward return only when its symmetric scope geometry already exists in recent trusted history. It requires current non-near-black source pixels, a clean opposing bar, an isolated occupied band separated from the picture, and a pixel-safe envelope containing both the picture and the occupied band. History does not establish a new boundary by itself.

Queued and live observations use the same helper; queued extraction caches retain raw evidence. Ordinary confirmation, available-frame limits, identity continuity, publication and presentation admission still apply. On actual adoption, the logical scope boundary and the protected outward-fit presentation are applied together. No early subtitle translation is fabricated. Every continuation frame is re-inspected using the original taller base, and its protection is restricted to the current source identity and adopted geometry.

This path is disabled for requested NLS, fixed cropping, global near-black evidence, moving-picture transitions and conflicting subtitle/presentation owners. Aspect-limit fill cannot trim the protected envelope. It does not change global confidence thresholds, confirmation timers, or animation policy.

Preserving a caption outside the scope picture can still require modest temporary pillarboxing. This change targets the larger old-IMAX hold; it does not promise full-width scope while also preserving pixels outside scope.

## Validation and replay

Regression coverage includes the old blocked proof, known-return acquisition, same-frame picture/caption containment, lookahead budget/identity rejection, unknown history, asymmetric content, menus, two occupied bars, darkness, clean content, invalid input, native V210/planar parity and source-sized continuation. Synthetic fixtures are not a reconstruction of the original HDMI pixels.

Replay the original HDMI content with NLS off and the existing CIH/subtitle-fit configuration. Inspect `Alpha inward caption adoption` for the original base, logical picture, protected envelope, identity and queued/live adoption. `Alpha inward caption protection ended` identifies fresh evidence ending the exception. Confirm that ordinary scope acquisition, genuine IMAX expansion and caption clearance remain correct. Visual validation remains necessary.

Focused red/green evidence is retained in this worktree's `artifacts` directory. With a no-op implementation, three required acquisition/publication tests failed and the four characterization/rejection tests passed. After the implementation was compiled, all nine focused tests passed. Existing tests were not edited to change their expected behavior. A local x64 Release synthetic 4K measurement was about 0.94 ms per helper call (3.77 ms for four inspections); this is informational, not a timing assertion or a guarantee for other machines.

Small measurement changes use the model's existing history-match tolerance. The certificate uses the same canonical remembered bounds that reacquisition will publish; its protected envelope includes both those bounds and the freshly measured picture. This avoids losing caption protection when a boundary moves by one detector step. The jitter regression covers both signs of a four-pixel difference and checks actual publication and presentation containment.
