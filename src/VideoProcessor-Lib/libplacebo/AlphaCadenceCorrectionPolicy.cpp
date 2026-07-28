#include <pch.h>

#include "AlphaCadenceCorrectionPolicy.h"

#include <algorithm>
#include <cmath>

void AlphaCadenceCorrectionPolicy::Reset(uint64_t generation)
{
	m_generation = generation;
	m_phaseFrames = 0.0;
	m_stableSamples = 0;
	m_rateFilterSamples = 0;
	m_plannedFrames = 0;
	m_cooldownFrames = 0;
	m_lastSceneEventId = 0;
	m_verificationPending = false;
	m_verificationAction = AlphaCadenceAction::None;
	m_verificationDebt = 0;
	m_verificationPresentId = 0;
	m_lastVerificationValid = false;
	m_lastVerificationSucceeded = false;
	m_filteredPhasePerFrame = 0.0;
	m_predictionDirection = AlphaCadenceAction::None;
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
	decision.diagnostic.policyGeneration = input.generation;
	decision.diagnostic.detectorGeneration = input.detectorGeneration;
	decision.diagnostic.presentationGeneration =
		input.presentationGeneration;
	decision.diagnostic.sourceSequence = input.sourceSequence;
	decision.diagnostic.presentationEvidence = input.presentationEvidence;
	decision.diagnostic.captureRateHz = input.captureRateHz;
	decision.diagnostic.displayRateHz = input.displayRateHz;
	decision.diagnostic.queueDepth = input.queueDepth;
	decision.diagnostic.desiredQueueDepth =
		std::max<size_t>(1, input.desiredQueueDepth);
	decision.diagnostic.oldestQueuedAgeMs = input.oldestQueuedAgeMs;
	decision.diagnostic.presentationDebt = input.presentationDebt;
	decision.diagnostic.lastPresentId = input.lastPresentId;
	decision.diagnostic.safeSceneBoundary = input.safeSceneBoundary;
	decision.diagnostic.sceneEventId = input.sceneEventId;
	const bool ratesInRange =
		input.captureRateHz >= 10.0 && input.captureRateHz <= 240.0 &&
		input.displayRateHz >= 10.0 && input.displayRateHz <= 500.0;
	if (!input.enabled ||
		input.presentationEvidence != AlphaPresentationEvidence::Stable ||
		!ratesInRange)
	{
		m_stableSamples = 0;
		m_rateFilterSamples = 0;
		m_filteredPhasePerFrame = 0.0;
		m_predictionDirection = AlphaCadenceAction::None;
		m_plannedFrames = 0;
		m_verificationPending = false;
		m_lastVerificationValid = false;
		decision.phaseFrames = m_phaseFrames;
		decision.timingStatus = !input.enabled
			? AlphaCadenceTimingStatus::Disabled
			: (input.presentationEvidence != AlphaPresentationEvidence::Stable
				? AlphaCadenceTimingStatus::WaitingForDxgi
				: AlphaCadenceTimingStatus::Measuring);
		decision.blockReason = !input.enabled
			? AlphaCadenceBlockReason::Disabled
			: (input.presentationEvidence != AlphaPresentationEvidence::Stable
				? AlphaCadenceBlockReason::PresentationEvidenceUnavailable
				: AlphaCadenceBlockReason::InvalidRates);
		return decision;
	}

	const double rawPhasePerFrame =
		input.captureRateHz / input.displayRateHz - 1.0;
	decision.diagnostic.rawMismatchPpm =
		rawPhasePerFrame * 1000000.0;
	if (m_rateFilterSamples < MINIMUM_PREDICTION_SAMPLES)
	{
		++m_rateFilterSamples;
		m_filteredPhasePerFrame +=
			(rawPhasePerFrame - m_filteredPhasePerFrame) /
			static_cast<double>(m_rateFilterSamples);
	}
	else
	{
		m_filteredPhasePerFrame +=
			(rawPhasePerFrame - m_filteredPhasePerFrame) * RATE_FILTER_ALPHA;
	}
	const double filteredMismatchPpm =
		std::abs(m_filteredPhasePerFrame) * 1000000.0;
	decision.rateFilterSamples = m_rateFilterSamples;
	decision.filteredMismatchPpm = filteredMismatchPpm;
	decision.ratesCompatible =
		filteredMismatchPpm <= MAXIMUM_RATE_MISMATCH_PPM;
	if (!decision.ratesCompatible)
	{
		m_stableSamples = 0;
		m_plannedFrames = 0;
		m_verificationPending = false;
		m_lastVerificationValid = false;
		decision.phaseFrames = m_phaseFrames;
		decision.timingStatus = AlphaCadenceTimingStatus::Incompatible;
		decision.blockReason =
			AlphaCadenceBlockReason::RateMismatchTooLarge;
		return decision;
	}

	if (m_verificationPending &&
		input.lastPresentId != 0 &&
		input.lastPresentId != m_verificationPresentId)
	{
		decision.verificationAction = m_verificationAction;
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
		decision.timingStatus = AlphaCadenceTimingStatus::Measuring;
		decision.blockReason =
			AlphaCadenceBlockReason::StabilizingRates;
		return decision;
	}

	m_phaseFrames += m_filteredPhasePerFrame;
	decision.diagnostic.phaseFrames = m_phaseFrames;
	const double signedFilteredPpm = m_filteredPhasePerFrame * 1000000.0;
	decision.diagnostic.filteredMismatchPpm =
		signedFilteredPpm;
	if (m_predictionDirection == AlphaCadenceAction::None)
	{
		if (signedFilteredPpm >= MINIMUM_PREDICTION_PPM)
			m_predictionDirection = AlphaCadenceAction::Drop;
		else if (signedFilteredPpm <= -MINIMUM_PREDICTION_PPM)
			m_predictionDirection = AlphaCadenceAction::Repeat;
	}
	else if (m_predictionDirection == AlphaCadenceAction::Drop &&
		signedFilteredPpm <= -DIRECTION_REVERSAL_PPM)
	{
		m_predictionDirection = AlphaCadenceAction::Repeat;
	}
	else if (m_predictionDirection == AlphaCadenceAction::Repeat &&
		signedFilteredPpm >= DIRECTION_REVERSAL_PPM)
	{
		m_predictionDirection = AlphaCadenceAction::Drop;
	}

	const bool predictionDirectionAgrees =
		(m_predictionDirection == AlphaCadenceAction::Drop &&
			signedFilteredPpm >= MINIMUM_PREDICTION_PPM) ||
		(m_predictionDirection == AlphaCadenceAction::Repeat &&
			signedFilteredPpm <= -MINIMUM_PREDICTION_PPM);
	if (m_rateFilterSamples >= MINIMUM_PREDICTION_SAMPLES &&
		predictionDirectionAgrees && !m_verificationPending)
	{
		decision.predictedAction = m_predictionDirection;
		const double phasePerFrame = m_filteredPhasePerFrame;
		const double correctionPhase =
			m_predictionDirection == AlphaCadenceAction::Drop
				? ACTION_PHASE_FRAMES : -ACTION_PHASE_FRAMES;
		const double correctionFrames =
			(correctionPhase - m_phaseFrames) / phasePerFrame;
		decision.secondsUntilCorrection =
			correctionFrames / input.captureRateHz;
		const double planPhase =
			m_predictionDirection == AlphaCadenceAction::Drop
				? PLAN_PHASE_FRAMES : -PLAN_PHASE_FRAMES;
		const double planFrames =
			(planPhase - m_phaseFrames) / phasePerFrame;
		decision.secondsUntilPlan = planFrames / input.captureRateHz;
		decision.predictionValid =
			std::isfinite(decision.secondsUntilCorrection) &&
			std::isfinite(decision.secondsUntilPlan);
	}
	decision.timingStatus = m_verificationPending
		? AlphaCadenceTimingStatus::Verifying
		: (m_rateFilterSamples < MINIMUM_PREDICTION_SAMPLES
			? AlphaCadenceTimingStatus::Measuring
			: (decision.predictionValid
				? AlphaCadenceTimingStatus::Forecasting
				: AlphaCadenceTimingStatus::Matched));

	const AlphaCadenceAction requested =
		m_phaseFrames >= ACTION_PHASE_FRAMES
			? AlphaCadenceAction::Drop
			: (m_phaseFrames <= -ACTION_PHASE_FRAMES
				? AlphaCadenceAction::Repeat
				: AlphaCadenceAction::None);
	decision.due = requested != AlphaCadenceAction::None;
	decision.planned = std::abs(m_phaseFrames) >= PLAN_PHASE_FRAMES;
	decision.diagnostic.cooldownFrames = m_cooldownFrames;

	if (m_cooldownFrames > 0)
	{
		--m_cooldownFrames;
		decision.phaseFrames = m_phaseFrames;
		decision.blockReason = AlphaCadenceBlockReason::Cooldown;
		return decision;
	}
	if (m_verificationPending)
	{
		decision.phaseFrames = m_phaseFrames;
		decision.blockReason =
			AlphaCadenceBlockReason::VerificationPending;
		return decision;
	}

	if (decision.planned)
		++m_plannedFrames;
	else
		m_plannedFrames = 0;

	if (requested == AlphaCadenceAction::None)
	{
		decision.phaseFrames = m_phaseFrames;
		decision.blockReason = decision.predictionValid
			? AlphaCadenceBlockReason::BeforeDeadline
			: AlphaCadenceBlockReason::NoActionableMismatch;
		return decision;
	}

	const size_t desired = std::max<size_t>(1, input.desiredQueueDepth);
	const uint32_t fallbackFrames = static_cast<uint32_t>(std::max(
		1.0, std::ceil(MAXIMUM_QUEUE_AGE_MS * input.displayRateHz / 1000.0)));
	decision.diagnostic.fallbackFrames = fallbackFrames;
	decision.diagnostic.plannedFrames = m_plannedFrames;
	decision.diagnostic.sceneEventFresh =
		input.sceneEventId != 0 && input.sceneEventId != m_lastSceneEventId;
	const bool sceneAuthorized =
		input.safeSceneBoundary && input.sceneEventId != 0 &&
		input.sceneEventId != m_lastSceneEventId;
	decision.diagnostic.sceneAuthorized = sceneAuthorized;
	decision.diagnostic.fallbackMature =
		m_plannedFrames >= fallbackFrames;
	const bool deadlineAuthorized =
		m_plannedFrames >= fallbackFrames &&
		(requested == AlphaCadenceAction::Repeat ||
			input.oldestQueuedAgeMs >= MAXIMUM_QUEUE_AGE_MS);
	decision.diagnostic.fallbackEligible = deadlineAuthorized;

	if (requested == AlphaCadenceAction::Drop &&
		input.queueDepth <= desired)
	{
		decision.phaseFrames = m_phaseFrames;
		decision.blockReason =
			AlphaCadenceBlockReason::DropQueueNotAboveDesired;
		return decision;
	}
	if (requested == AlphaCadenceAction::Repeat &&
		input.queueDepth >= desired)
	{
		decision.phaseFrames = m_phaseFrames;
		decision.blockReason =
			AlphaCadenceBlockReason::RepeatQueueNotBelowDesired;
		return decision;
	}
	if (requested == AlphaCadenceAction::Drop &&
		input.presentationDebt == 0)
	{
		decision.phaseFrames = m_phaseFrames;
		decision.blockReason =
			AlphaCadenceBlockReason::DropPresentationDebtMissing;
		return decision;
	}
	if (requested == AlphaCadenceAction::Repeat &&
		input.presentationDebt != 0)
	{
		decision.phaseFrames = m_phaseFrames;
		decision.blockReason =
			AlphaCadenceBlockReason::RepeatPresentationDebtPresent;
		return decision;
	}

	if (!sceneAuthorized && !deadlineAuthorized)
	{
		decision.phaseFrames = m_phaseFrames;
		if (m_plannedFrames < fallbackFrames)
			decision.blockReason =
				AlphaCadenceBlockReason::FallbackNotMature;
		else if (requested == AlphaCadenceAction::Drop &&
			input.oldestQueuedAgeMs < MAXIMUM_QUEUE_AGE_MS)
			decision.blockReason =
				AlphaCadenceBlockReason::DropFallbackQueueTooYoung;
		else
			decision.blockReason =
				AlphaCadenceBlockReason::WaitingForFreshScene;
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
