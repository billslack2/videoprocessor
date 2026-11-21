#include "pch.h"

#include "AnalysisLumaSource.h"
#include <DolbyVisionTunnelInput.h>

#include <algorithm>
#include <limits>
#include <emmintrin.h>

namespace
{
	bool CheckedMultiply(size_t left, size_t right, size_t& result)
	{
		if (left != 0 && right > std::numeric_limits<size_t>::max() / left)
			return false;
		result = left * right;
		return true;
	}

	uint16_t P010Code(const uint8_t* address)
	{
		return static_cast<uint16_t>((static_cast<uint16_t>(address[0]) |
			(static_cast<uint16_t>(address[1]) << 8)) >> 6);
	}

	// DV output stores exact 12-bit codes in the high bits of 16-bit lanes.
	// The established detector contract uses the upper ten bits (no rounding).
	uint16_t DecodedDolbyVisionCode(const uint8_t* address)
	{
		return P010Code(address);
	}

	uint32_t ReadBigEndian32(const uint8_t* address)
	{
		return (static_cast<uint32_t>(address[0]) << 24) |
			(static_cast<uint32_t>(address[1]) << 16) |
			(static_cast<uint32_t>(address[2]) << 8) |
			static_cast<uint32_t>(address[3]);
	}

	uint32_t ReadLittleEndian32(const uint8_t* address)
	{
		return static_cast<uint32_t>(address[0]) |
			(static_cast<uint32_t>(address[1]) << 8) |
			(static_cast<uint32_t>(address[2]) << 16) |
			(static_cast<uint32_t>(address[3]) << 24);
	}

	uint16_t Expand8To10(uint8_t value)
	{
		return static_cast<uint16_t>((static_cast<uint32_t>(value) * 1023 + 127) / 255);
	}

	bool ReadNativeYuv422(const AnalysisLumaSource& source, int x, int y,
		AnalysisLumaSample& result)
	{
		const uint8_t* row = source.data +
			static_cast<size_t>(y) * source.rowBytes;
		if (source.encoding == VideoFrameEncoding::UYVY ||
			source.encoding == VideoFrameEncoding::HDYC)
		{
			const uint8_t* pair = row + static_cast<size_t>(x / 2) * 4;
			result.chromaU = static_cast<uint16_t>(pair[0]) << 2;
			result.luma = static_cast<uint16_t>(pair[(x & 1) ? 3 : 1]) << 2;
			result.chromaV = static_cast<uint16_t>(pair[2]) << 2;
			return true;
		}
		if (source.encoding != VideoFrameEncoding::V210)
			return false;

		const uint8_t* group = row + static_cast<size_t>(x / 6) * 16;
		const uint32_t words[4] = {
			ReadLittleEndian32(group),
			ReadLittleEndian32(group + 4),
			ReadLittleEndian32(group + 8),
			ReadLittleEndian32(group + 12)
		};
		auto component = [&words](int word, int shift)
		{
			return static_cast<uint16_t>((words[word] >> shift) & 0x3ff);
		};
		switch (x % 6)
		{
		case 0:
			result = { component(0, 10), component(0, 0), component(0, 20) };
			break;
		case 1:
			result = { component(1, 0), component(0, 0), component(0, 20) };
			break;
		case 2:
			result = { component(1, 20), component(1, 10), component(2, 0) };
			break;
		case 3:
			result = { component(2, 10), component(1, 10), component(2, 0) };
			break;
		case 4:
			result = { component(3, 0), component(2, 20), component(3, 10) };
			break;
		default:
			result = { component(3, 20), component(2, 20), component(3, 10) };
			break;
		}
		return true;
	}

	uint16_t Clamp10(int value)
	{
		return static_cast<uint16_t>(std::max(0, std::min(1023, value)));
	}

	struct LimitedRgbCoefficients
	{
		int32_t yR, yG, yB;
		int32_t cbR, cbG, cbB;
		int32_t crR, crG, crB;
	};

	constexpr LimitedRgbCoefficients BT709_R10 = {
		222927, 749942, 75707, -122880, -413378, 536258,
		536258, -487086, -49172
	};
	constexpr LimitedRgbCoefficients BT709_R210 = {
		217951, 733202, 74017, -120138, -404150, 524288,
		524288, -476214, -48074
	};
	constexpr LimitedRgbCoefficients BT2020_R10 = {
		275461, 710935, 62181, -149755, -386503, 536258,
		536258, -493128, -43130
	};
	constexpr LimitedRgbCoefficients BT2020_R210 = {
		269312, 695065, 60793, -146413, -377875, 524288,
		524288, -482120, -42168
	};

