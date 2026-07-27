#include <pch.h>

#include "AlphaCadenceCorrectionPolicy.h"

#include <algorithm>
#include <cmath>

void AlphaCadenceCorrectionPolicy::Reset(uint64_t generation)
{
	m_generation = generation;
	m_phaseFrames = 0.0;
	m_stableSamples = 0;
	m_plannedFrames = 0;
	m_cooldownFrames = 0;
	m_lastSceneEventId = 0;
	m_verificationPending = false;
	m_verificationAction = AlphaCadenceAction::None;
	m_verificationDebt = 0;
	m_verificationPresentId = 0;
	m_lastVerificationValid = false;
	m_lastVerificationSucceeded = false;
}

void AlphaCadenceCorrectionPolicy::CancelPendingAction()
{
	if (!m_verificationPending)
		return;
	m_phaseFrames +=
		m_verificationAction == AlphaCadenceAction::Drop ? 1.0 : -1.0;
	m_verificationPending = false;
	m_verificationAction = AlphaCadenceAction::None;
	m_verificationDebt = 0;
	m_verificationPresentId = 0;
}

AlphaCadenceCorrectionDecision AlphaCadenceCorrectionPolicy::Evaluate(
	const AlphaCadenceCorrectionInput& input)
{
	if (input.generation != m_generation)
		Reset(input.generation);

	AlphaCadenceCorrectionDecision decision;
	const bool ratesInRange =
		input.captureRateHz >= 10.0 && input.captureRateHz <= 240.0 &&
		input.displayRateHz >= 10.0 && input.displayRateHz <= 500.0;
	const double mismatchPpm = ratesInRange
		? std::abs(input.captureRateHz / input.displayRateHz - 1.0) * 1000000.0
		: 0.0;
	decision.ratesCompatible =
		ratesInRange && mismatchPpm <= MAXIMUM_RATE_MISMATCH_PPM;

	if (!input.enabled ||
		input.presentationEvidence != AlphaPresentationEvidence::Stable ||
		!decision.ratesCompatible)
	{
		m_stableSamples = 0;
		m_plannedFrames = 0;
		m_verificationPending = false;
		m_lastVerificationValid = false;
		decision.phaseFrames = m_phaseFrames;
		return decision;
	}

	if (m_verificationPending &&
		input.lastPresentId != 0 &&
		input.lastPresentId != m_verificationPresentId)
	{
		m_lastVerificationValid = true;
		m_lastVerificationSucceeded =
			m_verificationAction == AlphaCadenceAction::Drop
				? (m_verificationDebt > 0 &&
					input.presentationDebt + 1 == m_verificationDebt)
				: input.presentationDebt == m_verificationDebt + 1;
		m_verificationPending = false;
		m_cooldownFrames = COOLDOWN_FRAMES;
		decision.verificationCompleted = true;
	}
	decision.lastVerificationValid = m_lastVerificationValid;
	decision.lastVerificationSucceeded = m_lastVerificationSucceeded;
	decision.verificationPending = m_verificationPending;

	if (m_stableSamples < MINIMUM_STABLE_SAMPLES)
	{
		++m_stableSamples;
		decision.phaseFrames = m_phaseFrames;
		return decision;
	}

	m_phaseFrames += input.captureRateHz / input.displayRateHz - 1.0;
	if (m_cooldownFrames > 0)
	{
		--m_cooldownFrames;
		decision.phaseFrames = m_phaseFrames;
		return decision;
	}
	if (m_verificationPending)
	{
		decision.phaseFrames = m_phaseFrames;
		return decision;
	}

	const AlphaCadenceAction requested =
		m_phaseFrames >= ACTION_PHASE_FRAMES
			? AlphaCadenceAction::Drop
			: (m_phaseFrames <= -ACTION_PHASE_FRAMES
				? AlphaCadenceAction::Repeat
				: AlphaCadenceAction::None);
	decision.planned = std::abs(m_phaseFrames) >= PLAN_PHASE_FRAMES;
	if (decision.planned)
		++m_plannedFrames;
	else
		m_plannedFrames = 0;

	const size_t desired = std::max<size_t>(1, input.desiredQueueDepth);
	const bool debtAndQueueAgree =
		requested == AlphaCadenceAction::Drop
			? input.queueDepth > desired && input.presentationDebt > 0
			: (requested == AlphaCadenceAction::Repeat
				? input.queueDepth < desired && input.presentationDebt == 0
				: false);
	if (!debtAndQueueAgree)
	{
		decision.phaseFrames = m_phaseFrames;
		return decision;
	}

	const uint32_t fallbackFrames = static_cast<uint32_t>(std::max(
		1.0, std::ceil(MAXIMUM_QUEUE_AGE_MS * input.displayRateHz / 1000.0)));
	const bool sceneAuthorized =
		input.safeSceneBoundary && input.sceneEventId != 0 &&
		input.sceneEventId != m_lastSceneEventId;
	const bool deadlineAuthorized =
		m_plannedFrames >= fallbackFrames &&
		(requested == AlphaCadenceAction::Repeat ||
			input.oldestQueuedAgeMs >= MAXIMUM_QUEUE_AGE_MS);
	if (requested == AlphaCadenceAction::None ||
		(!sceneAuthorized && !deadlineAuthorized))
	{
		decision.phaseFrames = m_phaseFrames;
		return decision;
	}

	decision.action = requested;
	decision.deadlineFallback = !sceneAuthorized;
	decision.sceneEventId = sceneAuthorized ? input.sceneEventId : 0;
	m_lastSceneEventId = input.sceneEventId;
	m_phaseFrames += requested == AlphaCadenceAction::Drop ? -1.0 : 1.0;
	m_plannedFrames = 0;
	m_verificationPending = true;
	m_verificationAction = requested;
	m_verificationDebt = input.presentationDebt;
	m_verificationPresentId = input.lastPresentId;
	decision.verificationPending = true;
	decision.phaseFrames = m_phaseFrames;
	return decision;
}
