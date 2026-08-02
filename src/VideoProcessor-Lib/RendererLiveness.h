#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

// Lock-free progress evidence published by renderer hot paths. Tick values use
// GetTickCount64() and are zero until that stage has made progress.
struct RendererLivenessSnapshot
{
	bool supported = false;
	bool active = false;
	bool buffering = false;
	bool deliveryInProgress = false;
	bool resetInProgress = false;
	uint32_t captureThreadId = 0;
	uint32_t conversionThreadId = 0;
	uint32_t deliveryThreadId = 0;
	uint64_t queueEpoch = 0;
	uint64_t inputCount = 0;
	uint64_t conversionCount = 0;
	uint64_t dequeueCount = 0;
	uint64_t deliveryAttemptCount = 0;
	uint64_t deliverySuccessCount = 0;
	uint64_t currentEpochDeliverySuccessCount = 0;
	uint64_t lastDeliverySuccessQueueEpoch = 0;
	uint64_t lastInputTick = 0;
	uint64_t lastConversionTick = 0;
	uint64_t lastDequeueTick = 0;
	uint64_t lastDeliveryStartTick = 0;
	uint64_t lastDeliverySuccessTick = 0;
	uint64_t maximumSuccessfulDeliveryDurationUs = 0;
	size_t rawQueueDepth = 0;
	size_t convertedQueueDepth = 0;
	size_t queueCapacity = 0;
	// VP-owned proof that this exact queue epoch recovered from downstream
	// backpressure and applied its one-shot convergence. This is not a renderer
	// queue-occupancy measurement.
	uint64_t convergenceAppliedEpoch = 0;
	uint64_t convergenceAppliedTick = 0;
	uint64_t convergenceDeliverySuccessCount = 0;
	size_t convergenceTargetFrames = 0;
	bool convergenceHardBlockRecovered = false;
	// True only when this epoch reached its latched fresh-epoch prime target
	// before convergence. The target is bounded by VP capacity, negotiated
	// allocator headroom, and (when available) active madVR configuration.
	bool convergenceConvertedQueueWasFull = false;
	uint64_t primePrefillReachedEpoch = 0;
	size_t primeTargetFrames = 0;
	size_t primeRawTargetFrames = 0;
	// DeckLink-backed ownership retained by VP's raw transport plus the one
	// possible conversion-in-flight frame.
	size_t retainedSourceBufferCount = 0;
	size_t retainedSourceBufferHighWater = 0;
	uint64_t oldestRetainedSourceBufferAgeMs = 0;
	// Zero means normal legacy drain policy. A nonzero value is a VP-owned
	// converted-frame reserve; it never describes renderer-internal queues.
	size_t deliveryReserveFrames = 0;
};

// Latency boundaries owned by VP. "Scheduled" ends at the DirectShow sample's
// requested presentation time; it is not a claim about madVR, scanout, or the
// physical display.
struct RendererLatencySnapshot
{
	bool supported = false;
	bool scheduledPresentationKnown = false;
	double vpInternalMs = 0.0;
	double dsScheduleLeadMs = 0.0;
	double scheduledLatencyMs = 0.0;
};

// CBaseFilter::StreamTime can retain the custom reference clock's absolute
// domain on this live source. DirectShow sample timestamps are epoch-relative,
// so latency telemetry normalizes the observed clock to the first delivery of
// each queue epoch before comparing the two. If DirectShow restarts StreamTime
// at zero inside the same VP epoch, the diagnostic origin is rebased and its
// consumer must rewarm before publishing. This never changes delivery timing.
class RendererStreamTimeNormalizer
{
public:
	bool Normalize(uint64_t epoch, int64_t observedTime100ns,
		int64_t& streamTime100ns)
	{
		if (epoch == 0)
			return false;
		if (!m_initialized || epoch != m_epoch)
		{
			m_initialized = true;
			m_epoch = epoch;
			m_observedBase100ns = observedTime100ns;
			m_lastObserved100ns = observedTime100ns;
			m_lastObservationRebased = false;
			streamTime100ns = 0;
			return true;
		}
		m_lastObservationRebased = false;
		if (observedTime100ns < m_lastObserved100ns)
		{
			// The graph reference clock can be visible before Run() and then
			// restart its StreamTime domain near zero without changing VP's queue
			// epoch. The returned value is now already graph-relative; subtracting
			// it as another base would manufacture exactly that much extra PTS
			// lead. Switch to the direct graph domain and rewarm telemetry.
			m_observedBase100ns = 0;
			m_lastObserved100ns = observedTime100ns;
			m_lastObservationRebased = true;
			streamTime100ns = observedTime100ns;
			return true;
		}
		m_lastObserved100ns = observedTime100ns;
		streamTime100ns = observedTime100ns - m_observedBase100ns;
		return true;
	}

	bool LastObservationRebased() const
	{
		return m_lastObservationRebased;
	}