	int RoundQ20(int64_t value)
	{
		constexpr int64_t half = 1LL << 19;
		return value >= 0 ? static_cast<int>((value + half) >> 20) :
			-static_cast<int>(((-value) + half) >> 20);
	}

	uint16_t Round12To10(uint16_t value)
	{
		return static_cast<uint16_t>(
			(static_cast<uint32_t>(value) * 1023U + 2047U) / 4095U);
	}

	bool ReadR12BPixel(const uint8_t* source, int x,
		uint16_t& red, uint16_t& green, uint16_t& blue)
	{
		const uint8_t* block = source + static_cast<size_t>(x / 8) * 36;
		auto byte = [block](int word, int index) noexcept
		{
			return block[word * 4 + index];
		};
		auto low = [&byte](int word, int index) noexcept
		{
			return static_cast<uint16_t>(byte(word, index) & 0x0f);
		};
		auto high = [&byte](int word, int index) noexcept
		{
			return static_cast<uint16_t>(byte(word, index) >> 4);
		};
		switch (x % 8)
		{
		case 0:
			red = byte(0, 3) | (low(0, 2) << 8);
			green = high(0, 2) | (static_cast<uint16_t>(byte(0, 1)) << 4);
			blue = byte(0, 0) | (low(1, 3) << 8);
			break;
		case 1:
			red = high(1, 3) | (static_cast<uint16_t>(byte(1, 2)) << 4);
			green = byte(1, 1) | (low(1, 0) << 8);
			blue = high(1, 0) | (static_cast<uint16_t>(byte(2, 3)) << 4);
			break;
		case 2:
			red = byte(2, 2) | (low(2, 1) << 8);
			green = high(2, 1) | (static_cast<uint16_t>(byte(2, 0)) << 4);
			blue = byte(3, 3) | (low(3, 2) << 8);
			break;
		case 3:
			red = high(3, 2) | (static_cast<uint16_t>(byte(3, 1)) << 4);
			green = byte(3, 0) | (low(4, 3) << 8);
			blue = high(4, 3) | (static_cast<uint16_t>(byte(4, 2)) << 4);
			break;
		case 4:
			red = byte(4, 1) | (low(4, 0) << 8);
			green = high(4, 0) | (static_cast<uint16_t>(byte(5, 3)) << 4);
			blue = byte(5, 2) | (low(5, 1) << 8);
			break;
		case 5:
			red = high(5, 1) | (static_cast<uint16_t>(byte(5, 0)) << 4);
			green = byte(6, 3) | (low(6, 2) << 8);
			blue = high(6, 2) | (static_cast<uint16_t>(byte(6, 1)) << 4);
			break;
		case 6:
			red = byte(6, 0) | (low(7, 3) << 8);
			green = high(7, 3) | (static_cast<uint16_t>(byte(7, 2)) << 4);
			blue = byte(7, 1) | (low(7, 0) << 8);
			break;
		default:
			red = high(7, 0) | (static_cast<uint16_t>(byte(8, 3)) << 4);
			green = byte(8, 2) | (low(8, 1) << 8);
			blue = high(8, 1) | (static_cast<uint16_t>(byte(8, 0)) << 4);
			break;
		}
		red = Round12To10(red);
		green = Round12To10(green);
		blue = Round12To10(blue);
		return true;
	}

