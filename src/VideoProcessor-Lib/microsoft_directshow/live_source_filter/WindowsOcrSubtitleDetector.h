#pragma once

#include <cstdint>
#include <vector>

struct WindowsOcrWordBox
{
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;
};

struct WindowsOcrSubtitleResult
{
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;
	int score = 0;
	int lineCount = 0;
	int representativeLineHeight = 0;
	uint64_t contentHash = 0;
	bool available = false;
	bool detected = false;
	bool textObserved = false;
	bool atTop = false;
	std::vector<WindowsOcrWordBox> words;
};

WindowsOcrSubtitleResult DetectWindowsOcrSubtitle(
	const uint16_t* luma,
	int width,
	int height,
	int pictureTop,
	int pictureBottom);
