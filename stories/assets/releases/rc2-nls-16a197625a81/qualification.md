# RC2 NLS correction test packages — 2026-09-21

Merged into v1.3.005-beta via [PR #107](https://github.com/billslack2/videoprocessor/pull/107) and [PR #108](https://github.com/billslack2/videoprocessor/pull/108). Final source: `16a197625a81aae27c971f13c0b5da0a69f5e9f4`.

Root shader selectors resolve through the shared identity resolver. The example's NLS Off is an ordinary named profile with shortcut N; the empty Standard profile has an explicit Off label. No special editor naming was retained.

Clean x64 Release build, 1,387/1,387 native tests, 49 preservation/recovery checks, installer identity checks, and 23 runtime checks passed. Four focused editor checks passed before the final test-only correction. Initial qualification failed on a stale example test and an intermittent cache test; the example assertion was corrected, the cache test passed isolated checks and the final full suite. Failed-run evidence is retained.

Exports: `C:\Users\bslac\Documents\ChatGPT\Done\VP-RC2-NLS-16a197625a81`.

- [Setup](C:/Users/bslac/Documents/ChatGPT/Done/VP-RC2-NLS-16a197625a81/VideoProcessorSetup-1.3.005-beta-16a197625a81.exe) — SHA-256 `3986348666A65584821ACFE7D423AD9F8C32C536B0F74152BC3E988A5439BE7E`.
- [Portable ZIP](C:/Users/bslac/Documents/ChatGPT/Done/VP-RC2-NLS-16a197625a81/VideoProcessor-1.3.005-beta-16a197625a81-x64-Portable.zip) — SHA-256 `4174B00D21A95DA2111671DB15DD56553865C4983B70323E4D455F7CEF17C8F2`.
- Matching `.sha256` sidecars accompany both files.
- [Build receipt](release-receipt.json).

Unsigned test packages; no public GitHub release uploaded. The real installer lifecycle harness refused execution because the account already has a registered VP installation. Clean Windows without Visual Studio, interactive Config Apply/OK/reopen and physical playback were not newly qualified.

Deployment to C:\Videoprocessor\vp is authorized but awaiting closure of VP and its Config tray process. No deployed files were changed by the blocked attempt. Story states are unchanged.
