# VP-0168 configuration RPC readiness review

Reviewed against `v1.3.005-beta` at `c84863eaa59e03fe218de824c34b7c0220c4fd87`.

## Current Apply chain

1. `ConfigEditorWindow::saveChanges` edits a `ConfigDocument`, validates it
   through `ConfigEditorCore::ValidateCandidate`, and calls `SaveSafely` with
   `overwriteExternalChanges=true`. `SaveSafely` writes a sibling temporary
   file and replaces the target file. The document keeps comments, unknown
   settings, section order, and original newline style.
2. The editor signals `Local\\VideoProcessor.ConfigurationChanged.v1`. This
   event has no acknowledgement or error result. It can also be signaled by an
   editor targeting a different local installation.
3. VP's dialog timer consumes that event on the UI thread and calls
   `ApplySavedConfiguration`. `StageSavedConfiguration` reads the saved file
   twice for stability, validates the schema, stages runtime settings and
   profile resolution, and classifies changed values.
4. `ApplySavedConfiguration` publishes or requests the existing capture
   restart, renderer restart, queue reset/profile apply, shortcut reload,
   interface apply, or save-only action. Renderer and capture transitions are
   requested; the call does not wait for their completion.

## Boundary required by RPC

`GetConfig` must return the target's **exact document bytes**. The client
loads those bytes into the same editable document model, and must not call
`SaveSafely` on the laptop for a running target. `GetCapabilities` must be
collected on the target because the current editor discovers capture devices,
monitors, renderers, and LUT files on its own computer.

`ApplyConfig` must enter the VP dialog's UI thread before touching its runtime
state. The target must validate the complete candidate, stage the prospective
runtime configuration, commit through the existing safe-save path, and then
use the same action dispatch as local Apply. A response may report that a
restart or reset was **requested**, never that it completed synchronously.
Failed validation, staging, or persistence must leave the prior target file
and runtime intact. The server must reject malformed and oversized requests
before allocation and must never accept a path from a client.

## Protocol and delivery constraints

The shared `ConfigurationRpcProtocol.h` starts a versioned, length-bounded
binary frame contract. The initial payload ceiling is 4 MiB. The three TCP
operations are `GetConfig`, `GetCapabilities`, and `ApplyConfig`; UDP discovery
uses the same version marker and returns identity, name, version, and endpoint.
Local editing of a running instance must take the loopback RPC path. Explicit
offline mode retains file editing when no VP instance is available.

The editor's current tray action, local capability cache, calibration/LUT
directory checks, shader cache controls, and log-folder controls require
target-aware handling before remote editing is enabled. A selected target
must remain pinned through Apply, including when discovery refreshes or the
target becomes unavailable.

## Target endpoint contract (implementation in progress)

The VP host listens on TCP **41686** and answers UDP discovery queries on
**41687**. It accepts IPv4 loopback, RFC 1918 private addresses, and link-local
peers. The discovery responder runs independently of the TCP request worker,
so a slow Apply does not prevent a discovery reply. UDP queries and replies
use the same 12-byte `VPCR` frame header as TCP. The reply carries the stable
installation ID, computer name, VP version, and TCP port; the datagram source
address is the reachable host address. No unsolicited announcements are sent.

Each TCP connection carries one request and one response. The header contains
magic `VPCR`, a big-endian protocol version (`1`), operation, and payload byte
count. Requests with an unknown version, operation, flags, malformed length,
or payload over 4 MiB are rejected before payload allocation. A response sets
the response flag; a failed response also sets the error flag and carries one
length-prefixed UTF-8 error string.

| Operation | Request payload | Success payload |
| --- | --- | --- |
| `GetConfig` | Empty | UTF-8 target path; exact configuration bytes |
| `GetCapabilities` | Empty | Capture devices; device/connection pairs; monitors; filtered renderers; all renderers; relative `luts/*.cube` paths. Each list starts with a big-endian count and contains length-prefixed UTF-8 strings. |
| `ApplyConfig` | Exact baseline bytes; complete candidate bytes | Runtime action label; one status byte: `0` saved for next start, `1` applied live, `2` reset/restart requested. This never asserts a completed transition. |

All three operations are marshalled to VP's dialog thread. `ApplyConfig`
rejects a stale baseline, validates the candidate, stages runtime state using
a sibling candidate file with the real configuration path retained as its
logical path, and only then calls `SaveSafely` and the established runtime
action dispatcher. The client supplies document bytes, never a target path.
