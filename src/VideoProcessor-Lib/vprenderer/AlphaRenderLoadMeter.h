/*
 * Copyright(C) 2025 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.
 */

#pragma once

#include <RendererLiveness.h>
#include <OsdTimingPolicy.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>

// Rolling render-load telemetry for the Ctrl+I OSD.
//
// GPU values come directly from libplacebo's asynchronous per-pass timers.
// A callback reports the most recently resolved value for each pass; it may lag
// or be omitted while a query is unresolved. Consequently a committed sample is
// a recent render-cost estimate, not an exact timestamp ledger for the frame
// currently being submitted. We intentionally never flush or wait for a query.
class AlphaRenderLoadMeter
{
public:
	using Clock = std::chrono::steady_clock;

	static constexpr double WINDOW_SECONDS = 10.0;
	static constexpr double GUARD_SECONDS = 3.0;
	static constexpr double GUARD_MAX_SECONDS = 10.0;
	static constexpr size_t CAPACITY = 2400;
	static constexpr uint64_t ABSURD_PASS_NS = 1000000000ULL;

	explicit AlphaRenderLoadMeter(double guardSeconds = OsdTimingPolicy::WarmingEnabled ? GUARD_SECONDS : 0.0,
		double guardMaxSeconds = OsdTimingPolicy::WarmingEnabled ? GUARD_MAX_SECONDS : 0.0)
		: m_guardSeconds((std::max)(0.0, guardSeconds)),
		m_guardMaxSeconds((std::max)(m_guardSeconds, guardMaxSeconds)),
		m_guardStart(Clock::now())
	{
	}

	// Render-thread boundary. This prevents unresolved values left by a failed
	// render from leaking into the next successfully submitted frame.
	void BeginFrame()
	{
		m_pendingGpuNs = 0;
		m_pendingPasses = 0;
		m_pendingGpuTimed = false;
	}

	// Called synchronously by libplacebo on the render thread. Pending state is
	// render-thread-only, avoiding a mutex operation for every shader pass.
	void AddPass(uint64_t nanoseconds)
	{
		++m_pendingPasses;
		if (nanoseconds == 0 || nanoseconds > ABSURD_PASS_NS)
			return;
		m_pendingGpuNs += nanoseconds;
		m_pendingGpuTimed = true;
	}

	void CommitFrame(double renderMs, double swapMs, double framePeriodMs,
		bool framePeriodFromDisplay)
	{
		const uint64_t pendingGpuNs = m_pendingGpuNs;
		const int pendingPasses = m_pendingPasses;
		const bool pendingGpuTimed = m_pendingGpuTimed;
		BeginFrame();

		std::lock_guard<std::mutex> guard(m_mutex);
		renderMs = Sanitized(renderMs);
		swapMs = Sanitized(swapMs);
		const double gpuMs = Sanitized(
			static_cast<double>(pendingGpuNs) / 1000000.0);
		m_lastPasses = pendingPasses;
		m_gpuEverTimed = m_gpuEverTimed || pendingGpuTimed;
		if (framePeriodMs > 0.0)
		{
			m_framePeriodMs = Sanitized(framePeriodMs);
			m_framePeriodFromDisplay = framePeriodFromDisplay;
		}

		const Clock::time_point now = Clock::now();
		if (!GuardLiftedLocked(now))
			return;

		EvictExpiredLocked(now);
		if (m_count == CAPACITY)
		{
			m_head = (m_head + 1) % CAPACITY;
			--m_count;
		}
		Sample& sample = m_samples[(m_head + m_count) % CAPACITY];
		sample.stamp = now;
		sample.gpuMs = gpuMs;
		sample.gpuTimed = pendingGpuTimed;
		sample.renderMs = renderMs;
		sample.swapMs = swapMs;
		sample.framePeriodMs = m_framePeriodMs;
		sample.framePeriodFromDisplay = m_framePeriodFromDisplay;
		++m_count;

		++m_sessionFrames;
		m_sessionRenderPeakMs = (std::max)(m_sessionRenderPeakMs, renderMs);
		if (pendingGpuTimed)
		{
			++m_sessionGpuFrames;
			m_sessionPeakValid = true;
			m_sessionGpuPeakMs = (std::max)(m_sessionGpuPeakMs, gpuMs);
			if (sample.framePeriodFromDisplay && sample.framePeriodMs > 0.0)
			{
				const double percent = 100.0 * gpuMs / sample.framePeriodMs;
				if (!m_sessionGpuPercentValid ||
					percent > m_sessionGpuPercent)
				{
					m_sessionGpuPercentValid = true;
					m_sessionGpuPercent = percent;
					m_sessionGpuWorstLoadMs = gpuMs;
					m_sessionGpuWorstLoadFramePeriodMs = sample.framePeriodMs;
				}
			}
		}
	}

