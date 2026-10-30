# Magewell high-rate capture qualification — 2026-10-29

PR #228: https://github.com/billslack2/videoprocessor/pull/228

Exact feature source `193c1a1c6f8e1481cdb11b71c55b0e664d62de25`, based on beta `9f5c9749a6732d3ca55a4c45cae7b61b04192811`, merged at `ad32c3fb05b39df981d3f36a65a61fd857250843`. The merge tree matches the tested feature tree exactly.

All 238 focused capture/DV/format/OSD/focus tests and both 208-test renderer suites passed. The direct 30-second UHD59.94 RGB24 pipeline probe recorded 59.940779 timestamp fps, zero missed input intervals/duplicates and zero differences across 24,883,200 same-picture RGB channel bytes. Input range was restored.

Full VP sustained DV59.94, DV50 and native HDR59.94/P010 with zero steady capture misses. DV metadata/CRC checks passed; user confirmed smooth motion and clean transitions. The session total retains one startup miss. The detailed live record distinguishes format-start windows and the later 50 Hz source from the earlier true 59.94 Hz test. Background GPU tests were active during these observations.

Native P010 uses the compatible VP Renderer route above the card's ~1.6 GB/s transfer budget. Lower-bandwidth capture retains P210. Raw high-rate DV captures BGR24 and expands it losslessly with a bounded CPU worker overlapping the next DMA. SDK calls/callbacks stay on the capture thread; leases/drain barriers protect buffer lifetime and format changes. Capture fps/misses are separate from renderer drops. Shared host/renderer ABI is 33. No settings changes; verified backups and installed hashes are recorded. DeckLink selection is unchanged.

Hardware qualification applies to the installed Magewell Pro Capture HDMI 4K+ and PC. Native UHD60 P210 still exceeds bus capacity; RGB24 is lossless for the encoded DV carrier, but would reduce ordinary HDR precision to 8 bits. Broader card/driver and DeckLink UHD60 DV acceptance remain separate.
