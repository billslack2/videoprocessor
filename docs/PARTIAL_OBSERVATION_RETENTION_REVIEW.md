# Partial-observation retention correction

This change addresses loss of an established crop when a provisional dark-picture observation moves far inward on one edge but extends by at most one native vertical scan step on the opposite edge. It does not acquire a new crop, recognize movie titles, or assume a fixed set of aspect ratios.

## Evidence and RED checkpoint

The September 26 Fox-to-Alien log retained 0,284-3840,1876 but observed 972,660-3840,1880. At sequence 404, expired inspection and near-black episode exit exposed full-raster fallback; scope returned 45.547 seconds later. The partial measurement bypassed the existing absolute-per-edge jitter test. Broad bars sampled clean, but the disputed strip was not certified; its coarse p90 was112. The log does not establish that retaining that exact frame is safe.

Four new source/policy tests were first built against unchanged production. Two failed at the intended assertions: safe partial evidence could not retain presentation, and current safe partial evidence could not clear expired inspection after near-black exit. The two negative tests (occupied strips and larger/outward geometry) passed. No existing assertions were changed. Initial tests use synthetic source pixels, not reconstructed movie frames.

## Reviewed correction

Keep the existing geometry-equivalent border tolerance untouched. A separate partialSamplingReaffirmed certificate requires:
- valid provisional current source measurement and an existing top/bottom crop;
- no outward side movement, and at most one native vertical scan step outward;
- current broad excluded-band safety and non-near-black analysis;
- every sampled disputed row passing the existing luma/chroma strip limits.

The new route never sets samplingEquivalent. It can retain only the existing rectangle, with explicit proof passed to inspection/recovery and existing generation/sequence/base checks. New-crop admission remains independent. Near-black full-raster re-entry logic is unchanged.

Regular bounded crop recovery/source-crop logs add partial_sampling_checked and partial_sampling_reaffirmed; existing strip peak Y/chroma values explain rejection. No new configuration option, background task, per-frame log stream, or motion-based acquisition is introduced.

## Coverage and limits

Validation includes safe and occupied source pixels, mirrored boundaries, scan-step scaling, proof omission, source context, inspection/recovery/admission, and existing full-suite acquisition/OSD/caption/lookahead/gradual-transition regressions. The synthetic sequence prevents new recovery from arming after inspection expiry; tests of already-active recovery are separate. Full renderer/HDMI playback remains a later validation step.

The actual Alien opening may still require release if the disputed pixels fail this stricter proof. This is not a promise to solve ambiguous initial acquisition or all OSD cases. Deployment requires a separately requested validated Release build.

## Final validation, September 26, 2026

- RED against unchanged production: 2 expected failures and 2 negative-case passes. Both failures reached the intended retention assertions.
- Six new tests now pass, including native v210 versus planar P010/P210 coverage and stale-proof rejection. Existing tests and expectations were not edited.
- Clean full x64 Release rebuild succeeded. Full native suite: **1,727 passed, 0 failed**.
- The first incremental build failed with MSVC LNK1103 (corrupt intermediate debug information); its log is preserved. A clean Rebuild resolved it.
- An additional NEW test initially assigned context-change blocking to recovery. Review corrected it to check recovery reset followed by final-admission rejection of stale retention. Production and existing tests were not changed to accommodate that test mistake; its failed diagnostic receipt is preserved.
- Independent architecture and test reviews found no remaining blocker in this narrow retention path. Existing desk/OSD/caption, lookahead, gradual-fade and genuine multi-AR tests remain green.

Receipts are in artifacts/partial-retention-proof: red, green, green-probe, green2 and full. validation.json pins source-file hashes and successful Release output hashes. This worktree contains uncommitted changes on beta base ab683eefe5fdffa018b788eedfcd8913dbebe1b4; that base alone does not identify these binaries.

No deployment, runtime configuration change, package or merge was performed. Remaining validation: Fox-to-Alien with controls hidden and then visible; a captioned Eternals AR transition; a Mandalorian gradual fade. The new partial_sampling_checked / partial_sampling_reaffirmed fields distinguish a proven retention from a rejected strip. Exact Fox source pixels and a live queued-caption/partial-proof renderer replay are not available as automated fixtures.
