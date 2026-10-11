Fork: https://github.com/billslack2/libplacebo
Branch: codex/vp-master-beta
Commit: 8bccd6578c9e845f77205cec9e85fdb97bf36ed7
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

Binary SHA256: 19023e283d1ff21a3b5a13e2754e091cc34231dc06c8a0818c5b75953dcb3722
Source archive SHA256: fa3acffa6266091520cbdb9becc18e672b109cd5a3b13a85f8984130489a68f4

All eight transitive DLLs remain byte-identical to the stable 7.360.1 bundle.
Stable and Beta have distinct DLL names, import libraries, headers and shader caches.
Gamut overflow correction (Experimental) defaults Off. It follows the original
gamut mapper and replaces the old fixed-Y/IPT LUT experiment. It may reduce
brightness or saturation. Uses the existing bool API and no environment switch.
