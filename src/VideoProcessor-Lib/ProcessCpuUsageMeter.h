/*
 * Copyright(C) 2025 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdint>

// How much CPU VideoProcessor is actually using.
//
// WHY THIS EXISTS
//   Nothing in the build measured CPU. Every figure named "CPU" on the OSD was
//   wall-clock time around a function call, which on a vsync-paced render loop
//   measures WAITING, not work - `render_ms` reads 30 ms of a 41.71 ms frame
//   while the renderer is idle. This counts CPU time the OS actually charged to
//   the process, so a blocked thread contributes nothing.
//
// PROCESS-WIDE, NOT PER-THREAD
//   VP's real CPU cost is capture plus the V210/UYVY -> P010 conversion, not
//   the render call. A render-thread-only figure would miss the spike that
//   matters, so this covers every thread in the process.
//
// WHAT THE PERCENTAGE MEANS
//   Share of the WHOLE machine, the way Task Manager reports it: 100% means
//   every logical processor saturated. On 12 logical processors a single
//   saturated thread reads ~8%, not 100%.
class ProcessCpuUsageMeter
{
public:
	// CPU time is charged in scheduler ticks (~15.6 ms), so a short interval
	// quantises badly. One second is both stable and often enough to catch a
	// spike that would drop frames.
	static constexpr uint64_t SAMPLE_INTERVAL_MS = 1000;

	// Nothing is reported until this has elapsed. Startup - shader
	// compilation, device creation, cache load - is not representative, and
	// without this it would always be the session peak.
	static constexpr uint64_t GUARD_MS = 3000;

	// Weight of each new sample in the running baseline. Slow enough that a
	// spike does not drag the baseline up behind it and mask itself.
	static constexpr double BASELINE_WEIGHT = 0.05;

	// A peak must be this many times the baseline AND above the floor before
	// it is worth a line. The floor stops a quiet machine alarming on noise;
	// the multiple is what makes it work on any machine.
	static constexpr double ANOMALY_MULTIPLE = 4.0;
	static constexpr double MINIMUM_ALARM_PERCENT = 10.0;

	ProcessCpuUsageMeter()
	{
		SYSTEM_INFO systemInfo{};
		GetSystemInfo(&systemInfo);
		m_processors = systemInfo.dwNumberOfProcessors > 0 ?
			systemInfo.dwNumberOfProcessors : 1;
		m_startTick = GetTickCount64();
	}

	// Cheap to call at any rate; it only recomputes once per interval.
	// Returns true when a percentage is available.
	bool Sample()
	{
		const uint64_t nowTick = GetTickCount64();
		if (nowTick - m_startTick < GUARD_MS)
			return m_valid;
		if (m_lastTick != 0 && nowTick - m_lastTick < SAMPLE_INTERVAL_MS)
			return m_valid;

		FILETIME creation{};
		FILETIME exit{};
		FILETIME kernel{};
		FILETIME user{};
		if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit,
			&kernel, &user))
			return m_valid;

		const uint64_t busy = ToUint64(kernel) + ToUint64(user);
		if (m_lastTick == 0)
		{
			// First reading establishes the baseline only. Measuring against
			// process start would average in startup, which the guard exists
			// to exclude.
			m_lastTick = nowTick;
			m_lastBusy100ns = busy;
			return m_valid;
		}

		const uint64_t elapsedMs = nowTick - m_lastTick;
		const uint64_t busyDelta100ns = busy - m_lastBusy100ns;
		m_lastTick = nowTick;
		m_lastBusy100ns = busy;
		if (elapsedMs == 0)
			return m_valid;

		// 100ns units into ms, then against the wall time every processor
		// could have supplied.
		const double busyMs = static_cast<double>(busyDelta100ns) / 10000.0;
		const double availableMs =
			static_cast<double>(elapsedMs) * static_cast<double>(m_processors);
		// std::clamp is C++17; this project builds older.
		const double percent =
			(std::max)(0.0, (std::min)(100.0, 100.0 * busyMs / availableMs));

		m_currentPercent = percent;
		// Running baseline of what "normal" looks like on this machine, so a
		// spike can be judged as a multiple of it rather than against a fixed
		// number nobody can pick in advance. Measured on the rig: VP idles at
		// 4-5%, so any fixed threshold worth alarming on would be either
		// permanently silent or permanently noisy.
		m_averagePercent = m_valid ?
			m_averagePercent + (percent - m_averagePercent) * BASELINE_WEIGHT :
			percent;
		m_valid = true;
		if (percent > m_sessionPeakPercent)
		{
			m_sessionPeakPercent = percent;
			m_sessionPeakTick = nowTick;
			m_peakIsNew = true;
		}
		return true;
	}

	bool Valid() const { return m_valid; }
	double CurrentPercent() const { return m_currentPercent; }
	double AveragePercent() const { return m_averagePercent; }

	// True when the session peak is a genuine outlier against this machine's
	// own baseline - the test for "did the CPU ever spike", which a fixed
	// percentage cannot answer portably.
	bool SessionPeakIsAnomalous() const
	{
		return m_valid && m_sessionPeakPercent >= MINIMUM_ALARM_PERCENT &&
			m_sessionPeakPercent >= m_averagePercent * ANOMALY_MULTIPLE;
	}
	double SessionPeakPercent() const { return m_sessionPeakPercent; }
	DWORD Processors() const { return m_processors; }

	// True once, for each new session peak, so a caller can log the spike
	// without writing a line every second. Deliberately consuming: the caller
	// decides whether the new peak is worth a line at all.
	bool ConsumeNewPeak()
	{
		const bool wasNew = m_peakIsNew;
		m_peakIsNew = false;
		return wasNew;
	}

private:
	static uint64_t ToUint64(const FILETIME& value)
	{
		ULARGE_INTEGER converted{};
		converted.LowPart = value.dwLowDateTime;
		converted.HighPart = value.dwHighDateTime;
		return converted.QuadPart;
	}

	DWORD m_processors = 1;
	uint64_t m_startTick = 0;
	uint64_t m_lastTick = 0;
	uint64_t m_lastBusy100ns = 0;
	uint64_t m_sessionPeakTick = 0;
	double m_currentPercent = 0.0;
	double m_averagePercent = 0.0;
	double m_sessionPeakPercent = 0.0;
	bool m_valid = false;
	bool m_peakIsNew = false;
};
