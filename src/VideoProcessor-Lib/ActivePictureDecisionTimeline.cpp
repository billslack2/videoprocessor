#include <pch.h>

#include "ActivePictureDecisionTimeline.h"

#include <algorithm>

namespace
{
bool ContainsBounds(
	const ActivePictureBounds& outer,
	const ActivePictureBounds& inner)
{
	return outer.rasterWidth > 0 && outer.rasterHeight > 0 &&
		outer.rasterWidth == inner.rasterWidth &&
		outer.rasterHeight == inner.rasterHeight &&
		outer.left <= inner.left && outer.top <= inner.top &&
		outer.right >= inner.right && outer.bottom >= inner.bottom;
}

bool SameDecisionContext(
	const ActivePictureFrameIdentity& identity,
	const ActivePictureFrameIdentity& current)
{
	return identity.transportGeneration == current.transportGeneration &&
		identity.sourceFormatGeneration == current.sourceFormatGeneration &&
		identity.viewportGeneration == current.viewportGeneration &&
		identity.rendererGeneration == current.rendererGeneration;
}

bool SameTrustedBounds(
	const ActivePictureBounds& left,
	const ActivePictureBounds& right)
{
	return left.left == right.left && left.top == right.top &&
		left.right == right.right && left.bottom == right.bottom &&
		left.rasterWidth == right.rasterWidth &&
		left.rasterHeight == right.rasterHeight &&
		left.trustedBarAxes == right.trustedBarAxes;
}


bool SameLookaheadObservation(
	const ActivePictureObservation& left,
	const ActivePictureObservation& right)
{
	return left.frameNumber == right.frameNumber &&
		left.available == right.available &&
		left.classification == right.classification &&
		SameTrustedBounds(left.bounds, right.bounds);
}

}


bool SameActivePictureFrameIdentity(
	const ActivePictureFrameIdentity& left,
	const ActivePictureFrameIdentity& right)
{
	return left.transportGeneration == right.transportGeneration &&
		left.acceptedSequence == right.acceptedSequence &&
		left.sourceFrameNumber == right.sourceFrameNumber &&
		left.captureTimestamp == right.captureTimestamp &&
		left.sourceFormatGeneration == right.sourceFormatGeneration &&
		left.viewportGeneration == right.viewportGeneration &&
		left.rendererGeneration == right.rendererGeneration;
}


ActivePictureScheduledDecisionValidation
ValidateActivePictureScheduledDecision(
	const ActivePictureFrameDecision& decision,
	const ActivePictureFrameIdentity& currentIdentity,
	const ActivePictureBounds& currentTrustedBounds,
	ActivePictureClassification currentClassification)
{
	const bool trustedClassification = currentClassification ==
		ActivePictureClassification::FULL_RASTER_TRUSTED ||
		currentClassification ==
			ActivePictureClassification::BAR_CROP_TRUSTED;
	if (!decision.transition.publish || !decision.transition.stable ||
		!trustedClassification)
	{
		return ActivePictureScheduledDecisionValidation::NON_AUTHORITATIVE;
	}
	if (!SameActivePictureFrameIdentity(
		decision.effectiveIdentity, currentIdentity))
	{
		return ActivePictureScheduledDecisionValidation::
			EFFECTIVE_IDENTITY_MISMATCH;
	}
	if (!SameDecisionContext(decision.observationIdentity, currentIdentity))
	{
		return ActivePictureScheduledDecisionValidation::
			OBSERVATION_CONTEXT_MISMATCH;
	}
	if (!SameTrustedBounds(
		decision.transition.bounds, currentTrustedBounds))
	{
		return ActivePictureScheduledDecisionValidation::
			TRUSTED_BOUNDS_MISMATCH;
	}
	return ActivePictureScheduledDecisionValidation::ACCEPTED;
}