	int64_t LastValidObservedTime100ns() const
	{
		return m_lastObserved100ns;
	}

private:
	bool m_initialized = false;
	bool m_lastObservationRebased = false;
	uint64_t m_epoch = 0;
	int64_t m_observedBase100ns = 0;
	int64_t m_lastObserved100ns = 0;
};

// UI telemetry deliberately ignores the first second of a fresh graph epoch,
// then requires one second of clean evidence. This never gates video delivery;
// it only prevents preroll and graph-clock startup transients from being shown
// as a stable latency measurement.
class RendererLatencyStabilizer
{
public:
	static constexpr uint64_t IGNORE_MS = 1000;
	static constexpr uint64_t EVIDENCE_MS = 1000;
	static constexpr uint64_t MINIMUM_SAMPLES = 5;

	void Reset()
	{
		m_initialized = false;
		m_ready = false;
		m_epoch = 0;
		m_firstTickMs = 0;
		m_lastTickMs = 0;
		m_internalSamples = 0;
		m_scheduledSamples = 0;
		m_internalSum = 0.0;
		m_leadSum = 0.0;
		m_scheduledSum = 0.0;
		m_stable = {};
	}

	bool Observe(uint64_t epoch, uint64_t tickMs,
		const RendererLatencySnapshot& observed,
		RendererLatencySnapshot& stable)
	{
		if (!observed.supported)
			return false;
		if (!m_initialized || epoch != m_epoch || tickMs < m_firstTickMs)
			Reset(epoch, tickMs);

		const uint64_t elapsedMs = tickMs - m_firstTickMs;
		if (elapsedMs < IGNORE_MS)
			return false;

		if (!m_ready)
		{
			m_internalSum += observed.vpInternalMs;
			++m_internalSamples;
			if (observed.scheduledPresentationKnown)
			{
				m_leadSum += observed.dsScheduleLeadMs;
				m_scheduledSum += observed.scheduledLatencyMs;
				++m_scheduledSamples;
			}
			else
			{
				// Scheduled timing must be supported by one contiguous clean
				// clock-domain run. Do not publish samples collected before a
				// clock discontinuity as if they were still current.
				m_scheduledSamples = 0;
				m_leadSum = 0.0;
				m_scheduledSum = 0.0;
			}
			if (elapsedMs < IGNORE_MS + EVIDENCE_MS ||
				m_internalSamples < MINIMUM_SAMPLES)
				return false;

			m_stable.supported = true;
			m_stable.vpInternalMs =
				m_internalSum / static_cast<double>(m_internalSamples);
			m_stable.scheduledPresentationKnown =
				m_scheduledSamples >= MINIMUM_SAMPLES;
			if (m_stable.scheduledPresentationKnown)
			{
				m_stable.dsScheduleLeadMs =
					m_leadSum / static_cast<double>(m_scheduledSamples);
				m_stable.scheduledLatencyMs =
					m_scheduledSum / static_cast<double>(m_scheduledSamples);
			}
			m_ready = true;
			m_lastTickMs = tickMs;
			stable = m_stable;
			return true;
		}

		const uint64_t deltaMs = tickMs - m_lastTickMs;
		m_lastTickMs = tickMs;
		const double alpha = deltaMs >= 1000 ? 1.0 :
			static_cast<double>(deltaMs) / 1000.0;
		m_stable.vpInternalMs += alpha *
			(observed.vpInternalMs - m_stable.vpInternalMs);
		if (observed.scheduledPresentationKnown)
		{
			if (!m_stable.scheduledPresentationKnown)
			{
				m_stable.dsScheduleLeadMs = observed.dsScheduleLeadMs;
				m_stable.scheduledLatencyMs = observed.scheduledLatencyMs;
			}
			else
			{
				m_stable.dsScheduleLeadMs += alpha *
					(observed.dsScheduleLeadMs - m_stable.dsScheduleLeadMs);
				m_stable.scheduledLatencyMs += alpha *
					(observed.scheduledLatencyMs - m_stable.scheduledLatencyMs);
			}
			m_stable.scheduledPresentationKnown = true;
		}
		else
		{
			m_stable.scheduledPresentationKnown = false;
		}
		stable = m_stable;
		return true;
	}

private:
	void Reset(uint64_t epoch, uint64_t tickMs)
	{
		m_initialized = true;
		m_ready = false;
		m_epoch = epoch;
		m_firstTickMs = tickMs;
		m_lastTickMs = tickMs;
		m_internalSamples = 0;
		m_scheduledSamples = 0;
		m_internalSum = 0.0;
		m_leadSum = 0.0;
		m_scheduledSum = 0.0;
		m_stable = {};
	}

	bool m_initialized = false;
	bool m_ready = false;
	uint64_t m_epoch = 0;
	uint64_t m_firstTickMs = 0;
	uint64_t m_lastTickMs = 0;
	uint64_t m_internalSamples = 0;
	uint64_t m_scheduledSamples = 0;
	double m_internalSum = 0.0;
	double m_leadSum = 0.0;
	double m_scheduledSum = 0.0;
	RendererLatencySnapshot m_stable;
};

