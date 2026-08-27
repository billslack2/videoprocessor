#pragma once

#include <algorithm>

// Geometry-only native OSD placement.  Keeping this independent of libplacebo
// makes the containment rules directly unit-testable.
namespace NativeStatsOverlayPlacement
{
	constexpr float kDefaultInsetPixels = 40.0f;
	constexpr float kProfileOverlayInsetPixels = 30.0f;
	constexpr float kProfileOverlayReferenceHeight = 1080.0f;
	constexpr float kProfileOverlayBaselineScale = 1.275f;

	inline float ProfileOverlayScale(float outputHeight)
	{
		if (outputHeight <= 0.0f)
			return 1.0f;
		// Keep the banner's share of the output height constant at every
		// resolution. The 1.5x baseline is the readability choice; resolution
		// scaling itself remains strictly proportional.
		return std::max(0.75f, std::min(3.0f,
			kProfileOverlayBaselineScale * outputHeight /
				kProfileOverlayReferenceHeight));
	}

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

	inline Result PlaceTopRight(const Rect& requestedPicture, const Rect& output,
		float panelWidth, float panelHeight,
		float insetPixels = kDefaultInsetPixels)
	{
		Result result = Place(requestedPicture, output, panelWidth, panelHeight,
			insetPixels);
		if (!result.visiblePicture.IsValid() || !result.panel.IsValid())
			return result;
		const float height = result.panel.Height();
		float top = result.visiblePicture.top + std::max(0.0f, insetPixels);
		if (top + height > result.visiblePicture.bottom)
		{
			top = result.visiblePicture.top;
			result.insetClamped = true;
		}
		result.panel.top = top;
		result.panel.bottom = top + height;
		return result;
	}

	inline Result PlaceTopLeft(const Rect& requestedPicture, const Rect& output,
		float panelWidth, float panelHeight,
		float insetPixels = kDefaultInsetPixels)
	{
		Result result = PlaceTopRight(requestedPicture, output, panelWidth,
			panelHeight, insetPixels);
		if (!result.visiblePicture.IsValid() || !result.panel.IsValid())
			return result;
		const float width = result.panel.Width();
		float left = result.visiblePicture.left + std::max(0.0f, insetPixels);
		if (left + width > result.visiblePicture.right)
		{
			left = result.visiblePicture.left;
			result.insetClamped = true;
		}
		result.panel.left = left;
		result.panel.right = left + width;
		return result;
	}
}
