# HDMI input to analysis: design-first audit

Source reviewed:25a6abfd, E:\codex\videoprocessor\native-boundary-cadence-20261031. Read-only audit; no production edits, builds or new test execution. Existing test names below are inspected coverage, not a claim they ran in this audit.

## Established source contracts

- Ordinary V210 analysis bypasses rendering conversion. AnalysisFrame.cpp87..212 selects the original captured raster for both LiveRenderer and QueuedCapture; changing P010/P210 upload selection cannot select different ordinary analysis pixels. PrepareFrameAnalysis at renderer9137 retains the immutable frame owner and shares the canonical domain across both consumers.
- Native V210 sampling in AnalysisLumaSource.cpp53..106 uses six pixels per four little-endian words with exact10bit component positions. VideoState.cpp75 uses128-byte row alignment. The V210-to-P210 formatter reads each aligned source row independently, preserves every luma/chroma code with a left shift6, writes two full-height planes, and handles partial six-pixel tail packs. There is no spatial resize, chroma row averaging or source crop in that path.
- Decoded Dolby Vision analysis requires valid tunnel metadata, supported transport encoding, converted ownership, decoded422,12bit depth, shift4 and an explicit matching full/limited contract (AnalysisFrame.cpp123 onward). Untrusted metadata requiring black output cannot become analysis evidence. Decoder scalar rows are per-row/per-pixel bit reconstruction, not spatial filtering. Output12bit codes occupy high bits; analysis deliberately reads their upper10bits with>>6. This is precision reduction, not row deletion or geometric scaling.
- P210/decoded422 sampling uses full-height chroma. P010 uses y/2 chroma. AnalysisLumaSource::IsValid checks even dimensions where required, row minima, full luma/chroma byte coverage and checked size arithmetic. Actual capture files match the documented layouts: V2103840x2160/stride10240/22,118,400bytes; decoded4227680-byte luma and UV rows/33,177,600bytes.
- PreparedAnalysisFrame preserves3840x2160 source coordinates. DomainKey includes encoding, dimensions, colorspace, transfer, range, precision/alignment and both strides; the frame identity separately carries generation, source frame and timestamp. Renderer output size, CIH/fill and profile labels do not enter the interpretation key.
- Physical edge measurement samples original source coordinates. SubtitleBoxDetector uses a sparse sampling step, not a resized input image: SamplingStep at.cpp100 is max(1,ceil(width/960),ceil(height/540)), so step4 at3840x2160. Its sourceRows table.cpp353 retains actual source row coordinates, including explicit boundary-adjacent rows. This can cause sampling/shape misses; it is not proof of a conversion or scaling error.

## Independent captured-pixel evidence

Native V210 previews decode coherently into the documented movie/menu scenes. Native scope frames independently measure263..1897 and expose the exact physical-observer false interior matte model. The browse grid has exactly blank outer left136/top59 margins but58,914 real header pixels above the wrongly accepted internal tile-top332. The32-column observer misses the avatar and logo between probes. These failures reproduce from native source files without renderer scaling or P210 upload conversion.

The former terminal test used blackReference.threshold+16 even though threshold already included matte/retention tolerance. The native measurements show that using the source floor separately restores existing unchanged witness quotas in all92 menu and95 sun frames. This is a detector calculation/contract issue, not HDMI code unpacking. Separate shape errors remain: a menu component rectangle can still be mistaken for picture, and sparse bottom-edge support can miss real source edges.

## Assumptions and unverified conditions

- The capture-buffer byte count is inferred from immutable VideoState (VideoFrame.h explicitly has no byte-length accessor). Native analysis validates that declared count, not the hardware buffer's independently reported capacity. No mismatch was found, but state/frame ownership and format-transition safety must remain explicit.
- Live decoded DV preparation defaults converted strides to width*2 because the formatter contract currently specifies range/depth/shift, not stride. This matches the current tightly packed decoder. A future padded formatter must extend the contract and its tests rather than silently reuse this assumption.
- Decoded captures are downstream of tunnel reconstruction. They prove the detector's sampling and coordinates on that decoded plane; they do not independently validate original HDMI wire bytes. A same-source raw-tunnel plus decoded-plane pair would close that last observational gap. Existing independently generated tunnel tests provide synthetic coverage.
- Packed RGB formats use legacy renderer-compatible matrix/range rules; generic full/limited behavior cannot be inferred from the format name alone. Existing tests document those rules. Do not normalize every source to the same black code or apply a DV-only exception.
- Scalar/AVX parity can reproduce a shared mistake. Independent packing fixtures and known code values matter. AVX cases require an AVX-capable host; record whether the kernel actually ran rather than equating an early-return Passed result with vector execution.

