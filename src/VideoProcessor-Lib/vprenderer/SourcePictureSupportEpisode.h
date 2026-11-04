#pragma once
#include <SourcePictureDetector.h>
#include <array>

// Owned exclusively by the serial native-analysis worker. Only immutable
// per-source results leave this episode; the renderer never reads its state.
class SourcePictureSupportEpisode {
public:
    bool Begin(const AnalysisLumaSource& s,const ActivePictureFrameIdentity& id,
        SourcePictureSupportRequest& request) {
        const std::array<uint64_t,15> context{{id.transportGeneration,id.sourceFormatGeneration,
            id.viewportGeneration,id.rendererGeneration,id.analysisDomainKey,uint64_t(s.width),
            uint64_t(s.height),uint64_t(s.format),uint64_t(s.encoding),uint64_t(s.colorspace),
            uint64_t(s.range),uint64_t(s.rowBytes),uint64_t(s.chromaRowBytes),s.rgbBitAlignment,s.generation}};
        if(observed_ && (id.transportGeneration<last_.transportGeneration ||
            (id.transportGeneration==last_.transportGeneration &&
             (id.acceptedSequence<=last_.acceptedSequence || id.sourceFrameNumber<=last_.sourceFrameNumber ||
              (id.captureTimestamp && last_.captureTimestamp && id.captureTimestamp<last_.captureTimestamp)))))return false;
        // Advance even on failed verification; late completions cannot rewind.
        if(!observed_ || context!=context_) {baseline_={};}
        observed_=true;last_=id;context_=context;request=baseline_;return true;
    }
    void Complete(const SourcePictureDetectionResult& result) {
        if(!result.support || !result.support->currentSupport.verified)return;
        const auto& support=*result.support;
        const bool measured=result.evidence.physicalEdges.estimateAvailable;
        if(baseline_.retainedSupportAvailable && support.physicalHypothesisCompatible) {
            // A repaired outward UI envelope is current-only. Neither it nor
            // transient failure renews the immutable aperture/reference seed.
            return;
        }
        if(!measured || !support.boundaryReference)return;
        baseline_.boundary=support.boundaryReference;
        baseline_.retainedSupportAvailable=true;
        baseline_.retainedSupport=support.currentSupport.samplingBounds;
        baseline_.physicalHypothesisAvailable=true;
        baseline_.physicalHypothesis=support.boundaryReference->BoundHypothesis();
    }
private:
    bool observed_=false;
    ActivePictureFrameIdentity last_;
    std::array<uint64_t,15> context_{};
    SourcePictureSupportRequest baseline_;
};
