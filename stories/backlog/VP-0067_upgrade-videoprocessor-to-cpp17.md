# VP-0067: Upgrade VideoProcessor to C++17

## Status

Backlog. This is a build-system and language-standard upgrade. It should be
implemented only after recording the current compiler, language-standard,
runtime-library, third-party ABI, and test baseline.

## User story

As a VideoProcessor developer, I want the solution to use the C++17 language
standard consistently, so the codebase can use standardized modern C++
facilities and establish a supported baseline for future refactoring without
introducing inconsistent project settings or runtime incompatibilities.

## Objective

Upgrade every VP project and relevant build configuration from its current C++
standard to C++17, using the existing supported x64 Visual Studio toolchain,
while preserving runtime behavior, binary compatibility at the renderer-plugin
boundary, release packaging, and test results.

The upgrade must be explicit and reproducible. It must not be implemented as a
compiler-only switch in one project while dependent projects continue using a
different language mode.

## Investigation prerequisites

Before changing project files, record:

- the current `LanguageStandard`/`/std` setting for every solution project and
  configuration;
- the Visual Studio/MSVC version used by the supported build;
- runtime-library settings (`/MD`, `/MDd`, or equivalent) and architecture;
- the C++ standard used by any external renderer/plugin ABI headers;
- FFmpeg, libplacebo, NVAPI, DirectML/ONNX, DeckLink SDK, and other bundled
  dependency build assumptions;
- current x64 Release and Debug build results;
- native test count/results and warning policy; and
- release/deployment artifacts that must remain byte- or interface-compatible.

If any dependency or plugin boundary cannot safely be compiled/consumed under
C++17, create a bounded compatibility spike before implementation and keep
this story in Backlog.

## Scope

Update the solution, project files, shared property sheets, test projects,
custom build scripts, and CI/build documentation so all VP-owned C++ targets
use C++17 consistently. Verify that C++/CLI, COM, DirectShow base classes,
DeckLink SDK headers, libplacebo headers, and Windows SDK headers continue to
compile without changing ownership or calling conventions.

Review code exposed across the main executable and optional renderer DLL. Do
not introduce new ABI-affecting public types merely because C++17 is enabled;
preserve existing exported C interfaces and plugin contracts.

## Compatibility requirements

- Preserve x64 Release as the supported deployment configuration.
- Preserve Debug builds and test builds where currently supported.
- Preserve the selected MSVC runtime-library mode and CRT ownership rules.
- Do not change structure packing, calling conventions, exception boundaries,
  COM ownership, or exported symbol contracts without a separate decision.
- Keep third-party prebuilt libraries and DLLs compatible; rebuild only when
  required and document the reason and resulting hashes.
- Do not silently enable newer C++20/23 behavior in only part of the tree.
- Avoid unrelated source modernization during the standard upgrade.

## Validation plan

1. Build the complete x64 Release solution before the change and retain the
   baseline output and test results.
2. Apply C++17 settings to all VP-owned projects and verify generated project
   files do not override the setting for individual configurations.
3. Build Debug and x64 Release, including the optional renderer DLL and all
   native tests.
4. Run the full native test suite and compare failures, warnings, and test
   count with the baseline.
5. Exercise startup with the optional renderer DLLs absent and present.
6. Validate capture, DirectShow/external-renderer startup, Alpha/libplacebo
   startup, renderer switching, refresh switching, HDR/SDR transitions,
   queue reset, OSD, shaders, subtitles, and persisted profile state.
7. Confirm that release packaging, deployed executable loading, and the
   renderer-plugin ABI remain correct.
8. Record compiler version, `/std` settings, build commands, test results, and
   any accepted warning changes in the story.

## Acceptance criteria

- Every VP-owned C++ project uses C++17 in all supported configurations.
- A clean x64 Release build succeeds with the supported Visual Studio
  toolchain.
- Debug and test builds succeed where they did before the upgrade.
- The complete native test suite passes with no unexplained regressions.
- Main executable and optional renderer DLLs load and communicate correctly.
- Capture and renderer behavior is unchanged in the validation matrix.
- No unintended CRT, COM, DirectShow, DeckLink, libplacebo, or plugin ABI
  changes are introduced.
- Build scripts and developer documentation state the new C++17 requirement.
- The final diff contains no unrelated modernization or behavior changes.

## Out of scope

Adopting C++20 or later; redesigning the renderer or live pipeline; changing
queue/timing policies; replacing third-party libraries; changing compiler
optimization levels; broad warning cleanup; and opportunistic refactoring not
required to compile under C++17.

## Definition of done

The C++17 setting is consistent and reproducible across the solution, Release,
Debug, and test validation is complete, plugin and third-party compatibility
is documented, deployment artifacts are verified, and any required follow-up
work is captured in separate stories.
