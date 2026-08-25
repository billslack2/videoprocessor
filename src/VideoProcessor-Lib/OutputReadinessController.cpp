#include <pch.h>

#include <OutputReadinessController.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr double kMinimumRefreshHz = 10.0;
constexpr double kMaximumRefreshHz = 240.0;
constexpr double kRefreshFamilyToleranceRatio = 0.02;
constexpr double kRefreshFamilyToleranceHz = 0.5;

bool IsExpectedOutputRefreshFamily(double expectedHz, double observedHz)
{
	if (!std::isfinite(expectedHz) || !std::isfinite(observedHz) ||
		expectedHz < kMinimumRefreshHz || expectedHz > kMaximumRefreshHz ||
		observedHz < kMinimumRefreshHz || observedHz > kMaximumRefreshHz)
	{
		return false;
	}
	return std::fabs(expectedHz - observedHz) <= std::max(
		kRefreshFamilyToleranceHz,
		expectedHz * kRefreshFamilyToleranceRatio);
}

uint64_t ElapsedOrDeadline(
	uint64_t nowTickMs,
	uint64_t startedTickMs,
	uint64_t deadlineMs)
{
	return startedTickMs == 0 || nowTickMs < startedTickMs ?
		deadlineMs : nowTickMs - startedTickMs;
}
}

