#pragma once

#include "AlphaSourceCropPolicy.h"
#include "ActivePictureDecisionTimeline.h"

namespace AlphaSourceCrop
{
    struct BufferedPictureExpansionSample
    {
        ActivePictureFrameIdentity identity;
        ActivePictureObservation observation;
        ActivePicturePresentationRetentionEvidence retention;
        bool nearBlackEvaluated = false;
        RelativeBarContrastEvidence relativeContrast;
        bool qualifiedAspectPair = false;
    };

    // Use the same trusted/provisional bounds contract as live analysis.
    // This is the actual preview adapter, including its source-frame identity.
    BufferedPictureExpansionSample MakeBufferedPictureSample(
        const ActivePictureFrameIdentity& identity, const ActivePictureEvidence& evidence,
        double framesPerSecond, bool nearBlackEvaluated, bool globalNearBlack);
    bool BufferedPictureReferenceMatches(const ActivePictureFrameIdentity& first,
        uint64_t trustedSourceGeneration, uint64_t trustedSourceFormat,
        uint64_t liveViewportGeneration);

    // Queue/source identity and shader lifetime use independent generation domains.
    bool GuardedRememberedReferenceMatches(const ActivePictureFrameIdentity& first,
        uint64_t trustedSourceGeneration, uint64_t trustedSourceFormat,
        uint64_t liveViewportGeneration, uint64_t capturedShaderGeneration,
        uint64_t currentShaderGeneration);
    enum class MovingPictureTransitionChange : uint8_t
    {
        NONE, ACQUIRED, QUIET_ENDPOINT, DIFFERENT_CROP, ABRUPT_NATIVE, OWNER_OR_CONTEXT
    };
    // A presentation-only hold. It never grants crop or full-raster authority.
    // Only consecutive current source samples may establish or settle motion.
    struct MovingPictureTransitionState
    {
        bool active = false;
        // Keep the established presentation after motion ends until ordinary publication
        // accepts the new crop; this flag never delays the model or queued proof.
        bool awaitingPublication = false;
        // This episode has independently measured physical bars despite a caption.
        // Only this provenance may bridge a later legacy vertical FIT proof gap.
        bool subtitleAssistedBars = false;
        uint64_t subtitleBarPolicyGeneration = 0, subtitleBarContinuityGeneration = 0;
        SubtitleScriptHint subtitleBarScriptHint = static_cast<SubtitleScriptHint>(0);
        // An uncommitted seed may bridge at most two adjacent unknown samples.
        // It never owns presentation; last remains the last proved position.
        uint32_t missingBarProofSamples = 0;
        // Adjacent unknown samples since an active captioned ramp's last
        // certified position; used only to recognize an implausible return jump.
        uint32_t missedActiveBarSamples = 0;
        ActivePictureBounds base, anchor, last, quietAnchor;
        // Current native edge position only. It is not motion or quiet proof.
        // Invalidated by a source gap or missing native vertical bars.
        ActivePictureBounds observedNative;
        uint64_t observedNativeSourceSequence = 0;
        ActivePictureFrameIdentity identity;
        uint32_t directionalChanges = 0;
        uint32_t quietSamples = 0;
        MovingPictureTransitionChange change = MovingPictureTransitionChange::NONE;
    };

    // Advanced subtitle modes keep the picture at its physical native crop.
    // Classic translation state may still be pending for the subtitle route,
    // but it is not a competing picture owner for motion observation. This
    // copy is for ObserveMovingPictureTransition only; admission still uses
    // the original input and its current-pixel checks. FIT and FAIL_OPEN
    // remain competing picture owners in either mode.
    TransitionAdmissionInput PictureMotionTrackingInput(
        const TransitionAdmissionInput& current,
        bool classicPicturePresentationAllowed);

    MovingPictureTransitionState ObserveMovingPictureTransition(
        const MovingPictureTransitionState& previous,
        const ActivePictureFrameIdentity& identity,
        const TransitionAdmissionInput& current);

    // Native motion may use exact-current independently proved subtitle-excluded
    // bars even when the untouched raw proposal includes the caption. This does
    // not alter AR admission, crop authority or subtitle erasure ownership.
    // Advanced modes may suppress legacy vertical FIT for this observation;
    // an established same-context episode can bridge missing proof without
    // gaining directional or quiet credit. Classic and FAIL_OPEN are unchanged.
    MovingPictureTransitionState ObserveSubtitleAssistedMovingPictureTransition(
        const MovingPictureTransitionState& previous,
        const ActivePictureFrameIdentity& identity,
        const TransitionAdmissionInput& current,
        bool classicPicturePresentationAllowed,
        const AnalysisLumaSource& source,
        const SubtitleAssistedVerticalBarProof& subtitleBars,
        uint64_t policyGeneration, uint64_t continuityGeneration,
        SubtitleScriptHint scriptHint);

