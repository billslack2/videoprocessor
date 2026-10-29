# VP-0221: Native Magewell P210 capture and direct VP Renderer upload

## Status

Review

2026-10-29: User-authorized [PR #226](https://github.com/billslack2/videoprocessor/pull/226) merged into beta at `561fcab49be5285c0059a02713f00502f53ae256`, using the already qualified clean source `5dfb96dc`. The dependent focus/stats correction merged as [PR #227](https://github.com/billslack2/videoprocessor/pull/227), followed by madVR NLS [PR #200](https://github.com/billslack2/videoprocessor/pull/200). [Combined integration evidence](../assets/VP-0212/integration-20261029/README.md). Broader capture performance/card qualification remains open, including the newly reported sixty-Hz delivery concern.

Implemented at 5dfb96dc7c35b4ffa28543e851aa3150f5e5be8d in PR #226. Complete x64 Release solution build, 487 focused standard checks, 16 beta feature checks, and both 208-test GPU/caption suites passed on the exact clean commit. The user's Pro Capture HDMI 4K+ validated ordinary SDR and HDR10 native P210 plus a clean content transition through BGRA Dolby Vision. User confirmed the native-capture OSD label and SDR picture.

Deployed the matching host, both renderers, Config and ConfigDiscovery; all five hashes were verified during hardware validation. Backup: C:\Videoprocessor\vp\backups\magewell-native-p210-5dfb96dc-20261029-110238. No configuration edits. A newer concurrent stats build e6f8c62 subsequently superseded the host/renderers; Git confirms it contains 5dfb96dc, and it was preserved. This validation applies to 5dfb96dc, not the later stats changes. PR was subsequently merged on 2026-10-29; no public release update is included in that merge. Full driver/card matrix, calibrated color/performance and extended queue-pressure qualification remain separate.

## Objective

For ordinary Magewell YCbCr capture with VP Renderer and no forced P010 conversion, deliver the card's P210 planes directly to the renderer's existing 4:2:2 GPU upload. Preserve the captured samples and source-coordinate analysis without either CPU packing pass. TV-led DV continues through byte-preserving RGB capture and its DV decoder.

## Contract and acceptance

- Add an explicit native P210 frame encoding with correct two-plane size, pitch, limited-range 10-bit/shift-6 interpretation and versioned host/renderer ABI.
- Enable this delivery only for a compatible VP Renderer consumer; retain v210 delivery for DirectShow and explicit P010 conversion. Renderer/format/mode changes must not deliver a stale layout to the new consumer.
- GPU upload borrows retained capture planes; no replacement conversion buffer, CPU P210-to-v210 repack, or v210-to-P210 unpack is used on the native route.
- Live and queued active-picture/subtitle analysis read native P210 with identical source samples and geometry to the proven v210 reference. Keep detection independent of screen/fitting/NLS choices.
- Verify stride/size bounds, odd/invalid geometry, row/chroma precision, SDR/HDR metadata, retained-buffer lifetime, failure/signal recovery, mode changes, and the existing BGRA DV path.
- Build matching x64 Release host/renderers, run relevant native/GPU tests against both shipped renderer dependencies, and validate ordinary YCbCr on the installed Pro Capture HDMI 4K+.
- Record CPU packing work removed and actual test limits. Do not label GPU upload as zero-copy capture. No public release or unrelated story-status change is implied.

## Initial evidence

The October 7 Magewell tester note explicitly excluded direct P210 renderer optimization. Current code captures P210, repacks it into v210, then VP Renderer unpacks it into P210 for upload. The installed card captures P210 successfully. Separate v210 probes at small and 1920x1080 geometry returned success without completing or modifying the destination, so native v210 is not selected merely because its SDK constant exists. User confirmed ordinary versus DV output follows content, rather than requiring a setting change.

## Implementation and focused verification — 2026-10-29

PR: https://github.com/billslack2/videoprocessor/pull/226 (updated existing BGRA fix PR; not merged or publicly released).

- Added the native P210 frame contract, two-plane layout/bounds validation and ABI 32. Existing encoding identifiers are preserved.
- Compatible VP Renderer consumers borrow the retained SDK capture destination directly for two-plane upload, without a capture repack, conversion formatter/worker or intermediate conversion image. Legacy DirectShow and explicit P010 consumers still receive v210.
- Capture renegotiation and delivery guards cover consumer changes; the renderer rejects incompatible forced-P010 P210 states. Source analysis retains full-height chroma and original 10-bit codes, including all precision-sensitive picture-evidence paths.
- Complete x64 Release solution build succeeded on the clean exact commit. All 487 focused native tests passed, including nine new native P210 tests and existing capture/DV/analysis/ABI regressions. The first run's one failure was a test fixture below DisplayMode's supported raster minimum; corrected before the exact-commit passing run.
- Required GPU/caption tests against libplacebo 360 and 377 subsequently passed, as did the SDR/HDR10 hardware and DV transition checks recorded below. User configuration already selects Magewell and VP Renderer conversion NONE; no configuration edit is required.

Local evidence: E:\codex\diagnostics\magewell-native-p210-20261029, including native-p210-final.trx and the pinned GPU run gpu-5dfb96dc. Source worktree remains E:\codex\videoprocessor\magewell-native-p210-20261029; PR branch codex/magewell-bgra-capture-20261029 was fast-forwarded to the same commit.

## Final verification and deployment — 2026-10-29

Evidence: [validation and hardware results](../assets/VP-0221/native-p210-5dfb96dc-20261029/README.md).

- Exact source 5dfb96dc7c35b4ffa28543e851aa3150f5e5be8d; beta base c716bd8108b03b0ef279c14c584e097eecc29515; API 32 host/renderers matched.
- Passed 487 focused standard tests, 16 beta checks, and 208 GPU/caption tests for each shipped dependency. Receipts include loaded dependency hashes and clean source identity.
- Hardware confirmed direct P210 SDR at 4K59.941 and HDR10 at 4K23.976, including 12-bit PQ/BT.2020 wire input converted to the existing 10-bit P210 contract. Both native analysis consumers agreed; no CPU capture repack or renderer conversion formatter was used.
- BGRA DV transition passed CRC and complete DM/L1 checks, with zero rejected/failed-CRC copies over the logged 600-picture interval. SDR picture/OSD was explicitly accepted by the user.
- Installed hashes were verified after deployment and during hardware checks. Final audit detected and preserved the newer e6f8c62 descendant; its native path remains active. No active configuration edits were made. Full card/driver and long-duration/performance qualification are not implied.
