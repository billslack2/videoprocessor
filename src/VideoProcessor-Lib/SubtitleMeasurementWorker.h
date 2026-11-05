#pragma once
#include <AnalysisCompletionSignal.h>
#include <SubtitleCurrentCueProofSlot.h>
#include <SourcePictureDetector.h>
#include <SourceSynchronizedAnalysis.h>
#include <SubtitleBoxLookahead.h>
#include <SubtitleCadenceGuard.h>
#include <SubtitleCurrentCuePresentation.h>
#include <vprenderer/SubtitleAspectRetention.h>
#include <SubtitlePreviewMode.h>
#include <AnalysisDiagnosticHooks.h>
#include <DebugLog.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iterator>
#include <fstream>
#include <cstdlib>
#include <mutex>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

// The optional fault capture stays diagnostic: a current worker observation and
// a current shared picture are both required. This never grants crop authority.
inline bool IsSubtitleProofFailureCaptureCandidate(const AnalysisLumaSource& source,
    const SubtitleBoxObservation& key,const SubtitleBoxObservation& result)
{
    constexpr size_t MaxFaultCaptureBytes=64u*1024u*1024u;
    if(!source.IsValid() || source.dataBytes>MaxFaultCaptureBytes ||
        !key.sharedPicture.required ||
        !key.sharedPicture.AvailableFor(key.identity,source.width,source.height) ||
        !SameAnalysisFrameIdentity(key.identity,result.identity) ||
        !result.sharedPicture.Matches(key.sharedPicture) || !result.analyzed ||
        !result.sharedProbeAttempted || !result.sharedProbeDetected ||
        result.sharedProbeWorkLimit || result.text.workLimit ||
        result.text.detected || result.pendingRefresh || result.discontinuity)
        return false;
    const auto& shared=key.sharedPicture.bounds;
    return (shared.top>0 && result.pictureTop==0) ||
        (shared.bottom<source.height && result.pictureBottom==source.height);
}

inline bool ShouldCaptureSubtitleDiagnosticFrame(bool pathConfigured,bool sourceValid,
    bool proofFailureMode,bool allowDarkStartupCapture,uint64_t acceptedSequence,
    bool globalNearBlack,bool proofFailureCandidate)
{
    if(!pathConfigured || !sourceValid)return false;
    return proofFailureMode ? proofFailureCandidate :
        (allowDarkStartupCapture ? acceptedSequence>=240 : !globalNearBlack);
}

// One claim per linked module. The renderer DLL owns this sampler in normal use.
inline std::atomic_bool& SubtitleDiagnosticCaptureClaimed()
{
    static std::atomic_bool claimed{false};
    return claimed;
}

inline bool TryClaimSubtitleDiagnosticCapture(bool proofFailureMode)
{
    (void)proofFailureMode; // All capture modes write the same module-owned file.
    return !SubtitleDiagnosticCaptureClaimed().exchange(true);
}

// Every source frame gets independent geometry and raw ink. Rechecking only
// old glyph rectangles cannot discover a new word/companion outside them, and
// copied ink must never count as fresh lookahead confirmation. Safe same-frame
// sampled-pixel/buffer reuse remains controlled by the detector's existing mode.
class SubtitleMeasurementSampler {
public:
    void Reset() { m_scanner.Reset();m_analysisDomainKey=0; }
    bool TryMeasureCurrentSpatialNominee(const AnalysisLumaSource& source,
        const SubtitleBoxObservation& key,const ActivePictureEvidence& nativeEvidence,
        SubtitleBoxObservation& result) {
        const auto& bounds=nativeEvidence.proposedBounds;
        const auto& physical=nativeEvidence.physicalEdges;
        if(!source.IsValid() || !source.generation ||
            (source.generation!=key.identity.sourceFormatGeneration &&
             source.generation!=key.identity.transportGeneration) || !key.identity.acceptedSequence ||
            key.discontinuity || key.analysisRefresh || key.pendingRefresh ||
            key.width!=source.width || key.height!=source.height ||
            !nativeEvidence.available || !nativeEvidence.currentSourceIdentityAvailable ||
            !SameAnalysisFrameIdentity(nativeEvidence.currentSourceIdentity,key.identity) ||
            nativeEvidence.detector!=ActivePictureDetectorKind::SourceSpatialMeasured ||
            nativeEvidence.authorityOrigin!=ActivePictureAuthorityOrigin::NATIVE ||
            !physical.evaluated || !physical.estimateAvailable ||
            (physical.sourceGeneration!=source.generation &&
             physical.sourceGeneration!=key.identity.sourceFormatGeneration) ||
            bounds.rasterWidth!=source.width || bounds.rasterHeight!=source.height ||
            bounds.left!=0 || bounds.right!=source.width ||
            bounds.top<=0 || bounds.top>=bounds.bottom || bounds.bottom>=source.height ||
            physical.estimate.left!=bounds.left || physical.estimate.top!=bounds.top ||
            physical.estimate.right!=bounds.right || physical.estimate.bottom!=bounds.bottom ||
            physical.estimate.rasterWidth!=source.width || physical.estimate.rasterHeight!=source.height)
            return false;
        // A nominee selects where to inspect this source. It supplies neither
        // independent bar evidence nor retained presentation/crop permission.
        // Reset before the full scan so its raw ink cannot come from another source.
        m_scanner.Reset();m_analysisDomainKey=key.identity.analysisDomainKey;
        m_scanner.SetOptimizationMode(key.optimizationMode);
        m_scanner.SetScriptHint(key.scriptHint);
        m_scanner.SetNearBarDistance(key.nearBarDistance);
        result={};result.identity=key.identity;result.sharedPicture=key.sharedPicture;
        result.width=source.width;result.height=source.height;
        result.policyGeneration=key.policyGeneration;result.continuityGeneration=key.continuityGeneration;
        result.nearBarDistance=key.nearBarDistance;result.optimizationMode=key.optimizationMode;
        result.scriptHint=key.scriptHint;result.analysisStride=key.analysisStride;
        result.pictureAuthorityUnavailableForMeasurement=
            !key.sharedPicture.AvailableFor(key.identity,source.width,source.height);
        result.pictureTop=bounds.top;result.pictureBottom=bounds.bottom;
        result.currentSpatialNomineeInspection=true;result.analyzed=true;
        result.barEvidence.reason="current-spatial-nominee-inspection-only";
        result.text=m_scanner.Analyze(source,bounds.top,bounds.bottom,
            key.identity.acceptedSequence,key.identity.viewportGeneration);
        result.ink=m_scanner.InkSnapshot();
        return true;
    }
    SubtitleBoxObservation Measure(const AnalysisLumaSource& source,const SubtitleBoxObservation& key,
        const SubtitleBarTrackingReference& prior,double /*fps*/,
        const ActivePictureEvidence& nativeEvidence = {},
        std::shared_ptr<const CurrentSourceSupportEvidence> currentSupport = {},
        const std::function<std::shared_ptr<const CurrentSourceSupportEvidence>()>& currentSupportProvider = {}) {
        const auto started=std::chrono::steady_clock::now();
        if(m_analysisDomainKey!=key.identity.analysisDomainKey) {
            m_scanner.Reset();m_analysisDomainKey=key.identity.analysisDomainKey;
        }
        m_scanner.SetOptimizationMode(key.optimizationMode);
        m_scanner.SetScriptHint(key.scriptHint);
        m_scanner.SetNearBarDistance(key.nearBarDistance);
        auto result=SubtitleBoxLookahead::Measure(m_scanner,source,{},key.identity,key.discontinuity,
            prior,key.policyGeneration,key.continuityGeneration,key.sharedPicture,std::move(currentSupport),currentSupportProvider);
        result.analysisStride=key.analysisStride;
        // A cold independent bar probe can be unavailable even though this
        // source has a spatial nominee. Inspect that nominee only when no raw
        // glyph scan ran; completed negative scans and retained hints keep their
        // original semantics. The existing masked verifier below is still the
        // sole authority for accepting the nominee through caption pixels.
        if(SourcePictureDetectorEnabled() && !result.ink && !result.barAuthority &&
            !result.text.topSearchComplete && !result.text.bottomSearchComplete &&
            !result.text.workLimit && !result.inspectionCandidateUsed && !result.sharedProbeAttempted &&
            !key.sharedPicture.AvailableFor(key.identity,source.width,source.height))
            TryMeasureCurrentSpatialNominee(source,key,nativeEvidence,result);
        const bool currentInspectionMask=SourcePictureDetectorEnabled() &&
            SubtitleBoxLookahead::HasCurrentInspectionGlyphMeasurement(result);
        if ((!result.inspectionCandidateUsed || currentInspectionMask) && result.text.detected && !result.text.held && !result.text.workLimit) {
            // Geometry and current glyph ownership are independent facts. Active
            // mode verifies the spatial nominee through its occlusion; a caption
            // never nominates a replacement rectangle.
            auto proofSource=source;proofSource.generation=result.identity.transportGeneration;
            if(SourcePictureDetectorEnabled()) {
                result.subtitleArProof=VerifySuppliedActivePictureCandidateWithSubtitle(proofSource,nativeEvidence,result,
                    result.identity,result.policyGeneration,result.continuityGeneration,result.scriptHint);
                auto trace=AnalysisTraceRecord(result.identity,nullptr,"source-picture-caption-proof",result.subtitleArProof.reason);
                trace.policyGeneration=result.policyGeneration;trace.continuityGeneration=result.continuityGeneration;
                trace.resultCode=static_cast<int32_t>(result.subtitleArProof.evidence.detector);
                trace.flags=(result.subtitleArProof.accepted?1u:0u)|(nativeEvidence.physicalEdges.estimateAvailable?2u:0u);
                AnalysisTraceRect(trace.measured,nativeEvidence.physicalEdges.estimate);
                AnalysisTraceRect(trace.published,result.subtitleArProof.candidate);
                AnalysisDiagnosticTrace::Emit(trace);
            } else if(result.independentBarAnalysis) {
                const auto raw=nativeEvidence.available ? nativeEvidence : ExtractActivePictureEvidence(source);
                result.subtitleArProof=EvaluateSubtitleAssistedVerticalBarProof(proofSource,raw,result,
                    result.identity,result.policyGeneration,result.continuityGeneration,result.scriptHint);
            }
            if (result.subtitleArProof.accepted)
                result.subtitleArDarkness=EvaluateActivePictureArDarkness(proofSource,result.subtitleArProof.evidence);
        }
        // Explicit per-process diagnostic opt-in: save one native
        // input without changing capture, subtitle settings or presentation.
        static const std::string capturePath=[] {
            char* value=nullptr;size_t size=0;
            _dupenv_s(&value,&size,"VP_SUBTITLE_CAPTURE_PATH");
            const std::string path=value?value:"";std::free(value);return path;
        }();
        static const bool captureDark=[] {
            char* value=nullptr;size_t size=0;
            _dupenv_s(&value,&size,"VP_SUBTITLE_CAPTURE_ALLOW_DARK");
            const bool enabled=value && std::string(value)=="1";
            std::free(value);return enabled;
        }();
        static const bool captureOnProofFailure=[] {
            char* value=nullptr;size_t size=0;
            _dupenv_s(&value,&size,"VP_SUBTITLE_CAPTURE_ON_PROOF_FAILURE");
            const bool enabled=value && std::string(value)=="1";
            std::free(value);return enabled;
        }();
        // Dark-scene diagnostics must not be vetoed by the classification being
        // investigated. Fault mode waits for the exact current worker failure.
        if(SubtitleDiagnosticCaptureClaimed().load())
            m_capturedDiagnostic=true;
        if(!m_capturedDiagnostic && !capturePath.empty() && source.IsValid() &&
            ShouldCaptureSubtitleDiagnosticFrame(true,true,captureOnProofFailure,captureDark,
                key.identity.acceptedSequence,
                captureOnProofFailure ? false : EvaluateActivePictureGlobalNearBlack(source).nearBlack,
                captureOnProofFailure && IsSubtitleProofFailureCaptureCandidate(source,key,result)) &&
            TryClaimSubtitleDiagnosticCapture(captureOnProofFailure)) {
            m_capturedDiagnostic=true;
            std::ofstream pixels(capturePath,std::ios::binary);
            pixels.write(reinterpret_cast<const char*>(source.data),source.dataBytes);
            std::ofstream metadata(std::string(capturePath)+".txt");
            metadata<<source.width<<' '<<source.height<<' '<<source.rowBytes<<' '<<source.chromaRowBytes<<' '
                <<static_cast<int>(source.format)<<' '<<static_cast<int>(source.encoding)<<' '
                <<static_cast<int>(source.colorspace)<<' '<<result.pictureTop<<' '<<result.pictureBottom<<' '
                <<static_cast<int>(key.scriptHint)<<' '<<static_cast<int>(source.range)<<'\n';
            if(captureOnProofFailure) {
                const auto& id=key.identity;
                const auto& shared=key.sharedPicture.bounds;
                const auto& prior=result.incomingBarTrackingReference;
                const auto& bars=result.barEvidence;
                std::ofstream diagnostic(std::string(capturePath)+".diagnostic.txt");
                diagnostic<<"capture_mode=proof_failure\n"
                    <<"identity transport="<<id.transportGeneration
                    <<" sequence="<<id.acceptedSequence
                    <<" source_frame="<<id.sourceFrameNumber
                    <<" capture_timestamp="<<id.captureTimestamp
                    <<" format="<<id.sourceFormatGeneration
                    <<" domain="<<id.analysisDomainKey
                    <<" viewport="<<id.viewportGeneration
                    <<" renderer="<<id.rendererGeneration<<'\n'
                    <<"policy="<<key.policyGeneration
                    <<" continuity="<<key.continuityGeneration
                    <<" near_bar="<<key.nearBarDistance
                    <<" optimization="<<key.optimizationMode
                    <<" script="<<static_cast<int>(key.scriptHint)<<'\n'
                    <<"shared_sequence="<<key.sharedPicture.identity.acceptedSequence
                    <<" shared_bounds="<<shared.left<<','<<shared.top<<','<<shared.right<<','<<shared.bottom<<'\n'
                    <<"prior valid="<<prior.valid
                    <<" sequence="<<prior.identity.acceptedSequence
                    <<" edges="<<prior.pictureTop<<','<<prior.pictureBottom<<'\n'
                    <<"result analyzed="<<result.analyzed
                    <<" bar_authority="<<result.barAuthority
                    <<" independent="<<result.independentBarAnalysis
                    <<" picture="<<result.pictureTop<<','<<result.pictureBottom
                    <<" text_detected="<<result.text.detected
                    <<" text_work_limit="<<result.text.workLimit
                    <<" text_reason="<<(result.text.diagnosticReason?result.text.diagnosticReason:"")
                    <<" components="<<result.text.diagnosticComponentCount
                    <<" near_bar_gap="<<result.text.diagnosticNearBarGap
                    <<" top_search_complete="<<result.text.topSearchComplete
                    <<" bottom_search_complete="<<result.text.bottomSearchComplete<<'\n'
                    <<"probe attempted="<<result.sharedProbeAttempted
                    <<" detected="<<result.sharedProbeDetected
                    <<" work_limit="<<result.sharedProbeWorkLimit
                    <<" reason="<<(result.sharedProbeReason?result.sharedProbeReason:"")
                    <<" anchor="<<result.sharedProbeAnchor.left<<','<<result.sharedProbeAnchor.top<<','
                    <<result.sharedProbeAnchor.right<<','<<result.sharedProbeAnchor.bottom<<'\n'
                    <<"bars reason="<<(bars.reason?bars.reason:"")
                    <<" available="<<bars.available
                    <<" edges="<<bars.top<<','<<bars.bottom
                    <<" support="<<bars.topSupport<<','<<bars.bottomSupport
                    <<" boundary_support="<<bars.topBoundarySupport<<','<<bars.bottomBoundarySupport
                    <<" black_fraction="<<bars.topBlackFraction<<','<<bars.bottomBlackFraction
                    <<" max_intrusion="<<bars.topMaxIntrusion<<','<<bars.bottomMaxIntrusion
                    <<" reference_support="<<bars.topReferenceBoundarySupport<<','<<bars.bottomReferenceBoundarySupport
                    <<" reference_fraction="<<bars.topReferenceBlackFraction<<','<<bars.bottomReferenceBlackFraction
                    <<" reference_intrusion="<<bars.topReferenceMaxIntrusion<<','<<bars.bottomReferenceMaxIntrusion
                    <<" reference_exact="<<bars.topRevalidatedAtReference<<','<<bars.bottomRevalidatedAtReference
                    <<" reference_nearby="<<bars.topRevalidatedNearby<<','<<bars.bottomRevalidatedNearby
                    <<" nearby_delta="<<bars.topReferenceDelta<<','<<bars.bottomReferenceDelta
                    <<" nearby_probes="<<bars.nearbyProbes
                    <<" samples="<<bars.samples<<'\n';
                pixels.flush();metadata.flush();diagnostic.flush();
                DebugLog::Log("Subtitle fault capture: success=%d bytes=%zu sequence=%llu domain=%llu",
                    pixels.good() && metadata.good() && diagnostic.good() ? 1 : 0,
                    source.dataBytes,
                    static_cast<unsigned long long>(key.identity.acceptedSequence),
                    static_cast<unsigned long long>(key.identity.analysisDomainKey));
            }
        }
        result.analysisMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        return result;
    }
private:
    SubtitleBoxDetector m_scanner;
    uint64_t m_analysisDomainKey=0;
    bool m_capturedDiagnostic=false;
};

// A successful accepted cue is retained as a comparison template only. The
// source owner is carried while the seed job samples it, then discarded; no
// previous frame pixels are retained for presentation.
struct SubtitleCurrentCueTemplateInput {
    SubtitleBoxObservation observation;
    SubtitleBoxResult acceptedCue;
    AnalysisLumaSource source;
    ActivePictureBounds physicalBase;
    SubtitleCutPasteGeometry composedOperation;
    SubtitlePreviewMode sourceMode=SubtitlePreviewMode::None;
    bool compositionSucceeded=false;
    bool acceptedCurrentScan=false;
    // Completed exact-source compositor qualification; presentation only.
    std::shared_ptr<const QualifiedSubtitlePresentationEvidence> preparedPresentation;
    // Same-source raw/retained-pose binding only; worker samples the new seed.
    std::shared_ptr<const SubtitleCurrentPreparedCueCertificate> heldCurrentCue;
    int backgroundMode=3;
    double frameMs=1000.0/60.0;
    int holdMs=250;
};

// This alternative seeds only a comparison template, never a native lease.
inline bool HasQualifiedCurrentCueSamplingBasis(const SubtitleCurrentCueTemplateInput& input) {
    const auto& o=input.observation;const auto& proof=input.preparedPresentation;
    const auto& op=input.composedOperation;
    if(!SubtitlePreviewOverridesClassicHandling(input.sourceMode) || !proof ||
        !proof->qualified || proof->status!=QualifiedSubtitlePresentationStatus::Qualified ||
        !proof->currentCompositionVerified || proof->currentCompositionMode!=input.sourceMode ||
        !SameAnalysisFrameIdentity(proof->identity,o.identity) ||
        !SameSubtitlePresentationBounds(proof->physicalBase,input.physicalBase) ||
        !o.samplingBasis.MatchesBase(o,input.physicalBase) || !input.compositionSucceeded || !op.valid)
        return false;
    const auto& erased=proof->erasedSourceBounds;const auto& content=op.content;
    return erased.rasterWidth==o.width && erased.rasterHeight==o.height &&
        erased.left==content.left && erased.top==content.top &&
        erased.right==content.right && erased.bottom==content.bottom;
}

enum class SubtitleCurrentCueProofStatus : uint8_t {
    NoTemplate, NoTask, NotEligible, Queued, Pending, Ready, FallbackQueued, Failed, Expired, Consumed
};

enum class SubtitleCurrentCueProofProvenance : uint8_t {
    None, PresentationOnlyCurrentPixels, PresentationOnlyExactSourcePixels
};

// Typed presentation evidence. It is deliberately separate from the ordinary
// analyzed observation cache and carries no physical, bar, absence, or new-cue
// authority. `currentKey` is a sanitized exact-source key, not a scan result.
class SubtitleCurrentCueProofReceipt {
    friend class SubtitleMeasurementWorker;
    friend class SubtitleBoxPresentation;
    bool m_valid=false;
    SourcePictureEquivalenceToken m_sourceEquivalence;
    SubtitleCurrentCueProofProvenance m_provenance=SubtitleCurrentCueProofProvenance::None;
    uint64_t m_templateRevision=0;
    ActivePictureFrameIdentity m_seedIdentity;
    uint64_t m_workerThreadId=0;
    double m_workerMilliseconds=0;
    int m_verifiedLineCount=0;
    SubtitlePreviewMode m_sourceMode=SubtitlePreviewMode::None;
    int m_backgroundMode=3;
    SubtitleBoxObservation m_currentKey;
    SubtitleBoxResult m_acceptedCue;
    std::shared_ptr<const SubtitleInkSnapshot> m_currentInk;
    ActivePictureBounds m_physicalBase;
    SubtitleCutPasteGeometry m_operation;
    std::array<SubtitleBoxRect,3> m_verifiedSemanticLineBounds{};
    bool m_cropExcludedBarsVerified=false;
    size_t m_footprintSamples=0,m_barVisibilitySamples=0;
    const char* m_reason="not-ready";
    std::shared_ptr<std::atomic_bool> m_consumed;
    std::shared_ptr<const std::atomic_uint64_t> m_minimumRevision;
public:
    bool Valid() const {
        return m_valid && m_minimumRevision &&
            m_templateRevision>=m_minimumRevision->load(std::memory_order_acquire) &&
            (m_provenance!=SubtitleCurrentCueProofProvenance::PresentationOnlyExactSourcePixels ||
                m_sourceEquivalence.MatchesSource(m_currentKey.identity));
    }
    SubtitleCurrentCueProofProvenance Provenance() const {return m_provenance;}
    uint64_t TemplateRevision() const {return m_templateRevision;}
    const ActivePictureFrameIdentity& SeedIdentity() const {return m_seedIdentity;}
    uint64_t WorkerThreadId() const {return m_workerThreadId;}
    double WorkerMilliseconds() const {return m_workerMilliseconds;}
    int VerifiedLineCount() const {return m_verifiedLineCount;}
    SubtitlePreviewMode SourceMode() const {return m_sourceMode;}
    int BackgroundMode() const {return m_backgroundMode;}
    const SubtitleBoxObservation& CurrentKey() const {return m_currentKey;}
    const SubtitleBoxResult& AcceptedCue() const {return m_acceptedCue;}
    const std::shared_ptr<const SubtitleInkSnapshot>& CurrentInk() const {return m_currentInk;}
    const ActivePictureBounds& PhysicalBase() const {return m_physicalBase;}
    const SubtitleCutPasteGeometry& Operation() const {return m_operation;}
    const std::array<SubtitleBoxRect,3>& VerifiedSemanticLineBounds() const {
        return m_verifiedSemanticLineBounds;
    }
    bool CropExcludedBarsVerified() const {return m_cropExcludedBarsVerified;}
    size_t FootprintSamples() const {return m_footprintSamples;}
    size_t BarVisibilitySamples() const {return m_barVisibilitySamples;}
    const char* Reason() const {return m_reason;}
};

// The source ticket owns this immutable completion. Layout is metadata only;
// neither the publication nor its slot retains the captured movie buffer.
struct SubtitleCurrentCueProofPublication {
    SubtitleCurrentCueProofReceipt receipt;
    AnalysisLumaSource sourceLayout;
};

inline bool SubtitleBoxPresentation::ConsumeCurrentCueProof(
    const SubtitleCurrentCueProofReceipt& receipt,SubtitleBoxPreview& outPreview,
    SubtitleBoxResult& outCue) {
    outPreview={};outCue={};
    if(!receipt.Valid() || (receipt.m_provenance!=
            SubtitleCurrentCueProofProvenance::PresentationOnlyCurrentPixels && receipt.m_provenance!=
            SubtitleCurrentCueProofProvenance::PresentationOnlyExactSourcePixels) ||
        !receipt.m_templateRevision || !receipt.m_currentInk ||
        receipt.m_currentInk->rawEvidenceComplete || !receipt.m_currentInk->paletteValid ||
        !receipt.m_acceptedCue.detected || receipt.m_acceptedCue.held ||
        receipt.m_acceptedCue.confirmationPending || receipt.m_acceptedCue.workLimit ||
        receipt.m_acceptedCue.lineCount<1 || receipt.m_acceptedCue.lineCount>3 ||
        !receipt.m_currentKey.identity.acceptedSequence ||
        !receipt.m_currentKey.width || !receipt.m_currentKey.height ||
        receipt.m_physicalBase.rasterWidth!=receipt.m_currentKey.width ||
        receipt.m_physicalBase.rasterHeight!=receipt.m_currentKey.height ||
        receipt.m_physicalBase.left<0 || receipt.m_physicalBase.top<0 ||
        receipt.m_physicalBase.right>receipt.m_currentKey.width ||
        receipt.m_physicalBase.bottom>receipt.m_currentKey.height ||
        receipt.m_physicalBase.left>=receipt.m_physicalBase.right ||
        receipt.m_physicalBase.top>=receipt.m_physicalBase.bottom ||
        (receipt.m_sourceMode==SubtitlePreviewMode::None && receipt.m_operation.valid) ||
        (receipt.m_sourceMode!=SubtitlePreviewMode::None && !receipt.m_operation.valid) ||
        (receipt.m_currentKey.sharedPicture.required &&
            !receipt.m_currentKey.sharedPicture.AvailableFor(receipt.m_currentKey.identity,
                receipt.m_currentKey.width,receipt.m_currentKey.height)) ||
        (receipt.m_operation.valid && !receipt.m_operation.content.Valid()))return false;
    const int currentPictureTop=receipt.m_sourceMode==SubtitlePreviewMode::None?
        receipt.m_physicalBase.top:receipt.m_operation.pictureTop;
    const int currentPictureBottom=receipt.m_sourceMode==SubtitlePreviewMode::None?
        receipt.m_physicalBase.bottom:receipt.m_operation.pictureBottom;
    if(currentPictureTop<0 || currentPictureBottom>receipt.m_currentKey.height ||
        currentPictureTop>=currentPictureBottom)return false;
    bool expected=false;
    if(!receipt.m_consumed || !receipt.m_consumed->compare_exchange_strong(
            expected,true,std::memory_order_acq_rel))return false;
    if(!receipt.Valid())return false;
    auto cue=receipt.m_acceptedCue;cue.held=true;cue.revised=false;
    cue.confirmationPending=false;cue.workLimit=false;
    outPreview.current=receipt.m_currentKey;
    // The worker request key is deliberately unmeasured (its picture edges are
    // normally zero). For presentation only, expose the already verified
    // physical extent bound to this receipt; this is not detector/bar authority.
    outPreview.current.pictureTop=currentPictureTop;
    outPreview.current.pictureBottom=currentPictureBottom;
    outPreview.current.analyzed=true;
    outPreview.current.pendingRefresh=true;
    outPreview.current.analysisRefresh=false;
    outPreview.current.cadenceSkipped=false;
    outPreview.current.independentBarAnalysis=false;
    outPreview.current.barAuthority=false;
    outPreview.current.barTrackingAuthority=false;
    outPreview.current.currentAnchorUsesTrackedEdge=false;
    outPreview.current.sharedProbeAttempted=false;
    outPreview.current.sharedProbeDetected=false;
    outPreview.current.sharedProbeWorkLimit=false;
    outPreview.current.sharedProbeReason="presentation-only-current-pixel-proof";
    outPreview.current.sharedProbeAnchor={};
    outPreview.current.subtitleArProof={};
    outPreview.current.subtitleArDarkness={};
    outPreview.current.barStripClearance={};
    outPreview.current.barEvidence={};
    outPreview.current.hasBarTrackingReference=false;
    outPreview.current.barTrackingReferenceIdentity={};
    outPreview.current.incomingBarTrackingReference={};
    outPreview.text=cue;
    outPreview.current.text=cue;
    outPreview.current.text.diagnosticReason="presentation-only-current-pixel-proof";
    outPreview.current.ink=receipt.m_currentInk;
    outPreview.policyGeneration=receipt.m_currentKey.policyGeneration;
    outPreview.continuityGeneration=receipt.m_currentKey.continuityGeneration;
    outPreview.matchingFrames=1;outPreview.available=true;
    outPreview.futureAvailable=false;outPreview.currentLinesConfirmed=false;
    outPreview.presentationOnlyCurrentCueProof=true;
    outPreview.newScanMs=0;outPreview.newScanFrames=0;
    outCue=cue;
    // Seal one adjacency witness only when the typed proof follows the
    // tracker's latest independent full source. The worker seed and immutable
    // comparison snapshots may be older, but they must remain ordered in the
    // same context and describe this exact accepted cue/base.
    const auto& anchor=m_previous.current;
    const bool independentAnchor=HasAcceptedSourceTemplate() && m_result.detected &&
        !m_result.revised && !m_result.confirmationPending && !m_result.workLimit &&
        anchor.analyzed && !anchor.pendingRefresh && !anchor.analysisRefresh && !anchor.cadenceSkipped &&
        !anchor.discontinuity && !anchor.inspectionCandidateUsed && !anchor.glyphLocalTop && !anchor.glyphLocalBottom &&
        (anchor.analysisStride==1 || anchor.analysisStride==2) &&
        anchor.independentBarAnalysis && anchor.barAuthority && !anchor.barTrackingAuthority &&
        !anchor.currentAnchorUsesTrackedEdge && anchor.barEvidence.available &&
        anchor.sharedPicture.AvailableFor(anchor.identity,anchor.width,anchor.height) &&
        anchor.pictureTop==anchor.barEvidence.top && anchor.pictureBottom==anchor.barEvidence.bottom &&
        !anchor.barEvidence.topRevalidatedAtReference && !anchor.barEvidence.topRevalidatedNearby &&
        !anchor.barEvidence.bottomRevalidatedAtReference && !anchor.barEvidence.bottomRevalidatedNearby &&
        !anchor.barEvidence.captionBackedTop && !anchor.barEvidence.captionBackedBottom &&
        anchor.text.detected && !anchor.text.revised &&
        !anchor.text.confirmationPending && !anchor.text.workLimit &&
        anchor.text.topSearchComplete && anchor.text.bottomSearchComplete &&
        anchor.ink && anchor.ink->rawEvidenceComplete && anchor.ink->paletteValid &&
        m_reference.ink && m_inkReference.ink &&
        SubtitleBoxLookahead::SameContext(m_inkReference,anchor) &&
        SubtitleBoxLookahead::SameCue(m_inkReference,anchor) &&
        SubtitleBoxPresentation::SameAcceptedCueLayout(m_result,receipt.m_acceptedCue) &&
        SubtitleBoxPresentation::SameAcceptedCueLayout(anchor.text,m_result,false) &&
        SubtitleBoxPresentation::SameAcceptedCueLayout(m_reference.text,m_result,false) &&
        SubtitleBoxPresentation::SameAcceptedCueLayout(m_inkReference.text,m_result,false) &&
        SubtitleBoxPresentation::SameTransportContext(anchor.identity,receipt.m_seedIdentity) &&
        SubtitleBoxPresentation::SameTransportContext(anchor.identity,receipt.m_currentKey.identity) &&
        SubtitleBoxPresentation::NotAfterTransport(receipt.m_seedIdentity,anchor.identity) &&
        SubtitleBoxPresentation::NotAfterTransport(m_reference.identity,anchor.identity) &&
        SubtitleBoxPresentation::NotAfterTransport(m_inkReference.identity,anchor.identity) &&
        m_previous.policyGeneration==receipt.m_currentKey.policyGeneration &&
        m_reference.policyGeneration==receipt.m_currentKey.policyGeneration &&
        m_inkReference.policyGeneration==receipt.m_currentKey.policyGeneration &&
        m_previous.continuityGeneration==receipt.m_currentKey.continuityGeneration &&
        m_reference.continuityGeneration==receipt.m_currentKey.continuityGeneration &&
        m_inkReference.continuityGeneration==receipt.m_currentKey.continuityGeneration &&
        m_previous.current.width==receipt.m_currentKey.width &&
        m_previous.current.height==receipt.m_currentKey.height &&
        m_reference.width==receipt.m_currentKey.width && m_reference.height==receipt.m_currentKey.height &&
        m_inkReference.width==receipt.m_currentKey.width && m_inkReference.height==receipt.m_currentKey.height &&
        m_previous.current.nearBarDistance==receipt.m_currentKey.nearBarDistance &&
        m_reference.nearBarDistance==receipt.m_currentKey.nearBarDistance &&
        m_inkReference.nearBarDistance==receipt.m_currentKey.nearBarDistance &&
        m_previous.current.optimizationMode==receipt.m_currentKey.optimizationMode &&
        m_reference.optimizationMode==receipt.m_currentKey.optimizationMode &&
        m_inkReference.optimizationMode==receipt.m_currentKey.optimizationMode &&
        m_previous.current.scriptHint==receipt.m_currentKey.scriptHint &&
        m_reference.scriptHint==receipt.m_currentKey.scriptHint &&
        m_inkReference.scriptHint==receipt.m_currentKey.scriptHint &&
        SubtitleBoxPresentation::SamePhysicalBase(m_previous.current.sharedPicture.bounds,receipt.m_physicalBase) &&
        SubtitleBoxPresentation::SamePhysicalBase(m_reference.sharedPicture.bounds,receipt.m_physicalBase) &&
        SubtitleBoxPresentation::SamePhysicalBase(m_inkReference.sharedPicture.bounds,receipt.m_physicalBase);
    auto& witness=m_currentCueProofComparison;
    if(!witness.valid && independentAnchor &&
        SubtitleBoxPresentation::ImmediatelyFollowsTransport(anchor.identity,receipt.m_currentKey.identity)) {
        witness.valid=true;witness.templateRevision=receipt.m_templateRevision;
        witness.seedIdentity=receipt.m_seedIdentity;witness.anchorIdentity=anchor.identity;
        witness.proofIdentity=receipt.m_currentKey.identity;
        witness.policy=receipt.m_currentKey.policyGeneration;
        witness.continuity=receipt.m_currentKey.continuityGeneration;
        witness.cue=m_result.cue;witness.lineCount=m_result.lineCount;
        witness.width=receipt.m_currentKey.width;witness.height=receipt.m_currentKey.height;
        witness.nearBarDistance=receipt.m_currentKey.nearBarDistance;
        witness.optimizationMode=receipt.m_currentKey.optimizationMode;
        witness.base=receipt.m_physicalBase;
    } else if(witness.valid) {
        // A chain of exact, one-use current-pixel proofs may bridge the gap
        // until the next full scan. Keep the original independent anchor and
        // seed fixed; only advance the latest verified source identity.
        const bool continuesSameWitness=independentAnchor &&
            receipt.m_templateRevision==witness.templateRevision &&
            SameAnalysisFrameIdentity(receipt.m_seedIdentity,witness.seedIdentity) &&
            SameAnalysisFrameIdentity(anchor.identity,witness.anchorIdentity) &&
            SubtitleBoxPresentation::ImmediatelyFollowsTransport(witness.proofIdentity,
                receipt.m_currentKey.identity) &&
            witness.policy==receipt.m_currentKey.policyGeneration &&
            witness.continuity==receipt.m_currentKey.continuityGeneration &&
            witness.cue==m_result.cue && witness.lineCount==m_result.lineCount &&
            witness.width==receipt.m_currentKey.width && witness.height==receipt.m_currentKey.height &&
            witness.nearBarDistance==receipt.m_currentKey.nearBarDistance &&
            witness.optimizationMode==receipt.m_currentKey.optimizationMode &&
            SubtitleBoxPresentation::SamePhysicalBase(witness.base,receipt.m_physicalBase);
        if(continuesSameWitness)witness.proofIdentity=receipt.m_currentKey.identity;
        else witness={};
    }
    // This is an output adapter only. The exact-source receipt is one-use, but
    // it must not replace, clear, or renew the independent tracker history.
    // The renderer bypasses semantic Consume for this presentation-only view.
    return true;
}

