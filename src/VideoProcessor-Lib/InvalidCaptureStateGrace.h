#pragma once

#include <cstdint>

// Only a valid capture-state notification proves recovery. Frame callbacks
// can continue while DeckLink reports no input or incomplete color metadata.
class InvalidCaptureStateGrace
{
public:
	static constexpr uint64_t DurationMs = 1500;

	void ObserveInvalid(uint64_t nowMs)
	{
		if (!m_pending)
		{
			m_pending = true;
			m_startedMs = nowMs;
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

private:
	bool m_pending = false;
	uint64_t m_startedMs = 0;
};
