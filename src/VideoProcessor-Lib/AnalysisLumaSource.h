#pragma once

#include <cstddef>
#include <cstdint>

#include <ColorSpace.h>
#include <VideoFrameEncoding.h>

// A renderer-independent, source-coordinate analysis contract. It never owns
// pixels: consumers may only request the small, bounded sample grids they need.
// RGB sources are decoded into 10-bit full-range luma/chroma evidence so P010
// consumers do not need a converted full-frame buffer.
enum class AnalysisLumaFormat
{
	P010,
	P210,
	NativeRgb,
	NativeYuv422,
};

struct AnalysisLumaSample
{
	uint16_t luma = 0;
	uint16_t chromaU = 512;
	uint16_t chromaV = 512;
};

struct AnalysisLumaSource
{
	const uint8_t* data = nullptr;
	size_t dataBytes = 0;
	int width = 0;
	int height = 0;
	size_t rowBytes = 0;
	size_t chromaRowBytes = 0;
	AnalysisLumaFormat format = AnalysisLumaFormat::P010;
	VideoFrameEncoding encoding = VideoFrameEncoding::UNKNOWN;
	ColorSpace colorspace = ColorSpace::UNKNOWN;
	uint64_t generation = 0;

	bool IsValid() const;
	bool Sample(int x, int y, AnalysisLumaSample& result) const;
};

const char* AnalysisLumaFormatName(const AnalysisLumaSource& source);
