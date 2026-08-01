#pragma once

#include <cstdint>


enum class LiveFrameCounterTransition
{
	First,
	Consecutive,
	ForwardGap,
	CounterReset
};

struct LiveFrameCounterDecision
{
	LiveFrameCounterTransition transition = LiveFrameCounterTransition::First;
	uint64_t previous = 0;
	uint64_t current = 0;
	uint64_t missingFrames = 0;

	bool IsDiscontinuity() const noexcept
	{
		return transition == LiveFrameCounterTransition::ForwardGap ||
			transition == LiveFrameCounterTransition::CounterReset;
	}
};

// Tracks only source-counter continuity. A resumed live frame proves that
// capture is flowing; a gap is therefore a local source discontinuity, not
// evidence that the DirectShow/madVR graph is dead.
class LiveFrameCounterTracker
{
public:
	LiveFrameCounterDecision Observe(uint64_t counter) noexcept
	{
		LiveFrameCounterDecision decision;
		decision.previous = m_last;
		decision.current = counter;

		if (!m_hasLast)
		{
			decision.transition = LiveFrameCounterTransition::First;
			m_hasLast = true;
		}
		else if (counter == m_last + 1)
		{
			decision.transition = LiveFrameCounterTransition::Consecutive;
		}
		else if (counter > m_last)
		{
			decision.transition = LiveFrameCounterTransition::ForwardGap;
			decision.missingFrames = counter - m_last - 1;
		}
		else
		{
			decision.transition = LiveFrameCounterTransition::CounterReset;
		}

		m_last = counter;
		return decision;
	}

	void Reset() noexcept
	{
		m_hasLast = false;
		m_last = 0;
	}

private:
	bool m_hasLast = false;
	uint64_t m_last = 0;
};


inline const char* ToString(LiveFrameCounterTransition transition) noexcept
{
	switch (transition)
	{
	case LiveFrameCounterTransition::First: return "first";
	case LiveFrameCounterTransition::Consecutive: return "consecutive";
	case LiveFrameCounterTransition::ForwardGap: return "forward-gap";
	case LiveFrameCounterTransition::CounterReset: return "counter-reset";
	default: return "unknown";
	}
}