OutputReadinessDecision OutputReadinessController::Observe(
	const OutputReadinessInput& input)
{
	const uint64_t correctiveRecoveryGeneration =
		input.correctiveRecoveryGeneration != 0 ?
		input.correctiveRecoveryGeneration : input.transitionGeneration;
	const bool recoveryBudgetChanged = !m_initialized ||
		correctiveRecoveryGeneration != m_correctiveRecoveryGeneration ||
		input.correctiveRecoveryContractRevision !=
			m_correctiveRecoveryContractRevision;
	if (!m_initialized || input.transitionGeneration != m_transitionGeneration ||
		recoveryBudgetChanged)
	{
		const bool clearRecoveryBudget = recoveryBudgetChanged;
		m_initialized = true;
		m_transitionGeneration = input.transitionGeneration;
		m_correctiveRecoveryGeneration = correctiveRecoveryGeneration;
		m_correctiveRecoveryContractRevision =
			input.correctiveRecoveryContractRevision;
		m_state = OutputReadinessState::OutputNotReady;
		m_postReadyEpoch = 0;
		m_postReadyReserveFrames = 0;
		m_readinessValidatedTickMs = 0;
		m_validationStartedTickMs = 0;
		ClearValidationWindow();
		m_resetRequested = false;
		m_resetRequestIssued = false;
		if (clearRecoveryBudget)
		{
			m_correctiveReprimeAttempted = false;
			m_manualRecoveryRequired = false;
			m_lastFailedValidationEpoch = 0;
		}
		m_entryAdoptionCandidate = false;
		m_entryAdoptionTransitionGeneration = 0;
		m_entryAdoptionEpoch = 0;
		m_entryAdoptionTargetFrames = 0;
	}

	OutputReadinessDecision decision;
	decision.transitionGeneration = m_transitionGeneration;
	decision.postReadyEpoch = m_postReadyEpoch;
	decision.readinessValidatedTickMs = m_readinessValidatedTickMs;
	decision.postReadySettleRequiredMs = kPostReadySettleMs;
	decision.correctiveReprimeAttempted = m_correctiveReprimeAttempted;
	decision.manualRecoveryRequired = m_manualRecoveryRequired;
	// Never make first video wait for display-rate evidence. The existing live
	// path remains active until a validated observation requests the one
	// serialized reset that creates a deterministic new epoch.
	decision.discardLiveCapture = false;
	decision.admitCurrentEpochCapture = true;
	decision.allowDownstreamDelivery = true;

	if (!input.graphOperational)
	{
		m_state = OutputReadinessState::OutputNotReady;
		m_postReadyEpoch = 0;
		m_postReadyReserveFrames = 0;
		m_readinessValidatedTickMs = 0;
		m_validationStartedTickMs = 0;
		ClearValidationWindow();
		m_resetRequested = false;
		m_resetRequestIssued = false;
		m_entryAdoptionCandidate = false;
		m_entryAdoptionTransitionGeneration = 0;
		m_entryAdoptionEpoch = 0;
		m_entryAdoptionTargetFrames = 0;
		decision.readinessValidatedTickMs = 0;
		decision.state = m_state;
		decision.reason = OutputReadinessReason::AwaitingGraph;
		return decision;
	}

	if (input.displayDecision != DisplayRefreshRateDecision::Accepted)
	{
		m_state = OutputReadinessState::OutputNotReady;
		m_postReadyEpoch = 0;
		m_postReadyReserveFrames = 0;
		m_readinessValidatedTickMs = 0;
		m_validationStartedTickMs = 0;
		ClearValidationWindow();
		m_resetRequested = false;
		m_resetRequestIssued = false;
		m_entryAdoptionCandidate = false;
		m_entryAdoptionTransitionGeneration = 0;
		m_entryAdoptionEpoch = 0;
		m_entryAdoptionTargetFrames = 0;
		decision.readinessValidatedTickMs = 0;
		decision.state = m_state;
		decision.reason = input.displayDecision ==
			DisplayRefreshRateDecision::Quarantined ?
			OutputReadinessReason::DisplayMeasurementRejected :
			OutputReadinessReason::AwaitingDisplayMeasurement;
		return decision;
	}

	if (!IsExpectedOutputRefreshFamily(
		input.expectedOutputRefreshHz, input.observedOutputRefreshHz))
	{
		m_state = OutputReadinessState::OutputNotReady;
		m_postReadyEpoch = 0;
		m_postReadyReserveFrames = 0;
		m_readinessValidatedTickMs = 0;
		m_validationStartedTickMs = 0;
		ClearValidationWindow();
		m_resetRequested = false;
		m_resetRequestIssued = false;
		m_entryAdoptionCandidate = false;
		m_entryAdoptionTransitionGeneration = 0;
		m_entryAdoptionEpoch = 0;
		m_entryAdoptionTargetFrames = 0;
		decision.readinessValidatedTickMs = 0;
		decision.state = m_state;
		decision.reason = OutputReadinessReason::OutputRefreshFamilyMismatch;
		return decision;
	}

	// A successful manual or coalesced graph reset can arrive while validation,
	// steady delivery, or terminal recovery is active. Only a different exact
	// epoch is eligible; the failed epoch can never satisfy its own retry.
	if (input.postReadyResetCompleted && input.postReadyEpoch != 0 &&
		input.postReadyEpoch != m_postReadyEpoch &&
		m_state != OutputReadinessState::PostReadyResetPending &&
		m_state != OutputReadinessState::OutputNotReady)
	{
		BeginPostReadyEpoch(input);
	}

	if (m_manualRecoveryRequired)
	{
		if (input.postReadyResetCompleted && input.postReadyEpoch != 0 &&
			input.postReadyEpoch != m_lastFailedValidationEpoch)
		{
			BeginPostReadyEpoch(input);
		}
		else
		{
			m_state = OutputReadinessState::ManualRecoveryRequired;
			decision.state = m_state;
			decision.reason =
				OutputReadinessReason::PostResetValidationFailedManualRecovery;
			decision.postReadyEpoch = m_postReadyEpoch;
			decision.correctiveReprimeAttempted = true;
			decision.manualRecoveryRequired = true;
			return decision;
		}
	}

	// A queue epoch can advance while the exact post-reset epoch is prefilling
	// or validating. An explicitly covered successor is adopted above through
	// postReadyResetCompleted. Any other non-zero successor invalidates the
	// proof and must consume the bounded corrective path instead of leaving the
	// controller waiting forever on an epoch whose depth can no longer change.
	const bool uncreditedPostResetEpochChange =
		(m_state == OutputReadinessState::Prefilling ||
		 m_state == OutputReadinessState::PostResetValidating ||
		 m_state == OutputReadinessState::Steady) &&
		m_postReadyEpoch != 0 && input.currentGraphQueueEpoch != 0 &&
		input.currentGraphQueueEpoch != m_postReadyEpoch;
	if (uncreditedPostResetEpochChange)
	{
		m_lastFailedValidationEpoch = m_postReadyEpoch;
		m_validationStartedTickMs = 0;
		ClearValidationWindow();
		if (!m_correctiveReprimeAttempted)
		{
			m_correctiveReprimeAttempted = true;
			m_state = OutputReadinessState::PostReadyResetPending;
			m_resetRequested = false;
			m_resetRequestIssued = true;
			decision.state = m_state;
			decision.reason =
				OutputReadinessReason::AwaitingCorrectivePostReadyReset;
			decision.requestSerializedPostReadyReset = true;
			decision.correctiveReprimeAttempted = true;
			return decision;
		}

		m_manualRecoveryRequired = true;
		m_state = OutputReadinessState::ManualRecoveryRequired;
		decision.state = m_state;
		decision.reason =
			OutputReadinessReason::PostResetValidationFailedManualRecovery;
		decision.correctiveReprimeAttempted = true;
		decision.manualRecoveryRequired = true;
		return decision;
	}

	if (m_state == OutputReadinessState::OutputNotReady)
	{
		m_state = OutputReadinessState::PostReadyResetPending;
		m_readinessValidatedTickMs = input.observationTickMs;
		decision.readinessValidatedTickMs = m_readinessValidatedTickMs;

		// Proof gets one opportunity to become an adoption candidate. It is
		// revalidated only at the settle deadline, so a later HDMI-scale block
		// vetoes adoption while proof first appearing during the window cannot
		// suppress the committed fallback reset.
		m_entryAdoptionCandidate = CanAdoptCurrentGraph(input);
		m_entryAdoptionTransitionGeneration =
			m_entryAdoptionCandidate ? input.currentGraphPrimeTransitionGeneration : 0;
		m_entryAdoptionEpoch =
			m_entryAdoptionCandidate ? input.currentGraphPrimeEpoch : 0;
		m_entryAdoptionTargetFrames =
			m_entryAdoptionCandidate ? input.currentGraphPrimeTargetFrames : 0;
	}

	if (m_state == OutputReadinessState::PostReadyResetPending)
	{
		const bool completedEligibleEpoch = input.postReadyResetCompleted &&
			input.postReadyEpoch != 0 &&
			input.postReadyEpoch != m_lastFailedValidationEpoch;
		if (!completedEligibleEpoch)
		{
			if (m_correctiveReprimeAttempted &&
				m_lastFailedValidationEpoch != 0)
			{
				decision.state = m_state;
				decision.reason =
					OutputReadinessReason::AwaitingCorrectivePostReadyReset;
				decision.requestSerializedPostReadyReset = m_resetRequested;
				decision.correctiveReprimeAttempted = true;
				m_resetRequested = false;
				return decision;
			}
			// A zero or backward monotonic tick must fail toward the established
			// reset, never strand readiness waiting on an invalid deadline.
			const bool invalidTick = m_readinessValidatedTickMs == 0 ||
				input.observationTickMs < m_readinessValidatedTickMs;
			const uint64_t elapsedMs = invalidTick ? kPostReadySettleMs :
				input.observationTickMs - m_readinessValidatedTickMs;
			decision.postReadySettleElapsedMs = static_cast<uint32_t>(
				std::min<uint64_t>(elapsedMs,
					std::numeric_limits<uint32_t>::max()));
			if (elapsedMs < kPostReadySettleMs)
			{
				decision.state = m_state;
				decision.reason = OutputReadinessReason::AwaitingPostReadySettle;
				return decision;
			}
			if (m_entryAdoptionCandidate &&
				input.currentGraphPrimeTransitionGeneration ==
					m_entryAdoptionTransitionGeneration &&
				input.currentGraphPrimeEpoch == m_entryAdoptionEpoch &&
				input.currentGraphPrimeTargetFrames ==
					m_entryAdoptionTargetFrames &&
				CanAdoptCurrentGraph(input))
			{
				m_state = OutputReadinessState::Steady;
				m_postReadyEpoch = input.currentGraphPrimeEpoch;
				m_resetRequested = false;
				m_resetRequestIssued = false;
				m_entryAdoptionCandidate = false;
				m_entryAdoptionTransitionGeneration = 0;
				m_entryAdoptionEpoch = 0;
				m_entryAdoptionTargetFrames = 0;
				decision.state = m_state;
				decision.reason = OutputReadinessReason::CurrentGraphPrimeAdopted;
				decision.postReadyEpoch = m_postReadyEpoch;
				decision.prefillSatisfied = true;
				decision.adoptedCurrentGraph = true;
				return decision;
			}
			if (!m_resetRequestIssued)
			{
				m_resetRequested = true;
				m_resetRequestIssued = true;
			}
			decision.state = m_state;
			decision.reason = OutputReadinessReason::AwaitingPostReadyReset;
			decision.requestSerializedPostReadyReset = m_resetRequested;
			m_resetRequested = false;
			return decision;
		}
		BeginPostReadyEpoch(input);
		decision.postReadyEpoch = m_postReadyEpoch;
	}

	decision.postReadyEpoch = m_postReadyEpoch;
	decision.correctiveReprimeAttempted = m_correctiveReprimeAttempted;
	decision.manualRecoveryRequired = m_manualRecoveryRequired;
	decision.admitCurrentEpochCapture = true;
	decision.allowDownstreamDelivery = false;
	if (m_state == OutputReadinessState::Prefilling)
	{
		// A literal zero-frame reserve is a valid explicit policy and is
		// satisfied immediately. It must not strand readiness in Prefilling.
		decision.prefillSatisfied =
			input.currentEpochProcessedDepth >= input.reserveFrames;
		if (!decision.prefillSatisfied)
		{
			decision.state = m_state;
			decision.reason = OutputReadinessReason::AwaitingPrefill;
			return decision;
		}
		m_state = OutputReadinessState::PostResetValidating;
		m_validationStartedTickMs = input.observationTickMs;
		ClearValidationWindow();
		decision.state = m_state;
		decision.reason = OutputReadinessReason::AwaitingPostResetValidation;
		decision.prefillSatisfied = true;
		decision.allowDownstreamDelivery = true;
		return decision;
	}

	if (m_state == OutputReadinessState::PostResetValidating)
	{
		decision.prefillSatisfied = true;
		decision.allowDownstreamDelivery = true;
		const uint64_t validationElapsedMs = ElapsedOrDeadline(
			input.observationTickMs,
			m_validationStartedTickMs,
			kPostResetValidationDeadlineMs);
		decision.postResetValidationElapsedMs = static_cast<uint32_t>(
			std::min<uint64_t>(validationElapsedMs,
				std::numeric_limits<uint32_t>::max()));

		const bool validValidationClock = m_validationStartedTickMs != 0 &&
			input.observationTickMs >= m_validationStartedTickMs;
		const bool exactEpoch = input.currentGraphQueueEpoch == m_postReadyEpoch;
		const bool unexpectedGap = exactEpoch &&
			(input.currentGraphUnexpectedLiveDeliveryGapEvents != 0 ||
			 input.currentGraphUnexpectedLiveDeliveryGapSlots != 0);
		const bool validationEvidenceHealthy =
			IsPostResetEvidenceExact(input) &&
			IsPostResetEnvelopeHealthy(input);
		if (validationEvidenceHealthy)
		{
			if (m_validationStableObservationCount == 0 ||
				input.observationTickMs < m_validationStableStartedTickMs)
			{
				m_validationStableStartedTickMs = input.observationTickMs;
				m_validationStableObservationCount = 1;
			}
			else
			{
				++m_validationStableObservationCount;
			}
			const uint64_t stableElapsedMs = input.observationTickMs >=
				m_validationStableStartedTickMs ?
				input.observationTickMs - m_validationStableStartedTickMs : 0;
			decision.postResetValidationStableElapsedMs =
				static_cast<uint32_t>(std::min<uint64_t>(
					stableElapsedMs,
					std::numeric_limits<uint32_t>::max()));
			if (validValidationClock &&
				validationElapsedMs <= kPostResetValidationDeadlineMs &&
				m_validationStableObservationCount >= 2 &&
				stableElapsedMs >= kPostResetValidationStableMs)
			{
				m_state = OutputReadinessState::Steady;
				m_manualRecoveryRequired = false;
				decision.state = m_state;
				decision.reason = OutputReadinessReason::Ready;
				decision.correctiveReprimeAttempted =
					m_correctiveReprimeAttempted;
				decision.manualRecoveryRequired = false;
				return decision;
			}
		}
		else
		{
			ClearValidationWindow();
		}

		if (unexpectedGap ||
			validationElapsedMs >= kPostResetValidationDeadlineMs)
		{
			m_lastFailedValidationEpoch = m_postReadyEpoch;
			ClearValidationWindow();
			if (!m_correctiveReprimeAttempted)
			{
				m_correctiveReprimeAttempted = true;
				m_state = OutputReadinessState::PostReadyResetPending;
				m_resetRequested = false;
				m_resetRequestIssued = true;
				decision.state = m_state;
				decision.reason =
					OutputReadinessReason::AwaitingCorrectivePostReadyReset;
				decision.requestSerializedPostReadyReset = true;
				decision.correctiveReprimeAttempted = true;
				return decision;
			}

			m_manualRecoveryRequired = true;
			m_state = OutputReadinessState::ManualRecoveryRequired;
			decision.state = m_state;
			decision.reason =
				OutputReadinessReason::PostResetValidationFailedManualRecovery;
			decision.correctiveReprimeAttempted = true;
			decision.manualRecoveryRequired = true;
			return decision;
		}

		decision.state = m_state;
		decision.reason = OutputReadinessReason::AwaitingPostResetValidation;
		decision.correctiveReprimeAttempted = m_correctiveReprimeAttempted;
		return decision;
	}

	decision.state = m_state;
	decision.reason = OutputReadinessReason::Ready;
	decision.prefillSatisfied = true;
	decision.allowDownstreamDelivery = true;
	return decision;
}