## Existing CPU regression shortlist

Paths below are relative to the reviewed source worktree. No GPU test is needed to run these contracts.

Primary small set:

| File:line | Method | Contract |
|---|---|---|
| NativeRgbAnalysisTests.cpp:421 | V210SparseSamplingDecodesAllSixPixels | independent native pack positions |
| NativeRgbAnalysisTests.cpp:381 | P210SamplingPreservesEachChromaRow | no420 row collapse |
| AnalysisRecoveryDvTests.cpp:214 | DecodedDolbyVisionKeepsTwelveBitStorageAndLegacyUpperTenEvidence | storage precision versus analysis codes |
| AnalysisRecoveryDvTests.cpp:362 | InvalidMetadataFormatStrideAndDepthCannotCreateDvEvidence | invalid input fails closed |
| AnalysisRecoveryDvTests.cpp:519 | SameSourceSharesCanonicalKeysAcrossProfilesAndOutputPaths | live/queued/output selection parity |
| AnalysisRecoveryParityTests.cpp:977 | NativeP210MatchesV210PictureAndCaptionEvidenceForLiveAndQueuedConsumers | equivalent native content/evidence |
| AnalysisRecoveryParityTests.cpp:1145 | PreparedSourcesCarryAuthoritativeRangeWithoutChangingSamples | range labels do not rescale pixels |
| VideoFrameFormatterTests.cpp:1324 | CV210toP210VideoFrameFormatterPreservesPaddedEdgeSamples | aligned strides and final partial pack |

Extended precision/lifetime set:

| File:line | Method |
|---|---|
| AnalysisRecoveryParityTests.cpp:1052 | NativeP210RejectsShortPlanesAndRetainsSourceThroughAnalysisCopies |
| AnalysisRecoveryParityTests.cpp:1076 | P010AndP210UploadChoicesKeepOneNativeAnalysisDomain |
| VideoFrameFormatterTests.cpp:1371 | CV210toP210AVX2MatchesScalarBitExactly |
| DolbyVisionTunnelFormatterTests.cpp:371 | AllTwelveBitCodesSurviveScalarAndAvx2WithPaddingAndTails |
| DolbyVisionTunnelFormatterTests.cpp:400 | ThreadedRowsAndUnevenTailPreserveEverySample |

All files are in src/VideoProcessor-Test. These classes/methods were not selected by tools/critical_runtime_tests.json at this checkpoint. Consequently a passing current critical qualification is not proof that this entire conversion shortlist ran. This is a concrete build-gate validation gap; extending the focused CPU gate is a design recommendation, not an edit made here.

##60fps implications

V210 native evidence borrows the captured owner; it need not allocate or copy another full raster. P210/decoded422 occupies33.18MB/frame, about1.99GB/s for one full write at60fps. Avoid redundant conversion, repeated full-frame scans or duplicated worker jobs. Share immutable source evidence, not cached presentation pixels under a new identity.

Recent actual worker scans cost approximately20..30ms. Two independent workers can improve throughput, but average throughput does not guarantee a current result after startup/backlog retirement. Trace source-window changes, typed coalescing, dispatch/start/complete/read and exact source ownership. Use authoritative source frame duration/time scale, not display refresh or a capture-rate estimate that starts at zero. No new wait, threshold change or history-authority relaxation is justified by this audit.

## Proposed shared-source evidence contract

Use one immutable source-pixel interpretation and current source identity across detector, subtitle and presentation consumers. Distinguish at least: nominal signal black from declared range; independently observed matte floor/noise and its qualified source mask; classification/retention cutoff; measured source edges and uncertainty; retained pixel envelope; accepted crop/presentation authority. Do not call a legacy fallback floor a measured matte, and do not add picture contrast to an already padded classification cutoff.

A picture candidate may be rejected by current external components without transferring caption/UI geometry into native picture authority. Source facts must remain independent of viewport size, fitting and shader preparation. Known/remembered geometry can rank candidates and retain an explicitly held presentation, but cannot invent a current boundary observation. This separation addresses the demonstrated calculation and sparse-shape failures while preserving the verified packing/coordinate pipeline.