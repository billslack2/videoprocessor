# Smooth Motion overlap fix, 2026-10-25

Source: 3520e71396a3c20112e4b18c67258052af34e0e0, draft PR #198.
Successful clean x64 Release solution build: 10 existing conversion warnings,
zero errors; both renderer variants. All 375 focused native regressions passed
in 24.1 seconds. Luna xhigh final review found no concrete blocker. The unit
suite covers Auto classification/transitions, field cadence, source-clock reset
and mixed-output timestamp parity; the production queue handoff guard was
source-reviewed and remains a live qualification item.

The user reported stutter with both controls enabled at mismatched refresh.
Active Smooth Motion mixing now bypasses every-refresh repeats/cache, timestamp
normalization and stale retirement. Queued frames about to mix cancel the old
repeat scheduler before dequeue. Auto bypass and deinterlace-only retain repeats;
presentation depth and source lookahead remain independent.

Authorized test deployment replaced and hash-verified host + primary/Beta
renderers from the same clean Release build. Config UI was unchanged and its
running session preserved. Active VideoProcessor.cfg was backed up and preserved
byte-for-byte, including current every-refresh=false and depth=3. Runtime
receipts verified Release/x64/hash. Backup and hashes are in deployment.json.
Graceful close and restart produced responsive PID 84800. Host/renderer log
identities both match this commit, dirty=0. Smooth Motion engaged at about
23.976455 to 59.951 Hz. Startup inherited startminimized and returned DXGI
occluded status; restoring the playback window stopped these messages. Startup
drops are not a clean cadence measurement. Every-refresh remains off as saved
by the user: simultaneous-on visual acceptance is pending, not claimed.

Rollback: gracefully stop VP; restore VideoProcessor.exe and both renderer DLLs
and their .runtime.json receipts from the backup recorded in deployment.json;
restart VP. Keep the current configuration unless the user requests rollback
of settings. No merge or public release was performed.
