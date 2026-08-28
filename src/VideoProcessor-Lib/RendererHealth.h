#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>


enum class RendererHealthState
{
	Warming,
	Good,
	Degraded
};


struct RendererHealthSnapshot
{
	RendererHealthState state = RendererHealthState::Warming;
	uint64_t framesRendered = 0;
	uint64_t droppedFrames = 0;
	uint64_t timesStalled = 0;
	double stalledMs = 0.0;
	double renderAverageMs = 0.0;
	double renderPeakMs = 0.0;
	double submitAverageMs = 0.0;
	double submitPeakMs = 0.0;
};


// Accumulates the concise render-health evidence exposed by the Ctrl+I OSD.
// Callers provide GetTickCount64-compatible millisecond timestamps so this
// policy remains deterministic and independently testable.
class RendererHealthTracker
{
public:
	static constexpr uint64_t WARMING_FRAME_COUNT = 8;
	static constexpr uint64_t ISSUE_VISIBILITY_MS = 10000;

	void Reset(uint64_t droppedFrames = 0)
	{
		m_framesRendered = 0;
		m_lastDroppedFrames = droppedFrames;
		m_timesStalled = 0;
		m_stalledMs = 0.0;
		m_renderTotalMs = 0.0;
		m_renderPeakMs = 0.0;
		m_submitTotalMs = 0.0;
		m_submitPeakMs = 0.0;
		m_lastIssueTick = 0;
		m_hasIssue = false;
	}

	void RecordSuccessfulFrame(double renderMs, double submitMs)
	{
		renderMs = SanitizedDuration(renderMs);
		submitMs = SanitizedDuration(submitMs);
		++m_framesRendered;
		m_renderTotalMs += renderMs;
		m_renderPeakMs = std::max(m_renderPeakMs, renderMs);
		m_submitTotalMs += submitMs;
		m_submitPeakMs = std::max(m_submitPeakMs, submitMs);
	}

	void RecordStall(uint64_t nowTick, double durationMs)
	{
		++m_timesStalled;
		m_stalledMs += SanitizedDuration(durationMs);
		MarkIssue(nowTick);
	}

	void ObserveDroppedFrames(uint64_t nowTick, uint64_t droppedFrames)
	{
		if (droppedFrames > m_lastDroppedFrames)
			MarkIssue(nowTick);
		m_lastDroppedFrames = droppedFrames;
	}

	RendererHealthSnapshot Snapshot(
		uint64_t nowTick, uint64_t droppedFrames)
	{
		ObserveDroppedFrames(nowTick, droppedFrames);
		RendererHealthSnapshot result;
		result.framesRendered = m_framesRendered;
		result.droppedFrames = droppedFrames;
		result.timesStalled = m_timesStalled;
		result.stalledMs = m_stalledMs;
		if (m_framesRendered > 0)
		{
			result.renderAverageMs =
				m_renderTotalMs / static_cast<double>(m_framesRendered);
			result.submitAverageMs =
				m_submitTotalMs / static_cast<double>(m_framesRendered);
		}
		result.renderPeakMs = m_renderPeakMs;
		result.submitPeakMs = m_submitPeakMs;

		const bool recentIssue = m_hasIssue && nowTick >= m_lastIssueTick &&
			(nowTick - m_lastIssueTick) <= ISSUE_VISIBILITY_MS;
		result.state = recentIssue ? RendererHealthState::Degraded :
			m_framesRendered < WARMING_FRAME_COUNT ?
				RendererHealthState::Warming : RendererHealthState::Good;
		return result;
	}

private:
	static double SanitizedDuration(double durationMs)
	{
		return std::isfinite(durationMs) ? std::max(0.0, durationMs) : 0.0;
	}

	void MarkIssue(uint64_t nowTick)
	{
		m_lastIssueTick = nowTick;
		m_hasIssue = true;
	}

	uint64_t m_framesRendered = 0;
	uint64_t m_lastDroppedFrames = 0;
	uint64_t m_timesStalled = 0;
	double m_stalledMs = 0.0;
	double m_renderTotalMs = 0.0;
	double m_renderPeakMs = 0.0;
	double m_submitTotalMs = 0.0;
	double m_submitPeakMs = 0.0;
	uint64_t m_lastIssueTick = 0;
	bool m_hasIssue = false;
};
