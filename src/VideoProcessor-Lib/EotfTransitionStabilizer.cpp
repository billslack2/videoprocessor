#include "pch.h"

#include "EotfTransitionStabilizer.h"

#include <algorithm>
#include <limits>


EotfTransitionStabilizer::EotfTransitionStabilizer(
	uint64_t settlingWindowMs, uint32_t requiredObservations) :
	m_settlingWindowMs(settlingWindowMs),
	m_requiredObservations(std::max<uint32_t>(2, requiredObservations))
{
}


void EotfTransitionStabilizer::Reset(EOTF active)
{
	m_active = active;
	ClearCandidate();
}


EotfTransitionResult EotfTransitionStabilizer::Observe(
	EOTF observed, bool valid, uint64_t nowMs)
{
	if (!valid || observed == EOTF::UNKNOWN || m_active == EOTF::UNKNOWN)
		return Result(EotfTransitionAction::None);

	if (observed == m_active)
	{
		if (!HasPendingCandidate())
			return Result(EotfTransitionAction::None);

		const EotfTransitionResult cancelled =
			Result(EotfTransitionAction::CandidateCancelled);
		ClearCandidate();
		return cancelled;
	}

	if (!HasPendingCandidate())
	{
		m_candidate = observed;
		m_candidateSinceMs = nowMs;
		m_matchingObservations = 1;
		m_restartCommitted = false;
		m_commitDeferralReported = false;
		return Result(EotfTransitionAction::CandidateStarted);
	}

	if (observed != m_candidate)
	{
		m_candidate = observed;
		m_candidateSinceMs = nowMs;
		m_matchingObservations = 1;
		m_restartCommitted = false;
		m_commitDeferralReported = false;
		return Result(EotfTransitionAction::CandidateChanged);
	}

	if (m_matchingObservations != std::numeric_limits<uint32_t>::max())
		++m_matchingObservations;

	if (m_matchingObservations == m_requiredObservations)
		return Result(EotfTransitionAction::CandidateConfirmed);

	return Result(EotfTransitionAction::ObservationCoalesced);
}


EotfTransitionResult EotfTransitionStabilizer::Evaluate(
	uint64_t nowMs, bool commitAllowed)
{
	if (!IsConfirmed() || m_restartCommitted ||
		nowMs - m_candidateSinceMs < m_settlingWindowMs)
	{
		return Result(EotfTransitionAction::None);
	}

	if (!commitAllowed)
	{
		if (m_commitDeferralReported)
			return Result(EotfTransitionAction::ObservationCoalesced);

		m_commitDeferralReported = true;
		return Result(EotfTransitionAction::CommitDeferred);
	}

	m_restartCommitted = true;
	return Result(EotfTransitionAction::CommitRestart);
}


EotfTransitionResult EotfTransitionStabilizer::Result(
	EotfTransitionAction action) const
{
	return { action, m_active, m_candidate, m_matchingObservations };
}


void EotfTransitionStabilizer::ClearCandidate()
{
	m_candidate = EOTF::UNKNOWN;
	m_candidateSinceMs = 0;
	m_matchingObservations = 0;
	m_restartCommitted = false;
	m_commitDeferralReported = false;
}
