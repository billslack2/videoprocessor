#pragma once

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace AlphaQueuePolicy
{
	constexpr size_t DEFAULT_DESIRED_DEPTH = 4;
	constexpr double DEFAULT_FRAME_RATE_HZ = 60.0;
	constexpr double MINIMUM_RENDER_STALL_MS = 50.0;
	constexpr double MINIMUM_STALE_BACKLOG_AGE_MS = 100.0;

	struct ActivePicturePreviewWindow
	{
		size_t availableFutureFrames = 0;
		size_t effectiveFutureFrames = 0;
		std::vector<size_t> indices;
	};

	// Snapshot selection only: never changes queue depth or waits for evidence.
	// Each queue entry exposes cadenceRepeat; repeats cannot add proof votes.
	template<typename Queue>
	ActivePicturePreviewWindow SelectActivePicturePreview(const Queue& queue,
		size_t configured, size_t maxLookahead)
	{
		ActivePicturePreviewWindow result;
		if (queue.empty()) return result;
		const size_t requested = (std::min)(configured, maxLookahead);
		const bool inspect = requested > 0 && !queue.front().cadenceRepeat;
		for (size_t index = 0; index < queue.size(); ++index)
		{
			if (queue[index].cadenceRepeat) continue;
			// Count the complete buffered source window even when work is capped
			// or disabled. The current queue entry is not a future frame.
			if (index != 0) ++result.availableFutureFrames;
			if (inspect && result.indices.size() <= requested)
				result.indices.push_back(index);
		}
		result.effectiveFutureFrames = result.indices.empty() ? 0 : result.indices.size() - 1;
		return result;
	}

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

	inline double ValidFrameRateOrDefault(double frameRateHz)
	{
		return frameRateHz >= 10.0 && frameRateHz <= 240.0 ?
			frameRateHz : DEFAULT_FRAME_RATE_HZ;
	}

	inline double RenderStallThresholdMs(double frameRateHz)
	{
		const double framePeriodMs =
			1000.0 / ValidFrameRateOrDefault(frameRateHz);
		return std::max(MINIMUM_RENDER_STALL_MS, framePeriodMs * 2.0);
	}

	inline double StaleBacklogAgeThresholdMs(double frameRateHz)
	{
		const double framePeriodMs =
			1000.0 / ValidFrameRateOrDefault(frameRateHz);
		return std::max(MINIMUM_STALE_BACKLOG_AGE_MS, framePeriodMs * 3.0);
	}

	inline bool ShouldRecoverBacklog(size_t queueDepth, size_t desiredDepth,
		double oldestQueuedAgeMs, double renderCycleMs, double frameRateHz)
	{
		if (queueDepth <= NormalizeDesiredDepth(desiredDepth))
			return false;
		return renderCycleMs >= RenderStallThresholdMs(frameRateHz) ||
			oldestQueuedAgeMs >= StaleBacklogAgeThresholdMs(frameRateHz);
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
