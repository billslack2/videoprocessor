# Updater hardening package qualification — November 13, 2026

Source: 69c9dfcf443b2b0dd7a56ef48359cb33bf4180ae, pushed codex/updater-process-hardening-20261111. Base: remote v2.0.4-beta 3247ea13687ba5cf96bae6c84a883e875dca575f. No local AR/subtitle runtime changes included.

Setup and portable ZIP built successfully as version 2.0.4-beta-updater1. Defender custom scan found no threats. Artifacts remain under the source checkout artifacts/installers; distribution export was withheld.

Full suite: 3,567 tests, 3,545 passed, 22 failed in existing AR diagnostics/remembered-edge tests. The same 22 filtered tests also fail in the clean beta build at deploy-v2.0.4-beta-3247ea13. No AR changes or test bypass applied.

Additional passing checks: updater core 37, control 62, staged handoff 57, portable configuration contract 8, full-payload isolated Inno lifecycle 231. Setup and ZIP sidecars match. Unsigned; clean-Windows and physical capture qualification were not performed. No Google Drive upload or public release publication.

Setup SHA256: 12AEB58C28A7EEB5FC40044CFBD56B2F4DB052267B8674BDEB22EBF4A9016863
ZIP SHA256: 433686C39EF7168F96023EF2B46BC537510F87E26D6A4F30B0B29C6D7401297C

Full suite evidence: E:/codex/videoprocessor/updater-process-hardening-20261111/artifacts/release-verification-04e4d5d9c5484df483167687678627b6
Installer lifecycle evidence: artifacts/e2e-000134ee (231 passing assertions).

## Authorized manual test distribution

User explicitly approved generation after disclosure of the 22 identical baseline failures. Verified setup, portable ZIP and optional EDID were copied to C:/Users/bslac/Documents/ChatGPT/Done/VP-2.0.4-beta-updater1-69c9dfcf443b with READ-ME-FIRST.txt documenting the limitation and unsigned status. Final copied hashes match source sidecars; Defender scan found no threats. No tests were changed or skipped, no Google Drive upload performed, and no public release published. This authorization supersedes the fully-passing-suite export gate for this test distribution only.

