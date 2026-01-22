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
	m_ticksPerSecond(m_timingClock.TimingClockTicksPerSecond())
{
	DbgLog((LOG_TRACE, 1, TEXT("DirectShowTimingClock: Initialized (pure hardware passthrough, %llu ticks/sec)"), m_ticksPerSecond));
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

	// Pure integer conversion: hardware ticks → 100ns DirectShow ticks
	// Simple truncating division - deterministic and predictable
	// Any monotonic enforcement or rounding is done by the consumer (ALiveSourceVideoOutputPin)
	const REFERENCE_TIME rt = (now * 10000000LL) / m_ticksPerSecond;

	return rt;
}