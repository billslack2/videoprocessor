/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, version 3.
 */

#pragma once
#include <cstdint>

namespace
{
	constexpr uint32_t PIXELS_PER_BLOCK = 8;
	constexpr uint32_t BYTES_PER_BLOCK = 36;

	inline uint16_t Expand12To16(uint16_t value)
	{
		// Bit replication maps both endpoints exactly: 0x000 -> 0x0000, 0xFFF -> 0xFFFF.
		return static_cast<uint16_t>((value << 4) | (value >> 8));
	}

	inline uint8_t Byte(const uint8_t* source, uint32_t word, uint32_t byte)
	{
		return source[word * 4 + byte];
	}

	inline uint16_t LowNibble(const uint8_t* source, uint32_t word, uint32_t byte)
	{
		return static_cast<uint16_t>(Byte(source, word, byte) & 0x0F);
	}

	inline uint16_t HighNibble(const uint8_t* source, uint32_t word, uint32_t byte)
	{
		return static_cast<uint16_t>(Byte(source, word, byte) >> 4);
	}

	inline void StoreRGB48(uint16_t*& destination, uint16_t red, uint16_t green, uint16_t blue)
	{
		*destination++ = Expand12To16(red);
		*destination++ = Expand12To16(green);
		*destination++ = Expand12To16(blue);
	}

	inline void ConvertBlockScalar(const uint8_t* source, uint16_t* destination)
	{
		// Mapping from the DeckLink SDK R12B table (eight pixels / nine 32-bit words).
		StoreRGB48(destination,
			Byte(source, 0, 3) | (LowNibble(source, 0, 2) << 8),
			HighNibble(source, 0, 2) | (static_cast<uint16_t>(Byte(source, 0, 1)) << 4),
			Byte(source, 0, 0) | (LowNibble(source, 1, 3) << 8));
		StoreRGB48(destination,
			HighNibble(source, 1, 3) | (static_cast<uint16_t>(Byte(source, 1, 2)) << 4),
			Byte(source, 1, 1) | (LowNibble(source, 1, 0) << 8),
			HighNibble(source, 1, 0) | (static_cast<uint16_t>(Byte(source, 2, 3)) << 4));
		StoreRGB48(destination,
			Byte(source, 2, 2) | (LowNibble(source, 2, 1) << 8),
			HighNibble(source, 2, 1) | (static_cast<uint16_t>(Byte(source, 2, 0)) << 4),
			Byte(source, 3, 3) | (LowNibble(source, 3, 2) << 8));
		StoreRGB48(destination,
			HighNibble(source, 3, 2) | (static_cast<uint16_t>(Byte(source, 3, 1)) << 4),
			Byte(source, 3, 0) | (LowNibble(source, 4, 3) << 8),
			HighNibble(source, 4, 3) | (static_cast<uint16_t>(Byte(source, 4, 2)) << 4));
		StoreRGB48(destination,
			Byte(source, 4, 1) | (LowNibble(source, 4, 0) << 8),
			HighNibble(source, 4, 0) | (static_cast<uint16_t>(Byte(source, 5, 3)) << 4),
			Byte(source, 5, 2) | (LowNibble(source, 5, 1) << 8));
		StoreRGB48(destination,
			HighNibble(source, 5, 1) | (static_cast<uint16_t>(Byte(source, 5, 0)) << 4),
			Byte(source, 6, 3) | (LowNibble(source, 6, 2) << 8),
			HighNibble(source, 6, 2) | (static_cast<uint16_t>(Byte(source, 6, 1)) << 4));
		StoreRGB48(destination,
			Byte(source, 6, 0) | (LowNibble(source, 7, 3) << 8),
			HighNibble(source, 7, 3) | (static_cast<uint16_t>(Byte(source, 7, 2)) << 4),
			Byte(source, 7, 1) | (LowNibble(source, 7, 0) << 8));
		StoreRGB48(destination,
			HighNibble(source, 7, 0) | (static_cast<uint16_t>(Byte(source, 8, 3)) << 4),
			Byte(source, 8, 2) | (LowNibble(source, 8, 1) << 8),
			HighNibble(source, 8, 1) | (static_cast<uint16_t>(Byte(source, 8, 0)) << 4));
	}

}