using SubtitleMeasurementWork = std::function<SubtitleBoxObservation(
    SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&)>;

struct SubtitleCurrentCueProofRequest {
    SubtitleBoxObservation currentKey;
    // The optional semantic scan keeps its original inspection/native key;
    // presentation-only normalization must never become detection authority.
    SubtitleBoxObservation fullScanKey;
    bool hasFullScanKey=false;
    AnalysisLumaSource source;
    ActivePictureBounds physicalBase;
    SubtitlePreviewMode sourceMode=SubtitlePreviewMode::None;
    int backgroundMode=3;
    ActivePictureFrameIdentity eligibleRenderHead;
    uint64_t eligibleTailSequence=0;
    uint64_t templateRevision=0;
    double frameMs=1000.0/60.0;
    bool fullScanDue=false;
    SubtitleMeasurementWork fullScanOnMiss;
    OwnedScanBacklogWitness ownedWitness;
    bool hasOwnedWitness=false;
    uint64_t ownedTimelinePolicy=0;
    std::shared_ptr<AnalysisCompletionSignal> completion;
    std::shared_ptr<SubtitleCurrentCueProofSlot> completedProof;
};

struct SubtitleCurrentCueTemplateDescriptor;
struct SubtitleCurrentCueWorkerState;
struct SubtitleCurrentCueTaskEntry;
struct SubtitleCurrentCueRefreshLease;
// Small immutable publications of the EXISTING cue state and source window.
// Readers never take the worker job mutex or copy the cue descriptor.
struct SubtitleCurrentCueSourceWindow { ActivePictureFrameIdentity head; uint64_t tail=0; };
struct SubtitleCurrentCueReadView {
    std::shared_ptr<SubtitleCurrentCueWorkerState> state;
    std::shared_ptr<const SubtitleCurrentCueTemplateDescriptor> descriptor;
    uint64_t revision=0;
};

enum class SubtitleCurrentCueFullScanRoute : uint8_t {
    Enqueue, Suppress, Deferred
};

// Bounded CPU-only work. Jobs own their source storage. Current-source preparation
// may await completion within its enqueue-derived deadline; presentation never
// consumes a result for another source frame or policy epoch.
class SubtitleMeasurementWorker {
public:
    SubtitleMeasurementWorker() {
        // Initialize the existing bounded worker pair before source presentation,
        // rather than creating threads during an optional render/capture offer.
        try {for(size_t i=0;i<m_threads.size();++i)m_threads[i]=std::thread([this,i]{Run(i);});}
        catch(...) {Stop();throw;}
    }
    struct Diagnostics {
        uint64_t fullScans=0,refreshes=0,failed=0,dropped=0,requestMisses=0;
        uint64_t currentCueTasks=0;
        uint64_t submitted=0,exactReads=0,deduplicated=0,expired=0,staleCompleted=0;
        double fullMs=0,fullPeakMs=0,refreshMs=0,refreshPeakMs=0,queueWaitMs=0,queueWaitPeakMs=0;
        double currentCueMs=0,currentCuePeakMs=0;
        size_t pending=0,ready=0,workerLimit=1,activeWorkers=0;bool active=false,stopping=false;
    };
    bool TryTakeDiagnostics(Diagnostics& out) {
        std::unique_lock<std::mutex> lock(m_mutex,std::try_to_lock);
        if(!lock.owns_lock())return false;
        out=m_diagnostics;out.pending=m_pending.size()+m_currentCuePending.size();out.ready=m_ready.size();out.workerLimit=m_workerLimit;
        for(const auto& active:m_activeJobs)if(active.active)++out.activeWorkers;
        out.active=out.activeWorkers!=0;out.stopping=m_stopping;m_diagnostics={};return true;
    }
    Diagnostics TakeDiagnostics() {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto out=m_diagnostics;out.pending=m_pending.size()+m_currentCuePending.size();out.ready=m_ready.size();out.workerLimit=m_workerLimit;
        for(const auto& active:m_activeJobs)if(active.active)++out.activeWorkers;
        out.active=out.activeWorkers!=0;out.stopping=m_stopping;
        m_diagnostics={};return out;
    }
    void Trace(const SubtitleBoxObservation& key,const char* stage,const char* reason="",uint64_t mismatch=0) const noexcept {
        auto r=AnalysisTraceRecord(key.identity,this,stage,reason);r.kind=AnalysisDiagnosticRecordKindV1::Worker;
        r.policyGeneration=key.policyGeneration;r.continuityGeneration=key.continuityGeneration;
        r.nearBarDistance=key.nearBarDistance;r.optimizationMode=key.optimizationMode;r.scriptHint=static_cast<uint32_t>(key.scriptHint);
        r.sourceWidth=key.width;r.sourceHeight=key.height;r.discontinuity=key.discontinuity;
        r.flags=(key.analyzed?1u:0u)|(key.text.detected?2u:0u)|(key.sharedPicture.required?4u:0u)|
            (key.sharedPicture.AvailableFor(key.identity,key.width,key.height)?8u:0u)|(key.pictureAuthorityUnavailableForMeasurement?16u:0u)|
            (key.sharedPicture.inspectionCandidate.AvailableFor(key.identity,key.width,key.height,key.policyGeneration,key.continuityGeneration)?32u:0u)|
            (key.inspectionCandidateUsed?64u:0u);
        r.counters[0]=mismatch;r.counters[1]=key.sharedPicture.identity.acceptedSequence;
        r.counters[2]=key.sharedPicture.inspectionCandidate.originIdentity.acceptedSequence;
        r.counters[3]=key.sharedPicture.inspectionCandidate.nativeGeometryRevision;
        AnalysisTraceRect(r.candidate,key.sharedPicture.inspectionCandidate.bounds);
        r.resultCode=key.text.lineCount;AnalysisTraceRect(r.subtitle,key.text.bounds);
        r.metrics[0]=key.analysisMs;r.metrics[1]=key.pictureTop;r.metrics[2]=key.pictureBottom;
        AnalysisDiagnosticTrace::Emit(r);
    }
    using Work=SubtitleMeasurementWork;
    // Seed only after the renderer has accepted and composed this exact full
    // current cue. Classic may seed an accepted scan without cut/paste; the
    // advanced modes require a successful operation.
    SubtitleCurrentCueProofStatus SubmitCurrentCueTemplate(
        const SubtitleCurrentCueTemplateInput& input,uint64_t& templateRevision);
    // Render callers transfer the large input without creating a by-value
    // temporary on the 1 MiB presentation thread stack.
    __declspec(noinline) SubtitleCurrentCueProofStatus SubmitCurrentCueTemplateOwned(
        std::unique_ptr<SubtitleCurrentCueTemplateInput> input,uint64_t& templateRevision,bool nonblocking=false);
    // Proof is attempted before grouping. On a miss/unknown, the captured
    // full scan is handed to the ordinary bounded queue only when due.
    SubtitleCurrentCueProofStatus SubmitCurrentCueProof(SubtitleCurrentCueProofRequest request,bool nonblocking=false);
    // Exact-key, one-use, nonblocking receipt poll.
    SubtitleCurrentCueProofStatus TryTakeCurrentCueProof(
        const SubtitleBoxObservation& exactKey,uint64_t templateRevision,
        SubtitleCurrentCueProofReceipt& receipt);
    // Withdraw presentation context without freeing worker-owned pixels or
    // waiting for pending work. Ordinary compatible cue rollover does not call this.
    void InvalidateCurrentCueProofContext();
    // Presentation polls are non-destructive. The tracker commits the shared
    // one-use token only after current operation/context validation succeeds.
    SubtitleCurrentCueProofStatus TryPeekCurrentCueProof(
        const SubtitleBoxObservation& exactKey,SubtitleCurrentCueProofReceipt& receipt,bool nonblocking=false);
    SubtitleCurrentCueProofStatus TryReadCompletedCurrentCueProof(
        const SubtitleBoxObservation& exactKey,const SubtitleCurrentCueProofSlot& slot,
        SubtitleCurrentCueProofReceipt& receipt);
    bool AssociateQualifiedSource(const SourceAnalysisTicket&,const SourcePictureEquivalenceToken&,
        const SubtitleCurrentCueProofReceipt&,const QualifiedSubtitlePresentationEvidence&,
        const SubtitleCutPasteGeometry& actualOperation,bool temporalDonorPossible);
    bool TryReadCurrentEquivalentCue(const SourceAnalysisTicket&,const SourcePictureEquivalenceToken&,
        const SubtitleBoxObservation& key,const ActivePictureBounds& base,SubtitlePreviewMode mode,
        int background,uint64_t revision,const CurrentSourceSupportEvidence* currentSupport,
        bool temporalDonorPossible,SubtitleCurrentCueProofReceipt& out);
    bool MakeCurrentCueProofKey(const SubtitleBoxObservation& sourceKey,
        SubtitlePreviewMode sourceMode,int backgroundMode,SubtitleBoxObservation& proofKey,
        ActivePictureBounds& physicalBase,uint64_t& templateRevision);
    // The proof source window follows renderable queued frames, not capture's
    // newest sequence. Tail is bounded by the caller's existing queue window.
    void AdvanceCurrentCueRenderWindow(const ActivePictureFrameIdentity& head,
        uint64_t tailSequence,bool advanceHead=true);
    // Input cadence owns analysis capacity. Display refresh/repeated outputs
    // cannot add workers or change source observations. A lower cadence parks
    // worker1 after its current independent job, without interrupting pixels.
    void SetInputRateHz(double inputHz,bool nonblocking=false) {
        {std::unique_lock<std::mutex> lock(m_mutex,std::defer_lock);
            if(nonblocking) {if(!lock.try_lock())return;}else lock.lock();
            if(m_stopping)return;
            m_workerLimit=(std::isfinite(inputHz) && inputHz>30.0) || HasCurrentCueProofContextLocked()?2u:1u;
            if(m_workerLimit>1 && !m_threads[1].joinable())m_threads[1]=std::thread([this]{Run(1);});
        }
        m_changed.notify_all();
    }
    ~SubtitleMeasurementWorker() { Stop(); }
    static bool SameKey(const SubtitleBoxObservation& a,const SubtitleBoxObservation& b) {
        if((a.sharedPicture.inspectionCandidate.valid &&
                !a.sharedPicture.inspectionCandidate.AvailableFor(a.identity,a.width,a.height,a.policyGeneration,a.continuityGeneration)) ||
            (b.sharedPicture.inspectionCandidate.valid &&
                !b.sharedPicture.inspectionCandidate.AvailableFor(b.identity,b.width,b.height,b.policyGeneration,b.continuityGeneration)))
            return false;
        const bool samePicture=a.sharedPicture.Matches(b.sharedPicture);
        // A worker request with no usable shared snapshot follows the same
        // exact-frame raw-bar path regardless of which unavailable snapshot
        // was published. This lets active/pending work coalesce safely; a
        // newly available authority still requires a separate scan.
        const bool sameUnavailablePictureScan=
            !a.sharedPicture.inspectionCandidate.valid && !b.sharedPicture.inspectionCandidate.valid &&
            a.pictureAuthorityUnavailableForMeasurement &&
            b.pictureAuthorityUnavailableForMeasurement &&
            a.sharedPicture.required && b.sharedPicture.required &&
            !a.sharedPicture.AvailableFor(a.identity,a.width,a.height) &&
            !b.sharedPicture.AvailableFor(b.identity,b.width,b.height);
        return SameAnalysisFrameIdentity(a.identity,b.identity) &&
            a.analysisStride==b.analysisStride &&
            a.width==b.width && a.height==b.height && a.nearBarDistance==b.nearBarDistance && a.optimizationMode==b.optimizationMode && a.scriptHint==b.scriptHint &&
            a.policyGeneration==b.policyGeneration &&
            (a.continuityGeneration==b.continuityGeneration ||
                (!a.pictureAuthorityUnavailableForMeasurement &&
                    !b.pictureAuthorityUnavailableForMeasurement &&
                    !a.independentBarAnalysis && !b.independentBarAnalysis &&
                    a.sharedPicture.required && b.sharedPicture.required)) &&
            a.discontinuity==b.discontinuity && (samePicture || sameUnavailablePictureScan);
    }
    // WORK ownership is weaker than READY admission: an already-running raw
    // full-source scan need not run twice merely because native geometry became
    // available. Keep its original key/closure and validate the completed result
    // through the strict completed-result read/rebind path. An incompatible completed
    // result cannot suppress a later canonical request.
    static bool SamePendingFullSourceWork(const SubtitleBoxObservation& existing,
        const SubtitleBoxObservation& incoming) {
        const auto ordinary=[](const SubtitleBoxObservation& key) {
            return !key.analyzed && !key.analysisRefresh && !key.pendingRefresh &&
                !key.cadenceSkipped && !key.discontinuity && !key.inspectionCandidateUsed &&
                !key.sharedPicture.inspectionCandidate.valid && !key.glyphLocalTop && !key.glyphLocalBottom &&
                !key.barTrackingAuthority && !key.currentAnchorUsesTrackedEdge &&
                !key.hasBarTrackingReference && !key.incomingBarTrackingReference.valid &&
                !key.trackedBottomHypothesisAttempted;
        };
        return ordinary(existing) && ordinary(incoming) &&
            existing.identity.acceptedSequence && existing.identity.analysisDomainKey &&
            SameAnalysisFrameIdentity(existing.identity,incoming.identity) &&
            existing.width>0 && existing.height>0 && existing.width==incoming.width && existing.height==incoming.height &&
            existing.analysisStride==1 && incoming.analysisStride==1 &&
            existing.nearBarDistance==incoming.nearBarDistance && existing.optimizationMode==incoming.optimizationMode &&
            existing.scriptHint==incoming.scriptHint && existing.policyGeneration==incoming.policyGeneration &&
            existing.continuityGeneration==incoming.continuityGeneration &&
            existing.pictureAuthorityUnavailableForMeasurement && existing.sharedPicture.required &&
            !existing.sharedPicture.AvailableFor(existing.identity,existing.width,existing.height) &&
            incoming.sharedPicture.required &&
            incoming.sharedPicture.AvailableFor(incoming.identity,incoming.width,incoming.height);
    }
    // The head has advanced; queued scans for older transport frames cannot
    // become current. The active worker is left alone and remains nonblocking.
    void BeginSourceWindow(const ActivePictureFrameIdentity& head,bool nonblocking=false) {
        std::vector<Job> retiredJobs;
        std::unique_lock<std::mutex> lock(m_mutex,std::defer_lock);
        if(nonblocking) {if(!lock.try_lock())return;}else lock.lock();
        if(m_windowHead.transportGeneration>head.transportGeneration ||
            (m_windowHead.transportGeneration==head.transportGeneration &&
                m_windowHead.acceptedSequence>=head.acceptedSequence))return;
        m_windowHead=head;
        PruneRawHistory();
        for(auto i=m_pending.begin();i!=m_pending.end();) {
            const auto& identity=i->key.identity;
            if(identity.transportGeneration<head.transportGeneration ||
                (identity.transportGeneration==head.transportGeneration &&
                    identity.acceptedSequence<head.acceptedSequence)) {
                Trace(i->key,"worker-drop","source-window-expired");
                retiredJobs.push_back(std::move(*i));
                i=m_pending.erase(i);
                ++m_diagnostics.expired;
            } else ++i;
        }
    }
    // Lookahead and presentation can inspect the same exact-frame result.
    // Rebinding modifies only this copy; the cached observation is immutable.
    bool TryRead(const SubtitleBoxObservation& key,SubtitleBoxObservation& out,
        bool backlogRetirementOnly=false,bool nonblocking=false) {
        return ReadReady(key,out,false,backlogRetirementOnly,nullptr,0,nonblocking);
    }
    // Observation-only query used by the queue owner to request an exact
    // timeline witness. It grants no reuse and never changes cached evidence.
    static bool CompleteIndependentScanForKey(const SubtitleBoxObservation& scan,const SubtitleBoxObservation& key,
        bool requireDetected=true) {
        const auto& b=scan.barEvidence;
        return SameAnalysisFrameIdentity(scan.identity,key.identity) && scan.policyGeneration==key.policyGeneration &&
            scan.width==key.width && scan.height==key.height && scan.nearBarDistance==key.nearBarDistance &&
            scan.optimizationMode==key.optimizationMode && scan.scriptHint==key.scriptHint &&
            scan.analysisStride==1 && key.analysisStride==1 && !scan.discontinuity && !key.discontinuity &&
            scan.analyzed && scan.independentBarAnalysis && scan.barAuthority && b.available &&
            !scan.pendingRefresh && !scan.analysisRefresh && !scan.barTrackingAuthority &&
            !scan.currentAnchorUsesTrackedEdge && !scan.glyphLocalTop && !scan.glyphLocalBottom &&
            !scan.inspectionCandidateUsed && !b.topRevalidatedAtReference && !b.topRevalidatedNearby &&
            !b.bottomRevalidatedAtReference && !b.bottomRevalidatedNearby && !b.captionBackedTop && !b.captionBackedBottom &&
            scan.pictureTop==b.top && scan.pictureBottom==b.bottom &&
            (!requireDetected || scan.text.detected) && !scan.text.held && !scan.text.workLimit && !scan.text.confirmationPending &&
            scan.text.topSearchComplete && scan.text.bottomSearchComplete && scan.ink &&
            scan.ink->rawEvidenceComplete && scan.ink->paletteValid;
    }
    bool PeekIndependentContinuity(const SubtitleBoxObservation& key,uint64_t& continuity,bool nonblocking=false) {
        continuity=0;std::unique_lock<std::mutex> lock(m_mutex,std::defer_lock);
        if(nonblocking) {if(!lock.try_lock())return false;}else lock.lock();
        for(auto i=m_ready.rbegin();i!=m_ready.rend();++i)
            if(CompleteIndependentScanForKey(*i,key)) {continuity=i->continuityGeneration;return true;}
        return false;
    }
    bool TryReadOwnedScan(const SubtitleBoxObservation& key,SubtitleBoxObservation& out,
        const OwnedScanBacklogWitness& witness,uint64_t timelinePolicy,bool nonblocking=false) {
        return ReadReady(key,out,false,false,&witness,timelinePolicy,nonblocking);
    }
    bool TryTake(const SubtitleBoxObservation& key,SubtitleBoxObservation& out) {
        return ReadReady(key,out,true,false);
    }
    // Historical RAW observations are separate from current READY admission.
    // A bound empty snapshot withdraws prior history instead of reusing it.
    std::unique_ptr<SubtitleMeasuredRawPredecessors> ReadMeasuredRawPredecessors(
        const SubtitleBoxObservation& currentKey) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto snapshot=std::make_unique<SubtitleMeasuredRawPredecessors>();
        std::vector<SubtitleBoxObservation> frames;
        PruneRawHistory();
        if(RawResultEligible(currentKey,currentKey) && RawVersionOwns(currentKey)) {
            const SubtitleBoxObservation* next=&currentKey;size_t exportedBytes=0;
            for(uint64_t offset=1;offset<=3 && currentKey.identity.acceptedSequence>=offset;++offset) {
                const uint64_t wanted=currentKey.identity.acceptedSequence-offset;
                std::unique_ptr<SubtitleBoxObservation> found;
                const auto inspect=[&](const SubtitleBoxObservation& candidate) {
                    const size_t bytes=RawInkBytes(candidate);
                    if(candidate.identity.acceptedSequence!=wanted || !RawVersionOwns(candidate) ||
                        !RawResultEligible(candidate,candidate) || bytes>16u*1024u*1024u ||
                        exportedBytes+bytes>48u*1024u*1024u)return;
                    auto copy=std::make_unique<SubtitleBoxObservation>(candidate);
                    if(!copy->sharedPicture.Matches(currentKey.sharedPicture)) {
                        // Independent RAW pixels may have completed before the
                        // canonical plane existed. Rebind only the COPY, using
                        // the existing strict path and identical physical bars.
                        if(copy->pictureTop!=currentKey.pictureTop || copy->pictureBottom!=currentKey.pictureBottom ||
                            !SubtitleBoxLookahead::RebindIndependentPicture(*copy,currentKey.sharedPicture))return;
                    }
                    if(SubtitleBoxLookahead::ConsecutiveCrowdedRawContext(*copy,*next))found=std::move(copy);
                };
                for(const auto& candidate:m_ready)inspect(candidate);
                for(const auto& candidate:m_pastRaw)inspect(candidate);
                if(!found)break;
                exportedBytes+=RawInkBytes(*found);frames.push_back(std::move(*found));next=&frames.back();
            }
            std::reverse(frames.begin(),frames.end());
            for(auto& frame:frames) {frame.subtitleArProof={};frame.subtitleArDarkness={};}
        }
        snapshot->Record(currentKey,std::move(frames));
        return snapshot;
    }
    // Metadata-only query; the queue owner must independently qualify the
    // returned old continuity with an exact-source timeline witness.
    static bool RequiredFullSourceWork(const SubtitleBoxObservation& key) {
        return key.sharedPicture.required && key.width>0 && key.height>0 && key.analysisStride==1 && !key.analyzed &&
            !key.discontinuity && !key.pendingRefresh && !key.analysisRefresh && !key.cadenceSkipped &&
            !key.sharedPicture.inspectionCandidate.valid && !key.inspectionCandidateUsed &&
            !key.glyphLocalTop && !key.glyphLocalBottom && !key.barTrackingAuthority &&
            !key.currentAnchorUsesTrackedEdge && !key.trackedBottomHypothesisAttempted;
    }
    static bool SameOwnedWorkSource(const SubtitleBoxObservation& existing,const SubtitleBoxObservation& incoming) {
        return RequiredFullSourceWork(existing) && RequiredFullSourceWork(incoming) && existing.continuityGeneration &&
            existing.continuityGeneration<incoming.continuityGeneration &&
            existing.identity.acceptedSequence && existing.identity.analysisDomainKey &&
            SameAnalysisFrameIdentity(existing.identity,incoming.identity) &&
            RawSettingsMatch(existing,incoming,false);
    }
    bool PeekOwnedWorkContinuity(const SubtitleBoxObservation& incoming,uint64_t& oldContinuity,bool nonblocking=false) {
        oldContinuity=0;std::unique_lock<std::mutex> lock(m_mutex,std::defer_lock);
        if(nonblocking) {if(!lock.try_lock())return false;}else lock.lock();
        if(m_stopping)return false;
        const auto inspect=[&](const SubtitleBoxObservation& key) {
            if(SameOwnedWorkSource(key,incoming))oldContinuity=(std::max)(oldContinuity,key.continuityGeneration);
        };
        for(const auto& active:m_activeJobs)if(active.active && !active.currentCueTask)inspect(active.key);
        for(const auto& job:m_pending)inspect(job.key);
        return oldContinuity!=0;
    }
    static bool CanCoalesceOwnedWork(const SubtitleBoxObservation& existing,const SubtitleBoxObservation& incoming,
        const OwnedScanBacklogWitness* witness,uint64_t timelinePolicy) {
        return witness && SameOwnedWorkSource(existing,incoming) &&
            witness->Matches(incoming.identity,timelinePolicy,existing.continuityGeneration,incoming.continuityGeneration);
    }
    static bool CanCoalesceOwnedCompleted(const SubtitleBoxObservation& original,const SubtitleBoxObservation& incoming,
        const OwnedScanBacklogWitness* witness,uint64_t timelinePolicy) {
        if(!witness || !RequiredFullSourceWork(incoming) || !original.sharedPicture.required ||
            original.sharedPicture.inspectionCandidate.valid || original.continuityGeneration>=incoming.continuityGeneration ||
            !CompleteIndependentScanForKey(original,incoming) || !original.text.bounds.Valid() || original.text.lineCount<=0 ||
            !witness->Matches(incoming.identity,timelinePolicy,original.continuityGeneration,incoming.continuityGeneration))return false;
        // A completion may race the queue owner's peek/witness round trip.
        // Coalesce only when the unchanged READY path can actually return this
        // original source under the same typed witness. Never alter the cache.
        auto copy=std::make_unique<SubtitleBoxObservation>(original);
        return SubtitleBoxLookahead::RebindIndependentPicture(*copy,incoming.sharedPicture) &&
            SubtitleBoxLookahead::RebindCompletedIndependentScanAfterBacklog(*copy,incoming.continuityGeneration,true) &&
            SameKey(*copy,incoming);
    }
    bool Submit(const SubtitleBoxObservation& key,Work work,
        const OwnedScanBacklogWitness* ownedWitness=nullptr,uint64_t timelinePolicy=0,bool nonblocking=false,
        std::shared_ptr<AnalysisCompletionSignal> completion={}) {
        std::shared_ptr<SubtitleCurrentCueRefreshLease> refreshLease;
        const auto route=RouteCurrentCueFullScan(key,work,ownedWitness,timelinePolicy,nonblocking,refreshLease);
        if(route==SubtitleCurrentCueFullScanRoute::Suppress)return false;
        if(route==SubtitleCurrentCueFullScanRoute::Deferred)return true;
        return SubmitCore(key,std::move(work),ownedWitness,timelinePolicy,nonblocking,
            std::move(completion),std::move(refreshLease));
    }
private:
    // Proof-selected fallback/refresh scans bypass the ordinary admission
    // throttle only through this private enqueue path.
    bool SubmitCore(const SubtitleBoxObservation& key,Work work,
        const OwnedScanBacklogWitness* ownedWitness,uint64_t timelinePolicy,bool nonblocking,
        std::shared_ptr<AnalysisCompletionSignal> completion,
        std::shared_ptr<SubtitleCurrentCueRefreshLease> refreshLease={}) {
        // Retired callbacks may own a prepared frame/COM source. Declare this
        // before the lock so its last release happens after the lock is gone.
        std::vector<Job> retiredJobs;
        auto requestKey=key;
        // QueueSubtitleMeasurement submits only valid prepared sources. Mark
        // the request's scan path here so active and pending jobs can coalesce
        // before Measure has produced an observation.
        requestKey.pictureAuthorityUnavailableForMeasurement=
            requestKey.sharedPicture.required &&
            !requestKey.sharedPicture.AvailableFor(
                requestKey.identity,requestKey.width,requestKey.height);
        std::unique_lock<std::mutex> lock(m_mutex,std::defer_lock);
        if(nonblocking) {if(!lock.try_lock())return false;}else lock.lock();
        if(m_stopping)return false;
        if(requestKey.identity.transportGeneration<m_windowHead.transportGeneration ||
            (requestKey.identity.transportGeneration==m_windowHead.transportGeneration &&
                requestKey.identity.acceptedSequence<m_windowHead.acceptedSequence)) {
            Trace(requestKey,"worker-drop","source-window-expired");
            ++m_diagnostics.expired;return false;
        }
        for(const auto& active:m_activeJobs)
            if(active.active && !active.currentCueTask &&
                (SameKey(active.key,requestKey) || SamePendingFullSourceWork(active.key,requestKey) ||
                CanCoalesceOwnedWork(active.key,requestKey,ownedWitness,timelinePolicy))) {
                Trace(requestKey,"worker-dedup","active");++m_diagnostics.deduplicated;return false;
            }
        for(const auto& job:m_pending)if(SameKey(job.key,requestKey) || SamePendingFullSourceWork(job.key,requestKey) ||
            CanCoalesceOwnedWork(job.key,requestKey,ownedWitness,timelinePolicy)){Trace(requestKey,"worker-dedup","queued");++m_diagnostics.deduplicated;return false;}
        for(const auto& ready:m_ready)if(SameKey(ready,requestKey) ||
            CanCoalesceOwnedCompleted(ready,requestKey,ownedWitness,timelinePolicy)){Trace(requestKey,"worker-dedup","ready");++m_diagnostics.deduplicated;return false;}
        if(m_pending.size()>=8) {
            // Keep the nearest accepted source. A new current-frame request may
            // replace the farthest future; a later future cannot evict the head.
            auto leastUseful=m_pending.begin();
            for(auto i=std::next(m_pending.begin());i!=m_pending.end();++i) {
                const auto& x=i->key.identity;
                const auto& worst=leastUseful->key.identity;
                if(x.transportGeneration<worst.transportGeneration ||
                    (x.transportGeneration==worst.transportGeneration &&
                        x.acceptedSequence>worst.acceptedSequence))
                    leastUseful=i;
            }
            const auto& worst=leastUseful->key.identity;
            const auto& incoming=requestKey.identity;
            if(incoming.transportGeneration>worst.transportGeneration ||
                (incoming.transportGeneration==worst.transportGeneration &&
                    incoming.acceptedSequence<worst.acceptedSequence)) {
                Trace(leastUseful->key,"worker-drop","lower-priority-replaced");
                retiredJobs.push_back(std::move(*leastUseful));
                m_pending.erase(leastUseful);
            } else {
                Trace(requestKey,"worker-drop","future-capacity");
                ++m_diagnostics.dropped;
                return false;
            }
            ++m_diagnostics.dropped;
        }
        if(refreshLease) {
            auto scan=std::move(work);
            work=[scan=std::move(scan),refreshLease](SubtitleMeasurementSampler& sampler,
                const SubtitleBarTrackingReference& previous) mutable {
                (void)refreshLease;
                return scan(sampler,previous);
            };
        }
        Trace(requestKey,"worker-submit");
        // Execute newer transport generations first, then the nearest source
        // sequence. Equal-frame contexts keep submission order.
        const auto place=std::find_if(m_pending.begin(),m_pending.end(),
            [&requestKey](const Job& queued) {
                const auto& incoming=requestKey.identity;
                const auto& existing=queued.key.identity;
                return incoming.transportGeneration>existing.transportGeneration ||
                    (incoming.transportGeneration==existing.transportGeneration &&
                        incoming.acceptedSequence<existing.acceptedSequence);
            });
        const uint64_t rawRevision=RecordAcceptedRawJob(requestKey);
        m_pending.insert(place,{requestKey,std::move(work),rawRevision,std::move(completion)});
        if(refreshLease)CommitCurrentCueRefreshReservation(refreshLease,requestKey.identity.acceptedSequence);
        ++m_diagnostics.submitted;
        for(size_t index=0;index<m_workerLimit;++index)
            if(!m_threads[index].joinable())m_threads[index]=std::thread([this,index]{ Run(index); });
        m_changed.notify_all();
        return true;
    }
public:
    void Stop() {
        std::deque<Job> retiredPending,retiredCuePending;
        std::deque<std::shared_ptr<SubtitleCurrentCueTaskEntry>> retiredCueTasks;
        std::deque<SubtitleBoxObservation> retiredPastRaw;
        std::deque<RawVersion> retiredRawVersions;
        std::shared_ptr<SubtitleCurrentCueWorkerState> retiredCueState;
        {
            std::lock_guard<std::mutex> lock(m_mutex);m_stopping=true;
            retiredPending.swap(m_pending);retiredCuePending.swap(m_currentCuePending);
            retiredCueTasks.swap(m_currentCueTasks);retiredPastRaw.swap(m_pastRaw);
            retiredRawVersions.swap(m_rawVersions);retiredCueState=std::move(m_currentCueState);
            PublishCurrentCueReadViewLocked();
        }
        m_changed.notify_all();
        for(auto& thread:m_threads)if(thread.joinable())thread.join();
    }
