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
	ConvergenceState,
};

struct LiveOutputTraceRecord
{
	uint64_t sequence = 0;
	uint64_t frameNumber = 0;
	uint64_t pipelineEpoch = 0;
	uint64_t captureTimestamp = 0;
	// Same GetTickCount64 domain as eventTick. This is VP's measurable
	// capture-accepted-to-delivery interval; captureTimestamp remains the
	// source timing-clock value used for timing diagnostics.
	uint64_t captureArrivalTick = 0;
	uint64_t eventTick = 0;
	int64_t presentationStart = 0;
	int64_t presentationStop = 0;
	int64_t streamTime = 0;
	int64_t observedClockTime = 0;
	int64_t vpInternalUs = 0;
	int64_t dsScheduleLeadUs = 0;
	int64_t scheduledLatencyUs = 0;
	int64_t displayedVpInternalUs = 0;
	int64_t displayedDsScheduleLeadUs = 0;
	int64_t displayedScheduledLatencyUs = 0;
	int64_t mediaStart = 0;
	int64_t mediaStop = 0;
	uint64_t outputSequence = 0;
	uint32_t rawQueueDepth = 0;
	uint32_t convertedQueueDepth = 0;
	uint32_t totalQueueDepth = 0;
	uint32_t queueCapacity = 0;
	uint32_t processingDurationUs = 0;
	uint32_t deliveryDurationUs = 0;
	int32_t deliveryResult = 0;
	uint32_t queueTarget = 0;
	uint32_t queueDepthBefore = 0;
	uint32_t queueDepthAfter = 0;
	uint32_t queueDiscarded = 0;
	uint32_t rawQueueDiscarded = 0;
	uint32_t convertedQueueDiscarded = 0;
	uint32_t convergenceSuccessCount = 0;
	uint32_t convergenceBlockCount = 0;
	uint32_t convergenceRecoveryStreak = 0;
	uint32_t convergencePacedStreak = 0;
	uint32_t convergenceBlockThresholdUs = 0;
	uint32_t convergenceNormalThresholdUs = 0;
	uint32_t convergencePacedMinimumUs = 0;
	uint32_t convergencePacedMaximumUs = 0;
	uint32_t convergencePacedPrimingDepth = 0;
	uint32_t convergenceElapsedMs = 0;
	uint32_t sourceGapSlotsBefore = 0;
	uint32_t observedSourceGapSlotsBefore = 0;
	uint32_t intentionalSourceGapSlotsSuppressed = 0;
	uint8_t convergenceState = 0;
	uint8_t convergenceReason = 0;
	uint8_t convergenceActivation = 0;
	uint8_t timestampOwner = 0;
	uint8_t timestampMethod = 0;
	LiveOutputTraceKind kind = LiveOutputTraceKind::CaptureAccepted;
	bool sceneBoundary = false;
	bool intentionalDrop = false;
	bool sourceDiscontinuity = false;
	bool convergenceApplied = false;
	bool convergenceRawZero = false;
	bool convergenceRawBacklog = false;
	bool scheduledLatencyKnown = false;
	bool latencyDisplayReady = false;
	bool sourceGapSuppressed = false;
	bool materialSourceGapSuppressed = false;
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
				lhs.streamTime != rhs.streamTime ||
				lhs.observedClockTime != rhs.observedClockTime ||
				lhs.vpInternalUs != rhs.vpInternalUs ||
				lhs.dsScheduleLeadUs != rhs.dsScheduleLeadUs ||
				lhs.scheduledLatencyUs != rhs.scheduledLatencyUs ||
				lhs.displayedVpInternalUs != rhs.displayedVpInternalUs ||
				lhs.displayedDsScheduleLeadUs != rhs.displayedDsScheduleLeadUs ||
				lhs.displayedScheduledLatencyUs != rhs.displayedScheduledLatencyUs ||
				lhs.rawQueueDepth != rhs.rawQueueDepth ||
				lhs.convertedQueueDepth != rhs.convertedQueueDepth ||
				lhs.totalQueueDepth != rhs.totalQueueDepth ||
				lhs.queueCapacity != rhs.queueCapacity ||
				lhs.processingDurationUs != rhs.processingDurationUs ||
				lhs.deliveryDurationUs != rhs.deliveryDurationUs ||
				lhs.deliveryResult != rhs.deliveryResult ||
				lhs.mediaStart != rhs.mediaStart ||
				lhs.mediaStop != rhs.mediaStop ||
				lhs.outputSequence != rhs.outputSequence ||
				lhs.queueTarget != rhs.queueTarget ||
				lhs.queueDepthBefore != rhs.queueDepthBefore ||
				lhs.queueDepthAfter != rhs.queueDepthAfter ||
				lhs.queueDiscarded != rhs.queueDiscarded ||
				lhs.rawQueueDiscarded != rhs.rawQueueDiscarded ||
				lhs.convertedQueueDiscarded != rhs.convertedQueueDiscarded ||
				lhs.convergenceSuccessCount != rhs.convergenceSuccessCount ||
				lhs.convergenceBlockCount != rhs.convergenceBlockCount ||
				lhs.convergenceRecoveryStreak != rhs.convergenceRecoveryStreak ||
				lhs.convergencePacedStreak != rhs.convergencePacedStreak ||
				lhs.convergenceBlockThresholdUs != rhs.convergenceBlockThresholdUs ||
				lhs.convergenceNormalThresholdUs != rhs.convergenceNormalThresholdUs ||
				lhs.convergencePacedMinimumUs != rhs.convergencePacedMinimumUs ||
				lhs.convergencePacedMaximumUs != rhs.convergencePacedMaximumUs ||
				lhs.convergencePacedPrimingDepth != rhs.convergencePacedPrimingDepth ||
				lhs.convergenceElapsedMs != rhs.convergenceElapsedMs ||
				lhs.sourceGapSlotsBefore != rhs.sourceGapSlotsBefore ||
				lhs.observedSourceGapSlotsBefore != rhs.observedSourceGapSlotsBefore ||
				lhs.intentionalSourceGapSlotsSuppressed !=
					rhs.intentionalSourceGapSlotsSuppressed ||
				lhs.convergenceState != rhs.convergenceState ||
				lhs.convergenceReason != rhs.convergenceReason ||
				lhs.convergenceActivation != rhs.convergenceActivation ||
				lhs.timestampOwner != rhs.timestampOwner ||
				lhs.timestampMethod != rhs.timestampMethod ||
				lhs.sceneBoundary != rhs.sceneBoundary ||
				lhs.intentionalDrop != rhs.intentionalDrop ||
				lhs.sourceDiscontinuity != rhs.sourceDiscontinuity ||
				lhs.convergenceApplied != rhs.convergenceApplied ||
				lhs.convergenceRawZero != rhs.convergenceRawZero ||
				lhs.convergenceRawBacklog != rhs.convergenceRawBacklog ||
				lhs.scheduledLatencyKnown != rhs.scheduledLatencyKnown ||
				lhs.latencyDisplayReady != rhs.latencyDisplayReady ||
				lhs.sourceGapSuppressed != rhs.sourceGapSuppressed ||
				lhs.materialSourceGapSuppressed != rhs.materialSourceGapSuppressed ||
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
		stream << "sequence,kind,frame,epoch,capture_timestamp,capture_arrival_tick,event_tick,"
			"presentation_start,presentation_stop,stream_time,observed_clock_time,"
			"vp_internal_us,ds_schedule_lead_us,"
			"vp_to_scheduled_us,displayed_vp_internal_us,displayed_ds_schedule_lead_us,"
			"displayed_vp_to_scheduled_us,media_start,media_stop,output_sequence,"
			"raw_queue,converted_queue,total_queue,queue_capacity,processing_us,delivery_us,"
			"delivery_result,queue_target,queue_depth_before,queue_depth_after,queue_discarded,"
			"raw_queue_discarded,converted_queue_discarded,"
			"convergence_successes,convergence_blocks,convergence_recovery_streak,"
			"convergence_paced_streak,convergence_block_threshold_us,convergence_normal_threshold_us,"
			"convergence_paced_minimum_us,convergence_paced_maximum_us,"
			"convergence_paced_priming_depth,convergence_elapsed_ms,"
			"source_gap_slots_before,observed_source_gap_slots_before,"
			"intentional_source_gap_slots_suppressed,"
			"convergence_state,convergence_reason,convergence_activation,"
			"timestamp_owner,timestamp_method,scene_boundary,intentional_drop,"
			"source_discontinuity,convergence_applied,convergence_raw_zero,convergence_raw_backlog,"
			"scheduled_latency_known,latency_display_ready,source_gap_suppressed,"
			"material_source_gap_suppressed\n";
		for (const LiveOutputTraceRecord& record : records)
		{
			stream << record.sequence << ',' << static_cast<unsigned>(record.kind) << ','
				<< record.frameNumber << ',' << record.pipelineEpoch << ','
				<< record.captureTimestamp << ',' << record.captureArrivalTick << ','
				<< record.eventTick << ','
				<< record.presentationStart << ',' << record.presentationStop << ','
				<< record.streamTime << ',' << record.observedClockTime << ','
				<< record.vpInternalUs << ','
				<< record.dsScheduleLeadUs << ',' << record.scheduledLatencyUs << ','
				<< record.displayedVpInternalUs << ','
				<< record.displayedDsScheduleLeadUs << ','
				<< record.displayedScheduledLatencyUs << ','
				<< record.mediaStart << ',' << record.mediaStop << ','
				<< record.outputSequence << ','
				<< record.rawQueueDepth << ',' << record.convertedQueueDepth << ','
				<< record.totalQueueDepth << ',' << record.queueCapacity << ','
				<< record.processingDurationUs << ',' << record.deliveryDurationUs << ','
				<< record.deliveryResult << ',' << record.queueTarget << ','
				<< record.queueDepthBefore << ',' << record.queueDepthAfter << ','
				<< record.queueDiscarded << ',' << record.rawQueueDiscarded << ','
				<< record.convertedQueueDiscarded << ','
				<< record.convergenceSuccessCount << ','
				<< record.convergenceBlockCount << ',' << record.convergenceRecoveryStreak << ','
				<< record.convergencePacedStreak << ','
				<< record.convergenceBlockThresholdUs << ','
				<< record.convergenceNormalThresholdUs << ','
				<< record.convergencePacedMinimumUs << ','
				<< record.convergencePacedMaximumUs << ','
				<< record.convergencePacedPrimingDepth << ','
				<< record.convergenceElapsedMs << ','
				<< record.sourceGapSlotsBefore << ','
				<< record.observedSourceGapSlotsBefore << ','
				<< record.intentionalSourceGapSlotsSuppressed << ','
				<< static_cast<unsigned>(record.convergenceState) << ','
				<< static_cast<unsigned>(record.convergenceReason) << ','
				<< static_cast<unsigned>(record.convergenceActivation) << ','
				<< static_cast<unsigned>(record.timestampOwner) << ','
				<< static_cast<unsigned>(record.timestampMethod) << ','
				<< (record.sceneBoundary ? 1 : 0) << ','
				<< (record.intentionalDrop ? 1 : 0) << ','
				<< (record.sourceDiscontinuity ? 1 : 0) << ','
				<< (record.convergenceApplied ? 1 : 0) << ','
				<< (record.convergenceRawZero ? 1 : 0) << ','
				<< (record.convergenceRawBacklog ? 1 : 0) << ','
				<< (record.scheduledLatencyKnown ? 1 : 0) << ','
				<< (record.latencyDisplayReady ? 1 : 0) << ','
				<< (record.sourceGapSuppressed ? 1 : 0) << ','
				<< (record.materialSourceGapSuppressed ? 1 : 0) << '\n';
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
