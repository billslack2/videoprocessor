# VP-0166: madVR-style external HDR 3D LUT tone mapping

## Status

In Progress. Created on 2026-08-29 as the consolidated replacement for VP-0100
and VP-0101; the consolidation was committed and synchronized to tracker
`main` as `c0a5480641c20101bb34392315687fbc20b68774` before source work began.

Implementation started on 2026-08-29 from freshly fetched
`origin/v1.3.004-beta` at
`66f22307158b2da40f2cdb0ee398ea6ab62c5997`, on source branch
`codex/vp-0166-lut-contract` in the clean worktree
`C:\Videoprocessor\vp\vprenderer\.codex-worktrees\vp-0166-lut-contract`.
The first slice resolves a typed Rec.709/SDR-BT.2020 LUT-input and carrier
contract, semantic-zero black handling, and focused contract tests before the
reload-state refactor.

## Direction correction — 2026-08-30

The prior target-frame display-calibration interpretation is withdrawn. The
requested product contract is the distinct madVR HDR mode shown as **tone map
HDR using external 3DLUT**, owned by each tone-mapping/Rendering profile. Where
this section conflicts with the 2026-08-29 implementation history or the old
user story below, this section controls until the obsolete text is rewritten.

Behavioral parity means:

- the Rendering profile selects one of three HDR paths: passthrough to the
  display, VP/libplacebo shader tone mapping, or external 3D-LUT tone mapping;
- external mode owns three independently configured LUT slots labeled BT.709,
  DCI-P3, and BT.2020, and VP selects the appropriate slot from authoritative
  source/content gamut evidence rather than from the display target label;
- the selected LUT replaces internal HDR tone and gamut mapping exactly once;
  it is not a display-calibration cube applied after VP has already tone mapped;
- each external-LUT profile owns the outgoing HDR metadata gamut and peak-nits
  values sent to the display;
- slot selection, missing-slot conversion/fallback, source-gamut ambiguity,
  Cube input/output transfer and range, HDR signaling failure, reload, and
  no-LUT fallback are deterministic, fail closed, logged, and independently
  tested; and
- VP continues to accept a documented `.cube` contract. Behavioral parity does
  not silently imply binary compatibility with PC madVR `.3dlut` files.

The separately advertised Luma feature "multiple LUTs per frame ADL" is a
relevant future extension, not evidence of madVR behavior. Preserve a schema
path from each gamut slot to an ordered Average Display Level LUT bank, but the
first madVR-parity milestone binds exactly one cube per gamut slot. ADL-bank
acceptance additionally requires a defined ADL measurement domain/window,
scene-cut behavior, temporal smoothing/hysteresis, endpoint policy, compatible
cube dimensions/domains, two-LUT GPU interpolation without per-frame resource
recreation, and tests for monotonic transitions and flicker. The existing Cube
parser already accepts dimensions beyond the advertised 2^3 through 65^3
range; file dimension alone does not implement ADL adaptation.

### Contract checkpoint 1: madVR behavior reviewed

The first independent madVR review closes the observable product contract:

- the three slot names are the **nonlinear RGB input gamut expected by the
  LUT**, not its output gamut and not the outgoing HDMI metadata gamut;
- external mode is static LUT-owned tone and gamut mapping, mutually exclusive
  with passthrough and internal pixel-shader tone mapping;
- exact-gamut selection wins. The documented wide-gamut fallback is frozen as
  `P3: P3 -> 2020 -> 709` and `BT.2020: 2020 -> P3 -> 709`. BT.709 selects
  only the 709 slot; if it is absent VP uses the internal shader path because
  PC madVR's reverse BT.709-to-wide-slot behavior is not authoritative. Any
  accepted fallback converts source RGB into the selected LUT's declared input
  primaries before lookup and logs that fact;
- slot selection uses resolved stream/source primaries. It does not inspect
  pixel coverage or reinterpret an ordinary BT.2020 container as P3 from
  mastering-display coordinates in v1. Unknown primaries fail to the configured
  internal shader path;
- v1 external HDR LUT input is HDR10/PQ only. HLG, unknown transfer, HDR10+,
  and Dolby Vision are rejected to the configured internal shader path rather
  than silently preconverted;
- a VP `.cube` coordinate in `[0,1]` represents normalized nominal nonlinear
  RGB picture code. Range normalization/restoration occurs outside the cube;
  Cube files do not inherit madVR binary-header semantics;
- the first runtime milestone is PQ input to PQ output. The profile declares
  LUT-output primaries and peak luminance separately from the input-gamut slot;
  a later explicit SDR-output role may reuse the loader but is not inferred;
- outgoing HDR10 metadata gamut and peak nits are profile-owned and independent
  of the slot selected. VP must verify the applied DXGI/PQ output state and
  must not reproduce PC madVR's known path where configured metadata may not
  actually reach the output; and
- a missing, invalid, ambiguous, or runtime-failed LUT falls back to that
  profile's explicitly configured VP/libplacebo shader tone mapper. It never
  falls through to unsafe HDR passthrough.

The established presenter/backbuffer may be reused, but the old
`target.lut = ...; PL_LUT_NATIVE` seam is rejected for this mode. Pinned
libplacebo's `pl_render_params.lut` with `PL_LUT_CONVERSION` is the required
candidate because it replaces image-to-target color conversion, including tone
mapping. The pipeline checkpoint must prove the pre-LUT gamut conversion and
PQ-in/PQ-out metadata rather than assuming them from the enum name.

The former archive
`VP-0166-3D-LUT-Test-v1.3.004-beta-0416d22.zip` is withdrawn and was moved to
`Done\Withdrawn` as a recoverable `.WITHDRAWN.zip`. It must not be shared or
used as acceptance evidence. Source work continues on
`codex/vp-0166-madvr-external-lut`; the old
`codex/vp-0166-lut-contract` branch is retained only as implementation history
and a source of reusable strict-Cube and atomic-reload code.

### Contract checkpoint 2: profile schema and editor reviewed

The reviewed configuration foundation is committed and pushed on
`codex/vp-0166-madvr-external-lut` as `cc946c4c`. It defines the three
HDR modes, three input-gamut Cube slots, and independent outgoing HDR10 gamut
and peak declarations. The pure selection policy freezes exact-slot priority,
the conservative P3/BT.2020 fallback order, explicit preconversion on every
fallback, HDR10/PQ-only v1 eligibility, and internal-shader fallback for every
missing, unsupported, or ambiguous case. Omitted or invalid mode tokens retain
the existing internal pixel-shader behavior.

The Cube boundary is normalized nonlinear picture RGB: source range decoding
maps nominal black/white to 0/1, and the first output milestone is full-range
PQ/BT.2020. Outgoing metadata uses the configured primaries, D65 white,
configured mastering maximum, zero mastering minimum, and zero MaxCLL/MaxFALL
to mean unknown rather than fabricated content measurements. External values
are suppressed whenever the external LUT is not active.

The legacy final-calibration declaration remains a separate SDR post-mapping
stage. It is available with internal tone mapping. HDR passthrough masks it
because that path retains an HDR target outside the existing calibration
contract, and external HDR LUT mode masks it because the external LUT already
owns the target transform. The declaration is preserved in both cases, keeping
a named external profile representable when it inherits legacy calibration.
An external failure that atomically restores internal pixel-shader tone mapping
also restores eligible final calibration. Pure-policy, editor, and
inherited/root profile tests freeze these ownership transitions.

Independent profile and madVR-contract reviews report no remaining P1/P2 issue
in this schema/editor/policy slice. The focused external-HDR tests pass 6/6 and
the complete Config editor test executable passes after the final ownership
reconciliation. This checkpoint is not a
tester build: the next work is loading/selecting runtime Cube resources at the
`pl_render_params.lut` / `PL_LUT_CONVERSION` seam and applying/verifying the
full-range PQ/BT.2020 DXGI color space and HDR10 metadata atomically.

### Contract checkpoint 3: HDR10 metadata and rollback policy reviewed