	bool ReadRgb10(const AnalysisLumaSource& source, int x, int y,
		uint16_t& red, uint16_t& green, uint16_t& blue)
	{
		const uint8_t* row = source.data + static_cast<size_t>(y) * source.rowBytes;
		const uint8_t* pixel = row + static_cast<size_t>(x) * 4;
		switch (source.encoding)
		{
		case VideoFrameEncoding::BGRA_8BIT:
			blue = Expand8To10(pixel[0]);
			green = Expand8To10(pixel[1]);
			red = Expand8To10(pixel[2]);
			return true;
		case VideoFrameEncoding::ARGB_8BIT:
			red = Expand8To10(pixel[1]);
			green = Expand8To10(pixel[2]);
			blue = Expand8To10(pixel[3]);
			return true;
		case VideoFrameEncoding::R210:
		{
			const uint32_t word = ReadBigEndian32(pixel);
			red = static_cast<uint16_t>((word >> 20) & 0x3ff);
			green = static_cast<uint16_t>((word >> 10) & 0x3ff);
			blue = static_cast<uint16_t>(word & 0x3ff);
			return true;
		}
		case VideoFrameEncoding::R10b:
		{
			const uint32_t word = ReadBigEndian32(pixel);
			red = static_cast<uint16_t>((word >> 22) & 0x3ff);
			green = static_cast<uint16_t>((word >> 12) & 0x3ff);
			blue = static_cast<uint16_t>((word >> 2) & 0x3ff);
			return true;
		}
		case VideoFrameEncoding::R10l:
		{
			const uint32_t word = ReadLittleEndian32(pixel);
			red = static_cast<uint16_t>((word >> 22) & 0x3ff);
			green = static_cast<uint16_t>((word >> 12) & 0x3ff);
			blue = static_cast<uint16_t>((word >> 2) & 0x3ff);
			return true;
		}
		case VideoFrameEncoding::R12L:
		{
			const uint8_t* pair = row + static_cast<size_t>(x / 2) * 9;
			if ((x & 1) == 0)
			{
				red = static_cast<uint16_t>(pair[0] | ((pair[1] & 0x0f) << 8));
				green = static_cast<uint16_t>((pair[1] >> 4) | (pair[2] << 4));
				blue = static_cast<uint16_t>(pair[3] | ((pair[4] & 0x0f) << 8));
			}
			else
			{
				red = static_cast<uint16_t>((pair[4] >> 4) | (pair[5] << 4));
				green = static_cast<uint16_t>(pair[6] | ((pair[7] & 0x0f) << 8));
				blue = static_cast<uint16_t>((pair[7] >> 4) | (pair[8] << 4));
			}
			red = Round12To10(red);
			green = Round12To10(green);
			blue = Round12To10(blue);
			return true;
		}
		case VideoFrameEncoding::R12B:
			return ReadR12BPixel(row, x, red, green, blue);
		default:
			return false;
		}
	}
}

bool AnalysisLumaSource::IsValid() const
{
	if (!data || width < 1 || height < 1 || rowBytes == 0)
		return false;
	size_t requiredBytes = 0;
	if (!CheckedMultiply(rowBytes, static_cast<size_t>(height), requiredBytes) ||
		requiredBytes > dataBytes)
		return false;
	if (format == AnalysisLumaFormat::SdrTunnelRgb)
		return DolbyVisionTunnelEncodingSupported(encoding) &&
			(rgbBitAlignment == 2 ||
				(encoding == VideoFrameEncoding::R210 && rgbBitAlignment == 1)) &&
			rowBytes >= static_cast<size_t>(width) * 4;
	if (format == AnalysisLumaFormat::NativeRgb)
	{
		if (encoding == VideoFrameEncoding::R12B ||
			encoding == VideoFrameEncoding::R12L)
			return (width % 8) == 0 &&
				rowBytes >= static_cast<size_t>(width) * 36 / 8;
		return rowBytes >= static_cast<size_t>(width) * 4;
	}
	if (format == AnalysisLumaFormat::NativeYuv422)
	{
		if ((width & 1) != 0)
			return false;
		if (encoding == VideoFrameEncoding::UYVY ||
			encoding == VideoFrameEncoding::HDYC)
			return rowBytes >= static_cast<size_t>(width) * 2;
		return encoding == VideoFrameEncoding::V210 &&
			rowBytes >= ((static_cast<size_t>(width) + 5) / 6) * 16;
	}
	if ((width & 1) != 0 ||
		(format == AnalysisLumaFormat::P010 && (height & 1) != 0) ||
		rowBytes < static_cast<size_t>(width) * 2 ||
		chromaRowBytes < static_cast<size_t>(width) * 2)
		return false;
	size_t chromaBytes = 0;
	const size_t chromaHeight = format == AnalysisLumaFormat::P210 ||
		format == AnalysisLumaFormat::DecodedDolbyVision422 ?
		static_cast<size_t>(height) : static_cast<size_t>(height / 2);
	return CheckedMultiply(chromaRowBytes, chromaHeight, chromaBytes) &&
		requiredBytes <= std::numeric_limits<size_t>::max() - chromaBytes &&
		requiredBytes + chromaBytes <= dataBytes;
}

