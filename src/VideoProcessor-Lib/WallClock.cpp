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
	// The FILETIME structure contains a 64-bit value representing the number of 100-nanosecond intervals since January 1, 1601 (UTC).
	//  -- https://zetcode.com/gui/winapi/datetime/ (2020)
	FILETIME ft;

	// GetSystemTimePreciseAsFileTime provides sub-microsecond precision (Windows 8+)
	// Falls back to GetSystemTimeAsFileTime on older systems
	// "Retrieves the current system date and time with the highest possible level of precision (<1us)"
	//  -- https://docs.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemtimepreciseasfiletime
	GetSystemTimePreciseAsFileTime(&ft);

	timestamp_t t = (timestamp_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime;
	t -= 11644473600000000Ui64;  // start of the windows time (1601-01-01) to epoch (1970-01-01)

	return t;
}