The pure HDR10 output gate is committed and pushed on the source branch as
`e1eaf4b1`. It builds exact BT.709, P3-D65, or BT.2020 mastering-primary
coordinates with D65 white, whole-nit maximum mastering luminance, zero
minimum, and zero MaxCLL/MaxFALL (unknown rather than invented). The focused
external-HDR policy set now passes 9/9.

Activation requires valid built metadata, a top-level VP-owned flip presenter,
R10, active Windows Advanced Color, Check/Set/recheck acceptance of full-range
G2084/P2020, IDXGISwapChain4, and accepted HDR10 metadata submission. The state
is explicitly API-accepted, not claimed wire-verified. Prior HDR state, a
successful color-space mutation, or a successful metadata mutation requires
metadata clear plus set-and-verify SDR rollback. Internal SDR presentation
remains unsafe until all rollback evidence succeeds; otherwise the live wiring
must suppress Present and recreate the output contract.

Independent pipeline and calibration reviews caught and corrected a critical
DXGI unit mistake before runtime wiring (maximum mastering luminance is whole
nits, while only minimum uses 0.0001-nit units), plus metadata-validity and
rollback atomicity gaps. Their final reviews report no remaining P1/P2 in this
policy slice. Runtime API calls and Cube resources are still unwired, so this
remains not tester-ready.

### Contract checkpoint 4: atomic three-slot resource generation reviewed

The external HDR Cube resource seam is committed and pushed on the source
branch as `2314f9cd`. A candidate loads BT.709, P3-D65, and BT.2020 off-side,
then publishes or discards the complete set as one render-thread generation.
Partial-valid generations replace all prior slots without mixing profiles;
zero-valid generations deliberately publish internal fallback so a prior
profile's LUT cannot remain authorized.

The monotonic profile transaction is bound to the candidate before file I/O.
Older and duplicate completions cannot be relabeled or committed. Resolution
requires the currently expected profile transaction and returns internal
pixel-shader rendering with no LUT while a newer profile's files are in flight.
Borrowed LUT lifetime is limited to one render-thread interval with no commit:
resolve, verify the generation, copy the descriptor into frame-local render
parameters, and synchronously render.

Regression tests distinguish unconfigured, configured-missing, invalid, and
available slots; cover exact and explicit-gamut fallback selection; prove
zero-valid fallback, older/equal transaction rejection, in-flight profile
fallback, old-resolution invalidation, and no cross-profile slot reuse. The
x64 Release build succeeds, the external-HDR set passes 12/12, and the complete
LUT-parser class passes 19/19. Independent madVR-contract, profile, and render-
seam reviews report no remaining P1/P2. ADL LUT banks remain a future extension
and are neither implemented nor blocked by this single-LUT-per-gamut set.

This checkpoint is still not tester-ready. The next slice wires selected
profile declarations and transaction generation into renderer state, then
attaches the resolved LUT through frame-local `pl_render_params.lut` with
`PL_LUT_CONVERSION`; HDR10 carrier activation and rollback remain gated until
the corresponding live DXGI calls are integrated and verified.

### Contract checkpoint 5: renderer resources loaded but activation blocked

The selected renderer settings and initialization resource layer are committed
and pushed as `6c12d5f9`. Root and selected-profile resolution now carry the
typed HDR mode, all three independent slot declarations, outgoing metadata
primaries, and metadata peak. Each relative path has independent configured,
resolved, constrained-base, and preflight-rejection evidence; a path escape
therefore remains visibly configured and rejected rather than collapsing into
an unconfigured slot. Every semantic field participates in the effective and
restart fingerprints. External-HDR changes remain a conservative renderer-
rebuild boundary in this slice.

After the libplacebo log exists and before D3D/render startup, initialization
loads and atomically commits the complete three-slot generation. Retirement
releases the set before destroying the log. The renderer records only
`Loaded; HDR carrier not armed` or internal fallback status: it never resolves
the set for a frame, attaches `target.lut` or `pl_render_params.lut`, changes
the output target, submits HDR metadata, or arms a PQ/BT.2020 carrier. Current
pixel-shader/SDR rendering and the separate legacy final-calibration path are
unchanged.

The x64 Release build succeeds, the external-HDR set passes 13/13, and the
complete LUT-parser class passes 20/20. Independent madVR-contract, profile,
and render-seam reviews report no P1/P2 and approve this as a safe, bounded,
initialization-only slice. Per-renderer transaction `1` is acceptable only
because no resource can authorize pixels or display state yet. Before live
reload or activation, bind candidate identity to the application/profile
generation, add same-path content-identity detection, prepare candidates
outside the render mutex, atomically commit paired settings/resources at the
pre-frame safe point, and preserve the resolve/current-check/render interval.

This remains not tester-ready. The next activation slice must first implement
the gated frame-local `PL_LUT_CONVERSION` path and then join it atomically to
the verified HDR10 carrier/rollback state; neither half may present alone.

### Contract checkpoint 6: frame-local conversion seam gated fail-closed

The selected external resource can now be projected into a complete
frame-local render description at the image-side `pl_render_params.lut` /
`PL_LUT_CONVERSION` seam. This is committed and pushed on the source branch as
`396a07c9`. Runtime activation remains a compile-time false carrier gate, so a
loaded Cube still cannot change presentation in this checkpoint.

Attachment requires the current profile transaction, HDR10/PQ source input,
and a VP-owned target description that is RGB, full range, 10-bit, PQ, and
BT.2020. Exact-gamut attachment preserves source metadata. A P3/BT.2020
fallback retags only the frame-local LUT input and pins only a frame-local copy
of libplacebo color-map parameters to `pl_gamut_map_clip`, causing libplacebo
to convert source primaries into the chosen slot before lookup. A missing clip
mapper fails closed. Successful external attachment disables peak detection
and masks the separate legacy target/native calibration LUT for that frame;
every rejected or unarmed path preserves the shared target, render parameters,
dither, peak detection, and legacy LUT unchanged.

The x64 Release rebuild succeeds after clearing one incremental-linker
`LNK1103` artifact. The external-HDR set passes 17/17 and the complete LUT-
parser/GPU class passes 24/24. Tests cover the hard-false carrier, exact
metadata, non-RGB and non-PQ target rejection, explicit clipped fallback,
missing fallback mapper, legacy-LUT masking/preservation, and end-to-end stale
profile-generation rejection. Independent madVR-contract, profile-state, and
render-seam reviews report no remaining P1/P2 and approve this inert seam.

This is still not tester-ready. Before activation, join LUT selection and the
HDR10 output mutation into one frame transaction: requested external mode with
any unattached component must suppress Present and roll back/rebuild, and the
legacy target-LUT error-diffusion workaround must not leak into the external
conversion path. An offscreen `PL_LUT_CONVERSION` golden-pixel test, including
fallback-gamut conversion, is required before the carrier gate is armed.

### Contract checkpoint 7: conversion-LUT GPU path proven

The offscreen WARP proof is committed and pushed as `eab0621e`. It invokes the
production frame-projection helper, attaches the selected Cube on the image-
side `PL_LUT_CONVERSION` seam, and reads back the rendered pixel. A nonlinear
PQ sample `(200,136,48)` through the asymmetric R/B-swap Cube produces the
exact-slot golden `(48,136,200)`. With only the P3-D65 slot available, an
independent double-precision ST-2084 decode, linear BT.2020-to-P3-D65 matrix,
clip, ST-2084 encode, then Cube oracle predicts `(56,112,208)`; WARP matches
within two 8-bit codes per channel. Every fallback channel is at least eight
codes from the exact result, so a skipped primaries conversion cannot pass.

The fixture uses libplacebo's real `pl_gamut_map_clip`, disables nondeterministic
processing, and proves the external path masks a pre-existing target/native LUT
and peak detector. The x64 Release build succeeds; external-HDR tests pass
19/19 and the complete LUT-parser/GPU class passes 26/26. Independent madVR-
contract review reports no P1/P2. This is GPU seam evidence, not an R10 carrier
or physical-display proof, and it does not make the build tester-ready.

