# VP-0225 live trial: 69c51f18 — 2026-11-03

## Status

This was a user-authorized manual x64 Release live trial, not a qualified release and not acceptance of the original scene. Source `69c51f18f516866912d89c0900c7da9478ab9bf5` was built from a clean tree and deployed at 13:55:10. PID 63656 responded. Backup: `C:\Videoprocessor\vp\backups\trial-69c51f18-20261103-135509`.

## Build and CPU checks

Run `20261103-134055-25e3feb6` reports all matching x64 Release builds passing and 3/3 focused CPU tests passing. The full 1,083 Native pass is from earlier tested code `3ea6335ad5e772505e38878ad7baf91bace38300`, not a 69c51f18 Native run.

## GPU actual-renderer failures

Run `20261103-134859-dbbd0ddc` executed four GPU actual-renderer tests and all four failed:

- Standard and Beta `StaticCueWorkerFitsLiveFrameBudget`: worker-proof worst cases were 21.0904 ms and 18.4053 ms, above the 16.683 ms source period. The corresponding mandatory frame-work maxima were 12.67 ms and 12.37 ms.
- Standard and Beta `DecodedDvUnknownInputKeepsCurrentSource`: each reported “Persistent caption composition flashed OFF on an actually submitted source.”

The prior runs are not matched baselines for these tests, so the failures cannot be labeled newly introduced or pre-existing. The source TRX is 7,010,985 bytes and is deliberately not copied; its SHA-256 is recorded in `manifest.json`. The compact `failed.json` is included.

## Cold shader preparation

Cold preparation totals were 64.79 seconds (Standard) and 64.64 seconds (Beta). A background-3 pass took 25.32 seconds. The user accepts a roughly 25-second heavy-background pass temporarily; the roughly 65-second total remains unresolved. This is not a 3x improvement claim. The full runtime log is not copied; the compact test receipts and settings hashes are recorded in the manifest.

## Settings and trial state

Deployment receipt records no configuration edits. Before restart, `VideoProcessor.cfg` SHA-256 was `ADB4E329196B4852DD27661013C93D59122F38AE48234AF5B289E322FDE619F3` and `VideoProcessor.state` was `99CF94A66291AC295858C9AFD73DC880EE42F9496FD974D9CC44A9A1BF979222`. The config still has the same hash. At 13:55:23 the state changed only `profile.subtitles` from `reconstruction` to `classic`; current state SHA-256 is `2CD6A80974E0ABA4551C8403623F39E965DFF227C825380AAF5FCBDFD9B264C0`. The current state reads Classic. The reason for that state change is under investigation.

The prior d8ae4175 trial is superseded. No configuration edit, full qualification, release/merge, or user acceptance is claimed here. Keep VP-0225 in Review.
