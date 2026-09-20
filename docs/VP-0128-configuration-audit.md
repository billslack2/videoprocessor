# VP-0128 configuration audit — 2026-09-20

## Verdict and baseline

**No: defaults and initial states are not yet consistently represented and applied.** Most native mappings work, but inherited UI state, Auto black resolution, DirectShow omission behavior and unknown-token preservation have defects. This is an audit, not a behavior change or deployment.

Baseline: `billslack2/videoprocessor`, current GitHub default/latest beta `v1.3.005-beta`, commit `b3c0b3a6a8fdf4f5bb1c9ce0dc3340a3d5e395d7`. Branch `codex/vp-0128-config-audit-20260920`. Library: libplacebo 7.360.1/API 360, VP fork source `c646b398`. Recomputed hashes:

- DLL: `2BEFD13B92CCC8C034CD2EBEE6E3BDDC7F8FC1323B135AE005F11506C086C572`
- Source ZIP: `8650EA2606B987EB03296F277161B174867F047FE58E0CF69045B5BE3C7598BF`

Scope: Rendering, Scaling, Color / Output, Output Experiments, Screen, Zoom and profile overrides; broader startup, input, queue, LLDV, logging, shortcuts/actions and shaders. Physical HDMI behavior, every capture device and universal performance are not established by source/projection/UI tests.

## Findings

Evidence: **probe** = executed against this checkout; **source trace** = identified application path without live-capture reproduction. C1–C6 refer to the story's mismatch classes; startup/inheritance defects fall under C3 (not applied/displayed as labelled) and C5 (documentation/status).

### F1 — P1: true Boolean aliases initialize profile checkboxes as false

C3; probe. `ConfigEditorWindow.cpp:5483` compares only with `true`, while the shared parsers also accept `on`, `yes` and `1`. Tested output diagnostics, BT.2020 signalling, automatic crop and subtitle fit. All tokens parse successfully; the three aliases display unchecked. This can also mis-gate dependent controls such as LUT settings. The confirmed defect is the initial display; opening alone is not evidence that the Boolean was overwritten. General/logging use a different helper and are not implicated.

Fix: use the shared Boolean parser, including inherited aliases, then refresh dependent controls. Test untouched-save preservation.

### F2 — P2: inherited Fast/Balanced is previewed as High

C3/C5; probe. `refreshRendererAutoStatus` (`ConfigEditorWindow.cpp:2012`) reads raw `currentData()`, not `effectiveValue`. An inherited quality has empty item data. A child showing **Inherited: Fast** consequently reports HQ peak detection, contrast 0.30, EWA Lanczos sharp, Hermite, debanding and dithering, although Fast disables these preset features. Balanced inheritance also reports High.

The probe performs the normal post-construction status refresh; initial High labels before this refresh in test mode are not counted as a production defect. Scaling status also follows the Rendering profile selected in the editor rather than a composite active-profile resolver.

Fix: share effective-value resolution and distinguish edited versus active profile dependencies.

### F3 — P2: inherited Limited transport is described as unused

C5; probe. At `ConfigEditorWindow.cpp:2128`, range lookup again uses raw combo data. A child inheriting Limited and transport Auto says **Auto: Not used**, even though effective range is Limited and Auto resolves to 2.4. `refreshLimitedTransportControls` already has broader effective resolution; status and compatibility notices should reuse it.

### F4 — P1: unknown downscalers are silently deleted

C3; probe. `ConfigEditorWindow.cpp:5366` removes every downscaler outside its supported list. `downscaler: typo_filter` fails the real parser, but Config presents Auto, enables Apply, and saving deletes the token. Only the two intentionally retired spellings `none` and `ewa_lanczos` should receive their explicit compatibility migration.

Fix: whitelist those two migrations; preserve and visibly reject arbitrary unknown values (acceptance criterion 8). Related source finding: Zoom cleanup also removes malformed `fixed_crop_aspect`, although that field is strict in the parser; only the narrower/wider optional limits have a deliberately forgiving contract.