inline bool CalculateVpInternalLatency(
	uint64_t vpArrivalTickMs,
	uint64_t observationTickMs,
	RendererLatencySnapshot& snapshot)
{
	if (vpArrivalTickMs == 0 || observationTickMs < vpArrivalTickMs)
		return false;

	snapshot.supported = true;
	snapshot.vpInternalMs = static_cast<double>(
		observationTickMs - vpArrivalTickMs);
	return true;
}

inline bool CalculateScheduledLatency(
	uint64_t vpArrivalTickMs,
	uint64_t observationTickMs,
	int64_t presentationStart100ns,
	int64_t streamTime100ns,
	RendererLatencySnapshot& snapshot)
{
	if (!CalculateVpInternalLatency(
		vpArrivalTickMs, observationTickMs, snapshot))
		return false;

	snapshot.scheduledPresentationKnown = true;
	snapshot.dsScheduleLeadMs = static_cast<double>(
		presentationStart100ns - streamTime100ns) / 10000.0;
	snapshot.scheduledLatencyMs =
		snapshot.vpInternalMs + snapshot.dsScheduleLeadMs;
	return true;
}

constexpr uint64_t MINIMUM_CURRENT_EPOCH_DELIVERIES = 5;

inline bool HasSufficientDownstreamPreroll(uint64_t deliveryCount)
{
	return deliveryCount >= MINIMUM_CURRENT_EPOCH_DELIVERIES;
}

inline bool HasCurrentEpochDownstreamDelivery(
	const RendererLivenessSnapshot& snapshot)
{
	return snapshot.supported &&
		snapshot.active &&
		!snapshot.buffering &&
		!snapshot.resetInProgress &&
		HasSufficientDownstreamPreroll(
			snapshot.currentEpochDeliverySuccessCount) &&
		snapshot.lastDeliverySuccessTick > 0 &&
		snapshot.lastDeliverySuccessQueueEpoch == snapshot.queueEpoch;
}

inline uint64_t RendererTickAge(uint64_t nowTick, uint64_t eventTick)
{
	return eventTick == 0 || eventTick > nowTick ?
		(std::numeric_limits<uint64_t>::max)() : nowTick - eventTick;
}

inline bool HasRecentCurrentEpochDelivery(
	const RendererLivenessSnapshot& snapshot,
	uint64_t nowTick,
	uint64_t maximumAgeMs)
{
	return HasCurrentEpochDownstreamDelivery(snapshot) &&
		RendererTickAge(nowTick, snapshot.lastDeliverySuccessTick) <=
		maximumAgeMs;
}

inline bool IsSustainedDirectShowDeliveryStall(
	const RendererLivenessSnapshot& snapshot,
	uint64_t nowTick,
	bool atCapacity,
	uint64_t stallThresholdMs)
{
	if (!snapshot.supported || !snapshot.active || snapshot.buffering ||
		snapshot.resetInProgress || !atCapacity || stallThresholdMs == 0)
		return false;
	const bool inputStillAdvancing = snapshot.lastInputTick != 0 &&
		RendererTickAge(nowTick, snapshot.lastInputTick) <= 2000;
	const bool blockedDelivery = snapshot.deliveryInProgress &&
		snapshot.lastDeliveryStartTick != 0 &&
		RendererTickAge(nowTick, snapshot.lastDeliveryStartTick) >=
		stallThresholdMs;
	const bool noDeliveryProgress = snapshot.lastDeliverySuccessTick != 0 &&
		RendererTickAge(nowTick, snapshot.lastDeliverySuccessTick) >=
		stallThresholdMs;
	return inputStillAdvancing && (blockedDelivery || noDeliveryProgress);
}

// A completed HWND retarget is a bounded transition, not steady-state queue
// pressure. It must prove that downstream Receive continues to complete even
// after capture ingress has stopped naturally behind full VP queues.
inline bool IsPostRetargetReceiveStall(
	const RendererLivenessSnapshot& snapshot,
	uint64_t nowTick,
	uint64_t retargetCompletedTick,
	uint64_t healthDeadlineMs = 2000,
	uint64_t blockedDeliveryMs = 1000,
	uint64_t recentDeliveryMs = 1000)
{
	if (!snapshot.supported || !snapshot.active || snapshot.buffering ||
		snapshot.resetInProgress || !snapshot.deliveryInProgress ||
		retargetCompletedTick == 0 ||
		RendererTickAge(nowTick, retargetCompletedTick) < healthDeadlineMs ||
		snapshot.lastDeliveryStartTick == 0 ||
		RendererTickAge(nowTick, snapshot.lastDeliveryStartTick) <
			blockedDeliveryMs)
	{
		return false;
	}
	return !HasRecentCurrentEpochDelivery(
		snapshot, nowTick, recentDeliveryMs);
}
