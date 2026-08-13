# VP-0127: Enforce output format and prove composed display delivery

## Status

In Progress (2026-08-13). The SDR active-output sweep in the VP-0125 beta
package reproduced both defects on the selected Epson output. The repository
default was rediscovered as `origin/v1.2.001-beta`; this continues the
developer-confirmed beta-based VP-0125 output work on
`codex/vp-0125-dxgi-output`, with the shared presentation-result contract also
now synchronized into the active VP-0126 branch `VP0126-pattern-generator`.

Readiness review: the requested and negotiated swapchain formats, the
libplacebo wrapper handoff, renderer-backbuffer readback, successful swap-call
count, DXGI presentation telemetry, and fullscreen HWND state are all logged.
The remaining platform boundary is explicit: a nonblack renderer backbuffer
and a successful swap call do not prove that DWM displayed the composed frame.
This story therefore separates renderer proof from display-delivery proof and
does not silently promote unavailable evidence to PASS.

Implementation commit `95f680d` and evidence-documentation commit `9c4e686`
are published on
`origin/codex/vp-0125-dxgi-output`. The VP-owned forced-8-bit path now creates
`R8G8B8A8_UNORM`, matching libplacebo's D3D11 8-bit selector, and supplies
`disable_10bit_sdr` plus the requested color depth even when libplacebo wraps
VP's external swapchain. Structured status now separates renderer readback,
successful submission, and presented-frame evidence. Direct/flip cases wait
for DXGI presentation evidence; composed/BitBlt submission without that proof
returns `MEASURE / DISPLAY DELIVERY UNVERIFIED` rather than PASS. Live and
retained sweep details report `rendered`, `submissions`, and
`display_delivery` independently.

Focused `LibplaceboOutputPolicyTests` passed 39/39. The x64 Release GUI,
VP-renderer DLL, and native test DLL compiled successfully; a whole-solution
invocation was separately obstructed in this automation environment by its
duplicate `Path`/`PATH` process environment in unrelated Config/OutputProbe
project tool launches, not by a source compile failure. Live Epson confirmation
of the final 8-bit format and revised composed result remains required.

## User story

As a VP beta tester, I want output experiments and generated test patterns to
report the format actually used and whether pixels were merely rendered or
were verifiably delivered to the selected display, so a black composed window
or an overridden bit-depth request cannot be reported as a successful test.

## Reproduction evidence

The 2026-08-13 SDR sweep produced the following reproducible evidence:

1. Test 8 requested a VP-owned 8-bit SDR swapchain. VP created
   `B8G8R8A8_UNORM` (DXGI format 87), after which libplacebo logged
   `Attempting to reconfigure swap chain format: B8G8R8A8_UNORM ->
   R10G10B10A2_UNORM`. The final contract correctly reported 10-bit and the
   test failed its requested-format assertion.
2. The composed tests rendered nonblack R10 backbuffers with full code-value
   excursions and successful swap calls. The selected display nevertheless
   showed a black interval. Presentation telemetry reported frame statistics
   unavailable and no confirmed presented frames, while the sweep incorrectly
   classified the cases PASS from swap-call success alone.

## Scope

1. Make the forced 8-bit SDR option authoritative for both libplacebo-owned
   and VP-owned swapchains. The wrapper must not upgrade a supplied 8-bit
   swapchain to 10-bit without returning an explicit unsupported/failure
   result.
2. Validate the swapchain format after libplacebo creation/reconfiguration and
   before a test can pass. Log requested, initially created, returned, and
   final formats plus the component responsible for any change.
3. Split active output evidence into at least: renderer content, swap-call
   acceptance, DXGI/direct presentation evidence, and physical/composed
   display-delivery evidence.
4. A nonblack backbuffer is renderer-content proof only. A successful
   `pl_swapchain_swap`/Present submission is submission proof only. Neither is
   sufficient to mark a composed/BitBlt fullscreen test PASS.
5. Classify composed output as `MEASURE` or `DISPLAY DELIVERY UNVERIFIED` when
   authoritative delivery evidence is unavailable. Preserve an explicit
   visual tester grade and investigate a bounded Desktop Duplication or
   equivalent output-capture probe for automated nonblack delivery evidence.
6. Show requested format, actual format, renderer-content state, submission
   state, delivery state, and fallback/reconfiguration reason in the live OSD,
   retained summary, and detailed log.
7. Reuse the same result model in VP-0126. The standalone test-pattern
   generator must not describe a pattern as visible or suitable for adjustment
   merely because Alpha rendered a nonblack backbuffer.
8. Keep direct/flip proof distinct from composed/BitBlt proof and keep physical
   wire/calibration measurement outside claims made by software-only checks.

## Acceptance criteria

1. A forced-8-bit VP-owned SDR test ends with an 8-bit swapchain and an 8-bit
   Alpha target, or fails explicitly before rendering; it never finishes on
   R10 while claiming the request applied.
2. Unit or seam tests reproduce libplacebo requesting a format change for an
   externally supplied swapchain and prove that the strict requested-format
   contract is retained or rejected deterministically.
3. Direct/flip and composed/BitBlt test cases record separate render,
   submission, and delivery states. Missing frame statistics or other missing
   delivery evidence cannot produce PASS.
4. A deliberately nonblack backbuffer behind a hidden, occluded, or otherwise
   nondelivered test surface is not classified as visible delivery.
5. If an automated desktop/output capture probe is safe and reliable, it
   compares a known changing marker or signature rather than merely checking
   for nonzero pixels. If it is unavailable, the result remains visibly
   `MEASURE` and accepts an explicit tester grade.
6. Active-output sweep logs and the fullscreen summary identify the exact
   proof boundary, including cases such as `rendered=nonblack`,
   `submitted=yes`, and `display_delivery=unverified`.
7. VP-0126 pattern presentation exposes the same states and instructions; its
   documentation distinguishes generated source codes, rendered backbuffer,
   composed display delivery, DXGI declaration, and measured external output.
8. Focused native tests, Config tests, and an x64 Release build pass. A live
   fullscreen rerun on the selected Epson output confirms the 8-bit result and
   no longer labels the observed black composed case PASS.

## Non-goals

- Claiming physical HDMI values, projector EOTF, or calibration accuracy from
  a backbuffer or desktop capture.
- Making composed output authoritative for limited-range or pure-power gamma
  contracts that DXGI/DWM cannot represent.
- Removing the tester's visual or meter-based measurement step.

## Dependencies and risks

- Continues VP-0125 output-policy, diagnostics, and active-sweep work.
- VP-0126 consumes the shared result model but remains independently releasable
  when it clearly reports delivery as unverified.
- Desktop Duplication may be unavailable for protected content, Advanced
  Color, mode transitions, or some fullscreen paths. Failure to capture is an
  explicit unavailable result, never implicit success.
- BitBlt frame statistics are commonly unavailable; the implementation must
  not manufacture direct-presentation evidence from a successful API return.
