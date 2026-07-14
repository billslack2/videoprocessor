/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include "WallClock.h"


timestamp_t GetWallClockTime()
{
	// Use QueryPerformanceCounter for high-resolution timing (nanosecond precision)
	// instead of GetSystemTimeAsFileTime which has ~15-16ms resolution
	static LARGE_INTEGER frequency = { 0 };
	if (frequency.QuadPart == 0)
	{
		QueryPerformanceFrequency(&frequency);
	}

	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);

	// Convert to 100ns increments (ticks) for compatibility with existing code
	// counter.QuadPart / frequency.QuadPart = seconds
	// seconds * 10,000,000 = 100ns ticks
	return (counter.QuadPart * 10000000) / frequency.QuadPart;
}
