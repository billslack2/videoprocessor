#include <pch.h>

#include <DisplayRefreshRateWindow.h>

#include <algorithm>

DisplayRefreshRateWindow::DisplayRefreshRateWindow(
	int64_t qpcFrequency, int64_t retentionQpc)
	: m_qpcFrequency(qpcFrequency)
	, m_retentionQpc(retentionQpc)
{
}

void DisplayRefreshRateWindow::Add(int64_t endQpc, int64_t elapsedQpc,
	uint64_t compensatedIntervals)
{
	if (endQpc <= 0 || elapsedQpc <= 0 || compensatedIntervals == 0)
		return;

	m_samples.push_back({ endQpc, elapsedQpc, compensatedIntervals });
	Trim(endQpc);
}

void DisplayRefreshRateWindow::Reset()
{
	m_samples.clear();
}

DisplayRefreshRateWindowSnapshot DisplayRefreshRateWindow::Snapshot() const
{
	DisplayRefreshRateWindowSnapshot result;
	if (m_qpcFrequency <= 0 || m_samples.empty())
		return result;

	int64_t elapsedQpc = 0;
	for (const Sample& sample : m_samples)
	{
		elapsedQpc += sample.elapsedQpc;
		result.compensatedIntervals += sample.compensatedIntervals;
		++result.rawWaitIntervals;
		if (result.minimumWaitIntervalQpc == 0 ||
			sample.elapsedQpc < result.minimumWaitIntervalQpc)
		{
			result.minimumWaitIntervalQpc = sample.elapsedQpc;
		}
		result.maximumWaitIntervalQpc = std::max(
			result.maximumWaitIntervalQpc, sample.elapsedQpc);
	}

	if (elapsedQpc <= 0)
		return result;

	const double elapsedSeconds = static_cast<double>(elapsedQpc) /
		static_cast<double>(m_qpcFrequency);
	result.refreshRateHz = static_cast<double>(result.compensatedIntervals) /
		elapsedSeconds;
	result.rawWaitRateHz = static_cast<double>(result.rawWaitIntervals) /
		elapsedSeconds;
	return result;
}

void DisplayRefreshRateWindow::Trim(int64_t newestEndQpc)
{
	if (m_retentionQpc <= 0)
	{
		m_samples.clear();
		return;
	}

	while (!m_samples.empty() &&
		newestEndQpc - m_samples.front().endQpc > m_retentionQpc)
	{
		m_samples.pop_front();
	}
}
