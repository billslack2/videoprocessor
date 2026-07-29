# VP-0052: Lowercase runtime layout and `vprenderer` directory

## Status

In Progress. Implementation starts from `origin/v1.1.014-beta` after VP-0045.
The optional plugin project and binary will be renamed to
`VideoProcessor-VPRenderer` and `VideoProcessorVPRenderer.dll`; its C export
ABI will remain unchanged while the loader moves atomically to `vprenderer`.

Implementation is rebased onto the VP-0050 merge (`b2247df`) and pushed as
`0736243` on `codex/vp-0052-runtime-layout`. x64 Release build and 194/194
native tests pass. A filtered runtime payload is deployed for user validation:
`vprenderer` contains only the plugin/runtime DLLs, README, and licenses; the
previous active renderer directory and executable were retained as dated
`pre-VP0052` backups. `VideoProcessor.cfg` was not modified.

Coordinate this work after VP-0045 is integrated because both stories touch
the built-in renderer's project, configuration, help, and packaging references.

## User story

As a VideoProcessor user or release maintainer, I want every shipped
subdirectory to use a lowercase canonical name, the built-in renderer runtime
directory to be named `vprenderer`, and each asset to have one clear home, so
packages are predictable and do not contain duplicate shaders or development
artifacts.

## Current behavior

The current release/deployment layout can contain:

```text
VideoProcessor.exe
Shaders\
libplacebo\
  Shaders\
  third_party_licenses\
  VideoProcessorLibplacebo.dll
  VideoProcessorLibplacebo.exp
  VideoProcessorLibplacebo.lib
  VideoProcessorLibplacebo.pdb
  VideoProcessorShaderCache.bin
  <third-party runtime DLLs>
```

Observed deployment state includes duplicate `NLS.glsl` and `NLS.hlsl`
copies under `libplacebo\Shaders` in addition to the root `Shaders` directory.
The renderer directory also contains linker/debug outputs that are not runtime
requirements.

Source and build references currently use several first-party paths tied to
the implementation library name, including:

- root `Shaders`;
- `src\VideoProcessor-Lib\libplacebo`;
- `src\VideoProcessor-Libplacebo`;
- an output directory named `libplacebo`; and
- hard-coded runtime/cache paths beneath `libplacebo`.

## Canonical naming and scope

All directories shipped below the VideoProcessor application root must be
lowercase. The canonical runtime layout is:

```text
VideoProcessor.exe
shaders\
vprenderer\
  third_party_licenses\
```

The lowercase rule also applies to first-party source/asset directory paths
renamed by this story. At minimum:

- repository asset directory `Shaders` becomes `shaders`;
- first-party renderer implementation directory
  `src\VideoProcessor-Lib\libplacebo` becomes
  `src\VideoProcessor-Lib\vprenderer`; and
- the optional renderer's build/output/package directory becomes
  `vprenderer`.

Review whether the first-party plugin project directory and plugin binary
should be renamed to `VideoProcessor-VPRenderer` and
`VideoProcessorVPRenderer.dll` as part of the same coherent refactor. Prefer
the VP Renderer name for first-party artifacts, but record and test the plugin
ABI/loading impact before finalizing it.

The upstream dependency remains accurately named:

- `3rdparty\libplacebo` is not renamed;
- libplacebo headers continue to use their upstream `<libplacebo/...>` include
  namespace;
- upstream runtime DLL filenames are not disguised; and
- libplacebo and transitive dependency license names remain truthful.

This story does not require renaming every longstanding Visual Studio project
directory in the repository. It requires lowercase packaged/deployed
subdirectories and lowercase canonical first-party asset/renderer paths
affected by this cleanup.

## Single asset ownership

The root `shaders` directory is the only packaged location for user-selectable
shader source:

```text
shaders\NLS.hlsl
shaders\NLS.glsl
shaders\<other supported shader files>
```

Do not copy a second `Shaders`, `shaders`, or shader-source tree under
`vprenderer`. Both Alpha and external-renderer shader loaders must resolve
relative shader paths from the same canonical root.

Remove duplicate shader-copy items from Visual Studio projects, post-build
steps, package scripts, release manifests, documentation, and deployment
logic. A clean build followed by packaging must not recreate the nested copy.

## `vprenderer` runtime manifest

Define an explicit allowlist for files shipped under `vprenderer`. It may
contain only:

- the optional VP Renderer plugin DLL;
- DLLs required at runtime by that plugin;
- a concise runtime README when still useful;
- `third_party_licenses` and required license files; and
- renderer-generated runtime data in the deployed installation, such as the
  shader cache, when created after first use.

It must not package:

- `.exp` or import `.lib` linker outputs;
- `.pdb`, object, incremental-link, or other development/debug artifacts in
  the normal release package;
- shader source files or a nested shader directory;
- stale DLL versions;
- duplicate licenses or READMEs;
- test fixtures;
- configuration copies; or
- a prebuilt shader cache from the build machine.

If symbols are published separately, place them in a distinct symbol artifact,
not the runtime `vprenderer` directory.

