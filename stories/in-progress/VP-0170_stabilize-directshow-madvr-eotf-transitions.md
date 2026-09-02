# VP-0170: Stabilize DirectShow/madVR EOTF transitions

## Status

In Progress. Implementation is active on branch
`codex/vp-0170-eotf-transitions` in clean worktree
`C:\Videoprocessor\vp\vprenderer\.codex-worktrees\vp-0170-eotf-transitions`,
based on remote `v1.3.005-beta` tip `1d5c731b` (verified 2026-09-02).

Initial implementation consolidates capture-event and periodic EOTF detection
behind one graph-independent stabilization policy. A candidate requires two
matching valid observations over a documented 5000 ms settling window, cancels
when the signal returns to the renderer baseline, and defers its sole renderer
rebuild commit while another renderer/display reset is active. The prior
periodic full-capture restart path has been removed.

Source commit `dfcf1eac` is pushed on the feature branch. Validation so far:
x64 Release `VideoProcessor-Test` and `VideoProcessor-GUI` targets build
successfully; all five focused `EotfTransitionStabilizerTests` pass. The full
suite passes 1042/1044 tests. Its two failures,
`ConfigurationReferenceMatchesPublicFieldInventory` and
`ConfigurationApplyPolicyGroupsOnlyOrderedProfiles`, reproduce unchanged on
the untouched `v1.3.005-beta` baseline and are unrelated to VP-0170. Hardware
acceptance remains outstanding.

### Readiness review (2026-09-02)

A canonical x64 Release package was built from source commit `dfcf1eac` at
`artifacts\release\VideoProcessor-v1.3.005-beta-vp0170-dfcf1eac.zip`. The
archive SHA-256 is
`C2A576A34249272E875CDD02D2CE7E673175897C6D3B969D830AD4C448047B29`
and its size is 30,457,305 bytes. Packaging verification found an exact
57-file match with the canonical staged manifest, one `VideoProcessor/`
top-level directory, `VideoProcessor.cfg.example` but no active
`VideoProcessor.cfg`, and no PDB, import-library, export, incremental-link,
state, cache, or backup payloads. The staged executable SHA-256 is
`54AADEE2A1E565E4C739378C49E6F65BD34DEA794462C035860900713C72BCB3`;
the staged renderer DLL SHA-256 is
`EE4D0474F8A8770DD875D5962405FF115AE5C1D6FBC8B8985071D3408FE5B530`.
All five focused transition tests pass against the final x64 Release build.
The package has not been deployed; hardware acceptance remains outstanding.

- Configuration: no new setting is needed; the existing automatic EOTF restart
  behavior remains enabled and its stabilization contract is made explicit.
- API and pipeline: DirectShow renderer media types are immutable per renderer
  generation; capture video-state notifications and the one-second periodic UI
  timer are the two observation sources, while the existing renderer-reset
  coordinator continues to own display-transition queue re-primes.
- Resource lifetime: the policy owns values and monotonic timestamps only. It
  does not own MFC timers, renderers, capture devices, windows, or graph objects.
  Renderer stop clears the pending candidate and renderer start establishes the
  new active baseline.
- Dependencies/platform: implementation uses existing MFC timers and
  `GetTickCount64`; no new runtime or hardware dependency is introduced.
- Validation boundary: deterministic tests cover stable commit, flip-back
  cancellation, event/periodic coalescing, invalid observations, and deferral
  through a madVR-style display transition. x64 Release compilation proves host
  integration; DeckLink/madVR hardware replay remains the acceptance boundary.
- Worktree: clean branch was created from the freshly fetched authoritative
  remote beta tip; existing local checkouts were not used as a baseline.

## User story

As a DirectShow/madVR user, I want VP to wait for a capture EOTF transition to stabilize before rebuilding the graph, so a transient PQ/SDR report during startup does not cause repeated black screens, HDMI re-syncs, or a renderer that remains black until dismissed.

## Evidence

The affected session used `DirectShow - madVR`, not VP Renderer. No libplacebo refresh-rate candidate-selection path ran.

The known-good older session had one capture start and one BlackMagic HDMI format-change/resync event. The affected session had four capture starts, four DirectShow/madVR renderer generations, four HDMI re-sync events, and three EOTF changes in roughly 40 seconds:

- `PQ (ST2084) -> SDR`
- `SDR -> PQ (ST2084)`
- `PQ (ST2084) -> SDR`

For each transition VP scheduled or performed a capture/renderer restart. madVR's move into exclusive approximately 23.976 Hz presentation also generated Windows display-change notifications, which VP observed and used to schedule additional graph/queue resets. The combined lifecycle churn matches the visible black/video cycling.

## Scope

1. Make EOTF-triggered restarts for non-dynamic renderers stable and coalesced. Require a documented settling window with repeated matching valid observations before restarting DirectShow/madVR.
2. Cancel a pending EOTF restart when the signal returns to the renderer's active EOTF before the settling window expires.
3. Ensure the capture-video-state and periodic EOTF checks share one pending transition/restart state; they must not independently schedule restarts.
4. Keep handling genuine persistent SDR/HDR transitions correctly, including capture metadata and renderer media-type changes.
5. Preserve madVR's own display-mode switching. VP must observe its display transition without issuing a competing refresh change or amplifying it into redundant graph restarts.
6. Add compact diagnostics for candidate, confirmation, cancellation, coalescing, and committed EOTF transitions.

## Non-goals

- Do not change VP Renderer DXGI refresh-mode selection (VP-0167).
- Do not disable madVR display-mode switching or exclusive presentation.
- Do not hide a genuine loss of capture signal.

## Acceptance criteria

- A transient PQ/SDR/PQ sequence during startup results in no more than one committed renderer restart after the final stable state is known.
- A stable SDR-to-PQ or PQ-to-SDR input transition still rebuilds DirectShow/madVR once when required.
- A cancelled transient transition leaves playback active without a black screen or capture restart.
- madVR refresh switching can produce display-change notifications without causing repeated VP graph resets.
- Logs identify which EOTF observation was confirmed, cancelled, coalesced, or committed.
- Targeted tests cover stable transition, flip-back cancellation, duplicate event/periodic detection, and madVR display-transition interaction.

## Validation

Reproduce with the DeckLink Quad HDMI Recorder and a 23.976 source that emits transient PQ/SDR state during startup. Compare capture-start count, renderer-generation count, HDMI-resync count, and black-screen duration with the reported `v1.3.004-beta-e9ed97c2` session.
