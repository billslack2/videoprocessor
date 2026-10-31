# Required runtime build qualification

Release|x64 is the initially supported runtime configuration. A passing receipt
requires every test from AlphaSourceCropPolicyTests.cpp and
ActivePictureEvidenceTests.cpp, plus the six actual renderer Classic lifecycle
methods listed in critical_runtime_tests.json, in BOTH native dependency variants.
A successful compiler/linker result alone does not qualify a runtime build.

Run from a Visual Studio developer command environment:

```powershell
powershell.exe -NoProfile -File tools\qualify_runtime_build.ps1 `
  -MagewellSdkRoot E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3 `
  -MSBuildPath 'C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\MSBuild.exe'
```

The runner serially builds the native library, GUI, both renderer plugins and both
native test dependency variants. It always executes the required tests. It writes
artifacts/qualification/runtime-Release-x64.json only after success; logs and TRX
files are retained beneath artifacts/qualification/runs. No qualification bypass
switch exists. Child MSBuild context requires the runner's matching inherited
nonce and worktree root; setting a child property alone fails.

The receipt pins the Git commit, dirty source content fingerprint, manifest,
all runtime/test/dependency binary hashes, and both complete TRX files. Tracked
files and untracked nonignored source/test/gate files contribute to the fingerprint.
Git-ignored build products, logs and artifacts do not. Changing the source,
binaries or evidence invalidates the receipt. Packaging, including portable
payload generation, requires the receipt matching the exact canonical outputs.

Private captured movie frames are optional. An absent frame is explicitly recorded
as absent-not-claimed. A supplied frame is recorded as supplied-not-proven-exercised;
that status is never a claim of successful replay based solely on an environment
variable or a test's Passed outcome. Mandatory synthetic native contracts and
actual renderer lifecycle tests are required regardless of private frame presence.

Unsupported runtime configurations and IDE runtime builds need matching automated
test support before qualification is available. Use command-line Release|x64 in
the meantime; library/tool-only development builds remain available. Direct runtime
project builds must provide /p:SolutionDir=<absolute-worktree-root>\.

## Active build hooks

Directory.Build.targets imports tools/critical_runtime_build.targets, and
Directory.Solution.targets imports tools/critical_runtime_solution.targets. These
mandatory hooks are active. The project hook defers solution builds to the solution coordinator;
direct runtime project builds coordinate after linking. The solution hook runs
after Build/Rebuild or an explicitly selected runtime project. Distinct qualification
targets cover every runtime entry point and its Build/Rebuild aliases. A multiple
target invocation qualifies each requested runtime entry point in order, ensuring
its last target cannot invalidate an earlier receipt and still succeed. A normal
/t:Build qualifies once; explicit multiple runtime targets may run the gate more
than once. The coordinator
uses independent serial MSBuild processes, never recursively requests work from
the parent's project graph. Visual Studio does not execute solution MSBuild hooks,
so project hooks explicitly reject unsupported IDE runtime builds.

Metadata-only gate verification (no compilation or GPU work):

```powershell
powershell.exe -NoProfile -File tools\test_runtime_qualification.ps1
```