The shader cache remains runtime-generated and must use a canonical path under
`vprenderer`. Upgrade/migration behavior must not copy an old cache into a
release artifact. An incompatible old cache may be ignored and rebuilt.

## Path and loader refactor

Update every first-party reference consistently:

- optional renderer loader path;
- shader-cache path;
- source includes and Visual Studio project/filter entries;
- plugin output and dependency-copy destinations;
- shader file defaults and path resolution;
- tests and test fixtures;
- package/release scripts;
- deployment and backup scripts;
- sample configuration and `CONFIGURATION.html`; and
- diagnostics that print canonical paths.

Do not retain duplicate fallback searches for old directory spellings in
normal runtime behavior. The canonical package and documentation should expose
only `shaders` and `vprenderer`.

Alpha must remain optional: if `vprenderer` or its plugin DLL is absent, VP
starts normally and silently omits Alpha from the renderer list, while logging
one clear diagnostic for troubleshooting.

## Windows-safe rename requirements

Because Windows commonly uses a case-insensitive filesystem, perform
case-only Git renames through a temporary intermediate name:

```text
Shaders -> __shaders_rename -> shaders
```

Use tracked `git mv` operations and verify the final Git tree from a fresh
checkout. Do not rely on a working-tree casing change that Git fails to record.

The implementation branch must start from the developer-confirmed integration
branch after VP-0045. Preserve unrelated working-tree changes and do not
perform the rename in a dirty checkout.

## Packaging and deployment cleanup

1. Build into a clean output directory.
2. Generate the runtime package from an explicit manifest rather than copying
   an entire Visual Studio output folder.
3. Verify the package contains only lowercase subdirectories.
4. Verify each shader source appears exactly once under `shaders`.
5. Verify `vprenderer` matches its runtime allowlist exactly.
6. Deploy by backing up affected configuration/runtime files and making the
   smallest required path changes.
7. Remove obsolete duplicate directories from the deployed application only
   after confirming the new package starts and loads both renderer paths.

Do not overwrite the user's active configuration wholesale. Update only path
values that actually changed.

## Diagnostics

At startup, log:

- canonical application root;
- canonical shared shader root;
- attempted optional plugin path;
- whether Alpha is available;
- shader-cache path; and
- any unexpected file rejected by package verification, when running the
  packaging check.

Runtime logs should use `VP Renderer` or `Alpha` for the first-party component.
They may name libplacebo where identifying the actual third-party library,
version, DLL, or error source is technically relevant.

## Verification

### Automated

- A repository/path test rejects uppercase packaged subdirectory names.
- A package-manifest test rejects files not on the `vprenderer` allowlist.
- A duplicate-asset test verifies every packaged shader has one canonical
  `shaders` path.
- Source/project validation finds no first-party `Shaders\` path and no
  first-party renderer implementation/output path still targeting
  `libplacebo`.
- Third-party dependency paths and license references continue to resolve from
  `3rdparty\libplacebo`.
- Alpha is discovered from `vprenderer` when present and omitted safely when
  absent.
- Both renderers resolve the same relative shader configuration from root
  `shaders`.
- Cache creation and reuse occur under `vprenderer` and no cache is included in
  a fresh package.

### Build and runtime

- Perform a clean x64 Release build from a fresh checkout to prove case-only
  renames are tracked.
- Run the full native test suite.
- Inspect the clean Release output and packaged archive against the manifest.
- Start VP with `vprenderer` present, select Alpha, load a GLSL shader, restart,
  and confirm warm cache use.
- Select an external renderer and load its shader from the same root
  `shaders` directory.
- Start VP with the complete `vprenderer` directory removed and confirm normal
  operation without Alpha.
- Test upgrade deployment over an installation containing old `Shaders`,
  `libplacebo`, and nested `libplacebo\Shaders` directories; confirm the final
  active tree has only canonical lowercase directories and no duplicate assets.

## Acceptance criteria

- Every subdirectory in the shipped application tree is lowercase.
- The canonical shared asset directory is `shaders`.
- The optional built-in renderer runtime directory is `vprenderer`.
- First-party renderer source/build references use the agreed VP Renderer
  naming rather than an internal `libplacebo` directory.
- `3rdparty\libplacebo`, upstream headers, runtime filenames, and licenses
  retain their truthful dependency names.
- Shader sources exist only once, under root `shaders`; there is no nested
  renderer shader directory.
- `vprenderer` contains only manifest-approved runtime dependencies, licenses,
  optional README, and runtime-generated data.
- Normal release packages contain no `.exp`, `.lib`, `.pdb`, test files,
  duplicate assets, or prebuilt shader cache under `vprenderer`.
- Alpha remains optional and external-renderer operation is unaffected.
- Clean x64 Release build, tests, package verification, and both renderer
  smoke tests pass.

## References

- VP-0045: Namespace built-in renderer configuration as `vpvr`
- VP-0049: Complete canonical `CONFIGURATION.html` reference
- VP-0051: Generic Alpha shader-chain support
- root `Shaders`
- `src\VideoProcessor-Lib\libplacebo`
- `src\VideoProcessor-Libplacebo`
- `3rdparty\libplacebo`
- optional renderer loader and Release packaging scripts
