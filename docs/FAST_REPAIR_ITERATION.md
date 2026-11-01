# Fast repair iteration

Use the existing x64 Release toolchain without paying for the full GPU matrix on
every edit. These commands never issue a full-qualification receipt or deploy.

```powershell
# All mandatory native AR/subtitle contracts; no host/renderer build or GPU work.
./tools/qualify_runtime_build.ps1 -Lane Native -MagewellSdkRoot E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3

# Smallest CPU reproducer, with strict executed-method verification.
./tools/qualify_runtime_build.ps1 -Lane Native -Methods ClassicTimedEngageKeepsCurrentCaptionVisibleUntilActualPanCoversIt -MagewellSdkRoot E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3

# Build host + BOTH renderers, then run named actual renderer regressions.
./tools/qualify_runtime_build.ps1 -Lane Focused -Methods StandardRendererClassicPersistentDecodedDvCaptionHasNoCadenceFlicker,BetaRendererClassicPersistentDecodedDvCaptionHasNoCadenceFlicker -MagewellSdkRoot E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3

# Full qualification before release/packaging. No selected-method shortcut.
./tools/qualify_runtime_build.ps1 -MagewellSdkRoot E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3

# Run the broad GPU/cue suite using those exact qualified binaries, without rebuilding.
./tools/test_subtitle_motion_renderers.ps1 -UseQualifiedBuild -OutputDirectory E:\codex\diagnostics\gpu-cue-qualified -MagewellSdkRoot E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3
```

The shared native library finishes first. Independent companion projects then
build two at a time (`-BuildParallelism 1` disables this); shared renderer version
source is generated before the parallel phase. GPU cases run serially to avoid
contention changing their timing results. Each build records elapsed seconds.
The tests retain their actual source-pixel, caption, cadence and transition
assertions. Native/Focused results are explicitly partial evidence, saved as
`iteration.json` beside logs/TRX; missing, failing or unexpected tests fail the
run. Any build invalidates the previous full qualification receipt.

A manual-review deployment may use focused evidence only when explicitly
requested by the operator, and must disclose incomplete qualification. Release
packaging still requires the full matching source/binary qualification receipt.

Native test compilation uses four compiler workers per test project. Full
qualification runs shared native contracts once in the standard host and each
Standard/Beta actual-renderer case exactly once in its matching host. Both test
binaries and runtime dependencies remain built and hash-verified. The strict TRX
gate requires the complete CPU suite and both GPU suites; a missing case or
dependency cannot qualify. GPU assertions and input-rate coverage are unchanged.

Tests use a persistent writable shader cache under
`%LOCALAPPDATA%/VideoProcessor/TestShaderCache` (override with
`VP_SHADER_CACHE_DIRECTORY`). Standard/beta caches are distinct and libplacebo
retains shader compatibility validation. For an intentional cold-cache check,
point that variable to a new empty absolute scratch directory. Do not label a
warm-cache run as cold-start coverage.

The Native lane builds `VideoProcessor-CriticalNativeTests.vcxproj`, which imports the
normal test project's compiler, PCH, MFC, library and dependency settings. Its compile
whitelist is generated from every `nativeSources` and `explicitNativeMethods.source`
entry plus `pch.cpp` and the explicit `LibplaceboRenderParameters.cpp` support
translation unit needed by mixed MotionRenderer tests; missing or unmatched sources
fail the build. It keeps separate
`obj-tests-critical-native` and `tests-critical-native` directories and does not
replace either full qualification test DLL. Mixed test files still compile in full;
only manifest CPU methods execute. Full qualification continues to build and verify
both complete test DLLs and every required actual renderer method.

Full qualification always starts with a new empty application shader-cache
directory, exercising cold preparation for both renderer dependencies. Later
cases may reuse those compiled shaders. Use `-ColdShaderCache` on a Focused
run to reproduce cold warmup; for a warm comparison set
`VP_SHADER_CACHE_DIRECTORY` to that run's cache directory. Driver-cache state
is not reset or claimed cold. Warm-only evidence cannot qualify a release.

Caption clipping while subtitle shaders are building is intentionally allowed
by the operator. Picture AR must remain independent of captions throughout;
after shader readiness, strict caption/cadence correctness still applies.

With `-Lane Native -Methods ...`, the whitelist contains only the translation
units that own those validated CPU methods, plus the PCH and any explicit required
production support. Omitting `-Methods` still compiles and runs the complete Native
manifest. Both forms use the same isolated iteration output and never grant full
runtime qualification; the TRX validator still requires exactly the selected tests.

For the broad GPU/cue suite, prefer full qualification followed by
`test_subtitle_motion_renderers.ps1 -UseQualifiedBuild`. The switch verifies the
matching source, required binaries and qualification evidence before creating a
results directory or invoking tests, then verifies qualification again at the end.
Missing or stale qualification fails closed; it never falls back to a rebuild.
All existing dependency hashes, selected classes and TRX checks remain required.
Its receipt records `reusedQualifiedBuild=true`, build mode
`reused-qualified-build` and a null build exit because no build ran. Without the
switch, the existing rebuild behavior remains and can invalidate qualification.

Shared shader-correctness fixtures retain only compiled program bytes, bounded
by adapter and feature level; each fixture still owns fresh GPU resources,
textures, frame history and assertions. Reconstruction/motion fixtures now use
the same program cache as subtitle composition fixtures. The preparation/cache
matrix starts with its own empty cache, then reuses compatible programs across
its three source-processing scenarios. Later scenarios are not independently
cold-start claims. Full runtime qualification still creates an empty application
cache for each run and exercises both renderer dependencies; cold startup and
post-readiness caption correctness remain required.