void OutputReadinessController::Reset()
{
	m_initialized = false;
	m_transitionGeneration = 0;
	m_correctiveRecoveryGeneration = 0;
	m_correctiveRecoveryContractRevision = 0;
	m_state = OutputReadinessState::OutputNotReady;
	m_postReadyEpoch = 0;
	m_postReadyReserveFrames = 0;
	m_readinessValidatedTickMs = 0;
	m_validationStartedTickMs = 0;
	ClearValidationWindow();
	m_resetRequested = false;
	m_resetRequestIssued = false;
	m_correctiveReprimeAttempted = false;
	m_manualRecoveryRequired = false;
	m_lastFailedValidationEpoch = 0;
	m_entryAdoptionCandidate = false;
	m_entryAdoptionTransitionGeneration = 0;
	m_entryAdoptionEpoch = 0;
	m_entryAdoptionTargetFrames = 0;
}

void OutputReadinessController::BeginPostReadyEpoch(
	const OutputReadinessInput& input)
{
	m_state = OutputReadinessState::Prefilling;
	m_postReadyEpoch = input.postReadyEpoch;
	m_postReadyReserveFrames = input.reserveFrames;
	m_validationStartedTickMs = 0;
	ClearValidationWindow();
	m_resetRequested = false;
	m_manualRecoveryRequired = false;
	m_entryAdoptionCandidate = false;
	m_entryAdoptionTransitionGeneration = 0;
	m_entryAdoptionEpoch = 0;
	m_entryAdoptionTargetFrames = 0;
}