### F5 — P1: omitted DirectShow frame offset is 90 ms, not Auto

C3/C5; source trace. `VideoProcessorDlg.h:676–678` initializes Auto false and offset 90. `VideoProcessorDlg.cpp:13278–13285` installs those values. `VideoProcessorApp.cpp:1506` enables Auto only for an explicit Auto argument; config argument generation does not synthesize it when absent. Config (`:6033–6035`) and the reference describe omission as Auto. The sample explicitly writes Auto and masks the inconsistency.

Fix: deliberately choose and share the omitted default across startup, live apply, Config and documentation. Test missing file, omitted key, Auto, 0 and 90. No capture session was restarted to measure this effect.

### F6 — P1: named Base profiles collide with the synthetic root

C3; source trace. The parser accepts `[vprenderer.Base]` and uses normalized `base` as its default selection. `ApplyAutomaticProfiles` (`LibplaceboVideoRenderer.cpp:1318–1333`) skips named-baseline application when the name is `base`, then maps it to literal `[vprenderer]`. With only `[vprenderer.Base] quality: fast`, the model/editor can say Fast while that renderer path reads a nonexistent root and retains High. The same sentinel is used for renderer child groups; a literal root plus named Base can also collide in model identity.

Fix: carry source section identity explicitly; cover literal root, ordinary named baseline, named Base and persisted selections. Do not silently rename user profiles. This is an identified dispatch collision, not a measured rendering comparison.

### F7 — P2: Auto black is evaluated before final inherited white

C3/C5; source trace. The renderer calculates initial black at `LibplaceboVideoRenderer.cpp:2147`. `ApplyDisplayRuleOverrides:1759–1780` changes white but recalculates black only when that section explicitly supplies Auto. `NormalizeSdrBlackLevel:1675` repairs only invalid pairs.

Example: default white 100 / black Auto, child white 200 / omitted black. UI says Auto 0.200; the application path retains valid black 0.100. A named profile specifying only white 100 can retain the initial 0.203 fallback. Explicit zero must continue inheriting as zero.

Fix: preserve an Auto/explicit discriminator until all layers are merged, then resolve once. Test inherited Auto, absent black, explicit zero, measured black and white-only changes.

### F8 — P2: diagnostic preset token does not select a runtime preset

C3/C5; source trace. The parser accepts `output_path_profile: legacy/proposed/custom`; no renderer code consumes it. The UI expands a user selection into Boolean flags (`ConfigEditorWindow.cpp:4666`). Loading a handwritten proposed token does not expand it, so Output investigation can coexist with diagnostics off. Normal does not itself override explicit experiments. Current Normal/Investigation expansions differ only in `output_diagnostics`.

Fix: either derive/document this as an editor preset label, or implement a shared preset resolver with explicit-flag precedence. Keep experiments opt-in.

### F9 — P2: documentation and diagnostics contain obsolete contracts

C4/C5; source trace and native probe.

- VP-0128's prior Hermite/Lanczos gaps and LUT/error-diffusion exclusion are already fixed on this beta.
- CONFIGURATION.html Output Experiments still describes old page ownership, ties G22 to display gamma instead of transport gamma, and describes an older presenter stage.
- Public TSV inventory retains old Color/Scaling/Zoom owners and omits canonical fields including deband_strength, sigmoid, sdr_lut_input_gamma and some diagnostics. Most renderer Auto fields are marked as not requiring explanation.
- Startup summary at renderer `:5028` collapses peak detection to on/off and labels explicit diffusion kernels `auto/error-diffusion`. Auto ordinary dither remains `auto`. The next serialized-options log contains more detail, but reports frame_mixer=oversample even though VP calls `pl_render_image`, not `pl_render_image_mix` (`:12381`); this is not active interpolation.
- `NormalizeCalibrationChoice` (`RendererProfileConfig.h:948`) deliberately normalizes BT.1886 to pure 2.4 across calibration fields, SDR input Auto to 2.4 and SDR adjustment Auto/Off to passthrough. Reference/examples must describe this migration.

