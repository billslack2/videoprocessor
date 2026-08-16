/*
 * VideoProcessor balanced nonlinear stretch for VP Renderer/libplacebo.
 *
 * This is an original two-axis extension of VideoProcessor's monotonic NLS
 * map. VideoProcessor owns target selection, source geometry, optional
 * presentation crop, safe-fit policy, and transition timing. The hook receives
 * only the bounded runtime ratio/direction and performs the selected mapping.
 *
 * Copyright (C) 2026 Bill Slack
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

//!PARAM stretch_ratio
//!DESC Absolute active-picture correction ratio supplied by VideoProcessor
//!TYPE DYNAMIC float
//!MINIMUM 1.0
//!MAXIMUM 1.5
1.0

//!PARAM warp_axis
//!DESC 0 when source is narrower than target, 1 when source is wider
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.0

//!HOOK MAIN
//!BIND HOOKED
//!DESC VideoProcessor NLS+ Balanced Stretch

float nls_plus_map_radius(float radius, float center_scale,
                          float curve, int geometry,
                          float center_protection)
{
    if (geometry == 0 || center_protection <= 0.0) {
        // This normalized power curve fixes both edges at one. It is monotonic
        // for the complete supported centre-slope range, including the
        // below-one slope used by the complementary axis.
        return center_scale * radius -
            (center_scale - 1.0) * pow(radius, curve);
    }

    if (radius <= center_protection)
        return radius * center_scale;

    float span = 1.0 - center_protection;
    float t = clamp((radius - center_protection) / span, 0.0, 1.0);
    float t2 = t * t;
    float t3 = t2 * t;
    float t4 = t3 * t;
    float t5 = t4 * t;
    float h00 = 1.0 - 10.0 * t3 + 15.0 * t4 - 6.0 * t5;
    float h10 = t - 6.0 * t3 + 8.0 * t4 - 3.0 * t5;
    float h01 = 10.0 * t3 - 15.0 * t4 + 6.0 * t5;
    float h11 = -4.0 * t3 + 7.0 * t4 - 3.0 * t5;
    float start_position = center_protection * center_scale;
    float start_slope = center_scale;
    float edge_slope = max(
        0.20, 1.0 - 0.5 * (center_scale - 1.0) * curve);
    return h00 * start_position + h10 * span * start_slope +
        h01 + h11 * span * edge_slope;
}

float nls_plus_map_coordinate(float coordinate, float center_scale,
                              float curve, int geometry,
                              float center_protection)
{
    float centered = coordinate * 2.0 - 1.0;
    float mapped_radius = nls_plus_map_radius(abs(centered), center_scale,
        curve, geometry, center_protection);
    return (centered < 0.0 ? -mapped_radius : mapped_radius) * 0.5 + 0.5;
}

vec4 hook()
{
    float strength = clamp(float({{strength}}), 0.0, 1.0);
    float curve = clamp(float({{curve}}), 1.0, 2.9);
    float ratio = clamp(stretch_ratio, 1.0, 1.5);
    float balance = clamp(float({{axis_balance}}), 0.0, 1.0);
    bool source_wider = warp_axis >= 0.5;
    int geometry = int(clamp(float({{geometry}}), 0.0, 1.0));
    float center_protection =
        clamp(float({{center_protection}}), 0.0, 0.45);

    // q is target/source aspect. The inverse-map centre slopes must satisfy
    // x_slope / y_slope == q to cancel the presentation layer's linear aspect
    // change at full strength. Balance zero reproduces the established selected
    // axis; 0.5 splits the correction equally in logarithmic aspect space.
    float q = source_wider ? 1.0 / ratio : ratio;
    float x_exponent = source_wider ? balance : 1.0 - balance;
    float y_exponent = source_wider ? balance - 1.0 : -balance;
    float x_scale = pow(q, x_exponent * strength);
    float y_scale = pow(q, y_exponent * strength);

    vec2 sample_pos = HOOKED_pos;
    sample_pos.x = nls_plus_map_coordinate(sample_pos.x, x_scale,
        curve, geometry, center_protection);
    sample_pos.y = nls_plus_map_coordinate(sample_pos.y, y_scale,
        curve, geometry, center_protection);
    return HOOKED_tex(clamp(sample_pos, vec2(0.0), vec2(1.0)));
}
