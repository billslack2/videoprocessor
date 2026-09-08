# VP-0168: Native LAN configuration editor discovery and apply parity

## Status

Backlog (2026-09-01). Proposed from the operator requirement to use the same
native `VideoProcessorConfig.exe` UI locally or from a laptop to configure a
running VideoProcessor instance on the same LAN.

2026-09-08: Scope refined to the existing Windows config executable, three
RPC operations, local loopback from the first implementation, and tray-based
selection with a remembered target and a ten-instance discovery limit.

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
3. The editor obtains the selected instance's editable configuration document
   through `GetConfig` and keeps edits pending in its normal local document.
   Preserve comments, unknown settings, profile order, and inheritance instead
   of flattening effective values. No change is sent while fields are edited.
   A separate `GetCapabilities` returns the target's capture devices and
   connections, monitors, renderers, and available LUTs; never populate remote
   choices from the client computer's hardware or files.
4. **Apply** and **OK** submit the complete candidate configuration through
   `ApplyConfig`. **Cancel** submits nothing. **OK** closes only after a
   successful remote apply.
5. The target VP instance is the sole authority for persistence and runtime
   behavior. On a successful `ApplyConfig`, it invokes the same existing
   save, reload, apply, reset, and renderer-restart decision path as a local
   configuration change. The laptop must never write the target's config file
   directly or independently decide which changes require a reset/restart.
6. From the first implementation, editing a running local instance uses the
   same RPC client and API against loopback. Only the selected endpoint changes
   between local and remote use. Both exercise the same serialization,
   validation, persistence, and runtime apply implementation. Existing local
   functionality and Apply/OK behavior must remain equivalent to today.
7. Discovery uses a fixed-port UDP LAN query/reply protocol: the editor sends
   a discovery request and each current VP instance replies with a stable
   instance ID, friendly machine/display name, VP version, and RPC endpoint.
   The picker also permits a manual host/address for cases where broadcasts
   are unavailable.
8. The apply response reports success or a user-facing validation/save error
   and the accepted runtime action. Distinguish an accepted/requested reset or
   restart from a completed one; the current local path requests transitions
   without waiting for renderer completion. Reuse its capture-restart behavior
   as well as renderer restart, reset, and live update behavior.
9. The tray's open action reads `Open Configuration (<name>)`, using the remote
   computer name or `LOCAL` for an instance on this computer. Provide target
   selection in the tray when more than one instance is available, and make
   the selected target clear in the editor as well.
10. Remember the last explicitly selected target in this client's preferences
    and use it by default on subsequent opens and client restarts. With no
    prior selection, default to the local instance. If the remembered target
    is unavailable, show that state and allow another selection; do not silently
    redirect pending edits to a different host. Resolve unsaved edits before
    changing targets using the existing save/discard interaction.
11. Treat discovery results as VP instances, not just computer names or version
    strings. Multiple versions/installations may run on one computer. Retain
    stable instance identity and endpoint, and show version plus an instance
    qualifier when needed to disambiguate otherwise identical names (including
    multiple `LOCAL` entries).
12. Stop each discovery scan once ten distinct valid VP instances have been
    collected, counting local and remote instances together. Deduplicate
    replies before counting, ignore further results for that scan, and display
    at most ten discovered instances. With fewer than ten, finish after a
    bounded timeout. A refresh starts a new bounded scan; do not collect an
    unlimited list and merely truncate its presentation.

## Scope

- Define a compact versioned request/response contract containing only
  `GetConfig`, `GetCapabilities`, `ApplyConfig`, and UDP discovery data.
- Add the target-side endpoint and marshal its apply operation safely onto the
  existing VP configuration/runtime ownership path.
- Add instance selection/discovery and a local-versus-remote transport adapter
  to the existing native Qt editor while retaining one UI codebase.
- Preserve existing standalone, local configuration-editor behavior when no
  VP instance is running through an explicit offline file-editing mode. All
  editing of running instances uses RPC, including local editing.
- First delivery targets two Windows computers using the existing config EXE
  and its packaged dependencies; no new client application or platform port.
- Auxiliary operations that currently open local folders/logs or clear shader
  caches must not accidentally operate on the laptop in remote mode. Identify
  these during implementation and disable unsupported remote operations with
  a clear explanation for the initial delivery.
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
- Starting it on a laptop discovers responding VP instances on the same LAN
  subnet up to the ten-instance limit; a manually entered host can also be
  selected.
- Tray actions use `Open Configuration (LOCAL)` or
  `Open Configuration (<computer name>)` as appropriate. Multiple instances
  on the same machine remain distinguishable and independently selectable.
- The last explicitly selected target is restored after reopening/restarting
  the client; an unavailable target does not cause silent fallback or writes
  to another computer.
- Duplicate discovery replies do not consume slots. Ten distinct instances
  end the scan; an eleventh is not added. Scans with fewer results time out.
- Device, display, renderer, and LUT choices come from the selected target via
  `GetCapabilities`, including when the client's installed hardware differs.
- Local loopback and a second Windows computer both exercise all three RPC
  operations through the same client/server implementation. Verify document
  round-trip preservation and target isolation with multiple VP instances.
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
