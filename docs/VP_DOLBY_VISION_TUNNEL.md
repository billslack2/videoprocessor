# TV-led Dolby Vision HDMI RGB tunnel

This experimental path accepts an Apple TV or other TV-led Dolby Vision
source through a capture path that preserves the RGB8 tunnel as ARGB, BGRA,
or DeckLink r210. The original live qualification used a Vertex2/DeckLink
4K23.976 chain; the additional layouts are qualified by independent synthetic
senders and real renderer tests, and still need live card/driver qualification. Enable it in the source configuration:

    [general]
    renderer: VP Renderer (Beta)
    dolby_vision_tunnel: true

Restart VP after changing this startup setting. Keep the Vertex2 on a
**real Dolby Vision** custom EDID and **No Scaling** for the DeckLink output.
Use the normal HDR10/LLDV configuration when the tunnel switch is off.

The formatter accepts native ARGB/BGRA bytes and either placement of RGB8
in DeckLink's 10-bit r210 channels, but recognizes Dolby Vision only after an embedded
display-management packet passes its MPEG-2 CRC-32. It reconstructs the
original 12-bit intensity and alternating chroma samples in two 16-bit
planes at full 4:2:2 resolution. It does not convert the tunnel to P010.
The packet's opponent-color and LMS matrices are passed to libplacebo's
Dolby Vision decoder; Level 1 scene peak/average PQ values are passed to
its tone mapper after complete metadata reassembly, including L1 blocks that
cross packet boundaries. Each 128-byte packet is read at its raster position,
with up to three CRC-checked copies. Declared lengths are bounded at 0x2F00
bytes and by the available picture pixels; packet types, IDs and header flags
must match. Incomplete structures never replace the active metadata.
Ordinary RGB8 with no Dolby packet uses SDR RGB passthrough before DV lock
once byte alignment is known, or after a CRC-validated EOS boundary.

The HDMI signal is already composed by the playback device. This path
does not decode or combine a Dolby Vision FEL itself. libplacebo does not
apply Dolby Level 2/8 display trims; it tone maps the decoded signal to
VP's configured output target. The formatter keeps active metadata when
a CRC-valid packet has no_md set. Advance metadata is associated with
the immediate next captured picture or with a later packet that names
the affected metadata ID; EOS separately ends the HDR stream. The
renderer bounds its metadata-matrix cache at 512 entries.

After DV lock, a picture without a CRC-valid supported header is presented as
black immediately, before upload, tone mapping or image analysis. Cached DM
is retained for brief loss, but it cannot authorize displaying untrusted pixels.
A valid `no_md` header with the current ID resumes normal rendering using that
cache. Temporal interpolation history is cleared while black is presented.

After 24 consecutive pictures without a complete usable metadata transaction,
cached active/pending metadata is discarded. Black continues until fresh complete
DM is available; a valid `no_md` header alone cannot rebuild discarded matrices.
CRC loss does not authorize SDR passthrough: a damaged/scaled tunnel is
indistinguishable from a same-mode SDR switch without EOS. An EOS boundary or
capture video-state reset permits SDR again. A same-mode SDR switch that omits
EOS may therefore remain black until Restart capture.

Capture may report the transport container as ARGB/BGRA 8-bit or RGB
10-bit 4:4:4 (r210). The alpha byte in ARGB/BGRA is ignored.
The DV tunnel inside that container is 12-bit 4:2:2, which the VP OSD
reports after unpacking. This does not add 4:4:4 movie chroma. The
previous measured LLDV input was also 4:2:2, at 10-bit capture depth,
so this path changes code precision and Dolby display-management
information, not the number of chroma samples. It cannot establish
whether a disc player used a Profile 7 FEL before HDMI output.

For Apple TV, choose 4K SDR as the normal menu format and enable Match
Dynamic Range and Match Frame Rate. This lets 23.976 Hz Dolby Vision
movies use the supported RGB tunnel while keeping the 59.94 Hz menu in
ordinary SDR. The decoder accepts ARGB/BGRA/R210 RGB tunnels; a v210
menu is an ordinary YCbCr capture, not a supported RGB tunnel.

The log reports complete DM length/packet count/IDs when the layout changes,
newly encountered extension levels (L1 is applied; other levels are logged
but not applied), loss/recovery events, and aggregate counters every 600
pictures. CRC-failed-copy counts include attempts at an alternate RGB8
alignment, so a nonzero count alone is not proof of HDMI corruption.
The log also reports "CRC-validated &lt;layout&gt; RGB8 alignment" on first detection,
"new display-management matrix" when it changes, and "DV 12-bit 4:2:2"
for the formatter contract. If the CRC never validates, the path cannot
interpret that signal as Dolby Vision; check Vertex2 EDID/scaling, DeckLink
packing, and whether the player actually sends TV-led DV. The previous single-packet decoder has produced a correct image on the
user's DeckLink/Vertex2/Apple TV and Sony setup. Multi-packet reception and
loss/recovery changes still need live source confirmation.


## Capture compatibility

ARGB and BGRA preserve the RGB8 carrier without increasing HDMI bit depth.
The decoder reads their documented byte order directly, ignores alpha and
reconstructs the same exact 12-bit 4:2:2 samples as r210. Both scalar and AVX2
kernels handle row tails and threaded UHD conversion without an intermediate
full-frame RGB repack. Format changes reset the metadata and DV lock.

