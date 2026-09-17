# VP-0189 live playback follow-up, 2026-09-17

The user reproduced remaining crop jumps on installed 884029b1 while watching Women in Blue S02E06. Saved the 11:35 screenshot, live log, and paused-state log before changing binaries. A video is not required to establish the specific conflict below; visual acceptance remains pending another playback.

At 08:24:00, source sequence 4885/event 18, the trusted rectangle was 192,0–3652,2160 and the observation was 188,0–3652,2160. The current excluded-band scan was safe; sampling_reaffirmed=1, outward_visible=0, and the only recovery gate was presentation-owner-unresolved. Nevertheless, current coarse left-edge expansion forced full raster. Recovery ended after 578 ms. Event 16 lasted 4641 ms and also included unsafe/outward evidence; the targeted change does not claim to resolve every such event.

Cause: the coarse presentation envelope was published before sampling equivalence was applied to crop authority. Its independent edge flags could therefore override the later safe reaffirmation. This was an incomplete fix in 884029b1.

Correction a7dc020a suppresses publication of a new coarse expansion only when a current trusted crop, affirmative bar observation, valid current retention scan, safe excluded bands, no positive outward pixel extent, and sampling-equivalent complete envelope all agree. The exact existing crop is retained. Real outward pixels, material changes, new cropped axes, invalid scans, and provisional observations retain their existing behavior. Existing held envelopes still expire normally; no timing or black thresholds changed.

The normal log now emits reason=pixel-safe-sampling-reaffirmed with source sequence, generation, base and measured bounds, at most once every two seconds while suppressing an expansion. No extra scans or trace environment settings are required.

Added regression coverage for repeated four-pixel pillarbox jitter without crop withdrawal and negative cases for unsafe bands, larger envelope extents, combined edge growth, missing bar authority, and raster mismatch. All 1184 native tests passed before the diagnostic addition; final committed Release build/test results are recorded separately before deployment.

Next playback: replay the gradual transition with player controls hidden once seeking finishes. Check whether the abrupt full-frame drops disappear and whether the gradual format change is acceptable. If problems remain, record the visible transition and correlate it with the saved log; do not assume every full-frame event shares the fixed cause.
