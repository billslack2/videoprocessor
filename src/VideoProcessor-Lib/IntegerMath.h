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
 * Computes: floor((a * b) / div) with 128-bit intermediate precision.
 * 
 * This is critical for timing calculations where:
 * - a = frame count or timestamp (can be very large)
 * - b = conversion factor (e.g., 10,000,000 for 100ns ticks)
 * - div = time scale (e.g., 24000 for 23.976 fps)
 * 
 * WITHOUT 128-bit intermediate precision, (a * b) would overflow int64_t.
 * 
 * IMPORTANT: This function TRUNCATES (does not round).
 * For timestamp sequences, this is correct because each timestamp is calculated
 * independently from frame 0, so truncation error does NOT accumulate.
 * 
 * @param a First multiplicand
 * @param b Second multiplicand  
 * @param div Divisor
 * @return floor((a * b) / div), or 0 if div is zero
 */
static inline uint64_t U64_MulDiv(uint64_t a, uint64_t b, uint64_t div)
{
	if (div == 0) return 0;
	uint64_t hi = 0;
	uint64_t lo = _umul128(a, b, &hi);
	return _udiv128(hi, lo, div, nullptr);
}

/**
 * 64-bit multiply with 64-bit divisor, returning both quotient and remainder.
 * Computes: (a * b) / div with 128-bit intermediate precision.
 * 
 * The remainder can be used for sub-tick precision tracking or error analysis.
 * Use this when you need to know how much precision was lost in the division.
 * 
 * @param a First multiplicand
 * @param b Second multiplicand  
 * @param div Divisor
 * @param remainder Pointer to receive remainder (can be nullptr if not needed)
 * @return floor((a * b) / div), or 0 if div is zero
 */
static inline uint64_t U64_MulDivRemainder(uint64_t a, uint64_t b, uint64_t div, uint64_t* remainder)
{
	if (div == 0)
	{
		if (remainder) *remainder = 0;
		return 0;
	}
	
	uint64_t hi = 0;
	uint64_t lo = _umul128(a, b, &hi);
	return _udiv128(hi, lo, div, remainder);
}