private:
    bool HasCurrentCueProofContextLocked() const;
    void PublishCurrentCueReadViewLocked();
    void PublishCurrentCueWindow(const ActivePictureFrameIdentity&,uint64_t,bool);
    bool InCurrentCueWindow(const ActivePictureFrameIdentity&) const;
    void PublishCompletedCurrentCueProof(const std::shared_ptr<SubtitleCurrentCueProofSlot>&,
        const std::shared_ptr<const SubtitleCurrentCueProofPublication>&);
    bool CurrentCueReadViewAccepts(const std::shared_ptr<const SubtitleCurrentCueReadView>&,
        const SubtitleBoxObservation&,uint64_t) const;
    SubtitleCurrentCueFullScanRoute RouteCurrentCueFullScan(const SubtitleBoxObservation& key,
        Work& work,const OwnedScanBacklogWitness* ownedWitness,uint64_t timelinePolicy,bool nonblocking,
        std::shared_ptr<SubtitleCurrentCueRefreshLease>& refreshLease);
    void CommitCurrentCueRefreshReservation(
        const std::shared_ptr<SubtitleCurrentCueRefreshLease>& refreshLease,uint64_t sequence);
    void ObserveCurrentCueSemanticScan(const SubtitleBoxObservation& key,
        const SubtitleBoxObservation& result);
    struct RawVersion {
        SubtitleBoxObservation key;
        uint64_t revision=0;
        bool ambiguous=false,complete=false;
        const SubtitleInkSnapshot* ink=nullptr;
        std::unique_ptr<SubtitleBoxObservation> original;
    };
    static bool RawSettingsMatch(const SubtitleBoxObservation& a,const SubtitleBoxObservation& b,bool sameContinuity=true) {
        const auto& x=a.identity;const auto& y=b.identity;
        return x.transportGeneration==y.transportGeneration && x.sourceFormatGeneration==y.sourceFormatGeneration &&
            x.analysisDomainKey==y.analysisDomainKey && x.viewportGeneration==y.viewportGeneration &&
            x.rendererGeneration==y.rendererGeneration && a.policyGeneration==b.policyGeneration &&
            (!sameContinuity || a.continuityGeneration==b.continuityGeneration) && a.width==b.width && a.height==b.height &&
            a.nearBarDistance==b.nearBarDistance && a.optimizationMode==b.optimizationMode &&
            a.scriptHint==b.scriptHint && a.analysisStride==b.analysisStride;
    }
    static bool SameRawSource(const SubtitleBoxObservation& a,const SubtitleBoxObservation& b) {
        return SameAnalysisFrameIdentity(a.identity,b.identity) && RawSettingsMatch(a,b);
    }
    static bool RawResultEligible(const SubtitleBoxObservation& result,const SubtitleBoxObservation& jobKey) {
        return SameRawSource(result,jobKey) && result.sharedPicture.Matches(jobKey.sharedPicture) &&
            SubtitleBoxLookahead::CompleteConsecutiveCrowdedRaw(result) && CompleteIndependentScanForKey(result,jobKey) &&
            !result.cadenceSkipped;
    }
    static size_t RawInkBytes(const SubtitleBoxObservation& raw) {
        constexpr size_t limit=16u*1024u*1024u;
        if(!raw.ink)return limit+1;
        size_t total=sizeof(raw)+sizeof(SubtitleInkSnapshot);
        const auto add=[&](size_t count,size_t width) {
            if(count>(limit-total)/width)return false;total+=count*width;return true;
        };
        if(!add(raw.ink->sourceRows.capacity(),sizeof(int)) || !add(raw.ink->rawInk.capacity(),sizeof(uint64_t)) ||
            !add(raw.ink->ownedInk.capacity(),sizeof(uint64_t)) || !add(raw.ink->blackBacking.capacity(),sizeof(uint64_t)))return limit+1;
        return total;
    }
    bool RawWindowContext(const ActivePictureFrameIdentity& id) const {
        return m_windowHead.transportGeneration && id.transportGeneration==m_windowHead.transportGeneration &&
            id.sourceFormatGeneration==m_windowHead.sourceFormatGeneration &&
            id.viewportGeneration==m_windowHead.viewportGeneration && id.rendererGeneration==m_windowHead.rendererGeneration &&
            (!m_windowHead.analysisDomainKey || id.analysisDomainKey==m_windowHead.analysisDomainKey);
    }
    void PruneRawHistory() {
        if(!m_windowHead.transportGeneration)return;
        const uint64_t floor=m_windowHead.acceptedSequence>3?m_windowHead.acceptedSequence-3:0;
        for(auto i=m_pastRaw.begin();i!=m_pastRaw.end();) {
            if(!RawWindowContext(i->identity) || i->identity.acceptedSequence<floor ||
                i->identity.acceptedSequence>=m_windowHead.acceptedSequence)i=m_pastRaw.erase(i);else ++i;
        }
        for(auto i=m_rawVersions.begin();i!=m_rawVersions.end();) {
            const auto& id=i->key.identity;
            if(!RawWindowContext(id) || id.acceptedSequence<floor ||
                (id.acceptedSequence>m_windowHead.acceptedSequence &&
                 id.acceptedSequence-m_windowHead.acceptedSequence>ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES+2))
                i=m_rawVersions.erase(i);else ++i;
        }
    }
    uint64_t RecordAcceptedRawJob(const SubtitleBoxObservation& key) {
        PruneRawHistory();
        // A changed current detector epoch cannot inherit older RAW history.
        if(m_windowHead.acceptedSequence==key.identity.acceptedSequence && RawWindowContext(key.identity)) {
            if(m_rawSettingsKnown && !RawSettingsMatch(m_rawSettings,key)) {
                for(auto i=m_pastRaw.begin();i!=m_pastRaw.end();)if(!RawSettingsMatch(*i,key))i=m_pastRaw.erase(i);else ++i;
                for(auto i=m_rawVersions.begin();i!=m_rawVersions.end();)if(!RawSettingsMatch(i->key,key))i=m_rawVersions.erase(i);else ++i;
            }
            m_rawSettings=key;m_rawSettings.text={};m_rawSettings.ink.reset();m_rawSettingsKnown=true;
        }
        const uint64_t revision=++m_nextRawRevision;
        for(auto& v:m_rawVersions)if(SameRawSource(v.key,key)) {
            // A distinct accepted interpretation withdraws prior history. Calls
            // deduplicated above never reach this point and add no version.
            v.revision=revision;v.ambiguous=true;v.complete=false;v.ink=nullptr;v.original.reset();
            for(auto i=m_pastRaw.begin();i!=m_pastRaw.end();)if(SameRawSource(*i,key))i=m_pastRaw.erase(i);else ++i;
            return revision;
        }
        RawVersion version;version.key=key;version.key.text={};version.key.ink.reset();version.revision=revision;
        m_rawVersions.push_back(std::move(version));
        // Metadata only; completed future work cannot evict nearer past sources.
        while(m_rawVersions.size()>32) {
            auto oldest=m_rawVersions.begin();
            for(auto i=m_rawVersions.begin();i!=m_rawVersions.end();++i)
                if(i->key.identity.acceptedSequence<oldest->key.identity.acceptedSequence)oldest=i;
            m_rawVersions.erase(oldest);
        }
        return revision;
    }
    bool RawVersionOwns(const SubtitleBoxObservation& raw) const {
        for(const auto& v:m_rawVersions)if(SameRawSource(v.key,raw)) {
            if(v.ambiguous || !v.complete || v.ink!=raw.ink.get() || !v.original)return false;
            auto original=std::make_unique<SubtitleBoxObservation>(*v.original);original->ink=raw.ink;
            if(!original->sharedPicture.Matches(raw.sharedPicture) &&
                (original->pictureTop!=raw.pictureTop || original->pictureBottom!=raw.pictureBottom ||
                 !SubtitleBoxLookahead::RebindIndependentPicture(*original,raw.sharedPicture)))return false;
            return SubtitleBoxLookahead::SameRepeatedCrowdedRaw(*original,raw);
        }
        return false;
    }
    void RecordCompletedRaw(const SubtitleBoxObservation& key,uint64_t revision,const SubtitleBoxObservation& result) {
        PruneRawHistory();
        RawVersion* version=nullptr;
        for(auto& v:m_rawVersions)if(SameRawSource(v.key,key)) {version=&v;break;}
        if(!version || version->revision!=revision || version->ambiguous)return;
        version->complete=RawResultEligible(result,key);version->ink=version->complete?result.ink.get():nullptr;
        if(version->complete) {
            version->original=std::make_unique<SubtitleBoxObservation>(result);
            version->original->ink.reset(); // Shape/provenance only; do not retain an unbudgeted bitmap owner.
        } else version->original.reset();
        if(!version->complete || !RawWindowContext(result.identity) ||
            result.identity.acceptedSequence>=m_windowHead.acceptedSequence ||
            m_windowHead.acceptedSequence-result.identity.acceptedSequence>3 || RawInkBytes(result)>16u*1024u*1024u)return;
        m_pastRaw.push_back(result);
        std::sort(m_pastRaw.begin(),m_pastRaw.end(),[](const SubtitleBoxObservation& a,const SubtitleBoxObservation& b) {
            return a.identity.acceptedSequence<b.identity.acceptedSequence;
        });
        size_t bytes=0;for(const auto& raw:m_pastRaw)bytes+=RawInkBytes(raw);
        while(m_pastRaw.size()>3 || bytes>48u*1024u*1024u) {bytes-=RawInkBytes(m_pastRaw.front());m_pastRaw.pop_front();}
    }
    bool ReadReady(const SubtitleBoxObservation& key,SubtitleBoxObservation& out,bool consume,bool backlogRetirementOnly,
        const OwnedScanBacklogWitness* ownedWitness=nullptr,uint64_t timelinePolicy=0,bool nonblocking=false) {
        std::unique_lock<std::mutex> lock(m_mutex,std::defer_lock);
        if(nonblocking) {if(!lock.try_lock())return false;}else lock.lock();
        uint64_t mismatches=0;
        for(auto i=m_ready.begin();i!=m_ready.end();++i) {
            auto candidate=*i;
            // A nominated scan is not independent-bar proof. Reuse only its
            // exact completed glyph work when the SAME plane becomes canonical.
            // The cache keeps the original record; only this read copy is rebound.
            SubtitleBoxLookahead::RebindCompletedInspectedPlane(candidate,key.sharedPicture);
            const bool newCanonicalPlane=candidate.pictureAuthorityUnavailableForMeasurement &&
                candidate.sharedPicture.required && key.sharedPicture.required &&
                !candidate.sharedPicture.AvailableFor(candidate.identity,candidate.width,candidate.height) &&
                key.sharedPicture.AvailableFor(key.identity,key.width,key.height) &&
                !candidate.sharedPicture.Matches(key.sharedPicture);
            // Coalescing WORK must not turn an incomplete raw scan into the
            // canonical request's permanent answer. The original same-key
            // observation remains readable as UNKNOWN; a new-plane miss lets
            // the caller submit the canonical job without changing its cache.
            if(newCanonicalPlane && (!CompleteIndependentScanForKey(candidate,key,false) ||
                (candidate.text.detected ? (!candidate.text.bounds.Valid() || candidate.text.lineCount<=0) :
                    candidate.text.lineCount!=0))) {
                mismatches|=1024ull;continue;
            }
            if (!SubtitleBoxLookahead::RebindIndependentPicture(candidate,key.sharedPicture)) {
                mismatches|=16ull;continue;
            }
            const bool ownedBacklog=ownedWitness && CompleteIndependentScanForKey(candidate,key) &&
                ownedWitness->Matches(key.identity,timelinePolicy,candidate.continuityGeneration,key.continuityGeneration);
            bool reboundFromBacklog=false;
            if(candidate.continuityGeneration!=key.continuityGeneration &&
                (backlogRetirementOnly || ownedBacklog)) {
                // Preserve all immutable key fields. Only exact-source, fully
                // independent glyph/bar scans can carry across this drop.
                const bool sameSource=SameAnalysisFrameIdentity(candidate.identity,key.identity) &&
                    candidate.policyGeneration==key.policyGeneration &&
                    candidate.scriptHint==key.scriptHint &&
                    candidate.nearBarDistance==key.nearBarDistance &&
                    candidate.optimizationMode==key.optimizationMode &&
                    candidate.width==key.width && candidate.height==key.height &&
                    candidate.discontinuity==key.discontinuity &&
                    candidate.sharedPicture.Matches(key.sharedPicture);
                reboundFromBacklog=sameSource &&
                    SubtitleBoxLookahead::RebindCompletedIndependentScanAfterBacklog(
                        candidate,key.continuityGeneration,true);
            }
            if (!SameKey(candidate,key)) {
                mismatches|=(!SameActivePictureFrameIdentity(candidate.identity,key.identity)?1ull:0ull)|
                    (candidate.identity.analysisDomainKey!=key.identity.analysisDomainKey?2ull:0ull)|
                    (candidate.policyGeneration!=key.policyGeneration?4ull:0ull)|
                    (candidate.continuityGeneration!=key.continuityGeneration?8ull:0ull)|
                    (!candidate.sharedPicture.Matches(key.sharedPicture)?16ull:0ull)|
                    (candidate.scriptHint!=key.scriptHint?32ull:0ull)|
                    (candidate.discontinuity!=key.discontinuity?64ull:0ull)|
                    (candidate.nearBarDistance!=key.nearBarDistance || candidate.optimizationMode!=key.optimizationMode?128ull:0ull)|
                    (candidate.width!=key.width || candidate.height!=key.height?256ull:0ull);
                continue;
            }
            if(!reboundFromBacklog &&
                !SubtitleBoxLookahead::RebindIndependentMeasurement(candidate,key.continuityGeneration)){mismatches|=512ull;continue;}
            Trace(candidate,consume?"worker-consumed":"worker-read",
                reboundFromBacklog?(ownedBacklog?"owned-source-backlog-scan-rebound":"backlog-current-scan-rebound"):"exact-key-accepted");
            out=std::move(candidate);
            if(consume)m_ready.erase(i);
            else ++m_diagnostics.exactReads;
            return true;
        }
        Trace(key,"worker-miss",m_ready.empty()?"no-ready-results":"no-compatible-result",mismatches);
        ++m_diagnostics.requestMisses;return false;
    }
    struct Job {
        SubtitleBoxObservation key;Work work;uint64_t rawRevision=0;
        std::shared_ptr<AnalysisCompletionSignal> completion;
        std::chrono::steady_clock::time_point queued=std::chrono::steady_clock::now();
        std::function<void(size_t)> currentCueTask;
        bool currentCueSeedTask=false;
        std::shared_ptr<SubtitleCurrentCueTaskEntry> cueEntry;
    };
    void Run(size_t workerIndex) {
        SubtitleMeasurementSampler scanner;
        SubtitleBarTrackingReference previous;
        for(;;) {
            Job job;
            {std::unique_lock<std::mutex> lock(m_mutex);
                m_changed.wait(lock,[this,workerIndex]{
                    if(m_stopping || workerIndex>=m_workerLimit)return m_stopping;
                    if(workerIndex==0)return !m_currentCuePending.empty() ||
                        (!HasCurrentCueProofContextLocked() && !m_pending.empty());
                    return !m_pending.empty();
                });
                if(m_stopping)return;
                if(workerIndex==0 && !m_currentCuePending.empty()) {
                    job=std::move(m_currentCuePending.front());m_currentCuePending.pop_front();
                } else {job=std::move(m_pending.front());m_pending.pop_front();}
                m_activeJobs[workerIndex].active=true;m_activeJobs[workerIndex].currentCueTask=bool(job.currentCueTask);
                m_activeJobs[workerIndex].key=job.key;
            }
            if(job.currentCueTask) {
                const auto workStart=std::chrono::steady_clock::now();
                try {job.currentCueTask(workerIndex);} catch(...) {}
                const auto finished=std::chrono::steady_clock::now();
                const double ms=std::chrono::duration<double,std::milli>(finished-workStart).count();
                const double waited=std::chrono::duration<double,std::milli>(workStart-job.queued).count();
                {std::lock_guard<std::mutex> lock(m_mutex);
                    m_diagnostics.queueWaitMs+=waited;
                    m_diagnostics.queueWaitPeakMs=(std::max)(m_diagnostics.queueWaitPeakMs,waited);
                    ++m_diagnostics.currentCueTasks;m_diagnostics.currentCueMs+=ms;
                    m_diagnostics.currentCuePeakMs=(std::max)(m_diagnostics.currentCuePeakMs,ms);
                    m_activeJobs[workerIndex]={};
                }
                if(job.completion)job.completion->Notify();
                continue;
            }
            if(job.key.discontinuity || !previous.IsImmediatePredecessor(job.key.identity,
                job.key.width,job.key.height,job.key.policyGeneration,job.key.continuityGeneration,job.key.scriptHint)) {
                previous={};scanner.Reset();
            }
            Trace(job.key,"worker-start");
            const auto workStart=std::chrono::steady_clock::now();
            const double waited=std::chrono::duration<double,std::milli>(workStart-job.queued).count();
            SubtitleBoxObservation result=job.key;
            try {result=job.work(scanner,previous);previous=SubtitleBoxLookahead::AdvanceBarTrackingReference(result);} catch(...) {result.analyzed=false;}
            ObserveCurrentCueSemanticScan(job.key,result);
            Trace(result,"worker-complete",result.analyzed?result.text.diagnosticReason:"analysis-failed");
            {std::lock_guard<std::mutex> lock(m_mutex);
                const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-workStart).count();
                m_diagnostics.queueWaitMs+=waited;m_diagnostics.queueWaitPeakMs=(std::max)(m_diagnostics.queueWaitPeakMs,waited);
                if(!result.analyzed)++m_diagnostics.failed;
                else if(result.analysisRefresh){++m_diagnostics.refreshes;m_diagnostics.refreshMs+=ms;m_diagnostics.refreshPeakMs=(std::max)(m_diagnostics.refreshPeakMs,ms);}
                else {++m_diagnostics.fullScans;m_diagnostics.fullMs+=ms;m_diagnostics.fullPeakMs=(std::max)(m_diagnostics.fullPeakMs,ms);}
                m_activeJobs[workerIndex]={};
                if(!m_stopping)RecordCompletedRaw(job.key,job.rawRevision,result);
                const bool stale=m_windowHead.transportGeneration &&
                    (result.identity.transportGeneration<m_windowHead.transportGeneration ||
                     (result.identity.transportGeneration==m_windowHead.transportGeneration &&
                      result.identity.acceptedSequence<m_windowHead.acceptedSequence));
                if(!m_stopping && stale) {
                    ++m_diagnostics.staleCompleted;Trace(result,"worker-drop","completed-source-window-expired");
                } else if(!m_stopping) {
                    if(m_ready.size()>=16)m_ready.pop_front();
                    m_ready.push_back(std::move(result));
                }
            }
            if(job.completion)job.completion->Notify();
        }
    }
    Diagnostics m_diagnostics;
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::array<std::thread,2> m_threads;
    std::deque<Job> m_pending;
    std::deque<Job> m_currentCuePending;
    std::deque<std::shared_ptr<SubtitleCurrentCueTaskEntry>> m_currentCueTasks;
    std::shared_ptr<SubtitleCurrentCueWorkerState> m_currentCueState;
    std::shared_ptr<const SubtitleCurrentCueReadView> m_currentCueReadView;
    std::shared_ptr<const SubtitleCurrentCueSourceWindow> m_currentCueSourceWindow;
    std::atomic_uint64_t m_nextCurrentCueRevision{0};
    std::shared_ptr<std::atomic_uint64_t> m_currentCueMinimumRevision=
        std::make_shared<std::atomic_uint64_t>(1);
    std::deque<SubtitleBoxObservation> m_ready;
    std::deque<SubtitleBoxObservation> m_pastRaw;
    std::deque<RawVersion> m_rawVersions;
    SubtitleBoxObservation m_rawSettings;
    uint64_t m_nextRawRevision=0;bool m_rawSettingsKnown=false;
    ActivePictureFrameIdentity m_windowHead;
    struct ActiveJob { bool active=false,currentCueTask=false;SubtitleBoxObservation key; };
    std::array<ActiveJob,2> m_activeJobs;
    size_t m_workerLimit=1;
    bool m_stopping=false;
};


