#pragma once
#include "AsyncOutwardWindow.h"
#include "PreparedPictureEvidence.h"
#include <SourcePictureDetector.h>
#include <AnalysisDiagnosticHooks.h>
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

// Support is a separately completed capability. A negative attempt is terminal
// scheduling data, not crop permission. Positive proof still has its own exact
// source and bounded-aperture contract; raw native completion cannot imply it.
inline bool PreparedSourceSupportAttemptCurrent(const PreparedAnalysisFrame& frame,
    const ActivePictureFrameIdentity& identity,const std::shared_ptr<const SourcePictureSupportResult>& result)
{
    if(!result || !SameAnalysisFrameIdentity(result->currentSupport.identity,identity))return false;
    const auto& support=result->currentSupport;
    if(!support.verified)return true;
    if(!support.evaluated)return false;
    const auto& b=support.samplingBounds;const auto& v=support.verification;
    const auto valid=[&](const ActivePictureBounds& r) {
        return r.rasterWidth==frame.source.width && r.rasterHeight==frame.source.height &&
            r.left>=0 && r.top>=0 && r.right<=r.rasterWidth && r.bottom<=r.rasterHeight &&
            r.left<r.right && r.top<r.bottom;
    };
    const bool full=b.left==0 && b.top==0 && b.right==frame.source.width && b.bottom==frame.source.height;
    return valid(b) && valid(v.bounds) && v.bounds.left>=b.left && v.bounds.top>=b.top &&
        v.bounds.right<=b.right && v.bounds.bottom<=b.bottom &&
        (v.sourceGeneration==identity.transportGeneration || v.sourceGeneration==identity.sourceFormatGeneration) &&
        v.vertical!=ActivePictureCandidateVerificationState::CONTRADICTED &&
        v.horizontal!=ActivePictureCandidateVerificationState::CONTRADICTED &&
        (full || v.vertical==ActivePictureCandidateVerificationState::VERIFIED ||
            v.horizontal==ActivePictureCandidateVerificationState::VERIFIED);
}
template<class RawMeasurement>
inline bool PreparedNativeSupportWorkComplete(const PreparedAnalysisFrame& frame,
    const ActivePictureFrameIdentity& identity,const std::shared_ptr<const RawMeasurement>& raw)
{
    return PreparedOutwardRawMeasurementCurrent(frame,identity,raw) &&
        SourcePictureMeasurementWorkComplete(raw->evidence) &&
        PreparedSourceSupportAttemptCurrent(frame,identity,raw->sourcePictureSupport);
}

template<class RawMeasurement>
inline void TracePreparedRawPublication(const PreparedAnalysisFrame& frame,const ActivePictureFrameIdentity& identity,
    const char* reason,const std::shared_ptr<const RawMeasurement>& prior,
    const std::shared_ptr<const RawMeasurement>& incoming,const std::shared_ptr<const RawMeasurement>& selected)
{
    auto trace=AnalysisTraceRecord(identity,nullptr,"source-raw-publication",reason);
    const auto capabilities=[&](const std::shared_ptr<const RawMeasurement>& raw)->uint64_t {
        if(!raw)return 0;
        return (PreparedOutwardRawMeasurementCurrent(frame,identity,raw)?1u:0u) |
            (SourcePictureMeasurementWorkComplete(raw->evidence)?2u:0u) | (raw->sourcePictureSupport?4u:0u) |
            (PreparedSourceSupportAttemptCurrent(frame,identity,raw->sourcePictureSupport)?8u:0u) |
            (raw->sourcePictureSupport && raw->sourcePictureSupport->currentSupport.verified?16u:0u);
    };
    trace.counters[0]=capabilities(prior);trace.counters[1]=capabilities(incoming);trace.counters[2]=capabilities(selected);
    if(prior) {trace.counters[3]=prior->identity.acceptedSequence;trace.counters[4]=prior->domainKey;}
    if(incoming) {trace.counters[5]=incoming->identity.acceptedSequence;trace.counters[6]=incoming->domainKey;}
    AnalysisDiagnosticTrace::Emit(trace);
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
    if(!PreparedOutwardRawMeasurementCurrent(frame,identity,computed)) {
        const auto selected=PreparedOutwardRawMeasurementCurrent(frame,identity,expected)?expected:computed;
        TracePreparedRawPublication(frame,identity,"foreign-computation",expected,computed,selected);
        return selected;
    }
    for(;;) {
        auto publication=computed;const char* reason="initial-publication";
        if(PreparedOutwardRawMeasurementCurrent(frame,identity,expected)) {
            const bool nativeUpgrade=SourcePictureMeasurementCanComplete(expected->evidence,computed->evidence);
            const bool priorSupport=PreparedSourceSupportAttemptCurrent(frame,identity,expected->sourcePictureSupport);
            const bool newSupport=PreparedSourceSupportAttemptCurrent(frame,identity,computed->sourcePictureSupport);
            const bool supportUpgrade=newSupport && (!priorSupport ||
                (!expected->sourcePictureSupport->currentSupport.verified && computed->sourcePictureSupport->currentSupport.verified));
            if(!nativeUpgrade && !supportUpgrade) {
                TracePreparedRawPublication(frame,identity,"kept-current-capabilities",expected,computed,expected);
                return expected;
            }
            reason=nativeUpgrade?(supportUpgrade?"native-and-support-upgrade":"native-upgrade"):"support-upgrade";
            const auto support=supportUpgrade?computed->sourcePictureSupport:
                priorSupport?expected->sourcePictureSupport:std::shared_ptr<const SourcePictureSupportResult>{};
            if(!nativeUpgrade || support!=computed->sourcePictureSupport) {
                auto combined=std::make_shared<RawMeasurement>(nativeUpgrade?*computed:*expected);
                combined->sourcePictureSupport=support;
                publication=std::move(combined);
            }
        } else if(expected!=initiallyReady) {
            // An old producer cannot overwrite a newer source/context owner.
            TracePreparedRawPublication(frame,identity,"foreign-slot-owner",expected,computed,expected);
            return computed;
        }
        if(std::atomic_compare_exchange_weak(&slot->ready,&expected,publication)) {
            // Outward computation can complete this raw slot while its normal
            // worker is still running. Wake preparation at the common immutable
            // publication point, not only when that normal worker exits.
            if(slot->completion)slot->completion->Notify();
            TracePreparedRawPublication(frame,identity,reason,expected,computed,publication);
            return publication;
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