const char* ActivePictureScheduledDecisionValidationName(
	ActivePictureScheduledDecisionValidation validation)
{
	switch (validation)
	{
	case ActivePictureScheduledDecisionValidation::ACCEPTED:
		return "accepted";
	case ActivePictureScheduledDecisionValidation::NON_AUTHORITATIVE:
		return "non_authoritative";
	case ActivePictureScheduledDecisionValidation::
		EFFECTIVE_IDENTITY_MISMATCH:
		return "effective_identity_mismatch";
	case ActivePictureScheduledDecisionValidation::
		OBSERVATION_CONTEXT_MISMATCH:
		return "observation_context_mismatch";
	case ActivePictureScheduledDecisionValidation::TRUSTED_BOUNDS_MISMATCH:
		return "trusted_bounds_mismatch";
	default:
		return "unknown";
	}
}


const char* ActivePictureDecisionAssociationName(
	ActivePictureDecisionAssociation association)
{
	switch (association)
	{
	case ActivePictureDecisionAssociation::CONFIRMATION:
		return "confirmation";
	case ActivePictureDecisionAssociation::OUTWARD:
		return "outward";
	case ActivePictureDecisionAssociation::EXACT_INWARD:
		return "exact_inward";
	default:
		return "unknown";
	}
}


const char* ActivePictureInwardProofValidationName(
	ActivePictureInwardProofValidation validation)
{
	switch (validation)
	{
	case ActivePictureInwardProofValidation::NOT_APPLICABLE:
		return "not_applicable";
	case ActivePictureInwardProofValidation::ACCEPTED:
		return "accepted";
	case ActivePictureInwardProofValidation::CANDIDATE_UNAVAILABLE:
		return "candidate_unavailable";
	case ActivePictureInwardProofValidation::CANDIDATE_CONSUMED:
		return "candidate_consumed";
	case ActivePictureInwardProofValidation::CONTINUITY_MISMATCH:
		return "continuity_mismatch";
	case ActivePictureInwardProofValidation::CONTEXT_MISMATCH:
		return "context_mismatch";
	case ActivePictureInwardProofValidation::INSUFFICIENT_LEAD:
		return "insufficient_lead";
	case ActivePictureInwardProofValidation::MISSING_IDENTITY:
		return "missing_identity";
	case ActivePictureInwardProofValidation::MISSING_EVIDENCE:
		return "missing_evidence";
	case ActivePictureInwardProofValidation::EVIDENCE_NOT_TRUSTED:
		return "evidence_not_trusted";
	case ActivePictureInwardProofValidation::EVIDENCE_NEAR_BLACK:
		return "evidence_near_black";
	case ActivePictureInwardProofValidation::EVIDENCE_BOUNDS_MISMATCH:
		return "evidence_bounds_mismatch";
	default:
		return "unknown";
	}
}


bool IsExactInwardActivePictureAssociationGeometry(
	const ActivePictureTransitionDecision& transition,
	ActivePictureClassification classification)
{
	return classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
		(transition.bounds.trustedBarAxes ==
				ActivePictureBounds::BarAxes::TOP_BOTTOM ||
			transition.bounds.trustedBarAxes ==
				ActivePictureBounds::BarAxes::LEFT_RIGHT) &&
		transition.stableBounds.trustedBarAxes ==
			transition.bounds.trustedBarAxes &&
		(transition.bounds.trustedBarAxes ==
				ActivePictureBounds::BarAxes::TOP_BOTTOM
			? transition.bounds.left == transition.stableBounds.left &&
				transition.bounds.right == transition.stableBounds.right
			: transition.bounds.top == transition.stableBounds.top &&
				transition.bounds.bottom == transition.stableBounds.bottom) &&
		ContainsBounds(transition.stableBounds, transition.bounds) &&
		!SameTrustedBounds(transition.stableBounds, transition.bounds);
}


void ActivePictureDecisionTimeline::AdvanceContinuityGeneration()
{
	++m_continuityGeneration;
	if (m_continuityGeneration == 0)
		++m_continuityGeneration;
}


void ActivePictureDecisionTimeline::Reset(uint64_t transportGeneration)
{
	m_transition.Reset();
	m_observed.clear();
	m_lookaheadEvidence.clear();
	m_accepted.clear();
	m_continuityBoundaries.clear();
	AdvanceContinuityGeneration();
	m_transportGeneration = transportGeneration;
	m_lastAcceptedSequence = 0;
	m_lastConsumedSequence = 0;
	m_hasAcceptedSequence = false;
	m_lastAcceptedIdentity = {};
}