// A late asynchronous result is not negative subtitle evidence. Bridge only a
// bounded delay, with fresh bar proof and unchanged native pixels in every
// accepted glyph rectangle. No OCR, spatial search, wait or GPU readback here.
class SubtitlePendingMeasurementGuard {
public:
    enum class ReadyRearmRejectCode : uint8_t {
        NotAttempted, Passed, NoEligibleFreshPositive, FootprintDelegated,
        SeedOrMode, SourceLayout, CurrentFlags, BarEvidence, PhysicalBounds,
        PolicyGeneration, ContinuityGeneration, IdentityContext,
        CueIdentity, CueBounds, CuePanelsCapture, CueSignatures, CueLines, CueEdges,
        Sequence, InkPalette, InkGrid, RawMask, OwnedMask, GlyphPixels,
        KnownPixels, BackingPixels
    };
    enum ReadyRearmLayoutMismatch : uint32_t {
        ReadyLayoutCueIdentity=1u<<0, ReadyLayoutBounds=1u<<1,
        ReadyLayoutPanels=1u<<2, ReadyLayoutCapture=1u<<3,
        ReadyLayoutSignatures=1u<<4, ReadyLayoutLines=1u<<5, ReadyLayoutEdges=1u<<6
    };
    static const char* ReadyRearmRejectReasonName(ReadyRearmRejectCode code) {
        switch(code) {
        case ReadyRearmRejectCode::NotAttempted:return "not-attempted";
        case ReadyRearmRejectCode::Passed:return "passed";
        case ReadyRearmRejectCode::NoEligibleFreshPositive:return "no-eligible-fresh-positive";
        case ReadyRearmRejectCode::FootprintDelegated:return "footprint-delegated";
        case ReadyRearmRejectCode::SeedOrMode:return "seed-or-mode";
        case ReadyRearmRejectCode::SourceLayout:return "source-layout";
        case ReadyRearmRejectCode::CurrentFlags:return "current-flags";
        case ReadyRearmRejectCode::BarEvidence:return "bar-evidence";
        case ReadyRearmRejectCode::PhysicalBounds:return "physical-bounds";
        case ReadyRearmRejectCode::PolicyGeneration:return "policy-generation";
        case ReadyRearmRejectCode::ContinuityGeneration:return "continuity-generation";
        case ReadyRearmRejectCode::IdentityContext:return "identity-context";
        case ReadyRearmRejectCode::CueIdentity:return "cue-identity";
        case ReadyRearmRejectCode::CueBounds:return "cue-bounds";
        case ReadyRearmRejectCode::CuePanelsCapture:return "cue-panels-capture";
        case ReadyRearmRejectCode::CueSignatures:return "cue-signatures";
        case ReadyRearmRejectCode::CueLines:return "cue-lines";
        case ReadyRearmRejectCode::CueEdges:return "cue-edges";
        case ReadyRearmRejectCode::Sequence:return "sequence";
        case ReadyRearmRejectCode::InkPalette:return "ink-palette";
        case ReadyRearmRejectCode::InkGrid:return "ink-grid";
        case ReadyRearmRejectCode::RawMask:return "raw-mask";
        case ReadyRearmRejectCode::OwnedMask:return "owned-mask";
        case ReadyRearmRejectCode::GlyphPixels:return "glyph-pixels";
        case ReadyRearmRejectCode::KnownPixels:return "known-pixels";
        case ReadyRearmRejectCode::BackingPixels:return "backing-pixels";
        }
        return "unknown";
    }
    enum ContextMismatch : uint32_t {
        ContextDiscontinuity=1u<<0, ContextSource=1u<<1,
        ContextDimensions=1u<<2, ContextTransport=1u<<3,
        ContextFormat=1u<<4, ContextDomain=1u<<5,
        ContextViewport=1u<<6, ContextRenderer=1u<<7,
        ContextNearBar=1u<<8, ContextScript=1u<<9,
        ContextPolicy=1u<<10, ContextContinuity=1u<<11, ContextOptimization=1u<<12
    };
    enum class CurrentPixelVerificationFailure : uint8_t { None, PixelMismatch };
    struct Diagnostics {
        CurrentPixelVerificationFailure currentPixelFailure=CurrentPixelVerificationFailure::None;
        SubtitleCadenceGuard::PixelMismatchWitness pixelWitness;
        ReadyRearmRejectCode readyRearmRejectCode=ReadyRearmRejectCode::NotAttempted;
        uint32_t readyRearmLayoutMismatchBits=0;
        uint32_t contextMismatch=0;
        size_t glyphSamplesChecked=0;
        size_t currentBandSamplesChecked=0,backingSamplesChecked=0;
        size_t currentFootprintSamplesChecked=0,barVisibilitySamplesChecked=0;
        size_t seedBandSamplesChecked=0,seedGlyphSamplesChecked=0;
        size_t seedKnownSamplesChecked=0,seedBackingSamplesChecked=0;
        size_t readyRearmMaskWordsChecked=0,readyRearmGlyphSamplesChecked=0;
        size_t readyRearmBackingSamplesChecked=0,readyRearmKnownSamplesChecked=0;
        int glyphMismatchX=-1, glyphMismatchY=-1;
        double footprintVerifyMs=0.0,currentPixelValidationMs=0.0,encodedBarVetoMs=0.0;
        bool sharedPictureMatches=false, sharedPictureCurrent=false;
        bool barsAvailable=false, topRevalidated=false, bottomRevalidated=false;
        bool backlogReassociated=false;
        uint64_t lastVerifiedSource=0,tokenSource=0,referenceContinuity=0,currentContinuity=0;
        bool backlogTokenAvailable=false;
        int expectedTop=0, expectedBottom=0, measuredTop=0, measuredBottom=0;
    };
    struct PhaseWallClock {
        double& targetMs;
        std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
        bool active=true;
        explicit PhaseWallClock(double& target):targetMs(target) { targetMs=0.0; }
        void Stop() {
            if(!active)return;
            targetMs=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-started).count();
            active=false;
        }
        ~PhaseWallClock(){Stop();}
    };
    void Reset() { m_reference={};m_samples.clear();m_ownedSamples.clear();m_backingSamples.clear();
        m_knownInkSamples.clear();m_paletteFringe.clear();
        m_currentPixels.clear();m_currentSampled.clear();
        m_contrastSeedPixels.clear();m_contrastSeedDirections.clear();
        m_contrastSeedTopEnd=m_contrastSeedBottomStart=m_contrastSeedPictureTop=m_contrastSeedPictureBottom=0;
        m_contrastSeedWidth=m_contrastSeedHeight=0;
        m_currentInkProofSeeded=false;m_currentFootprintRequired=false;m_sourceLayout={};m_elapsed=0;m_last={};
        m_pixelSeedIdentity={};m_pixelSeedInk.reset();m_diagnostics={};
        m_certifiedSeed={};m_certifiedPolicy=m_certifiedContinuity=0;m_consumedCertificate={}; }
    const char* DecisionReason() const { return m_reason; }
    const Diagnostics& DecisionDiagnostics() const { return m_diagnostics; }
    ActivePictureFrameIdentity LastVerifiedIdentity() const { return m_last; }
    ActivePictureFrameIdentity AcceptedSeedIdentity() const { return m_pixelSeedIdentity; }
    double PendingElapsedMilliseconds() const { return m_elapsed; }
    uint64_t ReferenceContinuity() const { return m_reference.continuityGeneration; }
    // Only accepted extraction may seed the late-worker fallback. A held cue
    // can be more complete than either current grouping or lookahead pruning;
    // never turn those partial measurements into a smaller replacement cue.
    bool RememberAccepted(const SubtitleBoxPreview& preview,const SubtitleBoxResult& accepted,
        const AnalysisLumaSource& source,double frameMs,int holdMs,
        const SubtitleAcceptedCueSeedCertificate* existingCueCertificate=nullptr,
        const SubtitleCurrentPreparedCueCertificate* heldCurrentCue=nullptr,
        const ActivePictureBounds* physicalBase=nullptr,
        const SubtitleCadenceGuard* currentFootprint=nullptr,
        bool presentationTemplateProof=false) {
        m_diagnostics.readyRearmRejectCode=ReadyRearmRejectCode::NotAttempted;
        m_diagnostics.readyRearmLayoutMismatchBits=0;
        m_diagnostics.seedBandSamplesChecked=0;
        m_diagnostics.readyRearmMaskWordsChecked=0;
        m_diagnostics.readyRearmGlyphSamplesChecked=0;
        m_diagnostics.readyRearmBackingSamplesChecked=0;
        m_diagnostics.readyRearmKnownSamplesChecked=0;
        const auto contains=[](const SubtitleBoxRect& outer,const SubtitleBoxRect& inner) {
            return outer.Valid() && inner.Valid() && outer.left<=inner.left &&
                outer.top<=inner.top && outer.right>=inner.right && outer.bottom>=inner.bottom;
        };
        const auto covers=[&](const SubtitleBoxResult& measured) {
            if(!measured.detected || measured.workLimit || measured.lineCount<accepted.lineCount ||
                measured.lineCount>3)return false;
            for(int line=0;line<accepted.lineCount;++line) {
                const auto& glyph=accepted.lineBounds[line];bool found=false;
                for(int other=0;other<measured.lineCount;++other)
                    found|=contains(measured.lineBounds[other],glyph);
                if(!found)return false;
            }
            const auto& oldCard=accepted.capturePanelMeasured?accepted.capturePanel:accepted.sourcePanel;
            const auto& newCard=measured.capturePanelMeasured?measured.capturePanel:measured.sourcePanel;
            return !oldCard.Valid() || contains(newCard,oldCard);
        };
        // A completed independent scan may restore an ALREADY acquired cue
        // after backlog retirement even though Resolve has no future rows yet.
        // Only the tracker's exact current-cue certificate can replace that
        // future grouping vote; raw current extraction must still cover all rows.
        const auto& current=preview.current;
        if(accepted.held) {
            // A held pose cannot become a new seed. Full CURRENT prepared-cue
            // proof may preserve the ORIGINAL acquired seed and its samples,
            // without changing either its source or elapsed pending budget.
            const auto& old=m_reference.current;const auto& a=m_last;const auto& b=current.identity;
            const auto sameRect=[](const SubtitleBoxRect& x,const SubtitleBoxRect& y) {
                return x.left==y.left && x.top==y.top && x.right==y.right && x.bottom==y.bottom;
            };
            const auto sameBase=[](const ActivePictureBounds& x,const ActivePictureBounds& y) {
                return x.left==y.left && x.top==y.top && x.right==y.right && x.bottom==y.bottom &&
                    x.rasterWidth==y.rasterWidth && x.rasterHeight==y.rasterHeight;
            };
            bool sameShape=m_reference.text.detected && m_reference.text.cue==accepted.cue &&
                m_reference.text.lineCount==accepted.lineCount &&
                m_reference.text.capturePanelMeasured==accepted.capturePanelMeasured &&
                sameRect(m_reference.text.bounds,accepted.bounds) &&
                sameRect(m_reference.text.sourcePanel,accepted.sourcePanel) &&
                sameRect(m_reference.text.provenSourcePanel,accepted.provenSourcePanel) &&
                sameRect(m_reference.text.capturePanel,accepted.capturePanel);
            for(int line=0;sameShape && line<accepted.lineCount && line<3;++line)
                sameShape=sameRect(m_reference.text.lineBounds[line],accepted.lineBounds[line]) &&
                    sameRect(m_reference.text.linePanels[line],accepted.linePanels[line]) &&
                    sameRect(m_reference.text.provenLinePanels[line],accepted.provenLinePanels[line]) &&
                    sameRect(m_reference.text.lineCapturePanels[line],accepted.lineCapturePanels[line]);
            // The prepared certificate proves CURRENT glyph ownership, not
            // equality of its raw backing with the old seed's sampled card.
            // A larger/new card must never be represented by the old geometry.
            bool sameBacking=sameShape;
            const int rowTolerance=(std::max)(4,current.height/180);
            for(int line=0;sameBacking && line<m_reference.text.lineCount && line<3;++line) {
                bool matched=false;
                for(int rawLine=0;rawLine<current.text.lineCount && rawLine<3;++rawLine)
                    matched|=SubtitleBoxLookahead::SameCrowdedLine(old.text.lineBounds[line],current.text.lineBounds[rawLine],rowTolerance) &&
                        SubtitleBoxLookahead::SameCrowdedBacking(old,line,current,rawLine,0);
                sameBacking=matched;
            }
            const bool context=preview.available && current.analyzed && !current.pendingRefresh &&
                source.IsValid() && source.width==current.width && source.height==current.height &&
                HasPixelSeed() && sameShape && sameBacking && covers(current.text) &&
                heldCurrentCue && physicalBase && heldCurrentCue->Matches(current,accepted,*physicalBase) &&
                SubtitleBoxLookahead::SameContext(old,current) &&
                old.policyGeneration==current.policyGeneration && old.continuityGeneration==current.continuityGeneration &&
                preview.policyGeneration==current.policyGeneration && preview.continuityGeneration==current.continuityGeneration &&
                old.optimizationMode==current.optimizationMode && old.pictureTop==current.pictureTop && old.pictureBottom==current.pictureBottom &&
                old.sharedPicture.Matches(current.sharedPicture) &&
                sameBase(old.sharedPicture.bounds,*physicalBase) &&
                b.acceptedSequence>a.acceptedSequence && a.sourceFrameNumber && b.sourceFrameNumber>a.sourceFrameNumber &&
                b.sourceFrameNumber-a.sourceFrameNumber==b.acceptedSequence-a.acceptedSequence &&
                b.captureTimestamp>a.captureTimestamp;
            if(!context)return Reject("held-current-seed-unqualified");
            const double period=std::isfinite(frameMs)&&frameMs>0?frameMs:1000.0/60;
            if(m_elapsed+period*double(b.acceptedSequence-a.acceptedSequence)>(std::max)(0,(std::min)(1000,holdMs)))
                return Reject("held-current-seed-expired");
            m_reason="held-current-proof-preserves-original-seed";
            return false;
        }
        const bool independentCurrentCue=current.independentBarAnalysis && !current.analysisRefresh &&
            !current.barTrackingAuthority && !current.currentAnchorUsesTrackedEdge &&
            current.barEvidence.available && !current.barEvidence.topRevalidatedAtReference &&
            !current.barEvidence.topRevalidatedNearby && !current.barEvidence.bottomRevalidatedAtReference &&
            !current.barEvidence.bottomRevalidatedNearby && !current.barEvidence.captionBackedTop &&
            !current.barEvidence.captionBackedBottom &&
            current.pictureTop==current.barEvidence.top && current.pictureBottom==current.barEvidence.bottom &&
            current.ink && current.ink->rawEvidenceComplete &&
            !current.text.held && !accepted.held && !accepted.revised;
        const bool resolvedCovers=covers(preview.text);
        const bool certifiedExistingCue=independentCurrentCue && existingCueCertificate &&
            existingCueCertificate->Matches(preview,accepted);
        // A complete current grouping already met the normal seed eligibility;
        // the tracker certificate is needed only when pruning omitted that cue.
        const bool currentCompletePositive=independentCurrentCue && resolvedCovers;
        const bool eligibleSameCueRearm=certifiedExistingCue || currentCompletePositive;
        const bool consumedCertificate=independentCurrentCue && m_consumedCertificate.Matches(preview,accepted);
        m_diagnostics.readyRearmRejectCode=eligibleSameCueRearm ? ReadyRearmRejectCode::NotAttempted :
            ReadyRearmRejectCode::NoEligibleFreshPositive;
        if(!preview.available || !preview.current.analyzed || preview.current.pendingRefresh ||
            preview.current.discontinuity || preview.current.glyphLocalTop || preview.current.glyphLocalBottom ||
            !preview.current.barAuthority ||
            !source.IsValid() || source.width!=preview.current.width || source.height!=preview.current.height ||
            !accepted.detected || accepted.confirmationPending || accepted.workLimit || accepted.lineCount<1 || accepted.lineCount>3 ||
            !covers(preview.current.text) || (!resolvedCovers && !certifiedExistingCue && !consumedCertificate)) {
            return Reject("accepted-seed-incomplete");
        }
        if(!resolvedCovers && consumedCertificate) {
            // The tracker intentionally exports no new certificate on a repeated
            // presentation. Preserve the exact prior seed without renewing time.
            m_diagnostics.readyRearmRejectCode=ReadyRearmRejectCode::Sequence;
            m_reason="accepted-certificate-already-consumed";return false;
        }
        const auto& old=m_certifiedSeed;const auto& now=current.identity;
        if(!resolvedCovers && certifiedExistingCue && old.acceptedSequence &&
            old.transportGeneration==now.transportGeneration && old.sourceFormatGeneration==now.sourceFormatGeneration &&
            old.analysisDomainKey==now.analysisDomainKey && old.viewportGeneration==now.viewportGeneration &&
            old.rendererGeneration==now.rendererGeneration && m_certifiedPolicy==preview.policyGeneration &&
            m_certifiedContinuity==preview.continuityGeneration && now.acceptedSequence<=old.acceptedSequence) {
            // Replaying the same exported certificate cannot renew the lease.
            m_diagnostics.readyRearmRejectCode=ReadyRearmRejectCode::Sequence;
            m_reason="accepted-certificate-already-consumed";return false;
        }
        if(HasPixelSeed() && eligibleSameCueRearm && now.acceptedSequence<=m_last.acceptedSequence) {
            const bool sameSource=SameAnalysisFrameIdentity(now,m_last);
            m_diagnostics.readyRearmRejectCode=ReadyRearmRejectCode::Sequence;
            m_reason=sameSource ? (certifiedExistingCue ? "accepted-certificate-already-consumed" :
                "accepted-current-positive-already-consumed") : "accepted-current-positive-not-forward";
            return false;
        }
        if(eligibleSameCueRearm &&
            TryRearmAcceptedSameCue(preview,accepted,source,frameMs,currentFootprint,eligibleSameCueRearm)) {
            if(certifiedExistingCue && existingCueCertificate) {
                // Remember the newest exact current certificate with the
                // adopted wrapper. A repeated tracker presentation may omit
                // exporting it; that replay must preserve this seed without
                // rearming or spending its lease again.
                m_certifiedSeed=now;m_certifiedPolicy=preview.policyGeneration;
                m_certifiedContinuity=preview.continuityGeneration;
                m_consumedCertificate=*existingCueCertificate;
            }
            return true;
        }
        if(eligibleSameCueRearm && currentFootprint)
            m_diagnostics.readyRearmRejectCode=ReadyRearmRejectCode::FootprintDelegated;
        const auto readyRearmReject=m_diagnostics.readyRearmRejectCode;
        const auto readyRearmLayoutMismatchBits=m_diagnostics.readyRearmLayoutMismatchBits;
        auto seed=preview;seed.current.text=accepted;seed.text=accepted;
        Resolve(seed,source,frameMs,holdMs,nullptr,false,currentFootprint,presentationTemplateProof);
        m_diagnostics.readyRearmRejectCode=readyRearmReject;
        m_diagnostics.readyRearmLayoutMismatchBits=readyRearmLayoutMismatchBits;
        if(HasPixelSeed() && !resolvedCovers && certifiedExistingCue) {
            m_certifiedSeed=now;m_certifiedPolicy=preview.policyGeneration;
            m_certifiedContinuity=preview.continuityGeneration;
            m_consumedCertificate=*existingCueCertificate;
            m_reason="accepted-existing-cue-certified";
        }
        return HasPixelSeed();
    }
    // Consume already verified cadence pixels only after the tracker accepts
    // the exact same cue. Advance the last-verified source, never the seed,
    // native samples, continuity generation or original pending budget.
    bool AdvanceVerifiedCadence(const SubtitleCadenceProof& cadence,
        const SubtitleBoxResult& accepted,double frameMs,int holdMs) {
        const auto& p=cadence.continuation;const auto& v=cadence.preview;
        const auto& c=v.current;const auto& old=m_reference.current;
        const auto sameContext=[&]() {
            const auto& a=old.identity;const auto& b=c.identity;
            return a.transportGeneration==b.transportGeneration &&
                a.sourceFormatGeneration==b.sourceFormatGeneration && a.analysisDomainKey==b.analysisDomainKey &&
                a.viewportGeneration==b.viewportGeneration && a.rendererGeneration==b.rendererGeneration &&
                old.width==c.width && old.height==c.height && old.nearBarDistance==c.nearBarDistance &&
                old.scriptHint==c.scriptHint && old.optimizationMode==c.optimizationMode &&
                old.pictureTop==c.pictureTop && old.pictureBottom==c.pictureBottom &&
                old.policyGeneration==c.policyGeneration && old.continuityGeneration==c.continuityGeneration &&
                m_reference.policyGeneration==v.policyGeneration && m_reference.continuityGeneration==v.continuityGeneration;
        };
        if(!HasPixelSeed() || !cadence.valid || !v.available || !c.analyzed ||
            !c.cadenceSkipped || c.analysisStride!=2 || !c.pendingRefresh ||
            c.analysisRefresh || c.discontinuity || c.text.workLimit || accepted.workLimit ||
            !p.valid || p.localGlyphsOnly || !p.samples || !cadence.samples ||
            !p.MatchesCue(accepted) || !p.MatchesCue(m_reference.text) ||
            !SameAnalysisFrameIdentity(p.identity,c.identity) || !sameContext() ||
            p.policyGeneration!=v.policyGeneration || p.continuityGeneration!=v.continuityGeneration ||
            !p.Matches(c.identity,v.policyGeneration,v.continuityGeneration,old.sharedPicture.bounds) ||
            !c.sharedPicture.AvailableFor(c.identity,c.width,c.height) ||
            !c.sharedPicture.Matches(old.sharedPicture) ||
            !std::isfinite(frameMs) || frameMs<=0 || !std::isfinite(p.elapsedMs) ||
            std::abs(p.elapsedMs-frameMs)>0.001)
            return Reject("cadence-advance-context-changed");
        const double pendingBudget=(std::max)(0,(std::min)(1000,holdMs));
        if(m_elapsed>pendingBudget)return Reject("pending-budget-expired");
        if(SameAnalysisFrameIdentity(m_last,c.identity))return true; // Retry spends no time twice.
        if(!SameAnalysisFrameIdentity(p.previousIdentity,m_last) ||
            !SameAnalysisFrameIdentity(p.templateIdentity,m_last) ||
            m_last.acceptedSequence==UINT64_MAX || c.identity.acceptedSequence!=m_last.acceptedSequence+1 ||
            !m_last.sourceFrameNumber || m_last.sourceFrameNumber==UINT64_MAX ||
            c.identity.sourceFrameNumber!=m_last.sourceFrameNumber+1 ||
            c.identity.captureTimestamp<=m_last.captureTimestamp)
            return Reject("cadence-advance-source-gap");
        if(m_elapsed+frameMs>pendingBudget)
            return Reject("pending-budget-expired");
        m_elapsed+=frameMs;m_last=c.identity;m_reason="cadence-last-source-advanced";
        return true;
    }
    // Worker-only source proof for a static accepted cue. This is deliberately
    // independent of Resolve's short pending lease: it reads required current
    // pixels and never advances or replaces the immutable seed.
    bool VerifyCurrentCuePixels(const SubtitleBoxObservation& current,
        const AnalysisLumaSource& source,const SubtitleCadenceGuard* currentFootprint,
        std::shared_ptr<SubtitleInkSnapshot>& currentInk,const char*& reason,
        bool presentationTemplateProof=false) {
        m_diagnostics={};currentInk.reset();reason="current-cue-proof-unavailable";
        const auto& seed=m_reference.current;
        if(!HasPixelSeed() || !m_currentInkProofSeeded || !source.backingOwner ||
            !source.IsValid() || source.width!=seed.width || source.height!=seed.height ||
            current.width!=seed.width || current.height!=seed.height || current.discontinuity ||
            current.identity.transportGeneration!=seed.identity.transportGeneration ||
            current.identity.sourceFormatGeneration!=seed.identity.sourceFormatGeneration ||
            current.identity.analysisDomainKey!=seed.identity.analysisDomainKey ||
            current.identity.viewportGeneration!=seed.identity.viewportGeneration ||
            current.identity.rendererGeneration!=seed.identity.rendererGeneration ||
            current.policyGeneration!=seed.policyGeneration ||
            current.continuityGeneration!=seed.continuityGeneration ||
            current.nearBarDistance!=seed.nearBarDistance || current.scriptHint!=seed.scriptHint ||
            current.optimizationMode!=seed.optimizationMode ||
            !current.sharedPicture.AvailableFor(current.identity,current.width,current.height) ||
            !current.sharedPicture.Matches(seed.sharedPicture) ||
            current.identity.acceptedSequence<=seed.identity.acceptedSequence ||
            current.identity.captureTimestamp<=seed.identity.captureTimestamp ||
            (seed.identity.sourceFrameNumber && (!current.identity.sourceFrameNumber ||
                current.identity.sourceFrameNumber<=seed.identity.sourceFrameNumber)) ||
            (m_currentFootprintRequired!=static_cast<bool>(currentFootprint))) {
            reason="current-cue-proof-context-changed";return false;
        }
        if(!BuildCurrentInkProof(source,current,currentInk,reason,currentFootprint,presentationTemplateProof))return false;
        if(!currentInk || currentInk->rawEvidenceComplete || !currentInk->paletteValid) {
            currentInk.reset();reason="current-cue-proof-not-presentation-only";return false;
        }
        reason="current-cue-pixels-verified";return true;
    }
    bool Resolve(SubtitleBoxPreview& preview,const AnalysisLumaSource& source,
        double frameMs,int holdMs,
        const ActivePictureFrameIdentity* backlogTemplateIdentity=nullptr,
        bool backlogRetirementOnly=false,
        const SubtitleCadenceGuard* currentFootprint=nullptr,
        bool presentationTemplateProof=false) {
        struct FootprintReceiptScope {
            const SubtitleCadenceGuard* guard=nullptr;
            bool keep=false;
            ~FootprintReceiptScope(){if(guard && !keep)guard->ClearCurrentFootprintProtectionReceipt();}
        } receiptScope{currentFootprint,false};
        if(currentFootprint)currentFootprint->ClearCurrentFootprintProtectionReceipt();
        m_diagnostics={};
        const auto key=preview.current;
        m_diagnostics.lastVerifiedSource=m_last.acceptedSequence;
        m_diagnostics.tokenSource=backlogTemplateIdentity?backlogTemplateIdentity->acceptedSequence:0;
        m_diagnostics.backlogTokenAvailable=backlogRetirementOnly && backlogTemplateIdentity;
        m_diagnostics.referenceContinuity=m_reference.continuityGeneration;
        m_diagnostics.currentContinuity=preview.continuityGeneration;
        if(preview.available) {
            if(key.pendingRefresh)return false; // A repeated presentation cannot renew the budget.
            if(key.analyzed && !key.glyphLocalTop && !key.glyphLocalBottom &&
                key.text.detected && key.barAuthority && !key.text.workLimit) {
                const bool requireFootprint=currentFootprint!=nullptr;
                if(requireFootprint && !currentFootprint->MatchesAcceptedSeed(
                    preview,preview.text,source,key.sharedPicture.bounds))
                    return Reject("accepted-current-footprint-seed-unavailable");
                m_reference=preview;m_pixelSeedIdentity=key.identity;m_pixelSeedInk=key.ink;
                m_samples.clear();m_elapsed=0;m_last=key.identity;
                m_ownedSamples.clear();m_backingSamples.clear();m_knownInkSamples.clear();m_paletteFringe.clear();
                m_currentInkProofSeeded=false;m_currentFootprintRequired=false;
                m_sourceLayout={};
                const auto currentInkSeed=SeedCurrentInkProof(preview,source,
                    !requireFootprint && !presentationTemplateProof,currentFootprint,presentationTemplateProof);
                if(currentInkSeed==CurrentInkSeedState::Seeded) {
                    m_currentFootprintRequired=requireFootprint;
                    m_reason="accepted-current-ink-seed";
                    return false;
                }
                if(requireFootprint)
                    return Reject("accepted-current-footprint-seed-unavailable");
                if(currentInkSeed==CurrentInkSeedState::Failed)
                    return Reject("accepted-current-ink-seed-unavailable");
                const int step=SubtitleBoxDetector::SamplingStep(source.width,source.height);
                for(int line=0;line<key.text.lineCount && line<3;++line) {
                    const auto r=key.text.lineBounds[line];
                    for(int y=(std::max)(0,r.top-step);y<(std::min)(source.height,r.bottom+step);y+=step)
                        for(int x=(std::max)(0,r.left-step);x<(std::min)(source.width,r.right+step);x+=step) {
                            AnalysisLumaSample value;
                            if(m_samples.size()>=32768 || !source.Sample(x,y,value)) return Reject("seed-sample-unavailable");
                            m_samples.push_back({x,y,value});
                        }
                }
                m_reason="accepted-seed";
            } else Reject("seed-not-detected");
            return false;
        }
        const auto& old=m_reference.current;
        const auto& a=m_last;const auto& b=key.identity;
        std::shared_ptr<SubtitleInkSnapshot> currentInkForOutput;
        frameMs=std::isfinite(frameMs)&&frameMs>0?frameMs:1000.0/60;
        if(!HasPixelSeed())return Reject("no-accepted-seed");
        // Presentation may skip captured frames under load. Current native
        // pixels still prove the fixed accepted cue, but spend the full source
        // time gap and never permit repeats, backwards frames or epoch changes.
        if(b.acceptedSequence<=a.acceptedSequence)return Reject("non-forward-source");
        const double elapsedMs=frameMs*double(b.acceptedSequence-a.acceptedSequence);
        if(m_elapsed+elapsedMs>(std::max)(0,(std::min)(1000,holdMs)))return Reject("pending-budget-expired");
        uint32_t context=0;
        if(key.discontinuity)context|=ContextDiscontinuity;
        if(!source.IsValid())context|=ContextSource;
        if(source.width!=key.width || source.height!=key.height ||
            key.width!=old.width || key.height!=old.height)context|=ContextDimensions;
        if(a.transportGeneration!=b.transportGeneration)context|=ContextTransport;
        if(a.sourceFormatGeneration!=b.sourceFormatGeneration)context|=ContextFormat;
        if(a.analysisDomainKey!=b.analysisDomainKey)context|=ContextDomain;
        if(a.viewportGeneration!=b.viewportGeneration)context|=ContextViewport;
        if(a.rendererGeneration!=b.rendererGeneration)context|=ContextRenderer;
        if(key.nearBarDistance!=old.nearBarDistance)context|=ContextNearBar;
        if(key.scriptHint!=old.scriptHint)context|=ContextScript;
        if(key.optimizationMode!=old.optimizationMode)context|=ContextOptimization;
        if(key.policyGeneration!=old.policyGeneration ||
            preview.policyGeneration!=key.policyGeneration)context|=ContextPolicy;
        if(key.continuityGeneration!=old.continuityGeneration ||
            preview.continuityGeneration!=key.continuityGeneration)context|=ContextContinuity;
        m_diagnostics.contextMismatch=context;
        // The timeline witness authorizes only this queue-only continuity
        // change. It is tied to the last pixel-validated source, which advances
        // on pending frames; the original seed and its lease age do not renew.
        const uint64_t sourceGap=b.acceptedSequence-a.acceptedSequence;
        const bool backlogReassociation=context==ContextContinuity &&
            backlogRetirementOnly && backlogTemplateIdentity &&
            SameAnalysisFrameIdentity(a,*backlogTemplateIdentity) &&
            old.continuityGeneration && key.continuityGeneration &&
            preview.continuityGeneration==key.continuityGeneration &&
            key.continuityGeneration>old.continuityGeneration &&
            b.captureTimestamp>a.captureTimestamp &&
            (!(a.sourceFrameNumber || b.sourceFrameNumber) ||
                (a.sourceFrameNumber && b.sourceFrameNumber>a.sourceFrameNumber &&
                 b.sourceFrameNumber-a.sourceFrameNumber==sourceGap));
        if(context && !backlogReassociation)return Reject("pending-context-changed");
        if(m_currentInkProofSeeded) {
            const char* proofFailure="current-ink-proof-unavailable";
            if(m_currentFootprintRequired!=static_cast<bool>(currentFootprint))
                return Reject("current-footprint-mode-changed");
            if(!BuildCurrentInkProof(source,key,currentInkForOutput,proofFailure,currentFootprint)) {
                if(currentFootprint)currentFootprint->ClearCurrentFootprintProtectionReceipt();
                return Reject(proofFailure);
            }
            // This current-source continuation witness is not a detector pass.
            // Keep rawEvidenceComplete clear so it cannot mint current semantic,
            // acquisition, absence, or native authority.
        } else {
            if(m_currentFootprintRequired || currentFootprint)
                return Reject("current-footprint-required-unavailable");
            for(const auto& sample:m_samples) {
                AnalysisLumaSample now;
                ++m_diagnostics.glyphSamplesChecked;
                if(!source.Sample(sample.x,sample.y,now) ||
                    std::abs(int(now.luma)-sample.value.luma)>8 ||
                    std::abs(int(now.chromaU)-sample.value.chromaU)>8 ||
                    std::abs(int(now.chromaV)-sample.value.chromaV)>8) {
                    m_diagnostics.glyphMismatchX=sample.x;
                    m_diagnostics.glyphMismatchY=sample.y;
                    return Reject("current-pixels-changed");
                }
            }
        }
        SubtitleBarEvidence bars;
        m_diagnostics.expectedTop=old.pictureTop;
        m_diagnostics.expectedBottom=old.pictureBottom;
        if(key.sharedPicture.required) {
            m_diagnostics.sharedPictureMatches=key.sharedPicture.Matches(old.sharedPicture);
            m_diagnostics.sharedPictureCurrent=
                key.sharedPicture.AvailableFor(key.identity,source.width,source.height);
            if(!m_diagnostics.sharedPictureMatches || !m_diagnostics.sharedPictureCurrent)
                return Reject("shared-picture-changed");
            // Matching retained Classic geometry does not prove that its bar
            // planes still exist on this frame. Pending reuse needs the same
            // current-pixel proof as a complete shared measurement, and the
            // effective analysis planes must equal the accepted cue's planes.
            // Pending render-thread reuse cannot admit nearby/reference proof;
            // leave the extra search to the asynchronous full measurement.
            bars=ResolveCurrentSubtitleAnalysisBars(source,ExtractSubtitleBarEvidence(source),
                key.sharedPicture.bounds.top,key.sharedPicture.bounds.bottom,nullptr,nullptr,false);
            m_diagnostics.barsAvailable=bars.available;
            m_diagnostics.topRevalidated=bars.topRevalidatedAtReference;
            m_diagnostics.bottomRevalidated=bars.bottomRevalidatedAtReference;
            m_diagnostics.measuredTop=bars.top;
            m_diagnostics.measuredBottom=bars.bottom;
            // No fresh detector ink exists on a pending frame. A fixed-plane
            // revalidation could otherwise mistake the old glyphs themselves
            // for an encoded edge after the physical bar disappears.
            if(!bars.available || bars.topRevalidatedAtReference || bars.topRevalidatedNearby ||
                bars.bottomRevalidatedAtReference ||
                bars.bottomRevalidatedNearby ||
                bars.top!=old.pictureTop || bars.bottom!=old.pictureBottom)
                return Reject("current-bars-changed");
        } else {
            bars=ExtractSubtitleBarEvidence(source);
            m_diagnostics.barsAvailable=bars.available;
            m_diagnostics.topRevalidated=bars.topRevalidatedAtReference;
            m_diagnostics.bottomRevalidated=bars.bottomRevalidatedAtReference;
            m_diagnostics.measuredTop=bars.top;
            m_diagnostics.measuredBottom=bars.bottom;
            if(!bars.available || bars.top!=old.pictureTop || bars.bottom!=old.pictureBottom) return Reject("current-bars-changed");
        }
        m_elapsed+=elapsedMs;m_last=b;
        // Rebase only a validated backlog continuity change after unchanged
        // CURRENT glyphs and physical bars pass. Ordinary pending refreshes
        // preserve the accepted wrapper metadata, samples and elapsed time.
        if(backlogReassociation) {
            m_reference.current.continuityGeneration=key.continuityGeneration;
            m_reference.continuityGeneration=key.continuityGeneration;
        }
        preview=m_reference;preview.current.identity=b;preview.current.discontinuity=false;
        if(currentInkForOutput)preview.current.ink=std::move(currentInkForOutput);
        preview.current.sharedPicture=key.sharedPicture;
        preview.current.barEvidence=bars;preview.current.barTrackingAuthority=false;
        preview.current.currentAnchorUsesTrackedEdge=false;preview.current.hasBarTrackingReference=false;
        preview.current.analysisMs=0;preview.current.analysisRefresh=false;
        preview.current.pendingRefresh=true;
        preview.current.incomingBarTrackingReference={};
        preview.current.subtitleArProof={};preview.current.subtitleArDarkness={};
        preview.current.barStripClearance={};
        if(m_currentFootprintRequired) {
            // Current operation pixels are proved; no new physical boundary
            // measurement or caption absence has been performed on this source.
            preview.current.independentBarAnalysis=false;
            preview.current.barAuthority=false;preview.current.barEvidence={};
        }
        preview.futureAvailable=false;preview.following={};preview.followingCount=0;preview.matchingFrames=1;
        preview.currentLinesConfirmed=false;preview.newScanMs=0;preview.newScanFrames=0;
        m_diagnostics.backlogReassociated=backlogReassociation;
        m_reason=backlogReassociation ? "backlog-verified-current-pixels" : "verified-current-pixels";
        receiptScope.keep=currentFootprint!=nullptr;
        return true;
    }
