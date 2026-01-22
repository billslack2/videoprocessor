/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <refclock.h>
#include <ITimingClock.h>

#define DIRECTSHOW_TIMING_CLOCK_NAME TEXT("TimingClock")

 /**
  * DirectShow timing clock using pure hardware time.
  * ULTRA-SIMPLIFIED: Dumb hardware timestamp converter
  *
  * This class does ONE thing: convert hardware ticks to DirectShow 100ns ticks.
  * No rounding, no monotonic enforcement, no fancy logic.
  * Consumers (ALiveSourceVideoOutputPin) handle all timing logic.
  */
class DirectShowTimingClock : public CBaseReferenceClock
{
public:
	DirectShowTimingClock(ITimingClock& timingClock);
	virtual ~DirectShowTimingClock();

	// CBaseReferenceClock override - pure integer conversion
	REFERENCE_TIME GetPrivateTime() override;

private:
	ITimingClock& m_timingClock;
	const timingclocktime_t m_ticksPerSecond;
};