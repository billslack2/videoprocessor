# VP-0087: VP-managed subtitle fit with madVR presentation

## Status

Blocked (2026-08-04). VP-managed whole-picture subtitle-fit repositioning for
madVR cannot proceed until we identify a supported, no-resample presentation
placement control.

This is not blocked by detection. The current shared analysis path is
format-neutral and supports native RGB and planar sources. The exact blocker is
the absence of a known documented madVR presentation-placement control that can
move the already-rendered picture vertically while retaining madVR's chosen
scaling, tone mapping, and presentation behavior.

VP's madVR shader support is not that control. It installs external D3D9 HLSL
effects at madVR's pre-scale or post-scale stages. That is appropriate for NLS,
which is deliberately a shader effect, but it is not an acceptable substitute
for display placement. A shader-based vertical remap would risk resampling,
cropping, black-fill artifacts, transition flashes, or interaction with madVR
scaling/tone-mapping decisions merely to gain a subtitle offset.

Until resolved, Alpha owns VP-managed `subtitle_fit` because it controls the
complete libplacebo viewport and presentation geometry. When madVR is
selected, use madVR's native subtitle/viewport handling instead.

This story resumes only if a supported madVR API or contract is identified that
supplies a runtime presentation/viewport offset with no pixel remap or renderer
restart, and a bounded prototype can prove correct SDR/HDR, fullscreen,
refresh-change, NLS, and subtitle-transition behavior.

## Context retained for a future revisit

- `AnalysisLumaSource` and `ExtractActivePictureEvidence` make active-picture
  and bar-content analysis independent of P010. Native RGB support is covered
  by VP-0075; P010 conversion must not be introduced solely for this feature.
- `MadVRShaderLoader` already consumes VP active-picture state to select NLS
  shader rules and effective output aspect ratios. This validates the
  detection-to-policy control path, but does not expose final-image placement.
- `IMadVRExternalPixelShaders` exposes `PRE_SCALE` and `POST_SCALE` shader
  stages only. It does not provide a documented dynamic destination rectangle
  or vertical image-offset API.
- The existing Alpha `subtitle_fit` shifts the whole picture only after its
  renderer-owned viewport has been calculated. It is not OCR or per-glyph
  relocation and must remain renderer-local.

## Boundaries while blocked

- Do not add a generated translation shader to madVR.
- Do not alter captured frame pixels or force P010 solely to implement a
  madVR subtitle offset.
- Do not change madVR profiles, default configuration, or existing Alpha
  subtitle-fit behavior.

## References

- `src\\VideoProcessor-Lib\\AnalysisLumaSource.*`
- `src\\VideoProcessor-Lib\\P010ActivePictureEvidence.*`
- `src\\VideoProcessor-Lib\\microsoft_directshow\\MadVRShaderLoader.*`
- `src\\VideoProcessor-Lib\\microsoft_directshow\\MadVRExternalPixelShaders.h`
- `src\\VideoProcessor-Lib\\vprenderer\\LibplaceboVideoRenderer.cpp`
- VP-0038, VP-0075
