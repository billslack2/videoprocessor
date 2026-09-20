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
