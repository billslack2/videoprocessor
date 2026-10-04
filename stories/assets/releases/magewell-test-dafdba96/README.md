# Magewell trial build — dafdba962487

Run `Run\VideoProcessor.exe` from this folder. Close another VP capture session first if it is using the same card. Config is available at `Run\config\VideoProcessorConfig.exe`.

This is the reviewed, unmerged PR #129 feature commit dafdba962487381be87fd04ccf9b07ff1375dbe5, based on beta 982adb0ea309a7cf314ec2c933bd3213153ff649. The installer and portable ZIP contain the same x64 Release application build. No GitHub release was published and the current installation was not changed.

The Run folder contains copies of your existing VideoProcessor.cfg and VideoProcessor.state. Only startminimized was changed to false, to make launch visible. The original copied configuration is VideoProcessor.cfg.original. The distributable packages contain no personal settings. No Magewell card is expected to be listed without supported hardware.

Validation: clean x64 Release rebuild; 1,930/1,930 native tests; 49 installer preservation/recovery, 12 identity, and 23 runtime checks. All 62 managed files in Run verified against the package manifest. Source/import isolation passed: no mandatory Magewell load-time or delay imports. Receipts, hashes and logs are in validation and the adjacent SHA-256 sidecars.

Unsigned test build. Hardware capture and clean Windows without the SDK/runtime are not qualified. Live installer lifecycle was not run because an existing Config session is open; that session was left untouched. Setup requires closing VP and Config. For a separate trial use the Run folder; setup uses its normal installation registration.

Local output: E:\codex\releases\magewell-test-dafdba96

- [Setup](E:/codex/releases/magewell-test-dafdba96/VideoProcessorSetup-1.3.005-beta-dafdba962487.exe)
- [Portable ZIP](E:/codex/releases/magewell-test-dafdba96/VideoProcessor-1.3.005-beta-dafdba962487-x64-Portable.zip)
- [Build receipt](release-receipt.json)
- SHA-256 sidecars are preserved alongside this record.
