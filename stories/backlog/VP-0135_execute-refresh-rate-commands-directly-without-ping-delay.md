# VP-0135: Execute refresh-rate commands directly without a ping delay

## Status

Backlog (2026-08-18). The current Alpha renderer prepends a loopback
`ping.exe` command to a configured refresh-rate command to manufacture a
whole-second delay. Preserve the intended external command hook, but remove
that artificial delay and execute the configured command directly.

## User story

As a VideoProcessor user who configures a batch file or command for a display
refresh rate, I want VP to launch exactly that command when the refresh-rate
decision is complete, without first running an unrelated `ping.exe` timer, so
the configured action is immediate, understandable, and free of hidden delay
processes.

## Confirmed problem

`RunRefreshRateCommand()` currently converts `delay_seconds` to a whole-second
count and builds this shell sequence before the configured command:

```text
ping.exe 127.0.0.1 -n <delay + 1> >nul & <configured command>
```

The resulting string is passed to `cmd.exe /d /s /c`. The loopback ping is not
a connectivity, display, renderer, or readiness check. It is only a timing
hack. With the default five-second delay it creates a hidden `cmd.exe` plus
`ping.exe`, waits approximately five seconds, and only then starts the command
the user actually configured.

The command hook is invoked when Alpha confirms that the display is already
at the desired refresh rate, after Alpha applies a new rate, and after Alpha
restores the original rate. The hook is optional: when no matching
`[refresh_rate_commands]` rule exists, no process is launched.

Using `cmd.exe` is still appropriate for `.bat`/`.cmd` files, shell built-ins,
quoting, redirection, and compound command lines. The defect is the injected
`ping.exe` preamble and artificial delay, not the configured command hook or
its shell compatibility.

## Required behavior

1. After a refresh-rate decision reaches the point where the existing code
   calls `RunRefreshRateCommand()`, select the matching rule using the existing
   exact/range precedence and launch its configured command immediately.
2. Invoke the configured command through the existing hidden Windows command
   processor contract:

   ```text
   cmd.exe /d /s /c "<configured command>"
   ```

   Do not prepend `ping.exe`, `timeout.exe`, a sleep command, a polling loop,
   or any other artificial wait.
3. Remove `refreshRateCommandDelayMs` from renderer runtime state and remove
   the delay from startup/effective-configuration logging.
4. Retire `[refresh_rate_commands] delay_seconds`. Existing configurations
   that contain the key must continue to load safely, but the value must not
   delay command execution. Emit one clear deprecation message identifying
   the ignored key so users can remove it.
5. Update the shipped configuration example, generated/reference
   documentation, configuration UI or schema metadata, and tests so they no
   longer advertise a delay setting or show `ping.exe` as part of command
   execution.
6. Preserve asynchronous launch behavior. VP must report immediate
   `CreateProcessW` success or failure but must not block renderer startup, the
   UI thread, or refresh restoration waiting for an arbitrary user script to
   finish.
7. Preserve command text and Windows quoting behavior. Do not split a batch
   file path or arguments into a new tokenization scheme, silently rewrite the
   configured command, or require users to add their own `cmd.exe /c` wrapper.
8. Log the selected refresh rate/key and the configured command launch, but do
   not claim that successful process creation proves the external command
   completed successfully.

## Acceptance criteria

1. A matching rule launches the configured batch file or command immediately
   through `cmd.exe /d /s /c` with no `ping.exe`, `timeout.exe`, sleep, or
   delay preamble in the process command line.
2. The rate-already-correct, rate-applied, and original-rate-restored paths
   each retain their existing one-command invocation behavior.
3. No matching rule and an empty rule section launch no process.
4. Exact-key precedence over a wider matching range is unchanged, including
   the established `23.976 -> 23` and `59.94 -> 59` key convention.
5. A command containing a quoted batch-file path with spaces and arguments is
   passed through without corruption and launches successfully.
6. A legacy valid, zero, maximum, malformed, or negative `delay_seconds`
   value never delays the command and produces at most one actionable
   deprecation message per configuration load.
7. Process-launch failure remains visible in the log with the refresh rate,
   selected key, and Windows error; VP continues safely.
8. Repository search confirms that the refresh-command implementation and
   current documentation contain no loopback-ping delay construction and no
   active delay setting.
9. Focused parser/rule-selection/command-line tests and a clean x64 Release
   build pass before deployment.

## Validation matrix

Test a `.bat` file, a `.cmd` file, an executable with quoted arguments, and a
shell command using redirection. Cover exact and range rules at 23.976,
24.000, 50.000, 59.940, and 60.000 Hz; no matching rule; display already at
the target rate; successful switch; restoration; `delay_seconds` present and
absent; malformed command text; and `CreateProcessW` failure.

For the direct-launch proof, capture the effective process command line or use
an injectable launcher seam. Assert that the configured command follows
`cmd.exe /d /s /c` directly and that neither `ping.exe` nor another delay
utility appears anywhere in the launch request.

## Boundaries and related work

- VP-0134 owns renderer-generation handoff and restoration of VP-owned display
  state. VP-0135 removes the pre-launch timer; it does not make arbitrary user
  scripts part of renderer state or infer that their side effects are
  reversible.
- Do not remove the refresh-rate command feature or change its rate matching.
- Do not wait synchronously for user commands to exit and do not terminate
  commands merely because a renderer changes.
- Do not replace the command with a new automation, scripting, or plugin
  system.
- Do not change Windows display-mode selection, NVIDIA signaling, renderer
  cadence, or fullscreen policy as part of this story.

## Likely implementation areas

- `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`
- Alpha renderer configuration examples and reference generation
- Alpha configuration parser/schema tests and focused command-launch tests
