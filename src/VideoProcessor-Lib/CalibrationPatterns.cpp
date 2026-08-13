#include "pch.h"

#include "CalibrationPatterns.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>


namespace
{
struct Rgb
{
	std::uint8_t r;
	std::uint8_t g;
	std::uint8_t b;
};

void SetPixel(CalibrationPatternFrame& frame, unsigned int x, unsigned int y,
	const Rgb& color)
{
	const size_t offset = (static_cast<size_t>(y) * frame.width + x) * 4;
	frame.bgra[offset + 0] = color.b;
	frame.bgra[offset + 1] = color.g;
	frame.bgra[offset + 2] = color.r;
	frame.bgra[offset + 3] = 255;
}

void Fill(CalibrationPatternFrame& frame, const Rgb& color)
{
	for (unsigned int y = 0; y < frame.height; ++y)
		for (unsigned int x = 0; x < frame.width; ++x)
			SetPixel(frame, x, y, color);
}

void Rectangle(CalibrationPatternFrame& frame, int left, int top, int right,
	int bottom, const Rgb& color)
{
	left = std::max(0, left);
	top = std::max(0, top);
	right = std::min(static_cast<int>(frame.width), right);
	bottom = std::min(static_cast<int>(frame.height), bottom);
	for (int y = top; y < bottom; ++y)
		for (int x = left; x < right; ++x)
			SetPixel(frame, static_cast<unsigned int>(x),
				static_cast<unsigned int>(y), color);
}

Rgb Gray(std::uint8_t value)
{
	return { value, value, value };
}

std::uint8_t ClampCode(int value)
{
	return static_cast<std::uint8_t>(std::max(0, std::min(255, value)));
}

void DrawBlackLevel(CalibrationPatternFrame& frame,
	CalibrationSignalRange range)
{
	const int black = range == CalibrationSignalRange::STUDIO ? 16 : 0;
	const int values[] = {
		range == CalibrationSignalRange::STUDIO ? 12 : 0,
		black,
		black + 1,
		black + 2,
		black + 4,
		black + 8,
		black + 12,
		black + 16
	};
	Fill(frame, Gray(ClampCode(black)));
	const int left = static_cast<int>(frame.width / 10);
	const int right = static_cast<int>(frame.width - frame.width / 10);
	const int top = static_cast<int>(frame.height / 4);
	const int bottom = static_cast<int>(frame.height - frame.height / 4);
	for (int index = 0; index < 8; ++index)
	{
		const int x0 = left + (right - left) * index / 8;
		const int x1 = left + (right - left) * (index + 1) / 8;
		Rectangle(frame, x0, top, x1, bottom, Gray(ClampCode(values[index])));
	}
}

void DrawWhiteClipping(CalibrationPatternFrame& frame,
	CalibrationSignalRange range)
{
	const int valuesFull[] = { 204, 217, 230, 236, 242, 247, 250, 252, 254, 255 };
	const int valuesStudio[] = { 191, 202, 213, 224, 235, 236, 240, 245, 250, 255 };
	const int* values = range == CalibrationSignalRange::STUDIO
		? valuesStudio : valuesFull;
	Fill(frame, Gray(CalibrationPatterns::EncodeGray(0.25, range)));
	const int left = static_cast<int>(frame.width / 16);
	const int right = static_cast<int>(frame.width - frame.width / 16);
	const int top = static_cast<int>(frame.height / 5);
	const int bottom = static_cast<int>(frame.height - frame.height / 5);
	for (int index = 0; index < 10; ++index)
	{
		const int x0 = left + (right - left) * index / 10;
		const int x1 = left + (right - left) * (index + 1) / 10;
		Rectangle(frame, x0, top, x1, bottom, Gray(ClampCode(values[index])));
	}
}

void DrawGrayscale(CalibrationPatternFrame& frame,
	CalibrationSignalRange range)
{
	for (int index = 0; index < 11; ++index)
	{
		const int x0 = static_cast<int>(frame.width) * index / 11;
		const int x1 = static_cast<int>(frame.width) * (index + 1) / 11;
		Rectangle(frame, x0, 0, x1, static_cast<int>(frame.height),
			Gray(CalibrationPatterns::EncodeGray(index / 10.0, range)));
	}
}

void DrawGammaComparison(CalibrationPatternFrame& frame,
	CalibrationSignalRange range)
{
	const std::uint8_t black = CalibrationPatterns::EncodeGray(0.0, range);
	const std::uint8_t white = CalibrationPatterns::EncodeGray(1.0, range);
	const unsigned int checker = std::max(1u, frame.height / 270u);
	for (unsigned int y = 0; y < frame.height; ++y)
		for (unsigned int x = 0; x < frame.width; ++x)
			SetPixel(frame, x, y, Gray((((x / checker) + (y / checker)) & 1)
				? white : black));

	const int marginX = static_cast<int>(frame.width / 10);
	const int gap = static_cast<int>(frame.width / 30);
	const int center = static_cast<int>(frame.width / 2);
	const int top = static_cast<int>(frame.height / 4);
	const int bottom = static_cast<int>(frame.height - frame.height / 4);
	const std::uint8_t gamma22 = CalibrationPatterns::EncodeGray(
		std::pow(0.5, 1.0 / 2.2), range);
	const std::uint8_t gamma24 = CalibrationPatterns::EncodeGray(
		std::pow(0.5, 1.0 / 2.4), range);
	Rectangle(frame, marginX, top, center - gap / 2, bottom, Gray(gamma22));
	Rectangle(frame, center + gap / 2, top,
		static_cast<int>(frame.width) - marginX, bottom, Gray(gamma24));
}

void DrawColorClipping(CalibrationPatternFrame& frame,
	CalibrationSignalRange range)
{
	const Rgb masks[] = {
		{ 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 },
		{ 0, 255, 255 }, { 255, 0, 255 }, { 255, 255, 0 }
	};
	Fill(frame, Gray(CalibrationPatterns::EncodeGray(0.0, range)));
	const int marginX = static_cast<int>(frame.width / 16);
	const int marginY = static_cast<int>(frame.height / 16);
	for (int row = 0; row < 6; ++row)
	{
		for (int column = 0; column < 8; ++column)
		{
			const std::uint8_t zero = CalibrationPatterns::EncodeGray(0.0, range);
			const std::uint8_t level = CalibrationPatterns::EncodeGray(
				0.72 + column * 0.04, range);
			const Rgb color = {
				masks[row].r ? level : zero,
				masks[row].g ? level : zero,
				masks[row].b ? level : zero
			};
			const int x0 = marginX +
				(static_cast<int>(frame.width) - 2 * marginX) * column / 8;
			const int x1 = marginX +
				(static_cast<int>(frame.width) - 2 * marginX) * (column + 1) / 8;
			const int y0 = marginY +
				(static_cast<int>(frame.height) - 2 * marginY) * row / 6;
			const int y1 = marginY +
				(static_cast<int>(frame.height) - 2 * marginY) * (row + 1) / 6;
			Rectangle(frame, x0 + 1, y0 + 1, x1 - 1, y1 - 1, color);
		}
	}
}

void DrawSharpness(CalibrationPatternFrame& frame,
	CalibrationSignalRange range)
{
	const Rgb black = Gray(CalibrationPatterns::EncodeGray(0.0, range));
	const Rgb white = Gray(CalibrationPatterns::EncodeGray(1.0, range));
	const Rgb gray = Gray(CalibrationPatterns::EncodeGray(0.5, range));
	Fill(frame, gray);
	for (unsigned int y = frame.height / 8; y < frame.height * 7 / 8; ++y)
	{
		for (unsigned int x = frame.width / 16; x < frame.width * 7 / 16; ++x)
			SetPixel(frame, x, y, ((x + y) & 1) ? white : black);
		for (unsigned int x = frame.width * 9 / 16; x < frame.width * 15 / 16; ++x)
			SetPixel(frame, x, y, (((x / 2) + (y / 2)) & 1) ? white : black);
	}
	const int cx = static_cast<int>(frame.width / 2);
	const int cy = static_cast<int>(frame.height / 2);
	Rectangle(frame, cx - 1, static_cast<int>(frame.height / 12), cx + 1,
		static_cast<int>(frame.height * 11 / 12), white);
	Rectangle(frame, static_cast<int>(frame.width / 12), cy - 1,
		static_cast<int>(frame.width * 11 / 12), cy + 1, white);
}

void DrawGeometry(CalibrationPatternFrame& frame,
	CalibrationSignalRange range)
{
	const Rgb black = Gray(CalibrationPatterns::EncodeGray(0.0, range));
	const Rgb white = Gray(CalibrationPatterns::EncodeGray(1.0, range));
	Fill(frame, black);
	const unsigned int stepX = std::max(1u, frame.width / 20);
	const unsigned int stepY = std::max(1u, frame.height / 20);
	for (unsigned int x = 0; x < frame.width; x += stepX)
		Rectangle(frame, static_cast<int>(x), 0, static_cast<int>(x + 1),
			static_cast<int>(frame.height), white);
	for (unsigned int y = 0; y < frame.height; y += stepY)
		Rectangle(frame, 0, static_cast<int>(y), static_cast<int>(frame.width),
			static_cast<int>(y + 1), white);
	Rectangle(frame, 0, 0, static_cast<int>(frame.width), 2, white);
	Rectangle(frame, 0, static_cast<int>(frame.height) - 2,
		static_cast<int>(frame.width), static_cast<int>(frame.height), white);
	Rectangle(frame, 0, 0, 2, static_cast<int>(frame.height), white);
	Rectangle(frame, static_cast<int>(frame.width) - 2, 0,
		static_cast<int>(frame.width), static_cast<int>(frame.height), white);

	const double cx = (frame.width - 1) / 2.0;
	const double cy = (frame.height - 1) / 2.0;
	const double radius = std::min(frame.width, frame.height) * 0.4;
	for (unsigned int y = 0; y < frame.height; ++y)
	{
		for (unsigned int x = 0; x < frame.width; ++x)
		{
			const double distance = std::sqrt((x - cx) * (x - cx) +
				(y - cy) * (y - cy));
			if (std::abs(distance - radius) < 0.8)
				SetPixel(frame, x, y, white);
		}
	}
}

const char* Glyph(char character)
{
	switch (character)
	{
	case '0': return "111101101101111";
	case '1': return "010110010010111";
	case '2': return "111001111100111";
	case '3': return "111001111001111";
	case '4': return "101101111001001";
	case '5': return "111100111001111";
	case '6': return "111100111101111";
	case '7': return "111001010010010";
	case '8': return "111101111101111";
	case '9': return "111101111001111";
	case '.': return "000000000000010";
	case ':': return "000010000010000";
	default: return "000000000000000";
	}
}

void DrawLabel(CalibrationPatternFrame& frame, const char* text, int centerX,
	int top, int scale, const Rgb& color)
{
	const int length = static_cast<int>(strlen(text));
	const int width = length * 4 * scale - scale;
	int left = centerX - width / 2;
	for (int index = 0; index < length; ++index)
	{
		const char* glyph = Glyph(text[index]);
		for (int row = 0; row < 5; ++row)
			for (int column = 0; column < 3; ++column)
				if (glyph[row * 3 + column] == '1')
					Rectangle(frame, left + column * scale, top + row * scale,
						left + (column + 1) * scale, top + (row + 1) * scale,
						color);
		left += 4 * scale;
	}
}

void DrawCinemaGeometry(CalibrationPatternFrame& frame, double aspectRatio,
	const Rgb& grid)
{
	Fill(frame, { 0, 0, 0 });
	const double rasterAspect = static_cast<double>(frame.width) / frame.height;
	int patternWidth = static_cast<int>(frame.width);
	int patternHeight = static_cast<int>(frame.height);
	if (aspectRatio > rasterAspect)
		patternHeight = std::max(1, static_cast<int>(frame.width / aspectRatio + 0.5));
	else
		patternWidth = std::max(1, static_cast<int>(frame.height * aspectRatio + 0.5));
	const int left = (static_cast<int>(frame.width) - patternWidth) / 2;
	const int top = (static_cast<int>(frame.height) - patternHeight) / 2;
	const int right = left + patternWidth;
	const int bottom = top + patternHeight;
	const int thickness = std::max(1, static_cast<int>(std::min(
		frame.width, frame.height) / 540));

	for (int division = 0; division <= 16; ++division)
	{
		const int x = left + patternWidth * division / 16;
		Rectangle(frame, x, top, x + thickness, bottom, grid);
	}
	for (int division = 0; division <= 9; ++division)
	{
		const int y = top + patternHeight * division / 9;
		Rectangle(frame, left, y, right, y + thickness, grid);
	}
	Rectangle(frame, left, top, right, top + thickness * 2, grid);
	Rectangle(frame, left, bottom - thickness * 2, right, bottom, grid);
	Rectangle(frame, left, top, left + thickness * 2, bottom, grid);
	Rectangle(frame, right - thickness * 2, top, right, bottom, grid);

	const double cx = (left + right - 1) / 2.0;
	const double cy = (top + bottom - 1) / 2.0;
	const double radius = std::min(patternWidth, patternHeight) * 0.4;
	for (int y = top; y < bottom; ++y)
		for (int x = left; x < right; ++x)
		{
			const double distance = std::sqrt((x - cx) * (x - cx) +
				(y - cy) * (y - cy));
			if (std::abs(distance - radius) < thickness)
				SetPixel(frame, static_cast<unsigned int>(x),
					static_cast<unsigned int>(y), grid);
		}

	char label[16] = {};
	sprintf_s(label, "%.2f:1", aspectRatio);
	const int labelScale = std::max(2, patternHeight / 90);
	DrawLabel(frame, label, static_cast<int>(cx),
		top + std::max(thickness * 4, patternHeight / 40), labelScale, grid);
}

void DrawField(CalibrationPatternFrame& frame, CalibrationSignalRange range,
	double level)
{
	Fill(frame, Gray(CalibrationPatterns::EncodeGray(level, range)));
}

void DrawWindow(CalibrationPatternFrame& frame, CalibrationSignalRange range)
{
	DrawField(frame, range, 0.0);
	const double side = std::sqrt(0.10);
	const int width = static_cast<int>(frame.width * side);
	const int height = static_cast<int>(frame.height * side);
	const int left = (static_cast<int>(frame.width) - width) / 2;
	const int top = (static_cast<int>(frame.height) - height) / 2;
	Rectangle(frame, left, top, left + width, top + height,
		Gray(CalibrationPatterns::EncodeGray(1.0, range)));
}
}