bool AnalysisLumaSource::MeasureLumaRowContent(int y, int x0, int x1, int step,
    uint16_t threshold, AnalysisLumaRowContent& result) const
{
    result = {};
    if (!IsValid() || y < 0 || y >= height || x0 < 0 || x1 > width ||
        x0 >= x1 || step <= 0 || threshold > 1023)
        return false;
    result.left = width;
    const auto record = [&](int x, uint16_t code) {
        if (code <= threshold) return;
        ++result.samples;
        result.left = (std::min)(result.left, x);
        result.right = x;
    };
    if (format == AnalysisLumaFormat::P010 || format == AnalysisLumaFormat::P210 ||
        format == AnalysisLumaFormat::DecodedDolbyVision422) {
        const auto* row = data + static_cast<size_t>(y) * rowBytes;
        // All three formats use the same upper ten bits, including exact DV
        // truncation. Keep byte reads valid for unaligned/padded source rows.
        for (int x = x0; x < x1; ) {
            record(x, P010Code(row + static_cast<size_t>(x) * 2));
            if (step >= x1 - x) break;
            x += step;
        }
    } else {
        for (int x = x0; x < x1; ) {
            AnalysisLumaSample sample;
            if (!Sample(x, y, sample)) return false;
            record(x, sample.luma);
            if (step >= x1 - x) break;
            x += step;
        }
    }
    return true;
}

bool AnalysisLumaSource::FindDetachedMatteRowDetail(int y,int boundary,int matte,int& x,int startX) const
{
    x=startX;
    if (!IsValid() || startX<0 || startX>width || y<0 || y>=height || boundary<0 || boundary>=height ||
        std::abs(y-boundary)<4 || matte<0 || matte>1023 ||
        (format!=AnalysisLumaFormat::P010 && format!=AnalysisLumaFormat::P210 &&
         format!=AnalysisLumaFormat::DecodedDolbyVision422)) return false;
    const int outward=y>boundary ? 1 : -1;
    const auto row=[&](int index){return data+size_t(index)*rowBytes;};
    const auto* target=row(y);
    const auto* a=row(boundary+outward);
    const auto* b=row(boundary+2*outward);
    const auto* c=row(boundary+3*outward);
    const auto levels=[](const uint8_t* p){return _mm_srli_epi16(
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(p)),6);};
    const auto bright=_mm_set1_epi16(static_cast<short>(matte+15));
    const auto lo=_mm_set1_epi16(static_cast<short>(matte-2));
    const auto hi=_mm_set1_epi16(static_cast<short>(matte+2));
    for (;x+8<=width;x+=8) {
        auto mark=_mm_cmpgt_epi16(levels(target+2*x),bright);
        if (!_mm_movemask_epi8(mark)) continue;
        for (const auto* gap : {a,b,c}) {
            const auto v=levels(gap+2*x);
            mark=_mm_and_si128(mark,_mm_and_si128(_mm_cmpgt_epi16(v,lo),_mm_cmpgt_epi16(hi,v)));
        }
        if (_mm_movemask_epi8(mark)) break;
    }
    for (;x<width;++x)
        if (P010Code(target+2*x)>matte+15 && std::abs(int(P010Code(a+2*x))-matte)<=1 &&
            std::abs(int(P010Code(b+2*x))-matte)<=1 && std::abs(int(P010Code(c+2*x))-matte)<=1)
            break;
    return true;
}

