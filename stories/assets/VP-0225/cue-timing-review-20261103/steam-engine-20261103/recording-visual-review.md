# Steam-engine recording review — 2026-11-03

## Scope and sources

Reviewed `C:\Users\bslac\Videos\2026-11-03 12-53-07.mp4` (2560×1440 desktop recording, 60 fps, 42.62 s) against `C:\Videoprocessor\vp\logs\vp.log`, restricted to 12:53:07–12:53:50. Captured stills and reduced log excerpts are in this folder. The active settings read from `VideoProcessor.cfg` and `VideoProcessor.state` were `standard_scope`, Scope viewport/zoom (2.35:1), and Reconstruction subtitles; scope crop/fill and subtitle fit were enabled.

## Findings

**Physical detection remains steady while presentation geometry changes.** The OSD repeatedly reports detected/current or retained AR around 2.39:1, with native bounds near `[0,270,3840,1886]` (3840×1616, 2.376:1). The slight difference between the measured/native rectangle ratio and the displayed 2.39 label is visible throughout. The screen viewport remains 2.35:1.

On composed frames, the log’s actual output target is approximately 2.349:1 (`[0.421,136.968,1999.579,988.032]`). On raw-fallback frames, the current source rectangle and target change with the fallback bounds: examples are about 2.047:1 for `[0,150,3840,2026]` and 2.188:1 for `[0,270,3840,2026]`. These are presentation/canvas changes; they do not correspond to a newly detected physical aspect.

| Recording time | OSD output/source | Visible state | Log/current-source evidence |
|---|---|---|---|
| 17.7 s | O2679 / S2681 | Loading ring over the same dark shot; subtitle is very dim. OSD says AR in use 2.20:1; detected current briefly reads 2.45:1 and retained 2.39:1. | Corresponding source row is raw fallback; target about 2.03:1. |
| 17.8–18.0 s | O2681 / S2683; O2686 / S2688 | Same scene/loading ring. OSD in-use label changes from 2.03:1 to 2.35:1; detected/retained returns to stable 2.39:1. | Logs alternate composition and raw fallback; source/output IDs advance. |
| 18.7–18.9 s | O2708 / S2710; O2715 / S2717 | Dialogue subtitle “I mean, they only have had the wheel 1,000 years, so…” is bright in one still and dim in the next. OSD label changes 2.20:1 to 2.35:1 while detected/retained remains 2.39:1. | Input 2708 is composed at the native target; nearby fallback rows use a wider source canvas. |
| 34.0 s | O3070 / S3072 | “Engine. It’s a steam engine. / It’s too soon.” is visible. OSD says AR in use 2.35:1; detected/retained 2.39:1. | Current composition is true; caption bounds are `[1258,1778,2578,2022]`, extending below native bottom 1886. |
| 34.2 s | O3073 / S3075 | Subtitle is absent for this sample. | Current empty native bands, `no-bright-ink`, and confirmed absence are logged. |
| 34.5 s | O3078 / S3080 | New subtitle “And there she is.” is visible. | Fresh detected cue32 and current composition; caption `[1518,1926,2310,2022]`. The blank interval between samples is about 0.21 s. |
| 36.0 s | O3118 / S3120 | Cue32 appears dim/gray. OSD says 2.35:1 presentation; detected/retained 2.39:1. | Composed/current composition true; actual target is about 2.349:1. |
| 36.2 s | O3120 / S3122 | Same caption is bright; OSD says about 2.05:1. | Raw subtitle fallback, source `[0,150,3840,2026]`, target about 2.047:1. `last_submitted_source=3120`. |
| 36.8 s | O3137 / S3139 | Same caption remains visible; OSD says about 2.20:1. | Raw fallback source `[0,270,3840,2026]`, target about 2.188:1; detected/retained remains 2.39:1. |
| 37 s through clip end | O3141 / S3143 onward | “And there she is.” remains on-screen in the sampled stills. | The log continues raw fallback with `subtitle inspection pending`; no later composed source is recorded after 3120. Source IDs continue advancing through the end of this excerpt. |