Carrier arming remains blocked on two prerequisites: bind resource work to the
application/profile generation with same-path content identity and last-known-
good rules, then land the indivisible live vertical slice comprising the HDR10
DXGI transaction/rollback, PQ target, LUT attachment, and Present suppression.

### Contract checkpoint 8: profile-bound reload transaction foundation

The resource transaction foundation is committed and pushed as `6ab7c588`.
The GUI's already-available application profile generation is captured before
`Build`, frozen into renderer initialization without a plugin ABI change, and
reserved on the active LUT set before any Cube file I/O. Initial candidate
identity is therefore application/profile-bound rather than a renderer-local
constant. Unrelated compatible live snapshot generations do not stale LUTs.

The active set now has separate latest-requested, latest-processed, and active-
resource transaction generations. `BeginRequest` makes newer work supersede an
older load while both are still in flight; reload completion requires exact
reservation equality, and delayed or duplicate completion is rejected. Loader
identity compares status/rejection, exact opened canonical path, byte count,
SHA-256, and cube dimension for all three slots.

Same-contract unchanged bytes retain the active transaction. A valid same-path
replacement atomically advances the complete three-slot generation. If a same-
contract candidate makes any formerly available slot unavailable, the entire
last-known-good set remains authorized; its active transaction is deliberately
unchanged while request/processed watermarks advance. A changed-contract
failure bypasses retention and commits internal fallback, so an old profile's
LUT can never masquerade under new settings.

The x64 Release build succeeds; external-HDR tests pass 22/22 and the complete
LUT-parser/GPU class passes 29/29. Independent profile, render-seam, and madVR-
contract reviews found and corrected the pre-I/O latest-request race, then
reported no remaining P1/P2. Runtime asynchronous configuration reloading is
not yet connected to this policy and carrier activation remains false, so this
checkpoint is still not tester-ready.

The next safe live work is the indivisible carrier vertical slice already
mapped in review: force/verify the top-level VP-owned flip/R10 path, perform the
PQ/BT.2020 color-space and HDR10 metadata transaction with reverse-order SDR
rollback, construct the PQ target, require frame LUT attachment, and suppress
Present on any mismatch or render failure. Resize, recreation, monitor change,
terminal black, and retirement must all participate in the same state machine.

### Contract checkpoint 9: fail-closed HDR10 carrier transaction

The pure/injected carrier transaction is committed and pushed as `e946565f`.
It owns the exact Check/Set/recheck/HDR10-metadata activation order and the
metadata-clear/Full-G22-P709-set/recheck rollback order. State is bound to
nonzero, exact swapchain, application-profile, and active LUT transaction
generations. An active or suppressed live swapchain cannot be reset by another
activation request; only verified rollback or destruction/replacement by a new
swapchain generation clears that ownership.

Repeated rollback failure remains `SUPPRESS_RECREATE`; a later complete retry
may return to SDR. Frame presentation authorization joins the carrier state to
the projection's actual LUT transaction and additionally requires that this
frame attached the conversion LUT and rendered successfully. Tests cover
activation and rollback order, active-to-invalid-profile retirement, failed
rollback retries, suppression persistence, generation mismatches, and the
carrier/LUT/render Present truth table.

The x64 Release renderer and test projects build, the output-policy class
passes 57/57, and the LUT-parser/GPU class passes 30/30. Independent pipeline,
profile-generation, and madVR-contract reviews report no remaining P1/P2. This
checkpoint remains unarmed by itself and is not tester-ready.

### Contract checkpoint 10: live external-HDR carrier and frame transaction

The live vertical slice is committed and pushed as `241e8a9d`. External 3D-LUT
mode now forces the top-level VP-owned flip/R10 presenter with one verified
Full/sRGB/Rec.709 rollback and internal-fallback baseline. For an eligible PQ
frame with a current selected slot, VP checks/sets/rechecks full-range
G2084/P2020, submits the independently configured HDR10 metadata, constructs a
10-bit Full RGB PQ/BT.2020 libplacebo target, masks the legacy target/native LUT,
and attaches the selected Cube only as frame-local `PL_LUT_CONVERSION`.

Present occurs only after the exact carrier/application/LUT generations join,
the selected LUT is attached to that frame, and `pl_render_image` succeeds.
GetBuffer/wrapped-target failure, render or attachment failure, resize, monitor
change, deferred output renegotiation, terminal black, profile-generation
change, recreation, and retirement all rollback or suppress/recreate the
carrier. DXGI interface references are released before recreating a flip chain.
Live and no-delta profile intents carry settings and application generation as
one render-safe-point value, including automatic source/viewport refresh, so an
old carrier cannot authorize a newer profile.

HDR passthrough is deliberately not claimed by this checkpoint: runtime reports
it unavailable and masks the legacy calibration LUT rather than silently
aliasing it to a completed passthrough path. ADL LUT banks and asynchronous
runtime Cube reload remain future work.

Validation passes the fresh x64 Release VPRenderer rebuild plus 57/57 output-
policy and 30/30 LUT/parser/GPU tests. Three rounds of independent pipeline,
profile-generation, and madVR-contract review found and corrected swapchain-
reference lifetime, early-exit rollback, fallback-baseline, legacy-parameter,
generation-coalescing, and viewport-state defects; final reviews report no
remaining P1/P2 in the external-3DLUT slice. A full solution build could not be
used as final evidence in this environment: VS2019 lacks the required v143
toolset, while VS18 hits the existing ConfigTests `Path`/`PATH` child-process
environment collision. The directly affected x64 Release products and focused
tests succeed.

This is the first end-to-end implementation checkpoint, but it is not yet a
tester package. Next acceptance work is a controlled real-display run on an
Advanced Color/HDR-capable top-level output, confirming activation/rollback
logs, HDR metadata, non-identity Cube effect, and no Present on forced failure.

### Contract checkpoint 11: current-beta rebase and HDR-analysis isolation

The VP-0166 branch is rebased onto the fetched `origin/v1.3.004-beta` tip
`f30ebdef052d921d74560a45d483f812ab817c87`; its reviewed tip is now
`df8df7d269024e51d3f5377d12582bd728169159`. The prior remote history ended at
`241e8a9d` on base `66f22307` and was therefore not current. A local safety
branch preserves that pre-rebase tip, and the remote feature branch was
updated with an explicit old-object force-with-lease after comparison showed
the same thirteen VP-0166 changes rebased, not unrelated remote work.

The single textual conflict combined the beta's fixed-mode HDR-analysis
control enablement with VP-0166's inherited/effective-value tracking. Rebase
review then found one substantive integration defect: external-LUT frames
disabled frame-local libplacebo peak detection but the new beta analysis crop
and telemetry still consulted the shared configured parameter. Those frames
could therefore report restricted/stale analysis despite the external LUT
owning the complete PQ transform. Commit `df8df7d2` now derives crop and
metadata telemetry from actual per-frame peak-detection activity, clears
shared crop state, and resets interval counters while the external carrier is
armed.

The combined HDR-analysis, output-policy, and LUT/parser/GPU suite passes
102/102. The complete C++ test DLL passes 1065/1066; the sole failure is the
current beta's configuration-reference mismatch (four viewport crop fields
exist only in the public inventory and one HDR-analysis-position field exists
only in `CONFIGURATION.html`), not a LUT/rendering failure. Independent
pipeline, profile-generation, and madVR-contract re-reviews report no
remaining P1/P2. The pre-test deployment and configuration were restored
byte-for-byte; no rebased test package is deployed yet.

Fresh x64 Release rebuilds of the renderer, main GUI, configuration editor,
and configuration-editor tests now succeed at `df8df7d`. The previously noted
Config/ConfigTests build obstacle was environmental rather than a source
failure: the Codex child environment exported distinct `PATH` and `Path`
entries, while a child with one canonical `PATH` builds normally. The complete
configuration-editor executable test run, including LUT discovery and
inherited external-HDR control state, exits successfully. The next acceptance
step is the controlled HDR-display run; deployment remains intentionally
restored until that isolated test begins.

### Contract checkpoint 12: explicit operator controls