    void CompleteMovingPictureTransition(MovingPictureTransitionState& state,
        const TransitionAdmissionInput& current,
        const ActivePictureTransitionDecision& transition);

    bool HasCurrentMovingPictureTransition(const MovingPictureTransitionState& state,
        uint64_t sourceGeneration, uint64_t sourceSequence);

    // Apply before both local Observe and queued adoption. The queued proof
    // consumer must also be withheld while state.active is true.
    void ConstrainMovingPictureTransition(const MovingPictureTransitionState& state,
        TransitionAdmissionDecision& admission);

    // Owns no frames and never advances the live confirmation state. The
    // publication always names the first/current frame's exact measurement.
    struct BufferedPictureExpansionProof
    {
        bool valid = false;
        bool relativeContrast = false;
        ActivePictureFrameDecision decision;
    };

    // Diagnostic data deliberately contains no publishable frame decision.
    struct BufferedPictureExpansionDiagnostic
    {
        bool passes = false;
        const char* reason = "not-evaluated";
        int failedSample = -1;
    };

    struct BufferedExpansionShadowTrace
    {
        bool active = false;
        ActivePictureFrameIdentity identity;
        ActivePictureBounds base;
        uint64_t policyGeneration = 0, continuityGeneration = 0;
        uint64_t firstSequence = 0, firstPassSequence = 0, windows = 0, passes = 0;
    };

    // Diagnostic-only bookkeeping; repeated presentations never count twice.
    // Returns true once, at the first passing window in this context.
    bool ObserveBufferedExpansionShadow(BufferedExpansionShadowTrace& trace,
        const ActivePictureFrameIdentity& identity, const ActivePictureBounds& base,
        bool passes, uint64_t policyGeneration = 0, uint64_t continuityGeneration = 0);

    BufferedPictureExpansionDiagnostic InspectBufferedPictureExpansion(
        const BufferedPictureExpansionSample* samples, size_t count,
        const ActivePictureBounds& base, uint8_t configuredLookahead,
        uint8_t availableLookahead, uint64_t continuityGeneration,
        uint64_t policyGeneration);

    BufferedPictureExpansionProof BuildBufferedPictureExpansion(
        const BufferedPictureExpansionSample* samples, size_t count,
        const ActivePictureBounds& base, uint8_t configuredLookahead,
        uint8_t availableLookahead, uint64_t continuityGeneration,
        uint64_t policyGeneration, BufferedPictureExpansionDiagnostic* diagnostic = nullptr);

    // Self-contained inward proof from the current live model. Pending live
    // candidates are discarded; every contributing frame must have identical
    // trusted, non-near-black bounds. Does not mutate live temporal state.
    ActivePictureFrameDecision BuildBufferedInwardDecision(
        const BufferedPictureExpansionSample* samples, size_t count,
        ActivePictureTransitionModel liveModel, const ActivePictureBounds& base,
        uint8_t configuredLookahead, uint8_t availableLookahead,
        uint64_t continuityGeneration, uint64_t policyGeneration);

    // The caller must additionally check the decision against the current
    // timeline continuity/policy generation before calling this consumer.
    // Adoption still goes through ActivePictureTransitionModel's vetoes.
    bool ValidateBufferedPictureExpansion(
        const BufferedPictureExpansionProof& proof,
        const ActivePictureFrameIdentity& currentIdentity,
        const TransitionAdmissionInput& current,
        bool modelWouldAdmit);
    // Explicit handoff for an old subtitle translation; ordinary proof consumers
    // continue to reject all presentation owners.
    bool ValidateBufferedTranslatedPictureExpansion(
        const BufferedPictureExpansionProof& proof,
        const ActivePictureFrameIdentity& currentIdentity,
        const TransitionAdmissionInput& current, bool modelWouldAdmit,
        const ActivePictureBounds& subtitleBase);

    // Alternate proof requires independently qualified history and fresh pixels.
    // Only a confirmed, same-source FIT owner may be retired after adoption.
    bool ValidateBufferedRelativePictureExpansion(
        const BufferedPictureExpansionProof& proof,
        const ActivePictureFrameIdentity& currentIdentity,
        const TransitionAdmissionInput& current, bool modelWouldAdmit,
        bool qualifiedAspectPair, const RelativeBarContrastEvidence& currentRelative,
        const ActivePictureBounds& subtitleBase, uint32_t fitConfirmations = 0);

    // Never retire an owner merely because a preview passed validation.
    void RetireVerticalPresentationForBufferedExpansion(bool adopted,
        VerticalBarPresentationState& presentation, VerticalTranslationDrift& drift,
        OutwardPictureConfirmationState& outward);

}
