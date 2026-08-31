#pragma once

#include <atomic>
#include <cstdint>

// Cross-thread hand-off for source-derived presentation state. Queue resets
// request invalidation on the graph/UI thread; the render thread consumes it
// before it can reuse crop, subtitle, or HDR-analysis authority.
class PresentationResetEpoch
{
public:
	void Request()
	{
		m_requested.fetch_add(1, std::memory_order_release);
	}

	bool Consume(uint64_t& consumedEpoch)
	{
		const uint64_t requested = m_requested.load(std::memory_order_acquire);
		if (requested == consumedEpoch)
			return false;
		consumedEpoch = requested;
		return true;
	}

private:
	std::atomic<uint64_t> m_requested{0};
};
