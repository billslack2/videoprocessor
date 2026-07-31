#include <pch.h>

#include <DisplayRefreshRateEstimator.h>

#include <cmath>

namespace
{
constexpr double kQuarantineSeconds = 5.0;
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
	{
		m_quarantineStartQpc = endQpc;
		return;
	}
	if (endQpc - m_quarantineStartQpc <
		static_cast<int64_t>(kQuarantineSeconds * m_qpcFrequency))
	{
		return;
	}

	m_samples.push_back({ endQpc, elapsedQpc, compensatedIntervals });
	Trim(endQpc);
}

DisplayRefreshRateEstimatorSnapshot DisplayRefreshRateEstimator::Snapshot() const
{
	DisplayRefreshRateEstimatorSnapshot result;
	if (m_qpcFrequency <= 0 || m_quarantineStartQpc == 0)
		return result;

	const int64_t newestEndQpc = m_samples.empty() ?
		m_quarantineStartQpc : m_samples.back().endQpc;
	result.quarantineComplete = newestEndQpc - m_quarantineStartQpc >=
		static_cast<int64_t>(kQuarantineSeconds * m_qpcFrequency);
	if (m_samples.empty())
		return result;

	result.evidenceSeconds = static_cast<double>(
		m_samples.back().endQpc - m_samples.front().endQpc) /
		static_cast<double>(m_qpcFrequency);
	const WindowRate recent = CalculateWindowRate(
		static_cast<int64_t>(kRecentWindowSeconds * m_qpcFrequency));
	const WindowRate fast = CalculateWindowRate(
		static_cast<int64_t>(kFastWindowSeconds * m_qpcFrequency));
	result.readinessRateHz = recent.rateHz;
	result.phaseRateHz = CalculateWeightedRate();
	result.fastRateHz = fast.rateHz;
	result.recentRawIntervals = recent.rawIntervals;
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
DisplayRefreshRateEstimator::CalculateWindowRate(int64_t durationQpc) const
{
	WindowRate result;
	if (m_qpcFrequency <= 0 || durationQpc <= 0 || m_samples.empty())
		return result;

	const int64_t newestEndQpc = m_samples.back().endQpc;
	long double elapsedQpc = 0.0L;
	long double intervals = 0.0L;
	for (const Sample& sample : m_samples)
	{
		if (newestEndQpc - sample.endQpc > durationQpc)
			continue;
		elapsedQpc += sample.elapsedQpc;
		intervals += sample.compensatedIntervals;
		++result.rawIntervals;
	}
	if (elapsedQpc > 0.0L)
	{
		result.rateHz = static_cast<double>(intervals *
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