Fix: regenerate owner/value inventory and effective help; separate requested, inherited, resolved and actually active diagnostics. Keep frame mixing unexposed.

## Which Auto choices belong

| Family | Recommendation |
| --- | --- |
| Scalers, peak detection, contrast recovery, debanding, sigmoid, dithering | Keep policy; label **Use Rendering quality preset**. Auto is not algorithm discovery. |
| Tone/gamut mapping | Keep preset inheritance with clear wording. All current presets use Spline/Perceptual, but retaining preset ownership is meaningful. |
| Presentation | Keep **Prefer flip (allow fallback)**. Never promise DirectFlip or exclusive presentation. |
| Physical display gamma, gamut, white/black and panel precision | Explicit new-profile declarations; legacy automatic values remain readable with exact explanations. These are not measurable by this config resolver. |
| SDR input transfer / SDR adjustment | No new vague Auto. Current explicit reference/no-conversion UI is appropriate; legacy Auto is normalized. |
| RGB range / Limited transport transfer | Explicit new choices; legacy Auto means Full, or 2.4 when range is Limited. Not display-chain detection. |
| Dither depth | Explicit 8/10 for new profiles; legacy Auto follows surface precision, not panel/HDMI depth. |
| Black Auto | At most a legacy policy named **Assume 1000:1 contrast**, with F7 fixed. Not measured black. |
| DeckLink packing | Keep Auto for compatibility preference and capability fallback. |
| Conversion method | Keep Auto: actual CPU-feature/frame-size selection. |
| DirectShow frame offset, PPM, metadata overrides | Keep genuine timing/metadata automatic policies; fix omitted-offset mismatch. |
| LUT files/enabled, diagnostics, queue sizes, crop limits, actions | No generic third Auto state; explicit enablement, absence and inheritance already have distinct meanings. |

## Binary-verified preset matrix

Read through production `LibplaceboRenderParameters::Build` from the bundled DLL; exports are pl_render_fast_params, pl_render_default_params and pl_render_high_quality_params.

| Auto field | Fast | Balanced | High |
| --- | --- | --- | --- |
| Upscaler | Built-in/null | Lanczos | EWA Lanczos sharp |
| Downscaler | Built-in/null | Hermite | Hermite |
| Tone mapping | Spline | Spline | Spline |
| Gamut mapping | Perceptual | Perceptual | Perceptual |
| Peak detection | Off | Standard, percentile 100 | HQ, percentile 99.995 |
| Contrast recovery | 0 | 0 | 0.30 |
| Debanding | Off | Off | Standard |
| Sigmoid | Off | On | On |
| Dither | Off | Blue noise | Blue noise |
| Error diffusion | None | None | None |
| Frame-mixer pointer | Null | Oversample | Oversample |
| Frame mixing invoked by VP | No | No | No |

Off correctly clears both dither paths; ordinary methods clear diffusion; diffusion clears ordinary dither. Target-frame calibration LUTs no longer disable these final stages. Capability failures still require hardware testing. Preset-only internals (radius, precision, smoothing, etc.) need not become user controls.
## Renderer field and override matrix

R = Rendering (`vprenderer.<name>`), S = Scaling, C = Color / Output, V = Screen/viewport, Z = Zoom. Named profiles inherit their group's literal root or first named profile; omission inherits, explicit Auto replaces an inherited named algorithm with preset policy. Choices below follow UI order after the inheritance placeholder. Tokens are case-insensitive. Defaults mean runtime omission, except **new** means Config-created profile. Dynamic paths/names are not fixed enums.

Sources: `RendererProfileConfig.h:516–711` validates; `LibplaceboVideoRenderer.cpp:1690–2752` resolves; `LibplaceboRenderParameters.cpp` maps native parameters; `ConfigEditorWindow.cpp:4184–5620` builds/loads widgets. Matching key articles in CONFIGURATION.html are the reference, subject to F9's ownership and wording defects. Logs below mean startup settings, serialized native options, calibration/output contracts and profile records, not an individual log for every key.

