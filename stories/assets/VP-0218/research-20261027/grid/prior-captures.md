# Prior native capture comparison

All calculations below used the unchanged `7394ba420999a8fa708718ee4ce4d87e7459369f` beta objects in `baseline/` for raw extraction, global darkness, AR darkness, and the frozen relative-grid nomination. The separate loader reads only each `.bin.txt` first line and the native pixel bytes. Both sidecars specify 3840×2160, rowBytes/chromaRowBytes 7680, format value 4 (`DecodedDolbyVision422`), encoding value 6, colorspace value 8. The sidecars have no range field; the loader leaves `AnalysisLumaRange::Unknown` (0), matching the existing test's default. No `ActivePictureFrameIdentity` or source generation was invented in this component analysis.

## paused-1649

`E:\codex\diagnostics\dark-ar-followup-20261013\paused-1649\frame.bin` (33,177,600 bytes): unchanged beta extractor returns `BAR_CROP_TRUSTED`, proposed/trusted `[0,274..3840,1884]`, AR 2.38509, both top and bottom trusted. Raw global classifier is near-black (p90 80); AR classifier finds distributed dark picture (15 textured tiles, all 4 quadrants), so `EffectiveNearBlack=false`.

The frozen grid nominee also finds both edges: top bracket [272,280], midpoint 276; bottom scan bracket [1878,1886] from inside/outside coordinates (reported scan order 1886→1878), midpoint 1882. One-grid-pitch fringes are [264,288] and [1870,1894]. The grid therefore nominates similar boundaries, but with coarse brackets; it adds no evidence that outside bands are safe.

The requested existing test was actually executed against `E:\codex\videoprocessor\vp-0218-adaptive-ar-20261027\x64\Release\VideoProcessor-Test.dll` at source HEAD `ddf5d666803c29e0bdf7af310963b2695e382bc2`, with `VP_DARK_AR_ANALYSIS_FRAME` set to this exact `.bin`. `OptionalCapturedDarkArFrameKeepsRawNearBlackAndFindsDistributedPicture` passed (1 test matched, 1 passed), so this was not an optional no-op. TRX: `paused-1649-ddf5d666-dark-ar.trx`; console log: `paused-1649-ddf5d666-dark-ar.log`.

## continuous-subtitle-ar-20261013

`E:\codex\diagnostics\continuous-subtitle-ar-20261013\frame.bin` (33,177,600 bytes): unchanged beta extractor returns `BAR_CROP_TRUSTED`, proposed/trusted `[0,272..3840,1886]`, AR 2.37918, both edges trusted. Raw global p90 is 405, so global-near-black is false and distributed-dark is false (0 tiles). The dark-frame test's expected contract does not match this capture; no assertion test was run on it.

The grid nominates only the top: bracket [272,280], midpoint 276, fringe [264,288]. It abstains on the bottom edge. This is a concrete reason to keep the cue additive: replacing existing native extraction would lose a boundary on this prior subtitle capture.

## Limits

These are component-only single-frame reads, not identity, temporal, caption-ownership, retention, or publication tests. The grid midpoint does not establish physical aspect authority. The `Unknown` range was preserved and not coerced to Full. The ddf test DLL hash before the run was `19F5640B2C1A88BCD2755A5D106AF7B878428D48002CDFC49A059B82F9D1AB0F`; no test/source build was performed.
