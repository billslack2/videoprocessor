#pragma once

#include <cstdint>

// Only a valid capture-state notification proves recovery. Frame callbacks
// can continue while DeckLink reports no input or incomplete color metadata.
class InvalidCaptureStateGrace
{
public:
	static constexpr uint64_t DurationMs = 1500;

	explicit InvalidCaptureStateGrace(bool boundedRecovery = true) :
		m_boundedRecovery(boundedRecovery) {}

	void Configure(bool boundedRecovery)
	{
		Reset();
		m_boundedRecovery = boundedRecovery;
	}

	void ObserveInvalid(uint64_t nowMs, uint64_t capturedFrames = 0)
	{
		if (!m_pending || !m_boundedRecovery)
		{
			m_pending = true;
			m_startedMs = nowMs;
			m_capturedFrames = capturedFrames;
		}
	}

	void Reset() { m_pending = false; }
	bool Pending() const { return m_pending; }

	uint64_t RemainingMs(uint64_t nowMs) const
	{
		if (!m_pending)
			return 0;
		const uint64_t elapsedMs = nowMs - m_startedMs;
		return elapsedMs < DurationMs ? DurationMs - elapsedMs : 0;
	}

	// Preserve the original counter-based dismissal only in legacy mode.
	bool RetainExpiredState(uint64_t capturedFrames) const
	{
		return !m_boundedRecovery && capturedFrames > m_capturedFrames;
	}

private:
	bool m_boundedRecovery = true;
	uint64_t m_capturedFrames = 0;
	bool m_pending = false;
	uint64_t m_startedMs = 0;
};
