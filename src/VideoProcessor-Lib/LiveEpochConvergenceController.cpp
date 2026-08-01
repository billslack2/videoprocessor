#include <pch.h>

#include <LiveEpochConvergenceController.h>

#include <algorithm>
#include <limits>

namespace
{
uint64_t SaturatingMultiply(uint64_t value, uint64_t multiplier)
{
	if (value == 0 || multiplier == 0)
		return 0;
	return value > (std::numeric_limits<uint64_t>::max)() / multiplier ?
		(std::numeric_limits<uint64_t>::max)() : value * multiplier;
}
}

LiveEpochConvergenceDecision LiveEpochConvergenceController::Observe(
	const LiveEpochConvergenceInput& input)
{
	if (!input.epochActive || input.epoch == 0 ||
		(!input.targetConfigured && input.desiredVpDepth == 0))
	{
		Reset();
		LiveEpochConvergenceDecision decision;
		decision.reason = LiveEpochConvergenceReason::DisabledByConfiguration;
		decision.epoch = input.epoch;
		return decision;
	}

	if (!m_initialized || input.epoch != m_epoch)
		BeginEpoch(input.epoch, input.desiredVpDepth);

	const LiveEpochConvergenceState previousState = m_state;
	if (input.desiredVpDepth != m_desiredVpDepth)
	{
		m_state = LiveEpochConvergenceState::UnprovenNoTrim;
		return MakeDecision(input, previousState,
			LiveEpochConvergenceReason::TargetChangedWithinEpoch);
	}

	if (IsTerminal())
		return MakeDecision(input, previousState, LiveEpochConvergenceReason::None);

	if (input.resetOrFlushInProgress || input.sceneCadenceActive)
	{
		return MakeDecision(input, previousState,
			LiveEpochConvergenceReason::UnsafeBoundary);
	}

	if (m_state == LiveEpochConvergenceState::ObservingIngress &&
		m_hasFirstSuccessTick &&
		HasElapsed(input.observationTickMs, m_firstSuccessTickMs,
			kBlockObservationTimeoutMs))
	{
		m_state = LiveEpochConvergenceState::UnprovenNoTrim;
		return MakeDecision(input, previousState,
			LiveEpochConvergenceReason::BlockObservationTimedOut);
	}

	if ((m_state == LiveEpochConvergenceState::Armed ||
		m_state == LiveEpochConvergenceState::DeferredQueueEvidence) &&
		m_hasArmedTick &&
		HasElapsed(input.observationTickMs, m_armedTickMs,
			kArmedConvergenceWindowMs))
	{
		m_state = input.vpConvertedDepth <= m_desiredVpDepth ?
			LiveEpochConvergenceState::SettledNoTrim :
			LiveEpochConvergenceState::UnprovenNoTrim;
		return MakeDecision(input, previousState,
			LiveEpochConvergenceReason::ArmedWindowTimedOut);
	}

	if (!input.deliveryCompleted)
	{
		return MakeDecision(input, previousState,
			LiveEpochConvergenceReason::AwaitingIngressBlock);
	}

	if (!input.deliverySucceeded)
	{
		m_state = LiveEpochConvergenceState::UnprovenNoTrim;
		m_consecutiveRecoveryDeliveryCount = 0;
		return MakeDecision(input, previousState,
			LiveEpochConvergenceReason::DeliveryFailed);
	}

	++m_successfulDeliveryCount;
	if (!m_hasFirstSuccessTick && input.observationTickMs != 0)
	{
		m_hasFirstSuccessTick = true;
		m_firstSuccessTickMs = input.observationTickMs;
	}

	const bool ingressBlocked = IsIngressBlocked(input);
	const bool normalDelivery = IsNormalDelivery(input);
	if (m_state == LiveEpochConvergenceState::ObservingIngress)
	{
		if (ingressBlocked)
		{
			++m_ingressBlockCount;
			m_consecutiveRecoveryDeliveryCount = 0;
			m_state = LiveEpochConvergenceState::IngressBlocked;
			return MakeDecision(input, previousState,
				LiveEpochConvergenceReason::IngressBlockObserved);
		}
		return MakeDecision(input, previousState,
			LiveEpochConvergenceReason::AwaitingIngressBlock);
	}

	if (m_state == LiveEpochConvergenceState::IngressBlocked ||
		m_state == LiveEpochConvergenceState::Recovering ||
		m_state == LiveEpochConvergenceState::Armed ||
		m_state == LiveEpochConvergenceState::DeferredQueueEvidence)
	{
		if (ingressBlocked)
		{
			++m_ingressBlockCount;
			m_consecutiveRecoveryDeliveryCount = 0;
			m_state = LiveEpochConvergenceState::IngressBlocked;
			return MakeDecision(input, previousState,
				LiveEpochConvergenceReason::IngressBlockObserved);
		}

		if (!normalDelivery)
		{
			m_consecutiveRecoveryDeliveryCount = 0;
			m_state = LiveEpochConvergenceState::IngressBlocked;
			return MakeDecision(input, previousState,
				LiveEpochConvergenceReason::IngressBlockObserved);
		}

		if (m_state == LiveEpochConvergenceState::IngressBlocked ||
			m_state == LiveEpochConvergenceState::Recovering)
		{
			++m_consecutiveRecoveryDeliveryCount;
			if (m_consecutiveRecoveryDeliveryCount < kRequiredRecoveryDeliveries)
			{
				m_state = LiveEpochConvergenceState::Recovering;
				return MakeDecision(input, previousState,
					LiveEpochConvergenceReason::RecoveryDelivery);
			}

			m_state = LiveEpochConvergenceState::Armed;
			if (input.observationTickMs != 0)
			{
				m_hasArmedTick = true;
				m_armedTickMs = input.observationTickMs;
			}
		}

		if (CanRequestTrim(input))
		{
			m_state = LiveEpochConvergenceState::TrimApplied;
			LiveEpochConvergenceDecision decision = MakeDecision(input, previousState,
				LiveEpochConvergenceReason::TrimRequested);
			decision.requestConvergence = true;
			decision.staleRawFrames = input.vpRawDepth;
			decision.staleConvertedFrames = input.vpConvertedDepth >
				m_desiredVpDepth ?
				input.vpConvertedDepth - m_desiredVpDepth : 0;
			decision.staleVpFrames = decision.staleRawFrames +
				decision.staleConvertedFrames;
			return decision;
		}

		if (!input.rawDepthKnown)
		{
			m_state = LiveEpochConvergenceState::DeferredQueueEvidence;
			return MakeDecision(input, previousState,
				LiveEpochConvergenceReason::RawDepthUnknown);
		}

		m_state = LiveEpochConvergenceState::Armed;
		return MakeDecision(input, previousState,
			LiveEpochConvergenceReason::ArmedNoBacklog);
	}

	return MakeDecision(input, previousState, LiveEpochConvergenceReason::None);
}

