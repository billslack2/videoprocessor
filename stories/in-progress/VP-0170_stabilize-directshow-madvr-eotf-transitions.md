# VP-0170: Stabilize DirectShow/madVR EOTF transitions

## Status

Backlog. Created 2026-09-02 from a user report against `v1.3.004-beta-e9ed97c2`.

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
