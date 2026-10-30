# Vertex2 EDID for DeckLink Quad HDMI Recorder

`vertex2-lg-dv30-hdr10bit.bin` is a 256-byte EDID based on the Vertex2
**FULL + LG DV** custom default. It preserves low-rate 4K standard Dolby Vision
and high-rate SDR/HDR timing advertisements, withdraws high-rate 4K Dolby Vision,
and removes the 12-bit deep-color advertisements that triggered the current
VP build's YCbCr 12-bit input rejection.

This is an optional source-negotiation workaround, not a VP decoder change.
It is not applied automatically by installing or updating VP. It retains the
base preset's audio, timing, and color capabilities and is not a universal EDID.

SHA-256: `9a271cf7e071bf247a1804efda3a62fdc182e161951c1097c40484c210956b01`

## Apply on Vertex2

1. Download and retain a backup of the active source-input EDID.
2. Upload this BIN to an appropriate custom EDID slot and select it for that
   source input. The tested setup used the already-active custom slot 2.
3. Issue hotplug from CONFIG so the source rereads its capabilities.
4. On Apple TV, enable Match Dynamic Range and Match Frame Rate when testing
   content-driven switching. Check the actual input rate and HDR mode in VP.

To undo the change, upload and select your saved original EDID, then issue
hotplug again. VP installation does not modify the Vertex2 configuration.

## Exact changes from the base EDID

| Offset | Original | Modified | Meaning |
| --- | --- | --- | --- |
| 0xCA | B8 | 98 | Clear HDMI 36-bit/12-bpc deep-color capability |
| 0xDA | 03 | 01 | Clear 12-bit 4:2:0 capability; retain 10-bit |
| 0xE0 | 26 | 24 | Clear version-1 Dolby VSVDB supports2160p60 capability |
| 0xFF | 63 | 87 | Correct CTA extension checksum |

All other bytes are unchanged. Both 128-byte blocks have valid checksums.
The original base EDID SHA-256 was
`5559f42d5a1cf43449584900d46d83a926c54eb07544bb9e244136141ccc86a9`.

## Validation on 2026-10-30

Setup: Apple TV -> Vertex2 firmware 0.60 -> DeckLink Quad HDMI Recorder,
channel 2, Desktop Video 14.4.1, VideoProcessor.

- DeckLink capability queries rejected all seven tested RGB layouts at
  UHD50/59.94/60 while allowing UYVY/v210. Live forced tests at UHD50 and
  UHD59.94 produced zero valid source frames in every RGB layout.
- The original high-rate DV HDMI signal was received as YUV by DeckLink,
  losing the RGB carrier needed for standard Dolby Vision tunneling.
- With the first DV-only restriction, Apple TV chose UHD59.94 12-bit HDR;
  VP rejected that detected YUV depth. Direct capture still delivered
  131/131 valid v210 frames, identifying a separate application limitation.
- With this final EDID, Vertex2 reported UHD59.94 BT.2020 10-bit HDR.
  Active RX0 EDID readback matched the file's SHA-256 exactly.
- User playback of Netflix Meridian confirmed 3840x2160 at 59.940 fps,
  10-bit v210 capture, BT.2020/PQ and HDR10 mastering metadata, with a
  normally rendered picture. This validates high-rate HDR fallback for that
  source/app/content combination.
- User playback of Our Oceans confirmed UHD25 Dolby Vision with r210
  capture and DV RGB tunnel decoding; low-rate DV options remained available.

Exact 60 Hz was capability-tested only. Post-change true UHD50 content
fallback has not been verified. Behavior depends on the source and app's
available renditions: withdrawing DV support does not itself convert DV to HDR.

Content frame rate, HDMI input rate, and PC display refresh are separate.
A 25-fps title can be transported over 50-Hz HDMI with repeated frames. The
Our Oceans screenshot proves 25-Hz input in that session, not the title's
mastering rate or that an earlier 50-Hz signal was dropping unique frames.

For a high-rate test, Netflix's Meridian is documented as Dolby Vision with
UHD59.94 assets; select the first episode rather than its HD SDR counterpart:
[Netflix Open Content](https://opencontent.netflix.com/),
[Meridian](https://www.netflix.com/title/80161105).