private:
    bool HasPixelSeed() const { return !m_samples.empty() || m_currentInkProofSeeded; }
    static bool SameCueRect(const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
    }
    static bool SameRearmPhysicalBase(const ActivePictureBounds& a,const ActivePictureBounds& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom &&
            a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight &&
            a.aspectRatio==b.aspectRatio && a.trustedBarAxes==b.trustedBarAxes;
    }
    static bool SameCueLayout(const SubtitleBoxResult& a,const SubtitleBoxResult& b,bool compareCue=true) {
        if(!a.detected || !b.detected || (compareCue && a.cue!=b.cue) || a.lineCount!=b.lineCount ||
            a.capturePanelMeasured!=b.capturePanelMeasured ||
            !SameCueRect(a.bounds,b.bounds) || !SameCueRect(a.anchor,b.anchor) ||
            !SameCueRect(a.sourcePanel,b.sourcePanel) || !SameCueRect(a.provenSourcePanel,b.provenSourcePanel) ||
            !SameCueRect(a.capturePanel,b.capturePanel) || a.signature!=b.signature ||
            a.anchorSignature!=b.anchorSignature || a.topSearchComplete!=b.topSearchComplete ||
            a.bottomSearchComplete!=b.bottomSearchComplete ||
            a.nearBarEligibilityMeasured!=b.nearBarEligibilityMeasured ||
            a.nearBarEligible!=b.nearBarEligible)return false;
        for(int line=0;line<a.lineCount && line<3;++line)
            if(!SameCueRect(a.lineBounds[line],b.lineBounds[line]) ||
                a.lineSignatures[line]!=b.lineSignatures[line] ||
                !SameCueRect(a.linePanels[line],b.linePanels[line]) ||
                !SameCueRect(a.provenLinePanels[line],b.provenLinePanels[line]) ||
                !SameCueRect(a.lineCapturePanels[line],b.lineCapturePanels[line]) ||
                a.panelTopEdges[line].y!=b.panelTopEdges[line].y ||
                a.panelTopEdges[line].left!=b.panelTopEdges[line].left ||
                a.panelTopEdges[line].right!=b.panelTopEdges[line].right ||
                a.panelTopEdges[line].supportBasisPoints!=b.panelTopEdges[line].supportBasisPoints)return false;
        return true;
    }
    static uint32_t CueLayoutMismatchBits(const SubtitleBoxResult& a,const SubtitleBoxResult& b,
        bool compareCue=true) {
        uint32_t bits=0;
        if(!a.detected || !b.detected || (compareCue && a.cue!=b.cue))bits|=ReadyLayoutCueIdentity;
        if(a.lineCount!=b.lineCount)bits|=ReadyLayoutLines;
        if(!SameCueRect(a.bounds,b.bounds) || !SameCueRect(a.anchor,b.anchor))bits|=ReadyLayoutBounds;
        if(!SameCueRect(a.sourcePanel,b.sourcePanel) || !SameCueRect(a.provenSourcePanel,b.provenSourcePanel))
            bits|=ReadyLayoutPanels;
        if(a.capturePanelMeasured!=b.capturePanelMeasured || !SameCueRect(a.capturePanel,b.capturePanel))
            bits|=ReadyLayoutCapture;
        if(a.signature!=b.signature || a.anchorSignature!=b.anchorSignature ||
            a.lineSignatures!=b.lineSignatures)bits|=ReadyLayoutSignatures;
        for(int line=0;line<a.lineCount && line<3;++line) {
            if(!SameCueRect(a.linePanels[line],b.linePanels[line]) ||
                !SameCueRect(a.provenLinePanels[line],b.provenLinePanels[line]))bits|=ReadyLayoutPanels;
            if(!SameCueRect(a.lineCapturePanels[line],b.lineCapturePanels[line]))bits|=ReadyLayoutCapture;
            if(!SameCueRect(a.lineBounds[line],b.lineBounds[line]))bits|=ReadyLayoutLines;
            if(a.panelTopEdges[line].y!=b.panelTopEdges[line].y ||
                a.panelTopEdges[line].left!=b.panelTopEdges[line].left ||
                a.panelTopEdges[line].right!=b.panelTopEdges[line].right ||
                a.panelTopEdges[line].supportBasisPoints!=b.panelTopEdges[line].supportBasisPoints)
                bits|=ReadyLayoutEdges;
        }
        if(a.topSearchComplete!=b.topSearchComplete || a.bottomSearchComplete!=b.bottomSearchComplete ||
            a.nearBarEligibilityMeasured!=b.nearBarEligibilityMeasured || a.nearBarEligible!=b.nearBarEligible)
            bits|=ReadyLayoutEdges;
        return bits;
    }
    static ReadyRearmRejectCode CueLayoutCodeForMismatch(uint32_t bits) {
        if(bits&ReadyLayoutCueIdentity)return ReadyRearmRejectCode::CueIdentity;
        if(bits&ReadyLayoutBounds)return ReadyRearmRejectCode::CueBounds;
        if(bits&(ReadyLayoutPanels|ReadyLayoutCapture))return ReadyRearmRejectCode::CuePanelsCapture;
        if(bits&ReadyLayoutSignatures)return ReadyRearmRejectCode::CueSignatures;
        if(bits&ReadyLayoutLines)return ReadyRearmRejectCode::CueLines;
        if(bits&ReadyLayoutEdges)return ReadyRearmRejectCode::CueEdges;
        return ReadyRearmRejectCode::NotAttempted;
    }
    bool TryRearmAcceptedSameCue(const SubtitleBoxPreview& preview,const SubtitleBoxResult& accepted,
        const AnalysisLumaSource& source,double frameMs,const SubtitleCadenceGuard* currentFootprint,
        bool freshIndependentCue) {
        m_diagnostics.readyRearmMaskWordsChecked=0;
        m_diagnostics.readyRearmGlyphSamplesChecked=0;
        m_diagnostics.readyRearmKnownSamplesChecked=0;
        m_diagnostics.readyRearmBackingSamplesChecked=0;
        m_diagnostics.readyRearmLayoutMismatchBits=0;
        const auto reject=[&](ReadyRearmRejectCode code) {
            m_diagnostics.readyRearmRejectCode=code;return false;
        };
        if(!m_currentInkProofSeeded || m_currentFootprintRequired || currentFootprint ||
            !m_samples.empty() || !std::isfinite(frameMs) || frameMs<=0)
            return reject(ReadyRearmRejectCode::SeedOrMode);
        const auto& old=m_reference.current;const auto& current=preview.current;
        const auto oldInk=m_pixelSeedInk;const auto ink=current.ink;
        const auto& a=m_last;const auto& b=current.identity;
        if(!oldInk || !ink)return reject(ReadyRearmRejectCode::InkGrid);
        if(!source.IsValid() || !SameSourceLayout(source,m_sourceLayout) ||
            source.width!=old.width || source.height!=old.height || current.width!=old.width || current.height!=old.height)
            return reject(ReadyRearmRejectCode::SourceLayout);
        if(!current.analyzed || (current.analysisStride!=1 && current.analysisStride!=2) || current.cadenceSkipped ||
            current.analysisRefresh || current.pendingRefresh || current.discontinuity ||
            current.glyphLocalTop || current.glyphLocalBottom || current.text.held ||
            current.text.confirmationPending || current.text.revised || current.text.workLimit)
            return reject(ReadyRearmRejectCode::CurrentFlags);
        if(!current.independentBarAnalysis || !current.barAuthority || current.barTrackingAuthority ||
            current.currentAnchorUsesTrackedEdge || !current.barEvidence.available ||
            current.barEvidence.topRevalidatedAtReference || current.barEvidence.topRevalidatedNearby ||
            current.barEvidence.bottomRevalidatedAtReference || current.barEvidence.bottomRevalidatedNearby ||
            current.barEvidence.captionBackedTop || current.barEvidence.captionBackedBottom)
            return reject(ReadyRearmRejectCode::BarEvidence);
        if(current.pictureTop!=current.barEvidence.top || current.pictureBottom!=current.barEvidence.bottom ||
            current.pictureTop!=old.pictureTop || current.pictureBottom!=old.pictureBottom)
            return reject(ReadyRearmRejectCode::PhysicalBounds);
        if(current.policyGeneration!=preview.policyGeneration || old.policyGeneration!=preview.policyGeneration ||
            m_reference.policyGeneration!=preview.policyGeneration)
            return reject(ReadyRearmRejectCode::PolicyGeneration);
        if(current.continuityGeneration!=preview.continuityGeneration ||
            m_reference.continuityGeneration!=old.continuityGeneration)
            return reject(ReadyRearmRejectCode::ContinuityGeneration);
        if(old.continuityGeneration!=preview.continuityGeneration &&
            (!freshIndependentCue || !old.continuityGeneration || !preview.continuityGeneration))
            return reject(ReadyRearmRejectCode::ContinuityGeneration);
        if(!SubtitleBoxLookahead::SameContext(old,current))return reject(ReadyRearmRejectCode::IdentityContext);
        if(!SameRearmPhysicalBase(old.sharedPicture.bounds,current.sharedPicture.bounds))
            return reject(ReadyRearmRejectCode::PhysicalBounds);
        if(!current.sharedPicture.AvailableFor(current.identity,current.width,current.height) ||
            current.sharedPicture.required!=old.sharedPicture.required || !old.sharedPicture.Matches(current.sharedPicture))
            return reject(ReadyRearmRejectCode::IdentityContext);
        if(!current.text.topSearchComplete || !current.text.bottomSearchComplete)
            return reject(ReadyRearmRejectCode::CueEdges);
        m_diagnostics.readyRearmLayoutMismatchBits=
            CueLayoutMismatchBits(m_reference.text,current.text,false) |
            CueLayoutMismatchBits(m_reference.text,accepted,true) |
            CueLayoutMismatchBits(current.text,accepted,false) |
            CueLayoutMismatchBits(m_reference.text,m_reference.current.text,false);
        if(m_diagnostics.readyRearmLayoutMismatchBits)
            return reject(CueLayoutCodeForMismatch(m_diagnostics.readyRearmLayoutMismatchBits));
        if(b.acceptedSequence<=a.acceptedSequence || !b.sourceFrameNumber || !a.sourceFrameNumber ||
            b.sourceFrameNumber<=a.sourceFrameNumber ||
            b.sourceFrameNumber-a.sourceFrameNumber!=b.acceptedSequence-a.acceptedSequence ||
            b.captureTimestamp<=a.captureTimestamp)return reject(ReadyRearmRejectCode::Sequence);
        if(!ink->rawEvidenceComplete || !ink->paletteValid || !oldInk->rawEvidenceComplete || !oldInk->paletteValid)
            return reject(ReadyRearmRejectCode::InkPalette);
        if(ink->width!=oldInk->width || ink->height!=oldInk->height || ink->step!=oldInk->step ||
            ink->sourceRows!=oldInk->sourceRows)return reject(ReadyRearmRejectCode::InkGrid);
        if(ink->inkFloor!=oldInk->inkFloor || ink->inkU!=oldInk->inkU || ink->inkV!=oldInk->inkV ||
            ink->blackLimit!=oldInk->blackLimit ||
            ink->captionBackingLimit!=oldInk->captionBackingLimit)
            return reject(ReadyRearmRejectCode::InkPalette);

        const size_t words=(size_t(oldInk->width)*oldInk->height+63)/64;
        if(oldInk->width<=0 || oldInk->height<=0 || oldInk->step<=0 ||
            oldInk->rawInk.size()!=words || oldInk->ownedInk.size()!=words ||
            ink->rawInk.size()!=words || ink->ownedInk.size()!=words ||
            oldInk->blackBacking.size()!=words || ink->blackBacking.size()!=words)
            return reject(ReadyRearmRejectCode::InkGrid);
        for(size_t word=0;word<words;++word) {
            ++m_diagnostics.readyRearmMaskWordsChecked;
            // A ready rearm uses the full current detector mask as a cheap
            // source-current novelty check. Any raw or owned-mask change falls
            // through to the normal complete seed path.
            if(oldInk->rawInk[word]!=ink->rawInk[word])return reject(ReadyRearmRejectCode::RawMask);
            if(oldInk->ownedInk[word]!=ink->ownedInk[word])return reject(ReadyRearmRejectCode::OwnedMask);
        }

        const ValidatedAnalysisLumaSampler sampler(source);
        if(!sampler.IsValid())return reject(ReadyRearmRejectCode::SourceLayout);
        for(const auto& glyph:m_ownedSamples) {
            if(glyph.x<0 || glyph.y<0 || glyph.x>=ink->width || glyph.y>=ink->height ||
                !ink->Get(glyph.x,glyph.y,true))return reject(ReadyRearmRejectCode::GlyphPixels);
            AnalysisLumaSample now;
            if(!sampler.Sample(glyph.x*ink->step,ink->sourceRows[size_t(glyph.y)],now))
                return reject(ReadyRearmRejectCode::GlyphPixels);
            ++m_diagnostics.readyRearmGlyphSamplesChecked;
            if(!SubtitleCadenceGuard::EncodingEquivalent(glyph.value,now))
                return reject(ReadyRearmRejectCode::GlyphPixels);
        }
        for(const auto& known:m_knownInkSamples) {
            if(known.x<0 || known.y<0 || known.x>=ink->width || known.y>=ink->height)
                return reject(ReadyRearmRejectCode::KnownPixels);
            AnalysisLumaSample now;
            if(!sampler.Sample(known.x*ink->step,ink->sourceRows[size_t(known.y)],now))
                return reject(ReadyRearmRejectCode::KnownPixels);
            ++m_diagnostics.readyRearmKnownSamplesChecked;
            if(std::abs(int(now.luma)-known.value.luma)>8 ||
                std::abs(int(now.chromaU)-known.value.chromaU)>8 ||
                std::abs(int(now.chromaV)-known.value.chromaV)>8)return reject(ReadyRearmRejectCode::KnownPixels);
        }
        for(const auto& backing:m_backingSamples) {
            if(backing.x<0 || backing.y<0 || backing.x>=ink->width || backing.y>=ink->height)
                return reject(ReadyRearmRejectCode::BackingPixels);
            AnalysisLumaSample now;
            if(!sampler.Sample(backing.x*ink->step,ink->sourceRows[size_t(backing.y)],now))
                return reject(ReadyRearmRejectCode::BackingPixels);
            ++m_diagnostics.readyRearmBackingSamplesChecked;
            const bool inEncodedBar=backing.y<m_contrastSeedTopEnd || backing.y>=m_contrastSeedBottomStart;
            const bool black=now.luma<=ink->blackLimit && (!inEncodedBar ||
                (std::abs(int(now.chromaU)-512)<=48 && std::abs(int(now.chromaV)-512)<=48));
            if(black!=backing.black && !SubtitleCadenceGuard::EncodingEquivalent(
                backing.value,now))return reject(ReadyRearmRejectCode::BackingPixels);
        }
        // Adopt only the actual fresh complete measurement wrapper. The native
        // ink/pixel reference and its identity remain immutable across this
        // continuity-only rebase.
        m_reference=preview;m_reference.current.text=accepted;m_reference.text=accepted;
        m_elapsed=0;m_last=b;m_reason="accepted-same-cue-current-scan-rearmed";
        m_diagnostics.readyRearmRejectCode=ReadyRearmRejectCode::Passed;
        m_diagnostics.lastVerifiedSource=b.acceptedSequence;
        return true;
    }
    struct OwnedSample { int x=0,y=0;AnalysisLumaSample value{}; };
    struct BackingSample { int x=0,y=0;bool black=false;AnalysisLumaSample value{}; };
    struct KnownInkSample { int x=0,y=0;AnalysisLumaSample value{}; };
    enum class CurrentInkSeedState { NotApplicable, Seeded, Failed };
    struct SourceLayout {
        AnalysisLumaFormat format=AnalysisLumaFormat::P010;
        VideoFrameEncoding encoding=VideoFrameEncoding::UNKNOWN;
        ColorSpace colorspace=ColorSpace::UNKNOWN;
        AnalysisLumaRange range=AnalysisLumaRange::Unknown;
        size_t rowBytes=0,chromaRowBytes=0;
        uint64_t generation=0;
        uint8_t rgbBitAlignment=0;
    };
    static constexpr size_t CurrentInkSampleBudget=327680;
    static bool SameSourceLayout(const AnalysisLumaSource& source,const SourceLayout& layout) {
        return source.format==layout.format && source.encoding==layout.encoding &&
            source.colorspace==layout.colorspace && source.range==layout.range &&
            source.rowBytes==layout.rowBytes && source.chromaRowBytes==layout.chromaRowBytes &&
            source.generation==layout.generation && source.rgbBitAlignment==layout.rgbBitAlignment;
    }
    static SourceLayout CaptureSourceLayout(const AnalysisLumaSource& source) {
        SourceLayout layout;layout.format=source.format;layout.encoding=source.encoding;
        layout.colorspace=source.colorspace;layout.range=source.range;layout.rowBytes=source.rowBytes;
        layout.chromaRowBytes=source.chromaRowBytes;layout.generation=source.generation;
        layout.rgbBitAlignment=source.rgbBitAlignment;return layout;
    }
    static bool InCurrentBand(int y,int height,int topEnd,int bottomStart) {
        return y>=0 && y<height && (y<topEnd || y>=bottomStart);
    }
    static size_t CurrentBandCellCount(int width,int height,int topEnd,int bottomStart) {
        const size_t rows=topEnd>=bottomStart?size_t(height):size_t(topEnd)+size_t(height-bottomStart);
        return rows*size_t(width);
    }
    static size_t CurrentBandIndex(int x,int y,int width,int height,int topEnd,int bottomStart) {
        if(topEnd>=bottomStart)return size_t(y)*size_t(width)+size_t(x);
        const size_t row=y<topEnd?size_t(y):size_t(topEnd+y-bottomStart);
        return row*size_t(width)+size_t(x);
    }
    bool CurrentInkGeometry(const SubtitleBoxObservation& observation,
        const SubtitleInkSnapshot& ink,int& topEnd,int& bottomStart) const {
        const int width=observation.width,height=observation.height,step=ink.step;
        if(width<1 || height<1 || step<1 || step>8 || ink.width!=(width+step-1)/step ||
            ink.height!=(height+step-1)/step || step!=SubtitleBoxDetector::SamplingStep(width,height) ||
            ink.sourceRows.size()!=size_t(ink.height) || observation.pictureTop<0 ||
            observation.pictureBottom>height || observation.pictureTop>=observation.pictureBottom ||
            observation.text.topSearchComplete!=(observation.pictureTop>0) ||
            observation.text.bottomSearchComplete!=(observation.pictureBottom<height))return false;
        const int top=(observation.pictureTop+step-1)/step;
        const int bottom=observation.pictureBottom<height?
            (std::min)(ink.height-1,(observation.pictureBottom+step-1)/step):ink.height;
        const int depth=(std::max)(24,ink.height/7)+
            (observation.nearBarDistance+step-1)/step;
        topEnd=observation.pictureTop>0?(std::min)(ink.height,top+depth):0;
        bottomStart=observation.pictureBottom<height?(std::max)(0,bottom-depth):ink.height;
        for(int y=0;y<ink.height;++y) {
            const int expected=observation.pictureTop>0 && y==top-1?observation.pictureTop-1:
                (observation.pictureBottom<height && y==bottom?observation.pictureBottom:
                    (std::min)(height-1,y*step));
            if(ink.sourceRows[size_t(y)]!=expected)return false;
        }
        return true;
    }
    CurrentInkSeedState SeedCurrentInkProof(const SubtitleBoxPreview& preview,const AnalysisLumaSource& source,
        bool seedDirectionalBaseline=true,const SubtitleCadenceGuard* currentFootprint=nullptr,
        bool presentationTemplateProof=false) {
        m_diagnostics.seedBandSamplesChecked=0;m_diagnostics.seedGlyphSamplesChecked=0;
        m_diagnostics.seedKnownSamplesChecked=0;m_diagnostics.seedBackingSamplesChecked=0;
        const auto& current=preview.current;const auto& text=current.text;const auto ink=current.ink;
        const bool verifiedPresentationFootprint=presentationTemplateProof && currentFootprint &&
            currentFootprint->MatchesAcceptedSeed(preview,preview.text,source,current.sharedPicture.bounds);
        if(!preview.available || !current.analyzed || current.pendingRefresh || current.analysisRefresh ||
            current.discontinuity || (!current.independentBarAnalysis && !verifiedPresentationFootprint) || !current.barAuthority ||
            current.barTrackingAuthority || current.currentAnchorUsesTrackedEdge ||
            (seedDirectionalBaseline?current.analysisStride!=1:
                (current.analysisStride!=1 && current.analysisStride!=2)) || current.cadenceSkipped ||
            (!seedDirectionalBaseline && !currentFootprint && !presentationTemplateProof) || !current.barEvidence.available ||
            ((!verifiedPresentationFootprint) &&
                (current.pictureTop!=current.barEvidence.top || current.pictureBottom!=current.barEvidence.bottom)) ||
            current.barEvidence.captionBackedTop || current.barEvidence.captionBackedBottom ||
            !text.detected || text.held || text.confirmationPending || text.revised || text.workLimit ||
            text.lineCount<1 || text.lineCount>3 ||
            !ink || !ink->rawEvidenceComplete ||
            !ink->paletteValid || !source.IsValid() || source.width!=current.width ||
            source.height!=current.height || ink->inkFloor<0 || ink->inkFloor>1023 ||
            ink->inkU<0 || ink->inkU>1023 || ink->inkV<0 || ink->inkV>1023 ||
            ink->blackLimit<0 || ink->blackLimit>1023 ||
            ink->captionBackingLimit<0 || ink->captionBackingLimit>1023 ||
            (ink->captionBackingLimit!=0 &&
                (ink->captionBackingLimit<ink->blackLimit || ink->captionBackingLimit>=ink->inkFloor)))
            return CurrentInkSeedState::NotApplicable;
        if(!text.topSearchComplete || !text.bottomSearchComplete)return CurrentInkSeedState::Failed;
        int topEnd=0,bottomStart=0;
        if(!CurrentInkGeometry(current,*ink,topEnd,bottomStart))return CurrentInkSeedState::Failed;
        const size_t area=size_t(ink->width)*size_t(ink->height),words=(area+63)/64;
        if(ink->rawInk.size()!=words || ink->ownedInk.size()!=words || ink->blackBacking.size()!=words)
            return CurrentInkSeedState::Failed;
        const ValidatedAnalysisLumaSampler sampler(source);
        if(!sampler.IsValid())return CurrentInkSeedState::Failed;
        const int top=(current.pictureTop+ink->step-1)/ink->step;
        const int bottom=current.pictureBottom<current.height?
            (std::min)(ink->height-1,(current.pictureBottom+ink->step-1)/ink->step):ink->height;
        const size_t bandCells=CurrentBandCellCount(ink->width,ink->height,topEnd,bottomStart);
        if(!bandCells || bandCells>CurrentInkSampleBudget)return CurrentInkSeedState::Failed;
        m_contrastSeedPixels.clear();m_contrastSeedDirections.clear();
        m_contrastSeedTopEnd=m_contrastSeedBottomStart=m_contrastSeedPictureTop=m_contrastSeedPictureBottom=0;
        m_contrastSeedWidth=m_contrastSeedHeight=0;
        if(seedDirectionalBaseline) {
            m_contrastSeedPixels.assign(bandCells,AnalysisLumaSample{});
            m_contrastSeedDirections.assign(bandCells,0);
            m_contrastSeedTopEnd=topEnd;m_contrastSeedBottomStart=bottomStart;
            m_contrastSeedPictureTop=top;m_contrastSeedPictureBottom=bottom;
            m_contrastSeedWidth=ink->width;m_contrastSeedHeight=ink->height;
            for(int y=0;y<ink->height;++y)if(InCurrentBand(y,ink->height,topEnd,bottomStart))
                for(int x=0;x<ink->width;++x) {
                    const size_t at=CurrentBandIndex(x,y,ink->width,ink->height,topEnd,bottomStart);
                    if(at>=m_contrastSeedPixels.size() ||
                        !sampler.Sample(x*ink->step,ink->sourceRows[size_t(y)],m_contrastSeedPixels[at]))
                        return CurrentInkSeedState::Failed;
                    ++m_diagnostics.seedBandSamplesChecked;
                }
            constexpr int contrastX[4]={-1,1,0,0},contrastY[4]={0,0,-1,1};
            for(int y=0;y<ink->height;++y)if(InCurrentBand(y,ink->height,topEnd,bottomStart))
                for(int x=1;x<ink->width-1;++x) {
                    const size_t at=CurrentBandIndex(x,y,ink->width,ink->height,topEnd,bottomStart);
                    const auto& point=m_contrastSeedPixels[at];
                    const bool pointInPicture=y>=top && y<bottom;
                    for(int direction=0;direction<4;++direction) {
                        const int nx=x+contrastX[direction],ny=y+contrastY[direction];
                        if(!InCurrentBand(ny,ink->height,topEnd,bottomStart))continue;
                        const bool neighborInPicture=ny>=top && ny<bottom;
                        if(pointInPicture!=neighborInPicture)continue;
                        const size_t neighborAt=CurrentBandIndex(nx,ny,ink->width,ink->height,topEnd,bottomStart);
                        const auto& neighbor=m_contrastSeedPixels[neighborAt];
                        if(int(point.luma)-int(neighbor.luma)>=48)
                            m_contrastSeedDirections[at]|=uint8_t(1u<<direction);
                        const bool alternatePalette=(std::abs(int(point.chromaU)-ink->inkU)>80 ||
                            std::abs(int(point.chromaV)-ink->inkV)>80) &&
                            (std::abs(int(point.chromaU)-int(neighbor.chromaU))>=64 ||
                             std::abs(int(point.chromaV)-int(neighbor.chromaV))>=64);
                        if(alternatePalette)m_contrastSeedDirections[at]|=uint8_t(1u<<(direction+4));
                    }
                }
        }
        else if(currentFootprint && !currentFootprint->MatchesAcceptedSeed(preview,text,source,current.sharedPicture.bounds))
            return CurrentInkSeedState::Failed;
        const auto sampleSeed=[&](int x,int y,AnalysisLumaSample& value) {
            if(x<0 || y<0 || x>=ink->width || y>=ink->height)return false;
            if(seedDirectionalBaseline && InCurrentBand(y,ink->height,topEnd,bottomStart)) {
                const size_t at=CurrentBandIndex(x,y,ink->width,ink->height,topEnd,bottomStart);
                if(at>=m_contrastSeedPixels.size())return false;
                value=m_contrastSeedPixels[at];return true;
            }
            return sampler.Sample(x*ink->step,ink->sourceRows[size_t(y)],value);
        };
        std::vector<KnownInkSample> knownInk;knownInk.reserve(4096);
        for(int y=0;y<ink->height;++y)for(int x=0;x<ink->width;++x)if(ink->Get(x,y)) {
            const int sourceY=ink->sourceRows[size_t(y)];
            const bool encodedBar=sourceY<current.pictureTop || sourceY>=current.pictureBottom;
            if(currentFootprint && !currentFootprint->ContainsFootprintSample(x*ink->step,sourceY))continue;
            if(presentationTemplateProof && !currentFootprint && !encodedBar && !ink->Get(x,y,true))continue;
            if(knownInk.size()>=CurrentInkSampleBudget)return CurrentInkSeedState::Failed;
            AnalysisLumaSample sample;
            if(!sampleSeed(x,y,sample))return CurrentInkSeedState::Failed;
            knownInk.push_back({x,y,sample});
        }
        std::vector<uint64_t> paletteFringe(words,0);
        size_t fringeSamples=0;
        for(int y=0;y<ink->height;++y)if(y<topEnd || y>=bottomStart)for(int x=1;x<ink->width-1;++x) {
            if(presentationTemplateProof && !currentFootprint &&
                ink->sourceRows[size_t(y)]>=current.pictureTop &&
                ink->sourceRows[size_t(y)]<current.pictureBottom)continue;
            if(ink->Get(x,y))continue;
            if(currentFootprint && !currentFootprint->ContainsFootprintSample(x*ink->step,ink->sourceRows[size_t(y)]))continue;
            bool nextToKnownRaw=false;
            for(int dy=-1;dy<=1 && !nextToKnownRaw;++dy)for(int dx=-1;dx<=1;++dx)
                if((dx || dy) && ink->Get(x+dx,y+dy)){nextToKnownRaw=true;break;}
            if(!nextToKnownRaw)continue;
            if(++fringeSamples>CurrentInkSampleBudget)return CurrentInkSeedState::Failed;
            AnalysisLumaSample sample;
            if(!sampleSeed(x,y,sample))return CurrentInkSeedState::Failed;
            if(sample.luma>=ink->inkFloor-80 &&
                std::abs(int(sample.chromaU)-ink->inkU)<=80 &&
                std::abs(int(sample.chromaV)-ink->inkV)<=80) {
                const size_t at=size_t(y)*ink->width+x;
                paletteFringe[at/64]|=uint64_t{1}<<(at%64);
                if(knownInk.size()>=CurrentInkSampleBudget)return CurrentInkSeedState::Failed;
                knownInk.push_back({x,y,sample});
            }
        }
        std::vector<OwnedSample> owned;owned.reserve(4096);
        std::array<unsigned,3> ownedPerLine{};
        for(int line=0;line<text.lineCount;++line) {
            const auto& bounds=text.lineBounds[line];
            if(!bounds.Valid() || bounds.left<0 || bounds.top<0 ||
                bounds.right>current.width || bounds.bottom>current.height)return CurrentInkSeedState::Failed;
            for(int y=0;y<ink->height;++y) {
                const int py=ink->sourceRows[size_t(y)];
                if(py<bounds.top || py>=bounds.bottom)continue;
                for(int x=0;x<ink->width;++x) {
                    const int px=x*ink->step;
                    if(px<bounds.left || px>=bounds.right || !ink->Get(x,y,true))continue;
                    if(!ink->Get(x,y) || owned.size()>=32768 ||
                        (currentFootprint && !currentFootprint->ContainsFootprintSample(px,py)))
                        return CurrentInkSeedState::Failed;
                    AnalysisLumaSample value;
                    if(!sampleSeed(x,y,value) || value.luma<ink->inkFloor ||
                        std::abs(int(value.chromaU)-ink->inkU)>80 ||
                        std::abs(int(value.chromaV)-ink->inkV)>80)return CurrentInkSeedState::Failed;
                    owned.push_back({x,y,value});++ownedPerLine[size_t(line)];
                }
            }
            if(ownedPerLine[size_t(line)]<4)return CurrentInkSeedState::Failed;
        }
        std::vector<uint8_t> support(area,0);
        const auto addSupport=[&](const SubtitleBoxRect& bounds) {
            if(!bounds.Valid())return true;
            if(bounds.left<0 || bounds.top<0 || bounds.right>current.width || bounds.bottom>current.height)
                return false;
            for(int y=0;y<ink->height;++y) {
                const int py=ink->sourceRows[size_t(y)];
                if(py<bounds.top || py>=bounds.bottom)continue;
                for(int x=0;x<ink->width;++x) {
                    const int px=x*ink->step;
                    if(px>=bounds.left && px<bounds.right)support[size_t(y)*ink->width+x]=1;
                }
            }
            return true;
        };
        if(!addSupport(text.sourcePanel) || !addSupport(text.provenSourcePanel) ||
            !addSupport(text.capturePanel))return CurrentInkSeedState::Failed;
        for(int line=0;line<3;++line)
            if(!addSupport(text.linePanels[line]) || !addSupport(text.provenLinePanels[line]) ||
                !addSupport(text.lineCapturePanels[line]))return CurrentInkSeedState::Failed;
        std::vector<BackingSample> backing;backing.reserve(4096);
        for(int y=0;y<ink->height;++y)for(int x=0;x<ink->width;++x) {
            if(presentationTemplateProof && !currentFootprint)continue;
            if(!support[size_t(y)*ink->width+x] ||
                (currentFootprint && !currentFootprint->ContainsFootprintSample(x*ink->step,ink->sourceRows[size_t(y)])))continue;
            AnalysisLumaSample value;
            if(!sampleSeed(x,y,value))return CurrentInkSeedState::Failed;
            backing.push_back({x,y,(y<topEnd || y>=bottomStart)?ink->Black(x,y):
                value.luma<=ink->blackLimit,value});
            if(backing.size()>CurrentInkSampleBudget)return CurrentInkSeedState::Failed;
        }
        m_ownedSamples=std::move(owned);m_backingSamples=std::move(backing);
        m_knownInkSamples=std::move(knownInk);
        m_paletteFringe=std::move(paletteFringe);
        m_diagnostics.seedGlyphSamplesChecked=m_ownedSamples.size();
        m_diagnostics.seedKnownSamplesChecked=m_knownInkSamples.size();
        m_diagnostics.seedBackingSamplesChecked=m_backingSamples.size();
        m_sourceLayout=CaptureSourceLayout(source);m_currentInkProofSeeded=true;
        return CurrentInkSeedState::Seeded;
    }
    bool BuildCurrentInkProof(const AnalysisLumaSource& source,const SubtitleBoxObservation& current,
        std::shared_ptr<SubtitleInkSnapshot>& currentInk,const char*& reason,
        const SubtitleCadenceGuard* currentFootprint,bool presentationTemplateProof=false) {
        m_diagnostics.footprintVerifyMs=0.0;
        m_diagnostics.currentPixelValidationMs=0.0;
        m_diagnostics.encodedBarVetoMs=0.0;
        const auto& old=m_reference.current;const auto oldInk=m_pixelSeedInk;
        if(!m_currentInkProofSeeded || !oldInk || !SameSourceLayout(source,m_sourceLayout) ||
            source.width!=old.width || source.height!=old.height ||
            current.width!=old.width || current.height!=old.height) {
            reason="current-source-layout-changed";return false;
        }
        if(m_currentFootprintRequired) {
            if(!currentFootprint) {reason="current-footprint-required-unavailable";return false;}
            PhaseWallClock footprintPhase(m_diagnostics.footprintVerifyMs);
            const auto verified=currentFootprint->VerifyCurrentFootprint(
                current,source,old,m_reference.text,old.sharedPicture.bounds);
            footprintPhase.Stop();
            if(!verified.valid) {
                m_diagnostics.pixelWitness=verified.pixelWitness;
                m_diagnostics.glyphMismatchX=verified.mismatchX;
                m_diagnostics.glyphMismatchY=verified.mismatchY;
                m_diagnostics.currentBandSamplesChecked=verified.samples;
                if(verified.actualPixelMismatch)
                    m_diagnostics.currentPixelFailure=CurrentPixelVerificationFailure::PixelMismatch;
                reason=verified.reason;return false;
            }
            m_diagnostics.currentFootprintSamplesChecked=verified.samples;
        } else if(currentFootprint) {
            reason="current-footprint-mode-changed";return false;
        }
        PhaseWallClock currentPixelPhase(m_diagnostics.currentPixelValidationMs);
        int topEnd=0,bottomStart=0;
        if(!CurrentInkGeometry(old,*oldInk,topEnd,bottomStart)) {
            reason="current-search-coverage-unavailable";return false;
        }
        const ValidatedAnalysisLumaSampler sampler(source);
        if(!sampler.IsValid()) {reason="current-source-layout-changed";return false;}
        const size_t area=size_t(oldInk->width)*size_t(oldInk->height),words=(area+63)/64;
        if(area>1048576 || oldInk->rawInk.size()!=words || oldInk->ownedInk.size()!=words ||
            oldInk->blackBacking.size()!=words) {reason="current-ink-grid-invalid";return false;}
        auto fresh=std::make_shared<SubtitleInkSnapshot>();
        fresh->width=oldInk->width;fresh->height=oldInk->height;fresh->step=oldInk->step;
        fresh->sourceRows=oldInk->sourceRows;fresh->inkFloor=oldInk->inkFloor;
        fresh->inkU=oldInk->inkU;fresh->inkV=oldInk->inkV;fresh->blackLimit=oldInk->blackLimit;
        fresh->captionBackingLimit=oldInk->captionBackingLimit;
        fresh->paletteValid=true;fresh->rawEvidenceComplete=false;
        fresh->rawInk.assign(words,0);fresh->ownedInk.assign(words,0);fresh->blackBacking.assign(words,0);
        const int top=(old.pictureTop+fresh->step-1)/fresh->step;
        const int bottom=old.pictureBottom<old.height?
            (std::min)(fresh->height-1,(old.pictureBottom+fresh->step-1)/fresh->step):fresh->height;
        // Search rows belong to the subtitle scan, whose snapped edge can lie
        // a few pixels inside the accepted picture. Only source coordinates
        // outside the separately verified physical base are encoded bars.
        const bool physicalBarPolicy=m_currentFootprintRequired || presentationTemplateProof;
        if(physicalBarPolicy && (!old.sharedPicture.AvailableFor(old.identity,old.width,old.height) ||
            !current.sharedPicture.AvailableFor(current.identity,current.width,current.height) ||
            !old.sharedPicture.Matches(current.sharedPicture))) {
            reason="current-physical-bar-context-changed";return false;
        }
        const auto encodedBar=[&](int y) {
            const int sourceY=fresh->sourceRows[size_t(y)];
            return physicalBarPolicy?(sourceY<current.sharedPicture.bounds.top ||
                sourceY>=current.sharedPicture.bounds.bottom):(y<top || y>=bottom);
        };
        m_currentPixels.resize(area);m_currentSampled.resize(area);
        std::fill(m_currentSampled.begin(),m_currentSampled.end(),uint8_t(0));
        auto& pixels=m_currentPixels;auto& sampled=m_currentSampled;
        constexpr uint8_t CurrentSampleRead=1,CurrentAcceptedGlyphVerified=2;
        size_t count=0;
        const auto read=[&](int x,int y,AnalysisLumaSample& value)->bool {
            if(x<0 || y<0 || x>=fresh->width || y>=fresh->height)return false;
            const size_t at=size_t(y)*fresh->width+x;
            if(!sampled[at]) {
                if(count>=CurrentInkSampleBudget)return false;
                const int sourceX=x*fresh->step,sourceY=fresh->sourceRows[size_t(y)];
                const bool reusedFootprintSample=m_currentFootprintRequired && currentFootprint &&
                    currentFootprint->TryGetVerifiedCurrentSampleForBoundReceipt(sourceX,sourceY,pixels[at]);
                if(!reusedFootprintSample && !sampler.Sample(sourceX,sourceY,pixels[at]))return false;
                sampled[at]=CurrentSampleRead;++count;
                m_diagnostics.currentBandSamplesChecked=count;
                if(encodedBar(y))++m_diagnostics.barVisibilitySamplesChecked;
                if(pixels[at].luma<=fresh->blackLimit)
                    fresh->blackBacking[at/64]|=uint64_t{1}<<(at%64);
            }
            value=pixels[at];return true;
        };
        if(!m_currentFootprintRequired && !presentationTemplateProof) {
            for(int y=0;y<fresh->height;++y)if(y<topEnd || y>=bottomStart)
                for(int x=0;x<fresh->width;++x) {
                    AnalysisLumaSample unused;if(!read(x,y,unused)) {
                        reason=count>=CurrentInkSampleBudget?"current-ink-sample-budget":"current-ink-sample-failed";return false;
                    }
                }
        }
        for(const auto& backing:m_backingSamples) {
            if(m_currentFootprintRequired && currentFootprint &&
                !currentFootprint->ContainsVerifiedFootprintSample(
                    backing.x*fresh->step,fresh->sourceRows[size_t(backing.y)]))continue;
            AnalysisLumaSample value;
            if(!read(backing.x,backing.y,value)) {
                reason=count>=CurrentInkSampleBudget?"current-ink-sample-budget":"current-backing-sample-failed";return false;
            }
            ++m_diagnostics.backingSamplesChecked;
            const size_t at=size_t(backing.y)*fresh->width+backing.x;
            const bool black=value.luma<=fresh->blackLimit;
            if(black!=backing.black && !SubtitleCadenceGuard::EncodingEquivalent(
                backing.value,value)) {
                m_diagnostics.glyphMismatchX=backing.x*fresh->step;
                m_diagnostics.glyphMismatchY=fresh->sourceRows[size_t(backing.y)];
                m_diagnostics.currentPixelFailure=CurrentPixelVerificationFailure::PixelMismatch;
                reason="current-black-backing-changed";return false;
            }
            if(black)fresh->blackBacking[at/64]|=uint64_t{1}<<(at%64);
            else fresh->blackBacking[at/64]&=~(uint64_t{1}<<(at%64));
        }
        for(const auto& known:m_knownInkSamples) {
            if(m_currentFootprintRequired && currentFootprint &&
                !currentFootprint->ContainsVerifiedFootprintSample(
                    known.x*fresh->step,fresh->sourceRows[size_t(known.y)]))continue;
            AnalysisLumaSample value;
            if(!read(known.x,known.y,value)) {
                reason=count>=CurrentInkSampleBudget?"current-ink-sample-budget":"current-known-ink-sample-failed";return false;
            }
            if(std::abs(int(value.luma)-known.value.luma)>8 ||
                std::abs(int(value.chromaU)-known.value.chromaU)>8 ||
                std::abs(int(value.chromaV)-known.value.chromaV)>8) {
                m_diagnostics.glyphMismatchX=known.x*fresh->step;
                m_diagnostics.glyphMismatchY=fresh->sourceRows[size_t(known.y)];
                m_diagnostics.currentPixelFailure=CurrentPixelVerificationFailure::PixelMismatch;
                reason="current-known-ink-changed";return false;
            }
        }
        const size_t bandCells=CurrentBandCellCount(fresh->width,fresh->height,topEnd,bottomStart);
        if(!m_currentFootprintRequired && !presentationTemplateProof &&
            (m_contrastSeedWidth!=fresh->width || m_contrastSeedHeight!=fresh->height ||
            m_contrastSeedTopEnd!=topEnd || m_contrastSeedBottomStart!=bottomStart ||
            m_contrastSeedPictureTop!=top || m_contrastSeedPictureBottom!=bottom ||
            m_contrastSeedPixels.size()!=bandCells || m_contrastSeedDirections.size()!=bandCells)) {
            reason="current-contrast-coverage-changed";return false;
        }
        const int radius=(std::max)(3,fresh->height/180);
        for(int y=0;y<fresh->height;++y)if(y<topEnd || y>=bottomStart)for(int x=1;x<fresh->width-1;++x) {
            if(presentationTemplateProof && !m_currentFootprintRequired && !encodedBar(y))continue;
            const bool inNativeFootprint=m_currentFootprintRequired && currentFootprint &&
                currentFootprint->ContainsVerifiedFootprintSample(x*fresh->step,fresh->sourceRows[size_t(y)]);
            if(m_currentFootprintRequired && !encodedBar(y) && !inNativeFootprint)continue;
            AnalysisLumaSample point;
            if(!read(x,y,point)) {reason=count>=CurrentInkSampleBudget?"current-ink-sample-budget":"current-ink-sample-failed";return false;}
            if(point.luma<fresh->inkFloor || std::abs(int(point.chromaU)-fresh->inkU)>80 ||
                std::abs(int(point.chromaV)-fresh->inkV)>80)continue;
            bool raw=encodedBar(y);
            if(!raw) {
                const int x0=(std::max)(0,x-radius),x1=(std::min)(fresh->width-1,x+radius);
                const int y0=(std::max)(y<topEnd?0:bottomStart,y-radius);
                const int y1=(std::min)(y<topEnd?topEnd-1:fresh->height-1,y+radius);
                int dark=point.luma;
                for(int xx=x0;xx<=x1;++xx) {AnalysisLumaSample n;if(!read(xx,y,n)) {
                    reason=count>=CurrentInkSampleBudget?"current-ink-sample-budget":"current-ink-sample-failed";return false;}
                    dark=(std::min)(dark,int(n.luma));}
                for(int yy=y0;yy<=y1;++yy) {AnalysisLumaSample n;if(!read(x,yy,n)) {
                    reason=count>=CurrentInkSampleBudget?"current-ink-sample-budget":"current-ink-sample-failed";return false;}
                    dark=(std::min)(dark,int(n.luma));}
                raw=int(point.luma)-dark>=48; // Conservative lower bound; false positives reject rather than acquire ink.
            }
            if(!raw)continue;
            const size_t at=size_t(y)*fresh->width+x;const uint64_t bit=uint64_t{1}<<(at%64);
            const bool equivalentFootprintSample=m_currentFootprintRequired && inNativeFootprint &&
                currentFootprint->VerifiedFootprintSampleIsEncodingEquivalent(x*fresh->step,fresh->sourceRows[size_t(y)]);
            if(!oldInk->Get(x,y) && (!m_currentFootprintRequired || inNativeFootprint) && !equivalentFootprintSample) {
                m_diagnostics.glyphMismatchX=x*fresh->step;
                m_diagnostics.glyphMismatchY=fresh->sourceRows[size_t(y)];
                m_diagnostics.currentPixelFailure=CurrentPixelVerificationFailure::PixelMismatch;
                reason="new-current-raw-ink";return false;
            }
            fresh->rawInk[at/64]|=bit;
        }
        for(const auto& sample:m_ownedSamples) {
            if(m_currentFootprintRequired && currentFootprint &&
                !currentFootprint->ContainsVerifiedFootprintSample(
                    sample.x*fresh->step,fresh->sourceRows[size_t(sample.y)])) {
                reason="current-owned-glyph-protected";return false;
            }
            AnalysisLumaSample now;
            if(!read(sample.x,sample.y,now)) {
                reason=count>=CurrentInkSampleBudget?"current-ink-sample-budget":"current-glyph-sample-failed";return false;
            }
            ++m_diagnostics.glyphSamplesChecked;
            if(!SubtitleCadenceGuard::EncodingEquivalent(sample.value,now)) {
                m_diagnostics.glyphMismatchX=sample.x*fresh->step;
                m_diagnostics.glyphMismatchY=fresh->sourceRows[size_t(sample.y)];
                m_diagnostics.currentPixelFailure=CurrentPixelVerificationFailure::PixelMismatch;
                reason="current-owned-glyph-changed";return false;
            }
            const size_t at=size_t(sample.y)*fresh->width+sample.x;
            sampled[at]|=CurrentAcceptedGlyphVerified;
            // Exact current pixels at previously detector-owned positions are
            // valid composition inputs even when a crowded caption row falls
            // outside this source's current subtitle search bands. This only
            // refreshes partial output ink; rawEvidenceComplete stays false.
            // The current-source receipt proves this old glyph location,
            // but threshold noise must not invent current palette bits. The
            // immutable prepared operation still uses the current texture.
            const bool currentPalette=now.luma>=fresh->inkFloor &&
                std::abs(int(now.chromaU)-fresh->inkU)<=80 && std::abs(int(now.chromaV)-fresh->inkV)<=80;
            if(currentPalette) {
                const uint64_t bit=uint64_t{1}<<(at%64);
                fresh->rawInk[at/64]|=bit;
                fresh->ownedInk[at/64]|=bit;
            }
        }
        if(m_currentFootprintRequired || presentationTemplateProof) {
            currentPixelPhase.Stop();
            PhaseWallClock encodedBarPhase(m_diagnostics.encodedBarVetoMs);
            if(!CurrentBandCellCount(fresh->width,fresh->height,topEnd,bottomStart)) {
                reason="new-current-bar-ink";return false;
            }
            // The earlier bar-only sampling pass is fused here with the
            // nonblack-bar veto. Current owned bits are populated only after
            // their exact YUV checks above, so the same old exception remains
            // limited to pixels proven as current owned glyphs.
            for(int y=0;y<fresh->height;++y)if(InCurrentBand(y,fresh->height,topEnd,bottomStart) &&
                encodedBar(y))
                for(int x=0;x<fresh->width;++x) {
                    AnalysisLumaSample sample;
                    if(!read(x,y,sample)) {
                        reason=count>=CurrentInkSampleBudget?"current-ink-sample-budget":"current-ink-sample-failed";return false;
                    }
                    const size_t at=size_t(y)*fresh->width+x;
                    // A verified old glyph can cross the palette threshold.
                    // Its current mask stays honest; this separate witness is
                    // set only after checking its actual current source value.
                    if(fresh->Get(x,y,true) || (sampled[at]&CurrentAcceptedGlyphVerified))continue;
                    const bool black=sample.luma<=fresh->blackLimit &&
                        std::abs(int(sample.chromaU)-512)<=48 && std::abs(int(sample.chromaV)-512)<=48;
                    // Only an already-proved erased sample may excuse bar
                    // residue; capture-only membership is not removal proof.
                    const bool equivalentFootprintSample=!black && m_currentFootprintRequired && currentFootprint &&
                        currentFootprint->VerifiedFootprintSampleIsEncodingEquivalent(
                            x*fresh->step,fresh->sourceRows[size_t(y)],true);
                    if(!black && !equivalentFootprintSample) {
                        m_diagnostics.glyphMismatchX=x*fresh->step;
                        m_diagnostics.glyphMismatchY=fresh->sourceRows[size_t(y)];
                        m_diagnostics.currentPixelFailure=CurrentPixelVerificationFailure::PixelMismatch;
                        reason="new-current-bar-ink";return false;
                    }
                }
            encodedBarPhase.Stop();
        } else {
            bool actualPixelMismatch=false;
            if(HasNovelPotentialInk(*oldInk,m_paletteFringe,pixels,m_contrastSeedPixels,m_contrastSeedDirections,
            top,bottom,topEnd,bottomStart,
                m_diagnostics.glyphMismatchX,m_diagnostics.glyphMismatchY,actualPixelMismatch)) {
                if(actualPixelMismatch)m_diagnostics.currentPixelFailure=CurrentPixelVerificationFailure::PixelMismatch;
                reason="new-current-contrast-ink";return false;
            }
        }
        currentInk=std::move(fresh);
        reason="current-owned-cue-verified";return true;
    }
    static bool HasNovelPotentialInk(const SubtitleInkSnapshot& reference,
        const std::vector<uint64_t>& paletteFringe,
        const std::vector<AnalysisLumaSample>& pixels,
        const std::vector<AnalysisLumaSample>& seedPixels,const std::vector<uint8_t>& seedDirections,
        int top,int bottom,int topEnd,int bottomStart,
        int& mismatchX,int& mismatchY,bool& actualPixelMismatch) {
        actualPixelMismatch=false;
        const int width=reference.width,height=reference.height;
        const size_t area=size_t(width)*size_t(height);
        const size_t bandCells=CurrentBandCellCount(width,height,topEnd,bottomStart);
        if(pixels.size()!=area || seedPixels.size()!=bandCells || seedDirections.size()!=bandCells)return true;
        const auto inBand=[&](int y){return InCurrentBand(y,height,topEnd,bottomStart);};
        const auto sameSample=[](const AnalysisLumaSample& a,const AnalysisLumaSample& b) {
            return std::abs(int(a.luma)-int(b.luma))<=8 &&
                std::abs(int(a.chromaU)-int(b.chromaU))<=8 &&
                std::abs(int(a.chromaV)-int(b.chromaV))<=8;
        };
        const auto knownPaletteFringe=[&](int x,int y) {
            if(reference.Get(x,y))return true;
            const size_t at=size_t(y)*width+x;
            if(at/64>=paletteFringe.size() || !(paletteFringe[at/64]&(uint64_t{1}<<(at%64))))return false;
            const auto& point=pixels[size_t(y)*width+x];
            if(point.luma<reference.inkFloor-80 ||
                std::abs(int(point.chromaU)-reference.inkU)>80 ||
                std::abs(int(point.chromaV)-reference.inkV)>80)return false;
            return true;
        };
        constexpr int neighborX[4]={-1,1,0,0},neighborY[4]={0,0,-1,1};
        for(int y=0;y<height;++y)if(inBand(y))for(int x=1;x<width-1;++x) {
            // Existing raw and accepted-palette fringe cells have their own
            // exact current-source YUV checks. Do not let their changed
            // non-owned neighbors turn a legitimate moving margin into a new
            // subtitle; newly changed neighbors are still tested as centers.
            if(knownPaletteFringe(x,y))continue;
            const auto& point=pixels[size_t(y)*width+x];
            // In encoded bars, any newly observed nonblack sample outside the
            // accepted raw mask may be a differently colored or dimmer cue.
            // Reject it even when a single grid cell cannot form a component.
            if((y<top || y>=bottom) && point.luma>reference.blackLimit) {
                actualPixelMismatch=true;
                mismatchX=x*reference.step;mismatchY=reference.sourceRows[size_t(y)];return true;
            }
            for(int direction=0;direction<4;++direction) {
                const int nx=x+neighborX[direction],ny=y+neighborY[direction];
                if(!inBand(ny))continue;
                const bool pointInPicture=y>=top && y<bottom;
                const bool neighborInPicture=ny>=top && ny<bottom;
                if(pointInPicture!=neighborInPicture)continue;
                const auto& neighbor=pixels[size_t(ny)*width+nx];
                const bool brightLocalInk=int(point.luma)-int(neighbor.luma)>=48;
                const bool alternatePalette=(std::abs(int(point.chromaU)-reference.inkU)>80 ||
                    std::abs(int(point.chromaV)-reference.inkV)>80) &&
                    (std::abs(int(point.chromaU)-int(neighbor.chromaU))>=64 ||
                     std::abs(int(point.chromaV)-int(neighbor.chromaV))>=64);
                if(!brightLocalInk && !alternatePalette)continue;
                const size_t pointAt=CurrentBandIndex(x,y,width,height,topEnd,bottomStart);
                const size_t neighborAt=CurrentBandIndex(nx,ny,width,height,topEnd,bottomStart);
                if(pointAt>=seedPixels.size() || neighborAt>=seedPixels.size() || pointAt>=seedDirections.size()) {
                    mismatchX=x*reference.step;mismatchY=reference.sourceRows[size_t(y)];return true;
                }
                const uint8_t baseline=seedDirections[pointAt];
                const bool sameSamples=sameSample(point,seedPixels[pointAt]) &&
                    sameSample(neighbor,seedPixels[neighborAt]);
                const bool existingBright=!brightLocalInk ||
                    ((baseline&(uint8_t(1u<<direction))) && sameSamples);
                const bool existingAlternate=!alternatePalette ||
                    ((baseline&(uint8_t(1u<<(direction+4)))) && sameSamples);
                if(!existingBright || !existingAlternate) {
                    actualPixelMismatch=true;
                    mismatchX=x*reference.step;mismatchY=reference.sourceRows[size_t(y)];return true;
                }
            }
        }
        return false;
    }
    bool Reject(const char* reason) {
        const auto diagnostics=m_diagnostics;
        const auto certified=m_certifiedSeed;const auto policy=m_certifiedPolicy,continuity=m_certifiedContinuity;
        const auto certificate=m_consumedCertificate;
        Reset();
        m_certifiedSeed=certified;m_certifiedPolicy=policy;m_certifiedContinuity=continuity;
        m_consumedCertificate=certificate;
        m_diagnostics=diagnostics;
        m_reason=reason;
        return false;
    }
    const char* m_reason="reset";
    Diagnostics m_diagnostics;
    struct Sample { int x,y;AnalysisLumaSample value; };
    std::vector<OwnedSample> m_ownedSamples;
    std::vector<BackingSample> m_backingSamples;
    std::vector<KnownInkSample> m_knownInkSamples;
    std::vector<uint64_t> m_paletteFringe;
    // Reused per pending frame. Every sampled mark is reset before reuse, and
    // pixel values are read only after the corresponding mark is set current.
    std::vector<AnalysisLumaSample> m_currentPixels;
    std::vector<uint8_t> m_currentSampled;
    // Compact immutable seed snapshot of every sampled search-band pixel and
    // its four directional baseline contrast classifications. High nibbles
    // store alternate-chroma directions; low nibbles store bright-edge ones.
    std::vector<AnalysisLumaSample> m_contrastSeedPixels;
    std::vector<uint8_t> m_contrastSeedDirections;
    int m_contrastSeedTopEnd=0,m_contrastSeedBottomStart=0;
    int m_contrastSeedPictureTop=0,m_contrastSeedPictureBottom=0;
    int m_contrastSeedWidth=0,m_contrastSeedHeight=0;
    SourceLayout m_sourceLayout{};
    bool m_currentInkProofSeeded=false,m_currentFootprintRequired=false;
    SubtitleBoxPreview m_reference;
    SubtitleAcceptedCueSeedCertificate m_consumedCertificate;
    ActivePictureFrameIdentity m_last,m_certifiedSeed,m_pixelSeedIdentity;
    std::shared_ptr<const SubtitleInkSnapshot> m_pixelSeedInk;
    uint64_t m_certifiedPolicy=0,m_certifiedContinuity=0;
    std::vector<Sample> m_samples;
    double m_elapsed=0;
};

