libplacebo 7.360.1
==================

This directory contains the headers and Windows x64 runtime used to build the
optional VideoProcessorVPRenderer.dll renderer plugin. The normal
VideoProcessor executable does not import libplacebo.

Upstream project: https://github.com/haasn/libplacebo
Version: 7.360.1 plus local VP analysis-crop and D3D11 timer controls
Local source branch: codex/pr75-total-gpu-timing
Local source commit: 3f5bce24 (based on c3a3d203 and the v7.360.1 tag)
Review repository: https://github.com/billslack2/libplacebo
Published base branch: codex/vp0147-analysis-roi-v7
Current-upstream review branch: codex/vp0147-analysis-roi
libplacebo-360.dll SHA-256: FEFC9B85A521C09025298754959D1A68EB00FEF9FF343C65EEAFE1A9EDBBFAD5
Corresponding source: source/libplacebo-7.360.1-vp-source.zip
Source archive SHA-256: 2F0FE14F51FC433B9406BF673C09605EFEFD581FFF8F5842517A268CC12EA44B
License: LGPL-2.1-or-later (see LICENSE.txt)

The libplacebo DLL was built locally as an x64 Release binary with D3D11,
shaderc, and built-in Dolby Vision support enabled. Its local-only change adds
pl_peak_detect_params.analysis_crop so VP can restrict peak, average,
histogram, and scene-change analysis to a normalized active-picture region.
It also adds a D3D11 backend switch which prevents libplacebo from allocating
timer queries while VP owns one frame-scoped timestamp-disjoint interval. The
analysis-crop base is published only to the billslack2 review fork. The complete
source corresponding to this DLL, including the timer control commit and build
files, is bundled in the source archive named above. No pull request, merge
request, issue, or other submission has been made against upstream libplacebo.
The local upstream remote remains fetch-only with pushing disabled.
The libplacebo DLL is dynamically linked so it remains independently
replaceable. The accompanying DLLs are runtime dependencies of that binary.
Their license texts are retained under the licenses directory.

The MSVC import library was generated from the exported symbols of
libplacebo-360.dll; it contains no libplacebo implementation code. Built plugin
packs are staged under x64\<configuration>\vprenderer so the normal executable
can be distributed independently.
