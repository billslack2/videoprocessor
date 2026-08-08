#include "pch.h"

#include "AnalysisLumaSource.h"

#include <algorithm>
#include <limits>

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
	const size_t chromaHeight = format == AnalysisLumaFormat::P210 ?
		static_cast<size_t>(height) : static_cast<size_t>(height / 2);
	return CheckedMultiply(chromaRowBytes, chromaHeight, chromaBytes) &&
		requiredBytes <= std::numeric_limits<size_t>::max() - chromaBytes &&
		requiredBytes + chromaBytes <= dataBytes;
}

bool AnalysisLumaSource::Sample(int x, int y, AnalysisLumaSample& result) const
{
	if (!IsValid() || x < 0 || y < 0 || x >= width || y >= height)
		return false;
	if (format != AnalysisLumaFormat::NativeRgb)
	{
		if (format == AnalysisLumaFormat::NativeYuv422)
			return ReadNativeYuv422(*this, x, y, result);
		const uint8_t* luma = data + static_cast<size_t>(y) * rowBytes +
			static_cast<size_t>(x) * 2;
		const size_t lumaBytes = rowBytes * static_cast<size_t>(height);
		const size_t chromaLine = format == AnalysisLumaFormat::P210 ?
			static_cast<size_t>(y) : static_cast<size_t>(y / 2);
		const uint8_t* chroma = data + lumaBytes + chromaLine * chromaRowBytes +
			static_cast<size_t>(x / 2) * 4;
		result.luma = P010Code(luma);
		result.chromaU = P010Code(chroma);
		result.chromaV = P010Code(chroma + 2);
		return true;
	}
	uint16_t red = 0;
	uint16_t green = 0;
	uint16_t blue = 0;
	if (!ReadRgb10(*this, x, y, red, green, blue))
		return false;
	const bool bt2020 = colorspace == ColorSpace::BT_2020;
	const bool limitedRgb = encoding == VideoFrameEncoding::R210 ||
		encoding == VideoFrameEncoding::R10b ||
		encoding == VideoFrameEncoding::R10l;
	if (limitedRgb)
	{
		const LimitedRgbCoefficients& coefficients = bt2020 ?
			(encoding == VideoFrameEncoding::R210 ? BT2020_R210 : BT2020_R10) :
			(encoding == VideoFrameEncoding::R210 ? BT709_R210 : BT709_R10);
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
	if (source.format == AnalysisLumaFormat::NativeRgb)
		return "native-rgb-sparse";
	if (source.format == AnalysisLumaFormat::NativeYuv422)
		return "native-yuv422-sparse";
	return source.format == AnalysisLumaFormat::P210 ? "p210-plane" : "p010-plane";
}
