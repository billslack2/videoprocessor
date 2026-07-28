#include <pch.h>

#include "ActivePictureTransitionModel.h"

#include <algorithm>
#include <cmath>


void ActivePictureTransitionModel::Reset()
{
	m_hasStable = false;
	m_waitingPublished = false;
	m_stable = {};
	ClearCandidate();
	m_unavailableCandidates = 0;
	m_lastAnalyzedFrame = 0;
}


uint64_t ActivePictureTransitionModel::AnalysisIntervalFrames(
	double framesPerSecond)
{
	if (!std::isfinite(framesPerSecond) || framesPerSecond <= 0.0)
		framesPerSecond = 60.0;
	const uint64_t interval = static_cast<uint64_t>(
		std::llround(framesPerSecond * ANALYSIS_PERIOD_SECONDS));
	return std::max<uint64_t>(1, std::min<uint64_t>(6, interval));
}


bool ActivePictureTransitionModel::ShouldAnalyze(
	uint64_t frameNumber, double framesPerSecond)
{
	const uint64_t interval = AnalysisIntervalFrames(framesPerSecond);
	if (m_lastAnalyzedFrame != 0 &&
		frameNumber > m_lastAnalyzedFrame &&
		frameNumber - m_lastAnalyzedFrame < interval)
		return false;
	m_lastAnalyzedFrame = frameNumber;
	return true;
}


bool ActivePictureTransitionModel::SameBounds(
	const ActivePictureBounds& left,
	const ActivePictureBounds& right)
{
	if (left.rasterWidth != right.rasterWidth ||
		left.rasterHeight != right.rasterHeight ||
		left.rasterWidth <= 0 || left.rasterHeight <= 0)
		return false;
	const int tolerance = std::max(
		2, std::max(left.rasterWidth / 480, left.rasterHeight / 270));
	return std::abs(left.aspectRatio - right.aspectRatio) <= 0.025 &&
		std::abs(left.left - right.left) <= tolerance &&
		std::abs(left.top - right.top) <= tolerance &&
		std::abs(left.right - right.right) <= tolerance &&
		std::abs(left.bottom - right.bottom) <= tolerance;
}


bool ActivePictureTransitionModel::MateriallyDifferent(
	const ActivePictureBounds& left,
	const ActivePictureBounds& right)
{
	if (left.rasterWidth != right.rasterWidth ||
		left.rasterHeight != right.rasterHeight)
		return true;
	const int tolerance = std::max(
		4, std::max(left.rasterWidth / 240, left.rasterHeight / 135));
	return std::abs(left.aspectRatio - right.aspectRatio) >= 0.06 ||
		std::abs(left.left - right.left) > tolerance ||
		std::abs(left.top - right.top) > tolerance ||
		std::abs(left.right - right.right) > tolerance ||
		std::abs(left.bottom - right.bottom) > tolerance;
}


void ActivePictureTransitionModel::StartCandidate(
	const ActivePictureObservation& observation)
{
	if (m_matchingCandidates > 0 &&
		!SameBounds(m_candidate, observation.bounds) &&
		m_candidateReversals < 255)
		++m_candidateReversals;
	m_candidate = observation.bounds;
	m_matchingCandidates = 1;
	m_firstContradictoryFrame = observation.frameNumber;
}


void ActivePictureTransitionModel::ClearCandidate()
{
	m_candidate = {};
	m_matchingCandidates = 0;
	m_contradictoryCandidates = 0;
	m_candidateReversals = 0;
	m_firstContradictoryFrame = 0;
}


ActivePictureTransitionDecision
ActivePictureTransitionModel::CommitCandidate(
	const ActivePictureObservation& observation,
	const char* reason)
{
	ActivePictureTransitionDecision decision;
	decision.state = ActivePictureTransitionState::STABLE;
	decision.bounds = m_candidate;
	decision.stableBounds = m_stable;
	decision.publish = true;
	decision.stable = true;
	decision.diagnostic = true;
	decision.matchingCandidates = m_matchingCandidates;
	decision.contradictoryCandidates = m_contradictoryCandidates;
	decision.candidateReversals = m_candidateReversals;
	decision.confidence = 1.0;
	decision.firstContradictoryFrame = m_firstContradictoryFrame;
	decision.decisionLatencyFrames =
		observation.frameNumber >= m_firstContradictoryFrame ?
		observation.frameNumber - m_firstContradictoryFrame : 0;
	decision.reason = reason;
	m_stable = m_candidate;
	m_hasStable = true;
	m_waitingPublished = false;
	m_unavailableCandidates = 0;
	ClearCandidate();
	return decision;
}


