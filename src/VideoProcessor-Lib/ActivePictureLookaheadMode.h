#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>


enum class ActivePictureLookaheadMode : uint8_t
{
	OFF = 0,
	SHADOW = 1
};


inline const char* ActivePictureLookaheadModeName(
	ActivePictureLookaheadMode mode)
{
	switch (mode)
	{
	case ActivePictureLookaheadMode::OFF:
		return "off";
	case ActivePictureLookaheadMode::SHADOW:
		return "shadow";
	default:
		return "off";
	}
}


inline bool ParseActivePictureLookaheadMode(
	std::string value, ActivePictureLookaheadMode& mode)
{
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (value == "off")
	{
		mode = ActivePictureLookaheadMode::OFF;
		return true;
	}
	if (value == "shadow")
	{
		mode = ActivePictureLookaheadMode::SHADOW;
		return true;
	}
	return false;
}
