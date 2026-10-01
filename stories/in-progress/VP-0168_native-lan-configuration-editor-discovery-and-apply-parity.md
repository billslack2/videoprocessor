# VP-0168: Native LAN configuration editor discovery and apply parity

## Status

In progress (2026-09-30). Implementation started from the current
v1.3.005-beta integration tip. The readiness review is tracing the existing
editor safe-save and VP asynchronous reload path before the RPC boundary is
introduced.

Originally proposed (2026-09-01) from the operator requirement to use the same
native `VideoProcessorConfig.exe` UI locally or from a laptop to configure a
running VideoProcessor instance on the same LAN.

2026-09-08: Scope refined to the existing Windows config executable, three
RPC operations, local loopback from the first implementation, and tray-based
selection with a remembered target and a ten-instance discovery limit across
the network. Discovery must find both the local and remote running VP systems;
multiple versions across the LAN are the concern, not a same-computer feature.

2026-10-01: Local configuration must remain editable and safely savable while
VP is stopped. `LOCAL` opens the local configuration file immediately; LAN
discovery runs alongside it and offers remote targets. Remote Apply/OK remains
RPC through the target VP. Keep document editing and the wire contract separate
from Windows-specific discovery and UI code so a future web or Android client
can implement its own transport and presentation layer.
Source commit `bbe152fc` on `codex/vp-0168-config-rpc` implements the local
file startup and save path; the x64 Release Config and test projects build, and
focused local-save, target-selector, RPC-to-local recovery, and Apply/OK/Cancel
tests pass. A physical two-computer LAN test remains outstanding.

2026-10-01: Default to This computer when a local VP installation exists.
Remember the last selected target, including unavailable remotes; show the
unavailable overlay until that same instance reconnects or the user chooses
another target. Scan the LAN in the background and require explicit selection
of new remotes. Source commit cb245b3f implements this behavior. The x64
Release Config build and focused tests pass; deployment and a physical
two-computer test remain outstanding.

2026-10-01: Deployed the x64 Release Config executable and discovery DLL from
cb245b3f to C:\Videoprocessor\vp\config after backing up the four replaced
files under config\backup-VP0168-20261001-102033. The active VP configuration
file was not edited. The deployed executable SHA-256 is
7952F5AB4DBBB7E120CBF1CF00C03B95E3860A74B403BB22CB53C67D208ACA23;
the discovery DLL SHA-256 is
7EF8F82111F25080DFCAE7C2BD5C5AEE2A45BD65B71F0481EC0883EFD8DA3755.
The Config-only x64 ZIP is
C:\Users\bslac\Documents\ChatGPT\Done\VideoProcessorConfig-VP0168-cb245b3f3c6e-x64.zip
(SHA-256 7325A1E7826AA3860E49E942D4205ACC622926347A30BDD8F3FA7704B1E5F43E).
All 36 packaged files verified after extraction, the portable EXE launched,
and a deployed screenshot showed This computer selected immediately. A
physical two-computer LAN and Apply/OK test remains outstanding.

2026-10-01: Corrected the header target selector and refresh control to match
the small Configure VP caption. Source commit 4ea82570 passed the focused
selector test and an x64 Release Config build; a preview screenshot was
reviewed. The updated Config-only portable ZIP is
C:\Users\bslac\Documents\ChatGPT\Done\VideoProcessorConfig-VP0168-4ea825704526-x64.zip
(SHA-256 09FC2B5EDC6DD49D5F4CAD38877C17275EED01C8A965C041D1F9C3470367F7D4).
All 36 files verified after extraction. After Config exited, the x64 Release
executable and discovery DLL were deployed to C:\Videoprocessor\vp\config.
The four replaced files were backed up under
config\backup-VP0168-compact-20261001-124527. The active VP configuration
file was not edited. The installed executable SHA-256 is
25ACC59C74927F24016806DD432EF547DA50FDD6BFCC3A522AAEC80C509774EE
and the installed discovery DLL SHA-256 is
7EF8F82111F25080DFCAE7C2BD5C5AEE2A45BD65B71F0481EC0883EFD8DA3755.
A launch from the deployed folder showed the compact target controls and
This computer selected. Physical two-computer testing remains outstanding.

