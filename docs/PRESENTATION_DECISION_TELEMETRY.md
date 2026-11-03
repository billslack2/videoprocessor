# Presentation decision telemetry

Enable the existing **Enable enhanced logging** setting (`[logging] debug: true`)
and restart VP. The host explicitly forwards this opt-in to both renderer DLLs.
Normal mode does not emit these detailed records. Startup reports plugin support.

`VP presentation decision` records are bounded JSON in `logs/vp.log`, using the
existing asynchronous writer. They perform no pixel reads, GPU readbacks, detector
work or changes to rendering policy. Every semantic change is emitted immediately,
including multiple changes in one millisecond. Stable state emits a one-second
heartbeat with the number of output attempts since the previous record (`span`).

Each record joins a renderer instance/process, generation, viewport, source format,
analysis domain, policy and continuity to an exact **selected source packet**.
Input sequence is distinct from selected sequence. A missing or wrong-generation
packet says `facts_current:false` and `selected-source-facts-unavailable`; it never
borrows the latest input's analysis. Temporal outputs retain their own facts.
Complete-image cadence repeats retain the original displayed source facts.

The same record includes measured candidate, classification and measurement source;
accepted native geometry and its original accepting source; raw visible envelope;
caption analysis source, currentness, scan reason, detected/held/incomplete state,
capture and destination bounds; composition success, source preparation failures,
current composition and stronger cleanup permission, absence proof, continuation
rejection, final policy owner/reason, actual source/target rectangles, source/output
PTS, millisecond monotonic time, QPC and output ID. `rendered` and `submitted` report
the actual attempt result. Measured candidates do not imply accepted crop authority.

`prepare_failures` is a bit mask, in bit order 0..14:
composition-failed, preparation-missing, generation-mismatch, source-mismatch,
viewport-mismatch, mode-mismatch, policy-mismatch, continuity-mismatch,
geometry-missing, geometry-generation-mismatch, scene-verification-only,
non-native-origin, geometry-untrusted, retention-generation-mismatch,
retention-source-mismatch. The report decodes this mask. These mirror existing
preparation gates; logging grants no permission and never retries analysis.

Existing rendered-output screenshots now include `presentation_decision` in their
JSON sidecar. It describes that selected output, with the same instance/output ID
as the log, rather than the latest input. Screenshot readback occurs before Present;
its `submitted:false` is not a failed Present result. Correlate with the output log
to see the eventual submission result. Screenshot requests force the corresponding
output record even between normal heartbeats.

Create a retrospective report without source video pixels:

```powershell
.\tools\summarize_presentation_log.ps1 -LogPath C:\Videoprocessor\vp\logs\vp.log -OutputDirectory E:\codex\videoprocessor\evidence\new-report
```

Optionally pass `-ScreenshotMetadataPath <capture.json>`. The report produces a
chronological `timeline.csv`, loss/malformed-record summary and `decisions.jsonl`.
Existing pre-telemetry logs receive a limited geometry timeline; missing caption
facts are explicitly unknown. Original logs are never modified. Reports use a new
directory and refuse to overwrite earlier results. Async drop/truncation counters
are retained in the summary; an absent counter is not proof of complete recording.

For basic shrinking reports, inspect the first actual-source/target change and its
native geometry, scan/composition/preparation state and reason. Request source
pixels only when the remaining question concerns the correctness of pixel evidence.

## Recording and incident feedback

In Config > Logging, enable **Show recording timecode on video** and/or **Capture
suspected AR failures automatically**, alongside enhanced logging. Both default off.
The configuration keys are `[logging] video_timecode: true` and
`incident_screenshots: true`. Logging settings apply on the next VP start.

The small panel reads `HH:MM:SS.mmmZ INSTANCE Ggeneration Ooutput Ssource`.
It uses UTC, and the exact text plus full UTC milliseconds are logged for every
output while the panel is enabled. The panel is drawn after the movie image has
been cached: repeated images receive a fresh timestamp without changing their
source identity or contaminating the repeat cache. It uses a separate renderer.
A false `timecode_drawn` explicitly records a failed panel render.

Automatic capture treats an unexpected widening with unchanged native geometry,
lost advanced composition, or a disagreement between measured and accepted geometry
as a **suspected incident**, not proof of a detector bug. It saves an output image
at the failure and another two seconds later, plus up to 512 preceding decision
records and source-bound metadata. Persistent geometry disagreement is captured
once until it clears. Limits are 12 images per renderer session, a 20-second cooldown,
two pending GPU copies and one writer. It does not capture native input frames.
The GPU copy is queried without flushing or blocking; Map uses DO_NOT_WAIT.
Expired, full, unsupported or failed captures are logged. Images are at most
1920 pixels wide, preserve R10 code values in 16-bit PNG, and encode off-thread.

Bundles are in the existing screenshot folder, under `incident-PROCESS-INSTANCE-ID`.
The sidecar includes the selected decision and the eventual submission result;
its image stage remains after-render-before-present. A picture from a failed
submission is not claimed visible. Each capture's `.decisions.jsonl` contains the
preceding history; the after capture adds post-failure context.

Paste a recording timecode into the report command with `-VideoTimecode '<text>'`.
Pass `-IncidentDirectory <bundle>` to include saved context and an image index.
`video-match.csv` is an exact instance/generation/output/source match, never a
nearest-frame guess. Missing matches and dropped logs remain explicit.

Use the bundle to identify the first changed decision and failed gate, write a
regression for that source/state transition, fix the responsible producer/policy,
and compare the same recording interval after repair. Pixel-level source analysis
is reserved for questions the decision trail and displayed images cannot answer.
