#pragma once
#include "AsyncOutwardWindow.h"
#include "PreparedPictureEvidence.h"
#include <SourcePictureDetector.h>
#include <array>
#include <limits>

// One complete bounded worker request. Request metadata/slots are prepared by
// the presentation owner; workers only publish immutable completed components.
// Missing components are computed here, never awaited or deferred to a later
// render pass. Current queue/model/owner admission remains the caller's job.
template<class RawMeasurement>
struct PreparedOutwardWindowRequest
{
    static constexpr size_t FrameCount = AlphaSourceCrop::OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED;
    using RawWorker = OptionalAnalysisWorker<RawMeasurement>;
    std::array<PreparedAnalysisFrame, FrameCount> frames;
    std::array<ActivePictureFrameIdentity, FrameCount> identities;
    std::array<std::shared_ptr<typename RawWorker::Slot>, FrameCount> rawSlots;
    std::array<PreparedPictureEvidenceKey, FrameCount> retentionKeys;
    std::array<std::shared_ptr<PreparedPictureEvidenceWorker::Slot>, FrameCount> retentionSlots;
    std::array<double, FrameCount> sourceRates{};
    AlphaSourceCrop::AsyncOutwardWindowContext context;
};

struct PreparedOutwardWindowResult
{
    AlphaSourceCrop::AsyncOutwardWindowContext context;
    AlphaSourceCrop::BufferedPictureExpansionProof proof;
    // True only when all completed native observations exactly retain the base.
    // Other complete rejections are invalid proof; neither case means "late".
    bool noChange = false;
    uint64_t rawComputed = 0, rawReused = 0;
    uint64_t retentionComputed = 0, retentionReused = 0;
};

template<class RawMeasurement>
inline bool PreparedOutwardRawMeasurementCurrent(const PreparedAnalysisFrame& frame,
    const ActivePictureFrameIdentity& identity, const std::shared_ptr<const RawMeasurement>& raw)
{
    if (!raw || !PreparedSourceDetectorCurrent(raw->evidence) || raw->domainKey != frame.domainKey || !SameAnalysisFrameIdentity(raw->identity, identity))
        return false;
    auto imported = frame;
    imported.source = raw->source;
    return SamePreparedPictureFrame(imported, frame);
}

// A cold computation can race an independent producer completing the same
// immutable source. Publication must not erase a newer physical observation.
template<class RawMeasurement>
std::shared_ptr<const RawMeasurement> PublishPreparedOutwardRaw(
    const PreparedAnalysisFrame& frame,const ActivePictureFrameIdentity& identity,
    const std::shared_ptr<typename OptionalAnalysisWorker<RawMeasurement>::Slot>& slot,
    const std::shared_ptr<const RawMeasurement>& initiallyReady,
    std::shared_ptr<const RawMeasurement> computed)
{
    auto expected=std::atomic_load(&slot->ready);
    if(!PreparedOutwardRawMeasurementCurrent(frame,identity,computed))
        return PreparedOutwardRawMeasurementCurrent(frame,identity,expected)?expected:computed;
    for(;;) {
        if(PreparedOutwardRawMeasurementCurrent(frame,identity,expected)) {
            if(!SourcePictureMeasurementCanComplete(expected->evidence,computed->evidence))return expected;
        } else if(expected!=initiallyReady) {
            // An old producer cannot overwrite a newer source/context owner.
            return computed;
        }
        if(std::atomic_compare_exchange_weak(&slot->ready,&expected,computed)) {
            // Outward computation can complete this raw slot while its normal
            // worker is still running. Wake preparation at the common immutable
            // publication point, not only when that normal worker exits.
            if(slot->completion)slot->completion->Notify();
            return computed;
        }
    }
}