2026-10-01: Fixed the compact selector's clipped popup and right border in
source commit 3a05e43c. The two-choice and four-choice popup tests pass,
including visibility of the final menu choice and the selector border. The
x64 Release Config executable was deployed after Config exited; the four
replaced files were backed up under
C:\Videoprocessor\vp\config\backup-VP0168-popup-20261001-125540.
The installed executable SHA-256 is
E67598B95F62552BF081545EA7436D4EC21A299FFE6BCFF6B8F141390D9D53CC.
The refreshed Config-only ZIP is
C:\Users\bslac\Documents\ChatGPT\Done\VideoProcessorConfig-VP0168-3a05e43c1d76-x64.zip
(SHA-256 CF350B854C1C3B7A4A778BB13266E0EC5D1CBC363362233DAFC0A6D0307FA797).
All 36 packaged files verified after extraction. The active VP configuration
file was not edited; physical two-computer testing remains outstanding.

2026-10-01: Source commit 16dcde9d36b0b1285f1b6b76294b72b8c837aaa4
rounds the target selector corners and moves local file/device discovery and
remote configuration/capability RPC loads to worker threads. A full-window
loading screen remains responsive during selection; a delayed loopback RPC
test verifies timer processing, window resizing, and successful target
loading. Focused selector, remote switch, offline local recovery, and
remembered-target tests pass. The full Config UI suite had one existing
Windows foreground-ordering failure, reproducible with the selector mask
disabled; other cases passed. Physical two-computer testing remains
outstanding. The x64 Release Config EXE and unchanged discovery DLL were
deployed after backing up the four replaced files under
C:\Videoprocessor\vp\config\backup-VP0168-responsive-20261001-133027.
The installed EXE SHA-256 is
4E5FEF869982FF9715F39908B98F779F6DC59C6DC1EDA283C50196E102E840E4;
the discovery DLL SHA-256 remains
7EF8F82111F25080DFCAE7C2BD5C5AEE2A45BD65B71F0481EC0883EFD8DA3755.
The Config-only portable ZIP is
C:\Users\bslac\Documents\ChatGPT\Done\VideoProcessorConfig-VP0168-16dcde9d-x64.zip
(SHA-256 F0436047CC4EB32EC524BD72EEA1F2B6B88784945F1EB0EF0FDA26BC31A9177D).
All 36 payload files verified after extraction; six Microsoft runtime DLL
signatures and the EXE x64 Release record verified. No active VP
configuration file was changed.
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
6. This computer is offered when a local VP installation is present next to
   the configuration file or a configuration path is explicitly supplied.
   It loads and saves the local file through the safe-save path, including
   while VP is stopped. When VP runs, local Apply/OK uses the existing
   change notification path. A selected remote target always uses RPC.

7. Every running VP host exposes a discovery responder for the lifetime of its
   RPC endpoint, independently of whether its config editor is open. Discovery
   uses a fixed-port UDP LAN query/reply protocol: the config editor broadcasts
   a discovery request on the local network and each running VP host advertises
   its availability by replying with a stable instance ID, computer name, VP
   version, protocol version, and reachable RPC endpoint. This advertisement
   is a response to discovery; periodic unsolicited announcements are not
   required. Include a loopback discovery query so the local running VP is
   discovered even when the network does not echo broadcasts to the sender.
   Deduplicate local and LAN replies for the same instance. Discover hosts
   without requiring users to enter their addresses or open remote config
   editors. Manual host/address entry remains a fallback when broadcasts are
   unavailable.
8. The apply response reports success or a user-facing validation/save error
   and the accepted runtime action. Distinguish an accepted/requested reset or
   restart from a completed one; the current local path requests transitions
   without waiting for renderer completion. Reuse its capture-restart behavior
   as well as renderer restart, reset, and live update behavior.
