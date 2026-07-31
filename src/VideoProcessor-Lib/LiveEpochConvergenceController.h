/*
 * One-shot live-queue convergence policy for a fresh DirectShow epoch.
 *
 * The controller deliberately has no DirectShow, madVR, HDMI, queue, or
 * clock API dependency.  It consumes VP-observable delivery completions only.
 * A request means that an owner may remove stale VP-owned work and rebase its
 * presentation timeline; it never authorizes a raw sample discard by itself.
 */
#pragma once

#include <cstddef>
#include <cstdint>

enum class LiveEpochConvergenceState
{
	Disabled,
	AwaitingDownstreamPrime,
	AwaitingStableDelivery,
	Converged
};

struct LiveEpochConvergenceInput
{
	uint64_t epoch = 0;
	bool epochActive = false;
	size_t vpConvertedDepth = 0;
	size_t desiredVpDepth = 0;
	bool deliveryCompleted = false;
	bool deliverySucceeded = false;
	uint64_t deliveryDurationUs = 0;
	uint64_t nominalFrameDurationUs = 0;
};

struct LiveEpochConvergenceDecision
{
	LiveEpochConvergenceState state =
		LiveEpochConvergenceState::Disabled;
	bool requestConvergence = false;
	size_t staleVpFrames = 0;
	uint64_t epoch = 0;
};

class LiveEpochConvergenceController
{
public:
	LiveEpochConvergenceDecision Observe(
		const LiveEpochConvergenceInput& input);
	void Reset();

private:
	void BeginEpoch(uint64_t epoch, size_t desiredVpDepth);

	bool m_initialized = false;
	uint64_t m_epoch = 0;
	size_t m_desiredVpDepth = 0;
	uint32_t m_successfulDeliveryCount = 0;
	uint32_t m_consecutiveStableDeliveryCount = 0;
	LiveEpochConvergenceState m_state =
		LiveEpochConvergenceState::Disabled;
};

const char* ToString(LiveEpochConvergenceState state);