void OutputReadinessController::ClearValidationWindow()
{
	m_validationStableStartedTickMs = 0;
	m_validationStableObservationCount = 0;
}

void OutputReadinessController::RearmResetRequest()
{
	if (m_state == OutputReadinessState::PostReadyResetPending)
		m_resetRequested = true;
}

bool OutputReadinessController::CanAdoptCurrentGraph(
	const OutputReadinessInput& input) const
{
	const uint64_t frameRelativeHandshakeScaleBlockUs =
		input.expectedOutputRefreshHz >= kMinimumRefreshHz ?
		static_cast<uint64_t>(std::ceil(
			(kHandshakeScaleBlockPeriods * 1000000.0) /
			input.expectedOutputRefreshHz)) : 0;
	const uint64_t handshakeScaleBlockUs = std::min(
		frameRelativeHandshakeScaleBlockUs,
		kMaximumAdoptableBlockDurationUs);
	return input.currentGraphPrimeProven &&
		input.currentGraphPrimeObservedFullConvertedQueue &&
		input.currentGraphBoundarySafe &&
		input.currentGraphDeliveryRecent &&
		input.currentGraphPrimeTransitionGeneration ==
			input.transitionGeneration &&
		input.currentGraphPrimeEpoch != 0 &&
		input.currentGraphPrimeTargetFrames == input.reserveFrames &&
		input.currentGraphPostProofDeliverySuccesses >=
			kRequiredPostProofDeliveries &&
		handshakeScaleBlockUs > 0 &&
		input.currentGraphMaximumSuccessfulDeliveryDurationUs > 0 &&
		input.currentGraphMaximumSuccessfulDeliveryDurationUs <
			handshakeScaleBlockUs &&
		input.currentGraphRawDepth <= kMaximumAdoptionRawDepth &&
		input.currentGraphConvertedDepth == input.reserveFrames;
}

