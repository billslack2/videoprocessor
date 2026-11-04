# Held-card fix and causal telemetry — 2026-11-03

The new recordings still show severe subtitle/framing flashes on deployed `378e090a`. In the second recording, “We need to trust Arishem’s design / for this planet.” alternates between reconstructed and original presentation around 18–21 seconds. Logged native geometry remains fixed while the fallback aperture expands. This is not established as disappearance of the source text or a stalled input stream.

A completed diagnostic capture preserved 96 original 3840×2160 V210 frames and worker records. The capture samples every fifth input frame; accepted sequence and physical source-frame number remain separately mapped. For live cue106, captures44–50 (accepted13568–13598; physical25072–25102) now provide a reproducible component failure using unchanged captured bytes.

The actual tracker produces held phases47–49 without forced held flags. Before the fix, its current-cue certificate rejects `LineCoverage`: a measured picture-side backing card ends at the picture edge while the glyph line continues into the encoded bar. The old rule required that finite card to contain the whole line. Commit `aeb94f38ef7edc89f5f10b3307c47c67a8a508bd` checks card coverage only over the independently measured picture portion, while retaining the existing full-line current-glyph checks. It does not enlarge pixel tolerances or change native AR authority.

Matched RED/GREEN evidence: the three held phases fail before and certify after; all seven frames qualify after, with zero omitted capture samples. Pixel bytes and the replay producer are unchanged. The harness maps the stride-five captures to explicitly synthetic consecutive tracker identities and models successful composition identically on both sides: this is CPU component evidence, not reproduced live history or actual GPU output. The producer receipts retain their pre-commit dirty source identities; audited candidate source/header hashes match final `aeb94f38`.

Telemetry commit `dc774495dc3629b06e33b46e400fc3e023dd25de` adds exact certificate, line-containment, physical-base and worker pixel rejection facts plus an offline causal report. Renderer detail is bounded and reports suppressed counts; sampled detail is not an every-frame trace. These diagnostics do not relax admission decisions.

Validation and remaining work:

- Final clean `aeb94f38`: Native1105/1105 passed, run `20261103-214538-3a4095a8`; this receipt explicitly is not full runtime qualification.
- Both focused UNKNOWN-work/current-video renderer checks pass. Motion/subtitle GPU suites pass 226/226 on Standard and 226/226 on Beta; exact receipts are included. These are focused checks, not full qualification.
- Deployed matching clean x64 Release host/Standard/Beta from aeb94f38; all 28 installed hashes verified. Configuration and saved state preserved before restart. Backup: `C:\Videoprocessor\vp\backups\trial-held-card-aeb94f38-20261103-222552`. Full qualification gate remains incomplete and is disclosed in the manual-review trial receipt.
- VP reopened as PID 31588 with saved settings. Initial current-process telemetry is in live.json, including full-window startup gaps and separate last240 summaries. This is not visual acceptance. Problem-scene Reconstruction playback acceptance remains open; do not mark all flashing resolved.
- Still open: cue34’s incomplete current glyph extraction/fringe discrepancy; moving-background cleanup/destination and worker-padding rejections; missing-top-edge UNKNOWN cases; full runtime qualification and playback acceptance. The cue34 comparison did not reproduce its live held failure: both versions pass with ordinary reacquisition, so it supplies no fix claim for cue34.

Evidence:

- [Recorded visual correlations](E:/codex/videoprocessor/subtitle-pixel-tolerance-20261103/artifacts/incident-20261103-210654/visual/visual-correlations.json)
- [Native cue106 subset and original identities](E:/codex/videoprocessor/subtitle-pixel-tolerance-20261103/artifacts/incident-20261103-210654/native-inspection/regression-subset-cue106.json)
- [Matched actual-capture RED/GREEN receipt](E:/codex/videoprocessor/subtitle-pixel-tolerance-20261103/artifacts/incident-20261103-210654/caption/held-replay-20261103-214303/paired-comparison.json)
- [Final-source component evidence audit](E:/codex/videoprocessor/subtitle-pixel-tolerance-20261103/artifacts/deployment-trial-aeb94f38/captured-contract-validation.json)
- [Final Native1105 receipt](E:/codex/videoprocessor/subtitle-pixel-tolerance-20261103/artifacts/qualification/runs/20261103-214538-3a4095a8/iteration.json)

Live observation retains an 8.424-second process-start-to-first-recorded-submit delay and a 35.914-second gap across renderer instances, followed by 429 ms and 162 ms transition gaps. The 35.914-second interval is explicitly window occlusion: iconic=1, client=0x0, present HRESULT 0x087A0001; 848 current frames rendered while submission was unavailable. It is not evidence of optional subtitle starvation. The 429 ms gap accompanies renderer replacement and 162 ms the scheduled fullscreen-entry live-queue reset. No user action is inferred. These intervals remain in the record rather than being hidden by per-instance summaries. The latest 240 Reconstruction submissions are all distinct/current; 104 are composed, and raw/pending presentation paths still occur. Peak measured active CPU work is 31.297 ms, with 9 display-period exceedances; GPU sampled peak is 15.616 ms at at most 49.2% coverage. These separate measurements do not establish the total critical-path deadline for every frame. Full-session continuity and Reconstruction visual acceptance remain open.
