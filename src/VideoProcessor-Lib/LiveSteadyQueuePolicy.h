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
	size_t discardOldest = 0;
};

// Pure policy for VP's post-prime converted queue. A literal zero setting has
// no retained reserve, but one sample must exist long enough for the delivery
// worker to consume it. The queue implementation removes oldest entries, so
// enforcing this decision is a latest-wins live policy.
class LiveSteadyQueuePolicy
{
public:
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
		if (input.convertedDepth > decision.highWater)
			decision.discardOldest = input.convertedDepth - decision.highWater;
		return decision;
	}
};
