#pragma once

#include <cstdint>

#include <VideoConversionOverride.h>
#include <VideoFrameEncoding.h>

// A regular packed RGB layout is safe to hand to libplacebo's generic image
// uploader. Keeping this table separate makes byte order and bit placement
// independently testable from the D3D11 renderer.
struct AlphaNativeRgbLayout
{
	uint64_t masks[4]{};
	bool swapped = false;
	bool limitedRange = false;
	int bitDepth = 0;
	const char* label = nullptr;
};

inline bool GetAlphaNativeRgbLayout(VideoFrameEncoding encoding,
	AlphaNativeRgbLayout& layout)
{
	layout = {};
	switch (encoding)
	{
	case VideoFrameEncoding::BGRA_8BIT:
		layout.masks[0] = 0x00FF0000; // R in [B G R A]
		layout.masks[1] = 0x0000FF00;
		layout.masks[2] = 0x000000FF;
		layout.masks[3] = 0xFF000000;
		layout.bitDepth = 8;
		layout.label = "Native BGRA -> RGB";
		return true;
	case VideoFrameEncoding::ARGB_8BIT:
		layout.masks[0] = 0x0000FF00; // R in [A R G B]
		layout.masks[1] = 0x00FF0000;
		layout.masks[2] = 0xFF000000;
		layout.masks[3] = 0x000000FF;
		layout.bitDepth = 8;
		layout.label = "Native ARGB -> RGB";
		return true;
	case VideoFrameEncoding::R210:
		layout.masks[0] = 0x3FF00000; // [00 R10 G10 B10], big endian
		layout.masks[1] = 0x000FFC00;
		layout.masks[2] = 0x000003FF;
		layout.swapped = true;
		layout.limitedRange = true;
		layout.bitDepth = 10;
		layout.label = "Native r210 -> RGB";
		return true;
	case VideoFrameEncoding::R10b:
		layout.masks[0] = 0xFFC00000; // [R10 G10 B10 00], big endian
		layout.masks[1] = 0x003FF000;
		layout.masks[2] = 0x00000FFC;
		layout.swapped = true;
		layout.limitedRange = true;
		layout.bitDepth = 10;
		layout.label = "Native R10b -> RGB";
		return true;
	case VideoFrameEncoding::R10l:
		layout.masks[0] = 0xFFC00000; // [R10 G10 B10 00], little endian
		layout.masks[1] = 0x003FF000;
		layout.masks[2] = 0x00000FFC;
		layout.limitedRange = true;
		layout.bitDepth = 10;
		layout.label = "Native R10l -> RGB";
		return true;
	default:
		return false;
	}
}

inline bool AlphaCanUseNativeRgbUpload(VideoFrameEncoding encoding,
	bool dolbyVisionTunnel = false)
{
	AlphaNativeRgbLayout layout;
	// libplacebo's generic limited-RGB representation uses conventional
	// 10-bit 64-940 levels. DeckLink r210 has a distinct 64-960 interval, so
	// normalize it through the range-aware P010 formatter instead.
	return !dolbyVisionTunnel && encoding != VideoFrameEncoding::R210 &&
		GetAlphaNativeRgbLayout(encoding, layout);
}

inline bool AlphaUsesP210Ingress(VideoFrameEncoding encoding,
	VideoConversionOverride conversionOverride) noexcept
{
	return conversionOverride ==
			VideoConversionOverride::VIDEOCONVERSION_NONE &&
		(encoding == VideoFrameEncoding::UYVY ||
			encoding == VideoFrameEncoding::HDYC ||
			encoding == VideoFrameEncoding::V210 ||
			encoding == VideoFrameEncoding::P210);
}

// Bit position of a mask's lowest set bit; 0 for an empty mask.
inline int AlphaNativeRgbMaskShift(uint64_t mask) noexcept
{
	if (!mask)
		return 0;
	int shift = 0;
	while (!(mask & 1u))
	{
		mask >>= 1;
		++shift;
	}
	return shift;
}

// libplacebo's generic uploader matches a packed layout against the GPU's
// texture formats bit for bit, padding included. D3D11's one packed 10-bit
// format, R10G10B10A2, has its three components from bit 0 and the pad on top.
// The DeckLink 10-bit RGB words (R10l, R10b) put their two pad bits at the
// bottom, so no format matches them and the generic upload fails on every
// frame. Such a layout reports its low pad here; the renderer then uploads the
// words unchanged and rebuilds the components on the GPU. BGRA and ARGB have
// no low pad and use the generic upload.
inline int AlphaNativeRgbLowPadBits(const AlphaNativeRgbLayout& layout) noexcept
{
	return AlphaNativeRgbMaskShift(layout.masks[0] | layout.masks[1] |
		layout.masks[2] | layout.masks[3]);
}
