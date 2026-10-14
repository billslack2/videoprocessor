# UHD player Dolby Vision qualification

Use this protocol to qualify a UHD disc player through Vertex2, DeckLink and
VP's experimental TV-led Dolby Vision tunnel path. Start with ordinary disc
playback, then verify the transport and recovery behavior, and finally verify
FEL separately. A Dolby Vision badge or a 12-bit decoded format is not proof
that the player applied a Full Enhancement Layer.

Read [the tunnel setup and recovery contract](VP_DOLBY_VISION_TUNNEL.md) first.
Hardware model, firmware and the exact Dune disc edition are still pending;
no hardware or FEL qualification is claimed by this document.

## What is being compared

| Route | Input available to the renderer | Where FEL is composed |
| --- | --- | --- |
| JRVR disc/title or file playback | Decoded base layer, enhancement layer and DV metadata | In the player/rendering pipeline |
| VP TV-led HDMI capture | The source player's composed DV image and HDMI display-management metadata | In the source player, before HDMI |

JRiver's supplied Media Center 36.0.9 announcement describes automatic Profile
7 EL processing for Windows title-mode discs/folders and MKV/MP4, an advanced
EL toggle affecting future frames, and FEL/MEL identification in Video Analysis.
Its [announcement](https://yabb.jriver.com/interact/index.php/topic,143755.0.html)
is the behavioral reference. JRVR renderer source was not available for this
review; its private algorithms, frame-pairing rules and exact libplacebo use
have not been audited.

VP supports HDMI capture only. Direct movie-file or disc playback is outside
its product scope. The relevant comparison is preservation of the source
player's reconstructed picture through HDMI capture and SDR tone mapping.
The [libplacebo frame API](https://github.com/haasn/libplacebo/blob/master/src/include/libplacebo/renderer.h)
requires a decoded enhancement frame for FEL composition. HDMI capture does
not provide that separate frame; sharing libplacebo cannot recover an
enhancement layer omitted by the source player.

VP should unpack the composed HDMI image once. Do not attach a synthetic EL
or apply bitstream RPU reshaping again to these samples. VP's current L1-informed
tone mapping does not implement Dolby's complete L2/L8 display trims, so matching
transport precision does not establish identical output to JRVR or a Dolby TV.

## 1. Establish the source and preserve the setup

Record the player's model/firmware, DeckLink model, Vertex2 firmware, exact VP
commit/build and the Dune film year, region and edition. Confirm that this player
supports the disc's Dolby Vision and can send the TV-led RGB tunnel. Do not infer
FEL decoding from a general Dolby Vision product claim.

Dune's FEL/MEL type remains **unknown** until the exact edition is classified.
Some Dune editions may be MEL. A MEL disc can qualify tunnel decoding and
metadata recovery, but cannot qualify preservation of meaningful FEL residuals.
Use JRiver Video Analysis or inspect the exact source's RPU with
[`dovi_tool info`](https://github.com/quietvoid/dovi_tool#info) if source files are
available. Preserve the classification evidence; `Profile 7` or `BL+EL+RPU`
alone is insufficient.

Save the current VP and Vertex2 settings before any operator change. Use a
successfully completed x64 Release build, with the host and renderer DLL from
the same build. Keep the existing display target, picture controls and calibration
fixed throughout a comparison. Record any required settings change precisely.

## 2. Qualify an intact TV-led tunnel

Use the documented real-DV EDID and **No Scaling** on the capture output.
Retain full raster dimensions, follow-input color/depth behavior and no Vertex2
pixel OSD on this path. Establish the currently supported 4K23.976 r210 capture
mode before expanding to other timings. Keep crop/zoom disabled for this phase.

The [Vertex2 manual](https://www.hdfury.com/docs/HDfuryVertex2.pdf) explains that
DV modes 0/1 travel in an RGB 8-bit container and that No Scaling passes the
signal untouched. A Vertex2 `RGB 8b` status is therefore expected. DeckLink's
r210 transport status and VP's decoded 12-bit 4:2:2 format describe different
stages. Converting transport pixels as ordinary RGB/YUV before unpacking can
damage the tunnel.

Play the feature and record the Vertex2 input/output status and VP OSD/log.
Check for CRC-validated RGB8 alignment, usable complete DM, stable color and
geometry, and the decoded `DV 12-bit 4:2:2` contract. At several moving scene
cuts, record DM source range and L1 scene peak/average when present. Absent L1
is not itself a transport failure. Pause may leave already-rendered values on
screen; assess metadata changes after playback resumes.

**Pass:** VP locks onto validated metadata and renders stable decoded images
without unexplained sustained loss. Record packet counts and observed extension
levels; a single-packet session does not qualify multi-packet reception.
CRC-failed-copy counters include alternate-alignment attempts, so a nonzero
counter alone is not proof of HDMI corruption.

## 3. Qualify transitions and recovery

Exercise pause/resume, chapter seek, menu-to-feature, feature-to-menu,
stop/start and a normal HDMI reacquisition. Repeat a transition involving a
capture timing/format change. Save the active `C:\Videoprocessor\vp\logs\vp.log`
with timestamps matching this run; do not use a stale log from another session.

**Pass:** no corrupted tunnel pixels appear as ordinary SDR; usable metadata
returns after reacquisition; the previous title's matrix or L1 values do not
remain active after a stream reset; and audio/video timing remains usable.
Compare transitions with the documented loss/EOS contract. After DV lock,
prolonged loss suspends submission and may retain the last displayed image;
that is not successful continued decoding. A same-mode SDR switch without EOS
may require **Restart capture**. Record this limitation instead of claiming
seamless switching. Fresh complete DM is needed after discarded metadata;
a `no_md` header alone cannot restore it.

Known recovery edge from source review: metadata IDs have only 16 values.
A gap spanning at least 16 picture periods, or a sufficiently long run without
authenticated headers, can hide a complete ID cycle. A returning `no_md`
header can then match an obsolete cached ID before the current 24-rejected-frame
cutoff clears it. This has not been reproduced on hardware. Complete HDMI
recovery robustness is not claimed; a policy change needs a dedicated synthetic
regression covering ID wrap, fresh-DM recovery and suspension behavior. Normal
continuous `no_md` reuse remains valid and must not acquire an arbitrary expiry.

Then enable VP crop/zoom after unpacking and repeat a scene plus a chapter seek.
**Pass:** visible image geometry, chroma alignment and color remain consistent
with the uncropped run, including the letterbox boundary. Crop is a viewport
operation on the decoded picture; the transport raster must remain intact.

## 4. Verify precision and actual FEL separately

The software precision gate uses independent sender fixtures and checks exact
12-bit intensity/chroma recovery, both supported RGB8 placements, padded rows
and scalar/AVX2 equivalence. Preserve all decoded codes in the upload path.
An 8-bit screenshot, a visually smooth ramp or nonzero low bits in movie samples
is not a bit-exact precision test.

The hardware FEL gate requires an independently classified FEL source and a
known EL-dependent pattern or a controlled decoded reference. First confirm
that the player reconstructs that source; then establish that its composed
result survives Vertex2, capture, unpacking and rendering. Where possible,
compare the same frames with EL applied and omitted while holding output target,
RPU/display mapping, crop, scale and other processing fixed. A player may not
provide this toggle, so the reference method must be recorded explicitly.

**Pass:** the expected EL-dependent information survives the full path and
agrees with the reference within the stated measurement method. DV versus HDR10
alone does not isolate FEL: metadata, reshaping and tone mapping also differ.
Dolby Vision status, DM/L1 metadata and 12-bit HDMI samples cannot establish
whether the original disc was FEL or whether the player used it.

If only Dune playback and tunnel recovery have been tested, report **transport
qualified; FEL unverified**. Full JRVR image parity additionally needs matched
rendering settings and an image comparison; this protocol does not imply it.

## Qualification record

Copy this record for each run; use `Pass`, `Fail`, or `Not run` for every gate.
Keep captures local and out of the source repository.

```text
Date / operator:
VP commit / x64 Release build / host and renderer hashes:
Player model / firmware:
DeckLink model / driver:
Vertex2 firmware / saved EDID identity / capture-output settings:
Disc film year / region / edition / title or clip:
Source FEL/MEL classification / evidence:
Input timing / tunnel mode / DeckLink transport / decoded format:
Output target brightness / mapping / crop / scale settings:
Transport lock and stable playback: Not run
Transition and metadata recovery: Not run
Crop alignment after unpacking: Not run
Software 12-bit precision: Not run
Observed multi-packet reception: Not run
Hardware FEL preservation / reference method: Not run
JRVR matched-output comparison: Not run
Log and comparison artifacts / timecodes:
Failures / recovery actions / limitations:
Overall claim supported by this run:
```