struct SubtitleCurrentCueTemplateDescriptor {
    SubtitleBoxObservation context;
    SubtitleBoxResult cue;
    ActivePictureBounds physicalBase;
    SubtitleCutPasteGeometry operation;
    AnalysisLumaSource sourceLayout;
    SubtitlePreviewMode sourceMode=SubtitlePreviewMode::None;
    int backgroundMode=3;
    uint64_t revision=0;
    bool valid=false;
};

struct SubtitleCurrentCueTaskEntry {
    SubtitleBoxObservation key;
    // Original scan provenance paired with the canonical exact-source proof key.
    SubtitleBoxObservation scanKey;
    SubtitleCurrentCueTemplateDescriptor descriptor;
    std::shared_ptr<SubtitleCurrentCueWorkerState> workerState;
    SubtitleMeasurementWork deferredFullScan;
    SubtitleBoxObservation deferredFullScanKey;
    OwnedScanBacklogWitness deferredWitness;
    uint64_t deferredTimelinePolicy=0;
    SubtitleCurrentCueProofStatus status=SubtitleCurrentCueProofStatus::Queued;
    SubtitleCurrentCueProofReceipt receipt;
    std::shared_ptr<SubtitleCurrentCueProofSlot> completedProof;
    std::shared_ptr<const SubtitleCurrentCueProofPublication> completedPublication;
    // Revisions proving the same exact source share the final consumption token.
    std::shared_ptr<std::atomic_bool> consumed;
    uint64_t revision=0;
    bool seedTask=false,fullScanDue=false,hasDeferredWitness=false;
    bool scanDecisionMade=false,proofVerified=false,semanticRefreshDue=false;
    bool fullScanQueued=false,fallbackQueued=false;
};

struct SubtitleCurrentCueWorkerState {
    SubtitlePendingMeasurementGuard pending;
    SubtitleCadenceGuard footprint;
    SubtitleCurrentCueTemplateDescriptor descriptor;
    // Qualified operation metadata belongs to this cue revision, not a second
    // cue history. The weak cache stamps own no source/background/GPU pixels.
    struct QualifiedEncoding {
        SourcePictureEquivalenceToken token;
        std::shared_ptr<const SubtitleCurrentCueProofReceipt> receipt;
        uint64_t lastUse=0;
        ActivePictureFrameIdentity lastIssued;
    };
    struct QualifiedAssociations {
        std::array<QualifiedEncoding,4> encodings{};
        uint64_t clock=0;
        ActivePictureFrameIdentity lastIssued;
        bool haveIssued=false;
    };
    // Only renderer qualification/issuance updates this immutable metadata via
    // ordered CAS. Worker job bookkeeping cannot contend with these reads.
    std::shared_ptr<const QualifiedAssociations> qualifiedAssociations;
    std::atomic_bool semanticRefreshInFlight{false};
    std::atomic_bool needsFullScanBurst{false};
    // Ordinary scan admission has no presentation-mode fields in its key.
    // Disable cadence suppression when a same-context proof request shows the
    // current interpretation no longer matches this template.
    bool cadenceRoutingEligible=true;
    uint64_t lastSemanticRefreshSequence=0,lastSemanticScanSequence=0;
    uint64_t semanticRefreshIntervalFrames=6;
};

struct SubtitleCurrentCueRefreshLease {
    std::shared_ptr<SubtitleCurrentCueWorkerState> state;
    ~SubtitleCurrentCueRefreshLease() {
        if(state)state->semanticRefreshInFlight.store(false,std::memory_order_release);
    }
};

namespace SubtitleCurrentCueWorkerDetail {
inline bool SameRect(const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
    return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
}
inline bool SameBounds(const ActivePictureBounds& a,const ActivePictureBounds& b) {
    return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom &&
        a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight;
}
inline bool SameInterpretationContext(const SubtitleBoxObservation& a,const SubtitleBoxObservation& b) {
    return a.identity.transportGeneration==b.identity.transportGeneration &&
        a.identity.sourceFormatGeneration==b.identity.sourceFormatGeneration &&
        a.identity.analysisDomainKey==b.identity.analysisDomainKey &&
        a.identity.viewportGeneration==b.identity.viewportGeneration &&
        a.identity.rendererGeneration==b.identity.rendererGeneration &&
        a.width==b.width && a.height==b.height && a.nearBarDistance==b.nearBarDistance &&
        a.scriptHint==b.scriptHint && a.optimizationMode==b.optimizationMode &&
        a.policyGeneration==b.policyGeneration && a.continuityGeneration==b.continuityGeneration;
}
inline bool SameContext(const SubtitleBoxObservation& a,const SubtitleBoxObservation& b) {
    return SameInterpretationContext(a,b) && a.sharedPicture.Matches(b.sharedPicture);
}
// A scan's optional geometry hint may be absent. A present current hint must
// agree with the already verified presentation base; it is never rewritten.
inline bool CompatibleScanBase(const SubtitleBoxObservation& key,const ActivePictureBounds& base) {
    const auto& hint=key.sharedPicture;
    const bool hasBounds=hint.bounds.top<hint.bounds.bottom && hint.bounds.left<hint.bounds.right;
    return (!hasBounds || (hint.AvailableFor(key.identity,key.width,key.height) && SameBounds(hint.bounds,base))) &&
        (!hint.inspectionCandidate.valid ||
         (hint.inspectionCandidate.AvailableFor(key.identity,key.width,key.height,key.policyGeneration,key.continuityGeneration) &&
          SameBounds(hint.inspectionCandidate.bounds,base)));
}
// Scheduling alias only: exact source/settings and an already successful
// current-pixel receipt. This grants neither raw scan nor native authority.
inline bool MatchesVerifiedScanSource(const SubtitleCurrentCueTaskEntry& entry,const SubtitleBoxObservation& key) {
    return entry.proofVerified && entry.receipt.Valid() && entry.receipt.SourceMode()!=SubtitlePreviewMode::None &&
        SameAnalysisFrameIdentity(entry.key.identity,key.identity) && SameInterpretationContext(entry.key,key) &&
        entry.key.analysisStride==key.analysisStride &&
        !key.discontinuity && !key.pendingRefresh && !key.analysisRefresh && !key.cadenceSkipped &&
        !key.glyphLocalTop && !key.glyphLocalBottom && CompatibleScanBase(key,entry.descriptor.physicalBase);
}
inline bool SameSourceLayout(const AnalysisLumaSource& a,const AnalysisLumaSource& b) {
    return a.width==b.width && a.height==b.height && a.format==b.format &&
        a.encoding==b.encoding && a.colorspace==b.colorspace && a.range==b.range &&
        a.rowBytes==b.rowBytes && a.chromaRowBytes==b.chromaRowBytes &&
        a.generation==b.generation && a.rgbBitAlignment==b.rgbBitAlignment;
}
inline bool SameCue(const SubtitleBoxResult& a,const SubtitleBoxResult& b) {
    if(a.cue!=b.cue || a.lineCount!=b.lineCount || a.detected!=b.detected ||
        a.held!=b.held || a.confirmationPending!=b.confirmationPending || a.revised!=b.revised ||
        a.workLimit!=b.workLimit || a.signature!=b.signature || a.anchorSignature!=b.anchorSignature ||
        a.lineSignatures!=b.lineSignatures || a.capturePanelMeasured!=b.capturePanelMeasured ||
        !SameRect(a.bounds,b.bounds) || !SameRect(a.anchor,b.anchor) ||
        !SameRect(a.sourcePanel,b.sourcePanel) || !SameRect(a.provenSourcePanel,b.provenSourcePanel) ||
        !SameRect(a.capturePanel,b.capturePanel))return false;
    for(size_t i=0;i<a.lineBounds.size();++i)
        if(!SameRect(a.lineBounds[i],b.lineBounds[i]) || !SameRect(a.linePanels[i],b.linePanels[i]) ||
            !SameRect(a.provenLinePanels[i],b.provenLinePanels[i]) ||
            !SameRect(a.lineCapturePanels[i],b.lineCapturePanels[i]))return false;
    return true;
}
inline bool CompleteSemanticScanConfirmsCue(const SubtitleBoxObservation& result,
    const SubtitleBoxObservation& key,const SubtitleBoxResult& accepted) {
    const auto& bars=result.barEvidence;
    if(!SameAnalysisFrameIdentity(result.identity,key.identity) ||
        result.policyGeneration!=key.policyGeneration || result.continuityGeneration!=key.continuityGeneration ||
        result.width!=key.width || result.height!=key.height || result.analysisStride!=key.analysisStride ||
        !result.analyzed || result.discontinuity || result.pendingRefresh || result.analysisRefresh ||
        result.cadenceSkipped || result.glyphLocalTop || result.glyphLocalBottom ||
        !bars.available ||
        result.pictureTop!=bars.top || result.pictureBottom!=bars.bottom ||
        !result.text.detected || result.text.held || result.text.confirmationPending || result.text.workLimit ||
        !result.text.topSearchComplete || !result.text.bottomSearchComplete ||
        !result.ink || !result.ink->rawEvidenceComplete || !result.ink->paletteValid)return false;
    auto candidate=result.text;
    // Raw scans do not own tracker cue IDs. Compare current detected geometry
    // and signatures against the accepted cue without deriving identity from
    // an unconfirmed raw token.
    candidate.cue=accepted.cue;
    return SameCue(candidate,accepted);
}
inline bool CompleteSemanticScanConfirmsVerifiedCue(const SubtitleBoxObservation& result,
    const SubtitleBoxObservation& key,const SubtitleCurrentCueProofReceipt& receipt) {
    if(!receipt.Valid() || receipt.SourceMode()==SubtitlePreviewMode::None ||
        !SameAnalysisFrameIdentity(receipt.CurrentKey().identity,result.identity) ||
        !SameAnalysisFrameIdentity(result.identity,key.identity) ||
        !SameInterpretationContext(result,key) || result.analysisStride!=key.analysisStride ||
        !result.analyzed || result.discontinuity || result.pendingRefresh || result.analysisRefresh ||
        result.cadenceSkipped || result.glyphLocalTop || result.glyphLocalBottom ||
        !result.text.topSearchComplete || !result.text.bottomSearchComplete ||
        !result.ink || !result.ink->rawEvidenceComplete || !result.ink->paletteValid ||
        !CompatibleScanBase(key,receipt.PhysicalBase()) || !CompatibleScanBase(result,receipt.PhysicalBase()) ||
        !CurrentSubtitleCueFitsVerifiedOperation(result.text,receipt.AcceptedCue(),receipt.Operation(),
            result.ink->step,&receipt.VerifiedSemanticLineBounds()))return false;
    const auto& ink=*result.ink;
    if(ink.step<1 || ink.step>8 || ink.width<=0 || ink.height<=0 ||
        ink.sourceRows.size()!=size_t(ink.height) ||
        ink.ownedInk.size()!=(size_t(ink.width)*size_t(ink.height)+63)/64)return false;
    // Skip empty mask words; do not scan the whole raster under the queue lock.
    for(size_t word=0;word<ink.ownedInk.size();++word) {
        uint64_t bits=ink.ownedInk[word];
        for(unsigned bit=0;bits;++bit,bits>>=1)if(bits&1) {
            const size_t cell=word*64+bit;
            if(cell>=size_t(ink.width)*size_t(ink.height))return false;
            const int px=int(cell%size_t(ink.width))*ink.step;
            const int py=ink.sourceRows[cell/size_t(ink.width)];bool captured=false;
            for(const auto& capture:receipt.Operation().glyphLines)
                captured|=capture.Valid() && px>=capture.left && px<capture.right && py>=capture.top && py<capture.bottom;
            if(!captured)return false;
        }
    }
    return true;
}
inline bool SameOperation(const SubtitleCutPasteGeometry& a,const SubtitleCutPasteGeometry& b) {
    if(!SameRect(a.source,b.source) || !SameRect(a.destination,b.destination) ||
        !SameRect(a.content,b.content) || !SameRect(a.generatedCleanup,b.generatedCleanup) ||
        !SameRect(a.pictureCapture,b.pictureCapture) || !SameRect(a.pendingProtectedCleanup,b.pendingProtectedCleanup) ||
        !SameRect(a.generatedCleanupExclusion,b.generatedCleanupExclusion) ||
        !SameRect(a.placementLimits,b.placementLimits) || !SameRect(a.layoutContent,b.layoutContent) ||
        !SameRect(a.layoutCoverage,b.layoutCoverage) || !SameRect(a.cleanupLimit,b.cleanupLimit) ||
        a.generatedCleanupRowCount!=b.generatedCleanupRowCount || a.glyphScale!=b.glyphScale ||
        a.glyphTranslateX!=b.glyphTranslateX || a.glyphTranslateY!=b.glyphTranslateY ||
        a.centeredGlyphMapping!=b.centeredGlyphMapping || a.roundedCorners!=b.roundedCorners ||
        a.extendToBar!=b.extendToBar || a.pictureIntrusion!=b.pictureIntrusion ||
        a.backgroundGuarded!=b.backgroundGuarded || a.pictureTop!=b.pictureTop ||
        a.pictureBottom!=b.pictureBottom || a.displayPictureLeft!=b.displayPictureLeft ||
        a.displayPictureTop!=b.displayPictureTop || a.displayPictureRight!=b.displayPictureRight ||
        a.displayPictureBottom!=b.displayPictureBottom || a.valid!=b.valid || a.fromTopBar!=b.fromTopBar)
        return false;
    for(size_t i=0;i<a.glyphLines.size();++i)if(!SameRect(a.glyphLines[i],b.glyphLines[i]))return false;
    for(size_t i=0;i<a.generatedCleanupRows.size();++i)
        if(!SameRect(a.generatedCleanupRows[i],b.generatedCleanupRows[i]))return false;
    return true;
}
inline SubtitleCurrentCueTemplateDescriptor Describe(const SubtitleCurrentCueTemplateInput& input) {
    SubtitleCurrentCueTemplateDescriptor out;
    out.context=input.observation;out.context.text={};out.context.ink.reset();
    out.context.subtitleArProof={};out.context.subtitleArDarkness={};out.context.barEvidence={};
    out.context.barStripClearance={};out.context.hasBarTrackingReference=false;
    out.context.barTrackingReferenceIdentity={};out.context.incomingBarTrackingReference={};
    out.context.sharedProbeAnchor={};
    out.cue=input.acceptedCue;out.physicalBase=input.physicalBase;out.operation=input.composedOperation;
    out.sourceLayout=input.source;out.sourceLayout.data=nullptr;out.sourceLayout.dataBytes=0;
    out.sourceLayout.backingOwner.reset();out.sourceMode=input.sourceMode;
    out.backgroundMode=input.backgroundMode;out.valid=true;return out;
}
inline bool SameDescriptor(const SubtitleCurrentCueTemplateDescriptor& a,
    const SubtitleCurrentCueTemplateDescriptor& b) {
    return a.valid && b.valid && SameContext(a.context,b.context) && SameCue(a.cue,b.cue) &&
        SameBounds(a.physicalBase,b.physicalBase) && SameOperation(a.operation,b.operation) &&
        SameSourceLayout(a.sourceLayout,b.sourceLayout) && a.sourceMode==b.sourceMode &&
        a.backgroundMode==b.backgroundMode;
}
// A later cue may replace the working template while an earlier exact source
// is still renderable. Its completed current-pixel proof retains its own cue
// and operation; only interpretation/base changes invalidate that proof here.
// The presentation adapter separately validates current semantic compatibility.
inline bool SameProofInterpretation(const SubtitleBoxObservation& context,
    const ActivePictureBounds& base,const AnalysisLumaSource& layout,
    SubtitlePreviewMode mode,int background,const SubtitleCurrentCueTemplateDescriptor& current) {
    // Working validity is separate from already completed exact-source proof.
    return SameContext(context,current.context) && SameBounds(base,current.physicalBase) &&
        SameSourceLayout(layout,current.sourceLayout) && mode==current.sourceMode && background==current.backgroundMode;
}
inline bool SameProofInterpretation(const SubtitleCurrentCueTemplateDescriptor& a,
    const SubtitleCurrentCueTemplateDescriptor& b) {
    return SameProofInterpretation(a.context,a.physicalBase,a.sourceLayout,a.sourceMode,a.backgroundMode,b);
}
inline bool CurrentMatches(const SubtitleCurrentCueTemplateDescriptor& seed,
    const SubtitleBoxObservation& current,const ActivePictureBounds& base,
    const AnalysisLumaSource& source,SubtitlePreviewMode mode,int backgroundMode) {
    return seed.valid && SameContext(seed.context,current) && SameBounds(seed.physicalBase,base) &&
        source.IsValid() && SameSourceLayout(seed.sourceLayout,source) &&
        mode==seed.sourceMode && backgroundMode==seed.backgroundMode &&
        current.sharedPicture.AvailableFor(current.identity,current.width,current.height) &&
        SameBounds(current.sharedPicture.bounds,base);
}
inline bool InWindow(const ActivePictureFrameIdentity& head,uint64_t tail,
    const ActivePictureFrameIdentity& current) {
    return head.transportGeneration && current.transportGeneration==head.transportGeneration &&
        current.sourceFormatGeneration==head.sourceFormatGeneration &&
        current.viewportGeneration==head.viewportGeneration && current.rendererGeneration==head.rendererGeneration &&
        (!head.analysisDomainKey || current.analysisDomainKey==head.analysisDomainKey) &&
        current.acceptedSequence>=head.acceptedSequence && current.acceptedSequence<=tail &&
        current.acceptedSequence-head.acceptedSequence<=ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES+2;
}
inline bool SameExactTaskKey(const SubtitleBoxObservation& a,const SubtitleBoxObservation& b) {
    return SameAnalysisFrameIdentity(a.identity,b.identity) && a.policyGeneration==b.policyGeneration &&
        a.continuityGeneration==b.continuityGeneration && a.width==b.width && a.height==b.height &&
        a.nearBarDistance==b.nearBarDistance && a.optimizationMode==b.optimizationMode &&
        a.scriptHint==b.scriptHint && a.sharedPicture.Matches(b.sharedPicture);
}

inline uint64_t ThreadId() {
#ifdef _WIN32
    return static_cast<uint64_t>(::GetCurrentThreadId());
#else
    return static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}
}

inline bool SubtitleMeasurementWorker::HasCurrentCueProofContextLocked() const {
    return m_currentCueState && m_currentCueState->descriptor.revision>=
        m_currentCueMinimumRevision->load(std::memory_order_acquire);
}

inline void SubtitleMeasurementWorker::PublishCurrentCueReadViewLocked() {
    std::shared_ptr<const SubtitleCurrentCueReadView> next;
    if(!m_stopping && HasCurrentCueProofContextLocked()) {
        const auto old=std::atomic_load(&m_currentCueReadView);
        if(old && old->state==m_currentCueState && old->revision==m_currentCueState->descriptor.revision &&
            old->descriptor->valid==m_currentCueState->descriptor.valid)return;
        auto view=std::make_shared<SubtitleCurrentCueReadView>();
        view->state=m_currentCueState;view->revision=m_currentCueState->descriptor.revision;
        view->descriptor=std::make_shared<const SubtitleCurrentCueTemplateDescriptor>(m_currentCueState->descriptor);
        next=std::move(view);
    }
    std::atomic_store(&m_currentCueReadView,std::move(next));
}
inline bool SubtitleMeasurementWorker::CurrentCueReadViewAccepts(
    const std::shared_ptr<const SubtitleCurrentCueReadView>& expected,
    const SubtitleBoxObservation& key,uint64_t revision) const {
    const auto current=std::atomic_load(&m_currentCueReadView);
    return expected && current && current->descriptor && current->descriptor->valid &&
        current->state==expected->state && current->revision==revision &&
        revision>=m_currentCueMinimumRevision->load(std::memory_order_acquire) &&
        InCurrentCueWindow(key.identity);
}

inline bool SubtitleMeasurementWorker::InCurrentCueWindow(const ActivePictureFrameIdentity& identity) const {
    const auto window=std::atomic_load(&m_currentCueSourceWindow);
    return window && SubtitleCurrentCueWorkerDetail::InWindow(window->head,window->tail,identity);
}
inline void SubtitleMeasurementWorker::PublishCurrentCueWindow(const ActivePictureFrameIdentity& head,
    uint64_t tailSequence,bool advanceHead) {
    if(!head.transportGeneration)return;
    const uint64_t lookahead=ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES+2;
    auto before=std::atomic_load(&m_currentCueSourceWindow);
    for(uint64_t attempt=0;attempt<lookahead+3;++attempt) {
        const bool haveHead=before && before->head.transportGeneration;
        const auto& old=haveHead?before->head:head;
        if(haveHead && head.transportGeneration<old.transportGeneration)return;
        const bool sameContext=haveHead && head.transportGeneration==old.transportGeneration &&
            head.sourceFormatGeneration==old.sourceFormatGeneration && head.viewportGeneration==old.viewportGeneration &&
            head.rendererGeneration==old.rendererGeneration &&
            (!head.analysisDomainKey || !old.analysisDomainKey || head.analysisDomainKey==old.analysisDomainKey);
        const bool reset=!haveHead || head.transportGeneration>old.transportGeneration || !sameContext;
        if(sameContext && head.acceptedSequence==old.acceptedSequence && !SameAnalysisFrameIdentity(head,old))return;
        if(reset && haveHead && head.transportGeneration==old.transportGeneration &&
            head.acceptedSequence<old.acceptedSequence && head.sourceFormatGeneration<=old.sourceFormatGeneration &&
            head.viewportGeneration<=old.viewportGeneration && head.rendererGeneration<=old.rendererGeneration)return;
        if(!reset && advanceHead && head.acceptedSequence<old.acceptedSequence)return;
        auto next=std::make_shared<SubtitleCurrentCueSourceWindow>();
        next->head=(reset || advanceHead)?head:old;
        const uint64_t first=next->head.acceptedSequence;
        const uint64_t maxTail=first>UINT64_MAX-lookahead?UINT64_MAX:first+lookahead;
        next->tail=(reset || advanceHead)?(std::max)(first,(std::min)(tailSequence,maxTail)):
            (std::max)(before->tail,(std::min)(tailSequence,maxTail));
        if(before && SameAnalysisFrameIdentity(before->head,next->head) && before->tail==next->tail)return;
        std::shared_ptr<const SubtitleCurrentCueSourceWindow> publish=std::move(next);
        if(std::atomic_compare_exchange_strong(&m_currentCueSourceWindow,&before,publish))return;
        // A failed CAS observed another monotonic publication, never a worker
        // lock. Tail-only contenders can advance only within bounded lookahead;
        // a newer render head/context makes an older request ineligible above.
        // An adversarial context churn cannot create unbounded render work.
    }
}
inline void SubtitleMeasurementWorker::InvalidateCurrentCueProofContext() {
    const uint64_t next=m_nextCurrentCueRevision.load(std::memory_order_acquire)+1;
    auto floor=m_currentCueMinimumRevision->load(std::memory_order_acquire);
    while(floor<next && !m_currentCueMinimumRevision->compare_exchange_weak(floor,next,std::memory_order_acq_rel)){}
    // Do not clear a concurrently published NEW revision. Existing receipt/view
    // checks observe this floor; no job mutex or worker-owned destruction here.
    m_changed.notify_all();
}

inline SubtitleCurrentCueFullScanRoute SubtitleMeasurementWorker::RouteCurrentCueFullScan(
    const SubtitleBoxObservation& key,Work& work,const OwnedScanBacklogWitness* ownedWitness,
    uint64_t timelinePolicy,bool nonblocking,
    std::shared_ptr<SubtitleCurrentCueRefreshLease>& refreshLease) {
    using namespace SubtitleCurrentCueWorkerDetail;
    std::unique_lock<std::mutex> lock(m_mutex,std::defer_lock);
    if(nonblocking) {if(!lock.try_lock())return SubtitleCurrentCueFullScanRoute::Suppress;}
    else lock.lock();
    if(m_stopping)return SubtitleCurrentCueFullScanRoute::Suppress;

    const auto& state=m_currentCueState;
    const auto minimumRevision=m_currentCueMinimumRevision->load(std::memory_order_acquire);
    // A renderer mode/background transition is not represented in the ordinary
    // analysis key. Once the typed proof path has observed such a transition,
    // let complete current scans acquire the replacement template instead of
    // applying the old template's semantic refresh throttle.
    if(HasCurrentCueProofContextLocked() && state->descriptor.valid && !state->cadenceRoutingEligible &&
        SameContext(state->descriptor.context,key))
        return SubtitleCurrentCueFullScanRoute::Enqueue;

    bool matchedVerifiedCurrentTask=false;
    for(auto i=m_currentCueTasks.rbegin();i!=m_currentCueTasks.rend();++i) {
        auto& entry=**i;
        if(entry.seedTask || entry.revision<minimumRevision ||
            (!SameExactTaskKey(entry.scanKey,key) && !MatchesVerifiedScanSource(entry,key)))continue;
        if(entry.status==SubtitleCurrentCueProofStatus::Failed ||
            entry.status==SubtitleCurrentCueProofStatus::Expired)
            return SubtitleCurrentCueFullScanRoute::Enqueue;
        if(entry.fullScanQueued)return SubtitleCurrentCueFullScanRoute::Suppress;
        if(!entry.scanDecisionMade) {
            if(work && !entry.deferredFullScan) {
                entry.deferredFullScan=std::move(work);entry.fullScanDue=true;
                entry.deferredFullScanKey=key;entry.deferredFullScanKey.ink.reset();
                if(ownedWitness) {entry.deferredWitness=*ownedWitness;entry.hasDeferredWitness=true;}
                entry.deferredTimelinePolicy=timelinePolicy;
            }
            return SubtitleCurrentCueFullScanRoute::Deferred;
        }
        if(!entry.proofVerified)return SubtitleCurrentCueFullScanRoute::Enqueue;
        matchedVerifiedCurrentTask=true;
        break;
    }

    // Semantic refresh cadence is optional only after this exact source has
    // verified current pixels. A template alone cannot replace a missing task
    // or suppress the ordinary scan needed before this source reaches render.
    if(!matchedVerifiedCurrentTask)return SubtitleCurrentCueFullScanRoute::Enqueue;

    if(!HasCurrentCueProofContextLocked() || !state->descriptor.valid || !state->cadenceRoutingEligible || !SameInterpretationContext(state->descriptor.context,key) ||
        key.identity.acceptedSequence<=state->descriptor.context.identity.acceptedSequence ||
        key.discontinuity || key.cadenceSkipped || (key.analysisStride!=1 && key.analysisStride!=2) ||
        key.pendingRefresh || key.analysisRefresh || key.glyphLocalTop || key.glyphLocalBottom)
        return SubtitleCurrentCueFullScanRoute::Enqueue;

    const uint64_t sequence=key.identity.acceptedSequence;
    // Even a burst requests another SOURCE, not duplicate grouping of this
    // already verified source after its first refresh callback completed.
    if(state->descriptor.sourceMode!=SubtitlePreviewMode::None &&
        sequence==state->lastSemanticRefreshSequence)return SubtitleCurrentCueFullScanRoute::Suppress;
    const bool burst=state->needsFullScanBurst.load(std::memory_order_acquire);
    const uint64_t last=state->lastSemanticRefreshSequence;
    const uint64_t interval=(std::max)(uint64_t(1),state->semanticRefreshIntervalFrames);
    const bool intervalReached=sequence>last && sequence-last>=interval;
    if(!burst && !intervalReached)return SubtitleCurrentCueFullScanRoute::Suppress;
    bool expected=false;
    if(!state->semanticRefreshInFlight.compare_exchange_strong(expected,true,std::memory_order_acq_rel))
        return SubtitleCurrentCueFullScanRoute::Suppress;
    refreshLease=std::make_shared<SubtitleCurrentCueRefreshLease>();
    refreshLease->state=state;
    return SubtitleCurrentCueFullScanRoute::Enqueue;
}