The loading-ring interval also shows source IDs and timestamps advancing at roughly the 23.976 Hz input rate. The OSD reports 23.98/23.98 capture and zero capture misses, with two render drops in the sampled status panel. The ring appears in the recorded video image; this review cannot tell whether it is a source-player buffering overlay or a true paused source frame. There is no raw HDMI capture here to prove per-pixel source progression or to distinguish that overlay from source behavior.

The 34.2 s subtitle gap coincides with an explicit current empty-band/absence decision, followed by a newly detected cue at 34.5 s. With no raw input recording, the evidence does not establish whether that roughly five-frame gap is natural subtitle timing or a lost subtitle.

## Bottom line

The aspect oscillation visible to the viewer is not evidence that the physical detector is oscillating: its retained native bounds remain effectively fixed. The actual output rectangle does oscillate because presentation switches between native-cropped composition and a broader raw current-source fallback. The strongest sustained failure is after source 3120: the same current cue remains visible, but the renderer stays on pending raw fallback for the rest of the recorded interval, changing the fitted picture from about 2.35:1 to roughly 2.05–2.19:1. This also changes the caption’s position/brightness relative to the picture.

## Evidence files

- Contact sheets: [0–14 s](contact_00-14s.jpg), [15–29 s](contact_15-29s.jpg), [30–42 s](contact_30-42s.jpg)
- Transition frames: [17.7 s](frame_017.7s.png), [17.8 s](frame_017.8s.png), [18.0 s](frame_018.0s.png), [18.7 s](frame_018.7s.png), [18.9 s](frame_018.9s.png), [34.0 s](frame_034.0s.png), [34.2 s](frame_034.2s.png), [34.5 s](frame_034.5s.png), [36.0 s](frame_036.0s.png), [36.2 s](frame_036.2s.png), [36.8 s](frame_036.8s.png), [37.0 s](frame_037.0s.png)
- Correlated log extracts: `presentation_transitions_1253.txt` and `key_log_excerpt_steam.txt`

## Follow-up: 0.1-second and 60-fps check of the steam-engine cue

The 0.1-second contact sheets from 27–34 s show the lead-in “Phastos, this steam…” followed by “Engine. It’s a steam engine. / It’s too soon.” The latter is logged as cue31 (for example source3017) and remains on-screen through the 34.0 s sample (O3070/S3072). It does **not** blink out and back in during that same cue in the checked interval.

To check for sub-0.1-second gaps, I counted grayscale pixels above fixed thresholds in the recorded caption region (x=1100–2049, y=1020–1249) for each 60-fps video frame from clip time 27–35 s. Across the 147 frames from 31.6 s through 34.05 s, the count above 120 was 5,783–11,254 (median 5,971); the count above 200 was 1,155–8,159. There was no near-empty frame while cue31 was visible. At 34.083 s the count above 120 fell to 92 and stayed around 91–96 through 34.267 s. The new caption “And there she is.” is visibly present by the 34.3 s sample; the log records cue32 at source3080. Thus the measured blank is a short gap between cue31 and cue32, not a brief loss and restoration of the same steam-engine text. The log’s source3075 `cue=0`/`no-bright-ink`/`absence=true` is the renderer’s decision for that frame, not independent proof that the HDMI source contained no caption.

| Clip sample | Visible caption | OSD output/source | Log cue |
|---|---|---|---|
| 31.7 s | “Engine. It’s a steam engine. / It’s too soon.” | O3015 / S3017 | cue31, current composition |
| 34.0 s | Same cue31 text | O3070 / S3072 | cue31 still current |
| 34.1 s | No caption visible | O3072 / S3074 | Transition interval |
| 34.2 s | No caption visible | O3075 / S3077 | source3075 logs cue0 and no-bright-ink |
| 34.3 s | “And there she is.” | O3077 / S3079 | cue32 logged at source3080 immediately after this sample |

Additional 0.1-second sheets: [27 s](engine_detail_27s.jpg), [28 s](engine_detail_28s.jpg), [29 s](engine_detail_29s.jpg), [30 s](engine_detail_30s.jpg), [31 s](engine_detail_31s.jpg), [32 s](engine_detail_32s.jpg), [33 s](engine_detail_33s.jpg), [34 s](engine_detail_34s.jpg). The per-frame pixel counts are in [engine_caption_pixels_27-35.csv](engine_caption_pixels_27-35.csv).
