/*
 * DirectShow delivery result classification.
 *
 * This is deliberately independent of a graph, queue, renderer, and clock.
 * The output-pin coordinator retains the counters, logging cadence, and any
 * recovery action; this component makes the existing result classification
 * explicit and directly testable.
 */
#pragma once

#include <cstdint>

#include <windows.h>

enum class DirectShowDeliveryLatencyClass
{
	Instant,
	Normal,
	Slow
};

struct DirectShowDeliveryOutcomeInput
{
	HRESULT result = E_POINTER;
	uint64_t durationUs = 0;
	uint64_t slowDeliveryThresholdUs = 0;
};

struct DirectShowDeliveryOutcome
{
	DirectShowDeliveryLatencyClass latencyClass =
		DirectShowDeliveryLatencyClass::Instant;
	bool deliveryFailed = false;
	bool deliverySucceeded = false;
	bool countDroppedFrame = false;
	bool incrementRecentFailures = false;
	bool clearRecentFailures = false;
};

class DirectShowDeliveryOutcomeClassifier
{
public:
	DirectShowDeliveryOutcome Classify(
		const DirectShowDeliveryOutcomeInput& input) const;
};