bool AnalysisLumaSource::FindPlanarMatteRowDetail(int y, int referenceY, int shift,
    int matte, int tolerance, int& x, int startX, int taperBoundaryY) const
{
    x = startX;
    if (!IsValid() || startX < 0 || startX > width || y < 0 || y >= height || referenceY < -1 || referenceY >= height ||
        shift < 0 || shift > 10 || matte < 0 || matte > 1023 || tolerance < 0 || tolerance > 1023 ||
        (format != AnalysisLumaFormat::P010 && format != AnalysisLumaFormat::P210 &&
         format != AnalysisLumaFormat::DecodedDolbyVision422)) return false;
    // All three planar formats store canonical 10-bit codes in the top bits
    // of a 16-bit lane. U/V pairs span two neighbouring luma pixels.
    const auto* luma = data + size_t(y) * rowBytes;
    const auto* chroma = data + size_t(height) * rowBytes +
        size_t(format == AnalysisLumaFormat::P010 ? y / 2 : y) * chromaRowBytes;
    const auto* reference = referenceY < 0 ? nullptr : data + size_t(referenceY) * rowBytes;
    if (taperBoundaryY < -1 || taperBoundaryY >= height) return false;
    const int distance = taperBoundaryY < 0 ? 0 : std::abs(y-taperBoundaryY);
    const int inward = taperBoundaryY > y ? 1 : -1;
    const bool taper = taperBoundaryY >= 0;
    if (taper && (!reference || distance < 1 || distance > 8 ||
        (referenceY-taperBoundaryY)*inward <= 0)) return false;
    const int shoulderShift = distance <= 2 ? 1 : distance <= 4 ? 2 : 3;
    const auto inwardRow = [&](int k) { return data + size_t(y+k*inward)*rowBytes; };
    const auto scalarUpper = [&](int column) {
        int upper = matte + tolerance;
        if (!reference) return upper;
        const int inner = int(P010Code(reference+2*column)) - matte;
        upper = std::min(matte+128,upper+(std::max(0,inner)>>shift));
        if (!taper || inner < 24) return upper;
        const int neighbour = P010Code(inwardRow(1)+2*column);
        int shoulder = std::min(matte+4+(inner>>shoulderShift),
            neighbour-std::max(1,(neighbour-matte)/16));
        for (int k=2; k<=distance; ++k)
            shoulder = std::min(shoulder,int(P010Code(inwardRow(k)+2*column)));
        return std::max(upper,shoulder);
    };
    const auto lower = _mm_set1_epi16(static_cast<short>(matte - 4));
    const auto base = _mm_set1_epi16(static_cast<short>(matte + tolerance));
    const auto floor = _mm_set1_epi16(static_cast<short>(matte));
    const auto cap = _mm_set1_epi16(static_cast<short>(matte + 128));
    const auto shiftCount = _mm_cvtsi32_si128(shift);
    const auto loUV = _mm_set1_epi16(480), hiUV = _mm_set1_epi16(544);
    const auto codes = [](const uint8_t* src) {
        return _mm_srli_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(src)), 6);
    };
    // Interleaved UV pairs are aligned to even luma columns. Check a lone
    // odd prefix before beginning SIMD at an even column.
    if ((x & 1) && x < width) {
        AnalysisLumaSample pixel;
        if (!Sample(x,y,pixel)) return false;
        const int limit = scalarUpper(x);
        if (pixel.luma < matte-4 || pixel.luma > limit || std::abs(int(pixel.chromaU)-512)>32 ||
            std::abs(int(pixel.chromaV)-512)>32) return true;
        ++x;
    }
    for (; x + 8 <= width; x += 8) {
        const auto ys = codes(luma + 2 * x);
        auto upper = base;
        if (reference) {
            const auto delta = _mm_subs_epu16(codes(reference + 2 * x), floor);
            upper = _mm_min_epi16(cap, _mm_add_epi16(base, _mm_srl_epi16(delta, shiftCount)));
            // Most sections already pass the regular matte envelope. Only
            // evaluate the qualified same-column taper for exceptional lanes.
            if (taper && _mm_movemask_epi8(_mm_cmpgt_epi16(ys,upper))) {
                const auto next = codes(inwardRow(1)+2*x);
                const auto fall = _mm_max_epi16(_mm_set1_epi16(1),_mm_srli_epi16(_mm_subs_epu16(next,floor),4));
                auto shoulder = _mm_add_epi16(_mm_set1_epi16(static_cast<short>(matte+4)),
                    _mm_srl_epi16(delta,_mm_cvtsi32_si128(shoulderShift)));
                shoulder = _mm_min_epi16(shoulder,_mm_sub_epi16(next,fall));
                for (int k=2; k<=distance; ++k)
                    shoulder = _mm_min_epi16(shoulder,codes(inwardRow(k)+2*x));
                shoulder = _mm_and_si128(shoulder,_mm_cmpgt_epi16(delta,_mm_set1_epi16(23)));
                upper = _mm_max_epi16(upper,shoulder);
            }
        }
        const auto uv = codes(chroma + 2 * x);
        const auto badY = _mm_or_si128(_mm_cmpgt_epi16(ys,upper),_mm_cmpgt_epi16(lower,ys));
        const auto badUV = _mm_or_si128(_mm_cmpgt_epi16(uv,hiUV),_mm_cmpgt_epi16(loUV,uv));
        if (_mm_movemask_epi8(_mm_or_si128(badY,badUV))) break;
    }
    for (; x < width; ++x) {
        const int upper = scalarUpper(x);
        const int value = P010Code(luma + 2 * x);
        const auto* pair = chroma + size_t(x / 2) * 4;
        if (value < matte - 4 || value > upper ||
            std::abs(int(P010Code(pair)) - 512) > 32 ||
            std::abs(int(P010Code(pair + 2)) - 512) > 32)
            break;
    }
    return true;
}

