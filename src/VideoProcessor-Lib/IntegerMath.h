/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>
#include <intrin.h>

#pragma intrinsic(_umul128)

/**
 * 64-bit multiply with 64-bit divisor, preventing intermediate overflow.
 * Computes: (a * b) / div with 128-bit intermediate precision.
 * 
 * This is critical for timing calculations where:
 * - a = frame count or timestamp (can be very large)
 * - b = conversion factor (e.g., 10,000,000 for 100ns ticks)
 * - div = time scale (e.g., 24000 for 23.976 fps)
 * 
 * Without 128-bit intermediate precision, (a * b) would overflow int64_t.
 * 
 * @param a First multiplicand
 * @param b Second multiplicand  
 * @param div Divisor
 * @return Result of (a * b) / div, or 0 if div is zero
 */
static inline uint64_t U64_MulDiv(uint64_t a, uint64_t b, uint64_t div)
{
	if (div == 0) return 0;
	uint64_t hi = 0;
	uint64_t lo = _umul128(a, b, &hi);
	return _udiv128(hi, lo, div, nullptr);
}
