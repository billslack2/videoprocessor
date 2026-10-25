# Persistent subtitle-transition cadence correction

User steering: NLS was also a real earlier issue and the user reported it fixed;
the remaining trigger is subtitle profile changes (notably clear/yellow), also at
60fps YouTube TV. Current failure continues with NLS off. Recovery can take
seconds to a minute or two. A brief internal recovery is acceptable; an HDMI
resync is not requested. The earlier precision-only fc20e916 / deployed 2a224308
was insufficient and is not treated as accepted live behavior.

Incident: yellow selected at 14:21:06, then paired cached repeats and adjacent
source retirements with ready queue. Slot lead 0.02-1.14ms exceeds 10us precision.
GPU samples around 5-6ms at24Hz and4ms at60Hz do not show saturation; CPU render
wall time includes pacing / driver backpressure. A strict binary due gate used
for both presentation and dropping created boundary chatter.

Candidate feature head6f6c0d2e58c2ea732bbe192ef5c7261d8df7dbbb updates draftPR204.
Source remains based on latest fetched beta8c453746. Separate +J eligibility and
-J retirement boundaries use J=min(3ms, quarter-refresh), retaining timestamp
precision floor. It does not retime raw PTS or change source speed. TrueDCdrift
still crosses the bounded band. Original24-to60 3/2phase is preserved.
Output-profile changes coalesce a one-successful-present recovery. The changed
frame pays warmup; repeat clock/feedback/cache eligibility reset once, and the
next source anchors cadence without requesting display-mode/HDMI resync.
Subtitle pixels and NLS rendering code are unchanged.

Validation:
- Feature6f6c clean x64Release solution build passed,11warnings/0errors.
-20clock/transition tests passed: matched24/59.94/60 with alternating+/-1.14ms,
  eligibility versus retirement, jittered24-to60phase, realdrift, failure and
  coalescing semantics of one-present recovery; existing stalls/feedback covered.
- Combined621223092dbea7e088947523a1c9f260c4bdb930 preserves installed tone GUI
  sources and earlier fixes. Clean x64Release build passed,11warnings/0errors;
  all425focused native/config/Motion/cache/NLS/tone compatibility tests passed.
- Luna xhigh regression and mpv-source reviewers researched bounded hysteresis;
  final regression review found no concrete blocker. FullRenderLocked/ObservePresent
  ordering is source-reviewed; helper test is not full production integration.
- Previous429GPU/subtitle and421combined passes remain historical evidence for
  unchanged pixel paths; they are not counted as new full-suite passes.

Deployment: matching Release host+primary/Beta renderers and receipts verified;
active configuration was backed up and byte-preserved (every-refresh ON,depth3,
Motion off), tone GUI Config binary/session preserved. Backup/hash evidence in
 deployment.json. Graceful close succeeded this time. Restarted responsivePID82768;
host/renderer both log clean62122309. Restored player from minimized startup.
Logs confirm every-refresh active and one-present internal cadence restart with
hdmi_resync=0. Initial60fps sample after startup settled has no further repeat
entries and no renderer drops. This does not establish live transition acceptance.

Please retest repeated clear/yellow/classic/reconstruction changes at24/60Hz,
with every-refresh ON and NLS independently off/on. Check immediate recovery,
continued cadence, and subtitle placement/quality. Story remainsReview pending
that user/hardware confirmation; no merge of PR204 has occurred.

Rollback: stop VP, restore host and both renderer DLLs plus matching runtime
receipts from the backup recorded in deployment.json, restart fromC:\Videoprocessor\vp.
Keep current config unless user requests setting rollback. No config edits were
made in this deployment and no installer/publicrelease was produced.