std::uint8_t CalibrationPatterns::EncodeGray(double normalized,
	CalibrationSignalRange range)
{
	normalized = std::max(0.0, std::min(1.0, normalized));
	const double encoded = range == CalibrationSignalRange::STUDIO
		? 16.0 + 219.0 * normalized
		: 255.0 * normalized;
	return ClampCode(static_cast<int>(std::floor(encoded + 0.5)));
}


CalibrationPatternFrame CalibrationPatterns::Generate(CalibrationPattern pattern,
	CalibrationSignalRange range, unsigned int width, unsigned int height)
{
	if (width < 64 || height < 64)
		throw std::invalid_argument("Calibration pattern raster must be at least 64x64");

	CalibrationPatternFrame frame;
	frame.width = width;
	frame.height = height;
	frame.bgra.resize(static_cast<size_t>(width) * height * 4, 255);

	switch (pattern)
	{
	case CalibrationPattern::BLACK_LEVEL: DrawBlackLevel(frame, range); break;
	case CalibrationPattern::WHITE_CLIPPING: DrawWhiteClipping(frame, range); break;
	case CalibrationPattern::GRAYSCALE_STEPS: DrawGrayscale(frame, range); break;
	case CalibrationPattern::GAMMA_COMPARISON: DrawGammaComparison(frame, range); break;
	case CalibrationPattern::COLOR_CLIPPING: DrawColorClipping(frame, range); break;
	case CalibrationPattern::SHARPNESS: DrawSharpness(frame, range); break;
	case CalibrationPattern::GEOMETRY: DrawGeometry(frame, range); break;
	case CalibrationPattern::BLACK_FIELD: DrawField(frame, range, 0.0); break;
	case CalibrationPattern::GRAY_FIELD: DrawField(frame, range, 0.5); break;
	case CalibrationPattern::WHITE_FIELD: DrawField(frame, range, 1.0); break;
	case CalibrationPattern::TEN_PERCENT_WHITE_WINDOW: DrawWindow(frame, range); break;
	default: throw std::invalid_argument("Unknown calibration pattern");
	}

	return frame;
}


CalibrationPatternFrame CalibrationPatterns::GenerateCinemaGeometry(
	double aspectRatio, std::uint8_t red, std::uint8_t green,
	std::uint8_t blue, unsigned int width, unsigned int height)
{
	if (!std::isfinite(aspectRatio) || aspectRatio <= 0.0)
		throw std::invalid_argument("Cinema aspect ratio must be positive");
	if (width < 64 || height < 64)
		throw std::invalid_argument("Calibration pattern raster must be at least 64x64");
	CalibrationPatternFrame frame;
	frame.width = width;
	frame.height = height;
	frame.bgra.resize(static_cast<size_t>(width) * height * 4, 255);
	DrawCinemaGeometry(frame, aspectRatio, { red, green, blue });
	return frame;
}
