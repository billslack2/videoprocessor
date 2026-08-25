/*
 * Deterministic renderer-readiness and post-ready prefill state machine.
 *
 * This component consumes a caller-validated renderer/display refresh
 * observation. It has no DirectShow, madVR, queue, thread, or HDMI API
 * dependency. In particular, acceptance means renderer readiness only; it is
 * not a claim that a physical HDMI sink has completed its handshake. Normal
 * live delivery remains open while evidence is collected; only the new epoch
 * created by the accepted reset is gated for exact prefill.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include <DisplayRefreshRatePolicy.h>

enum class OutputReadinessState
{
	OutputNotReady,
	PostReadyResetPending,
	Prefilling,
	PostResetValidating,
	ManualRecoveryRequired,
	Steady
};

enum class OutputReadinessReason
{
	AwaitingGraph,
	AwaitingDisplayMeasurement,
	DisplayMeasurementRejected,
	OutputRefreshFamilyMismatch,
	CurrentGraphPrimeAdopted,
	AwaitingPostReadySettle,
	AwaitingPostReadyReset,
	AwaitingPrefill,
	AwaitingPostResetValidation,
	AwaitingCorrectivePostReadyReset,
	PostResetValidationFailedManualRecovery,
	Ready
};

enum OutputReadinessValidationBlocker : uint32_t
{
	OutputReadinessValidationBlockerNone = 0,
	OutputReadinessValidationBlockerQueueEpoch = 1u << 0,
	OutputReadinessValidationBlockerPrimeProof = 1u << 1,
	OutputReadinessValidationBlockerFullPrime = 1u << 2,
	OutputReadinessValidationBlockerBoundary = 1u << 3,
	OutputReadinessValidationBlockerRecentDelivery = 1u << 4,
	OutputReadinessValidationBlockerTransitionGeneration = 1u << 5,
	OutputReadinessValidationBlockerPrimeEpoch = 1u << 6,
	OutputReadinessValidationBlockerPrimeTarget = 1u << 7,
	OutputReadinessValidationBlockerReserve = 1u << 8,
	OutputReadinessValidationBlockerPostProofDeliveries = 1u << 9,
	OutputReadinessValidationBlockerUnexpectedGap = 1u << 10,
	OutputReadinessValidationBlockerRawDepth = 1u << 11,
	OutputReadinessValidationBlockerRetainedSource = 1u << 12,
	OutputReadinessValidationBlockerConvertedEnvelope = 1u << 13
};

struct OutputReadinessInput
{
	uint64_t transitionGeneration = 0;
	// Corrective re-prime budget identity. A display-measurement generation may
	// change during a manual graph reset without creating a new renderer or
	// effective launch contract, so it must not replenish the automatic retry.
	// A zero recovery generation falls back to transitionGeneration.
	uint64_t correctiveRecoveryGeneration = 0;
	uint64_t correctiveRecoveryContractRevision = 0;
	bool graphOperational = false;
	uint64_t observationTickMs = 0;
	DisplayRefreshRateDecision displayDecision =
		DisplayRefreshRateDecision::Unavailable;
	DisplayRefreshRateReason displayReason =
		DisplayRefreshRateReason::NoSamples;
	double expectedOutputRefreshHz = 0.0;
	double observedOutputRefreshHz = 0.0;
	bool postReadyResetCompleted = false;
	uint64_t postReadyEpoch = 0;
	size_t currentEpochProcessedDepth = 0;
	size_t reserveFrames = 0;
	// A current DirectShow epoch may already be completing the same opaque
	// downstream-prime transaction that the post-ready reset would repeat.
	// These fields describe only VP-owned recovery/convergence evidence. They
	// never claim that madVR queue occupancy is observable.
	bool currentGraphPrimeProven = false;
	bool currentGraphPrimeObservedFullConvertedQueue = false;
	bool currentGraphBoundarySafe = false;
	// Exact post-reset validation excludes the legacy graph-health timer. It
	// already proves this epoch's convergence and delivery, so waiting on a
	// second timer can consume the retry solely because of observer phase.
	bool currentGraphPostResetBoundarySafe = false;
	bool currentGraphDeliveryRecent = false;
	uint64_t currentGraphPrimeTransitionGeneration = 0;
	uint64_t currentGraphQueueEpoch = 0;
	uint64_t currentGraphPrimeEpoch = 0;
	size_t currentGraphPrimeTargetFrames = 0;
	size_t currentGraphRawDepth = 0;
	size_t currentGraphConvertedDepth = 0;
	uint32_t currentGraphPostProofDeliverySuccesses = 0;
	uint64_t currentGraphMaximumSuccessfulDeliveryDurationUs = 0;
	size_t currentGraphRetainedSourceBufferCount = 0;
	uint64_t currentGraphUnexpectedLiveDeliveryGapEvents = 0;
	uint64_t currentGraphUnexpectedLiveDeliveryGapSlots = 0;
};

// Retains successful completion until the DirectShow pin publishes one
// coherent, non-zero epoch snapshot. Mixed-epoch reads are transient and must
// neither strand readiness nor trigger an immediate duplicate reset.
class OutputReadinessCompletionLatch
{
public:
	bool MarkCompleted(uint64_t transitionGeneration, uint64_t queueEpoch)
	{
		m_completedGeneration = 0;
		m_completedEpoch = 0;
		m_awaitingSnapshotGeneration = transitionGeneration;
		return BindStableSnapshot(transitionGeneration, queueEpoch);
	}

	bool BindStableSnapshot(uint64_t transitionGeneration, uint64_t queueEpoch)
	{
		if (m_awaitingSnapshotGeneration == 0 ||
			transitionGeneration == 0 || queueEpoch == 0)
		{
			return false;
		}
		if (transitionGeneration != m_awaitingSnapshotGeneration)
		{
			Reset();
			return false;
		}
		m_completedGeneration = transitionGeneration;
		m_completedEpoch = queueEpoch;
		m_awaitingSnapshotGeneration = 0;
		return true;
	}

	bool Matches(uint64_t transitionGeneration) const
	{
		return transitionGeneration != 0 &&
			m_completedGeneration == transitionGeneration &&
			m_completedEpoch != 0;
	}

	uint64_t CompletedEpoch() const { return m_completedEpoch; }
	uint64_t AwaitingSnapshotGeneration() const
	{
		return m_awaitingSnapshotGeneration;
	}

	void Reset()
	{
		m_awaitingSnapshotGeneration = 0;
		m_completedGeneration = 0;
		m_completedEpoch = 0;
	}

private:
	uint64_t m_awaitingSnapshotGeneration = 0;
	uint64_t m_completedGeneration = 0;
	uint64_t m_completedEpoch = 0;
};

struct OutputReadinessDecision
{
	OutputReadinessState state = OutputReadinessState::OutputNotReady;
	OutputReadinessReason reason = OutputReadinessReason::AwaitingGraph;
	bool discardLiveCapture = true;
	bool admitCurrentEpochCapture = false;
	bool requestSerializedPostReadyReset = false;
	bool adoptedCurrentGraph = false;
	bool allowDownstreamDelivery = false;
	bool prefillSatisfied = false;
	uint64_t transitionGeneration = 0;
	uint64_t postReadyEpoch = 0;
	uint64_t readinessValidatedTickMs = 0;
	uint32_t postReadySettleElapsedMs = 0;
	uint32_t postReadySettleRequiredMs = 0;
	uint32_t postResetValidationElapsedMs = 0;
	uint32_t postResetValidationStableElapsedMs = 0;
	uint32_t postResetValidationStableObservationCount = 0;
	uint32_t postResetValidationBlockers =
		OutputReadinessValidationBlockerNone;
	bool correctiveReprimeAttempted = false;
	bool manualRecoveryRequired = false;
};

class OutputReadinessController
{
public:
	static constexpr uint32_t kRequiredPostProofDeliveries = 3;
	static constexpr uint32_t kRequiredPostResetValidationDeliveries = 16;
	static constexpr size_t kMaximumAdoptionRawDepth = 1;
	static constexpr size_t kMaximumPostResetRetainedSourceBuffers = 1;
	static constexpr uint32_t kPostReadySettleMs = 2000;
	static constexpr uint32_t kPostResetValidationStableMs = 250;
	static constexpr uint32_t kPostResetValidationDeadlineMs = 2000;
	// Readiness is observed by a nominal one-second WM_TIMER. Healthy evidence
	// must begin by the deadline, but one already-open stable window gets one
	// bounded jitter allowance to reach its second observation.
	static constexpr uint32_t kPostResetValidationCompletionGraceMs = 1250;
	static constexpr uint32_t kHandshakeScaleBlockPeriods = 16;
	static constexpr uint64_t kMaximumAdoptableBlockDurationUs = 500000;

	OutputReadinessDecision Observe(const OutputReadinessInput& input);
	void RearmResetRequest();
	void Reset();

private:
	bool CanAdoptCurrentGraph(const OutputReadinessInput& input) const;
	uint32_t PostResetValidationBlockers(
		const OutputReadinessInput& input) const;
	void BeginPostReadyEpoch(const OutputReadinessInput& input);
	void ClearValidationWindow();

	bool m_initialized = false;
	uint64_t m_transitionGeneration = 0;
	uint64_t m_correctiveRecoveryGeneration = 0;
	uint64_t m_correctiveRecoveryContractRevision = 0;
	OutputReadinessState m_state = OutputReadinessState::OutputNotReady;
	uint64_t m_postReadyEpoch = 0;
	size_t m_postReadyReserveFrames = 0;
	uint64_t m_readinessValidatedTickMs = 0;
	uint64_t m_validationStartedTickMs = 0;
	uint64_t m_validationStableStartedTickMs = 0;
	uint32_t m_validationStableObservationCount = 0;
	bool m_resetRequested = false;
	bool m_resetRequestIssued = false;
	bool m_correctiveReprimeAttempted = false;
	bool m_manualRecoveryRequired = false;
	uint64_t m_lastFailedValidationEpoch = 0;
	bool m_entryAdoptionCandidate = false;
	uint64_t m_entryAdoptionTransitionGeneration = 0;
	uint64_t m_entryAdoptionEpoch = 0;
	size_t m_entryAdoptionTargetFrames = 0;
};

const char* ToString(OutputReadinessState state);
const char* ToString(OutputReadinessReason reason);
