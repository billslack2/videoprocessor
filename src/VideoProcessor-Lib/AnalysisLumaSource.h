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
	uint8_t rgbBitAlignment = 0; // SDR tunnel: 1 r210 high8, 2 low8 or packed RGB8
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
private:
    friend class ValidatedAnalysisLumaSampler;
    // Caller owns validation and coordinate bounds; conversion stays canonical.
    bool SampleValidated(int x,int y,AnalysisLumaSample& result) const;
};

// Immutable metadata snapshot; retaining backingOwner keeps owned frame pixels
// alive. Borrowed sources retain their original caller-owned lifetime contract.
class ValidatedAnalysisLumaSampler {
public:
    explicit ValidatedAnalysisLumaSampler(const AnalysisLumaSource& source)
        : source_(source),valid_(source_.IsValid()),
          planar_(source_.format==AnalysisLumaFormat::P010 || source_.format==AnalysisLumaFormat::P210 ||
              source_.format==AnalysisLumaFormat::DecodedDolbyVision422),
          chroma_(valid_ && planar_?source_.data+source_.rowBytes*size_t(source_.height):nullptr),
          chromaShift_(source_.format==AnalysisLumaFormat::P010?1:0) {}
    bool IsValid() const { return valid_; }
    bool Sample(int x,int y,AnalysisLumaSample& result) const {
        if(!valid_ || x<0 || y<0 || x>=source_.width || y>=source_.height)return false;
        if(!planar_)return source_.SampleValidated(x,y,result);
        const auto* luma=source_.data+size_t(y)*source_.rowBytes+size_t(x)*2;
        const auto* chroma=chroma_+size_t(y>>chromaShift_)*source_.chromaRowBytes+size_t(x/2)*4;
        // P010, P210 and decoded DV all use exact upper ten bits, no rounding.
        result.luma=Code(luma);result.chromaU=Code(chroma);result.chromaV=Code(chroma+2);
        return true;
    }
private:
    static uint16_t Code(const uint8_t* p) { return uint16_t((uint16_t(p[0])|(uint16_t(p[1])<<8))>>6); }
    AnalysisLumaSource source_;
    bool valid_,planar_;
    const uint8_t* chroma_;
    unsigned chromaShift_;
};

const char* AnalysisLumaFormatName(const AnalysisLumaSource& source);
