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

// The evidence state answers whether timing can be used. This records why it
// cannot, so a supported swapchain that is still warming is never conflated
// with a timing API that is unavailable for the active presentation model.
enum class AlphaPresentationTimingStatus
{
	NoSwapchain,
	Available,
	Disjoint,
	FrameStatisticsUnavailable,
	FrameStatisticsFailed
};

enum class AlphaSourceReleaseReason
{
	Unknown,
	Submitted,
	RenderFailed,
	QueuePressure,
	GenerationReset
};

struct AlphaPreRenderTimings
{
	double previewWaitMs = 0.0;
	double dequeueWaitMs = 0.0;
	double lookaheadMs = 0.0;
	double renderLockWaitMs = 0.0;
	double activeMs = 0.0;
};

struct AlphaPresentationRecord
{
	uint64_t generation = 0;
	uint64_t sourceSequence = 0;
	uint64_t sourceFrameNumber = 0;
	int64_t captureTimestamp = 0;
	int64_t callbackQpc = 0;
	int64_t enqueueQpc = 0;
	int64_t dequeueQpc = 0;
	int64_t submitQpc = 0;
	uint32_t presentId = 0;
	uint32_t presentedRefresh = 0;
	int64_t presentedQpc = 0;
	size_t queueDepthAfterDequeue = 0;
	double oldestQueuedAgeMs = 0.0;
	double renderMs = 0.0;
	double conversionCpuMs = 0.0;
	double sourceUploadCpuMs = 0.0;
	bool nativeRgbUpload = false;
	double swapBlockMs = 0.0;
	AlphaPreRenderTimings preRender;
	bool cadenceRepeat = false;
	bool frameStatsAvailable = false;
	bool frameStatsDisjoint = false;
	int32_t frameStatsResult = 0;
	uint32_t observedPresentCount = 0;
	uint32_t observedPresentRefreshCount = 0;
	uint32_t observedSyncRefreshCount = 0;
	int64_t observedSyncQpc = 0;
	AlphaSourceReleaseReason releaseReason = AlphaSourceReleaseReason::Unknown;
	bool presented = false;
};

struct AlphaDxgiPresentationSample
{
	uint64_t generation = 0;
	AlphaPresentationTimingStatus timingStatus =
		AlphaPresentationTimingStatus::NoSwapchain;
	int32_t frameStatisticsResult = 0;
	bool available = false;
	bool disjoint = false;
	uint32_t presentCount = 0;
	uint32_t presentRefreshCount = 0;
	uint32_t syncRefreshCount = 0;
	int64_t syncQpc = 0;
	int64_t qpcFrequency = 0;
	// Monitor-qualified rate supplied by the host. A nonzero value constrains
	// frame-statistics cadence so counters from another output fail closed.
	double expectedDisplayHz = 0.0;
};

struct AlphaPresentationSnapshot
{
	AlphaPresentationEvidence evidence = AlphaPresentationEvidence::Unavailable;
	AlphaPresentationTimingStatus timingStatus =
		AlphaPresentationTimingStatus::NoSwapchain;
	int32_t frameStatisticsResult = 0;
	uint64_t generation = 0;
	size_t retainedRecords = 0;
	uint64_t lastSubmittedSequence = 0;
	uint64_t lastPresentedSequence = 0;
	uint64_t sourceToPresentDebt = 0;
	uint32_t lastPresentId = 0;
	uint32_t lastPresentRefresh = 0;
	// Current counter average for OSD only; does not grant stable evidence.
	double observedDisplayHz = 0.0;
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
	const std::deque<AlphaPresentationRecord>& RecentRecords() const
	{
		return m_records;
	}
	const std::deque<AlphaPresentationRecord>& RecordsForTesting() const
	{
		return RecentRecords();
	}

private:
	void ResetCadence(AlphaPresentationEvidence evidence);

	size_t m_capacity;
	uint64_t m_generation = 0;
	std::deque<AlphaPresentationRecord> m_records;
	AlphaPresentationEvidence m_evidence =
		AlphaPresentationEvidence::Unavailable;
	AlphaPresentationTimingStatus m_timingStatus =
		AlphaPresentationTimingStatus::NoSwapchain;
	int32_t m_frameStatisticsResult = 0;
	uint64_t m_lastSubmittedSequence = 0;
	uint64_t m_lastPresentedSequence = 0;
	uint32_t m_lastPresentId = 0;
	uint32_t m_lastPresentRefresh = 0;
	AlphaDxgiPresentationSample m_lastObservedSample;
	bool m_hasLastObservation = false;
	uint32_t m_duplicateObservations = 0;
	uint32_t m_cadenceSamples = 0;
	uint32_t m_firstSyncRefresh = 0;
	uint32_t m_lastSyncRefresh = 0;
	int64_t m_firstSyncQpc = 0;
	int64_t m_lastSyncQpc = 0;
	int64_t m_qpcFrequency = 0;
	double m_measuredDisplayHz = 0.0;
};