| Field; current UI choices/tokens | Validation, runtime mapping and effective default | Audit result / log-reference parity |
| --- | --- | --- |
| R quality: High/Balanced/Fast → high/balanced/fast | Native preset exports above; omitted/new High; no Auto accepted | Named export tests pass; inherited UI F2. S parser also accepts quality although S UI has no quality selector. |
| S upscaler: Auto, EWA Lanczos 4 sharpest, EWA Lanczos sharp, EWA Lanczos, Lanczos, Catmull-Rom, Bicubic, Gaussian, Oversample, Bilinear, Nearest, Built-in GPU sampling | auto/ewa_lanczos4sharpest/ewa_lanczossharp/ewa_lanczos/lanczos/catmull_rom/bicubic/gaussian/oversample/bilinear/nearest/none; matching pl_filter_* exports, none null | Preset matrix; original Lanczos gap fixed. UI/parser/native mapping tests pass; effective filter logged. |
| S downscaler: Auto, Lanczos, Mitchell, Catmull-Rom, Bicubic, Gaussian, Hermite, Bilinear, Box, Built-in GPU sampling | auto/lanczos/mitchell/catmull_rom/bicubic/gaussian/hermite/bilinear/box/gpu; matching exports, gpu null | Preset matrix; Hermite gap fixed. Retired none/ewa_lanczos accepted only for Auto compatibility. Unknown cleanup F4. |
| S sigmoid: Anti-ringing, Auto/On/Off | auto/on/off; native sigmoid defaults or null; Auto preset | Mechanism is sigmoidization, not a general antiringing-strength control. Tooltip should say Rendering quality. Missing inventory entry. C4/C5. |
| R tone_mapping: Auto/Spline/BT.2390/ST 2094-40/Reinhard | auto/spline/bt2390/st2094-40/reinhard; matching pl_tone_map_* | All Auto Spline. Mapping and effective logs agree. |
| R gamut_mapping: Auto/Perceptual/Soft clip/Relative/Desaturate | auto/perceptual/softclip/relative/desaturate; matching pl_gamut_map_* | All Auto Perceptual. Mapping and effective logs agree. |
| R peak_detection: Auto/High quality/Standard/Off | auto/high_quality/on/off; parser also default alias; default/HQ parameter exports or null | Preset Off/Standard/HQ. Summary on/off loses distinction; serialized percentile retained. F2/F9. |
| R contrast_recovery: Auto or 0–2 | Finite inclusive 0–2 or Auto; explicit color-map contrast_recovery | Auto 0/0/0.3. Has-value flag initialized false. F2. |
| R deband_strength: Auto/Standard/Light/Off | auto/default/light/off; native default parameters; Light halves threshold and grain | Auto Off/Off/Standard. Standard is explicit native strength, not preset inheritance. Missing canonical inventory entry. |
| R legacy deband (no duplicate new control) | auto/on/off; canonical strength takes precedence | Compatibility retained and translated by editor. |
| R dithering: Auto, Blue noise, Ordered LUT, Ordered fixed, White noise, ten diffusion kernels, Off | auto/blue_noise/ordered_lut/ordered_fixed/white_noise/error_diffusion_simple/error_diffusion_false_fs/error_diffusion_sierra_lite/error_diffusion_floyd_steinberg/error_diffusion_atkinson/error_diffusion_jarvis_judice_ninke/error_diffusion_stucki/error_diffusion_burkes/error_diffusion_sierra2/error_diffusion_sierra3/off; legacy on → blue noise | Auto Off/Blue/Blue. All mappings and coupled Off tested. LUT compatible. F9 summary wording. |
| R display_bit_depth: 10-bit/8-bit | 10/8; legacy auto accepted; clamps dither color-depth and bit-shift to surface storage | New 10; omitted Auto surface depth. No HDMI-depth configuration/detection. |
| R sdr_target_nits: Target nits | Finite >0.000001 through 10000, above black; HDR target max_luma | New 100; omitted 203. SDR reference stays fixed; target slider does not change SDR brightness. Pair-validation tests pass. |
| R sdr_black_nits: HDR tone-map target black | Auto or finite nonnegative below white; zero maps to PL_COLOR_HDR_BLACK (0.000001) | New 0; Auto assumes white/1000. F7. Numeric-only widget validator still loads retained Auto text; compatibility needs explicit presentation. |
| C target_primaries: Rec.709/P3-D65/BT.2020 | rec709/p3_d65/bt2020; render primaries and exact LUT slot | New/omitted Rec.709. Alias sdr_target_primaries readable; canonical wins same section, child alias beats inherited parent. No Auto. |
| C output_gamma: calibrated display gamma | Explicit sRGB/1.8/2.0/2.2/2.4/2.6/2.8; parser also auto/bt1886 | New 2.2; omitted Auto accepted transport; bt1886 normalized to 2.4. Inactive only with confirmed usable LUT. |
| C sdr_input_transfer: SDR reference | Same explicit transfer family; Auto/bt1886 normalize 2.4 | Omitted/new 2.4; assumed reference, not detected mastering gamma. |
| C sdr_adjust_gamma: no conversion / desired SDR gamma | on/passthrough; legacy auto/off normalize passthrough | Omitted/new passthrough. Without LUT controls compensation; usable LUT has independent encoding contract. |
| C sdr_lut_input_gamma: no conversion / transfer | passthrough/srgb/1.8/2.0/2.2/2.4/2.6/2.8; bt1886 accepted then normalized | Omitted/new passthrough; only SDR with usable LUT. Inventory missing. |
| C hdr_tone_map_target_gamma: explicit transfer | Explicit transfer family, no Auto; default resolves 2.2 | Only HDR with usable LUT. UI editing follows LUT enablement; actual use follows attachment evidence. |
| C calibration_lut_enabled: checkbox | Boolean aliases; usable attachment additionally requires matching loadable LUT | False; F1 applies. Live config-identity checks avoid stale gating. |
| C calibration_lut_bt709: LUT/None | none or .cube path; loader validates contents and constrained config-relative path | Empty/none new; omission inherits, none clears. Rec.709 slot only. |
| C calibration_lut_p3_d65 | Same | Independent P3-D65 slot. |
| C calibration_lut_bt2020 | Same | Independent BT.2020 slot. |
| Legacy calibration_lut_input_gamma | display or transfer; accepted but no longer controls encoding, logs diagnostic | Retain legacy only, not a second modern gamma. |
| Legacy calibration_lut_input_transfer | HDR-target alias; display → 2.2, canonical wins same section | Migration archives old Rendering calibration and preserves variants. Tests pass. |
| C output_presentation: Prefer flip (allow fallback)/Flip model/BitBlt model | auto/direct/composed; output-policy plan | Auto flip preference. Embedded preview forces composed, BT.2020 requires flip. Do not claim DirectFlip. |
| C output_range: Full/Limited | full/limited; legacy auto→Full | New Full; omission Auto. Limited requires capability/set acceptance. F3. |
| C output_transport_gamma: 2.2/2.4 | 2.2/2.4; legacy auto→2.4 when Limited | New 2.2 dormant while Full; edits derive G22 gate. Legacy false gate preserved on load. G22 not exact pure-2.2 declaration. |
| C report_bt2020_to_display: checkbox | Boolean signalling request independent of render gamut | False; F1 aliases; physical output not measured. |
| C output_path_profile: Normal diagnostics/Output investigation/Custom | legacy/proposed/custom; editor expands flags, runtime ignores token | Label defaults legacy. F8. |
| C diagnostic_allow_limited_g22: hidden derived checkbox | Boolean runtime gate for Limited+2.2 | False omitted/new; edited combinations derive; existing saved false preserved. |
| C diagnostic_allow_full_g22: file-only | Boolean full-G22 experiment | False; intentionally not routine UI (C6); inventory needs coverage. |
| C diagnostic_disable_compute: checkbox | Boolean D3D11 device parameter | False; affects capabilities required by some processing; F1. |
| C diagnostic_force_8bit_sdr_swapchain: checkbox | Boolean 8-bit request rather than normal 10-bit preference | False; distinct from dither target depth; F1. |
| C diagnostic_vp_owned_dxgi_presenter: checkbox | Boolean alternate VP-owned flip path; composed uses libplacebo | False; architecture repair outside story; F1/F9. |
| C output_diagnostics: checkbox | Boolean telemetry/investigation | False; UI proposed sets true; F1/F8. |
| C diagnostic_disable_shader_cache: checkbox | Boolean cache policy | False; F1. |
| File-only profile_update_mode | rebuild/live/never enum | Rebuild omitted; legacy live_profile_updates Boolean only when canonical absent. No Auto. |
| Shared switch_refresh_rate | never/fullscreen_only/always; true=fullscreen_only, false=never; full_screen_only alias | Omitted fullscreen-only; General is shared owner, compatibility profile locations retained. |
| V screen_aspect | Ratio/decimal 1–4 | Omitted renderer uses output-panel aspect; existing empty field UI fallback 16:9 is an assumption, not detection. |
| V vertical_alignment | top/center/bottom | Center. Padding inactive at center; inherited dependency resolution should be shared. |
| V screen_edge_padding | Integer 0–100000 | 0 pixels. |
| V anamorphic_scale | Ratio 0.5–2 | Omitted 1/unset; explicit enablement in editor. |
| V legacy mode | normal/scope; deprecated/ignored warning | Aspect owns current behavior. |
| Z automatic_crop | Boolean, compatible viewport location accepted | False; F1 aliases. Not an Auto token. |
| Z crop_narrower_content_to_fill_screen | Boolean | False. |
| Z crop_narrower_content_aspect_limit | Optional ratio; malformed means no limit | Unset/no limit; intentional forgiving policy must be clear. |
| Z crop_wider_content_to_fill_screen | Boolean | False. |
| Z crop_wider_content_aspect_limit | Optional ratio; same policy | Unset/no limit. |
| Z fixed_crop_aspect | Strict ratio 1–4 | Unset; fixed center crop; cleanup issue related to F4. |
| Z subtitle_fit | Boolean | False; F1. |
| Z subtitle_hold_seconds | 0.25–30 | 2 sec / initialized 2000 ms. |
| Z subtitle_engage_drift_ms | Integer 0–30000 | 0. |
| Z subtitle_release_drift_ms | Integer 0–30000 | 0. |
| Z subtitle_padding_pixels | Integer 0–500 | 20. |
| Z subtitle_target_buffer_pixels | Integer 0–50 | Shared constant 10. |
| Z hdr_peak_analysis_picture_only | Boolean; analysis-mode UI | False; only relevant when peak detection runs. |
| Z hdr_peak_analysis_motion_compensation | Boolean; analysis-mode UI | False; motion protection. |
| Z hdr_peak_analysis_height_percent | Integer 10–100 | Shared 75; fixed analysis mode. |
| Z hdr_peak_analysis_position | top/center/bottom | Shared top. |
| Profile when/shortcut/cycle_shortcut | Valid expressions/chords; file order is automatic priority | Empty=no selector. First profile default, persisted selection fallback, source rules override unless session manual override. |
| V label | Human-readable string distinct from section identity | Identifier fallback. |
| Legacy priority | Integer -100000–100000; ignored with warning | File order wins. |
| Frame mixing | No accepted VP field/control; preset pointer only | Not invoked. Intentionally unexposed (C6). |

