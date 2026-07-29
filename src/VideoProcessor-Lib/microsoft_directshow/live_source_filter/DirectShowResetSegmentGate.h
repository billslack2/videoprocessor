#pragma once

#include <atomic>
#include <cstdint>


// Tracks the one NewSegment that must precede the first sample of a reset
// epoch. It stores no samples and therefore cannot add queue depth or latency.
class DirectShowResetSegmentGate
{
public:
	void Arm(uint64_t epoch) noexcept
	{
		m_pendingEpoch.store(epoch, std::memory_order_release);
	}

	bool IsArmedFor(uint64_t epoch) const noexcept
	{
		return epoch != 0 &&
			m_pendingEpoch.load(std::memory_order_acquire) == epoch;
	}

	bool Complete(uint64_t epoch) noexcept
	{
		if (epoch == 0)
			return false;
		return m_pendingEpoch.compare_exchange_strong(
			epoch, 0,
			std::memory_order_acq_rel,
			std::memory_order_acquire);
	}

	void Clear() noexcept
	{
		m_pendingEpoch.store(0, std::memory_order_release);
	}

	uint64_t PendingEpoch() const noexcept
	{
		return m_pendingEpoch.load(std::memory_order_acquire);
	}

private:
	std::atomic<uint64_t> m_pendingEpoch{0};
};
