#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>


class DirectShowEpochPrimePolicy
{
public:
	static constexpr uint64_t PrimeTimeoutMs = 2500;
	static constexpr size_t AllocatorHeadroomFrames = 1;
	static constexpr size_t MaximumRetainedRawQueueFrames = 3;

	// VP cannot observe madVR's private queue occupancy. A fresh DirectShow
	// segment therefore seeks downstream backpressure with the largest bounded
	// reservoir VP already owns, then the existing convergence controller trims
	// back to the configured steady queue target.
	static size_t PrimeTarget(
		size_t queueCapacity,
		size_t negotiatedAllocatorBuffers)
	{
		const size_t allocatorBound = negotiatedAllocatorBuffers > 0 ?
			(negotiatedAllocatorBuffers > AllocatorHeadroomFrames ?
				negotiatedAllocatorBuffers - AllocatorHeadroomFrames :
				negotiatedAllocatorBuffers) : queueCapacity;
		const size_t physicalBound = std::min(queueCapacity, allocatorBound);
		return physicalBound;
	}

	static size_t RawBridgeTarget(size_t maximumRawBridgeFrames)
	{
		// Always retain a small probe bridge. Active-profile settings can lag an
		// in-place media-type change until madVR sees the first new sample, so they
		// are diagnostic and must never reduce the launch reservoir.
		return maximumRawBridgeFrames;
	}

	static bool CanReleaseBuffering(
		uint64_t candidateEpoch,
		uint64_t currentEpoch,
		bool active,
		bool stopping,
		bool flushing,
		bool resetInProgress,
		size_t convertedDepth,
		size_t target,
		size_t rawDepth,
		size_t rawTarget,
		bool timedOut)
	{
		return candidateEpoch != 0 && candidateEpoch == currentEpoch &&
			active && !stopping && !flushing && !resetInProgress &&
			((convertedDepth >= target && rawDepth >= rawTarget) ||
				(timedOut && convertedDepth > 0));
	}

	static size_t ResolveBufferingTarget(
		size_t normalTarget,
		uint64_t currentEpoch,
		uint64_t primeEpoch,
		size_t primeTarget,
		size_t queueCapacity)
	{
		if (currentEpoch == 0 || currentEpoch != primeEpoch)
			return std::min(normalTarget, queueCapacity);
		return std::min(queueCapacity, std::max(normalTarget, primeTarget));
	}
};
