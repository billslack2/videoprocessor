> Update: REQ-006 subsequently removed the 40–500-nit limit. The results below
> describe the earlier implementation; see REQ-006-sdr-transfer-implementation.md.

# VP-0173: SDR input preservation

## Contract and implementation

Per the user's clarified contract, no separate SDR luminance control is added.
SDR is not forced into literal passthrough: selected debanding and other
non-tone-mapping processing still apply. The unity test disables optional
processing only to isolate this change; a separate test enables debanding
and verifies its effect is retained independently of HDR destination nits.
The existing `sdr_target_nits` and `sdr_black_nits` keys remain compatible and
specify only the HDR-to-SDR tone-mapping destination. SDR source and target
metadata use 203 / 0.203 nits internally; these reference values do not set
physical display brightness. Source/target transfer, primaries, range,
explicit gamma adjustment, calibration LUT and scaling settings are retained.
Stale SDR HDR metadata is cleared, including scene and mastering metadata.
HDR source metadata is unchanged and its destination uses the configured nits.

Both previous swapchain-hint call sites now use one submission method and a
copy of transport-only metadata. SDR transfer hints cannot become HDR because
of configured luminance. The VP-owned presenter retains its existing control
of DXGI state. HDR-transfer hints are unchanged.

The editor labels the controls as HDR tone-map white/black, rejects invalid
values continuously, and retains the saved configuration. The existing white
range remains 40 through 500 nits; there is no new numeric limit in libplacebo.
Black remains Auto or a nonnegative value below destination white. Auto uses
white / 1000. The internal SDR reference ignores both destination settings.

`LUMINANCE_CONTRACT` diagnostics report source/target transfer, luminance and
effective-HDR decisions, configured HDR destination, negotiated texture format,
DXGI contract and peak eligibility when those inputs change.

## Baseline and dependency

- Source base: `origin/v1.3.005-beta`,
  `e5f80f89a564a7600957013d2efcdfa4d7764426` (queried and fetched 2026-09-08).
  Initial investigation used d663773c; the branch was rebased and final tests
  rerun against e5f80f89 and its updated dependency. Fetch used an explicit
  branch-to-tracking-ref refspec because the shared checkout tracks only a
  small branch subset by default.
- Bundled libplacebo: 7.360.1 plus VP analysis crop and D3D11 timer controls,
  fork `c646b39886d6172b853eafe82e6d6e5c2aeb5de0` (source archive manifest).
