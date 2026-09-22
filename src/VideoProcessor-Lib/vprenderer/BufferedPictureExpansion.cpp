#include <pch.h>

#include "BufferedPictureExpansion.h"

#include <algorithm>
#include <cmath>

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

        bool ContainsBounds(const ActivePictureBounds& outer, const ActivePictureBounds& inner)
        {
            return inner.rasterWidth == outer.rasterWidth && inner.rasterHeight == outer.rasterHeight &&
                inner.left >= outer.left && inner.top >= outer.top &&
                inner.right <= outer.right && inner.bottom <= outer.bottom &&
                inner.right > inner.left && inner.bottom > inner.top;
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

    MovingPictureTransitionState ObserveMovingPictureTransition(
        const MovingPictureTransitionState& previous,
        const ActivePictureFrameIdentity& identity,
        const TransitionAdmissionInput& current)
    {
        const auto& candidate = current.evidence.trustedBounds;
        const bool context = SameContext(previous.identity, identity) &&
            SameBounds(previous.base, current.trustedGeometry);
        // An old/relabelled capture cannot rewind the episode or contribute
        // quiet proof. Context changes are handled separately below.
        if (context && previous.identity.acceptedSequence != 0 && identity.acceptedSequence != 0 &&
            identity.acceptedSequence <= previous.identity.acceptedSequence)
            return previous;
        if (!current.compatiblePresentation || !current.trustedGeometryAvailable ||
            current.sourceGeneration == 0 || current.trustedGeneration != current.sourceGeneration ||
            current.sourceGeneration != identity.transportGeneration ||
            current.sourceSequence == 0 || current.sourceSequence != identity.acceptedSequence ||
            !SameBounds(current.presentationBeforeObservation, current.trustedGeometry) ||
            current.retention.globalNearBlack ||
            current.presentation.action != VerticalBarPresentationAction::NONE ||
            current.translationDriftActive || current.previousOutward.verticalPresentationSeen ||
            current.evidence.classification == ActivePictureClassification::FULL_RASTER_TRUSTED)
            return {};
        const bool adjacent = context && previous.identity.acceptedSequence != UINT64_MAX &&
            identity.acceptedSequence == previous.identity.acceptedSequence + 1 &&
            ((identity.sourceFrameNumber == 0 && previous.identity.sourceFrameNumber == 0) ||
             (previous.identity.sourceFrameNumber != UINT64_MAX &&
              identity.sourceFrameNumber == previous.identity.sourceFrameNumber + 1)) &&
            ((identity.captureTimestamp == 0 && previous.identity.captureTimestamp == 0) ||
             identity.captureTimestamp > previous.identity.captureTimestamp);
        // A gap invalidates proof, but must not re-enable intermediate crops
        // in an already established motion episode of the same context.
        auto state = context && (adjacent || previous.active || previous.awaitingPublication)
            ? previous : MovingPictureTransitionState{};
        if (!adjacent) { state.directionalChanges = 0; state.quietSamples = 0; }
        state.base = current.trustedGeometry;
        state.identity = identity;
        const auto observation = MakeActivePictureObservation(current.evidence,
            current.sourceSequence, current.framesPerSecond);
        const bool certified = SameBounds(candidate, current.outwardCandidate) &&
            CurrentBroadEvidence(state.base, observation, current.retention,
                current.sourceGeneration, current.sourceSequence);
        // A trusted different crop (including a cut back to the old scope)
        // leaves this outward episode immediately, even without expansion strips.
        if (current.evidence.available &&
            current.evidence.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
            (!BothEdgesExpand(state.base, candidate) ||
             (adjacent && previous.last.rasterHeight != 0 &&
              (std::abs(previous.last.top - candidate.top) > (std::max)(4, candidate.rasterHeight / 100) ||
               std::abs(previous.last.bottom - candidate.bottom) > (std::max)(4, candidate.rasterHeight / 100)))))
        {
            const bool keepFull = state.active || state.awaitingPublication;
            state = {};
            state.awaitingPublication = keepFull;
            state.base = current.trustedGeometry;
            state.identity = identity;
            if (!certified) return state;
            state.anchor = state.last = state.quietAnchor = candidate;
            return state;
        }
        if (!certified)
        {
            // A ramp near the raster edge may lose bar authority. It cannot
            // regain the old crop or count darkness/ambiguity as settled bars.
            // Fresh full authority and unrelated owners were handled above.
            state.quietSamples = 0;
            state.directionalChanges = 0;
            return state.active || state.awaitingPublication ? state : MovingPictureTransitionState{};
        }
        if (!adjacent || previous.last.rasterHeight == 0)
        {
            state.anchor = state.last = state.quietAnchor = candidate;
            return state;
        }
        const int topStep = previous.last.top - candidate.top;
        const int bottomStep = candidate.bottom - previous.last.bottom;
        const int scanStep = (std::max)(2, candidate.rasterHeight / 540);
        state.last = candidate;
        if (!state.active)
        {
            if (topStep < 0 || bottomStep < 0)
            {
                state.anchor = candidate;
                state.directionalChanges = 0;
            }
            else if (topStep > 0 || bottomStep > 0)
                ++state.directionalChanges;
            if (state.directionalChanges >= 3 &&
                state.anchor.top - candidate.top > 2 * scanStep &&
                candidate.bottom - state.anchor.bottom > 2 * scanStep)
            {
                state.active = true;
                state.awaitingPublication = false;
                state.quietAnchor = candidate;
                state.quietSamples = 1;
            }
            return state;
        }
        // Anchor the quiet band; repeated sub-step movement cannot walk it.
        if (std::abs(candidate.top - state.quietAnchor.top) > scanStep ||
            std::abs(candidate.bottom - state.quietAnchor.bottom) > scanStep)
        {
            state.quietAnchor = candidate;
            state.quietSamples = 1;
        }
        else
            ++state.quietSamples;
        const double fps = std::isfinite(current.framesPerSecond) && current.framesPerSecond > 0
            ? current.framesPerSecond : 60.0;
        const auto required = static_cast<uint32_t>((std::max)(2.0, std::ceil(fps * 0.250) + 1));
        if (state.quietSamples >= required)
        {
            state.active = false;
            state.awaitingPublication = true;
            state.quietSamples = 0;
            // A stationary endpoint must not reuse the finished ramp's motion
            // count on the next frame while local publication is confirming.
            state.directionalChanges = 0;
            state.anchor = candidate;
        }
        return state;
    }

    void CompleteMovingPictureTransition(MovingPictureTransitionState& state,
        const TransitionAdmissionInput& current,
        const ActivePictureTransitionDecision& transition)
    {
        if (!state.awaitingPublication || state.active ||
            !HasCurrentMovingPictureTransition(state, current.sourceGeneration, current.sourceSequence))
            return;
        // Publication has already passed normal live/queued admission. The
        // model may also deliberately retain a pixel-safe crop within its
        // deadband; do not wait forever for a publication it need not produce.
        const bool retainedCrop = current.evidence.available &&
            current.evidence.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
            current.retention.analysisValid && current.retention.presentationValid &&
            !current.retention.globalNearBlack && current.retention.excludedBandsPixelSafe &&
            SameBounds(state.base, current.trustedGeometry) &&
            (ContainsBounds(current.trustedGeometry, current.evidence.trustedBounds) ||
             IsPixelSafeCropReaffirmation(current.trustedGeometry, current.evidence.trustedBounds, true));
        if ((transition.publish && transition.stable && !transition.clearTransition) || retainedCrop)
            state = {};
    }

    bool HasCurrentMovingPictureTransition(const MovingPictureTransitionState& state,
        uint64_t sourceGeneration, uint64_t sourceSequence)
    {
        return (state.active || state.awaitingPublication) && sourceGeneration != 0 && sourceSequence != 0 &&
            state.identity.transportGeneration == sourceGeneration &&
            state.identity.acceptedSequence == sourceSequence;
    }

    void ConstrainMovingPictureTransition(const MovingPictureTransitionState& state,
        TransitionAdmissionDecision& admission)
    {
        if (!state.active) return;
        admission.deferOutward = true;
        admission.observation.transitionDeferred = true;
        admission.observation.classification = ActivePictureClassification::PROVISIONAL;
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

    ActivePictureFrameDecision BuildBufferedInwardDecision(
        const BufferedPictureExpansionSample* samples, size_t count,
        ActivePictureTransitionModel liveModel, const ActivePictureBounds& base,
        uint8_t configuredLookahead, uint8_t availableLookahead,
        uint64_t continuityGeneration, uint64_t policyGeneration)
    {
        ActivePictureFrameDecision result;
        const uint8_t budget = (std::min)((std::min)(configuredLookahead,
            ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES), availableLookahead);
        if (!samples || count < 2 || budget == 0 ||
            continuityGeneration == 0 || policyGeneration == 0) return result;
        const auto& first = samples[0];
        ActivePictureTransitionDecision geometry;
        geometry.stableBounds = base;
        geometry.bounds = first.observation.bounds;
        if (!IsExactInwardActivePictureAssociationGeometry(geometry,
                first.observation.classification) ||
            first.identity.transportGeneration == 0 ||
            first.identity.sourceFormatGeneration == 0 || first.identity.acceptedSequence == 0 ||
            first.identity.acceptedSequence > UINT64_MAX - budget) return result;
        // A cut may invalidate pre-window candidates before the live consumer.
        // Retain established geometry/deadbands, but prove this window afresh.
        liveModel.ResetCandidateEvidence();
        count = (std::min)(count, size_t(budget) + 1);
        for (size_t i = 0; i < count; ++i)
        {
            const auto& sample = samples[i];
            if (!SameContext(first.identity, sample.identity) ||
                sample.identity.acceptedSequence != first.identity.acceptedSequence + i ||
                sample.observation.frameNumber != sample.identity.acceptedSequence ||
                !sample.observation.available || sample.observation.transitionDeferred ||
                sample.observation.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
                sample.observation.axisEvidence.HasFailedBar() ||
                !SameBounds(first.observation.bounds, sample.observation.bounds) ||
                !sample.nearBlackEvaluated || sample.retention.globalNearBlack)
                return {};
            if (i != 0 &&
                ((sample.identity.sourceFrameNumber != 0 || samples[i-1].identity.sourceFrameNumber != 0) &&
                 (samples[i-1].identity.sourceFrameNumber == UINT64_MAX ||
                  sample.identity.sourceFrameNumber != samples[i-1].identity.sourceFrameNumber + 1))) return {};
            if (i != 0 &&
                ((sample.identity.captureTimestamp != 0 || samples[i-1].identity.captureTimestamp != 0) &&
                 sample.identity.captureTimestamp <= samples[i-1].identity.captureTimestamp)) return {};
            // Established live crops are inspected on every source frame.
            // Match that cadence using already-buffered evidence, not the
            // sparse acquisition cadence used when no crop is established.
            const auto transition = liveModel.Observe(sample.observation);
            if (!transition.publish) continue;
            if (i == 0 || !SameBounds(transition.stableBounds, base) ||
                !SameBounds(transition.bounds, first.observation.bounds) ||
                !IsExactInwardActivePictureAssociationGeometry(transition,
                    sample.observation.classification)) return {};
            result.transition = transition;
            result.effectiveIdentity = first.identity;
            result.observationIdentity = sample.identity;
            result.configuredLookahead = (std::min)(configuredLookahead,
                ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES);
            result.availableLookahead = availableLookahead;
            result.effectiveLookahead = budget;
            result.proofFrameCount = static_cast<uint8_t>(i + 1);
            result.continuityGeneration = continuityGeneration;
            result.lookaheadPolicyGeneration = policyGeneration;
            result.association = ActivePictureDecisionAssociation::EXACT_INWARD;
            result.inwardProof = ActivePictureInwardProofValidation::ACCEPTED;
            return result;
        }
        return result;
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
    bool ValidateBufferedTranslatedPictureExpansion(
        const BufferedPictureExpansionProof& proof,
        const ActivePictureFrameIdentity& currentIdentity,
        const TransitionAdmissionInput& current, bool modelWouldAdmit,
        const ActivePictureBounds& subtitleBase)
    {
        if (current.presentation.action != VerticalBarPresentationAction::TRANSLATE ||
            current.sourceGeneration == 0 ||
            current.presentationEvidenceGeneration != current.sourceGeneration ||
            !SameBounds(subtitleBase, current.trustedGeometry))
            return false;
        // The complete broad-picture certificate replaces only the old owner
        // veto. All pixel, identity, composition and model gates remain intact.
        auto unowned = current;
        unowned.presentation.action = VerticalBarPresentationAction::NONE;
        unowned.translationDriftActive = false;
        unowned.previousOutward.verticalPresentationSeen = false;
        return ValidateBufferedPictureExpansion(proof, currentIdentity, unowned,
            modelWouldAdmit);
    }

    void RetireVerticalPresentationForBufferedExpansion(bool adopted,
        VerticalBarPresentationState& presentation, VerticalTranslationDrift& drift,
        OutwardPictureConfirmationState& outward)
    {
        if (!adopted) return;
        presentation = {};
        drift.Reset();
        outward = {};
    }

}
