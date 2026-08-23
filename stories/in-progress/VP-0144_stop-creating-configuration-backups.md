# VP-0144: Stop creating configuration backups

## Status

In Progress (2026-08-23). Implementation is starting from the current remote
default beta integration branch `v1.2.001-beta` (queried at
`bb5be05ab7dd8e92d3cbab02b789ab22bdad4329`) in the clean worktree
`C:\Videoprocessor\vp\git-main\stories\.vp-0144-no-config-backups` on branch
`codex/vp-0144-no-config-backups`. VideoProcessorConfig currently creates a dated
`VideoProcessor.cfg.backup-YYYYMMDD-HHMMSS` file whenever it applies an edit
and includes that path in the success notification. The operator no longer
needs those backups; saving must update the active configuration without
creating a backup artifact or showing backup text.

## Progress

- 2026-08-23: Implemented source commit `5fc3cfb` on
  `codex/vp-0144-no-config-backups`, based on the freshly queried and fetched
  `v1.2.001-beta` tip `bb5be05`. The save core no longer creates a dated
  configuration copy before atomic replacement. Validation, temporary-file
  writing, external-change detection, and atomic replacement remain in place.
  The Config UI no longer appends `Backup: <path>` to normal or draft-save
  status messages.
- x64 Release builds succeeded for `VideoProcessorConfig.exe`,
  `VideoProcessor-Test`, and `VideoProcessorConfigTests`. Focused native
  save-flow tests passed 5/5; the complete Qt Config-editor test executable
  passed.
- Deployed the matching x64 Release `VideoProcessorConfig.exe` to
  `C:\Videoprocessor\vp\config\VideoProcessorConfig.exe`; deployed SHA-256 is
  `72C2043170E670970E26D80191D6BCBADBE4EB986293EE2B097E5F5138FB05C8`.
  The prior executable is backed up at
  `C:\Videoprocessor\vp\backups\VP-0144-no-config-backups-20260823-0727\VideoProcessorConfig.exe`
  (SHA-256 `4CB178AE64BBA0C58F48631046657084D8F4839DD939D72224268EA2F338FA6A`).
  `C:\Videoprocessor\vp\VideoProcessor.cfg` was not changed.
- Pending user validation: apply a real edit through the deployed Config
  editor, confirm no new `VideoProcessor.cfg.backup-*` file appears, and
  confirm the success message has no `Backup:` text.

## User story

As a VideoProcessor operator, I want VideoProcessorConfig saves to update the
configuration directly without automatic backup files, so normal editing does
not clutter the installation directory or show irrelevant backup paths.

## Required behavior

1. Remove the automatic dated backup creation from every successful
   VideoProcessorConfig edit/apply save path.
2. Preserve the existing safe configuration-write behavior: validation,
   atomic replacement/publication, error reporting, and renderer-restart
   request behavior must remain intact.
3. Remove the `Backup: <path>` portion of the success notification. For
   example, a save that requests a renderer restart may report
   `Changes saved safely. Restart renderer was requested.` without a backup
   path or dangling punctuation/whitespace.
4. Do not delete or alter any backup files that already exist on an operator
   installation.

## Acceptance criteria

1. Applying an actual configuration edit through VideoProcessorConfig updates
   `VideoProcessor.cfg` and creates no new
   `VideoProcessor.cfg.backup-*` file.
2. The success notification contains no `Backup:` label, backup filename, or
   backup path.
3. Save validation failures and write failures remain actionable and do not
   replace or corrupt the active configuration.
4. Existing renderer-restart and takes-effect-next-start messaging continues
   to accurately reflect the change classification.
5. Automated coverage (or a focused regression test where practical) verifies
   that the save flow does not invoke backup creation and formats the success
   message without backup information.

## Boundaries and related work

- This story removes creation and display of new configuration backups only;
  it does not remove already-created files or redesign configuration recovery.
- Keep configuration publication transactional. The absence of a retained
  backup must not weaken validation or atomic-write safeguards.
- The screenshot reports the observed behavior: `Changes saved safely. Restart
  renderer was requested. Backup: C:/Videoprocessor/vp/VideoProcessor.cfg.backup-20260823-070451`.

## Likely implementation areas

- VideoProcessorConfig configuration save/apply handler and backup helper
- Configuration-save notification formatting
- Focused VideoProcessorConfig save-flow tests

## Tracker audit note

- Audit against fetched `origin/main` on 2026-08-23 assigned `VP-0144`, greater
  than every discovered canonical root-story ID and index ID.
- Pre-existing discrepancy retained for deliberate tracker repair: the
  canonical `done/VP-0143_restart-renderer-after-queue-profile-changes.md`
  record says `Done`, while `INDEX.md` lists VP-0143 as `In Progress`.
