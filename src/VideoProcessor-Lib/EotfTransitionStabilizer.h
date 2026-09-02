#pragma once

#include <cstdint>

#include <EOTF.h>


enum class EotfTransitionAction
{
	None,
	CandidateStarted,
	CandidateChanged,
	CandidateConfirmed,
	CandidateCancelled,
	ObservationCoalesced,
	CommitDeferred,
	CommitRestart,
};


struct EotfTransitionResult
{
	EotfTransitionAction action = EotfTransitionAction::None;
	EOTF active = EOTF::UNKNOWN;
	EOTF candidate = EOTF::UNKNOWN;
	uint32_t matchingObservations = 0;
};


// Pure policy for non-dynamic renderers. Capture notifications and periodic
// sampling both feed this one state machine; the UI owns timers and graph work.
// A candidate must be observed repeatedly and remain stable for the complete
// settling window before exactly one renderer restart may be committed.
class EotfTransitionStabilizer
{
public:
	static constexpr uint64_t DefaultSettlingWindowMs = 5000;
	static constexpr uint32_t DefaultRequiredObservations = 2;

	explicit EotfTransitionStabilizer(
		uint64_t settlingWindowMs = DefaultSettlingWindowMs,
		uint32_t requiredObservations = DefaultRequiredObservations);

	void Reset(EOTF active);
	EotfTransitionResult Observe(EOTF observed, bool valid, uint64_t nowMs);
	EotfTransitionResult Evaluate(uint64_t nowMs, bool commitAllowed);

	bool HasPendingCandidate() const { return m_candidate != EOTF::UNKNOWN; }
	bool IsConfirmed() const
	{
		return HasPendingCandidate() &&
			m_matchingObservations >= m_requiredObservations;
	}
	EOTF Active() const { return m_active; }
	EOTF Candidate() const { return m_candidate; }
	uint32_t MatchingObservations() const { return m_matchingObservations; }

private:
	EotfTransitionResult Result(EotfTransitionAction action) const;
	void ClearCandidate();

	uint64_t m_settlingWindowMs = DefaultSettlingWindowMs;
	uint32_t m_requiredObservations = DefaultRequiredObservations;
	EOTF m_active = EOTF::UNKNOWN;
	EOTF m_candidate = EOTF::UNKNOWN;
	uint64_t m_candidateSinceMs = 0;
	uint32_t m_matchingObservations = 0;
	bool m_restartCommitted = false;
	bool m_commitDeferralReported = false;
};
