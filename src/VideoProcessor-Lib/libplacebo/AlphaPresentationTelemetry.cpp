#include <pch.h>

#include "AlphaPresentationTelemetry.h"

#include <algorithm>

namespace
{
	constexpr uint32_t MIN_STABLE_SAMPLES = 8;
	constexpr double MIN_STABLE_SECONDS = 0.25;
}

AlphaPresentationTelemetry::AlphaPresentationTelemetry(size_t capacity)
	: m_capacity(std::max<size_t>(1, capacity))
{
}

void AlphaPresentationTelemetry::Reset(uint64_t generation)
{
	m_generation = generation;
	m_records.clear();
	m_lastSubmittedSequence = 0;
	m_lastPresentedSequence = 0;
	m_lastPresentId = 0;
	m_lastPresentRefresh = 0;
	ResetCadence(AlphaPresentationEvidence::Unavailable);
}

void AlphaPresentationTelemetry::RecordSubmission(
	const AlphaPresentationRecord& record)
{
	if (record.generation != m_generation)
		Reset(record.generation);

	m_records.push_back(record);
	while (m_records.size() > m_capacity)
		m_records.pop_front();
	m_lastSubmittedSequence = record.sourceSequence;
	m_lastPresentId = record.presentId;
	if (m_evidence == AlphaPresentationEvidence::Unavailable)
		m_evidence = AlphaPresentationEvidence::Warming;
}

void AlphaPresentationTelemetry::Observe(
	const AlphaDxgiPresentationSample& sample)
{
	if (sample.generation != m_generation)
		Reset(sample.generation);
	if (sample.disjoint)
	{
		ResetCadence(AlphaPresentationEvidence::Disjoint);
		return;
	}
	if (!sample.available || sample.qpcFrequency <= 0 || sample.syncQpc <= 0)
	{
		ResetCadence(AlphaPresentationEvidence::Unavailable);
		return;
	}

	if (m_cadenceSamples != 0 &&
		(sample.syncRefreshCount < m_lastSyncRefresh ||
			sample.syncQpc <= m_lastSyncQpc))
	{
		ResetCadence(AlphaPresentationEvidence::Disjoint);
		return;
	}

	for (AlphaPresentationRecord& record : m_records)
	{
		if (!record.presented && record.presentId != 0 &&
			record.presentId <= sample.presentCount)
		{
			record.presented = true;
			record.presentedRefresh = sample.presentRefreshCount;
			record.presentedQpc = sample.syncQpc;
			m_lastPresentedSequence =
				std::max(m_lastPresentedSequence, record.sourceSequence);
			m_lastPresentRefresh = sample.presentRefreshCount;
		}
	}

	if (m_cadenceSamples == 0)
	{
		m_firstSyncRefresh = sample.syncRefreshCount;
		m_firstSyncQpc = sample.syncQpc;
		m_qpcFrequency = sample.qpcFrequency;
	}
	m_lastSyncRefresh = sample.syncRefreshCount;
	m_lastSyncQpc = sample.syncQpc;
	++m_cadenceSamples;
	m_evidence = AlphaPresentationEvidence::Warming;

	const uint32_t refreshDelta = m_lastSyncRefresh - m_firstSyncRefresh;
	const int64_t qpcDelta = m_lastSyncQpc - m_firstSyncQpc;
	const double elapsedSeconds =
		static_cast<double>(qpcDelta) / static_cast<double>(m_qpcFrequency);
	if (m_cadenceSamples >= MIN_STABLE_SAMPLES &&
		refreshDelta > 0 && elapsedSeconds >= MIN_STABLE_SECONDS)
	{
		const double measured =
			static_cast<double>(refreshDelta) / elapsedSeconds;
		if (measured >= 10.0 && measured <= 500.0)
		{
			m_measuredDisplayHz = measured;
			m_evidence = AlphaPresentationEvidence::Stable;
		}
	}
}

AlphaPresentationSnapshot AlphaPresentationTelemetry::Snapshot() const
{
	AlphaPresentationSnapshot snapshot;
	snapshot.evidence = m_evidence;
	snapshot.generation = m_generation;
	snapshot.retainedRecords = m_records.size();
	snapshot.lastSubmittedSequence = m_lastSubmittedSequence;
	snapshot.lastPresentedSequence = m_lastPresentedSequence;
	snapshot.sourceToPresentDebt =
		m_lastSubmittedSequence >= m_lastPresentedSequence
			? m_lastSubmittedSequence - m_lastPresentedSequence
			: 0;
	snapshot.lastPresentId = m_lastPresentId;
	snapshot.lastPresentRefresh = m_lastPresentRefresh;
	snapshot.measuredDisplayHz = m_measuredDisplayHz;
	snapshot.cadenceSamples = m_cadenceSamples;
	return snapshot;
}

void AlphaPresentationTelemetry::ResetCadence(
	AlphaPresentationEvidence evidence)
{
	m_evidence = evidence;
	m_cadenceSamples = 0;
	m_firstSyncRefresh = 0;
	m_lastSyncRefresh = 0;
	m_firstSyncQpc = 0;
	m_lastSyncQpc = 0;
	m_qpcFrequency = 0;
	m_measuredDisplayHz = 0.0;
}
