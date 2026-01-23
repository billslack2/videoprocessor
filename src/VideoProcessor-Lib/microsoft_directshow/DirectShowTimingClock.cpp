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
    // 1) Read hardware tick counter
    const timingclocktime_t now = m_timingClock.TimingClockNow();

    // 2) Convert ticks -> 100ns REFERENCE_TIME safely (no overflow, keeps precision)
    //    rt = (now / tps) * 10,000,000 + (now % tps) * 10,000,000 / tps (rounded)
    const timingclocktime_t tps = m_ticksPerSecond; // ticks per second (must be > 0)
    const timingclocktime_t q = now / tps;         // whole seconds
    const timingclocktime_t r = now - (q * tps);   // remainder ticks (avoid % weirdness for some types)

    REFERENCE_TIME rt =
        (REFERENCE_TIME)(q * 10000000LL) +
        (REFERENCE_TIME)((r * 10000000LL + (tps / 2)) / tps); // round-to-nearest

    // 3) Monotonic, thread-safe, NON-DECREASING:
    //    - Never allow time to go backwards
    //    - Do NOT "invent" time when hardware repeats the same value
    REFERENCE_TIME prev = m_lastReturnedTime.load(std::memory_order_relaxed);

    for (;;)
    {
        REFERENCE_TIME out = rt;

        // Clamp only if hardware went backwards (allow equality)
        if (out < prev)
            out = prev;

        // Atomically publish out; if another thread won the race, prev gets updated and we retry
        if (m_lastReturnedTime.compare_exchange_weak(
            prev, out,
            std::memory_order_release,
            std::memory_order_relaxed))
        {
            return out;
        }
        // else: CAS failed; 'prev' now holds the newer value -> loop to clamp against it
    }
}
