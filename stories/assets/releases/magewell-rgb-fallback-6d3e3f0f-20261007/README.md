# Magewell RGB fallback tester build

Version: 1.4.00-rc1-magewell1
Source: 6d3e3f0f0086febed47f3fdc3769b00f630c86e2
Beta/RC1 base: b801730610a095826799275084ce65a29a0fc167
Branch: codex/magewell-rgb-fallback
Draft PR: https://github.com/billslack2/videoprocessor/pull/134

Fixes two RC1 rejection paths: locked RGB with unavailable/invalid/zero-depth input metadata, and RGB8/RGB10 without matching native output capability. Native ARGB8 and RGB10 remain preferred. Otherwise known RGB uses advertised P210, delivered as the existing V210 format. RGB12/16 also use P210. This is limited-range 10-bit YUV 4:2:2: chroma detail is reduced and precision above 10 bits is reduced. Unknown input depth remains unknown rather than being guessed. Malformed depths, unknown color, odd P210 width and no compatible output remain errors.

Only Magewell code, tests and documentation changed. DeckLink and renderer sources match the beta base. No direct-P210 renderer optimization is included.

Validation: clean x64 Release build; all 1,967 native tests passed; installer support, identity, runtime packaging, portable configuration contract and archive checks passed. Source isolation and no Magewell static/delay imports passed. Independent code review and synchronized asynchronous fallback-to-native recovery test passed.

Limitations: no physical Magewell card is available locally. This candidate is not hardware-qualified and is not merged into beta or published as a release. Setup is unsigned. Isolated QA installer lifecycle was attempted but its running-process guard blocked fresh install while existing VP/Config sessions were running; no sessions were terminated. Clean Windows and interactive Config testing remain unverified.

## Test instructions
Close VP and Config, including its tray icon, before installing. Preserve your settings. For a new portable folder only, copy VideoProcessor.cfg.example to VideoProcessor.cfg.

Retest the source and signal mode that failed on RC1, then change between SDR/HDR and RGB8/10/12 where available. Check picture, colors, HDR, frame pacing, black-bar handling and capture recovery after signal loss. Confirm the build identifies 6d3e3f0f0086.

Send vp.log from startup through any failure. New lines include `Magewell capture output FOURCC`, `Magewell signal read` with inputStatus/valid/type, and `Magewell compatibility conversion`. These tell us whether the SDK lacks metadata or native output. Include the card model, driver/runtime version, source device and exact on-screen error. Passing mock SDK tests does not establish that the real device's conversion works.

SHA-256 sidecars accompany both downloads. Full pinned build evidence is in validation/release-receipt.json.

Local artifact directory: E:\codex\releases\magewell-rgb-fallback-6d3e3f0f-20261007
