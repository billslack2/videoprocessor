/*
 * VideoProcessor nonlinear stretch for Alpha/libplacebo
 *
 * mpv user-shader format, parsed by libplacebo. VideoProcessor owns the
 * active-picture policy, trusted crop, target aspect, safe-fit decision, and
 * transition timing. This hook only performs the selected nonlinear mapping.
 *
 * The mpv shader format and the availability of horizontal/vertical mapping
 * were evaluated against NLS-Next. This implementation uses VideoProcessor's
 * established monotonic mapping and does not require NLS-Next's Lua helper.
 *
 * Copyright (C) 2026 Bill Slack
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

//!PARAM stretch_ratio
//!DESC Active-picture stretch ratio supplied by VideoProcessor
//!TYPE DYNAMIC float
//!MINIMUM 1.0
//!MAXIMUM 1.5
1.0

//!PARAM warp_axis
//!DESC 0 for horizontal mapping, 1 for vertical mapping
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.0

//!HOOK MAIN
//!BIND HOOKED
//!DESC VideoProcessor Nonlinear Stretch

vec4 hook()
{
    float strength = clamp(float({{strength}}), 0.0, 1.0);
    // A classic power curve needs an exponent of at least one to preserve a
    // monotonic source-coordinate map. The upper bound also keeps its edge
    // slope positive at the largest supported stretch ratio.
    float curve = clamp(float({{curve}}), 1.0, 2.9);
    float active_stretch_ratio = clamp(stretch_ratio, 1.0, 1.5);
    bool vertical_warp = warp_axis >= 0.5;
    int geometry = int(clamp(float({{geometry}}), 0.0, 1.0));
    float center_protection =
        clamp(float({{center_protection}}), 0.0, 0.45);

    vec2 sample_pos = HOOKED_pos;
    float centered =
        (vertical_warp ? sample_pos.y : sample_pos.x) * 2.0 - 1.0;
    float radius = abs(centered);
    float effective_ratio = mix(1.0, active_stretch_ratio, strength);
    float mapped_radius;

    if (geometry == 0 || center_protection <= 0.0) {
        // Preserve the centre after the presentation layer's required linear
        // expansion, then return smoothly to the untouched source edge. The
        // previous r * localScale formulation had a 0.356 edge slope for the
        // default 16:9-to-scope ratio and curve=2, making the sides appear
        // crushed. This normalized power curve gives 0.678 instead: plainly
        // nonlinear while retaining believable edge geometry.
        mapped_radius = effective_ratio * radius -
            (effective_ratio - 1.0) * pow(radius, curve);
    } else if (radius <= center_protection) {
        mapped_radius = radius * effective_ratio;
    } else {
        float span = 1.0 - center_protection;
        float t = clamp(
            (radius - center_protection) / span, 0.0, 1.0);
        float t2 = t * t;
        float t3 = t2 * t;
        float t4 = t3 * t;
        float t5 = t4 * t;
        float h00 = 1.0 - 10.0 * t3 + 15.0 * t4 - 6.0 * t5;
        float h10 = t - 6.0 * t3 + 8.0 * t4 - 3.0 * t5;
        float h01 = 10.0 * t3 - 15.0 * t4 + 6.0 * t5;
        float h11 = -4.0 * t3 + 7.0 * t4 - 3.0 * t5;
        float start_position = center_protection * effective_ratio;
        float start_slope = effective_ratio;
        float edge_slope = max(
            0.20, 1.0 - 0.5 * (effective_ratio - 1.0) * curve);
        mapped_radius =
            h00 * start_position + h10 * span * start_slope +
            h01 + h11 * span * edge_slope;
    }

    float mapped =
        (centered < 0.0 ? -mapped_radius : mapped_radius) * 0.5 + 0.5;
    if (vertical_warp)
        sample_pos.y = mapped;
    else
        sample_pos.x = mapped;
    return HOOKED_tex(clamp(sample_pos, vec2(0.0), vec2(1.0)));
}
