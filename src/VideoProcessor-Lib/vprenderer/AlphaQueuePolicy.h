#pragma once

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <string>

namespace AlphaQueuePolicy
{
	constexpr size_t DEFAULT_DESIRED_DEPTH = 4;

	inline size_t NormalizeDesiredDepth(size_t value)
	{
		return std::max<size_t>(1, value);
	}

	inline size_t ResolveDesiredDepth(size_t configuredOverride,
		size_t rememberedValue = DEFAULT_DESIRED_DEPTH)
	{
		return configuredOverride > 0 ?
			NormalizeDesiredDepth(configuredOverride) :
			NormalizeDesiredDepth(rememberedValue);
	}

	inline size_t HardCapacity(size_t configuredQueueSize)
	{
		// queue_size is user-visible and is the actual FIFO cap.  A lower
		// steady reserve controls presentation pacing; it must not shrink this.
		return NormalizeDesiredDepth(configuredQueueSize);
	}

	inline size_t ClampDesiredDepthToCapacity(size_t desiredDepth,
		size_t hardCapacity)
	{
		return std::min(NormalizeDesiredDepth(desiredDepth),
			HardCapacity(hardCapacity));
	}

	inline size_t HealthyLowWater(size_t desiredDepth)
	{
		const size_t desired = NormalizeDesiredDepth(desiredDepth);
		return desired > 1 ? desired - 1 : 0;
	}

	inline size_t HealthyHighWater(size_t desiredDepth)
	{
		const size_t desired = NormalizeDesiredDepth(desiredDepth);
		return desired;
	}

	inline bool CanDequeue(size_t queueDepth, size_t desiredDepth,
		bool startupPrefillPending)
	{
		if (queueDepth == 0)
			return false;

		const size_t desired = NormalizeDesiredDepth(desiredDepth);
		return startupPrefillPending ?
			queueDepth >= desired :
			queueDepth > HealthyLowWater(desired);
	}

	inline bool TryParsePositiveSize(const std::string& value, size_t& parsedValue)
	{
		if (value.empty())
			return false;
		for (const char character : value)
		{
			if (character < '0' || character > '9')
				return false;
		}

		errno = 0;
		char* end = nullptr;
		const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
		if (errno != 0 || end == value.c_str() || *end != '\0' || parsed == 0 ||
			parsed > static_cast<unsigned long long>(
				std::numeric_limits<size_t>::max()))
			return false;

		parsedValue = static_cast<size_t>(parsed);
		return true;
	}
}
