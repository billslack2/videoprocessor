# TV-led Dolby Vision HDMI RGB tunnel

This experimental path accepts an Apple TV or other TV-led Dolby Vision
source through an HDFury Vertex2 and a DeckLink device that captures the
4K23.976 RGB 8-bit tunnel as r210. Enable it in the source configuration:

    [general]
    renderer: VP Renderer (Beta)
    dolby_vision_tunnel: true

Restart VP after changing this startup setting. Keep the Vertex2 on a
**real Dolby Vision** custom EDID and **No Scaling** for the DeckLink output.
Use the normal HDR10/LLDV configuration when the tunnel switch is off.

The formatter accepts either placement of RGB8 in DeckLink's 10-bit r210
channels, but recognizes Dolby Vision only after an embedded
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

After DV lock, 24 consecutive pictures without a complete usable metadata
transaction suspend DV frame submission and discard cached active/pending
metadata. The renderer can retain the last presented picture. CRC loss does
not authorize SDR passthrough: a damaged/scaled tunnel is indistinguishable
from a same-mode SDR switch without EOS. Fresh complete DM is required to
resume; a valid no_md header alone cannot rebuild discarded matrices. An EOS
boundary or capture video-state reset permits SDR again. Transient loss under
24 pictures keeps the existing matrix unless a valid header changes its ID.
A same-mode SDR switch that omits EOS may therefore require Restart capture.

DeckLink reports the transport container as RGB 10-bit 4:4:4 (r210).
The DV tunnel inside that container is 12-bit 4:2:2, which the VP OSD
reports after unpacking. This does not add 4:4:4 movie chroma. The
previous measured LLDV input was also 4:2:2, at 10-bit capture depth,
so this path changes code precision and Dolby display-management
information, not the number of chroma samples. It cannot establish
whether a disc player used a Profile 7 FEL before HDMI output.

For Apple TV, choose 4K SDR as the normal menu format and enable Match
Dynamic Range and Match Frame Rate. This lets 23.976 Hz Dolby Vision
movies use the supported RGB tunnel while keeping the 59.94 Hz menu in
ordinary SDR. The current decoder only accepts the r210 RGB tunnel;
do not interpret a 59.94 Hz v210 menu as Dolby Vision.

The log reports complete DM length/packet count/IDs when the layout changes,
newly encountered extension levels (L1 is applied; other levels are logged
but not applied), loss/recovery events, and aggregate counters every 600
pictures. CRC-failed-copy counts include attempts at an alternate RGB8
alignment, so a nonzero count alone is not proof of HDMI corruption.
The log also reports "CRC-validated r210 RGB8 alignment" on first detection,
"new display-management matrix" when it changes, and "DV 12-bit 4:2:2"
for the formatter contract. If the CRC never validates, the path cannot
interpret that signal as Dolby Vision; check Vertex2 EDID/scaling, DeckLink
packing, and whether the player actually sends TV-led DV. The previous single-packet decoder has produced a correct image on the
user's DeckLink/Vertex2/Apple TV and Sony setup. Multi-packet reception and
loss/recovery changes still need live source confirmation.

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