bool AnalysisLumaSource::RowWithinLimits(int y, uint16_t lumaMax,
    uint16_t chromaDeviation, bool& withinLimits, size_t& samplesChecked) const
{
    withinLimits = false;
    samplesChecked = 0;
    if (!IsValid() || y < 0 || y >= height || lumaMax > 1023 || chromaDeviation > 512)
        return false;
    const auto inside = [=](const AnalysisLumaSample& sample) {
        return sample.luma <= lumaMax &&
            sample.chromaU >= 512 - chromaDeviation &&
            sample.chromaU <= 512 + chromaDeviation &&
            sample.chromaV >= 512 - chromaDeviation &&
            sample.chromaV <= 512 + chromaDeviation;
    };
    if (format == AnalysisLumaFormat::P010 || format == AnalysisLumaFormat::P210 ||
        format == AnalysisLumaFormat::DecodedDolbyVision422) {
        // Same code readers and chroma footprint as Sample. Validate the
        // immutable source once per row, not again for every native pixel.
        const auto* luma = data + static_cast<size_t>(y) * rowBytes;
        const size_t chromaLine = format == AnalysisLumaFormat::P010 ?
            static_cast<size_t>(y / 2) : static_cast<size_t>(y);
        const auto* chroma = data + rowBytes * static_cast<size_t>(height) +
            chromaLine * chromaRowBytes;
        const auto readCode = format == AnalysisLumaFormat::DecodedDolbyVision422 ?
            DecodedDolbyVisionCode : P010Code;
        for (int x = 0; x < width; ++x) {
            const auto* pair = chroma + static_cast<size_t>(x / 2) * 4;
            const AnalysisLumaSample sample = { readCode(luma + static_cast<size_t>(x) * 2),
                readCode(pair), readCode(pair + 2) };
            ++samplesChecked;
            if (!inside(sample)) return true;
        }
    } else {
        // Packed/RGB inputs retain the canonical format conversion path.
        for (int x = 0; x < width; ++x) {
            AnalysisLumaSample sample;
            if (!Sample(x, y, sample)) return false;
            ++samplesChecked;
            if (!inside(sample)) return true;
        }
    }
    withinLimits = true;
    return true;
}

