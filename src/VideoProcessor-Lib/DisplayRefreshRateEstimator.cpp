#include <pch.h>

#include <DisplayRefreshRateEstimator.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kQuarantineSeconds = 5.0;
constexpr double kStartupEvidenceSeconds = 2.0;
constexpr double kReadinessEvidenceSeconds = 10.0;
constexpr double kPhaseEvidenceSeconds = 30.0;
constexpr double kRecentWindowSeconds = 30.0;
constexpr double kFastWindowSeconds = 2.0;
constexpr double kHistorySeconds = 120.0;
constexpr double kRecencyHalfLifeSeconds = 20.0;
constexpr double kMaterialRateChangeRatio = 0.005;
constexpr uint64_t kMinimumRecentRawIntervals = 12;

bool IsUsableRate(double value)
{
	return std::isfinite(value) && value >= 10.0 && value <= 240.0;
}
}

DisplayRefreshRateEstimator::DisplayRefreshRateEstimator(int64_t qpcFrequency)
	: m_qpcFrequency(qpcFrequency)
{
}

void DisplayRefreshRateEstimator::Reset()
{
	m_quarantineStartQpc = 0;
	m_startupFirstSampleQpc = 0;
	m_startupSamples.clear();
	m_samples.clear();
}

void DisplayRefreshRateEstimator::Observe(int64_t endQpc, int64_t elapsedQpc,
	uint64_t compensatedIntervals)
{
	if (m_qpcFrequency <= 0 || endQpc <= 0 || elapsedQpc <= 0 ||
		compensatedIntervals == 0)
	{
		return;
	}

	if (m_quarantineStartQpc == 0)
		m_quarantineStartQpc = endQpc;
	if (m_startupFirstSampleQpc == 0)
		m_startupFirstSampleQpc = endQpc;
	m_startupSamples.push_back({ endQpc, elapsedQpc, compensatedIntervals });
	const int64_t startupHistoryQpc = static_cast<int64_t>(
		(kStartupEvidenceSeconds * 2.0) * m_qpcFrequency);
	while (!m_startupSamples.empty() &&
		endQpc - m_startupSamples.front().endQpc > startupHistoryQpc)
	{
		m_startupSamples.pop_front();
	}

	if (endQpc - m_quarantineStartQpc <
		static_cast<int64_t>(kQuarantineSeconds * m_qpcFrequency))
		return;

	m_samples.push_back({ endQpc, elapsedQpc, compensatedIntervals });
	Trim(endQpc);
}

DisplayRefreshRateEstimatorSnapshot DisplayRefreshRateEstimator::Snapshot() const
{
	DisplayRefreshRateEstimatorSnapshot result;
	if (m_qpcFrequency <= 0 || m_quarantineStartQpc == 0)
		return result;

	const int64_t newestEndQpc = !m_samples.empty() ?
		m_samples.back().endQpc : (!m_startupSamples.empty() ?
			m_startupSamples.back().endQpc : m_quarantineStartQpc);
	result.quarantineComplete = newestEndQpc - m_quarantineStartQpc >=
		static_cast<int64_t>(kQuarantineSeconds * m_qpcFrequency);
	const WindowRate startup = CalculateWindowRate(m_startupSamples,
		static_cast<int64_t>(kStartupEvidenceSeconds * m_qpcFrequency));
	result.startupRateHz = startup.rateHz;
	result.startupEvidenceSeconds = m_startupFirstSampleQpc > 0 &&
		newestEndQpc >= m_startupFirstSampleQpc ?
		static_cast<double>(newestEndQpc - m_startupFirstSampleQpc) /
			static_cast<double>(m_qpcFrequency) : 0.0;
	result.startupCompensatedIntervals = startup.compensatedIntervals;
	result.startupRawIntervals = startup.rawIntervals;
	result.startupRawWaitRateHz = startup.rawWaitRateHz;
	result.startupMinimumWaitIntervalQpc = startup.minimumWaitIntervalQpc;
	result.startupMaximumWaitIntervalQpc = startup.maximumWaitIntervalQpc;
	result.startupEvidenceReady =
		result.startupEvidenceSeconds >= kStartupEvidenceSeconds &&
		startup.rawIntervals >= kMinimumRecentRawIntervals &&
		IsUsableRate(startup.rateHz);
	if (m_samples.empty())
		return result;

	result.evidenceSeconds = static_cast<double>(
		m_samples.back().endQpc - m_samples.front().endQpc) /
		static_cast<double>(m_qpcFrequency);
	const WindowRate recent = CalculateWindowRate(m_samples,
		static_cast<int64_t>(kRecentWindowSeconds * m_qpcFrequency));
	const WindowRate fast = CalculateWindowRate(m_samples,
		static_cast<int64_t>(kFastWindowSeconds * m_qpcFrequency));
	result.readinessRateHz = recent.rateHz;
	result.phaseRateHz = CalculateWeightedRate();
	result.fastRateHz = fast.rateHz;
	result.recentCompensatedIntervals = recent.compensatedIntervals;
	result.recentRawIntervals = recent.rawIntervals;
	result.recentRawWaitRateHz = recent.rawWaitRateHz;
	result.recentMinimumWaitIntervalQpc =
		recent.minimumWaitIntervalQpc;
	result.recentMaximumWaitIntervalQpc =
		recent.maximumWaitIntervalQpc;
	result.materialRateChangeDetected = IsUsableRate(result.fastRateHz) &&
		IsUsableRate(result.phaseRateHz) &&
		std::fabs(result.fastRateHz - result.phaseRateHz) /
			result.phaseRateHz >= kMaterialRateChangeRatio;
	result.readinessEvidenceReady = result.quarantineComplete &&
		result.evidenceSeconds >= kReadinessEvidenceSeconds &&
		recent.rawIntervals >= kMinimumRecentRawIntervals &&
		IsUsableRate(result.readinessRateHz) &&
		!result.materialRateChangeDetected;
	result.phaseEvidenceReady = result.quarantineComplete &&
		result.evidenceSeconds >= kPhaseEvidenceSeconds &&
		IsUsableRate(result.phaseRateHz) &&
		!result.materialRateChangeDetected;
	return result;
}