- DLL SHA-256: `2BEFD13B92CCC8C034CD2EBEE6E3BDDC7F8FC1323B135AE005F11506C086C572`.
- Upstream transport correction: [4dbc490b0770539942abb3cc61fdce5438d06331](https://github.com/haasn/libplacebo/commit/4dbc490b0770539942abb3cc61fdce5438d06331).
  The bundled corresponding source archive was inspected: it still uses `pl_color_space_is_hdr` in the three affected
  D3D11 decisions. VP-side containment is used; no DLL update or cherry-pick.
- Initial direct calls to the c3a3d203 DLL confirmed SDR classification is false at
  203 nits and true at 203.01, 400 and 500 nits before normalization.

## Validation

Validation completed on 2026-09-08 against implementation commit `b1d7eb2b`.
Subsequent edits to this record and the dependency README are documentation only.
GPU fixtures use the actual bundled libplacebo D3D11 backend with WARP and a
64x64 RGBA8 gradient, with optional debanding and dithering disabled for exact
readback. Scaling cases use 32x32, 64x64 and 96x96 destinations and the native
high-quality scaler/sigmoid/peak parameters. These are repeatable software-GPU
checks, not physical HDMI/projector measurements. No deployment is performed.

### Results

- Clean solution x64 Release rebuild: passed, 0 errors. Projects use their
  declared toolsets (v142 for VP/MFC, v143 for Qt). An incremental LTCG cache
  produced LNK1103 during development; the final clean build used full LTCG
  (`/p:LinkTimeCodeGeneration=UseLinkTimeCodeGeneration`).
- VSTest: **245 passed / 245**, including all LibplaceboRenderParameters,
  LibplaceboLutParser, LibplaceboOutputPolicy, ConfigFile and ConfigEditorCore
  tests. No failed or skipped tests in the selected run.
- Qt offscreen editor: **2 passed / 2**: `HDR target luminance validation
  retains saved value` and `every page round trips`. Explicit checks cover
  40/500 accepted, 39/501/600/NaN rejected, invalid black rejected, and the
  saved file retained. Controls retain existing configuration key names.
- SDR RGBA8 readback at 75, 203, 400 and 500 nits: byte-identical across each
  32x32 / 64x64 / 96x96 output size. At unity with optional processing off,
  output also matches every input byte. HDR peak metadata is absent.
- SDR debanding enabled: output differs from the unprocessed input, but is
  byte-identical across those four target settings; HDR peak metadata remains
  absent. Production debanding settings have not been disabled or changed.
- HDR PQ BT.2020-to-SDR at 400/500 nits: old and corrected render-policy
  readbacks are byte-identical. Unit coverage also verifies unchanged PQ/HLG
  source metadata, destination white/black, and HDR-transfer transport hints.
- Native D3D11 SDR hints at 40, 75, 203, 203.01, 400 and 500 nits: returned
  transfer is sRGB throughout. Normal output stays R10G10B10A2_UNORM
  (DXGI_FORMAT 24); forced 8-bit stays R8G8B8A8_UNORM (DXGI_FORMAT 28).
  The tests create hidden windows and submit frames through real swapchains.

### Gamut-mismatch measurements

Perceptual gamut mapping into Rec.709, same 64x64 gradient, 16,384 RGBA bytes:

| SDR source | Old target nits | Changed bytes, old versus corrected | Maximum 8-bit code delta |
| --- | ---: | ---: | ---: |
| BT.2020 | 75 | 2266 | 25 |
| BT.2020 | 203 | 0 | 0 |
| BT.2020 | 400 | 1775 | 20 |
| P3-D65 | 75 | 1946 | 24 |
| P3-D65 | 203 | 0 | 0 |
| P3-D65 | 400 | 1661 | 22 |

The corrected output is byte-identical across target settings within each
source gamut. It intentionally matches the existing 203-nit reference behavior;
gamut-mapped output previously calculated at 75 or 400 nits can change. This
removes the unintended dependency on an HDR-only control, while retaining gamut
mapping itself. These differences are exposed for review, not described as
legacy-output equivalence or literal SDR passthrough.

### Reproduction and limits

From the source worktree, using Visual Studio's MSBuild and VSTest:

```powershell
MSBuild.exe VideoProcessor.sln /t:Rebuild /m:4 /p:Configuration=Release /p:Platform=x64 /p:LinkTimeCodeGeneration=UseLinkTimeCodeGeneration
vstest.console.exe x64/Release/VideoProcessor-Test.dll /Platform:x64 /TestCaseFilter:"FullyQualifiedName~LibplaceboRenderParametersTests|FullyQualifiedName~LibplaceboLutParserTests|FullyQualifiedName~LibplaceboOutputPolicyTests|FullyQualifiedName~ConfigFileTests|FullyQualifiedName~ConfigEditorCoreTests"
$env:QT_QPA_PLATFORM='offscreen'
./x64/Release/VideoProcessorConfigTests.exe --test 'HDR target luminance validation retains saved value'
./x64/Release/VideoProcessorConfigTests.exe --test 'every page round trips'
```

Local execution artifacts: `tmp/vp0173-release.log`,
`tmp/TestResults/vp0173-final.trx`, `tmp/vp0173-tests.log`, and
`tmp/vp0173-editor.log`. The built host is `x64/Release/VideoProcessor-GUI.exe`
and its paired renderer is `x64/Release/vprenderer/VideoProcessorVPRenderer.dll`.
The staged libplacebo DLL hash matches the dependency hash above.

Physical GPU/HDMI signaling and projector observation remain operator validation;
WARP swapchain/readback tests do not claim those results. No active configuration
or deployed runtime was changed.
