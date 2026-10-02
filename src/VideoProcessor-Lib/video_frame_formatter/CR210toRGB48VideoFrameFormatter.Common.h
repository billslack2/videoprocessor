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
	inline uint16_t Align10To16(uint16_t value)
	{
		// Preserve documented limited-range codes and legal excursions in the
		// most-significant ten bits of the RGB48 component.
		return static_cast<uint16_t>(value << 6);
	}

	inline uint32_t ReadBigEndian32(const uint8_t* source)
	{
		return (static_cast<uint32_t>(source[0]) << 24) |
			(static_cast<uint32_t>(source[1]) << 16) |
			(static_cast<uint32_t>(source[2]) << 8) |
			static_cast<uint32_t>(source[3]);
	}

}
