# VP-0109-2: Implement rejection-safe pure-2.2 Studio limited output

## Status

Backlog (created from the VP-0109 readiness review on 2026-08-09). Blocked by
acceptance of VP-0109-1. No implementation branch/worktree has been selected.

## Parent

VP-0109.

## Objective

Implement the contract approved by VP-0109-1: explicit Limited/2.2 output with
pure Gamma-2.2 renderer values, Studio/G22 P709 transport, transactional
retention/rollback, fail-closed behavior, truthful diagnostics, documentation,
and complete regression coverage.

## Scope

1. Add Gamma22 and explicit requested/planned/prior/active transition state to
   `LibplaceboOutputPolicy`.
2. Centralize render-transfer, levels, render primaries, DXGI transport, and
   diagnostic metadata. Remove UNKNOWN-to-sRGB substitution for Studio/G22.
3. Implement same-generation retention, Check/Set/Check acquisition,
   rollback-to-prior, generation invalidation, and fail-closed behavior under
   renderer serialization.
4. Ensure all libplacebo hint/resize operations precede the final VP DXGI set.
5. Update target metadata, LUT validation, readback, logs/OSD, support-probe
   context, configuration reference, and sample configuration.
6. Add and run the policy, transition, renderer, GPU-code, cache, lifecycle,
   HDR-to-SDR, preview, BT.2020-target, and recovery tests specified by the
   parent.

## Acceptance criteria

1. All VP-0109 production acceptance criteria pass in a clean x64 Release
   build and test run.
2. Explicit Limited/2.2 produces the approved pure-2.2 Studio R10 values and
   Studio/G22 P709 API contract on the active flip swapchain.
3. Invalid configuration is rejected without DXGI mutation. Runtime failure
   retains/restores a prior limited contract on the same valid generation or
   fails closed; it never renders an unrequested Full/sRGB fallback.
4. Auto-range/2.2, Full/2.2, and Composed/Limited remain explicitly invalid.
   Limited/Auto, Limited/2.4, Full/sRGB, preview, madVR, and recovery semantics
   retain their approved behavior.
5. BT.2020 render targets preserve the VP-0093 P709 transport plus AVI-signaling
   architecture.
6. Documentation and diagnostics distinguish renderer target, DXGI nominal
   transport, API acceptance, application-code evidence, wire evidence, and
   display-response evidence.

## Dependencies and likely paths

- Depends on accepted VP-0109-1 and parent VP-0109.
- Likely paths:
  `src\VideoProcessor-Lib\vprenderer\LibplaceboOutputPolicy.{h,cpp}`,
  `src\VideoProcessor-Lib\vprenderer\LibplaceboVideoRenderer.cpp`,
  `src\VideoProcessor-Test\LibplaceboOutputPolicyTests.cpp`,
  `src\VideoProcessor-Test\ConfigFileTests.cpp`, `CONFIGURATION.html`, and
  `VideoProcessor.cfg`.
