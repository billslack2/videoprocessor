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
	uint64_t lastInputTick = 0;
	uint64_t lastConversionTick = 0;
	uint64_t lastDequeueTick = 0;
	uint64_t lastDeliveryStartTick = 0;
	uint64_t lastDeliverySuccessTick = 0;
	size_t rawQueueDepth = 0;
	size_t convertedQueueDepth = 0;
	size_t queueCapacity = 0;
};
