# VideoProcessor release layout

`packaging/release-manifest.json` is the release allowlist. Run
`tools/package_release.ps1` after a successful x64 Release solution build. The
script always recreates its output under `artifacts\`, copies only manifest
entries, and rejects symbols, import libraries, backups, caches, duplicate
private DLLs, and unexpected files.

## Canonical tree

```text
  VideoProcessor\
  START-HERE.txt
  SETUP-RUNTIME.cmd
  prerequisites\
    vc_redist.x64.exe
    setup-runtime.ps1, runtime-common.ps1, runtime-requirement.json
  VideoProcessor.exe
  VideoProcessor.cfg.example
  RELEASE-LAYOUT.md
  RELEASE-MANIFEST.json
  logs\                         (intentionally empty)
  docs\
    CONFIGURATION.html
    VideoProcessor_NLS_Configuration_Guide.pdf
  config\
    VideoProcessorConfig.exe
    VideoProcessorConfigDiscovery.dll
    Qt6*.dll, opengl32sw.dll
    generic\, iconengines\, imageformats\, networkinformation\
    platforms\, styles\, tls\
  shaders\
    Adaptive sharpen.hlsl, Debanding mild.hlsl, Denoise.hlsl
    Invert.hlsl, NLS.hlsl, NLS.glsl
  vprenderer\
    VideoProcessorVPRenderer.dll
    libplacebo-360.dll and its private imported DLLs
    README.txt
    third_party_licenses\
```

The application root contains the native MFC host, operator data and shared
assets, but no Qt or Config-only binaries. Operator-facing configuration
documentation is grouped under `docs\`; the configuration HTML and NLS PDF
must not be staged at the release root. `config\` owns the complete
configuration editor process: Windows resolves its normal Qt imports beside
`VideoProcessorConfig.exe`, and Qt discovers its typed plugin directories below
that same private directory. The host launches Config by the absolute
installation-relative `config\VideoProcessorConfig.exe` path and passes the
active root configuration explicitly. A direct Config launch also checks its
parent installation for `VideoProcessor.cfg`.

## Microsoft runtime setup

Run **SETUP-RUNTIME.cmd** after extracting or updating the ZIP and before launching
VP or Config. It checks the native x64 system runtime files against the generated
`prerequisites/runtime-requirement.json`. A sufficient runtime is left alone;
otherwise setup verifies and runs the bundled official Microsoft x64 installer
with `/install /passive /norestart`. Windows requests administrator approval.
Exit 3010 means restart Windows and rerun setup before starting VP. Installation
and restart are never required merely to inspect with `SETUP-RUNTIME.cmd -CheckOnly`.

The current dependency policy floor is **14.44.35211.0**, covering the Qt editor's
14.44 toolset and the constexpr-mutex behavior exposed by configuration caching.
A DLL with the same name from 14.29 is insufficient. The main application using
v142 does not lower the editor's runtime requirement.

`Directory.Build.targets` records the selected `VCToolsVersion` and SHA-256 beside
each x64 Release binary. Packaging verifies all four shipped VP binary records,
then raises the policy minimum to cover any newer build toolset or PE linker
family found across **all** shipped native binaries, including Qt. The included
Microsoft-signed installer must meet that calculated minimum. Its version and
SHA-256 are recorded in the generated requirement JSON. Update the policy floor
when third-party headers/binaries require a newer runtime patch: a PE header
cannot tell us the full compiler/header version used for a prebuilt dependency.

Setup reports old app-local Microsoft runtime DLLs that could shadow the system
runtime, and asks the user to extract into a clean folder. It does not remove DLLs,
modify VP settings, start VP, or downgrade a sufficient runtime. For managed
machines where scripts are blocked, an administrator can run the included
`prerequisites/vc_redist.x64.exe` and then recheck. The setup command uses the
Windows PowerShell included with Windows; PowerShell 7 is not required.

The official installer is packaged as a prerequisite; loose Microsoft runtime
DLLs and Windows/API-set libraries are never copied from a development machine.
Microsoft documents [central runtime deployment and installer options](https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files)
and provides the [current official x64 redistributable](https://aka.ms/vc14/vc_redist.x64.exe).
Use the installer supplied by a licensed Visual Studio installation or downloaded
from Microsoft's official location.

## Optional renderer contract

`vprenderer\` is the only supported optional-renderer directory. The host loads
`vprenderer\VideoProcessorVPRenderer.dll` by absolute path with
`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`. Consequently
the plugin's libplacebo, Shaderc, SPIRV-Cross, GCC, libdovi, Little CMS,
winpthreads, and Vulkan imports resolve only beside the plugin or from System32;
they must not appear in the application root or be accepted from `PATH`.

The host and plugin form one versioned pair. Omitting the entire `vprenderer\`
directory safely makes VP Renderer unavailable. If the directory is present but
incomplete, startup diagnostics identify the exact missing private file before
Windows attempts to load the plugin.

## Shaders and mutable data

Both DirectShow/madVR and VP Renderer resolve configured shader filenames from
the executable-relative `shaders\` directory. That is the sole packaged shader
tree. There is no `vprenderer\shaders\` fallback or duplicate.

`vprenderer\VideoProcessorShaderCache.bin`, `VideoProcessor.state`, log files,
and configuration are mutable operator data. They are not release inputs. The
empty `logs\` directory is created in every release for operator clarity; the
logger independently creates it on startup when it is absent. The manifest
stages the source configuration as `VideoProcessor.cfg.example`, so copying a
release cannot overwrite an active `VideoProcessor.cfg` implicitly.

## Commands

```powershell
# First complete an x64 Release solution build, which emits *.runtime.json records.
# Set this to the official installer from your Visual Studio Redist directory
# or Microsoft's official download. Never take it from an arbitrary DLL site.
$vcInstaller = 'C:\path\to\vc_redist.x64.exe'

# Validate all inputs, signatures, versions, and build records without writing.
.\tools\package_release.ps1 -VcRedistPath $vcInstaller -DryRun

# Exercise regression checks, then recreate the complete verified release tree.
.\tools\test_runtime_packaging.ps1 -VcRedistPath $vcInstaller
.\tools\package_release.ps1 -VcRedistPath $vcInstaller
.\artifacts\release\VideoProcessor\SETUP-RUNTIME.cmd -CheckOnly

# ZIP the entire generated tree, including START-HERE and prerequisites.
Compress-Archive -Path .\artifacts\release\VideoProcessor\* -DestinationPath .\artifacts\VideoProcessor-x64-Release.zip
```

`x64\Release` is a build output, not a distributable directory: unit-test
projects intentionally place test binaries and libplacebo test dependencies
there. Only the manifest-generated staging tree is a release.

## Release qualification

Run the packaged setup on clean Windows without Visual Studio, once with a missing
or older runtime and once with a sufficient runtime. Verify installation (including
UAC denial/failure and a requested restart), and confirm the sufficient case skips
installation. Open packaged Config with a disposable configuration, change a value,
Apply, OK, reopen, and verify persistence while VP is closed. Check that setup leaves
existing configuration/state files unchanged. Record actual results and distinguish
automated prerequisite checks from a real clean-machine installer run.
