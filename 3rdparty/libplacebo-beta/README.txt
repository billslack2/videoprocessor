Fork: https://github.com/billslack2/libplacebo
Branch: codex/vp-master-beta
Commit: 7091553e68bed28e5a39a82af298ca681511fd5a
Upstream: 1dd022687b9ac1dd996855b51433bf770ae76aab
API: 376
 74d8a6cb930c68ef617b202c3ff3c59d919e086b 3rdparty/Vulkan-Headers (74d8a6c)
 97b54ca9e75f5303507699d27c6b4f4efe4641a1 3rdparty/fast_float (97b54ca)
 73db193f853e2ee079bf3ca8a64aa2eaf6459043 3rdparty/glad (73db193)
 15206881c006c79667fe5154fe80c01c65410679 3rdparty/jinja (1520688)
 297fc8e356e6836a62087949245d09a28e9f1b13 3rdparty/markupsafe (297fc8e)
 242f35efa067a46c595645eeda7b1771ea1f83b1 demos/3rdparty/nuklear (242f35e)

Build: MSYS2 mingw64 GCC 16.1.0; Meson 1.11.2; x64 Release.
D3D11, shaderc, lcms2 and libdovi enabled; Vulkan/OpenGL backends disabled.
See VP-FORK.md in the complete source archive for commands and patch inventory.

Binary SHA256: 25616C215E2AE9A68643595ED55E48370008D6A441F18D4F8F29F818A6862149
Source archive SHA256: 1AA14CDFC000AF62C1A362118A84FE77F9DFC3BE87DB9F4B3462A4E8F0866898

All eight transitive DLLs are byte-identical to the stable 7.360.1 bundle.
Keep these runtimes synchronized if updating either backend.
Stable and Beta have distinct DLL names, import libraries, headers and shader caches.
