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
reports a valid PQ, HLG, or HDR input EOTF. The dedicated HDR template defines
the repeatable fullscreen output baseline: Direct presentation, Limited range,
Gamma 2.2, BT.2020 target primaries, and BT.2020 signaling. The suite does not
alter incoming HDR metadata.

| Test | Change from the HDR baseline | Purpose |
| --- | --- | --- |
| 1 | 100 nits | Conventional low target reference. |
| 2 | 200 nits | Reported safe-boundary comparison. |
| 3 | 250 nits | First just-above-boundary test. |
| 4 | 300 nits | Direct fullscreen anchor for control comparisons. |
| 5 | 400 nits | Higher target stress comparison. |
| 6 | 300 nits + composed presentation | Distinguishes fullscreen direct presentation from compositor behavior. |
| 7 | 300 nits + Rec.709 target/signaling | Separates BT.2020 target/signaling from tone-map behavior. |
| 8 | 300 nits + BT.2390 | Alternate highlight roll-off. |
| 9 | 300 nits + Reinhard | Alternate compression baseline. |
| 10 | 300 nits + softclip gamut mapping | Tests boundary color compression. |
| 11 | 300 nits + peak detection off | Identifies dynamic peak-analysis influence. |
| 12 | 300 nits + contrast recovery 0.0 | Identifies local-contrast recovery influence. |

Tests 1 through 5 isolate the reported target-nits threshold. Tests 6 through
12 change one relevant control at the 300-nit anchor. If a case is visually
wrong but passes, VP did present a live frame: record the visual/meter result
and preserve the test number, OSD description, and renderer log.