Historical schema ownership is broader than the UI: S accepts quality, deband_strength and dithering, while current UI puts them in R. The fixed application order (input, scaling, display, color, ...) means later Rendering values can win duplicate ownership. Tighten the compatibility contract rather than adding controls solely to expose every historical parser allowance.
## Broader configuration and startup matrix

| Family/fields | Default and initialization contract | Assessment |
| --- | --- | --- |
| General renderer/capture_device/capture_input | Sample VP Renderer + Quad HDMI channel 1; discovery-dependent | Sample device is not universal. Discovery-outage tests pass; no capture-device hardware qualification here. |
| fullscreen/windowed_fullscreen_mode/noui/startminimized; monitor/session mode | Visible/windowed sample; explicit existing/target-only monitor policy | No generic Auto needed. Reveal/startup tests pass; physical second-monitor coverage skipped. |
| scene_detect/disable_detection_features/scene_correction_mode/basic/subtitle_reposition | Scene detection defaults off; enablement and modes distinct | Covered editor initialization tests pass; don't imply disabled analysis is active. |
| hide_legacy_renderers/interface | Legacy renderers hidden by default; malformed interface nonfatally falls back Classic | Deliberate exceptions; retain warnings and unavailable configured renderer. |
| video_conversion/container_colorspace/hdr_colorspace/hdr_luminance | Backend override → General → legacy command_line; backend defaults independent | Existing inheritance/migration/live-apply tests pass. Auto/follow-input appropriate for metadata, not calibration. |
| DeckLink rgb_8bit_packing/rgb_10bit_packing/rgb_12bit_packing | Auto prefers ARGB/r210/R12B; explicit ARGB/BGRA, R210/R10B/R10L, R12B/R12L | Keep compatibility Auto; actual device support/fallback must be hardware-qualified. |
| Queue queue_size | Omitted 32, positive capacity | Schema/UI agree; construction bounds tested. |
| Queue lead_frames/target_frames | Omitted 1/4; editor agrees | Sample Normal explicitly 4/3, a policy override. |
| Queue startup_preroll_frames | Omitted automatic startup priming; explicit legacy 0–16 accepted | UI still exposes Startup pre-roll despite startup source comment calling it compatibility-only. Decide Advanced visibility; distinguish omitted automatic from explicit zero. C4/C5. |
| Queue active_picture_lookahead_frames | Omitted 0, range 0–8 | Independent bounded policy. |
| Queue reset_after_render_restart_seconds/reset_queue_too_large_percent | Omitted 5/75; positive seconds, 1–200 percent | Sample Normal comment mentions 70 but is not active. Existing constructor/policy tests pass. |
| DirectShow renderer_start_stop_time_method | CLOCK_SMART | Real modes, no artificial Auto needed. |
| DirectShow frame_offset | Omitted fixed 90; explicit Auto resolver | F5. Sample explicitly Auto. |
| DirectShow renderer_nominal_range/renderer_transfer_function/renderer_transfer_matrix/renderer_primaries | Unknown/Auto enums delegate source/renderer metadata | Keep genuine delegation. |
| Conversion conversion_method/min_core_count/max_core_count/chroma_downsampling | Auto/1/1/Average; loader resets fields, CPU-feature cache and pool state | Auto chooses SIMD for eligible larger AVX2 frames, optimized for smaller AVX2, scalar otherwise. Sample SIMD is explicit. Core/chroma controls file-only, preserved by UI. |
| PPM ppm | Auto automatic-timing sentinel; numeric ±1000000 accepted | Keep Auto. Numeric 999999 shares internal Auto sentinel: boundary regression needed before promising every numeric token is fixed correction. |
| LLDV max_cll/max_fall/mastering_min_luminance/mastering_max_luminance | Legacy 1000/1000/0.0001/1000; newlldv 1000/401/0.001/4000 | Shared defaults and has-value flags distinguish omitted from explicit zero. Mode-sensitive UI/tests agree. |
| Logging enabled/debug/debug_log_retention | true/false/10 total files, range 1–100; enhanced retains all | Invalid retention runtime fallback=10. UI converts/clamps raw invalid text and can display 1 or 100 instead: source follow-up. Schema's stale 1–200 comment is not runtime behavior. |
| Shortcuts, foreground_only, renderer_alias | Command defaults; explicit empty disables; focus guard; positive alias indices | Existing tests pass. Omitted and empty must stay distinct. |
| Shader group/member type/when/shortcut/shader_type/label/stage/order/hlsl_file/glsl_file and custom parameter maps | Explicit single/multi selection; standard shaders compose; selected profiles/paths | Schema and tests cover selection, stage/order and backend applicability. No blanket inheritance of standard shader settings. |
| NLS geometry/strength/center_protection/curve/quality/horizontal_center_protection/vertical_center_protection/axis_balance/max_center_zoom/tolerance_percent/max_stretch_ratio/aspect_direction/vprenderer_max_crop_percent/vprenderer_crop_preference | Explicit bounded values and selected profiles | Shader/profile suites pass. NLS quality is a separate contract from Rendering quality; don't attach its Auto semantics. |
| Actions enabled/renderer/on/when/run/delay_seconds/coalesce_role | Runnable event/command contract; default vprenderer and delay 5; disabled drafts retained | Event variable validation and profile tests pass. No external actions/messages launched by this audit. |
| persist_profile_selection/profile_change_display_seconds/bounded_invalid_capture_recovery | true/5 sec/true | Initial runtime generation published only after model+snapshot success. Persisted selection is fallback to matching source rules. Bounded recovery has 1500-ms grace; file-only. |

