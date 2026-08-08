#pragma once

#include <algorithm>
#include <cstdint>

namespace ModernOperatorLayout
{
	struct Rect
	{
		int x = 0;
		int y = 0;
		int width = 0;
		int height = 0;
	};

	struct Layout
	{
		Rect header;
		Rect information;
		Rect preview;
	};

	constexpr int BaseDpi = 96;
	// The approved default pairs a 1040x585 (exact 16:9) preview with the
	// fixed-height operator cards. Both columns therefore finish at y=655.
	constexpr int DefaultClientWidth = 1600;
	constexpr int DefaultClientHeight = 671;
	constexpr int HeaderHeight = 55;
	constexpr int OuterMargin = 16;
	constexpr int ContentTop = 70;
	constexpr int InformationWidth = 512;
	constexpr int ColumnGap = 16;

	inline int Scale(int logicalPixels, unsigned int dpi)
	{
		return static_cast<int>((static_cast<int64_t>(logicalPixels) * dpi +
			(BaseDpi / 2)) / BaseDpi);
	}

	inline Rect FitSixteenByNine(const Rect& bounds)
	{
		if (bounds.width <= 0 || bounds.height <= 0)
			return { bounds.x, bounds.y, 0, 0 };

		int width = bounds.width - (bounds.width % 16);
		int height = (width / 16) * 9;
		if (height > bounds.height)
		{
			height = bounds.height - (bounds.height % 9);
			width = (height / 9) * 16;
		}

		return {
			bounds.x + (bounds.width - width) / 2,
			bounds.y + (bounds.height - height) / 2,
			width,
			height
		};
	}

	inline Layout Calculate(int clientWidth, int clientHeight,
		unsigned int dpi = BaseDpi)
	{
		const int margin = Scale(OuterMargin, dpi);
		const int contentTop = Scale(ContentTop, dpi);
		const int informationWidth = Scale(InformationWidth, dpi);
		const int gap = Scale(ColumnGap, dpi);
		const int contentHeight = std::max(0,
			clientHeight - contentTop - margin);

		Layout result;
		result.header = { 0, 0, clientWidth, Scale(HeaderHeight, dpi) };
		result.information = {
			margin, contentTop, informationWidth, contentHeight
		};

		const int previewLeft = margin + informationWidth + gap;
		const Rect previewBounds = {
			previewLeft,
			contentTop,
			std::max(0, clientWidth - previewLeft - margin),
			contentHeight
		};
		result.preview = FitSixteenByNine(previewBounds);
		return result;
	}
}

namespace NoUiLayout
{
	// No-ui is a video-only host. Keep both its startup and minimum client
	// areas explicitly 16:9 instead of inheriting the Classic dialog resource.
	constexpr int DefaultClientWidth = 960;
	constexpr int DefaultClientHeight = 540;
	constexpr int MinimumClientWidth = 320;
	constexpr int MinimumClientHeight = 180;
}
