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
to see the eventual submission result. No automatic screenshots or native frame
capture are added.

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
