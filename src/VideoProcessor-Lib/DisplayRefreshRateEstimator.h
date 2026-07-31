/*
 * Testable refresh-rate evidence model.
 *
 * It distinguishes post-transition quarantine, short readiness evidence, and
 * the longer weighted history used by phase-sensitive cadence correction.
 * This model has no DXGI, DirectShow, renderer, queue, or thread dependency.
 */
#pragma once

#include <cstdint>
#include <deque>

struct DisplayRefreshRateEstimatorSnapshot
{
	// Short evidence used to start the deterministic reset/prefill sequence.
	// It is distinct from post-transition rate confidence and phase history.
	double startupRateHz = 0.0;
	double startupEvidenceSeconds = 0.0;
	uint64_t startupCompensatedIntervals = 0;
	uint64_t startupRawIntervals = 0;
	double startupRawWaitRateHz = 0.0;
	int64_t startupMinimumWaitIntervalQpc = 0;
	int64_t startupMaximumWaitIntervalQpc = 0;
	double readinessRateHz = 0.0;
	double phaseRateHz = 0.0;
	double fastRateHz = 0.0;
	double evidenceSeconds = 0.0;
	uint64_t recentCompensatedIntervals = 0;
	uint64_t recentRawIntervals = 0;
	double recentRawWaitRateHz = 0.0;
	int64_t recentMinimumWaitIntervalQpc = 0;
	int64_t recentMaximumWaitIntervalQpc = 0;
	bool quarantineComplete = false;
	bool startupEvidenceReady = false;
	bool readinessEvidenceReady = false;
	bool phaseEvidenceReady = false;
	bool materialRateChangeDetected = false;
};

class DisplayRefreshRateEstimator
{
public:
	explicit DisplayRefreshRateEstimator(int64_t qpcFrequency);

	void Reset();
	void Observe(int64_t endQpc, int64_t elapsedQpc,
		uint64_t compensatedIntervals);
	DisplayRefreshRateEstimatorSnapshot Snapshot() const;

private:
	struct Sample
	{
		int64_t endQpc = 0;
		int64_t elapsedQpc = 0;
		uint64_t compensatedIntervals = 0;
	};

	struct WindowRate
	{
		double rateHz = 0.0;
		uint64_t compensatedIntervals = 0;
		uint64_t rawIntervals = 0;
		double rawWaitRateHz = 0.0;
		int64_t minimumWaitIntervalQpc = 0;
		int64_t maximumWaitIntervalQpc = 0;
	};

	WindowRate CalculateWindowRate(const std::deque<Sample>& samples,
		int64_t durationQpc) const;
	double CalculateWeightedRate() const;
	void Trim(int64_t newestEndQpc);

	int64_t m_qpcFrequency = 0;
	int64_t m_quarantineStartQpc = 0;
	int64_t m_startupFirstSampleQpc = 0;
	std::deque<Sample> m_startupSamples;
	std::deque<Sample> m_samples;
};
