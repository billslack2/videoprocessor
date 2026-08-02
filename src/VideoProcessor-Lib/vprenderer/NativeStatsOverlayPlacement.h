#pragma once

#include <algorithm>

// Geometry-only native OSD placement.  Keeping this independent of libplacebo
// makes the containment rules directly unit-testable.
namespace NativeStatsOverlayPlacement
{
	constexpr float kDefaultInsetPixels = 25.0f;

	struct Rect
	{
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;

		float Width() const { return right - left; }
		float Height() const { return bottom - top; }
		bool IsValid() const { return Width() > 0.0f && Height() > 0.0f; }
	};

	struct Result
	{
		Rect visiblePicture;
		Rect panel;
		float scale = 1.0f;
		bool insetClamped = false;
		bool usedOutputFallback = false;
	};

	inline Rect Intersect(const Rect& first, const Rect& second)
	{
		return {
			std::max(first.left, second.left),
			std::max(first.top, second.top),
			std::min(first.right, second.right),
			std::min(first.bottom, second.bottom) };
	}

	inline Result Place(const Rect& requestedPicture, const Rect& output,
		float panelWidth, float panelHeight,
		float insetPixels = kDefaultInsetPixels)
	{
		Result result;
		result.visiblePicture = Intersect(requestedPicture, output);
		if (!result.visiblePicture.IsValid())
		{
			result.visiblePicture = output;
			result.usedOutputFallback = true;
		}
		if (!result.visiblePicture.IsValid() || panelWidth <= 0.0f ||
			panelHeight <= 0.0f)
			return result;

		const float availableWidth = result.visiblePicture.Width();
		const float availableHeight = result.visiblePicture.Height();
		const float inset = std::max(0.0f, insetPixels);
		const float insetWidth = std::max(0.0f, availableWidth - inset);
		const float insetHeight = std::max(0.0f, availableHeight - inset);
		result.scale = std::min(1.0f, std::min(
			insetWidth / panelWidth, insetHeight / panelHeight));
		if (result.scale <= 0.0f)
		{
			// A picture smaller than the requested inset still receives a panel,
			// scaled uniformly to remain inside the rendered image.
			result.scale = std::min(1.0f, std::min(
				availableWidth / panelWidth, availableHeight / panelHeight));
			result.insetClamped = true;
		}
		if (result.scale < 1.0f)
			result.insetClamped = true;

		const float width = panelWidth * result.scale;
		const float height = panelHeight * result.scale;
		float right = result.visiblePicture.right - inset;
		float bottom = result.visiblePicture.bottom - inset;
		if (right - width < result.visiblePicture.left)
		{
			right = result.visiblePicture.right;
			result.insetClamped = true;
		}
		if (bottom - height < result.visiblePicture.top)
		{
			bottom = result.visiblePicture.bottom;
			result.insetClamped = true;
		}
		result.panel = { right - width, bottom - height, right, bottom };
		return result;
	}
}