Tester review corrected four misleading editor affordances in `145a48db`.
The External HDR 3D LUT card now has an explicit enable checkbox tied to the
existing mutually exclusive processing mode. Outgoing metadata gamut no
longer displays an inherited/default BT.2020 choice, and outgoing peak nits no
longer receives a fabricated 1000-nit value when external mode is selected;
both remain visibly required and unset until the operator supplies them. The
legacy final-calibration inspection card is removed from the editor surface.
Compatibility parsing remains intact so opening and saving an older file does
not destroy its declarations.

The complete x64 Release configuration-editor test executable passes,
including enable/mode synchronization, required-empty metadata, explicit
persistence, missing-Cube retention, inherited external mode, and absence of
the legacy card. A fresh editor and main executable build succeeds. A matching
non-deployed standalone test directory is available at
`C:\Users\bslac\Documents\ChatGPT\Done\VP-0166-145a48d-standalone`.

Follow-up tester review in `eb7dd38e` removes the remaining fabricated slot
default: an unpopulated root BT.709, P3-D65, or BT.2020 selector now reads
`None`. The three slots are optional individually, as in madVR; external mode
still requires at least one usable Cube. Named VP profiles identify a genuine
inherited Cube path rather than calling it a default. The full editor test
executable passes, and the refreshed standalone directory is
`C:\Users\bslac\Documents\ChatGPT\Done\VP-0166-eb7dd38-standalone`.

## Implementation progress — 2026-08-29

The first source slice is committed and pushed on
`codex/vp-0166-lut-contract` as
`16377298ebfb4b0a235c4edc5b48fdcf8e160fab`. It adds:

- `LibplaceboLutContract`, a pure resolved-contract and activation validator
  for the v1 Rec.709/SDR-BT.2020, full-range, R10, video-picture target/native
  stage;
- exact cube path/hash/size/reload, calibration generation, typed presentation
  route, display-mode/installation attestation, final target, semantic `Lw/Lb`,
  Advanced Color/ICC, and NVIDIA BT.2020 proof bindings;
- fail-closed rejection reasons for unsupported or stale contracts, including
  P3, limited range, PQ/HLG target cubes, unresolved `AUTO` values, route or
  resource changes, and incomplete BT.2020 carrier evidence;
- semantic-zero black adaptation to `PL_COLOR_HDR_BLACK` at all three existing
  SDR libplacebo metadata write sites while retaining semantic zero in the VP
  contract; and
- 18 focused contract tests, including the accepted Rec.709 and SDR BT.2020
  matrix and inert-P709-carrier/BT.2020-target independence.

Both VPRenderer and the test project build successfully in x64 Release. The
focused contract suite passes 18/18 and the complete `Libplacebo`-named test
set passes 97/97. The whole test DLL passes 1018/1019; its sole failure is the
pre-existing configuration-reference inventory mismatch on four viewport crop
fields. `CONFIGURATION.html`, both inventory files, and that test are unchanged
from the `v1.3.004-beta` base, so it is recorded as an upstream baseline issue,
not a LUT regression.

Three independent AI specialist passes covered the current VP source seam,
mpv/MPC/madVR precedent, and calibration/BT.2020 correctness, then cross-checked
the fixes. Their final reviews found the contract foundation safe to wire. One
important wiring invariant is retained in the header: populate final `Lw/Lb`
evidence from the semantic pre-adapter doubles, never from libplacebo's float
metadata where semantic zero is represented by a sentinel.

This commit is deliberately a foundation, not completed runtime activation.
The next slice must resolve selected-profile configuration into this object,
populate live carrier evidence, replace the legacy pre-override/wildcard LUT
gate with `ValidateActivation`, and make dither depend on `Active` rather than
merely parsed state. Strict cube canonicalization and atomic reload/failure
lifecycle follow that activation wiring.

### Fail-closed runtime wiring and Cube proof — 2026-08-29

The next source slice is committed and pushed on
`codex/vp-0166-lut-contract` as
`2ed16b05fa3fd3cb143a2ba2f9d0c56a777abff4`. It:

- removes the legacy target/signal validator from runtime and calls the typed
  `LibplaceboLutContract::ValidateActivation` gate with independently labeled
  expected-contract and observed-resource/carrier evidence;
- leaves selected-profile provenance, authoring depth, expected cube SHA-256,
  installation attestations, Advanced Color/ICC proof, carrier identity, and
  contract generations explicitly unresolved. The runtime therefore rejects
  at `CONTRACT_ORIGIN_UNKNOWN`, keeps `target.lut` null, and retains ordinary
  dither until those facts are implemented; it does not mislabel this
  intermediate state as active calibration;
- separates a Rec.709 or SDR BT.2020 LUT-input target from the inert P709 DXGI
  carrier instead of requiring their primaries labels to match;
- makes LUT activity and its render-parameter projection atomic, and ensures a
  render-rejected resource is not retried even if ordinary-parameter
  restoration throws;
- rejects non-default Cube `DOMAIN_MIN/MAX` before pinned libplacebo can apply
  its incompatible output-rescale interpretation; and
- replaces the silently skipped external-fixture test with a deterministic
  WARP channel-swap application/order test plus an exact, licensed OpenColorIO
  interoperability fixture pinned by source commit and Git blob.

The x64 Release solution and VPRenderer project build successfully. The final
focused parser/typed-contract/render-parameter suite passes 44/44. Before the
legacy contract tests were removed, the expanded focused suite passed 51/51.
The complete test DLL passed 1020/1021; its sole failure remains the existing
`CONFIGURATION.html`/public-field inventory mismatch, outside the LUT files.

A follow-up proof commit,
`0479fe02b00292f1f28ba0453270ae17f18817f8`, embeds the exact 3,406-byte
nonlinear 4x4x4 Cube example published by MIT-licensed
`cube-lut-factory.js` at pinned commit
`fde633ad057e514bd3f04049cee3289af93cef2b`. The test independently evaluates
the published formula/lattice value: WARP maps 8-bit input `(85,170,255)` to
approximately `(99,82,198)`. The embedded bytes match the pinned README source
exactly, and the focused suite now passes 45/45. This complements the
OpenColorIO semantic-rejection fixture with a real internet Cube that parses
and applies at a numerically predictable point; neither fixture is represented
as a physical display-calibration profile.

Two independent specialist reviews approved the carrier split, activation
state transitions, evidence labeling, and deterministic fail-closed behavior.
This is a go for the wiring/test foundation, not yet a go for production LUT
activation. The next slice must freeze a selected calibration-profile record,
add LUT authoring black/depth and independent opened-resource canonical
path/SHA-256 evidence, and populate the installation, Advanced Color/ICC, and
carrier identity/generation proofs. Transactional same-path replacement and a
nonlinear independent tetrahedral/clamp oracle remain required afterward.

### Carrier-bound runtime and tester package — 2026-08-29

The activation and lifecycle slice is complete and pushed on
`codex/vp-0166-lut-contract` as
`0416d22c7365de5e436f836a10bc66c21906bc8e`. It adds:

- an immutable selected-profile record that must own the cube and every v1
  declaration, including authoring depth, cube size/content hash, installation
  attestations, display-mode authority, and the declared carrier-identity hash;
- SHA-256 and canonical-path evidence calculated from the exact opened Cube
  handle, plus bounded same-path content detection on configuration reapply;
- atomic last-known-good candidate replacement, with a rejected same-contract
  replacement recorded separately and retried without publishing its identity;
- the production `output_presentation: calibrated_direct` selector for the
  existing top-level VP-owned R10 flip presenter, with no backbuffer/presenter
  redesign;
- live fail-closed carrier evidence from exact DisplayConfig path/mode indices,
  adapter/target IDs, supported connector technology, physical mode and refresh,
  renderer adapter/driver, validated OS-effective EDID, normalized monitor path,
  connector instance, swapchain output, and mutually corroborated reported RGB
  bit depth;
- Windows Advanced Color v2 checks that accept only explicit SDR state and
  reject active HDR/WCG, unknown/virtual output technology, incomplete EDID or
  topology evidence, cross-adapter routes, and any declared/observed carrier
  mismatch; and
