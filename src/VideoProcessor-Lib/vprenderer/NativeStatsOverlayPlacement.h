#pragma once

#include <algorithm>
#include <cmath>

// Geometry-only native OSD placement.  Keeping this independent of libplacebo
// makes the containment rules directly unit-testable.
namespace NativeStatsOverlayPlacement
{
	constexpr float kDefaultInsetPixels = 40.0f;
	constexpr float kProfileOverlayInsetPixels = 30.0f;
	constexpr float kProfileOverlayAdditionalLeftInsetReferencePixels = 30.0f;
	constexpr float kProfileOverlayAdditionalTopInsetReferencePixels = 30.0f;
	constexpr float kProfileOverlayReferenceHeight = 1080.0f;
	constexpr float kProfileOverlayBaselineScale = 1.275f;

	inline float ProfileOverlayScale(float visiblePictureHeight)
	{
		if (!std::isfinite(visiblePictureHeight) || visiblePictureHeight <= 0.0f)
			return 1.0f;
		// Keep the banner's share of the visible picture height constant.  A
		// scope viewport or zoom crop can be substantially smaller than the raw
		// output frame, whose dimensions must not make the banner oversized.
		return std::max(0.75f, std::min(3.0f,
			kProfileOverlayBaselineScale * visiblePictureHeight /
				kProfileOverlayReferenceHeight));
	}

	inline float ProfileOverlayLeftInset(float visiblePictureHeight)
	{
		if (!std::isfinite(visiblePictureHeight) || visiblePictureHeight <= 0.0f)
			return kProfileOverlayInsetPixels +
				kProfileOverlayAdditionalLeftInsetReferencePixels;
		const float relativeScale = std::max(0.5f, std::min(3.0f,
			visiblePictureHeight / kProfileOverlayReferenceHeight));
		return kProfileOverlayInsetPixels +
			kProfileOverlayAdditionalLeftInsetReferencePixels * relativeScale;
	}

	inline float ProfileOverlayTopInset(float visiblePictureHeight)
	{
		if (!std::isfinite(visiblePictureHeight) || visiblePictureHeight <= 0.0f)
			return kProfileOverlayInsetPixels +
				kProfileOverlayAdditionalTopInsetReferencePixels;
		const float relativeScale = std::max(0.5f, std::min(3.0f,
			visiblePictureHeight / kProfileOverlayReferenceHeight));
		return kProfileOverlayInsetPixels +
			kProfileOverlayAdditionalTopInsetReferencePixels * relativeScale;
	}

	struct StatsSizing
	{
		float scale = 1.0f;
		float insetPixels = kDefaultInsetPixels;
	};

	// CIH assumes profiles describe the same physical screen height. The fitted
	// configured screen is measured before movie fit/crop/zoom; changing movie
	// geometry must not change nominal OSD size. A bad screen fit falls back to
	// the output raster, then the 2160-line calibration if neither is available.
	inline float ScreenReferenceHeight(float screenHeight, float outputHeight)
	{
		const bool screenValid = std::isfinite(screenHeight) && screenHeight > 0.0f;
		const bool outputValid = std::isfinite(outputHeight) && outputHeight > 0.0f;
		if (screenValid) return outputValid ? std::min(screenHeight, outputHeight) : screenHeight;
		return outputValid ? outputHeight : 2160.0f;
	}

	inline StatsSizing ResolveStatsSizing(bool enabled, float screenHeight, float outputHeight)
	{
		if (!enabled) return {};
		const float geometryScale = ScreenReferenceHeight(screenHeight, outputHeight) / 2160.0f;
		return { 1.25f * geometryScale, kDefaultInsetPixels * geometryScale };
	}

	inline StatsSizing ResolveSweepSizing(bool enabled, float screenHeight, float outputHeight)
	{
		if (!enabled) return {};
		const float geometryScale = ScreenReferenceHeight(screenHeight, outputHeight) / 2160.0f;
		return { geometryScale, kDefaultInsetPixels * geometryScale };
	}

	inline StatsSizing ResolveProfileSizing(bool enabled, float screenHeight,
		float outputHeight, float visiblePictureHeight)
	{
		if (!enabled) return { ProfileOverlayScale(visiblePictureHeight),
			ProfileOverlayTopInset(visiblePictureHeight) };
		const float geometryScale = ScreenReferenceHeight(screenHeight, outputHeight) / 2160.0f;
		// Retain the existing profile banner's 2160-line typography and margin;
		// apply the same physical geometry factor as stats and sweep notices.
		return { 2.0f * kProfileOverlayBaselineScale * geometryScale, 90.0f * geometryScale };
	}

