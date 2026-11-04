Active crop-shift evidence, PID61168, 2026-11-04. No UI, profile, playback or production-code changes were made.

Settings snapshot: Scope viewport, centered 2.35 screen; automatic crop enabled; narrower/wider fill both enabled, limits 2.32/2.42; Reconstruction selected. The source-crop log also prints those same fill flags and limits on the failing sources.

Preserved range: 12:01:33.028Z 6EC7FF9D G1 O693194 S693194 to 12:22:14.118Z 4368DA22 G2 O22874 S22877, 29,711 decisions. Restricting to consecutive current/submitted Reconstruction sources with unchanged context and native bounds yields 146 geometry changes: 6 horizontal-fill transitions (three out-and-back pulses), and 140 changes involving an expanded envelope. Of 241 detected/no-detection transitions, 227 keep geometry unchanged; 14 change it.

Small pulse: 4368DA22/G2, S1413/1414/1415 at07:07:18.909/.948/.990ET. Native stays[0,272,3840,1886]. Actual source[24,272,3816,1886] becomes[0,272,3840,1886] for the current unaccepted candidate, then returns. Target center stays(1280,720), but height changes 1089.3618→1076→1089.3618; top/bottom each move 6.6808px. The log explicitly says current raw pixels are presented without optional fill. S1417/1418/1419 repeats it; S1329/1330/1331 shows the same pulse during a cue handoff. This is a fill/scale change, not vertical center translation.

Current source evidence: S1414 logs search_complete1/1, work_limit0, current independent bars275..1884, current_scan14.731ms and raw candidate[2478,1850,2538,1894]. Its horizontal span is far inside the requested24px side crop. Tracker rejection is picture-glyph-acquisition-neighbor-incomplete. S1330 also has complete search/current bars, analyzed/shared-current evidence, and a pixel comparison that rejects component coverage. Palette validity and rawEvidenceComplete are not directly logged for these sources; zero precondition mask with pixels_compared0 is not affirmative proof. No native capture exists for this exact interval.

Larger fallback: 12:10:00.946Z 4368DA22 G2 O5295 S5298, 12:10:00.983Z 4368DA22 G2 O5296 S5299, 12:10:01.025Z 4368DA22 G2 O5297 S5300. Cue78/native stay fixed; composed[24,272,3816,1886] switches to pending raw[0,272,3840,2026] and back. Target horizontal inset changes 0.3037→87.5402→0.3037px while vertical extent stays fixed. This 140-row raw-visibility expansion is separate from the small fill pulse.

Proposed assertions:

- A complete exact-current candidate inside the requested horizontal crop must retain configured fill across absent→unaccepted-current→composed→absent states, with unchanged native/context. Test both wider/narrower options and their aspect limits.
- Disabled or out-of-limit fill must remain disabled. Stale/incomplete/unavailable-palette evidence and a caption/UI crossing the crop margin must retain their conservative behavior.
- Preserve expanded raw-visible envelopes, current caption pixels and native authority separation. Do not make the small fill fix hide bottom-bar captions or bypass pending composition.
- Submit every distinct current source without waiting for optional processing.

Reproduce aggregate analysis: python analyze_crop_transitions.py --log runtime.log --process 61168 --output .. Exact source rows are in repro-cases.json; scan log lines in current-scan-evidence.json; all filtered transitions in crop-transition-summary.json; preservation.json hashes original settings/log slice.

Recording discovery: native Win32 directory enumeration still exposes yesterday’s latest MP4. OBS PID66896 launched07:06:04. The new OBS log reports 0 bytes in directory metadata but shared-handle reads return 9284bytes through07:06:10.943, with no Recording Start/output-file announcement. Configured recording path is C:\Users\bslac\Videos, hybrid_mp4. A new recording file cannot yet be identified from this evidence; recorder controls were untouched.
