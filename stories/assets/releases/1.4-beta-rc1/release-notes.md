# VideoProcessor 1.4 RC1

Built from the clean `v1.4.00-beta` commit `b801730610a095826799275084ce65a29a0fc167` as version `1.4.00-rc1` for Windows 10/11 x64.

## Changes since 1.3 RC3

- Show detected picture aspect and viewport labels in the renderer OSD ([#124](https://github.com/billslack2/videoprocessor/pull/124)).
- Add live Config discovery, remote editing, and saved profile/action activation ([#127](https://github.com/billslack2/videoprocessor/pull/127)).
- Improve anamorphic NLS compensation and add optional Magewell capture support ([#130](https://github.com/billslack2/videoprocessor/pull/130)).
- Separate frame conversion from presentation and add frame pacing diagnostics ([#131](https://github.com/billslack2/videoprocessor/pull/131)).
- Keep out-of-range content stationary in fill-only zoom profiles ([#126](https://github.com/billslack2/videoprocessor/pull/126)).
- Make AVX2 conversion optional on older CPUs and add startup CPU diagnostics ([#133](https://github.com/billslack2/videoprocessor/pull/133)).
- Set executable and installer publisher metadata to Bill Slack ([#132](https://github.com/billslack2/videoprocessor/pull/132)).

## Downloads and installation

Choose the Setup EXE for a per-user installation or the x64 Portable ZIP. Both carry app-local Microsoft VC runtime files; the packaging policy minimum is `14.44.35211.0`. Close VideoProcessor and Config, including the Config tray icon, before running Setup. Existing configuration and user data are preserved during updates. For a fresh portable folder, copy `VideoProcessor.cfg.example` to `VideoProcessor.cfg`; keep the existing file when updating. An older build may not understand newer settings if you roll back.

The executable and Setup are **unsigned**. Bill Slack is their Windows version/installer metadata, not an Authenticode signature. Windows may show an unknown-publisher or SmartScreen warning. SHA-256 sidecar files are attached for both downloads.

## Verification and limits

- Clean x64 Release solution build; source and all packaged binary hashes pinned to the commit above.
- Native suite: 1,965 passed. Installer support: 49 checks. Installer identity: 12 checks. Runtime packaging: 23 checks. Portable configuration contract: 8 checks, plus validation of the finished ZIP sample.
- A real installer lifecycle run was attempted under an isolated QA identity, but setup refused the first install because VideoProcessor Config processes were already running on this machine. Those sessions were left open. Clean Windows without Visual Studio and interactive Config Apply/OK/reopen qualification have not been completed for this RC.

**SHA-256**

- `VideoProcessorSetup-1.4.00-rc1-b801730610a0.exe`: `157530AA051454552E3EC4BCDF525DBA5B39E6662E29ED66190208A093E56AE0`
- `VideoProcessor-1.4.00-rc1-b801730610a0-x64-Portable.zip`: `3134B151146769B94FFF4B015C109F9FF6BDD87BE06F7A4BF15B4DBDD546E411`
