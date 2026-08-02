#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Bounded duration evidence which accepts only consecutive source frames.
// A latest-wins discard spans multiple frame intervals and must not become a
// bogus single-frame CLOCK_SMART2 duration.
class ConsecutiveFrameDurationHistory
{
public:
	static constexpr size_t CAPACITY = 100;
	static constexpr int64_t MINIMUM_DURATION_100NS = 50000;
	static constexpr int64_t MAXIMUM_DURATION_100NS = 10000000;

	bool Observe(
		uint64_t frameNumber,
		int64_t timestamp100ns,
		int64_t nominalDuration100ns) noexcept
	{
		if (!m_hasPrevious)
		{
			Rebaseline(frameNumber, timestamp100ns);
			return false;
		}

		const bool consecutive = frameNumber == m_previousFrameNumber + 1;
		const int64_t duration = timestamp100ns - m_previousTimestamp100ns;
		Rebaseline(frameNumber, timestamp100ns);
		const bool nominalValid =
			nominalDuration100ns >= MINIMUM_DURATION_100NS &&
			nominalDuration100ns <= MAXIMUM_DURATION_100NS;
		const int64_t nominalMinimum = nominalDuration100ns / 2;
		const int64_t nominalMaximum = nominalDuration100ns +
			nominalDuration100ns / 2;
		if (!consecutive || !nominalValid ||
			duration < MINIMUM_DURATION_100NS ||
			duration > MAXIMUM_DURATION_100NS ||
			duration < nominalMinimum || duration > nominalMaximum)
			return false;

		if (m_count == CAPACITY)
			m_sum -= m_durations[m_index];
		else
			++m_count;
		m_durations[m_index] = duration;
		m_sum += duration;
		m_index = (m_index + 1) % CAPACITY;
		return true;
	}

	int64_t AverageOr(int64_t fallback) const noexcept
	{
		return m_count == 0 ? fallback : m_sum / static_cast<int64_t>(m_count);
	}

	size_t Count() const noexcept { return m_count; }

	void Reset() noexcept
	{
		m_durations.fill(0);
		m_index = 0;
		m_count = 0;
		m_sum = 0;
		m_hasPrevious = false;
		m_previousFrameNumber = 0;
		m_previousTimestamp100ns = 0;
	}

private:
	void Rebaseline(uint64_t frameNumber, int64_t timestamp100ns) noexcept
	{
		m_hasPrevious = true;
		m_previousFrameNumber = frameNumber;
		m_previousTimestamp100ns = timestamp100ns;
	}

	std::array<int64_t, CAPACITY> m_durations{};
	size_t m_index = 0;
	size_t m_count = 0;
	int64_t m_sum = 0;
	bool m_hasPrevious = false;
	uint64_t m_previousFrameNumber = 0;
	int64_t m_previousTimestamp100ns = 0;
};
