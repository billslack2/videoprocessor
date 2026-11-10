# VP-0229: Resolve ambiguous r210 Dolby Vision tunnel alignment

## Status

Backlog

Requested 2026-11-10. This is future work; no implementation or deployment is included in this story. Start from the then-current remote beta and inspect any separately pending integration before deciding what applies.

## User story

As a viewer using a live HDMI capture card, I want Dolby Vision tunnel samples interpreted with the correct r210 alignment through dark and noisy scenes, so an ambiguous frame cannot silently corrupt picture color, black levels, aspect detection, or subtitles.

## Problem and scope

The RGB Dolby Vision tunnel decoder supports high- and low-aligned eight-bit carrier values inside r210's ten-bit channels. Its current distributed-pixel test and metadata CRC can both admit more than one interpretation, especially on dark or uniform frames. A valid metadata CRC alone does not prove packing alignment. When current evidence cannot distinguish the candidates, the decoder can choose by probe order; an incorrect selection can alter decoded picture samples.

The existing integration at codex/v2-magewell-integration-20261110 (commit 01535a619777ff9f270e7bd6c54f7d95d49ba68c at story creation) contains the prior bounded per-frame sampling improvement, but is a separately tested, unmerged branch; do not assume it is current beta or adopt it wholesale. The user's DeckLink capture logs have reported high8, but this is not independent proof of physical packing because the existing decision can itself be ambiguous. Inspect real captured source bytes and hardware/SDK format guarantees where available.

## Desired behavior and guardrails

- Establish alignment from independently credible evidence, accounting for packed pixels, metadata, source/device contracts and corruption. Preserve a sufficiently qualified alignment during later ambiguous frames in the same valid capture context; ambiguity alone must not switch it. Do not hard-code a device assumption without confirming its contract and real capture evidence.
- Define when alignment is established, contradicted, corrected and reset: device/input and actual format changes, signal loss or resync, source generation, and relevant decoder resets. Verify whether alignment can change without resync on real inputs instead of assuming the synthetic case is physically typical. Current evidence that contradicts an earlier alignment must not be ignored indefinitely.
- At an ambiguous cold start, do not claim a guessed alignment is verified or give uncertain samples authoritative black-bar, aspect or subtitle decisions. Determine and document an appropriate current-source pixel-preserving presentation fallback for the actual transport; packed tunnel pixels cannot simply be treated as ordinary RGB. Do not claim an impossible fallback is solved without demonstrating it.
- Live HDMI video must keep advancing on its normal schedule with bounded end-to-end latency. Do not hold or replay the last whole frame, increase queues, or wait for optional analysis to settle. Account for any unavoidable presentation limitations explicitly; do not fabricate detected/native AR authority or reuse stale movie pixels.
- Preserve current subtitle, black-bar and AR policies. This is an input interpretation fix, not a detector redesign. RGB8 ARGB/BGRA and non-DV SDR/HDR behavior must remain correct, including full/limited-range interpretation. Keep per-frame processing bounded and measure worst-case critical-path cost.

## Acceptance and validation

- Build independently generated high8 and low8 r210 fixtures with exact expected decoded Y/C values, including fully ambiguous startup, dark bars, quantized/noisy pixels, mixed low-level values, metadata CRC collisions, missing/damaged metadata, and decisive evidence arriving later. Assert exact pixels and truthful confidence, not just a passing CRC.
- Exercise established high/low alignment through ambiguous frames, contradictory evidence, source/format/resync resets, seeks, and out-of-order or missing optional evidence. Verify no wrong or stale alignment becomes crop/subtitle authority and that legitimate changes recover promptly.
- Compare against real DeckLink r210 samples on the user's machine when available. Distinguish real capture evidence from synthetic midstream packing changes and explicitly report any unsupported or unverified physical scenarios. Preserve a separate Magewell/RGB8 parity regression.
- Test live and queued analysis, dark-bar and subtitle-on/off cases, including Ben-Hur, LOTR and Eternals captures where applicable; verify picture/caption preservation, measured AR and geometry stability. Exercise source advancement, actual presentation gaps, bounded latency and worst-case per-frame cost at applicable input rates, not only average speed or renderer drop counters.
- Run relevant formatter/conversion, capture, subtitle, AR and both standard/beta renderer regressions. Report unresolved GPU-suite timeouts and real-hardware coverage separately. Keep any deployment isolated until explicitly authorized and a successful x64 Release build is verified.
