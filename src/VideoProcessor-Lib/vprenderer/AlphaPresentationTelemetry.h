#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

enum class AlphaPresentationEvidence
{
	Unavailable,
	Warming,
	Stable,
	Disjoint
};

enum class AlphaSourceReleaseReason
{
	Unknown,
	Submitted,
	RenderFailed,
	QueuePressure,
	GenerationReset
};

struct AlphaPresentationRecord
{
	uint64_t generation = 0;
	uint64_t sourceSequence = 0;
	int64_t captureTimestamp = 0;
	int64_t enqueueQpc = 0;
	int64_t dequeueQpc = 0;
	int64_t submitQpc = 0;
	uint32_t presentId = 0;
	uint32_t presentedRefresh = 0;
	int64_t presentedQpc = 0;
	size_t queueDepthAfterDequeue = 0;
	double oldestQueuedAgeMs = 0.0;
	double renderMs = 0.0;
	double swapBlockMs = 0.0;
	AlphaSourceReleaseReason releaseReason = AlphaSourceReleaseReason::Unknown;
	bool presented = false;
};

struct AlphaDxgiPresentationSample
{
	uint64_t generation = 0;
	bool available = false;
	bool disjoint = false;
	uint32_t presentCount = 0;
	uint32_t presentRefreshCount = 0;
	uint32_t syncRefreshCount = 0;
	int64_t syncQpc = 0;
	int64_t qpcFrequency = 0;
};

struct AlphaPresentationSnapshot
{
	AlphaPresentationEvidence evidence = AlphaPresentationEvidence::Unavailable;
	uint64_t generation = 0;
	size_t retainedRecords = 0;
	uint64_t lastSubmittedSequence = 0;
	uint64_t lastPresentedSequence = 0;
	uint64_t sourceToPresentDebt = 0;
	uint32_t lastPresentId = 0;
	uint32_t lastPresentRefresh = 0;
	double measuredDisplayHz = 0.0;
	uint32_t cadenceSamples = 0;
};

class AlphaPresentationTelemetry
{
public:
	explicit AlphaPresentationTelemetry(size_t capacity = 256);

	void Reset(uint64_t generation);
	void RecordSubmission(const AlphaPresentationRecord& record);
	void Observe(const AlphaDxgiPresentationSample& sample);
	AlphaPresentationSnapshot Snapshot() const;
	const std::deque<AlphaPresentationRecord>& RecordsForTesting() const
	{
		return m_records;
	}

private:
	void ResetCadence(AlphaPresentationEvidence evidence);

	size_t m_capacity;
	uint64_t m_generation = 0;
	std::deque<AlphaPresentationRecord> m_records;
	AlphaPresentationEvidence m_evidence =
		AlphaPresentationEvidence::Unavailable;
	uint64_t m_lastSubmittedSequence = 0;
	uint64_t m_lastPresentedSequence = 0;
	uint32_t m_lastPresentId = 0;
	uint32_t m_lastPresentRefresh = 0;
	uint32_t m_cadenceSamples = 0;
	uint32_t m_firstSyncRefresh = 0;
	uint32_t m_lastSyncRefresh = 0;
	int64_t m_firstSyncQpc = 0;
	int64_t m_lastSyncQpc = 0;
	int64_t m_qpcFrequency = 0;
	double m_measuredDisplayHz = 0.0;
};
