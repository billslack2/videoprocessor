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
		left.rasterHeight == right.rasterHeight;
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


void ActivePictureDecisionTimeline::Reset(uint64_t transportGeneration)
{
	m_transition.Reset();
	m_observed.clear();
	m_accepted.clear();
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
		}
		else if (identity.viewportGeneration !=
				m_lastAcceptedIdentity.viewportGeneration ||
			identity.rendererGeneration !=
				m_lastAcceptedIdentity.rendererGeneration)
		{
			BreakContinuity(identity.sourceFrameNumber);
		}
		if (identity.acceptedSequence != m_lastAcceptedSequence + 1)
			BreakContinuity(identity.sourceFrameNumber);
	}
	m_lastAcceptedSequence = identity.acceptedSequence;
	m_hasAcceptedSequence = true;
	m_lastAcceptedIdentity = identity;
	m_accepted.push_back(identity);
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
	while (!m_accepted.empty() &&
		m_accepted.front().acceptedSequence <= m_lastConsumedSequence)
		m_accepted.pop_front();
}


void ActivePictureDecisionTimeline::MarkDiscarded(
	const ActivePictureFrameIdentity& identity,
	uint64_t detectorFrameNumber)
{
	if (identity.transportGeneration != m_transportGeneration)
		return;
	BreakContinuity(detectorFrameNumber);
	MarkConsumed(identity);
}


void ActivePictureDecisionTimeline::BreakContinuity(
	uint64_t detectorFrameNumber)
{
	ActivePictureObservation unavailable;
	unavailable.frameNumber = detectorFrameNumber;
	(void)m_transition.Observe(unavailable);
	m_observed.clear();
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


bool ActivePictureDecisionTimeline::HasAcceptedIdentity(
	const ActivePictureFrameIdentity& identity) const
{
	return std::any_of(m_accepted.begin(), m_accepted.end(),
		[&identity](const ActivePictureFrameIdentity& accepted)
		{
			return SameActivePictureFrameIdentity(accepted, identity);
		});
}


bool ActivePictureDecisionTimeline::SubmitScheduledObservation(
	const ActivePictureFrameIdentity& identity,
	const ActivePictureObservation& observation,
	uint8_t configuredLookahead,
	uint8_t availableLookahead,
	ActivePictureFrameDecision& published,
	const ActivePicturePresentationIntent& presentation)
{
	if (!HasAcceptedIdentity(identity) && !TrackAcceptedFrame(identity))
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

	ActivePictureFrameIdentity candidate;
	const bool candidateRetained =
		published.effectiveLookahead > 0 &&
		FindRetainedIdentity(transition.firstContradictoryFrame, candidate);
	const bool candidatePending = candidateRetained &&
		candidate.acceptedSequence > m_lastConsumedSequence;
	const bool candidateCompatible = candidateRetained &&
		candidate.sourceFormatGeneration == identity.sourceFormatGeneration &&
		candidate.viewportGeneration == identity.viewportGeneration &&
		candidate.rendererGeneration == identity.rendererGeneration;
	// Without evidence from every intervening buffered frame, only an outward
	// expansion is safe to associate with an earlier identity. Inward or mixed
	// changes remain effective at confirmation and cannot crop unknown pixels.
	const bool outwardSafe = ContainsBounds(
		transition.bounds, transition.stableBounds);
	const uint64_t requiredLead = candidateRetained &&
		identity.acceptedSequence >= candidate.acceptedSequence ?
		identity.acceptedSequence - candidate.acceptedSequence : UINT64_MAX;
	if (candidatePending && candidateCompatible && outwardSafe &&
		requiredLead <= published.effectiveLookahead)
	{
		published.effectiveIdentity = candidate;
	}
	else if (published.configuredLookahead > 0 &&
		transition.firstContradictoryFrame != 0 &&
		transition.firstContradictoryFrame != observation.frameNumber)
	{
		published.late = true;
	}

	return true;
}