- a canonical carrier log/hash bootstrap and swapchain-generation invalidation
  so the operator can bind a profile to the measured route without treating a
  stale current route as its expected identity.

The final independent renderer/API/calibration reviews found no serious design
issue and no need to move the LUT from the established target-native seam. Their
last two P1 findings—fail-open unknown output technology and incomplete Windows
Advanced Color v2 interpretation—were corrected before packaging. The remaining
engineering observation is performance rather than correctness: the active LUT
safety guard re-queries display topology on the render path. This is intentionally
retained for the targeted test build; presentation/drop telemetry must establish
whether it needs an event-driven snapshot optimization before production release.

The post-commit x64 Release solution build succeeds and identifies clean commit
`0416d22c`. The focused parser/contract/render/output suite passes 47/47. The
complete test DLL passes 1015/1016; the sole failure is the same
`ConfigurationReferenceMatchesPublicFieldInventory` viewport-documentation
mismatch reproduced on untouched `v1.3.004-beta`, not a LUT regression.

The end-user test archive is
`VP-0166-3D-LUT-Test-v1.3.004-beta-0416d22.zip` (SHA-256
`9B3F617A903A198CF91241AE1B3EA39EB102324F60158A3218BCB000120BA629`). It
contains the verified immutable release layout, safe merge instructions, a
two-pass carrier/attestation template, an identity cube, an obvious synthetic
red/blue-swap cube, and the exact licensed nonlinear Cube example pinned from
the internet source. The fixtures prove loading, ordering, and application;
they are not represented as physical display calibrations. Real-output
measurement and performance telemetry remain the acceptance work before this
story can move to Done.

## User story

As a calibrated-display user, I want VP Renderer to feed a 3D `.cube` file the
exact full-range Rec.709 or SDR BT.2020 encoded RGB reference space for which it
was authored, apply it once at the final video-picture calibration stage, and
preserve predictable dither, reload, fallback, and diagnostics, so the
displayed video is calibrated without an accidental second color transform or
a silently mismatched cube.

## Decision and implementation boundary

VP-0011 and VP-0012 selected the correct architectural seam:

`source decode -> scale -> tone/gamut map -> target primaries/transfer ->`
`full-range target encode -> target PL_LUT_NATIVE -> final video dither`

Keep that seam. It matches mpv/libplacebo's target/display-LUT model and the
broad ordering used by MPC-BE's internal ICC-LUT path. External MPC Video
Renderer does not implement user 3D LUTs and is not a contract model.

It also matches madVR's important color-contract behavior: the gamut named by
the calibration LUT is the RGB input gamut madVR prepares before lookup. A
BT.2020 source with a BT.2020-input LUT is not first converted to Rec.709; a
Rec.709 source is converted to the declared BT.2020 reference when that is what
the cube expects. PC madVR's binary `.3dlut` and VP's `.cube` are not file- or
range-compatible, however: PC madVR uses video-level RGB at its LUT boundary,
while VP-0166 deliberately defines full-range normalized Cube input.

This first production contract is explicitly a **video-picture calibration
LUT**, not a creative LUT, source-normalization LUT, color-conversion
replacement, ICC workflow, or whole-backbuffer shader. The active video
picture passes through the cube. VP's current target overlays (statistics,
sweep, and profile notices), diagnostic overlays, and out-of-picture
clears/borders are composited or cleared later and do not. Subtitle pixels
already baked into the video picture do pass through it; this story does not
invent or make claims for a separate subtitle-overlay path that VP does not
currently construct.

No presenter redesign is in scope. Whole-backbuffer calibration would require
a separately approved final composed-frame LUT-and-dither pass. During
calibration measurement VP must suppress optional OSD/diagnostic overlays, and
an accepted v1 cube must preserve a neutral zero black so bypassed zero-black
borders do not acquire a visible chromatic or level discontinuity.

## Current implementation assessment

The `004-beta` code already has useful bounded loading, path containment,
3D-only dimension limits, cached libplacebo resources, target-native
attachment, diagnostics, and no-LUT fallback. The following defects or missing
contracts must be corrected together:

1. Target/signal equality is calculated before the renderer replaces target
   fields with the selected calibration target. The stale boolean is then used
   to accept or reject the LUT. Equality of reference target metadata and the
   presentation carrier is not the same as proof that the carrier preserves
   the produced codes.
2. `AUTO` reference fields are wildcards. A cube can remain accepted after a
   renderer/profile target changes. Every field must instead resolve to a
   concrete value before activation.
3. LUT reference white exists but reference black does not. This makes a true
   BT.1886 authoring contract unverifiable.
4. The public configuration can request P3-D65 even though the runtime rejects
   it. Merely accepting a P3 enum would still be unsafe: DXGI has no ordinary
   SDR RGB P3 swapchain declaration, and a Windows-managed transport would not
   preserve opaque device-native cube output as semantic P3.
5. At `PL_LUT_NATIVE`, a limited 10-bit target presents nominal black and white
   to the cube at `64/1023` and `940/1023`. BT.1886 picture values instead use
   `(D-64)/876`. Treating these domains as interchangeable is incorrect.
6. Pinned libplacebo parses non-default `DOMAIN_MIN/MAX` by rescaling table
   outputs rather than retaining Adobe's input-domain transform. Such cubes
   can load but mean the wrong thing.
7. Parsed, contract-active, and runtime-rejected states are conflated. This is
   a latent dither-state bug: pinned presets do not currently select error
   diffusion, but an incompatible path could remain disabled after a cube is
   rejected or fails at render time.
8. Replacing a cube's contents at the same path is not a reload key. The old
   resource can remain resident until the path or renderer changes.
9. The current tests prove basic parsing and a gross identity/non-identity GPU
   difference, but do not independently prove cube ordering, tetrahedral
   interpolation, target contract, clamp, profile invalidation, same-path
   reload, or failure-state dither restoration.

## Resolved v1 calibration contract

### LUT role, target, and carrier

- Attach exactly one display-calibration cube to the libplacebo target frame
  with `PL_LUT_NATIVE`.
- The cube receives encoded target RGB after source interpretation, scaling,
  HDR-to-SDR tone mapping where required, gamut mapping, target primaries,
  target transfer, and target representation encoding.
- The cube's outputs are device/native drive codes. Do not relabel those
  outputs with the cube's input primaries or call them a generic P3/BT.2020
  wire signal.
- Keep ordinary final video dithering after the cube. Disable error diffusion
  only while an active LUT makes it incompatible; restore the selected normal
  dither path immediately when the LUT is inactive or rejected.
- Replace `targetMatchesSignaledOutput` as the activation concept with two
  independent facts: the resolved LUT-input target matches the cube contract,
  and exact carrier evidence matches the supported v1 predicate below. A
  carrier's DXGI label need not duplicate the calibration target label, but VP
  must not claim that the label describes post-LUT native codes.
- V1 supports only a verified legacy SDR path with Windows Advanced Color/HDR
  off and no competing ICC/MHC/VCGT/device correction. A physical
  display-correction cube and the OS, GPU, or display must not both own the same
  correction. Disabling libplacebo ICC alone is insufficient: Windows can
  automatically color-manage integer swapchains while Advanced Color is on,
  and per-process legacy-ICC compatibility is not fully controlled by VP. The
  profile and diagnostics must state the required OS/display mode. The bounded
  automatic guard queries Advanced Color/HDR and rejects enabled or unknown
  state; VP does not claim it can discover projector-internal modes, vendor GPU
  ramps, or every external correction. Activation additionally requires an
  explicit profile attestation that those unobservable competing corrections
  are disabled: `lut_external_color_management: none_attested`. Missing,
  `AUTO`, or any other value fails closed.

The common v1 carrier predicate is:

- `actualOutput.safeToRender` and `requestedEncodingActive` are true;
- encoding is `FULL_G22_P709`, used only as the inert Windows/DXGI carrier;
- presentation is the existing top-level VP-owned R10 `FLIP` path with direct
  delivery separately attested through
  `lut_direct_delivery_authority: external_attested`; a flip swap effect alone
  is not evidence of DirectFlip/MPO/independent-flip delivery. Child preview,
  composed/`BITBLT`, and an unattested flip candidate reject the LUT;
