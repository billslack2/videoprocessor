Fork: https://github.com/billslack2/libplacebo
Branch: codex/vp-master-beta
Base commit: 8bccd6578c9e845f77205cec9e85fdb97bf36ed7
Source state: uncommitted translated-ROI-copy patch; see source/VP-SOURCE-MANIFEST.txt.
Upstream: 1dd022687b9ac1dd996855b51433bf770ae76aab
API: 377
 74d8a6cb930c68ef617b202c3ff3c59d919e086b 3rdparty/Vulkan-Headers (v1.4.345)
 97b54ca9e75f5303507699d27c6b4f4efe4641a1 3rdparty/fast_float (v6.1.0-275-g97b54ca)
 73db193f853e2ee079bf3ca8a64aa2eaf6459043 3rdparty/glad (v0.1.11a-302-g73db193)
 15206881c006c79667fe5154fe80c01c65410679 3rdparty/jinja (3.1.6)
 297fc8e356e6836a62087949245d09a28e9f1b13 3rdparty/markupsafe (3.0.3)
 242f35efa067a46c595645eeda7b1771ea1f83b1 demos/3rdparty/nuklear (4.12.8)

Build: MSYS2 mingw64 GCC 16.1.0; Meson 1.11.2; clean x64 Release.
D3D11, shaderc, lcms2 and libdovi enabled; Vulkan/OpenGL backends disabled.
All 13 library tests passed. See VP-FORK.md in the complete source archive.

Binary SHA256: 8189e9d8c156c28dd6752a34d9a26946b46a0eeb9dc66ac246a396dd9b13d2df
Source archive SHA256: 928d5abf853b7ed466a33efd16554914e19a13ecdc8c5004edf6f62f57a23ced

All eight transitive DLLs remain byte-identical to the stable 7.360.1 bundle.
Stable and Beta have distinct DLL names, import libraries, headers and shader caches.
Gamut overflow correction (Experimental) defaults Off. It follows the original
gamut mapper and replaces the old fixed-Y/IPT LUT experiment. It may reduce
brightness or saturation. Uses the existing bool API and no environment switch.

Translated subtitle ROI copy (2026-10-19)
Patch SHA256: 8351e94d6dd32a11585d3bbcbe6ff7054d8972bf61bd83595addc06c6f99756f
Runtime: v7.377.0 (v7.360.0-159-g8bccd657-dirty)
Same-format, unscaled, unflipped normal 2D translated copies now use D3D11 CopySubresourceRegion.
Logged subtitle ROI shapes copy exact pixels without generating per-ROI shader variants.
All 13 native tests passed; RTX3080 and WARP covered. No public API/header/import-library change.
Other render passes may still compile new shaders. Live player/HDMI qualification remains open.
