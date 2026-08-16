# VP-0133 rendered-output capture

This diagnostic captures VP Renderer's own final render target. It is taken
after libplacebo finishes rendering and immediately before that backbuffer is
submitted to DXGI. It is therefore more useful than a desktop screenshot for
comparing VP's generated pixel values with another renderer.

## Capture a comparison

1. Use a static SDR near-black or grayscale pattern and confirm the VP OSD
   reports SDR input. HDR/PQ input invokes tone mapping and is a separate test.
2. Let playback and the output mode settle.
3. Press `Ctrl+Alt+S`. The shortcut is editable on the Shortcuts page as
   **Capture rendered output**; an explicitly empty value disables it.
4. Wait for `Rendered-output capture complete` in the VP log.
5. Collect the matching `.png` and `.json` files from the `screenshots` folder
   beside `VideoProcessor.exe`.

The PNG is 16-bit RGBA. For an R10 swapchain, each original 10-bit channel code
`c` is stored as `(c << 6) | (c >> 4)`. The exact original code is recovered by
shifting the 16-bit PNG sample right by six. No display ICC transform, desktop
capture, or JPEG encoding is applied.

The sidecar's `raw_r10_sha256` hashes the tightly packed authoritative R10
pixels before PNG expansion. It is the preferred identity check when two
captures are expected to contain exactly the same output codes.

## What to send with a report

- The VP PNG and its JSON sidecar.
- The full VP log from the same run. Enable **Capture detailed output
  diagnostics** for the most useful DXGI and near-black records.
- The comparison renderer's lossless screenshot, if it provides one.
- The exact test pattern, VP renderer profile, projector range/gamma setting,
  and whether Windows HDR/Advanced Color was enabled.

The JSON records source levels/transfer/primaries, renderer ingress, requested
output values, accepted DXGI declaration, pixel-transfer override, presenter
owner, target/black nits, tone/gamut mapping, and code-value statistics.

## Interpretation limits

Matching VP and madVR files strongly suggests both renderers generated matching
pixels. It does not prove that the GPU driver, HDMI link, projector range, or
projector gamma interpreted those pixels identically. A fixed-exposure camera
or calibration meter is still required for the physical display chain.

Conversely, different screenshots localize the issue before the display. Pay
particular attention to source levels, actual output range, and the near-black
code buckets in the JSON/log.

VP currently has exact output-policy mappings for Auto/sRGB, pure 2.2 under the
explicit guarded experiments, and Limited/pure 2.4. Values such as 1.8, 2.0,
2.6, and 2.8 have no matching implemented DXGI output contract. The log now
states when one of those selections is unsupported and which fallback is used;
do not treat changing such a value as proof that the rendered transfer changed.
