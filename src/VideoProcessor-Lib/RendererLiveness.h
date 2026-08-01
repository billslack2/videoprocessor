#pragma once

#include <cstddef>
#include <cstdint>

// Lock-free progress evidence published by renderer hot paths. Tick values use
// GetTickCount64() and are zero until that stage has made progress.
struct RendererLivenessSnapshot
{
	bool supported = false;
	bool active = false;
	bool buffering = false;
	bool deliveryInProgress = false;
	bool resetInProgress = false;
	uint32_t captureThreadId = 0;
	uint32_t conversionThreadId = 0;
	uint32_t deliveryThreadId = 0;
	uint64_t queueEpoch = 0;
	uint64_t inputCount = 0;
	uint64_t conversionCount = 0;
	uint64_t dequeueCount = 0;
	uint64_t deliveryAttemptCount = 0;
	uint64_t deliverySuccessCount = 0;
	uint64_t currentEpochDeliverySuccessCount = 0;
	uint64_t lastDeliverySuccessQueueEpoch = 0;
	uint64_t lastInputTick = 0;
	uint64_t lastConversionTick = 0;
	uint64_t lastDequeueTick = 0;
	uint64_t lastDeliveryStartTick = 0;
	uint64_t lastDeliverySuccessTick = 0;
	size_t rawQueueDepth = 0;
	size_t convertedQueueDepth = 0;
	size_t queueCapacity = 0;
	// Zero means normal legacy drain policy. A nonzero value is a VP-owned
	// converted-frame reserve; it never describes renderer-internal queues.
	size_t deliveryReserveFrames = 0;
};

// Latency boundaries owned by VP. "Scheduled" ends at the DirectShow sample's
// requested presentation time; it is not a claim about madVR, scanout, or the
// physical display.
struct RendererLatencySnapshot
{
	bool supported = false;
	bool scheduledPresentationKnown = false;
	double vpInternalMs = 0.0;
	double dsScheduleLeadMs = 0.0;
	double scheduledLatencyMs = 0.0;
};

inline bool CalculateVpInternalLatency(
	uint64_t vpArrivalTickMs,
	uint64_t observationTickMs,
	RendererLatencySnapshot& snapshot)
{
	if (vpArrivalTickMs == 0 || observationTickMs < vpArrivalTickMs)
		return false;

	snapshot.supported = true;
	snapshot.vpInternalMs = static_cast<double>(
		observationTickMs - vpArrivalTickMs);
	return true;
}

inline bool CalculateScheduledLatency(
	uint64_t vpArrivalTickMs,
	uint64_t observationTickMs,
	int64_t presentationStart100ns,
	int64_t streamTime100ns,
	RendererLatencySnapshot& snapshot)
{
	if (!CalculateVpInternalLatency(
		vpArrivalTickMs, observationTickMs, snapshot))
		return false;

	snapshot.scheduledPresentationKnown = true;
	snapshot.dsScheduleLeadMs = static_cast<double>(
		presentationStart100ns - streamTime100ns) / 10000.0;
	snapshot.scheduledLatencyMs =
		snapshot.vpInternalMs + snapshot.dsScheduleLeadMs;
	return true;
}

constexpr uint64_t MINIMUM_CURRENT_EPOCH_DELIVERIES = 5;

inline bool HasSufficientDownstreamPreroll(uint64_t deliveryCount)
{
	return deliveryCount >= MINIMUM_CURRENT_EPOCH_DELIVERIES;
}

inline bool HasCurrentEpochDownstreamDelivery(
	const RendererLivenessSnapshot& snapshot)
{
	return snapshot.supported &&
		snapshot.active &&
		!snapshot.buffering &&
		!snapshot.resetInProgress &&
		HasSufficientDownstreamPreroll(
			snapshot.currentEpochDeliverySuccessCount) &&
		snapshot.lastDeliverySuccessTick > 0 &&
		snapshot.lastDeliverySuccessQueueEpoch == snapshot.queueEpoch;
}
