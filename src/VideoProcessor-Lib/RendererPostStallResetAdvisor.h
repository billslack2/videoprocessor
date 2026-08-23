#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

// Read-only post-stall diagnostics shared by the VP and DirectShow/madVR
// paths. This class only classifies observations for logging; it has no reset
// callback and deliberately cannot change renderer state.
enum class PostStallRendererKind : uint8_t
{
	VpRenderer,
	MadVR
};

enum class PostStallResetDiagnosticState : uint8_t
{
	Monitoring,
	Settling,
	Suppressed,
	Advisory
};

struct PostStallResetObservation
{
	PostStallRendererKind renderer = PostStallRendererKind::VpRenderer;
	uint64_t nowTick = 0;
	uint64_t generation = 0;
	bool outputReady = false;
	bool rendererQuiet = false;
	// An event, rather than a persistent level. The observer remembers it for
	// the bounded post-stall window below.
	bool materialStall = false;
	size_t queueDepth = 0;
	size_t healthyQueueDepth = 0;
	double framePeriodMs = 0.0;
	double oldestQueuedAgeMs = 0.0;
	double renderMs = 0.0;
	double swapBlockMs = 0.0;
	bool scheduledLatencyKnown = false;
	double scheduledLatencyMs = 0.0;
};

struct PostStallResetDecision
{
	PostStallResetDiagnosticState state =
		PostStallResetDiagnosticState::Monitoring;
	const char* reason = "monitoring";
	bool shouldLog = false;
	// This is an advisory field for diagnostics only. No caller may treat it as
	// authorization to invoke a renderer reset.
	bool resetShouldOccur = false;
	uint32_t persistentBadObservations = 0;
	uint64_t millisecondsSinceMaterialStall = 0;
	double normalizedScheduledLatencyFrames = 0.0;
	double baselineScheduledLatencyFrames = 0.0;
	double scheduledLatencyDeltaFrames = 0.0;
};

inline const char* PostStallRendererKindText(PostStallRendererKind renderer)
{
	return renderer == PostStallRendererKind::VpRenderer ? "VP" : "madVR";
}

inline const char* PostStallResetDiagnosticStateText(
	PostStallResetDiagnosticState state)
{
	switch (state)
	{
	case PostStallResetDiagnosticState::Settling: return "settling";
	case PostStallResetDiagnosticState::Suppressed: return "suppressed";
	case PostStallResetDiagnosticState::Advisory: return "advisory";
	default: return "monitoring";
	}
}

class PostStallResetAdvisor
{
public:
	static constexpr uint64_t MATERIAL_STALL_MS = 1000;
	static constexpr uint64_t QUIET_WINDOW_MS = 2000;
	static constexpr uint64_t POST_STALL_WINDOW_MS = 60000;
	static constexpr uint64_t OBSERVATION_INTERVAL_MS = 1000;
	static constexpr uint64_t PERSISTENT_LOG_INTERVAL_MS = 10000;
	static constexpr uint32_t REQUIRED_BAD_OBSERVATIONS = 3;

	PostStallResetDecision Observe(const PostStallResetObservation& input)
	{
		if (!m_initialized || input.renderer != m_renderer ||
			input.generation != m_generation)
		{
			ResetForGeneration(input.renderer, input.generation);
		}

		PostStallResetDecision decision;
		decision.normalizedScheduledLatencyFrames =
			NormalizedFrames(input.scheduledLatencyMs, input.framePeriodMs);

		const bool validFramePeriod = input.framePeriodMs > 0.0;
		if (input.materialStall)
		{
			m_lastMaterialStallTick = input.nowTick;
			m_badObservations = 0;
			m_nextObservationTick = input.nowTick + OBSERVATION_INTERVAL_MS;
		}

		const uint64_t sinceStall = m_lastMaterialStallTick == 0 ? 0 :
			Elapsed(input.nowTick, m_lastMaterialStallTick);
		decision.millisecondsSinceMaterialStall = sinceStall;
		const bool withinPostStallWindow = m_lastMaterialStallTick != 0 &&
			sinceStall <= POST_STALL_WINDOW_MS;

		if (!withinPostStallWindow)
		{
			LearnMadVRBaseline(input, validFramePeriod);
			SetState(PostStallResetDiagnosticState::Monitoring, "monitoring",
				input.nowTick, decision);
			return decision;
		}

		if (!input.outputReady)
		{
			m_badObservations = 0;
			SetState(PostStallResetDiagnosticState::Suppressed,
				"output_not_ready", input.nowTick, decision);
			return decision;
		}
		if (!input.rendererQuiet)
		{
			m_badObservations = 0;
			SetState(PostStallResetDiagnosticState::Suppressed,
				"renderer_busy", input.nowTick, decision);
			return decision;
		}
		if (!validFramePeriod)
		{
			m_badObservations = 0;
			SetState(PostStallResetDiagnosticState::Suppressed,
				"frame_period_unknown", input.nowTick, decision);
			return decision;
		}
		if (sinceStall < QUIET_WINDOW_MS)
		{
			m_badObservations = 0;
			SetState(PostStallResetDiagnosticState::Settling,
				"post_stall_quiet_window", input.nowTick, decision);
			return decision;
		}

		const bool candidate = input.renderer == PostStallRendererKind::VpRenderer ?
			VpCandidate(input) : MadVRCandidate(input, decision);
		if (input.nowTick >= m_nextObservationTick)
		{
			m_nextObservationTick = input.nowTick + OBSERVATION_INTERVAL_MS;
			m_badObservations = candidate ? m_badObservations + 1 : 0;
		}

		if (m_badObservations >= REQUIRED_BAD_OBSERVATIONS)
		{
			SetState(PostStallResetDiagnosticState::Advisory,
				input.renderer == PostStallRendererKind::VpRenderer ?
					"queue_age_swap_persisted" :
					"scheduled_latency_one_frame_above_baseline",
				input.nowTick, decision);
		}
		else if (candidate)
		{
			SetState(PostStallResetDiagnosticState::Settling,
				"awaiting_persistent_observations", input.nowTick, decision);
		}
		else
		{
			SetState(PostStallResetDiagnosticState::Monitoring,
				"recovered", input.nowTick, decision);
		}
		return decision;
	}