bool ActivePictureDecisionTimeline::TrackAcceptedFrame(
	const ActivePictureFrameIdentity& identity)
{
	if (identity.transportGeneration < m_transportGeneration)
		return false;
	if (identity.transportGeneration > m_transportGeneration)
		Reset(identity.transportGeneration);
	if (m_hasAcceptedSequence)
	{
		if (identity.acceptedSequence <= m_lastAcceptedSequence)
			return SameActivePictureFrameIdentity(
				identity, m_lastAcceptedIdentity);
		if (identity.sourceFormatGeneration !=
			m_lastAcceptedIdentity.sourceFormatGeneration)
		{
			m_transition.Reset();
			m_observed.clear();
			m_lookaheadEvidence.clear();
			RecordContinuityBoundary(identity.acceptedSequence);
		}
		else if (identity.viewportGeneration !=
				m_lastAcceptedIdentity.viewportGeneration ||
			identity.rendererGeneration !=
				m_lastAcceptedIdentity.rendererGeneration)
		{
			BreakContinuityAtSequence(identity.acceptedSequence);
		}
		if (identity.acceptedSequence != m_lastAcceptedSequence + 1)
			BreakContinuityAtSequence(identity.acceptedSequence);
	}
	m_lastAcceptedSequence = identity.acceptedSequence;
	m_hasAcceptedSequence = true;
	m_lastAcceptedIdentity = identity;
	m_accepted.push_back({ identity, m_continuityGeneration });
	while (m_accepted.size() > MAX_RETAINED_IDENTITIES)
		m_accepted.pop_front();
	return true;
}


void ActivePictureDecisionTimeline::MarkConsumed(
	const ActivePictureFrameIdentity& identity)
{
	if (identity.transportGeneration != m_transportGeneration)
		return;
	m_lastConsumedSequence = std::max(
		m_lastConsumedSequence, identity.acceptedSequence);
	while (!m_observed.empty() &&
		m_observed.front().identity.acceptedSequence <= m_lastConsumedSequence)
		m_observed.pop_front();
	while (!m_lookaheadEvidence.empty() &&
		m_lookaheadEvidence.front().identity.acceptedSequence <=
			m_lastConsumedSequence)
	{
		m_lookaheadEvidence.pop_front();
	}
	while (!m_accepted.empty() &&
		m_accepted.front().identity.acceptedSequence <=
			m_lastConsumedSequence)
		m_accepted.pop_front();
}


void ActivePictureDecisionTimeline::MarkDiscarded(
	const ActivePictureFrameIdentity& identity,
	uint64_t /* detectorFrameNumber */)
{
	if (identity.transportGeneration != m_transportGeneration)
		return;
	BreakContinuityAtSequence(identity.acceptedSequence);
	MarkConsumed(identity);
}


void ActivePictureDecisionTimeline::BreakContinuity(
	uint64_t /* detectorFrameNumber */)
{
	const uint64_t acceptedSequence = m_hasAcceptedSequence &&
		m_lastAcceptedSequence != UINT64_MAX
		? m_lastAcceptedSequence + 1 : m_lastAcceptedSequence;
	BreakContinuityAtSequence(acceptedSequence);
}


void ActivePictureDecisionTimeline::BreakContinuityAtSequence(
	uint64_t acceptedSequence)
{
	// A continuity break invalidates partial proof but is not itself a decoded
	// source observation. Resetting candidate evidence lets the current real
	// frame contribute once while retaining the last affirmative geometry.
	m_transition.ResetCandidateEvidence();
	m_observed.clear();
	m_lookaheadEvidence.clear();
	RecordContinuityBoundary(acceptedSequence);
}


void ActivePictureDecisionTimeline::RecordContinuityBoundary(
	uint64_t acceptedSequence)
{
	const uint64_t beforeGeneration = m_continuityGeneration;
	AdvanceContinuityGeneration();
	m_continuityBoundaries.push_back(
		{ beforeGeneration, m_continuityGeneration, acceptedSequence });
	while (m_continuityBoundaries.size() > MAX_RETAINED_IDENTITIES)
		m_continuityBoundaries.pop_front();
}


