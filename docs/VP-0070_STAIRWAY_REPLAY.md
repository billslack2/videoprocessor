# VP-0070 stairway recording regression

The private recording `2026-10-03 19-50-41.mp4` (SHA-256
`720273938b1b351e759a7f9d6e86cd3545ef5e04d847528e0c89596c6b347771`)
contains one diagnostic phase with old green boxes and a later clean movie-raster
phase. `tools/subtitle_stairway_clean_recording_manifest.json` records manually
reviewed line envelopes for the clean phase. The media is not checked in.
The replay decodes the recorded raster without OCR, resizing, or removal of old
overlays. Its sample cadence is 24000/1001; this desktop capture cannot prove
the identity or timing of the original source frames.

The lower line reaches into the lower black bar. Detection requires current bar
evidence, uses that line as an anchor and minimum horizontal extent, and joins
nearby picture-side line candidates into one cue. Queued observations validate
glyph expansion and keep one box through locally ambiguous frames. A scene or
cue change still clears the lock. Padding is added only after detection and
does not count as glyph evidence.

In a continuous natural-bar replay, the reviewed question cue displayed a
complete two-line box on 65/65 sampled frames, with one identical box and no
oversized frames. Its raw top edge was one pixel short of strict manual
containment; all 65 frames pass the report's two-pixel sensitivity check, and
the displayed box with 30-pixel top padding strictly contains both lines.
The next treaty cue had one identical, strictly complete two-line box on 84/84
frames. The reviewed blank interval had no boxes on 48/48 frames. The earlier
detector, replayed against this same interval, displayed only 59/65 question
samples with 4/65 correct line counts;
it frequently included the moving stair rail as a third row.

The separately recorded Arabic diagnostic cue displayed one complete,
line-correct box on 84/84 interior samples, with no boxes on the reviewed
pre-onset and after-cue blank samples. That recording has a green box baked into
some pixels, so it is diagnostic evidence rather than clean acceptance.

Run the production-path replay after building the x64 Release solution and
`tools/subtitle_clip_probe.vcxproj` in Release x64:

```powershell
python tools/test_subtitle_recording.py `
  --video 'C:\Users\bslac\Videos\2026-10-03 19-50-41.mp4' `
  --probe x64/Release/SubtitleClipProbe.exe `
  --manifest tools/subtitle_stairway_clean_recording_manifest.json `
  --output artifacts/stairway-replay
```

These reviewed cues were used during development and are regression cases, not
unseen validation. The held box is a bounded visual inference, not proof that
every subtitle style or moving background will work. The trial remains Blocked
pending broader live validation, including original-resolution playback and
cue transitions across display aspect-ratio changes.
