# VP-0218: Boundary-led evidence research, 2026-10-27

## Finding

There is a concrete improvement path for the captured YTTV ad: search for independently supported relative picture-to-exterior transitions before requiring an absolute black-bar classification. Both new diagnostic searches find the two strong borders. The current detector misses the raised upper band before it can use that evidence. A local background model describes the gradient much better than the earlier coarse fit, but this experiment does not yet qualify a safe production crop. The remaining question is which residual structure is bar texture versus picture/caption detail, and how that evidence reaches current-frame visibility checks without bypassing them.

This is source-pixel detection research. Screen size, CIH, fit, zoom, NLS and a standard target aspect were not inputs. Nominal boundary measurements, conservative retained fringe and caption-visible presentation envelope are three separate quantities. The measured red-panel aperture in this sample is approximately 2.47:1 within a 3840x2160 transport; it must not be forced to 16:9 or to the physical screen ratio.

No production source, configuration or deployed binaries changed in this research. VP-0218 remains In Progress.

## Provenance and limits

GitHub beta was rediscovered and fetched as `v1.4.02-beta`, tip `7394ba420999a8fa708718ee4ce4d87e7459369f`. The clean detached research worktree is `E:\codex\videoprocessor\vp-0218-boundary-research-20261027`. Diagnostic probes link optimized x64 beta component objects; they are not a new complete VP Release build. The separately deployed contained patch remains `ddf5d666803c29e0bdf7af310963b2695e382bc2`, PR #213.

The ad input is `C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1`: 97 accepted native V210/Limited source identities, four unique pixel payloads occurring 24/24/24/25 times, and 15 unsaved source positions. Identical pixels do not make distinct accepted identities cadence repeats. They also do not turn this short paused scene into 97 independent scene examples or a moving-transition latency test. Recording gaps cannot be counted as unseen confirmations.

Three Luna agents reviewed the profile, regression and subtitle/older-capture evidence. The parent inspected the corrected P210 serializer and full-pixel beta-sampler validation, and distinguished baseline tests from experimental-route qualification. Raw frames, derived pixel buffers and binaries remain local; this archive contains source, numeric results, logs and hashes.

## Executed comparisons

| Evidence | Result | Meaning |
| --- | --- | --- |
| Fresh unchanged beta replay, 97 ad frames | All PROVISIONAL; upper proposal 0, lower 1858; trusted fallback full raster | Confirms the existing miss. Confirmations were already saturated; more dwell is not the fix. |
| Existing absolute diagnostic grid | Top 0, bottom 1855; not clean | The grid's current absolute visible-pixel rule also misses the upper border. |
| New frozen relative-grid nominee | Both borders on 97/97; all four horizontal quartiles 32/32 Y support and optional UV support | Strong relative geometry is available without an absolute black floor. This supplies candidates, not crop authority. |
| Full-column profile prototype, four unique payloads | Both lines supported; top exterior passes local median residual/neutrality checks but fails connected-feature veto | Gradient modeling improves; the frozen complete route still rejects the actual ad. |
| Older paused dark capture | Beta trusted 274..1884; grid nominates both edges | New relative evidence can work in dark content too. |
| Older continuous-subtitle capture | Beta trusted 272..1886; grid nominates top only | Replacing the old route would lose a working case. Keep any new route additive. |
| Existing deployed Release tests | 132 boundary + 16 visibility/subtitle cases pass; recorded dark-frame test actually executes and passes | 149 unique existing tests pass, not validation of an integrated experimental route. |

See [baseline](baseline/summary.json), [grid experiment](grid/README.md), [profile and errata](profile/README.md), [prior captures](grid/prior-captures.md), and [paired controls](regression/paired-controls/paired-control-results-20261027-01.txt).

The grid's upper outside/inside samples are 296/304, midpoint 300. The lower outside/inside samples are 1862/1854, midpoint 1858. These coarse brackets are not exact bar boundaries. The finer independent search reports top outside=300/inside=301 and bottom inside=1856/outside=1857; the saved JSON's bottom outside label is corrected in `profile/errata.md`. Its conservative eight-row fringe retains top=293 through bottom-exclusive=1865. That retained rectangle is deliberately larger than the nominal aperture and must not redefine the active-picture aspect.

The upper band's local Y residual P95/P99 is 0/4; neutral fraction is 1.0; tile-median step P95/max is 8/18. These pass the frozen exploratory limits. The connected-feature gate rejects 22 components of area 19..136 pixels (median 51), with widths 4..19 and heights 1..16. Their boxes recur across all four payloads. The generated caption has a largest component of 2,425 pixels, but this size difference alone cannot authorize removing small text, stars, logos or scene details. The local median used the evaluated pixels; this is not a held-out fitted model or independent validation corpus.

## Negative controls and corrected comparison

