#pragma once

#include <IRenderer.h>
#include <DolbyVisionLuminanceStatus.h>
#include <libplacebo/renderer.h>
#include <cmath>
#include <algorithm>

// Source metadata and detector values are captured together at the source pass.
// Temporal output passes must reuse this value, never query the shared renderer
// after a later filter neighbour has overwritten its detector history.
namespace FrameHdrStatus
{
    inline bool FiniteNits(double value) { return std::isfinite(value) && value >= 0.0; }
    inline bool PositiveNits(double value) { return FiniteNits(value) && value > 0.0; }
    inline double Nits(double value) { return FiniteNits(value) ? value : 0.0; }
    inline bool Supported(const RendererHdrStatus& value)
    {
        return value.Supported();
    }

    inline RendererHdrStatus Source(const pl_frame& frame, uint64_t sequence,
        const DolbyVisionLuminanceStatus& dv = {})
    {
        RendererHdrStatus result;
        result.sourceSequence = sequence;
        result.hdr = pl_color_transfer_is_hdr(frame.color.transfer);
        result.dolbyVision = result.hdr && frame.repr.dovi != nullptr;
        if (!result.hdr) return result;
        const auto& hdr = frame.color.hdr;
        result.maxCll = Nits(hdr.max_cll);
        result.maxFall = Nits(hdr.max_fall);
        // The minimum can legitimately be black (zero), but a missing maximum
        // does not establish a mastering range.
        if (PositiveNits(hdr.max_luma) && FiniteNits(hdr.min_luma) && hdr.min_luma < hdr.max_luma)
        {
            result.masteringMin = hdr.min_luma;
            result.masteringMax = hdr.max_luma;
        }
        result.sceneValid = std::isfinite(hdr.max_pq_y) && std::isfinite(hdr.avg_pq_y) &&
            hdr.max_pq_y > 0.0f && hdr.max_pq_y <= 1.0f &&
            hdr.avg_pq_y >= 0.0f && hdr.avg_pq_y <= hdr.max_pq_y;
        if (result.sceneValid)
        {
            result.scenePeakNits = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, hdr.max_pq_y);
            result.sceneAvgNits = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, hdr.avg_pq_y);
            if (result.dolbyVision && dv.sceneAvailable)
                result.sceneMinNits = Nits(dv.sceneMin);
        }
        return result;
    }

    inline RendererHdrStatus WithDetection(RendererHdrStatus source, bool enabled,
        const pl_hdr_metadata* detected)
    {
        source.peakDetectionEnabled = source.hdr && enabled;
        source.detectedValid = false;
        source.detectedPeakNits = source.detectedAvgNits = 0.0;
        source.detectedLinearAvgNits = source.detectedSceneMaxNits = 0.0;
        if (source.peakDetectionEnabled && detected &&
            std::isfinite(detected->max_pq_y) && std::isfinite(detected->avg_pq_y) &&
            detected->max_pq_y >= 0.0f && detected->max_pq_y <= 1.0f &&
            // The reported peak can be percentile-clipped and is reduced
            // independently of the mean. A mean above it is not invalid.
            detected->avg_pq_y >= 0.0f && detected->avg_pq_y <= 1.0f)
        {
            source.detectedValid = true;
            source.detectedPeakNits = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, detected->max_pq_y);
            source.detectedAvgNits = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, detected->avg_pq_y);
            source.detectedLinearAvgNits = Nits(detected->scene_avg);
            source.detectedSceneMaxNits = Nits(detected->scene_max[0]);
        }
        return source;
    }

    inline RendererHdrStatus Capture(RendererHdrStatus source, pl_renderer renderer, bool enabled)
    {
        pl_hdr_metadata detected{};
        const bool available = source.hdr && enabled && renderer &&
            pl_renderer_get_hdr_metadata(renderer, &detected);
        return WithDetection(source, enabled, available ? &detected : nullptr);
    }

    struct Context
    {
        uint64_t sourceGeneration = 0, viewportEpoch = 0, processingEpoch = 0;
        int transfer = 0, primaries = 0;
        bool operator==(const Context& other) const
        {
            return sourceGeneration == other.sourceGeneration && viewportEpoch == other.viewportEpoch &&
                processingEpoch == other.processingEpoch && transfer == other.transfer && primaries == other.primaries;
        }
    };

    // Externally synchronized. An invalidation rejects any in-flight publication
    // carrying an older token; unchanged contexts deliberately have no timeout,
    // because a paused picture remains a valid description of what is visible.
    class Publication
    {
        Context context{};
        uint64_t revision = 1, expectedGeneration = 0, expectedViewport = 0;
        bool contextKnown = false, generationKnown = false, viewportKnown = false, suspended = false, available = false;
        RendererHdrStatus status{};
    public:
        void Invalidate() { ++revision; available = false; contextKnown = false; status = {}; }
        void Suspend() { Invalidate(); suspended = true; }
        void ResetForGeneration(uint64_t generation)
        {
            Invalidate(); suspended = false; expectedGeneration = generation; generationKnown = true;
        }
        void ResetForViewport(uint64_t epoch)
        {
            Invalidate(); expectedViewport = epoch; viewportKnown = true;
        }
        uint64_t Begin(const Context& next, bool discontinuity = false, bool transitionPending = false)
        {
            if (suspended || transitionPending) return 0;
            if ((generationKnown && expectedGeneration != next.sourceGeneration) ||
                (viewportKnown && expectedViewport != next.viewportEpoch)) return 0;
            if (!contextKnown || !(context == next) || discontinuity)
            {
                Invalidate(); context = next; contextKnown = true;
            }
            return revision;
        }
        bool Commit(uint64_t token, RendererHdrStatus next)
        {
            if (!token || token != revision || !contextKnown || !Supported(next)) return false;
            next.sourceGeneration = context.sourceGeneration;
            next.processingEpoch = context.processingEpoch;
            status = next; available = true; return true;
        }
        bool CommitPresented(uint64_t token, RendererHdrStatus next, bool rendered, bool submitted)
        {
            if (!submitted) return false; // The old visible picture remains.
            if (!rendered) { Invalidate(); return false; } // A cleared black frame was submitted.
            return Commit(token, next);
        }
        bool Snapshot(RendererHdrStatus& output) const
        {
            output = status; return available;
        }
    };
}
