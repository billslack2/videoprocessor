#include <pch.h>

#include "AlphaSourceCropPolicy.h"


namespace AlphaSourceCrop
{
	namespace
	{
		ActivePictureBounds FullRaster(int width, int height)
		{
			ActivePictureBounds bounds;
			bounds.right = width;
			bounds.bottom = height;
			bounds.rasterWidth = width;
			bounds.rasterHeight = height;
			bounds.aspectRatio = width > 0 && height > 0
				? static_cast<double>(width) / height : 0.0;
			bounds.symmetricBars = true;
			return bounds;
		}

		bool ValidBounds(const ActivePictureBounds& bounds, int width, int height)
		{
			return width > 0 && height > 0 &&
				bounds.rasterWidth == width && bounds.rasterHeight == height &&
				bounds.left >= 0 && bounds.top >= 0 &&
				bounds.right <= width && bounds.bottom <= height &&
				bounds.left < bounds.right && bounds.top < bounds.bottom;
		}

		bool CropEdgesAreChromaAligned(
			const ActivePictureBounds& bounds, int width, int height)
		{
			const bool horizontalCrop = bounds.left > 0 || bounds.right < width;
			const bool verticalCrop = bounds.top > 0 || bounds.bottom < height;
			return (!horizontalCrop ||
				((bounds.left & 1) == 0 && (bounds.right & 1) == 0)) &&
				(!verticalCrop ||
				((bounds.top & 1) == 0 && (bounds.bottom & 1) == 0));
		}
	}

	Decision Evaluate(const Input& input)
	{
		Decision decision;
		decision.sourceBounds = FullRaster(input.rasterWidth, input.rasterHeight);
		if (!input.automaticCropEnabled)
		{
			decision.reason = "automatic crop is off; preserving full raster";
			return decision;
		}
		if (input.subtitleDisplacementActive)
		{
			decision.reason =
				"subtitle displacement is active; expanding to full raster";
			return decision;
		}
		if (!input.sharedGeometryAvailable)
		{
			decision.reason = "shared trusted geometry is unavailable";
			return decision;
		}
		if (input.geometrySourceGeneration == 0 ||
			input.geometrySourceGeneration != input.frameSourceGeneration)
		{
			decision.reason = "shared geometry belongs to a stale source generation";
			return decision;
		}
		if (input.classification !=
			ActivePictureClassification::BAR_CROP_TRUSTED)
		{
			decision.reason = input.classification ==
				ActivePictureClassification::FULL_RASTER_TRUSTED
				? "shared authority is full raster"
				: "shared geometry lacks crop authority";
			return decision;
		}
		if (!input.latestObservationSupportsCrop)
		{
			decision.reason =
				"latest observation does not reaffirm crop authority";
			return decision;
		}
		if (!input.geometry.symmetricBars)
		{
			decision.reason =
				"shared crop bounds lack opposing-edge authority";
			return decision;
		}
		if (!ValidBounds(input.geometry, input.rasterWidth, input.rasterHeight))
		{
			decision.reason = "shared crop bounds do not match the current raster";
			return decision;
		}
		if (!CropEdgesAreChromaAligned(
			input.geometry, input.rasterWidth, input.rasterHeight))
		{
			decision.reason = "shared crop bounds are not chroma aligned";
			return decision;
		}

		decision.sourceBounds = input.geometry;
		decision.applyCrop = true;
		decision.reason = "generation-current shared crop authority accepted";
		return decision;
	}
}
