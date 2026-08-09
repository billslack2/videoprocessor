# VP-0107: Normalize runtime dependency layout and plugin-private libraries

## Status

Backlog (2026-08-09). Deployment inspection found runtime DLLs both beside the
executables and in the legacy `vprenderer\` directory. The active deployment
uses DirectShow/madVR, but the optional renderer payload must remain launchable
when selected. No implementation branch or worktree has been created.

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

## Non-goals

- Do not change the active `VideoProcessor.cfg` or overwrite user settings.
- Do not move Qt core DLLs into a generic directory without a separately
  validated bootstrap/loading design.
- Do not change renderer behavior, shader semantics, or graphics APIs merely
  to reorganize files.

## Acceptance criteria

- A documented release-tree manifest classifies every shipped DLL and mutable
  artifact by owner and load mechanism.
- The root contains only executable-adjacent dependencies that Windows must
  resolve before application code runs; plugin-private libraries are present
  only in the canonical plugin directory.
- Selecting the optional renderer succeeds with its plugin installed and is
  safely unavailable when its entire directory is absent; loading never falls
  back to a same-named root/PATH DLL.
- `VideoProcessor.exe` and `VideoProcessorConfig.exe` launch from a clean x64
  Release staging tree, including Qt platform/plugin discovery.
- A deployment dry run proves the active configuration remains untouched and
  release staging contains no historical DLL backups, build symbols, or stale
  duplicate private libraries.
- Focused loader/layout tests and an x64 Release build pass.

## Dependencies and next action

Before implementation, inventory PE import tables for both executables and
the renderer plugin, decide whether the canonical plugin directory is
`vprenderer\` or `libplacebo\`, and confirm the default integration branch
under the tracker workflow gate.