void LiveEpochConvergenceController::Reset()
{
	m_initialized = false;
	m_epoch = 0;
	m_desiredVpDepth = 0;
	m_successfulDeliveryCount = 0;
	m_ingressBlockCount = 0;
	m_consecutiveRecoveryDeliveryCount = 0;
	m_hasFirstSuccessTick = false;
	m_firstSuccessTickMs = 0;
	m_hasArmedTick = false;
	m_armedTickMs = 0;
	m_state = LiveEpochConvergenceState::Disabled;
}

void LiveEpochConvergenceController::BeginEpoch(
	uint64_t epoch, size_t desiredVpDepth)
{
	m_initialized = true;
	m_epoch = epoch;
	m_desiredVpDepth = desiredVpDepth;
	m_successfulDeliveryCount = 0;
	m_ingressBlockCount = 0;
	m_consecutiveRecoveryDeliveryCount = 0;
	m_hasFirstSuccessTick = false;
	m_firstSuccessTickMs = 0;
	m_hasArmedTick = false;
	m_armedTickMs = 0;
	m_state = LiveEpochConvergenceState::ObservingIngress;
}

LiveEpochConvergenceDecision LiveEpochConvergenceController::MakeDecision(
	const LiveEpochConvergenceInput& input,
	LiveEpochConvergenceState previousState,
	LiveEpochConvergenceReason reason) const
{
	LiveEpochConvergenceDecision decision;
	decision.previousState = previousState;
	decision.state = m_state;
	decision.reason = reason;
	decision.epoch = m_epoch;
	decision.successfulDeliveryCount = m_successfulDeliveryCount;
	decision.ingressBlockCount = m_ingressBlockCount;
	decision.consecutiveRecoveryDeliveryCount = m_consecutiveRecoveryDeliveryCount;
	decision.ingressBlockThresholdUs =
		IngressBlockThresholdUs(input.nominalFrameDurationUs);
	decision.normalDeliveryThresholdUs =
		NormalDeliveryThresholdUs(input.nominalFrameDurationUs);
	decision.rawDepthKnown = input.rawDepthKnown;
	decision.rawBacklogObserved =
		input.rawDepthKnown && input.vpRawDepth > 0;
	if (m_hasFirstSuccessTick && input.observationTickMs >= m_firstSuccessTickMs)
		decision.elapsedSinceFirstSuccessMs =
			input.observationTickMs - m_firstSuccessTickMs;
	return decision;
}

