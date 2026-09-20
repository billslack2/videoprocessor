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
    };

    // A presentation-only hold. It never grants crop or full-raster authority.
    // Only consecutive current source samples may establish or settle motion.
    struct MovingPictureTransitionState
    {
        bool active = false;
        // Keep full presentation after motion ends until ordinary publication
        // accepts the new crop; this flag never delays the model or queued proof.
        bool awaitingPublication = false;
        ActivePictureBounds base, anchor, last, quietAnchor;
        ActivePictureFrameIdentity identity;
        uint32_t directionalChanges = 0;
        uint32_t quietSamples = 0;
    };

    MovingPictureTransitionState ObserveMovingPictureTransition(
        const MovingPictureTransitionState& previous,
        const ActivePictureFrameIdentity& identity,
        const TransitionAdmissionInput& current);

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
        ActivePictureFrameDecision decision;
    };

    BufferedPictureExpansionProof BuildBufferedPictureExpansion(
        const BufferedPictureExpansionSample* samples, size_t count,
        const ActivePictureBounds& base, uint8_t configuredLookahead,
        uint8_t availableLookahead, uint64_t continuityGeneration,
        uint64_t policyGeneration);

    // The caller must additionally check the decision against the current
    // timeline continuity/policy generation before calling this consumer.
    // Adoption still goes through ActivePictureTransitionModel's vetoes.
    bool ValidateBufferedPictureExpansion(
        const BufferedPictureExpansionProof& proof,
        const ActivePictureFrameIdentity& currentIdentity,
        const TransitionAdmissionInput& current,
        bool modelWouldAdmit);
}
