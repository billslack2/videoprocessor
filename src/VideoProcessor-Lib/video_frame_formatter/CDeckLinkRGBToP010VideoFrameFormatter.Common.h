/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, version 3.
 */

#pragma once
#include <cstdint>
#include <array>
#include <cstring>

namespace DeckLinkP010Detail
{
	struct RGBToYuvCoefficients
	{
		int32_t yR;
		int32_t yG;
		int32_t yB;
		int32_t cbR;
		int32_t cbG;
		int32_t cbB;
		int32_t crR;
		int32_t crG;
		int32_t crB;
	};

	struct LimitedRGBToYuvCoefficients
	{
		int32_t yR;
		int32_t yG;
		int32_t yB;
		int32_t cbR;
		int32_t cbG;
		int32_t cbB;
		int32_t crR;
		int32_t crG;
		int32_t crB;
	};

	// Coefficients use 16 fractional bits. Input and output are both 10-bit full-range values.
	constexpr RGBToYuvCoefficients BT709 = {
		13933, 46871, 4732,
		-7508, -25259, 32768,
		32768, -29763, -3005
	};
	constexpr RGBToYuvCoefficients BT2020 = {
		17218, 44444, 3886,
		-9147, -23621, 32768,
		32768, -30134, -2634
	};

	// Q20 coefficients map DeckLink's documented limited RGB intervals to
	// limited P010: Y 64-940 and Cb/Cr 64-960. r210 uses a distinct 64-960
	// input span; R10b/R10l use 64-940.
	constexpr LimitedRGBToYuvCoefficients BT709_R10 = {
		222927, 749942, 75707,
		-122880, -413378, 536258,
		536258, -487086, -49172
	};
	constexpr LimitedRGBToYuvCoefficients BT709_R210 = {
		217951, 733202, 74017,
		-120138, -404150, 524288,
		524288, -476214, -48074
	};
	constexpr LimitedRGBToYuvCoefficients BT2020_R10 = {
		275461, 710935, 62181,
		-149755, -386503, 536258,
		536258, -493128, -43130
	};
	constexpr LimitedRGBToYuvCoefficients BT2020_R210 = {
		269312, 695065, 60793,
		-146413, -377875, 524288,
		524288, -482120, -42168
	};
	// The largest absolute coefficient sum is 1,072,516. With the widest
	// possible post-offset input magnitude (959), every Q20 matrix sum fits in
	// signed 32-bit lanes, including legal out-of-nominal-range input codes.
	static_assert(1072516LL * 959LL < INT32_MAX,
		"Limited RGB AVX2 matrix requires wider intermediates");

	inline int32_t RoundQ20(int64_t value) noexcept
	{
		constexpr int64_t half = 1LL << 19;
		return value >= 0 ?
			static_cast<int32_t>((value + half) >> 20) :
			-static_cast<int32_t>(((-value) + half) >> 20);
	}

	inline uint16_t Clamp10(int32_t value) noexcept
	{
		return static_cast<uint16_t>(value < 0 ? 0 : value > 1023 ? 1023 : value);
	}

	extern const std::array<uint16_t, 4096> SCALE_12_TO_10;

	inline uint16_t Scale12To10(uint16_t value) noexcept
	{
		// The immutable 8 KiB table retains exact normalized round-to-nearest
		// while avoiding six constant divisions for every pair of R12 pixels.
		return SCALE_12_TO_10[value];
	}

	inline uint32_t ReadLittleEndian32(const uint8_t* source) noexcept
	{
		uint32_t value;
		memcpy(&value, source, sizeof(value));
		return value;
	}

	inline uint32_t ReadBigEndian32(const uint8_t* source) noexcept
	{
		return (static_cast<uint32_t>(source[0]) << 24) |
			(static_cast<uint32_t>(source[1]) << 16) |
			(static_cast<uint32_t>(source[2]) << 8) |
			static_cast<uint32_t>(source[3]);
	}

}
