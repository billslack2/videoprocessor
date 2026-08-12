# VideoProcessor release layout

`packaging/release-manifest.json` is the release allowlist. Run
`tools/package_release.ps1` after a successful x64 Release solution build. The
script always recreates its output under `artifacts\`, copies only manifest
entries, and rejects symbols, import libraries, backups, caches, duplicate
private DLLs, and unexpected files.

## Canonical tree

```text
VideoProcessor\
  VideoProcessor.exe
  VideoProcessorConfig.exe
  VideoProcessorConfigDiscovery.dll
  VideoProcessor.cfg.example
  CONFIGURATION.html
  RELEASE-LAYOUT.md
  RELEASE-MANIFEST.json
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

The application root contains only process images, normal pre-main Config
imports, the Config discovery module loaded by an absolute application-relative
path, and Qt's executable-adjacent runtime. Microsoft C/C++ and MFC runtimes are
provided by the VC v143 Redistributable; Windows/API-set libraries come from the
operating system and are never copied from a development machine.

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

`vprenderer\VideoProcessorShaderCache.bin`, `VideoProcessor.state`, logs, and
configuration are mutable operator data. They are not release inputs. The
manifest stages the source configuration as `VideoProcessor.cfg.example`, so
copying a release cannot overwrite an active `VideoProcessor.cfg` implicitly.

## Commands

```powershell
# Validate sources and print the exact plan without writing anything.
.\tools\package_release.ps1 -DryRun

# Recreate and verify artifacts\release\VideoProcessor.
.\tools\package_release.ps1
```

`x64\Release` is a build output, not a distributable directory: unit-test
projects intentionally place test binaries and libplacebo test dependencies
there. Only the manifest-generated staging tree is a release.
