#pragma once

#include <cstdint>

#include <VideoFrameEncoding.h>

// A regular packed RGB layout is safe to hand to libplacebo's generic image
// uploader. Keeping this table separate makes byte order and bit placement
// independently testable from the D3D11 renderer.
struct AlphaNativeRgbLayout
{
	uint64_t masks[4]{};
	bool swapped = false;
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
		layout.bitDepth = 10;
		layout.label = "Native r210 -> RGB";
		return true;
	case VideoFrameEncoding::R10b:
		layout.masks[0] = 0xFFC00000; // [R10 G10 B10 00], big endian
		layout.masks[1] = 0x003FF000;
		layout.masks[2] = 0x00000FFC;
		layout.swapped = true;
		layout.bitDepth = 10;
		layout.label = "Native R10b -> RGB";
		return true;
	case VideoFrameEncoding::R10l:
		layout.masks[0] = 0xFFC00000; // [R10 G10 B10 00], little endian
		layout.masks[1] = 0x003FF000;
		layout.masks[2] = 0x00000FFC;
		layout.bitDepth = 10;
		layout.label = "Native R10l -> RGB";
		return true;
	default:
		return false;
	}
}
