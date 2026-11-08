// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <dxgiformat.h>

#include <algorithm>
#include <cstdint>
#include <string>

// The pure decisions of the renderer pattern host (CalibrationPatternHost.h), header-only so the
// CPU tests cover them (tests/CalibrationPatternHostTests.cpp). No libplacebo, no Windows beyond
// the DXGI format enum.
namespace CalibrationPatternHost
{
	inline uint32_t ContainerBits(DXGI_FORMAT format)
	{
		return format == DXGI_FORMAT_R10G10B10A2_UNORM ? 10u :
			format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 16u :
			format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UNORM ? 8u : 0u;
	}

	// The link / dither target: display_bit_depth 8 or 10 limits an 8- or 10-bit container
	// (8 in a 10-bit container = 8-in-10 codes); "auto" follows the container.
	inline uint32_t CodeBits(uint32_t containerBits, const std::string& displayBitDepth)
	{
		if ((displayBitDepth == "8" || displayBitDepth == "10") && (containerBits == 8 || containerBits == 10))
			return (std::min)(containerBits, static_cast<uint32_t>(displayBitDepth == "8" ? 8 : 10));
		return containerBits;
	}

	// The maximum code of the swapchain format a patch is written in (0 = patches unsupported:
	// a float swapchain has no exact integer codes).
	inline double PatchMaxCode(DXGI_FORMAT format)
	{
		return format == DXGI_FORMAT_R10G10B10A2_UNORM ? 1023.0 :
			format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM ? 255.0 : 0.0;
	}

	// The output query refreshes the render-thread format facts under renderMutex (try-lock with
	// 2 ms sleeps). While patches show, the render loop holds renderMutex across each vsync-paced
	// present and re-locks at once, so once the format is cached the query makes one try-lock and
	// otherwise answers from the cache (the format does not change while video is paused unless the
	// swapchain is rebuilt). The presented-generation counters are atomics and stay live.
	inline int FreshOutputAttempts(bool patternsActive, uint32_t cachedContainerBits)
	{
		return patternsActive && cachedContainerBits != 0 ? 1 : 100;
	}
}
