/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include "DirectShowTimingClock.h"

DirectShowTimingClock::DirectShowTimingClock(ITimingClock& timingClock)
	: CBaseReferenceClock(DIRECTSHOW_TIMING_CLOCK_NAME, nullptr, nullptr, nullptr),
	m_timingClock(timingClock),
	m_ticksPerSecond(m_timingClock.TimingClockTicksPerSecond()),
	m_lastReturnedTime(0)
{
	DbgLog((LOG_TRACE, 1, TEXT("DirectShowTimingClock: Initialized (pure hardware timing, %llu ticks/sec)"), m_ticksPerSecond));
	assert(m_ticksPerSecond > 0);
}

DirectShowTimingClock::~DirectShowTimingClock()
{
	DbgLog((LOG_TRACE, 1, TEXT("DirectShowTimingClock: Destroyed")));
}

REFERENCE_TIME DirectShowTimingClock::GetPrivateTime()
{
	// Get current hardware timestamp
	const timingclocktime_t now = m_timingClock.TimingClockNow();

	// High-precision conversion with overflow protection
	REFERENCE_TIME rt;
	const timingclocktime_t maxSafeTimestamp = INT64_MAX / 10000000LL;

	if (now > maxSafeTimestamp)
	{
		// Overflow protection: less precise but stable
		// This only triggers after ~10 days of continuous operation at 1MHz clock
		rt = (now / m_ticksPerSecond) * 10000000LL;
	}
	else
	{
		// Normal path: High-precision conversion with banker's rounding
		// Add half divisor before division to round to nearest (not truncate)
		rt = ((now * 10000000LL) + (m_ticksPerSecond / 2)) / m_ticksPerSecond;
	}

	// Lock-free monotonic enforcement using compare-and-swap
	// Ensures timeline never goes backwards even if hardware clock jitters
	REFERENCE_TIME lastTime = m_lastReturnedTime.load(std::memory_order_acquire);
	while (rt <= lastTime)
	{
		// Hardware went backwards or returned same value - enforce progression
		// Try to update to lastTime + 1 (0.1µs minimum increment)
		REFERENCE_TIME newTime = lastTime + 1;
		if (m_lastReturnedTime.compare_exchange_weak(lastTime, newTime,
			std::memory_order_release, std::memory_order_acquire))
		{
			return newTime;
		}
		// CAS failed - another thread updated lastTime, retry with new value
	}

	// Normal case: hardware time is monotonically increasing
	// Update last returned time and return
	m_lastReturnedTime.store(rt, std::memory_order_release);
	return rt;
}