#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>

// A bounded trace of VP-owned live-output pipeline events.  It deliberately
// records only facts VP can observe; madVR queue occupancy is not observable.
enum class LiveOutputTraceKind : uint8_t
{
	CaptureAccepted,
	ConversionCompleted,
	DeliveryAttempted,
	DeliveryCompleted,
	ResetStarted,
	ResetCompleted,
	PlannedDrop,
	QueueSnapshot,
};

struct LiveOutputTraceRecord
{
	uint64_t sequence = 0;
	uint64_t frameNumber = 0;
	uint64_t pipelineEpoch = 0;
	uint64_t captureTimestamp = 0;
	uint64_t eventTick = 0;
	int64_t presentationStart = 0;
	int64_t presentationStop = 0;
	uint32_t rawQueueDepth = 0;
	uint32_t convertedQueueDepth = 0;
	uint32_t totalQueueDepth = 0;
	uint32_t queueCapacity = 0;
	uint32_t processingDurationUs = 0;
	uint32_t deliveryDurationUs = 0;
	int32_t deliveryResult = 0;
	LiveOutputTraceKind kind = LiveOutputTraceKind::CaptureAccepted;
	bool sceneBoundary = false;
	bool intentionalDrop = false;
};

struct LiveOutputTraceComparison
{
	bool equivalent = true;
	size_t firstMismatch = 0;
	size_t expectedCount = 0;
	size_t actualCount = 0;
};

class LiveOutputTrace
{
public:
	static constexpr size_t CAPACITY = 4096;

	void Clear() noexcept
	{
		// Clear is called only between graph epochs, after every producer has
		// stopped.  Do not make it part of the capture/delivery hot path.
		m_nextSequence.store(0, std::memory_order_release);
		m_droppedRecords.store(0, std::memory_order_release);
	}

	void Record(LiveOutputTraceRecord record) noexcept
	{
		// Capture, conversion, delivery, and reset are independent producers.
		// Tracing must never make any of them wait, so an overlapping write is
		// counted and omitted rather than taking a pipeline lock or spinning.
		if (m_writeInProgress.test_and_set(std::memory_order_acquire))
		{
			m_droppedRecords.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		const uint64_t sequence =
			m_nextSequence.fetch_add(1, std::memory_order_relaxed) + 1;
		record.sequence = sequence;
		m_records[(sequence - 1) % CAPACITY] = record;
		m_writeInProgress.clear(std::memory_order_release);
	}

	uint64_t DroppedRecordCount() const noexcept
	{
		return m_droppedRecords.load(std::memory_order_acquire);
	}

	std::vector<LiveOutputTraceRecord> Snapshot() const
	{
		// Snapshots are taken during graph teardown. A concurrent producer is
		// treated exactly like any other trace collision: do not wait or read a
		// partially-written record.
		if (m_writeInProgress.test_and_set(std::memory_order_acquire))
			return {};

		try
		{
			const uint64_t lastSequence =
				m_nextSequence.load(std::memory_order_acquire);
			const uint64_t firstSequence = lastSequence > CAPACITY ?
				lastSequence - CAPACITY + 1 : 1;

			std::vector<LiveOutputTraceRecord> result;
			result.reserve(static_cast<size_t>(lastSequence - firstSequence + 1));
			for (uint64_t sequence = firstSequence; sequence <= lastSequence; ++sequence)
			{
				const LiveOutputTraceRecord record =
					m_records[(sequence - 1) % CAPACITY];
				if (record.sequence == sequence)
					result.push_back(record);
			}
			m_writeInProgress.clear(std::memory_order_release);
			return result;
		}
		catch (...)
		{
			m_writeInProgress.clear(std::memory_order_release);
			throw;
		}
	}

	static LiveOutputTraceComparison Compare(
		const std::vector<LiveOutputTraceRecord>& expected,
		const std::vector<LiveOutputTraceRecord>& actual,
		int64_t presentationTolerance = 0)
	{
		LiveOutputTraceComparison comparison;
		comparison.expectedCount = expected.size();
		comparison.actualCount = actual.size();
		if (expected.size() != actual.size())
		{
			comparison.equivalent = false;
			comparison.firstMismatch =
				expected.size() < actual.size() ? expected.size() : actual.size();
			return comparison;
		}

		for (size_t index = 0; index < expected.size(); ++index)
		{
			const LiveOutputTraceRecord& lhs = expected[index];
			const LiveOutputTraceRecord& rhs = actual[index];
			if (lhs.kind != rhs.kind ||
				lhs.frameNumber != rhs.frameNumber ||
				lhs.pipelineEpoch != rhs.pipelineEpoch ||
				lhs.captureTimestamp != rhs.captureTimestamp ||
				lhs.rawQueueDepth != rhs.rawQueueDepth ||
				lhs.convertedQueueDepth != rhs.convertedQueueDepth ||
				lhs.totalQueueDepth != rhs.totalQueueDepth ||
				lhs.queueCapacity != rhs.queueCapacity ||
				lhs.processingDurationUs != rhs.processingDurationUs ||
				lhs.deliveryDurationUs != rhs.deliveryDurationUs ||
				lhs.deliveryResult != rhs.deliveryResult ||
				lhs.sceneBoundary != rhs.sceneBoundary ||
				lhs.intentionalDrop != rhs.intentionalDrop ||
				AbsoluteDifference(lhs.presentationStart, rhs.presentationStart) > presentationTolerance ||
				AbsoluteDifference(lhs.presentationStop, rhs.presentationStop) > presentationTolerance)
			{
				comparison.equivalent = false;
				comparison.firstMismatch = index;
				return comparison;
			}
		}
		return comparison;
	}

	static void WriteCsv(
		std::ostream& stream,
		const std::vector<LiveOutputTraceRecord>& records)
	{
		stream << "sequence,kind,frame,epoch,capture_timestamp,event_tick,"
			"presentation_start,presentation_stop,raw_queue,converted_queue,total_queue,queue_capacity,"
			"processing_us,delivery_us,delivery_result,scene_boundary,intentional_drop\n";
		for (const LiveOutputTraceRecord& record : records)
		{
			stream << record.sequence << ',' << static_cast<unsigned>(record.kind) << ','
				<< record.frameNumber << ',' << record.pipelineEpoch << ','
				<< record.captureTimestamp << ',' << record.eventTick << ','
				<< record.presentationStart << ',' << record.presentationStop << ','
				<< record.rawQueueDepth << ',' << record.convertedQueueDepth << ','
				<< record.totalQueueDepth << ',' << record.queueCapacity << ','
				<< record.processingDurationUs << ',' << record.deliveryDurationUs << ','
				<< record.deliveryResult << ',' << (record.sceneBoundary ? 1 : 0) << ','
				<< (record.intentionalDrop ? 1 : 0) << '\n';
		}
	}

private:
	static int64_t AbsoluteDifference(int64_t left, int64_t right) noexcept
	{
		if (left >= right)
			return left - right;
		return right - left;
	}

	std::array<LiveOutputTraceRecord, CAPACITY> m_records{};
	std::atomic<uint64_t> m_nextSequence{ 0 };
	std::atomic<uint64_t> m_droppedRecords{ 0 };
	mutable std::atomic_flag m_writeInProgress = ATOMIC_FLAG_INIT;
};
