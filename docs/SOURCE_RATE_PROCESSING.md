# Source-rate processing and analysis

Smooth motion processes each retained input picture (or deinterlaced field) into
an output-sized RGBA16F cache before blending. Scaling, tone/gamut mapping,
source hooks and target calibration run in that preparation pass. Each subtitle
belongs to its source packet and is composed before processing; complete processed
pictures, including subtitle changes, are then blended at output cadence.

Source processing owns a separate renderer so output-only passes cannot reset
its HDR peak-detection history. Both bundled runtimes blend in an explicitly
linear float surface before the final encoding pass.

The output pass retains live OSD/overlays, range conversion and output-depth
dithering. It does not repeat source peak detection, tone mapping, NLS or LUTs.
Cache reuse requires matching source/field/neighbors, geometry, target color and
representation, calibration, hooks, HDR analysis crop and render-settings epoch.
Changes reprocess retained pictures; incompatible source geometry remains excluded
from mixing. Float caches are retained only within the active mixer support.

Expensive subtitle measurement follows the immutable input sequence, independent
of output Hz: every frame through 30 fps; precisely odd input sequences above
30 fps. Thus 60 fps gets 30 full analyses/sec, 120 fps gets 60, and 23.976/24 fps
gets one full analysis per source frame. No decoded video frames are discarded.
Repeated outputs may read a completed result but cannot submit additional scans.
Skipped analyses remain explicitly unknown. They never count as absent text,
positive ownership, or additional confirmation. Lookahead may compare consecutive
independent stride-two observations across explicitly skipped frames, retaining
all source/context checks; a missing scheduled result still ends readiness.
Current-pixel continuation checks and cheap/native AR checks remain per input.
Clean AR cuts and gradual hold-then-snap policy are not changed.

The existing GPU utilization metric still measures total GPU output workload.
The OSD labels it GPU output load. The source/output intervals remain separate.
Runtime VP source/output processing logs report source render/reuse and output
counts, plus input analysis stride, every two seconds.

Validation must exercise processing reuse at 24-to-75 Hz, HDR-to-SDR, output
color/geometry/settings changes, live OSD overlays, native fields and subtitle
transitions. Counters distinguish input preparation from output presentations;
raw scan speedups alone do not establish full playback deadline compliance.

The linear mixer target must support blit destination operations: libplacebo
clears its border when the output crop letterboxes the picture. A renderable-only
texture can still return successful rendering while logging a failed clear and
expensive backtraces on every output. Run `tools/test_temporal_letterbox.ps1` to
exercise both bundled APIs with error logging enabled, three mixers,
subtitles composition enabled/disabled, repeated outputs and black border readback.
