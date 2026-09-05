/*
 * Copyright(C) 2025 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <RendererLiveness.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>

// Render-cost measurement for the Ctrl+I OSD.
//
// WHY THIS EXISTS
//   The presentation telemetry log line already carries `render_ms` and
//   `swap_ms`, but it emits one spot sample of one frame every 2-5 seconds.
//   That cannot answer "is this setting too expensive for this GPU", because
//   the frame it happened to sample is not the frame that overran.
//
// THREE TIERS, ANSWERING THREE DIFFERENT QUESTIONS
//   average over the window  what it normally costs
//   peak over the window     what it just did
//   session peak             whether it EVER hit the ceiling
//
//   The session peak is the one that cannot be reconstructed after the fact,
//   and today nothing in the build keeps it. It matters because the window is
//   cleared by ResetTimingAfterBacklogRecovery - which is triggered BY a render
//   stall - so a stall bad enough to drop frames currently erases its own
//   evidence. The session peak deliberately survives Reset() for that reason.
//
//   "Session" means the life of one renderer instance. A renderer restart
//   destroys this Impl and builds a new one, so the peak starts again - which
//   is correct, because a restart follows a source or output change and a peak
//   carried across it would describe a pipeline that no longer exists.
//
// THE WINDOW IS MEASURED IN SECONDS, NOT FRAMES
//   A frame-count window silently means different things at different refresh
//   rates: 256 frames is 10.7s at 24p but 4.3s at 60p, so two runs cannot be
//   compared. (The same bug exists in RollingPerformanceWindow in the frame
//   formatters, whose "10 second" window is 600 samples - really 25s at 24p.
//   Not fixed here; that is a separate change on the conversion hot path.)
//
// THE WARM-UP GUARD
//   The first frames after a start, a renderer restart, or a backlog recovery
//   are shader compilation and pipeline priming, not render cost - one measured
//   483 ms. Those must not become the session peak, so samples are dropped
//   entirely until the guard lifts and the OSD reports `settling` instead.
//
// WHAT THE THREE COSTS MEAN - they are not interchangeable
//   gpu     Time the GPU spent executing the render passes, from libplacebo's
//           per-pass timer queries. This is the number that scales with the
//           quality settings, and the only one that says whether the GPU has
//           headroom.
//   render  CPU wall time around pl_render_image. On a VP-owned FLIP_DISCARD
//           swapchain this is mostly the BLOCKED wait for the display, not
//           work - measured at 30 ms against a 41.71 ms frame while the GPU
//           used 4.4 ms. Never read it as cost.
//   swap    Wall time around the present. On that same path it is ~0.07 ms
//           because the present queues and returns. Never add it to `render`
//           and call the sum a frame cost.
//   Which of the two absorbs the wait is a property of the swapchain path, not
//   of load, so neither is shown on the OSD. Both stay in the log, where they
//   sit beside the pre-existing render_ms/swap_ms spot samples and are useful
//   only once frames actually drop.

// The measurement itself is transported in RendererRenderLoad, declared in
// RendererLiveness.h, so the OSD does not depend on this backend's header.
class AlphaRenderLoadMeter
{
public:
	using Clock = std::chrono::steady_clock;

	// Rolling window length. Long enough to be stable, short enough that a
	// setting can be changed and the effect seen without a long wait.
	static constexpr double WINDOW_SECONDS = 10.0;

	// Samples are dropped for this long after any reset. Covers shader
	// compilation and pipeline priming.
	static constexpr double GUARD_SECONDS = 3.0;

	// The guard normally lifts once the GPU timers have resolved, which proves
	// shader compilation finished. This caps the wait if they never do, so a
	// path without timer support still records a session peak.
	static constexpr double GUARD_MAX_SECONDS = 10.0;

	// 10 s of headroom up to 240 Hz. Samples are evicted by age, so this is
	// only an upper bound, never the window length.
	static constexpr size_t CAPACITY = 2400;

	// A single render pass taking over a second is a failed timer readback,
	// not a cost. Rejected rather than recorded.
	static constexpr uint64_t ABSURD_PASS_NS = 1000000000ULL;

	AlphaRenderLoadMeter()
	{
		m_guardStart = Clock::now();
	}

	// Clears the rolling window and re-arms the warm-up guard.
	//
	// The session peak deliberately SURVIVES this. Reset() is called from
	// ResetTimingAfterBacklogRecovery, i.e. immediately after the kind of stall
	// the session peak exists to record; clearing it here would delete the
	// evidence at exactly the moment it was earned.
	void Reset()
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		m_head = 0;
		m_count = 0;
		m_pendingGpuNs = 0;
		m_pendingPasses = 0;
		m_lastPasses = 0;
		m_gpuEverTimed = false;
		// m_framePeriodMs and m_framePeriodFromDisplay deliberately survive.
		// The frame period is a property of the DISPLAY, not of the sample
		// window, and the session peak survives too - clearing it here left
		// the session percentage blank for the whole guard period after a
		// backlog recovery while its milliseconds still showed. Observed on
		// the rig 2026-09-05: "gpu_session_peak_ms=7.931 gpu_session_pct=0.0".
		m_guardStart = Clock::now();
	}

	// Called from libplacebo's render info_callback, once per shader pass,
	// during pl_render_image. Accumulates into the frame being rendered.
	//
	// CAVEAT: `nanoseconds` is the last RESOLVED timer result for that pass,
	// which libplacebo reads back asynchronously - it lags the current frame by
	// a few frames and is 0 until the first query completes. The window is
	// therefore a recent-cost estimate, not a per-frame ledger. That is the
	// right trade here: blocking on a timer query to make it exact would cost
	// more than the measurement is worth.
	void AddPass(uint64_t nanoseconds)
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		++m_pendingPasses;
		if (nanoseconds == 0)
			return;
		// A timer result beyond a second is not a render pass, it is a bad
		// readback. The session peak never decays, so one absurd value would
		// pin it for the life of the renderer.
		if (nanoseconds > ABSURD_PASS_NS)
			return;
		m_pendingGpuNs += nanoseconds;
		m_gpuEverTimed = true;
	}

	// Called once per presented frame, after the present completes.
	//
	// `framePeriodFromDisplay` says whether framePeriodMs was derived from the
	// measured DISPLAY rate or fell back to the source rate. It matters: at 60
	// Hz output with 24p content the two differ by 2.5x, and a percentage
	// computed against the source rate would report 16% for a frame actually
	// using 40% of the refresh.
	void CommitFrame(double renderMs, double swapMs, double framePeriodMs,
		bool framePeriodFromDisplay)
	{
		std::lock_guard<std::mutex> guard(m_mutex);

		renderMs = Sanitized(renderMs);
		swapMs = Sanitized(swapMs);
		const double gpuMs =
			Sanitized(static_cast<double>(m_pendingGpuNs) / 1000000.0);
		const bool gpuTimed = m_pendingGpuNs > 0;
		m_lastPasses = m_pendingPasses;
		m_pendingGpuNs = 0;
		m_pendingPasses = 0;
		if (framePeriodMs > 0.0)
		{
			m_framePeriodMs = Sanitized(framePeriodMs);
			m_framePeriodFromDisplay = framePeriodFromDisplay;
		}

		const Clock::time_point now = Clock::now();

		// Warm-up frames are not render cost. Dropping them rather than
		// recording them keeps them out of the average and the peaks alike.
		if (!GuardLiftedLocked(now))
			return;

		EvictExpiredLocked(now);
		if (m_count == CAPACITY)
		{
			// Cannot happen below 240 Hz, but overwriting the oldest is the
			// correct behaviour if it ever does.
			m_head = (m_head + 1) % CAPACITY;
			--m_count;
		}
		Sample& sample = m_samples[(m_head + m_count) % CAPACITY];
		sample.stamp = now;
		sample.gpuMs = gpuMs;
		sample.gpuTimed = gpuTimed;
		sample.renderMs = renderMs;
		sample.swapMs = swapMs;
		++m_count;

		++m_sessionFrames;
		m_sessionRenderPeakMs = std::max(m_sessionRenderPeakMs, renderMs);
		if (gpuTimed)
		{
			m_sessionGpuPeakMs = std::max(m_sessionGpuPeakMs, gpuMs);
			m_sessionPeakValid = true;
		}
	}

	RendererRenderLoad Snapshot() const
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		RendererRenderLoad result;
		result.supported = true;
		result.windowSeconds = WINDOW_SECONDS;
		result.framePeriodMs = m_framePeriodMs;
		result.framePeriodFromDisplay = m_framePeriodFromDisplay;
		result.gpuPasses = m_lastPasses;

		const Clock::time_point now = Clock::now();
		result.settling = !GuardLiftedLocked(now);

		// Session peaks are reported even while settling: they describe the
		// whole session, and a restart mid-session must not blank them.
		result.sessionPeakValid = m_sessionPeakValid;
		result.sessionFrames = m_sessionFrames;
		result.sessionGpuPeakMs = m_sessionGpuPeakMs;
		result.sessionRenderPeakMs = m_sessionRenderPeakMs;
		if (m_framePeriodMs > 0.0 && m_sessionPeakValid)
		{
			result.sessionGpuPercent =
				100.0 * m_sessionGpuPeakMs / m_framePeriodMs;
		}

		const double windowMs = WindowSpanMsLocked(now);
		result.windowFilledSeconds = windowMs / 1000.0;

		size_t live = 0;
		double gpuTotal = 0.0;
		double renderTotal = 0.0;
		double swapTotal = 0.0;
		size_t gpuTimed = 0;
		for (size_t index = 0; index < m_count; ++index)
		{
			const Sample& sample = m_samples[(m_head + index) % CAPACITY];
			if (ExpiredLocked(sample, now))
				continue;
			++live;
			// Frames whose timer had not resolved yet would drag the GPU
			// average toward zero, so they are excluded rather than counted
			// as free.
			if (sample.gpuTimed)
			{
				gpuTotal += sample.gpuMs;
				++gpuTimed;
				result.gpu.peak = std::max(result.gpu.peak, sample.gpuMs);
				result.gpu.last = sample.gpuMs;
			}
			renderTotal += sample.renderMs;
			swapTotal += sample.swapMs;
			result.render.last = sample.renderMs;
			result.swap.last = sample.swapMs;
			result.render.peak = std::max(result.render.peak, sample.renderMs);
			result.swap.peak = std::max(result.swap.peak, sample.swapMs);
		}

		result.frames = live;
		result.valid = live > 0;
		result.gpuValid = gpuTimed > 0;
		if (gpuTimed > 0)
			result.gpu.average = gpuTotal / static_cast<double>(gpuTimed);
		if (live > 0)
		{
			result.render.average = renderTotal / static_cast<double>(live);
			result.swap.average = swapTotal / static_cast<double>(live);
		}

		// Report headroom against the PEAK, not the average: a frame that
		// overran once is visible on screen, and an average that hides it is
		// what makes a marginal setting look safe.
		if (result.framePeriodMs > 0.0 && gpuTimed > 0)
		{
			result.gpuLoadPercent =
				100.0 * result.gpu.peak / result.framePeriodMs;
		}
		return result;
	}

private:
	struct Sample
	{
		Clock::time_point stamp{};
		double gpuMs = 0.0;
		double renderMs = 0.0;
		double swapMs = 0.0;
		bool gpuTimed = false;
	};

	// Borrowed from RendererHealthTracker::SanitizedDuration - a non-finite or
	// negative duration must never reach the window, and least of all the
	// session peak, which has no way to forget it.
	static double Sanitized(double milliseconds)
	{
		return std::isfinite(milliseconds) ?
			(std::max)(0.0, milliseconds) : 0.0;
	}

	static double ElapsedSeconds(Clock::time_point from, Clock::time_point to)
	{
		return std::chrono::duration<double>(to - from).count();
	}

	bool GuardLiftedLocked(Clock::time_point now) const
	{
		const double elapsed = ElapsedSeconds(m_guardStart, now);
		if (elapsed < GUARD_SECONDS)
			return false;
		// Resolved GPU timers prove shader compilation has finished. The cap
		// keeps a path with no timer support from being guarded forever.
		return m_gpuEverTimed || elapsed >= GUARD_MAX_SECONDS;
	}

	bool ExpiredLocked(const Sample& sample, Clock::time_point now) const
	{
		return ElapsedSeconds(sample.stamp, now) > WINDOW_SECONDS;
	}

	void EvictExpiredLocked(Clock::time_point now)
	{
		while (m_count > 0 && ExpiredLocked(m_samples[m_head], now))
		{
			m_head = (m_head + 1) % CAPACITY;
			--m_count;
		}
	}

	double WindowSpanMsLocked(Clock::time_point now) const
	{
		for (size_t index = 0; index < m_count; ++index)
		{
			const Sample& sample = m_samples[(m_head + index) % CAPACITY];
			if (!ExpiredLocked(sample, now))
				return ElapsedSeconds(sample.stamp, now) * 1000.0;
		}
		return 0.0;
	}

	mutable std::mutex m_mutex;
	std::array<Sample, CAPACITY> m_samples{};
	size_t m_head = 0;
	size_t m_count = 0;
	uint64_t m_pendingGpuNs = 0;
	int m_pendingPasses = 0;
	int m_lastPasses = 0;
	bool m_gpuEverTimed = false;
	double m_framePeriodMs = 0.0;
	bool m_framePeriodFromDisplay = false;
	Clock::time_point m_guardStart{};

	// Survive Reset() by design - see the comment on Reset().
	double m_sessionGpuPeakMs = 0.0;
	double m_sessionRenderPeakMs = 0.0;
	bool m_sessionPeakValid = false;
	uint64_t m_sessionFrames = 0;
};
