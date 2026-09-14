# VP-0187: REQ006 SDR transfer and calibration integration

## Status

In Progress

User authorized merge, x64 Release build, deployment and ZIP on 2026-09-14.
Integration worktree: E:\codex\videoprocessor\merge-req006-20260914.
Beta base: v1.3.005-beta bb51ffcb; REQ006 head: 5c244ebd.
Local merge: 94f938d3. Build and verification in progress.

## Scope and decisions

Integrate the existing codex/req006-sdr-transfer branch, including the Color
Output layout, pure-2.4 SDR reference, default passthrough, independent SDR LUT
input gamma, BT.1886-to-2.4 normalization, expanded HDR luminance validation,
Limited-transfer diagnostics and read-only Windows display snapshots.

The user accepts changed Auto/default/Off semantics and does not require a
legacy compatibility mode. Keep internal BT.1886 implementation available.
Do not add the assistant-proposed madVR editor gating. VP-0179's unmerged
capture-to-madVR diagnostics are excluded; that story was closed Will Not Do.

## Acceptance

- Preserve the current beta screen-edge-padding implementation in the merge.
- Complete x64 Release build and relevant native/configuration regression tests.
- Push the combined revision to the beta integration branch.
- Back up replaced deployment files, deploy host and renderer from the same
  successful Release build, and verify hashes. Preserve active configuration.
- Produce a verified ZIP and checksum in the user's Done folder.

## Review limitations retained

The prior review identified stale public gamma documentation and the shared
SDR reference behind the fallback desired-gamma control. This integration
does not claim to resolve those items. No physical HDMI/gamma measurement
is claimed. Display snapshots can add transition latency.

## ID audit

Fetched origin/main ab52902e: 205 canonical records and 205 index items,
no duplicate or unmatched IDs, maximum root 0186. Allocated VP-0187.