void ActivePictureDecisionTimeline::InvalidateLookaheadPolicy(
	bool resetPartialEvidence)
{
	// Every live depth change invalidates queued decisions. Positive-to-positive
	// tuning retains depth-independent pixel evidence and detector votes. A zero
	// crossing starts a new preview session because no observations exist for
	// the disabled interval.
	if (resetPartialEvidence)
	{
		m_transition.ResetCandidateEvidence();
		m_observed.clear();
		m_lookaheadEvidence.clear();
	}
	++m_lookaheadPolicyGeneration;
	if (m_lookaheadPolicyGeneration == 0)
		++m_lookaheadPolicyGeneration;
}


bool ActivePictureDecisionTimeline::TrackLookaheadEvidence(
	const ActivePictureFrameIdentity& identity,
	const ActivePictureObservation& observation,
	bool nearBlackEvaluated,
	bool nearBlack)
{
	AcceptedIdentity accepted;
	if (identity.transportGeneration != m_transportGeneration ||
		identity.acceptedSequence <= m_lastConsumedSequence ||
		!FindAcceptedIdentity(identity, accepted) ||
		accepted.continuityGeneration != m_continuityGeneration)
	{
		return false;
	}
	for (const LookaheadEvidence& existing : m_lookaheadEvidence)
	{
		if (existing.identity.acceptedSequence != identity.acceptedSequence)
			continue;
		return SameActivePictureFrameIdentity(existing.identity, identity) &&
			SameLookaheadObservation(existing.observation, observation) &&
			existing.nearBlackEvaluated == nearBlackEvaluated &&
			existing.nearBlack == nearBlack;
	}
	m_lookaheadEvidence.push_back(
		{ identity, observation, nearBlackEvaluated, nearBlack });
	while (m_lookaheadEvidence.size() > MAX_RETAINED_IDENTITIES)
		m_lookaheadEvidence.pop_front();
	return true;
}


bool ActivePictureDecisionTimeline::IsDecisionCurrent(
	const ActivePictureFrameDecision& decision) const
{
	bool continuityCurrent =
		decision.continuityGeneration == m_continuityGeneration;
	uint64_t expectedGeneration = decision.continuityGeneration;
	if (!continuityCurrent && expectedGeneration != 0)
	{
		for (const ContinuityBoundary& boundary : m_continuityBoundaries)
		{
			if (boundary.beforeGeneration != expectedGeneration)
				continue;
			if (decision.observationIdentity.acceptedSequence >=
					boundary.acceptedSequence ||
				decision.effectiveIdentity.acceptedSequence >=
					boundary.acceptedSequence)
			{
				break;
			}
			expectedGeneration = boundary.afterGeneration;
			if (expectedGeneration == m_continuityGeneration)
			{
				continuityCurrent = true;
				break;
			}
		}
	}
	return continuityCurrent &&
		decision.lookaheadPolicyGeneration ==
			m_lookaheadPolicyGeneration &&
		decision.observationIdentity.transportGeneration ==
			m_transportGeneration &&
		decision.effectiveIdentity.transportGeneration ==
			m_transportGeneration;
}


void ActivePictureDecisionTimeline::RetainIdentity(
	const ActivePictureFrameIdentity& identity,
	uint64_t detectorFrameNumber)
{
	m_observed.push_back({ identity, detectorFrameNumber });
	while (m_observed.size() > MAX_RETAINED_IDENTITIES)
		m_observed.pop_front();
}


bool ActivePictureDecisionTimeline::FindRetainedIdentity(
	uint64_t detectorFrameNumber,
	ActivePictureFrameIdentity& identity) const
{
	for (auto current = m_observed.rbegin(); current != m_observed.rend();
		++current)
	{
		if (current->detectorFrameNumber == detectorFrameNumber)
		{
			identity = current->identity;
			return true;
		}
	}
	return false;
}


