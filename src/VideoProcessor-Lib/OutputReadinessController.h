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
	Ready
};

struct OutputReadinessInput
{
	uint64_t transitionGeneration = 0;
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
	bool currentGraphDeliveryRecent = false;
	uint64_t currentGraphPrimeTransitionGeneration = 0;
	uint64_t currentGraphPrimeEpoch = 0;
	size_t currentGraphPrimeTargetFrames = 0;
	size_t currentGraphRawDepth = 0;
	size_t currentGraphConvertedDepth = 0;
	uint32_t currentGraphPostProofDeliverySuccesses = 0;
	uint64_t currentGraphMaximumSuccessfulDeliveryDurationUs = 0;
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
};

class OutputReadinessController
{
public:
	static constexpr uint32_t kRequiredPostProofDeliveries = 3;
	static constexpr size_t kMaximumAdoptionRawDepth = 1;
	static constexpr uint32_t kPostReadySettleMs = 2000;
	static constexpr uint32_t kHandshakeScaleBlockPeriods = 16;
	static constexpr uint64_t kMaximumAdoptableBlockDurationUs = 500000;

	OutputReadinessDecision Observe(const OutputReadinessInput& input);
	void RearmResetRequest();
	void Reset();

private:
	bool CanAdoptCurrentGraph(const OutputReadinessInput& input) const;

	bool m_initialized = false;
	uint64_t m_transitionGeneration = 0;
	OutputReadinessState m_state = OutputReadinessState::OutputNotReady;
	uint64_t m_postReadyEpoch = 0;
	uint64_t m_readinessValidatedTickMs = 0;
	bool m_resetRequested = false;
	bool m_resetRequestIssued = false;
	bool m_entryAdoptionCandidate = false;
	uint64_t m_entryAdoptionTransitionGeneration = 0;
	uint64_t m_entryAdoptionEpoch = 0;
	size_t m_entryAdoptionTargetFrames = 0;
};

const char* ToString(OutputReadinessState state);
const char* ToString(OutputReadinessReason reason);