template<class RawMeasurement>
PreparedOutwardWindowResult EvaluatePreparedOutwardWindow(
    const PreparedOutwardWindowRequest<RawMeasurement>& request)
{
    using Request = PreparedOutwardWindowRequest<RawMeasurement>;
    using RawWorker = typename Request::RawWorker;
    constexpr size_t count = Request::FrameCount;
    static_assert(count == 3, "Outward proof requires three consecutive real sources");
    // Validate the entire request before publishing any component. In particular,
    // a changed interpretation or policy cannot poison a currently owned slot.
    for (size_t i = 0; i < count; ++i)
    {
        const auto& key = request.retentionKeys[i];
        const auto& identity = request.identities[i];
        if (!request.rawSlots[i] || !request.retentionSlots[i] ||
            !ValidPreparedPictureEvidenceKey(key) || key.kind != PreparedPictureEvidenceKind::Retention ||
            !SameAnalysisFrameIdentity(identity, request.context.contributors[i]) ||
            !SameAnalysisFrameIdentity(identity, key.identity) ||
            !SamePreparedPictureFrame(request.frames[i], key.frame) ||
            !SamePreparedPictureBounds(key.base, request.context.base) ||
            key.policyGeneration != request.context.policyGeneration ||
            key.continuityGeneration != request.context.continuityGeneration ||
            !SamePreparedPictureOwner(key.rawOwner, request.rawSlots[i]) ||
            !std::isfinite(request.sourceRates[i]) || request.sourceRates[i] <= 0 ||
            (i && (request.identities[i-1].acceptedSequence == (std::numeric_limits<uint64_t>::max)() ||
                identity.acceptedSequence != request.identities[i-1].acceptedSequence + 1 ||
                identity.transportGeneration != request.identities[0].transportGeneration ||
                identity.sourceFormatGeneration != request.identities[0].sourceFormatGeneration ||
                identity.viewportGeneration != request.identities[0].viewportGeneration ||
                identity.rendererGeneration != request.identities[0].rendererGeneration ||
                identity.analysisDomainKey != request.identities[0].analysisDomainKey)))
            throw std::invalid_argument("inconsistent prepared outward window");
    }

    PreparedOutwardWindowResult result;
    result.context = request.context;
    result.noChange = true;
    std::array<AlphaSourceCrop::BufferedPictureExpansionSample, count> samples;
    for (size_t i = 0; i < count; ++i)
    {
        const auto& frame = request.frames[i];
        const auto& identity = request.identities[i];
        auto raw = RawWorker::Ready(request.rawSlots[i]);
        if (PreparedOutwardRawMeasurementCurrent(frame, identity, raw) &&
            SourcePictureMeasurementWorkComplete(raw->evidence))
            ++result.rawReused;
        else
        {
            auto measured = std::make_shared<RawMeasurement>();
            measured->source = frame.source;
            measured->identity = identity;
            measured->domainKey = frame.domainKey;
            measured->evidence = DetectPreparedSourcePictureBounded(frame.source,identity).evidence;
            measured->darkness = EvaluateActivePictureArDarkness(frame.source, measured->evidence);
            std::shared_ptr<const RawMeasurement> computed=std::move(measured);
            raw=PublishPreparedOutwardRaw<RawMeasurement>(frame,identity,
                request.rawSlots[i],raw,std::move(computed));
            ++result.rawComputed;
        }
        // Incomplete bounded work is neither a negative proof nor a completed
        // window. Keep its raw facts, but let a later authorized job finish it.
        if(!SourcePictureMeasurementWorkComplete(raw->evidence))throw AnalysisEvaluationCancelled{};
        auto key = request.retentionKeys[i];
        key.rawRevision=raw;
        auto retention = PreparedPictureEvidenceWorker::Ready(request.retentionSlots[i]);
        if (retention && SamePreparedPictureEvidenceKey(retention->key, key))
            ++result.retentionReused;
        else
        {
            retention = std::make_shared<const PreparedPictureEvidence>(
                EvaluatePreparedPictureEvidence(key, raw->evidence, raw->darkness));
            std::atomic_store(&request.retentionSlots[i]->ready, retention);
            ++result.retentionComputed;
        }
        result.noChange = result.noChange && raw->evidence.available &&
            raw->evidence.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
            SamePreparedPictureBounds(raw->evidence.trustedBounds, request.context.base);
        samples[i] = AlphaSourceCrop::MakeBufferedPictureSample(identity, raw->evidence,
            request.sourceRates[i], raw->darkness.global.evaluated, raw->darkness.EffectiveNearBlack());
        samples[i].retention = retention->retention;
    }
    result.proof = AlphaSourceCrop::BuildBufferedPictureExpansion(samples.data(), samples.size(),
        request.context.base, static_cast<uint8_t>(count-1), static_cast<uint8_t>(count-1),
        request.context.continuityGeneration, request.context.policyGeneration);
    return result;
}