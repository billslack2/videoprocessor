# RC4 subtitle basis comparison

Repository of record: billslack2/videoprocessor. The public RC4 release currently retains two package families:
- Original:6d5f8ad4241c24b750b96d2e3f54abb458bc7986.
- Updated/newest:fa4e210db426b086e2ab4ae5d590a10112c4c09c; release body explicitly identifies this source and signed update sequence20261012181510.

The user's RC4 label alone does not distinguish these packages. Both are compared on the unchanged89 native frames; neither package is launched. `published-release.json` records repository API asset digests, source references and URLs. Actual bytes/hashes and component receipts are owned by the adjacent `rc4-comparison` experiment.

Both archived RC4 SubtitleBoxLookahead.h shared-required paths (lines145–181) set pictureTop/Bottom directly from valid sharedPicture.bounds, set shared barAuthority and run scanner.Analyze at that basis. They do not run independent ExtractSubtitleBarEvidence/ResolveCurrentSubtitleAnalysisBars in this path. The fallback independent path is different and must not be presented as the shared production behavior.

Commit222e64b6032753753bb4ff764ba341c286b26aa1 (October14) introduced current bar extraction/resolution in that shared path. The current b1 path can produce raw/effective top0 on three encoding phases despite stable shared native top272; this basis is carried into extraction and the composed operation. Both old and current renderer code fit display placement to nlsGeometry, but FitSubtitleToVisiblePicture reconciles picture edges only within3 pixels, so it cannot repair top0→272 downstream.

The actual89 component replay reports both RC4 pins with recorded shared272..1890 produce the same line[1048,1933,2800,2032]89/89. Their independent path fails67/89(no-current-flat-bar), with22 detections. Thus the useful working reference is the shared source-coordinate basis, not relaxed luma thresholds or a historically robust independent edge extractor. This is CPU component evidence, not complete RC4 GPU playback qualification.

Modern current-source safeguards must remain. A replacement basis needs exact source/context-bound current verification and distinct provenance from raw barEvidence/independent acquisition. Only the verified missing opposite side is relevant; a current support envelope extending through the raw caption to2032 must not become the physical picture bottom. Keep raw bar facts truthful, current glyph/capture/cleanup checks and final qualifier unchanged. No production change made by this audit.
