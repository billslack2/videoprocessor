#pragma once

#include <cstdint>
#include <limits>


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


enum class LiveSourceGapRecoveryAction
{
	None,
	LocalDiscontinuity,
	RequestGraphReprime,
	SuppressedUntilHealthy,
};

struct LiveSourceGapRecoveryDecision
{
	LiveSourceGapRecoveryAction action = LiveSourceGapRecoveryAction::None;
	uint64_t materialGapFrames = 0;
	uint64_t healthyIntervalsRequired = 0;
	uint64_t healthyIntervalsObserved = 0;
};

// A live-source gap can drain opaque downstream buffering even when the
// delivery timeline remains continuous. Re-prime only after 100ms of missing
// source time; isolated capture misses remain local discontinuities. After a
// graph reset, require one second of consecutive input with healthy downstream
// delivery before re-arming so either an unstable HDMI handshake or local
// backpressure cannot create a reset loop.
class LiveSourceGapRecoveryPolicy
{
public:
	LiveSourceGapRecoveryDecision Observe(
		const LiveFrameCounterDecision& counter,
		uint64_t timeScale,
		uint64_t frameDuration,
		bool downstreamHealthy = true) noexcept
	{
		LiveSourceGapRecoveryDecision decision;
		decision.materialGapFrames = FramesForDuration(
			timeScale, frameDuration, 100,
			(std::numeric_limits<uint64_t>::max)());
		decision.healthyIntervalsRequired = FramesForDuration(
			timeScale, frameDuration, 1000, 1);

		if (m_waitingForHealthy)
		{
			if (counter.transition == LiveFrameCounterTransition::First)
				m_healthyIntervals = 0;
			else if (counter.transition ==
				LiveFrameCounterTransition::Consecutive &&
				downstreamHealthy)
				++m_healthyIntervals;
			else
				m_healthyIntervals = 0;

			if (m_healthyIntervals >=
				decision.healthyIntervalsRequired)
			{
				m_waitingForHealthy = false;
				m_recoveryRequested = false;
			}

			decision.healthyIntervalsObserved = m_healthyIntervals;
			if (counter.IsDiscontinuity())
			{
				decision.action =
					LiveSourceGapRecoveryAction::SuppressedUntilHealthy;
			}
			return decision;
		}

		if (!counter.IsDiscontinuity())
			return decision;

		// A counter reset can accompany an HDR/rate/format transition whose
		// owner-side state callback has not yet arrived. Keep it local here;
		// only a duration-qualified forward gap proves this same-contract graph
		// has lost enough live input to need a downstream re-prime.
		const bool materialGap =
			counter.transition == LiveFrameCounterTransition::ForwardGap &&
			counter.missingFrames >= decision.materialGapFrames;
		if (!materialGap)
		{
			decision.action =
				LiveSourceGapRecoveryAction::LocalDiscontinuity;
			return decision;
		}

		// A blocked downstream Receive can backpressure VP until the capture
		// callback itself misses frames. That is not evidence of an HDMI/source
		// outage and must not start a second graph-reset loop.
		if (!downstreamHealthy)
		{
			decision.action =
				LiveSourceGapRecoveryAction::SuppressedUntilHealthy;
			return decision;
		}

		if (m_recoveryRequested)
		{
			decision.action =
				LiveSourceGapRecoveryAction::SuppressedUntilHealthy;
			return decision;
		}

		m_recoveryRequested = true;
		decision.action =
			LiveSourceGapRecoveryAction::RequestGraphReprime;
		return decision;
	}

	void OnGraphReset() noexcept
	{
		m_recoveryRequested = false;
		m_waitingForHealthy = true;
		m_healthyIntervals = 0;
	}

private:
	static uint64_t FramesForDuration(
		uint64_t timeScale,
		uint64_t frameDuration,
		uint64_t durationMs,
		uint64_t fallback) noexcept
	{
		if (timeScale == 0 || frameDuration == 0 || durationMs == 0)
			return fallback;

		if (frameDuration >
			(std::numeric_limits<uint64_t>::max)() / 1000)
			return fallback;
		const uint64_t denominator = frameDuration * 1000;
		if (timeScale >
			(std::numeric_limits<uint64_t>::max)() / durationMs)
			return fallback;
		const uint64_t numerator = timeScale * durationMs;
		return numerator / denominator +
			(numerator % denominator == 0 ? 0 : 1);
	}

	bool m_recoveryRequested = false;
	bool m_waitingForHealthy = false;
	uint64_t m_healthyIntervals = 0;
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