bool ActivePictureDecisionTimeline::HasObservedSequence(
	uint64_t acceptedSequence) const
{
	return std::any_of(m_observed.begin(), m_observed.end(),
		[acceptedSequence](const ObservedIdentity& observed)
		{
			return observed.identity.acceptedSequence == acceptedSequence;
		});
}


bool ActivePictureDecisionTimeline::FindAcceptedIdentity(
	const ActivePictureFrameIdentity& identity,
	AcceptedIdentity& accepted) const
{
	for (auto current = m_accepted.rbegin(); current != m_accepted.rend();
		++current)
	{
		if (SameActivePictureFrameIdentity(current->identity, identity))
		{
			accepted = *current;
			return true;
		}
	}
	return false;
}


bool ActivePictureDecisionTimeline::FindLookaheadEvidence(
	const ActivePictureFrameIdentity& identity,
	LookaheadEvidence& evidence) const
{
	for (auto current = m_lookaheadEvidence.rbegin();
		current != m_lookaheadEvidence.rend(); ++current)
	{
		if (SameActivePictureFrameIdentity(current->identity, identity))
		{
			evidence = *current;
			return true;
		}
	}
	return false;
}


ActivePictureInwardProofValidation
ActivePictureDecisionTimeline::ValidateExactInwardProof(
	const ActivePictureFrameIdentity& candidate,
	const ActivePictureFrameIdentity& confirmation,
	const ActivePictureBounds& targetBounds,
	uint64_t requiredLead,
	uint8_t effectiveLookahead,
	uint8_t& proofFrameCount) const
{
	proofFrameCount = 0;
	if (candidate.acceptedSequence <= m_lastConsumedSequence)
		return ActivePictureInwardProofValidation::CANDIDATE_CONSUMED;
	if (!SameDecisionContext(candidate, confirmation))
		return ActivePictureInwardProofValidation::CONTEXT_MISMATCH;
	if (requiredLead > effectiveLookahead)
		return ActivePictureInwardProofValidation::INSUFFICIENT_LEAD;

	uint64_t expectedSequence = candidate.acceptedSequence;
	for (const AcceptedIdentity& acceptedRecord : m_accepted)
	{
		const ActivePictureFrameIdentity& accepted = acceptedRecord.identity;
		if (accepted.acceptedSequence < expectedSequence)
			continue;
		if (accepted.acceptedSequence > confirmation.acceptedSequence)
			break;
		if (accepted.acceptedSequence != expectedSequence)
			return ActivePictureInwardProofValidation::MISSING_IDENTITY;
		if (acceptedRecord.continuityGeneration != m_continuityGeneration)
			return ActivePictureInwardProofValidation::CONTINUITY_MISMATCH;
		if (!SameDecisionContext(accepted, confirmation))
			return ActivePictureInwardProofValidation::CONTEXT_MISMATCH;
		if (expectedSequence == candidate.acceptedSequence &&
			!SameActivePictureFrameIdentity(accepted, candidate))
		{
			return ActivePictureInwardProofValidation::MISSING_IDENTITY;
		}
		if (expectedSequence == confirmation.acceptedSequence &&
			!SameActivePictureFrameIdentity(accepted, confirmation))
		{
			return ActivePictureInwardProofValidation::MISSING_IDENTITY;
		}

		LookaheadEvidence evidence;
		if (!FindLookaheadEvidence(accepted, evidence))
			return ActivePictureInwardProofValidation::MISSING_EVIDENCE;
		if (!evidence.observation.available ||
			evidence.observation.classification !=
				ActivePictureClassification::BAR_CROP_TRUSTED ||
			!evidence.nearBlackEvaluated)
		{
			return ActivePictureInwardProofValidation::EVIDENCE_NOT_TRUSTED;
		}
		if (evidence.nearBlack)
			return ActivePictureInwardProofValidation::EVIDENCE_NEAR_BLACK;
		if (!SameTrustedBounds(evidence.observation.bounds, targetBounds))
		{
			return ActivePictureInwardProofValidation::
				EVIDENCE_BOUNDS_MISMATCH;
		}
		++proofFrameCount;
		++expectedSequence;
	}
	if (expectedSequence != confirmation.acceptedSequence + 1)
		return ActivePictureInwardProofValidation::MISSING_IDENTITY;
	return ActivePictureInwardProofValidation::ACCEPTED;
}