Magewell RGB8 capture chooses advertised ARGB or BGRA output with matching byte order and requests full-range RGB output. Pro Capture HDMI 4K+ advertises BGRA without ARGB; it must not be rejected or sent through YCbCr conversion for DV.
While DV Tunneling is selected with VP Renderer, capture saves the existing
SDK input-range override (including AUTO), temporarily treats input as full,
and verifies that setting before restarting capture. Matching full-range
input interpretation and output avoids range expansion or nominal-range
clipping of the RGB8 data carrier, even when HDMI reports limited or unknown
range. The original override is restored before ordinary capture resumes,
on signal loss, on stop and on capture failure. Setup/readback failure stops
capture without delivering a falsely admitted tunnel. Restoration failure
is reported explicitly. No EDID or active configuration file is changed.

The wire range comes from AVI independently of the temporary SDK override.
SDR fallback honors an explicit saved input-range override, or the original
AVI full/limited range when that override was AUTO, for both renderer levels
and picture analysis. Unknown wire range permits CRC-validated Dolby Vision
because DM owns its decoded range; it cannot authorize guessed SDR levels.
Real Magewell hardware/driver qualification remains required: the automated
capture harness models SDK range expansion and tests byte-exact delivery,
but cannot establish how an untested card implements its processing engine.

Native RGB upload is bypassed while tunneling so the conversion worker always
validates packets before rendering. Picture, bar and subtitle analysis consume
the decoded picture, never the packed transport. SDR RGB analysis is permitted
only before DV lock or after an explicit EOS/state reset; loss after lock blanks
presentation and excludes the picture from analysis.

Run `DolbyVisionTunnelFormatterTests`, `AnalysisRecoveryDvTests`, and
`MagewellCaptureTests` after an x64 Release solution build. Magewell tests
feed the independent sender through the real capture-thread code and a fake
SDK that expands limited RGB unless raw capture is requested. They cover
full/limited/unknown signaling, all saved range overrides, a deliberately
corrupted normal-capture negative control, live mode changes and failed
override setup/readback with restoration. The tunnel class
includes an integration test that loads the built renderer DLL and confirms
actual RGB8 decoded upload and recovered DM/L1 status with native RGB upload
otherwise enabled. Run this class for both standard and beta test binaries;
then run `tools/test_subtitle_motion_renderers.ps1` for both dependencies.
The renderer plugin API is incremented because the shared capture state now
includes the RGB-byte-preservation contract; mismatched host/DLL pairs fail closed.

References: [HDMI 1.4a RGB tunnel packing](https://felloff.net/doc/comp/spec/HDMISpecification14a.pdf),
[ETSI GS CCM 001 display-management packet format](https://www.etsi.org/deliver/etsi_gs/CCM/001_099/001/01.01.01_60/gs_ccm001v010101p.pdf),
[libplacebo Dolby color representation](https://github.com/haasn/libplacebo/blob/master/src/include/libplacebo/colorspace.h).

The independent sender reference is
[CroqueMr/libreelec-x86-DV transport contract](https://github.com/CroqueMr/libreelec-x86-DV/blob/d9ad989ee92846afc32957cbeab6d5e2a5cb49b4/CB1/docs/SOURCE-TRANSPORT.md)
and its `CB1/src/dvbridge_render.c` RGB8 packer at the same pinned revision.
Its 12-bit intensity/chroma packing is the inverse of this unpacker. The
reference's FEL composer operates upstream of that HDMI packing step.

## Luminance status

The Modern view shows a **DV luminance (nits)** card while the tunnel is active:
source minimum/maximum from recovered DM, and scene peak/average from L1.
The card marks a recovered source range as **DM**, or **Fallback** when a
valid source range is absent and VP's effective static range is used. Missing
scene metadata appears as `---`; a present black scene appears as zero.
Before metadata is available or after metadata loss the values are hidden.
SDR RGB passthrough is labelled **SDR**. HDR10 and LLDV retain their static
HDR luminance card. The legacy HDR controls still represent effective static
metadata, rather than this per-frame DV snapshot.

For DV, libplacebo receives zero (unknown) MaxCLL/MaxFALL instead of the
synthetic static values; recovered DM source range and L1 PQ fields are
supplied separately. L1 peak/average are not relabelled as HDR10 MaxCLL/FALL.
The output target brightness remains the configured projector/display target.
The shared renderer API is bumped to 26 so older host/plugin pairs are rejected.
## UHD disc player qualification

A UHD player can use this same tunnel path when it sends compatible TV-led
Dolby Vision. The player must reconstruct any Profile 7 enhancement layer
before HDMI output. VP preserves and decodes the composed tunnel; it cannot
recover an EL discarded by the source, or determine FEL/MEL from this signal.
Use the [staged UHD test protocol](VP_DV_UHD_QUALIFICATION.md) before claiming
hardware qualification. Dune is a transport test until its exact edition's
FEL/MEL type and the player's behavior have been independently established.

Rendered-output captures (`Ctrl+Alt+S`) now record the source representation,
sample depth, color depth and bit shift, plus a `dolby_vision` object identifying
active DV decoding, DM versus fallback source range, L1 presence and the PQ
scene values supplied for that frame. `fel_status` remains `unknown` on DV HDMI
input. These are additive fields in the existing capture JSON schema; the PNG
still contains the final 10-bit output expanded losslessly to 16 bits, not the
original 12-bit tunnel samples. See [capture interpretation](VP-0133_RENDERED_OUTPUT_CAPTURE.md).

The reviewed Beta library is the bundled API 377 source at
`billslack2/libplacebo` commit `8bccd6578c9e845f77205cec9e85fdb97bf36ed7`.
Its Dolby decoder already normalizes the recovered codes and uses the supplied
matrices. No second reshaping/FEL pass is appropriate for this HDMI route.
With peak detection enabled, L1 PQ fields remain supplied, while measured
maxRGB/average fields can also influence libplacebo's adaptive target. L1 is
therefore an input to VP's selected tone mapping, not a complete Dolby display
mapping implementation.
