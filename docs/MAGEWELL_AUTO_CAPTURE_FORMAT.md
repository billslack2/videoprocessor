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
intervals; P010 59.94 fps with zero skipped intervals. Full VP playback, mode
transitions and GPU/caption regressions must be qualified before deployment.
