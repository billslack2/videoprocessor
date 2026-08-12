# VP Renderer active output sweeps

These are fullscreen diagnostic runs against real capture content. They use a
generated configuration whose filename contains `active-output-sweep`; VP
refuses to run against a normal user configuration. Each case recreates the
renderer (and by default the capture graph), observes whether a live frame is
presented, then restores the generated configuration.

The top-right test card is separate from the normal VP statistics overlay. It
stays inside the active picture area, uses green `PASS` only after a live frame
is observed, and uses red `FAIL` after a 15-second no-frame timeout. This is a
presentation/stability result, not proof of physical levels, transfer, or
calibration; use the displayed full description together with visual or meter
results.

## Shared controls

`/active_output_sweep` starts a sweep. `/active_output_sweep_suite sdr|hdr`
selects the suite; SDR is the default. `/active_output_sweep_hold_ms` defaults
to 10000 and accepts 1000 through 600000 milliseconds. Use
`/active_output_sweep_tests 2,5` or `2-5,8` for a subset. The default runs the
whole selected suite. `/active_output_sweep_restart capture|renderer` defaults
to `capture`; `renderer` keeps capture live but still recreates the renderer,
D3D11 device, and swapchain.

## SDR output-transport suite

Run this while SDR material is live. It has 17 cases covering the shipping
legacy path, automatic and pure-gamma negotiations, the VP-owned DXGI
experiment, limited-range Gamma 2.2, 8-bit swapchain, compute/cache toggles,
and composed presentation. Its purpose is transport and presentation
diagnosis; HDR tone-map settings are not varied.

The key projector comparison is SDR case 6: VP-owned DXGI, Direct, Limited
range, and pure Gamma 2.2. Cases 5 and 6 show whether the stock policy or the
VP-owned presenter is what changes the outcome.

## HDR tone-mapping suite

Run this only with real HDR content. It refuses to start unless VP currently
reports a valid PQ, HLG, or HDR input EOTF. The launcher asks whether this run
targets Rec.709 or BT.2020 and whether it should send the BT.2020 HDMI
InfoFrame; that selected color/signaling path is held constant across every
case. The suite never alters incoming HDR metadata.

| Test | Change from the HDR baseline | Purpose |
| --- | --- | --- |
| 1 | 100 nits | Conventional low target reference. |
| 2 | 200 nits | Reported safe-boundary comparison. |
| 3 | 250 nits | First just-above-boundary test. |
| 4 | 300 nits | Direct fullscreen anchor for control comparisons. |
| 5 | 400 nits | Higher target stress comparison. |
| 6–21 | 300 nits + every relevant SDR transport/presentation contract | HDR black-floor and output-policy comparisons: legacy/VP-owned, full/limited/auto range, transfer choices, compute/cache, and composed paths. The SDR-only force-8-bit swapchain case is excluded. |
| 22 | 300 nits + BT.2390 | Alternate highlight roll-off. |
| 23 | 300 nits + Reinhard | Alternate compression baseline. |
| 24 | 300 nits + softclip gamut mapping | Tests boundary color compression. |
| 25 | 300 nits + peak detection off | Identifies dynamic peak-analysis influence. |
| 26 | 300 nits + contrast recovery 0.0 | Identifies local-contrast recovery influence. |

Tests 1 through 5 isolate the reported target-nits threshold. Tests 6 through
21 repeat the relevant output/black-floor contracts with HDR input. Tests 22
through 26 change a single tone-map control at the 300-nit anchor. If a case is visually
wrong but passes, VP did present a live frame: record the visual/meter result
and preserve the test number, OSD description, and renderer log.
