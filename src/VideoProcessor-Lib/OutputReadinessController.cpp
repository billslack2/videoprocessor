#include <pch.h>

#include <OutputReadinessController.h>

#include <algorithm>
#include <cmath>

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
		m_resetRequested = false;
	}

	OutputReadinessDecision decision;
	decision.transitionGeneration = m_transitionGeneration;
	decision.postReadyEpoch = m_postReadyEpoch;
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
		m_resetRequested = false;
		decision.state = m_state;
		decision.reason = OutputReadinessReason::AwaitingGraph;
		return decision;
	}

	if (input.displayDecision != DisplayRefreshRateDecision::Accepted)
	{
		m_state = OutputReadinessState::OutputNotReady;
		m_postReadyEpoch = 0;
		m_resetRequested = false;
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
		m_resetRequested = false;
		decision.state = m_state;
		decision.reason = OutputReadinessReason::OutputRefreshFamilyMismatch;
		return decision;
	}

	// The established DirectShow/madVR re-prime is reliable only after its
	// normal live startup has formed a substantial VP reservoir. Express that
	// requirement as an exact VP depth, not an arbitrary elapsed-time delay.
	if (m_state == OutputReadinessState::OutputNotReady &&
		!input.postReadyResetCompleted && input.preResetPrimeFrames > 0 &&
		input.currentEpochProcessedDepth < input.preResetPrimeFrames)
	{
		decision.state = m_state;
		decision.reason = OutputReadinessReason::AwaitingPreResetPrime;
		return decision;
	}

	if (m_state == OutputReadinessState::OutputNotReady)
	{
		m_state = OutputReadinessState::PostReadyResetPending;
		m_resetRequested = true;
	}

	if (m_state == OutputReadinessState::PostReadyResetPending)
	{
		if (!input.postReadyResetCompleted || input.postReadyEpoch == 0)
		{
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
		decision.prefillSatisfied = input.reserveFrames > 0 &&
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
	m_resetRequested = false;
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
	case OutputReadinessReason::AwaitingPreResetPrime: return "awaiting-pre-reset-prime";
	case OutputReadinessReason::AwaitingPostReadyReset: return "awaiting-post-ready-reset";
	case OutputReadinessReason::AwaitingPrefill: return "awaiting-prefill";
	case OutputReadinessReason::Ready: return "ready";
	default: return "awaiting-graph";
	}
}
