#pragma once

#include <cstdint>
#include <vector>


enum class CalibrationPattern
{
	BLACK_LEVEL,
	WHITE_CLIPPING,
	GRAYSCALE_STEPS,
	GAMMA_COMPARISON,
	COLOR_CLIPPING,
	SHARPNESS,
	GEOMETRY,
	BLACK_FIELD,
	GRAY_FIELD,
	WHITE_FIELD,
	TEN_PERCENT_WHITE_WINDOW
};


enum class CalibrationSignalRange
{
	FULL,
	STUDIO
};


struct CalibrationPatternFrame
{
	unsigned int width = 0;
	unsigned int height = 0;
	std::vector<std::uint8_t> bgra;
};


class CalibrationPatterns
{
public:
	static CalibrationPatternFrame Generate(CalibrationPattern pattern,
		CalibrationSignalRange range, unsigned int width, unsigned int height);
	static CalibrationPatternFrame GenerateCinemaGeometry(double aspectRatio,
		std::uint8_t red, std::uint8_t green, std::uint8_t blue,
		unsigned int width, unsigned int height);
	static std::uint8_t EncodeGray(double normalized,
		CalibrationSignalRange range);
};
