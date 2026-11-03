# VP-0225 follow-up, 2026-11-03

- Source `7c264419b7e648fa03992ee58512d7de1559a176` completed the x64 Release Native run with 1,084/1,084 tests passing (`20261103-141551-53d4a8dc`). This is a Native result, not qualification.
- Matching focused CPU/Release host and renderer checks were still running when these bounded receipts were assembled; their result will be recorded separately.
- On code `958c230d43bf0f08352d3760e29513147149dc0a`, a CPU-only benchmark used a validated V210 source view, three runs and 32 repeats per phase. Static footprint medians were 2.92–3.02 ms after versus 3.52–3.62 ms before. Exact phase failures remain in the recorded output. This is a microbenchmark, not end-to-end throughput evidence.
- The existing live trial remains source `69c51f18f516866912d89c0900c7da9478ab9bf5`; no deployment is recorded here. The short Reconstruction window has 240/240 current selected/submitted/composed/prepared sources, selected step 1, and a maximum submission log gap of 49.6082 ms over 14:14:39.623–14:14:49.589Z at 23.976 Hz. Native/source shapes were `0,272,3840,1888` and `22,272,3818,1888`. Visual acceptance and full qualification are explicitly false.
- The saved application state changed to Reconstruction at 14:12:33 without agent input. This note records observation only and does not infer the cause.

- User playback report: paused screenshots look fixed, but live playback still often flashes zoom outside the bars and briefly loses subtitles while OSD AR remains stable; user reports it is not reliable or acceptable. This remains the primary unresolved reproduction; the short log window is not acceptance.

- Matching x64 Release host, Standard renderer, Beta renderer and focused test builds all succeeded in 20261103-141756-16008dbd; the three focused CPU tests passed: ValidatedSamplerMatchesCanonicalCodesAcrossFormatsRangesAndCoordinates, CurrentCueProofReceiptReadyWhileExternalTraceSinkBlocks, and HeldCurrentPreparedCuePreservesOriginalPendingSeedWithoutRenewingItsAge. This focused run is not GPU or full qualification evidence. The TRX is recorded by SHA-256, not copied.
