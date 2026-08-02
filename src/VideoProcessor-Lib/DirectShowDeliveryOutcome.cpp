#include <pch.h>

#include <DirectShowDeliveryOutcome.h>

DirectShowDeliveryOutcome DirectShowDeliveryOutcomeClassifier::Classify(
	const DirectShowDeliveryOutcomeInput& input) const
{
	DirectShowDeliveryOutcome outcome;
	if (input.durationUs < 2000)
		outcome.latencyClass = DirectShowDeliveryLatencyClass::Instant;
	else if (input.durationUs <= input.slowDeliveryThresholdUs)
		outcome.latencyClass = DirectShowDeliveryLatencyClass::Normal;
	else
		outcome.latencyClass = DirectShowDeliveryLatencyClass::Slow;

	outcome.deliveryFailed = FAILED(input.result);
	outcome.deliverySucceeded = input.result == S_OK;
	outcome.deliveryRejected = input.result == S_FALSE;
	outcome.countDroppedFrame = outcome.deliveryFailed;
	outcome.incrementRecentFailures = outcome.deliveryFailed;
	outcome.clearRecentFailures = outcome.deliverySucceeded;
	return outcome;
}
