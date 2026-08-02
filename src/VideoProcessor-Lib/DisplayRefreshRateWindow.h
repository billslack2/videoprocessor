/*
 * A bounded recent-history estimator for physical-vblank observations.
 *
 * The estimator intentionally has no DXGI, window, thread, or renderer
 * dependency. A display-mode change must be visible promptly: old intervals
 * age out of the rate and raw-gap evidence rather than diluting a new rate for
 * the lifetime of the sampler generation.
 */
#pragma once

#include <cstdint>
#include <deque>

struct DisplayRefreshRateWindowSnapshot
{
	double refreshRateHz = 0.0;
	double rawWaitRateHz = 0.0;
	int64_t minimumWaitIntervalQpc = 0;
	int64_t maximumWaitIntervalQpc = 0;
	uint64_t compensatedIntervals = 0;
	uint64_t rawWaitIntervals = 0;
};

class DisplayRefreshRateWindow
{
public:
	DisplayRefreshRateWindow(int64_t qpcFrequency, int64_t retentionQpc);

	void Add(int64_t endQpc, int64_t elapsedQpc,
		uint64_t compensatedIntervals);
	void Reset();
	DisplayRefreshRateWindowSnapshot Snapshot() const;

private:
	struct Sample
	{
		int64_t endQpc = 0;
		int64_t elapsedQpc = 0;
		uint64_t compensatedIntervals = 0;
	};

	void Trim(int64_t newestEndQpc);

	int64_t m_qpcFrequency = 0;
	int64_t m_retentionQpc = 0;
	std::deque<Sample> m_samples;
};
