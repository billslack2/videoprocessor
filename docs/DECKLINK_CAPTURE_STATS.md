# DeckLink capture delivery statistics

DeckLink supplies the same Capture fps, Capture missed and Render dropped OSD rows as Magewell, with either VP Renderer or DirectShow. The statistics apply to SDR, HDR and DV capture; they are not conditional on DV metadata.

Capture fps counts received valid-input video frames over completed five-second monotonic wall-clock windows. Expected fps comes from the detected input mode. Hardware timestamp gaps estimate skipped input frames independently of renderer queue drops. The existing VFrames/source sequence keeps its timestamp-interval meaning; it is not the delivered-fps numerator. Audio-only callbacks and no-signal frames do not contribute to delivery statistics.

Signal loss, format restart, stop and capture errors clear the observation window. Duplicate/backward hardware timestamps restart measurement without a negative or unsigned-overflow miss count. Session capture misses remain accumulated until capture restarts. The first frame anchors a window and does not itself establish an interval. A completed window expires if no callback arrives for more than five seconds. No RGB24 packing label is shown for DeckLink.

These counters measure capture delivery, not physical display drops or DV content correctness. Hardware timestamp gaps are estimates; they cannot detect corruption or repeated picture content with otherwise normal timestamps.
