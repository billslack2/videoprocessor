Fork: https://github.com/billslack2/libplacebo
Branch: codex/vp-master-beta
Commit: a89f8241bfd6c5f5affaa9c3ce39fa2b1767b8c7
Upstream: a83aaeb4e60fdd7ed01619885f4f42bb0ef34eaa (API 387)
API: 388; fix version: 0; PE architecture: x86-64
Source state: clean committed source, including the previously shipped translated-ROI-copy patch.
Runtime pl_version(): v7.388.0 (v7.387.0-12-ga89f8241)
DLL: libplacebo-388.dll
DLL SHA256: 93a9292d31accf606dc9d5289225138e55942a9b03978401320137aaa9ecbb88
Source archive: libplacebo-7.388.0-vp-source.zip
Source archive SHA256: 66a63ad2a2dbcc7fc17ce2483f5b03db4b07b55163f70291a48fa74340b3dbc9
Build: MSYS2 mingw64 GCC 16.1.0; Meson 1.11.2; Ninja 1.13.2; x64 Release -O3.
D3D11, shaderc, lcms2 and libdovi enabled; Vulkan/OpenGL and glslang disabled.
Dependencies: shaderc 2026.2.1; SPIRV-Cross 0.68.0; lcms2 2.19; libdovi 3.4.0.
All 15 native library suites passed, including D3D11 hardware/WARP, ROI copy,
analysis crop, timers, and original/custom/original gamut restoration.
All eight transitive runtime DLLs remain byte-identical to stable.
Pinned submodules:
74d8a6cb930c68ef617b202c3ff3c59d919e086b 3rdparty/Vulkan-Headers (v1.4.345)
 97b54ca9e75f5303507699d27c6b4f4efe4641a1 3rdparty/fast_float (v6.1.0-275-g97b54ca)
 73db193f853e2ee079bf3ca8a64aa2eaf6459043 3rdparty/glad (v0.1.11a-302-g73db193)
 15206881c006c79667fe5154fe80c01c65410679 3rdparty/jinja (3.1.6)
 297fc8e356e6836a62087949245d09a28e9f1b13 3rdparty/markupsafe (3.0.3)
 242f35efa067a46c595645eeda7b1771ea1f83b1 demos/3rdparty/nuklear (4.12.8)

Upstream refresh: 2026-10-29. Includes gamut LUT range clamping (ed5d8d70)
and neutral chroma code correction (4828f628). VP gamut overflow correction
remains experimental and defaults Off; it is preserved after the upstream stage.
Stable 7.360.1 remains unchanged. API 388 headers/import library and DLL must
be consumed together; do not rename this DLL to libplacebo-377.dll.
The historical translated ROI patch is retained as provenance and is already
committed in this source; do not apply it again. Full source includes submodules.
Library tests are not live player/HDMI or perceptual gamut qualification.
