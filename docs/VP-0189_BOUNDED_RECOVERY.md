# Bounded presentation during crop recovery

The Agency replay on 2026-09-21 retained a trusted 3840x1608 movie rectangle
(0,276)-(3840,1884), but visible pixels reached the bottom of the HDMI raster.
At source sequence 4013, the current top band was uniformly Y64 and the bottom
band measured p90 Y281. The pixel inspector reported a bounded visible rectangle
(0,276)-(3840,2160). Overlay inspection expired without a confirmed dense owner.
Recovery discarded the confirmed top boundary and presented the full 3840x2160
raster for roughly 12 seconds. This was a presentation fallback, not affirmative
16:9 picture authority.

## Behavior

Unresolved recovery may now fit the current pixel-bounded rectangle instead of
unconditionally selecting full raster. The same rule covers every edge and does
not classify menus, subtitles, or picture content. Logical movie geometry is
unchanged. The expected source aspect for the logged case is 3840/1884 = 2.0382,
so a 2.35 screen has modest pillarboxing while the additional content is visible.

The fallback requires a current, valid pixel measurement against the same trusted
geometry, matching nonzero source generation and sequence, a valid current
observation contained by the visible envelope, and non-near-black evidence.
The existing pixel inspector only publishes outward bounds when all unsafe
excluded edges are bounded. Boundaries are rounded outward for chroma.
Missing, stale, conflicting, full-raster, and moving-picture evidence retain the
existing conservative fallback. Final crop admission still prevents a provisional
observation from acquiring an unpresented movie geometry.

Within one unresolved recovery, the fallback can only expand. Small inward
extent changes cannot cause repeated size changes. Existing inward confirmation,
confirmed presentation resolution, or context change ends that hold. No new timer,
AR threshold, pixel scan, or configuration option is introduced. Automatic fill
and NLS must not recrop this recovery envelope; an explicitly fixed source aspect
retains its existing precedence.

Recovery logs include `bounded_recovery=1` and the actual rectangle, together
with the existing recovery gates and inward proof counter.

## Validation

Six BoundedRecovery tests cover the logged expired-inspection route through final
admission, four-pixel extent jitter, full-raster expansion, both axes and outward
chroma alignment, stale/conflicting evidence, inward proof and viewport reset.
Real source-pixel tests cover native v210 and P010 with a black top band and
visible bottom content. Three initial regression cases failed against unchanged
beta 38477f13560a3c4f0085c8840127ae064d0a5b86. The first completed green suite
passed all 1392 native tests. Final committed-build evidence is retained with the
local test/deployment record.

Build with Visual Studio x64 Release and run VideoProcessor-Test.dll using
vstest.console.exe. To run only these regressions, use
`/TestCaseFilter:Name~BoundedRecovery`.

## Playback validation

Replay The Agency with the playback UI both visible and dismissed. Check that
bottom-only expansion retains the black top boundary, shows the bottom controls,
and does not jump to full raster merely because inspection expires. Verify that
ordinary scope playback and genuine 16:9 / IMAX transitions still behave normally.
A genuinely unbounded measurement can still require full raster; this change does
not manufacture a boundary when the source provides insufficient evidence.