	// Backlog recovery clears the recent window but preserves evidence of an
	// earlier peak. It also re-arms startup/compile settling.
	void Reset()
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		ResetWindowLocked();
	}

	// A live shader/profile change creates a different render pipeline. Neither
	// warm-up frames nor peaks from the old pipeline should describe the new one.
	void ResetForPipelineChange()
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		ResetWindowLocked();
		m_sessionFrames = 0;
		m_sessionGpuFrames = 0;
		m_sessionGpuPeakMs = 0.0;
		m_sessionRenderPeakMs = 0.0;
		m_sessionPeakValid = false;
		m_sessionGpuPercentValid = false;
		m_sessionGpuPercent = 0.0;
		m_sessionGpuWorstLoadMs = 0.0;
		m_sessionGpuWorstLoadFramePeriodMs = 0.0;
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
		result.sessionPeakValid = m_sessionPeakValid;
		result.sessionFrames = m_sessionFrames;
		result.sessionGpuFrames = m_sessionGpuFrames;
		result.sessionGpuPeakMs = m_sessionGpuPeakMs;
		result.sessionRenderPeakMs = m_sessionRenderPeakMs;
		result.sessionGpuPercentValid = m_sessionGpuPercentValid;
		result.sessionGpuPercent = m_sessionGpuPercent;
		result.sessionGpuWorstLoadMs = m_sessionGpuWorstLoadMs;
		result.sessionGpuWorstLoadFramePeriodMs =
			m_sessionGpuWorstLoadFramePeriodMs;
		result.windowFilledSeconds = WindowSpanSecondsLocked(now);

		size_t live = 0;
		double gpuTotal = 0.0;
		double renderTotal = 0.0;
		double swapTotal = 0.0;
		for (size_t index = 0; index < m_count; ++index)
		{
			const Sample& sample = m_samples[(m_head + index) % CAPACITY];
			if (ExpiredLocked(sample, now))
				continue;
			++live;
			if (sample.gpuTimed)
			{
				gpuTotal += sample.gpuMs;
				++result.gpuFrames;
				result.gpu.peak = (std::max)(result.gpu.peak, sample.gpuMs);
				result.gpu.last = sample.gpuMs;
				if (sample.framePeriodFromDisplay && sample.framePeriodMs > 0.0)
				{
					const double percent =
						100.0 * sample.gpuMs / sample.framePeriodMs;
					if (!result.gpuLoadPercentValid ||
						percent > result.gpuLoadPercent)
					{
						result.gpuLoadPercentValid = true;
						result.gpuLoadPercent = percent;
						result.gpuWorstLoadMs = sample.gpuMs;
						result.gpuWorstLoadFramePeriodMs = sample.framePeriodMs;
					}
				}
			}
			renderTotal += sample.renderMs;
			swapTotal += sample.swapMs;
			result.render.last = sample.renderMs;
			result.swap.last = sample.swapMs;
			result.render.peak = (std::max)(result.render.peak, sample.renderMs);
			result.swap.peak = (std::max)(result.swap.peak, sample.swapMs);
		}

		result.frames = live;
		result.valid = live > 0;
		result.gpuValid = result.gpuFrames > 0;
		if (result.gpuFrames > 0)
			result.gpu.average = gpuTotal / static_cast<double>(result.gpuFrames);
		if (live > 0)
		{
			result.render.average = renderTotal / static_cast<double>(live);
			result.swap.average = swapTotal / static_cast<double>(live);
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
		double framePeriodMs = 0.0;
		bool gpuTimed = false;
		bool framePeriodFromDisplay = false;
	};

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
		if (elapsed < m_guardSeconds)
			return false;
		return m_gpuEverTimed || elapsed >= m_guardMaxSeconds;
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

	double WindowSpanSecondsLocked(Clock::time_point now) const
	{
		for (size_t index = 0; index < m_count; ++index)
		{
			const Sample& sample = m_samples[(m_head + index) % CAPACITY];
			if (!ExpiredLocked(sample, now))
				return ElapsedSeconds(sample.stamp, now);
		}
		return 0.0;
	}

	void ResetWindowLocked()
	{
		m_head = 0;
		m_count = 0;
		BeginFrame();
		m_lastPasses = 0;
		m_gpuEverTimed = false;
		m_guardStart = Clock::now();
	}

	mutable std::mutex m_mutex;
	std::array<Sample, CAPACITY> m_samples{};
	size_t m_head = 0;
	size_t m_count = 0;
	uint64_t m_pendingGpuNs = 0;
	int m_pendingPasses = 0;
	bool m_pendingGpuTimed = false;
	int m_lastPasses = 0;
	bool m_gpuEverTimed = false;
	double m_framePeriodMs = 0.0;
	bool m_framePeriodFromDisplay = false;
	const double m_guardSeconds;
	const double m_guardMaxSeconds;
	Clock::time_point m_guardStart{};

	double m_sessionGpuPeakMs = 0.0;
	double m_sessionRenderPeakMs = 0.0;
	bool m_sessionPeakValid = false;
	uint64_t m_sessionFrames = 0;
	uint64_t m_sessionGpuFrames = 0;
	bool m_sessionGpuPercentValid = false;
	double m_sessionGpuPercent = 0.0;
	double m_sessionGpuWorstLoadMs = 0.0;
	double m_sessionGpuWorstLoadFramePeriodMs = 0.0;
};
