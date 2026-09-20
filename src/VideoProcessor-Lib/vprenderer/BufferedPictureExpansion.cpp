#include <pch.h>

#include "BufferedPictureExpansion.h"

#include <algorithm>

namespace AlphaSourceCrop
{
    namespace
    {
        bool SameBounds(const ActivePictureBounds& a, const ActivePictureBounds& b)
        {
            return a.left == b.left && a.top == b.top &&
                a.right == b.right && a.bottom == b.bottom &&
                a.rasterWidth == b.rasterWidth && a.rasterHeight == b.rasterHeight &&
                a.trustedBarAxes == b.trustedBarAxes;
        }

        bool ValidVerticalBarPicture(const ActivePictureBounds& bounds)
        {
            return bounds.rasterWidth > 0 && bounds.rasterHeight > 0 &&
                bounds.left == 0 && bounds.right == bounds.rasterWidth &&
                bounds.top > 0 && bounds.bottom < bounds.rasterHeight &&
                bounds.bottom > bounds.top &&
                (bounds.top % 2) == 0 && (bounds.bottom % 2) == 0 &&
                bounds.trustedBarAxes == ActivePictureBounds::BarAxes::TOP_BOTTOM;
        }

        bool BothEdgesExpand(const ActivePictureBounds& base,
            const ActivePictureBounds& target)
        {
            return ValidVerticalBarPicture(base) && ValidVerticalBarPicture(target) &&
                base.rasterWidth == target.rasterWidth &&
                base.rasterHeight == target.rasterHeight &&
                target.top < base.top && target.bottom > base.bottom;
        }

        bool SameContext(const ActivePictureFrameIdentity& a,
            const ActivePictureFrameIdentity& b)
        {
            return a.transportGeneration == b.transportGeneration &&
                a.sourceFormatGeneration == b.sourceFormatGeneration &&
                a.viewportGeneration == b.viewportGeneration &&
                a.rendererGeneration == b.rendererGeneration;
        }

        bool CurrentBroadEvidence(const ActivePictureBounds& base,
            const ActivePictureObservation& observation,
            const ActivePicturePresentationRetentionEvidence& retention,
            uint64_t generation, uint64_t sequence)
        {
            if (!observation.available || observation.transitionDeferred ||
                observation.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
                observation.axisEvidence.HasFailedBar() ||
                !BothEdgesExpand(base, observation.bounds) ||
                !retention.analysisValid || !retention.presentationValid ||
                retention.globalNearBlack || !retention.expansionStripsAvailable ||
                !SameBounds(retention.expansionBase, base) ||
                !SameBounds(retention.expansionCandidate, observation.bounds))
                return false;
            return ConfirmOutwardPictureTransition({}, base, observation.bounds,
                retention, generation, sequence).broadOpposingPicture;
        }
    }