DisplayRefreshRateEstimator::WindowRate
DisplayRefreshRateEstimator::CalculateWindowRate(
	const std::deque<Sample>& samples, int64_t durationQpc) const
{
	WindowRate result;
	if (m_qpcFrequency <= 0 || durationQpc <= 0 || samples.empty())
		return result;

	const int64_t newestEndQpc = samples.back().endQpc;
	long double elapsedQpc = 0.0L;
	long double intervals = 0.0L;
	for (const Sample& sample : samples)
	{
		if (newestEndQpc - sample.endQpc > durationQpc)
			continue;
		elapsedQpc += sample.elapsedQpc;
		intervals += sample.compensatedIntervals;
		result.compensatedIntervals += sample.compensatedIntervals;
		++result.rawIntervals;
		if (result.minimumWaitIntervalQpc == 0 ||
			sample.elapsedQpc < result.minimumWaitIntervalQpc)
		{
			result.minimumWaitIntervalQpc = sample.elapsedQpc;
		}
		result.maximumWaitIntervalQpc = std::max(
			result.maximumWaitIntervalQpc, sample.elapsedQpc);
	}
	if (elapsedQpc > 0.0L)
	{
		result.rateHz = static_cast<double>(intervals *
			static_cast<long double>(m_qpcFrequency) / elapsedQpc);
		result.rawWaitRateHz = static_cast<double>(
			static_cast<long double>(result.rawIntervals) *
			static_cast<long double>(m_qpcFrequency) / elapsedQpc);
	}
	return result;
}

double DisplayRefreshRateEstimator::CalculateWeightedRate() const
{
	if (m_qpcFrequency <= 0 || m_samples.empty())
		return 0.0;

	const int64_t newestEndQpc = m_samples.back().endQpc;
	long double weightedElapsedQpc = 0.0L;
	long double weightedIntervals = 0.0L;
	for (const Sample& sample : m_samples)
	{
		const double ageSeconds = static_cast<double>(
			newestEndQpc - sample.endQpc) /
			static_cast<double>(m_qpcFrequency);
		const double weight = std::exp(-std::log(2.0) * ageSeconds /
			kRecencyHalfLifeSeconds);
		weightedElapsedQpc += weight * sample.elapsedQpc;
		weightedIntervals += weight * sample.compensatedIntervals;
	}
	return weightedElapsedQpc > 0.0L ? static_cast<double>(
		weightedIntervals * static_cast<long double>(m_qpcFrequency) /
		weightedElapsedQpc) : 0.0;
}

void DisplayRefreshRateEstimator::Trim(int64_t newestEndQpc)
{
	const int64_t historyQpc = static_cast<int64_t>(
		kHistorySeconds * m_qpcFrequency);
	while (!m_samples.empty() &&
		newestEndQpc - m_samples.front().endQpc > historyQpc)
	{
		m_samples.pop_front();
	}
}
