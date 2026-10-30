# VideoProcessor 1.4 RC6 â€” Magewell update

This RC6 refresh improves Magewell Dolby Vision capture and sustained high-frame-rate playback. It is published on the **Stable** update channel.

- **Magewell Dolby Vision:** preserve the RGB tunnel bytes and handle the card's BGRA layout and input range correctly. High-rate DV now captures RGB24 and expands it losslessly for the existing decoder. AVX2 packing overlaps the next capture transfer, removing the serial delay that caused capture-frame losses at 4K59.94/60.
- **Lower-rate capture performance:** native P210 reaches VP Renderer directly, removing the P210-to-v210 and return CPU packing passes while preserving 10-bit 4:2:2 samples.
- **Automatic bandwidth-aware formats:** with VP Renderer, retain native P210 where the transfer fits; select native P010 for high-rate ordinary SDR/HDR when it does not. DV uses its byte-preserving RGB24 path.
- **Honest capture statistics:** show delivered/expected capture fps and timestamp-based upstream capture-miss estimates separately from renderer drops. Keep the older working statistics; temporarily pause the unreliable new CPU/wait measurements.
- **Fullscreen focus:** retry foreground and keyboard focus once per second during the first ten seconds after fullscreen entry, with cancellation on exit or shutdown.
- Includes the recent DV capture-format support and shared active-profile status improvements.

## Magewell bandwidth limits

The tested Pro Capture HDMI 4K+ uses PCIe Gen 2 Ã—4 and provides roughly 1.6 GB/s of practical capture bandwidth. UHD60 P210 requires about 1.99 GB/s, so removing CPU conversion alone cannot sustain that transfer. P010 keeps 10-bit precision and reduces vertical chroma to 4:2:0. High-rate ordinary 50/59.94/60 Hz capture can therefore use P010; lower-bandwidth modes retain P210.

RGB24 is lossless for the encoded DV carrier and requires about 1.49 GB/s at UHD59.94. Using ordinary RGB24 for native HDR would instead reduce precision to 8 bits; this release does not substitute it for native HDR capture. The installed Magewell driver did not provide a usable packed 10-bit 4:2:2 capture path. DeckLink capture policy is unchanged; 59.94/60 Hz DeckLink DV still requires separate hardware qualification.

## Verification and downloads

On the tested Magewell card, live 4K59.94 DV, 4K50 DV and ordinary 4K59.94 HDR/P010 sustained their expected rates with zero steady capture misses. DV metadata/CRC checks passed, and motion and transitions were visually accepted. Startup misses remain visible in the session total. Results apply to this tested card/PC and are not a complete capture-card matrix.

Download the [Setup EXE](https://github.com/billslack2/videoprocessor/releases/download/1.4-rc6-magewell-20261029/VideoProcessorSetup-1.4.0-rc6-5b8132b6f2a3.exe) or [x64 Portable ZIP](https://github.com/billslack2/videoprocessor/releases/download/1.4-rc6-magewell-20261029/VideoProcessor-1.4.0-rc6-5b8132b6f2a3-x64-Portable.zip). Earlier RC6 releases remain available for rollback.

Windows 10/11 x64. Setup and the portable ZIP include VP, both renderers, Config and the updater. Existing settings are preserved. For a fresh portable folder, copy `VideoProcessor.cfg.example` to `VideoProcessor.cfg`. Select the **Stable** update channel for this release.

The Windows binaries and installer are unsigned; update metadata is signed. Clean Windows without Visual Studio and interactive Config/physical-display coverage are reported separately from automated qualification. Older versions may not understand newer settings on rollback.

Production application source matches merged beta `ad32c3fb05b39df981d3f36a65a61fd857250843`. Release source `5b8132b6f2a3d900d189496dcc86e002c300ae26` retains the previously published RC6 packaging fixes and two test-only expectation corrections.

The clean x64 Release rebuild passed all 3,538 native tests. Capture qualification also passed 238 focused tests and 208 GPU/cue tests on each shipped renderer dependency at the application-equivalent feature commit. Another 11 Config UI, 29 updater-policy, 19 helper-control, and 326 isolated real-installer lifecycle checks passed, along with corrupt-payload recovery, runtime/installer packaging, portable-update and configuration-preservation checks.