- negotiated format is `DXGI_FORMAT_R10G10B10A2_UNORM`, effective sample and
  color depth are 10 bits with no bit shift, and `display_bit_depth` resolves
  to 10;
- target primaries are the resolved Rec.709 or BT.2020 LUT-input gamut and the
  target representation is full RGB;
- `actualOutput.targetTransfer` and the final target-frame transfer equal the
  resolved, supported LUT-reference transfer;
- Windows Advanced Color/HDR is confirmed off, no libplacebo target ICC is
  attached, and the profile contains the external-correction attestation; and
- monitor/output identity and carrier generation match the evidence recorded
  when the contract was resolved.

Any other presentation model, encoding, format/depth, color-management state,
or stale generation rejects the LUT while leaving ordinary playback available.

Target-specific display-mode authority is also mandatory:

- A Rec.709 cube requires a non-empty named calibrated display mode and
  `lut_display_mode_authority: manual_attested`.
- A BT.2020 cube requires `targetBt2020`, a final target-frame primaries value
  of `PL_COLOR_PRIM_BT_2020`, and one of two authorities:
  `manual_attested`, meaning the named projector BT.2020/native mode and RGB
  Full link were deliberately selected and externally verified; or
  `nvidia_external_verified`, meaning NVAPI SET/readback is combined with
  stored external analyzer evidence for RGB + BT.2020 + Full, 10 bpc, code
  preservation, delivery mode, and projector mode.
- The current `NvidiaBt2020Reporter::IsReadbackVerified()` proves only AVI
  colorimetry SET/readback. It does not prove physical-link RGB/range/depth,
  code preservation, direct delivery, or projector mode and must not map by
  itself to calibration authority. If signaling is required and NVAPI fails,
  VP rejects the LUT instead of continuing with signaling silently unavailable.

These gates harden the already implemented `004-beta` target/P709-carrier/R10
presenter path; they do not introduce another presenter seam. Promote that
existing path behind a production selector,
`output_presentation: calibrated_direct`, which selects the top-level VP-owned
R10 path and its guards. A production LUT profile must not require users to
enable the diagnostic-only `diagnostic_vp_owned_dxgi_presenter` switch.

The selected profile binds the cube hash and contract to an EDID hash,
DisplayConfig adapter LUID and target ID, output technology/connector path,
active width/height, refresh numerator/denominator, scanline/scaling state,
adapter LUID and driver version, named display mode, TRC, `Lb/Lw`, direct-link
attestation, and signaling authority. Unknown identity/evidence rejects the
LUT. Display/topology change, monitor crossing, hotplug, mode/refresh change,
device reset, swapchain generation, driver identity, or profile change advances
the calibration contract generation and re-resolves it. Public configuration
includes `lut_display_mode: <non-empty label>` alongside the authority,
direct-delivery, and external-color-management attestations.

### One atomic resolved contract

Resolve one immutable `calibrationContractGeneration` before attaching a cube.
It contains at least:

- cube canonical path, content signature/fingerprint, dimensions, and reload
  generation;
- role (`target_display_calibration`) and scope (`video_picture`);
- input primaries (`REC709` or `BT2020` for v1);
- input transfer (`sRGB`, pure `GAMMA22`, pure `GAMMA24`, or `BT1886` with
  Rec.709; pure `GAMMA22` or `BT1886` with BT.2020; other parsed target
  transfers remain unavailable until added to the tested matrix);
- input range (`FULL` for v1);
- reference white and black luminance in nits;
- resolved tone- and gamut-mapping policy;
- output/native profile label, display-mode authority, and installation
  requirements;
- presentation carrier identity/capability generation;
- presentation model/owner, DXGI encoding, swapchain format, sample/color
  depth, bit shift, effective display depth, monitor/output identity, detected
  Advanced Color state, and external-correction attestation; and
- activation state (`Disabled`, `Parsed`, `ContractRejected`, `Active`, or
  `RuntimeRejected`) plus a concise reason; and
- a separate optional reload-candidate identity, signature, status, and reason,
  so a rejected same-contract candidate does not replace the identity of the
  last-known-good active resource.

All existing profile inheritance and rule selection must finish before this
object is created. Changes to any field invalidate the object atomically.

`AUTO` never means wildcard. For each LUT-reference field it means “inherit the
final concrete value from the selected renderer target profile.” If no concrete
value exists, disable the cube with an actionable reason. The `.cube` file
cannot supply primaries, transfer, range, white, black, or role. For BT.1886,
both authoring white and black must ultimately come from explicit profile
values; an auto-derived black such as `white/1000` is not sufficient.
Pure `GAMMA22` and `GAMMA24` profiles likewise require explicit authoring
`Lw/Lb`; an inherited or auto-derived black cannot verify the declared
black-scaled power curve against the cube-authoring contract.

BT.2020 is a first-class SDR LUT-input gamut in v1. It does not imply PQ, HLG,
HDR passthrough, display coverage, or a transfer function. Rec.709 SDR and HDR
BT.2020/PQ/HLG sources are decoded and tone/gamut mapped as needed into the
declared SDR BT.2020 target, then encoded with the profile's explicit SDR
transfer before the cube. A cube authored to receive PQ or HLG, or to perform
external HDR tone mapping, is a different stage and is rejected.

The two accepted BT.2020 profile shapes are
`BT2020_D65_BT1886_FULL_R10` with explicit `Lb/Lw`, and
`BT2020_D65_G22_FULL_R10` only for a cube explicitly authored for pure gamma
2.2 with explicit `Lb/Lw`. They use BT.2020 primaries R `(0.708,0.292)`, G `(0.170,0.797)`, B
`(0.131,0.046)`, and D65 `(0.3127,0.3290)`. For the G22 profile the luminance
model is `L = Lb + (Lw - Lb) * V^2.2`; do not call gamma 2.2 a “BT.2020
transfer.”

P3-D65 remains deferred. It names only RGB chromaticities and D65 white; it
never implies sRGB, gamma 2.2, 2.4, 2.6, or another transfer. A later P3
extension must select and prove either a pixel-transparent device-native
carrier or a declared BT.2020/scRGB/Windows-managed transport whose cube output
contract is designed for that compositor. It cannot activate merely because a
target enum parses.

### Full-range-only production boundary

V1 accepts full-range Rec.709 or SDR BT.2020 encoded input coordinates:
normalized cube coordinate 0 is target code 0 and coordinate 1 is the maximum
target code. Its table outputs are normalized opaque native-drive values in
`[0,1]`; calling them “full-range Rec.709/BT.2020 output” would incorrectly
assign reference-space semantics after the device cube. Reject a LUT profile
when the resolved target input representation or transport is limited/studio,
while continuing safe ordinary playback without the cube.

Tetrahedral sample coordinates intentionally clamp to `[0,1]` in v1. Negative,
extended, and super-white target values therefore use edge texels rather than
extrapolation. This behavior must be explicit in diagnostics/documentation and
covered at `-epsilon`, `0`, `1`, and `1+epsilon`; v1 does not claim to preserve
an extended-range calibration domain through the cube.

Limited-range support is a future bounded extension. It must either normalize
nominal picture codes before the cube and restore storage codes after it, or
define and validate an explicitly named storage-code-authored cube contract.
It must not call a `64/1023...940/1023` storage-domain cube a generic BT.1886
`0...1` cube.

For a true BT.1886 profile, use the standard black-aware function over
normalized picture value `V` with explicit authoring `Lw` and `Lb`. When
`Lb=0`, it reduces to pure gamma 2.4. Diagnostics and documentation must not
use “BT.1886” as shorthand for every gamma-2.4 cube.

Pinned libplacebo uses zero `hdr.min_luma` as an unknown/default-black
sentinel. When the resolved semantic `Lb` is exactly zero, retain and report
`0` in the VP contract but write `PL_COLOR_HDR_BLACK` into the libplacebo
target frame. A literal zero must not silently become libplacebo's default
1000:1 black assumption.

