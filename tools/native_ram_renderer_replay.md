# Native RAM capture through the actual renderer

`replay_native_ram_renderer.py` drives the existing isolated native renderer host,
using the production plugin factory. It does not start VideoProcessor, alter the
installed configuration, change display mode, repeat frames for acquisition, or
supply sidecar picture rectangles as detection truth.

## Prepare and run

First export a RAM recording with `export_native_ram_replay.py`. Supply the
verified clock frequency from that capture device/build: timestamps alone do not
establish their units. DeckLink and Magewell clocks may differ.

```powershell
python tools/replay_native_ram_renderer.py E:/capture-export/replay.json `
  --output E:/replay-result --capture-clock-hz 1000000 `
  --profile reconstruction --preload-mib 2048 `
  --host artifacts/source-ticket-renderer/native-renderer-host.exe `
  --test-dll x64/Release/VideoProcessor-Test.dll `
  --plugin-root x64/Release
```

Use `--prepare-only` for validation without GPU/process execution. The host must
export regression cases 20/21 through the matching test DLL. Build that DLL in
x64 Release; the fixture is test-only. `--variant beta` selects the Beta plugin.
`--visual` requests two source-linked readbacks per segment and adds their cost.

Initial support is limited-range SDR REC709 native V210, up to 8192x4320,
4096 originals and 120 seconds per segment. Unsupported formats fail rather
than being silently converted. Each genuine gap/reset begins a cold renderer.
The wrapper revalidates the original capture index, retained files, all sixteen
recorded identity fields, omissions and segment boundaries before execution;
the host hashes the actual bytes it reads again.

## Timing and memory

By default, each source is read and hashed on the replay producer. Slow storage
can make this mode unsuitable for cadence measurements. Optional `--preload-mib`
allocates only the segment's exact native bytes before GPU creation and before
the source clock starts. Its explicit cap is at most 16384 MiB; native bytes plus
512 MiB must also fit within half of currently available physical memory. No
preload is enabled by default. Frames move into source owners without a second
pixel copy and are released normally. Allocation failure is a failed run.

Source offers use a high-resolution waitable timer and original timestamp
spacing. `inputScheduleQualified` is false if any offer is at least one nominal
source period late. A failed producer schedule cannot establish renderer cadence.
The fixture remains instrumented, and GPU idle checks before each run cannot
exclude another process beginning work later. The wrapper refuses to run while
VideoProcessor is present or the sampled GPU is busy; it never stops other apps.

## Evidence and limitations

- `plan.json` records original files, clock/profile/config and cold segmentation.
- `producer.json` pins the host, test DLL and plugin dependencies before execution.
- Segment `inputs.jsonl` records all original identities, IO/hash costs, source
  offer times, ingress cost and lateness.
- `records.jsonl` records actual production measurement, source-work and submitted
  geometry/timing events. Fresh runtime transport/accepted IDs are explicitly
  mapped to the original physical-source counter and exact timestamp pair.
- `frames.jsonl` includes every original input, including those with no submitted
  output. No expected aspect or caption truth is fabricated.
- `summary.json` includes existing queue disposition counters before/after Stop,
  diagnostic/log loss, missing original indices, stop duration and preload facts.
- `progress.json` preserves completed segments if a later idle gate refuses work.
  `receipt.json` distinguishes integrity, coverage, input timing and completeness.

A successful process exit and some submitted frames are not uninterrupted
playback. A short segment can fail cold prefill. At EOF the normal lookahead tail
may remain unsubmitted; no synthetic future frames flush it. Queue counters can
identify lifecycle retirement separately from capacity/recovery removals.
A submitted event is not proof of physical screen visibility or pixel correctness.
Optional PNGs require independent visual/pixel review and exact-source joins.
CPU render brackets are wall time, not GPU hardware execution time. No release,
deployment, or live HDMI performance claim follows automatically from this tool.

Planner/validation tests (no GPU):

```powershell
python -m unittest discover -s tools -p test_native_ram_renderer.py -v
```
