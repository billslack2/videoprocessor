#include <pch.h>

#include <LiveEpochConvergenceController.h>

#include <algorithm>
#include <limits>

namespace
{
constexpr uint32_t kMinimumDownstreamPrimeDeliveries = 5;
constexpr uint32_t kRequiredStableDeliveries = 3;
constexpr uint64_t kMaximumNormalDeliveryPeriods = 2;

bool IsNormalDelivery(const LiveEpochConvergenceInput& input)
{
	if (!input.deliverySucceeded || input.nominalFrameDurationUs == 0)
		return false;

	const uint64_t maximumNormalDurationUs =
		input.nominalFrameDurationUs >
			std::numeric_limits<uint64_t>::max() /
				kMaximumNormalDeliveryPeriods ?
			std::numeric_limits<uint64_t>::max() :
			input.nominalFrameDurationUs * kMaximumNormalDeliveryPeriods;
	return input.deliveryDurationUs <= maximumNormalDurationUs;
}
}

LiveEpochConvergenceDecision LiveEpochConvergenceController::Observe(
	const LiveEpochConvergenceInput& input)
{
	if (!input.epochActive || input.epoch == 0 || input.desiredVpDepth == 0)
	{
		Reset();
		return { LiveEpochConvergenceState::Disabled, false, 0, input.epoch };
	}

	if (!m_initialized || input.epoch != m_epoch ||
		input.desiredVpDepth != m_desiredVpDepth)
	{
		BeginEpoch(input.epoch, input.desiredVpDepth);
	}

	LiveEpochConvergenceDecision decision;
	decision.state = m_state;
	decision.epoch = m_epoch;
	if (!input.deliveryCompleted || m_state == LiveEpochConvergenceState::Converged)
		return decision;

	if (input.deliverySucceeded)
		++m_successfulDeliveryCount;

	if (m_successfulDeliveryCount < kMinimumDownstreamPrimeDeliveries)
	{
		m_state = LiveEpochConvergenceState::AwaitingDownstreamPrime;
		decision.state = m_state;
		return decision;
	}

	m_state = LiveEpochConvergenceState::AwaitingStableDelivery;
	if (IsNormalDelivery(input))
		++m_consecutiveStableDeliveryCount;
	else
		m_consecutiveStableDeliveryCount = 0;

	if (m_consecutiveStableDeliveryCount < kRequiredStableDeliveries)
	{
		decision.state = m_state;
		return decision;
	}

	m_state = LiveEpochConvergenceState::Converged;
	decision.state = m_state;
	decision.requestConvergence = input.vpConvertedDepth > m_desiredVpDepth;
	decision.staleVpFrames = decision.requestConvergence ?
		input.vpConvertedDepth - m_desiredVpDepth : 0;
	return decision;
}

void LiveEpochConvergenceController::Reset()
{
	m_initialized = false;
	m_epoch = 0;
	m_desiredVpDepth = 0;
	m_successfulDeliveryCount = 0;
	m_consecutiveStableDeliveryCount = 0;
	m_state = LiveEpochConvergenceState::Disabled;
}

void LiveEpochConvergenceController::BeginEpoch(
	uint64_t epoch, size_t desiredVpDepth)
{
	m_initialized = true;
	m_epoch = epoch;
	m_desiredVpDepth = desiredVpDepth;
	m_successfulDeliveryCount = 0;
	m_consecutiveStableDeliveryCount = 0;
	m_state = LiveEpochConvergenceState::AwaitingDownstreamPrime;
}

const char* ToString(LiveEpochConvergenceState state)
{
	switch (state)
	{
	case LiveEpochConvergenceState::Disabled: return "disabled";
	case LiveEpochConvergenceState::AwaitingDownstreamPrime: return "awaiting-downstream-prime";
	case LiveEpochConvergenceState::AwaitingStableDelivery: return "awaiting-stable-delivery";
	case LiveEpochConvergenceState::Converged: return "converged";
	default: return "disabled";
	}
}
