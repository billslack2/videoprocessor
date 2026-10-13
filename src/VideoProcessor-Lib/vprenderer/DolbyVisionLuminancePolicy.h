#pragma once

#include <DolbyVisionLuminanceStatus.h>
#include <video_frame_formatter/IVideoFrameFormatter.h>
#include <libplacebo/colorspace.h>

namespace DolbyVisionLuminancePolicy
{
    // The HDMI tunnel supplies DM source range and L1 scene data, not HDR10
    // MaxCLL/MaxFALL. Keep the supplied static range only as an explicit fallback.
    inline DolbyVisionLuminanceStatus Apply(
        const DolbyVisionTunnelFrameMetadata& dm, pl_hdr_metadata& hdr)
    {
        DolbyVisionLuminanceStatus status;
        status.tunnelActive = true;
        status.available = dm.valid;
        hdr.max_cll = hdr.max_fall = 0;
        hdr.max_pq_y = hdr.avg_pq_y = 0;
        if (!dm.valid)
            return status;
        status.sourceRangeFromMetadata = dm.sourceMaxPq > dm.sourceMinPq &&
            dm.sourceMaxPq <= 4095;
        if (status.sourceRangeFromMetadata)
        {
            hdr.min_luma = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS,
                static_cast<float>(dm.sourceMinPq) / 4095.0f);
            hdr.max_luma = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS,
                static_cast<float>(dm.sourceMaxPq) / 4095.0f);
        }
        status.sourceMin = hdr.min_luma;
        status.sourceMax = hdr.max_luma;
        status.sceneAvailable = dm.hasSceneMetadata &&
            dm.sceneMaxPq <= 4095 && dm.sceneAvgPq <= 4095;
        if (status.sceneAvailable)
        {
            hdr.max_pq_y = static_cast<float>(dm.sceneMaxPq) / 4095.0f;
            hdr.avg_pq_y = static_cast<float>(dm.sceneAvgPq) / 4095.0f;
            status.sceneMax = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, hdr.max_pq_y);
            status.sceneAvg = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, hdr.avg_pq_y);
        }
        return status;
    }
}