bool LiveEpochConvergenceController::HasElapsed(
	uint64_t now, uint64_t since, uint64_t duration) const
{
	return now != 0 && now >= since && now - since >= duration;
}

uint64_t LiveEpochConvergenceController::IngressBlockThresholdUs(
	uint64_t nominalFrameDurationUs) const
{
	return std::max(kMinimumIngressBlockUs,
		SaturatingMultiply(nominalFrameDurationUs, kIngressBlockPeriods));
}

uint64_t LiveEpochConvergenceController::NormalDeliveryThresholdUs(
	uint64_t nominalFrameDurationUs) const
{
	return SaturatingMultiply(nominalFrameDurationUs,
		kMaximumNormalDeliveryPeriods);
}

bool LiveEpochConvergenceController::IsIngressBlocked(
	const LiveEpochConvergenceInput& input) const
{
	return input.nominalFrameDurationUs != 0 && input.deliverySucceeded &&
		input.deliveryDurationUs >=
		IngressBlockThresholdUs(input.nominalFrameDurationUs);
}

bool LiveEpochConvergenceController::IsNormalDelivery(
	const LiveEpochConvergenceInput& input) const
{
	return input.nominalFrameDurationUs != 0 && input.deliverySucceeded &&
		input.deliveryDurationUs <=
		NormalDeliveryThresholdUs(input.nominalFrameDurationUs);
}

bool LiveEpochConvergenceController::IsTerminal() const
{
	return m_state == LiveEpochConvergenceState::TrimApplied ||
		m_state == LiveEpochConvergenceState::SettledNoTrim ||
		m_state == LiveEpochConvergenceState::UnprovenNoTrim;
}

bool LiveEpochConvergenceController::CanRequestTrim(
	const LiveEpochConvergenceInput& input) const
{
	return input.rawDepthKnown &&
		(input.vpRawDepth > 0 ||
		 input.vpConvertedDepth > m_desiredVpDepth);
}

const char* ToString(LiveEpochConvergenceState state)
{
	switch (state)
	{
	case LiveEpochConvergenceState::Disabled: return "disabled";
	case LiveEpochConvergenceState::ObservingIngress: return "observing-ingress";
	case LiveEpochConvergenceState::IngressBlocked: return "ingress-blocked";
	case LiveEpochConvergenceState::Recovering: return "recovering";
	case LiveEpochConvergenceState::Armed: return "armed";
	case LiveEpochConvergenceState::DeferredQueueEvidence: return "deferred-queue-evidence";
	case LiveEpochConvergenceState::TrimApplied: return "trim-applied";
	case LiveEpochConvergenceState::SettledNoTrim: return "settled-no-trim";
	case LiveEpochConvergenceState::UnprovenNoTrim: return "unproven-no-trim";
	default: return "disabled";
	}
}

const char* ToString(LiveEpochConvergenceReason reason)
{
	switch (reason)
	{
	case LiveEpochConvergenceReason::None: return "none";
	case LiveEpochConvergenceReason::DisabledByConfiguration: return "disabled-by-configuration";
	case LiveEpochConvergenceReason::AwaitingIngressBlock: return "awaiting-ingress-block";
	case LiveEpochConvergenceReason::IngressBlockObserved: return "ingress-block-observed";
	case LiveEpochConvergenceReason::RecoveryDelivery: return "recovery-delivery";
	case LiveEpochConvergenceReason::ArmedNoBacklog: return "armed-no-backlog";
	case LiveEpochConvergenceReason::RawDepthUnknown: return "raw-depth-unknown";
	case LiveEpochConvergenceReason::RawBacklogObserved: return "raw-backlog-observed";
	case LiveEpochConvergenceReason::TrimRequested: return "trim-requested";
	case LiveEpochConvergenceReason::BlockObservationTimedOut: return "block-observation-timed-out";
	case LiveEpochConvergenceReason::ArmedWindowTimedOut: return "armed-window-timed-out";
	case LiveEpochConvergenceReason::DeliveryFailed: return "delivery-failed";
	case LiveEpochConvergenceReason::UnsafeBoundary: return "unsafe-boundary";
	case LiveEpochConvergenceReason::TargetChangedWithinEpoch: return "target-changed-within-epoch";
	default: return "none";
	}
}