### Strict `.cube` v1 subset

Before calling libplacebo, validate and canonicalize a documented Adobe Cube
subset:

- file size at most 64 MiB, UTF-8 (an optional UTF-8 BOM is stripped) or 7-bit
  ASCII text, with UTF-16, embedded NUL, and invalid UTF-8 rejected;
- optional blank lines and `#` comments, plus at most one optional quoted
  `TITLE` before the table; unknown or duplicate directives are rejected;
- 3D only, exactly one `LUT_3D_SIZE`, cubic dimensions 2 through 128, and the
  parsed `N^3` allocation must remain within the existing resource bound;
- exactly `N^3` RGB rows in Adobe R-fastest ordering;
- finite numeric values only, with all table outputs in `[0,1]` for v1;
- zero or one `DOMAIN_MIN` and zero or one `DOMAIN_MAX`; if present their values
  must be exactly `0 0 0` and `1 1 1` respectively;
- decimal, sign, and `e`/`E` exponent forms accepted only where the VP validator
  can canonicalize them unambiguously;
- no combined 1D shaper, extra rows, missing rows, extrapolation contract, or
  non-default domain; and
- tetrahedral interpolation with input coordinates clamped to `[0,1]`.

Reject unsupported syntax or semantics with a specific reason. Do not allow
libplacebo's permissive/narrow parser behavior to silently reinterpret a file.
An accepted cube's first RGB row must map `(0,0,0)` to exact `0.0` in all three
channels after canonical numeric parsing. This keeps an R10 code-zero picture
black aligned with bypassed code-zero borders.

### Resource and failure lifecycle

- On path, file identity, content, profile, output, device, HDR/Advanced Color,
  ICC-compatibility, or carrier-generation change, parse and validate a
  complete candidate off-side before changing the active attachment.
- Reload is explicit or profile/configuration-triggered, not per-frame polling.
  Reapplying configuration must detect a same-path content replacement. A
  permanent stale path cache is not acceptable.
- If only the file content changed under the same resolved contract and the
  replacement fails, retain the last-known-good LUT as
  `Active` and report its signature alongside the separately rejected
  candidate. If the output/profile/monitor contract changed, validate the
  candidate off-side first, then at the frame-boundary commit detach the old
  attachment before committing the changed contract. A valid candidate becomes
  active in that commit; an invalid candidate commits a no-LUT state. Do not
  discard last-known-good merely to attempt file I/O or parsing.
- Runtime render failure changes the state to `RuntimeRejected`, detaches and
  frees the failing LUT safely, restores ordinary render/dither parameters,
  and retries without a cube on the next eligible frame without a renderer
  reset loop. A same-frame second render is not implied.
- Device recreation and teardown free each resource once. libplacebo's content
  signature may invalidate dependent GPU resources; a full renderer or
  presenter reset is not required solely for a valid cube change.

## Implementation plan

1. Introduce a resolved target-calibration contract/value object and pure
   resolver/validator functions. Separate target-match, the exact
   `CarrierEvidence` predicate, and state decisions. Preserve
   explicit-versus-inherited white/black intent, including the semantic-zero
   `PL_COLOR_HDR_BLACK` adapter.
2. Enforce the initial supported matrix—Rec.709 or SDR BT.2020, full range, and
   the explicitly supported transfers—at one resolver boundary. Remove or mark
   unavailable combinations that cannot activate, including P3, limited range,
   PQ/HLG target cubes, and external HDR tone-map LUTs.
3. Add explicit LUT reference black configuration and surface all resolved
   fields through profile/rule inheritance, live configuration, inventory,
   debug log, and Ctrl+I/status OSD.
4. Add the strict cube pre-validator/canonicalizer, detailed rejection codes,
   neutral-black check, content fingerprint, and atomic candidate swap.
5. Refactor renderer LUT state so parsing alone cannot alter dither. Attach
   only an `Active` contract, and restore the selected normal render parameters
   on contract or runtime rejection.
6. Invalidate/re-resolve on every relevant profile/output/device generation
   and on same-path content replacement when configuration is reapplied.
7. Update the sample configuration, configuration UI/inventory, and
   `CONFIGURATION.html`. Clearly label v1 as full-range, video-picture-only,
   target/display calibration and document required OS/display state.
8. Add the independent CPU/GPU/lifecycle validation below, then complete an
   x64 Release build. Real-display measurement is final acceptance evidence,
   not a reason to redesign the established backbuffer path.

## Validation plan

### Deterministic automated proof

- Build an independent double-precision tetrahedral evaluator; do not use
  libplacebo as both implementation and oracle.
- Test identity and asymmetric channel/order cubes, all six tetrahedral
  regions, vertices, edges, neutral ramps, and `-epsilon`/`1+epsilon` clamp.
- With dither disabled, compare WARP/GPU R10 output with the independent oracle
  within one code per channel, with exact black and white endpoints where the
  fixture requires them.
- Probe full-range codes `0`, `1`, `64`, `512`, `940`, `1022`, and `1023` and
  prove that a limited/studio LUT profile is rejected.
- Add BT.1886 CPU reference vectors for `Lb/Lw = 0/100` and `0.005/100` at
  `V = 0, 0.018, 0.18, 0.5, 1`, with error no greater than `1e-6` normalized or
  `1e-5` relative.
- Add pure-G22 CPU vectors for the same `Lb/Lw` and `V` sets using
  `L = Lb + (Lw - Lb) * V^2.2`, plus a target-boundary GPU probe. Do not assume
  pinned libplacebo's nonzero-black gamma behavior without this proof.
- Prove target primaries/transfer/range/white/black and gamut/tone policy at
  the LUT boundary for accepted Rec.709 and SDR BT.2020 profiles, and prove
  P3-D65 and PQ/HLG target-cube profiles fail closed before attachment.
- Prove Rec.709 source -> SDR BT.2020 target -> cube and HDR BT.2020/PQ/HLG
  source -> tone/gamut-mapped SDR BT.2020 target -> cube. A BT.2020-input cube
  must receive BT.2020 RGB coordinates, not Rec.709 RGB or unconverted PQ/HLG.
- For independent linear-light conversion proof, test the Rec.709 basis vectors
  against the Rec.709-to-BT.2020 matrix
  `[0.627403896,0.329283038,0.043313066;`
  `0.069097289,0.919540395,0.011362316;`
  `0.016391439,0.088013308,0.895595253]` within `5e-6`, and prove neutrals
  remain neutral before target transfer encoding.
- Use a seam-probe cube that maps black to an obvious tint in test-only code to
  prove the video picture changes while final overlays and outer borders do
  not. Production acceptance still requires the neutral-zero black rule.
- Cover default-domain acceptance and every strict-subset rejection, including
  non-default domain, NaN/Inf, extra/missing rows, mixed 1D+3D, out-of-range
  table values, unsafe dimensions, and syntax canonicalization.
- Cover cross-profile switches, Advanced Color/HDR detection and external-
  correction attestation states, carrier and device generation changes,
  same-path replacement,
  invalid replacement, runtime render failure, no-LUT retry, error-diffusion
  disable/restore, teardown, and device recreation. Prove the guard; a warning
  in documentation alone is not acceptance.
- Cover Rec.709/BT.2020 profile switching, top-level VP-owned R10 flip versus
  child/composed rejection, direct-delivery attestation, manual display-mode
  attestation, NVIDIA SET/readback-only, analyzer-backed signaling, signaling
  failure, and restoration on exit.
- Make known exporter fixtures mandatory test assets or explicit named test
  inputs; a silently skipped environment-variable test is not acceptance.

### Calibration and real-output proof

- Measure with VP OSD/statistics/diagnostic overlays disabled and record which
  subtitle path is active.
- Record cube provenance/exporter, size, checksum, authoring primaries,
  transfer, full range, `Lw`, `Lb`, display/native mode, OS HDR/Advanced Color,
  ICC compatibility, ICC/MHC/VCGT state, GPU format/range, display bit depth,
  and VP resolved contract.
