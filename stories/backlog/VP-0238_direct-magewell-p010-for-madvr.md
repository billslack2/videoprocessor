# VP-0238 Qualify direct Magewell P010 capture and delivery to madVR at UHD 59.94

## Status

Backlog

## Observation

On November 20 the madVR trial OSD reported v210, Clock-Smart, and conversion time of 1.57 ms average and 2.97 ms maximum over ten seconds. The rotated trial log at 11:33:01 recorded Magewell 3840x2160p at 59.941 Hz, 8-bit input and V210 capture. The operator UI showed a Gen2 x4 PCIe link. Whether DMA throughput, host conversion, copies, graph negotiation or delivery cadence limits playback remains open.

## Work

1. Trace the requested and accepted Magewell capture layout, SDK buffer strides and bytes transferred, VP formatter and copy stages, DirectShow media type, and the subtype madVR actually accepts. VP-0221 documents the existing renderer-dependent native capture policy and DirectShow v210 path.
2. Under matched hardware, signal, clock, queue and output settings, compare the existing route with device-native P010 delivered to madVR, if supported end to end. Distinguish capture-side P010 from v210 captured first and converted downstream. Record chroma subsampling and quality implications for a 4:2:2 source.
3. Measure actual DMA bytes/sec, capture gaps, conversion and copy cost, total worst-case frame-critical time, distinct submissions, actual presentation gaps and end-to-end latency. A conversion timer or cumulative drop count alone cannot identify the bottleneck.
4. If direct P010 improves sustained delivery, implement capability-based negotiation for compatible madVR sessions. Preserve fallback to a supported format and handle graph rejection, renderer switches and input format changes. Preserve HDR range, colorimetry and metadata, and bit-exact capture for RGB Dolby Vision tunnel modes.

## Acceptance

Demonstrate the selected route on real Magewell-to-madVR playback at UHD 59.94 with a 23.976 control. Show requested and negotiated formats and measured before/after cadence, latency, transfer volume and critical-path cost. Check chroma, range and HDR on compatible signals and confirm clean renegotiation on unsupported modes. If native P010 is unsupported or fails to improve throughput, document the limiting stage and measured alternative.

Related work includes VP-0221 for native capture policy, VP-0021 for renderer negotiation and VP-0234 for live frame budgets.
