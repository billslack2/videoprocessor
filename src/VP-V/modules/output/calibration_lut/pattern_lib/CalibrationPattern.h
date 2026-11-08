// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration: the patch the VP renderer shows, shared by the host
// (the calibration session) and the renderer DLL (IVideoRenderer::
// SetCalibrationPattern / GetCalibrationOutput, plugin API 39; 36 on its branch).
//
// The renderer writes the codes straight into its own swapchain backbuffer,
// as raw integers in the backbuffer's format: libplacebo's shading - tone
// and gamut mapping, the calibration LUT, deband, the sigma-delta and
// libplacebo's dither - is not applied to the patch, and the output
// pipeline (swapchain format, range, DXGI colour space, HDR signalling,
// refresh) stays exactly as VP configured it for video. So a patch is the
// exact code the display receives; BTB / WTW codes reach the display as
// they are. The sigma-delta itself is untouched: it simply does not run for
// the patch frames, and runs again from the first video frame after them.
//
// Header-only and POD: the layout crosses the renderer DLL boundary
// (VP_LIBPLACEBO_PLUGIN_API_VERSION guards it).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Calibration
{
	enum class SignalTransfer : int32_t
	{
		Sdr = 0,
		Pq = 1,
		Hlg = 2,
	};

	// What the renderer's output is, read before any patch is chosen.
	struct PatternOutputInfo
	{
		bool available = false;          // a renderer is running and can show patches
		uint32_t containerBits = 0;      // swapchain bits per channel (8 or 10; 16 = float, refused)
		uint32_t codeBits = 0;           // the link / dither target (8 in a 10-bit container = 8-in-10)
		bool limitedRange = true;
		SignalTransfer transfer = SignalTransfer::Sdr;
		bool bt2020 = false;             // the container's primaries (signalled to the display)
		uint32_t width = 0, height = 0;  // the backbuffer
		double refreshHz = 0.0;          // measured by the renderer (0 = not known)
		uint64_t presentedGeneration = 0;// the last pattern generation presented
		uint64_t presentsOfGeneration = 0;
		// PatternReadback: the backbuffer read back once per generation (10-bit
		// swapchains), container codes at the centre and at a corner.
		uint64_t readbackGeneration = 0;
		uint16_t readbackCenter[3] = { 0, 0, 0 };
		uint16_t readbackCorner[3] = { 0, 0, 0 };
		char reason[256] = {};           // why patterns are unavailable
		// Phase 2, HDR (PQ) verify: VP's own tone mapping for the source last
		// rendered, sampled by the renderer with libplacebo's tone curve
		// (pl_tone_map_sample with the frame's colour-map parameters), and the
		// HDR metadata VP signals to the display. toneCurveCount 0 = not known
		// (no video frame yet, or an SDR output). Nits in, nits out, rising.
		// The layout change is a plugin ABI change: the API number stays at
		// phase 1's value on this branch and is renumbered at merge.
		static constexpr uint32_t ToneCurveMax = 64;
		uint32_t toneCurveCount = 0;
		float toneCurveSourceNits[ToneCurveMax] = {};
		float toneCurveOutputNits[ToneCurveMax] = {};
		float sourcePeakNits = 0.0f;     // the source's peak as libplacebo infers it
		float targetPeakNits = 0.0f;     // VP's tone-mapping target (the display peak VP maps to)
		float targetBlackNits = 0.0f;
		bool toneMappingActive = false;  // false: VP passes the source through (noop)
		char toneFunction[32] = {};      // libplacebo's tone-mapping function name
		// What VP sends (the target colour space's HDR metadata).
		float sentMaxLuma = 0.0f, sentMinLuma = 0.0f, sentMaxCll = 0.0f, sentMaxFall = 0.0f;
	};

	// PatternRequest::flags
	constexpr uint32_t PatternReadback = 1;   // verify the codes in the backbuffer (tests; costs a full-frame copy)

	struct PatternRequest
	{
		bool active = false;             // false = back to video
		uint64_t generation = 0;         // raised by the session for every new patch
		uint16_t patch[3] = { 0, 0, 0 }; // container codes
		uint16_t background[3] = { 0, 0, 0 };
		float windowAreaPercent = 100.0f;// share of the screen area; 100 = full field
		uint32_t flags = 0;
	};

	struct PixelRect
	{
		int left = 0, top = 0, right = 0, bottom = 0;
		int Width() const { return right - left; }
		int Height() const { return bottom - top; }
	};

	// A centred window covering areaPercent of the surface with the surface's
	// aspect ratio (any resolution; nothing assumed). Even offsets keep a 4:2:x
	// chroma pairing intact if a display subsamples. At least 2 x 2 pixels.
	inline PixelRect PatchWindowRect(int width, int height, double areaPercent)
	{
		PixelRect r;
		if (width <= 0 || height <= 0) return r;
		const double area = (std::min)((std::max)(areaPercent, 0.01), 100.0) / 100.0;
		if (area >= 0.9999)
		{
			r.right = width;
			r.bottom = height;
			return r;
		}
		const double scale = std::sqrt(area);
		int w = static_cast<int>(std::lround(width * scale));
		int h = static_cast<int>(std::lround(height * scale));
		w = (std::max)(2, (std::min)(width, w & ~1));
		h = (std::max)(2, (std::min)(height, h & ~1));
		r.left = ((width - w) / 2) & ~1;
		r.top = ((height - h) / 2) & ~1;
		r.right = (std::min)(width, r.left + w);
		r.bottom = (std::min)(height, r.top + h);
		return r;
	}
}