bool AnalysisLumaSource::Sample(int x, int y, AnalysisLumaSample& result) const
{
	if (!IsValid() || x < 0 || y < 0 || x >= width || y >= height)
		return false;
	if (format != AnalysisLumaFormat::NativeRgb &&
		format != AnalysisLumaFormat::SdrTunnelRgb)
	{
		if (format == AnalysisLumaFormat::NativeYuv422)
			return ReadNativeYuv422(*this, x, y, result);
		const uint8_t* luma = data + static_cast<size_t>(y) * rowBytes +
			static_cast<size_t>(x) * 2;
		const size_t lumaBytes = rowBytes * static_cast<size_t>(height);
		const size_t chromaLine = format == AnalysisLumaFormat::P210 ||
			format == AnalysisLumaFormat::DecodedDolbyVision422 ?
			static_cast<size_t>(y) : static_cast<size_t>(y / 2);
		const uint8_t* chroma = data + lumaBytes + chromaLine * chromaRowBytes +
			static_cast<size_t>(x / 2) * 4;
		const auto readCode = format == AnalysisLumaFormat::DecodedDolbyVision422 ?
			DecodedDolbyVisionCode : P010Code;
		result.luma = readCode(luma);
		result.chromaU = readCode(chroma);
		result.chromaV = readCode(chroma + 2);
		return true;
	}
	uint16_t red = 0;
	uint16_t green = 0;
	uint16_t blue = 0;
	if (format == AnalysisLumaFormat::SdrTunnelRgb)
	{
		const uint8_t* pixel = data + static_cast<size_t>(y) * rowBytes +
			static_cast<size_t>(x) * 4;
		const uint32_t word = ReadDolbyVisionTunnelWord(pixel, encoding);
		auto channel = [this](uint16_t code)
		{
			const uint8_t byte = static_cast<uint8_t>(rgbBitAlignment == 1 ?
				(code >> 2) : (code & 0xff));
			return range == AnalysisLumaRange::Limited ? uint16_t(byte) << 2 : Expand8To10(byte);
		};
		red = channel(static_cast<uint16_t>((word >> 20) & 0x3ff));
		green = channel(static_cast<uint16_t>((word >> 10) & 0x3ff));
		blue = channel(static_cast<uint16_t>(word & 0x3ff));
	}
	else if (!ReadRgb10(*this, x, y, red, green, blue))
		return false;
	const bool bt2020 = colorspace == ColorSpace::BT_2020;
	const bool limitedRgb = (format == AnalysisLumaFormat::SdrTunnelRgb && range == AnalysisLumaRange::Limited) ||
        (format != AnalysisLumaFormat::SdrTunnelRgb &&
		(encoding == VideoFrameEncoding::R210 ||
		encoding == VideoFrameEncoding::R10b ||
		encoding == VideoFrameEncoding::R10l));
	if (limitedRgb)
	{
        // SDR tunnel bytes use the RGB8 16-235 interval even inside r210.
        // The distinct native DeckLink r210 64-960 convention applies only
        // to ordinary ten-bit capture, not this eight-bit carrier.
        const bool nativeR210Range = format != AnalysisLumaFormat::SdrTunnelRgb &&
            encoding == VideoFrameEncoding::R210;
		const LimitedRgbCoefficients& coefficients = bt2020 ?
			(nativeR210Range ? BT2020_R210 : BT2020_R10) :
			(nativeR210Range ? BT709_R210 : BT709_R10);
		const int r = static_cast<int>(red) - 64;
		const int g = static_cast<int>(green) - 64;
		const int b = static_cast<int>(blue) - 64;
		result.luma = Clamp10(64 + RoundQ20(
			static_cast<int64_t>(coefficients.yR) * r +
			static_cast<int64_t>(coefficients.yG) * g +
			static_cast<int64_t>(coefficients.yB) * b));
		result.chromaU = Clamp10(512 + RoundQ20(
			static_cast<int64_t>(coefficients.cbR) * r +
			static_cast<int64_t>(coefficients.cbG) * g +
			static_cast<int64_t>(coefficients.cbB) * b));
		result.chromaV = Clamp10(512 + RoundQ20(
			static_cast<int64_t>(coefficients.crR) * r +
			static_cast<int64_t>(coefficients.crG) * g +
			static_cast<int64_t>(coefficients.crB) * b));
		return true;
	}
	const int yRed = bt2020 ? 17218 : 13933;
	const int yGreen = bt2020 ? 44444 : 46871;
	const int yBlue = bt2020 ? 3886 : 4732;
	const int cbRed = bt2020 ? -9147 : -7508;
	const int cbGreen = bt2020 ? -23621 : -25259;
	const int cbBlue = 32768;
	const int crRed = 32768;
	const int crGreen = bt2020 ? -30134 : -29763;
	const int crBlue = bt2020 ? -2634 : -3005;
	result.luma = Clamp10((yRed * red + yGreen * green + yBlue * blue + 32768) >> 16);
	result.chromaU = Clamp10(((cbRed * red + cbGreen * green + cbBlue * blue + 32768) >> 16) + 512);
	result.chromaV = Clamp10(((crRed * red + crGreen * green + crBlue * blue + 32768) >> 16) + 512);
	return true;
}

const char* AnalysisLumaFormatName(const AnalysisLumaSource& source)
{
	if (source.format == AnalysisLumaFormat::DecodedDolbyVision422)
		return "decoded-dv-12bit-422";
	if (source.format == AnalysisLumaFormat::SdrTunnelRgb)
		return "sdr-tunnel-rgb8";
	if (source.format == AnalysisLumaFormat::NativeRgb)
		return "native-rgb-sparse";
	if (source.format == AnalysisLumaFormat::NativeYuv422)
		return "native-yuv422-sparse";
	return source.format == AnalysisLumaFormat::P210 ? "p210-plane" : "p010-plane";
}
