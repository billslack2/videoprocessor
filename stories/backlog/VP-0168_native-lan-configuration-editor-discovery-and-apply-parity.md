# VP-0168: Native LAN configuration editor discovery and apply parity

## Status

Backlog (2026-09-01). Proposed from the operator requirement to use the same
native `VideoProcessorConfig.exe` UI locally or from a laptop to configure a
running VideoProcessor instance on the same LAN.

## User story

As a VideoProcessor operator, I want the native configuration editor running
on my laptop to discover a running VP instance on the local LAN and edit that
instance's configuration, so I can use the exact familiar Qt UI while seeing
the real display update when I choose **Apply** or **OK**.

## Required behavior

1. A running `VideoProcessor.exe` exposes a small LAN control endpoint for
   configuration only. The endpoint is enabled/configured for the private
   local network and is not an Internet-facing service.
2. `VideoProcessorConfig.exe` supports selecting a local or discovered remote
   VP instance without creating a second configuration UI. The existing Qt
   editor pages, controls, validation presentation, **Apply**, **OK**, and
   **Cancel** semantics remain the only editor UI.
3. The editor obtains the selected instance's editable effective configuration
   through `GetConfig` and keeps edits pending in its normal local document.
   No change is sent while fields are being edited.
4. **Apply** and **OK** submit the complete candidate configuration through
   `ApplyConfig`. **Cancel** submits nothing. **OK** closes only after a
   successful remote apply.
5. The target VP instance is the sole authority for persistence and runtime
   behavior. On a successful `ApplyConfig`, it invokes the same existing
   save, reload, apply, reset, and renderer-restart decision path as a local
   configuration change. The laptop must never write the target's config file
   directly or independently decide which changes require a reset/restart.
6. Local editing uses that same API against the local instance (for example,
   loopback), so local and remote Apply requests exercise one server-side
   implementation and have exact behavior parity.
7. Discovery uses a fixed-port UDP LAN query/reply protocol: the editor sends
   a discovery request and each current VP instance replies with a stable
   instance ID, friendly machine/display name, VP version, and RPC endpoint.
   The picker also permits a manual host/address for cases where broadcasts
   are unavailable.
8. The apply response reports success or a user-facing validation/save error,
   plus the relevant outcome (applied live, reset performed, renderer restarted,
   or restart still required) so the existing status area can present truth.

## Scope

- Define a compact versioned request/response contract containing only
  `GetConfig`, `ApplyConfig`, and UDP discovery data.
- Add the target-side endpoint and marshal its apply operation safely onto the
  existing VP configuration/runtime ownership path.
- Add instance selection/discovery and a local-versus-remote transport adapter
  to the existing native Qt editor while retaining one UI codebase.
- Preserve existing standalone, local configuration-editor behavior when no
  remote instance is selected.
- Add focused unit/integration coverage for request validation, discovery
  parsing, local-loopback parity, remote Apply/OK/Cancel behavior, and each
  reported runtime outcome.

## Non-goals

- A browser, WebAssembly, HTML, mobile, cloud, account, certificate, or
  Internet-access configuration UI.
- Continuous per-keystroke or slider-preview updates; requests occur only on
  **Apply** or **OK**.
- General remote control of playback, capture, operating-system functions, or
  arbitrary file access.
- Discovery across routed subnets/VLANs; manual address entry is sufficient
  outside the local broadcast domain.

## Acceptance criteria

- Starting the same native config executable locally opens the established Qt
  configuration UI and applies changes through the shared target-side API.
- Starting it on a laptop discovers every responding VP instance on the same
  LAN subnet and displays a clear selection identity; a manually entered host
  can also be selected.
- With a remote instance selected, the UI loads that instance's configuration;
  editing controls changes neither its file nor its runtime state until
  **Apply** or **OK** is selected.
- **Apply** updates the selected target and leaves the editor open; **OK**
  performs the same successful operation and closes; **Cancel** produces no
  target request or configuration change.
- For representative changes that the current local path applies live, resets,
  and restarts, a remote Apply produces the same target-side behavior and
  status outcome as local Apply.
- Invalid candidates, save failures, and unavailable targets leave the prior
  target configuration intact and keep the editor open with an actionable
  error.
- The x64 Release application and configuration-editor builds succeed, and
  focused automated coverage passes without changing the active deployment
  configuration during tests.

## Dependencies and readiness

- VP-0103 established the existing safe apply-to-running-VP path and is the
  behavior that must be reused rather than replicated.
- VP-0097 established the standalone native Qt configuration editor.
- Before implementation, perform the required readiness review against the
  current configuration model and trace the exact local Apply call chain,
  runtime-thread ownership, persistence conflict behavior, and restart
  reporting contract.
