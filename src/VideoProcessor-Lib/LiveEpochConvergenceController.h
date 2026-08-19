/*
 * One-shot, fresh-epoch convergence policy for VP's live transport backlog.
 *
 * This controller deliberately owns no DirectShow, madVR, HDMI, or queue
 * object.  In particular, an S_OK from Deliver is evidence only that the
 * DirectShow ingress accepted a sample; it is not evidence of madVR queue
 * occupancy or presentation. A recovered >=3-frame block and a sustained
 * frame-paced ingress plateau are separate black-box priming signals; both
 * authorize only VP's local latest-wins transition. The owner must execute the
 * requested transition to a persistent local high-water and report the actual
 * result separately.
 */
#pragma once

#include <cstddef>
#include <cstdint>

enum class LiveEpochConvergenceState
{
	Disabled,
	ObservingIngress,
	IngressBlocked,
	Recovering,
	Armed,
	DeferredQueueEvidence,
	TrimApplied,
	SettledNoTrim,
	UnprovenNoTrim
};

enum class LiveEpochConvergenceReason
{
	None,
	DisabledByConfiguration,
	AwaitingIngressBlock,
	IngressBlockObserved,
	RecoveryDelivery,
	ArmedNoBacklog,
	RawDepthUnknown,
	RawBacklogObserved,
	TrimRequested,
	BlockObservationTimedOut,
	ArmedWindowTimedOut,
	DeliveryFailed,
	UnsafeBoundary,
	TargetChangedWithinEpoch,
	PacedIngressObserved,
	PacedPrimeRequested,
	SteadyBacklogObserved,
	SteadyBacklogCatchUpRequested
};

enum class LiveEpochConvergenceActivation
{
	None,
	HardBlockRecovery,
	PacedPrime,
	SteadyBacklogRecovery
};

struct LiveEpochConvergenceInput
{
	uint64_t epoch = 0;
	bool epochActive = false;
	// The configured steady reserve is retained in the converted queue. Once a
	// synchronous downstream block and recovery prove that live work is stale,
	// raw backlog is rapidly processed while converted backlog is held at this
	// floor by a latest-wins policy.
	size_t vpConvertedDepth = 0;
	size_t desiredVpDepth = 0;
	bool targetConfigured = false;
	bool deliveryCompleted = false;
	bool deliverySucceeded = false;
	uint64_t deliveryDurationUs = 0;
	uint64_t nominalFrameDurationUs = 0;

	// Appended so existing aggregate callers stay source-compatible until they
	// explicitly provide the safety evidence required to request a trim.
	size_t vpRawDepth = 0;
	bool rawDepthKnown = false;
	bool resetOrFlushInProgress = false;
	// Retained for observation/compatibility. An explicit queue target takes
	// precedence over scene-history depth; the scene detector already falls back
	// to tagging its current confirmation frame when an older candidate is gone.
	bool sceneCadenceActive = false;
	// Monotonic milliseconds (GetTickCount64 domain).  Zero means timing-based
	// fallbacks are unavailable; the policy then remains conservative.
	uint64_t observationTickMs = 0;
};

struct LiveEpochConvergenceDecision
{
	LiveEpochConvergenceState previousState =
		LiveEpochConvergenceState::Disabled;
	LiveEpochConvergenceState state =
		LiveEpochConvergenceState::Disabled;
	LiveEpochConvergenceReason reason = LiveEpochConvergenceReason::None;
	// A request authorizes one live catch-up transition after either recovered
	// hard-block evidence or a locally backlogged, frame-paced ingress plateau:
	// activate the steady latest-wins policy, discard stale raw work, and reduce
	// converted work to desiredVpDepth. Final timestamps remain owned by the
	// delivery sequencer, so discarded pictures create no presentation-timeline
	// hole. A sustained raw backlog may request the same transaction again after
	// the initial convergence; transient one-frame handoff jitter cannot.
	bool requestConvergence = false;
	size_t staleVpFrames = 0;
	size_t staleRawFrames = 0;
	size_t staleConvertedFrames = 0;
	uint64_t epoch = 0;
	uint32_t successfulDeliveryCount = 0;
	uint32_t ingressBlockCount = 0;
	uint32_t consecutiveRecoveryDeliveryCount = 0;
	uint32_t consecutivePacedDeliveryCount = 0;
	uint64_t ingressBlockThresholdUs = 0;
	uint64_t normalDeliveryThresholdUs = 0;
	uint64_t pacedDeliveryMinimumUs = 0;
	uint64_t pacedDeliveryMaximumUs = 0;
	size_t pacedPrimingDepth = 0;
	uint64_t elapsedSinceFirstSuccessMs = 0;
	bool rawDepthKnown = false;
	bool rawBacklogObserved = false;
	bool targetIsConvertedQueue = true;
	LiveEpochConvergenceActivation activation =
		LiveEpochConvergenceActivation::None;
};

