# VP-0012: Alpha renderer LUT pipeline contract spike

## Status

Draft.

## User story

As the maintainer of the experimental alpha renderer, I need a tested decision
on the exact 3D-LUT pipeline position and color contract before exposing any
calibration-LUT configuration, so the renderer cannot silently apply a LUT in
the wrong transfer/gamut space or apply a second conversion afterward.

## Why this is required

VP-0011 is for display/projector calibration LUTs, not creative source LUTs.
The bundled libplacebo API has several LUT attachment mechanisms whose meanings
are materially different:

- an image-frame LUT is applied in normalized RGB and is not automatically a
  post-tone-map output-calibration stage;
- a target-frame LUT has different conversion semantics;
- `PL_LUT_NATIVE`, `PL_LUT_NORMALIZED`, and `PL_LUT_CONVERSION` are not
  interchangeable, and conversion LUTs bypass ordinary color mapping.

Current alpha rendering creates a Rec.709 SDR swapchain target and can request
an SDR transfer/range through DXGI. It does not yet expose a render reference
primaries target or a post-calibration pass. DWM-composed presentation can
negotiate a different active signal than a requested setting, so a requested
gamma is not by itself proof of the signal reaching a projector.

## Scope

This is an engineering investigation. It may add test-only code, temporary
instrumentation, or an isolated renderer harness, but must not expose a user
LUT setting, change the released configuration schema, or claim production LUT
support.

Work from a clean worktree based on the current alpha renderer. Preserve the
existing no-LUT renderer output.

## Required investigations

1. **LUT placement and type**
   - With the bundled libplacebo version, test identity and visibly non-identity
     `.cube` LUTs attached to the image frame and target frame.
   - Determine the actual ordering relative to source decoding, HDR tone
     mapping, gamut mapping, output transfer/range encoding, and presentation.
   - Test only explicit `PL_LUT_NATIVE` and `PL_LUT_NORMALIZED` meanings that
     match the proposed contract. Do not use `PL_LUT_CONVERSION` merely to make
     a result appear on screen, because it replaces normal conversion/tone-map
     behavior.
2. **Reference-target and P3 path**
   - Prove a PQ/BT.2020 source can be tone/gamut mapped to a declared P3-D65 /
     gamma 2.20 reference before a calibration LUT, without reinterpreting P3
     mastering-display metadata as source primaries.
   - Exercise a gamut-stress sample outside P3 but inside BT.2020 and compare a
     roll-off mode (`perceptual` or `softclip`) with a clipping mode
     (`relative`).
   - Establish whether the high-level target-frame route can express this
     safely. If not, prototype the minimum supported offscreen/intermediate
     target plus final pass necessary to do so.
3. **Range, transfer, and presentation boundary**
   - Trace and log source frame metadata, selected reference target, LUT stage,
     target frame metadata, requested DXGI color space, and the active/
     accepted DXGI color space where available.
   - Validate that an identity LUT does not change known output samples and
     that a non-identity LUT changes only the expected samples.
   - Establish whether full and limited range can each be proven on this
     presentation path. If limited cannot, document full-only as the initial
     production boundary and reference VP-0004 as the follow-up.
   - Specifically test that no post-LUT gamut, transfer, or range conversion is
     introduced by the proposed path.

## Deliverables

1. A concise decision record added to this story containing:
   - selected LUT attachment/pass design;
   - explicit libplacebo LUT type and why it is correct;
   - input/reference/output contracts and their ordering;
   - supported initial primaries, transfers, ranges, and presentation path;
   - observed DXGI/DWM limitations and fallback behavior.
2. Reproducible test evidence: command/test fixture, input colors, expected
   results, and actual captured/read-back results. An identity LUT and a
   diagnostic non-identity LUT are mandatory.
3. A proposed minimal configuration mapping for VP-0011 using the existing
   `[display]` and `[display_rules.name]` inheritance model. Do not use a new
   `[libplacebo]` section unless the renderer configuration loader is
   intentionally redesigned and that redesign is separately justified.
4. A list of any VP-0011 wording/configuration changes required by the result.

## Acceptance criteria

- A maintainable, post-tone-map/post-gamut-map calibration-LUT path is proven,
  or the story records a concrete technical blocker and recommended alternative.
- The path states exactly where gamma/range/primaries conversion occurs and
  proves no unrequested conversion follows the LUT.
- The P3 calibration scenario retains BT.2020 as the HDR source representation
  and uses configurable gamut mapping to reach the P3 reference target.
- The initial range/presentation support boundary is explicit and tested.
- VP-0011 has been updated with the decision and can truthfully move forward
  from `Draft`, or remains in `Draft` with the next required action.

## Suggested validation status record

When work starts, replace this section with branch, commit, toolchain, GPU,
driver, Windows presentation mode, test fixtures, results, and remaining
real-projector validation. Do not move this story to Done from source review
alone.
