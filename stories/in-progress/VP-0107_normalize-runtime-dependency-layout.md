# VP-0107: Normalize runtime dependency layout and plugin-private libraries

## Status

In progress (started 2026-08-11). Deployment inspection found runtime DLLs both
beside the executables and in the legacy `vprenderer\` directory, plus duplicate
shader asset trees under the VP Renderer payload. The deployed tree may also
contain libraries left by older packaging passes that are no longer imported or
loaded. The active deployment uses DirectShow/madVR, but the optional renderer
payload must remain launchable when selected. Implementation is proceeding on
`codex/vp-0107-runtime-layout` from default branch `v1.2.001-beta` at
`8e0ea86`, beginning with the required import, loader, asset, and staging
inventory.

## User story

As a VideoProcessor installer and operator, I want the deployed runtime to
have one intentional, documented DLL layout so an optional renderer can carry
its private dependencies without cluttering the application directory or
accidentally loading a wrong DLL.

## Current evidence

- `C:\Videoprocessor\vp` contains Qt runtime DLLs, the standalone
  configuration editor, and a flat duplicate of the libplacebo dependency
  stack.
- `C:\Videoprocessor\vp\vprenderer\` contains the legacy optional renderer
  (`VideoProcessorVPRenderer.dll`) plus the same libplacebo/Shaderc/GCC/Vulkan
  dependency family, shaders, licenses, cache, and many historical DLL backup
  copies.
- Current source stages the newer optional renderer as
  `libplacebo\VideoProcessorLibplacebo.dll`; its loader uses an absolute path
  and `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`, which
  is the desired private-dependency loading model.
- Qt6Core/Gui/Widgets and their direct dependencies cannot simply move to a
  generic `lib\` directory: Windows resolves normal executable imports before
  application startup code can establish a custom DLL search path. Keep the
  Qt core DLLs beside `VideoProcessorConfig.exe` unless the packaging model is
  deliberately redesigned. Qt plugins already belong in their typed folders
  such as `platforms\`, `imageformats\`, `styles\`, and `tls\`.

## Scope

1. Define and document the canonical release layout for application binaries,
   Qt runtime/plugins, optional renderer plugins, plugin-private DLLs, shader
   assets, licenses, and mutable caches.
2. Select one canonical optional-renderer directory and migrate the legacy
   `vprenderer\VideoProcessorVPRenderer.dll` payload to the current plugin
   contract, or deliberately retain a compatible legacy loader with an
   explicit retirement plan. Do not leave two active layouts.
3. Stage every libplacebo-family dependency exactly once beside its plugin,
   not in the application root. This includes `libplacebo`, Shaderc,
   SPIRV-Cross, libdovi, GCC runtime, winpthreads, and Vulkan loader files.
4. Audit `VideoProcessorConfigDiscovery.dll` and every other root DLL to
   determine whether it is a normal executable import, a plugin dependency,
   or an obsolete deployment artifact; relocate only after its loader contract
   is explicit.
5. Update Release packaging/deployment scripts to produce the layout from a
   clean staging directory, preserve third-party license obligations, and
   avoid copying historical `.bak`, `.pdb`, `.lib`, `.exp`, or cache artifacts
   into a release.
6. Inventory every shipped DLL, library, Qt plugin, shader file/tree, and other
   runtime asset. Record its owner, source package/version, destination,
   consumer, and proof that it is required at runtime; remove artifacts with no
   demonstrated consumer instead of carrying them forward speculatively.
7. Resolve the duplicate shader folders under `vprenderer\`. Establish one
   canonical shader source and staged destination, update lookup paths as
   needed, and prove that built-in and configured shaders still load without a
   fallback duplicate copy.
8. Make packaging deterministic and stale-file-safe: the packaged result must
   come from an empty staging directory and be defined by an explicit manifest
   or equivalent allowlist, not by copying the contents of a prior deployment.

## Non-goals

- Do not change the active `VideoProcessor.cfg` or overwrite user settings.
- Do not move Qt core DLLs into a generic directory without a separately
  validated bootstrap/loading design.
- Do not change renderer behavior, shader semantics, or graphics APIs merely
  to reorganize files.

## Acceptance criteria

- A documented release-tree manifest classifies every shipped DLL and mutable
  artifact by owner, source/version, consumer, destination, and load mechanism.
- Every shipped DLL and library has positive import/load evidence or a
  documented runtime reason to remain. Files found only in an old deployment,
  build output, PATH, or developer machine are not silently included.
- The root contains only executable-adjacent dependencies that Windows must
  resolve before application code runs; plugin-private libraries are present
  only in the canonical plugin directory.
- Exactly one packaged copy of each VP Renderer shader asset exists. Shader
  discovery and compilation succeed from a clean extracted package with no
  source tree, build tree, prior deployment, or duplicate shader directory
  available.
- Selecting the optional renderer succeeds with its plugin installed and is
  safely unavailable when its entire directory is absent; loading never falls
  back to a same-named root/PATH DLL.
- `VideoProcessor.exe` and `VideoProcessorConfig.exe` launch from a clean x64
  Release staging tree, including Qt platform/plugin discovery.
- A deployment dry run proves the active configuration remains untouched and
  release staging contains no historical DLL backups, build symbols, or stale
  duplicate private libraries.
- A clean-machine-equivalent smoke test launches the main application and
  configuration editor, discovers capture/render devices, and exercises both
  DirectShow and VP Renderer selection from the packaged tree. Missing required
  dependencies fail with an actionable file/path diagnostic rather than a dead
  launch.
- Focused loader/layout tests and an x64 Release build pass.

## Dependencies and next action

Before implementation, inventory PE import tables for both executables and the
renderer plugin; trace dynamic loads and asset lookup paths during launch and
both renderer selections; compare the source staging rules, a fresh Release
staging tree, and the deployed tree; decide whether the canonical plugin
directory is `vprenderer\` or `libplacebo\`; and confirm the default integration
branch under the tracker workflow gate.
