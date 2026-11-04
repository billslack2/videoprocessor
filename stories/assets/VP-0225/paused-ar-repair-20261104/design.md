# Paused native edge uncertainty

The November 4 paused HDMI captures exposed two independent acquisition failures. The current-source subtitle inspection repair in `1fa22c98d323a9a2cbca52b4ad3e91a6bc523c54` lets the worker inspect an independently measured current spatial nominee when ordinary bar extraction cannot start a raw caption scan. That inspection grants no bar or crop authority; the existing exact-current caption-masked native verifier still decides acceptance.

After that repair, 45 of 180 saved native frames still had no current physical rectangle. Retaining an earlier aspect hid neither the failed measurement nor its cause. The remaining failure was in edge support aggregation.

## Measured cause and repair

On every missing phase, top-edge start bins at rows 274, 275 and 276 contained 16, 31 and 15 rays. Each integrated profile already measured a two-row uncertainty interval. The old merger required the **union** of those intervals to span no more than two rows, so it left them separate. The strongest bin missed the unchanged 32-ray threshold by one. Without a selected top edge, the left relative-matte contact could not be completed either.

An artifact-only per-ray probe established that row 275 actually had 48 distinct supporting rays across all four regions; row 276 had 61. It added no source reads and reproduced the original five-frame decisions exactly. These are measured interval memberships, not inferred tolerance around a retained rectangle.

`SourcePictureMeasurement.cpp` now gives each ray one vote at each coordinate in its own measured abrupt point or integrated interval (zero or two rows). Each coordinate keeps a unique 128-bit ray set and the common intersection of its contributors' intervals. The adjacent-candidate union merger is removed: a ray cannot support a coordinate outside its measured interval, and overlapping intervals cannot form a transitive chain of invented support.

The existing outward-first selection, 32-ray support requirement, regional support, glyph veto, topology reconciliation and excluded-band verification remain unchanged. No extra pixel reads, aspect target, threshold relaxation, buffering or presentation hold is introduced.

The old nearby-edge test expected 128 rays combined across three distinct exact coordinates. Its assertion now checks truthful per-coordinate ray membership/counts while retaining the same safe measured-bound constraint. New tests cover all four quantization phases, separated intervals, overlapping chains and repeated departures on the same ray.

## Matched captured evidence

The byte-identical CPU producer used both preserved bursts with unedited V210 pixels, original full source identities and original capture gaps.

| Capture | Saved frames | Current physical rectangles before interval repair | After repair | Exact-current proofs after repair |
| --- | ---: | ---: | ---: | ---: |
| 07:44 | 91 | 67 | 91 | 91 |
| 07:51 | 89 | 68 | 89 | 89 |

All 180 current physical measurements are `[0,275,3840,1884]`, giving `3840 / 1609 = 2.386575512740833` (displayed as 2.39). Both cold models acquire on their first saved source and remain stable. Their sampling aperture is `[0,274,3840,1886]`; it is separate from the measured physical boundary and aspect.

Focused RED: `20261104-082448-dbb8f868`. Focused GREEN: `20261104-082652-1c03395c`. All 57 detector methods passed in `20261104-082811-3c0a2d23`. Full Native and renderer qualification are separate gates.

Evidence under `artifacts/paused-ar-20261104-0744/`:

- `uncertainty-probe/receipt.json`: original per-ray interval facts and unchanged-decision equivalence.
- `worker-ordered-replay/receipt.json`: pre-interval comparison; SHA-256 `36d305561e938d80d813041693d73a533aaa4690bdcfa6cd0f97e459d811c827`.
- `worker-ordered-replay-interval/receipt.json`: successful matched comparison; SHA-256 `719dc449c730d7e58f3402a045fe27579917d3d20d611bc933e36a9e153e439c`.

The candidate receipt records source/library/executable hashes, the unchanged producer hash, all 360 reverified BIN/TXT hashes and original identity gaps. Its production measurement source SHA-256 is `fa0449695ea6dbcea2c1e407c29e8e95a887ad9264fa7fbc438b164b9b266598`.

## Evidence limits

These are 180 saved frames, not 180 consecutive frames. The replay exercises the actual sampler, strict native verifier, transition model and physical telemetry; it does not establish renderer scheduling, live deadlines, GPU composition or visual acceptance. It prints exact-current proof validity and the stable model aperture, not each current proof's sampling rectangle separately. Native movie pixels remain private artifacts and are not committed. No deployment or full qualification follows from this component result alone.
