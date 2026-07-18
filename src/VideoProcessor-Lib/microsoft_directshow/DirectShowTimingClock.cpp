/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */


#include <pch.h>

#include "DirectShowTimingClock.h"


DirectShowTimingClock::DirectShowTimingClock(ITimingClock& timingClock, HRESULT& result) :
	CBaseReferenceClock(DIRECTSHOW_TIMING_CLOCK_NAME, nullptr, &result, nullptr),
	m_timingClock(timingClock),
	m_ticksPerSecond(m_timingClock.TimingClockTicksPerSecond()),
	m_lastReturnedTime(0)
{
	DbgLog((LOG_TRACE, 1, TEXT("DirectShowTimingClock::DirectShowTimingClock()")));

	assert(m_ticksPerSecond > 0);
	if (m_ticksPerSecond <= 0)
		result = E_INVALIDARG;
}


DirectShowTimingClock::~DirectShowTimingClock()
{
	DbgLog((LOG_TRACE, 1, TEXT("DirectShowTimingClock::~DirectShowTimingClock()")));
}


REFERENCE_TIME DirectShowTimingClock::GetPrivateTime()
{
	try
	{
		const timingclocktime_t now = m_timingClock.TimingClockNow();
		const REFERENCE_TIME rt = (now * 10000000) / m_ticksPerSecond;
		m_lastReturnedTime.store(rt, std::memory_order_relaxed);
		return rt;
	}
	catch (...)
	{
		// IReferenceClock::GetTime is a COM boundary and must not allow a
		// hardware/API exception to escape onto a DirectShow scheduler thread.
		// Holding the last valid value makes GetTime return S_FALSE until the
		// hardware clock is available again.
		return m_lastReturnedTime.load(std::memory_order_relaxed);
	}
}
