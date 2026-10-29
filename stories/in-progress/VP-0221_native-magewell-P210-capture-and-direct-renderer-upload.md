# VP-0221: Native Magewell P210 capture and direct VP Renderer upload

## Status

In Progress

Started 2026-10-29 after the user authorized removing the Magewell P210 -> v210 -> P210 CPU round trip. Source worktree: E:\codex\videoprocessor\magewell-native-p210-20261029, branch codex/magewell-native-p210-20261029. Baseline is freshly fetched beta c716bd8108b03b0ef279c14c584e097eecc29515 with the tested BGRA fix 7e746c749698146f1619c18b89908cf2c9fdef57 (PR #226) included as a dependency. The native P210 change is not yet implemented or qualified.

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