class LiveEpochConvergenceController
{
public:
	static constexpr uint64_t kMinimumIngressBlockUs = 30000;
	static constexpr uint64_t kIngressBlockPeriods = 3;
	static constexpr uint64_t kMaximumNormalDeliveryPeriods = 2;
	static constexpr uint32_t kRequiredRecoveryDeliveries = 3;
	static constexpr uint32_t kMinimumPacedWarmupDeliveries = 12;
	static constexpr uint32_t kRequiredPacedDeliveries = 6;
	static constexpr size_t kMinimumPacedPrimingDepth = 8;
	static constexpr uint64_t kBlockObservationTimeoutMs = 3000;
	static constexpr uint64_t kArmedConvergenceWindowMs = 2000;
	// A raw depth of one is normal thread handoff jitter. More than one raw
	// frame surviving six successful deliveries is a self-sustaining latency
	// reservoir: converted steady backpressure otherwise replaces exactly one
	// old raw frame for every newly captured frame and can never catch up.
	static constexpr size_t kSteadyRawBacklogTolerance = 1;
	static constexpr uint32_t kRequiredSteadyBacklogDeliveries = 6;

	LiveEpochConvergenceDecision Observe(
		const LiveEpochConvergenceInput& input);
	void Reset();

private:
	void BeginEpoch(uint64_t epoch, size_t desiredVpDepth);
	LiveEpochConvergenceDecision MakeDecision(
		const LiveEpochConvergenceInput& input,
		LiveEpochConvergenceState previousState,
		LiveEpochConvergenceReason reason) const;
	bool HasElapsed(uint64_t now, uint64_t since, uint64_t duration) const;
	uint64_t IngressBlockThresholdUs(uint64_t nominalFrameDurationUs) const;
	uint64_t NormalDeliveryThresholdUs(uint64_t nominalFrameDurationUs) const;
	uint64_t PacedDeliveryMinimumUs(uint64_t nominalFrameDurationUs) const;
	uint64_t PacedDeliveryMaximumUs(uint64_t nominalFrameDurationUs) const;
	size_t PacedPrimingDepth() const;
	bool IsIngressBlocked(const LiveEpochConvergenceInput& input) const;
	bool IsNormalDelivery(const LiveEpochConvergenceInput& input) const;
	bool IsPacedDelivery(const LiveEpochConvergenceInput& input) const;
	bool HasPacedPrimeEvidence(const LiveEpochConvergenceInput& input) const;
	void ResetPacedEvidence();
	bool IsTerminal() const;
	bool CanRequestTrim(const LiveEpochConvergenceInput& input) const;
	LiveEpochConvergenceDecision ObserveSteadyBacklog(
		const LiveEpochConvergenceInput& input,
		LiveEpochConvergenceState previousState);

	bool m_initialized = false;
	uint64_t m_epoch = 0;
	size_t m_desiredVpDepth = 0;
	uint32_t m_successfulDeliveryCount = 0;
	uint32_t m_ingressBlockCount = 0;
	uint32_t m_consecutiveRecoveryDeliveryCount = 0;
	uint32_t m_consecutivePacedDeliveryCount = 0;
	uint64_t m_pacedWindowDurationUs = 0;
	bool m_hasFirstSuccessTick = false;
	uint64_t m_firstSuccessTickMs = 0;
	bool m_hasArmedTick = false;
	uint64_t m_armedTickMs = 0;
	bool m_observationTimeoutReported = false;
	uint32_t m_consecutiveSteadyBacklogDeliveries = 0;
	LiveEpochConvergenceState m_state =
		LiveEpochConvergenceState::Disabled;
};

const char* ToString(LiveEpochConvergenceState state);
const char* ToString(LiveEpochConvergenceReason reason);
const char* ToString(LiveEpochConvergenceActivation activation);
