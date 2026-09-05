# Reading the render-load stats

What the Ctrl+I render rows mean, and which numbers are worth acting on.

Measured on: RTX 3060 Ti, i5-11400, 4K 23.976p, VP-owned DXGI swapchain.

---

## The idea in one paragraph

At 23.976p the display gives you a **41.71 ms slot** per frame. The graphics
card does its work in about **4.4 ms**. Everything else in that slot is the
renderer waiting for the display, which is exactly what you want to see. The
question the OSD answers is therefore a narrow one: **how close does the GPU
get to filling its slot?**

---

## The four rows

```
GPU Render:      4.42 ms avg, 6.71 peak in last 10s  (16%)
 - Budget:       41.71 ms/frame  (23.976 Hz)
 - Session peak: 7.94 ms  (19%)  over 143820 frames
 - CPU:          12% now,  34% session peak
```

**GPU Render** — how long the graphics card spends drawing, averaged over the
last 10 seconds, and the worst single frame in those 10 seconds. The percentage
is the **peak** against one refresh, because one frame that overran is visible
on screen while an average that hides it is what makes a bad setting look safe.

**Budget** — how long a frame is allowed to take, with the refresh rate it comes
from. This is a property of the display and is **constant for the session**; it
carries no window. (The 10 s on the row above belongs to the average and peak,
which is the only place a window applies. The window is measured in seconds, so
a 24p and a 60p run mean the same thing and can be compared.)

**Session peak** — the worst frame since the renderer started, warm-up excluded.
This is the row that answers *did we ever hit the ceiling*, and it is the only
figure that cannot be recovered later: the 10-second window forgets, so without
this a single bad frame twenty minutes ago leaves no trace. "Session" means since
the **renderer** started — a renderer restart begins a new one, because a restart
follows a source or output change and a peak carried across it would describe a
pipeline that no longer exists.

**CPU** — how much CPU VideoProcessor is actually using, as a share of the whole
machine, plus the worst it has reached. Same meaning as Task Manager. On 12
logical processors, one fully saturated thread is about 8%.

---

## How to judge a quality setting

1. Start playback and **wait ~15 s**. The rows read `settling...` until the
   warm-up is discarded — that is the feature working, not a fault.
2. Read the **GPU Render percentage**.
3. Change one setting, wait ~15 s, read it again. Compare **peak with peak**.
4. Glance at **Session peak** before you finish. A low average with a high
   session peak is a setting that will judder occasionally.
5. Repeat on **demanding content**. The session peak steps when the material
   gets harder, so a calm scene will understate what the setting costs.

**Rough guide to the percentage:** under 50% is comfortable, 50–75% is the point
to stop pushing, and above that a single heavy scene will drop a frame. At 100%
one frame needed an entire refresh to draw, which is a visible drop or repeat.

**State the refresh rate with any verdict.** The same 10.053 ms peak is 24% of a
24p frame but 60% of a 60 Hz one - comfortable on one, at the limit on the other.

---

## Two things that will otherwise mislead you

**The first ~15 seconds are not data.** A cold shader cache made one frame take
**459 ms**; with a warm cache it was still 187 ms. Those are compilation, not
render cost. Samples are discarded until the GPU timers resolve and 3 seconds
have passed, and the OSD shows `settling...` meanwhile. This guard re-arms after
a renderer restart and after a backlog recovery, for the same reason.

**A budget marked `source rate` means the percentages are missing, and that is
deliberate.** The frame budget normally comes from the measured display rate. If
that is unavailable it falls back to the source rate, and at 60 Hz output with
24p content the two differ by 2.5x - a percentage computed from the source rate
would report 16% for a frame actually using 40% of the refresh. Rather than show
a flattering number, the percentage is suppressed and the budget row says which
rate it used. The millisecond figures remain correct either way. The log carries
the same thing as `frame_period_src=display|source`.

**The 10 s peak decays, `Session peak` does not.** The window peak falls away
once the bad frame ages out — that is intended, so you can see the effect of a
change quickly. If you want the figure that never forgets, read `Session peak`.

---

## Measured baseline

4K 23.976p, 8 shader passes, `ewa_lanczossharp` / `hermite` / oversample /
sigmoid / peak detection / contrast recovery:

Measured on real Dolby Vision content, 4 min 10 s, 6049 frames:

| | |
|---|---|
| GPU average | 4.354 ms (10.4%) |
| **Session peak** | **10.053 ms (24.1%)** |
| Frame budget | 41.71 ms |
| CPU | 4% average, 5% peak |
| Drops | 0 |

**The session peak is driven by content, not by elapsed time.** It converges
within a given kind of content and steps when the content gets harder. On one
8.5-minute run it crept only 3.93 → 4.63 ms across six minutes, then a scene
change took it straight to 8.48 ms where it held flat. On a different clip it
reached 10.05 ms.

So peaks from runs on different material are not comparable, and **the way to
find the real worst case is to run across representative content, not merely
for longer** - ninety seconds of a demanding scene is worth more than ten
minutes of a calm one. Always say what was playing and for how long.

At one point the 10 s window read 4.60 ms (11.0%) while the session peak stood
at 8.99 ms (21.6%) - a 2x under-read, and the reason the session row exists.

**At 60 Hz that 10.053 ms peak is 60.3% of a 16.67 ms budget**, which is on the
line below.

---

## In the log

The figures are appended to the existing `Alpha presentation telemetry:` line,
at the end, so any existing parser of that line keeps working:

| Field | Meaning |
|---|---|
| `gpu_avg_ms`, `gpu_peak_ms` | Average and peak over the 10 s window |
| | *(every presented frame enters that window - not a periodic sample)* |
| `gpu_load_pct` | `gpu_peak_ms` as a percentage of one frame |
| `gpu_session_peak_ms`, `gpu_session_pct` | Session peak - worst frame since the renderer started |
| `session_frames` | Frames behind that figure |
| `settling` | 1 while warm-up samples are being discarded |
| `frame_period_src` | `display` (trustworthy) or `source` (percentages suppressed) |
| `window_s` | How much of the 10 s window has filled |
| `gpu_passes` | Shader passes in the last frame |
| `gpu_ms`, `gpu_frames`, `gpu_timed` | Newest frame, window size, timers resolved |
| `render_avg_ms`, `render_peak_ms` | See the note below |

CPU writes **no periodic line at all**. It logs once, as
`Process CPU peak: percent=... baseline=...`, only when a new session peak is at
least **4x this machine's own running baseline** and above 10%. A fixed
threshold was tried first and was useless - VP idles at 4-5%, so any number high
enough to be alarming would never have fired. If that line is absent, CPU never
spiked.

### Why `render_ms` is not on the OSD

`render_ms` and `swap_ms` (and the `render_avg_ms` / `render_peak_ms` summaries
beside them) are wall-clock time around the render call and the present. On this
swapchain they measure **waiting, not work**: `render_ms` reads about 30 ms of a
41.71 ms frame while the GPU is using 4.4 ms and nothing is dropping. Which of
the two holds the wait depends on the swapchain path, not on load, so neither
belongs on screen. They stay in the log because they are useful once frames
*do* drop — high `render_ms` **with** drops and a draining queue is a real
stall; high `render_ms` with no drops is just the renderer waiting for vsync.

---

## Known, not fixed here

`RollingPerformanceWindow` in the four frame formatters sizes its window as
`600 samples // 10 seconds @ 60fps`. At 23.976p that is **25 seconds**, so the
`10s Avg/Max` conversion row on the OSD is mislabelled on 24p content. Left
alone deliberately: it is on the capture/conversion hot path and does not belong
in a render-load change.