The broad rows are source/policy and existing-suite coverage, not a claim that every field has an independent new hardware test. Additional source findings (PPM sentinel, invalid retention display, fixed-crop cleanup, overlapping group ownership) should receive focused regressions in the correction pass.

## Default-policy judgment

Fresh profiles are a coherent explicit starting point: High with Auto processing, 100-nit HDR target, effectively zero black, 10-bit dither target, Rec.709, display gamma 2.2, SDR reference 2.4/no gamma conversion, LUT disabled, Full RGB/flip-preferred, experiments off. SDR luminance is intentionally independent of the HDR target sliders; native tests confirm zero-black handling.

They are not universally measured calibration values. High enables expensive HQ peak analysis, debanding and EWA upscaling; switching the generic default to Balanced requires target-hardware measurements. Zero black may not fit a projector. The dormant Limited 2.2 choice enters a beta transport path when selected; choosing a conservative generic Limited default is a product decision. This audit does not silently retune existing installations.

Document three starting points: fresh Config-created file, distributed sample, existing file with omitted keys. Fresh versus omitted white (100/203), black (0/assumed), display gamma (2.2/transport-following), depth (10/surface), plus explicit sample queue/conversion choices are materially different. Some differences preserve compatibility; F5/F7 are genuine contradictions.

## Validation and follow-through

