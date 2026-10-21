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

        // A measured position can guard continuity of an already active hold,
        // but cannot acquire crop authority, prove moving picture pixels or
        // contribute a quiet endpoint sample.
        bool CurrentNativeEdgePosition(const TransitionAdmissionInput& current,
            const ActivePictureBounds& candidate)
        {
            const auto& vertical = current.evidence.axisEvidence.vertical;
            return current.evidence.available &&
                current.evidence.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE &&
                current.evidence.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
                vertical.scanComplete && vertical.state == ActivePictureAxisState::TRUSTED_BARS &&
                current.retention.analysisValid && current.retention.presentationValid &&
                !current.retention.EffectiveArNearBlack() &&
                ValidVerticalBarPicture(candidate) &&
                candidate.left == current.evidence.proposedBounds.left &&
                candidate.top == current.evidence.proposedBounds.top &&
                candidate.right == current.evidence.proposedBounds.right &&
                candidate.bottom == current.evidence.proposedBounds.bottom &&
                candidate.rasterWidth == current.evidence.proposedBounds.rasterWidth &&
                candidate.rasterHeight == current.evidence.proposedBounds.rasterHeight &&
                SameBounds(candidate, current.outwardCandidate);
        }

        bool SameContext(const ActivePictureFrameIdentity& a,
            const ActivePictureFrameIdentity& b)
        {
            return a.transportGeneration == b.transportGeneration &&
                a.sourceFormatGeneration == b.sourceFormatGeneration &&
                a.analysisDomainKey == b.analysisDomainKey &&
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
                retention.EffectiveArNearBlack() || !retention.expansionStripsAvailable ||
                !SameBounds(retention.expansionBase, base) ||
                !SameBounds(retention.expansionCandidate, observation.bounds))
                return false;
            return ConfirmOutwardPictureTransition({}, base, observation.bounds,
                retention, generation, sequence).broadOpposingPicture;
        }
        bool CurrentRelativeEvidence(const ActivePictureBounds& base,
            const ActivePictureObservation& observation,
            const RelativeBarContrastEvidence& pixels, bool qualifiedAspectPair,
            uint64_t generation)
        {
            const auto& axes = observation.axisEvidence;
            const auto& horizontal = axes.horizontal;
            const bool sideAllowed = horizontal.scanComplete &&
                ((horizontal.state == ActivePictureAxisState::FULL_EXTENT_SUPPORTED && !horizontal.barCandidate) ||
                 (horizontal.FailedBar() && horizontal.reason == ActivePictureAxisReason::BAR_EDGE_REJECTED));
            return qualifiedAspectPair && pixels.evaluated && pixels.valid &&
                generation != 0 && pixels.sourceGeneration == generation &&
                SameBounds(pixels.base, base) && SameBounds(pixels.target, observation.bounds) &&
                observation.available && !observation.transitionDeferred &&
                observation.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE &&
                observation.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
                BothEdgesExpand(base, observation.bounds) && sideAllowed &&
                axes.vertical.scanComplete && axes.vertical.state == ActivePictureAxisState::TRUSTED_BARS;
        }

        bool ExactNativeWindowPosition(const BufferedPictureExpansionSample& sample)
        {
            const auto& observation=sample.observation;
            const auto& raw=sample.retention.activePicture;
            return observation.available && !observation.transitionDeferred &&
                observation.authorityOrigin==ActivePictureAuthorityOrigin::NATIVE &&
                observation.classification==ActivePictureClassification::BAR_CROP_TRUSTED &&
                observation.axisEvidence.vertical.scanComplete &&
                observation.axisEvidence.vertical.state==ActivePictureAxisState::TRUSTED_BARS &&
                !observation.axisEvidence.HasFailedBar() &&
                ValidVerticalBarPicture(observation.bounds) && raw.available &&
                raw.authorityOrigin==ActivePictureAuthorityOrigin::NATIVE &&
                raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED &&
                raw.axisEvidence.vertical.scanComplete &&
                raw.axisEvidence.vertical.state==ActivePictureAxisState::TRUSTED_BARS &&
                !raw.axisEvidence.HasFailedBar() && raw.top.trusted && raw.bottom.trusted &&
                SameBounds(raw.trustedBounds,observation.bounds) &&
                raw.proposedBounds.left==observation.bounds.left && raw.proposedBounds.top==observation.bounds.top &&
                raw.proposedBounds.right==observation.bounds.right && raw.proposedBounds.bottom==observation.bounds.bottom &&
                raw.proposedBounds.rasterWidth==observation.bounds.rasterWidth &&
                raw.proposedBounds.rasterHeight==observation.bounds.rasterHeight;
        }

        bool ProgressiveNativeWindow(const BufferedPictureExpansionSample* samples,
            size_t count, const ActivePictureBounds& base)
        {
            if(count<OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED)return false;
            for(size_t i=0;i<count;++i)
            {
                const auto& candidate=samples[i].observation.bounds;
                if(!ExactNativeWindowPosition(samples[i]) || !BothEdgesExpand(base,candidate))return false;
                if(i==0)continue;
                const auto& prior=samples[i-1].observation.bounds;
                const int top=prior.top-candidate.top,bottom=candidate.bottom-prior.bottom;
                const int step=(std::max)(2,candidate.rasterHeight/540);
                if(top<0||bottom<0||(top==0&&bottom==0)||top>step||bottom>step)return false;
            }
            return true;
        }
        bool CurrentScheduledExpansion(const BufferedPictureExpansionProof& proof,
            const ActivePictureFrameIdentity& currentIdentity,
            const TransitionAdmissionInput& current, bool modelWouldAdmit)
        {
            const auto& decision = proof.decision;
            if (!proof.valid || !modelWouldAdmit || !current.compatiblePresentation ||
                !current.trustedGeometryAvailable || current.sourceGeneration == 0 ||
                current.trustedGeneration != current.sourceGeneration ||
                current.sourceGeneration != currentIdentity.transportGeneration ||
                current.sourceSequence != currentIdentity.acceptedSequence ||
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
            return true;
        }
    }

    TransitionAdmissionInput PictureMotionTrackingInput(
        const TransitionAdmissionInput& current,
        bool classicPicturePresentationAllowed)
    {
        auto tracking = current;
        if (!classicPicturePresentationAllowed &&
            (tracking.presentation.action == VerticalBarPresentationAction::NONE ||
             tracking.presentation.action == VerticalBarPresentationAction::TRANSLATE))
        {
            // Advanced subtitle composition does not move the source picture.
            // Keep all native geometry, pixel, identity and scene evidence intact.
            tracking.presentation.action = VerticalBarPresentationAction::NONE;
            tracking.translationDriftActive = false;
            tracking.previousOutward.verticalPresentationSeen = false;
        }
        return tracking;
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
            current.retention.EffectiveArNearBlack() ||
            current.presentation.action != VerticalBarPresentationAction::NONE ||
            current.translationDriftActive || current.previousOutward.verticalPresentationSeen ||
            current.evidence.classification == ActivePictureClassification::FULL_RASTER_TRUSTED)
        {
            MovingPictureTransitionState reset;
            reset.change = MovingPictureTransitionChange::OWNER_OR_CONTEXT;
            return reset;
        }
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
        state.change = MovingPictureTransitionChange::NONE;
        state.onsetPending = false;
        state.base = current.trustedGeometry;
        state.identity = identity;
        const bool nativePosition = CurrentNativeEdgePosition(current, candidate);
        const bool priorNativePosition = previous.active && adjacent &&
            previous.observedNativeSourceSequence != 0 &&
            previous.observedNativeSourceSequence == previous.identity.acceptedSequence &&
            ValidVerticalBarPicture(previous.observedNative);
        // An unknown source or sequence gap cannot bridge a stale edge location.
        state.observedNative = {};
        state.observedNativeSourceSequence = 0;
        if (previous.active && adjacent && nativePosition)
        {
            state.observedNative = candidate;
            state.observedNativeSourceSequence = identity.acceptedSequence;
        }
        const auto observation = MakeActivePictureObservation(current.evidence,
            current.sourceSequence, current.framesPerSecond);
        const bool broadCertified = SameBounds(candidate, current.outwardCandidate) &&
            CurrentBroadEvidence(state.base, observation, current.retention,
                current.sourceGeneration, current.sourceSequence);
        // Strict native distributed support may continue and settle an already
        // active ramp. It cannot start an episode or bypass the ordinary quiet
        // interval before endpoint publication.
        const bool nativeDistributedContinuation = !broadCertified && previous.active && adjacent &&
            current.evidence.available &&
            current.evidence.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE &&
            current.evidence.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
            !current.evidence.axisEvidence.HasFailedBar() &&
            SameBounds(candidate, current.outwardCandidate) &&
            BothEdgesExpand(state.base, candidate) &&
            ConfirmOutwardPictureTransition({}, state.base, candidate, current.retention,
                current.sourceGeneration, current.sourceSequence).nativeDistributedPicture;
        const bool certified = broadCertified || nativeDistributedContinuation;
        // A trusted different crop (including a cut back to the old scope)
        // leaves this outward episode immediately, even without expansion strips.
        if (current.evidence.available &&
            current.evidence.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
            (!BothEdgesExpand(state.base, candidate) ||
             (nativePosition && priorNativePosition &&
              (std::abs(previous.observedNative.top - candidate.top) > (std::max)(4, candidate.rasterHeight / 100) ||
               std::abs(previous.observedNative.bottom - candidate.bottom) > (std::max)(4, candidate.rasterHeight / 100)))))
        {
            const bool keepPresentationHold = state.active || state.awaitingPublication;
            const auto reason = !BothEdgesExpand(state.base, candidate)
                ? MovingPictureTransitionChange::DIFFERENT_CROP
                : MovingPictureTransitionChange::ABRUPT_NATIVE;
            state = {};
            state.change = reason;
            state.awaitingPublication = keepPresentationHold;
            state.base = current.trustedGeometry;
            state.identity = identity;
            if (!certified) return state;
            state.anchor = state.last = state.quietAnchor = candidate;
            return state;
        }
        if (!certified)
        {
            // A ramp near the raster edge may lose bar authority. Keep the
            // established hold, but never count darkness/ambiguity as settled bars.
            // Fresh full authority and unrelated owners were handled above.
            state.quietSamples = 0;
            state.directionalChanges = 0;
            return state.active || state.awaitingPublication ? state : MovingPictureTransitionState{};
        }
        // Keep the exact certified native predecessor for onset, never unknown pixels.
        if (nativePosition)
        {
            state.observedNative = candidate;
            state.observedNativeSourceSequence = identity.acceptedSequence;
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
            // A moving preview can otherwise publish before the longer motion
            // threshold is reached. Defer only this freshly certified small step;
            // static, unknown, abrupt, reversed or non-native samples cannot renew it.
            state.onsetPending = !state.awaitingPublication && nativePosition && adjacent &&
                previous.observedNativeSourceSequence == previous.identity.acceptedSequence &&
                SameBounds(previous.observedNative, previous.last) &&
                topStep >= 0 && bottomStep >= 0 && (topStep > 0 || bottomStep > 0) &&
                topStep <= scanStep && bottomStep <= scanStep;
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
                state.onsetPending = false;
                state.awaitingPublication = false;
                state.change = MovingPictureTransitionChange::ACQUIRED;
                state.observedNative = nativePosition ? candidate : ActivePictureBounds{};
                state.observedNativeSourceSequence = nativePosition ? identity.acceptedSequence : 0;
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
            state.change = MovingPictureTransitionChange::QUIET_ENDPOINT;
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
            !current.retention.EffectiveArNearBlack() && current.retention.excludedBandsPixelSafe &&
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

    bool HasCurrentMovingPictureOnset(const MovingPictureTransitionState& state,
        uint64_t sourceGeneration, uint64_t sourceSequence)
    {
        return state.onsetPending && !state.active && !state.awaitingPublication &&
            sourceGeneration != 0 && sourceSequence != 0 &&
            state.identity.transportGeneration == sourceGeneration &&
            state.identity.acceptedSequence == sourceSequence;
    }
    void ConstrainMovingPictureTransition(const MovingPictureTransitionState& state,
        TransitionAdmissionDecision& admission)
    {
        if (!state.active && !HasCurrentMovingPictureOnset(state,
            state.identity.transportGeneration, admission.observation.frameNumber)) return;
        admission.deferOutward = true;
        admission.observation.transitionDeferred = true;
        admission.observation.classification = ActivePictureClassification::PROVISIONAL;
    }

    BufferedPictureExpansionProof BuildBufferedPictureExpansion(
        const BufferedPictureExpansionSample* samples, size_t count,
        const ActivePictureBounds& base, uint8_t configuredLookahead,
        uint8_t availableLookahead, uint64_t continuityGeneration,
        uint64_t policyGeneration, BufferedPictureExpansionDiagnostic* diagnostic)
    {
        BufferedPictureExpansionProof proof;
        BufferedPictureExpansionDiagnostic details;
        auto rejected = [&](const char* reason, int sample = -1)
        {
            details.reason = reason;
            details.failedSample = sample;
            if (diagnostic) *diagnostic = details;
            return proof;
        };
        constexpr uint8_t required = OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED;
        constexpr uint8_t lead = required - 1;
        if (!samples || count < required || configuredLookahead < lead ||
            availableLookahead < lead || continuityGeneration == 0 || policyGeneration == 0)
            return rejected("budget");
        const auto& first = samples[0];
        if (first.identity.transportGeneration == 0 ||
            first.identity.sourceFormatGeneration == 0 || first.identity.acceptedSequence == 0 ||
            first.identity.acceptedSequence > UINT64_MAX - lead)
            return rejected("first-identity");

        bool relativeReady = true;
        for (uint8_t i = 0; i < required; ++i)
        {
            const auto& sample = samples[i];
            relativeReady = relativeReady && sample.nearBlackEvaluated &&
                !sample.retention.EffectiveArNearBlack() && sample.retention.analysisValid && sample.retention.presentationValid &&
                sample.retention.expansionStripsAvailable &&
                SameBounds(first.observation.bounds, sample.observation.bounds) &&
                CurrentRelativeEvidence(base, sample.observation, sample.relativeContrast,
                    sample.qualifiedAspectPair, first.identity.transportGeneration);
        }
        OutwardPictureConfirmationState confirmation;
        for (uint8_t i = 0; i < required; ++i)
        {
            const auto& sample = samples[i];
            if (!SameContext(first.identity, sample.identity) ||
                sample.identity.acceptedSequence != first.identity.acceptedSequence + i ||
                sample.observation.frameNumber != sample.identity.acceptedSequence)
                return rejected("sample-identity", i);
            if (!relativeReady && (!sample.nearBlackEvaluated ||
                !CurrentBroadEvidence(base, sample.observation, sample.retention,
                    first.identity.transportGeneration, sample.identity.acceptedSequence)))
                return rejected("sample-evidence", i);
            // Accepted sequence continuity is mandatory. Where capture metadata
            // is supplied, repeated or missing source frames cannot count.
            if (i != 0 &&
                ((sample.identity.sourceFrameNumber != 0 || samples[i - 1].identity.sourceFrameNumber != 0) &&
                    (samples[i - 1].identity.sourceFrameNumber == UINT64_MAX ||
                     sample.identity.sourceFrameNumber != samples[i - 1].identity.sourceFrameNumber + 1)))
                return rejected("source-continuity", i);
            if (i != 0 &&
                ((sample.identity.captureTimestamp != 0 || samples[i - 1].identity.captureTimestamp != 0) &&
                    sample.identity.captureTimestamp <= samples[i - 1].identity.captureTimestamp))
                return rejected("timestamp-continuity", i);
            if (!relativeReady)
            {
                const auto result = ConfirmOutwardPictureTransition(confirmation, base,
                    sample.observation.bounds, sample.retention,
                    first.identity.transportGeneration, sample.identity.acceptedSequence);
                confirmation = result.state;
                // A restart means geometry wandered beyond the anchored scan step.
                if (!result.broadOpposingPicture || confirmation.confirmations != i + 1)
                    return rejected("geometry-continuity", i);
            }
        }

        details.passes = true;
        details.reason = relativeReady ? "relative-proof-ready" : "proof-ready";
        if (diagnostic) *diagnostic = details;
        proof.valid = true;
        proof.relativeContrast = relativeReady;
        proof.progressiveNativeEdges = !relativeReady && ProgressiveNativeWindow(samples, required, base);
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
        transition.reason = relativeReady ?
            "buffered known-aspect relative bar contrast confirmed on current and future frames" :
            "buffered broad picture expansion confirmed on current and future frames";
        return proof;
    }

    BufferedPictureExpansionDiagnostic InspectBufferedPictureExpansion(
        const BufferedPictureExpansionSample* samples, size_t count,
        const ActivePictureBounds& base, uint8_t configuredLookahead,
        uint8_t availableLookahead, uint64_t continuityGeneration,
        uint64_t policyGeneration)
    {
        BufferedPictureExpansionDiagnostic diagnostic;
        // Evaluate the exact production certificate, then discard it locally.
        // The caller receives metrics, never a certificate it can publish.
        BuildBufferedPictureExpansion(samples, count, base, configuredLookahead,
            availableLookahead, continuityGeneration, policyGeneration, &diagnostic);
        return diagnostic;
    }

    bool ObserveBufferedExpansionShadow(BufferedExpansionShadowTrace& trace,
        const ActivePictureFrameIdentity& identity, const ActivePictureBounds& base,
        bool passes, uint64_t policyGeneration, uint64_t continuityGeneration)
    {
        if (!trace.active || !SameContext(trace.identity, identity) ||
            !SameBounds(trace.base, base) || trace.policyGeneration != policyGeneration ||
            trace.continuityGeneration != continuityGeneration ||
            identity.acceptedSequence < trace.identity.acceptedSequence)
        {
            trace = {};
            trace.active = true;
            trace.base = base;
            trace.policyGeneration = policyGeneration;
            trace.continuityGeneration = continuityGeneration;
            trace.firstSequence = identity.acceptedSequence;
        }
        else if (identity.acceptedSequence == trace.identity.acceptedSequence)
            return false;
        trace.identity = identity;
        ++trace.windows;
        if (!passes) return false;
        ++trace.passes;
        if (trace.firstPassSequence != 0) return false;
        trace.firstPassSequence = identity.acceptedSequence;
        return true;
    }

    BufferedPictureExpansionSample MakeBufferedPictureSample(
        const ActivePictureFrameIdentity& identity, const ActivePictureEvidence& evidence,
        double framesPerSecond, bool nearBlackEvaluated, bool globalNearBlack)
    {
        BufferedPictureExpansionSample sample;
        sample.identity = identity;
        sample.observation = MakeActivePictureObservation(evidence,
            identity.acceptedSequence, framesPerSecond);
        sample.nearBlackEvaluated = nearBlackEvaluated;
        sample.retention.globalNearBlack = globalNearBlack;
        return sample;
    }

    bool BufferedPictureReferenceMatches(const ActivePictureFrameIdentity& first,
        uint64_t trustedSourceGeneration, uint64_t trustedSourceFormat,
        uint64_t liveViewportGeneration)
    {
        return trustedSourceGeneration != 0 && trustedSourceFormat != 0 &&
            first.acceptedSequence != 0 &&
            first.transportGeneration == trustedSourceGeneration &&
            first.rendererGeneration == trustedSourceGeneration &&
            first.sourceFormatGeneration == trustedSourceFormat &&
            first.viewportGeneration == liveViewportGeneration;
    }

    bool GuardedRememberedReferenceMatches(const ActivePictureFrameIdentity& first,
        uint64_t trustedSourceGeneration, uint64_t trustedSourceFormat,
        uint64_t liveViewportGeneration, uint64_t capturedShaderGeneration,
        uint64_t currentShaderGeneration)
    {
        return BufferedPictureReferenceMatches(first, trustedSourceGeneration,
            trustedSourceFormat, liveViewportGeneration) &&
            capturedShaderGeneration != 0 &&
            // Shader lifetimes and queue/source lifetimes are independent counters.
            capturedShaderGeneration == currentShaderGeneration;
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
                sample.observation.axisEvidence.HasBlockingFailedBar(sample.observation.bounds) ||
                !SameBounds(first.observation.bounds, sample.observation.bounds) ||
                !sample.nearBlackEvaluated || sample.retention.EffectiveArNearBlack())
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
        if (proof.relativeContrast ||
            !CurrentScheduledExpansion(proof, currentIdentity, current, modelWouldAdmit) ||
            current.presentation.action != VerticalBarPresentationAction::NONE ||
            current.translationDriftActive || current.previousOutward.verticalPresentationSeen)
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
    bool ConstrainBufferedMovingPictureOnset(
        const BufferedPictureExpansionProof& proof,
        const ActivePictureFrameIdentity& currentIdentity,
        const TransitionAdmissionInput& current,
        TransitionAdmissionDecision& admission)
    {
        // Geometry eligibility is deliberately not authority here: even a history
        // return must wait while this exact source window independently moves.
        if(!proof.progressiveNativeEdges || proof.relativeContrast ||
            !CurrentScheduledExpansion(proof,currentIdentity,current,true) ||
            admission.observation.frameNumber!=currentIdentity.acceptedSequence ||
            !SameBounds(admission.observation.bounds,current.evidence.trustedBounds))
            return false;
        BufferedPictureExpansionSample sample;
        sample.observation=MakeActivePictureObservation(current.evidence,current.sourceSequence,current.framesPerSecond);
        sample.retention=current.retention;
        if(!ExactNativeWindowPosition(sample) ||
            !CurrentBroadEvidence(current.trustedGeometry,sample.observation,current.retention,
                current.sourceGeneration,current.sourceSequence) ||
            EvaluateTransitionAdmission(current).deferPartialComposition)
            return false;
        admission.deferOutward=true;
        admission.observation.transitionDeferred=true;
        admission.observation.classification=ActivePictureClassification::PROVISIONAL;
        return true;
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

    bool ValidateBufferedRelativePictureExpansion(
        const BufferedPictureExpansionProof& proof,
        const ActivePictureFrameIdentity& currentIdentity,
        const TransitionAdmissionInput& current, bool modelWouldAdmit,
        bool qualifiedAspectPair, const RelativeBarContrastEvidence& currentRelative,
        const ActivePictureBounds& subtitleBase, uint32_t fitConfirmations)
    {
        if (!proof.relativeContrast ||
            !CurrentScheduledExpansion(proof, currentIdentity, current, modelWouldAdmit) ||
            current.translationDriftActive || !current.retention.analysisValid ||
            !current.retention.presentationValid || !current.retention.expansionStripsAvailable ||
            current.retention.EffectiveArNearBlack())
            return false;
        if (current.presentation.action == VerticalBarPresentationAction::FIT)
        {
            if (fitConfirmations < VERTICAL_FIT_CONFIRMATIONS_REQUIRED ||
                current.presentationEvidenceGeneration != current.sourceGeneration ||
                !SameBounds(subtitleBase, current.trustedGeometry)) return false;
        }
        else if (current.presentation.action != VerticalBarPresentationAction::NONE ||
            current.previousOutward.verticalPresentationSeen) return false;
        const auto observation = MakeActivePictureObservation(current.evidence,
            current.sourceSequence, current.framesPerSecond);
        if (!CurrentRelativeEvidence(current.trustedGeometry, observation, currentRelative,
            qualifiedAspectPair, current.sourceGeneration)) return false;
        // Fresh samples span the full depth of both discarded bars. Retirement
        // of the FIT owner belongs to the caller, only after successful adoption.
        return !EvaluateTransitionAdmission(current).deferPartialComposition;
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
