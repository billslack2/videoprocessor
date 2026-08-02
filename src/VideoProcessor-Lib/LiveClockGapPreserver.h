#pragma once

#include <cstdint>
#include <limits>


struct LiveClockGapDecision
{
	int64_t adjustedHardwareTime = 0;
	int64_t cumulativeOffset = 0;
	int64_t repair = 0;
	int64_t hardwareAdvance = 0;
	int64_t expectedAdvance = 0;
	uint64_t missingFrames = 0;
	bool applied = false;

	int64_t MaximumPermittedAdvance(int64_t legacyMaximum) const noexcept
	{
		return expectedAdvance > legacyMaximum ?
			expectedAdvance : legacyMaximum;
	}
};

// DeckLink source counters and hardware timestamps describe two independent
// parts of the live timeline. During a short HDMI/source interruption the
// counter can report missing pictures while the hardware timestamp advances by
// only one frame. Without preserving the missing interval, clock-based sample
// PTS loses the same amount of renderer lead and an already-full madVR pipeline
// drains permanently.
//
// This value-only, epoch-local helper adds only the part of the expected source
// interval that the hardware clock did not already report. Intentional VP queue
// removal is excluded by the sourceDiscontinuity input, so convergence catch-up
// remains a separate operation and is never double-counted here.
class LiveClockGapPreserver
{
public:
	LiveClockGapDecision Observe(
		uint64_t sourceFrameNumber,
		bool sourceDiscontinuity,
		int64_t hardwareTime,
		int64_t nominalFrameDuration) noexcept
	{
		LiveClockGapDecision decision;
		decision.adjustedHardwareTime = SaturatingAdd(
			hardwareTime, m_cumulativeOffset);

		if (m_hasPrevious && sourceDiscontinuity &&
			sourceFrameNumber > m_previousSourceFrameNumber &&
			sourceFrameNumber - m_previousSourceFrameNumber > 1 &&
			hardwareTime >= m_previousHardwareTime &&
			nominalFrameDuration > 0)
		{
			decision.missingFrames = sourceFrameNumber -
				m_previousSourceFrameNumber - 1;
			const uint64_t elapsedFrameSlots =
				sourceFrameNumber - m_previousSourceFrameNumber;
			decision.expectedAdvance = SaturatingMultiply(
				elapsedFrameSlots, nominalFrameDuration);
			decision.hardwareAdvance =
				hardwareTime - m_previousHardwareTime;

			if (decision.expectedAdvance > decision.hardwareAdvance)
			{
				decision.repair = decision.expectedAdvance -
					decision.hardwareAdvance;
				m_cumulativeOffset = SaturatingAdd(
					m_cumulativeOffset, decision.repair);
				decision.cumulativeOffset = m_cumulativeOffset;
				decision.adjustedHardwareTime = SaturatingAdd(
					hardwareTime, m_cumulativeOffset);
				decision.applied = true;
			}
		}

		decision.cumulativeOffset = m_cumulativeOffset;
		m_previousSourceFrameNumber = sourceFrameNumber;
		m_previousHardwareTime = hardwareTime;
		m_hasPrevious = true;
		return decision;
	}

	void Reset() noexcept
	{
		m_hasPrevious = false;
		m_previousSourceFrameNumber = 0;
		m_previousHardwareTime = 0;
		m_cumulativeOffset = 0;
	}

	int64_t Offset() const noexcept { return m_cumulativeOffset; }

private:
	static int64_t SaturatingAdd(int64_t lhs, int64_t rhs) noexcept
	{
		const int64_t maximum = (std::numeric_limits<int64_t>::max)();
		const int64_t minimum = (std::numeric_limits<int64_t>::min)();
		if (rhs > 0 && lhs > maximum - rhs)
			return maximum;
		if (rhs < 0 && lhs < minimum - rhs)
			return minimum;
		return lhs + rhs;
	}

	static int64_t SaturatingMultiply(
		uint64_t lhs, int64_t rhs) noexcept
	{
		if (rhs <= 0 || lhs == 0)
			return 0;
		const uint64_t maximum = static_cast<uint64_t>(
			(std::numeric_limits<int64_t>::max)());
		if (lhs > maximum / static_cast<uint64_t>(rhs))
			return (std::numeric_limits<int64_t>::max)();
		return static_cast<int64_t>(lhs * static_cast<uint64_t>(rhs));
	}

	bool m_hasPrevious = false;
	uint64_t m_previousSourceFrameNumber = 0;
	int64_t m_previousHardwareTime = 0;
	int64_t m_cumulativeOffset = 0;
};