ActivePictureTransitionDecision ActivePictureTransitionModel::Observe(
	const ActivePictureObservation& observation)
{
	ActivePictureTransitionDecision decision;
	decision.state = m_hasStable ?
		ActivePictureTransitionState::STABLE :
		ActivePictureTransitionState::UNAVAILABLE;
	decision.bounds = m_hasStable ? m_stable : ActivePictureBounds{};
	decision.stableBounds = m_hasStable ? m_stable : ActivePictureBounds{};
	decision.stable = m_hasStable && !m_waitingPublished;

	if (!observation.available)
	{
		if (m_unavailableCandidates < 255)
			++m_unavailableCandidates;
		if (m_matchingCandidates > 0)
		{
			decision.diagnostic = true;
			decision.matchingCandidates = m_matchingCandidates;
			decision.contradictoryCandidates = m_contradictoryCandidates;
			decision.candidateReversals = m_candidateReversals;
			decision.reason =
				"candidate rejected by unavailable/ambiguous observation";
		}
		ClearCandidate();
		// Black/fade frames carry no geometry evidence. Preserve the last stable
		// mapping so a fade cannot create a false aspect-mode change.
		decision.state = ActivePictureTransitionState::UNAVAILABLE;
		decision.stable = m_hasStable && !m_waitingPublished;
		decision.confidence = 0.0;
		return decision;
	}
	m_unavailableCandidates = 0;

	if (!m_hasStable)
	{
		if (m_matchingCandidates == 0 ||
			!SameBounds(m_candidate, observation.bounds))
		{
			StartCandidate(observation);
			decision.diagnostic = true;
			decision.reason = "initial geometry candidate";
		}
		else if (m_matchingCandidates < 255)
		{
			++m_matchingCandidates;
			m_candidate.aspectRatio =
				m_candidate.aspectRatio * 0.75 +
				observation.bounds.aspectRatio * 0.25;
		}
		decision.state = ActivePictureTransitionState::CANDIDATE_TRANSITION;
		decision.bounds = m_candidate;
		decision.matchingCandidates = m_matchingCandidates;
		decision.confidence = static_cast<double>(m_matchingCandidates) /
			INITIAL_CONFIRMATIONS;
		if (m_matchingCandidates >= INITIAL_CONFIRMATIONS)
			return CommitCandidate(
				observation, "initial geometry confirmed");
		return decision;
	}

	if (SameBounds(m_stable, observation.bounds))
	{
		const bool recovered = m_waitingPublished;
		const uint8_t rejectedMatches = m_matchingCandidates;
		const uint8_t rejectedReversals = m_candidateReversals;
		ClearCandidate();
		m_waitingPublished = false;
		decision.state = ActivePictureTransitionState::STABLE;
		decision.bounds = m_stable;
		decision.stable = true;
		decision.matchingCandidates = rejectedMatches;
		decision.candidateReversals = rejectedReversals;
		decision.confidence = 1.0;
		if (recovered)
		{
			decision.publish = true;
			decision.diagnostic = true;
			decision.reason =
				"clear transition candidate reversed; stable geometry restored";
		}
		else if (rejectedMatches > 0)
		{
			decision.diagnostic = true;
			decision.reason =
				"ambiguous transition candidate rejected by stable geometry";
		}
		return decision;
	}

	if (m_contradictoryCandidates < 255)
		++m_contradictoryCandidates;
	if (m_matchingCandidates == 0 ||
		!SameBounds(m_candidate, observation.bounds))
	{
		StartCandidate(observation);
		decision.diagnostic = true;
		decision.reason = "materially different geometry candidate";
	}
	else
	{
		if (m_matchingCandidates < 255)
			++m_matchingCandidates;
		m_candidate.aspectRatio =
			m_candidate.aspectRatio * 0.75 +
			observation.bounds.aspectRatio * 0.25;
	}

	const bool clearTransition =
		MateriallyDifferent(m_stable, m_candidate) &&
		m_stable.symmetricBars && m_candidate.symmetricBars;
	const uint8_t required = clearTransition ?
		CLEAR_TRANSITION_CONFIRMATIONS :
		AMBIGUOUS_TRANSITION_CONFIRMATIONS;
	decision.state = ActivePictureTransitionState::CANDIDATE_TRANSITION;
	decision.bounds = m_candidate;
	decision.stable = !m_waitingPublished;
	decision.clearTransition = clearTransition;
	decision.matchingCandidates = m_matchingCandidates;
	decision.contradictoryCandidates = m_contradictoryCandidates;
	decision.candidateReversals = m_candidateReversals;
	decision.confidence =
		std::min(1.0, static_cast<double>(m_matchingCandidates) / required);
	decision.firstContradictoryFrame = m_firstContradictoryFrame;
	decision.decisionLatencyFrames =
		observation.frameNumber >= m_firstContradictoryFrame ?
		observation.frameNumber - m_firstContradictoryFrame : 0;

	if (m_matchingCandidates >= required)
		return CommitCandidate(observation,
			clearTransition ?
			"clear symmetric transition confirmed" :
			"ambiguous transition reached conservative confidence");

	if (clearTransition && !m_waitingPublished)
	{
		// A strong bar appearance/disappearance invalidates the old crop
		// immediately. Publish Waiting for safe passthrough until the second
		// consistent observation confirms the new mapping.
		m_waitingPublished = true;
		decision.publish = true;
		decision.stable = false;
		decision.diagnostic = true;
		decision.reason =
			"clear symmetric transition; stale geometry withdrawn";
	}
	return decision;
}