	struct Rect
	{
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;

		float Width() const { return right - left; }
		float Height() const { return bottom - top; }
		bool IsValid() const {
            return std::isfinite(left) && std::isfinite(top) && std::isfinite(right) &&
                std::isfinite(bottom) && std::isfinite(Width()) && std::isfinite(Height()) &&
                Width() > 0.0f && Height() > 0.0f;
        }
	};

	struct Result
	{
		Rect visiblePicture;
		Rect panel;
		float scale = 0.0f;
		bool insetClamped = false;
		bool usedOutputFallback = false;
	};

	inline Rect Intersect(const Rect& first, const Rect& second)
	{
		if (!first.IsValid() || !second.IsValid()) return {};
		return {
			std::max(first.left, second.left),
			std::max(first.top, second.top),
			std::min(first.right, second.right),
			std::min(first.bottom, second.bottom) };
	}

	// maxScale above 1.0 lets a caller enlarge the panel; the default keeps the
	// original shrink-to-fit-only behaviour for every existing caller.
	inline Result Place(const Rect& requestedPicture, const Rect& output,
		float panelWidth, float panelHeight,
		float insetPixels = kDefaultInsetPixels, float maxScale = 1.0f,
        float horizontalInsetPixels = -1.0f)
	{
		Result result;
		result.visiblePicture = Intersect(requestedPicture, output);
		if (!result.visiblePicture.IsValid())
		{
			result.visiblePicture = output;
			result.usedOutputFallback = true;
		}
		if (!result.visiblePicture.IsValid() || !std::isfinite(panelWidth) ||
            !std::isfinite(panelHeight) || panelWidth <= 0.0f || panelHeight <= 0.0f)
			return result;

        if (!std::isfinite(maxScale) || maxScale <= 0.0f) {
            maxScale = 1.0f; result.insetClamped = true;
        }
		const float availableWidth = result.visiblePicture.Width();
		const float availableHeight = result.visiblePicture.Height();
        const float inset = std::isfinite(insetPixels) ? std::max(0.0f, insetPixels) : 0.0f;
        const float horizontalInset = std::isfinite(horizontalInsetPixels) && horizontalInsetPixels >= 0.0f
            ? horizontalInsetPixels : inset;
		const float insetWidth = std::max(0.0f, availableWidth - horizontalInset);
		const float insetHeight = std::max(0.0f, availableHeight - inset);
		result.scale = std::min(maxScale, std::min(
			insetWidth / panelWidth, insetHeight / panelHeight));
		if (result.scale <= 0.0f)
		{
			// A picture smaller than the requested inset still receives a panel,
			// scaled uniformly to remain inside the rendered image.
			result.scale = std::min(maxScale, std::min(
				availableWidth / panelWidth, availableHeight / panelHeight));
			result.insetClamped = true;
		}
		if (result.scale < maxScale)
			result.insetClamped = true;

		const float width = panelWidth * result.scale;
		const float height = panelHeight * result.scale;
		float right = result.visiblePicture.right - horizontalInset;
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
        if (!result.panel.IsValid()) { result.panel = {}; result.scale = 0.0f; }
		return result;
	}

	inline Result PlaceTopRight(const Rect& requestedPicture, const Rect& output,
		float panelWidth, float panelHeight,
		float insetPixels = kDefaultInsetPixels, float maxScale = 1.0f,
        float horizontalInsetPixels = -1.0f)
	{
		Result result = Place(requestedPicture, output, panelWidth, panelHeight,
			insetPixels, maxScale, horizontalInsetPixels);
		if (!result.visiblePicture.IsValid() || !result.panel.IsValid())
			return result;
		const float height = result.panel.Height();
		float top = result.visiblePicture.top +
            (std::isfinite(insetPixels) ? std::max(0.0f, insetPixels) : 0.0f);
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
		float insetPixels = kDefaultInsetPixels,
		float leftInsetPixels = -1.0f, float maxScale = 1.0f,
        float horizontalInsetPixels = -1.0f)
	{
		Result result = PlaceTopRight(requestedPicture, output, panelWidth,
			panelHeight, insetPixels, maxScale, horizontalInsetPixels);
		if (!result.visiblePicture.IsValid() || !result.panel.IsValid())
			return result;
		const float width = result.panel.Width();
		const float requestedLeftInset = std::isfinite(leftInsetPixels) && leftInsetPixels >= 0.0f
            ? leftInsetPixels : (std::isfinite(insetPixels) ? insetPixels : 0.0f);
		float left = result.visiblePicture.left +
			std::max(0.0f, requestedLeftInset);
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
