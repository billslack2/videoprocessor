#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <ColorSpace.h>
#include <VideoFrameEncoding.h>

// A renderer-independent, source-coordinate analysis contract. Legacy raw
// views can borrow pixels or retain an immutable per-frame backing owner.
// The sampler retains beta 10-bit rounding and chroma footprints.
enum class AnalysisLumaFormat
{
	P010,
	P210,
	NativeRgb,
	NativeYuv422,
	DecodedDolbyVision422,
	SdrTunnelRgb,
};

// Encoded sample levels, independent of the format-specific 10-bit sampler.
// Unknown preserves the historical interpretation of manually built sources.
enum class AnalysisLumaRange : uint8_t
{
	Unknown,
	Limited,
	Full,
};

struct AnalysisLumaSample
{
	uint16_t luma = 0;
	uint16_t chromaU = 512;
	uint16_t chromaV = 512;
};

struct AnalysisLumaRowContent
{
    int samples = 0;
    int left = 0, right = 0;
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
	uint8_t rgbBitAlignment = 0; // SDR tunnel: 1 high, 2 low eight bits
	std::shared_ptr<const void> backingOwner;
	AnalysisLumaRange range = AnalysisLumaRange::Unknown;

	bool IsValid() const;
	bool Sample(int x, int y, AnalysisLumaSample& result) const;

    // Exact Sample().luma threshold/count/span on a strided row. Validate once
    // and avoid fetching/converting chroma when the consumer only needs luma.
    bool MeasureLumaRowContent(int y, int x0, int x1, int step,
        uint16_t threshold, AnalysisLumaRowContent& result) const;

    // Checks every native pixel in a row using Sample's exact code domain.
    // False means invalid input/read failure; a valid foreground hit returns
    // true with withinLimits=false and counts the first rejected pixel.
    bool RowWithinLimits(int y, uint16_t lumaMax, uint16_t chromaDeviation,
        bool& withinLimits, size_t& samplesChecked) const;
};

const char* AnalysisLumaFormatName(const AnalysisLumaSource& source);
