# RC4 versus current subtitle shader preparation

Read-only source/log audit, 2026-11-04. No player restart, cache clearing, GPU test or new timing experiment. Published RC4 source pins are6d5f8ad4241c24b750b96d2e3f54abb458bc7986 andfa4e210db426b086e2ab4ae5d590a10112c4c09c; currentae467d578c272b5a4aaa59d7d41a6e53df194659. Neither RC4 label is substituted for an independently measured working binary.

## Established differences

Both RC4 sources create/bind the selected subtitle hook on demand in LibplaceboVideoRenderer.cpp. Neither contains SubtitlePipelinePreparation.h or its all-profile readiness gate. Current preparation, introduced in558c35b9, serially warms backgrounds3/5, blur off/on, and full/cleanup/layer graphs:12 composition invocations for progressive input,48 for interlaced field+algorithm combinations, plus normalization/presentation. These are invocations, not12/48 guaranteed unique driver shaders. Both advanced profiles remain unavailable until the entire request finishes. Style changes no longer revoke readiness once prepared.

The current GLSL generator has a128-row balanced selection tree,256 dynamic left/right uniforms and associated cleanup predicates, absent from both RC4 sources. Commit9cc105e9 introduced it to preserve rounded current backing spans and corners; later243f1b47 reused cleanup membership. This is a concrete graph-complexity regression candidate, not a measured attribution. Removing its safety semantics is not proposed.

Default persistent cache paths in both RC4 and current are executable-relative and variant-specific. Current also supports an explicit absolute cache directory; runtime load validates bounds/format and libplacebo compatibility. Our isolated preview launcher copied only the28 runtime files and current cfg/state into a new per-pin folder, omitting caches. Consequently new previews repeatedly started cold. This is an identified packaging cause, not a presumed GPU-driver problem. Same-folder retirement/recreation does load saved cache (3f:22objects/496040bytes).

## Actual current timing

Preserved3f log: first background3/blur0/full20,000.43ms; background5/blur1/full24,278.30ms; complete44,511.08ms. Other composition invocations mostly1–10ms. Currentae467 log explicitly reports no cache file, then Black/full19,519.45ms and Reconstruction/blur/full28,012.94ms, total47,760.50ms. Thus two heavy graphs dominate the wait, not all12 invocations equally. No matched cold/warm RC4 GPU timing was found in the existing comparison artifacts; do not state an exact RC4 speedup factor.

## Narrow next actions and limits

The artifact launcher now copies only known per-variant cache files after graceful close/SaveCache, requiring identical libplacebo binary hashes, non-reparse bounded paths,16B..256MiB size and pl_cache version1 header. It records source/cache/dependency hashes; runtime content-addressed lookup and pl_cache_load retain final compatibility checks. It does not copy clear requests, settings or arbitrary directories. Current running ae467 was not restarted; this is a future-launch correction, not an already measured warm-start improvement.

Separate production proposal: publish readiness for the requested complete pipeline once its required graph is prepared; warm other profiles afterward without making them prerequisites. Preserve exact pipeline identity and bounded off-thread preparation. This reduces unrelated profile gating but will not itself remove the19–28second cold cost of a requested heavy graph. Investigate a semantically equivalent compact representation of the row contour and compare shader source/compile costs under identical cold cache/driver/input. Keep original current-pixel/corner protection and actual frame advancement tests. Fullscreen compiler-join retirement remains a distinct open issue.