9. The editor and tray identify the local file choice as This computer.
   Remote targets use the computer name, with endpoint or version detail
   where needed. Discovery adds choices without selecting a new remote.

10. Remember the last explicitly selected target in this client's preferences.
    On first launch, select This computer when a local VP installation exists,
    even if VP is stopped. A remembered remote remains selected on later
    launches. If unavailable, show its last name and an unavailable overlay
    with editing disabled. Reconnect only to that same stable instance ID.
    The user may choose another target explicitly, including This computer.
    Resolve unsaved edits before changing targets.

11. Discover and distinguish running VP instances across the same network,
    including different versions on different computers. Use stable instance
    identity and endpoint, retaining computer name and version. Two hosts
    with the same VP version remain separate. Use This computer for the
    local file choice and the computer name for remote targets; qualify
    names with version or endpoint only when needed.

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
- Open the local file editor by default when a local VP installation exists,
  including while VP is stopped. Keep LAN discovery active in the background.
  A previously selected remote stays selected and shows an unavailable
  overlay until it reconnects or the user chooses another target.

- First delivery targets two Windows computers using the existing config EXE
  and its packaged dependencies; no new client application or platform port.
- Auxiliary operations that currently open local folders/logs or clear shader
  caches must not accidentally operate on the laptop in remote mode. Identify
  these during implementation and disable unsupported remote operations with
  a clear explanation for the initial delivery.
- Add focused unit/integration coverage for request validation, discovery
  parsing, local saves with VP stopped, remote Apply/OK/Cancel behavior, and
  each reported runtime outcome.

## Non-goals

- Shipping a browser, WebAssembly, HTML, mobile, cloud, account, certificate,
  or Internet-access configuration UI in this story. Future web and Android
  clients should reuse the document and RPC contract through platform-specific
  adapters rather than depend on Win32 or the Qt desktop shell.
- Continuous per-keystroke or slider-preview updates; requests occur only on
  **Apply** or **OK**.
- General remote control of playback, capture, operating-system functions, or
  arbitrary file access.
- Discovery across routed subnets/VLANs; manual address entry is sufficient
  outside the local broadcast domain.

## Acceptance criteria

- Switching targets shows a loading screen while local file/device discovery
  or remote RPC reads run off the main UI thread. The window continues to
  repaint and respond to resize events; a failed connection preserves the
  previous target.
- Beside a local VP installation, Config immediately opens the editable
  local file as This computer even while VP is stopped. Apply/OK safely
  persists it for the next VP start. A remote-only Config copy has no
  local file choice. LAN discovery runs in the background, and newly
  discovered remotes require explicit selection.

- Starting the config editor discovers both local and remote running VP hosts
  on the same LAN subnet, up to ten distinct instances network-wide. Verify
  discovery across multiple computers with both matching and differing VP
  versions, without manually entering addresses or opening config editors on
  those hosts. A manually entered host can also be selected.
- Each running VP host answers discovery while its RPC endpoint is available;
  a new scan no longer advertises a stopped host. Loopback and LAN responses
  for one instance produce one entry, and two hosts sharing a version remain
  separate entries.
- Tray actions identify This computer or the selected remote computer name.
  Discovered systems remain independently selectable.

- After Config restarts, a remembered remote reconnects only to the same
  stable instance. Until then, its last known name remains in the selector
  with an unavailable overlay and editing disabled. The user may explicitly
  choose the local file when available. No unavailable target writes to
  another computer.

- Duplicate discovery replies do not consume slots. Ten distinct instances
  end the scan; an eleventh is not added. Scans with fewer results time out.
- Device, display, renderer, and LUT choices come from the selected target via
  `GetCapabilities`, including when the client's installed hardware differs.
- A second Windows computer exercises all three RPC operations. Verify
  document round-trip preservation and target isolation with multiple VP
  instances. Local file editing works both with VP running and stopped.
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