// SubmitCore calls this while holding m_mutex after inserting the refresh job.
// A failed admission leaves the previous deadline untouched.
inline void SubtitleMeasurementWorker::CommitCurrentCueRefreshReservation(
    const std::shared_ptr<SubtitleCurrentCueRefreshLease>& refreshLease,uint64_t sequence) {
    if(refreshLease && refreshLease->state && sequence>
        refreshLease->state->lastSemanticRefreshSequence)
        refreshLease->state->lastSemanticRefreshSequence=sequence;
}

inline void SubtitleMeasurementWorker::ObserveCurrentCueSemanticScan(
    const SubtitleBoxObservation& key,const SubtitleBoxObservation& result) {
    using namespace SubtitleCurrentCueWorkerDetail;
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto& state=m_currentCueState;
    if(!HasCurrentCueProofContextLocked() || !state->descriptor.valid ||
        !SameInterpretationContext(state->descriptor.context,key) ||
        key.identity.transportGeneration!=state->descriptor.context.identity.transportGeneration ||
        key.identity.acceptedSequence<=state->descriptor.context.identity.acceptedSequence ||
        key.identity.acceptedSequence<=state->lastSemanticScanSequence ||
        !InCurrentCueWindow(key.identity) ||
        !SameAnalysisFrameIdentity(result.identity,key.identity) ||
        result.policyGeneration!=key.policyGeneration ||
        result.continuityGeneration!=key.continuityGeneration)return;
    // Observe ordinary scans too: a scan may already have been queued before
    // this proof job attempted to submit its periodic refresh. If that exact
    // source finds a changed or incomplete cue, keep scheduling the bounded
    // due-source burst until the new cue is accepted and seeds a replacement.
    // The monotonic sequence watermark prevents an older completion from
    // clearing a burst reported by a newer result.
    state->lastSemanticScanSequence=key.identity.acceptedSequence;
    bool confirms=SameContext(state->descriptor.context,key) &&
        CompleteSemanticScanConfirmsCue(result,key,state->descriptor.cue);
    if(!confirms)for(auto i=m_currentCueTasks.rbegin();i!=m_currentCueTasks.rend();++i) {
        const auto& entry=**i;
        if(entry.seedTask || entry.revision!=state->descriptor.revision || !entry.proofVerified ||
            !MatchesVerifiedScanSource(entry,key))continue;
        confirms=CompleteSemanticScanConfirmsVerifiedCue(result,key,entry.receipt);break;
    }
    state->needsFullScanBurst.store(!confirms,std::memory_order_release);
}

namespace SubtitleCurrentCueWorkerDetail {
inline bool SameSourceEpoch(const ActivePictureFrameIdentity& a,const ActivePictureFrameIdentity& b) {
    return a.transportGeneration==b.transportGeneration && a.sourceFormatGeneration==b.sourceFormatGeneration &&
        a.analysisDomainKey==b.analysisDomainKey && a.viewportGeneration==b.viewportGeneration &&
        a.rendererGeneration==b.rendererGeneration;
}
}

__declspec(noinline) inline bool SubtitleMeasurementWorker::AssociateQualifiedSource(
        const SourceAnalysisTicket& ticket,const SourcePictureEquivalenceToken& token,
        const SubtitleCurrentCueProofReceipt& receipt,
        const QualifiedSubtitlePresentationEvidence& qualified,const SubtitleCutPasteGeometry& actualOperation,bool temporalDonorPossible) {
        const auto& key=receipt.CurrentKey();
        const auto& op=receipt.Operation();
        if(temporalDonorPossible || !token.MatchesSource(key.identity) || !receipt.Valid() ||
            receipt.Provenance()!=SubtitleCurrentCueProofProvenance::PresentationOnlyCurrentPixels ||
            !receipt.m_consumed || !receipt.m_consumed->load(std::memory_order_acquire) ||
            !SubtitlePreviewOverridesClassicHandling(receipt.SourceMode()) || !op.valid ||
            !SubtitleCurrentCueWorkerDetail::SameOperation(op,actualOperation) ||
            !qualified.qualified || qualified.status!=QualifiedSubtitlePresentationStatus::Qualified ||
            !qualified.currentCompositionVerified || qualified.currentCompositionMode!=receipt.SourceMode() ||
            !SameAnalysisFrameIdentity(qualified.identity,key.identity) ||
            !SameSubtitlePresentationBounds(qualified.physicalBase,receipt.PhysicalBase()) ||
            qualified.erasedSourceBounds.rasterWidth!=key.width || qualified.erasedSourceBounds.rasterHeight!=key.height ||
            qualified.erasedSourceBounds.left!=op.content.left || qualified.erasedSourceBounds.top!=op.content.top ||
            qualified.erasedSourceBounds.right!=op.content.right || qualified.erasedSourceBounds.bottom!=op.content.bottom)
            return false;
        if(!ticket.MatchesRequestContext(key))return false;
        const auto view=std::atomic_load(&m_currentCueReadView);
        if(receipt.m_minimumRevision!=m_currentCueMinimumRevision ||
            !CurrentCueReadViewAccepts(view,key,receipt.TemplateRevision()))return false;
        const auto state=view->state;
        auto before=std::atomic_load(&state->qualifiedAssociations);
        if(before && before->haveIssued &&
            (!SubtitleCurrentCueWorkerDetail::SameSourceEpoch(before->lastIssued,key.identity) ||
             (!SameAnalysisFrameIdentity(before->lastIssued,key.identity) &&
              !(key.identity.acceptedSequence>before->lastIssued.acceptedSequence &&
                key.identity.sourceFrameNumber>before->lastIssued.sourceFrameNumber &&
                key.identity.captureTimestamp>before->lastIssued.captureTimestamp))))return false;
        auto next=std::make_shared<SubtitleCurrentCueWorkerState::QualifiedAssociations>();
        if(before)*next=*before; // Small metadata only; receipts/ink remain shared immutable objects.
        auto& entries=next->encodings;
        SubtitleCurrentCueWorkerState::QualifiedEncoding* slot=nullptr;
        bool keepSeed=false;
        for(auto& entry:entries)if(entry.receipt && entry.token.SameSnapshot(token)) {
            if(entry.receipt->CurrentKey().identity.acceptedSequence>key.identity.acceptedSequence ||
                entry.lastIssued.acceptedSequence>key.identity.acceptedSequence)return false;
            const auto& existing=*entry.receipt;
            keepSeed=existing.Valid() && existing.TemplateRevision()==receipt.TemplateRevision() &&
                existing.SourceMode()==receipt.SourceMode() && existing.BackgroundMode()==receipt.BackgroundMode() &&
                SubtitleCurrentCueWorkerDetail::SameContext(existing.CurrentKey(),key) &&
                SameSubtitlePresentationBounds(existing.PhysicalBase(),receipt.PhysicalBase()) &&
                SubtitleCurrentCueWorkerDetail::SameCue(existing.AcceptedCue(),receipt.AcceptedCue()) &&
                SubtitleCurrentCueWorkerDetail::SameOperation(existing.Operation(),op);
            slot=&entry;break;
        }
        if(!slot)for(auto& entry:entries)if(!entry.receipt || !entry.token.Valid() || !entry.receipt->Valid()) {slot=&entry;break;}
        if(!slot)slot=&*std::min_element(entries.begin(),entries.end(),[](const SubtitleCurrentCueWorkerState::QualifiedEncoding& a,const SubtitleCurrentCueWorkerState::QualifiedEncoding& b){return a.lastUse<b.lastUse;});
        if(!keepSeed) {slot->token=token;slot->receipt=std::make_shared<const SubtitleCurrentCueProofReceipt>(receipt);}
        Trace(key,"cue-equivalent-association","qualified-operation-copy-ready");
        if(!receipt.Valid() || !token.MatchesSource(key.identity) ||
            !CurrentCueReadViewAccepts(view,key,receipt.TemplateRevision()))return false;
        next->lastIssued=key.identity;next->haveIssued=true;
        slot->lastIssued=key.identity;slot->lastUse=++next->clock;
        std::shared_ptr<const SubtitleCurrentCueWorkerState::QualifiedAssociations> publish=std::move(next);
        if(!std::atomic_compare_exchange_strong(&state->qualifiedAssociations,&before,publish))return false;
        return CurrentCueReadViewAccepts(view,key,receipt.TemplateRevision());
    }

__declspec(noinline) inline bool SubtitleMeasurementWorker::TryReadCurrentEquivalentCue(
        const SourceAnalysisTicket& ticket,const SourcePictureEquivalenceToken& token,
        const SubtitleBoxObservation& key,const ActivePictureBounds& base,
        SubtitlePreviewMode mode,int background,uint64_t revision,
        const CurrentSourceSupportEvidence* currentSupport,bool temporalDonorPossible,SubtitleCurrentCueProofReceipt& out) {
        // Invalidate without a large value temporary on the render stack.
        out.m_valid=false;out.m_consumed.reset();out.m_sourceEquivalence={};out.m_reason="equivalence-ticket-context";
        if(!ticket.MatchesRequestContext(key))return false;
        out.m_reason="equivalence-worker-window";
        const auto view=std::atomic_load(&m_currentCueReadView);
        if(!CurrentCueReadViewAccepts(view,key,revision))return false;
        const auto state=view->state;
        auto before=std::atomic_load(&state->qualifiedAssociations);
        if(!before)return false;
        const auto& lastIssued_=before->lastIssued;const bool haveIssued_=before->haveIssued;
        out.m_reason="equivalence-current-support-context";
        if(!haveIssued_ || !SubtitleCurrentCueWorkerDetail::SameSourceEpoch(lastIssued_,key.identity) || !(key.identity.acceptedSequence>lastIssued_.acceptedSequence && key.identity.sourceFrameNumber>lastIssued_.sourceFrameNumber && key.identity.captureTimestamp>lastIssued_.captureTimestamp) ||
            temporalDonorPossible || !token.MatchesSource(key.identity) || !currentSupport ||
            !currentSupport->evaluated || !currentSupport->verified ||
            !SameAnalysisFrameIdentity(currentSupport->identity,key.identity) ||
            currentSupport->samplingBounds.rasterWidth!=key.width || currentSupport->samplingBounds.rasterHeight!=key.height ||
            currentSupport->samplingBounds.left>base.left || currentSupport->samplingBounds.top>base.top ||
            currentSupport->samplingBounds.right<base.right || currentSupport->samplingBounds.bottom<base.bottom ||
            !revision || !SubtitlePreviewOverridesClassicHandling(mode) ||
            !key.sharedPicture.AvailableFor(key.identity,key.width,key.height) ||
            !SameSubtitlePresentationBounds(key.sharedPicture.bounds,base))return false;
        out.m_reason="equivalence-current-support-verification";
        const auto& support=currentSupport->samplingBounds;const auto& verified=currentSupport->verification;
        if(support.left<0 || support.top<0 || support.right>key.width || support.bottom>key.height ||
            support.left>=support.right || support.top>=support.bottom ||
            verified.bounds.rasterWidth!=key.width || verified.bounds.rasterHeight!=key.height ||
            verified.bounds.left<support.left || verified.bounds.top<support.top ||
            verified.bounds.right>support.right || verified.bounds.bottom>support.bottom ||
            verified.bounds.left>=verified.bounds.right || verified.bounds.top>=verified.bounds.bottom ||
            (verified.sourceGeneration!=key.identity.transportGeneration && verified.sourceGeneration!=key.identity.sourceFormatGeneration) ||
            verified.vertical==ActivePictureCandidateVerificationState::CONTRADICTED ||
            verified.horizontal==ActivePictureCandidateVerificationState::CONTRADICTED ||
            (verified.vertical!=ActivePictureCandidateVerificationState::VERIFIED &&
             verified.horizontal!=ActivePictureCandidateVerificationState::VERIFIED))return false;
        out.m_reason="equivalence-encoding-not-associated";
        for(size_t index=0;index<before->encodings.size();++index) {
            const auto& entry=before->encodings[index];
            if(!entry.receipt || !entry.token.SameSnapshot(token))continue;
            const auto& prior=*entry.receipt;
            const auto& old=prior.CurrentKey();const auto& issued=entry.lastIssued;
            out.m_reason="equivalence-source-order";
            if(key.identity.acceptedSequence<=issued.acceptedSequence ||
                key.identity.sourceFrameNumber<=issued.sourceFrameNumber || key.identity.captureTimestamp<=issued.captureTimestamp)return false;
            out.m_reason="equivalence-prior-cue-context";
            if(!prior.Valid() || prior.TemplateRevision()!=revision || prior.SourceMode()!=mode ||
                prior.BackgroundMode()!=background || old.identity.acceptedSequence>=key.identity.acceptedSequence ||
                old.identity.sourceFrameNumber>=key.identity.sourceFrameNumber || old.identity.captureTimestamp>=key.identity.captureTimestamp ||
                !SubtitleCurrentCueWorkerDetail::SameContext(old,key) ||
                !SameSubtitlePresentationBounds(prior.PhysicalBase(),base) || !prior.CurrentInk() ||
                !prior.CropExcludedBarsVerified() || prior.VerifiedLineCount()<1 || !prior.FootprintSamples())return false;
            // Full bytes were compared by the current native worker. Rebind only
            // immutable verified semantics and the operation recipe; the GPU
            // still reads the selected CURRENT source for all rendering/cleanup.
            const auto saved=entry.receipt;
            out=*saved;
            out.m_currentKey=key;out.m_currentKey.text={};out.m_currentKey.ink.reset();
            out.m_sourceEquivalence=token;
            out.m_provenance=SubtitleCurrentCueProofProvenance::PresentationOnlyExactSourcePixels;
            out.m_consumed=std::make_shared<std::atomic_bool>(false);
            out.m_reason="current-exact-source-encoding";
            out.m_workerThreadId=0;out.m_workerMilliseconds=0; // Old cue-worker cost/thread is not current work.
            Trace(key,"cue-equivalent-candidate","current-equivalence-copy-ready");
            if(!out.Valid() || out.m_minimumRevision!=m_currentCueMinimumRevision ||
                !CurrentCueReadViewAccepts(view,key,revision)) {
                out.m_valid=false;out.m_consumed.reset();out.m_sourceEquivalence={};
                out.m_reason="equivalence-retired-during-copy";return false;
            }
            auto next=std::make_shared<SubtitleCurrentCueWorkerState::QualifiedAssociations>(*before);
            next->lastIssued=key.identity;next->haveIssued=true;
            next->encodings[index].lastIssued=key.identity;next->encodings[index].lastUse=++next->clock;
            std::shared_ptr<const SubtitleCurrentCueWorkerState::QualifiedAssociations> publish=std::move(next);
            if(!std::atomic_compare_exchange_strong(&state->qualifiedAssociations,&before,publish) ||
                !CurrentCueReadViewAccepts(view,key,revision) || !out.Valid()) {
                out.m_valid=false;out.m_consumed.reset();out.m_sourceEquivalence={};
                out.m_reason="equivalence-issuance-superseded";return false;
            }
            return true;
        }
        return false;
    }

inline SubtitleCurrentCueProofStatus SubtitleMeasurementWorker::SubmitCurrentCueTemplate(
    const SubtitleCurrentCueTemplateInput& input,uint64_t& templateRevision) {
    return SubmitCurrentCueTemplateOwned(std::make_unique<SubtitleCurrentCueTemplateInput>(input),templateRevision);
}

__declspec(noinline) inline SubtitleCurrentCueProofStatus SubtitleMeasurementWorker::SubmitCurrentCueTemplateOwned(
    std::unique_ptr<SubtitleCurrentCueTemplateInput> ownedInput,uint64_t& templateRevision,bool nonblocking) {
    using namespace SubtitleCurrentCueWorkerDetail;
    templateRevision=0;
    if(!ownedInput)return SubtitleCurrentCueProofStatus::NotEligible;
    auto& input=*ownedInput;
    const auto& o=input.observation;const auto& cue=input.acceptedCue;
    const bool classic=input.sourceMode==SubtitlePreviewMode::None;
    const bool advanced=input.sourceMode==SubtitlePreviewMode::BlackBackground ||
        input.sourceMode==SubtitlePreviewMode::GeneratedGrayBackground;
    const bool retainedPresentation=cue.held;
    const bool qualifiedSamplingBasis=HasQualifiedCurrentCueSamplingBasis(input);
    if(retainedPresentation) {
        if(!advanced || !input.heldCurrentCue || !input.heldCurrentCue->Matches(o,cue,input.physicalBase))
            return SubtitleCurrentCueProofStatus::NotEligible;
        // Never clear held on historical geometry. Use this complete raw
        // measurement as the semantic seed, mapping only its display cue ID.
        auto currentSemanticCue=o.text;currentSemanticCue.cue=cue.cue;
        input.acceptedCue=std::move(currentSemanticCue);
    }
    if((!classic && !advanced) || !input.acceptedCurrentScan || !o.analyzed || o.discontinuity ||
        o.pendingRefresh || o.analysisRefresh || o.cadenceSkipped || o.glyphLocalTop || o.glyphLocalBottom ||
        (o.analysisStride!=1 && o.analysisStride!=2) || !o.text.detected || o.text.workLimit || o.text.held ||
        o.text.confirmationPending || o.text.revised || !o.text.topSearchComplete || !o.text.bottomSearchComplete ||
        (!o.independentBarAnalysis && !qualifiedSamplingBasis) || !o.barAuthority || o.barTrackingAuthority || o.currentAnchorUsesTrackedEdge ||
        !o.barEvidence.available || o.barEvidence.captionBackedTop || o.barEvidence.captionBackedBottom ||
        (!qualifiedSamplingBasis && (o.pictureTop!=o.barEvidence.top || o.pictureBottom!=o.barEvidence.bottom)) ||
        !o.ink || !o.ink->rawEvidenceComplete || !o.ink->paletteValid ||
        !cue.detected || cue.held || cue.confirmationPending || cue.revised || cue.workLimit ||
        cue.lineCount<1 || cue.lineCount>3 || !input.source.backingOwner || !input.source.IsValid() ||
        input.source.width!=o.width || input.source.height!=o.height ||
        (!qualifiedSamplingBasis && (!o.sharedPicture.AvailableFor(o.identity,o.width,o.height) ||
            !SameBounds(o.sharedPicture.bounds,input.physicalBase))) ||
        input.physicalBase.rasterWidth!=o.width || input.physicalBase.rasterHeight!=o.height ||
        !std::isfinite(input.frameMs) || input.frameMs<=0 || input.holdMs<=0 ||
        (advanced && (!input.compositionSucceeded || !input.composedOperation.valid)) ||
        (advanced && input.backgroundMode!=3 && input.backgroundMode!=5))
        return SubtitleCurrentCueProofStatus::NotEligible;
    // Canonicalize only this private presentation template's context. The
    // accepted full scan and its physical authority are validated above; its
    // RAW observation in the detector/tracker is never rewritten.
    SubtitleBoxObservation presentationKey;
    if(!BuildSubtitleCurrentCueProofKey(o,o.identity,input.physicalBase,true,presentationKey))
        return SubtitleCurrentCueProofStatus::NotEligible;
    input.observation.sharedPicture=presentationKey.sharedPicture;
    auto descriptor=Describe(input);
    // A stable healthy template is read from the immutable publication before
    // allocating a replacement. This is metadata, never current output proof.
    const auto readyView=std::atomic_load(&m_currentCueReadView);
    if(readyView && readyView->descriptor && readyView->descriptor->valid && readyView->revision>=m_currentCueMinimumRevision->load(std::memory_order_acquire) &&
        ((retainedPresentation && readyView->descriptor->cue.cue==descriptor.cue.cue &&
          SameProofInterpretation(*readyView->descriptor,descriptor) && SameOperation(readyView->descriptor->operation,descriptor.operation)) ||
         SameDescriptor(*readyView->descriptor,descriptor)) && std::atomic_load(&m_currentCueReadView)==readyView) {
        templateRevision=readyView->revision;return SubtitleCurrentCueProofStatus::Ready;
    }
    auto state=std::make_shared<SubtitleCurrentCueWorkerState>();
    auto entry=std::make_shared<SubtitleCurrentCueTaskEntry>();
    entry->key=o;entry->key.ink.reset();entry->descriptor=descriptor;entry->workerState=state;
    entry->seedTask=true;entry->status=SubtitleCurrentCueProofStatus::Queued;
    Job job;job.key=o;job.key.ink.reset();job.currentCueSeedTask=true;job.cueEntry=entry;
    job.queued=std::chrono::steady_clock::now();
    // The copyable std::function closure must stay small: capturing the input
    // and descriptor by value created another 55 KiB stack temporary here.
    // Both immutable objects remain owned until the bounded job is retired.
    std::shared_ptr<const SubtitleCurrentCueTemplateInput> seedInput(std::move(ownedInput));
    job.currentCueTask=[this,entry,state,seedInput](size_t) {
        const auto& input=*seedInput;
        const auto& descriptor=entry->descriptor;
        const auto started=std::chrono::steady_clock::now();
        {std::lock_guard<std::mutex> lock(m_mutex);
            if(m_stopping || entry->revision<m_currentCueMinimumRevision->load(std::memory_order_acquire)) {
                entry->status=SubtitleCurrentCueProofStatus::Expired;return;
            }
        }
        bool seeded=false;const char* reason="current-cue-template-seed-failed";
        SubtitleBoxPreview preview;preview.current=input.observation;
        preview.current.text=input.observation.text;preview.text=input.acceptedCue;
        preview.available=true;preview.policyGeneration=input.observation.policyGeneration;
        preview.continuityGeneration=input.observation.continuityGeneration;
        const bool advanced=input.sourceMode!=SubtitlePreviewMode::None;
        bool footprintSeeded=!advanced || state->footprint.RememberCurrentFootprint(preview,input.acceptedCue,
            input.source,input.physicalBase,input.composedOperation,input.frameMs,input.holdMs,input.backgroundMode);
        if(footprintSeeded) {
            const auto* footprint=advanced?&state->footprint:nullptr;
            seeded=state->pending.RememberAccepted(preview,input.acceptedCue,input.source,input.frameMs,
                input.holdMs,nullptr,nullptr,&input.physicalBase,footprint,true);
            if(seeded) {
                state->descriptor=descriptor;state->descriptor.revision=entry->revision;
                state->lastSemanticRefreshSequence=descriptor.context.identity.acceptedSequence;
                state->lastSemanticScanSequence=descriptor.context.identity.acceptedSequence;
                const double intervalValue=std::ceil(100.0/input.frameMs);
                state->semanticRefreshIntervalFrames=static_cast<uint64_t>((std::min)(1000.0,
                    (std::max)(1.0,std::isfinite(intervalValue)?intervalValue:1000.0)));
                reason=advanced?"current-cue-template-footprint-seeded":"current-cue-template-seeded";
            } else {seeded=false;reason=state->pending.DecisionReason();}
        } else reason=state->footprint.DecisionReason();
        if(seeded) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if(!m_stopping && entry->revision>=m_currentCueMinimumRevision->load(std::memory_order_acquire) &&
                (!m_currentCueState || entry->revision>=m_currentCueState->descriptor.revision)) {
                m_currentCueState=state;m_workerLimit=2;PublishCurrentCueReadViewLocked();
                if(!m_threads[1].joinable())m_threads[1]=std::thread([this]{Run(1);});
            } else {seeded=false;reason="current-cue-template-context-invalidated";}
        }
        const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        auto trace=AnalysisTraceRecord(input.observation.identity,this,"worker-cue-template",reason);
        trace.kind=AnalysisDiagnosticRecordKindV1::Worker;trace.policyGeneration=input.observation.policyGeneration;
        trace.continuityGeneration=input.observation.continuityGeneration;trace.sourceWidth=input.observation.width;
        trace.sourceHeight=input.observation.height;trace.resultCode=input.acceptedCue.lineCount;
        trace.counters[0]=entry->revision;trace.counters[1]=SubtitleCurrentCueWorkerDetail::ThreadId();
        const auto& seedDiagnostics=state->pending.DecisionDiagnostics();
        trace.counters[2]=seedDiagnostics.seedBandSamplesChecked+seedDiagnostics.seedGlyphSamplesChecked+
            seedDiagnostics.seedKnownSamplesChecked+seedDiagnostics.seedBackingSamplesChecked;
        trace.counters[3]=0;trace.counters[4]=0;trace.metrics[0]=elapsed;
        trace.flags=seeded?1u:0u;AnalysisDiagnosticTrace::Emit(trace);
        std::vector<std::shared_ptr<SubtitleCurrentCueTaskEntry>> retiredEntries;
        {std::lock_guard<std::mutex> lock(m_mutex);
            entry->status=entry->revision<m_currentCueMinimumRevision->load(std::memory_order_acquire)?
                SubtitleCurrentCueProofStatus::Expired:
                (seeded?SubtitleCurrentCueProofStatus::Ready:SubtitleCurrentCueProofStatus::Failed);
            entry->key.ink.reset();entry->workerState.reset();
            while(m_currentCueTasks.size()>8) {
                retiredEntries.push_back(std::move(m_currentCueTasks.front()));m_currentCueTasks.pop_front();
            }
        }
    };
    {
        std::unique_lock<std::mutex> lock(m_mutex,std::defer_lock);
        if(nonblocking) {if(!lock.try_lock())return SubtitleCurrentCueProofStatus::NotEligible;}else lock.lock();
        if(m_stopping)return SubtitleCurrentCueProofStatus::Failed;
        // Held presentation does not rebuild a healthy template for grouping
        // quantization alone. Actual pixel failure retires that baseline below.
        if(retainedPresentation && HasCurrentCueProofContextLocked() && m_currentCueState->descriptor.valid &&
            m_currentCueState->descriptor.cue.cue==descriptor.cue.cue &&
            SameProofInterpretation(m_currentCueState->descriptor,descriptor) &&
            SameOperation(m_currentCueState->descriptor.operation,descriptor.operation)) {
            templateRevision=m_currentCueState->descriptor.revision;
            return SubtitleCurrentCueProofStatus::Ready;
        }
        if(HasCurrentCueProofContextLocked() && SameDescriptor(m_currentCueState->descriptor,descriptor)) {
            templateRevision=m_currentCueState->descriptor.revision;
            return SubtitleCurrentCueProofStatus::Ready;
        }
        for(const auto& pending:m_currentCueTasks)
            if(pending->seedTask && pending->status==SubtitleCurrentCueProofStatus::Queued &&
                pending->revision>=m_currentCueMinimumRevision->load(std::memory_order_acquire) && SameDescriptor(pending->descriptor,descriptor)) {
                templateRevision=pending->revision;return SubtitleCurrentCueProofStatus::Queued;
            }
        // A failed working seed is replaced only by a later independently
        // measured source, never by replaying the seed or an older scan.
        if(HasCurrentCueProofContextLocked() && !m_currentCueState->descriptor.valid &&
            SameInterpretationContext(descriptor.context,m_currentCueState->descriptor.context) &&
            (descriptor.context.identity.sourceFrameNumber<m_currentCueState->descriptor.context.identity.sourceFrameNumber ||
             descriptor.context.identity.captureTimestamp<m_currentCueState->descriptor.context.identity.captureTimestamp ||
             descriptor.context.identity.acceptedSequence<=m_currentCueState->descriptor.context.identity.acceptedSequence))
            return SubtitleCurrentCueProofStatus::NotEligible;
        if(m_currentCuePending.size()>=2)return SubtitleCurrentCueProofStatus::NotEligible;
        // Real render seeds follow an already running scan worker. Thread
        // creation belongs to normal dispatch, never this nonblocking offer.
        if(nonblocking && !m_threads[0].joinable())return SubtitleCurrentCueProofStatus::NotEligible;
        descriptor.revision=++m_nextCurrentCueRevision;templateRevision=descriptor.revision;
        entry->revision=descriptor.revision;entry->descriptor.revision=descriptor.revision;
        m_currentCueTasks.push_back(entry);m_currentCuePending.push_back(std::move(job));
        if(!m_threads[0].joinable())m_threads[0]=std::thread([this]{Run(0);});
    }
    m_changed.notify_all();
    return SubtitleCurrentCueProofStatus::Queued;
}

