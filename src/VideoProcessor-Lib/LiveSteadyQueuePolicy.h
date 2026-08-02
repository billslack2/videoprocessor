#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

struct LiveSteadyQueueInput
{
	uint64_t activeEpoch = 0;
	uint64_t currentEpoch = 0;
	bool targetConfigured = false;
	bool sceneCadenceActive = false;
	size_t configuredTarget = 0;
	size_t convertedDepth = 0;
};

struct LiveSteadyQueueDecision
{
	bool active = false;
	size_t highWater = 0;
	// In steady live operation, backpressure must be applied before conversion.
	// Dropping an already-converted sample makes the capture counter and output
	// timeline disagree; that can either manufacture a renderer repeat or make
	// the scheduled lead slowly drain.  A one-shot post-prime trim is still
	// allowed by the convergence controller, but normal steady ingress waits.
	bool holdConversion = false;
};

// Pure policy for VP's post-prime converted queue. A literal zero setting has
// no retained reserve, but one sample must exist long enough for the delivery
// worker to consume it. Once the one-shot startup catch-up has completed,
// enforcing this decision blocks conversion before another sample is stamped.
class LiveSteadyQueuePolicy
{
public:
	// Converted frames are the user-configured reserve. Raw DeckLink-backed
	// frames are only a handoff between capture and conversion once steady mode
	// is active; retaining more of them increases live latency without adding a
	// useful downstream cushion.
	static constexpr size_t SteadyRawHandoffCapacity() noexcept
	{
		return 1;
	}

	static LiveSteadyQueueDecision Evaluate(
		const LiveSteadyQueueInput& input) noexcept
	{
		LiveSteadyQueueDecision decision;
		decision.active = input.activeEpoch != 0 &&
			input.activeEpoch == input.currentEpoch &&
			input.targetConfigured;
		if (!decision.active)
			return decision;

		decision.highWater = std::max<size_t>(1, input.configuredTarget);
		decision.holdConversion =
			input.convertedDepth >= decision.highWater;
		return decision;
	}
};
