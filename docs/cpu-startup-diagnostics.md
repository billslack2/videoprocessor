# CPU startup diagnostics

Normal startup writes `cpu:` records when debug logging is enabled. Detection
uses CPUID, checks XSAVE and OSXSAVE before XGETBV, then requires AVX2, FMA,
BMI1, BMI2 and OS-enabled XMM/YMM state before enabling conversion kernels.
Raw feature bits, XCR0, the environment opt-out and the selected path are logged.
No instruction probe runs during normal startup; the dispatch decision is cached.
The application's baseline still requires AVX.

## Collect evidence without opening video

Run from the folder containing this build's VideoProcessor.exe:

```powershell
$p = Start-Process .\VideoProcessor.exe -ArgumentList '/cpu_check' -PassThru -Wait
$p.ExitCode
Get-Content .\logs\vp.log | Select-String 'build identity|cpu:'

$p = Start-Process .\VideoProcessor.exe -ArgumentList '/avx2_probe' -PassThru -Wait
$p.ExitCode
Get-Content .\logs\vp.log | Select-String 'build identity|cpu:'
```

Both switches force logging for that invocation even if configuration disables
it. Logs follow the existing logger's rotation/retention policy; save each log
before starting another run. No capture, renderer, display recovery, or settings
write takes place. `/help` takes precedence; diagnostics take precedence over
`/fix_display` and regular playback options.

`/cpu_check` only detects and reports capabilities; exit 0 means diagnostics
completed, not that AVX2 is available.

`/avx2_probe` intentionally calls an isolated, non-inlined AVX2 function regardless
of the capability result or VP_DISABLE_AVX2. Runtime input is permuted and added,
then checked by baseline code. Only EXCEPTION_ILLEGAL_INSTRUCTION is caught by a
narrow Windows SEH boundary around that call. Other exceptions are not swallowed.
The process exits immediately after logging and flushing:

| Result | Exit | Meaning |
| --- | --- | --- |
| PASS | 0 | The AVX2 probe executed and arithmetic matched |
| UNSUPPORTED | 2 | The probe raised illegal instruction (0xc000001d) |
| INCORRECT_RESULT | 3 | The instruction returned unexpected arithmetic |

An AVX2-capable machine should report PASS. The i7-3820 should select baseline-AVX
and report UNSUPPORTED for the explicit probe. That hardware result remains to be
collected from the tester. Running under a debugger may break on the first-chance
exception before the handler runs; run normally to collect the diagnostic.

Setting `$env:VP_DISABLE_AVX2='1'` disables conversion dispatch, but does not
emulate an old CPU. The explicit probe should still PASS on AVX2 hardware.
Use `Remove-Item Env:VP_DISABLE_AVX2` afterwards to restore this shell's default.

This probe verifies a real AVX2 instruction, not every AVX2/FMA/BMI instruction,
converter, linked DLL, or the cause of a separate crash. For the original crash,
retain the full Event 1000 record, fault offset, and exact faulting DLL.

## Local validation — 2026-10-05

- Branch: codex/avx2-startup-diagnostics, based on fetched beta 982adb0e.
- Applied optional-AVX2 source commit 4bad3af4658a as local commit 661108f2.
- Full x64 Release solution build succeeded.
- Native suite: 1908/1908 passed, including three new CPU/probe tests.
- CPU/converter tests with VP_DISABLE_AVX2=1: 86/86 passed.
- AVX2 boundary audit: 5/5 passed, including the new probe object.
- Release disassembly confirms VPERMD and 256-bit VPADDD in the probe.
- /cpu_check and /avx2_probe exited 0 on the local AVX2 machine.
- With forced fallback the log selected baseline-AVX, while the explicit
  instruction probe still passed, as expected on AVX2 hardware.
- Evidence: artifacts/cpu-diagnostics (logs, TRX, build hashes).
- This validation build reports dirty=1; it is a local development build.
- No playback-frame algorithms or hot dispatch work added by diagnostics.
  No new comparative performance benchmark was conducted.
- Not yet exercised on a physical non-AVX2 CPU. The caught-illegal-instruction
  branch still requires that hardware test. No deployment or beta merge.
