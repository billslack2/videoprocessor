# VP Active Output Probe

`VideoProcessorOutputProbe.exe` is an explicitly requested hardware test for
VP's D3D11/DXGI output contracts. It is not part of normal unit-test runs.

Run it only when it is acceptable for a short borderless fullscreen window to
cover the selected display:

```text
VideoProcessorOutputProbe.exe --active --monitor 1 --hold-ms 700
```

It tests each supported VP transport family by creating the real D3D11/DXGI
swapchain, checking and setting its color space, clearing a visible pattern,
and presenting it. Results are `PASS`, `UNSUPPORTED`, or `FAIL`.

The test proves API acceptance and a successful present. It cannot measure a
projector's physical output range or transfer curve; visual/meter validation
remains required for that claim.