bool OutputReadinessController::IsPostResetEvidenceExact(
	const OutputReadinessInput& input) const
{
	const uint64_t frameRelativeHandshakeScaleBlockUs =
		input.expectedOutputRefreshHz >= kMinimumRefreshHz ?
		static_cast<uint64_t>(std::ceil(
			(kHandshakeScaleBlockPeriods * 1000000.0) /
			input.expectedOutputRefreshHz)) : 0;
	const uint64_t handshakeScaleBlockUs = std::min(
		frameRelativeHandshakeScaleBlockUs,
		kMaximumAdoptableBlockDurationUs);
	return input.currentGraphQueueEpoch == m_postReadyEpoch &&
		input.currentGraphPrimeProven &&
		input.currentGraphPrimeObservedFullConvertedQueue &&
		input.currentGraphPostResetBoundarySafe &&
		input.currentGraphDeliveryRecent &&
		input.currentGraphPrimeTransitionGeneration == m_transitionGeneration &&
		input.currentGraphPrimeEpoch == m_postReadyEpoch &&
		input.currentGraphPrimeTargetFrames == m_postReadyReserveFrames &&
		input.reserveFrames == m_postReadyReserveFrames &&
		input.currentGraphPostProofDeliverySuccesses >=
			kRequiredPostResetValidationDeliveries &&
		handshakeScaleBlockUs > 0 &&
		input.currentGraphMaximumSuccessfulDeliveryDurationUs > 0 &&
		input.currentGraphMaximumSuccessfulDeliveryDurationUs <
			handshakeScaleBlockUs;
}