    BufferedPictureExpansionProof BuildBufferedPictureExpansion(
        const BufferedPictureExpansionSample* samples, size_t count,
        const ActivePictureBounds& base, uint8_t configuredLookahead,
        uint8_t availableLookahead, uint64_t continuityGeneration,
        uint64_t policyGeneration)
    {
        BufferedPictureExpansionProof proof;
        constexpr uint8_t required = OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED;
        constexpr uint8_t lead = required - 1;
        if (!samples || count < required || configuredLookahead < lead ||
            availableLookahead < lead || continuityGeneration == 0 || policyGeneration == 0)
            return proof;
        const auto& first = samples[0];
        if (first.identity.transportGeneration == 0 ||
            first.identity.sourceFormatGeneration == 0 || first.identity.acceptedSequence == 0 ||
            first.identity.acceptedSequence > UINT64_MAX - lead)
            return proof;

        OutwardPictureConfirmationState confirmation;
        for (uint8_t i = 0; i < required; ++i)
        {
            const auto& sample = samples[i];
            if (!SameContext(first.identity, sample.identity) ||
                sample.identity.acceptedSequence != first.identity.acceptedSequence + i ||
                sample.observation.frameNumber != sample.identity.acceptedSequence ||
                !sample.nearBlackEvaluated ||
                !CurrentBroadEvidence(base, sample.observation, sample.retention,
                    first.identity.transportGeneration, sample.identity.acceptedSequence))
                return proof;
            // Accepted sequence continuity is mandatory. Where capture metadata
            // is supplied, repeated or missing source frames cannot count.
            if (i != 0 &&
                ((sample.identity.sourceFrameNumber != 0 || samples[i - 1].identity.sourceFrameNumber != 0) &&
                    (samples[i - 1].identity.sourceFrameNumber == UINT64_MAX ||
                     sample.identity.sourceFrameNumber != samples[i - 1].identity.sourceFrameNumber + 1)))
                return proof;
            if (i != 0 &&
                ((sample.identity.captureTimestamp != 0 || samples[i - 1].identity.captureTimestamp != 0) &&
                    sample.identity.captureTimestamp <= samples[i - 1].identity.captureTimestamp))
                return proof;
            const auto result = ConfirmOutwardPictureTransition(confirmation, base,
                sample.observation.bounds, sample.retention,
                first.identity.transportGeneration, sample.identity.acceptedSequence);
            confirmation = result.state;
            // A restart means geometry wandered beyond the anchored scan step.
            if (!result.broadOpposingPicture || confirmation.confirmations != i + 1)
                return proof;
        }

        proof.valid = true;
        auto& decision = proof.decision;
        decision.observationIdentity = samples[required - 1].identity;
        decision.effectiveIdentity = first.identity;
        decision.configuredLookahead = (std::min)(configuredLookahead,
            ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES);
        decision.availableLookahead = availableLookahead;
        decision.effectiveLookahead = (std::min)(decision.configuredLookahead, availableLookahead);
        decision.proofFrameCount = required;
        decision.continuityGeneration = continuityGeneration;
        decision.lookaheadPolicyGeneration = policyGeneration;
        decision.association = ActivePictureDecisionAssociation::OUTWARD;
        auto& transition = decision.transition;
        transition.state = ActivePictureTransitionState::STABLE;
        transition.bounds = first.observation.bounds;
        transition.stableBounds = base;
        transition.publish = true;
        transition.stable = true;
        transition.diagnostic = true;
        transition.clearTransition = false;
        transition.authoritativeClassification = ActivePictureClassification::BAR_CROP_TRUSTED;
        transition.matchingCandidates = required;
        transition.contradictoryCandidates = required;
        transition.confidence = 1.0;
        transition.firstContradictoryFrame = first.observation.frameNumber;
        transition.decisionLatencyFrames = lead;
        transition.reason = "buffered broad picture expansion confirmed on current and future frames";
        return proof;
    }

    bool ValidateBufferedPictureExpansion(
        const BufferedPictureExpansionProof& proof,
        const ActivePictureFrameIdentity& currentIdentity,
        const TransitionAdmissionInput& current,
        bool modelWouldAdmit)
    {
        const auto& decision = proof.decision;
        if (!proof.valid || !modelWouldAdmit || !current.compatiblePresentation ||
            !current.trustedGeometryAvailable || current.sourceGeneration == 0 ||
            current.trustedGeneration != current.sourceGeneration ||
            current.sourceGeneration != currentIdentity.transportGeneration ||
            current.sourceSequence != currentIdentity.acceptedSequence ||
            current.presentation.action != VerticalBarPresentationAction::NONE ||
            current.translationDriftActive || current.previousOutward.verticalPresentationSeen ||
            decision.proofFrameCount != OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED ||
            decision.association != ActivePictureDecisionAssociation::OUTWARD ||
            decision.transition.clearTransition ||
            decision.transition.authoritativeClassification != ActivePictureClassification::BAR_CROP_TRUSTED ||
            decision.effectiveIdentity.acceptedSequence > UINT64_MAX - (OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED - 1) ||
            decision.observationIdentity.acceptedSequence != decision.effectiveIdentity.acceptedSequence +
                OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED - 1 ||
            decision.configuredLookahead > ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES ||
            decision.effectiveLookahead != (std::min)(decision.configuredLookahead, decision.availableLookahead) ||
            decision.effectiveLookahead < OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED - 1 ||
            decision.continuityGeneration == 0 || decision.lookaheadPolicyGeneration == 0 ||
            !SameBounds(decision.transition.stableBounds, current.presentationBeforeObservation) ||
            !SameBounds(decision.transition.stableBounds, current.trustedGeometry) ||
            !SameBounds(decision.transition.bounds, current.outwardCandidate) ||
            ValidateActivePictureScheduledDecision(decision, currentIdentity,
                current.evidence.trustedBounds, current.evidence.classification) !=
                    ActivePictureScheduledDecisionValidation::ACCEPTED)
            return false;
        const auto observation = MakeActivePictureObservation(current.evidence,
            current.sourceSequence, current.framesPerSecond);
        if (!CurrentBroadEvidence(decision.transition.stableBounds, observation,
            current.retention, current.sourceGeneration, current.sourceSequence))
            return false;
        // Preserve the composition veto. Only temporal outward deferral is
        // satisfied by this independent complete proof.
        return !EvaluateTransitionAdmission(current).deferPartialComposition;
    }
}
