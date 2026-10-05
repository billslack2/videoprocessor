#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

// CPU preparation time on the VP conversion worker. The window is measured
// by elapsed time, so it remains comparable across capture frame rates.
class AlphaConversionTimingWindow
{
public:
	static constexpr uint64_t WINDOW_MS = 10000;

	void Reset()
	{
		m_samples.clear();
	}

	void Record(uint64_t nowTick, double durationUs)
	{
		Evict(nowTick);
		if (durationUs > 0.0)
			m_samples.emplace_back(nowTick, durationUs);
	}

	bool Snapshot(uint64_t nowTick, double& latestUs,
		double& averageUs, double& peakUs) const
	{
		latestUs = averageUs = peakUs = 0.0;
		size_t count = 0;
		for (const auto& sample : m_samples)
		{
			if (sample.first > nowTick ||
				nowTick - sample.first > WINDOW_MS)
				continue;
			latestUs = sample.second;
			averageUs += sample.second;
			peakUs = (std::max)(peakUs, sample.second);
			++count;
		}
		if (count == 0)
			return false;
		averageUs /= static_cast<double>(count);
		return true;
	}

private:
	void Evict(uint64_t nowTick)
	{
		while (!m_samples.empty() &&
			(nowTick < m_samples.front().first ||
				nowTick - m_samples.front().first > WINDOW_MS))
			m_samples.pop_front();
	}

	std::deque<std::pair<uint64_t, double>> m_samples;
};
