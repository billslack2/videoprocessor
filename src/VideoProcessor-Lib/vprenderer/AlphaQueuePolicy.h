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

	inline size_t HardCapacity(size_t desiredDepth)
	{
		// The configured queue size is user-visible and must be a true cap.
		// Keeping hidden headroom here made states such as 5/3 possible and
		// turned ordinary burst buffering into unexpected latency.
		return NormalizeDesiredDepth(desiredDepth);
	}

	inline size_t HealthyLowWater(size_t desiredDepth)
	{
		const size_t desired = NormalizeDesiredDepth(desiredDepth);
		return desired > 1 ? desired - 1 : 0;
	}

	inline size_t HealthyHighWater(size_t desiredDepth)
	{
		const size_t desired = NormalizeDesiredDepth(desiredDepth);
		const size_t capacity = HardCapacity(desired);
		return desired < capacity ? desired + 1 : desired;
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
