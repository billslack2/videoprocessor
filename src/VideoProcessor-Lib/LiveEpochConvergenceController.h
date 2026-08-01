/*
 * One-shot, fresh-epoch convergence policy for VP's converted live queue.
 *
 * This controller deliberately owns no DirectShow, madVR, HDMI, or queue
 * object.  In particular, an S_OK from Deliver is evidence only that the
 * DirectShow ingress accepted a sample; it is not evidence of madVR queue
 * occupancy or presentation.  The owner must execute a requested TrimTo and
 * report the actual result separately.
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
	DeferredRawNotEmpty,
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
	RawDepthNotEmpty,
	TrimRequested,
	BlockObservationTimedOut,
	ArmedWindowTimedOut,
	DeliveryFailed,
	UnsafeBoundary,
	TargetChangedWithinEpoch
};

struct LiveEpochConvergenceInput
{
	uint64_t epoch = 0;
	bool epochActive = false;
	// This policy's target is converted-queue depth only.  Raw frames are
	// never discarded by it; rawDepthKnown && vpRawDepth == 0 is a required
	// precondition for an automatic converted-queue trim.
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
	// Scene cadence may depend on buffered scene-boundary candidates. Do not
	// remove converted work while that planner is active.
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
	// A request authorizes only a converted-queue TrimTo(desiredVpDepth).  It
	// is emitted once per epoch and only with raw depth explicitly observed as
	// zero.  `staleVpFrames` is the requested converted-frame count, not a
	// claim about total VP R/C/T depth.
	bool requestConvergence = false;
	size_t staleVpFrames = 0;
	uint64_t epoch = 0;
	uint32_t successfulDeliveryCount = 0;
	uint32_t ingressBlockCount = 0;
	uint32_t consecutiveRecoveryDeliveryCount = 0;
	uint64_t ingressBlockThresholdUs = 0;
	uint64_t normalDeliveryThresholdUs = 0;
	uint64_t elapsedSinceFirstSuccessMs = 0;
	bool rawZeroPreconditionMet = false;
	bool targetIsConvertedQueue = true;
};

class LiveEpochConvergenceController
{
public:
	static constexpr uint64_t kMinimumIngressBlockUs = 30000;
	static constexpr uint64_t kIngressBlockPeriods = 3;
	static constexpr uint64_t kMaximumNormalDeliveryPeriods = 2;
	static constexpr uint32_t kRequiredRecoveryDeliveries = 3;
	static constexpr uint64_t kBlockObservationTimeoutMs = 3000;
	static constexpr uint64_t kArmedConvergenceWindowMs = 2000;

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
	bool IsIngressBlocked(const LiveEpochConvergenceInput& input) const;
	bool IsNormalDelivery(const LiveEpochConvergenceInput& input) const;
	bool IsTerminal() const;
	bool CanRequestTrim(const LiveEpochConvergenceInput& input) const;

	bool m_initialized = false;
	uint64_t m_epoch = 0;
	size_t m_desiredVpDepth = 0;
	uint32_t m_successfulDeliveryCount = 0;
	uint32_t m_ingressBlockCount = 0;
	uint32_t m_consecutiveRecoveryDeliveryCount = 0;
	bool m_hasFirstSuccessTick = false;
	uint64_t m_firstSuccessTickMs = 0;
	bool m_hasArmedTick = false;
	uint64_t m_armedTickMs = 0;
	LiveEpochConvergenceState m_state =
		LiveEpochConvergenceState::Disabled;
};

const char* ToString(LiveEpochConvergenceState state);
const char* ToString(LiveEpochConvergenceReason reason);