The generated caption, colored object and noisy gradient are rejected by both the profile prototype and unchanged beta's crop/retention safety paths. The generated neutral gradient passes both. A broad smooth neutral +8-code object also passes both, including beta BAR_CROP_TRUSTED and pixel-safe retention: this exposes a shared existing synthetic blind spot, not a newly demonstrated regression from the prototype. A separately labeled authored-ceiling image is byte-identical to the gradient control; identical pixels cannot establish different semantic labels and must not be counted as independent operational examples.

An initial paired-control run had invalid P210 row-interleaved packing. Its results are retracted and retained only in `regression/paired-controls-invalid-interleaved-20261027`. The corrected run writes the full Y plane followed by the full interleaved UV plane. Python verified every serialized Y/U/V value; the actual beta `AnalysisLumaSource::Sample` then verified all 921,600 pixels per control before classification. Use only the corrected `regression/paired-controls/beta-results.csv`. This correction changes the +8 finding from a supposed new regression to a shared baseline limitation.

## Final diagnostic ablation

On copies of the four unique payloads only, a frozen experiment replaced excluded-band Y with `64 + (original Y - 17x17 local median)`, preserving chroma, the image and the eight-row edge fringe. No values clipped. Full-plane P210 serialization roundtripped exactly, and beta's sampler verified all 8,294,400 pixels per copy against expected Y/U/V before evaluation. See [protocol](profile/normalized-ablation/protocol.txt) and [results](profile/normalized-ablation/beta-results.csv).

All four copies remained PROVISIONAL with no trusted bar axes. Retention of the supplied guarded rectangle passed on the transformed copies. These are separate outcomes: retaining a supplied crop is not acquiring a new crop. The operation also creates a photometric seam at each normalization endpoint; its top proposal moved to 292, near the artificial endpoint, rather than the original independently measured transition. Consequently this ablation does not isolate the original acquisition failure to a particular texture/coherence predicate. It demonstrates neither native-frame safety nor a deployable normalization route. Any future local-reference authority must explain the original pixels and reconcile raw visibility, rather than feeding modified pixels into production as if they were the source.
## Proposed implementation contract

1. Preserve current strict-black, uniform-raised-matte and distributed-dark paths. Invoke a bounded independent relative-boundary search only when existing evidence is unresolved or conflicts with strong current border evidence. It must abstain safely when either edge lacks support. Do not change already trusted geometry merely because a coarse nominee differs by a few pixels.
2. Refine each nominated edge independently, retain its uncertainty bracket and confirm broad horizontal support plus sustained inward picture support. Do not mirror one edge, infer an aspect standard, or use screen geometry. Keep original source-space coordinates and range/format metadata throughout.
3. Qualify the exterior using constrained per-edge background evidence and residual structure, including localized captions and connected picture. The upper and lower backgrounds need not have equal levels. Do not raise a global black cutoff, use a median to erase meaningful detail, or weaken a component threshold solely to pass this sample. Reconcile each raw visibility witness explicitly; normalized/qualified evidence cannot suppress contradictory raw pixels by default.
4. Carry typed evidence with exact source generation/sequence, format, owner and geometry through raw-to-qualified publication, history/cache, queued/live routes and final current-frame admission. A boundary nominee alone gets no crop authority. Temporal confirmation starts only on complete spatial evidence; mixed incomplete routes, repeats, gaps, stale owners and canceled work cannot manufacture proof.
5. Keep source-picture aspect separate from subtitle extent. Existing same-frame caption ownership, translation/base contracts, current visibility vetoes and caption-safe envelopes remain active. A caption mask must not train the matte reference; a caption-safe envelope must not become the detected picture aspect. Preserve broad-only subtitle-translation retirement until separately qualified. See [subtitle contract and named tests](subtitle/subtitle-boundary-contract.txt).
6. Budget the new analysis. The existing diagnostic grid takes 34,560 samples per frame and is not already mandatory on every production frame. The exploratory baseline measured the full SampleLocalBoundaryDiagnostics call at about 1.04 ms median; the new Nominate call, including grid sampling and relative scoring, averaged about 0.54 ms in concurrent runs. These are not isolated playback performance results. Reuse only when available, constrain work and cancel stale analysis; add no queue wait or growth.

## Qualification before enabling a crop route

The next implementation should first emit diagnostic candidate/qualification reasons on exact source identities. Its admission stage needs paired unchanged-beta and candidate replay with annotated native boundaries, real matte texture, different grain/gradients, sparse captions and logos, caption crossings, bright scenery and dark scenes, internal rectangles, windowbox, fades and changing aspect. The two older native cases must retain their successful output when the new cue abstains. Add counterexamples that distinguish a legitimate profile from low-contrast picture structure rather than relying on the byte-identical semantic alias.

Test both acquisition and retention, raw visibility reconciliation, caption glyph pixels, current ownership, queue/identity invalidation, physical-picture aspect versus caption envelope, and actual submitted geometry. Measure moving transitions in accepted source frames and analysis cost under the normal budget. Do not infer faster switching from this paused sample or claim universal absence of regressions from the 149 existing tests. A production change still requires its own successful x64 Release build and deployment verification under the established VP workflow.


