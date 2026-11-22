# Private S2154/S2173 subtitle projection replay

This opt-in source replay requires an existing private native P010 capture. It does not contain or save video pixels. The capture must be 3840x2160, 7680-byte luma/chroma stride and match the saved S2154 or S2173 footer; the tool checks a known footer pixel and uses each capture's recorded source identity, range and Rec.709 interpretation. Point it at the original unmodified `.bin`, and do not commit that file.

The replay enables the active AR experiment, computes current adaptive picture evidence, calls `SubtitleMeasurementSampler` with the matching source identity, builds production cut-and-paste geometry, and evaluates both unseeded and exact-current-seeded retention, qualified erasure, projection and final fill. It simulates a successful source preparation: **it does not run the compositor or prove submitted/scanout output**. A positive readiness result must additionally be verified through the real offscreen compositor and renderer.

Variants:

- `raised`: exact captured frame, including the player UI. The projection may not qualify while that unowned current UI remains outside the erasure.
- `raised-clear-ui`: synthetic control. Preserve the exact picture and footer rectangle x=1100..2749, y=1980..2079; set other outer-bar luma to the measured 119-code pedestal. Chroma is untouched. This does not establish that every unmodified pixel inside the preserved rectangle is footer; it is a conservative fixture, not recorded footage without UI.
- `raised-clear-ui-with-mark`: the preceding fixture plus a synthetic luma 700 mark at x=25..32, y=1980..1987. A qualified projection must not conceal that independent mark, and the selected conservative evidence bounds must contain it. Because footer residuals can also force fallback, a pass alone does not prove the mark was independently detected.

- `raised-clear-ui-with-near-mark`: the clear-UI fixture plus a bright independent mark at x=2440..2447, y=2048..2049, inside the prospective expanded line rectangle. This case must remain unqualified; baseline failure alone does not establish independent mark rejection.

Usage from a matching x64 Release developer shell after an official Release build, selecting its actual library path:

```powershell
$repo = "E:\codex\videoprocessor\yttv-matte-1121"
$library = "<path-to-matching-release-VideoProcessor-Lib.lib>"
Push-Location $env:TEMP
cl /nologo /O2 /MD /EHsc /std:c++17 /DNOMINMAX /D_AFXDLL /D_UNICODE /DUNICODE `
  /I "$repo/src/VideoProcessor-Lib" /I "$repo/3rdparty/microsoft_directshow_baseclasses" `
  /I "$repo/3rdparty/blackmagic_decklink" "$repo/tools/vp_s2154_projection_replay.cpp" `
  "$library" /Fe:vp-s2154-projection.exe /link /LTCG
& ./vp-s2154-projection.exe <private-frame.bin> raised 2154
& ./vp-s2154-projection.exe <private-frame.bin> raised-clear-ui-with-mark 2154
& ./vp-s2154-projection.exe <private-frame.bin> raised-clear-ui 2154 --require-scope
Pop-Location
```

Use `2173` for its separately saved input. The first two runs check conservative behavior. `--require-scope` is a separate desired-behavior acceptance gate: it exits nonzero until the clear-UI case qualifies for scope and the actual glyph footprint includes the currently observed final footer row. On the current candidate it is **red** (`unowned-picture-outside-erasure`), even though conservative projection correctly remains unqualified. The test does not convert that correct fallback into a green end-to-end result.

## Synthetic paired controls

`vp_s2154_synthetic_controls.py` requires numpy and the original matching capture.
It writes three **private** frames outside the repository: a flat neutral matte
with a clipped glyph-shaped patch, and the same synthetic image with a detached
8x8 Y=126 or Y=700 mark. The patch intentionally ends at the current prepared
rectangle, and therefore omits the captured footer's real lower fringe. It is
not evidence that the recorded frame is safe or that actual GPU preparation
preserves the glyphs. The test compares projection authority against exact
synthetic inputs; the positive control must qualify and the marks must veto it.

```powershell
python tools/vp_s2154_synthetic_controls.py <private-capture.bin> <private-output-directory> 2154
& <replay.exe> <private-output-directory>/s2154-synthetic-clean.bin synthetic-clean 2154
& <replay.exe> <private-output-directory>/s2154-synthetic-weakmark.bin synthetic-weakmark 2154
& <replay.exe> <private-output-directory>/s2154-synthetic-brightmark.bin synthetic-brightmark 2154
```

Use 2173 and its corresponding files for the second capture. These replay cases
simulate preparation; they do not execute or validate the compositor.
