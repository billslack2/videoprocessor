# VP-0146: Re-apply profile rules shortcut and safe queue-profile handoff

## Status

Done (2026-08-24). The developer confirmed the deployed behavior works. The
tested feature branch `codex/reset-queue-after-profile` was merged into
`v1.2.001-beta` at `6188d5a` and published to
`origin/v1.2.001-beta`. A clean x64 Release rebuild from that merge commit
passed the complete native test suite before deployment.

This record was created directly in `done` at the developer's request after
the implementation, review, acceptance, merge, and release were complete.

## Progress and release evidence

- The queue-profile repair series (`9df6f7a` through `428cb33`) makes queue
  profile transitions use the existing delayed queue-reset path, lets source
  rules override persisted fallback selections, preserves live explicit
  shortcut overrides, and keeps that runtime-only override state outside the
  EXE/DLL shared profile snapshot ABI.
- `149f948` adds the optional global `[shortcuts] reapply_rules` command. It
  clears every live shortcut override, immediately resolves all current
  profile rules, retains persisted manual selections as fallbacks, and queues
  the normal delayed queue reset only when the effective queue changes.
- The Config editor exposes **Re-apply rules** with no built-in key so the
  operator can choose a non-conflicting chord. The shortcut is applied live.
- x64 Release builds succeeded for `VideoProcessor-GUI`,
  `VideoProcessor-VPRenderer`, and `VideoProcessor-Config` from `6188d5a`.
  The complete native VSTest suite passed 871/871.
- Deployment replaced the host executable and VP Renderer DLL as a matched
  pair from the same merge commit, plus the Config editor executable. Their
  verified deployed SHA-256 values are
  `A510AEE62537FE548E2DB8E9D0C5BDC63D4F23D356DA38F71E083C6FB8058723`,
  `5335C2F9CF19CD3E6810F5A9DD35746490FAD2A3B279BC942B1315FCF2DFA8C9`, and
  `ADB6B85583F4B5F4797B2E2D31B2BB9402164AEA71FA6183CB8DDDA64494AACE`.
  The verified deployed set is archived at
  `C:\Videoprocessor\vp\backups\VP-0146-deployed-20260824-091810`.
  Active configuration and persisted-state files were not changed.
- The initial predecessor-snapshot directory creation was rejected by an
  invalid PowerShell parameter before any predecessor files were copied.
  The deployment itself completed and verified; the clean deployed release is
  now recoverable from the archived snapshot above. Existing historical
  deployment backups were left intact.
- The required tracker audit before assignment found 162 canonical files and
  162 index rows, with no duplicate, missing, or state-mismatched records.
  The pre-existing Registry total incorrectly said 161; this commit
  deliberately corrects it to 163 after adding VP-0146.

## User story

As a VideoProcessor operator, I want a global **Re-apply rules** shortcut so
I can temporarily force a profile such as Low Latency, then return control to
the current renderer/source/refresh rules without losing the remembered
selection that should act as the fallback when no rule matches.

## Acceptance criteria

1. A configured `reapply_rules` shortcut clears only live runtime override
   groups and immediately reevaluates every profile rule with the current
   renderer, source-rate, and actual-refresh context.
2. Re-applying rules never rewrites or clears persisted manual selections.
   When no rule matches later, the remembered selection remains the fallback.
3. A matching rule wins immediately after re-apply; repeated re-apply is
   safe and does not introduce further state changes.
4. If re-application changes the effective queue profile, VP uses the normal
   delayed queue-reset policy rather than a renderer restart or an immediate
   unmanaged flush.
5. The Config editor displays the shortcut, validates/saves/reloads it, and
   the runtime dispatches it as a global configurable accelerator.
6. Runtime tests cover rule precedence, live low-latency override, re-apply,
   persisted-state retention, fallback behavior, and idempotence; the full
   native suite remains green.

## Boundaries

- This does not remove remembered selections, edit `VideoProcessor.state`, or
  change the meaning of any existing profile shortcut.
- It does not assign a default chord, avoiding a new conflict with paired
  renderer/profile shortcuts.
- It does not restart a renderer solely to re-apply rules; a changed queue
  follows the established delayed reset path.