bool ActivePictureDecisionTimeline::SubmitScheduledObservation(
	const ActivePictureFrameIdentity& identity,
	const ActivePictureObservation& observation,
	uint8_t configuredLookahead,
	uint8_t availableLookahead,
	ActivePictureFrameDecision& published,
	const ActivePicturePresentationIntent& presentation)
{
	AcceptedIdentity accepted;
	if (!FindAcceptedIdentity(identity, accepted))
	{
		if (!TrackAcceptedFrame(identity) ||
			!FindAcceptedIdentity(identity, accepted))
		{
			return false;
		}
	}
	if (accepted.continuityGeneration != m_continuityGeneration)
		return false;
	if (HasObservedSequence(identity.acceptedSequence))
		return false;

	RetainIdentity(identity, observation.frameNumber);

	const ActivePictureTransitionDecision transition =
		m_transition.Observe(observation);
	if (!transition.publish)
		return false;

	published = {};
	published.observationIdentity = identity;
	published.effectiveIdentity = identity;
	published.transition = transition;
	published.presentation = presentation;
	published.configuredLookahead = std::min<uint8_t>(
		configuredLookahead, MAX_LOOKAHEAD_FRAMES);
	published.availableLookahead = availableLookahead;
	published.effectiveLookahead = std::min(
		published.configuredLookahead, published.availableLookahead);
	published.continuityGeneration = m_continuityGeneration;
	published.lookaheadPolicyGeneration = m_lookaheadPolicyGeneration;

	ActivePictureFrameIdentity candidate;
	const bool candidateRetained =
		published.effectiveLookahead > 0 &&
		FindRetainedIdentity(transition.firstContradictoryFrame, candidate);
	const bool candidatePending = candidateRetained &&
		candidate.acceptedSequence > m_lastConsumedSequence;
	const bool candidateCompatible = candidateRetained &&
		SameDecisionContext(candidate, identity);
	// Without evidence from every intervening buffered frame, only an outward
	// expansion is safe to associate with an earlier identity. A strictly
	// contained same-axis crop may also move only when every intervening pending
	// frame carries an exact trusted, non-near-black proof certificate.
	const bool outwardSafe = ContainsBounds(
		transition.bounds, transition.stableBounds);
	const bool exactInwardGeometry =
		IsExactInwardActivePictureAssociationGeometry(
			transition, observation.classification);
	const uint64_t requiredLead = candidateRetained &&
		identity.acceptedSequence >= candidate.acceptedSequence ?
		identity.acceptedSequence - candidate.acceptedSequence : UINT64_MAX;
	if (exactInwardGeometry)
	{
		if (!candidateRetained)
		{
			published.inwardProof = ActivePictureInwardProofValidation::
				CANDIDATE_UNAVAILABLE;
		}
		else if (!candidatePending)
		{
			published.inwardProof = ActivePictureInwardProofValidation::
				CANDIDATE_CONSUMED;
		}
		else if (!candidateCompatible)
		{
			published.inwardProof = ActivePictureInwardProofValidation::
				CONTEXT_MISMATCH;
		}
		else
		{
			published.inwardProof = ValidateExactInwardProof(
				candidate, identity, transition.bounds, requiredLead,
				published.effectiveLookahead, published.proofFrameCount);
		}
	}
	if (candidatePending && candidateCompatible && outwardSafe &&
		requiredLead <= published.effectiveLookahead)
	{
		published.effectiveIdentity = candidate;
		published.association = ActivePictureDecisionAssociation::OUTWARD;
	}
	else if (published.inwardProof ==
		ActivePictureInwardProofValidation::ACCEPTED)
	{
		published.effectiveIdentity = candidate;
		published.association = ActivePictureDecisionAssociation::EXACT_INWARD;
	}
	else if (published.configuredLookahead > 0 &&
		transition.firstContradictoryFrame != 0 &&
		transition.firstContradictoryFrame != observation.frameNumber)
	{
		published.late = true;
	}

	return true;
}