inline SubtitleCurrentCueProofStatus SubtitleMeasurementWorker::SubmitCurrentCueProof(
    SubtitleCurrentCueProofRequest request,bool nonblocking) {
    using namespace SubtitleCurrentCueWorkerDetail;
    std::shared_ptr<SubtitleCurrentCueWorkerState> state;
    std::shared_ptr<SubtitleCurrentCueTaskEntry> entry;
    std::shared_ptr<SubtitleCurrentCueTaskEntry> duplicate;
    SubtitleCurrentCueTemplateDescriptor descriptor;
    SubtitleCurrentCueProofStatus duplicateStatus=SubtitleCurrentCueProofStatus::NoTask;
    {
        std::unique_lock<std::mutex> lock(m_mutex,std::defer_lock);
        if(nonblocking) {if(!lock.try_lock())return SubtitleCurrentCueProofStatus::NotEligible;}else lock.lock();
        if(m_stopping)return SubtitleCurrentCueProofStatus::Failed;
        if(!request.templateRevision)return SubtitleCurrentCueProofStatus::NoTemplate;
        if(request.templateRevision<m_currentCueMinimumRevision->load(std::memory_order_acquire))
            return SubtitleCurrentCueProofStatus::Expired;
        if(m_currentCueState && m_currentCueState->descriptor.revision==request.templateRevision) {
            state=m_currentCueState;descriptor=state->descriptor;
        } else {
            for(const auto& pending:m_currentCueTasks)
                if(pending->seedTask && pending->status==SubtitleCurrentCueProofStatus::Queued &&
                    pending->revision==request.templateRevision) {
                    state=pending->workerState;descriptor=pending->descriptor;break;
                }
            if(!state)return m_currentCueState?SubtitleCurrentCueProofStatus::NotEligible:
                SubtitleCurrentCueProofStatus::NoTemplate;
        }
        const bool sameTemplateContext=SameContext(descriptor.context,request.currentKey);
        const bool currentMatches=CurrentMatches(descriptor,request.currentKey,request.physicalBase,request.source,
            request.sourceMode,request.backgroundMode);
        const bool scanBaseCompatible=CompatibleScanBase(request.fullScanKey,request.physicalBase);
        const bool scanKeyMatches=!request.hasFullScanKey ||
            (SameAnalysisFrameIdentity(request.fullScanKey.identity,request.currentKey.identity) &&
             SameInterpretationContext(request.fullScanKey,request.currentKey) && scanBaseCompatible);
        const bool basicEligible=currentMatches && scanKeyMatches && request.source.backingOwner &&
            request.currentKey.identity.acceptedSequence && request.currentKey.identity.captureTimestamp &&
            std::isfinite(request.frameMs) && request.frameMs>0;
        if(sameTemplateContext)state->cadenceRoutingEligible=currentMatches;
        if(!basicEligible)
            return SubtitleCurrentCueProofStatus::NotEligible;
        if(!std::atomic_load(&m_currentCueSourceWindow))
            PublishCurrentCueWindow(request.eligibleRenderHead,request.eligibleTailSequence,true);
        const auto sourceWindow=std::atomic_load(&m_currentCueSourceWindow);
        const bool inWindow=sourceWindow && request.eligibleTailSequence<=sourceWindow->tail &&
            InCurrentCueWindow(request.eligibleRenderHead) && InCurrentCueWindow(request.currentKey.identity) &&
            InWindow(request.eligibleRenderHead,request.eligibleTailSequence,request.currentKey.identity);
        if(!inWindow)
            return SubtitleCurrentCueProofStatus::NotEligible;
        for(const auto& old:m_currentCueTasks)
            if(!old->seedTask && old->revision==request.templateRevision && SameExactTaskKey(old->key,request.currentKey))
                {duplicate=old;duplicateStatus=old->status;break;}
        if(duplicate && request.completedProof) {
            if(!duplicate->completedProof)duplicate->completedProof=request.completedProof;
            PublishCompletedCurrentCueProof(request.completedProof,duplicate->completedPublication);
        }
        if(duplicate && nonblocking && !duplicate->scanDecisionMade && request.fullScanDue &&
            request.fullScanOnMiss && !duplicate->deferredFullScan) {
            duplicate->deferredFullScan=std::move(request.fullScanOnMiss);
            duplicate->deferredFullScanKey=request.hasFullScanKey?request.fullScanKey:request.currentKey;
            duplicate->deferredFullScanKey.ink.reset();duplicate->fullScanDue=true;
            if(request.hasOwnedWitness) {duplicate->deferredWitness=request.ownedWitness;duplicate->hasDeferredWitness=true;}
            duplicate->deferredTimelinePolicy=request.ownedTimelinePolicy;
        }
        if(!duplicate) {
            if(m_currentCuePending.size()>=2)return SubtitleCurrentCueProofStatus::NotEligible;
            entry=std::make_shared<SubtitleCurrentCueTaskEntry>();entry->key=request.currentKey;
            entry->key.ink.reset();
            entry->scanKey=request.hasFullScanKey?request.fullScanKey:request.currentKey;
            entry->scanKey.ink.reset();
            entry->revision=request.templateRevision;entry->status=SubtitleCurrentCueProofStatus::Queued;
            entry->completedProof=request.completedProof;
            entry->descriptor=descriptor;
            for(const auto& old:m_currentCueTasks)
                if(!old->seedTask && old->revision>=m_currentCueMinimumRevision->load(std::memory_order_acquire) &&
                    SameExactTaskKey(old->key,request.currentKey) && old->consumed) {
                    entry->consumed=old->consumed;break;
                }
            if(!entry->consumed)entry->consumed=std::make_shared<std::atomic_bool>(false);
            if(request.fullScanDue && request.fullScanOnMiss) {
                entry->deferredFullScan=std::move(request.fullScanOnMiss);entry->fullScanDue=true;
                entry->deferredFullScanKey=request.hasFullScanKey?request.fullScanKey:request.currentKey;
                entry->deferredFullScanKey.ink.reset();
                if(request.hasOwnedWitness) {
                    entry->deferredWitness=request.ownedWitness;entry->hasDeferredWitness=true;
                }
                entry->deferredTimelinePolicy=request.ownedTimelinePolicy;
            }
        }
    }
    if(duplicate && nonblocking) {
        // A completed proof may receive a later scan offer once native facts
        // become available. Preserve that original key/callback via the existing
        // nonblocking ordinary route, which owns cadence/dedup/capacity.
        if(request.fullScanDue && request.fullScanOnMiss)
            Submit(request.hasFullScanKey?request.fullScanKey:request.currentKey,
                std::move(request.fullScanOnMiss),request.hasOwnedWitness?&request.ownedWitness:nullptr,
                request.ownedTimelinePolicy,true,request.completion);
        return duplicateStatus;
    }
    if(duplicate) {
        Work lateScan;std::shared_ptr<SubtitleCurrentCueRefreshLease> lateLease;
        bool lateFallback=false,advanceRefreshDeadline=false;uint64_t previousRefreshSequence=0;
        OwnedScanBacklogWitness lateWitness;bool hasLateWitness=false;uint64_t lateTimelinePolicy=0;
        if(request.fullScanDue && request.fullScanOnMiss) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if(request.templateRevision<m_currentCueMinimumRevision->load(std::memory_order_acquire))
                return SubtitleCurrentCueProofStatus::Expired;
            if(!duplicate->scanDecisionMade) {
                if(!duplicate->deferredFullScan) {
                    duplicate->deferredFullScan=std::move(request.fullScanOnMiss);
                    duplicate->deferredFullScanKey=request.hasFullScanKey?request.fullScanKey:request.currentKey;
                    duplicate->deferredFullScanKey.ink.reset();
                    duplicate->fullScanDue=true;
                    if(request.hasOwnedWitness) {
                        duplicate->deferredWitness=request.ownedWitness;duplicate->hasDeferredWitness=true;
                    }
                    duplicate->deferredTimelinePolicy=request.ownedTimelinePolicy;
                }
            } else if(!duplicate->fullScanQueued) {
                lateFallback=!duplicate->proofVerified;
                bool due=lateFallback || duplicate->semanticRefreshDue ||
                    state->needsFullScanBurst.load(std::memory_order_acquire);
                const double intervalValue=std::ceil(100.0/request.frameMs);
                const double boundedInterval=(std::min)(1000.0,
                    (std::max)(1.0,std::isfinite(intervalValue)?intervalValue:1000.0));
                const uint64_t interval=static_cast<uint64_t>(boundedInterval);
                const uint64_t sequence=request.currentKey.identity.acceptedSequence;
                if(!lateFallback && sequence>state->lastSemanticRefreshSequence &&
                    sequence-state->lastSemanticRefreshSequence>=interval)due=true;
                if(due) {
                    if(!lateFallback) {
                        bool expected=false;
                        if(!state->semanticRefreshInFlight.compare_exchange_strong(
                            expected,true,std::memory_order_acq_rel))due=false;
                        else {
                            lateLease=std::make_shared<SubtitleCurrentCueRefreshLease>();lateLease->state=state;
                            previousRefreshSequence=state->lastSemanticRefreshSequence;
                            state->lastSemanticRefreshSequence=sequence;advanceRefreshDeadline=true;
                        }
                    }
                    if(due) {
                        duplicate->fullScanQueued=true;
                        lateScan=std::move(request.fullScanOnMiss);
                        hasLateWitness=request.hasOwnedWitness;lateWitness=request.ownedWitness;
                        lateTimelinePolicy=request.ownedTimelinePolicy;
                    }
                }
            }
        }
        bool lateQueued=false;
        if(lateScan) {
            const auto fullScan=[](Work scan,std::shared_ptr<SubtitleCurrentCueRefreshLease> lease) {
                return Work([scan=std::move(scan),lease=std::move(lease)](
                    SubtitleMeasurementSampler& sampler,const SubtitleBarTrackingReference& previous) mutable {
                    (void)lease;return scan(sampler,previous);
                });
            };
            lateQueued=SubmitCore(request.hasFullScanKey?request.fullScanKey:request.currentKey,
                fullScan(std::move(lateScan),std::move(lateLease)),
                hasLateWitness?&lateWitness:nullptr,lateTimelinePolicy,true,{});
            if(!lateQueued) {
                std::lock_guard<std::mutex> lock(m_mutex);
                duplicate->fullScanQueued=false;
                if(advanceRefreshDeadline && state->lastSemanticRefreshSequence==request.currentKey.identity.acceptedSequence)
                    state->lastSemanticRefreshSequence=previousRefreshSequence;
            } else if(lateFallback) {
                std::lock_guard<std::mutex> lock(m_mutex);
                duplicate->fallbackQueued=true;duplicate->status=SubtitleCurrentCueProofStatus::FallbackQueued;
            }
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        return duplicate->status==SubtitleCurrentCueProofStatus::NoTask?duplicateStatus:duplicate->status;
    }
    Job job;job.key=request.currentKey;job.cueEntry=entry;job.queued=std::chrono::steady_clock::now();
    job.currentCueTask=[this,state,entry,queued=job.queued,request=std::move(request)](size_t) mutable {
        const auto started=std::chrono::steady_clock::now();
        uint64_t cycleDelta=0;
#ifdef _WIN32
        ULONG64 startCycles=0;
        const bool haveStartCycles=QueryThreadCycleTime(GetCurrentThread(),&startCycles)!=FALSE;
#endif
        {std::lock_guard<std::mutex> lock(m_mutex);entry->status=SubtitleCurrentCueProofStatus::Pending;}
        SubtitleCurrentCueProofReceipt receipt;const char* reason="current-cue-template-miss";
        std::shared_ptr<SubtitleInkSnapshot> currentInk;bool verified=false,pixelVerificationAttempted=false;
        SubtitleCutPasteGeometry operation=state->descriptor.operation;
        const auto& key=request.currentKey;
        bool stillCurrent=false;
        std::array<SubtitleBoxRect,3> semanticLineBounds{};
        {std::lock_guard<std::mutex> lock(m_mutex);
            stillCurrent=!m_stopping && HasCurrentCueProofContextLocked() &&
                state->descriptor.valid && m_currentCueState->descriptor.valid &&
                entry->revision>=m_currentCueMinimumRevision->load(std::memory_order_acquire) &&
                SameProofInterpretation(state->descriptor,m_currentCueState->descriptor) &&
                entry->revision==state->descriptor.revision &&
                InCurrentCueWindow(key.identity) &&
                SubtitleCurrentCueWorkerDetail::CurrentMatches(state->descriptor,key,
                    request.physicalBase,request.source,request.sourceMode,request.backgroundMode);
        }
        if(stillCurrent) {
            const auto* footprint=request.sourceMode==SubtitlePreviewMode::None?nullptr:&state->footprint;
            pixelVerificationAttempted=true;
            verified=state->pending.VerifyCurrentCuePixels(
                key,request.source,footprint,currentInk,reason,true);
            if(verified && footprint && currentInk)
                semanticLineBounds=footprint->VerifiedSemanticLineBounds(currentInk->step);
            if(verified && footprint && !footprint->ApplyCurrentFootprintProtection(
                    key,request.physicalBase,operation)) {
                verified=false;currentInk.reset();reason="current-cue-cleanup-projection-unavailable";
            }
        } else reason="current-cue-template-context-or-window-changed";
        const auto proofFinished=std::chrono::steady_clock::now();
        const double elapsed=std::chrono::duration<double,std::milli>(proofFinished-started).count();
#ifdef _WIN32
        ULONG64 nativeEndCycles=0;
        if(haveStartCycles&&QueryThreadCycleTime(GetCurrentThread(),&nativeEndCycles)!=FALSE&&
            nativeEndCycles>=startCycles)
            cycleDelta=static_cast<uint64_t>(nativeEndCycles-startCycles);
#endif
        const uint64_t threadId=SubtitleCurrentCueWorkerDetail::ThreadId();
        const auto& diagnostics=state->pending.DecisionDiagnostics();
        if(verified && currentInk && !currentInk->rawEvidenceComplete && currentInk->paletteValid) {
            receipt.m_valid=true;receipt.m_provenance=SubtitleCurrentCueProofProvenance::PresentationOnlyCurrentPixels;
            receipt.m_templateRevision=entry->revision;receipt.m_minimumRevision=m_currentCueMinimumRevision;receipt.m_seedIdentity=state->descriptor.context.identity;
            receipt.m_acceptedCue=state->descriptor.cue;
            receipt.m_workerThreadId=threadId;receipt.m_workerMilliseconds=elapsed;
            receipt.m_verifiedLineCount=state->descriptor.cue.lineCount;
            receipt.m_sourceMode=state->descriptor.sourceMode;
            receipt.m_backgroundMode=state->descriptor.backgroundMode;
            receipt.m_currentKey=key;receipt.m_currentKey.text={};receipt.m_currentKey.ink.reset();
            receipt.m_currentKey.independentBarAnalysis=false;receipt.m_currentKey.barAuthority=false;
            receipt.m_currentKey.barTrackingAuthority=false;receipt.m_currentKey.currentAnchorUsesTrackedEdge=false;
            receipt.m_currentInk=std::move(currentInk);receipt.m_physicalBase=request.physicalBase;
            receipt.m_operation=operation;receipt.m_cropExcludedBarsVerified=true;
            receipt.m_verifiedSemanticLineBounds=semanticLineBounds;
            receipt.m_footprintSamples=diagnostics.currentFootprintSamplesChecked;
            receipt.m_barVisibilitySamples=diagnostics.barVisibilitySamplesChecked;
            receipt.m_reason="current-cue-pixels-verified";receipt.m_consumed=entry->consumed;
            reason=receipt.m_reason;
        } else verified=false;
        bool fallbackQueued=false,semanticRefreshDue=false,semanticRefreshQueued=false;
        const double intervalValue=std::ceil(100.0/request.frameMs);
        const double boundedInterval=(std::min)(1000.0,
            (std::max)(1.0,std::isfinite(intervalValue)?intervalValue:1000.0));
        const uint64_t interval=static_cast<uint64_t>(boundedInterval);
        const uint64_t sequence=key.identity.acceptedSequence;
        uint64_t lastRefreshSequence=0;
        {std::lock_guard<std::mutex> lock(m_mutex);lastRefreshSequence=state->lastSemanticRefreshSequence;}
        if(verified) {
            const bool burst=state->needsFullScanBurst.load(std::memory_order_acquire);
            const bool intervalReached=sequence>lastRefreshSequence && sequence-lastRefreshSequence>=interval;
            semanticRefreshDue=burst||intervalReached;
        }
        Work queuedScan,retiredDeferredScan;SubtitleBoxObservation queuedScanKey;OwnedScanBacklogWitness queuedWitness;bool hasQueuedWitness=false;
        uint64_t queuedTimelinePolicy=0;bool queuedAsFallback=false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if(entry->revision<m_currentCueMinimumRevision->load(std::memory_order_acquire)) {
                verified=false;stillCurrent=false;semanticRefreshDue=false;
                receipt={};reason="current-cue-proof-context-invalidated";
            }
            entry->scanDecisionMade=true;entry->proofVerified=verified;
            entry->semanticRefreshDue=semanticRefreshDue;
            if(entry->revision>=m_currentCueMinimumRevision->load(std::memory_order_acquire) &&
                entry->fullScanDue && entry->deferredFullScan && !entry->fullScanQueued &&
                (!verified || semanticRefreshDue)) {
                queuedScan=std::move(entry->deferredFullScan);queuedScanKey=entry->deferredFullScanKey;entry->fullScanDue=false;
                queuedWitness=entry->deferredWitness;hasQueuedWitness=entry->hasDeferredWitness;
                queuedTimelinePolicy=entry->deferredTimelinePolicy;entry->hasDeferredWitness=false;
                entry->fullScanQueued=true;queuedAsFallback=!verified;
            } else {
                // A source-specific callback that was not due is discarded;
                // a later same-key due upgrade supplies its own exact callback.
                retiredDeferredScan=std::move(entry->deferredFullScan);
                entry->fullScanDue=false;entry->hasDeferredWitness=false;
            }
        }
        if(queuedScan) {
            const auto fullScan=[](Work scan,std::shared_ptr<SubtitleCurrentCueRefreshLease> lease) {
                return Work([scan=std::move(scan),lease=std::move(lease)](
                    SubtitleMeasurementSampler& sampler,const SubtitleBarTrackingReference& previous) mutable {
                    (void)lease;return scan(sampler,previous);
                });
            };
            std::shared_ptr<SubtitleCurrentCueRefreshLease> lease;
            bool permitQueue=true;
            if(!queuedAsFallback) {
                bool expected=false;
                permitQueue=state->semanticRefreshInFlight.compare_exchange_strong(
                    expected,true,std::memory_order_acq_rel);
                if(permitQueue) {
                    lease=std::make_shared<SubtitleCurrentCueRefreshLease>();lease->state=state;
                    std::lock_guard<std::mutex> lock(m_mutex);
                    state->lastSemanticRefreshSequence=sequence;
                }
            }
            if(permitQueue) {
                fallbackQueued=queuedAsFallback;
                const bool accepted=SubmitCore(queuedScanKey,fullScan(std::move(queuedScan),std::move(lease)),
                    hasQueuedWitness?&queuedWitness:nullptr,queuedTimelinePolicy,true,{});
                if(queuedAsFallback)fallbackQueued=accepted;
                else semanticRefreshQueued=accepted;
                if(!accepted) {
                    std::lock_guard<std::mutex> lock(m_mutex);entry->fullScanQueued=false;
                    if(queuedAsFallback)entry->fallbackQueued=false;
                } else if(queuedAsFallback) {
                    std::lock_guard<std::mutex> lock(m_mutex);entry->fallbackQueued=true;
                }
            } else {
                std::lock_guard<std::mutex> lock(m_mutex);entry->fullScanQueued=false;
            }
        }
        // Allocate/copy on the optional worker before its short commit lock.
        // Keep only layout metadata, never a captured movie/background owner.
        std::shared_ptr<const SubtitleCurrentCueProofPublication> completed;
        if(verified) {
            auto next=std::make_shared<SubtitleCurrentCueProofPublication>();
            next->receipt=receipt;next->sourceLayout=request.source;
            next->sourceLayout.data=nullptr;next->sourceLayout.dataBytes=0;next->sourceLayout.backingOwner.reset();
            completed=std::move(next);
        }
        // Publish the exact-source result before optional diagnostic callbacks.
        // Trace transport must not postpone an otherwise ready video decision.
        std::vector<std::shared_ptr<SubtitleCurrentCueTaskEntry>> retiredEntries;
        {std::lock_guard<std::mutex> lock(m_mutex);
            if(entry->revision<m_currentCueMinimumRevision->load(std::memory_order_acquire)) {
                verified=false;stillCurrent=false;receipt={};reason="current-cue-proof-context-invalidated";
            }
            if(!verified && stillCurrent && m_currentCueState==state &&
                entry->revision==state->descriptor.revision &&
                diagnostics.currentPixelFailure==SubtitlePendingMeasurementGuard::CurrentPixelVerificationFailure::PixelMismatch) {
                // Cue tasks run serially on worker0. Retain context and completed
                // receipts; fresh independent evidence must replace this seed.
                state->descriptor.valid=false;PublishCurrentCueReadViewLocked();
            }
            if(verified) {
                entry->receipt=std::move(receipt);entry->status=SubtitleCurrentCueProofStatus::Ready;
                entry->completedPublication=std::move(completed);
                PublishCompletedCurrentCueProof(entry->completedProof,entry->completedPublication);
            }
            else entry->status=(fallbackQueued || entry->fallbackQueued)?SubtitleCurrentCueProofStatus::FallbackQueued:
                (stillCurrent?SubtitleCurrentCueProofStatus::Failed:SubtitleCurrentCueProofStatus::Expired);
            entry->key.ink.reset();
            while(m_currentCueTasks.size()>8) {
                retiredEntries.push_back(std::move(m_currentCueTasks.front()));m_currentCueTasks.pop_front();
            }
        }
        const auto published=std::chrono::steady_clock::now();
        const auto timeNs=[](const std::chrono::steady_clock::time_point& value) {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                value.time_since_epoch()).count());
        };
        if(semanticRefreshDue) {
            auto refreshTrace=AnalysisTraceRecord(key.identity,this,"worker-cue-semantic-refresh",
                semanticRefreshQueued?"queued":(state->semanticRefreshInFlight.load(std::memory_order_acquire)?
                    "coalesced":"not-due-callback"));
            refreshTrace.kind=AnalysisDiagnosticRecordKindV1::Worker;
            refreshTrace.policyGeneration=key.policyGeneration;
            refreshTrace.continuityGeneration=key.continuityGeneration;
            refreshTrace.sourceWidth=key.width;refreshTrace.sourceHeight=key.height;
            refreshTrace.counters[0]=entry->revision;refreshTrace.counters[1]=interval;
            refreshTrace.counters[2]=sequence;refreshTrace.flags=semanticRefreshQueued?1u:0u;
            refreshTrace.metrics[0]=request.frameMs;AnalysisDiagnosticTrace::Emit(refreshTrace);
        }
        auto trace=AnalysisTraceRecord(key.identity,this,"worker-cue-proof",reason);
        trace.kind=AnalysisDiagnosticRecordKindV1::Worker;trace.policyGeneration=key.policyGeneration;
        trace.continuityGeneration=key.continuityGeneration;trace.sourceWidth=key.width;trace.sourceHeight=key.height;
        trace.resultCode=verified?state->descriptor.cue.lineCount:0;
        trace.counters[0]=entry->revision;trace.counters[1]=threadId;
        trace.counters[2]=diagnostics.currentFootprintSamplesChecked;
        trace.counters[3]=diagnostics.barVisibilitySamplesChecked;
        trace.counters[4]=verified?state->descriptor.cue.lineCount:0;
        trace.metrics[0]=elapsed;
        trace.metrics[1]=diagnostics.glyphMismatchX;trace.metrics[2]=diagnostics.glyphMismatchY;
        trace.metrics[3]=diagnostics.footprintVerifyMs;
        trace.metrics[4]=diagnostics.currentPixelValidationMs;
        trace.metrics[5]=diagnostics.encodedBarVetoMs;
        trace.metrics[6]=static_cast<double>(cycleDelta);
        trace.metrics[7]=std::chrono::duration<double,std::milli>(started-queued).count();
        trace.metrics[8]=std::chrono::duration<double,std::milli>(published-queued).count();
        trace.metrics[9]=std::chrono::duration<double,std::milli>(published-proofFinished).count();
        trace.counters[5]=timeNs(queued);trace.counters[6]=timeNs(started);trace.counters[7]=timeNs(published);
        trace.flags=(verified?1u:0u)|(semanticRefreshDue?2u:0u)|
            (semanticRefreshQueued?4u:0u);AnalysisDiagnosticTrace::Emit(trace);
        if(pixelVerificationAttempted && !verified && diagnostics.pixelWitness.Valid()) {
            const auto& w=diagnostics.pixelWitness;
            auto witness=AnalysisTraceRecord(key.identity,this,"worker-cue-pixel-witness",reason);
            witness.kind=AnalysisDiagnosticRecordKindV1::Worker;
            witness.policyGeneration=key.policyGeneration;witness.continuityGeneration=key.continuityGeneration;
            witness.sourceWidth=key.width;witness.sourceHeight=key.height;
            witness.resultCode=static_cast<int32_t>(w.stage);witness.flags=w.zones;
            witness.counters[0]=entry->revision;witness.counters[1]=w.baselineIdentity.acceptedSequence;
            witness.counters[2]=w.seedClass;witness.counters[3]=w.currentClass;
            witness.counters[4]=w.seedNeighborClass;witness.counters[5]=w.currentNeighborClass;
            witness.counters[6]=w.seedDirections;witness.counters[7]=w.currentDirections;
            witness.metrics[0]=w.x;witness.metrics[1]=w.y;
            witness.metrics[2]=w.neighborX;witness.metrics[3]=w.neighborY;
            const AnalysisLumaSample samples[]={w.seed,w.current,w.seedNeighbor,w.currentNeighbor};
            for(size_t i=0;i<4;++i) {
                witness.metrics[4+i*3]=samples[i].luma;
                witness.metrics[5+i*3]=samples[i].chromaU;
                witness.metrics[6+i*3]=samples[i].chromaV;
            }
            witness.metrics[16]=w.blackLimit;witness.metrics[17]=w.inkFloor;
            witness.metrics[18]=w.backingLimit;witness.metrics[19]=w.neighborZones;
            witness.metrics[20]=SubtitleCadenceGuard::EncodingEquivalent(w.seed,w.current)?1:0;
            witness.metrics[21]=w.neighborX>=0 && SubtitleCadenceGuard::EncodingEquivalent(w.seedNeighbor,w.currentNeighbor)?1:0;
            AnalysisTraceRect(witness.measured,state->descriptor.operation.content);
            AnalysisTraceRect(witness.candidate,state->descriptor.operation.generatedCleanup);
            AnalysisTraceRect(witness.published,state->descriptor.operation.pictureCapture);
            AnalysisTraceRect(witness.displayed,state->descriptor.operation.destination);
            AnalysisDiagnosticTrace::Emit(witness);
            // Full immutable baseline identity, including rearm provenance.
            auto seedWitness=AnalysisTraceRecord(w.baselineIdentity,this,"worker-cue-pixel-seed",reason);
            seedWitness.kind=AnalysisDiagnosticRecordKindV1::Worker;
            seedWitness.policyGeneration=w.baselinePolicy;seedWitness.continuityGeneration=w.baselineContinuity;
            seedWitness.counters[0]=entry->revision;seedWitness.counters[1]=key.identity.acceptedSequence;
            seedWitness.resultCode=static_cast<int32_t>(w.stage);AnalysisDiagnosticTrace::Emit(seedWitness);
        }
        AnalysisLumaSource retiredSource=std::move(request.source);
        Work retiredRequestScan=std::move(request.fullScanOnMiss);
    };
    {
        std::unique_lock<std::mutex> lock(m_mutex,std::defer_lock);
        if(nonblocking) {if(!lock.try_lock())return SubtitleCurrentCueProofStatus::NotEligible;}else lock.lock();
        if(m_stopping || entry->revision<m_currentCueMinimumRevision->load(std::memory_order_acquire) ||
            !InCurrentCueWindow(entry->key.identity))return SubtitleCurrentCueProofStatus::Expired;
        // Another dispatcher may have published this exact task while its
        // immutable closure was prepared. Never register a duplicate/orphan.
        for(const auto& old:m_currentCueTasks)
            if(!old->seedTask && old->revision==entry->revision && SameExactTaskKey(old->key,entry->key))return old->status;
        if(m_currentCuePending.size()>=2)return SubtitleCurrentCueProofStatus::NotEligible;
        m_currentCueTasks.push_back(entry);m_currentCuePending.push_back(std::move(job));
        if(!m_threads[0].joinable())m_threads[0]=std::thread([this]{Run(0);});
    }
    m_changed.notify_all();return SubtitleCurrentCueProofStatus::Queued;
}

inline SubtitleCurrentCueProofStatus SubtitleMeasurementWorker::TryTakeCurrentCueProof(
    const SubtitleBoxObservation& exactKey,uint64_t templateRevision,
    SubtitleCurrentCueProofReceipt& receipt) {
    using namespace SubtitleCurrentCueWorkerDetail;
    receipt={};std::lock_guard<std::mutex> lock(m_mutex);
    if(!templateRevision)return SubtitleCurrentCueProofStatus::NoTemplate;
    if(templateRevision<m_currentCueMinimumRevision->load(std::memory_order_acquire))
        return SubtitleCurrentCueProofStatus::Expired;
    for(auto i=m_currentCueTasks.rbegin();i!=m_currentCueTasks.rend();++i) {
        auto& entry=**i;
        if(entry.seedTask || entry.revision!=templateRevision || !SameExactTaskKey(entry.key,exactKey))continue;
        if(entry.status==SubtitleCurrentCueProofStatus::Ready) {
            if(!m_currentCueState || m_currentCueState->descriptor.revision!=templateRevision ||
                !InCurrentCueWindow(exactKey.identity)) {
                entry.status=SubtitleCurrentCueProofStatus::Expired;entry.receipt={};
                return SubtitleCurrentCueProofStatus::Expired;
            }
            if(!entry.receipt.Valid() || !entry.receipt.m_consumed ||
                entry.receipt.m_consumed->load(std::memory_order_acquire))return SubtitleCurrentCueProofStatus::Consumed;
            receipt=entry.receipt;
            entry.status=SubtitleCurrentCueProofStatus::Consumed;
            entry.receipt={};
            return SubtitleCurrentCueProofStatus::Ready;
        }
        return entry.status;
    }
    if(!templateRevision || !m_currentCueState)return SubtitleCurrentCueProofStatus::NoTemplate;
    return SubtitleCurrentCueProofStatus::NoTask;
}

inline bool SubtitleMeasurementWorker::MakeCurrentCueProofKey(
    const SubtitleBoxObservation& sourceKey,SubtitlePreviewMode sourceMode,int backgroundMode,
    SubtitleBoxObservation& proofKey,ActivePictureBounds& physicalBase,uint64_t& templateRevision) {
    using namespace SubtitleCurrentCueWorkerDetail;
    proofKey={};physicalBase={};templateRevision=0;
    const auto view=std::atomic_load(&m_currentCueReadView);
    if(!view || !view->descriptor || view->revision<m_currentCueMinimumRevision->load(std::memory_order_acquire) ||
        sourceKey.discontinuity)return false;
    const auto& descriptor=*view->descriptor;
    if(!descriptor.valid || !SameInterpretationContext(descriptor.context,sourceKey) ||
        sourceMode!=descriptor.sourceMode || backgroundMode!=descriptor.backgroundMode ||
        !BuildSubtitleCurrentCueProofKey(sourceKey,sourceKey.identity,descriptor.physicalBase,true,proofKey))
        return false;
    // This is a request to inspect CURRENT pixels against an accepted template,
    // not fresh physical detection or permission to crop. Render must match the
    // returned proof's base to its independently accepted current native base.
    if(std::atomic_load(&m_currentCueReadView)!=view ||
        view->revision<m_currentCueMinimumRevision->load(std::memory_order_acquire)) {proofKey={};return false;}
    physicalBase=descriptor.physicalBase;templateRevision=descriptor.revision;
    return true;
}

inline void SubtitleMeasurementWorker::PublishCompletedCurrentCueProof(
    const std::shared_ptr<SubtitleCurrentCueProofSlot>& slot,
    const std::shared_ptr<const SubtitleCurrentCueProofPublication>& next) {
    if(!slot || !next || !next->receipt.Valid() ||
        next->receipt.m_minimumRevision!=m_currentCueMinimumRevision ||
        !InCurrentCueWindow(next->receipt.CurrentKey().identity))return;
    auto before=std::atomic_load(&slot->published);
    for(unsigned attempt=0;attempt<2;++attempt) {
        if(before && (!SubtitleCurrentCueWorkerDetail::SameExactTaskKey(
                before->receipt.CurrentKey(),next->receipt.CurrentKey()) ||
            before->receipt.m_minimumRevision!=m_currentCueMinimumRevision ||
            before->receipt.TemplateRevision()>next->receipt.TemplateRevision()))return;
        if(std::atomic_compare_exchange_strong(&slot->published,&before,next))return;
    }
}

inline SubtitleCurrentCueProofStatus SubtitleMeasurementWorker::TryReadCompletedCurrentCueProof(
    const SubtitleBoxObservation& exactKey,const SubtitleCurrentCueProofSlot& slot,
    SubtitleCurrentCueProofReceipt& out) {
    using namespace SubtitleCurrentCueWorkerDetail;
    // Do not create a large zero-value receipt temporary on the render stack.
    out.m_valid=false;out.m_consumed.reset();out.m_sourceEquivalence={};
    const auto completed=std::atomic_load(&slot.published);
    if(!completed)return SubtitleCurrentCueProofStatus::NoTask;
    const auto& receipt=completed->receipt;
    if(receipt.m_minimumRevision!=m_currentCueMinimumRevision ||
        !SameExactTaskKey(receipt.CurrentKey(),exactKey))return SubtitleCurrentCueProofStatus::NotEligible;
    const auto compatible=[&](const std::shared_ptr<const SubtitleCurrentCueReadView>& view) {
        // A later failed/changed cue does not revoke a completed earlier source.
        // Preserve SameProofInterpretation, including compatible cue rollover;
        // new work still requires the separate working-valid descriptor flag.
        return view && view->descriptor &&
            view->revision>=m_currentCueMinimumRevision->load(std::memory_order_acquire) &&
            SameProofInterpretation(receipt.CurrentKey(),receipt.PhysicalBase(),completed->sourceLayout,
                receipt.SourceMode(),receipt.BackgroundMode(),*view->descriptor);
    };
    if(!receipt.Valid() || !InCurrentCueWindow(exactKey.identity))return SubtitleCurrentCueProofStatus::Expired;
    if(!compatible(std::atomic_load(&m_currentCueReadView)))return SubtitleCurrentCueProofStatus::NotEligible;
    if(!receipt.m_consumed || receipt.m_consumed->load(std::memory_order_acquire))
        return SubtitleCurrentCueProofStatus::Consumed;
    out=receipt;
    Trace(exactKey,"cue-completed-candidate","completed-source-copy-ready");
    if(!out.Valid() || !InCurrentCueWindow(exactKey.identity) ||
        !compatible(std::atomic_load(&m_currentCueReadView)) ||
        out.m_consumed->load(std::memory_order_acquire)) {
        out.m_valid=false;out.m_consumed.reset();out.m_sourceEquivalence={};
        return SubtitleCurrentCueProofStatus::Expired;
    }
    return SubtitleCurrentCueProofStatus::Ready;
}

inline SubtitleCurrentCueProofStatus SubtitleMeasurementWorker::TryPeekCurrentCueProof(
    const SubtitleBoxObservation& exactKey,SubtitleCurrentCueProofReceipt& receipt,bool nonblocking) {
    using namespace SubtitleCurrentCueWorkerDetail;
    receipt={};std::unique_lock<std::mutex> lock(m_mutex,std::defer_lock);
    if(nonblocking) {if(!lock.try_lock())return SubtitleCurrentCueProofStatus::Pending;}
    else lock.lock();
    if(!HasCurrentCueProofContextLocked())return SubtitleCurrentCueProofStatus::NoTemplate;
    if(!InCurrentCueWindow(exactKey.identity))
        return SubtitleCurrentCueProofStatus::Expired;
    auto pending=SubtitleCurrentCueProofStatus::NoTask;
    for(auto i=m_currentCueTasks.rbegin();i!=m_currentCueTasks.rend();++i) {
        const auto& entry=**i;
        if(entry.seedTask || entry.revision<m_currentCueMinimumRevision->load(std::memory_order_acquire) ||
            !SameExactTaskKey(entry.key,exactKey))continue;
        if(entry.consumed && entry.consumed->load(std::memory_order_acquire))
            return SubtitleCurrentCueProofStatus::Consumed;
        if(!SameProofInterpretation(entry.descriptor,m_currentCueState->descriptor)) {
            if(pending==SubtitleCurrentCueProofStatus::NoTask)pending=SubtitleCurrentCueProofStatus::NotEligible;
            continue;
        }
        if(entry.status==SubtitleCurrentCueProofStatus::Ready && entry.receipt.Valid() &&
            entry.receipt.m_consumed) {
            receipt=entry.receipt;
            return SubtitleCurrentCueProofStatus::Ready;
        }
        // A newer queued proof must not hide an already completed proof of the
        // same source from the preceding compatible template revision.
        if(pending==SubtitleCurrentCueProofStatus::NoTask)pending=entry.status;
    }
    return pending;
}

inline void SubtitleMeasurementWorker::AdvanceCurrentCueRenderWindow(
    const ActivePictureFrameIdentity& head,uint64_t tailSequence,bool advanceHead) {
    using namespace SubtitleCurrentCueWorkerDetail;
    std::vector<std::shared_ptr<AnalysisCompletionSignal>> completions;
    std::vector<Job> retiredJobs;
    std::vector<Work> retiredScans;
    std::vector<SubtitleCurrentCueProofReceipt> retiredReceipts;
    std::vector<std::shared_ptr<SubtitleCurrentCueTaskEntry>> retiredEntries;
    PublishCurrentCueWindow(head,tailSequence,advanceHead);
    {
        std::unique_lock<std::mutex> lock(m_mutex,std::try_to_lock);
        if(!lock.owns_lock()) {m_changed.notify_all();return;}
        const auto window=std::atomic_load(&m_currentCueSourceWindow);
        if(!window)return;
        const auto& effectiveHead=window->head;
        for(auto i=m_currentCuePending.begin();i!=m_currentCuePending.end();) {
            if(!i->currentCueSeedTask && !InWindow(effectiveHead,window->tail,i->key.identity)) {
                if(i->cueEntry)i->cueEntry->status=SubtitleCurrentCueProofStatus::Expired;
                if(i->completion)completions.push_back(i->completion);
                retiredJobs.push_back(std::move(*i));
                i=m_currentCuePending.erase(i);++m_diagnostics.expired;
            } else ++i;
        }
        for(auto& entry:m_currentCueTasks)if(!entry->seedTask &&
            entry->status!=SubtitleCurrentCueProofStatus::Consumed &&
            !InWindow(effectiveHead,window->tail,entry->key.identity)) {
            entry->status=SubtitleCurrentCueProofStatus::Expired;
            retiredReceipts.push_back(std::move(entry->receipt));
            retiredScans.push_back(std::move(entry->deferredFullScan));
            entry->fullScanDue=false;entry->hasDeferredWitness=false;
            ++m_diagnostics.expired;
        }
        while(m_currentCueTasks.size()>8) {
            retiredEntries.push_back(std::move(m_currentCueTasks.front()));m_currentCueTasks.pop_front();
        }
    }
    m_changed.notify_all();
    for(const auto& completion:completions)completion->Notify();
}
