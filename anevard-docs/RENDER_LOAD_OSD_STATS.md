# Render-load OSD stats

This change adds madVR-style GPU render headroom to the Ctrl+I OSD without
adding synchronous GPU work or altering the rendered picture.

## Design

- Use libplacebo's existing `pl_render_params.info_callback` and asynchronous
  per-pass timers. Sum the latest resolved passes once per successful submit.
- Mark a frame boundary before each `pl_render_image` call so a failed render
  cannot leak pending pass values into the next submitted sample.
- Do not create a second D3D11 timer layer, patch libplacebo, flush, poll, or
  wait for same-frame query completion.
- Keep a time-based ten-second window and a session maximum. Warm-up and live
  shader/profile transitions settle before either statistic accepts samples.
- Pair every load percentage with the monitor-qualified refresh period recorded
  for that sample. The host supplies that rate through the renderer interface.
- Cross-check DXGI presentation cadence against the expected target and fail
  closed when frame statistics appear to come from another output.

The OSD is telemetry-only. Rendering parameters, textures, overlays, frame
ordering, queue policy, and presentation behavior are unchanged.

## Scope of the number

The result is a recent asynchronous estimate of libplacebo render-pass GPU
execution. It is useful for comparing shader/profile cost and judging display
budget headroom. It is intentionally not described as exact same-frame timing
or total GPU submission cost. See `RENDER_LOAD_INTERPRETATION.md` for operator
guidance and log-field semantics.

The renderer plugin API is version 17 because the host-to-renderer display-rate
setter changes the shared C++ vtable. Host and plugin must be built and deployed
together; a stale plugin is rejected by the existing API-version check.
