#pragma once

#include <cstddef>
#include <cstdint>

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
	size_t rawQueueDepth = 0;
	size_t convertedQueueDepth = 0;
	size_t queueCapacity = 0;
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
