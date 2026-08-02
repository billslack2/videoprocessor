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
}

OutputReadinessDecision OutputReadinessController::Observe(
	const OutputReadinessInput& input)
{
	if (!m_initialized || input.transitionGeneration != m_transitionGeneration)
	{
		m_initialized = true;
		m_transitionGeneration = input.transitionGeneration;
		m_state = OutputReadinessState::OutputNotReady;
		m_postReadyEpoch = 0;
		m_readinessValidatedTickMs = 0;
		m_resetRequested = false;
		m_resetRequestIssued = false;
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
		m_readinessValidatedTickMs = 0;
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
		m_readinessValidatedTickMs = 0;
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
		m_readinessValidatedTickMs = 0;
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
		if (!input.postReadyResetCompleted || input.postReadyEpoch == 0)
		{
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
		m_state = OutputReadinessState::Prefilling;
		m_postReadyEpoch = input.postReadyEpoch;
		decision.postReadyEpoch = m_postReadyEpoch;
	}

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
		m_state = OutputReadinessState::Steady;
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
	m_state = OutputReadinessState::OutputNotReady;
	m_postReadyEpoch = 0;
	m_readinessValidatedTickMs = 0;
	m_resetRequested = false;
	m_resetRequestIssued = false;
	m_entryAdoptionCandidate = false;
	m_entryAdoptionTransitionGeneration = 0;
	m_entryAdoptionEpoch = 0;
	m_entryAdoptionTargetFrames = 0;
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

const char* ToString(OutputReadinessState state)
{
	switch (state)
	{
	case OutputReadinessState::OutputNotReady: return "output-not-ready";
	case OutputReadinessState::PostReadyResetPending: return "post-ready-reset-pending";
	case OutputReadinessState::Prefilling: return "prefilling";
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
	case OutputReadinessReason::Ready: return "ready";
	default: return "awaiting-graph";
	}
}
