# Automatic Magewell capture format

Only Magewell capture for native VP Renderer consumers changes. DeckLink capture
and the existing legacy/forced-conversion consumer contracts are unchanged.

The SDK capability list must support the selected card output. Ordinary native
YCbCr uses P210 (10-bit 4:2:2) while its resolution/rate payload is at most the
conservative 1.6 GB/s transfer budget. Above that budget, request P010 directly
from the card: 10-bit luma and 4:2:0 chroma, retained in the SDK buffer and uploaded
as native half-height UV without CPU conversion/repacking. Unsupported P010 is
an explicit capture error, never an unadvertised format or silent 8-bit fallback.

At 3840x2160, 23.976/24/25/29.97/30 retain P210; 50/59.94/60 choose P010 pending
separate qualification of any higher P210 transfer budget. 1080p60 retains P210.
This budget is a conservative policy, not a measured bus-speed guarantee on all
boards or slot configurations. Format changes use existing stop/invalidate/new
buffer/state-acknowledgement boundaries. Retained old frames keep their buffers.

Dolby Vision RGB tunneling keeps its prior byte-preserving capture and decoder
path. Do not reduce/subsample the RGB tunnel to make it fit. Lower-rate P210
preserves the existing ten-bit capture contract; this does not turn a twelve-bit
HDMI input into a lossless twelve-bit capture. Wire metadata remains independent
of the negotiated SDK output format.

OSD identifies P010 card capture as 10-bit 4:2:0. Logs record format, payload,
source/delivery cadence, skipped input intervals and SDK/callback timing. The
appended native P010 encoding requires ABI 33 in matching host/renderers.

The initial independent UHD59.94 probe measured P210 52.02 fps with 76 skipped
intervals; P010 59.94 fps with zero skipped intervals. Full VP UHD59.94 playback confirmed native P010, sustained 59.94 fps without skipped
input intervals, and smooth motion in the user's visual trial. The separate UHD50
DV trial retained packed RGB and delivered about 46-47 fps; subsampling a Dolby
Vision RGB tunnel is not a valid fallback. YCbCr UHD50/P210 remains unqualified.

High-bandwidth progressive raw RGB8 tunnels now request advertised BGR24 from
Magewell and restore opaque BGRA with byte-exact packing before the existing
decoder. This drops only the unused alpha byte on PCIe; it does not subsample or
change RGB range. Lower-rate RGB32, unsupported BGR24, non-raw/legacy consumers,
interlaced capture and DeckLink keep their prior route. CPU-dispatched AVX2
packing uses bounded loads and fenced streaming stores; scalar fallback is exact.
The UHD50 card probe compared all 24,883,200 RGB channels on the same picture
against BGRA with zero differences, then sustained 50 fps with zero input skips.
UHD59.94/60 DV remains unqualified without a suitable hardware source.
OSD identifies lossless RGB24 packing. Its render-budget row is not panel scanout
telemetry; actual smooth-motion engagement must be verified separately.

Magewell delivery telemetry now feeds the OSD as well as the log. Five-second
windows show actual captured fps beside expected input fps. Hardware timestamp
gaps increment the existing capture-missed counter, separately from renderer
drops. Capture totals reset on capture restart; interval tracking and window
measurements reset on format changes, without charging the resync gap as lost
pictures. Invalid video clears cached rate telemetry. This does not change
DeckLink's capture or OSD behavior. The source clock interval is timing evidence,
not proof that every input frame reached host memory.
