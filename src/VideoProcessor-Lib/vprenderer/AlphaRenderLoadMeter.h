/*
 * Copyright(C) 2025 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <RendererLiveness.h>
#include <OsdTimingPolicy.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>

// Reproduces PR #75's synchronous collection policy: each callback contributes
// the pass's latest asynchronously resolved libplacebo duration to the estimate
// assigned to the current comparison frame. This intentionally preserves the
// original method's lack of per-frame provenance so it can be compared fairly.
// Used only by the alternating-frame diagnostic comparison build.
class AlphaPr75PassAccumulator
{
public:
	static constexpr uint64_t MAXIMUM_VALID_PASS_NS = 1000000000ULL;

	void BeginFrame()
	{
		m_nanoseconds = 0;
		m_passes = 0;
	}

	void AddPass(uint64_t nanoseconds)
	{
		++m_passes;
		if (nanoseconds == 0 || nanoseconds > MAXIMUM_VALID_PASS_NS)
			return;
		m_nanoseconds += nanoseconds;
	}

	bool HasTiming() const { return m_nanoseconds > 0; }
	double Milliseconds() const
	{
		return static_cast<double>(m_nanoseconds) / 1000000.0;
	}
	size_t Passes() const { return m_passes; }

private:
	uint64_t m_nanoseconds = 0;
	size_t m_passes = 0;
};

// Correlates asynchronously resolved GPU durations with exact successful
// submission records. GPU results that cannot match renderer generation,
// source sequence, and submission serial are discarded; they can never leak
// into a later frame.
class AlphaRenderLoadMeter
{
public:
	using Clock = std::chrono::steady_clock;

	static constexpr double WINDOW_SECONDS = 10.0;
	static constexpr double GUARD_SECONDS = 3.0;
	static constexpr double GUARD_MAX_SECONDS = 10.0;
	static constexpr size_t CAPACITY = 2400;
	static constexpr double MAXIMUM_VALID_GPU_MS = 1000.0;

	explicit AlphaRenderLoadMeter(double guardSeconds = OsdTimingPolicy::WarmingEnabled ? GUARD_SECONDS : 0.0,
		double guardMaxSeconds = OsdTimingPolicy::WarmingEnabled ? GUARD_MAX_SECONDS : 0.0)
		: m_guardSeconds((std::max)(0.0, guardSeconds)),
		m_guardMaxSeconds((std::max)(m_guardSeconds, guardMaxSeconds)),
		m_guardStart(Clock::now())
	{
	}

	// Clears only the recent window and re-arms warm-up. Whole-renderer session
	// peaks survive a backlog recovery, but a new renderer instance constructs a
	// new meter and therefore starts a genuinely new session.
	void Reset()
	{
		std::unique_lock<std::mutex> guard(m_mutex, std::try_to_lock);
		if (!guard.owns_lock())
		{
			m_resetPending.store(true, std::memory_order_release);
			m_contentionDrops.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		m_resetPending.store(false, std::memory_order_release);
		ResetWindowLocked();
	}

	// A queue generation can change while an old render is still completing.
	// Reject that in-flight frame even if it commits after the reset request.
	void ResetForGeneration(uint64_t generation)
	{
		m_requiredGeneration.store(generation, std::memory_order_release);
		Reset();
	}

	void ResetForPipelineChange()
	{
		m_pipelineResetPending.store(true, std::memory_order_release);
		Reset();
	}

	// Records identity and CPU wall-time diagnostics only after both render and
	// submission succeeded. The GPU duration is associated later.
	void CommitFrame(uint64_t generation, uint64_t sourceSequence,
		uint64_t submissionSerial,
		double renderMs, double swapMs, double framePeriodMs,
		bool framePeriodFromRenderCadence)
	{
		std::unique_lock<std::mutex> guard(m_mutex, std::try_to_lock);
		if (!guard.owns_lock())
		{
			m_contentionDrops.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		ApplyPendingResetLocked();
		const uint64_t required = m_requiredGeneration.load(std::memory_order_acquire);
		if (required != 0 && generation != required)
			return;
		if (framePeriodMs > 0.0)
		{
			m_framePeriodMs = Sanitized(framePeriodMs);
			m_framePeriodFromRenderCadence = framePeriodFromRenderCadence;
		}

		const Clock::time_point now = Clock::now();
		EvictExpiredLocked(now);
		if (m_count == CAPACITY)
		{
			m_head = (m_head + 1) % CAPACITY;
			--m_count;
		}

		Sample& sample = m_samples[(m_head + m_count) % CAPACITY];
		sample = {};
		sample.stamp = now;
		sample.generation = generation;
		sample.sourceSequence = sourceSequence;
		sample.submissionSerial = submissionSerial;
		sample.eligible = GuardLiftedLocked(now);
		sample.renderMs = Sanitized(renderMs);
		sample.swapMs = Sanitized(swapMs);
		sample.framePeriodMs = Sanitized(framePeriodMs);
		sample.framePeriodFromRenderCadence = framePeriodFromRenderCadence &&
			sample.framePeriodMs > 0.0;
		++m_count;
		if (!sample.eligible)
			return;
		++m_sessionFrames;
		m_sessionRenderPeakMs =
			(std::max)(m_sessionRenderPeakMs, sample.renderMs);
	}

	// Attaches one asynchronously resolved pl_render_image interval to the matching
	// successful submission. Repeated presentation of one source sequence is
	// therefore handled in submission order. Any identity mismatch is stale
	// evidence and is counted, then discarded.
	bool RecordGpuFrame(uint64_t generation, uint64_t sourceSequence,
		uint64_t submissionSerial,
		double gpuMs, uint64_t lagFrames)
	{
		std::unique_lock<std::mutex> guard(m_mutex, std::try_to_lock);
		if (!guard.owns_lock())
		{
			m_contentionDrops.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		ApplyPendingResetLocked();
		const uint64_t required = m_requiredGeneration.load(std::memory_order_acquire);
		if (required != 0 && generation != required)
		{
			++m_unmatchedGpuSamples;
			return false;
		}
		gpuMs = Sanitized(gpuMs);
		if (gpuMs <= 0.0 || gpuMs > MAXIMUM_VALID_GPU_MS)
		{
			++m_invalidGpuSamples;
			return false;
		}
		const Clock::time_point now = Clock::now();
		EvictExpiredLocked(now);
		Sample* const matched = FindSubmissionLocked(submissionSerial);
		if (matched && matched->generation == generation &&
			matched->sourceSequence == sourceSequence && !matched->gpuTimed)
		{
			Sample& sample = *matched;
			m_gpuEverTimed = true;
			sample.gpuMs = gpuMs;
			sample.gpuTimed = true;
			sample.gpuLagFrames = lagFrames;
			if (!sample.eligible)
			{
				++m_warmupGpuSamples;
				return true;
			}
			m_latestGpuSourceSequence = sourceSequence;
			m_latestGpuSubmissionSerial = submissionSerial;
			m_latestGpuLagFrames = lagFrames;
			if (!m_sessionPeakValid || gpuMs > m_sessionGpuPeakMs)
				m_sessionGpuPeakMs = gpuMs;
			m_sessionPeakValid = true;
			if (sample.framePeriodFromRenderCadence)
			{
				const double percent =
					100.0 * gpuMs / sample.framePeriodMs;
				if (!m_sessionGpuPercentValid ||
					percent > m_sessionGpuPercent)
				{
					m_sessionGpuPercentValid = true;
					m_sessionGpuPercent = percent;
					m_sessionGpuWorstLoadMs = gpuMs;
					m_sessionGpuWorstLoadFramePeriodMs =
						sample.framePeriodMs;
				}
			}
			++m_sessionGpuFrames;
			return true;
		}
		++m_unmatchedGpuSamples;
		return false;
	}

	RendererRenderLoad Snapshot() const
	{
		std::unique_lock<std::mutex> guard(m_mutex, std::try_to_lock);
		RendererRenderLoad result;
		result.supported = true;
		if (!guard.owns_lock())
		{
			result.telemetryContentionDrops =
				m_contentionDrops.fetch_add(1, std::memory_order_relaxed) + 1;
			return result;
		}
		if (m_resetPending.load(std::memory_order_acquire))
			return result;
		result.windowSeconds = WINDOW_SECONDS;
		result.framePeriodMs = m_framePeriodMs;
		result.framePeriodFromRenderCadence = m_framePeriodFromRenderCadence;
		result.latestGpuSourceSequence = m_latestGpuSourceSequence;
		result.latestGpuSubmissionSerial = m_latestGpuSubmissionSerial;
		result.latestGpuLagFrames = m_latestGpuLagFrames;
		result.unmatchedGpuSamples = m_unmatchedGpuSamples;
		result.invalidGpuSamples = m_invalidGpuSamples;
		result.warmupGpuSamples = m_warmupGpuSamples;

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
		result.telemetryContentionDrops =
			m_contentionDrops.load(std::memory_order_relaxed);

		result.windowFilledSeconds = WindowSpanMsLocked(now) / 1000.0;
		size_t live = 0;
		double gpuTotal = 0.0;
		double gpuLoadPercentTotal = 0.0;
		double renderTotal = 0.0;
		double swapTotal = 0.0;
		size_t gpuTimed = 0;
		size_t gpuLoadPercentSamples = 0;
		for (size_t index = 0; index < m_count; ++index)
		{
			const Sample& sample = m_samples[(m_head + index) % CAPACITY];
			if (ExpiredLocked(sample, now) || !sample.eligible)
				continue;
			++live;
			if (sample.gpuTimed)
			{
				gpuTotal += sample.gpuMs;
				++gpuTimed;
				result.gpu.peak = (std::max)(result.gpu.peak, sample.gpuMs);
				if (sample.framePeriodFromRenderCadence)
				{
					const double percent =
						100.0 * sample.gpuMs / sample.framePeriodMs;
					gpuLoadPercentTotal += percent;
					++gpuLoadPercentSamples;
					if (!result.gpuLoadPercentValid ||
						percent > result.gpuLoadPercent)
					{
						result.gpuLoadPercentValid = true;
						result.gpuLoadPercent = percent;
						result.gpuWorstLoadMs = sample.gpuMs;
						result.gpuWorstLoadFramePeriodMs =
							sample.framePeriodMs;
					}
				}
				result.gpu.last = sample.gpuMs;
			}
			renderTotal += sample.renderMs;
			swapTotal += sample.swapMs;
			result.render.last = sample.renderMs;
			result.swap.last = sample.swapMs;
			result.render.peak = (std::max)(result.render.peak, sample.renderMs);
			result.swap.peak = (std::max)(result.swap.peak, sample.swapMs);
		}

		result.frames = live;
		result.gpuFrames = gpuTimed;
		result.valid = live > 0;
		result.gpuValid = gpuTimed > 0;
		if (gpuTimed > 0)
			result.gpu.average = gpuTotal / static_cast<double>(gpuTimed);
		if (gpuLoadPercentSamples > 0)
			result.gpuAverageLoadPercent = gpuLoadPercentTotal /
				static_cast<double>(gpuLoadPercentSamples);
		if (live > 0)
		{
			result.render.average = renderTotal / static_cast<double>(live);
			result.swap.average = swapTotal / static_cast<double>(live);
		}
		return result;
	}

private:
	void ResetWindowLocked()
	{
		if (m_pipelineResetPending.exchange(false, std::memory_order_acq_rel))
		{
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
		m_head = 0;
		m_count = 0;
		m_gpuEverTimed = false;
		m_framePeriodMs = 0.0;
		m_framePeriodFromRenderCadence = false;
		m_guardStart = Clock::now();
		m_latestGpuSourceSequence = 0;
		m_latestGpuSubmissionSerial = 0;
		m_latestGpuLagFrames = 0;
	}

	void ApplyPendingResetLocked()
	{
		if (m_resetPending.exchange(false, std::memory_order_acq_rel))
			ResetWindowLocked();
	}

	struct Sample
	{
		Clock::time_point stamp{};
		uint64_t generation = 0;
		uint64_t sourceSequence = 0;
		uint64_t submissionSerial = 0;
		uint64_t gpuLagFrames = 0;
		double gpuMs = 0.0;
		double renderMs = 0.0;
		double swapMs = 0.0;
		double framePeriodMs = 0.0;
		bool gpuTimed = false;
		bool eligible = false;
		bool framePeriodFromRenderCadence = false;
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

	// Successful-submission serials are strictly increasing in the ring. Use
	// them as the lookup key so one resolved query costs O(log N), rather than
	// scanning up to ten seconds of frames on the render thread.
	Sample* FindSubmissionLocked(uint64_t submissionSerial)
	{
		size_t first = 0;
		size_t count = m_count;
		while (count > 0)
		{
			const size_t step = count / 2;
			const size_t middle = first + step;
			Sample& sample = m_samples[(m_head + middle) % CAPACITY];
			if (sample.submissionSerial < submissionSerial)
			{
				first = middle + 1;
				count -= step + 1;
			}
			else
			{
				count = step;
			}
		}
		if (first >= m_count)
			return nullptr;
		Sample& sample = m_samples[(m_head + first) % CAPACITY];
		return sample.submissionSerial == submissionSerial ? &sample : nullptr;
	}

	bool GuardLiftedLocked(Clock::time_point now) const
	{
		const double elapsed = ElapsedSeconds(m_guardStart, now);
		if (elapsed < m_guardSeconds)
			return false;
		return m_gpuEverTimed || elapsed >= m_guardMaxSeconds ||
			m_guardSeconds == 0.0;
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
			if (!ExpiredLocked(sample, now) && sample.eligible)
				return ElapsedSeconds(sample.stamp, now) * 1000.0;
		}
		return 0.0;
	}

	mutable std::mutex m_mutex;
	std::array<Sample, CAPACITY> m_samples{};
	size_t m_head = 0;
	size_t m_count = 0;
	bool m_gpuEverTimed = false;
	double m_framePeriodMs = 0.0;
	bool m_framePeriodFromRenderCadence = false;
	double m_guardSeconds = GUARD_SECONDS;
	double m_guardMaxSeconds = GUARD_MAX_SECONDS;
	Clock::time_point m_guardStart{};

	double m_sessionGpuPeakMs = 0.0;
	bool m_sessionGpuPercentValid = false;
	double m_sessionGpuPercent = 0.0;
	double m_sessionGpuWorstLoadMs = 0.0;
	double m_sessionGpuWorstLoadFramePeriodMs = 0.0;
	double m_sessionRenderPeakMs = 0.0;
	bool m_sessionPeakValid = false;
	uint64_t m_sessionFrames = 0;
	uint64_t m_sessionGpuFrames = 0;
	uint64_t m_latestGpuSourceSequence = 0;
	uint64_t m_latestGpuSubmissionSerial = 0;
	uint64_t m_latestGpuLagFrames = 0;
	uint64_t m_unmatchedGpuSamples = 0;
	uint64_t m_invalidGpuSamples = 0;
	uint64_t m_warmupGpuSamples = 0;
	std::atomic<uint64_t> m_requiredGeneration{ 0 };
	std::atomic_bool m_pipelineResetPending{ false };
	std::atomic_bool m_resetPending{ false };
	mutable std::atomic<uint64_t> m_contentionDrops{ 0 };
};
