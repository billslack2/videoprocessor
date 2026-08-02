/*
 * Graph-independent bounded transport queue for live video work.
 *
 * The owner supplies the value type and release action.  The queue never
 * knows about DirectShow, samples, conversion, timing, renderer state, or
 * worker lifecycle.  A PipelineEpoch is only a stale-work boundary: callers
 * retain ownership of state transitions and wake/sleep policy.
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <limits>
#include <utility>

#include <VideoTimingController.h>

struct EpochBoundedQueueMetrics
{
	size_t depth = 0;
	size_t capacity = 0;
	uint64_t accepted = 0;
	uint64_t overflowDiscarded = 0;
	uint64_t staleDiscarded = 0;
	uint64_t flushed = 0;
};

enum class EpochBoundedQueuePushResult : uint8_t
{
	Accepted,
	AcceptedAfterOverflowDiscard,
	RejectedStale,
	RejectedNoCapacity
};

template <typename TValue, typename TRelease>
class EpochBoundedQueue
{
public:
	explicit EpochBoundedQueue(size_t capacity, TRelease release = TRelease())
		: m_capacity(capacity), m_release(std::move(release))
	{
	}

	EpochBoundedQueue(const EpochBoundedQueue&) = delete;
	EpochBoundedQueue& operator=(const EpochBoundedQueue&) = delete;

	~EpochBoundedQueue()
	{
		Flush();
	}

	EpochBoundedQueuePushResult Push(
		TValue value,
		PipelineEpoch valueEpoch,
		PipelineEpoch currentEpoch)
	{
		return PushWithMaximum(
			std::move(value), valueEpoch, currentEpoch,
			std::numeric_limits<size_t>::max());
	}

	EpochBoundedQueuePushResult PushWithMaximum(
		TValue value,
		PipelineEpoch valueEpoch,
		PipelineEpoch currentEpoch,
		size_t temporaryMaximum,
		size_t* discardedCount = nullptr)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (valueEpoch.value != currentEpoch.value)
		{
			Release(value);
			++m_metrics.staleDiscarded;
			return EpochBoundedQueuePushResult::RejectedStale;
		}
		const size_t effectiveCapacity =
			std::min(m_capacity, temporaryMaximum);
		if (effectiveCapacity == 0)
		{
			Release(value);
			return EpochBoundedQueuePushResult::RejectedNoCapacity;
		}

		bool overflow = false;
		size_t discarded = 0;
		while (m_entries.size() >= effectiveCapacity)
		{
			Release(m_entries.front().value);
			m_entries.pop_front();
			++m_metrics.overflowDiscarded;
			++discarded;
			overflow = true;
		}

		m_entries.push_back({ std::move(value), valueEpoch });
		if (discardedCount)
			*discardedCount = discarded;
		++m_metrics.accepted;
		m_metrics.depth = m_entries.size();
		return overflow ?
			EpochBoundedQueuePushResult::AcceptedAfterOverflowDiscard :
			EpochBoundedQueuePushResult::Accepted;
	}

	bool TryPopCurrent(PipelineEpoch currentEpoch, TValue& value)
	{
		return TryPopCurrentIfDepthAbove(currentEpoch, 0, value);
	}

	bool TryPopCurrentIfDepthAbove(
		PipelineEpoch currentEpoch,
		size_t minimumRemainingDepth,
		TValue& value)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		DiscardStaleHeadLocked(currentEpoch);
		if (m_entries.size() <= minimumRemainingDepth)
			return false;

		value = std::move(m_entries.front().value);
		m_entries.pop_front();
		m_metrics.depth = m_entries.size();
		return true;
	}

	template <typename TMutate>
	bool TryMutateCurrentFromBack(
		PipelineEpoch currentEpoch,
		size_t framesBack,
		TMutate mutate)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (framesBack == 0 || framesBack > m_entries.size())
			return false;
		Entry& entry = m_entries[m_entries.size() - framesBack];
		if (entry.epoch.value != currentEpoch.value)
			return false;
		mutate(entry.value);
		return true;
	}

	bool TryPeekCurrent(PipelineEpoch currentEpoch, TValue& value) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		// A stale front must be discarded by TryPopCurrent or Flush.  Peek is
		// intentionally read-only, so it fails closed rather than changing queue
		// ownership from a timestamp-query call.
		if (m_entries.empty() ||
			m_entries.front().epoch.value != currentEpoch.value)
			return false;
		value = m_entries.front().value;
		return true;
	}

	size_t Resize(size_t capacity)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_capacity = capacity;
		size_t discarded = 0;
		while (m_entries.size() > m_capacity)
		{
			Release(m_entries.front().value);
			m_entries.pop_front();
			++discarded;
			++m_metrics.overflowDiscarded;
		}
		m_metrics.depth = m_entries.size();
		return discarded;
	}

	void SetCapacityWithoutDiscard(size_t capacity)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_capacity = capacity;
	}

	size_t TrimTo(size_t maximumDepth)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		size_t discarded = 0;
		while (m_entries.size() > maximumDepth)
		{
			Release(m_entries.front().value);
			m_entries.pop_front();
			++discarded;
			++m_metrics.overflowDiscarded;
		}
		m_metrics.depth = m_entries.size();
		return discarded;
	}

	size_t Flush()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const size_t discarded = m_entries.size();
		while (!m_entries.empty())
		{
			Release(m_entries.front().value);
			m_entries.pop_front();
		}
		m_metrics.depth = 0;
		m_metrics.flushed += discarded;
		return discarded;
	}

	size_t Size() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_entries.size();
	}

	size_t CurrentDepth(PipelineEpoch currentEpoch) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		size_t depth = 0;
		for (const Entry& entry : m_entries)
		{
			if (entry.epoch.value == currentEpoch.value)
				++depth;
		}
		return depth;
	}

	EpochBoundedQueueMetrics Metrics() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		EpochBoundedQueueMetrics metrics = m_metrics;
		metrics.depth = m_entries.size();
		metrics.capacity = m_capacity;
		return metrics;
	}

private:
	struct Entry
	{
		TValue value;
		PipelineEpoch epoch;
	};

	void DiscardStaleHeadLocked(PipelineEpoch currentEpoch)
	{
		while (!m_entries.empty() &&
			m_entries.front().epoch.value != currentEpoch.value)
		{
			Release(m_entries.front().value);
			m_entries.pop_front();
			++m_metrics.staleDiscarded;
		}
		m_metrics.depth = m_entries.size();
	}

	void Release(TValue& value)
	{
		m_release(value);
	}

	mutable std::mutex m_mutex;
	std::deque<Entry> m_entries;
	size_t m_capacity = 0;
	TRelease m_release;
	EpochBoundedQueueMetrics m_metrics;
};