- Measure black/near-black, white/near-white, neutral tracking, primary and
  secondary colors, color-checker or equivalent patches, and at least one
  gamut-stress set. Compare no-LUT, identity, and calibration cubes.
- Use authoritative backbuffer/readback evidence to prove VP's stage and code
  output, then an independent probe/capture/measurement path to validate the
  actual display result. Do not infer calibration correctness from visual
  program material or backbuffer readback alone.
- Record tolerances before measuring. Final acceptance requires no unexplained
  systematic range remap, double transfer, double gamut transform, clipping,
  channel swap, stale cube, or calibrated-picture/border black discontinuity.
- For BT.2020, independently verify RGB Full link encoding plus the projector's
  actual BT.2020/native mode or HDMI BT.2020 signaling. A P709 DXGI carrier
  label, NVAPI SET result, or renderer target log alone is insufficient.
- HDMI analyzer/capture evidence records RGB, 10 bpc, full range, BT.2020 AVI
  colorimetry, code preservation, and SDR rather than HDR/PQ display mode.
  Meter evidence records D65, grayscale/EOTF, ColorChecker-class patches,
  saturation sweeps, and high-saturation gradients through the exact path.

## Acceptance criteria

- Every active cube has one logged, OSD-visible, fully resolved input contract:
  role/scope, file/signature, primaries, transfer, full range, white, black,
  tone/gamut policy, native profile, exact carrier identity/model/encoding,
  format/effective depth, Advanced Color state, and operator attestation.
- `AUTO` fields resolve deterministically and are never contract wildcards.
- Rec.709 and SDR BT.2020 LUT-input profiles activate only under their exact
  full-range target, transfer, luminance, carrier, and display-mode contracts.
  BT.2020 is not inferred from source mastering metadata and never implies PQ.
- BT.2020 diagnostics separately report `LUT input=FULL BT2020/<TRC>`,
  `DXGI carrier=P709`, `HDMI/display mode=BT2020`, and the authority/evidence
  for each. Post-LUT native-drive values are not mislabeled semantic BT.2020.
- P3-D65 and PQ/HLG target-cube profiles are explicitly unavailable and fail
  closed before attachment.
- A BT.1886 cube activates only with explicit matching authoring white/black;
  pure gamma 2.4 remains a separately named contract.
- Limited/studio LUT profiles and unsupported cube semantics fail closed while
  ordinary playback continues with an actionable reason.
- The accepted cube operates exactly once at target/native video-picture
  stage, followed by ordinary final video dither and no unintended color
  transform.
- Parsed/rejected/runtime-failed cubes cannot leave incompatible dither state,
  stale LUT attachment/reference, repeated render failures, or misleading
  active status. Libplacebo may legitimately retain unreachable cached objects.
- Same-path replacement is detected on configuration reapply. A failed
  same-contract replacement leaves the prior signature visibly active and the
  candidate visibly rejected; a changed-contract failure cannot retain or
  masquerade as the prior calibration.
- Deterministic parser, contract, CPU oracle, GPU readback, lifecycle, x64
  Release build, and recorded real-display validation all pass.
- Existing no-LUT playback and supported VP-0011 safe fallback remain intact.

## Non-goals

- No presenter/backbuffer redesign, arbitrary shader/LUT chain, creative look
  LUT, source normalization LUT, main/conversion LUT, ICC profile generator,
  1D shaper, or whole-frame LUT in this story.
- No P3-D65, PQ/HLG target cube, external HDR tone-map LUT, or limited/studio-
  range LUT activation until each separate target/carrier, stage, or
  domain/remap contract is approved and proven.
- No PC-madVR `.3dlut` import or claim of PC-madVR video-level file/range
  compatibility; VP v1 accepts the documented full-range `.cube` contract.
- No claim that MPC-family precedent, a DXGI label, GPU “Full” setting, or
  backbuffer readback alone proves physical-display calibration.
- No automatic projector/display-mode control and no overwrite of deployed
  user configuration or user LUT files.
- No libplacebo upgrade unless a separate dependency story approves it.

## Dependencies and disposition

- VP-0011 and VP-0012 remain the completed loader/placement foundation.
- VP-0096 supplies accepted source/range conversion groundwork.
- VP-0125, VP-0127, and VP-0133 contain the relevant presenter, delivery, and
  authoritative backbuffer evidence; this story does not repeat their design.
- VP-0100 and VP-0101 are superseded and moved to Will Not Do with this story.
- VP-0029, VP-0047, and VP-0048 remain historical inputs and are not separate
  implementation work.
- Coordinate with VP-0051 for any future generic shader-chain stage, VP-0109
  for pure-2.2 studio transport, VP-0128 for option inventory, and VP-0141 for
  live configuration reapplication. None changes the full-range v1 boundary.

## Comparative and standards references

- [mpv target-LUT option](https://github.com/mpv-player/mpv/blob/e8673660ab7ee5d4ea8f93e4bf3a6e170ab2a19a/DOCS/man/options.rst#L7604-L7608)
- [madshi on madVR LUT input-gamut preconversion](https://forum.doom9.org/archive-perm/index.php/t-146228-p-1117.html)
- [madshi on the PC madVR video-level `.3dlut` boundary](https://forum.doom9.org/archive/index.php/t-146228-p-390.html)
- [ArgyllCMS madVR calibration workflow](https://www.argyllcms.com/doc/Scenarios.html)
- [Pinned VP libplacebo target encoding and native LUT order](https://github.com/billslack2/libplacebo/blob/c3a3d203/src/renderer.c#L2800-L2825)
- [Pinned VP libplacebo cube parser](https://github.com/billslack2/libplacebo/blob/c3a3d203/src/shaders/lut.c#L30-L145)
- [Pinned VP libplacebo dither and overlay order](https://github.com/billslack2/libplacebo/blob/c3a3d203/src/renderer.c#L2945-L3015)
- [MPC-BE ICC LUT construction](https://github.com/Aleksoid1978/MPC-BE/blob/6c07f2cec684ae3a4433b1391a224a053e5803d4/src/filters/renderer/VideoRenderers/DX9RenderingEngine.cpp#L1699-L1875)
- [MPC-BE final LUT/dither shader](https://github.com/Aleksoid1978/MPC-BE/blob/6c07f2cec684ae3a4433b1391a224a053e5803d4/src/Shaders/Transformation/final_pass.hlsl#L17-L37)
- ITU-R BT.1886, Reference electro-optical transfer function for flat panel
  displays used in HDTV studio production.
- [ITU-R BT.2020](https://www.itu.int/rec/r-rec-bt.2020/en)
- Adobe Cube LUT Specification, version 1.0.
- Microsoft DXGI color-space and Windows Advanced Color/MHC documentation.
- [Microsoft Advanced Color and ICC profile behavior](https://learn.microsoft.com/windows/win32/wcs/advanced-color-icc-profiles)
- [Microsoft HDR/Advanced Color swapchain guidance](https://learn.microsoft.com/windows/win32/direct3darticles/high-dynamic-range)

## Source references

- `src\VideoProcessor-Lib\vprenderer\LibplaceboDisplayLut.cpp`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboDisplayLut.h`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboVideoRenderer.cpp`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboVideoRenderer.h`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboOutputPolicy.cpp`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboRenderParameters.h`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboRenderParameters.cpp`
- `src\VideoProcessor-Lib\RendererProfileConfig.h`
- `src\VideoProcessor-Config\ConfigEditorWindow.cpp`
- `src\VideoProcessor-Test\LibplaceboLutParserTests.cpp`
- `src\VideoProcessor-Test\LibplaceboOutputPolicyTests.cpp`
- `src\VideoProcessor-Test\LibplaceboRenderParametersTests.cpp`
- `src\VideoProcessor-ConfigTests\ConfigEditorWindowTests.cpp`
- `src\VideoProcessor-VPRenderer\VideoProcessor-VPRenderer.vcxproj`
- `src\VideoProcessor-Test\VideoProcessor-Test.vcxproj.vcxproj`
- `3rdparty\libplacebo\README.txt`
- `CONFIGURATION.html`
