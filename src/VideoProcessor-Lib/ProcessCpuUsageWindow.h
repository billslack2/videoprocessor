#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Time-weighted process CPU samples over the trailing ten seconds.
// Call at most once per second, after reading the OS process counters.
class ProcessCpuUsageWindow
{
public:
    static constexpr uint64_t WINDOW_MS = 10000;
    static constexpr size_t WINDOW_CAPACITY = 16;
    void Reset() { *this = ProcessCpuUsageWindow{}; }
    double AveragePercent() const { return m_windowAveragePercent; }
    double PeakPercent() const { return m_windowPeakPercent; }
	struct WindowSample
	{
		uint64_t tick = 0;
		uint64_t elapsedMs = 0;
		double percent = 0.0;
	};

	void Record(uint64_t nowTick, uint64_t elapsedMs,
		double percent)
	{
		if (elapsedMs == 0 || elapsedMs > nowTick || !std::isfinite(percent))
			return;
		while (m_windowCount > 0 &&
			nowTick - m_windowSamples[m_windowHead].tick >= WINDOW_MS)
		{
			m_windowHead = (m_windowHead + 1) % WINDOW_CAPACITY;
			--m_windowCount;
		}
		if (m_windowCount == WINDOW_CAPACITY)
		{
			m_windowHead = (m_windowHead + 1) % WINDOW_CAPACITY;
			--m_windowCount;
		}
		WindowSample& sample =
			m_windowSamples[(m_windowHead + m_windowCount) % WINDOW_CAPACITY];
		sample.tick = nowTick;
		sample.elapsedMs = elapsedMs;
		sample.percent = percent;
		++m_windowCount;

		double weightedTotal = 0.0;
		uint64_t totalElapsedMs = 0;
		m_windowPeakPercent = 0.0;
		for (size_t index = 0; index < m_windowCount; ++index)
		{
			const WindowSample& current =
				m_windowSamples[(m_windowHead + index) % WINDOW_CAPACITY];
			// Clip the oldest interval to the trailing window, including long UI gaps.
			const uint64_t overlapMs = (std::min)(current.elapsedMs,
				WINDOW_MS - (nowTick - current.tick));
			weightedTotal += current.percent * static_cast<double>(overlapMs);
			totalElapsedMs += overlapMs;
			m_windowPeakPercent =
				(std::max)(m_windowPeakPercent, current.percent);
		}
		m_windowAveragePercent = totalElapsedMs > 0 ?
			weightedTotal / static_cast<double>(totalElapsedMs) : 0.0;
	}

private:
	std::array<WindowSample, WINDOW_CAPACITY> m_windowSamples{};
	size_t m_windowHead = 0;
	size_t m_windowCount = 0;
	double m_windowAveragePercent = 0.0;
	double m_windowPeakPercent = 0.0;
};
