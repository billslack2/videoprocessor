# VP-0187: REQ006 SDR transfer and calibration integration

## Status

Done

User authorized merge, x64 Release build, deployment and ZIP on 2026-09-14.
Integration worktree: E:\codex\videoprocessor\merge-req006-20260914.
Beta base: v1.3.005-beta bb51ffcb; REQ006 head: 5c244ebd.
Merge 94f938d33216d0f212797f1ed4c6f83db02d9187 pushed to v1.3.005-beta,
successfully built, deployed and packaged on 2026-09-14.

## Release evidence

- Complete x64 Release solution build: 0 errors, 45 warnings.
- Selected native tests: 282/282 passed (libplacebo policy, GPU LUT/ramp and
  luminance tests, configuration core and picture/crop policy).
- Complete Config UI suite: 74 passed, 0 failed, exit 0. Physical two-monitor
  placement unavailable; synthetic negative-origin and target-window coverage ran.
- Both screen-edge commits 190f989d and ca5786c5 are ancestors of this merge.
- Canonical package: 59 files. Every deployed file and every ZIP entry was
  SHA-256 verified against the staged Release package.
- Deployment: C:\Videoprocessor\vp. Rollback backup:
  C:\Videoprocessor\vp\backups\vp0187-20260914-175437.
- Active VideoProcessor.cfg unchanged, SHA-256
  77E0FAC0F279B6813F5FEF195CA6F042D47822D32B1B99BC8CC451F607A5408D.
- Host SHA-256: C055A9B61EAC18A02A54BC14C5795E1762F58D05C3A472DB126A047BCCFCDAF2.
- Renderer SHA-256: 20EE0015C069B823BE5376416DAFAC8A6AE1DC0715AB96DB474A514390CDADEB.
- Config SHA-256: 92243EEE5B7E4310B3E8754F19CA58DE846739262E5F1CC97796A73D7DCFC788.
- ZIP: C:\Users\bslac\Documents\ChatGPT\Done\VideoProcessor-v1.3.005-beta-VP0187-94f938d3-x64-Release.zip.
- ZIP SHA-256: 9E5D521AA7C3327A57CF12873913D42DD5C9B546A4D56C922B778AC6AA63BCC9.
- Build log and test results: integration worktree build-release.log and
  artifacts/tests/req006-native.trx, artifacts/tests/req006-ui.log.
- Deployment backup includes deployment-verification.json with all hashes.
- VP was already stopped; its hidden cached Config process was stopped for
  replacement. No new playback session or physical display measurement performed.

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
