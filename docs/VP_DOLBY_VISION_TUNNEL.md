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
its tone mapper when they fit in the first packet. Ordinary RGB8 with no
Dolby packet uses an SDR RGB passthrough once the byte alignment is known.

The HDMI signal is already composed by the playback device. This path
does not decode or combine a Dolby Vision FEL itself. libplacebo does not
apply Dolby Level 2/8 display trims; it tone maps the decoded signal to
VP's configured output target. The formatter keeps active metadata when
a CRC-valid packet has no_md set. Advance metadata is associated with
the immediate next captured picture or with a later packet that names
the affected metadata ID; EOS separately ends the HDR stream. The
renderer bounds its metadata-matrix cache at 512 entries.

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

The log reports "CRC-validated r210 RGB8 alignment" on first detection,
"new display-management matrix" when it changes, and "DV 12-bit 4:2:2"
for the formatter contract. If the CRC never validates, the path cannot
interpret that signal as Dolby Vision; check Vertex2 EDID/scaling, DeckLink
packing, and whether the player actually sends TV-led DV. This code has
been compiled in x64 Release, but needs confirmation on the user's
DeckLink/Vertex2/Apple TV signal.

References: [HDMI 1.4a RGB tunnel packing](https://felloff.net/doc/comp/spec/HDMISpecification14a.pdf),
[ETSI GS CCM 001 display-management packet format](https://www.etsi.org/deliver/etsi_gs/CCM/001_099/001/01.01.01_60/gs_ccm001v010101p.pdf),
[libplacebo Dolby color representation](https://github.com/haasn/libplacebo/blob/master/src/include/libplacebo/colorspace.h).