	void Reset()
	{
		m_initialized = false;
		m_lastMaterialStallTick = 0;
		m_nextObservationTick = 0;
		m_lastLogTick = 0;
		m_badObservations = 0;
		m_latencyBaselineValid = false;
		m_latencyBaselineSamples = 0;
		m_state = PostStallResetDiagnosticState::Monitoring;
	}

private:
	static uint64_t Elapsed(uint64_t now, uint64_t before)
	{
		return now >= before ? now - before : 0;
	}

	static double NormalizedFrames(double milliseconds, double framePeriodMs)
	{
		return framePeriodMs > 0.0 ? milliseconds / framePeriodMs : 0.0;
	}

	void ResetForGeneration(PostStallRendererKind renderer, uint64_t generation)
	{
		Reset();
		m_initialized = true;
		m_renderer = renderer;
		m_generation = generation;
	}

	void LearnMadVRBaseline(const PostStallResetObservation& input,
		bool validFramePeriod)
	{
		if (input.renderer != PostStallRendererKind::MadVR ||
			!input.outputReady || !input.rendererQuiet ||
			!input.scheduledLatencyKnown || !validFramePeriod)
			return;

		const double latencyFrames = NormalizedFrames(input.scheduledLatencyMs,
			input.framePeriodMs);
		if (!m_latencyBaselineValid ||
			std::abs(m_latencyBaselineFramePeriodMs - input.framePeriodMs) /
				input.framePeriodMs > 0.05)
		{
			m_latencyBaselineValid = true;
			m_latencyBaselineFrames = latencyFrames;
			m_latencyBaselineFramePeriodMs = input.framePeriodMs;
			m_latencyBaselineSamples = 1;
			return;
		}

		// A short fixed window avoids silently adapting the baseline to the
		// elevated state that this diagnostic is intended to retain.
		if (m_latencyBaselineSamples < 10)
		{
			m_latencyBaselineFrames =
				(m_latencyBaselineFrames * m_latencyBaselineSamples + latencyFrames) /
				(m_latencyBaselineSamples + 1);
			++m_latencyBaselineSamples;
		}
	}

	bool VpCandidate(const PostStallResetObservation& input) const
	{
		return input.queueDepth > input.healthyQueueDepth &&
			input.oldestQueuedAgeMs >= input.framePeriodMs * 1.5 &&
			input.swapBlockMs >= input.framePeriodMs * 0.5;
	}

	bool MadVRCandidate(const PostStallResetObservation& input,
		PostStallResetDecision& decision) const
	{
		if (!input.scheduledLatencyKnown || !m_latencyBaselineValid)
			return false;

		decision.baselineScheduledLatencyFrames = m_latencyBaselineFrames;
		decision.scheduledLatencyDeltaFrames =
			decision.normalizedScheduledLatencyFrames - m_latencyBaselineFrames;
		// madVR occupancy is intentionally not inferred. This is only a
		// normalized VP-to-scheduled-latency delta after a material stall.
		return decision.scheduledLatencyDeltaFrames >= 0.9;
	}

	void SetState(PostStallResetDiagnosticState nextState, const char* reason,
		uint64_t nowTick, PostStallResetDecision& decision)
	{
		const PostStallResetDiagnosticState priorState = m_state;
		m_state = nextState;
		decision.state = nextState;
		decision.reason = reason;
		decision.resetShouldOccur =
			nextState == PostStallResetDiagnosticState::Advisory;
		decision.persistentBadObservations = m_badObservations;

		const bool stateChanged = priorState != nextState;
		const bool persistentState =
			nextState != PostStallResetDiagnosticState::Monitoring;
		const bool intervalElapsed = m_lastLogTick == 0 ||
			Elapsed(nowTick, m_lastLogTick) >= PERSISTENT_LOG_INTERVAL_MS;
		decision.shouldLog = stateChanged || (persistentState && intervalElapsed);
		if (decision.shouldLog)
			m_lastLogTick = nowTick;
	}

	bool m_initialized = false;
	PostStallRendererKind m_renderer = PostStallRendererKind::VpRenderer;
	uint64_t m_generation = 0;
	uint64_t m_lastMaterialStallTick = 0;
	uint64_t m_nextObservationTick = 0;
	uint64_t m_lastLogTick = 0;
	uint32_t m_badObservations = 0;
	PostStallResetDiagnosticState m_state =
		PostStallResetDiagnosticState::Monitoring;
	bool m_latencyBaselineValid = false;
	double m_latencyBaselineFrames = 0.0;
	double m_latencyBaselineFramePeriodMs = 0.0;
	uint32_t m_latencyBaselineSamples = 0;
};