- Isolated worktree from queried remote beta tip; no stale checkout used as baseline.
- x64 Release solution build passed: **0 errors, 26 warnings**, project-configured v142/v143 toolsets. Initial forced-v143 build lacked MFC; removing that inappropriate override succeeded. No dependencies installed/changed.
- Full Config editor executable: **75 passed**, exit 0. Offscreen execution; physical two-monitor case explicitly skipped with synthetic clamp coverage retained.
- Relevant native filter: **435 passed**, exit 0. Configuration, profile, libplacebo mapping/output/LUT/proxy, queue/startup and shader tests. Local TRX: artifacts/vp0128/config-native.trx.
- Extra disposable UI/parser probes reproduce F1/F2/F3/F4; native probe verifies three DLL presets. See adjacent VP-0128-evidence sources/output.
- No production source behavior changed, no deployment/merge/config replacement/capture restart, no physical output claim.

Correction order: shared effective-value/Boolean UI resolution; strict unknown-token preservation; source-section identity and resolve-after-inheritance defaults; shared startup default contracts; then preset/log/reference cleanup. Add tests at those boundaries; existing passing suites miss these counterexamples.

**Audit complete; story remains In Progress for corrections.** Acceptance items 2/4/7/8/9 are not comprehensively satisfied. Matrix coverage does not approve exposing every libplacebo capability.