bool OutputReadinessController::IsPostResetEnvelopeHealthy(
	const OutputReadinessInput& input) const
{
	const size_t minimumConvertedDepth =
		m_postReadyReserveFrames == 0 ? 0 : m_postReadyReserveFrames - 1;
	return input.currentGraphUnexpectedLiveDeliveryGapEvents == 0 &&
		input.currentGraphUnexpectedLiveDeliveryGapSlots == 0 &&
		input.currentGraphRawDepth <= kMaximumAdoptionRawDepth &&
		input.currentGraphRetainedSourceBufferCount <=
			kMaximumPostResetRetainedSourceBuffers &&
		input.currentGraphConvertedDepth >= minimumConvertedDepth &&
		input.currentGraphConvertedDepth <= m_postReadyReserveFrames;
}

const char* ToString(OutputReadinessState state)
{
	switch (state)
	{
	case OutputReadinessState::OutputNotReady: return "output-not-ready";
	case OutputReadinessState::PostReadyResetPending: return "post-ready-reset-pending";
	case OutputReadinessState::Prefilling: return "prefilling";
	case OutputReadinessState::PostResetValidating: return "post-reset-validating";
	case OutputReadinessState::ManualRecoveryRequired: return "manual-recovery-required";
	case OutputReadinessState::Steady: return "steady";
	default: return "output-not-ready";
	}
}

const char* ToString(OutputReadinessReason reason)
{
	switch (reason)
	{
	case OutputReadinessReason::AwaitingGraph: return "awaiting-graph";
	case OutputReadinessReason::AwaitingDisplayMeasurement: return "awaiting-display";
	case OutputReadinessReason::DisplayMeasurementRejected: return "display-rejected";
	case OutputReadinessReason::OutputRefreshFamilyMismatch: return "output-rate-mismatch";
	case OutputReadinessReason::CurrentGraphPrimeAdopted: return "current-graph-prime-adopted";
	case OutputReadinessReason::AwaitingPostReadySettle: return "awaiting-post-ready-settle";
	case OutputReadinessReason::AwaitingPostReadyReset: return "awaiting-post-ready-reset";
	case OutputReadinessReason::AwaitingPrefill: return "awaiting-prefill";
	case OutputReadinessReason::AwaitingPostResetValidation: return "awaiting-post-reset-validation";
	case OutputReadinessReason::AwaitingCorrectivePostReadyReset: return "awaiting-corrective-post-ready-reset";
	case OutputReadinessReason::PostResetValidationFailedManualRecovery: return "post-reset-validation-failed-manual-recovery";
	case OutputReadinessReason::Ready: return "ready";
	default: return "awaiting-graph";
	}
}
