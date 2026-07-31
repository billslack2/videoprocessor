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
	AwaitingPreResetPrime,
	AwaitingPostReadyReset,
	AwaitingPrefill,
	Ready
};

struct OutputReadinessInput
{
	uint64_t transitionGeneration = 0;
	bool graphOperational = false;
	DisplayRefreshRateDecision displayDecision =
		DisplayRefreshRateDecision::Unavailable;
	DisplayRefreshRateReason displayReason =
		DisplayRefreshRateReason::NoSamples;
	double expectedOutputRefreshHz = 0.0;
	double observedOutputRefreshHz = 0.0;
	bool postReadyResetCompleted = false;
	uint64_t postReadyEpoch = 0;
	size_t currentEpochProcessedDepth = 0;
	// A VP-owned converted-frame reservoir that must exist before the single
	// graph re-prime. Zero preserves immediate reset behaviour for callers that
	// do not use the deterministic priming policy.
	size_t preResetPrimeFrames = 0;
	size_t reserveFrames = 0;
};

struct OutputReadinessDecision
{
	OutputReadinessState state = OutputReadinessState::OutputNotReady;
	OutputReadinessReason reason = OutputReadinessReason::AwaitingGraph;
	bool discardLiveCapture = true;
	bool admitCurrentEpochCapture = false;
	bool requestSerializedPostReadyReset = false;
	bool allowDownstreamDelivery = false;
	bool prefillSatisfied = false;
	uint64_t transitionGeneration = 0;
	uint64_t postReadyEpoch = 0;
};

class OutputReadinessController
{
public:
	OutputReadinessDecision Observe(const OutputReadinessInput& input);
	void Reset();

private:
	bool m_initialized = false;
	uint64_t m_transitionGeneration = 0;
	OutputReadinessState m_state = OutputReadinessState::OutputNotReady;
	uint64_t m_postReadyEpoch = 0;
	bool m_resetRequested = false;
};

const char* ToString(OutputReadinessState state);
const char* ToString(OutputReadinessReason reason);
